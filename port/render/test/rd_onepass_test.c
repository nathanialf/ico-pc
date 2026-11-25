/* rd_onepass_test.c: the one-pass blend's pure part (rd_onepass.c,
 * rd_onepass.h; issue 179), CPU only, and its run's state without a device
 * (rd_onepass_run.c).
 *   model    the stored alpha of the carry against the alpha pass's for
 *            every As 0..255 at every blender precision (8..16, 20, 24 bits
 *            fixed point, binary16, binary32 for the source, the constant
 *            and the product), rounded to nearest: the same byte for As
 *            0..128, 128 above (the source's clamp); the directed-rounding
 *            models that differ counted and printed (Vulkan does not pin
 *            the blender's rounding: the device check decides)
 *   proof    the planner's own pair (rd__expand_no_dual of a LERP As pass
 *            with RGBA in its mask) proven; each refusal alone: As 129,
 *            FBA, PABE, LERP FIX, Cd*FIX + Cs, Z GREATER with Z write,
 *            another target format, a destination read, two samples, a
 *            pass taken already, not a pair
 *   class    a plain draw, a cut and a kick drawn apart, a fused kick, the
 *            cut of a fused command; the classes from the arms' verdicts
 *            (C used only with A and B passed or nothing fused; A's failure
 *            puts nothing in use; C's failure leaves A's class)
 *   take     the run's choice (ICO_RD_ONEPASS=full, no device): a plain
 *            pair in one pass (the alpha pass dropped, the colour key with
 *            A and the bit), a fused kick and a fused command's cut kept,
 *            a cut drawn apart taken; =plain: the cut refused for its
 *            class; the check's arms: REF and B take nothing, A only the
 *            plain class, C both; without a device the check's pipelines
 *            are not made ("not made yet", counted for its retry); the
 *            unfuse under each arm; the 10 s clause and the stats line
 *   ways     the ways back without a device: held until the check, =0 off,
 *            the loss ladder and the install's mark off before =full, a
 *            background fault off for the run (the 10 s clause says so)
 *            and kept off through a renderer made again
 *   compose  the fast pixel build takes a one-pass key (rd_fastps.c), the
 *            device's dynamic blend state folds the bit away
 *            (rd__key_dynamic_normalise)
 *   names    every reason's and verdict's words, the judge's use */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_fastps.h"
#include "rd_internal.h"
#include "rd_nodiscard.h"
#include "rd_onepass.h"
#include "shader_consts.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void env(const char *name, const char *value)
{
#ifdef _WIN32
    (void)_putenv_s(name, value != NULL ? value : "");
#else
    if (value != NULL) {
        (void)setenv(name, value, 1);
    } else {
        (void)unsetenv(name);
    }
#endif
}

static void model(void)
{
    static const int kBits[] = {8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 24, 116, 132};
    const int nb = (int)(sizeof(kBits) / sizeof(kBits[0]));
    uint64_t cases = 0;
    int bad = 0, directed = 0;
    for (int as = 0; as <= 255; as++) {
        for (int i = 0; i < nb; i++) {
            for (int j = 0; j < nb; j++) {
                for (int k = 0; k < nb; k++) {
                    int two = 0;
                    const int one = rd_op_alpha_model(as, kBits[i], kBits[j], kBits[k], 0, 0, &two);
                    cases++;
                    if (as <= 128 ? one != two : one != 128) {
                        if (bad++ < 4) {
                            printf("  As %d bits %d %d %d: carry %d, alpha pass %d\n", as, kBits[i],
                                   kBits[j], kBits[k], one, two);
                        }
                    }
                }
            }
        }
    }
    CHECK(bad == 0);
    for (int i = 0; i < nb; i++) {
        for (int r = 1; r <= 2; r++) {
            for (int s = 0; s <= 2; s++) {
                bool differ = false;
                for (int as = 0; as <= 128 && !differ; as++) {
                    int two = 0;
                    differ = rd_op_alpha_model(as, kBits[i], kBits[i], kBits[i], r, s, &two) != two;
                }
                directed += differ ? 1 : 0;
            }
        }
    }
    printf("  model: %llu nearest cases, %d differ; %d of %d directed-rounding models differ "
           "somewhere in As 0..128 (the device check decides)\n",
           (unsigned long long)cases, bad, directed, nb * 6);
}

/* the planner's pair of a LERP As pass with RGBA in its mask (vu_ps's
   *_nodual build, Z GEQUAL with Z write) */
static int pairOf(RdDrawPass out[4], uint8_t blend, uint32_t flags)
{
    RdDrawPass p;
    memset(&p, 0, sizeof(p));
    p.key.fs = RD_FS_VU;
    p.key.gs.blend = blend;
    p.key.gs.colorMask = 0xF;
    p.key.gs.ztst = RD_ZTST_GEQUAL;
    p.key.gs.zwrite = RD_ZWRITE_ON;
    p.key.colorFmt = RHI_FMT_RGBA8_UNORM;
    p.key.dst = RHI_DST_READ_NONE;
    p.flags = flags;
    g_rd.noDual = true;
    return rd__expand_no_dual(&p, 1, out);
}

static void proof(void)
{
    RdDrawPass d[4];
    CHECK(pairOf(d, RD_BLEND_LERP_AS, 0) == 2);
    CHECK((d[0].flags & ICO_DF_NODUAL_FACTOR) && (d[1].flags & ICO_DF_NODUAL_ALPHA_PASS));
    CHECK(rd__no_dual_second(d, 1));
    CHECK(rd_op_why(&d[0], &d[1], 128, 0) == RD_OP_ONE);
    CHECK(rd_op_why(&d[0], &d[1], 0, 1) == RD_OP_ONE);
    CHECK(rd_op_why(&d[0], &d[1], 129, 0) == RD_OP_ALPHA);
    CHECK(rd_op_why(&d[0], &d[1], 128, 2) == RD_OP_OTHER);
    CHECK(rd_op_why(&d[1], &d[0], 64, 0) == RD_OP_OTHER); /* not a pair in this order */
    RdDrawPass e[4] = {d[0], d[1], d[0], d[1]};
    e[0].flags |= ICO_DF_FBA;
    CHECK(rd_op_why(&e[0], &e[1], 64, 0) == RD_OP_FBA);
    e[0] = d[0];
    e[0].flags |= ICO_DF_PABE;
    CHECK(rd_op_why(&e[0], &e[1], 64, 0) == RD_OP_PABE);
    CHECK(pairOf(e, RD_BLEND_LERP_FIX, ICO_DF_FIX_FACTOR) == 2);
    CHECK(rd_op_why(&e[0], &e[1], 64, 0) == RD_OP_FIX);
    CHECK(pairOf(e, RD_BLEND_CD_FIX_ADD_CS, 0) == 2);
    CHECK(rd_op_why(&e[0], &e[1], 64, 0) == RD_OP_OTHER);
    e[0] = d[0];
    e[1] = d[1];
    e[0].key.gs.ztst = RD_ZTST_GREATER;
    CHECK(rd_op_why(&e[0], &e[1], 64, 0) == RD_OP_OTHER);
    e[0] = d[0];
    e[0].key.colorFmt = RHI_FMT_RGBA8_UINT;
    CHECK(rd_op_why(&e[0], &e[1], 64, 0) == RD_OP_OTHER);
    e[0] = d[0];
    e[0].key.dst = RHI_DST_READ_STORAGE;
    CHECK(rd_op_why(&e[0], &e[1], 64, 0) == RD_OP_OTHER);
    e[0] = d[0];
    e[0].key.gs.nodual |= RD_NODUAL_ONEPASS;
    e[0].key.gs.colorMask |= 8u;
    CHECK(rd_op_why(&e[0], &e[1], 64, 0) == RD_OP_OTHER);
    /* GREATER without Z write is the same test in both passes */
    e[0] = d[0];
    e[0].key.gs.ztst = RD_ZTST_GREATER;
    e[0].key.gs.zwrite = RD_ZWRITE_OFF;
    CHECK(rd_op_why(&e[0], &e[1], 64, 0) == RD_OP_ONE);
}

static void classes(void)
{
    CHECK(rd_op_class_of(0u, false) == RD_OP_PLAIN);
    CHECK(rd_op_class_of(0u, true) == RD_OP_PLAIN);
    CHECK(rd_op_class_of((uint32_t)ICO_VU_CUT_ONLY, false) == RD_OP_CUT);
    CHECK(rd_op_class_of((uint32_t)ICO_VU_CUT_ONLY, true) == 0u);
    CHECK(rd_op_class_of((uint32_t)ICO_VU_KICK_ONLY, false) == RD_OP_CUT);
    CHECK(rd_op_class_of((uint32_t)(ICO_VU_KICK_ONLY | ICO_VU_FUSED), true) == 0u);
    const int P = RD_OP_V_PASS;
    /* the cut class never: its form is inexact where a corner reaches the
       GPU behind the eye (rd_op_safe_draw), B and C are measurements */
    CHECK(rd_op_classes(P, P, P) == RD_OP_PLAIN);
    CHECK(rd_op_classes(P, RD_OP_V_NOTHING, P) == RD_OP_PLAIN);
    CHECK(rd_op_classes(P, RD_OP_V_DIFFERS, P) == RD_OP_PLAIN); /* the suspect alone failed */
    CHECK(rd_op_classes(P, RD_OP_V_UNTESTED, P) == RD_OP_PLAIN);
    CHECK(rd_op_classes(P, RD_OP_V_UNCHECKED, P) == RD_OP_PLAIN);
    CHECK(rd_op_classes(P, P, RD_OP_V_DIFFERS) == RD_OP_PLAIN); /* C fails, A passes: A alone */
    CHECK(rd_op_classes(P, P, RD_OP_V_BLIND) == RD_OP_PLAIN);
    CHECK(rd_op_classes(P, P, RD_OP_V_UNCHECKED) == RD_OP_PLAIN);
    CHECK(rd_op_classes(RD_OP_V_DIFFERS, P, P) == 0u);
    CHECK(rd_op_classes(RD_OP_V_UNCHECKED, P, P) == 0u);
    CHECK(rd_op_classes(RD_OP_V_BLIND, P, P) == 0u);
    CHECK(rd_op_classes(RD_OP_V_DEVICE, RD_OP_V_DEVICE, RD_OP_V_DEVICE) == 0u);
    /* the draws whose corners all reach the rasteriser at w = 1 */
    CHECK(rd_op_safe_draw((uint32_t)ICO_VU_CLIP_NONE, false, false));
    CHECK(!rd_op_safe_draw((uint32_t)ICO_VU_CLIP_NONE, true, false)); /* particles never */
    CHECK(!rd_op_safe_draw((uint32_t)ICO_VU_CLIP_REGION, false, false));
    CHECK(rd_op_safe_draw((uint32_t)ICO_VU_CLIP_REGION, false, true)); /* proven in front */
    CHECK(
        !rd_op_safe_draw((uint32_t)ICO_VU_CLIP_SCISSOR | (uint32_t)ICO_VU_CUT_ONLY, false, false));
    CHECK(
        rd_op_safe_draw((uint32_t)ICO_VU_CLIP_SCISSOR | (uint32_t)ICO_VU_KICK_ONLY, false, false));
    CHECK(!rd_op_safe_draw((uint32_t)ICO_VU_CLIP_SCISSOR | (uint32_t)ICO_VU_KICK_ONLY |
                               (uint32_t)ICO_VU_FUSED,
                           false, false));
    CHECK(strcmp(rd_op_classes_name(0u), "none") == 0);
    CHECK(strcmp(rd_op_classes_name(RD_OP_PLAIN), "the pairs outside code 36") == 0);
}

/* a draw's planned passes: the pair, then one more pass after it (the
   passes after a dropped alpha pass move down) */
static int plan(RdDrawPass dp[8])
{
    RdDrawPass d[4];
    const int n = pairOf(d, RD_BLEND_LERP_AS, 0);
    dp[0] = d[0];
    dp[1] = d[1];
    dp[2] = d[0];
    dp[2].key.gs.blend = RD_BLEND_COUNT; /* a third pass, kept as it is */
    dp[2].flags = 0;
    return n + 1;
}

/* a plain draw of clip mode none (every corner at its GS position) */
#define N ((uint32_t)ICO_VU_CLIP_NONE)

static void take(void)
{
    char buf[1024], st[1024];
    env("ICO_LOSS_ROW", NULL);
    env("ICO_ONEPASS_BLOCK", NULL);
    env("ICO_ONEPASS_MARK", NULL);
    env("ICO_RD_ONEPASS_SABOTAGE", NULL);
    env("ICO_RD_ONEPASS", "full");
    g_rd.passSamples = 0;
    rd__op_init();
    CHECK(rd__op_on() && rd__op_forced());
    (void)rd_op_text(buf, sizeof(buf), 1); /* the block's counts cleared */
    RdDrawPass dp[8];
    int np = plan(dp);
    const RdDrawPass third = dp[2];
    rd__op_replay_begin();
    rd__op_take(dp, &np, 120, N, 30u, false, false);
    CHECK(np == 2);
    CHECK(dp[0].key.gs.colorMask == 0xF);
    CHECK(dp[0].key.gs.nodual == (RD_NODUAL_ON | RD_NODUAL_ONEPASS));
    CHECK((dp[0].flags & ICO_DF_NODUAL_FACTOR) != 0u);
    CHECK(memcmp(&dp[1], &third, sizeof(third)) == 0);
    /* a fused kick, a fused command's cut: kept */
    np = plan(dp);
    rd__op_take(dp, &np, 120, (uint32_t)(ICO_VU_KICK_ONLY | ICO_VU_FUSED), 30u, false, false);
    CHECK(np == 3);
    rd__op_cut_fused(true);
    np = plan(dp);
    rd__op_take(dp, &np, 120, (uint32_t)ICO_VU_CUT_ONLY, 30u, false, false);
    CHECK(np == 3);
    /* the cut of a command drawn apart: taken */
    rd__op_cut_fused(false);
    np = plan(dp);
    rd__op_take(dp, &np, 120, (uint32_t)ICO_VU_CUT_ONLY, 30u, false, false);
    CHECK(np == 2);
    /* As above 128: kept */
    np = plan(dp);
    rd__op_take(dp, &np, 129, N, 30u, false, false);
    CHECK(np == 3);
    /* the plain class only where every corner is at its GS position: the
       region test not proven in front of the eye and particles kept, the
       region test proven in front taken */
    np = plan(dp);
    rd__op_take(dp, &np, 120, (uint32_t)ICO_VU_CLIP_REGION, 30u, false, false);
    CHECK(np == 3);
    np = plan(dp);
    rd__op_take(dp, &np, 120, N, 30u, true, false);
    CHECK(np == 3);
    np = plan(dp);
    rd__op_take(dp, &np, 120, (uint32_t)ICO_VU_CLIP_REGION, 30u, false, true);
    CHECK(np == 2);
    (void)rd_op_text(buf, sizeof(buf), 1);
    CHECK(strcmp(buf, "; one-pass blend (both classes): 3 pairs a picture in one pass (1 of code "
                      "36), 30 triangles, 0 code 36 commands unfused (5 pairs kept two-pass a "
                      "picture: alpha above 128 1, FBA 0, PABE 0, a FIX factor 0, other 0, not "
                      "proven in front of the eye 2, fused 2, class not in use 0, not made yet 0, "
                      "timed without it 0)") == 0);
    if (strstr(buf, "3 pairs a picture") == NULL) {
        printf("  clause: %s\n", buf);
    }
    /* the unfuse of a code 36 command: its cut pair and the kick's one pass */
    RdDrawPass cut[4], kick[2];
    (void)pairOf(cut, RD_BLEND_LERP_AS, 0);
    kick[0] = cut[0];
    kick[0].key.gs.blend = RD_BLEND_COUNT;
    kick[0].flags = 0;
    CHECK(rd__op_unfuse(cut, 2, kick, 1, 120));
    CHECK(!rd__op_unfuse(cut, 2, kick, 1, 129));
    CHECK(!rd__op_unfuse(cut, 1, kick, 1, 120));
    CHECK(!rd__op_unfuse(cut, 2, kick, 2, 120));
    (void)rd__op_stats_text(st, sizeof(st));
    CHECK(strstr(st, "one-pass blend on (the pairs outside code 36 and code 36's cut and kick "
                     "pairs drawn apart): 3 pairs in one pass of 8 (1 of code 36), 30 triangles, "
                     "1 code 36 commands unfused; kept two-pass: alpha above 128 1,") != NULL);
    if (strstr(st, "3 pairs in one pass of 8") == NULL) {
        printf("  stats: %s\n", st);
    }
    /* =plain: the cut refused for its class, nothing unfused */
    env("ICO_RD_ONEPASS", "plain");
    rd__op_init();
    rd__op_cut_fused(false);
    np = plan(dp);
    rd__op_take(dp, &np, 120, (uint32_t)ICO_VU_CUT_ONLY, 30u, false, false);
    CHECK(np == 3);
    CHECK(!rd__op_unfuse(cut, 2, kick, 1, 120));
    np = plan(dp);
    rd__op_take(dp, &np, 120, N, 30u, false, false);
    CHECK(np == 2);
    /* the check's arms (its pipelines made in the draw under =plain) */
    rd__op_check_arm(RD_OP_ARM_REF, false);
    np = plan(dp);
    rd__op_take(dp, &np, 120, N, 30u, false, false);
    CHECK(np == 3);
    CHECK(!rd__op_unfuse(cut, 2, kick, 1, 120));
    rd__op_check_arm(RD_OP_ARM_B, false);
    np = plan(dp);
    rd__op_take(dp, &np, 120, N, 30u, false, false);
    CHECK(np == 3);
    CHECK(rd__op_unfuse(cut, 2, kick, 1, 120)); /* B: unfused, two passes kept */
    rd__op_check_arm(RD_OP_ARM_A, false);
    np = plan(dp);
    rd__op_take(dp, &np, 120, (uint32_t)ICO_VU_CUT_ONLY, 30u, false, false);
    CHECK(np == 3);
    CHECK(!rd__op_unfuse(cut, 2, kick, 1, 120));
    np = plan(dp);
    rd__op_take(dp, &np, 120, N, 30u, false, false);
    CHECK(np == 2);
    rd__op_check_arm(RD_OP_ARM_C, false);
    np = plan(dp);
    rd__op_take(dp, &np, 120, (uint32_t)ICO_VU_CUT_ONLY, 30u, false, false);
    CHECK(np == 2);
    CHECK(rd__op_unfuse(cut, 2, kick, 1, 120));
    rd__op_check_arm(-1, false);
    uint64_t one[2], unf = 0, unmade = 0;
    rd__op_check_take(one, &unf, &unmade);
    CHECK(one[0] == 1u && one[1] == 1u && unf == 2u && unmade == 0u);
    /* the check without a device and not forced: its pipelines not made
       (counted for its retry), nothing taken */
    env("ICO_RD_ONEPASS", NULL);
    rd__op_init();
    CHECK(strcmp(rd__op_state_text(), "held") == 0 && rd__op_on() == false);
    np = plan(dp);
    rd__op_take(dp, &np, 120, N, 30u, false, false);
    CHECK(np == 3); /* held: nothing */
    rd__op_check_arm(RD_OP_ARM_C, false);
    CHECK(rd__op_on());
    np = plan(dp);
    rd__op_take(dp, &np, 120, N, 30u, false, false);
    CHECK(np == 3);
    CHECK(!rd__op_unfuse(cut, 2, kick, 1, 120));
    rd__op_check_arm(-1, false);
    rd__op_check_take(one, &unf, &unmade);
    CHECK(one[0] == 0u && one[1] == 0u && unf == 0u && unmade == 2u);
}

static void ways(void)
{
    char buf[512], st[1024];
    env("ICO_RD_ONEPASS", NULL);
    env("ICO_LOSS_ROW", NULL);
    env("ICO_ONEPASS_BLOCK", NULL);
    rd__op_init();
    CHECK(strcmp(rd__op_state_text(), "held") == 0 && !rd__op_triable());
    CHECK(rd_op_text(buf, sizeof(buf), 1) == 0u && buf[0] == '\0');
    rd__op_verdict(RD_OP_PLAIN, true, "");
    CHECK(strcmp(rd__op_state_text(), "on") == 0 && rd__op_triable());
    (void)rd__op_stats_text(st, sizeof(st));
    CHECK(strstr(st, "one-pass blend on (the pairs outside code 36): ") == st);
    env("ICO_RD_ONEPASS", "0");
    rd__op_init();
    rd__op_verdict(RD_OP_PLAIN, true, "");
    CHECK(strcmp(rd__op_state_text(), "off") == 0);
    env("ICO_RD_ONEPASS", "full");
    env("ICO_LOSS_ROW", "1");
    rd__op_init();
    CHECK(strcmp(rd__op_state_text(), "off") == 0);
    (void)rd__op_stats_text(st, sizeof(st));
    CHECK(strstr(st, "one-pass blend off (for this start (the loss ladder: 1 start in a row") ==
          st);
    env("ICO_LOSS_ROW", NULL);
    env("ICO_ONEPASS_BLOCK", "was on when it ended (a crash)");
    rd__op_init();
    (void)rd__op_stats_text(st, sizeof(st));
    CHECK(strstr(st, "one-pass blend off (on this install (was on when it ended (a crash)))") ==
          st);
    env("ICO_ONEPASS_BLOCK", NULL);
    env("ICO_RD_ONEPASS", NULL);
    /* a background fault while on: off for the run, kept so through a
       renderer made again (the last case: the run's state stays off; a
       check that puts nothing in use takes the same path, its words the
       selftest tests') */
    rd__op_init();
    rd__op_verdict(RD_OP_PLAIN | RD_OP_CUT, true, "");
    rd__op_fault("a pipeline of it could not be made in the background (the test)");
    CHECK(strcmp(rd__op_state_text(), "off") == 0 && !rd__op_on());
    (void)rd_op_text(buf, sizeof(buf), 1);
    CHECK(strcmp(buf, "; one-pass blend off (for the run: a pipeline of it could not be made in "
                      "the background (the test))") == 0);
    rd__op_verdict(RD_OP_PLAIN, true, ""); /* off: no verdict turns it on again */
    CHECK(strcmp(rd__op_state_text(), "off") == 0);
    rd__op_init();
    (void)rd__op_stats_text(st, sizeof(st));
    CHECK(strstr(st, "one-pass blend off (for the run: a pipeline of it could not be made") == st);
}

static void compose(void)
{
    /* the fast pixel build takes a one-pass colour pass (its blend state
       alone differs) */
    RdFpsPass p;
    memset(&p, 0, sizeof(p));
    p.fs = RD_FS_VU;
    p.nodual = RD_NODUAL_ON | RD_NODUAL_ONEPASS;
    p.dst = RHI_DST_READ_NONE;
    p.mode[0] = (uint32_t)ICO_DF_TEXTURED | (uint32_t)ICO_DF_TCC_RGBA;
    CHECK(rd_fps_eligible(&p) == RD_FPS_FAST);
    p.nodual = RD_NODUAL_ON | RD_NODUAL_NODISCARD | RD_NODUAL_ONEPASS;
    CHECK(rd_fps_eligible(&p) == RD_FPS_FAST);
    p.nodual = RD_NODUAL_ON | RD_NODUAL_FAST | RD_NODUAL_ONEPASS;
    CHECK(rd_fps_eligible(&p) == -1); /* a fast key already */
    /* the dynamic blend state: the bit folded into the colour pass's key */
    RdPipeKeyInt k;
    memset(&k, 0, sizeof(k));
    k.fs = RD_FS_VU;
    k.vs = RD_VS_VU_MAIN;
    k.colorFmt = RHI_FMT_RGBA8_UNORM;
    k.dst = RHI_DST_READ_NONE;
    k.gs.blend = RD_BLEND_LERP_AS;
    k.gs.colorMask = 0xF;
    k.gs.nodual = RD_NODUAL_ON | RD_NODUAL_ONEPASS;
    const bool dynB = g_rd.dynBlend;
    g_rd.dynBlend = true;
    RdPipeKeyInt n = k;
    rd__key_dynamic_normalise(&n);
    CHECK(n.gs.nodual == RD_NODUAL_ON && n.gs.colorMask == 0xF && n.gs.blend == RD_BLEND_COUNT);
    g_rd.dynBlend = false;
    n = k;
    rd__key_dynamic_normalise(&n);
    CHECK(n.gs.nodual == (RD_NODUAL_ON | RD_NODUAL_ONEPASS));
    g_rd.dynBlend = dynB;
}

static void names(void)
{
    for (int i = 0; i < RD_OP_REASONS; i++) {
        CHECK(strcmp(rd_op_reason_name(i), "?") != 0);
    }
    CHECK(strcmp(rd_op_reason_name(RD_OP_REASONS), "?") == 0);
    for (int v = 0; v < RD_OP_VERDICTS; v++) {
        CHECK(strcmp(rd_op_verdict_name(v), "?") != 0);
    }
    CHECK(strcmp(rd_op_verdict_name(RD_OP_V_PASS), "the same bytes") == 0);
    RdOpUse u;
    memset(&u, 0, sizeof(u));
    CHECK(!rd_op_use_counts(&u));
    rd_op_use_note(&u, RD_OP_ALPHA);
    CHECK(!rd_op_use_counts(&u));
    rd_op_use_note(&u, RD_OP_TRIAL);
    CHECK(rd_op_use_counts(&u));
    rd_op_use_note(&u, RD_OP_UNMADE);
    CHECK(!rd_op_use_counts(&u));
    CHECK(RD_NODUAL_ONEPASS == 8u &&
          (RD_NODUAL_ONEPASS & (RD_NODUAL_ON | RD_NODUAL_NODISCARD | RD_NODUAL_FAST)) == 0u);
}

int main(void)
{
    model();
    proof();
    classes();
    take();
    compose();
    names();
    ways(); /* last: it leaves the run off */
    if (failures) {
        printf("rd_onepass_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_onepass_test: ok\n");
    return 0;
}
