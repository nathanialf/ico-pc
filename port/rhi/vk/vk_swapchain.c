/* vk_swapchain.c: the swapchain over SDL3's Vulkan surface.  Absent when
 * the device is headless (RhiDeviceDesc.sdlWindow == NULL). */
#include "vk_internal.h"
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RHI_HAVE_SDL

#include <SDL3/SDL.h>

#endif

static void vkr_DestroySwapResources(void)
{
    for (uint32_t i = 0; i < g_vkr.swapImageCount; i++) {
        vkr_ReleaseSwapchainImage(g_vkr.swapTextures[i]);
        g_vkr.swapTextures[i] = 0;
        if (g_vkr.renderDone[i]) {
            vkDestroySemaphore(g_vkr.device, g_vkr.renderDone[i], NULL);
            g_vkr.renderDone[i] = VK_NULL_HANDLE;
        }
    }
    g_vkr.swapImageCount = 0;
}

void vkr_SwapchainDestroy(void)
{
    if (!g_vkr.device) {
        return;
    }
    vkr_DestroySwapResources();
    if (g_vkr.swapchain) {
        vkDestroySwapchainKHR(g_vkr.device, g_vkr.swapchain, NULL);
        g_vkr.swapchain = VK_NULL_HANDLE;
    }
}

/* package P1 (rhi_PreferMailbox): kept across rhi_Init, which clears g_vkr */
static bool s_preferMailbox;

static VkPresentModeKHR vkr_PickPresentMode(bool vsync)
{
    uint32_t n = 0;
    if (vsync) {
        /* FIFO is always available; mailbox when asked for and offered:
         * still no tearing, but a present never waits for the display */
        if (s_preferMailbox) {
            VkPresentModeKHR modes[16];
            vkGetPhysicalDeviceSurfacePresentModesKHR(g_vkr.phys, g_vkr.surface, &n, NULL);
            if (n > 16) {
                n = 16;
            }
            vkGetPhysicalDeviceSurfacePresentModesKHR(g_vkr.phys, g_vkr.surface, &n, modes);
            for (uint32_t i = 0; i < n; i++) {
                if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    return modes[i];
                }
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_vkr.phys, g_vkr.surface, &n, NULL);
    VkPresentModeKHR modes[16];
    if (n > 16) {
        n = 16;
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_vkr.phys, g_vkr.surface, &n, modes);
    VkPresentModeKHR pick = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < n; i++) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            return modes[i];
        }
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            pick = modes[i];
        }
    }
    return pick;
}

bool vkr_SwapchainCreate(uint32_t w, uint32_t h, bool vsync)
{
    if (!g_vkr.surface) {
        return false;
    }
    VkSurfaceCapabilitiesKHR caps;
    if (!VKR_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkr.phys, g_vkr.surface, &caps))) {
        return false;
    }
    if (caps.currentExtent.width != UINT32_MAX) {
        w = caps.currentExtent.width;
        h = caps.currentExtent.height;
    }
    if (w < caps.minImageExtent.width) {
        w = caps.minImageExtent.width;
    }
    if (h < caps.minImageExtent.height) {
        h = caps.minImageExtent.height;
    }
    if (w > caps.maxImageExtent.width) {
        w = caps.maxImageExtent.width;
    }
    if (h > caps.maxImageExtent.height) {
        h = caps.maxImageExtent.height;
    }
    if (w == 0 || h == 0) {
        return false; /* minimised: the caller retries on the next resize */
    }

    /* BGRA8 UNORM in sRGB-nonlinear colour space: the game's colours are
     * written as-is, like the PS2's DAC */
    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkr.phys, g_vkr.surface, &nf, NULL);
    VkSurfaceFormatKHR *fmts = calloc(nf ? nf : 1, sizeof(*fmts));
    if (!fmts) {
        return false;
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkr.phys, g_vkr.surface, &nf, fmts);
    VkSurfaceFormatKHR pick = nf ? fmts[0] : (VkSurfaceFormatKHR){VK_FORMAT_B8G8R8A8_UNORM, 0};
    bool found = false;
    for (uint32_t i = 0; i < nf && !found; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            pick = fmts[i];
            found = true;
        }
    }
    for (uint32_t i = 0; i < nf && !found; i++) {
        if (fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM &&
            fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            pick = fmts[i];
            found = true;
        }
    }
    free(fmts);
    if (!found) {
        VKR_LOG("surface offers neither BGRA8 nor RGBA8 UNORM; using format %d", (int)pick.format);
    }
    g_vkr.swapFormat = pick.format;
    g_vkr.swapColorSpace = pick.colorSpace;
    g_vkr.swapRhiFormat =
        pick.format == VK_FORMAT_R8G8B8A8_UNORM ? RHI_FMT_RGBA8_UNORM : RHI_FMT_BGRA8_UNORM;

    uint32_t count = caps.minImageCount + 1u;
    if (caps.maxImageCount && count > caps.maxImageCount) {
        count = caps.maxImageCount;
    }
    if (count > 8) {
        count = 8;
    }
    VkSwapchainKHR old = g_vkr.swapchain;
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = g_vkr.surface,
        .minImageCount = count,
        .imageFormat = pick.format,
        .imageColorSpace = pick.colorSpace,
        .imageExtent = {w, h},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = vkr_PickPresentMode(vsync),
        .clipped = VK_TRUE,
        .oldSwapchain = old,
    };
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    VkSwapchainKHR sc;
    if (!VKR_CHECK(vkCreateSwapchainKHR(g_vkr.device, &ci, NULL, &sc))) {
        return false;
    }
    vkr_DestroySwapResources();
    if (old) {
        vkDestroySwapchainKHR(g_vkr.device, old, NULL);
    }
    g_vkr.swapchain = sc;
    g_vkr.mailbox = vsync && ci.presentMode == VK_PRESENT_MODE_MAILBOX_KHR;
    g_vkr.swapWidth = w;
    g_vkr.swapHeight = h;
    g_vkr.vsync = vsync;

    VkImage images[8];
    uint32_t n = 8;
    vkGetSwapchainImagesKHR(g_vkr.device, sc, &n, images);
    VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < n; i++) {
        g_vkr.swapTextures[i] =
            vkr_RegisterSwapchainImage(images[i], pick.format, g_vkr.swapRhiFormat, w, h);
        VKR_CHECK(vkCreateSemaphore(g_vkr.device, &sci, NULL, &g_vkr.renderDone[i]));
    }
    g_vkr.swapImageCount = n;
    g_vkr.swapAcquired = false;
    g_vkr.acquireWaitPending = false;
    return true;
}

bool rhi_ResizeSwapchain(uint32_t width, uint32_t height, bool vsync)
{
    if (!g_vkr.surface) {
        return false;
    }
    const uint64_t t0 = vkr_NowNs();
    vkDeviceWaitIdle(g_vkr.device);
    g_vkr.stats.waitIdles++;
    g_vkr.stats.fenceWaitNs += vkr_NowNs() - t0;
    return vkr_SwapchainCreate(width, height, vsync);
}

void rhi_PreferMailbox(bool on)
{
    s_preferMailbox = on;
}

bool rhi_PresentMailbox(void)
{
    return g_vkr.swapchain && g_vkr.mailbox;
}

RhiFormat rhi_SwapchainFormat(void)
{
    return g_vkr.surface ? g_vkr.swapRhiFormat : RHI_FMT_UNKNOWN;
}

RhiTexture rhi_AcquireBackbuffer(void)
{
    RhiTexture out = {0};
    if (!g_vkr.swapchain) {
        return out;
    }
    if (g_vkr.swapAcquired) {
        out.id = g_vkr.swapTextures[g_vkr.swapImage];
        return out;
    }
    VkrFrame *f = vkr_CurFrame();
    uint32_t idx = 0;
    const uint64_t t0 = vkr_NowNs();
    VkResult r = vkAcquireNextImageKHR(g_vkr.device, g_vkr.swapchain, UINT64_MAX, f->acquireSem,
                                       VK_NULL_HANDLE, &idx);
    g_vkr.stats.acquireNs += vkr_NowNs() - t0;
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        return out; /* rd_present recreates via rhi_ResizeSwapchain */
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        VKR_CHECK(r);
        return out;
    }
    g_vkr.swapImage = idx;
    g_vkr.swapAcquired = true;
    g_vkr.acquireWaitPending = true;
    g_vkr.acquireSem = f->acquireSem;
    out.id = g_vkr.swapTextures[idx];
    return out;
}

void rhi_Present(void)
{
    if (!g_vkr.swapchain || !g_vkr.swapAcquired) {
        return;
    }
    if (!vkr_SubmitPresentSignal()) {
        return;
    }
    VkPresentInfoKHR pi = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_vkr.renderDone[g_vkr.swapImage],
        .swapchainCount = 1,
        .pSwapchains = &g_vkr.swapchain,
        .pImageIndices = &g_vkr.swapImage,
    };
    const uint64_t t0 = vkr_NowNs();
    VkResult r = vkQueuePresentKHR(g_vkr.queue, &pi);
    g_vkr.stats.presentNs += vkr_NowNs() - t0;
    g_vkr.stats.presents++;
    g_vkr.swapAcquired = false;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        int w = (int)g_vkr.swapWidth, h = (int)g_vkr.swapHeight;
#ifdef ICO_RHI_HAVE_SDL
        SDL_GetWindowSizeInPixels((SDL_Window *)g_vkr.window, &w, &h);
#endif
        rhi_ResizeSwapchain((uint32_t)w, (uint32_t)h, g_vkr.vsync);
    } else if (r != VK_SUCCESS) {
        VKR_CHECK(r);
    }
}
