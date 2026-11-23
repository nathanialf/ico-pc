/*
 * port/platform/test/perf_secs_test.c
 *
 * Pictures a second by stage (perf_secs.h): a 10 s block at 25 pictures a
 * second with a 0.4 s hitch (its second reads 15), a stage change inside a
 * second (the second keeps the stage of its first step) and the next
 * block's start in the stage then, the part second at the end,
 * pictures before any step ("?"), a quiet end (zeros to the end), seconds
 * past the 64 held counted apart, no second begun (no line) and a short
 * buffer cut, the lines word for word; each second's timed pictures'
 * medians (GPU span, VU triangles, reduction, present pass) and longest
 * present interval, the block's present intervals (p50, p90, the longest,
 * those over 2, 3 and 4 ticks of the game's pace, rounded to whole ticks;
 * none with no tick), the interval across a block's end, a block with no
 * present, records and intervals past the 2048 held counted, word for word.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "perf_secs.h"

static int fails;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int text_is(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("got:  %s\nwant: %s\n", got, want);
        return 0;
    }
    return 1;
}

/* steps every 20 ms in stage from t0 to t1 (ms, t1 left out); a picture
   at every multiple of 40 ms, none in [gap0, gap1) */
static void play(PerfSecs *s, int stage, int t0, int t1, int gap0, int gap1)
{
    for (int t = t0; t < t1; t += 20) {
        perf_secs_step(s, stage, (double)t);
        if (t % 40 == 0 && !(t >= gap0 && t < gap1)) {
            perf_secs_picture(s, (double)t);
        }
    }
}

static PerfSecs s_m; /* 50 KB: not on the stack */

/* a record at t: presented (its present's return at t + 5), timed when
   gpu >= 0 */
static void rec(PerfSecs *s, double t, bool presented, double gpu, double tris)
{
    const PerfSecsRec r = {presented, presented ? t + 5.0 : 0.0, gpu >= 0.0, gpu, 2.4, 2.7, tris};
    perf_secs_record(s, &r, t);
    if (presented) {
        perf_secs_picture(s, t);
    }
}

static void more_cases(void)
{
    PerfSecs *s = &s_m;
    char a[1024], m[4096], iv[512];

    /* 3 s at 25 a second, a 0.2 s hitch in the second second (its one
       interval of 240 ms); the GPU span 40, 50, 60 ms by second; the
       triangles 60000 then 30000 */
    perf_secs_reset(s);
    for (int t = 0; t < 3000; t += 40) {
        perf_secs_step(s, 42, (double)t);
        if (!(t >= 1000 && t < 1200)) {
            rec(s, (double)t, true,
                t < 1000   ? 40.0
                : t < 2000 ? 50.0
                           : 60.0,
                t < 1000 ? 60000.0 : 30000.0);
        }
    }
    CHECK(perf_secs_block_lines(s, 3000.0, 20.0, a, sizeof a, m, sizeof m, iv, sizeof iv) ==
          strlen(a));
    CHECK(text_is(a, "pictures a second: 42: 25 20 25"));
    CHECK(text_is(m, "per second (the timed pictures' medians: GPU span ms, VU triangles, "
                     "reduction ms, present pass ms; then the longest present interval ms): 42: "
                     "40.0/60.0k/2.40/2.70/40 50.0/30.0k/2.40/2.70/240 60.0/30.0k/2.40/2.70/40"));
    CHECK(text_is(iv, "present intervals (the CPU's present returns): 69 in the block, p50 40.0 "
                      "ms, p90 40.0 ms, longest 240.0 ms; over 2 ticks 1, over 3 ticks 1, over 4 "
                      "ticks 1 (a tick 20.0 ms; an interval in whole ticks, rounded)"));
    /* the next block: its first interval from the block before's last
       present; an untimed record; no tick known */
    CHECK(s->lastEndMs == 2965.0 && s->t0Ms == 3000.0);
    rec(s, 3000.0, true, -1.0, 30000.0);
    (void)perf_secs_block_lines(s, 4000.0, 0.0, a, sizeof a, m, sizeof m, iv, sizeof iv);
    CHECK(text_is(a, "pictures a second: 42: 1"));
    CHECK(strstr(m, "): 42: -/30.0k/-/-/40") != NULL);
    CHECK(text_is(iv, "present intervals (the CPU's present returns): 1 in the block, p50 40.0 "
                      "ms, p90 40.0 ms, longest 40.0 ms"));
    /* intervals of 50 and 70 ms at a 20 ms tick: 2.5 rounds to 3 (over 2),
       3.5 to 4 (over 2 and 3); under 1000 triangles said whole */
    perf_secs_reset(s);
    perf_secs_step(s, 3, 0.0);
    rec(s, 0.0, true, 10.0, 900.0);
    rec(s, 50.0, true, 10.0, 900.0);
    rec(s, 120.0, true, 10.0, 900.0);
    (void)perf_secs_block_lines(s, 1000.0, 20.0, a, sizeof a, m, sizeof m, iv, sizeof iv);
    CHECK(strstr(m, "): 3: 10.0/900/2.40/2.70/70") != NULL);
    CHECK(strstr(iv, "2 in the block, p50 50.0 ms, p90 70.0 ms, longest 70.0 ms; over 2 ticks 2, "
                     "over 3 ticks 1, over 4 ticks 0 (") != NULL);
    /* no picture presented: no interval */
    perf_secs_reset(s);
    perf_secs_step(s, 3, 0.0);
    rec(s, 10.0, false, 10.0, 900.0);
    (void)perf_secs_block_lines(s, 1000.0, 20.0, a, sizeof a, m, sizeof m, iv, sizeof iv);
    CHECK(text_is(iv, "present intervals (the CPU's present returns): none in the block"));
    CHECK(strstr(m, "): 3: 10.0/900/2.40/2.70/-") != NULL);
    /* past the 2048 records and intervals held: counted */
    perf_secs_reset(s);
    perf_secs_step(s, 1, 0.0);
    for (int i = 0; i < 2100; i++) {
        rec(s, 10.0 * i, true, 5.0, 100.0);
    }
    (void)perf_secs_block_lines(s, 21000.0, 20.0, a, sizeof a, m, sizeof m, iv, sizeof iv);
    CHECK(strstr(m, "; 52 records past the 2048 held") != NULL);
    CHECK(strstr(iv, "2099 in the block, p50 10.0 ms") != NULL);
    CHECK(strstr(iv, "; 51 past the 2048 held (in the count and the longest only)") != NULL);
    /* a pass that did not run (0 ms: a replay not presented, a scene
       without the reduction) is not a time of the fixed work: left out of
       the medians */
    perf_secs_reset(s);
    perf_secs_step(s, 5, 0.0);
    for (int i = 0; i < 3; i++) {
        const PerfSecsRec z = {false, 0.0, true, 30.0, 0.0, 0.0, 800.0};
        perf_secs_record(s, &z, 10.0 * i);
    }
    {
        const PerfSecsRec one = {false, 0.0, true, 30.0, 2.4, 2.7, 800.0};
        perf_secs_record(s, &one, 50.0);
    }
    (void)perf_secs_block_lines(s, 1000.0, 20.0, a, sizeof a, m, sizeof m, iv, sizeof iv);
    CHECK(strstr(m, "): 5: 30.0/800/2.40/2.70/-") != NULL);
    /* the phone's readings a second (thermal_read.h): the second's last,
       "-" a second with none, the block's thermal line; nothing with no
       reading */
    {
        char ph[1024], blk[512];
        perf_secs_reset(s);
        perf_secs_step(s, 42, 0.0);
        CHECK(perf_secs_phone_lines(s, 500.0, NULL, ph, sizeof ph, blk, sizeof blk) == 0 &&
              ph[0] == '\0' && blk[0] == '\0');
        perf_secs_reset(s);
        perf_secs_step(s, 42, 0.0);
        ThermalSample t;
        thermal_sample_none(&t);
        t.status = 1;
        t.headroom = 0.5;
        t.clusters = 1;
        t.curMhz[0] = 1000;
        t.maxMhz[0] = 1800;
        perf_secs_phone(s, &t, 100.0);
        t.headroom = 0.67; /* the second's last wins */
        perf_secs_phone(s, &t, 900.0);
        t.status = 2;
        t.curMhz[0] = 1200;
        t.maxMhz[0] = 1500;
        perf_secs_phone(s, &t, 2100.0);
        perf_secs_step(s, 40, 2500.0);
        (void)perf_secs_phone_lines(s, 3000.0, NULL, ph, sizeof ph, blk, sizeof blk);
        CHECK(text_is(ph, "phone a second (thermal status/headroom/hottest zone C/each CPU "
                          "cluster's clock:allowed most MHz): 42: 1/0.67/-/1000:1800 - | 40: "
                          "2/0.67/-/1200:1500"));
        CHECK(text_is(blk, "thermal in the block: status 1-2 (changed), headroom 0.67-0.67, no "
                           "zone read; cluster 0 1000-1200 MHz, allowed 1500-1800 MHz; the "
                           "allowed most changed: yes"));
        (void)perf_secs_block_lines(s, 3000.0, 20.0, a, sizeof a, m, sizeof m, iv, sizeof iv);
        CHECK(!s->phoneSet[0]); /* the next block from nothing */
    }
    /* the pictures line alone (perf_secs_block_text) is as before */
    perf_secs_reset(s);
    play(s, 42, 0, 2000, -1, -1);
    (void)perf_secs_block_text(s, 2000.0, a, sizeof a);
    CHECK(text_is(a, "pictures a second: 42: 25 25"));
}

int main(void)
{
    PerfSecs s;
    char b[512];

    /* nothing begun: no line, and the next block starts there */
    perf_secs_reset(&s);
    CHECK(perf_secs_block_text(&s, 500.0, b, sizeof(b)) == 0 && b[0] == '\0');
    CHECK(s.t0Ms == 500.0);

    /* 25 a second, a hitch of 0.4 s in the fourth second */
    perf_secs_reset(&s);
    play(&s, 42, 0, 10000, 3000, 3400);
    CHECK(perf_secs_block_text(&s, 10000.0, b, sizeof(b)) == strlen(b));
    CHECK(text_is(b, "pictures a second: 42: 25 25 25 15 25 25 25 25 25 25"));

    /* a stage change at 4.5 s: the fifth second keeps 42 (its first step);
       the part second's pictures */
    perf_secs_reset(&s);
    play(&s, 42, 0, 4500, -1, -1);
    play(&s, 40, 4500, 6500, -1, -1);
    (void)perf_secs_block_text(&s, 6500.0, b, sizeof(b));
    CHECK(text_is(b, "pictures a second: 42: 25 25 25 25 25 | 40: 25 (and 13 in the block's last "
                     "0.5 s)"));
    /* the next block: from 6.5 s in the stage now */
    CHECK(s.stage == 40 && s.t0Ms == 6500.0);
    play(&s, 40, 6500, 8500, 7500, 8500);
    (void)perf_secs_block_text(&s, 8500.0, b, sizeof(b));
    CHECK(text_is(b, "pictures a second: 40: 25 0"));

    /* pictures before any step; a quiet end: the seconds to the end */
    perf_secs_reset(&s);
    perf_secs_picture(&s, 0.0);
    perf_secs_picture(&s, 500.0);
    perf_secs_step(&s, 3, 1200.0);
    perf_secs_picture(&s, 1300.0);
    (void)perf_secs_block_text(&s, 4200.0, b, sizeof(b));
    CHECK(text_is(b, "pictures a second: ?: 2 | 3: 1 0 0 (and 0 in the block's last 0.2 s)"));

    /* a block under 1 s */
    perf_secs_reset(&s);
    play(&s, 7, 0, 600, -1, -1);
    (void)perf_secs_block_text(&s, 600.0, b, sizeof(b));
    CHECK(text_is(b, "pictures a second: 7: no whole second (and 15 in the block's last 0.6 s)"));

    /* seconds past the 64 held: counted apart */
    perf_secs_reset(&s);
    perf_secs_step(&s, 1, 0.0);
    for (int i = 0; i < 70; i++) {
        perf_secs_picture(&s, 1000.0 * i + 10.0);
    }
    (void)perf_secs_block_text(&s, 70000.0, b, sizeof(b));
    CHECK(strstr(b, "pictures a second: 1: 1 1 1") == b);
    CHECK(strstr(b, " 1 1; 6 seconds past the 64 held, 6 pictures in them") != NULL);

    /* a short buffer: cut and ended */
    {
        char t[16];
        perf_secs_reset(&s);
        play(&s, 42, 0, 5000, -1, -1);
        const size_t n = perf_secs_block_text(&s, 5000.0, t, sizeof(t));
        CHECK(n == sizeof(t) - 1u && strlen(t) == n);
    }

    more_cases();

    if (fails) {
        printf("perf_secs_test: %d failures\n", fails);
        return 1;
    }
    printf("perf_secs_test: ok\n");
    return 0;
}
