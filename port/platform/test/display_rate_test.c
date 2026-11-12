/*
 * port/platform/test/display_rate_test.c
 *
 * The refresh rate's log lines and the Android frame rate pin's decisions
 * (display_rate.h, issue 123):
 *   watch    the first rate is noted without a change; the same rate, a
 *            rate within 0.05 Hz (59.94 against 59.95) and an unknown rate
 *            (0, negative) are not changes; 144 -> 60 -> 120 are two
 *            changes, counted;
 *   lines    the block's line (the rate, the size, the display's name, the
 *            run time, "1 change"/"2 changes"), a rate not known, the
 *            change's line; a small buffer cut; the 10 s poll
 *            (display_rate_poll): no change nothing, an event without a
 *            change nothing, a change SDL announced at the event's time, one
 *            it did not with its own line, an unknown rate nothing;
 *   mode     [dev] frame_rate_pin: auto for NULL, "", "auto" and a value not
 *            read; off for off, false, 0, no in any case;
 *   rate     the Frame rate row 60, 120, 144 asks that rate; Original (0)
 *            and Uncapped (-1) ask none; the pin off asks none; above 1000
 *            asks none;
 *   pin      the sequence 60 (set) -> 60 (nothing) -> 120 (set) -> Uncapped
 *            (release) -> Uncapped (nothing); a device without the call is
 *            never asked and its state stays; a refused ask is asked again
 *            at the next change of the call; the surface: a new native
 *            window pointer or a resume (the pointer reused) forgets the
 *            ask, the next set is DISPLAY_PIN_SET_AGAIN; a resume with no
 *            rate asked, or the row turned to Uncapped before the next call,
 *            asks and says nothing; a device without the call is never asked;
 *   text     the pin's line for a set (rate, row, call, return, "(refused)"
 *            for a return other than 0), a set again for a new surface and
 *            a release;
 *   top      Uncapped's ask, the display's highest rate at the current
 *            mode's size: two rates (60, 120), ties (a rate listed twice,
 *            120.00 and 120.02 one rate), one mode, none, a list that could
 *            not be read, other sizes not counted (144 Hz at another size),
 *            fractional rates (59.94, 119.88: asked as 119.88, 120 in the
 *            state), invalid rates, more than DISPLAY_TOP_RATES rates;
 *            display_pin_want's order (a number and Original as before, the
 *            pin off, no seamless-only call, refused, the list, focus with
 *            and without a held ask); the state: the row 120 then Uncapped
 *            at 120 is a new ask, a release clears it, a lost surface asks
 *            again; every line word for word and a small buffer;
 *   pick     the refresh the game paces by (display_refresh_pick) and the
 *            vsync lock's inputs from it as window_host.c video_settings
 *            computes them (window_refresh_ns's period, then
 *            pace_cap_equals_refresh, pace_lock_presents,
 *            pace_grid_period_ok): a number or Original with any reported
 *            rate decides exactly as with no report (the default 60 on a
 *            stale 120 or 60 with the panel at 60 or 120, 120 and 144 rows,
 *            Original); Uncapped takes the reported period and never the
 *            lock; reports out of range are SDL's; the report guard
 *            (display_report_new): the same report twice does nothing, a
 *            new rate or a new mode list is new, 0 nothing.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "display_rate.h"
#include "pace_policy.h"

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
            printf("FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, (got), (want));    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void testWatch(void)
{
    DisplayRateWatch w = {0};
    char line[256];

    CHECK(display_rate_note(&w, 0.0) == 0 && !w.have);
    CHECK(display_rate_note(&w, -1.0) == 0 && !w.have);
    display_rate_text(&w, 0, 0, NULL, 0.0, line, sizeof line);
    CHECK_STR(line, "display refresh: rate not known, 0x0 (run time 0.0 s, 0 changes this run)");
    CHECK(display_rate_note(&w, 144.0) == 0 && w.have && w.hz == 144.0);
    CHECK(display_rate_note(&w, 144.0) == 0);
    CHECK(display_rate_note(&w, 143.97) == 0 && w.changes == 0);
    CHECK(display_rate_note(&w, 0.0) == 0 && w.hz == 144.0);
    CHECK(display_rate_note(&w, 60.0) == 1 && w.changes == 1 && w.hz == 60.0);
    display_rate_text(&w, 3048, 2032, "Built-in display", 85.3, line, sizeof line);
    CHECK_STR(line, "display refresh: 60.00 Hz, 3048x2032 \"Built-in display\" (run time 85.3 s, "
                    "1 change this run)");
    CHECK(display_rate_note(&w, 120.0) == 1 && w.changes == 2);
    display_rate_text(&w, 3048, 2032, "", 90.0, line, sizeof line);
    CHECK_STR(line, "display refresh: 120.00 Hz, 3048x2032 (run time 90.0 s, 2 changes this run)");
    display_rate_change_text(60.0, 120.0, 3048, 2032, 85.5, 2, line, sizeof line);
    CHECK_STR(line, "display refresh changed: 60.00 Hz -> 120.00 Hz, 3048x2032 (run time 85.5 s, "
                    "change 2 of this run)");
    {
        char small[10];
        display_rate_text(&w, 1, 1, NULL, 0.0, small, sizeof small);
        CHECK(strlen(small) == 9);
    }
    /* the 10 s poll (the event watch only counts): a change SDL announced
       is written at the event's time, one it did not as before, none
       nothing */
    {
        DisplayRateWatch p = {0};

        CHECK(display_rate_poll(&p, 144.0, 0, 0.0, 3048, 2032, line, sizeof line) == 0);
        CHECK_STR(line, "");
        CHECK(display_rate_poll(&p, 144.0, 1, 12.0, 3048, 2032, line, sizeof line) == 0);
        CHECK(display_rate_poll(&p, 60.0, 2, 85.2, 3048, 2032, line, sizeof line) == 1);
        CHECK_STR(line, "display refresh changed: 144.00 Hz -> 60.00 Hz, 3048x2032 (run time "
                        "85.2 s, change 1 of this run)");
        CHECK(display_rate_poll(&p, 120.0, 0, 85.2, 3048, 2032, line, sizeof line) == 1);
        CHECK_STR(line, "display refresh changed without an event from SDL: 120.00 Hz");
        CHECK(p.changes == 2 && p.hz == 120.0);
        CHECK(display_rate_poll(&p, 0.0, 1, 90.0, 3048, 2032, line, sizeof line) == 0);
    }
}

static void testPin(void)
{
    DisplayPin p = {0};
    char line[320];

    CHECK(display_pin_mode(NULL) == DISPLAY_PIN_AUTO);
    CHECK(display_pin_mode("") == DISPLAY_PIN_AUTO);
    CHECK(display_pin_mode("auto") == DISPLAY_PIN_AUTO);
    CHECK(display_pin_mode("maybe") == DISPLAY_PIN_AUTO);
    CHECK(display_pin_mode("off") == DISPLAY_PIN_OFF);
    CHECK(display_pin_mode("OFF") == DISPLAY_PIN_OFF);
    CHECK(display_pin_mode("false") == DISPLAY_PIN_OFF);
    CHECK(display_pin_mode("0") == DISPLAY_PIN_OFF);
    CHECK(display_pin_mode("No") == DISPLAY_PIN_OFF);

    CHECK(display_pin_rate(60, DISPLAY_PIN_AUTO) == 60);
    CHECK(display_pin_rate(120, DISPLAY_PIN_AUTO) == 120);
    CHECK(display_pin_rate(144, DISPLAY_PIN_AUTO) == 144);
    CHECK(display_pin_rate(0, DISPLAY_PIN_AUTO) == 0);
    CHECK(display_pin_rate(-1, DISPLAY_PIN_AUTO) == 0);
    CHECK(display_pin_rate(60, DISPLAY_PIN_OFF) == 0);
    CHECK(display_pin_rate(1001, DISPLAY_PIN_AUTO) == 0);

    /* the sequence */
    CHECK(display_pin_next(&p, 0, 1) == DISPLAY_PIN_NOTHING);
    CHECK(display_pin_next(&p, 60, 1) == DISPLAY_PIN_SET && p.asked == 60);
    CHECK(display_pin_next(&p, 60, 1) == DISPLAY_PIN_NOTHING);
    CHECK(display_pin_next(&p, 120, 1) == DISPLAY_PIN_SET && p.asked == 120);
    CHECK(display_pin_next(&p, 0, 1) == DISPLAY_PIN_RELEASE && p.asked == 0);
    CHECK(display_pin_next(&p, 0, 1) == DISPLAY_PIN_NOTHING);
    /* a device without the call */
    CHECK(display_pin_next(&p, 60, 0) == DISPLAY_PIN_NOTHING && p.asked == 0);
    /* a refused ask is tried again */
    CHECK(display_pin_next(&p, 60, 1) == DISPLAY_PIN_SET);
    display_pin_failed(&p);
    CHECK(p.asked == 0);
    CHECK(display_pin_next(&p, 60, 1) == DISPLAY_PIN_SET);

    /* the surface: a new native window holds no vote (the app came back) */
    {
        DisplayPin q = {0};
        int a = 1, b = 2;

        CHECK(display_pin_surface(&q, &a, 0) == 0); /* the first surface: nothing asked yet */
        CHECK(display_pin_next(&q, 60, 1) == DISPLAY_PIN_SET && q.asked == 60);
        CHECK(display_pin_surface(&q, &a, 0) == 0 && q.asked == 60); /* the same: kept */
        CHECK(display_pin_next(&q, 60, 1) == DISPLAY_PIN_NOTHING);
        /* a new pointer: the ask lost, made again and said so */
        CHECK(display_pin_surface(&q, &b, 0) == 1 && q.asked == 0);
        CHECK(display_pin_next(&q, 60, 1) == DISPLAY_PIN_SET_AGAIN && q.asked == 60);
        CHECK(display_pin_next(&q, 60, 1) == DISPLAY_PIN_NOTHING);
        /* a resume with the pointer reused: asked again all the same */
        CHECK(display_pin_surface(&q, &b, 1) == 1 && q.asked == 0);
        CHECK(display_pin_next(&q, 60, 1) == DISPLAY_PIN_SET_AGAIN);
        /* a resume with no rate asked: nothing to ask, nothing said */
        CHECK(display_pin_next(&q, 0, 1) == DISPLAY_PIN_RELEASE && q.asked == 0);
        CHECK(display_pin_surface(&q, &a, 1) == 0);
        CHECK(display_pin_next(&q, 0, 1) == DISPLAY_PIN_NOTHING && q.again == 0);
        CHECK(display_pin_next(&q, 120, 1) == DISPLAY_PIN_SET); /* a plain ask after */
        /* lost, then the row turned to Uncapped before the next call: no
           release on the new surface, and the later ask is a plain one */
        CHECK(display_pin_surface(&q, &b, 1) == 1);
        CHECK(display_pin_next(&q, 0, 1) == DISPLAY_PIN_NOTHING && q.again == 0);
        CHECK(display_pin_next(&q, 60, 1) == DISPLAY_PIN_SET);
        /* a device without the call: lost, never asked */
        CHECK(display_pin_surface(&q, &a, 1) == 1);
        CHECK(display_pin_next(&q, 60, 0) == DISPLAY_PIN_NOTHING && q.asked == 0);
    }

    display_pin_text(DISPLAY_PIN_SET_AGAIN, 60, 60, "ANativeWindow_setFrameRate", 0, line,
                     sizeof line);
    CHECK_STR(line, "frame rate pin: asked again for a new surface: 60 Hz (fixed source; the "
                    "Frame rate row is 60) with ANativeWindow_setFrameRate: returned 0");
    display_pin_text(DISPLAY_PIN_SET, 60, 60, "ANativeWindow_setFrameRate", 0, line, sizeof line);
    CHECK_STR(line, "frame rate pin: asked 60 Hz (fixed source; the Frame rate row is 60) with "
                    "ANativeWindow_setFrameRate: returned 0");
    display_pin_text(DISPLAY_PIN_SET, 120, 120, "ANativeWindow_setFrameRate", -22, line,
                     sizeof line);
    CHECK(strstr(line, "returned -22 (refused)") != NULL);
    display_pin_text(DISPLAY_PIN_RELEASE, 0, -1, "ANativeWindow_setFrameRate", 0, line,
                     sizeof line);
    CHECK_STR(line, "frame rate pin: released (the Frame rate row is Uncapped) with "
                    "ANativeWindow_setFrameRate: returned 0");
    display_pin_text(DISPLAY_PIN_RELEASE, 0, 0, NULL, 0, line, sizeof line);
    CHECK(strstr(line, "Original") != NULL && strstr(line, "(no call)") != NULL);
    display_pin_text(DISPLAY_PIN_NOTHING, 0, 0, NULL, 0, line, sizeof line);
    CHECK_STR(line, "");

    /* Android's display timing on offer (read only) */
    display_choreo_text(1, 1, 0, line, sizeof line);
    CHECK_STR(line, "display timing on Android (read only, nothing called): AChoreographer frame "
                    "callbacks reachable (AChoreographer_getInstance yes, "
                    "AChoreographer_postFrameCallback64 yes, AChoreographer_postVsyncCallback no)");
    display_choreo_text(0, 1, 1, line, sizeof line);
    CHECK(strstr(line, "callbacks not reachable (AChoreographer_getInstance no,") != NULL);
    display_choreo_text(1, 0, 0, line, sizeof line);
    CHECK(strstr(line, "callbacks not reachable") != NULL);
    display_choreo_text(1, 0, 1, line, sizeof line);
    CHECK(strstr(line, "callbacks reachable") != NULL &&
          strstr(line, "AChoreographer_postVsyncCallback yes)") != NULL);
}

static void testTop(void)
{
    static const char call[] = "ANativeWindow_setFrameRateWithChangeStrategy";
    const int W = 1080, H = 2364;
    DisplayTop t;
    DisplayTopSkip skip = DISPLAY_TOP_ASK;
    char line[400];

    /* the Pixel 10 Pro Fold's kind of list: 60 and 120 at one size */
    {
        const DisplayModeRate m[] = {{W, H, 60.0}, {W, H, 120.0}};

        display_top_rate(m, 2, W, H, &t);
        CHECK(t.listed == 2 && t.nRates == 2 && t.rates[0] == 60.0 && t.rates[1] == 120.0);
        CHECK(t.hz == 120.0 && t.w == W && t.h == H);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, &skip) == 120);
        CHECK(skip == DISPLAY_TOP_ASK);
        display_pin_top_text(DISPLAY_PIN_SET, &t, call, 0, line, sizeof line);
        CHECK_STR(line, "frame rate pin: asked 120 Hz (fixed source; the display's highest of 60, "
                        "120 at 1080x2364; the Frame rate row is Uncapped) with "
                        "ANativeWindow_setFrameRateWithChangeStrategy: returned 0");
        display_pin_top_text(DISPLAY_PIN_SET_AGAIN, &t, call, 0, line, sizeof line);
        CHECK_STR(line, "frame rate pin: asked again for a new surface: 120 Hz (fixed source; the "
                        "display's highest of 60, 120 at 1080x2364; the Frame rate row is "
                        "Uncapped) with ANativeWindow_setFrameRateWithChangeStrategy: returned 0");
        display_pin_top_text(DISPLAY_PIN_SET, &t, call, -22, line, sizeof line);
        CHECK(strstr(line, "returned -22 (refused)") != NULL);
        display_pin_top_text(DISPLAY_PIN_SET, &t, NULL, -1000, line, sizeof line);
        CHECK(strstr(line, "with (no call): returned -1000 (refused)") != NULL);
        display_pin_top_text(DISPLAY_PIN_RELEASE, &t, call, 0, line, sizeof line);
        CHECK_STR(line, "");
        display_pin_top_text(DISPLAY_PIN_NOTHING, &t, call, 0, line, sizeof line);
        CHECK_STR(line, "");
        {
            char small[16];

            display_pin_top_text(DISPLAY_PIN_SET, &t, call, 0, small, sizeof small);
            CHECK(strlen(small) == 15);
        }

        /* the order of the rule */
        CHECK(display_pin_want(60, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, &skip) == 60 &&
              skip == DISPLAY_TOP_NOT_UNCAPPED);
        CHECK(display_pin_want(0, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_NOT_UNCAPPED);
        CHECK(display_pin_want(60, DISPLAY_PIN_OFF, &t, 1, 0, 1, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_NOT_UNCAPPED);
        CHECK(display_pin_want(-1, DISPLAY_PIN_OFF, &t, 1, 0, 1, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_PIN_OFF);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 0, 0, 1, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_NO_SEAMLESS);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 1, 1, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_REFUSED);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 0, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_UNFOCUSED);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, NULL) == 120);
        {
            DisplayPin held = {0};

            held.asked = 120;
            held.uncapped = 1;
            CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 0, &held, &skip) == 120 &&
                  skip == DISPLAY_TOP_UNFOCUSED);
            held.uncapped = 0; /* the row 120's ask is not Uncapped's to keep */
            CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 0, &held, &skip) == 0);
        }
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, NULL, 1, 0, 1, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_NO_LIST);
    }

    /* ties: a rate listed twice (two modes of one size and rate) is one;
       120.00 and 120.02 are one rate, the first value kept */
    {
        const DisplayModeRate m[] = {
            {W, H, 120.0}, {W, H, 60.0}, {W, H, 120.0}, {W, H, 60.0}, {W, H, 120.02}};

        display_top_rate(m, 5, W, H, &t);
        CHECK(t.listed == 5 && t.nRates == 2 && t.rates[0] == 60.0 && t.hz == 120.0);
    }
    /* one mode: nothing to choose, today's behaviour */
    {
        const DisplayModeRate m[] = {{W, H, 60.0}};

        display_top_rate(m, 1, W, H, &t);
        CHECK(t.nRates == 1 && t.hz == 60.0);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_ONE_RATE);
        CHECK(display_pin_skip_text(skip, &t, 0, line, sizeof line) > 0);
        CHECK_STR(line, "frame rate pin: Uncapped asks for no rate: the display offers one rate "
                        "at 1080x2364 (60 Hz, 1 mode listed); the system chooses the refresh "
                        "rate as before");
    }
    /* two modes of one rate are one rate */
    {
        const DisplayModeRate m[] = {{W, H, 90.0}, {W, H, 90.0}};

        display_top_rate(m, 2, W, H, &t);
        CHECK(t.nRates == 1);
        display_pin_skip_text(DISPLAY_TOP_ONE_RATE, &t, 0, line, sizeof line);
        CHECK(strstr(line, "(90 Hz, 2 modes listed)") != NULL);
    }
    /* none: an empty list, and a list with no mode of the current size */
    {
        const DisplayModeRate m[] = {{H, W, 60.0}, {H, W, 120.0}};

        display_top_rate(m, 0, W, H, &t);
        CHECK(t.listed == 0 && t.nRates == 0 && t.hz == 0.0);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_NO_SIZE);
        display_pin_skip_text(skip, &t, 0, line, sizeof line);
        CHECK_STR(line, "frame rate pin: Uncapped asks for no rate: the display lists no mode at "
                        "the current mode's size 1080x2364 (0 modes listed); the system chooses "
                        "the refresh rate as before");
        display_top_rate(m, 2, W, H, &t); /* the other orientation's size is another size */
        CHECK(t.listed == 2 && t.nRates == 0);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_NO_SIZE);
        display_pin_skip_text(skip, &t, 0, line, sizeof line);
        CHECK(strstr(line, "(2 modes listed)") != NULL);
    }
    /* a list that could not be read */
    {
        display_top_rate(NULL, 3, W, H, &t);
        CHECK(t.listed == -1 && t.nRates == 0);
        display_top_rate(NULL, -1, 0, 0, &t);
        CHECK(t.listed == -1);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, &skip) == 0 &&
              skip == DISPLAY_TOP_NO_LIST);
        display_pin_skip_text(skip, &t, 0, line, sizeof line);
        CHECK_STR(line, "frame rate pin: Uncapped asks for no rate: the display's modes could not "
                        "be read; the system chooses the refresh rate as before");
    }
    /* other resolutions are not counted: 144 Hz at another size */
    {
        const DisplayModeRate m[] = {{720, 1580, 144.0},
                                     {W, H, 60.0},
                                     {2208, 1840, 120.0},
                                     {W, H, 120.0},
                                     {720, 1580, 60.0}};

        display_top_rate(m, 5, W, H, &t);
        CHECK(t.listed == 5 && t.nRates == 2 && t.hz == 120.0);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, &skip) == 120);
    }
    /* fractional rates: asked as listed, whole Hz in the state */
    {
        const DisplayModeRate m[] = {{W, H, 119.88}, {W, H, 59.94}};

        display_top_rate(m, 2, W, H, &t);
        CHECK(t.nRates == 2 && t.rates[0] == 59.94 && t.hz == 119.88);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, &skip) == 120);
        display_pin_top_text(DISPLAY_PIN_SET, &t, call, 0, line, sizeof line);
        CHECK_STR(line, "frame rate pin: asked 119.88 Hz (fixed source; the display's highest of "
                        "59.94, 119.88 at 1080x2364; the Frame rate row is Uncapped) with "
                        "ANativeWindow_setFrameRateWithChangeStrategy: returned 0");
    }
    /* 59.94 and 60.00 are two rates (0.06 Hz apart) */
    {
        const DisplayModeRate m[] = {{W, H, 60.0}, {W, H, 59.94}};

        display_top_rate(m, 2, W, H, &t);
        CHECK(t.nRates == 2 && t.rates[0] == 59.94 && t.hz == 60.0);
        CHECK(display_pin_want(-1, DISPLAY_PIN_AUTO, &t, 1, 0, 1, NULL, &skip) == 60);
    }
    /* invalid rates are not counted */
    {
        const DisplayModeRate m[] = {{W, H, 0.0}, {W, H, -1.0}, {W, H, 1001.0}, {W, H, 90.0}};

        display_top_rate(m, 4, W, H, &t);
        CHECK(t.listed == 4 && t.nRates == 1 && t.hz == 90.0);
    }
    /* more rates than kept: the first DISPLAY_TOP_RATES met, the line names
       the highest 8 */
    {
        DisplayModeRate m[40];

        for (int i = 0; i < 40; i++) {
            m[i].w = W;
            m[i].h = H;
            m[i].hz = (double)(i + 1);
        }
        display_top_rate(m, 40, W, H, &t);
        CHECK(t.nRates == DISPLAY_TOP_RATES && t.hz == (double)DISPLAY_TOP_RATES);
        display_pin_top_text(DISPLAY_PIN_SET, &t, call, 0, line, sizeof line);
        CHECK(strstr(line, "asked 32 Hz (fixed source; the display's highest of 32 rates, the "
                           "highest 8: 25, 26, 27, 28, 29, 30, 31, 32 at 1080x2364;") != NULL);
    }

    /* the other reasons' lines; none for an ask, a number or the pin off */
    display_pin_skip_text(DISPLAY_TOP_NO_SEAMLESS, NULL, 0, line, sizeof line);
    CHECK_STR(line, "frame rate pin: Uncapped asks for no rate: the device has no "
                    "ANativeWindow_setFrameRateWithChangeStrategy (Android 12 and later), the only "
                    "call Uncapped's ask is made with (it allows seamless switches only); the "
                    "system chooses the refresh rate as before");
    display_pin_skip_text(DISPLAY_TOP_REFUSED, NULL, -22, line, sizeof line);
    CHECK_STR(line, "frame rate pin: Uncapped asks for no rate: the system refused the ask "
                    "(returned -22), so it is not made again this run; the system chooses the "
                    "refresh rate as before");
    display_pin_skip_text(DISPLAY_TOP_UNFOCUSED, NULL, 0, line, sizeof line);
    CHECK_STR(line, "frame rate pin: Uncapped's ask waits for the window's focus (no new ask "
                    "while the window has none)");
    CHECK(display_pin_skip_text(DISPLAY_TOP_ASK, NULL, 0, line, sizeof line) == 0);
    CHECK_STR(line, "");
    CHECK(display_pin_skip_text(DISPLAY_TOP_NOT_UNCAPPED, NULL, 0, line, sizeof line) == 0);
    CHECK(display_pin_skip_text(DISPLAY_TOP_PIN_OFF, NULL, 0, line, sizeof line) == 0);

    /* the state: the same rate for the other reason is a new ask */
    {
        DisplayPin q = {0};
        int a = 1, b = 2;

        CHECK(display_pin_surface(&q, &a, 0) == 0);
        CHECK(display_pin_next_uncapped(&q, 120, 0, 1) == DISPLAY_PIN_SET && q.uncapped == 0);
        CHECK(display_pin_next_uncapped(&q, 120, 1, 1) == DISPLAY_PIN_SET && q.uncapped == 1);
        CHECK(display_pin_next_uncapped(&q, 120, 1, 1) == DISPLAY_PIN_NOTHING);
        CHECK(display_pin_next(&q, 120, 1) == DISPLAY_PIN_SET && q.uncapped == 0);
        CHECK(display_pin_next_uncapped(&q, 120, 1, 1) == DISPLAY_PIN_SET);
        /* Original: released, the flag with it */
        CHECK(display_pin_next_uncapped(&q, 0, 1, 1) == DISPLAY_PIN_RELEASE && q.asked == 0 &&
              q.uncapped == 0);
        CHECK(display_pin_next_uncapped(&q, 0, 0, 1) == DISPLAY_PIN_NOTHING);
        /* a device without the call is never asked */
        CHECK(display_pin_next_uncapped(&q, 120, 1, 0) == DISPLAY_PIN_NOTHING && q.asked == 0);
        /* a lost surface: asked again, said so */
        CHECK(display_pin_next_uncapped(&q, 120, 1, 1) == DISPLAY_PIN_SET);
        CHECK(display_pin_surface(&q, &b, 1) == 1 && q.asked == 0 && q.uncapped == 0);
        CHECK(display_pin_next_uncapped(&q, 120, 1, 1) == DISPLAY_PIN_SET_AGAIN && q.uncapped == 1);
        /* refused: none held */
        display_pin_failed(&q);
        CHECK(q.asked == 0 && q.uncapped == 0);
    }
}

/* the vsync lock's inputs as window_host.c video_settings computes them:
   window_refresh_ns's period ((Uint64)(1e9 / hz) above 1 Hz, else 0), the
   cap equal to the refresh with vsync on, the presents a step and the grid
   (the mailbox choice follows from these and the device's present timing,
   which a report does not touch) */
typedef struct LockIn {
    uint64_t refresh;
    int capEq;
    unsigned n;
    int grid;
} LockIn;

static LockIn lock_in(int framerate, double sdlHz, double reportedHz)
{
    const double hz = display_refresh_pick(framerate, sdlHz, reportedHz);
    const uint64_t sim = 16683333ull;
    LockIn L;

    L.refresh = hz > 1.0 ? (uint64_t)(1e9 / hz) : 0;
    L.capEq = pace_cap_equals_refresh(framerate, L.refresh);
    L.n = L.capEq ? pace_lock_presents(framerate, sim) : 0;
    L.grid = L.n > 0 && pace_grid_period_ok(L.n, L.refresh, sim);
    return L;
}

static int lock_same(LockIn a, LockIn b)
{
    return a.refresh == b.refresh && a.capEq == b.capEq && a.n == b.n && a.grid == b.grid;
}

static void testPick(void)
{
    static const int rows[] = {60, 120, 144, 30, 0};
    static const double sdl[] = {120.0, 60.0, 90.0, 144.0, 59.94};
    static const double reports[] = {60.0, 120.0, 90.0, 144.0, 59.94, 1.0, 1001.0};

    /* a number or Original: whatever the activity reports, the decision is
       the one made with no report (SDL's rate) */
    for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++) {
        for (size_t s = 0; s < sizeof sdl / sizeof sdl[0]; s++) {
            const LockIn before = lock_in(rows[r], sdl[s], 0.0);

            CHECK(display_refresh_pick(rows[r], sdl[s], 0.0) == sdl[s]);
            for (size_t k = 0; k < sizeof reports / sizeof reports[0]; k++) {
                CHECK(display_refresh_pick(rows[r], sdl[s], reports[k]) == sdl[s]);
                CHECK(lock_same(lock_in(rows[r], sdl[s], reports[k]), before));
            }
        }
    }
    /* the default (60, vsync on) on a 120 Hz start whose panel the pin
       moved to 60 and a touch to 120: SDL's 120 throughout, no lock, as
       before the listener */
    CHECK(!lock_in(60, 120.0, 60.0).capEq && !lock_in(60, 120.0, 120.0).capEq);
    /* a 60 Hz start: the lock as before, whatever the panel does after */
    CHECK(lock_in(60, 60.0, 120.0).capEq &&
          lock_same(lock_in(60, 60.0, 120.0), lock_in(60, 60.0, 0.0)));

    /* Uncapped: the reported period, never the lock */
    for (size_t s = 0; s < sizeof sdl / sizeof sdl[0]; s++) {
        for (size_t k = 0; k < 5; k++) {
            const LockIn L = lock_in(-1, sdl[s], reports[k]);

            CHECK(display_refresh_pick(-1, sdl[s], reports[k]) == reports[k]);
            CHECK(L.refresh == (uint64_t)(1e9 / reports[k]));
            CHECK(!L.capEq && L.n == 0 && !L.grid);
        }
        /* no report yet, or one out of range: SDL's */
        CHECK(display_refresh_pick(-1, sdl[s], 0.0) == sdl[s]);
        CHECK(display_refresh_pick(-1, sdl[s], 1.0) == sdl[s]);
        CHECK(display_refresh_pick(-1, sdl[s], 1001.0) == sdl[s]);
        CHECK(!lock_in(-1, sdl[s], 0.0).capEq);
    }
    CHECK(lock_in(-1, 60.0, 120.0).refresh == 8333333ull);

    /* the report guard */
    {
        DisplayReportSeen s = {0};

        CHECK(display_report_new(&s, 0.0, 5) == 0 && !s.have);
        CHECK(display_report_new(&s, 60.0, 5) == 1);
        CHECK(display_report_new(&s, 60.0, 5) == 0);
        CHECK(display_report_new(&s, 60.03, 5) == 0); /* within 0.05 Hz */
        CHECK(display_report_new(&s, 120.0, 5) == 1);
        CHECK(display_report_new(&s, 120.0, 5) == 0);
        CHECK(display_report_new(&s, 120.0, 6) == 1); /* the list changed */
        CHECK(display_report_new(&s, -1.0, 7) == 0 && s.modes == 6);
    }
}

int main(void)
{
    testWatch();
    testPin();
    testTop();
    testPick();
    printf("display_rate_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
