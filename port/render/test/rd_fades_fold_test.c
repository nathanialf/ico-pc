/* rd_fades_fold_test.c: the fades fold (port/render/rd_fades_fold.c), CPU
 * only, without a device (rd__init_record_only):
 *
 *   step      gs_math.hlsli gs_fold_ch with r 0 is the GS's blend of the
 *             three sprites (gs_blend_reg_ch of ALPHA 0x44 and 0x64, C the
 *             factor, COLCLAMP either way) for every Cs, Cd and k 0..0x80,
 *             so a texel stepped at scale 1 is the texel the sprite stored
 *             under the exact blend; with r 64 the nearest level of the
 *             exact quotient; both inside 0..255
 *   applies   gs_fold_applies's truth table: the count, a bar step only on
 *             the bars' rows, the "no bar" words
 *   filter    above 1x the steps follow the filter: for two taps at every
 *             weight the stepped filter and the filtered steps (each rounded
 *             as the GPU rounds, the step's own rounding either way) are at
 *             most a level apart; the count of channels a level apart printed
 *   plan      rd__fades_fold_plan on recorded frames (rd_post as the game
 *             records it, list 11 then list 12's reduction):
 *               the fade alone: one sprite, one full step, its colour, level
 *               and alpha, the nearest level under the fixed-function blend,
 *               the floor under an exact one;
 *               the fade and the letterbox: three sprites, two steps, the
 *               bars one step with SCENE's rows 0..57 and from H - 57 on (the
 *               bars' quarter-pixel corners), its alpha 0x80 and FIX the
 *               level; at scale 2 the rows snapped as rd_replay.c snapAxis
 *               draws them (116, 2 H - 114) and the hardware sprite's way;
 *               the brightness, the fade and the letterbox: four sprites,
 *               three steps in that order;
 *               the keep sprite (textured) after the fade: no fold;
 *               a Z-testing sprite after the letterbox: it ends the run (no
 *               fold), and before the letterbox the letterbox folds;
 *               the effects depth on: the letterbox is drawn (the fade
 *               before it with it), a fade after it folds;
 *               the menus up (a textured UI sprite after the fade): no fold,
 *               and the deferred text's quads after the fade (the present
 *               draws them) leave it;
 *               a draw into SCENE in list 12 before the reduction, a frame
 *               with MSAA, a lerp factor above 0x80: no fold, each with its
 *               reason;
 *               the plan's sprite ordinals are those of the commands (the
 *               store plan's count of the replayed lists). */
#include <math.h>

#include "test/hlsl_shim.h"
#include "gs_math.hlsli"
#include "rd_replay.h"
#include "../../test/ico_check.h"

static void checkStep(void)
{
    long bad = 0, badRound = 0, n = 0;
    for (int f = 0; f < 2; f++) {
        const uint reg = f ? 0x64u : 0x44u; /* LERP_FIX, LERP_AS */
        for (uint k = 0; k <= 0x80; k++) {
            for (uint cs = 0; cs < 256; cs++) {
                const uint s = cs | ((255u - cs) << 8) | ((cs ^ 0x5Au) << 16) | (k << 24);
                for (uint cd = 0; cd < 256; cd++) {
                    for (uint ch = 0; ch < 3; ch++) {
                        const int c = (int)((s >> (8u * ch)) & 255u);
                        const int gs = gs_blend_reg_ch(reg, c, (int)cd, (int)k, 0, (int)k, 1u);
                        const int gw = gs_blend_reg_ch(reg, c, (int)cd, (int)k, 0, (int)k, 0u);
                        const uint got = gs_fold_ch(s, ch, cd, 0);
                        bad += (int)got != gs || (int)got != gw;
                        /* the nearest level of Cd + (Cs - Cd) k / 128, halves up */
                        const int q = (c - (int)cd) * (int)k;
                        const int near = (int)cd + (int)floor((double)q / 128.0 + 0.5);
                        const uint gotR = gs_fold_ch(s, ch, cd, 64);
                        badRound += (int)gotR != near || gotR > 255u;
                        n++;
                    }
                }
            }
        }
    }
    CHECK(bad == 0, "step: %ld of %ld channels not the GS's blend", bad, n);
    CHECK(badRound == 0, "step: %ld of %ld channels not the nearest level", badRound, n);
    printf("  step: %ld (register, k, Cs, Cd, channel) the GS's floor and the nearest level\n", n);
}

static void checkApplies(void)
{
    const uint bars = 58u | (455u << 16); /* rows 0..57 and 455.. */
    CHECK(!gs_fold_applies(0, 0u, bars, 10), "no step");
    CHECK(gs_fold_applies(0, 1u, bars, 200), "a full step on a middle row");
    CHECK(!gs_fold_applies(1, 1u, bars, 200), "step 1 of one");
    const uint meta = 2u | (1u << 5); /* two steps, the second the bars */
    CHECK(gs_fold_applies(0, meta, bars, 200) && gs_fold_applies(0, meta, bars, 0),
          "the full step everywhere");
    CHECK(gs_fold_applies(1, meta, bars, 0) && gs_fold_applies(1, meta, bars, 57),
          "the top bar's rows");
    CHECK(!gs_fold_applies(1, meta, bars, 58) && !gs_fold_applies(1, meta, bars, 454),
          "the rows between the bars");
    CHECK(gs_fold_applies(1, meta, bars, 455) && gs_fold_applies(1, meta, bars, 511),
          "the bottom bar's rows");
    CHECK(!gs_fold_applies(1, meta, 0xFFFF0000u, 0) && !gs_fold_applies(1, meta, 0xFFFF0000u, 511),
          "no bar");
    CHECK(gs_fold_applies(1, meta, 58u | 0xFFFF0000u, 3) &&
              !gs_fold_applies(1, meta, 58u | 0xFFFF0000u, 511),
          "the top bar alone");
}

/* above 1x: the stepped filter against the filtered steps */
static void checkFilter(void)
{
    long n = 0, one = 0, over = 0;
    uint32_t seed = 12345u;
    for (int it = 0; it < 200000; it++) {
        seed = seed * 1664525u + 1013904223u;
        const uint cs = seed >> 24, k = (seed >> 8) % 129u;
        seed = seed * 1664525u + 1013904223u;
        const uint x0 = seed >> 24, x1 = (seed >> 16) & 255u;
        const double w = (double)((seed >> 4) & 255u) / 256.0;
        const uint s = cs | (k << 24);
        for (int r = 0; r <= 64; r += 64) {
            const double a0 = gs_fold_ch(s, 0, x0, r), a1 = gs_fold_ch(s, 0, x1, r);
            const int twoStep = (int)floor((1.0 - w) * a0 + w * a1 + 0.5);
            const uint filtered = (uint)floor((1.0 - w) * x0 + w * x1 + 0.5);
            const int fold = (int)gs_fold_ch(s, 0, filtered, r);
            const int d = twoStep > fold ? twoStep - fold : fold - twoStep;
            one += d == 1;
            over += d > 1;
            n++;
        }
    }
    CHECK(over == 0, "filter: %ld of %ld channels more than a level apart", over, n);
    printf("  filter: %ld (Cs, k, two taps, weight, rounding): %ld a level apart, %ld more\n", n,
           one, over);
}

/* ------------------------------------------------------------- the plan */

/* what a recorded frame's list 11 holds, in this order: the brightness,
 * the letterbox (before the fade), an untextured sprite under a Z test, the
 * fade, the keep sprite, a textured UI sprite, a text quad, the letterbox,
 * an untextured sprite under a Z test; and a draw in list 12 */
enum {
    F_BRIGHT = 1,
    F_FADE = 2,
    F_LETTER = 4,
    F_KEEP = 8,
    F_ZTEST = 16,
    F_MENU = 32,
    F_LETTER_FIRST = 64,
    F_TEXT = 128,
    F_LIST12 = 256,
    F_ZTEST_BEFORE = 512
};

static const uint8_t kFade[4] = {0, 0, 0, 0x60};

static void untexturedSprite(uint64_t test)
{
    static const uint8_t c[4] = {255, 0, 0, 0x40};
    rd_test_gs(test);
    rd_blend(RD_BLEND_LERP_AS, 0, 1);
    rd_texture_off();
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].x = 0x8000 - 256 * 16;
    v[0].y = 0x8000 - 256 * 16;
    v[1].x = 0x8000 + 256 * 16;
    v[1].y = 0x8000 + 256 * 16;
    memcpy(v[0].rgba, c, 4);
    memcpy(v[1].rgba, c, 4);
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_FULLFRAME, 1, 0);
}

static void texturedSprite(int textQuads)
{
    static const uint8_t c[4] = {128, 128, 128, 128};
    rd_texture(rd_target_texture(rd_target(RD_TARGET_WORK0), RD_VIEW_RGBA), RD_TEXFN_MODULATE,
               RD_TCC_RGBA);
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].x = 0x8000 - 16 * 16;
    v[0].y = 0x8000 - 16 * 16;
    v[1].x = 0x8000 + 16 * 16;
    v[1].y = 0x8000 + 16 * 16;
    v[1].s = v[1].t = 16.0f;
    v[0].q = v[1].q = 1.0f;
    memcpy(v[0].rgba, c, 4);
    memcpy(v[1].rgba, c, 4);
    if (textQuads) {
        rd_deferred_text_quads(1);
    }
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 1, 0);
    if (textQuads) {
        rd_deferred_text_quads(0);
    }
}

/* a frame as GsBase.c's post passes record it (list 11, then list 12's
 * reduction) */
static const RdFrame *record(int what, uint8_t fadeLevel)
{
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512,
                  RD_TARGET_OFFSET);
    static const uint8_t grey[4] = {60, 70, 80, 0x80};
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    rd_select_list(11);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    if (what & F_BRIGHT) {
        p.dst = rd_target(RD_TARGET_SCENE);
        p.rgba[3] = 9;
        rd_post(RD_POST_BRIGHTNESS, &p);
        memset(&p, 0, sizeof(p));
    }
    if (what & F_LETTER_FIRST) {
        p.fix = 0x80;
        rd_post(RD_POST_LETTERBOX, &p);
        memset(&p, 0, sizeof(p));
    }
    if (what & F_ZTEST_BEFORE) {
        untexturedSprite(RD_TEST_Z_GEQUAL);
    }
    if (what & F_FADE) {
        memcpy(p.rgba, kFade, 4);
        p.rgba[3] = fadeLevel;
        rd_post(RD_POST_FADE, &p);
        memset(&p, 0, sizeof(p));
    }
    if (what & F_KEEP) {
        p.dst = rd_target(RD_TARGET_SCENE);
        rd_post(RD_POST_KEEP, &p);
        memset(&p, 0, sizeof(p));
    }
    if (what & F_MENU) {
        texturedSprite(0);
    }
    if (what & F_TEXT) {
        texturedSprite(1);
    }
    if (what & F_LETTER) {
        p.fix = 0x40;
        rd_post(RD_POST_LETTERBOX, &p);
        memset(&p, 0, sizeof(p));
    }
    if (what & F_ZTEST) {
        untexturedSprite(RD_TEST_Z_GEQUAL);
    }
    rd_select_list(12);
    if (what & F_LIST12) {
        untexturedSprite(RD_TEST_Z_ALWAYS);
    }
    static const uint8_t tint[4] = {0x80, 0x80, 0x80, 0x80};
    memcpy(p.rgba, tint, 4);
    rd_post(RD_POST_REDUCTION, &p);
    rd_end_frame(0);
    return rd__last_frame();
}

/* the ordinals of list 11's screen commands of f, from the frame's first
 * list, in out (at most max); their count */
static uint32_t screenOrds(const RdFrame *f, uint32_t *out, uint32_t max)
{
    uint32_t n = 0, ord = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++, ord++) {
            if (l == 11 && f->lists[l].cmds[i].type == RDC_SCREEN && n < max) {
                out[n++] = ord;
            }
        }
    }
    return n;
}

static const char *plan(int what, uint8_t level, const RdFadesFoldEnv *env, RdFadesFold *out,
                        const RdFrame **frame)
{
    const RdFrame *f = record(what, level);
    if (frame) {
        *frame = f;
    }
    return rd__fades_fold_plan(f, 0, env, out);
}

static void checkPlan(void)
{
    RdFadesFoldEnv env;
    memset(&env, 0, sizeof(env));
    RdFadesFold p;
    const RdFrame *f = NULL;
    uint32_t ords[16];
    const uint32_t H = 512;

    /* the fade alone */
    const char *why = plan(F_FADE, 0x60, &env, &p, &f);
    CHECK(why == NULL && p.on, "the fade: %s", why ? why : "");
    CHECK(p.sprites == 1 && p.n == 1 && (p.meta & 7u) == 1 && (p.meta & 8u) && !(p.meta & 0xF0u),
          "the fade: %u sprites, %u steps, meta 0x%x", p.sprites, p.n, p.meta);
    CHECK(p.steps[0] == (0x60u << 24) && (p.alphas & 255u) == 0x60 && p.bars == 0xFFFF0000u,
          "the fade's step 0x%08x alpha 0x%x bars 0x%x", p.steps[0], p.alphas, p.bars);
    CHECK(screenOrds(f, ords, 16) == 1 && p.ords[0] == ords[0], "the fade's ordinal");
    CHECK(!p.scaled, "the fade at scale 1: the GS sprite model");
    env.exact = true;
    why = plan(F_FADE, 0x60, &env, &p, NULL);
    CHECK(why == NULL && !(p.meta & 8u), "the fade under the exact blend: the floor");
    env.exact = false;

    /* the fade and the letterbox */
    why = plan(F_FADE | F_LETTER, 0x7F, &env, &p, &f);
    CHECK(why == NULL && p.sprites == 3 && p.n == 2, "fade + letterbox: %s, %u sprites, %u steps",
          why ? why : "folded", p.sprites, p.n);
    CHECK(p.sides[0] == 0 && p.sides[1] == 3 && (p.meta & 0xF0u) == 0x20u,
          "fade + letterbox: the bars one step (sides %u %u, meta 0x%x)", p.sides[0], p.sides[1],
          p.meta);
    CHECK(p.bars == (58u | ((H - 57u) << 16)), "fade + letterbox: bars 0x%08x", p.bars);
    CHECK(p.steps[1] == (0x40u << 24) && ((p.alphas >> 8) & 255u) == 0x80,
          "the bars' step 0x%08x alpha 0x%x", p.steps[1], (p.alphas >> 8) & 255u);
    const uint32_t ns = screenOrds(f, ords, 16);
    CHECK(ns == 3 && p.ords[0] == ords[0] && p.ords[1] == ords[1] && p.ords[2] == ords[2],
          "fade + letterbox: the ordinals");

    /* at scale 2: the bars' rows as snapAxis draws them, the hardware sprite */
    RdTargetRec *sc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    RdTargetRec *dc = rd__target_rec(rd_target(RD_TARGET_DISPLAY).id);
    if (sc && dc) {
        const RdTargetRec s0 = *sc, d0 = *dc;
        sc->sx = sc->sy = 2.0f;
        sc->tw = 2 * sc->w;
        sc->th = 2 * sc->h;
        dc->sx = dc->sy = 2.0f;
        dc->tw = 2 * dc->w;
        dc->th = 2 * dc->h;
        why = plan(F_FADE | F_LETTER, 0x7F, &env, &p, NULL);
        CHECK(why == NULL && p.scaled, "fade + letterbox at 2x: %s, scaled %d",
              why ? why : "folded", p.scaled);
        CHECK(p.bars == (116u | ((2u * H - 114u) << 16)), "fade + letterbox at 2x: bars 0x%08x",
              p.bars);
        *sc = s0;
        *dc = d0;
    } else {
        CHECK(0, "no SCENE or DISPLAY record");
    }

    /* the brightness, the fade and the letterbox: the game's order */
    why = plan(F_BRIGHT | F_FADE | F_LETTER, 0x20, &env, &p, NULL);
    CHECK(why == NULL && p.sprites == 4 && p.n == 3, "brightness + fade + letterbox: %s, %u, %u",
          why ? why : "folded", p.sprites, p.n);
    CHECK(p.steps[0] == (0xFFFFFFu | (9u << 24)) && p.steps[1] == (0x20u << 24) && p.sides[2] == 3,
          "brightness + fade + letterbox: the steps in order (0x%08x 0x%08x)", p.steps[0],
          p.steps[1]);

    /* the keep sprite after the fade */
    why = plan(F_FADE | F_KEEP, 0x60, &env, &p, NULL);
    CHECK(why != NULL && !p.on, "keep after the fade: folded");
    /* the menus up */
    why = plan(F_FADE | F_MENU, 0x60, &env, &p, NULL);
    CHECK(why != NULL && !p.on, "a menu after the fade: folded");
    /* the deferred text's quads after the fade: the present draws them */
    env.deferText = true;
    why = plan(F_FADE | F_TEXT, 0x60, &env, &p, NULL);
    CHECK(why == NULL && p.sprites == 1, "deferred text after the fade: %s", why ? why : "");
    env.deferText = false;
    why = plan(F_FADE | F_TEXT, 0x60, &env, &p, NULL);
    CHECK(why != NULL, "text quads drawn into SCENE after the fade: folded");

    /* a Z-testing sprite after the letterbox ends the run */
    why = plan(F_FADE | F_LETTER | F_ZTEST, 0x60, &env, &p, NULL);
    CHECK(why != NULL && !p.on && why[0], "a Z test after the letterbox: %s", why ? why : "folded");
    /* before the letterbox: the letterbox folds */
    why = plan(F_ZTEST_BEFORE | F_LETTER, 0x60, &env, &p, NULL);
    CHECK(why == NULL && p.sprites == 2 && p.n == 1, "a Z test before the letterbox: %s",
          why ? why : "folded");

    /* the effects depth: the letterbox drawn, a fade after it folds */
    env.effectsDepth = true;
    why = plan(F_FADE | F_LETTER, 0x60, &env, &p, NULL);
    CHECK(why != NULL && strstr(why, "effects depth"), "the effects depth: %s",
          why ? why : "folded");
    why = plan(F_LETTER_FIRST | F_FADE, 0x60, &env, &p, NULL);
    CHECK(why == NULL && p.sprites == 1 && p.n == 1 && !(p.meta & 0xF0u),
          "the effects depth, the fade after the bars: %s", why ? why : "folded");
    env.effectsDepth = false;
    env.depthCarried = true;
    why = plan(F_FADE | F_LETTER, 0x60, &env, &p, NULL);
    CHECK(why != NULL, "SCENE's depth read by a later frame: the bars folded");
    env.depthCarried = false;

    /* list 12, MSAA, k above 0x80 */
    why = plan(F_FADE | F_LIST12, 0x60, &env, &p, NULL);
    CHECK(why != NULL && strstr(why, "list 12"), "a list 12 draw: %s", why ? why : "folded");
    env.msaa = true;
    why = plan(F_FADE, 0x60, &env, &p, NULL);
    CHECK(why != NULL && strstr(why, "MSAA"), "MSAA: %s", why ? why : "folded");
    env.msaa = false;
    why = plan(F_FADE, 0xC0, &env, &p, NULL);
    CHECK(why != NULL && strstr(why, "0x80"), "k 0xC0: %s", why ? why : "folded");
    printf("  plan: the recorded frames\n");
}

int main(void)
{
    checkStep();
    checkApplies();
    checkFilter();
    if (!rd__init_record_only(512, 512)) {
        printf("rd_fades_fold_test: no context\n");
        return 1;
    }
    checkPlan();
    rd_shutdown();
    return ico_check_summary("rd_fades_fold_test");
}
