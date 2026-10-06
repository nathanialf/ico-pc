/* rd_shadow.c: the shadow count of Shadow.c on the stencil (renderer wave 4,
 * package R4b; docs/port/RENDER_API.md "Shadows").
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
 * The A the resolve writes is the TEXA expansion of the PSMCT24 read, not
 * the alpha the GS stored (As = 0x80 wherever a face was drawn); nothing
 * reads FBP 0x142 as 32 bits, and baking the expansion lets the chain's
 * bilinear filter work on expanded texels as the GS filter does (chosen
 * when rd's shader still expanded TEXA after the sampler had filtered;
 * RENDER_API.md "Textures").
 *
 * Package V3: every vertex carries the place its triangle had in the call
 * (rd__SetShadowTag, in rgba: the volume draw writes no colour), so
 * rd_interp.c can regroup Shadow.c's prisms after the split.
 *
 * Replay (rd_replay.c, doShadow*) draws on the state block's colour and
 * depth targets at the command, so the commands leak and inherit state like
 * any other.  Recording is here; rd_ShadowStrip (rd_core.c) shares
 * RDC_SHADOW_STRIP with b[0] = 0.
 */
#include <string.h>
#include "rd_internal.h"

RdTarget rd_ShadowCountTarget(uint32_t gsW, uint32_t gsH)
{
    static uint32_t s_id;
    RdFrame *f = rd__RecFrame();
    if (!f || gsW == 0 || gsH == 0) {
        return (RdTarget){0};
    }
    /* this frame's, if it made one: a temporary target dies with its frame */
    for (uint32_t i = 0; i < f->tempCount; i++) {
        const RdTargetRec *t = rd__TargetRec(f->tempTargets[i]);
        if (f->tempTargets[i] == s_id && t && t->w == gsW && t->h == gsH && !t->withDepth) {
            return (RdTarget){s_id};
        }
    }
    s_id = rd_TempTarget(gsW, gsH, 0, 0).id;
    return (RdTarget){s_id};
}

void rd_ShadowReset(void)
{
    rd__Push(RDC_SHADOW_RESET);
}

void rd_ShadowResolve(void)
{
    rd__Push(RDC_SHADOW_RESOLVE);
}

void rd_ShadowTris(const RdScreenVtx *v, const int8_t *sign, uint32_t triCount, RdKey key)
{
    RdFrame *f = rd__RecFrame();
    if (!v || !sign || triCount == 0 || !rd__DrawFilterPass(key)) {
        return;
    }
    if (!f) {
        rd__Push(RDC_SHADOW_STRIP); /* reports once */
        return;
    }
    uint32_t inc = 0;
    for (uint32_t t = 0; t < triCount; t++) {
        inc += sign[t] > 0;
    }
    const uint32_t n = triCount * 3;
    const uint32_t off = rd__FramePayload(f, NULL, n * (uint32_t)sizeof(RdScreenVtx));
    RdScreenVtx *out = (RdScreenVtx *)(f->payload + off);
    uint32_t a = 0, b = inc * 3;
    for (uint32_t t = 0; t < triCount; t++) {
        uint32_t *at = sign[t] > 0 ? &a : &b;
        memcpy(&out[*at], &v[t * 3], 3 * sizeof(RdScreenVtx));
        for (uint32_t k = 0; k < 3; k++) {
            rd__SetShadowTag(&out[*at + k], t + 1); /* V3: the call's order */
        }
        *at += 3;
    }
    RdCmd *c = rd__Push(RDC_SHADOW_STRIP);
    if (!c) {
        return;
    }
    c->b[0] = RD_SHADOW_TRIS;
    c->u[0] = inc * 3;
    c->u[1] = off;
    c->u[2] = n * (uint32_t)sizeof(RdScreenVtx);
    c->u[3] = (triCount - inc) * 3;
    c->keyLo = (uint32_t)key;
    c->keyHi = (uint32_t)(key >> 32);
    g_rd.stats.draws++;
}
