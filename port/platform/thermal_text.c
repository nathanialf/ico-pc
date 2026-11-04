/*
 * port/platform/thermal_text.c
 *
 * The thermal and screen recording words (thermal_text.h).
 */
#include "thermal_text.h"
#include <math.h>
#include <stdio.h>

static const char *const s_names[] = {"NONE",     "LIGHT",     "MODERATE", "SEVERE",
                                      "CRITICAL", "EMERGENCY", "SHUTDOWN"};

ThermalState thermal_state_none(void)
{
    ThermalState s;

    s.headroom = NAN;
    s.status = -1;
    s.recording = -1;
    return s;
}

static int has_headroom(const ThermalState *s)
{
    return !isnan(s->headroom) && s->headroom >= 0.0;
}

int thermal_state_any(const ThermalState *s)
{
    return has_headroom(s) || s->status >= 0 || s->recording >= 0;
}

const char *thermal_status_name(int status)
{
    return status >= 0 && status < (int)(sizeof s_names / sizeof s_names[0]) ? s_names[status]
                                                                             : "UNKNOWN";
}

int thermal_change_text(int oldStatus, int newStatus, double runSeconds, char *buf, size_t size)
{
    return snprintf(buf, size,
                    "ico_pc: thermal status changed: %s (%d) -> %s (%d) (run time %.1f s)",
                    thermal_status_name(oldStatus), oldStatus, thermal_status_name(newStatus),
                    newStatus, runSeconds);
}

int thermal_recording_text(int state, double runSeconds, char *buf, size_t size)
{
    return snprintf(buf, size, "ico_pc: screen recording: %s (run time %.1f s)",
                    state != 0 ? "visible" : "not visible", runSeconds);
}

int thermal_block_text(const ThermalState *s, const char *why, char *buf, size_t size)
{
    size_t at = 0;
    int n;

    if (size == 0) {
        return 0;
    }
    if (!thermal_state_any(s)) {
        return snprintf(buf, size, "thermal: not available (%s)", why != NULL ? why : "no reason");
    }
    n = snprintf(buf, size, "thermal:");
    at = (size_t)n < size ? (size_t)n : size - 1;
    if (has_headroom(s)) {
        n = snprintf(buf + at, size - at, " headroom %.2f (1.0 is the throttling point)",
                     s->headroom);
        at += (size_t)n < size - at ? (size_t)n : size - at - 1;
    }
    if (s->status >= 0) {
        n = snprintf(buf + at, size - at, "%s status %s (%d)", has_headroom(s) ? "," : "",
                     thermal_status_name(s->status), s->status);
        at += (size_t)n < size - at ? (size_t)n : size - at - 1;
    }
    if (s->recording >= 0) {
        n = snprintf(buf + at, size - at, "%s screen recording %s",
                     has_headroom(s) || s->status >= 0 ? ";" : "",
                     s->recording != 0 ? "visible" : "not visible");
        at += (size_t)n < size - at ? (size_t)n : size - at - 1;
    }
    return (int)at;
}

int thermal_around_text(const ThermalState *s, char *buf, size_t size)
{
    size_t at = 0;
    int n;

    if (size == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (has_headroom(s)) {
        n = snprintf(buf + at, size - at, "; thermal headroom %.2f", s->headroom);
        at += (size_t)n < size - at ? (size_t)n : size - at - 1;
    }
    if (s->status >= 0) {
        n = snprintf(buf + at, size - at, "%s status %s", has_headroom(s) ? "," : "; thermal",
                     thermal_status_name(s->status));
        at += (size_t)n < size - at ? (size_t)n : size - at - 1;
    }
    if (s->recording >= 0) {
        n = snprintf(buf + at, size - at, "; screen recording %s",
                     s->recording != 0 ? "visible" : "not visible");
        at += (size_t)n < size - at ? (size_t)n : size - at - 1;
    }
    return (int)at;
}
