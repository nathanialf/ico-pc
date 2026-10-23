/* d3d11_swapchain.c: the DXGI flip-model swapchain on the SDL window's
 * HWND for the Direct3D 11 backend, cloned from d3d12_swapchain.c (three
 * buffers, the frame latency waitable object, tearing without vsync, the
 * frame statistics).  Under the flip model a Direct3D 11 swapchain's buffer
 * 0 is always the one to draw (the runtime rotates the buffers behind it),
 * so it is the one RHI texture.  Absent when the device is headless.
 * Compile-only in v0.8.6: it has not run on any Windows machine. */
#include "d3d11_internal.h"
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RHI_HAVE_SDL
#include <SDL3/SDL.h>
#endif

#define D11_SWAP_BUFFERS 3u

static void d11_release_swap_buffer(void)
{
    if (g_d11.swapTexture) {
        d11_release_swapchain_buffer(g_d11.swapTexture);
        g_d11.swapTexture = 0;
    }
}

/* buffer 0's texture and RTV (again after every ResizeBuffers) */
static bool d11_register_swap_buffer(uint32_t w, uint32_t h)
{
    ID3D11Texture2D *res = NULL;
    if (!D11_CHECK(
            IDXGISwapChain1_GetBuffer(g_d11.swapchain, 0, &IID_ID3D11Texture2D, (void **)&res))) {
        return false;
    }
    g_d11.swapTexture = d11_register_swapchain_buffer(res, w, h);
    if (!g_d11.swapTexture) {
        ID3D11Texture2D_Release(res);
        return false;
    }
    g_d11.swapWidth = w;
    g_d11.swapHeight = h;
    g_d11.swapAcquired = false;
    return true;
}

static void d11_frame_latency_close(void)
{
    if (g_d11.frameLatencyEvent) {
        CloseHandle(g_d11.frameLatencyEvent);
        g_d11.frameLatencyEvent = NULL;
    }
}

void d11_swapchain_destroy(void)
{
    d11_release_swap_buffer();
    d11_frame_latency_close();
    D11_RELEASE(g_d11.swapchain2);
    D11_RELEASE(g_d11.swapchain);
}

/* ICO_D3D11_FRAME_LATENCY "0" = no waitable object, "1".."3" that
 * latency, anything else 2 (as ICO_D3D12_FRAME_LATENCY) */
static uint32_t d11_frame_latency_wanted(void)
{
    const char *env = getenv("ICO_D3D11_FRAME_LATENCY");
    if (env && env[0] >= '0' && env[0] <= '3' && env[1] == '\0') {
        return (uint32_t)(env[0] - '0');
    }
    if (env && env[0]) {
        D11_LOG("ICO_D3D11_FRAME_LATENCY=%s is not 0..3: frame latency 2", env);
    }
    return 2u;
}

/* the same flags at creation and in ResizeBuffers */
static UINT d11_swap_flags(void)
{
    UINT f = g_d11.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    if (g_d11.frameLatency) {
        f |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    }
    return f;
}

/* After creation: the latency and the waitable object (DXGI 1.3's
 * IDXGISwapChain2); without it Present blocks on a full queue. */
static void d11_frame_latency_open(void)
{
    d11_frame_latency_close();
    g_d11.frameLatencyTimeoutLogged = false;
    g_d11.frameLatencySkipWait = false;
    if (!g_d11.frameLatency || !g_d11.swapchain2) {
        return;
    }
    if (!D11_CHECK(IDXGISwapChain2_SetMaximumFrameLatency(g_d11.swapchain2, g_d11.frameLatency))) {
        D11_LOG("frame latency %u not set: no waitable object", g_d11.frameLatency);
        return;
    }
    g_d11.frameLatencyEvent = IDXGISwapChain2_GetFrameLatencyWaitableObject(g_d11.swapchain2);
    if (!g_d11.frameLatencyEvent) {
        D11_LOG("no frame latency waitable object: Present waits for the display");
    }
}

bool d11_swapchain_create(uint32_t w, uint32_t h, bool vsync)
{
    if (!g_d11.hwnd || w == 0 || h == 0) {
        return false;
    }
    /* tearing (uncapped without vsync) when DXGI 1.5 offers it */
    IDXGIFactory5 *f5 = NULL;
    g_d11.tearing = false;
    if (SUCCEEDED(IDXGIFactory2_QueryInterface(g_d11.factory, &IID_IDXGIFactory5, (void **)&f5))) {
        BOOL allow = FALSE;
        if (SUCCEEDED(IDXGIFactory5_CheckFeatureSupport(f5, DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                        &allow, sizeof(allow)))) {
            g_d11.tearing = allow != FALSE;
        }
        IDXGIFactory5_Release(f5);
    }
    DXGI_SWAP_CHAIN_DESC1 d;
    memset(&d, 0, sizeof(d));
    d.Width = w;
    d.Height = h;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; /* the game's colours as-is, as the other backends */
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = D11_SWAP_BUFFERS;
    d.Scaling = DXGI_SCALING_STRETCH;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    g_d11.frameLatency = d11_frame_latency_wanted();
    d.Flags = d11_swap_flags();
    HRESULT hr = IDXGIFactory2_CreateSwapChainForHwnd(g_d11.factory, (IUnknown *)g_d11.device,
                                                      g_d11.hwnd, &d, NULL, NULL, &g_d11.swapchain);
    g_d11.flipDiscard = SUCCEEDED(hr);
    if (FAILED(hr)) {
        /* Windows 8 and 8.1 have FLIP_SEQUENTIAL only (and no tearing) */
        D11_LOG("FLIP_DISCARD refused (HRESULT 0x%08lx): FLIP_SEQUENTIAL", (unsigned long)hr);
        g_d11.tearing = false;
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        d.Flags = d11_swap_flags();
        hr = IDXGIFactory2_CreateSwapChainForHwnd(g_d11.factory, (IUnknown *)g_d11.device,
                                                  g_d11.hwnd, &d, NULL, NULL, &g_d11.swapchain);
        if (FAILED(hr) && g_d11.frameLatency) {
            /* the frame latency waitable object is Windows 8.1's (DXGI
             * 1.3): Windows 8.0 refuses the flag; without it Present waits
             * for the display */
            D11_LOG("FLIP_SEQUENTIAL with the frame latency waitable object refused (HRESULT "
                    "0x%08lx): without it",
                    (unsigned long)hr);
            g_d11.frameLatency = 0;
            d.Flags = d11_swap_flags();
            hr = IDXGIFactory2_CreateSwapChainForHwnd(g_d11.factory, (IUnknown *)g_d11.device,
                                                      g_d11.hwnd, &d, NULL, NULL, &g_d11.swapchain);
        }
    }
    if (!D11_CHECK(hr)) {
        g_d11.swapchain = NULL;
        return false;
    }
    if (FAILED(IDXGISwapChain1_QueryInterface(g_d11.swapchain, &IID_IDXGISwapChain2,
                                              (void **)&g_d11.swapchain2))) {
        g_d11.swapchain2 = NULL;
    }
    g_d11.swapchains++;
    /* SDL owns fullscreen (the window's own Alt+Enter); no DXGI Alt+Enter */
    IDXGIFactory2_MakeWindowAssociation(g_d11.factory, g_d11.hwnd, DXGI_MWA_NO_ALT_ENTER);
    g_d11.vsync = vsync;
    d11_frame_latency_open();
    if (g_d11.frameLatencyEvent) {
        D11_LOG("swapchain %ux%u, %u buffers, %s, frame latency %u (waitable)", w, h,
                D11_SWAP_BUFFERS, g_d11.flipDiscard ? "flip discard" : "flip sequential",
                g_d11.frameLatency);
    } else {
        D11_LOG("swapchain %ux%u, %u buffers, %s, no waitable object", w, h, D11_SWAP_BUFFERS,
                g_d11.flipDiscard ? "flip discard" : "flip sequential");
    }
    return d11_register_swap_buffer(w, h);
}

bool rhi_resize_swapchain(uint32_t width, uint32_t height, bool vsync)
{
    if (!g_d11.swapchain) {
        return false;
    }
    if (width == 0 || height == 0) {
        return false; /* minimised: the caller retries on the next resize */
    }
    rhi_wait_idle();
    /* ResizeBuffers needs every reference to the buffers gone: the RTV,
     * the texture, and the immediate context's bindings */
    d11_release_swap_buffer();
    ID3D11DeviceContext_ClearState(g_d11.ctx);
    ID3D11DeviceContext_Flush(g_d11.ctx);
    if (!D11_CHECK(IDXGISwapChain1_ResizeBuffers(g_d11.swapchain, D11_SWAP_BUFFERS, width, height,
                                                 DXGI_FORMAT_B8G8R8A8_UNORM, d11_swap_flags()))) {
        return false;
    }
    g_d11.swapchains++;
    g_d11.vsync = vsync;
    return d11_register_swap_buffer(width, height);
}

RhiFormat rhi_swapchain_format(void)
{
    return g_d11.swapchain ? RHI_FMT_BGRA8_UNORM : RHI_FMT_UNKNOWN;
}

bool rhi_swapchain_size(uint32_t *w, uint32_t *h)
{
    if (!g_d11.swapchain || !g_d11.swapTexture || !g_d11.swapWidth || !g_d11.swapHeight) {
        return false;
    }
    *w = g_d11.swapWidth;
    *h = g_d11.swapHeight;
    return true;
}

void rhi_surface_poll_restart(void)
{
    /* DXGI's size follows the window at each present; nothing to poll */
}

static bool s_presentShown;

bool rhi_present_shown(void)
{
    return s_presentShown;
}

RhiTexture rhi_acquire_backbuffer(void)
{
    RhiTexture out = {0};
    s_presentShown = false;
    if (!g_d11.swapchain || !g_d11.swapTexture) {
        return out;
    }
    if (!g_d11.swapAcquired) {
        /* the waitable swap chain: wait before the replay for room in the
         * queue (as d3d12_swapchain.c, 50 ms at most; not after a Present
         * that queued nothing) */
        if (g_d11.frameLatencyEvent && !g_d11.frameLatencySkipWait) {
            const uint64_t t0 = d11_now_ns();
            const DWORD r = WaitForSingleObjectEx(g_d11.frameLatencyEvent, 50, TRUE);
            g_d11.acquireNs += d11_now_ns() - t0;
            if (r == WAIT_TIMEOUT && !g_d11.frameLatencyTimeoutLogged) {
                g_d11.frameLatencyTimeoutLogged = true;
                D11_LOG("the frame latency wait took over 50 ms: going on without it (once)");
            }
        }
        g_d11.frameLatencySkipWait = false;
        g_d11.swapAcquired = true;
    }
    out.id = g_d11.swapTexture;
    return out;
}

/* rhi.h, D3D12's rule: "fifo" with vsync; without, "immediate" when DXGI
 * allows tearing, else "mailbox" */
const char *rhi_present_mode_name(void)
{
    if (!g_d11.swapchain) {
        return "none";
    }
    if (g_d11.vsync) {
        return "fifo";
    }
    return g_d11.tearing ? "immediate" : "mailbox";
}

/* the frame statistics after a present; DXGI_ERROR_FRAME_STATISTICS_DISJOINT
 * is a normal answer (a mode change, the first presents) */
static void d11_frame_stats(void)
{
    DXGI_FRAME_STATISTICS fs;
    memset(&fs, 0, sizeof(fs));
    if (FAILED(IDXGISwapChain1_GetFrameStatistics(g_d11.swapchain, &fs)) || fs.PresentCount == 0) {
        return;
    }
    g_d11.frameStatsPresent = fs.PresentCount;
    g_d11.frameStatsRefresh = fs.PresentRefreshCount;
    g_d11.frameStatsSyncRefresh = fs.SyncRefreshCount;
    g_d11.frameStatsSyncNs = 0;
#ifdef ICO_RHI_HAVE_SDL
    {
        /* SyncQPCTime on SDL_GetTicksNS's clock */
        LARGE_INTEGER now;
        const uint64_t ticks = SDL_GetTicksNS();
        QueryPerformanceCounter(&now);
        const uint64_t sync = (uint64_t)fs.SyncQPCTime.QuadPart;
        if (sync != 0 && sync <= (uint64_t)now.QuadPart) {
            const uint64_t ago = d11_qpc_ns((uint64_t)now.QuadPart - sync);
            g_d11.frameStatsSyncNs = ago < ticks ? ticks - ago : 0;
        }
    }
#endif
    g_d11.frameStatsValid = true;
}

bool rhi_present_statistics(uint64_t *presentCount, uint64_t *presentRefresh, uint64_t *syncRefresh,
                            uint64_t *syncNs)
{
    if (!g_d11.swapchain || !g_d11.frameStatsValid) {
        return false;
    }
    *presentCount = g_d11.frameStatsPresent;
    *presentRefresh = g_d11.frameStatsRefresh;
    *syncRefresh = g_d11.frameStatsSyncRefresh;
    *syncNs = g_d11.frameStatsSyncNs;
    return true;
}

void rhi_present(void)
{
    s_presentShown = false;
    if (!g_d11.swapchain || !g_d11.swapAcquired) {
        return;
    }
    if (g_d11.deviceLost) {
        g_d11.swapAcquired = false;
        g_d11.frameLatencySkipWait = true;
        return;
    }
    const UINT flags = (!g_d11.vsync && g_d11.tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    const uint64_t t0 = d11_now_ns();
    HRESULT hr = IDXGISwapChain1_Present(g_d11.swapchain, g_d11.vsync ? 1u : 0u, flags);
    g_d11.presentNs += d11_now_ns() - t0;
    g_d11.presents++;
    g_d11.swapAcquired = false;
    g_d11.frameStatsValid = false;
    g_d11.frameLatencySkipWait = FAILED(hr) || hr == DXGI_STATUS_OCCLUDED;
    if (FAILED(hr)) {
        D11_CHECK(hr); /* a removal marks the device lost; the window ends the session */
        return;
    }
    s_presentShown = hr != DXGI_STATUS_OCCLUDED;
    if (s_presentShown) {
        d11_frame_stats();
    }
    /* DXGI has no out-of-date result: follow the window's size here */
    int w = (int)g_d11.swapWidth, h = (int)g_d11.swapHeight;
#ifdef ICO_RHI_HAVE_SDL
    if (g_d11.window) {
        SDL_GetWindowSizeInPixels((SDL_Window *)g_d11.window, &w, &h);
    }
#endif
    if (w > 0 && h > 0 && ((uint32_t)w != g_d11.swapWidth || (uint32_t)h != g_d11.swapHeight)) {
        rhi_resize_swapchain((uint32_t)w, (uint32_t)h, g_d11.vsync);
    }
}

/* rhi.h: the Android lifecycle's surface calls; a DXGI swapchain stays on
 * its window, as on D3D12 */
void rhi_release_surface(void) {}

bool rhi_recreate_surface(void *window)
{
    (void)window;
    return g_d11.swapchain != NULL;
}
