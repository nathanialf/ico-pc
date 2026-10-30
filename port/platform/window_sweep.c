/*
 * port/platform/window_sweep.c
 *
 * Shift+F12's aspect sweep, moved out of window_host.c (at its file size
 * gate); no window state beyond its own.
 */
#include "window_sweep.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "dump_zip.h"
#include "host_config.h"
#include "hotkeys.h"
#include "ico_frame.h"
#include "photo_mode.h"
#include "rd.h"
#include "video_options.h"

/* rd_dump.c (declared here as window_host.c does) */
bool rd_dump_on_demand(const char *dumpPath, const char *pngPath);

/* Shift+F12 in photo mode, the aspect sweep: the paused frame captured at
   4:3, 16:9, 32:9, 48:9 and 60:9 into <pref>/dumps/sweep-<time>.zip, each
   aspect's <aspect>.rddump and <aspect>.png (dump_zip.h ico_sweep_zip), for
   tools/native_frame_check.py.

   The game builds its frame at the width gsFrame holds, latched at each
   flip from ico_video_frame (GsBase.c gsbHostFrameLatch), so the sweep forces
   the aspect there (ico_video_frame_force_aspect) and waits.  At the next
   flip the latch makes the new width and remakes rd's scene-sized targets
   (rd_reset_scene, the 50/60 Hz switch's path), and the paused tick that
   follows draws from the photo camera (photo_view.c ico_photo_view_tick) and
   rebuilds the game's matrices at that width, +0x280 the cull's included,
   each tick: no game state is touched.  Once the last closed frame is a frame
   at the width and SWEEP_SETTLE closed frames have followed it (the feedback
   passes and the blended picture settle), the normal F12 path takes it
   (rd_dump_on_demand: the .rddump and the DISPLAY .png at the native width).
   After the last aspect, or on leaving photo mode or a timeout, the forcing
   is cleared: the next flip latches the live width back with its
   rd_reset_scene.  The window shows the sweep's frames meanwhile (a second
   or two); the presentation's aspect stays the live one. */
#define SWEEP_SETTLE 4
#define SWEEP_TIMEOUT_PUMPS 900

static struct {
    int active, idx, captured, seen, waited;
    unsigned lastNumber;
    char prefix[ICO_PATH_MAX], zip[ICO_PATH_MAX];
} s_sweep;

static void sweep_finish(const char *reason)
{
    char why[160];

    ico_video_frame_force_aspect(0.0f);
    s_sweep.active = 0;
    why[0] = '\0';
    if (reason != NULL) {
        fprintf(stderr, "window: aspect sweep: %s\n", reason);
    }
    if (s_sweep.captured > 0) {
        ico_host_log_flush();
        const int n = ico_sweep_zip(s_sweep.zip, s_sweep.prefix, why, sizeof(why));

        if (n >= 0) {
            fprintf(stderr, "window: aspect sweep: %d of %d aspects in %s\n", n, ICO_SWEEP_COUNT,
                    s_sweep.zip);
        } else {
            fprintf(stderr, "window: aspect sweep: zip not written: %s; the loose files are kept\n",
                    why);
        }
    } else {
        fprintf(stderr, "window: aspect sweep: nothing captured\n");
    }
}

void window_sweep_start(void)
{
    char pref[ICO_PATH_MAX], dir[ICO_PATH_MAX], stamp[32];
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);

    if (s_sweep.active) {
        return;
    }
    if (!ico_photo_active()) {
        fprintf(stderr, "window: aspect sweep: Shift+F12 works in photo mode only\n");
        return;
    }
    if (tm == NULL || strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm) == 0) {
        snprintf(stamp, sizeof(stamp), "unknown");
    }
    ico_host_pref_dir(pref, sizeof(pref));
    if (ico_path_join(dir, sizeof(dir), pref, "dumps") != 0 || ico_make_dir(dir) != 0 ||
        ico_sweep_paths(dir, stamp, s_sweep.prefix, sizeof(s_sweep.prefix), s_sweep.zip,
                        sizeof(s_sweep.zip)) != 0) {
        fprintf(stderr, "window: aspect sweep: no usable dumps folder under %s\n", pref);
        return;
    }
    s_sweep.active = 1;
    s_sweep.idx = 0;
    s_sweep.captured = 0;
    s_sweep.seen = 0;
    s_sweep.waited = 0;
    s_sweep.lastNumber = 0;
    fprintf(stderr, "window: aspect sweep: starts, %d aspects\n", ICO_SWEEP_COUNT);
    ico_video_frame_force_aspect(ico_sweep_aspect_value(0));
}

/* one pump of the sweep: capture the aspect in force once its frame has
   settled, then ask for the next */
void window_sweep_pump(void)
{
    IcoFrame want;
    uint32_t number = 0, w = 0;
    float aspect = 0.0f;
    char dump[ICO_PATH_MAX], png[ICO_PATH_MAX];

    if (!s_sweep.active) {
        return;
    }
    if (!ico_photo_active()) {
        sweep_finish("left photo mode");
        return;
    }
    if (++s_sweep.waited > SWEEP_TIMEOUT_PUMPS) {
        fprintf(stderr, "window: aspect sweep: no frame at %s after %d vsyncs\n",
                ico_sweep_aspect_name(s_sweep.idx), SWEEP_TIMEOUT_PUMPS);
        sweep_finish("gave up");
        return;
    }
    ico_frame_make(&want, ico_sweep_aspect_value(s_sweep.idx), 448);
    if (!rd_last_frame_native(&number, &aspect, &w) || (int)w != want.w ||
        aspect < want.aspect - 1e-3f || aspect > want.aspect + 1e-3f) {
        s_sweep.seen = 0;
        return;
    }
    if (number != s_sweep.lastNumber) {
        s_sweep.lastNumber = number;
        s_sweep.seen++;
    }
    if (s_sweep.seen < SWEEP_SETTLE) {
        return;
    }
    if (snprintf(dump, sizeof(dump), "%s%s.rddump", s_sweep.prefix,
                 ico_sweep_aspect_name(s_sweep.idx)) >= (int)sizeof(dump) ||
        snprintf(png, sizeof(png), "%s%s.png", s_sweep.prefix,
                 ico_sweep_aspect_name(s_sweep.idx)) >= (int)sizeof(png)) {
        sweep_finish("the capture path is too long");
        return;
    }
    if (rd_dump_on_demand(dump, png)) {
        s_sweep.captured++;
        fprintf(stderr, "window: aspect sweep: %s at frame %u, GS width %u\n",
                ico_sweep_aspect_name(s_sweep.idx), (unsigned)number, (unsigned)w);
    } else {
        fprintf(stderr, "window: aspect sweep: %s could not be written\n",
                ico_sweep_aspect_name(s_sweep.idx));
    }
    s_sweep.idx++;
    s_sweep.seen = 0;
    s_sweep.waited = 0;
    s_sweep.lastNumber = 0;
    if (s_sweep.idx >= ICO_SWEEP_COUNT) {
        sweep_finish(NULL);
    } else {
        ico_video_frame_force_aspect(ico_sweep_aspect_value(s_sweep.idx));
    }
}
