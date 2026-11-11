/* vk_cmd.c: frames, command lists, submission, barriers, render passes,
 * draws, copies and readback for the Vulkan backend.
 *
 * Synchronisation: RHI_FRAMES_IN_FLIGHT frame slots each own a command
 * pool, descriptor pools and the objects destroyed while they recorded.
 * Every submit takes the next number of the port's counter, which the
 * submission sync signals (vk_sync.c: a timeline semaphore, or a ring of
 * fences on a device without one), and rhi_wait_frame waits for the number
 * of a slot's last submit before it reuses the slot.  After a swapchain
 * acquire, the submit of the first list that puts a barrier on the
 * acquired image waits on the acquire semaphore (a list on other images
 * does not: vkr_submit_batch); the present waits on the image's
 * renderDone semaphore, which the presenting list's submit or an empty
 * batch in rhi_present signals (rhi_submit).
 *
 * Frame slots (docs/port/RHI.md "Frame slots and the submit thread"): a
 * command list belongs to the slot whose pool it came from
 * (VkrCmdList.slot, and its handle's tag names the slot), and its end and
 * submit take that slot's fence value, acquire and query pools, never the
 * current frame index.  So with RhiLimits.slotOverlap the submit thread
 * (port/render/rd_submit_thread.h) ends and submits picture N's list of
 * slot A and presents its image while the render thread recycles slot B
 * (rhi_wait_frame: B's own fence), acquires B's image and records B's
 * list.  What each side writes then:
 *   recording thread  frameIndex, slot B (its pool, lists, descriptor
 *                     pools, sets, garbage, queries, tag, acquire),
 *                     hzEpoch, stats, tsResult and occResult (the recycled
 *                     slot's results), the texture and buffer tracking
 *                     state of what its list touches;
 *   submit thread     slot A's lists (their end, unless the recording
 *                     thread ended them: rhi_end_commands is a slot call,
 *                     port/render/rd_submit.c ends a picture's scene list
 *                     before it begins the present's), A's waitValue and
 *                     acquire record, the submission counter
 *                     (timelineValue, stored with __atomic), the submit
 *                     history, late (atomic adds), and under the
 *                     swapchain's lock the present's state;
 *   both, read only   the object pools' live entries, the swapchain's
 *                     images and semaphores (made again only with the
 *                     submit thread idle: vk_swapchain.c defers a rebuild
 *                     its present asks for to the next acquire).
 * The queue (vkQueueSubmit, vkQueuePresentKHR, vkDeviceWaitIdle) is the
 * submit thread's while it works: every call of the recording thread that
 * submits or waits idle is one the call guard holds until the submit
 * thread is idle (rhi_backend.c), and rhi_cmd_begin_occlusion's first use,
 * which submits the pools' reset, asks the guard first. */
#include "vk_internal.h"
#include "rhi_vk.h"
#include "../rhi_bind_cache.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------- the barrier trace
 * rhi_vk.h vkr_test_barrier_trace: every vkCmdPipelineBarrier of this file
 * noted while the tests ask (the recording thread's; one branch a barrier
 * otherwise).  The macro below wraps volk's function pointer of the same
 * name (not expanded again inside itself); its arguments have no side
 * effects at any call site. */
#define VKR_TEST_BARRIERS 64u
static bool s_barTrace;
static VkrTestBarrier s_bar[VKR_TEST_BARRIERS];
static uint32_t s_barN;

void vkr_test_barrier_trace(bool on)
{
    s_barTrace = on;
    s_barN = 0;
}

uint32_t vkr_test_barrier_get(VkrTestBarrier *out, uint32_t cap)
{
    for (uint32_t i = 0; out && i < cap && i < s_barN && i < VKR_TEST_BARRIERS; i++) {
        out[i] = s_bar[i];
    }
    return s_barN;
}

static void vkr_bar_note(VkPipelineStageFlags src, VkPipelineStageFlags dst, uint32_t nMem,
                         const VkMemoryBarrier *mb, uint32_t nBuf, uint32_t nImg)
{
    if (s_barN < VKR_TEST_BARRIERS) {
        s_bar[s_barN] = (VkrTestBarrier){(uint32_t)src,
                                         (uint32_t)dst,
                                         nMem && mb ? (uint32_t)mb->srcAccessMask : 0u,
                                         nMem && mb ? (uint32_t)mb->dstAccessMask : 0u,
                                         nMem,
                                         nBuf,
                                         nImg};
    }
    s_barN++;
}

#define vkCmdPipelineBarrier(cb, src, dst, dep, nMem, mb, nBuf, bufs, nImg, imgs)                  \
    do {                                                                                           \
        if (s_barTrace) {                                                                          \
            vkr_bar_note((src), (dst), (nMem), (mb), (nBuf), (nImg));                              \
        }                                                                                          \
        vkCmdPipelineBarrier((cb), (src), (dst), (dep), (nMem), (mb), (nBuf), (bufs), (nImg),      \
                             (imgs));                                                              \
    } while (0)

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
    const char *da = getenv("ICO_VK_DEPTH_ALWAYS");
    g_vkr.depthAlways = da && da[0] && da[0] != '0';
    if (g_vkr.depthAlways) {
        VKR_LOG("ICO_VK_DEPTH_ALWAYS: every depth test passes (the occlusion measurement's run "
                "without the depth; not the game's picture)");
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
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (!VKR_CHECK(vkCreateFence(g_vkr.device, &fci, NULL, &f->acquireFence))) {
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
        f->gen = i & VKR_GEN_MASK; /* the frame index the slot would hold first */
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
        if (f->acquireFence) {
            vkDestroyFence(g_vkr.device, f->acquireFence, NULL);
        }
        if (f->queryPool) {
            vkDestroyQueryPool(g_vkr.device, f->queryPool, NULL);
        }
        if (f->occPool) {
            vkDestroyQueryPool(g_vkr.device, f->occPool, NULL);
        }
        memset(f, 0, sizeof(*f));
    }
    free(g_vkr.occResult);
    g_vkr.occResult = NULL;
    g_vkr.occUsed = false;
    g_vkr.occCount = 0;
}

/* rhi_gpu_progress (rhi.h): the values a watchdog reads from another
 * thread while this one may be blocked in the driver; only the submitting
 * thread stores them */
void vkr_progress_reset(void)
{
    __atomic_store_n(&g_vkr.progWaitingFor, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_vkr.progFinished, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_vkr.progSubmitted, 0, __ATOMIC_RELEASE);
}

/* value seen finished (a wait that returned, a counter read) */
static void vkr_progress_finished(uint64_t value)
{
    if (value > __atomic_load_n(&g_vkr.progFinished, __ATOMIC_RELAXED)) {
        __atomic_store_n(&g_vkr.progFinished, value, __ATOMIC_RELEASE);
    }
}

/* a wait for value starts (value != 0) or ends (0) */
static void vkr_progress_wait(uint64_t value)
{
    if (value != 0) {
        __atomic_store_n(&g_vkr.progWaitSinceNs, vkr_now_ns(), __ATOMIC_RELAXED);
    }
    __atomic_store_n(&g_vkr.progWaitingFor, value, __ATOMIC_RELEASE);
}

/* vkr_wait_value; poll: the frame slot whose last submission it is, its
 * stamps polled while the wait blocks (vk_tsclock.c), or NULL */
static void vkr_wait_value_on(uint64_t value, VkrFrame *poll)
{
    /* a lost device finishes nothing more: no wait (the first failed one
     * is the report's, vkr_lost_report) */
    if (value == 0 || g_vkr.deviceLost) {
        return;
    }
    /* a wait that blocks is counted with its time. The watchdog sees the
     * wait from the counter read on (rhi_gpu_progress): a driver may block
     * in either call while the GPU hangs */
    uint64_t done = 0;
    vkr_progress_wait(value);
    if (vkr_sync_reached(&done)) {
        vkr_progress_finished(done);
        if (done >= value) {
            vkr_progress_wait(0);
            return;
        }
    }
    const uint64_t t0 = vkr_now_ns();
    const VkResult r = poll ? vkr_tsclock_wait(poll, value) : vkr_sync_wait(value);
    vkr_progress_wait(0);
    const uint64_t waited = vkr_now_ns() - t0;
    if (r == VK_SUCCESS) {
        vkr_progress_finished(value);
    }
    if (r != VK_SUCCESS) {
        /* the value waited for against the one the GPU reached: the
         * submissions between them never finished (the loss report names
         * their frames, vkr_lost_report) */
        uint64_t reached = 0;
        const bool known = vkr_sync_reached(&reached);
        VKR_LOG("waited %.1f s for submission %llu (the last submitted is %llu; sync %s); the "
                "GPU reached %llu%s",
                (double)waited / 1e9, (unsigned long long)value,
                (unsigned long long)__atomic_load_n(&g_vkr.timelineValue, __ATOMIC_ACQUIRE),
                vkr_sync_name(), (unsigned long long)reached,
                known ? "" : " (the counter read failed too)");
        if (!__atomic_load_n(&g_vkr.lostWaitValue, __ATOMIC_RELAXED)) {
            /* atomic: rhi_gpu_progress reads it from the game thread */
            __atomic_store_n(&g_vkr.lostWaitValue, value, __ATOMIC_RELAXED);
        }
    }
    VKR_CHECK(r);
    g_vkr.stats.fenceWaits++;
    g_vkr.stats.fenceWaitNs += waited;
}

void vkr_wait_value(uint64_t value)
{
    vkr_wait_value_on(value, NULL);
}

void vkr_wait_slot_value(VkrFrame *f, uint64_t value)
{
    vkr_wait_value_on(value, f);
}

void vkr_note_submit(const VkrFrame *f, uint64_t value, uint32_t lists, bool present)
{
    const uint32_t i = g_vkr.submitCount++ % VKR_SUBMIT_HISTORY;
    __atomic_store_n(&g_vkr.progReplay, f->tagReplay, __ATOMIC_RELAXED);
    __atomic_store_n(&g_vkr.progFrame, f->tagFrame, __ATOMIC_RELAXED);
    __atomic_store_n(&g_vkr.progSubmitNs, vkr_now_ns(), __ATOMIC_RELAXED);
    __atomic_store_n(&g_vkr.progSubmitted, value, __ATOMIC_RELEASE);
    g_vkr.submits[i].value = value;
    g_vkr.submits[i].replay = f->tagReplay;
    g_vkr.submits[i].frame = f->tagFrame;
    g_vkr.submits[i].lists = (uint8_t)(lists > 255u ? 255u : lists);
    g_vkr.submits[i].present = present ? 1u : 0u;
    {
        /* the same numbers for rhi_gpu_progress (the loss dump's lost frame,
         * read from the game thread while this thread submits): atomic words */
        const uint32_t k =
            __atomic_load_n(&g_vkr.progTagCount, __ATOMIC_RELAXED) % VKR_SUBMIT_HISTORY;

        __atomic_store_n(&g_vkr.progTagTag[k], ((uint64_t)f->tagReplay << 32) | f->tagFrame,
                         __ATOMIC_RELAXED);
        __atomic_store_n(&g_vkr.progTagValue[k], value, __ATOMIC_RELAXED);
        __atomic_store_n(&g_vkr.progTagCount, g_vkr.submitCount, __ATOMIC_RELEASE);
    }
}

/* the recording thread's slot's tag: its submits carry it, wherever they
 * are made (vkr_note_submit) */
void rhi_set_submit_tag(uint32_t replay, uint32_t frame)
{
    VkrFrame *f = vkr_cur_frame();
    f->tagReplay = replay;
    f->tagFrame = frame;
}

/* the finished slot's timestamps into g_vkr.tsResult (the slot is complete:
 * nothing waits) */
static void vkr_collect_timestamps(VkrFrame *f)
{
    g_vkr.tsCount = 0;
    memset(g_vkr.tsResult, 0, sizeof(g_vkr.tsResult));
    /* the results up to the highest index written */
    uint32_t hi = 0;
    for (uint32_t i = 0; i < RHI_MAX_TIMESTAMPS; i++) {
        if (rhi_ts_has(f->tsWritten, i)) {
            hi = i + 1;
        }
    }
    /* what the slot's submitted lists recorded, for rhi_read_timestamp_clock
     * (a list recorded and never submitted wrote nothing on the GPU: its
     * stamps would read as unwritten, port/render/rd_perf_clock.c), and
     * how many lists with stamps were not submitted */
    for (uint32_t w = 0; w < RHI_TIMESTAMP_WORDS; w++) {
        g_vkr.tsClock.recorded[w] =
            f->tsWritten[w] & __atomic_load_n(&f->tsSubmitted[w], __ATOMIC_RELAXED);
    }
    const uint32_t stamped = __atomic_load_n(&f->tsListsStamped, __ATOMIC_RELAXED);
    const uint32_t sent = __atomic_load_n(&f->tsListsSubmitted, __ATOMIC_RELAXED);
    g_vkr.tsClock.listsUnsubmitted = stamped > sent ? stamped - sent : 0u;
    memset(f->tsSubmitted, 0, sizeof(f->tsSubmitted));
    f->tsListsStamped = f->tsListsSubmitted = 0;
    if (!f->queryPool || hi == 0) {
        memset(f->tsWritten, 0, sizeof(f->tsWritten));
        return;
    }
    uint64_t data[RHI_MAX_TIMESTAMPS][2];
    memset(data, 0, sizeof(data[0]) * hi);
    VkResult r = vkGetQueryPoolResults(
        g_vkr.device, f->queryPool, 0, hi, sizeof(data[0]) * hi, data, sizeof(data[0]),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (r == VK_SUCCESS || r == VK_NOT_READY) {
        bool first = true;
        f->tsPrevFirst = 0;
        for (uint32_t i = 0; i < hi; i++) {
            if (rhi_ts_has(f->tsWritten, i) && data[i][1]) {
                g_vkr.tsResult[i] =
                    (uint64_t)((double)(data[i][0] & g_vkr.timestampMask) * g_vkr.timestampPeriod);
                g_vkr.tsCount = i + 1;
                rhi_ts_add(g_vkr.tsClock.available, i);
                if (first) {
                    /* the lowest recorded stamp's value: a poll of the
                     * slot's next use that reads it again reads this use's
                     * results, before that use's reset ran (vk_tsclock.c) */
                    f->tsPrevFirst = data[i][0] & g_vkr.timestampMask;
                }
            }
            if (rhi_ts_has(f->tsWritten, i)) {
                first = false;
            }
        }
    }
    memset(f->tsWritten, 0, sizeof(f->tsWritten));
}

/* the finished slot's occlusion counts into g_vkr.occResult, as the
 * timestamps above (rhi_read_occlusion) */
static void vkr_collect_occlusion(VkrFrame *f)
{
    if (!g_vkr.occResult) {
        return;
    }
    const uint32_t clear = g_vkr.occCount;
    for (uint32_t i = 0; i < clear; i++) {
        g_vkr.occResult[i] = RHI_OCCLUSION_NONE;
    }
    g_vkr.occCount = 0;
    uint32_t hi = 0;
    for (uint32_t w = 0; w < RHI_OCCLUSION_WORDS; w++) {
        if (f->occWritten[w]) {
            hi = w * 32u + 32u - (uint32_t)__builtin_clz(f->occWritten[w]);
        }
    }
    if (!f->occPool || hi == 0) {
        memset(f->occWritten, 0, sizeof(f->occWritten));
        return;
    }
    /* the (count, availability) pairs up to the highest index written */
    uint64_t *data = malloc(sizeof(uint64_t) * 2u * hi);
    if (data) {
        memset(data, 0, sizeof(uint64_t) * 2u * hi);
        const VkResult r = vkGetQueryPoolResults(
            g_vkr.device, f->occPool, 0, hi, sizeof(uint64_t) * 2u * hi, data,
            sizeof(uint64_t) * 2u, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (r == VK_SUCCESS || r == VK_NOT_READY) {
            for (uint32_t i = 0; i < hi; i++) {
                if (((f->occWritten[i >> 5] >> (i & 31u)) & 1u) && data[2u * i + 1u]) {
                    g_vkr.occResult[i] = data[2u * i];
                }
            }
            g_vkr.occCount = hi;
        }
        free(data);
    }
    memset(f->occWritten, 0, sizeof(f->occWritten));
}

/* Recycles a frame slot once the GPU is done with it. */
static void vkr_recycle_frame(VkrFrame *f)
{
    /* the slot's clock (rhi_read_timestamp_clock): the wait's trace, the
     * stamps' bits, then the submit times and the calibrated pair */
    memset(&g_vkr.tsClock, 0, sizeof(g_vkr.tsClock));
    vkr_wait_slot_value(f, f->waitValue);
    vkr_collect_timestamps(f);
    vkr_tsclock_recycled(f);
    vkr_collect_occlusion(f);
    f->tsReset = false;
    f->occReset = false;
    /* the acquire's fence, reset once it signalled (a fence an acquire
     * never signalled, a picture given up, stays used: the next acquire
     * of the slot makes a fresh one) */
    if (f->acquireFenceUsed && vkGetFenceStatus(g_vkr.device, f->acquireFence) == VK_SUCCESS &&
        vkResetFences(g_vkr.device, 1, &f->acquireFence) == VK_SUCCESS) {
        f->acquireFenceUsed = false;
    }
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

/* The next slot, recycled: its own last submit waited for.  The call guard
 * (rhi_backend.c) lets it run beside the submit thread only when none of
 * that thread's jobs is of this slot (RhiSlotGuard), so the slot's lists
 * were all submitted and its waitValue is the last. */
void rhi_wait_frame(void)
{
    const VkrFrame *prev = vkr_cur_frame();
    g_vkr.frameIndex++;
    VkrFrame *f = vkr_cur_frame();
    vkr_recycle_frame(f);
    f->gen = (uint32_t)(g_vkr.frameIndex & VKR_GEN_MASK);
    /* the tag in force goes on until the next rhi_set_submit_tag */
    f->tagReplay = prev->tagReplay;
    f->tagFrame = prev->tagFrame;
}

uint32_t rhi_frame_slot(void)
{
    return (uint32_t)(g_vkr.frameIndex % RHI_FRAMES_IN_FLIGHT);
}

bool rhi_commands_left(void)
{
    return vkr_cur_frame()->listCount < VKR_MAX_CMD_LISTS;
}

void rhi_wait_idle(void)
{
    const uint64_t t0 = vkr_now_ns();
    /* a loss found here is noted and reported like any other (the loss
     * bisect waits here after each replay, rd_dump.c rd_loss_bisect); a
     * device already lost is not asked again */
    if (!g_vkr.deviceLost) {
        /* a watchdog sees the wait (rhi_gpu_progress): everything submitted */
        const uint64_t all = g_vkr.timelineValue;
        vkr_progress_wait(all);
        const bool ok = VKR_CHECK(vkDeviceWaitIdle(g_vkr.device));
        vkr_progress_wait(0);
        if (ok) {
            vkr_progress_finished(all);
        }
    }
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
static void vkr_occ_close(VkrCmdList *c);

/* VkrCmdList.known: the bind cache's state the list holds (vk_internal.h) */
#define VKR_KNOW_VIEWPORT 1u
#define VKR_KNOW_SCISSOR 2u
#define VKR_KNOW_INDEX 4u
#define VKR_KNOW_STENCIL 8u
#define VKR_KNOW_BLEND_CONSTANT 16u

/* ---------------------------------------------------------- command lists
 * Handle id: (frame tag << 20) | (list index + 1).  The tag is the frame
 * index the list's slot was recycled at (masked), so it names the slot
 * (tag % RHI_FRAMES_IN_FLIGHT) and goes stale when that slot is recycled
 * again: a list of slot A stays valid for the submit thread while the
 * recording thread has moved on to slot B. */
VkrCmdList *vkr_get_cmd(RhiCommandList cl)
{
    const uint32_t tag = cl.id >> VKR_GEN_SHIFT;
    VkrFrame *f = &g_vkr.frames[tag % RHI_FRAMES_IN_FLIGHT];
    uint32_t idx = cl.id & VKR_INDEX_MASK;
    if (idx == 0 || idx > f->listCount || tag != f->gen) {
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
    c->slot = (uint8_t)(f - g_vkr.frames);
    memset(c->tsWritten, 0, sizeof(c->tsWritten));
    /* the bind cache starts empty (the slot's recycle zeroed it too) */
    c->pipeline = NULL;
    c->pipelineId = 0;
    c->known = 0;
    c->vbKnown = 0;
    c->dynFresh = 0;
    c->externalOpen = false;
    c->pendCount = 0;
    c->pendSrc = c->pendDst = 0;
    c->pendMemDst = c->pendMemSrc = 0;
    c->presentImage = VK_NULL_HANDLE;
    c->swapImage = VK_NULL_HANDLE;
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
    if (f->occPool && !f->occReset) {
        /* likewise the occlusion queries, once a run has used them: the
         * range in use (rhi_occlusion_prepare) */
        vkCmdResetQueryPool(c->cb, f->occPool, 0, g_vkr.occRange);
        f->occReset = true;
        f->occResetN = g_vkr.occRange;
    }
    out.id = (f->gen << VKR_GEN_SHIFT) | f->listCount;
    return out;
}

static void vkr_flush_barriers_to(VkrCmdList *c, bool late);

/* A slot call (rhi_backend.c): on the recording thread beside the submit
 * thread's work of the other slot, or on the submit thread with
 * RhiLimits.slotOverlap.  Either way its own list only (its command buffer,
 * of its slot's pool), and the barriers it sends counted in late (the
 * recording thread writes stats; atomic adds) */
void rhi_end_commands(RhiCommandList cl)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->recording) {
        return;
    }
    vkr_flush_barriers_to(c, true);
    if (c->inPass) {
        VKR_LOG("rhi_end_commands inside a render pass");
        vkr_occ_close(c);
        vkr_pass_end(c);
        c->inPass = false;
    }
    /* Copies into READBACK buffers become visible to the host once this
     * submission has finished; and this list's copies are ordered before
     * every later list's (the transfer half of the order between lists,
     * "Hazards" above: a later copy into a texture or buffer this list
     * copied into or from, in the same state, needs no barrier of its own).
     * Not counted in stats.barriers (it is every list's, as it was before
     * the transfer half moved here from vkr_open_barrier).  The wide scopes
     * (VkrState.tile.wide, rhi_tile_rule.h) keep v0.11.2's: the host's read
     * alone here, the transfer half in every list's opening barrier */
    const bool wide = g_vkr.tile.wide;
    VkMemoryBarrier mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT |
                         (wide ? 0u : VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT),
    };
    vkCmdPipelineBarrier(c->cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT | (wide ? 0u : VK_PIPELINE_STAGE_TRANSFER_BIT),
                         0, 1, &mb, 0, NULL, 0, NULL);
    VKR_CHECK(vkEndCommandBuffer(c->cb));
    c->recording = false;
}

/* Submits command buffers (possibly none) with the frame's swapchain
 * semaphores attached as needed, taking the counter's next number
 * (vkr_sync_submit).
 * forPresent: the batch also signals the acquired image's present
 * semaphore (renderDone), either as an empty batch after all frame work
 * (vkr_submit_present_signal) or as the submit of the list that moved the
 * image to PRESENT (rhi_submit). */
static bool vkr_submit_batch(VkrFrame *f, const VkCommandBuffer *cbs, uint32_t count,
                             bool forPresent, bool takeAcquire)
{
    VkrSubmitDesc d = {.cbs = cbs, .cbCount = count};
    bool tookAcquire = false;

    if (f->acq.waitPending && (count == 0 || takeAcquire)) {
        /* The submit of the list that uses the acquired image waits for it
         * (takeAcquire: the list put a barrier on that image or drew into
         * it, VkrCmdList.swapImage),
         * at COLOR_ATTACHMENT_OUTPUT: the image's first use after an
         * acquire is a barrier out of UNDEFINED or PRESENT, and such a
         * barrier on a swapchain image has that stage in its first scope
         * (vkr_image_barrier), which chains it to this wait.  A semaphore
         * wait holds that stage for every command of its batch, so a list
         * that does not touch the image is submitted without it: with the
         * wait on one list of off-screen passes and the window's pass
         * together, every colour pass of the picture waited for the
         * presentation engine (issue 179: 38-84 ms of a 88-143 ms scene
         * picture on a Mali, in whichever pass wrote colour first).  The
         * renderer records a picture's scene in one list and the present's
         * pass in a second (port/render/rd_submit.c), so only the second
         * waits.  An empty batch (the present's semaphore signal when no
         * list touched the image, a dropped acquire's drain) takes it at
         * ALL_COMMANDS.  The slot's own acquire: a submit of slot A never
         * takes the image slot B acquired since. */
        if (count && f->acq.fenced) {
            /* the present's list: was the display's image free already? */
            vkr_late_add(vkGetFenceStatus(g_vkr.device, f->acquireFence) == VK_SUCCESS
                             ? &g_vkr.late.acquireReady
                             : &g_vkr.late.acquireNotReady,
                         1u);
            f->acq.fenced = false;
        }
        d.wait = f->acquireSem;
        d.waitStage = count ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                            : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        f->acq.waitPending = false;
        tookAcquire = true;
    }
    if (forPresent) {
        /* an empty batch after all frame work: on the timeline path a wait
         * for the previous number orders it after every earlier submission
         * (the fence path has none: the semaphore signal below covers every
         * earlier submission on the queue already) */
        d.afterAll = count == 0;
        /* the binary semaphore the present waits on; a semaphore signal
         * covers the batch's commands and every earlier submission */
        d.signal = g_vkr.renderDone[f->acq.image];
    }
    uint64_t value = 0;
    if (!vkr_sync_submit(&d, &value)) {
        /* a failed submit other than a device loss leaves its semaphores
         * as they were: the acquire semaphore is still signalled, and the
         * next submit must wait on it before the next acquire reuses it */
        if (tookAcquire && !__atomic_load_n(&g_vkr.deviceLost, __ATOMIC_ACQUIRE)) {
            f->acq.waitPending = true;
        }
        return false;
    }
    vkr_late_add(&g_vkr.late.submits, 1u);
    f->waitValue = value;
    vkr_note_submit(f, value, count, forPresent);
    vkr_tsclock_note_submit(f, value);
    return true;
}

/* Submits nothing but the wait on slot f's pending acquire semaphore, then
 * waits for it: a swapchain recreate drops the acquire, and the semaphore
 * must not stay signalled for the slot's next acquire. */
bool vkr_submit_empty(VkrFrame *f)
{
    if (!f->acq.waitPending) {
        return true;
    }
    if (!vkr_submit_batch(f, NULL, 0, false, true)) {
        return false;
    }
    vkr_wait_value(f->waitValue);
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
     * present's wait.  Today the present's own list (port/render/
     * rd_submit.c, rd__present_record) and rd_video.c each present from
     * one list. */
    VkrFrame *f = &g_vkr.frames[c->slot];
    bool signal = false, takeAcquire = false;
    if (f->acq.acquired && (c->presentImage != VK_NULL_HANDLE || c->swapImage != VK_NULL_HANDLE)) {
        const VkrTexture *bb = vkr_get_texture((RhiTexture){g_vkr.swapTextures[f->acq.image]});
        signal = !f->acq.signalled && bb && bb->image == c->presentImage;
        /* the acquire's wait goes with the list that uses the image (vkr_submit_batch) */
        takeAcquire = bb && bb->image == c->swapImage;
    }
    const bool sent = vkr_submit_batch(f, &c->cb, 1, signal, takeAcquire);
    if (sent && signal) {
        f->acq.signalled = true;
    }
    /* its stamps reach the GPU: the slot's tsSubmitted (vkr_collect_timestamps) */
    bool stamped = false;
    for (uint32_t w = 0; sent && w < RHI_TIMESTAMP_WORDS; w++) {
        if (c->tsWritten[w]) {
            __atomic_or_fetch(&f->tsSubmitted[w], c->tsWritten[w], __ATOMIC_RELAXED);
            stamped = true;
        }
    }
    if (stamped) {
        __atomic_add_fetch(&f->tsListsSubmitted, 1u, __ATOMIC_RELAXED);
    }
}

bool vkr_submit_present_signal(VkrFrame *f)
{
    return vkr_submit_batch(f, NULL, 0, true, true);
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
/* Buffer barriers: the stages that can touch a buffer.  A barrier's first
 * scope is every command recorded or submitted before it, at the stages
 * its source mask names, whichever resource they used: so a fragment stage
 * there makes the barrier wait for all earlier fragment work, the previous
 * picture's included, and every command after it that its second scope
 * holds waits too (and whatever waits for those).  On a tile-based GPU the
 * vertex and compute work of a picture can run beside the fragment work of
 * the one before only when no barrier between them joins the two (issue
 * 179: the VU record's compute bracket and the mesh uploads' copies named
 * the fragment stage, so the record and every vertex after it waited for
 * the picture before's last pixel).  So each buffer barrier names the
 * stages that can have used that buffer, no wider.
 *
 * Earlier uses (a first scope: the earlier reads a copy or a store must
 * not overtake, the earlier writes it must follow), from what Vulkan lets
 * reach the buffer and what has:
 *   - vertex input: a buffer made as a vertex or index buffer (binding one
 *     needs the usage bit);
 *   - the vertex shader: a uniform or storage buffer (a descriptor needs
 *     the usage bit); the compute shader too where the VU record's compute
 *     builds exist (RhiLimits.computeStores: they read the stream at t0,
 *     the ring at t5 and the record, and store the record), the only
 *     compute work that reads RHI buffers;
 *   - the fragment shader: a uniform buffer always; a storage buffer only
 *     once a fragment module that declares one was made
 *     (VkrState.fragmentStorage, vk_resource.c): a shader can read a
 *     storage buffer only through a resource its module declares, and the
 *     flag is set before that module can be in any pipeline or draw.  None
 *     of port/shaders' 105 fragment modules declares one (they read the
 *     frame's and the draw's uniform blocks; the VU programs hand the
 *     record's planes to the pixel side as flat varyings,
 *     vu_common.hlsli vu_rec_planes), so the renderer's storage buffers,
 *     the mesh arena and the VU record, are reached by the vertex input,
 *     vertex and compute stages alone;
 *   - the transfer stage: a buffer a copy has read or written
 *     (VkrBuffer.copied; a copy's own first scope always has it).
 * The uniform ring and the bone ring the record reads (the slot's upload
 * ring, port/render/rd_upload.c) are host-written mapped memory, one per
 * frame slot, rewritten only after the slot's fence: no GPU barrier is
 * theirs (the submit makes the host's writes visible).
 *
 * Later uses (a second scope: what a copy's or store's writes become
 * visible to) cannot be narrowed by what has happened (a later module or
 * group may read the buffer anywhere its usage allows), so they are the
 * usage's: vertex input for a vertex or index buffer, the vertex, fragment
 * (and compute) shaders for a uniform or storage buffer, the transfer stage
 * for every buffer a copy may touch.  A fragment stage there makes only
 * later fragment work wait for the write, which costs no overlap.
 *
 * The wide scopes (VkrState.tile.wide: a tile-based phone GPU by default, or a
 * start after a lost one; rhi_tile_rule.h) are v0.11.2's instead, for every
 * buffer whatever its usage or history: vertex input, the vertex and
 * fragment shaders, the compute shader where the compute record exists and
 * the transfer stage on both sides, with every read access and the
 * transfer's writes (vkr_buf_wide_stages, VKR_BUF_WIDE_ACCESS).  The picture
 * is the same either way (ordering only); the wide scopes keep a picture's
 * uploads, VU record and vertex work behind the picture before's fragment
 * work, as every release before v0.11.3 did. */
static VkPipelineStageFlags vkr_buf_compute_stage(void)
{
    return g_vkr.computeStores ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : 0u;
}

#define VKR_BUF_WIDE_ACCESS                                                                        \
    (VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | \
     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)

static VkPipelineStageFlags vkr_buf_wide_stages(void)
{
    return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | vkr_buf_compute_stage() |
           VK_PIPELINE_STAGE_TRANSFER_BIT;
}

static VkPipelineStageFlags vkr_buf_earlier_stages(const VkrBuffer *b)
{
    if (g_vkr.tile.wide) {
        return vkr_buf_wide_stages();
    }
    VkPipelineStageFlags s = 0;
    if (b->vkUsage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        s |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    }
    if (b->vkUsage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        s |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | vkr_buf_compute_stage();
    }
    if ((b->vkUsage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ||
        ((b->vkUsage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
         __atomic_load_n(&g_vkr.fragmentStorage, __ATOMIC_ACQUIRE))) {
        s |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    if (b->copied) {
        s |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    return s;
}

/* the later uses' stages, and in *access their accesses (each supported by
   a stage it names, as the validation layer checks) */
static VkPipelineStageFlags vkr_buf_later_stages(const VkrBuffer *b, VkAccessFlags *access)
{
    if (g_vkr.tile.wide) {
        *access = VKR_BUF_WIDE_ACCESS;
        return vkr_buf_wide_stages();
    }
    VkPipelineStageFlags s = 0;
    VkAccessFlags a = 0;
    if (b->vkUsage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
        s |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        a |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if (b->vkUsage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
        s |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        a |= VK_ACCESS_INDEX_READ_BIT;
    }
    if (b->vkUsage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        s |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
             vkr_buf_compute_stage();
        a |= (b->vkUsage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ? VK_ACCESS_UNIFORM_READ_BIT : 0u;
        a |= (b->vkUsage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ? VK_ACCESS_SHADER_READ_BIT : 0u;
    }
    if (b->vkUsage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        s |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        a |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    *access = a;
    return s ? s : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
}

/* The barriers that hold later vertex, compute and copy work behind earlier
 * fragment work (VkrState.holdSite, RhiStats.fragmentHolds and holdSites):
 * a first scope with a fragment-side stage (the fragment shader, the depth
 * tests, colour output, or a mask that holds them all) and a second with a
 * stage that runs before them.  Two kinds: RHI_HOLD_KIND_VERTEX, whose second
 * scope holds draw indirect, vertex input, the vertex or another
 * pre-rasterisation shader, compute, the top of the pipe or a mask that
 * holds them all (the picture's VU record and vertex work wait behind the
 * fragment work before it), and RHI_HOLD_KIND_COPY, whose second scope holds
 * the transfer stage alone (later copies wait, and vertex or compute work
 * only where a copy's own barrier passes the wait on: every copy out of a
 * render target is one).  Counted where each vkCmdPipelineBarrier is
 * recorded (a few dozen a picture, none a draw); one line for each of the
 * first 8 of a site and kind, then none (the counts go to the 10 s
 * summary through RhiStats.holdSites: port/render/rd_perf.c), with the
 * masks: a reporter's log then names the barriers that keep a picture from
 * starting beside the one before.  A copy over a sampled texture is one by
 * its nature (the copy waits for the sampling). */
typedef enum VkrHoldSite {
    VKR_HOLD_OPEN = RHI_HOLD_SITE_OPEN,         /* a list's opening barrier */
    VKR_HOLD_DEFERRED = RHI_HOLD_SITE_DEFERRED, /* the barriers held back, sent before a command */
    VKR_HOLD_HAZARD = RHI_HOLD_SITE_HAZARD, /* a pass's or copy's barrier, with those held back */
    VKR_HOLD_COPY = RHI_HOLD_SITE_COPY,     /* a buffer copy's wait for its destination's uses */
    VKR_HOLD_GLOBAL = RHI_HOLD_SITE_GLOBAL, /* the global barriers (ICO_VK_GLOBAL_BARRIERS=1) */
    VKR_HOLD_EXTERNAL = RHI_HOLD_SITE_EXTERNAL, /* the external compute's bracket (rhi_native.h) */
    VKR_HOLD_SITES = RHI_HOLD_SITES
} VkrHoldSite;

_Static_assert(VKR_HOLD_SITES <= sizeof(g_vkr.holdSite[0]) / sizeof(g_vkr.holdSite[0][0]),
               "VkrState.holdSite holds every site");

static const char *const vkr_holdSiteName[VKR_HOLD_SITES] = {
    "a command list's opening barrier",
    ("the barriers held back for the next command (image transitions, the store or compute "
     "bracket, a copy's visibility)"),
    "a render pass's or copy's own barrier (its hazards, with the barriers held back)",
    "a buffer copy's wait for the earlier uses of its destination",
    "a global barrier (ICO_VK_GLOBAL_BARRIERS=1, or command lists recorded interleaved)",
    "the external compute's bracket (FidelityFX)",
};

#define VKR_FRAGMENT_SIDE                                                                          \
    (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |          \
     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |   \
     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT |                   \
     VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
/* (the top of the pipe in a second scope stands for every stage, as the
   bottom does in a first) */
#define VKR_BEFORE_FRAGMENT_VERTEX                                                                 \
    (VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |                     \
     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |                    \
     VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |                                           \
     VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |                                        \
     VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |                \
     VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
#define VKR_BEFORE_FRAGMENT (VKR_BEFORE_FRAGMENT_VERTEX | VK_PIPELINE_STAGE_TRANSFER_BIT)

/* late: a list's end (VkrState.late, either thread) */
static void vkr_hold_note(VkrHoldSite site, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                          uint32_t images, bool late)
{
    if (!(src & VKR_FRAGMENT_SIDE) || !(dst & VKR_BEFORE_FRAGMENT)) {
        return;
    }
    if (late) {
        vkr_late_add(&g_vkr.late.fragmentHolds, 1u);
    } else {
        g_vkr.stats.fragmentHolds++;
    }
    const int kind = (dst & VKR_BEFORE_FRAGMENT_VERTEX) ? RHI_HOLD_KIND_VERTEX : RHI_HOLD_KIND_COPY;
    const uint64_t n = __atomic_add_fetch(&g_vkr.holdSite[kind][site], 1u, __ATOMIC_RELAXED);
    if (n <= 8u) {
        VKR_LOG("barriers: one holds every later %s (the next picture's too) behind the "
                "fragment work before it: %s (first scope 0x%x, second 0x%x, %u image "
                "barrier%s); %llu of this kind at this site (the first 8 of each said; the "
                "counts in the 10 s summary)",
                kind == RHI_HOLD_KIND_VERTEX
                    ? "vertex, compute and copy command"
                    : "copy (vertex and compute work only through a copy's "
                      "own barrier)",
                vkr_holdSiteName[site], (unsigned)src, (unsigned)dst, images,
                images == 1u ? "" : "s", (unsigned long long)n);
    }
}

static void vkr_flush_barriers(VkrCmdList *c)
{
    vkr_flush_barriers_to(c, false);
}

/* late: counted in VkrState.late (a list's end, which the submit thread
 * may make), else in stats */
static void vkr_flush_barriers_to(VkrCmdList *c, bool late)
{
    if (c->pendCount == 0 && c->pendMemDst == 0) {
        return;
    }
    /* the transfer write only with the transfer stage in the first scope: a
       store bracket's end alone (rhi_cmd_buffer_store_end) has the vertex
       shader there, and the validation layer refuses an access its stage
       mask does not support */
    const VkMemoryBarrier mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask =
            ((c->pendSrc & VK_PIPELINE_STAGE_TRANSFER_BIT) ? VK_ACCESS_TRANSFER_WRITE_BIT : 0u) |
            c->pendMemSrc,
        .dstAccessMask = c->pendMemDst,
    };
    vkCmdPipelineBarrier(c->cb, c->pendSrc, c->pendDst, 0, c->pendMemDst ? 1u : 0u, &mb, 0, NULL,
                         c->pendCount, c->pendImg);
    vkr_hold_note(c->globalOrder ? VKR_HOLD_GLOBAL : VKR_HOLD_DEFERRED, c->pendSrc, c->pendDst,
                  c->pendCount, late);
    if (late) {
        vkr_late_add(&g_vkr.late.barriers, 1u);
    } else {
        g_vkr.stats.barriers++;
    }
    c->pendCount = 0;
    c->pendSrc = c->pendDst = 0;
    c->pendMemDst = c->pendMemSrc = 0;
}

/* The stages and accesses of t in state s (a barrier's scope on that
 * side): the state map's, but for a depth-stencil texture in
 * RHI_STATE_RENDER_TARGET_READ, the depth attachment a pass's fragment
 * shaders also read as an input attachment (RhiDepthAttachment.readable,
 * dynamic rendering local read: the same layout as the colour's, the depth
 * tests' stages and accesses with the fragment shader's read) */
static void vkr_state_scope(RhiState s, const VkrTexture *t, VkPipelineStageFlags *stages,
                            VkAccessFlags *access)
{
    if (s == RHI_STATE_RENDER_TARGET_READ && (t->aspects & VK_IMAGE_ASPECT_DEPTH_BIT)) {
        *stages = VKR_DEPTH_STAGES | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        *access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                  VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        return;
    }
    *stages = vkr_stateMap[s].stages;
    *access = vkr_stateMap[s].access;
    if (s == RHI_STATE_STORAGE && g_vkr.queueCompute) {
        /* a storage image is also what an external compute dispatch reads
         * and writes (rhi_native.h) */
        *stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
}

void vkr_image_barrier(VkrCmdList *c, VkrTexture *t, RhiState before, RhiState after)
{
    VkPipelineStageFlags src, dst;
    VkAccessFlags srcAccess, dstAccess;
    vkr_state_scope(before, t, &src, &srcAccess);
    vkr_state_scope(after, t, &dst, &dstAccess);
    if (t->swapchain) {
        /* the list's submit takes the acquire's wait when this is the
         * acquired image (rhi_submit) */
        c->swapImage = t->image;
    }
    if (t->swapchain && (before == RHI_STATE_UNDEFINED || before == RHI_STATE_PRESENT)) {
        /* a swapchain image's first barrier after an acquire: this list's
         * submit waits on the acquire semaphore at COLOR_ATTACHMENT_OUTPUT
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
        .dstAccessMask = dstAccess,
        .oldLayout = vkr_texture_layout(before, t),
        .newLayout = vkr_texture_layout(after, t),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = t->image,
        .subresourceRange = {t->aspects, 0, t->mips, 0, 1},
    };
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
    vkr_hold_note(VKR_HOLD_GLOBAL, stages, stages, 0u, false);
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
 * The record belongs to one command list (its epoch).  Every list is
 * ordered after every list submitted before it, this frame's and the
 * frames' still in flight, for the two kinds of same-state write the record
 * tracks, so a record from an older list counts as clean and nothing is
 * carried across submissions or frame slots: attachment writes and reads by
 * the barrier each list begins with (vkr_open_barrier: colour output and
 * the depth tests on both sides), transfer writes and reads by the barrier
 * each list ends with (rhi_end_commands: the transfer stage on both sides,
 * beside the host's read).  Before v0.11.3 one opening barrier held both
 * kinds, so its first scope's colour output reached its second scope's
 * transfer stage: the next picture's uploads, and the VU record and every
 * vertex shader after them, waited for the picture before's last pixel
 * (issue 179).  No other pair crosses a list without a barrier of its own:
 * an attachment written by a copy (or copied from after a pass) changes
 * state, and the caller's transition names both stages (vkr_image_barrier).
 * Lists recorded interleaved fall back to vkr_order_writes.  The wide scopes
 * (VkrState.tile.wide, rhi_tile_rule.h) keep v0.11.2's one barrier over
 * both kinds (the transfer stage on both sides here, the closing barrier
 * the host's read alone). */
static void vkr_open_barrier(VkCommandBuffer cb)
{
    const bool wide = g_vkr.tile.wide;
    const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                        VKR_DEPTH_STAGES |
                                        (wide ? VK_PIPELINE_STAGE_TRANSFER_BIT : 0u);
    VkMemoryBarrier mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         (wide ? VK_ACCESS_TRANSFER_WRITE_BIT : 0u),
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         (wide ? VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT : 0u),
    };
    vkCmdPipelineBarrier(cb, stages, stages, 0, 1, &mb, 0, NULL, 0, NULL);
    vkr_hold_note(VKR_HOLD_OPEN, stages, stages, 0u, false);
    g_vkr.stats.barriers++;
}

/* the barriers one pass or copy needs, emitted as one vkCmdPipelineBarrier */
typedef struct VkrHz {
    VkImageMemoryBarrier img[VKR_PENDING_BARRIERS + RHI_MAX_COLOR_TARGETS + 2];
    VkBufferMemoryBarrier buf[2];
    uint32_t imgCount, bufCount;
    VkPipelineStageFlags src, dst;
    /* the list's pending memory barrier (VkrCmdList.pendMemDst), and an
     * execution dependency with no barrier of its own (a copy into a
     * buffer shaders read waits for their earlier reads) */
    VkAccessFlags memDst;
    VkAccessFlags memSrc; /* beside the transfer writes (VkrCmdList.pendMemSrc) */
    bool force;
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
    h->memDst |= c->pendMemDst;
    h->memSrc |= c->pendMemSrc;
    c->pendCount = 0;
    c->pendSrc = c->pendDst = 0;
    c->pendMemDst = c->pendMemSrc = 0;
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
    if (h->imgCount + h->bufCount == 0 && h->memDst == 0 && !h->force) {
        return;
    }
    const VkMemoryBarrier mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask =
            ((h->src & VK_PIPELINE_STAGE_TRANSFER_BIT) ? VK_ACCESS_TRANSFER_WRITE_BIT : 0u) |
            h->memSrc, /* as vkr_flush_barriers_to: the access with its stage */
        .dstAccessMask = h->memDst,
    };
    vkCmdPipelineBarrier(cb, h->src, h->dst, 0, h->memDst ? 1u : 0u, &mb, h->bufCount, h->buf,
                         h->imgCount, h->img);
    vkr_hold_note(VKR_HOLD_HAZARD, h->src, h->dst, h->imgCount, false);
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
        if (srcBuf) {
            srcBuf->copied = true;
        }
        if (dstBuf) {
            dstBuf->copied = true;
        }
        return;
    }
    VkrHz h;
    h.imgCount = h.bufCount = 0;
    h.src = h.dst = 0;
    h.memDst = h.memSrc = 0;
    h.force = false;
    vkr_hz_take_pending(&h, c);
    if (dstBuf && dstBuf->shaderRead) {
        /* rhi.h: a copy into a buffer shaders read waits for their earlier
         * reads, as rhi_cmd_copy_buffer does: at the stages that can have
         * read it (vkr_buf_earlier_stages; the transfer stage for a copy's
         * read of it, which vkr_hz_buffer also orders within the list).
         * The wide scopes keep v0.11.2's first scope here: the shader stages
         * and vertex input alone, no transfer stage (and so no transfer
         * write in vkr_hz_flush's source access) unless a hazard of the list
         * adds it */
        h.src |= g_vkr.tile.wide
                     ? vkr_buf_wide_stages() & ~(VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT
                     : vkr_buf_earlier_stages(dstBuf) | VK_PIPELINE_STAGE_TRANSFER_BIT;
        h.dst |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        h.force = true;
    }
    if (srcBuf) {
        srcBuf->copied = true;
    }
    if (dstBuf) {
        dstBuf->copied = true;
    }
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
/* A render area must lie within every attachment (VkRenderingInfo,
 * VUID-VkRenderingInfo-pNext-06079/06080): one that does not is drawn over
 * memory past the image's end.  Such a pass is logged once (with the
 * sizes; an assert in the debug build), counted (vkr_test_pass_area_count)
 * and clamped to the smallest attachment, its default viewport and
 * scissor with it; every scissor set while the pass is open is cut to the
 * area too (rhi_cmd_set_scissor), so a later full-target scissor cannot
 * reach past it and the pass draws what lies inside.  A pass whose area
 * fits is untouched. */
static bool s_passAreaNoAssert; /* vkr_test_pass_area_no_assert */

uint32_t vkr_test_pass_area_count(void)
{
    return g_vkr.passAreaBad;
}

void vkr_test_pass_area_no_assert(bool on)
{
    s_passAreaNoAssert = on;
}

static void vkr_pass_area_fits(uint32_t *w, uint32_t *h, VkrTexture *const *color, uint32_t n,
                               const VkrTexture *depth)
{
    uint32_t mw = *w, mh = *h;
    for (uint32_t i = 0; i < n; i++) {
        mw = color[i]->width < mw ? color[i]->width : mw;
        mh = color[i]->height < mh ? color[i]->height : mh;
    }
    if (depth) {
        mw = depth->width < mw ? depth->width : mw;
        mh = depth->height < mh ? depth->height : mh;
    }
    if (mw == *w && mh == *h) {
        return;
    }
    if (g_vkr.passAreaBad++ == 0) {
        VKR_LOG("render pass %ux%u is larger than an attachment (colour %ux%u, depth %ux%u): "
                "drawn at %ux%u (reported once)",
                *w, *h, n ? color[0]->width : 0u, n ? color[0]->height : 0u,
                depth ? depth->width : 0u, depth ? depth->height : 0u, mw, mh);
    }
    if (!s_passAreaNoAssert) { /* read outside the assert: the release build keeps it */
#ifdef ICO_RHI_DEBUG_DEFAULT
        assert(!"render pass larger than an attachment");
#endif
    }
    *w = mw;
    *h = mh;
}

/* A pass's target without an attachment view: nothing in the RHI makes one
 * today (vk_resource.c gives every texture its view, or fails the
 * texture), but a null view handed to the driver's begin is the shape the
 * Intel crash reports blame (rhi_pass_row.h), so it is refused here before
 * any barrier or driver call and the pass's draws are skipped, as a legacy
 * pass whose objects could not be made.  The line names the target, the
 * first 8 then every 100th, with the count. */
static bool vkr_pass_has_view(const VkrTexture *t, const char *what, uint32_t i)
{
    static uint32_t s_noView;
    if (t->view) {
        return true;
    }
    s_noView++;
    if (s_noView <= 8u || s_noView % 100u == 0u) {
        VKR_LOG("render pass: target without an attachment view (%s %u, %ux%u, format %d; the "
                "pass's draws are skipped; %u so far)",
                what, i, t->width, t->height, (int)t->rhiFormat, s_noView);
    }
    return false;
}

/* The RHI pass as a VkrPassDesc (vk_internal.h), the hazard barriers its
 * attachments need, then vkr_pass_begin (vk_renderpass.c): dynamic
 * rendering, or a VkRenderPass and framebuffer (g_vkr.legacyPasses). */
void rhi_cmd_begin_render_pass(RhiCommandList cl, const RhiRenderPassDesc *pass)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !pass || pass->colorCount > RHI_MAX_COLOR_TARGETS) {
        return;
    }
    VkrPassDesc d;
    memset(&d, 0, sizeof(d));
    d.colorCount = pass->colorCount;
    VkrTexture *colorTex[RHI_MAX_COLOR_TARGETS];
    VkrTexture *depthTex = NULL;
    for (uint32_t i = 0; i < pass->colorCount; i++) {
        const RhiColorAttachment *a = &pass->color[i];
        VkrTexture *t = vkr_get_texture(a->texture);
        if (!t) {
            VKR_LOG("render pass: invalid colour target %u", i);
            return;
        }
        if (!vkr_pass_has_view(t, "colour", i)) {
            return;
        }
        colorTex[i] = t;
        if (t->swapchain) {
            c->swapImage = t->image; /* drawn here: the acquire's wait (rhi_submit) */
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
        /* readable: the exact blend's attachment read (rasterization
         * order, rasterOrder) or a read after rhi_cmd_local_read_barrier
         * (DATE's stencil setup), through dynamic rendering's local read
         * or the subpass read (VKR_PASS_READ) */
        if (a->readable && !VKR_PASS_READ) {
            VKR_LOG("render pass: a readable colour target without local read");
            return;
        }
        VkrPassAttach *pa = &d.color[i];
        pa->tex = t;
        pa->view = t->view;
        pa->format = t->format;
        pa->samples = (VkSampleCountFlagBits)t->samples;
        /* read by the pass's RHI_DST_READ_ATTACHMENT pipelines (GENERAL
         * for the subpass read, vkr_texture_layout) */
        pa->layout = vkr_texture_layout(
            a->readable ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_RENDER_TARGET, t);
        pa->load = vkr_loadOpMap[a->load < RHI_LOAD_COUNT ? a->load : 0].vk;
        /* DONT_CARE when asked (a tiler skips the write-back) */
        pa->store = a->store == RHI_STORE_DONT_CARE ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                    : VK_ATTACHMENT_STORE_OP_STORE;
        pa->stencilLoad = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        pa->stencilStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        pa->clear = cv;
    }
    if (pass->depth.texture.id) {
        VkrTexture *t = vkr_get_texture(pass->depth.texture);
        if (!t) {
            VKR_LOG("render pass: invalid depth target");
            return;
        }
        if (!vkr_pass_has_view(t, "depth", 0)) {
            return;
        }
        if (pass->depth.readable && (!g_vkr.localRead || pass->depth.readOnlyDepth)) {
            /* the subpass read reads a colour only (RhiLimits
             * .localReadSubpass): the fog's depth read is dynamic
             * rendering's */
            VKR_LOG("render pass: a readable depth target without local read%s, or read-only",
                    g_vkr.subpassRead ? " (the subpass read reads the colour only)" : "");
            return;
        }
        /* readable: the pass's fragment shaders read the depth as an input
         * attachment after rhi_cmd_local_read_barrier (the fog's local
         * read), the texture in RHI_STATE_RENDER_TARGET_READ
         * (RENDERING_LOCAL_READ, vkr_state_scope) */
        VkrPassAttach *pa = &d.depth;
        d.hasDepth = true;
        d.readOnlyDepth = pass->depth.readOnlyDepth;
        depthTex = t;
        pa->tex = t;
        pa->view = t->view;
        pa->format = t->format;
        pa->samples = (VkSampleCountFlagBits)t->samples;
        pa->layout = pass->depth.readOnlyDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                     : pass->depth.readable    ? VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR
                                               : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        pa->clear.depthStencil.depth = pass->depth.clearDepth;
        pa->clear.depthStencil.stencil = pass->depth.clearStencil;
        pa->load =
            vkr_loadOpMap[pass->depth.depthLoad < RHI_LOAD_COUNT ? pass->depth.depthLoad : 0].vk;
        pa->store = pass->depth.readOnlyDepth                  ? VK_ATTACHMENT_STORE_OP_NONE
                    : pass->depth.store == RHI_STORE_DONT_CARE ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                               : VK_ATTACHMENT_STORE_OP_STORE;
        pa->stencilLoad = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        pa->stencilStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        if (t->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
            d.hasStencil = true;
            pa->stencilLoad =
                vkr_loadOpMap[pass->depth.stencilLoad < RHI_LOAD_COUNT ? pass->depth.stencilLoad
                                                                       : 0]
                    .vk;
            /* the stencil's own store op; zero (RHI_STORE_STORE) keeps the
             * stencil */
            pa->stencilStore = pass->depth.readOnlyDepth ? VK_ATTACHMENT_STORE_OP_NONE
                               : pass->depth.stencilStore == RHI_STORE_DONT_CARE
                                   ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                   : VK_ATTACHMENT_STORE_OP_STORE;
        }
        /* STORE_OP_NONE is core in 1.3 only.  Dynamic rendering keeps the
         * version check (a 1.2 device stores, as it always has); a
         * VkRenderPass takes it where an extension gave it (storeOpNone:
         * VK_KHR/EXT_load_store_op_none, VK_QCOM_render_pass_store_ops) */
        const bool noneOk =
            g_vkr.legacyPasses ? g_vkr.storeOpNone : g_vkr.apiVersion >= VK_API_VERSION_1_3;
        if (!noneOk && pass->depth.readOnlyDepth) {
            pa->store = VK_ATTACHMENT_STORE_OP_STORE;
            if (d.hasStencil) {
                pa->stencilStore = VK_ATTACHMENT_STORE_OP_STORE;
            }
        }
    }
    uint32_t areaW = pass->width, areaH = pass->height;
    vkr_pass_area_fits(&areaW, &areaH, colorTex, pass->colorCount, depthTex);
    /* the framebuffer (legacy path): the attachments' common size, the
     * pass's own without one */
    d.fbW = areaW;
    d.fbH = areaH;
    /* the render area: RhiRenderPassDesc.area cut to the pass's size, the
     * whole of it when the area is not given or leaves nothing (Vulkan
     * wants an extent above zero).  The loads and stores apply inside it
     * only; the pixels outside are not accessed and keep their contents */
    const uint32_t passW = areaW, passH = areaH;
    uint32_t areaX = 0, areaY = 0;
    if (pass->area.w && pass->area.h) {
        const int64_t x0 = pass->area.x > 0 ? pass->area.x : 0;
        const int64_t y0 = pass->area.y > 0 ? pass->area.y : 0;
        int64_t x1 = (int64_t)pass->area.x + pass->area.w;
        int64_t y1 = (int64_t)pass->area.y + pass->area.h;
        x1 = x1 < (int64_t)passW ? x1 : (int64_t)passW;
        y1 = y1 < (int64_t)passH ? y1 : (int64_t)passH;
        if (x1 > x0 && y1 > y0) {
            areaX = (uint32_t)x0;
            areaY = (uint32_t)y0;
            areaW = (uint32_t)(x1 - x0);
            areaH = (uint32_t)(y1 - y0);
        }
    }
    d.areaX = areaX;
    d.areaY = areaY;
    d.areaW = areaW;
    d.areaH = areaH;
    if (pass->colorCount || depthTex) {
        d.fbW = d.fbH = UINT32_MAX;
        for (uint32_t i = 0; i < pass->colorCount; i++) {
            d.fbW = colorTex[i]->width < d.fbW ? colorTex[i]->width : d.fbW;
            d.fbH = colorTex[i]->height < d.fbH ? colorTex[i]->height : d.fbH;
        }
        if (depthTex) {
            d.fbW = depthTex->width < d.fbW ? depthTex->width : d.fbW;
            d.fbH = depthTex->height < d.fbH ? depthTex->height : d.fbH;
        }
    }
    if (c->globalOrder) {
        vkr_flush_barriers(c);
        vkr_order_writes(c->cb, true);
    } else {
        /* barriers for the targets only ("Hazards"), in one
         * call with the deferred image barriers */
        VkrHz h;
        h.imgCount = h.bufCount = 0;
        h.src = h.dst = 0;
        h.memDst = h.memSrc = 0;
        h.force = false;
        vkr_hz_take_pending(&h, c);
        for (uint32_t i = 0; i < pass->colorCount; i++) {
            /* a readable attachment is also read by the fragment shaders */
            const bool rd = pass->color[i].readable;
            vkr_hz_image(&h, c, colorTex[i], d.color[i].layout,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             (rd ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0u),
                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             (rd ? VK_ACCESS_INPUT_ATTACHMENT_READ_BIT : 0u),
                         ~0u);
        }
        /* a read-only depth attachment is only read when it is loaded and
         * not stored; anything else writes it (a DONT_CARE store too: it is
         * a write access in Vulkan's synchronisation) */
        bool depthWrites = false;
        if (depthTex) {
            depthWrites = d.depth.store != VK_ATTACHMENT_STORE_OP_NONE ||
                          d.depth.load != VK_ATTACHMENT_LOAD_OP_LOAD ||
                          (d.hasStencil && (d.depth.stencilStore != VK_ATTACHMENT_STORE_OP_NONE ||
                                            d.depth.stencilLoad != VK_ATTACHMENT_LOAD_OP_LOAD));
            /* a readable depth is also read by the fragment shaders */
            const bool rdz = pass->depth.readable;
            vkr_hz_image(&h, c, depthTex, d.depth.layout,
                         VKR_DEPTH_STAGES | (rdz ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0u),
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                             (depthWrites ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0) |
                             (rdz ? VK_ACCESS_INPUT_ATTACHMENT_READ_BIT : 0u),
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
    /* the subpass read's shape (vk_internal.h vkr_subpass_class): every
     * pass of its class, its colour one of its textures (in GENERAL) */
    if (g_vkr.legacyPasses && d.colorCount == 1u &&
        vkr_subpass_class(1u, d.color[0].format, d.color[0].samples, d.hasDepth)) {
        if (colorTex[0]->subpassRead && d.color[0].layout == VK_IMAGE_LAYOUT_GENERAL) {
            d.subpassRead = true;
        } else {
            /* not reached by construction (a swapchain image drawn with a
             * depth target): the class's pipelines would not match */
            g_vkr.subpassMisfit++;
            if (g_vkr.subpassMisfit <= 8u || g_vkr.subpassMisfit % 100u == 0u) {
                VKR_LOG("render pass: the subpass read's class (one RGBA8 colour target and a "
                        "depth target) on a colour %s (%ux%u, layout %d): begun without its "
                        "input attachment; the class's pipelines do not match it (%u so far)",
                        colorTex[0]->swapchain ? "swapchain image" : "texture made before it",
                        colorTex[0]->width, colorTex[0]->height, (int)d.color[0].layout,
                        g_vkr.subpassMisfit);
            }
        }
    }
    if (!vkr_pass_begin(c, &d)) {
        return;
    }
    if (d.subpassRead) {
        g_vkr.subpassPasses++;
    }
    c->passSubpassRead = d.subpassRead;
    c->inPass = true;
    c->areaX = areaX;
    c->areaY = areaY;
    c->areaW = areaW;
    c->areaH = areaH;
    c->storageDrawn = c->drawnInPass = false;
    g_vkr.stats.renderPasses++;

    /* defaults: the viewport the pass's whole size (an area does not move
     * the draws' coordinates), the scissor its render area, recorded
     * whatever the list held before (the bind cache forgets both at a
     * pass's start) */
    c->known &= ~(VKR_KNOW_VIEWPORT | VKR_KNOW_SCISSOR);
    RhiViewport vp = {0.0f, 0.0f, (float)passW, (float)passH, 0.0f, 1.0f};
    RhiRect sc = {(int32_t)areaX, (int32_t)areaY, areaW, areaH};
    rhi_cmd_set_viewport(cl, &vp);
    rhi_cmd_set_scissor(cl, &sc);
}

void rhi_cmd_end_render_pass(RhiCommandList cl)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->inPass) {
        return;
    }
    vkr_occ_close(c); /* a query does not outlive its pass */
    vkr_flush_barriers(c);
    vkr_pass_end(c);
    c->inPass = false;
    c->passSubpassRead = false;
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
    if (rhi_bind_cache_enabled() && (c->known & VKR_KNOW_VIEWPORT) &&
        memcmp(&vp, &c->vp, sizeof(vp)) == 0) {
        g_vkr.stats.stateSetsSkipped++;
        return;
    }
    vkCmdSetViewport(c->cb, 0, 1, &vp);
    c->vp = vp;
    c->known |= VKR_KNOW_VIEWPORT;
}

void rhi_cmd_set_scissor(RhiCommandList cl, const RhiRect *r)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !r) {
        return;
    }
    vkr_flush_barriers(c);
    VkRect2D sc = {{r->x, r->y}, {r->w, r->h}};
    if (c->inPass) {
        /* within the render area (vkr_pass_area_fits, RhiRenderPassDesc.area):
         * Vulkan leaves rendering outside it undefined, so the scissor is
         * the two rectangles' intersection (an empty one keeps its offset,
         * extent 0) */
        const int64_t ax0 = c->areaX, ay0 = c->areaY;
        const int64_t ax1 = ax0 + c->areaW, ay1 = ay0 + c->areaH;
        int64_t x0 = r->x > 0 ? r->x : 0, y0 = r->y > 0 ? r->y : 0;
        int64_t x1 = (int64_t)r->x + r->w, y1 = (int64_t)r->y + r->h;
        x0 = x0 > ax0 ? x0 : ax0;
        y0 = y0 > ay0 ? y0 : ay0;
        x1 = x1 < ax1 ? x1 : ax1;
        y1 = y1 < ay1 ? y1 : ay1;
        sc.offset.x = (int32_t)x0;
        sc.offset.y = (int32_t)y0;
        sc.extent.width = x1 > x0 ? (uint32_t)(x1 - x0) : 0u;
        sc.extent.height = y1 > y0 ? (uint32_t)(y1 - y0) : 0u;
    }
    /* compared as recorded (cut to the pass's area) */
    if (rhi_bind_cache_enabled() && (c->known & VKR_KNOW_SCISSOR) &&
        memcmp(&sc, &c->sc, sizeof(sc)) == 0) {
        g_vkr.stats.stateSetsSkipped++;
        return;
    }
    vkCmdSetScissor(c->cb, 0, 1, &sc);
    c->sc = sc;
    c->known |= VKR_KNOW_SCISSOR;
}

/* The groups a change from pipeline `from` (NULL: none known, or a new
 * pipeline in the bound one's pool slot) to `to` must bind again: every
 * group from the first whose set layout differs (or that `from` does not
 * have) on.  Vulkan keeps a bound descriptor set valid for a pipeline whose
 * layout is compatible for that set (the same set layouts up to it, and the
 * same push constants: none here), so the groups below stay bound; the
 * pipelines' VkPipelineLayout handles differ (one each), so comparing them
 * would bind every group again at every change. */
static uint32_t vkr_groups_disturbed(const VkrPipeline *from, const VkrPipeline *to)
{
    if (!from) {
        return 0xFFu;
    }
    uint32_t g = 0;
    while (g < to->layoutCount && g < from->layoutCount &&
           from->setLayoutIds[g] == to->setLayoutIds[g]) {
        g++;
    }
    return (0xFFu << g) & 0xFFu;
}

void rhi_cmd_set_pipeline(RhiCommandList cl, RhiPipeline p)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrPipeline *pp = vkr_pool_get(&g_vkr.pipelines, p.id);
    if (!c || !pp) {
        return;
    }
    /* by id: a pipeline destroyed and another made in its pool slot has the
     * same pointer but a new id (the slot's generation) */
    const bool cache = rhi_bind_cache_enabled();
    if (!cache || c->pipelineId != p.id) {
        /* the cache off (rhi_bind_cache.h): every group bound again at
         * every pipeline bind, whatever the layouts share */
        /* descriptor sets are bound per bind point in Vulkan and the list's
         * cache (c->groups) is not: a change between a graphics and a
         * compute pipeline (the VU record's dispatches) binds every group
         * again, else the first draw after the record would find the
         * graphics bind point's sets as they were before it */
        const bool otherPoint = c->pipeline && c->pipeline->bindPoint != pp->bindPoint;
        c->groupDirty |= cache && !otherPoint
                             ? vkr_groups_disturbed(c->pipeline == pp ? NULL : c->pipeline, pp)
                             : 0xFFu;
        c->pipeline = pp;
        c->pipelineId = p.id;
        vkr_flush_barriers(c);
        vkCmdBindPipeline(c->cb, pp->bindPoint, pp->pipeline);
        g_vkr.stats.pipelineBinds++;
        /* a group the new pipeline has static: its own values replaced the
         * ones set before, which a later dynamic pipeline must set again */
        c->dynValid &= pp->dynamic;
    } else {
        g_vkr.stats.pipelineBindsSkipped++;
    }
    /* the next draw's dynamic values: the pipeline's own until a set */
    c->dynSet = 0;
    c->dynFresh = 0;
    c->depthWant = pp->depthDefault;
    c->blendWant = pp->blendDefault;
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
    if (!rhi_bind_cache_enabled() || c->groups[group] != s || c->dynCount[group] != dyn ||
        memcmp(c->offsets[group], off, dyn * sizeof(off[0])) != 0) {
        c->groups[group] = s;
        c->dynCount[group] = dyn;
        memcpy(c->offsets[group], off, sizeof(off));
        c->groupDirty |= 1u << group;
    } else {
        g_vkr.stats.bindGroupBindsSkipped++;
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
            vkCmdBindDescriptorSets(c->cb, c->pipeline->bindPoint, c->pipeline->layout, g, 1,
                                    &c->groups[g], c->dynCount[g], c->offsets[g]);
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
    const uint32_t bit = binding < RHI_MAX_VERTEX_ATTRS ? 1u << binding : 0u;
    if (rhi_bind_cache_enabled() && bit && (c->vbKnown & bit) && c->vbId[binding] == b.id &&
        c->vbOff[binding] == offset) {
        g_vkr.stats.stateSetsSkipped++;
        return;
    }
    vkCmdBindVertexBuffers(c->cb, binding, 1, &buf->buffer, &off);
    if (bit) {
        c->vbId[binding] = b.id;
        c->vbOff[binding] = offset;
        c->vbKnown |= bit;
    }
}

void rhi_cmd_set_index_buffer(RhiCommandList cl, RhiBuffer b, uint64_t offset, bool u32)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrBuffer *buf = vkr_get_buffer(b);
    if (!c || !buf) {
        return;
    }
    vkr_flush_barriers(c);
    if (rhi_bind_cache_enabled() && (c->known & VKR_KNOW_INDEX) && c->ibId == b.id &&
        c->ibOff == offset && c->ib32 == u32) {
        g_vkr.stats.stateSetsSkipped++;
        return;
    }
    vkCmdBindIndexBuffer(c->cb, buf->buffer, offset,
                         u32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    c->ibBytes = buf->size > offset ? buf->size - offset : 0;
    c->ibId = b.id;
    c->ibOff = offset;
    c->ib32 = u32;
    c->known |= VKR_KNOW_INDEX;
}

void rhi_cmd_set_stencil_ref(RhiCommandList cl, uint8_t ref)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (c) {
        vkr_flush_barriers(c);
        if (rhi_bind_cache_enabled() && (c->known & VKR_KNOW_STENCIL) && c->stencilRef == ref) {
            g_vkr.stats.stateSetsSkipped++;
            return;
        }
        vkCmdSetStencilReference(c->cb, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
        c->stencilRef = ref;
        c->known |= VKR_KNOW_STENCIL;
    }
}

void rhi_cmd_set_blend_constant(RhiCommandList cl, const float rgba[4])
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (c && rgba) {
        vkr_flush_barriers(c);
        if (rhi_bind_cache_enabled() && (c->known & VKR_KNOW_BLEND_CONSTANT) &&
            memcmp(c->blendConst, rgba, sizeof(c->blendConst)) == 0) {
            g_vkr.stats.stateSetsSkipped++;
            return;
        }
        vkCmdSetBlendConstants(c->cb, rgba);
        memcpy(c->blendConst, rgba, sizeof(c->blendConst));
        c->known |= VKR_KNOW_BLEND_CONSTANT;
    }
}

/* The dynamic depth and blend state (rhi.h RhiPipelineDesc.dynamic;
 * docs/port/RHI.md "Dynamic state").  The sets keep what the next draw
 * needs (only for a bound pipeline with that group dynamic; for any other
 * they do nothing, its own values are in force); the draw emits what
 * differs from the values the command buffer holds, every command of a
 * group after a bind of a pipeline with the group static (that bind
 * replaced them), nothing that is already in force.  So a draw gets exactly
 * the values a pipeline with every state static would have given it, and
 * RhiStats.dynamicSets counts the commands recorded. */
void rhi_cmd_set_depth_state(RhiCommandList cl, bool test, bool write, RhiCompare cmp)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->pipeline || !(c->pipeline->dynamic & RHI_DYN_DEPTH)) {
        return;
    }
    memset(&c->depthWant, 0, sizeof(c->depthWant));
    c->depthWant.test = test ? VK_TRUE : VK_FALSE;
    c->depthWant.write = test && write ? VK_TRUE : VK_FALSE;
    c->depthWant.compare = g_vkr.depthAlways ? VK_COMPARE_OP_ALWAYS : vkr_compareMap[cmp].vk;
    c->dynSet |= RHI_DYN_DEPTH;
    c->dynFresh |= RHI_DYN_DEPTH;
}

void rhi_cmd_set_blend_state(RhiCommandList cl, const RhiBlendState *blend)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !blend || !c->pipeline || !(c->pipeline->dynamic & RHI_DYN_BLEND)) {
        return;
    }
    const VkPipelineColorBlendAttachmentState a = vkr_blend_state(blend, c->pipeline->intTarget);
    vkr_blend_dyn(&a, &c->blendWant);
    c->dynSet |= RHI_DYN_BLEND;
    c->dynFresh |= RHI_DYN_BLEND;
}

/* whether a blend equation reads the shader's second output (a SRC1
 * factor: dual-source blending) */
static bool vkr_dual_factor(VkBlendFactor f)
{
    return f == VK_BLEND_FACTOR_SRC1_COLOR || f == VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR ||
           f == VK_BLEND_FACTOR_SRC1_ALPHA || f == VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
}

static bool vkr_eq_dual(const VkColorBlendEquationEXT *eq)
{
    return vkr_dual_factor(eq->srcColorBlendFactor) || vkr_dual_factor(eq->dstColorBlendFactor) ||
           vkr_dual_factor(eq->srcAlphaBlendFactor) || vkr_dual_factor(eq->dstAlphaBlendFactor);
}

/* before a draw: the bound pipeline's dynamic groups as the draw needs
 * them (rhi_cmd_set_depth_state's comment) */
static void vkr_flush_dynamic(VkrCmdList *c)
{
    const uint8_t dyn = c->pipeline->dynamic;
    if (!dyn) {
        return;
    }
    /* the cache off: every command of a dynamic group at every draw */
    const bool cache = rhi_bind_cache_enabled();
    if ((dyn & ~c->dynSet) != 0u) {
        /* a bind site that does not set the state (rd binds through
         * rd__set_pipeline, which always does): the pipeline's own values,
         * which are the key's normalised ones, not the draw's (once a run) */
        static bool logged;
        if (!logged) {
            logged = true;
            VKR_LOG("dynamic state: a draw with its pipeline's %s%s%s left unset since the bind "
                    "(the pipeline's own values used; reported once)",
                    (dyn & ~c->dynSet & RHI_DYN_DEPTH) ? "depth" : "",
                    (dyn & ~c->dynSet) == (RHI_DYN_DEPTH | RHI_DYN_BLEND) ? " and " : "",
                    (dyn & ~c->dynSet & RHI_DYN_BLEND) ? "blend" : "");
        }
    }
    if (dyn & RHI_DYN_DEPTH) {
        const bool all = !cache || !(c->dynValid & RHI_DYN_DEPTH);
        const VkrDepthDyn *w = &c->depthWant, *h = &c->depthHave;
        uint32_t sent = 0;
        if (all || w->test != h->test) {
            g_vkr.cmdSetDepthTestEnable(c->cb, w->test);
            sent++;
        }
        if (all || w->write != h->write) {
            g_vkr.cmdSetDepthWriteEnable(c->cb, w->write);
            sent++;
        }
        if (all || w->compare != h->compare) {
            g_vkr.cmdSetDepthCompareOp(c->cb, w->compare);
            sent++;
        }
        g_vkr.stats.dynamicSets += sent;
        if (c->dynFresh & RHI_DYN_DEPTH) {
            g_vkr.stats.stateSetsSkipped += 3u - sent; /* the set's values in force */
        }
        c->depthHave = *w;
        c->dynValid |= RHI_DYN_DEPTH;
    }
    if (dyn & RHI_DYN_BLEND) {
        const bool all = !cache || !(c->dynValid & RHI_DYN_BLEND);
        const VkrBlendDyn *w = &c->blendWant;
        VkrBlendDyn *h = &c->blendHave;
        const uint64_t before = g_vkr.stats.dynamicSets;
        if (all || w->enable != h->enable) {
            g_vkr.cmdSetColorBlendEnable(c->cb, 0, 1, &w->enable);
            g_vkr.stats.dynamicSets++;
            h->enable = w->enable;
        }
        /* the equation is always the draw's own, blending on or off (a
         * draw with blending off wants the zeroed one, vk_pipeline.c
         * vkr_blend_state): RADV on Mesa 25.x decides dual-source blending
         * from the factors alone, whatever the enable (radv_cmd_buffer.c
         * radv_emit_color_blend and the PS epilog state; Mesa 26.2 checks
         * the enable first, radv_is_dual_src_enabled), and on RDNA3 the
         * pixel shader epilog then swaps the two exports between
         * neighbouring pixels, so a one-output shader after a SRC1 blend
         * left every other column undefined (issues 146 and 154).  Sent on
         * every driver; blendEqOffSets counts the commands for draws with
         * blending off, dualLeftovers those that replaced a SRC1 equation */
        if (all || memcmp(&w->eq, &h->eq, sizeof(w->eq)) != 0) {
            if (!w->enable) {
                g_vkr.stats.blendEqOffSets++;
                if (!all && vkr_eq_dual(&h->eq)) {
                    g_vkr.stats.dualLeftovers++;
                }
            }
            g_vkr.cmdSetColorBlendEquation(c->cb, 0, 1, &w->eq);
            g_vkr.stats.dynamicSets++;
            h->eq = w->eq;
        }
        if (all || w->mask != h->mask) {
            g_vkr.cmdSetColorWriteMask(c->cb, 0, 1, &w->mask);
            g_vkr.stats.dynamicSets++;
            h->mask = w->mask;
        }
        if (c->dynFresh & RHI_DYN_BLEND) {
            g_vkr.stats.stateSetsSkipped += 3u - (g_vkr.stats.dynamicSets - before);
        }
        c->dynValid |= RHI_DYN_BLEND;
    }
    c->dynFresh = 0;
}

/* Inside the open pass (dynamic rendering local read, or a pass of the
 * subpass read, whose self-dependency holds every mask here: vk_renderpass.c):
 * one by-region memory barrier holding the masks asked for, counted in
 * stats.barriers.
 *   COLOR  colour attachment writes -> the fragment shaders' input
 *          attachment reads (DATE's setup draw reads the alpha);
 *   DEPTH  the depth tests' writes -> the fragment shaders' input
 *          attachment reads (the fog reads the depth);
 *   BACK   those reads -> the next draws' depth and colour writes (the
 *          write after the read of the same pixel).
 * Nothing outside a pass, or without local read.  Under the subpass read
 * a pass outside its class (colour only, or another format) has no
 * self-dependency, so a barrier there would break
 * VUID-vkCmdPipelineBarrier-None-07889: skipped, counted and logged (the
 * renderer reads only in passes with a depth target there, RhiLimits
 * .localReadSubpass). */
void rhi_cmd_local_read_barrier(RhiCommandList cl, uint32_t mask)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->inPass || !VKR_PASS_READ || mask == 0u) {
        return;
    }
    if (!g_vkr.localRead && !c->passSubpassRead) {
        g_vkr.subpassSkipped++;
        if (g_vkr.subpassSkipped <= 8u || g_vkr.subpassSkipped % 100u == 0u) {
            VKR_LOG("render pass: a local-read barrier in a pass without the subpass read (no "
                    "depth target, or not one RGBA8 colour): skipped, and a read after it is "
                    "not ordered (%u so far)",
                    g_vkr.subpassSkipped);
        }
        return;
    }
    VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    VkPipelineStageFlags src = 0, dst = 0;
    if (mask & RHI_LOCAL_READ_COLOR) {
        src |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        mb.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dst |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        mb.dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    }
    if (mask & RHI_LOCAL_READ_DEPTH) {
        src |= VKR_DEPTH_STAGES;
        mb.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dst |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        mb.dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    }
    if (mask & RHI_LOCAL_READ_BACK) {
        src |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        mb.srcAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        dst |= VKR_DEPTH_STAGES | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        mb.dstAccessMask |=
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    vkr_flush_barriers(c);
    vkCmdPipelineBarrier(c->cb, src, dst, VK_DEPENDENCY_BY_REGION_BIT, 1, &mb, 0, NULL, 0, NULL);
    g_vkr.stats.barriers++;
}

/* A destination-read draw (rhi.h RhiDstRead) needs no barrier before it:
 * the interlock orders a storage draw's critical sections after the
 * pass's earlier ones (primitive order, the image Coherent), and the
 * rasterization-order attachment access orders an attachment read after
 * the pass's earlier colour writes.  The validation layer's
 * synchronisation checks model neither, so with g_vkr.storageBarriers
 * (under the layer, or ICO_VK_STORAGE_BARRIERS=1) a by-region barrier
 * from the pass's earlier writes goes before it (inside the render pass:
 * dynamic rendering local read).  drawnInPass / storageDrawn: a draw, a
 * storage draw, recorded in the open pass. */
static void vkr_dst_order(VkrCmdList *c)
{
    const uint8_t dst = c->pipeline ? c->pipeline->dstRead : (uint8_t)RHI_DST_READ_NONE;
    if (c->inPass && g_vkr.storageBarriers) {
        VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        VkPipelineStageFlags src = 0, dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        if (dst == RHI_DST_READ_STORAGE && c->storageDrawn) {
            src = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        } else if (dst == RHI_DST_READ_ATTACHMENT && c->drawnInPass) {
            src = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        }
        if (src) {
            vkCmdPipelineBarrier(c->cb, src, dstStage, VK_DEPENDENCY_BY_REGION_BIT, 1, &mb, 0, NULL,
                                 0, NULL);
            g_vkr.stats.orderBarriers++; /* not stats.barriers: validation only */
        }
    }
    c->drawnInPass = true;
    c->storageDrawn = c->storageDrawn || dst == RHI_DST_READ_STORAGE;
}

void rhi_cmd_draw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                  uint32_t instanceCount)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    /* no open pass (the legacy pass begin failed, vk_renderpass.c): the draws
     * until the pass end are skipped; so is a draw with a compute pipeline
     * bound (the VU record's, rhi_cmd_dispatch's) */
    if (!c || !c->pipeline || !c->inPass ||
        c->pipeline->bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) {
        return;
    }
    vkr_flush_barriers(c);
    vkr_dst_order(c);
    vkr_flush_bind_groups(c);
    vkr_flush_dynamic(c);
    vkCmdDraw(c->cb, vertexCount, instanceCount ? instanceCount : 1u, firstVertex, 0);
    g_vkr.stats.draws++;
}

void rhi_cmd_draw_indexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                          int32_t vertexOffset, uint32_t instanceCount)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    /* no open pass (the legacy pass begin failed, vk_renderpass.c): the draws
     * until the pass end are skipped; so is a draw with a compute pipeline
     * bound (the VU record's, rhi_cmd_dispatch's) */
    if (!c || !c->pipeline || !c->inPass ||
        c->pipeline->bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) {
        return;
    }
    vkr_flush_barriers(c);
    vkr_dst_order(c);
    vkr_flush_bind_groups(c);
    vkr_flush_dynamic(c);
    vkr_range_index(c, firstIndex, indexCount);
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
    vkr_range_buffer(c, "a buffer copy's source", s, srcOffset, size);
    vkr_range_buffer(c, "a buffer copy's destination", d, dstOffset, size);
    vkr_flush_barriers(c);
    /* rhi.h: the copy waits for every earlier read of the destination
     * (draws of earlier frames reading a range rewritten now) and write (an
     * earlier copy or store), at the stages that can have made them
     * ("Buffer barriers" above):
     *   - the shaders' and vertex input's reads: vkr_buf_earlier_stages
     *     (the mesh arena: vertex input, vertex and compute shaders; never
     *     the fragment shader, whose modules declare no storage buffer);
     *   - the stores: their bracket's end has the transfer stage and its
     *     write in its second scope (vkr_store_close);
     *   - an earlier buffer copy's read or write of either buffer: that
     *     copy's own second barrier (below) holds the transfer stage;
     *   - a copy between a texture and either buffer in this list
     *     (vkr_hz_copy: its hazard record, VkrBuffer.hzXfer*): the transfer
     *     stage here, with its write;
     *   - those of earlier lists: each list's closing barrier
     *     (rhi_end_commands).
     * So the transfer stage is in this first scope only for a copy between
     * a texture and either buffer.  That does not keep this copy from
     * waiting for fragment work recorded before it: an execution
     * dependency covers every command in its scopes, whatever resource, so
     * an earlier barrier whose first scope holds the fragment shader and
     * whose second the transfer stage (a texture upload over a sampled
     * texture: SHADER_READ -> COPY_DST) makes every later copy wait for the
     * fragment work before it, this one included, and this copy's second
     * barrier passes that wait on to the vertex and compute work that
     * reads the destination.  The renderer therefore records the mesh
     * uploads and the VU record before its texture uploads
     * (port/render/rd_upload_late.h).  What remains: the previous list's
     * closing barrier orders its copies before this list's transfers, so a
     * copy out of a render target at the end of the picture before (its
     * own barrier waits for colour output) still holds this copy, and the
     * record after it, behind that pass's fragment work.  A buffer nothing
     * can have used gets the top of the pipe (no wait); the call stays one
     * either way (rd_perf's barrier counts). */
    const bool dPending = d->hzEpoch == c->epoch && (d->hzXferWrite || d->hzXferRead);
    const bool sPending = s->hzEpoch == c->epoch && s->hzXferWrite;
    VkPipelineStageFlags preSrc =
        vkr_buf_earlier_stages(d) & ~(VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT;
    /* on the global barrier path vkr_hz_copy keeps no hazard record (its
       global barrier goes before a texture copy, none after it): the
       transfer stage always, so an earlier copy between a texture and
       either buffer stays ordered before this one; and the wide scopes'
       v0.11.2 first scope always holds it (vkr_buf_wide_stages) */
    if (dPending || sPending || c->globalOrder || g_vkr.tile.wide) {
        preSrc |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    VkMemoryBarrier pre = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask =
            (preSrc & VK_PIPELINE_STAGE_TRANSFER_BIT) ? VK_ACCESS_TRANSFER_WRITE_BIT : 0u,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    if (preSrc == 0) {
        preSrc = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    vkCmdPipelineBarrier(c->cb, preSrc, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &pre, 0, NULL, 0,
                         NULL);
    vkr_hold_note(VKR_HOLD_COPY, preSrc, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, false);
    g_vkr.stats.barriers++;
    if ((dPending || sPending) && !g_vkr.tile.wide) {
        /* the transfer stage above covered the list's earlier copies of both
           (the wide scopes keep the hazard record, as v0.11.2 did: a later
           texture copy of either buffer in this list still makes its own
           buffer barrier) */
        d->hzXferWrite = d->hzXferRead = false;
        s->hzXferWrite = false;
    }
    vkCmdCopyBuffer(c->cb, s->buffer, d->buffer, 1, &r);
    g_vkr.stats.copies++;
    s->copied = d->copied = true;
    /* rhi.h: buffer copies are visible to every later read, and ordered
     * before every later copy (the transfer stage here: a later copy's own
     * first scope need not hold it), at the stages the destination's usage
     * lets reach it (vkr_buf_later_stages) */
    VkAccessFlags laterAccess = 0;
    const VkPipelineStageFlags later = vkr_buf_later_stages(d, &laterAccess);
    VkMemoryBarrier mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = laterAccess,
    };
    vkCmdPipelineBarrier(c->cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         later | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
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
    vkr_range_rows(c, "a buffer-to-texture copy's source rows", s, srcOffset, rowPitch, t, region);
    vkr_range_image(c, "a buffer-to-texture copy's region", t, mip, region);
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
    if (s->samples > 1u || d->samples > 1u) {
        VKR_LOG("rhi_cmd_copy_texture: a multisampled texture is not copied (resolve it)");
        return;
    }
    VkImageCopy r = {
        .srcSubresource = {s->aspects, 0, 0, 1},
        .srcOffset = {srcRegion.x, srcRegion.y, 0},
        .dstSubresource = {d->aspects, 0, 0, 1},
        .dstOffset = {dstX, dstY, 0},
        .extent = {srcRegion.w, srcRegion.h, 1},
    };
    vkr_range_image(c, "a texture copy's source region", s, 0, srcRegion);
    vkr_range_image(c, "a texture copy's destination region", d, 0,
                    (RhiRect){dstX, dstY, srcRegion.w, srcRegion.h});
    vkr_hz_copy(c, s, NULL, d, 0, NULL);
    vkCmdCopyImage(c->cb, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    g_vkr.stats.copies++;
}

void rhi_cmd_resolve_color(RhiCommandList cl, RhiTexture src, RhiTexture dst)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrTexture *s = vkr_get_texture(src);
    VkrTexture *d = vkr_get_texture(dst);
    if (!c || !s || !d) {
        return;
    }
    if (s->samples <= 1u || d->samples > 1u || s->width != d->width || s->height != d->height ||
        s->format != d->format || !(s->aspects & VK_IMAGE_ASPECT_COLOR_BIT)) {
        VKR_LOG("rhi_cmd_resolve_color: needs a multisampled colour texture and a "
                "single-sampled one of its size and format");
        return;
    }
    VkImageResolve r = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .srcOffset = {0, 0, 0},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .dstOffset = {0, 0, 0},
        .extent = {s->width, s->height, 1},
    };
    vkr_hz_copy(c, s, NULL, d, 0, NULL);
    vkCmdResolveImage(c->cb, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image,
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
    vkr_range_image(c, "a texture-to-buffer copy's region", t, 0, region);
    vkr_range_rows(c, "a texture-to-buffer copy's destination rows", d, dstOffset, rowPitch, t,
                   region);
    /* the earlier reads' stages, before vkr_hz_copy marks the buffer copied
       (the global path's own barrier below) */
    const VkPipelineStageFlags earlier = vkr_buf_earlier_stages(d) | VK_PIPELINE_STAGE_TRANSFER_BIT;
    vkr_hz_copy(c, t, NULL, NULL, 0, d);
    VkAccessFlags reads = 0;
    const VkPipelineStageFlags readStages = vkr_buf_later_stages(d, &reads);
    if (d->shaderRead && c->globalOrder) {
        /* rhi.h: into a buffer shaders read, after their earlier reads, at
         * the stages that can have made them (the hazard path folds this
         * into vkr_hz_copy's barrier) */
        VkMemoryBarrier pre = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        };
        vkCmdPipelineBarrier(c->cb, earlier, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &pre, 0, NULL, 0,
                             NULL);
        vkr_hold_note(VKR_HOLD_GLOBAL, earlier, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, false);
        g_vkr.stats.barriers++;
    }
    vkCmdCopyImageToBuffer(c->cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->buffer, 1, &r);
    g_vkr.stats.copies++;
    if (d->shaderRead) {
        /* ... and visible to every later read (the VU vertex pre-pass's
         * record, read by the triangle stage's vertex shader), at the
         * stages its usage lets reach it (vkr_buf_later_stages): a memory
         * barrier held back with the image barriers, so the copies of a
         * batch and the next pass share their barriers */
        c->pendMemDst |= reads;
        c->pendSrc |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        c->pendDst |= readStages;
        if (c->globalOrder) {
            vkr_flush_barriers(c);
        }
    }
}

/* ------------------------------------------------------------ vertex stores
 * rhi.h rhi_cmd_buffer_store_begin / _end: the passes whose vertex shaders
 * store to a buffer (the VU record, port/render/rd_replay_vu.c
 * vuRecPassBegin), outside render passes.  Each is a memory barrier held
 * back with the list's deferred barriers (VkrCmdList.pendMemDst, with the
 * shader writes in pendMemSrc), so it goes out in the next pass's or
 * copy's one vkCmdPipelineBarrier (vkr_hz_take_pending): _end's of pass V
 * and _begin's of pass T are one call.  _begin: every earlier use of the
 * buffer before the shader's reads and stores (write after read, write
 * after write, and a store pass reading the last pass's stores); _end: the
 * stores before every later read, as a copy into the buffer is.  A
 * pipeline barrier's first scope holds every command submitted to the
 * queue before it, so the lists of the other frame slot and of earlier
 * replays are ordered too.
 *
 * The scopes, proven against the stages that can touch the buffer
 * ("Buffer barriers" above; the VU record, port/render/rd_replay_vu.c
 * recEnsure: RHI_BUF_STORAGE_READ | _STORAGE_WRITE | _COPY_DST, a storage
 * buffer that is never a vertex, index or uniform buffer):
 *   _begin, first scope: the stages of its earlier reads and writes,
 *     vkr_buf_earlier_stages: the vertex shader (the triangle stage's
 *     vu_rec_buf at t4, the velocity pass, the store passes' u6), the
 *     compute shader where the compute builds exist (their u6), the
 *     transfer stage once a copy has touched it (the render targets'
 *     record copies into it); its accesses: the shader writes (the stores:
 *     the vertex or compute stage is in the scope) and, with the transfer
 *     stage, the copies' writes (vkr_flush_barriers_to).  Not the fragment
 *     shader: no fragment module of the run declares a storage buffer
 *     until VkrState.fragmentStorage says one does (then it is in), and
 *     no other access reaches a storage buffer.  So the first dispatch of
 *     a picture waits for the picture before's vertex and compute work,
 *     not for its pixels (issue 179);
 *   _begin, second scope: the writing stage ws with its reads and writes;
 *   _end, first scope: ws and its writes;
 *   _end, second scope: the stages the usage lets read it
 *     (vkr_buf_later_stages: the vertex, fragment and compute shaders'
 *     storage reads, the transfer stage's reads and writes) with ws's
 *     writes (the next pass's stores).  The fragment stage there holds
 *     only later fragment work, behind the stores. */

/* the list and the buffer, outside a pass, where the stores of the kind
   are offered (compute: rhi_cmd_buffer_compute_begin / _end) */
static VkrCmdList *vkr_store_list_of(RhiCommandList cl, RhiBuffer buf, const char *what,
                                     bool compute, VkrBuffer **out)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    VkrBuffer *b = vkr_get_buffer(buf);
    if (!c || !b || !(compute ? g_vkr.computeStores : g_vkr.vertexStores)) {
        return NULL;
    }
    if (c->inPass) {
        VKR_LOG("%s: inside a render pass (the bracket goes around the passes)", what);
        return NULL;
    }
    *out = b;
    return c;
}

/* the bracket's two halves for the writing stage ws (the vertex shader,
 * or the compute shader: rhi_cmd_buffer_compute_begin / _end, the same
 * barriers with the compute stage writing) on buffer b */
static void vkr_store_open(VkrCmdList *c, const VkrBuffer *b, VkPipelineStageFlags ws)
{
    c->pendMemSrc |= VK_ACCESS_SHADER_WRITE_BIT;
    c->pendMemDst |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    c->pendSrc |= vkr_buf_earlier_stages(b) | ws;
    c->pendDst |= ws;
    if (c->globalOrder) {
        vkr_flush_barriers(c);
    }
}

static void vkr_store_close(VkrCmdList *c, const VkrBuffer *b, VkPipelineStageFlags ws)
{
    VkAccessFlags reads = 0;
    const VkPipelineStageFlags later = vkr_buf_later_stages(b, &reads);
    c->pendMemSrc |= VK_ACCESS_SHADER_WRITE_BIT;
    c->pendMemDst |= reads | VK_ACCESS_SHADER_WRITE_BIT;
    c->pendSrc |= ws;
    c->pendDst |= later | ws;
    if (c->globalOrder) {
        vkr_flush_barriers(c);
    }
}

bool rhi_cmd_buffer_store_begin(RhiCommandList cl, RhiBuffer buf)
{
    VkrBuffer *b = NULL;
    VkrCmdList *c = vkr_store_list_of(cl, buf, "rhi_cmd_buffer_store_begin", false, &b);
    if (!c) {
        return false;
    }
    vkr_store_open(c, b, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
    return true;
}

bool rhi_cmd_buffer_store_end(RhiCommandList cl, RhiBuffer buf)
{
    VkrBuffer *b = NULL;
    VkrCmdList *c = vkr_store_list_of(cl, buf, "rhi_cmd_buffer_store_end", false, &b);
    if (!c) {
        return false;
    }
    vkr_store_close(c, b, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
    return true;
}

/* ------------------------------------------------------------ compute stores
 * rhi.h rhi_cmd_buffer_compute_begin / _end and rhi_cmd_dispatch: the VU
 * record written by compute dispatches (port/render/rd_replay_vu.c
 * vuRecPassBegin), outside render passes, where RhiLimits.computeStores.
 * The bracket is the store bracket's with the compute shader as the writing
 * stage; held back the same way, so it goes out with the first dispatch's
 * (rhi_cmd_dispatch flushes the list's deferred barriers) or the next
 * pass's or copy's barrier. */
bool rhi_cmd_buffer_compute_begin(RhiCommandList cl, RhiBuffer buf)
{
    VkrBuffer *b = NULL;
    VkrCmdList *c = vkr_store_list_of(cl, buf, "rhi_cmd_buffer_compute_begin", true, &b);
    if (!c) {
        return false;
    }
    vkr_store_open(c, b, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    return true;
}

bool rhi_cmd_buffer_compute_end(RhiCommandList cl, RhiBuffer buf)
{
    VkrBuffer *b = NULL;
    VkrCmdList *c = vkr_store_list_of(cl, buf, "rhi_cmd_buffer_compute_end", true, &b);
    if (!c) {
        return false;
    }
    vkr_store_close(c, b, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    return true;
}

void rhi_cmd_dispatch(RhiCommandList cl, uint32_t gx, uint32_t gy, uint32_t gz)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !g_vkr.computeStores || gx == 0 || gy == 0 || gz == 0) {
        return;
    }
    if (c->inPass || !c->pipeline || c->pipeline->bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE) {
        static uint32_t s_refused;
        if (s_refused++ < 8u) {
            VKR_LOG("rhi_cmd_dispatch: %s: refused (%u)",
                    c->inPass ? "inside a render pass" : "no compute pipeline bound", s_refused);
        }
        return;
    }
    vkr_flush_barriers(c);
    vkr_flush_bind_groups(c);
    vkCmdDispatch(c->cb, gx, gy, gz);
    g_vkr.stats.dispatches++;
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

/* ------------------------------------------------------------- checkpoints */
void rhi_cmd_checkpoint(RhiCommandList cl, const void *marker)
{
    /* the crash report's breadcrumb names the next pass by it
     * (vk_renderpass.c vkr_pass_crumb), checkpoints on or off */
    g_vkr.lastMarker = (uint64_t)(uintptr_t)marker;
    if (!g_vkr.checkpoints) {
        return;
    }
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->recording) {
        return;
    }
    /* in order with what the list recorded before it */
    vkr_flush_barriers(c);
    g_vkr.cmdSetCheckpoint(c->cb, marker);
}

/* ---------------------------------------------------------------- readback */
bool rhi_readback_texture(RhiTexture h, RhiViewAspect aspect, void *dst, size_t dstSize,
                          uint32_t *outRowPitch)
{
    VkrTexture *t = vkr_get_texture(h);
    if (!t || !dst || t->samples > 1u) {
        return false; /* a multisampled texture is resolved first (rhi.h) */
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
        uint64_t value = 0;
        ok = vkr_sync_submit(&(VkrSubmitDesc){.cbs = &cb, .cbCount = 1}, &value);
        if (ok) {
            vkr_cur_frame()->waitValue = value;
            vkr_note_submit(vkr_cur_frame(), value, 1, false);
            vkr_wait_value(value);
            const void *mapped = rhi_map_buffer(rb); /* NULL when vkMapMemory failed */
            ok = mapped != NULL;
            if (ok) {
                memcpy(dst, mapped, size);
            }
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
    index &= ~(RHI_TIMESTAMP_KEEP_BARRIERS | RHI_TIMESTAMP_ISSUE_TIME); /* no busy clock here */
    if (!c || !f->queryPool || !f->tsReset || index >= RHI_MAX_TIMESTAMPS ||
        rhi_ts_has(f->tsWritten, index)) {
        return;
    }
    if (!keep) {
        vkr_flush_barriers(c);
    }
    vkCmdWriteTimestamp(c->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f->queryPool, index);
    rhi_ts_add(f->tsWritten, index);
    bool first = true;
    for (uint32_t w = 0; w < RHI_TIMESTAMP_WORDS; w++) {
        first = first && c->tsWritten[w] == 0u;
    }
    if (first) {
        __atomic_add_fetch(&g_vkr.frames[c->slot].tsListsStamped, 1u, __ATOMIC_RELAXED);
    }
    rhi_ts_add(c->tsWritten, index);
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

/* no busy clock (rhi.h): a command buffer goes to the queue whole at rhi_submit */
uint32_t rhi_read_timestamps_busy(uint64_t *ns, uint32_t max)
{
    (void)ns;
    (void)max;
    return 0;
}

/* ------------------------------------------------------- occlusion queries */
/* The pools at the first begin (rhi.h rhi_cmd_begin_occlusion) or at
 * rhi_occlusion_prepare: one per slot and the result array; the current
 * slot's pool is reset by a one-time submit (its first list began before
 * the pool existed), the others in their first list (rhi_begin_commands),
 * over range queries.  False when a pool or the array could not be made
 * (occlusion then stays off for the run). */
static bool vkr_occ_make(uint32_t range)
{
    if (g_vkr.occUsed || g_vkr.occBroken) {
        return g_vkr.occUsed;
    }
    g_vkr.occRange = range;
    g_vkr.occBroken = true; /* until every part is made */
    g_vkr.occResult = malloc(sizeof(uint64_t) * RHI_MAX_OCCLUSION);
    if (!g_vkr.occResult) {
        VKR_LOG("occlusion queries: off (no memory for the counts)");
        return false;
    }
    for (uint32_t i = 0; i < RHI_MAX_OCCLUSION; i++) {
        g_vkr.occResult[i] = RHI_OCCLUSION_NONE;
    }
    g_vkr.occCount = 0;
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        VkrFrame *f = &g_vkr.frames[i];
        VkQueryPoolCreateInfo qci = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_OCCLUSION,
            .queryCount = RHI_MAX_OCCLUSION,
        };
        if (!VKR_CHECK(vkCreateQueryPool(g_vkr.device, &qci, NULL, &f->occPool))) {
            f->occPool = VK_NULL_HANDLE;
            for (uint32_t j = 0; j < RHI_FRAMES_IN_FLIGHT; j++) {
                if (g_vkr.frames[j].occPool) {
                    vkDestroyQueryPool(g_vkr.device, g_vkr.frames[j].occPool, NULL);
                    g_vkr.frames[j].occPool = VK_NULL_HANDLE;
                }
            }
            free(g_vkr.occResult);
            g_vkr.occResult = NULL;
            VKR_LOG("occlusion queries: off (a query pool could not be made)");
            return false;
        }
        f->occReset = false;
        memset(f->occWritten, 0, sizeof(f->occWritten));
    }
    VkrFrame *f = vkr_cur_frame();
    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_vkr.oneShotPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb;
    if (VKR_CHECK(vkAllocateCommandBuffers(g_vkr.device, &ai, &cb))) {
        VkCommandBufferBeginInfo bi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(cb, &bi);
        vkCmdResetQueryPool(cb, f->occPool, 0, range);
        vkEndCommandBuffer(cb);
        /* submitted before the lists of this frame, so the reset comes
           first in the queue's order */
        uint64_t value = 0;
        if (vkr_sync_submit(&(VkrSubmitDesc){.cbs = &cb, .cbCount = 1}, &value)) {
            f->waitValue = value;
            vkr_note_submit(f, value, 1, false);
            vkr_wait_value(value);
            f->occReset = true;
            f->occResetN = range;
        }
        vkFreeCommandBuffers(g_vkr.device, g_vkr.oneShotPool, 1, &cb);
    }
    g_vkr.occUsed = true;
    g_vkr.occBroken = false;
    VKR_LOG("occlusion queries: on (%u a frame slot, %u of them reset a frame, %s)",
            (unsigned)RHI_MAX_OCCLUSION, (unsigned)range,
            g_vkr.occPrecise ? "precise sample counts"
                             : "not precise: a count is exact only as zero or not zero");
    return true;
}

bool rhi_occlusion_prepare(uint32_t count)
{
    if (!rhi_occlusion_supported() || g_vkr.deviceLost || count == 0) {
        return false;
    }
    count = count > RHI_MAX_OCCLUSION ? RHI_MAX_OCCLUSION : count;
    if (!g_vkr.occUsed && !g_vkr.occBroken) {
        /* the reset's submit, as at a first begin */
        rhi_call_guard_settle();
        return vkr_occ_make(count);
    }
    if (g_vkr.occUsed && count > g_vkr.occRange) {
        g_vkr.occRange = count; /* each slot's next first list */
    }
    return g_vkr.occUsed;
}

void rhi_cmd_begin_occlusion(RhiCommandList cl, uint32_t index)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->inPass || c->occOpen || index >= RHI_MAX_OCCLUSION || g_vkr.deviceLost) {
        return;
    }
    if (!g_vkr.occUsed && !g_vkr.occBroken) {
        /* the first use submits the pools' reset: the queue is the submit
         * thread's while it works, so the call guard's wait first, as an
         * entry point that submits makes it ("Frame slots" above) */
        rhi_call_guard_settle();
    }
    if (!vkr_occ_make(RHI_MAX_OCCLUSION)) {
        return;
    }
    VkrFrame *f = vkr_cur_frame();
    if (f->occPool && f->occReset && index >= f->occResetN) {
        /* past the range this slot reset (rhi_occlusion_prepare): dropped,
         * and the whole pool reset from each slot's next use */
        if (g_vkr.occRange < RHI_MAX_OCCLUSION) {
            VKR_LOG("occlusion queries: query %u is past the %u reset a frame: dropped; the "
                    "whole pool is reset from each slot's next use",
                    (unsigned)index, (unsigned)g_vkr.occRange);
        }
        g_vkr.occRange = RHI_MAX_OCCLUSION;
        return;
    }
    if (!f->occPool || !f->occReset || ((f->occWritten[index >> 5] >> (index & 31u)) & 1u)) {
        return;
    }
    vkCmdBeginQuery(c->cb, f->occPool, index, g_vkr.occPrecise ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
    f->occWritten[index >> 5] |= 1u << (index & 31u);
    c->occOpen = index + 1u;
}

/* the open query's end (rhi_cmd_end_occlusion, a pass end) */
static void vkr_occ_close(VkrCmdList *c)
{
    if (c->occOpen) {
        vkCmdEndQuery(c->cb, g_vkr.frames[c->slot].occPool, c->occOpen - 1u);
        c->occOpen = 0;
    }
}

void rhi_cmd_end_occlusion(RhiCommandList cl, uint32_t index)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (c && c->occOpen == index + 1u) {
        vkr_occ_close(c);
    }
}

uint32_t rhi_read_occlusion(uint64_t *samples, uint32_t max)
{
    if (!samples || !g_vkr.occResult) {
        return 0;
    }
    const uint32_t n = g_vkr.occCount < max ? g_vkr.occCount : max;
    memcpy(samples, g_vkr.occResult, (size_t)n * sizeof(uint64_t));
    return n;
}

/* ------------------------------------------------- external compute
 * rhi_native.h: the caller records compute work (FidelityFX) straight into
 * the native command buffer between rhi_cmd_external_begin and _end.  The
 * RHI keeps no state for it: begin orders everything recorded so far before
 * the external work with one memory barrier (the image layouts were set by
 * the caller's own rhi_cmd_barrier calls, which go out first), and end
 * orders the external work before everything after and makes the backend's
 * cached bindings untrue, because the external work may have bound anything:
 * the graphics pipeline is bound again, every descriptor set is dirty (the
 * next draw binds the sets its pipeline uses), the viewport, scissor, index
 * buffer, stencil reference and blend constant are forgotten (the next pass
 * records its own defaults; the others are set again by the next draw), the
 * vertex buffers too, and the dynamic depth and blend state is set again at
 * the next draw.  Both barriers are counted in stats.barriers. */
static void vkr_external_barrier(VkrCmdList *c, bool begin)
{
    vkr_flush_barriers(c);
    const VkMemoryBarrier mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
    };
    const VkPipelineStageFlags dst =
        begin ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    vkCmdPipelineBarrier(c->cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, dst, 0, 1, &mb, 0, NULL, 0,
                         NULL);
    vkr_hold_note(VKR_HOLD_EXTERNAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, dst, 0u, false);
    g_vkr.stats.barriers++;
}

bool rhi_cmd_external_begin(RhiCommandList cl)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->recording || !g_vkr.queueCompute) {
        return false;
    }
    if (c->inPass || c->externalOpen) {
        static uint32_t s_refused;
        if (s_refused++ < 8u) {
            VKR_LOG("rhi_cmd_external_begin: %s: refused (%u)",
                    c->inPass ? "inside a render pass" : "already open", s_refused);
        }
        return false;
    }
    vkr_occ_close(c);
    vkr_external_barrier(c, true);
    c->externalOpen = true;
    return true;
}

void rhi_cmd_external_end(RhiCommandList cl)
{
    VkrCmdList *c = vkr_get_cmd(cl);
    if (!c || !c->recording || !c->externalOpen) {
        return;
    }
    c->externalOpen = false;
    vkr_external_barrier(c, false);
    c->known = 0;
    c->vbKnown = 0;
    c->ibId = 0;
    c->dynValid = 0;
    c->dynFresh = 0;
    c->groupDirty = 0xFFu;
    if (c->pipeline) {
        vkCmdBindPipeline(c->cb, c->pipeline->bindPoint, c->pipeline->pipeline);
        g_vkr.stats.pipelineBinds++;
    }
}
