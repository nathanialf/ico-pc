/*
 * port/platform/test/gpu_hang_test.c
 *
 * The GPU hang limit (gpu_hang.h):
 *   limit    half the watchdog: 30 s gives 15 s, 0 turns it off, under
 *            2 s is 2 s (watchdog 1, 2, 3, 4), large values halved;
 *   step     issue 123's wait (submission 263, the newest seen finished
 *            262) polled every 250 ms fires once at 15 s and not before;
 *   finished a wait whose submission finished, or no wait, clears the
 *            time; the next wait counts from 0;
 *   other    a wait for another submission starts again from 0 (one
 *            frame's waits each short never add up);
 *   paused   time not counted (the watchdog paused, a suspend gap) does
 *            not reach the limit;
 *   off      a limit of 0 never fires, however long the wait.
 */
#include <stdio.h>
#include "gpu_hang.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void testLimit(void)
{
    CHECK(ico_gpu_hang_limit_s(30) == 15.0);
    CHECK(ico_gpu_hang_limit_s(0) == 0.0);
    CHECK(ico_gpu_hang_limit_s(1) == 2.0);
    CHECK(ico_gpu_hang_limit_s(2) == 2.0);
    CHECK(ico_gpu_hang_limit_s(3) == 2.0);
    CHECK(ico_gpu_hang_limit_s(4) == 2.0);
    CHECK(ico_gpu_hang_limit_s(5) == 2.5);
    CHECK(ico_gpu_hang_limit_s(60) == 30.0);
    CHECK(ico_gpu_hang_limit_s(86400) == 43200.0);
}

/* polls every 250 ms for seconds s; the polls that fired */
static int pollFor(IcoGpuHangWatch *w, double limit, unsigned long long waiting,
                   unsigned long long finished, double s, int count, int *firstFire)
{
    int fired = 0;
    for (int i = 0; i < (int)(s * 4.0 + 0.5); i++) {
        if (ico_gpu_hang_step(w, limit, waiting, finished, 0.25, count)) {
            if (fired == 0 && firstFire != NULL) {
                *firstFire = i;
            }
            fired++;
        }
    }
    return fired;
}

static void testStep(void)
{
    IcoGpuHangWatch w = {0};
    int first = -1;
    /* the first poll only notes the wait; 60 polls of 0.25 s after it */
    CHECK(pollFor(&w, 15.0, 263, 262, 15.0, 1, &first) == 0);
    CHECK(w.counted_s > 14.7 && w.counted_s < 15.0);
    CHECK(ico_gpu_hang_step(&w, 15.0, 263, 262, 0.25, 1) == 1);
    CHECK(w.fired && w.counted_s >= 15.0);
    /* once: the wait going on fires no more */
    CHECK(pollFor(&w, 15.0, 263, 262, 10.0, 1, NULL) == 0);
}

static void testFinished(void)
{
    IcoGpuHangWatch w = {0};
    CHECK(pollFor(&w, 15.0, 263, 262, 10.0, 1, NULL) == 0);
    CHECK(w.counted_s > 9.0);
    /* the wait returned: submission 263 finished */
    CHECK(ico_gpu_hang_step(&w, 15.0, 263, 263, 0.25, 1) == 0);
    CHECK(w.waiting_for == 0 && w.counted_s == 0.0);
    /* no wait in progress */
    CHECK(pollFor(&w, 15.0, 264, 263, 5.0, 1, NULL) == 0);
    CHECK(ico_gpu_hang_step(&w, 15.0, 0, 263, 0.25, 1) == 0);
    CHECK(w.waiting_for == 0 && w.counted_s == 0.0);
    /* the next wait counts from 0: 14 s more is not the limit */
    CHECK(pollFor(&w, 15.0, 265, 264, 14.0, 1, NULL) == 0);
    CHECK(!w.fired);
}

static void testOther(void)
{
    IcoGpuHangWatch w = {0};
    /* a run of short waits, each for the next submission, 2 s each */
    for (unsigned long long n = 300; n < 330; n++) {
        CHECK(pollFor(&w, 15.0, n, n - 1, 2.0, 1, NULL) == 0);
    }
    CHECK(!w.fired && w.counted_s < 2.0);
}

static void testPaused(void)
{
    IcoGpuHangWatch w = {0};
    /* 10 s counted, 60 s paused in the background, 4 s counted: 14 s */
    CHECK(pollFor(&w, 15.0, 263, 262, 10.0, 1, NULL) == 0);
    CHECK(pollFor(&w, 15.0, 263, 262, 60.0, 0, NULL) == 0);
    CHECK(pollFor(&w, 15.0, 263, 262, 4.0, 1, NULL) == 0);
    CHECK(!w.fired);
    /* a suspend gap's one long poll, not counted */
    CHECK(ico_gpu_hang_step(&w, 15.0, 263, 262, 3600.0, 0) == 0);
    CHECK(!w.fired);
    /* the second more */
    CHECK(pollFor(&w, 15.0, 263, 262, 1.25, 1, NULL) == 1);
}

static void testOff(void)
{
    IcoGpuHangWatch w = {0};
    CHECK(pollFor(&w, ico_gpu_hang_limit_s(0), 263, 262, 600.0, 1, NULL) == 0);
    CHECK(!w.fired && w.waiting_for == 0);
    /* the smallest limit: 2 s */
    IcoGpuHangWatch v = {0};
    int first = -1;
    CHECK(pollFor(&v, ico_gpu_hang_limit_s(1), 263, 262, 3.0, 1, &first) == 1);
    CHECK(first == 8); /* the noting poll, then 8 polls of 0.25 s */
}

int main(void)
{
    testLimit();
    testStep();
    testFinished();
    testOther();
    testPaused();
    testOff();
    printf("gpu_hang_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
