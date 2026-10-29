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
 * of the count's step (rd__shadow_table_texel: one table for the step 4,
 * one for the step 8, rd_shadow.c "The step"; the shader is the same), and
 * averages them in box_reduce_ps's order over
 * box_reduce_ps's DrawCB (rd__shadow_reduce_cb).  rd_replay.c
 * shadowCountOne picks the destination (the count target at scale 1, the
 * reduced count above it); rd_shadow_selftest.c checks the result against
 * the seven draws on the device. */
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_replay.h"

/* the tables, [0] the step 4's, [1] the step 8's, each uploaded once a
 * renderer, at its first use */
static RhiTexture s_table[2];

static RhiState s_tableState[2];

static bool s_tableReady[2];

bool rd__shadow_sabotaged(void)
{
    static int s_sab = -1;
    if (s_sab < 0) {
        const char *e = getenv("ICO_RD_SHADOW_SABOTAGE");
        s_sab = e && strcmp(e, "1") == 0;
    }
    return s_sab == 1;
}

void rd__shadow_table_texel(uint32_t m, uint32_t step, uint8_t rgba[4])
{
    m &= step == 8 ? RD_SHADOW_STEP8_MASK : RD_SHADOW_STENCIL_MASK;
    rgba[0] = rgba[1] = rgba[2] = (uint8_t)((step == 8 ? 8u : 4u) * m);
    rgba[3] = m ? 0x80u : 0u;
}

bool rd__shadow_resolve_pass(uint32_t step, int pass, RdPipeKeyInt *k, IcoDrawCB *cb, uint8_t *ref)
{
    const int bits = step == 8 ? 5 : 6;
    if (pass >= bits && pass < 6) {
        return false; /* step 8's bit 5: 256, nothing in eight bits */
    }
    *k = rd__shadow_resolve_key(pass, step);
    memset(cb, 0, sizeof(*cb));
    if (pass < 6) {
        cb->col[0] = cb->col[1] = cb->col[2] = (step == 8 ? 8u : 4u) << pass;
    } else {
        cb->col[3] = 0x80;
    }
    cb->uvRect[2] = cb->uvRect[3] = 1.0f;
    cb->tex[0] = cb->tex[1] = cb->tex[2] = cb->tex[3] = 1.0f;
    *ref = pass < 6 ? (uint8_t)(1u << pass) : 0;
    return true;
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
    for (int i = 0; i < 2; i++) {
        if (s_table[i].id) {
            rhi_destroy_texture(s_table[i]);
        }
        s_table[i] = (RhiTexture){0};
        s_tableReady[i] = false;
    }
}

/* the step's table (i 1: the step 8's) made and its texels uploaded
 * through the ring (outside a pass, in the replay's list) */
static bool tableMake(RhiCommandList cl, int i)
{
    if (s_tableReady[i]) {
        return true;
    }
    if (!s_table[i].id) {
        s_table[i] = rhi_create_texture(&(RhiTextureDesc){
            RD_SHADOW_TABLE_SIZE, 1, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST,
            i ? "rd shadow count table (step 8)" : "rd shadow count table", 0});
        s_tableState[i] = RHI_STATE_UNDEFINED;
        if (!s_table[i].id) {
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
        rd__shadow_table_texel(m, i ? 8u : 4u, p + m * 4u);
    }
    if (rd__shadow_sabotaged()) {
        p[4] ^= 0x04u; /* m = 1: R 0 (4) or 12 (8), not the step (the self-test's failure path) */
    }
    rd__transition(cl, s_table[i], &s_tableState[i], RHI_STATE_COPY_DST);
    rhi_cmd_copy_buffer_to_texture(cl, g_rd.ring[slot], off, pitch, s_table[i], 0,
                                   (RhiRect){0, 0, RD_SHADOW_TABLE_SIZE, 1});
    rd__transition(cl, s_table[i], &s_tableState[i], RHI_STATE_SHADER_READ);
    s_tableReady[i] = true;
    return true;
}

RhiPipeline rd__get_pipeline_held(const RdPipeKeyInt *k)
{
    rd__passring_pipeline_hold(1);
    const RhiPipeline p = rd__get_pipeline(k);
    rd__passring_pipeline_hold(0);
    return p;
}

bool rd__shadow_one_draw(Replay *r, RdTargetRec *tc, RdTargetRec *td, uint32_t step, RhiTexture dst,
                         RhiState *dstState, const IcoDrawCB *reduceCb)
{
    const int ti = step == 8 ? 1 : 0;
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
    if (!tableMake(cl, ti)) {
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
    b[2].texture = s_table[ti];
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
