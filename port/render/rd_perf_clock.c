/*
 * port/render/rd_perf_clock.c
 *
 * A replay's GPU clock against the CPU's (rd_perf_clock.h).
 */
#include "rd_perf_clock.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* the first of the recording order (from k on) whose index is in set, -1 none */
static int firstIn(const RdPerfClockIn *in, const uint32_t *set, bool want)
{
    for (uint32_t k = 0; k < in->orderN; k++) {
        const uint32_t i = in->order[k];
        if (rhi_ts_has(in->clk->recorded, i) && rhi_ts_has(set, i) == want) {
            return (int)i;
        }
    }
    return -1;
}

/* the last of the recording order whose index is in set, -1 none */
static int lastIn(const RdPerfClockIn *in, const uint32_t *set)
{
    int last = -1;
    for (uint32_t k = 0; k < in->orderN; k++) {
        const uint32_t i = in->order[k];
        if (rhi_ts_has(in->clk->recorded, i) && rhi_ts_has(set, i)) {
            last = (int)i;
        }
    }
    return last;
}

void rd_perf_clock_judge(const RdPerfClockIn *in, RdPerfClockVerdict *v)
{
    memset(v, 0, sizeof(*v));
    v->firstMissing = -1;
    const RhiTimestampClock *c = in->clk;
    if (c == NULL) {
        return;
    }
    uint64_t first = 0, last = 0;
    bool haveFirst = false;
    for (uint32_t k = 0; k < in->orderN; k++) {
        const uint32_t i = in->order[k];
        if (!rhi_ts_has(c->recorded, i)) {
            continue; /* the backend recorded none there */
        }
        v->recorded++;
        const bool got = rhi_ts_has(c->available, i) && i < in->n && in->ts[i] != 0;
        if (!got) {
            if (c->submits > 0) {
                /* (a slot whose lists were never submitted wrote none) */
                v->missing++;
                if (v->firstMissing < 0) {
                    v->firstMissing = (int)i;
                }
            }
            continue;
        }
        if (!haveFirst) {
            first = in->ts[i];
            haveFirst = true;
        }
        if (in->ts[i] > last) {
            last = in->ts[i];
        }
    }
    if (c->waitBeginNs != 0 && c->waitEndNs >= c->waitBeginNs) {
        v->waitMs = (double)(c->waitEndNs - c->waitBeginNs) / 1e6;
    }
    v->ranTwice = c->again;
    if (haveFirst) {
        v->counterMs = (double)first / 1e6;
        if (c->calibrated && c->gpuNs >= last && c->firstSubmitNs != 0) {
            /* stamp t at cpuNs - (gpuNs - t) on the CPU's clock */
            const double startCpu = (double)c->cpuNs - (double)(c->gpuNs - first);
            v->lateMs = (startCpu - (double)c->firstSubmitNs) / 1e6;
            v->lateValid = 1;
        } else if (v->waitMs > 0.0 && c->firstSubmitNs != 0 && last >= first) {
            /* the wait returned about when the last stamp was written */
            const double startCpu = (double)c->waitEndNs - (double)(last - first);
            v->lateMs = (startCpu - (double)c->firstSubmitNs) / 1e6;
            v->lateValid = 2;
        }
    }
    v->report = (c->traceN > 0 && v->waitMs >= RD_PERF_CLOCK_WAIT_MS) || v->ranTwice ||
                v->missing > 0 || in->backNs != 0;
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

static void name(const RdPerfClockIn *in, int index, char *buf, size_t n)
{
    if (index < 0) {
        snprintf(buf, n, "none");
    } else if (in->name != NULL) {
        in->name((uint32_t)index, in->user, buf, n);
    } else {
        snprintf(buf, n, "stamp %d", index);
    }
}

static const char *finishedWords(uint8_t f)
{
    return f == 2   ? "all of its submissions finished"
           : f == 1 ? "its first submission finished, not its last"
                    : "none of its submissions finished";
}

int rd_perf_clock_text(const RdPerfClockIn *in, const RdPerfClockVerdict *v, char *buf, size_t n)
{
    Out o = {buf, n, 0, 0};
    if (n > 0) {
        buf[0] = '\0';
    }
    const RhiTimestampClock *c = in->clk;
    if (c == NULL) {
        return 0;
    }
    char nm[96];
    put(&o, "GPU clock: replay %u: ", in->replay);
    if (v->lateValid == 1) {
        put(&o,
            "its first stamp %.1f ms after its first submit (the calibrated clocks, within "
            "%.1f us)",
            v->lateMs, (double)c->deviationNs / 1e3);
    } else if (v->lateValid == 2) {
        put(&o, "its first stamp about %.1f ms after its first submit (from its wait's end)",
            v->lateMs);
    } else {
        put(&o, "its first stamp not placed on the CPU's clock (%s)",
            c->calibrated ? "the counter restarted after it" : "no calibrated clocks, no wait");
    }
    put(&o, ", the counter at %.1f ms then", v->counterMs);
    if (in->backNs != 0) {
        put(&o, " (it went back %.1f ms since the replay before: a restart)",
            (double)in->backNs / 1e6);
    }
    put(&o, "; %u submissions", c->submits);
    if (v->waitMs <= 0.0) {
        put(&o, "; its work had finished when the CPU came to wait for it");
    } else {
        const double gap = c->lastSubmitNs && c->waitBeginNs >= c->lastSubmitNs
                               ? (double)(c->waitBeginNs - c->lastSubmitNs) / 1e6
                               : -1.0;
        put(&o, "; waited %.1f ms for it", v->waitMs);
        if (gap >= 0.0) {
            put(&o, " from %.1f ms after its last submit", gap);
        }
        if (c->traceN == 0) {
            put(&o, " (no trace: the wait did not pass %u ms)", (unsigned)RHI_TS_TRACE_AFTER_MS);
        } else {
            const RhiTsTracePoint *p0 = &c->trace[0];
            put(&o, ": at +%.1f ms %u of %u written", (double)p0->atUs / 1e3, (unsigned)p0->avail,
                v->recorded);
            if (p0->stale) {
                put(&o, " (the previous use's: its reset had not run, the GPU had not begun it)");
            } else {
                name(in, lastIn(in, p0->bits), nm, sizeof nm);
                put(&o, " (the last: %s)", nm);
            }
            put(&o, ", %s", finishedWords(p0->finished));
            if (c->stillUs > 0) {
                put(&o, "; nothing changed for %.1f ms from +%.1f ms with %u written%s",
                    (double)c->stillUs / 1e3, (double)c->stillFromUs / 1e3, c->stillAvail,
                    c->stillStale ? " (the previous use's)" : "");
            }
            if (c->again) {
                put(&o,
                    "; its written stamps fell from %u at +%.1f ms: its queries were reset again, "
                    "its first list ran a second time (a reset and replay of the work in flight)",
                    c->againFrom, (double)c->againAtUs / 1e3);
            }
            put(&o, "; points");
            const uint32_t show =
                c->traceN < RD_PERF_CLOCK_POINTS ? c->traceN : RD_PERF_CLOCK_POINTS;
            for (uint32_t k = 0; k < show; k++) {
                const RhiTsTracePoint *p = &c->trace[k];
                put(&o, " %u%s@+%.1f", (unsigned)p->avail, p->stale ? "s" : "",
                    (double)p->atUs / 1e3);
            }
            if (c->traceN > show || c->traceMore) {
                put(&o, " (%u more)", (unsigned)(c->traceN - show + c->traceMore));
            }
        }
    }
    if (v->missing > 0) {
        name(in, v->firstMissing, nm, sizeof nm);
        put(&o,
            "; %u of %u recorded stamps came back unwritten after its work was signalled "
            "finished (the first: %s)",
            v->missing, v->recorded, nm);
        const int firstGot = firstIn(in, c->available, true);
        if (firstGot < 0) {
            put(&o, ": none written");
        }
    } else {
        put(&o, "; all %u recorded stamps written", v->recorded);
    }
    if (c->listsUnsubmitted > 0) {
        /* recorded counts the submitted lists' stamps alone (vk_cmd.c
           vkr_collect_timestamps), so these are not among the unwritten */
        put(&o, "; %u of the slot's lists with stamps were recorded and not submitted (left out)",
            c->listsUnsubmitted);
    }
    return o.total;
}
