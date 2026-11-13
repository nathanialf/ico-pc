/*
 * port/platform/core_share.c
 *
 * The share of run time on the faster cores (core_share.h): pure.
 */
#include "core_share.h"

#include <stdio.h>
#include <string.h>

#include "cpu_place.h" /* cpu_place_mask_text */

void core_share_reset(CoreShare *s)
{
    memset(s, 0, sizeof(*s));
}

void core_share_add(CoreShare *s, const char *name, int core, uint64_t runNs, uint64_t fastMask)
{
    if (core < 0 || core > 63 || fastMask == 0 || name == NULL) {
        return;
    }
    int i = 0;
    while (i < s->n && strncmp(s->t[i].name, name, sizeof(s->t[i].name) - 1u) != 0) {
        i++;
    }
    if (i == s->n) {
        if (s->n == CORE_SHARE_MAX) {
            return;
        }
        memset(&s->t[i], 0, sizeof(s->t[i]));
        snprintf(s->t[i].name, sizeof(s->t[i].name), "%.23s", name);
        s->n++;
    }
    if ((fastMask >> core) & 1u) {
        s->t[i].fastNs += (double)runNs;
    } else {
        s->t[i].slowNs += (double)runNs;
    }
}

/* snprintf at *at, kept at most cap - 1 */
static void put(char *out, size_t cap, size_t *at, const char *fmt, double a, double b,
                const char *name, const char *sep)
{
    const int w = snprintf(out + *at, cap - *at, fmt, sep, name, a, b);
    if (w > 0) {
        *at = *at + (size_t)w < cap ? *at + (size_t)w : cap - 1u;
    }
}

size_t core_share_text(const CoreShare *s, uint64_t fastMask, char *out, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    int any = 0;
    for (int i = 0; i < s->n; i++) {
        any += s->t[i].fastNs + s->t[i].slowNs > 0.0;
    }
    if (fastMask == 0 || any == 0) {
        return 0;
    }
    char m[96];
    (void)cpu_place_mask_text(fastMask, m, sizeof(m));
    int w = snprintf(out, cap, "run time on the faster cores %s: ", m);
    size_t at = w > 0 ? ((size_t)w < cap ? (size_t)w : cap - 1u) : 0u;
    int shown = 0;
    for (int i = 0; i < s->n; i++) {
        const double all = s->t[i].fastNs + s->t[i].slowNs;
        if (!(all > 0.0)) {
            continue;
        }
        put(out, cap, &at, "%s%s %.0f %% of %.1f s", 100.0 * s->t[i].fastNs / all, all / 1e9,
            s->t[i].name, shown ? ", " : "");
        shown++;
    }
    w = snprintf(out + at, cap - at,
                 " (each thread's last core once a second, weighted by its run time in that "
                 "second)");
    if (w > 0) {
        at = at + (size_t)w < cap ? at + (size_t)w : cap - 1u;
    }
    return at;
}

bool core_share_parse_stat(const char *buf, size_t len, int *core)
{
    size_t close = len;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == ')') {
            close = i;
        }
    }
    if (close == len) {
        return false;
    }
    /* after ") ": field 3 (the state) is the first token; field 39 the 37th */
    int field = 2;
    size_t i = close + 1;
    while (i < len) {
        while (i < len && buf[i] == ' ') {
            i++;
        }
        if (i >= len || buf[i] == '\n') {
            break;
        }
        field++;
        if (field == 39) {
            long v = 0;
            bool any = false;
            while (i < len && buf[i] >= '0' && buf[i] <= '9') {
                v = v * 10 + (buf[i] - '0');
                i++;
                any = true;
            }
            if (!any || v > 63) {
                return false;
            }
            *core = (int)v;
            return true;
        }
        while (i < len && buf[i] != ' ' && buf[i] != '\n') {
            i++;
        }
    }
    return false;
}
