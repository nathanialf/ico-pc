/*
 * port/platform/test/bench_plan_test.c
 *
 * The rc bench's pure parts (bench_plan.h): the picks of the opening's
 * frames over made-up records, the schedule (the rounds' turned orders and
 * the baseline blocks between), K from a picture time, the type 7 quantiles,
 * a frame's results over made-up samples (the deltas against the bracketing
 * baselines, an excluded sample left out, the drift), the ranking, and the
 * lines and the CSV row word for word.  Every expectation is worked out
 * from the rules in bench_plan.h's head comment.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bench_plan.h"

static int fails;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int near(double a, double b)
{
    return fabs(a - b) < 1e-9;
}

static void testPick(void)
{
    IcoBenchPick p;
    ico_bench_pick_init(&p);
    uint32_t f = 1;
    int got = -2;
    /* 60 lamps pictures arm nothing; the 61st (more than 60 in a row) arms
       the plain lamps kind */
    for (int i = 0; i < 60; i++) {
        got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_LAMPS, 0, f++);
        CHECK(got == -1, "lamps picture %d armed %d", i + 1, got);
    }
    got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_LAMPS, 0, f++);
    CHECK(got == ICO_BENCH_LAMPS_PLAIN && p.armedFrame == 61, "the 61st arms plain (%d, %u)", got,
          p.armedFrame);
    /* one with a volume while plain is armed: nothing (one at a time) */
    got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_LAMPS, 1, f++);
    CHECK(got == -1 && p.armed == ICO_BENCH_LAMPS_PLAIN, "a volume while plain is armed (%d)", got);
    /* a later plain one moves the arm to its frame */
    got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_LAMPS, 0, f++);
    CHECK(got == ICO_BENCH_LAMPS_PLAIN && p.armedFrame == 63, "moved to 63 (%d, %u)", got,
          p.armedFrame);
    ico_bench_pick_done(&p, 1);
    CHECK(p.armed == -1 && (p.used & (1u << ICO_BENCH_LAMPS_PLAIN)), "plain used");
    got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_LAMPS, 1, f++);
    CHECK(got == ICO_BENCH_LAMPS_VOLUME && p.armedFrame == 64, "the volume kind (%d)", got);
    ico_bench_pick_done(&p, 0); /* refused: used all the same */
    got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_LAMPS, 1, f++);
    CHECK(got == -1, "a used kind does not arm again (%d)", got);
    /* a picture with no plan of the same frame continues the run; of a new
       frame it ends it */
    const uint32_t run0 = p.run;
    got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_NONE, 0, f - 1);
    CHECK(got == -1 && p.run == run0, "a later present of the tick (%u, %u)", p.run, run0);
    got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_NONE, 0, f++);
    CHECK(p.run == 0, "a new frame without a plan ends the run (%u)", p.run);
    /* DARK: the 61st arms; outside the opening the arm is dropped */
    for (int i = 0; i < 60; i++) {
        got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_DARK, 0, f++);
    }
    CHECK(got == -1, "60 dark pictures (%d)", got);
    got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_DARK, 0, f++);
    CHECK(got == ICO_BENCH_DARK, "the 61st dark arms (%d)", got);
    got = ico_bench_pick_picture(&p, 0, ICO_BENCH_PLAN_DARK, 0, f++);
    CHECK(got == -1 && p.armed == -1 && p.run == 0, "outside the opening: dropped");
    /* WHOLE: the 21st arms the sun */
    for (int i = 0; i < 20; i++) {
        got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_WHOLE, 1, f++);
    }
    CHECK(got == -1, "20 whole pictures (%d)", got);
    got = ico_bench_pick_picture(&p, 1, ICO_BENCH_PLAN_WHOLE, 1, f++);
    CHECK(got == ICO_BENCH_SUN, "the 21st whole arms the sun (%d)", got);
    CHECK(strcmp(ico_bench_kind_name(ICO_BENCH_PAUSE), "the paused picture") == 0, "kind name");
    CHECK(strcmp(ico_bench_plan_name(2), "lamps") == 0 && strcmp(ico_bench_plan_name(9), "?") == 0,
          "plan names");
}

static void testSchedule(void)
{
    IcoBenchBlock b[64];
    /* m 5, two rounds, a baseline after every 4: round 0 from 1, round 1
       turned by 5 / 2 = 2 places */
    static const uint8_t want[][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {0, 0},
                                      {5, 0}, {0, 0}, {0, 1}, {3, 1}, {4, 1}, {5, 1},
                                      {1, 1}, {0, 1}, {2, 1}, {0, 1}};
    const int n = ico_bench_schedule(6, 2, 4, b, 64);
    CHECK(n == 16, "16 blocks (%d)", n);
    for (int i = 0; i < n && i < 16; i++) {
        CHECK(b[i].variant == want[i][0] && b[i].round == want[i][1],
              "block %d: variant %u round %u, want %u %u", i, b[i].variant, b[i].round, want[i][0],
              want[i][1]);
    }
    CHECK(ico_bench_schedule(1, 2, 4, b, 64) == 2 && b[0].variant == 0 && b[1].round == 1,
          "the baseline alone: a block a round");
    CHECK(ico_bench_schedule(0, 2, 4, b, 64) == 0, "no variant");
    CHECK(ico_bench_schedule(6, ICO_BENCH_ROUNDS + 1, 4, b, 64) == 0, "too many rounds");
    CHECK(ico_bench_schedule(6, 2, 0, b, 64) == 0, "every 0");
    CHECK(ico_bench_schedule(6, 2, 4, b, 15) == 0, "no room");
    /* the window's: 48 variants, 3 rounds fit the block bound */
    static IcoBenchBlock big[ICO_BENCH_BLOCKS];
    CHECK(ico_bench_schedule(ICO_BENCH_VARIANTS, ICO_BENCH_ROUNDS, 4, big, ICO_BENCH_BLOCKS) > 0,
          "the largest schedule fits");
}

static void testK(void)
{
    CHECK(ico_bench_k(20.0, 72, 3, 22000.0, 5, 10) == 10, "20 ms: the most");
    CHECK(ico_bench_k(30.0, 72, 3, 22000.0, 5, 10) == 7, "30 ms: 10 - 3");
    CHECK(ico_bench_k(40.0, 72, 3, 22000.0, 5, 10) == 5, "40 ms: 7 - 3 kept to 5");
    CHECK(ico_bench_k(100.0, 72, 3, 22000.0, 5, 10) == 5, "100 ms: the least");
    CHECK(ico_bench_k(0.0, 72, 3, 22000.0, 5, 10) == 10, "no time: the most");
}

static void testQuantiles(void)
{
    const float v[4] = {4.0f, 1.0f, 3.0f, 2.0f};
    CHECK(near(ico_bench_quantile(v, 4, 0.5), 2.5), "median 2.5");
    CHECK(near(ico_bench_quantile(v, 4, 0.25), 1.75), "q1 1.75");
    CHECK(near(ico_bench_quantile(v, 4, 0.75), 3.25), "q3 3.25");
    const float one = 7.0f;
    const IcoBenchStat s = ico_bench_stat(&one, 1);
    CHECK(near(s.median, 7.0) && near(s.q1, 7.0) && near(s.q3, 7.0) && s.n == 1, "one value");
    CHECK(near(ico_bench_quantile(v, 0, 0.5), 0.0), "none: 0");
}

static void sample(IcoBenchRun *run, int b, float pic, int excluded)
{
    IcoBenchSample *s = &run->s[b][run->ns[b]++];
    memset(s, 0, sizeof(*s));
    s->pictureMs = pic;
    s->gpuMs = 17.0f;
    s->gpuValid = 1;
    s->waitMs = 6.0f;
    s->passes = 14;
    s->draws = 500;
    s->tris = 25000;
    s->plan = 2;
    s->noVolume = 1;
    s->emptyComposites = 3;
    s->crackFills = 1;
    s->excluded = (uint8_t)excluded;
    run->excluded += (uint32_t)excluded;
}

static void testResults(void)
{
    static IcoBenchRun run;
    memset(&run, 0, sizeof(run));
    run.frame = 2;
    run.frames = 4;
    run.kind = ICO_BENCH_LAMPS_PLAIN;
    run.number = 1234;
    run.w = 3;
    run.k = 10;
    run.rounds = 1;
    run.every = 4;
    run.nVariants = 3;
    snprintf(run.info[0].name, sizeof(run.info[0].name), "baseline");
    snprintf(run.info[1].name, sizeof(run.info[1].name), "without the fog");
    run.info[1].knock = 1;
    run.info[1].sum = 2;
    run.info[1].removed = 7;
    snprintf(run.info[2].name, sizeof(run.info[2].name), "without list 4");
    run.info[2].knock = 1;
    run.info[2].sum = 1;
    run.nBlocks = ico_bench_schedule(3, 1, 4, run.block, ICO_BENCH_BLOCKS);
    CHECK(run.nBlocks == 4 && run.block[1].variant == 1 && run.block[2].variant == 2 &&
              run.block[3].variant == 0,
          "B 1 2 B (%d)", run.nBlocks);
    sample(&run, 0, 20.0f, 0);
    sample(&run, 0, 20.0f, 0);
    sample(&run, 0, 21.0f, 0);
    sample(&run, 1, 18.0f, 0);
    sample(&run, 1, 99.0f, 1); /* a pipeline made in it: left out */
    sample(&run, 1, 18.0f, 0);
    sample(&run, 1, 18.0f, 0);
    for (int j = 0; j < 3; j++) {
        sample(&run, 2, 19.5f, 0);
        sample(&run, 3, 21.0f, 0);
    }
    IcoBenchResult res[3];
    IcoBenchDrift d;
    ico_bench_results(&run, res, &d);
    CHECK(near(res[1].delta, -2.5) && res[1].deltaRounds == 1, "fog: 18 - (20 + 21) / 2 (%f)",
          res[1].delta);
    CHECK(near(res[2].delta, -1.0), "list 4: 19.5 - 20.5 (%f)", res[2].delta);
    CHECK(near(res[0].pic.median, 21.0) && near(res[0].pic.q1, 20.25) && res[0].pic.n == 6,
          "the baseline over both blocks (%f %f %d)", res[0].pic.median, res[0].pic.q1,
          res[0].pic.n);
    CHECK(res[1].pic.n == 3 && near(res[1].pic.median, 18.0), "the excluded sample left out");
    CHECK(d.blocks == 2 && near(d.first, 20.0) && near(d.last, 21.0) && near(d.hi - d.lo, 1.0),
          "drift");
    CHECK(ico_bench_variants_ran(res, 3) == 3, "every variant ran");

    char line[2048];
    ico_bench_head_line(line, sizeof(line), &run, 1500, 9000, "mailbox", 1);
    CHECK(strcmp(line, "bench: frame 2 of 4: a lamps picture without a shadow volume (frame 1234, "
                       "1500 commands, 9000 KB): 3 variants, 1 round of 3 warm-up and 10 timed "
                       "replays each, a baseline after every 4; present mode mailbox; GPU stamps "
                       "yes") == 0,
          "head: %s", line);
    ico_bench_variant_line(line, sizeof(line), &run, 1, &res[1]);
    CHECK(strcmp(line, "bench: frame 2 | without the fog | 14 passes, 500 draws, 25000 triangles "
                       "| picture 18.00 ms (18.00-18.00, n 3) | GPU 17.00 ms (17.00-17.00) | wait "
                       "6.00 ms | -2.50 ms against the baselines (rounds -2.50 to -2.50) | removed "
                       "7 | cuts: plan lamps, no volume 1, composites skipped 3, volumes merged 0, "
                       "crack fills 1, SCENE presented 0") == 0,
          "variant: %s", line);
    ico_bench_variant_line(line, sizeof(line), &run, 0, &res[0]);
    CHECK(strstr(line, "| picture 21.00 ms (20.25-21.00, n 6) |") != NULL &&
              strstr(line, "| wait 6.00 ms | the baseline | removed 0 |") != NULL,
          "baseline: %s", line);
    ico_bench_drift_line(line, sizeof(line), &run, &d);
    CHECK(strcmp(line, "bench: frame 2: the baseline over 2 blocks: first 20.00 ms, last 21.00 "
                       "ms, from 20.00 to 21.00 ms (drift 1.00 ms)") == 0,
          "drift: %s", line);
    int order[3];
    const int n = ico_bench_rank(&run, res, order, 3);
    CHECK(n == 2 && order[0] == 1 && order[1] == 2, "rank: fog, then list 4");
    ico_bench_rank_line(line, sizeof(line), &run, res, order, n, 0, 6);
    CHECK(strcmp(line, "bench: frame 2: what removing each part saves (the picture 21.00 ms): "
                       "without the fog 2.50 ms, without list 4 1.00 ms") == 0,
          "rank: %s", line);
    ico_bench_sum_line(line, sizeof(line), &run, res);
    CHECK(strncmp(line,
                  "bench: frame 2: the lists' savings add up to 1.00 ms of the picture's 21.00 "
                  "ms (5 %), the parts' (",
                  strlen("bench: frame 2: the lists' savings add up to 1.00 ms of the picture's "
                         "21.00 ms (5 %), the parts' (")) == 0 &&
              strstr(line, ") to 2.50 ms (12 %); what neither holds") != NULL,
          "sum: %s", line);
    ico_bench_csv_header(line, sizeof(line));
    CHECK(strncmp(line, "frame,kind,number,variant,knock,removed,passes,", 47) == 0, "csv head");
    ico_bench_csv_row(line, sizeof(line), &run, 1, &res[1], &d);
    CHECK(strcmp(line, "2,1,1234,\"without the fog\",1,7,14,500,25000,18.000,18.000,18.000,3,"
                       "17.000,17.000,17.000,3,6.000,-2.500,-2.500,-2.500,1,1.000,lamps,1,3,0,1,"
                       "0,1") == 0,
          "csv row: %s", line);
    /* a cut buffer stays terminated */
    char small[16];
    ico_bench_variant_line(small, sizeof(small), &run, 1, &res[1]);
    CHECK(strlen(small) < sizeof(small), "a short buffer");
}

int main(void)
{
    testPick();
    testSchedule();
    testK();
    testQuantiles();
    testResults();
    if (fails) {
        printf("bench_plan_test: %d failures\n", fails);
        return 1;
    }
    printf("bench_plan_test: ok\n");
    return 0;
}
