/* rhi_vk_swapchain_test.c: the window path of the Vulkan backend (SDL3
 * surface, swapchain, acquire, present, resize) without a display, through
 * SDL's "offscreen" video driver, which creates its Vulkan surface with
 * VK_EXT_headless_surface (lavapipe has it).
 *
 * Each frame clears the backbuffer to a known colour, reads it back (the
 * swapchain format is BGRA8 or RGBA8; the test checks the channel order
 * rhi_swapchain_format reports) and presents.  Then the surface
 * released and made again (the Android background and foreground), a
 * present reporting suboptimal without a size change (a new swapchain on
 * the desktop, none on Android) and
 * one reporting the surface lost (a new surface and swapchain), through
 * the rhi_vk.h test hook.  rhi_swapchain_size reports the
 * swapchain's size (none without a surface), and a window resized without
 * rhi_resize_swapchain, then a present forced out of date, leaves a
 * swapchain at the window's new size, which rhi_swapchain_size reports and
 * the next frame draws at.  With ICO_VK_POLL_SURFACE=1 (always
 * on Android) a window resized without rhi_resize_swapchain and a present
 * that reports nothing (lavapipe's headless surface returns VK_SUCCESS)
 * leaves a swapchain at the window's new size, made by that present; the
 * desktop without the switch keeps the old one.  A list that moves the
 * backbuffer to PRESENT signals the present semaphore with its own submit:
 * such a frame costs one submit, and a frame whose drawing list does not
 * present costs one submit per list and none more.  The display's timing
 * of presents on a stand-in for vkWaitForPresentKHR (presentTiming, and a
 * wait that does not keep its timeout: presentWaitHang).  Each frame's
 * image counts as acquired (RhiStats.swapAcquired) from its acquire to its
 * present.  Exit
 * 77 when SDL has no offscreen Vulkan surface or no device presents to it. */
#include "rhi.h"
#include "vk/rhi_vk.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

/* setenv for the tests: the mingw C runtime has _putenv_s instead (an
   empty value removes the variable there; NULL removes it on both). */
static void testSetEnv(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

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
    rhi_wait_frame();
    RhiTexture bb = rhi_acquire_backbuffer();
    if (!bb.id) {
        printf("FAIL frame %d: no backbuffer\n", n);
        return 1;
    }
    int failures = 0;
    {
        /* the image held: one acquired, not yet presented (RhiStats.swapAcquired) */
        RhiStats st;
        rhi_get_stats(&st);
        if (st.swapImages == 0 || st.swapAcquired != 1) {
            printf("FAIL frame %d: %llu images, %llu acquired after the acquire\n", n,
                   (unsigned long long)st.swapImages, (unsigned long long)st.swapAcquired);
            failures++;
        }
    }
    RhiCommandList cl = rhi_begin_commands();
    RhiTextureBarrier b0 = {bb, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
    rhi_cmd_barrier(cl, &b0, 1);
    uint8_t r = (uint8_t)(16 * (n % 16) + 8), g = 0x80, bl = 0xF0;
    RhiRenderPassDesc rp = {0};
    rp.color[0] =
        (RhiColorAttachment){bb,
                             RHI_LOAD_CLEAR,
                             {(float)r / 255.0f, (float)g / 255.0f, (float)bl / 255.0f, 1.0f},
                             RHI_STORE_STORE,
                             false};
    rp.colorCount = 1;
    rp.width = w;
    rp.height = h;
    rhi_cmd_begin_render_pass(cl, &rp);
    rhi_cmd_end_render_pass(cl);
    RhiTextureBarrier b1 = {bb, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC};
    rhi_cmd_barrier(cl, &b1, 1);
    rhi_submit(cl);

    uint32_t pitch = 0;
    if (!rhi_readback_texture(bb, RHI_ASPECT_COLOR, px, sizeof(px), &pitch) || pitch != w * 4) {
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

    RhiCommandList cl2 = rhi_begin_commands();
    RhiTextureBarrier b2 = {bb, RHI_STATE_COPY_SRC, RHI_STATE_PRESENT};
    rhi_cmd_barrier(cl2, &b2, 1);
    rhi_submit(cl2);
    rhi_present();
    {
        RhiStats st;
        rhi_get_stats(&st);
        if (st.swapAcquired != 0) {
            printf("FAIL frame %d: %llu acquired after the present\n", n,
                   (unsigned long long)st.swapAcquired);
            failures++;
        }
    }
    return failures;
}

/* The surface's lifecycle on the device made by run() */
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
    uint32_t n0 = vkr_test_swapchain_creations();
    vkr_test_force_present_result(RHI_VK_TEST_SUBOPTIMAL);
    failures += frame(win, fmt, 10);
    failures += frame(win, fmt, 11);
    /* a suboptimal present was shown (rhi_present_shown) */
    if (!rhi_present_shown()) {
        printf("FAIL a suboptimal present not reported shown\n");
        failures++;
    }
    if (vkr_test_swapchain_creations() - n0 != wantNew) {
        printf("FAIL suboptimal without a size change made %u swapchain(s), want %u\n",
               vkr_test_swapchain_creations() - n0, wantNew);
        failures++;
    }

    /* the background: the surface goes, the device stays; nothing to
       acquire, a present does nothing, the format is kept */
    rhi_wait_idle();
    rhi_release_surface();
    if (rhi_acquire_backbuffer().id != 0) {
        printf("FAIL a backbuffer without a surface\n");
        failures++;
    }
    rhi_present();
    /* nothing reached the display (a minimised window has no swapchain
       either) */
    if (rhi_present_shown()) {
        printf("FAIL a present without a surface reported shown\n");
        failures++;
    }
    if (rhi_swapchain_format() != fmt) {
        printf("FAIL the format after the release is %d, was %d\n", (int)rhi_swapchain_format(),
               (int)fmt);
        failures++;
    }
    {
        uint32_t sw = 0, sh = 0;
        if (rhi_swapchain_size(&sw, &sh)) {
            printf("FAIL rhi_swapchain_size reports %ux%u without a surface\n", sw, sh);
            failures++;
        }
    }
    rhi_release_surface(); /* twice: nothing */

    /* the foreground: a surface and one swapchain, and frames again */
    n0 = vkr_test_swapchain_creations();
    if (!rhi_recreate_surface(win)) {
        printf("FAIL rhi_recreate_surface\n");
        return failures + 1;
    }
    if (vkr_test_swapchain_creations() != n0 + 1) {
        printf("FAIL the recreation made %u swapchains, expected 1\n",
               vkr_test_swapchain_creations() - n0);
        failures++;
    }
    if (rhi_swapchain_format() != fmt) {
        printf("FAIL the format after the recreation is %d, was %d\n", (int)rhi_swapchain_format(),
               (int)fmt);
        failures++;
    }
    failures += frame(win, fmt, 12);
    failures += frame(win, fmt, 13);

    /* a present that reports the surface lost: a new surface and
       swapchain at once, and the next frame draws on them */
    n0 = vkr_test_swapchain_creations();
    vkr_test_force_present_result(RHI_VK_TEST_SURFACE_LOST);
    failures += frame(win, fmt, 14);
    if (vkr_test_swapchain_creations() != n0 + 1) {
        printf("FAIL surface lost: %u swapchains made, expected 1\n",
               vkr_test_swapchain_creations() - n0);
        failures++;
    }
    failures += frame(win, fmt, 15);
    return failures;
}

/* The window resized without rhi_resize_swapchain (an Android
 * rotation or unfold whose size event the renderer has not seen); a present
 * forced out of date rebuilds the swapchain at the window's size, and
 * rhi_swapchain_size reports it */
static int followSize(SDL_Window *win, RhiFormat fmt)
{
    int failures = 0;
    int pw = 0, ph = 0;
    uint32_t sw = 0, sh = 0;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    if (!rhi_swapchain_size(&sw, &sh) || sw != (uint32_t)pw || sh != (uint32_t)ph) {
        printf("FAIL rhi_swapchain_size %ux%u, window %dx%d\n", sw, sh, pw, ph);
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
    rhi_wait_frame();
    RhiTexture bb = rhi_acquire_backbuffer();
    if (!bb.id) {
        printf("FAIL no backbuffer on the old swapchain\n");
        return failures + 1;
    }
    RhiCommandList cl = rhi_begin_commands();
    RhiTextureBarrier b0 = {bb, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
    rhi_cmd_barrier(cl, &b0, 1);
    RhiRenderPassDesc rp = {0};
    rp.color[0] =
        (RhiColorAttachment){bb, RHI_LOAD_CLEAR, {0.0f, 0.0f, 0.0f, 1.0f}, RHI_STORE_STORE, false};
    rp.colorCount = 1;
    rp.width = aw;
    rp.height = ah;
    rhi_cmd_begin_render_pass(cl, &rp);
    rhi_cmd_end_render_pass(cl);
    RhiTextureBarrier b1 = {bb, RHI_STATE_RENDER_TARGET, RHI_STATE_PRESENT};
    rhi_cmd_barrier(cl, &b1, 1);
    rhi_submit(cl);
    const uint32_t n0 = vkr_test_swapchain_creations();
    vkr_test_force_present_result(RHI_VK_TEST_OUT_OF_DATE);
    rhi_present();
    if (vkr_test_swapchain_creations() != n0 + 1) {
        printf("FAIL out of date: %u swapchains made, expected 1\n",
               vkr_test_swapchain_creations() - n0);
        failures++;
    }
    sw = sh = 0;
    if (!rhi_swapchain_size(&sw, &sh) || sw != bw || sh != bh) {
        printf("FAIL rhi_swapchain_size %ux%u after out of date, want %ux%u (was %ux%u)\n", sw, sh,
               bw, bh, aw, ah);
        failures++;
    }
    /* the next frames draw at the window's (and the swapchain's) size */
    failures += frame(win, fmt, 20);
    failures += frame(win, fmt, 21);
    return failures;
}

/* A frame on the swapchain at its own size (the window may differ),
 * cleared and presented with whatever the driver returns.  0, or 1. */
static int presentAtSwapSize(int n)
{
    uint32_t sw = 0, sh = 0;
    if (!rhi_swapchain_size(&sw, &sh)) {
        printf("FAIL frame %d: no swapchain\n", n);
        return 1;
    }
    rhi_wait_frame();
    RhiTexture bb = rhi_acquire_backbuffer();
    if (!bb.id) {
        printf("FAIL frame %d: no backbuffer\n", n);
        return 1;
    }
    int failures = 0;
    {
        /* the image held: one acquired, not yet presented (RhiStats.swapAcquired) */
        RhiStats st;
        rhi_get_stats(&st);
        if (st.swapImages == 0 || st.swapAcquired != 1) {
            printf("FAIL frame %d: %llu images, %llu acquired after the acquire\n", n,
                   (unsigned long long)st.swapImages, (unsigned long long)st.swapAcquired);
            failures++;
        }
    }
    RhiCommandList cl = rhi_begin_commands();
    RhiTextureBarrier b0 = {bb, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
    rhi_cmd_barrier(cl, &b0, 1);
    RhiRenderPassDesc rp = {0};
    rp.color[0] =
        (RhiColorAttachment){bb, RHI_LOAD_CLEAR, {0.0f, 0.0f, 0.0f, 1.0f}, RHI_STORE_STORE, false};
    rp.colorCount = 1;
    rp.width = sw;
    rp.height = sh;
    rhi_cmd_begin_render_pass(cl, &rp);
    rhi_cmd_end_render_pass(cl);
    RhiTextureBarrier b1 = {bb, RHI_STATE_RENDER_TARGET, RHI_STATE_PRESENT};
    rhi_cmd_barrier(cl, &b1, 1);
    rhi_submit(cl);
    rhi_present();
    return failures;
}

/* The submits of a present (RhiStats.submits; rhi_readback_texture's own
 * submit is not counted there).  A frame drawn and moved to PRESENT in one
 * list: that list's submit signals the semaphore the present waits on, so
 * one submit and one present.  frame(): the drawing list leaves the image
 * in COPY_SRC and a second list moves it to PRESENT, so two submits (one
 * per list) and no empty one before the present.  A present with no list
 * moving the image to PRESENT (the empty submit's path) is not drawn here:
 * the image would be presented in an undefined layout. */
static int submitCounts(SDL_Window *win, RhiFormat fmt)
{
    int failures = 0;
    RhiStats a, b;
    rhi_get_stats(&a);
    if (presentAtSwapSize(40)) {
        return 1;
    }
    rhi_get_stats(&b);
    if (b.submits - a.submits != 1 || b.presents - a.presents != 1) {
        printf("FAIL a presenting list: %llu submits and %llu presents, want 1 and 1\n",
               (unsigned long long)(b.submits - a.submits),
               (unsigned long long)(b.presents - a.presents));
        failures++;
    }
    rhi_get_stats(&a);
    failures += frame(win, fmt, 41);
    rhi_get_stats(&b);
    if (b.submits - a.submits != 2 || b.presents - a.presents != 1) {
        printf("FAIL a drawing list and a presenting list: %llu submits and %llu presents, want "
               "2 and 1\n",
               (unsigned long long)(b.submits - a.submits),
               (unsigned long long)(b.presents - a.presents));
        failures++;
    }
    return failures;
}

/* The window resized without rhi_resize_swapchain and no present
 * reporting it (an Android surface that changed size in the first seconds
 * while the driver returns VK_SUCCESS): with the poll on, the present
 * itself rebuilds the swapchain at the new size */
static int pollSize(SDL_Window *win, RhiFormat fmt)
{
    int failures = 0;
    uint32_t sw = 0, sh = 0;
    if (!rhi_swapchain_size(&sw, &sh)) {
        printf("FAIL poll: no swapchain\n");
        return 1;
    }
#ifndef __ANDROID__
    /* the desktop without the switch: no new swapchain on a plain present */
    {
        const uint32_t aw = sw, ah = sh;
        testSetEnv("ICO_VK_POLL_SURFACE", NULL);
        SDL_SetWindowSize(win, 80, 56);
        SDL_SyncWindow(win);
        const uint32_t n0 = vkr_test_swapchain_creations();
        failures += presentAtSwapSize(30);
        sw = sh = 0;
        if (vkr_test_swapchain_creations() != n0 || !rhi_swapchain_size(&sw, &sh) || sw != aw ||
            sh != ah) {
            printf("FAIL poll off: %u swapchains made, size %ux%u, want none at %ux%u\n",
                   vkr_test_swapchain_creations() - n0, sw, sh, aw, ah);
            failures++;
        }
    }
#endif
    testSetEnv("ICO_VK_POLL_SURFACE", "1");
    const uint32_t cw = 112, ch = 80;
    SDL_SetWindowSize(win, (int)cw, (int)ch);
    SDL_SyncWindow(win);
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    if ((uint32_t)pw != cw || (uint32_t)ph != ch) {
        printf("FAIL the window is %dx%d, asked %ux%u\n", pw, ph, cw, ch);
        testSetEnv("ICO_VK_POLL_SURFACE", NULL);
        return failures + 1;
    }
    /* one present on the old swapchain, reporting nothing: rebuilt by it */
    uint32_t n0 = vkr_test_swapchain_creations();
    failures += presentAtSwapSize(31);
    if (vkr_test_swapchain_creations() != n0 + 1) {
        printf("FAIL poll: %u swapchains made by the present, want 1\n",
               vkr_test_swapchain_creations() - n0);
        failures++;
    }
    sw = sh = 0;
    if (!rhi_swapchain_size(&sw, &sh) || sw != cw || sh != ch) {
        printf("FAIL poll: rhi_swapchain_size %ux%u, want %ux%u\n", sw, sh, cw, ch);
        testSetEnv("ICO_VK_POLL_SURFACE", NULL);
        return failures + 1; /* frame() would draw past the old images */
    }
    /* the next frames draw at the new size; nothing more is rebuilt */
    n0 = vkr_test_swapchain_creations();
    failures += frame(win, fmt, 32);
    failures += frame(win, fmt, 33);
    if (vkr_test_swapchain_creations() != n0) {
        printf("FAIL poll: %u swapchains made at an unchanged size\n",
               vkr_test_swapchain_creations() - n0);
        failures++;
    }
    testSetEnv("ICO_VK_POLL_SURFACE", NULL);
    return failures;
}

static int run(SDL_Window *win)
{
    RhiDeviceDesc dd = {win, true, true, "rhi_vk_swapchain_test", NULL};
    if (!rhi_init(&dd)) {
        printf("SKIP rhi_vk_swapchain_test: rhi_init with an offscreen window failed\n");
        return 77;
    }
    int failures = 0;
    RhiFormat fmt = rhi_swapchain_format();
    if (fmt != RHI_FMT_BGRA8_UNORM && fmt != RHI_FMT_RGBA8_UNORM) {
        printf("FAIL swapchain format %d\n", (int)fmt);
        failures++;
    }
    uint64_t idleBefore = 0, idleAfter = 0;
    for (int n = 0; n < 6 && !failures; n++) {
        RhiStats s0, s1;

        rhi_get_stats(&s0);
        if (n == 3) {
            /* resize as rd_present would on a window event */
            SDL_SetWindowSize(win, 128, 96);
            SDL_SyncWindow(win);
            if (!rhi_resize_swapchain(128, 96, true)) {
                printf("FAIL rhi_resize_swapchain\n");
                failures++;
                break;
            }
            /* counted for the window's pacing (RhiStats.swapchains: the
               first present on the new chain is the rebuild's) */
            rhi_get_stats(&s1);
            if (s1.swapchains != s0.swapchains + 1) {
                printf("FAIL the resize counted %llu swapchain(s), want 1\n",
                       (unsigned long long)(s1.swapchains - s0.swapchains));
                failures++;
                break;
            }
            /* the swapchain it replaced stays until the first present on the
               new one: the screen keeps its last picture through the rebuild
               (a frame rate change went black for the rebuild's length) */
            if (!vkr_test_retired_swapchain()) {
                printf("FAIL the replaced swapchain was not kept until the first present\n");
                failures++;
                break;
            }
            /* a second rebuild before any present (a frame-rate change that
               also changes the present mode): the swapchain on the screen is
               still the first one, so it stays held and the unshown middle
               one goes; the screen never shows the rebuild's gap */
            const uint64_t onScreen = vkr_test_swapchain_handle(true);
            const uint64_t middle = vkr_test_swapchain_handle(false);
            SDL_SetWindowSize(win, 120, 90);
            SDL_SyncWindow(win);
            if (!rhi_resize_swapchain(120, 90, true)) {
                printf("FAIL the second rhi_resize_swapchain\n");
                failures++;
                break;
            }
            rhi_get_stats(&s1);
            if (s1.swapchains != s0.swapchains + 2 || vkr_test_swapchain_handle(true) != onScreen ||
                vkr_test_swapchain_handle(false) == middle) {
                printf("FAIL two rebuilds before a present: %llu made, the held one %s the one on "
                       "the screen\n",
                       (unsigned long long)(s1.swapchains - s0.swapchains),
                       vkr_test_swapchain_handle(true) == onScreen ? "is" : "is not");
                failures++;
                break;
            }
        }
        failures += frame(win, fmt, n);
        rhi_get_stats(&s1);
        if (n == 3 && vkr_test_retired_swapchain()) {
            printf("FAIL the replaced swapchain is still held after a present\n");
            failures++;
        }
        /* the presents after the first on the new chain: no swapchain
           made, nothing held, and no wait for the device beyond what a
           frame before the rebuild took (the pump path stays cheap) */
        if (n == 1 || n == 2) {
            idleBefore += s1.waitIdles - s0.waitIdles;
        } else if (n == 4 || n == 5) {
            idleAfter += s1.waitIdles - s0.waitIdles;
            if (vkr_test_retired_swapchain() || s1.swapchains != s0.swapchains) {
                printf("FAIL frame %d after the rebuild: a swapchain made (%llu) or held (%u)\n", n,
                       (unsigned long long)(s1.swapchains - s0.swapchains),
                       vkr_test_retired_swapchain());
                failures++;
            }
        }
    }
    if (!failures && idleAfter > idleBefore) {
        printf("FAIL the two frames after the first present on the new swapchain waited idle %llu "
               "times, the two before the rebuild %llu\n",
               (unsigned long long)idleAfter, (unsigned long long)idleBefore);
        failures++;
    }
    if (!failures) {
        failures += submitCounts(win, fmt);
    }
    if (!failures) {
        failures += lifecycle(win, fmt);
    }
    if (!failures) {
        failures += followSize(win, fmt);
    }
    if (!failures) {
        failures += pollSize(win, fmt);
    }
    rhi_wait_idle();
    rhi_shutdown();
    if (rhi_vk_validation_error_count()) {
        printf("FAIL %u validation error(s)\n", rhi_vk_validation_error_count());
        failures++;
    }
    return failures ? 1 : 0;
}

/* The display's timing of presents' notes so far: in order of id, each
   after the last; *timed counts those with a time */
static int timingNotes(uint64_t *lastId, unsigned *notes, unsigned *timed)
{
    uint64_t id = 0, ns = 0;
    int r, failures = 0;

    while ((r = rhi_present_timing_next(&id, &ns)) == 1) {
        if (id <= *lastId) {
            printf("FAIL present timing: id %llu after %llu\n", (unsigned long long)id,
                   (unsigned long long)*lastId);
            failures++;
        }
        *lastId = id;
        ++*notes;
        *timed += ns != 0;
    }
    if (r < 0) {
        printf("FAIL present timing stopped\n");
        failures++;
    }
    return failures;
}

/* The display's timing of presents (rhi_present_timing) on the test's
   stand-in for vkWaitForPresentKHR (vkr_test_fake_present_wait; lavapipe's
   headless surface does not offer it): the thread notes the presents in
   order, with a time, through a resize and the surface's lifecycle
   (suboptimal, released and made again, lost), never waits on a swapchain
   that is made again or destroyed meanwhile, and stops at rhi_shutdown */
static int presentTiming(SDL_Window *win)
{
    RhiDeviceDesc dd = {win, true, true, "rhi_vk_swapchain_test", NULL};
    uint64_t lastId = 0;
    unsigned notes = 0, timed = 0;
    int failures = 0;

    SDL_SetWindowSize(win, 64, 48);
    SDL_SyncWindow(win);
    vkr_test_fake_present_wait(1);
    if (!rhi_init(&dd)) {
        printf("FAIL rhi_init with the present wait stand-in\n");
        vkr_test_fake_present_wait(0);
        return 1;
    }
    const RhiFormat fmt = rhi_swapchain_format();
    if (!rhi_present_timing()) {
        printf("FAIL present timing not in use with the stand-in\n");
        failures++;
    }
    /* not in use (rhi_present_timing_use, the window outside its vsync
       lock): presents carry no ids and nothing is noted */
    for (int n = 0; n < 3 && !failures; n++) {
        failures += frame(win, fmt, 20 + n);
    }
    SDL_Delay(10);
    {
        uint64_t id = 0, ns = 0;
        if (rhi_present_last_id() != 0 || rhi_present_timing_next(&id, &ns) != 0) {
            printf("FAIL presents carried ids with the timing not in use\n");
            failures++;
        }
    }
    rhi_present_timing_use(true);
    for (int n = 0; n < 8 && !failures; n++) {
        if (n == 4) {
            SDL_SetWindowSize(win, 96, 64);
            SDL_SyncWindow(win);
            if (!rhi_resize_swapchain(96, 64, true)) {
                printf("FAIL rhi_resize_swapchain\n");
                failures++;
            }
        }
        failures += frame(win, fmt, 30 + n);
        SDL_Delay(5);
        failures += timingNotes(&lastId, &notes, &timed);
    }
    if (!failures) {
        failures += lifecycle(win, fmt);
    }
    for (int n = 0; n < 4 && !failures; n++) {
        failures += frame(win, fmt, 40 + n);
    }
    /* the last present noted within 200 ms */
    for (int i = 0; i < 40 && lastId < rhi_present_last_id(); i++) {
        SDL_Delay(5);
        failures += timingNotes(&lastId, &notes, &timed);
    }
    printf("present timing: %u notes, %u with a time, the last %llu of %llu, %u waits misused\n",
           notes, timed, (unsigned long long)lastId, (unsigned long long)rhi_present_last_id(),
           vkr_test_present_wait_misuse());
    if (notes < 10 || timed * 2 < notes) {
        printf("FAIL present timing: %u notes, %u timed\n", notes, timed);
        failures++;
    }
    if (lastId != rhi_present_last_id()) {
        printf("FAIL the last present not noted\n");
        failures++;
    }
    rhi_wait_idle();
    rhi_shutdown(); /* the thread stops */
    vkr_test_fake_present_wait(0);
    if (vkr_test_present_wait_misuse() != 0) {
        printf("FAIL %u waits ended on another swapchain\n", vkr_test_present_wait_misuse());
        failures++;
    }
    if (rhi_vk_validation_error_count()) {
        printf("FAIL %u validation error(s)\n", rhi_vk_validation_error_count());
        failures++;
    }
    return failures;
}

/* A wait on a present that does not keep its timeout (a driver's, here the
   stand-in's: 2 s whatever the timeout): a swapchain made again and
   rhi_shutdown go on after 100 ms, the timing stopped, and frames are
   presented meanwhile, not held until the wait ends */
static int presentWaitHang(SDL_Window *win)
{
    RhiDeviceDesc dd = {win, true, true, "rhi_vk_swapchain_test", NULL};
    int failures = 0;

    SDL_SetWindowSize(win, 64, 48);
    SDL_SyncWindow(win);
    vkr_test_fake_present_wait(1);
    if (!rhi_init(&dd)) {
        printf("FAIL rhi_init with the present wait stand-in\n");
        vkr_test_fake_present_wait(0);
        return 1;
    }
    const RhiFormat fmt = rhi_swapchain_format();
    rhi_present_timing_use(true);
    failures += frame(win, fmt, 50);
    vkr_test_fake_present_wait_hang(2000);
    failures += frame(win, fmt, 51); /* the thread now waits on it */
    SDL_Delay(20);
    uint64_t t0 = SDL_GetTicksNS();
    SDL_SetWindowSize(win, 96, 64);
    SDL_SyncWindow(win);
    if (!rhi_resize_swapchain(96, 64, true)) {
        printf("FAIL rhi_resize_swapchain\n");
        failures++;
    }
    const double resizeMs = (double)(SDL_GetTicksNS() - t0) / 1e6;
    if (resizeMs > 1000.0) {
        printf("FAIL a swapchain made again waited %.0f ms for a stuck wait\n", resizeMs);
        failures++;
    }
    if (rhi_present_timing()) {
        printf("FAIL the timing not stopped after a stuck wait\n");
        failures++;
    }
    for (int n = 0; n < 3; n++) {
        failures += frame(win, fmt, 52 + n);
    }
    rhi_wait_idle();
    t0 = SDL_GetTicksNS();
    rhi_shutdown(); /* the thread is still in its 2 s wait: left */
    const double shutMs = (double)(SDL_GetTicksNS() - t0) / 1e6;
    if (shutMs > 1000.0) {
        printf("FAIL rhi_shutdown waited %.0f ms for a stuck wait\n", shutMs);
        failures++;
    }
    printf("present timing, a stuck wait: swapchain made again in %.0f ms, shutdown in %.0f ms\n",
           resizeMs, shutMs);
    vkr_test_fake_present_wait(0);
    vkr_test_fake_present_wait_hang(0);
    return failures;
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
    if (rc == 0 && presentTiming(win) != 0) {
        rc = 1;
    }
    if (rc == 0 && presentWaitHang(win) != 0) {
        rc = 1;
    }
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (rc == 0) {
        printf("rhi_vk_swapchain_test: all checks passed\n");
    }
    return rc;
}
