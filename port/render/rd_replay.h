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
     * both axes (doBlurSpriteDraw: blurAbove1xSteps 0), so spriteAbove1x
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
    /* the axes (bit 0 S, bit 1 T) a screen prim samples wholly past the
     * first period of a REPEAT (rd__screen_repeat_kept): Full pixel keeps
     * the game's wrap there (drawWrap) */
    uint32_t repeatKept;
    /* DATE through the stencil bit (prepareDraw): bindDrawEx writes the
     * bit over dateWant (the texels the draw can touch, the whole target
     * when dateWantAll) in the draw's pass before it (rd__replay_date_take);
     * no R8 snapshot (dateTex the dummy) */
    int dateStencil, dateWantAll;
    RhiRect dateWant;
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
void rd__fill_draw_cb_extra(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds,
                            IcoDrawCB *cb);
/* rd_replay_tiler.c (docs/port/RENDER.md, "The replay").
 * rd__replay_date_take  DATE's stencil bit written in the draw's pass
 *                       (bindDrawEx, a draw with DrawSetup.dateStencil);
 *                       false: the draw is skipped
 * rd__replay_blur_fold  a staticBlur pair at its pass 1 c drawn as one pass
 *                       (doBlurSprite); true: drawn, and the loop skips
 *                       rd__replay_fold_skip() commands with their state
 *                       applied
 * rd__replay_fold_list  the list the loop walks (NULL at its end), the
 *                       list number and the frame head's half pixel;
 *                       rd__replay_fold_at the command being replayed */
bool rd__replay_date_take(Replay *r, const DrawSetup *ds);
bool rd__replay_blur_fold(Replay *r, const RdFrame *f, const RdCmd *c);
void rd__replay_fold_list(const RdCmd *cmds, uint32_t count, int list, uint32_t headHalf);
void rd__replay_fold_at(uint32_t index);
uint32_t rd__replay_fold_skip(void);
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
 *                     (rd__gpu_shutdown) */
void rd__replay_vu(Replay *r, const RdFrame *f, const RdCmd *c);
void rd__vu_prepass(const RdFrame *f, int keep);
void rd__vu_prepass_shutdown(void);

#endif /* PORT_RENDER_RD_REPLAY_H */
