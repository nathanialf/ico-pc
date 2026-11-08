/* rd_vu_record.h: how the VU pre-pass writes its record (rd_vu_record.c;
 * docs/port/PIPELINES.md "The vertex and triangle pre-passes", "The
 * record's two writes").
 *
 * The record (rd_replay_vu.c "the record's buffers": three planes of
 * 16-byte words a slot in the frame slot's record buffer) is written one
 * of two ways:
 *   targets  the pre-pass's points rasterised into three RGBA32_UINT
 *            render targets through vu_pre_ps, each pass's rows copied into
 *            the buffer after it (vu_X_pre_vs, vu_X_batch_pre_vs; 48 bytes
 *            a pixel, six copies a replay)
 *   stores   the same points' vertex shaders store the words into the
 *            buffer themselves (ICO_VU_REC_STORES: vu_X_store_pre_vs,
 *            vu_X_batch_store_pre_vs; vu_common.hlsli vu_rec_store), drawn
 *            by pipelines without rasterization or a fragment shader
 *            (RhiPipelineDesc.noRaster) in a pass without attachments,
 *            bracketed by rhi_cmd_buffer_store_begin / _end: no render
 *            targets, no copies
 * Both give the buffer the same bytes where any reader looks (the store
 * builds write slot t's planes at t, rows * 1024 + t, 2 * rows * 1024 + t:
 * the layout of the copies), so every picture is the same.
 *
 * [dev] vu_record (ICO_RD_VU_RECORD) = auto (stores wherever the device
 * has vertex stores, RhiLimits.vertexStores, and the backend the bracket:
 * Vulkan in this release, on every device), targets or stores (stores
 * where the device cannot store keeps the targets and says why).  The
 * start-up self-test (rd_vuprepass_selftest.c) judges the mode it starts
 * with: a stores failure is logged and the test run again with the
 * targets (rd__vu_record_to_targets), whose verdict then decides the
 * pre-pass.
 *
 * [dev] vu_prepass_min (ICO_RD_VU_PREPASS_MIN) = N: a frame whose pre-pass
 * would record fewer than N vertex and triangle slots draws every VU
 * command the old way (rd__vu_record_skip; 0, the default: never). */
#ifndef PORT_RENDER_RD_VU_RECORD_H
#define PORT_RENDER_RD_VU_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_internal.h"

enum { RD_VU_RECORD_AUTO = 0, RD_VU_RECORD_TARGETS, RD_VU_RECORD_STORES };

typedef struct RdVuRecord {
    /* the record is stored by the vertex shaders this run (else targets) */
    bool stores;
    /* [dev] vu_record as asked (RD_VU_RECORD_*) */
    int asked;
    /* the mode's words for the start line and the summary: how the record
     * is written, and why the other way is not taken */
    char why[224];
    /* the store builds by RdVsId (RD_VS_VU_PRE_* and the four stages):
     * vu_X_store_pre_vs, and vu_X_batch_store_pre_vs under the pre-pass's
     * batches (g_rd.vuPreBatch); made by rd__vu_record_shaders */
    RhiShader vsStore[RD_VS_COUNT], vsBatchStore[RD_VS_COUNT];
    /* group 1 of the store pipelines: t0 the stream, b1 DrawCB, b2 VuCB,
     * b3 VuBoneCB, t5 the ring, u6 the record written (no t4: the record
     * is read through u6 there) */
    RhiBindGroupLayout layoutVuPre;
    /* [dev] vu_prepass_min, and the self-test's hold on it (the self-test
     * judges the record, so it is never skipped there) */
    uint32_t prepassMin;
    bool minHeld;
    /* the frames skipped under prepassMin since rd_vu_prepass_skips last
     * took them, and the most slots one of them had */
    uint32_t skipped, skippedMaxSlots;
    uint64_t skippedRun; /* for the run */
} RdVuRecord;

extern RdVuRecord g_rdRec;

/* rd__vu_prepass_init's: the mode for this device (lim may be NULL: no
 * device, targets), [dev] vu_prepass_min, group 1's store layout */
void rd__vu_record_init(const RhiLimits *lim);
/* the store builds, before any pipeline (preBatchInit's place); a build
 * missing turns the run to the targets (logged).  Nothing under targets */
void rd__vu_record_shaders(void);
/* the store builds and the layout (rd__vu_prepass_shutdown) */
void rd__vu_record_shutdown(void);
/* the targets from here (the self-test's stores verdict failed, a store
 * build missing): why, for the log and the summary.  The store pipelines
 * already made stay in the cache; the targets' keys are other keys
 * (rd__vu_pre_key: colorFmt), made on first use */
void rd__vu_record_to_targets(const char *why);
/* "stores" or "targets" */
const char *rd__vu_record_name(void);
/* the vertex shader a pre-pass or stage key (rd__vs_is_vu_pre,
 * rd__vs_is_vu_stage) draws through under the stores, {0} under the
 * targets or when it is missing */
RhiShader rd__vu_record_vs(uint8_t vs, bool batched);
/* vuPrepassRun's step 2: true when a frame of slots record slots is
 * skipped under [dev] vu_prepass_min (counted) */
bool rd__vu_record_skip(uint64_t slots);

#endif /* PORT_RENDER_RD_VU_RECORD_H */
