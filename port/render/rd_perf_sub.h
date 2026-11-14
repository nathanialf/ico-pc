/*
 * port/render/rd_perf_sub.h
 *
 * A replay's GPU submissions as its record carries them (rd.h
 * RdPerfRecord.gpuSub; issue 123): which of a stalled replay's submissions
 * held the stamps the GPU never wrote, what each held, when each was made.
 * Filled at the slot's collection (rd_perf.c rd__perf_collect_gpu, from
 * rhi.h RhiTimestampClock.sub: the CPU's times kept at each submit, no
 * Vulkan call), its contents (the lists, pieces and stamps) only for a
 * stalled replay's verdict (rd_perf_clock.h rd_perf_subs_judge).  Read by
 * the window's per-stall line (port/platform/window_stall.c).
 */
#ifndef RD_PERF_SUB_H
#define RD_PERF_SUB_H

#include <stdint.h>

/* the submissions a record keeps (rhi.h RHI_TS_SUBMITS: the same) */
#define RD_PERF_SUBS 4

typedef struct RdPerfSub {
    float submitMs;  /* after the replay's first submit, on the CPU's clock */
    uint8_t lists;   /* command lists in it (0: an empty batch) */
    uint8_t present; /* it signals the present's semaphore: the replay's present waits on it */
    /* filled for a stalled replay's verdict alone (named 1): the game lists
       whose end stamps it carries (listLo..listHi, -1 none), the timed
       pieces' stamps in it, its stamps recorded and of them those that came
       back unwritten, and whether it carries the present's stamps */
    uint8_t named, presentStamps;
    int8_t listLo, listHi;
    uint16_t pieces, stamps, missing;
} RdPerfSub;

#endif /* RD_PERF_SUB_H */
