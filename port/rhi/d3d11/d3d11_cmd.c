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
#include <stdlib.h>
#include <string.h>

#define D11_STAGE_VS (1u << RHI_STAGE_VERTEX)
#define D11_STAGE_PS (1u << RHI_STAGE_FRAGMENT)
#define D11_GROUPS_ALL ((1u << RHI_MAX_BIND_SLOTS) - 1u)

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
    /* the store ops are ignored: Direct3D 11 keeps what a pass drew, a valid
     * DONT_CARE (rhi.h) */
    ID3D11DeviceContext_OMSetRenderTargets(c->ctx, pass->colorCount, pass->colorCount ? rtv : NULL,
                                           dsv);
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        const RhiColorAttachment *a = &pass->color[i];
        if (a->load == RHI_LOAD_DONT_CARE) {
            ID3D11DeviceContext1_DiscardView(c->ctx1, (ID3D11View *)rtv[i]);
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
                   (!stencil || pass->depth.stencilLoad == RHI_LOAD_DONT_CARE)) {
            /* both aspects undefined: the whole view may be discarded */
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
    ID3D11DeviceContext_RSSetViewports(c->ctx, 1, &vp);
}

void rhi_cmd_set_scissor(RhiCommandList cl, const RhiRect *r)
{
    D11CommandList *c = d11_rec(cl);
    if (!c || !r) {
        return;
    }
    D3D11_RECT sc = {r->x, r->y, r->x + (LONG)r->w, r->y + (LONG)r->h};
    ID3D11DeviceContext_RSSetScissorRects(c->ctx, 1, &sc);
}

/* ------------------------------------------------------------------ state */
void rhi_cmd_set_pipeline(RhiCommandList cl, RhiPipeline p)
{
    D11CommandList *c = d11_rec(cl);
    D11Pipeline *pp = d11_get_pipeline(p.id);
    if (!c || !pp) {
        return;
    }
    c->st.pipeline = p.id;
    if (c->st.pipelineSet == p.id) {
        return;
    }
    ID3D11DeviceContext_IASetInputLayout(c->ctx, pp->il);
    ID3D11DeviceContext_IASetPrimitiveTopology(c->ctx, pp->topo);
    ID3D11DeviceContext_VSSetShader(c->ctx, pp->vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(c->ctx, pp->ps, NULL, 0);
    ID3D11DeviceContext_RSSetState(c->ctx, pp->rs);
    ID3D11DeviceContext_OMSetBlendState(c->ctx, pp->blend, c->st.blendConstant, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(c->ctx, pp->ds, c->st.stencilRef);
    c->st.pipelineSet = p.id;
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
    memset(c->st.offsets[group], 0, sizeof(c->st.offsets[group]));
    for (uint32_t i = 0; i < count && offsets; i++) {
        c->st.offsets[group][i] = offsets[i];
    }
    c->st.groups[group] = bg.id;
    c->st.groupDirty |= 1u << group;
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
    c->st.stencilRef = ref;
    D11Pipeline *p = c->st.pipelineSet ? d11_get_pipeline(c->st.pipelineSet) : NULL;
    if (p) {
        ID3D11DeviceContext_OMSetDepthStencilState(c->ctx, p->ds, ref);
    }
}

void rhi_cmd_set_blend_constant(RhiCommandList cl, const float rgba[4])
{
    D11CommandList *c = d11_rec(cl);
    if (!c || !rgba) {
        return;
    }
    memcpy(c->st.blendConstant, rgba, sizeof(c->st.blendConstant));
    D11Pipeline *p = c->st.pipelineSet ? d11_get_pipeline(c->st.pipelineSet) : NULL;
    if (p) {
        ID3D11DeviceContext_OMSetBlendState(c->ctx, p->blend, c->st.blendConstant, 0xFFFFFFFFu);
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
            ID3D11DeviceContext1_VSSetConstantBuffers1(c->ctx1, e->slot, 1, &cb, &first, &num);
        }
        if (stages & D11_STAGE_PS) {
            ID3D11DeviceContext1_PSSetConstantBuffers1(c->ctx1, e->slot, 1, &cb, &first, &num);
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
        if (stages & D11_STAGE_VS) {
            ID3D11DeviceContext_VSSetShaderResources(c->ctx, e->slot, 1, &srv);
            c->st.srvMask[RHI_STAGE_VERTEX] |= 1u << e->slot;
        }
        if (stages & D11_STAGE_PS) {
            ID3D11DeviceContext_PSSetShaderResources(c->ctx, e->slot, 1, &srv);
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
        if (stages & D11_STAGE_VS) {
            ID3D11DeviceContext_VSSetSamplers(c->ctx, e->slot, 1, &smp);
        }
        if (stages & D11_STAGE_PS) {
            ID3D11DeviceContext_PSSetSamplers(c->ctx, e->slot, 1, &smp);
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
        for (uint32_t i = 0; i < bg->count; i++) {
            d11_bind_entry(c, &bg->e[i], c->st.offsets[g]);
        }
    }
    return p;
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
        for (uint32_t i = 0; i < D11_PART_COUNT; i++) {
            if (d->part[i]) {
                ID3D11DeviceContext_CopySubresourceRegion(c->ctx, (ID3D11Resource *)d->part[i], 0,
                                                          (UINT)dstOffset, 0, 0, from, 0, &box);
            }
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

/* Direct3D 11 has no copy from a texture into a buffer.  Into a readback
 * buffer (the crack filler's readback, rd_crack_fill.c) the region goes to
 * a staging texture, whose rows land in the buffer's shadow at the next
 * rhi_map_buffer (d11_readback_texture_target); into any other buffer (the
 * vertex pre-pass, off on this backend: colorBytesPerPixel 32) the copy is
 * skipped and logged once. */
void rhi_cmd_copy_texture_to_buffer(RhiCommandList cl, RhiTexture src, RhiViewAspect aspect,
                                    RhiRect region, RhiBuffer dst, uint64_t dstOffset,
                                    uint32_t rowPitch)
{
    static bool s_otherLogged;
    D11CommandList *c = d11_rec(cl);
    D11Texture *t = d11_get_texture(src);
    D11Buffer *d = d11_get_buffer(dst);
    (void)aspect; /* depth formats copy their depth (the staging map takes the float) */
    if (!c || !t || !d || region.w == 0 || region.h == 0 || region.x < 0 || region.y < 0) {
        return;
    }
    if (d->kind != RHI_MEM_READBACK) {
        if (d11_once(&s_otherLogged)) {
            D11_LOG("rhi_cmd_copy_texture_to_buffer: only into a readback buffer on Direct3D 11; "
                    "skipped (logged once)");
        }
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
