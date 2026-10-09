/* vk_surface_android.c: the window's surface on Android (v0.4.3, package
 * AN-22a; vk_internal.h vkr_CreateWindowSurface).
 *
 * SDL_Vulkan_CreateSurface fetches vkCreateAndroidSurfaceKHR through SDL's
 * own loader.  With a graphics driver the program loaded itself
 * (rhi_SetVulkanLoader, port/platform/android/gpu_driver_android.c) the
 * instance belongs to another loader, so the surface is made here through
 * the vkGetInstanceProcAddr rhi_Init gave volk, with either driver.  The
 * instance extensions are SDL's list (VK_KHR_surface and
 * VK_KHR_android_surface on Android), so the call is enabled. */
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include "vk_internal.h"
#ifdef ICO_RHI_HAVE_SDL

#include <SDL3/SDL.h>

#endif

bool vkr_CreateWindowSurface(void *sdlWindow, VkSurfaceKHR *out)
{
    *out = VK_NULL_HANDLE;
#ifndef ICO_RHI_HAVE_SDL
    (void)sdlWindow;
    return false;
#else
    if (sdlWindow == NULL || g_vkr.instance == VK_NULL_HANDLE) {
        return false;
    }
    /* NULL while the app is in the background (SDL clears it when the
       system takes the native surface away); the caller tries again */
    struct ANativeWindow *native = (struct ANativeWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties((SDL_Window *)sdlWindow), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER,
        NULL);
    if (native == NULL) {
        return false;
    }
    PFN_vkCreateAndroidSurfaceKHR create = (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(
        g_vkr.instance, "vkCreateAndroidSurfaceKHR");
    if (create == NULL) {
        VKR_LOG("vkCreateAndroidSurfaceKHR is not available on this instance");
        return false;
    }
    const VkAndroidSurfaceCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = native,
    };
    const VkResult r = create(g_vkr.instance, &info, NULL, out);
    if (r != VK_SUCCESS) {
        VKR_LOG("vkCreateAndroidSurfaceKHR: %d", (int)r);
        *out = VK_NULL_HANDLE;
        return false;
    }
    return true;
#endif
}
