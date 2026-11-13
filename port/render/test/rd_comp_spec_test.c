/* rd_comp_spec_test.c: the pure parts of the shadow composites' and levels'
 * own entry (port/render/rd_comp_spec.c, CPU only, no device).
 *   rd_comp_spec_key_form: the composites' key as the planner makes it from
 *     shadow.c's state (ATST GREATER 0, AFAIL KEEP, Z ALWAYS without write,
 *     the LERP As; with SCENE's depth and without; DATE's stencil test of the
 *     bit) and the levels' (no blend, no alpha test, no depth) take their
 *     forms; each field the forms hold, changed alone, refuses
 *   rd_comp_spec_flags_why: the planner's flags for those keys with the
 *     texture's TEXTURED and TCC pass; each flag sprite_ps reads, added alone,
 *     refuses; a flag it never reads in this build does not; the TEXA word
 *   rd__comp_spec_twin: the fragment entry alone differs
 *   without a device: a draw noted and its run's bind (rd__comp_spec_turn)
 *     leave the pipeline as it was, counted as seen and kept; the start-up
 *     set gains no twin; the self-test is not asked
 *   ICO_RD_COMP_SPEC's words, the bench's force and its DATE set-up switch,
 *   the reasons' words, the attempt ledger's row */
#include <stdio.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_bench.h"
#include "rd_comp_spec.h"
#include "shader_consts.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the planner's one pass for state s (a sprite command's triangles) */
static RdDrawPass planned(const RdStateBlock *s, uint8_t space, RhiFormat depth)
{
    RdDrawPass dp[2];
    memset(dp, 0, sizeof(dp));
    const int np =
        rd__plan_screen_draw(s, RD_PRIM_TRIANGLES, space, RHI_FMT_RGBA8_UNORM, depth, dp);
    CHECK(np == 1);
    return dp[0];
}

/* shadow.c's composite: ALPHA 0x44 (the LERP As), ABE, TEST ATE ATST
 * GREATER AREF 0 AFAIL KEEP, ZTE ZTST ALWAYS, Z write off (dateOn: DATE,
 * DATM 0) */
static RdStateBlock compositeState(bool dateOn)
{
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(dateOn ? 0x3400Du : 0x3000Du);
    s.ds.zwrite = RD_ZWRITE_OFF;
    s.ds.abe = 1;
    s.ds.blend = RD_BLEND_LERP_AS;
    s.ds.colorMask = 0xF;
    return s;
}

/* a level: ABE 0, TEST ZTE ZTST ALWAYS (no alpha test) */
static RdStateBlock levelState(void)
{
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(RD_TEST_Z_ALWAYS);
    s.ds.zwrite = RD_ZWRITE_OFF;
    s.ds.abe = 0;
    s.ds.colorMask = 0xF;
    return s;
}

static void testForms(void)
{
    for (int space = 0; space < 2; space++) {
        const uint8_t sp = space ? RD_SPACE_WORLD : RD_SPACE_UI;
        for (int dz = 0; dz < 2; dz++) {
            const RhiFormat depth = dz ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN;
            RdStateBlock c = compositeState(false);
            const RdDrawPass p = planned(&c, sp, depth);
            CHECK(rd_comp_spec_key_form(&p.key) == RD_COMP_SPEC_FORM_COMPOSITE);
            /* the planner's flags with the texture's (rd_replay.c fillDrawCB:
             * TEXTURED, MODULATE, TCC) */
            CHECK(rd_comp_spec_flags_why(p.flags | ICO_DF_TEXTURED | ICO_DF_TCC_RGBA,
                                         (uint32_t)RD_TEXA_80_80) == RD_COMP_SPEC_WHY_NONE);
            /* each field the form holds, alone */
            RdPipeKeyInt k = p.key;
            k.fs = RD_FS_SPRITE_TEXA;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.nodual = 1;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.dst = RHI_DST_READ_ATTACHMENT;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.msaa = 4;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.aa1 = 1;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.prim = RD_PRIM_LINES;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.colorFmt = RHI_FMT_RGBA16F;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.afailSplit = 1;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.colorMask = 0x7;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.blend = RD_BLEND_CS_AS_ADD_CD;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.atst = RD_ATST_GEQUAL;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.zwrite = RD_ZWRITE_ON;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.stencil = RD_STENCIL_INCR;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            k = p.key;
            k.gs.program = RD_PROG_SHADOW_VOLUME;
            CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            /* the twin: the fragment entry alone */
            const RdPipeKeyInt t = rd__comp_spec_twin(&p.key);
            CHECK(t.fs == RD_FS_SPRITE_COMP);
            RdPipeKeyInt back = t;
            back.fs = p.key.fs;
            CHECK(memcmp(&back, &p.key, sizeof(back)) == 0);
            CHECK(rd_comp_spec_key_form(&t) == RD_COMP_SPEC_FORM_NONE); /* not twinned again */
            /* DATE through the stencil bit: the bit's test, DATE out of the flags */
            if (dz) {
                RdStateBlock cd = compositeState(true);
                RdDrawPass pd = planned(&cd, sp, depth);
                if (rd__date_stencil_pass(&pd)) {
                    CHECK(pd.key.gs.stencil == RD_STENCIL_DATE_TEST0);
                    CHECK(rd_comp_spec_key_form(&pd.key) == RD_COMP_SPEC_FORM_COMPOSITE);
                    CHECK(rd_comp_spec_flags_why(pd.flags | ICO_DF_TEXTURED | ICO_DF_TCC_RGBA,
                                                 0u) == RD_COMP_SPEC_WHY_NONE);
                } else {
                    printf("FAIL %s:%d: the DATE composite has no stencil twin with a depth\n",
                           __FILE__, __LINE__);
                    failures++;
                }
                /* DATE by the snapshot: in the flags, refused */
                const RdDrawPass ps = planned(&cd, sp, depth);
                CHECK(rd_comp_spec_flags_why(ps.flags | ICO_DF_TEXTURED | ICO_DF_TCC_RGBA, 0u) ==
                      RD_COMP_SPEC_WHY_FLAGS);
            }
            /* the level: no depth alone */
            RdStateBlock l = levelState();
            const RdDrawPass pl = planned(&l, sp, depth);
            CHECK(rd_comp_spec_key_form(&pl.key) ==
                  (dz ? RD_COMP_SPEC_FORM_NONE : RD_COMP_SPEC_FORM_LEVEL));
            if (!dz) {
                CHECK(rd_comp_spec_flags_why(pl.flags | ICO_DF_TEXTURED | ICO_DF_TCC_RGBA, 0u) ==
                      RD_COMP_SPEC_WHY_NONE);
                k = pl.key;
                k.gs.atst = RD_ATST_GREATER;
                CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
                k = pl.key;
                k.gs.stencil = RD_STENCIL_DATE_TEST0;
                CHECK(rd_comp_spec_key_form(&k) == RD_COMP_SPEC_FORM_NONE);
            }
        }
    }
}

static void testFlags(void)
{
    const uint32_t fixed = (uint32_t)ICO_DF_TEXTURED | (uint32_t)ICO_DF_TCC_RGBA;
    CHECK(RD_COMP_SPEC_FIXED == fixed);
    CHECK(rd_comp_spec_flags_why(fixed, 0u) == RD_COMP_SPEC_WHY_NONE);
    /* every TEXA mode word with an RGBA32 texel; a 24- or 16-bit one refused */
    CHECK(rd_comp_spec_flags_why(fixed, 0xFFu) == RD_COMP_SPEC_WHY_NONE);
    CHECK(rd_comp_spec_flags_why(fixed, 1u << 8) == RD_COMP_SPEC_WHY_TEXFMT);
    CHECK(rd_comp_spec_flags_why(fixed, 2u << 8) == RD_COMP_SPEC_WHY_TEXFMT);
    /* each flag sprite_ps reads, alone over the fixed ones, and each fixed
     * one taken away */
    static const uint32_t kRead[] = {
        ICO_DF_DECAL,  ICO_DF_FBA,    ICO_DF_PABE,    ICO_DF_FIX_FACTOR, ICO_DF_PREMUL, ICO_DF_DATE,
        ICO_DF_C1_DST, ICO_DF_GS_LOD, ICO_DF_NEAREST, ICO_DF_ABOVE1X,    ICO_DF_FOLD,
    };
    for (size_t i = 0; i < sizeof(kRead) / sizeof(kRead[0]); i++) {
        CHECK((RD_COMP_SPEC_READ & kRead[i]) != 0u);
        CHECK(rd_comp_spec_flags_why(fixed | kRead[i], 0u) == RD_COMP_SPEC_WHY_FLAGS);
    }
    CHECK(rd_comp_spec_flags_why(ICO_DF_TEXTURED, 0u) == RD_COMP_SPEC_WHY_FLAGS);
    CHECK(rd_comp_spec_flags_why(ICO_DF_TCC_RGBA, 0u) == RD_COMP_SPEC_WHY_FLAGS);
    CHECK(rd_comp_spec_flags_why(0u, 0u) == RD_COMP_SPEC_WHY_FLAGS);
    /* bits read only by other builds, or under a refused one: taken */
    static const uint32_t kOther[] = {
        ICO_DF_DATM,         ICO_DF_TEXA_MAG_LINEAR, ICO_DF_TEXA_MIN_LINEAR,  ICO_DF_TEXA_CLAMP_S,
        ICO_DF_TEXA_CLAMP_T, ICO_DF_PLANE,           ICO_DF_GS_BILINEAR_GRID, ICO_DF_NODUAL_FACTOR,
    };
    for (size_t i = 0; i < sizeof(kOther) / sizeof(kOther[0]); i++) {
        CHECK((RD_COMP_SPEC_READ & kOther[i]) == 0u);
        CHECK(rd_comp_spec_flags_why(fixed | kOther[i], 0u) == RD_COMP_SPEC_WHY_NONE);
    }
}

static void testNoDevice(void)
{
    rd__comp_spec_init(); /* no device: no module, the state cleared */
    CHECK(!rd__comp_spec_on());
    const char *w = NULL;
    CHECK(rd__comp_spec_test_state(&w) == 0 && w && strcmp(w, "not run") == 0);
    CHECK(rd__comp_spec_selftest()); /* not offered: nothing to check */
    CHECK(rd__comp_spec_test_state(NULL) == 0);
    RdStateBlock c = compositeState(false);
    const RdDrawPass p = planned(&c, RD_SPACE_WORLD, RHI_FMT_D32F_S8);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.mode[0] = p.flags | ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
    rd__comp_spec_note(&p.key, &cb);
    rd__comp_spec_note(&p.key, NULL); /* nothing */
    /* not of the forms: not seen */
    RdPipeKeyInt other = p.key;
    other.fs = RD_FS_FONT;
    rd__comp_spec_note(&other, &cb);
    uint64_t taken = 9, seen = 9;
    int last = -1;
    rd__comp_spec_counts(&taken, &seen, &last);
    CHECK(taken == 0 && seen == 1);
    /* the default build wants it, so the reason is the missing module */
    CHECK(last == (RD_COMP_SPEC_DEFAULT ? RD_COMP_SPEC_WHY_MODULE : RD_COMP_SPEC_WHY_OFF));
    uint32_t rt = 9, rs = 9;
    rd__comp_spec_replay(&rt, &rs);
    CHECK(rt == 0 && rs == 1);
    /* the run's bind: the pipeline as it was */
    const RhiPipeline general = {0x1234u};
    const RhiPipeline got = rd__comp_spec_turn(general, &p.key);
    CHECK(got.id == general.id);
    /* the start-up set: no twin without the module */
    RdPipeKeyInt keys[4];
    keys[0] = p.key;
    CHECK(rd__comp_spec_keys(keys, 4, 1, 1) == 1);
    /* the bench's force: still no module */
    rd__comp_spec_force(1);
    CHECK(!rd__comp_spec_on());
    rd__comp_spec_force(-1);
    rd__date_setup_bench_skip(1);
    CHECK(rd__date_setup_bench_skipped());
    rd__date_setup_bench_skip(0);
    CHECK(!rd__date_setup_bench_skipped());
}

static void testWords(void)
{
    CHECK(rd__comp_spec_env("0") == 0);
    CHECK(rd__comp_spec_env("1") == 1);
    CHECK(rd__comp_spec_env("sabotage") == 2);
    CHECK(rd__comp_spec_env("") == -1);
    for (int w = 0; w < RD_COMP_SPEC_WHY_COUNT; w++) {
        CHECK(rd__comp_spec_why_text(w) != NULL && rd__comp_spec_why_text(w)[0] != '?');
    }
    CHECK(strcmp(rd__comp_spec_why_text(RD_COMP_SPEC_WHY_COUNT), "?") == 0);
    CHECK(strcmp(rd__comp_spec_why_text(-1), "?") == 0);
}

static void testLedger(void)
{
    CHECK(ico_attempt_decide_comp_spec(1, 1, -1, 1, 1) == 1);
    CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_ON);
    CHECK(ico_attempt_decide_comp_spec(0, 1, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_NA);
    CHECK(ico_attempt_decide_comp_spec(1, 1, -1, 1, 0) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_COMP_SPEC) == ICO_ATT_REFUSED);
}

int main(void)
{
    testForms();
    testFlags();
    testNoDevice();
    testWords();
    testLedger();
    printf("rd_comp_spec_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
