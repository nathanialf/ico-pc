/*
 * port/render/rd_perf_clock.h
 *
 * A replay's GPU clock against the CPU's (rhi.h rhi_read_timestamp_clock),
 * read when its frame slot is recycled (rd_perf.c rd__perf_collect_gpu).
 *
 * Issue 123 (Adreno 740 on Qualcomm's driver): presents that blocked about
 * 340 ms with the GPU finishing nothing and its timestamp counter restarted
 * before the stalled replay's first stamp (rd_perf_gap.h).  The best fit is
 * a GPU fault or hang and the kernel driver's reset and replay of the work
 * in flight, unproven: the stamps alone cannot say when the GPU began the
 * replay or whether it ran part of it first.  The verdict here can, from
 * the log alone:
 *   - late: the replay's first stamp after its first submit, from the
 *     calibrated clocks (exact within their deviation) or estimated from the
 *     wait's end (the GPU's last stamp about when the CPU's wait returned);
 *     a few ms is a GPU that began at once, about the stall's length less
 *     the replay's span is a GPU that ran none of it for that long;
 *   - the wait's trace: what the slot's stamps did while the CPU waited for
 *     it (stamps of the slot's previous use still there: its reset had not
 *     run, so the GPU had not begun the replay; the stamps written and then
 *     going away again: the replay's first list ran a second time, which is
 *     a reset and replay; the longest stretch with no change: the time the
 *     GPU did nothing of it, and the stamp it stopped after);
 *   - stamps the replay recorded that came back unwritten once its work was
 *     signalled finished (the GPU skipped part of it).
 * The renderer logs the line on any of: a wait of RD_PERF_CLOCK_WAIT_MS or
 * more with a trace, a second run, a stamp missing, a counter restart.
 *
 * Pure C, no globals: the unit test is port/render/test/rd_perf_clock_test.c.
 */
#ifndef RD_PERF_CLOCK_H
#define RD_PERF_CLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rhi.h"

/* a wait at least this long with a trace gets the line */
#define RD_PERF_CLOCK_WAIT_MS 100.0
/* the trace points the line lists one by one */
#define RD_PERF_CLOCK_POINTS 8u

/* a stamp index's name into buf (a pass, a list's end, ...) */
typedef void (*RdPerfClockName)(uint32_t index, void *user, char *buf, size_t n);

typedef struct RdPerfClockIn {
    const RhiTimestampClock *clk;
    const uint64_t *ts; /* the slot's stamps, ns (rhi_read_timestamps), 0 not written */
    uint32_t n;
    const uint8_t *order; /* the indices the replay recorded, in recording order */
    uint32_t orderN;
    uint32_t replay;
    uint64_t backNs; /* the counter went back this much before this replay (0: not) */
    RdPerfClockName name;
    void *user;
} RdPerfClockIn;

typedef struct RdPerfClockVerdict {
    uint32_t recorded, missing; /* recorded indices; of them not written */
    int firstMissing;           /* in recording order, -1 none */
    int lateValid;              /* 1 calibrated, 2 from the wait's end, 0 not known */
    double lateMs;              /* the first stamp after the first submit */
    double counterMs;           /* the first stamp's value: the counter's age then */
    double waitMs;              /* the recycle's wait, 0 it did not block */
    bool ranTwice;
    bool report; /* a line is due */
} RdPerfClockVerdict;

void rd_perf_clock_judge(const RdPerfClockIn *in, RdPerfClockVerdict *v);

/* The line (without "rd: ") into buf; returns snprintf's count. */
int rd_perf_clock_text(const RdPerfClockIn *in, const RdPerfClockVerdict *v, char *buf, size_t n);

#endif /* RD_PERF_CLOCK_H */
