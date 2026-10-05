/* vk_cmd.c: frames, command lists, submission, barriers, render passes,
 * draws, copies and readback for the Vulkan backend.  README.md, "Frame
 * lifecycle", describes the synchronisation. */
#include "vk_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ frames */
bool vkr_FramesInit(void)
{
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        VkrFrame *f = &g_vkr.frames[i];
        VkCommandPoolCreateInfo pci = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = g_vkr.queueFamily,
        };
        if (!VKR_CHECK(vkCreateCommandPool(g_vkr.device, &pci, NULL, &f->cmdPool))) {
            return false;
        }
        VkCommandBuffer cbs[VKR_MAX_CMD_LISTS];
        VkCommandBufferAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = f->cmdPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = VKR_MAX_CMD_LISTS,
        };
        if (!VKR_CHECK(vkAllocateCommandBuffers(g_vkr.device, &ai, cbs))) {
            return false;
        }
        for (uint32_t j = 0; j < VKR_MAX_CMD_LISTS; j++) {
            f->lists[j].cb = cbs[j];
        }
        VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (!VKR_CHECK(vkCreateSemaphore(g_vkr.device, &sci, NULL, &f->acquireSem))) {
            return false;
        }
        if (g_vkr.timestamps) {
            VkQueryPoolCreateInfo qci = {
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount = RHI_MAX_TIMESTAMPS,
            };
            if (!VKR_CHECK(vkCreateQueryPool(g_vkr.device, &qci, NULL, &f->queryPool))) {
                f->queryPool = VK_NULL_HANDLE;
                g_vkr.timestamps = false;
            }
        }
    }
    g_vkr.frameIndex = 0;
    return true;
}

void vkr_FramesShutdown(void)
{
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        VkrFrame *f = &g_vkr.frames[i];
        vkr_DestroyGarbage(f);
        free(f->garbage);
        free(f->sets);
        for (uint32_t j = 0; j < f->descPoolCount; j++) {
            vkDestroyDescriptorPool(g_vkr.device, f->descPools[j], NULL);
        }
        if (f->cmdPool) {
            vkDestroyCommandPool(g_vkr.device, f->cmdPool, NULL);
        }
        if (f->acquireSem) {
            vkDestroySemaphore(g_vkr.device, f->acquireSem, NULL);
        }
        if (f->queryPool) {
            vkDestroyQueryPool(g_vkr.device, f->queryPool, NULL);
        }
        memset(f, 0, sizeof(*f));
    }
}

static void vkr_WaitValue(uint64_t value)
{
    if (value == 0) {
        return;
    }
    /* package P1: a wait that blocks is counted with its time */
    uint64_t done = 0;
    if (vkGetSemaphoreCounterValue(g_vkr.device, g_vkr.timeline, &done) == VK_SUCCESS &&
        done >= value) {
        return;
    }
    VkSemaphoreWaitInfo wi = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &g_vkr.timeline,
        .pValues = &value,
    };
    const uint64_t t0 = vkr_NowNs();
    VKR_CHECK(vkWaitSemaphores(g_vkr.device, &wi, UINT64_MAX));
    g_vkr.stats.fenceWaits++;
    g_vkr.stats.fenceWaitNs += vkr_NowNs() - t0;
}

/* package P1: the finished slot's timestamps into g_vkr.tsResult (the slot
 * is complete: nothing waits) */
static void vkr_CollectTimestamps(VkrFrame *f)
{
    g_vkr.tsCount = 0;
    memset(g_vkr.tsResult, 0, sizeof(g_vkr.tsResult));
    if (!f->queryPool || !f->tsWritten) {
        f->tsWritten = 0;
        return;
    }
    uint64_t data[RHI_MAX_TIMESTAMPS][2];
    memset(data, 0, sizeof(data));
    VkResult r = vkGetQueryPoolResults(
        g_vkr.device, f->queryPool, 0, RHI_MAX_TIMESTAMPS, sizeof(data), data, sizeof(data[0]),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (r == VK_SUCCESS || r == VK_NOT_READY) {
        for (uint32_t i = 0; i < RHI_MAX_TIMESTAMPS; i++) {
            if ((f->tsWritten & (1u << i)) && data[i][1]) {
                g_vkr.tsResult[i] =
                    (uint64_t)((double)(data[i][0] & g_vkr.timestampMask) * g_vkr.timestampPeriod);
                g_vkr.tsCount = i + 1;
            }
        }
    }
    f->tsWritten = 0;
}

/* Recycles a frame slot once the GPU is done with it. */
static void vkr_RecycleFrame(VkrFrame *f)
{
    vkr_WaitValue(f->waitValue);
    vkr_CollectTimestamps(f);
    f->tsReset = false;
    vkr_DestroyGarbage(f);
    vkResetCommandPool(g_vkr.device, f->cmdPool, 0);
    for (uint32_t j = 0; j < VKR_MAX_CMD_LISTS; j++) {
        VkCommandBuffer cb = f->lists[j].cb;
        memset(&f->lists[j], 0, sizeof(f->lists[j]));
        f->lists[j].cb = cb;
    }
    f->listCount = 0;
    for (uint32_t j = 0; j < f->descPoolCount; j++) {
        vkResetDescriptorPool(g_vkr.device, f->descPools[j], 0);
    }
    f->descPoolCur = 0;
    f->setCount = 0;
}

void rhi_WaitFrame(void)
{
    g_vkr.frameIndex++;
    vkr_RecycleFrame(vkr_CurFrame());
}

void rhi_WaitIdle(void)
{
    const uint64_t t0 = vkr_NowNs();
    vkDeviceWaitIdle(g_vkr.device);
    g_vkr.stats.waitIdles++;
    g_vkr.stats.fenceWaitNs += vkr_NowNs() - t0;
    /* The other slots' garbage is now safe to destroy.  The current frame's
     * is not: a command list recorded but not yet submitted may still use
     * an object destroyed this frame. */
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (&g_vkr.frames[i] != vkr_CurFrame()) {
            vkr_DestroyGarbage(&g_vkr.frames[i]);
        }
    }
}

/* ---------------------------------------------------------- command lists
 * Handle id: (frame tag << 20) | (list index + 1). */
VkrCmdList *vkr_GetCmd(RhiCommandList cl)
{
    VkrFrame *f = vkr_CurFrame();
    uint32_t idx = cl.id & VKR_INDEX_MASK;
    if (idx == 0 || idx > f->listCount ||
        (cl.id >> VKR_GEN_SHIFT) != (uint32_t)(g_vkr.frameIndex & VKR_GEN_MASK)) {
        return NULL;
    }
    return &f->lists[idx - 1];
}

RhiCommandList rhi_BeginCommands(void)
{
    RhiCommandList out = {0};
    VkrFrame *f = vkr_CurFrame();
    if (f->listCount == VKR_MAX_CMD_LISTS) {
        VKR_LOG("more than %d command lists in one frame", VKR_MAX_CMD_LISTS);
        return out;
    }
    VkrCmdList *c = &f->lists[f->listCount];
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (!VKR_CHECK(vkBeginCommandBuffer(c->cb, &bi))) {
        return out;
    }
    c->recording = true;
    f->listCount++;
    if (f->queryPool && !f->tsReset) {
        /* package P1: the slot's timestamps start unwritten (outside any
         * render pass: the frame's first list) */
        vkCmdResetQueryPool(c->cb, f->queryPool, 0, RHI_MAX_TIMESTAMPS);
        f->tsReset = true;
    }
    out.id = ((uint32_t)(g_vkr.frameIndex & VKR_GEN_MASK) << VKR_GEN_SHIFT) | f->listCount;
    return out;
}

void rhi_EndCommands(RhiCommandList cl)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !c->recording) {
        return;
    }
    if (c->inPass) {
        VKR_LOG("rhi_EndCommands inside a render pass");
        g_vkr.cmdEndRendering(c->cb);
        c->inPass = false;
    }
    /* Copies into READBACK buffers become visible to the host once the
     * timeline value of this submission is reached. */
    VkMemoryBarrier mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    vkCmdPipelineBarrier(c->cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &mb, 0, NULL, 0, NULL);
    VKR_CHECK(vkEndCommandBuffer(c->cb));
    c->recording = false;
}

/* Submits command buffers (possibly none) with the frame's swapchain
 * semaphores attached as needed, signalling the next timeline value. */
static bool vkr_SubmitBatch(const VkCommandBuffer *cbs, uint32_t count, bool forPresent)
{
    VkSemaphore waitSems[2];
    uint64_t waitValues[2];
    VkPipelineStageFlags waitStages[2];
    uint32_t waitCount = 0;
    VkSemaphore sigSems[2];
    uint64_t sigValues[2];
    uint32_t sigCount = 0;

    if (g_vkr.acquireWaitPending) {
        /* the first submit after an acquire waits for the image */
        waitSems[waitCount] = g_vkr.acquireSem;
        waitValues[waitCount] = 0;
        waitStages[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        waitCount++;
        g_vkr.acquireWaitPending = false;
    }
    if (forPresent) {
        /* an empty batch after all frame work: the timeline wait orders it
         * after every earlier submission, and it signals the binary
         * semaphore the present waits on */
        waitSems[waitCount] = g_vkr.timeline;
        waitValues[waitCount] = g_vkr.timelineValue;
        waitStages[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        waitCount++;
        sigSems[sigCount] = g_vkr.renderDone[g_vkr.swapImage];
        sigValues[sigCount] = 0;
        sigCount++;
    }
    uint64_t value = ++g_vkr.timelineValue;
    sigSems[sigCount] = g_vkr.timeline;
    sigValues[sigCount] = value;
    sigCount++;

    VkTimelineSemaphoreSubmitInfo ti = {
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .waitSemaphoreValueCount = waitCount,
        .pWaitSemaphoreValues = waitValues,
        .signalSemaphoreValueCount = sigCount,
        .pSignalSemaphoreValues = sigValues,
    };
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = &ti,
        .waitSemaphoreCount = waitCount,
        .pWaitSemaphores = waitSems,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = count,
        .pCommandBuffers = cbs,
        .signalSemaphoreCount = sigCount,
        .pSignalSemaphores = sigSems,
    };
    if (!VKR_CHECK(vkQueueSubmit(g_vkr.queue, 1, &si, VK_NULL_HANDLE))) {
        return false;
    }
    g_vkr.stats.submits++;
    vkr_CurFrame()->waitValue = value;
    return true;
}

void rhi_Submit(RhiCommandList cl)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || c->submitted) {
        return;
    }
    if (c->recording) {
        rhi_EndCommands(cl);
    }
    c->submitted = true;
    vkr_SubmitBatch(&c->cb, 1, false);
}

bool vkr_SubmitPresentSignal(void)
{
    return vkr_SubmitBatch(NULL, 0, true);
}

/* ---------------------------------------------------------------- barriers */
void vkr_ImageBarrier(VkCommandBuffer cb, VkrTexture *t, RhiState before, RhiState after)
{
    const VkrStateMap *b = &vkr_stateMap[before];
    const VkrStateMap *a = &vkr_stateMap[after];
    VkImageMemoryBarrier ib = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = b->access,
        .dstAccessMask = a->access,
        .oldLayout = vkr_StateLayout(before, t->rhiFormat),
        .newLayout = vkr_StateLayout(after, t->rhiFormat),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = t->image,
        .subresourceRange = {t->aspects, 0, t->mips, 0, 1},
    };
    VkPipelineStageFlags dst = a->stages;
    if (after == RHI_STATE_PRESENT) {
        dst = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    vkCmdPipelineBarrier(cb, b->stages, dst, 0, 0, NULL, 0, NULL, 1, &ib);
    g_vkr.stats.barriers++;
}

void rhi_CmdBarrier(RhiCommandList cl, const RhiTextureBarrier *barriers, uint32_t count)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c) {
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        VkrTexture *t = vkr_GetTexture(barriers[i].texture);
        if (!t || barriers[i].before >= RHI_STATE_COUNT || barriers[i].after >= RHI_STATE_COUNT) {
            VKR_LOG("rhi_CmdBarrier: invalid barrier %u", i);
            continue;
        }
        vkr_ImageBarrier(c->cb, t, barriers[i].before, barriers[i].after);
    }
}

/* Same-state write ordering.  rhi_CmdBarrier is only needed when a texture
 * changes state; two writes in the same state (a copy after a copy into one
 * texture, a render pass after a render pass on one target) are ordered by
 * the backend.  Vulkan gives no implicit ordering across copies or across
 * render passes, so a global memory barrier goes before each copy and each
 * render pass.  It is cheap at this game's scale (tens of passes per frame,
 * copies mostly at load). */
static void vkr_OrderWrites(VkCommandBuffer cb, bool attachments)
{
    VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    VkPipelineStageFlags stages;
    if (attachments) {
        stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VKR_DEPTH_STAGES;
        mb.srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    } else {
        stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    vkCmdPipelineBarrier(cb, stages, stages, 0, 1, &mb, 0, NULL, 0, NULL);
    g_vkr.stats.barriers++;
}

/* ------------------------------------------------------------ render passes */
void rhi_CmdBeginRenderPass(RhiCommandList cl, const RhiRenderPassDesc *pass)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !pass || pass->colorCount > RHI_MAX_COLOR_TARGETS) {
        return;
    }
    VkRenderingAttachmentInfo color[RHI_MAX_COLOR_TARGETS];
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        const RhiColorAttachment *a = &pass->color[i];
        VkrTexture *t = vkr_GetTexture(a->texture);
        if (!t) {
            VKR_LOG("render pass: invalid colour target %u", i);
            return;
        }
        VkClearValue cv;
        if (vkr_formatMap[t->rhiFormat].isInteger) {
            /* integer targets clear with integer values: the float clear
             * colour is taken as the value per channel (0..255) */
            for (int k = 0; k < 4; k++) {
                float v = a->clear[k];
                cv.color.uint32[k] = v <= 0.0f ? 0u : (uint32_t)(v + 0.5f);
            }
        } else {
            memcpy(cv.color.float32, a->clear, sizeof(cv.color.float32));
        }
        color[i] = (VkRenderingAttachmentInfo){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = t->view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = vkr_loadOpMap[a->load < RHI_LOAD_COUNT ? a->load : 0].vk,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = cv,
        };
    }
    VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingAttachmentInfo stencil = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    bool hasDepth = false, hasStencil = false;
    if (pass->depth.texture.id) {
        VkrTexture *t = vkr_GetTexture(pass->depth.texture);
        if (!t) {
            VKR_LOG("render pass: invalid depth target");
            return;
        }
        VkImageLayout layout = pass->depth.readOnlyDepth
                                   ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                   : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkClearValue cv = {.depthStencil = {pass->depth.clearDepth, pass->depth.clearStencil}};
        hasDepth = true;
        depth.imageView = t->view;
        depth.imageLayout = layout;
        depth.loadOp =
            vkr_loadOpMap[pass->depth.depthLoad < RHI_LOAD_COUNT ? pass->depth.depthLoad : 0].vk;
        depth.storeOp =
            pass->depth.readOnlyDepth ? VK_ATTACHMENT_STORE_OP_NONE : VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue = cv;
        if (t->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
            hasStencil = true;
            stencil = depth;
            stencil.loadOp =
                vkr_loadOpMap[pass->depth.stencilLoad < RHI_LOAD_COUNT ? pass->depth.stencilLoad
                                                                       : 0]
                    .vk;
        }
        if (g_vkr.apiVersion < VK_API_VERSION_1_3 && pass->depth.readOnlyDepth) {
            /* STORE_OP_NONE is core in 1.3 only */
            depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            stencil.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
    }
    VkRenderingInfo ri = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {pass->width, pass->height}},
        .layerCount = 1,
        .colorAttachmentCount = pass->colorCount,
        .pColorAttachments = color,
        .pDepthAttachment = hasDepth ? &depth : NULL,
        .pStencilAttachment = hasStencil ? &stencil : NULL,
    };
    vkr_OrderWrites(c->cb, true);
    g_vkr.cmdBeginRendering(c->cb, &ri);
    c->inPass = true;
    g_vkr.stats.renderPasses++;

    /* defaults: full-target viewport and scissor */
    RhiViewport vp = {0.0f, 0.0f, (float)pass->width, (float)pass->height, 0.0f, 1.0f};
    RhiRect sc = {0, 0, pass->width, pass->height};
    rhi_CmdSetViewport(cl, &vp);
    rhi_CmdSetScissor(cl, &sc);
}

void rhi_CmdEndRenderPass(RhiCommandList cl)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !c->inPass) {
        return;
    }
    g_vkr.cmdEndRendering(c->cb);
    c->inPass = false;
}

void rhi_CmdSetViewport(RhiCommandList cl, const RhiViewport *v)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !v) {
        return;
    }
    /* Negative height (core since Vulkan 1.1) gives D3D's y-up NDC, so the
     * same HLSL runs on both backends without -fvk-invert-y. */
    VkViewport vp = {v->x, v->y + v->h, v->w, -v->h, v->minDepth, v->maxDepth};
    vkCmdSetViewport(c->cb, 0, 1, &vp);
}

void rhi_CmdSetScissor(RhiCommandList cl, const RhiRect *r)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !r) {
        return;
    }
    VkRect2D sc = {{r->x, r->y}, {r->w, r->h}};
    vkCmdSetScissor(c->cb, 0, 1, &sc);
}

void rhi_CmdSetPipeline(RhiCommandList cl, RhiPipeline p)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    VkrPipeline *pp = vkr_PoolGet(&g_vkr.pipelines, p.id);
    if (!c || !pp) {
        return;
    }
    if (c->pipeline != pp) {
        if (!c->pipeline || c->pipeline->layout != pp->layout) {
            c->groupDirty = 0xFFu; /* rebind against the new layout */
        }
        c->pipeline = pp;
        vkCmdBindPipeline(c->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pp->pipeline);
        g_vkr.stats.pipelineBinds++;
    }
}

void rhi_CmdSetBindGroup(RhiCommandList cl, uint32_t group, RhiBindGroup bg)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || group >= RHI_MAX_BIND_SLOTS) {
        return;
    }
    VkDescriptorSet s = vkr_GetBindGroup(bg);
    if (!s && bg.id) {
        VKR_LOG("rhi_CmdSetBindGroup: bind group %08x is not from this frame", bg.id);
    }
    if (c->groups[group] != s) {
        /* package P1: the set already bound is not bound again */
        c->groups[group] = s;
        c->groupDirty |= 1u << group;
    }
}

/* Binds the dirty groups against the current pipeline's layout. */
static void vkr_FlushBindGroups(VkrCmdList *c)
{
    if (!c->pipeline || !c->groupDirty) {
        return;
    }
    for (uint32_t g = 0; g < c->pipeline->layoutCount; g++) {
        if ((c->groupDirty & (1u << g)) && c->groups[g]) {
            vkCmdBindDescriptorSets(c->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c->pipeline->layout, g,
                                    1, &c->groups[g], 0, NULL);
            g_vkr.stats.bindGroupBinds++;
        }
    }
    c->groupDirty = 0;
}

void rhi_CmdSetVertexBuffer(RhiCommandList cl, uint32_t binding, RhiBuffer b, uint64_t offset)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    VkrBuffer *buf = vkr_GetBuffer(b);
    if (!c || !buf) {
        return;
    }
    VkDeviceSize off = offset;
    vkCmdBindVertexBuffers(c->cb, binding, 1, &buf->buffer, &off);
}

void rhi_CmdSetIndexBuffer(RhiCommandList cl, RhiBuffer b, uint64_t offset, bool u32)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    VkrBuffer *buf = vkr_GetBuffer(b);
    if (!c || !buf) {
        return;
    }
    vkCmdBindIndexBuffer(c->cb, buf->buffer, offset,
                         u32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

void rhi_CmdSetStencilRef(RhiCommandList cl, uint8_t ref)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (c) {
        vkCmdSetStencilReference(c->cb, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
    }
}

void rhi_CmdSetBlendConstant(RhiCommandList cl, const float rgba[4])
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (c) {
        vkCmdSetBlendConstants(c->cb, rgba);
    }
}

void rhi_CmdDraw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                 uint32_t instanceCount)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !c->pipeline) {
        return;
    }
    vkr_FlushBindGroups(c);
    vkCmdDraw(c->cb, vertexCount, instanceCount ? instanceCount : 1u, firstVertex, 0);
    g_vkr.stats.draws++;
}

void rhi_CmdDrawIndexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                        int32_t vertexOffset, uint32_t instanceCount)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !c->pipeline) {
        return;
    }
    vkr_FlushBindGroups(c);
    vkCmdDrawIndexed(c->cb, indexCount, instanceCount ? instanceCount : 1u, firstIndex,
                     vertexOffset, 0);
    g_vkr.stats.draws++;
}

/* ------------------------------------------------------------------ copies */
void rhi_CmdCopyBuffer(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset, RhiBuffer dst,
                       uint64_t dstOffset, uint64_t size)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    VkrBuffer *s = vkr_GetBuffer(src);
    VkrBuffer *d = vkr_GetBuffer(dst);
    if (!c || !s || !d || size == 0) {
        return;
    }
    VkBufferCopy r = {srcOffset, dstOffset, size};
    /* package P1: rhi.h: the copy waits for every earlier read of the
     * destination (draws of earlier frames reading a range rewritten now)
     * and write (an earlier copy) */
    VkMemoryBarrier pre = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(c->cb,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &pre, 0, NULL, 0, NULL);
    g_vkr.stats.barriers++;
    vkCmdCopyBuffer(c->cb, s->buffer, d->buffer, 1, &r);
    g_vkr.stats.copies++;
    /* rhi.h: buffer copies are visible to every later read */
    VkMemoryBarrier mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
                         VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                         VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(c->cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &mb, 0, NULL, 0, NULL);
    g_vkr.stats.barriers++;
}

static VkImageAspectFlags vkr_CopyAspect(const VkrTexture *t, RhiViewAspect aspect)
{
    if (t->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
        (void)aspect; /* depth formats copy their depth aspect */
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

void rhi_CmdCopyBufferToTexture(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset,
                                uint32_t rowPitch, RhiTexture dst, uint32_t mip, RhiRect region)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    VkrBuffer *s = vkr_GetBuffer(src);
    VkrTexture *t = vkr_GetTexture(dst);
    if (!c || !s || !t) {
        return;
    }
    uint32_t texel = vkr_formatMap[t->rhiFormat].texelBytes;
    VkBufferImageCopy r = {
        .bufferOffset = srcOffset,
        .bufferRowLength = texel ? rowPitch / texel : 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vkr_CopyAspect(t, RHI_ASPECT_COLOR), mip, 0, 1},
        .imageOffset = {region.x, region.y, 0},
        .imageExtent = {region.w, region.h, 1},
    };
    vkr_OrderWrites(c->cb, false);
    vkCmdCopyBufferToImage(c->cb, s->buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    g_vkr.stats.copies++;
}

void rhi_CmdCopyTexture(RhiCommandList cl, RhiTexture src, RhiRect srcRegion, RhiTexture dst,
                        int32_t dstX, int32_t dstY)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    VkrTexture *s = vkr_GetTexture(src);
    VkrTexture *d = vkr_GetTexture(dst);
    if (!c || !s || !d) {
        return;
    }
    VkImageCopy r = {
        .srcSubresource = {s->aspects, 0, 0, 1},
        .srcOffset = {srcRegion.x, srcRegion.y, 0},
        .dstSubresource = {d->aspects, 0, 0, 1},
        .dstOffset = {dstX, dstY, 0},
        .extent = {srcRegion.w, srcRegion.h, 1},
    };
    vkr_OrderWrites(c->cb, false);
    vkCmdCopyImage(c->cb, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    g_vkr.stats.copies++;
}

void rhi_CmdCopyTextureToBuffer(RhiCommandList cl, RhiTexture src, RhiViewAspect aspect,
                                RhiRect region, RhiBuffer dst, uint64_t dstOffset,
                                uint32_t rowPitch)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    VkrTexture *t = vkr_GetTexture(src);
    VkrBuffer *d = vkr_GetBuffer(dst);
    if (!c || !t || !d) {
        return;
    }
    uint32_t texel = vkr_formatMap[t->rhiFormat].texelBytes;
    VkBufferImageCopy r = {
        .bufferOffset = dstOffset,
        .bufferRowLength = texel ? rowPitch / texel : 0,
        .imageSubresource = {vkr_CopyAspect(t, aspect), 0, 0, 1},
        .imageOffset = {region.x, region.y, 0},
        .imageExtent = {region.w, region.h, 1},
    };
    vkr_OrderWrites(c->cb, false);
    vkCmdCopyImageToBuffer(c->cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->buffer, 1, &r);
    g_vkr.stats.copies++;
}

/* ------------------------------------------------------------ debug labels */
void rhi_CmdBeginLabel(RhiCommandList cl, const char *name)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !g_vkr.debugUtils || !vkCmdBeginDebugUtilsLabelEXT) {
        return;
    }
    VkDebugUtilsLabelEXT l = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = name};
    vkCmdBeginDebugUtilsLabelEXT(c->cb, &l);
}

void rhi_CmdEndLabel(RhiCommandList cl)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !g_vkr.debugUtils || !vkCmdEndDebugUtilsLabelEXT) {
        return;
    }
    vkCmdEndDebugUtilsLabelEXT(c->cb);
}

/* ---------------------------------------------------------------- readback */
bool rhi_ReadbackTexture(RhiTexture h, RhiViewAspect aspect, void *dst, size_t dstSize,
                         uint32_t *outRowPitch)
{
    VkrTexture *t = vkr_GetTexture(h);
    if (!t || !dst) {
        return false;
    }
    if (aspect == RHI_ASPECT_DEPTH && !(t->aspects & VK_IMAGE_ASPECT_DEPTH_BIT)) {
        return false;
    }
    uint32_t texel = vkr_formatMap[t->rhiFormat].texelBytes;
    uint32_t pitch = t->width * texel;
    size_t size = (size_t)pitch * t->height;
    if (outRowPitch) {
        *outRowPitch = pitch;
    }
    if (dstSize < size) {
        return false;
    }
    g_vkr.stats.readbacks++;
    RhiBufferDesc bd = {size, RHI_BUF_READBACK, RHI_MEM_READBACK, "readback"};
    RhiBuffer rb = rhi_CreateBuffer(&bd);
    VkrBuffer *b = vkr_GetBuffer(rb);
    if (!b) {
        return false;
    }
    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_vkr.oneShotPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb;
    bool ok = VKR_CHECK(vkAllocateCommandBuffers(g_vkr.device, &ai, &cb));
    if (ok) {
        VkCommandBufferBeginInfo bi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(cb, &bi);
        /* earlier submissions' writes to the image (render, copy) must be
         * visible to this copy */
        VkMemoryBarrier mb0 = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 1, &mb0, 0, NULL, 0, NULL);
        VkBufferImageCopy r = {
            .bufferOffset = 0,
            .bufferRowLength = t->width,
            .imageSubresource = {vkr_CopyAspect(t, aspect), 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {t->width, t->height, 1},
        };
        vkCmdCopyImageToBuffer(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b->buffer, 1,
                               &r);
        VkMemoryBarrier mb = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                             &mb, 0, NULL, 0, NULL);
        vkEndCommandBuffer(cb);
        uint64_t value = ++g_vkr.timelineValue;
        VkTimelineSemaphoreSubmitInfo ti = {
            .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .signalSemaphoreValueCount = 1,
            .pSignalSemaphoreValues = &value,
        };
        VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &ti,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &g_vkr.timeline,
        };
        ok = VKR_CHECK(vkQueueSubmit(g_vkr.queue, 1, &si, VK_NULL_HANDLE));
        if (ok) {
            vkr_CurFrame()->waitValue = value;
            vkr_WaitValue(value);
            memcpy(dst, rhi_MapBuffer(rb), size);
        }
        vkFreeCommandBuffers(g_vkr.device, g_vkr.oneShotPool, 1, &cb);
    }
    rhi_DestroyBuffer(rb);
    return ok;
}

/* ------------------------------------------------------- timestamps (P1) */
void rhi_CmdWriteTimestamp(RhiCommandList cl, uint32_t index)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    VkrFrame *f = vkr_CurFrame();
    if (!c || !f->queryPool || !f->tsReset || index >= RHI_MAX_TIMESTAMPS ||
        (f->tsWritten & (1u << index))) {
        return;
    }
    vkCmdWriteTimestamp(c->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f->queryPool, index);
    f->tsWritten |= 1u << index;
}

uint32_t rhi_ReadTimestamps(uint64_t *ns, uint32_t max)
{
    if (!ns || !g_vkr.timestamps) {
        return 0;
    }
    const uint32_t n = g_vkr.tsCount < max ? g_vkr.tsCount : max;
    memcpy(ns, g_vkr.tsResult, (size_t)n * sizeof(uint64_t));
    return n;
}
