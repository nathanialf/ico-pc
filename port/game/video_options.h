/*
 * port/game/video_options.h
 *
 * The display options (the [video] keys): one place that holds them, so
 * the window (port/platform/window_host.c), the game's native frame
 * (ico2/seki/src/GsBase.c gsFrame, from ico_video_frame) and the Settings
 * menu (port/ui/settings.c) read and write the same values.
 *
 *   [video] preset          "original"  "original" | "enhanced" | "custom"
 *   [video] resolution      "window"    "window" | "WxH" | "Nx" (N = 1..16) | "auto"
 *                                       ("auto" on Android: the window's size,
 *                                       lowered a step at a time (3x, 2x, 1x)
 *                                       while the presents take too long)
 *   [video] aspect          "4:3"       "4:3" | "16:10" | "16:9" | "21:9" | "32:9" | "48:9" | "auto"
 *   [video] window_mode     "windowed"  "windowed" | "borderless" | "fullscreen"
 *                                       (borderless: a frameless window over the
 *                                       whole display; ignored on Android)
 *   [video] fullscreen      false       read when window_mode is absent (true is
 *                                       "fullscreen", false "windowed"), and written
 *                                       beside it (true only for "fullscreen")
 *   [video] vsync           true
 *   [video] texture_filter  "original"  "original" | "trilinear" | "anisotropic"
 *   [video] full_height     false
 *   [video] full_pixel      (preset)    the picture is the window's own pixels (the
 *                                       "window" resolution: one texel a pixel, nothing
 *                                       cropped) and shows the strip the PS2's thin black
 *                                       border hid; false keeps the PS2's frame shape with
 *                                       its border.  Absent: true when the rows add up to
 *                                       Enhanced, else false; the Preset shortcut sets it
 *                                       (Enhanced on, Original off) but it is not one of
 *                                       the four rows the preset is derived from
 *   [video] framerate       "uncapped"  "original" | "uncapped" | N (30..1000)
 *                                       (60 on Android)
 *   [video] anti_aliasing   "off"       "off" | "ssaa2" | "ssaa4" | "msaa2" | "msaa4" |
 *                                       "msaa8": supersampling (the scene at 2x/4x the
 *                                       resolution, reduced) or multisampling; not under
 *                                       the Original preset (always off there); an MSAA
 *                                       count the GPU lacks draws as off
 *   [video] crt             false       the CRT filter (any preset)
 *   [video] crt_mode        "consumer"  "scanlines" | "consumer" | "trinitron" | "pvm" |
 *                                       "shadow"
 *   [video] crt_strength    1.0         0..1
 *   [video] crt_scanlines, crt_mask, crt_halation, crt_bloom, crt_curvature
 *                           -1          config only: the mode's value when < 0
 *   [video] texture_pack    true        load a PCSX2 texture pack when one is installed
 *   [video] dump_textures   false       write each texture under its PCSX2 name (pack authors)
 *   [video] model_pack      true        load a model pack (replacement models) when one is installed
 *   [video] dump_models     false       save each model part as a glTF file (pack makers)
 *   [video] texture_pack_budget_mb
 *                           2048        config only: GPU memory for replacements, 128..65536
 *   [video] texture_pack_precache
 *                           true        config only: read the whole pack into memory at start
 *   [video] texture_pack_cache_mb
 *                           0           config only: RAM for that read-ahead, 0 (half the
 *                                       computer's memory) or 128..65536
 *   [video] effect_glow, effect_depth_of_field, effect_softening, effect_motion_blur,
 *           effect_fog, effect_cinematic_bars
 *                           true        the game's own picture effects: the
 *                                       light bloom and sun flare, the distance blur, the
 *                                       edge softening, the motion trail, the distance fog
 *   [video] effect_water_sharp
 *                           false       the reflections in water (and the picture through
 *                                       the queen's barrier) at the scene's resolution;
 *                                       false keeps the PS2's size (Water reflections:
 *                                       Original)
 *   [video] effect_glow_size
 *                           "scaled"    "scaled" | "original": the glow (and the other
 *                                       effects' work buffers: depth of field, softening)
 *                                       at the work scale above 1x, or at the PS2's
 *                                       buffer sizes at every resolution (Glow size:
 *                                       Original: coarser, much lighter on the GPU)
 *   [video] effects_depth   true        config only: the scene's depth beside
 *                                       the picture for an effects program (ReShade's depth
 *                                       effects), drawn only when one is loaded; not with
 *                                       the CRT filter, never on Android
 *
 * Every option applies on its own.  preset is not an option but a
 * shortcut over four of them, read and written as such: "enhanced" or
 * "custom" takes resolution, aspect, texture_filter and full_height as
 * written; anything else ("original", no key, a misspelling) puts them at
 * the PS2's values (1x, 4:3, original, half) whatever the file says.  The preset in force is derived from those four rows
 * (ico_video_preset): Original when all four are the PS2's, Enhanced when
 * they are window (on Android also resolution "auto"), aspect auto,
 * anisotropic and full, Custom otherwise; it is
 * saved back as "original" when Original, else "enhanced" with the rows.
 * fullscreen, vsync, framerate, the CRT keys, the texture pack keys and the
 * effect keys are not part of it.
 * framerate: "original" presents once per
 * simulation tick (each picture held for the tick's refreshes, as the PS2);
 * "uncapped" presents as often as the display allows (vsync) and
 * interpolates between the last two ticks; N does the same at most N times
 * a second.  It applies whatever the preset: each tick's picture is the
 * options', the in-between ones are blended from the last two.
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

#include "../include/ico_frame.h"

/* ico_video_preset: the preset the four rows add up to */
enum { ICO_VIDEO_ORIGINAL = 0, ICO_VIDEO_ENHANCED = 1, ICO_VIDEO_CUSTOM = 2 };

enum {
    ICO_ASPECT_4_3 = 0,
    ICO_ASPECT_16_10 = 1,
    ICO_ASPECT_16_9 = 2,
    ICO_ASPECT_21_9 = 3,
    ICO_ASPECT_32_9 = 4,
    ICO_ASPECT_48_9 = 5,
    ICO_ASPECT_AUTO = 6,
    ICO_ASPECT_COUNT = 7
};

/* The widest aspect the picture is made at: the native frame's
   (port/include/ico_frame.h ICO_FRAME_K_MAX, 5 times the 4:3 width, aspect
   20:3); 48:9 is 4 times. */
#define ICO_ASPECT_MAX ICO_FRAME_ASPECT_MAX

/* The window's mode.  FULLSCREEN is SDL's desktop fullscreen;
   BORDERLESS a frameless window over its display. */
enum {
    ICO_WINDOW_WINDOWED = 0,
    ICO_WINDOW_BORDERLESS = 1,
    ICO_WINDOW_FULLSCREEN = 2,
    ICO_WINDOW_COUNT = 3
};

enum { ICO_FILTER_ORIGINAL = 0, ICO_FILTER_TRILINEAR = 1, ICO_FILTER_ANISOTROPIC = 2 };

/* IcoVideoOptions.antiAlias: the same numbers as the renderer's
   RdAntiAlias (port/render/rd.h), which the window copies across */
enum {
    ICO_AA_OFF = 0,
    ICO_AA_SSAA2 = 1,
    ICO_AA_SSAA4 = 2,
    ICO_AA_MSAA2 = 3,
    ICO_AA_MSAA4 = 4,
    ICO_AA_MSAA8 = 5,
    ICO_AA_COUNT = 6
};

typedef struct IcoVideoOptions {
    int resW, resH; /* resolution "WxH"; 0 x 0 with resScale 0: "window" */
    int resScale;   /* resolution "Nx": N (1..16); ICO_RES_AUTO: "auto"; 0 otherwise */
    int aspect;     /* ICO_ASPECT_* */
    int windowMode; /* ICO_WINDOW_* */
    int vsync;      /* the swapchain waits for the vertical blank */
    int filter;     /* ICO_FILTER_* */
    int fullHeight; /* skip the reduction's vertical halving */
    int fullPixel;  /* the window's own pixels, no black border (full_pixel above) */
    int antiAlias;  /* ICO_AA_*, as written (ico_video_anti_alias: the one in force) */
    int framerate;  /* ICO_FRAMERATE_ORIGINAL, _UNCAPPED, or presents a second */
    /* the CRT filter: applied whatever the preset */
    int crt;           /* the filter on */
    int crtMode;       /* ICO_CRT_* */
    float crtStrength; /* 0..1 */
    /* the config-only overrides, -1 = the mode's own: scanline strength,
       mask strength, halation, bloom (0..1), curvature (0..0.25) */
    float crtScanlines, crtMask, crtHalation, crtBloom, crtCurvature;
    /* texture packs: applied whatever the preset */
    int texturePack;         /* replacements from an installed pack drawn */
    int dumpTextures;        /* each texture written under its PCSX2 name */
    int modelPack;           /* replacement models from an installed pack drawn */
    int dumpModels;          /* each model part saved as a glTF file (Developer mode) */
    int texturePackBudgetMb; /* config only: the replacements' GPU memory */
    int texturePackPrecache; /* config only: the pack read into memory at start */
    int texturePackCacheMb;  /* config only: the RAM that read-ahead may use (0: half the
                                computer's memory) */
    /* the game's own picture effects, 1 = on (the PS2 picture),
       applied whatever the preset; the game reads them through the
       ico_video_effect_* getters below */
    int effectGlow;          /* the flare and bloom passes and the sun (staticBlur.c) */
    int effectDepthOfField;  /* the depth-of-field pass (staticBlur.c depthField) */
    int effectSoftening;     /* the edge softening (GsBase.c gsb_antiAlias) */
    int effectMotionBlur;    /* the motion blur (staticBlur.c MotionBlur) */
    int effectFog;           /* the depth fog (ZFog.c fog_DrawFog) */
    int effectCinematicBars; /* the cutscene bars (GsBase.c); 1 = on */
    /* the water's reflections (puddle.c, pool.c) and the queen barrier's
       picture at the scene's resolution (rd.h RdSettings.waterSharp);
       0 = Original, the PS2's size: the one effect row whose default is 0 */
    int effectWaterSharp;
    /* Glow size (rd.h RdSettings.glowOriginal): 0 = Scaled, the work
       scale above 1x (the default); 1 = Original, the PS2's buffer sizes */
    int effectGlowOriginal;
    /* an output-size depth buffer in the presentation for an
       effects program (rd.h RdSettings.effectsDepth); 1 = on */
    int effectsDepth;
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
    ICO_CRT_SHADOW = 4
};

#define ICO_CRT_MODES 5

/* IcoVideoOptions.framerate: these two, or 30..1000 (a cap) */
enum { ICO_FRAMERATE_ORIGINAL = 0, ICO_FRAMERATE_UNCAPPED = -1 };

/* IcoVideoOptions.resScale for resolution "auto": the scene
   starts at the window's size and the window lowers it a step (3x, 2x, 1x)
   when the presents take too long (pace_policy.h pace_auto_resolution_step),
   never back up while the game runs */
#define ICO_RES_AUTO (-1)
/* The largest resolution "Nx" (the menu stops at it too). */
#define ICO_RES_SCALE_MAX 16

/* The defaults: the Original rows (1x, 4:3, original filter, half height),
   windowed, vsync on, framerate uncapped (60 on Android), the CRT filter
   off (its mode Consumer TV at full strength, no overrides), texture packs
   on with precache and a 2048 MB budget, no dump, every effect on, the
   effects depth on. */
void ico_video_defaults(IcoVideoOptions *o);
/* The defaults of either build, android 1 or 0 (tests check
   the Android ones on any computer) */
void ico_video_defaults_for(IcoVideoOptions *o, int android);
/* The framerate default: 60 on Android, else ICO_FRAMERATE_UNCAPPED */
int ico_video_default_framerate(int android);
/* The rules in force, 1 on the Android build: its defaults, and Enhanced's
   resolution "auto" (ico_video_preset, ico_video_set_preset).  Tests set
   them; nothing else does. */
void ico_video_set_android(int android);
int ico_video_android(void);
/* Resolution "auto"'s scene scale in force: 0 while the scene is the
   window's size, else N (3, 2, 1) after the window lowered it.  The window
   sets it (window_host.c), the Settings menu shows it ("Auto (2x)"). */
void ico_video_set_auto_scale(int scale);
int ico_video_auto_scale(void);
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
   whatever the preset, "auto" being the window's clamped to [4:3, ICO_ASPECT_MAX]. */
float ico_video_aspect(void);
/* The native frame (port/include/ico_frame.h) for the aspect in force and
   a frame height h: 512 wide at 4:3 exactly.  GsBase.c latches it at each
   flip (gsFrame, with ScreenHeight). */
void ico_video_frame(IcoFrame *out, int h);
/* The aspect sweep (window_host.c, Shift+F12 in photo mode): the native
   frame ico_video_frame makes is built for this aspect instead of the one in
   force, until it is set to 0 again.  The next flip latches it
   (GsBase.c gsbHostFrameLatch: rd_reset_scene at a new width), the paused
   photo camera rebuilds the cull (+0x280) at that width in the same tick,
   and clearing it brings the live width back the same way.  Only the frame
   follows: the presentation (ico_video_aspect) is untouched. */
void ico_video_frame_force_aspect(float aspect);
/* 1 while ico_video_frame_force_aspect holds an aspect (the aspect sweep,
   window_host.c), else 0 */
int ico_video_frame_forced(void);
/* The effect switches in force, 1 or 0: plain functions the
   game's files declare extern (staticBlur.c, GsBase.c, ZFog.c), read at
   most once a frame. */
int ico_video_effect_glow(void);
int ico_video_effect_depth_of_field(void);
int ico_video_effect_softening(void);
int ico_video_effect_motion_blur(void);
int ico_video_effect_fog(void);
int ico_video_effect_cinematic_bars(void);
/* [video] effect_water_sharp, 1 (Sharp) or 0 (Original): the host hands it
   to the renderer (window_host.c video_settings, RdSettings.waterSharp) */
int ico_video_effect_water_sharp(void);
/* [video] effect_glow_size, 1 ("original") or 0 ("scaled"): the host hands
   it to the renderer (window_host.c video_settings, RdSettings.glowOriginal) */
int ico_video_effect_glow_original(void);
/* [video] effects_depth, 1 or 0 */
int ico_video_effects_depth(void);
/* The anti-aliasing in force, ICO_AA_*: the anti_aliasing option, except
   ICO_AA_OFF whenever the preset is Original (ico_video_preset) */
int ico_video_anti_alias(void);
/* Whether the options in force are the Original preset (the
   PS2's picture out of the box: 1x, 4:3, the original filter, half
   height); GsBase.c's frame latch hands the opposite to GifPacket.c
   (gif_HostSetExtended), as rd's preset is the same test (window_host.c) */
int ico_video_original(void);
/* Run-time change of the anti_aliasing option alone (as ico_video_set: the
   window applies it at its next pump); out of range is ICO_AA_OFF */
void ico_video_set_anti_alias(int aa);
/* "off", "ssaa2", "ssaa4", "msaa2", "msaa4", "msaa8" (any case) */
int ico_video_parse_anti_alias(const char *s, int *aa);
const char *ico_video_anti_alias_name(int aa);
/* The model pack switches in force, 1 or 0: [video] model_pack and
   dump_models (the dump also needs Developer mode, which the caller checks). */
int ico_video_model_pack(void);
int ico_video_dump_models(void);
/* The presentation rate in force: the framerate option, whatever
   the preset. */
int ico_video_framerate(void);
/* 1 when the presenter blends pictures between the simulation's ticks (a
   framerate other than "original", as the window decides it), else 0.  The
   game's cull (GsBase.c gsbHostCullMargin) keeps a margin past the screen's
   edges then, so a part that is just off screen at a tick is still recorded
   for the blended pictures that show it. */
int ico_video_interpolate(void);
/* The game's camera-cut signal: the hard-cut sites (camera-root.c,
   StageManager.c, under ICO_HOST) call ico_video_camera_cut(); the window
   compares ico_video_cut_serial() once per vsync and marks the frame being
   recorded as a cut (rd_camera_cut), so the presenter does not blend across
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
/* "windowed", "borderless", "fullscreen" (any case) */
int ico_video_parse_window_mode(const char *s, int *mode);
const char *ico_video_window_mode_name(int mode);
/* the CRT modes: "scanlines", "consumer", "trinitron", "pvm", "shadow" */
int ico_video_parse_crt_mode(const char *s, int *mode);
const char *ico_video_crt_mode_name(int mode);
const char *ico_video_filter_name(int filter);
/* The preset the four rows add up to: ICO_VIDEO_ORIGINAL when resolution,
   aspect, texture filter and height are 1x, 4:3, original, half;
   ICO_VIDEO_ENHANCED when window (or auto on Android), auto, anisotropic,
   full; else
   ICO_VIDEO_CUSTOM. */
int ico_video_preset(const IcoVideoOptions *o);
/* The shortcut: writes the four rows of ORIGINAL or ENHANCED into o, the
   other options untouched (except ORIGINAL also sets antiAlias off).
   CUSTOM (or anything else) changes nothing. */
void ico_video_set_preset(IcoVideoOptions *o, int preset);
/* "original", "enhanced" or "custom" */
const char *ico_video_preset_name(int preset);
/* "window", "WxH", "Nx" or "auto" into buf (at least 24 bytes). */
const char *ico_video_resolution_name(const IcoVideoOptions *o, char *buf, unsigned size);

#endif /* ICO_PORT_GAME_VIDEO_OPTIONS_H */
