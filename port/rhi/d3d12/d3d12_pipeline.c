/* d3d12_pipeline.c: bind group layouts, root signatures, transient bind
 * groups and pipeline state objects for the D3D12 backend. */
#include "d3d12_internal.h"
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------- bind group layouts */
RhiBindGroupLayout rhi_create_bind_group_layout(const RhiBindGroupLayoutDesc *desc)
{
    RhiBindGroupLayout out = {0};
    if (!desc) {
        return out;
    }
    D3dpLayout l;
    if (!d3dp_layout_build(desc->slots, desc->slotCount, &l)) {
        DX_LOG("bind group layout %s: invalid", desc->debugName ? desc->debugName : "?");
        return out;
    }
    DxLayout *dl = NULL;
    uint32_t id = d3dp_pool_alloc(&g_dx.layouts, (void **)&dl);
    if (!id) {
        return out;
    }
    dl->l = l;
    out.id = id;
    return out;
}

void rhi_destroy_bind_group_layout(RhiBindGroupLayout h)
{
    /* root signatures copied what they need; nothing on the GPU */
    d3dp_pool_release(&g_dx.layouts, h.id);
}

/* ------------------------------------------------------- root signatures
 * One root signature per distinct list of layouts (cached; rd_core has a
 * handful): group g is register space g, as descriptor tables. */
static D3D12_SHADER_VISIBILITY dx_visibility(uint32_t stages)
{
    const uint32_t vs = 1u << RHI_STAGE_VERTEX, fs = 1u << RHI_STAGE_FRAGMENT;
    if ((stages & (vs | fs)) == vs) {
        return D3D12_SHADER_VISIBILITY_VERTEX;
    }
    if ((stages & (vs | fs)) == fs) {
        return D3D12_SHADER_VISIBILITY_PIXEL;
    }
    return D3D12_SHADER_VISIBILITY_ALL;
}

static DxRootSig *dx_root_signature(const RhiBindGroupLayout *layouts, uint32_t count)
{
    for (uint32_t i = 0; i < g_dx.rootCount; i++) {
        DxRootSig *r = &g_dx.roots[i];
        bool same = r->layoutCount == count;
        for (uint32_t g = 0; same && g < count; g++) {
            same = r->layoutIds[g] == layouts[g].id;
        }
        if (same) {
            return r;
        }
    }
    if (g_dx.rootCount == DX_MAX_ROOTSIGS) {
        DX_LOG("more than %d distinct pipeline layouts", DX_MAX_ROOTSIGS);
        return NULL;
    }
    const D3dpLayout *ls[RHI_MAX_BIND_SLOTS] = {0};
    for (uint32_t g = 0; g < count; g++) {
        DxLayout *dl = d3dp_pool_get(&g_dx.layouts, layouts[g].id);
        if (!dl) {
            DX_LOG("pipeline: invalid bind group layout %u", g);
            return NULL;
        }
        ls[g] = &dl->l;
    }
    DxRootSig *r = &g_dx.roots[g_dx.rootCount];
    memset(r, 0, sizeof(*r));
    uint32_t np = d3dp_root_params(ls, count, r->resParam, r->smpParam, r->dynParam);

    D3D12_ROOT_PARAMETER params[D3DP_MAX_ROOT_PARAMS];
    D3D12_DESCRIPTOR_RANGE ranges[RHI_MAX_BIND_SLOTS][D3DP_MAX_SLOTS];
    memset(params, 0, sizeof(params));
    for (uint32_t g = 0; g < count; g++) {
        const D3dpLayout *l = ls[g];
        uint32_t nr[2] = {0, 0};
        D3D12_DESCRIPTOR_RANGE *rr[2] = {&ranges[g][0], &ranges[g][l->resCount]};
        for (uint32_t i = 0; i < l->slotCount; i++) {
            const RhiBindSlot *s = &l->slots[i];
            if (l->table[i] == D3DP_ROOT_CBV) {
                /* a root CBV; its address comes at bind time */
                D3D12_ROOT_PARAMETER *p = &params[r->dynParam[g] + l->offset[i]];
                p->ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                p->Descriptor.ShaderRegister = s->slot;
                p->Descriptor.RegisterSpace = g;
                p->ShaderVisibility = dx_visibility(
                    s->stages ? s->stages
                              : ((1u << RHI_STAGE_VERTEX) | (1u << RHI_STAGE_FRAGMENT)));
                continue;
            }
            D3D12_DESCRIPTOR_RANGE *dr = &rr[l->table[i]][nr[l->table[i]]++];
            memset(dr, 0, sizeof(*dr));
            switch (s->type) {
            case RHI_BIND_UNIFORM_BUFFER:
                dr->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                break;
            case RHI_BIND_SAMPLER:
                dr->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                break;
            case RHI_BIND_STORAGE_TEXTURE:
                /* the destination read's ROV (u register) */
                dr->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                break;
            default:
                dr->RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                break;
            }
            dr->NumDescriptors = 1;
            dr->BaseShaderRegister = s->slot;
            dr->RegisterSpace = g;
            dr->OffsetInDescriptorsFromTableStart = l->offset[i];
        }
        if (r->resParam[g] >= 0) {
            D3D12_ROOT_PARAMETER *p = &params[r->resParam[g]];
            p->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p->DescriptorTable.NumDescriptorRanges = l->resCount;
            p->DescriptorTable.pDescriptorRanges = rr[0];
            p->ShaderVisibility = dx_visibility(l->resStages);
        }
        if (r->smpParam[g] >= 0) {
            D3D12_ROOT_PARAMETER *p = &params[r->smpParam[g]];
            p->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p->DescriptorTable.NumDescriptorRanges = l->smpCount;
            p->DescriptorTable.pDescriptorRanges = rr[1];
            p->ShaderVisibility = dx_visibility(l->smpStages);
        }
    }
    D3D12_ROOT_SIGNATURE_DESC rd;
    memset(&rd, 0, sizeof(rd));
    rd.NumParameters = np;
    rd.pParameters = np ? params : NULL;
    rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob *blob = NULL, *err = NULL;
    HRESULT hr = g_dx.serializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(hr)) {
        DX_LOG("D3D12SerializeRootSignature: %s",
               err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "failed");
        if (err) {
            ID3D10Blob_Release(err);
        }
        return NULL;
    }
    hr = ID3D12Device_CreateRootSignature(g_dx.device, 0, ID3D10Blob_GetBufferPointer(blob),
                                          ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                          (void **)&r->rs);
    ID3D10Blob_Release(blob);
    if (err) {
        ID3D10Blob_Release(err);
    }
    if (!DX_CHECK(hr)) {
        return NULL;
    }
    r->layoutCount = count;
    for (uint32_t g = 0; g < count; g++) {
        r->layoutIds[g] = layouts[g].id;
        r->dynCount[g] = (uint8_t)ls[g]->dynCount;
    }
    g_dx.rootCount++;
    return r;
}

void dx_release_root_signatures(void)
{
    for (uint32_t i = 0; i < g_dx.rootCount; i++) {
        DX_RELEASE(g_dx.roots[i].rs);
    }
    g_dx.rootCount = 0;
}

/* ---------------------------------------------------- transient bind groups
 * Descriptors are written straight into the frame slot's region of the
 * shader-visible heaps; the region is reset when the slot is recycled
 * (rhi_wait_frame).  The handle id is (frame tag << 20) | (index + 1). */
static uint32_t dx_frame_tag(void)
{
    return (uint32_t)(g_dx.frameIndex & D3DP_GEN_MASK);
}

DxBindGroup *dx_get_bind_group(uint32_t id)
{
    DxFrame *f = dx_cur_frame();
    uint32_t idx = id & D3DP_INDEX_MASK;
    if (idx == 0 || idx > f->groupCount || (id >> D3DP_GEN_SHIFT) != dx_frame_tag()) {
        return NULL;
    }
    return &f->groups[idx - 1];
}

static const RhiBinding *dx_find_binding(const RhiBindGroupDesc *d, const RhiBindSlot *s)
{
    for (uint32_t i = 0; i < d->bindingCount; i++) {
        if (d->bindings[i].slot == s->slot && d->bindings[i].type == s->type) {
            return &d->bindings[i];
        }
    }
    return NULL;
}

static void dx_null_view(const RhiBindSlot *s, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    if (s->type == RHI_BIND_UNIFORM_BUFFER) {
        ID3D12Device_CreateConstantBufferView(g_dx.device, NULL, h);
        return;
    }
    if (s->type == RHI_BIND_STORAGE_TEXTURE) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC u;
        memset(&u, 0, sizeof(u));
        u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(g_dx.device, NULL, NULL, &u, h);
        return;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC v;
    memset(&v, 0, sizeof(v));
    v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (s->type == RHI_BIND_STORAGE_BUFFER) {
        v.Format = DXGI_FORMAT_UNKNOWN;
        v.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        v.Buffer.StructureByteStride = 16;
    } else {
        v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Texture2D.MipLevels = 1;
    }
    ID3D12Device_CreateShaderResourceView(g_dx.device, NULL, &v, h);
}

/* Writes one resource-table descriptor; false when the binding is unusable
 * (a null view is written instead). */
static bool dx_write_resource(const RhiBindSlot *s, const RhiBinding *b,
                              D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    switch (s->type) {
    case RHI_BIND_UNIFORM_BUFFER: {
        DxBuffer *buf = dx_get_buffer(b->buffer);
        uint32_t size = buf ? d3dp_cbv_size(b->offset, b->size, buf->size) : 0;
        if (!size) {
            DX_LOG("bind group: b%u: invalid buffer or range (offset %llu, size %llu)", s->slot,
                   (unsigned long long)b->offset, (unsigned long long)b->size);
            return false;
        }
        D3D12_CONSTANT_BUFFER_VIEW_DESC v = {buf->gpu + b->offset, size};
        ID3D12Device_CreateConstantBufferView(g_dx.device, &v, h);
        return true;
    }
    case RHI_BIND_STORAGE_BUFFER: {
        /* StructuredBuffer<float4> (vu_common.hlsli): 16-byte elements */
        DxBuffer *buf = dx_get_buffer(b->buffer);
        /* the offset is checked first: past the end, buf->size - b->offset
           wraps and a huge offset + size would pass the bound */
        uint64_t size = 0;
        if (buf && b->offset < buf->size) {
            size = b->size ? b->size : buf->size - b->offset;
        }
        if (!buf || (b->offset & 15u) || size < 16 || size > buf->size - b->offset) {
            DX_LOG("bind group: t%u: invalid storage buffer range", s->slot);
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC v;
        memset(&v, 0, sizeof(v));
        v.Format = DXGI_FORMAT_UNKNOWN;
        v.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Buffer.FirstElement = b->offset / 16u;
        v.Buffer.NumElements = (UINT)(size / 16u);
        v.Buffer.StructureByteStride = 16;
        ID3D12Device_CreateShaderResourceView(g_dx.device, buf->res, &v, h);
        return true;
    }
    case RHI_BIND_SAMPLED_TEXTURE: {
        DxTexture *t = dx_get_texture(b->texture);
        if (!t) {
            DX_LOG("bind group: t%u: invalid texture", s->slot);
            return false;
        }
        const DxFormatMap *fm = &dx_formatMap[t->rhiFormat];
        D3D12_SHADER_RESOURCE_VIEW_DESC v;
        memset(&v, 0, sizeof(v));
        /* depth formats are always viewed as depth (plane 0), whatever the
         * aspect: stencil is never sampled (rhi.h) */
        v.Format = t->swapchain ? t->resFormat : fm->view;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (t->samples > 1u) {
            /* Texture2DMS in the shader (Load at a sample) */
            v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        } else {
            v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            v.Texture2D.MipLevels = t->mips;
        }
        ID3D12Device_CreateShaderResourceView(g_dx.device, t->res, &v, h);
        return true;
    }
    case RHI_BIND_STORAGE_TEXTURE: {
        /* the destination read: the target as a typed RGBA8 UNORM UAV, in
         * UNORDERED_ACCESS (RHI_STATE_STORAGE); the shader declares it a
         * RasterizerOrderedTexture2D */
        DxTexture *t = dx_get_texture(b->texture);
        if (!t || !t->uav) {
            DX_LOG("bind group: u%u: invalid storage texture (not made with RHI_TEX_STORAGE)",
                   s->slot);
            return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC u;
        memset(&u, 0, sizeof(u));
        u.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(g_dx.device, t->res, NULL, &u, h);
        return true;
    }
    default:
        return false;
    }
}

static RhiBindGroup dx_create_bind_group(const RhiBindGroupDesc *desc, bool quiet);

uint32_t dx_null_bind_group(uint32_t layoutId)
{
    uint32_t slot = (layoutId & D3DP_INDEX_MASK) - 1u;
    if (!d3dp_pool_get(&g_dx.layouts, layoutId) || slot >= DX_MAX_LAYOUTS) {
        return 0;
    }
    DxFrame *f = dx_cur_frame();
    if (!dx_get_bind_group(f->nullGroups[slot])) {
        RhiBindGroupDesc d = {{layoutId}, NULL, 0};
        f->nullGroups[slot] = dx_create_bind_group(&d, true).id;
    }
    return f->nullGroups[slot];
}

RhiBindGroup rhi_create_bind_group(const RhiBindGroupDesc *desc)
{
    return dx_create_bind_group(desc, false);
}

static RhiBindGroup dx_create_bind_group(const RhiBindGroupDesc *desc, bool quiet)
{
    RhiBindGroup out = {0};
    DxLayout *dl = desc ? d3dp_pool_get(&g_dx.layouts, desc->layout.id) : NULL;
    if (!dl) {
        return out;
    }
    const D3dpLayout *l = &dl->l;
    DxFrame *f = dx_cur_frame();
    if (f->groupCount == f->groupCap) {
        uint32_t cap = f->groupCap ? f->groupCap * 2u : 1024u;
        DxBindGroup *g = realloc(f->groups, cap * sizeof(*g));
        if (!g) {
            return out;
        }
        f->groups = g;
        f->groupCap = cap;
    }
    if (f->groupCount >= D3DP_INDEX_MASK) {
        return out;
    }
    DxBindGroup bg;
    memset(&bg, 0, sizeof(bg));
    bg.layoutId = desc->layout.id;
    uint32_t resBase = 0, smpBase = 0;
    if (l->resCount && !d3dp_ring_alloc(&f->resRing, l->resCount, &resBase)) {
        DX_LOG("CBV/SRV descriptor ring full for this frame (%u)", f->resRing.size);
        return out;
    }
    /* one sampler: its persistent descriptor; several: a table in the ring */
    bool ringSamplers = l->smpCount > 1;
    if (ringSamplers && !d3dp_ring_alloc(&f->smpRing, l->smpCount, &smpBase)) {
        DX_LOG("sampler descriptor ring full for this frame (%u)", f->smpRing.size);
        return out;
    }
    bg.dynCount = l->dynCount;
    for (uint32_t i = 0; i < l->slotCount; i++) {
        const RhiBindSlot *s = &l->slots[i];
        const RhiBinding *b = dx_find_binding(desc, s);
        if (l->table[i] == D3DP_ROOT_CBV) {
            /* the base address; a root CBV has no size and no
             * null form, so a missing or bad binding leaves 0, which the
             * shader must not read (rd_core binds every one) */
            DxBuffer *buf = b ? dx_get_buffer(b->buffer) : NULL;
            if (buf && (b->offset & 255u) == 0 && b->size && b->size <= 65536u &&
                b->offset + b->size <= buf->size) {
                bg.dyn[l->offset[i]] = buf->gpu + b->offset;
            } else if (!quiet) {
                DX_LOG("bind group: b%u: invalid dynamic uniform buffer or range", s->slot);
            }
            continue;
        }
        if (l->table[i] == D3DP_TABLE_SAMPLER) {
            DxSampler *smp = b ? d3dp_pool_get(&g_dx.samplers, b->sampler.id) : NULL;
            if (!smp || smp->dead) {
                if (!quiet) {
                    DX_LOG("bind group: s%u: invalid sampler", s->slot);
                }
                smp = NULL;
            }
            if (!ringSamplers) {
                /* a missing sampler binds the default (point, clamp) */
                bg.smp = dx_gpu(g_dx.smpGpu, g_dx.smpInc, smp ? smp->heapIndex : DX_SMP_DEFAULT);
                bg.hasSmp = true;
            } else {
                D3D12_SAMPLER_DESC d = smp ? smp->desc : (D3D12_SAMPLER_DESC){0};
                if (!smp) {
                    d.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
                    d.AddressU = d.AddressV = d.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    d.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
                    d.MaxAnisotropy = 1;
                }
                ID3D12Device_CreateSampler(
                    g_dx.device, &d, dx_cpu(g_dx.smpCpu, g_dx.smpInc, smpBase + l->offset[i]));
            }
            continue;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE h = dx_cpu(g_dx.resCpu, g_dx.resInc, resBase + l->offset[i]);
        if (!b || !dx_write_resource(s, b, h)) {
            /* a null view reads zeros; a slot the shader never reads
             * (a layout shared by several entry points) is harmless, so
             * the first one is enough to find the group */
            static bool s_unboundLogged;
            if (!b && !quiet && !s_unboundLogged) {
                s_unboundLogged = true;
                DX_LOG("bind group: slot %u (type %d) has no binding: a null view (reported once)",
                       s->slot, (int)s->type);
            }
            dx_null_view(s, h);
        }
    }
    if (l->resCount) {
        bg.res = dx_gpu(g_dx.resGpu, g_dx.resInc, resBase);
        bg.hasRes = true;
    }
    if (ringSamplers) {
        bg.smp = dx_gpu(g_dx.smpGpu, g_dx.smpInc, smpBase);
        bg.hasSmp = true;
    }
    f->groups[f->groupCount++] = bg;
    out.id = (dx_frame_tag() << D3DP_GEN_SHIFT) | f->groupCount;
    return out;
}

/* -------------------------------------------------------------- pipelines */
static D3D12_RENDER_TARGET_BLEND_DESC dx_blend(const RhiBlendState *b, RhiFormat fmt)
{
    D3D12_RENDER_TARGET_BLEND_DESC s;
    memset(&s, 0, sizeof(s));
    s.RenderTargetWriteMask = (UINT8)(b->writeMask & 0xFu); /* R G B A = bits 0..3 in both */
    s.SrcBlend = s.SrcBlendAlpha = D3D12_BLEND_ONE;
    s.DestBlend = s.DestBlendAlpha = D3D12_BLEND_ZERO;
    s.BlendOp = s.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    s.LogicOp = D3D12_LOGIC_OP_NOOP;
    bool enable = b->enable;
    if (enable && dx_formatMap[fmt].isInteger) {
        DX_LOG("pipeline: blending on an integer target is ignored");
        enable = false;
    }
    if (!enable) {
        return s;
    }
    s.BlendEnable = TRUE;
    s.SrcBlend = dx_blendFactorMap[b->srcColor].color;
    s.DestBlend = dx_blendFactorMap[b->dstColor].color;
    s.BlendOp = dx_blendOpMap[b->colorOp].d3d;
    s.SrcBlendAlpha = dx_blendFactorMap[b->srcAlpha].alpha;
    s.DestBlendAlpha = dx_blendFactorMap[b->dstAlpha].alpha;
    s.BlendOpAlpha = dx_blendOpMap[b->alphaOp].d3d;
    return s;
}

static D3D12_DEPTH_STENCILOP_DESC dx_stencil(const RhiStencilFace *f)
{
    D3D12_DEPTH_STENCILOP_DESC s = {
        dx_stencilOpMap[f->fail].d3d,
        dx_stencilOpMap[f->depthFail].d3d,
        dx_stencilOpMap[f->pass].d3d,
        dx_compareMap[f->compare].d3d,
    };
    return s;
}

static bool dx_valid_pipeline_desc(const RhiPipelineDesc *d)
{
    if (d->topology >= RHI_TOPO_COUNT || d->colorCount > RHI_MAX_COLOR_TARGETS ||
        d->depthFormat >= RHI_FMT_COUNT || d->vertexAttrCount > RHI_MAX_VERTEX_ATTRS ||
        d->vertexBindingCount > RHI_MAX_VERTEX_ATTRS || d->layoutCount > RHI_MAX_BIND_SLOTS) {
        return false;
    }
    for (uint32_t i = 0; i < d->colorCount; i++) {
        const RhiBlendState *b = &d->blend[i];
        if (d->colorFormats[i] <= RHI_FMT_UNKNOWN || d->colorFormats[i] >= RHI_FMT_COUNT ||
            b->srcColor >= RHI_BF_COUNT || b->dstColor >= RHI_BF_COUNT ||
            b->srcAlpha >= RHI_BF_COUNT || b->dstAlpha >= RHI_BF_COUNT ||
            b->colorOp >= RHI_BO_COUNT || b->alphaOp >= RHI_BO_COUNT) {
            return false;
        }
    }
    const RhiDepthStencilState *ds = &d->depthStencil;
    const RhiStencilFace *faces[2] = {&ds->front, &ds->back};
    for (int i = 0; i < 2; i++) {
        if (faces[i]->compare >= RHI_CMP_COUNT || faces[i]->pass >= RHI_SO_COUNT ||
            faces[i]->fail >= RHI_SO_COUNT || faces[i]->depthFail >= RHI_SO_COUNT) {
            return false;
        }
    }
    for (uint32_t i = 0; i < d->vertexAttrCount; i++) {
        if (d->vertexAttrs[i].format >= RHI_VTX_COUNT ||
            d->vertexAttrs[i].binding >= RHI_MAX_VERTEX_ATTRS) {
            return false;
        }
    }
    for (uint32_t i = 0; i < d->vertexBindingCount; i++) {
        if (d->vertexBindings[i].binding >= RHI_MAX_VERTEX_ATTRS) {
            return false;
        }
    }
    return ds->depthCompare < RHI_CMP_COUNT;
}

/* One pipeline's creation, in three steps (rhi.h rhi_create_pipelines):
 * dx_pipe_prepare on the calling thread (the checks, the root signature
 * from the shared table, the state description), dx_pipe_compile on any
 * thread (ID3D12Device is free threaded), dx_pipe_finish on the calling
 * thread (the pool slot, the name, the debug layer's messages).  The
 * description points into the job, so a job does not move once prepared. */
typedef struct DxPipeJob {
    bool ok; /* prepared: there is something to compile */
    DxRootSig *root;
    ID3D12PipelineState *pso;
    HRESULT hr;
    float ms;
    D3D12_INPUT_ELEMENT_DESC ie[RHI_MAX_VERTEX_ATTRS];
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd;
} DxPipeJob;

static bool dx_pipe_prepare(const RhiPipelineDesc *d, DxPipeJob *j)
{
    memset(j, 0, sizeof(*j));
    const char *name = d && d->debugName ? d->debugName : "?";
    if (!d || !dx_valid_pipeline_desc(d)) {
        DX_LOG("pipeline %s: invalid description", name);
        return false;
    }
    /* the destination read: ROVs only, with no render target (the pixel
     * shader writes the target through its UAV) */
    if (d->dstRead == RHI_DST_READ_ATTACHMENT ||
        (d->dstRead == RHI_DST_READ_STORAGE && (!g_dx.limits.rov || d->colorCount != 0)) ||
        d->dstRead >= RHI_DST_READ_COUNT) {
        DX_LOG("pipeline %s: destination read %d not available", name, (int)d->dstRead);
        return false;
    }
    DxShader *vs = d3dp_pool_get(&g_dx.shaders, d->vertex.id);
    DxShader *fs = d3dp_pool_get(&g_dx.shaders, d->fragment.id);
    if (!vs) {
        DX_LOG("pipeline %s: no vertex shader", name);
        return false;
    }
    DxRootSig *root = dx_root_signature(d->layouts, d->layoutCount);
    if (!root) {
        return false;
    }

    /* input layout: RHI locations to the vertex shader's semantics */
    D3D12_INPUT_ELEMENT_DESC *ie = j->ie;
    for (uint32_t i = 0; i < d->vertexAttrCount; i++) {
        const RhiVertexAttr *a = &d->vertexAttrs[i];
        int e = vs->sigCount > 0 ? d3dp_location_element(vs->sig, vs->sigCount, a->location) : -1;
        if (e < 0) {
            DX_LOG("pipeline %s: the vertex shader has no input at location %u", name, a->location);
            return false;
        }
        bool inst = false;
        for (uint32_t k = 0; k < d->vertexBindingCount; k++) {
            if (d->vertexBindings[k].binding == a->binding) {
                inst = d->vertexBindings[k].perInstance;
            }
        }
        ie[i].SemanticName = vs->sig[e].name;
        ie[i].SemanticIndex = vs->sig[e].semanticIndex;
        ie[i].Format = dx_vertexFormatMap[a->format].dxgi;
        ie[i].InputSlot = a->binding;
        ie[i].AlignedByteOffset = a->offset;
        ie[i].InputSlotClass = inst ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                    : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        ie[i].InstanceDataStepRate = inst ? 1u : 0u;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC *pd = &j->pd;
    pd->pRootSignature = root->rs;
    pd->VS.pShaderBytecode = vs->code;
    pd->VS.BytecodeLength = vs->size;
    if (fs) {
        pd->PS.pShaderBytecode = fs->code;
        pd->PS.BytecodeLength = fs->size;
    }
    pd->BlendState.IndependentBlendEnable = d->colorCount > 1 ? TRUE : FALSE;
    for (uint32_t i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
        pd->BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd->BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
        pd->BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
        pd->BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
        pd->BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
        pd->BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
        pd->BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pd->BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
    }
    for (uint32_t i = 0; i < d->colorCount; i++) {
        pd->BlendState.RenderTarget[i] = dx_blend(&d->blend[i], d->colorFormats[i]);
        pd->RTVFormats[i] = dx_formatMap[d->colorFormats[i]].view;
    }
    pd->SampleMask = UINT_MAX;
    /* The game never culls (rhi.h: cullNone is always true). */
    pd->RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd->RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd->RasterizerState.FrontCounterClockwise = FALSE;
    pd->RasterizerState.DepthClipEnable = TRUE;
    const RhiDepthStencilState *dss = &d->depthStencil;
    if (d->depthFormat != RHI_FMT_UNKNOWN) {
        pd->DepthStencilState.DepthEnable = dss->depthTest ? TRUE : FALSE;
        pd->DepthStencilState.DepthWriteMask = (dss->depthTest && dss->depthWrite)
                                                   ? D3D12_DEPTH_WRITE_MASK_ALL
                                                   : D3D12_DEPTH_WRITE_MASK_ZERO;
        pd->DepthStencilState.DepthFunc = dx_compareMap[dss->depthCompare].d3d;
        pd->DepthStencilState.StencilEnable =
            (dss->stencilTest && dx_formatMap[d->depthFormat].stencil) ? TRUE : FALSE;
        pd->DepthStencilState.StencilReadMask = dss->stencilReadMask;
        pd->DepthStencilState.StencilWriteMask = dss->stencilWriteMask;
        pd->DepthStencilState.FrontFace = dx_stencil(&dss->front);
        pd->DepthStencilState.BackFace = dx_stencil(&dss->back);
        pd->DSVFormat = dx_formatMap[d->depthFormat].dsv;
    } else {
        pd->DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pd->DepthStencilState.FrontFace = dx_stencil(&dss->front);
        pd->DepthStencilState.BackFace = dx_stencil(&dss->back);
    }
    pd->InputLayout.pInputElementDescs = d->vertexAttrCount ? ie : NULL;
    pd->InputLayout.NumElements = d->vertexAttrCount;
    pd->PrimitiveTopologyType = dx_topologyMap[d->topology].type;
    pd->NumRenderTargets = d->colorCount;
    pd->SampleDesc.Count = d->samples > 1u ? d->samples : 1u;

    j->root = root;
    j->ok = true;
    return true;
}

/* Any thread: the driver call alone */
static void dx_pipe_compile(DxPipeJob *j)
{
    if (!j->ok) {
        return;
    }
    const uint64_t t0 = dx_now_ns();
    j->hr = ID3D12Device_CreateGraphicsPipelineState(g_dx.device, &j->pd, &IID_ID3D12PipelineState,
                                                     (void **)&j->pso);
    j->ms = (float)((double)(dx_now_ns() - t0) / 1e6);
}

static RhiPipeline dx_pipe_finish(const RhiPipelineDesc *d, DxPipeJob *j)
{
    RhiPipeline out = {0};
    const char *name = d && d->debugName ? d->debugName : "?";
    if (!j->ok) {
        return out;
    }
    if (!dx_check(j->hr, "ID3D12Device_CreateGraphicsPipelineState", __FILE__, __LINE__)) {
        DX_LOG("pipeline %s not created", name);
        DX_RELEASE(j->pso);
        return out;
    }
    DxPipeline *p = NULL;
    uint32_t id = d3dp_pool_alloc(&g_dx.pipelines, (void **)&p);
    if (!id) {
        DX_RELEASE(j->pso);
        return out;
    }
    p->pso = j->pso;
    p->root = j->root;
    p->topo = dx_topologyMap[d->topology].topo;
    for (uint32_t i = 0; i < d->vertexBindingCount; i++) {
        p->strides[d->vertexBindings[i].binding] = d->vertexBindings[i].stride;
        p->bindingMask |= 1u << d->vertexBindings[i].binding;
    }
    dx_set_name((ID3D12Object *)p->pso, d->debugName);
    dx_drain_messages();
    out.id = id;
    return out;
}

RhiPipeline rhi_create_pipeline(const RhiPipelineDesc *d)
{
    DxPipeJob j;
    if (!dx_pipe_prepare(d, &j)) {
        return (RhiPipeline){0};
    }
    dx_pipe_compile(&j);
    return dx_pipe_finish(d, &j);
}

/* ------------------------------------------------- several at once (rhi.h)
 * Win32 threads (the backend does not need SDL): the workers take the next
 * job index from one counter until none is left; each finished job counts
 * and sets an event the calling thread waits on, calling the caller's wait
 * function at most every RHI_PIPELINE_WAIT_MS. */
typedef struct DxPipeWork {
    DxPipeJob *jobs;
    uint32_t n;
    volatile LONG next, finished;
    HANDLE wake;
} DxPipeWork;

/* the workers' stack, reserved (not committed): a driver's compiler may
 * recurse deeply, and a new thread's default is 1 MB */
#define DX_PIPE_STACK_BYTES (8u * 1024u * 1024u)

static DWORD WINAPI dx_pipe_worker(LPVOID arg)
{
    DxPipeWork *w = arg;
    for (;;) {
        const LONG i = InterlockedIncrement(&w->next) - 1;
        if (i < 0 || (uint32_t)i >= w->n) {
            break;
        }
        dx_pipe_compile(&w->jobs[i]);
        InterlockedIncrement(&w->finished);
        SetEvent(w->wake);
    }
    return 0;
}

uint32_t rhi_create_pipelines(const RhiPipelineDesc *descs, uint32_t n, RhiPipeline *out,
                              RhiPipelineBatch *batch)
{
    uint32_t made = 0, threads = 1;
    if (batch) {
        batch->threads = 1;
    }
    if (!descs || !out || n == 0) {
        return 0;
    }
    DxPipeWork w;
    memset(&w, 0, sizeof(w));
    w.jobs = calloc(n, sizeof(DxPipeJob));
    w.n = n;
    if (!w.jobs) {
        for (uint32_t i = 0; i < n; i++) {
            out[i] = rhi_create_pipeline(&descs[i]);
            made += out[i].id != 0;
            if (batch && batch->ms) {
                batch->ms[i] = 0.0f;
            }
        }
        return made;
    }
    for (uint32_t i = 0; i < n; i++) {
        dx_pipe_prepare(&descs[i], &w.jobs[i]);
    }
    bool done = false;
    if (n > 1 || (batch && batch->wait)) {
        HANDLE th[RHI_PIPELINE_THREADS_MAX];
        uint32_t started = 0;
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        uint32_t want = si.dwNumberOfProcessors > 1 ? (uint32_t)si.dwNumberOfProcessors - 1u : 1u;
        if (want > RHI_PIPELINE_THREADS_MAX) {
            want = RHI_PIPELINE_THREADS_MAX;
        }
        if (want > n) {
            want = n;
        }
        w.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        for (; w.wake && started < want; started++) {
            th[started] = CreateThread(NULL, DX_PIPE_STACK_BYTES, dx_pipe_worker, &w,
                                       STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
            if (!th[started]) {
                break;
            }
        }
        if (started > 0) {
            uint64_t last = dx_now_ns();
            while ((uint32_t)InterlockedCompareExchange(&w.finished, 0, 0) < n) {
                WaitForSingleObject(w.wake, RHI_PIPELINE_WAIT_MS);
                const uint64_t now = dx_now_ns();
                if (batch && batch->wait &&
                    now - last >= (uint64_t)RHI_PIPELINE_WAIT_MS * 1000000ull) {
                    batch->wait(batch->ctx,
                                (uint32_t)InterlockedCompareExchange(&w.finished, 0, 0));
                    last = dx_now_ns();
                }
            }
            WaitForMultipleObjects(started, th, TRUE, INFINITE);
            for (uint32_t i = 0; i < started; i++) {
                CloseHandle(th[i]);
            }
            threads = started;
            done = true;
        } else if (w.wake) {
            DX_LOG("pipelines: no worker thread (error %lu); creating them on this thread",
                   (unsigned long)GetLastError());
        }
        if (w.wake) {
            CloseHandle(w.wake);
        }
    }
    if (!done) {
        for (uint32_t i = 0; i < n; i++) {
            dx_pipe_compile(&w.jobs[i]);
            if (batch && batch->wait && i + 1 < n) {
                batch->wait(batch->ctx, i + 1);
            }
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        out[i] = dx_pipe_finish(&descs[i], &w.jobs[i]);
        made += out[i].id != 0;
        if (batch && batch->ms) {
            batch->ms[i] = w.jobs[i].ms;
        }
    }
    if (batch) {
        batch->threads = threads;
    }
    free(w.jobs);
    return made;
}

void rhi_destroy_pipeline(RhiPipeline h)
{
    DxPipeline *p = d3dp_pool_get(&g_dx.pipelines, h.id);
    if (!p) {
        return;
    }
    dx_defer((IUnknown *)p->pso);
    d3dp_pool_release(&g_dx.pipelines, h.id);
}
