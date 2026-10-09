/* gs_math_test.c: gs_math.hlsli, compiled as C, against the formulas of
 * port/test/gs_blend_test.c (gs_blend_int, gs_tfx_int) and the GS rules
 * re-expressed here (TEXA, alpha test, Z mapping). Tables are exhaustive
 * where the domain is small and dense elsewhere. */
#include "hlsl_shim.h"
#include "gs_math.hlsli"
#include "shader_consts.h"
#include "../../render/rd_state.h"
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

    /* Z (package QUEEN: the depth grows with Z): 0 -> 0, 0xFFFFFF -> just
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

    if (failures) {
        printf("gs_math_test: %d failures\n", failures);
        return 1;
    }
    printf("gs_math_test: ok\n");
    return 0;
}
