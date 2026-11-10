/*
 * port/render/rd_perf_gap.c
 *
 * The GPU's time between two timed replays (rd_perf_gap.h).
 */
#include "rd_perf_gap.h"

bool rd_perf_gap_step_ex(RdPerfGap *g, uint32_t replay, uint64_t firstNs, uint64_t lastNs,
                         int64_t *gapNs, uint32_t *skipped, uint64_t *backNs)
{
    bool ok = false;

    *gapNs = 0;
    *skipped = 0;
    *backNs = 0;
    if (g->lastReplay != 0 && replay > g->lastReplay) {
        const int64_t gap = (int64_t)(firstNs - g->lastNs);

        if (firstNs < g->lastFirstNs) {
            /* begun before the replay before began: the clock restarted */
            *backNs = g->lastNs - firstNs;
            g->restarts++;
        } else if (gap > -RD_PERF_GAP_MAX_NS && gap < RD_PERF_GAP_MAX_NS) {
            *gapNs = gap;
            *skipped = replay - g->lastReplay - 1u;
            ok = true;
        }
    }
    g->lastReplay = replay;
    g->lastNs = lastNs;
    g->lastFirstNs = firstNs;
    return ok;
}

bool rd_perf_gap_step(RdPerfGap *g, uint32_t replay, uint64_t firstNs, uint64_t lastNs,
                      int64_t *gapNs, uint32_t *skipped)
{
    uint64_t back;

    return rd_perf_gap_step_ex(g, replay, firstNs, lastNs, gapNs, skipped, &back);
}
