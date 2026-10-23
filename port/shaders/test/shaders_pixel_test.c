/* shaders_pixel_test.c: sprite.hlsl, blit.hlsl and blend_int.hlsl run
 * through the Vulkan RHI (lavapipe in the container), checked texel by
 * texel. The expected values come from gs_math.hlsli compiled as C, the
 * same arithmetic the shaders use, so a shader that disagrees with the GS
 * formulas fails here.
 *
 *   pass 1  96x8 RGBA8 target, twelve 8x8 cells drawn with sprite_*_vs and
 *           sprite_ps: untextured, textured modulate, alpha test discard
 *           and pass, dual-source LERP_AS at As 0x80 / 0x40, additive with
 *           As 0xFF through DF_PREMUL (exact) and through the plain
 *           dual-source factor (measured, reported); PABE:
 *           a lerp with As 0x7F writes Cs unblended, a LERP_FIX with As
 *           0x90 blends by FIX, Cd*FIX + Cs with As 0x10 writes Cs; FBA: a
 *           lerp with As 0x40 blends by 0x40 and stores alpha 0xC0
 *   planes  pass 1 again into another target with sprite.hlsl's planes
 *           build (ICO_SPRITE_PLANES: sprite_plane_ui_vs,
 *           sprite_plane_world_vs, sprite_ps_planes, the IcoSpritePlaneVertex
 *           layout, Q 1, no exact viewport so no planes): byte for byte the
 *           plain build's picture, the two builds being one source
 *   pass 2  blit_ps: identity tint copy of pass 1 (exact), then tinted
 *   pass 3  blend_int_ps: three GS blends on RGBA8_UINT textures (exact)
 *   above1x (CPU, before the device) common.hlsli's above-1x kernels
 *           compiled as C against port/render/test/above1x_ref.c:
 *           gs_bayer8 the Bayer matrix's recursion (each value once a
 *           block), gs_bspline_w the B-spline's distance form (each fraction's weights summing to
 *           24576, mirror-symmetric), gs_box_w the counted overlap, the
 *           axes' kinds and taps, and fx_sprite_ps's separable combine over
 *           a noise texture, every step from 0 to past the box's widest;
 *           gs_fog_channel (the fog above 1x) against a1x_fog's formula
 *           over entries, fractions, factors and every dither threshold
 *   grid    (CPU) common.hlsli gs_grid_level's claim: the coordinate it
 *           hands the sampler, q / 16 / size in float, puts the sampler's
 *           texel pair and weight at the GS's ((q - 8) >> 4, (q - 8) & 15)
 *           for every power-of-two size to 2048 and q over four periods
 *           each way; and the residual of an exact bilinear (one weighted
 *           sum, rounded) against the GS's truncating steps (gs_bilerp)
 *           over every pair of 4-bit weights and texel values in steps of
 *           17: 0 to 2 levels above, never below, the histogram printed
 *   a1x steps  sprite_ps above 1x (DF_ABOVE1X) over a noise texture
 *           magnified (both axes the B-spline: sprite_bspline4's table),
 *           decimated (both the box) and mixed, drawn with the steps from
 *           DrawCB.scale.w as rd_fx_plan.c rd__a1x_sprite_steps computes
 *           them and with scale.w 0 (the steps from the pixel's
 *           footprint): within 2 levels of each other (sprite.hlsl
 *           sprite_above1x; the count of differing pixels printed)
 *
 * --nodual (ctest shaders_pixel_nodual): sprite_ps_nodual (and
 * sprite_ps_planes_nodual for the planes pass),
 * and the draws that read the second output (the lerps, Cd*FIX + Cs, the
 * plain additive) drawn as rd_pipeline.c's two-pass fallback draws them: a
 * colour pass (ICO_DF_NODUAL_FACTOR, SRC_ALPHA factors, mask RGB), then an
 * alpha pass (ICO_DF_NODUAL_ALPHA_PASS, no blending, mask A); the same
 * expectations.  It needs no dual-source blending (the ctest runs it with
 * the feature off, ICO_VK_FAKE_NO_DUAL=1).
 *
 * Exit 0 on success, 1 on a mismatch, 77 (skipped) without a Vulkan device. */
#include "hlsl_shim.h"
#include "gs_math.hlsli"
#include "common.hlsli"
#include "above1x_ref.h"
#include "rhi.h"
#include "shader_consts.h"
#include "shaders_gen.h"
#include "vk/rhi_vk.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 96
#define H 8
#define CELL 8

static int failures;

static void failf(const char *what, int x, int y, const uint8_t *got, const int *want)
{
    if (failures < 30) {
        printf("FAIL %s (%d,%d): got %3u %3u %3u %3u, expected %3d %3d %3d %3d\n", what, x, y,
               got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
    }
    failures++;
}

static void expect(const char *what, const uint8_t *img, uint32_t pitch, int x, int y, int r, int g,
                   int b, int a, int tol)
{
    const uint8_t *p = img + (size_t)y * pitch + (size_t)x * 4;
    const int want[4] = {r, g, b, a};
    for (int k = 0; k < 4; k++) {
        if (abs((int)p[k] - want[k]) > tol) {
            failf(what, x, y, p, want);
            return;
        }
    }
}

/* every pixel of cell c (x range [c*8, c*8+8), all rows) */
static void expectCell(const char *what, const uint8_t *img, uint32_t pitch, int cell, int r, int g,
                       int b, int a, int tol)
{
    for (int y = 0; y < H; y++) {
        for (int x = cell * CELL; x < cell * CELL + CELL; x++) {
            expect(what, img, pitch, x, y, r, g, b, a, tol);
        }
    }
}

static RhiShader makeShader(const char *name)
{
    const IcoShaderBlob *b = ico_find_shader(name);
    if (!b) {
        printf("FAIL shader %s not in the table\n", name);
        failures++;
        return (RhiShader){0};
    }
    RhiShaderDesc d = {0};
    d.stage = b->stage == ICO_SHADER_STAGE_VERTEX ? RHI_STAGE_VERTEX : RHI_STAGE_FRAGMENT;
    d.bytecode = b->spirv;
    d.bytecodeSize = b->spirv_len;
    d.entryPoint = b->entry;
    d.debugName = b->name;
    RhiShader s = rhi_create_shader(&d);
    if (!s.id) {
        printf("FAIL shader %s not created\n", name);
        failures++;
    }
    return s;
}

static const RhiBlendState kOpaque = {.writeMask = 0xF};

/* sprite: 0 no vertex input, 1 IcoSpriteVertex, 2 IcoSpritePlaneVertex (the
 * layout rd_pipeline.c gives sprite_plane_*_vs) */
static RhiPipeline makePipeline(RhiShader vs, RhiShader ps, const RhiBindGroupLayout *layouts,
                                int sprite, RhiFormat fmt, const RhiBlendState *blend,
                                const char *name)
{
    static const RhiVertexBinding vb = {0, sizeof(IcoSpriteVertex), false};
    static const RhiVertexAttr va[4] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
    };
    static const RhiVertexBinding vbPlane = {0, sizeof(IcoSpritePlaneVertex), false};
    static const RhiVertexAttr vaPlane[11] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
        {4, 0, RHI_VTX_F32x1, offsetof(IcoSpriteStqVertex, q)},
        {5, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, c0)},
        {6, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, cx)},
        {7, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, cy)},
        {8, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, s0)},
        {9, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, sx)},
        {10, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, sy)},
    };
    RhiPipelineDesc d = {0};
    d.vertex = vs;
    d.fragment = ps;
    d.layouts = layouts;
    d.layoutCount = 3;
    if (sprite == 1) {
        d.vertexBindings = &vb;
        d.vertexBindingCount = 1;
        d.vertexAttrs = va;
        d.vertexAttrCount = 4;
    } else if (sprite == 2) {
        d.vertexBindings = &vbPlane;
        d.vertexBindingCount = 1;
        d.vertexAttrs = vaPlane;
        d.vertexAttrCount = 11;
    }
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0] = *blend;
    d.colorFormats[0] = fmt;
    d.colorCount = 1;
    d.depthFormat = RHI_FMT_UNKNOWN;
    d.debugName = name;
    RhiPipeline p = rhi_create_pipeline(&d);
    if (!p.id) {
        printf("FAIL pipeline %s not created\n", name);
        failures++;
    }
    return p;
}

/* a quad over cell c, texels (0,0)-(tw,th) */
static void cellQuad(IcoSpriteVertex *v, int cell, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                     float tw, float th)
{
    const uint32_t x0 = (uint32_t)(cell * CELL) * 16u, x1 = x0 + CELL * 16u;
    const uint32_t y0 = 0, y1 = H * 16u;
    const uint32_t px[6] = {x0, x1, x0, x0, x1, x1};
    const uint32_t py[6] = {y0, y0, y1, y1, y0, y1};
    const float pu[6] = {0, tw, 0, 0, tw, tw};
    const float pv[6] = {0, 0, th, th, 0, th};
    for (int i = 0; i < 6; i++) {
        v[i] = (IcoSpriteVertex){(uint16_t)px[i], (uint16_t)py[i], 0, {r, g, b, a}, pu[i], pv[i]};
    }
}

enum {
    Q_UNTEX,
    Q_TEX,
    Q_ATEST_FAIL,
    Q_ATEST_PASS,
    Q_LERP80,
    Q_LERP40,
    Q_ADD_FF,
    Q_PABE,
    Q_ADD_PLAIN,
    Q_PABE_ON,
    Q_FBA,
    Q_PABE_DSTFIX,
    Q_COUNT
};

static void setTex(IcoDrawCB *cb, float w, float h)
{
    cb->tex[0] = w;
    cb->tex[1] = h;
    cb->tex[2] = 1.0f / w;
    cb->tex[3] = 1.0f / h;
}

/* ------------------------------------------------------------ above 1x */

_Static_assert(GS_RS_BOX_MIN32 == A1X_BOX_MIN32 && GS_RS_BSPLINE_MAX32 == A1X_BSPLINE_MAX32 &&
                   GS_RS_BOX_MAX32 == A1X_BOX_MAX32 && GS_RS_BSPLINE_DIV == A1X_BSPLINE_DIV,
               "common.hlsli's above-1x constants are above1x_ref.h's");

static uint32_t a1xNoise(int x, int y, int ch)
{
    uint32_t h = (uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u ^ (uint32_t)ch * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h & 255u;
}

static void a1xFetch(void *user, int x, int y, uint32_t out[4])
{
    (void)user;
    for (int ch = 0; ch < 4; ch++) {
        out[ch] = a1xNoise(x, y, ch);
    }
}

/* fx_sprite_ps's FXF_ABOVE1X combine with the hlsli's axis functions */
static void a1xShader(int c16x, int c16y, int sx, int sy, uint linear, uint32_t out[4])
{
    const int kx = gs_rs_kind(sx, linear), ky = gs_rs_kind(sy, linear);
    const int fx = gs_rs_first(kx, c16x, sx, linear), nx = gs_rs_count(kx, c16x, sx, linear);
    const int fy = gs_rs_first(ky, c16y, sy, linear), ny = gs_rs_count(ky, c16y, sy, linear);
    const uint dx = (uint)gs_rs_div(kx, sx, linear), dy = (uint)gs_rs_div(ky, sy, linear);
    uint32_t sum[4] = {0, 0, 0, 0};
    for (int j = 0; j < ny; j++) {
        uint32_t row[4] = {0, 0, 0, 0};
        for (int i = 0; i < nx; i++) {
            const uint w = (uint)gs_rs_weight(kx, c16x, sx, linear, i);
            for (int ch = 0; ch < 4; ch++) {
                row[ch] += a1xNoise(fx + i, fy + j, ch) * w;
            }
        }
        const uint wy = (uint)gs_rs_weight(ky, c16y, sy, linear, j);
        for (int ch = 0; ch < 4; ch++) {
            sum[ch] += ((row[ch] * 16u + dx / 2u) / dx) * wy;
        }
    }
    for (int ch = 0; ch < 4; ch++) {
        out[ch] = (sum[ch] + 8u * dy) / (16u * dy);
    }
}

static void checkAbove1xKernels(void)
{
    const int f0 = failures;
    /* the Bayer matrix: its recursion, each value once in a block */
    int seen[64] = {0};
    for (uint y = 0; y < 8; y++) {
        for (uint x = 0; x < 8; x++) {
            const uint b = gs_bayer8(x, y);
            if (b > 63 || b != a1x_bayer8(x, y) || gs_bayer8(x + 8, y + 16) != b) {
                printf("FAIL above1x: gs_bayer8(%u, %u) = %u, the recursion %u\n", x, y, b,
                       a1x_bayer8(x, y));
                failures++;
            } else {
                seen[b]++;
            }
        }
    }
    for (int v = 0; v < 64; v++) {
        if (seen[v] != 1) {
            printf("FAIL above1x: Bayer value %d appears %d times\n", v, seen[v]);
            failures++;
        }
    }
    /* the B-spline: the distance form, sums, mirror symmetry, no negative
     * weight */
    for (uint f = 0; f < 16; f++) {
        int sum = 0;
        for (uint k = 0; k < 4; k++) {
            const int w = gs_bspline_w(f, k);
            sum += w;
            if (w != a1x_bspline_w(f, k) || w < 0 ||
                (f > 0 && w != gs_bspline_w(16u - f, 3u - k))) {
                printf("FAIL above1x: gs_bspline_w(%u, %u) = %d, the B-spline %d\n", f, k, w,
                       a1x_bspline_w(f, k));
                failures++;
            }
        }
        if (sum != GS_RS_BSPLINE_DIV) {
            printf("FAIL above1x: the B-spline's weights at %u/16 sum to %d\n", f, sum);
            failures++;
        }
    }
    /* the box: the overlap counted in 1/32 */
    for (int lo = -100; lo < 100; lo += 7) {
        for (int len = 1; len < 140; len += 3) {
            for (int i = -5; i < 8; i++) {
                int n = 0;
                for (int q = lo; q < lo + len; q++) {
                    n += q >= 32 * i && q < 32 * i + 32;
                }
                if (gs_box_w(lo, lo + len, i) != n || a1x_box_w(lo, lo + len, i) != n) {
                    printf("FAIL above1x: box [%d, %d) texel %d: %d / %d, counted %d\n", lo,
                           lo + len, i, gs_box_w(lo, lo + len, i), a1x_box_w(lo, lo + len, i), n);
                    failures++;
                }
            }
        }
    }
    /* the axes and the combine against the reference */
    int kinds[3] = {0, 0, 0};
    for (uint linear = 0; linear < 2; linear++) {
        for (int step = 0; step <= GS_RS_BOX_MAX32 + 64; step += (step < 80 ? 1 : 29)) {
            const int k = gs_rs_kind(step, linear);
            kinds[k]++;
            if (k != a1x_kind((uint32_t)step, (int)linear)) {
                printf("FAIL above1x: step %d linear %u: kind %d, the reference %d\n", step, linear,
                       k, a1x_kind((uint32_t)step, (int)linear));
                failures++;
            }
            int wsum = 0;
            for (int i = 0; i < gs_rs_count(k, 37, step, linear); i++) {
                wsum += gs_rs_weight(k, 37, step, linear, i);
            }
            if (wsum != gs_rs_div(k, step, linear)) {
                printf("FAIL above1x: step %d linear %u: the weights sum to %d, not %d\n", step,
                       linear, wsum, gs_rs_div(k, step, linear));
                failures++;
            }
            for (int c = -70; c < 90; c += 13) {
                uint32_t a[4], b[4];
                a1xShader(c, 3 * c + 5, step, (step * 7) % (GS_RS_BOX_MAX32 + 40), linear, a);
                a1x_resample(a1xFetch, NULL, c, 3 * c + 5, (uint32_t)step,
                             (uint32_t)((step * 7) % (GS_RS_BOX_MAX32 + 40)), (int)linear, b);
                if (memcmp(a, b, sizeof(a)) != 0 && failures < f0 + 20) {
                    printf("FAIL above1x: c16 %d step %d linear %u: %u %u %u %u, the reference "
                           "%u %u %u %u\n",
                           c, step, linear, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
                    failures++;
                }
            }
        }
    }
    /* the fog's channel against the formula, and the GS's MODULATE at the
     * table's own entries without the dither's carry (f = 0, mul 128) */
    static const uint muls[] = {0u, 1u, 0x40u, 0x60u, 0x80u, 0xC0u, 0xFFu};
    int fogN = 0;
    for (uint lo = 0; lo < 256; lo += 5) {
        for (uint hi = 0; hi < 256; hi += 9) {
            for (uint f = 0; f < 256; f += 3) {
                for (uint m = 0; m < sizeof(muls) / sizeof(muls[0]); m++) {
                    for (uint b = 0; b < 64; b++) {
                        const uint got = gs_fog_channel(lo, hi, f, muls[m], b);
                        const uint want = a1x_fog(lo, hi, f, muls[m], b);
                        fogN++;
                        if ((got != want || (f == 0 && muls[m] == 0x80u && got != lo)) &&
                            failures < f0 + 20) {
                            printf("FAIL above1x: gs_fog_channel(%u, %u, %u, %u, %u) = %u, the "
                                   "formula %u\n",
                                   lo, hi, f, muls[m], b, got, want);
                            failures++;
                        }
                    }
                }
            }
        }
    }
    printf("  above1x kernels (CPU): %s (GS %d, box %d, B-spline %d steps; %d fog channels)\n",
           failures == f0 ? "ok" : "FAILED", kinds[0], kinds[1], kinds[2], fogN);
}

/* ------------------------------------------------------- the GS's grid */

/* common.hlsli gs_bilerp, one channel: across then down, each step
 * truncated (arithmetic shifts) */
static int gridBilerp(int a, int b, int c, int d, int fu, int fv)
{
    const int top = a + (((b - a) * fu) >> 4);
    const int bot = c + (((d - c) * fu) >> 4);
    return top + (((bot - top) * fv) >> 4);
}

static void checkGridBilinear(void)
{
    /* the sampler's coordinate: gs_grid_level's uvg = q * (1/16) / size,
       the sampler's texel coordinate uvg * size - 0.5 */
    int badCoord = 0;
    long coords = 0;
    for (int lg = 0; lg <= 11; lg++) {
        const int size = 1 << lg;
        for (int q = -64 * size; q <= 64 * size + 64; q++) {
            const float uvg = (float)q * (1.0f / 16.0f) / (float)size;
            const float t = uvg * (float)size - 0.5f;
            int i0 = (int)t; /* floor, without libm */
            i0 -= (float)i0 > t ? 1 : 0;
            badCoord += i0 != (q - 8) >> 4 || t - (float)i0 != (float)((q - 8) & 15) / 16.0f;
            coords++;
        }
    }
    if (badCoord) {
        printf("FAIL grid: %d of %ld coordinates off the GS's texel pair and weight\n", badCoord,
               coords);
        failures++;
    }
    /* the residual: the exact four-texel sum with the same weights,
       rounded, against the GS's three truncating steps */
    long hist[5] = {0, 0, 0, 0, 0};
    int out = 0;
    for (int a = 0; a < 256; a += 17) {
        for (int b = 0; b < 256; b += 17) {
            for (int c = 0; c < 256; c += 17) {
                for (int d = 0; d < 256; d += 17) {
                    for (int fu = 0; fu < 16; fu++) {
                        for (int fv = 0; fv < 16; fv++) {
                            const int g = gridBilerp(a, b, c, d, fu, fv);
                            const int v256 = a * (16 - fu) * (16 - fv) + b * fu * (16 - fv) +
                                             c * (16 - fu) * fv + d * fu * fv;
                            const int h = (v256 + 128) >> 8;
                            const int e = h - g;
                            if (e < 0 || e > 2) {
                                out++;
                            } else {
                                hist[e]++;
                            }
                        }
                    }
                }
            }
        }
    }
    printf("  grid: %ld coordinates on the GS's texels; the exact bilinear above the GS's: "
           "+0 %ld, +1 %ld, +2 %ld, outside %d\n",
           coords, hist[0], hist[1], hist[2], out);
    if (out) {
        printf("FAIL grid: %d samples outside 0..2 levels above the GS's filter\n", out);
        failures++;
    }
}

/* pass 1's pipelines for one build of sprite.hlsl: layout 1 the plain
 * build's vertex (IcoSpriteVertex), 2 the planes build's
 * (IcoSpritePlaneVertex) */
typedef struct SpritePipes {
    RhiPipeline opaque, lerp, add, addPremul, lerpWorld, dstFix, alpha, alphaWorld;
} SpritePipes;

static SpritePipes makeSpritePipes(RhiShader vsUi, RhiShader vsWorld, RhiShader ps,
                                   const RhiBindGroupLayout *layA, int layout, int nodual)
{
    const RhiBlendState lerp = {true,
                                RHI_BF_SRC1_COLOR,
                                RHI_BF_ONE_MINUS_SRC1_COLOR,
                                RHI_BO_ADD,
                                RHI_BF_ONE,
                                RHI_BF_ZERO,
                                RHI_BO_ADD,
                                0xF};
    const RhiBlendState add = {true,       RHI_BF_SRC1_COLOR, RHI_BF_ONE, RHI_BO_ADD,
                               RHI_BF_ONE, RHI_BF_ZERO,       RHI_BO_ADD, 0xF};
    const RhiBlendState addOne = {true,       RHI_BF_ONE,  RHI_BF_ONE, RHI_BO_ADD,
                                  RHI_BF_ONE, RHI_BF_ZERO, RHI_BO_ADD, 0xF};
    /* Cd*FIX + Cs (rd_pipeline.c RD_BP_DST_FIX) */
    const RhiBlendState dstFix = {true,       RHI_BF_ONE,  RHI_BF_SRC1_COLOR, RHI_BO_ADD,
                                  RHI_BF_ONE, RHI_BF_ZERO, RHI_BO_ADD,        0xF};
    /* --nodual: the colour passes (the factor in c0.a, mask RGB) and the
     * alpha pass (the stored alpha, no blending) */
    const RhiBlendState lerpC = {true,
                                 RHI_BF_SRC_ALPHA,
                                 RHI_BF_ONE_MINUS_SRC_ALPHA,
                                 RHI_BO_ADD,
                                 RHI_BF_ONE,
                                 RHI_BF_ZERO,
                                 RHI_BO_ADD,
                                 0x7};
    const RhiBlendState addC = {true,       RHI_BF_SRC_ALPHA, RHI_BF_ONE, RHI_BO_ADD,
                                RHI_BF_ONE, RHI_BF_ZERO,      RHI_BO_ADD, 0x7};
    const RhiBlendState dstFixC = {true,       RHI_BF_ONE,  RHI_BF_SRC_ALPHA, RHI_BO_ADD,
                                   RHI_BF_ONE, RHI_BF_ZERO, RHI_BO_ADD,       0x7};
    const RhiBlendState alphaOnly = {.writeMask = 0x8};
    const RhiFormat f = RHI_FMT_RGBA8_UNORM;
    const int pl = layout == 2;
    SpritePipes o;
    memset(&o, 0, sizeof(o));
    o.opaque = makePipeline(vsUi, ps, layA, layout, f, &kOpaque, pl ? "planes opaque" : "opaque");
    o.addPremul =
        makePipeline(vsUi, ps, layA, layout, f, &addOne, pl ? "planes addPremul" : "addPremul");
    if (!nodual) {
        o.lerp = makePipeline(vsUi, ps, layA, layout, f, &lerp, pl ? "planes lerp" : "lerp");
        o.add = makePipeline(vsUi, ps, layA, layout, f, &add, pl ? "planes add" : "add");
        o.lerpWorld = makePipeline(vsWorld, ps, layA, layout, f, &lerp,
                                   pl ? "planes lerpWorld" : "lerpWorld");
        o.dstFix =
            makePipeline(vsUi, ps, layA, layout, f, &dstFix, pl ? "planes dstFix" : "dstFix");
    } else {
        o.lerp = makePipeline(vsUi, ps, layA, layout, f, &lerpC, pl ? "planes lerpC" : "lerpC");
        o.add = makePipeline(vsUi, ps, layA, layout, f, &addC, pl ? "planes addC" : "addC");
        o.lerpWorld = makePipeline(vsWorld, ps, layA, layout, f, &lerpC,
                                   pl ? "planes lerpWorldC" : "lerpWorldC");
        o.dstFix =
            makePipeline(vsUi, ps, layA, layout, f, &dstFixC, pl ? "planes dstFixC" : "dstFixC");
        o.alpha =
            makePipeline(vsUi, ps, layA, layout, f, &alphaOnly, pl ? "planes alpha" : "alpha");
        o.alphaWorld = makePipeline(vsWorld, ps, layA, layout, f, &alphaOnly,
                                    pl ? "planes alphaWorld" : "alphaWorld");
    }
    return o;
}

/* a1x steps: one 64x16 target per case and form, a 256x64 noise texture
 * (CLAMP, bilinear), one sprite over the target, its UV span the case's */
#define A1W 64
#define A1H 16
#define A1TW 256
#define A1TH 64

static void checkAbove1xSteps(RhiBindGroupLayout l0, RhiBindGroupLayout l1, RhiBindGroupLayout l2a,
                              RhiPipeline opaque)
{
    static const struct {
        const char *name;
        float us, vs; /* the UV span in texels over the 64 x 16 target */
    } kCases[3] = {{"magnified (B-spline)", 16.0f, 4.0f},
                   {"decimated (box)", 256.0f, 64.0f},
                   {"mixed (B-spline x, box y)", 16.0f, 64.0f}};

    const uint64_t offV = 0, offTex = 1024, offU = offTex + A1TW * A1TH * 4;
    const uint64_t size = offU + 8 * 512;
    RhiBuffer ring = rhi_create_buffer(&(RhiBufferDesc){
        size, RHI_BUF_VERTEX | RHI_BUF_UNIFORM | RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "a1x ring"});
    uint8_t *map = rhi_map_buffer(ring);
    RhiTexture tex = rhi_create_texture(&(RhiTextureDesc){
        A1TW, A1TH, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "a1x noise", 0});
    RhiTexture rt[6];
    for (int i = 0; i < 6; i++) {
        rt[i] = rhi_create_texture(&(RhiTextureDesc){A1W, A1H, 1, RHI_FMT_RGBA8_UNORM,
                                                     RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                                                     "a1x target", 0});
    }
    RhiSampler smp = rhi_create_sampler(&(RhiSamplerDesc){RHI_FILTER_LINEAR, RHI_FILTER_LINEAR,
                                                          RHI_FILTER_NEAREST, RHI_WRAP_CLAMP,
                                                          RHI_WRAP_CLAMP, 1.0f, 0.0f, 0.0f, 0.0f});
    if (!ring.id || !map || !tex.id || !rt[5].id || !smp.id) {
        printf("FAIL a1x steps: resources\n");
        failures++;
        return;
    }
    for (int y = 0; y < A1TH; y++) {
        for (int x = 0; x < A1TW; x++) {
            for (int ch = 0; ch < 4; ch++) {
                map[offTex + ((size_t)y * A1TW + (size_t)x) * 4 + (size_t)ch] =
                    (uint8_t)a1xNoise(x, y, ch);
            }
        }
    }
    IcoFrameCB fcb;
    memset(&fcb, 0, sizeof(fcb));
    fcb.target[0] = A1W;
    fcb.target[1] = A1H;
    fcb.target[2] = 1.0f / A1W;
    fcb.target[3] = 1.0f / A1H;
    fcb.frame[0] = 1.0f;
    fcb.z[0] = 1.0f / 16777216.0f;
    memcpy(map + offU, &fcb, sizeof(fcb));
    RhiBinding fb = {0};
    fb.slot = 0;
    fb.type = RHI_BIND_UNIFORM_BUFFER;
    fb.buffer = ring;
    fb.offset = offU;
    fb.size = sizeof(IcoFrameCB);
    RhiBindGroup g0 = rhi_create_bind_group(&(RhiBindGroupDesc){l0, &fb, 1});
    RhiBinding tb[3] = {{0}, {0}, {0}};
    tb[0].slot = 1;
    tb[0].type = RHI_BIND_SAMPLED_TEXTURE;
    tb[0].texture = tex;
    tb[1].slot = 1;
    tb[1].type = RHI_BIND_SAMPLER;
    tb[1].sampler = smp;
    tb[2].slot = 2;
    tb[2].type = RHI_BIND_SAMPLED_TEXTURE;
    tb[2].texture = tex;
    RhiBindGroup g2 = rhi_create_bind_group(&(RhiBindGroupDesc){l2a, tb, 3});
    RhiBindGroup g1[6];
    IcoSpriteVertex v[3 * 6];
    for (int c = 0; c < 3; c++) {
        const uint32_t px[6] = {0, A1W * 16, 0, 0, A1W * 16, A1W * 16};
        const uint32_t py[6] = {0, 0, A1H * 16, A1H * 16, 0, A1H * 16};
        const float pu[6] = {0, kCases[c].us, 0, 0, kCases[c].us, kCases[c].us};
        const float pv[6] = {0, 0, kCases[c].vs, kCases[c].vs, 0, kCases[c].vs};
        for (int i = 0; i < 6; i++) {
            v[c * 6 + i] =
                (IcoSpriteVertex){(uint16_t)px[i],          (uint16_t)py[i], 0,
                                  {0x80, 0x80, 0x80, 0x80}, pu[i] + 3.0f,    pv[i] + 1.0f};
        }
        /* rd__a1x_sprite_steps: the texels a bound texel (scale 1: a GS
         * pixel) steps over, in 1/32, rounded */
        const uint32_t sx = (uint32_t)floor((double)kCases[c].us / A1W * 32.0 + 0.5);
        const uint32_t sy = (uint32_t)floor((double)kCases[c].vs / A1H * 32.0 + 0.5);
        for (int form = 0; form < 2; form++) {
            IcoDrawCB cb;
            memset(&cb, 0, sizeof(cb));
            cb.mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA | ICO_DF_ABOVE1X | ICO_DF_TEXA_CLAMP_S |
                         ICO_DF_TEXA_CLAMP_T;
            cb.mode[2] = ATST_ALWAYS;
            setTex(&cb, A1TW, A1TH);
            cb.scale[3] = form ? (float)(sx * 4096u + sy) : 0.0f;
            const uint64_t at = offU + (uint64_t)(1 + c * 2 + form) * 512;
            memcpy(map + at, &cb, sizeof(cb));
            RhiBinding b = {0};
            b.slot = 1;
            b.type = RHI_BIND_UNIFORM_BUFFER;
            b.buffer = ring;
            b.offset = at;
            b.size = sizeof(IcoDrawCB);
            g1[c * 2 + form] = rhi_create_bind_group(&(RhiBindGroupDesc){l1, &b, 1});
        }
    }
    memcpy(map + offV, v, sizeof(v));
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id || !g0.id || !g2.id || !g1[5].id) {
        printf("FAIL a1x steps: setup\n");
        failures++;
        return;
    }
    RhiTextureBarrier up = {tex, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST};
    rhi_cmd_barrier(cl, &up, 1);
    rhi_cmd_copy_buffer_to_texture(cl, ring, offTex, A1TW * 4, tex, 0, (RhiRect){0, 0, A1TW, A1TH});
    RhiTextureBarrier rd = {tex, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ};
    rhi_cmd_barrier(cl, &rd, 1);
    for (int i = 0; i < 6; i++) {
        RhiTextureBarrier b = {rt[i], RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
        rhi_cmd_barrier(cl, &b, 1);
        RhiRenderPassDesc rp = {0};
        rp.color[0] = (RhiColorAttachment){rt[i], RHI_LOAD_CLEAR, {0}, RHI_STORE_STORE, false};
        rp.colorCount = 1;
        rp.width = A1W;
        rp.height = A1H;
        rhi_cmd_begin_render_pass(cl, &rp);
        rhi_cmd_set_vertex_buffer(cl, 0, ring, offV);
        rhi_cmd_set_pipeline(cl, opaque);
        rhi_cmd_set_bind_group(cl, 0, g0);
        rhi_cmd_set_bind_group(cl, 1, g1[i]);
        rhi_cmd_set_bind_group(cl, 2, g2);
        rhi_cmd_draw(cl, 6, (uint32_t)(i / 2) * 6, 1);
        rhi_cmd_end_render_pass(cl);
        RhiTextureBarrier a = {rt[i], RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC};
        rhi_cmd_barrier(cl, &a, 1);
    }
    rhi_end_commands(cl);
    rhi_submit(cl);
    for (int c = 0; c < 3; c++) {
        static uint8_t img[2][A1W * A1H * 4];
        uint32_t pitch[2] = {0, 0};
        if (!rhi_readback_texture(rt[c * 2], RHI_ASPECT_COLOR, img[0], sizeof(img[0]), &pitch[0]) ||
            !rhi_readback_texture(rt[c * 2 + 1], RHI_ASPECT_COLOR, img[1], sizeof(img[1]),
                                  &pitch[1])) {
            printf("FAIL a1x steps: readback\n");
            failures++;
            return;
        }
        int differ = 0, worst = 0;
        for (int y = 0; y < A1H; y++) {
            for (int x = 0; x < A1W; x++) {
                const uint8_t *s = img[0] + (size_t)y * pitch[0] + (size_t)x * 4;
                const uint8_t *t = img[1] + (size_t)y * pitch[1] + (size_t)x * 4;
                int d = 0;
                for (int k = 0; k < 4; k++) {
                    const int e = abs((int)s[k] - (int)t[k]);
                    d = e > d ? e : d;
                }
                differ += d > 0;
                worst = d > worst ? d : worst;
            }
        }
        printf("  a1x steps %s: the CPU's steps against the footprint's: %d of %d pixels differ, "
               "max %d\n",
               kCases[c].name, differ, A1W * A1H, worst);
        if (worst > 2) {
            printf("FAIL a1x steps %s: %d levels apart (2 allowed)\n", kCases[c].name, worst);
            failures++;
        }
    }
}

int main(int argc, char **argv)
{
    const int nodual = argc > 1 && strcmp(argv[1], "--nodual") == 0;
    checkAbove1xKernels();
    checkGridBilinear();
    if (failures) {
        return 1;
    }
    RhiDeviceDesc dd = {NULL, false, true, "shaders_pixel_test"};
    if (!rhi_init(&dd)) {
        printf("SKIP shaders_pixel_test: no usable Vulkan device\n");
        return 77;
    }
    printf("shaders_pixel_test: adapter %s%s\n", rhi_adapter_name(),
           nodual ? " (two-pass blend, sprite_ps_nodual)" : "");
    if (rhi_limits()->uniformAlign > 512 || 512 % rhi_limits()->uniformAlign) {
        printf("FAIL uniformAlign %u does not divide 512\n", rhi_limits()->uniformAlign);
        failures++;
    }
    const uint32_t FS = 1u << RHI_STAGE_FRAGMENT, VS = 1u << RHI_STAGE_VERTEX;

    static const RhiBindSlot s0[1] = {{0, RHI_BIND_UNIFORM_BUFFER, VS | FS}};
    static const RhiBindSlot s1[1] = {{1, RHI_BIND_UNIFORM_BUFFER, VS | FS}};
    /* t2: sprite_ps's DATE snapshot, read only under DF_DATE */
    static const RhiBindSlot s2a[3] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                                       {1, RHI_BIND_SAMPLER, FS},
                                       {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
    static const RhiBindSlot s2b[2] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                                       {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
    RhiBindGroupLayout l0 = rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){s0, 1, "frame"});
    RhiBindGroupLayout l1 = rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){s1, 1, "draw"});
    RhiBindGroupLayout l2a =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){s2a, 3, "texsmp"});
    RhiBindGroupLayout l2b =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){s2b, 2, "tex2"});
    const RhiBindGroupLayout layA[3] = {l0, l1, l2a}, layB[3] = {l0, l1, l2b};

    RhiShader svsUi = makeShader("sprite_ui_vs"), svsWorld = makeShader("sprite_world_vs");
    RhiShader sps = makeShader(nodual ? "sprite_ps_nodual" : "sprite_ps");
    /* the planes build (ICO_SPRITE_PLANES) */
    RhiShader pvsUi = makeShader("sprite_plane_ui_vs"),
              pvsWorld = makeShader("sprite_plane_world_vs");
    RhiShader pps = makeShader(nodual ? "sprite_ps_planes_nodual" : "sprite_ps_planes");
    RhiShader bvs = makeShader("blit_vs"), bps = makeShader("blit_ps");
    RhiShader nvs = makeShader("blend_int_vs"), nps = makeShader("blend_int_ps");

    SpritePipes sp = makeSpritePipes(svsUi, svsWorld, sps, layA, 1, nodual);
    SpritePipes pp = makeSpritePipes(pvsUi, pvsWorld, pps, layA, 2, nodual);
    RhiPipeline pBlit = makePipeline(bvs, bps, layA, 0, RHI_FMT_RGBA8_UNORM, &kOpaque, "blit");
    RhiPipeline pInt = makePipeline(nvs, nps, layB, 0, RHI_FMT_RGBA8_UINT, &kOpaque, "blendInt");
    if (failures) {
        rhi_shutdown();
        return 1;
    }

    /* ---- data ---- */
    IcoSpriteVertex verts[Q_COUNT * 6];
    cellQuad(&verts[Q_UNTEX * 6], 0, 0x80, 0x40, 0x20, 0x80, 0, 0);
    cellQuad(&verts[Q_TEX * 6], 1, 0x40, 0x80, 0xFF, 0x80, 2, 2);
    cellQuad(&verts[Q_ATEST_FAIL * 6], 2, 200, 100, 50, 0x40, 0, 0);
    cellQuad(&verts[Q_ATEST_PASS * 6], 3, 10, 20, 30, 0x41, 0, 0);
    cellQuad(&verts[Q_LERP80 * 6], 4, 200, 100, 50, 0x80, 0, 0);
    cellQuad(&verts[Q_LERP40 * 6], 5, 200, 100, 50, 0x40, 0, 0);
    cellQuad(&verts[Q_ADD_FF * 6], 6, 64, 64, 64, 0xFF, 0, 0);
    cellQuad(&verts[Q_PABE * 6], 7, 90, 90, 90, 0x7F, 0, 0);
    cellQuad(&verts[Q_ADD_PLAIN * 6], 8, 64, 64, 64, 0xFF, 0, 0);
    cellQuad(&verts[Q_PABE_ON * 6], 9, 200, 100, 50, 0x90, 0, 0);
    cellQuad(&verts[Q_FBA * 6], 10, 200, 100, 50, 0x40, 0, 0);
    cellQuad(&verts[Q_PABE_DSTFIX * 6], 11, 90, 90, 90, 0x10, 0, 0);

    /* the planes build's vertices: the same, Q 1, the planes 0 (FrameCB's
     * viewport is 0, not the exact viewport, so sprite_planes writes none) */
    static IcoSpritePlaneVertex pverts[Q_COUNT * 6];
    memset(pverts, 0, sizeof(pverts));
    for (int i = 0; i < Q_COUNT * 6; i++) {
        pverts[i].s.v = verts[i];
        pverts[i].s.q = 1.0f;
    }

    static const uint8_t texels[4][4] = {
        {200, 100, 50, 0xFF}, {10, 20, 30, 0x80}, {255, 255, 255, 0x40}, {0, 129, 127, 0}};

    /* blend_int inputs: 4x4 UINT, a different value per texel */
    uint8_t srcInt[16 * 4], dstInt[16 * 4];
    for (int i = 0; i < 16; i++) {
        srcInt[i * 4 + 0] = (uint8_t)(i * 17 + 3);
        srcInt[i * 4 + 1] = (uint8_t)(255 - i * 13);
        srcInt[i * 4 + 2] = (uint8_t)(i * 31);
        srcInt[i * 4 + 3] = (uint8_t)(i * 16 + 5); /* As */
        dstInt[i * 4 + 0] = (uint8_t)(250 - i * 9);
        dstInt[i * 4 + 1] = (uint8_t)(i * 7 + 100);
        dstInt[i * 4 + 2] = (uint8_t)(i * 5);
        dstInt[i * 4 + 3] = (uint8_t)(i * 3);
    }

    /* ---- resources ---- */
    const uint64_t offVerts = 0, offTex = 4096, offSrc = 4608, offDst = 5120, offUbo = 8192;
    /* DrawCBs at 512: D_COUNT and their alpha twins; then the planes
     * build's vertices */
    const uint64_t offPlane = 8192 + 64 * 512;
    const uint64_t ringSize = offPlane + sizeof(pverts);
    RhiBuffer ring = rhi_create_buffer(&(RhiBufferDesc){
        ringSize, RHI_BUF_VERTEX | RHI_BUF_UNIFORM | RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "ring"});
    uint8_t *map = rhi_map_buffer(ring);
    if (!ring.id || !map) {
        printf("FAIL ring\n");
        rhi_shutdown();
        return 1;
    }
    const uint32_t RT = RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC;
    RhiTexture A = rhi_create_texture(
        &(RhiTextureDesc){W, H, 1, RHI_FMT_RGBA8_UNORM, RT | RHI_TEX_SAMPLED, "A", 0});
    RhiTexture B = rhi_create_texture(&(RhiTextureDesc){W, H, 1, RHI_FMT_RGBA8_UNORM, RT, "B", 0});
    RhiTexture AP =
        rhi_create_texture(&(RhiTextureDesc){W, H, 1, RHI_FMT_RGBA8_UNORM, RT, "A planes", 0});
    RhiTexture C = rhi_create_texture(&(RhiTextureDesc){W, H, 1, RHI_FMT_RGBA8_UNORM, RT, "C", 0});
    RhiTexture tex = rhi_create_texture(&(RhiTextureDesc){
        2, 2, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "tex2x2", 0});
    RhiTexture usrc = rhi_create_texture(&(RhiTextureDesc){
        4, 4, 1, RHI_FMT_RGBA8_UINT, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "usrc", 0});
    RhiTexture udst = rhi_create_texture(&(RhiTextureDesc){
        4, 4, 1, RHI_FMT_RGBA8_UINT, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "udst", 0});
    RhiTexture uout[3];
    for (int i = 0; i < 3; i++) {
        uout[i] = rhi_create_texture(&(RhiTextureDesc){4, 4, 1, RHI_FMT_RGBA8_UINT, RT, "uout", 0});
    }
    RhiSampler smp = rhi_create_sampler(&(RhiSamplerDesc){RHI_FILTER_NEAREST, RHI_FILTER_NEAREST,
                                                          RHI_FILTER_NEAREST, RHI_WRAP_CLAMP,
                                                          RHI_WRAP_CLAMP, 1.0f, 0.0f, 0.0f, 0.0f});
    if (!A.id || !AP.id || !B.id || !C.id || !tex.id || !usrc.id || !udst.id || !uout[2].id ||
        !smp.id) {
        printf("FAIL resources\n");
        rhi_shutdown();
        return 1;
    }

    /* ---- uniforms ---- */
    IcoFrameCB fcb;
    memset(&fcb, 0, sizeof(fcb));
    fcb.target[0] = W;
    fcb.target[1] = H;
    fcb.target[2] = 1.0f / W;
    fcb.target[3] = 1.0f / H;
    fcb.frame[0] = 1.0f; /* the region scale: a target that is not frame-wide */
    fcb.z[0] = 1.0f / 16777216.0f;

    enum {
        D_UNTEX,
        D_TEX,
        D_ATEST_FAIL,
        D_ATEST_PASS,
        D_LERP80,
        D_LERP40,
        D_ADD_FF,
        D_PABE,
        D_ADD_PLAIN,
        D_PABE_ON,
        D_FBA,
        D_PABE_DSTFIX,
        D_BLIT_ID,
        D_BLIT_TINT,
        D_INT0,
        D_INT1,
        D_INT2,
        D_COUNT
    };

    IcoDrawCB dcb[D_COUNT];
    memset(dcb, 0, sizeof(dcb));
    for (int i = 0; i < D_COUNT; i++) {
        setTex(&dcb[i], 2, 2);
    }
    dcb[D_TEX].mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
    dcb[D_ATEST_FAIL].mode[2] = ATST_GREATER | (1u << 8);
    dcb[D_ATEST_FAIL].mode[3] = 0x40;
    dcb[D_ATEST_PASS].mode[2] = ATST_GREATER | (1u << 8);
    dcb[D_ATEST_PASS].mode[3] = 0x40;
    dcb[D_ADD_FF].mode[0] = ICO_DF_PREMUL;
    dcb[D_PABE].mode[0] = ICO_DF_PABE;
    dcb[D_PABE_ON].mode[0] = ICO_DF_PABE | ICO_DF_FIX_FACTOR;
    dcb[D_PABE_ON].blend[1] = 0x40;
    dcb[D_FBA].mode[0] = ICO_DF_FBA;
    dcb[D_PABE_DSTFIX].mode[0] = ICO_DF_PABE | ICO_DF_FIX_FACTOR | ICO_DF_C1_DST;
    dcb[D_PABE_DSTFIX].blend[1] = 0x40;
    for (int i = D_BLIT_ID; i <= D_BLIT_TINT; i++) {
        setTex(&dcb[i], W, H);
        dcb[i].mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
        dcb[i].uvRect[2] = W;
        dcb[i].uvRect[3] = H;
    }
    for (int k = 0; k < 4; k++) {
        dcb[D_BLIT_ID].col[k] = 0x80;
    }
    const uint32_t tint[4] = {0x40, 0x80, 0xFF, 0x60};
    memcpy(dcb[D_BLIT_TINT].col, tint, sizeof(tint));
    const uint32_t regs[3] = {0x44, 0x64, 0x48}; /* LERP_AS, LERP_FIX 0x70, add wrap */
    const uint32_t fix[3] = {0, 0x70, 0};
    const uint32_t clampMode[3] = {1, 1, 0};
    for (int i = 0; i < 3; i++) {
        dcb[D_INT0 + i].blend[0] = regs[i];
        dcb[D_INT0 + i].blend[1] = fix[i];
        dcb[D_INT0 + i].blend[2] = clampMode[i];
    }

    /* --nodual: the colour pass of a two-pass draw writes the factor, its
     * alpha pass (dcbA, bound from g1a) the stored alpha */
    static const int kTwoPass[] = {D_LERP80,  D_LERP40, D_PABE,       D_ADD_PLAIN,
                                   D_PABE_ON, D_FBA,    D_PABE_DSTFIX};
    IcoDrawCB dcbA[D_COUNT];
    memcpy(dcbA, dcb, sizeof(dcb));
    for (int i = 0; nodual && i < (int)(sizeof(kTwoPass) / sizeof(kTwoPass[0])); i++) {
        dcbA[kTwoPass[i]].mode[0] |= ICO_DF_NODUAL_ALPHA_PASS;
        dcb[kTwoPass[i]].mode[0] |= ICO_DF_NODUAL_FACTOR;
    }

    int exitCode = 0;
    for (int frame = 0; frame < 2; frame++) {
        rhi_wait_frame();
        memcpy(map + offVerts, verts, sizeof(verts));
        memcpy(map + offPlane, pverts, sizeof(pverts));
        memcpy(map + offTex, texels, sizeof(texels));
        memcpy(map + offSrc, srcInt, sizeof(srcInt));
        memcpy(map + offDst, dstInt, sizeof(dstInt));
        RhiBindGroup g1[D_COUNT], g1a[D_COUNT];
        for (int i = 0; i < 2 * D_COUNT; i++) {
            const uint64_t at = offUbo + (uint64_t)(i + 1) * 512;
            memcpy(map + at, i < D_COUNT ? &dcb[i] : &dcbA[i - D_COUNT], sizeof(IcoDrawCB));
            RhiBinding b = {0};
            b.slot = 1;
            b.type = RHI_BIND_UNIFORM_BUFFER;
            b.buffer = ring;
            b.offset = at;
            b.size = sizeof(IcoDrawCB);
            RhiBindGroup g = rhi_create_bind_group(&(RhiBindGroupDesc){l1, &b, 1});
            if (i < D_COUNT) {
                g1[i] = g;
            } else {
                g1a[i - D_COUNT] = g;
            }
        }
        memcpy(map + offUbo, &fcb, sizeof(fcb));
        RhiBinding fb = {0};
        fb.slot = 0;
        fb.type = RHI_BIND_UNIFORM_BUFFER;
        fb.buffer = ring;
        fb.offset = offUbo;
        fb.size = sizeof(IcoFrameCB);
        RhiBindGroup g0 = rhi_create_bind_group(&(RhiBindGroupDesc){l0, &fb, 1});

        RhiBinding t2x2[3] = {{0}, {0}, {0}};
        t2x2[0].slot = 1;
        t2x2[0].type = RHI_BIND_SAMPLED_TEXTURE;
        t2x2[0].texture = tex;
        t2x2[1].slot = 1;
        t2x2[1].type = RHI_BIND_SAMPLER;
        t2x2[1].sampler = smp;
        t2x2[2].slot = 2;
        t2x2[2].type = RHI_BIND_SAMPLED_TEXTURE;
        t2x2[2].texture = tex;
        RhiBindGroup g2tex = rhi_create_bind_group(&(RhiBindGroupDesc){l2a, t2x2, 3});
        RhiBinding tA[3] = {t2x2[0], t2x2[1], t2x2[2]};
        tA[0].texture = A;
        RhiBindGroup g2A = rhi_create_bind_group(&(RhiBindGroupDesc){l2a, tA, 3});
        RhiBinding tI[2] = {{0}, {0}};
        tI[0].slot = 1;
        tI[0].type = RHI_BIND_SAMPLED_TEXTURE;
        tI[0].texture = usrc;
        tI[1].slot = 2;
        tI[1].type = RHI_BIND_SAMPLED_TEXTURE;
        tI[1].texture = udst;
        RhiBindGroup g2int = rhi_create_bind_group(&(RhiBindGroupDesc){l2b, tI, 2});

        RhiCommandList cl = rhi_begin_commands();
        if (!cl.id || !g0.id || !g2tex.id || !g2A.id || !g2int.id) {
            printf("FAIL setup\n");
            failures++;
            break;
        }
        RhiTextureBarrier up[3] = {{tex, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {usrc, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {udst, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST}};
        rhi_cmd_barrier(cl, up, 3);
        rhi_cmd_copy_buffer_to_texture(cl, ring, offTex, 8, tex, 0, (RhiRect){0, 0, 2, 2});
        rhi_cmd_copy_buffer_to_texture(cl, ring, offSrc, 16, usrc, 0, (RhiRect){0, 0, 4, 4});
        rhi_cmd_copy_buffer_to_texture(cl, ring, offDst, 16, udst, 0, (RhiRect){0, 0, 4, 4});
        RhiTextureBarrier pre[8] = {
            {tex, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ},
            {usrc, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ},
            {udst, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ},
            {A, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
            {B, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
            {C, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
            {uout[0], RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
            {uout[1], RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
        };
        rhi_cmd_barrier(cl, pre, 8);
        RhiTextureBarrier pre2[2] = {{uout[2], RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
                                     {AP, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET}};
        rhi_cmd_barrier(cl, pre2, 2);

        /* pass 1: the sprite cells; the clear (64,64,64,64) shows through
         * where nothing is drawn; then the same into AP with the planes
         * build */
        for (int build = 0; build < 2; build++) {
            const SpritePipes *P = build ? &pp : &sp;
            RhiRenderPassDesc rp = {0};
            rp.color[0] = (RhiColorAttachment){build ? AP : A,
                                               RHI_LOAD_CLEAR,
                                               {64 / 255.f, 64 / 255.f, 64 / 255.f, 64 / 255.f},
                                               RHI_STORE_STORE,
                                               false};
            rp.colorCount = 1;
            rp.width = W;
            rp.height = H;
            rhi_cmd_begin_render_pass(cl, &rp);
            rhi_cmd_set_vertex_buffer(cl, 0, ring, build ? offPlane : offVerts);
            rhi_cmd_set_bind_group(cl, 0, g0);
            rhi_cmd_set_bind_group(cl, 2, g2tex);

            /* pa: --nodual's alpha pass after the colour pass (0: one pass) */
            const RhiPipeline none = {0}, pa = nodual ? P->alpha : none;

            struct {
                int q, d;
                RhiPipeline p, pa;
            } draws[Q_COUNT] = {
                {Q_UNTEX, D_UNTEX, P->opaque, none},
                {Q_TEX, D_TEX, P->opaque, none},
                {Q_ATEST_FAIL, D_ATEST_FAIL, P->opaque, none},
                {Q_ATEST_PASS, D_ATEST_PASS, P->opaque, none},
                {Q_LERP80, D_LERP80, P->lerp, pa},
                {Q_LERP40, D_LERP40, P->lerp, pa},
                {Q_ADD_FF, D_ADD_FF, P->addPremul, none},
                {Q_PABE, D_PABE, P->lerpWorld, nodual ? P->alphaWorld : none},
                {Q_ADD_PLAIN, D_ADD_PLAIN, P->add, pa},
                {Q_PABE_ON, D_PABE_ON, P->lerp, pa},
                {Q_FBA, D_FBA, P->lerp, pa},
                {Q_PABE_DSTFIX, D_PABE_DSTFIX, P->dstFix, pa},
            };

            for (int i = 0; i < Q_COUNT; i++) {
                rhi_cmd_set_pipeline(cl, draws[i].p);
                rhi_cmd_set_bind_group(cl, 1, g1[draws[i].d]);
                rhi_cmd_draw(cl, 6, (uint32_t)draws[i].q * 6, 1);
                if (draws[i].pa.id) {
                    rhi_cmd_set_pipeline(cl, draws[i].pa);
                    rhi_cmd_set_bind_group(cl, 1, g1a[draws[i].d]);
                    rhi_cmd_draw(cl, 6, (uint32_t)draws[i].q * 6, 1);
                }
            }
            rhi_cmd_end_render_pass(cl);
        }
        RhiTextureBarrier aRead = {A, RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ};
        rhi_cmd_barrier(cl, &aRead, 1);

        /* pass 2: blit A to B (identity tint) and to C (tinted) */
        for (int k = 0; k < 2; k++) {
            RhiRenderPassDesc bp = {0};
            bp.color[0] =
                (RhiColorAttachment){k ? C : B, RHI_LOAD_DONT_CARE, {0}, RHI_STORE_STORE, false};
            bp.colorCount = 1;
            bp.width = W;
            bp.height = H;
            rhi_cmd_begin_render_pass(cl, &bp);
            rhi_cmd_set_pipeline(cl, pBlit);
            rhi_cmd_set_bind_group(cl, 0, g0);
            rhi_cmd_set_bind_group(cl, 1, g1[D_BLIT_ID + k]);
            rhi_cmd_set_bind_group(cl, 2, g2A);
            rhi_cmd_draw(cl, 3, 0, 1);
            rhi_cmd_end_render_pass(cl);
        }

        /* pass 3: three integer blends */
        for (int k = 0; k < 3; k++) {
            RhiRenderPassDesc ip = {0};
            ip.color[0] =
                (RhiColorAttachment){uout[k], RHI_LOAD_DONT_CARE, {0}, RHI_STORE_STORE, false};
            ip.colorCount = 1;
            ip.width = 4;
            ip.height = 4;
            rhi_cmd_begin_render_pass(cl, &ip);
            rhi_cmd_set_pipeline(cl, pInt);
            rhi_cmd_set_bind_group(cl, 0, g0);
            rhi_cmd_set_bind_group(cl, 1, g1[D_INT0 + k]);
            rhi_cmd_set_bind_group(cl, 2, g2int);
            rhi_cmd_draw(cl, 3, 0, 1);
            rhi_cmd_end_render_pass(cl);
        }
        RhiTextureBarrier apRead = {AP, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC};
        rhi_cmd_barrier(cl, &apRead, 1);
        RhiTextureBarrier post[6] = {{A, RHI_STATE_SHADER_READ, RHI_STATE_COPY_SRC},
                                     {B, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {C, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {uout[0], RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {uout[1], RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {uout[2], RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC}};
        rhi_cmd_barrier(cl, post, 6);
        rhi_end_commands(cl);
        rhi_submit(cl);

        /* ---- checks ---- */
        static uint8_t imgA[W * H * 4], imgAP[W * H * 4], imgB[W * H * 4], imgC[W * H * 4];
        uint32_t pitch = 0, pitchAP = 0;
        if (!rhi_readback_texture(A, RHI_ASPECT_COLOR, imgA, sizeof(imgA), &pitch) ||
            !rhi_readback_texture(AP, RHI_ASPECT_COLOR, imgAP, sizeof(imgAP), &pitchAP) ||
            !rhi_readback_texture(B, RHI_ASPECT_COLOR, imgB, sizeof(imgB), &pitch) ||
            !rhi_readback_texture(C, RHI_ASPECT_COLOR, imgC, sizeof(imgC), &pitch)) {
            printf("FAIL readback\n");
            failures++;
            break;
        }
        /* the planes build: pass 1's picture byte for byte */
        {
            int differ = 0;
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    const uint8_t *s = imgA + (size_t)y * pitch + (size_t)x * 4;
                    const uint8_t *t = imgAP + (size_t)y * pitchAP + (size_t)x * 4;
                    if (memcmp(s, t, 4) != 0) {
                        if (differ++ == 0) {
                            const int want[4] = {s[0], s[1], s[2], s[3]};
                            failf("planes build", x, y, t, want);
                        }
                    }
                }
            }
            if (frame == 0) {
                printf("  planes build (sprite_plane_*_vs + %s): %d of %d pixels differ from "
                       "the plain build's\n",
                       nodual ? "sprite_ps_planes_nodual" : "sprite_ps_planes", differ, W * H);
            }
        }
        expectCell("untextured", imgA, pitch, 0, 0x80, 0x40, 0x20, 0x80, 0);
        /* textured: the 2x2 texture over 8x8, texel = 4x4 pixels */
        for (int y = 0; y < H; y++) {
            for (int x = CELL; x < 2 * CELL; x++) {
                const uint8_t *t = texels[(y / 4) * 2 + ((x - CELL) / 4)];
                const uint32_t c[4] = {0x40, 0x80, 0xFF, 0x80};
                expect("textured", imgA, pitch, x, y, (int)gs_tfx_mod(t[0], c[0]),
                       (int)gs_tfx_mod(t[1], c[1]), (int)gs_tfx_mod(t[2], c[2]),
                       (int)gs_tfx_mod(t[3], c[3]), 0);
            }
        }
        expectCell("alpha test discard", imgA, pitch, 2, 64, 64, 64, 64, 0);
        expectCell("alpha test pass", imgA, pitch, 3, 10, 20, 30, 0x41, 0);
        expectCell("lerp As=0x80", imgA, pitch, 4, 200, 100, 50, 0x80, 0);
        /* hardware unorm blend: within one LSB of the integer formula */
        expectCell("lerp As=0x40", imgA, pitch, 5, gs_blend_reg_ch(0x44, 200, 64, 0x40, 0, 0, 1),
                   gs_blend_reg_ch(0x44, 100, 64, 0x40, 0, 0, 1),
                   gs_blend_reg_ch(0x44, 50, 64, 0x40, 0, 0, 1), 0x40, 1);
        /* Cs*As + Cd with As = 0xFF: the term is 64 * 255 >> 7 = 127. With
         * DF_PREMUL the shader computes it, exactly. With the plain
         * dual-source factor 255/128 the hardware clamps the factor to 1.0
         * on UNORM targets (reported, not asserted: driver behaviour). */
        {
            int want = gs_blend_reg_ch(0x48, 64, 64, 0xFF, 0, 0, 1);
            expectCell("add As=0xFF, DF_PREMUL", imgA, pitch, 6, want, want, want, 0xFF, 0);
            if (frame == 0) {
                const uint8_t *p = imgA + 8 * CELL * 4;
                printf("  plain dual-source additive As=0xFF: got %u, GS formula %d (%s)\n", p[0],
                       want, p[0] == want ? "factor above 1.0 survives" : "factor clamped to 1.0");
            }
        }
        /* PABE: the GS writes Cs where the MSB of As is clear (the blend
         * is skipped, not made to give Cd), and blends where it is set */
        expectCell("lerp with PABE, As MSB clear: Cs", imgA, pitch, 7, 90, 90, 90, 0x7F, 0);
        expectCell("LERP_FIX 0x40 with PABE, As 0x90: blended", imgA, pitch, 9,
                   gs_blend_reg_ch(0x64, 200, 64, 0x90, 0, 0x40, 1),
                   gs_blend_reg_ch(0x64, 100, 64, 0x90, 0, 0x40, 1),
                   gs_blend_reg_ch(0x64, 50, 64, 0x90, 0, 0x40, 1), 0x90, 1);
        expectCell("Cd*FIX + Cs with PABE, As MSB clear: Cs", imgA, pitch, 11, 90, 90, 90, 0x10, 0);
        /* FBA forces the stored alpha's MSB after the blend: the factor is
         * still As 0x40 */
        expectCell("lerp As=0x40 with FBA", imgA, pitch, 10,
                   gs_blend_reg_ch(0x44, 200, 64, 0x40, 0, 0, 1),
                   gs_blend_reg_ch(0x44, 100, 64, 0x40, 0, 0, 1),
                   gs_blend_reg_ch(0x44, 50, 64, 0x40, 0, 0, 1), 0xC0, 1);

        /* blit identity: exact copy; tinted: the texture function */
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                const uint8_t *s = imgA + (size_t)y * pitch + (size_t)x * 4;
                expect("blit identity", imgB, pitch, x, y, s[0], s[1], s[2], s[3], 0);
                expect("blit tint", imgC, pitch, x, y, (int)gs_tfx_mod(s[0], tint[0]),
                       (int)gs_tfx_mod(s[1], tint[1]), (int)gs_tfx_mod(s[2], tint[2]),
                       (int)gs_tfx_mod(s[3], tint[3]), 0);
            }
        }

        /* blend_int against the arithmetic of gs_math.hlsli */
        for (int k = 0; k < 3; k++) {
            uint8_t u[4 * 4 * 4];
            uint32_t up2 = 0;
            if (!rhi_readback_texture(uout[k], RHI_ASPECT_COLOR, u, sizeof(u), &up2)) {
                printf("FAIL uint readback\n");
                failures++;
                break;
            }
            for (int i = 0; i < 16; i++) {
                const uint8_t *s = &srcInt[i * 4], *d = &dstInt[i * 4];
                int want[3];
                for (int c = 0; c < 3; c++) {
                    want[c] =
                        gs_blend_reg_ch(regs[k], s[c], d[c], s[3], d[3], (int)fix[k], clampMode[k]);
                }
                expect("blend_int", u, up2, i % 4, i / 4, want[0], want[1], want[2], s[3], 0);
            }
        }
    }
    if (!nodual) {
        checkAbove1xSteps(l0, l1, l2a, sp.opaque);
    }
    if (rhi_vk_validation_error_count() != 0) {
        printf("FAIL %u validation errors\n", (unsigned)rhi_vk_validation_error_count());
        failures++;
    }
    if (failures) {
        printf("shaders_pixel_test: %d failures\n", failures);
        exitCode = 1;
    } else {
        printf("shaders_pixel_test: ok\n");
    }
    rhi_shutdown();
    return exitCode;
}
