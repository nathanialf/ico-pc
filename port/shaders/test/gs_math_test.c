/* gs_math_test.c: gs_math.hlsli, compiled as C, against the formulas of
 * port/test/gs_blend_test.c (gs_blend_int, gs_tfx_int) and the GS rules
 * re-expressed here (TEXA, alpha test, Z mapping), and the shaders' integer
 * forms against the plain operators they replace (gs_wrap_repeat against
 * the double modulo). Tables are exhaustive where the domain is small and
 * dense elsewhere. */
#include "hlsl_shim.h"
#include "gs_math.hlsli"
#include "shader_consts.h"
#include "../../render/rd_state.h"
#include <limits.h>
#include <stdio.h>

/* The constants gs_math.hlsli shares with C, pinned at compile time: the
 * texel formats (shader_consts.h), the alpha tests and the TEXA modes
 * (port/render/rd_state.h). */
_Static_assert(TEXFMT_RGBA32 == ICO_TEXFMT_RGBA32, "TEXFMT_RGBA32");
_Static_assert(TEXFMT_RGB24 == ICO_TEXFMT_RGB24, "TEXFMT_RGB24");
_Static_assert(TEXFMT_RGBA16 == ICO_TEXFMT_RGBA16, "TEXFMT_RGBA16");
_Static_assert(ATST_NEVER == RD_ATST_NEVER, "ATST_NEVER");
_Static_assert(ATST_ALWAYS == RD_ATST_ALWAYS, "ATST_ALWAYS");
_Static_assert(ATST_LESS == RD_ATST_LESS, "ATST_LESS");
_Static_assert(ATST_LEQUAL == RD_ATST_LEQUAL, "ATST_LEQUAL");
_Static_assert(ATST_EQUAL == RD_ATST_EQUAL, "ATST_EQUAL");
_Static_assert(ATST_GEQUAL == RD_ATST_GEQUAL, "ATST_GEQUAL");
_Static_assert(ATST_GREATER == RD_ATST_GREATER, "ATST_GREATER");
_Static_assert(ATST_NOTEQUAL == RD_ATST_NOTEQUAL, "ATST_NOTEQUAL");
_Static_assert(TEXA_80_80 == RD_TEXA_80_80, "TEXA_80_80");
_Static_assert(TEXA_7F_81_AEM == RD_TEXA_7F_81_AEM, "TEXA_7F_81_AEM");
_Static_assert(TEXA_80_80_AEM == RD_TEXA_80_80_AEM, "TEXA_80_80_AEM");

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            if (failures < 20) {                                                                   \
                printf("FAIL " __VA_ARGS__);                                                       \
                printf("\n");                                                                      \
            }                                                                                      \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* verbatim from port/test/gs_blend_test.c */
static int ref_blend_int(int a, int b, int c, int d, int colclamp)
{
    int v = (((a - b) * c) >> 7) + d;
    if (colclamp) {
        if (v < 0)
            v = 0;
        if (v > 255)
            v = 255;
    } else {
        v &= 0xFF;
    }
    return v;
}

static int ref_tfx_int(int tex, int col)
{
    int v = (tex * col) >> 7;
    return v > 255 ? 255 : v;
}

/* REPEAT's residue as the shaders wrote it before gs_wrap_repeat */
static int ref_wrap(int c, int n)
{
    return ((c % n) + n) % n;
}

/* gs_wrap_repeat(c, n) against ref_wrap for c in -span..span, the edges of
 * a period and the int range's ends */
static void check_wrap(int n, int span)
{
    for (int c = -span; c <= span; c++) {
        CHECK(gs_wrap_repeat(c, n) == ref_wrap(c, n), "wrap %d mod %d", c, n);
    }
    const int edge[] = {INT_MIN, INT_MIN + 1, INT_MAX - 1, INT_MAX, -n, n, -n - 1, n + 1};
    for (int i = 0; i < (int)(sizeof(edge) / sizeof(edge[0])); i++) {
        CHECK(gs_wrap_repeat(edge[i], n) == ref_wrap(edge[i], n), "wrap %d mod %d", edge[i], n);
    }
}

static int ref_atest(int atst, int aref, int a)
{
    switch (atst) {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return a < aref;
    case 3:
        return a <= aref;
    case 4:
        return a == aref;
    case 5:
        return a >= aref;
    case 6:
        return a > aref;
    default:
        return a != aref;
    }
}

int main(void)
{
    /* texture function: every (tex, col) pair */
    for (int t = 0; t < 256; t++) {
        for (int c = 0; c < 256; c++) {
            CHECK((int)gs_tfx_mod((uint)t, (uint)c) == ref_tfx_int(t, c), "tfx %d %d", t, c);
        }
    }

    /* blend: A, B, D in {Cs, Cd, 0} and C in {As, Ad, FIX} collapse to the
     * formula; check the channel form over a dense grid and both clamp
     * modes, then the register form for the twelve modes of rd_state.h. */
    static const int vals[] = {0, 1, 2, 31, 63, 64, 100, 127, 128, 129, 200, 254, 255};
    const int nv = (int)(sizeof(vals) / sizeof(vals[0]));
    for (int cl = 0; cl < 2; cl++) {
        for (int a = 0; a < nv; a++) {
            for (int b = 0; b < nv; b++) {
                for (int c = 0; c < 256; c += 3) {
                    for (int d = 0; d < nv; d++) {
                        int got = gs_blend_ch(vals[a], vals[b], c, vals[d], (uint)cl);
                        int want = ref_blend_int(vals[a], vals[b], c, vals[d], cl);
                        CHECK(got == want, "blend a=%d b=%d c=%d", vals[a], vals[b], c);
                    }
                }
            }
        }
    }
    /* the twelve ALPHA registers the game uses
     * (A | B<<2 | C<<4 | D<<6): the register form against the formula with
     * the terms picked by hand */
    static const uint regs[12] = {0x68, 0x62, 0x64, 0x61, 0x44, 0x48,
                                  0x42, 0x44, 0x58, 0x52, 0x54, 0x49};
    for (int m = 0; m < 12; m++) {
        uint reg = regs[m];
        for (int cs = 0; cs < 256; cs += 5) {
            for (int cd = 0; cd < 256; cd += 7) {
                for (int as = 0; as < 256; as += 11) {
                    for (int ad = 0; ad < 256; ad += 51) {
                        int fix = 0x80;
                        int sel[3] = {cs, cd, 0};
                        int A = sel[reg & 3], B = sel[(reg >> 2) & 3], D = sel[(reg >> 6) & 3];
                        int facs[3] = {as, ad, fix};
                        int C = facs[(reg >> 4) & 3];
                        int want = ref_blend_int(A, B, C, D, 1);
                        int got = gs_blend_reg_ch(reg, cs, cd, as, ad, fix, 1);
                        CHECK(got == want, "mode %d cs=%d cd=%d as=%d ad=%d", m, cs, cd, as, ad);
                    }
                }
            }
        }
    }
    /* named spot values: LERP_AS with As = 0x40 on 200 over 100:
     * ((200-100)*64 >> 7) + 100 = 150; REVERSE (mode 6): Cd - Cs*As */
    CHECK(gs_blend_reg_ch(0x44, 200, 100, 0x40, 0, 0, 1) == 150, "lerp spot");
    CHECK(gs_blend_reg_ch(0x48, 200, 100, 0x80, 0, 0, 1) == 255, "add clamps");
    CHECK(gs_blend_reg_ch(0x48, 200, 100, 0x80, 0, 0, 0) == ((300) & 255), "add wraps");
    CHECK(gs_blend_reg_ch(0x42, 200, 100, 0x80, 0, 0, 1) == 0, "sub clamps");
    CHECK(gs_blend_reg_ch(0x64, 0, 255, 0x70, 0, 0x70, 1) == 255 + (((0 - 255) * 0x70) >> 7),
          "floor toward -inf");

    /* the destination-read blend (gs_dst_channel): the forest's tint sprite
     * after the fog as PCSX2 2.8.2's software renderer draws it (the
     * godrays oracle, forest_trunk state 552, GS draw 480: SPRITE with ABE,
     * ALPHA 0x44 = (Cs - Cd) * As >> 7 + Cd, COLCLAMP 1, FBA 0, PABE 0,
     * FBMSK 0, RGBA 223 219 205 4 over the fogged SCENE), eight pixels of
     * its rt0 (before) and rt1 (after) dumps.  The floor formula gives
     * every pixel of the 512 x 512 draw (pass mean +4.98 +4.77 +4.68); the
     * fixed-function blend's round to nearest is one level up on half of
     * them (+5.51 +5.22 +5.19), the hw column below */
    {
        static const struct {
            int x, y;
            uint8_t dst[3], gs[3], hw[3];
        } tint[8] = {
            {1, 0, {15, 22, 8}, {21, 28, 14}, {22, 28, 14}},
            {427, 76, {12, 17, 14}, {18, 23, 19}, {19, 23, 20}},
            {500, 150, {37, 40, 31}, {42, 45, 36}, {43, 46, 36}},
            {256, 223, {66, 84, 38}, {70, 88, 43}, {71, 88, 43}},
            {236, 297, {23, 31, 16}, {29, 36, 21}, {29, 37, 22}},
            {385, 368, {9, 9, 9}, {15, 15, 15}, {16, 16, 15}},
            {13, 440, {79, 89, 56}, {83, 93, 60}, {84, 93, 61}},
            {511, 511, {25, 31, 24}, {31, 36, 29}, {31, 37, 30}},
        };

        static const uint src[4] = {223, 219, 205, 4};
        for (int i = 0; i < 8; i++) {
            int hwDiffers = 0;
            for (uint ch = 0; ch < 3; ch++) {
                const uint got = gs_dst_channel(ch, src[ch], tint[i].dst[ch], src[3], 0x80u, 0x44u,
                                                0x80u, 1u, false, false, 15u);
                CHECK(got == tint[i].gs[ch], "forest tint (%d,%d) channel %u: %u, PCSX2 %u",
                      tint[i].x, tint[i].y, ch, got, tint[i].gs[ch]);
                /* the float lerp the blender rounds: Cs * F + Cd * (1 - F) */
                const double f = 4.0 / 128.0;
                const int r = (int)(src[ch] * f + tint[i].dst[ch] * (1.0 - f) + 0.5);
                CHECK(r == tint[i].hw[ch], "forest tint (%d,%d) channel %u: round %d, table %u",
                      tint[i].x, tint[i].y, ch, r, tint[i].hw[ch]);
                hwDiffers |= r != (int)tint[i].gs[ch];
            }
            CHECK(hwDiffers, "forest tint (%d,%d): a pixel the blender rounds up", tint[i].x,
                  tint[i].y);
            /* the alpha written is As (FBA 0) */
            CHECK(gs_dst_channel(3u, src[3], 0x55u, src[3], 0x55u, 0x44u, 0x80u, 1u, false, false,
                                 15u) == 4u,
                  "forest tint alpha");
        }
    }
    /* the channel rules: a masked channel keeps Cd, FBA sets the stored
     * alpha's MSB, PABE writes Cs where As < 0x80, As above 0x80 overshoots
     * (no 1.0 clamp), COLCLAMP 0 wraps, the Ad and FIX factors */
    CHECK(gs_dst_channel(0u, 200, 100, 0x40, 0, 0x44, 0x80, 1, false, false, 14u) == 100u,
          "mask R");
    /* channel 3's Cd is Ad (common.hlsli passes dst.a for both) */
    CHECK(gs_dst_channel(3u, 0x40, 0x22, 0x40, 0x22, 0x44, 0x80, 1, false, false, 7u) == 0x22u,
          "mask A keeps Ad");
    CHECK(gs_dst_channel(3u, 0, 0, 0x40, 0, 0x44, 0x80, 1, true, false, 15u) == 0xC0u, "FBA");
    CHECK(gs_dst_channel(0u, 200, 100, 0x40, 0, 0x44, 0x80, 1, false, true, 15u) == 200u,
          "PABE below 0x80: Cs");
    CHECK(gs_dst_channel(0u, 200, 100, 0x90, 0, 0x44, 0x80, 1, false, true, 15u) ==
              (uint)ref_blend_int(200, 100, 0x90, 100, 1),
          "PABE MSB set: blended");
    CHECK(gs_dst_channel(0u, 200, 100, 0xFF, 0, 0x44, 0x80, 1, false, false, 15u) == 255u,
          "As 0xFF overshoots, clamped by COLCLAMP");
    CHECK(gs_dst_channel(0u, 150, 100, 0xC0, 0, 0x44, 0x80, 1, false, false, 15u) == 175u,
          "As 0xC0 overshoots past Cs");
    CHECK(gs_dst_channel(0u, 200, 100, 0x80, 0, 0x48, 0x80, 0, false, false, 15u) == 44u,
          "COLCLAMP 0 wraps");
    CHECK(gs_dst_channel(1u, 200, 100, 0x10, 0x40, 0x54, 0x80, 1, false, false, 15u) == 150u,
          "Ad lerp (mode 10)");
    CHECK(gs_dst_channel(2u, 200, 100, 0x10, 0, 0x64, 0x20, 1, false, false, 15u) == 125u,
          "FIX lerp (mode 2)");
    CHECK(gs_dst_channel(0u, 60, 100, 0x40, 0, 0x29, 0xC0, 1, false, false, 15u) == 210u,
          "Cd*FIX + Cs with FIX above 0x80 (mode 3)");

    /* alpha test: all eight modes over a grid */
    for (int atst = 0; atst < 8; atst++) {
        for (int aref = 0; aref < 256; aref += 1) {
            for (int a = 0; a < 256; a += 1) {
                CHECK((int)gs_alpha_pass((uint)atst, (uint)aref, (uint)a) ==
                          ref_atest(atst, aref, a),
                      "atest %d aref %d a %d", atst, aref, a);
            }
        }
    }

    /* TEXA: the three modes of rd_state.h */
    static const struct {
        int ta0, ta1, aem;
    } texa[3] = {{0x80, 0x80, 0}, {0x7F, 0x81, 1}, {0x80, 0x80, 1}};

    for (int m = 0; m < 3; m++) {
        for (int abit = 0; abit < 2; abit++) {
            for (int rgb = 0; rgb < 2; rgb++) { /* 0: black texel, 1: coloured */
                uint r = rgb ? 40u : 0u;
                int want24 = (texa[m].aem && !rgb) ? 0 : texa[m].ta0;
                int want16 = (texa[m].aem && !rgb) ? 0 : (abit ? texa[m].ta1 : texa[m].ta0);
                CHECK((int)gs_texa_alpha(r, 0, 0, (uint)abit, (uint)m, TEXFMT_RGB24) == want24,
                      "texa24 mode %d rgb %d", m, rgb);
                CHECK((int)gs_texa_alpha(r, 0, 0, (uint)abit, (uint)m, TEXFMT_RGBA16) == want16,
                      "texa16 mode %d abit %d rgb %d", m, abit, rgb);
            }
        }
    }

    /* Z (the depth grows with Z): 0 -> 0, 0xFFFFFF -> just
     * under 1, monotone, exact at the 2^-24 scale; PSMZ32 on a float depth
     * buffer (GS_ZSCALE_32F) z * 2^-33 below the top band, which is apart */
    const float s24 = 1.0f / 16777216.0f, s32f = (float)GS_ZSCALE_32F;
    CHECK(gs_z_to_depth(0u, s24) == 0.0f, "z far");
    CHECK(gs_z_to_depth(0xFFFFFFu, s24) == 1.0f - s24, "z near");
    CHECK(gs_z_to_depth(0x1000000u, s24) == 1.0f - s24, "z 2^24 clamps");
    CHECK(gs_z_to_depth(0x800000u, s24) == 0.5f, "z mid");
    float prev = -1.0f;
    for (uint z = 0; z < 0x1000000u; z += 4099u) {
        float d = gs_z_to_depth(z, s24);
        CHECK(d > prev, "z monotone at %u", z);
        CHECK(d == (float)z / 16777216.0f, "z exact at %u", z);
        prev = d;
    }
    CHECK(gs_z_to_depth(16999983u, s32f) < gs_z_to_depth(17000000u, s32f), "z32f 17 apart");
    CHECK(gs_z_to_depth(0xFFFFFF9Bu, s32f) < gs_z_to_depth(0xFFFFFFFFu, s32f), "z32f top apart");
    CHECK(gs_z_to_depth(0x7FFFFFFFu, s32f) < gs_z_to_depth(0xFFFF0000u, s32f), "z32f band");

    /* REPEAT: gs_wrap_repeat (the mask for a power of two, the double
     * modulo otherwise) against the double modulo, every n up to 4096 and
     * the powers of two to 2^30, c over three periods each way (4101 values
     * each way above 4096) and the int range's ends */
    for (int n = 1; n <= 4096; n++) {
        check_wrap(n, 3 * n + 5);
    }
    for (int k = 13; k <= 30; k++) {
        check_wrap(1 << k, 4101);
    }

    if (failures) {
        printf("gs_math_test: %d failures\n", failures);
        return 1;
    }
    printf("gs_math_test: ok\n");
    return 0;
}
