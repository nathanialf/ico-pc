/* port/platform/present_fallback.c: the FIFO fallback's decision and its
   words (present_fallback.h). */
#include "present_fallback.h"
#include <stdio.h>
#include <string.h>

IcoPresentFallbackMode ico_present_fallback_mode(const char *value)
{
    if (value == NULL) {
        return ICO_PRESENT_FALLBACK_AUTO;
    }
    if (strcmp(value, "off") == 0 || strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
        strcmp(value, "no") == 0) {
        return ICO_PRESENT_FALLBACK_OFF;
    }
    if (strcmp(value, "fifo") == 0) {
        return ICO_PRESENT_FALLBACK_FIFO;
    }
    return ICO_PRESENT_FALLBACK_AUTO;
}

/* word anywhere in s, any case (the driver texts' own spelling varies:
   "turnip Mesa driver", "Mesa 25.1.0") */
static int has_word(const char *s, const char *word)
{
    const size_t n = strlen(word);

    for (; s != NULL && *s != '\0'; s++) {
        size_t i = 0;

        while (i < n && s[i] != '\0' && (s[i] | 0x20) == (word[i] | 0x20)) {
            i++;
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

int ico_present_fallback_driver_eligible(unsigned vendorId, const char *driverText)
{
    return vendorId != 0 && !has_word(driverText, "mesa") && !has_word(driverText, "turnip");
}

int ico_present_fallback_qualcomm_own(unsigned vendorId, const char *driverText)
{
    return vendorId == ICO_PRESENT_FALLBACK_QCOM_VENDOR &&
           ico_present_fallback_driver_eligible(vendorId, driverText);
}

int ico_present_fallback_start_asked(IcoPresentFallbackMode mode, int android)
{
    return mode == ICO_PRESENT_FALLBACK_AUTO && android;
}

int ico_present_fallback_start(IcoPresentFallbackMode mode, int android, int vsync,
                               unsigned vendorId, const char *driverText)
{
    return ico_present_fallback_start_asked(mode, android) && !vsync &&
           ico_present_fallback_qualcomm_own(vendorId, driverText);
}

void ico_present_fallback_start_line(char *out, size_t size)
{
    snprintf(out, size,
             "window: present fifo (Qualcomm's own driver: FIFO from the start; [dev] "
             "present_fallback = off keeps mailbox)");
}

/* the held blocks older than the window before nowNs leave */
static void expire(IcoPresentFallback *f, unsigned long long nowNs)
{
    int drop = 0;

    while (drop < f->n && nowNs - f->atNs[drop] > ICO_PRESENT_FALLBACK_WINDOW_NS) {
        drop++;
    }
    if (drop > 0) {
        for (int i = drop; i < f->n; i++) {
            f->atNs[i - drop] = f->atNs[i];
            f->ms[i - drop] = f->ms[i];
        }
        f->n -= drop;
    }
}

/* the counted blocks' range and span into the taken fields */
static void record(IcoPresentFallback *f, unsigned long long nowNs)
{
    f->takenNs = nowNs;
    f->takenBlocks = f->blocks;
    f->takenMinMs = f->ms[0];
    f->takenMaxMs = f->ms[0];
    for (int i = 1; i < f->n; i++) {
        f->takenMinMs = f->ms[i] < f->takenMinMs ? f->ms[i] : f->takenMinMs;
        f->takenMaxMs = f->ms[i] > f->takenMaxMs ? f->ms[i] : f->takenMaxMs;
    }
    f->takenSpanS = (double)(nowNs - f->atNs[0]) / 1e9;
}

int ico_present_fallback_note(IcoPresentFallback *f, unsigned long long nowNs, double presentMs,
                              int gate)
{
    f->lastGate = gate;
    if (f->taken) {
        if (presentMs > ICO_PRESENT_FALLBACK_SLOW_MS) {
            f->slowSince++;
        }
        return 0;
    }
    if (presentMs < ICO_PRESENT_FALLBACK_BLOCK_MS) {
        return 0;
    }
    f->blocks++;
    expire(f, nowNs);
    if (f->n == ICO_PRESENT_FALLBACK_COUNT) {
        /* full: the oldest gives way (the newest COUNT are the ones that
           can make the count within the window) */
        for (int i = 1; i < f->n; i++) {
            f->atNs[i - 1] = f->atNs[i];
            f->ms[i - 1] = f->ms[i];
        }
        f->n--;
    }
    f->atNs[f->n] = nowNs;
    f->ms[f->n] = presentMs;
    f->n++;
    if (f->n < ICO_PRESENT_FALLBACK_COUNT) {
        return 0;
    }
    if (gate == ICO_PRESENT_FALLBACK_GATE_YES) {
        f->taken = 1;
        record(f, nowNs);
        return ICO_PRESENT_FALLBACK_SWITCH;
    }
    if (gate == ICO_PRESENT_FALLBACK_GATE_KEPT && !f->kept) {
        f->kept = 1;
        record(f, nowNs);
        return ICO_PRESENT_FALLBACK_KEPT;
    }
    return 0;
}

/* the driver text up to its first line break (Qualcomm's carries "Date:"
   and more on lines of their own) */
static void first_line(const char *s, char *out, size_t size)
{
    size_t n = 0;

    if (s == NULL) {
        s = "driver unknown";
    }
    while (s[n] != '\0' && s[n] != '\n' && s[n] != '\r') {
        n++;
    }
    snprintf(out, size, "%.*s", (int)n, s);
}

void ico_present_fallback_switch_line(const IcoPresentFallback *f, const char *adapter,
                                      const char *driverText, unsigned images, char *out,
                                      size_t size)
{
    char drv[160];

    first_line(driverText, drv, sizeof drv);
    snprintf(out, size,
             "window: present blocked %d times (%.0f-%.0f ms) in %.0f s on %s (%s) under mailbox "
             "(vsync off, %u images): presents now FIFO for this run ([dev] "
             "present_fallback=off keeps mailbox)",
             f->n, f->takenMinMs, f->takenMaxMs, f->takenSpanS,
             adapter != NULL ? adapter : "an unknown device", drv, images);
}

void ico_present_fallback_kept_line(const IcoPresentFallback *f, char *out, size_t size)
{
    snprintf(out, size,
             "window: present blocked %d times over %.0f ms in %.0f s under mailbox: mailbox "
             "kept ([dev] present_fallback=off)",
             f->n, ICO_PRESENT_FALLBACK_BLOCK_MS, f->takenSpanS);
}

void ico_present_fallback_block_suffix(const IcoPresentFallback *f, unsigned long long nowNs,
                                       char *out, size_t size)
{
    int k = 0;

    out[0] = '\0';
    if (f->taken) {
        snprintf(out, size, "; %u since presents went FIFO %.0f s ago", f->slowSince,
                 (double)(nowNs - f->takenNs) / 1e9);
        return;
    }
    if (f->lastGate != ICO_PRESENT_FALLBACK_GATE_YES) {
        return;
    }
    for (int i = 0; i < f->n; i++) {
        k += nowNs - f->atNs[i] <= ICO_PRESENT_FALLBACK_WINDOW_NS;
    }
    if (k > 0) {
        snprintf(out, size, "; %d of %d over %.0f ms in %.0f s toward FIFO", k,
                 ICO_PRESENT_FALLBACK_COUNT, ICO_PRESENT_FALLBACK_BLOCK_MS,
                 (double)ICO_PRESENT_FALLBACK_WINDOW_NS / 1e9);
    }
}

void ico_present_fallback_mode_suffix(const IcoPresentFallback *f, IcoPresentFallbackMode mode,
                                      int armed, int fifoStart, char *out, size_t size)
{
    if (f != NULL && f->taken) {
        snprintf(out, size, ", FIFO fallback taken after %u blocks", f->takenBlocks);
    } else if (mode == ICO_PRESENT_FALLBACK_FIFO) {
        snprintf(out, size, ", FIFO fallback set by [dev] present_fallback=fifo");
    } else if (fifoStart) {
        snprintf(out, size, ", FIFO from the start (Qualcomm's own driver)");
    } else if (armed) {
        snprintf(out, size, ", FIFO fallback armed");
    } else {
        out[0] = '\0';
    }
}
