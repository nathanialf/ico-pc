/* rd_alpha_plan_test.c: the alpha/blend planner (port/render/rd_alpha_plan.c),
 * CPU only.
 *
 *   off       with RdProfile.alphaPlan off the range of a draw is [0, 255]
 *             and every plan stays as rd__plan_screen_draw made it
 *   truth     every ATST x AREF x As range lo..hi (8 x 256 x 32896), for
 *             AFAIL KEEP and for the FB_ONLY split with Z write (the
 *             forest's state), against gs_alpha_pass (gs_math.hlsli) over
 *             every As of the range: a test that passes for all of them
 *             leaves the passing pass without the test and drops the
 *             failing one, a test that fails for all the other way round,
 *             any other range leaves the plan as it is
 *   afail     every ATST x 18 AREF x 171 ranges (both from the same 18
 *             values around the references) under KEEP, FB_ONLY, ZB_ONLY
 *             and RGB_ONLY with and without Z write and the alpha merge
 *             off: for every As of the range the planned passes that do not
 *             discard the pixel are the same passes in the same order as
 *             before, and no pass keeps a test the range decides
 *   blend     the blend identities over every Cs and Cd (0..255): the GS's
 *             ((A - B) * C >> 7) + D (gs_blend_reg_ch) is Cs for the LERPs
 *             with C 0x80, Cd for the LERPs and the premultiplied add and
 *             subtract with C 0, Cs under PABE with the MSB of As clear
 *             (gs_dst_channel); the fixed-function blender's Cs * 1 + Cd *
 *             0 and Cs * 0 + Cd * 1 give Cs and Cd; the premultiplied
 *             shader term (Cs * 0) >> 7 is 0
 *   model     ABE off and every blend mode x PABE x FBA x Z write x
 *             COLCLAMP x FIX (0, 0x40, 0x80, 0xFF) x four tests x ten
 *             ranges x the dynamic blend state on and off: the pixel the
 *             planned passes store (a model of the fixed-function path:
 *             the shader's outputs, the blend state's equation, the write
 *             mask, the Z write) is the pixel the plan before stores, for
 *             every As of the range and four Cs/Cd pairs; blending is
 *             turned off only for a LERP, R, G and B masked only with the
 *             dynamic blend state, neither under COLCLAMP 0; nothing
 *             changes under DATE, PRIM.AA1, the text shaders, the exact
 *             blend (blend rules) or COLCLAMP 0 (blend rules)
 *   range     rd__alpha_range_of_draw: screen prims' vertex alpha (one
 *             value exact, a span widened by one), TCC RGB, a texture not
 *             known (MODULATE, DECAL); a prelit mesh of alpha 0x80 is
 *             [0x80, 0x80] and plans the forest's state in one pass
 *             without the test and the blending (counted one-pass and
 *             opaque), a lit mesh's alpha clamped at 255, a NaN alpha
 *             decides nothing, the cluster's debug code 24 scales by
 *             mem[2].w (both floats around an inexact product), the
 *             per-replay memo; a grid's batch colours and a particle
 *             batch's alphas from the payload
 *   texscan   rdtex_alpha_range over RGBA32, RGB24 and RGBA16 sources under
 *             the three TEXA modes: the range holds every texel's
 *             expanded alpha (rdtex_apply_texa) and every two- and
 *             three-texel blend in 16ths (TEXA after and before the
 *             filter), and is exact on the textures the forest draws */
#include <math.h>
#include <stdlib.h>
#include "test/hlsl_shim.h"
#include "gs_math.hlsli"
#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_tex.h"
#include "shader_consts.h"
#include "../../test/ico_check.h"

/* ------------------------------------------------------------- helpers */

static bool passEq(const RdDrawPass *a, const RdDrawPass *b)
{
    return memcmp(&a->key, &b->key, sizeof(a->key)) == 0 && a->flags == b->flags &&
           a->modeZ == b->modeZ && a->aref == b->aref && a->fix == b->fix && a->fixGs == b->fixGs &&
           a->dstWant == b->dstWant && a->csOnly == b->csOnly;
}

static bool planEq(const RdDrawPass *a, int na, const RdDrawPass *b, int nb)
{
    if (na != nb) {
        return false;
    }
    for (int i = 0; i < na; i++) {
        if (!passEq(&a[i], &b[i])) {
            return false;
        }
    }
    return true;
}

static int planOf(const RdStateBlock *s, RdDrawPass out[2])
{
    memset(out, 0, 2 * sizeof(*out)); /* a pass the plan leaves out compares as zero */
    return rd__plan_screen_draw(s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                RHI_FMT_D32F_S8, out);
}

/* the state: TEST word, Z write, blend (-1 off) */
static RdStateBlock stateOf(uint64_t test, int zwrite, int blend)
{
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(test);
    s.ds.zwrite = zwrite ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
    s.ds.abe = blend >= 0;
    s.ds.blend = (uint8_t)(blend >= 0 ? blend : RD_BLEND_LERP_AS);
    return s;
}

/* whether pass p discards a pixel of alpha a: gs_alpha_discard (common.hlsli) */
static bool discards(const RdDrawPass *p, uint32_t a)
{
    if ((p->modeZ & (1u << 8)) == 0) {
        return false;
    }
    const bool pass = gs_alpha_pass(p->modeZ & 0xFFu, p->aref, a);
    const uint32_t split = (p->modeZ >> 16) & 3u;
    return split == 2u ? pass : !pass;
}

/* ------------------------------------------------------------------ off */

static void testOff(void)
{
    g_rd.profile.alphaPlan = false;
    RdStateBlock s = stateOf(RD_TEST_AT_GT64_FBONLY, 1, RD_BLEND_LERP_AS);
    RdDrawPass dp[2], before[2];
    int n = planOf(&s, dp);
    memcpy(before, dp, sizeof(dp));
    const int n0 = n;
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].rgba[3] = v[1].rgba[3] = 0x80;
    RdAlphaRange as = {7, 9};
    rd__alpha_range_of_draw(&s, &(RdAlphaSrc){.kind = RD_ALPHA_SRC_SCREEN, .v = v, .n = 2}, &as);
    CHECK(as.lo == 0 && as.hi == 255, "off: range %u..%u", as.lo, as.hi);
    CHECK(!rd__plan_screen_draw_alpha(&s, (RdAlphaRange){0x80, 0x80}, dp, &n));
    CHECK(planEq(dp, n, before, n0), "off: the plan changed");
    g_rd.profile.alphaPlan = true;
}

/* ---------------------------------------------------------------- truth */

static void testTruth(void)
{
    long bad = 0, calls = 0, decided = 0;
    for (int shape = 0; shape < 2; shape++) {
        for (uint32_t atst = 0; atst < 8; atst++) {
            for (uint32_t aref = 0; aref < 256; aref++) {
                /* KEEP: ATE, Z GEQUAL; the split: AFAIL FB_ONLY with Z write */
                const uint64_t test = 1u | (atst << 1) | (aref << 4) |
                                      ((uint64_t)(shape ? RD_AFAIL_FB_ONLY : RD_AFAIL_KEEP) << 12) |
                                      0x50000u;
                RdStateBlock s = stateOf(test, 1, -1);
                RdDrawPass plan[2], base[2], failPass;
                const int n0 = planOf(&s, plan);
                RdStateBlock s0 = s;
                s0.ds.test.ate = 0;
                planOf(&s0, base);
                failPass = plan[n0 > 1 ? 1 : 0];
                failPass.modeZ = 0;
                int prefix[257];
                prefix[0] = 0;
                for (uint32_t a = 0; a < 256; a++) {
                    prefix[a + 1] = prefix[a] + (gs_alpha_pass(atst, aref, a) ? 1 : 0);
                }
                for (uint32_t lo = 0; lo < 256; lo++) {
                    for (uint32_t hi = lo; hi < 256; hi++) {
                        const int passing = prefix[hi + 1] - prefix[lo];
                        const bool all = passing == (int)(hi - lo + 1), none = passing == 0;
                        RdDrawPass dp[2] = {plan[0], plan[1]};
                        int n = n0;
                        const bool ch =
                            rd__plan_screen_draw_alpha(&s, (RdAlphaRange){lo, hi}, dp, &n);
                        calls++;
                        bool ok;
                        if (atst == RD_ATST_ALWAYS || (shape && atst == RD_ATST_NEVER)) {
                            /* no test in the plan: ATE off, or the fail pass alone */
                            ok = !ch && planEq(dp, n, plan, n0);
                        } else if (all) {
                            ok = ch && planEq(dp, n, base, 1);
                        } else if (none) {
                            ok = ch && (shape ? planEq(dp, n, &failPass, 1) : n == 0);
                        } else {
                            ok = !ch && planEq(dp, n, plan, n0);
                        }
                        decided += ch;
                        if (!ok && bad++ < 8) {
                            fprintf(stderr,
                                    "truth: %s ATST %u AREF 0x%02X As 0x%02X..0x%02X: changed %d, "
                                    "%d passes of %d\n",
                                    shape ? "FB_ONLY split" : "KEEP", atst, aref, lo, hi, ch, n,
                                    n0);
                        }
                    }
                }
            }
        }
    }
    printf("rd_alpha_plan_test: truth: %ld plans, %ld re-planned, %ld wrong\n", calls, decided,
           bad);
    CHECK(bad == 0, "truth: %ld plans wrong", bad);
}

/* ---------------------------------------------------------------- afail */

/* the ranges of the sweeps: the ends from a list around the references */
static const uint8_t kEnds[] = {0x00, 0x01, 0x3F, 0x40, 0x41, 0x5F, 0x60, 0x61, 0x7E,
                                0x7F, 0x80, 0x81, 0x82, 0xBF, 0xC0, 0xFE, 0xFF, 0x20};

static void testAfail(void)
{
    static const struct {
        uint32_t afail;
        int zwrite, merge;
    } kShapes[] = {{RD_AFAIL_KEEP, 1, 1},    {RD_AFAIL_KEEP, 0, 1},     {RD_AFAIL_FB_ONLY, 1, 1},
                   {RD_AFAIL_FB_ONLY, 0, 1}, {RD_AFAIL_FB_ONLY, 0, 0},  {RD_AFAIL_ZB_ONLY, 1, 1},
                   {RD_AFAIL_ZB_ONLY, 0, 1}, {RD_AFAIL_RGB_ONLY, 1, 1}, {RD_AFAIL_RGB_ONLY, 0, 1},
                   {RD_AFAIL_RGB_ONLY, 0, 0}};

    const int ne = (int)(sizeof(kEnds) / sizeof(kEnds[0]));
    long bad = 0, plans = 0;
    for (size_t k = 0; k < sizeof(kShapes) / sizeof(kShapes[0]); k++) {
        rd__set_alpha_merge(kShapes[k].merge != 0);
        for (uint32_t atst = 0; atst < 8; atst++) {
            for (int r = 0; r < ne; r++) {
                const uint32_t aref = kEnds[r];
                const uint64_t test =
                    1u | (atst << 1) | (aref << 4) | ((uint64_t)kShapes[k].afail << 12) | 0x50000u;
                RdStateBlock s = stateOf(test, kShapes[k].zwrite, -1);
                RdDrawPass plan[2];
                const int n0 = planOf(&s, plan);
                for (int i = 0; i < ne; i++) {
                    for (int j = 0; j < ne; j++) {
                        if (kEnds[i] > kEnds[j]) {
                            continue;
                        }
                        const uint32_t lo = kEnds[i], hi = kEnds[j];
                        RdDrawPass dp[2] = {plan[0], plan[1]};
                        int n = n0;
                        rd__plan_screen_draw_alpha(&s, (RdAlphaRange){(uint8_t)lo, (uint8_t)hi}, dp,
                                                   &n);
                        plans++;
                        bool ok = n >= 0 && n <= n0;
                        int passing = 0;
                        for (uint32_t a = lo; a <= hi; a++) {
                            passing += gs_alpha_pass(atst, aref, a) ? 1 : 0;
                            /* the passes that draw the pixel, before and after */
                            RdDrawPass w0[2], w1[2];
                            int m0 = 0, m1 = 0;
                            for (int q = 0; q < n0; q++) {
                                if (!discards(&plan[q], a)) {
                                    w0[m0] = plan[q];
                                    w0[m0++].modeZ = 0;
                                }
                            }
                            for (int q = 0; q < n && ok; q++) {
                                if (!discards(&dp[q], a)) {
                                    w1[m1] = dp[q];
                                    w1[m1++].modeZ = 0;
                                }
                            }
                            ok = ok && planEq(w0, m0, w1, m1);
                        }
                        const bool decided = passing == 0 || passing == (int)(hi - lo + 1);
                        for (int q = 0; q < n && decided; q++) {
                            ok = ok && (dp[q].modeZ & (1u << 8)) == 0;
                        }
                        if (!ok && bad++ < 8) {
                            fprintf(stderr,
                                    "afail: AFAIL %u Z write %d merge %d ATST %u AREF 0x%02X As "
                                    "0x%02X..0x%02X: %d passes of %d\n",
                                    kShapes[k].afail, kShapes[k].zwrite, kShapes[k].merge, atst,
                                    aref, lo, hi, n, n0);
                        }
                    }
                }
            }
        }
    }
    rd__set_alpha_merge(true);
    printf("rd_alpha_plan_test: afail: %ld plans, %ld wrong\n", plans, bad);
    CHECK(bad == 0, "afail: %ld plans wrong", bad);
}

/* ---------------------------------------------------------------- blend */

/* the GS register of each blend mode (rd_pipeline.c rd__alpha_register) */
static void testBlendIdentities(void)
{
    long bad = 0;
    for (int cs = 0; cs < 256; cs++) {
        for (int cd = 0; cd < 256; cd++) {
            for (int ad = 0; ad < 256; ad += 51) {
                /* LERP As and FIX (0x44, 0x64) with C 0x80: Cs; with C 0: Cd */
                bad += gs_blend_reg_ch(0x44u, cs, cd, 0x80, ad, 0, 1u) != cs;
                bad += gs_blend_reg_ch(0x64u, cs, cd, 0, ad, 0x80, 1u) != cs;
                bad += gs_blend_reg_ch(0x44u, cs, cd, 0, ad, 0, 1u) != cd;
                bad += gs_blend_reg_ch(0x64u, cs, cd, 0, ad, 0, 1u) != cd;
                /* Cs*As + Cd, Cs*FIX + Cd (0x48, 0x68), Cd - Cs*As, Cd -
                 * Cs*FIX (0x42, 0x62) with C 0: Cd */
                bad += gs_blend_reg_ch(0x48u, cs, cd, 0, ad, 0, 1u) != cd;
                bad += gs_blend_reg_ch(0x68u, cs, cd, 0, ad, 0, 1u) != cd;
                bad += gs_blend_reg_ch(0x42u, cs, cd, 0, ad, 0, 1u) != cd;
                bad += gs_blend_reg_ch(0x62u, cs, cd, 0, ad, 0, 1u) != cd;
                /* PABE with the MSB clear: Cs whatever the equation */
                for (uint32_t as = 0; as < 0x80u; as += 0x1Fu) {
                    bad += gs_dst_channel(0u, (uint)cs, (uint)cd, as, (uint)ad, 0x44u, 0x80u, 1u,
                                          false, true, 0xFu) != (uint)cs;
                    bad += gs_dst_channel(1u, (uint)cs, (uint)cd, as, (uint)ad, 0x64u, 0x40u, 1u,
                                          false, true, 0xFu) != (uint)cs;
                }
                /* alpha is As whatever the equation */
                bad += gs_dst_channel(3u, (uint)cs, (uint)cd, (uint)cs, (uint)ad, 0x44u, 0x80u, 1u,
                                      false, false, 0xFu) != (uint)cs;
            }
            /* the fixed-function blender (UNORM8: k / 255 in, nearest out):
             * factor 1 gives Cs, 0 gives Cd, as blending off gives Cs */
            const float s = (float)cs * (1.0f / 255.0f), d = (float)cd * (1.0f / 255.0f);
            const float one = s * 1.0f + d * (1.0f - 1.0f), zero = s * 0.0f + d * (1.0f - 0.0f);
            bad += (int)floorf(one * 255.0f + 0.5f) != cs;
            bad += (int)floorf(zero * 255.0f + 0.5f) != cd;
            /* the premultiplied shader term at factor 0 (gs_dual_out) */
            bad += (int)min(((uint)cs * 0u) >> 7, 255u) != 0;
        }
    }
    printf("rd_alpha_plan_test: blend identities: %ld wrong\n", bad);
    CHECK(bad == 0, "blend identities: %ld wrong", bad);
}

/* What one pass p stores over the pixel px (RGBA) for a fragment of colour
 * cs and alpha as: the port's fixed-function path (gs_dual_out's outputs,
 * rd_pipeline.c blendState's equation in float rounded to nearest, the
 * write mask); *z set when it writes Z.  A model for comparing two plans,
 * not the GS. */
static void modelPass(const RdDrawPass *p, const uint8_t cs[3], uint32_t as, uint8_t px[4], bool *z)
{
    const uint32_t fl = p->flags;
    const bool unblended = (fl & ICO_DF_PABE) && (as & 0x80u) == 0;
    const uint32_t f = (fl & ICO_DF_FIX_FACTOR) ? p->fix : as;
    const uint32_t aOut = (fl & ICO_DF_FBA) ? ((as | 0x80u) & 255u) : as;
    const RdBlendPath bp = rd__blend_path(p->key.gs.blend);
    const uint8_t mask = p->key.gs.colorMask;
    uint8_t o[4];
    for (int ch = 0; ch < 3; ch++) {
        const float s = (float)cs[ch], d = (float)px[ch];
        float c1 = (float)f / 128.0f;
        c1 = c1 > 1.0f ? 1.0f : c1;
        uint32_t c0 = cs[ch];
        if ((fl & ICO_DF_PREMUL) && !unblended) {
            c0 = min((cs[ch] * f) >> 7, 255u);
        }
        float v;
        switch (bp) {
        case RD_BP_NONE:
            v = s;
            break;
        case RD_BP_LERP:
            v = unblended ? s : s * c1 + d * (1.0f - c1);
            break;
        case RD_BP_PREMUL_ADD:
            v = (float)c0 + d;
            break;
        case RD_BP_PREMUL_REVSUB:
            v = d - (float)c0;
            break;
        case RD_BP_DST_FIX:
            v = s + d * (unblended ? 0.0f : c1);
            break;
        case RD_BP_AD_ADD:
            v = s * ((float)px[3] / 255.0f) + d;
            break;
        case RD_BP_AD_REVSUB:
            v = d - s * ((float)px[3] / 255.0f);
            break;
        case RD_BP_AD_LERP:
            v = s * ((float)px[3] / 255.0f) + d * (1.0f - (float)px[3] / 255.0f);
            break;
        default:
            v = d;
            break;
        }
        v = v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
        o[ch] = (uint8_t)floorf(v + 0.5f);
    }
    o[3] = (uint8_t)aOut;
    for (int ch = 0; ch < 4; ch++) {
        if (mask & (1u << ch)) {
            px[ch] = o[ch];
        }
    }
    *z = *z || p->key.gs.zwrite == RD_ZWRITE_ON;
}

/* the pixel a plan stores (every pass that does not discard the fragment,
 * in order) and whether it writes Z */
static void modelPlan(const RdDrawPass *p, int n, const uint8_t cs[3], uint32_t as,
                      const uint8_t cd[4], uint8_t px[4], bool *z)
{
    memcpy(px, cd, 4);
    *z = false;
    for (int i = 0; i < n; i++) {
        if (!discards(&p[i], as)) {
            modelPass(&p[i], cs, as, px, z);
        }
    }
}

static void testModel(void)
{
    static const uint64_t kTests[] = {RD_TEST_Z_GEQUAL, 0x5160Du /* ATE GREATER 0x60 FB_ONLY */,
                                      0x5080Bu /* ATE GEQUAL 0x80 KEEP */,
                                      0x53815u /* ATE LESS 0x81 RGB_ONLY */};
    static const uint8_t kRanges[][2] = {{0, 0},       {0x7F, 0x7F}, {0x80, 0x80}, {0x81, 0x81},
                                         {0, 0x7F},    {0x80, 0xFF}, {0x40, 0x90}, {0x61, 0x80},
                                         {0x00, 0xFF}, {0x60, 0x60}};
    static const uint8_t kFix[] = {0, 0x40, 0x80, 0xFF};
    static const uint8_t kCs[4][3] = {{0, 0, 0}, {255, 255, 255}, {100, 37, 200}, {128, 1, 254}};
    static const uint8_t kCd[4][4] = {
        {255, 255, 255, 0x11}, {0, 0, 0, 0x90}, {37, 100, 1, 0x80}, {128, 254, 0, 0xFF}};
    long bad = 0, plans = 0, off = 0, masked = 0, misuse = 0;
    for (int dyn = 0; dyn < 2; dyn++) {
        g_rd.dynBlend = dyn != 0;
        for (int blend = -1; blend < RD_BLEND_COUNT; blend++) {
            for (int bits = 0; bits < 16; bits++) {
                for (size_t t = 0; t < sizeof(kTests) / sizeof(kTests[0]); t++) {
                    for (size_t fi = 0; fi < sizeof(kFix); fi++) {
                        RdStateBlock s = stateOf(kTests[t], (bits & 4) == 0, blend);
                        s.ds.pabe = (uint8_t)(bits & 1);
                        s.ds.fba = (uint8_t)((bits >> 1) & 1);
                        s.ds.colclamp = (uint8_t)((bits & 8) == 0);
                        s.ds.blendFix = kFix[fi];
                        RdDrawPass plan[2];
                        const int n0 = planOf(&s, plan);
                        for (size_t r = 0; r < sizeof(kRanges) / sizeof(kRanges[0]); r++) {
                            const uint8_t lo = kRanges[r][0], hi = kRanges[r][1];
                            RdDrawPass dp[2] = {plan[0], plan[1]};
                            int n = n0;
                            rd__plan_screen_draw_alpha(&s, (RdAlphaRange){lo, hi}, dp, &n);
                            plans++;
                            bool ok = true;
                            /* every pass of a plan has the same blend */
                            const RdBlendPath was = rd__blend_path(plan[0].key.gs.blend);
                            for (int q = 0; q < n; q++) {
                                const bool nowOff =
                                    was != RD_BP_NONE && dp[q].key.gs.blend == RD_BLEND_COUNT;
                                const bool rgbOff = nowOff && (dp[q].key.gs.colorMask & 7u) == 0;
                                off += nowOff && !rgbOff;
                                masked += rgbOff;
                                /* blending off for a LERP only, the masks for the
                                 * LERPs and the premultiplied forms with the
                                 * dynamic blend state only, neither with COLCLAMP 0 */
                                if (nowOff && (!s.ds.colclamp || (!rgbOff && was != RD_BP_LERP) ||
                                               (rgbOff && !dyn))) {
                                    ok = false;
                                }
                            }
                            misuse += !ok;
                            for (uint32_t a = lo; a <= hi && ok; a++) {
                                for (int c = 0; c < 4 && ok; c++) {
                                    uint8_t p0[4], p1[4];
                                    bool z0, z1;
                                    modelPlan(plan, n0, kCs[c], a, kCd[c], p0, &z0);
                                    modelPlan(dp, n, kCs[c], a, kCd[c], p1, &z1);
                                    ok = memcmp(p0, p1, 4) == 0 && z0 == z1;
                                }
                            }
                            if (!ok && bad++ < 8) {
                                fprintf(stderr,
                                        "model: blend %d PABE %d FBA %d COLCLAMP %u FIX 0x%02X "
                                        "TEST 0x%llX Z write %d As 0x%02X..0x%02X dynamic %d: "
                                        "%d passes of %d\n",
                                        blend, bits & 1, (bits >> 1) & 1, s.ds.colclamp, kFix[fi],
                                        (unsigned long long)kTests[t], (bits & 4) == 0, lo, hi, dyn,
                                        n, n0);
                            }
                        }
                    }
                }
            }
        }
    }
    g_rd.dynBlend = false;
    printf("rd_alpha_plan_test: model: %ld plans, %ld passes blended no more, %ld masked to "
           "alpha, %ld wrong (%ld rule misuses)\n",
           plans, off, masked, bad, misuse);
    CHECK(bad == 0, "model: %ld plans store other pixels", bad);
    CHECK(off > 0 && masked > 0, "model: the blend rules never applied (%ld, %ld)", off, masked);

    /* not planned: DATE, PRIM.AA1, the text shaders; the blend rules not
     * under the exact blend */
    RdStateBlock s = stateOf(0x5160Du, 1, RD_BLEND_LERP_FIX);
    s.ds.test.date = RD_DATE_DEST_ALPHA_1;
    RdDrawPass dp[2];
    int n = planOf(&s, dp);
    CHECK(!rd__plan_screen_draw_alpha(&s, (RdAlphaRange){0x80, 0x80}, dp, &n), "DATE planned");
    s = stateOf(0x5160Du, 1, RD_BLEND_LERP_FIX);
    n = planOf(&s, dp);
    dp[0].key.gs.aa1 = 1;
    CHECK(!rd__plan_screen_draw_alpha(&s, (RdAlphaRange){0x80, 0x80}, dp, &n), "AA1 planned");
    n = planOf(&s, dp);
    dp[1].key.fs = RD_FS_FONT;
    CHECK(!rd__plan_screen_draw_alpha(&s, (RdAlphaRange){0x80, 0x80}, dp, &n), "font planned");
    g_rd.dstMode = RD_BLEND_EXACT_STORAGE;
    n = planOf(&s, dp);
    CHECK(rd__plan_screen_draw_alpha(&s, (RdAlphaRange){0x80, 0x80}, dp, &n) && n == 1 &&
              dp[0].key.gs.blend != RD_BLEND_COUNT && (dp[0].modeZ & (1u << 8)) == 0,
          "exact blend: the test planned, the blend kept (%d passes)", n);
    g_rd.dstMode = RD_BLEND_EXACT_OFF;
}

/* ---------------------------------------------------------------- range */

static uint8_t s_payload[4096];
static RdFrame s_frame;

/* a VU command whose payload is p (and, for a grid or particles, the
 * stream qw of nq quadwords after its VU block) */
static RdCmd vuCmd(uint8_t type, uint32_t mesh, const RdVuPayload *p, const float (*qw)[4],
                   uint32_t nq)
{
    memset(s_payload, 0, sizeof(s_payload));
    memcpy(s_payload, p, sizeof(*p));
    if (qw) {
        memcpy(s_payload + sizeof(*p) + sizeof(RdVuBlock), qw, (size_t)nq * 16u);
    }
    s_frame.payload = s_payload;
    RdCmd c;
    memset(&c, 0, sizeof(c));
    c.type = type;
    c.u[0] = mesh;
    c.u[1] = 0;
    c.u[2] = (uint32_t)(sizeof(*p) + sizeof(RdVuBlock) + (size_t)nq * 16u);
    return c;
}

static RdAlphaRange meshRangeFaded(const RdStateBlock *s, const RdMeshRec *m, uint8_t prog,
                                   uint8_t code, uint32_t id, float fade)
{
    RdVuPayload p;
    memset(&p, 0, sizeof(p));
    p.prog = prog;
    p.code = code;
    const uint8_t type = prog >= RD_PROG_SKIN && prog <= RD_PROG_SKIN_SPEC ? RDC_SKINNED : RDC_MESH;
    const RdCmd c = vuCmd(type, id, &p, NULL, 0);
    /* mem[2].w: the cluster's fade */
    memcpy(s_payload + sizeof(p) + 2u * 16u + 3u * 4u, &fade, sizeof(fade));
    RdAlphaRange as = {7, 9};
    rd__alpha_range_of_draw(s,
                            &(RdAlphaSrc){.kind = RD_ALPHA_SRC_VU,
                                          .prog = prog,
                                          .cmdType = type,
                                          .f = &s_frame,
                                          .c = &c,
                                          .mesh = m},
                            &as);
    return as;
}

static RdAlphaRange meshRange(const RdStateBlock *s, const RdMeshRec *m, uint8_t prog, uint8_t code,
                              uint32_t id)
{
    return meshRangeFaded(s, m, prog, code, id, 1.0f);
}

static void testRange(void)
{
    /* screen prims */
    RdStateBlock s = stateOf(0x5160Du, 1, RD_BLEND_LERP_AS);
    RdScreenVtx v[3];
    memset(v, 0, sizeof(v));
    v[0].rgba[3] = v[1].rgba[3] = v[2].rgba[3] = 0x80;
    RdAlphaRange as;
    const RdAlphaSrc scr = {.kind = RD_ALPHA_SRC_SCREEN, .v = v, .n = 3};
    rd__alpha_range_of_draw(&s, &scr, &as);
    CHECK(as.lo == 0x80 && as.hi == 0x80, "screen: one alpha: %02X..%02X", as.lo, as.hi);
    v[1].rgba[3] = 0x40;
    v[2].rgba[3] = 0xFF;
    rd__alpha_range_of_draw(&s, &scr, &as);
    CHECK(as.lo == 0x3F && as.hi == 0xFF, "screen: a span widened: %02X..%02X", as.lo, as.hi);
    v[1].rgba[3] = 0;
    rd__alpha_range_of_draw(&s, &scr, &as);
    CHECK(as.lo == 0 && as.hi == 0xFF, "screen: a span from 0: %02X..%02X", as.lo, as.hi);
    v[1].rgba[3] = v[2].rgba[3] = 0x80;
    s.ds.texEnabled = 1;
    s.tex = 12345; /* no such texture: not known */
    s.ds.tcc = RD_TCC_RGB;
    rd__alpha_range_of_draw(&s, &scr, &as);
    CHECK(as.lo == 0x80 && as.hi == 0x80, "screen: TCC RGB: %02X..%02X", as.lo, as.hi);
    s.ds.tcc = RD_TCC_RGBA;
    rd__alpha_range_of_draw(&s, &scr, &as);
    CHECK(as.lo == 0 && as.hi == 0xFF, "screen: MODULATE, texture not known: %02X..%02X", as.lo,
          as.hi);
    v[0].rgba[3] = v[1].rgba[3] = v[2].rgba[3] = 0x20;
    rd__alpha_range_of_draw(&s, &scr, &as);
    CHECK(as.lo == 0 && as.hi == 0x3F, "screen: MODULATE 0x20, texture not known: %02X..%02X",
          as.lo, as.hi);
    s.ds.texFn = RD_TEXFN_DECAL;
    rd__alpha_range_of_draw(&s, &scr, &as);
    CHECK(as.lo == 0 && as.hi == 0xFF, "screen: DECAL, texture not known: %02X..%02X", as.lo,
          as.hi);

    /* a prelit mesh of alpha 0x80: the forest's state in one pass,
     * without the test and the blending */
    s = stateOf(0x5160Du, 1, RD_BLEND_LERP_AS);

    enum { NV = 6 };

    float stream[NV * 5][4];
    memset(stream, 0, sizeof(stream));
    for (int i = 0; i < NV; i++) {
        stream[i * 3 + 2][3] = 128.0f;
    }
    RdMeshRec m;
    memset(&m, 0, sizeof(m));
    m.vu = 1;
    m.vertexCount = NV;
    m.qwPerVertex = RD_VU_QW_PRELIT;
    m.stream = stream;
    g_rd.replayCounter = 1;
    as = meshRange(&s, &m, RD_PROG_PRELIT, 32, 1);
    CHECK(as.lo == 0x80 && as.hi == 0x80, "prelit mesh 0x80: %02X..%02X", as.lo, as.hi);
    RdDrawPass dp[2];
    int n = planOf(&s, dp);
    const uint32_t one0 = g_rdPerf.alphaOnePass, opq0 = g_rdPerf.alphaOpaque;
    CHECK(n == 2, "the forest's state: %d passes", n);
    CHECK(rd__plan_screen_draw_alpha(&s, as, dp, &n) && n == 1 && (dp[0].modeZ & (1u << 8)) == 0 &&
              dp[0].key.gs.blend == RD_BLEND_COUNT && dp[0].key.gs.zwrite == RD_ZWRITE_ON,
          "prelit mesh 0x80: %d passes, mode %X, blend %u", n, dp[0].modeZ, dp[0].key.gs.blend);
    CHECK(g_rdPerf.alphaOnePass == one0 + 1 && g_rdPerf.alphaOpaque == opq0 + 1,
          "prelit mesh 0x80: counted %u one-pass, %u opaque", g_rdPerf.alphaOnePass - one0,
          g_rdPerf.alphaOpaque - opq0);
    /* the seam meshes' 127: one pass, blended */
    for (int i = 0; i < NV; i++) {
        stream[i * 3 + 2][3] = 127.0f;
    }
    as = meshRange(&s, &m, RD_PROG_PRELIT, 32, 1);
    CHECK(as.lo == 0x80, "the memo: a stream rewritten in the replay keeps its range");
    g_rd.replayCounter++;
    as = meshRange(&s, &m, RD_PROG_PRELIT, 32, 1);
    CHECK(as.lo == 0x7F && as.hi == 0x7F, "prelit mesh 127: %02X..%02X", as.lo, as.hi);
    n = planOf(&s, dp);
    CHECK(rd__plan_screen_draw_alpha(&s, as, dp, &n) && n == 1 &&
              dp[0].key.gs.blend != RD_BLEND_COUNT,
          "prelit mesh 127: %d passes", n);
    /* lit: alpha min 255 */
    m.qwPerVertex = RD_VU_QW_LIT;
    for (int i = 0; i < NV; i++) {
        stream[i * 4 + 3][3] = i ? 300.0f : 255.5f;
    }
    as = meshRange(&s, &m, RD_PROG_LIT, 32, 2);
    CHECK(as.lo == 0xFF && as.hi == 0xFF, "lit mesh 300: %02X..%02X", as.lo, as.hi);
    /* prelit as sent: ftoi0's low byte (300 is 0x2C, -1 is 0xFF) */
    m.qwPerVertex = RD_VU_QW_PRELIT;
    memset(stream, 0, sizeof(stream));
    for (int i = 0; i < NV; i++) {
        stream[i * 3 + 2][3] = i ? 300.0f : 300.9f;
    }
    as = meshRange(&s, &m, RD_PROG_PRELIT, 32, 3);
    CHECK(as.lo == 0x2C && as.hi == 0x2C, "prelit mesh 300: %02X..%02X", as.lo, as.hi);
    stream[2][3] = NAN;
    as = meshRange(&s, &m, RD_PROG_PRELIT, 32, 4);
    CHECK(as.lo == 0 && as.hi == 0xFF, "a NaN alpha: %02X..%02X", as.lo, as.hi);
    /* skinned: code 24 scales by mem[2].w */
    m.qwPerVertex = RD_VU_QW_SKIN;
    memset(stream, 0, sizeof(stream));
    for (int i = 0; i < NV; i++) {
        stream[i * 5 + 4][3] = 128.0f;
    }
    as = meshRange(&s, &m, RD_PROG_SKIN_SPEC, 22, 5);
    CHECK(as.lo == 0x80 && as.hi == 0x80, "skinned 22: %02X..%02X", as.lo, as.hi);
    as = meshRange(&s, &m, RD_PROG_SKIN_SPEC, 24, 5);
    CHECK(as.lo == 0x80 && as.hi == 0x80, "skinned 24, fade 1: %02X..%02X", as.lo, as.hi);
    as = meshRangeFaded(&s, &m, RD_PROG_SKIN_SPEC, 24, 5, 0.5f);
    CHECK(as.lo == 0x40 && as.hi == 0x40, "skinned 24, fade 0.5: %02X..%02X", as.lo, as.hi);
    /* 100 * 0.01f is 0.99999997...: the device's float is 1 or the one below */
    for (int i = 0; i < NV; i++) {
        stream[i * 5 + 4][3] = 100.0f;
    }
    as = meshRangeFaded(&s, &m, RD_PROG_SKIN_SPEC, 24, 5, 0.01f);
    CHECK(as.lo == 0 && as.hi == 2, "skinned 24, an inexact product: %02X..%02X", as.lo, as.hi);
    as = meshRangeFaded(&s, &m, RD_PROG_SKIN_SPEC, 24, 5, INFINITY);
    CHECK(as.lo == 0 && as.hi == 0xFF, "skinned 24, an infinite fade: %02X..%02X", as.lo, as.hi);
    /* a layout the program does not read */
    as = meshRange(&s, &m, RD_PROG_PRELIT, 32, 6);
    CHECK(as.lo == 0 && as.hi == 0xFF, "prelit program on a skinned stream: %02X..%02X", as.lo,
          as.hi);

    /* a grid of two strips of three vertices (RD_VU_QW_GRID): per strip
     * the VIF qword, the tag, the colour, the vertices, MSCNT */
    float gq[2 * (3 + 3 * 2 + 1)][4];
    memset(gq, 0, sizeof(gq));
    gq[2][3] = 128.0f;
    gq[10 + 2][3] = 64.0f;
    RdVuPayload p;
    memset(&p, 0, sizeof(p));
    p.prog = RD_PROG_GRID;
    p.batchCount = 2;
    p.vertsPerBatch = 3;
    p.qwPerVertex = RD_VU_QW_GRID;
    p.streamQw = 20;
    RdCmd c = vuCmd(RDC_GRID, 0, &p, (const float (*)[4])gq, 20);
    rd__alpha_range_of_draw(&s,
                            &(RdAlphaSrc){.kind = RD_ALPHA_SRC_VU,
                                          .prog = RD_PROG_GRID,
                                          .cmdType = RDC_GRID,
                                          .f = &s_frame,
                                          .c = &c},
                            &as);
    CHECK(as.lo == 0x3F && as.hi == 0x81, "grid 0x80 and 0x40: %02X..%02X", as.lo, as.hi);
    /* particles: (x, y, z, size), (u, v, grey, alpha) after six header qwords */
    float pq[6 + 2 * 3][4];
    memset(pq, 0, sizeof(pq));
    pq[7][3] = pq[9][3] = pq[11][3] = 96.0f;
    memset(&p, 0, sizeof(p));
    p.prog = RD_PROG_PARTICLE;
    p.vertsPerBatch = 3;
    p.qwPerVertex = RD_VU_QW_PARTICLE;
    p.streamQw = 12;
    c = vuCmd(RDC_PARTICLES, 0, &p, (const float (*)[4])pq, 12);
    rd__alpha_range_of_draw(&s,
                            &(RdAlphaSrc){.kind = RD_ALPHA_SRC_VU,
                                          .prog = RD_PROG_PARTICLE,
                                          .cmdType = RDC_PARTICLES,
                                          .f = &s_frame,
                                          .c = &c},
                            &as);
    CHECK(as.lo == 0x60 && as.hi == 0x60, "particles 96: %02X..%02X", as.lo, as.hi);
}

/* -------------------------------------------------------------- texscan */

/* the alpha the shaders' lookups give for texels t[0..n) filtered with
 * weights w (in 16ths, summing to 16): TEXA after the filter (vu_ps:
 * gs_sample then gs_texa_expand) and before it (vu_texa_ps: each texel
 * expanded, then blended); into the span lo..hi */
static void filtered(const uint8_t (*t)[4], const int *w, int n, RdTexSrc src, int texa,
                     uint32_t *lo, uint32_t *hi)
{
    float f[4] = {0, 0, 0, 0};
    float e = 0.0f;
    for (int i = 0; i < n; i++) {
        for (int ch = 0; ch < 4; ch++) {
            f[ch] += (float)w[i] / 16.0f * ((float)t[i][ch] / 255.0f);
        }
        uint8_t x[4];
        memcpy(x, t[i], 4);
        rdtex_apply_texa(x, 1, src, (RdTexA)texa);
        e += (float)w[i] / 16.0f * (float)x[3];
    }
    uint8_t q[4];
    for (int ch = 0; ch < 4; ch++) {
        q[ch] = (uint8_t)floorf(f[ch] * 255.0f + 0.5f);
    }
    uint8_t after[4];
    memcpy(after, q, 4);
    rdtex_apply_texa(after, 1, src, (RdTexA)texa);
    const uint32_t a1 = after[3], a2 = (uint32_t)floorf(e + 0.5f);
    *lo = a1 < *lo ? a1 : *lo;
    *hi = a1 > *hi ? a1 : *hi;
    *lo = a2 < *lo ? a2 : *lo;
    *hi = a2 > *hi ? a2 : *hi;
}

static void testTexScan(void)
{
    static const RdTexSrc kSrc[3] = {RD_TEXSRC_RGBA32, RD_TEXSRC_RGB24, RD_TEXSRC_RGBA16};
    srand(7);
    long bad = 0, cases = 0;
    for (int k = 0; k < 3; k++) {
        for (int texa = 0; texa < RD_TEXA_COUNT; texa++) {
            for (int iter = 0; iter < 400; iter++) {
                enum { NT = 4 };

                uint8_t t[NT][4];
                for (int i = 0; i < NT; i++) {
                    for (int ch = 0; ch < 3; ch++) {
                        const int r = rand() % 4;
                        t[i][ch] = (uint8_t)(r == 0 ? 0 : r == 1 ? 1 : rand() % 256);
                    }
                    t[i][3] = (uint8_t)(kSrc[k] == RD_TEXSRC_RGBA16  ? rand() % 2
                                        : kSrc[k] == RD_TEXSRC_RGB24 ? 0
                                                                     : rand() % 256);
                }
                RdTexAlphaScan a;
                rdtex_alpha_scan_begin(&a);
                rdtex_alpha_scan(&a, &t[0][0], NT);
                uint8_t lo = 0, hi = 0;
                if (!rdtex_alpha_range(&a, kSrc[k], texa, &lo, &hi)) {
                    bad++;
                    continue;
                }
                uint32_t olo = 255, ohi = 0;
                for (int i = 0; i < NT; i++) {
                    const int w1[1] = {16};
                    filtered((const uint8_t (*)[4]) & t[i], w1, 1, kSrc[k], texa, &olo, &ohi);
                    for (int j = 0; j < NT; j++) {
                        for (int x = 1; x < 16; x++) {
                            const uint8_t pair[2][4] = {{t[i][0], t[i][1], t[i][2], t[i][3]},
                                                        {t[j][0], t[j][1], t[j][2], t[j][3]}};
                            const int w2[2] = {x, 16 - x};
                            filtered(pair, w2, 2, kSrc[k], texa, &olo, &ohi);
                        }
                        const int l = (i + j + 1) % NT;
                        const uint8_t tri[3][4] = {{t[i][0], t[i][1], t[i][2], t[i][3]},
                                                   {t[j][0], t[j][1], t[j][2], t[j][3]},
                                                   {t[l][0], t[l][1], t[l][2], t[l][3]}};
                        const int w3[3] = {5, 5, 6};
                        filtered(tri, w3, 3, kSrc[k], texa, &olo, &ohi);
                    }
                }
                cases++;
                if (olo < lo || ohi > hi) {
                    if (bad++ < 8) {
                        fprintf(stderr,
                                "texscan: source %d TEXA %d: range %02X..%02X, a lookup gives "
                                "%02X..%02X\n",
                                (int)kSrc[k], texa, lo, hi, olo, ohi);
                    }
                }
            }
        }
    }
    /* exact on the forest's textures: RGBA32 alpha 0x80, 0x71..0x80 */
    uint8_t t[4][4] = {{10, 20, 30, 0x80}, {0, 0, 0, 0x80}, {5, 5, 5, 0x71}, {9, 9, 9, 0x80}};
    RdTexAlphaScan a;
    uint8_t lo = 0, hi = 0;
    rdtex_alpha_scan_begin(&a);
    rdtex_alpha_scan(&a, &t[0][0], 1);
    CHECK(rdtex_alpha_range(&a, RD_TEXSRC_RGBA32, RD_TEXA_7F_81_AEM, &lo, &hi) && lo == 0x80 &&
              hi == 0x80,
          "texscan: RGBA32 0x80: %02X..%02X", lo, hi);
    rdtex_alpha_scan(&a, &t[1][0], 3);
    CHECK(rdtex_alpha_range(&a, RD_TEXSRC_RGBA32, RD_TEXA_80_80, &lo, &hi) && lo == 0x71 &&
              hi == 0x80,
          "texscan: RGBA32 0x71..0x80: %02X..%02X", lo, hi);
    /* RGB24: TA0, 0 under AEM where a texel can filter to black */
    CHECK(rdtex_alpha_range(&a, RD_TEXSRC_RGB24, RD_TEXA_80_80, &lo, &hi) && lo == 0x80 &&
              hi == 0x80,
          "texscan: RGB24 80/80: %02X..%02X", lo, hi);
    CHECK(rdtex_alpha_range(&a, RD_TEXSRC_RGB24, RD_TEXA_7F_81_AEM, &lo, &hi) && lo == 0 &&
              hi == 0x7F,
          "texscan: RGB24 with black, 7F/81 AEM: %02X..%02X", lo, hi);
    rdtex_alpha_scan_begin(&a);
    rdtex_alpha_scan(&a, &t[2][0], 2);
    CHECK(rdtex_alpha_range(&a, RD_TEXSRC_RGB24, RD_TEXA_7F_81_AEM, &lo, &hi) && lo == 0x7F &&
              hi == 0x7F,
          "texscan: RGB24 without black, 7F/81 AEM: %02X..%02X", lo, hi);
    rdtex_alpha_scan_begin(&a);
    CHECK(!rdtex_alpha_range(&a, RD_TEXSRC_RGBA32, RD_TEXA_80_80, &lo, &hi),
          "texscan: an empty scan decides");
    printf("rd_alpha_plan_test: texscan: %ld textures, %ld wrong\n", cases, bad);
    CHECK(bad == 0, "texscan: %ld textures wrong", bad);
}

int main(void)
{
    testOff();
    g_rd.profile.alphaPlan = true;
    g_rd.dstMode = RD_BLEND_EXACT_OFF;
    g_rd.dynBlend = false;
    testTruth();
    testAfail();
    testBlendIdentities();
    testModel();
    testRange();
    testTexScan();
    return ico_check_summary("rd_alpha_plan_test");
}
