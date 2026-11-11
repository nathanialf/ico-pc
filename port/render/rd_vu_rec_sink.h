/* rd_vu_rec_sink.h: the throwaway colour targets of the VU record's store
 * passes (rd_vu_record.h "stores"; rd_replay_vu.c recStorePassOpen).
 *
 * The store passes draw points without rasterization (RhiPipelineDesc
 * .noRaster): the vertex shaders' stores into the record buffer are their
 * whole work.  Until v0.11.3 they were render passes without attachments,
 * the one shape of the replay that the reports of a crash inside Intel's
 * vkCmdBeginRendering share (issue 123's Intel HD 530 on driver 101.2125,
 * Vulkan 1.3.215, and other ports' reports on the same drivers:
 * port/rhi/rhi_pass_row.h).  Where RhiLimits.storePassTarget says so (the
 * Intel driver row, or the subpass read on: rd__vu_rec_sink_wanted) every
 * store pass begins on such a target, on both render pass paths; everywhere
 * else (v0.11.4: the phones of issues 179 and 211 lost the device in the
 * first 3D scene on v0.11.3, cause unknown, so they run what v0.11.2 ran)
 * the passes begin without an attachment and the store pipelines name no
 * colour target, as before v0.11.3.  The target: ICO_VU_REC_W x 1 texels of the
 * cheapest colour format a device must render to (R8_UNORM, a required
 * format: vk_device.c vkr_rate_device), loaded DONT_CARE, stored
 * DONT_CARE, never sampled, copied or read back.  The store pipelines name
 * it (rd_pipeline.c: one colour target of RD_VU_REC_SINK_FMT, its write
 * mask 0), so their keys, counts and the start-up set are unchanged; no
 * fragment runs, so nothing is written to it and every picture is the
 * same byte for byte.
 *
 * Two targets, taken in turn (RD_VU_REC_SINKS).  With one, the second store
 * pass of a list found the first's attachment write pending (a DONT_CARE
 * load and store is a write in Vulkan's synchronisation), so its begin's
 * barrier, which goes out with the store bracket's own (vertex stages),
 * held colour output before the bracket: the pass's vertex shaders waited
 * for every earlier colour write of the list, the previous picture's too
 * (BARRIER179's finding; port/rhi/vk/vk_cmd.c vkr_hz_image).  With two,
 * neither of a list's two store passes begins on a target written earlier
 * in the list, so their begins carry no attachment barrier (a third pass
 * in one list, which the replay does not make, would wait again). */
#ifndef PORT_RENDER_RD_VU_REC_SINK_H
#define PORT_RENDER_RD_VU_REC_SINK_H

#include <stdbool.h>
#include <stdint.h>

#include "rhi.h"

#define RD_VU_REC_SINK_FMT RHI_FMT_R8_UNORM
#define RD_VU_REC_SINKS 2u

/* Whether the store passes begin on the targets on this device
 * (RhiLimits.storePassTarget); the store pipelines name one only then
 * (rd_pipeline.c) */
bool rd__vu_rec_sink_wanted(void);
/* Whether the two targets exist, made at the first call (one line); false
 * when they could not be made (one line; not tried again on this device).
 * True without making anything where they are not wanted (one line: the
 * passes begin without an attachment).
 * The pre-pass asks before a replay's record under the stores
 * (rd_replay_vu.c rd__vu_prepass): without them a store pass would begin
 * without an attachment while its pipelines name one (invalid usage), so
 * the stores are turned off for the run (rd__vu_record_fall_back) */
bool rd__vu_rec_sink_ready(void);
/* The next of the two targets in RHI_STATE_RENDER_TARGET on cl (made as
 * rd__vu_rec_sink_ready makes them); {0} when they could not be made, or
 * are not wanted (no attachment: counted by rd__vu_rec_sink_bare_passes) */
RhiTexture rd__vu_rec_sink(RhiCommandList cl);
/* target i (0 or 1; {0} before the first call or past them), for the tests */
RhiTexture rd__vu_rec_sink_target(uint32_t i);
/* the store passes begun on the target, and without it (it could not be
 * made), since the start (the replay tool's --stats and the tests) */
uint32_t rd__vu_rec_sink_passes(uint32_t *without);
/* the store passes begun without an attachment where none is wanted */
uint32_t rd__vu_rec_sink_bare_passes(void);
/* the targets destroyed (rd__vu_prepass_shutdown); the counts kept */
void rd__vu_rec_sink_free(void);

#endif /* PORT_RENDER_RD_VU_REC_SINK_H */
