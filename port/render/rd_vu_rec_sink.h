/* rd_vu_rec_sink.h: the throwaway colour target of the VU record's store
 * passes (rd_vu_record.h "stores"; rd_replay_vu.c recStorePassOpen).
 *
 * The store passes draw points without rasterization (RhiPipelineDesc
 * .noRaster): the vertex shaders' stores into the record buffer are their
 * whole work.  Until v0.11.3 they were render passes without attachments,
 * the one shape of the replay that the reports of a crash inside Intel's
 * vkCmdBeginRendering share (issue 123's Intel HD 530 on driver 101.2125,
 * Vulkan 1.3.215, and other ports' reports on the same drivers:
 * port/rhi/rhi_pass_row.h).  Every store pass now begins on this target,
 * on every GPU and both render pass paths: ICO_VU_REC_W x 1 texels of the
 * cheapest colour format a device must render to (R8_UNORM, a required
 * format: vk_device.c vkr_rate_device), loaded DONT_CARE, stored
 * DONT_CARE, never sampled, copied or read back.  The store pipelines name
 * it (rd_pipeline.c: one colour target of RD_VU_REC_SINK_FMT, its write
 * mask 0), so their keys, counts and the start-up set are unchanged; no
 * fragment runs, so nothing is written to it and every picture is the
 * same byte for byte. */
#ifndef PORT_RENDER_RD_VU_REC_SINK_H
#define PORT_RENDER_RD_VU_REC_SINK_H

#include <stdint.h>

#include "rhi.h"

#define RD_VU_REC_SINK_FMT RHI_FMT_R8_UNORM

/* The target in RHI_STATE_RENDER_TARGET on cl, made at the first call (one
 * line); {0} when it could not be made (one line: the store passes then
 * begin without an attachment, as before v0.11.3) */
RhiTexture rd__vu_rec_sink(RhiCommandList cl);
/* the store passes begun on the target, and without it (it could not be
 * made), since the start (the replay tool's --stats and the tests) */
uint32_t rd__vu_rec_sink_passes(uint32_t *without);
/* the target destroyed (rd__vu_prepass_shutdown); the counts kept */
void rd__vu_rec_sink_free(void);

#endif /* PORT_RENDER_RD_VU_REC_SINK_H */
