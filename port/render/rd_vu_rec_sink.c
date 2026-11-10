/* rd_vu_rec_sink.c: the throwaway colour targets of the VU record's store
 * passes (rd_vu_rec_sink.h). */
#include "rd_vu_rec_sink.h"

#include "rd_internal.h"
#include "shader_consts.h" /* ICO_VU_REC_W */

/* two, taken in turn (rd_vu_rec_sink.h): the store passes of one list
 * never begin on the target the one before wrote */
static RhiTexture s_sink[RD_VU_REC_SINKS];
static RhiState s_sinkState[RD_VU_REC_SINKS];
static uint32_t s_next;
static bool s_failed, s_madeSaid;
static uint32_t s_with, s_without;

static void sinksFree(void)
{
    for (uint32_t i = 0; i < RD_VU_REC_SINKS; i++) {
        if (s_sink[i].id) {
            rhi_destroy_texture(s_sink[i]);
        }
        s_sink[i] = (RhiTexture){0};
        s_sinkState[i] = RHI_STATE_UNDEFINED;
    }
}

RhiTexture rd__vu_rec_sink(RhiCommandList cl)
{
    if (!s_sink[0].id && !s_failed) {
        for (uint32_t i = 0; i < RD_VU_REC_SINKS; i++) {
            s_sink[i] = rhi_create_texture(
                &(RhiTextureDesc){ICO_VU_REC_W, 1, 1, RD_VU_REC_SINK_FMT, RHI_TEX_RENDER_TARGET,
                                  (i & 1u) ? "rd vu record store pass B (never read)"
                                           : "rd vu record store pass A (never read)",
                                  0});
            s_sinkState[i] = RHI_STATE_UNDEFINED;
        }
        s_next = 0;
        bool all = true;
        for (uint32_t i = 0; i < RD_VU_REC_SINKS; i++) {
            all = all && s_sink[i].id != 0;
        }
        if (!all) {
            /* not tried again on this device: one line */
            sinksFree();
            s_failed = true;
            rd__log("VU pre-pass: the store passes' two throwaway %ux1 R8 targets could not be "
                    "made: they begin without an attachment, as before v0.11.3 (the shape "
                    "the Intel vkCmdBeginRendering crash reports share)",
                    (unsigned)ICO_VU_REC_W);
        } else if (!s_madeSaid) {
            s_madeSaid = true;
            rd__log("VU pre-pass: the store passes begin on two throwaway %ux1 R8 targets "
                    "taken in turn (loaded and stored DONT_CARE, never written or read: the "
                    "pipelines draw no fragment), so no render pass begins without an "
                    "attachment and none waits for the one before's attachment",
                    (unsigned)ICO_VU_REC_W);
        }
    }
    if (!s_sink[0].id) {
        s_without++;
        return (RhiTexture){0};
    }
    const uint32_t i = s_next;
    s_next = (s_next + 1u) % RD_VU_REC_SINKS;
    rd__transition(cl, s_sink[i], &s_sinkState[i], RHI_STATE_RENDER_TARGET);
    s_with++;
    return s_sink[i];
}

uint32_t rd__vu_rec_sink_passes(uint32_t *without)
{
    if (without) {
        *without = s_without;
    }
    return s_with;
}

RhiTexture rd__vu_rec_sink_target(uint32_t i)
{
    return i < RD_VU_REC_SINKS ? s_sink[i] : (RhiTexture){0};
}

void rd__vu_rec_sink_free(void)
{
    sinksFree();
    s_next = 0;
    s_failed = false;   /* a new device tries again */
    s_madeSaid = false; /* and says it again */
}
