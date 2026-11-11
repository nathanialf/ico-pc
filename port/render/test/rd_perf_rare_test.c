/*
 * port/render/test/rd_perf_rare_test.c
 *
 * A stalled replay's list 0 named by its pipelines (rd_perf_rare.h, issue
 * 123), from made-up replays:
 *   replay   the lookups of one replay: distinct pipelines kept with their
 *            counts, id 0 ignored, the 65th distinct one dropped and counted;
 *   block    the replays a pipeline was seen in (a replay's many lookups
 *            count once), the block's table full;
 *   text     99 ordinary replays and a stalled one that drew a pipeline seen
 *            in 2 of the 100: the rare one named first with its shaders, the
 *            most used after it, word for word; none rare; no lookups.
 */
#include <stdio.h>
#include <string.h>

#include "rd_perf_rare.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            printf("FAIL %s:%d:\n got  \"%s\"\n want \"%s\"\n", __FILE__, __LINE__, (got),         \
                   (want));                                                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const char *vsName(uint8_t c)
{
    return c == 30 ? "vu_prelit_vs" : c == 31 ? "vu_skin_vs" : "sprite_vs";
}

static const char *fsName(uint8_t c)
{
    return c == 5 ? "vu_ps" : "sprite_ps";
}

static RdPerfRareReplay s_r;
static RdPerfRareBlock s_b;

static void testReplay(void)
{
    rd_perf_rare_replay_reset(&s_r, 7);
    rd_perf_rare_replay_add(&s_r, 12, 30, 5, 0);
    rd_perf_rare_replay_add(&s_r, 12, 30, 5, 0);
    rd_perf_rare_replay_add(&s_r, 0, 1, 1, 0); /* no pipeline: not counted */
    rd_perf_rare_replay_add(&s_r, 13, 31, 5, 1);
    CHECK(s_r.replay == 7 && s_r.n == 2 && s_r.lookups == 3 && s_r.dropped == 0);
    CHECK(s_r.k[0].id == 12 && s_r.k[0].lookups == 2 && s_r.k[1].id == 13 && s_r.k[1].dst == 1);
    rd_perf_rare_replay_reset(&s_r, 8);
    for (uint32_t id = 1; id <= RD_PERF_RARE_KEYS + 1u; id++) {
        rd_perf_rare_replay_add(&s_r, id * 977u, 2, 0, 0);
    }
    rd_perf_rare_replay_add(&s_r, 977u, 2, 0, 0); /* a kept one again */
    CHECK(s_r.n == RD_PERF_RARE_KEYS && s_r.dropped == 1 && s_r.lookups == RD_PERF_RARE_KEYS + 2u);
    CHECK(s_r.k[0].lookups == 2);
}

static void testBlock(void)
{
    rd_perf_rare_block_reset(&s_b);
    rd_perf_rare_replay_reset(&s_r, 1);
    rd_perf_rare_replay_add(&s_r, 12, 30, 5, 0);
    rd_perf_rare_replay_add(&s_r, 12, 30, 5, 0);
    rd_perf_rare_block_add(&s_b, &s_r);
    rd_perf_rare_block_add(&s_b, &s_r);
    CHECK(s_b.replays == 2 && rd_perf_rare_seen(&s_b, 12) == 2 && rd_perf_rare_seen(&s_b, 99) == 0);
    rd_perf_rare_block_reset(&s_b);
    for (uint32_t base = 0; base < RD_PERF_RARE_BLOCK + 64u; base += RD_PERF_RARE_KEYS) {
        rd_perf_rare_replay_reset(&s_r, base);
        for (uint32_t i = 0; i < RD_PERF_RARE_KEYS; i++) {
            rd_perf_rare_replay_add(&s_r, base + i + 1u, 2, 0, 0);
        }
        rd_perf_rare_block_add(&s_b, &s_r);
    }
    CHECK(s_b.n == RD_PERF_RARE_BLOCK && s_b.dropped == 64u);
    CHECK(rd_perf_rare_seen(&s_b, 1) == 1 && rd_perf_rare_seen(&s_b, RD_PERF_RARE_BLOCK + 1u) == 0);
}

/* list 0 of an ordinary replay: the prelit program 60 times, a sprite 4 */
static void ordinary(RdPerfRareReplay *r, uint32_t replay)
{
    rd_perf_rare_replay_reset(r, replay);
    for (int i = 0; i < 60; i++) {
        rd_perf_rare_replay_add(r, 12, 30, 5, 0);
    }
    for (int i = 0; i < 4; i++) {
        rd_perf_rare_replay_add(r, 40, 2, 0, 0);
    }
}

static void testText(void)
{
    char line[1024];
    rd_perf_rare_block_reset(&s_b);
    for (uint32_t k = 1; k <= 99; k++) {
        ordinary(&s_r, k);
        if (k == 50) {
            rd_perf_rare_replay_add(&s_r, 812, 31, 5, 0); /* the skinned draw, once before */
        }
        rd_perf_rare_block_add(&s_b, &s_r);
    }
    /* the stalled replay: the rare one again, 3 lookups */
    ordinary(&s_r, 2417);
    for (int i = 0; i < 3; i++) {
        rd_perf_rare_replay_add(&s_r, 812, 31, 5, 0);
    }
    rd_perf_rare_block_add(&s_b, &s_r);
    rd_perf_rare_text(&s_b, &s_r, RD_PERF_RARE_PCT, vsName, fsName, line, sizeof line);
    CHECK_STR(line,
              "GPU clock: replay 2417's list 0 (the stalled replay): 3 pipelines in 67 lookups; "
              "seen in under 5 % of the block's 100 replays: pipeline 812 "
              "(vu_skin_vs/vu_ps) 3 lookups, in 2 replays; the most used: pipeline 12 "
              "(vu_prelit_vs/vu_ps) 60 lookups, in 100 replays, pipeline 40 "
              "(sprite_vs/sprite_ps) 4 lookups, in 100 replays");
    /* none rare; numbers without names */
    ordinary(&s_r, 2418);
    rd_perf_rare_text(&s_b, &s_r, RD_PERF_RARE_PCT, NULL, NULL, line, sizeof line);
    CHECK(strstr(line, "of the block's 100 replays: none; the most used: pipeline 12 (vs 30 fs 5) "
                       "60 lookups, in 100 replays") != NULL);
    /* nothing asked for in list 0 */
    rd_perf_rare_replay_reset(&s_r, 2419);
    rd_perf_rare_text(&s_b, &s_r, RD_PERF_RARE_PCT, vsName, fsName, line, sizeof line);
    CHECK_STR(line, "GPU clock: replay 2419's list 0 (the stalled replay): 0 pipelines in 0 "
                    "lookups; none asked for in list 0");
    /* a short buffer is cut and terminated */
    char small[24];
    const int w =
        rd_perf_rare_text(&s_b, &s_r, RD_PERF_RARE_PCT, vsName, fsName, small, sizeof small);
    CHECK(w > (int)sizeof small && strlen(small) == sizeof small - 1);
}

int main(void)
{
    testReplay();
    testBlock();
    testText();
    printf("rd_perf_rare_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
