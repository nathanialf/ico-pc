/*
 * port/render/rd_perf_gap.h
 *
 * The GPU's time between two timed replays (rd.h RdPerfRecord.gpuGapNs):
 * a replay's first timestamp less the last timestamp of the timed replay
 * before it, on the same GPU clock (rd_perf.c rd__perf_collect_gpu).
 *
 * Issue 123: a present that blocks for 350 ms is the moment the gap
 * matters most, and the records around such a stall are the ones most
 * likely to come without timestamps (a record whose slot was recycled
 * before its stamps were read is queued without them).  So the gap is kept
 * whatever untimed replays came between (`skipped` counts them: the gap
 * then holds their GPU work too, so it is an upper bound of the idle
 * time), and refused only past RD_PERF_GAP_MAX_NS either way (a clock
 * that jumped, a run left in the background); the first timed replay has
 * none, and a replay number not after the last (the counters started
 * again) starts again.
 *
 * Pure C, no globals: the unit test is port/render/test/rd_perf_gap_test.c.
 */
#ifndef RD_PERF_GAP_H
#define RD_PERF_GAP_H

#include <stdbool.h>
#include <stdint.h>

#define RD_PERF_GAP_MAX_NS 60000000000ll /* 60 s */

typedef struct RdPerfGap {
    uint32_t lastReplay;  /* the last timed replay (0: none yet) */
    uint64_t lastNs;      /* its last timestamp */
    uint64_t lastFirstNs; /* its first timestamp */
    uint32_t restarts;    /* the clock restarts seen (rd_perf_gap_step_ex) */
} RdPerfGap;

/* Timed replay `replay` with its first and last timestamps: true with
   *gapNs (its first less the last timed replay's last; under 0 begun
   before that one ended) and *skipped (the replays between without
   timestamps); false when there is none to measure (the first, a replay
   number not after the last, a gap over RD_PERF_GAP_MAX_NS either way).
   The replay becomes the last either way. */
bool rd_perf_gap_step(RdPerfGap *g, uint32_t replay, uint64_t firstNs, uint64_t lastNs,
                      int64_t *gapNs, uint32_t *skipped);

/* rd_perf_gap_step, and a clock that went back (issue 123): a replay
   cannot begin before the replay before it began, on one GPU clock. When
   firstNs is under the last replay's first stamp the clock was restarted
   between them (Adreno's timestamp counter, at a stall: the stalled
   replays' "begun 20699.4 ms before the one before ended" was the time the
   counter had run since its last restart, 14-36 s, not an overlap): no gap
   is measured (false), *backNs is how far back it went (lastNs less
   firstNs) and g->restarts counts it. *backNs is 0 for every other
   result. */
bool rd_perf_gap_step_ex(RdPerfGap *g, uint32_t replay, uint64_t firstNs, uint64_t lastNs,
                         int64_t *gapNs, uint32_t *skipped, uint64_t *backNs);

#endif /* RD_PERF_GAP_H */
