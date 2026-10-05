/* rhi_vk_swapchain_test.c: the window path of the Vulkan backend (SDL3
 * surface, swapchain, acquire, present, resize) without a display, through
 * SDL's "offscreen" video driver, which creates its Vulkan surface with
 * VK_EXT_headless_surface (lavapipe has it).
 *
 * Each frame clears the backbuffer to a known colour, reads it back (the
 * swapchain format is BGRA8 or RGBA8; the test checks the channel order
 * rhi_SwapchainFormat reports) and presents.  Exit 77 when SDL has no
 * offscreen Vulkan surface or no device presents to it. */
#include "rhi.h"
#include "vk/rhi_vk.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

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
    static uint8_t px[128 * 96 * 4];
    for (int frame = 0; frame < 6 && !failures; frame++) {
        uint32_t w = frame < 3 ? 64u : 128u, h = frame < 3 ? 48u : 96u;
        if (frame == 3) {
            /* resize as rd_present would on a window event */
            SDL_SetWindowSize(win, (int)w, (int)h);
            SDL_SyncWindow(win);
            if (!rhi_ResizeSwapchain(w, h, true)) {
                printf("FAIL rhi_ResizeSwapchain\n");
                failures++;
                break;
            }
        }
        rhi_WaitFrame();
        RhiTexture bb = rhi_AcquireBackbuffer();
        if (!bb.id) {
            printf("FAIL frame %d: no backbuffer\n", frame);
            failures++;
            break;
        }
        RhiCommandList cl = rhi_BeginCommands();
        RhiTextureBarrier b0 = {bb, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
        rhi_CmdBarrier(cl, &b0, 1);
        uint8_t r = (uint8_t)(32 * frame + 16), g = 0x80, bl = 0xF0;
        RhiRenderPassDesc rp = {0};
        rp.color[0] = (RhiColorAttachment){
            bb, RHI_LOAD_CLEAR, {(float)r / 255.0f, (float)g / 255.0f, (float)bl / 255.0f, 1.0f}};
        rp.colorCount = 1;
        rp.width = w;
        rp.height = h;
        rhi_CmdBeginRenderPass(cl, &rp);
        rhi_CmdEndRenderPass(cl);
        RhiTextureBarrier b1 = {bb, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC};
        rhi_CmdBarrier(cl, &b1, 1);
        rhi_Submit(cl);

        uint32_t pitch = 0;
        if (!rhi_ReadbackTexture(bb, RHI_ASPECT_COLOR, px, sizeof(px), &pitch) || pitch != w * 4) {
            printf("FAIL frame %d: readback (pitch %u)\n", frame, pitch);
            failures++;
            break;
        }
        const uint8_t *p = px + (size_t)(h / 2) * pitch + (size_t)(w / 2) * 4;
        uint8_t want[4] = {r, g, bl, 255};
        if (fmt == RHI_FMT_BGRA8_UNORM) {
            want[0] = bl;
            want[2] = r;
        }
        if (p[0] != want[0] || p[1] != want[1] || p[2] != want[2] || p[3] != want[3]) {
            printf("FAIL frame %d: got %u %u %u %u, expected %u %u %u %u\n", frame, p[0], p[1],
                   p[2], p[3], want[0], want[1], want[2], want[3]);
            failures++;
        }

        RhiCommandList cl2 = rhi_BeginCommands();
        RhiTextureBarrier b2 = {bb, RHI_STATE_COPY_SRC, RHI_STATE_PRESENT};
        rhi_CmdBarrier(cl2, &b2, 1);
        rhi_Submit(cl2);
        rhi_Present();
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
