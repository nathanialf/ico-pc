/* d3d12_pipeline.c: bind group layouts, root signatures, transient bind
 * groups and pipeline state objects for the D3D12 backend (README.md,
 * "Descriptors"). */
#include "d3d12_internal.h"
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------- bind group layouts */
RhiBindGroupLayout rhi_CreateBindGroupLayout(const RhiBindGroupLayoutDesc *desc)
{
    RhiBindGroupLayout out = {0};
    if (!desc) {
        return out;
    }
    D3dpLayout l;
    if (!d3dp_LayoutBuild(desc->slots, desc->slotCount, &l)) {
        DX_LOG("bind group layout %s: invalid", desc->debugName ? desc->debugName : "?");
        return out;
    }
    DxLayout *dl = NULL;
    uint32_t id = d3dp_PoolAlloc(&g_dx.layouts, (void **)&dl);
    if (!id) {
        return out;
    }
    dl->l = l;
    out.id = id;
    return out;
}

void rhi_DestroyBindGroupLayout(RhiBindGroupLayout h)
{
    /* root signatures copied what they need; nothing on the GPU */
    d3dp_PoolRelease(&g_dx.layouts, h.id);
}

/* ------------------------------------------------------- root signatures
 * One root signature per distinct list of layouts (cached; rd_core has a
 * handful): group g is register space g, as descriptor tables. */
static D3D12_SHADER_VISIBILITY dx_Visibility(uint32_t stages)
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

static DxRootSig *dx_RootSignature(const RhiBindGroupLayout *layouts, uint32_t count)
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
        DxLayout *dl = d3dp_PoolGet(&g_dx.layouts, layouts[g].id);
        if (!dl) {
            DX_LOG("pipeline: invalid bind group layout %u", g);
            return NULL;
        }
        ls[g] = &dl->l;
    }
    DxRootSig *r = &g_dx.roots[g_dx.rootCount];
    memset(r, 0, sizeof(*r));
    uint32_t np = d3dp_RootParams(ls, count, r->resParam, r->smpParam, r->dynParam);

    D3D12_ROOT_PARAMETER params[2 * RHI_MAX_BIND_SLOTS];
    D3D12_DESCRIPTOR_RANGE ranges[RHI_MAX_BIND_SLOTS][D3DP_MAX_SLOTS];
    memset(params, 0, sizeof(params));
    for (uint32_t g = 0; g < count; g++) {
        const D3dpLayout *l = ls[g];
        uint32_t nr[2] = {0, 0};
        D3D12_DESCRIPTOR_RANGE *rr[2] = {&ranges[g][0], &ranges[g][l->resCount]};
        for (uint32_t i = 0; i < l->slotCount; i++) {
            const RhiBindSlot *s = &l->slots[i];
            if (l->table[i] == D3DP_ROOT_CBV) {
                /* package PA: a root CBV; its address comes at bind time */
                D3D12_ROOT_PARAMETER *p = &params[r->dynParam[g] + l->offset[i]];
                p->ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                p->Descriptor.ShaderRegister = s->slot;
                p->Descriptor.RegisterSpace = g;
                p->ShaderVisibility = dx_Visibility(
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
            p->ShaderVisibility = dx_Visibility(l->resStages);
        }
        if (r->smpParam[g] >= 0) {
            D3D12_ROOT_PARAMETER *p = &params[r->smpParam[g]];
            p->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p->DescriptorTable.NumDescriptorRanges = l->smpCount;
            p->DescriptorTable.pDescriptorRanges = rr[1];
            p->ShaderVisibility = dx_Visibility(l->smpStages);
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

void dx_ReleaseRootSignatures(void)
{
    for (uint32_t i = 0; i < g_dx.rootCount; i++) {
        if (g_dx.roots[i].rs) {
            ID3D12RootSignature_Release(g_dx.roots[i].rs);
        }
    }
    g_dx.rootCount = 0;
}

/* ---------------------------------------------------- transient bind groups
 * Descriptors are written straight into the frame slot's region of the
 * shader-visible heaps; the region is reset when the slot is recycled
 * (rhi_WaitFrame).  The handle id is (frame tag << 20) | (index + 1). */
static uint32_t dx_FrameTag(void)
{
    return (uint32_t)(g_dx.frameIndex & D3DP_GEN_MASK);
}

DxBindGroup *dx_GetBindGroup(uint32_t id)
{
    DxFrame *f = dx_CurFrame();
    uint32_t idx = id & D3DP_INDEX_MASK;
    if (idx == 0 || idx > f->groupCount || (id >> D3DP_GEN_SHIFT) != dx_FrameTag()) {
        return NULL;
    }
    return &f->groups[idx - 1];
}

static const RhiBinding *dx_FindBinding(const RhiBindGroupDesc *d, const RhiBindSlot *s)
{
    for (uint32_t i = 0; i < d->bindingCount; i++) {
        if (d->bindings[i].slot == s->slot && d->bindings[i].type == s->type) {
            return &d->bindings[i];
        }
    }
    return NULL;
}

static void dx_NullView(const RhiBindSlot *s, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    if (s->type == RHI_BIND_UNIFORM_BUFFER) {
        ID3D12Device_CreateConstantBufferView(g_dx.device, NULL, h);
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
static bool dx_WriteResource(const RhiBindSlot *s, const RhiBinding *b,
                             D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    switch (s->type) {
    case RHI_BIND_UNIFORM_BUFFER: {
        DxBuffer *buf = dx_GetBuffer(b->buffer);
        uint32_t size = buf ? d3dp_CbvSize(b->offset, b->size, buf->size) : 0;
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
        DxBuffer *buf = dx_GetBuffer(b->buffer);
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
        DxTexture *t = dx_GetTexture(b->texture);
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
        v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Texture2D.MipLevels = t->mips;
        ID3D12Device_CreateShaderResourceView(g_dx.device, t->res, &v, h);
        return true;
    }
    default:
        return false;
    }
}

static RhiBindGroup dx_CreateBindGroup(const RhiBindGroupDesc *desc, bool quiet);

uint32_t dx_NullBindGroup(uint32_t layoutId)
{
    uint32_t slot = (layoutId & D3DP_INDEX_MASK) - 1u;
    if (!d3dp_PoolGet(&g_dx.layouts, layoutId) || slot >= DX_MAX_LAYOUTS) {
        return 0;
    }
    DxFrame *f = dx_CurFrame();
    if (!dx_GetBindGroup(f->nullGroups[slot])) {
        RhiBindGroupDesc d = {{layoutId}, NULL, 0};
        f->nullGroups[slot] = dx_CreateBindGroup(&d, true).id;
    }
    return f->nullGroups[slot];
}

RhiBindGroup rhi_CreateBindGroup(const RhiBindGroupDesc *desc)
{
    return dx_CreateBindGroup(desc, false);
}

static RhiBindGroup dx_CreateBindGroup(const RhiBindGroupDesc *desc, bool quiet)
{
    RhiBindGroup out = {0};
    DxLayout *dl = desc ? d3dp_PoolGet(&g_dx.layouts, desc->layout.id) : NULL;
    if (!dl) {
        return out;
    }
    const D3dpLayout *l = &dl->l;
    DxFrame *f = dx_CurFrame();
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
    if (l->resCount && !d3dp_RingAlloc(&f->resRing, l->resCount, &resBase)) {
        DX_LOG("CBV/SRV descriptor ring full for this frame (%u)", f->resRing.size);
        return out;
    }
    /* one sampler: its persistent descriptor; several: a table in the ring */
    bool ringSamplers = l->smpCount > 1;
    if (ringSamplers && !d3dp_RingAlloc(&f->smpRing, l->smpCount, &smpBase)) {
        DX_LOG("sampler descriptor ring full for this frame (%u)", f->smpRing.size);
        return out;
    }
    bg.dynCount = l->dynCount;
    for (uint32_t i = 0; i < l->slotCount; i++) {
        const RhiBindSlot *s = &l->slots[i];
        const RhiBinding *b = dx_FindBinding(desc, s);
        if (l->table[i] == D3DP_ROOT_CBV) {
            /* package PA: the base address; a root CBV has no size and no
             * null form, so a missing or bad binding leaves 0, which the
             * shader must not read (rd_core binds every one) */
            DxBuffer *buf = b ? dx_GetBuffer(b->buffer) : NULL;
            if (buf && (b->offset & 255u) == 0 && b->size && b->size <= 65536u &&
                b->offset + b->size <= buf->size) {
                bg.dyn[l->offset[i]] = buf->gpu + b->offset;
            } else if (!quiet) {
                DX_LOG("bind group: b%u: invalid dynamic uniform buffer or range", s->slot);
            }
            continue;
        }
        if (l->table[i] == D3DP_TABLE_SAMPLER) {
            DxSampler *smp = b ? d3dp_PoolGet(&g_dx.samplers, b->sampler.id) : NULL;
            if (!smp || smp->dead) {
                if (!quiet) {
                    DX_LOG("bind group: s%u: invalid sampler", s->slot);
                }
                smp = NULL;
            }
            if (!ringSamplers) {
                /* a missing sampler binds the default (point, clamp) */
                bg.smp = dx_Gpu(g_dx.smpGpu, g_dx.smpInc, smp ? smp->heapIndex : DX_SMP_DEFAULT);
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
                    g_dx.device, &d, dx_Cpu(g_dx.smpCpu, g_dx.smpInc, smpBase + l->offset[i]));
            }
            continue;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE h = dx_Cpu(g_dx.resCpu, g_dx.resInc, resBase + l->offset[i]);
        if (!b || !dx_WriteResource(s, b, h)) {
            if (!b && !quiet) {
                DX_LOG("bind group: slot %u (type %d) has no binding", s->slot, (int)s->type);
            }
            dx_NullView(s, h);
        }
    }
    if (l->resCount) {
        bg.res = dx_Gpu(g_dx.resGpu, g_dx.resInc, resBase);
        bg.hasRes = true;
    }
    if (ringSamplers) {
        bg.smp = dx_Gpu(g_dx.smpGpu, g_dx.smpInc, smpBase);
        bg.hasSmp = true;
    }
    f->groups[f->groupCount++] = bg;
    out.id = (dx_FrameTag() << D3DP_GEN_SHIFT) | f->groupCount;
    return out;
}

/* -------------------------------------------------------------- pipelines */
static D3D12_RENDER_TARGET_BLEND_DESC dx_Blend(const RhiBlendState *b, RhiFormat fmt)
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

static D3D12_DEPTH_STENCILOP_DESC dx_Stencil(const RhiStencilFace *f)
{
    D3D12_DEPTH_STENCILOP_DESC s = {
        dx_stencilOpMap[f->fail].d3d,
        dx_stencilOpMap[f->depthFail].d3d,
        dx_stencilOpMap[f->pass].d3d,
        dx_compareMap[f->compare].d3d,
    };
    return s;
}

static bool dx_ValidPipelineDesc(const RhiPipelineDesc *d)
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

RhiPipeline rhi_CreatePipeline(const RhiPipelineDesc *d)
{
    RhiPipeline out = {0};
    const char *name = d && d->debugName ? d->debugName : "?";
    if (!d || !dx_ValidPipelineDesc(d)) {
        DX_LOG("pipeline %s: invalid description", name);
        return out;
    }
    DxShader *vs = d3dp_PoolGet(&g_dx.shaders, d->vertex.id);
    DxShader *fs = d3dp_PoolGet(&g_dx.shaders, d->fragment.id);
    if (!vs) {
        DX_LOG("pipeline %s: no vertex shader", name);
        return out;
    }
    DxRootSig *root = dx_RootSignature(d->layouts, d->layoutCount);
    if (!root) {
        return out;
    }

    /* input layout: RHI locations to the vertex shader's semantics */
    D3D12_INPUT_ELEMENT_DESC ie[RHI_MAX_VERTEX_ATTRS];
    for (uint32_t i = 0; i < d->vertexAttrCount; i++) {
        const RhiVertexAttr *a = &d->vertexAttrs[i];
        int e = vs->sigCount > 0 ? d3dp_LocationElement(vs->sig, vs->sigCount, a->location) : -1;
        if (e < 0) {
            DX_LOG("pipeline %s: the vertex shader has no input at location %u", name, a->location);
            return out;
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

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd;
    memset(&pd, 0, sizeof(pd));
    pd.pRootSignature = root->rs;
    pd.VS.pShaderBytecode = vs->code;
    pd.VS.BytecodeLength = vs->size;
    if (fs) {
        pd.PS.pShaderBytecode = fs->code;
        pd.PS.BytecodeLength = fs->size;
    }
    pd.BlendState.IndependentBlendEnable = d->colorCount > 1 ? TRUE : FALSE;
    for (uint32_t i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
        pd.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.BlendState.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
        pd.BlendState.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
        pd.BlendState.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
    }
    for (uint32_t i = 0; i < d->colorCount; i++) {
        pd.BlendState.RenderTarget[i] = dx_Blend(&d->blend[i], d->colorFormats[i]);
        pd.RTVFormats[i] = dx_formatMap[d->colorFormats[i]].view;
    }
    pd.SampleMask = UINT_MAX;
    /* The game never culls (rhi.h: cullNone is always true). */
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.FrontCounterClockwise = FALSE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    const RhiDepthStencilState *dss = &d->depthStencil;
    if (d->depthFormat != RHI_FMT_UNKNOWN) {
        pd.DepthStencilState.DepthEnable = dss->depthTest ? TRUE : FALSE;
        pd.DepthStencilState.DepthWriteMask = (dss->depthTest && dss->depthWrite)
                                                  ? D3D12_DEPTH_WRITE_MASK_ALL
                                                  : D3D12_DEPTH_WRITE_MASK_ZERO;
        pd.DepthStencilState.DepthFunc = dx_compareMap[dss->depthCompare].d3d;
        pd.DepthStencilState.StencilEnable =
            (dss->stencilTest && dx_formatMap[d->depthFormat].stencil) ? TRUE : FALSE;
        pd.DepthStencilState.StencilReadMask = dss->stencilReadMask;
        pd.DepthStencilState.StencilWriteMask = dss->stencilWriteMask;
        pd.DepthStencilState.FrontFace = dx_Stencil(&dss->front);
        pd.DepthStencilState.BackFace = dx_Stencil(&dss->back);
        pd.DSVFormat = dx_formatMap[d->depthFormat].dsv;
    } else {
        pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pd.DepthStencilState.FrontFace = dx_Stencil(&dss->front);
        pd.DepthStencilState.BackFace = dx_Stencil(&dss->back);
    }
    pd.InputLayout.pInputElementDescs = d->vertexAttrCount ? ie : NULL;
    pd.InputLayout.NumElements = d->vertexAttrCount;
    pd.PrimitiveTopologyType = dx_topologyMap[d->topology].type;
    pd.NumRenderTargets = d->colorCount;
    pd.SampleDesc.Count = 1;

    DxPipeline *p = NULL;
    uint32_t id = d3dp_PoolAlloc(&g_dx.pipelines, (void **)&p);
    if (!id) {
        return out;
    }
    if (!DX_CHECK(ID3D12Device_CreateGraphicsPipelineState(
            g_dx.device, &pd, &IID_ID3D12PipelineState, (void **)&p->pso))) {
        DX_LOG("pipeline %s not created", name);
        d3dp_PoolRelease(&g_dx.pipelines, id);
        return out;
    }
    p->root = root;
    p->topo = dx_topologyMap[d->topology].topo;
    for (uint32_t i = 0; i < d->vertexBindingCount; i++) {
        p->strides[d->vertexBindings[i].binding] = d->vertexBindings[i].stride;
        p->bindingMask |= 1u << d->vertexBindings[i].binding;
    }
    if (g_dx.debugLayer && d->debugName) {
        WCHAR w[128];
        if (MultiByteToWideChar(CP_UTF8, 0, d->debugName, -1, w, 128)) {
            ID3D12PipelineState_SetName(p->pso, w);
        }
    }
    dx_DrainMessages();
    out.id = id;
    return out;
}

void rhi_DestroyPipeline(RhiPipeline h)
{
    DxPipeline *p = d3dp_PoolGet(&g_dx.pipelines, h.id);
    if (!p) {
        return;
    }
    dx_Defer((IUnknown *)p->pso);
    d3dp_PoolRelease(&g_dx.pipelines, h.id);
}
