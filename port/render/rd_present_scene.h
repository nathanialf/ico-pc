/* rd_present_scene.h: the present from SCENE (port/render/rd_present_scene.c;
 * docs/port/RENDER_POST.md "The present from SCENE (off by default until
 * measured)").
 *
 * Where nothing but the present's box blit reads DISPLAY before DISPLAY is
 * written again, the reduction (list 12's black and textured sprites into
 * DISPLAY) is not drawn: DISPLAY is owed, its texels g(SCENE(x + dx,
 * y + dy)), g the reduction's own per-texel arithmetic (the fades fold's
 * steps, its texture function with the tint), (dx, dy) the whole SCENE
 * texels the reduction's sample lies from its DISPLAY texel.  The present
 * samples SCENE through g instead of DISPLAY (blit.hlsl blit_scene_ps), and
 * any other reader gets DISPLAY drawn on demand from the same SCENE first
 * (the same arithmetic, one texel a pixel by Load): a later replay that
 * reads DISPLAY or draws on SCENE before DISPLAY is drawn again
 * (rd__present_scene_begin), a read of DISPLAY outside a replay
 * (rd__present_scene_readback: the F12 dump, a save state's picture, the
 * tool's PNGs), a present that cannot read SCENE (rd__present_scene_draw).
 *
 * Off by default in v0.11.5 (the picture is not byte for byte the
 * reduction's: the rounding of g comes before the present's filter, not
 * after); the owner decides after the measurement.  ICO_RD_PRESENT_SCENE=0|1
 * (tests and the measurement only: docs/port/TESTING.md) overrides the
 * default; no config key. */
#ifndef PORT_RENDER_RD_PRESENT_SCENE_H
#define PORT_RENDER_RD_PRESENT_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

struct Replay;
struct IcoDrawCB; /* shader_consts.h */

/* Why the reduction was drawn (kept), or why DISPLAY was drawn on demand:
 * RdPerfRecord.presentSceneWhy and presentSceneDrawWhy, the 10 s line and
 * rd_replay_tool --stats (rd__present_scene_why_text) */
enum {
    RD_PS_WHY_NONE = 0,   /* taken (not drawn; the present reads SCENE) */
    RD_PS_WHY_OFF,        /* the switch off */
    RD_PS_WHY_DEVICE,     /* no device, or no blit_scene_ps in the shader table */
    RD_PS_WHY_WINDOW,     /* a pass window (the tool's bisect reads targets mid-frame) */
    RD_PS_WHY_SCALE1,     /* the reduction's GS sprite model (scale 1: Original, 1x) */
    RD_PS_WHY_MSAA,       /* the MSAA epoch */
    RD_PS_WHY_UBER,       /* GS state as shader data (the attachment read's twin, no fold) */
    RD_PS_WHY_UPSCALER,   /* an upscaler set (FSR 1.0 at the present, a temporal one) */
    RD_PS_WHY_LAYER,      /* the display layer */
    RD_PS_WHY_CRT,        /* the CRT filter (reads DISPLAY's own lines) */
    RD_PS_WHY_DOWNSAMPLE, /* Downsampling (DISPLAY box-averaged first) */
    RD_PS_WHY_LINES,      /* the present doubles DISPLAY's lines (not full height) */
    RD_PS_WHY_DEPTH,      /* the effects depth (its own blit) */
    RD_PS_WHY_CAPTURE,    /* a capture armed or pending */
    RD_PS_WHY_REDUCE,     /* the present box-averages DISPLAY first (more texels than pixels) */
    RD_PS_WHY_BORDER,     /* the border closed (the reduction's scissor crops DISPLAY) */
    RD_PS_WHY_STRIPS, /* the open border's repeated outer columns (the frame at its exact width) */
    RD_PS_WHY_FORM,   /* the reduction's sprites not in the form g takes */
    RD_PS_WHY_RESAMPLE, /* not one SCENE texel a DISPLAY texel at a whole offset */
    RD_PS_WHY_BARS,     /* the fades fold's bars (their rows' split filter) */
    RD_PS_WHY_AFTER,    /* DISPLAY or SCENE touched after the reduction in the frame */
    /* drawn on demand (presentSceneDrawWhy) */
    RD_PS_WHY_READ, /* a later replay reads DISPLAY first (a keep frame, the motion blur's held copy) */
    RD_PS_WHY_SCENE,    /* a later replay draws on SCENE and not DISPLAY again */
    RD_PS_WHY_READBACK, /* a read of DISPLAY outside a replay (F12, a state's picture, the tool) */
    RD_PS_WHY_COUNT
};

const char *rd__present_scene_why_text(int why);

/* The switch: the default (the one constant rd_present_scene.c holds) or
 * ICO_RD_PRESENT_SCENE=0|1; its line once ("rd: present from SCENE: off
 * ..." or "on ..."). */
bool rd__present_scene_on(void);
/* Tests: -1 the default and the environment again, 0 off, 1 on. */
void rd__present_scene_force(int mode);
/* rd_core.c, a context's start: no DISPLAY owed from another context (its
 * targets are new) */
void rd__present_scene_init(void);

/* The predicate's inputs (rd__present_scene_plan gathers them from the
 * replay; the tests set them), each true where it keeps the reduction but
 * mapOk, borderOpen and formOk, which are true where they allow it. */
typedef struct RdPresentSceneIn {
    bool on, device, window, scaled, msaa, uber, upscaler, layer;
    bool borderOpen, strips, formOk, mapOk, bars, after;
    int presentWhy; /* rd__present_scene_present_why (rd_present.c): 0 none */
} RdPresentSceneIn;

/* RD_PS_WHY_NONE when the reduction may go undrawn, else the first reason
 * in the order of the enum. */
int rd__present_scene_why(const RdPresentSceneIn *in);

/* One axis of the reduction's hardware sprite (rd_blur.c
 * rd__blur_screen_fallback's pair after rd__blur_snap_verts, as rd_replay.c
 * convVtx, snapAxis and the exact viewport draw it): corners pa, pb (12.4
 * window coordinates), their texture coordinates ua, ub (12.4 GS texels:
 * uvFixed), the target's pixel 0 at origin16 (12.4), DISPLAY's texels per
 * GS pixel sd and left-out texels od (Full pixel), SCENE's sx and ox, n
 * DISPLAY texels on the axis.  True when every DISPLAY texel's centre is
 * covered and samples the centre of SCENE texel X + *shift (one SCENE texel
 * a DISPLAY texel, a whole offset); false otherwise. */
bool rd__present_scene_axis(int32_t pa, int32_t pb, double ua, double ub, int32_t origin16,
                            double sd, double od, double ss, double os, uint32_t n, int32_t *shift);

/* The verdict of a replay's frame on an owed DISPLAY (rd__present_scene_begin):
 * RD_PS_SETTLE_KEEP nothing in it touches DISPLAY or SCENE (the still
 * re-present's empty keep frame: the present reads SCENE again),
 * RD_PS_SETTLE_DROP DISPLAY written whole (the reduction's black sprite
 * over the whole target, a clear of it) before anything reads it,
 * RD_PS_SETTLE_DRAW DISPLAY drawn now (*why RD_PS_WHY_READ: a command
 * reads it first; RD_PS_WHY_SCENE: SCENE drawn and DISPLAY not written
 * again).  disp and scene are the targets' ids. */
enum { RD_PS_SETTLE_KEEP = 0, RD_PS_SETTLE_DROP, RD_PS_SETTLE_DRAW };

int rd__present_scene_settle(const RdFrame *f, int keep, uint32_t disp, uint32_t scene, int *why);

/* The replay's hooks.
 * rd__present_scene_begin  rd_replay.c replayFrame, after the store plan's
 *                      scan, before the new targets' clears: an owed
 *                      DISPLAY settled for this frame (drawn on demand in
 *                      the replay's list when the verdict says so)
 * rd__present_scene_skip   rd_blur_border.c blurPostSprite, after SCENE's
 *                      border repeat: true when the command (the
 *                      reduction's black sprite, then its textured one) is
 *                      not drawn; strips: the open border's columns would
 *                      be drawn after the textured sprite
 * rd__present_scene_readback  rd_replay.c rd__read_target, rd_present.c
 *                      rd_display_probe: DISPLAY (id) drawn on demand
 *                      before it is read */
void rd__present_scene_begin(const RdFrame *f, int keep);
bool rd__present_scene_skip(struct Replay *r, const RdFrame *f, const RdCmd *c, bool strips);
void rd__present_scene_readback(uint32_t id);

/* The present's side (rd_present.c rd__present_record).
 * rd__present_scene_owed      DISPLAY not drawn since the last reduction
 *                             was skipped, its SCENE unchanged
 * rd__present_scene_present_why  rd_present.c: the present's reasons to
 *                             read DISPLAY itself (the CRT filter,
 *                             Downsampling, the doubled lines, the effects
 *                             depth, a capture, the window's reduce); 0 none
 * rd__present_scene_ready     the box blit's pipeline for the output format
 * rd__present_scene_texture   SCENE's texture, made readable in cl
 * rd__present_scene_cb        the box blit's DrawCB fields for
 *                             blit_scene_ps (cb filled as blit() fills it)
 * rd__present_scene_shown     the present drew from SCENE (counted; the
 *                             10 s line)
 * rd__present_scene_draw      DISPLAY drawn from SCENE in cl (why
 *                             RD_PS_WHY_*: the present's reason, or a
 *                             reader's); nothing when not owed */
bool rd__present_scene_owed(void);
int rd__present_scene_present_why(void);
bool rd__present_scene_ready(RhiFormat outFmt);
RhiTexture rd__present_scene_texture(RhiCommandList cl);
void rd__present_scene_cb(struct IcoDrawCB *cb, bool tone);
void rd__present_scene_shown(void);
void rd__present_scene_draw(RhiCommandList cl, int why);

#endif /* PORT_RENDER_RD_PRESENT_SCENE_H */
