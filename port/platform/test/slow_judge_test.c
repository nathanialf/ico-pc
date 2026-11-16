/*
 * port/platform/test/slow_judge_test.c
 *
 * What slow pictures and steps are judged against, and the game's rate over
 * its own frames (slow_judge.h; issue 179's v0.11.7 log: 18.7-21 ms
 * pictures judged slow against a 16.7 ms refresh on a 25 fps disc, "the game
 * 25.1 of 25.0").  A table of pacing states: the PAL disc at Frame rate
 * Original on 60 Hz (one picture a game frame, 40 ms), 60 pictures a second
 * on 120 Hz (a cap's gap), Uncapped (one a refresh), nothing known; the step
 * bound scaled; the step's CPU words; the game's rate from its frame closes
 * and from the wall time; the line word for word (CPU only).
 */
#include <stdio.h>
#include <string.h>

#include "slow_judge.h"

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

static int near(double a, double b)
{
    return a - b < 1e-6 && b - a < 1e-6;
}

#define HZ60 16666667ull
#define HZ120 8333333ull

typedef struct Case {
    const char *what;
    uint64_t refresh, picture;
    double replayMs, stepMs, targetHz; /* with [dev] slow_step_ms 8 */
} Case;

int main(void)
{
    static const Case k[] = {
        /* Frame rate Original on the PAL disc: two 20 ms steps a frame */
        {"PAL Original on 60 Hz", HZ60, 40000000ull, 40.0, 8.0 * 40000000.0 / HZ60, 25.0},
        /* a cap of 60 on 120 Hz: the gap */
        {"60 on 120 Hz", HZ120, HZ60, HZ60 / 1e6, 8.0 * HZ60 / (double)HZ120, 1e9 / HZ60},
        /* Uncapped, the vsync lock: one a refresh (picture 0) */
        {"Uncapped at 60 Hz", HZ60, 0, HZ60 / 1e6, 8.0, 1e9 / HZ60},
        /* a cap above the refresh in mailbox: its gap under the refresh */
        {"a cap of 144 on 60 Hz", HZ60, 6944444ull, HZ60 / 1e6, 8.0, 1e9 / HZ60},
        /* no display rate known: the picture's, the key as it is */
        {"no refresh, Original", 0, 40000000ull, 40.0, 8.0, 25.0},
        {"nothing known", 0, 0, 0.0, 8.0, 0.0},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        const Case *c = &k[i];
        const double r = slow_judge_replay_ms(c->refresh, c->picture);
        const double s = slow_judge_step_ms(8.0, c->refresh, c->picture);
        const double t = slow_judge_target_hz(c->refresh, c->picture);
        if (!near(r, c->replayMs) || !near(s, c->stepMs) || !near(t, c->targetHz)) {
            printf("FAIL %s: replay %.6f (%.6f) step %.6f (%.6f) target %.6f (%.6f)\n", c->what, r,
                   c->replayMs, s, c->stepMs, t, c->targetHz);
            failures++;
        }
    }
    /* the reporter's pictures: 18.7 ms is not slow at 40, 41 is; at a
       refresh both were */
    CHECK(!(18.7 > slow_judge_replay_ms(HZ60, 40000000ull)));
    CHECK(41.0 > slow_judge_replay_ms(HZ60, 40000000ull));
    CHECK(18.7 > slow_judge_replay_ms(HZ60, 0));
    /* the reporter's steps: 11.0 ms against 19.2 is not slow; the key off */
    CHECK(slow_judge_step_ms(8.0, HZ60, 40000000ull) > 19.19 &&
          slow_judge_step_ms(8.0, HZ60, 40000000ull) < 19.21);
    CHECK(!(11.0 > slow_judge_step_ms(8.0, HZ60, 40000000ull)));
    CHECK(slow_judge_step_ms(0.0, HZ60, 40000000ull) == 0.0);
    CHECK(slow_judge_interval_ns(HZ60, 40000000ull) == 40000000ull);
    CHECK(slow_judge_interval_ns(HZ60, 0) == HZ60);
    CHECK(slow_judge_interval_ns(0, 0) == 0);

    /* the step's CPU words: most of it off the CPU, most on it, not known */
    char t[256];
    (void)slow_judge_step_cpu_text(t, sizeof t, 11.0, 3.2);
    CHECK_STR(t, " (CPU 3.2 ms of it; 7.8 ms off the CPU: the thread waited or was not "
                 "scheduled)");
    (void)slow_judge_step_cpu_text(t, sizeof t, 11.0, 9.1);
    CHECK_STR(t, " (CPU 9.1 ms of it; 1.9 ms off the CPU: the thread's own work)");
    CHECK(slow_judge_step_cpu_text(t, sizeof t, 11.0, 0.0) == 0 && t[0] == '\0');
    {
        char s[12];
        const size_t n = slow_judge_step_cpu_text(s, sizeof s, 11.0, 3.2);
        CHECK(n == sizeof s - 1u && strlen(s) == n);
    }

    /* the game's rate: 250 frames over 250 frame intervals of 40 ms is 25.0
       (v0.11.7 counted 251 in a 10.0 s block: 25.1); the wall time when the
       span is not known; a slowed block (the resyncs' lag inside the span) */
    CHECK(near(slow_judge_game_fps(250, 1000000000ull, 11000000000ull, 10.0), 25.0));
    CHECK(near(slow_judge_game_fps(251, 0, 11000000000ull, 10.0), 25.1));
    CHECK(near(slow_judge_game_fps(234, 1000000000ull, 11000000000ull, 10.0), 23.4));
    CHECK(near(slow_judge_game_fps(5, 2000000000ull, 1000000000ull, 0.0), 0.0));
    CHECK(slow_judge_game_fps(0, 1000000000ull, 2000000000ull, 10.0) == 0.0);

    /* the line */
    (void)slow_judge_text(t, sizeof t, HZ60, 40000000ull, 8.0);
    CHECK_STR(t, "slow pictures judged against 40.0 ms (the larger of the display's refresh 16.7 "
                 "ms and the picture interval in force 40.0 ms), slow steps against 19.2 ms ([dev] "
                 "slow_step_ms 8.0 scaled by 40.0 / 16.7)");
    (void)slow_judge_text(t, sizeof t, HZ60, 0, 0.0);
    CHECK_STR(t, "slow pictures judged against 16.7 ms (the larger of the display's refresh 16.7 "
                 "ms and the picture interval in force 0.0 ms), slow steps off ([dev] "
                 "slow_step_ms 0)");
    (void)slow_judge_text(t, sizeof t, 0, 40000000ull, 8.0);
    CHECK_STR(t, "slow pictures judged against 40.0 ms (the larger of the display's refresh 0.0 "
                 "ms and the picture interval in force 40.0 ms), slow steps against 8.0 ms ([dev] "
                 "slow_step_ms)");
    if (failures) {
        printf("slow_judge_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("slow_judge_test: ok\n");
    return 0;
}
