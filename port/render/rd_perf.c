/* rd_perf.c: the per-replay performance records (rd.h RdPerfRecord).
 *
 * A record is filled while a replay runs (rd_replay.c adds its CPU phases
 * and counts to g_rdPerf), closed by rd__perf_end with the RHI's counters
 * since the previous record, and then waits in a slot per frame in flight
 * for its GPU timestamps: rhi_wait_frame of the replay RHI_FRAMES_IN_FLIGHT
 * later recycles the slot the record's commands used, and rhi_read_timestamps
 * then reads them without waiting (rd__perf_collect_gpu).  Finished records
 * queue for rd_perf_pop (the window's per-frame CSV and its 10 s line, the
 * replay tool, rd_perf_test).
 *
 * The timestamps a replay writes, in the order it wrote them, and for each
 * the picture effect (rd__perf_post) whose work it ends, travel with the
 * record; the collection walks them in that order, so a list's time is the
 * sum of its stretches and each stretch also counts for its effect. */
#include <string.h>
#include "rd_internal.h"

RdPerfRecord g_rdPerf;

#define RD_PERF_QUEUE 64

static RdPerfRecord s_queue[RD_PERF_QUEUE];

static uint32_t s_head, s_count;

/* the records waiting for their timestamps, by rhi_frame_slot */
static RdPerfRecord s_pending[RHI_FRAMES_IN_FLIGHT];

static bool s_pendingValid[RHI_FRAMES_IN_FLIGHT];

static RhiStats s_last;

static bool s_haveLast;

static double s_interpMs, s_readbackMs, s_t0;

static uint32_t s_replay;

static uint32_t s_pipelineCreates;

/* the timestamps of one replay: their indices in the order written, and for
   each index the effect (RD_PERF_POST_*, -1 none) of the stretch it ends */
typedef struct PerfStamps {
    uint8_t order[RHI_MAX_TIMESTAMPS];
    int8_t post[RHI_MAX_TIMESTAMPS];
    uint32_t count;
    uint32_t written; /* bit i: index i written */
} PerfStamps;

static PerfStamps s_stamps; /* the replay being recorded */

static PerfStamps s_pendingStamps[RHI_FRAMES_IN_FLIGHT];

static int s_post = -1;    /* the effect the work being recorded belongs to */
static uint32_t s_nextTs;  /* the next free index above RD_PERF_TS_COUNT */
static bool s_postPartial; /* an effect change found no free index */

static const char *const s_postNames[RD_PERF_POST_COUNT] = {
    "reduction", "fog", "motion blur", "aura", "glow", "depth of field", "softening", "CRT",
};

const char *rd_perf_post_name(int post)
{
    return post >= 0 && post < RD_PERF_POST_COUNT ? s_postNames[post] : "none";
}

int rd__perf_post_of_kind(uint32_t kind)
{
    switch (kind) {
    case RD_POST_REDUCTION:
        return RD_PERF_POST_REDUCTION;
    case RD_POST_FOG:
        return RD_PERF_POST_FOG;
    case RD_POST_MOTION_BLUR:
        return RD_PERF_POST_MOTION_BLUR;
    case RD_POST_AURA:
        return RD_PERF_POST_AURA;
    case RD_POST_FLARE:
    case RD_POST_BLOOM:
    case RD_POST_EYE_BLUR:
        return RD_PERF_POST_GLOW;
    case RD_POST_DOF:
        return RD_PERF_POST_DOF;
    default:
        return -1;
    }
}

static void push(const RdPerfRecord *r)
{
    if (s_count == RD_PERF_QUEUE) {
        s_head = (s_head + 1) % RD_PERF_QUEUE; /* the oldest is dropped */
        s_count--;
    }
    s_queue[(s_head + s_count) % RD_PERF_QUEUE] = *r;
    s_count++;
}

bool rd_perf_pop(RdPerfRecord *out)
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

void rd__perf_reset(void)
{
    s_head = s_count = 0;
    memset(s_pendingValid, 0, sizeof(s_pendingValid));
    memset(s_pendingStamps, 0, sizeof(s_pendingStamps));
    s_haveLast = false;
    s_interpMs = s_readbackMs = 0.0;
}

void rd__perf_interp_ms(double ms)
{
    s_interpMs += ms;
}

/* the alpha of the present about to replay (rd_present) */
static float s_alpha = -1.0f;

static uint8_t s_first;

void rd__perf_alpha(float alpha, int firstOfTick)
{
    s_alpha = alpha;
    s_first = (uint8_t)(firstOfTick != 0);
}

void rd__perf_readback_ms(double ms)
{
    s_readbackMs += ms;
}

void rd__perf_begin(const RdFrame *f, int keep, bool present)
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
        rhi_get_stats(&s_last);
        s_haveLast = true;
    }
    s_pipelineCreates = g_rd.stats.pipelineCreates;
    memset(&s_stamps, 0, sizeof(s_stamps));
    s_post = -1;
    s_nextTs = RD_PERF_TS_COUNT;
    s_postPartial = false;
    s_t0 = rd__now_ms();
    g_rdPerf.startMs = s_t0;
}

/* timestamps of the slot just recycled: the replay RHI_FRAMES_IN_FLIGHT ago */
void rd__perf_collect_gpu(void)
{
    /* called inside a replay, after rd__wait_frame waited on the slot's
       fence: the record pending in this RHI frame slot (rhi_frame_slot) is
       the one rd__perf_end left there the last time the slot was used,
       RHI_FRAMES_IN_FLIGHT frames ago */
    const uint32_t slot = rhi_frame_slot();
    if (!s_pendingValid[slot]) {
        return;
    }
    RdPerfRecord *r = &s_pending[slot];
    const PerfStamps *ps = &s_pendingStamps[slot];
    s_pendingValid[slot] = false;
    uint64_t ts[RHI_MAX_TIMESTAMPS];
    const uint32_t n = g_rd.hasDevice ? rhi_read_timestamps(ts, RHI_MAX_TIMESTAMPS) : 0;
    if (n > RD_PERF_TS_BEGIN && ts[RD_PERF_TS_BEGIN] != 0) {
        const uint64_t t0 = ts[RD_PERF_TS_BEGIN];
        uint64_t prev = t0, last = t0;
        double stretch = 0.0; /* since the last RD_PERF_TS_* stamp */
        for (uint32_t k = 0; k < ps->count; k++) {
            const uint32_t i = ps->order[k];
            if (i == RD_PERF_TS_BEGIN || i >= n || ts[i] == 0 || ts[i] < prev) {
                continue;
            }
            const double ms = (double)(ts[i] - prev) / 1e6;
            if (ps->post[i] >= 0 && ps->post[i] < RD_PERF_POST_COUNT) {
                r->gpuPostMs[ps->post[i]] += ms;
            }
            stretch += ms;
            if (i == RD_PERF_TS_LISTS) {
                r->gpuUploadMs = stretch;
            } else if (i == RD_PERF_TS_PRESENT) {
                r->gpuPresentMs = stretch;
            } else if (i >= RD_PERF_TS_LIST0 && i < RD_PERF_TS_LIST0 + RD_LIST_COUNT) {
                r->gpuListMs[i - RD_PERF_TS_LIST0] = stretch;
            }
            if (i < RD_PERF_TS_COUNT) {
                stretch = 0.0;
            }
            prev = last = ts[i];
        }
        r->gpuMs = (double)(last - t0) / 1e6;
        r->gpuValid = 1;
    }
    push(r);
}

void rd__perf_end(void)
{
    RdPerfRecord *r = &g_rdPerf;
    r->totalMs = rd__now_ms() - s_t0 + r->interpMs;
    r->pipelineCreates = g_rd.stats.pipelineCreates - s_pipelineCreates;
    if (g_rd.hasDevice) {
        RhiStats s;
        rhi_get_stats(&s);
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
    const uint32_t slot = rhi_frame_slot();
    if (s_pendingValid[slot]) {
        push(&s_pending[slot]); /* its timestamps never came: queued without */
    }
    r->gpuPostPartial = (uint8_t)s_postPartial;
    s_pending[slot] = *r;
    s_pendingStamps[slot] = s_stamps;
    s_pendingValid[slot] = true;
}

/* timestamp index, ending a stretch of effect s_post.  The effects' own
   (index RD_PERF_TS_COUNT and above) leave the barriers held back by the
   backend where they are, so the replay records the barriers it would
   without them */
static void stamp(RhiCommandList cl, uint32_t index)
{
    if (!g_rd.hasDevice || index >= RHI_MAX_TIMESTAMPS || (s_stamps.written & (1u << index))) {
        return;
    }
    rhi_cmd_write_timestamp(cl, index >= RD_PERF_TS_COUNT ? index | RHI_TIMESTAMP_KEEP_BARRIERS
                                                          : index);
    s_stamps.written |= 1u << index;
    s_stamps.order[s_stamps.count++] = (uint8_t)index;
    s_stamps.post[index] = (int8_t)s_post;
}

void rd__perf_stamp(RhiCommandList cl, uint32_t index)
{
    stamp(cl, index);
    s_post = -1;
}

void rd__perf_post(RhiCommandList cl, int post)
{
    if (post == s_post || !g_rd.hasDevice) {
        return;
    }
    if (s_nextTs >= RHI_MAX_TIMESTAMPS) {
        s_postPartial = true;
        return;
    }
    stamp(cl, s_nextTs++);
    s_post = post;
}
