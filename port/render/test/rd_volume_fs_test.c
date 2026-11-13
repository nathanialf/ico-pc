/* rd_volume_fs_test.c: the pure parts of the shadow volumes' stencil-only
 * entry (port/render/rd_volume_fs.c, CPU only, no device).
 *   rd__volume_fs_why: each reason in its order (the switch, the backend,
 *     the self-test, the pass's samples), and taken only when all hold
 *   rd__volume_fs_twin: the volume key by date_clear_ps, every other field
 *     the general key's; a key that is not a volume's unchanged
 *   rd__shadow_volume_key without a device: the general key (sprite_ps), the
 *     replay's volume draws seen and none taken; the start-up set's shadow
 *     keys without a twin (nothing offered)
 *   ICO_RD_VOLUME_FS's words, the bench's force, the reasons' words
 *   the attempt ledger's row through its decider: each state and its words */
#include <stdio.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_volume_fs.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void testWhy(void)
{
    CHECK(rd__volume_fs_why(1, 1, 1, 0) == RD_VOLUME_FS_WHY_NONE);
    CHECK(rd__volume_fs_why(1, 1, 1, 1) == RD_VOLUME_FS_WHY_NONE);
    CHECK(rd__volume_fs_why(0, 1, 1, 0) == RD_VOLUME_FS_WHY_OFF);
    CHECK(rd__volume_fs_why(0, 0, 0, 4) == RD_VOLUME_FS_WHY_OFF); /* the switch first */
    CHECK(rd__volume_fs_why(1, 0, 1, 0) == RD_VOLUME_FS_WHY_BACKEND);
    CHECK(rd__volume_fs_why(1, 0, 0, 4) == RD_VOLUME_FS_WHY_BACKEND);
    CHECK(rd__volume_fs_why(1, 1, 0, 0) == RD_VOLUME_FS_WHY_TEST);
    CHECK(rd__volume_fs_why(1, 1, 2, 0) == RD_VOLUME_FS_WHY_TEST);
    CHECK(rd__volume_fs_why(1, 1, 2, 4) == RD_VOLUME_FS_WHY_TEST);
    CHECK(rd__volume_fs_why(1, 1, 1, 2) == RD_VOLUME_FS_WHY_MSAA);
    CHECK(rd__volume_fs_why(1, 1, 1, 4) == RD_VOLUME_FS_WHY_MSAA);
    for (int w = 0; w < RD_VOLUME_FS_WHY_COUNT; w++) {
        CHECK(rd__volume_fs_why_text(w) != NULL && rd__volume_fs_why_text(w)[0] != '?');
    }
    CHECK(strcmp(rd__volume_fs_why_text(RD_VOLUME_FS_WHY_COUNT), "?") == 0);
    CHECK(strcmp(rd__volume_fs_why_text(-1), "?") == 0);
}

static void testTwin(void)
{
    /* shadow_Reset's TEST 0x50000 (Z GEQUAL) and the self-test's ALWAYS */
    static const uint64_t kTests[2] = {RD_TEST_Z_GEQUAL, RD_TEST_Z_ALWAYS};
    for (int t = 0; t < 2; t++) {
        RdStateBlock s;
        rd__reset_state_block(&s);
        s.ds.test = rd_test_from_gs(kTests[t]);
        s.ds.zwrite = RD_ZWRITE_ON; /* the volumes never write Z whatever the state */
        for (int decr = 0; decr < 2; decr++) {
            const RdPipeKeyInt g = rd__shadow_volume_key(&s, RHI_FMT_RGBA8_UNORM, decr);
            /* no device: the general key, as before this entry */
            CHECK(g.gs.program == RD_PROG_SHADOW_VOLUME && g.vs == RD_VS_SPRITE_WORLD &&
                  g.fs == RD_FS_SPRITE && g.gs.colorMask == 0 && g.gs.zwrite == RD_ZWRITE_OFF &&
                  g.gs.stencil == (decr ? RD_STENCIL_DECR : RD_STENCIL_INCR) &&
                  g.depthFmt == RHI_FMT_D32F_S8 &&
                  g.gs.ztst == (t == 0 ? RD_ZTST_GEQUAL : RD_ZTST_ALWAYS));
            const RdPipeKeyInt k = rd__volume_fs_twin(&g);
            CHECK(k.fs == RD_FS_DATE_CLEAR);
            RdPipeKeyInt back = k;
            back.fs = g.fs;
            CHECK(memcmp(&back, &g, sizeof(g)) == 0); /* the fragment entry alone differs */
            /* the twin of the twin is the twin (not a sprite_ps key any more) */
            const RdPipeKeyInt kk = rd__volume_fs_twin(&k);
            CHECK(memcmp(&kk, &k, sizeof(k)) == 0);
        }
    }
    /* not a volume's key: unchanged (DATE's clear set-up, a screen prim) */
    const RdPipeKeyInt d = rd__date_setup_key(RD_STENCIL_DATE_CLEAR, RHI_FMT_RGBA8_UNORM);
    const RdPipeKeyInt dt = rd__volume_fs_twin(&d);
    CHECK(memcmp(&dt, &d, sizeof(d)) == 0);
    RdPipeKeyInt sp;
    memset(&sp, 0, sizeof(sp));
    sp.vs = RD_VS_SPRITE_WORLD;
    sp.fs = RD_FS_SPRITE;
    sp.gs.program = (uint8_t)(RD_PROG_SHADOW_VOLUME + 1);
    const RdPipeKeyInt spt = rd__volume_fs_twin(&sp);
    CHECK(memcmp(&spt, &sp, sizeof(sp)) == 0);
}

static void testTurn(void)
{
    /* no device: every volume draw keeps sprite_ps, counted as seen in its
     * replay; none taken; nothing for the run's or the block's counts
     * beyond the seen */
    rd__volume_fs_init();
    CHECK(!rd__volume_fs_on());
    CHECK(rd__volume_fs_test_state(NULL) == 0);
    const char *w = NULL;
    (void)rd__volume_fs_test_state(&w);
    CHECK(w && strcmp(w, "not run") == 0);
    CHECK(rd__volume_fs_selftest()); /* not asked here: true, nothing run */
    CHECK(rd__volume_fs_test_state(NULL) == 0);
    g_rd.replayCounter = 41u;
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(RD_TEST_Z_GEQUAL);
    for (int i = 0; i < 3; i++) {
        (void)rd__shadow_volume_key(&s, RHI_FMT_RGBA8_UNORM, i & 1);
    }
    uint32_t taken = 9, seen = 9;
    rd__volume_fs_replay(&taken, &seen);
    CHECK(taken == 0 && seen == 3);
    g_rd.replayCounter = 42u; /* another replay: its counts start at 0 */
    rd__volume_fs_replay(&taken, &seen);
    CHECK(taken == 0 && seen == 0);
    (void)rd__shadow_volume_key(&s, RHI_FMT_RGBA8_UNORM, 0);
    rd__volume_fs_replay(&taken, &seen);
    CHECK(taken == 0 && seen == 1);
    uint64_t rt = 9, rs = 9;
    int why = -1;
    rd__volume_fs_counts(&rt, &rs, &why);
    CHECK(rt == 0 && rs == 4);
    CHECK(why == (RD_VOLUME_FS_DEFAULT ? RD_VOLUME_FS_WHY_BACKEND : RD_VOLUME_FS_WHY_OFF));
    /* the bench's force: off and on, then the default again */
    rd__volume_fs_force(0);
    CHECK(!rd__volume_fs_wanted());
    rd__volume_fs_force(1);
    CHECK(rd__volume_fs_wanted());
    CHECK(!rd__volume_fs_on()); /* still behind the backend and the self-test */
    rd__volume_fs_force(-1);
    CHECK(rd__volume_fs_wanted() == (RD_VOLUME_FS_DEFAULT != 0));
    /* the start-up set's shadow keys: nothing offered without a device, so
     * no twin among them */
    static RdPipeKeyInt keys[64];
    const uint32_t n = rd__enumerate_reachable_shadow(keys, 64, 0);
    CHECK(n > 0 && n < 64);
    for (uint32_t i = 0; i < n; i++) {
        CHECK(!(keys[i].gs.program == RD_PROG_SHADOW_VOLUME && keys[i].fs == RD_FS_DATE_CLEAR));
    }
    RdPipeKeyInt gen[1];
    gen[0] = rd__shadow_volume_key(&s, RHI_FMT_RGBA8_UNORM, 0);
    CHECK(rd__volume_fs_keys(keys, 64, 5, gen, 1) == 5);
    g_rd.replayCounter = 0;
}

static void testEnv(void)
{
    CHECK(rd__volume_fs_env("") == -1);
    CHECK(rd__volume_fs_env("0") == 0);
    CHECK(rd__volume_fs_env("1") == 1);
    CHECK(rd__volume_fs_env("sabotage") == 2);
}

static void testLedger(void)
{
    ico_attempt_reset();
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_NOT_SET);
    CHECK(ico_attempt_decide_volume_fs(1, -1, 1, 1) == 1);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_ON);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_VOLUME_FS),
                 "self-test passed: the same count as sprite_ps") == 0);
    CHECK(ico_attempt_decide_volume_fs(1, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_volume_fs(1, -1, 1, 0) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_REFUSED);
    CHECK(ico_attempt_decide_volume_fs(0, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_NA);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_VOLUME_FS),
                 "date_clear_ps draws the volumes under Vulkan alone") == 0);
    CHECK(ico_attempt_decide_volume_fs(1, 0, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_volume_fs(1, -1, 0, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VOLUME_FS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_volume_fs(1, 1, 0, 1) == 1);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_VOLUME_FS), "ICO_RD_VOLUME_FS=1, self-test passed") ==
          0);
    ico_attempt_fire(ICO_ATT_VOLUME_FS, 20);
    ico_attempt_fire2(ICO_ATT_VOLUME_FS, 1);
    char b[4096];
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(strstr(b, "the shadow volumes by the stencil-only entry on 20 volume draws (1 kept on "
                    "sprite_ps)") != NULL);
    ico_attempt_reset();
}

int main(void)
{
    testWhy();
    testTwin();
    testTurn();
    testEnv();
    testLedger();
    if (failures) {
        printf("rd_volume_fs_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_volume_fs_test: ok\n");
    return 0;
}
