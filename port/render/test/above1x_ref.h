/* above1x_ref.h: the above-1x resampling on the CPU, the oracle of
 * fx_sprite_ps's FXF_ABOVE1X path (rd_blur_test's above1x case) and of
 * common.hlsli's kernels compiled as C (shaders_pixel_test).  Written from
 * the definitions, not from the shader's code: the B-spline from its
 * distance form, the Bayer matrix by its recursion, the box by overlap.
 * docs/port/RENDER_POST.md "Above 1x" says what is modelled.
 *
 * a1x_bayer8      the 8x8 ordered-dither threshold 0..63 at (x, y), built
 *                 by the matrix's recursion
 * a1x_bspline_w   the cubic B-spline's (B = 1, C = 0) weight of tap k
 *                 (0..3: texels i0 - 1 .. i0 + 2) at the 4-bit fraction f,
 *                 in 1/A1X_BSPLINE_DIV
 * a1x_box_w       texel i's overlap with [lo32, hi32), in 1/32 texel
 * a1x_step32      the step, src texels per bound texel in 1/32, of a
 *                 sprite with UV extent du over rect extent dx (both 12.4),
 *                 src at ss texels and the bound target at ts texels a GS
 *                 texel: rounded, at most A1X_STEP_MAX
 * a1x_kind        the kernel of an axis at step32 (A1X_GS, A1X_BOX,
 *                 A1X_BSPLINE)
 * a1x_resample    one texel resampled about the GS sample point (c16x,
 *                 c16y) in src's 12.4 texels: the taps fetched through
 *                 fetch (addressing and TEXA are the caller's), each axis
 *                 its kernel, combined in integers: per row
 *                 R' = (16 sum(wx t) + Dx / 2) / Dx, then
 *                 (sum(wy R') + 8 Dy) / (16 Dy)
 * a1x_fog         one channel of the fog above 1x (fog_lut.hlsl): the
 *                 table's entries lo and hi (n and n + 1) at the fraction
 *                 f / 256 between them, times the texture function's factor
 *                 mul / 128, plus the ordered dither's threshold
 *                 (bayer + 0.5) / 64, rounded down, at most 255 */
#ifndef PORT_RENDER_TEST_ABOVE1X_REF_H
#define PORT_RENDER_TEST_ABOVE1X_REF_H

#include <stdint.h>

#define A1X_BOX_MIN32 64
#define A1X_BSPLINE_MAX32 16
#define A1X_BOX_MAX32 1024
#define A1X_BSPLINE_DIV 24576
#define A1X_STEP_MAX 4095

enum { A1X_GS = 0, A1X_BOX = 1, A1X_BSPLINE = 2 };

typedef void (*A1xFetch)(void *user, int x, int y, uint32_t out[4]);

uint32_t a1x_bayer8(uint32_t x, uint32_t y);
int a1x_bspline_w(uint32_t f, uint32_t k);
int a1x_box_w(int lo32, int hi32, int i);
uint32_t a1x_step32(double du, double dx, double ss, double ts);
int a1x_kind(uint32_t step32, int linear);
uint32_t a1x_fog(uint32_t lo, uint32_t hi, uint32_t f, uint32_t mul, uint32_t bayer);
void a1x_resample(A1xFetch fetch, void *user, int c16x, int c16y, uint32_t step32x,
                  uint32_t step32y, int linear, uint32_t out[4]);

#endif
