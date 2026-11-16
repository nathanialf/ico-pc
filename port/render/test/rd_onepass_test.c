/* rd_onepass_test.c: the pure parts of the two-pass blend's pairs in one pass
 * (port/render/rd_onepass.c, CPU only, no device).
 *   the blend table: for every As 0..128 the carried alpha (As/128 times the
 *     constant's 128/255) and the alpha pass's (As/255, blending off) store
 *     the same byte, As, with each value (the source, the constant, the
 *     product, the alpha pass's source) at every precision a blender may
 *     use (8 to 24 bits fixed, binary16, binary32), rounded to nearest; for
 *     every As 129..255 the carry stores 128 (the factor clamped to 1.0):
 *     the bound; the directed-rounding models where they differ, printed
 *   the proof (rd_onepass_why) on the planner's own pairs: a LERP As pair
 *     with As at most 128 is taken; 129, FBA, PABE, LERP FIX, Cd*FIX + Cs,
 *     Z GREATER with Z write, another target format, a code 36 cut, kick or
 *     fused draw each keep two passes with their reason
 *   the carry (rd__expand_no_dual under the self-test's arms): a LERP with
 *     A gives a colour pass with A and nodual RD_NODUAL_CARRY; a LERP
 *     without A, Cd*FIX + Cs and the alpha pass keep their keys
 *   the take: the rule off, a proven pair kept (the rule not in force),
 *     counted; arm 1, its alpha pass dropped, the passes after it moved
 *     down, the vertex invocations counted; FBA kept; no two-pass blend:
 *     nothing counted
 *   the reasons' words, the switch's state line, the ledger's row */
#include <stdio.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_onepass.h"
#include "shader_consts.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const int kBits[] = {8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 24, 116, 132};
#define NBITS ((int)(sizeof(kBits) / sizeof(kBits[0])))

static void testTable(void)
{
    long cases = 0;
    for (int a = 0; a < NBITS; a++) {
        for (int b = 0; b < NBITS; b++) {
            for (int c = 0; c < NBITS; c++) {
                for (int as = 0; as <= 255; as++) {
                    int two = -1;
                    const int one =
                        rd_onepass_alpha_model(as, kBits[a], kBits[b], kBits[c], 0, 0, &two);
                    if (as <= (int)RD_ONEPASS_ALPHA_MAX) {
                        if (one != as || two != as) {
                            printf("FAIL table: As %d bits %d/%d/%d: carry %d, alpha pass %d\n", as,
                                   kBits[a], kBits[b], kBits[c], one, two);
                            failures++;
                        }
                    } else if (one != 128 || two != as) {
                        printf("FAIL bound: As %d bits %d/%d/%d: carry %d, alpha pass %d\n", as,
                               kBits[a], kBits[b], kBits[c], one, two);
                        failures++;
                    }
                    cases++;
                }
            }
        }
    }
    printf("blend table: %ld cases (As 0..255, %d precisions each for the source, the constant "
           "and the product, round to nearest)\n",
           cases, NBITS);
    /* the directed roundings (not what a blender does where it rounds to
       nearest; the device self-test judges the device): printed */
    int differ = 0, models = 0;
    for (int a = 0; a < NBITS; a++) {
        for (int r = 1; r <= 2; r++) {
            for (int st = 0; st <= 2; st++) {
                models++;
                int n = 0;
                for (int as = 0; as <= (int)RD_ONEPASS_ALPHA_MAX; as++) {
                    int two = -1;
                    n += rd_onepass_alpha_model(as, kBits[a], kBits[a], kBits[a], r, st, &two) !=
                         two;
                }
                differ += n != 0;
            }
        }
    }
    printf("directed rounding: %d of %d models store another byte for some As in 0..128\n", differ,
           models);
}

/* the planner's pass for a LERP or mode 3 draw under s, expanded */
static int pair(const RdStateBlock *s, RhiFormat fmt, RdDrawPass ex[4])
{
    RdDrawPass pl[2];
    const int n =
        rd__plan_screen_draw(s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, fmt, RHI_FMT_D32F_S8, pl);
    CHECK(n == 1);
    return rd__expand_no_dual(pl, 1, ex);
}

static RdStateBlock lerpState(uint8_t blend)
{
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(RD_TEST_Z_GEQUAL);
    s.ds.zwrite = RD_ZWRITE_ON;
    s.ds.abe = 1;
    s.ds.blend = blend;
    s.ds.blendFix = 0x40;
    s.ds.colorMask = 0xF;
    s.ds.colclamp = 1;
    return s;
}

static void testWhy(void)
{
    g_rd.noDual = true;
    RdDrawPass ex[4];
    RdStateBlock s = lerpState(RD_BLEND_LERP_AS);
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 128, 0) == RD_ONEPASS_WHY_NONE);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 0, 0) == RD_ONEPASS_WHY_NONE);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 129, 0) == RD_ONEPASS_WHY_ALPHA);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 255, 0) == RD_ONEPASS_WHY_ALPHA);
    /* a code 36 cut or kick drawn alone is an ordinary draw; a fused kick
       is not (its pixels unblended) */
    CHECK(rd_onepass_why(&ex[0], &ex[1], 64, ICO_VU_CUT_ONLY) == RD_ONEPASS_WHY_NONE);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 64, ICO_VU_KICK_ONLY) == RD_ONEPASS_WHY_NONE);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 64, ICO_VU_KICK_ONLY | ICO_VU_FUSED) ==
          RD_ONEPASS_WHY_OTHER);
    s.ds.fba = 1;
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 64, 0) == RD_ONEPASS_WHY_FBA);
    s = lerpState(RD_BLEND_LERP_AS);
    s.ds.pabe = 1;
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 64, 0) == RD_ONEPASS_WHY_PABE);
    s = lerpState(RD_BLEND_LERP_FIX);
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 64, 0) == RD_ONEPASS_WHY_FIX);
    s = lerpState(RD_BLEND_CD_FIX_ADD_CS);
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 64, 0) == RD_ONEPASS_WHY_OTHER);
    s = lerpState(RD_BLEND_LERP_AS);
    s.ds.test = rd_test_from_gs(0x70000u); /* ZTE, ZTST GREATER */
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(ex[0].key.gs.ztst == RD_ZTST_GREATER);
    CHECK(rd_onepass_why(&ex[0], &ex[1], 64, 0) == RD_ONEPASS_WHY_OTHER);
    s = lerpState(RD_BLEND_LERP_AS);
    RdDrawPass c = ex[0];
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    c = ex[0];
    c.key.colorFmt = RHI_FMT_RGBA16F;
    CHECK(rd_onepass_why(&c, &ex[1], 64, 0) == RD_ONEPASS_WHY_OTHER);
    g_rd.noDual = false;
}

static void testCarry(void)
{
    g_rd.noDual = true;
    RdDrawPass ex[4];
    RdStateBlock s = lerpState(RD_BLEND_LERP_AS);
    rd__onepass_force_arm(-1);
    CHECK(!rd__onepass_carry());
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(ex[0].key.gs.nodual == 1u && ex[0].key.gs.colorMask == 7u);
    rd__onepass_force_arm(0);
    CHECK(rd__onepass_carry());
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(ex[0].key.gs.nodual == RD_NODUAL_CARRY && ex[0].key.gs.colorMask == 0xFu);
    CHECK(ex[1].key.gs.nodual == 1u && ex[1].key.gs.colorMask == 8u &&
          (ex[1].flags & (uint32_t)ICO_DF_NODUAL_ALPHA_PASS) != 0u);
    /* a LERP without A: no carry, no alpha pass */
    s.ds.colorMask = 7;
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 1);
    CHECK(ex[0].key.gs.nodual == 1u && ex[0].key.gs.colorMask == 7u);
    /* Cd*FIX + Cs: the colour pass as before */
    s = lerpState(RD_BLEND_CD_FIX_ADD_CS);
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(ex[0].key.gs.nodual == 1u && ex[0].key.gs.colorMask == 7u);
    /* the code 36 cut not fused where its pair goes to one pass: under arm 1
       a carried, proven pair; not with FBA, a third pass, As above 128, or
       the rule off */
    s = lerpState(RD_BLEND_LERP_AS);
    rd__onepass_force_arm(1);
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(rd__onepass_unfuse(ex, 2, 128));
    CHECK(!rd__onepass_unfuse(ex, 2, 129));
    CHECK(!rd__onepass_unfuse(ex, 1, 128));
    s.ds.fba = 1;
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(!rd__onepass_unfuse(ex, 2, 64));
    s.ds.fba = 0;
    rd__onepass_force_arm(0);
    CHECK(pair(&s, RHI_FMT_RGBA8_UNORM, ex) == 2);
    CHECK(!rd__onepass_unfuse(ex, 2, 64));
    rd__onepass_force_arm(-1);
    CHECK(!rd__onepass_unfuse(ex, 2, 64));
    rd__onepass_force_arm(-1);
    g_rd.noDual = false;
}

static void testTake(void)
{
    ico_attempt_reset();
    rd__onepass_init();
    g_rd.noDual = true;
    RdDrawPass ex[4];
    RdStateBlock s = lerpState(RD_BLEND_LERP_AS);
    /* the rule off: proven, kept, counted */
    int np = pair(&s, RHI_FMT_RGBA8_UNORM, ex);
    g_rd.replayCounter++;
    rd__onepass_take(ex, &np, 100, 0, 300);
    CHECK(np == 2);
    RdOnepassCounts oc;
    rd__onepass_counts(&oc);
    CHECK(oc.pairs == 1u && oc.onePass == 0u && oc.kept[RD_ONEPASS_WHY_OFF] == 1u &&
          oc.saved == 0u);
    /* arm 1: carried and proven, the alpha pass dropped */
    rd__onepass_force_arm(1);
    RdStateBlock t = lerpState(RD_BLEND_LERP_AS);
    RdDrawPass pl[2];
    CHECK(rd__plan_screen_draw(&t, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                               RHI_FMT_D32F_S8, pl) == 1);
    /* two planned passes (the alpha test's split stands in): two pairs */
    pl[1] = pl[0];
    np = rd__expand_no_dual(pl, 2, ex);
    CHECK(np == 4);
    rd__onepass_take(ex, &np, 128, 0, 300);
    CHECK(np == 2);
    CHECK(ex[0].key.gs.nodual == RD_NODUAL_CARRY && ex[1].key.gs.nodual == RD_NODUAL_CARRY);
    rd__onepass_counts(&oc);
    CHECK(oc.pairs == 3u && oc.onePass == 2u && oc.saved == 600u);
    uint32_t ro = 9, rp = 9;
    rd__onepass_replay(&ro, &rp);
    CHECK(ro == 2u && rp == 3u);
    /* FBA under arm 1: kept with its reason */
    t.ds.fba = 1;
    np = pair(&t, RHI_FMT_RGBA8_UNORM, ex);
    rd__onepass_take(ex, &np, 64, 0, 30);
    CHECK(np == 2);
    rd__onepass_counts(&oc);
    CHECK(oc.kept[RD_ONEPASS_WHY_FBA] == 1u);
    /* As above 128 under arm 1 */
    t.ds.fba = 0;
    np = pair(&t, RHI_FMT_RGBA8_UNORM, ex);
    rd__onepass_take(ex, &np, 129, 0, 30);
    CHECK(np == 2);
    rd__onepass_counts(&oc);
    CHECK(oc.kept[RD_ONEPASS_WHY_ALPHA] == 1u);
    /* PABE under arm 1 */
    t.ds.pabe = 1;
    np = pair(&t, RHI_FMT_RGBA8_UNORM, ex);
    rd__onepass_take(ex, &np, 64, 0, 30);
    CHECK(np == 2);
    rd__onepass_counts(&oc);
    CHECK(oc.kept[RD_ONEPASS_WHY_PABE] == 1u);
    /* no two-pass blend: nothing counted */
    g_rd.noDual = false;
    rd__onepass_take(ex, &np, 64, 0, 30);
    RdOnepassCounts oc2;
    rd__onepass_counts(&oc2);
    CHECK(oc2.pairs == oc.pairs);
    rd__onepass_force_arm(-1);
    /* the self-test without a device: nothing to check */
    CHECK(rd__onepass_selftest());
    const char *words = NULL;
    CHECK(rd__onepass_test_state(&words) == 0 && strcmp(words, "not run") == 0);
    CHECK(!rd__onepass_on());
    CHECK(ico_attempt_state_of(ICO_ATT_ONEPASS) == ICO_ATT_NA);
    CHECK(strcmp(rd__onepass_why_text(RD_ONEPASS_WHY_ALPHA), "alpha above 128") == 0);
    CHECK(strcmp(rd__onepass_why_text(RD_ONEPASS_WHY_COUNT), "?") == 0);
    ico_attempt_reset();
}

int main(void)
{
    testTable();
    testWhy();
    testCarry();
    testTake();
    printf("rd_onepass_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
