/*
 * port/render/test/rd_even_test.c
 *
 * The pictures' evenness line (rd_even.h), CPU only:
 *   1. ten made-up presents: GPU ends 8.125, 16.125, 24.125, 2.125 and
 *      40.125 ms apart (the middles of their quarter-ms bins: 1, 2, 3,
 *      under half and 5 refreshes of 8 ms), then one end a bound, one
 *      present with no end, a frame number that went back (a chain
 *      restart) and one more 8.125 ms on; contents 10.25 ... (frame +
 *      alpha) stepping 0.25, 0.25, 0.5, -0.125, 0.875, 0.5, 0.25 and 0.25
 *      ticks; four of them handed over (the alpha's sample 10, 18, 20 and
 *      28 ms before their GPU's end, the hand-over 9, 16, 19 and 27 ms,
 *      drains of 1, 2, 1 and 1 ms); five
 *      clock notes (two clamps below 0, one above 0.999, a forward reset
 *      and two back, one held 7.5 ms): the line word for word;
 *   2. taken again: nothing, no line;
 *   3. one more present with the refresh not known: the gaps not on a
 *      grid, no hand-over, the line word for word;
 *   4. the hand-over's stamps taken once.
 */
#include <stdio.h>
#include <string.h>

#include "rd_even.h"

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

static void pic(uint32_t frame, float alpha, double ask, double hand, double end, bool exact)
{
    const RdEvenPic p = {frame, alpha, ask, hand, end, exact};
    rd_even_picture(&p);
}

static RdEvenWindow s_w;

int main(void)
{
    printf("rd_even_test\n");
    char out[2048];

    /* 1. the folds and the line */
    pic(10, 0.25f, 990.0, 991.0, 1000.0, true);
    pic(10, 0.5f, 990.125, 992.125, 1008.125, true);
    pic(10, 0.75f, 1004.25, 1005.25, 1024.25, true);
    pic(11, 0.25f, 0.0, 0.0, 1048.375, true);
    pic(11, 0.125f, 0.0, 0.0, 1050.5, true);
    pic(12, 0.0f, 0.0, 0.0, 1090.625, true);
    pic(12, 0.5f, 0.0, 0.0, 1100.0, false);
    pic(12, 0.75f, 0.0, 0.0, 0.0, false);
    pic(3, 0.5f, 0.0, 0.0, 1200.0, true);
    pic(3, 0.75f, 1180.125, 1181.125, 1208.125, true);
    rd_even_clock_note(0.5, 0, 0.0);
    rd_even_clock_note(-0.1, 1, 0.0);
    rd_even_clock_note(-0.25, 0, 0.0);
    rd_even_clock_note(1.2, -1, 0.0);
    rd_even_clock_note(0.3, -1, 7.5);
    rd_even_rebase_note(5.0, 3.0, 2.0, false);
    rd_even_rebase_note(1.0, 3.25, -2.25, true);
    rd_even_take(&s_w);
    CHECK(s_w.rebN == 2 && s_w.rebHeld == 1 && s_w.rebBefore[42] == 1 && s_w.rebBefore[34] == 1 &&
          s_w.rebAfter[38] == 2 && s_w.rebShift[36] == 1 && s_w.rebShift[27] == 1);
    CHECK(s_w.pictures == 10 && s_w.steps == 8 && s_w.breaks == 1 && s_w.bound == 1 &&
          s_w.noEnd == 1 && s_w.latN == 4 && s_w.drainN == 4 && s_w.drainUs == 5000);
    CHECK(s_w.gap[32] == 2 && s_w.gap[64] == 1 && s_w.gap[96] == 1 && s_w.gap[8] == 1 &&
          s_w.gap[160] == 1 && s_w.gapStep[32] == 5000 && s_w.gapStep[8] == -1250);
    CHECK(s_w.stepSum == 27500 && s_w.stepSum2 == 153125000u && s_w.stepMax == 8750 &&
          s_w.back == 1 && s_w.backMax == 1250);
    CHECK(s_w.lat[10] == 1 && s_w.lat[18] == 1 && s_w.lat[20] == 1 && s_w.lat[28] == 1);
    CHECK(s_w.latHandN == 4 && s_w.latHand[9] == 1 && s_w.latHand[16] == 1 &&
          s_w.latHand[19] == 1 && s_w.latHand[27] == 1);
    CHECK(s_w.samples == 5 && s_w.clampLo == 2 && s_w.clampLoMax == 2500 && s_w.clampHi == 1 &&
          s_w.clampHiMax == 2010 && s_w.resetFwd == 1 && s_w.resetBack == 2 && s_w.held == 1 &&
          s_w.heldMaxUs == 7500);
    CHECK(rd_even_format(&s_w, 8.0, out, sizeof out));
    CHECK_STR(
        out,
        "evenness in 10 s: 10 pictures; the GPU's ends apart (6 with both ends exact, 1 "
        "with a bound at one, 1 without a GPU end) by refreshes of 8.00 ms: under half a "
        "refresh 1, 1 refresh 2, 2 refreshes 1, 3 refreshes 1, 4 or more 1; the content's "
        "step over them in ticks (mean, deviation): under half a refresh -0.125 (0.000), 1 "
        "refresh 0.250 (0.000), 2 refreshes 0.250 (0.000), 3 refreshes 0.500 (0.000), 4 or "
        "more 0.875 (0.000); every step (8): mean 0.344 (deviation 0.271), 2 of them over "
        "gaps not on the grid: mean 0.375 (deviation 0.125), the largest 0.875, back 1 times (the most 0.125), 1 chain restarts; the alpha clamped at 0 2 "
        "times (the most 0.250 below), at 0.999 1 times (the most 0.201 above); the "
        "present clock: 5 samples, resets 1 forward and 2 back, 1 held instead of going "
        "back within a frame pair (the most 7.50 ms); the alpha's sample to the "
        "GPU's end over 4 pictures: 10th 10.5, median 18.5, 90th 28.5 ms, the hand-over to "
        "it 10th 9.5, median 16.5, 90th 27.5 ms (the drain before the hand-over 1.25 ms on "
        "average over 4); re-based to the hand-over: 2 pictures (the shift 10th -2.25, "
        "median -2.25, 90th 2.25 ms; 1 held at the frame's last value), the content's "
        "moment to the hand-over 10th 3.25, median 3.25, 90th 3.25 ms (at the sample 10th "
        "1.25, median 1.25, 90th 5.25)");
    /* a short buffer: cut, still terminated */
    char small[16];
    CHECK(rd_even_format(&s_w, 8.0, small, sizeof small) && strlen(small) == 15);

    /* 2. taken again: empty */
    rd_even_take(&s_w);
    CHECK(s_w.pictures == 0 && s_w.samples == 0 && s_w.steps == 0 && s_w.gap[32] == 0 &&
          s_w.rebN == 0 && s_w.rebAfter[38] == 0 && s_w.lat[10] == 0 && s_w.stepMax == 0);
    CHECK(!rd_even_format(&s_w, 8.0, out, sizeof out) && out[0] == '\0');

    /* 3. the chain goes on from the last present (frame 3 at 0.75, its end
     * at 1208.125 ms): one step of 0.25 over 8 ms; no refresh known */
    pic(4, 0.0f, 0.0, 0.0, 1216.125, true);
    rd_even_take(&s_w);
    CHECK(s_w.gap[32] == 1 && s_w.steps == 1 && s_w.breaks == 0);
    CHECK(rd_even_format(&s_w, 0.0, out, sizeof out));
    CHECK_STR(out,
              "evenness in 10 s: 1 pictures; the GPU's ends apart (1 with both ends exact, 0 "
              "with a bound at one, 0 without a GPU end): the refresh not known, not put on its "
              "grid; every step (1): mean 0.250 (deviation 0.000), the largest 0.250, back 0 "
              "times (the most 0.000), 0 chain restarts; the alpha clamped at 0 0 times (the "
              "most 0.000 below), at 0.999 0 times (the most 0.000 above); the present clock: 0 "
              "samples, resets 0 forward and 0 back, 0 held instead of going back within a frame "
              "pair (the most 0.00 ms); the alpha's sample to the GPU's end: not "
              "measured (no exact GPU end of a picture handed over)");

    /* 4. the hand-over's stamps */
    double ask = -1.0, hand = -1.0;
    rd_even_hand_note(5.0, 6.5);
    rd_even_hand_take(&ask, &hand);
    CHECK(ask == 5.0 && hand == 6.5);
    rd_even_hand_take(&ask, &hand);
    CHECK(ask == 0.0 && hand == 0.0);

    if (failures) {
        printf("rd_even_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("rd_even_test: ok\n");
    return 0;
}
