/* rd_replay.h: what rd_replay.c shares with rd_replay_vu.c, its VU draws:
 * the replay's state (Replay, ScreenRun), a draw's setup (DrawSetup), the
 * replay's helpers the VU draws call (rd__replay_*, each the function of
 * rd_replay.c named beside it), the VU draw itself and the vertex
 * pre-pass. */
#ifndef PORT_RENDER_RD_REPLAY_H
#define PORT_RENDER_RD_REPLAY_H

#include "rd_internal.h"
#include "rd_passring.h"
#include "shader_consts.h"

/* Consecutive screen-prim commands that bind the same things are one draw
 * (doScreen).  A command whose plan is one draw opens a run or joins the
 * open one; the run's vertices are staged in s_runVx and drawn by
 * flushScreenRun, which every action that records anything calls first
 * (endPass, replayFrame, doScreen). */
typedef struct ScreenRun {
    uint32_t count;  /* vertices staged; 0: no run */
    uint32_t stride; /* bytes per vertex (sprite, STQ or AA1 vertex) */
    uint8_t prim;    /* the commands' RD_PRIM_* */
    uint32_t passSerial, writeSerial, dateSerial, dateFor;
    RhiPipeline pipe;
    RdPipeKeyInt key;
    RdUniform frame;
    IcoDrawCB cb;
    /* without dual-source blending, the two-pass fallback's alpha pass,
     * drawn after the run's colour pass (passes 2), zeroed when there is
     * none */
    uint8_t passes;
    RhiPipeline pipe2;
    RdPipeKeyInt key2;
    IcoDrawCB cb2;
    RhiBindGroup g2;
    uint32_t tex, sampler, dateTex;
    RhiRect sc;
    RdStateBlock st;
} ScreenRun;

typedef struct Replay {
    RdStateBlock st;
    int passOpen;
    uint32_t passColor, passDepth;
    /* the exact blend (rd_dst_read.c): the open pass is a storage pass (the
     * colour target bound as a storage texture, no colour attachment) or
     * its colour attachment is readable (the attachment read); the scissor
     * last set, which a pass begun again for the other kind takes back;
     * group 3 of the open pass */
    int passStorage, passReadable;
    RhiRect sc;
    RhiBindGroup dstGroup;
    uint32_t dstGroupSerial;
    RdUniform frameBG;
    uint32_t frameKey[6]; /* colour, gsW, gsH, useOffset, pass serial, picture */
    /* the VU draw being bound: its camera's picture (RdCmd b[3] | b[6] << 8,
     * rd_mesh.c pushVu), 0 for the full screen's and every other draw */
    uint32_t picture;
    int mirror;    /* the draw being bound is a flipped UI draw (scissor too) */
    int halfAdded; /* sceneKeepsHalf gave the target in force the frame head's half pixel */
    uint32_t passSerial;
    uint32_t writeSerial; /* bumped by every action that may change a target's alpha */
    uint32_t dateFor;     /* target id the DATE snapshot holds, 0 = none */
    uint32_t dateSerial;  /* writeSerial when it was taken */
    RhiRect dateArea;     /* the texels it holds (dateSnapshot) */
    ScreenRun run;        /* the screen-prim run not drawn yet */
    /* the GPU timing's picture effect (rd__perf_post): the one the target
       in force or the last post sprite belongs to, and a change waiting for
       the open run's draw (the run's draws belong to the effect before);
       perfTargetEffect the target's alone (perfTargetPost), which a list
       starts with: a post sprite's effect ends with its list */
    int perfPost, perfPending, perfPendingPost, perfTargetEffect;
    /* the reduction's fallback sprite being drawn keeps the GS's filter on
     * both axes (doBlurSpriteDraw: blurAbove1xSteps 0), so rd__sprite_above1x
     * leaves its flag off */
    int a1xGs;
    /* the tiler passes (rd_replay.c).  passDepthReadable: the open pass
     * binds its depth readable (the fog's local read, beginPass).  DATE
     * through the stencil bit: dateStNow the draw being planned takes it
     * (prepareDraw: expandPasses turns its passes into the bit's tests),
     * and what bit 6 holds in the open pass: the destination alpha's MSB of
     * target dateStFor over dateStArea as of write serial dateStSerial, in
     * the pass of serial dateStPass (rd__replay_date_take) */
    int passDepthReadable;
    int dateStNow;
    uint32_t dateStFor, dateStSerial, dateStPass;
    RhiRect dateStArea;
} Replay;

/* What a draw binds besides its geometry: doScreen and the VU draws. */
typedef struct DrawSetup {
    RdTargetRec *tc;
    uint32_t tdId;
    RhiFormat depthFmt;
    RhiTexture tex, dateTex;
    uint32_t tw, th, tfmt;
    int textured;
    int mipmapped;   /* the image texture has generated mips */
    int replacement; /* texture packs: the image texture is a pack replacement */
    int gsLevels;    /* the image texture's GS levels as uploaded (RdTexRec.gsLevels), 0 none:
                        sampled at the GS's level (DF_GS_LOD) */
    int screenCopy;  /* a screen-space pass, not geometry (screenCopy): no added half pixel */
    /* the texture is a Full pixel crop of the frame (RdTargetRec.ox): its
     * UVs, in GS texels of the target, address its own texels at its scale
     * less its left-out ones (fillDrawCB, common.hlsli gs_uv_norm); NULL
     * for every other texture */
    const RdTargetRec *texCrop;
    /* a screen-prim command (doScreen, doScreenWrap): its read of a block
     * smaller than the frame keeps the game's REPEAT under the open
     * border (drawWrap, rd__block_keeps_repeat) */
    int screenPrim;
    /* DATE through the stencil bit (prepareDraw): bindDrawEx writes the
     * bit over dateWant (the texels the draw can touch, the whole target
     * when dateWantAll) in the draw's pass before it (rd__replay_date_take);
     * no R8 snapshot (dateTex the dummy) */
    int dateStencil, dateWantAll;
    RhiRect dateWant;
    /* above 1x: a sprite command's steps (rd_fx_plan.c rd__a1x_sprite_steps,
     * doScreen), DrawCB.scale.w of its sprite_above1x passes; 0 the pixel's
     * footprint */
    uint32_t a1xSteps;
} DrawSetup;

/* rd_replay.c: beginPass, bindDrawKeep, bindGroup, s_bindEpoch, drawCmd,
 * drawIndexedCmd, dstBind, dstPassMode, expandPasses, fillDrawCB,
 * prepareDraw, the MSAA epoch's sample count this replay (s_ms.want),
 * passPush (a pass begun outside beginPass: its pass ring entry and its
 * number in the pass window) */
void rd__replay_begin_pass(Replay *r, RdTargetRec *c, RdTargetRec *d, uint32_t cid, uint32_t did,
                           RhiLoadOp colorLoad, const float clear[4], RhiLoadOp depthLoad,
                           float clearDepth, bool storage);
RhiBindGroup rd__replay_bind_draw_keep(Replay *r, const DrawSetup *ds);
RhiBindGroup rd__replay_bind_group(RhiBindGroupLayout layout, const RhiBinding *b, uint32_t n);
uint32_t rd__replay_bind_epoch(void);
void rd__replay_draw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                     uint32_t instanceCount);
void rd__replay_draw_indexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                             int32_t vertexOffset, uint32_t instanceCount);
void rd__replay_dst_bind(Replay *r, const RdPipeKeyInt *k);
void rd__replay_dst_pass_mode(Replay *r, const RdPipeKeyInt *k);
int rd__replay_expand_passes(const Replay *r, const RdDrawPass *in, int n, RdDrawPass out[8]);
void rd__replay_fill_draw_cb(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds,
                             IcoDrawCB *cb);
bool rd__replay_prepare_draw(Replay *r, DrawSetup *ds, const RhiRect *dateArea);
uint32_t rd__replay_msaa_want(void);

/* rd_msaa.c: the MSAA epoch's resolve (rd_replay.c msaaEnd; docs/port/MSAA.md).
 * rd__msaa_resolve: one pass of msaa_resolve_ps into SCENE's (ts) colour and
 * depth (DONT_CARE loads, the stencil loaded and kept) from SCENE_MS's
 * colour and depth (samples each, put in SHADER_READ), the covered-majority
 * rule against clearDepth when hasClear (else off, a line once a run);
 * false, with nothing recorded and a line once, when its pipeline or its
 * texture group cannot be made (the caller resolves another way).
 * rd__msaa_close_note: where the epoch closed (the list in force, or the
 * replay's end), its line once a list and cause; returns
 * RdPerfRecord.msaaCloseAt's value */
enum {
    RD_MSAA_CLOSE_READ,   /* SCENE's colour or depth read, copied or snapshot (the hook) */
    RD_MSAA_CLOSE_SHADOW, /* a pass on SCENE's depth with another colour (the shadow count's) */
    RD_MSAA_CLOSE_DEPTH,  /* a pass on SCENE with another depth */
    RD_MSAA_CLOSE_END,    /* the replay's end */
    RD_MSAA_CLOSE_COUNT
};

bool rd__msaa_resolve(RhiCommandList cl, RdTargetRec *ts, RhiTexture color, RhiState *colorState,
                      RhiTexture depth, RhiState *depthState, uint32_t samples, bool hasClear,
                      float clearDepth);
uint8_t rd__msaa_close_note(int cause);
const void *rd__replay_pass_push(RdPassKind kind, uint32_t color, uint32_t cw, uint32_t ch,
                                 uint32_t depth, uint32_t dw, uint32_t dh, int storage, int post);
/* rd_replay.c frameRefresh's FrameCB of a VU draw of the camera picture
 * picture (RdCmd b[3] | b[6] << 8) under the state st (its target, size,
 * offset and depth), for the record stages of rd_replay_vu.c rd__vu_prepass
 * to evaluate a command's triangles as its draw's vertex shader would; id
 * 0 when the target has no texture */
RdUniform rd__replay_vu_frame(const RdStateBlock *st, uint32_t picture);
/* rd_replay.c sceneHeadHalf and sceneKeepsHalf, for a walk of the lists
 * outside replayFrame (rd_replay_vu.c rd__vu_prepass): the frame head's
 * half offset on SCENE, and after a TARGET command the half pixel the walk
 * adds to a later SCENE target set with the screen offset (1: added) */
uint32_t rd__replay_scene_head_half(const RdFrame *f);
int rd__replay_scene_keeps_half(RdStateBlock *st, int list, uint32_t headHalf);
/* rd_replay.c for rd_replay_tiler.c: flushScreenRun, bindDraw,
 * scissorRect, blurFlags, blurAux1, blurCoversTarget, blurSwapTarget,
 * takeSnap, perfTargetPost, sceneKeepsHalf, perfPost and the FIX 128 skip
 * (rd__set_blur_fix128_skip) */
void rd__replay_flush_run(Replay *r);
RhiBindGroup rd__replay_bind_draw(Replay *r, const DrawSetup *ds);
bool rd__replay_scissor(const Replay *r, const RdTargetRec *tc, RhiRect *sc);
uint32_t rd__replay_blur_flags(const RdDrawState *d, uint32_t lines, int textured, int dstRead);
void rd__replay_blur_aux1(IcoDrawCB *cb);
bool rd__replay_blur_covers(const Replay *r, const RdTargetRec *tc, const RdPostRec *p,
                            const RdDrawState *d, const RdPipeKeyInt *key, int useDepth);
bool rd__replay_blur_swap(Replay *r, RdTargetRec *tc);
bool rd__replay_take_snap(Replay *r, RdTargetRec *tc, RhiRect area);
int rd__replay_perf_target_post(const Replay *r, const RdCmd *c);
void rd__replay_perf_post(Replay *r, int post);
bool rd__replay_blur_fix128_skip(void);
/* rd_replay.c for rd_blur_border.c and rd_blur_smooth.c: doBlurSpriteDraw
 * (one post sprite, the state block's scissor, wrap, texture and FIX in
 * force), borderOpen (Full pixel on, or the border mask on a cropped
 * frame) and setViewport (a pass's viewport for a texture w x h: the exact
 * one while the lists record) */
void rd__replay_blur_sprite_draw(Replay *r, const RdFrame *f, const RdCmd *c);
bool rd__replay_border_open(void);
void rd__replay_viewport(uint32_t w, uint32_t h);
/* rd_blur_border.c: an RDC_POST_STUB of an rd__is_blur_kind kind, the list
 * loop's (the fold, the reduction's open border and its outer columns, the
 * motion blur under Smooth) */
void rd__blur_post_sprite(Replay *r, const RdFrame *f, const RdCmd *c);
/* rd_blur_border.c: doFog's fog drew on SCENE (r's colour target): the
 * repeat of SCENE's outer columns (rd_internal.h rd__scene_border_state) is
 * done before this replay's first post sprite that reads SCENE */
void rd__scene_border_arm(const Replay *r);
/* rd_blur_smooth.c: the motion blur's sprite c under Smooth
 * (rd_internal.h rd__blur_smooth_arm): the accumulator's passes, then the
 * recorded sprite reading the accumulator with FIX'; false when Smooth is
 * not in force for this replay (the sprite is then drawn as recorded) */
bool rd__blur_smooth_sprite(Replay *r, const RdFrame *f, const RdCmd *c);

/* rd_crack_fill.c: the crack filler (rd_internal.h rd__crack_fill_*), from
 * doFog before the fog reads the depth (after the pass before the fog
 * ended, but on the local read): tc the fog's colour target, tz its
 * depth's (the filler draws when they are SCENE both), copy the fog's depth
 * copy object (filled here with SCENE's depth before the fill, as the copy
 * path fills it), msaa the MSAA epoch at the fog (RD_CRACK_MSAA_*: open,
 * not drawn then; closed this replay, drawn on the resolved picture; or
 * neither), local the fog draws by the local read (fogLocalFor): the
 * fill is drawn in a pass begun with the depth readable and left open, the
 * one the fog keeps */
enum {
    RD_CRACK_MSAA_NONE,     /* no MSAA, or the epoch not opened yet: single-sampled */
    RD_CRACK_MSAA_OPEN,     /* the epoch open at the fog (per-sample fog): not drawn */
    RD_CRACK_MSAA_RESOLVED, /* the epoch closed before the fog (msaaEnd's resolve) */
};

void rd__crack_fill(Replay *r, RdTargetRec *tc, const RdTargetRec *tz, RdDepthCopy *copy, int msaa,
                    bool local);
/* the target's passes readable for the exact blend's attachment read (exactReadable) */
bool rd__replay_exact_readable(uint32_t target);

/* The pixel diet's replay hooks.
 * rd__fx_plan_draw  rd_fx_plan.c (RdProfile.fxPlan, fxDstLocal): a
 *                   staticBlur sprite as doBlurSpriteDraw has it before its
 *                   snapshot (RdFxDraw) drawn another way (a constant fold,
 *                   the fixed-function premultiplied add, the destination by
 *                   local read); true: drawn (doBlurSpriteDraw returns),
 *                   false: doBlurSpriteDraw draws it (the snapshot path)
 * rd__fill_draw_cb_extra  rd_pixel_plan.c: what a draw pass's DrawCB needs
 *                   besides fillDrawCB's fields, at its end (every planned
 *                   pass of the screen prims and the VU draws) */
typedef struct RdFxDraw {
    const RdFrame *f;
    const RdCmd *c;
    const RdPostRec *p;      /* the sprite's record */
    const RdDrawState *d;    /* the GS state (FIX 128 normalised: ABE off) */
    RdTargetRec *tc;         /* the colour target (RGBA8) */
    RdTargetRec *td;         /* its depth target, NULL when it has none of its size */
    RdTargetRec *src;        /* the source target, NULL for an image or none */
    RhiTexture srcTex;       /* the image, the source target's colour or g_rd.dummy */
    const RdPipeKeyInt *key; /* rd__blur_key's (fx_sprite_ps) */
    const float *uv;         /* rd__blur_uv_rect's four UVs */
    uint32_t tw, th, tfmt;   /* the texture's size and TEXFMT */
    uint32_t steps;          /* blurAbove1xSteps (0: the GS's own filter) */
    int useDepth, textured, dstRead, selfSample;
} RdFxDraw;

bool rd__fx_plan_draw(Replay *r, const RdFxDraw *x);
/* rd_fx_plan.c's other parts:
 * rd__fx_fold        an untextured sprite's blend per colour channel: true
 *                    when each is the destination for every Cd and Ad (bit
 *                    ch of *identity) or one value for every Cd and Ad
 *                    (rgb[ch]); RGBAQ rgba
 * rd__a1x_sprite_steps  a sprite command's above-1x steps (DrawSetup.a1xSteps)
 * rd__sprite_above1x  fillDrawCB: 1 when a pass takes DF_ABOVE1X (sprite.hlsl
 *                    sprite_above1x); redId the shadow count's GS-size
 *                    texture (rd_replay.c s_shadowRed), 0 none; the shadow
 *                    composites only with RdProfile.shadowBspline
 * rd__fx_reduction_clears  the reduction's black sprite on a scaled target
 *                    (the pair v as doBlurSpriteDraw draws it) is the clear
 *                    of colour col (doBlurSpriteDraw records it)
 * rd__replay_end_pass  rd_replay.c endPass */
bool rd__fx_fold(const RdDrawState *d, const uint8_t rgba[4], uint8_t *identity, uint8_t rgb[3]);
uint32_t rd__a1x_sprite_steps(const Replay *r, const DrawSetup *ds, const IcoSpriteVertex *v,
                              uint32_t nv, uint8_t prim);
int rd__sprite_above1x(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds, uint32_t redId);
bool rd__fx_reduction_clears(Replay *r, const RdScreenVtx v[2], float col[4]);
void rd__replay_end_pass(Replay *r);
/* box_reduce_ps's DrawCB for the count target tc, its scaled texels into
 * its GS size (shadowReduce; shadow_count_ps reads the same fields): the
 * source texels per output pixel, the source size, a Full pixel source's
 * left-out texels (rd_shadow_one.c) */
void rd__shadow_reduce_cb(const RdTargetRec *tc, IcoDrawCB *cb);
/* rd_shadow_one.c: the shadow count of the state block's targets tc
 * (colour) and td (depth-stencil) in one draw (shadow_count_ps) into dst at
 * tc's GS size, reduceCb rd__shadow_reduce_cb's; the open pass ended first.
 * False, nothing drawn, when the stencil view, the pipeline, the table or
 * the bind group is missing (the seven draws then). */
bool rd__shadow_one_draw(Replay *r, RdTargetRec *tc, RdTargetRec *td, RhiTexture dst,
                         RhiState *dstState, const IcoDrawCB *reduceCb);
void rd__fill_draw_cb_extra(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds,
                            IcoDrawCB *cb);
/* rd_const_read.c: a staticBlur sprite whose source target holds only its
 * last clear drawn untextured in that colour (docs/port/WORK_BUFFERS.md, "A
 * cleared source read as its colour").
 * rd__const_cmd        every command the list loop walks
 *                      (rd__replay_fold_at): the actions counted
 * rd__const_note_clear rd_replay.c pendClear: target id's colour cleared
 *                      to col (k / 255)
 * rd__const_read       doBlurSpriteDraw, a sprite into tc (tcId) sampling
 *                      target src (srcId) with TEXFMT tfmt: true when src
 *                      held only its clear, p->rgba then the texture
 *                      function's colour of it (the sprite drawn
 *                      untextured); counts RdPerfRecord.constReads /
 *                      constKept (rd__const_reads_on: rd_internal.h) */
void rd__const_cmd(const RdCmd *c);
void rd__const_note_clear(uint32_t id, const RdTargetRec *t, const float col[4]);
bool rd__const_read(const RdTargetRec *tc, uint32_t tcId, const RdTargetRec *src, uint32_t srcId,
                    const RdDrawState *d, RdPostRec *p, uint32_t tfmt);
/* rd_replay_tiler.c (docs/port/RENDER.md, "The replay").
 * rd__replay_date_take  DATE's stencil bit written in the draw's pass
 *                       (bindDrawEx, a draw with DrawSetup.dateStencil);
 *                       false: the draw is skipped
 * rd__replay_date_clear  DATE's stencil bit cleared over an area in the
 *                       open pass (a PRIM.AA1 DATE draw on the snapshot
 *                       path, rd_replay_aa1.inc aa1DateDraw); false: not
 *                       drawn
 * rd__replay_blur_fold  a staticBlur pair at its pass 1 c drawn as one pass
 *                       (rd__blur_post_sprite); true: drawn, and the loop skips
 *                       rd__replay_fold_skip() commands with their state
 *                       applied
 * rd__replay_fold_list  the list the loop walks (NULL at its end), the
 *                       list number and the frame head's half pixel;
 *                       rd__replay_fold_at the command being replayed
 * rd__replay_glow_edge_columns (rd_blur_border.c) the WORK1 columns 0 ..
 *                       n - 1 the glow's edge strip draws after c, pass 1
 *                       of blur(n) on a wide frame, under r's state, as
 *                       c's texels at its first drawn column n; 0: none */
bool rd__replay_date_take(Replay *r, const DrawSetup *ds);
bool rd__replay_date_clear(Replay *r, const DrawSetup *ds, const RhiRect *area);
bool rd__replay_blur_fold(Replay *r, const RdFrame *f, const RdCmd *c);
void rd__replay_fold_list(const RdCmd *cmds, uint32_t count, int list, uint32_t headHalf);
void rd__replay_fold_at(uint32_t index);
uint32_t rd__replay_fold_skip(void);
int rd__replay_glow_edge_columns(const Replay *r, const RdFrame *f, const RdCmd *c);
/* rd_replay_vu.c.
 * rd__replay_vu       a VU draw under its camera's picture
 * rd__vu_prepass      before list 0 (replayFrame, after the uploads and
 *                     the first clears): the vertex and triangle pre-passes
 *                     (docs/port/RENDER.md, "The vertex and triangle
 *                     pre-passes"), every mesh and grid command's vertices
 *                     and triangles and every particle command's sprites
 *                     into the record, copied into the frame slot's record
 *                     buffer
 * rd__vu_prepass_shutdown  the record's textures and buffers, the entries
 *                     (rd__gpu_shutdown)
 * rd__replay_vu_flush the draw merge's open run drawn (docs/port/
 *                     VU1_PROGRAMS.md, "The draw merge"): before anything
 *                     else is recorded into the replay (endPass,
 *                     rd__replay_flush_run, the walk before every command but
 *                     the VU draws and the state, the list's end)
 * rd__replay_vu_flush_post  the run drawn when the GPU timing's effect
 *                     changes from the one it was held under (the walk,
 *                     before perfPost) */
void rd__replay_vu(Replay *r, const RdFrame *f, const RdCmd *c);
void rd__replay_vu_flush(Replay *r);
void rd__replay_vu_flush_post(Replay *r, int post);
void rd__vu_prepass(const RdFrame *f, int keep);
void rd__vu_prepass_shutdown(void);

#endif /* PORT_RENDER_RD_REPLAY_H */
