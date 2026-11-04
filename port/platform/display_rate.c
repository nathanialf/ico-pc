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

int display_pin_rate(int framerate, DisplayPinMode mode)
{
    if (mode == DISPLAY_PIN_OFF || framerate <= 0 || framerate > 1000) {
        return 0;
    }
    return framerate;
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

int display_pin_text(DisplayPinAct act, int rate, int framerate, const char *call, int ret,
                     char *buf, size_t size)
{
    char row[24];

    if (framerate == 0) {
        snprintf(row, sizeof row, "Original");
    } else if (framerate < 0) {
        snprintf(row, sizeof row, "Uncapped");
    } else {
        snprintf(row, sizeof row, "%d", framerate);
    }
    if (call == NULL) {
        call = "(no call)";
    }
    if (act == DISPLAY_PIN_SET_AGAIN) {
        return snprintf(buf, size,
                        "frame rate pin: asked again for a new surface: %d Hz (fixed source; the "
                        "Frame rate row is %s) with %s: returned %d%s",
                        rate, row, call, ret, ret == 0 ? "" : " (refused)");
    }
    if (act == DISPLAY_PIN_SET) {
        return snprintf(buf, size,
                        "frame rate pin: asked %d Hz (fixed source; the Frame rate row is %s) with "
                        "%s: returned %d%s",
                        rate, row, call, ret, ret == 0 ? "" : " (refused)");
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
