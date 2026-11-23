/* rd_ratecard.c: the rate card's pure part (rd_ratecard.h): the medians,
 * the unit arithmetic, the printed block and the result file's decision.
 * No renderer state: port/render/test/rd_ratecard_test.c calls it alone. */
#include "rd_ratecard.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmpD(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

RdRcStat rd_rc_stat(const double *ms, uint32_t n)
{
    RdRcStat s;
    memset(&s, 0, sizeof(s));
    double v[64];
    uint32_t k = 0;
    for (uint32_t i = 0; ms != NULL && i < n && k < 64u; i++) {
        if (isfinite(ms[i]) && ms[i] > 0.0) {
            v[k++] = ms[i];
        }
    }
    if (k == 0) {
        return s;
    }
    qsort(v, k, sizeof(v[0]), cmpD);
    s.n = k;
    s.min = v[0];
    s.max = v[k - 1];
    s.med = k % 2u ? v[k / 2u] : 0.5 * (v[k / 2u - 1u] + v[k / 2u]);
    return s;
}

RdRcUnit rd_rc_none(const char *why)
{
    RdRcUnit u;
    memset(&u, 0, sizeof(u));
    snprintf(u.why, sizeof(u.why), "%s", why != NULL ? why : "?");
    return u;
}

RdRcUnit rd_rc_unit(RdRcStat a, RdRcStat b, double count, double scale)
{
    if (a.n < RD_RC_SAMPLES || (b.n != 0u && b.n < RD_RC_SAMPLES)) {
        char w[120];
        snprintf(w, sizeof(w), "%u and %u timed replays, %u wanted", (unsigned)a.n, (unsigned)b.n,
                 (unsigned)RD_RC_SAMPLES);
        return rd_rc_none(w);
    }
    if (!(count > 0.0) || !isfinite(count) || !(scale > 0.0)) {
        return rd_rc_none("nothing to divide by");
    }
    RdRcUnit u;
    memset(&u, 0, sizeof(u));
    const double bm = b.n ? b.med : 0.0, bmin = b.n ? b.min : 0.0, bmax = b.n ? b.max : 0.0;
    u.med = (a.med - bm) / count * scale;
    u.lo = (a.min - bmax) / count * scale;
    u.hi = (a.max - bmin) / count * scale;
    u.ok = isfinite(u.med) && isfinite(u.lo) && isfinite(u.hi);
    if (!u.ok) {
        return rd_rc_none("a time not finite");
    }
    return u;
}

RdRcUnit rd_rc_ratio(RdRcUnit a, RdRcUnit b)
{
    if (!a.ok || !b.ok) {
        return rd_rc_none(!a.ok ? a.why : b.why);
    }
    if (!(b.med > 0.0) || !(b.lo > 0.0)) {
        return rd_rc_none("its divisor not above 0");
    }
    RdRcUnit u;
    memset(&u, 0, sizeof(u));
    u.med = a.med / b.med;
    u.lo = a.lo / b.hi;
    u.hi = a.hi / b.lo;
    u.ok = isfinite(u.med) && isfinite(u.lo) && isfinite(u.hi);
    return u.ok ? u : rd_rc_none("a ratio not finite");
}

/* text appended at at (kept below cap), the new end */
#if defined(__GNUC__)
__attribute__((format(printf, 4, 5)))
#endif
static size_t put(char *out, size_t cap, size_t at, const char *fmt, ...)
{
    if (cap == 0 || at + 1u >= cap) {
        return at;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(out + at, cap - at, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return at;
    }
    return at + (size_t)n < cap ? at + (size_t)n : cap - 1u;
}

/* a value in its unit: ns and x with two decimals, ms with three */
static const char *fmtOf(const char *unit)
{
    return strcmp(unit, "ms") == 0 ? "%.3f" : "%.2f";
}

size_t rd_rc_text(const RdRateCard *c, char *out, size_t cap)
{
    size_t at = 0;
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (c == NULL) {
        return 0;
    }
    at = put(out, cap, at,
             "rate card: %s on SCENE %ux%u at %ux%u texels, %s; the median of %u timed replays a "
             "frame (min, max beside it); %.1f ms in all\n",
             c->device, (unsigned)c->w, (unsigned)c->h, (unsigned)c->tw, (unsigned)c->th, c->sized,
             (unsigned)RD_RC_SAMPLES, c->totalMs);
    for (int i = 0; i < RD_RC_ITEMS; i++) {
        const RdRcItem *it = &c->item[i];
        if (!it->u.ok) {
            at = put(out, cap, at, "rate card: %s: not measured (%s)\n", it->label, it->u.why);
            continue;
        }
        char med[32], lo[32], hi[32];
        const char *f = fmtOf(it->unit);
        snprintf(med, sizeof(med), f, it->u.med);
        snprintf(lo, sizeof(lo), f, it->u.lo);
        snprintf(hi, sizeof(hi), f, it->u.hi);
        at = put(out, cap, at, "rate card: %s: %s %s (min %s, max %s)\n", it->label, med, it->unit,
                 lo, hi);
    }
    return at;
}

/* the first line's word and date, the second line's key: the lengths
   (0: not this form) */
static size_t lineLen(const char *s)
{
    return strcspn(s, "\n");
}

int rd_rc_decide(const char *text, const char *key, char *date, size_t dateSize, const char **lines)
{
    if (date != NULL && dateSize > 0) {
        date[0] = '\0';
    }
    if (lines != NULL) {
        *lines = NULL;
    }
    if (text == NULL || key == NULL || text[0] == '\0') {
        return RD_RC_MEASURE;
    }
    static const char *const kWord[3] = {"measuring ", "done ", "blocked "};
    int state = -1;
    size_t wl = 0;
    for (int i = 0; i < 3; i++) {
        wl = strlen(kWord[i]);
        if (strncmp(text, kWord[i], wl) == 0) {
            state = i;
            break;
        }
    }
    const size_t l1 = lineLen(text);
    if (state < 0 || text[l1] != '\n') {
        return RD_RC_MEASURE; /* a file this program does not know: measured again */
    }
    const char *k = text + l1 + 1;
    const size_t l2 = lineLen(k);
    if (strncmp(k, "key ", 4) != 0 || l2 - 4u != strlen(key) || strncmp(k + 4, key, l2 - 4u) != 0) {
        return RD_RC_MEASURE; /* another device, driver, backend, size or version */
    }
    if (date != NULL && dateSize > 0) {
        snprintf(date, dateSize, "%.*s", (int)(l1 - wl), text + wl);
    }
    if (state == 1) {
        if (lines != NULL) {
            *lines = k[l2] == '\n' ? k + l2 + 1 : k + l2;
        }
        return RD_RC_REPRINT;
    }
    return RD_RC_SKIP; /* measuring (the run ended inside it) or blocked since */
}

size_t rd_rc_file_text(const char *state, const char *date, const char *key, const char *lines,
                       char *out, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    size_t at = put(out, cap, 0, "%s %s\nkey %s\n", state ? state : "?", date ? date : "?",
                    key ? key : "?");
    if (lines != NULL) {
        at = put(out, cap, at, "%s", lines);
    }
    return at;
}

bool rd_rc_parse(const char *lines, RdRateCard *c)
{
    if (lines == NULL || c == NULL) {
        return false;
    }
    memset(c, 0, sizeof(*c));
    const char *p = lines;
    int item = -1;
    while (*p != '\0') {
        const size_t n = lineLen(p);
        char line[512];
        snprintf(line, sizeof(line), "%.*s", (int)(n < sizeof(line) ? n : sizeof(line) - 1u), p);
        p += n;
        if (*p == '\n') {
            p++;
        }
        if (strncmp(line, "rate card: ", 11) != 0) {
            continue;
        }
        if (item < 0) {
            /* the head: its sizes and time */
            const char *on = strstr(line, " on SCENE ");
            if (on == NULL ||
                sscanf(on, " on SCENE %ux%u at %ux%u texels", &c->w, &c->h, &c->tw, &c->th) != 4) {
                return false;
            }
            snprintf(c->device, sizeof(c->device), "%.*s", (int)(on - line - 11), line + 11);
            const char *all = strstr(line, " ms in all");
            const char *semi = all ? all : NULL;
            while (semi != NULL && semi > line && *semi != ';') {
                semi--;
            }
            if (all == NULL || semi == NULL || sscanf(semi, "; %lf", &c->totalMs) != 1) {
                return false;
            }
            item = 0;
            continue;
        }
        if (item >= RD_RC_ITEMS) {
            break;
        }
        RdRcItem *it = &c->item[item++];
        const char *nm = strstr(line, ": not measured (");
        const char *colon = nm;
        if (colon == NULL) {
            /* the label's end: the last ": " before the value */
            for (const char *q = strstr(line + 11, ": "); q != NULL; q = strstr(q + 1, ": ")) {
                colon = q;
            }
        }
        if (colon == NULL) {
            return false;
        }
        snprintf(it->label, sizeof(it->label), "%.*s", (int)(colon - line - 11), line + 11);
        if (nm != NULL) {
            const char *w = nm + strlen(": not measured (");
            const char *e = strrchr(w, ')');
            snprintf(it->u.why, sizeof(it->u.why), "%.*s", (int)(e ? e - w : (long)strlen(w)), w);
            continue;
        }
        char unit[8] = "";
        if (sscanf(colon, ": %lf %7s (min %lf, max %lf)", &it->u.med, unit, &it->u.lo, &it->u.hi) !=
            4) {
            return false;
        }
        snprintf(it->unit, sizeof(it->unit), "%s", unit);
        it->u.ok = true;
    }
    c->have = item == RD_RC_ITEMS;
    return c->have;
}
