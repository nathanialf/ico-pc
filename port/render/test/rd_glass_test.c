/*
 * port/render/test/rd_glass_test.c
 *
 * The content's speed on the glass (rd_glass.h), CPU only:
 *   1. a pair: the step over the display's gap per rule (drawn, the three
 *      computed), its class by the refresh, against the hand-overs and the
 *      GPU's ends (exact at both only), the shares above 1.25 and 1.5 and
 *      below 0.75; not a step: a frame back, on by more than 8, a gap not
 *      positive or over a second;
 *   2. the join: pictures and display times by id in either order, a
 *      present never shown counted, the pairs in the presents' order;
 *   3. the line word for word, and nothing with no picture;
 *   4. the drawn rule's measure equal to its own rule's (the same alphas).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rd_glass.h"
#include "rd_pace_rules.h"
#include "rd_pace_screen.h"

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

/* a picture: tick 32 ms, the three rules' alphas, the hand-over rule drawn */
static RdGlassPic pic(uint32_t id, uint32_t frame, float hand, float gpu, float timer,
                      double handMs, double endMs)
{
    const RdGlassPic p = {id,           frame, hand,   {hand, gpu, timer, -1.0f},
                          RD_RULE_HAND, 32.0,  handMs, endMs,
                          endMs > 0.0,  0.0,   0.0};
    return p;
}

#define MS 1000000ull
#define REF 8000000ull /* a refresh of 8 ms */

static void testPair(void)
{
    RdGlassWindow w;
    memset(&w, 0, sizeof w);
    /* frame 10: the hand-over rule 0.25 then 0.75 (16 ms of content), the
       GPU end 0.25 then 0.75, the timer 0.25 then 0.5 (8 ms); shown 16 ms
       apart (2 refreshes); hand-overs 8 ms apart, GPU ends 16 */
    const RdGlassPic a = pic(1, 10, 0.25f, 0.25f, 0.25f, 100.0, 130.0);
    const RdGlassPic b = pic(2, 10, 0.75f, 0.75f, 0.5f, 108.0, 146.0);
    CHECK(rd_glass_pair(&w, &a, 1000 * MS, &b, 1016 * MS, REF));
    CHECK(w.pairs == 1 && w.gaps[2] == 1 && w.refreshNs == REF && w.drawnRule == RD_RULE_HAND);
    /* drawn: 16 / 16 = 1; the timer 8 / 16 = 0.5 (below 0.75) */
    CHECK(w.glass[RD_GLASS_DRAWN][2].n == 1 && w.glass[RD_GLASS_DRAWN][2].sum == 10000);
    CHECK(w.glass[3][5].sum == 5000 && w.glass[3][5].below075 == 1);
    /* against the hand-overs 16 / 8 = 2 (above 1.5), the GPU's ends 16/16 */
    CHECK(w.hand[1].sum == 20000 && w.hand[1].above15 == 1 && w.hand[1].above125 == 1);
    CHECK(w.gpu[2].sum == 10000 && w.gpu[2].n == 1);
    /* a GPU end not exact at one: no GPU-end step */
    RdGlassPic c = pic(3, 11, 0.25f, 0.25f, 0.25f, 120.0, 160.0);
    c.gpuExact = false;
    CHECK(rd_glass_pair(&w, &b, 1016 * MS, &c, 1024 * MS, REF));
    CHECK(w.gpu[1].n == 1 && w.hand[1].n == 2 && w.gaps[1] == 1);
    /* a rule not computed (the present clock's picture): its measure left out */
    RdGlassPic d = pic(4, 11, 0.5f, -1.0f, -1.0f, 0.0, 0.0);
    d.rule = RD_RULE_CLOCK;
    d.alpha[0] = -1.0f;
    CHECK(rd_glass_pair(&w, &c, 1024 * MS, &d, 1032 * MS, 0));
    CHECK(w.glass[RD_GLASS_DRAWN][5].n == 3 && w.glass[1][5].n == 2 && w.hand[0].n == 2);
    /* not steps */
    const RdGlassPic back = pic(5, 9, 0.5f, 0.5f, 0.5f, 0.0, 0.0);
    const RdGlassPic jump = pic(6, 20, 0.5f, 0.5f, 0.5f, 0.0, 0.0);
    CHECK(!rd_glass_pair(&w, &d, 1032 * MS, &back, 1040 * MS, REF));
    CHECK(!rd_glass_pair(&w, &d, 1032 * MS, &jump, 1040 * MS, REF));
    CHECK(!rd_glass_pair(&w, &d, 1032 * MS, &c, 1032 * MS, REF));
    CHECK(!rd_glass_pair(&w, &a, 1000 * MS, &b, 2001 * MS, REF));
    CHECK(w.pairs == 3);
}

static void testJoin(void)
{
    rd_glass_reset();
    RdGlassWindow w;
    /* ids 1-4: 1 and 2 pictures first, then their times; 3 never shown (no
       time); 4's time before its picture */
    RdGlassPic p1 = pic(1, 10, 0.2f, 0.2f, 0.2f, 100.0, 130.0);
    RdGlassPic p2 = pic(2, 10, 0.7f, 0.7f, 0.7f, 116.0, 146.0);
    RdGlassPic p3 = pic(3, 11, 0.2f, 0.2f, 0.2f, 133.0, 160.0);
    RdGlassPic p4 = pic(4, 11, 0.7f, 0.7f, 0.7f, 150.0, 178.0);
    rd_glass_join(&p1);
    rd_glass_join(&p2);
    rd_glass_time(1, 1000 * MS, REF, 0, 0);
    rd_glass_time(2, 1016 * MS, REF, 0, 0);
    rd_glass_join(&p3);
    rd_glass_time(4, 1048 * MS, REF, 0, 0);
    rd_glass_join(&p4);
    /* a picture with no id: not counted */
    RdGlassPic p0 = pic(0, 11, 0.7f, 0.7f, 0.7f, 150.0, 178.0);
    rd_glass_join(&p0);
    rd_glass_take(&w);
    CHECK(w.pictures == 4 && w.shown == 3 && w.unseen == 1 && w.pairs == 2);
    /* 1 -> 2: 0.5 tick (16 ms) in 16 ms; 2 -> 4: 1.0 tick (32 ms) in 32 ms
       (4 refreshes) */
    CHECK(w.gaps[2] == 1 && w.gaps[4] == 1 && w.glass[0][5].sum == 20000);
    rd_glass_take(&w);
    CHECK(w.pictures == 0 && w.pairs == 0);
}

static void testLine(void)
{
    RdGlassWindow w;
    memset(&w, 0, sizeof w);
    char out[4096];
    CHECK(!rd_glass_format(&w, out, sizeof out) && out[0] == '\0');
    const RdGlassPic a = pic(1, 10, 0.25f, 0.25f, 0.25f, 100.0, 130.0);
    const RdGlassPic b = pic(2, 10, 0.75f, 0.75f, 0.5f, 108.0, 146.0);
    (void)rd_glass_pair(&w, &a, 1000 * MS, &b, 1016 * MS, REF);
    w.pictures = 2;
    w.shown = 2;
    CHECK(rd_glass_format(&w, out, sizeof out));
    CHECK_STR(out,
              "on the glass in 10 s: 2 pictures with a display id, 2 shown with the display's "
              "time, 0 presented with no time back (replaced before shown, or not reported), 1 "
              "pairs; the display's gaps by refreshes of 8.00 ms: under half 0, 1 0, 2 1, 3 0, 4 "
              "or more 0; the content's speed on the glass (its step over the display's gap, "
              "1.00 real time), drawn (the hand-over): mean 1.00 (deviation 0.000), above 1.25 "
              "0.0 %, above 1.5 0.0 %, below 0.75 0.0 %; by the display's gap: 2 refreshes 1.00 "
              "(0.000); against the hand-overs 2.00 (0.000) over 1, against the GPU's ends 1.00 "
              "(0.000) over 1; by the hand-over (drawn): mean 1.00 (deviation 0.000), above 1.25 "
              "0.0 %, above 1.5 0.0 %, below 0.75 0.0 %; by the display's gap: 2 refreshes 1.00 "
              "(0.000); against the hand-overs 2.00 (0.000) over 1, against the GPU's ends 1.00 "
              "(0.000) over 1; by the GPU's predicted end (not drawn): mean 1.00 (deviation "
              "0.000), above 1.25 0.0 %, above 1.5 0.0 %, below 0.75 0.0 %; by the display's "
              "gap: 2 refreshes 1.00 (0.000); against the hand-overs 2.00 (0.000) over 1, "
              "against the GPU's ends 1.00 (0.000) over 1; by the own timer (not drawn): mean "
              "0.50 (deviation 0.000), above 1.25 0.0 %, above 1.5 0.0 %, below 0.75 100.0 %; by "
              "the display's gap: 2 refreshes 0.50 (0.000); against the hand-overs 1.00 (0.000) "
              "over 1, against the GPU's ends 0.50 (0.000) over 1");
    char small[16];
    CHECK(rd_glass_format(&w, small, sizeof small) && strlen(small) == 15);
}

/* the screen's rule: its alpha a fifth measure, its prediction scored at
   the join (on it, one late, one early, further) and sent to its model;
   the display's own figures (presentMargin, earliestPresentTime) */
static void testScreen(void)
{
    rd_glass_reset();
    rd_pace_screen_drop();
    RdGlassWindow w;
    /* refresh 8 ms; predicted at 1000, 1008, 1016, 1040 ms; shown at 1000
       (on it), 1016 (one late), 1016 + 8 = 1024 for a prediction of 1032
       (one early) and 1064 for 1040 (three: further) */
    static const double kPred[4] = {1000.0, 1008.0, 1032.0, 1040.0};
    static const uint64_t kShown[4] = {1000, 1016, 1024, 1064};
    for (uint32_t i = 0; i < 4; i++) {
        RdGlassPic p = pic(i + 1u, 10 + i / 2u, 0.25f + 0.5f * (float)(i % 2u), 0.2f, 0.2f,
                           100.0 + 8.0 * i, 0.0);
        p.alpha[3] = 0.25f + 0.25f * (float)i;
        p.screenEndMs = 900.0 + 8.0 * i;
        p.screenAtMs = kPred[i];
        rd_glass_join(&p);
        rd_glass_time(i + 1u, kShown[i] * MS, REF, i == 3u ? (kShown[i] - 8) * MS : kShown[i] * MS,
                      2 * MS);
    }
    rd_glass_take(&w);
    CHECK(w.scored == 4 && w.hit == 1 && w.late == 1 && w.early == 1 && w.further == 1);
    CHECK(w.marginN == 4 && w.marginSumUs == 8000 && w.earlyN == 1 && w.earlySumUs == 8000);
    /* the screen's measure (1 + RD_RULE_SCREEN): 1000 -> 1016 a quarter
       tick (8 ms) over 16 ms */
    CHECK(w.glass[1 + RD_RULE_SCREEN][5].n == 3 && w.glass[1 + RD_RULE_SCREEN][2].sum == 5000);
    /* each shown picture with a predicted end went to the model's ring */
    RdPaceScreen m;
    rd_pace_screen_reset(&m);
    CHECK(rd_pace_screen_drain(&m) == 4u && m.sN == 4u && m.s[0].endPredMs == 900.0 &&
          m.s[0].actualMs == 1000.0 && m.s[0].refreshMs == 8.0);
    char out[4096];
    CHECK(rd_glass_format(&w, out, sizeof out));
    CHECK(strstr(out, "; by the screen's predicted refresh (not drawn): ") != NULL);
    CHECK(strstr(out, "; the display's own figures over 4 shown: present margin 2.00 ms on "
                      "average, earliest present half a refresh or more before the time shown 1 "
                      "(by 8.00 ms on average)") != NULL);
}

int main(void)
{
    printf("rd_glass_test\n");
    testPair();
    testJoin();
    testLine();
    testScreen();
    if (failures) {
        printf("rd_glass_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_glass_test: ok\n");
    return 0;
}
