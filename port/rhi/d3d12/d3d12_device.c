/* d3d12_device.c: runtime loading, adapter selection, the device and its
 * queue, the debug layer, descriptor heaps, limits, rhi_Init/rhi_Shutdown
 * of the D3D12 backend (README.md, "Device"). */
#include "d3d12_internal.h"
#include "rhi_d3d12.h"
#include "../rhi_backend.h"
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RHI_HAVE_SDL
#include <SDL3/SDL.h>
#endif

DxState g_dx;

static bool dx_IsLossCode(HRESULT hr)
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

static const char *dx_LossName(HRESULT hr)
{
    switch (hr) {
    case DXGI_ERROR_DEVICE_REMOVED:
        return "DXGI_ERROR_DEVICE_REMOVED";
    case DXGI_ERROR_DEVICE_RESET:
        return "DXGI_ERROR_DEVICE_RESET";
    case DXGI_ERROR_DEVICE_HUNG:
        return "DXGI_ERROR_DEVICE_HUNG";
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
        return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    case S_OK:
        return "S_OK";
    default:
        return "another HRESULT";
    }
}

bool dx_Check(HRESULT hr, const char *what, const char *file, int line)
{
    if (SUCCEEDED(hr)) {
        return true;
    }
    if (g_dx.deviceLost) {
        /* the reason was logged once; the calls that keep failing after it
         * are counted, not logged (one line per call per frame otherwise) */
        g_dx.lostFailures++;
        return false;
    }
    fprintf(stderr, "rhi_d3d12: %s failed (HRESULT 0x%08lx) at %s:%d\n", what, (unsigned long)hr,
            file, line);
    /* removal shows as the call's own code (Present, Close, Signal) or as
     * another failure on a device whose removed reason is set */
    HRESULT reason = g_dx.device ? ID3D12Device_GetDeviceRemovedReason(g_dx.device) : S_OK;
    if (dx_IsLossCode(hr) || reason != S_OK) {
        g_dx.deviceLost = true;
        fprintf(stderr,
                "rhi_d3d12: the device was lost (%s); removed reason 0x%08lx (%s). Rendering "
                "stops; the session ends.\n",
                dx_LossName(hr), (unsigned long)reason, dx_LossName(reason));
    }
    dx_DrainMessages();
    return false;
}

/* ------------------------------------------------------------- debug layer */
void dx_DrainMessages(void)
{
    if (!g_dx.info) {
        return;
    }
    UINT64 n = ID3D12InfoQueue_GetNumStoredMessages(g_dx.info);
    for (UINT64 i = 0; i < n; i++) {
        SIZE_T len = 0;
        if (FAILED(ID3D12InfoQueue_GetMessage(g_dx.info, i, NULL, &len)) || len == 0) {
            continue;
        }
        D3D12_MESSAGE *m = malloc(len);
        if (!m) {
            continue;
        }
        if (SUCCEEDED(ID3D12InfoQueue_GetMessage(g_dx.info, i, m, &len))) {
            const char *sev = "info";
            if (m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                m->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
                g_dx.debugErrors++;
                sev = m->Severity == D3D12_MESSAGE_SEVERITY_ERROR ? "error" : "corruption";
            } else if (m->Severity == D3D12_MESSAGE_SEVERITY_WARNING) {
                sev = "warning";
            }
            fprintf(stderr, "rhi_d3d12 debug %s (id %d): %.*s\n", sev, (int)m->ID,
                    (int)m->DescriptionByteLength, m->pDescription);
        }
        free(m);
    }
    ID3D12InfoQueue_ClearStoredMessages(g_dx.info);
}

uint32_t rhi_d3d12_DebugErrorCount(void)
{
    return g_dx.debugErrors;
}

bool rhi_d3d12_DebugLayerActive(void)
{
    return g_dx.info != NULL;
}

bool rhi_d3d12_IsWarp(void)
{
    return g_dx.warp;
}

static bool dx_DebugWanted(const RhiDeviceDesc *desc)
{
    bool debug = desc->debugLayers;
#ifdef ICO_RHI_DEBUG_DEFAULT
    debug = true;
#endif
    const char *env = getenv("ICO_D3D12_DEBUG");
    if (env && *env) {
        debug = env[0] != '0';
    }
    return debug;
}

static void dx_SetupInfoQueue(void)
{
    if (FAILED(
            ID3D12Device_QueryInterface(g_dx.device, &IID_ID3D12InfoQueue, (void **)&g_dx.info))) {
        g_dx.info = NULL;
        return;
    }
    /* Kept: corruption, errors, warnings.  Denied: the performance notes on
     * clear values (render targets are created without an optimised clear
     * value: the game clears to many colours). */
    D3D12_MESSAGE_SEVERITY deny[] = {D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE};
    D3D12_MESSAGE_ID denyIds[] = {D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                                  D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE};
    D3D12_INFO_QUEUE_FILTER f;
    memset(&f, 0, sizeof(f));
    f.DenyList.NumSeverities = (UINT)(sizeof(deny) / sizeof(deny[0]));
    f.DenyList.pSeverityList = deny;
    f.DenyList.NumIDs = (UINT)(sizeof(denyIds) / sizeof(denyIds[0]));
    f.DenyList.pIDList = denyIds;
    ID3D12InfoQueue_PushStorageFilter(g_dx.info, &f);
    ID3D12InfoQueue_SetMessageCountLimit(g_dx.info, 4096);
}

/* ------------------------------------------------------- runtime loading
 * d3d12.dll and dxgi.dll are loaded at run time, so the Vulkan path of the
 * same executable never depends on them. */
typedef HRESULT(WINAPI *PFN_CreateDXGIFactory2)(UINT, REFIID, void **);
typedef HRESULT(WINAPI *PFN_CreateDXGIFactory1)(REFIID, void **);

static bool dx_LoadRuntime(bool debug)
{
    if (!g_dx.d3d12Dll) {
        g_dx.d3d12Dll = LoadLibraryA("d3d12.dll");
    }
    if (!g_dx.dxgiDll) {
        g_dx.dxgiDll = LoadLibraryA("dxgi.dll");
    }
    if (!g_dx.d3d12Dll || !g_dx.dxgiDll) {
        DX_LOG("d3d12.dll or dxgi.dll not found (Windows 10 or later is required)");
        return false;
    }
    g_dx.createDevice =
        (PFN_D3D12_CREATE_DEVICE)(void *)GetProcAddress(g_dx.d3d12Dll, "D3D12CreateDevice");
    g_dx.getDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)(void *)GetProcAddress(
        g_dx.d3d12Dll, "D3D12GetDebugInterface");
    g_dx.serializeRootSignature = (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)(void *)GetProcAddress(
        g_dx.d3d12Dll, "D3D12SerializeRootSignature");
    if (!g_dx.createDevice || !g_dx.serializeRootSignature) {
        DX_LOG("d3d12.dll lacks D3D12CreateDevice or D3D12SerializeRootSignature");
        return false;
    }
    PFN_CreateDXGIFactory2 f2 =
        (PFN_CreateDXGIFactory2)(void *)GetProcAddress(g_dx.dxgiDll, "CreateDXGIFactory2");
    HRESULT hr;
    if (f2) {
        hr = f2(debug ? DXGI_CREATE_FACTORY_DEBUG : 0, &IID_IDXGIFactory4, (void **)&g_dx.factory);
        if (FAILED(hr) && debug) {
            /* the DXGI debug layer comes with the same optional feature */
            hr = f2(0, &IID_IDXGIFactory4, (void **)&g_dx.factory);
        }
    } else {
        PFN_CreateDXGIFactory1 f1 =
            (PFN_CreateDXGIFactory1)(void *)GetProcAddress(g_dx.dxgiDll, "CreateDXGIFactory1");
        hr = f1 ? f1(&IID_IDXGIFactory4, (void **)&g_dx.factory) : E_FAIL;
    }
    return DX_CHECK(hr);
}

/* ------------------------------------------------------- adapter selection */
static void dx_AdapterName(IDXGIAdapter1 *a, char *out, size_t size)
{
    DXGI_ADAPTER_DESC1 d;
    memset(&d, 0, sizeof(d));
    IDXGIAdapter1_GetDesc1(a, &d);
    if (!WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, out, (int)size, NULL, NULL)) {
        snprintf(out, size, "adapter %04x:%04x", d.VendorId, d.DeviceId);
    }
}

static bool dx_AdapterOk(IDXGIAdapter1 *a)
{
    return SUCCEEDED(
        g_dx.createDevice((IUnknown *)a, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL));
}

/* The i-th hardware adapter, discrete first when DXGI 1.6 can order them
 * (EnumAdapterByGpuPreference), else in EnumAdapters1 order. */
static IDXGIAdapter1 *dx_EnumHardware(UINT i)
{
    IDXGIAdapter1 *a = NULL;
    IDXGIFactory6 *f6 = NULL;
    if (SUCCEEDED(IDXGIFactory4_QueryInterface(g_dx.factory, &IID_IDXGIFactory6, (void **)&f6))) {
        HRESULT hr = IDXGIFactory6_EnumAdapterByGpuPreference(
            f6, i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &IID_IDXGIAdapter1, (void **)&a);
        IDXGIFactory6_Release(f6);
        return SUCCEEDED(hr) ? a : NULL;
    }
    return SUCCEEDED(IDXGIFactory4_EnumAdapters1(g_dx.factory, i, &a)) ? a : NULL;
}

static bool dx_IsSoftware(IDXGIAdapter1 *a)
{
    DXGI_ADAPTER_DESC1 d;
    memset(&d, 0, sizeof(d));
    IDXGIAdapter1_GetDesc1(a, &d);
    return (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

static bool dx_PickAdapter(void)
{
    /* ICO_D3D12_ADAPTER=warp, an index into the list below, or a substring
     * of the adapter's name forces a pick */
    const char *force = getenv("ICO_D3D12_ADAPTER");
    bool forceWarp = force && (_stricmp(force, "warp") == 0);
    IDXGIAdapter1 *pick = NULL;
    if (!forceWarp) {
        for (UINT i = 0;; i++) {
            IDXGIAdapter1 *a = dx_EnumHardware(i);
            if (!a) {
                break;
            }
            char name[128];
            dx_AdapterName(a, name, sizeof(name));
            bool ok = !dx_IsSoftware(a) && dx_AdapterOk(a);
            DX_LOG("adapter %u: %s%s", i, name, ok ? "" : " (unsuitable: no feature level 11_0)");
            bool forced = false;
            if (force && *force && ok) {
                char idx[16];
                snprintf(idx, sizeof(idx), "%u", i);
                forced = strcmp(force, idx) == 0 || strstr(name, force) != NULL;
            }
            if (ok && (forced || (!pick && !(force && *force)))) {
                if (pick) {
                    IDXGIAdapter1_Release(pick);
                }
                IDXGIAdapter1_AddRef(a);
                pick = a;
            }
            IDXGIAdapter1_Release(a);
        }
        if (!pick && force && *force) {
            DX_LOG("ICO_D3D12_ADAPTER=%s matched no adapter", force);
        }
    }
    if (!pick) {
        /* WARP: the software rasteriser every Windows 10 ships */
        if (!DX_CHECK(
                IDXGIFactory4_EnumWarpAdapter(g_dx.factory, &IID_IDXGIAdapter1, (void **)&pick))) {
            return false;
        }
        g_dx.warp = true;
    }
    g_dx.adapter = pick;
    dx_AdapterName(pick, g_dx.adapterName, sizeof(g_dx.adapterName));
    return true;
}

/* --------------------------------------------------------------- heaps */
static bool dx_CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool visible,
                          ID3D12DescriptorHeap **out)
{
    D3D12_DESCRIPTOR_HEAP_DESC d = {
        type, count,
        visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    return DX_CHECK(ID3D12Device_CreateDescriptorHeap(g_dx.device, &d, &IID_ID3D12DescriptorHeap,
                                                      (void **)out));
}

static bool dx_CreateHeaps(void)
{
    if (!dx_CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, DX_RES_HEAP_SIZE, true,
                       &g_dx.resHeap) ||
        !dx_CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, DX_SMP_HEAP_SIZE, true, &g_dx.smpHeap) ||
        !dx_CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, DX_RTV_SLOTS, false, &g_dx.rtvHeap) ||
        !dx_CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DX_DSV_SLOTS, false, &g_dx.dsvHeap)) {
        return false;
    }
    g_dx.resInc = ID3D12Device_GetDescriptorHandleIncrementSize(
        g_dx.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_dx.smpInc = ID3D12Device_GetDescriptorHandleIncrementSize(g_dx.device,
                                                                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    g_dx.rtvInc =
        ID3D12Device_GetDescriptorHandleIncrementSize(g_dx.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    g_dx.dsvInc =
        ID3D12Device_GetDescriptorHandleIncrementSize(g_dx.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    g_dx.resCpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_dx.resHeap);
    g_dx.resGpu = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(g_dx.resHeap);
    g_dx.smpCpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_dx.smpHeap);
    g_dx.smpGpu = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(g_dx.smpHeap);
    g_dx.rtvCpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_dx.rtvHeap);
    g_dx.dsvCpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_dx.dsvHeap);
    return d3dp_SlotsInit(&g_dx.rtvSlots, DX_RTV_SLOTS) &&
           d3dp_SlotsInit(&g_dx.dsvSlots, DX_DSV_SLOTS);
}

static void dx_FillLimits(void)
{
    RhiLimits *o = &g_dx.limits;
    o->uniformAlign = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT; /* 256 */
    o->maxTextureSize = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;         /* 16384 */
    o->dualSourceBlend = true; /* every D3D12 device (feature level 11_0) */
    o->stencilWrap = true;
    o->depthReadback = true;
    o->copyRowPitchAlign = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;   /* 256 */
    o->copyOffsetAlign = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT; /* 512 */
    /* R7a's fields: textures carry mipLevels, every barrier spans all
     * subresources, rhi_CmdCopyBufferToTexture writes the given level, and
     * samplers apply the mip filter, LOD range and anisotropy (d3d12_cmd.c,
     * d3d12_resource.c); 16x anisotropy at every feature level */
    o->textureMips = true;
    o->maxAnisotropy = (float)D3D12_MAX_MAXANISOTROPY;
    /* package PA: root CBVs cost 2 of the root signature's 64 DWORDs each;
     * 8 of them leave room for every group's tables */
    o->maxDynamicUniforms = 8;
    /* a structured-buffer SRV of 16-byte elements:
     * D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP (27) elements */
    o->maxStorageRange = (uint64_t)16u << 27;
}

/* ------------------------------------------------------------- lifecycle */
#define DX_RELEASE(p)                                                                              \
    do {                                                                                           \
        if (p) {                                                                                   \
            IUnknown_Release((IUnknown *)(p));                                                     \
            (p) = NULL;                                                                            \
        }                                                                                          \
    } while (0)

bool rhi_Init(const RhiDeviceDesc *desc)
{
    if (g_dx.initialised) {
        return true;
    }
    HMODULE keepD3d = g_dx.d3d12Dll, keepDxgi = g_dx.dxgiDll;
    memset(&g_dx, 0, sizeof(g_dx));
    g_dx.d3d12Dll = keepD3d; /* kept loaded across rhi_Shutdown */
    g_dx.dxgiDll = keepDxgi;
    bool debug = dx_DebugWanted(desc);
    if (!dx_LoadRuntime(debug)) {
        rhi_Shutdown();
        return false;
    }
    if (debug) {
        ID3D12Debug *dbg = NULL;
        if (g_dx.getDebugInterface &&
            SUCCEEDED(g_dx.getDebugInterface(&IID_ID3D12Debug, (void **)&dbg))) {
            ID3D12Debug_EnableDebugLayer(dbg);
            ID3D12Debug_Release(dbg);
            g_dx.debugLayer = true;
        } else {
            DX_LOG("debugLayers: the D3D12 debug layer is not installed (Windows optional "
                   "feature \"Graphics Tools\"); continuing without");
        }
    }
    if (!dx_PickAdapter()) {
        rhi_Shutdown();
        return false;
    }
    if (!DX_CHECK(g_dx.createDevice((IUnknown *)g_dx.adapter, D3D_FEATURE_LEVEL_11_0,
                                    &IID_ID3D12Device, (void **)&g_dx.device))) {
        rhi_Shutdown();
        return false;
    }
    if (g_dx.debugLayer) {
        dx_SetupInfoQueue();
    }
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE,
                                   0};
    if (!DX_CHECK(ID3D12Device_CreateCommandQueue(g_dx.device, &qd, &IID_ID3D12CommandQueue,
                                                  (void **)&g_dx.queue)) ||
        !DX_CHECK(ID3D12Device_CreateFence(g_dx.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                                           (void **)&g_dx.fence))) {
        rhi_Shutdown();
        return false;
    }
    g_dx.fenceEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_dx.fenceEvent || !dx_CreateHeaps()) {
        rhi_Shutdown();
        return false;
    }
    dx_FillLimits();
    if (!d3dp_PoolInit(&g_dx.buffers, "buffer", 4096, sizeof(DxBuffer)) ||
        !d3dp_PoolInit(&g_dx.textures, "texture", 8192, sizeof(DxTexture)) ||
        !d3dp_PoolInit(&g_dx.samplers, "sampler", DX_SMP_PERSISTENT, sizeof(DxSampler)) ||
        !d3dp_PoolInit(&g_dx.shaders, "shader", 512, sizeof(DxShader)) ||
        !d3dp_PoolInit(&g_dx.layouts, "bind group layout", 64, sizeof(DxLayout)) ||
        !d3dp_PoolInit(&g_dx.pipelines, "pipeline", 1024, sizeof(DxPipeline))) {
        rhi_Shutdown();
        return false;
    }
    if (!dx_FramesInit()) {
        rhi_Shutdown();
        return false;
    }
    {
        /* the default sampler of null tables, after the per-RhiSampler ones */
        D3D12_SAMPLER_DESC sd;
        memset(&sd, 0, sizeof(sd));
        sd.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sd.MaxAnisotropy = 1;
        sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        ID3D12Device_CreateSampler(g_dx.device, &sd,
                                   dx_Cpu(g_dx.smpCpu, g_dx.smpInc, DX_SMP_DEFAULT));
    }
    g_dx.initialised = true;
    if (desc->sdlWindow) {
#ifdef ICO_RHI_HAVE_SDL
        SDL_Window *w = (SDL_Window *)desc->sdlWindow;
        g_dx.window = w;
        g_dx.hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(w),
                                                 SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(w, &pw, &ph);
        if (!g_dx.hwnd || !dx_SwapchainCreate((uint32_t)pw, (uint32_t)ph, desc->vsync)) {
            DX_LOG("no swapchain on the SDL window (HWND %p)", (void *)g_dx.hwnd);
            rhi_Shutdown();
            return false;
        }
#else
        DX_LOG("built without SDL3: no window support");
        rhi_Shutdown();
        return false;
#endif
    }
    DX_LOG("%s (D3D12, feature level 11_0%s)%s", g_dx.adapterName, g_dx.warp ? ", WARP" : "",
           g_dx.swapchain ? "" : ", headless");
    dx_DrainMessages();
    return true;
}

void rhi_Shutdown(void)
{
    if (g_dx.device && g_dx.queue && g_dx.fence) {
        dx_WaitFence(dx_Signal());
    }
    dx_SwapchainDestroy();
    dx_FramesShutdown();
    dx_ReleaseAllObjects();
    dx_ReleaseRootSignatures();
    d3dp_PoolFree(&g_dx.buffers);
    d3dp_PoolFree(&g_dx.textures);
    d3dp_PoolFree(&g_dx.samplers);
    d3dp_PoolFree(&g_dx.shaders);
    d3dp_PoolFree(&g_dx.layouts);
    d3dp_PoolFree(&g_dx.pipelines);
    d3dp_SlotsFree(&g_dx.rtvSlots);
    d3dp_SlotsFree(&g_dx.dsvSlots);
    DX_RELEASE(g_dx.resHeap);
    DX_RELEASE(g_dx.smpHeap);
    DX_RELEASE(g_dx.rtvHeap);
    DX_RELEASE(g_dx.dsvHeap);
    DX_RELEASE(g_dx.fence);
    DX_RELEASE(g_dx.queue);
    dx_DrainMessages();
    DX_RELEASE(g_dx.info);
    DX_RELEASE(g_dx.device);
    DX_RELEASE(g_dx.adapter);
    DX_RELEASE(g_dx.factory);
    if (g_dx.fenceEvent) {
        CloseHandle(g_dx.fenceEvent);
    }
    uint32_t errors = g_dx.debugErrors;
    bool warp = g_dx.warp;
    HMODULE d3d = g_dx.d3d12Dll, dxgi = g_dx.dxgiDll;
    memset(&g_dx, 0, sizeof(g_dx));
    g_dx.debugErrors = errors; /* readable after shutdown, for tests */
    g_dx.warp = warp;
    g_dx.d3d12Dll = d3d;
    g_dx.dxgiDll = dxgi;
}

RhiBackendKind rhi_Backend(void)
{
    return RHI_BACKEND_D3D12;
}

const RhiLimits *rhi_Limits(void)
{
    return &g_dx.limits;
}

const char *rhi_AdapterName(void)
{
    return g_dx.adapterName;
}

bool rhi_DeviceLost(void)
{
    return g_dx.deviceLost;
}

/* Package P1's performance entry points (rhi.h).  Not implemented on D3D12
 * yet: the counters read zero, timestamps are unsupported (nothing is
 * written, nothing read back) and there is no mailbox mode (DXGI's flip
 * model with sync interval 1 is FIFO). */
void rhi_GetStats(RhiStats *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}

bool rhi_TimestampsSupported(void)
{
    return false;
}

void rhi_CmdWriteTimestamp(RhiCommandList cl, uint32_t index)
{
    (void)cl;
    (void)index;
}

uint32_t rhi_ReadTimestamps(uint64_t *ns, uint32_t max)
{
    (void)ns;
    (void)max;
    return 0;
}

void rhi_PreferMailbox(bool on)
{
    (void)on;
}

bool rhi_PresentMailbox(void)
{
    return false;
}

void rhi_SetPipelineCachePath(const char *path)
{
    (void)path; /* the driver keeps its own shader cache */
}

/* The rhi_CreateBackend entry (port/rhi/rhi_backend.h). */
RHI_BACKEND_DEFINE(rhi_backend_d3d12, "d3d12");
