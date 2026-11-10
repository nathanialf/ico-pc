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
 *   rate     the Frame rate row 60, 120, 144, 30 and 25 ask that rate (in
 *            mHz: the system picks a refresh, the app asks its own rate,
 *            the Android frame rate guide); Original (0) asks the game's
 *            field rate, 59.94 Hz for an NTSC rate and 50 Hz for a PAL 50
 *            Hz one; Uncapped (-1) asks none; the pin off asks none (also
 *            for Original); above 1000 asks none;
 *   took     the rate the display took against the ask: 60 on 59.94
 *            taken, 120 on 59.94 2x, 120 on 30 4x, 60 on 30 2x, 50 on 25
 *            2x, 60 on 25 not a multiple, 100 on 50 2x, 144 on 59.94 not a
 *            multiple, an unknown rate; the lines word for word; "nothing
 *            asked" for Uncapped and for the pin off;
 *   pin      the sequence 60 (set) -> 60 (nothing) -> 120 (set) -> Uncapped
 *            (release) -> Uncapped (nothing); a device without the call is
 *            never asked and its state stays; a refused ask is asked again
 *            at the next change of the call; the surface: a new native
 *            window pointer or a resume (the pointer reused) forgets the
 *            ask, the next set is DISPLAY_PIN_SET_AGAIN; a resume with no
 *            rate asked, or the row turned to Uncapped before the next call,
 *            asks and says nothing; a device without the call is never asked;
 *   text     the pin's line for a set (rate, what it is, row, call,
 *            return, "(refused)" for a return other than 0), Original's
 *            59.94, a set again for a new surface and a release.
 */
#include <stdio.h>
#include <string.h>
#include "display_rate.h"

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

    CHECK(display_pin_rate(60, DISPLAY_PIN_AUTO, 60) == 60000);
    CHECK(display_pin_rate(120, DISPLAY_PIN_AUTO, 60) == 120000);
    CHECK(display_pin_rate(144, DISPLAY_PIN_AUTO, 60) == 144000);
    CHECK(display_pin_rate(30, DISPLAY_PIN_AUTO, 60) == 30000);
    CHECK(display_pin_rate(25, DISPLAY_PIN_AUTO, 50) == 25000);
    /* Original: the field rate (v0.11.2 asked none here: the issue 123 log's
       pin was "on" with no ask, the display left at 120 Hz) */
    CHECK(display_pin_rate(0, DISPLAY_PIN_AUTO, 60) == DISPLAY_PIN_NTSC_MHZ);
    CHECK(display_pin_rate(0, DISPLAY_PIN_AUTO, 50) == DISPLAY_PIN_PAL_MHZ);
    CHECK(DISPLAY_PIN_NTSC_MHZ == 59940 && DISPLAY_PIN_PAL_MHZ == 50000);
    CHECK(display_pin_rate(-1, DISPLAY_PIN_AUTO, 60) == 0);
    CHECK(display_pin_rate(60, DISPLAY_PIN_OFF, 60) == 0);
    CHECK(display_pin_rate(0, DISPLAY_PIN_OFF, 60) == 0);
    CHECK(display_pin_rate(1001, DISPLAY_PIN_AUTO, 60) == 0);

    /* the sequence */
    CHECK(display_pin_next(&p, 0, 1) == DISPLAY_PIN_NOTHING);
    CHECK(display_pin_next(&p, 60000, 1) == DISPLAY_PIN_SET && p.asked == 60000);
    CHECK(display_pin_next(&p, 60000, 1) == DISPLAY_PIN_NOTHING);
    CHECK(display_pin_next(&p, 120000, 1) == DISPLAY_PIN_SET && p.asked == 120000);
    CHECK(display_pin_next(&p, 0, 1) == DISPLAY_PIN_RELEASE && p.asked == 0);
    CHECK(display_pin_next(&p, 0, 1) == DISPLAY_PIN_NOTHING);
    /* a device without the call */
    CHECK(display_pin_next(&p, 60000, 0) == DISPLAY_PIN_NOTHING && p.asked == 0);
    /* a refused ask is tried again */
    CHECK(display_pin_next(&p, 60000, 1) == DISPLAY_PIN_SET);
    display_pin_failed(&p);
    CHECK(p.asked == 0);
    CHECK(display_pin_next(&p, 60000, 1) == DISPLAY_PIN_SET);

    /* the surface: a new native window holds no vote (the app came back) */
    {
        DisplayPin q = {0};
        int a = 1, b = 2;

        CHECK(display_pin_surface(&q, &a, 0) == 0); /* the first surface: nothing asked yet */
        CHECK(display_pin_next(&q, 60000, 1) == DISPLAY_PIN_SET && q.asked == 60000);
        CHECK(display_pin_surface(&q, &a, 0) == 0 && q.asked == 60000); /* the same: kept */
        CHECK(display_pin_next(&q, 60000, 1) == DISPLAY_PIN_NOTHING);
        /* a new pointer: the ask lost, made again and said so */
        CHECK(display_pin_surface(&q, &b, 0) == 1 && q.asked == 0);
        CHECK(display_pin_next(&q, 60000, 1) == DISPLAY_PIN_SET_AGAIN && q.asked == 60000);
        CHECK(display_pin_next(&q, 60000, 1) == DISPLAY_PIN_NOTHING);
        /* a resume with the pointer reused: asked again all the same */
        CHECK(display_pin_surface(&q, &b, 1) == 1 && q.asked == 0);
        CHECK(display_pin_next(&q, 60000, 1) == DISPLAY_PIN_SET_AGAIN);
        /* a resume with no rate asked: nothing to ask, nothing said */
        CHECK(display_pin_next(&q, 0, 1) == DISPLAY_PIN_RELEASE && q.asked == 0);
        CHECK(display_pin_surface(&q, &a, 1) == 0);
        CHECK(display_pin_next(&q, 0, 1) == DISPLAY_PIN_NOTHING && q.again == 0);
        CHECK(display_pin_next(&q, 120000, 1) == DISPLAY_PIN_SET); /* a plain ask after */
        /* lost, then the row turned to Uncapped before the next call: no
           release on the new surface, and the later ask is a plain one */
        CHECK(display_pin_surface(&q, &b, 1) == 1);
        CHECK(display_pin_next(&q, 0, 1) == DISPLAY_PIN_NOTHING && q.again == 0);
        CHECK(display_pin_next(&q, 60000, 1) == DISPLAY_PIN_SET);
        /* a device without the call: lost, never asked */
        CHECK(display_pin_surface(&q, &a, 1) == 1);
        CHECK(display_pin_next(&q, 60000, 0) == DISPLAY_PIN_NOTHING && q.asked == 0);
    }

    display_pin_text(DISPLAY_PIN_SET_AGAIN, 60000, 60, "ANativeWindow_setFrameRate", 0, line,
                     sizeof line);
    CHECK_STR(line, "frame rate pin: asked again for a new surface: 60.00 Hz (the frame rate cap; "
                    "the Frame rate row is 60) with ANativeWindow_setFrameRate: returned 0");
    display_pin_text(DISPLAY_PIN_SET, 60000, 60, "ANativeWindow_setFrameRate", 0, line,
                     sizeof line);
    CHECK_STR(line, "frame rate pin: asked 60.00 Hz (the frame rate cap; the Frame rate row is "
                    "60) with ANativeWindow_setFrameRate: returned 0");
    display_pin_text(DISPLAY_PIN_SET, DISPLAY_PIN_NTSC_MHZ, 0,
                     "ANativeWindow_setFrameRateWithChangeStrategy", 0, line, sizeof line);
    CHECK_STR(line, "frame rate pin: asked 59.94 Hz (the game's own field rate; the Frame rate "
                    "row is Original) with ANativeWindow_setFrameRateWithChangeStrategy: "
                    "returned 0");
    display_pin_text(DISPLAY_PIN_SET, 120000, 120, "ANativeWindow_setFrameRate", -22, line,
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
}

static void testTook(void)
{
    char line[256];

    CHECK(display_pin_took(DISPLAY_PIN_NTSC_MHZ, 60.0) == 0);
    CHECK(display_pin_took(DISPLAY_PIN_NTSC_MHZ, 59.94) == 0);
    CHECK(display_pin_took(DISPLAY_PIN_NTSC_MHZ, 120.0) == 2);
    CHECK(display_pin_took(30000, 120.0) == 4);
    CHECK(display_pin_took(30000, 60.0) == 2);
    CHECK(display_pin_took(25000, 50.0) == 2);
    CHECK(display_pin_took(25000, 60.0) == -1);
    CHECK(display_pin_took(DISPLAY_PIN_PAL_MHZ, 100.0) == 2);
    CHECK(display_pin_took(DISPLAY_PIN_PAL_MHZ, 60.0) == -1);
    CHECK(display_pin_took(DISPLAY_PIN_NTSC_MHZ, 144.0) == -1);
    CHECK(display_pin_took(DISPLAY_PIN_NTSC_MHZ, 0.0) == -2);
    CHECK(display_pin_took(0, 60.0) == -2);

    display_pin_took_text(DISPLAY_PIN_NTSC_MHZ, 60.0, 10.2, line, sizeof line);
    CHECK_STR(line, "frame rate pin: the display runs 60.00 Hz with 59.94 Hz asked: the ask "
                    "taken (run time 10.2 s)");
    display_pin_took_text(DISPLAY_PIN_NTSC_MHZ, 120.0, 10.2, line, sizeof line);
    CHECK_STR(line, "frame rate pin: the display runs 120.00 Hz with 59.94 Hz asked: 2 x the "
                    "ask (the system kept a multiple; the pictures still divide it) (run time "
                    "10.2 s)");
    display_pin_took_text(25000, 60.0, 3.0, line, sizeof line);
    CHECK_STR(line, "frame rate pin: the display runs 60.00 Hz with 25.00 Hz asked: not a "
                    "multiple of the ask (the pictures do not divide it: uneven spacing) (run "
                    "time 3.0 s)");
    display_pin_took_text(30000, 0.0, 3.0, line, sizeof line);
    CHECK_STR(line, "frame rate pin: 30.00 Hz asked; the display's rate not known (run time "
                    "3.0 s)");

    display_pin_none_text(-1, DISPLAY_PIN_AUTO, line, sizeof line);
    CHECK_STR(line, "frame rate pin: nothing asked (the Frame rate row is Uncapped: the "
                    "display's own choice)");
    display_pin_none_text(0, DISPLAY_PIN_OFF, line, sizeof line);
    CHECK_STR(line, "frame rate pin: nothing asked ([dev] frame_rate_pin = off; the Frame rate "
                    "row is Original)");
}

int main(void)
{
    testWatch();
    testPin();
    testTook();
    printf("display_rate_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
