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
 * host does the transfer at once (the buffer is filled when sceCdRead
 * returns, which no caller can observe before syncing) and models the
 * completion as follows:
 *   - a non-blocking poll (sceCdSync with an odd mode) reports the command
 *     busy until the next simulated vsync (ico_cdvd_host_vsync, registered
 *     with the host loop), so the background reader's cdWait sleeps once,
 *     as it did while the drive worked;
 *   - a blocking wait (sceCdSync with an even mode) from a game thread
 *     blocks that thread until the same vsync, as libcdvd's does
 *     (sce/libcdvd/cdvd000.c: sceCdSync(0) loops on sceCdDelayThread,
 *     which is CreateSema, SetAlarm and WaitSema): while the drive works
 *     the other threads run, the same-priority Main among them.  An
 *     earlier version completed the command on the spot, assuming the
 *     waiter's order of events did not depend on it; it does: a stage
 *     load then ran start to end without a Main tick, and the stage-load
 *     thread clipped against a collision list (fumi/src/fieldCollision.c
 *     colObjList) that Main had built before StageManager removed every
 *     object (docs/port/DATA.md, "Timing").  From the host context (tests) the
 *     wait still completes at once.
 * The drive never reports a tray-open or not-ready state while a disc
 * image is mounted.
 */
#include "cdvd_host.h"
#include "clock.h"
#include "iop_ram.h"
#include "vfs.h"
#include <eekernel.h>
#include <libcdvd.h>
#include "sched.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The host loop's vsync callback list (package 1B, port/platform/
   host_loop.h).  The fallback keeps this file building without it: then
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

static struct {
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
    int stPaused;
    int stBufMax;
    uint32_t stLsn;

    IcoCdClockFn clock;
} cd;

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
            fprintf(stderr, "cdvd: no disc: cannot open %s (set ICO_ISO to the SCES-50760 image)\n",
                    path);
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
                    strcpy(cd.bootName, base);
                    cd.diskType = ICO_CD_TYPE_PS2DVD;
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
}

int ico_cdvd_host_busy(void)
{
    return cd.busy;
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

/* One read command: the transfer happens now, the completion at the next
   vsync (see the file comment). */
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
    } else if (sectors != 0 && ico_vfs_read_sectors(vfs, lsn, sectors, dst) != 0) {
        uint32_t vol = ico_vfs_volume_sectors(vfs);

        cd.error = (lsn >= vol || sectors > vol - lsn) ? ICO_CD_ERR_END : ICO_CD_ERR_READ;
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
   is always full and sceCdStStat reports its size. */

int sceCdStInit(int bufmax, int bankmax, void *buf)
{
    (void)bankmax;
    (void)buf;
    cd.stInit = 1;
    cd.stActive = 0;
    cd.stPaused = 0;
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
    cd.stPaused = 0;
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
    if (n == 0 || ico_vfs_read_sectors(vfs, cd.stLsn, n, buf) != 0) {
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
    cd.stPaused = 0;
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
    cd.stPaused = 1;
    return 1;
}

int sceCdStResume(void)
{
    if (!cd.stActive) {
        return 0;
    }
    cd.stPaused = 0;
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
