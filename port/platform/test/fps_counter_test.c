/*
 * port/platform/test/fps_counter_test.c
 *
 * The Frame rate counter's number (fps_counter.h): zero before the first
 * second; presents counted with interpolation on (50 a second gives 50, each
 * second); a second that runs long divides by its real length and rounds to
 * the nearest; replays counted with interpolation off through the renderer's
 * count being zeroed every 10 s (a zeroing loses no replay); a change of mode
 * starts the second again; a clock that goes backwards changes nothing.
 * Issue 123: a gap the window notes (fps_gap: a capture, the app back) starts
 * the second again and keeps the value; a gap between ticks of the guard's
 * length does the same with no note, a shorter one counts; a 450 ms stall
 * inside play lowers the value truthfully; three 440 ms stalls in a little
 * more than a second read 5.
 */
#include <stdio.h>

#include "fps_counter.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define MS 1000000ull
#define BASE (5000ull * MS)

/* 50 presents a second, a tick every millisecond: 0 until the second ends,
   then 50 at every whole second */
static void testPresents(FpsCounter *c)
{
    for (unsigned t = 0; t <= 3000; t++) {
        if (t > 0 && t % 20 == 0) {
            fps_present(c);
        }
        fps_tick(c, BASE + t * MS, 1, 0);
        if (t == 999) {
            CHECK(fps_value(c) == 0);
        }
        if (t == 1000 || t == 2000 || t == 3000) {
            CHECK(fps_value(c) == 50);
        }
    }
}

/* a second that ran 1.5 s long: 91 presents are 60.67 a second, 61; 89 are
   59.33, 59 */
static void testLongSecond(void)
{
    static const unsigned kPresents[2] = {91, 89};
    static const int kWant[2] = {61, 59};

    for (int i = 0; i < 2; i++) {
        FpsCounter c = {0};

        fps_tick(&c, BASE, 1, 0);
        for (unsigned k = 0; k < kPresents[i]; k++) {
            fps_present(&c);
        }
        fps_tick(&c, BASE + 1500 * MS, 1, 0);
        CHECK(fps_value(&c) == kWant[i]);
    }
}

/* 100 replays a second, the renderer's count zeroed after the tick at 600 ms
   (the 10 s line): the count at 610 ms is 1, and no replay is lost */
static void testReplays(void)
{
    FpsCounter c = {0};

    for (unsigned t = 0; t <= 1000; t += 10) {
        const unsigned count = t <= 600 ? t / 10 : (t - 600) / 10;

        fps_tick(&c, BASE + t * MS, 0, count);
        if (t == 990) {
            CHECK(fps_value(&c) == 0);
        }
    }
    CHECK(fps_value(&c) == 100);
}

/* a change of mode starts the second again: the value stays until the new
   mode has had its own whole second */
static void testModeChange(FpsCounter *c)
{
    const uint64_t t0 = BASE + 4000 * MS;

    CHECK(fps_value(c) == 50);
    fps_tick(c, t0, 0, 0);
    fps_tick(c, t0 + 900 * MS, 0, 30);
    CHECK(fps_value(c) == 50);
    fps_tick(c, t0 + 1000 * MS, 0, 30);
    CHECK(fps_value(c) == 30);
}

/* a time before the second began changes nothing and does not crash */
static void testBackwards(FpsCounter *c)
{
    const int before = fps_value(c);

    fps_tick(c, BASE, 0, 30);
    CHECK(fps_value(c) == before);
}

/* 50 a second for a whole second from t0: the value 50 at t0 + 1 s */
static void steady(FpsCounter *c, uint64_t t0, unsigned every)
{
    fps_tick(c, t0, 1, 0);
    for (unsigned t = 1; t <= 1000; t++) {
        if (t % every == 0) {
            fps_present(c);
        }
        fps_tick(c, t0 + t * MS, 1, 0);
    }
}

/* the reporter's capture (issue 123: 1.1 s with no pace): 15 presents in
   300 ms, the capture noted, the next tick 1.1 s later starts the second
   again and keeps 50 (without the note: 15 / 1.4 s, 11); the second after
   it is its own, 40 a second gives 40 */
static void testGapNote(void)
{
    FpsCounter c = {0};
    const uint64_t t1 = BASE + 1000 * MS;

    steady(&c, BASE, 20);
    CHECK(fps_value(&c) == 50);
    for (unsigned t = 1; t <= 300; t++) {
        if (t % 20 == 0) {
            fps_present(&c);
        }
        fps_tick(&c, t1 + t * MS, 1, 0);
    }
    fps_gap(&c);
    fps_tick(&c, t1 + 1400 * MS, 1, 0);
    CHECK(fps_value(&c) == 50);
    CHECK(fps_restarts(&c) == 1);
    for (unsigned t = 1401; t <= 2400; t++) {
        if (t % 25 == 0) {
            fps_present(&c);
        }
        fps_tick(&c, t1 + t * MS, 1, 0);
        if (t == 2399) {
            CHECK(fps_value(&c) == 50);
        }
    }
    CHECK(fps_value(&c) == 40);
    CHECK(fps_restarts(&c) == 1);
}

/* no note: a gap between ticks of ICO_GPU_HANG_MIN_S (2 s, the guard with
   gapNs 0) or more starts the second again, the value kept; with gapNs the
   15 s limit a 13 s gap is inside the second: 10 presents over 13.3 s, 1 */
static void testGapGuard(void)
{
    FpsCounter c = {0};
    const uint64_t t1 = BASE + 1000 * MS;

    steady(&c, BASE, 20);
    for (unsigned k = 0; k < 10; k++) {
        fps_present(&c);
    }
    fps_tick(&c, t1 + 300 * MS, 1, 0);
    fps_tick(&c, t1 + 2300 * MS, 1, 0);
    CHECK(fps_value(&c) == 50);
    CHECK(fps_restarts(&c) == 1);

    FpsCounter d = {0};

    d.gapNs = 15000ull * MS;
    steady(&d, BASE, 20);
    for (unsigned k = 0; k < 10; k++) {
        fps_present(&d);
    }
    fps_tick(&d, t1 + 300 * MS, 1, 0);
    fps_tick(&d, t1 + 13300 * MS, 1, 0);
    CHECK(fps_value(&d) == 1);
    CHECK(fps_restarts(&d) == 0);
}

/* a 450 ms stall inside play: presents every 20 ms to 540 ms (27), no pace
   from 550 to 1000 ms; the second's 27 presents over 1 s give 27, the
   second not started again */
static void testStall(void)
{
    FpsCounter c = {0};

    fps_tick(&c, BASE, 1, 0);
    for (unsigned t = 1; t <= 550; t++) {
        if (t % 20 == 0) {
            fps_present(&c);
        }
        fps_tick(&c, BASE + t * MS, 1, 0);
    }
    fps_tick(&c, BASE + 1000 * MS, 1, 0);
    CHECK(fps_value(&c) == 27);
    CHECK(fps_restarts(&c) == 0);
}

/* three 440 ms stalls in a row (the reporter's cluster): presents at 10, 20,
   460, 470, 910, 920 and 1360 ms, a pace at each; at 1360 ms 7 presents over
   1.36 s, 5.15, 5 */
static void testStallCluster(void)
{
    static const unsigned kAt[7] = {10, 20, 460, 470, 910, 920, 1360};
    FpsCounter c = {0};

    fps_tick(&c, BASE, 1, 0);
    for (int i = 0; i < 7; i++) {
        fps_present(&c);
        fps_tick(&c, BASE + kAt[i] * MS, 1, 0);
        if (i == 5) {
            CHECK(fps_value(&c) == 0);
        }
    }
    CHECK(fps_value(&c) == 5);
    CHECK(fps_restarts(&c) == 0);
}

int main(void)
{
    FpsCounter c = {0};

    CHECK(fps_value(&c) == 0);
    testPresents(&c);
    testLongSecond();
    testReplays();
    testModeChange(&c);
    CHECK(fps_restarts(&c) == 0); /* a change of mode is not a gap */
    testBackwards(&c);
    testGapNote();
    testGapGuard();
    testStall();
    testStallCluster();
    if (fails) {
        printf("fps_counter_test: %d failures\n", fails);
        return 1;
    }
    printf("fps_counter_test: ok\n");
    return 0;
}
