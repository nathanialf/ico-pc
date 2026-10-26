/*
 * port/platform/test/fps_counter_test.c
 *
 * The Frame rate counter's number (fps_counter.h): zero before the first
 * second; presents counted with interpolation on (50 a second gives 50, each
 * second); a second that runs long divides by its real length and rounds to
 * the nearest; replays counted with interpolation off through the renderer's
 * count being zeroed every 10 s (a zeroing loses no replay); a change of mode
 * starts the second again; a clock that goes backwards changes nothing.
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

int main(void)
{
    FpsCounter c = {0};

    CHECK(fps_value(&c) == 0);
    testPresents(&c);
    testLongSecond();
    testReplays();
    testModeChange(&c);
    testBackwards(&c);
    if (fails) {
        printf("fps_counter_test: %d failures\n", fails);
        return 1;
    }
    printf("fps_counter_test: ok\n");
    return 0;
}
