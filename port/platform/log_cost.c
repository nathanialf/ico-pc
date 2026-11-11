/*
 * port/platform/log_cost.c
 *
 * The logging's own cost (log_cost.h).
 */
#include "log_cost.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void log_cost_reset(LogCost *c)
{
    memset(c, 0, sizeof(*c));
    c->bytes = -1;
}

static void keep(LogCostSpan *s, unsigned *n, unsigned *all, unsigned cap, double t0, double t1)
{
    if (all != NULL) {
        (*all)++;
    }
    if (*n < cap) {
        s[*n].t0 = t0;
        s[*n].t1 = t1 >= t0 ? t1 : t0;
        (*n)++;
    }
}

void log_cost_present(LogCost *c, double t0, double t1)
{
    keep(c->present, &c->presentN, &c->presentAll, LOG_COST_SPANS, t0, t1);
}

void log_cost_late(LogCost *c, double t0, double t1)
{
    keep(c->late, &c->lateN, &c->lateAll, LOG_COST_SPANS, t0, t1);
}

void log_cost_writes(LogCost *c, unsigned writes, double writeMs, double longestMs,
                     const char *longestFamily, double logcatMs)
{
    c->thread = 1;
    c->writes += writes;
    c->writeMs += writeMs;
    c->logcatMs += logcatMs;
    if (longestMs > c->longestMs) {
        c->longestMs = longestMs;
        snprintf(c->longestFamily, sizeof(c->longestFamily), "%s",
                 longestFamily != NULL ? longestFamily : "");
    }
}

void log_cost_slow_write(LogCost *c, double t0, double t1)
{
    keep(c->slowW, &c->slowN, &c->slow, LOG_COST_SPANS, t0, t1);
}

void log_cost_pipe(LogCost *c, unsigned maxKB, unsigned capKB)
{
    c->pipeMaxKB = maxKB > c->pipeMaxKB ? maxKB : c->pipeMaxKB;
    if (capKB != 0) {
        c->pipeCapKB = capKB;
    }
}

void log_cost_job(LogCost *c, double gameMs, double t0, double t1)
{
    c->jobs++;
    c->jobGameMs += gameMs;
    c->jobWorkerMs += t1 > t0 ? t1 - t0 : 0.0;
    c->jobLongestGameMs = gameMs > c->jobLongestGameMs ? gameMs : c->jobLongestGameMs;
    keep(c->jobW, &c->jobN, NULL, LOG_COST_JOBS, t0, t1);
}

unsigned log_cost_overlaps(const LogCostSpan *a, unsigned na, const LogCostSpan *b, unsigned nb)
{
    unsigned n = 0;
    for (unsigned i = 0; i < na; i++) {
        for (unsigned j = 0; j < nb; j++) {
            if (a[i].t0 < b[j].t1 && b[j].t0 < a[i].t1) {
                n++;
                break;
            }
        }
    }
    return n;
}

typedef struct Out {
    char *buf;
    size_t n, at;
    int total;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *dst = o->at < o->n ? o->buf + o->at : NULL;
    const size_t room = o->at < o->n ? o->n - o->at : 0;
    const int w = vsnprintf(dst, room, fmt, ap);
    va_end(ap);
    if (w > 0) {
        o->total += w;
        o->at += (size_t)w;
    }
}

/* "N during a present that blocked, M during a late step" of spans s */
static void overlapWords(Out *o, const LogCost *c, const LogCostSpan *s, unsigned n)
{
    put(o, "%u during a present that blocked, %u during a late game step",
        log_cost_overlaps(s, n, c->present, c->presentN),
        log_cost_overlaps(s, n, c->late, c->lateN));
}

int log_cost_text(const LogCost *c, char *buf, size_t n)
{
    Out o = {buf, n, 0, 0};
    if (n > 0) {
        buf[0] = '\0';
    }
    put(&o, "logging: ");
    if (c->bytes >= 0) {
        put(&o, "%.1f KB written to the log in the block (every thread's)",
            (double)c->bytes / 1024.0);
    } else {
        put(&o, "the log's bytes not known");
    }
    if (c->printValid) {
        put(&o, "; the window thread's 10 s lines %.2f ms (formatting and writing; ", c->printMs);
        overlapWords(&o, c, &c->print, 1);
        put(&o, ")");
    }
    if (c->thread) {
        put(&o, "; the log thread's file writes %u in %.1f ms, the longest %.1f ms", c->writes,
            c->writeMs, c->longestMs);
        if (c->longestFamily[0] != '\0') {
            put(&o, " (from \"%s\")", c->longestFamily);
        }
        put(&o, ", %u of %.0f ms or more", c->slow, LOG_COST_SLOW_MS);
        if (c->slow > 0) {
            put(&o, " (");
            overlapWords(&o, c, c->slowW, c->slowN);
            if (c->slow > c->slowN) {
                put(&o, "; the first %u compared", c->slowN);
            }
            put(&o, ")");
        }
        put(&o, "; logcat %.1f ms", c->logcatMs);
        if (c->pipeCapKB != 0) {
            put(&o, "; the pipe at most %u of %u KB waiting (%s)", c->pipeMaxKB, c->pipeCapKB,
                c->pipeMaxKB >= c->pipeCapKB ? "full: a writer waited for the log thread"
                                             : "never full: no writer waited");
        }
    } else {
        put(&o, "; no log thread: each line written on its own thread");
    }
    if (c->jobs > 0) {
        put(&o,
            "; captures %u: %.1f ms on the game thread (the longest %.1f ms), %.0f ms on the "
            "worker (",
            c->jobs, c->jobGameMs, c->jobLongestGameMs, c->jobWorkerMs);
        overlapWords(&o, c, c->jobW, c->jobN);
        put(&o, ")");
    }
    put(&o, "; %u presents blocked, %u late game steps in the block", c->presentAll, c->lateAll);
    return o.total;
}
