/*
 * port/render/rd_upscale.h
 *
 * The upscale point (docs/port/RENDER.md, "The upscale point"): where a
 * temporal upscaler takes the scene.  It runs after the whole post chain,
 * at the moment the game's reduction (rd_post.c postReduction, the end of
 * list 12) first reads SCENE: the glow, the depth of field, the fog, the
 * motion blur, list 11's UI, subtitles, fades, letterbox and film noise
 * are all in SCENE by then.  postReduction records a marker, RDC_UPSCALE_POINT
 * (u[0] the target the reduction reads, u[1] the one it writes), before it
 * binds DISPLAY; the marker is a state command that changes no state
 * (rd__cmd_is_state, rd__apply_state), so every scan of the lists passes
 * over it, and the replay does nothing at it unless the split below is in
 * force.  Dumps from version 17 carry it; an older dump has none and
 * replays as before (no dispatch).
 *
 * The scale split (rd_present.c rd__apply_display).  While the temporal
 * upscaler is asked (rd__upscale_wanted: its switch, not under the CRT
 * filter), the Resolution row (and the "window" resolution, and Auto)
 * gives the DISPLAY scale d: DISPLAY, DISPLAY_HELD and the upscaler's
 * output UPSCALED are made at it.  The scene-class targets (SCENE, WORK2,
 * AURA_WORK, the DATE snapshot, a frame-sized temporary) take the RENDER
 * scale r = d / ratio, not below 1 (the GS size), not below d / 3, and not
 * above the cap Auto's step sets (RdSettings.upscaleRenderCap); the
 * supersampling and MSAA are off (rd__aa_mode), and Downsampling with them.
 * g_rd.sceneSx/Sy hold r; rd__display_scale gives d.  Off, d is r and
 * every picture is the one before the split existed.
 *
 * At the marker (rd__upscale_point, from rd_replay.c's list loop with the
 * scene's screen run and VU merge flushed and the open pass ended): SCENE's
 * first column and row are made copies of their neighbours when the jitter
 * is positive on that axis (a full-frame GS sprite moved right or down
 * leaves texel 0's centre uncovered: the edge guard), then the hook
 * rd__upscale_dispatch (rd_upscale_hook.c) gets SCENE's colour, its depth
 * (rd__sampled_depth: in place or the D32F copy), VELOCITY (rd_velocity.h),
 * the jitter and the reset, and writes UPSCALED.  The reduction's textured
 * sprite then samples UPSCALED in SCENE's place (rd__upscale_remap_begin,
 * from rd_blur_border.c rd__blur_post_sprite): SCENE's record lends its
 * texture and texel geometry (tw, th, sx, sy, ox) for that draw only, so
 * the UV mapping follows UPSCALED's texels; nothing recorded changes
 * (never rd_alias_target, which is record-time and would land in dumps).
 * The fades fold is off while the split is in force (rd_fades_fold.c).
 * With the display layer (rd_layer.h, the default) the dispatch is at the
 * layer's boundary instead, lists 11-12 after it are drawn into COMPOSITE
 * (SCENE's record lends the composite's to the end of the replay) and the
 * marker does nothing; without it ([dev] display_layer = false) the fades
 * are drawn into SCENE before the dispatch, as above.
 *
 * The jitter (rd_jitter.h).  With a phase count set (rd__upscale_set_phases:
 * the upscaler's own, FSR's ceil(8 ratio^2)), each replay that presents a
 * newly drawn picture sets the jitter to Halton (2, 3) at (present index %
 * phase count) before it replays (rd__upscale_replay_begin, from
 * rd_replay.c rd__replay_frame_here: never during a replay).  Phase count 0
 * (the stub's default) sets none.  A re-shown present (the still
 * re-present's keep frame) replays no scene: no dispatch, UPSCALED and
 * DISPLAY stay; the next dispatch is told how many presents passed
 * (RdUpscaleIn.presents).
 *
 * The switch: the settings' FSR 3.1 (rd__upscale_follow: Upscaling =
 * FSR 3.1, rd_fsr3.c's dispatch), or rd__upscale_set (rd_replay_tool
 * --upscale-test [ratio], the tests) or [dev] upscale_test = true
 * (ICO_RD_UPSCALE_TEST=1: the Quality-like ratio 1.5), read at the first
 * ask, these with the test's bilinear stand-in (rd_upscale_hook.c).  A change applies at the
 * next display change (rd__apply_display).  Turning it on also turns the
 * motion vectors on (rd__velocity_set), and off again when it turned them on.
 */
#ifndef RD_UPSCALE_H
#define RD_UPSCALE_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* rd__target_scale_of's named argument for UPSCALED's texel geometry: a
 * frame-sized scene-class target at the display scale (SCENE's GS size,
 * DISPLAY's scale, no full-height doubling) */
#define RD_UPSCALED_NAMED (-2)

/* The switch: on with ratio (1..3, the display scale over the render
 * scale; 1 is a native-resolution pass, out of range clamps) or off */
void rd__upscale_set(bool on, float ratio);
/* The settings' source (rd_present.c rd__apply_display, before and after
 * the display scale is known; displayScale 0 when not yet): Upscaling =
 * FSR 3.1 in force (rd_upscale_temporal.c rd__upscale_temporal_on) turns
 * the upscale point on at the quality's ratio (Native AA under the Original
 * preset or at a display scale of 1 or less).  rd__upscale_set's switch,
 * when on, wins (the tools' and the tests' upscaler, [dev] upscale_test). */
void rd__upscale_follow(const RdSettings *st, bool crt, float displayScale);
/* The upscale point is the settings' FSR 3.1 (the hook dispatches FSR 3.1,
 * rd_fsr3.c), not rd__upscale_set's test */
bool rd__upscale_by_fsr3(void);
/* Asked (the switch on, or FSR 3.1 by the settings) and not under the CRT
 * filter, which shows the PS2's pixels at 1x */
bool rd__upscale_wanted(void);
/* The ratio asked (1..3), 0 when neither source asks */
float rd__upscale_ratio(void);
/* The jitter's phase count (0: no jitter; at most 1024) */
void rd__upscale_set_phases(uint32_t n);
uint32_t rd__upscale_phases(void);

/* The render scale for the display scale dsx, dsy (pure): d / ratio, scaled
 * down together to the cap on the vertical scale (cap > 0: Auto's step),
 * not below d / 3 nor 1 */
void rd__upscale_render_scale(float dsx, float dsy, float ratio, float cap, float *sx, float *sy);
/* rd__apply_display: whether (on, dsx, dsy) differs from the split in
 * force, and the split put in force */
bool rd__upscale_split_differs(bool on, float dsx, float dsy);
void rd__upscale_split_set(bool on, float dsx, float dsy);
/* The split in force (the marker dispatches, the fades fold is off) */
bool rd__upscale_point_on(void);
/* DISPLAY's scale: the display scale with the split in force, else the
 * scene's (g_rd.sceneSx/Sy, 1 when unset) */
void rd__display_scale(float *sx, float *sy);

/* rd_replay.c rd__replay_frame_here, before a replay: the jitter of a
 * present that draws a picture; a re-shown one counted */
void rd__upscale_replay_begin(int keep, bool present);

struct Replay;
/* rd_replay.c's list loop at an RDC_UPSCALE_POINT c of frame f, the split
 * in force, or at the display layer's boundary (rd_layer.h, any command
 * c), the open pass ended: the layer begun at its boundary; at the marker,
 * with no layer begun, the edge guard and the dispatch; msaa: SCENE is
 * drawn multisampled (no dispatch) */
void rd__upscale_point(struct Replay *r, const RdFrame *f, const RdCmd *c, bool msaa);
/* The dispatch for frame f's marker (the edge guard, the hook's inputs,
 * COMPOSITE written, once a replay): from rd__upscale_point, or from the
 * display layer's boundary (rd_layer.c) with keepRun the keep frames held
 * since the last dispatch (a new history when any).  False when nothing
 * was written (refused, logged) */
bool rd__upscale_dispatch_at(struct Replay *r, const RdFrame *f, const RdCmd *marker, bool msaa,
                             uint32_t keepRun);
/* COMPOSITE (the hook's output, UPSCALED in the logs) made at the display
 * scale in its reserved slot (rd_layer.h RD_LAYER_SLOT_COMPOSITE): SCENE's
 * GS size, DISPLAY's scale, Full pixel's ox and guard; false without it */
bool rd__upscale_composite_ensure(void);
/* rd_blur_border.c: the reduction's sprite under state st samples UPSCALED
 * in SCENE's place (true: SCENE's record lent until rd__upscale_remap_end);
 * under the display layer SCENE's record already holds the composite
 * (counted, false) */
bool rd__upscale_remap_begin(const RdStateBlock *st);
void rd__upscale_remap_end(void);

/* The hook's inputs.  Textures in the states the pointers hold (the hook
 * moves them as it needs and leaves them tracked there); colour and depth at
 * the render scale (texels), out at the display scale. */
typedef struct RdUpscaleIn {
    RhiTexture color;
    RhiState *colorState;
    uint32_t colorW, colorH;
    RhiTexture depth; /* SCENE's depth sampled (D32F_S8, depth aspect), in RHI_STATE_DEPTH_READ
                         in place or RHI_STATE_SHADER_READ as the copy; id 0 none */
    uint32_t depthW, depthH;
    RhiTexture velocity; /* RGBA16F, xy texels current to previous; id 0 none */
    RhiState *velocityState;
    uint32_t velocityW, velocityH;
    RhiTexture out; /* UPSCALED, RGBA8 */
    RhiState *outState;
    uint32_t outW, outH;
    float renderSx, renderSy, displaySx, displaySy;
    float jitter[2];   /* texels of the render target, x right, y down (rd_jitter.h) */
    bool reset;        /* a new history: the frame's cut or its marker's RD_UPSCALE_NO_HISTORY,
                          the first dispatch, a new size */
    uint32_t presents; /* presents since the last dispatch, this one included (re-shown ones) */
    uint32_t frame;    /* the frame's number */
    /* the frame camera (false: none): near and far planes in the game's
     * units, the vertical angle in radians */
    bool camera;
    float cameraNear, cameraFar, fovY;
} RdUpscaleIn;

/* The temporal upscaler's dispatch (rd_upscale_hook.c): writes in->out from
 * in->color; false when it wrote nothing (the reduction then reads SCENE).
 * Outside any pass on the replay's command list, with the plain FrameCB
 * mapping (rd__frame_group is the present's: a pass sets its own viewport,
 * not rd__replay_viewport's exact one). */
bool rd__upscale_dispatch(RhiCommandList cl, const RdUpscaleIn *in);
/* The hook's name for the log ("the test's bilinear upscale") */
const char *rd__upscale_hook_name(void);

/* The counters since start (the replay tool's --stats, the tests) */
typedef struct RdUpscaleStats {
    uint32_t points;               /* markers met with the split in force */
    uint32_t dispatched;           /* of them, UPSCALED written */
    uint32_t resets;               /* of them, told a new history (RdUpscaleIn.reset) */
    uint32_t refused;              /* not dispatched (logged: the first 8, then every 100th) */
    uint32_t remapped;             /* reduction sprites that read UPSCALED */
    uint32_t guardCols, guardRows; /* edge guard copies */
    uint32_t reshown;              /* presents without a scene replay */
    uint32_t jittered;             /* presents given a jitter from the sequence */
    uint32_t outW, outH, colorW, colorH; /* the last dispatch's sizes */
    /* the resets by their first reason (RD_UPSCALE_RESET_*) */
    uint32_t resetsBy[6];
    /* the display layer (rd_layer.h) */
    uint32_t keepHeld;      /* keep frames held: no dispatch */
    uint32_t layerFrames;   /* replays whose layer began */
    uint32_t layerDraws;    /* the actions drawn into COMPOSITE */
    uint32_t layerTextured; /* of them, textured */
    uint32_t keepCopies;    /* keep sprites drawn as the 1:1 copy of the held composite */
    uint32_t keepNoSprite;  /* keep frames held with no keep sprite in the layer */
    uint32_t layerLate;     /* layers begun after a scene-class command of lists 11-12 */
    uint32_t preReductions; /* the reduction replayed into PRE_DISPLAY (the motion blur's read) */
    uint32_t layerRefused;  /* layers planned and not begun (logged: the first 8, every 100th) */
} RdUpscaleStats;

/* RdUpscaleStats.resetsBy: the first reason that holds */
enum {
    RD_UPSCALE_RESET_FIRST = 0, /* the first dispatch */
    RD_UPSCALE_RESET_SIZE,      /* a new size */
    RD_UPSCALE_RESET_CUT,       /* the frame's camera cut */
    RD_UPSCALE_RESET_KEEP,      /* the first picture after a run of keep frames */
    RD_UPSCALE_RESET_CAMERA,    /* the interpolator's camera snap */
    RD_UPSCALE_RESET_FORGOTTEN  /* the attach's mark (RD_UPSCALE_NO_HISTORY) */
};

const RdUpscaleStats *rd__upscale_stats(void);
/* the same counters for the display layer to count into (rd_layer.c) */
RdUpscaleStats *rd__upscale_stats_layer(void);
/* COMPOSITE (UPSCALED) read back (RGBA8, w * h * 4), false without it */
bool rd__upscale_read(void *dst, size_t size, uint32_t *w, uint32_t *h);
/* rd_core.c createNamedTargets and the present's shutdown: COMPOSITE and
 * the display layer's other slots (rd_layer.h), the edge guard's strips and
 * the depth copy given back */
void rd__upscale_free(void);

#ifdef __cplusplus
}
#endif

#endif
