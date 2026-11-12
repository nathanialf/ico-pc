/* rd_glow_blur_test.c: the glow's blur chain in two passes, its pure parts, no
 * device (port/render/rd_glow_blur.c, rd_glow_blur.h; docs/port/RENDER_POST.md,
 * "The glow's blur in two passes"; DIVERGENCES.md VD-33).
 *
 *   rd_glow_blur_test
 *
 *   t1  the switch: rd__lamps_parse takes the word glowblur (alone and with
 *       others), rd__lamps_bits_text names it, 1 and all leave it out (not a
 *       byte-identical item), the default bits carry it as the build's
 *       RD_GLOW_BLUR_DEFAULT says; every reason has a text
 *   t2  the operator of the game's ten pairs as recorded (the boot corpus's
 *       rd-00400 at 4:3, WORK0 256 x 128, list 7:114 .. 7:304; the PowerVR
 *       phone's v3677 at 848 x 448, WORK0 424 x 128, list 7:84 .. 7:274):
 *       each axis's rows sum to 1, the offsets within 8 texels, nothing
 *       left out of a row's window above RD_GLOW_BLUR_LOST_MAX, the gains
 *       (139/128)^10, (136/128)^10, (132/128)^10 and 1; no blur on the top
 *       row and, at 4:3, the left column (the shrinks' anchor: the kernel
 *       there is the texel itself), and somewhere a spread of the taps above
 *       1 texel; the table round trip (rd__glow_blur_encode, decoded as
 *       fx_glowblur_ps reads it)
 *   t3  against the chain: the GS's integers (the passes' U, the 4-bit
 *       bilinear with its truncations, HIGHLIGHT's colour and clamp, the
 *       glow's edge strip at 848) on a fixed picture of lamp-like blobs
 *       (an LCG, the same one the evidence's Python mirror uses), and the two
 *       passes as the shader computes them (the table decoded, hardware
 *       bilinear with 8-bit fractions, the across pass stored to 8 bits, the
 *       gains and the loss, the store rounded): WORK0's RGB at most
 *       T3_MAX levels apart and T3_MEAN on average (the Python mirror's
 *       numbers plus a margin, below), alpha at most T3_AMAX
 *   t4  refusals: a pass 1 whose colour is not 128 (COLOUR), a pass 2 that
 *       leaves a column of WORK0 (COVER), one pair (FORM), HIGHLIGHT adding
 *       a constant (COLOUR) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_glow_blur.h"
#include "rd_lamps.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* t3's bounds: the Python mirror (build-host evidence glowblur/ctest_mirror.py)
 * of this picture gave WORK0 RGB at most 21 levels apart at 4:3 and at 848,
 * a mean of 0.118 and 0.059 levels, alpha at most 5 and 4; the margins: +4
 * levels, a mean of 0.40 (float32 here, float64 there), +3 on alpha */
#define T3_MAX 25
#define T3_MEAN 0.40
#define T3_AMAX 8

typedef struct ChainCmd {
    int32_t rect[4], uv[4];
    uint8_t rgba[4];
} ChainCmd;

/* rd-00400 (boot corpus, 512 x 512 GS): the ten pairs as recorded */
static const ChainCmd k43[] = {
    {{30720, 31744, 34800, 33776}, {8, 8, 4104, 2056}, {128, 128, 128, 0}}, /* 7:114 */
    {{30720, 31744, 34816, 33792}, {8, 8, 4088, 2040}, {139, 136, 132, 0}}, /* 7:124 */
    {{30720, 31744, 34799, 33775}, {8, 8, 4104, 2056}, {128, 128, 128, 0}}, /* 7:134 */
    {{30720, 31744, 34816, 33792}, {8, 8, 4087, 2039}, {139, 136, 132, 0}}, /* 7:144 */
    {{30720, 31744, 34798, 33774}, {8, 8, 4104, 2056}, {128, 128, 128, 0}}, /* 7:154 */
    {{30720, 31744, 34816, 33792}, {8, 8, 4086, 2038}, {139, 136, 132, 0}}, /* 7:164 */
    {{30720, 31744, 34797, 33773}, {8, 8, 4104, 2056}, {128, 128, 128, 0}}, /* 7:174 */
    {{30720, 31744, 34816, 33792}, {8, 8, 4085, 2037}, {139, 136, 132, 0}}, /* 7:184 */
    {{30720, 31744, 34796, 33772}, {8, 8, 4104, 2056}, {128, 128, 128, 0}}, /* 7:194 */
    {{30720, 31744, 34816, 33792}, {8, 8, 4084, 2036}, {139, 136, 132, 0}}, /* 7:204 */
    {{30720, 31744, 34795, 33771}, {8, 8, 4104, 2056}, {128, 128, 128, 0}}, /* 7:214 */
    {{30720, 31744, 34816, 33792}, {8, 8, 4083, 2035}, {139, 136, 132, 0}}, /* 7:224 */
    {{30720, 31744, 34794, 33770}, {8, 8, 4104, 2056}, {128, 128, 128, 0}}, /* 7:234 */
    {{30720, 31744, 34816, 33792}, {8, 8, 4082, 2034}, {139, 136, 132, 0}}, /* 7:244 */
    {{30720, 31744, 34793, 33769}, {8, 8, 4104, 2056}, {128, 128, 128, 0}}, /* 7:254 */
    {{30720, 31744, 34816, 33792}, {8, 8, 4081, 2033}, {139, 136, 132, 0}}, /* 7:264 */
    {{30720, 31744, 34792, 33768}, {8, 8, 4104, 2056}, {128, 128, 128, 0}}, /* 7:274 */
    {{30720, 31744, 34816, 33792}, {8, 8, 4080, 2032}, {139, 136, 132, 0}}, /* 7:284 */
    {{30720, 31744, 34791, 33767}, {8, 8, 4104, 2056}, {128, 128, 128, 0}}, /* 7:294 */
    {{30720, 31744, 34816, 33792}, {8, 8, 4079, 2031}, {139, 136, 132, 0}}, /* 7:304 */
};

/* v3677 (the PowerVR phone, 848 x 448 GS): the ten pairs as recorded */
static const ChainCmd k848[] = {
    {{29381, 31744, 36138, 33776}, {8, 8, 6792, 2056}, {128, 128, 128, 0}},  /* 7:84 */
    {{29376, 31744, 36160, 33792}, {13, 8, 6770, 2040}, {139, 136, 132, 0}}, /* 7:94 */
    {{29382, 31744, 36138, 33775}, {8, 8, 6792, 2056}, {128, 128, 128, 0}},  /* 7:104 */
    {{29376, 31744, 36160, 33792}, {14, 8, 6770, 2039}, {139, 136, 132, 0}}, /* 7:114 */
    {{29382, 31744, 36136, 33774}, {8, 8, 6792, 2056}, {128, 128, 128, 0}},  /* 7:124 */
    {{29376, 31744, 36160, 33792}, {14, 8, 6768, 2038}, {139, 136, 132, 0}}, /* 7:134 */
    {{29382, 31744, 36135, 33773}, {8, 8, 6792, 2056}, {128, 128, 128, 0}},  /* 7:144 */
    {{29376, 31744, 36160, 33792}, {14, 8, 6767, 2037}, {139, 136, 132, 0}}, /* 7:154 */
    {{29383, 31744, 36134, 33772}, {8, 8, 6792, 2056}, {128, 128, 128, 0}},  /* 7:164 */
    {{29376, 31744, 36160, 33792}, {15, 8, 6766, 2036}, {139, 136, 132, 0}}, /* 7:174 */
    {{29383, 31744, 36132, 33771}, {8, 8, 6792, 2056}, {128, 128, 128, 0}},  /* 7:184 */
    {{29376, 31744, 36160, 33792}, {15, 8, 6764, 2035}, {139, 136, 132, 0}}, /* 7:194 */
    {{29383, 31744, 36131, 33770}, {8, 8, 6792, 2056}, {128, 128, 128, 0}},  /* 7:204 */
    {{29376, 31744, 36160, 33792}, {15, 8, 6763, 2034}, {139, 136, 132, 0}}, /* 7:214 */
    {{29384, 31744, 36130, 33769}, {8, 8, 6792, 2056}, {128, 128, 128, 0}},  /* 7:224 */
    {{29376, 31744, 36160, 33792}, {16, 8, 6762, 2033}, {139, 136, 132, 0}}, /* 7:234 */
    {{29384, 31744, 36128, 33768}, {8, 8, 6792, 2056}, {128, 128, 128, 0}},  /* 7:244 */
    {{29376, 31744, 36160, 33792}, {16, 8, 6760, 2032}, {139, 136, 132, 0}}, /* 7:254 */
    {{29384, 31744, 36127, 33767}, {8, 8, 6792, 2056}, {128, 128, 128, 0}},  /* 7:264 */
    {{29376, 31744, 36160, 33792}, {16, 8, 6759, 2031}, {139, 136, 132, 0}}, /* 7:274 */
};

#define H0 128
#define H1 256

static int32_t ceilDiv16(int32_t v)
{
    return v >= 0 ? (v + 15) / 16 : -((-v) / 16);
}

static int32_t floorDiv16(int32_t v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

/* the chain as rd_replay_tiler.c rd__replay_blur_chain reads it: the
 * origin of a WORK target gsW wide (16 (2048 - gsW / 2)), the covered texels
 * (the rect, the scissor the target), a pass 1's bytes held from column 0
 * on a frame wider than 512 (the glow's edge strip), the TEX0 size
 * (scalar: the 4:3 buffer's at 4:3, 512 x 128 at 848) and the sources'
 * textures (WORK0 w x 128, WORK1 w x 256) */
static void buildChain(const ChainCmd *k, int n, int frameW, RdBlurChain *ch)
{
    memset(ch, 0, sizeof(*ch));
    const int32_t w = frameW / 2;
    ch->w0 = ch->w1 = (uint32_t)w;
    ch->h0 = H0;
    ch->h1 = H1;
    ch->n = (uint32_t)n;
    for (int i = 0; i < n; i++) {
        RdBlurChainPass *p = &ch->p[i];
        memcpy(p->rect, k[i].rect, sizeof(p->rect));
        memcpy(p->uv, k[i].uv, sizeof(p->uv));
        memcpy(p->rgba, k[i].rgba, sizeof(p->rgba));
        p->toWork1 = (i & 1) == 0;
        p->lsz[0] = frameW > 512 ? 512 : w;
        p->lsz[1] = H0;
        p->asz[0] = w;
        p->asz[1] = p->toWork1 ? H0 : H1;
        p->o[0] = 16 * (2048 - w / 2);
        p->o[1] = 16 * (2048 - H0 / 2);
        for (int a = 0; a < 2; a++) {
            int32_t c0 = ceilDiv16(p->rect[a] - p->o[a]);
            int32_t c1 = ceilDiv16(p->rect[a + 2] - p->o[a]) - 1;
            const int32_t top = a ? H0 - 1 : w - 1;
            p->c0[a] = c0 < 0 ? 0 : c0;
            p->c1[a] = c1 > top ? top : c1;
        }
        p->first = p->toWork1 && frameW > 512 && p->c0[0] > 0 ? 0 : p->c0[0];
        p->flags = RD_FXF_TEXTURED | (2u << RD_FXF_TFX_SHIFT) | RD_FXF_TCC | RD_FXF_LINEAR |
                   RD_FXF_CLAMP_S | RD_FXF_CLAMP_T;
        p->texfmt = 0;
    }
}

/* ---------------------------------------------- the chain in the GS's integers */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* one sprite of the chain on buffers of w x h (src sw x sh), as fx_sprite_ps */
static void gsPass(const RdBlurChainPass *p, const uint8_t *src, int sw, int sh, uint8_t *dst,
                   int dw, int dh)
{
    for (int y = p->c0[1]; y <= p->c1[1] && y < dh; y++) {
        const int32_t Y = p->o[1] + 16 * y;
        const int32_t v = p->uv[1] + (int32_t)(((int64_t)(Y - p->rect[1]) * (p->uv[3] - p->uv[1])) /
                                               (p->rect[3] - p->rect[1]));
        const int32_t vv = v - 8, iy = floorDiv16(vv), fv = vv - 16 * iy;
        const int y0 = clampi(clampi(iy, 0, p->lsz[1] - 1), 0, sh - 1);
        const int y1 = clampi(clampi(iy + 1, 0, p->lsz[1] - 1), 0, sh - 1);
        for (int x = p->c0[0]; x <= p->c1[0] && x < dw; x++) {
            const int32_t X = p->o[0] + 16 * x;
            const int32_t u =
                p->uv[0] + (int32_t)(((int64_t)(X - p->rect[0]) * (p->uv[2] - p->uv[0])) /
                                     (p->rect[2] - p->rect[0]));
            const int32_t uu = u - 8, ix = floorDiv16(uu), fu = uu - 16 * ix;
            const int x0 = clampi(clampi(ix, 0, p->lsz[0] - 1), 0, sw - 1);
            const int x1 = clampi(clampi(ix + 1, 0, p->lsz[0] - 1), 0, sw - 1);
            for (int c = 0; c < 4; c++) {
                const int a = src[(y0 * sw + x0) * 4 + c], b = src[(y0 * sw + x1) * 4 + c];
                const int cc = src[(y1 * sw + x0) * 4 + c], d = src[(y1 * sw + x1) * 4 + c];
                const int t = a + (((b - a) * fu) >> 4);
                const int bo = cc + (((d - cc) * fu) >> 4);
                const int f = t + (((bo - t) * fv) >> 4);
                int o;
                if (c < 3) {
                    o = (f * p->rgba[c]) >> 7;
                    o = o > 255 ? 255 : o;
                    o += p->rgba[3];
                } else {
                    o = f + p->rgba[3];
                }
                dst[(y * dw + x) * 4 + c] = (uint8_t)(o > 255 ? 255 : o);
            }
        }
        if (p->toWork1 && p->first == 0 && p->c0[0] > 0) {
            for (int x = 0; x < p->c0[0]; x++) {
                memcpy(&dst[(y * dw + x) * 4], &dst[(y * dw + p->c0[0]) * 4], 4);
            }
        }
    }
}

/* ------------------------------------------------- the two passes, emulated */

static float hwSample(const float *img, int w, int h, float px, float py, int c)
{
    /* the hardware bilinear at texel coordinates (centres at +0.5), clamp
     * to edge, 8-bit fractions */
    const float fx = px - 0.5f, fy = py - 0.5f;
    const int ix = (int)floorf(fx), iy = (int)floorf(fy);
    const float tx = floorf((fx - (float)ix) * 256.0f + 0.5f) / 256.0f;
    const float ty = floorf((fy - (float)iy) * 256.0f + 0.5f) / 256.0f;
    const int x0 = clampi(ix, 0, w - 1), x1 = clampi(ix + 1, 0, w - 1);
    const int y0 = clampi(iy, 0, h - 1), y1 = clampi(iy + 1, 0, h - 1);
    const float a = img[(y0 * w + x0) * 4 + c], b = img[(y0 * w + x1) * 4 + c];
    const float d0 = img[(y1 * w + x0) * 4 + c], d1 = img[(y1 * w + x1) * 4 + c];
    const float top = a + (b - a) * tx, bot = d0 + (d1 - d0) * tx;
    return top + (bot - top) * ty;
}

static void decodeTap(const uint8_t *tab, uint32_t tw, int row, int i, float *w, float *off)
{
    const uint8_t *t = tab + ((size_t)row * tw + (size_t)i) * 4u;
    *w = (float)(t[0] * 256 + t[1]) / 65535.0f;
    *off = (float)(t[2] * 256 + t[3]) / 256.0f - 64.0f;
}

static float decodeLoss(const uint8_t *tab, uint32_t tw, int row, int i, int c)
{
    const uint8_t *hi = tab + ((size_t)row * tw + (size_t)i) * 4u;
    const uint8_t *lo = hi + (size_t)tw * 4u;
    return (float)hi[c] + (float)lo[c] / 256.0f;
}

/* the blobs: the LCG and the picture ctest_mirror.py makes */
static uint32_t s_lcg;

static double lcg(void)
{
    s_lcg = s_lcg * 1664525u + 1013904223u;
    return (double)(s_lcg >> 8) / 16777216.0;
}

static void blobs(uint8_t *img, int w, int h, uint32_t seed)
{
    int *acc = calloc((size_t)w * h * 4, sizeof(int));
    s_lcg = seed;
    for (int k = 0; k < 14; k++) {
        const double cx = lcg() * w, cy = lcg() * h, s = 0.6 + 3.4 * lcg();
        const double amp = 40.0 + 360.0 * lcg();
        double col[3];
        for (int c = 0; c < 3; c++) {
            col[c] = 0.4 + 0.6 * lcg();
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const double d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                const double g = amp * exp(-d2 / (2.0 * s * s));
                for (int c = 0; c < 3; c++) {
                    acc[(y * w + x) * 4 + c] += (int)(g * col[c]);
                }
                acc[(y * w + x) * 4 + 3] += (int)(g * 0.4);
            }
        }
    }
    for (int i = 0; i < w * h * 4; i++) {
        img[i] = (uint8_t)(acc[i] > 255 ? 255 : acc[i]);
    }
    free(acc);
}

static void t3(const char *what, const ChainCmd *k, int frameW, uint32_t seed)
{
    RdBlurChain ch;
    buildChain(k, 20, frameW, &ch);
    const int w = (int)ch.w0;
    uint8_t *r = malloc((size_t)w * H0 * 4), *w0 = malloc((size_t)w * H0 * 4);
    uint8_t *w1 = calloc((size_t)w * H1 * 4, 1);
    blobs(r, w, H0, seed);
    memcpy(w0, r, (size_t)w * H0 * 4);
    for (uint32_t i = 0; i < ch.n; i++) {
        const RdBlurChainPass *p = &ch.p[i];
        if (p->toWork1) {
            gsPass(p, w0, w, H0, w1, w, H1);
        } else {
            gsPass(p, w1, w, H1, w0, w, H0);
        }
    }
    /* the tables */
    const uint32_t n[2] = {(uint32_t)w, H0};
    float *off[2], *wt[2], *loss[2], gain[4], lc[4], lcy[4], gy[4], lost;
    for (int a = 0; a < 2; a++) {
        off[a] = malloc(n[a] * RD_GLOW_BLUR_TAPS * sizeof(float));
        wt[a] = malloc(n[a] * RD_GLOW_BLUR_TAPS * sizeof(float));
        loss[a] = malloc(n[a] * 4 * sizeof(float));
        const int why =
            rd__glow_blur_axis(&ch, a, off[a], wt[a], loss[a], a ? gy : gain, a ? lcy : lc, &lost);
        CHECK(why == RD_GLOW_BLUR_WHY_NONE, "t3 %s axis %d: %s", what, a,
              rd__glow_blur_why_text(why));
    }
    const uint32_t tw = n[0] > n[1] ? n[0] : n[1];
    uint8_t *tab = malloc((size_t)tw * RD_GLOW_BLUR_TABLE_ROWS * 4);
    CHECK(rd__glow_blur_encode(tab, tw, n, off, wt, loss), "t3 %s: encode", what);
    /* across: WORK0 -> WORK1 (WORK0's height), stored to 8 bits */
    float *src = malloc((size_t)w * H0 * 4 * sizeof(float));
    float *mid = calloc((size_t)w * H1 * 4, sizeof(float));
    for (int i = 0; i < w * H0 * 4; i++) {
        src[i] = (float)r[i];
    }
    for (int y = 0; y < H0; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < 4; c++) {
                float s = 0.0f;
                for (int t = 0; t < RD_GLOW_BLUR_TAPS; t++) {
                    float tw0, to;
                    decodeTap(tab, tw, t, x, &tw0, &to);
                    s += tw0 * hwSample(src, w, H0, (float)x + 0.5f + to, (float)y + 0.5f, c);
                }
                mid[(y * w + x) * 4 + c] = floorf(fminf(fmaxf(s, 0.0f), 255.0f) + 0.5f);
            }
        }
    }
    /* down: WORK1 -> WORK0, the gains, the loss */
    double sum = 0.0;
    int worst = 0, aworst = 0;
    for (int y = 0; y < H0; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < 4; c++) {
                float s = 0.0f;
                for (int t = 0; t < RD_GLOW_BLUR_TAPS; t++) {
                    float tw0, to;
                    decodeTap(tab, tw, RD_GLOW_BLUR_AXIS_ROWS + t, y, &tw0, &to);
                    s += tw0 * hwSample(mid, w, H1, (float)x + 0.5f, (float)y + 0.5f + to, c);
                }
                float o = s * gain[c] - decodeLoss(tab, tw, RD_GLOW_BLUR_TAPS, x, c) -
                          decodeLoss(tab, tw, RD_GLOW_BLUR_AXIS_ROWS + RD_GLOW_BLUR_TAPS, y, c) -
                          lc[c];
                o = floorf(fminf(fmaxf(o, 0.0f), 255.0f) + 0.5f);
                const int d = abs((int)o - (int)w0[(y * w + x) * 4 + c]);
                if (c < 3) {
                    sum += d;
                    worst = d > worst ? d : worst;
                } else {
                    aworst = d > aworst ? d : aworst;
                }
            }
        }
    }
    const double mean = sum / (3.0 * w * H0);
    printf("  t3 %s (WORK0 %dx%d, seed %u): RGB at most %d levels from the chain's, a mean of "
           "%.3f; alpha at most %d\n",
           what, w, H0, (unsigned)seed, worst, mean, aworst);
    CHECK(worst <= T3_MAX, "t3 %s: %d levels > %d", what, worst, T3_MAX);
    CHECK(mean <= T3_MEAN, "t3 %s: a mean of %.3f > %.2f", what, mean, T3_MEAN);
    CHECK(aworst <= T3_AMAX, "t3 %s: alpha %d > %d", what, aworst, T3_AMAX);
    for (int a = 0; a < 2; a++) {
        free(off[a]);
        free(wt[a]);
        free(loss[a]);
    }
    free(tab);
    free(src);
    free(mid);
    free(r);
    free(w0);
    free(w1);
}

static void t2(const char *what, const ChainCmd *k, int frameW)
{
    RdBlurChain ch;
    buildChain(k, 20, frameW, &ch);
    const uint32_t n[2] = {ch.w0, ch.h0};
    float *off[2], *wt[2], *loss[2];
    float gain[4], lc[4], gy[4], lcy[4], lost[2];
    for (int a = 0; a < 2; a++) {
        off[a] = malloc(n[a] * RD_GLOW_BLUR_TAPS * sizeof(float));
        wt[a] = malloc(n[a] * RD_GLOW_BLUR_TAPS * sizeof(float));
        loss[a] = malloc(n[a] * 4 * sizeof(float));
        const int why = rd__glow_blur_axis(&ch, a, off[a], wt[a], loss[a], a ? gy : gain,
                                           a ? lcy : lc, &lost[a]);
        CHECK(why == RD_GLOW_BLUR_WHY_NONE, "t2 %s axis %d: %s", what, a,
              rd__glow_blur_why_text(why));
        CHECK(lost[a] <= RD_GLOW_BLUR_LOST_MAX, "t2 %s axis %d: %.4f left out", what, a,
              (double)lost[a]);
        float widest = 0.0f;
        for (uint32_t i = 0; i < n[a]; i++) {
            float s = 0.0f, mu = 0.0f, m2 = 0.0f;
            for (int t = 0; t < RD_GLOW_BLUR_TAPS; t++) {
                const float ww = wt[a][i * RD_GLOW_BLUR_TAPS + t];
                const float o = off[a][i * RD_GLOW_BLUR_TAPS + t];
                s += ww;
                mu += ww * o;
                m2 += ww * o * o;
                CHECK(ww == 0.0f || fabsf(o) <= 8.0f, "t2 %s axis %d pos %u tap %d: offset %.3f",
                      what, a, (unsigned)i, t, (double)o);
            }
            CHECK(fabsf(s - 1.0f) < 1e-4f, "t2 %s axis %d pos %u: weights sum %.6f", what, a,
                  (unsigned)i, (double)s);
            /* the spread of the merged taps (a lower bound of the kernel's) */
            const float sd = sqrtf(fmaxf(m2 - mu * mu, 0.0f));
            widest = sd > widest ? sd : widest;
        }
        printf("  t2 %s axis %d: %u positions, at most %.5f left out, the taps' spread up to "
               "%.2f texels\n",
               what, a, (unsigned)n[a], (double)lost[a], (double)widest);
        CHECK(widest > 1.0f, "t2 %s axis %d: the taps' spread %.2f: no blur", what, a,
              (double)widest);
    }
    /* the gains: the pass 2s' colours, ten times */
    const double want[4] = {pow(139.0 / 128.0, 10), pow(136.0 / 128.0, 10), pow(132.0 / 128.0, 10),
                            1.0};
    for (int c = 0; c < 4; c++) {
        CHECK(fabs(gain[c] - want[c]) < 1e-3, "t2 %s: gain %d %.4f, want %.4f", what, c,
              (double)gain[c], want[c]);
    }
    /* the top row, and at 4:3 the left column: the texel itself (every
     * pass's fraction 0 there; on the phone's 848 the shrinks' anchor moves
     * pass 2's UVs a texel's fraction off it, so no column is exact) */
    for (int a = frameW == 512 ? 0 : 1; a < 2; a++) {
        const uint32_t i = 0u;
        float wmax = 0.0f, at = 99.0f;
        for (int t = 0; t < RD_GLOW_BLUR_TAPS; t++) {
            if (wt[a][i * RD_GLOW_BLUR_TAPS + t] > wmax) {
                wmax = wt[a][i * RD_GLOW_BLUR_TAPS + t];
                at = off[a][i * RD_GLOW_BLUR_TAPS + t];
            }
        }
        CHECK(wmax > 0.999f && fabsf(at) < 1e-3f,
              "t2 %s axis %d position %u: weight %.4f at %.3f (the texel itself expected)", what, a,
              (unsigned)i, (double)wmax, (double)at);
    }
    /* the table's round trip */
    const uint32_t tw = n[0] > n[1] ? n[0] : n[1];
    uint8_t *tab = malloc((size_t)tw * RD_GLOW_BLUR_TABLE_ROWS * 4);
    CHECK(rd__glow_blur_encode(tab, tw, n, off, wt, loss), "t2 %s: encode", what);
    float werr = 0.0f, oerr = 0.0f, lerr = 0.0f;
    for (int a = 0; a < 2; a++) {
        for (uint32_t i = 0; i < n[a]; i++) {
            for (int t = 0; t < RD_GLOW_BLUR_TAPS; t++) {
                float ww, o;
                decodeTap(tab, tw, a * RD_GLOW_BLUR_AXIS_ROWS + t, (int)i, &ww, &o);
                werr = fmaxf(werr, fabsf(ww - wt[a][i * RD_GLOW_BLUR_TAPS + t]));
                if (ww > 0.0f) {
                    oerr = fmaxf(oerr, fabsf(o - off[a][i * RD_GLOW_BLUR_TAPS + t]));
                }
            }
            for (int c = 0; c < 4; c++) {
                const float l =
                    decodeLoss(tab, tw, a * RD_GLOW_BLUR_AXIS_ROWS + RD_GLOW_BLUR_TAPS, (int)i, c);
                lerr = fmaxf(lerr, fabsf(l - loss[a][4 * i + c]));
            }
        }
    }
    CHECK(werr <= 1.0f / 65535.0f && oerr <= 1.0f / 512.0f + 1e-6f && lerr <= 1.0f / 512.0f + 1e-6f,
          "t2 %s: the table's round trip: weight %.7f, offset %.5f, loss %.5f", what, (double)werr,
          (double)oerr, (double)lerr);
    printf("  t2 %s: gains %.4f %.4f %.4f %.4f, the common loss %.2f %.2f %.2f %.2f levels\n", what,
           (double)gain[0], (double)gain[1], (double)gain[2], (double)gain[3], (double)lc[0],
           (double)lc[1], (double)lc[2], (double)lc[3]);
    free(tab);
    for (int a = 0; a < 2; a++) {
        free(off[a]);
        free(wt[a]);
        free(loss[a]);
    }
}

static int axisWhy(const RdBlurChain *ch)
{
    static float off[RD_GLOW_BLUR_MAXN * RD_GLOW_BLUR_TAPS],
        w[RD_GLOW_BLUR_MAXN * RD_GLOW_BLUR_TAPS];
    static float loss[RD_GLOW_BLUR_MAXN * 4];
    float g[4], lc[4], lost;
    return rd__glow_blur_axis(ch, 0, off, w, loss, g, lc, &lost);
}

static void t4(void)
{
    static RdBlurChain ch;
    buildChain(k43, 20, 512, &ch);
    ch.p[2].rgba[0] = 130; /* a pass 1 with a gain */
    CHECK(axisWhy(&ch) == RD_GLOW_BLUR_WHY_COLOUR, "t4: pass 1's colour not 128");
    buildChain(k43, 20, 512, &ch);
    ch.p[3].c1[0] = (int32_t)ch.w0 - 2; /* a pass 2 leaves WORK0's last column */
    CHECK(axisWhy(&ch) == RD_GLOW_BLUR_WHY_COVER, "t4: a pass 2 not covering WORK0");
    buildChain(k43, 2, 512, &ch);
    CHECK(axisWhy(&ch) == RD_GLOW_BLUR_WHY_FORM, "t4: one pair");
    buildChain(k43, 20, 512, &ch);
    ch.p[5].rgba[3] = 4; /* HIGHLIGHT adds Af to every channel */
    CHECK(axisWhy(&ch) == RD_GLOW_BLUR_WHY_COLOUR, "t4: a constant added");
    buildChain(k43, 20, 512, &ch);
    CHECK(axisWhy(&ch) == RD_GLOW_BLUR_WHY_NONE, "t4: the chain as recorded");
}

static void t1(void)
{
    uint32_t b = 0;
    CHECK(rd__lamps_parse("glowblur", &b) == 1 && b == RD_LAMPS_GLOWBLUR, "t1: glowblur");
    CHECK(rd__lamps_parse("pad+owed+wholepad+glowblur", &b) == 1 &&
              b == (RD_LAMPS_PAD | RD_LAMPS_OWED | RD_LAMPS_WHOLEPAD | RD_LAMPS_GLOWBLUR),
          "t1: pad+owed+wholepad+glowblur");
    CHECK(rd__lamps_parse("1", &b) == 1 && !(b & RD_LAMPS_GLOWBLUR), "t1: 1 leaves it out");
    CHECK(strcmp(rd__lamps_bits_text(RD_LAMPS_GLOWBLUR), "glowblur") == 0, "t1: the word: %s",
          rd__lamps_bits_text(RD_LAMPS_GLOWBLUR));
    const uint32_t d = rd__lamps_default_bits(1, false, true, true, true);
    CHECK(((d & RD_LAMPS_GLOWBLUR) != 0) == (RD_GLOW_BLUR_DEFAULT != 0),
          "t1: the default bits 0x%x against RD_GLOW_BLUR_DEFAULT %d", (unsigned)d,
          (int)RD_GLOW_BLUR_DEFAULT);
    for (int i = 0; i < RD_GLOW_BLUR_WHY_COUNT; i++) {
        CHECK(strcmp(rd__glow_blur_why_text(i), "unknown") != 0, "t1: reason %d has no text", i);
    }
}

int main(void)
{
    t1();
    t2("4:3", k43, 512);
    t2("848", k848, 848);
    t3("4:3", k43, 512, 1u);
    t3("848", k848, 848, 2u);
    t4();
    printf("rd_glow_blur_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
