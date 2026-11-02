/* d3d11_cmd.c: the rhi_cmd_* recording of the Direct3D 11 backend on the
 * lists' deferred contexts (issue 126; compile-only in v0.8.6: it has not
 * run on any Windows machine).  begin/end/submit, the timestamps and the
 * labels are d3d11_device.c's.
 *
 * Direct3D 11 tracks hazards itself, so barriers record nothing.  What it
 * does not allow is one texture bound as a shader resource and a target at
 * once (the runtime unbinds one side, and a shader resource bound while
 * the texture is still an output is forced to NULL): a pass begins by
 * unbinding the SRVs the list bound and ends by unbinding its targets, and
 * the bind groups are bound again at the next draw.
 *
 * Everything a list reads from an UPLOAD buffer's parts on the GPU is
 * noted with d11_cmd_touch (the uniform windows, the storage ranges, the
 * vertex and index ranges of each draw, the source of a buffer copy);
 * rhi_submit writes those ranges from the CPU shadow before the list runs.
 * A buffer-to-texture copy from an UPLOAD buffer reads the shadow when it is
 * recorded (UpdateSubresource1 on the deferred context), so the rows must
 * be written before the copy is recorded, as every caller does. */
#include "d3d11_internal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define D11_STAGE_VS (1u << RHI_STAGE_VERTEX)
#define D11_STAGE_PS (1u << RHI_STAGE_FRAGMENT)
#define D11_GROUPS_ALL ((1u << RHI_MAX_BIND_SLOTS) - 1u)

/* D11CmdState.objKnown and .known: what the bind cache knows the deferred
 * context holds (d3d11_internal.h) */
#define D11_OBJ_IL 1u
#define D11_OBJ_TOPO 2u
#define D11_OBJ_VS 4u
#define D11_OBJ_PS 8u
#define D11_OBJ_RS 16u
#define D11_OBJ_BLEND 32u
#define D11_OBJ_DS 64u
#define D11_KNOW_VIEWPORT 1u
#define D11_KNOW_SCISSOR 2u
#define D11_KNOW_INDEX 4u
#define D11_KNOW_STENCIL 8u
#define D11_KNOW_BLEND_CONSTANT 16u

/* logged once per kind of misuse the backend can only skip */
static bool d11_once(bool *flag)
{
    if (*flag) {
        return false;
    }
    *flag = true;
    return true;
}

/* The list being recorded for a handle, NULL for a stale or finished one. */
static D11CommandList *d11_rec(RhiCommandList cl)
{
    D11CommandList *c = d11_get_cmd(cl);
    return c && c->recording && c->ctx ? c : NULL;
}

/* ---------------------------------------------------------------- barriers
 * Nothing to record (the runtime orders every read after every write);
 * the barriers are checked so a bad handle is still reported. */
void rhi_cmd_barrier(RhiCommandList cl, const RhiTextureBarrier *barriers, uint32_t count)
{
    D11CommandList *c = d11_rec(cl);
    if (!c || !barriers) {
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!d11_get_texture(barriers[i].texture) || barriers[i].before >= RHI_STATE_COUNT ||
            barriers[i].after >= RHI_STATE_COUNT) {
            D11_LOG("rhi_cmd_barrier: invalid barrier %u", i);
        }
    }
}

void rhi_cmd_local_read_barrier(RhiCommandList cl, uint32_t mask)
{
    (void)cl; /* no local read on this backend (RhiLimits.localRead false) */
    (void)mask;
}

/* ------------------------------------------------------------ render passes */
/* The SRVs the list bound, unbound (a texture about to be a target must not
 * stay a shader resource); the groups are bound again at the next draw. */
static void d11_unbind_srvs(D11CommandList *c)
{
    static ID3D11ShaderResourceView *const s_null[32];
    for (uint32_t st = 0; st < 2; st++) {
        const uint32_t m = c->st.srvMask[st];
        if (!m) {
            continue;
        }
        uint32_t n = 0;
        while (n < 32u && (m >> n)) {
            n++;
        }
        if (st == RHI_STAGE_VERTEX) {
            ID3D11DeviceContext_VSSetShaderResources(c->ctx, 0, n,
                                                     (ID3D11ShaderResourceView **)s_null);
        } else {
            ID3D11DeviceContext_PSSetShaderResources(c->ctx, 0, n,
                                                     (ID3D11ShaderResourceView **)s_null);
        }
        c->st.srvMask[st] = 0;
        memset(c->st.srvSet[st], 0, sizeof(c->st.srvSet[st]));
    }
    c->st.groupDirty = D11_GROUPS_ALL;
}

/* A pass larger than an attachment draws past it: logged once with the
 * sizes, an assert in the debug build, and the defaults clamped to the
 * smallest attachment, as on Vulkan and D3D12 (dx_pass_area_check). */
static void d11_pass_area_check(uint32_t *w, uint32_t *h, const RhiRenderPassDesc *pass,
                                const D11Texture *depth)
{
    static uint32_t s_bad;
    uint32_t mw = *w, mh = *h;
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        const D11Texture *t = d11_get_texture(pass->color[i].texture);
        if (t) {
            mw = t->width < mw ? t->width : mw;
            mh = t->height < mh ? t->height : mh;
        }
    }
    if (depth) {
        mw = depth->width < mw ? depth->width : mw;
        mh = depth->height < mh ? depth->height : mh;
    }
    if (mw == *w && mh == *h) {
        return;
    }
    if (s_bad++ == 0) {
        D11_LOG("render pass %ux%u is larger than an attachment (depth %ux%u): drawn at %ux%u "
                "(reported once)",
                *w, *h, depth ? depth->width : 0u, depth ? depth->height : 0u, mw, mh);
    }
#ifdef ICO_RHI_DEBUG_DEFAULT
    assert(!"render pass larger than an attachment");
#endif
    *w = mw;
    *h = mh;
}

/* A colour target and a depth target of different sizes in one pass: the
 * Vulkan and D3D12 backends draw where both are (the area clamped,
 * d11_pass_area_check); Direct3D 11 binds a depth-stencil view only with
 * render target views of its own dimensions (OMSetRenderTargets refuses the
 * pair, which leaves the pass with no target).  The first 8 pairs are
 * logged with their sizes, as an event, so a log says whether a pass met
 * it; the debug layer names the refusal itself. */
static void d11_pair_size_note(const D11Texture *colour, const D11Texture *depth)
{
    static uint32_t s_n;
    if (s_n >= 8u) {
        return;
    }
    s_n++;
    D11_LOG("render pass: colour target %ux%u with a depth target %ux%u: Direct3D 11 binds a "
            "depth-stencil view only with targets of its size (the first 8 reported)",
            colour->width, colour->height, depth->width, depth->height);
}

void rhi_cmd_begin_render_pass(RhiCommandList cl, const RhiRenderPassDesc *pass)
{
    D11CommandList *c = d11_rec(cl);
    if (!c || !pass || pass->colorCount > RHI_MAX_COLOR_TARGETS) {
        return;
    }
    ID3D11RenderTargetView *rtv[RHI_MAX_COLOR_TARGETS] = {0};
    D11Texture *ct[RHI_MAX_COLOR_TARGETS] = {0};
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        ct[i] = d11_get_texture(pass->color[i].texture);
        if (!ct[i] || !ct[i]->rtv) {
            D11_LOG("render pass: invalid colour target %u", i);
            return;
        }
        rtv[i] = ct[i]->rtv;
    }
    D11Texture *dt = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    if (pass->depth.texture.id) {
        dt = d11_get_texture(pass->depth.texture);
        if (!dt || !dt->dsv) {
            D11_LOG("render pass: invalid depth target");
            return;
        }
        dsv = pass->depth.readOnlyDepth ? dt->dsvRO : dt->dsv;
    }
    d11_unbind_srvs(c);
    if (dt && pass->colorCount && (dt->width != ct[0]->width || dt->height != ct[0]->height)) {
        d11_pair_size_note(ct[0], dt);
    }
    c->st.rtW = pass->colorCount ? ct[0]->width : 0u;
    c->st.rtH = pass->colorCount ? ct[0]->height : 0u;
    c->st.rtFmt = pass->colorCount ? ct[0]->resFormat : DXGI_FORMAT_UNKNOWN;
    c->st.dsW = dt ? dt->width : 0u;
    c->st.dsH = dt ? dt->height : 0u;
    c->st.dsReadOnly = dt && pass->depth.readOnlyDepth;
    /* the store ops are ignored: Direct3D 11 keeps what a pass drew, a valid
     * DONT_CARE (rhi.h) */
    ID3D11DeviceContext_OMSetRenderTargets(c->ctx, pass->colorCount, pass->colorCount ? rtv : NULL,
                                           dsv);
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        const RhiColorAttachment *a = &pass->color[i];
        if (a->load == RHI_LOAD_DONT_CARE) {
            /* DiscardView drops the whole view: only when the pass covers
             * it (a discard is a hint; outside the area must be kept) */
            if (pass->width >= ct[i]->width && pass->height >= ct[i]->height) {
                ID3D11DeviceContext1_DiscardView(c->ctx1, (ID3D11View *)rtv[i]);
            }
            continue;
        }
        if (a->load != RHI_LOAD_CLEAR) {
            continue;
        }
        float v[4];
        for (int k = 0; k < 4; k++) {
            /* integer targets: the clear value is the integer, rounded as on
             * Vulkan; D3D11 converts the (now integral) float exactly */
            v[k] = d11_formatMap[ct[i]->rhiFormat].isInteger ? d3dp_int_clear_value(a->clear[k])
                                                             : a->clear[k];
        }
        ID3D11DeviceContext_ClearRenderTargetView(c->ctx, rtv[i], v);
    }
    if (dt) {
        const bool stencil = d11_formatMap[dt->rhiFormat].stencil;
        UINT flags = 0;
        if (pass->depth.depthLoad == RHI_LOAD_CLEAR) {
            flags |= D3D11_CLEAR_DEPTH;
        }
        if (pass->depth.stencilLoad == RHI_LOAD_CLEAR && stencil) {
            flags |= D3D11_CLEAR_STENCIL;
        }
        if (flags && pass->depth.readOnlyDepth) {
            D11_LOG("render pass: a read-only depth target cannot be cleared; clear ignored");
        } else if (flags) {
            ID3D11DeviceContext_ClearDepthStencilView(
                c->ctx, dt->dsv, flags, pass->depth.clearDepth, pass->depth.clearStencil);
        } else if (!pass->depth.readOnlyDepth && pass->depth.depthLoad == RHI_LOAD_DONT_CARE &&
                   (!stencil || pass->depth.stencilLoad == RHI_LOAD_DONT_CARE) &&
                   pass->width >= dt->width && pass->height >= dt->height) {
            /* both aspects undefined and the pass over the whole view: it
             * may be discarded */
            ID3D11DeviceContext1_DiscardView(c->ctx1, (ID3D11View *)dt->dsv);
        }
    }
    for (uint32_t i = 0; i < RHI_MAX_COLOR_TARGETS; i++) {
        c->st.targets[i] = i < pass->colorCount ? pass->color[i].texture.id : 0u;
    }
    c->st.depthTarget = dt ? pass->depth.texture.id : 0u;
    c->st.inPass = true;
    g_d11.counts.renderPasses++;
    uint32_t areaW = pass->width, areaH = pass->height;
    d11_pass_area_check(&areaW, &areaH, pass, dt);
    c->st.passW = areaW;
    c->st.passH = areaH;
    /* the defaults are recorded whatever the list held (the bind cache
     * forgets the viewport and scissor at a pass's start) */
    c->st.known &= ~(D11_KNOW_VIEWPORT | D11_KNOW_SCISSOR);
    RhiViewport vp = {0.0f, 0.0f, (float)areaW, (float)areaH, 0.0f, 1.0f};
    RhiRect sc = {0, 0, areaW, areaH};
    rhi_cmd_set_viewport(cl, &vp);
    rhi_cmd_set_scissor(cl, &sc);
}

void rhi_cmd_end_render_pass(RhiCommandList cl)
{
    D11CommandList *c = d11_rec(cl);
    if (!c || !c->st.inPass) {
        return;
    }
    /* the targets unbound, so a later pass can sample them */
    ID3D11DeviceContext_OMSetRenderTargets(c->ctx, 0, NULL, NULL);
    memset(c->st.targets, 0, sizeof(c->st.targets));
    c->st.depthTarget = 0;
    c->st.inPass = false;
}

void rhi_cmd_set_viewport(RhiCommandList cl, const RhiViewport *v)
{
    D11CommandList *c = d11_rec(cl);
    if (!c || !v) {
        return;
    }
    /* D3D's clip space is the RHI's: no flip (the Vulkan backend flips) */
    D3D11_VIEWPORT vp = {v->x, v->y, v->w, v->h, v->minDepth, v->maxDepth};
    if ((c->st.known & D11_KNOW_VIEWPORT) && memcmp(&vp, &c->st.vp, sizeof(vp)) == 0) {
        g_d11.counts.stateSetsSkipped++;
        return;
    }
    ID3D11DeviceContext_RSSetViewports(c->ctx, 1, &vp);
    c->st.vp = vp;
    c->st.known |= D11_KNOW_VIEWPORT;
}

void rhi_cmd_set_scissor(RhiCommandList cl, const RhiRect *r)
{
    D11CommandList *c = d11_rec(cl);
    if (!c || !r) {
        return;
    }
    D3D11_RECT sc = {r->x, r->y, r->x + (LONG)r->w, r->y + (LONG)r->h};
    if ((c->st.known & D11_KNOW_SCISSOR) && memcmp(&sc, &c->st.sc, sizeof(sc)) == 0) {
        g_d11.counts.stateSetsSkipped++;
        return;
    }
    ID3D11DeviceContext_RSSetScissorRects(c->ctx, 1, &sc);
    c->st.sc = sc;
    c->st.known |= D11_KNOW_SCISSOR;
}

/* ------------------------------------------------------------------ state */
/* The output merger's two states with their values (the blend factor, the
 * stencil reference), set unless the context holds them; true when set */
static bool d11_om_blend(D11CommandList *c, ID3D11BlendState *b, const float factor[4])
{
    if ((c->st.objKnown & D11_OBJ_BLEND) && c->st.blendSet == b &&
        memcmp(c->st.blendFactorSet, factor, sizeof(c->st.blendFactorSet)) == 0) {
        return false;
    }
    ID3D11DeviceContext_OMSetBlendState(c->ctx, b, factor, 0xFFFFFFFFu);
    c->st.blendSet = b;
    memcpy(c->st.blendFactorSet, factor, sizeof(c->st.blendFactorSet));
    c->st.objKnown |= D11_OBJ_BLEND;
    return true;
}

static bool d11_om_ds(D11CommandList *c, ID3D11DepthStencilState *ds, uint8_t ref)
{
    if ((c->st.objKnown & D11_OBJ_DS) && c->st.dsSet == ds && c->st.stencilRefSet == ref) {
        return false;
    }
    ID3D11DeviceContext_OMSetDepthStencilState(c->ctx, ds, ref);
    c->st.dsSet = ds;
    c->st.stencilRefSet = ref;
    c->st.objKnown |= D11_OBJ_DS;
    return true;
}

/* A pipeline change sets the objects of the new pipeline that the context
 * does not hold already (pipelines of one shader pair share their shaders
 * and input layout, the state tables their rasterizer, blend and
 * depth-stencil objects); each one not set is a stateSetsSkipped */
void rhi_cmd_set_pipeline(RhiCommandList cl, RhiPipeline p)
{
    D11CommandList *c = d11_rec(cl);
    D11Pipeline *pp = d11_get_pipeline(p.id);
    if (!c || !pp) {
        return;
    }
    c->st.pipeline = p.id;
    if (c->st.pipelineSet == p.id) {
        g_d11.counts.pipelineBindsSkipped++;
        return;
    }
    D11CmdState *st = &c->st;
    uint32_t same = 0;
    if ((st->objKnown & D11_OBJ_IL) && st->ilSet == pp->il) {
        same++;
    } else {
        ID3D11DeviceContext_IASetInputLayout(c->ctx, pp->il);
        st->ilSet = pp->il;
    }
    if ((st->objKnown & D11_OBJ_TOPO) && st->topoSet == pp->topo) {
        same++;
    } else {
        ID3D11DeviceContext_IASetPrimitiveTopology(c->ctx, pp->topo);
        st->topoSet = pp->topo;
    }
    if ((st->objKnown & D11_OBJ_VS) && st->vsSet == pp->vs) {
        same++;
    } else {
        ID3D11DeviceContext_VSSetShader(c->ctx, pp->vs, NULL, 0);
        st->vsSet = pp->vs;
    }
    if ((st->objKnown & D11_OBJ_PS) && st->psSet == pp->ps) {
        same++;
    } else {
        ID3D11DeviceContext_PSSetShader(c->ctx, pp->ps, NULL, 0);
        st->psSet = pp->ps;
    }
    if ((st->objKnown & D11_OBJ_RS) && st->rsSet == pp->rs) {
        same++;
    } else {
        ID3D11DeviceContext_RSSetState(c->ctx, pp->rs);
        st->rsSet = pp->rs;
    }
    st->objKnown |= D11_OBJ_IL | D11_OBJ_TOPO | D11_OBJ_VS | D11_OBJ_PS | D11_OBJ_RS;
    same += !d11_om_blend(c, pp->blend, st->blendConstant);
    same += !d11_om_ds(c, pp->ds, st->stencilRef);
    g_d11.counts.stateSetsSkipped += same;
    st->pipelineSet = p.id;
    g_d11.counts.pipelineBinds++;
}

void rhi_cmd_set_bind_group_offsets(RhiCommandList cl, uint32_t group, RhiBindGroup bg,
                                    const uint32_t *offsets, uint32_t count)
{
    D11CommandList *c = d11_rec(cl);
    if (!c || group >= RHI_MAX_BIND_SLOTS) {
        return;
    }
    D11BindGroup *g = bg.id ? d11_get_bind_group(bg.id) : NULL;
    if (bg.id && !g) {
        D11_LOG("rhi_cmd_set_bind_group: bind group %08x is not from this frame", bg.id);
        bg.id = 0;
    }
    const uint32_t dyn = g ? g->dynCount : 0;
    if (count > dyn) {
        D11_LOG("rhi_cmd_set_bind_group_offsets: %u offsets for %u dynamic slots", count, dyn);
        count = dyn;
    }
    uint32_t off[RHI_MAX_DYNAMIC_OFFSETS] = {0};
    for (uint32_t i = 0; i < count && offsets && i < RHI_MAX_DYNAMIC_OFFSETS; i++) {
        off[i] = offsets[i];
    }
    const uint32_t bit = 1u << group;
    /* the group the slot holds with the same offsets, its registers not
     * written over since (groupStale): bound, or flushed at the next draw
     * (its dirty bit kept, as after a pass's start) */
    if (c->st.groups[group] == bg.id && !(c->st.groupStale & bit) &&
        memcmp(c->st.offsets[group], off, sizeof(off)) == 0) {
        g_d11.counts.bindGroupBindsSkipped++;
        return;
    }
    memcpy(c->st.offsets[group], off, sizeof(off));
    c->st.groups[group] = bg.id;
    c->st.groupDirty |= bit;
}

void rhi_cmd_set_bind_group(RhiCommandList cl, uint32_t group, RhiBindGroup bg)
{
    rhi_cmd_set_bind_group_offsets(cl, group, bg, NULL, 0);
}

void rhi_cmd_set_vertex_buffer(RhiCommandList cl, uint32_t binding, RhiBuffer b, uint64_t offset)
{
    D11CommandList *c = d11_rec(cl);
    D11Buffer *buf = d11_get_buffer(b);
    if (!c || !buf || binding >= RHI_MAX_VERTEX_ATTRS || offset >= buf->size ||
        !buf->part[D11_PART_VI] || offset > 0xFFFFFFFFull) {
        return;
    }
    if (c->st.vb[binding] == b.id && c->st.vbOffset[binding] == offset) {
        /* the buffer the slot holds (or gets at the next draw) */
        g_d11.counts.stateSetsSkipped++;
        return;
    }
    c->st.vb[binding] = b.id;
    c->st.vbOffset[binding] = offset;
    c->st.vbDirty |= 1u << binding;
}

void rhi_cmd_set_index_buffer(RhiCommandList cl, RhiBuffer b, uint64_t offset, bool u32)
{
    D11CommandList *c = d11_rec(cl);
    D11Buffer *buf = d11_get_buffer(b);
    if (!c || !buf || offset >= buf->size || !buf->part[D11_PART_VI] || offset > 0xFFFFFFFFull) {
        return;
    }
    if ((c->st.known & D11_KNOW_INDEX) && c->st.ib == b.id && c->st.ibOffset == offset &&
        c->st.ib32 == u32) {
        g_d11.counts.stateSetsSkipped++;
        return;
    }
    c->st.known |= D11_KNOW_INDEX;
    ID3D11DeviceContext_IASetIndexBuffer(c->ctx, buf->part[D11_PART_VI],
                                         u32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT,
                                         (UINT)offset);
    c->st.ib = b.id;
    c->st.ibOffset = offset;
    c->st.ib32 = u32;
}

/* Direct3D 11 takes the stencil reference and the blend constant with the
 * state objects: the bound pipeline's are set again with the new value */
void rhi_cmd_set_stencil_ref(RhiCommandList cl, uint8_t ref)
{
    D11CommandList *c = d11_rec(cl);
    if (!c) {
        return;
    }
    if ((c->st.known & D11_KNOW_STENCIL) && c->st.stencilRef == ref) {
        g_d11.counts.stateSetsSkipped++;
        return;
    }
    c->st.stencilRef = ref;
    c->st.known |= D11_KNOW_STENCIL;
    D11Pipeline *p = c->st.pipelineSet ? d11_get_pipeline(c->st.pipelineSet) : NULL;
    if (p) {
        d11_om_ds(c, p->ds, ref);
    }
}

void rhi_cmd_set_blend_constant(RhiCommandList cl, const float rgba[4])
{
    D11CommandList *c = d11_rec(cl);
    if (!c || !rgba) {
        return;
    }
    if ((c->st.known & D11_KNOW_BLEND_CONSTANT) &&
        memcmp(c->st.blendConstant, rgba, sizeof(c->st.blendConstant)) == 0) {
        g_d11.counts.stateSetsSkipped++;
        return;
    }
    memcpy(c->st.blendConstant, rgba, sizeof(c->st.blendConstant));
    c->st.known |= D11_KNOW_BLEND_CONSTANT;
    D11Pipeline *p = c->st.pipelineSet ? d11_get_pipeline(c->st.pipelineSet) : NULL;
    if (p) {
        d11_om_blend(c, p->blend, c->st.blendConstant);
    }
}

/* Dynamic depth and blend state (rhi.h): not on this backend (its limits
 * say dynamicDepth and dynamicBlend false, so the renderer never calls
 * them); a call is logged once and changes nothing */
void rhi_cmd_set_depth_state(RhiCommandList cl, bool test, bool write, RhiCompare cmp)
{
    static bool s_logged;
    (void)cl;
    (void)test;
    (void)write;
    (void)cmp;
    if (d11_once(&s_logged)) {
        D11_LOG("rhi_cmd_set_depth_state: no dynamic depth state on Direct3D 11; ignored "
                "(logged once)");
    }
}

void rhi_cmd_set_blend_state(RhiCommandList cl, const RhiBlendState *blend)
{
    static bool s_logged;
    (void)cl;
    (void)blend;
    if (d11_once(&s_logged)) {
        D11_LOG("rhi_cmd_set_blend_state: no dynamic blend state on Direct3D 11; ignored "
                "(logged once)");
    }
}

/* ------------------------------------------------------------------- draws */
/* One constant buffer window into register `slot` of a stage
 * (RHI_STAGE_*).  A window equal to the one the register holds is not set
 * again.  The same buffer with another window is unbound first when the
 * runtime emulates command lists: Microsoft documents that the runtime's
 * emulation may drop a VSSetConstantBuffers1 (and the other stages') that
 * names the buffer already bound with only the offset or size changed (the
 * remarks' "Calling VSSetConstantBuffers1 with command list emulation",
 * whose workaround, for emulated command lists only, is this NULL bind);
 * the port binds every draw's uniforms that way, windows of one ring
 * buffer.  A driver that records command lists itself (DriverCommandLists,
 * the start-up line's "command lists by the driver") takes the window
 * alone.  NULL (a binding the group could not use) binds nothing, without
 * windows, as the remarks require of a NULL buffer. */
static void d11_set_cb(D11CommandList *c, uint32_t stage, uint32_t slot, ID3D11Buffer *cb,
                       UINT first, UINT num)
{
    ID3D11Buffer **held = &c->st.cbSet[stage][slot];
    if (*held == cb &&
        (!cb || (c->st.cbFirst[stage][slot] == first && c->st.cbNum[stage][slot] == num))) {
        return;
    }
    ID3D11Buffer *none = NULL;
    const bool sameBuffer = cb && *held == cb && !g_d11.driverCommandLists;
    if (stage == RHI_STAGE_VERTEX) {
        if (sameBuffer) {
            ID3D11DeviceContext1_VSSetConstantBuffers1(c->ctx1, slot, 1, &none, NULL, NULL);
        }
        ID3D11DeviceContext1_VSSetConstantBuffers1(c->ctx1, slot, 1, &cb, cb ? &first : NULL,
                                                   cb ? &num : NULL);
    } else {
        if (sameBuffer) {
            ID3D11DeviceContext1_PSSetConstantBuffers1(c->ctx1, slot, 1, &none, NULL, NULL);
        }
        ID3D11DeviceContext1_PSSetConstantBuffers1(c->ctx1, slot, 1, &cb, cb ? &first : NULL,
                                                   cb ? &num : NULL);
    }
    *held = cb;
    c->st.cbFirst[stage][slot] = cb ? first : 0u;
    c->st.cbNum[stage][slot] = cb ? num : 0u;
}

/* One bind group entry into its registers in the stages it names (none
 * named: both).  Uniforms are constant buffer windows (11.1 offsets, in
 * 16-byte constants), storage buffers structured SRVs, textures SRVs and
 * samplers sampler states; the upload ranges they read are noted. */
static void d11_bind_entry(D11CommandList *c, const D11BindEntry *e, const uint32_t *offsets)
{
    static bool s_slotLogged;
    const uint32_t stages = e->stages ? e->stages : (D11_STAGE_VS | D11_STAGE_PS);
    switch ((RhiBindType)e->type) {
    case RHI_BIND_UNIFORM_BUFFER:
    case RHI_BIND_UNIFORM_BUFFER_DYNAMIC: {
        if (e->slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) {
            if (d11_once(&s_slotLogged)) {
                D11_LOG("bind group: register %u is past what Direct3D 11 binds (logged once)",
                        e->slot);
            }
            return;
        }
        const uint32_t off = e->dynIndex != D11_NO_DYN && e->dynIndex < RHI_MAX_DYNAMIC_OFFSETS
                                 ? offsets[e->dynIndex]
                                 : 0u;
        ID3D11Buffer *cb = e->cb;
        UINT first = e->firstConstant + off / 16u, num = e->numConstants;
        if (cb) {
            d11_cmd_touch(c, e->buffer, D11_PART_CB, e->lo + off, e->hi + off);
        } else {
            first = num = 0;
        }
        if (stages & D11_STAGE_VS) {
            d11_set_cb(c, RHI_STAGE_VERTEX, e->slot, cb, first, num);
        }
        if (stages & D11_STAGE_PS) {
            d11_set_cb(c, RHI_STAGE_FRAGMENT, e->slot, cb, first, num);
        }
        return;
    }
    case RHI_BIND_STORAGE_BUFFER:
    case RHI_BIND_SAMPLED_TEXTURE: {
        if (e->slot >= 32u) {
            if (d11_once(&s_slotLogged)) {
                D11_LOG("bind group: register %u is past what Direct3D 11 binds (logged once)",
                        e->slot);
            }
            return;
        }
        ID3D11ShaderResourceView *srv = e->srv;
        if (srv && e->type == RHI_BIND_STORAGE_BUFFER) {
            d11_cmd_touch(c, e->buffer, D11_PART_SRV, e->lo, e->hi);
        }
        /* a register already holding the view is not set again */
        if ((stages & D11_STAGE_VS) && c->st.srvSet[RHI_STAGE_VERTEX][e->slot] != srv) {
            ID3D11DeviceContext_VSSetShaderResources(c->ctx, e->slot, 1, &srv);
            c->st.srvSet[RHI_STAGE_VERTEX][e->slot] = srv;
            c->st.srvMask[RHI_STAGE_VERTEX] |= 1u << e->slot;
        }
        if ((stages & D11_STAGE_PS) && c->st.srvSet[RHI_STAGE_FRAGMENT][e->slot] != srv) {
            ID3D11DeviceContext_PSSetShaderResources(c->ctx, e->slot, 1, &srv);
            c->st.srvSet[RHI_STAGE_FRAGMENT][e->slot] = srv;
            c->st.srvMask[RHI_STAGE_FRAGMENT] |= 1u << e->slot;
        }
        return;
    }
    case RHI_BIND_SAMPLER: {
        if (e->slot >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) {
            if (d11_once(&s_slotLogged)) {
                D11_LOG("bind group: register %u is past what Direct3D 11 binds (logged once)",
                        e->slot);
            }
            return;
        }
        ID3D11SamplerState *smp = e->smp ? e->smp : g_d11.defaultSampler;
        /* a register already holding the sampler is not set again */
        if ((stages & D11_STAGE_VS) && c->st.smpSet[RHI_STAGE_VERTEX][e->slot] != smp) {
            ID3D11DeviceContext_VSSetSamplers(c->ctx, e->slot, 1, &smp);
            c->st.smpSet[RHI_STAGE_VERTEX][e->slot] = smp;
        }
        if ((stages & D11_STAGE_PS) && c->st.smpSet[RHI_STAGE_FRAGMENT][e->slot] != smp) {
            ID3D11DeviceContext_PSSetSamplers(c->ctx, e->slot, 1, &smp);
            c->st.smpSet[RHI_STAGE_FRAGMENT][e->slot] = smp;
        }
        return;
    }
    default:
        /* the storage texture and the input attachment: not on this backend */
        return;
    }
}

/* The vertex buffers, with the ranges a draw reads from upload buffers:
 * per-vertex bindings [first, first + count) vertices (an indexed draw:
 * from the binding's offset to the end, the indices being unknown here),
 * per-instance bindings the instances. */
static void d11_flush_vertex_buffers(D11CommandList *c, const D11Pipeline *p, bool indexed,
                                     uint32_t first, uint32_t count, uint32_t instances)
{
    for (uint32_t b = 0; b < RHI_MAX_VERTEX_ATTRS; b++) {
        if (!(p->bindingMask & (1u << b)) || !c->st.vb[b]) {
            continue;
        }
        RhiBuffer h = {c->st.vb[b]};
        D11Buffer *buf = d11_get_buffer(h);
        if (!buf || !buf->part[D11_PART_VI]) {
            continue;
        }
        const uint32_t stride = p->strides[b];
        if ((c->st.vbDirty & (1u << b)) || !(c->st.vbSet & (1u << b)) ||
            c->st.vbStrideSet[b] != stride) {
            UINT st = stride, off = (UINT)c->st.vbOffset[b];
            ID3D11DeviceContext_IASetVertexBuffers(c->ctx, b, 1, &buf->part[D11_PART_VI], &st,
                                                   &off);
            c->st.vbStrideSet[b] = stride;
            c->st.vbSet |= 1u << b;
            c->st.vbDirty &= ~(1u << b);
        }
        if (buf->kind != RHI_MEM_UPLOAD) {
            continue;
        }
        const uint64_t base = c->st.vbOffset[b];
        uint64_t lo = base, hi = buf->size;
        if (p->instanceMask & (1u << b)) {
            hi = base + (uint64_t)instances * stride;
        } else if (!indexed) {
            lo = base + (uint64_t)first * stride;
            hi = lo + (uint64_t)count * stride;
        }
        d11_cmd_touch(c, c->st.vb[b], D11_PART_VI, lo, hi);
    }
}

/* The registers an entry writes, OR-ed into regs[stage][b, t, s] (the
 * bind cache's groupRegs: d11_bind_entry's cases) */
static void d11_entry_regs(const D11BindEntry *e, uint32_t regs[2][3])
{
    const uint32_t stages = e->stages ? e->stages : (D11_STAGE_VS | D11_STAGE_PS);
    int kind;
    switch ((RhiBindType)e->type) {
    case RHI_BIND_UNIFORM_BUFFER:
    case RHI_BIND_UNIFORM_BUFFER_DYNAMIC:
        kind = 0;
        break;
    case RHI_BIND_STORAGE_BUFFER:
    case RHI_BIND_SAMPLED_TEXTURE:
        kind = 1;
        break;
    case RHI_BIND_SAMPLER:
        kind = 2;
        break;
    default:
        return;
    }
    if (e->slot >= 32u) {
        return; /* past every register file: d11_bind_entry binds nothing */
    }
    for (uint32_t st = 0; st < 2; st++) {
        if (stages & (1u << st)) {
            regs[st][kind] |= 1u << e->slot;
        }
    }
}

/* Slot g's group just flushed into regs: any other slot whose group had a
 * register among them no longer holds it whole (groupStale: bound again,
 * it is flushed again, as any changed bind is) */
static void d11_group_regs_note(D11CmdState *st, uint32_t g, const uint32_t regs[2][3])
{
    for (uint32_t h = 0; h < RHI_MAX_BIND_SLOTS; h++) {
        if (h == g) {
            continue;
        }
        for (uint32_t k = 0; k < 6u; k++) {
            if (st->groupRegs[h][k / 3u][k % 3u] & regs[k / 3u][k % 3u]) {
                st->groupStale |= 1u << h;
                break;
            }
        }
    }
    memcpy(st->groupRegs[g], regs, sizeof(st->groupRegs[g]));
    st->groupStale &= ~(1u << g);
}

/* Before a draw: the dirty bind groups of the pipeline's layouts and the
 * vertex buffers.  False when no pipeline is bound. */
static D11Pipeline *d11_flush(D11CommandList *c)
{
    D11Pipeline *p = c->st.pipelineSet ? d11_get_pipeline(c->st.pipelineSet) : NULL;
    if (!p) {
        return NULL;
    }
    for (uint32_t g = 0; g < p->layoutCount && c->st.groupDirty; g++) {
        const uint32_t bit = 1u << g;
        if (!(c->st.groupDirty & bit)) {
            continue;
        }
        c->st.groupDirty &= ~bit;
        D11BindGroup *bg = c->st.groups[g] ? d11_get_bind_group(c->st.groups[g]) : NULL;
        if (!bg) {
            /* an unbound group: what an earlier group left in its registers
             * stays (no pipeline reads a group it leaves unbound) */
            continue;
        }
        g_d11.counts.bindGroupBinds++;
        uint32_t regs[2][3] = {{0, 0, 0}, {0, 0, 0}};
        for (uint32_t i = 0; i < bg->count; i++) {
            d11_bind_entry(c, &bg->e[i], c->st.offsets[g]);
            d11_entry_regs(&bg->e[i], regs);
        }
        d11_group_regs_note(&c->st, g, regs);
    }
    return p;
}

/* ------------------------------------------------- the last draw's state
 * A copy of what the draw ran with (under 200 bytes, no call into the
 * runtime), made into text only when asked (rhi_get_stats' lastDrawText):
 * what a self-test that read back its cleared target needs to say which
 * state the draw had (issue 126).  The constant buffer windows of b0..b3,
 * the registers the shaders use (common.hlsli, vu_common.hlsli). */
#define D11_SNAP_CB 4u

typedef struct D11DrawSnap {
    uint64_t index; /* 1-based draw count since rhi_init; 0 none yet */
    bool indexed;
    uint32_t count, instances;
    D3D11_VIEWPORT vp;
    D3D11_RECT sc;
    uint32_t rtW, rtH, dsW, dsH;
    DXGI_FORMAT rtFmt;
    bool dsReadOnly;
    uint32_t vbSet;
    uint8_t stencilRef;
    bool cb[2][D11_SNAP_CB];
    UINT cbFirst[2][D11_SNAP_CB], cbNum[2][D11_SNAP_CB];
    ID3D11BlendState *blend;
    ID3D11DepthStencilState *ds;
    ID3D11RasterizerState *rs;
    D3D11_PRIMITIVE_TOPOLOGY topo;
    bool ps, il;
    uint32_t bindingMask;
} D11DrawSnap;

static D11DrawSnap s_snap;
static char s_snapText[1024];

static void d11_snap(const D11CommandList *c, const D11Pipeline *p, bool indexed, uint32_t count,
                     uint32_t instances)
{
    s_snap.index = g_d11.counts.draws + 1u;
    s_snap.indexed = indexed;
    s_snap.count = count;
    s_snap.instances = instances;
    s_snap.vp = c->st.vp;
    s_snap.sc = c->st.sc;
    s_snap.rtW = c->st.rtW;
    s_snap.rtH = c->st.rtH;
    s_snap.dsW = c->st.dsW;
    s_snap.dsH = c->st.dsH;
    s_snap.rtFmt = c->st.rtFmt;
    s_snap.dsReadOnly = c->st.dsReadOnly;
    s_snap.vbSet = c->st.vbSet;
    s_snap.stencilRef = c->st.stencilRef;
    for (uint32_t st = 0; st < 2; st++) {
        for (uint32_t i = 0; i < D11_SNAP_CB; i++) {
            s_snap.cb[st][i] = c->st.cbSet[st][i] != NULL;
            s_snap.cbFirst[st][i] = c->st.cbFirst[st][i];
            s_snap.cbNum[st][i] = c->st.cbNum[st][i];
        }
    }
    s_snap.blend = p->blend;
    s_snap.ds = p->ds;
    s_snap.rs = p->rs;
    s_snap.topo = p->topo;
    s_snap.ps = p->ps != NULL;
    s_snap.il = p->il != NULL;
    s_snap.bindingMask = p->bindingMask;
}

/* "b0 32@0 b1 16@4096" for a stage's bound windows (constants: count@first) */
static void d11_cb_text(const D11DrawSnap *sn, uint32_t stage, char *out, size_t n)
{
    size_t k = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < D11_SNAP_CB && k + 1 < n; i++) {
        if (!sn->cb[stage][i]) {
            continue;
        }
        const int w = snprintf(out + k, n - k, "%sb%u %u@%u", k ? " " : "", i, sn->cbNum[stage][i],
                               sn->cbFirst[stage][i]);
        if (w < 0 || (size_t)w >= n - k) {
            break;
        }
        k += (size_t)w;
    }
    if (!out[0]) {
        snprintf(out, n, "none");
    }
}

const char *d11_last_draw_text(void)
{
    /* none yet, or one from before an rhi_shutdown (its state objects are
     * gone; the count started again at rhi_init) */
    if (!s_snap.index || !g_d11.initialised || s_snap.index > g_d11.counts.draws) {
        return NULL;
    }
    const D11DrawSnap *st = &s_snap;
    D3D11_BLEND_DESC bd;
    D3D11_DEPTH_STENCIL_DESC dd;
    D3D11_RASTERIZER_DESC rd;
    memset(&bd, 0, sizeof(bd));
    memset(&dd, 0, sizeof(dd));
    memset(&rd, 0, sizeof(rd));
    if (s_snap.blend) {
        ID3D11BlendState_GetDesc(s_snap.blend, &bd);
    }
    if (s_snap.ds) {
        ID3D11DepthStencilState_GetDesc(s_snap.ds, &dd);
    }
    if (s_snap.rs) {
        ID3D11RasterizerState_GetDesc(s_snap.rs, &rd);
    }
    const D3D11_RENDER_TARGET_BLEND_DESC *b0 = &bd.RenderTarget[0];
    char vsCb[160], psCb[160];
    d11_cb_text(st, RHI_STAGE_VERTEX, vsCb, sizeof(vsCb));
    d11_cb_text(st, RHI_STAGE_FRAGMENT, psCb, sizeof(psCb));
    snprintf(
        s_snapText, sizeof(s_snapText),
        "draw %llu (%s, %u x %u): viewport %g,%g %gx%g depth %g..%g; scissor %ld,%ld..%ld,%ld; "
        "target %ux%u format %d, depth %ux%u%s; topology %d, pixel shader %s, input layout %s "
        "(vertex buffers 0x%x bound 0x%x); blend %s %d/%d op %d, mask 0x%x; depth %s write %d "
        "func %d, stencil %s ref %u; cull %d, scissor test %s, depth clip %s; constants "
        "vertex %s, pixel %s; feature level %s, constant buffer offsets yes, partial updates "
        "%s, command lists %s",
        (unsigned long long)s_snap.index, s_snap.indexed ? "indexed" : "plain", s_snap.count,
        s_snap.instances, (double)st->vp.TopLeftX, (double)st->vp.TopLeftY, (double)st->vp.Width,
        (double)st->vp.Height, (double)st->vp.MinDepth, (double)st->vp.MaxDepth, (long)st->sc.left,
        (long)st->sc.top, (long)st->sc.right, (long)st->sc.bottom, st->rtW, st->rtH, (int)st->rtFmt,
        st->dsW, st->dsH, st->dsReadOnly ? " read-only" : "", (int)s_snap.topo,
        s_snap.ps ? "yes" : "none", s_snap.il ? "yes" : "none", s_snap.bindingMask, st->vbSet,
        b0->BlendEnable ? "on" : "off", (int)b0->SrcBlend, (int)b0->DestBlend, (int)b0->BlendOp,
        (unsigned)b0->RenderTargetWriteMask, dd.DepthEnable ? "on" : "off", (int)dd.DepthWriteMask,
        (int)dd.DepthFunc, dd.StencilEnable ? "on" : "off", (unsigned)st->stencilRef,
        (int)rd.CullMode, rd.ScissorEnable ? "on" : "off", rd.DepthClipEnable ? "on" : "off", vsCb,
        psCb,
        g_d11.featureLevel == D3D_FEATURE_LEVEL_11_1   ? "11_1"
        : g_d11.featureLevel == D3D_FEATURE_LEVEL_11_0 ? "11_0"
                                                       : "other",
        g_d11.cbPartialUpdate ? "yes" : "no",
        g_d11.driverCommandLists ? "by the driver" : "emulated by the runtime");
    return s_snapText;
}

void rhi_cmd_draw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                  uint32_t instanceCount)
{
    D11CommandList *c = d11_rec(cl);
    D11Pipeline *p = c ? d11_flush(c) : NULL;
    if (!p) {
        return;
    }
    const uint32_t inst = instanceCount ? instanceCount : 1u;
    d11_flush_vertex_buffers(c, p, false, firstVertex, vertexCount, inst);
    d11_snap(c, p, false, vertexCount, inst);
    ID3D11DeviceContext_DrawInstanced(c->ctx, vertexCount, inst, firstVertex, 0);
    g_d11.counts.draws++;
}

void rhi_cmd_draw_indexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                          int32_t vertexOffset, uint32_t instanceCount)
{
    D11CommandList *c = d11_rec(cl);
    D11Pipeline *p = c ? d11_flush(c) : NULL;
    if (!p) {
        return;
    }
    const uint32_t inst = instanceCount ? instanceCount : 1u;
    d11_flush_vertex_buffers(c, p, true, 0, 0, inst);
    if (c->st.ib) {
        const uint64_t is = c->st.ib32 ? 4u : 2u;
        const uint64_t lo = c->st.ibOffset + (uint64_t)firstIndex * is;
        d11_cmd_touch(c, c->st.ib, D11_PART_VI, lo, lo + (uint64_t)indexCount * is);
    }
    d11_snap(c, p, true, indexCount, inst);
    ID3D11DeviceContext_DrawIndexedInstanced(c->ctx, indexCount, inst, firstIndex, vertexOffset, 0);
    g_d11.counts.draws++;
}

/* ------------------------------------------------------------------ copies */
/* The index of a buffer's first part (the copy source), D11_PART_COUNT for
 * none. */
static uint32_t d11_first_part(const D11Buffer *b)
{
    for (uint32_t i = 0; i < D11_PART_COUNT; i++) {
        if (b->part[i]) {
            return i;
        }
    }
    return D11_PART_COUNT;
}

/* A copy into the structured part (stride 16) must cover whole elements
 * (the debug layer's error 280; without it the runtime drops the copy).
 * The Vulkan backend copies bytes, so the part takes the 16-byte-aligned
 * range around [dstOffset, dstOffset + size) from a part of the same
 * buffer the copy has just written exactly (the parts hold the same bytes,
 * so the bytes around the range stay the buffer's own).  A buffer with no
 * other part takes the aligned range from the source instead, which writes
 * the source's neighbouring bytes into those around the range (logged
 * once).  Every part's size is a multiple of 256, so the aligned end fits. */
static void d11_copy_into_structured(D11CommandList *c, D11Buffer *d, ID3D11Resource *sibling,
                                     ID3D11Resource *from, const D11Buffer *s, uint64_t srcOffset,
                                     uint64_t dstOffset, uint64_t size)
{
    static bool s_noSiblingLogged;
    const uint64_t lo = dstOffset & ~(uint64_t)15u;
    const uint64_t hi = d3dp_align_up(dstOffset + size, 16u);
    if (sibling) {
        D3D11_BOX box = {(UINT)lo, 0, 0, (UINT)hi, 1, 1};
        ID3D11DeviceContext_CopySubresourceRegion(c->ctx, (ID3D11Resource *)d->part[D11_PART_SRV],
                                                  0, (UINT)lo, 0, 0, sibling, 0, &box);
        return;
    }
    const uint64_t head = dstOffset - lo;
    if (srcOffset < head || srcOffset - head + (hi - lo) > s->size) {
        D11_LOG("rhi_cmd_copy_buffer: %llu bytes into %llu of a storage buffer cover part of a "
                "16-byte element and the source has no bytes around them; skipped",
                (unsigned long long)size, (unsigned long long)dstOffset);
        return;
    }
    if (d11_once(&s_noSiblingLogged)) {
        D11_LOG("rhi_cmd_copy_buffer: %llu bytes into %llu of a storage-only buffer cover part of "
                "a 16-byte element: the whole elements are copied from the source (logged once)",
                (unsigned long long)size, (unsigned long long)dstOffset);
    }
    D3D11_BOX box = {(UINT)(srcOffset - head), 0, 0, (UINT)(srcOffset - head + (hi - lo)), 1, 1};
    ID3D11DeviceContext_CopySubresourceRegion(c->ctx, (ID3D11Resource *)d->part[D11_PART_SRV], 0,
                                              (UINT)lo, 0, 0, from, 0, &box);
}

void rhi_cmd_copy_buffer(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset, RhiBuffer dst,
                         uint64_t dstOffset, uint64_t size)
{
    D11CommandList *c = d11_rec(cl);
    D11Buffer *s = d11_get_buffer(src);
    D11Buffer *d = d11_get_buffer(dst);
    if (!c || !s || !d || size == 0) {
        return;
    }
    if (d->kind == RHI_MEM_UPLOAD) {
        D11_LOG("rhi_cmd_copy_buffer: an upload buffer cannot be a copy destination");
        return;
    }
    if (srcOffset > s->size || size > s->size - srcOffset || dstOffset > d->size ||
        size > d->size - dstOffset) {
        D11_LOG("rhi_cmd_copy_buffer: %llu bytes from %llu into %llu pass a buffer's end",
                (unsigned long long)size, (unsigned long long)srcOffset,
                (unsigned long long)dstOffset);
        return;
    }
    const uint32_t sp = d11_first_part(s);
    if (sp == D11_PART_COUNT) {
        D11_LOG("rhi_cmd_copy_buffer: a readback buffer cannot be a copy source");
        return;
    }
    /* an upload source is read from its part, written from the shadow at
     * the submit like every other upload read */
    d11_cmd_touch(c, src.id, sp, srcOffset, srcOffset + size);
    D3D11_BOX box = {(UINT)srcOffset, 0, 0, (UINT)(srcOffset + size), 1, 1};
    ID3D11Resource *from = (ID3D11Resource *)s->part[sp];
    if (d->kind == RHI_MEM_READBACK) {
        ID3D11DeviceContext_CopySubresourceRegion(c->ctx, (ID3D11Resource *)d->staging, 0,
                                                  (UINT)dstOffset, 0, 0, from, 0, &box);
        d->stagingDirty = true;
    } else {
        /* a device buffer: every part it has gets the bytes */
        ID3D11Resource *sibling = NULL; /* a part without the structure, written first */
        for (uint32_t i = 0; i < D11_PART_COUNT; i++) {
            if (!d->part[i]) {
                continue;
            }
            if (i != D11_PART_SRV || (((dstOffset | size) & 15u) == 0)) {
                ID3D11DeviceContext_CopySubresourceRegion(c->ctx, (ID3D11Resource *)d->part[i], 0,
                                                          (UINT)dstOffset, 0, 0, from, 0, &box);
                sibling = sibling ? sibling : (ID3D11Resource *)d->part[i];
                continue;
            }
            d11_copy_into_structured(c, d, sibling, from, s, srcOffset, dstOffset, size);
        }
    }
    g_d11.counts.copies++;
}

void rhi_cmd_copy_buffer_to_texture(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset,
                                    uint32_t rowPitch, RhiTexture dst, uint32_t mip, RhiRect region)
{
    static bool s_srcLogged;
    D11CommandList *c = d11_rec(cl);
    D11Buffer *s = d11_get_buffer(src);
    D11Texture *t = d11_get_texture(dst);
    if (!c || !s || !t || mip >= t->mips || region.w == 0 || region.h == 0 || region.x < 0 ||
        region.y < 0 || t->samples > 1u) {
        return;
    }
    if (s->kind != RHI_MEM_UPLOAD || !s->shadow) {
        /* the renderer uploads from its ring only; a device or readback
         * source has no CPU copy to read */
        if (d11_once(&s_srcLogged)) {
            D11_LOG("rhi_cmd_copy_buffer_to_texture: only an upload buffer can be the source "
                    "on Direct3D 11; skipped (logged once)");
        }
        return;
    }
    /* texture packs: a BC copy covers whole 4x4 blocks, so a level under
     * 4 x 4 (or a region's ragged edge) is rounded up to the block multiple,
     * as on D3D12; the rows are block rows */
    uint32_t fw = region.w, fh = region.h, rows = region.h;
    if (rhi_format_is_block(t->rhiFormat)) {
        fw = (fw + 3u) & ~3u;
        fh = (fh + 3u) & ~3u;
        rows = fh / 4u;
    }
    const uint64_t bytes = (uint64_t)rowPitch * rows;
    if (srcOffset > s->size || bytes > s->size - srcOffset) {
        D11_LOG("rhi_cmd_copy_buffer_to_texture: %u rows of %u bytes from %llu pass the buffer",
                rows, rowPitch, (unsigned long long)srcOffset);
        return;
    }
    const uint8_t *data = s->shadow + srcOffset;
    const UINT sub = d3dp_subresource(mip, 0, t->mips); /* D3D11CalcSubresource */
    if (g_d11.driverCommandLists) {
        D3D11_BOX box = {(UINT)region.x,      (UINT)region.y,      0,
                         (UINT)region.x + fw, (UINT)region.y + fh, 1};
        ID3D11DeviceContext1_UpdateSubresource1(c->ctx1, (ID3D11Resource *)t->res, sub, &box, data,
                                                rowPitch, (UINT)bytes, 0);
    } else {
        /* command lists emulated by the runtime: a destination box with an
         * offset is applied to the source pointer as well on a deferred
         * context (the runtime's documented UpdateSubresource fault), so
         * the rows go into a texture of their own made with them as its
         * contents and are copied from there on the GPU */
        D3D11_TEXTURE2D_DESC td;
        memset(&td, 0, sizeof(td));
        td.Width = fw;
        td.Height = fh;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = t->resFormat;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA init = {data, rowPitch, (UINT)bytes};
        ID3D11Texture2D *tmp = NULL;
        if (!D11_CHECK(ID3D11Device_CreateTexture2D(g_d11.device, &td, &init, &tmp)) || !tmp) {
            return;
        }
        D3D11_BOX box = {0, 0, 0, fw, fh, 1};
        ID3D11DeviceContext_CopySubresourceRegion(c->ctx, (ID3D11Resource *)t->res, sub,
                                                  (UINT)region.x, (UINT)region.y, 0,
                                                  (ID3D11Resource *)tmp, 0, &box);
        d11_defer((IUnknown *)tmp); /* the list keeps it alive until it ran */
    }
    g_d11.counts.copies++;
}

void rhi_cmd_copy_texture(RhiCommandList cl, RhiTexture src, RhiRect srcRegion, RhiTexture dst,
                          int32_t dstX, int32_t dstY)
{
    D11CommandList *c = d11_rec(cl);
    D11Texture *s = d11_get_texture(src);
    D11Texture *d = d11_get_texture(dst);
    if (!c || !s || !d) {
        return;
    }
    if (s->samples > 1u || d->samples > 1u) {
        D11_LOG("rhi_cmd_copy_texture: a multisampled texture is not copied (resolve it)");
        return;
    }
    if (d11_formatMap[s->rhiFormat].depth) {
        /* depth-stencil resources copy whole subresources (both planes) */
        if (srcRegion.x || srcRegion.y || dstX || dstY || srcRegion.w != s->width ||
            srcRegion.h != s->height || s->width != d->width || s->height != d->height) {
            D11_LOG("rhi_cmd_copy_texture: depth formats copy whole textures only");
            return;
        }
        ID3D11DeviceContext_CopySubresourceRegion(c->ctx, (ID3D11Resource *)d->res, 0, 0, 0, 0,
                                                  (ID3D11Resource *)s->res, 0, NULL);
        g_d11.counts.copies++;
        return;
    }
    if (srcRegion.x < 0 || srcRegion.y < 0 || dstX < 0 || dstY < 0) {
        return;
    }
    D3D11_BOX box = {(UINT)srcRegion.x,
                     (UINT)srcRegion.y,
                     0,
                     (UINT)srcRegion.x + srcRegion.w,
                     (UINT)srcRegion.y + srcRegion.h,
                     1};
    ID3D11DeviceContext_CopySubresourceRegion(c->ctx, (ID3D11Resource *)d->res, 0, (UINT)dstX,
                                              (UINT)dstY, 0, (ID3D11Resource *)s->res, 0, &box);
    g_d11.counts.copies++;
}

void rhi_cmd_resolve_color(RhiCommandList cl, RhiTexture src, RhiTexture dst)
{
    D11CommandList *c = d11_rec(cl);
    D11Texture *s = d11_get_texture(src);
    D11Texture *d = d11_get_texture(dst);
    if (!c || !s || !d) {
        return;
    }
    const D11FormatMap *fm = &d11_formatMap[s->rhiFormat];
    if (s->samples <= 1u || d->samples > 1u || s->width != d->width || s->height != d->height ||
        s->rhiFormat != d->rhiFormat || fm->depth || fm->isInteger) {
        D11_LOG("rhi_cmd_resolve_color: needs a multisampled colour texture and a "
                "single-sampled one of its size and format");
        return;
    }
    ID3D11DeviceContext_ResolveSubresource(c->ctx, (ID3D11Resource *)d->res, 0,
                                           (ID3D11Resource *)s->res, 0,
                                           s->swapchain ? s->resFormat : fm->view);
    g_d11.counts.copies++;
}

/* ------------------------------------------- the copy into a storage buffer
 * Direct3D 11 copies a texture into a buffer only through a staging
 * texture the CPU maps (the readback branch below).  The VU vertex
 * pre-pass's record (port/render/rd_replay_vu.c vuRecPassCopy: three
 * RGBA32_UINT planes, 1024 texels a row, into the frame slot's storage
 * buffer, which the next pass's vertex shaders and the main ones read as
 * StructuredBuffer<uint4> vu_rec_buf) needs the copy on the GPU, ordered
 * between two passes of one list.  So a copy into a storage-only device
 * buffer is this compute shader (cs_5_0, feature level 11_0, which
 * rhi_init requires): a texel of the region a thread, Load from the
 * texture's SRV into the buffer's UAV (d3d11_resource.c: an RGBA32_UINT
 * copy source has an SRV, a storage-only device buffer a structured UAV of
 * 16-byte elements), element first + row * pitch + column.  Its limits,
 * each refused with its line: an RGBA32_UINT single-sampled source, a
 * storage-only device destination, dstOffset and rowPitch whole 16-byte
 * elements (the structured part's rule, d11_copy_into_structured), outside
 * a render pass.  The runtime orders the dispatch after every earlier draw
 * that read the buffer and before every later read; a resource bound as a
 * UAV and as an SRV at once is not allowed, so every SRV the list bound is
 * unbound first (the groups are bound again at the next draw, as at a
 * pass's start) and the copy's SRV and UAV are unbound after it. */
static const char s_copyCsHlsl[] =
    "// d3d11_cmd.c: a texture region into a structured buffer\n"
    "Texture2D<uint4> g_src : register(t0);\n"
    "RWStructuredBuffer<uint4> g_dst : register(u0);\n"
    "cbuffer CopyCB : register(b0) {\n"
    "    uint4 g_rect; // the region: x, y, w, h\n"
    "    uint4 g_to;   // the first element, the row pitch in elements\n"
    "};\n"
    "[numthreads(64, 1, 1)]\n"
    "void cs_copy(uint3 id : SV_DispatchThreadID) {\n"
    "    if (id.x >= g_rect.z || id.y >= g_rect.w) {\n"
    "        return;\n"
    "    }\n"
    "    g_dst[g_to.x + id.y * g_to.y + id.x] =\n"
    "        g_src.Load(int3(int(g_rect.x + id.x), int(g_rect.y + id.y), 0));\n"
    "}\n";

#define D11_COPY_CS_GROUP 64u

/* The shader and its constant buffer, made once (any thread: the first
 * copy's); false when they could not be (g_d11.copyCsWhy, logged once) */
static bool d11_copy_cs_ready(void)
{
    const int st = __atomic_load_n(&g_d11.copyCsState, __ATOMIC_ACQUIRE);
    if (st != 0) {
        return st == 1;
    }
    AcquireSRWLockExclusive(&g_d11.copyCsLock);
    if (g_d11.copyCsState != 0) {
        const bool ok = g_d11.copyCsState == 1;
        ReleaseSRWLockExclusive(&g_d11.copyCsLock);
        return ok;
    }
    const uint64_t t0 = d11_now_ns();
    char err[256];
    void *dxbc = NULL;
    size_t len = 0;
    bool ok =
        d3d11_shader_compile_text("d3d11_copy_cs.hlsl", s_copyCsHlsl, sizeof(s_copyCsHlsl) - 1u,
                                  "cs_copy", "cs_5_0", &dxbc, &len, err, sizeof(err));
    if (!ok) {
        snprintf(g_d11.copyCsWhy, sizeof(g_d11.copyCsWhy), "%s", err);
    } else if (!D11_CHECK(ID3D11Device_CreateComputeShader(g_d11.device, dxbc, len, NULL,
                                                           &g_d11.copyCs)) ||
               !g_d11.copyCs) {
        g_d11.copyCs = NULL;
        snprintf(g_d11.copyCsWhy, sizeof(g_d11.copyCsWhy),
                 "CreateComputeShader refused its %zu bytes of DXBC", len);
        ok = false;
    } else {
        D3D11_BUFFER_DESC bd;
        memset(&bd, 0, sizeof(bd));
        bd.ByteWidth = 32;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (!D11_CHECK(ID3D11Device_CreateBuffer(g_d11.device, &bd, NULL, &g_d11.copyCb)) ||
            !g_d11.copyCb) {
            g_d11.copyCb = NULL;
            D11_RELEASE(g_d11.copyCs);
            snprintf(g_d11.copyCsWhy, sizeof(g_d11.copyCsWhy),
                     "its 32-byte constant buffer could not be made");
            ok = false;
        }
    }
    free(dxbc);
    if (ok) {
        d11_set_name((ID3D11DeviceChild *)g_d11.copyCs, "rhi_d3d11 texture-to-buffer copy");
        D11_LOG("texture-to-buffer copies into storage buffers: a compute shader (cs_5_0, %u "
                "texels a group, made in %.1f ms; the VU vertex pre-pass's record)",
                D11_COPY_CS_GROUP, (double)(d11_now_ns() - t0) / 1e6);
    } else {
        D11_LOG("texture-to-buffer copies into storage buffers: the compute shader could not be "
                "made (%s): every such copy is skipped, and the VU vertex pre-pass's start-up "
                "self-test turns the pre-pass off",
                g_d11.copyCsWhy);
    }
    __atomic_store_n(&g_d11.copyCsState, ok ? 1 : 2, __ATOMIC_RELEASE);
    ReleaseSRWLockExclusive(&g_d11.copyCsLock);
    return ok;
}

void d11_copy_cs_release(void)
{
    D11_RELEASE(g_d11.copyCb);
    D11_RELEASE(g_d11.copyCs);
    g_d11.copyCsState = 0;
}

/* A copy into a buffer the backend cannot make: the first 8 lines, then
 * every 100th (a refused record copy repeats every replay) */
static void d11_copy_refused(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void d11_copy_refused(const char *fmt, ...)
{
    static uint32_t s_n;
    const uint32_t n = __atomic_add_fetch(&s_n, 1u, __ATOMIC_RELAXED);
    if (n > 8u && n % 100u != 0u) {
        return;
    }
    char text[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    D11_LOG("rhi_cmd_copy_texture_to_buffer: %s; skipped (%u so far%s)", text, (unsigned)n,
            n == 8u ? "; from here every 100th is written" : "");
}

static void d11_copy_texture_to_storage(D11CommandList *c, D11Texture *t, RhiRect region,
                                        D11Buffer *d, uint64_t dstOffset, uint32_t rowPitch)
{
    static bool s_firstLogged;
    if (c->st.inPass) {
        d11_copy_refused("inside a render pass");
        return;
    }
    if (t->rhiFormat != RHI_FMT_RGBA32_UINT || t->samples > 1u || !t->srv) {
        d11_copy_refused("into a device buffer only from a single-sampled RGBA32_UINT texture "
                         "made with RHI_TEX_COPY_SRC on Direct3D 11 (format %d, %u samples)",
                         (int)t->rhiFormat, t->samples);
        return;
    }
    if (!d->uav) {
        d11_copy_refused("into a device buffer only when it is storage-only "
                         "(RHI_BUF_STORAGE_READ | RHI_BUF_COPY_DST) on Direct3D 11");
        return;
    }
    if (((dstOffset | rowPitch) & 15u) != 0u) {
        d11_copy_refused("offset %llu and row pitch %u are not whole 16-byte elements of the "
                         "storage buffer",
                         (unsigned long long)dstOffset, rowPitch);
        return;
    }
    const uint64_t rowBytes = (uint64_t)region.w * 16u;
    if ((uint64_t)region.x + region.w > t->width || (uint64_t)region.y + region.h > t->height ||
        rowPitch < rowBytes || dstOffset > d->size ||
        (uint64_t)(region.h - 1u) * rowPitch + rowBytes > d->size - dstOffset ||
        region.h > D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION) {
        d11_copy_refused("%ux%u texels at (%d,%d) of a %ux%u texture into %llu of a %llu-byte "
                         "buffer, rows %u bytes apart, do not fit",
                         region.w, region.h, region.x, region.y, t->width, t->height,
                         (unsigned long long)dstOffset, (unsigned long long)d->size, rowPitch);
        return;
    }
    if (!d11_copy_cs_ready()) {
        d11_copy_refused("the compute shader is missing (%s)", g_d11.copyCsWhy);
        return;
    }
    d11_unbind_srvs(c);
    const uint32_t words[8] = {(uint32_t)region.x,          (uint32_t)region.y, region.w, region.h,
                               (uint32_t)(dstOffset / 16u), rowPitch / 16u,     0u,       0u};
    ID3D11DeviceContext_UpdateSubresource(c->ctx, (ID3D11Resource *)g_d11.copyCb, 0, NULL, words, 0,
                                          0);
    ID3D11DeviceContext_CSSetShader(c->ctx, g_d11.copyCs, NULL, 0);
    ID3D11DeviceContext_CSSetConstantBuffers(c->ctx, 0, 1, &g_d11.copyCb);
    ID3D11DeviceContext_CSSetShaderResources(c->ctx, 0, 1, &t->srv);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(c->ctx, 0, 1, &d->uav, NULL);
    ID3D11DeviceContext_Dispatch(c->ctx, (region.w + D11_COPY_CS_GROUP - 1u) / D11_COPY_CS_GROUP,
                                 region.h, 1);
    ID3D11ShaderResourceView *noSrv = NULL;
    ID3D11UnorderedAccessView *noUav = NULL;
    ID3D11DeviceContext_CSSetShaderResources(c->ctx, 0, 1, &noSrv);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(c->ctx, 0, 1, &noUav, NULL);
    g_d11.counts.copies++;
    __atomic_add_fetch(&g_d11.copyCsCount, 1u, __ATOMIC_RELAXED);
    if (d11_once(&s_firstLogged)) {
        D11_LOG("texture-to-buffer copy by the compute shader: the first, %ux%u texels at (%d,%d) "
                "into byte %llu, rows %u bytes apart (logged once)",
                region.w, region.h, region.x, region.y, (unsigned long long)dstOffset, rowPitch);
    }
}

/* Direct3D 11 has no copy from a texture into a buffer.  Into a readback
 * buffer (the crack filler's readback, rd_crack_fill.c) the region goes to
 * a staging texture, whose rows land in the buffer's shadow at the next
 * rhi_map_buffer (d11_readback_texture_target); into a storage-only device
 * buffer (the VU vertex pre-pass's record) it is the compute shader above;
 * into any other buffer the copy is skipped with a line. */
void rhi_cmd_copy_texture_to_buffer(RhiCommandList cl, RhiTexture src, RhiViewAspect aspect,
                                    RhiRect region, RhiBuffer dst, uint64_t dstOffset,
                                    uint32_t rowPitch)
{
    D11CommandList *c = d11_rec(cl);
    D11Texture *t = d11_get_texture(src);
    D11Buffer *d = d11_get_buffer(dst);
    (void)aspect; /* depth formats copy their depth (the staging map takes the float) */
    if (!c || !t || !d || region.w == 0 || region.h == 0 || region.x < 0 || region.y < 0) {
        return;
    }
    if (d->kind != RHI_MEM_READBACK) {
        d11_copy_texture_to_storage(c, t, region, d, dstOffset, rowPitch);
        return;
    }
    if (t->samples > 1u) {
        D11_LOG("rhi_cmd_copy_texture_to_buffer: a multisampled texture is not copied (resolve "
                "it)");
        return;
    }
    const bool depth = d11_formatMap[t->rhiFormat].depth;
    const bool whole =
        region.x == 0 && region.y == 0 && region.w == t->width && region.h == t->height;
    if (depth && !whole) {
        D11_LOG("rhi_cmd_copy_texture_to_buffer: depth formats copy whole textures only");
        return;
    }
    ID3D11Texture2D *staging =
        d11_readback_texture_target(d, t, region.w, region.h, dstOffset, rowPitch);
    if (!staging) {
        return; /* logged there */
    }
    D3D11_BOX box = {
        (UINT)region.x, (UINT)region.y, 0, (UINT)region.x + region.w, (UINT)region.y + region.h, 1};
    ID3D11DeviceContext_CopySubresourceRegion(c->ctx, (ID3D11Resource *)staging, 0, 0, 0, 0,
                                              (ID3D11Resource *)t->res, 0, depth ? NULL : &box);
    g_d11.counts.copies++;
}
