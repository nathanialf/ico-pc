/* vk_cmd.c: frames, command lists, submission, barriers, render passes,
 * draws, copies and readback for the Vulkan backend.
 *
 * Synchronisation: RHI_FRAMES_IN_FLIGHT frame slots each own a command
 * pool, descriptor pools and the objects destroyed while they recorded.
 * Every submit signals the next value of one timeline semaphore, and
 * rhi_wait_frame waits for the value of a slot's last submit before it
 * reuses the slot.  The first submit after a swapchain acquire waits on
 * the acquire semaphore; the present waits on the image's renderDone
 * semaphore, which the presenting list's submit or an empty batch in
 * rhi_present signals (rhi_submit). */
#include "vk_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ frames */
bool vkr_frames_init(void)
{
    /* ICO_VK_GLOBAL_BARRIERS=1: a global barrier before every pass and copy
     * instead of hazard tracking, for A/B comparisons and bisecting
     * ("Hazards" below) */
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

void vkr_frames_shutdown(void)
{
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        VkrFrame *f = &g_vkr.frames[i];
        vkr_destroy_garbage(f);
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

static void vkr_wait_value(uint64_t value)
{
    if (value == 0) {
        return;
    }
    /* a wait that blocks is counted with its time */
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
    const uint64_t t0 = vkr_now_ns();
    VKR_CHECK(vkWaitSemaphores(g_vkr.device, &wi, UINT64_MAX));
    g_vkr.stats.fenceWaits++;
    g_vkr.stats.fenceWaitNs += vkr_now_ns() - t0;
}

/* the finished slot's timestamps into g_vkr.tsResult (the slot is complete:
 * nothing waits) */
static void vkr_collect_timestamps(VkrFrame *f)
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
static void vkr_recycle_frame(VkrFrame *f)
{
    vkr_wait_value(f->waitValue);
    vkr_collect_timestamps(f);
    f->tsReset = false;
    vkr_destroy_garbage(f);
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

void rhi_wait_frame(void)
{
    g_vkr.frameIndex++;
    vkr_recycle_frame(vkr_cur_frame());
}

uint32_t rhi_frame_slot(void)
{
    return (uint32_t)(g_vkr.frameIndex % RHI_FRAMES_IN_FLIGHT);
}

void rhi_wait_idle(void)
{
    const uint64_t t0 = vkr_now_ns();
    vkDeviceWaitIdle(g_vkr.device);
    g_vkr.stats.waitIdles++;
    g_vkr.stats.fenceWaitNs += vkr_now_ns() - t0;
    /* The other slots' garbage is now safe to destroy.  The current frame's
     * is not: a command list recorded but not yet submitted may still use
     * an object destroyed this frame. */
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (&g_vkr.frames[i] != vkr_cur_frame()) {
            vkr_destroy_garbage(&g_vkr.frames[i]);
        }
    }
}

void rhi_collect_garbage_now(void)
{
    rhi_wait_idle();
    vkr_destroy_garbage(vkr_cur_frame());
}

static void vkr_open_barrier(VkCommandBuffer cb);
static void vkr_flush_barriers(VkrCmdList *c);

/* ---------------------------------------------------------- command lists
 * Handle id: (frame tag << 20) | (list index + 1). */
VkrCmdList *vkr_get_cmd(RhiCommandList cl)
{
    VkrFrame *f = vkr_cur_frame();
    uint32_t idx = cl.id & VKR_INDEX_MASK;
    if (idx == 0 || idx > f->listCount ||
        (cl.id >> VKR_GEN_SHIFT) != (uint32_t)(g_vkr.frameIndex & VKR_GEN_MASK)) {
        return NULL;
    }
    return &f->lists[idx - 1];
}

RhiCommandList rhi_begin_commands(void)
{
    RhiCommandList out = {0};
    VkrFrame *f = vkr_cur_frame();
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
    /* a new hazard-tracking epoch ("Hazards") */
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
        vkr_open_barrier(c->cb);
    }
    f->listCount++;
    if (f->queryPool && !f->tsReset) {
        /* the slot's timestamps start unwritten (outside any render pass:
         * the frame's first list) */
        vkCmdResetQueryPool(c->cb, f->queryPool, 0, RHI_MAX_TIMESTAMPS);
        f->tsReset = true;
    }
    out.id = ((uint32_t)(g_vkr.frameIndex & VKR_GEN_MASK) << VKR_GEN_SHIFT) | f->listCount;
    return out;
}

void rhi_end_commands(RhiCommandList cl)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->recording) {
        return;
    }
    vkr_flush_barriers(c);
    if (c->inPass) {
        VKR_LOG("rhi_end_commands inside a render pass");
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
 * (vkr_submit_present_signal) or as the submit of the list that moved the
 * image to PRESENT (rhi_submit). */
static bool vkr_submit_batch(const VkCommandBuffer *cbs, uint32_t count, bool forPresent)
{
    VkSemaphore waitSems[2];
    uint64_t waitValues[2];
    VkPipelineStageFlags waitStages[2];
    uint32_t waitCount = 0;
    VkSemaphore sigSems[2];
    uint64_t sigValues[2];
    uint32_t sigCount = 0;
    bool tookAcquire = false;

    if (g_vkr.acquireWaitPending) {
        /* The first submit after an acquire waits for the image.  With
         * commands, at COLOR_ATTACHMENT_OUTPUT: the image's first use after
         * an acquire is a barrier out of UNDEFINED or PRESENT, and such a
         * barrier on a swapchain image has that stage in its first scope
         * (vkr_image_barrier), which chains it to this wait in this submit
         * or a later one; the work before it, on other images, need not
         * wait for the presentation engine.  An empty batch keeps
         * ALL_COMMANDS. */
        waitSems[waitCount] = g_vkr.acquireSem;
        waitValues[waitCount] = 0;
        waitStages[waitCount] = count ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                      : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        waitCount++;
        g_vkr.acquireWaitPending = false;
        tookAcquire = true;
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
         * batch's, vkr_wait_value) would never return */
        g_vkr.timelineValue--;
        /* a failed submit other than a device loss leaves its semaphores
         * as they were: the acquire semaphore is still signalled, and the
         * next submit must wait on it before the next acquire reuses it */
        if (tookAcquire && !g_vkr.deviceLost) {
            g_vkr.acquireWaitPending = true;
        }
        return false;
    }
    g_vkr.stats.submits++;
    vkr_cur_frame()->waitValue = value;
    return true;
}

/* Submits nothing but the wait on a pending acquire semaphore, then waits
 * for it: a swapchain recreate drops acquireWaitPending, and the semaphore
 * must not stay signalled for the frame's next acquire. */
bool vkr_submit_empty(void)
{
    if (!g_vkr.acquireWaitPending) {
        return true;
    }
    if (!vkr_submit_batch(NULL, 0, false)) {
        return false;
    }
    vkr_wait_value(g_vkr.timelineValue);
    return true;
}

void rhi_submit(RhiCommandList cl)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || c->submitted) {
        return;
    }
    if (c->recording) {
        rhi_end_commands(cl);
    }
    c->submitted = true;
    /* The first list of a frame that moves the acquired image to PRESENT
     * signals the present semaphore (renderDone) with its own submit, and
     * rhi_present then adds no empty batch.  When no list did, rhi_present
     * signals it with an empty batch after the frame's work.  Only the
     * first presenting list signals: a later list that writes the
     * backbuffer again and moves it back to PRESENT is not covered by the
     * present's wait.  Today rd_replay.c (rd__present_record) and
     * rd_video.c each present from one list. */
    bool signal = false;
    if (c->presentImage != VK_NULL_HANDLE && g_vkr.swapAcquired && !g_vkr.presentSignalled) {
        const VkrTexture *bb = vkr_get_texture((RhiTexture){g_vkr.swapTextures[g_vkr.swapImage]});
        signal = bb && bb->image == c->presentImage;
    }
    if (vkr_submit_batch(&c->cb, 1, signal) && signal) {
        g_vkr.presentSignalled = true;
    }
}

bool vkr_submit_present_signal(void)
{
    return vkr_submit_batch(NULL, 0, true);
}

/* ---------------------------------------------------------------- barriers
 * Image barriers are deferred.  vkr_image_barrier adds each one to the
 * list's pending set (VkrCmdList.pendImg) instead of recording it, and the
 * set goes out as one vkCmdPipelineBarrier, its stage masks OR-ed, before
 * the next command of any kind: every recording entry point below calls
 * vkr_flush_barriers first, and a render pass or a copy puts the set into
 * the barrier of its own hazards (vkr_hz_take_pending).  So every command
 * sees the same barriers it saw when each went out in a call of its own;
 * the wider stage masks only add waits.  A second barrier on an image
 * already in the set sends the set first (two layout transitions of one
 * image in one call are not ordered), and so does a full set.  A list on
 * the global barrier path (ICO_VK_GLOBAL_BARRIERS=1, or recorded
 * interleaved: vkr_order_writes) defers nothing and records each barrier
 * at once.  stats.barriers counts vkCmdPipelineBarrier calls. */
static void vkr_flush_barriers(VkrCmdList *c)
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

void vkr_image_barrier(VkrCmdList *c, VkrTexture *t, RhiState before, RhiState after)
{
    const VkrStateMap *b = &vkr_stateMap[before];
    const VkrStateMap *a = &vkr_stateMap[after];
    VkPipelineStageFlags src = b->stages;
    VkAccessFlags srcAccess = b->access;
    if (t->swapchain && (before == RHI_STATE_UNDEFINED || before == RHI_STATE_PRESENT)) {
        /* a swapchain image's first barrier after an acquire: the submit
         * waits on the acquire semaphore at COLOR_ATTACHMENT_OUTPUT
         * (vkr_submit_batch), and this stage in the first scope makes the
         * transition wait for it */
        src = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    if (t->hzEpoch == c->epoch) {
        /* the uses still pending on the image join the first scope (they
         * are the before state's own, unless the caller discards the
         * contents with before = UNDEFINED) */
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
        .oldLayout = vkr_state_layout(before, t->rhiFormat),
        .newLayout = vkr_state_layout(after, t->rhiFormat),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = t->image,
        .subresourceRange = {t->aspects, 0, t->mips, 0, 1},
    };
    VkPipelineStageFlags dst = a->stages;
    if (after == RHI_STATE_PRESENT) {
        dst = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        if (t->swapchain) {
            c->presentImage = t->image; /* rhi_submit */
        }
    }
    for (uint32_t i = 0; i < c->pendCount; i++) {
        if (c->pendImg[i].image == t->image) {
            vkr_flush_barriers(c);
            break;
        }
    }
    if (c->pendCount == VKR_PENDING_BARRIERS) {
        vkr_flush_barriers(c);
    }
    c->pendImg[c->pendCount++] = ib;
    c->pendSrc |= src;
    c->pendDst |= dst;
    if (c->globalOrder) {
        vkr_flush_barriers(c);
    }
    /* the barrier covers whatever was pending on the image */
    t->hzEpoch = 0;
    t->hzXferMips = 0;
    t->hzAttach = 0;
}

void rhi_cmd_barrier(RhiCommandList cl, const RhiTextureBarrier *barriers, uint32_t count)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c) {
        return;
    }
    if (!c->recording) {
        /* after rhi_end_commands nothing more is recorded: the barrier
           would sit in the pending set unrecorded while the texture's
           state and hazards said it happened */
        static bool s_logged;
        if (!s_logged) {
            s_logged = true;
            VKR_LOG("rhi_cmd_barrier on a list already ended: ignored (logged once)");
        }
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        VkrTexture *t = vkr_get_texture(barriers[i].texture);
        if (!t || barriers[i].before >= RHI_STATE_COUNT || barriers[i].after >= RHI_STATE_COUNT) {
            VKR_LOG("rhi_cmd_barrier: invalid barrier %u", i);
            continue;
        }
        vkr_image_barrier(c, t, barriers[i].before, barriers[i].after);
    }
}

/* Same-state write ordering.  rhi_cmd_barrier is only needed when a texture
 * changes state; two writes in the same state (a copy after a copy into one
 * texture, a render pass after a render pass on one target) are ordered by
 * the backend.  Vulkan gives no implicit ordering across copies or across
 * render passes.
 *
 * vkr_order_writes orders them for ICO_VK_GLOBAL_BARRIERS=1 and for lists
 * recorded interleaved: a global memory barrier before every copy and
 * every render pass.  Other lists use hazard tracking ("Hazards" below). */
static void vkr_order_writes(VkCommandBuffer cb, bool attachments)
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

/* Hazards.  Each texture and buffer records what touched it since its last
 * barrier: the mips a copy wrote, an attachment write or a read-only depth
 * attachment read (a buffer: a copy's write or read).  A render pass or
 * copy emits one pipeline barrier holding an image (buffer) barrier for
 * each resource it uses that has a pending hazard with that use (write
 * after write, read after write, write after read), in the layout the use
 * needs (old = new: layouts change only at the caller's rhi_cmd_barrier,
 * which clears the record).  Anything else needs no barrier: sampling,
 * copying from and presenting all need a state change, and that
 * transition is the caller's.
 *
 * The record belongs to one command list (its epoch).  Each list begins
 * with one global barrier over attachment and transfer writes
 * (vkr_open_barrier), which orders it after every list submitted before it,
 * this frame's and the frames' still in flight, so a record from an older
 * list counts as clean and nothing is carried across submissions or frame
 * slots.  Lists recorded interleaved fall back to vkr_order_writes. */
static void vkr_open_barrier(VkCommandBuffer cb)
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
 * makes (vkr_hz_flush) with the hazards' own barriers.  None of their
 * images is among those hazards: a deferred barrier cleared its image's
 * record (vkr_image_barrier), and vkr_hz_image adds nothing for a cleared
 * record. */
static void vkr_hz_take_pending(VkrHz *h, VkrCmdList *c)
{
    for (uint32_t i = 0; i < c->pendCount; i++) {
        h->img[h->imgCount++] = c->pendImg[i];
    }
    h->src |= c->pendSrc;
    h->dst |= c->pendDst;
    c->pendCount = 0;
    c->pendSrc = c->pendDst = 0;
}

static void vkr_hz_touch(const VkrCmdList *c, VkrTexture *t)
{
    if (t->hzEpoch != c->epoch) {
        t->hzEpoch = c->epoch;
        t->hzXferMips = 0;
        t->hzAttach = 0;
    }
}

/* A use of t in `layout` by dstStages/dstAccess; writeMips: the mips the
 * use writes (0: a read). */
static void vkr_hz_image(VkrHz *h, const VkrCmdList *c, VkrTexture *t, VkImageLayout layout,
                         VkPipelineStageFlags dstStages, VkAccessFlags dstAccess,
                         uint32_t writeMips)
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
static void vkr_hz_buffer(VkrHz *h, const VkrCmdList *c, VkrBuffer *b, bool write)
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

static void vkr_hz_flush(VkCommandBuffer cb, const VkrHz *h)
{
    if (h->imgCount + h->bufCount == 0) {
        return;
    }
    vkCmdPipelineBarrier(cb, h->src, h->dst, 0, 0, NULL, h->bufCount, h->buf, h->imgCount, h->img);
    g_vkr.stats.barriers++;
}

static uint32_t vkr_mip_bit(uint32_t mip)
{
    return mip < 32 ? 1u << mip : 0x80000000u;
}

/* A copy from srcTex or srcBuf into dstTex (mip dstMip) or dstBuf. */
static void vkr_hz_copy(VkrCmdList *c, VkrTexture *srcTex, VkrBuffer *srcBuf, VkrTexture *dstTex,
                        uint32_t dstMip, VkrBuffer *dstBuf)
{
    if (c->globalOrder) {
        vkr_flush_barriers(c);
        vkr_order_writes(c->cb, false);
        return;
    }
    VkrHz h;
    h.imgCount = h.bufCount = 0;
    h.src = h.dst = 0;
    vkr_hz_take_pending(&h, c);
    if (srcTex) {
        vkr_hz_image(&h, c, srcTex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0);
    }
    if (dstTex) {
        vkr_hz_image(&h, c, dstTex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                     vkr_mip_bit(dstMip));
    }
    if (srcBuf) {
        vkr_hz_buffer(&h, c, srcBuf, false);
    }
    if (dstBuf) {
        vkr_hz_buffer(&h, c, dstBuf, true);
    }
    vkr_hz_flush(c->cb, &h);
    if (dstTex) {
        vkr_hz_touch(c, dstTex);
        dstTex->hzXferMips |= vkr_mip_bit(dstMip);
    }
}

/* ------------------------------------------------------------ render passes */
void rhi_cmd_begin_render_pass(RhiCommandList cl, const RhiRenderPassDesc *pass)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !pass || pass->colorCount > RHI_MAX_COLOR_TARGETS) {
        return;
    }
    VkRenderingAttachmentInfo color[RHI_MAX_COLOR_TARGETS];
    VkrTexture *colorTex[RHI_MAX_COLOR_TARGETS];
    VkrTexture *depthTex = NULL;
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        const RhiColorAttachment *a = &pass->color[i];
        VkrTexture *t = vkr_get_texture(a->texture);
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
            /* DONT_CARE when asked (a tiler skips the write-back) */
            .storeOp = a->store == RHI_STORE_DONT_CARE ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                       : VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = cv,
        };
    }
    VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingAttachmentInfo stencil = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    bool hasDepth = false, hasStencil = false;
    if (pass->depth.texture.id) {
        VkrTexture *t = vkr_get_texture(pass->depth.texture);
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
            /* the stencil's own store op; zero (RHI_STORE_STORE) keeps the
             * stencil */
            stencil.storeOp = pass->depth.readOnlyDepth ? VK_ATTACHMENT_STORE_OP_NONE
                              : pass->depth.stencilStore == RHI_STORE_DONT_CARE
                                  ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                  : VK_ATTACHMENT_STORE_OP_STORE;
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
        vkr_flush_barriers(c);
        vkr_order_writes(c->cb, true);
    } else {
        /* barriers for the targets only ("Hazards"), in one
         * call with the deferred image barriers */
        VkrHz h;
        h.imgCount = h.bufCount = 0;
        h.src = h.dst = 0;
        vkr_hz_take_pending(&h, c);
        for (uint32_t i = 0; i < pass->colorCount; i++) {
            vkr_hz_image(&h, c, colorTex[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
            vkr_hz_image(&h, c, depthTex, depth.imageLayout, VKR_DEPTH_STAGES,
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             (depthWrites ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0),
                         depthWrites ? ~0u : 0u);
        }
        vkr_hz_flush(c->cb, &h);
        for (uint32_t i = 0; i < pass->colorCount; i++) {
            vkr_hz_touch(c, colorTex[i]);
            colorTex[i]->hzAttach |= VKR_HZ_ATTACH_WRITE;
        }
        if (depthTex) {
            vkr_hz_touch(c, depthTex);
            depthTex->hzAttach |= depthWrites ? VKR_HZ_ATTACH_WRITE : VKR_HZ_ATTACH_READ;
        }
    }
    g_vkr.cmdBeginRendering(c->cb, &ri);
    c->inPass = true;
    g_vkr.stats.renderPasses++;

    /* defaults: full-target viewport and scissor */
    RhiViewport vp = {0.0f, 0.0f, (float)pass->width, (float)pass->height, 0.0f, 1.0f};
    RhiRect sc = {0, 0, pass->width, pass->height};
    rhi_cmd_set_viewport(cl, &vp);
    rhi_cmd_set_scissor(cl, &sc);
}

void rhi_cmd_end_render_pass(RhiCommandList cl)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->inPass) {
        return;
    }
    vkr_flush_barriers(c);
    g_vkr.cmdEndRendering(c->cb);
    c->inPass = false;
}

void rhi_cmd_set_viewport(RhiCommandList cl, const RhiViewport *v)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !v) {
        return;
    }
    vkr_flush_barriers(c);
    /* Negative height (core since Vulkan 1.1) gives D3D's y-up NDC, so the
     * same HLSL runs on both backends without -fvk-invert-y. */
    VkViewport vp = {v->x, v->y + v->h, v->w, -v->h, v->minDepth, v->maxDepth};
    vkCmdSetViewport(c->cb, 0, 1, &vp);
}

void rhi_cmd_set_scissor(RhiCommandList cl, const RhiRect *r)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !r) {
        return;
    }
    vkr_flush_barriers(c);
    VkRect2D sc = {{r->x, r->y}, {r->w, r->h}};
    vkCmdSetScissor(c->cb, 0, 1, &sc);
}

void rhi_cmd_set_pipeline(RhiCommandList cl, RhiPipeline p)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrPipeline *pp = vkr_pool_get(&g_vkr.pipelines, p.id);
    if (!c || !pp) {
        return;
    }
    if (c->pipeline != pp) {
        if (!c->pipeline || c->pipeline->layout != pp->layout) {
            c->groupDirty = 0xFFu; /* rebind against the new layout */
        }
        c->pipeline = pp;
        vkr_flush_barriers(c);
        vkCmdBindPipeline(c->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pp->pipeline);
        g_vkr.stats.pipelineBinds++;
    }
}

void rhi_cmd_set_bind_group_offsets(RhiCommandList cl, uint32_t group, RhiBindGroup bg,
                                    const uint32_t *offsets, uint32_t count)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || group >= RHI_MAX_BIND_SLOTS) {
        return;
    }
    VkDescriptorSet s = vkr_get_bind_group(bg);
    if (!s && bg.id) {
        VKR_LOG("rhi_cmd_set_bind_group: bind group %08x is not from this frame", bg.id);
    }
    /* the layout's dynamic slots take the given offsets, the missing ones 0
     * (rhi_cmd_set_bind_group) */
    const uint32_t dyn = s ? vkr_bind_group_dynamic_count(bg) : 0;
    if (count > dyn) {
        VKR_LOG("rhi_cmd_set_bind_group_offsets: %u offsets for %u dynamic slots", count, dyn);
        count = dyn;
    }
    uint32_t off[RHI_MAX_DYNAMIC_OFFSETS] = {0};
    for (uint32_t i = 0; i < count && offsets; i++) {
        off[i] = offsets[i];
    }
    /* the set already bound with the same dynamic offsets is not bound
     * again */
    if (c->groups[group] != s || c->dynCount[group] != dyn ||
        memcmp(c->offsets[group], off, dyn * sizeof(off[0])) != 0) {
        c->groups[group] = s;
        c->dynCount[group] = dyn;
        memcpy(c->offsets[group], off, sizeof(off));
        c->groupDirty |= 1u << group;
    }
}

void rhi_cmd_set_bind_group(RhiCommandList cl, uint32_t group, RhiBindGroup bg)
{
    rhi_cmd_set_bind_group_offsets(cl, group, bg, NULL, 0);
}

/* Binds the dirty groups against the current pipeline's layout. */
static void vkr_flush_bind_groups(VkrCmdList *c)
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

void rhi_cmd_set_vertex_buffer(RhiCommandList cl, uint32_t binding, RhiBuffer b, uint64_t offset)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrBuffer *buf = vkr_get_buffer(b);
    if (!c || !buf) {
        return;
    }
    VkDeviceSize off = offset;
    vkr_flush_barriers(c);
    vkCmdBindVertexBuffers(c->cb, binding, 1, &buf->buffer, &off);
}

void rhi_cmd_set_index_buffer(RhiCommandList cl, RhiBuffer b, uint64_t offset, bool u32)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrBuffer *buf = vkr_get_buffer(b);
    if (!c || !buf) {
        return;
    }
    vkr_flush_barriers(c);
    vkCmdBindIndexBuffer(c->cb, buf->buffer, offset,
                         u32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

void rhi_cmd_set_stencil_ref(RhiCommandList cl, uint8_t ref)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (c) {
        vkr_flush_barriers(c);
        vkCmdSetStencilReference(c->cb, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
    }
}

void rhi_cmd_set_blend_constant(RhiCommandList cl, const float rgba[4])
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (c) {
        vkr_flush_barriers(c);
        vkCmdSetBlendConstants(c->cb, rgba);
    }
}

void rhi_cmd_draw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                  uint32_t instanceCount)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->pipeline) {
        return;
    }
    vkr_flush_barriers(c);
    vkr_flush_bind_groups(c);
    vkCmdDraw(c->cb, vertexCount, instanceCount ? instanceCount : 1u, firstVertex, 0);
    g_vkr.stats.draws++;
}

void rhi_cmd_draw_indexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                          int32_t vertexOffset, uint32_t instanceCount)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->pipeline) {
        return;
    }
    vkr_flush_barriers(c);
    vkr_flush_bind_groups(c);
    vkCmdDrawIndexed(c->cb, indexCount, instanceCount ? instanceCount : 1u, firstIndex,
                     vertexOffset, 0);
    g_vkr.stats.draws++;
}

/* ------------------------------------------------------------------ copies */
void rhi_cmd_copy_buffer(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset, RhiBuffer dst,
                         uint64_t dstOffset, uint64_t size)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrBuffer *s = vkr_get_buffer(src);
    VkrBuffer *d = vkr_get_buffer(dst);
    if (!c || !s || !d || size == 0) {
        return;
    }
    VkBufferCopy r = {srcOffset, dstOffset, size};
    vkr_flush_barriers(c);
    /* rhi.h: the copy waits for every earlier read of the destination
     * (draws of earlier frames reading a range rewritten now) and write (an
     * earlier copy) */
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

static VkImageAspectFlags vkr_copy_aspect(const VkrTexture *t, RhiViewAspect aspect)
{
    if (t->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
        (void)aspect; /* depth formats copy their depth aspect */
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

void rhi_cmd_copy_buffer_to_texture(RhiCommandList cl, RhiBuffer src, uint64_t srcOffset,
                                    uint32_t rowPitch, RhiTexture dst, uint32_t mip, RhiRect region)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrBuffer *s = vkr_get_buffer(src);
    VkrTexture *t = vkr_get_texture(dst);
    if (!c || !s || !t) {
        return;
    }
    /* bufferRowLength is in texels; a BC row of blocks (rowPitch bytes)
     * covers 4 texel columns per block.  The extent stays in texels: a BC
     * level under 4 x 4 passes its real size (offset + extent may end at
     * the level's edge instead of a block multiple) */
    uint32_t texel = vkr_formatMap[t->rhiFormat].texelBytes;
    const uint32_t blockW = rhi_format_is_block(t->rhiFormat) ? 4u : 1u;
    VkBufferImageCopy r = {
        .bufferOffset = srcOffset,
        .bufferRowLength = texel ? rowPitch / texel * blockW : 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vkr_copy_aspect(t, RHI_ASPECT_COLOR), mip, 0, 1},
        .imageOffset = {region.x, region.y, 0},
        .imageExtent = {region.w, region.h, 1},
    };
    vkr_hz_copy(c, NULL, s, t, mip, NULL);
    vkCmdCopyBufferToImage(c->cb, s->buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    g_vkr.stats.copies++;
}

void rhi_cmd_copy_texture(RhiCommandList cl, RhiTexture src, RhiRect srcRegion, RhiTexture dst,
                          int32_t dstX, int32_t dstY)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrTexture *s = vkr_get_texture(src);
    VkrTexture *d = vkr_get_texture(dst);
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
    vkr_hz_copy(c, s, NULL, d, 0, NULL);
    vkCmdCopyImage(c->cb, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    g_vkr.stats.copies++;
}

void rhi_cmd_copy_texture_to_buffer(RhiCommandList cl, RhiTexture src, RhiViewAspect aspect,
                                    RhiRect region, RhiBuffer dst, uint64_t dstOffset,
                                    uint32_t rowPitch)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrTexture *t = vkr_get_texture(src);
    VkrBuffer *d = vkr_get_buffer(dst);
    if (!c || !t || !d) {
        return;
    }
    uint32_t texel = vkr_formatMap[t->rhiFormat].texelBytes;
    VkBufferImageCopy r = {
        .bufferOffset = dstOffset,
        .bufferRowLength = texel ? rowPitch / texel : 0,
        .imageSubresource = {vkr_copy_aspect(t, aspect), 0, 0, 1},
        .imageOffset = {region.x, region.y, 0},
        .imageExtent = {region.w, region.h, 1},
    };
    vkr_hz_copy(c, t, NULL, NULL, 0, d);
    vkCmdCopyImageToBuffer(c->cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->buffer, 1, &r);
    g_vkr.stats.copies++;
}

/* ------------------------------------------------------------ debug labels */
void rhi_cmd_begin_label(RhiCommandList cl, const char *name)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !g_vkr.debugUtils || !VKR_FUNCTION_AVAILABLE(vkCmdBeginDebugUtilsLabelEXT)) {
        return;
    }
    VkDebugUtilsLabelEXT l = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = name};
    vkr_flush_barriers(c);
    vkCmdBeginDebugUtilsLabelEXT(c->cb, &l);
}

void rhi_cmd_end_label(RhiCommandList cl)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !g_vkr.debugUtils || !VKR_FUNCTION_AVAILABLE(vkCmdEndDebugUtilsLabelEXT)) {
        return;
    }
    vkr_flush_barriers(c);
    vkCmdEndDebugUtilsLabelEXT(c->cb);
}

/* ---------------------------------------------------------------- readback */
bool rhi_readback_texture(RhiTexture h, RhiViewAspect aspect, void *dst, size_t dstSize,
                          uint32_t *outRowPitch)
{
    VkrTexture *t = vkr_get_texture(h);
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
    RhiBuffer rb = rhi_create_buffer(&bd);
    VkrBuffer *b = vkr_get_buffer(rb);
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
            .imageSubresource = {vkr_copy_aspect(t, aspect), 0, 0, 1},
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
            vkr_cur_frame()->waitValue = value;
            vkr_wait_value(value);
            const void *mapped = rhi_map_buffer(rb); /* NULL when vkMapMemory failed */
            ok = mapped != NULL;
            if (ok) {
                memcpy(dst, mapped, size);
            }
        } else {
            g_vkr.timelineValue--; /* not signalled (vkr_submit_batch) */
        }
        vkFreeCommandBuffers(g_vkr.device, g_vkr.oneShotPool, 1, &cb);
    }
    rhi_destroy_buffer(rb);
    return ok;
}

/* -------------------------------------------------------------- timestamps */
void rhi_cmd_write_timestamp(RhiCommandList cl, uint32_t index)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrFrame *f = vkr_cur_frame();
    const bool keep = (index & RHI_TIMESTAMP_KEEP_BARRIERS) != 0;
    index &= ~RHI_TIMESTAMP_KEEP_BARRIERS;
    if (!c || !f->queryPool || !f->tsReset || index >= RHI_MAX_TIMESTAMPS ||
        (f->tsWritten & (1u << index))) {
        return;
    }
    if (!keep) {
        vkr_flush_barriers(c);
    }
    vkCmdWriteTimestamp(c->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f->queryPool, index);
    f->tsWritten |= 1u << index;
}

uint32_t rhi_read_timestamps(uint64_t *ns, uint32_t max)
{
    if (!ns || !g_vkr.timestamps) {
        return 0;
    }
    const uint32_t n = g_vkr.tsCount < max ? g_vkr.tsCount : max;
    memcpy(ns, g_vkr.tsResult, (size_t)n * sizeof(uint64_t));
    return n;
}
