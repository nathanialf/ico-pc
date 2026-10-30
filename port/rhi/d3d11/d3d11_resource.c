/* d3d11_resource.c: buffers (bind-class parts and the upload shadow),
 * textures and their views, samplers, shaders (recorded, compiled lazily),
 * bind group layouts and transient bind groups, deferred releases and the
 * synchronous texture readback of the Direct3D 11 backend.  Compile-only
 * in v0.8.6: it has not run on any Windows machine. */
#include "d3d11_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------ deferred release */
void d11_defer(IUnknown *obj)
{
    if (!obj) {
        return;
    }
    D11Frame *f = d11_cur_frame();
    if (f->garbageCount == f->garbageCap) {
        const uint32_t cap = f->garbageCap ? f->garbageCap * 2u : 64u;
        IUnknown **g = realloc(f->garbage, cap * sizeof(*g));
        if (!g) {
            /* the list cannot grow: release now.  The runtime keeps an
             * object a command already given to it alive until the GPU is
             * done; only a list still being recorded could miss it */
            static bool s_logged;
            if (!s_logged) {
                s_logged = true;
                D11_LOG("out of memory for deferred releases: releasing at once (logged once)");
            }
            IUnknown_Release(obj);
            return;
        }
        f->garbage = g;
        f->garbageCap = cap;
    }
    f->garbage[f->garbageCount++] = obj;
}

void d11_destroy_garbage(D11Frame *f)
{
    for (uint32_t i = 0; i < f->garbageCount; i++) {
        IUnknown_Release(f->garbage[i]);
    }
    f->garbageCount = 0;
}

void d11_set_name(ID3D11DeviceChild *o, const char *name)
{
    if (!o || !name || !g_d11.debugLayer) {
        return;
    }
    /* WKPDID_D3DDebugObjectName: the debug layer's name for the object */
    static const GUID kName = {
        0x429b8c22, 0x9188, 0x4b0c, {0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00}};
    ID3D11DeviceChild_SetPrivateData(o, &kName, (UINT)strlen(name), name);
}

/* --------------------------------------------------------------- buffers */
D11Buffer *d11_get_buffer(RhiBuffer b)
{
    return d3dp_pool_get(&g_d11.buffers, b.id);
}

ID3D11Buffer *d11_buffer_part(const D11Buffer *b, uint32_t part)
{
    return b && part < D11_PART_COUNT ? b->part[part] : NULL;
}

ID3D11Buffer *d11_buffer_any_part(const D11Buffer *b)
{
    for (uint32_t i = 0; b && i < D11_PART_COUNT; i++) {
        if (b->part[i]) {
            return b->part[i];
        }
    }
    return NULL;
}

/* dynamic: a DYNAMIC buffer the CPU maps (the CB part of an upload buffer
 * on a device without partial constant buffer updates, d11_buffer_flush) */
static bool d11_make_part(D11Buffer *b, uint32_t part, UINT bind, const char *name, bool dynamic)
{
    D3D11_BUFFER_DESC d;
    memset(&d, 0, sizeof(d));
    d.ByteWidth = (UINT)b->size;
    d.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    d.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0u;
    d.BindFlags = bind;
    if (part == D11_PART_SRV) {
        /* StructuredBuffer<uint4> (vu_common.hlsli): 16-byte elements */
        d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        d.StructureByteStride = 16;
    }
    if (!D11_CHECK(ID3D11Device_CreateBuffer(g_d11.device, &d, NULL, &b->part[part]))) {
        b->part[part] = NULL;
        return false;
    }
    d11_set_name((ID3D11DeviceChild *)b->part[part], name);
    return true;
}

static void d11_buffer_release(D11Buffer *b, bool now)
{
    IUnknown *objs[D11_PART_COUNT + 2 + D11_SRV_CACHE];
    uint32_t n = 0;
    for (uint32_t i = 0; i < D11_PART_COUNT; i++) {
        objs[n++] = (IUnknown *)b->part[i];
        b->part[i] = NULL;
    }
    objs[n++] = (IUnknown *)b->staging;
    objs[n++] = (IUnknown *)b->stagingTex;
    b->staging = NULL;
    b->stagingTex = NULL;
    for (uint32_t i = 0; i < D11_SRV_CACHE; i++) {
        objs[n++] = (IUnknown *)b->srvs[i].srv;
        b->srvs[i].srv = NULL;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (!objs[i]) {
            continue;
        }
        if (now) {
            IUnknown_Release(objs[i]);
        } else {
            d11_defer(objs[i]);
        }
    }
    free(b->shadow);
    b->shadow = NULL;
}

static RhiBuffer createBuffer(const RhiBufferDesc *desc);

/* timed into RhiStats.createNs */
RhiBuffer rhi_create_buffer(const RhiBufferDesc *desc)
{
    const uint64_t t0 = d11_now_ns();
    const RhiBuffer b = createBuffer(desc);
    g_d11.counts.createNs += d11_now_ns() - t0;
    return b;
}

static RhiBuffer createBuffer(const RhiBufferDesc *desc)
{
    RhiBuffer out = {0};
    if (!desc || desc->size == 0 || !g_d11.device) {
        return out;
    }
    /* whole 256-byte blocks: a constant buffer window at the end stays
     * inside it, and every part's size is a multiple of 16 */
    const uint64_t size = d3dp_align_up(desc->size, 256u);
    if (size > 0xFFFFFF00ull) {
        D11_LOG("buffer %s: %llu bytes is past what one D3D11 buffer holds",
                desc->debugName ? desc->debugName : "?", (unsigned long long)desc->size);
        return out;
    }
    D11Buffer *b = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_d11.buffers, (void **)&b);
    if (!id) {
        D11_LOG("buffer pool full");
        return out;
    }
    memset(b, 0, sizeof(*b));
    b->size = size;
    b->usage = desc->usage;
    b->kind = desc->memory;
    bool ok = true;
    if (desc->memory == RHI_MEM_READBACK) {
        D3D11_BUFFER_DESC d;
        memset(&d, 0, sizeof(d));
        d.ByteWidth = (UINT)size;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ok = D11_CHECK(ID3D11Device_CreateBuffer(g_d11.device, &d, NULL, &b->staging));
    } else {
        const uint32_t u = desc->usage;
        const UINT vi = ((u & RHI_BUF_VERTEX) ? D3D11_BIND_VERTEX_BUFFER : 0u) |
                        ((u & RHI_BUF_INDEX) ? D3D11_BIND_INDEX_BUFFER : 0u);
        if (vi) {
            ok = d11_make_part(b, D11_PART_VI, vi, desc->debugName, false);
        }
        if (ok && (u & RHI_BUF_UNIFORM)) {
            /* without partial constant buffer updates an upload buffer's
             * CB part is mapped at the submits (the range read, not the
             * whole buffer); only beside another part, which stays the
             * copy source (d3d11_cmd.c d11_first_part), as a DYNAMIC
             * buffer is no copy's business here */
            const bool mapped = g_d11.cbMapUpload && desc->memory == RHI_MEM_UPLOAD &&
                                (vi || (u & RHI_BUF_STORAGE_READ));
            ok = d11_make_part(b, D11_PART_CB, D3D11_BIND_CONSTANT_BUFFER, desc->debugName, mapped);
            if (!ok && mapped) {
                static bool s_dynLogged;
                if (!s_dynLogged) {
                    s_dynLogged = true;
                    D11_LOG("buffer %s: no DYNAMIC constant buffer of %llu bytes: written whole at "
                            "each submit instead (logged once)",
                            desc->debugName ? desc->debugName : "?", (unsigned long long)size);
                }
                ok = d11_make_part(b, D11_PART_CB, D3D11_BIND_CONSTANT_BUFFER, desc->debugName,
                                   false);
            } else {
                b->cbMapped = mapped;
            }
        }
        if (ok && (u & RHI_BUF_STORAGE_READ)) {
            ok = d11_make_part(b, D11_PART_SRV, D3D11_BIND_SHADER_RESOURCE, desc->debugName, false);
        }
        if (ok && !d11_buffer_any_part(b)) {
            /* copies only (COPY_SRC/COPY_DST): a buffer without a bind flag */
            ok = d11_make_part(b, D11_PART_VI, 0u, desc->debugName, false);
        }
    }
    if (ok && desc->memory != RHI_MEM_DEVICE) {
        b->shadow = calloc(1, (size_t)size);
        if (!b->shadow) {
            D11_LOG("buffer %s: out of memory for its %llu-byte CPU copy",
                    desc->debugName ? desc->debugName : "?", (unsigned long long)size);
            ok = false;
        }
    }
    if (!ok) {
        d11_buffer_release(b, true);
        d3dp_pool_release(&g_d11.buffers, id);
        return out;
    }
    g_d11.counts.buffersCreated++;
    out.id = id;
    return out;
}

void rhi_destroy_buffer(RhiBuffer h)
{
    D11Buffer *b = d11_get_buffer(h);
    if (!b) {
        return;
    }
    d11_buffer_release(b, false);
    g_d11.counts.buffersDestroyed++;
    d3dp_pool_release(&g_d11.buffers, h.id);
}

/* READBACK: what the copies left in the staging buffer and the staging
 * texture, into the shadow (Map waits for the copies) */
static void d11_readback_refresh(D11Buffer *b)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (b->stagingDirty && b->staging) {
        memset(&m, 0, sizeof(m));
        if (D11_CHECK(ID3D11DeviceContext_Map(g_d11.ctx, (ID3D11Resource *)b->staging, 0,
                                              D3D11_MAP_READ, 0, &m))) {
            memcpy(b->shadow, m.pData, (size_t)b->size);
            ID3D11DeviceContext_Unmap(g_d11.ctx, (ID3D11Resource *)b->staging, 0);
        }
        b->stagingDirty = false;
    }
    if (b->texPending && b->stagingTex) {
        memset(&m, 0, sizeof(m));
        if (D11_CHECK(ID3D11DeviceContext_Map(g_d11.ctx, (ID3D11Resource *)b->stagingTex, 0,
                                              D3D11_MAP_READ, 0, &m))) {
            for (uint32_t y = 0; y < b->texH; y++) {
                uint8_t *dst = b->shadow + b->texDstOffset + (uint64_t)y * b->texRowPitch;
                const uint8_t *src = (const uint8_t *)m.pData + (size_t)y * m.RowPitch;
                if (b->texMapTexelBytes == b->texTexelBytes) {
                    memcpy(dst, src, (size_t)b->texW * b->texTexelBytes);
                } else {
                    /* D32S8: the depth float of each 8-byte texel */
                    for (uint32_t x = 0; x < b->texW; x++) {
                        memcpy(dst + (size_t)x * b->texTexelBytes,
                               src + (size_t)x * b->texMapTexelBytes, b->texTexelBytes);
                    }
                }
            }
            ID3D11DeviceContext_Unmap(g_d11.ctx, (ID3D11Resource *)b->stagingTex, 0);
        }
        b->texPending = false;
    }
}

void *rhi_map_buffer(RhiBuffer h)
{
    D11Buffer *b = d11_get_buffer(h);
    if (!b || !b->shadow) {
        return NULL;
    }
    if (b->kind == RHI_MEM_READBACK) {
        d11_readback_refresh(b);
    }
    return b->shadow;
}

void rhi_unmap_buffer(RhiBuffer h)
{
    (void)h; /* the shadow stays; uploads are written at the submits that read them */
}

void rhi_buffer_note_written(RhiBuffer h, uint64_t hi)
{
    static bool s_logged;
    D11Buffer *b = d11_get_buffer(h);
    if (!b || b->kind != RHI_MEM_UPLOAD || !b->shadow) {
        return;
    }
    hi = hi < b->size ? hi : b->size;
    const uint64_t frame = g_d11.frameIndex + 1u;
    if (b->writtenFrame != frame) {
        /* the frame's first allocation: the ring starts again at 0 */
        b->writtenFrame = frame;
        b->written = hi;
    } else if (hi > b->written) {
        b->written = hi;
    }
    if (!s_logged) {
        s_logged = true;
        D11_LOG("upload writes: a submit writes the ranges its lists read under the renderer's "
                "written mark (the first: %llu of the ring's %llu bytes); constant buffers %s "
                "(logged once)",
                (unsigned long long)b->written, (unsigned long long)b->size,
                g_d11.cbPartialUpdate ? "by range (partial updates)"
                : b->cbMapped         ? (g_d11.cbMapNoOverwrite
                                             ? "mapped, the range read (discard, then no-overwrite)"
                                             : "mapped, discard under the mark")
                                      : "written whole");
    }
}

uint64_t d11_buffer_written_end(const D11Buffer *b)
{
    return b->writtenFrame ? b->written : b->size;
}

/* The CB part of an UPLOAD buffer as a DYNAMIC buffer (cbMapped): [lo, hi)
 * of the shadow through Map on the immediate context, before the list that
 * reads it executes.  A frame's first map is DISCARD (a new copy: the lists
 * of earlier frames still in flight keep theirs), later ones NO_OVERWRITE
 * (the ring is append-only within a frame, so the bytes a list already
 * executed may read are never changed; a range two lists read is written
 * again with the same bytes); without NO_OVERWRITE on dynamic constant
 * buffers every map is DISCARD and writes all the frame wrote, from 0 to
 * the written mark.  Returns the bytes written. */
static uint64_t d11_cb_map_write(D11Buffer *b, uint64_t lo, uint64_t hi)
{
    static bool s_failLogged;
    const uint64_t frame = g_d11.frameIndex + 1u;
    D3D11_MAP type = D3D11_MAP_WRITE_DISCARD;
    if (b->cbDiscardFrame == frame) {
        if (g_d11.cbMapNoOverwrite) {
            type = D3D11_MAP_WRITE_NO_OVERWRITE;
        } else {
            lo = 0;
            hi = d3dp_align_up(d11_buffer_written_end(b), 16u);
            hi = hi < b->size ? hi : b->size;
        }
    }
    D3D11_MAPPED_SUBRESOURCE m;
    memset(&m, 0, sizeof(m));
    const HRESULT hr =
        ID3D11DeviceContext_Map(g_d11.ctx, (ID3D11Resource *)b->part[D11_PART_CB], 0, type, 0, &m);
    if (FAILED(hr) || !m.pData) {
        if (!s_failLogged) {
            s_failLogged = true;
            D11_LOG("upload writes: Map of a constant buffer failed (HRESULT 0x%08lx): its "
                    "constants this submit are stale (logged once)",
                    (unsigned long)hr);
        }
        return 0;
    }
    if (type == D3D11_MAP_WRITE_DISCARD) {
        b->cbDiscardFrame = frame;
    }
    memcpy((uint8_t *)m.pData + lo, b->shadow + lo, (size_t)(hi - lo));
    ID3D11DeviceContext_Unmap(g_d11.ctx, (ID3D11Resource *)b->part[D11_PART_CB], 0);
    return hi - lo;
}

void d11_buffer_flush(D11Buffer *b, ID3D11DeviceContext1 *ctx, uint32_t part, uint64_t lo,
                      uint64_t hi)
{
    if (!b || !ctx || !b->shadow || b->kind != RHI_MEM_UPLOAD) {
        return;
    }
    lo &= ~(uint64_t)15u;
    hi = d3dp_align_up(hi, 16u);
    hi = hi < b->size ? hi : b->size;
    if (lo >= hi) {
        return;
    }
    for (uint32_t i = 0; i < D11_PART_COUNT; i++) {
        if (!b->part[i] || (part < D11_PART_COUNT && part != i)) {
            continue;
        }
        if (i == D11_PART_CB && b->cbMapped) {
            g_d11.counts.uploadWriteBytes += d11_cb_map_write(b, lo, hi);
            continue;
        }
        if (i == D11_PART_CB && !g_d11.cbPartialUpdate) {
            /* no partial constant buffer updates: the whole buffer */
            ID3D11DeviceContext1_UpdateSubresource1(ctx, (ID3D11Resource *)b->part[i], 0, NULL,
                                                    b->shadow, 0, 0, 0);
            g_d11.counts.uploadWriteBytes += b->size;
            continue;
        }
        /* a box on a constant buffer is valid only with NO_OVERWRITE or
         * DISCARD (the debug layer's error 288; without the flag the
         * runtime drops the update and the shaders read stale constants).
         * NO_OVERWRITE is the upload contract: the CPU writes a range only
         * once no submitted list reads it any more (as Vulkan's mapped
         * memory requires), and DISCARD would lose the rest of the buffer.
         * The other parts take no flag. */
        const UINT flags = i == D11_PART_CB ? D3D11_COPY_NO_OVERWRITE : 0u;
        D3D11_BOX box = {(UINT)lo, 0, 0, (UINT)hi, 1, 1};
        ID3D11DeviceContext1_UpdateSubresource1(ctx, (ID3D11Resource *)b->part[i], 0, &box,
                                                b->shadow + lo, 0, 0, flags);
        g_d11.counts.uploadWriteBytes += hi - lo;
    }
}

ID3D11ShaderResourceView *d11_buffer_srv(D11Buffer *b, uint64_t offset, uint64_t size)
{
    if (!b || !b->part[D11_PART_SRV] || offset >= b->size) {
        return NULL;
    }
    if (size == 0) {
        size = b->size - offset;
    }
    if ((offset & 15u) || size < 16u || size > b->size - offset) {
        return NULL;
    }
    const uint32_t first = (uint32_t)(offset / 16u), count = (uint32_t)(size / 16u);
    for (uint32_t i = 0; i < D11_SRV_CACHE; i++) {
        if (b->srvs[i].srv && b->srvs[i].first == first && b->srvs[i].count == count) {
            return b->srvs[i].srv;
        }
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC v;
    memset(&v, 0, sizeof(v));
    v.Format = DXGI_FORMAT_UNKNOWN;
    v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    v.Buffer.FirstElement = first;
    v.Buffer.NumElements = count;
    ID3D11ShaderResourceView *srv = NULL;
    if (!D11_CHECK(ID3D11Device_CreateShaderResourceView(
            g_d11.device, (ID3D11Resource *)b->part[D11_PART_SRV], &v, &srv))) {
        return NULL;
    }
    D11SrvCacheEntry *e = &b->srvs[b->srvNext];
    b->srvNext = (b->srvNext + 1u) % D11_SRV_CACHE;
    if (e->srv) {
        d11_defer((IUnknown *)e->srv); /* a bind group of this frame may hold it */
    }
    e->first = first;
    e->count = count;
    e->srv = srv;
    return srv;
}

ID3D11Texture2D *d11_readback_texture_target(D11Buffer *dst, const D11Texture *src, uint32_t w,
                                             uint32_t h, uint64_t dstOffset, uint32_t rowPitch)
{
    if (!dst || !src || dst->kind != RHI_MEM_READBACK || !dst->shadow || w == 0 || h == 0 ||
        src->samples > 1u) {
        return NULL;
    }
    const D11FormatMap *fm = &d11_formatMap[src->rhiFormat];
    const uint64_t rowBytes = (uint64_t)w * fm->texelBytes;
    if (rowPitch < rowBytes || dstOffset > dst->size ||
        (uint64_t)(h - 1u) * rowPitch + rowBytes > dst->size - dstOffset) {
        D11_LOG("rhi_cmd_copy_texture_to_buffer: %ux%u rows of %u bytes do not fit the buffer", w,
                h, rowPitch);
        return NULL;
    }
    if (dst->texPending) {
        /* one pending texture copy a readback buffer: the earlier one's rows
           are lost (no caller does this on Direct3D 11 today) */
        static bool s_logged;
        if (!s_logged) {
            s_logged = true;
            D11_LOG("rhi_cmd_copy_texture_to_buffer: a second texture copy into one readback "
                    "buffer before its map replaces the first (logged once)");
        }
    }
    if (dst->stagingTex) {
        D3D11_TEXTURE2D_DESC have;
        memset(&have, 0, sizeof(have));
        ID3D11Texture2D_GetDesc(dst->stagingTex, &have);
        if (have.Width != w || have.Height != h || have.Format != src->resFormat) {
            d11_defer((IUnknown *)dst->stagingTex);
            dst->stagingTex = NULL;
        }
    }
    if (!dst->stagingTex) {
        D3D11_TEXTURE2D_DESC d;
        memset(&d, 0, sizeof(d));
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = src->resFormat;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (!D11_CHECK(ID3D11Device_CreateTexture2D(g_d11.device, &d, NULL, &dst->stagingTex))) {
            dst->stagingTex = NULL;
            return NULL;
        }
    }
    dst->texPending = true;
    dst->texW = w;
    dst->texH = h;
    dst->texTexelBytes = fm->texelBytes;
    dst->texMapTexelBytes = src->swapchain ? 4u : fm->mapTexelBytes;
    dst->texRowPitch = rowPitch;
    dst->texDstOffset = dstOffset;
    return dst->stagingTex;
}

/* -------------------------------------------------------------- textures */
D11Texture *d11_get_texture(RhiTexture t)
{
    return d3dp_pool_get(&g_d11.textures, t.id);
}

static void d11_texture_release(D11Texture *t, bool now)
{
    IUnknown *objs[6] = {(IUnknown *)t->rtv, (IUnknown *)t->dsv,        (IUnknown *)t->dsvRO,
                         (IUnknown *)t->srv, (IUnknown *)t->srvStencil, (IUnknown *)t->res};
    for (int i = 0; i < 6; i++) {
        if (objs[i]) {
            if (now) {
                IUnknown_Release(objs[i]);
            } else {
                d11_defer(objs[i]);
            }
        }
    }
    t->rtv = NULL;
    t->dsv = t->dsvRO = NULL;
    t->srv = NULL;
    t->srvStencil = NULL;
    t->res = NULL;
}

static bool d11_create_views(D11Texture *t, const D11FormatMap *fm, UINT bind)
{
    const bool ms = t->samples > 1u;
    if (bind & D3D11_BIND_RENDER_TARGET) {
        D3D11_RENDER_TARGET_VIEW_DESC v;
        memset(&v, 0, sizeof(v));
        v.Format = t->swapchain ? t->resFormat : fm->view;
        v.ViewDimension = ms ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        if (!D11_CHECK(ID3D11Device_CreateRenderTargetView(g_d11.device, (ID3D11Resource *)t->res,
                                                           &v, &t->rtv))) {
            t->rtv = NULL;
            return false;
        }
    }
    if (bind & D3D11_BIND_DEPTH_STENCIL) {
        D3D11_DEPTH_STENCIL_VIEW_DESC v;
        memset(&v, 0, sizeof(v));
        v.Format = fm->dsv;
        v.ViewDimension = ms ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
        if (!D11_CHECK(ID3D11Device_CreateDepthStencilView(g_d11.device, (ID3D11Resource *)t->res,
                                                           &v, &t->dsv))) {
            t->dsv = NULL;
            return false;
        }
        /* read-only depth (and stencil): depth tested, the texture also
         * sampled in the same pass (RHI_STATE_DEPTH_READ) */
        v.Flags = D3D11_DSV_READ_ONLY_DEPTH | (fm->stencil ? D3D11_DSV_READ_ONLY_STENCIL : 0u);
        if (!D11_CHECK(ID3D11Device_CreateDepthStencilView(g_d11.device, (ID3D11Resource *)t->res,
                                                           &v, &t->dsvRO))) {
            t->dsvRO = NULL;
            return false;
        }
    }
    if (bind & D3D11_BIND_SHADER_RESOURCE) {
        /* depth formats are viewed as their depth plane whatever the aspect:
         * stencil is never sampled (rhi.h) */
        D3D11_SHADER_RESOURCE_VIEW_DESC v;
        memset(&v, 0, sizeof(v));
        v.Format = fm->view;
        if (ms) {
            v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        } else {
            v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            v.Texture2D.MipLevels = t->mips;
        }
        if (!D11_CHECK(ID3D11Device_CreateShaderResourceView(g_d11.device, (ID3D11Resource *)t->res,
                                                             &v, &t->srv))) {
            t->srv = NULL;
            return false;
        }
        /* the stencil plane (RHI_ASPECT_STENCIL: the shadow count's one
         * draw), its value in green (RhiLimits.stencilViewChannel 1).
         * Optional: a refused view keeps the texture, and
         * RhiLimits.stencilView goes false for the device (logged once),
         * which turns the one draw off (rd_shadow_one.c: the seven draws) */
        if (fm->stencil && !ms && t->rhiFormat == RHI_FMT_D32F_S8 && g_d11.limits.stencilView) {
            v.Format = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
            if (!D11_CHECK(ID3D11Device_CreateShaderResourceView(
                    g_d11.device, (ID3D11Resource *)t->res, &v, &t->srvStencil))) {
                t->srvStencil = NULL;
                g_d11.limits.stencilView = false;
                D11_LOG("the stencil view was not made: no stencil views on this device from "
                        "now (the shadow count takes the seven draws)");
            }
        }
    }
    return true;
}

static RhiTexture createTexture(const RhiTextureDesc *desc);

/* timed into RhiStats.createNs */
RhiTexture rhi_create_texture(const RhiTextureDesc *desc)
{
    const uint64_t t0 = d11_now_ns();
    const RhiTexture t = createTexture(desc);
    g_d11.counts.createNs += d11_now_ns() - t0;
    return t;
}

static RhiTexture createTexture(const RhiTextureDesc *desc)
{
    RhiTexture out = {0};
    if (!desc || desc->format <= RHI_FMT_UNKNOWN || desc->format >= RHI_FMT_COUNT ||
        desc->width == 0 || desc->height == 0 || !g_d11.device) {
        return out;
    }
    if (rhi_format_is_block(desc->format) &&
        (desc->usage & (RHI_TEX_RENDER_TARGET | RHI_TEX_DEPTH_STENCIL))) {
        return out; /* texture packs: sampled and copied into only */
    }
    const D11FormatMap *fm = &d11_formatMap[desc->format];
    const uint32_t mips = desc->mipLevels ? desc->mipLevels : 1u;
    const uint32_t samples = desc->samples > 1u ? desc->samples : 1u;
    if (samples > 1u &&
        ((g_d11.limits.msaaCounts & samples) == 0u || (samples & (samples - 1u)) != 0u ||
         mips != 1u || !(desc->usage & (RHI_TEX_RENDER_TARGET | RHI_TEX_DEPTH_STENCIL)))) {
        D11_LOG("rhi_create_texture: %u samples not taken", samples);
        return out;
    }
    D11Texture *t = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_d11.textures, (void **)&t);
    if (!id) {
        D11_LOG("texture pool full");
        return out;
    }
    memset(t, 0, sizeof(*t));
    UINT bind = 0;
    if (desc->usage & RHI_TEX_SAMPLED) {
        bind |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (desc->usage & RHI_TEX_RENDER_TARGET) {
        bind |= D3D11_BIND_RENDER_TARGET;
    }
    if ((desc->usage & RHI_TEX_DEPTH_STENCIL) || fm->depth) {
        /* depth formats are always depth-stencil resources, as on D3D12 */
        bind |= D3D11_BIND_DEPTH_STENCIL;
        bind &= ~(UINT)D3D11_BIND_RENDER_TARGET;
    }
    D3D11_TEXTURE2D_DESC d;
    memset(&d, 0, sizeof(d));
    d.Width = desc->width;
    d.Height = desc->height;
    d.MipLevels = mips;
    d.ArraySize = 1;
    d.Format = fm->resource;
    d.SampleDesc.Count = samples;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = bind;
    if (!D11_CHECK(ID3D11Device_CreateTexture2D(g_d11.device, &d, NULL, &t->res))) {
        d3dp_pool_release(&g_d11.textures, id);
        return out;
    }
    t->rhiFormat = desc->format;
    t->resFormat = fm->resource;
    t->width = desc->width;
    t->height = desc->height;
    t->mips = mips;
    t->samples = samples;
    t->usage = desc->usage;
    if (!d11_create_views(t, fm, bind)) {
        d11_texture_release(t, true);
        d3dp_pool_release(&g_d11.textures, id);
        return out;
    }
    d11_set_name((ID3D11DeviceChild *)t->res, desc->debugName);
    g_d11.counts.texturesCreated++;
    out.id = id;
    return out;
}

void rhi_destroy_texture(RhiTexture h)
{
    D11Texture *t = d11_get_texture(h);
    if (!t || t->swapchain) {
        return;
    }
    d11_texture_release(t, false);
    g_d11.counts.texturesDestroyed++;
    d3dp_pool_release(&g_d11.textures, h.id);
}

uint32_t d11_register_swapchain_buffer(ID3D11Texture2D *res, uint32_t w, uint32_t h)
{
    D11Texture *t = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_d11.textures, (void **)&t);
    if (!id) {
        return 0;
    }
    memset(t, 0, sizeof(*t));
    t->res = res;
    t->rhiFormat = RHI_FMT_BGRA8_UNORM;
    t->resFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    t->width = w;
    t->height = h;
    t->mips = 1;
    t->samples = 1;
    t->usage = RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST;
    t->swapchain = true;
    if (!d11_create_views(t, &d11_formatMap[RHI_FMT_BGRA8_UNORM], D3D11_BIND_RENDER_TARGET)) {
        t->res = NULL; /* the caller's reference */
        d3dp_pool_release(&g_d11.textures, id);
        return 0;
    }
    return id;
}

void d11_release_swapchain_buffer(uint32_t id)
{
    RhiTexture h = {id};
    D11Texture *t = d11_get_texture(h);
    if (!t) {
        return;
    }
    /* now, not deferred: ResizeBuffers needs every reference gone (the
     * caller waited for the GPU and cleared the immediate context) */
    d11_texture_release(t, true);
    d3dp_pool_release(&g_d11.textures, id);
}

/* -------------------------------------------------------------- samplers */
RhiSampler rhi_create_sampler(const RhiSamplerDesc *desc)
{
    RhiSampler out = {0};
    if (!desc || !g_d11.device) {
        return out;
    }
    D3D11_SAMPLER_DESC d;
    memset(&d, 0, sizeof(d));
    if (desc->maxAnisotropy > 1.0f) {
        d.Filter = D3D11_FILTER_ANISOTROPIC; /* linear between levels, as D3D12 */
        d.MaxAnisotropy = desc->maxAnisotropy > 16.0f ? 16u : (UINT)desc->maxAnisotropy;
    } else {
        /* D3D11_ENCODE_BASIC_FILTER: min << 4 | mag << 2 | mip */
        d.Filter = (D3D11_FILTER)(((desc->min & 1u) << 4) | ((desc->mag & 1u) << 2) |
                                  (desc->mip == RHI_FILTER_LINEAR ? 1u : 0u));
        d.MaxAnisotropy = 1;
    }
    const D3D11_TEXTURE_ADDRESS_MODE wrap[2] = {D3D11_TEXTURE_ADDRESS_WRAP,
                                                D3D11_TEXTURE_ADDRESS_CLAMP};
    d.AddressU = wrap[desc->s & 1];
    d.AddressV = wrap[desc->t & 1];
    d.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    d.MipLODBias = desc->lodBias;
    d.ComparisonFunc = D3D11_COMPARISON_NEVER;
    d.MinLOD = desc->minLod;
    d.MaxLOD = desc->maxLod;
    D11Sampler *s = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_d11.samplers, (void **)&s);
    if (!id) {
        D11_LOG("sampler pool full (%u)", g_d11.samplers.cap);
        return out;
    }
    /* identical descriptions give the same object back (D3D11 keeps 4096
     * distinct ones at most) */
    if (!D11_CHECK(ID3D11Device_CreateSamplerState(g_d11.device, &d, &s->state))) {
        s->state = NULL;
        d3dp_pool_release(&g_d11.samplers, id);
        return out;
    }
    out.id = id;
    return out;
}

void rhi_destroy_sampler(RhiSampler h)
{
    D11Sampler *s = d3dp_pool_get(&g_d11.samplers, h.id);
    if (!s) {
        return;
    }
    d11_defer((IUnknown *)s->state); /* a bind group of this frame may hold it */
    s->state = NULL;
    d3dp_pool_release(&g_d11.samplers, h.id);
}

/* --------------------------------------------------------------- shaders */
D11Shader *d11_get_shader(RhiShader s)
{
    return d3dp_pool_get(&g_d11.shaders, s.id);
}

static bool d11_copy_text(char *dst, size_t n, const char *src)
{
    const size_t len = src ? strlen(src) : 0;
    if (len >= n) {
        return false;
    }
    memcpy(dst, src ? src : "", len);
    dst[len] = '\0';
    return true;
}

static void d11_shader_free(D11Shader *s)
{
    D11_RELEASE(s->vs);
    D11_RELEASE(s->ps);
    free(s->dxbc);
    s->dxbc = NULL;
    free(s->sources);
    s->sources = NULL;
}

/* The description only: the HLSL, the entry, the macros and the table of
 * embedded sources (the array is copied; the texts are the caller's, the
 * renderer's static table).  The compile waits for the first pipeline. */
RhiShader rhi_create_shader(const RhiShaderDesc *desc)
{
    RhiShader out = {0};
    const char *name = desc && desc->debugName ? desc->debugName : "?";
    if (!desc || !desc->hlslFile || !desc->entryPoint || !desc->sources || desc->sourceCount == 0) {
        D11_LOG("shader %s: no HLSL source to compile (the Direct3D 11 backend compiles the "
                "embedded HLSL at run time)",
                name);
        return out;
    }
    D11Shader *s = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_d11.shaders, (void **)&s);
    if (!id) {
        D11_LOG("shader pool full");
        return out;
    }
    memset(s, 0, sizeof(*s)); /* the SRWLOCK too: SRWLOCK_INIT is zero */
    if (!d11_copy_text(s->entry, sizeof(s->entry), desc->entryPoint) ||
        !d11_copy_text(s->file, sizeof(s->file), desc->hlslFile) ||
        !d11_copy_text(s->defines, sizeof(s->defines), desc->defines)) {
        D11_LOG("shader %s: an entry, file or macro text too long", name);
        d3dp_pool_release(&g_d11.shaders, id);
        return out;
    }
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->sources = malloc(desc->sourceCount * sizeof(*s->sources));
    if (!s->sources) {
        d3dp_pool_release(&g_d11.shaders, id);
        return out;
    }
    memcpy(s->sources, desc->sources, desc->sourceCount * sizeof(*s->sources));
    s->desc.stage = desc->stage;
    s->desc.entryPoint = s->entry;
    s->desc.debugName = s->name;
    s->desc.hlslFile = s->file;
    s->desc.defines = s->defines;
    s->desc.sources = s->sources;
    s->desc.sourceCount = desc->sourceCount;
    s->sigCount = -1;
    out.id = id;
    return out;
}

void rhi_destroy_shader(RhiShader h)
{
    D11Shader *s = d11_get_shader(h);
    if (!s) {
        return;
    }
    /* pipelines hold their own references to the shader objects */
    d11_shader_free(s);
    d3dp_pool_release(&g_d11.shaders, h.id);
}

static bool d11_shader_compile_locked(D11Shader *s)
{
    void *dxbc = NULL;
    size_t len = 0;
    char err[1024];
    err[0] = '\0';
    if (!d3d11_shader_compile(&s->desc, g_d11.cacheDir[0] ? g_d11.cacheDir : NULL, g_d11.buildId,
                              &dxbc, &len, err, sizeof(err))) {
        snprintf(s->error, sizeof(s->error), "%s", err[0] ? err : "the compile failed");
        return false; /* d3d11_shader_compile logged it (rate-limited) */
    }
    HRESULT hr;
    if (s->desc.stage == RHI_STAGE_VERTEX) {
        hr = ID3D11Device_CreateVertexShader(g_d11.device, dxbc, len, NULL, &s->vs);
        if (SUCCEEDED(hr)) {
            s->dxbc = dxbc; /* CreateInputLayout takes it per pipeline */
            s->dxbcSize = len;
            s->sigCount = d3dp_read_input_signature(dxbc, len, s->sig, D11_MAX_SIG);
            if (s->sigCount > D11_MAX_SIG) {
                D11_LOG("shader %s: %d vertex inputs, only %d used", s->name, s->sigCount,
                        D11_MAX_SIG);
                s->sigCount = D11_MAX_SIG;
            }
            return true;
        }
        s->vs = NULL;
    } else {
        hr = ID3D11Device_CreatePixelShader(g_d11.device, dxbc, len, NULL, &s->ps);
        if (SUCCEEDED(hr)) {
            free(dxbc);
            return true;
        }
        s->ps = NULL;
    }
    free(dxbc);
    snprintf(s->error, sizeof(s->error), "Create%sShader failed (HRESULT 0x%08lx)",
             s->desc.stage == RHI_STAGE_VERTEX ? "Vertex" : "Pixel", (unsigned long)hr);
    D11_LOG("shader %s: %s", s->name, s->error);
    return false;
}

bool d11_shader_compiled(D11Shader *s, char *err, size_t errn)
{
    if (!s) {
        if (err && errn) {
            snprintf(err, errn, "no such shader");
        }
        return false;
    }
    AcquireSRWLockExclusive(&s->lock);
    if (!s->compiled && !s->failed) {
        s->compiled = d11_shader_compile_locked(s);
        s->failed = !s->compiled;
    }
    const bool ok = s->compiled;
    if (!ok && err && errn) {
        snprintf(err, errn, "%s", s->error);
    }
    ReleaseSRWLockExclusive(&s->lock);
    return ok;
}

/* ----------------------------------------------------- bind group layouts */
D11Layout *d11_get_layout(uint32_t id)
{
    return d3dp_pool_get(&g_d11.layouts, id);
}

RhiBindGroupLayout rhi_create_bind_group_layout(const RhiBindGroupLayoutDesc *desc)
{
    RhiBindGroupLayout out = {0};
    if (!desc) {
        return out;
    }
    D3dpLayout l;
    if (!d3dp_layout_build(desc->slots, desc->slotCount, &l)) {
        D11_LOG("bind group layout %s: invalid", desc->debugName ? desc->debugName : "?");
        return out;
    }
    D11Layout *dl = NULL;
    const uint32_t id = d3dp_pool_alloc(&g_d11.layouts, (void **)&dl);
    if (!id) {
        return out;
    }
    dl->l = l;
    out.id = id;
    return out;
}

void rhi_destroy_bind_group_layout(RhiBindGroupLayout h)
{
    /* pipelines copied the layout ids; bind groups of this frame read the
     * layout only at their creation */
    d3dp_pool_release(&g_d11.layouts, h.id);
}

/* ------------------------------------------------------------ bind groups
 * Transient: the frame slot's arena, reset when the slot is recycled.  The
 * handle id is (frame tag << 20) | (index + 1). */
static uint32_t d11_frame_tag(void)
{
    return (uint32_t)(g_d11.frameIndex & D3DP_GEN_MASK);
}

D11BindGroup *d11_get_bind_group(uint32_t id)
{
    D11Frame *f = d11_cur_frame();
    const uint32_t idx = id & D3DP_INDEX_MASK;
    if (idx == 0 || idx > f->groupCount || (id >> D3DP_GEN_SHIFT) != d11_frame_tag()) {
        return NULL;
    }
    return &f->groups[idx - 1];
}

static const RhiBinding *d11_find_binding(const RhiBindGroupDesc *d, const RhiBindSlot *s)
{
    for (uint32_t i = 0; i < d->bindingCount; i++) {
        if (d->bindings[i].slot == s->slot && d->bindings[i].type == s->type) {
            return &d->bindings[i];
        }
    }
    return NULL;
}

/* one entry from its binding; false when the binding is unusable (the
 * entry then reads zeros, or the default sampler) */
static bool d11_fill_entry(D11BindEntry *e, const RhiBindSlot *s, const RhiBinding *b)
{
    switch (s->type) {
    case RHI_BIND_UNIFORM_BUFFER:
    case RHI_BIND_UNIFORM_BUFFER_DYNAMIC: {
        D11Buffer *buf = d11_get_buffer(b->buffer);
        if (!buf || !buf->part[D11_PART_CB] || b->offset >= buf->size || (b->offset & 255u)) {
            D11_LOG("bind group: b%u: invalid uniform buffer or offset", s->slot);
            return false;
        }
        /* size 0 (static uniforms): to the end, at most the 64 KiB a
         * shader sees of one constant buffer */
        uint64_t size = b->size ? b->size : buf->size - b->offset;
        if (!b->size && size > 65536u) {
            size = 65536u;
        }
        if ((s->type == RHI_BIND_UNIFORM_BUFFER_DYNAMIC && !b->size) || size > 65536u ||
            size > buf->size - b->offset) {
            D11_LOG("bind group: b%u: invalid uniform range (offset %llu, size %llu)", s->slot,
                    (unsigned long long)b->offset, (unsigned long long)b->size);
            return false;
        }
        e->cb = buf->part[D11_PART_CB];
        e->firstConstant = (uint32_t)(b->offset / 16u);
        e->numConstants = (uint32_t)(d3dp_align_up(size, 256u) / 16u);
        e->buffer = b->buffer.id;
        e->lo = b->offset;
        e->hi = b->offset + size;
        return true;
    }
    case RHI_BIND_STORAGE_BUFFER: {
        D11Buffer *buf = d11_get_buffer(b->buffer);
        ID3D11ShaderResourceView *srv = d11_buffer_srv(buf, b->offset, b->size);
        if (!srv) {
            D11_LOG("bind group: t%u: invalid storage buffer range", s->slot);
            return false;
        }
        e->srv = srv;
        e->buffer = b->buffer.id;
        e->lo = b->offset;
        e->hi = b->size ? b->offset + b->size : buf->size;
        return true;
    }
    case RHI_BIND_SAMPLED_TEXTURE: {
        D11Texture *t = d11_get_texture(b->texture);
        if (!t || !t->srv) {
            D11_LOG("bind group: t%u: invalid texture (or one made without RHI_TEX_SAMPLED)",
                    s->slot);
            return false;
        }
        /* RHI_ASPECT_UINT: no UINT view here (RhiLimits.uintView false);
         * RHI_ASPECT_STENCIL: the stencil plane's view where there is one */
        e->srv = b->aspect == RHI_ASPECT_STENCIL && t->srvStencil ? t->srvStencil : t->srv;
        return true;
    }
    case RHI_BIND_SAMPLER: {
        D11Sampler *smp = d3dp_pool_get(&g_d11.samplers, b->sampler.id);
        if (!smp || !smp->state) {
            D11_LOG("bind group: s%u: invalid sampler", s->slot);
            return false;
        }
        e->smp = smp->state;
        return true;
    }
    default:
        /* the destination read's storage texture and input attachment: not
         * on this backend (RhiLimits.rov false) */
        return false;
    }
}

RhiBindGroup rhi_create_bind_group(const RhiBindGroupDesc *desc)
{
    RhiBindGroup out = {0};
    D11Layout *dl = desc ? d11_get_layout(desc->layout.id) : NULL;
    if (!dl) {
        return out;
    }
    const D3dpLayout *l = &dl->l;
    D11Frame *f = d11_cur_frame();
    if (f->groupCount >= D3DP_INDEX_MASK) {
        return out;
    }
    if (f->groupCount == f->groupCap) {
        const uint32_t cap = f->groupCap ? f->groupCap * 2u : 1024u;
        D11BindGroup *g = realloc(f->groups, cap * sizeof(*g));
        if (!g) {
            return out;
        }
        f->groups = g;
        f->groupCap = cap;
    }
    D11BindGroup *bg = &f->groups[f->groupCount];
    memset(bg, 0, sizeof(*bg));
    bg->layoutId = desc->layout.id;
    bg->count = l->slotCount;
    bg->dynCount = l->dynCount;
    for (uint32_t i = 0; i < l->slotCount; i++) {
        const RhiBindSlot *s = &l->slots[i];
        D11BindEntry *e = &bg->e[i];
        e->slot = (uint8_t)s->slot;
        e->type = (uint8_t)s->type;
        e->stages = (uint8_t)s->stages;
        e->dynIndex = l->table[i] == D3DP_ROOT_CBV ? l->offset[i] : D11_NO_DYN;
        const RhiBinding *b = d11_find_binding(desc, s);
        if (b) {
            if (!d11_fill_entry(e, s, b)) {
                e->cb = NULL;
                e->srv = NULL;
                e->smp = NULL;
                e->buffer = 0;
            }
        } else {
            /* a slot the shader never reads (a layout shared by several
             * entry points) is harmless: the first is enough to find it */
            static bool s_unboundLogged;
            if (!s_unboundLogged && s->type != RHI_BIND_SAMPLER) {
                s_unboundLogged = true;
                D11_LOG("bind group: slot %u (type %d) has no binding: it reads zeros (reported "
                        "once)",
                        s->slot, (int)s->type);
            }
        }
        if (s->type == RHI_BIND_SAMPLER && !e->smp) {
            e->smp = g_d11.defaultSampler;
        }
    }
    f->groupCount++;
    g_d11.counts.bindGroups++;
    out.id = (d11_frame_tag() << D3DP_GEN_SHIFT) | f->groupCount;
    return out;
}

/* ---------------------------------------------------------------- readback
 * A staging copy of mip 0 (a depth-stencil texture copies whole, as D3D11
 * requires), mapped once the copy is done: Map waits for it, and for
 * every command executed before it. */
bool rhi_readback_texture(RhiTexture h, RhiViewAspect aspect, void *dst, size_t dstSize,
                          uint32_t *outRowPitch)
{
    D11Texture *t = d11_get_texture(h);
    if (!t || !dst || t->samples > 1u || !g_d11.ctx) {
        return false; /* a multisampled texture is resolved first (rhi.h) */
    }
    const D11FormatMap *fm = &d11_formatMap[t->rhiFormat];
    if (aspect == RHI_ASPECT_DEPTH && !fm->depth) {
        return false;
    }
    const uint32_t pitch = t->width * fm->texelBytes;
    const size_t size = (size_t)pitch * t->height;
    if (outRowPitch) {
        *outRowPitch = pitch;
    }
    if (dstSize < size) {
        return false;
    }
    g_d11.counts.readbacks++;
    D3D11_TEXTURE2D_DESC d;
    memset(&d, 0, sizeof(d));
    d.Width = t->width;
    d.Height = t->height;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = t->resFormat;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging = NULL;
    if (!D11_CHECK(ID3D11Device_CreateTexture2D(g_d11.device, &d, NULL, &staging))) {
        return false;
    }
    ID3D11DeviceContext_CopySubresourceRegion(g_d11.ctx, (ID3D11Resource *)staging, 0, 0, 0, 0,
                                              (ID3D11Resource *)t->res, 0, NULL);
    /* the copy waits with the slot's work: the slot's event, ended when
     * rhi_wait_frame leaves the slot, comes after it */
    d11_cur_frame()->eventPending = true;
    D3D11_MAPPED_SUBRESOURCE m;
    memset(&m, 0, sizeof(m));
    const uint64_t t0 = d11_now_ns();
    bool ok = D11_CHECK(
        ID3D11DeviceContext_Map(g_d11.ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m));
    g_d11.fenceWaits++;
    g_d11.fenceWaitNs += d11_now_ns() - t0;
    if (ok) {
        const uint32_t mapTexel = t->swapchain ? 4u : fm->mapTexelBytes;
        for (uint32_t y = 0; y < t->height; y++) {
            uint8_t *row = (uint8_t *)dst + (size_t)y * pitch;
            const uint8_t *src = (const uint8_t *)m.pData + (size_t)y * m.RowPitch;
            if (mapTexel == fm->texelBytes) {
                memcpy(row, src, pitch);
            } else {
                for (uint32_t x = 0; x < t->width; x++) {
                    memcpy(row + (size_t)x * fm->texelBytes, src + (size_t)x * mapTexel,
                           fm->texelBytes);
                }
            }
        }
        ID3D11DeviceContext_Unmap(g_d11.ctx, (ID3D11Resource *)staging, 0);
    }
    ID3D11Texture2D_Release(staging);
    d11_drain_messages();
    return ok;
}

/* ---------------------------------------------------------------- teardown */
void d11_release_all_objects(void)
{
    for (uint32_t i = 0; i < g_d11.buffers.next; i++) {
        D11Buffer *b = d3dp_pool_at(&g_d11.buffers, i);
        if (b) {
            d11_buffer_release(b, true);
        }
    }
    for (uint32_t i = 0; i < g_d11.textures.next; i++) {
        D11Texture *t = d3dp_pool_at(&g_d11.textures, i);
        if (t && !t->swapchain) {
            d11_texture_release(t, true);
        }
    }
    for (uint32_t i = 0; i < g_d11.samplers.next; i++) {
        D11Sampler *s = d3dp_pool_at(&g_d11.samplers, i);
        if (s) {
            D11_RELEASE(s->state);
        }
    }
    for (uint32_t i = 0; i < g_d11.shaders.next; i++) {
        D11Shader *s = d3dp_pool_at(&g_d11.shaders, i);
        if (s) {
            d11_shader_free(s);
        }
    }
}
