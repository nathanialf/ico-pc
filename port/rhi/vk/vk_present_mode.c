/* vk_present_mode.c: the swapchain's present mode from what the surface
 * offers, and whether the display's timing of presents is used.  Pure: no
 * device and no Vulkan call, so test/rhi_vk_present_mode_test.c compiles it
 * on its own. */
#include "vk_internal.h"

static bool vkr_offered(const VkPresentModeKHR *modes, uint32_t n, VkPresentModeKHR m)
{
    for (uint32_t i = 0; i < n; i++) {
        if (modes[i] == m) {
            return true;
        }
    }
    return false;
}

VkPresentModeKHR vkr_choose_present_mode(const VkPresentModeKHR *modes, uint32_t n, bool vsync,
                                         bool preferMailbox, bool preferLatestReady, bool forceFifo)
{
    /* the FIFO fallback (rhi_present_fifo_fallback): presents blocked for
     * hundreds of milliseconds under mailbox on a driver, so FIFO whatever
     * vsync and the preferences say; FIFO is always offered */
    if (forceFifo) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    if (vsync) {
        /* FIFO is always available; mailbox when asked for (rhi_prefer_mailbox)
         * and offered: still no tearing, but a present never waits for the
         * display */
        if (preferMailbox && vkr_offered(modes, n, VK_PRESENT_MODE_MAILBOX_KHR)) {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
        /* FIFO latest ready (VK_EXT_present_mode_fifo_latest_ready, opt-in:
         * ICO_VK_FIFO_LATEST_READY=1) where FIFO would be taken: the queue
         * is drained to the newest ready picture at each vblank, so a present
         * never waits for the display and nothing tears; what a present that
         * started near a vblank does to the picture's cadence is the same as
         * under mailbox (pace_loop_test testLatestReady) */
        if (preferLatestReady && vkr_offered(modes, n, VK_PRESENT_MODE_FIFO_LATEST_READY_EXT)) {
            return VK_PRESENT_MODE_FIFO_LATEST_READY_EXT;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    /* vsync off is IMMEDIATE wherever the surface has it: a present never
     * waits, and the picture tears where the compositor lets it (the Deck's
     * "Allow Tearing", KWin).  Mailbox first would make vsync off look like
     * on under Mesa, which offers both.  Then mailbox, then FIFO. */
    if (vkr_offered(modes, n, VK_PRESENT_MODE_IMMEDIATE_KHR)) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    if (vkr_offered(modes, n, VK_PRESENT_MODE_MAILBOX_KHR)) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

const char *vkr_present_mode_name(VkPresentModeKHR m)
{
    switch (m) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "mailbox";
    case VK_PRESENT_MODE_FIFO_KHR:
        return "fifo";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "fifo_relaxed";
    case VK_PRESENT_MODE_FIFO_LATEST_READY_EXT:
        return "fifo latest ready";
    default:
        return NULL; /* a mode the backend never picks (the shared ones) */
    }
}

bool vkr_present_wait_wanted(bool offered, const char *asked, bool canThread)
{
    /* ICO_VK_PRESENT_WAIT set to anything but "" or "0" */
    const bool on = asked != NULL && asked[0] != '\0' && !(asked[0] == '0' && asked[1] == '\0');

    return on && offered && canThread;
}
