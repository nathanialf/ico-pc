/* rd_vu_record.c: how the VU pre-pass writes its record, render targets
 * and copies or the vertex shaders' own stores (rd_vu_record.h; issue 179:
 * on a Mali-G52 the record's 48 bytes a pixel and six texture-to-buffer
 * copies a replay were a quarter of the boat scene's GPU time). */
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
    rd__log("VU record: ICO_RD_VU_RECORD=%s is none of auto, targets, stores: auto", e);
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

static void freeShaders(void)
{
    for (int v = 0; v < RD_VS_COUNT; v++) {
        if (g_rdRec.vsStore[v].id) {
            rhi_destroy_shader(g_rdRec.vsStore[v]);
        }
        if (g_rdRec.vsBatchStore[v].id) {
            rhi_destroy_shader(g_rdRec.vsBatchStore[v]);
        }
        g_rdRec.vsStore[v] = (RhiShader){0};
        g_rdRec.vsBatchStore[v] = (RhiShader){0};
    }
}

static const char kTargetsWords[] = "drawn into 3 RGBA32_UINT render targets and copied into the "
                                    "frame slot's record buffer";
static const char kStoresWords[] = "stored by the vertex shaders into the frame slot's record "
                                   "buffer: no render targets, no copies";

void rd__vu_record_init(const RhiLimits *lim)
{
    freeShaders();
    if (g_rdRec.layoutVuPre.id) {
        rhi_destroy_bind_group_layout(g_rdRec.layoutVuPre);
    }
    g_rdRec.layoutVuPre = (RhiBindGroupLayout){0};
    g_rdRec.stores = false;
    g_rdRec.minHeld = false;
    g_rdRec.skipped = g_rdRec.skippedMaxSlots = 0;
    g_rdRec.skippedRun = 0;
    g_rdRec.asked = askedFromEnv();
    const bool can = lim != NULL && lim->vertexStores;
    if (g_rdRec.asked == RD_VU_RECORD_TARGETS) {
        snprintf(g_rdRec.why, sizeof(g_rdRec.why), "%s ([dev] vu_record = targets)", kTargetsWords);
    } else if (!can) {
        snprintf(g_rdRec.why, sizeof(g_rdRec.why), "%s (no vertex stores: %s%s)", kTargetsWords,
                 noStoresWhy(lim),
                 g_rdRec.asked == RD_VU_RECORD_STORES ? "; [dev] vu_record = stores not taken"
                                                      : "");
    } else {
        /* group 1 of the store pipelines: layoutVu's slots but t4, and u6 */
        enum { VS = 1u << RHI_STAGE_VERTEX, FS = 1u << RHI_STAGE_FRAGMENT };

        static const RhiBindSlot s[6] = {{0, RHI_BIND_STORAGE_BUFFER, VS},
                                         {1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS},
                                         {2, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS},
                                         {3, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS},
                                         {5, RHI_BIND_STORAGE_BUFFER, VS},
                                         {6, RHI_BIND_STORAGE_BUFFER_RW, VS}};
        g_rdRec.layoutVuPre =
            rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){s, 6, "rd vu pre-pass stores"});
        g_rdRec.stores = g_rdRec.layoutVuPre.id != 0;
        if (g_rdRec.stores && lim->validationLayer) {
            rd__log("VU record: each record store draw in a pass of its own (the validation "
                    "layer is loaded: its synchronisation checks take the whole bound buffer as "
                    "each draw's write and cannot see the draws' slots are disjoint; a run "
                    "without it draws them in one pass with no barrier)");
        }
        if (g_rdRec.stores) {
            snprintf(g_rdRec.why, sizeof(g_rdRec.why), "%s (%s)", kStoresWords,
                     g_rdRec.asked == RD_VU_RECORD_STORES ? "[dev] vu_record = stores"
                                                          : "[dev] vu_record = targets keeps "
                                                            "the render targets");
        } else {
            snprintf(g_rdRec.why, sizeof(g_rdRec.why),
                     "%s (the stores' bind group layout could not be made)", kTargetsWords);
        }
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

void rd__vu_record_to_targets(const char *why)
{
    if (!g_rdRec.stores) {
        return;
    }
    g_rdRec.stores = false;
    snprintf(g_rdRec.why, sizeof(g_rdRec.why), "%s (the stores off: %s)", kTargetsWords,
             why != NULL ? why : "?");
    rd__log("VU record: %s", g_rdRec.why);
}

void rd__vu_record_shaders(void)
{
    freeShaders();
    if (!g_rdRec.stores) {
        return;
    }
    if (!g_rd.vuPrepass) {
        g_rdRec.stores = false;
        snprintf(g_rdRec.why, sizeof(g_rdRec.why), "%s (the pre-pass is off)", kTargetsWords);
        return;
    }
    uint32_t planned = 0;
    for (int v = 0; v < RD_VS_COUNT; v++) {
        planned += rd__vs_is_vu_pre((uint8_t)v) || rd__vs_is_vu_stage((uint8_t)v) ? 1u : 0u;
    }
    rhi_plan_shaders(g_rd.vuPreBatch ? 2u * planned : planned); /* the start-up set's progress */
    char miss[64] = "";
    for (int v = 0; v < RD_VS_COUNT && !miss[0]; v++) {
        if (!rd__vs_is_vu_pre((uint8_t)v) && !rd__vs_is_vu_stage((uint8_t)v)) {
            continue;
        }
        /* vu_X_pre_vs -> vu_X_store_pre_vs, vu_X_batch_store_pre_vs */
        const char *e = rd__vs_name((uint8_t)v);
        const size_t n = strlen(e);
        if (n < 7 || strcmp(e + n - 7, "_pre_vs") != 0) {
            snprintf(miss, sizeof(miss), "%s", e);
            break;
        }
        char name[64];
        snprintf(name, sizeof(name), "%.*s_store_pre_vs", (int)(n - 7), e);
        g_rdRec.vsStore[v] = rd__make_shader(name);
        if (!g_rdRec.vsStore[v].id) {
            snprintf(miss, sizeof(miss), "%s", name);
            break;
        }
        if (g_rd.vuPreBatch) {
            snprintf(name, sizeof(name), "%.*s_batch_store_pre_vs", (int)(n - 7), e);
            g_rdRec.vsBatchStore[v] = rd__make_shader(name);
            if (!g_rdRec.vsBatchStore[v].id) {
                snprintf(miss, sizeof(miss), "%s", name);
            }
        }
    }
    if (miss[0]) {
        freeShaders();
        char why[96];
        snprintf(why, sizeof(why), "%s could not be made", miss);
        rd__vu_record_to_targets(why);
    }
}

void rd__vu_record_shutdown(void)
{
    freeShaders();
    if (g_rdRec.layoutVuPre.id) {
        rhi_destroy_bind_group_layout(g_rdRec.layoutVuPre);
    }
    g_rdRec.layoutVuPre = (RhiBindGroupLayout){0};
    g_rdRec.stores = false;
}

const char *rd__vu_record_name(void)
{
    return g_rdRec.stores ? "stores" : "targets";
}

RhiShader rd__vu_record_vs(uint8_t vs, bool batched)
{
    if (!g_rdRec.stores || vs >= RD_VS_COUNT) {
        return (RhiShader){0};
    }
    return batched ? g_rdRec.vsBatchStore[vs] : g_rdRec.vsStore[vs];
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
