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
 *            count starts again after the last creation;
 *   note     ico_pipe_note_creating: a creation's note counts; the
 *            background set's counts only while some of its compiles
 *            have not returned; other notes never;
 *   stall    the pipeline limit's watch (ico_pipe_stall_step): a
 *            movie of 40 s (no presented picture, so the
 *            background batch begun before it is never taken and its note
 *            stays) with the background set's compiles returning for its
 *            first 6 s and then none left is not a stall at the 30 s
 *            limit; a background compile that does not return for 30 s
 *            is, at 30 s; a first-use creation that does not return is,
 *            whatever the background set does after its batch; the time
 *            not counted, or the limit off, never stalls;
 *   188      issue 188: the limit before the game starts is 10 s, 60 s
 *            on a driver whose compiles run one at a time, 30 s in play
 *            on both (ico_pipe_stall_limit_s); the heartbeat's memory rule
 *            (ico_pipe_mem_moving: 2 MB or more of growth, an unknown
 *            reading never) over the issue 188 phone's heartbeats (147,
 *            251, 275, 296, 295, 293 MB at 4 to 14 s, a creation in
 *            progress from 4 s): the limit counts from the heartbeat after
 *            the last growth (12 s), so 10 s fires at 21.75 s, not 14 s,
 *            and 60 s at 71.75 s; the words of
 *            the next start (ico_pipe_stall_next_differs): inside the
 *            self-tests or on a lazy run the same way, unless the note
 *            names a family the renderer can leave out or a VU shader
 *            before the plain ones are in force; the set of a full or
 *            careful run another way.
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

#define BG_NOTE                                                                                    \
    "creating pipelines 81-88 of 412 in the background: list 3 TRI blend 1 to list 9 SPRITE; "     \
    "vs 212 words + fs 830 words"

static void testNote(void)
{
    CHECK(ico_pipe_note_creating("creating pipeline 217 of 587 (in batch 212-219): k (m)", 0));
    CHECK(ico_pipe_note_creating("creating pipeline list 2 TRI (vs 100 words)", 0));
    CHECK(ico_pipe_note_creating("creating pipelines 1-8 of 587: a to b; m", 0));
    CHECK(ico_pipe_note_creating(BG_NOTE, 3));
    CHECK(!ico_pipe_note_creating(BG_NOTE, 0));
    CHECK(!ico_pipe_note_creating("loading the texture pack", 3));
    CHECK(!ico_pipe_note_creating("", 3));
    CHECK(!ico_pipe_note_creating(NULL, 3));
}

/* polls every 250 ms from *t for seconds s with the note and the
   background set's compiles as given (left[] a poll each, its last
   held; the done count moving as left falls); the polls at or
   over the limit and the first such poll's time */
static int stallFor(IcoPipeStallWatch *w, double *t, const char *note, unsigned seq,
                    unsigned *bgDone, const unsigned *left, unsigned nLeft, double s, int counted,
                    int on, double limit, double *firstAt)
{
    int over = 0;
    unsigned prev = nLeft > 0 ? left[0] : 0;
    for (int i = 0; i < (int)(s * 4.0 + 0.5); i++) {
        const unsigned l = nLeft == 0 ? 0 : left[(unsigned)i < nLeft ? (unsigned)i : nLeft - 1];
        if (l < prev) {
            *bgDone += prev - l;
        }
        prev = l;
        *t += 0.25;
        const double st = ico_pipe_stall_step(w, *t, ico_pipe_note_creating(note, l), counted, on,
                                              seq, 120, *bgDone);
        if (st >= limit) {
            if (over == 0 && firstAt != NULL) {
                *firstAt = st;
            }
            over++;
        }
    }
    return over;
}

static void testStall(void)
{
    /* a movie: the batch of 8 begun at the last picture before it, its
       compiles returning one each 0.75 s on two workers for its first 6 s,
       then none left; the note stays for the 40 s (nothing takes the
       batch) and the notes' counter does not move */
    unsigned left[24];
    for (unsigned i = 0; i < 24; i++) {
        left[i] = 8u - (i + 1u) / 3u;
    }
    left[23] = 0;
    IcoPipeStallWatch w = {0};
    double t = 100.0;
    unsigned done = 40;
    CHECK(stallFor(&w, &t, BG_NOTE, 9, &done, left, 24, 40.0, 1, 1, 30.0, NULL) == 0);
    CHECK(done == 48);
    /* a background compile that does not return: two of the batch's left
       for 30 s with nothing returning; the stall reaches the limit at 30 s
       (the first poll notes it, then 120 polls of 0.25 s) */
    const unsigned stuck[1] = {2};
    IcoPipeStallWatch s = {0};
    double firstAt = 0.0;
    t = 200.0;
    CHECK(stallFor(&s, &t, BG_NOTE, 9, &done, stuck, 1, 30.0, 1, 1, 30.0, &firstAt) == 0);
    CHECK(stallFor(&s, &t, BG_NOTE, 9, &done, stuck, 1, 0.25, 1, 1, 30.0, &firstAt) == 1);
    CHECK(firstAt >= 30.0 && firstAt < 30.3);
    /* a first-use creation that does not return: its own note, the
       background set idle (none left); a stall at 30 s */
    IcoPipeStallWatch f = {0};
    t = 300.0;
    CHECK(stallFor(&f, &t, "creating pipeline list 2 TRI (vs 100 words)", 10, &done, NULL, 0, 31.0,
                   1, 1, 30.0, NULL) > 0);
    /* not counted (paused, asleep) or the limit off: never */
    IcoPipeStallWatch p = {0};
    t = 400.0;
    CHECK(stallFor(&p, &t, BG_NOTE, 9, &done, stuck, 1, 120.0, 0, 1, 30.0, NULL) == 0);
    CHECK(stallFor(&p, &t, BG_NOTE, 9, &done, stuck, 1, 120.0, 1, 0, 30.0, NULL) == 0);
}

static void test188(void)
{
    CHECK(ico_pipe_stall_limit_s(0, 0) == 10.0);
    CHECK(ico_pipe_stall_limit_s(0, 1) == 60.0);
    CHECK(ico_pipe_stall_limit_s(1, 0) == 30.0);
    CHECK(ico_pipe_stall_limit_s(1, 1) == 30.0);

    CHECK(ico_pipe_mem_moving(147L * 1024, 251L * 1024));
    CHECK(ico_pipe_mem_moving(1000, 1000 + ICO_PIPE_MEM_MOVING_KB));
    CHECK(!ico_pipe_mem_moving(1000, 1000 + ICO_PIPE_MEM_MOVING_KB - 1));
    CHECK(!ico_pipe_mem_moving(296L * 1024, 295L * 1024));
    CHECK(!ico_pipe_mem_moving(-1, 251L * 1024));
    CHECK(!ico_pipe_mem_moving(251L * 1024, -1));

    /* the issue 188 log's heartbeats, 2 s apart: 147 MB at 4 s, 251, 275
       and 296 MB at 6, 8 and 10 s, 295 and 293 MB at 12 and 14 s; a
       creation in progress from 4 s; polled every 250 ms, each heartbeat's
       verdict held until the next */
    static const long kMb[] = {147, 147, 147, 251, 275, 296, 295, 293};
    for (int serial = 0; serial < 2; serial++) {
        const double lim = ico_pipe_stall_limit_s(0, serial);
        IcoPipeStallWatch w = {0, 0, 0, 0.0};
        long prev = -1;
        int moving = 0;
        double firedAt = -1.0;
        for (int q = 0; q <= 90 * 4 && firedAt < 0.0; q++) {
            const double t = q * 0.25;
            if (q % 8 == 0) {
                const int beat = q / 8;
                const long mb = kMb[beat < 8 ? beat : 7];
                moving = ico_pipe_mem_moving(prev, mb * 1024);
                prev = mb * 1024;
            }
            const int creating = t >= 4.0;
            const double st = ico_pipe_stall_step(&w, t, creating, !moving, 1, 7, 0, 0);
            if (st >= lim) {
                firedAt = t;
            }
        }
        printf("  issue 188: limit %.0f s fired at %.2f s\n", lim, firedAt);
        /* the 10 s heartbeat's growth holds the count until the 12 s one:
           the last poll not counted is 11.75 s */
        CHECK(firedAt == 11.75 + lim);
    }

    CHECK(!ico_pipe_stall_next_differs(1, 0, 0, 0, 0)); /* issue 188 */
    CHECK(!ico_pipe_stall_next_differs(0, 1, 0, 0, 0));
    CHECK(!ico_pipe_stall_next_differs(1, 1, 0, 1, 1));
    CHECK(ico_pipe_stall_next_differs(0, 0, 0, 0, 0));
    CHECK(ico_pipe_stall_next_differs(1, 0, 1, 0, 0));
    CHECK(ico_pipe_stall_next_differs(0, 1, 0, 1, 0));
    CHECK(ico_pipe_stall_next_differs(1, 1, 0, 1, 0));
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
    testNote();
    testStall();
    test188();
    printf("gpu_hang_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
