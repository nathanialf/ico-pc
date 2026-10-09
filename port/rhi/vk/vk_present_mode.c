/* vk_present_mode.c: the swapchain's present mode from what the surface
 * offers.  Pure: no device and no Vulkan call, so
 * test/rhi_vk_present_mode_test.c compiles it on its own. */
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
                                         bool preferMailbox)
{
    if (vsync) {
        /* FIFO is always available; mailbox when asked for (rhi_prefer_mailbox)
         * and offered: still no tearing, but a present never waits for the
         * display */
        if (preferMailbox && vkr_offered(modes, n, VK_PRESENT_MODE_MAILBOX_KHR)) {
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    /* vsync off is IMMEDIATE wherever the surface has it: a present never
     * waits, and the picture tears where the compositor lets it (the Deck's
     * "Allow Tearing", KWin).  Mailbox before it made vsync off look like on
     * under Mesa, which offers both.  Then mailbox, then FIFO. */
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
    default:
        return NULL; /* a mode the backend never picks (the shared ones) */
    }
}
