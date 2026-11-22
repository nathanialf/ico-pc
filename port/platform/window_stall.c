/*
 * port/platform/window_stall.c
 *
 * The window's lines around a present that blocked (window_stall.h).
 */
#include "window_stall.h"
#include <stdio.h>
#include <string.h>
#include "cpu_place.h"
#include "diag_host.h"
#include "gpu_sysfs.h"
#include "ico_thread.h"
#include "present_hist.h"
#include "present_latency.h"
#include "present_stall_block.h"
#include "rd_perf_clock.h" /* RD_RESET_* */
#include "rd_perf_rare.h"
#include "rd.h"            /* rd_hold_replay_kind */
#include "render_thread.h" /* ico_render_thread_tick */
#include "rhi.h"
#include "thermal_text.h"
#include "log_cost.h"
#include "memory_floor.h"
#include "perf_secs.h" /* each second's GPU times and present intervals */
#include "power_read.h"
#include "stall_coincide.h"
#include "vmstat_read.h"
#include "window_lines.h"   /* window_log_cost_present */
#include "window_present.h" /* window_refresh_ns */
#include "sys_cell.h"
#include <errno.h>
#include <stdlib.h>
#ifdef __ANDROID__
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <sys/system_properties.h>
#include <unistd.h>
#include "gpu_sysfs_android.h"
#include "host_android.h"
#endif

/* the present that blocked, waiting for the record after it */
static struct {
    int pending;
    unsigned replay;
    double ms;
} s_after;

static void stall_rec(const RdPerfRecord *r, PresentStallRec *o)
{
    memset(o, 0, sizeof(*o));
    o->replay = r->replay;
    o->presentMs = r->presentMs;
    o->split = r->presentSplit; /* 0, 1 or 2 (no present call made) */
    o->lockMs = r->presentLockMs;
    o->callMs = r->presentCallMs;
    o->pollMs = r->presentPollMs;
    o->waitMs = r->waitMs;
    o->fenceMs = r->fenceWaitMs;
    o->acquireMs = r->acquireMs;
    o->gpuValid = r->gpuValid != 0;
    o->gpuMs = r->gpuMs;
    o->gapValid = r->gpuGapValid != 0;
    o->gapMs = (double)r->gpuGapNs / 1e6;
    o->gapSkipped = r->gpuGapSkipped;
    o->gapBackMs = (double)r->gpuGapBackNs / 1e6;
}

const char *window_stall_split(const RdPerfRecord *r, unsigned blockGpuN, double blockGpuMs)
{
    static char text[448];
    PresentStallRec o;

    stall_rec(r, &o);
    present_split_text(&o, blockGpuN, blockGpuMs, text, sizeof(text));
    return text;
}

/* The presents that blocked waiting for the kernel GPU driver's sample
   after them (window_stall_poll), oldest first; a full queue drops the
   oldest.  s_gpuOn: the nodes are sampled */
#define AROUND_MAX 8
#define AROUND_WAIT_MS 1000.0 /* past the present's end: written without the after sample */

static struct {
    unsigned n;

    struct {
        unsigned replay;
        double t0Ms, t1Ms;
        int core; /* the game thread's core as the record popped (-1 not known) */
    } q[AROUND_MAX];
} s_around;

static int s_gpuOn;
#ifdef __ANDROID__
static double s_gpuBlockMs = -1.0; /* the last block line's time, ico_thread_now_ms */
#endif

/* the stalls per block and per run (present_stall_block.h); s_stallT0 the
   block's start, ico_thread_now_ms (0: the first record not yet seen) */
static StallBlock s_stall;
static double s_stallT0;

/* the display's rate now (SDL's current mode, as the 10 s lines read it),
   0 not known */
static double display_hz_now(void)
{
    const Uint64 ns = window_refresh_ns(0);

    return ns > 0 ? 1e9 / (double)ns : 0.0;
}

static void arm_block_end(void); /* the bounded pass timing, below */

/* What a stall coincided with (stall_coincide.h): the block's pass
   medians, its tally, the GPU's fastest clock (the open line's, -1 not
   known) */
static StallMedians s_med;
static StallTally s_tally;
static StallLineBudget s_lines; /* the run's per-stall lines (stall_coincide.h) */

/* the last records that showed the GPU's own sign of a reset (rd.h
   RdPerfRecord.gpuResetSign, rd_perf_clock.h rd_perf_reset_sign), for a
   stall at them or up to two replays after (issue 123: the hung replay's
   stamps, the next one's restarted clock, the present that blocked
   between): the last hang (stamps unwritten), the last other sign counted
   (its first list run twice, the counter gone back with the GPU kept
   busy), the last counter restart after a span with no GPU work (not a
   sign: said, not counted) */
static struct {
    uint32_t replay;
    unsigned missing;
    char first[48];
} s_hang;

static struct {
    uint32_t replay;
    bool twice, back;
} s_sign;

static struct {
    uint32_t replay;
    double ms;
} s_idle;

/* The last timed record's pieces against the block's medians (issue 179's
   classes, stall_coincide.h stall_pieces_judge): the GPU times its record
   carries on every timed replay (each list, each effect, the VU pre-pass,
   the stretch before list 0, the present), each with its median in the
   block before it was added; its GPU idle before (gpuGapNs) and its VU
   triangles with their median.  Taken by window_stall_after, which sees
   every record before a stall of it is judged (window_host.c perf_drain:
   window_stall_after, then the present's stall, then the passes) */
#define PIECE_KEY 0x40000000u /* the record's pieces' keys: apart from the passes' */
#define PIECE_TRIS (PIECE_KEY + 0xFFFFu)
#define PIECES (RD_LIST_COUNT + RD_PERF_POST_COUNT + 3)

static struct {
    uint32_t replay; /* 0 none */
    unsigned n;
    double ms[PIECES], med[PIECES];
    int what[PIECES]; /* 0..RD_LIST_COUNT-1 a list, then the effects, then the three */
    bool idleKnown;
    double idleMs;
    bool trisKnown;
    double tris, trisMed;
    double trisMax;   /* the most of the stage's 2 s before it, -1 none */
    double red, pres; /* the reduction's and the present pass's GPU ms, -1 not timed */
} s_pc;

/* the block's stalls against the pieces' shares of the GPU time and
   their table (stall_coincide.h StallTable); the VU triangles of the
   stage's last 2 s; the stage now; the piece the last moment judged grew
   most (an index of s_pc's, -1 none) */
static StallTable s_table;
static StallTris s_tris;
static int s_stageNow = STALL_NO_STAGE;
static int s_lastPiece = -1;
#define TRIS_SPAN_MS 2000.0

_Static_assert(PIECES <= STALL_SHARE_PIECES, "the table's pieces");

static void piece_name(int what, char *out, size_t cap)
{
    if (what < RD_LIST_COUNT) {
        snprintf(out, cap, "list %d", what);
    } else if (what < RD_LIST_COUNT + RD_PERF_POST_COUNT) {
        snprintf(out, cap, "%s", rd_perf_post_name(what - RD_LIST_COUNT));
    } else {
        static const char *const k_rest[3] = {"the VU pre-pass", "before list 0", "the present"};
        snprintf(out, cap, "%s", k_rest[what - RD_LIST_COUNT - RD_PERF_POST_COUNT]);
    }
}

static void piece_add(int what, double ms)
{
    if (!(ms > 0.0) || s_pc.n >= PIECES) {
        return;
    }
    s_pc.ms[s_pc.n] = ms;
    s_pc.med[s_pc.n] = stall_medians_add(&s_med, PIECE_KEY + (uint32_t)what, ms);
    s_pc.what[s_pc.n] = what;
    s_pc.n++;
}

/* the pieces' names by index, for the share and table lines */
static const char *const *piece_names(void)
{
    static char names[PIECES][32];
    static const char *ptr[STALL_SHARE_PIECES];
    if (ptr[0] == NULL) {
        for (int i = 0; i < PIECES; i++) {
            piece_name(i, names[i], sizeof names[i]);
            ptr[i] = names[i];
        }
    }
    return ptr;
}

/* a record's pieces, idle time and triangles (every record: the medians;
   the triangles of the stage's 2 s before it; a timed one's pieces into
   the block's shares and the waiting stall rows) */
static void pieces_take(const RdPerfRecord *r)
{
    s_pc.replay = r->replay;
    s_pc.n = 0;
    s_pc.idleKnown = r->gpuValid && r->gpuGapValid;
    s_pc.idleMs = (double)r->gpuGapNs / 1e6;
    s_pc.tris = (double)r->vuTris;
    s_pc.trisMed = stall_medians_add(&s_med, PIECE_TRIS, (double)r->vuTris);
    s_pc.trisKnown = s_pc.trisMed >= 0.0;
    s_pc.trisMax = stall_tris_max(&s_tris, r->startMs, TRIS_SPAN_MS);
    stall_tris_add(&s_tris, r->startMs, (double)r->vuTris);
    s_pc.red = r->gpuValid ? r->gpuPostMs[RD_PERF_POST_REDUCTION] : -1.0;
    s_pc.pres = r->gpuValid ? r->gpuPresentMs : -1.0;
    if (!r->gpuValid) {
        return;
    }
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        piece_add(l, r->gpuListMs[l]);
    }
    for (int e = 0; e < RD_PERF_POST_COUNT; e++) {
        piece_add(RD_LIST_COUNT + e, r->gpuPostMs[e]);
    }
    piece_add(RD_LIST_COUNT + RD_PERF_POST_COUNT, r->gpuPrepassMs);
    piece_add(RD_LIST_COUNT + RD_PERF_POST_COUNT + 1, r->gpuUploadMs);
    piece_add(RD_LIST_COUNT + RD_PERF_POST_COUNT + 2, r->gpuPresentMs);
    stall_table_timed(&s_table, r->replay, r->gpuMs, s_pc.what, s_pc.ms, s_pc.n, s_pc.red,
                      s_pc.pres);
}

/* the stalled replay's pieces into m (when the last record taken is it) */
static void moment_pieces(StallMoment *m, uint32_t replay)
{
    if (s_pc.replay != replay || replay == 0) {
        return;
    }
    const int at = stall_pieces_judge(m, s_pc.ms, s_pc.med, s_pc.n);
    if (at >= 0) {
        piece_name(s_pc.what[at], m->pieceName, sizeof(m->pieceName));
        s_lastPiece = at;
    }
    m->idleKnown = s_pc.idleKnown;
    m->idleBeforeMs = s_pc.idleMs;
    m->trisKnown = s_pc.trisKnown;
    m->tris = s_pc.tris;
    m->trisMedian = s_pc.trisMed;
}

/* the last records' submissions (rd_perf_sub.h), for the per-stall line:
   the stalled replay's and the two before it (one more kept) */
#define SUB_RECS 4

typedef struct SubRec {
    uint32_t replay;
    double firstMs, signalMs;
    uint8_t doneValid, subN, subAll, hung;
    RdPerfSub sub[RD_PERF_SUBS];
} SubRec;

static SubRec s_subs[SUB_RECS];

static unsigned s_subsAt;

/* the block's present latency (present_latency.h) */
static PresentLatency s_lat;

static int s_gpuMaxMhz = -1;

/* the block's stalls against the pieces' shares and every stall's row
   (stall_coincide.h); all: the run's end, every row written */
static void stall_table_lines(bool all)
{
    static char text[3072];
    const char *const *names = piece_names();
    if (stall_share_text(&s_table, names, text, sizeof text) > 0) {
        fprintf(stderr, "window: %s\n", text);
    }
    for (unsigned part = 0; stall_table_text(&s_table, names, all, part, text, sizeof text) > 0;
         part++) {
        fprintf(stderr, "window: %s\n", text);
    }
    stall_table_end(&s_table, all);
}

/* the block's stall line and its join into the run's sums; clock the
   kernel driver's GPU clock in the block, NULL when not read; all: the
   run's end */
static void stall_block_close(const StallBlockClock *clock, bool all)
{
    const double now = ico_thread_now_ms();
    char text[2048];

    if (s_stallT0 <= 0.0) {
        return; /* no record yet: nothing presented, nothing to say */
    }
    if (stall_block_end(&s_stall, (now - s_stallT0) / 1000.0, display_hz_now(), clock, text,
                        sizeof(text))) {
        fprintf(stderr, "window: %s\n", text);
    }
    if (present_latency_text(&s_lat, text, sizeof(text)) > 0) {
        fprintf(stderr, "window: %s\n", text);
    }
    memset(&s_lat, 0, sizeof(s_lat));
    stall_table_lines(all);
    s_stallT0 = now;
    rd_perf_rare_block_end(); /* a stalled replay's rare pipelines: counted per block */
    arm_block_end();
    stall_medians_reset(&s_med); /* the passes' medians are the block's */
}

void window_stall_stage(int stage)
{
    static int s_stage;
    static bool s_known;
    if (s_known && stage != s_stage) {
        stall_medians_reset(&s_med); /* the new stage's own medians */
        stall_tris_reset(&s_tris);   /* and its own 2 s of triangles */
    }
    s_stageNow = stage;
    s_stage = stage;
    s_known = true;
}

void window_stall_run_summary(void)
{
    char text[1536];

    stall_block_close(NULL, true); /* the last part block (its clock not read) */
    if (stall_block_run_text(&s_stall, text, sizeof(text)) > 0) {
        fprintf(stderr, "window: %s\n", text);
    }
}

static void around_queue(const RdPerfRecord *r)
{
    if (!s_gpuOn || !(r->presentEndMs > 0.0) || !present_stall_lines(r->presentMs)) {
        return;
    }
    if (s_around.n == AROUND_MAX) {
        memmove(&s_around.q[0], &s_around.q[1], sizeof(s_around.q[0]) * (AROUND_MAX - 1));
        s_around.n--;
    }
    s_around.q[s_around.n].replay = r->replay;
    s_around.q[s_around.n].t0Ms = r->presentEndMs - r->presentMs;
    s_around.q[s_around.n].t1Ms = r->presentEndMs;
    s_around.q[s_around.n].core = cpu_place_now();
    s_around.n++;
}

/* The asks the game thread must not make (sys_cell.h): on Android the log
 * thread (host_android.c ico_android_log_mirror_set_tick, the one worker
 * every Android start has: the GPU sampler runs only where a node is
 * readable, the performance hints' thread only from API 33) asks the
 * thermal state (two JNI calls into PowerManager, binder calls: 1.6-2.8 ms
 * of the owner's Pixel's 10 s print, rc4-rc9) and reads /proc/self/status
 * and /proc/meminfo once a second; the window's lines read the last
 * answers and their age.  Without the log thread they are asked where they
 * were, on the game thread (never both: ico_host_thermal keeps statics). */
static SysCell s_thermalCell, s_memoryCell;
/* the log thread's other reads (stall_coincide.h, window_lines.c's
   memory line): /proc/pressure/memory's avg10s (some, full, the some of
   the read before; ok 1, or 0 with its errno), and the block's memory
   extremes since the window last took them (the device's least available,
   the reads under the floor of all, the process's resident range and its
   anonymous, file-backed and shared parts' ranges, the C heap in use and
   mapped at the last read) */
static SysCell s_memBlockCell;
#ifdef __ANDROID__
static SysCell s_pressureCell; /* written and read on Android alone */
#endif
static uint32_t s_memBlockGen; /* __atomic: the window took the block's extremes */
#ifdef __ANDROID__
static const char *s_thermalWhy; /* __atomic: a static string (Android's asks only) */
#endif
static int s_asksOn;     /* the tick is set: the game thread asks nothing */
static uint32_t s_asksN; /* __atomic: the tick's runs since the window took them */

#ifdef __ANDROID__
/* a small file's text into buf (NUL-ended): its length, or -errno */
static int read_small(const char *path, char *buf, size_t cap)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    const ssize_t n = read(fd, buf, cap - 1u);
    const int e = errno;
    close(fd);
    if (n < 0) {
        return -e;
    }
    buf[n] = '\0';
    return (int)n;
}

/* the floor as the start-up set reads it (window_pipe_finish.c's), MB */
static long floor_mb(long totalMb)
{
    long setting = ico_memory_floor_setting(getenv("ICO_PIPELINE_MEMORY_FLOOR_MB"));
    if (setting == -2) {
        setting = -1;
    }
    return ico_memory_floor_mb(totalMb, setting, ico_diag_memory_floor_driver());
}

/* the log thread's: the pressure, the status's parts, the C heap and the
   block's extremes (asks_tick, after the memory figures) */
static void asks_more(long rssKb, long availKb, long totalKb, bool haveDev, double atMs)
{
    static double s_some = -1.0;
    static uint32_t s_gen;
    static double ext[16];
    static bool s_have;
    /* /proc/self/status reaches VmSwap past 1 KB on Android (its Groups
       line alone can be long) */
    char buf[4096];
    double some = 0.0, full = 0.0;
    const int n = read_small("/proc/pressure/memory", buf, sizeof buf);
    if (n > 0 && stall_pressure_parse(buf, (size_t)n, &some, &full)) {
        const double pv[4] = {some, full, s_some, 1.0};
        sys_cell_put(&s_pressureCell, atMs, 0.0, pv, 4);
        s_some = some;
    } else {
        const double pv[4] = {0.0, 0.0, -1.0, (double)(n < 0 ? n : -EINVAL)};
        sys_cell_put(&s_pressureCell, atMs, 0.0, pv, 4);
    }
    long anon = -1, file = -1, shmem = -1, swap = -1;
    const int m = read_small("/proc/self/status", buf, sizeof buf);
    if (m <= 0 || !stall_status_parse(buf, (size_t)m, &anon, &file, &shmem)) {
        anon = file = shmem = -1;
    }
    if (m <= 0 || !stall_status_swap(buf, (size_t)m, &swap)) {
        swap = -1;
    }
    const struct mallinfo mi = mallinfo(); /* bionic: size_t fields */
    const uint32_t gen = __atomic_load_n(&s_memBlockGen, __ATOMIC_ACQUIRE);
    const double avail = haveDev ? (double)availKb : -1.0;
    const double underFloor =
        haveDev && totalKb > 0 && availKb / 1024 < floor_mb(totalKb / 1024) ? 1.0 : 0.0;
    const double now[6] = {avail, (double)rssKb, (double)anon, (double)file, (double)shmem, 0.0};
    if (!s_have || gen != s_gen) {
        /* a new block: from this read */
        s_gen = gen;
        s_have = true;
        ext[0] = now[0];
        for (int i = 0; i < 4; i++) {
            ext[2 + 2 * i] = ext[3 + 2 * i] = now[1 + i];
        }
        ext[10] = ext[11] = 0.0;
        ext[14] = ext[15] = (double)swap;
    }
    if (now[0] >= 0.0 && (ext[0] < 0.0 || now[0] < ext[0])) {
        ext[0] = now[0];
    }
    ext[1] = floor_mb(totalKb / 1024);
    for (int i = 0; i < 4; i++) {
        if (now[1 + i] >= 0.0) {
            ext[2 + 2 * i] =
                ext[2 + 2 * i] < 0.0 || now[1 + i] < ext[2 + 2 * i] ? now[1 + i] : ext[2 + 2 * i];
            ext[3 + 2 * i] = now[1 + i] > ext[3 + 2 * i] ? now[1 + i] : ext[3 + 2 * i];
        }
    }
    ext[10] += underFloor;
    ext[11] += 1.0;
    ext[12] = (double)mi.uordblks / 1048576.0;
    ext[13] = (double)mi.hblkhd / 1048576.0;
    if (swap >= 0) {
        ext[14] = ext[14] < 0.0 || (double)swap < ext[14] ? (double)swap : ext[14];
        ext[15] = (double)swap > ext[15] ? (double)swap : ext[15];
    }
    sys_cell_put(&s_memBlockCell, atMs, 0.0, ext, 16);
}

/* The battery and the kernel's memory events (power_read.h,
   vmstat_read.h), read once a second here on the log thread, read only:
   the battery's sysfs folder found at the first tick, else Android's
   BatteryManager, else nothing (decided once; a later failed read is
   silent: no sample).  The game thread's block lines read the cells:
   s_batNowCell {have, current mA, has current, voltage mV, has voltage,
   level, status}, s_batBlockCell {the block's first POWER_BLOCK_MAX
   currents, samples, currents, voltage least, most} from the read after
   the window took the last block's (s_batBlockGen), s_vmCell {the
   VMSTAT_FIELDS counts, the have bits, 1 or -errno} */
static SysCell s_batNowCell, s_batBlockCell, s_vmCell;
static uint32_t s_batBlockGen;    /* __atomic: the window took the block's readings */
static int s_batSource = -1;      /* __atomic: POWER_SOURCE_*, -1 not decided yet */
static char s_batPath[128];       /* written before s_batSource's release */
static int s_batSysErr;           /* likewise: the sysfs errno (0 none: no battery folder) */
static const char *s_batWhy = ""; /* likewise: why BatteryManager gave nothing */

/* the battery's folder under /sys/class/power_supply into s_batPath (the
   one named "battery" first, else the first whose type is Battery): 0
   found, else the errno met (0 with no battery) */
static int power_find(void)
{
    DIR *d = opendir("/sys/class/power_supply");
    if (d == NULL) {
        return errno != 0 ? errno : ENOENT;
    }
    const struct dirent *e;
    int err = 0;
    bool found = false;
    char path[192], buf[64];
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || strlen(e->d_name) > 64) {
            continue;
        }
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", e->d_name);
        const int n = read_small(path, buf, sizeof buf);
        if (n < 0) {
            err = -n;
            continue;
        }
        if (!power_read_is_battery(buf, (size_t)n)) {
            continue;
        }
        if (!found || strcmp(e->d_name, "battery") == 0) {
            snprintf(s_batPath, sizeof s_batPath, "/sys/class/power_supply/%s", e->d_name);
            found = true;
        }
    }
    closedir(d);
    return found ? 0 : err;
}

/* one sysfs file of the battery's folder into buf (NULL when not read;
   *err its errno) */
static const char *power_file(const char *name, char *buf, size_t cap, int *err)
{
    char path[192];
    snprintf(path, sizeof path, "%s/%s", s_batPath, name);
    const int n = read_small(path, buf, cap);
    if (n < 0) {
        *err = -n;
        return NULL;
    }
    return buf;
}

static bool power_sysfs(PowerSample *ps, int *err)
{
    char c[32], v[32], l[32], st[32];
    *err = 0;
    const char *cp = power_file("current_now", c, sizeof c, err);
    const char *vp = power_file("voltage_now", v, sizeof v, err);
    const char *lp = power_file("capacity", l, sizeof l, err);
    const char *sp = power_file("status", st, sizeof st, err);
    return power_from_sysfs(cp, vp, lp, sp, ps);
}

static bool power_android(PowerSample *ps)
{
    int v[4] = {INT_MIN, -1, -1, -1};
    return ico_android_battery(v) && power_from_android(v, ps);
}

static void power_tick(double atMs)
{
    static int s_source = -1;
    static PowerBlock s_block;
    static uint32_t s_gen;
    static bool s_have;
    PowerSample ps;
    bool ok = false;
    if (s_source < 0) {
        /* the first tick: where the battery can be read */
        int err = power_find();
        if (err == 0 && s_batPath[0] != '\0' && power_sysfs(&ps, &err)) {
            s_source = POWER_SOURCE_SYSFS;
            ok = true;
        } else {
            s_batSysErr = err;
            s_batPath[0] = '\0';
            if (power_android(&ps)) {
                s_source = POWER_SOURCE_ANDROID;
                ok = true;
            } else {
                s_source = POWER_SOURCE_NONE;
                s_batWhy = "no answer from the app's Java side";
            }
        }
        __atomic_store_n(&s_batSource, s_source, __ATOMIC_RELEASE);
    } else if (s_source == POWER_SOURCE_SYSFS) {
        int err;
        ok = power_sysfs(&ps, &err);
    } else if (s_source == POWER_SOURCE_ANDROID) {
        ok = power_android(&ps);
    }
    if (!ok) {
        return; /* silent: no sample this second */
    }
    const double now[7] = {1.0,
                           ps.currentMa,
                           ps.haveCurrent ? 1.0 : 0.0,
                           ps.voltageMv,
                           ps.haveVoltage ? 1.0 : 0.0,
                           (double)ps.level,
                           (double)ps.status};
    sys_cell_put(&s_batNowCell, atMs, 0.0, now, 7);
    const uint32_t gen = __atomic_load_n(&s_batBlockGen, __ATOMIC_ACQUIRE);
    if (!s_have || gen != s_gen) {
        s_gen = gen;
        s_have = true;
        power_block_reset(&s_block);
    }
    power_block_add(&s_block, &ps);
    double b[16];
    for (int i = 0; i < POWER_BLOCK_MAX; i++) {
        b[i] = s_block.currentMa[i];
    }
    b[12] = (double)s_block.n;
    b[13] = (double)s_block.currents;
    b[14] = s_block.vMinMv;
    b[15] = s_block.vMaxMv;
    sys_cell_put(&s_batBlockCell, atMs, 0.0, b, 16);
}

/* /proc/vmstat in full into buf (NUL-ended): its length, or -errno */
static int read_full(const char *path, char *buf, size_t cap)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    size_t at = 0;
    while (at + 1u < cap) {
        const ssize_t n = read(fd, buf + at, cap - 1u - at);
        if (n < 0) {
            const int e = errno;
            close(fd);
            return -e;
        }
        if (n == 0) {
            break;
        }
        at += (size_t)n;
    }
    close(fd);
    buf[at] = '\0';
    return (int)at;
}

static void vmstat_tick(double atMs)
{
    static char buf[16384]; /* the log thread's */
    static bool s_off, s_read;
    VmStat vs;
    double v[VMSTAT_FIELDS + 2];
    if (s_off) {
        return; /* not readable: said once, silent after */
    }
    const int n = read_full("/proc/vmstat", buf, sizeof buf);
    if (n <= 0 || !vmstat_read_parse(buf, (size_t)n, &vs)) {
        if (s_read) {
            return; /* a later failed read: silent, the last counts kept */
        }
        s_off = true;
        memset(v, 0, sizeof v);
        v[VMSTAT_FIELDS + 1] = n < 0 ? (double)n : -(double)EINVAL;
        sys_cell_put(&s_vmCell, atMs, 0.0, v, VMSTAT_FIELDS + 2);
        return;
    }
    unsigned bits = 0;
    for (int f = 0; f < VMSTAT_FIELDS; f++) {
        v[f] = (double)vs.v[f];
        bits |= vs.have[f] ? 1u << f : 0u;
    }
    v[VMSTAT_FIELDS] = (double)bits;
    v[VMSTAT_FIELDS + 1] = 1.0;
    s_read = true;
    sys_cell_put(&s_vmCell, atMs, 0.0, v, VMSTAT_FIELDS + 2);
}

static void asks_tick(void)
{
    if (ico_diag_watchdog_paused()) {
        return; /* the app in the background: nothing asked, as the GPU sampler */
    }
    double t0 = ico_thread_now_ms();
    const char *why = "";
    const ThermalState t = ico_host_thermal(&why);
    double t1 = ico_thread_now_ms();
    const double tv[3] = {t.headroom, (double)t.status, (double)t.recording};
    __atomic_store_n(&s_thermalWhy, why, __ATOMIC_RELEASE);
    sys_cell_put(&s_thermalCell, t1, t1 - t0, tv, 3);
    long rss = -1, peak = -1, avail = -1, total = -1;
    t0 = t1;
    ico_diag_process_memory(&rss, &peak);
    const double dev = ico_diag_device_memory(&avail, &total) == 0 ? 1.0 : 0.0;
    t1 = ico_thread_now_ms();
    const double mv[5] = {(double)rss, (double)peak, (double)avail, (double)total, dev};
    sys_cell_put(&s_memoryCell, t1, t1 - t0, mv, 5);
    asks_more(rss, avail, total, dev != 0.0, t1);
    power_tick(t1);           /* the battery (power_read.h) */
    vmstat_tick(t1);          /* the kernel's memory events (vmstat_read.h) */
    ico_render_thread_tick(); /* the threads' schedstat and list (render_thread.h) */
    __atomic_fetch_add(&s_asksN, 1u, __ATOMIC_RELAXED);
}
#endif

uint32_t window_stall_asks_take(double *thermalMs, double *memoryMs)
{
    double v[1], at, took;
    *thermalMs = sys_cell_get(&s_thermalCell, v, 1, &at, &took) ? took : 0.0;
    *memoryMs = sys_cell_get(&s_memoryCell, v, 1, &at, &took) ? took : 0.0;
    return __atomic_exchange_n(&s_asksN, 0u, __ATOMIC_RELAXED);
}

#ifdef __ANDROID__
/* the thermal state: the log thread's last answer (and its age) when the
   tick runs, else asked here as before */
static ThermalState thermal_now(const char **why, double *ageMs)
{
    *ageMs = -1.0;
    if (!s_asksOn) {
        return ico_host_thermal(why);
    }
    ThermalState t = thermal_state_none();
    double v[3], at;
    if (sys_cell_get(&s_thermalCell, v, 3, &at, NULL) == 3) {
        t.headroom = v[0];
        t.status = (int)v[1];
        t.recording = (int)v[2];
        *ageMs = ico_thread_now_ms() - at;
    }
    if (why != NULL) {
        const char *w = __atomic_load_n(&s_thermalWhy, __ATOMIC_ACQUIRE);
        *why = w != NULL ? w : "not asked yet";
    }
    return t;
}
#endif

#ifdef __ANDROID__
static bool battery_now_ma(double *ma)
{
    double v[7], at;
    if (!s_asksOn || sys_cell_get(&s_batNowCell, v, 7, &at, NULL) != 7 || v[2] == 0.0) {
        return false;
    }
    *ma = v[1];
    return true;
}

/* The block's battery and memory events lines (the game thread's, at the
   block's end; power_read.h, vmstat_read.h), each once said readable or
   not first */
static void power_block_lines(void)
{
    static bool s_batSaid, s_vmSaid, s_vmPrevHave;
    static VmStat s_vmPrev;
    char text[512];
    double v[16], at;
    if (!s_asksOn) {
        return;
    }
    const int source = __atomic_load_n(&s_batSource, __ATOMIC_ACQUIRE);
    if (source >= 0 && !s_batSaid) {
        s_batSaid = true;
        (void)power_source_text(source, s_batPath, s_batSysErr, s_batWhy, text, sizeof text);
        fprintf(stderr, "window: %s\n", text);
    }
    if (source > 0 && sys_cell_get(&s_batBlockCell, v, 16, &at, NULL) == 16) {
        __atomic_fetch_add(&s_batBlockGen, 1u, __ATOMIC_RELEASE); /* the next block from here */
        PowerBlock b;
        power_block_reset(&b);
        for (int i = 0; i < POWER_BLOCK_MAX; i++) {
            b.currentMa[i] = v[i];
        }
        b.n = (unsigned)v[12];
        b.currents = (unsigned)v[13];
        b.vMinMv = v[14];
        b.vMaxMv = v[15];
        double nv[7];
        if (sys_cell_get(&s_batNowCell, nv, 7, &at, NULL) == 7) {
            b.level = (int)nv[5];
            b.status = (int)nv[6];
        }
        if (power_block_text(&b, source, text, sizeof text) > 0) {
            fprintf(stderr, "window: %s\n", text);
        }
    }
    if (sys_cell_get(&s_vmCell, v, VMSTAT_FIELDS + 2, &at, NULL) != VMSTAT_FIELDS + 2) {
        return; /* not read yet */
    }
    if (v[VMSTAT_FIELDS + 1] <= 0.0) {
        if (!s_vmSaid) {
            s_vmSaid = true;
            fprintf(stderr,
                    "window: memory events: /proc/vmstat not readable (errno %d): no memory "
                    "events lines\n",
                    -(int)v[VMSTAT_FIELDS + 1]);
        }
        return;
    }
    if (!s_vmSaid) {
        s_vmSaid = true;
        fprintf(stderr, "window: memory events: /proc/vmstat read once a second on the log "
                        "thread (a block's line from the second block on)\n");
    }
    VmStat now;
    const unsigned bits = (unsigned)v[VMSTAT_FIELDS];
    for (int f = 0; f < VMSTAT_FIELDS; f++) {
        now.v[f] = (unsigned long long)v[f];
        now.have[f] = (bits >> f) & 1u;
    }
    if (s_vmPrevHave && vmstat_delta_text(&s_vmPrev, &now, text, sizeof text) > 0) {
        fprintf(stderr, "window: %s\n", text);
    }
    s_vmPrev = now;
    s_vmPrevHave = true;
}
#else
static bool battery_now_ma(double *ma)
{
    (void)ma;
    return false; /* read on Android alone */
}
#endif

bool window_stall_memory_block(WindowMemBlock *b)
{
    double v[16], at;
    memset(b, 0, sizeof(*b));
    if (!s_asksOn || sys_cell_get(&s_memBlockCell, v, 16, &at, NULL) != 16) {
        return false;
    }
    __atomic_fetch_add(&s_memBlockGen, 1u, __ATOMIC_RELEASE); /* the next block from here */
    b->minAvailMb = v[0] >= 0.0 ? (long)(v[0] / 1024.0) : -1;
    b->floorMb = (long)v[1];
    for (int i = 0; i < 4; i++) {
        b->lo[i] = v[2 + 2 * i] >= 0.0 ? (long)(v[2 + 2 * i] / 1024.0) : -1;
        b->hi[i] = v[3 + 2 * i] >= 0.0 ? (long)(v[3 + 2 * i] / 1024.0) : -1;
    }
    b->underFloor = (unsigned)v[10];
    b->reads = (unsigned)v[11];
    b->heapMb = v[12];
    b->mappedMb = v[13];
    b->swapLo = v[14] >= 0.0 ? (long)(v[14] / 1024.0) : -1;
    b->swapHi = v[15] >= 0.0 ? (long)(v[15] / 1024.0) : -1;
    return true;
}

int window_stall_memory(long *rss, long *peak, long *avail, long *total, double *ageMs)
{
    double v[5], at;
    if (!s_asksOn || sys_cell_get(&s_memoryCell, v, 5, &at, NULL) != 5) {
        return -1; /* read on the caller's thread */
    }
    *rss = (long)v[0];
    *peak = (long)v[1];
    *avail = v[4] != 0.0 ? (long)v[2] : -1;
    *total = v[4] != 0.0 ? (long)v[3] : -1;
    *ageMs = ico_thread_now_ms() - at;
    return 0;
}

/* what follows the around line's samples: the app's own words on the
   device's thermal state and screen recording where it reports them ("" on
   Android when it reports none, and everywhere else) */
static const char *around_suffix(void)
{
#ifdef __ANDROID__
    static char text[160];
    double age;
    ThermalState t = thermal_now(NULL, &age);

    thermal_around_text(&t, text, sizeof text);
    return text;
#else
    return "";
#endif
}

/* a core's clock at [t0Ms, t1Ms] from the kernel sampler's ring (the
   lowest during it, else the last before; -1 not known), and the GPU's */
static int core_mhz(double t0Ms, double t1Ms, int core, int *gpuMhz)
{
#ifdef __ANDROID__
    GpuSysfsAround a;
    if (core < 0 || !gpu_sysfs_android_around(t0Ms, t1Ms, core, &a)) {
        return -1;
    }
    if (gpuMhz != NULL) {
        *gpuMhz = a.during > 0 && a.duringClockMin > 0   ? a.duringClockMin
                  : a.hasBefore && a.before.clockMhz > 0 ? a.before.clockMhz
                                                         : -1;
    }
    if (a.during > 0 && a.duringCpuMin > 0) {
        return a.duringCpuMin;
    }
    return a.hasBefore && core < GPU_SYSFS_CPUS && a.before.cpuMhz[core] != 0
               ? (int)a.before.cpuMhz[core]
               : -1;
#else
    (void)t0Ms;
    (void)t1Ms;
    (void)core;
    (void)gpuMhz;
    return -1;
#endif
}

/* the submissions of the stalled replay and the two before it, oldest
   first, from the stall's start t0Ms (rd's clock: the records' times are
   the same CLOCK_MONOTONIC as the window's) */
static void moment_subs(StallMoment *m, double t0Ms, uint32_t replay)
{
    for (uint32_t back = 3; back-- > 0;) {
        if (replay < back + 1u) {
            continue;
        }
        const uint32_t want = replay - back;
        for (unsigned i = 0; i < SUB_RECS; i++) {
            const SubRec *e = &s_subs[i];
            if (e->replay != want || e->firstMs <= 0.0) {
                continue;
            }
            for (unsigned k = 0; k < e->subN && m->subs < STALL_SUBS; k++) {
                const RdPerfSub *u = &e->sub[k];
                StallSub *o = &m->sub[m->subs++];
                o->replay = e->replay;
                o->index = (uint8_t)(k + 1u);
                o->of = e->subAll;
                o->lists = u->lists;
                o->present = u->present;
                o->stalledOn = (uint8_t)(u->present && e->replay == replay);
                o->hung = (uint8_t)(e->hung == k + 1u);
                o->named = u->named;
                o->presentStamps = u->presentStamps;
                o->listLo = u->listLo;
                o->listHi = u->listHi;
                o->pieces = u->pieces;
                o->stamps = u->stamps;
                o->missing = u->missing;
                o->submitMs = e->firstMs + (double)u->submitMs - t0Ms;
                const bool last = k + 1u == e->subAll;
                o->signalKind = e->doneValid == 0 ? 0u : last ? e->doneValid : 3u;
                o->signalMs = e->doneValid != 0 ? e->signalMs - t0Ms : 0.0;
                /* the paused scene's hold (rd_hold.c): held or drawn in full */
                o->scene = (uint8_t)rd_hold_replay_kind(e->replay);
            }
            break;
        }
    }
}

/* the moment of a stall over [t0Ms, t1Ms] from the last samples (no file
   read here: the log thread's cells, the sampler's ring) */
static void moment_now(StallMoment *m, double t0Ms, double t1Ms, uint32_t replay)
{
    memset(m, 0, sizeof(*m));
    s_lastPiece = -1;
    if (s_hang.replay != 0 && replay >= s_hang.replay && replay - s_hang.replay <= 2u) {
        m->hangReplay = s_hang.replay;
        m->stampsMissing = s_hang.missing;
        memcpy(m->firstMissing, s_hang.first, sizeof(m->firstMissing));
    }
    if (s_sign.replay != 0 && replay >= s_sign.replay && replay - s_sign.replay <= 2u) {
        m->resetReplay = s_sign.replay;
        m->ranTwice = s_sign.twice;
        m->clockBack = s_sign.back;
    }
    if (s_idle.replay != 0 && replay >= s_idle.replay && replay - s_idle.replay <= 2u) {
        m->idleReplay = s_idle.replay;
        m->idleMs = s_idle.ms;
    }
    moment_subs(m, t0Ms, replay);
    moment_pieces(m, replay);
    m->gameCore = cpu_place_now();
    m->renderCore = ico_render_thread_render_core();
    m->gpuMhz = m->gpuMaxMhz = -1;
    m->gameMhz = core_mhz(t0Ms, t1Ms, m->gameCore, &m->gpuMhz);
    m->renderMhz = core_mhz(t0Ms, t1Ms, m->renderCore, NULL);
    m->gpuMaxMhz = s_gpuMaxMhz;
    m->haveWaits = ico_render_thread_last_second(m->waitMs, NULL);
    m->drivers = ico_render_thread_driver_threads(m->driverName, m->driverRunMs, m->driverWaitMs,
                                                  STALL_DRIVERS);
    long rss, peak, avail = -1, total = -1;
    double age;
    m->availMb = m->floorMb = -1;
    if (window_stall_memory(&rss, &peak, &avail, &total, &age) == 0 && avail >= 0) {
        m->availMb = avail / 1024;
#ifdef __ANDROID__
        m->floorMb = floor_mb(total / 1024);
#endif
    }
    m->pressure = -1;
    m->some10Before = -1.0;
    m->headroom = -1.0;
#ifdef __ANDROID__
    double pv[4], at;
    if (s_asksOn && sys_cell_get(&s_pressureCell, pv, 4, &at, NULL) == 4) {
        m->pressure = pv[3] > 0.0 ? 1 : 0;
        m->pressureErr = pv[3] > 0.0 ? 0 : -(int)pv[3];
        m->some10 = pv[0];
        m->full10 = pv[1];
        m->some10Before = pv[2];
    }
    {
        const ThermalState t = thermal_now(NULL, &age);
        m->headroom = t.headroom >= 0.0 ? t.headroom : -1.0;
    }
#endif
}

static bool battery_now_ma(double *ma); /* the last second's current (Android) */

/* the stall's row in the block's table (stall_coincide.h StallRow): kind
   0 a present of ms, 1 a pass of ms at passFactor its median */
static void stall_row(const StallMoment *m, uint32_t replay, int kind, double ms, double passFactor)
{
    StallRow row;
    memset(&row, 0, sizeof(row));
    row.replay = replay;
    row.stage = s_stageNow;
    row.kind = (uint8_t)kind;
    row.ms = (float)ms;
    row.passFactor = (float)passFactor;
    row.piece = -1;
    row.tris = row.trisMed = row.trisMax = -1.0f;
    row.red[0] = row.pres[0] = -1.0f;
    if (s_pc.replay == replay && replay != 0) {
        if (s_lastPiece >= 0 && (unsigned)s_lastPiece < s_pc.n) {
            row.piece = (int8_t)s_pc.what[s_lastPiece];
            row.pieceMs = (float)s_pc.ms[s_lastPiece];
            row.pieceFactor = (float)m->pieceFactor;
        }
        row.tris = (float)s_pc.tris;
        row.trisMed = s_pc.trisKnown ? (float)s_pc.trisMed : -1.0f;
        row.trisMax = (float)s_pc.trisMax;
        row.red[0] = (float)s_pc.red;
        row.pres[0] = (float)s_pc.pres;
    }
    double ma;
    if (battery_now_ma(&ma)) {
        row.batteryKnown = 1;
        row.batteryMa = (float)ma;
    }
    stall_table_add(&s_table, &row);
}

static void stall_event(const char *what, double t0Ms, double t1Ms, uint32_t replay, int kind,
                        double ms, double passFactor)
{
    /* every stall while they are rare, v0.11.6's rule past that
       (stall_coincide.h stall_line_due; the tally says how many were left
       out): v0.11.6's run-wide cap of 8 left issue 123's stalls 9-13 with
       the tallies alone; every one in the block's table */
    static StallMoment m;
    static char text[4096];
    moment_now(&m, t0Ms, t1Ms, replay);
    stall_tally_add(&s_tally, &m);
    stall_row(&m, replay, kind, ms, passFactor);
    if (!stall_line_due(&s_tally, &s_lines)) {
        return;
    }
    (void)stall_moment_text(&m, text, sizeof text);
    fprintf(stderr, "window: stall: %s; at that moment: %s\n", what, text);
    ico_diag_log_sync_ask(); /* issue 215: on the disc from the watchdog's thread, not this one */
}

int window_stall_tally_take(char *out, size_t cap)
{
    static int s_driversSaid;
    if (!s_driversSaid) {
        char names[512];
        if (ico_render_thread_driver_names(names, sizeof names) > 0) {
            s_driversSaid = 1;
            fprintf(stderr,
                    "window: threads named like the graphics driver's: %s (the first %d are "
                    "named at a stall with their run and wait times)\n",
                    names, STALL_DRIVERS);
        }
    }
    const int n = (int)s_tally.stalls;
    if (cap > 0) {
        out[0] = '\0';
        if (n > 0) {
            (void)stall_tally_text(&s_tally, out, cap);
        }
    }
    memset(&s_tally, 0, sizeof(s_tally));
    return n;
}

void window_stall_poll(void)
{
#ifdef __ANDROID__
    const double now = ico_thread_now_ms();
    unsigned keep = 0;

    for (unsigned i = 0; i < s_around.n; i++) {
        GpuSysfsAround a;
        char text[640]; /* the reset count's clause (about 60 bytes) on top */

        if (!gpu_sysfs_android_around(s_around.q[i].t0Ms, s_around.q[i].t1Ms, s_around.q[i].core,
                                      &a)) {
            continue; /* not sampled: dropped */
        }
        if (!a.hasAfter && now - s_around.q[i].t1Ms < AROUND_WAIT_MS) {
            s_around.q[keep++] = s_around.q[i]; /* its sample after is still to come */
            continue;
        }
        gpu_sysfs_around_text(&a, s_around.q[i].replay, text, sizeof(text));
        ico_log_limited("window: kernel GPU driver around", "window: %s%s", text, around_suffix());
    }
    s_around.n = keep;
#else
    (void)around_suffix;
    s_around.n = 0;
#endif
}

void window_stall_gpu_open(void)
{
#ifdef __ANDROID__
    static int s_said;
    GpuSysfsInfo info;
    char text[1024];

    gpu_sysfs_android_start(&info); /* the sampler thread (again after a close) */
    if (s_said) {
        return;
    }
    s_said = 1;
    gpu_sysfs_open_text(&info, text, sizeof(text));
    fprintf(stderr, "window: %s\n", text);
    /* issue 123: the reset count's change across each stall, or once here
       why the stall lines have none; issue 215: this process's GPU memory
       for last-run.txt */
    gpu_sysfs_resets_note(&info, text, sizeof(text));
    fprintf(stderr, "window: %s\n", text);
    fprintf(stderr, "window: %s\n", gpu_sysfs_android_proc_text());
    {
        /* what the device runs (issues 123, 179: their logs did not say;
           the performance hints need API 33): the system properties,
           android.os.Build's own sources, read once here */
        char rel[PROP_VALUE_MAX] = "", sdk[PROP_VALUE_MAX] = "", man[PROP_VALUE_MAX] = "",
             model[PROP_VALUE_MAX] = "", dev[PROP_VALUE_MAX] = "";
        (void)__system_property_get("ro.build.version.release", rel);
        (void)__system_property_get("ro.build.version.sdk", sdk);
        (void)__system_property_get("ro.product.manufacturer", man);
        (void)__system_property_get("ro.product.model", model);
        (void)__system_property_get("ro.product.device", dev);
        fprintf(stderr, "window: device: Android %s (API %s), %s %s (%s)\n", rel[0] ? rel : "?",
                sdk[0] ? sdk : "?", man[0] ? man : "?", model[0] ? model : "?", dev[0] ? dev : "?");
    }
    if (ico_android_log_mirror_running()) {
        /* the thermal and /proc asks on the log thread from here */
        s_asksOn = 1;
        ico_android_log_mirror_set_tick(asks_tick);
        fprintf(stderr,
                "window: the thermal state and the memory figures are asked once a second on the "
                "log thread; the 10 s lines read the last answers\n");
    }
    s_gpuOn = gpu_sysfs_any_sampled(&info); /* the GPU's nodes or the CPU clocks */
    s_gpuMaxMhz = info.maxMhz > 0 ? (int)info.maxMhz : -1;
    s_gpuBlockMs = ico_thread_now_ms();
#endif
}

void window_stall_gpu_close(void)
{
#ifdef __ANDROID__
    if (s_asksOn) {
        /* the log thread's asks end with the window (the thermal JNI calls
           and the /proc reads once a second); first, before the sampler
           stops.  A tick already running ends on its own: asks_tick reads
           only statics (sys_cell.h cells, render_thread.c's), none freed
           here or by ico_render_thread_stop, which ran before */
        ico_android_log_mirror_set_tick(NULL);
        s_asksOn = 0;
        fprintf(stderr, "window: the log thread's thermal and memory asks stopped with the "
                        "window\n");
    }
    gpu_sysfs_android_stop();
#endif
}

void window_stall_gpu_block(void)
{
    window_log_cost_mark("the acquire and present lines"); /* before it (log_cost.h) */
#ifdef __ANDROID__
    const double now = ico_thread_now_ms();
    static int s_thermalSaid;
    const char *why = "";
    ThermalState t;
    GpuSysfsBlock b;
    StallBlockClock clock = {0};
    char text[512];

    if (s_gpuOn && gpu_sysfs_android_block(s_gpuBlockMs, &b)) {
        s_gpuBlockMs = now;
        gpu_sysfs_block_text(&b, text, sizeof(text));
        fprintf(stderr, "window: %s\n", text);
        if (b.clockN > 0) {
            clock.n = b.clockN;
            clock.minMhz = (int)b.clockMin;
            clock.maxMhz = (int)b.clockMax;
            clock.meanMhz = b.clockMean;
        }
    }
    /* the thermal line each block, after the driver's; "not available"
       only once; the log thread's last answer where it asks (its age said) */
    double age;
    t = thermal_now(&why, &age);
    if (thermal_state_any(&t) || !s_thermalSaid) {
        thermal_block_text(&t, why, text, sizeof(text));
        if (age >= 0.0) {
            fprintf(stderr, "window: %s (asked %.1f s before on the log thread)\n", text,
                    age / 1000.0);
        } else {
            fprintf(stderr, "window: %s\n", text);
        }
        s_thermalSaid = 1;
    }
    stall_block_close(&clock, false);
    power_block_lines();
#else
    stall_block_close(NULL, false);
#endif
    window_log_cost_part(LOG_COST_KERNEL);
}

/* The every-replay pass timing armed by a present that blocked
   (rd_perf_passes_arm): held until a stalled replay's passes were compared
   (window_stall_passes) or ARM_QUIET_BLOCKS blocks passed with no present
   blocking, then back to one replay every 250 ms; the next blocked present
   arms it again (issues 179 and 123: armed for the rest of the run) */
#define ARM_QUIET_BLOCKS 3u

static struct {
    int on;         /* armed by this file */
    int compared;   /* a stalled replay's passes compared since */
    unsigned quiet; /* blocks ended with no present blocking since */
    unsigned arms, disarms;
} s_arm;

static void arm_block_end(void)
{
    if (!s_arm.on) {
        return;
    }
    const char *why = NULL;
    if (s_arm.compared) {
        why = "the stalled replay compared";
    } else if (++s_arm.quiet >= ARM_QUIET_BLOCKS) {
        why = "no present blocked in 3 blocks";
    }
    if (why == NULL) {
        return;
    }
    (void)rd_perf_passes_arm(false);
    s_arm.on = s_arm.compared = 0;
    s_arm.quiet = 0;
    s_arm.disarms++;
    if (s_arm.disarms <= 8u || s_arm.disarms % 100u == 0u) {
        fprintf(stderr,
                "window: pass timing: back to one replay every 250 ms (%s; %u times of the run); "
                "a present that blocks arms it again\n",
                why, s_arm.disarms);
    }
}

void window_stall_blocked(const RdPerfRecord *r)
{
    around_queue(r);
    if (r->presentMs > STALL_PRESENT_MS) {
        char what[320];
        snprintf(
            what, sizeof what,
            "a present of %.0f ms (replay %u: its acquire %.1f ms, the swapchain lock %.1f ms, "
            "the driver's present call %.1f ms, the surface poll %.1f ms; the replay's GPU "
            "span %.1f ms%s)",
            r->presentMs, r->replay, r->acquireMs, r->presentLockMs, r->presentCallMs,
            r->presentPollMs, r->gpuValid ? r->gpuMs : 0.0, r->gpuValid ? "" : ", not timed yet");
        const double t1 = r->presentEndMs > 0.0 ? r->presentEndMs : ico_thread_now_ms();
        stall_event(what, t1 - r->presentMs, t1, r->replay, 0, r->presentMs, 0.0);
    }
    if (r->presentEndMs > 0.0) {
        window_log_cost_present(r->presentEndMs - r->presentMs, r->presentEndMs);
    }
    s_after.pending = 1;
    s_after.replay = r->replay;
    s_after.ms = r->presentMs;
    /* only where the record carries the present split (the Vulkan
       backend): on OpenGL an armed replay's stamps would each read the GL
       clock, which may hold the CPU (rd.h rd_perf_passes_arm_backend) */
    s_arm.quiet = 0; /* a present blocked: the arm holds on */
    if (rd_perf_passes_arm_backend(rhi_backend(), r->presentSplit != 0) &&
        rd_perf_passes_arm(true)) {
        char text[320];

        s_arm.on = 1;
        s_arm.compared = 0;
        s_arm.arms++;
        if (s_arm.arms <= 8u || s_arm.arms % 100u == 0u) {
            rd_perf_passes_arm_text(r->replay, r->presentMs, rhi_backend_kind_name(rhi_backend()),
                                    text, sizeof(text));
            fprintf(stderr, "window: %s\n", text);
        }
    }
}

void window_stall_after(const RdPerfRecord *r, PaceGpuGap *gap)
{
    char text[448];
    PresentStallRec o;
    /* the display's rate read only for a stall (SDL's mode, as the 10 s
       lines read it): the stall's own rate in the run's sums */
    const StallBlockRec sr = {r->presented != 0,
                              r->presentMs,
                              r->gpuValid != 0,
                              r->gpuMs,
                              r->gpuGapValid != 0,
                              (double)r->gpuGapNs / 1e6,
                              (double)r->gpuGapBackNs / 1e6,
                              r->startMs,
                              r->presented && r->presentMs >= STALL_BLOCK_MS ? display_hz_now()
                                                                             : 0.0,
                              r->gpuLateValid,
                              r->gpuLateMs,
                              r->gpuRanTwice};

    if (s_stallT0 <= 0.0) {
        s_stallT0 = ico_thread_now_ms(); /* the first block starts with the first record */
    }
    pieces_take(r); /* its pieces against the block's medians (a stall of it judges them) */
    {
        /* each second's GPU span, triangles, fixed-work passes and present
           intervals (perf_secs.h) */
        const PerfSecsRec pr = {r->presented != 0,
                                r->presentEndMs,
                                r->gpuValid != 0,
                                r->gpuMs,
                                r->gpuPostMs[RD_PERF_POST_REDUCTION],
                                r->gpuPresentMs,
                                (double)r->vuTris};
        perf_secs_window_record(&pr, ico_thread_now_ms());
    }
    switch (r->gpuResetSign) {
    case RD_RESET_HANG:
        s_hang.replay = r->replay;
        s_hang.missing = r->gpuStampsMissing;
        memcpy(s_hang.first, r->gpuFirstMissing, sizeof(s_hang.first));
        s_hang.first[sizeof(s_hang.first) - 1u] = '\0';
        if (r->gpuRanTwice || r->gpuGapBackNs != 0) {
            /* its other signs, said beside the hang */
            s_sign.replay = r->replay;
            s_sign.twice = r->gpuRanTwice != 0;
            s_sign.back = r->gpuGapBackNs != 0;
        }
        break;
    case RD_RESET_TWICE:
    case RD_RESET_RESTART:
        s_sign.replay = r->replay;
        s_sign.twice = r->gpuRanTwice != 0;
        s_sign.back = r->gpuGapBackNs != 0;
        break;
    case RD_RESET_IDLE:
        s_idle.replay = r->replay;
        s_idle.ms = r->gpuIdleMs;
        break;
    default:
        break;
    }
    if (r->gpuSubN > 0) {
        /* the submissions for the per-stall line: a copy of under 100 bytes */
        SubRec *e = &s_subs[s_subsAt++ % SUB_RECS];
        e->replay = r->replay;
        e->firstMs = r->gpuFirstSubmitMs;
        e->signalMs = r->gpuSignalMs;
        e->doneValid = r->gpuDoneValid;
        e->subN = r->gpuSubN;
        e->subAll = r->gpuSubAll;
        e->hung = r->gpuSubHung;
        memcpy(e->sub, r->gpuSub, sizeof(e->sub));
    }
    if (r->presented && r->presentEndMs > 0.0) {
        /* the block's present latency (present_latency.h): a few adds */
        const PresentLatencyRec lr = {r->replay,
                                      r->evenHandMs > 0.0 ? r->evenHandMs : r->startMs,
                                      r->evenHandMs > 0.0,
                                      r->presentEndMs,
                                      r->presentMs,
                                      r->presentCallMs,
                                      r->fenceWaitMs,
                                      r->acquireMs,
                                      r->gpuDoneValid == 1 ? r->gpuSignalMs : 0.0};
        present_latency_add(&s_lat, &lr);
    }
    stall_block_add(&s_stall, &sr);
    if (!s_after.pending) {
        return;
    }
    s_after.pending = 0;
    if (present_stall_lines(s_after.ms)) {
        stall_rec(r, &o);
        present_after_text(s_after.replay, s_after.ms, &o, text, sizeof(text));
        ico_log_limited("window: after the present that blocked", "window: %s", text);
    }
    if (r->gpuGapValid) {
        pace_gpu_gap_stall(gap, r->gpuGapNs);
    }
}

void window_stall_passes(const RdPerfRecord *r, const RdPerfPassAgg *agg, const RdPerfPass *p,
                         uint32_t n, unsigned blockGpuN, double blockGpuMs)
{
    char text[1024];

    {
        /* a pass over 8 times its block median and 40 ms (stall_coincide.h):
           the replay's largest such one */
        int best = -1;
        double bestMs = 0.0, bestMed = 0.0;
        for (uint32_t i = 0; i < n; i++) {
            if (p[i].gpuUs < 0.0f) {
                continue;
            }
            const double ms = (double)p[i].gpuUs / 1000.0;
            const uint32_t key = (uint32_t)p[i].kind | (uint32_t)p[i].target << 8 |
                                 (uint32_t)(uint8_t)(p[i].list + 1) << 16;
            const double med = stall_medians_add(&s_med, key, ms);
            if (stall_pass_spike(ms, med) && ms > bestMs) {
                best = (int)i;
                bestMs = ms;
                bestMed = med;
            }
        }
        if (best >= 0) {
            char label[64], what[200];
            (void)rd_perf_pass_label(&p[best], label, sizeof label);
            snprintf(what, sizeof what,
                     "%s %ux%u %.1f ms against the block's median %.1f ms (replay %u, GPU span "
                     "%.1f ms)",
                     label, (unsigned)p[best].w, (unsigned)p[best].h, bestMs, bestMed, r->replay,
                     r->gpuValid ? r->gpuMs : 0.0);
            stall_event(what, r->startMs, r->startMs + r->totalMs + (r->gpuValid ? r->gpuMs : 0.0),
                        r->replay, 1, bestMs, bestMs / bestMed);
        }
    }

    if (!r->presented || !present_stall_lines(r->presentMs)) {
        return;
    }
    rd_perf_pass_grow_text(agg, p, n, r->replay, r->presentMs, r->gpuValid, r->gpuMs,
                           blockGpuN ? blockGpuMs : -1.0, 5, text, sizeof(text));
    ico_log_limited("window: stalled replay", "window: %s", text);
    s_arm.compared = s_arm.on; /* the arm's purpose met: off at the block's end */
}
