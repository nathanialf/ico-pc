/* rd_vu_rec_sink.c: the throwaway colour target of the VU record's store
 * passes (rd_vu_rec_sink.h). */
#include "rd_vu_rec_sink.h"

#include "rd_internal.h"
#include "shader_consts.h" /* ICO_VU_REC_W */

static RhiTexture s_sink;
static RhiState s_sinkState;
static bool s_failed, s_madeSaid;
static uint32_t s_with, s_without;

RhiTexture rd__vu_rec_sink(RhiCommandList cl)
{
    if (!s_sink.id && !s_failed) {
        s_sink = rhi_create_texture(&(RhiTextureDesc){ICO_VU_REC_W, 1, 1, RD_VU_REC_SINK_FMT,
                                                      RHI_TEX_RENDER_TARGET,
                                                      "rd vu record store pass (never read)", 0});
        s_sinkState = RHI_STATE_UNDEFINED;
        if (!s_sink.id) {
            /* not tried again on this device: one line */
            s_failed = true;
            rd__log("VU pre-pass: the store passes' throwaway %ux1 R8 target could not be "
                    "made: they begin without an attachment, as before v0.11.3 (the shape "
                    "the Intel vkCmdBeginRendering crash reports share)",
                    (unsigned)ICO_VU_REC_W);
        } else if (!s_madeSaid) {
            s_madeSaid = true;
            rd__log("VU pre-pass: the store passes begin on a throwaway %ux1 R8 target (loaded "
                    "and stored DONT_CARE, never written or read: the pipelines draw no "
                    "fragment), so no render pass begins without an attachment",
                    (unsigned)ICO_VU_REC_W);
        }
    }
    if (!s_sink.id) {
        s_without++;
        return (RhiTexture){0};
    }
    rd__transition(cl, s_sink, &s_sinkState, RHI_STATE_RENDER_TARGET);
    s_with++;
    return s_sink;
}

uint32_t rd__vu_rec_sink_passes(uint32_t *without)
{
    if (without) {
        *without = s_without;
    }
    return s_with;
}

void rd__vu_rec_sink_free(void)
{
    if (s_sink.id) {
        rhi_destroy_texture(s_sink);
    }
    s_sink = (RhiTexture){0};
    s_sinkState = RHI_STATE_UNDEFINED;
    s_failed = false;   /* a new device tries again */
    s_madeSaid = false; /* and says it again */
}
