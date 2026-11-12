/*
 * port/platform/test/capture_trigger_test.c
 *
 * A release candidate's frames of the opening: when they are taken
 * (capture_trigger.h), over made-up sequences of pictures (a glow light
 * plan, a frame number and a time each, in the opening or not):
 *   - LAMPS arms on the first LAMPS picture after 60 in a row (not after
 *     59), SUN on the first WHOLE picture after 20, LATER on the first
 *     DARK picture after 60 DARK pictures counted from the LAMPS dump's
 *     take (DARK pictures before it, or with no LAMPS dump, do not count);
 *   - a run is ended by another plan, by a picture outside the opening and
 *     by a picture with no plan of a new frame; one with no plan of the
 *     same frame is passed over;
 *   - an armed dump: kept while busy, dropped (and armable again) out of
 *     the opening, taken once; nothing else arms while one is armed;
 *   - 2 s from the end of a dump (ico_capture_trigger_done) to the next
 *     arm, not from its take; at most 3 dumps; each moment once a run;
 *     ico_capture_trigger_init starts a run again;
 *   - the start text and the dump lines word for word, a short buffer.
 */
#include <stdio.h>
#include <string.h>
#include "capture_trigger.h"

static int fails;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define MS 1000000ull
#define SEC 1000000000ull

enum { NONE = ICO_CAPTURE_PLAN_NONE, DARK = ICO_CAPTURE_PLAN_DARK };

enum { LAMPS = ICO_CAPTURE_PLAN_LAMPS, WHOLE = ICO_CAPTURE_PLAN_WHOLE };

/* the sequence's clock and frame: one picture each 25 ms, a frame each */
static uint64_t s_now;
static uint32_t s_frame;

/* n pictures of plan in the opening (or not); the moment the last one armed
   (the first armed one if several did, -1 none) */
static int feed(IcoCaptureTrigger *t, int n, int inOpening, int plan)
{
    int armed = -1;

    for (int i = 0; i < n; i++) {
        s_now += 25 * MS;
        s_frame++;
        const int r = ico_capture_trigger_picture(t, inOpening, plan, s_frame, s_now);
        if (r >= 0 && armed < 0) {
            armed = r;
        }
    }
    return armed;
}

/* the number of pictures of plan until one arms (at most max), 0 none */
static int until_armed(IcoCaptureTrigger *t, int max, int plan)
{
    for (int i = 1; i <= max; i++) {
        if (feed(t, 1, 1, plan) >= 0) {
            return i;
        }
    }
    return 0;
}

static void testLamps(void)
{
    IcoCaptureTrigger t;

    ico_capture_trigger_init(&t);
    CHECK(t.armed == -1 && t.dumps == 0 && t.used == 0, "init");
    CHECK(feed(&t, 60, 1, LAMPS) == -1, "60 LAMPS pictures arm nothing");
    CHECK(t.run == 60, "the run is 60 (%u)", t.run);
    CHECK(feed(&t, 1, 1, LAMPS) == ICO_CAPTURE_LAMPS, "the 61st arms LAMPS");
    CHECK(t.armed == ICO_CAPTURE_LAMPS && t.armedRun == 60, "armed after 60 (%d, %u)", t.armed,
          t.armedRun);
    CHECK(feed(&t, 30, 1, LAMPS) == -1, "nothing arms while armed");
    CHECK(t.armedRun == 60, "the armed run kept (%u)", t.armedRun);
    CHECK(ico_capture_trigger_take(&t, 1, 0, s_now) == ICO_CAPTURE_LAMPS, "taken");
    CHECK(t.dumps == 1 && t.armed == -1 && (t.used & (1u << ICO_CAPTURE_LAMPS)),
          "taken: counted and used");
    CHECK(ico_capture_trigger_take(&t, 1, 0, s_now) == -1, "taken once");
    ico_capture_trigger_done(&t, s_now);
    s_now += 10 * SEC;
    CHECK(feed(&t, 200, 1, LAMPS) == -1, "LAMPS once a run");

    /* 59 in a row, broken by another plan, 59 again: nothing */
    ico_capture_trigger_init(&t);
    CHECK(feed(&t, 59, 1, LAMPS) == -1 && feed(&t, 1, 1, WHOLE) == -1 &&
              feed(&t, 60, 1, LAMPS) == -1,
          "a run broken by WHOLE starts again");
    CHECK(t.run == 60, "the new run is 60 (%u)", t.run);
    CHECK(feed(&t, 1, 1, LAMPS) == ICO_CAPTURE_LAMPS, "and its 61st arms");
}

static void testSun(void)
{
    IcoCaptureTrigger t;

    ico_capture_trigger_init(&t);
    CHECK(until_armed(&t, 100, WHOLE) == 21, "SUN on the 21st WHOLE picture, LAMPS not needed");
    CHECK(t.armed == ICO_CAPTURE_SUN && t.armedRun == 20, "SUN after 20 (%u)", t.armedRun);
    CHECK(ico_capture_trigger_take(&t, 1, 0, s_now) == ICO_CAPTURE_SUN, "SUN taken");
}

static void testLater(void)
{
    IcoCaptureTrigger t;

    /* DARK without a LAMPS dump: never */
    ico_capture_trigger_init(&t);
    CHECK(feed(&t, 500, 1, DARK) == -1, "DARK alone arms nothing");
    CHECK(t.darkAfter == 0, "no DARK counted before the LAMPS dump (%u)", t.darkAfter);

    /* LAMPS armed, 30 DARK pictures before its take: not counted */
    CHECK(until_armed(&t, 100, LAMPS) == 61, "LAMPS after DARK");
    CHECK(feed(&t, 30, 1, DARK) == -1, "DARK while LAMPS is armed");
    CHECK(t.darkAfter == 0, "DARK before the take not counted (%u)", t.darkAfter);
    CHECK(ico_capture_trigger_take(&t, 1, 0, s_now) == ICO_CAPTURE_LAMPS, "LAMPS taken");
    ico_capture_trigger_done(&t, s_now);
    s_now += 5 * SEC;
    CHECK(feed(&t, 60, 1, DARK) == -1, "60 DARK after the take arm nothing");
    CHECK(t.darkAfter == 60 && t.run == 90, "60 counted after the take, the run 90 (%u, %u)",
          t.darkAfter, t.run);
    CHECK(feed(&t, 1, 1, DARK) == ICO_CAPTURE_LATER, "the 61st after the take arms LATER");
    CHECK(t.armedRun == 60, "LATER after 60 (%u)", t.armedRun);
    CHECK(ico_capture_trigger_take(&t, 1, 0, s_now) == ICO_CAPTURE_LATER, "LATER taken");

    /* the DARK count after LAMPS is ended by another plan */
    ico_capture_trigger_init(&t);
    CHECK(until_armed(&t, 100, LAMPS) == 61, "LAMPS");
    CHECK(ico_capture_trigger_take(&t, 1, 0, s_now) == ICO_CAPTURE_LAMPS, "LAMPS taken");
    ico_capture_trigger_done(&t, s_now);
    s_now += 5 * SEC;
    CHECK(feed(&t, 59, 1, DARK) == -1 && feed(&t, 1, 1, LAMPS) == -1 && feed(&t, 60, 1, DARK) == -1,
          "59 DARK, a LAMPS, 60 DARK: nothing");
    CHECK(feed(&t, 1, 1, DARK) == ICO_CAPTURE_LATER, "the next DARK arms LATER");
}

static void testNoPlan(void)
{
    IcoCaptureTrigger t;

    /* each frame's later present has no plan (the tick's glow pasted
       without its chain): passed over */
    ico_capture_trigger_init(&t);
    int armedAt = 0;
    for (int i = 1; i <= 100 && !armedAt; i++) {
        s_now += 25 * MS;
        s_frame++;
        if (ico_capture_trigger_picture(&t, 1, LAMPS, s_frame, s_now) >= 0) {
            armedAt = i;
        }
        s_now += 8 * MS;
        CHECK(ico_capture_trigger_picture(&t, 1, NONE, s_frame, s_now) == -1, "NONE arms nothing");
    }
    CHECK(armedAt == 61, "the same frame's NONE passed over: armed at the 61st LAMPS (%d)",
          armedAt);

    /* NONE of a new frame ends the run */
    ico_capture_trigger_init(&t);
    CHECK(feed(&t, 50, 1, LAMPS) == -1 && feed(&t, 1, 1, NONE) == -1,
          "50 LAMPS, a new frame's NONE");
    CHECK(t.run == 0 && t.runPlan == NONE, "the run ended (%u)", t.run);
    CHECK(until_armed(&t, 100, LAMPS) == 61, "the 61st LAMPS after it arms");

    /* a plan the trigger does not know (the renderer's OFF) is no plan */
    ico_capture_trigger_init(&t);
    CHECK(feed(&t, 50, 1, LAMPS) == -1 && feed(&t, 1, 1, 7) == -1 && t.run == 0,
          "an unknown plan of a new frame ends the run");
}

static void testOpening(void)
{
    IcoCaptureTrigger t;

    /* outside the opening: no run, nothing armed */
    ico_capture_trigger_init(&t);
    CHECK(feed(&t, 200, 0, LAMPS) == -1 && t.run == 0, "outside the opening");
    /* a picture outside ends the run */
    CHECK(feed(&t, 60, 1, LAMPS) == -1 && feed(&t, 1, 0, LAMPS) == -1 && t.run == 0,
          "a picture outside the opening ends the run");
    CHECK(until_armed(&t, 100, LAMPS) == 61, "61 LAMPS again");
    /* the arm dropped out of the opening, not used, armable again */
    CHECK(ico_capture_trigger_take(&t, 0, 0, s_now) == -1, "not taken out of the opening");
    CHECK(t.armed == -1 && t.dumps == 0 && t.used == 0, "the arm dropped, nothing used");
    CHECK(feed(&t, 1, 0, LAMPS) == -1, "outside");
    CHECK(until_armed(&t, 100, LAMPS) == 61, "armed again after a new run of 60");
    /* busy keeps the arm */
    CHECK(ico_capture_trigger_take(&t, 1, 1, s_now) == -1, "busy: not taken");
    CHECK(t.armed == ICO_CAPTURE_LAMPS && t.dumps == 0, "busy: the arm kept");
    CHECK(ico_capture_trigger_take(&t, 1, 0, s_now) == ICO_CAPTURE_LAMPS, "taken once free");
}

static void testSpacingAndCap(void)
{
    IcoCaptureTrigger t;

    /* the 2 s count from the dump's end: taken at T, done at T + 1.5 s */
    ico_capture_trigger_init(&t);
    CHECK(until_armed(&t, 100, LAMPS) == 61, "LAMPS");
    const uint64_t taken = s_now;
    CHECK(ico_capture_trigger_take(&t, 1, 0, taken) == ICO_CAPTURE_LAMPS, "taken at T");
    ico_capture_trigger_done(&t, taken + 1500 * MS);
    s_now = taken + 1500 * MS;
    /* WHOLE pictures every 25 ms from T + 1.525 s: the 21st (T + 2.025 s)
       is 2 s after the take but 0.525 s after the end */
    CHECK(feed(&t, 21, 1, WHOLE) == -1, "SUN's 21st 0.525 s after the end: not armed");
    /* the first WHOLE picture at T + 3.5 s or later arms: the 80th, at
       T + 1.5 s + 80 x 25 ms = T + 3.5 s */
    CHECK(until_armed(&t, 200, WHOLE) == 59, "armed at 2 s after the end (21 + 59 = 80)");
    CHECK(s_now == taken + 3500 * MS, "at T + 3.5 s (%llu ms after T)",
          (unsigned long long)((s_now - taken) / MS));
    CHECK(t.armedRun == 79, "after 79 WHOLE pictures (%u)", t.armedRun);
    CHECK(ico_capture_trigger_take(&t, 1, 0, s_now) == ICO_CAPTURE_SUN, "SUN taken");
    ico_capture_trigger_done(&t, s_now);

    /* LATER, then nothing more: the three moments, 3 dumps */
    s_now += 3 * SEC;
    CHECK(until_armed(&t, 200, DARK) == 61, "LATER");
    CHECK(ico_capture_trigger_take(&t, 1, 0, s_now) == ICO_CAPTURE_LATER, "LATER taken");
    ico_capture_trigger_done(&t, s_now);
    CHECK(t.dumps == 3, "3 dumps (%d)", t.dumps);
    s_now += 10 * SEC;
    CHECK(feed(&t, 300, 1, LAMPS) == -1 && feed(&t, 300, 1, WHOLE) == -1 &&
              feed(&t, 300, 1, DARK) == -1,
          "nothing after 3");

    /* the cap of 3 on its own (the dumps counted, a moment not used) */
    ico_capture_trigger_init(&t);
    t.dumps = ICO_CAPTURE_MAX;
    CHECK(feed(&t, 200, 1, LAMPS) == -1 && t.armed == -1, "no arm at the cap");

    /* a clock that went back is too soon */
    ico_capture_trigger_init(&t);
    ico_capture_trigger_done(&t, s_now + 60 * SEC);
    CHECK(feed(&t, 200, 1, LAMPS) == -1, "no arm before the last dump's end");

    /* a new run: everything again */
    ico_capture_trigger_init(&t);
    CHECK(t.dumps == 0 && t.used == 0 && t.haveLast == 0 && t.armed == -1, "a new run");
    CHECK(until_armed(&t, 100, LAMPS) == 61, "LAMPS again in a new run");
}

static void testLines(void)
{
    char buf[512];
    size_t n;

    CHECK(strcmp(ico_capture_trigger_start_text(),
                 "capture: on (a release candidate): frames of the opening are saved by "
                 "themselves (a lamps picture, a sun picture, a later picture) into the dumps "
                 "folder and sent with \"Share a bug report\"") == 0,
          "start: %s", ico_capture_trigger_start_text());
    CHECK(strcmp(ico_capture_trigger_name(ICO_CAPTURE_LAMPS), "the lamps picture") == 0 &&
              strcmp(ico_capture_trigger_name(ICO_CAPTURE_SUN), "the sun picture") == 0 &&
              strcmp(ico_capture_trigger_name(ICO_CAPTURE_LATER), "the later picture") == 0 &&
              strcmp(ico_capture_trigger_name(-1), "") == 0 &&
              strcmp(ico_capture_trigger_name(ICO_CAPTURE_REASONS), "") == 0,
          "names");
    n = ico_capture_trigger_line(buf, sizeof(buf), ICO_CAPTURE_LAMPS, 60, 1, 1234,
                                 "frame-20261112-101010-v900.zip");
    CHECK(strcmp(buf, "capture: the lamps picture (dump 1 of 3 this run, after 60 lamps pictures "
                      "in a row): frame 1234, frame-20261112-101010-v900.zip") == 0,
          "lamps: %s", buf);
    CHECK(n == strlen(buf), "lamps: length %zu", n);
    ico_capture_trigger_line(buf, sizeof(buf), ICO_CAPTURE_SUN, 20, 2, 5, "z.zip");
    CHECK(strcmp(buf, "capture: the sun picture (dump 2 of 3 this run, after 20 sun pictures (the "
                      "glow drawn whole) in a row): frame 5, z.zip") == 0,
          "sun: %s", buf);
    ico_capture_trigger_line(buf, sizeof(buf), ICO_CAPTURE_LATER, 60, 3, 99, NULL);
    CHECK(strcmp(buf, "capture: the later picture (dump 3 of 3 this run, after 60 dark pictures in "
                      "a row after the lamps picture): frame 99 not saved (no zip; the lines "
                      "above say why); the capture goes on") == 0,
          "later, no zip: %s", buf);
    ico_capture_trigger_line(buf, sizeof(buf), ICO_CAPTURE_LATER, 60, 3, 99, "");
    CHECK(strstr(buf, "not saved") != NULL, "an empty name is no zip: %s", buf);
    char small[16];
    memset(small, 'x', sizeof(small));
    n = ico_capture_trigger_line(small, sizeof(small), ICO_CAPTURE_LAMPS, 60, 1, 1, "a.zip");
    CHECK(n == 15 && strcmp(small, "capture: the la") == 0, "short buffer: %zu \"%s\"", n, small);
    CHECK(ico_capture_trigger_line(small, 0, ICO_CAPTURE_LAMPS, 60, 1, 1, "a.zip") == 0,
          "no buffer");
}

int main(void)
{
    testLamps();
    testSun();
    testLater();
    testNoPlan();
    testOpening();
    testSpacingAndCap();
    testLines();
    if (fails) {
        printf("capture_trigger_test: %d failures\n", fails);
        return 1;
    }
    printf("capture_trigger_test: ok\n");
    return 0;
}
