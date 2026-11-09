/* rd_vu_record.h: how the VU pre-pass writes its record (rd_vu_record.c;
 * docs/port/PIPELINES.md "The vertex and triangle pre-passes", "The
 * record's three writes").
 *
 * The record (rd_replay_vu.c "the record's buffers": three planes of
 * 16-byte words a slot in the frame slot's record buffer) is written one
 * of three ways:
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
 *   compute  the same entries run as compute dispatches, one invocation a
 *            point (ICO_VU_REC_COMPUTE: vu_X_compute_pre_cs,
 *            vu_X_batch_compute_pre_cs; vu_common.hlsli VU_PRE_CS), the
 *            store builds' stores, outside render passes, bracketed by
 *            rhi_cmd_buffer_compute_begin / _end: no render targets, no
 *            copies, no passes (issue 179: the Mali-G52 MC2 offers no
 *            vertexPipelineStoresAndAtomics, and the targets were 14.6 of
 *            its 64 ms GPU a picture)
 * All three give the buffer the same bytes where any reader looks (slot
 * t's planes at t, rows * 1024 + t, 2 * rows * 1024 + t: the layout of the
 * copies), so every picture is the same.
 *
 * [dev] vu_record (ICO_RD_VU_RECORD) = auto (stores wherever the device
 * has vertex stores, RhiLimits.vertexStores; else compute wherever it has
 * compute stores, RhiLimits.computeStores; else targets: on every device,
 * so the lavapipe gates run every path), targets, stores (where the device
 * cannot store from its vertex shaders: auto's next rung, said in the
 * words) or compute (where it cannot: the targets, said).  The start-up
 * self-test (rd_vuprepass_selftest.c) judges the mode it starts with and
 * falls back one rung at a time (rd__vu_record_fall_back: stores, compute,
 * targets, then the pre-pass off), every verdict logged.
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

enum { RD_VU_RECORD_AUTO = 0, RD_VU_RECORD_TARGETS, RD_VU_RECORD_STORES, RD_VU_RECORD_COMPUTE };

typedef struct RdVuRecord {
    /* how the record is written this run: RD_VU_RECORD_TARGETS, _STORES or
     * _COMPUTE (never _AUTO; rd__vu_record_buffered, rd__vu_record_compute) */
    int mode;
    /* [dev] vu_record as asked (RD_VU_RECORD_*) */
    int asked;
    /* the device's offers at rd__vu_record_init (RhiLimits.vertexStores,
     * computeStores), for the fallback's next rung */
    bool canStores, canCompute;
    /* the mode's words for the start line and the summary: how the record
     * is written, and why the other ways are not taken */
    char why[320];
    /* the rungs given up this run, in order ("stores, compute"), for the
     * self-test's summary; "" when none */
    char failed[48];
    /* the store builds by RdVsId (RD_VS_VU_PRE_* and the four stages):
     * vu_X_store_pre_vs, and vu_X_batch_store_pre_vs under the pre-pass's
     * batches (g_rd.vuPreBatch); the compute builds likewise:
     * vu_X_compute_pre_cs, vu_X_batch_compute_pre_cs; made by
     * rd__vu_record_shaders for the start mode and by the fallback for
     * compute, kept until the shutdown (a pipeline already made of them may
     * stay in the cache) */
    RhiShader vsStore[RD_VS_COUNT], vsBatchStore[RD_VS_COUNT];
    RhiShader csCompute[RD_VS_COUNT], csBatchCompute[RD_VS_COUNT];
    /* group 1 of the store and compute pipelines: t0 the stream, b1
     * DrawCB, b2 VuCB, b3 VuBoneCB, t5 the ring, u6 the record written (no
     * t4: the record is read through u6 there), visible to the vertex and
     * compute stages (b1, b2 to the fragment's too, as layoutVu's), so one
     * layout serves both kinds across a fallback */
    RhiBindGroupLayout layoutVuPre;
    /* [dev] vu_prepass_min, and the self-test's hold on it (the self-test
     * judges the record, so it is never skipped there) */
    uint32_t prepassMin;
    bool minHeld;
    /* the frames skipped under prepassMin since rd_vu_prepass_skips last
     * took them, and the most slots one of them had (atomics: the replay
     * counts on the render thread, the 10 s line takes them on the main
     * thread) */
    uint32_t skipped, skippedMaxSlots;
    uint64_t skippedRun; /* for the run */
} RdVuRecord;

extern RdVuRecord g_rdRec;

/* rd__vu_prepass_init's: the mode for this device (lim may be NULL: no
 * device, targets), [dev] vu_prepass_min, group 1's store layout */
void rd__vu_record_init(const RhiLimits *lim);
/* the start mode's builds, before any pipeline (preBatchInit's place); a
 * build missing falls back a rung (logged).  Nothing under targets */
void rd__vu_record_shaders(void);
/* the builds and the layout (rd__vu_prepass_shutdown) */
void rd__vu_record_shutdown(void);
/* the record written without render targets this run (stores or
 * compute): the record's buffers made writable, no textures, no copies,
 * the pre/stage keys' colorFmt UNKNOWN (rd__vu_pre_key) */
bool rd__vu_record_buffered(void);
/* written by compute dispatches this run (the keys' fs RD_FS_VU_PRE_CS,
 * the record's passes no render passes: rd_replay_vu.c vuRecPassBegin) */
bool rd__vu_record_compute(void);
/* one rung down (the self-test's verdict failed, a build missing): stores
 * to compute where the device offers it and its builds are made, else to
 * the targets; compute to the targets; false at the targets (nothing
 * lower: the caller turns the pre-pass off).  why, for the log, the words
 * and the summary's failed rungs.  The pipelines already made stay in the
 * cache: each mode's keys are other keys (rd__vu_pre_key: colorFmt, fs),
 * made on first use */
bool rd__vu_record_fall_back(const char *why);
/* every rung down to the targets (rd__vu_record_fall_back until it stops) */
void rd__vu_record_to_targets(const char *why);
/* "stores", "compute" or "targets" */
const char *rd__vu_record_name(void);
/* the vertex shader a pre-pass or stage key (rd__vs_is_vu_pre,
 * rd__vs_is_vu_stage) draws through under the stores, {0} where its store
 * build was not made */
RhiShader rd__vu_record_vs(uint8_t vs, bool batched);
/* the compute shader such a key dispatches under compute, {0} where its
 * compute build was not made */
RhiShader rd__vu_record_cs(uint8_t vs, bool batched);
/* the record's compute dispatch of points points under the bound compute
 * pipeline: (points + 63) / 64 groups of 64 (vu_common.hlsli VU_PRE_CS),
 * left out outside the loss bisect's window as a draw is (rd__win_take;
 * no occlusion query); rd_replay_vu.c vuStageDraw, preBatchDraw */
void rd__replay_dispatch(RhiCommandList cl, uint32_t points);
/* vuPrepassRun's step 2: true when a frame of slots record slots is
 * skipped under [dev] vu_prepass_min (counted) */
bool rd__vu_record_skip(uint64_t slots);

#endif /* PORT_RENDER_RD_VU_RECORD_H */
