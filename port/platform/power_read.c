/*
 * port/platform/power_read.c
 *
 * The battery's state for the log (power_read.h): pure.
 */
#include "power_read.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

bool power_read_long(const char *text, size_t len, long long *out)
{
    size_t i = 0;
    while (i < len && isspace((unsigned char)text[i])) {
        i++;
    }
    bool neg = false;
    if (i < len && (text[i] == '-' || text[i] == '+')) {
        neg = text[i] == '-';
        i++;
    }
    long long v = 0;
    bool any = false;
    while (i < len && isdigit((unsigned char)text[i])) {
        if (v > (LLONG_MAX - 9) / 10) {
            return false; /* not a reading */
        }
        v = v * 10 + (text[i] - '0');
        i++;
        any = true;
    }
    if (!any) {
        return false;
    }
    *out = neg ? -v : v;
    return true;
}

/* text's first word (up to a space or a line's end), case kept */
static size_t word_of(const char *text, size_t len, char *w, size_t cap)
{
    size_t i = 0, k = 0;
    while (i < len && isspace((unsigned char)text[i])) {
        i++;
    }
    while (i < len && text[i] != '\n' && text[i] != '\r' && k + 1 < cap) {
        w[k++] = text[i++];
    }
    while (k > 0 && isspace((unsigned char)w[k - 1])) {
        k--;
    }
    w[k] = '\0';
    return k;
}

int power_read_status(const char *text, size_t len)
{
    char w[32];
    (void)word_of(text, len, w, sizeof w);
    if (strcmp(w, "Charging") == 0) {
        return POWER_STATUS_CHARGING;
    }
    if (strcmp(w, "Discharging") == 0) {
        return POWER_STATUS_DISCHARGING;
    }
    if (strcmp(w, "Not charging") == 0) {
        return POWER_STATUS_NOT_CHARGING;
    }
    if (strcmp(w, "Full") == 0) {
        return POWER_STATUS_FULL;
    }
    return POWER_STATUS_UNKNOWN;
}

bool power_read_is_battery(const char *text, size_t len)
{
    char w[32];
    (void)word_of(text, len, w, sizeof w);
    return strcmp(w, "Battery") == 0;
}

int power_status_from_android(int status)
{
    switch (status) {
    case 2:
        return POWER_STATUS_CHARGING;
    case 3:
        return POWER_STATUS_DISCHARGING;
    case 4:
        return POWER_STATUS_NOT_CHARGING;
    case 5:
        return POWER_STATUS_FULL;
    default:
        return POWER_STATUS_UNKNOWN;
    }
}

const char *power_status_name(int status)
{
    switch (status) {
    case POWER_STATUS_CHARGING:
        return "charging";
    case POWER_STATUS_DISCHARGING:
        return "discharging";
    case POWER_STATUS_NOT_CHARGING:
        return "not charging";
    case POWER_STATUS_FULL:
        return "full";
    default:
        return "status not known";
    }
}

bool power_from_sysfs(const char *current, const char *voltage, const char *capacity,
                      const char *status, PowerSample *out)
{
    long long v;
    memset(out, 0, sizeof(*out));
    out->level = -1;
    if (current != NULL && power_read_long(current, strlen(current), &v)) {
        out->haveCurrent = true;
        out->currentMa = (double)v / 1000.0; /* the kernel's uA */
    }
    if (voltage != NULL && power_read_long(voltage, strlen(voltage), &v) && v > 0) {
        out->haveVoltage = true;
        out->voltageMv = (double)v / 1000.0; /* uV */
    }
    if (capacity != NULL && power_read_long(capacity, strlen(capacity), &v) && v >= 0 && v <= 100) {
        out->haveLevel = true;
        out->level = (int)v;
    }
    if (status != NULL) {
        out->status = power_read_status(status, strlen(status));
    }
    return out->haveCurrent || out->haveVoltage || out->haveLevel ||
           out->status != POWER_STATUS_UNKNOWN;
}

bool power_from_android(const int v[4], PowerSample *out)
{
    memset(out, 0, sizeof(*out));
    out->level = -1;
    if (v[0] != INT_MIN) {
        out->haveCurrent = true;
        out->currentMa = (double)v[0] / 1000.0; /* uA */
    }
    if (v[1] > 0) {
        out->haveVoltage = true;
        out->voltageMv = (double)v[1];
    }
    if (v[2] >= 0 && v[2] <= 100) {
        out->haveLevel = true;
        out->level = v[2];
    }
    out->status = v[3] >= 0 ? power_status_from_android(v[3]) : POWER_STATUS_UNKNOWN;
    return out->haveCurrent || out->haveVoltage || out->haveLevel ||
           out->status != POWER_STATUS_UNKNOWN;
}

int power_pick(bool sysfsAny, bool sysfsCurrent, bool androidAny, bool androidCurrent)
{
    if (sysfsAny && sysfsCurrent) {
        return POWER_SOURCE_SYSFS;
    }
    if (androidAny && androidCurrent) {
        return POWER_SOURCE_ANDROID;
    }
    return sysfsAny ? POWER_SOURCE_SYSFS : androidAny ? POWER_SOURCE_ANDROID : POWER_SOURCE_NONE;
}

bool power_fresh(double atMs, double nowMs)
{
    return nowMs >= atMs && nowMs - atMs <= POWER_FRESH_MS;
}

void power_block_reset(PowerBlock *b)
{
    memset(b, 0, sizeof(*b));
    b->vMinMv = b->vMaxMv = -1.0;
    b->level = -1;
}

void power_block_add(PowerBlock *b, const PowerSample *s)
{
    b->n++;
    if (s->haveCurrent) {
        if (b->currents < POWER_BLOCK_MAX) {
            b->currentMa[b->currents] = s->currentMa;
        }
        b->currents++;
    }
    if (s->haveVoltage) {
        b->vMinMv = b->vMinMv < 0.0 || s->voltageMv < b->vMinMv ? s->voltageMv : b->vMinMv;
        b->vMaxMv = s->voltageMv > b->vMaxMv ? s->voltageMv : b->vMaxMv;
    }
    if (s->haveLevel) {
        b->level = s->level;
    }
    b->status = s->status;
}

typedef struct Out {
    char *out;
    size_t cap, at;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    if (o->cap == 0 || o->at + 1u >= o->cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(o->out + o->at, o->cap - o->at, fmt, ap);
    va_end(ap);
    if (w > 0) {
        o->at = o->at + (size_t)w < o->cap ? o->at + (size_t)w : o->cap - 1u;
    }
}

size_t power_block_text(const PowerBlock *b, int source, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    if (b->n == 0) {
        return 0;
    }
    put(&o, "battery each second:");
    if (b->currents > 0) {
        put(&o, " current");
        const unsigned k = b->currents < POWER_BLOCK_MAX ? b->currents : POWER_BLOCK_MAX;
        for (unsigned i = 0; i < k; i++) {
            put(&o, " %.0f", b->currentMa[i]);
        }
        put(&o, " mA");
        if (b->currents > POWER_BLOCK_MAX) {
            put(&o, " (the first %u of %u)", POWER_BLOCK_MAX, b->currents);
        }
    } else {
        put(&o, " no current");
    }
    if (b->vMinMv >= 0.0) {
        put(&o, ", voltage %.0f-%.0f mV", b->vMinMv, b->vMaxMv);
    }
    if (b->level >= 0) {
        put(&o, ", level %d %%", b->level);
    }
    put(&o, ", %s (%s; the current's sign as the device gives it)", power_status_name(b->status),
        source == POWER_SOURCE_SYSFS ? "sysfs" : "BatteryManager");
    return o.at;
}

size_t power_source_text(int source, const char *path, int sysErr, const char *androidWhy,
                         char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    char sys[64];
    if (sysErr != 0) {
        snprintf(sys, sizeof sys, "/sys/class/power_supply not readable (errno %d)", sysErr);
    } else {
        snprintf(sys, sizeof sys, "no battery in /sys/class/power_supply");
    }
    if (source == POWER_SOURCE_SYSFS) {
        put(&o,
            "battery: read once a second on the log thread from %s (current, voltage, level, "
            "status)",
            path != NULL ? path : "/sys/class/power_supply");
    } else if (source == POWER_SOURCE_ANDROID) {
        put(&o, "battery: %s: read once a second on the log thread from Android's BatteryManager",
            sys);
    } else {
        put(&o, "battery: not readable (%s; BatteryManager: %s): no battery lines", sys,
            androidWhy != NULL && androidWhy[0] != '\0' ? androidWhy : "no answer");
    }
    return o.at;
}
