/*
 * port/platform/window_dump_auto.c
 *
 * The window's side of the automatic dumps (window_dump_auto.h).
 */
#include "window_dump_auto.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "attempt_ledger.h"
#include "diag_host.h"
#include "dump_auto.h"
#include "dump_zip.h"
#include "host_config.h"
#include "host_fs.h"
#include "host_loop.h"
#include "hotkeys.h"
#include "input_record.h"
#include "ico_thread.h" /* ico_thread_now_ms */
#include "rd.h"
#include "rd_dump_mem.h" /* the slow-scene dump's two steps */
#include "rd_thread.h"   /* rd_thread_drain */
#include "trace_host.h"
#include "window_lines.h" /* window_log_cost_job */
#include "window_present.h"

#define s_pres g_windowPres

static IcoDumpJob s_job; /* the game thread's, copied into the worker's slot */

static int s_skinPending;
static char s_skinSidecar[ICO_PATH_MAX];
static int s_flushSet;

static double ms_since(Uint64 t0)
{
    return (double)(SDL_GetTicksNS() - t0) / 1e6;
}

static const char *base_of(const char *p)
{
    const char *b = p + strlen(p);

    while (b > p && b[-1] != '/' && b[-1] != '\\') {
        b--;
    }
    return b;
}

static void use_flush(void)
{
    if (!s_flushSet) {
        ico_dump_auto_set_flush(ico_host_log_flush);
        s_flushSet = 1;
    }
}

static void stamp_now(char *stamp, size_t n)
{
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);

    if (tm == NULL || strftime(stamp, n, "%Y%m%d-%H%M%S", tm) == 0) {
        snprintf(stamp, n, "unknown");
    }
}

/* the job's blocks freed when the worker did not take them */
static void job_release(IcoDumpJob *j)
{
    free(j->dump);
    free(j->prev);
    j->dump = j->prev = NULL;
}

/* The slow-scene dump between its two steps (rd_dump_mem.h): the frame
   measured and the worker asked for a block of its size; at a later
   picture the block taken and the frame copied into it.  The game
   thread's alone */
#define SLOW_PREP_LIMIT_MS 10000.0 /* the arm's own life: a 10 s block */

static struct {
    int on;
    IcoDumpJob job; /* the paths, filled at the first step */
    uint32_t frame; /* the frame picked (window_host.c slow_scene_poll) */
    uint32_t seen;  /* the last replay looked at for another frame */
    SlowSceneBlock b;
    double work, meanWork;
    double gameMs, drainMs; /* the first step's */
    double measureMs;       /* of gameMs, the measure (without its drain) */
    double t0Ms;            /* the first step, ico_thread_now_ms */
    size_t asked;           /* the block's size */
    void *block;            /* the worker's block once taken (NULL before) */
    size_t size;
    double prepMs;
    char stamp[32];
} s_slow;

/* A second step given up (window_dump_auto_slow_lost): the block's figures
   as the first step had them (its count and the dump before's work and
   stage), so window_host.c counts the dump as not made and the next slow
   block can arm one again; at most SLOW_SCENE_DUMPS re-arms a run (a
   device whose every block gives up does not try forever) */
static int s_lost, s_rearms;
static SlowSceneBlock s_lostB;

static void slow_given_up(const char *why)
{
    const int again = s_rearms < SLOW_SCENE_DUMPS;
    if (again) {
        s_rearms++;
        s_lost = 1;
        s_lostB = s_slow.b;
    }
    fprintf(stderr, "window: slow-scene dump: not written: %s; %s\n", why,
            again ? "not counted, the next slow block arms one again"
                  : "counted (the run's re-arms used up)");
}

int window_dump_auto_slow_lost(SlowSceneBlock *b)
{
    if (!s_lost) {
        return 0;
    }
    s_lost = 0;
    *b = s_lostB;
    return 1;
}

/* the slow-scene job's rest and its hand-over: 1 handed over, 0 dropped
   (its blocks freed) */
static int slow_submit(IcoDumpJob *j, const SlowSceneBlock *b, uint32_t got, double work,
                       double meanWork, const char *stamp, double gameMs)
{
    char name[64], line[640];

    snprintf(j->logPath, sizeof(j->logPath), "%s", ico_diag_log_path());
    j->pruneSlow = 1;
    snprintf(name, sizeof(name), "slow-%s.zip", stamp);
    (void)window_slow_scene_text(line, sizeof(line), b, (unsigned)got, work, meanWork, name);
    snprintf(j->text, sizeof(j->text), "%s", line);
    j->gameMs = gameMs;
    if (ico_dump_auto_submit(j) != 0) {
        job_release(j);
        return 0;
    }
    return 1;
}

/* the skin check's frame dump (F12's frame_dump without the display PNG,
   the log flush and the zip: those are the worker's) */
static void skin_capture(const char *sidecar)
{
    char pref[ICO_PATH_MAX], dir[ICO_PATH_MAX], dump[ICO_PATH_MAX], png[ICO_PATH_MAX], stamp[32];
    const Uint64 t0 = SDL_GetTicksNS();
    IcoDumpJob *j = &s_job;

    memset(j, 0, sizeof(*j));
    j->drainMs = -1.0;
    j->measureMs = j->copyMs = -1.0;
    snprintf(j->tag, sizeof(j->tag), "skin-check capture");
    stamp_now(stamp, sizeof(stamp));
    ico_host_pref_dir(pref, sizeof(pref));
    if (ico_path_join(dir, sizeof(dir), pref, "dumps") != 0 || ico_make_dir(dir) != 0) {
        fprintf(stderr, "window: skin-check capture: no usable dumps folder under %s\n", pref);
        return;
    }
    if (ico_frame_dump_paths(dir, stamp, ico_host_vsync_count(), dump, sizeof(dump), png,
                             sizeof(png), j->zip, sizeof(j->zip)) != 0) {
        fprintf(stderr, "window: skin-check capture: the dumps folder %s is too deep\n", dir);
        return;
    }
    use_flush();
    rd_dump_on_demand_log();
    j->drainMs = (double)rd_thread_drain() / 1e6; /* timed apart, as the slow scene's */
    if (!rd_dump_frame_mem(&j->dump, &j->dumpLen)) {
        fprintf(stderr, "window: skin-check capture: the frame could not be taken\n");
        return;
    }
    snprintf(j->dumpName, sizeof(j->dumpName), "%s", dump);
    const size_t stem = strlen(dump) > 7 ? strlen(dump) - 7 : 0;
    snprintf(j->prevName, sizeof(j->prevName), "%.*s-prev.rddump", (int)stem, dump);
    const int havePrev = rd_dump_prev_frame_mem(&j->prev, &j->prevLen);
    /* the picture as presented, at the next present (the capture the
       present thread takes; the zip does not carry it) */
    s_pres.reportZip[0] = '\0';
    if (ico_frame_present_path(dir, stamp, s_pres.presentPng, sizeof(s_pres.presentPng)) == 0 &&
        !rd_capture_presented(s_pres.presentPng)) {
        s_pres.presentPng[0] = '\0';
    }
    s_pres.presentInfoPending = s_pres.presentPng[0] != '\0';
    s_pres.presentInfoHave = 0;
    s_pres.presentDumpFrame = rd_frame_number();
    s_pres.presentDumpPrev = havePrev;
    ico_input_record_flush();
    snprintf(j->logPath, sizeof(j->logPath), "%s", ico_diag_log_path());
    snprintf(j->sidecar, sizeof(j->sidecar), "%s", sidecar);
    /* the two paths cut to what the line holds (-Wformat-truncation) */
    snprintf(j->text, sizeof(j->text),
             "skin check: the capture: %.220s and the frame dump %.220s, the presented picture at "
             "the next present%s%s",
             sidecar[0] != '\0' ? sidecar : "no sidecar", j->zip,
             havePrev ? "" : " (without the tick before)",
             s_pres.presentPng[0] != '\0' ? "" : " (not armed)");
    j->gameMs = ms_since(t0);
    if (ico_dump_auto_submit(j) != 0) {
        job_release(j);
    }
}

void window_dump_auto_skin_pump(void)
{
    char path[ICO_PATH_MAX];

    if (!s_skinPending && rd_vu_skin_capture_due(path, sizeof(path))) {
        s_skinPending = 1;
        snprintf(s_skinSidecar, sizeof(s_skinSidecar), "%s", path);
        fprintf(stderr,
                "window: VU skin check: a failure was captured (%s): a frame dump for the "
                "next present, written by the dump worker\n",
                path[0] != '\0' ? path : "no sidecar");
    }
    if (!s_skinPending) {
        return;
    }
    if (ico_dump_auto_busy()) {
        return; /* a slow-scene zip is being written: this is once a run, so it waits */
    }
    s_skinPending = 0;
    skin_capture(s_skinSidecar);
}

int window_dump_auto_slow(uint32_t frame, const SlowSceneBlock *b, double work, double meanWork)
{
    char logs[ICO_PATH_MAX], stamp[32];
    const Uint64 t0 = SDL_GetTicksNS();
    IcoDumpJob *j = &s_job;
    uint32_t got = 0;

    memset(j, 0, sizeof(*j));
    j->drainMs = -1.0;
    j->measureMs = j->copyMs = -1.0;
    snprintf(j->tag, sizeof(j->tag), "slow-scene dump");
    if (ico_dump_auto_busy()) {
        (void)ico_dump_auto_submit(j); /* the drop line, counted */
        return 0;
    }
    stamp_now(stamp, sizeof(stamp));
    if (ico_host_logs_dir(logs, sizeof(logs)) != 0 ||
        ico_slow_dump_path(j->zip, sizeof(j->zip), logs, stamp) != 0) {
        fprintf(stderr, "window: slow scene: no usable logs folder\n");
        return 0;
    }
    if (snprintf(j->dumpName, sizeof(j->dumpName), "%s/slow-%s.rddump", logs, stamp) >=
        (int)sizeof(j->dumpName)) {
        fprintf(stderr, "window: slow scene: the logs folder's path is too long\n");
        return 0;
    }
    use_flush();
    /* The frame's bytes are read on the game thread, not on the worker:
       the ring frame names textures, meshes and targets the game thread
       changes from its next call on (rd_dump.c dumpFrame drains the render
       and submit threads first; the zip and the file writes are the
       worker's).  Here the frame is only measured and the worker makes a
       block of its size with its pages touched; the copy into it is made
       at a later picture (slow_pump), one pass of the dump's bytes
       (rd_dump_mem.h; issue 179: 47-56 ms here when the block was grown
       on this thread).  The drain is timed apart */
    size_t bytes = 0;
    j->drainMs = (double)rd_thread_drain() / 1e6;
    if (!rd_dump_retained_frame_bytes(frame, &bytes)) {
        fprintf(stderr, "window: slow scene: frame %u not written\n", (unsigned)frame);
        return 0;
    }
    /* room for the frame's textures to change before the copy: an eighth
       more and 1 MiB */
    const size_t want = bytes + bytes / 8u + ((size_t)1 << 20);
    const double measured = ms_since(t0) - j->drainMs;
    if (ico_dump_auto_prepare(want) != 0) {
        /* no worker thread (or busy since the check): the old way, the
           copy grown here; the submit says why when it fails too */
        const Uint64 t1 = SDL_GetTicksNS();
        if (!rd_dump_retained_frame_mem(frame, &j->dump, &j->dumpLen, &got)) {
            fprintf(stderr, "window: slow scene: frame %u not written\n", (unsigned)frame);
            return 0;
        }
        j->measureMs = measured;
        j->copyMs = ms_since(t1);
        snprintf(j->how, sizeof(j->how), "grown here: the worker could not be asked");
        ico_attempt_fire2(ICO_ATT_CAPTURE_TWO_STEP, 1);
        return slow_submit(j, b, got, work, meanWork, stamp, ms_since(t0));
    }
    {
        RdReplayWork w;

        memset(&s_slow, 0, sizeof(s_slow));
        s_slow.seen = rd_perf_last_replay_work(&w) ? w.replay : 0u;
    }
    s_slow.on = 1;
    s_slow.job = *j;
    s_slow.frame = frame;
    s_slow.b = *b;
    s_slow.work = work;
    s_slow.meanWork = meanWork;
    s_slow.gameMs = ms_since(t0);
    s_slow.drainMs = j->drainMs;
    s_slow.measureMs = measured;
    s_slow.t0Ms = ico_thread_now_ms();
    s_slow.asked = want;
    snprintf(s_slow.stamp, sizeof(s_slow.stamp), "%s", stamp);
    return 1;
}

/* the second step, every vsync while a block is asked for (from
   window_dump_auto_report): the block taken when the worker has made it,
   the frame picked (the first step's while the ring holds it, else the
   next replay at or above the block's mean, window_host.c's rule) copied
   into it, the job handed back */
static void slow_pump(void)
{
    if (!s_slow.on) {
        return;
    }
    if (ico_thread_now_ms() - s_slow.t0Ms > SLOW_PREP_LIMIT_MS) {
        ico_dump_auto_unprepare(s_slow.block); /* the block taken or asked for, freed */
        s_slow.block = NULL;
        s_slow.on = 0;
        char why[160];
        snprintf(why, sizeof(why),
                 "no frame of the scene could be copied in %.0f ms (the worker's block of %zu "
                 "bytes given up)",
                 SLOW_PREP_LIMIT_MS, s_slow.asked);
        slow_given_up(why);
        return;
    }
    if (s_slow.block == NULL) {
        const int r = ico_dump_auto_prepared(&s_slow.block, &s_slow.size, &s_slow.prepMs);

        if (r == 0) {
            return; /* the worker is making it */
        }
        if (r < 0) {
            s_slow.on = 0;
            char why[96];
            snprintf(why, sizeof(why), "the worker could not make a block of %zu bytes",
                     s_slow.asked);
            slow_given_up(why);
            return;
        }
    }
    void *const block = s_slow.block;
    const size_t size = s_slow.size;
    uint32_t frame = s_slow.frame;
    double work = s_slow.work;
    if (!rd_frame_retained(frame)) {
        RdReplayWork w;

        frame = 0;
        if (rd_perf_last_replay_work(&w) && w.replay != s_slow.seen) {
            s_slow.seen = w.replay;
            work =
                (double)window_scene_vu_invocations(w.vuTris, w.vuPreVertices, rd_vu_prepass_on());
            if (window_slow_scene_pick(work, s_slow.meanWork) && rd_frame_retained(w.frame)) {
                frame = w.frame;
            }
        }
    }
    if (frame == 0) {
        return; /* the block kept for a later picture's replay */
    }
    s_slow.on = 0;
    s_slow.block = NULL;
    IcoDumpJob *j = &s_job;
    const Uint64 t1 = SDL_GetTicksNS();
    uint32_t got = 0;
    size_t len = 0;

    *j = s_slow.job;
    const double drain = (double)rd_thread_drain() / 1e6;
    const RdDumpInto into = rd_dump_retained_frame_into(frame, block, size, &len, &got);

    j->drainMs = s_slow.drainMs + drain;
    j->prepMs = s_slow.prepMs;
    j->measureMs = s_slow.measureMs;
    if (into == RD_DUMP_INTO_OK) {
        j->dump = block;
        j->dumpLen = len;
        j->copyMs = ms_since(t1) - drain;
        snprintf(j->how, sizeof(j->how), "two steps: frame %u copied into the worker's block",
                 (unsigned)frame);
        ico_attempt_fire(ICO_ATT_CAPTURE_TWO_STEP, 1);
    } else {
        /* the frame grew past the block (or could not be read): the block
           given up, the old way */
        ico_dump_auto_unprepare(block);
        fprintf(stderr,
                "window: slow-scene dump: frame %u %s the worker's block of %zu bytes: copied "
                "into a block grown here instead\n",
                (unsigned)frame,
                into == RD_DUMP_INTO_SMALL ? "had grown past" : "could not be written into", size);
        if (!rd_dump_retained_frame_mem(frame, &j->dump, &j->dumpLen, &got)) {
            char why[64];
            snprintf(why, sizeof(why), "frame %u could not be copied", (unsigned)frame);
            slow_given_up(why);
            return;
        }
        j->copyMs = ms_since(t1) - drain;
        snprintf(j->how, sizeof(j->how), "grown here: frame %u %s the worker's block",
                 (unsigned)frame, into == RD_DUMP_INTO_SMALL ? "had grown past" : "missed");
        ico_attempt_fire2(ICO_ATT_CAPTURE_TWO_STEP, 1);
    }
    (void)slow_submit(j, &s_slow.b, got, work, s_slow.meanWork, s_slow.stamp,
                      s_slow.gameMs + ms_since(t1));
}

static void report_one(const IcoDumpDone *d)
{
    /* the zip's start (the block made before it is not in the span) */
    window_log_cost_job(d->gameMs, d->workerEndMs - (d->workerMs - d->prepMs), d->workerEndMs);
    if (d->result < 0) {
        fprintf(stderr,
                "window: %s: zip not written: %s (%.1f ms on the game thread, %.0f ms on the "
                "worker; the .rddump is kept)\n",
                d->tag, d->why, d->gameMs, d->workerMs);
        return;
    }
    char drain[96] = "";
    if (d->drainMs >= 0.0) {
        snprintf(drain, sizeof(drain),
                 " (the render thread's work in flight %.1f ms of it, the frame's bytes %.1f ms)",
                 d->drainMs, d->gameMs > d->drainMs ? d->gameMs - d->drainMs : 0.0);
    }
    fprintf(stderr,
            "window: %s: %.1f ms on the game thread%s, %.0f ms on the worker (%zu bytes of frame "
            "into %s%s)\n",
            d->tag, d->gameMs, drain, d->workerMs, d->bytes, base_of(d->zip),
            d->result == 1 ? "; the log could not be read" : "");
    if (d->how[0] != '\0') {
        /* the steps apart (issue 179's question: which of them costs the
           game thread, and which way the frame went) */
        fprintf(stderr,
                "window: %s: the game thread's parts: the measure %.1f ms, the copy %.1f ms (each "
                "without its drain); the worker's block made in %.0f ms; %s\n",
                d->tag, d->measureMs, d->copyMs, d->prepMs, d->how);
    }
    if (d->why[0] != '\0') {
        fprintf(stderr, "window: %s: %s\n", d->tag, d->why);
    }
    if (strncmp(d->tag, "slow", 4) == 0) {
        ico_diag_note_slow_scene(d->zip);
    }
    fprintf(stderr, "window: %s\n", d->text);
}

void window_dump_auto_report(void)
{
    IcoDumpDone d;

    while (ico_dump_auto_poll(&d)) {
        report_one(&d);
    }
    slow_pump(); /* a slow-scene dump's second step */
}

void window_dump_auto_stop(void)
{
    if (s_slow.on) {
        ico_dump_auto_unprepare(s_slow.block); /* a slow-scene dump not copied yet */
        s_slow.block = NULL;
        s_slow.on = 0;
    }
    ico_dump_auto_shutdown();
    {
        IcoDumpDone d;

        while (ico_dump_auto_poll(&d)) {
            report_one(&d);
        }
    }
}
