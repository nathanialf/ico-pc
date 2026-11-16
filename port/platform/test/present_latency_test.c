/*
 * port/platform/test/present_latency_test.c
 *
 * The present's latency in a 10 s block (present_latency.h, issue 123):
 * the mean and longest from the hand-over (or the replay's start), the
 * part before the present and the present's own, the driver's call split
 * at the picture's GPU end (all before it, all after it, across it), the
 * records left out (no present end, an end before the start), the line
 * word for word (no queue priority clause since v0.11.8: the priority was
 * taken off), and nothing with no present (CPU only).
 */
#include <stdio.h>
#include <string.h>

#include "present_latency.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int text_is(const char *got, const char *want, int line)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL line %d:\n got  %s\n want %s\n", line, got, want);
        failures++;
        return 0;
    }
    return 1;
}

int main(void)
{
    PresentLatency p;
    char t[1024];
    memset(&p, 0, sizeof p);
    CHECK(present_latency_text(&p, t, sizeof t) == 0 && t[0] == '\0');

    /* a: handed over at 1000, presented by 1030 (the present 16 ms, its
       call 12 ms from 1018 to 1030), the slot's wait 10 ms, the acquire
       0.4 ms; the picture's GPU end at 1026: 8 ms of the call before it,
       4 after */
    const PresentLatencyRec a = {100, 1000.0, true, 1030.0, 16.0, 12.0, 10.0, 0.4, 1026.0};
    /* b: from the replay's start (not handed over) at 2000 to 2050, the
       present 40 ms, its call 40 ms from 2010; the GPU end at 2000, before
       the call: none of it a wait for the GPU */
    const PresentLatencyRec b = {101, 2000.0, false, 2050.0, 40.0, 40.0, 0.0, 0.0, 2000.0};
    /* c: the hang's shape: 400 ms, the call 365 ms from 3035, the GPU end
       at 3500 after the call's return: all of the call while the GPU ran */
    const PresentLatencyRec c = {102, 3000.0, true, 3400.0, 365.0, 365.0, 20.0, 0.0, 3500.0};
    /* d: the GPU end not seen exactly (0): in the means, not the split */
    const PresentLatencyRec d = {103, 4000.0, true, 4030.0, 9.0, 9.0, 16.0, 0.0, 0.0};
    /* left out: no present end; an end before the start */
    const PresentLatencyRec e = {104, 5000.0, true, 0.0, 10.0, 10.0, 0.0, 0.0, 0.0};
    const PresentLatencyRec f = {105, 6000.0, true, 5990.0, 10.0, 10.0, 0.0, 0.0, 0.0};
    present_latency_add(&p, &a);
    present_latency_add(&p, &b);
    present_latency_add(&p, &c);
    present_latency_add(&p, &d);
    present_latency_add(&p, &e);
    present_latency_add(&p, &f);
    CHECK(p.n == 4 && p.handed == 3 && p.split == 3);
    /* latencies 30, 50, 400, 30: mean 127.5, longest 400 (replay 102) */
    CHECK(p.sum == 510.0 && p.maxMs == 400.0 && p.maxReplay == 102);
    /* before the present: 14, 10, 35, 21 (80 / 4 = 20.0); the present 16,
       40, 365, 9 (430 / 4 = 107.5); calls 426 / 4 = 106.5; the slot's
       waits 46 / 4 = 11.5, acquires 0.4 / 4 = 0.1 */
    CHECK(p.before == 80.0 && p.present == 430.0 && p.call == 426.0 && p.fence == 46.0);
    /* the split over a, b, c: 8 + 0 + 365 = 373 before the GPU end (124.33),
       4 + 40 + 0 = 44 after (14.67) */
    CHECK(p.callGpu == 373.0 && p.callAfter == 44.0);
    (void)present_latency_text(&p, t, sizeof t);
    text_is(t,
            "present latency in the block: 4 presents (3 from the hand-over, 1 from the replay's "
            "start), mean 127.5 ms, longest 400.0 ms (replay 102); of the mean: before the present "
            "20.0 ms (the slot's wait for the GPU 11.5 ms, the acquire 0.1 ms), the present 107.5 "
            "ms, the driver's present call 106.5 ms of it; over 3 presents with the picture's GPU "
            "end seen, the call 124.3 ms while the picture's GPU work still ran and 14.7 ms after "
            "its end (the display's)",
            __LINE__);
    CHECK(strstr(t, "priority") == NULL);

    /* no GPU end seen: the call not split; all handed over */
    memset(&p, 0, sizeof p);
    present_latency_add(&p, &d);
    (void)present_latency_text(&p, t, sizeof t);
    text_is(t,
            "present latency in the block: 1 presents (1 from the hand-over), mean 30.0 ms, "
            "longest 30.0 ms (replay 103); of the mean: before the present 21.0 ms (the slot's "
            "wait for the GPU 16.0 ms, the acquire 0.0 ms), the present 9.0 ms, the driver's "
            "present call 9.0 ms of it; the picture's GPU end not seen exactly (its slot's wait "
            "did not block): the call not split",
            __LINE__);

    if (failures) {
        printf("present_latency_test: %d failures\n", failures);
        return 1;
    }
    printf("present_latency_test: ok\n");
    return 0;
}
