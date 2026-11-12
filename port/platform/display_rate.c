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
        p->uncapped = 0;
        p->again = p->again || lost;
    }
    return lost;
}

DisplayPinAct display_pin_next(DisplayPin *p, int want, int deviceOffers)
{
    return display_pin_next_uncapped(p, want, 0, deviceOffers);
}

DisplayPinAct display_pin_next_uncapped(DisplayPin *p, int want, int uncapped, int deviceOffers)
{
    const int up = want > 0 && uncapped != 0;

    if (!deviceOffers || (want == p->asked && (want == 0 || up == p->uncapped))) {
        if (want == 0) {
            p->again = 0; /* none wanted on the new surface: nothing to say */
        }
        return DISPLAY_PIN_NOTHING;
    }
    p->asked = want;
    p->uncapped = up;
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
    p->uncapped = 0;
}

int display_choreo_text(int getInstance, int frame64, int vsync, char *buf, size_t size)
{
    return snprintf(
        buf, size,
        "display timing on Android (read only, nothing called): AChoreographer frame "
        "callbacks %s (AChoreographer_getInstance %s, AChoreographer_postFrameCallback64 "
        "%s, AChoreographer_postVsyncCallback %s)",
        getInstance && (frame64 || vsync) ? "reachable" : "not reachable",
        getInstance ? "yes" : "no", frame64 ? "yes" : "no", vsync ? "yes" : "no");
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

void display_top_rate(const DisplayModeRate *modes, int n, int curW, int curH, DisplayTop *out)
{
    memset(out, 0, sizeof *out);
    out->w = curW;
    out->h = curH;
    if (modes == NULL || n < 0) {
        out->listed = -1;
        return;
    }
    out->listed = n;
    for (int i = 0; i < n; i++) {
        const double hz = modes[i].hz;
        int at = 0, same = 0;

        if (modes[i].w != curW || modes[i].h != curH || !(hz > 0.0) || hz > 1000.0) {
            continue;
        }
        for (int k = 0; k < out->nRates; k++) {
            if (fabs(out->rates[k] - hz) <= 0.05) {
                same = 1;
                break;
            }
        }
        if (same || out->nRates >= DISPLAY_TOP_RATES) {
            continue;
        }
        /* lowest first: the place of the first rate above this one */
        while (at < out->nRates && out->rates[at] < hz) {
            at++;
        }
        for (int k = out->nRates; k > at; k--) {
            out->rates[k] = out->rates[k - 1];
        }
        out->rates[at] = hz;
        out->nRates++;
    }
    out->hz = out->nRates > 0 ? out->rates[out->nRates - 1] : 0.0;
}

int display_pin_want(int framerate, DisplayPinMode mode, const DisplayTop *top, int seamless,
                     int refused, int focused, const DisplayPin *p, DisplayTopSkip *skip)
{
    DisplayTopSkip why;
    int want = 0;

    if (framerate >= 0) {
        why = DISPLAY_TOP_NOT_UNCAPPED;
        want = display_pin_rate(framerate, mode);
    } else if (mode == DISPLAY_PIN_OFF) {
        why = DISPLAY_TOP_PIN_OFF;
    } else if (!seamless) {
        why = DISPLAY_TOP_NO_SEAMLESS;
    } else if (refused) {
        why = DISPLAY_TOP_REFUSED;
    } else if (top == NULL || top->listed < 0) {
        why = DISPLAY_TOP_NO_LIST;
    } else if (top->nRates == 0) {
        why = DISPLAY_TOP_NO_SIZE;
    } else if (top->nRates == 1) {
        why = DISPLAY_TOP_ONE_RATE;
    } else if (!focused) {
        why = DISPLAY_TOP_UNFOCUSED;
        want = p != NULL && p->uncapped ? p->asked : 0;
    } else {
        why = DISPLAY_TOP_ASK;
        want = (int)(top->hz + 0.5);
    }
    if (skip != NULL) {
        *skip = why;
    }
    return want;
}

/* a rate in Hz as the lines write it: whole when it is (60, 120), else to
   two places (59.94, 119.88) */
static void hz_words(double hz, char *buf, size_t size)
{
    if (fabs(hz - floor(hz + 0.5)) < 0.005) {
        snprintf(buf, size, "%.0f", floor(hz + 0.5));
    } else {
        snprintf(buf, size, "%.2f", hz);
    }
}

/* "60, 120" (lowest first), or "12 rates, the highest 8: 72, ..., 120" */
static void rates_words(const DisplayTop *t, char *buf, size_t size)
{
    const int shown = t->nRates > 8 ? 8 : t->nRates;
    size_t len = 0;

    buf[0] = '\0';
    if (t->nRates > 8) {
        len = (size_t)snprintf(buf, size, "%d rates, the highest 8: ", t->nRates);
    }
    for (int k = t->nRates - shown; k < t->nRates && len < size; k++) {
        char hz[24];

        hz_words(t->rates[k], hz, sizeof hz);
        len +=
            (size_t)snprintf(buf + len, size - len, "%s%s", k > t->nRates - shown ? ", " : "", hz);
    }
}

int display_pin_top_text(DisplayPinAct act, const DisplayTop *top, const char *call, int ret,
                         char *buf, size_t size)
{
    char hz[24], rates[256];

    if (act != DISPLAY_PIN_SET && act != DISPLAY_PIN_SET_AGAIN) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return 0;
    }
    hz_words(top->hz, hz, sizeof hz);
    rates_words(top, rates, sizeof rates);
    return snprintf(buf, size,
                    "frame rate pin: asked %s%s Hz (fixed source; the display's highest of %s at "
                    "%dx%d; the Frame rate row is Uncapped) with %s: returned %d%s",
                    act == DISPLAY_PIN_SET_AGAIN ? "again for a new surface: " : "", hz, rates,
                    top->w, top->h, call != NULL ? call : "(no call)", ret,
                    ret == 0 ? "" : " (refused)");
}

int display_pin_skip_text(DisplayTopSkip skip, const DisplayTop *top, int ret, char *buf,
                          size_t size)
{
    static const char before[] = "the system chooses the refresh rate as before";
    char hz[24] = "";
    const int listed = top != NULL && top->listed > 0 ? top->listed : 0;

    if (top != NULL && top->nRates > 0) {
        hz_words(top->hz, hz, sizeof hz);
    }
    switch (skip) {
    case DISPLAY_TOP_NO_SEAMLESS:
        return snprintf(buf, size,
                        "frame rate pin: Uncapped asks for no rate: the device has no "
                        "ANativeWindow_setFrameRateWithChangeStrategy (Android 12 and later), the "
                        "only call Uncapped's ask is made with (it allows seamless switches "
                        "only); %s",
                        before);
    case DISPLAY_TOP_NO_LIST:
        return snprintf(buf, size,
                        "frame rate pin: Uncapped asks for no rate: the display's modes could not "
                        "be read; %s",
                        before);
    case DISPLAY_TOP_NO_SIZE:
        return snprintf(buf, size,
                        "frame rate pin: Uncapped asks for no rate: the display lists no mode at "
                        "the current mode's size %dx%d (%d mode%s listed); %s",
                        top != NULL ? top->w : 0, top != NULL ? top->h : 0, listed,
                        listed == 1 ? "" : "s", before);
    case DISPLAY_TOP_ONE_RATE:
        return snprintf(buf, size,
                        "frame rate pin: Uncapped asks for no rate: the display offers one rate "
                        "at %dx%d (%s Hz, %d mode%s listed); %s",
                        top != NULL ? top->w : 0, top != NULL ? top->h : 0, hz, listed,
                        listed == 1 ? "" : "s", before);
    case DISPLAY_TOP_REFUSED:
        return snprintf(buf, size,
                        "frame rate pin: Uncapped asks for no rate: the system refused the ask "
                        "(returned %d), so it is not made again this run; %s",
                        ret, before);
    case DISPLAY_TOP_UNFOCUSED:
        return snprintf(buf, size,
                        "frame rate pin: Uncapped's ask waits for the window's focus (no new ask "
                        "while the window has none)");
    default:
        break;
    }
    if (size > 0) {
        buf[0] = '\0';
    }
    return 0;
}
