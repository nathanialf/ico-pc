/* d3d12_device.c: runtime loading, adapter selection, the device and its
 * queue, the debug layer, descriptor heaps, limits, rhi_init/rhi_shutdown
 * of the D3D12 backend. */
#include "d3d12_internal.h"
#include "rhi_d3d12.h"
#include "../rhi_backend.h"
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RHI_HAVE_SDL
#include <SDL3/SDL.h>
#endif

DxState g_dx;

static bool dx_is_loss_code(HRESULT hr)
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

static const char *dx_loss_name(HRESULT hr)
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

bool dx_check(HRESULT hr, const char *what, const char *file, int line)
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
    if (dx_is_loss_code(hr) || reason != S_OK) {
        g_dx.deviceLost = true;
        fprintf(stderr,
                "rhi_d3d12: the device was lost (%s); removed reason 0x%08lx (%s). Rendering "
                "stops; the session ends.\n",
                dx_loss_name(hr), (unsigned long)reason, dx_loss_name(reason));
        dx_lost_report();
    }
    dx_drain_messages();
    return false;
}

/* ------------------------------------------------------------ device loss
 * The report dx_check prints once, at the loss (docs/port/RHI.md, "Device
 * loss"): the last submissions with the frames they carried and whether
 * the GPU finished them, and with gpu_checkpoints DRED's breadcrumbs (each
 * command list the GPU had not finished: its last operation done, the ones
 * after it, the checkpoint markers among them) and its page-fault report
 * (the faulting address and the resources at it, alive or freed). */
static bool dx_env_on(const char *name)
{
    const char *v = getenv(name);
    return v && (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0 ||
                 strcmp(v, "yes") == 0);
}

/* DRED on before the device is made (gpu_checkpoints): the breadcrumbs, the
 * markers' contexts and the page-fault report, forced on whatever the
 * system's settings say */
static void dx_dred_enable(void)
{
    if (!dx_env_on("ICO_GPU_CHECKPOINTS") || !g_dx.getDebugInterface) {
        return;
    }
    ID3D12DeviceRemovedExtendedDataSettings1 *s1 = NULL;
    ID3D12DeviceRemovedExtendedDataSettings *s0 = NULL;
    if (SUCCEEDED(
            g_dx.getDebugInterface(&IID_ID3D12DeviceRemovedExtendedDataSettings1, (void **)&s1))) {
        ID3D12DeviceRemovedExtendedDataSettings1_SetAutoBreadcrumbsEnablement(
            s1, D3D12_DRED_ENABLEMENT_FORCED_ON);
        ID3D12DeviceRemovedExtendedDataSettings1_SetPageFaultEnablement(
            s1, D3D12_DRED_ENABLEMENT_FORCED_ON);
        ID3D12DeviceRemovedExtendedDataSettings1_SetBreadcrumbContextEnablement(
            s1, D3D12_DRED_ENABLEMENT_FORCED_ON);
        ID3D12DeviceRemovedExtendedDataSettings1_Release(s1);
        g_dx.dred = true;
    } else if (SUCCEEDED(g_dx.getDebugInterface(&IID_ID3D12DeviceRemovedExtendedDataSettings,
                                                (void **)&s0))) {
        ID3D12DeviceRemovedExtendedDataSettings_SetAutoBreadcrumbsEnablement(
            s0, D3D12_DRED_ENABLEMENT_FORCED_ON);
        ID3D12DeviceRemovedExtendedDataSettings_SetPageFaultEnablement(
            s0, D3D12_DRED_ENABLEMENT_FORCED_ON);
        ID3D12DeviceRemovedExtendedDataSettings_Release(s0);
        g_dx.dred = true;
    }
    DX_LOG("loss diagnostics: DRED breadcrumbs and page faults %s (gpu_checkpoints)",
           g_dx.dred ? "on" : "asked for, not offered by this Windows");
}

/* a DRED context string (UTF-16) for the log */
static const char *dx_narrow(const WCHAR *w, char *out, int size)
{
    if (!w || WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, NULL, NULL) <= 0) {
        snprintf(out, (size_t)size, "?");
    }
    return out;
}

static void dx_report_breadcrumbs(const D3D12_AUTO_BREADCRUMB_NODE1 *node)
{
    char ctxText[64];
    uint32_t lists = 0;
    for (; node; node = node->pNext) {
        const UINT done = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
        if (done >= node->BreadcrumbCount) {
            continue; /* finished: not the one that hung */
        }
        lists++;
        DX_LOG("  command list \"%s\": %u of %u operations done; the next ones:",
               node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "?", done,
               node->BreadcrumbCount);
        /* the operation it stopped at and up to 7 after it, each with the
         * checkpoint marker that came before it (its context) */
        for (UINT i = done; i < node->BreadcrumbCount && i < done + 8; i++) {
            const WCHAR *ctx = NULL;
            for (UINT k = 0; k < node->BreadcrumbContextsCount; k++) {
                if (node->pBreadcrumbContexts[k].BreadcrumbIndex == i) {
                    ctx = node->pBreadcrumbContexts[k].pContextString;
                }
            }
            if (ctx) {
                DX_LOG("    op %u: %d (%s)", i, (int)node->pCommandHistory[i],
                       dx_narrow(ctx, ctxText, (int)sizeof(ctxText)));
            } else {
                DX_LOG("    op %u: %d", i, (int)node->pCommandHistory[i]);
            }
        }
        /* the marker before the stop: the pass it was in */
        for (UINT k = node->BreadcrumbContextsCount; k-- > 0;) {
            if (node->pBreadcrumbContexts[k].BreadcrumbIndex <= done) {
                DX_LOG("    last marker before it: %s (op %u)",
                       dx_narrow(node->pBreadcrumbContexts[k].pContextString, ctxText,
                                 (int)sizeof(ctxText)),
                       node->pBreadcrumbContexts[k].BreadcrumbIndex);
                break;
            }
        }
    }
    DX_LOG("loss report: %u command list%s not finished (operation numbers: "
           "D3D12_AUTO_BREADCRUMB_OP)",
           lists, lists == 1 ? "" : "s");
}

static void dx_report_allocations(const char *what, const D3D12_DRED_ALLOCATION_NODE1 *n)
{
    for (uint32_t k = 0; n && k < 16; n = n->pNext, k++) {
        DX_LOG("    %s: \"%s\" (allocation type %d)", what, n->ObjectNameA ? n->ObjectNameA : "?",
               (int)n->AllocationType);
    }
}

void dx_lost_report(void)
{
    static bool s_done;
    if (s_done || !g_dx.device) {
        return;
    }
    s_done = true;
    /* a removed device's fence reads UINT64_MAX (ID3D12Fence::GetCompletedValue):
     * then no submission's state is known */
    const uint64_t reached = g_dx.fence ? ID3D12Fence_GetCompletedValue(g_dx.fence) : UINT64_MAX;
    const bool known = reached != UINT64_MAX;
    const uint32_t n = g_dx.submitCount < DX_SUBMIT_HISTORY ? g_dx.submitCount : DX_SUBMIT_HISTORY;
    DX_LOG("loss report: the last %u submissions (fence reached %s%llu):", n,
           known ? "" : "unknown, ", (unsigned long long)(known ? reached : 0ull));
    for (uint32_t k = 0; k < n; k++) {
        const uint32_t i = (g_dx.submitCount - n + k) % DX_SUBMIT_HISTORY;
        DX_LOG("  fence %llu: replay %u, frame %u, %s", (unsigned long long)g_dx.submits[i].value,
               g_dx.submits[i].replay, g_dx.submits[i].frame,
               !known                             ? "state unknown"
               : g_dx.submits[i].value <= reached ? "finished"
                                                  : "not finished");
    }
    if (!g_dx.dred) {
        DX_LOG("loss report: no breadcrumbs (gpu_checkpoints = true in ico-pc.ini turns DRED on)");
        return;
    }
    ID3D12DeviceRemovedExtendedData1 *dred = NULL;
    if (FAILED(ID3D12Device_QueryInterface(g_dx.device, &IID_ID3D12DeviceRemovedExtendedData1,
                                           (void **)&dred))) {
        DX_LOG("loss report: DRED's report is not available");
        return;
    }
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 bc;
    memset(&bc, 0, sizeof(bc));
    if (SUCCEEDED(ID3D12DeviceRemovedExtendedData1_GetAutoBreadcrumbsOutput1(dred, &bc))) {
        dx_report_breadcrumbs(bc.pHeadAutoBreadcrumbNode);
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT1 pf;
    memset(&pf, 0, sizeof(pf));
    if (SUCCEEDED(ID3D12DeviceRemovedExtendedData1_GetPageFaultAllocationOutput1(dred, &pf))) {
        DX_LOG("loss report: page fault at 0x%016llx%s", (unsigned long long)pf.PageFaultVA,
               pf.PageFaultVA ? "" : " (none: not a page fault)");
        dx_report_allocations("alive there", pf.pHeadExistingAllocationNode);
        dx_report_allocations("freed there lately", pf.pHeadRecentFreedAllocationNode);
    }
    ID3D12DeviceRemovedExtendedData1_Release(dred);
    fflush(stderr);
}

/* ------------------------------------------------------------- debug layer */
void dx_drain_messages(void)
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

uint32_t rhi_d3d12_debug_error_count(void)
{
    return g_dx.debugErrors;
}

bool rhi_d3d12_debug_layer_active(void)
{
    return g_dx.info != NULL;
}

bool rhi_d3d12_is_warp(void)
{
    return g_dx.warp;
}

static bool dx_debug_wanted(const RhiDeviceDesc *desc)
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

static void dx_setup_info_queue(void)
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

/* ReShade's Direct3D 12 install is a dxgi.dll (or
 * d3d12.dll) beside the program, which LoadLibraryA finds before the
 * system's (no SetDefaultDllDirectories).  A module whose file is outside
 * the system directory (System32; SysWOW64 for a 32-bit program) is taken
 * for it.  Kept outside g_dx: rhi_shutdown keeps the modules loaded. */
static const char *s_dxInjector;

static bool dx_in_system_dir(HMODULE m)
{
    char file[MAX_PATH], dir[MAX_PATH];
    const DWORD n = GetModuleFileNameA(m, file, (DWORD)sizeof(file));
    if (n == 0 || n >= sizeof(file)) {
        return true; /* unknown: not reported */
    }
    UINT(WINAPI *const dirs[2])(LPSTR, UINT) = {GetSystemDirectoryA, GetSystemWow64DirectoryA};
    for (int i = 0; i < 2; i++) {
        const UINT d = dirs[i](dir, (UINT)sizeof(dir));
        if (d == 0 || d >= sizeof(dir)) {
            continue;
        }
        /* the file is dir\name, case-insensitively */
        if (n > d && _strnicmp(file, dir, d) == 0 && (file[d] == '\\' || file[d] == '/') &&
            !strpbrk(file + d + 1, "\\/")) {
            return true;
        }
    }
    return false;
}

static void dx_scan_injector(void)
{
    s_dxInjector = NULL;
    if ((g_dx.dxgiDll && !dx_in_system_dir(g_dx.dxgiDll)) ||
        (g_dx.d3d12Dll && !dx_in_system_dir(g_dx.d3d12Dll))) {
        s_dxInjector = "ReShade";
    }
}

const char *rhi_injector_name(void)
{
    return s_dxInjector;
}

const char *rhi_overlay_name(void)
{
    return NULL;
}

static bool dx_load_runtime(bool debug)
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
    dx_scan_injector(); /* an effects program beside the executable */
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

static void dx_video_memory(void);

/* ------------------------------------------------------- adapter selection */
static void dx_adapter_name(IDXGIAdapter1 *a, char *out, size_t size)
{
    DXGI_ADAPTER_DESC1 d;
    memset(&d, 0, sizeof(d));
    IDXGIAdapter1_GetDesc1(a, &d);
    if (!WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, out, (int)size, NULL, NULL)) {
        snprintf(out, size, "adapter %04x:%04x", d.VendorId, d.DeviceId);
    }
}

/* "; driver version 32.0.15.8157" for the adapter line: the user-mode
 * driver's version, four 16-bit parts (CheckInterfaceSupport with
 * IDXGIDevice); "" when the adapter does not say */
static char s_driverText[48];

static void dx_driver_text(IDXGIAdapter1 *a)
{
    LARGE_INTEGER umd;
    s_driverText[0] = '\0';
    if (SUCCEEDED(IDXGIAdapter1_CheckInterfaceSupport(a, &IID_IDXGIDevice, &umd))) {
        const unsigned long long v = (unsigned long long)umd.QuadPart;
        snprintf(s_driverText, sizeof(s_driverText), "; driver version %u.%u.%u.%u",
                 (unsigned)((v >> 48) & 0xffff), (unsigned)((v >> 32) & 0xffff),
                 (unsigned)((v >> 16) & 0xffff), (unsigned)(v & 0xffff));
    }
}

static bool dx_adapter_ok(IDXGIAdapter1 *a)
{
    return SUCCEEDED(
        g_dx.createDevice((IUnknown *)a, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL));
}

/* The i-th hardware adapter, discrete first when DXGI 1.6 can order them
 * (EnumAdapterByGpuPreference), else in EnumAdapters1 order. */
static IDXGIAdapter1 *dx_enum_hardware(UINT i)
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

static bool dx_is_software(IDXGIAdapter1 *a)
{
    DXGI_ADAPTER_DESC1 d;
    memset(&d, 0, sizeof(d));
    IDXGIAdapter1_GetDesc1(a, &d);
    return (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

static bool dx_pick_adapter(void)
{
    /* ICO_D3D12_ADAPTER=warp, an index into the list below, or a substring
     * of the adapter's name forces a pick */
    const char *force = getenv("ICO_D3D12_ADAPTER");
    bool forceWarp = force && (_stricmp(force, "warp") == 0);
    IDXGIAdapter1 *pick = NULL;
    if (!forceWarp) {
        for (UINT i = 0;; i++) {
            IDXGIAdapter1 *a = dx_enum_hardware(i);
            if (!a) {
                break;
            }
            char name[128];
            dx_adapter_name(a, name, sizeof(name));
            bool ok = !dx_is_software(a) && dx_adapter_ok(a);
            DX_LOG("adapter %u: %s%s", i, name,
                   ok ? "" : " (unsuitable: a software adapter, or no feature level 11_0)");
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
    if (pick &&
        FAILED(IDXGIAdapter1_QueryInterface(pick, &IID_IDXGIAdapter3, (void **)&g_dx.adapter3))) {
        g_dx.adapter3 = NULL;
    }
    dx_adapter_name(pick, g_dx.adapterName, sizeof(g_dx.adapterName));
    dx_driver_text(pick);
    return true;
}

/* --------------------------------------------------------------- heaps */
static bool dx_create_heap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool visible,
                           ID3D12DescriptorHeap **out)
{
    D3D12_DESCRIPTOR_HEAP_DESC d = {
        type, count,
        visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    return DX_CHECK(ID3D12Device_CreateDescriptorHeap(g_dx.device, &d, &IID_ID3D12DescriptorHeap,
                                                      (void **)out));
}

static bool dx_create_heaps(void)
{
    if (!dx_create_heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, DX_RES_HEAP_SIZE, true,
                        &g_dx.resHeap) ||
        !dx_create_heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, DX_SMP_HEAP_SIZE, true,
                        &g_dx.smpHeap) ||
        !dx_create_heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, DX_RTV_SLOTS, false, &g_dx.rtvHeap) ||
        !dx_create_heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DX_DSV_SLOTS, false, &g_dx.dsvHeap)) {
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
    return d3dp_slots_init(&g_dx.rtvSlots, DX_RTV_SLOTS) &&
           d3dp_slots_init(&g_dx.dsvSlots, DX_DSV_SLOTS);
}

/* The sample counts (2, 4, 8) every multisampled target the renderer makes
 * takes (RhiLimits.msaaCounts): a quality level for RGBA8 UNORM and UINT
 * and the scene's depth-stencil */
static uint32_t dx_msaa_counts(void)
{
    static const DXGI_FORMAT fmts[3] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UINT,
                                        DXGI_FORMAT_D32_FLOAT_S8X24_UINT};
    uint32_t counts = 1u;
    for (uint32_t n = 2u; n <= 8u; n *= 2u) {
        bool all = true;
        for (int i = 0; i < 3 && all; i++) {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q;
            memset(&q, 0, sizeof(q));
            q.Format = fmts[i];
            q.SampleCount = n;
            q.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
            all = SUCCEEDED(ID3D12Device_CheckFeatureSupport(
                      g_dx.device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) &&
                  q.NumQualityLevels > 0;
        }
        if (all) {
            counts |= n;
        }
    }
    DX_LOG("multisampling: sample counts 0x%x", counts);
    return counts;
}

/* The destination read (RhiLimits.rov): rasterizer-ordered views
 * (D3D12_OPTIONS.ROVsSupported) and typed UAV loads and stores of
 * R8G8B8A8_UNORM (the read-modify-write of the target: a UAV typed load of
 * it is one of the "additional formats", TypedUAVLoadAdditionalFormats,
 * confirmed by the format's own support bits).  ICO_DX_FAKE_NO_DST=1
 * reports none. */
static bool dx_rov_supported(void)
{
    const char *noDst = getenv("ICO_DX_FAKE_NO_DST");
    if (noDst && noDst[0] && noDst[0] != '0') {
        DX_LOG("destination read: off (ICO_DX_FAKE_NO_DST)");
        return false;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS o;
    memset(&o, 0, sizeof(o));
    if (FAILED(ID3D12Device_CheckFeatureSupport(g_dx.device, D3D12_FEATURE_D3D12_OPTIONS, &o,
                                                sizeof(o)))) {
        return false;
    }
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs;
    memset(&fs, 0, sizeof(fs));
    fs.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    const bool fmt = SUCCEEDED(ID3D12Device_CheckFeatureSupport(
                         g_dx.device, D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs))) &&
                     (fs.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) &&
                     (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) &&
                     (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
    const bool ok = o.ROVsSupported && o.TypedUAVLoadAdditionalFormats && fmt;
    DX_LOG("destination read: ROVs %s, typed UAV loads of RGBA8 %s", o.ROVsSupported ? "yes" : "no",
           o.TypedUAVLoadAdditionalFormats && fmt ? "yes" : "no");
    return ok;
}

static void dx_fill_limits(void)
{
    RhiLimits *o = &g_dx.limits;
    o->uniformAlign = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT; /* 256 */
    o->maxTextureSize = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;         /* 16384 */
    o->maxRenderTargetSize = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;    /* 16384 */
    o->dualSourceBlend = true; /* every D3D12 device (feature level 11_0) */
    o->stencilWrap = true;
    o->depthReadback = true;
    o->depthStencilFormatName = "D32S8";
    o->depthSampled = true;
    o->tiler = false;
    o->copyRowPitchAlign = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;   /* 256 */
    o->copyOffsetAlign = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT; /* 512 */
    /* the Enhanced filter's fields: textures carry mipLevels, every barrier spans all
     * subresources, rhi_cmd_copy_buffer_to_texture writes the given level, and
     * samplers apply the mip filter, LOD range and anisotropy (d3d12_cmd.c,
     * d3d12_resource.c); 16x anisotropy at every feature level */
    o->textureMips = true;
    o->maxAnisotropy = (float)D3D12_MAX_MAXANISOTROPY;
    /* root CBVs cost 2 of the root signature's 64 DWORDs each;
     * 8 of them leave room for every group's tables */
    o->maxDynamicUniforms = 8;
    /* a structured-buffer SRV of 16-byte elements:
     * D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP (27) elements */
    o->maxStorageRange = (uint64_t)16u << 27;
    /* indexed draws: every 32-bit index value but the strip cut (D3D12 has no
     * smaller limit) */
    o->maxDrawIndexedIndexValue = 0xFFFFFFFFu;
    o->fullDrawIndexUint32 = true;
    /* colour targets: eight a pass at feature level 11_0, no bound on their
     * bytes together (rhi.h RhiLimits.colorBytesPerPixel) */
    o->colorBytesPerPixel = 0;
    /* the summary line's (s_driverText starts "; " when known) */
    o->driverText = s_driverText[0] == ';' ? s_driverText + 2 : NULL;
    o->robustText = NULL;
    o->robustDefaultOff = false; /* its own bounds rules: no robust step */
    /* texture packs: BC1/2/3/7 are required of every D3D12 device
     * (feature level 11_0) */
    o->bcTextures = true;
    /* multisampling: the counts every format takes; a shader reading
     * SV_SampleIndex runs per sample on every device */
    o->msaaCounts = dx_msaa_counts();
    o->sampleRateShading = true;
    /* the destination read: ROVs (no interlock or attachment read here) */
    o->rov = dx_rov_supported();
    o->fragmentInterlock = false;
    o->rasterOrderAttachment = false;
    o->rasterOrderOff = NULL;
    o->moltenvk = false;
    o->localRead = false;
    o->vendorId = 0;
    o->dynamicDepth = false;
    o->dynamicBlend = false;
    /* RGBA8 textures are TYPELESS resources: a UINT SRV of any of them
     * (d3d12_pipeline.c, RHI_ASPECT_UINT) is implemented, but off: it has
     * never run on a Windows machine (the CI only cross-compiles), and a
     * mistake in it would draw a wrong glow on the whole backend; true once
     * a D3D12 run's glow has been checked against Vulkan's */
    o->uintView = false;
    /* the stencil read (RHI_ASPECT_STENCIL): an SRV of the depth-stencil's
     * plane 1 (d3d12_pipeline.c), the value in green.  Implemented, but
     * off for the reason uintView is: it has never run on a Windows
     * machine, and the start-up self-test catches wrong bytes, not a
     * removed device; the shadow count keeps its seven draws here.  The
     * follow-up: a Windows run of the shadow self-test on D3D12
     * (rd_shadow_selftest.c), then true */
    o->stencilView = false;
    o->stencilViewChannel = 1;
    o->vuFallbackPlain = NULL;
    o->carefulPipelines = NULL;
    o->compilerVersion = NULL;
}

/* ------------------------------------------------------------- lifecycle */
bool rhi_init(const RhiDeviceDesc *desc)
{
    if (g_dx.initialised) {
        return true;
    }
    HMODULE keepD3d = g_dx.d3d12Dll, keepDxgi = g_dx.dxgiDll;
    memset(&g_dx, 0, sizeof(g_dx));
    g_dx.d3d12Dll = keepD3d; /* kept loaded across rhi_shutdown */
    g_dx.dxgiDll = keepDxgi;
    bool debug = dx_debug_wanted(desc);
    if (!dx_load_runtime(debug)) {
        rhi_shutdown();
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
    if (!dx_pick_adapter()) {
        rhi_shutdown();
        return false;
    }
    dx_dred_enable(); /* gpu_checkpoints, before the device */
    if (!DX_CHECK(g_dx.createDevice((IUnknown *)g_dx.adapter, D3D_FEATURE_LEVEL_11_0,
                                    &IID_ID3D12Device, (void **)&g_dx.device))) {
        rhi_shutdown();
        return false;
    }
    if (g_dx.debugLayer) {
        dx_setup_info_queue();
    }
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE,
                                   0};
    if (!DX_CHECK(ID3D12Device_CreateCommandQueue(g_dx.device, &qd, &IID_ID3D12CommandQueue,
                                                  (void **)&g_dx.queue)) ||
        !DX_CHECK(ID3D12Device_CreateFence(g_dx.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                                           (void **)&g_dx.fence))) {
        rhi_shutdown();
        return false;
    }
    g_dx.fenceEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_dx.fenceEvent || !dx_create_heaps()) {
        rhi_shutdown();
        return false;
    }
    dx_fill_limits();
    if (!d3dp_pool_init(&g_dx.buffers, "buffer", 4096, sizeof(DxBuffer)) ||
        !d3dp_pool_init(&g_dx.textures, "texture", 8192, sizeof(DxTexture)) ||
        !d3dp_pool_init(&g_dx.samplers, "sampler", DX_SMP_PERSISTENT, sizeof(DxSampler)) ||
        !d3dp_pool_init(&g_dx.shaders, "shader", 512, sizeof(DxShader)) ||
        !d3dp_pool_init(&g_dx.layouts, "bind group layout", 64, sizeof(DxLayout)) ||
        !d3dp_pool_init(&g_dx.pipelines, "pipeline", RHI_PIPELINE_POOL_MAX, sizeof(DxPipeline))) {
        rhi_shutdown();
        return false;
    }
    if (!dx_frames_init()) {
        rhi_shutdown();
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
                                   dx_cpu(g_dx.smpCpu, g_dx.smpInc, DX_SMP_DEFAULT));
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
        if (!g_dx.hwnd || !dx_swapchain_create((uint32_t)pw, (uint32_t)ph, desc->vsync)) {
            DX_LOG("no swapchain on the SDL window (HWND %p)", (void *)g_dx.hwnd);
            rhi_shutdown();
            return false;
        }
#else
        DX_LOG("built without SDL3: no window support");
        rhi_shutdown();
        return false;
#endif
    }
    DX_LOG("%s (D3D12, feature level 11_0%s%s)%s; index values up to %u (full 32-bit range "
           "offered)",
           g_dx.adapterName, g_dx.warp ? ", WARP" : "", s_driverText,
           g_dx.swapchain ? "" : ", headless", g_dx.limits.maxDrawIndexedIndexValue);
    dx_video_memory();
    if (g_dx.memBudget == 0) {
        DX_LOG("video memory budget: not reported by this adapter");
    }
    dx_drain_messages();
    return true;
}

void rhi_shutdown(void)
{
    if (g_dx.device && g_dx.queue && g_dx.fence) {
        dx_wait_fence(dx_signal());
    }
    dx_swapchain_destroy();
    dx_frames_shutdown();
    dx_release_all_objects();
    dx_release_root_signatures();
    d3dp_pool_free(&g_dx.buffers);
    d3dp_pool_free(&g_dx.textures);
    d3dp_pool_free(&g_dx.samplers);
    d3dp_pool_free(&g_dx.shaders);
    d3dp_pool_free(&g_dx.layouts);
    d3dp_pool_free(&g_dx.pipelines);
    d3dp_slots_free(&g_dx.rtvSlots);
    d3dp_slots_free(&g_dx.dsvSlots);
    DX_RELEASE(g_dx.resHeap);
    DX_RELEASE(g_dx.smpHeap);
    DX_RELEASE(g_dx.rtvHeap);
    DX_RELEASE(g_dx.dsvHeap);
    DX_RELEASE(g_dx.fence);
    DX_RELEASE(g_dx.queue);
    dx_drain_messages();
    DX_RELEASE(g_dx.info);
    DX_RELEASE(g_dx.device);
    DX_RELEASE(g_dx.adapter3);
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

RhiBackendKind rhi_backend(void)
{
    return RHI_BACKEND_D3D12;
}

const RhiLimits *rhi_limits(void)
{
    return &g_dx.limits;
}

const char *rhi_adapter_name(void)
{
    return g_dx.adapterName;
}

bool rhi_device_lost(void)
{
    return g_dx.deviceLost;
}

/* The performance entry points (rhi.h) on D3D12: the counts the Vulkan
 * backend keeps (draws, passes, pipeline and bind group binds, barriers
 * as ResourceBarrier calls, copies, submits, wait-idles, readbacks,
 * buffers and textures made and destroyed, transient bind groups) are
 * kept at the same points (DxState.counts); the blocked times, swapchains
 * and presents as before; the memory gauges read zero.  Timestamps are
 * d3d12_cmd.c's.  There is no mailbox mode (DXGI's flip model with sync
 * interval 1 is FIFO; docs/port/RHI.md, "Direct3D 12"). */
/* The adapter's local video memory segment (RhiStats videoMem*), asked of
 * DXGI at most every 250 ms; 0 without an IDXGIAdapter3 */
static void dx_video_memory(void)
{
    const ULONGLONG now = GetTickCount64();
    DXGI_QUERY_VIDEO_MEMORY_INFO m;

    if (!g_dx.adapter3 || (g_dx.memTick && now - g_dx.memTick < 250u)) {
        return;
    }
    g_dx.memTick = now ? now : 1u;
    memset(&m, 0, sizeof(m));
    if (SUCCEEDED(IDXGIAdapter3_QueryVideoMemoryInfo(g_dx.adapter3, 0,
                                                     DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &m))) {
        g_dx.memBudget = m.Budget;
        g_dx.memUsage = m.CurrentUsage;
        g_dx.memAvail = m.AvailableForReservation;
    }
}

void rhi_get_stats(RhiStats *out)
{
    if (out) {
        dx_video_memory();
        *out = g_dx.counts;
        out->videoMemBudget = g_dx.memBudget;
        out->videoMemUsage = g_dx.memUsage;
        out->videoMemAvailable = g_dx.memAvail;
        out->swapchains = g_dx.swapchains;
        out->presents = g_dx.presents;
        out->presentNs = g_dx.presentNs;
        out->fenceWaits = g_dx.fenceWaits;
        out->fenceWaitNs = g_dx.fenceWaitNs;
        out->acquireNs = g_dx.acquireNs;
        out->pipelinePoolSize = g_dx.pipelines.cap;
        out->pipelinesLive = g_dx.pipelines.next - g_dx.pipelines.freeCount;
        out->pipelinesPeak = g_dx.pipelines.next;
        out->pipelinePoolFull = g_dx.pipelines.full;
    }
}

void rhi_prefer_mailbox(bool on)
{
    (void)on;
}

bool rhi_present_mailbox(void)
{
    return false;
}

/* rhi.h: ignored for now; the swapchain is DXGI's flip model in the
   window, and the window's fullscreen is SDL's and DXGI's to decide */
void rhi_set_exclusive_fullscreen(bool allowed)
{
    (void)allowed;
}

/* rhi.h: no display timing of presents (the vsync lock presents FIFO) */
bool rhi_present_timing(void)
{
    return false;
}

void rhi_present_timing_use(bool on)
{
    (void)on;
}

uint64_t rhi_present_last_id(void)
{
    return 0;
}

int rhi_present_timing_next(uint64_t *id, uint64_t *ns)
{
    (void)id;
    (void)ns;
    return -1;
}

void rhi_set_pipeline_cache_path(const char *path)
{
    (void)path; /* the driver keeps its own shader cache */
}

bool rhi_pipeline_cache_loaded(void)
{
    return false;
}

bool rhi_pipeline_cache_save(void)
{
    return false; /* the driver keeps its own shader cache */
}

uint64_t rhi_pipeline_cache_size(void)
{
    return 0; /* the driver keeps its own shader cache */
}

void rhi_set_vulkan_loader(void *getInstanceProcAddr)
{
    (void)getInstanceProcAddr; /* Vulkan only */
}

/* The rhi_create_backend entry (port/rhi/rhi_backend.h). */
RHI_BACKEND_DEFINE(rhi_backend_d3d12, "d3d12");
