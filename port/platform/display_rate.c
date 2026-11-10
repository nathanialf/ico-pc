/*
 * port/platform/display_rate.c
 *
 * The refresh rate's log lines and the frame rate pin's decisions
 * (display_rate.h).
 */
#include "display_rate.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int display_rate_note(DisplayRateWatch *w, double hz)
{
    if (!(hz > 0.0)) {
        return 0;
    }
    if (!w->have) {
        w->have = 1;
        w->hz = hz;
        return 0;
    }
    if (fabs(hz - w->hz) <= 0.05) {
        return 0;
    }
    w->hz = hz;
    w->changes++;
    return 1;
}

int display_rate_text(const DisplayRateWatch *w, int width, int height, const char *name,
                      double runSeconds, char *buf, size_t size)
{
    char hz[24], nm[96] = "";

    if (w->have) {
        snprintf(hz, sizeof hz, "%.2f Hz", w->hz);
    } else {
        snprintf(hz, sizeof hz, "rate not known");
    }
    if (name != NULL && name[0] != '\0') {
        snprintf(nm, sizeof nm, " \"%.80s\"", name);
    }
    return snprintf(buf, size,
                    "display refresh: %s, %dx%d%s (run time %.1f s, %u change%s this run)", hz,
                    width, height, nm, runSeconds, w->changes, w->changes == 1 ? "" : "s");
}

int display_rate_change_text(double oldHz, double newHz, int width, int height, double runSeconds,
                             unsigned change, char *buf, size_t size)
{
    return snprintf(buf, size,
                    "display refresh changed: %.2f Hz -> %.2f Hz, %dx%d (run time %.1f s, change "
                    "%u of this run)",
                    oldHz, newHz, width, height, runSeconds, change);
}

int display_rate_poll(DisplayRateWatch *w, double hz, unsigned events, double eventSeconds,
                      int width, int height, char *buf, size_t size)
{
    const double old = w->hz;

    if (size > 0) {
        buf[0] = '\0';
    }
    if (!display_rate_note(w, hz)) {
        return 0;
    }
    if (events > 0) {
        display_rate_change_text(old, hz, width, height, eventSeconds, w->changes, buf, size);
    } else {
        snprintf(buf, size, "display refresh changed without an event from SDL: %.2f Hz", hz);
    }
    return 1;
}

static int same_word(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if ((*a | 0x20) != (*b | 0x20)) {
            return 0;
        }
    }
    return *a == *b;
}

DisplayPinMode display_pin_mode(const char *value)
{
    if (value != NULL && (same_word(value, "off") || same_word(value, "false") ||
                          same_word(value, "0") || same_word(value, "no"))) {
        return DISPLAY_PIN_OFF;
    }
    return DISPLAY_PIN_AUTO;
}

int display_pin_rate(int framerate, DisplayPinMode mode, int vsyncHz)
{
    if (mode == DISPLAY_PIN_OFF || framerate < 0 || framerate > 1000) {
        return 0;
    }
    if (framerate == 0) {
        /* Original: the game's own field rate, which its 30 and 60 (25 and
           50) pictures a second divide */
        return vsyncHz == 50 ? DISPLAY_PIN_PAL_MHZ : DISPLAY_PIN_NTSC_MHZ;
    }
    return framerate * 1000;
}

int display_pin_surface(DisplayPin *p, const void *surface, int resumed)
{
    int lost = 0;

    if (surface != p->surface || resumed) {
        /* a new surface has no vote: nothing to release, the rate asked
           again */
        lost = p->asked != 0;
        p->surface = surface;
        p->asked = 0;
        p->again = p->again || lost;
    }
    return lost;
}

DisplayPinAct display_pin_next(DisplayPin *p, int want, int deviceOffers)
{
    if (!deviceOffers || want == p->asked) {
        if (want == 0) {
            p->again = 0; /* none wanted on the new surface: nothing to say */
        }
        return DISPLAY_PIN_NOTHING;
    }
    p->asked = want;
    if (want > 0 && p->again) {
        p->again = 0;
        return DISPLAY_PIN_SET_AGAIN;
    }
    p->again = 0;
    return want > 0 ? DISPLAY_PIN_SET : DISPLAY_PIN_RELEASE;
}

void display_pin_failed(DisplayPin *p)
{
    p->asked = 0;
}

static void row_name(int framerate, char *row, size_t size)
{
    if (framerate == 0) {
        snprintf(row, size, "Original");
    } else if (framerate < 0) {
        snprintf(row, size, "Uncapped");
    } else {
        snprintf(row, size, "%d", framerate);
    }
}

/* what the asked rate is, for the line */
static const char *rate_kind(int framerate)
{
    return framerate == 0 ? "the game's own field rate" : "the frame rate cap";
}

int display_pin_text(DisplayPinAct act, int rate, int framerate, const char *call, int ret,
                     char *buf, size_t size)
{
    char row[24];
    const double hz = (double)rate / 1000.0;

    row_name(framerate, row, sizeof row);
    if (call == NULL) {
        call = "(no call)";
    }
    if (act == DISPLAY_PIN_SET_AGAIN) {
        return snprintf(buf, size,
                        "frame rate pin: asked again for a new surface: %.2f Hz (%s; the Frame "
                        "rate row is %s) with %s: returned %d%s",
                        hz, rate_kind(framerate), row, call, ret, ret == 0 ? "" : " (refused)");
    }
    if (act == DISPLAY_PIN_SET) {
        return snprintf(buf, size,
                        "frame rate pin: asked %.2f Hz (%s; the Frame rate row is %s) with %s: "
                        "returned %d%s",
                        hz, rate_kind(framerate), row, call, ret, ret == 0 ? "" : " (refused)");
    }
    if (act == DISPLAY_PIN_RELEASE) {
        return snprintf(buf, size,
                        "frame rate pin: released (the Frame rate row is %s) with %s: returned %d",
                        row, call, ret);
    }
    if (size > 0) {
        buf[0] = '\0';
    }
    return 0;
}

int display_pin_none_text(int framerate, DisplayPinMode mode, char *buf, size_t size)
{
    char row[24];

    row_name(framerate, row, sizeof row);
    if (mode == DISPLAY_PIN_OFF) {
        return snprintf(buf, size,
                        "frame rate pin: nothing asked ([dev] frame_rate_pin = off; the Frame rate "
                        "row is %s)",
                        row);
    }
    return snprintf(buf, size,
                    "frame rate pin: nothing asked (the Frame rate row is %s: the display's own "
                    "choice)",
                    row);
}

int display_pin_took(int askedMhz, double displayHz)
{
    const double asked = (double)askedMhz / 1000.0;
    double n;

    if (!(displayHz > 0.0) || askedMhz <= 0) {
        return -2;
    }
    if (fabs(displayHz - asked) <= 0.5) {
        return 0;
    }
    n = floor(displayHz / asked + 0.5);
    if (n >= 2.0 && fabs(displayHz - n * asked) <= 0.005 * displayHz) {
        return (int)n;
    }
    return -1;
}

int display_pin_took_text(int askedMhz, double displayHz, double runSeconds, char *buf, size_t size)
{
    const int took = display_pin_took(askedMhz, displayHz);
    const double asked = (double)askedMhz / 1000.0;
    char what[128];

    if (took == -2) {
        return snprintf(buf, size,
                        "frame rate pin: %.2f Hz asked; the display's rate not known (run time "
                        "%.1f s)",
                        asked, runSeconds);
    }
    if (took == 0) {
        snprintf(what, sizeof what, "the ask taken");
    } else if (took > 0) {
        snprintf(what, sizeof what,
                 "%d x the ask (the system kept a multiple; the pictures still divide it)", took);
    } else {
        snprintf(what, sizeof what,
                 "not a multiple of the ask (the pictures do not divide it: uneven spacing)");
    }
    return snprintf(buf, size,
                    "frame rate pin: the display runs %.2f Hz with %.2f Hz asked: %s (run time "
                    "%.1f s)",
                    displayHz, asked, what, runSeconds);
}
