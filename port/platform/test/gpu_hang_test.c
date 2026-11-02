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
 *   off      a limit of 0 never fires, however long the wait;
 *   render   the render thread's watch (ico_render_hang_step): a picture
 *            that makes 60 pipelines of 250 ms each (the PowerVR DXT-48's
 *            cold compiles, 15 s in all) is not a hang, nor one inside a
 *            single creation for 20 s; a picture with no creation fires
 *            at 15 s, once; between pictures (seq 0) nothing counts; the
 *            count starts again after the last creation.
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

/* the render thread's watch polled every 250 ms for seconds s with the
   creations' counter moving by `step` a poll (0: still) and `creating`
   held; the polls that fired */
static int renderFor(IcoRenderHangWatch *r, unsigned long long seq, unsigned long long done,
                     unsigned *mark, unsigned step, int creating, double s)
{
    int fired = 0;
    for (int i = 0; i < (int)(s * 4.0 + 0.5); i++) {
        *mark += step;
        fired += ico_render_hang_step(r, 15.0, seq, done, *mark, creating, 0.25, 1);
    }
    return fired;
}

static void testRender(void)
{
    unsigned mark = 7;
    /* picture 40 makes 60 pipelines of 250 ms on first use: each poll sees
       a creation start or end (two notes a pipeline) */
    IcoRenderHangWatch r = {0};
    CHECK(renderFor(&r, 40, 39, &mark, 1, 0, 15.0) == 0);
    CHECK(renderFor(&r, 40, 39, &mark, 2, 1, 5.0) == 0);
    CHECK(!r.w.fired && r.w.counted_s == 0.0);
    /* one creation that does not return for 20 s: the pipeline limit's,
       not this one's */
    CHECK(renderFor(&r, 40, 39, &mark, 0, 1, 20.0) == 0);
    CHECK(!r.w.fired);
    /* after the last creation the picture's own time counts from 0: 14 s
       is not the limit, 15 s more fires once */
    CHECK(renderFor(&r, 40, 39, &mark, 0, 0, 14.0) == 0);
    CHECK(renderFor(&r, 40, 39, &mark, 0, 0, 2.0) == 1);
    CHECK(r.w.fired);
    CHECK(renderFor(&r, 40, 39, &mark, 0, 0, 10.0) == 0);
    /* a picture with no creation at all fires at 15 s */
    IcoRenderHangWatch q = {0};
    int fired = 0, at = -1;
    for (int i = 0; i < 70; i++) {
        if (ico_render_hang_step(&q, 15.0, 41, 40, mark, 0, 0.25, 1)) {
            fired++;
            at = i;
        }
    }
    CHECK(fired == 1 && at == 60); /* the noting poll, then 60 polls of 0.25 s */
    /* between pictures (a call job, or idle) nothing counts */
    IcoRenderHangWatch c = {0};
    CHECK(renderFor(&c, 0, 41, &mark, 0, 0, 60.0) == 0);
    CHECK(c.w.counted_s == 0.0 && !c.w.fired);
}

int main(void)
{
    testLimit();
    testStep();
    testFinished();
    testOther();
    testPaused();
    testOff();
    testRender();
    printf("gpu_hang_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
