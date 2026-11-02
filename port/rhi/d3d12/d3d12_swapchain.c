/* d3d12_swapchain.c: the DXGI flip-model swapchain on the SDL window's
 * HWND.  Absent when the device is headless (RhiDeviceDesc.sdlWindow ==
 * NULL). */
#include "d3d12_internal.h"
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RHI_HAVE_SDL
#include <SDL3/SDL.h>
#endif

#define DX_SWAP_BUFFERS 3u

/* The swapchain's lock: the acquire, the present and ResizeBuffers
 * (rhi_resize_swapchain) hold it, so a present on the window's render
 * thread (port/platform/render_thread.c) and an acquire or a resize on the
 * window's thread never interleave.  A critical section is recursive: a
 * present that follows the window's size calls rhi_resize_swapchain inside
 * it.  Initialised once, by whichever thread comes first. */
static CRITICAL_SECTION s_swapLock;
static INIT_ONCE s_swapLockOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK dx_swap_lock_init(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&s_swapLock);
    return TRUE;
}

static void dx_swap_lock(void)
{
    InitOnceExecuteOnce(&s_swapLockOnce, dx_swap_lock_init, NULL, NULL);
    EnterCriticalSection(&s_swapLock);
}

static void dx_swap_unlock(void)
{
    LeaveCriticalSection(&s_swapLock);
}

static void dx_release_swap_buffers(void)
{
    for (uint32_t i = 0; i < g_dx.swapCount; i++) {
        dx_release_swapchain_buffer(g_dx.swapTextures[i]);
        g_dx.swapTextures[i] = 0;
    }
    g_dx.swapCount = 0;
}

static bool dx_register_swap_buffers(uint32_t w, uint32_t h)
{
    for (uint32_t i = 0; i < DX_SWAP_BUFFERS; i++) {
        ID3D12Resource *res = NULL;
        if (!DX_CHECK(
                IDXGISwapChain3_GetBuffer(g_dx.swapchain, i, &IID_ID3D12Resource, (void **)&res))) {
            return false;
        }
        g_dx.swapTextures[i] = dx_register_swapchain_buffer(res, w, h);
        if (!g_dx.swapTextures[i]) {
            ID3D12Resource_Release(res);
            return false;
        }
        g_dx.swapCount = i + 1u;
    }
    g_dx.swapWidth = w;
    g_dx.swapHeight = h;
    g_dx.swapAcquired = false;
    return true;
}

static void dx_frame_latency_close(void)
{
    if (g_dx.frameLatencyEvent) {
        CloseHandle(g_dx.frameLatencyEvent);
        g_dx.frameLatencyEvent = NULL;
    }
}

void dx_swapchain_destroy(void)
{
    dx_release_swap_buffers();
    dx_frame_latency_close();
    DX_RELEASE(g_dx.swapchain);
}

/* The frame latency the waitable swap chain is made with:
 * ICO_D3D12_FRAME_LATENCY "0" = no waitable object (Present blocks on a
 * full queue), "1".."3" that latency (1 serialises the CPU and
 * the GPU), anything else 2.  With the object, rhi_acquire_backbuffer
 * waits for the display before the replay instead of Present after it. */
static uint32_t dx_frame_latency_wanted(void)
{
    const char *env = getenv("ICO_D3D12_FRAME_LATENCY");
    if (env && env[0] >= '0' && env[0] <= '3' && env[1] == '\0') {
        return (uint32_t)(env[0] - '0');
    }
    if (env && env[0]) {
        DX_LOG("ICO_D3D12_FRAME_LATENCY=%s is not 0..3: frame latency 2", env);
    }
    return 2u;
}

/* the same flags at creation and in ResizeBuffers (DXGI refuses a change
 * of the waitable flag) */
static UINT dx_swap_flags(void)
{
    UINT f = g_dx.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    if (g_dx.frameLatency) {
        f |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    }
    return f;
}

/* After creation: the latency and the waitable object.  IDXGISwapChain3
 * derives from IDXGISwapChain2, so its methods are called on the
 * swapchain itself (no second interface to hold).  No event = one log
 * line and Present blocks on a full queue. */
static void dx_frame_latency_open(void)
{
    dx_frame_latency_close();
    g_dx.frameLatencyTimeoutLogged = false;
    g_dx.frameLatencySkipWait = false; /* the first acquire waits */
    if (!g_dx.frameLatency) {
        return;
    }
    if (!DX_CHECK(IDXGISwapChain3_SetMaximumFrameLatency(g_dx.swapchain, g_dx.frameLatency))) {
        DX_LOG("frame latency %u not set: no waitable object", g_dx.frameLatency);
        return;
    }
    g_dx.frameLatencyEvent = IDXGISwapChain3_GetFrameLatencyWaitableObject(g_dx.swapchain);
    if (!g_dx.frameLatencyEvent) {
        DX_LOG("no frame latency waitable object: Present waits for the display");
    }
}

bool dx_swapchain_create(uint32_t w, uint32_t h, bool vsync)
{
    if (!g_dx.hwnd || w == 0 || h == 0) {
        return false;
    }
    /* tearing (uncapped without vsync) when DXGI 1.5 offers it */
    IDXGIFactory5 *f5 = NULL;
    g_dx.tearing = false;
    if (SUCCEEDED(IDXGIFactory4_QueryInterface(g_dx.factory, &IID_IDXGIFactory5, (void **)&f5))) {
        BOOL allow = FALSE;
        if (SUCCEEDED(IDXGIFactory5_CheckFeatureSupport(f5, DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                        &allow, sizeof(allow)))) {
            g_dx.tearing = allow != FALSE;
        }
        IDXGIFactory5_Release(f5);
    }
    /* BGRA8 UNORM: the game's colours as-is, like the Vulkan backend */
    DXGI_SWAP_CHAIN_DESC1 d;
    memset(&d, 0, sizeof(d));
    d.Width = w;
    d.Height = h;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = DX_SWAP_BUFFERS;
    d.Scaling = DXGI_SCALING_STRETCH;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    g_dx.frameLatency = dx_frame_latency_wanted();
    d.Flags = dx_swap_flags();
    IDXGISwapChain1 *sc1 = NULL;
    if (!DX_CHECK(IDXGIFactory4_CreateSwapChainForHwnd(g_dx.factory, (IUnknown *)g_dx.queue,
                                                       g_dx.hwnd, &d, NULL, NULL, &sc1))) {
        return false;
    }
    HRESULT hr =
        IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void **)&g_dx.swapchain);
    IDXGISwapChain1_Release(sc1);
    if (!DX_CHECK(hr)) {
        return false;
    }
    g_dx.swapchains++;
    /* SDL owns fullscreen (the window's own Alt+Enter); no DXGI Alt+Enter */
    IDXGIFactory4_MakeWindowAssociation(g_dx.factory, g_dx.hwnd, DXGI_MWA_NO_ALT_ENTER);
    g_dx.vsync = vsync;
    dx_frame_latency_open();
    if (g_dx.frameLatencyEvent) {
        DX_LOG("swapchain %ux%u, %u buffers, frame latency %u (waitable)", w, h, DX_SWAP_BUFFERS,
               g_dx.frameLatency);
    } else {
        DX_LOG("swapchain %ux%u, %u buffers, no waitable object", w, h, DX_SWAP_BUFFERS);
    }
    return dx_register_swap_buffers(w, h);
}

static bool dx_resize_swapchain_locked(uint32_t width, uint32_t height, bool vsync)
{
    if (!g_dx.swapchain) {
        return false;
    }
    if (width == 0 || height == 0) {
        return false; /* minimised: the caller retries on the next resize */
    }
    rhi_wait_idle();
    dx_release_swap_buffers();
    if (!DX_CHECK(IDXGISwapChain3_ResizeBuffers(g_dx.swapchain, DX_SWAP_BUFFERS, width, height,
                                                DXGI_FORMAT_B8G8R8A8_UNORM, dx_swap_flags()))) {
        return false;
    }
    g_dx.swapchains++;
    g_dx.vsync = vsync;
    return dx_register_swap_buffers(width, height);
}

bool rhi_resize_swapchain(uint32_t width, uint32_t height, bool vsync)
{
    dx_swap_lock();
    const bool ok = dx_resize_swapchain_locked(width, height, vsync);
    dx_swap_unlock();
    return ok;
}

RhiFormat rhi_swapchain_format(void)
{
    return g_dx.swapchain ? RHI_FMT_BGRA8_UNORM : RHI_FMT_UNKNOWN;
}

bool rhi_swapchain_size(uint32_t *w, uint32_t *h)
{
    if (!g_dx.swapchain || g_dx.swapCount == 0 || !g_dx.swapWidth || !g_dx.swapHeight) {
        return false;
    }
    *w = g_dx.swapWidth;
    *h = g_dx.swapHeight;
    return true;
}

void rhi_surface_poll_restart(void)
{
    /* D3D12 learns a size change from DXGI; nothing to poll */
}

/* the last present reached the display (rhi_present_shown) */
static bool s_presentShown;

bool rhi_present_shown(void)
{
    return s_presentShown;
}

static RhiTexture dx_acquire_backbuffer_locked(void)
{
    RhiTexture out = {0};
    s_presentShown = false; /* until this frame's present shows it */
    if (!g_dx.swapchain || g_dx.swapCount == 0) {
        return out;
    }
    if (!g_dx.swapAcquired) {
        /* the waitable swap chain: wait here, before the replay, until the
         * queue has room for this frame (Present then returns at once and
         * the picture is drawn after the wait, not before it).  The first
         * acquire waits too.  With vsync on or off: the object counts
         * presented frames, not refreshes, so without vsync the wait
         * returns as soon as a frame has left the queue (uncapped stays
         * uncapped), and a count left over from a run without waits
         * cannot carry into a later vsync-on run.  Not after a Present
         * that queued nothing (occluded, failed): no frame will leave
         * the queue for it.  50 ms (three refreshes) bounds a count that
         * is off to one frame.  RhiStats.acquireNs: the window's
         * "acquire". */
        if (g_dx.frameLatencyEvent && !g_dx.frameLatencySkipWait) {
            const uint64_t t0 = dx_now_ns();
            const DWORD r = WaitForSingleObjectEx(g_dx.frameLatencyEvent, 50, TRUE);
            g_dx.acquireNs += dx_now_ns() - t0;
            if (r == WAIT_TIMEOUT && !g_dx.frameLatencyTimeoutLogged) {
                g_dx.frameLatencyTimeoutLogged = true;
                DX_LOG("the frame latency wait took over 50 ms: going on without it (once)");
            }
        }
        g_dx.frameLatencySkipWait = false;
        g_dx.swapIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(g_dx.swapchain);
        if (g_dx.swapIndex >= g_dx.swapCount) {
            return out;
        }
        /* rhi.h: the acquired buffer starts UNDEFINED; on D3D12 it is in
         * PRESENT (COMMON) after the previous present */
        RhiTexture h = {g_dx.swapTextures[g_dx.swapIndex]};
        DxTexture *t = dx_get_texture(h);
        if (t) {
            t->state = D3DP_STATE_PRESENT;
        }
        g_dx.swapAcquired = true;
    }
    out.id = g_dx.swapTextures[g_dx.swapIndex];
    return out;
}

RhiTexture rhi_acquire_backbuffer(void)
{
    dx_swap_lock();
    const RhiTexture out = dx_acquire_backbuffer_locked();
    dx_swap_unlock();
    return out;
}

/* rhi.h: what rhi_present's sync interval and flags amount to, in the
 * Vulkan backend's words */
const char *rhi_present_mode_name(void)
{
    if (!g_dx.swapchain) {
        return "none";
    }
    if (g_dx.vsync) {
        return "fifo";
    }
    return g_dx.tearing ? "immediate" : "mailbox";
}

/* The frame statistics after a present (rhi_present_statistics): not
 * through DX_CHECK, DXGI_ERROR_FRAME_STATISTICS_DISJOINT is a normal
 * answer (a mode change; the first presents) */
static void dx_frame_stats(void)
{
    DXGI_FRAME_STATISTICS fs;
    memset(&fs, 0, sizeof(fs));
    if (FAILED(IDXGISwapChain3_GetFrameStatistics(g_dx.swapchain, &fs)) || fs.PresentCount == 0) {
        return;
    }
    g_dx.frameStatsPresent = fs.PresentCount;
    g_dx.frameStatsRefresh = fs.PresentRefreshCount;
    g_dx.frameStatsSyncRefresh = fs.SyncRefreshCount;
    g_dx.frameStatsSyncNs = 0;
#ifdef ICO_RHI_HAVE_SDL
    {
        /* SyncQPCTime on SDL_GetTicksNS's clock: as long before it as
           before QueryPerformanceCounter now */
        LARGE_INTEGER now;
        const uint64_t ticks = SDL_GetTicksNS();
        QueryPerformanceCounter(&now);
        const uint64_t sync = (uint64_t)fs.SyncQPCTime.QuadPart;
        if (sync != 0 && sync <= (uint64_t)now.QuadPart) {
            const uint64_t ago = dx_qpc_ns((uint64_t)now.QuadPart - sync);
            g_dx.frameStatsSyncNs = ago < ticks ? ticks - ago : 0;
        }
    }
#endif
    g_dx.frameStatsValid = true;
}

bool rhi_present_statistics(uint64_t *presentCount, uint64_t *presentRefresh, uint64_t *syncRefresh,
                            uint64_t *syncNs)
{
    if (!g_dx.swapchain || !g_dx.frameStatsValid) {
        return false;
    }
    *presentCount = g_dx.frameStatsPresent;
    *presentRefresh = g_dx.frameStatsRefresh;
    *syncRefresh = g_dx.frameStatsSyncRefresh;
    *syncNs = g_dx.frameStatsSyncNs;
    return true;
}

static void dx_present_locked(void)
{
    s_presentShown = false;
    if (!g_dx.swapchain || !g_dx.swapAcquired) {
        return;
    }
    if (g_dx.deviceLost) {
        g_dx.swapAcquired = false;
        g_dx.frameLatencySkipWait = true;
        return;
    }
    UINT flags = (!g_dx.vsync && g_dx.tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    const uint64_t t0 = dx_now_ns();
    HRESULT hr = IDXGISwapChain3_Present(g_dx.swapchain, g_dx.vsync ? 1u : 0u, flags);
    g_dx.presentNs += dx_now_ns() - t0; /* RhiStats */
    g_dx.presents++;
    g_dx.swapAcquired = false;
    g_dx.frameStatsValid = false;
    /* the next acquire waits only when this Present queued a frame */
    g_dx.frameLatencySkipWait = FAILED(hr) || hr == DXGI_STATUS_OCCLUDED;
    if (FAILED(hr)) {
        /* DXGI_ERROR_DEVICE_REMOVED / _RESET / _HUNG mark the device lost
         * here (dx_check); the window loop ends the session */
        DX_CHECK(hr);
        return;
    }
    /* DXGI_STATUS_OCCLUDED (a success code): the window is minimised or
       covered; the flip-model present returned without waiting for a
       vblank and nothing was shown */
    s_presentShown = hr != DXGI_STATUS_OCCLUDED;
    if (s_presentShown) {
        dx_frame_stats();
    }
    /* DXGI has no out-of-date result: follow the window's size here, as
     * the Vulkan backend does on VK_ERROR_OUT_OF_DATE_KHR */
    int w = (int)g_dx.swapWidth, h = (int)g_dx.swapHeight;
#ifdef ICO_RHI_HAVE_SDL
    if (g_dx.window) {
        SDL_GetWindowSizeInPixels((SDL_Window *)g_dx.window, &w, &h);
    }
#endif
    if (w > 0 && h > 0 && ((uint32_t)w != g_dx.swapWidth || (uint32_t)h != g_dx.swapHeight)) {
        rhi_resize_swapchain((uint32_t)w, (uint32_t)h, g_dx.vsync);
    }
}

void rhi_present(void)
{
    dx_swap_lock();
    dx_present_locked();
    dx_swap_unlock();
}

/* rhi.h: the Android lifecycle's surface calls.  A DXGI
 * swapchain stays on its window for the window's life, so there is nothing
 * to release; recreate reports whether the swapchain is there. */
void rhi_release_surface(void) {}

bool rhi_recreate_surface(void *window)
{
    (void)window;
    return g_dx.swapchain != NULL;
}
