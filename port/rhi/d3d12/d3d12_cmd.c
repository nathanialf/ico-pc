/* d3d12_cmd.c: frames, command lists, submission, barriers, render passes,
 * draws, copies and readback for the D3D12 backend.  README.md, "Frame
 * lifecycle" and "States and barriers", describes the synchronisation. */
#include "d3d12_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ fence */
uint64_t dx_Signal(void)
{
    uint64_t v = ++g_dx.fenceValue;
    DX_CHECK(ID3D12CommandQueue_Signal(g_dx.queue, g_dx.fence, v));
    return v;
}

void dx_WaitFence(uint64_t value)
{
    if (value == 0 || !g_dx.fence) {
        return;
    }
    if (ID3D12Fence_GetCompletedValue(g_dx.fence) >= value) {
        return;
    }
    if (DX_CHECK(ID3D12Fence_SetEventOnCompletion(g_dx.fence, value, g_dx.fenceEvent))) {
        WaitForSingleObject(g_dx.fenceEvent, INFINITE);
    }
}

void dx_Transition(ID3D12GraphicsCommandList *cl, ID3D12Resource *res, uint32_t before,
                   uint32_t after)
{
    D3D12_RESOURCE_BARRIER b;
    memset(&b, 0, sizeof(b));
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = (D3D12_RESOURCE_STATES)before;
    b.Transition.StateAfter = (D3D12_RESOURCE_STATES)after;
    ID3D12GraphicsCommandList_ResourceBarrier(cl, 1, &b);
}

/* ------------------------------------------------------------------ frames */
static bool dx_NewList(ID3D12CommandAllocator **alloc, ID3D12GraphicsCommandList **cl)
{
    if (!DX_CHECK(ID3D12Device_CreateCommandAllocator(g_dx.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      &IID_ID3D12CommandAllocator,
                                                      (void **)alloc)) ||
        !DX_CHECK(ID3D12Device_CreateCommandList(g_dx.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 *alloc, NULL, &IID_ID3D12GraphicsCommandList,
                                                 (void **)cl))) {
        return false;
    }
    /* created open; closed until rhi_BeginCommands resets it */
    return DX_CHECK(ID3D12GraphicsCommandList_Close(*cl));
}

bool dx_FramesInit(void)
{
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        DxFrame *f = &g_dx.frames[i];
        for (uint32_t j = 0; j < DX_MAX_CMD_LISTS; j++) {
            if (!dx_NewList(&f->lists[j].alloc, &f->lists[j].cl)) {
                return false;
            }
        }
        d3dp_RingInit(&f->resRing, DX_RES_HEAP_SIZE, 0, RHI_FRAMES_IN_FLIGHT, i);
        d3dp_RingInit(&f->smpRing, DX_SMP_HEAP_SIZE, DX_SMP_RESERVED, RHI_FRAMES_IN_FLIGHT, i);
    }
    g_dx.frameIndex = 0;
    return dx_NewList(&g_dx.oneShotAlloc, &g_dx.oneShotList);
}

void dx_FramesShutdown(void)
{
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        DxFrame *f = &g_dx.frames[i];
        dx_DestroyGarbage(f);
        free(f->garbage);
        free(f->groups);
        for (uint32_t j = 0; j < DX_MAX_CMD_LISTS; j++) {
            if (f->lists[j].cl) {
                ID3D12GraphicsCommandList_Release(f->lists[j].cl);
            }
            if (f->lists[j].alloc) {
                ID3D12CommandAllocator_Release(f->lists[j].alloc);
            }
        }
        memset(f, 0, sizeof(*f));
    }
    if (g_dx.oneShotList) {
        ID3D12GraphicsCommandList_Release(g_dx.oneShotList);
        g_dx.oneShotList = NULL;
    }
    if (g_dx.oneShotAlloc) {
        ID3D12CommandAllocator_Release(g_dx.oneShotAlloc);
        g_dx.oneShotAlloc = NULL;
    }
}

/* Recycles a frame slot once the GPU is done with it. */
static void dx_RecycleFrame(DxFrame *f)
{
    dx_WaitFence(f->fenceValue);
    dx_DestroyGarbage(f);
    for (uint32_t j = 0; j < f->listCount; j++) {
        DX_CHECK(ID3D12CommandAllocator_Reset(f->lists[j].alloc));
    }
    for (uint32_t j = 0; j < DX_MAX_CMD_LISTS; j++) {
        DxCmdList *c = &f->lists[j];
        ID3D12CommandAllocator *a = c->alloc;
        ID3D12GraphicsCommandList *l = c->cl;
        memset(c, 0, sizeof(*c));
        c->alloc = a;
        c->cl = l;
    }
    f->listCount = 0;
    f->groupCount = 0;
    d3dp_RingReset(&f->resRing);
    d3dp_RingReset(&f->smpRing);
    for (uint32_t j = 0; j < f->deadSamplerCount; j++) {
        d3dp_PoolRelease(&g_dx.samplers, f->deadSamplers[j]);
    }
    f->deadSamplerCount = 0;
    memset(f->nullGroups, 0, sizeof(f->nullGroups));
}

void rhi_WaitFrame(void)
{
    g_dx.frameIndex++;
    dx_RecycleFrame(dx_CurFrame());
}

void rhi_WaitIdle(void)
{
    dx_WaitFence(dx_Signal());
    /* The other slots' garbage is now safe to destroy; the current frame's
     * is not (a list recorded but not submitted may still use it). */
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (&g_dx.frames[i] != dx_CurFrame()) {
            dx_DestroyGarbage(&g_dx.frames[i]);
        }
    }
    dx_DrainMessages();
}

/* ---------------------------------------------------------- command lists
 * Handle id: (frame tag << 20) | (list index + 1).  Each list has its own
 * allocator, so several can be recorded at once within a frame. */
DxCmdList *dx_GetCmd(RhiCommandList cl)
{
    DxFrame *f = dx_CurFrame();
    uint32_t idx = cl.id & D3DP_INDEX_MASK;
    if (idx == 0 || idx > f->listCount ||
        (cl.id >> D3DP_GEN_SHIFT) != (uint32_t)(g_dx.frameIndex & D3DP_GEN_MASK)) {
        return NULL;
    }
    return &f->lists[idx - 1];
}

static void dx_BindHeaps(ID3D12GraphicsCommandList *cl)
{
    ID3D12DescriptorHeap *heaps[2] = {g_dx.resHeap, g_dx.smpHeap};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(cl, 2, heaps);
}

RhiCommandList rhi_BeginCommands(void)
{
    RhiCommandList out = {0};
    DxFrame *f = dx_CurFrame();
    if (f->listCount == DX_MAX_CMD_LISTS) {
        DX_LOG("more than %d command lists in one frame", DX_MAX_CMD_LISTS);
        return out;
    }
    DxCmdList *c = &f->lists[f->listCount];
    if (!DX_CHECK(ID3D12GraphicsCommandList_Reset(c->cl, c->alloc, NULL))) {
        return out;
    }
    dx_BindHeaps(c->cl);
    c->recording = true;
    c->serial = ++g_dx.listSerial;
    c->topoSet = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    f->listCount++;
    out.id = ((uint32_t)(g_dx.frameIndex & D3DP_GEN_MASK) << D3DP_GEN_SHIFT) | f->listCount;
    return out;
}

void rhi_EndCommands(RhiCommandList cl)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c || !c->recording) {
        return;
    }
    if (c->inPass) {
        DX_LOG("rhi_EndCommands inside a render pass");
        c->inPass = false;
    }
    DX_CHECK(ID3D12GraphicsCommandList_Close(c->cl));
    c->recording = false;
}

void rhi_Submit(RhiCommandList cl)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c || c->submitted) {
        return;
    }
    if (c->recording) {
        rhi_EndCommands(cl);
    }
    c->submitted = true;
    ID3D12CommandList *lists[1] = {(ID3D12CommandList *)c->cl};
    ID3D12CommandQueue_ExecuteCommandLists(g_dx.queue, 1, lists);
    dx_CurFrame()->fenceValue = dx_Signal();
    dx_DrainMessages();
}

/* ---------------------------------------------------------------- barriers */
void rhi_CmdBarrier(RhiCommandList cl, const RhiTextureBarrier *barriers, uint32_t count)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c) {
        return;
    }
    D3D12_RESOURCE_BARRIER rb[16];
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        DxTexture *t = dx_GetTexture(barriers[i].texture);
        if (!t || barriers[i].before >= RHI_STATE_COUNT || barriers[i].after >= RHI_STATE_COUNT) {
            DX_LOG("rhi_CmdBarrier: invalid barrier %u", i);
            continue;
        }
        uint32_t b = 0, a = 0;
        bool mismatch = false;
        bool emit =
            d3dp_PlanTextureBarrier(barriers[i].before, barriers[i].after,
                                    dx_formatMap[t->rhiFormat].depth, &t->state, &b, &a, &mismatch);
        if (mismatch) {
            DX_LOG("rhi_CmdBarrier: texture %08x: before state %d does not match the tracked "
                   "state 0x%x; using the caller's",
                   barriers[i].texture.id, (int)barriers[i].before, (unsigned)b);
        }
        d3dp_CopyMarkBarrier(&t->copySerial);
        if (!emit) {
            continue;
        }
        if (n == 16) {
            ID3D12GraphicsCommandList_ResourceBarrier(c->cl, n, rb);
            n = 0;
        }
        memset(&rb[n], 0, sizeof(rb[n]));
        rb[n].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb[n].Transition.pResource = t->res;
        rb[n].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rb[n].Transition.StateBefore = (D3D12_RESOURCE_STATES)b;
        rb[n].Transition.StateAfter = (D3D12_RESOURCE_STATES)a;
        n++;
    }
    if (n) {
        ID3D12GraphicsCommandList_ResourceBarrier(c->cl, n, rb);
    }
}

/* ------------------------------------------------------------ render passes
 * D3D12 orders render target and depth writes of successive draws and
 * clears in one state, so a pass after a pass on one target needs nothing
 * (the Vulkan backend's global barrier has no counterpart here). */
void rhi_CmdBeginRenderPass(RhiCommandList cl, const RhiRenderPassDesc *pass)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c || !pass || pass->colorCount > RHI_MAX_COLOR_TARGETS) {
        return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv[RHI_MAX_COLOR_TARGETS];
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        DxTexture *t = dx_GetTexture(pass->color[i].texture);
        if (!t || !t->rtv) {
            DX_LOG("render pass: invalid colour target %u", i);
            return;
        }
        rtv[i] = dx_Cpu(g_dx.rtvCpu, g_dx.rtvInc, t->rtv - 1u);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = {0};
    DxTexture *dt = NULL;
    if (pass->depth.texture.id) {
        dt = dx_GetTexture(pass->depth.texture);
        if (!dt || !dt->dsv) {
            DX_LOG("render pass: invalid depth target");
            return;
        }
        dsv = dx_Cpu(g_dx.dsvCpu, g_dx.dsvInc,
                     (pass->depth.readOnlyDepth ? dt->dsvRO : dt->dsv) - 1u);
    }
    ID3D12GraphicsCommandList_OMSetRenderTargets(
        c->cl, pass->colorCount, pass->colorCount ? rtv : NULL, FALSE, dt ? &dsv : NULL);
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        const RhiColorAttachment *a = &pass->color[i];
        if (a->load != RHI_LOAD_CLEAR) {
            continue;
        }
        DxTexture *t = dx_GetTexture(a->texture);
        float v[4];
        for (int k = 0; k < 4; k++) {
            /* integer targets: the clear value is the integer, rounded as
             * on Vulkan; D3D12 converts the (now integral) float exactly */
            v[k] = dx_formatMap[t->rhiFormat].isInteger ? d3dp_IntClearValue(a->clear[k])
                                                        : a->clear[k];
        }
        ID3D12GraphicsCommandList_ClearRenderTargetView(c->cl, rtv[i], v, 0, NULL);
    }
    if (dt) {
        D3D12_CLEAR_FLAGS flags = 0;
        if (pass->depth.depthLoad == RHI_LOAD_CLEAR) {
            flags |= D3D12_CLEAR_FLAG_DEPTH;
        }
        if (pass->depth.stencilLoad == RHI_LOAD_CLEAR && dx_formatMap[dt->rhiFormat].stencil) {
            flags |= D3D12_CLEAR_FLAG_STENCIL;
        }
        if (flags && pass->depth.readOnlyDepth) {
            DX_LOG("render pass: a read-only depth target cannot be cleared; clear ignored");
        } else if (flags) {
            ID3D12GraphicsCommandList_ClearDepthStencilView(
                c->cl, dsv, flags, pass->depth.clearDepth, pass->depth.clearStencil, 0, NULL);
        }
    }
    c->inPass = true;
    RhiViewport vp = {0.0f, 0.0f, (float)pass->width, (float)pass->height, 0.0f, 1.0f};
    RhiRect sc = {0, 0, pass->width, pass->height};
    rhi_CmdSetViewport(cl, &vp);
    rhi_CmdSetScissor(cl, &sc);
}

void rhi_CmdEndRenderPass(RhiCommandList cl)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (c) {
        c->inPass = false;
    }
}

void rhi_CmdSetViewport(RhiCommandList cl, const RhiViewport *v)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c || !v) {
        return;
    }
    /* D3D's clip space is the RHI's: no flip (the Vulkan backend flips) */
    D3D12_VIEWPORT vp = {v->x, v->y, v->w, v->h, v->minDepth, v->maxDepth};
    ID3D12GraphicsCommandList_RSSetViewports(c->cl, 1, &vp);
}

void rhi_CmdSetScissor(RhiCommandList cl, const RhiRect *r)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c || !r) {
        return;
    }
    D3D12_RECT sc = {r->x, r->y, r->x + (LONG)r->w, r->y + (LONG)r->h};
    ID3D12GraphicsCommandList_RSSetScissorRects(c->cl, 1, &sc);
}

void rhi_CmdSetPipeline(RhiCommandList cl, RhiPipeline p)
{
    DxCmdList *c = dx_GetCmd(cl);
    DxPipeline *pp = d3dp_PoolGet(&g_dx.pipelines, p.id);
    if (!c || !pp) {
        return;
    }
    if (c->pipeline != pp) {
        if (!c->pipeline) {
            c->vbDirty = 0xFFu;
        } else {
            /* strides live in the vertex buffer views */
            for (uint32_t b = 0; b < RHI_MAX_VERTEX_ATTRS; b++) {
                if (c->pipeline->strides[b] != pp->strides[b]) {
                    c->vbDirty |= 1u << b;
                }
            }
        }
        c->pipeline = pp;
        ID3D12GraphicsCommandList_SetPipelineState(c->cl, pp->pso);
    }
}

void rhi_CmdSetBindGroup(RhiCommandList cl, uint32_t group, RhiBindGroup bg)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c || group >= RHI_MAX_BIND_SLOTS) {
        return;
    }
    if (bg.id && !dx_GetBindGroup(bg.id)) {
        DX_LOG("rhi_CmdSetBindGroup: bind group %08x is not from this frame", bg.id);
        bg.id = 0;
    }
    c->groups[group] = bg.id;
    c->groupDirty |= 1u << group;
}

/* Root signature, topology, dirty bind groups and vertex buffers before a
 * draw. */
static void dx_Flush(DxCmdList *c)
{
    DxPipeline *p = c->pipeline;
    DxRootSig *r = p->root;
    if (c->rootSet != r->rs) {
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(c->cl, r->rs);
        c->rootSet = r->rs;
        c->groupDirty = 0xFFu; /* root arguments are reset with the signature */
    }
    if (c->topoSet != p->topo) {
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(c->cl, p->topo);
        c->topoSet = p->topo;
    }
    for (uint32_t g = 0; g < r->layoutCount && c->groupDirty; g++) {
        if (!(c->groupDirty & (1u << g))) {
            continue;
        }
        /* a group the caller has not bound gets null views, so no root
         * parameter is left unset at a draw */
        uint32_t id = c->groups[g] ? c->groups[g] : dx_NullBindGroup(r->layoutIds[g]);
        DxBindGroup *bg = id ? dx_GetBindGroup(id) : NULL;
        if (!bg) {
            continue;
        }
        if (r->resParam[g] >= 0 && bg->hasRes) {
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(c->cl, (UINT)r->resParam[g],
                                                                     bg->res);
        }
        if (r->smpParam[g] >= 0 && bg->hasSmp) {
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(c->cl, (UINT)r->smpParam[g],
                                                                     bg->smp);
        }
    }
    c->groupDirty = 0;
    uint32_t dirty = c->vbDirty & p->bindingMask;
    for (uint32_t b = 0; dirty && b < RHI_MAX_VERTEX_ATTRS; b++) {
        if (!(dirty & (1u << b)) || !c->vbAddr[b]) {
            continue;
        }
        D3D12_VERTEX_BUFFER_VIEW v = {c->vbAddr[b], c->vbSize[b], p->strides[b]};
        ID3D12GraphicsCommandList_IASetVertexBuffers(c->cl, b, 1, &v);
        c->vbDirty &= ~(1u << b);
    }
}

void rhi_CmdSetVertexBuffer(RhiCommandList cl, uint32_t binding, RhiBuffer b, uint64_t offset)
{
    DxCmdList *c = dx_GetCmd(cl);
    DxBuffer *buf = dx_GetBuffer(b);
    if (!c || !buf || binding >= RHI_MAX_VERTEX_ATTRS || offset >= buf->size) {
        return;
    }
    c->vbAddr[binding] = buf->gpu + offset;
    c->vbSize[binding] = (uint32_t)(buf->size - offset);
    c->vbDirty |= 1u << binding;
}

void rhi_CmdSetIndexBuffer(RhiCommandList cl, RhiBuffer b, uint64_t offset, bool u32)
{
    DxCmdList *c = dx_GetCmd(cl);
    DxBuffer *buf = dx_GetBuffer(b);
    if (!c || !buf || offset >= buf->size) {
        return;
    }
    D3D12_INDEX_BUFFER_VIEW v = {buf->gpu + offset, (UINT)(buf->size - offset),
                                 u32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT};
    ID3D12GraphicsCommandList_IASetIndexBuffer(c->cl, &v);
}

void rhi_CmdSetStencilRef(RhiCommandList cl, uint8_t ref)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (c) {
        ID3D12GraphicsCommandList_OMSetStencilRef(c->cl, ref);
    }
}

void rhi_CmdSetBlendConstant(RhiCommandList cl, const float rgba[4])
{
    DxCmdList *c = dx_GetCmd(cl);
    if (c) {
        ID3D12GraphicsCommandList_OMSetBlendFactor(c->cl, rgba);
    }
}

void rhi_CmdDraw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                 uint32_t instanceCount)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c || !c->pipeline) {
        return;
    }
    dx_Flush(c);
    ID3D12GraphicsCommandList_DrawInstanced(c->cl, vertexCount, instanceCount ? instanceCount : 1u,
                                            firstVertex, 0);
}

void rhi_CmdDrawIndexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                        int32_t vertexOffset, uint32_t instanceCount)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c || !c->pipeline) {
        return;
    }
    dx_Flush(c);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(
        c->cl, indexCount, instanceCount ? instanceCount : 1u, firstIndex, vertexOffset, 0);
}

/* ------------------------------------------------------------------ copies */
/* A device buffer about to be written by a copy: to COPY_DEST (this also
 * orders the copy after earlier copies into it in this list). */
static void dx_BufferCopyBegin(DxCmdList *c, DxBuffer *d)
{
    uint32_t b, a;
    if (d->kind == RHI_MEM_DEVICE && d3dp_BufferBeginCopyDst(&d->track, c->serial, &b, &a)) {
        dx_Transition(c->cl, d->res, b, a);
    }
}

/* ...and after: GENERIC_READ, visible to every later read (rhi.h). */
static void dx_BufferCopyEnd(DxCmdList *c, DxBuffer *d)
{
    uint32_t b, a;
    if (d->kind == RHI_MEM_DEVICE) {
        d3dp_BufferEndCopyDst(&d->track, c->serial, &b, &a);
        dx_Transition(c->cl, d->res, b, a);
    }
}

void rhi_CmdCopyBuffer(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset, RhiBuffer dst,
                       uint64_t dstOffset, uint64_t size)
{
    DxCmdList *c = dx_GetCmd(cl);
    DxBuffer *s = dx_GetBuffer(src);
    DxBuffer *d = dx_GetBuffer(dst);
    if (!c || !s || !d || size == 0) {
        return;
    }
    if (d->kind == RHI_MEM_UPLOAD) {
        DX_LOG("rhi_CmdCopyBuffer: an upload buffer cannot be a copy destination");
        return;
    }
    dx_BufferCopyBegin(c, d);
    ID3D12GraphicsCommandList_CopyBufferRegion(c->cl, d->res, dstOffset, s->res, srcOffset, size);
    dx_BufferCopyEnd(c, d);
}

/* Before a copy into a texture: a second copy into it in this list since
 * its last barrier waits for the first (d3dp_CopyNeedsSync). */
static void dx_TextureCopyBegin(DxCmdList *c, DxTexture *t)
{
    if (d3dp_CopyNeedsSync(&t->copySerial, c->serial)) {
        dx_Transition(c->cl, t->res, D3DP_STATE_COPY_DEST, D3DP_STATE_COMMON);
        dx_Transition(c->cl, t->res, D3DP_STATE_COMMON, D3DP_STATE_COPY_DEST);
    }
}

/* The placed footprint of plane `plane` of t for a buffer copy. */
static D3D12_PLACED_SUBRESOURCE_FOOTPRINT dx_Footprint(DxTexture *t, uint32_t plane,
                                                       uint64_t offset, uint32_t w, uint32_t h,
                                                       uint32_t rowPitch)
{
    D3D12_RESOURCE_DESC rd = ID3D12Resource_GetDesc(t->res);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    memset(&fp, 0, sizeof(fp));
    /* the copy format of the plane (R32_TYPELESS for D32F_S8's depth...) */
    ID3D12Device_GetCopyableFootprints(g_dx.device, &rd, d3dp_Subresource(0, plane, t->mips), 1, 0,
                                       &fp, NULL, NULL, NULL);
    fp.Offset = offset;
    fp.Footprint.Width = w;
    fp.Footprint.Height = h;
    fp.Footprint.Depth = 1;
    fp.Footprint.RowPitch = rowPitch;
    return fp;
}

static bool dx_CheckCopyAlign(const char *what, uint64_t offset, uint32_t rowPitch)
{
    if ((offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT) ||
        (rowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT)) {
        DX_LOG("%s: offset %llu / row pitch %u not aligned to rhi_Limits() (512 / 256)", what,
               (unsigned long long)offset, rowPitch);
        return false;
    }
    return true;
}

void rhi_CmdCopyBufferToTexture(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset,
                                uint32_t rowPitch, RhiTexture dst, uint32_t mip, RhiRect region)
{
    DxCmdList *c = dx_GetCmd(cl);
    DxBuffer *s = dx_GetBuffer(src);
    DxTexture *t = dx_GetTexture(dst);
    if (!c || !s || !t || mip >= t->mips ||
        !dx_CheckCopyAlign("rhi_CmdCopyBufferToTexture", srcOffset, rowPitch)) {
        return;
    }
    D3D12_TEXTURE_COPY_LOCATION sl, dl;
    memset(&sl, 0, sizeof(sl));
    memset(&dl, 0, sizeof(dl));
    sl.pResource = s->res;
    sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sl.PlacedFootprint = dx_Footprint(t, 0, srcOffset, region.w, region.h, rowPitch);
    dl.pResource = t->res;
    dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dl.SubresourceIndex = d3dp_Subresource(mip, 0, t->mips);
    dx_TextureCopyBegin(c, t);
    ID3D12GraphicsCommandList_CopyTextureRegion(c->cl, &dl, (UINT)region.x, (UINT)region.y, 0, &sl,
                                                NULL);
}

void rhi_CmdCopyTexture(RhiCommandList cl, RhiTexture src, RhiRect srcRegion, RhiTexture dst,
                        int32_t dstX, int32_t dstY)
{
    DxCmdList *c = dx_GetCmd(cl);
    DxTexture *s = dx_GetTexture(src);
    DxTexture *d = dx_GetTexture(dst);
    if (!c || !s || !d) {
        return;
    }
    dx_TextureCopyBegin(c, d);
    const DxFormatMap *fm = &dx_formatMap[s->rhiFormat];
    D3D12_TEXTURE_COPY_LOCATION sl, dl;
    memset(&sl, 0, sizeof(sl));
    memset(&dl, 0, sizeof(dl));
    sl.pResource = s->res;
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dl.pResource = d->res;
    dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    if (fm->depth) {
        /* depth-stencil resources copy whole subresources, plane by plane
         * (depth, then stencil) */
        if (srcRegion.x || srcRegion.y || dstX || dstY || srcRegion.w != s->width ||
            srcRegion.h != s->height || s->width != d->width || s->height != d->height) {
            DX_LOG("rhi_CmdCopyTexture: depth formats copy whole textures only");
        }
        uint32_t planes = fm->stencil ? 2u : 1u;
        for (uint32_t p = 0; p < planes; p++) {
            sl.SubresourceIndex = d3dp_Subresource(0, p, s->mips);
            dl.SubresourceIndex = d3dp_Subresource(0, p, d->mips);
            ID3D12GraphicsCommandList_CopyTextureRegion(c->cl, &dl, 0, 0, 0, &sl, NULL);
        }
        return;
    }
    D3D12_BOX box = {(UINT)srcRegion.x,
                     (UINT)srcRegion.y,
                     0,
                     (UINT)srcRegion.x + srcRegion.w,
                     (UINT)srcRegion.y + srcRegion.h,
                     1};
    ID3D12GraphicsCommandList_CopyTextureRegion(c->cl, &dl, (UINT)dstX, (UINT)dstY, 0, &sl, &box);
}

void rhi_CmdCopyTextureToBuffer(RhiCommandList cl, RhiTexture src, RhiViewAspect aspect,
                                RhiRect region, RhiBuffer dst, uint64_t dstOffset,
                                uint32_t rowPitch)
{
    DxCmdList *c = dx_GetCmd(cl);
    DxTexture *t = dx_GetTexture(src);
    DxBuffer *d = dx_GetBuffer(dst);
    (void)aspect; /* depth formats copy their depth plane (plane 0) */
    if (!c || !t || !d || d->kind == RHI_MEM_UPLOAD ||
        !dx_CheckCopyAlign("rhi_CmdCopyTextureToBuffer", dstOffset, rowPitch)) {
        return;
    }
    D3D12_TEXTURE_COPY_LOCATION sl, dl;
    memset(&sl, 0, sizeof(sl));
    memset(&dl, 0, sizeof(dl));
    sl.pResource = t->res;
    sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    sl.SubresourceIndex = 0;
    dl.pResource = d->res;
    dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dl.PlacedFootprint = dx_Footprint(t, 0, dstOffset, region.w, region.h, rowPitch);
    D3D12_BOX box = {
        (UINT)region.x, (UINT)region.y, 0, (UINT)region.x + region.w, (UINT)region.y + region.h, 1};
    bool whole = region.x == 0 && region.y == 0 && region.w == t->width && region.h == t->height;
    dx_BufferCopyBegin(c, d);
    ID3D12GraphicsCommandList_CopyTextureRegion(
        c->cl, &dl, 0, 0, 0, &sl, (dx_formatMap[t->rhiFormat].depth && whole) ? NULL : &box);
    dx_BufferCopyEnd(c, d);
}

/* ------------------------------------------------------------ debug labels
 * PIX markers (metadata 0: a UTF-16 string), only with the debug layer. */
void rhi_CmdBeginLabel(RhiCommandList cl, const char *name)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c || !g_dx.debugLayer || !name) {
        return;
    }
    WCHAR w[64];
    int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 64);
    if (n > 0) {
        ID3D12GraphicsCommandList_BeginEvent(c->cl, 0, w, (UINT)(n * sizeof(WCHAR)));
    }
}

void rhi_CmdEndLabel(RhiCommandList cl)
{
    DxCmdList *c = dx_GetCmd(cl);
    if (!c || !g_dx.debugLayer) {
        return;
    }
    ID3D12GraphicsCommandList_EndEvent(c->cl);
}

/* ---------------------------------------------------------------- readback */
bool rhi_ReadbackTexture(RhiTexture h, RhiViewAspect aspect, void *dst, size_t dstSize,
                         uint32_t *outRowPitch)
{
    DxTexture *t = dx_GetTexture(h);
    if (!t || !dst) {
        return false;
    }
    const DxFormatMap *fm = &dx_formatMap[t->rhiFormat];
    if (aspect == RHI_ASPECT_DEPTH && !fm->depth) {
        return false;
    }
    uint32_t pitch = t->width * fm->texelBytes;
    size_t size = (size_t)pitch * t->height;
    if (outRowPitch) {
        *outRowPitch = pitch;
    }
    if (dstSize < size) {
        return false;
    }
    D3D12_RESOURCE_DESC rd = ID3D12Resource_GetDesc(t->res);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    UINT rows = 0;
    UINT64 rowBytes = 0, total = 0;
    ID3D12Device_GetCopyableFootprints(g_dx.device, &rd, 0, 1, 0, &fp, &rows, &rowBytes, &total);
    if (rowBytes != pitch || rows != t->height) {
        DX_LOG("readback: unexpected footprint (%llu bytes x %u rows)",
               (unsigned long long)rowBytes, rows);
        return false;
    }
    RhiBufferDesc bd = {total, RHI_BUF_READBACK, RHI_MEM_READBACK, "readback"};
    RhiBuffer rb = rhi_CreateBuffer(&bd);
    DxBuffer *b = dx_GetBuffer(rb);
    if (!b || !b->mapped) {
        rhi_DestroyBuffer(rb);
        return false;
    }
    bool ok = DX_CHECK(ID3D12CommandAllocator_Reset(g_dx.oneShotAlloc)) &&
              DX_CHECK(ID3D12GraphicsCommandList_Reset(g_dx.oneShotList, g_dx.oneShotAlloc, NULL));
    if (ok) {
        D3D12_TEXTURE_COPY_LOCATION sl, dl;
        memset(&sl, 0, sizeof(sl));
        memset(&dl, 0, sizeof(dl));
        sl.pResource = t->res;
        sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        sl.SubresourceIndex = 0; /* mip 0; plane 0 (depth) of depth formats */
        dl.pResource = b->res;
        dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dl.PlacedFootprint = fp;
        ID3D12GraphicsCommandList_CopyTextureRegion(g_dx.oneShotList, &dl, 0, 0, 0, &sl, NULL);
        ok = DX_CHECK(ID3D12GraphicsCommandList_Close(g_dx.oneShotList));
    }
    if (ok) {
        ID3D12CommandList *lists[1] = {(ID3D12CommandList *)g_dx.oneShotList};
        ID3D12CommandQueue_ExecuteCommandLists(g_dx.queue, 1, lists);
        uint64_t v = dx_Signal();
        dx_CurFrame()->fenceValue = v;
        dx_WaitFence(v);
        const uint8_t *src = (const uint8_t *)b->mapped + fp.Offset;
        for (uint32_t y = 0; y < t->height; y++) {
            memcpy((uint8_t *)dst + (size_t)y * pitch, src + (size_t)y * fp.Footprint.RowPitch,
                   pitch);
        }
    }
    dx_DrainMessages();
    rhi_DestroyBuffer(rb);
    return ok;
}
