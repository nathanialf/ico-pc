/*
 * port/render/rd_layer.h
 *
 * The display layer (issue 203; docs/port/DISPLAY_LAYER.md).
 * While a temporal upscaler is in force (rd_upscale.h: the scene drawn at
 * the render scale, upscaled where the reduction reads it), the game's own
 * 2D layer is not drawn into SCENE: SCENE holds the 3D picture and the post
 * chain, the upscaler runs at the BOUNDARY (the first command after the
 * last scene-class command of lists 11 and 12, list 11's first at the
 * earliest), and every command from there to the reduction is drawn into
 * COMPOSITE, the upscaler's output at the display scale (DISPLAY's
 * geometry, Full pixel's ox and guard), which the reduction then reads.
 * The brightness, the keep sprite, the fades, the letterbox, the film
 * noise, the save tile and the game's text (list 12, DisplayFont.c) are
 * then drawn over the finished picture at the display scale, never
 * accumulated or sharpened by the upscaler.
 *
 * Scene-class (stays in SCENE, before the upscale): a VU draw, a shadow
 * command, the fog, a copy or an exact blend naming SCENE, a draw on SCENE
 * with the Z test on, DATE, a blend reading the destination alpha
 * (ALPHA's Ad), PRIM.AA1, Z write on (but a draw that does not test Z, the
 * letterbox's bars, while the present does not read SCENE's depth and no
 * frame read SCENE's depth before its clear: their Z goes nowhere), a draw
 * sampling SCENE's depth, and a draw into another target that samples
 * SCENE.  A frame whose lists 11-12 hold one (the debug lines, act.c and
 * the tools) begins the layer after it: what lies before goes through the
 * upscaler that frame (counted, logged).
 *
 * How the commands reach COMPOSITE: SCENE's record lends the composite's
 * texture and texel geometry (and no depth) from the boundary to the end of
 * the replay (lendScene), the way rd__upscale_remap_begin lent UPSCALED to
 * the reduction's sprite before; nothing recorded changes and every
 * command, its target ids, its scissor, its UI mirror and its half pixel
 * are replayed as recorded, only at the display scale.  The jitter is not
 * applied to SCENE's record while it is lent (rd_jitter.c).
 *
 * Keep frames (fbKeep: lists 11-12 only, ~256 a stage switch's fade): the
 * PS2 re-read its previous DISPLAY and darkened it 112/128 a tick
 * (rd_post.c postKeep).  Here there is no dispatch on a keep frame: at the
 * boundary the composite (the last picture, its layer included) is copied
 * whole into COMPOSITE_HELD, then the head's clear, the brightness and the
 * fades are drawn into the composite as recorded, and the keep sprite is
 * drawn as a 1:1 copy of COMPOSITE_HELD (UVs the GS position less the
 * frame's corner, a pixel's margin past the frame, NEAREST, the recorded MODULATE colour and state: one floor of
 * 112/128 a tick as on the PS2), DISPLAY's record lending COMPOSITE_HELD's
 * for that draw.  The next picture after a keep run resets the upscaler's
 * history.  The PS2's loop also compounded a vertical stretch and a tint
 * each tick; the hold does not (docs/port/DIVERGENCES.md).
 *
 * The motion blur (list 7, staticBlur.c) reads DISPLAY: with the layer it
 * reads PRE_DISPLAY instead, the reduction's two sprites replayed from SCENE
 * at the boundary into a target of DISPLAY's GS size at the render scale
 * (SCENE's scale and ox): what DISPLAY was before the upscaler existed, so
 * no upscaled picture feeds back into SCENE.  DISPLAY's record lends
 * PRE_DISPLAY's from the scan to the boundary in a frame that reads
 * DISPLAY in lists 0-10 (not while those lists copy DISPLAY: the
 * interpolator's held copy, rd_interp.c feedback, which moves to
 * PRE_DISPLAY / PRE_HELD with its ids, rd__layer_feedback_ids).
 *
 * Off (no temporal upscaler, the dev switch, MSAA, the tool's pass
 * window): the scan returns before touching anything and every picture is
 * the one before the layer existed, byte for byte.  [dev] display_layer =
 * false (ICO_RD_DISPLAY_LAYER=0) keeps the legacy path under the upscaler
 * (the 2D drawn into SCENE and upscaled with it) for an A/B.
 *
 * Reserved target slots: the top RD_LAYER_SLOTS of g_rd.targets, which
 * the temporary allocator never takes (rd_core.c), not named ids (the dumps
 * do not change: RD_DUMP_VERSION stays 17; the boundary is derived at
 * replay).
 */
#ifndef RD_LAYER_H
#define RD_LAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RD_LAYER_SLOTS 4
/* the upscaler's output and the layer's target (UPSCALED in the logs) */
#define RD_LAYER_SLOT_COMPOSITE (RD_MAX_TARGETS - 4)
/* a keep frame's copy of the composite before its layer draws */
#define RD_LAYER_SLOT_HELD (RD_MAX_TARGETS - 3)
/* the reduction of SCENE at the render scale, read for DISPLAY in lists 0-10 */
#define RD_LAYER_SLOT_PRE (RD_MAX_TARGETS - 2)
/* its copy held for a tick's later presents (rd_interp.c feedback) */
#define RD_LAYER_SLOT_PRE_HELD (RD_MAX_TARGETS - 1)
/* the first slot the temporary targets may not take */
#define RD_LAYER_SLOT_FIRST RD_LAYER_SLOT_COMPOSITE

/* A slot's name ("COMPOSITE", "COMPOSITE_HELD", "PRE_DISPLAY", "PRE_HELD"),
 * NULL for another slot */
const char *rd__layer_slot_name(uint32_t slot);
/* the slot's record (live or not) and its id (0 when it is not live) */
RdTargetRec *rd__layer_rec(int slot);
uint32_t rd__layer_id(int slot);
/* The slot made with geometry g (w, h, tw, th, sx, sy, ox; RGBA8, no depth),
 * its textures kept when tw x th is the same; usage extra besides render
 * target, sampled and copy source / destination.  False when the texture
 * could not be made. */
bool rd__layer_slot_make(int slot, const RdTargetRec *g, uint32_t usage, const char *name);
/* The slot's textures given back, the record no longer live */
void rd__layer_slot_free(int slot);

/* The plan of one replay (pure: rd_replay_tool --list without a device).
 * Ordinals as rd_replay.c's walk counts them (g_rdStoreOrd: every command
 * of the replayed lists from rd__first_list(keep), state commands
 * included).  ~0u: none. */
typedef struct RdLayerPlan {
    uint32_t count;     /* the commands walked */
    uint32_t list11Ord; /* list 11's first command */
    uint32_t markerOrd; /* list 12's RDC_UPSCALE_POINT naming SCENE -> DISPLAY */
    uint32_t boundaryOrd;
    uint32_t keepOrd;   /* a keep frame's keep sprite, in the layer */
    uint32_t keepAtOrd; /* the keep sprite found, wherever it lies */
    uint32_t redOrd[2]; /* the reduction's sprites after the marker (black, textured) */
    const RdCmd *marker, *red[2];
    RdStateBlock redSt[2];
    bool feedsDisplay;  /* a list 0-10 command samples DISPLAY */
    bool displayCopied; /* a list 0-10 copy or exact blend names DISPLAY */
    /* the last list 0-10 copy naming PRE_DISPLAY or PRE_HELD (rd_interp.c
     * feedback's held copy under the layer), ~0u none: DISPLAY's record
     * lends PRE_DISPLAY's after it, so the copy and the reads never see
     * one texture through two records */
    uint32_t preCopyOrd;
    bool depthRead; /* SCENE's depth tested before this frame clears it */
    int late;       /* the last scene-class command in lists 11-12: its kind (RD_LAYER_SC_*) */
    int lateList;
    uint8_t lateType;
    uint32_t draws, textured; /* the actions from the boundary to the marker */
} RdLayerPlan;

/* scene-class kinds (the "begun after" line) */
enum {
    RD_LAYER_SC_NONE = 0,
    RD_LAYER_SC_VU,
    RD_LAYER_SC_SHADOW,
    RD_LAYER_SC_FOG,
    RD_LAYER_SC_COPY,
    RD_LAYER_SC_ZTEST,
    RD_LAYER_SC_ZWRITE,
    RD_LAYER_SC_DATE,
    RD_LAYER_SC_AD,
    RD_LAYER_SC_AA1,
    RD_LAYER_SC_DEPTH_VIEW,
    RD_LAYER_SC_READ,
    RD_LAYER_SC_OTHER,
    RD_LAYER_SC_COUNT
};

const char *rd__layer_sc_name(int kind);

/* barsFree: a draw writing Z without testing it may join the layer;
 * depthCarried: a frame read SCENE's depth before its clear (in: from
 * earlier replays; the walk sets it when this frame does, before the
 * commands after) */
void rd__layer_plan(const RdFrame *f, int keep, bool effectsDepth, bool *depthCarried,
                    RdLayerPlan *p);

/* [dev] display_layer (ICO_RD_DISPLAY_LAYER=0 off): read once */
bool rd__layer_on(void);
/* rd_store_plan.c rd__store_plan_scan, before the walk: this replay's plan;
 * nothing when the split is not in force */
void rd__layer_scan(const RdFrame *f, int keep);
/* rd_replay.c's list loop at every command (ordinal ord): the layer's
 * boundary reached (the screen run, the VU merge and the open pass ended
 * before rd__upscale_point, which begins it); also puts back what the keep
 * draw changed once the walk is past it */
bool rd__layer_due(uint32_t ord);
struct Replay;
/* rd__upscale_point at the boundary: the pre-upscale reduction, the
 * dispatch or the keep frame's hold, then SCENE's record lent */
void rd__layer_begin(struct Replay *r, const RdFrame *f, bool msaa);
/* SCENE's record holds the composite (this replay's layer began) */
bool rd__layer_active(void);
/* rd_replay.c doScreen: the keep sprite at its ordinal replaced by the
 * 1:1 copy of COMPOSITE_HELD (v itself otherwise) */
const RdScreenVtx *rd__layer_keep_verts(struct Replay *r, const RdCmd *c, const RdScreenVtx *v);
/* rd_replay.c after the lists: everything lent given back, counters, the
 * 10 s line */
void rd__layer_end(struct Replay *r);
/* PRE_DISPLAY's and PRE_HELD's ids (0 when not made), PRE_HELD made at
 * PRE_DISPLAY's geometry when asked.  True when the layer is in force
 * (a temporal upscaler, the dev switch on) and both are made: rd_interp.c
 * feedback then holds PRE_DISPLAY in PRE_HELD instead of DISPLAY in
 * DISPLAY_HELD (it asks without makeHeld: the interpolation makes no GPU
 * call; the boundary makes PRE_HELD beside PRE_DISPLAY while the
 * interpolation is on). */
bool rd__layer_feedback_ids(uint32_t *pre, uint32_t *preHeld, bool makeHeld);
/* The composite holds a picture (a layer frame or a dispatch since it was
 * made); false after it was made again */
bool rd__layer_held(void);
void rd__layer_composite_new(void);
/* rd__upscale_free: every slot's textures given back */
void rd__layer_free(void);

#ifdef __cplusplus
}
#endif

#endif
