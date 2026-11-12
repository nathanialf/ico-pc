/*
 * port/platform/window_capture.c
 *
 * A release candidate's frames of the opening, the window's side
 * (window_capture.h).  Nothing here is compiled unless ICO_RC_TOOLS is on
 * and the target is Android.
 */
#include "window_capture.h"

#if ICO_RC_TOOLS && defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../core/region.h" /* the disc's scene layout (menu screen 55 on the European disc) */
#include "capture_trigger.h"
#include "host_config.h"
#include "host_fs.h"
#include "host_loop.h"
#include "hotkeys.h" /* ico_frame_dump_paths: frame_dump's names */
#include "rd.h"
#include "window_present.h"

#define s_pres g_windowPres

extern int stage_no;          /* common/src/main.c: 1 the boot, the opening and the title */
extern int current_layout_id; /* common/src/layout_texture.c: the menu screen */

static IcoCaptureTrigger s_trig;
static long long s_runStart = -1;
/* the dump the pump takes: its moment, frame, and what frame_dump names its
   zip from (the time stamp and the vsync count, read before and after) */
static int s_reason = -1;
static uint32_t s_frame;
static unsigned s_vsync;
static char s_stamp[32];

static void stamp_now(char *stamp, size_t n)
{
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);

    if (tm == NULL || strftime(stamp, n, "%Y%m%d-%H%M%S", tm) == 0) {
        snprintf(stamp, n, "unknown");
    }
}

/* stage 1 with the disc's scene layout in front: the opening */
static int in_opening(void)
{
    return stage_no == 1 && current_layout_id == ico_region_cur()->layout_scene;
}

static int plan_of(uint32_t glowLight)
{
    switch (glowLight) {
    case RD_GLOW_LIGHT_DARK:
        return ICO_CAPTURE_PLAN_DARK;
    case RD_GLOW_LIGHT_LAMPS:
        return ICO_CAPTURE_PLAN_LAMPS;
    case RD_GLOW_LIGHT_WHOLE:
        return ICO_CAPTURE_PLAN_WHOLE;
    default:
        return ICO_CAPTURE_PLAN_NONE; /* NONE, OFF */
    }
}

void window_capture_start(void)
{
    if (s_runStart >= 0) {
        return; /* once a process: a window opened again keeps the run's count and start */
    }
    ico_capture_trigger_init(&s_trig);
    s_runStart = (long long)time(NULL);
    s_reason = -1;
    fprintf(stderr, "window: %s\n", ico_capture_trigger_start_text());
}

void window_capture_picture(const struct RdPerfRecord *r)
{
    (void)ico_capture_trigger_picture(&s_trig, in_opening(), plan_of(r->glowLight), r->frame,
                                      SDL_GetTicksNS());
}

int window_capture_due(void)
{
    /* an earlier dump's presented picture (or the player's own) still owed:
       frame_dump would take its place */
    const int busy = s_pres.presentPng[0] != '\0' || s_pres.reportZip[0] != '\0';
    const int reason = ico_capture_trigger_take(&s_trig, in_opening(), busy, SDL_GetTicksNS());

    if (reason < 0) {
        return 0;
    }
    s_reason = reason;
    s_frame = rd_frame_number();
    s_vsync = ico_host_vsync_count();
    stamp_now(s_stamp, sizeof(s_stamp));
    return 1;
}

/* the zip frame_dump wrote: its names for the stamp and vsync count before
   or after the call (a second or a vsync can pass inside it) */
static int find_zip(char *zip, size_t n)
{
    char pref[ICO_PATH_MAX], dir[ICO_PATH_MAX], dump[ICO_PATH_MAX], png[ICO_PATH_MAX];
    char stamps[2][32];
    const unsigned vsyncs[2] = {s_vsync, ico_host_vsync_count()};

    snprintf(stamps[0], sizeof(stamps[0]), "%s", s_stamp);
    stamp_now(stamps[1], sizeof(stamps[1]));
    ico_host_pref_dir(pref, sizeof(pref));
    if (ico_path_join(dir, sizeof(dir), pref, "dumps") != 0) {
        return 0;
    }
    for (int s = 0; s < 2; s++) {
        for (int v = 0; v < 2; v++) {
            if (ico_frame_dump_paths(dir, stamps[s], vsyncs[v], dump, sizeof(dump), png,
                                     sizeof(png), zip, n) == 0 &&
                ico_path_kind(zip, NULL, NULL) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

void window_capture_taken(void)
{
    char zip[ICO_PATH_MAX], line[1400];

    if (s_reason < 0) {
        return;
    }
    const int found = find_zip(zip, sizeof(zip));
    const char *base = found ? strrchr(zip, '/') : NULL;

    (void)ico_capture_trigger_line(line, sizeof(line), s_reason, s_trig.armedRun, s_trig.dumps,
                                   s_frame, found ? (base != NULL ? base + 1 : zip) : NULL);
    fprintf(stderr, "window: %s\n", line);
    s_reason = -1;
    ico_capture_trigger_done(&s_trig, SDL_GetTicksNS());
}

long long window_capture_run_start(void)
{
    return s_runStart;
}
#endif
