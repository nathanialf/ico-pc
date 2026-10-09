/* d3d12_resource.c: buffers, textures, samplers, shaders and deferred
 * destruction for the D3D12 backend.  Every resource
 * is a committed resource. */
#include "d3d12_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------ deferred destroy */
void dx_defer(IUnknown *obj)
{
    if (!obj) {
        return;
    }
    DxFrame *f = dx_cur_frame();
    if (f->garbageCount == f->garbageCap) {
        uint32_t cap = f->garbageCap ? f->garbageCap * 2u : 64u;
        IUnknown **g = realloc(f->garbage, cap * sizeof(*g));
        if (!g) {
            /* the frame's list cannot grow: park the object in the fixed
             * overflow array, released with this slot's garbage.  Releasing
             * it now could free an object an unsubmitted list still uses. */
            const uint32_t slot = (uint32_t)(f - g_dx.frames);
            if (g_dx.overflowCount == DX_GARBAGE_OVERFLOW) {
                /* full: the other slots' entries are free after the GPU idles */
                DX_LOG("out of memory for deferred destroys; waiting idle");
                dx_wait_fence(dx_signal());
                uint32_t keep = 0;
                for (uint32_t i = 0; i < g_dx.overflowCount; i++) {
                    DxGarbageOverflow *e = &g_dx.overflow[i];
                    if (e->slot != slot) {
                        IUnknown_Release(e->obj);
                    } else {
                        g_dx.overflow[keep++] = *e;
                    }
                }
                g_dx.overflowCount = keep;
            }
            if (g_dx.overflowCount == DX_GARBAGE_OVERFLOW) {
                /* every entry belongs to the current frame: nothing can be
                 * released safely, so this one object leaks */
                DX_LOG("deferred destroy overflow full; leaking one object");
                return;
            }
            g_dx.overflow[g_dx.overflowCount].obj = obj;
            g_dx.overflow[g_dx.overflowCount].slot = slot;
            g_dx.overflowCount++;
            return;
        }
        f->garbage = g;
        f->garbageCap = cap;
    }
    f->garbage[f->garbageCount++] = obj;
}

void dx_destroy_garbage(DxFrame *f)
{
    for (uint32_t i = 0; i < f->garbageCount; i++) {
        IUnknown_Release(f->garbage[i]);
    }
    f->garbageCount = 0;
    /* the objects that overflowed from this slot (dx_defer) */
    const uint32_t slot = (uint32_t)(f - g_dx.frames);
    uint32_t keep = 0;
    for (uint32_t i = 0; i < g_dx.overflowCount; i++) {
        DxGarbageOverflow *e = &g_dx.overflow[i];
        if (e->slot == slot) {
            IUnknown_Release(e->obj);
        } else {
            g_dx.overflow[keep++] = *e;
        }
    }
    g_dx.overflowCount = keep;
}

void dx_set_name(ID3D12Object *o, const char *name)
{
    if (!o || !name || !g_dx.debugLayer) {
        return;
    }
    WCHAR w[128];
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 128)) {
        ID3D12Object_SetName(o, w);
    }
}

/* --------------------------------------------------------------- buffers */
DxBuffer *dx_get_buffer(RhiBuffer b)
{
    return d3dp_pool_get(&g_dx.buffers, b.id);
}

RhiBuffer rhi_create_buffer(const RhiBufferDesc *desc)
{
    RhiBuffer out = {0};
    if (!desc || desc->size == 0) {
        return out;
    }
    DxBuffer *b = NULL;
    uint32_t id = d3dp_pool_alloc(&g_dx.buffers, (void **)&b);
    if (!id) {
        DX_LOG("buffer pool full");
        return out;
    }
    D3D12_HEAP_PROPERTIES hp;
    memset(&hp, 0, sizeof(hp));
    D3D12_RESOURCE_STATES initial;
    switch (desc->memory) {
    case RHI_MEM_UPLOAD:
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        initial = D3D12_RESOURCE_STATE_GENERIC_READ; /* required for upload heaps */
        break;
    case RHI_MEM_READBACK:
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        initial = D3D12_RESOURCE_STATE_COPY_DEST; /* required for readback heaps */
        break;
    case RHI_MEM_DEVICE:
    default:
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        initial = D3D12_RESOURCE_STATE_COMMON;
        break;
    }
    /* a constant buffer view covers whole 256-byte blocks: round up so a
     * view at the end of the buffer stays inside it */
    uint64_t size = d3dp_align_up(desc->size, 256u);
    D3D12_RESOURCE_DESC rd;
    memset(&rd, 0, sizeof(rd));
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (!DX_CHECK(ID3D12Device_CreateCommittedResource(g_dx.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                       initial, NULL, &IID_ID3D12Resource,
                                                       (void **)&b->res))) {
        d3dp_pool_release(&g_dx.buffers, id);
        return out;
    }
    b->size = size;
    b->kind = desc->memory;
    b->gpu = ID3D12Resource_GetGPUVirtualAddress(b->res);
    if (desc->memory != RHI_MEM_DEVICE) {
        /* persistently mapped; the CPU does not read upload memory */
        D3D12_RANGE none = {0, 0};
        if (!DX_CHECK(ID3D12Resource_Map(b->res, 0, desc->memory == RHI_MEM_UPLOAD ? &none : NULL,
                                         &b->mapped))) {
            b->mapped = NULL;
        }
    }
    dx_set_name((ID3D12Object *)b->res, desc->debugName);
    out.id = id;
    return out;
}

void rhi_destroy_buffer(RhiBuffer h)
{
    DxBuffer *b = dx_get_buffer(h);
    if (!b) {
        return;
    }
    dx_defer((IUnknown *)b->res); /* releasing the resource unmaps it */
    d3dp_pool_release(&g_dx.buffers, h.id);
}

void *rhi_map_buffer(RhiBuffer h)
{
    DxBuffer *b = dx_get_buffer(h);
    return b ? b->mapped : NULL;
}

void rhi_unmap_buffer(RhiBuffer h)
{
    (void)h; /* persistently mapped; upload heaps are write-combined and coherent */
}

/* -------------------------------------------------------------- textures */
DxTexture *dx_get_texture(RhiTexture t)
{
    return d3dp_pool_get(&g_dx.textures, t.id);
}

static bool dx_create_rtv(DxTexture *t, DXGI_FORMAT fmt)
{
    uint32_t slot;
    if (!d3dp_slots_alloc(&g_dx.rtvSlots, &slot)) {
        DX_LOG("RTV heap full");
        return false;
    }
    D3D12_RENDER_TARGET_VIEW_DESC v;
    memset(&v, 0, sizeof(v));
    v.Format = fmt;
    v.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateRenderTargetView(g_dx.device, t->res, &v,
                                        dx_cpu(g_dx.rtvCpu, g_dx.rtvInc, slot));
    t->rtv = slot + 1u;
    return true;
}

static bool dx_create_dsv(DxTexture *t, const DxFormatMap *fm, bool readOnly)
{
    uint32_t slot;
    if (!d3dp_slots_alloc(&g_dx.dsvSlots, &slot)) {
        DX_LOG("DSV heap full");
        return false;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC v;
    memset(&v, 0, sizeof(v));
    v.Format = fm->dsv;
    v.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    if (readOnly) {
        v.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        if (fm->stencil) {
            v.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
        }
    }
    ID3D12Device_CreateDepthStencilView(g_dx.device, t->res, &v,
                                        dx_cpu(g_dx.dsvCpu, g_dx.dsvInc, slot));
    if (readOnly) {
        t->dsvRO = slot + 1u;
    } else {
        t->dsv = slot + 1u;
    }
    return true;
}

static void dx_free_views(DxTexture *t)
{
    /* RTV/DSV descriptors are read when a command is recorded, so their
     * slots can be reused at once (the resource itself is deferred) */
    if (t->rtv) {
        d3dp_slots_release(&g_dx.rtvSlots, t->rtv - 1u);
    }
    if (t->dsv) {
        d3dp_slots_release(&g_dx.dsvSlots, t->dsv - 1u);
    }
    if (t->dsvRO) {
        d3dp_slots_release(&g_dx.dsvSlots, t->dsvRO - 1u);
    }
    t->rtv = t->dsv = t->dsvRO = 0;
}

RhiTexture rhi_create_texture(const RhiTextureDesc *desc)
{
    RhiTexture out = {0};
    if (!desc || desc->format <= RHI_FMT_UNKNOWN || desc->format >= RHI_FMT_COUNT ||
        desc->width == 0 || desc->height == 0) {
        return out;
    }
    if (rhi_format_is_block(desc->format) &&
        (!g_dx.limits.bcTextures ||
         (desc->usage & (RHI_TEX_RENDER_TARGET | RHI_TEX_DEPTH_STENCIL)))) {
        /* texture packs: BC needs the device feature (RhiLimits.bcTextures)
         * and is sampled and copied into only, never drawn to */
        return out;
    }
    const DxFormatMap *fm = &dx_formatMap[desc->format];
    DxTexture *t = NULL;
    uint32_t id = d3dp_pool_alloc(&g_dx.textures, (void **)&t);
    if (!id) {
        DX_LOG("texture pool full");
        return out;
    }
    uint32_t mips = desc->mipLevels ? desc->mipLevels : 1u;
    D3D12_HEAP_PROPERTIES hp;
    memset(&hp, 0, sizeof(hp));
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd;
    memset(&rd, 0, sizeof(rd));
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = desc->width;
    rd.Height = desc->height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = (UINT16)mips;
    rd.Format = fm->resource;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (desc->usage & RHI_TEX_RENDER_TARGET) {
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if ((desc->usage & RHI_TEX_DEPTH_STENCIL) || fm->depth) {
        /* depth formats are always depth-stencil resources (a depth copy
         * target is sampled through a depth SRV, as on Vulkan) */
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        rd.Flags &= ~D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    /* Created in COMMON: an RHI texture starts UNDEFINED, and the first
     * barrier transitions from the tracked COMMON. */
    if (!DX_CHECK(ID3D12Device_CreateCommittedResource(g_dx.device, &hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                       D3D12_RESOURCE_STATE_COMMON, NULL,
                                                       &IID_ID3D12Resource, (void **)&t->res))) {
        d3dp_pool_release(&g_dx.textures, id);
        return out;
    }
    t->rhiFormat = desc->format;
    t->resFormat = fm->resource;
    t->width = desc->width;
    t->height = desc->height;
    t->mips = mips;
    t->usage = desc->usage;
    t->state = D3DP_STATE_COMMON;
    bool ok = true;
    if (rd.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
        ok = dx_create_rtv(t, fm->view);
    }
    if (ok && (rd.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) {
        ok = dx_create_dsv(t, fm, false) && dx_create_dsv(t, fm, true);
    }
    if (!ok) {
        dx_free_views(t);
        ID3D12Resource_Release(t->res);
        d3dp_pool_release(&g_dx.textures, id);
        return out;
    }
    dx_set_name((ID3D12Object *)t->res, desc->debugName);
    out.id = id;
    return out;
}

void rhi_destroy_texture(RhiTexture h)
{
    DxTexture *t = dx_get_texture(h);
    if (!t || t->swapchain) {
        return;
    }
    dx_free_views(t);
    dx_defer((IUnknown *)t->res);
    d3dp_pool_release(&g_dx.textures, h.id);
}

uint32_t dx_register_swapchain_buffer(ID3D12Resource *res, uint32_t w, uint32_t h)
{
    DxTexture *t = NULL;
    uint32_t id = d3dp_pool_alloc(&g_dx.textures, (void **)&t);
    if (!id) {
        return 0;
    }
    t->res = res;
    t->rhiFormat = RHI_FMT_BGRA8_UNORM;
    t->resFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    t->width = w;
    t->height = h;
    t->mips = 1;
    t->usage = RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST;
    t->state = D3DP_STATE_PRESENT;
    t->swapchain = true;
    if (!dx_create_rtv(t, DXGI_FORMAT_B8G8R8A8_UNORM)) {
        d3dp_pool_release(&g_dx.textures, id);
        return 0;
    }
    return id;
}

void dx_release_swapchain_buffer(uint32_t id)
{
    RhiTexture h = {id};
    DxTexture *t = dx_get_texture(h);
    if (!t) {
        return;
    }
    /* called after the GPU is idle: release now (ResizeBuffers needs every
     * reference gone) */
    dx_free_views(t);
    ID3D12Resource_Release(t->res);
    d3dp_pool_release(&g_dx.textures, id);
}

/* -------------------------------------------------------------- samplers
 * Each sampler's descriptor lives in the persistent part of the sampler
 * heap, at its pool index; a single-sampler table points at it directly. */
RhiSampler rhi_create_sampler(const RhiSamplerDesc *desc)
{
    RhiSampler out = {0};
    if (!desc) {
        return out;
    }
    DxSampler *s = NULL;
    uint32_t id = d3dp_pool_alloc(&g_dx.samplers, (void **)&s);
    if (!id) {
        DX_LOG("sampler pool full (%u)", DX_SMP_PERSISTENT);
        return out;
    }
    D3D12_SAMPLER_DESC d;
    memset(&d, 0, sizeof(d));
    bool aniso = desc->maxAnisotropy > 1.0f;
    if (aniso) {
        /* linear between levels: the OS runtime's headers have no
         * anisotropic filter with point mips (Vulkan honours desc->mip) */
        d.Filter = D3D12_FILTER_ANISOTROPIC;
        d.MaxAnisotropy = desc->maxAnisotropy > 16.0f ? 16u : (UINT)desc->maxAnisotropy;
    } else {
        /* D3D12_ENCODE_BASIC_FILTER: min << 4 | mag << 2 | mip */
        d.Filter = (D3D12_FILTER)(((desc->min & 1u) << 4) | ((desc->mag & 1u) << 2) |
                                  (desc->mip == RHI_FILTER_LINEAR ? 1u : 0u));
        d.MaxAnisotropy = 1;
    }
    const D3D12_TEXTURE_ADDRESS_MODE wrap[2] = {D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                                                D3D12_TEXTURE_ADDRESS_MODE_CLAMP};
    d.AddressU = wrap[desc->s & 1];
    d.AddressV = wrap[desc->t & 1];
    d.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    d.MipLODBias = desc->lodBias;
    d.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    d.MinLOD = desc->minLod;
    d.MaxLOD = desc->maxLod;
    s->desc = d;
    s->heapIndex = (id & D3DP_INDEX_MASK) - 1u;
    ID3D12Device_CreateSampler(g_dx.device, &d, dx_cpu(g_dx.smpCpu, g_dx.smpInc, s->heapIndex));
    out.id = id;
    return out;
}

void rhi_destroy_sampler(RhiSampler h)
{
    /* A bind group of a frame in flight may still point at the persistent
     * descriptor, so the pool slot (and with it the descriptor) is released
     * only when the current frame slot is recycled (dx_recycle_frame). */
    DxSampler *s = d3dp_pool_get(&g_dx.samplers, h.id);
    DxFrame *f = dx_cur_frame();
    if (!s || s->dead || f->deadSamplerCount >= DX_SMP_PERSISTENT) {
        return;
    }
    s->dead = true;
    f->deadSamplers[f->deadSamplerCount++] = h.id;
}

/* --------------------------------------------------------------- shaders */
RhiShader rhi_create_shader(const RhiShaderDesc *desc)
{
    RhiShader out = {0};
    if (!desc || !desc->bytecode || !d3dp_is_dxbc(desc->bytecode, desc->bytecodeSize)) {
        DX_LOG("shader %s is not a DXIL (DXBC) container",
               desc && desc->debugName ? desc->debugName : "?");
        return out;
    }
    DxShader *s = NULL;
    uint32_t id = d3dp_pool_alloc(&g_dx.shaders, (void **)&s);
    if (!id) {
        return out;
    }
    s->code = malloc(desc->bytecodeSize);
    if (!s->code) {
        d3dp_pool_release(&g_dx.shaders, id);
        return out;
    }
    memcpy(s->code, desc->bytecode, desc->bytecodeSize);
    s->size = desc->bytecodeSize;
    s->stage = desc->stage;
    s->sigCount = -1;
    if (desc->stage == RHI_STAGE_VERTEX) {
        s->sigCount = d3dp_read_input_signature(s->code, s->size, s->sig, DX_MAX_SIG);
        if (s->sigCount > DX_MAX_SIG) {
            DX_LOG("shader %s: %d vertex inputs, only %d used",
                   desc->debugName ? desc->debugName : "?", s->sigCount, DX_MAX_SIG);
            s->sigCount = DX_MAX_SIG;
        }
    }
    out.id = id;
    return out;
}

void rhi_destroy_shader(RhiShader h)
{
    DxShader *s = d3dp_pool_get(&g_dx.shaders, h.id);
    if (!s) {
        return;
    }
    /* bytecode is only read during pipeline creation */
    free(s->code);
    d3dp_pool_release(&g_dx.shaders, h.id);
}

/* ---------------------------------------------------------------- teardown */
void dx_release_all_objects(void)
{
    for (uint32_t i = 0; i < g_dx.buffers.next; i++) {
        DxBuffer *b = d3dp_pool_at(&g_dx.buffers, i);
        if (b && b->res) {
            ID3D12Resource_Release(b->res);
        }
    }
    for (uint32_t i = 0; i < g_dx.textures.next; i++) {
        DxTexture *t = d3dp_pool_at(&g_dx.textures, i);
        if (t && t->res && !t->swapchain) {
            ID3D12Resource_Release(t->res);
        }
    }
    for (uint32_t i = 0; i < g_dx.shaders.next; i++) {
        DxShader *s = d3dp_pool_at(&g_dx.shaders, i);
        if (s) {
            free(s->code);
        }
    }
    for (uint32_t i = 0; i < g_dx.pipelines.next; i++) {
        DxPipeline *p = d3dp_pool_at(&g_dx.pipelines, i);
        if (p && p->pso) {
            ID3D12PipelineState_Release(p->pso);
        }
    }
}
