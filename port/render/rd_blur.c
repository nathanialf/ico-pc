/* rd_blur.c: staticBlur.c's sprites (renderer wave 5, R5a).
 *
 * staticBlur.c's host path (ICO_RD) records every register write of its
 * packets as rd state, in packet order, and every gif_SpriteSensitiveOrg
 * as rd_Post of the kind of the effect it belongs to
 * (RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR); since R-POST rd_post.c's
 * reduction records its two sprites here too (RD_POST_REDUCTION), so the
 * motion blur loop through DISPLAY is in this model end to end.  This file
 * records that sprite: an RdPostRec in an RDC_POST_STUB, with the corners, UVs, RGBAQ
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
 * every pass, feedback or not.  That is the exactness of blend_int's RGBA8_UINT ping-pong, without the UINT copies:
 * UNORM8 holds k / 255 exactly and a Load of it gives k back.
 */
#include <string.h>
#include "rd_internal.h"

void rd__PostBlur(RdPostKind kind, const RdPostParams *p)
{
    rd__PostBlurVerts(kind, p, NULL);
}

void rd__PostBlurVerts(RdPostKind kind, const RdPostParams *p, const RdScreenVtx v[4])
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
    /* R-POST: the sprite as screen prims too (the reduction's, unmirrored
     * then mirrored), for a replay at a scale (rd__BlurScreenFallback) */
    r.lutOffset = v ? rd__FramePayload(f, v, 4 * (uint32_t)sizeof(RdScreenVtx)) : ~0u;
    const uint32_t off = rd__FramePayload(f, &r, sizeof(r));
    RdCmd *c = rd__Push(RDC_POST_STUB);
    if (c) {
        c->b[0] = (uint8_t)kind;
        c->u[1] = off;
        c->u[2] = (uint32_t)sizeof(r);
    }
}

/* The reduction (RD_POST_REDUCTION, rd_post.c) samples SCENE at
 * u = x + 0.75 for DISPLAY pixel x: corners at -0.25 px, U 8 (0.5 texel) at
 * the corner, one texel per pixel, so U(16x) = 16x + 12 and the bilinear
 * blends texels x and x + 1 at 12/16 and 4/16.  That bias is the PS2's and
 * stays as it is with the mirror off.  With the mirror on, the UI in SCENE
 * is drawn flipped and the present flips DISPLAY back, so the sample points
 * must be the mirror image of the unmirrored ones for the mirrored frame to
 * be the exact mirror of the unmirrored one: pixel W - 1 - x samples
 * W - (x + 0.75) = (W - 1 - x) + 0.25, i.e. U(16x) = 16x + 4, texels x - 1
 * and x at 4/16 and 12/16.  That is U moved back by 8 (half a texel); the
 * scissor (2 .. W - 3) and the black sprite (pixels 0 .. W - 1) are
 * symmetric already, and V (v = 2y + 1) does not change.  On a scaled
 * target the hardware sprite (rd__BlurScreenFallback) has its U moved the
 * same half texel (rd_post.c records that pair too), so it samples at
 * x + s / 4 and, mirrored, x - s / 4 in the target's texels, again the
 * mirror image. */
void rd__BlurUvRect(uint32_t kind, const RdPostRec *p, float uv[4])
{
    memcpy(uv, p->uv, sizeof(p->uv));
    if (kind == RD_POST_REDUCTION && rd__MirrorOn()) {
        uv[0] -= 8.0f;
        uv[2] -= 8.0f;
    }
}

/* The reduction on a scaled target (Enhanced above 1x, or 1x at a wider
 * aspect): fx_sprite_ps steps the GS position in 1/16 GS pixel and weighs in
 * 1/16 texel, which at a scale that is not an integer puts neighbouring
 * texels' samples up to a tenth of a texel off their place (edges of up to
 * 33 LSB jitter at 16:9 1080p).  There the reduction keeps the
 * hardware-filtered sprite it was before R-POST, continuous in position and
 * weight; Original, and Enhanced wherever both targets are at scale 1, take
 * the exact model.  Returns the payload offset of the two vertices to draw
 * (the mirrored pair with the mirror on), or ~0u for the model. */
uint32_t rd__BlurScreenFallback(uint32_t kind, const RdPostRec *p, const RdStateBlock *st)
{
    if (kind != RD_POST_REDUCTION || p->lutOffset == ~0u) {
        return ~0u;
    }
    const RdTargetRec *dst = rd__TargetRec(st->color);
    const RdTexRec *t = st->ds.texEnabled ? rd__TexRec(st->tex) : NULL;
    const RdTargetRec *src = t && t->kind == RD_TEXKIND_TARGET ? rd__TargetRec(t->target) : NULL;
    const int scaled = (dst && (dst->sx != 1.0f || dst->sy != 1.0f)) ||
                       (src && (src->sx != 1.0f || src->sy != 1.0f));
    if (!scaled) {
        return ~0u;
    }
    return p->lutOffset + (rd__MirrorOn() ? 2u * (uint32_t)sizeof(RdScreenVtx) : 0u);
}
