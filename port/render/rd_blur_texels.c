/* rd_blur_texels.c: the texels a staticBlur sprite reads (rd_replay.c
 * blurSnapArea's two pure helpers, kept apart from rd_replay.c, which is at
 * the 256 KiB size cap): one axis's texel range of a sprite sampling the
 * target it draws into (rd__blur_uv_axis) and the above-1x kernel's reach
 * past the GS's taps (rd__blur_kernel_reach).  Declared in rd_replay.h. */
#include <math.h>
#include <stdint.h>

#include "rd_replay.h"

static int32_t floorDiv16(int32_t v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

/* fx_sprite_ps's texel range on one axis of a sprite sampling the target it
 * draws into, in that target's texels: U(X) = u0 + (X - x0) (u1 - u0) /
 * (x1 - x0) (integers, 12.4, truncated: monotonic in X) for every X a
 * covered fragment can have (within a pixel of x0 .. x1; two allowed),
 * su = round(U * ss), the nearest texel su >> 4 or LINEAR's (su - 8) >> 4
 * and the one after (one texel more), CLAMP on the TEX0 size (round(size *
 * ss), at least 1) and then on the backing size asz: both monotonic, so the
 * range's ends are the ends' images.  False (the whole axis) under REPEAT,
 * for an empty or degenerate sprite and where the shader's int product
 * could overflow. */
bool rd__blur_uv_axis(int32_t x0, int32_t x1, int32_t u0, int32_t u1, float ss, float size,
                      int clamp, uint32_t asz, int32_t crop16, int32_t *lo, int32_t *hi)
{
    const int64_t dx = (int64_t)x1 - x0, du = (int64_t)u1 - u0;
    if (!clamp || dx <= 0 || asz == 0 || (dx + 32) * (du < 0 ? -du : du) > (1ll << 30)) {
        return false;
    }
    const int64_t ua = u0 + (-32 * du) / dx, ub = u0 + ((dx + 32) * du) / dx;
    const float umin = (float)((ua < ub ? ua : ub) - 1), umax = (float)((ua > ub ? ua : ub) + 1);
    const int32_t smin = (int32_t)floorf(umin * ss) - 1, smax = (int32_t)ceilf(umax * ss) + 1;
    int32_t t0 = floorDiv16(smin - 8), t1 = floorDiv16(smax) + 1;
    /* the shader's round() is floor or ceil of the product: the low end
     * clamps on the smaller, the high end on the larger */
    int32_t lszLo = (int32_t)floorf(size * ss), lszHi = (int32_t)ceilf(size * ss);
    lszLo = lszLo < 1 ? 1 : lszLo;
    lszHi = lszHi < 1 ? 1 : lszHi;
    t0 = t0 < 0 ? 0 : (t0 > lszLo - 1 ? lszLo - 1 : t0);
    t1 = t1 < 0 ? 0 : (t1 > lszHi - 1 ? lszHi - 1 : t1);
    if (crop16 > 0) {
        /* a Full pixel crop (fx_sprite.hlsl fx_src_crop16): the frame's
         * texels moved to the crop's, a texel more each way for its
         * rounding, the crop's ends the bounds */
        t0 = floorDiv16(16 * t0 - crop16) - 1;
        t1 = floorDiv16(16 * t1 - crop16) + 1;
        t0 = t0 < 0 ? 0 : t0;
        t1 = t1 < 0 ? 0 : t1;
    }
    *lo = t0 < (int32_t)asz - 1 ? t0 : (int32_t)asz - 1;
    *hi = t1 < (int32_t)asz - 1 ? t1 : (int32_t)asz - 1;
    return true;
}

/* The above-1x kernel's reach past the GS's taps, in texels, for one axis's
 * step32 of the sprite's steps (blurSnapArea: bits 12.. across, 0..11 down;
 * the box above RD_FX_BOX_MIN32, the B-spline below RD_FX_BSPLINE_MAX32) */
int32_t rd__blur_kernel_reach(uint32_t step32)
{
    if (step32 > RD_FX_BOX_MIN32) {
        /* the box: half its width (at most 32 texels) each side of the
         * centre, a texel more for the edges' partial texels */
        const uint32_t w = step32 < 1024u ? step32 : 1024u;
        return (int32_t)((w + 63u) / 64u) + 1;
    }
    /* the B-spline: a texel each side past the bilinear's two (one more
     * kept); the GS's own filter: none */
    return step32 > 0 && step32 < RD_FX_BSPLINE_MAX32 ? 2 : 0;
}
