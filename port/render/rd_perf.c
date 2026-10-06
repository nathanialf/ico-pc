/* rd_perf.c: the per-replay performance records of package P1 (rd.h
 * RdPerfRecord, docs/port/RENDER_API.md "Performance").
 *
 * A record is filled while a replay runs (rd_replay.c adds its CPU phases
 * and counts to g_rdPerf), closed by rd__PerfEnd with the RHI's counters
 * since the previous record, and then waits in a slot per frame in flight
 * for its GPU timestamps: rhi_WaitFrame of the replay RHI_FRAMES_IN_FLIGHT
 * later recycles the slot the record's commands used, and rhi_ReadTimestamps
 * then reads them without waiting (rd__PerfCollectGpu).  Finished records
 * queue for rd_PerfPop (the window's per-frame CSV and its 10 s line, the
 * replay tool, rd_perf_test). */
#include <string.h>
#include "rd_internal.h"

RdPerfRecord g_rdPerf;

#define RD_PERF_QUEUE 64

static RdPerfRecord s_queue[RD_PERF_QUEUE];

static uint32_t s_head, s_count;

/* the records waiting for their timestamps, by rhi_FrameSlot */
static RdPerfRecord s_pending[RHI_FRAMES_IN_FLIGHT];

static bool s_pendingValid[RHI_FRAMES_IN_FLIGHT];

static RhiStats s_last;

static bool s_haveLast;

static double s_interpMs, s_readbackMs, s_t0;

static uint32_t s_replay;

static uint32_t s_pipelineCreates;

static void push(const RdPerfRecord *r)
{
    if (s_count == RD_PERF_QUEUE) {
        s_head = (s_head + 1) % RD_PERF_QUEUE; /* the oldest is dropped */
        s_count--;
    }
    s_queue[(s_head + s_count) % RD_PERF_QUEUE] = *r;
    s_count++;
}

bool rd_PerfPop(RdPerfRecord *out)
{
    if (s_count == 0) {
        return false;
    }
    if (out) {
        *out = s_queue[s_head];
    }
    s_head = (s_head + 1) % RD_PERF_QUEUE;
    s_count--;
    return true;
}

void rd__PerfReset(void)
{
    s_head = s_count = 0;
    memset(s_pendingValid, 0, sizeof(s_pendingValid));
    s_haveLast = false;
    s_interpMs = s_readbackMs = 0.0;
}

void rd__PerfInterpMs(double ms)
{
    s_interpMs += ms;
}

/* S2: the alpha of the present about to replay (rd_Present) */
static float s_alpha = -1.0f;

static uint8_t s_first;

void rd__PerfAlpha(float alpha, int firstOfTick)
{
    s_alpha = alpha;
    s_first = (uint8_t)(firstOfTick != 0);
}

void rd__PerfReadbackMs(double ms)
{
    s_readbackMs += ms;
}

void rd__PerfBegin(const RdFrame *f, int keep, bool present)
{
    memset(&g_rdPerf, 0, sizeof(g_rdPerf));
    g_rdPerf.replay = ++s_replay;
    g_rdPerf.frame = f ? f->number : 0;
    g_rdPerf.keep = (uint8_t)(keep != 0);
    g_rdPerf.presented = (uint8_t)present;
    g_rdPerf.interpolated = (uint8_t)(s_interpMs > 0.0);
    g_rdPerf.interpMs = s_interpMs;
    g_rdPerf.readbackMs = s_readbackMs; /* since the previous replay */
    g_rdPerf.alpha = s_alpha;
    g_rdPerf.firstOfTick = s_first;
    s_alpha = -1.0f;
    s_first = 0;
    s_interpMs = s_readbackMs = 0.0;
    if (!s_haveLast && g_rd.hasDevice) {
        rhi_GetStats(&s_last);
        s_haveLast = true;
    }
    s_pipelineCreates = g_rd.stats.pipelineCreates;
    s_t0 = rd__NowMs();
    g_rdPerf.startMs = s_t0;
}

/* timestamps of the slot just recycled: the replay RHI_FRAMES_IN_FLIGHT ago */
void rd__PerfCollectGpu(void)
{
    /* called inside a replay, after rd__WaitFrame waited on the slot's
       fence: the record pending in this RHI frame slot (rhi_FrameSlot) is
       the one rd__PerfEnd left there the last time the slot was used,
       RHI_FRAMES_IN_FLIGHT frames ago */
    const uint32_t slot = rhi_FrameSlot();
    if (!s_pendingValid[slot]) {
        return;
    }
    RdPerfRecord *r = &s_pending[slot];
    s_pendingValid[slot] = false;
    uint64_t ts[RHI_MAX_TIMESTAMPS];
    const uint32_t n = g_rd.hasDevice ? rhi_ReadTimestamps(ts, RHI_MAX_TIMESTAMPS) : 0;
    if (n > RD_PERF_TS_BEGIN && ts[RD_PERF_TS_BEGIN] != 0) {
        const uint64_t t0 = ts[RD_PERF_TS_BEGIN];
        uint64_t prev = t0, last = t0;
        for (uint32_t i = RD_PERF_TS_LISTS; i < n && i < RD_PERF_TS_COUNT; i++) {
            if (ts[i] == 0 || ts[i] < prev) {
                continue;
            }
            const double ms = (double)(ts[i] - prev) / 1e6;
            if (i == RD_PERF_TS_LISTS) {
                r->gpuUploadMs = ms;
            } else if (i == RD_PERF_TS_PRESENT) {
                r->gpuPresentMs = ms;
            } else {
                r->gpuListMs[i - RD_PERF_TS_LIST0] = ms;
            }
            prev = last = ts[i];
        }
        r->gpuMs = (double)(last - t0) / 1e6;
        r->gpuValid = 1;
    }
    push(r);
}

void rd__PerfEnd(void)
{
    RdPerfRecord *r = &g_rdPerf;
    r->totalMs = rd__NowMs() - s_t0 + r->interpMs;
    r->pipelineCreates = g_rd.stats.pipelineCreates - s_pipelineCreates;
    if (g_rd.hasDevice) {
        RhiStats s;
        rhi_GetStats(&s);
#define D(field) (uint32_t)(s.field - s_last.field)
        r->buffersCreated = D(buffersCreated);
        r->buffersDestroyed = D(buffersDestroyed);
        r->texturesCreated = D(texturesCreated);
        r->texturesDestroyed = D(texturesDestroyed);
        r->memoryAllocs = D(memoryAllocs);
        r->memoryFrees = D(memoryFrees);
        r->bindGroups = D(bindGroups);
        r->pipelineBinds = D(pipelineBinds);
        r->bindGroupBinds = D(bindGroupBinds);
        r->draws = D(draws);
        r->renderPasses = D(renderPasses);
        r->barriers = D(barriers);
        r->copies = D(copies);
        r->fenceWaits = D(fenceWaits);
        r->waitIdles = D(waitIdles);
        r->readbacks = D(readbacks);
#undef D
        r->fenceWaitMs = (double)(s.fenceWaitNs - s_last.fenceWaitNs) / 1e6;
        s_last = s;
    }
    const uint32_t slot = rhi_FrameSlot();
    if (s_pendingValid[slot]) {
        push(&s_pending[slot]); /* its timestamps never came: queued without */
    }
    s_pending[slot] = *r;
    s_pendingValid[slot] = true;
}

void rd__PerfStamp(RhiCommandList cl, uint32_t index)
{
    if (g_rd.hasDevice && index < RHI_MAX_TIMESTAMPS) {
        rhi_CmdWriteTimestamp(cl, index);
    }
}
