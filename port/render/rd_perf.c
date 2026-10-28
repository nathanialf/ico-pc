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
 * sum of its stretches and each stretch also counts for its effect.
 *
 * The lists' timestamps also mark the walk: RD_PERF_TS_LISTS its start,
 * the last list's its end, so rd__replay_list says which list a draw is
 * recorded in, each list's CPU time (RdPerfRecord.walkListMs), and a walk
 * over WALK_SLOW_MS gets the line of what took its time (walkBreakdown).
 *
 * The pieces (rd__perf_pass, "the passes" below) time a replay's GPU work
 * pass by pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_passring.h"

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

/* the walk (RD_PERF_TS_LISTS to the last list's stamp): inside it, and at
   its start the pipelines' time and count and the RHI's counters
   (walkBreakdown) */
static bool s_inLists;
/* the walk's start (RD_PERF_TS_PREPASS) and end (rd__perf_walk_end): the
   pipelines made at first use (their time and count) and the RHI's stats */
static double s_walkPipeMs, s_walkEndPipeMs;
static uint32_t s_walkPipes, s_walkEndPipes;
static RhiStats s_walkStats, s_walkEndStats;
/* g_rd.above1xDraws at the replay's start (RdPerfRecord.a1xDraws) */
static uint32_t s_a1xDraws;

/* the list and effect timestamps of one replay (indices below
   RD_PERF_TS_PASS0): their indices in the order written, and for each index
   the effect (RD_PERF_POST_*, -1 none) of the stretch it ends */
typedef struct PerfStamps {
    uint8_t order[RD_PERF_TS_PASS0];
    int8_t post[RD_PERF_TS_PASS0];
    uint32_t count;
    uint32_t written; /* bit i: index i written */
} PerfStamps;

_Static_assert(RD_PERF_TS_COUNT < RD_PERF_TS_PASS0 && RD_PERF_TS_PASS0 <= 32u,
               "the list and effect stamps fit PerfStamps.written");
_Static_assert(RD_PERF_TS_PASS0 + 2u <= RHI_MAX_TIMESTAMPS, "room for a piece and its end");

/* the walk's list timing: the CPU clock at the lists' start or the last
   list's end */
static double s_listMs0;

static PerfStamps s_stamps; /* the replay being recorded */

static PerfStamps s_pendingStamps[RHI_FRAMES_IN_FLIGHT];

static int s_post = -1;    /* the effect the work being recorded belongs to */
static uint32_t s_nextTs;  /* the next free index above RD_PERF_TS_COUNT */
static bool s_postPartial; /* an effect change found no free index */

static const char *const s_postNames[RD_PERF_POST_COUNT] = {
    "reduction",      "fog",       "motion blur", "aura",         "glow",
    "depth of field", "softening", "CRT",         "MSAA resolve", "crack fill",
};

const char *rd_perf_post_name(int post)
{
    return post >= 0 && post < RD_PERF_POST_COUNT ? s_postNames[post] : "none";
}

static const char *const s_passKindNames[RD_PERF_PASS_KIND_COUNT] = {
    "uploads",        "VU pre-pass", "render pass",  "copy",          "snapshot", "DATE snapshot",
    "fog depth read", "exact blend", "MSAA resolve", "stencil clear", "present",
};

const char *rd_perf_pass_kind_name(int kind)
{
    return kind >= 0 && kind < RD_PERF_PASS_KIND_COUNT ? s_passKindNames[kind] : "none";
}

int rd_perf_pass_label(const RdPerfPass *p, char *buf, size_t n)
{
    if (p->kind == RD_PERF_PASS_RENDER) {
        if (p->post >= 0 && p->post < RD_PERF_POST_COUNT) {
            return snprintf(buf, n, "%s", rd_perf_post_name(p->post));
        }
        return snprintf(buf, n, "list %d", (int)p->list);
    }
    return snprintf(buf, n, "%s", rd_perf_pass_kind_name(p->kind));
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

/* Direct3D 11's submit writes of the records popped since the last
 * rd_perf_backend_text, and their count */
static uint64_t s_backendBytes;
static uint32_t s_backendReplays, s_reshownReplays;

bool rd_perf_pop(RdPerfRecord *out)
{
    if (s_count == 0) {
        return false;
    }
    s_backendBytes += s_queue[s_head].backendWriteBytes;
    s_backendReplays++;
    s_reshownReplays += s_queue[s_head].reshown;
    if (out) {
        *out = s_queue[s_head];
    }
    s_head = (s_head + 1) % RD_PERF_QUEUE;
    s_count--;
    return true;
}

bool rd__perf_gpu_ms_of(uint32_t replay, double *ms)
{
    for (uint32_t k = 0; k < s_count; k++) {
        const RdPerfRecord *r = &s_queue[(s_head + k) % RD_PERF_QUEUE];
        if (r->replay == replay) {
            if (!r->gpuValid) {
                return false;
            }
            *ms = r->gpuMs;
            return true;
        }
    }
    return false;
}

const char *rd_perf_backend_text(void)
{
    static char s_text[160];
    size_t at = 0;
    s_text[0] = '\0';
    if (s_backendBytes && s_backendReplays) {
        at = (size_t)snprintf(s_text, sizeof(s_text),
                              "; Direct3D 11 submits wrote %.0f KB to the GPU",
                              (double)s_backendBytes / s_backendReplays / 1024.0);
    }
    if (s_reshownReplays) {
        /* the replays above are counted with these: a re-shown present is a replay of no passes */
        snprintf(s_text + at, sizeof(s_text) - at,
                 "; %u presents re-shown without a replay ([dev] still_represent)",
                 s_reshownReplays);
    }
    s_backendBytes = 0;
    s_backendReplays = 0;
    s_reshownReplays = 0;
    return s_text;
}

/* ------------------------------------------------------------ the passes
 * The replay names each piece of GPU work as it begins it, outside any
 * render pass (rd__perf_pass: the uploads, the VU pre-pass, every render
 * pass, copy, snapshot, DATE snapshot, the fog's depth read, the exact
 * blend, the MSAA resolves, the present; rd.h RD_PERF_PASS_*), and every
 * replay counts them by kind (RdPerfRecord.passKinds, passEffects).  A
 * timed replay also writes a timestamp at each piece's start
 * (RD_PERF_TS_PASS0 + its index, keeping the held-back barriers held, so
 * the barriers recorded are those of an untimed replay) and one after the
 * last (rd__perf_pass_end), and keeps each piece's draws and pipeline binds
 * (the RHI's counters between two starts) and the CPU time the replay took
 * to record it; the piece's GPU time is its stamp to the next one's
 * (rd__perf_collect_gpu, RHI_FRAMES_IN_FLIGHT replays later), and its
 * record waits in a queue of RD_PERF_PASS_QUEUE for rd_perf_passes_of.
 * Timed: every replay with [dev] perf_passes (ICO_RD_PERF_PASSES=1), else
 * the first replay at least RD_PERF_PASS_EVERY_MS after the last timed one
 * (at most 40 in a 10 s block: the window's summary).  Not on iOS (as the
 * effects' stamps, rd__perf_post).  A replay that begins more pieces than
 * RD_PERF_PASS_MAX times the later ones with the last
 * (RdPerfRecord.passPartial). */
#define RD_PERF_PASS_QUEUE 8u

typedef struct PassTable {
    uint32_t replay;
    uint32_t count;
    bool closed; /* rd__perf_pass_end wrote the stamp after the last piece */
    RdPerfPass pass[RD_PERF_PASS_MAX];
} PassTable;

static bool s_passesMode;            /* every replay timed */
static bool s_noListStamps;          /* [dev] perf_list_stamps = false (stamp) */
static bool s_passOn;                /* the replay being recorded is timed */
static double s_passLastMs = -1.0e9; /* the last timed replay's start */
static PassTable s_passRec;          /* the replay being recorded */
static PassTable s_passPending[RHI_FRAMES_IN_FLIGHT];
static PassTable s_passQueue[RD_PERF_PASS_QUEUE];
static uint32_t s_passHead, s_passCount;
static RhiStats s_passStats; /* the RHI's counters at the open piece's start */
static double s_passCpu0;    /* the CPU clock at the open piece's start */

void rd__perf_passes_init(void)
{
    const char *env = getenv("ICO_RD_PERF_PASSES");
    s_passesMode = env && env[0] == '1';
    const char *lists = getenv("ICO_RD_PERF_LIST_STAMPS");
    s_noListStamps = lists && lists[0] == '0';
    if (s_noListStamps) {
        rd__log("GPU list and effect timestamps: off ([dev] perf_list_stamps = false, "
                "ICO_RD_PERF_LIST_STAMPS=0): no stamp inside a render pass; the GPU total of a "
                "present and the passes' times stay, the lists' and effects' read 0");
    }
#ifdef ICO_IOS
    rd__log("GPU pass timing: off on iOS");
    return;
#endif
    if (!rhi_timestamps_supported()) {
        rd__log("GPU pass timing: off (no GPU timestamps)");
    } else if (s_passesMode) {
        rd__log("GPU pass timing: every replay, each pass timed apart ([dev] perf_passes = true, "
                "ICO_RD_PERF_PASSES=1; the window writes ico-pc-passes.csv)");
    } else {
        rd__log("GPU pass timing: one replay every %.0f ms, each pass timed apart, for the 10 s "
                "summary; [dev] perf_passes = true times every replay",
                RD_PERF_PASS_EVERY_MS);
    }
}

void rd__set_perf_passes(bool on)
{
    s_passesMode = on;
}

void rd__set_perf_list_stamps(bool on)
{
    s_noListStamps = !on;
}

bool rd__perf_list_stamps_on(void)
{
    return !s_noListStamps;
}

bool rd_perf_passes_mode(void)
{
    return s_passesMode;
}

static uint16_t clamp16(uint32_t v)
{
    return v > 65535u ? (uint16_t)65535u : (uint16_t)v;
}

/* the open piece's counters and CPU time end here */
static void passClose(const RhiStats *st, double nowMs)
{
    if (s_passRec.count == 0) {
        return;
    }
    RdPerfPass *p = &s_passRec.pass[s_passRec.count - 1];
    p->draws = (uint32_t)(st->draws - s_passStats.draws);
    p->pipelineBinds = (uint32_t)(st->pipelineBinds - s_passStats.pipelineBinds);
    p->entryBinds = (uint32_t)(st->entryBinds - s_passStats.entryBinds);
    p->entrySkipped = (uint32_t)(st->entryBindsSkipped - s_passStats.entryBindsSkipped);
    p->cpuUs = (float)((nowMs - s_passCpu0) * 1000.0);
}

static uint8_t passTarget(uint32_t id)
{
    if (id == 0) {
        return RD_PERF_TARGET_NONE;
    }
    if (id == RD_PERF_ID_RECORD) {
        return RD_PERF_TARGET_RECORD;
    }
    if (id == RD_PERF_ID_WINDOW) {
        return RD_PERF_TARGET_WINDOW;
    }
    return rd__target_code(id);
}

void rd__perf_pass(RhiCommandList cl, int kind, uint32_t target, uint32_t w, uint32_t h, int post)
{
    if (kind < 0 || kind >= RD_PERF_PASS_KIND_COUNT) {
        return;
    }
    if (g_rdPerf.passKinds[kind] < UINT16_MAX) {
        g_rdPerf.passKinds[kind]++;
    }
    if (kind == RD_PERF_PASS_RENDER && post >= 0 && post < RD_PERF_POST_COUNT &&
        g_rdPerf.passEffects[post] < UINT16_MAX) {
        g_rdPerf.passEffects[post]++;
    }
    if (!s_passOn) {
        return;
    }
    if (s_passRec.count >= RD_PERF_PASS_MAX) {
        g_rdPerf.passPartial = 1; /* timed with the piece before */
        return;
    }
    const double now = rd__now_ms();
    RhiStats st;
    rhi_get_stats(&st);
    passClose(&st, now);
    rhi_cmd_write_timestamp(cl, (RD_PERF_TS_PASS0 + s_passRec.count) | RHI_TIMESTAMP_KEEP_BARRIERS |
                                    RHI_TIMESTAMP_ISSUE_TIME);
    RdPerfPass *p = &s_passRec.pass[s_passRec.count++];
    memset(p, 0, sizeof(*p));
    p->kind = (uint8_t)kind;
    p->target = passTarget(target);
    p->list = (int8_t)rd__replay_list();
    p->post = (int8_t)(post >= 0 && post < RD_PERF_POST_COUNT ? post : -1);
    p->w = clamp16(w);
    p->h = clamp16(h);
    p->gpuUs = -1.0f;
    s_passStats = st;
    s_passCpu0 = now;
}

void rd__perf_pass_end(RhiCommandList cl)
{
    if (!s_passOn || s_passRec.count == 0 || s_passRec.closed) {
        return;
    }
    RhiStats st;
    rhi_get_stats(&st);
    passClose(&st, rd__now_ms());
    rhi_cmd_write_timestamp(cl, (RD_PERF_TS_PASS0 + s_passRec.count) | RHI_TIMESTAMP_KEEP_BARRIERS |
                                    RHI_TIMESTAMP_ISSUE_TIME);
    s_passRec.closed = true;
}

/* a timed replay's pieces, their GPU times from its slot's stamps (and
   their busy times from its busy clock, nb 0 without one), into the queue
   (the oldest dropped when nobody takes them) */
static void passFinish(PassTable *t, const uint64_t *ts, uint32_t n, const uint64_t *busy,
                       uint32_t nb)
{
    for (uint32_t k = 0; k < t->count; k++) {
        const uint32_t a = RD_PERF_TS_PASS0 + k, b = a + 1u;
        RdPerfPass *p = &t->pass[k];
        p->gpuUs = -1.0f;
        p->gpuBusyUs = 0.0f;
        p->busyValid = 0;
        if (b < n && ts[a] != 0 && ts[b] != 0 && ts[b] >= ts[a] &&
            (k + 1u < t->count || t->closed)) {
            p->gpuUs = (float)((double)(ts[b] - ts[a]) / 1e3);
            if (b < nb && busy[a] != 0 && busy[b] >= busy[a]) {
                p->gpuBusyUs = (float)((double)(busy[b] - busy[a]) / 1e3);
                p->busyValid = 1;
            }
        }
    }
    if (s_passCount == RD_PERF_PASS_QUEUE) {
        s_passHead = (s_passHead + 1u) % RD_PERF_PASS_QUEUE;
        s_passCount--;
    }
    PassTable *q = &s_passQueue[(s_passHead + s_passCount) % RD_PERF_PASS_QUEUE];
    q->replay = t->replay;
    q->count = t->count;
    q->closed = t->closed;
    memcpy(q->pass, t->pass, (size_t)t->count * sizeof(t->pass[0]));
    s_passCount++;
}

uint32_t rd_perf_passes_of(uint32_t replay, const RdPerfPass **out)
{
    *out = NULL;
    while (s_passCount) {
        const PassTable *t = &s_passQueue[s_passHead];
        if (t->replay > replay) {
            return 0; /* a later replay's: kept for its record */
        }
        s_passHead = (s_passHead + 1u) % RD_PERF_PASS_QUEUE;
        s_passCount--;
        if (t->replay == replay) {
            *out = t->pass; /* the slot is written again only by a later replay */
            return t->count;
        }
    }
    return 0;
}

void rd__perf_reset(void)
{
    s_head = s_count = 0;
    memset(s_pendingValid, 0, sizeof(s_pendingValid));
    memset(s_pendingStamps, 0, sizeof(s_pendingStamps));
    s_haveLast = false;
    s_interpMs = s_readbackMs = 0.0;
    s_passHead = s_passCount = 0;
    s_passOn = false;
    s_passLastMs = -1.0e9;
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

/* the next replay is a re-shown present (rd_reshow.c) */
static uint8_t s_reshownNext;

void rd__perf_reshown(void)
{
    s_reshownNext = 1;
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
    g_rdPerf.reshown = s_reshownNext;
    s_reshownNext = 0;
    s_alpha = -1.0f;
    s_first = 0;
    s_interpMs = s_readbackMs = 0.0;
    if (!s_haveLast && g_rd.hasDevice) {
        rhi_get_stats(&s_last);
        s_haveLast = true;
    }
    s_pipelineCreates = g_rd.stats.pipelineCreates;
    s_a1xDraws = g_rd.above1xDraws;
    memset(&s_stamps, 0, sizeof(s_stamps));
    s_post = -1;
    s_nextTs = RD_PERF_TS_COUNT;
    s_postPartial = false;
    s_t0 = rd__now_ms();
    g_rdPerf.startMs = s_t0;
    s_listMs0 = s_t0;
    s_passRec.count = 0;
    s_passRec.closed = false;
    s_passRec.replay = g_rdPerf.replay;
    s_passOn = false;
#ifndef ICO_IOS
    if (g_rd.hasDevice && rhi_timestamps_supported() &&
        (s_passesMode || s_t0 - s_passLastMs >= RD_PERF_PASS_EVERY_MS)) {
        s_passOn = true;
        s_passLastMs = s_t0;
    }
#endif
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
    /* the busy clock (OpenGL): the same stamps without the GPU's waits for
       the CPU */
    uint64_t busy[RHI_MAX_TIMESTAMPS];
    const uint32_t nb = n ? rhi_read_timestamps_busy(busy, RHI_MAX_TIMESTAMPS) : 0;
    if (n > RD_PERF_TS_BEGIN && ts[RD_PERF_TS_BEGIN] != 0) {
        const uint64_t t0 = ts[RD_PERF_TS_BEGIN];
        uint64_t prev = t0, last = t0;
        uint32_t lastIndex = RD_PERF_TS_BEGIN;
        double stretch = 0.0; /* since the last RD_PERF_TS_* stamp */
        bool prepass = false; /* RD_PERF_TS_PREPASS came: LISTS ends the pre-pass */
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
            if (i == RD_PERF_TS_PREPASS) {
                r->gpuUploadMs = stretch;
                prepass = true;
            } else if (i == RD_PERF_TS_LISTS) {
                if (prepass) {
                    r->gpuPrepassMs = stretch;
                } else {
                    r->gpuUploadMs = stretch;
                }
            } else if (i == RD_PERF_TS_PRESENT) {
                r->gpuPresentMs = stretch;
            } else if (i >= RD_PERF_TS_LIST0 && i < RD_PERF_TS_LIST0 + RD_LIST_COUNT) {
                r->gpuListMs[i - RD_PERF_TS_LIST0] = stretch;
            }
            if (i < RD_PERF_TS_COUNT) {
                stretch = 0.0;
            }
            prev = last = ts[i];
            lastIndex = i;
        }
        r->gpuMs = (double)(last - t0) / 1e6;
        r->gpuValid = 1;
        if (lastIndex < nb && busy[RD_PERF_TS_BEGIN] != 0 &&
            busy[lastIndex] >= busy[RD_PERF_TS_BEGIN]) {
            r->gpuBusyMs = (double)(busy[lastIndex] - busy[RD_PERF_TS_BEGIN]) / 1e6;
            r->gpuBusyValid = 1;
        }
    }
    if (r->passTimed) {
        passFinish(&s_passPending[slot], ts, n, busy, nb);
    }
    push(r);
}

double rd_last_replay_ms(void)
{
    return g_rdPerf.totalMs;
}

int rd__replay_list(void)
{
    return s_inLists ? rd__passring_list_now() : -1;
}

/* A walk over WALK_SLOW_MS: what in it took the time, one line, the first
 * 8 then every 100th (the host's rate limiter's rule; the renderer does
 * not link it): the pipelines made at first use (rd__get_pipeline), the
 * textures and buffers made (the RHI's createNs: memory and objects), each
 * from the walk's start (RD_PERF_TS_PREPASS: the vertex pre-pass, then the
 * lists) to its end (rd__perf_walk_end, the present recorded), the window
 * walkMs covers, and the rest.  Texture decodes run in the game's step and
 * the replay's texture and mesh uploads in its upload phase, before the
 * walk. */
#define WALK_SLOW_MS 500.0

static void walkBreakdown(const RdPerfRecord *r)
{
    static uint32_t s_slow;
    const RhiStats *s = &s_walkEndStats;
    const double pipeMs = s_walkEndPipeMs - s_walkPipeMs;
    const double createMs = (double)(s->createNs - s_walkStats.createNs) / 1e6;
    const double rest = r->walkMs - pipeMs - createMs;

    if (++s_slow > 8u && s_slow % 100u != 0u) {
        return;
    }
    rd__log("slow walk %.1f ms (replay %u, frame %u): pipelines made %.1f ms (%u), textures and "
            "buffers made %.1f ms (%llu and %llu), the rest %.1f ms (%u so far%s)",
            r->walkMs, r->replay, r->frame, pipeMs, s_walkEndPipes - s_walkPipes, createMs,
            (unsigned long long)(s->texturesCreated - s_walkStats.texturesCreated),
            (unsigned long long)(s->buffersCreated - s_walkStats.buffersCreated),
            rest > 0.0 ? rest : 0.0, s_slow,
            s_slow == 8u ? "; from here every 100th is written" : "");
}

void rd__perf_end(void)
{
    RdPerfRecord *r = &g_rdPerf;
    r->totalMs = rd__now_ms() - s_t0 + r->interpMs;
    r->pipelineCreates = g_rd.stats.pipelineCreates - s_pipelineCreates;
    r->a1xDraws = g_rd.above1xDraws - s_a1xDraws;
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
        r->dynamicSets = D(dynamicSets);
        r->blendEqOffSets = D(blendEqOffSets);
        r->dualLeftovers = D(dualLeftovers);
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
        r->backendWriteBytes = s.uploadWriteBytes - s_last.uploadWriteBytes;
        if (r->walkMs > WALK_SLOW_MS) {
            walkBreakdown(r);
        }
        s_last = s;
    }
    s_inLists = false;
    const uint32_t slot = rhi_frame_slot();
    if (s_pendingValid[slot]) {
        s_pending[slot].passTimed = 0; /* its pieces are dropped with its times */
        push(&s_pending[slot]);        /* its timestamps never came: queued without */
    }
    r->gpuPostPartial = (uint8_t)s_postPartial;
    r->passTimed = (uint8_t)(s_passOn && s_passRec.count > 0);
    r->passCount = (uint16_t)(r->passTimed ? s_passRec.count : 0u);
    if (r->passTimed) {
        PassTable *t = &s_passPending[slot];
        t->replay = s_passRec.replay;
        t->count = s_passRec.count;
        t->closed = s_passRec.closed;
        memcpy(t->pass, s_passRec.pass, (size_t)s_passRec.count * sizeof(s_passRec.pass[0]));
    }
    s_passOn = false;
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
    if (!g_rd.hasDevice || index >= RD_PERF_TS_PASS0 || (s_stamps.written & (1u << index))) {
        return;
    }
    /* [dev] perf_list_stamps = false: only the stamps written outside every
       render pass (the replay's start, the pre-pass's, the lists' start, the
       present's end), for the A/B of what a stamp inside an open pass costs
       a tile-based GPU (one that ends the pass there and begins it again) */
    if (s_noListStamps && index != RD_PERF_TS_BEGIN && index != RD_PERF_TS_LISTS &&
        index != RD_PERF_TS_PREPASS && index != RD_PERF_TS_PRESENT) {
        return;
    }
    /* a replay timed pass by pass: each stamp's issue time too, for the busy
       clock (rhi.h RHI_TIMESTAMP_ISSUE_TIME: it may hold the CPU on
       OpenGL, so not in every replay) */
    const uint32_t issue = s_passOn ? RHI_TIMESTAMP_ISSUE_TIME : 0u;
    rhi_cmd_write_timestamp(
        cl, (index >= RD_PERF_TS_COUNT ? index | RHI_TIMESTAMP_KEEP_BARRIERS : index) | issue);
    s_stamps.written |= 1u << index;
    s_stamps.order[s_stamps.count++] = (uint8_t)index;
    s_stamps.post[index] = (int8_t)s_post;
}

void rd__perf_stamp(RhiCommandList cl, uint32_t index)
{
    stamp(cl, index);
    s_post = -1;
    if (index >= RD_PERF_TS_LIST0 && index < RD_PERF_TS_LIST0 + RD_LIST_COUNT) {
        /* the list's CPU time: from the lists' start or the list before */
        const double now = rd__now_ms();
        g_rdPerf.walkListMs[index - RD_PERF_TS_LIST0] = now - s_listMs0;
        s_listMs0 = now;
    }
    if (index == RD_PERF_TS_PREPASS) {
        /* the walk starts: the vertex pre-pass, then the lists */
        s_walkPipeMs = s_walkEndPipeMs = rd__pipeline_create_ms();
        s_walkPipes = s_walkEndPipes = g_rd.stats.pipelineCreates;
        if (g_rd.hasDevice) {
            rhi_get_stats(&s_walkStats);
            s_walkEndStats = s_walkStats;
        }
    } else if (index == RD_PERF_TS_LISTS) {
        s_listMs0 = rd__now_ms(); /* list 0's CPU time starts */
        s_inLists = true;
    } else if (index == RD_PERF_TS_LIST0 + RD_LIST_COUNT - 1) {
        s_inLists = false; /* the last list done: the present's draws follow */
    }
}

void rd__perf_walk_end(void)
{
    s_walkEndPipeMs = rd__pipeline_create_ms();
    s_walkEndPipes = g_rd.stats.pipelineCreates;
    if (g_rd.hasDevice) {
        rhi_get_stats(&s_walkEndStats);
    }
}

void rd__perf_post(RhiCommandList cl, int post)
{
#ifdef ICO_IOS
    return; /* Automatic resolution needs frame timings, not per-effect timestamps. */
#endif
    if (post == s_post || !g_rd.hasDevice) {
        return;
    }
    if (s_nextTs >= RD_PERF_TS_PASS0) {
        s_postPartial = true;
        return;
    }
    stamp(cl, s_nextTs++);
    s_post = post;
}

/* ------------------------------------------------- the pieces as text
 * The window's ico-pc-passes.csv (rd_perf_pass_csv_*) and its 10 s
 * summary (rd_perf_pass_agg_*, rd_perf_pass_kinds_text); pure: no device,
 * no renderer state (rd_perf_test's text checks). */
int rd_perf_pass_csv_header(char *buf, size_t n)
{
    return snprintf(buf, n,
                    "replay,frame,pass,kind,list,effect,target,width,height,draws,pipeline_binds,"
                    "gpu_us,cpu_us\n");
}

int rd_perf_pass_csv_line(uint32_t replay, uint32_t frame, uint32_t index, const RdPerfPass *p,
                          char *buf, size_t n)
{
    return snprintf(buf, n, "%u,%u,%u,%s,%d,%s,%s,%u,%u,%u,%u,%.1f,%.1f\n", replay, frame, index,
                    rd_perf_pass_kind_name(p->kind), (int)p->list, rd_perf_post_name(p->post),
                    rd_perf_target_name(p->target), (unsigned)p->w, (unsigned)p->h, p->draws,
                    p->pipelineBinds, (double)p->gpuUs, (double)p->cpuUs);
}

/* a piece under this GPU time counts as small (the tilers' fixed cost of a
   render pass is about 0.05 to 0.15 ms) */
#define RD_PERF_PASS_SMALL_US 50.0f

void rd_perf_pass_agg_reset(RdPerfPassAgg *a)
{
    memset(a, 0, sizeof(*a));
}

void rd_perf_pass_agg_add(RdPerfPassAgg *a, const RdPerfPass *p, uint32_t n)
{
    a->replays++;
    for (uint32_t k = 0; k < n; k++) {
        const RdPerfPass *q = &p[k];
        const double ms = q->gpuUs >= 0.0f ? (double)q->gpuUs / 1e3 : 0.0;
        a->passes++;
        a->gpuMs += ms;
        a->entryBinds += q->entryBinds;
        a->entrySkipped += q->entrySkipped;
        if (q->gpuUs >= 0.0f && q->gpuUs < RD_PERF_PASS_SMALL_US) {
            a->small++;
            a->smallMs += ms;
        }
        RdPerfPassAggEntry *e = NULL;
        for (uint32_t i = 0; i < a->count; i++) {
            RdPerfPassAggEntry *c = &a->e[i];
            if (c->kind == q->kind && c->target == q->target && c->list == q->list &&
                c->post == q->post && c->w == q->w && c->h == q->h) {
                e = c;
                break;
            }
        }
        if (!e) {
            if (a->count == RD_PERF_PASS_AGG_MAX) {
                a->dropped++;
                continue;
            }
            e = &a->e[a->count++];
            memset(e, 0, sizeof(*e));
            e->kind = q->kind;
            e->target = q->target;
            e->list = q->list;
            e->post = q->post;
            e->w = q->w;
            e->h = q->h;
        }
        e->passes++;
        e->draws += q->draws;
        e->gpuMs += ms;
        e->cpuMs += (double)q->cpuUs / 1e3;
        if (q->busyValid) {
            e->busyMs += (double)q->gpuBusyUs / 1e3;
            e->busyN++;
            a->busyMs += (double)q->gpuBusyUs / 1e3;
            a->busySpanMs += ms;
        }
    }
}

int rd_perf_pass_agg_text(const RdPerfPassAgg *a, unsigned top, char *buf, size_t n)
{
    if (n == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (a->replays == 0) {
        return snprintf(buf, n, "no replay timed");
    }
    const double r = (double)a->replays;
    bool used[RD_PERF_PASS_AGG_MAX];
    memset(used, 0, sizeof(used));
    size_t at = 0;
    unsigned shown = 0;
    for (; shown < top && shown < a->count; shown++) {
        int best = -1;
        for (uint32_t i = 0; i < a->count; i++) {
            if (!used[i] && (best < 0 || a->e[i].gpuMs > a->e[best].gpuMs)) {
                best = (int)i;
            }
        }
        used[best] = true;
        const RdPerfPassAggEntry *e = &a->e[best];
        const RdPerfPass lp = {.kind = e->kind, .list = e->list, .post = e->post};
        char label[32], passes[32] = "";
        rd_perf_pass_label(&lp, label, sizeof(label));
        const double pr = (double)e->passes / r;
        if (pr < 0.95 || pr > 1.05) {
            snprintf(passes, sizeof(passes), " %.1f passes", pr);
        }
        char busy[32] = "";
        if (e->busyN) {
            snprintf(busy, sizeof(busy), " (busy %.2f)", e->busyMs / r);
        }
        const int w =
            snprintf(buf + at, n - at, "%s%s %s %ux%u%s %.0f draws %.2f ms%s", shown ? ", " : "",
                     label, rd_perf_target_name(e->target), (unsigned)e->w, (unsigned)e->h, passes,
                     (double)e->draws / r, e->gpuMs / r, busy);
        if (w < 0 || (size_t)w >= n - at) {
            at = n - 1;
            break;
        }
        at += (size_t)w;
    }
    if (at + 1 < n) {
        /* OpenGL: the timed replays' busy time beside the span of the same
           pieces (the span also counts the GPU's waits for the CPU) */
        char busy[160] = "";
        int b = 0;
        if (a->entryBinds) {
            /* OpenGL: the groups' entries whose binding point already held
               them (gl_cmd.c gl_bind_groups skips their binds) */
            b = snprintf(busy, sizeof(busy),
                         "; group entries %.0f a timed replay, %.0f already bound",
                         (double)a->entryBinds / r, (double)a->entrySkipped / r);
        }
        if (a->busySpanMs > 0.0 && b >= 0 && (size_t)b < sizeof(busy)) {
            snprintf(busy + b, sizeof(busy) - (size_t)b,
                     "; GPU busy %.2f ms, span %.2f ms a timed replay", a->busyMs / r,
                     a->busySpanMs / r);
        }
        snprintf(buf + at, n - at,
                 " (%u of %.1f passes a replay, %u timed replays; passes under 0.05 ms: %.1f a "
                 "replay, %.2f ms%s)",
                 shown, (double)a->passes / r, a->replays, (double)a->small / r, a->smallMs / r,
                 busy);
    }
    return (int)strlen(buf);
}

int rd_perf_pass_kinds_text(const double kinds[RD_PERF_PASS_KIND_COUNT],
                            const double effects[RD_PERF_POST_COUNT], double replays, char *buf,
                            size_t n)
{
    const double r = replays > 0.0 ? replays : 1.0;
    double all = 0.0, fx = 0.0;
    for (int k = 0; k < RD_PERF_PASS_KIND_COUNT; k++) {
        all += kinds[k];
    }
    for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
        fx += effects[p];
    }
    if (n == 0) {
        return 0;
    }
    size_t at = 0;
    int w = snprintf(buf, n, "%.1f: render passes %.1f (lists %.1f", all / r,
                     kinds[RD_PERF_PASS_RENDER] / r, (kinds[RD_PERF_PASS_RENDER] - fx) / r);
#define ADD(...)                                                                                   \
    do {                                                                                           \
        if (w < 0 || (size_t)w >= n - at) {                                                        \
            return (int)strlen(buf);                                                               \
        }                                                                                          \
        at += (size_t)w;                                                                           \
        w = snprintf(buf + at, n - at, __VA_ARGS__);                                               \
    } while (0)
    for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
        if (effects[p] > 0.0) {
            ADD(", %s %.1f", rd_perf_post_name(p), effects[p] / r);
        }
    }
    ADD(")");
    for (int k = 0; k < RD_PERF_PASS_KIND_COUNT; k++) {
        if (k != RD_PERF_PASS_RENDER && kinds[k] > 0.0) {
            ADD(", %s %.1f", rd_perf_pass_kind_name(k), kinds[k] / r);
        }
    }
#undef ADD
    return (int)strlen(buf);
}
