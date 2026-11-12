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
#include "rd_present_scene.h" /* rd_replay.c and rd_blur_border.c call its hooks */
#include "rd_late_pass.h"     /* rd_crack_fill.c, rd_store_plan.c and rd_replay.c call it */
#include "rd_aura_paste.h"    /* rd_fx_plan.c calls it */
#include "rd_no_volume.h"     /* rd_replay.c, rd_store_plan.c and rd_shadow_merge.c call it */
#include "rd_hash.h"          /* rd_replay.c uniformOffset */

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
    uint32_t ord; /* g_rdStoreOrd of the command that opened the run (g_rdDrawOwner) */
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
    /* GS state as shader data (rd_uber.c rd__uber_date): the draw being
     * planned tests DATE in its shader against the alpha it reads, no
     * snapshot and no stencil bit taken (prepareDraw); PRIM.AA1's
     * per-primitive DATE draws are not needed then (doScreen) */
    int dateDstNow;
    /* the shadow tiles (rd_shadow_tile.c): the draw being planned is a
     * composite over the tile mask (its DATE tests become TEST0_T/TEST1_T,
     * expandPasses); reset by every prepareDraw */
    int tileNow;
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
    /* the shadow tiles (rd_shadow_tile.c): bindDrawEx draws the DATE set-up
     * over the tile mask (rd__shadow_tile_take) instead of
     * rd__replay_date_take */
    int shadowTile;
} DrawSetup;

/* rd_replay.c: beginPass, bindDrawKeep, bindGroup, s_bindEpoch, dstBind,
 * dstPassMode, expandPasses, fillDrawCB, prepareDraw, the MSAA epoch's
 * sample count this replay (s_ms.want), passPush (a pass begun outside
 * beginPass: its pass ring entry and its number in the pass window) */
void rd__replay_begin_pass(Replay *r, RdTargetRec *c, RdTargetRec *d, uint32_t cid, uint32_t did,
                           RhiLoadOp colorLoad, const float clear[4], RhiLoadOp depthLoad,
                           float clearDepth, bool storage);
RhiBindGroup rd__replay_bind_draw_keep(Replay *r, const DrawSetup *ds);
RhiBindGroup rd__replay_bind_group(RhiBindGroupLayout layout, const RhiBinding *b, uint32_t n);
uint32_t rd__replay_bind_epoch(void);
/* rd_occlusion.c: every GPU draw of the replay (rd_replay.c's drawCmd and
 * drawIndexedCmd): nothing outside the pass window's take, and the draw
 * inside an occlusion query while the measurement runs */
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
    RD_MSAA_CLOSE_READ,    /* SCENE's colour or depth read, copied or snapshot (the hook) */
    RD_MSAA_CLOSE_SHADOW,  /* a pass on SCENE's depth with another colour (the shadow count's) */
    RD_MSAA_CLOSE_DEPTH,   /* a pass on SCENE with another depth */
    RD_MSAA_CLOSE_COUNTED, /* a shadow reset: the count on SCENE_MS after it (rd_msaa_shadow.c) */
    RD_MSAA_CLOSE_END,     /* the replay's end */
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
/* rd_blur_texels.c: blurSnapArea's helpers (the texels a staticBlur sprite
 * reads).  rd__blur_uv_axis: one axis's texel range [*lo, *hi] of a sprite
 * x0..x1 with UVs u0..u1 (12.4) sampling a target of scale ss, TEX0 size
 * size, backing size asz and Full pixel crop crop16 (16ths); false (the
 * whole axis) under REPEAT, for an empty sprite or where the shader's
 * product could overflow.  rd__blur_kernel_reach: the above-1x kernel's
 * texels past the GS's taps for one axis's step. */
bool rd__blur_uv_axis(int32_t x0, int32_t x1, int32_t u0, int32_t u1, float ss, float size,
                      int clamp, uint32_t asz, int32_t crop16, int32_t *lo, int32_t *hi);
int32_t rd__blur_kernel_reach(uint32_t step32);
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

/* rd_crack_fill.c: the crack filler (rd_internal.h rd__crack_fill_*).
 * rd__crack_fill, from doFog (after the pass before the fog ended, but on
 * the local read): tc the fog's colour target, tz its depth's (the filler
 * draws when they are SCENE both), msaa the MSAA epoch at the fog
 * (RD_CRACK_MSAA_*: open, not drawn then; closed this replay, drawn on the
 * resolved picture; or neither): arms the fill for the same replay, drawn
 * from list 7 on (after lists 4-6) where rd__crack_fill_place puts it:
 *   rd__crack_fill_place  command c under state st (before c's own state):
 *                   HOLD the fill can wait past it (state; a staticBlur
 *                   sprite into another target that reads neither SCENE
 *                   nor writes its depth; the glow's sprites of SCENE's
 *                   alpha alone, *alpha set: the fill then writes RGB
 *                   alone), HERE the fill before it in a pass of its own,
 *                   PASTE the fill as the first draw of its pass (the
 *                   aura's paste into SCENE), TAKE as HERE, or its
 *                   snapshot here and its draw held (another target's
 *                   sprite after the glow's alpha: rd_late_pass.c, which
 *                   then says where it lands).  The store plan's scan and
 *                   the walk ask it the same, so the store the fill reads
 *                   is kept up to that command; ICO_RD_CRACK_PLACE=7 (the
 *                   tests' A/B) answers HERE always: the start of list 7
 *   rd__crack_fill_at     the walk, before command c of list (msaaOpen the
 *                   epoch open there): draws, holds or arms the paste
 *                   (more checks than the place's: the glow's first alpha
 *                   sprite over all of SCENE, the paste's pass readable and
 *                   whole; any failing draws the fill here)
 *   rd__crack_fill_late   the fill now if armed (the walk's end)
 *   rd__crack_fill_paste_due  armed for the paste being drawn now
 *   rd__crack_fill_in_pass   rd_fx_plan.c fxDraw's local read, before
 *                   bindDraw: the snapshot and the count, the paste's pass
 *                   begun (SCENE's colour alone; tdId the draw's depth, 0)
 *                   and the fill drawn in it
 *   rd__crack_fill_paste_late  the paste drawn another way: the fill's own
 *                   pass first (why for the log)
 * rd__crack_fill_may (the store plan's scan): a fog under state st may arm it */
enum {
    RD_CRACK_MSAA_NONE,     /* no MSAA, or the epoch not opened yet: single-sampled */
    RD_CRACK_MSAA_OPEN,     /* the epoch open at the fog (per-sample fog): not drawn */
    RD_CRACK_MSAA_RESOLVED, /* the epoch closed before the fog (msaaEnd's resolve) */
};

enum { RD_CRACK_PLACE_HOLD, RD_CRACK_PLACE_HERE, RD_CRACK_PLACE_PASTE, RD_CRACK_PLACE_TAKE };

void rd__crack_fill(Replay *r, RdTargetRec *tc, const RdTargetRec *tz, int msaa);
int rd__crack_fill_place(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool *alpha);
void rd__crack_fill_at(Replay *r, const RdFrame *f, const RdCmd *c, int list, bool msaaOpen);
void rd__crack_fill_late(Replay *r, bool msaaOpen);
bool rd__crack_fill_paste_due(void);
/* armed for the glow's paste being drawn now (the late pass's held fill:
 * rd_fx_plan.c takes it in the premultiplied add's pass) */
bool rd__crack_fill_glow_due(void);
void rd__crack_fill_in_pass(Replay *r, uint32_t tdId);
void rd__crack_fill_paste_late(Replay *r, const char *why);
bool rd__crack_fill_may(const RdStateBlock *st);
/* FILLSWAP and CRACKTAP (rd_crack_fill.c): the swap's start-up self-test
 * (rd_fog_path.c, with the other self-tests; true when it passed or had
 * nothing to check), its result (0 not run, 1 passed, 2 failed), and the
 * tests' switches (-1 the environment's: ICO_RD_CRACK_SWAP, ICO_RD_CRACK_TAP2) */
bool rd__crack_swap_selftest(void);
int rd__crack_swap_test(void);
bool rd__crack_swap_on(void);
const char *rd__crack_swap_why_text(uint32_t why);
bool rd__crack_tap2_on(void);
void rd__crack_swap_force(int on);
void rd__crack_tap2_force(int on);
/* the first list the armed fill may be drawn in (rd_store_plan.c's touch) */
#define RD_CRACK_LATE_LIST 7
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
 * rd__replay_end_pass  rd_replay.c endPass
 * rd__replay_end_pass_for  rd_replay.c endPassFor: the pass ended before
 *                    texture tex is read or copied outside a pass, a pending
 *                    clear of another target kept for that target's next
 *                    pass to take as its load op (the clear carry) */
bool rd__fx_fold(const RdDrawState *d, const uint8_t rgba[4], uint8_t *identity, uint8_t rgb[3]);
/* rd__fx_alpha_only  a sprite under d with record p writes RGB 0 whatever
 *                    its texture's RGB (MODULATE by RGBAQ's RGB 0, no
 *                    blend): its texture's alpha alone is read */
bool rd__fx_alpha_only(const RdDrawState *d, const RdPostRec *p);
uint32_t rd__a1x_sprite_steps(const Replay *r, const DrawSetup *ds, const IcoSpriteVertex *v,
                              uint32_t nv, uint8_t prim);
int rd__sprite_above1x(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds, uint32_t redId);
bool rd__fx_reduction_clears(Replay *r, const RdScreenVtx v[2], float col[4]);
void rd__replay_end_pass(Replay *r);
void rd__replay_end_pass_for(Replay *r, uint32_t tex);
/* rd_replay.c s_vpExact: the lists' exact viewport for the passes begun
 * from here (true), or the plain one (the upscale hook's); rd_layer.c's
 * pre-upscale reduction draws as the lists do inside rd__upscale_point */
void rd__replay_vp_exact(bool on);
/* box_reduce_ps's DrawCB for the count target tc, its scaled texels into
 * its GS size (shadowReduce; shadow_count_ps reads the same fields): the
 * source texels per output pixel, the source size, a Full pixel source's
 * left-out texels (rd_shadow_one.c) */
void rd__shadow_reduce_cb(const RdTargetRec *tc, IcoDrawCB *cb);
/* rd_shadow_one.c: the shadow count of the state block's targets tc
 * (colour) and td (depth-stencil) in one draw (shadow_count_ps) into dst at
 * tc's GS size, reduceCb rd__shadow_reduce_cb's, through the table of the
 * count's step (4 or 8); the open pass ended first.
 * False, nothing drawn, when the stencil view, the pipeline, the table or
 * the bind group is missing (the seven draws then). */
bool rd__shadow_one_draw(Replay *r, RdTargetRec *tc, RdTargetRec *td, uint32_t step, RhiTexture dst,
                         RhiState *dstState, const IcoDrawCB *reduceCb);
/* rd_shadow_one.c: resolve pass `pass` (0 .. RD_SHADOW_RESOLVE_PASSES - 1)
 * of rd_replay.c doShadowResolve's seven draws under the count's step: its
 * key, its DrawCB (the colour it adds, step << pass, or the A 0x80 it
 * writes) and its stencil reference; false where the step has no such pass
 * (step 8's bit 5: 8 << 5 is 256, nothing in eight bits) */
bool rd__shadow_resolve_pass(uint32_t step, int pass, RdPipeKeyInt *k, IcoDrawCB *cb, uint8_t *ref);
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
/* rd__const_actions  the actions walked so far this replay (this command's
 *                    counted); rd__const_took  the sprite being drawn now
 *                    (this action) into tcId was drawn in a cleared source's
 *                    colour (rd_aura_paste.c's chain) */
uint32_t rd__const_actions(void);
bool rd__const_took(uint32_t tcId);
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
/* rd_date_cut.c (docs/port/WORK_BUFFERS.md, "DATE's passes without the
 * depth"; the rule: rd_date_cut.h).
 * rd__date_cut_on        (rd.h) the cut in force
 * rd__date_cut_scan_begin, rd__date_cut_scan_cmd, rd__date_cut_scan_end
 *                        rd_store_plan.c rd__store_plan_scan's walk: every
 *                        command under the state after it (state: a state
 *                        command), its ordinal; the depth-free runs found;
 *                        rd__date_cut_end (rd__store_plan_end) the walk's end
 * rd__date_cut_depth     rd_replay.c prepareDrawEx, after the DATE snapshot:
 *                        the draw's depth (DrawSetup.tdId, depthFmt) from td
 *                        (its depth target, NULL none), none for a draw of a
 *                        depth-free run whose pass begins here or is open on
 *                        its target without a depth
 * rd__date_snapshot      rd_replay.c dateSnapshot: the R8 snapshot of tc
 *                        (taken, retaken or reused, counted by why); closing
 *                        and ended the MSAA epoch's state for rd__date_snap */
bool rd__date_cut_scan_begin(const RdFrame *f);
void rd__date_cut_scan_cmd(const RdCmd *c, const RdStateBlock *st, bool state, uint32_t ord);
void rd__date_cut_scan_end(uint32_t ord);
void rd__date_cut_end(void);
void rd__date_cut_depth(Replay *r, DrawSetup *ds, const RdTargetRec *td);
RhiTexture rd__date_snapshot(Replay *r, RdTargetRec *tc, uint32_t tcId, const RhiRect *area,
                             bool closing, bool ended);
bool rd__replay_date_take(Replay *r, const DrawSetup *ds);
bool rd__replay_date_clear(Replay *r, const DrawSetup *ds, const RhiRect *area);
bool rd__replay_blur_fold(Replay *r, const RdFrame *f, const RdCmd *c);
void rd__replay_fold_list(const RdCmd *cmds, uint32_t count, int list, uint32_t headHalf);
void rd__replay_fold_at(uint32_t index);
uint32_t rd__replay_fold_skip(void);
int rd__replay_glow_edge_columns(const Replay *r, const RdFrame *f, const RdCmd *c);
/* rd_fades_fold.c (docs/port/RENDER_POST.md, "The fades fold"): list 11's last
 * full-frame lerp sprites (the brightness, the fade, the letterbox bars)
 * applied by the reduction to the SCENE texels it reads instead of drawn
 * into SCENE.
 * RdFadesFold           a frame's plan: steps[i] R | G << 8 | B << 16 |
 *                       k << 24 (gs_math.hlsli gs_fold_ch), meta the count
 *                       | 8 for the nearest level (the fixed-function
 *                       blend) | 1 << (4 + i) for a bar step, alphas byte i
 *                       step i's alpha (As, FBA's MSB), bars the bars' texel
 *                       rows of SCENE (the top bar's end | the bottom bar's
 *                       first row << 16; 0 and 0xFFFF for none), sides[i]
 *                       0 every row, 1 the top bar, 2 the bottom bar, 3
 *                       both; ords the sprites' commands (the store plan's
 *                       ordinals, g_rdStoreOrd), reductionOrd the
 *                       reduction's textured sprite, scaled its hardware
 *                       sprite (above 1x); depthRead: a draw read SCENE's
 *                       depth before the frame cleared it (in list
 *                       depthReadList)
 * RdFadesFoldEnv        what the plan reads beside the frame
 * rd__fades_fold_plan   the plan of frame f: NULL (out->on) or why not
 *                       ("" when list 11 ends with no such sprite)
 * rd__fades_fold_scan   rd_store_plan.c rd__store_plan_scan: this replay's
 *                       plan, counted (RdPerfRecord.fadesFolded) and logged
 * rd__fades_fold_skip   rd_replay.c doScreen: command ord is not drawn
 * rd__fades_fold_fx     rd_replay.c doBlurSpriteDraw: the reduction's GS
 *                       sprite at scale 1 takes the plan (FXF_FOLD)
 * rd__fades_fold_screen rd_pixel_plan.c rd__fill_draw_cb_extra: its hardware
 *                       sprite above 1x (ICO_DF_FOLD)
 * rd__fades_fold_end    rd_store_plan.c rd__store_plan_end: a plan no
 *                       reduction took is logged and turns the fold off
 *                       (rd__fades_fold_on, rd_internal.h)
 * rd__fades_fold_exact  whether the sprites blend exactly (the GS's floor)
 *                       under exact mode mode (RdBlendExact) on a target of
 *                       dstUsage usage: RdFadesFoldEnv.exact
 * rd__fades_fold_present rd_present_scene.c: true when this replay's plan
 *                       is the reduction's textured sprite at ordinal ord
 *                       (its plan copied to out when not NULL); take: the
 *                       present from SCENE applies it instead (counted as
 *                       the reduction's taking it) */
#define RD_FADES_FOLD_STEPS 4
#define RD_FADES_FOLD_SPRITES 8

typedef struct RdFadesFold {
    bool on;
    uint32_t replay;
    uint32_t n, steps[RD_FADES_FOLD_STEPS], meta, alphas, bars;
    uint8_t sides[RD_FADES_FOLD_STEPS];
    uint32_t sprites, ords[RD_FADES_FOLD_SPRITES], reductionOrd;
    int scaled;
    int depthRead, depthReadList;
} RdFadesFold;

typedef struct RdFadesFoldEnv {
    bool effectsDepth, depthCarried, deferText, exact, msaa;
} RdFadesFoldEnv;

const char *rd__fades_fold_plan(const RdFrame *f, int keep, const RdFadesFoldEnv *env,
                                RdFadesFold *out);
void rd__fades_fold_scan(const RdFrame *f, int keep);
bool rd__fades_fold_skip(uint32_t ord);
void rd__fades_fold_fx(IcoDrawCB *cb, uint8_t kind, const RdTargetRec *src);
/* rd_aura_box.c (rd_internal.h has the rest), from rd_replay.c
 * doBlurSpriteDraw: its sprite drawn in two passes when it is the aura's
 * read of SCENE (src, its colour srcTex in SHADER_READ) into FEED128
 * (ds->tc) above 1x (steps) without a destination read or a self-sample
 * (other) and without a depth target (key, the one-pass key), with DrawCB
 * cb as fx_sprite_ps would have read it, before bindDraw.  True when drawn
 * (the pass before ended, ROWBOX's pass, FEED128's begun through
 * bindDraw and drawn: nothing left to do), false to draw it in one pass
 * (nothing recorded) */
bool rd__aura_box_draw(Replay *r, DrawSetup *ds, const RdPipeKeyInt *key, const IcoDrawCB *cb,
                       const RdTargetRec *src, RhiTexture srcTex, uint32_t steps, int other);
void rd__fades_fold_screen(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds,
                           IcoDrawCB *cb);
void rd__fades_fold_end(void);
bool rd__fades_fold_exact(int mode, uint32_t usage, bool rgba8);
bool rd__fades_fold_present(uint32_t ord, bool take, RdFadesFold *out);
/* The glow's mask folded into its reduction (rd_glow_fold.c; rd_internal.h
 * has the switch and the plan).
 * rd__glow_fold_scan_begin  rd_store_plan.c rd__store_plan_scan: true when
 *                      this replay's walk is to be read for the fold (the
 *                      switch on, no pass window); the counters zeroed
 * rd__glow_fold_scan_reset, rd__glow_fold_scan_cmd, rd__glow_fold_scan_end
 *                      the walk's every command (state: a state command),
 *                      then the verdict (logged once per reason, counted)
 * rd__glow_fold_skip   rd_blur_border.c blurPostSprite: the planned fill or
 *                      mask, not drawn (its state kept); true: skip it
 * rd__glow_fold_due    rd_fx_plan.c rd__fx_plan_draw: the planned reduction
 *                      x takes the fold: fx_glow_mask's words, SCENE's
 *                      record and, where WORK2 was drawn between the fill and
 *                      the mask (the sun's fans), WORK2's (*kept, else NULL:
 *                      the texels the mask keeps read from it); false otherwise (where it was planned but
 *                      cannot take it, the fill and the mask are drawn
 *                      first: rd__glow_fold_undo)
 * rd__glow_fold_drawn  the reduction drawn with the fold (counted, logged
 *                      once)
 * rd__glow_fold_undo   the fill and the mask drawn now with their states,
 *                      the fold off from now on (why: the log)
 * rd__glow_fold_end    rd_store_plan.c rd__store_plan_end: a plan no
 *                      reduction took is logged and turns the fold off */
bool rd__glow_fold_scan_begin(const RdFrame *f);
void rd__glow_fold_scan_reset(void);
void rd__glow_fold_scan_cmd(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool state,
                            uint32_t ord);
void rd__glow_fold_scan_end(void);
bool rd__glow_fold_skip(Replay *r, const RdFrame *f, const RdCmd *c);
bool rd__glow_fold_due(Replay *r, const RdFxDraw *x, uint32_t words[RD_GLOW_FOLD_WORDS],
                       RdTargetRec **scene, RdTargetRec **kept);
void rd__glow_fold_drawn(void);
void rd__glow_fold_undo(Replay *r, const char *why);
void rd__glow_fold_end(void);
/* The glow drawn only where it can add light (rd_glow_light.c).
 * rd__glow_light_on, rd__glow_light_force  (rd.h) the switch
 * rd__glow_light_scan_begin, _scan_cmd, _scan_end  rd_store_plan.c
 *                        rd__store_plan_scan's walk: every command (state:
 *                        a state command), then the plan (counted, logged)
 * rd__glow_light_planned this replay applies a plan (the WORK2 fold stands
 *                        down, rd_glow_fold.c)
 * rd__glow_light_sprite  rd_blur_border.c blurPostSprite: true when the
 *                        plan handled post sprite c (skipped, drawn as a
 *                        clear, drawn with its source planned)
 * rd__glow_light_skip    rd_replay.c doScreen: command ord is not drawn (the
 *                        fades fold's, rd__fades_fold_skip, or the sun's
 *                        fans off the picture under a plan)
 * rd__glow_light_src     rd_const_read.c rd__const_read: the colour the plan
 *                        gives source srcId for the sprite being drawn
 * rd__glow_light_lazy_begin, _readback  rd_pass_cut.c: the lazy clears of
 *                        WORK2_PAD, WORK2 and WORK3 drawn before a replay
 *                        that reads them first, or before a read outside one
 * rd__glow_light_reset   rd_pass_cut.c rd__pass_cut_init: a new renderer
 *                        holds no lazy clear
 * rd__blur_post_planned  rd_blur_border.c: sprite c and its glow edge strip
 *                        drawn (no fold, no border) */
bool rd__glow_light_scan_begin(const RdFrame *f);
void rd__glow_light_scan_cmd(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, bool state,
                             uint32_t ord);
void rd__glow_light_scan_end(void);
bool rd__glow_light_planned(void);
/* rd__glow_light_plan  this replay's plan (RD_GLOW_LIGHT_*; NONE before its
 *                      scan, at 1x and with the plan off); rd__glow_light_skips
 *                      the plan does not draw command c (walked now:
 *                      g_rdStoreOrd).  rd_late_pass.c holds the crack fill
 *                      past a skipped paste of a DARK frame */
int rd__glow_light_plan(void);
bool rd__glow_light_skips(const RdCmd *c);
bool rd__glow_light_sprite(Replay *r, const RdFrame *f, const RdCmd *c);
bool rd__glow_light_skip(uint32_t ord);
bool rd__glow_light_src(uint32_t srcId, uint8_t rgba[4]);
void rd__glow_light_lazy_begin(void);
void rd__glow_light_readback(uint32_t id);
void rd__glow_light_reset(void);
void rd__blur_post_planned(Replay *r, const RdFrame *f, const RdCmd *c);
/* rd_blur_border.c blurPostSprite, before SCENE's border repeat: the pass
 * serial there, so the passes the repeat begins are not counted as the
 * glow chain's (RdPerfRecord.glowLightPasses) */
void rd__glow_light_mark(const Replay *r);
/* rd_pass_area.c rd__pass_area_begin (every render pass rd_replay.c
 * beginPass begins): a pass begun in the light plan's chain, named in
 * rd__glow_light_debug (its target, size, colour load op, the command after) */
void rd__glow_light_pass(uint32_t cid, const RdTargetRec *t, int load);
/* rd_const_read.c: fx_tfx of texel t under RGBAQ col and the sprite's
 * flags (TFX in bits 1..2, TCC), the shader's integers */
void rd__const_tfx(const uint8_t t[4], const uint8_t col[4], uint32_t flags, uint8_t out[4]);
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

/* rd_vis.c (rd_vis.h): the port's visibility test per scene draw.
 * rd__vis_frame       the frame replayFrame walks: f, or its shadow with
 *                     the scene draws wholly off their target or behind
 *                     the eye turned into RDC_NOPs (after rd__diag_frame,
 *                     before every scan of the lists)
 * rd__vis_source      the frame a shadow was made from (what decides
 *                     across draws or replays: rd_join.c, rd_fades_fold.c)
 * rd__vis_culled      a shadow's NOP at (l, i) as recorded, or NULL
 *                     (rd_store_plan.c's carried depth read) */
const RdFrame *rd__vis_frame(const RdFrame *f, int keep);
const RdFrame *rd__vis_source(const RdFrame *f);
const RdCmd *rd__vis_culled(const RdFrame *f, int l, uint32_t i);

/* rd_msaa_shadow.c: the shadow count drawn in the MSAA epoch, on SCENE_MS's
 * own depth and stencil (docs/port/MSAA.md "The shadow count in the epoch").
 * rd__msaa_shadow_reset  doShadowReset: the reset taken in the epoch (true:
 *                        the epoch closed, then the count's pass begun on
 *                        SCENE_MS's colour and depth, its colour and stencil
 *                        cleared; false: the count after the epoch's
 *                        resolve, as before, with the line saying why)
 * rd__msaa_shadow_pass   the strips and the resolve: the count's pass open
 *                        (begun again when a command ended it); false
 *                        outside the count
 * rd__msaa_shadow_done   doShadowResolve after the seven draws: the count
 *                        resolved into its target
 * rd__msaa_shadow_selftest  the count in the epoch against the count without
 *                        anti-aliasing, the volumes' depth test included, on
 *                        SCENE at samples (rd_fog_path.c rd__fog_aa_reconcile)
 * rd__msaa_shadow_force  the tests' switch: -1 the self-test's choice, 0 the
 *                        count after the resolve, 1 in the epoch;
 *                        rd__msaa_shadow_mode the way in force
 * rd__msaa_shadow_counts the counts drawn in the epoch in the last replay
 * rd__msaa_shadow_retest the self-test runs again at its next call
 * rd__msaa_shadow_free   SCENE_MS goes (rd_replay.c msaaFree): no count open
 * and from rd_replay.c: rd__msaa_pair (SCENE_MS's colour and depth, their
 * states and count; true while the epoch is open with its depth drawn) and
 * rd__msaa_close (msaaEnd for RD_MSAA_CLOSE_COUNTED) */
/* rd_replay.c shadowTargets: the shadow commands' colour target tc and depth
 * target td of one texel size; a temporary of the scene's GS size whose
 * texels are another scale's (made before a scale change, its frame
 * replayed after it) is made again at the scale in force (rd_msaa_shadow.c) */
bool rd__shadow_size_agree(RdTargetRec *tc, const RdTargetRec *td);
bool rd__msaa_shadow_reset(Replay *r, RdTargetRec *tc, RdTargetRec *td);
bool rd__msaa_shadow_pass(Replay *r);
void rd__msaa_shadow_done(Replay *r, RdTargetRec *tc);
bool rd__msaa_shadow_selftest(uint32_t samples);
void rd__msaa_shadow_force(int mode);
int rd__msaa_shadow_mode(void);
uint32_t rd__msaa_shadow_counts(void);
void rd__msaa_shadow_retest(void);
void rd__msaa_shadow_free(void);
bool rd__msaa_pair(RhiTexture t[2], RhiState *st[2], uint32_t *samples);
void rd__msaa_close(Replay *r);

#endif /* PORT_RENDER_RD_REPLAY_H */
