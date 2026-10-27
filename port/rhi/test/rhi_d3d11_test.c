/* rhi_d3d11_test.c: the exact-texel RHI checks (rhi_test_common.c) on the
 * Direct3D 11 backend, for Windows (issue 126; built, not run, in v0.8.6:
 * the backend has not run on any Windows machine).  Built as a GUI program:
 * double-clicked it shows no console and needs no flags.  It writes
 * rhi_d3d11_test.log beside itself and ends with a message box giving the
 * verdict.
 *
 *   1. the cells on WARP, the software adapter every Windows has (the
 *      reference run: it does not depend on a GPU driver)
 *  1b. the cells on WARP again with ICO_D3D11_FAKE_NO_PARTIAL_CB=1: the
 *      upload buffers' constant buffers mapped, the path of the devices
 *      without partial constant buffer updates
 *  1c. the written mark (rhi_buffer_note_written) on WARP: a submit writes
 *      the bytes its lists read under the mark and no more
 *   2. the cells on the default hardware adapter (skipped when there is
 *      none: the default is then WARP again)
 *   3. the swapchain on a hidden SDL window: six frames of clear, readback
 *      and present, with a resize (hardware adapter, or WARP)
 *   4. for comparison, the same cells on Vulkan when a Vulkan driver is
 *      installed (a skip is not a failure)
 *
 * The cells' shaders are compiled at run time from rhi_test.hlsl
 * (rhi_test_hlsl.h, d3dcompiler_47.dll); the destination-read cell is
 * skipped by the limits (RhiLimits.rov false on this backend).  The D3D11
 * debug layer is requested for every run; it is present when the Windows
 * optional feature "Graphics Tools" is installed, and then every
 * debug-layer error fails the run.
 *
 * Developer switch: --console prints to the console instead of the log and
 * shows no message box (ctest uses it).  Exit 0 all passed, 1 a failure,
 * 77 nothing could run. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "d3d11/rhi_d3d11.h"
#include "rhi.h"
#include "rhi_test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RHI_HAVE_SDL
#include <SDL3/SDL.h>
#endif

static bool s_debugActive;
static bool s_wantWarp;
static char s_adapter[128];

/* After rhi_init: the run is on the adapter it asked for. */
static bool acceptWarp(void)
{
    s_debugActive = rhi_d3d11_debug_layer_active();
    snprintf(s_adapter, sizeof(s_adapter), "%s", rhi_adapter_name());
    rhi_test_log("  adapter %s, debug layer %s\n", s_adapter,
                 s_debugActive ? "on" : "not installed (Graphics Tools optional feature)");
    if (s_wantWarp != rhi_d3d11_is_warp()) {
        rhi_test_log("  %s\n", s_wantWarp ? "not WARP: skipped"
                                          : "no hardware adapter (default is WARP): skipped");
        return false;
    }
    return true;
}

static const char *verdict(int rc)
{
    return rc == 0 ? "pass" : rc == 77 ? "skipped" : "FAIL";
}

/* ---------------------------------------------------------------- swapchain */
#ifdef ICO_RHI_HAVE_SDL
static int runSwapchain(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        rhi_test_log("SKIP swapchain: SDL_Init: %s\n", SDL_GetError());
        return 77;
    }
    SDL_Window *win = SDL_CreateWindow("rhi_d3d11_test", 64, 48, SDL_WINDOW_HIDDEN);
    if (!win) {
        rhi_test_log("SKIP swapchain: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }
    int failures = 0;
    RhiDeviceDesc dd = {win, true, true, "rhi_d3d11_test swapchain"};
    if (!rhi_create_backend("d3d11") || !rhi_init(&dd)) {
        rhi_test_log("FAIL swapchain: rhi_init with a window failed (see above)\n");
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    rhi_test_log("swapchain: adapter %s, format %d\n", rhi_adapter_name(),
                 (int)rhi_swapchain_format());
    RhiFormat fmt = rhi_swapchain_format();
    if (fmt != RHI_FMT_BGRA8_UNORM) {
        rhi_test_log("FAIL swapchain format %d\n", (int)fmt);
        failures++;
    }
    static uint8_t px[128 * 96 * 4];
    for (int frame = 0; frame < 6 && !failures; frame++) {
        uint32_t w = frame < 3 ? 64u : 128u, h = frame < 3 ? 48u : 96u;
        if (frame == 3) {
            SDL_SetWindowSize(win, (int)w, (int)h);
            SDL_SyncWindow(win);
            if (!rhi_resize_swapchain(w, h, true)) {
                rhi_test_log("FAIL rhi_resize_swapchain\n");
                failures++;
                break;
            }
        }
        rhi_wait_frame();
        RhiTexture bb = rhi_acquire_backbuffer();
        if (!bb.id) {
            rhi_test_log("FAIL frame %d: no backbuffer\n", frame);
            failures++;
            break;
        }
        RhiCommandList cl = rhi_begin_commands();
        RhiTextureBarrier b0 = {bb, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
        rhi_cmd_barrier(cl, &b0, 1);
        uint8_t r = (uint8_t)(32 * frame + 16), g = 0x80, bl = 0xF0;
        RhiRenderPassDesc rp;
        memset(&rp, 0, sizeof(rp));
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
            rhi_test_log("FAIL frame %d: readback (pitch %u)\n", frame, pitch);
            failures++;
            break;
        }
        const uint8_t *p = px + (size_t)(h / 2) * pitch + (size_t)(w / 2) * 4;
        const uint8_t want[4] = {bl, g, r, 255}; /* BGRA */
        if (memcmp(p, want, 4) != 0) {
            rhi_test_log("FAIL frame %d: got %u %u %u %u, expected %u %u %u %u\n", frame, p[0],
                         p[1], p[2], p[3], want[0], want[1], want[2], want[3]);
            failures++;
        }
        RhiCommandList cl2 = rhi_begin_commands();
        RhiTextureBarrier b2 = {bb, RHI_STATE_COPY_SRC, RHI_STATE_PRESENT};
        rhi_cmd_barrier(cl2, &b2, 1);
        rhi_submit(cl2);
        rhi_present();
    }
    bool debugOn = rhi_d3d11_debug_layer_active();
    rhi_wait_idle();
    rhi_shutdown();
    if (rhi_d3d11_debug_error_count()) {
        rhi_test_log("FAIL %u debug layer error(s)\n", rhi_d3d11_debug_error_count());
        failures++;
    }
    SDL_DestroyWindow(win);
    SDL_Quit();
    rhi_test_log("swapchain: %s (debug layer %s)\n", failures ? "FAIL" : "all checks passed",
                 debugOn ? "on" : "off");
    return failures ? 1 : 0;
}
#endif

/* ----------------------------------------------------------- written mark */
/* One frame: the ring's bytes [0, 8192) a seeded pattern, the marks noted
 * in turn, a copy of [0, 8192) into a readback buffer.  The submit must
 * write exactly the last mark's bytes (RhiStats.uploadWriteBytes: the copy
 * reads the buffer's vertex part only) and the copy's first `mark` bytes
 * must be the pattern. */
static int markFrame(RhiBuffer up, RhiBuffer rb, const uint64_t *marks, uint32_t n, uint8_t seed,
                     uint64_t want)
{
    enum { SPAN = 8192 };

    rhi_wait_frame();
    uint8_t *m = rhi_map_buffer(up);
    if (!m) {
        rhi_test_log("FAIL written mark: the upload buffer did not map\n");
        return 1;
    }
    for (uint32_t i = 0; i < SPAN; i++) {
        m[i] = (uint8_t)(i * 7u + seed);
    }
    rhi_unmap_buffer(up);
    for (uint32_t i = 0; i < n; i++) {
        rhi_buffer_note_written(up, marks[i]);
    }
    RhiStats s0, s1;
    rhi_get_stats(&s0);
    RhiCommandList cl = rhi_begin_commands();
    rhi_cmd_copy_buffer(cl, up, 0, rb, 0, SPAN);
    rhi_submit(cl);
    rhi_get_stats(&s1);
    rhi_wait_idle();
    int failures = 0;
    const uint64_t wrote = s1.uploadWriteBytes - s0.uploadWriteBytes;
    if (wrote != want) {
        rhi_test_log("FAIL written mark (seed %u): the submit wrote %llu bytes, expected %llu\n",
                     seed, (unsigned long long)wrote, (unsigned long long)want);
        failures++;
    }
    const uint8_t *r = rhi_map_buffer(rb);
    for (uint64_t i = 0; r && i < want && i < SPAN; i++) {
        if (r[i] != (uint8_t)(i * 7u + seed)) {
            rhi_test_log("FAIL written mark (seed %u): byte %llu read %u, expected %u\n", seed,
                         (unsigned long long)i, r[i], (uint8_t)(i * 7u + seed));
            failures++;
            break;
        }
    }
    if (!r) {
        rhi_test_log("FAIL written mark: the readback buffer did not map\n");
        failures++;
    }
    return failures;
}

static int runWrittenMark(void)
{
    if (!rhi_create_backend("d3d11")) {
        rhi_test_log("SKIP written mark: no Direct3D 11 backend in this build\n");
        return 77;
    }
    RhiDeviceDesc dd = {NULL, false, true, "rhi_d3d11_test written mark"};
    if (!rhi_init(&dd)) {
        rhi_test_log("SKIP written mark: no usable device (see messages above)\n");
        return 77;
    }
    if (!acceptWarp()) {
        rhi_shutdown();
        return 77;
    }
    int failures = 0;
    /* the renderer's ring's usage (port/render/rd_upload.c), and a buffer
     * never noted */
    RhiBuffer up = rhi_create_buffer(&(RhiBufferDesc){
        65536,
        RHI_BUF_VERTEX | RHI_BUF_INDEX | RHI_BUF_UNIFORM | RHI_BUF_STORAGE_READ | RHI_BUF_COPY_SRC,
        RHI_MEM_UPLOAD, "mark ring"});
    RhiBuffer plain = rhi_create_buffer(
        &(RhiBufferDesc){65536, RHI_BUF_VERTEX | RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "no mark"});
    RhiBuffer rb = rhi_create_buffer(
        &(RhiBufferDesc){65536, RHI_BUF_COPY_DST, RHI_MEM_READBACK, "mark readback"});
    if (!up.id || !plain.id || !rb.id) {
        rhi_test_log("FAIL written mark: buffers not created\n");
        failures++;
    } else {
        /* a mark under the copy: only the mark's bytes */
        const uint64_t a[] = {4096};
        failures += markFrame(up, rb, a, 1, 1, 4096);
        /* the next frame starts the mark again, lower than the last */
        const uint64_t b[] = {1024};
        failures += markFrame(up, rb, b, 1, 2, 1024);
        /* within a frame the mark only rises; an unaligned mark is written
         * to its 16-byte end */
        const uint64_t c[] = {512, 2050, 1000};
        failures += markFrame(up, rb, c, 3, 3, 2064);
        /* never noted: the whole range read */
        failures += markFrame(plain, rb, NULL, 0, 4, 8192);
    }
    rhi_destroy_buffer(up);
    rhi_destroy_buffer(plain);
    rhi_destroy_buffer(rb);
    rhi_wait_idle();
    rhi_shutdown();
    if (rhi_d3d11_debug_error_count()) {
        rhi_test_log("FAIL %u debug layer error(s)\n", rhi_d3d11_debug_error_count());
        failures++;
    }
    rhi_test_log("written mark: %s\n", failures ? "FAIL" : "all checks passed");
    return failures ? 1 : 0;
}

/* ------------------------------------------------------------------- main */
static void logPath(char *out, size_t size)
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    if (n == 0 || n >= sizeof(exe)) {
        snprintf(out, size, "rhi_d3d11_test.log");
        return;
    }
    char *slash = strrchr(exe, '\\');
    if (slash) {
        slash[1] = '\0';
    } else {
        exe[0] = '\0';
    }
    snprintf(out, size, "%srhi_d3d11_test.log", exe);
}

int main(int argc, char **argv)
{
    bool console = false;
    for (int i = 1; i < argc; i++) {
        console = console || strcmp(argv[i], "--console") == 0;
    }
    char path[MAX_PATH + 32];
    logPath(path, sizeof(path));
    if (!console) {
        /* the backend's messages (stderr) and ours (stdout) into one log */
        if (!freopen(path, "w", stdout)) {
            MessageBoxA(NULL, path, "rhi_d3d11_test: cannot write the log", MB_OK | MB_ICONERROR);
            return 1;
        }
        setvbuf(stdout, NULL, _IONBF, 0);
        if (!freopen(path, "a", stderr)) {
            /* keep going: only the backend's own lines are lost */
        }
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    rhi_test_log("rhi_d3d11_test: backends in this build:");
    for (uint32_t i = 0; rhi_backend_name(i); i++) {
        rhi_test_log(" %s", rhi_backend_name(i));
    }
    rhi_test_log("\n\n");

    /* 1: WARP */
    rhi_test_log("== 1. Direct3D 11 on WARP\n");
    _putenv("ICO_D3D11_ADAPTER=warp");
    s_wantWarp = true;
    const RhiTestConfig warp = {"d3d11", "rhi_d3d11_test (WARP)", true, acceptWarp,
                                rhi_d3d11_debug_error_count};
    int rcWarp = rhi_test_run_cells(&warp);
    char warpAdapter[128];
    snprintf(warpAdapter, sizeof(warpAdapter), "%s", s_adapter);

    /* 1b: WARP with the upload buffers' constant buffers mapped */
    rhi_test_log("\n== 1b. Direct3D 11 on WARP, constant buffers mapped "
                 "(ICO_D3D11_FAKE_NO_PARTIAL_CB=1)\n");
    _putenv("ICO_D3D11_FAKE_NO_PARTIAL_CB=1");
    const RhiTestConfig warpMapped = {"d3d11", "rhi_d3d11_test (WARP, mapped constants)", true,
                                      acceptWarp, rhi_d3d11_debug_error_count};
    int rcMapped = rhi_test_run_cells(&warpMapped);
    _putenv("ICO_D3D11_FAKE_NO_PARTIAL_CB=");

    /* 1c: the written mark */
    rhi_test_log("\n== 1c. Direct3D 11 on WARP: the upload ring's written mark\n");
    int rcMark = runWrittenMark();

    /* 2: the hardware adapter */
    rhi_test_log("\n== 2. Direct3D 11 on the default hardware adapter\n");
    _putenv("ICO_D3D11_ADAPTER=");
    s_wantWarp = false;
    s_adapter[0] = '\0';
    const RhiTestConfig hw = {"d3d11", "rhi_d3d11_test (hardware)", true, acceptWarp,
                              rhi_d3d11_debug_error_count};
    int rcHw = rhi_test_run_cells(&hw);
    char hwAdapter[128];
    snprintf(hwAdapter, sizeof(hwAdapter), "%s", s_adapter[0] ? s_adapter : "none");

    /* 3: the swapchain */
    rhi_test_log("\n== 3. Direct3D 11 swapchain on a hidden window\n");
#ifdef ICO_RHI_HAVE_SDL
    int rcSwap = runSwapchain();
#else
    rhi_test_log("SKIP: built without SDL3\n");
    int rcSwap = 77;
#endif

    /* 4: Vulkan, for comparison */
    rhi_test_log("\n== 4. Vulkan on this machine (for comparison; a skip is not a failure)\n");
    const RhiTestConfig vk = {"vulkan", "rhi_d3d11_test (Vulkan)", false, NULL, NULL};
    int rcVk = rhi_test_run_cells(&vk);

    bool failed =
        rcWarp == 1 || rcMapped == 1 || rcMark == 1 || rcHw == 1 || rcSwap == 1 || rcVk == 1;
    bool ran = rcWarp == 0 || rcHw == 0 || rcSwap == 0;
    char summary[1024];
    snprintf(summary, sizeof(summary),
             "%s\n\n"
             "1. Direct3D 11 on WARP (%s): %s\n"
             "1b. the same, constant buffers mapped: %s\n"
             "1c. the upload ring's written mark: %s\n"
             "2. Direct3D 11 on hardware (%s): %s\n"
             "3. Direct3D 11 swapchain: %s\n"
             "4. Vulkan (comparison): %s\n"
             "Direct3D 11 debug layer: %s\n\n"
             "Log: %s",
             failed ? "rhi_d3d11_test: FAILED"
             : ran  ? "rhi_d3d11_test: PASSED"
                    : "rhi_d3d11_test: nothing could run",
             warpAdapter[0] ? warpAdapter : "-", verdict(rcWarp), verdict(rcMapped),
             verdict(rcMark), hwAdapter, verdict(rcHw), verdict(rcSwap), verdict(rcVk),
             s_debugActive ? "on (errors fail the test)"
                           : "not installed (Settings > Optional features > Graphics Tools "
                             "adds it)",
             console ? "(console)" : path);
    rhi_test_log("\n%s\n", summary);
    if (!console) {
        MessageBoxA(NULL, summary, "rhi_d3d11_test",
                    MB_OK | (failed ? MB_ICONERROR : MB_ICONINFORMATION));
    }
    return failed ? 1 : ran ? 0 : 77;
}
