/* rhi_vk_swapchain_test.c: the window path of the Vulkan backend (SDL3
 * surface, swapchain, acquire, present, resize) without a display, through
 * SDL's "offscreen" video driver, which creates its Vulkan surface with
 * VK_EXT_headless_surface (lavapipe has it).
 *
 * Each frame clears the backbuffer to a known colour, reads it back (the
 * swapchain format is BGRA8 or RGBA8; the test checks the channel order
 * rhi_SwapchainFormat reports) and presents.  Package AN-D: the surface
 * released and made again (the Android background and foreground), a
 * present reporting suboptimal without a size change (a new swapchain on
 * the desktop, none on Android) and
 * one reporting the surface lost (a new surface and swapchain), through
 * the rhi_vk.h test hook.  v0.4.2 N1: rhi_SwapchainSize reports the
 * swapchain's size (none without a surface), and a window resized without
 * rhi_ResizeSwapchain, then a present forced out of date, leaves a
 * swapchain at the window's new size, which rhi_SwapchainSize reports and
 * the next frame draws at.  Exit 77 when SDL has no offscreen Vulkan surface
 * or no device presents to it. */
#include "rhi.h"
#include "vk/rhi_vk.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

/* One frame on the backbuffer at the window's pixel size: clear to a
 * colour from n, read it back, present.  0, or 1 with the reason printed. */
static int frame(SDL_Window *win, RhiFormat fmt, int n)
{
    static uint8_t px[128 * 96 * 4];
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    const uint32_t w = (uint32_t)pw, h = (uint32_t)ph;
    if (w == 0 || h == 0 || (size_t)w * h * 4 > sizeof(px)) {
        printf("FAIL frame %d: window %dx%d\n", n, pw, ph);
        return 1;
    }
    rhi_WaitFrame();
    RhiTexture bb = rhi_AcquireBackbuffer();
    if (!bb.id) {
        printf("FAIL frame %d: no backbuffer\n", n);
        return 1;
    }
    RhiCommandList cl = rhi_BeginCommands();
    RhiTextureBarrier b0 = {bb, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
    rhi_CmdBarrier(cl, &b0, 1);
    uint8_t r = (uint8_t)(16 * (n % 16) + 8), g = 0x80, bl = 0xF0;
    RhiRenderPassDesc rp = {0};
    rp.color[0] =
        (RhiColorAttachment){bb,
                             RHI_LOAD_CLEAR,
                             {(float)r / 255.0f, (float)g / 255.0f, (float)bl / 255.0f, 1.0f},
                             RHI_STORE_STORE};
    rp.colorCount = 1;
    rp.width = w;
    rp.height = h;
    rhi_CmdBeginRenderPass(cl, &rp);
    rhi_CmdEndRenderPass(cl);
    RhiTextureBarrier b1 = {bb, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC};
    rhi_CmdBarrier(cl, &b1, 1);
    rhi_Submit(cl);

    int failures = 0;
    uint32_t pitch = 0;
    if (!rhi_ReadbackTexture(bb, RHI_ASPECT_COLOR, px, sizeof(px), &pitch) || pitch != w * 4) {
        printf("FAIL frame %d: readback (pitch %u)\n", n, pitch);
        failures++;
    } else {
        const uint8_t *p = px + (size_t)(h / 2) * pitch + (size_t)(w / 2) * 4;
        uint8_t want[4] = {r, g, bl, 255};
        if (fmt == RHI_FMT_BGRA8_UNORM) {
            want[0] = bl;
            want[2] = r;
        }
        if (p[0] != want[0] || p[1] != want[1] || p[2] != want[2] || p[3] != want[3]) {
            printf("FAIL frame %d: got %u %u %u %u, expected %u %u %u %u\n", n, p[0], p[1], p[2],
                   p[3], want[0], want[1], want[2], want[3]);
            failures++;
        }
    }

    RhiCommandList cl2 = rhi_BeginCommands();
    RhiTextureBarrier b2 = {bb, RHI_STATE_COPY_SRC, RHI_STATE_PRESENT};
    rhi_CmdBarrier(cl2, &b2, 1);
    rhi_Submit(cl2);
    rhi_Present();
    return failures;
}

/* Package AN-D: the surface's lifecycle on the device made by run() */
static int lifecycle(SDL_Window *win, RhiFormat fmt)
{
    int failures = 0;

    /* a present that reports suboptimal at the same size: shown; on
       Android no new swapchain (a display turned by the compositor reports
       it at every present), on the desktop one (Wayland and gamescope
       report it for direct scanout) */
#ifdef __ANDROID__
    const uint32_t wantNew = 0;
#else
    const uint32_t wantNew = 1;
#endif
    uint32_t n0 = vkr_TestSwapchainCreations();
    vkr_TestForcePresentResult(RHI_VK_TEST_SUBOPTIMAL);
    failures += frame(win, fmt, 10);
    failures += frame(win, fmt, 11);
    if (vkr_TestSwapchainCreations() - n0 != wantNew) {
        printf("FAIL suboptimal without a size change made %u swapchain(s), want %u\n",
               vkr_TestSwapchainCreations() - n0, wantNew);
        failures++;
    }

    /* the background: the surface goes, the device stays; nothing to
       acquire, a present does nothing, the format is kept */
    rhi_WaitIdle();
    rhi_ReleaseSurface();
    if (rhi_AcquireBackbuffer().id != 0) {
        printf("FAIL a backbuffer without a surface\n");
        failures++;
    }
    rhi_Present();
    if (rhi_SwapchainFormat() != fmt) {
        printf("FAIL the format after the release is %d, was %d\n", (int)rhi_SwapchainFormat(),
               (int)fmt);
        failures++;
    }
    {
        uint32_t sw = 0, sh = 0;
        if (rhi_SwapchainSize(&sw, &sh)) {
            printf("FAIL rhi_SwapchainSize reports %ux%u without a surface\n", sw, sh);
            failures++;
        }
    }
    rhi_ReleaseSurface(); /* twice: nothing */

    /* the foreground: a surface and one swapchain, and frames as before */
    n0 = vkr_TestSwapchainCreations();
    if (!rhi_RecreateSurface(win)) {
        printf("FAIL rhi_RecreateSurface\n");
        return failures + 1;
    }
    if (vkr_TestSwapchainCreations() != n0 + 1) {
        printf("FAIL the recreation made %u swapchains, expected 1\n",
               vkr_TestSwapchainCreations() - n0);
        failures++;
    }
    if (rhi_SwapchainFormat() != fmt) {
        printf("FAIL the format after the recreation is %d, was %d\n", (int)rhi_SwapchainFormat(),
               (int)fmt);
        failures++;
    }
    failures += frame(win, fmt, 12);
    failures += frame(win, fmt, 13);

    /* a present that reports the surface lost: a new surface and
       swapchain at once, and the next frame draws on them */
    n0 = vkr_TestSwapchainCreations();
    vkr_TestForcePresentResult(RHI_VK_TEST_SURFACE_LOST);
    failures += frame(win, fmt, 14);
    if (vkr_TestSwapchainCreations() != n0 + 1) {
        printf("FAIL surface lost: %u swapchains made, expected 1\n",
               vkr_TestSwapchainCreations() - n0);
        failures++;
    }
    failures += frame(win, fmt, 15);
    return failures;
}

/* v0.4.2 N1: the window resized without rhi_ResizeSwapchain (an Android
 * rotation or unfold whose size event the renderer has not seen); a present
 * forced out of date rebuilds the swapchain at the window's size, and
 * rhi_SwapchainSize reports it */
static int followSize(SDL_Window *win, RhiFormat fmt)
{
    int failures = 0;
    int pw = 0, ph = 0;
    uint32_t sw = 0, sh = 0;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    if (!rhi_SwapchainSize(&sw, &sh) || sw != (uint32_t)pw || sh != (uint32_t)ph) {
        printf("FAIL rhi_SwapchainSize %ux%u, window %dx%d\n", sw, sh, pw, ph);
        return failures + 1;
    }
    const uint32_t aw = sw, ah = sh, bw = 96, bh = 64;
    SDL_SetWindowSize(win, (int)bw, (int)bh);
    SDL_SyncWindow(win);
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    if ((uint32_t)pw != bw || (uint32_t)ph != bh) {
        printf("FAIL the window is %dx%d, asked %ux%u\n", pw, ph, bw, bh);
        return failures + 1;
    }
    /* a frame on the old swapchain (its own size), presented out of date */
    rhi_WaitFrame();
    RhiTexture bb = rhi_AcquireBackbuffer();
    if (!bb.id) {
        printf("FAIL no backbuffer on the old swapchain\n");
        return failures + 1;
    }
    RhiCommandList cl = rhi_BeginCommands();
    RhiTextureBarrier b0 = {bb, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
    rhi_CmdBarrier(cl, &b0, 1);
    RhiRenderPassDesc rp = {0};
    rp.color[0] =
        (RhiColorAttachment){bb, RHI_LOAD_CLEAR, {0.0f, 0.0f, 0.0f, 1.0f}, RHI_STORE_STORE};
    rp.colorCount = 1;
    rp.width = aw;
    rp.height = ah;
    rhi_CmdBeginRenderPass(cl, &rp);
    rhi_CmdEndRenderPass(cl);
    RhiTextureBarrier b1 = {bb, RHI_STATE_RENDER_TARGET, RHI_STATE_PRESENT};
    rhi_CmdBarrier(cl, &b1, 1);
    rhi_Submit(cl);
    const uint32_t n0 = vkr_TestSwapchainCreations();
    vkr_TestForcePresentResult(RHI_VK_TEST_OUT_OF_DATE);
    rhi_Present();
    if (vkr_TestSwapchainCreations() != n0 + 1) {
        printf("FAIL out of date: %u swapchains made, expected 1\n",
               vkr_TestSwapchainCreations() - n0);
        failures++;
    }
    sw = sh = 0;
    if (!rhi_SwapchainSize(&sw, &sh) || sw != bw || sh != bh) {
        printf("FAIL rhi_SwapchainSize %ux%u after out of date, want %ux%u (was %ux%u)\n", sw, sh,
               bw, bh, aw, ah);
        failures++;
    }
    /* the next frames draw at the window's (and the swapchain's) size */
    failures += frame(win, fmt, 20);
    failures += frame(win, fmt, 21);
    return failures;
}

static int run(SDL_Window *win)
{
    RhiDeviceDesc dd = {win, true, true, "rhi_vk_swapchain_test"};
    if (!rhi_Init(&dd)) {
        printf("SKIP rhi_vk_swapchain_test: rhi_Init with an offscreen window failed\n");
        return 77;
    }
    int failures = 0;
    RhiFormat fmt = rhi_SwapchainFormat();
    if (fmt != RHI_FMT_BGRA8_UNORM && fmt != RHI_FMT_RGBA8_UNORM) {
        printf("FAIL swapchain format %d\n", (int)fmt);
        failures++;
    }
    for (int n = 0; n < 6 && !failures; n++) {
        if (n == 3) {
            /* resize as rd_present would on a window event */
            SDL_SetWindowSize(win, 128, 96);
            SDL_SyncWindow(win);
            if (!rhi_ResizeSwapchain(128, 96, true)) {
                printf("FAIL rhi_ResizeSwapchain\n");
                failures++;
                break;
            }
        }
        failures += frame(win, fmt, n);
    }
    if (!failures) {
        failures += lifecycle(win, fmt);
    }
    if (!failures) {
        failures += followSize(win, fmt);
    }
    rhi_WaitIdle();
    rhi_Shutdown();
    if (rhi_vk_ValidationErrorCount()) {
        printf("FAIL %u validation error(s)\n", rhi_vk_ValidationErrorCount());
        failures++;
    }
    return failures ? 1 : 0;
}

int main(void)
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SKIP rhi_vk_swapchain_test: SDL_Init: %s\n", SDL_GetError());
        return 77;
    }
    SDL_Window *win = SDL_CreateWindow("rhi_vk_swapchain_test", 64, 48, SDL_WINDOW_VULKAN);
    if (!win) {
        printf("SKIP rhi_vk_swapchain_test: no Vulkan window: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }
    int rc = run(win);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (rc == 0) {
        printf("rhi_vk_swapchain_test: all checks passed\n");
    }
    return rc;
}
