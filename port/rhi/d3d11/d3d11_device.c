/* d3d11_device.c: runtime loading, adapter selection, the device and its
 * immediate context, the debug layer, limits, rhi_init/rhi_shutdown, the
 * frame slots (deferred-context command lists, submission, the per-slot
 * event queries), GPU timestamps and the DXBC cache's entry points of the
 * Direct3D 11 backend.  Compile-only in v0.8.6: it has not run on any
 * Windows machine (docs/port/RHI.md, "Direct3D 11"). */
#include "d3d11_internal.h"
#include "rhi_d3d11.h"
#include "../rhi_backend.h"
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RHI_HAVE_SDL
#include <SDL3/SDL.h>
#endif

D11State g_d11;

static bool d11_is_loss_code(HRESULT hr)
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

static const char *d11_loss_name(HRESULT hr)
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

/* The loss report: the last submissions with the frames they carried (no
 * fence values on Direct3D 11: the serial is the submission's number). */
static void d11_lost_report(void)
{
    const uint32_t n =
        g_d11.submitCount < D11_SUBMIT_HISTORY ? g_d11.submitCount : D11_SUBMIT_HISTORY;
    D11_LOG("loss report: the last %u submissions:", n);
    for (uint32_t k = 0; k < n; k++) {
        const uint32_t i = (g_d11.submitCount - n + k) % D11_SUBMIT_HISTORY;
        D11_LOG("  submission %llu: replay %u, frame %u",
                (unsigned long long)g_d11.submits[i].serial, g_d11.submits[i].replay,
                g_d11.submits[i].frame);
    }
    fflush(stderr);
}

bool d11_check(HRESULT hr, const char *what, const char *file, int line)
{
    if (SUCCEEDED(hr)) {
        return true;
    }
    if (g_d11.deviceLost) {
        /* the reason was logged once; later failures are counted */
        g_d11.lostFailures++;
        return false;
    }
    fprintf(stderr, "rhi_d3d11: %s failed (HRESULT 0x%08lx) at %s:%d\n", what, (unsigned long)hr,
            file, line);
    HRESULT reason = g_d11.device ? ID3D11Device_GetDeviceRemovedReason(g_d11.device) : S_OK;
    if (d11_is_loss_code(hr) || reason != S_OK) {
        g_d11.deviceLost = true;
        fprintf(stderr,
                "rhi_d3d11: the device was lost (%s); removed reason 0x%08lx (%s). Rendering "
                "stops; the session ends.\n",
                d11_loss_name(hr), (unsigned long)reason, d11_loss_name(reason));
        d11_lost_report();
    }
    d11_drain_messages();
    return false;
}

/* ------------------------------------------------------------- debug layer */
void d11_drain_messages(void)
{
    if (!g_d11.info) {
        return;
    }
    UINT64 n = ID3D11InfoQueue_GetNumStoredMessages(g_d11.info);
    for (UINT64 i = 0; i < n; i++) {
        SIZE_T len = 0;
        if (FAILED(ID3D11InfoQueue_GetMessage(g_d11.info, i, NULL, &len)) || len == 0) {
            continue;
        }
        D3D11_MESSAGE *m = malloc(len);
        if (!m) {
            continue;
        }
        if (SUCCEEDED(ID3D11InfoQueue_GetMessage(g_d11.info, i, m, &len))) {
            const char *sev = "info";
            if (m->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION ||
                m->Severity == D3D11_MESSAGE_SEVERITY_ERROR) {
                g_d11.debugErrors++;
                sev = m->Severity == D3D11_MESSAGE_SEVERITY_ERROR ? "error" : "corruption";
            } else if (m->Severity == D3D11_MESSAGE_SEVERITY_WARNING) {
                sev = "warning";
            }
            fprintf(stderr, "rhi_d3d11 debug %s (id %d): %.*s\n", sev, (int)m->ID,
                    (int)m->DescriptionByteLength, m->pDescription);
        }
        free(m);
    }
    ID3D11InfoQueue_ClearStoredMessages(g_d11.info);
}

uint32_t rhi_d3d11_debug_error_count(void)
{
    return g_d11.debugErrors;
}

bool rhi_d3d11_debug_layer_active(void)
{
    return g_d11.info != NULL;
}

bool rhi_d3d11_is_warp(void)
{
    return g_d11.warp;
}

uint32_t rhi_d3d11_feature_level(void)
{
    return (uint32_t)g_d11.featureLevel;
}

static bool d11_debug_wanted(const RhiDeviceDesc *desc)
{
    bool debug = desc->debugLayers;
#ifdef ICO_RHI_DEBUG_DEFAULT
    debug = true;
#endif
    const char *env = getenv("ICO_D3D11_DEBUG");
    if (env && *env) {
        debug = env[0] != '0';
    }
    return debug;
}

static void d11_setup_info_queue(void)
{
    if (FAILED(ID3D11Device_QueryInterface(g_d11.device, &IID_ID3D11InfoQueue,
                                           (void **)&g_d11.info))) {
        g_d11.info = NULL;
        return;
    }
    /* kept: corruption, errors, warnings */
    D3D11_MESSAGE_SEVERITY deny[] = {D3D11_MESSAGE_SEVERITY_INFO, D3D11_MESSAGE_SEVERITY_MESSAGE};
    D3D11_INFO_QUEUE_FILTER f;
    memset(&f, 0, sizeof(f));
    f.DenyList.NumSeverities = (UINT)(sizeof(deny) / sizeof(deny[0]));
    f.DenyList.pSeverityList = deny;
    ID3D11InfoQueue_PushStorageFilter(g_d11.info, &f);
    ID3D11InfoQueue_SetMessageCountLimit(g_d11.info, 4096);
}

/* ------------------------------------------------------- runtime loading
 * d3d11.dll and dxgi.dll are loaded at run time, so the other backends of
 * the same executable never depend on them.  An effects program's
 * d3d11.dll or dxgi.dll beside the executable (ReShade) is named as the
 * D3D12 backend names its own. */
typedef HRESULT(WINAPI *PFN_D11CreateDXGIFactory2)(UINT, REFIID, void **);
typedef HRESULT(WINAPI *PFN_D11CreateDXGIFactory1)(REFIID, void **);

static const char *s_injector;

static bool d11_in_system_dir(HMODULE m)
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
        if (n > d && _strnicmp(file, dir, d) == 0 && (file[d] == '\\' || file[d] == '/') &&
            !strpbrk(file + d + 1, "\\/")) {
            return true;
        }
    }
    return false;
}

const char *rhi_injector_name(void)
{
    return s_injector;
}

const char *rhi_overlay_name(void)
{
    return NULL;
}

static bool d11_load_runtime(bool debug)
{
    if (!g_d11.d3d11Dll) {
        g_d11.d3d11Dll = LoadLibraryA("d3d11.dll");
    }
    if (!g_d11.dxgiDll) {
        g_d11.dxgiDll = LoadLibraryA("dxgi.dll");
    }
    if (!g_d11.d3d11Dll || !g_d11.dxgiDll) {
        D11_LOG("d3d11.dll or dxgi.dll not found");
        return false;
    }
    s_injector = (!d11_in_system_dir(g_d11.dxgiDll) || !d11_in_system_dir(g_d11.d3d11Dll))
                     ? "ReShade"
                     : NULL;
    g_d11.createDevice =
        (PFN_D3D11_CREATE_DEVICE)(void *)GetProcAddress(g_d11.d3d11Dll, "D3D11CreateDevice");
    if (!g_d11.createDevice) {
        D11_LOG("d3d11.dll lacks D3D11CreateDevice");
        return false;
    }
    PFN_D11CreateDXGIFactory2 f2 =
        (PFN_D11CreateDXGIFactory2)(void *)GetProcAddress(g_d11.dxgiDll, "CreateDXGIFactory2");
    HRESULT hr;
    if (f2) {
        hr = f2(debug ? DXGI_CREATE_FACTORY_DEBUG : 0, &IID_IDXGIFactory2, (void **)&g_d11.factory);
        if (FAILED(hr) && debug) {
            hr = f2(0, &IID_IDXGIFactory2, (void **)&g_d11.factory);
        }
    } else {
        /* Windows 8.0: no CreateDXGIFactory2, but DXGI 1.2's factory */
        PFN_D11CreateDXGIFactory1 f1 =
            (PFN_D11CreateDXGIFactory1)(void *)GetProcAddress(g_d11.dxgiDll, "CreateDXGIFactory1");
        hr = f1 ? f1(&IID_IDXGIFactory2, (void **)&g_d11.factory) : E_FAIL;
    }
    if (FAILED(hr)) {
        D11_LOG("no DXGI 1.2 factory (HRESULT 0x%08lx): Direct3D 11.1 is needed (Windows 8 or "
                "later)",
                (unsigned long)hr);
        return false;
    }
    return true;
}

/* ------------------------------------------------------- adapter selection */
static const D3D_FEATURE_LEVEL s_levels[2] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};

/* D3D11CreateDevice at 11_1 or 11_0; a runtime that does not know 11_1
 * (Windows 7 without the platform update) answers E_INVALIDARG to a list
 * holding it, so the list without it is tried next.  out NULL: only asks
 * whether the adapter can. */
static HRESULT d11_create(IDXGIAdapter *a, D3D_DRIVER_TYPE type, UINT flags, ID3D11Device **out,
                          D3D_FEATURE_LEVEL *level, ID3D11DeviceContext **ctx)
{
    HRESULT hr =
        g_d11.createDevice(a, type, NULL, flags, s_levels, 2, D3D11_SDK_VERSION, out, level, ctx);
    if (hr == E_INVALIDARG) {
        hr = g_d11.createDevice(a, type, NULL, flags, s_levels + 1, 1, D3D11_SDK_VERSION, out,
                                level, ctx);
    }
    return hr;
}

static void d11_adapter_name(IDXGIAdapter1 *a, char *out, size_t size)
{
    DXGI_ADAPTER_DESC1 d;
    memset(&d, 0, sizeof(d));
    IDXGIAdapter1_GetDesc1(a, &d);
    if (!WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, out, (int)size, NULL, NULL)) {
        snprintf(out, size, "adapter %04x:%04x", d.VendorId, d.DeviceId);
    }
}

static bool d11_is_software(IDXGIAdapter1 *a)
{
    DXGI_ADAPTER_DESC1 d;
    memset(&d, 0, sizeof(d));
    IDXGIAdapter1_GetDesc1(a, &d);
    return (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

/* The i-th adapter, discrete first when DXGI 1.6 can order them. */
static IDXGIAdapter1 *d11_enum_hardware(UINT i)
{
    IDXGIAdapter1 *a = NULL;
    IDXGIFactory6 *f6 = NULL;
    if (SUCCEEDED(IDXGIFactory2_QueryInterface(g_d11.factory, &IID_IDXGIFactory6, (void **)&f6))) {
        HRESULT hr = IDXGIFactory6_EnumAdapterByGpuPreference(
            f6, i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &IID_IDXGIAdapter1, (void **)&a);
        IDXGIFactory6_Release(f6);
        return SUCCEEDED(hr) ? a : NULL;
    }
    return SUCCEEDED(IDXGIFactory2_EnumAdapters1(g_d11.factory, i, &a)) ? a : NULL;
}

/* ICO_D3D11_ADAPTER=warp, an index into the list below, or a part of the
 * adapter's name forces a pick; without a hardware adapter, WARP.  Sets
 * g_d11.adapter (NULL for WARP) and g_d11.warp. */
static void d11_pick_adapter(void)
{
    const char *force = getenv("ICO_D3D11_ADAPTER");
    const bool forceWarp = force && _stricmp(force, "warp") == 0;
    IDXGIAdapter1 *pick = NULL;
    if (!forceWarp) {
        for (UINT i = 0;; i++) {
            IDXGIAdapter1 *a = d11_enum_hardware(i);
            if (!a) {
                break;
            }
            char name[128];
            d11_adapter_name(a, name, sizeof(name));
            const bool ok = !d11_is_software(a) &&
                            SUCCEEDED(d11_create((IDXGIAdapter *)a, D3D_DRIVER_TYPE_UNKNOWN, 0,
                                                 NULL, NULL, NULL));
            D11_LOG("adapter %u: %s%s", i, name,
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
            D11_LOG("ICO_D3D11_ADAPTER=%s matched no adapter", force);
        }
    }
    g_d11.adapter = pick;
    g_d11.warp = pick == NULL;
}

/* "; driver version 32.0.15.8157" (the user-mode driver's version, as the
 * D3D12 backend reads it); "" when the adapter does not say */
static char s_driverText[64];

/* The device's own adapter (WARP's too): its name, ids and driver
 * version. */
static void d11_describe_adapter(void)
{
    IDXGIDevice *dxgiDev = NULL;
    IDXGIAdapter *a = NULL;
    s_driverText[0] = '\0';
    snprintf(g_d11.adapterName, sizeof(g_d11.adapterName), "%s",
             g_d11.warp ? "Microsoft Basic Render Driver (WARP)" : "unknown adapter");
    if (FAILED(ID3D11Device_QueryInterface(g_d11.device, &IID_IDXGIDevice, (void **)&dxgiDev))) {
        return;
    }
    if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgiDev, &a))) {
        DXGI_ADAPTER_DESC d;
        memset(&d, 0, sizeof(d));
        if (SUCCEEDED(IDXGIAdapter_GetDesc(a, &d))) {
            if (!WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, g_d11.adapterName,
                                     (int)sizeof(g_d11.adapterName), NULL, NULL)) {
                snprintf(g_d11.adapterName, sizeof(g_d11.adapterName), "adapter %04x:%04x",
                         d.VendorId, d.DeviceId);
            }
            g_d11.vendorId = d.VendorId;
            g_d11.deviceId = d.DeviceId;
        }
        LARGE_INTEGER umd;
        if (SUCCEEDED(IDXGIAdapter_CheckInterfaceSupport(a, &IID_IDXGIDevice, &umd))) {
            const unsigned long long v = (unsigned long long)umd.QuadPart;
            snprintf(s_driverText, sizeof(s_driverText), "; driver version %u.%u.%u.%u",
                     (unsigned)((v >> 48) & 0xffff), (unsigned)((v >> 32) & 0xffff),
                     (unsigned)((v >> 16) & 0xffff), (unsigned)(v & 0xffff));
        }
        IDXGIAdapter_Release(a);
    }
    IDXGIDevice_Release(dxgiDev);
}

/* The device at 11_1 or 11_0 on the picked adapter (or WARP), with the
 * debug layer when asked and installed. */
static bool d11_create_device(bool debug)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_DRIVER_TYPE type = g_d11.adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_WARP;
    HRESULT hr = E_FAIL;
    if (debug) {
        hr = d11_create((IDXGIAdapter *)g_d11.adapter, type, flags | D3D11_CREATE_DEVICE_DEBUG,
                        &g_d11.device, &g_d11.featureLevel, &g_d11.ctx);
        if (SUCCEEDED(hr)) {
            g_d11.debugLayer = true;
        } else {
            D11_LOG("debugLayers: the D3D11 debug layer is not installed (Windows optional "
                    "feature \"Graphics Tools\"); continuing without");
        }
    }
    if (!g_d11.device) {
        hr = d11_create((IDXGIAdapter *)g_d11.adapter, type, flags, &g_d11.device,
                        &g_d11.featureLevel, &g_d11.ctx);
    }
    if (FAILED(hr) && g_d11.adapter) {
        /* the hardware adapter refused at the end: WARP */
        D11_LOG("the adapter refused a device (HRESULT 0x%08lx): trying WARP", (unsigned long)hr);
        IDXGIAdapter1_Release(g_d11.adapter);
        g_d11.adapter = NULL;
        g_d11.warp = true;
        hr = d11_create(NULL, D3D_DRIVER_TYPE_WARP, flags, &g_d11.device, &g_d11.featureLevel,
                        &g_d11.ctx);
    }
    return D11_CHECK(hr);
}

/* ------------------------------------------------------------------ limits */
/* The sample counts (2, 4, 8) every multisampled target the renderer makes
 * takes: a quality level for RGBA8 UNORM and UINT and the scene's
 * depth-stencil (as the D3D12 backend) */
static uint32_t d11_msaa_counts(void)
{
    static const DXGI_FORMAT fmts[3] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UINT,
                                        DXGI_FORMAT_D32_FLOAT_S8X24_UINT};
    uint32_t counts = 1u;
    for (uint32_t n = 2u; n <= 8u; n *= 2u) {
        bool all = true;
        for (int i = 0; i < 3 && all; i++) {
            UINT q = 0;
            all = SUCCEEDED(
                      ID3D11Device_CheckMultisampleQualityLevels(g_d11.device, fmts[i], n, &q)) &&
                  q > 0;
        }
        if (all) {
            counts |= n;
        }
    }
    D11_LOG("multisampling: sample counts 0x%x", counts);
    return counts;
}

static char s_driverLimitText[96];

static void d11_fill_limits(void)
{
    RhiLimits *o = &g_d11.limits;
    memset(o, 0, sizeof(*o));
    o->uniformAlign = 256; /* VSSetConstantBuffers1 windows start at multiples of 16 constants */
    o->maxTextureSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;      /* 16384 */
    o->maxRenderTargetSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION; /* 16384 */
    o->dualSourceBlend = true; /* every feature level 10_0 and above */
    o->stencilWrap = true;
    o->depthReadback = true;
    o->depthStencilFormatName = "D32S8";
    o->depthSampled = true;
    o->tiler = false;
    /* buffer->texture copies are UpdateSubresource from the upload shadow:
     * any pitch; offsets of whole texels */
    o->copyRowPitchAlign = 1;
    o->copyOffsetAlign = 4;
    o->textureMips = true;
    o->maxAnisotropy = (float)D3D11_REQ_MAXANISOTROPY; /* 16 */
    /* each group's dynamic uniforms are constant buffer slots of their own */
    o->maxDynamicUniforms = 4;
    o->maxStorageRange = (uint64_t)16u << D3D11_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP;
    o->maxDrawIndexedIndexValue = 0xFFFFFFFFu;
    o->fullDrawIndexUint32 = true;
    /* the VU vertex pre-pass stays off: its record is read back through a
     * texture-to-buffer copy D3D11 does not have (rd_replay_vu.c takes the
     * 32-byte answer as the Apple A9/A10 path's) */
    o->colorBytesPerPixel = 32;
    if (g_d11.warp) {
        snprintf(s_driverLimitText, sizeof(s_driverLimitText), "WARP (software)%s", s_driverText);
    } else {
        snprintf(s_driverLimitText, sizeof(s_driverLimitText), "%s",
                 s_driverText[0] == ';' ? s_driverText + 2 : "");
    }
    o->driverText = s_driverLimitText[0] ? s_driverLimitText : NULL;
    o->robustText = NULL;
    o->robustDefaultOff = false;
    o->bcTextures = true; /* BC1/2/3/7 at feature level 11_0 */
    o->msaaCounts = d11_msaa_counts();
    o->sampleRateShading = true;
    /* D3D11.3 has ROVs, left off in this version (opt-in, untested) */
    o->rov = false;
    o->fragmentInterlock = false;
    o->rasterOrderAttachment = false;
    o->rasterOrderOff = NULL;
    o->moltenvk = false;
    o->localRead = false;
    o->vendorId = g_d11.vendorId;
    o->dynamicDepth = false;
    o->dynamicBlend = false;
    o->uintView = false;
    o->vuFallbackPlain = NULL;
    o->carefulPipelines = NULL;
    o->compilerVersion = NULL;
}

/* --------------------------------------------------------- the DXBC cache
 * rhi_set_pipeline_cache_path names the Vulkan cache file
 * (<pref>/pipelines-<build>.vkcache, window_host.c); the DXBC cache is the
 * folder beside it, <pref>/pipelines-<build>.d3d11, one file per shader
 * (d3d11_shader.c), keyed with <build>.  Another build's folder
 * (pipelines-*.d3d11) is removed at rhi_init, as the Vulkan backend drops
 * another build's file (vk_pipeline.c vkr_cache_drop_others).  Any other
 * path (a test's): the folder <path>.d3d11, keyed with the file's name, and
 * nothing dropped.  Kept across rhi_init. */
static char s_cachePath[1024];

void rhi_set_pipeline_cache_path(const char *path)
{
    s_cachePath[0] = '\0';
    if (path && strlen(path) + 8 < sizeof(s_cachePath)) {
        strcpy(s_cachePath, path);
    }
}

/* removes folder dir's files, then the folder (one level) */
static bool d11_remove_folder(const char *dir)
{
    char pat[1100];
    WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char f[1400];
                snprintf(f, sizeof(f), "%s\\%s", dir, fd.cFileName);
                DeleteFileA(f);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryA(dir) != 0;
}

/* other builds' pipelines*.d3d11 folders beside ours (prefix: the folder
 * with its separator, ours: our folder's name) */
static void d11_cache_drop_others(const char *prefix, size_t plen, const char *ours)
{
    enum { MAXN = 32 };

    static char names[MAXN][256];
    uint32_t n = 0;
    char pat[1100];
    WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof(pat), "%.*spipelines*", (int)plen, prefix);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            d3d11_shader_cache_is_other(fd.cFileName, ours) && n < MAXN &&
            strlen(fd.cFileName) < sizeof(names[0])) {
            strcpy(names[n++], fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    for (uint32_t i = 0; i < n; i++) {
        char full[1400];
        snprintf(full, sizeof(full), "%.*s%s", (int)plen, prefix, names[i]);
        if (d11_remove_folder(full)) {
            D11_LOG("shader cache: dropped %s (another version of the game)", names[i]);
        } else {
            D11_LOG("shader cache: could not drop %s", names[i]);
        }
    }
}

/* the folder and the build id from the path (d3d11_shader_cache_folder);
 * "" without a path */
static void d11_cache_setup(void)
{
    bool window = false;
    if (!d3d11_shader_cache_folder(s_cachePath, g_d11.cacheDir, sizeof(g_d11.cacheDir),
                                   g_d11.buildId, sizeof(g_d11.buildId), &window)) {
        if (s_cachePath[0]) {
            D11_LOG("shader cache path %s is too long: no shader cache", s_cachePath);
        }
        return;
    }
    if (window) {
        const char *base = g_d11.cacheDir;
        for (const char *p = g_d11.cacheDir; *p; p++) {
            if (*p == '/' || *p == '\\') {
                base = p + 1;
            }
        }
        d11_cache_drop_others(g_d11.cacheDir, (size_t)(base - g_d11.cacheDir), base);
    }
    if (!d3d11_shader_cache_mkdir(g_d11.cacheDir)) {
        D11_LOG("shader cache folder %s could not be made: the shaders are compiled at every "
                "start",
                g_d11.cacheDir);
        g_d11.cacheDir[0] = '\0';
    }
}

/* the cache's counters, logged once there is something to say (after the
 * start-up set: rhi_pipeline_cache_save; else at the first rhi_wait_idle
 * that follows a compile; and at rhi_shutdown) */
static void d11_cache_stats_log(bool force)
{
    D3d11ShaderStats st;
    memset(&st, 0, sizeof(st));
    d3d11_shader_stats(g_d11.cacheDir, &st, false);
    if (st.hits + st.compiles + st.failed == 0 || (g_d11.cacheStatsLogged && !force)) {
        return;
    }
    g_d11.cacheStatsLogged = true;
    d3d11_shader_stats(g_d11.cacheDir[0] ? g_d11.cacheDir : "(none)", NULL, true);
}

bool rhi_pipeline_cache_loaded(void)
{
    D3d11ShaderStats st;
    memset(&st, 0, sizeof(st));
    d3d11_shader_stats(g_d11.cacheDir, &st, false);
    return g_d11.cacheDir[0] && st.hits > 0;
}

/* each shader's file is written as it is compiled: nothing left to write */
bool rhi_pipeline_cache_save(void)
{
    if (!g_d11.initialised || g_d11.deviceLost) {
        return false;
    }
    d11_cache_stats_log(true);
    return g_d11.cacheDir[0] != '\0';
}

/* the cache folder's *.dxbc bytes */
uint64_t rhi_pipeline_cache_size(void)
{
    if (!g_d11.cacheDir[0] || g_d11.deviceLost) {
        return 0;
    }
    char pat[1100];
    WIN32_FIND_DATAA fd;
    uint64_t total = 0;
    snprintf(pat, sizeof(pat), "%s\\*.dxbc", g_d11.cacheDir);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            total += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return total;
}

/* ------------------------------------------------------------ query waits
 * GetData without DONOTFLUSH flushes the immediate context, so a poll
 * also hands the GPU the work before the query.  A wait longer than 5 s is
 * logged once (a hung device ends as DXGI_ERROR_DEVICE_HUNG from the
 * driver's own timeout, which GetData returns). */
static HRESULT d11_spin(ID3D11Query *q, void *data, UINT size)
{
    HRESULT hr = ID3D11DeviceContext_GetData(g_d11.ctx, (ID3D11Asynchronous *)q, data, size, 0);
    if (hr != S_FALSE) {
        return hr;
    }
    static bool s_slowLogged;
    const uint64_t t0 = d11_now_ns();
    uint32_t polls = 0;
    while ((hr = ID3D11DeviceContext_GetData(g_d11.ctx, (ID3D11Asynchronous *)q, data, size, 0)) ==
           S_FALSE) {
        if (++polls < 64) {
            Sleep(0);
        } else {
            SwitchToThread();
        }
        if (!s_slowLogged && d11_now_ns() - t0 > 5000000000ull) {
            s_slowLogged = true;
            D11_LOG("a wait for the GPU has taken over 5 s; still waiting (once)");
        }
    }
    g_d11.fenceWaits++;
    g_d11.fenceWaitNs += d11_now_ns() - t0;
    return hr;
}

bool d11_wait_query(ID3D11Query *q)
{
    if (!q || g_d11.deviceLost) {
        return false;
    }
    ID3D11DeviceContext_End(g_d11.ctx, (ID3D11Asynchronous *)q);
    BOOL done = FALSE;
    return D11_CHECK(d11_spin(q, &done, sizeof(done)));
}

/* -------------------------------------------------------------- timestamps
 * Per frame slot: a TIMESTAMP_DISJOINT query, begun on the immediate
 * context at the slot's first submit and ended when rhi_wait_frame leaves
 * the slot, around RHI_MAX_TIMESTAMPS TIMESTAMP queries the lists end on
 * their deferred contexts.  Read when the slot comes round again (its
 * event passed): the frequency from the disjoint query, nothing when it
 * says disjoint (a clock change). */
static void d11_timestamps_release(void)
{
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        D11Frame *f = &g_d11.frames[i];
        D11_RELEASE(f->tsDisjoint);
        for (uint32_t k = 0; k < RHI_MAX_TIMESTAMPS; k++) {
            D11_RELEASE(f->ts[k]);
        }
        memset(f->tsWritten, 0, sizeof(f->tsWritten));
        f->tsDisjointBegun = false;
    }
    g_d11.timestamps = false;
}

static void d11_timestamps_init(void)
{
    g_d11.timestamps = false;
    g_d11.tsCount = 0;
    const char *noTs = getenv("ICO_DX_FAKE_NO_TIMESTAMPS");
    if (noTs && noTs[0] && noTs[0] != '0') {
        D11_LOG("GPU timestamps: off (ICO_DX_FAKE_NO_TIMESTAMPS)");
        return;
    }
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        D11Frame *f = &g_d11.frames[i];
        D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        HRESULT hr = ID3D11Device_CreateQuery(g_d11.device, &qd, &f->tsDisjoint);
        qd.Query = D3D11_QUERY_TIMESTAMP;
        for (uint32_t k = 0; k < RHI_MAX_TIMESTAMPS && SUCCEEDED(hr); k++) {
            hr = ID3D11Device_CreateQuery(g_d11.device, &qd, &f->ts[k]);
        }
        if (FAILED(hr)) {
            D11_LOG("GPU timestamps: off (CreateQuery failed, HRESULT 0x%08lx)", (unsigned long)hr);
            d11_timestamps_release();
            return;
        }
    }
    g_d11.timestamps = true;
    D11_LOG("GPU timestamps: on (%u per frame slot)", (unsigned)RHI_MAX_TIMESTAMPS);
}

void rhi_cmd_write_timestamp(RhiCommandList cl, uint32_t index)
{
    D11CommandList *c = d11_get_cmd(cl);
    D11Frame *f = d11_cur_frame();
    index &= ~RHI_TIMESTAMP_KEEP_BARRIERS; /* no held-back barriers here */
    if (!c || !g_d11.timestamps || !c->recording || index >= RHI_MAX_TIMESTAMPS ||
        rhi_ts_has(f->tsWritten, index) || rhi_ts_has(c->tsMask, index)) {
        return;
    }
    /* a stamp is written once a frame, in one list */
    for (uint32_t j = 0; j < f->listCount; j++) {
        if (rhi_ts_has(f->lists[j].tsMask, index)) {
            return;
        }
    }
    ID3D11DeviceContext_End(c->ctx, (ID3D11Asynchronous *)f->ts[index]);
    rhi_ts_add(c->tsMask, index);
}

/* the slot just recycled: its stamps into g_d11.tsResult, as ns */
static void d11_collect_timestamps(D11Frame *f)
{
    g_d11.tsCount = 0;
    memset(g_d11.tsResult, 0, sizeof(g_d11.tsResult));
    uint32_t m[RHI_TIMESTAMP_WORDS];
    uint32_t any = 0;
    for (uint32_t w = 0; w < RHI_TIMESTAMP_WORDS; w++) {
        m[w] = f->tsWritten[w];
        any |= m[w];
    }
    memset(f->tsWritten, 0, sizeof(f->tsWritten));
    const bool begun = f->tsDisjointBegun;
    f->tsDisjointBegun = false;
    if (!any || !begun || !g_d11.timestamps || g_d11.deviceLost) {
        return;
    }
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
    memset(&dj, 0, sizeof(dj));
    if (d11_spin(f->tsDisjoint, &dj, sizeof(dj)) != S_OK) {
        return;
    }
    if (dj.Disjoint || dj.Frequency == 0) {
        if (!g_d11.tsDisjointLogged) {
            g_d11.tsDisjointLogged = true;
            D11_LOG("GPU timestamps: a frame's clock was disjoint (frequency %llu); its stamps "
                    "are dropped (logged once)",
                    (unsigned long long)dj.Frequency);
        }
        return;
    }
    const uint64_t freq = dj.Frequency;
    for (uint32_t i = 0; i < RHI_MAX_TIMESTAMPS; i++) {
        if (!rhi_ts_has(m, i)) {
            continue;
        }
        UINT64 t = 0;
        if (ID3D11DeviceContext_GetData(g_d11.ctx, (ID3D11Asynchronous *)f->ts[i], &t, sizeof(t),
                                        D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            t) {
            g_d11.tsResult[i] = t / freq * 1000000000ull + t % freq * 1000000000ull / freq;
            g_d11.tsCount = i + 1;
        }
    }
}

bool rhi_timestamps_supported(void)
{
    return g_d11.timestamps;
}

uint32_t rhi_read_timestamps(uint64_t *ns, uint32_t max)
{
    if (!ns || !g_d11.timestamps) {
        return 0;
    }
    const uint32_t n = g_d11.tsCount < max ? g_d11.tsCount : max;
    memcpy(ns, g_d11.tsResult, (size_t)n * sizeof(uint64_t));
    return n;
}

/* ------------------------------------------------------------------ frames */
static bool d11_frames_init(void)
{
    D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (!D11_CHECK(ID3D11Device_CreateQuery(g_d11.device, &qd, &g_d11.frames[i].event))) {
            return false;
        }
    }
    if (!D11_CHECK(ID3D11Device_CreateQuery(g_d11.device, &qd, &g_d11.idleEvent))) {
        return false;
    }
    g_d11.frameIndex = 0;
    d11_timestamps_init();
    return true;
}

static void d11_release_list(D11CommandList *c)
{
    D11_RELEASE(c->list);
    if (c->recording && c->ctx) {
        /* begun and never ended: finish it to clear the deferred context */
        ID3D11CommandList *drop = NULL;
        if (SUCCEEDED(ID3D11DeviceContext_FinishCommandList(c->ctx, FALSE, &drop))) {
            D11_RELEASE(drop);
        }
    }
    c->recording = false;
}

static void d11_frames_shutdown(void)
{
    d11_timestamps_release();
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        D11Frame *f = &g_d11.frames[i];
        d11_destroy_garbage(f);
        free(f->garbage);
        free(f->groups);
        for (uint32_t j = 0; j < D11_MAX_CMD_LISTS; j++) {
            D11CommandList *c = &f->lists[j];
            d11_release_list(c);
            D11_RELEASE(c->annot);
            D11_RELEASE(c->ctx1);
            D11_RELEASE(c->ctx);
            free(c->touch);
        }
        D11_RELEASE(f->event);
        memset(f, 0, sizeof(*f));
    }
    D11_RELEASE(g_d11.idleEvent);
}

/* A frame slot once the GPU is done with it: its event waited for, its
 * timestamps read, its garbage released, its lists and bind groups reset. */
static void d11_recycle_frame(D11Frame *f)
{
    if (f->eventIssued && !g_d11.deviceLost) {
        BOOL done = FALSE;
        D11_CHECK(d11_spin(f->event, &done, sizeof(done)));
    }
    f->eventIssued = false;
    d11_collect_timestamps(f);
    d11_destroy_garbage(f);
    for (uint32_t j = 0; j < D11_MAX_CMD_LISTS; j++) {
        D11CommandList *c = &f->lists[j];
        d11_release_list(c);
        c->submitted = false;
        c->finishFailed = false;
        c->touchCount = 0;
        memset(c->tsMask, 0, sizeof(c->tsMask));
    }
    f->listCount = 0;
    f->groupCount = 0;
}

void rhi_wait_frame(void)
{
    D11Frame *old = d11_cur_frame();
    if (old->tsDisjointBegun && old->tsDisjoint) {
        ID3D11DeviceContext_End(g_d11.ctx, (ID3D11Asynchronous *)old->tsDisjoint);
    }
    g_d11.frameIndex++;
    d11_recycle_frame(d11_cur_frame());
    /* a removal shows here at the latest (ExecuteCommandList returns
     * nothing) */
    if (!g_d11.deviceLost && g_d11.device) {
        D11_CHECK(ID3D11Device_GetDeviceRemovedReason(g_d11.device));
    }
}

uint32_t rhi_frame_slot(void)
{
    return (uint32_t)(g_d11.frameIndex % RHI_FRAMES_IN_FLIGHT);
}

void rhi_wait_idle(void)
{
    g_d11.counts.waitIdles++;
    if (g_d11.ctx) {
        d11_wait_query(g_d11.idleEvent);
    }
    if (!g_d11.deviceLost && g_d11.device) {
        D11_CHECK(ID3D11Device_GetDeviceRemovedReason(g_d11.device));
    }
    /* the other slots' garbage is now safe; the current frame's is not (a
     * list recorded but not submitted may still use it) */
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (&g_d11.frames[i] != d11_cur_frame()) {
            d11_destroy_garbage(&g_d11.frames[i]);
        }
    }
    d11_cache_stats_log(false);
    d11_drain_messages();
}

void rhi_collect_garbage_now(void)
{
    rhi_wait_idle();
    d11_destroy_garbage(d11_cur_frame());
}

/* ---------------------------------------------------------- command lists */
D11CommandList *d11_get_cmd(RhiCommandList cl)
{
    D11Frame *f = d11_cur_frame();
    const uint32_t idx = cl.id & D3DP_INDEX_MASK;
    if (idx == 0 || idx > f->listCount ||
        (cl.id >> D3DP_GEN_SHIFT) != (uint32_t)(g_d11.frameIndex & D3DP_GEN_MASK)) {
        return NULL;
    }
    return &f->lists[idx - 1];
}

void d11_cmd_touch(D11CommandList *c, uint32_t buffer, uint32_t part, uint64_t lo, uint64_t hi)
{
    RhiBuffer h = {buffer};
    D11Buffer *b = d11_get_buffer(h);
    if (!c || !b || b->kind != RHI_MEM_UPLOAD || !b->shadow || part >= D11_PART_COUNT ||
        !b->part[part]) {
        return;
    }
    hi = hi < b->size ? hi : b->size;
    if (lo >= hi) {
        return;
    }
    for (uint32_t i = 0; i < c->touchCount; i++) {
        D11Touch *t = &c->touch[i];
        if (t->buffer == buffer && t->part == part) {
            t->lo = lo < t->lo ? lo : t->lo;
            t->hi = hi > t->hi ? hi : t->hi;
            return;
        }
    }
    if (c->touchCount == c->touchCap) {
        const uint32_t cap = c->touchCap ? c->touchCap * 2u : 16u;
        D11Touch *t = realloc(c->touch, cap * sizeof(*t));
        if (!t) {
            /* no room to note it: write it now on the immediate context
             * (the CPU wrote it already; a later write before the submit
             * would be missed, which the ring's order rules out) */
            d11_buffer_flush(b, g_d11.ctx1, part, lo, hi);
            return;
        }
        c->touch = t;
        c->touchCap = cap;
    }
    c->touch[c->touchCount++] = (D11Touch){buffer, part, lo, hi};
}

static bool d11_new_list(D11CommandList *c)
{
    if (!D11_CHECK(ID3D11Device_CreateDeferredContext(g_d11.device, 0, &c->ctx))) {
        c->ctx = NULL;
        return false;
    }
    if (!D11_CHECK(ID3D11DeviceContext_QueryInterface(c->ctx, &IID_ID3D11DeviceContext1,
                                                      (void **)&c->ctx1))) {
        D11_RELEASE(c->ctx);
        return false;
    }
    if (g_d11.debugLayer && FAILED(ID3D11DeviceContext_QueryInterface(
                                c->ctx, &IID_ID3DUserDefinedAnnotation, (void **)&c->annot))) {
        c->annot = NULL;
    }
    return true;
}

RhiCommandList rhi_begin_commands(void)
{
    RhiCommandList out = {0};
    D11Frame *f = d11_cur_frame();
    if (!g_d11.initialised) {
        return out;
    }
    if (f->listCount == D11_MAX_CMD_LISTS) {
        D11_LOG("more than %d command lists in one frame", D11_MAX_CMD_LISTS);
        return out;
    }
    D11CommandList *c = &f->lists[f->listCount];
    if (!c->ctx && !d11_new_list(c)) {
        return out;
    }
    c->recording = true;
    c->submitted = false;
    c->finishFailed = false;
    c->serial = ++g_d11.listSerial;
    c->touchCount = 0;
    memset(c->tsMask, 0, sizeof(c->tsMask));
    memset(&c->st, 0, sizeof(c->st));
    f->listCount++;
    out.id = ((uint32_t)(g_d11.frameIndex & D3DP_GEN_MASK) << D3DP_GEN_SHIFT) | f->listCount;
    return out;
}

void rhi_end_commands(RhiCommandList cl)
{
    D11CommandList *c = d11_get_cmd(cl);
    if (!c || !c->recording) {
        return;
    }
    if (c->st.inPass) {
        D11_LOG("rhi_end_commands inside a render pass");
        c->st.inPass = false;
    }
    /* the deferred context's state is cleared (RestoreDeferredContextState
     * FALSE): each recording starts from nothing bound */
    c->finishFailed = !D11_CHECK(ID3D11DeviceContext_FinishCommandList(c->ctx, FALSE, &c->list));
    if (c->finishFailed) {
        c->list = NULL;
    }
    c->recording = false;
}

static void d11_note_submit(uint64_t serial)
{
    const uint32_t i = g_d11.submitCount++ % D11_SUBMIT_HISTORY;
    g_d11.submits[i].serial = serial;
    g_d11.submits[i].replay = g_d11.tagReplay;
    g_d11.submits[i].frame = g_d11.tagFrame;
}

void rhi_submit(RhiCommandList cl)
{
    D11CommandList *c = d11_get_cmd(cl);
    D11Frame *f = d11_cur_frame();
    if (!c || c->submitted) {
        return;
    }
    if (c->recording) {
        rhi_end_commands(cl);
    }
    c->submitted = true;
    if (c->finishFailed || !c->list || g_d11.deviceLost) {
        if (c->finishFailed && !g_d11.deviceLost) {
            D11_LOG("rhi_submit: the list did not finish; its commands are dropped");
        }
        return;
    }
    /* what the list reads from upload buffers, from their shadows */
    for (uint32_t i = 0; i < c->touchCount; i++) {
        RhiBuffer h = {c->touch[i].buffer};
        D11Buffer *b = d11_get_buffer(h);
        if (b) {
            d11_buffer_flush(b, g_d11.ctx1, c->touch[i].part, c->touch[i].lo, c->touch[i].hi);
        }
    }
    if (g_d11.timestamps && !f->tsDisjointBegun) {
        ID3D11DeviceContext_Begin(g_d11.ctx, (ID3D11Asynchronous *)f->tsDisjoint);
        f->tsDisjointBegun = true;
    }
    ID3D11DeviceContext_ExecuteCommandList(g_d11.ctx, c->list, FALSE);
    g_d11.counts.submits++;
    for (uint32_t w = 0; w < RHI_TIMESTAMP_WORDS; w++) {
        f->tsWritten[w] |= c->tsMask[w];
    }
    /* the slot's event: the last submit's end is the one waited for */
    ID3D11DeviceContext_End(g_d11.ctx, (ID3D11Asynchronous *)f->event);
    f->eventIssued = true;
    d11_note_submit(c->serial);
    d11_drain_messages();
}

void rhi_set_submit_tag(uint32_t replay, uint32_t frame)
{
    g_d11.tagReplay = replay;
    g_d11.tagFrame = frame;
}

/* not tracked (rhi.h): a hung device is found by the driver's timeout */
bool rhi_gpu_progress(RhiGpuProgress *out)
{
    memset(out, 0, sizeof(*out));
    return false;
}

/* no breadcrumbs on Direct3D 11 */
void rhi_cmd_checkpoint(RhiCommandList cl, const void *marker)
{
    (void)cl;
    (void)marker;
}

/* PIX markers through ID3DUserDefinedAnnotation, only with the debug
 * layer */
void rhi_cmd_begin_label(RhiCommandList cl, const char *name)
{
    D11CommandList *c = d11_get_cmd(cl);
    if (!c || !c->annot || !c->recording || !name) {
        return;
    }
    WCHAR w[64];
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 64) > 0) {
        ID3DUserDefinedAnnotation_BeginEvent(c->annot, w);
    }
}

void rhi_cmd_end_label(RhiCommandList cl)
{
    D11CommandList *c = d11_get_cmd(cl);
    if (!c || !c->annot || !c->recording) {
        return;
    }
    ID3DUserDefinedAnnotation_EndEvent(c->annot);
}

/* ------------------------------------------------------------- lifecycle */
static const char *d11_level_name(D3D_FEATURE_LEVEL l)
{
    return l == D3D_FEATURE_LEVEL_11_1 ? "11_1" : l == D3D_FEATURE_LEVEL_11_0 ? "11_0" : "other";
}

/* The 11.1 interfaces and the constant buffer windows the bind groups
 * use; false (and a line) when the system lacks them. */
static bool d11_require_11_1(void)
{
    if (FAILED(ID3D11Device_QueryInterface(g_d11.device, &IID_ID3D11Device1,
                                           (void **)&g_d11.device1)) ||
        FAILED(ID3D11DeviceContext_QueryInterface(g_d11.ctx, &IID_ID3D11DeviceContext1,
                                                  (void **)&g_d11.ctx1))) {
        D11_LOG("Direct3D 11.1 is needed (ID3D11Device1 and ID3D11DeviceContext1: Windows 8 or "
                "later); this system has 11.0 only");
        return false;
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS o;
    memset(&o, 0, sizeof(o));
    if (FAILED(ID3D11Device_CheckFeatureSupport(g_d11.device, D3D11_FEATURE_D3D11_OPTIONS, &o,
                                                sizeof(o))) ||
        !o.ConstantBufferOffsetting) {
        D11_LOG("Direct3D 11.1 is needed: the driver offers no constant buffer offsets "
                "(VSSetConstantBuffers1)");
        return false;
    }
    g_d11.cbPartialUpdate = o.ConstantBufferPartialUpdate != FALSE;
    D3D11_FEATURE_DATA_THREADING th;
    memset(&th, 0, sizeof(th));
    if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(g_d11.device, D3D11_FEATURE_THREADING, &th,
                                                   sizeof(th)))) {
        g_d11.driverCommandLists = th.DriverCommandLists != FALSE;
        g_d11.driverConcurrentCreates = th.DriverConcurrentCreates != FALSE;
    }
    return true;
}

bool rhi_init(const RhiDeviceDesc *desc)
{
    if (g_d11.initialised) {
        return true;
    }
    HMODULE keepD3d = g_d11.d3d11Dll, keepDxgi = g_d11.dxgiDll;
    memset(&g_d11, 0, sizeof(g_d11));
    g_d11.d3d11Dll = keepD3d; /* kept loaded across rhi_shutdown */
    g_d11.dxgiDll = keepDxgi;
    const bool debug = d11_debug_wanted(desc);
    if (!d11_load_runtime(debug)) {
        rhi_shutdown();
        return false;
    }
    d11_pick_adapter();
    if (!d11_create_device(debug)) {
        rhi_shutdown();
        return false;
    }
    d11_describe_adapter();
    if (g_d11.debugLayer) {
        d11_setup_info_queue();
    }
    if (!d11_require_11_1()) {
        rhi_shutdown();
        return false;
    }
    d11_fill_limits();
    if (!d3dp_pool_init(&g_d11.buffers, "buffer", 4096, sizeof(D11Buffer)) ||
        !d3dp_pool_init(&g_d11.textures, "texture", 8192, sizeof(D11Texture)) ||
        !d3dp_pool_init(&g_d11.samplers, "sampler", 1024, sizeof(D11Sampler)) ||
        !d3dp_pool_init(&g_d11.shaders, "shader", 512, sizeof(D11Shader)) ||
        !d3dp_pool_init(&g_d11.layouts, "bind group layout", 64, sizeof(D11Layout)) ||
        !d3dp_pool_init(&g_d11.pipelines, "pipeline", RHI_PIPELINE_POOL_MAX, sizeof(D11Pipeline))) {
        rhi_shutdown();
        return false;
    }
    if (!d11_frames_init()) {
        rhi_shutdown();
        return false;
    }
    {
        /* the sampler an unbound sampler slot gets: point, clamp */
        D3D11_SAMPLER_DESC sd;
        memset(&sd, 0, sizeof(sd));
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy = 1;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (!D11_CHECK(ID3D11Device_CreateSamplerState(g_d11.device, &sd, &g_d11.defaultSampler))) {
            rhi_shutdown();
            return false;
        }
    }
    d11_cache_setup();
    g_d11.initialised = true;
    if (desc->sdlWindow) {
#ifdef ICO_RHI_HAVE_SDL
        SDL_Window *w = (SDL_Window *)desc->sdlWindow;
        g_d11.window = w;
        g_d11.hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(w),
                                                  SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(w, &pw, &ph);
        if (!g_d11.hwnd || !d11_swapchain_create((uint32_t)pw, (uint32_t)ph, desc->vsync)) {
            D11_LOG("no swapchain on the SDL window (HWND %p)", (void *)g_d11.hwnd);
            rhi_shutdown();
            return false;
        }
#else
        D11_LOG("built without SDL3: no window support");
        rhi_shutdown();
        return false;
#endif
    }
    D11_LOG("%s (D3D11, feature level %s%s%s); 11.1 interfaces yes, partial constant buffer "
            "updates %s, command lists %s; tearing %s; debug layer %s; shader cache %s%s",
            g_d11.adapterName, d11_level_name(g_d11.featureLevel), g_d11.warp ? ", WARP" : "",
            s_driverText, g_d11.cbPartialUpdate ? "yes" : "no",
            g_d11.driverCommandLists ? "by the driver" : "emulated by the runtime",
            g_d11.swapchain ? (g_d11.tearing ? "yes" : "no") : "n/a",
            g_d11.info ? "on" : (debug ? "asked for, not installed" : "off"),
            g_d11.cacheDir[0] ? g_d11.cacheDir : "none", g_d11.swapchain ? "" : "; headless");
    d11_drain_messages();
    return true;
}

void rhi_shutdown(void)
{
    if (g_d11.ctx && g_d11.idleEvent && !g_d11.deviceLost) {
        d11_wait_query(g_d11.idleEvent);
    }
    if (g_d11.initialised) {
        d11_cache_stats_log(true);
    }
    d11_swapchain_destroy();
    if (g_d11.ctx) {
        ID3D11DeviceContext_ClearState(g_d11.ctx);
    }
    d11_frames_shutdown();
    d11_pipelines_release();
    d11_release_all_objects();
    d3dp_pool_free(&g_d11.buffers);
    d3dp_pool_free(&g_d11.textures);
    d3dp_pool_free(&g_d11.samplers);
    d3dp_pool_free(&g_d11.shaders);
    d3dp_pool_free(&g_d11.layouts);
    d3dp_pool_free(&g_d11.pipelines);
    D11_RELEASE(g_d11.defaultSampler);
    if (g_d11.ctx) {
        ID3D11DeviceContext_Flush(g_d11.ctx);
    }
    D11_RELEASE(g_d11.ctx1);
    D11_RELEASE(g_d11.ctx);
    d11_drain_messages();
    D11_RELEASE(g_d11.info);
    D11_RELEASE(g_d11.device1);
    D11_RELEASE(g_d11.device);
    D11_RELEASE(g_d11.adapter);
    D11_RELEASE(g_d11.factory);
    const uint32_t errors = g_d11.debugErrors;
    const bool warp = g_d11.warp;
    const D3D_FEATURE_LEVEL level = g_d11.featureLevel;
    HMODULE d3d = g_d11.d3d11Dll, dxgi = g_d11.dxgiDll;
    memset(&g_d11, 0, sizeof(g_d11));
    g_d11.debugErrors = errors; /* readable after shutdown, for tests */
    g_d11.warp = warp;
    g_d11.featureLevel = level;
    g_d11.d3d11Dll = d3d;
    g_d11.dxgiDll = dxgi;
}

RhiBackendKind rhi_backend(void)
{
    return RHI_BACKEND_D3D11;
}

const RhiLimits *rhi_limits(void)
{
    return &g_d11.limits;
}

const char *rhi_adapter_name(void)
{
    return g_d11.adapterName;
}

bool rhi_device_lost(void)
{
    return g_d11.deviceLost;
}

/* The counts are kept at the D3D12 backend's points (RhiStats); the memory
 * gauges read zero. */
void rhi_get_stats(RhiStats *out)
{
    if (out) {
        *out = g_d11.counts;
        out->swapchains = g_d11.swapchains;
        out->presents = g_d11.presents;
        out->presentNs = g_d11.presentNs;
        out->fenceWaits = g_d11.fenceWaits;
        out->fenceWaitNs = g_d11.fenceWaitNs;
        out->acquireNs = g_d11.acquireNs;
        out->pipelinePoolSize = g_d11.pipelines.cap;
        out->pipelinesLive = g_d11.pipelines.next - g_d11.pipelines.freeCount;
        out->pipelinesPeak = g_d11.pipelines.next;
        out->pipelinePoolFull = g_d11.pipelines.full;
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

/* rhi.h: ignored, as on D3D12 (a flip-model swapchain in a window; the
 * window's fullscreen is SDL's and DXGI's to decide) */
void rhi_set_exclusive_fullscreen(bool allowed)
{
    (void)allowed;
}

/* rhi.h: no display timing of presents */
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

void rhi_set_vulkan_loader(void *getInstanceProcAddr)
{
    (void)getInstanceProcAddr; /* Vulkan only */
}

/* The rhi_create_backend entry (port/rhi/rhi_backend.h). */
RHI_BACKEND_DEFINE(rhi_backend_d3d11, "d3d11");
