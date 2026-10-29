/* rd_shadow.c: the shadow count of Shadow.c on the stencil.
 *
 * What the PS2 does
 * -----------------
 * shadow_Reset clears FBP 0x142 (a scene-sized PSMCT32 buffer) to 0 and
 * leaves ZBUF 0xC0 with ZMSK, TEST 0x50000 (Z GEQUAL), ALPHA 0x68 with FIX
 * 0x80 (Cs * 0x80 >> 7 + Cd = Cs + Cd) and COLCLAMP 0, so the sum keeps its
 * low 8 bits.  shadow_RenderVolume sends each volume as flat triangle strips
 * (PRIM 0x144) whose positions carry the RGBAQ 0x04 or 0xFC by the facing of
 * the triangle they end; with flat shading a triangle adds its last vertex's
 * colour.  So a pixel ends at 4 n mod 256 on every channel, n = (faces of
 * colour 0x04) - (faces of colour 0xFC) that pass the Z test against the
 * scene.  shadow_Draw reads the buffer back as PSMCT24 with TEXA 0x80 AEM
 * (alpha 0 where RGB is 0, else 0x80), then blurs and composites it.
 *
 * What rd does
 * ------------
 * The volume triangles change the stencil of the bound depth target (SCENE)
 * instead: INCR_WRAP for 0x04, DECR_WRAP for 0xFC, both faces, where the
 * depth test passes, no colour or depth write, the write mask 0x3F.  With
 * that mask the stencil holds n mod 64 exactly (an increment from 63 writes
 * the low six bits of 64, 0; a decrement from 0 writes those of 255, 63),
 * and 4 n mod 256 = 4 (n mod 64), so the resolve writes, per pixel, the
 * colour the GS sum would have left: six passes with the stencil test EQUAL
 * on bit k add 4 << k to RGB (ONE + ONE, never above 252), a seventh writes
 * A = 0x80 where the count is not 0.  The order of the faces does not matter
 * on either side (addition mod 256 commutes; the Z test does not write), so
 * the triangles are drawn as two lists, increments then decrements.
 *
 * Without the write mask (an 8-bit stencil wrapping at 256) the two would
 * differ exactly when n is a non-zero multiple of 64 below 256 in magnitude
 * (n = 64, 128, 192, ...): the GS colour is 0 there (no shadow), the
 * stencil not.  rd_shadow_test checks n = 64, 128 and 256.
 *
 * The step
 * --------
 * The North American disc and the 2001 previews draw the faces with 0x08
 * and 0xF8 (Shadow.c emitVolumeStrip, PAL_NTSCAUG6JUN26), so a pixel ends
 * at 8 n mod 256 = 8 (n mod 32).  The volumes are the same draws (the
 * stencil's n mod 64 holds n mod 32 in its low five bits, 32 dividing 64);
 * the resolve reads the five: passes k = 0..4 add 8 << k (8 + ... + 128 =
 * 248 at most), bit 5 has no pass (8 << 5 = 256 is 0 in eight bits), and
 * the A pass tests the five bits (RD_STENCIL_TEST_NONZERO5), so n = 32 is
 * no shadow, as on the GS.  Shadow.c passes its step at each reset
 * (rd_shadow_reset_step); the reset and the resolve commands carry it in
 * b[0] (8, or 0 for 4: the European disc's commands and every older dump
 * keep their bytes), so a dump replays with its disc's step anywhere.
 * The one draw reads a table of its step (rd_shadow_one.c).  Before
 * the shadow step change the host hook took every 0x08 face for a decrement (its test was
 * the European 0x04): those discs' older dumps hold every face as a
 * decrement (RDC_SHADOW_STRIP u[0] 0), which no replay can undo.
 *
 * The A the resolve writes is the TEXA expansion of the PSMCT24 read, not
 * the alpha the GS stored (As = 0x80 wherever a face was drawn); nothing
 * reads FBP 0x142 as 32 bits, and baking the expansion lets the chain's
 * bilinear filter work on expanded texels as the GS filter does (chosen
 * when rd's shader still expanded TEXA after the sampler had filtered).
 *
 * Every vertex carries the place its triangle had in the call
 * (rd__set_shadow_tag, in rgba: the volume draw writes no colour), so
 * rd_interp.c can regroup Shadow.c's prisms after the split.
 *
 * Replay (rd_replay.c, doShadow*) draws on the state block's colour and
 * depth targets at the command, so the commands leak and inherit state like
 * any other.  Recording is here; rd_shadow_strip (rd_core.c) shares
 * RDC_SHADOW_STRIP with b[0] = 0.
 */
#include <string.h>
#include "rd_internal.h"
#include "../core/region.h" /* the running disc's step (rd__shadow_disc_step) */

RdTarget rd_shadow_count_target(uint32_t gsW, uint32_t gsH)
{
    static uint32_t s_id;
    RdFrame *f = rd__rec_frame();
    if (!f || gsW == 0 || gsH == 0) {
        return (RdTarget){0};
    }
    /* this frame's, if it made one: a temporary target dies with its frame */
    for (uint32_t i = 0; i < f->tempCount; i++) {
        const RdTargetRec *t = rd__target_rec(f->tempTargets[i]);
        if (f->tempTargets[i] == s_id && t && t->w == gsW && t->h == gsH && !t->withDepth) {
            return (RdTarget){s_id};
        }
    }
    s_id = rd_temp_target(gsW, gsH, 0, 0).id;
    return (RdTarget){s_id};
}

/* the step of the last reset recorded (the game thread's), for the
 * resolve after it */
static uint32_t s_recStep = 4;

uint32_t rd__shadow_step(const RdCmd *c)
{
    return c && c->b[0] == 8 ? 8u : 4u;
}

uint32_t rd__shadow_disc_step(void)
{
    return PAL_NTSCAUG6JUN26(4u, 8u); /* Shadow.c's 0x04 / 0x08 face colours */
}

void rd_shadow_reset_step(uint32_t step)
{
    static uint32_t s_logged;
    step = step == 8 ? 8u : 4u;
    if (step != s_logged) {
        /* once a run, and again if the disc's step changes */
        rd__log("shadow count: step %u (the disc's face colours 0x%02X / 0x%02X), %s bits", step,
                step, 256u - step, step == 8 ? "five" : "six");
        s_logged = step;
    }
    s_recStep = step;
    RdCmd *c = rd__push(RDC_SHADOW_RESET);
    if (c) {
        c->b[0] = (uint8_t)(step == 8 ? 8 : 0);
    }
}

void rd_shadow_reset(void)
{
    rd_shadow_reset_step(4);
}

void rd_shadow_resolve(void)
{
    RdCmd *c = rd__push(RDC_SHADOW_RESOLVE);
    if (c) {
        c->b[0] = (uint8_t)(s_recStep == 8 ? 8 : 0);
    }
}

void rd_shadow_tris(const RdScreenVtx *v, const int8_t *sign, uint32_t triCount, RdKey key)
{
    RdFrame *f = rd__rec_frame();
    if (!v || !sign || triCount == 0 || !rd__draw_filter_pass(key)) {
        return;
    }
    if (!f) {
        rd__push(RDC_SHADOW_STRIP); /* reports once */
        return;
    }
    uint32_t inc = 0;
    for (uint32_t t = 0; t < triCount; t++) {
        inc += sign[t] > 0;
    }
    const uint32_t n = triCount * 3;
    const uint32_t off = rd__frame_payload(f, NULL, n * (uint32_t)sizeof(RdScreenVtx));
    RdScreenVtx *out = (RdScreenVtx *)(f->payload + off);
    uint32_t a = 0, b = inc * 3;
    for (uint32_t t = 0; t < triCount; t++) {
        uint32_t *at = sign[t] > 0 ? &a : &b;
        memcpy(&out[*at], &v[t * 3], 3 * sizeof(RdScreenVtx));
        for (uint32_t k = 0; k < 3; k++) {
            rd__set_shadow_tag(&out[*at + k], t + 1); /* the call's order */
        }
        *at += 3;
    }
    RdCmd *c = rd__push(RDC_SHADOW_STRIP);
    if (!c) {
        return;
    }
    c->b[0] = RD_SHADOW_TRIS;
    c->b[1] = rd__frame_projected() ? RD_SHADOW_FRAME_CAMERA : 0;
    c->u[0] = inc * 3;
    c->u[1] = off;
    c->u[2] = n * (uint32_t)sizeof(RdScreenVtx);
    c->u[3] = (triCount - inc) * 3;
    c->keyLo = (uint32_t)key;
    c->keyHi = (uint32_t)(key >> 32);
    g_rd.stats.draws++;
}

/* ------------------------------------------- the stencil window's ops
 * The load and store of a pass's stencil against the shadow window
 * (rd_internal.h RdStencilWindow), which the replay's passes ask
 * (rd_replay.c beginPass, rd_upload.c) */
bool rd__stencil_live(const RdStencilWindow *w, uint32_t depth)
{
    if (depth == 0) {
        return false;
    }
    if (w->spill) {
        return true;
    }
    for (int i = 0; i < RD_STENCIL_LIVE_MAX; i++) {
        if (w->live[i] == depth) {
            return true;
        }
    }
    return false;
}

void rd__stencil_open(RdStencilWindow *w, uint32_t depth)
{
    if (depth == 0 || rd__stencil_live(w, depth)) {
        return;
    }
    for (int i = 0; i < RD_STENCIL_LIVE_MAX; i++) {
        if (w->live[i] == 0) {
            w->live[i] = depth;
            return;
        }
    }
    w->spill = 1;
}

void rd__stencil_close(RdStencilWindow *w, uint32_t depth)
{
    for (int i = 0; i < RD_STENCIL_LIVE_MAX && depth != 0; i++) {
        if (w->live[i] == depth) {
            w->live[i] = 0;
        }
    }
}

bool rd__stencil_ops(RdStencilWindow *w, uint32_t depth, bool whole, RhiLoadOp depthLoad,
                     RhiLoadOp *stencilLoad, RhiStoreOp *stencilStore)
{
    const bool live = rd__stencil_live(w, depth);
    *stencilStore = live ? RHI_STORE_STORE : RHI_STORE_DONT_CARE;
    if (depth != 0 && depth == w->clearFor && whole) {
        w->clearFor = 0;
        *stencilLoad = RHI_LOAD_CLEAR;
        return true;
    }
    *stencilLoad = live || depthLoad == RHI_LOAD_CLEAR ? depthLoad : RHI_LOAD_DONT_CARE;
    return false;
}

/* rd__stencil_ops, and with g_rd.depthCopy the stencil loaded with the
 * depth and stored outside the window too: no pass on a
 * packed depth-stencil then asks for its depth and its stencil
 * differently */
bool rd__stencil_ops_packed(RdStencilWindow *w, uint32_t depth, bool whole, RhiLoadOp depthLoad,
                            RhiLoadOp *stencilLoad, RhiStoreOp *stencilStore)
{
    const bool took = rd__stencil_ops(w, depth, whole, depthLoad, stencilLoad, stencilStore);
    if (g_rd.depthCopy) {
        *stencilStore = RHI_STORE_STORE;
        if (*stencilLoad == RHI_LOAD_DONT_CARE) {
            *stencilLoad = depthLoad;
        }
    }
    return took;
}
