/* vk_cmd.c: frames, command lists, submission, barriers, render passes,
 * draws, copies and readback for the Vulkan backend.  README.md, "Frame
 * lifecycle", describes the synchronisation. */
#include "vk_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ frames */
bool vkr_FramesInit(void)
{
    /* package PB: main's global barrier before every pass and copy, for A/B
     * comparisons and bisecting ("Hazards" below) */
    const char *gb = getenv("ICO_VK_GLOBAL_BARRIERS");
    g_vkr.globalBarriers = gb && gb[0] && gb[0] != '0';
    if (g_vkr.globalBarriers) {
        VKR_LOG("ICO_VK_GLOBAL_BARRIERS: a global barrier before every render pass and copy");
    }
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
        free(f->setDynamic);
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

uint32_t rhi_FrameSlot(void)
{
    return (uint32_t)(g_vkr.frameIndex % RHI_FRAMES_IN_FLIGHT);
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

static void vkr_OpenBarrier(VkCommandBuffer cb);
static void vkr_FlushBarriers(VkrCmdList *c);

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
    c->pendCount = 0;
    c->pendSrc = c->pendDst = 0;
    c->presentImage = VK_NULL_HANDLE;
    /* package PB: a new hazard-tracking epoch ("Hazards") */
    c->epoch = ++g_vkr.hzEpoch;
    c->globalOrder = g_vkr.globalBarriers;
    for (uint32_t j = 0; j < f->listCount; j++) {
        if (f->lists[j].recording) {
            /* recorded interleaved: tracking follows one list's order, so
             * both lists fall back to the global barriers */
            f->lists[j].globalOrder = true;
            c->globalOrder = true;
        }
    }
    if (!c->globalOrder) {
        vkr_OpenBarrier(c->cb);
    }
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
    vkr_FlushBarriers(c);
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
 * semaphores attached as needed, signalling the next timeline value.
 * forPresent: the batch also signals the acquired image's present
 * semaphore (renderDone), either as an empty batch after all frame work
 * (vkr_SubmitPresentSignal) or as the submit of the list that moved the
 * image to PRESENT (rhi_Submit). */
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
        /* The first submit after an acquire waits for the image.  With
         * commands, at COLOR_ATTACHMENT_OUTPUT: the image's first use after
         * an acquire is a barrier out of UNDEFINED or PRESENT, and such a
         * barrier on a swapchain image has that stage in its first scope
         * (vkr_ImageBarrier), which chains it to this wait in this submit
         * or a later one; the work before it, on other images, need not
         * wait for the presentation engine.  An empty batch keeps
         * ALL_COMMANDS. */
        waitSems[waitCount] = g_vkr.acquireSem;
        waitValues[waitCount] = 0;
        waitStages[waitCount] = count ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                      : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        waitCount++;
        g_vkr.acquireWaitPending = false;
    }
    if (forPresent) {
        if (count == 0) {
            /* an empty batch after all frame work: the timeline wait
             * orders it after every earlier submission */
            waitSems[waitCount] = g_vkr.timeline;
            waitValues[waitCount] = g_vkr.timelineValue;
            waitStages[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            waitCount++;
        }
        /* the binary semaphore the present waits on; a semaphore signal
         * covers the batch's commands and every earlier submission */
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
        /* nothing will signal the value: a later wait on it (the present
         * batch's, vkr_WaitValue) would never return */
        g_vkr.timelineValue--;
        return false;
    }
    g_vkr.stats.submits++;
    vkr_CurFrame()->waitValue = value;
    return true;
}

/* Submits nothing but the wait on a pending acquire semaphore, then waits
 * for it: a swapchain recreate drops acquireWaitPending, and the semaphore
 * must not stay signalled for the frame's next acquire. */
bool vkr_SubmitEmpty(void)
{
    if (!g_vkr.acquireWaitPending) {
        return true;
    }
    if (!vkr_SubmitBatch(NULL, 0, false)) {
        return false;
    }
    vkr_WaitValue(g_vkr.timelineValue);
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
    /* One submit per present: the list that moves the acquired image to
     * PRESENT signals the present semaphore itself, and rhi_Present adds
     * no empty submit.  Any other list (and a presenting list after the
     * semaphore was signalled) is submitted alone, and rhi_Present
     * signals the semaphore with an empty batch after it, as before. */
    bool signal = false;
    if (c->presentImage != VK_NULL_HANDLE && g_vkr.swapAcquired && !g_vkr.presentSignalled) {
        const VkrTexture *bb = vkr_GetTexture((RhiTexture){g_vkr.swapTextures[g_vkr.swapImage]});
        signal = bb && bb->image == c->presentImage;
    }
    if (vkr_SubmitBatch(&c->cb, 1, signal) && signal) {
        g_vkr.presentSignalled = true;
    }
}

bool vkr_SubmitPresentSignal(void)
{
    return vkr_SubmitBatch(NULL, 0, true);
}

/* ---------------------------------------------------------------- barriers
 * Image barriers are deferred.  vkr_ImageBarrier adds each one to the
 * list's pending set (VkrCmdList.pendImg) instead of recording it, and the
 * set goes out as one vkCmdPipelineBarrier, its stage masks OR-ed, before
 * the next command of any kind: every recording entry point below calls
 * vkr_FlushBarriers first, and a render pass or a copy puts the set into
 * the barrier of its own hazards (vkr_HzTakePending).  So every command
 * sees the same barriers it saw when each went out in a call of its own;
 * the wider stage masks only add waits.  A second barrier on an image
 * already in the set sends the set first (two layout transitions of one
 * image in one call are not ordered), and so does a full set.  A list on
 * the global barrier path (ICO_VK_GLOBAL_BARRIERS=1, or recorded
 * interleaved: vkr_OrderWrites) defers nothing and records each barrier
 * at once, as before.  stats.barriers counts vkCmdPipelineBarrier calls. */
static void vkr_FlushBarriers(VkrCmdList *c)
{
    if (c->pendCount == 0) {
        return;
    }
    vkCmdPipelineBarrier(c->cb, c->pendSrc, c->pendDst, 0, 0, NULL, 0, NULL, c->pendCount,
                         c->pendImg);
    g_vkr.stats.barriers++;
    c->pendCount = 0;
    c->pendSrc = c->pendDst = 0;
}

void vkr_ImageBarrier(VkrCmdList *c, VkrTexture *t, RhiState before, RhiState after)
{
    const VkrStateMap *b = &vkr_stateMap[before];
    const VkrStateMap *a = &vkr_stateMap[after];
    VkPipelineStageFlags src = b->stages;
    VkAccessFlags srcAccess = b->access;
    if (t->swapchain && (before == RHI_STATE_UNDEFINED || before == RHI_STATE_PRESENT)) {
        /* a swapchain image's first barrier after an acquire: the submit
         * waits on the acquire semaphore at COLOR_ATTACHMENT_OUTPUT
         * (vkr_SubmitBatch), and this stage in the first scope makes the
         * transition wait for it */
        src = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    if (t->hzEpoch == c->epoch) {
        /* package PB: the uses still pending on the image join the first
         * scope (they are the before state's own, unless the caller
         * discards the contents with before = UNDEFINED) */
        const bool depth = (t->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
        if (t->hzXferMips) {
            src |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            srcAccess |= VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        if (t->hzAttach) {
            src |= depth ? VKR_DEPTH_STAGES : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            if (t->hzAttach & VKR_HZ_ATTACH_WRITE) {
                srcAccess |= depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                   : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            }
        }
        if (src != VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT) {
            src &= ~(VkPipelineStageFlags)VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }
    }
    VkImageMemoryBarrier ib = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = srcAccess,
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
        if (t->swapchain) {
            c->presentImage = t->image; /* rhi_Submit */
        }
    }
    for (uint32_t i = 0; i < c->pendCount; i++) {
        if (c->pendImg[i].image == t->image) {
            vkr_FlushBarriers(c);
            break;
        }
    }
    if (c->pendCount == VKR_PENDING_BARRIERS) {
        vkr_FlushBarriers(c);
    }
    c->pendImg[c->pendCount++] = ib;
    c->pendSrc |= src;
    c->pendDst |= dst;
    if (c->globalOrder) {
        vkr_FlushBarriers(c);
    }
    /* package PB: the barrier covers whatever was pending on the image */
    t->hzEpoch = 0;
    t->hzXferMips = 0;
    t->hzAttach = 0;
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
        vkr_ImageBarrier(c, t, barriers[i].before, barriers[i].after);
    }
}

/* Same-state write ordering.  rhi_CmdBarrier is only needed when a texture
 * changes state; two writes in the same state (a copy after a copy into one
 * texture, a render pass after a render pass on one target) are ordered by
 * the backend.  Vulkan gives no implicit ordering across copies or across
 * render passes.
 *
 * vkr_OrderWrites is main's way, kept for ICO_VK_GLOBAL_BARRIERS=1 and for
 * lists recorded interleaved: a global memory barrier before every copy and
 * every render pass. */
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

/* Hazards (package PB; README.md, "States and barriers").  Each texture and
 * buffer records what touched it since its last barrier: the mips a copy
 * wrote, an attachment write or a read-only depth attachment read (a
 * buffer: a copy's write or read).  A render pass or copy emits one
 * pipeline barrier holding an image (buffer) barrier for each resource it
 * uses that has a pending hazard with that use (write after write, read
 * after write, write after read), in the layout the use needs (old = new:
 * layouts change only at the caller's rhi_CmdBarrier, which clears the
 * record).  Anything else needs no barrier: sampling, copying from and
 * presenting all need a state change, and that transition is the caller's.
 *
 * The record belongs to one command list (its epoch).  Each list begins
 * with one global barrier over attachment and transfer writes
 * (vkr_OpenBarrier), which orders it after every list submitted before it,
 * this frame's and the frames' still in flight, so a record from an older
 * list counts as clean and nothing is carried across submissions or frame
 * slots.  Lists recorded interleaved fall back to vkr_OrderWrites. */
static void vkr_OpenBarrier(VkCommandBuffer cb)
{
    const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                        VKR_DEPTH_STAGES | VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkMemoryBarrier mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(cb, stages, stages, 0, 1, &mb, 0, NULL, 0, NULL);
    g_vkr.stats.barriers++;
}

/* the barriers one pass or copy needs, emitted as one vkCmdPipelineBarrier */
typedef struct VkrHz {
    VkImageMemoryBarrier img[VKR_PENDING_BARRIERS + RHI_MAX_COLOR_TARGETS + 2];
    VkBufferMemoryBarrier buf[2];
    uint32_t imgCount, bufCount;
    VkPipelineStageFlags src, dst;
} VkrHz;

/* The list's deferred image barriers into h, so they go out in the call h
 * makes (vkr_HzFlush) with the hazards' own barriers.  None of their
 * images is among those hazards: a deferred barrier cleared its image's
 * record (vkr_ImageBarrier), and vkr_HzImage adds nothing for a cleared
 * record. */
static void vkr_HzTakePending(VkrHz *h, VkrCmdList *c)
{
    for (uint32_t i = 0; i < c->pendCount; i++) {
        h->img[h->imgCount++] = c->pendImg[i];
    }
    h->src |= c->pendSrc;
    h->dst |= c->pendDst;
    c->pendCount = 0;
    c->pendSrc = c->pendDst = 0;
}

static void vkr_HzTouch(const VkrCmdList *c, VkrTexture *t)
{
    if (t->hzEpoch != c->epoch) {
        t->hzEpoch = c->epoch;
        t->hzXferMips = 0;
        t->hzAttach = 0;
    }
}

/* A use of t in `layout` by dstStages/dstAccess; writeMips: the mips the
 * use writes (0: a read). */
static void vkr_HzImage(VkrHz *h, const VkrCmdList *c, VkrTexture *t, VkImageLayout layout,
                        VkPipelineStageFlags dstStages, VkAccessFlags dstAccess, uint32_t writeMips)
{
    if (t->hzEpoch != c->epoch) {
        return; /* behind the list's opening barrier */
    }
    const bool need = (t->hzXferMips & (writeMips ? writeMips : ~0u)) != 0 ||
                      (t->hzAttach & VKR_HZ_ATTACH_WRITE) ||
                      (writeMips && (t->hzAttach & VKR_HZ_ATTACH_READ));
    if (!need) {
        return;
    }
    const bool depth = (t->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
    VkPipelineStageFlags src = 0;
    VkAccessFlags srcAccess = 0;
    if (t->hzXferMips) {
        src |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        srcAccess |= VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    if (t->hzAttach) {
        src |= depth ? VKR_DEPTH_STAGES : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (t->hzAttach & VKR_HZ_ATTACH_WRITE) {
            srcAccess |= depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                               : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
    }
    h->img[h->imgCount++] = (VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
        .oldLayout = layout,
        .newLayout = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = t->image,
        .subresourceRange = {t->aspects, 0, t->mips, 0, 1},
    };
    h->src |= src;
    h->dst |= dstStages;
    t->hzXferMips = 0;
    t->hzAttach = 0;
}

/* A copy's use of a buffer: write (a copy's destination) or read. */
static void vkr_HzBuffer(VkrHz *h, const VkrCmdList *c, VkrBuffer *b, bool write)
{
    if (b->hzEpoch != c->epoch) {
        b->hzEpoch = c->epoch;
        b->hzXferWrite = false;
        b->hzXferRead = false;
    }
    if (b->hzXferWrite || (write && b->hzXferRead)) {
        h->buf[h->bufCount++] = (VkBufferMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = b->hzXferWrite ? VK_ACCESS_TRANSFER_WRITE_BIT : 0,
            .dstAccessMask = write ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = b->buffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        h->src |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        h->dst |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        b->hzXferWrite = false;
        b->hzXferRead = false;
    }
    if (write) {
        b->hzXferWrite = true;
    } else {
        b->hzXferRead = true;
    }
}

static void vkr_HzFlush(VkCommandBuffer cb, const VkrHz *h)
{
    if (h->imgCount + h->bufCount == 0) {
        return;
    }
    vkCmdPipelineBarrier(cb, h->src, h->dst, 0, 0, NULL, h->bufCount, h->buf, h->imgCount, h->img);
    g_vkr.stats.barriers++;
}

static uint32_t vkr_MipBit(uint32_t mip)
{
    return mip < 32 ? 1u << mip : 0x80000000u;
}

/* A copy from srcTex or srcBuf into dstTex (mip dstMip) or dstBuf. */
static void vkr_HzCopy(VkrCmdList *c, VkrTexture *srcTex, VkrBuffer *srcBuf, VkrTexture *dstTex,
                       uint32_t dstMip, VkrBuffer *dstBuf)
{
    if (c->globalOrder) {
        vkr_FlushBarriers(c);
        vkr_OrderWrites(c->cb, false);
        return;
    }
    VkrHz h;
    h.imgCount = h.bufCount = 0;
    h.src = h.dst = 0;
    vkr_HzTakePending(&h, c);
    if (srcTex) {
        vkr_HzImage(&h, c, srcTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0);
    }
    if (dstTex) {
        vkr_HzImage(&h, c, dstTex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    vkr_MipBit(dstMip));
    }
    if (srcBuf) {
        vkr_HzBuffer(&h, c, srcBuf, false);
    }
    if (dstBuf) {
        vkr_HzBuffer(&h, c, dstBuf, true);
    }
    vkr_HzFlush(c->cb, &h);
    if (dstTex) {
        vkr_HzTouch(c, dstTex);
        dstTex->hzXferMips |= vkr_MipBit(dstMip);
    }
}

/* ------------------------------------------------------------ render passes */
void rhi_CmdBeginRenderPass(RhiCommandList cl, const RhiRenderPassDesc *pass)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !pass || pass->colorCount > RHI_MAX_COLOR_TARGETS) {
        return;
    }
    VkRenderingAttachmentInfo color[RHI_MAX_COLOR_TARGETS];
    VkrTexture *colorTex[RHI_MAX_COLOR_TARGETS];
    VkrTexture *depthTex = NULL;
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        const RhiColorAttachment *a = &pass->color[i];
        VkrTexture *t = vkr_GetTexture(a->texture);
        if (!t) {
            VKR_LOG("render pass: invalid colour target %u", i);
            return;
        }
        colorTex[i] = t;
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
            /* v0.4.2 (N2): DONT_CARE when asked (a tiler skips the write-back) */
            .storeOp = a->store == RHI_STORE_DONT_CARE ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                       : VK_ATTACHMENT_STORE_OP_STORE,
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
        depthTex = t;
        depth.imageView = t->view;
        depth.imageLayout = layout;
        depth.loadOp =
            vkr_loadOpMap[pass->depth.depthLoad < RHI_LOAD_COUNT ? pass->depth.depthLoad : 0].vk;
        depth.storeOp = pass->depth.readOnlyDepth ? VK_ATTACHMENT_STORE_OP_NONE
                        : pass->depth.store == RHI_STORE_DONT_CARE
                            ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                            : VK_ATTACHMENT_STORE_OP_STORE;
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
    if (c->globalOrder) {
        vkr_FlushBarriers(c);
        vkr_OrderWrites(c->cb, true);
    } else {
        /* barriers for the targets only ("Hazards"), in one
         * call with the deferred image barriers */
        VkrHz h;
        h.imgCount = h.bufCount = 0;
        h.src = h.dst = 0;
        vkr_HzTakePending(&h, c);
        for (uint32_t i = 0; i < pass->colorCount; i++) {
            vkr_HzImage(&h, c, colorTex[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        ~0u);
        }
        /* a read-only depth attachment is only read when it is loaded and
         * not stored; anything else writes it (a DONT_CARE store too: it is
         * a write access in Vulkan's synchronisation) */
        bool depthWrites = false;
        if (depthTex) {
            depthWrites = depth.storeOp != VK_ATTACHMENT_STORE_OP_NONE ||
                          depth.loadOp != VK_ATTACHMENT_LOAD_OP_LOAD ||
                          (hasStencil && (stencil.storeOp != VK_ATTACHMENT_STORE_OP_NONE ||
                                          stencil.loadOp != VK_ATTACHMENT_LOAD_OP_LOAD));
            vkr_HzImage(&h, c, depthTex, depth.imageLayout, VKR_DEPTH_STAGES,
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            (depthWrites ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0),
                        depthWrites ? ~0u : 0u);
        }
        vkr_HzFlush(c->cb, &h);
        for (uint32_t i = 0; i < pass->colorCount; i++) {
            vkr_HzTouch(c, colorTex[i]);
            colorTex[i]->hzAttach |= VKR_HZ_ATTACH_WRITE;
        }
        if (depthTex) {
            vkr_HzTouch(c, depthTex);
            depthTex->hzAttach |= depthWrites ? VKR_HZ_ATTACH_WRITE : VKR_HZ_ATTACH_READ;
        }
    }
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
    vkr_FlushBarriers(c);
    g_vkr.cmdEndRendering(c->cb);
    c->inPass = false;
}

void rhi_CmdSetViewport(RhiCommandList cl, const RhiViewport *v)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !v) {
        return;
    }
    vkr_FlushBarriers(c);
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
    vkr_FlushBarriers(c);
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
        vkr_FlushBarriers(c);
        vkCmdBindPipeline(c->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pp->pipeline);
        g_vkr.stats.pipelineBinds++;
    }
}

void rhi_CmdSetBindGroupOffsets(RhiCommandList cl, uint32_t group, RhiBindGroup bg,
                                const uint32_t *offsets, uint32_t count)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || group >= RHI_MAX_BIND_SLOTS) {
        return;
    }
    VkDescriptorSet s = vkr_GetBindGroup(bg);
    if (!s && bg.id) {
        VKR_LOG("rhi_CmdSetBindGroup: bind group %08x is not from this frame", bg.id);
    }
    /* package PA: the layout's dynamic slots take the given offsets, the
     * missing ones 0 (rhi_CmdSetBindGroup) */
    const uint32_t dyn = s ? vkr_BindGroupDynamicCount(bg) : 0;
    if (count > dyn) {
        VKR_LOG("rhi_CmdSetBindGroupOffsets: %u offsets for %u dynamic slots", count, dyn);
        count = dyn;
    }
    uint32_t off[RHI_MAX_DYNAMIC_OFFSETS] = {0};
    for (uint32_t i = 0; i < count && offsets; i++) {
        off[i] = offsets[i];
    }
    /* package P1: the set already bound (PA: with the same offsets) is not
     * bound again */
    if (c->groups[group] != s || c->dynCount[group] != dyn ||
        memcmp(c->offsets[group], off, dyn * sizeof(off[0])) != 0) {
        c->groups[group] = s;
        c->dynCount[group] = dyn;
        memcpy(c->offsets[group], off, sizeof(off));
        c->groupDirty |= 1u << group;
    }
}

void rhi_CmdSetBindGroup(RhiCommandList cl, uint32_t group, RhiBindGroup bg)
{
    rhi_CmdSetBindGroupOffsets(cl, group, bg, NULL, 0);
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
                                    1, &c->groups[g], c->dynCount[g], c->offsets[g]);
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
    vkr_FlushBarriers(c);
    vkCmdBindVertexBuffers(c->cb, binding, 1, &buf->buffer, &off);
}

void rhi_CmdSetIndexBuffer(RhiCommandList cl, RhiBuffer b, uint64_t offset, bool u32)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    VkrBuffer *buf = vkr_GetBuffer(b);
    if (!c || !buf) {
        return;
    }
    vkr_FlushBarriers(c);
    vkCmdBindIndexBuffer(c->cb, buf->buffer, offset,
                         u32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

void rhi_CmdSetStencilRef(RhiCommandList cl, uint8_t ref)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (c) {
        vkr_FlushBarriers(c);
        vkCmdSetStencilReference(c->cb, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
    }
}

void rhi_CmdSetBlendConstant(RhiCommandList cl, const float rgba[4])
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (c) {
        vkr_FlushBarriers(c);
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
    vkr_FlushBarriers(c);
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
    vkr_FlushBarriers(c);
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
    vkr_FlushBarriers(c);
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
    /* bufferRowLength is in texels; a BC row of blocks (rowPitch bytes)
     * covers 4 texel columns per block.  The extent stays in texels: a BC
     * level under 4 x 4 passes its real size (offset + extent may end at
     * the level's edge instead of a block multiple) */
    uint32_t texel = vkr_formatMap[t->rhiFormat].texelBytes;
    const uint32_t blockW = rhi_FormatIsBlock(t->rhiFormat) ? 4u : 1u;
    VkBufferImageCopy r = {
        .bufferOffset = srcOffset,
        .bufferRowLength = texel ? rowPitch / texel * blockW : 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vkr_CopyAspect(t, RHI_ASPECT_COLOR), mip, 0, 1},
        .imageOffset = {region.x, region.y, 0},
        .imageExtent = {region.w, region.h, 1},
    };
    vkr_HzCopy(c, NULL, s, t, mip, NULL);
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
    vkr_HzCopy(c, s, NULL, d, 0, NULL);
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
    vkr_HzCopy(c, t, NULL, NULL, 0, d);
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
    vkr_FlushBarriers(c);
    vkCmdBeginDebugUtilsLabelEXT(c->cb, &l);
}

void rhi_CmdEndLabel(RhiCommandList cl)
{
    VkrCmdList *c = vkr_GetCmd(cl);
    if (!c || !g_vkr.debugUtils || !vkCmdEndDebugUtilsLabelEXT) {
        return;
    }
    vkr_FlushBarriers(c);
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
    if (aspect == RHI_ASPECT_DEPTH && t->format == VK_FORMAT_D24_UNORM_S8_UINT) {
        return false; /* RhiLimits.depthReadback is false: no float depth to give */
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
            const void *mapped = rhi_MapBuffer(rb); /* NULL when vkMapMemory failed */
            ok = mapped != NULL;
            if (ok) {
                memcpy(dst, mapped, size);
            }
        } else {
            g_vkr.timelineValue--; /* not signalled (vkr_SubmitBatch) */
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
    vkr_FlushBarriers(c);
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
