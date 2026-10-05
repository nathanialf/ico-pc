/* rd_blur.c: staticBlur.c's sprites (renderer wave 5, R5a;
 * docs/port/RENDER_API.md section 17).
 *
 * staticBlur.c's host path (ICO_RD) records every register write of its
 * packets as rd state, in packet order, and every gif_SpriteSensitiveOrg
 * as rd_Post of the kind of the effect it belongs to
 * (RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR).  This file records that
 * sprite: an RdPostRec in an RDC_POST_STUB, with the corners, UVs, RGBAQ
 * and Z as the GS gets them, the TEX0 size and TFX.  rd_replay.c
 * (doBlurSprite) draws it through fx_rect_vs / fx_sprite_ps with the state
 * block in force:
 *
 *   coverage  the pixels whose window coordinate X (12.4) has x0 <= X < x1
 *             (and the same in y), in the state's scissor
 *   UV        U(X) = u0 + (X - x0) (u1 - u0) / (x1 - x0), integers, 12.4
 *   texel     TEX1.MMAG (the sprites' LOD is TEX1.K = 0, so MMAG applies):
 *             nearest = texel (U >> 4, V >> 4); linear = the four texels
 *             around (U - 8, V - 8) weighted by the 4-bit fractions,
 *             sum >> 8; each texel wrapped by CLAMP on the TEX0 size, then
 *             clamped to the backing target, then expanded by TEXA (RGB24
 *             and RGBA16 sources) before filtering
 *   TFX       MODULATE, DECAL, HIGHLIGHT (RGB + Af, A = At + Af with TCC),
 *             HIGHLIGHT2 (RGB + Af, A = At), clamped at 255
 *   tests     alpha test (AFAIL KEEP discards, RGB_ONLY keeps the
 *             destination alpha, FB_ONLY writes colour; ZB_ONLY discards
 *             the colour), DATE against the destination before the
 *             sprite, the Z test in hardware on the bound depth target
 *   blend     ((A - B) C >> 7) + D on integers (gs_blend_int), PABE,
 *             COLCLAMP, FBA; written to the UNORM8 target as k / 255,
 *             which stores k exactly
 *
 * The destination is read from a copy taken just before the sprite (the
 * target's snapshot), so overlapping sprites of one record cannot happen
 * (one sprite per record) and the result is the GS integer arithmetic of
 * every pass, feedback or not.  That is the exactness of RENDER_API.md
 * section 7 (blend_int's RGBA8_UINT ping-pong) without the UINT copies:
 * UNORM8 holds k / 255 exactly and a Load of it gives k back.
 */
#include <math.h>
#include <string.h>
#include "rd_internal.h"

void rd__PostBlur(RdPostKind kind, const RdPostParams *p)
{
    RdFrame *f = rd__RecFrame();
    if (!f) {
        rd__Push(RDC_POST_STUB);
        return;
    }
    RdPostRec r;
    memset(&r, 0, sizeof(r));
    r.src = p->src.id;
    r.dst = p->dst.id;
    r.srcView = (uint32_t)p->srcView;
    memcpy(r.rgba, p->rgba, 4);
    r.fix = p->fix;
    r.blend = p->blend;
    r.abe = p->abe;
    r.exactInt = 1;
    r.z = p->z;
    memcpy(r.rect, p->rect, sizeof(r.rect));
    memcpy(r.uv, p->uv, sizeof(r.uv));
    memcpy(r.scalar, p->scalar, sizeof(r.scalar));
    if (r.scalar[0] < 1.0f) {
        r.scalar[0] = 1024.0f;
    }
    if (r.scalar[1] < 1.0f) {
        r.scalar[1] = 1024.0f;
    }
    if (r.scalar[2] <= 0.0f) {
        r.scalar[2] = 1.0f;
    }
    r.lines = p->lines & 3;
    r.lutOffset = ~0u;
    const uint32_t off = rd__FramePayload(f, &r, sizeof(r));
    RdCmd *c = rd__Push(RDC_POST_STUB);
    if (c) {
        c->b[0] = (uint8_t)kind;
        c->u[1] = off;
        c->u[2] = (uint32_t)sizeof(r);
    }
}

uint8_t rd__BlurFeedbackFix(uint8_t blend, uint8_t fix, float dt)
{
    if (dt == 1.0f || dt <= 0.0f) {
        return fix;
    }
    float f;
    if (blend == RD_BLEND_LERP_FIX) {
        /* the destination keeps (128 - FIX) / 128 per frame */
        const float keep = powf((128.0f - (float)fix) / 128.0f, dt);
        f = 128.0f - 128.0f * keep;
    } else {
        f = (float)fix * dt;
    }
    f = f < 0.0f ? 0.0f : (f > 255.0f ? 255.0f : f);
    return (uint8_t)(f + 0.5f);
}
