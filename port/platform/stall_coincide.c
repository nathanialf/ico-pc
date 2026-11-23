/*
 * port/platform/stall_coincide.c
 *
 * What a stall coincided with (stall_coincide.h): pure.
 */
#include "stall_coincide.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void stall_medians_reset(StallMedians *m)
{
    memset(m, 0, sizeof(*m));
}

static int cmpf(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

double stall_medians_add(StallMedians *m, uint32_t key, double ms)
{
    uint32_t i = 0;
    while (i < m->used && m->k[i].key != key) {
        i++;
    }
    if (i == m->used) {
        if (m->used == STALL_KEYS) {
            return -1.0;
        }
        memset(&m->k[i], 0, sizeof(m->k[i]));
        m->k[i].key = key;
        m->used++;
    }
    double median = -1.0;
    const uint32_t held = m->k[i].n < STALL_RING ? m->k[i].n : STALL_RING;
    if (held >= STALL_MEDIAN_MIN) {
        float s[STALL_RING];
        memcpy(s, m->k[i].ms, held * sizeof(float));
        qsort(s, held, sizeof(float), cmpf);
        median = (held & 1u) ? (double)s[held / 2u]
                             : 0.5 * ((double)s[held / 2u - 1u] + (double)s[held / 2u]);
    }
    m->k[i].ms[m->k[i].n % STALL_RING] = (float)ms;
    m->k[i].n++;
    return median;
}

double stall_medians_peek(const StallMedians *m, uint32_t key)
{
    for (uint32_t i = 0; i < m->used; i++) {
        if (m->k[i].key != key) {
            continue;
        }
        const uint32_t held = m->k[i].n < STALL_RING ? m->k[i].n : STALL_RING;
        if (held < STALL_MEDIAN_MIN) {
            return -1.0;
        }
        float s[STALL_RING];
        memcpy(s, m->k[i].ms, held * sizeof(float));
        qsort(s, held, sizeof(float), cmpf);
        return (held & 1u) ? (double)s[held / 2u]
                           : 0.5 * ((double)s[held / 2u - 1u] + (double)s[held / 2u]);
    }
    return -1.0;
}

static int cmpd(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

int stall_pieces_judge(StallMoment *m, const double *ms, const double *median, unsigned n)
{
    double f[64], others[64];
    unsigned k = 0, best = 0;
    int bestAt = -1;

    m->pieceN = m->grown = 0;
    m->pieceFactor = m->othersFactor = m->factorMin = m->factorMax = 0.0;
    for (unsigned i = 0; i < n && k < 64u; i++) {
        if (!(median[i] >= STALL_RATIO_MIN_MS) || ms[i] < 0.0) {
            continue;
        }
        f[k] = ms[i] / median[i];
        if (bestAt < 0 || f[k] > f[best]) {
            best = k;
            bestAt = (int)i;
        }
        m->grown += f[k] >= STALL_GROWN ? 1u : 0u;
        k++;
    }
    if (k == 0) {
        return -1;
    }
    m->pieceN = k;
    m->pieceFactor = f[best];
    m->factorMin = m->factorMax = f[0];
    unsigned no = 0;
    for (unsigned i = 0; i < k; i++) {
        m->factorMin = f[i] < m->factorMin ? f[i] : m->factorMin;
        m->factorMax = f[i] > m->factorMax ? f[i] : m->factorMax;
        if (i != best) {
            others[no++] = f[i];
        }
    }
    if (no > 0) {
        qsort(others, no, sizeof(double), cmpd);
        m->othersFactor =
            (no & 1u) ? others[no / 2u] : 0.5 * (others[no / 2u - 1u] + others[no / 2u]);
    }
    return bestAt;
}

bool stall_pass_spike(double ms, double median)
{
    return median > 0.0 && ms > STALL_PASS_TIMES * median && ms > STALL_PASS_MS;
}

unsigned stall_classes(const StallMoment *m)
{
    unsigned bits = 0;
    if (m->haveWaits) {
        for (int i = 0; i < STALL_WAIT_THREADS; i++) {
            if (m->waitMs[i] > STALL_WAIT_MS) {
                bits |= 1u << STALL_CLASS_WAIT;
            }
        }
    }
    if (m->availMb >= 0 && m->floorMb > 0 && m->availMb < m->floorMb) {
        bits |= 1u << STALL_CLASS_MEMORY;
    }
    if (m->pressure == 1 && m->some10Before >= 0.0 && m->some10 > m->some10Before) {
        bits |= 1u << STALL_CLASS_PRESSURE;
    }
    if (m->gpuMhz > 0 && m->gpuMaxMhz > 0 && (double)m->gpuMhz < STALL_GPU_LOW * m->gpuMaxMhz) {
        bits |= 1u << STALL_CLASS_GPU;
    }
    if (m->resetReplay != 0 || m->hangReplay != 0) {
        bits |= 1u << STALL_CLASS_RESET;
    }
    if (m->pieceN >= 2u && m->pieceFactor >= STALL_GROWN && m->othersFactor < STALL_GROWN) {
        bits |= 1u << STALL_CLASS_ONE_PIECE;
    }
    if (m->pieceN >= 2u && m->grown == m->pieceN) {
        bits |= 1u << STALL_CLASS_GPU_WHOLE;
    }
    return bits ? bits : 1u << STALL_CLASS_NONE;
}

void stall_tally_add(StallTally *t, const StallMoment *m)
{
    const unsigned bits = stall_classes(m);
    t->stalls++;
    for (int c = 0; c < STALL_CLASSES; c++) {
        t->cls[c] += (bits >> c) & 1u;
    }
    t->gpuReadable = t->gpuReadable || m->gpuMhz > 0;
    t->hangs += m->hangReplay != 0 ? 1u : 0u;
}

bool stall_line_due(StallTally *t, StallLineBudget *b)
{
    bool due;
    if (t->written + t->leftOut < STALL_RARE_LINES && b->rare < STALL_RUN_LINES) {
        b->rare++;
        due = true; /* rare: every one */
    } else {
        b->excess++; /* v0.11.6's rule over the others */
        due = b->excess <= STALL_EXCESS_FIRST || b->excess % STALL_EXCESS_EVERY == 0u;
    }
    if (due) {
        t->written++;
    } else {
        t->leftOut++;
    }
    return due;
}

typedef struct Out {
    char *out;
    size_t cap, at;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    if (o->cap == 0) {
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

static const char *const k_class[STALL_CLASSES] = {
    "a core wait over 100 ms",       "memory under the floor",
    "memory pressure rising",        "the GPU clock low",
    "the GPU's own sign of a reset", "one piece grew",
    "the GPU slow as a whole",       "none of these"};

size_t stall_tally_text(const StallTally *t, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    put(&o, "stalls by what coincided: %u in the block", t->stalls);
    if (t->stalls == 0) {
        return o.at;
    }
    /* the hangs apart (a stamp unwritten: as many as the renderer's "GPU
       clock" stall lines with "came back unwritten" in the block) */
    put(&o,
        ", %u of them hangs (the GPU's own sign of a reset with a stamp unwritten) and %u "
        "other; all %u by class: ",
        t->hangs, t->stalls - t->hangs, t->stalls);
    for (int c = 0; c < STALL_CLASSES; c++) {
        put(&o, "%s%s %u", c ? ", " : "", k_class[c], t->cls[c]);
        if (c == STALL_CLASS_GPU && !t->gpuReadable) {
            put(&o, " (not readable)");
        }
    }
    if (t->leftOut > 0) {
        put(&o,
            "; %u stall lines left out (%u written: a block's first %u while the run has written "
            "fewer than %u, then the first %u and every %uth of the others)",
            t->leftOut, t->written, STALL_RARE_LINES, STALL_RUN_LINES, STALL_EXCESS_FIRST,
            STALL_EXCESS_EVERY);
    }
    return o.at;
}

static const char *const k_who[STALL_WAIT_THREADS] = {"game", "render", "submit", "SDL audio",
                                                      "system audio"};

/* the submissions around a stall (StallMoment.sub): each one's replay and
   place, what it held where known, its submit and its signal from the
   stall's start; then those in flight at the start */
static void subsText(Out *o, const StallMoment *m)
{
    if (m->subs == 0) {
        return;
    }
    unsigned seen = 0, maybe = 0;
    put(o, "; the submissions around it (from the stall's start):");
    for (unsigned k = 0; k < m->subs && k < STALL_SUBS; k++) {
        const StallSub *s = &m->sub[k];
        put(o, "%s replay %u's %u of %u (", k ? ";" : "", s->replay, s->index, s->of);
        if (s->lists == 0) {
            put(o, "an empty batch");
        } else if (s->named && s->listLo >= 0) {
            put(o, "lists %d-%d", s->listLo, s->listHi);
        } else if (s->named && s->presentStamps) {
            put(o, "the present's list");
        } else {
            put(o, "%u list%s", s->lists, s->lists == 1 ? "" : "s");
        }
        if (s->scene == 1u) {
            put(o, ", menu open, scene held");
        } else if (s->scene == 2u) {
            put(o, ", menu open, scene replayed");
        }
        if (s->named && s->pieces > 0) {
            put(o, ", %u pieces", s->pieces);
        }
        if (s->present) {
            put(o, "%s",
                s->stalledOn ? ", the stalled present waits on it"
                             : ", its replay's present waits on it");
        }
        if (s->named) {
            if (s->missing > 0) {
                put(o, "; %u stamps, %u unwritten", s->stamps, s->missing);
            } else {
                put(o, "; %u stamps, all written", s->stamps);
            }
        }
        if (s->hung) {
            put(o, "; it holds the first unwritten stamp");
        }
        put(o, ") submitted at %+.1f ms, ", s->submitMs);
        bool known = false, flight = false;
        if (s->signalKind == 1) {
            put(o, "signalled at %+.1f ms (the slot's wait's end)", s->signalMs);
            known = true;
            flight = s->submitMs <= 0.0 && s->signalMs > 0.0;
        } else if (s->signalKind == 2) {
            put(o, "signalled at or before %+.1f ms (finished when the slot's wait began)",
                s->signalMs);
            known = s->signalMs <= 0.0;
        } else if (s->signalKind == 3) {
            put(o, "signalled at or before %+.1f ms (its replay's last's; not seen apart)",
                s->signalMs);
            known = s->signalMs <= 0.0;
        } else {
            put(o, "its signal not known");
        }
        if (s->submitMs > 0.0) {
            continue; /* made after the stall began */
        }
        if (flight) {
            seen++;
        } else if (!known) {
            maybe++;
        }
    }
    put(o, "; in flight at the stall's start: %u seen, %u not known", seen, maybe);
}

size_t stall_moment_text(const StallMoment *m, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    if (m->gameCore >= 0) {
        put(&o, "the game thread on core %d", m->gameCore);
        if (m->gameMhz > 0) {
            put(&o, " (%d MHz)", m->gameMhz);
        }
    } else {
        put(&o, "the game thread's core not known");
    }
    if (m->renderCore >= 0) {
        put(&o, ", the render thread on core %d", m->renderCore);
        if (m->renderMhz > 0) {
            put(&o, " (%d MHz)", m->renderMhz);
        }
    }
    if (m->haveWaits) {
        const char *sep = "; in the last second the core waits: ";
        for (int i = 0; i < STALL_WAIT_THREADS; i++) {
            if (m->waitMs[i] >= 0.0) {
                put(&o, "%s%s %.0f ms", sep, k_who[i], m->waitMs[i]);
                sep = ", ";
            }
        }
    } else {
        put(&o, "; the core waits not sampled");
    }
    if (m->drivers > 0) {
        put(&o, "; the driver's threads:");
        for (int i = 0; i < m->drivers && i < STALL_DRIVERS; i++) {
            put(&o, "%s %s ran %.0f ms, waited %.0f ms", i ? "," : "", m->driverName[i],
                m->driverRunMs[i], m->driverWaitMs[i]);
        }
    }
    if (m->availMb >= 0) {
        put(&o, "; memory %ld MB available", m->availMb);
        if (m->floorMb > 0) {
            put(&o, " against the floor %ld MB (%s)", m->floorMb,
                m->availMb < m->floorMb ? "under it" : "above it");
        }
    } else {
        put(&o, "; device memory not known");
    }
    if (m->pressure == 1) {
        put(&o, "; pressure some %.1f %%, full %.1f %% (avg10", m->some10, m->full10);
        if (m->some10Before >= 0.0) {
            put(&o, "; some %.1f %% a second before: %s", m->some10Before,
                m->some10 > m->some10Before ? "rising" : "not rising");
        }
        put(&o, ")");
    } else if (m->pressure == 0) {
        put(&o, "; memory pressure not readable (errno %d)", m->pressureErr);
    }
    if (m->headroom >= 0.0) {
        put(&o, "; thermal headroom %.2f", m->headroom);
    }
    if (m->gpuMhz > 0) {
        put(&o, "; the GPU clock %d MHz", m->gpuMhz);
        if (m->gpuMaxMhz > 0) {
            put(&o, " of %d", m->gpuMaxMhz);
        }
    }
    if (m->hangReplay != 0 || m->resetReplay != 0) {
        put(&o, "; the GPU's own sign of a reset at replay %u:",
            m->hangReplay != 0 ? m->hangReplay : m->resetReplay);
        if (m->hangReplay != 0) {
            put(&o, " %u stamps came back unwritten after its work was signalled finished",
                m->stampsMissing);
            if (m->firstMissing[0] != '\0') {
                put(&o, " (the first: %s)", m->firstMissing);
            }
        }
        if (m->resetReplay != 0 && m->hangReplay != 0) {
            put(&o, ", and at replay %u:", m->resetReplay);
        }
        const char *in = " ";
        if (m->resetReplay != 0 && m->ranTwice) {
            put(&o, "%sits first list ran twice", in);
            in = ", ";
        }
        if (m->resetReplay != 0 && m->clockBack) {
            put(&o, "%sthe GPU's clock restarted", in);
        }
        if (m->hangReplay != 0) {
            put(&o, " (a hang)");
        } else {
            put(&o, " (not a hang: no stamp unwritten in the stalled replay or the two before it)");
        }
    } else if (m->idleReplay != 0) {
        put(&o, "; the GPU's clock restarted at replay %u ", m->idleReplay);
        if (m->idleMs >= 0.0) {
            put(&o, "after %.1f ms with no GPU work", m->idleMs);
        } else {
            put(&o, "after a span the game was not presenting (a pause, a capture, the app away)");
        }
        put(&o, ": not a sign of a reset");
    }
    if (m->pieceN > 0) {
        /* the stalled replay's pieces against their block medians */
        put(&o, "; its %u timed pieces against their block medians: ", m->pieceN);
        if (m->pieceN >= 2u && m->grown == m->pieceN) {
            put(&o, "every piece grew, x%.1f to x%.1f", m->factorMin, m->factorMax);
        } else {
            put(&o, "%s x%.1f", m->pieceName[0] ? m->pieceName : "a piece", m->pieceFactor);
            if (m->pieceN >= 2u) {
                put(&o, ", the others x%.1f (their median; %u of %u at x%.0f or more)",
                    m->othersFactor, m->grown, m->pieceN, STALL_GROWN);
            }
        }
    }
    if (m->idleKnown) {
        if (m->idleBeforeMs >= 0.0) {
            put(&o, "; the GPU idle %.1f ms before its work", m->idleBeforeMs);
        } else {
            put(&o, "; its work begun %.1f ms before the picture before ended", -m->idleBeforeMs);
        }
    }
    if (m->trisKnown) {
        put(&o, "; %.0f VU triangles against the block's median %.0f", m->tris, m->trisMedian);
        if (m->trisMedian >= STALL_RATIO_MIN_TRIS) {
            put(&o, " (x%.2f)", m->tris / m->trisMedian);
        }
    }
    subsText(&o, m);
    const unsigned bits = stall_classes(m);
    const char *sep = "; coincided with: ";
    for (int c = 0; c < STALL_CLASSES; c++) {
        if ((bits >> c) & 1u) {
            put(&o, "%s%s", sep, k_class[c]);
            sep = ", ";
        }
    }
    return o.at;
}

/* the number after "<word>=" in line at s (len bytes), true when found */
static bool numAfter(const char *s, size_t len, const char *word, double *out)
{
    const size_t w = strlen(word);
    for (size_t i = 0; i + w < len; i++) {
        if (memcmp(s + i, word, w) == 0) {
            char tmp[32];
            size_t k = 0;
            for (size_t j = i + w;
                 j < len && k + 1 < sizeof tmp && (isdigit((unsigned char)s[j]) || s[j] == '.');
                 j++) {
                tmp[k++] = s[j];
            }
            tmp[k] = '\0';
            if (k == 0) {
                return false;
            }
            *out = strtod(tmp, NULL);
            return true;
        }
    }
    return false;
}

/* the line beginning with prefix (at a line's start), its offset and
   length; false when none */
static bool lineOf(const char *buf, size_t len, const char *prefix, size_t *at, size_t *n)
{
    const size_t p = strlen(prefix);
    size_t i = 0;
    while (i < len) {
        size_t e = i;
        while (e < len && buf[e] != '\n') {
            e++;
        }
        if (e - i >= p && memcmp(buf + i, prefix, p) == 0) {
            *at = i;
            *n = e - i;
            return true;
        }
        i = e + 1;
    }
    return false;
}

bool stall_pressure_parse(const char *buf, size_t len, double *some10, double *full10)
{
    size_t a, n, b, m;
    if (!lineOf(buf, len, "some ", &a, &n) || !lineOf(buf, len, "full ", &b, &m)) {
        return false;
    }
    return numAfter(buf + a, n, "avg10=", some10) && numAfter(buf + b, m, "avg10=", full10);
}

static bool kbOf(const char *buf, size_t len, const char *key, long *out)
{
    size_t a, n;
    if (!lineOf(buf, len, key, &a, &n)) {
        return false;
    }
    size_t i = a + strlen(key);
    while (i < a + n && (buf[i] == ' ' || buf[i] == '\t')) {
        i++;
    }
    long v = 0;
    bool any = false;
    while (i < a + n && isdigit((unsigned char)buf[i])) {
        v = v * 10 + (buf[i] - '0');
        i++;
        any = true;
    }
    *out = v;
    return any;
}

bool stall_status_parse(const char *buf, size_t len, long *anonKb, long *fileKb, long *shmemKb)
{
    return kbOf(buf, len, "RssAnon:", anonKb) && kbOf(buf, len, "RssFile:", fileKb) &&
           kbOf(buf, len, "RssShmem:", shmemKb);
}

bool stall_status_swap(const char *buf, size_t len, long *swapKb)
{
    return kbOf(buf, len, "VmSwap:", swapKb);
}

bool stall_driver_name(const char *name)
{
    static const char *const k[] = {"mali", "kgsl", "adreno", "qgl", "pvr", "img", "gpu", "tu_"};
    char low[16];
    size_t n = 0;
    while (name[n] != '\0' && n + 1 < sizeof low) {
        low[n] = (char)tolower((unsigned char)name[n]);
        n++;
    }
    low[n] = '\0';
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (strncmp(low, k[i], strlen(k[i])) == 0) {
            return true;
        }
    }
    return false;
}

/* --- the block's stalls: their pieces' shares and their table ----------- */

void stall_table_reset(StallTable *t)
{
    memset(t, 0, sizeof(*t));
}

void stall_table_timed(StallTable *t, uint32_t replay, double gpuMs, const int *what,
                       const double *ms, unsigned n, double redMs, double presMs)
{
    t->timed++;
    t->gpuMs += gpuMs > 0.0 ? gpuMs : 0.0;
    for (unsigned i = 0; i < n; i++) {
        if (what[i] >= 0 && what[i] < STALL_SHARE_PIECES && ms[i] > 0.0) {
            t->pieceMs[what[i]] += ms[i];
        }
    }
    for (unsigned i = 0; i < t->n; i++) {
        StallRow *r = &t->row[i];
        if (r->after < 2u && r->replay < replay) {
            r->red[1u + r->after] = (float)redMs;
            r->pres[1u + r->after] = (float)presMs;
            r->after++;
        }
    }
}

void stall_table_add(StallTable *t, const StallRow *r)
{
    t->stalls++;
    const bool named = r->piece >= 0 && r->piece < STALL_SHARE_PIECES;
    const bool again = t->replays > 0 && r->replay == t->lastReplay;
    if (!again) {
        t->replays++;
        t->lastReplay = r->replay;
        t->lastNamed = false;
    }
    if (named && !t->lastNamed) {
        /* once a replay: its present's and its pass's stalls judge the same
           record's pieces */
        t->named++;
        t->byPiece[r->piece]++;
        t->lastNamed = true;
    }
    if (t->n == STALL_TABLE_MAX) {
        t->dropped++;
        return;
    }
    StallRow *o = &t->row[t->n++];
    *o = *r;
    o->after = 0;
    o->carried = 0;
    o->red[1] = o->red[2] = o->pres[1] = o->pres[2] = -1.0f;
}

static const char *piece_of(const char *const *names, int i, char *tmp, size_t cap)
{
    if (names != NULL && i >= 0 && i < STALL_SHARE_PIECES && names[i] != NULL &&
        names[i][0] != '\0') {
        return names[i];
    }
    snprintf(tmp, cap, "piece %d", i);
    return tmp;
}

static double pct(double a, double b)
{
    return b > 0.0 ? 100.0 * a / b : 0.0;
}

size_t stall_share_text(const StallTable *t, const char *const *names, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    if (t->stalls == 0) {
        return 0;
    }
    /* the pieces with a stall or 1 % of the GPU time, the largest share
       first */
    int order[STALL_SHARE_PIECES];
    unsigned k = 0;
    for (int i = 0; i < STALL_SHARE_PIECES; i++) {
        if (t->byPiece[i] > 0u || pct(t->pieceMs[i], t->gpuMs) >= 1.0) {
            order[k++] = i;
        }
    }
    for (unsigned i = 1; i < k; i++) {
        const int v = order[i];
        unsigned j = i;
        while (j > 0 && (t->pieceMs[order[j - 1]] < t->pieceMs[v] ||
                         (t->pieceMs[order[j - 1]] == t->pieceMs[v] && order[j - 1] > v))) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = v;
    }
    put(&o,
        "stalls against each piece's share of the GPU time (piece: stalled replays, their share "
        "of the stalled replays naming a piece, its share of the GPU time): %u stall%s in %u "
        "replay%s, %u of them naming a piece",
        t->stalls, t->stalls == 1u ? "" : "s", t->replays, t->replays == 1u ? "" : "s", t->named);
    for (unsigned i = 0; i < k; i++) {
        char tmp[24];
        const int p = order[i];
        put(&o, "; %s: %u, %.0f %%, %.0f %%", piece_of(names, p, tmp, sizeof tmp), t->byPiece[p],
            pct((double)t->byPiece[p], (double)t->named), pct(t->pieceMs[p], t->gpuMs));
    }
    put(&o, "; %u naming none (%u timed pictures, %.1f ms of GPU spans)", t->replays - t->named,
        t->timed, t->gpuMs);
    return o.at;
}

static bool row_ready(const StallRow *r, bool all)
{
    return all || r->carried || r->after >= 2u;
}

unsigned stall_table_ready(const StallTable *t, bool all)
{
    unsigned k = 0;
    for (unsigned i = 0; i < t->n; i++) {
        k += row_ready(&t->row[i], all) ? 1u : 0u;
    }
    return k;
}

static void put_ms3(Out *o, const char *what, const float *v)
{
    put(o, "%s ", what);
    for (int i = 0; i < 3; i++) {
        if (v[i] >= 0.0f) {
            put(o, "%s%.2f", i ? "/" : "", (double)v[i]);
        } else {
            put(o, "%s-", i ? "/" : "");
        }
    }
}

static void row_text(Out *o, const StallRow *r, const char *const *names)
{
    put(o, "r%u ", r->replay);
    if (r->stage == STALL_NO_STAGE) {
        put(o, "s? ");
    } else {
        put(o, "s%d ", r->stage);
    }
    if (r->kind == 0) {
        put(o, "present %.0f ms: ", (double)r->ms);
    } else {
        put(o, "pass %s%s%.1f ms x%.1f: ", r->pass, r->pass[0] ? " " : "", (double)r->ms,
            (double)r->passFactor);
    }
    if (r->piece >= 0) {
        char tmp[24];
        put(o, "%s %.1f ms x%.1f", piece_of(names, r->piece, tmp, sizeof tmp), (double)r->pieceMs,
            (double)r->pieceFactor);
    } else {
        put(o, "no piece judged");
    }
    if (r->tris < 0.0f) {
        put(o, "; tris not known");
    } else {
        put(o, "; tris %.0f", (double)r->tris);
    }
    if (r->tris < 0.0f) {
        /* nothing to compare */
    } else if (r->trisMed >= (float)STALL_RATIO_MIN_TRIS) {
        put(o, " x%.2f of the median", (double)(r->tris / r->trisMed));
    } else {
        put(o, " (no median)");
    }
    if (r->tris < 0.0f) {
        /* nothing to compare */
    } else if (r->trisMax >= (float)STALL_RATIO_MIN_TRIS) {
        put(o, ", x%.2f of the 2 s most", (double)(r->tris / r->trisMax));
    } else {
        put(o, ", none in the 2 s before");
    }
    put(o, "; ");
    put_ms3(o, "reduction", r->red);
    put(o, ", ");
    put_ms3(o, "present", r->pres);
    put(o, " ms");
    if (r->batteryKnown) {
        put(o, "; battery %.0f mA", (double)r->batteryMa);
    }
    if (r->phone[0] != '\0') {
        put(o, "; phone %s", r->phone);
    }
}

size_t stall_table_text(const StallTable *t, const char *const *names, bool all, unsigned part,
                        char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    const unsigned ready = stall_table_ready(t, all);
    const unsigned parts = (ready + STALL_TABLE_LINE - 1u) / STALL_TABLE_LINE;
    if (part >= parts) {
        return 0;
    }
    unsigned carried = 0;
    for (unsigned i = 0; i < t->n; i++) {
        carried += t->row[i].carried ? 1u : 0u;
    }
    put(&o, "stall table (part %u of %u; %u stall%s, %u of them from the block before", part + 1u,
        parts, ready, ready == 1u ? "" : "s", carried);
    if (ready < t->n) {
        put(&o, ", %u waiting for their next timed replays", t->n - ready);
    }
    if (t->dropped > 0) {
        put(&o, ", %u more past the %u held", t->dropped, STALL_TABLE_MAX);
    }
    put(&o, "): ");
    unsigned seen = 0, wrote = 0;
    for (unsigned i = 0; i < t->n && wrote < STALL_TABLE_LINE; i++) {
        if (!row_ready(&t->row[i], all)) {
            continue;
        }
        if (seen++ < part * STALL_TABLE_LINE) {
            continue;
        }
        if (wrote++ > 0) {
            put(&o, " | ");
        }
        row_text(&o, &t->row[i], names);
    }
    return o.at;
}

void stall_table_end(StallTable *t, bool all)
{
    unsigned keep = 0;
    for (unsigned i = 0; i < t->n; i++) {
        if (!row_ready(&t->row[i], all)) {
            t->row[keep] = t->row[i];
            t->row[keep].carried = 1;
            keep++;
        }
    }
    /* the kept rows at the front, the sums emptied */
    t->stalls = t->dropped = t->named = t->timed = t->replays = 0;
    t->lastReplay = 0;
    t->lastNamed = false;
    t->gpuMs = 0.0;
    memset(t->byPiece, 0, sizeof(t->byPiece));
    memset(t->pieceMs, 0, sizeof(t->pieceMs));
    t->n = keep;
    t->carried = keep;
}

void stall_tris_reset(StallTris *w)
{
    memset(w, 0, sizeof(*w));
}

void stall_tris_add(StallTris *w, double atMs, double tris)
{
    w->ms[w->at] = atMs;
    w->tris[w->at] = (float)tris;
    w->at = (w->at + 1u) % STALL_TRIS_RING;
    w->n += w->n < STALL_TRIS_RING ? 1u : 0u;
}

double stall_tris_max(const StallTris *w, double nowMs, double spanMs)
{
    double best = -1.0;
    for (unsigned i = 0; i < w->n; i++) {
        if (w->ms[i] >= nowMs - spanMs && (double)w->tris[i] > best) {
            best = (double)w->tris[i];
        }
    }
    return best;
}
