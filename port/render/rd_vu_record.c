/* rd_vu_record.c: how the VU pre-pass writes its record: render targets
 * and copies, the vertex shaders' own stores, or compute dispatches
 * (rd_vu_record.h; issue 179: on a Mali-G52 the record's 48 bytes a pixel
 * and six texture-to-buffer copies a replay were a quarter of the boat
 * scene's GPU time, and v0.11.1's stores never engaged there: the driver
 * offers no vertexPipelineStoresAndAtomics). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_vu_record.h"

RdVuRecord g_rdRec;

static int askedFromEnv(void)
{
    const char *e = getenv("ICO_RD_VU_RECORD");
    if (e == NULL || e[0] == '\0' || strcmp(e, "auto") == 0) {
        return RD_VU_RECORD_AUTO;
    }
    if (strcmp(e, "targets") == 0) {
        return RD_VU_RECORD_TARGETS;
    }
    if (strcmp(e, "stores") == 0) {
        return RD_VU_RECORD_STORES;
    }
    if (strcmp(e, "compute") == 0) {
        return RD_VU_RECORD_COMPUTE;
    }
    rd__log("VU record: ICO_RD_VU_RECORD=%s is none of auto, targets, stores, compute: auto", e);
    return RD_VU_RECORD_AUTO;
}

/* why this device cannot store from its vertex shaders */
static const char *noStoresWhy(const RhiLimits *lim)
{
    if (lim == NULL) {
        return "no device";
    }
    switch (rhi_backend()) {
    case RHI_BACKEND_D3D11:
        return "Direct3D 11: feature level 11_0 has no vertex shader UAVs";
    case RHI_BACKEND_D3D12:
        return "Direct3D 12: not in this release";
    case RHI_BACKEND_OPENGL:
        return "OpenGL: not in this release";
    default:
        return "the device has no vertexPipelineStoresAndAtomics (or "
               "ICO_VK_FAKE_NO_VERTEX_STORES)";
    }
}

/* why it cannot store from compute shaders (the backend's own words,
   RhiLimits.computeStoresOff) */
static const char *noComputeWhy(const RhiLimits *lim)
{
    if (lim == NULL) {
        return "no device";
    }
    return lim->computeStoresOff != NULL && lim->computeStoresOff[0] != '\0' ? lim->computeStoresOff
                                                                             : "not offered";
}

static void freeSet(RhiShader *set)
{
    for (int v = 0; v < RD_VS_COUNT; v++) {
        if (set[v].id) {
            rhi_destroy_shader(set[v]);
        }
        set[v] = (RhiShader){0};
    }
}

static void freeShaders(void)
{
    freeSet(g_rdRec.vsStore);
    freeSet(g_rdRec.vsBatchStore);
    freeSet(g_rdRec.csCompute);
    freeSet(g_rdRec.csBatchCompute);
}

/* the record's reason, cut to its buffer if ever longer (vsnprintf with the
   result read: gcc 14's -Wformat-truncation at -O1 and -O2 cannot size the
   sites' words, and a cut reason is only words; the usual ones fit) */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
static void whySet(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(g_rdRec.why, sizeof g_rdRec.why, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof g_rdRec.why) {
        g_rdRec.why[sizeof g_rdRec.why - 1] = '\0';
    }
}

static const char kTargetsWords[] = "drawn into 3 RGBA32_UINT render targets and copied into the "
                                    "frame slot's record buffer";
static const char kStoresWords[] = "stored by the vertex shaders into the frame slot's record "
                                   "buffer: no render targets, no copies";
static const char kComputeWords[] = "stored by compute dispatches into the frame slot's record "
                                    "buffer: no render targets, no copies";

static const char *modeName(int mode)
{
    return mode == RD_VU_RECORD_STORES    ? "stores"
           : mode == RD_VU_RECORD_COMPUTE ? "compute"
                                          : "targets";
}

/* the builds of mode (stores or compute) for every pre-pass entry and
 * record stage, batch builds too under the pre-pass's batches: vu_X_pre_vs
 * -> vu_X_store_pre_vs and vu_X_batch_store_pre_vs, or vu_X_compute_pre_cs
 * and vu_X_batch_compute_pre_cs.  False with the first missing name in
 * miss (that mode's set freed) */
static bool makeBuilds(int mode, char *miss, size_t missSize)
{
    const bool compute = mode == RD_VU_RECORD_COMPUTE;
    RhiShader *one = compute ? g_rdRec.csCompute : g_rdRec.vsStore;
    RhiShader *batch = compute ? g_rdRec.csBatchCompute : g_rdRec.vsBatchStore;
    const char *kind = compute ? "compute_pre_cs" : "store_pre_vs";
    miss[0] = '\0';
    for (int v = 0; v < RD_VS_COUNT && !miss[0]; v++) {
        if (!rd__vs_is_vu_pre((uint8_t)v) && !rd__vs_is_vu_stage((uint8_t)v)) {
            continue;
        }
        const char *e = rd__vs_name((uint8_t)v);
        const size_t n = strlen(e);
        if (n < 7 || strcmp(e + n - 7, "_pre_vs") != 0) {
            snprintf(miss, missSize, "%s", e);
            break;
        }
        char name[64];
        if (!one[v].id) {
            snprintf(name, sizeof(name), "%.*s_%s", (int)(n - 7), e, kind);
            one[v] = rd__make_shader(name);
            if (!one[v].id) {
                snprintf(miss, missSize, "%s", name);
                break;
            }
        }
        if (g_rd.vuPreBatch && !batch[v].id) {
            snprintf(name, sizeof(name), "%.*s_batch_%s", (int)(n - 7), e, kind);
            batch[v] = rd__make_shader(name);
            if (!batch[v].id) {
                snprintf(miss, missSize, "%s", name);
            }
        }
    }
    if (miss[0]) {
        freeSet(one);
        freeSet(batch);
        return false;
    }
    return true;
}

/* the mode's words: how the record is written and why not the others
   (the reasons the device gave; [dev] vu_record as asked) */
static void wordsFor(int mode, const RhiLimits *lim)
{
    const int asked = g_rdRec.asked;
    if (mode == RD_VU_RECORD_STORES) {
        whySet("%s (%s)", kStoresWords,
               asked == RD_VU_RECORD_STORES ? "[dev] vu_record = stores"
                                            : "[dev] vu_record = targets keeps the render "
                                              "targets, = compute the compute dispatches");
    } else if (mode == RD_VU_RECORD_COMPUTE) {
        if (asked == RD_VU_RECORD_COMPUTE) {
            whySet("%s ([dev] vu_record = compute)", kComputeWords);
        } else {
            whySet("%s (no vertex stores: %s%s; [dev] vu_record = targets keeps the render "
                   "targets)",
                   kComputeWords, noStoresWhy(lim),
                   asked == RD_VU_RECORD_STORES ? "; [dev] vu_record = stores not taken" : "");
        }
    } else if (asked == RD_VU_RECORD_TARGETS) {
        whySet("%s ([dev] vu_record = targets)", kTargetsWords);
    } else {
        whySet("%s (no vertex stores: %s; no compute stores: %s%s)", kTargetsWords,
               noStoresWhy(lim), noComputeWhy(lim),
               asked == RD_VU_RECORD_STORES    ? "; [dev] vu_record = stores not taken"
               : asked == RD_VU_RECORD_COMPUTE ? "; [dev] vu_record = compute not taken"
                                               : "");
    }
}

void rd__vu_record_init(const RhiLimits *lim)
{
    freeShaders();
    if (g_rdRec.layoutVuPre.id) {
        rhi_destroy_bind_group_layout(g_rdRec.layoutVuPre);
    }
    g_rdRec.layoutVuPre = (RhiBindGroupLayout){0};
    g_rdRec.mode = RD_VU_RECORD_TARGETS;
    g_rdRec.failed[0] = '\0';
    g_rdRec.minHeld = false;
    g_rdRec.skipped = g_rdRec.skippedMaxSlots = 0;
    g_rdRec.skippedRun = 0;
    g_rdRec.asked = askedFromEnv();
    g_rdRec.canStores = lim != NULL && lim->vertexStores;
    g_rdRec.canCompute = lim != NULL && lim->computeStores;
    /* auto: stores, else compute, else targets; stores asked where the
       vertex shaders cannot store: auto's next rung; compute asked where
       the device cannot dispatch: the targets */
    int want = RD_VU_RECORD_TARGETS;
    if (g_rdRec.asked == RD_VU_RECORD_COMPUTE) {
        want = g_rdRec.canCompute ? RD_VU_RECORD_COMPUTE : RD_VU_RECORD_TARGETS;
    } else if (g_rdRec.asked != RD_VU_RECORD_TARGETS) {
        want = g_rdRec.canStores    ? RD_VU_RECORD_STORES
               : g_rdRec.canCompute ? RD_VU_RECORD_COMPUTE
                                    : RD_VU_RECORD_TARGETS;
    }
    if (want != RD_VU_RECORD_TARGETS) {
        /* group 1 of the store and compute pipelines: layoutVu's slots but
           t4, and u6; both kinds' stages, so a fallback keeps the layout */
        enum {
            VS = 1u << RHI_STAGE_VERTEX,
            FS = 1u << RHI_STAGE_FRAGMENT,
            CS = 1u << RHI_STAGE_COMPUTE
        };

        static const RhiBindSlot s[6] = {{0, RHI_BIND_STORAGE_BUFFER, VS | CS},
                                         {1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS | CS},
                                         {2, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS | CS},
                                         {3, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | CS},
                                         {5, RHI_BIND_STORAGE_BUFFER, VS | CS},
                                         {6, RHI_BIND_STORAGE_BUFFER_RW, VS | CS}};
        g_rdRec.layoutVuPre =
            rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){s, 6, "rd vu pre-pass stores"});
    }
    g_rdRec.mode = want;
    if (want != RD_VU_RECORD_TARGETS && !g_rdRec.layoutVuPre.id) {
        g_rdRec.mode = RD_VU_RECORD_TARGETS;
        whySet("%s (the bind group layout of the %s could not be made)", kTargetsWords,
               modeName(want));
    } else {
        wordsFor(want, lim);
    }
    if (rd__vu_record_buffered() && lim->validationLayer) {
        rd__log("VU record: each record %s in a %s of its own (the validation layer is loaded: "
                "its synchronisation checks take the whole bound buffer as each %s's write and "
                "cannot see the slots are disjoint; a run without it records them together with "
                "no barrier)",
                want == RD_VU_RECORD_COMPUTE ? "dispatch" : "store draw",
                want == RD_VU_RECORD_COMPUTE ? "bracket" : "pass",
                want == RD_VU_RECORD_COMPUTE ? "dispatch" : "draw");
    }
    g_rdRec.prepassMin = 0;
    const char *m = getenv("ICO_RD_VU_PREPASS_MIN");
    if (m != NULL && m[0] != '\0') {
        char *end = NULL;
        const unsigned long v = strtoul(m, &end, 10);
        if (end != m && *end == '\0') {
            g_rdRec.prepassMin = v > UINT32_MAX ? UINT32_MAX : (uint32_t)v;
        } else {
            rd__log("VU pre-pass: ICO_RD_VU_PREPASS_MIN=%s is not a number: 0 (never skipped)", m);
        }
    }
    if (g_rdRec.prepassMin) {
        rd__log("VU pre-pass: a frame under %u vertex and triangle slots draws every VU command "
                "the old way ([dev] vu_prepass_min = %u)",
                g_rdRec.prepassMin, g_rdRec.prepassMin);
    }
}

bool rd__vu_record_buffered(void)
{
    return g_rdRec.mode == RD_VU_RECORD_STORES || g_rdRec.mode == RD_VU_RECORD_COMPUTE;
}

bool rd__vu_record_compute(void)
{
    return g_rdRec.mode == RD_VU_RECORD_COMPUTE;
}

/* the rung given up, onto the summary's list */
static void failedAdd(int mode)
{
    const size_t n = strlen(g_rdRec.failed);
    /* the result read (gcc 14's -Wformat-truncation); three rungs fit */
    const int w = snprintf(g_rdRec.failed + n, sizeof g_rdRec.failed - n, "%s%s", n ? ", " : "",
                           modeName(mode));
    if (w < 0 || (size_t)w >= sizeof g_rdRec.failed - n) {
        g_rdRec.failed[sizeof g_rdRec.failed - 1] = '\0';
    }
}

bool rd__vu_record_fall_back(const char *why)
{
    const int from = g_rdRec.mode;
    if (from == RD_VU_RECORD_TARGETS) {
        return false;
    }
    failedAdd(from);
    const char *w = why != NULL ? why : "?";
    int to = RD_VU_RECORD_TARGETS;
    char miss[64] = "";
    if (from == RD_VU_RECORD_STORES && g_rdRec.canCompute && g_rdRec.layoutVuPre.id) {
        to = RD_VU_RECORD_COMPUTE;
        if (!makeBuilds(RD_VU_RECORD_COMPUTE, miss, sizeof miss)) {
            /* the compute rung given up too, said with its build */
            failedAdd(RD_VU_RECORD_COMPUTE);
            to = RD_VU_RECORD_TARGETS;
        }
    }
    g_rdRec.mode = to;
    if (to == RD_VU_RECORD_COMPUTE) {
        whySet("%s (the stores off: %s)", kComputeWords, w);
    } else if (miss[0]) {
        whySet("%s (the %s off: %s; the compute off: %s could not be made)", kTargetsWords,
               modeName(from), w, miss);
    } else if (from == RD_VU_RECORD_STORES) {
        whySet("%s (the stores off: %s; no compute stores: %s)", kTargetsWords, w,
               g_rdRec.canCompute ? "the stores' layout is missing"
                                  : (g_rd.hasDevice ? noComputeWhy(rhi_limits()) : "no device"));
    } else {
        whySet("%s (the %s off: %s)", kTargetsWords, modeName(from), w);
    }
    rd__log("VU record: %s", g_rdRec.why);
    return true;
}

void rd__vu_record_to_targets(const char *why)
{
    while (rd__vu_record_fall_back(why)) {}
}

void rd__vu_record_shaders(void)
{
    freeShaders();
    if (!rd__vu_record_buffered()) {
        return;
    }
    if (!g_rd.vuPrepass) {
        g_rdRec.mode = RD_VU_RECORD_TARGETS;
        whySet("%s (the pre-pass is off)", kTargetsWords);
        return;
    }
    uint32_t planned = 0;
    for (int v = 0; v < RD_VS_COUNT; v++) {
        planned += rd__vs_is_vu_pre((uint8_t)v) || rd__vs_is_vu_stage((uint8_t)v) ? 1u : 0u;
    }
    rhi_plan_shaders(g_rd.vuPreBatch ? 2u * planned : planned); /* the start-up set's progress */
    char miss[64];
    if (!makeBuilds(g_rdRec.mode, miss, sizeof miss)) {
        char why[96];
        snprintf(why, sizeof(why), "%s could not be made", miss);
        (void)rd__vu_record_fall_back(why);
    }
}

void rd__vu_record_shutdown(void)
{
    freeShaders();
    if (g_rdRec.layoutVuPre.id) {
        rhi_destroy_bind_group_layout(g_rdRec.layoutVuPre);
    }
    g_rdRec.layoutVuPre = (RhiBindGroupLayout){0};
    g_rdRec.mode = RD_VU_RECORD_TARGETS;
}

const char *rd__vu_record_name(void)
{
    return modeName(g_rdRec.mode);
}

RhiShader rd__vu_record_vs(uint8_t vs, bool batched)
{
    if (vs >= RD_VS_COUNT) {
        return (RhiShader){0};
    }
    return batched ? g_rdRec.vsBatchStore[vs] : g_rdRec.vsStore[vs];
}

RhiShader rd__vu_record_cs(uint8_t vs, bool batched)
{
    if (vs >= RD_VS_COUNT) {
        return (RhiShader){0};
    }
    return batched ? g_rdRec.csBatchCompute[vs] : g_rdRec.csCompute[vs];
}

void rd__replay_dispatch(RhiCommandList cl, uint32_t points)
{
    if (g_rdWin.first && !rd__win_take()) {
        return;
    }
    if (points == 0) {
        return;
    }
    rhi_cmd_dispatch(cl, (points + 63u) / 64u, 1, 1);
}

bool rd__vu_record_skip(uint64_t slots)
{
    if (g_rdRec.prepassMin == 0 || g_rdRec.minHeld || slots >= g_rdRec.prepassMin) {
        return false;
    }
    /* the replay (the render thread) counts, rd_vu_prepass_skips (the
       main thread's 10 s line) takes and resets: atomics, as rd_velocity.c
       (skippedRun only the replay touches) */
    __atomic_fetch_add(&g_rdRec.skipped, 1u, __ATOMIC_RELAXED);
    g_rdRec.skippedRun++;
    uint32_t most = __atomic_load_n(&g_rdRec.skippedMaxSlots, __ATOMIC_RELAXED);
    while ((uint32_t)slots > most &&
           !__atomic_compare_exchange_n(&g_rdRec.skippedMaxSlots, &most, (uint32_t)slots, true,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
    return true;
}

const char *rd_vu_record_mode(void)
{
    return rd__vu_record_name();
}

bool rd_vu_record_buffered(void)
{
    return rd__vu_record_buffered();
}

uint32_t rd_vu_prepass_skips(uint32_t *minSlots, uint32_t *maxSlots)
{
    /* taken and reset in one step each (the replay may count between
       them: a frame then lands in the next line's count) */
    const uint32_t n = __atomic_exchange_n(&g_rdRec.skipped, 0u, __ATOMIC_RELAXED);
    const uint32_t most = __atomic_exchange_n(&g_rdRec.skippedMaxSlots, 0u, __ATOMIC_RELAXED);
    if (minSlots) {
        *minSlots = g_rdRec.prepassMin;
    }
    if (maxSlots) {
        *maxSlots = most;
    }
    return n;
}
