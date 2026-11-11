/* rd_vu_rec_sink.c: the throwaway colour targets of the VU record's store
 * passes (rd_vu_rec_sink.h). */
#include "rd_vu_rec_sink.h"

#include <stdlib.h>

#include "rd_internal.h"
#include "shader_consts.h" /* ICO_VU_REC_W */

/* two, taken in turn (rd_vu_rec_sink.h): the store passes of one list
 * never begin on the target the one before wrote */
static RhiTexture s_sink[RD_VU_REC_SINKS];
static RhiState s_sinkState[RD_VU_REC_SINKS];
static uint32_t s_next;
static bool s_failed, s_madeSaid, s_bareSaid;
static uint32_t s_with, s_without, s_bare;

bool rd__vu_rec_sink_wanted(void)
{
    /* the device's limits (zero before rhi_init: not wanted) */
    return rhi_limits()->storePassTarget;
}

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

bool rd__vu_rec_sink_ready(void)
{
    if (!rd__vu_rec_sink_wanted()) {
        /* v0.11.2's shape (rd_vu_rec_sink.h): nothing to make, one line */
        if (!s_bareSaid) {
            s_bareSaid = true;
            rd__log("VU pre-pass: the store passes begin without an attachment (no throwaway "
                    "target: not the Intel driver row, no subpass read)");
        }
        return true;
    }
    if (!s_sink[0].id && !s_failed) {
        /* ICO_RD_VU_REC_SINK_FAIL=1 (the tests): the targets refused */
        const char *fail = getenv("ICO_RD_VU_REC_SINK_FAIL");
        const bool refuse = fail && fail[0] == '1';
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
            all = all && s_sink[i].id != 0 && !refuse;
        }
        if (!all) {
            /* not tried again on this device: one line */
            sinksFree();
            s_failed = true;
            rd__log("VU pre-pass: the store passes' two throwaway %ux1 R8 targets could not be "
                    "made: the stores off for the run (a store pass without them would not "
                    "match its pipelines, which name one)",
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
    return s_sink[0].id != 0;
}

RhiTexture rd__vu_rec_sink(RhiCommandList cl)
{
    if (!rd__vu_rec_sink_wanted()) {
        s_bare++; /* no attachment, as the store pipelines name none */
        return (RhiTexture){0};
    }
    if (!rd__vu_rec_sink_ready()) {
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

uint32_t rd__vu_rec_sink_bare_passes(void)
{
    return s_bare;
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
    s_bareSaid = false;
}
