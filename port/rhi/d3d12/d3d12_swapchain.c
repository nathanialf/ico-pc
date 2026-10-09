/* d3d12_swapchain.c: the DXGI flip-model swapchain on the SDL window's
 * HWND.  Absent when the device is headless (RhiDeviceDesc.sdlWindow ==
 * NULL). */
#include "d3d12_internal.h"
#include <string.h>

#ifdef ICO_RHI_HAVE_SDL
#include <SDL3/SDL.h>
#endif

#define DX_SWAP_BUFFERS 3u

static void dx_ReleaseSwapBuffers(void)
{
    for (uint32_t i = 0; i < g_dx.swapCount; i++) {
        dx_ReleaseSwapchainBuffer(g_dx.swapTextures[i]);
        g_dx.swapTextures[i] = 0;
    }
    g_dx.swapCount = 0;
}

static bool dx_RegisterSwapBuffers(uint32_t w, uint32_t h)
{
    for (uint32_t i = 0; i < DX_SWAP_BUFFERS; i++) {
        ID3D12Resource *res = NULL;
        if (!DX_CHECK(
                IDXGISwapChain3_GetBuffer(g_dx.swapchain, i, &IID_ID3D12Resource, (void **)&res))) {
            return false;
        }
        g_dx.swapTextures[i] = dx_RegisterSwapchainBuffer(res, w, h);
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

void dx_SwapchainDestroy(void)
{
    dx_ReleaseSwapBuffers();
    DX_RELEASE(g_dx.swapchain);
}

static UINT dx_SwapFlags(void)
{
    return g_dx.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
}

bool dx_SwapchainCreate(uint32_t w, uint32_t h, bool vsync)
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
    d.Flags = dx_SwapFlags();
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
    /* SDL owns fullscreen (F11); no DXGI Alt+Enter */
    IDXGIFactory4_MakeWindowAssociation(g_dx.factory, g_dx.hwnd, DXGI_MWA_NO_ALT_ENTER);
    g_dx.vsync = vsync;
    return dx_RegisterSwapBuffers(w, h);
}

bool rhi_ResizeSwapchain(uint32_t width, uint32_t height, bool vsync)
{
    if (!g_dx.swapchain) {
        return false;
    }
    if (width == 0 || height == 0) {
        return false; /* minimised: the caller retries on the next resize */
    }
    rhi_WaitIdle();
    dx_ReleaseSwapBuffers();
    if (!DX_CHECK(IDXGISwapChain3_ResizeBuffers(g_dx.swapchain, DX_SWAP_BUFFERS, width, height,
                                                DXGI_FORMAT_B8G8R8A8_UNORM, dx_SwapFlags()))) {
        return false;
    }
    g_dx.vsync = vsync;
    return dx_RegisterSwapBuffers(width, height);
}

RhiFormat rhi_SwapchainFormat(void)
{
    return g_dx.swapchain ? RHI_FMT_BGRA8_UNORM : RHI_FMT_UNKNOWN;
}

bool rhi_SwapchainSize(uint32_t *w, uint32_t *h)
{
    if (!g_dx.swapchain || g_dx.swapCount == 0 || !g_dx.swapWidth || !g_dx.swapHeight) {
        return false;
    }
    *w = g_dx.swapWidth;
    *h = g_dx.swapHeight;
    return true;
}

void rhi_SurfacePollRestart(void)
{
    /* D3D12 learns a size change from DXGI; nothing to poll */
}

RhiTexture rhi_AcquireBackbuffer(void)
{
    RhiTexture out = {0};
    if (!g_dx.swapchain || g_dx.swapCount == 0) {
        return out;
    }
    if (!g_dx.swapAcquired) {
        g_dx.swapIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(g_dx.swapchain);
        if (g_dx.swapIndex >= g_dx.swapCount) {
            return out;
        }
        /* rhi.h: the acquired buffer starts UNDEFINED; on D3D12 it is in
         * PRESENT (COMMON) after the previous present */
        RhiTexture h = {g_dx.swapTextures[g_dx.swapIndex]};
        DxTexture *t = dx_GetTexture(h);
        if (t) {
            t->state = D3DP_STATE_PRESENT;
        }
        g_dx.swapAcquired = true;
    }
    out.id = g_dx.swapTextures[g_dx.swapIndex];
    return out;
}

/* rhi.h: what rhi_Present's sync interval and flags amount to, in the
 * Vulkan backend's words */
const char *rhi_PresentModeName(void)
{
    if (!g_dx.swapchain) {
        return "none";
    }
    if (g_dx.vsync) {
        return "fifo";
    }
    return g_dx.tearing ? "immediate" : "mailbox";
}

void rhi_Present(void)
{
    if (!g_dx.swapchain || !g_dx.swapAcquired) {
        return;
    }
    if (g_dx.deviceLost) {
        g_dx.swapAcquired = false;
        return;
    }
    UINT flags = (!g_dx.vsync && g_dx.tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    HRESULT hr = IDXGISwapChain3_Present(g_dx.swapchain, g_dx.vsync ? 1u : 0u, flags);
    g_dx.swapAcquired = false;
    if (FAILED(hr)) {
        /* DXGI_ERROR_DEVICE_REMOVED / _RESET / _HUNG mark the device lost
         * here (dx_Check); the window loop ends the session */
        DX_CHECK(hr);
        return;
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
        rhi_ResizeSwapchain((uint32_t)w, (uint32_t)h, g_dx.vsync);
    }
}

/* rhi.h: the Android lifecycle's surface calls.  A DXGI
 * swapchain stays on its window for the window's life, so there is nothing
 * to release; recreate reports whether the swapchain is there. */
void rhi_ReleaseSurface(void) {}

bool rhi_RecreateSurface(void *window)
{
    (void)window;
    return g_dx.swapchain != NULL;
}
