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
#include "diag_host.h"
#include "dump_auto.h"
#include "dump_zip.h"
#include "host_config.h"
#include "host_fs.h"
#include "host_loop.h"
#include "hotkeys.h"
#include "input_record.h"
#include "rd.h"
#include "trace_host.h"
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

/* the skin check's frame dump (F12's frame_dump without the display PNG,
   the log flush and the zip: those are the worker's) */
static void skin_capture(const char *sidecar)
{
    char pref[ICO_PATH_MAX], dir[ICO_PATH_MAX], dump[ICO_PATH_MAX], png[ICO_PATH_MAX], stamp[32];
    const Uint64 t0 = SDL_GetTicksNS();
    IcoDumpJob *j = &s_job;

    memset(j, 0, sizeof(*j));
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
    snprintf(j->text, sizeof(j->text),
             "skin check: the capture: %s and the frame dump %s, the presented picture at the "
             "next present%s%s",
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
    snprintf(j->dumpName, sizeof(j->dumpName), "%s/slow-%s.rddump", logs, stamp);
    use_flush();
    if (!rd_dump_retained_frame_mem(frame, &j->dump, &j->dumpLen, &got)) {
        fprintf(stderr, "window: slow scene: frame %u not written\n", (unsigned)frame);
        return 0;
    }
    snprintf(j->logPath, sizeof(j->logPath), "%s", ico_diag_log_path());
    j->pruneSlow = 1;
    char name[64], line[640];
    snprintf(name, sizeof(name), "slow-%s.zip", stamp);
    (void)window_slow_scene_text(line, sizeof(line), b, (unsigned)got, work, meanWork, name);
    snprintf(j->text, sizeof(j->text), "%s", line);
    j->gameMs = ms_since(t0);
    if (ico_dump_auto_submit(j) != 0) {
        job_release(j);
        return 0;
    }
    return 1;
}

static void report_one(const IcoDumpDone *d)
{
    if (d->result < 0) {
        fprintf(stderr,
                "window: %s: zip not written: %s (%.1f ms on the game thread, %.0f ms on the "
                "worker; the .rddump is kept)\n",
                d->tag, d->why, d->gameMs, d->workerMs);
        return;
    }
    fprintf(stderr,
            "window: %s: %.1f ms on the game thread, %.0f ms on the worker (%zu bytes of frame "
            "into %s%s)\n",
            d->tag, d->gameMs, d->workerMs, d->bytes, base_of(d->zip),
            d->result == 1 ? "; the log could not be read" : "");
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
}

void window_dump_auto_stop(void)
{
    ico_dump_auto_shutdown();
    window_dump_auto_report();
}
