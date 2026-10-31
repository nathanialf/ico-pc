/*
 * port/data/cdvd_host.c
 *
 * libcdvd on the host: the sceCd* subset fumi/ios/cdvd.c and seki/src/
 * FileManager.c call, over the VFS disc (vfs.h).  The game's own layer
 * above it (the cdvd manager thread, its request queue, the background
 * request table, the stream manager and the directory cache) is unchanged.
 *
 * Timing.  On the PS2, sceCdRead and sceCdReadIOPm start a transfer and
 * return; sceCdSync(1) polls it and sceCdSync(0) blocks until it ends.  The
 * host starts the transfer at once: on the disc reader thread (the window
 * build's default, below; the buffer fills while the game runs on, which
 * no caller can observe before syncing), or on the calling thread itself
 * ([dev] disc_reader = sync, the tests and tools, no thread: the buffer is
 * filled when sceCdRead returns).  It models the completion as follows:
 *   - a non-blocking poll (sceCdSync with an odd mode) reports the command
 *     busy until the next simulated vsync (ico_cdvd_host_vsync, registered
 *     with the host loop), so the background reader's cdWait sleeps once,
 *     as it did while the drive worked;
 *   - a blocking wait (sceCdSync with an even mode) from a game thread
 *     blocks that thread until the same vsync, as libcdvd's does
 *     (sce/libcdvd/cdvd000.c: sceCdSync(0) loops on sceCdDelayThread,
 *     which is CreateSema, SetAlarm and WaitSema): while the drive works
 *     the other threads run, the same-priority Main among them.  The
 *     waiter's order of events depends on it: completed on the spot, a
 *     stage load would run start to end without a Main tick, and the
 *     stage-load thread would clip against a collision list
 *     (fumi/src/fieldCollision.c colObjList) that Main had built before
 *     StageManager removed every object.  From the host context (tests) the
 *     wait still completes at once.
 * The drive never reports a tray-open or not-ready state while a disc
 * image is mounted.
 *
 * The disc reader thread (issue 126: 16-sector reads of 16 to 123 ms on a
 * reporter's z: drive, each one a late step on the game thread).  A read
 * command is queued to the thread and sceCdRead returns; the command still
 * completes at the next simulated vsync and never earlier: cd_complete,
 * first in ico_cdvd_host_vsync, waits there for a read the thread has not
 * finished (the step's disc wait, IcoCdvdStats.wait_ns) and sets
 * sceCdGetError's code as the read on the calling thread would, so the
 * code is final when busy clears.  sceCdSync's host-context completion,
 * sceCdBreak (the read finished, then ABORT), the reset, the eject and a
 * new reader choice complete the command the same way first.  The
 * read-ahead ([dev] disc_read_ahead_kb, 256 KB by default with the thread):
 * after each command the thread reads on from where it ended, in chunks
 * of CD_RA_CHUNK sectors that a new command stops, into a buffer of the
 * disc's next sectors; a command or a stream read inside it is copied from
 * there on the calling thread at once, any other is read from the disc.
 * The image's or archive's FILE* is shared: every read of it goes through
 * ico_vfs_read_sectors, under the mount's mutex (vfs.c).  Host state only:
 * nothing of the reader is in a save state, which is taken only while the
 * drive is idle (no command in flight, ico_cdvd_host_idle).
 */
#include "cdvd_host.h"
#include "archive.h"
#include "clock.h"
#include "host_fs.h"
#include "ico_thread.h"
#include "iop_ram.h"
#include "vfs.h"
#include <eekernel.h>
#include <libcdvd.h>
#include "sched.h"
#include "state_port.h"
#include "state_registry.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The host loop's vsync callback list (port/platform/host_loop.h).  The fallback keeps this file building without it: then
   sceCdInit registers nothing and whoever drives the vsyncs must call
   ico_cdvd_host_vsync itself. */
#if defined(__has_include)
#if __has_include("host_loop.h")

#include "host_loop.h"

#define ICO_CDVD_HAVE_HOST_LOOP 1
#endif
#endif

/* The record sceCdSearchFile fills, as libcdvd lays it out: 0x24 bytes.
   Callers' own declarations are at least this large (cdvd.c's is 0x24 with
   the flag word named `reserved`, FileManager.c's 0x30) and read only lsn
   and size. */
typedef struct {
    uint32_t lsn;
    uint32_t size;
    char name[16];
    uint8_t date[8];
    uint32_t flag;
} CdlFileHost;

/* The record sceCdReadClock fills: a status byte and BCD fields
   (layout_action.c and GsBase.c declare the same body). */
typedef struct {
    uint8_t stat;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t pad;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} CdClockHost;

_Static_assert(sizeof(CdlFileHost) == 0x24, "sceCdlFILE is 0x24 bytes");

_Static_assert(sizeof(CdClockHost) == 8, "sceCdCLOCK is 8 bytes");
_Static_assert(sizeof(CdClockHost) == sizeof(IcoClockBcd), "clock.h has the same record");

/* the stream commands sceCdStream takes (libcdvd's numbering) */
enum {
    ST_CMD_START = 1,
    ST_CMD_READ = 2,
    ST_CMD_STOP = 3,
    ST_CMD_SEEK = 4,
    ST_CMD_INIT = 5,
    ST_CMD_STAT = 6,
    ST_CMD_PAUSE = 7,
    ST_CMD_RESUME = 8,
};

static struct CdHost {
    int busy;             /* a non-blocking command waits for the next vsync */
    int syncSema;         /* sceCdSync(0)'s waiters (ids > 0; 0 = not made) */
    int syncWaiters;      /* threads blocked on it */
    unsigned int reads;   /* read commands started (diagnostics) */
    unsigned int sectors; /* sectors read */
    uint32_t lastLsn;     /* the last command's first sector */
    int error;            /* sceCdGetError's value */
    int vsyncHooked;
    int defaultTried;
    IcoVfs *owned; /* a disc this layer mounted */

    /* the identification of the disc in the drive */
    IcoVfs *checkedDisc;
    int diskType;
    char bootName[32];

    /* the stream */
    int stInit;
    int stActive;
    int stBufMax;
    uint32_t stLsn;

    IcoCdClockFn clock;
} cd;

/* --- the disc reader ------------------------------------------------------- */

#define CD_RA_DEFAULT_KB 256 /* [dev] disc_read_ahead_kb's default with the thread */
#define CD_RA_MAX_KB 4096
#define CD_RA_CHUNK 32       /* the read-ahead's sectors a backend read */
#define CD_SLOW_READ_MS 50.0 /* a read that took longer is logged */

/* The command the thread reads (one at a time: busy refuses a second). */
typedef struct CdReq {
    IcoVfs *vfs;
    uint32_t lsn, n;
    void *dst;
    int result; /* ico_vfs_read_sectors's: 0, or -1 */
    int done;
    int hit;       /* copied from the read-ahead */
    double t0, t1; /* queued, done (ico_thread_now_ms) */
} CdReq;

static struct CdReader {
    int windowed;   /* ico_cdvd_host_set_windowed: the thread and read-ahead by default */
    int askThread;  /* ico_cdvd_host_set_reader: -1 [dev] or the default, 0 sync, 1 thread */
    int askAheadKb; /* ico_cdvd_host_set_reader: -1 [dev] or the default, else KB */
    int chosen;     /* reader_choose ran (sceCdInit) */
    int thread;     /* reads on the thread (else on the calling thread) */
    uint32_t ahead; /* the read-ahead's sectors (0: off; the thread only) */
    char line[256]; /* the start line written last */
    IcoMutex *lock; /* everything below, the thread's state and the counters */
    IcoCond *work;  /* a command or a read-ahead fill was asked for, or stop */
    IcoCond *done;  /* the thread finished a command or a fill chunk */
    IcoThread *worker;
    int stop;
    int halt;     /* reader_drain: a fill under way ends after its chunk */
    int queued;   /* req waits for the thread */
    int reading;  /* the thread is reading (a command or the read-ahead) */
    int inFlight; /* req queued and not completed yet (cd_complete; the game thread's) */
    CdReq req;
    /* the read-ahead: sectors [raLsn, raLsn + raCount) of raVfs in ra
       (the thread writes past raCount outside the lock, so the copies under
       it read below raCount only) */
    unsigned char *ra;
    IcoVfs *raVfs;
    uint32_t raLsn, raCount;
    int fill; /* a fill asked for: fillVfs from fillLsn */
    IcoVfs *fillVfs;
    uint32_t fillLsn;
    /* the counters (IcoCdvdStats) */
    unsigned int hits, waits, slowReads;
    unsigned long long readNs, waitNs, waitMaxNs, bytes;
} rdr = {.askThread = -1, .askAheadKb = -1};

static void rd_lock(void)
{
    if (rdr.lock != NULL) {
        ico_mutex_lock(rdr.lock);
    }
}

static void rd_unlock(void)
{
    if (rdr.lock != NULL) {
        ico_mutex_unlock(rdr.lock);
    }
}

static unsigned long long ns_of(double ms)
{
    return ms > 0.0 ? (unsigned long long)(ms * 1e6) : 0ull;
}

/* the read-ahead holds [lsn, lsn + n) of v (the lock held) */
static int ra_covers(const IcoVfs *v, uint32_t lsn, uint32_t n)
{
    return rdr.ra != NULL && v == rdr.raVfs && lsn >= rdr.raLsn && n <= rdr.raCount &&
           lsn - rdr.raLsn <= rdr.raCount - n;
}

/* [lsn, lsn + n) copied from the read-ahead when it holds them (the lock
   held): 1, else 0 */
static int ra_take(const IcoVfs *v, uint32_t lsn, uint32_t n, void *dst)
{
    if (!ra_covers(v, lsn, n)) {
        return 0;
    }
    memcpy(dst, rdr.ra + (size_t)(lsn - rdr.raLsn) * ICO_VFS_SECTOR, (size_t)n * ICO_VFS_SECTOR);
    return 1;
}

/* After a read of n sectors that ended at next: the thread reads on from
   there unless the read-ahead already holds the next n (the lock held) */
static void ra_ask(IcoVfs *v, uint32_t next, uint32_t n)
{
    if (rdr.ahead == 0 || rdr.worker == NULL || ra_covers(v, next, n)) {
        return;
    }
    rdr.fill = 1;
    rdr.fillVfs = v;
    rdr.fillLsn = next;
    ico_cond_broadcast(rdr.work);
}

/* The thread: the read-ahead filled from fillLsn, rdr.ahead sectors at
   most and never past the volume's end, CD_RA_CHUNK at a time; a command
   queued meanwhile ends it after the chunk being read (the lock held on
   entry and return) */
static void ra_fill(void)
{
    IcoVfs *v = rdr.fillVfs;
    const uint32_t lsn = rdr.fillLsn;
    const uint32_t vol = ico_vfs_volume_sectors(v);
    const uint32_t total = lsn < vol ? (vol - lsn < rdr.ahead ? vol - lsn : rdr.ahead) : 0;

    rdr.fill = 0;
    if (total == 0) {
        return; /* at the volume's end: what it holds stays */
    }
    rdr.raVfs = v;
    rdr.raLsn = lsn;
    rdr.raCount = 0;
    rdr.reading = 1;
    while (rdr.raCount < total && !rdr.queued && !rdr.stop && !rdr.halt) {
        const uint32_t c = total - rdr.raCount < CD_RA_CHUNK ? total - rdr.raCount : CD_RA_CHUNK;
        unsigned char *at = rdr.ra + (size_t)rdr.raCount * ICO_VFS_SECTOR;
        int r;

        ico_mutex_unlock(rdr.lock);
        r = ico_vfs_read_sectors(v, lsn + rdr.raCount, c, at);
        ico_mutex_lock(rdr.lock);
        if (r != 0) {
            break; /* what it holds so far stays */
        }
        rdr.raCount += c;
        rdr.bytes += (unsigned long long)c * ICO_VFS_SECTOR;
    }
    rdr.reading = 0;
    ico_cond_broadcast(rdr.done);
}

/* The thread: the queued command, from the read-ahead or the disc (the
   lock held on entry and return) */
static void serve(void)
{
    CdReq *q = &rdr.req;

    rdr.queued = 0;
    rdr.reading = 1;
    if (ra_take(q->vfs, q->lsn, q->n, q->dst)) {
        q->hit = 1;
        q->result = 0;
    } else {
        const double t = ico_thread_now_ms();
        int r;

        ico_mutex_unlock(rdr.lock);
        r = ico_vfs_read_sectors(q->vfs, q->lsn, q->n, q->dst);
        ico_mutex_lock(rdr.lock);
        q->result = r;
        rdr.readNs += ns_of(ico_thread_now_ms() - t);
        if (r == 0) {
            rdr.bytes += (unsigned long long)q->n * ICO_VFS_SECTOR;
        }
    }
    q->t1 = ico_thread_now_ms();
    q->done = 1;
    rdr.reading = 0;
    ico_cond_broadcast(rdr.done);
    if (q->result == 0) {
        ra_ask(q->vfs, q->lsn + q->n, q->n);
    }
}

static void reader_main(void *arg)
{
    (void)arg;
    ico_mutex_lock(rdr.lock);
    for (;;) {
        while (!rdr.stop && !rdr.queued && !rdr.fill) {
            ico_cond_wait(rdr.work, rdr.lock);
        }
        if (rdr.stop) {
            break;
        }
        if (rdr.queued) {
            serve();
        } else {
            ra_fill();
        }
    }
    ico_mutex_unlock(rdr.lock);
}

/* sceCdGetError's code for a failed read of [lsn, lsn + n) */
static int read_error(IcoVfs *vfs, uint32_t lsn, uint32_t n)
{
    const uint32_t vol = ico_vfs_volume_sectors(vfs);

    return (lsn >= vol || n > vol - lsn) ? ICO_CD_ERR_END : ICO_CD_ERR_READ;
}

/* A read that took CD_SLOW_READ_MS or longer: the first 8, then every
   100th (on the game thread, at its completion) */
static void slow_read(IcoVfs *vfs, uint32_t lsn, uint32_t n, double tookMs, const char *how,
                      double waitedMs)
{
    const char *loc = ico_vfs_location(vfs);

    rdr.slowReads++;
    if (rdr.slowReads > 8 && rdr.slowReads % 100 != 0) {
        return;
    }
    if (waitedMs >= 0.0) {
        fprintf(stderr,
                "cdvd: read of %u sectors at lsn %u took %.0f ms on %s (%s; the game thread "
                "waited %.0f ms of it)\n",
                n, lsn, tookMs, loc != NULL ? loc : "?", how, waitedMs);
    } else {
        fprintf(stderr, "cdvd: read of %u sectors at lsn %u took %.0f ms on %s (%s)\n", n, lsn,
                tookMs, loc != NULL ? loc : "?", how);
    }
}

/* A read on the calling thread (the sync reader, a stream read the
   read-ahead does not hold): the game thread waits the whole of it */
static int direct_read(IcoVfs *vfs, uint32_t lsn, uint32_t n, void *dst, const char *how)
{
    const double t = ico_thread_now_ms();
    const int r = ico_vfs_read_sectors(vfs, lsn, n, dst);
    const double ms = ico_thread_now_ms() - t;
    const unsigned long long ns = ns_of(ms);

    rd_lock();
    rdr.readNs += ns;
    rdr.waitNs += ns;
    rdr.waits++;
    rdr.waitMaxNs = ns > rdr.waitMaxNs ? ns : rdr.waitMaxNs;
    if (r == 0) {
        rdr.bytes += (unsigned long long)n * ICO_VFS_SECTOR;
    }
    rd_unlock();
    if (ms >= CD_SLOW_READ_MS) {
        slow_read(vfs, lsn, n, ms, how, -1.0);
    }
    return r;
}

/* The command in flight on the thread completed: its read waited for when
   it has not ended (the step's disc wait), sceCdGetError's code set as
   start_read sets it on the calling thread.  Nothing when none is. */
static void cd_complete(void)
{
    CdReq *q = &rdr.req;
    double waited = 0.0;

    if (!rdr.inFlight) {
        return;
    }
    ico_mutex_lock(rdr.lock);
    if (!q->done) {
        const double t = ico_thread_now_ms();

        while (!q->done) {
            ico_cond_wait(rdr.done, rdr.lock);
        }
        waited = ico_thread_now_ms() - t;
        rdr.waits++;
        rdr.waitNs += ns_of(waited);
        rdr.waitMaxNs = ns_of(waited) > rdr.waitMaxNs ? ns_of(waited) : rdr.waitMaxNs;
    }
    if (q->hit) {
        rdr.hits++;
    }
    ico_mutex_unlock(rdr.lock);
    rdr.inFlight = 0;
    cd.error = q->result != 0 ? read_error(q->vfs, q->lsn, q->n) : ICO_CD_ERR_NONE;
    if (q->t1 - q->t0 >= CD_SLOW_READ_MS) {
        slow_read(q->vfs, q->lsn, q->n, q->t1 - q->t0,
                  q->hit          ? "from the read-ahead"
                  : rdr.ahead > 0 ? "read-ahead miss"
                                  : "no read-ahead",
                  waited);
    }
}

/* The thread idle and the read-ahead emptied (before a disc goes, the
   reader changes or the drive is reset): the command in flight completed
   first */
static void reader_drain(void)
{
    cd_complete();
    if (rdr.lock == NULL) {
        return;
    }
    ico_mutex_lock(rdr.lock);
    rdr.fill = 0;
    rdr.halt = 1;
    while (rdr.queued || rdr.reading) {
        ico_cond_wait(rdr.done, rdr.lock);
    }
    rdr.halt = 0;
    rdr.raVfs = NULL;
    rdr.raCount = 0;
    ico_mutex_unlock(rdr.lock);
}

static void reader_stop(void)
{
    reader_drain();
    if (rdr.worker != NULL) {
        ico_mutex_lock(rdr.lock);
        rdr.stop = 1;
        ico_cond_broadcast(rdr.work);
        ico_mutex_unlock(rdr.lock);
        ico_thread_join(rdr.worker);
        rdr.worker = NULL;
        rdr.stop = 0;
    }
    free(rdr.ra);
    rdr.ra = NULL;
    rdr.ahead = 0;
    rdr.thread = 0;
}

/* The lock, the conditions, the read-ahead's buffer and the thread: 1, or
   0 when any could not be made (the reads stay on the calling thread) */
static int reader_start(uint32_t ahead)
{
    if (rdr.lock == NULL) {
        rdr.lock = ico_mutex_create();
        rdr.work = ico_cond_create();
        rdr.done = ico_cond_create();
        if (rdr.lock == NULL || rdr.work == NULL || rdr.done == NULL) {
            ico_cond_destroy(rdr.work);
            ico_cond_destroy(rdr.done);
            ico_mutex_destroy(rdr.lock);
            rdr.lock = NULL;
            rdr.work = rdr.done = NULL;
            return 0;
        }
    }
    if (ahead > 0) {
        rdr.ra = malloc((size_t)ahead * ICO_VFS_SECTOR);
        if (rdr.ra == NULL) {
            ahead = 0;
        }
    }
    rdr.ahead = ahead;
    rdr.worker = ico_thread_create(reader_main, NULL);
    if (rdr.worker == NULL) {
        free(rdr.ra);
        rdr.ra = NULL;
        rdr.ahead = 0;
        return 0;
    }
    return 1;
}

/* the start line's words for the game data's drive: "the game data on a
   fixed drive (NTFS)" */
static void drive_words(char *out, size_t size)
{
    const char *loc = ico_vfs_location(ico_vfs_disc());
    char kind[16], fs[32], paren[40] = "";
    const char *on;

    if (loc == NULL) {
        snprintf(out, size, "no game data in the drive yet");
        return;
    }
    if (ico_host_drive_kind(loc, kind, sizeof(kind)) != 0) {
        snprintf(kind, sizeof(kind), "unknown");
    }
    on = strcmp(kind, "fixed") == 0       ? "on a fixed drive"
         : strcmp(kind, "removable") == 0 ? "on a removable drive"
         : strcmp(kind, "network") == 0   ? "on a network drive"
         : strcmp(kind, "RAM disk") == 0  ? "on a RAM disk"
         : strcmp(kind, "CD-ROM") == 0    ? "on a CD or DVD drive"
         : strcmp(kind, "fuse") == 0      ? "on a user-space file system"
                                          : "on a drive of an unknown kind";
    if (ico_host_drive_fs_name(loc, fs, sizeof(fs)) == 0) {
        snprintf(paren, sizeof(paren), " (%s)", fs);
    }
    snprintf(out, size, "the game data %s%s", on, paren);
}

/* The reader as [dev] disc_reader (ICO_DISC_READER: thread or sync) and
   [dev] disc_read_ahead_kb (ICO_DISC_READ_AHEAD_KB: 0 to 4096) say, or
   ico_cdvd_host_set_reader; without them the thread with 256 KB of
   read-ahead in the window build (ico_cdvd_host_set_windowed), else the
   calling thread with none.  The line when it changes. */
static void reader_choose(void)
{
    const char *envReader = getenv("ICO_DISC_READER");
    const char *envAhead = getenv("ICO_DISC_READ_AHEAD_KB");
    char drive[96], line[sizeof(rdr.line)];
    const char *why = "";
    int thread;
    long kb;

    reader_stop();
    rdr.chosen = 1;
    if (rdr.askThread >= 0) {
        thread = rdr.askThread;
        why = " (set by the program)";
    } else if (envReader != NULL && envReader[0] != '\0') {
        thread = strcmp(envReader, "thread") == 0;
        why = thread ? "" : " ([dev] disc_reader=sync)";
    } else {
        thread = rdr.windowed;
        why = thread ? "" : " (no window)";
    }
    if (rdr.askAheadKb >= 0) {
        kb = rdr.askAheadKb;
    } else if (envAhead != NULL && envAhead[0] != '\0') {
        kb = strtol(envAhead, NULL, 10);
    } else {
        kb = thread && rdr.windowed ? CD_RA_DEFAULT_KB : 0;
    }
    kb = kb < 0 ? 0 : kb > CD_RA_MAX_KB ? CD_RA_MAX_KB : kb;
    if (thread && !reader_start((uint32_t)(kb * 1024 / ICO_VFS_SECTOR))) {
        thread = 0;
        why = " (no thread: the OS refused one)";
    }
    rdr.thread = thread;
    drive_words(drive, sizeof(drive));
    if (thread && rdr.ahead > 0) {
        snprintf(line, sizeof(line), "cdvd: disc reads on a worker thread%s, read-ahead %u KB; %s",
                 why, (unsigned)(rdr.ahead * ICO_VFS_SECTOR / 1024), drive);
    } else if (thread) {
        snprintf(line, sizeof(line), "cdvd: disc reads on a worker thread%s, read-ahead off; %s",
                 why, drive);
    } else {
        snprintf(line, sizeof(line), "cdvd: disc reads on the game thread%s; %s", why, drive);
    }
    if (strcmp(line, rdr.line) != 0) {
        snprintf(rdr.line, sizeof(rdr.line), "%s", line);
        fprintf(stderr, "%s\n", line);
    }
}

void ico_cdvd_host_set_windowed(int windowed)
{
    rdr.windowed = windowed != 0;
    if (rdr.chosen) {
        reader_choose();
    }
}

void ico_cdvd_host_set_reader(int thread, int aheadKb)
{
    rdr.askThread = thread < 0 ? -1 : thread != 0;
    rdr.askAheadKb = aheadKb < 0 ? -1 : aheadKb;
    reader_choose();
}

int ico_cdvd_host_reader_thread(void)
{
    return rdr.thread;
}

/* The save state's blocks (state_port.h): the drive's command state (the
   sync semaphore's id is the scheduler's, saved with it) and the stream.
   The record's host objects stay: the disc this layer mounted, the
   identification of the disc in the drive (the same disc, refused
   otherwise) and the clock callback. */
#define CD_SPAN(first, last)                                                                       \
    offsetof(struct CdHost, last) + sizeof cd.last - offsetof(struct CdHost, first)

int ico_cdvd_host_state_register(void)
{
    int rc = 0;

    rc |= ico_state_register("cdvd.drive", &cd.busy, CD_SPAN(busy, error), 0);
    rc |= ico_state_register("cdvd.stream", &cd.stInit, CD_SPAN(stInit, stLsn), 0);
    return rc != 0 ? -1 : 0;
}

#undef CD_SPAN

/* --- the drive ------------------------------------------------------------- */

const char *ico_cdvd_host_default_iso(void)
{
    const char *env = getenv("ICO_ISO");

    if (env != NULL && env[0] != '\0') {
        return env;
    }
    return "baserom/Ico_PAL.iso";
}

int ico_cdvd_host_mount_iso(const char *iso_path)
{
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, iso_path);

    if (vfs == NULL) {
        return -1;
    }
    ico_cdvd_host_eject();
    cd.owned = vfs;
    ico_vfs_set_disc(vfs);
    return 0;
}

void ico_cdvd_host_eject(void)
{
    /* the thread done with the disc before it goes */
    reader_drain();
    if (cd.owned != NULL) {
        ico_vfs_unmount(cd.owned); /* also detaches it as the disc */
        cd.owned = NULL;
    }
    cd.checkedDisc = NULL;
}

/* The disc in the drive, mounting the default image the first time none
   is. */
static IcoVfs *disc(void)
{
    IcoVfs *vfs = ico_vfs_disc();

    if (vfs == NULL && !cd.defaultTried) {
        const char *path = ico_cdvd_host_default_iso();

        cd.defaultTried = 1;
        if (ico_cdvd_host_mount_iso(path) != 0) {
            fprintf(stderr, "cdvd: no disc: cannot open %s (set ICO_ISO to the %s or %s image)\n",
                    path, ico_discs[0].id, ico_discs[1].id);
        }
        vfs = ico_vfs_disc();
    }
    return vfs;
}

/* Read SYSTEM.CNF's BOOT2 line and check the file it names. */
static void identify(IcoVfs *vfs)
{
    char cnf[1024];
    IcoVfsFile f;
    int64_t n;
    char *line;

    if (cd.checkedDisc == vfs) {
        return;
    }
    cd.checkedDisc = vfs;
    cd.diskType = ICO_CD_TYPE_NODISC;
    cd.bootName[0] = '\0';
    if (vfs == NULL || ico_vfs_open(vfs, "SYSTEM.CNF", &f) != 0) {
        return;
    }
    n = ico_vfs_read(&f, 0, cnf, sizeof(cnf) - 1);
    if (n <= 0) {
        return;
    }
    cnf[n] = '\0';
    for (line = cnf; line != NULL && *line != '\0';) {
        char *next = strpbrk(line, "\r\n");
        char *eq;

        if (next != NULL) {
            *next++ = '\0';
        }
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        eq = strchr(line, '=');
        if (strncmp(line, "BOOT2", 5) == 0 && eq != NULL) {
            char norm[ICO_VFS_PATH_MAX];
            char *v = eq + 1;
            char *end;
            const char *base;

            while (*v == ' ' || *v == '\t') {
                v++;
            }
            end = v + strlen(v);
            while (end > v && (end[-1] == ' ' || end[-1] == '\t')) {
                *--end = '\0';
            }
            if (ico_vfs_normalize(v, norm, sizeof(norm)) == 0 &&
                ico_vfs_stat(vfs, norm, NULL) == 0) {
                base = strrchr(norm, '/');
                base = base != NULL ? base + 1 : norm;
                if (strlen(base) < sizeof(cd.bootName)) {
                    const IcoDiscInfo *row = ico_disc_by_boot(base);

                    strcpy(cd.bootName, base);
                    /* the drive's answer for the disc: the Japanese
                       release is a CD, which its cdvd.c waits for (18) */
                    cd.diskType = row != NULL ? row->cd_type : ICO_CD_TYPE_PS2DVD;
                }
            }
            break;
        }
        line = next;
    }
}

const char *ico_cdvd_host_boot_name(void)
{
    IcoVfs *vfs = disc();

    identify(vfs);
    return cd.bootName[0] != '\0' ? cd.bootName : NULL;
}

void ico_cdvd_host_vsync(void *ctx)
{
    (void)ctx;
    /* the read on the thread finished (waited for here when it has not)
       and its error set before busy clears */
    cd_complete();
    cd.busy = 0;
    /* the command has ended: release sceCdSync(0)'s waiters (interrupt
       code, as libcdvd's alarm callback) */
    while (cd.syncWaiters > 0) {
        cd.syncWaiters--;
        iSignalSema(cd.syncSema);
    }
}

void ico_cdvd_host_stats(IcoCdvdStats *out)
{
    out->busy = cd.busy;
    out->waiters = cd.syncWaiters;
    out->reads = cd.reads;
    out->sectors = cd.sectors;
    out->last_lsn = cd.lastLsn;
    out->stream_active = cd.stActive;
    out->stream_lsn = cd.stLsn;
    rd_lock();
    out->hits = rdr.hits;
    out->waits = rdr.waits;
    out->read_ns = rdr.readNs;
    out->wait_ns = rdr.waitNs;
    out->wait_max_ns = rdr.waitMaxNs;
    out->bytes = rdr.bytes;
    rd_unlock();
}

void ico_cdvd_host_bg_retry(const char *name, unsigned int lsn, int error)
{
    static unsigned int retries;

    retries++;
    if (retries > 8 && retries % 100 != 0) {
        return;
    }
    if (error == 0) {
        fprintf(stderr,
                "cdvd: background read of %s at lsn %u retried (the drive was not ready; retry "
                "%u)\n",
                name != NULL ? name : "?", lsn, retries);
    } else {
        fprintf(stderr, "cdvd: background read of %s at lsn %u retried (read error %d; retry %u)\n",
                name != NULL ? name : "?", lsn, error, retries);
    }
}

void ico_cdvd_host_pack_load(const char *name, unsigned int lsn, unsigned int size, int members,
                             int inflating)
{
    static unsigned int loads;

    loads++;
    if (loads > 64 && loads % 100 != 0) {
        return;
    }
    fprintf(stderr, "cdvd: pack %s at lsn %u, %u bytes on the disc%s: %d members (load %u)\n",
            name != NULL ? name : "?", lsn, size, inflating ? " (deflated)" : "", members, loads);
}

int ico_cdvd_host_busy(void)
{
    return cd.busy;
}

int ico_cdvd_host_idle(void)
{
    return !cd.busy && cd.syncWaiters == 0 && !cd.stActive && !rdr.inFlight;
}

int ico_cdvd_host_vsync_hooked(void)
{
    return cd.vsyncHooked;
}

void ico_cdvd_host_set_clock_source(IcoCdClockFn fn)
{
    cd.clock = fn;
}

void ico_cdvd_host_reset(void)
{
    /* the thread stopped (the command in flight completed first) and the
       reader forgotten with the rest */
    reader_stop();
    ico_mutex_destroy(rdr.lock);
    ico_cond_destroy(rdr.work);
    ico_cond_destroy(rdr.done);
    memset(&rdr, 0, sizeof(rdr));
    rdr.askThread = rdr.askAheadKb = -1;
    ico_cdvd_host_eject();
#ifdef ICO_CDVD_HAVE_HOST_LOOP
    if (cd.vsyncHooked) {
        ico_host_on_vsync_unregister(ico_cdvd_host_vsync, NULL);
    }
#endif
    memset(&cd, 0, sizeof(cd));
}

static void hook_vsync(void)
{
    if (cd.vsyncHooked) {
        return;
    }
#ifdef ICO_CDVD_HAVE_HOST_LOOP
    ico_host_on_vsync_register(ico_cdvd_host_vsync, NULL);
    cd.vsyncHooked = 1;
#endif
}

/* The command to the thread: copied from the read-ahead at once when it
   holds the sectors (nothing in flight), else queued (cd_complete ends
   it) */
static void queue_read(IcoVfs *vfs, uint32_t lsn, uint32_t sectors, void *dst)
{
    CdReq *q = &rdr.req;

    ico_mutex_lock(rdr.lock);
    if (ra_take(vfs, lsn, sectors, dst)) {
        rdr.hits++;
        ra_ask(vfs, lsn + sectors, sectors);
        ico_mutex_unlock(rdr.lock);
        return;
    }
    memset(q, 0, sizeof(*q));
    q->vfs = vfs;
    q->lsn = lsn;
    q->n = sectors;
    q->dst = dst;
    q->t0 = ico_thread_now_ms();
    rdr.queued = 1;
    rdr.inFlight = 1;
    ico_cond_broadcast(rdr.work);
    ico_mutex_unlock(rdr.lock);
}

/* One read command: the transfer starts now (on the thread, or here), the
   completion at the next vsync (see the file comment). */
static int start_read(uint32_t lsn, uint32_t sectors, void *dst)
{
    IcoVfs *vfs = disc();

    if (cd.busy) {
        return 0; /* a command is in flight: the caller retries */
    }
    cd.error = ICO_CD_ERR_NONE;
    if (vfs == NULL) {
        cd.error = ICO_CD_ERR_NODISC;
    } else if (dst == NULL) {
        cd.error = ICO_CD_ERR_ADDRESS;
    } else if (sectors != 0 && rdr.worker != NULL) {
        queue_read(vfs, lsn, sectors, dst);
    } else if (sectors != 0 && direct_read(vfs, lsn, sectors, dst, "on the game thread") != 0) {
        cd.error = read_error(vfs, lsn, sectors);
    }
    cd.busy = 1;
    cd.reads++;
    cd.sectors += sectors;
    cd.lastLsn = lsn;
    return 1;
}

/* --- libcdvd --------------------------------------------------------------- */

int sceCdInit(int mode)
{
    (void)mode;
    disc();
    hook_vsync();
    if (!rdr.chosen) {
        reader_choose(); /* the disc is in the drive now: its drive in the line */
    }
    return 1;
}

int sceCdMmode(int media)
{
    (void)media;
    return 1;
}

int sceFsReset(void)
{
    return 1;
}

int sceCdDiskReady(int mode)
{
    (void)mode;
    return disc() != NULL ? ICO_CD_READY_COMPLETE : ICO_CD_READY_NOT_READY;
}

int sceCdStatus(void)
{
    return disc() != NULL ? ICO_CD_STAT_PAUSE : ICO_CD_STAT_STOP;
}

int sceCdGetDiskType(void)
{
    IcoVfs *vfs = disc();

    identify(vfs);
    return cd.diskType;
}

int sceCdGetError(void)
{
    return cd.error;
}

int sceCdBreak(void)
{
    if (cd.busy) {
        /* the read on the thread finished first: nothing is written into
           the buffer after the break */
        cd_complete();
        cd.busy = 0;
        cd.error = ICO_CD_ERR_ABORT;
    }
    return 1;
}

int sceCdSearchFile(struct sceCdlFILE *fp, const char *name)
{
    CdlFileHost rec;
    IcoVfsEntry e;
    IcoVfs *vfs = disc();
    size_t n;

    if (fp == NULL || name == NULL || ico_vfs_stat(vfs, name, &e) != 0 || e.is_dir) {
        return 0;
    }
    memset(&rec, 0, sizeof(rec));
    rec.lsn = e.lsn;
    rec.size = e.size;
    n = strlen(e.name);
    memcpy(rec.name, e.name, n < sizeof(rec.name) ? n : sizeof(rec.name) - 1);
    /* the recording date as libcdvd orders it: a reserved byte, second,
       minute, hour, day, month and the year as a 16-bit number */
    rec.date[1] = e.date[5];
    rec.date[2] = e.date[4];
    rec.date[3] = e.date[3];
    rec.date[4] = e.date[2];
    rec.date[5] = e.date[1];
    rec.date[6] = (uint8_t)((1900 + e.date[0]) & 0xFF);
    rec.date[7] = (uint8_t)((1900 + e.date[0]) >> 8);
    memcpy(fp, &rec, sizeof(rec));
    return 1;
}

int sceCdRead(int lsn, int sectors, void *buf, CdRMode *mode)
{
    (void)mode;
    if (lsn < 0 || sectors < 0) {
        return 0;
    }
    return start_read((uint32_t)lsn, (uint32_t)sectors, buf);
}

int sceCdReadIOPm(int lsn, int sectors, void *buf, CdRMode *mode)
{
    uint32_t iop = (uint32_t)(uintptr_t)buf;
    void *dst = NULL;

    (void)mode;
    if (lsn < 0 || sectors < 0) {
        return 0;
    }
    /* in 64 bits: sectors * 2048 can pass 4 GB and wrap into a length
       ico_iop_range_ok would accept */
    const uint64_t len = (uint64_t)(uint32_t)sectors * ICO_VFS_SECTOR;

    if (len <= UINT32_MAX && ico_iop_range_ok(iop, (uint32_t)len)) {
        dst = ico_iop_ptr(iop);
    } else {
        fprintf(stderr, "cdvd: sceCdReadIOPm to 0x%08x (%d sectors) is outside IOP RAM\n",
                (unsigned)iop, sectors);
    }
    return start_read((uint32_t)lsn, (uint32_t)sectors, dst);
}

int sceCdSync(int mode)
{
    if ((mode & 1) == 0) {
        /* blocking: a game thread waits for the vsync that ends the
           command (file comment); the host context cannot wait */
        if (cd.busy && cd.vsyncHooked && ico_sched_in_thread()) {
            if (cd.syncSema <= 0) {
                struct SemaParam p;

                memset(&p, 0, sizeof(p));
                p.initCount = 0;
                p.maxCount = ICO_SCHED_MAX_THREADS;
                cd.syncSema = CreateSema(&p);
            }
            while (cd.busy && cd.syncSema > 0) {
                cd.syncWaiters++;
                WaitSema(cd.syncSema);
            }
        }
        /* the host context completes at once: the read on the thread
           waited for (a game thread's wait ended at the vsync, which
           completed it) */
        cd_complete();
        cd.busy = 0;
        return 0;
    }
    return cd.busy;
}

int sceCdSyncS(int mode)
{
    return sceCdSync(mode);
}

static void fill_clock(CdClockHost *c)
{
    if (cd.clock != NULL) { /* a test's source, in decimal */
        IcoCdClockTime t = {2002, 1, 1, 0, 0, 0};
        IcoClockBcd b;

        cd.clock(&t);
        ico_clock_pack(&b, t.year, t.month, t.day, t.hour, t.minute, t.second);
        memcpy(c, &b, sizeof(*c));
        return;
    }
    {
        IcoClockBcd b;

        ico_clock_now(&b);
        memcpy(c, &b, sizeof(*c));
    }
}

int sceCdReadClock(struct sceCdCLOCK *clock)
{
    CdClockHost c;

    if (clock == NULL) {
        return 0;
    }
    fill_clock(&c);
    memcpy(clock, &c, sizeof(c));
    return 1;
}

/* --- the stream ------------------------------------------------------------ */
/* The PS2 streams through a ring in IOP memory that the drive keeps full;
   the host reads the requested sectors straight from the disc, so the ring
   is always full and sceCdStStat reports its size.  Pause and resume are
   accepted and change nothing: a paused stream still reads. */

int sceCdStInit(int bufmax, int bankmax, void *buf)
{
    (void)bankmax;
    (void)buf;
    cd.stInit = 1;
    cd.stActive = 0;
    cd.stBufMax = bufmax;
    return 1;
}

int sceCdStStart(int lsn, CdRMode *mode)
{
    (void)mode;
    if (!cd.stInit || lsn < 0 || disc() == NULL) {
        return 0;
    }
    cd.stLsn = (uint32_t)lsn;
    cd.stActive = 1;
    cd.error = ICO_CD_ERR_NONE;
    return 1;
}

int sceCdStRead(int sectors, void *buf, int mode, int *err)
{
    IcoVfs *vfs = disc();
    uint32_t vol;
    uint32_t n;

    (void)mode;
    if (err != NULL) {
        *err = ICO_CD_ERR_NONE;
    }
    if (!cd.stActive || vfs == NULL || sectors <= 0 || buf == NULL) {
        return 0;
    }
    vol = ico_vfs_volume_sectors(vfs);
    n = (uint32_t)sectors;
    if (cd.stLsn >= vol) {
        n = 0;
    } else if (n > vol - cd.stLsn) {
        n = vol - cd.stLsn;
    }
    if (n != 0 && rdr.worker != NULL) {
        /* from the read-ahead when it holds them; either way the thread
           reads on from the stream's next sector */
        int hit;

        ico_mutex_lock(rdr.lock);
        hit = ra_take(vfs, cd.stLsn, n, buf);
        rdr.hits += hit ? 1u : 0u;
        ico_mutex_unlock(rdr.lock);
        if (!hit && direct_read(vfs, cd.stLsn, n, buf, "a stream read on the game thread") != 0) {
            if (err != NULL) {
                *err = ICO_CD_ERR_READ;
            }
            return 0;
        }
        ico_mutex_lock(rdr.lock);
        ra_ask(vfs, cd.stLsn + n, n);
        ico_mutex_unlock(rdr.lock);
        cd.stLsn += n;
        return (int)n;
    }
    if (n == 0 || direct_read(vfs, cd.stLsn, n, buf, "a stream read on the game thread") != 0) {
        if (err != NULL) {
            *err = n == 0 ? ICO_CD_ERR_END : ICO_CD_ERR_READ;
        }
        return 0;
    }
    cd.stLsn += n;
    return (int)n;
}

int sceCdStStat(void)
{
    return cd.stActive ? cd.stBufMax : 0;
}

int sceCdStStop(void)
{
    cd.stActive = 0;
    return 1;
}

int sceCdStSeek(int lsn)
{
    if (!cd.stActive || lsn < 0) {
        return 0;
    }
    cd.stLsn = (uint32_t)lsn;
    return 1;
}

int sceCdStPause(void)
{
    if (!cd.stActive) {
        return 0;
    }
    return 1;
}

int sceCdStResume(void)
{
    if (!cd.stActive) {
        return 0;
    }
    return 1;
}

int sceCdStream(int lsn, int sectors, void *buf, int cmd, CdRMode *mode)
{
    int err;

    switch (cmd) {
    case ST_CMD_START:
        return sceCdStStart(lsn, mode);
    case ST_CMD_READ:
        return sceCdStRead(sectors, buf, 1, &err);
    case ST_CMD_STOP:
        return sceCdStStop();
    case ST_CMD_SEEK:
        return sceCdStSeek(lsn);
    case ST_CMD_INIT:
        return sceCdStInit(sectors, 0, buf);
    case ST_CMD_STAT:
        return sceCdStStat();
    case ST_CMD_PAUSE:
        return sceCdStPause();
    case ST_CMD_RESUME:
        return sceCdStResume();
    default:
        return 0;
    }
}
