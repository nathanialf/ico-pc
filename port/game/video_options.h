/*
 * port/game/video_options.h
 *
 * The display options (renderer wave 7, R7a, the
 * [video] keys): one place that holds them, so the
 * window (port/platform/window_host.c), the game's widescreen hook
 * (ico2/seki/src/GsBase.c gsbHostWideX, under ICO_HOST) and the Settings
 * menu (package 6C) read and write the same values.
 *
 *   [video] preset          "original"  "original" | "enhanced" | "custom"
 *   [video] resolution      "window"    "window" | "WxH" | "Nx" (N = 1..8)
 *   [video] aspect          "4:3"       "4:3" | "16:10" | "16:9" | "21:9" | "32:9" | "auto"
 *   [video] fullscreen      false
 *   [video] vsync           true
 *   [video] texture_filter  "original"  "original" | "trilinear" | "anisotropic"
 *   [video] full_height     false
 *   [video] framerate       "uncapped"  "original" | "uncapped" | N (30..1000)
 *   [video] crt             false       the CRT filter (package CRT, any preset)
 *   [video] crt_mode        "consumer"  "scanlines" | "consumer" | "trinitron" | "pvm" |
 *                                       "shadow"
 *   [video] crt_strength    1.0         0..1
 *   [video] crt_scanlines, crt_mask, crt_halation, crt_bloom, crt_curvature
 *                           -1          config only: the mode's value when < 0
 *   [video] texture_pack    true        load a PCSX2 texture pack when one is installed
 *   [video] dump_textures   false       write each texture under its PCSX2 name (pack authors)
 *   [video] texture_pack_budget_mb
 *                           2048        config only: GPU memory for replacements, 128..65536
 *   [video] texture_pack_precache
 *                           true        config only: read the whole pack into memory at start
 *   [video] texture_pack_cache_mb
 *                           0           config only: RAM for that read-ahead, 0 (half the
 *                                       computer's memory) or 128..65536
 *
 * Every option applies on its own.  preset is not an option but a
 * shortcut over four of them, read and written as such: "enhanced" or
 * "custom" takes resolution, aspect, texture_filter and full_height as
 * written; anything else ("original", no key, a misspelling) puts them at
 * the PS2's values (1x, 4:3, original, half) whatever the file says.  The preset in force is derived from those four rows
 * (ico_video_preset): Original when all four are the PS2's, Enhanced when
 * they are window, auto, anisotropic and full, Custom otherwise; it is
 * saved back as "original" when Original, else "enhanced" with the rows.
 * fullscreen, vsync, framerate, the CRT keys and the texture pack keys are
 * not part of it.
 * framerate (renderer wave 7, R7b): "original" presents once per
 * simulation tick (each picture held for the tick's refreshes, as the PS2);
 * "uncapped" presents as often as the display allows (vsync) and
 * interpolates between the last two ticks; N does the same at most N times
 * a second.  Whatever the preset (package F2; before it the Original
 * preset was always "original"): each tick's picture is the options', the
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

/* ico_video_preset: the preset the four rows add up to */
enum { ICO_VIDEO_ORIGINAL = 0, ICO_VIDEO_ENHANCED = 1, ICO_VIDEO_CUSTOM = 2 };

enum {
    ICO_ASPECT_4_3 = 0,
    ICO_ASPECT_16_10 = 1,
    ICO_ASPECT_16_9 = 2,
    ICO_ASPECT_21_9 = 3,
    ICO_ASPECT_32_9 = 4,
    ICO_ASPECT_AUTO = 5,
    ICO_ASPECT_COUNT = 6
};

enum { ICO_FILTER_ORIGINAL = 0, ICO_FILTER_TRILINEAR = 1, ICO_FILTER_ANISOTROPIC = 2 };

typedef struct IcoVideoOptions {
    int resW, resH; /* resolution "WxH"; 0 x 0 with resScale 0: "window" */
    int resScale;   /* resolution "Nx": N (1..8); 0 otherwise */
    int aspect;     /* ICO_ASPECT_* */
    int fullscreen; /* desktop-resolution borderless */
    int vsync;      /* the swapchain waits for the vertical blank */
    int filter;     /* ICO_FILTER_* */
    int fullHeight; /* skip the reduction's vertical halving */
    int framerate;  /* ICO_FRAMERATE_ORIGINAL, _UNCAPPED, or presents a second (R7b) */
    /* package CRT: applied whatever the preset */
    int crt;           /* the filter on */
    int crtMode;       /* ICO_CRT_* */
    float crtStrength; /* 0..1 */
    /* the config-only overrides, -1 = the mode's own: scanline strength,
       mask strength, halation, bloom (0..1), curvature (0..0.25) */
    float crtScanlines, crtMask, crtHalation, crtBloom, crtCurvature;
    /* texture packs: applied whatever the preset */
    int texturePack;         /* replacements from an installed pack drawn */
    int dumpTextures;        /* each texture written under its PCSX2 name */
    int texturePackBudgetMb; /* config only: the replacements' GPU memory */
    int texturePackPrecache; /* config only: the pack read into memory at start */
    int texturePackCacheMb;  /* config only: the RAM that read-ahead may use (0: half the
                                computer's memory) */
} IcoVideoOptions;

/* IcoVideoOptions.texturePackBudgetMb: the default and the range a value
   is clamped to */
#define ICO_TEXPACK_BUDGET_DEFAULT 2048
#define ICO_TEXPACK_BUDGET_MIN 128
#define ICO_TEXPACK_BUDGET_MAX 65536
/* IcoVideoOptions.texturePackCacheMb: 0 (automatic) or this range */
#define ICO_TEXPACK_CACHE_MIN 128
#define ICO_TEXPACK_CACHE_MAX 65536

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

/* The defaults: the Original rows (1x, 4:3, original filter, half height),
   windowed, vsync on, framerate uncapped, the CRT filter off (its mode
   Consumer TV at full strength, no overrides), texture packs on with
   precache and a 2048 MB budget, no dump. */
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
/* The presentation aspect (width / height) in force: the aspect option,
   whatever the preset, "auto" being the window's clamped to [4:3, 16:9]. */
float ico_video_aspect(void);
/* How much wider than 4:3 the presentation is: ico_video_aspect() / (4/3),
   at least 1 (GsBase.c gsbHostWideX). */
float ico_video_wide_x(void);
/* The presentation rate in force (R7b): the framerate option, whatever
   the preset. */
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
/* The preset the four rows add up to: ICO_VIDEO_ORIGINAL when resolution,
   aspect, texture filter and height are 1x, 4:3, original, half;
   ICO_VIDEO_ENHANCED when window, auto, anisotropic, full; else
   ICO_VIDEO_CUSTOM. */
int ico_video_preset(const IcoVideoOptions *o);
/* The shortcut: writes the four rows of ORIGINAL or ENHANCED into o, the
   other options untouched.  CUSTOM (or anything else) changes nothing. */
void ico_video_set_preset(IcoVideoOptions *o, int preset);
/* "original", "enhanced" or "custom" */
const char *ico_video_preset_name(int preset);
/* "window", "WxH" or "Nx" into buf (at least 24 bytes). */
const char *ico_video_resolution_name(const IcoVideoOptions *o, char *buf, unsigned size);

#endif /* ICO_PORT_GAME_VIDEO_OPTIONS_H */
