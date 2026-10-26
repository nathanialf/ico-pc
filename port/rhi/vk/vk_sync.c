/* vk_sync.c: the submission sync of the Vulkan backend (vk_internal.h,
 * VkrSyncKind).
 *
 * Every submission takes the next number of the port's counter
 * (VkrState.timelineValue).  What tells the CPU that number has finished:
 *
 *   VKR_SYNC_TIMELINE  one timeline semaphore, the number its value (Vulkan
 *                      1.2's core entry points, VK_KHR_timeline_semaphore's
 *                      below 1.2): the submit chains
 *                      VkTimelineSemaphoreSubmitInfo, a wait is
 *                      vkWaitSemaphores, the newest finished
 *                      vkGetSemaphoreCounterValue.
 *   VKR_SYNC_FENCES    a device without timeline semaphores (a Vulkan 1.1
 *                      driver without the extension; ICO_VK_FAKE_API=1.1):
 *                      a ring of VKR_SYNC_FENCE_RING fences, number n
 *                      signalling the fence at slot n % VKR_SYNC_FENCE_RING.
 *                      A submit whose slot still holds an unfinished number
 *                      waits for it first (logged the first time: the
 *                      renderer keeps fewer submissions than that in
 *                      flight, RHI_FRAMES_IN_FLIGHT frames of a few lists).
 *                      One queue signals in submission order, so a
 *                      signalled fence means every earlier number finished
 *                      too, and the newest finished is exact: the scan from
 *                      the newest known finished up to the first fence not
 *                      yet signalled.  A number whose slot holds a later
 *                      number finished before that slot was taken again.
 *
 * The callers (vk_cmd.c vkr_submit_batch, vkr_wait_value,
 * rhi_readback_texture; vk_device.c vkr_report_submits) see the counter
 * only, so the watchdog's "submission N has not finished"
 * (rhi_gpu_progress) and the loss report read the same on both paths.  The
 * timeline path's submits are the ones made before the fence path existed,
 * field for field. */
#include "vk_internal.h"
#include "rhi_vk.h"
#include <string.h>

bool vkr_sync_init(void)
{
    g_vkr.timelineValue = 0;
    g_vkr.fenceFinished = 0;
    g_vkr.fenceRecycleWaits = 0;
    memset(g_vkr.fenceValue, 0, sizeof(g_vkr.fenceValue));
    if (g_vkr.syncKind == VKR_SYNC_TIMELINE) {
        if (g_vkr.apiVersion >= VK_API_VERSION_1_2) {
            g_vkr.waitSemaphores = vkWaitSemaphores;
            g_vkr.getSemaphoreCounter = vkGetSemaphoreCounterValue;
        } else {
            /* VK_KHR_timeline_semaphore on a Vulkan 1.1 device */
            g_vkr.waitSemaphores =
                (PFN_vkWaitSemaphores)vkGetDeviceProcAddr(g_vkr.device, "vkWaitSemaphoresKHR");
            g_vkr.getSemaphoreCounter = (PFN_vkGetSemaphoreCounterValue)vkGetDeviceProcAddr(
                g_vkr.device, "vkGetSemaphoreCounterValueKHR");
            if (!g_vkr.waitSemaphores || !g_vkr.getSemaphoreCounter) {
                VKR_LOG("timeline semaphores: vkWaitSemaphoresKHR or "
                        "vkGetSemaphoreCounterValueKHR not found");
                return false;
            }
        }
        VkSemaphoreTypeCreateInfo tci = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        };
        VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                                     .pNext = &tci};
        return VKR_CHECK(vkCreateSemaphore(g_vkr.device, &sci, NULL, &g_vkr.timeline));
    }
    const VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    for (uint32_t i = 0; i < VKR_SYNC_FENCE_RING; i++) {
        if (!VKR_CHECK(vkCreateFence(g_vkr.device, &fci, NULL, &g_vkr.fences[i]))) {
            return false;
        }
    }
    VKR_LOG("submission sync: a ring of %u fences (no timeline semaphores%s)", VKR_SYNC_FENCE_RING,
            g_vkr.fakeNoTimeline ? ": ICO_VK_FAKE_API=1.1" : "");
    return true;
}

void vkr_sync_destroy(void)
{
    if (g_vkr.timeline) {
        vkDestroySemaphore(g_vkr.device, g_vkr.timeline, NULL);
        g_vkr.timeline = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < VKR_SYNC_FENCE_RING; i++) {
        if (g_vkr.fences[i]) {
            vkDestroyFence(g_vkr.device, g_vkr.fences[i], NULL);
            g_vkr.fences[i] = VK_NULL_HANDLE;
        }
    }
}

const char *vkr_sync_name(void)
{
    return g_vkr.syncKind == VKR_SYNC_TIMELINE ? "timeline" : "fences";
}

/* the fence path's newest number finished: the scan up to the first fence
 * not signalled (VK_NOT_READY), or the error a status read gave */
static VkResult vkr_fences_scan(void)
{
    while (g_vkr.fenceFinished < g_vkr.timelineValue) {
        const uint64_t v = g_vkr.fenceFinished + 1u;
        const uint32_t s = (uint32_t)(v % VKR_SYNC_FENCE_RING);
        if (g_vkr.fenceValue[s] == v) {
            const VkResult r = vkGetFenceStatus(g_vkr.device, g_vkr.fences[s]);
            if (r != VK_SUCCESS) {
                return r;
            }
        }
        /* signalled, or the slot was taken again by a later number, which
         * waited for this one first */
        g_vkr.fenceFinished = v;
    }
    return VK_SUCCESS;
}

bool vkr_sync_reached(uint64_t *out)
{
    if (g_vkr.syncKind == VKR_SYNC_TIMELINE) {
        *out = 0;
        return g_vkr.getSemaphoreCounter(g_vkr.device, g_vkr.timeline, out) == VK_SUCCESS;
    }
    const VkResult r = vkr_fences_scan();
    *out = g_vkr.fenceFinished;
    return r == VK_SUCCESS || r == VK_NOT_READY;
}

VkResult vkr_sync_wait(uint64_t value)
{
    if (g_vkr.syncKind == VKR_SYNC_TIMELINE) {
        VkSemaphoreWaitInfo wi = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1,
            .pSemaphores = &g_vkr.timeline,
            .pValues = &value,
        };
        return g_vkr.waitSemaphores(g_vkr.device, &wi, UINT64_MAX);
    }
    if (value <= g_vkr.fenceFinished) {
        return VK_SUCCESS;
    }
    const uint32_t s = (uint32_t)(value % VKR_SYNC_FENCE_RING);
    if (g_vkr.fenceValue[s] > value) {
        /* the slot was taken again, after a wait for this number */
        g_vkr.fenceFinished = value;
        return VK_SUCCESS;
    }
    if (g_vkr.fenceValue[s] < value) {
        /* never submitted (the counter only moves on a submit that
         * succeeded): nothing to wait for */
        return VK_SUCCESS;
    }
    const VkResult r = vkWaitForFences(g_vkr.device, 1, &g_vkr.fences[s], VK_TRUE, UINT64_MAX);
    if (r == VK_SUCCESS && value > g_vkr.fenceFinished) {
        /* in submission order: every earlier number finished too */
        g_vkr.fenceFinished = value;
    }
    return r;
}

/* the fence path: the slot of number `value` free (its earlier number
 * finished) and its fence unsignalled; false when the earlier number did
 * not finish (a lost device) or the reset failed */
static bool vkr_fence_slot_take(uint64_t value)
{
    const uint32_t s = (uint32_t)(value % VKR_SYNC_FENCE_RING);
    const uint64_t old = g_vkr.fenceValue[s];
    if (old == 0) {
        return true; /* never used, or reset after a failed submit */
    }
    if (old > g_vkr.fenceFinished) {
        (void)vkr_fences_scan();
    }
    if (old > g_vkr.fenceFinished) {
        if (g_vkr.fenceRecycleWaits++ == 0) {
            VKR_LOG("submission sync: submission %llu waits for submission %llu, the fence "
                    "ring's %u slots all in flight (logged once)",
                    (unsigned long long)value, (unsigned long long)old, VKR_SYNC_FENCE_RING);
        }
        vkr_wait_value(old);
        if (old > g_vkr.fenceFinished) {
            return false;
        }
    }
    if (!VKR_CHECK(vkResetFences(g_vkr.device, 1, &g_vkr.fences[s]))) {
        return false;
    }
    g_vkr.fenceValue[s] = 0;
    return true;
}

bool vkr_sync_submit(const VkrSubmitDesc *d, uint64_t *outValue)
{
    VkSemaphore waitSems[2];
    uint64_t waitValues[2];
    VkPipelineStageFlags waitStages[2];
    uint32_t waitCount = 0;
    VkSemaphore sigSems[2];
    uint64_t sigValues[2];
    uint32_t sigCount = 0;
    const bool timeline = g_vkr.syncKind == VKR_SYNC_TIMELINE;
    const uint64_t value = g_vkr.timelineValue + 1u;

    if (d->wait != VK_NULL_HANDLE) {
        waitSems[waitCount] = d->wait;
        waitValues[waitCount] = 0;
        waitStages[waitCount] = d->waitStage;
        waitCount++;
    }
    if (d->afterAll && timeline) {
        waitSems[waitCount] = g_vkr.timeline;
        waitValues[waitCount] = g_vkr.timelineValue;
        waitStages[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        waitCount++;
    }
    if (d->signal != VK_NULL_HANDLE) {
        sigSems[sigCount] = d->signal;
        sigValues[sigCount] = 0;
        sigCount++;
    }
    if (timeline) {
        sigSems[sigCount] = g_vkr.timeline;
        sigValues[sigCount] = value;
        sigCount++;
    }
    VkTimelineSemaphoreSubmitInfo ti = {
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .waitSemaphoreValueCount = waitCount,
        .pWaitSemaphoreValues = waitValues,
        .signalSemaphoreValueCount = sigCount,
        .pSignalSemaphoreValues = sigValues,
    };
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = timeline ? &ti : NULL,
        .waitSemaphoreCount = waitCount,
        .pWaitSemaphores = waitSems,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = d->cbCount,
        .pCommandBuffers = d->cbs,
        .signalSemaphoreCount = sigCount,
        .pSignalSemaphores = sigSems,
    };
    VkFence fence = VK_NULL_HANDLE;
    if (!timeline) {
        if (!vkr_fence_slot_take(value)) {
            return false;
        }
        fence = g_vkr.fences[value % VKR_SYNC_FENCE_RING];
    }
    if (!VKR_CHECK(vkQueueSubmit(g_vkr.queue, 1, &si, fence))) {
        /* nothing will signal the number: the counter stays, and a later
         * wait on it never blocks (the fence slot stays free: a failed
         * submit leaves its fence unsignalled) */
        return false;
    }
    if (!timeline) {
        g_vkr.fenceValue[value % VKR_SYNC_FENCE_RING] = value;
    }
    g_vkr.timelineValue = value;
    *outValue = value;
    return true;
}

/* rhi_vk.h, for rhi_vk_test's sync cell */
const char *vkr_test_sync_kind(void)
{
    return vkr_sync_name();
}

uint32_t vkr_test_sync_recycle_waits(void)
{
    return g_vkr.fenceRecycleWaits;
}
