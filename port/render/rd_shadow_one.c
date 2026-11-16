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
 * the seven draws on the device.  The shader skips the texels whose m is 0
 * (their term is +0.0) and reads the table texel of a run of equal m once:
 * the same sum, fewer reads (blit.hlsl), and reads the stencil four texels
 * a gather (shadow_count_gather: the same texels and the same terms in the
 * same order) unless its self-test found the gathers' count off on the
 * device (g_rd.shadowLoad: a texel a load).  The pass is "shadow count WxH" in
 * the pass list (RD_PERF_ID_SHADOW_COUNT).  The count target's colour is
 * neither loaded nor stored while the count is made (rd_store_plan.c
 * rd__store_plan_count_color).
 *
 * Where the stencil view takes no gather (RhiLimits.stencilGather false:
 * Direct3D's X32_TYPELESS_G8X24_UINT has no shader-gather support, and the
 * D3D11 debug layer stops every draw whose pixel shader holds a gather on
 * it, reached or not: the v0.10.0 tag's WARP run of rd_msaa, 16 errors
 * 416), and wherever the loads are in force, the draw binds
 * shadow_count_load_ps, the same entry built without the gathers
 * (rd__shadow_count_loads).  The stencil read is never multisampled: the
 * view exists for single-sampled textures only (rhi.h RHI_ASPECT_STENCIL),
 * and under MSAA the count's passes close the epoch first, so the count
 * reads SCENE's own depth after msaaEnd's resolve.  Above 1x the levels'
 * full-size count (rd_shadowres.c) is fullPass's, or (v0.11.8) read from
 * the stencil by the first level's step with the pass kept ready behind a
 * guard ("the count from the stencil" below), or the seven draws', as the
 * device's timing chose (rd_shadow_count.c).  The line, once a form
 * and sample count (rd__shadow_msaa_note):
 *   shadow count: under MSAA 2x the count reads the scene's own
 *     single-sampled stencil after the epoch's resolve: one draw, a texel
 *     a load (the backend's stencil view takes no gather)   (events) */
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_replay.h"
#include "rd_shadow_box.h"
#include "rd_shadow_count.h" /* the full-size count's form above 1x */
#include "rd_shadowres.h"    /* the fine levels' full-size count */

bool rd__shadow_count_loads(void)
{
    const RhiLimits *lim = g_rd.hasDevice ? rhi_limits() : NULL;
    return g_rd.shadowLoad || (lim && !lim->stencilGather);
}

void rd__shadow_msaa_note(int one, uint32_t samples)
{
    /* bit (form, samples / 2 - 1): forms seven, gather, load; 2x 4x 8x */
    static uint32_t s_said;
    const int form = !one ? 0 : rd__shadow_count_loads() ? 2 : 1;
    const int si = samples >= 8u ? 2 : samples >= 4u ? 1 : 0;
    const uint32_t bit = 1u << (form * 3 + si);
    if (samples <= 1u || (s_said & bit)) {
        return;
    }
    s_said |= bit;
    const RhiLimits *lim = rhi_limits();
    rd__log("shadow count: under MSAA %ux the count reads the scene's own single-sampled stencil "
            "after the epoch's resolve: %s (reported once a form and count)",
            samples,
            form == 0   ? "the seven stencil-tested draws, the volumes' pass taking the shadow "
                          "reset as its stencil clear"
            : form == 1 ? "one draw, the stencil four texels a gather"
            : !lim->stencilGather
                ? "one draw, a texel a load (the backend's stencil view takes no gather)"
                : "one draw, a texel a load (the self-test's choice)");
}

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
    rd__shadow_count_end();
    rd__shadow_count_reset();   /* the form timed again on the next device */
    rd__shadow_fine_shutdown(); /* the fine levels and their pipelines */
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

/* the full-size pass's texels: the volumes' box in texels where the
 * GS-size pass had one (gsBox; none drawn where it is empty or cannot be
 * mapped), else every texel */
static RhiRect fullBox(const RdTargetRec *tc, const RhiRect *gsBox)
{
    RhiRect box = {0, 0, tc->tw, tc->th};
    if (gsBox && (!gsBox->w || !gsBox->h ||
                  !rd__gs_rect_texels(tc, gsBox->x, gsBox->y, gsBox->x + (int32_t)gsBox->w - 1,
                                      gsBox->y + (int32_t)gsBox->h - 1, 0, &box))) {
        box = (RhiRect){0, 0, 0, 0};
    }
    return box;
}

/* The shadow's levels at the scene's scale (rd_shadowres.c): after the count at its GS size,
 * the count drawn again into the count target at its own texels (the box of
 * one texel: the stencil's table texel itself, as at scale 1), over the
 * volumes' box in texels where the GS-size pass had one (gsBox; the rest
 * cleared to 0), the same pipeline and stencil view: one pass more, its
 * pass "shadow count WxH" at the texels.  The form RD_SC_FULL; under
 * RD_SC_STENCIL drawn only where the guard below asks for it */
static void fullPass(Replay *r, RdTargetRec *tc, RhiPipeline pipe, const RdPipeKeyInt *k,
                     RhiBindGroup g2, const IcoDrawCB *reduce, const RhiRect *gsBox)
{
    RhiCommandList cl = rd__replay_cl();
    const RhiRect all = {0, 0, tc->tw, tc->th};
    const RhiRect box = fullBox(tc, gsBox);
    r->writeSerial++;
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = tc->color;
    p.color[0].load = gsBox ? RHI_LOAD_CLEAR : RHI_LOAD_DONT_CARE; /* clear[] zeroed */
    p.colorCount = 1;
    p.width = tc->tw;
    p.height = tc->th;
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, RD_PERF_ID_SHADOW_COUNT, tc->tw, tc->th, r->perfPost);
    rhi_cmd_checkpoint(cl, rd__replay_pass_push(RD_PASSK_PASS, r->st.color, tc->tw, tc->th, 0, 0, 0,
                                                0, r->perfPost));
    rhi_cmd_begin_render_pass(cl, &p);
    (void)rd__get_pipeline(k);
    rd__replay_viewport(tc->tw, tc->th);
    rhi_cmd_set_scissor(cl, gsBox ? &box : &all);
    IcoDrawCB cb = *reduce;
    cb.param[0] = cb.param[1] = 1.0f;
    cb.param[2] = (float)tc->tw;
    cb.param[3] = (float)tc->th;
    cb.scale[2] = 0.0f;
    rd__set_pipeline(cl, pipe, k);
    rd__bind_uniform(cl, 0, rd__frame_group(tc->tw, tc->th, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g2);
    if (box.w && box.h) {
        rd__replay_draw(cl, 3, 0, 1);
    }
    rhi_cmd_end_render_pass(cl);
    g_rdPerf.shadowFinePasses++;
    if (g_rdPerf.shadowCountFull < 0xFFFFu) {
        g_rdPerf.shadowCountFull++;
    }
    rd__shadow_fine_count_full(r->st.color);
}

/* ---------------------------------------------- the count from the stencil
 * RD_SC_STENCIL (rd_shadow_count.h): the full-size pass is not drawn; the
 * first level's step (rd_shadowres.c) reads what it would have written
 * from the stencil and the table (shadowres_step_stencil_ps).  That holds
 * while the stencil and the count target are as the one draw left them, so
 * the pass is kept ready (its pipeline, group, DrawCB and box: everything
 * fullPass takes) and the guard watches both textures: rd__transition calls
 * rd__shadow_count_touch first for every texture it is asked about, and the
 * first touch of the depth or the count target's colour before the step
 * draws the pass then, in the replay's list, before the touch's own
 * barrier (the depth still in RHI_STATE_DEPTH_READ, its stencil the
 * count's): from there the step reads the count as v0.11.7's did.  Every
 * write of the stencil or the count goes through a transition first (a
 * pass's attachment, a copy's destination, a clear's pass, a sampled read
 * included), so nothing changes either without the guard seeing it.  The
 * step takes the count from the stencil (rd__shadow_count_take) and the
 * watch ends; the replay's end (rd_store_plan.c rd__store_plan_end) ends
 * it too, with the pass never drawn: nothing reads the scaled count
 * target's colour after the step but the step (rd_store_plan.c
 * rd__store_plan_count_color: above 1x it is undrawn after the count and
 * every read of it as a texture takes the reduced count; v0.11.7's step was
 * the one exception).  A touch in another command list than the replay's
 * cannot put the pass before it: the chain is refused then
 * (RD_SC_WHY_LIST, the PS2's size), which the replay's own transitions
 * never give.  After a step took it the watch goes on without a pass: a
 * second first-level step of the chain (the game draws one a chain) reads
 * the stencil again where nothing touched it since, and is refused
 * (RD_SC_WHY_AGAIN) where something did. */
/* ARMED: the pass kept ready, nothing touched; TAKEN: a step read the
 * stencil, still untouched (a second step of the chain may read it too);
 * STALE: touched after a step took it (a second step cannot: refused);
 * LOST: touched in another list before the step (refused); CHECK: the
 * check replay (rd_shadow_count.h): the full-size pass drawn, the depth
 * alone watched until the step draws the check (a touch: not drawn this
 * replay) */
enum { PEND_NONE = 0, PEND_ARMED, PEND_TAKEN, PEND_STALE, PEND_LOST, PEND_CHECK };

static struct {
    int state;
    Replay *r;
    RdTargetRec *tc, *td;
    uint32_t step;  /* the count's step (4 or 8): the seven draws' (the check) */
    RhiRect texBox; /* the full-size pass's texel box */
    RhiTexture table;
    RhiPipeline pipe;
    RdPipeKeyInt k;
    RhiBindGroup g2;
    IcoDrawCB cb;
    RhiRect gsBox;
    bool boxed;
    uint32_t color;              /* the count target's id */
    uint32_t colorTex, depthTex; /* the watched textures' ids */
    uint32_t replay;             /* g_rd.replayCounter at the count */
} s_pend;

static void pendDraw(void)
{
    fullPass(s_pend.r, s_pend.tc, s_pend.pipe, &s_pend.k, s_pend.g2, &s_pend.cb,
             s_pend.boxed ? &s_pend.gsBox : NULL);
}

void rd__shadow_count_touch(RhiCommandList cl, uint32_t texId)
{
    if (s_pend.state == PEND_CHECK && texId != 0 && texId == s_pend.depthTex) {
        s_pend.state = PEND_NONE;
        rd__shadow_count_check_lost("the depth was touched before the level's step", true);
        return;
    }
    if ((s_pend.state != PEND_ARMED && s_pend.state != PEND_TAKEN) || texId == 0 ||
        (texId != s_pend.depthTex && texId != s_pend.colorTex)) {
        return;
    }
    if (s_pend.state == PEND_TAKEN) {
        /* after the step: nothing but another step of the chain reads the
         * count (above), and that one is refused now; no pass */
        s_pend.state = PEND_STALE;
        return;
    }
    if (s_pend.replay != g_rd.replayCounter) {
        s_pend.state = PEND_NONE; /* another replay's (never: its end ended it) */
        return;
    }
    const int why = texId == s_pend.depthTex ? RD_SC_WHY_DEPTH : RD_SC_WHY_COUNT;
    if (cl.id != rd__replay_cl().id) {
        s_pend.state = PEND_LOST; /* the step refuses the chain (counted there) */
        return;
    }
    s_pend.state = PEND_NONE; /* first: fullPass's own transitions come here again */
    pendDraw();
    rd__shadow_count_guard(why);
}

int rd__shadow_count_take(uint32_t color)
{
    if (s_pend.color != color || s_pend.replay != g_rd.replayCounter) {
        return 0;
    }
    if (s_pend.state == PEND_LOST || s_pend.state == PEND_STALE) {
        rd__shadow_count_guard(s_pend.state == PEND_LOST ? RD_SC_WHY_LIST : RD_SC_WHY_AGAIN);
        s_pend.state = PEND_NONE;
        return -1;
    }
    if (s_pend.state == PEND_TAKEN) {
        return 1; /* a second step of the chain, nothing touched since */
    }
    if (s_pend.state != PEND_ARMED) {
        return 0;
    }
    if (rd__shadow_count_touch_test()) {
        /* the tests' guard test: a sampled read of the depth before the
         * step, as another pass's would be */
        RdTargetRec *t = s_pend.td;
        if (t && t->depth.id == s_pend.depthTex) {
            rd__transition(rd__replay_cl(), t->depth, &t->depthState, RHI_STATE_DEPTH_READ);
        }
        if (s_pend.state != PEND_ARMED) {
            return 0; /* drawn after all (RD_SC_WHY_DEPTH counted) */
        }
    }
    s_pend.state = PEND_TAKEN;
    return 1;
}

void rd__shadow_count_flush(uint32_t color, int why)
{
    if (s_pend.state != PEND_ARMED || s_pend.color != color ||
        s_pend.replay != g_rd.replayCounter) {
        return;
    }
    s_pend.state = PEND_NONE;
    pendDraw();
    rd__shadow_count_guard(why);
}

void rd__shadow_count_materialise(void)
{
    if (s_pend.state == PEND_TAKEN && s_pend.replay == g_rd.replayCounter) {
        s_pend.state = PEND_NONE;
        pendDraw(); /* the tests' readback of the count (rd_replay_tool --fine-check) */
    }
}

void rd__shadow_count_end(void)
{
    /* a check asked this replay and not recorded (no first level step from
     * the count drawn, or its depth touched): the next presented replay */
    rd__shadow_count_check_lost("no first level step from the count in the picture", false);
    memset(&s_pend, 0, sizeof(s_pend));
}

bool rd__shadow_count_check_take(uint32_t color, RdSrStencil *src, RdTargetRec **td, uint32_t *step)
{
    if (s_pend.state != PEND_CHECK || s_pend.color != color ||
        s_pend.replay != g_rd.replayCounter) {
        return false;
    }
    s_pend.state = PEND_NONE; /* the check's own passes touch the depth next */
    *src = (RdSrStencil){s_pend.td->depth, s_pend.table, rhi_limits()->stencilViewChannel,
                         s_pend.texBox};
    *td = s_pend.td;
    *step = s_pend.step;
    return true;
}

/* The check's seven draws (rd_replay.c doShadowResolve's, the same keys,
 * groups and order) at the full size into dst (w x h, RGBA8, the count
 * target's texels), the stencil test against td's depth, its depth and
 * stencil loaded and stored as they are (nothing written to them) */
bool rd__shadow_count_seven_into(RhiTexture dst, RhiState *dstState, RdTargetRec *td, uint32_t w,
                                 uint32_t h, uint32_t step)
{
    RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, dst, dstState, RHI_STATE_RENDER_TARGET);
    rd__transition(cl, td->depth, &td->depthState, RHI_STATE_DEPTH_WRITE);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = RHI_LOAD_CLEAR; /* clear[] zeroed, as the resolve's pass */
    p.colorCount = 1;
    p.depth.texture = td->depth;
    p.depth.depthLoad = RHI_LOAD_LOAD;
    p.depth.stencilLoad = RHI_LOAD_LOAD;
    p.depth.store = RHI_STORE_STORE;
    p.depth.stencilStore = RHI_STORE_STORE;
    p.width = w;
    p.height = h;
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, RD_PERF_ID_SHADOW_COUNT, w, h, -1);
    rhi_cmd_checkpoint(cl, rd__replay_pass_push(RD_PASSK_PASS, 0, w, h, 0, 0, 0, 0, -1));
    rhi_cmd_begin_render_pass(cl, &p);
    rd__replay_viewport(w, h);
    const RhiRect all = {0, 0, w, h};
    rhi_cmd_set_scissor(cl, &all);
    const RdUniform g0 = rd__frame_group(w, h, 0.0f, 0.0f);
    const RhiBindGroup g2 =
        rd__tex_group(g_rd.dummy, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP,
                                              RD_WRAP_CLAMP));
    uint32_t drawn = 0;
    for (int pass = 0; pass < RD_SHADOW_RESOLVE_PASSES; pass++) {
        RdPipeKeyInt k;
        IcoDrawCB cb;
        uint8_t ref;
        if (!rd__shadow_resolve_pass(step, pass, &k, &cb, &ref)) {
            continue;
        }
        const RhiPipeline pipe = rd__get_pipeline(&k);
        if (!pipe.id) {
            continue;
        }
        rd__set_pipeline(cl, pipe, &k);
        rd__bind_uniform(cl, 0, g0);
        rd__bind_uniform(cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(cl, 2, g2);
        rhi_cmd_set_stencil_ref(cl, ref);
        rd__replay_draw(cl, 3, 0, 1);
        drawn++;
    }
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, dst, dstState, RHI_STATE_SHADER_READ);
    return drawn > 0;
}

/* RD_SC_STENCIL at the count: the pass kept ready and the step told where
 * the count stands; false (the pass drawn now) where the depth's texels are
 * not the count's: the step reads the stencil at the count's texels */
static bool armStencil(Replay *r, RdTargetRec *tc, RdTargetRec *td, RhiPipeline pipe,
                       const RdPipeKeyInt *k, RhiBindGroup g2, const IcoDrawCB *cb,
                       const RhiRect *gsBox, RhiTexture table)
{
    if (td->tw != tc->tw || td->th != tc->th) {
        rd__shadow_count_guard(RD_SC_WHY_SIZE);
        return false;
    }
    memset(&s_pend, 0, sizeof(s_pend));
    s_pend.r = r;
    s_pend.tc = tc;
    s_pend.td = td;
    s_pend.pipe = pipe;
    s_pend.k = *k;
    s_pend.g2 = g2;
    s_pend.cb = *cb;
    s_pend.boxed = gsBox != NULL;
    if (gsBox) {
        s_pend.gsBox = *gsBox;
    }
    s_pend.color = r->st.color;
    s_pend.colorTex = tc->color.id;
    s_pend.depthTex = td->depth.id;
    s_pend.replay = g_rd.replayCounter;
    s_pend.state = PEND_ARMED;
    const RhiLimits *lim = rhi_limits();
    const RdSrStencil src = {td->depth, table, lim->stencilViewChannel, fullBox(tc, gsBox)};
    rd__shadow_fine_count_stencil(r->st.color, &src);
    return true;
}

/* the check replay (rd_shadow_count.h), after the full-size pass: the
 * depth watched until the first level's step draws the check */
static void armCheck(Replay *r, RdTargetRec *tc, RdTargetRec *td, const RhiRect *gsBox,
                     RhiTexture table, uint32_t step)
{
    if (td->tw != tc->tw || td->th != tc->th) {
        rd__shadow_count_check_refuse("the depth's texels are not the count's");
        return;
    }
    memset(&s_pend, 0, sizeof(s_pend));
    s_pend.r = r;
    s_pend.tc = tc;
    s_pend.td = td;
    s_pend.step = step;
    s_pend.table = table;
    s_pend.texBox = fullBox(tc, gsBox);
    s_pend.color = r->st.color;
    s_pend.depthTex = td->depth.id; /* colorTex 0: the count target is read as ever */
    s_pend.replay = g_rd.replayCounter;
    s_pend.state = PEND_CHECK;
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
    /* no gather may reach a view that takes none: without the loads-only
     * module there the seven draws resolve the count */
    const bool load = rd__shadow_count_loads();
    if (!lim->stencilGather && k.fs != RD_FS_SHADOW_COUNT_LOAD) {
        return false;
    }
    /* above 1x with the levels at the scene's scale: the full-size count's
     * form (rd_shadow_count.c); the seven draws are the resolve's own when
     * this returns false (rd_replay.c doShadowResolve), at the full size.
     * Not for a count into SCENE: the self-tests' frames (rd_shadow_selftest.c,
     * rd_volume_fs.c) count there and check the one draw itself; the game's
     * count target is its own (WORK2 in every fixture), and a count into
     * SCENE keeps v0.11.7's full-size pass */
    const bool fine = dst.id != tc->color.id && rd__shadow_fine_count_wanted(tc) &&
                      r->st.color != rd_target(RD_TARGET_SCENE).id;
    const int form = fine ? rd__shadow_count_form() : RD_SC_FULL;
    if (form == RD_SC_SEVEN) {
        if (g_rdPerf.shadowCountSeven < 0xFFFFu) {
            g_rdPerf.shadowCountSeven++;
        }
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
    /* over the volumes' box (rd_shadow_box.c): the count is (0, 0, 0, 0)
     * outside it, which the clear writes */
    RhiRect box;
    const bool boxed = rd__shadow_box_count_area(tc, &box);
    if (boxed) {
        p.color[0].load = RHI_LOAD_CLEAR; /* clear[] zeroed: (0, 0, 0, 0) */
    }
    p.width = tc->w;
    p.height = tc->h;
    /* "shadow count 848x448" in the pass list (not the count target's
     * "list 3 temporary"): one colour attachment, not loaded, stored; no
     * depth attachment (the stencil is a texture here) */
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, RD_PERF_ID_SHADOW_COUNT, tc->w, tc->h, r->perfPost);
    /* in the pass ring as the count target's pass at its GS size (the
     * device-loss report; the seven draws' pass was the target's), before
     * the begin, so the crash report's pass begun is this one */
    rhi_cmd_checkpoint(cl, rd__replay_pass_push(RD_PASSK_PASS, r->st.color, tc->w, tc->h, 0, 0, 0,
                                                0, r->perfPost));
    rhi_cmd_begin_render_pass(cl, &p);
    (void)rd__get_pipeline(&k); /* the cache's hit: its pipeline on this entry */
    rd__replay_viewport(tc->w, tc->h);
    const RhiRect all = {0, 0, tc->w, tc->h};
    rhi_cmd_set_scissor(cl, boxed ? &box : &all);
    IcoDrawCB cb = *reduceCb;
    cb.mode[0] = lim->stencilViewChannel; /* the stencil's channel in its view */
    cb.mode[1] = load ? 1u : 0u;          /* the loads, else four texels a gather */
    rd__set_pipeline(cl, pipe, &k);
    rd__bind_uniform(cl, 0, rd__frame_group(tc->w, tc->h, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g2);
    if (!boxed || (box.w && box.h)) {
        rd__replay_draw(cl, 3, 0, 1); /* an empty box: the clear alone */
    }
    rhi_cmd_end_render_pass(cl);
    if (dst.id == tc->color.id) {
        rd__shadow_fine_count_full(r->st.color); /* at scale 1 the count is its full size */
    } else if (rd__shadow_fine_count_wanted(tc)) {
        if (boxed && (!box.w || !box.h) && rd__shadow_fine_cuts()) {
            /* an empty box: the chain draws no step (rd_shadowres.c's empty
             * count), so the full-size count is never read: no pass */
            rd__shadow_fine_count_full(r->st.color);
        } else if (form != RD_SC_STENCIL ||
                   !armStencil(r, tc, td, pipe, &k, g2, &cb, boxed ? &box : NULL, s_table[ti])) {
            fullPass(r, tc, pipe, &k, g2, &cb, boxed ? &box : NULL);
            if (fine && rd__shadow_count_check_now()) {
                armCheck(r, tc, td, boxed ? &box : NULL, s_table[ti], step);
            }
        }
    }
    g_rdPerf.shadowOne++;
    g_rdPerf.shadowGathered += load ? 0u : 1u;
    rd__shadow_msaa_note(1, g_rdPerf.msaaSamples);
    g_rdPerf.shadowOneW = (uint16_t)tc->w;
    g_rdPerf.shadowOneH = (uint16_t)tc->h;
    g_rdPerf.shadowReadW = (uint16_t)td->tw;
    g_rdPerf.shadowReadH = (uint16_t)td->th;
    return true;
}
