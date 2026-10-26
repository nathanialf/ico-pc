/* rhi_gl_test.c: the exact-texel RHI checks (rhi_test_common.c) on the
 * OpenGL backend, headless (a hidden window of the backend's own on SDL's
 * offscreen driver), with the debug output on: every error or
 * undefined-behaviour message the driver reports (KHR_debug) fails the run.
 * Then the copies the cells do not make (copyCell: a colour region into a
 * device buffer the vertex stage reads and on into a readback buffer, the
 * depth aspect of a D32F_S8 target as floats, an RGBA32_UINT target as
 * integers, each at a row pitch wider than the region; and a pass on a
 * texture made after one whose framebuffer was dropped), the busy clock
 * (busyCell: rhi_read_timestamps_busy against the timestamps), and the window
 * path (swapchainPass: six frames on an SDL offscreen window, cleared,
 * read back and presented, with a resize).
 *
 * Exit 0 on success, 1 on a mismatch or a debug output error, 77 (skipped)
 * when no OpenGL context can be made.  ctest runs it with
 * LIBGL_ALWAYS_SOFTWARE=1 and SDL_VIDEO_DRIVER=offscreen (Mesa's llvmpipe
 * in the container), and again with ICO_GL_FAKE_NO_DUAL=1 (rhi_gl_nodual). */
#include "gl/rhi_gl.h"
#include "rhi.h"
#include "rhi_test_common.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* A pass that only clears the target (and its depth), into its state for
 * a copy. */
static void clearPass(RhiCommandList cl, RhiTexture colour, const float rgba[4], RhiTexture depth,
                      float z, uint32_t w, uint32_t h)
{
    RhiTextureBarrier to[2] = {{colour, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
                               {depth, RHI_STATE_UNDEFINED, RHI_STATE_DEPTH_WRITE}};
    rhi_cmd_barrier(cl, colour.id ? to : &to[1], colour.id && depth.id ? 2u : 1u);
    RhiRenderPassDesc rp;
    memset(&rp, 0, sizeof(rp));
    if (colour.id) {
        rp.color[0].texture = colour;
        rp.color[0].load = RHI_LOAD_CLEAR;
        memcpy(rp.color[0].clear, rgba, sizeof(rp.color[0].clear));
        rp.colorCount = 1;
    }
    if (depth.id) {
        rp.depth.texture = depth;
        rp.depth.depthLoad = RHI_LOAD_CLEAR;
        rp.depth.stencilLoad = RHI_LOAD_CLEAR;
        rp.depth.clearDepth = z;
    }
    rp.width = w;
    rp.height = h;
    rhi_cmd_begin_render_pass(cl, &rp);
    rhi_cmd_end_render_pass(cl);
    RhiTextureBarrier back[2] = {{colour, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                 {depth, RHI_STATE_DEPTH_WRITE, RHI_STATE_COPY_SRC}};
    rhi_cmd_barrier(cl, colour.id ? back : &back[1], colour.id && depth.id ? 2u : 1u);
}

static int copyCell(void)
{
    enum { CW = 8, CH = 4, PITCH = 64 };

    if (!rhi_create_backend("opengl")) {
        return 0;
    }
    RhiDeviceDesc dd = {NULL, false, true, "rhi_gl_test copies"};
    if (!rhi_init(&dd)) {
        return 0;
    }
    int failures = 0;
    const float rgba[4] = {10.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 40.0f / 255.0f};
    const float none[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float words[4] = {1.0f, 70000.0f, 3.0f, 4000000.0f};
    const RhiTexture noTex = {0};
    RhiTexture col = rhi_create_texture(&(RhiTextureDesc){CW, CH, 1, RHI_FMT_RGBA8_UNORM,
                                                          RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                                                          "copy colour", 0});
    RhiTexture dep = rhi_create_texture(&(RhiTextureDesc){
        CW, CH, 1, RHI_FMT_D32F_S8, RHI_TEX_DEPTH_STENCIL | RHI_TEX_COPY_SRC, "copy depth", 0});
    RhiTexture u32 = rhi_create_texture(&(RhiTextureDesc){
        CW, CH, 1, RHI_FMT_RGBA32_UINT, RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC, "copy words", 0});
    RhiBuffer dev = rhi_create_buffer(&(RhiBufferDesc){
        4096, RHI_BUF_VERTEX | RHI_BUF_COPY_SRC | RHI_BUF_COPY_DST, RHI_MEM_DEVICE, "copy device"});
    RhiBuffer rb = rhi_create_buffer(&(RhiBufferDesc){8192, RHI_BUF_READBACK | RHI_BUF_COPY_DST,
                                                      RHI_MEM_READBACK, "copy readback"});
    const uint8_t *map = rb.id ? rhi_map_buffer(rb) : NULL;
    if (!col.id || !dep.id || !u32.id || !dev.id || !map) {
        rhi_test_log("FAIL copy cell: resources\n");
        rhi_shutdown();
        return 1;
    }
    RhiStats s0, s1;
    rhi_get_stats(&s0);
    rhi_wait_frame();
    RhiCommandList cl = rhi_begin_commands();
    clearPass(cl, col, rgba, noTex, 0.0f, CW, CH);
    clearPass(cl, noTex, none, dep, 0.375f, CW, CH);
    clearPass(cl, u32, words, noTex, 0.0f, CW, CH);
    /* the colour's 4 x 2 region at (2, 1) into the device buffer at 16, rows
     * 64 bytes apart, then on into the readback buffer */
    rhi_cmd_copy_texture_to_buffer(cl, col, RHI_ASPECT_COLOR, (RhiRect){2, 1, 4, 2}, dev, 16,
                                   PITCH);
    rhi_cmd_copy_buffer(cl, dev, 0, rb, 0, 16 + 2 * PITCH);
    /* the whole depth as floats at 1024; the words at 2048 */
    rhi_cmd_copy_texture_to_buffer(cl, dep, RHI_ASPECT_DEPTH, (RhiRect){0, 0, CW, CH}, rb, 1024,
                                   CW * 4 + 16);
    rhi_cmd_copy_texture_to_buffer(cl, u32, RHI_ASPECT_COLOR, (RhiRect){0, 0, CW, CH}, rb, 2048,
                                   CW * 16 + 32);
    rhi_end_commands(cl);
    rhi_submit(cl);
    rhi_wait_idle();
    rhi_get_stats(&s1);

    int bad = 0;
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 4; x++) {
            const uint8_t *p = map + 16 + y * PITCH + x * 4;
            bad += p[0] != 10 || p[1] != 20 || p[2] != 30 || p[3] != 40;
        }
    }
    if (bad) {
        rhi_test_log("FAIL copy cell: %d colour texels wrong\n", bad);
        failures++;
    }
    bad = 0;
    for (int y = 0; y < CH; y++) {
        for (int x = 0; x < CW; x++) {
            float z;
            memcpy(&z, map + 1024 + y * (CW * 4 + 16) + x * 4, 4);
            bad += z != 0.375f;
        }
    }
    if (bad) {
        rhi_test_log("FAIL copy cell: %d depth values wrong\n", bad);
        failures++;
    }
    bad = 0;
    for (int y = 0; y < CH; y++) {
        for (int x = 0; x < CW; x++) {
            uint32_t w[4];
            memcpy(w, map + 2048 + y * (CW * 16 + 32) + x * 16, 16);
            bad += w[0] != 1u || w[1] != 70000u || w[2] != 3u || w[3] != 4000000u;
        }
    }
    if (bad) {
        rhi_test_log("FAIL copy cell: %d RGBA32_UINT texels wrong\n", bad);
        failures++;
    }
    if (s1.copies - s0.copies != 4 || s1.renderPasses - s0.renderPasses != 3) {
        rhi_test_log("FAIL copy cell: %llu copies and %llu passes counted (want 4 and 3)\n",
                     (unsigned long long)(s1.copies - s0.copies),
                     (unsigned long long)(s1.renderPasses - s0.renderPasses));
        failures++;
    }

    /* the colour target's framebuffer goes with it at the slot's recycle;
     * a texture made after (which may get its name) clears and reads back */
    rhi_destroy_texture(col);
    rhi_wait_frame();
    rhi_wait_frame();
    RhiTexture col2 = rhi_create_texture(&(RhiTextureDesc){CW, CH, 1, RHI_FMT_RGBA8_UNORM,
                                                           RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                                                           "copy colour 2", 0});
    const float again[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    cl = rhi_begin_commands();
    clearPass(cl, col2, again, noTex, 0.0f, CW, CH);
    rhi_end_commands(cl);
    rhi_submit(cl);
    uint8_t px[CW * CH * 4];
    uint32_t pitch = 0;
    memset(px, 0, sizeof(px));
    if (!col2.id || !rhi_readback_texture(col2, RHI_ASPECT_COLOR, px, sizeof(px), &pitch) ||
        px[0] != 255 || px[1] != 0 || px[2] != 255 || px[3] != 255) {
        rhi_test_log("FAIL copy cell: a target made after a destroyed one (got %u %u %u %u)\n",
                     px[0], px[1], px[2], px[3]);
        failures++;
    }
    rhi_destroy_texture(col2);
    rhi_destroy_texture(dep);
    rhi_destroy_texture(u32);
    rhi_destroy_buffer(dev);
    rhi_destroy_buffer(rb);
    rhi_wait_idle();
    rhi_shutdown();
    if (rhi_gl_debug_error_count()) {
        rhi_test_log("FAIL copy cell: %u debug output error(s)\n", rhi_gl_debug_error_count());
        failures++;
    }
    if (!failures) {
        rhi_test_log("rhi_gl_test: copy cell passed (colour region, depth, RGBA32_UINT, a "
                     "framebuffer dropped)\n");
    }
    return failures;
}

/* The busy clock (rhi.h rhi_read_timestamps_busy): stamps 0, 1 and 3 of
 * one frame around two clears, written with RHI_TIMESTAMP_ISSUE_TIME, read
 * when the slot comes round again; the clock starts at stamp 0's time,
 * reads 0 for stamp 2 (not written), never runs backwards and never
 * advances more than the timestamps between two stamps.  Then a frame with
 * one stamp written without the flag: no clock (0 read), the timestamps
 * kept.  0 passed (or no timestamps: skipped), 1 failed. */
static int busyCell(void)
{
    enum { BW = 16, BH = 16 };

    if (!rhi_create_backend("opengl")) {
        return 0;
    }
    RhiDeviceDesc dd = {NULL, false, true, "rhi_gl_test busy clock"};
    if (!rhi_init(&dd)) {
        return 0;
    }
    if (!rhi_timestamps_supported()) {
        rhi_test_log("rhi_gl_test: busy cell skipped (no GPU timestamps)\n");
        rhi_shutdown();
        return 0;
    }
    int failures = 0;
    const float rgba[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    const RhiTexture noTex = {0};
    RhiTexture col = rhi_create_texture(&(RhiTextureDesc){BW, BH, 1, RHI_FMT_RGBA8_UNORM,
                                                          RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                                                          "busy colour", 0});
    if (!col.id) {
        rhi_test_log("FAIL busy cell: resources\n");
        rhi_shutdown();
        return 1;
    }
    rhi_wait_frame();
    RhiCommandList cl = rhi_begin_commands();
    rhi_cmd_write_timestamp(cl, 0 | RHI_TIMESTAMP_ISSUE_TIME);
    clearPass(cl, col, rgba, noTex, 0.0f, BW, BH);
    rhi_cmd_write_timestamp(cl, 1 | RHI_TIMESTAMP_ISSUE_TIME);
    clearPass(cl, col, rgba, noTex, 0.0f, BW, BH);
    rhi_cmd_write_timestamp(cl, 3 | RHI_TIMESTAMP_ISSUE_TIME | RHI_TIMESTAMP_KEEP_BARRIERS);
    rhi_end_commands(cl);
    rhi_submit(cl);
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        rhi_wait_frame(); /* the last one recycles the slot written above */
    }
    uint64_t ts[8], busy[8];
    memset(ts, 0, sizeof(ts));
    memset(busy, 0, sizeof(busy));
    const uint32_t n = rhi_read_timestamps(ts, 8);
    const uint32_t nb = rhi_read_timestamps_busy(busy, 8);
    if (n != 4 || nb != 4 || ts[0] == 0 || ts[1] < ts[0] || ts[3] < ts[1]) {
        rhi_test_log("FAIL busy cell: %u stamps and %u busy values read (want 4 and 4)\n", n, nb);
        failures++;
    } else if (busy[0] != ts[0] || busy[2] != 0 || busy[1] < busy[0] || busy[3] < busy[1] ||
               busy[1] - busy[0] > ts[1] - ts[0] || busy[3] - busy[1] > ts[3] - ts[1]) {
        rhi_test_log("FAIL busy cell: busy %llu %llu %llu %llu against stamps %llu %llu %llu\n",
                     (unsigned long long)busy[0], (unsigned long long)busy[1],
                     (unsigned long long)busy[2], (unsigned long long)busy[3],
                     (unsigned long long)ts[0], (unsigned long long)ts[1],
                     (unsigned long long)ts[3]);
        failures++;
    } else {
        rhi_test_log("rhi_gl_test: busy cell: span %llu ns, busy %llu ns\n",
                     (unsigned long long)(ts[3] - ts[0]), (unsigned long long)(busy[3] - busy[0]));
    }
    /* a stamp without the flag: no busy clock for the slot */
    cl = rhi_begin_commands();
    rhi_cmd_write_timestamp(cl, 0 | RHI_TIMESTAMP_ISSUE_TIME);
    clearPass(cl, col, rgba, noTex, 0.0f, BW, BH);
    rhi_cmd_write_timestamp(cl, 1);
    rhi_end_commands(cl);
    rhi_submit(cl);
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        rhi_wait_frame();
    }
    const uint32_t n2 = rhi_read_timestamps(ts, 8);
    const uint32_t nb2 = rhi_read_timestamps_busy(busy, 8);
    if (n2 != 2 || nb2 != 0) {
        rhi_test_log("FAIL busy cell: a stamp without the issue time: %u stamps and %u busy "
                     "values read (want 2 and 0)\n",
                     n2, nb2);
        failures++;
    }
    rhi_destroy_texture(col);
    rhi_wait_idle();
    rhi_shutdown();
    if (rhi_gl_debug_error_count()) {
        rhi_test_log("FAIL busy cell: %u debug output error(s)\n", rhi_gl_debug_error_count());
        failures++;
    }
    if (!failures) {
        rhi_test_log("rhi_gl_test: busy cell passed\n");
    }
    return failures;
}

/* One frame on the backbuffer at w x h: clear to a colour from n, read it
 * back (RGBA8: rhi_swapchain_format), present.  0, or 1 with the reason. */
static int swapFrame(int n, uint32_t w, uint32_t h)
{
    static uint8_t px[128 * 96 * 4];
    rhi_wait_frame();
    RhiTexture bb = rhi_acquire_backbuffer();
    if (!bb.id) {
        rhi_test_log("FAIL swapchain frame %d: no backbuffer\n", n);
        return 1;
    }
    const uint8_t r = (uint8_t)(32 * n + 16), g = 0x80, b = 0xF0;
    const float rgba[4] = {(float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f};
    const RhiTexture noTex = {0};
    RhiCommandList cl = rhi_begin_commands();
    clearPass(cl, bb, rgba, noTex, 0.0f, w, h);
    rhi_submit(cl);
    int failures = 0;
    uint32_t pitch = 0;
    if ((size_t)w * h * 4 > sizeof(px) ||
        !rhi_readback_texture(bb, RHI_ASPECT_COLOR, px, sizeof(px), &pitch) || pitch != w * 4) {
        rhi_test_log("FAIL swapchain frame %d: readback (pitch %u)\n", n, pitch);
        failures++;
    } else {
        const uint8_t *p = px + (size_t)(h / 2) * pitch + (size_t)(w / 2) * 4;
        if (p[0] != r || p[1] != g || p[2] != b || p[3] != 255) {
            rhi_test_log("FAIL swapchain frame %d: got %u %u %u %u, expected %u %u %u 255\n", n,
                         p[0], p[1], p[2], p[3], r, g, b);
            failures++;
        }
    }
    RhiCommandList cl2 = rhi_begin_commands();
    RhiTextureBarrier toPresent = {bb, RHI_STATE_COPY_SRC, RHI_STATE_PRESENT};
    rhi_cmd_barrier(cl2, &toPresent, 1);
    rhi_submit(cl2);
    rhi_present();
    return failures;
}

/* The window path on an SDL offscreen window: 0 passed, 1 failed, 77 no
 * OpenGL window. */
static int swapchainPass(void)
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        rhi_test_log("SKIP rhi_gl_test swapchain: SDL_Init: %s\n", SDL_GetError());
        return 77;
    }
    SDL_Window *win = SDL_CreateWindow("rhi_gl_test", 64, 48, SDL_WINDOW_OPENGL);
    if (!win) {
        rhi_test_log("SKIP rhi_gl_test swapchain: no OpenGL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }
    int failures = 0;
    RhiDeviceDesc dd = {win, true, true, "rhi_gl_test swapchain"};
    if (!rhi_create_backend("opengl") || !rhi_init(&dd)) {
        rhi_test_log("FAIL swapchain: rhi_init with a window failed (see above)\n");
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    if (rhi_swapchain_format() != RHI_FMT_RGBA8_UNORM) {
        rhi_test_log("FAIL swapchain format %d\n", (int)rhi_swapchain_format());
        failures++;
    }
    RhiStats s0, s1;
    rhi_get_stats(&s0);
    for (int n = 0; n < 6 && !failures; n++) {
        uint32_t w = n < 3 ? 64u : 96u, h = n < 3 ? 48u : 64u;
        if (n == 3) {
            SDL_SetWindowSize(win, (int)w, (int)h);
            SDL_SyncWindow(win);
            if (!rhi_resize_swapchain(w, h, true)) {
                rhi_test_log("FAIL rhi_resize_swapchain\n");
                failures++;
                break;
            }
        }
        uint32_t sw = 0, sh = 0;
        if (!rhi_swapchain_size(&sw, &sh) || sw != w || sh != h) {
            rhi_test_log("FAIL swapchain frame %d: size %ux%u, expected %ux%u\n", n, sw, sh, w, h);
            failures++;
            break;
        }
        failures += swapFrame(n, w, h);
    }
    rhi_get_stats(&s1);
    const uint64_t presents = s1.presents - s0.presents;
    rhi_test_log("rhi_gl_test swapchain: %llu presents, mode %s, last shown %d\n",
                 (unsigned long long)presents, rhi_present_mode_name(), (int)rhi_present_shown());
    if (!failures && presents != 6) {
        rhi_test_log("FAIL swapchain: %llu presents counted (want 6)\n",
                     (unsigned long long)presents);
        failures++;
    }
    rhi_wait_idle();
    rhi_shutdown();
    if (rhi_gl_debug_error_count()) {
        rhi_test_log("FAIL swapchain: %u debug output error(s)\n", rhi_gl_debug_error_count());
        failures++;
    }
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (!failures) {
        rhi_test_log("rhi_gl_test swapchain: all checks passed\n");
    }
    return failures ? 1 : 0;
}

int main(void)
{
    const RhiTestConfig cfg = {"opengl", "rhi_gl_test", true, NULL, rhi_gl_debug_error_count};
    const int rc = rhi_test_run_cells(&cfg);
    if (rc == 77) {
        rhi_test_log("SKIP rhi_gl_test: no OpenGL context\n");
        return 77;
    }
    int failures = rc != 0;
    failures += copyCell();
    failures += busyCell();
    const int swap = swapchainPass();
    failures += swap == 1;
    if (failures) {
        rhi_test_log("rhi_gl_test: FAILED\n");
        return 1;
    }
    rhi_test_log("rhi_gl_test: all checks passed%s\n",
                 swap == 77 ? " (the swapchain skipped: no OpenGL window)" : "");
    return 0;
}
