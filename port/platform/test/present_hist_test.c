/*
 * port/platform/test/present_hist_test.c
 *
 * The 10 s block's histogram of the driver's present call (present_hist.h,
 * issue 123):
 *   rows     a call is counted in every row it is over, the limits
 *            themselves are not over (20 ms is not over 20 ms);
 *   reporter the v0.10.0 log's block: 750 presents of 8.8 ms, 247 of them
 *            9-15 ms, five of 344-365 ms: over 20/50/100/300 is 5 each;
 *   text     the line's words, "" for an empty block, a small buffer cut;
 *   bad      negative and NaN times are not counted;
 *   reset    a reset block is empty;
 *   split    the blocked line's split (present_split_text) and the after
 *            line (present_after_text), word for word: a reporter's log's
 *            stall (replay 2941, 359 ms, GPU 67.9 ms against 26.7 ms), the
 *            call not timed apart, no GPU span, the block's first timed
 *            replay, the lock without a present call, "other" never under 0,
 *            a gap under 0, a gap across untimed replays, no gap,
 *            a short buffer;
 *   lines    the stall lines' threshold (present_stall_lines): a present
 *            gets the after, around and stalled-replay lines from 250 ms
 *            on, never from 50 to 250 ms (a reporter's 57-250 ms presents
 *            spent half of each line's first 8) nor for NaN.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "present_hist.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
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

static void split_texts(void)
{
    char line[400];
    PresentStallRec r;

    memset(&r, 0, sizeof r);
    r.replay = 2941;
    r.presentMs = 358.8;
    r.split = 1;
    r.callMs = 358.6;
    r.lockMs = 0.02;
    r.pollMs = 0.04;
    r.gpuValid = 1;
    r.gpuMs = 67.9;
    r.gapValid = 1;
    r.gapMs = 0.4;
    present_split_text(&r, 300, 26.7, line, sizeof line);
    CHECK(text_is(line, "inside vkQueuePresentKHR 358.6 ms, the swapchain lock 0.0 ms, the surface "
                        "poll 0.0 ms, other 0.1 ms; the replay's GPU span 67.9 ms against the "
                        "block's 26.7 ms so far, the GPU idle 0.4 ms before it"));
    /* the block's first timed replay; a gap under 0 */
    r.gapMs = -1.3;
    present_split_text(&r, 0, 0.0, line, sizeof line);
    CHECK(text_is(line, "inside vkQueuePresentKHR 358.6 ms, the swapchain lock 0.0 ms, the surface "
                        "poll 0.0 ms, other 0.1 ms; the replay's GPU span 67.9 ms (the block's "
                        "first timed replay), begun 1.3 ms before the one before ended"));
    /* the lock taken, no present call made (no slot, or the present-signal
       submit failed): the lock's wait kept, the rest as other */
    r.split = 2;
    r.presentMs = 352.0;
    r.lockMs = 351.5;
    r.callMs = 0.0;
    r.pollMs = 0.0;
    present_split_text(&r, 0, 0.0, line, sizeof line);
    CHECK(text_is(line, "no present call made: the swapchain lock 351.5 ms, other 0.5 ms; the "
                        "replay's GPU span 67.9 ms (the block's first timed replay), begun 1.3 ms "
                        "before the one before ended"));
    /* other never under 0 (the parts timed apart past the whole) */
    r.split = 1;
    r.presentMs = 10.0;
    r.callMs = 10.2;
    r.lockMs = 0.0;
    present_split_text(&r, 0, 0.0, line, sizeof line);
    CHECK(strstr(line, "the surface poll 0.0 ms, other 0.0 ms;") != NULL);
    /* not timed apart, no gap */
    r.split = 0;
    r.gapValid = 0;
    present_split_text(&r, 12, 30.0, line, sizeof line);
    CHECK(text_is(line, "the call not timed apart on this graphics API; the replay's GPU span 67.9 "
                        "ms against the block's 30.0 ms so far, the GPU's time before it not "
                        "measured"));
    /* no GPU span: no gap words either */
    r.gpuValid = 0;
    r.gapValid = 1;
    present_split_text(&r, 12, 30.0, line, sizeof line);
    CHECK(text_is(line, "the call not timed apart on this graphics API; the replay's GPU span not "
                        "timed"));

    /* the after line: the replay after it, its acquire holding the stall */
    memset(&r, 0, sizeof r);
    r.replay = 2942;
    r.waitMs = 0.3;
    r.fenceMs = 0.2;
    r.acquireMs = 298.7;
    r.gpuValid = 1;
    r.gpuMs = 26.9;
    r.gapValid = 1;
    r.gapMs = 301.2;
    present_after_text(2941, 352.0, &r, line, sizeof line);
    CHECK(text_is(line,
                  "after the present that blocked (replay 2941, 352 ms): replay 2942 waited "
                  "0.3 ms for its frame slot (fence 0.2), acquired its image in 298.7 ms, GPU "
                  "span 26.9 ms, the GPU idle 301.2 ms before it"));
    r.gapSkipped = 1;
    present_after_text(2941, 352.0, &r, line, sizeof line);
    CHECK(text_is(line,
                  "after the present that blocked (replay 2941, 352 ms): replay 2942 waited "
                  "0.3 ms for its frame slot (fence 0.2), acquired its image in 298.7 ms, GPU "
                  "span 26.9 ms, the GPU idle 301.2 ms before it (across 1 untimed replay)"));
    r.gapSkipped = 2;
    r.gpuValid = 0;
    present_after_text(2941, 352.0, &r, line, sizeof line);
    CHECK(text_is(line,
                  "after the present that blocked (replay 2941, 352 ms): replay 2942 waited "
                  "0.3 ms for its frame slot (fence 0.2), acquired its image in 298.7 ms, GPU "
                  "span not timed"));
    r.gpuValid = 1;
    present_after_text(2941, 352.0, &r, line, sizeof line);
    CHECK(strstr(line, "(across 2 untimed replays)") != NULL);
    /* a short buffer: cut and terminated, the whole length returned */
    {
        char small[16];
        const int n = present_after_text(2941, 352.0, &r, small, sizeof small);
        CHECK(n > 16 && strlen(small) == 15);
    }
}

int main(void)
{
    PresentHist h;
    char line[256];

    present_hist_reset(&h);
    CHECK(present_hist_text(&h, line, sizeof line) == 0 && line[0] == '\0');
    present_hist_add(&h, 20.0);
    CHECK(h.n == 1 && h.over[0] == 0);
    present_hist_add(&h, 20.5);
    CHECK(h.over[0] == 1 && h.over[1] == 0);
    present_hist_add(&h, 50.5);
    CHECK(h.over[0] == 2 && h.over[1] == 1 && h.over[2] == 0);
    present_hist_add(&h, 120.0);
    CHECK(h.over[2] == 1 && h.over[3] == 0);
    present_hist_add(&h, 300.5);
    CHECK(h.over[0] == 4 && h.over[1] == 3 && h.over[2] == 2 && h.over[3] == 1);
    CHECK(h.maxMs == 300.5);
    CHECK(present_hist_limit(0) == 20.0 && present_hist_limit(3) == 300.0);
    CHECK(present_hist_limit(-1) == 0.0 && present_hist_limit(4) == 0.0);

    /* bad times */
    present_hist_add(&h, -1.0);
    present_hist_add(&h, NAN);
    CHECK(h.n == 5);

    /* the reporter's block */
    present_hist_reset(&h);
    CHECK(h.n == 0 && h.maxMs == 0.0 && h.totalMs == 0.0);
    for (int i = 0; i < 503; i++) {
        present_hist_add(&h, 8.8);
    }
    for (int i = 0; i < 247; i++) {
        present_hist_add(&h, 9.0 + (double)(i % 7));
    }
    {
        const double big[5] = {344.0, 351.0, 360.0, 362.0, 365.0};
        for (int i = 0; i < 5; i++) {
            present_hist_add(&h, big[i]);
        }
    }
    CHECK(h.n == 755);
    CHECK(h.over[0] == 5 && h.over[1] == 5 && h.over[2] == 5 && h.over[3] == 5);
    CHECK(h.maxMs == 365.0);
    present_hist_text(&h, line, sizeof line);
    CHECK(strncmp(line,
                  "present calls: 755 timed (over 20 ms 5, over 50 ms 5, over 100 ms 5, "
                  "over 300 ms 5), longest 365 ms, total ",
                  strlen("present calls: 755 timed (over 20 ms 5, over 50 ms 5, over 100 ms 5, "
                         "over 300 ms 5), longest 365 ms, total ")) == 0);
    /* a small buffer is cut and terminated */
    {
        char small[12];
        present_hist_text(&h, small, sizeof small);
        CHECK(strlen(small) == 11 && strncmp(small, "present cal", 11) == 0);
    }
    split_texts();
    /* the stall lines' threshold */
    CHECK(PRESENT_STALL_LINE_MS == 250.0);
    CHECK(!present_stall_lines(50.5) && !present_stall_lines(57.0) && !present_stall_lines(249.9));
    CHECK(present_stall_lines(250.0) && present_stall_lines(337.0) && present_stall_lines(365.0));
    CHECK(!present_stall_lines(NAN) && !present_stall_lines(-1.0));
    printf("present_hist_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
