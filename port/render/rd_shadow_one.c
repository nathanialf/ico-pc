/* rd_shadow_one.c: the shadow count resolved in one draw (RdProfile.shadowOne;
 * docs/port/WORK_BUFFERS.md "The shadow count in one draw").
 *
 * rd_replay.c doShadowResolve's seven stencil-tested draws leave, for a
 * stencil whose bits 0-5 hold m, RGB 4 m and A 0x80 where m != 0 in the
 * count target, and shadowReduce box-averages the scaled count to its GS
 * size (blit.hlsl box_reduce_ps).  Here shadow_count_ps does both in one
 * draw at the GS size: it loads the stencil of each texel of a pixel's box
 * through the depth's stencil view (RHI_ASPECT_STENCIL, the depth in
 * RHI_STATE_DEPTH_READ) and the count's bytes for it from a 64 x 1 table
 * (rd__shadow_table_texel), and averages them in box_reduce_ps's order over
 * box_reduce_ps's DrawCB (rd__shadow_reduce_cb).  rd_replay.c
 * shadowCountOne picks the destination (the count target at scale 1, the
 * reduced count above it); rd_shadow_selftest.c checks the result against
 * the seven draws on the device. */
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_replay.h"

/* the table, uploaded once a renderer */
static RhiTexture s_table;

static RhiState s_tableState;

static bool s_tableReady;

bool rd__shadow_sabotaged(void)
{
    static int s_sab = -1;
    if (s_sab < 0) {
        const char *e = getenv("ICO_RD_SHADOW_SABOTAGE");
        s_sab = e && strcmp(e, "1") == 0;
    }
    return s_sab == 1;
}

void rd__shadow_table_texel(uint32_t m, uint8_t rgba[4])
{
    m &= RD_SHADOW_STENCIL_MASK;
    rgba[0] = rgba[1] = rgba[2] = (uint8_t)(4u * m);
    rgba[3] = m ? 0x80u : 0u;
}

void rd__shadow_reduce_cb(const RdTargetRec *tc, IcoDrawCB *cb)
{
    memset(cb, 0, sizeof(*cb));
    cb->uvRect[2] = cb->uvRect[3] = 1.0f;
    cb->tex[0] = cb->tex[1] = cb->tex[2] = cb->tex[3] = 1.0f;
    cb->param[0] = (float)tc->tw / (float)tc->w;
    cb->param[1] = (float)tc->th / (float)tc->h;
    cb->param[2] = (float)tc->tw;
    cb->param[3] = (float)tc->th;
    if (tc->ox > 0.0f) {
        /* Full pixel: GS pixel x is texels x sx - ox (blit.hlsl
         * box_reduce_ps) */
        cb->param[0] = tc->sx;
        cb->scale[2] = tc->ox;
    }
}

void rd__shadow_one_shutdown(void)
{
    if (s_table.id) {
        rhi_destroy_texture(s_table);
    }
    s_table = (RhiTexture){0};
    s_tableReady = false;
}

/* the table made and its texels uploaded through the ring (outside a pass,
 * in the replay's list) */
static bool tableMake(RhiCommandList cl)
{
    if (s_tableReady) {
        return true;
    }
    if (!s_table.id) {
        s_table = rhi_create_texture(
            &(RhiTextureDesc){RD_SHADOW_TABLE_SIZE, 1, 1, RHI_FMT_RGBA8_UNORM,
                              RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd shadow count table", 0});
        s_tableState = RHI_STATE_UNDEFINED;
        if (!s_table.id) {
            return false;
        }
    }
    const RhiLimits *lim = rhi_limits();
    const uint32_t pitchA = lim->copyRowPitchAlign ? lim->copyRowPitchAlign : 1u;
    const uint32_t pitch = (RD_SHADOW_TABLE_SIZE * 4u + pitchA - 1u) / pitchA * pitchA;
    const uint64_t off = rd__ring_alloc(pitch, lim->copyOffsetAlign);
    if (off == ~0ull) {
        return false;
    }
    const uint32_t slot = rd__ring_slot();
    uint8_t *p = g_rd.ringMap[slot] + off;
    for (uint32_t m = 0; m < RD_SHADOW_TABLE_SIZE; m++) {
        rd__shadow_table_texel(m, p + m * 4u);
    }
    if (rd__shadow_sabotaged()) {
        p[4] ^= 0x04u; /* m = 1: R 0, not 4 (the self-test's failure path) */
    }
    rd__transition(cl, s_table, &s_tableState, RHI_STATE_COPY_DST);
    rhi_cmd_copy_buffer_to_texture(cl, g_rd.ring[slot], off, pitch, s_table, 0,
                                   (RhiRect){0, 0, RD_SHADOW_TABLE_SIZE, 1});
    rd__transition(cl, s_table, &s_tableState, RHI_STATE_SHADER_READ);
    s_tableReady = true;
    return true;
}

RhiPipeline rd__get_pipeline_held(const RdPipeKeyInt *k)
{
    rd__passring_pipeline_hold(1);
    const RhiPipeline p = rd__get_pipeline(k);
    rd__passring_pipeline_hold(0);
    return p;
}

bool rd__shadow_one_draw(Replay *r, RdTargetRec *tc, RdTargetRec *td, RhiTexture dst,
                         RhiState *dstState, const IcoDrawCB *reduceCb)
{
    const RdPipeKeyInt k = rd__shadow_count_key();
    const RhiLimits *lim = rhi_limits();
    if (!lim->stencilView && g_rd.profile.shadowOne) {
        /* the backend could not make a stencil view (it turns
         * RhiLimits.stencilView off and logs it once): the one draw goes
         * off for good, [dev] or not, and the seven draws resolve the count */
        rd__profile_self_test_off(RD_PROFILE_SHADOW_ONE);
    }
    if (!g_rd.fs[RD_FS_SHADOW_COUNT].id || !lim->stencilView || !td->depthSampled || !dst.id) {
        return false;
    }
    RhiPipeline pipe = rd__get_pipeline_held(&k);
    if (!pipe.id) {
        return false;
    }
    /* a reset no volume pass took is recorded first (its stencil clear),
     * and any open pass ends */
    rd__replay_end_pass(r);
    RhiCommandList cl = rd__replay_cl();
    if (!tableMake(cl)) {
        return false;
    }
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = td->depth;
    b[0].aspect = RHI_ASPECT_STENCIL;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = s_table;
    b[2].aspect = RHI_ASPECT_COLOR;
    const RhiBindGroup g2 = rd__replay_bind_group(g_rd.layoutTex, b, 3);
    if (!g2.id) {
        return false;
    }
    r->writeSerial++;
    rd__transition(cl, td->depth, &td->depthState, RHI_STATE_DEPTH_READ);
    rd__transition(cl, dst, dstState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = RHI_LOAD_DONT_CARE; /* every texel is written */
    p.colorCount = 1;
    p.width = tc->w;
    p.height = tc->h;
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, r->st.color, tc->w, tc->h, r->perfPost);
    rhi_cmd_begin_render_pass(cl, &p);
    /* in the pass ring as the count target's pass at its GS size (the
     * device-loss report; the seven draws' pass was the target's) */
    rhi_cmd_checkpoint(cl, rd__replay_pass_push(RD_PASSK_PASS, r->st.color, tc->w, tc->h, 0, 0, 0,
                                                0, r->perfPost));
    (void)rd__get_pipeline(&k); /* the cache's hit: its pipeline on this entry */
    rd__replay_viewport(tc->w, tc->h);
    const RhiRect all = {0, 0, tc->w, tc->h};
    rhi_cmd_set_scissor(cl, &all);
    IcoDrawCB cb = *reduceCb;
    cb.mode[0] = lim->stencilViewChannel; /* the stencil's channel in its view */
    rd__set_pipeline(cl, pipe, &k);
    rd__bind_uniform(cl, 0, rd__frame_group(tc->w, tc->h, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g2);
    rd__replay_draw(cl, 3, 0, 1);
    rhi_cmd_end_render_pass(cl);
    g_rdPerf.shadowOne++;
    return true;
}
