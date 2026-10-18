/* rd_fx_frame_test.c: the frame-size post sprites' plan
 * (port/render/rd_fx_plan.c), CPU only, against gs_math.hlsli (the
 * shaders' arithmetic, compiled as C):
 *
 *   fold      rd__fx_fold over every blend mode, FIX in 0, 1, 0x40, 0x7F,
 *             0x80, 0xFF, COLCLAMP 0 and 1, PABE off and on and a set of
 *             RGBAQ: each channel it calls the destination is
 *             gs_dst_channel's Cd for every Cd and Ad, each it calls a
 *             constant is that value for every Cd and Ad, and it refuses
 *             only a sprite with a channel that is neither; the forest's
 *             cleanUpFB sprites (Cs * As + Cd, RGBAQ 0, 0, 0, 0 and
 *             0, 0, 0, 192) and the glow's WORK0 fill (Cs * FIX + Cd, FIX
 *             0) fold to the destination on RGB, the motion blur's LERP
 *             FIX 32 does not fold
 *   premul    fx_sprite.hlsl fx_premul's |(A - B) C >> 7|, at most 255,
 *             added to Cd by the pipeline's ONE/ONE and taken from it by
 *             the reverse subtract on a UNORM8 target (clamped at 255 and
 *             at 0), is gs_blend_reg_ch's COLCLAMP 1 result for every Cs, C
 *             and Cd of the four forms (0x68, 0x48, 0x62, 0x42)
 *   keys      rd__fx_enumerate adds nothing with the plan off and, without
 *             the frame and local modules, the constant fold's six keys
 *             (fx_sprite_ps, the colour mask A, every depth state of
 *             rd__enumerate_reachable_blur)
 *   device    without a device the plan draws nothing (rd__fx_plan_draw
 *             false) */
#include "test/hlsl_shim.h"
#include "gs_math.hlsli"
#include "rd_replay.h"
#include "../../test/ico_check.h"

static const uint8_t kBlends[] = {
    RD_BLEND_CS_FIX_ADD_CD, RD_BLEND_CD_SUB_CS_FIX, RD_BLEND_LERP_FIX,     RD_BLEND_CD_FIX_ADD_CS,
    RD_BLEND_LERP_AS,       RD_BLEND_CS_AS_ADD_CD,  RD_BLEND_CD_SUB_CS_AS, RD_BLEND_LERP_AS_ALT,
    RD_BLEND_CS_AD_ADD_CD,  RD_BLEND_CD_SUB_CS_AD,  RD_BLEND_LERP_AD,      RD_BLEND_CD_AS_ADD_CD,
};

static const uint8_t kRgba[][4] = {
    {0, 0, 0, 0},      {0, 0, 0, 192},        {0, 0, 0, 0x80},
    {0, 0, 0, 0x7F},   {255, 128, 1, 0x7F},   {10, 20, 30, 0xFF},
    {1, 0, 255, 0x40}, {128, 128, 128, 0x80}, {0, 77, 0, 0x01},
};

/* the channel of a sprite of RGBAQ rgba over Cd, Ad (gs_dst_channel, the
 * write mask whole, no FBA) */
static uint channelOf(const RdDrawState *d, const uint8_t rgba[4], uint ch, uint cd, uint ad)
{
    return gs_dst_channel(ch, rgba[ch], cd, rgba[3], ad, rd__alpha_register(d->blend), d->blendFix,
                          d->colclamp, false, d->pabe != 0, 0xFu);
}

static void checkFold(void)
{
    static const uint8_t kFix[] = {0, 1, 0x40, 0x7F, 0x80, 0xFF};
    int folds = 0, refusals = 0, identities = 0, constants = 0;
    for (size_t b = 0; b < sizeof(kBlends); b++) {
        for (size_t f = 0; f < sizeof(kFix); f++) {
            for (int cl = 0; cl < 2; cl++) {
                for (int pabe = 0; pabe < 2; pabe++) {
                    for (size_t c = 0; c < sizeof(kRgba) / sizeof(kRgba[0]); c++) {
                        RdDrawState d;
                        memset(&d, 0, sizeof(d));
                        d.abe = 1;
                        d.blend = kBlends[b];
                        d.blendFix = kFix[f];
                        d.colclamp = (uint8_t)cl;
                        d.pabe = (uint8_t)pabe;
                        uint8_t ident = 0xFF, rgb[3] = {0, 0, 0};
                        const bool ok = rd__fx_fold(&d, kRgba[c], &ident, rgb);
                        bool someNeither = false;
                        for (uint ch = 0; ch < 3; ch++) {
                            bool isId = true, isConst = true;
                            const uint k0 = channelOf(&d, kRgba[c], ch, 0, 0);
                            const uint reg = rd__alpha_register(d.blend);
                            const uint adN = ((reg >> 4) & 3u) == 1u ? 256u : 1u; /* C = Ad */
                            for (uint ad = 0; ad < adN; ad++) {
                                for (uint cd = 0; cd < 256; cd++) {
                                    const uint v = channelOf(&d, kRgba[c], ch, cd, ad);
                                    isId = isId && v == cd;
                                    isConst = isConst && v == k0;
                                }
                            }
                            someNeither = someNeither || (!isId && !isConst);
                            if (ok && (ident >> ch) & 1u) {
                                CHECK(isId,
                                      "blend %u FIX %u clamp %d pabe %d rgba %u: ch %u is "
                                      "not the destination",
                                      kBlends[b], kFix[f], cl, pabe, (unsigned)c, ch);
                                identities++;
                            } else if (ok) {
                                CHECK(isConst && rgb[ch] == k0,
                                      "blend %u FIX %u clamp %d pabe %d rgba %u: ch %u is not "
                                      "the constant %u",
                                      kBlends[b], kFix[f], cl, pabe, (unsigned)c, ch, rgb[ch]);
                                constants++;
                            }
                        }
                        CHECK(ok == !someNeither,
                              "blend %u FIX %u clamp %d pabe %d rgba %u: fold %d, a channel "
                              "neither %d",
                              kBlends[b], kFix[f], cl, pabe, (unsigned)c, ok, someNeither);
                        folds += ok;
                        refusals += !ok;
                    }
                }
            }
        }
    }
    /* the forest's sprites (rd-00720 list 7) */
    RdDrawState d;
    memset(&d, 0, sizeof(d));
    d.abe = 1;
    d.colclamp = 1;
    uint8_t ident = 0, rgb[3];
    d.blend = RD_BLEND_CS_AS_ADD_CD;
    CHECK(rd__fx_fold(&d, kRgba[0], &ident, rgb) && ident == 7, "cleanUpFB (0, 0, 0, 0)");
    CHECK(rd__fx_fold(&d, kRgba[1], &ident, rgb) && ident == 7, "cleanUpFB (0, 0, 0, 192)");
    d.blend = RD_BLEND_CS_FIX_ADD_CD;
    d.blendFix = 0;
    CHECK(rd__fx_fold(&d, kRgba[2], &ident, rgb) && ident == 7, "the WORK0 fill");
    d.blend = RD_BLEND_LERP_FIX;
    d.blendFix = 32;
    CHECK(!rd__fx_fold(&d, kRgba[7], &ident, rgb), "the motion blur's LERP FIX 32 folds");
    printf("  fold: %d sprites fold (%d channels the destination, %d constant), %d refused\n",
           folds, identities, constants, refusals);
}

/* fx_sprite.hlsl fx_premul for one channel */
static int premulOut(uint reg, int cs, int as, int fix)
{
    const int c = gs_blend_factor((reg >> 4) & 3u, as, 0, fix);
    const int v =
        ((gs_blend_term(reg & 3u, cs, 0) - gs_blend_term((reg >> 2) & 3u, cs, 0)) * c) >> 7;
    const int m = v < 0 ? -v : v;
    return m < 255 ? m : 255;
}

static void checkPremul(void)
{
    static const struct {
        uint8_t blend;
        int sub;
    } kForms[4] = {{RD_BLEND_CS_FIX_ADD_CD, 0},
                   {RD_BLEND_CS_AS_ADD_CD, 0},
                   {RD_BLEND_CD_SUB_CS_FIX, 1},
                   {RD_BLEND_CD_SUB_CS_AS, 1}};

    long n = 0, bad = 0;
    for (int f = 0; f < 4; f++) {
        const uint reg = rd__alpha_register(kForms[f].blend);
        const bool fixForm = ((reg >> 4) & 3u) == 2u;
        for (int cs = 0; cs < 256; cs++) {
            for (int c = 0; c < 256; c++) {
                const int as = fixForm ? 0x55 : c, fix = fixForm ? c : 0x33;
                const int o = premulOut(reg, cs, as, fix);
                for (int cd = 0; cd < 256; cd++) {
                    const int hw =
                        kForms[f].sub ? (cd - o < 0 ? 0 : cd - o) : (cd + o > 255 ? 255 : cd + o);
                    const int gs = gs_blend_reg_ch(reg, cs, cd, as, 0, fix, 1u);
                    bad += hw != gs;
                    n++;
                }
            }
        }
    }
    CHECK(bad == 0, "premul: %ld of %ld differ", bad, n);
    printf("  premul: %ld (form, Cs, C, Cd) through the fixed-function add or subtract, %ld "
           "differ\n",
           n, bad);
}

static void checkKeys(void)
{
    static RdPipeKeyInt keys[64];
    g_rd.profile.fxPlan = false;
    CHECK(rd__fx_enumerate(keys, 64, 0) == 0, "keys with the plan off");
    g_rd.profile.fxPlan = true;
    const uint32_t n = rd__fx_enumerate(keys, 64, 0);
    CHECK(n == 6, "keys without the frame and local modules: %u", n);
    for (uint32_t i = 0; i < n; i++) {
        CHECK(keys[i].fs == RD_FS_FX_SPRITE && keys[i].gs.colorMask == 8, "key %u", i);
    }
    Replay r;
    memset(&r, 0, sizeof(r));
    RdFxDraw x;
    memset(&x, 0, sizeof(x));
    CHECK(!rd__fx_plan_draw(&r, &x), "drawn without a device");
    g_rd.profile.fxPlan = false;
}

int main(void)
{
    checkFold();
    checkPremul();
    checkKeys();
    return ico_check_summary("rd_fx_frame_test");
}
