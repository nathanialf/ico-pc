/*
 * port/game/video_options.h
 *
 * The display options (renderer wave 7, R7a, the
 * [video] keys): one place that holds them, so the
 * window (port/platform/window_host.c), the game's widescreen hook
 * (ico2/seki/src/GsBase.c gsbHostWideX, under ICO_HOST) and the Settings
 * menu (package 6C) read and write the same values.
 *
 *   [video] preset          "original"  "original" | "enhanced"
 *   [video] resolution      "window"    "window" | "WxH" | "Nx" (N = 1..8)
 *   [video] aspect          "4:3"       "4:3" | "16:10" | "16:9" | "auto"
 *   [video] fullscreen      false
 *   [video] vsync           true
 *   [video] texture_filter  "original"  "original" | "trilinear" | "anisotropic"
 *   [video] full_height     false
 *   [video] framerate       "uncapped"  "original" | "uncapped" | N (30..1000)
 *   [video] crt             false       the CRT filter (package CRT, both presets)
 *   [video] crt_mode        "consumer"  "scanlines" | "consumer" | "trinitron" | "pvm" |
 *                                       "shadow"
 *   [video] crt_strength    1.0         0..1
 *   [video] crt_scanlines, crt_mask, crt_halation, crt_bloom, crt_curvature
 *                           -1          config only: the mode's value when < 0
 *
 * The Original preset is the PS2 picture whatever resolution, aspect,
 * texture_filter and full_height say; the Enhanced preset applies them,
 * each on its own.  fullscreen, vsync and framerate apply in both.
 * framerate (renderer wave 7, R7b): "original" presents once per
 * simulation tick (each picture held for the tick's refreshes, as the PS2);
 * "uncapped" presents as often as the display allows (vsync) and
 * interpolates between the last two ticks; N does the same at most N times
 * a second.  In both presets (package F2; before it the Original preset
 * was always "original"): each tick's picture is the preset's, the
 * in-between ones are blended from the last two.
 *
 * Read from config.toml through ico_config_get_* the first time any value
 * is asked for; ico_video_set changes them at run time (the window applies
 * the change at its next pump: ico_video_serial), ico_video_save writes
 * them back through ico_config_set_* and ico_config_save.  This module has
 * no renderer dependency: the headless build links it too, so a headless
 * run with aspect = "16:9" exercises the same widened cull as the window.
 */
#ifndef ICO_PORT_GAME_VIDEO_OPTIONS_H
#define ICO_PORT_GAME_VIDEO_OPTIONS_H

enum { ICO_VIDEO_ORIGINAL = 0, ICO_VIDEO_ENHANCED = 1 };

enum { ICO_ASPECT_4_3 = 0, ICO_ASPECT_16_10 = 1, ICO_ASPECT_16_9 = 2, ICO_ASPECT_AUTO = 3 };

enum { ICO_FILTER_ORIGINAL = 0, ICO_FILTER_TRILINEAR = 1, ICO_FILTER_ANISOTROPIC = 2 };

typedef struct IcoVideoOptions {
    int preset;     /* ICO_VIDEO_* */
    int resW, resH; /* resolution "WxH"; 0 x 0 with resScale 0: "window" */
    int resScale;   /* resolution "Nx": N (1..8); 0 otherwise */
    int aspect;     /* ICO_ASPECT_* */
    int fullscreen; /* desktop-resolution borderless */
    int vsync;      /* the swapchain waits for the vertical blank */
    int filter;     /* ICO_FILTER_* */
    int fullHeight; /* skip the reduction's vertical halving */
    int framerate;  /* ICO_FRAMERATE_ORIGINAL, _UNCAPPED, or presents a second (R7b) */
    /* package CRT: applied in both presets */
    int crt;           /* the filter on */
    int crtMode;       /* ICO_CRT_* */
    float crtStrength; /* 0..1 */
    /* the config-only overrides, -1 = the mode's own: scanline strength,
       mask strength, halation, bloom (0..1), curvature (0..0.25) */
    float crtScanlines, crtMask, crtHalation, crtBloom, crtCurvature;
} IcoVideoOptions;

/* IcoVideoOptions.crtMode (rd.h RdCrtMode is this + 1) */
enum {
    ICO_CRT_SCANLINES = 0,
    ICO_CRT_CONSUMER = 1,
    ICO_CRT_TRINITRON = 2,
    ICO_CRT_PVM = 3,
    ICO_CRT_SHADOW = 4 /* package CRT2 */
};

#define ICO_CRT_MODES 5

/* IcoVideoOptions.framerate: these two, or 30..1000 (a cap) */
enum { ICO_FRAMERATE_ORIGINAL = 0, ICO_FRAMERATE_UNCAPPED = -1 };

/* The defaults: Original, window, 4:3, windowed, vsync on, original filter,
   half height, framerate uncapped (both presets), the CRT filter off (its
   mode Consumer TV at full strength, no overrides). */
void ico_video_defaults(IcoVideoOptions *o);
/* The options in force (read from the config on first use). */
void ico_video_get(IcoVideoOptions *o);
/* Run-time change (the Settings menu): takes effect at the window's next
   pump, without a restart.  Values out of range fall back to the defaults'. */
void ico_video_set(const IcoVideoOptions *o);
/* Bumped by every ico_video_set (and the first read): the window compares
   it with what it applied. */
unsigned ico_video_serial(void);
/* Writes the [video] keys into config.toml (ico_config_set_*, then
   ico_config_save).  0, or -1. */
int ico_video_save(void);
/* Forget the run-time values: read from the config again on next use. */
void ico_video_reload(void);
/* The window's client size in pixels, for aspect "auto" (the window calls it
   on open and on every resize; 0 x 0, the headless build's, means 4:3). */
void ico_video_set_window(int w, int h);
/* The presentation aspect (width / height) in force: 4/3 in the Original
   preset; in Enhanced the aspect option, "auto" being the window's clamped
   to [4:3, 16:9]. */
float ico_video_aspect(void);
/* How much wider than 4:3 the presentation is: ico_video_aspect() / (4/3),
   1 in Original (GsBase.c gsbHostWideX). */
float ico_video_wide_x(void);
/* The presentation rate in force (R7b): the framerate option, in both
   presets. */
int ico_video_framerate(void);
/* The game's camera-cut signal (R7b): the hard-cut sites (camera-root.c,
   StageManager.c, under ICO_HOST) call ico_video_camera_cut(); the window
   compares ico_video_cut_serial() once per vsync and marks the frame being
   recorded as a cut (rd_CameraCut), so the presenter does not blend across
   it.  A counter only: no game state reads it, so the simulation and its
   trace are the same with or without a window. */
void ico_video_camera_cut(void);
unsigned ico_video_cut_serial(void);
/* The config strings (tests and the Settings menu's labels).  The parsers
   return 0, or -1 for a string they do not know (o is then unchanged). */
int ico_video_parse_resolution(const char *s, IcoVideoOptions *o);
/* "original", "uncapped" or a number 30..1000 */
int ico_video_parse_framerate(const char *s, int *framerate);
/* "original", "uncapped" or the number, into buf (at least 16 bytes) */
const char *ico_video_framerate_name(int framerate, char *buf, unsigned size);
int ico_video_parse_aspect(const char *s, int *aspect);
int ico_video_parse_filter(const char *s, int *filter);
const char *ico_video_aspect_name(int aspect);
/* package CRT: "scanlines", "consumer", "trinitron", "pvm", "shadow" */
int ico_video_parse_crt_mode(const char *s, int *mode);
const char *ico_video_crt_mode_name(int mode);
const char *ico_video_filter_name(int filter);
/* "window", "WxH" or "Nx" into buf (at least 24 bytes). */
const char *ico_video_resolution_name(const IcoVideoOptions *o, char *buf, unsigned size);

#endif /* ICO_PORT_GAME_VIDEO_OPTIONS_H */
