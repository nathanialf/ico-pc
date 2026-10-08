/*
 * port/fmv/test/rd_video_window_test.c
 *
 * v0.4.2 N1: the renderer's output follows the swapchain.  rd on a window
 * (SDL's "offscreen" video driver, whose Vulkan surface is
 * VK_EXT_headless_surface; lavapipe in the container): a movie frame at the
 * window's first size, then the window resized WITHOUT rd_ResizeOutput (the
 * size event an Android rotation or unfold may never send) and a present
 * forced out of date (rhi_vk.h's test hook), so the backend rebuilds the
 * swapchain at the new size on its own.  The next movie frame acquires an
 * image of the new size: the output size must follow it (rd_GetSettings,
 * rd_OutputFollowed once), a pending settings change must not take it back,
 * and the validation layer must report nothing.
 *
 * Exit 77 without SDL's offscreen Vulkan window or a Vulkan device.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "../fmv/rd_video.h"
#include "rd.h"
#include "rhi.h"
#include "vk/rhi_vk.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

enum { AW = 64, AH = 48, BW = 96, BH = 72, PW = 32, PH = 24 };

static uint8_t s_y[PW * PH], s_u[(PW / 2) * (PH / 2)], s_v[(PW / 2) * (PH / 2)];

static int movieFrame(void)
{
    const uint32_t pitch[3] = {PW, PW / 2, PW / 2};
    return rd_VideoFrame(s_y, s_u, s_v, pitch, PW, PH);
}

static void run(SDL_Window *win)
{
    memset(s_y, 120, sizeof(s_y));
    memset(s_u, 100, sizeof(s_u));
    memset(s_v, 160, sizeof(s_v));
    rd_VideoSetDisplay(AW, AH);

    /* size A: the swapchain, the output and the window agree */
    CHECK(movieFrame() == 0, "frame at %ux%u", AW, AH);
    uint32_t sw = 0, sh = 0;
    CHECK(rhi_SwapchainSize(&sw, &sh) && sw == AW && sh == AH, "swapchain %ux%u, want %ux%u", sw,
          sh, AW, AH);
    CHECK(rd_GetSettings()->outputWidth == AW && rd_GetSettings()->outputHeight == AH,
          "output %ux%u at the start", rd_GetSettings()->outputWidth,
          rd_GetSettings()->outputHeight);
    CHECK(!rd_OutputFollowed(NULL, NULL), "nothing followed at the start");

    /* size B without rd_ResizeOutput; the next present reports out of date
       and the backend rebuilds the swapchain at the window's size */
    SDL_SetWindowSize(win, BW, BH);
    SDL_SyncWindow(win);
    vkr_TestForcePresentResult(RHI_VK_TEST_OUT_OF_DATE);
    const uint32_t n0 = vkr_TestSwapchainCreations();
    CHECK(movieFrame() == 0, "the frame presented out of date");
    CHECK(vkr_TestSwapchainCreations() == n0 + 1, "%u swapchains made on out of date, want 1",
          vkr_TestSwapchainCreations() - n0);
    sw = sh = 0;
    CHECK(rhi_SwapchainSize(&sw, &sh) && sw == BW && sh == BH,
          "swapchain %ux%u after the rebuild, want %ux%u", sw, sh, BW, BH);

    /* the next frame acquires a B image: the output follows it */
    CHECK(movieFrame() == 0, "the frame on the rebuilt swapchain");
    CHECK(rd_GetSettings()->outputWidth == BW && rd_GetSettings()->outputHeight == BH,
          "output %ux%u after the rebuild, want the swapchain's %ux%u",
          rd_GetSettings()->outputWidth, rd_GetSettings()->outputHeight, BW, BH);
    uint32_t fw = 0, fh = 0;
    CHECK(rd_OutputFollowed(&fw, &fh) && fw == BW && fh == BH, "rd_OutputFollowed %ux%u", fw, fh);
    CHECK(!rd_OutputFollowed(NULL, NULL), "rd_OutputFollowed reports a change once");

    /* the settings applied at a frame's start keep the followed size */
    rd_BeginFrame();
    rd_DiscardFrame();
    CHECK(movieFrame() == 0, "a frame after the settings applied");
    CHECK(rd_GetSettings()->outputWidth == BW && rd_GetSettings()->outputHeight == BH,
          "output %ux%u after rd_BeginFrame", rd_GetSettings()->outputWidth,
          rd_GetSettings()->outputHeight);
    CHECK(!rd_OutputFollowed(NULL, NULL), "no second change at the same size");
}

int main(void)
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SKIP rd_video_window_test: SDL_Init: %s\n", SDL_GetError());
        return 77;
    }
    SDL_Window *win = SDL_CreateWindow("rd_video_window_test", AW, AH, SDL_WINDOW_VULKAN);
    if (!win) {
        printf("SKIP rd_video_window_test: no Vulkan window: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = AW;
    s.outputHeight = AH;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    if (!rd_Init(512, 512, &s, win)) {
        printf("SKIP rd_video_window_test: no Vulkan device presents to the window\n");
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 77;
    }
    if (rhi_Backend() != RHI_BACKEND_VULKAN || rhi_SwapchainFormat() == RHI_FMT_UNKNOWN) {
        printf("SKIP rd_video_window_test: not a Vulkan window device\n");
        rd_Shutdown();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 77;
    }
    printf("rd_video_window_test: adapter %s\n", rhi_AdapterName());
    run(win);
    rd_VideoShutdown();
    const uint32_t verr = rhi_vk_ValidationErrorCount();
    CHECK(verr == 0, "%u validation errors", verr);
    rd_Shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (failures) {
        printf("rd_video_window_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_video_window_test: all passed\n");
    return 0;
}
