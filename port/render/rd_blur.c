/* rd_blur.c: staticBlur.c's sprites.
 *
 * staticBlur.c's host path (ICO_RD) records every register write of its
 * packets as rd state, in packet order, and every gif_SpriteSensitiveOrg
 * as rd_post of the kind of the effect it belongs to
 * (RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR); rd_post.c's reduction
 * records its two sprites here too (RD_POST_REDUCTION), so the motion blur
 * loop through DISPLAY is in this model end to end.  This file records
 * that sprite: an RdPostRec in an RDC_POST_STUB, with the corners, UVs,
 * RGBAQ and Z as the GS gets them, the TEX0 size and TFX.  rd_replay.c
 * (rd_blur_border.c rd__blur_post_sprite) draws it through fx_rect_vs / fx_sprite_ps with the state
 * block in force:
 *
 *   coverage  the pixels whose window coordinate X (12.4) has x0 <= X < x1
 *             (and the same in y), in the state's scissor
 *   UV        U(X) = u0 + (X - x0) (u1 - u0) / (x1 - x0), integers, 12.4
 *   texel     TEX1.MMAG (the sprites' LOD is TEX1.K = 0, so MMAG applies):
 *             nearest = texel (U >> 4, V >> 4); linear = the four texels
 *             around (U - 8, V - 8) blended by the 4-bit fractions across
 *             then down, each step truncated (common.hlsli gs_bilerp, as
 *             PCSX2's per-draw dumps have it); each texel wrapped by CLAMP
 *             on the TEX0 size, then
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
 * every pass, feedback or not.  That is the exactness of blend_int's
 * RGBA8_UINT ping-pong, without the UINT copies: UNORM8 holds k / 255
 * exactly and a Load of it gives k back.
 */
#include <math.h>
#include <string.h>
#include "rd_internal.h"

void rd__post_blur(RdPostKind kind, const RdPostParams *p)
{
    rd__post_blur_verts(kind, p, NULL);
}

void rd__post_blur_verts(RdPostKind kind, const RdPostParams *p, const RdScreenVtx v[4])
{
    RdFrame *f = rd__rec_frame();
    if (!f) {
        rd__push(RDC_POST_STUB);
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
    /* the sprite as screen prims too (the reduction's, unmirrored
     * then mirrored), for a replay at a scale (rd__blur_screen_fallback) */
    r.lutOffset = v ? rd__frame_payload(f, v, 4 * (uint32_t)sizeof(RdScreenVtx)) : ~0u;
    const uint32_t off = rd__frame_payload(f, &r, sizeof(r));
    RdCmd *c = rd__push(RDC_POST_STUB);
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
 * target the hardware sprite (rd__blur_screen_fallback) has its U moved the
 * same half texel (rd_post.c records that pair too), so it would sample at
 * x + s / 4 and, mirrored, x - s / 4 in the target's texels (s the scene's
 * horizontal scale), again the mirror image.  A fraction of s / 4 blends two
 * neighbouring columns over the whole frame (s = 2.8125 at 1920x1080 16:9
 * gives 30 / 70), a softness the PS2's 1x model never had at that size, so
 * the replay snaps the offset to a whole texel (rd__blur_snap_verts): it
 * samples at x + round(s / 4) and, mirrored, x - round(s / 4).  The whole
 * texel part of the bias stays (position parity), no column is blended.  A
 * whole s is left as it is: every block of s texels then holds one GS
 * pixel's PS2 blend, and the block averages equal the 1x picture
 * (rd_present_test scale2). */
void rd__blur_uv_rect(uint32_t kind, const RdPostRec *p, float uv[4])
{
    memcpy(uv, p->uv, sizeof(p->uv));
    if (kind == RD_POST_REDUCTION && rd__mirror_on()) {
        uv[0] -= 8.0f;
        uv[2] -= 8.0f;
    }
}

/* The reduction on a scaled target (Enhanced above 1x, or 1x at a wider
 * aspect): fx_sprite_ps steps the GS position in 1/16 GS pixel and weighs in
 * 1/16 texel, which at a scale that is not an integer puts neighbouring
 * texels' samples up to a tenth of a texel off their place (edges of up to
 * 33 LSB jitter at 16:9 1080p).  There the reduction draws a
 * hardware-filtered sprite, continuous in position and weight;
 * Original, and Enhanced wherever both targets are at scale 1, take
 * the exact model.  Returns the payload offset of the two vertices to draw
 * (the mirrored pair with the mirror on), or ~0u for the model. */
uint32_t rd__blur_screen_fallback(uint32_t kind, const RdPostRec *p, const RdStateBlock *st)
{
    if (kind != RD_POST_REDUCTION || p->lutOffset == ~0u) {
        return ~0u;
    }
    const RdTargetRec *dst = rd__target_rec(st->color);
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    const RdTargetRec *src = t && t->kind == RD_TEXKIND_TARGET ? rd__target_rec(t->target) : NULL;
    const int scaled = (dst && (dst->sx != 1.0f || dst->sy != 1.0f)) ||
                       (src && (src->sx != 1.0f || src->sy != 1.0f));
    if (!scaled) {
        return ~0u;
    }
    return p->lutOffset + (rd__mirror_on() ? 2u * (uint32_t)sizeof(RdScreenVtx) : 0u);
}

/* The two vertices of the hardware sprite (rd__blur_screen_fallback's pair,
 * already copied by the caller) with S moved so the sample lands on a
 * texel centre of the scene: by (round(s / 4) - s / 4) / s GS texels, in
 * 1/16 units, the sign reversed with the mirror on (the sprite's bias is
 * -s / 4 there).  s is the scene target's horizontal scale; a whole s
 * keeps the PS2 blend. */
void rd__blur_snap_verts(const RdStateBlock *st, RdScreenVtx v[2])
{
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    const RdTargetRec *src = t && t->kind == RD_TEXKIND_TARGET ? rd__target_rec(t->target) : NULL;
    if (!src || !(src->sx > 0.0f)) {
        return;
    }
    const float sx = src->sx;
    if (sx == floorf(sx)) {
        return; /* a whole scale is the PS2's quarter pixel, replicated (2x averages to 1x) */
    }
    const float quarter = sx * 0.25f;
    float d = (floorf(quarter + 0.5f) - quarter) / sx * 16.0f;
    if (rd__mirror_on()) {
        d = -d;
    }
    v[0].s += d;
    v[1].s += d;
}

/* fx_sprite_ps's U at the pixel of 12.4 coordinate X: u0 + (X - x0)
 * (u1 - u0) / (x1 - x0), the quotient truncated toward zero (the shader's
 * gs_div_trunc_rcp; rd_replay_tiler.c blurFoldU) */
static int32_t borderU(int32_t X, int32_t x0, int32_t x1, int32_t u0, int32_t u1)
{
    return u0 + (int32_t)(((int64_t)X - x0) * ((int64_t)u1 - u0) / ((int64_t)x1 - x0));
}

/* the 12.4 coordinate of GS pixel 0 of a target gsW wide, XYOFFSET centred
 * on it (rd_post.c postReduction): pixel x is at origin + 16 x */
static int32_t borderOrigin(int32_t gsW)
{
    return 16 * (2048 - (gsW >> 1));
}

/* The border's strips (rd_internal.h).  The left one covers X from the
 * sprite's x0 (a quarter pixel before pixel 0) to pixel 2's coordinate, so
 * pixels 0 and 1; the right one from a quarter pixel before pixel gsW - 2
 * to the sprite's x1.  With u0 = u1 every pixel of a strip gets U(pixel 2)
 * (or U(pixel gsW - 3)): the same two texels at the same 4-bit weights
 * that column was drawn with, so the same bytes.  V is the sprite's, its
 * rows untouched. */
bool rd__blur_border_strips(const RdPostRec *p, const float uv[4], int32_t gsW, RdPostRec out[2])
{
    if (gsW < 6) {
        return false;
    }
    const int32_t o = borderOrigin(gsW);
    const int32_t x0 = (int32_t)p->rect[0], x1 = (int32_t)p->rect[2];
    const int32_t u0 = (int32_t)uv[0], u1 = (int32_t)uv[2];
    if ((float)x0 != p->rect[0] || (float)x1 != p->rect[2] || (float)u0 != uv[0] ||
        (float)u1 != uv[2] || x0 > o || x1 <= o + 16 * (gsW - 1)) {
        return false; /* not the reduction's sprite over the whole width */
    }
    const int32_t xl = o + 32, xr = o + 16 * (gsW - 3);
    const int32_t ul = borderU(xl, x0, x1, u0, u1), ur = borderU(xr, x0, x1, u0, u1);
    out[0] = *p;
    out[1] = *p;
    out[0].rect[2] = (float)xl; /* pixels 0 and 1: X < pixel 2's */
    out[1].rect[0] = (float)(o + 16 * (gsW - 2) - 4);
    out[0].uv[0] = out[0].uv[2] = (float)ul;
    out[1].uv[0] = out[1].uv[2] = (float)ur;
    return true;
}

void rd__blur_border_verts(const RdScreenVtx v[4], int32_t gsW, RdScreenVtx out[8])
{
    const int32_t o = borderOrigin(gsW);
    const int32_t at[2] = {o + 32, o + 16 * (gsW - 3)};
    for (int k = 0; k < 2; k++) {
        for (int m = 0; m < 2; m++) {
            const RdScreenVtx *a = &v[2 * m], *b = &v[2 * m + 1];
            RdScreenVtx *w = &out[4 * k + 2 * m];
            w[0] = *a;
            w[1] = *b;
            const double dx = (double)b->x - (double)a->x;
            const double s = dx != 0.0 ? (double)a->s + ((double)at[k] - (double)a->x) *
                                                            ((double)b->s - (double)a->s) / dx
                                       : (double)a->s;
            w[0].s = w[1].s = (float)s;
            if (k == 0) {
                w[1].x = o + 32;
            } else {
                w[0].x = o + 16 * (gsW - 2) - 4;
            }
        }
    }
}

/* The glow's left edge on a wide frame (issue 129, rd_internal.h).  At
 * 4:3 the anchor shift is 0 and x0 is the target's pixel 0, so nothing is
 * drawn; the PS2's own deltas at the left edge (the depth of field's
 * border, the aura's taps) belong to other kinds.  The strip covers X from
 * pixel 0's coordinate to x0, the pixels the sprite leaves, with u0 = u1 =
 * U(first covered pixel): the texels and 4-bit weights that column was
 * drawn with, so its bytes; V is the sprite's, its rows untouched. */
int rd__glow_edge_strip(uint32_t kind, const RdPostRec *p, int32_t gsW, const RdTargetRec *dst,
                        const RdTargetRec *src, RdPostRec *out)
{
    if ((kind != RD_POST_FLARE && kind != RD_POST_BLOOM) || gsW < 2 || !dst || !src || dst == src ||
        !rd__work_class(dst) || !rd__work_class(src) || dst->sx != 1.0f || dst->sy != 1.0f ||
        dst->ox != 0.0f || p->uv[0] != 8.0f) {
        return 0;
    }
    const int32_t o = borderOrigin(gsW);
    const int32_t x0 = (int32_t)p->rect[0], x1 = (int32_t)p->rect[2];
    const int32_t u0 = (int32_t)p->uv[0], u1 = (int32_t)p->uv[2];
    const int32_t d = x0 - o;
    if ((float)x0 != p->rect[0] || (float)x1 != p->rect[2] || (float)u1 != p->uv[2] || d <= 0 ||
        d > RD_GLOW_EDGE_MAX16 || x1 <= x0 + 16) {
        return 0; /* not pass 1 of blur(n) moved off pixel 0 by the anchor */
    }
    const int32_t n = (d + 15) / 16; /* pixels 0 .. n - 1 left */
    const int32_t u = borderU(o + 16 * n, x0, x1, u0, u1);
    *out = *p;
    out->rect[0] = (float)o;
    out->rect[2] = (float)x0;
    out->uv[0] = out->uv[2] = (float)u;
    return (int)n;
}
