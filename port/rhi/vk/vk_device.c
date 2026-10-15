/* vk_device.c: instance, physical device selection, logical device, limits,
 * validation layers and the handle pools of the Vulkan backend. */
#include "vk_internal.h"
#include "rhi_vk.h"
#include "../rhi_backend.h"
#include "../rhi_driver_version.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#else

#include <time.h>

#endif
#ifdef ICO_RHI_HAVE_SDL

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#endif

VkrState g_vkr;

uint64_t vkr_now_ns(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
#endif
}

bool vkr_check(VkResult r, const char *what, const char *file, int line)
{
    if (r == VK_SUCCESS) {
        return true;
    }
    fprintf(stderr, "rhi_vk: %s failed (VkResult %d) at %s:%d\n", what, (int)r, file, line);
    if (r == VK_ERROR_DEVICE_LOST && !g_vkr.deviceLost) {
        g_vkr.deviceLost = true;
        g_vkr.lostNs = vkr_now_ns();
        fprintf(stderr, "rhi_vk: device lost\n");
        vkr_lost_report();
    }
    return false;
}

/* a switch from the environment (ico-pc.ini's [dev] keys arrive there,
 * port/platform/host_config.c ico_ini_export): on for 1, true, on, yes */
static bool vkr_env_on(const char *name)
{
    const char *v = getenv(name);
    return v && (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0 ||
                 strcmp(v, "yes") == 0);
}

/* the environment's word on a switch: 1 on, 0 off (0, false, off, no), -1
 * when it is unset or says neither */
static int vkr_env_switch(const char *name)
{
    const char *v = getenv(name);
    if (!v) {
        return -1;
    }
    if (vkr_env_on(name)) {
        return 1;
    }
    return strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "off") == 0 ||
                   strcmp(v, "no") == 0
               ? 0
               : -1;
}

/* ------------------------------------------------------------ device loss
 * The report vkr_check prints once, at the first VK_ERROR_DEVICE_LOST
 * (docs/port/RHI.md, "Device loss"): the last submissions with the frames
 * they carried and whether the GPU finished them, the driver's fault report
 * (VK_EXT_device_fault: the faulting addresses, each matched to a resource
 * through the allocation table, vk_resource.c), and the checkpoints the GPU
 * reached (VK_NV_device_diagnostic_checkpoints, under gpu_checkpoints).
 * The renderer's pass ring follows from the window (port/render
 * rd_passring.h, port/platform/window_host.c device_lost_quit). */
static const char *vkr_fault_type_name(VkDeviceFaultAddressTypeEXT t)
{
    switch (t) {
    case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT:
        return "none";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:
        return "invalid read";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:
        return "invalid write";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:
        return "invalid execute";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT:
        return "instruction pointer (unknown)";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT:
        return "instruction pointer (invalid)";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:
        return "instruction pointer (fault)";
    default:
        return "unknown type";
    }
}

static const char *vkr_stage_name(VkPipelineStageFlagBits s)
{
    switch (s) {
    case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
        return "top of pipe (started)";
    case VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:
        return "bottom of pipe (finished)";
    default:
        return "another stage";
    }
}

static void vkr_report_submits(void)
{
    uint64_t reached = 0;
    const bool known =
        vkGetSemaphoreCounterValue(g_vkr.device, g_vkr.timeline, &reached) == VK_SUCCESS;
    const uint32_t n =
        g_vkr.submitCount < VKR_SUBMIT_HISTORY ? g_vkr.submitCount : VKR_SUBMIT_HISTORY;
    VKR_LOG("loss report: the last %u submissions (timeline reached %s%llu%s):", n,
            known ? "" : "unknown, ", (unsigned long long)(known ? reached : 0ull),
            g_vkr.lostWaitValue ? "" : "; no wait failed");
    for (uint32_t k = 0; k < n; k++) {
        const uint32_t i = (g_vkr.submitCount - n + k) % VKR_SUBMIT_HISTORY;
        const uint64_t v = g_vkr.submits[i].value;
        VKR_LOG("  timeline %llu: replay %u, frame %u, %u command list%s%s, %s%s",
                (unsigned long long)v, g_vkr.submits[i].replay, g_vkr.submits[i].frame,
                g_vkr.submits[i].lists, g_vkr.submits[i].lists == 1 ? "" : "s",
                g_vkr.submits[i].present ? " + the present signal" : "",
                !known         ? "state unknown"
                : v <= reached ? "finished"
                               : "not finished",
                v == g_vkr.lostWaitValue ? " (the wait that failed)" : "");
    }
}

static void vkr_report_fault(void)
{
    if (!g_vkr.deviceFault || !g_vkr.getFaultInfo) {
        VKR_LOG("loss report: no fault report (VK_EXT_device_fault not offered by the driver)");
        return;
    }
    VkDeviceFaultCountsEXT counts = {.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
    VkResult r = g_vkr.getFaultInfo(g_vkr.device, &counts, NULL);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        VKR_LOG("loss report: vkGetDeviceFaultInfoEXT (counts) returned %d", (int)r);
        return;
    }
    VkDeviceFaultAddressInfoEXT *addr =
        calloc(counts.addressInfoCount ? counts.addressInfoCount : 1, sizeof(*addr));
    VkDeviceFaultVendorInfoEXT *vend =
        calloc(counts.vendorInfoCount ? counts.vendorInfoCount : 1, sizeof(*vend));
    if (!addr || !vend) {
        free(addr);
        free(vend);
        return;
    }
    counts.vendorBinarySize = 0; /* the vendor binary is not asked for */
    VkDeviceFaultInfoEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
        .pAddressInfos = addr,
        .pVendorInfos = vend,
    };
    r = g_vkr.getFaultInfo(g_vkr.device, &counts, &info);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        VKR_LOG("loss report: vkGetDeviceFaultInfoEXT returned %d", (int)r);
    } else {
        info.description[VK_MAX_DESCRIPTION_SIZE - 1] = '\0';
        VKR_LOG("loss report: the driver's fault report: \"%s\", %u address%s, %u vendor "
                "entr%s",
                info.description, counts.addressInfoCount, counts.addressInfoCount == 1 ? "" : "es",
                counts.vendorInfoCount, counts.vendorInfoCount == 1 ? "y" : "ies");
        for (uint32_t i = 0; i < counts.addressInfoCount; i++) {
            const uint64_t a = addr[i].reportedAddress;
            const uint64_t prec = addr[i].addressPrecision ? addr[i].addressPrecision : 1;
            VKR_LOG("  %s at 0x%016llx (precision %llu bytes: 0x%016llx..0x%016llx)",
                    vkr_fault_type_name(addr[i].addressType), (unsigned long long)a,
                    (unsigned long long)prec, (unsigned long long)(a & ~(prec - 1)),
                    (unsigned long long)((a & ~(prec - 1)) + prec - 1));
            if (addr[i].addressType != VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT) {
                vkr_alloc_nearest(a & ~(prec - 1));
            }
        }
        for (uint32_t i = 0; i < counts.vendorInfoCount; i++) {
            vend[i].description[VK_MAX_DESCRIPTION_SIZE - 1] = '\0';
            VKR_LOG("  vendor: \"%s\" code 0x%llx data 0x%llx", vend[i].description,
                    (unsigned long long)vend[i].vendorFaultCode,
                    (unsigned long long)vend[i].vendorFaultData);
        }
    }
    free(addr);
    free(vend);
}

static void vkr_report_checkpoints(void)
{
    if (!g_vkr.checkpoints || !g_vkr.getCheckpointData) {
        VKR_LOG("loss report: no checkpoints (gpu_checkpoints = true in ico-pc.ini turns them on "
                "where the driver offers them)");
        return;
    }
    uint32_t n = 0;
    g_vkr.getCheckpointData(g_vkr.queue, &n, NULL);
    VkCheckpointDataNV *cp = calloc(n ? n : 1, sizeof(*cp));
    if (!cp) {
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        cp[i].sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
    }
    g_vkr.getCheckpointData(g_vkr.queue, &n, cp);
    VKR_LOG("loss report: %u checkpoint%s reached (markers are pass numbers of the pass ring "
            "below)",
            n, n == 1 ? "" : "s");
    for (uint32_t i = 0; i < n; i++) {
        VKR_LOG("  %s: pass #%llu", vkr_stage_name(cp[i].stage),
                (unsigned long long)(uintptr_t)cp[i].pCheckpointMarker);
    }
    free(cp);
}

void vkr_lost_report(void)
{
    if (g_vkr.lostReported || !g_vkr.device) {
        return;
    }
    g_vkr.lostReported = true;
    vkr_report_submits();
    vkr_report_fault();
    vkr_report_checkpoints();
    fflush(stderr);
}

/* VK_EXT_device_address_binding_report: each buffer's and image's GPU
 * address range into the allocation table (vk_resource.c vkr_alloc_bound) */
static VKAPI_ATTR VkBool32 VKAPI_CALL vkr_bind_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
    (void)severity;
    (void)user;
    if (!(types & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT) || !data ||
        data->objectCount == 0) {
        return VK_FALSE;
    }
    for (const VkBaseInStructure *p = data->pNext; p; p = p->pNext) {
        if (p->sType == VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT) {
            const VkDeviceAddressBindingCallbackDataEXT *b =
                (const VkDeviceAddressBindingCallbackDataEXT *)p;
            vkr_alloc_bound(data->pObjects[0].objectHandle, b->baseAddress, b->size,
                            b->bindingType == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
        }
    }
    return VK_FALSE;
}

/* ------------------------------------------------------------ handle pools */
bool vkr_pool_init(VkrPool *p, const char *name, uint32_t cap, uint32_t elemSize)
{
    memset(p, 0, sizeof(*p));
    p->name = name;
    p->cap = cap;
    p->elemSize = elemSize;
    p->data = calloc(cap, elemSize);
    p->gen = calloc(cap, sizeof(uint16_t));
    p->live = calloc(cap, 1);
    p->freeList = calloc(cap, sizeof(uint32_t));
    return p->data && p->gen && p->live && p->freeList;
}

void vkr_pool_free(VkrPool *p)
{
    free(p->data);
    free(p->gen);
    free(p->live);
    free(p->freeList);
    memset(p, 0, sizeof(*p));
}

uint32_t vkr_pool_alloc(VkrPool *p, void **out)
{
    uint32_t idx;
    if (p->freeCount > 0) {
        idx = p->freeList[--p->freeCount];
    } else if (p->next < p->cap) {
        idx = p->next++;
    } else {
        VKR_LOG("%s pool full (%u)", p->name, p->cap);
        return 0;
    }
    p->live[idx] = 1;
    void *e = p->data + (size_t)idx * p->elemSize;
    memset(e, 0, p->elemSize);
    if (out) {
        *out = e;
    }
    return ((uint32_t)(p->gen[idx] & VKR_GEN_MASK) << VKR_GEN_SHIFT) | (idx + 1u);
}

void *vkr_pool_get(const VkrPool *p, uint32_t id)
{
    uint32_t idx = (id & VKR_INDEX_MASK);
    if (idx == 0 || idx > p->cap) {
        return NULL;
    }
    idx -= 1u;
    if (!p->live[idx] || (p->gen[idx] & VKR_GEN_MASK) != (id >> VKR_GEN_SHIFT)) {
        return NULL;
    }
    return p->data + (size_t)idx * p->elemSize;
}

void vkr_pool_release(VkrPool *p, uint32_t id)
{
    if (!vkr_pool_get(p, id)) {
        return;
    }
    uint32_t idx = (id & VKR_INDEX_MASK) - 1u;
    p->live[idx] = 0;
    p->gen[idx] = (uint16_t)((p->gen[idx] + 1u) & VKR_GEN_MASK);
    p->freeList[p->freeCount++] = idx;
}

/* ------------------------------------------------------- debug messenger */
static VKAPI_ATTR VkBool32 VKAPI_CALL vkr_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
    (void)types;
    (void)user;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        g_vkr.validationErrors++;
        fprintf(stderr, "rhi_vk validation error: %s\n", data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        fprintf(stderr, "rhi_vk validation warning: %s\n", data->pMessage);
    }
    return VK_FALSE;
}

const char *vkr_test_last_limit(void)
{
    return g_vkr.lastLimit;
}

uint32_t vkr_test_limit(const char *name)
{
    const VkPhysicalDeviceLimits *l = &g_vkr.props.limits;
    if (!name) {
        return 0;
    }
    if (strcmp(name, "maxBoundDescriptorSets") == 0) {
        return l->maxBoundDescriptorSets;
    }
    if (strcmp(name, "maxPerStageDescriptorSampledImages") == 0) {
        return l->maxPerStageDescriptorSampledImages;
    }
    if (strcmp(name, "maxMemoryAllocationCount") == 0) {
        return l->maxMemoryAllocationCount;
    }
    if (strcmp(name, "maxSamplerAllocationCount") == 0) {
        return l->maxSamplerAllocationCount;
    }
    if (strcmp(name, "minStorageBufferOffsetAlignment") == 0) {
        return (uint32_t)l->minStorageBufferOffsetAlignment;
    }
    if (strcmp(name, "maxPushConstantsSize") == 0) {
        return l->maxPushConstantsSize;
    }
    return 0;
}

uint32_t rhi_vk_validation_error_count(void)
{
    return g_vkr.validationErrors;
}

/* ---------------------------------------------------------------- instance */
static bool vkr_has_layer(const char *name)
{
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, NULL);
    VkLayerProperties *props = calloc(n ? n : 1, sizeof(*props));
    bool found = false;
    if (props && vkEnumerateInstanceLayerProperties(&n, props) == VK_SUCCESS) {
        for (uint32_t i = 0; i < n && !found; i++) {
            found = strcmp(props[i].layerName, name) == 0;
        }
    }
    free(props);
    return found;
}

/* the injector and the overlay among the instance layers (rhi.h
 * rhi_injector_name), found once after volk is up; kept out of g_vkr so
 * rhi_init's reset leaves them to this scan */
static const char *s_vkrInjector, *s_vkrOverlay;

static const char *vkr_env(const char *name)
{
    return getenv(name);
}

static void vkr_scan_injectors(void)
{
    s_vkrInjector = s_vkrOverlay = NULL;
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, NULL);
    VkLayerProperties *props = calloc(n ? n : 1, sizeof(*props));
    if (props && vkEnumerateInstanceLayerProperties(&n, props) == VK_SUCCESS) {
        for (uint32_t i = 0; i < n; i++) {
            const char *l = props[i].layerName;
            if (!rhi_layer_switched_on(l, vkr_env)) {
                continue;
            }
            if (!s_vkrInjector) {
                s_vkrInjector = rhi_injector_from_layer_name(l);
            }
            if (!s_vkrOverlay) {
                s_vkrOverlay = rhi_overlay_from_layer_name(l);
            }
        }
    }
    free(props);
}

const char *rhi_injector_name(void)
{
    return s_vkrInjector;
}

const char *rhi_overlay_name(void)
{
    return s_vkrOverlay;
}

static bool vkr_has_instance_ext(const char *name)
{
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &n, NULL);
    VkExtensionProperties *props = calloc(n ? n : 1, sizeof(*props));
    bool found = false;
    if (props && vkEnumerateInstanceExtensionProperties(NULL, &n, props) == VK_SUCCESS) {
        for (uint32_t i = 0; i < n && !found; i++) {
            found = strcmp(props[i].extensionName, name) == 0;
        }
    }
    free(props);
    return found;
}

#ifdef ICO_RHI_MOLTENVK
/* The instance's API version (vkr_create_instance): linked straight to
 * MoltenVK there is no loader above the driver, so the instance asks for
 * no more than the driver reports */
static uint32_t s_instanceApi = VK_API_VERSION_1_3;
#endif

/* Whether a device of this API version takes the Vulkan 1.3 core paths
 * (dynamic rendering, VkPhysicalDeviceVulkan13Features): its own version,
 * and under MoltenVK the instance's too */
static bool vkr_core13(uint32_t deviceApi)
{
#ifdef ICO_RHI_MOLTENVK
    return deviceApi >= VK_API_VERSION_1_3 && s_instanceApi >= VK_API_VERSION_1_3;
#else
    return deviceApi >= VK_API_VERSION_1_3;
#endif
}

/* VK_EXT_debug_utils is enabled on the instance (validation, or
 * gpu_checkpoints for the address binding report) */
static bool s_debugUtilsExt;

static bool vkr_create_instance(const RhiDeviceDesc *desc)
{
    const char *exts[16];
    uint32_t extCount = 0;
    VkInstanceCreateFlags flags = 0;
    const char *layers[1];
    uint32_t layerCount = 0;

    if (desc->sdlWindow) {
#ifdef ICO_RHI_HAVE_SDL
        Uint32 n = 0;
        const char *const *sdlExts = SDL_Vulkan_GetInstanceExtensions(&n);
        if (!sdlExts) {
            VKR_LOG("SDL_Vulkan_GetInstanceExtensions: %s", SDL_GetError());
            return false;
        }
        for (Uint32 i = 0; i < n && extCount < 12; i++) {
            exts[extCount++] = sdlExts[i];
        }
#else
        VKR_LOG("built without SDL3: no window support");
        return false;
#endif
    }
    g_vkr.debugUtils = false;
#ifdef ICO_RHI_MOLTENVK
    /* MoltenVK is a portability driver: listed only when asked for. The
     * other builds keep the loader's conformant devices. */
    if (vkr_has_instance_ext(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        exts[extCount++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif
    bool debug = desc->debugLayers;
#ifdef ICO_RHI_DEBUG_DEFAULT
    debug = true;
#endif
    const char *env = getenv("ICO_VK_VALIDATION");
    if (env && *env) {
        debug = env[0] != '0';
    }
    if (debug) {
        if (vkr_has_layer("VK_LAYER_KHRONOS_validation")) {
            layers[layerCount++] = "VK_LAYER_KHRONOS_validation";
            g_vkr.validationOn = true;
        } else {
            VKR_LOG("debugLayers: VK_LAYER_KHRONOS_validation not installed; continuing without");
        }
        if (vkr_has_instance_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            exts[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            g_vkr.debugUtils = true;
        }
    }
    /* gpu_checkpoints: the debug utils messenger carries the address
     * binding report (vkr_create_device), without the validation layer */
    s_debugUtilsExt = g_vkr.debugUtils;
    if (!s_debugUtilsExt && vkr_env_on("ICO_GPU_CHECKPOINTS") &&
        vkr_has_instance_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        exts[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        s_debugUtilsExt = true;
    }

#ifdef ICO_RHI_MOLTENVK
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (!VKR_CHECK(vkEnumerateInstanceVersion(&loaderVersion))) {
        return false;
    }
#else
    uint32_t loaderVersion = volkGetInstanceVersion();
#endif
    if (loaderVersion < VK_API_VERSION_1_2) {
        VKR_LOG("the Vulkan loader is %u.%u; 1.2 or later is required",
                VK_API_VERSION_MAJOR(loaderVersion), VK_API_VERSION_MINOR(loaderVersion));
        return false;
    }
#ifdef ICO_RHI_MOLTENVK
    s_instanceApi = loaderVersion < VK_API_VERSION_1_3 ? loaderVersion : VK_API_VERSION_1_3;
    const uint32_t instanceApi = s_instanceApi;
#else
    const uint32_t instanceApi = VK_API_VERSION_1_3;
#endif
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = desc->appName ? desc->appName : "ico",
        .applicationVersion = 1,
        .pEngineName = "ico rhi",
        .engineVersion = 1,
        .apiVersion = instanceApi,
    };
    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = flags,
        .pApplicationInfo = &app,
        .enabledLayerCount = layerCount,
        .ppEnabledLayerNames = layers,
        .enabledExtensionCount = extCount,
        .ppEnabledExtensionNames = exts,
    };
    if (!VKR_CHECK(vkCreateInstance(&ci, NULL, &g_vkr.instance))) {
        return false;
    }
#ifndef ICO_RHI_MOLTENVK
    volkLoadInstanceOnly(g_vkr.instance);
#endif

    if (g_vkr.debugUtils) {
        VkDebugUtilsMessengerCreateInfoEXT mci = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = vkr_debug_callback,
        };
        VKR_CHECK(vkCreateDebugUtilsMessengerEXT(g_vkr.instance, &mci, NULL, &g_vkr.messenger));
    }
    return true;
}

/* ------------------------------------------------------- device selection */
static bool vkr_device_has_ext(VkPhysicalDevice pd, const char *name)
{
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, NULL, &n, NULL);
    VkExtensionProperties *props = calloc(n ? n : 1, sizeof(*props));
    bool found = false;
    if (props && vkEnumerateDeviceExtensionProperties(pd, NULL, &n, props) == VK_SUCCESS) {
        for (uint32_t i = 0; i < n && !found; i++) {
            found = strcmp(props[i].extensionName, name) == 0;
        }
    }
    free(props);
    return found;
}

static bool vkr_format_ok(VkPhysicalDevice pd, VkFormat f, VkFormatFeatureFlags need)
{
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(pd, f, &fp);
    return (fp.optimalTilingFeatures & need) == need;
}

/* The one read of ICO_VK_FAKE_LIMITS.  =min is noted for vkr_fill_limits
 * (the RhiLimits at the spec's required values).  =mali clamps the
 * device's limits to a Mali-G68's (Samsung A36 class: four descriptor
 * sets, 128 bytes of push constants, 256-byte storage offsets, 16 sampled
 * images, samplers and storage buffers a stage, 4096 memory allocations,
 * 4000 samplers), so the lavapipe tests run under a phone's limits: the
 * checks in rhi_create_pipeline and vkr_allocate (vk_pipeline.c,
 * vk_resource.c) read props.limits.  A maximum only goes down and an
 * alignment only up. */
static void vkr_fake_limits(void)
{
    const char *e = getenv("ICO_VK_FAKE_LIMITS");
    if (!e || !e[0] || strcmp(e, "0") == 0) {
        return;
    }
    if (strcmp(e, "min") == 0) {
        g_vkr.fakeMinLimits = true; /* applied in vkr_fill_limits */
        return;
    }
    if (strcmp(e, "mali") != 0) {
        VKR_LOG(
            "ICO_VK_FAKE_LIMITS=%s: not a known profile (mali, min); the limits are the device's",
            e);
        return;
    }
    VkPhysicalDeviceLimits *l = &g_vkr.props.limits;
#define VKR_FAKE_MAX(f, v)                                                                         \
    do {                                                                                           \
        if (l->f > (v)) {                                                                          \
            l->f = (v);                                                                            \
        }                                                                                          \
    } while (0)
#define VKR_FAKE_MIN(f, v)                                                                         \
    do {                                                                                           \
        if (l->f < (v)) {                                                                          \
            l->f = (v);                                                                            \
        }                                                                                          \
    } while (0)
    VKR_FAKE_MAX(maxBoundDescriptorSets, 4u);
    VKR_FAKE_MAX(maxPushConstantsSize, 128u);
    VKR_FAKE_MIN(minStorageBufferOffsetAlignment, 256u);
    VKR_FAKE_MIN(minUniformBufferOffsetAlignment, 16u);
    VKR_FAKE_MAX(maxPerStageDescriptorSampledImages, 16u);
    VKR_FAKE_MAX(maxPerStageDescriptorSamplers, 16u);
    VKR_FAKE_MAX(maxPerStageDescriptorStorageBuffers, 16u);
    VKR_FAKE_MAX(maxFragmentOutputAttachments, 8u);
    VKR_FAKE_MAX(maxVertexInputBindingStride, 2048u);
    VKR_FAKE_MIN(nonCoherentAtomSize, 64u);
    VKR_FAKE_MAX(maxMemoryAllocationCount, 4096u);
    VKR_FAKE_MAX(maxSamplerAllocationCount, 4000u);
    /* the VS->PS interface and the index values (issue 95): a Mali-G52's
     * 128 components, and the least maxDrawIndexedIndexValue (no
     * fullDrawIndexUint32, which the profile also reports absent) */
    VKR_FAKE_MAX(maxVertexOutputComponents, 128u);
    VKR_FAKE_MAX(maxFragmentInputComponents, 128u);
    VKR_FAKE_MAX(maxDrawIndexedIndexValue, (1u << 24) - 1u);
#undef VKR_FAKE_MAX
#undef VKR_FAKE_MIN
    g_vkr.fakeLimits = "mali";
    VKR_LOG("ICO_VK_FAKE_LIMITS=mali: the device's limits are clamped to a Mali-G68's");
}

/* The format behind RHI_FMT_D32F_S8.  The spec guarantees one of
 * D32_SFLOAT_S8_UINT and D24_UNORM_S8_UINT as an attachment; D32 is
 * preferred.  Either is taken only with the sampled and transfer uses too:
 * the fog and the effects depth sample the scene's depth or a copy of it
 * (rd_core.c rd__sampled_depth; on D24S8 the copy first), and the tests' depth
 * readbacks copy it out (rd_replay.c rd__read_target_depth).  When
 * neither has them all, D32 unless only D24 is an attachment.
 * ICO_VK_FAKE_D24S8=1 takes D24 where the device has it (tests:
 * rhi_vk_d24s8). */
static VkFormat vkr_choose_depth_stencil(VkPhysicalDevice pd, bool *fake)
{
    const VkFormatFeatureFlags ds = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    const VkFormatFeatureFlags all = ds | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                     VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                     VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    const char *e = getenv("ICO_VK_FAKE_D24S8");
    *fake = e && e[0] && e[0] != '0';
    const bool d24 = vkr_format_ok(pd, VK_FORMAT_D24_UNORM_S8_UINT, all);
    const bool d32 = vkr_format_ok(pd, VK_FORMAT_D32_SFLOAT_S8_UINT, all);
    if (d24 && (*fake || !d32)) {
        return VK_FORMAT_D24_UNORM_S8_UINT;
    }
    if (!d32 && !d24 && !vkr_format_ok(pd, VK_FORMAT_D32_SFLOAT_S8_UINT, ds) &&
        vkr_format_ok(pd, VK_FORMAT_D24_UNORM_S8_UINT, ds)) {
        return VK_FORMAT_D24_UNORM_S8_UINT;
    }
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
}

/* "; device 10de:1b80; driver NVIDIA 581.57, version 581.57" for the
 * adapter line: the PCI vendor and device ids (the form d3d12_device.c's
 * unnamed adapter takes), the driver's name and its own version text
 * (VK_KHR_driver_properties, core in Vulkan 1.2, which every device the
 * renderer takes has), and driverVersion decoded the vendor's way
 * (rhi_driver_version.h) */
static char s_driverText[600];

static void vkr_driver_text(VkPhysicalDevice pd, const VkPhysicalDeviceProperties *p)
{
    VkPhysicalDeviceDriverProperties dp = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
    };
    VkPhysicalDeviceProperties2 p2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &dp,
    };
    char ver[32];
    vkGetPhysicalDeviceProperties2(pd, &p2);
    dp.driverName[VK_MAX_DRIVER_NAME_SIZE - 1] = '\0';
    dp.driverInfo[VK_MAX_DRIVER_INFO_SIZE - 1] = '\0';
    rhi_driver_version_text(p->vendorID, (uint32_t)dp.driverID, p->driverVersion, ver, sizeof ver);
    snprintf(s_driverText, sizeof s_driverText, "; device %04x:%04x; driver %s%s%s, version %s",
             (unsigned)p->vendorID, (unsigned)p->deviceID,
             dp.driverName[0] ? dp.driverName : "(unnamed)", dp.driverInfo[0] ? " " : "",
             dp.driverInfo, ver);
}

/* Returns a score (higher is better) or -1 when the device cannot run the
 * renderer.  *outQueue receives the graphics(+present) queue family. */
static int vkr_rate_device(VkPhysicalDevice pd, uint32_t *outQueue, const char **why)
{
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(pd, &p);
    if (p.apiVersion < VK_API_VERSION_1_2) {
        *why = "Vulkan 1.2 not supported";
        return -1;
    }
    bool core13 = vkr_core13(p.apiVersion);
    if (!core13 && !vkr_device_has_ext(pd, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        *why = "no dynamic rendering";
        return -1;
    }
    if (g_vkr.surface && !vkr_device_has_ext(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        *why = "no swapchain extension";
        return -1;
    }
    /* dualSrcBlend is optional (rd's two-pass blend fallback covers its
     * absence, RhiLimits.dualSourceBlend); a device with it is preferred
     * over an otherwise equal one */
    VkPhysicalDeviceFeatures f;
    vkGetPhysicalDeviceFeatures(pd, &f);
    VkPhysicalDeviceVulkan12Features f12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                    .pNext = &f12};
    vkGetPhysicalDeviceFeatures2(pd, &f2);
    if (!f12.timelineSemaphore) {
        *why = "no timeline semaphores";
        return -1;
    }
    const VkFormatFeatureFlags rt =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    const VkFormatFeatureFlags ds = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (!vkr_format_ok(pd, VK_FORMAT_R8G8B8A8_UNORM,
                       rt | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) ||
        !vkr_format_ok(pd, VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
        !vkr_format_ok(pd, VK_FORMAT_R8_UNORM, rt) ||
        !vkr_format_ok(pd, VK_FORMAT_D32_SFLOAT, ds) ||
        (!vkr_format_ok(pd, VK_FORMAT_D32_SFLOAT_S8_UINT, ds) &&
         !vkr_format_ok(pd, VK_FORMAT_D24_UNORM_S8_UINT, ds))) {
        *why = "a required format (RGBA8, RGBA8_UINT, R8, D32F, D32F_S8 or D24S8) is missing";
        return -1;
    }

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, NULL);
    VkQueueFamilyProperties *qp = calloc(qn ? qn : 1, sizeof(*qp));
    if (!qp) {
        return -1;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qp);
    int64_t found = -1;
    for (uint32_t i = 0; i < qn && found < 0; i++) {
        if (!(qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            continue;
        }
        if (g_vkr.surface) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, g_vkr.surface, &present);
            if (!present) {
                continue;
            }
        }
        found = i;
    }
    free(qp);
    if (found < 0) {
        *why = "no graphics queue that can present";
        return -1;
    }
    *outQueue = (uint32_t)found;

    int score = 0;
    switch (p.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        score = 1000;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        score = 500;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        score = 200;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        score = 100;
        break;
    default:
        score = 50;
        break;
    }
    if (core13) {
        score += 10;
    }
    if (f.dualSrcBlend) {
        score += 5;
    }
    return score;
}

static bool vkr_pick_device(void)
{
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g_vkr.instance, &n, NULL);
    if (n == 0) {
        VKR_LOG("no Vulkan devices");
        return false;
    }
    VkPhysicalDevice *pds = calloc(n, sizeof(*pds));
    if (!pds) {
        return false;
    }
    vkEnumeratePhysicalDevices(g_vkr.instance, &n, pds);
    /* ICO_VK_DEVICE=<index> or a substring of the device name forces a pick */
    const char *force = getenv("ICO_VK_DEVICE");
    int best = -1;
    uint32_t bestQueue = 0;
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pds[i], &p);
        uint32_t q = 0;
        const char *why = "";
        int score = vkr_rate_device(pds[i], &q, &why);
        if (score < 0) {
            VKR_LOG("device %u (%s) unsuitable: %s", i, p.deviceName, why);
            continue;
        }
        if (force && *force) {
            char idx[16];
            snprintf(idx, sizeof(idx), "%u", i);
            if (strcmp(force, idx) == 0 || strstr(p.deviceName, force)) {
                score += 100000;
            }
        }
        if (score > best) {
            best = score;
            g_vkr.phys = pds[i];
            bestQueue = q;
        }
    }
    free(pds);
    if (best < 0) {
        VKR_LOG("no suitable Vulkan device");
        return false;
    }
    g_vkr.queueFamily = bestQueue;
    vkGetPhysicalDeviceProperties(g_vkr.phys, &g_vkr.props);
    vkr_driver_text(g_vkr.phys, &g_vkr.props);
    vkr_fake_limits();
    vkGetPhysicalDeviceMemoryProperties(g_vkr.phys, &g_vkr.memProps);
    /* the heaps, and the one uploads (the renderer's ring, texture
       pack uploads) come from: a device-local, host-visible type first
       (vkr_allocate), which without resizable BAR is a window of about
       256 MB that every upload buffer shares: under 1 GB it takes uploads
       of up to 16 MB only, and a full window sends them to system memory
       (vk_memory.c) */
    for (uint32_t i = 0; i < g_vkr.memProps.memoryHeapCount; i++) {
        const VkMemoryHeap *mh = &g_vkr.memProps.memoryHeaps[i];
        VKR_LOG("memory heap %u: %llu MB%s", i, (unsigned long long)(mh->size >> 20),
                (mh->flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? ", device-local" : "");
    }
    {
        const VkMemoryPropertyFlags up = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        uint32_t i = 0;
        while (i < g_vkr.memProps.memoryTypeCount &&
               (g_vkr.memProps.memoryTypes[i].propertyFlags & up) != up) {
            i++;
        }
        if (i < g_vkr.memProps.memoryTypeCount) {
            const uint32_t heap = g_vkr.memProps.memoryTypes[i].heapIndex;
            if (vkr_memory_small_window(&g_vkr.memProps, i)) {
                VKR_LOG("uploads prefer memory heap %u (device-local and host-visible; a %llu MB "
                        "window: uploads over %llu MB go through system memory)",
                        heap, (unsigned long long)(g_vkr.memProps.memoryHeaps[heap].size >> 20),
                        (unsigned long long)(VKR_SMALL_WINDOW_MAX_BYTES >> 20));
            } else {
                VKR_LOG("uploads prefer memory heap %u (device-local and host-visible)", heap);
            }
        } else {
            VKR_LOG("uploads use host memory (no device-local, host-visible type)");
        }
    }
    snprintf(g_vkr.adapterName, sizeof(g_vkr.adapterName), "%s", g_vkr.props.deviceName);
    bool fake = false;
    g_vkr.dsFormat = vkr_choose_depth_stencil(g_vkr.phys, &fake);
    g_vkr.dsSampled =
        vkr_format_ok(g_vkr.phys, g_vkr.dsFormat, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    if (g_vkr.dsFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
        VKR_LOG("depth-stencil: D24S8 in use%s, 24-bit depth (the scene depth has less precision "
                "than the 32-bit float of D32S8)",
                fake ? " (ICO_VK_FAKE_D24S8)" : "");
    }
    return true;
}

/* The sample counts every multisampled target the renderer makes takes
 * (RhiLimits.msaaCounts): the framebuffer and sampled-image limits, and the
 * image format queries of the colour formats (RGBA8 UNORM and UINT) and the
 * scene's depth-stencil, each as an attachment that is sampled and copied
 * from (resolves read it as a transfer source) */
static uint32_t vkr_msaa_counts(void)
{
    const VkPhysicalDeviceLimits *l = &g_vkr.props.limits;
    uint32_t counts =
        (uint32_t)(l->framebufferColorSampleCounts & l->framebufferDepthSampleCounts &
                   l->framebufferStencilSampleCounts & l->sampledImageColorSampleCounts &
                   l->sampledImageIntegerSampleCounts & l->sampledImageDepthSampleCounts);

    const struct {
        VkFormat f;
        VkImageUsageFlags u;
    } q[3] = {
        {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                       VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
        {VK_FORMAT_R8G8B8A8_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
        {g_vkr.dsFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
    };

    for (int i = 0; i < 3; i++) {
        VkImageFormatProperties ip;
        if (vkGetPhysicalDeviceImageFormatProperties(g_vkr.phys, q[i].f, VK_IMAGE_TYPE_2D,
                                                     VK_IMAGE_TILING_OPTIMAL, q[i].u, 0,
                                                     &ip) != VK_SUCCESS) {
            return 1u;
        }
        counts &= (uint32_t)ip.sampleCounts;
    }
    /* the renderer's counts: 2, 4, 8 */
    counts &= (uint32_t)(VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
                         VK_SAMPLE_COUNT_8_BIT);
    VKR_LOG("multisampling: sample counts 0x%x%s", counts | 1u,
            (counts & ~1u) != 0u ? "" : " (single sampling only)");
    return counts | 1u;
}

static bool vkr_create_device(void)
{
    bool core13 = vkr_core13(g_vkr.props.apiVersion);
    g_vkr.apiVersion = g_vkr.props.apiVersion;
#ifdef ICO_RHI_MOLTENVK
    /* the version the device is used at: no more than the instance's */
    if (g_vkr.apiVersion > s_instanceApi) {
        g_vkr.apiVersion = s_instanceApi;
    }
#endif
    const char *exts[16];
    uint32_t extCount = 0;
    if (g_vkr.surface) {
        exts[extCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }
    if (!core13) {
        exts[extCount++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
    }
#ifdef ICO_RHI_MOLTENVK
    /* the subset MoltenVK implements, with its features (below) */
    const bool portability = vkr_device_has_ext(g_vkr.phys, "VK_KHR_portability_subset");
    if (portability) {
        exts[extCount++] = "VK_KHR_portability_subset";
    }
#endif

    VkPhysicalDeviceFeatures avail;
    vkGetPhysicalDeviceFeatures(g_vkr.phys, &avail);
    g_vkr.anisotropy = avail.samplerAnisotropy == VK_TRUE;
    /* ICO_VK_FAKE_NO_DUAL=1 leaves the feature off, as on a device without
     * it (tests: rhi_vk_nodual) */
    const char *fakeNoDual = getenv("ICO_VK_FAKE_NO_DUAL");
    g_vkr.dualSrcBlend =
        avail.dualSrcBlend == VK_TRUE && !(fakeNoDual && fakeNoDual[0] && fakeNoDual[0] != '0');
    if (!g_vkr.dualSrcBlend) {
        VKR_LOG("no dualSrcBlend%s: blending takes the two-pass fallback",
                avail.dualSrcBlend == VK_TRUE ? " (ICO_VK_FAKE_NO_DUAL)" : "");
    }
    /* multisampling: a fragment shader reading SV_SampleIndex (the per-sample
     * fog) needs sampleRateShading; ICO_VK_FAKE_NO_MSAA=1 reports single
     * sampling only (tests) */
    {
        const char *noMs = getenv("ICO_VK_FAKE_NO_MSAA");
        g_vkr.sampleRate = avail.sampleRateShading == VK_TRUE;
        g_vkr.msaaCounts = noMs && noMs[0] && noMs[0] != '0' ? 1u : vkr_msaa_counts();
    }
    /* texture packs: BC1/2/3/7 come with textureCompressionBC (every
     * desktop GPU, the Deck, lavapipe); the formats' sampled and copy
     * features are checked too, so bcTextures promises what it says */
    g_vkr.bc = avail.textureCompressionBC == VK_TRUE;
    g_vkr.fullDrawIndex = avail.fullDrawIndexUint32 == VK_TRUE && g_vkr.fakeLimits == NULL;
    for (int f = RHI_FMT_BC1_UNORM; g_vkr.bc && f <= RHI_FMT_BC7_UNORM; f++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(g_vkr.phys, vkr_vk_format((RhiFormat)f), &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                          VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        g_vkr.bc = (fp.optimalTilingFeatures & need) == need;
    }

    /* the destination read (RhiLimits.fragmentInterlock,
     * rasterOrderAttachment; rd's exact GS blend, port/render/rd_dst_read.c):
     *   interlock    VK_EXT_fragment_shader_interlock's
     *                fragmentShaderPixelInterlock, fragmentStoresAndAtomics
     *                and RGBA8_UNORM storage images (a required format);
     *   rasterOrder  rasterizationOrderColorAttachmentAccess
     *                (VK_EXT_ or VK_ARM_rasterization_order_attachment_access,
     *                the same structure) and VK_KHR_dynamic_rendering_local_read
     *                (input attachments under dynamic rendering).
     * ICO_VK_FAKE_NO_DST=1 leaves both off, as on a device without them. */
    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT qInterlock = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};
    VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT qOrder = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT};
    VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR qLocal = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR};
    {
        const char *noDst = getenv("ICO_VK_FAKE_NO_DST");
        const bool fake = noDst && noDst[0] && noDst[0] != '0';
        const bool extInterlock =
            vkr_device_has_ext(g_vkr.phys, VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME);
        const bool extOrder = vkr_device_has_ext(
            g_vkr.phys, VK_EXT_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_EXTENSION_NAME);
        const bool extOrderArm =
            !extOrder &&
            vkr_device_has_ext(g_vkr.phys,
                               VK_ARM_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_EXTENSION_NAME);
        const bool extLocal =
            vkr_device_has_ext(g_vkr.phys, VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME);
        void *chain = NULL;
        if (extInterlock) {
            qInterlock.pNext = chain;
            chain = &qInterlock;
        }
        if (extOrder || extOrderArm) {
            qOrder.pNext = chain;
            chain = &qOrder;
        }
        if (extLocal) {
            qLocal.pNext = chain;
            chain = &qLocal;
        }
        if (chain) {
            VkPhysicalDeviceFeatures2 q = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                           .pNext = chain};
            vkGetPhysicalDeviceFeatures2(g_vkr.phys, &q);
        }
        g_vkr.interlock = !fake && extInterlock && qInterlock.fragmentShaderPixelInterlock &&
                          avail.fragmentStoresAndAtomics == VK_TRUE &&
                          vkr_format_ok(g_vkr.phys, VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
        g_vkr.rasterOrder = !fake && (extOrder || extOrderArm) &&
                            qOrder.rasterizationOrderColorAttachmentAccess && extLocal &&
                            qLocal.dynamicRenderingLocalRead;
        /* local read for the attachment read, and with the interlock for
         * the barriers a pass may record between draws (vk_cmd.c
         * vkr_dst_order) */
        g_vkr.localRead =
            extLocal && qLocal.dynamicRenderingLocalRead && (g_vkr.rasterOrder || g_vkr.interlock);
        /* the interlock and the rasterization-order access order a
         * destination read after the pass's earlier draws, so no barrier
         * is needed between them; the validation layer's synchronisation
         * checks model neither, so under it (and with
         * ICO_VK_STORAGE_BARRIERS=1) vkr_dst_order puts a by-region barrier
         * before such a draw */
        {
            const char *sb = getenv("ICO_VK_STORAGE_BARRIERS");
            g_vkr.storageBarriers =
                g_vkr.localRead && (g_vkr.validationOn || (sb && sb[0] && sb[0] != '0'));
        }
        VKR_LOG("destination read: interlock %s, rasterization-order attachment read %s%s",
                g_vkr.interlock ? "yes" : "no", g_vkr.rasterOrder ? "yes" : "no",
                fake ? " (ICO_VK_FAKE_NO_DST)" : "");
        /* only what is used is enabled; the queries' structures become the
         * enables, everything else in them off */
        memset(&qInterlock, 0, sizeof(qInterlock));
        qInterlock.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT;
        qInterlock.fragmentShaderPixelInterlock = VK_TRUE;
        memset(&qOrder, 0, sizeof(qOrder));
        qOrder.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT;
        qOrder.rasterizationOrderColorAttachmentAccess = VK_TRUE;
        memset(&qLocal, 0, sizeof(qLocal));
        qLocal.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR;
        qLocal.dynamicRenderingLocalRead = VK_TRUE;
        if (g_vkr.interlock) {
            exts[extCount++] = VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME;
        }
        if (g_vkr.rasterOrder) {
            exts[extCount++] = extOrder
                                   ? VK_EXT_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_EXTENSION_NAME
                                   : VK_ARM_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_EXTENSION_NAME;
        }
        if (g_vkr.localRead) {
            exts[extCount++] = VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME;
        }
    }

    /* the display's timing of presents (rhi_present_timing): VK_KHR_present_id
     * and VK_KHR_present_wait on a window, with SDL for the thread that
     * waits on each present (vk_swapchain.c).  Only when asked for
     * (opt-in with ICO_VK_PRESENT_WAIT=1, vkr_present_wait_wanted): when
     * they were used whenever the driver offered them, they left the
     * screen with nothing usable on an RTX 3090 under Windows with
     * presents in mailbox mode at 120/144/240 and under the vsync lock.  Nothing is queried or enabled otherwise
     * (docs/port/RHI.md, "Vulkan"). */
    VkPhysicalDevicePresentIdFeaturesKHR qPid = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR qPw = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    g_vkr.presentWait = false;
    if (g_vkr.surface) {
        const char *asked = getenv("ICO_VK_PRESENT_WAIT");
#ifdef ICO_RHI_HAVE_SDL
        const bool canThread = true;
#else
        const bool canThread = false;
#endif
        bool offered = false;
        if (vkr_present_wait_wanted(true, asked, canThread) &&
            vkr_device_has_ext(g_vkr.phys, VK_KHR_PRESENT_ID_EXTENSION_NAME) &&
            vkr_device_has_ext(g_vkr.phys, VK_KHR_PRESENT_WAIT_EXTENSION_NAME)) {
            qPid.pNext = &qPw;
            VkPhysicalDeviceFeatures2 q = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                           .pNext = &qPid};
            vkGetPhysicalDeviceFeatures2(g_vkr.phys, &q);
            offered = qPid.presentId && qPw.presentWait;
        }
        g_vkr.presentWait = vkr_present_wait_wanted(offered, asked, canThread);
        if (vkr_present_wait_fake()) {
            /* a test's stand-in: the thread without the extensions */
            g_vkr.presentWait = false;
            g_vkr.presentWaitFake = true;
            g_vkr.waitForPresent = vkr_present_wait_fake();
            VKR_LOG("display timing (present wait): on (a test's stand-in)");
        } else if (!vkr_present_wait_wanted(true, asked, canThread)) {
            VKR_LOG("display timing (present wait): off (ICO_VK_PRESENT_WAIT=1 tries it)");
        } else {
            VKR_LOG("display timing (present wait): %s",
                    g_vkr.presentWait ? "on (ICO_VK_PRESENT_WAIT=1)" : "asked for, not offered");
        }
        qPid.pNext = NULL;
        qPid.presentId = VK_TRUE;
        qPw.pNext = NULL;
        qPw.presentWait = VK_TRUE;
        if (g_vkr.presentWait) {
            exts[extCount++] = VK_KHR_PRESENT_ID_EXTENSION_NAME;
            exts[extCount++] = VK_KHR_PRESENT_WAIT_EXTENSION_NAME;
        }
    }

    /* VK_PRESENT_MODE_FIFO_LATEST_READY (VK_EXT_present_mode_fifo_latest_ready):
     * opt-in with ICO_VK_FIFO_LATEST_READY=1 (vkr_choose_present_mode takes it
     * where FIFO would be taken, so a present never waits for the display
     * and nothing tears); unverified on hardware, so never on by itself */
    VkPhysicalDevicePresentModeFifoLatestReadyFeaturesEXT qLatest = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_EXT};
    g_vkr.fifoLatestReady = false;
    if (g_vkr.surface) {
        const char *asked = getenv("ICO_VK_FIFO_LATEST_READY");
        const bool wanted = asked != NULL && asked[0] != '\0' && strcmp(asked, "0") != 0;

        if (wanted &&
            vkr_device_has_ext(g_vkr.phys, VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME)) {
            VkPhysicalDeviceFeatures2 q = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                           .pNext = &qLatest};
            vkGetPhysicalDeviceFeatures2(g_vkr.phys, &q);
            g_vkr.fifoLatestReady = qLatest.presentModeFifoLatestReady != 0;
        }
        if (wanted) {
            VKR_LOG("present mode fifo latest ready: %s", g_vkr.fifoLatestReady
                                                              ? "on (ICO_VK_FIFO_LATEST_READY=1)"
                                                              : "asked for, not offered");
        }
        qLatest.pNext = NULL;
        qLatest.presentModeFifoLatestReady = VK_TRUE;
        if (g_vkr.fifoLatestReady) {
            exts[extCount++] = VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME;
        }
    }

    /* GPU loss diagnostics (VkrState): the fault report whenever offered;
     * robust access under gpu_robust (on by default on Android);
     * checkpoints and the address binding report under gpu_checkpoints.
     * Queried, then the queries' structures become the enables with only
     * what is used on. */
    VkPhysicalDeviceFaultFeaturesEXT qFault = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    VkPhysicalDeviceRobustness2FeaturesEXT qRobust2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceAddressBindingReportFeaturesEXT qBind = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT};
    {
        /* robust access: on by default on Android, where a Mali GPU reads
         * storage buffers through plain pointers and an out-of-range read
         * loses the device (issue 95); off by default on the desktop, whose
         * GPUs bound their descriptors in hardware.  gpu_robust overrides
         * either way (ico-pc.ini [dev], exported as ICO_GPU_ROBUST;
         * gpu_robust=false turns it all off).  Wherever robust access is on,
         * VK_EXT_robustness2's robustBufferAccess2 and robustImageAccess2 go
         * with it when the device offers them (reads past a range give zero,
         * on every descriptor kind); nullDescriptor only when gpu_robust
         * asks */
        const int robustSaid = vkr_env_switch("ICO_GPU_ROBUST");
#ifdef __ANDROID__
        const bool robustDefault = true;
#else
        const bool robustDefault = false;
#endif
        const bool wantRobust = robustSaid >= 0 ? robustSaid == 1 : robustDefault;
        const bool wantCheck = vkr_env_on("ICO_GPU_CHECKPOINTS");
        const bool extFault = vkr_device_has_ext(g_vkr.phys, VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
        const bool extRobust2 =
            wantRobust && vkr_device_has_ext(g_vkr.phys, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
        const bool extBind =
            wantCheck && s_debugUtilsExt &&
            vkr_device_has_ext(g_vkr.phys, VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);
        const bool extCheck =
            wantCheck &&
            vkr_device_has_ext(g_vkr.phys, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
        void *chain = NULL;
        if (extFault) {
            qFault.pNext = chain;
            chain = &qFault;
        }
        if (extRobust2) {
            qRobust2.pNext = chain;
            chain = &qRobust2;
        }
        if (extBind) {
            qBind.pNext = chain;
            chain = &qBind;
        }
        if (chain) {
            VkPhysicalDeviceFeatures2 q = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                           .pNext = chain};
            vkGetPhysicalDeviceFeatures2(g_vkr.phys, &q);
        }
        g_vkr.deviceFault = extFault && qFault.deviceFault;
        g_vkr.robust = wantRobust && avail.robustBufferAccess == VK_TRUE;
        const bool buffer2 = g_vkr.robust && extRobust2 && qRobust2.robustBufferAccess2;
        const bool image2 = g_vkr.robust && extRobust2 && qRobust2.robustImageAccess2;
        const bool nullDesc =
            g_vkr.robust && extRobust2 && robustSaid == 1 && qRobust2.nullDescriptor;
        g_vkr.robust2 = buffer2 || image2 || nullDesc;
        g_vkr.addressReport = extBind && qBind.reportAddressBinding;
        g_vkr.checkpoints = extCheck;
        memset(&qFault, 0, sizeof(qFault));
        qFault.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
        qFault.deviceFault = VK_TRUE;
        memset(&qRobust2, 0, sizeof(qRobust2));
        qRobust2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
        qRobust2.robustBufferAccess2 = buffer2 ? VK_TRUE : VK_FALSE;
        qRobust2.robustImageAccess2 = image2 ? VK_TRUE : VK_FALSE;
        qRobust2.nullDescriptor = nullDesc ? VK_TRUE : VK_FALSE;
        memset(&qBind, 0, sizeof(qBind));
        qBind.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT;
        qBind.reportAddressBinding = VK_TRUE;
        if (g_vkr.deviceFault) {
            exts[extCount++] = VK_EXT_DEVICE_FAULT_EXTENSION_NAME;
        }
        if (g_vkr.robust2) {
            exts[extCount++] = VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
        }
        if (g_vkr.addressReport) {
            exts[extCount++] = VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME;
        }
        if (g_vkr.checkpoints) {
            exts[extCount++] = VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME;
        }
        /* what is on, and whether gpu_robust named it or it is the
           platform's default */
        char robustSay[160];
        if (!g_vkr.robust) {
            snprintf(robustSay, sizeof robustSay, "%s",
                     wantRobust        ? "asked for, not offered"
                     : robustSaid == 0 ? "off (gpu_robust)"
                                       : "off");
        } else {
            snprintf(robustSay, sizeof robustSay, "on, buffers%s%s%s (%s)",
                     buffer2 ? " (robustness2)" : "", image2 ? " and images (robustness2)" : "",
                     nullDesc ? ", null descriptors" : "",
                     robustSaid == 1 ? "gpu_robust" : "the Android default");
        }
        VKR_LOG("loss diagnostics: fault report %s; checkpoints %s; resource addresses %s; robust "
                "access %s",
                g_vkr.deviceFault ? "on" : "not offered",
                g_vkr.checkpoints ? "on (gpu_checkpoints)"
                : wantCheck       ? "asked for, not offered"
                                  : "off",
                g_vkr.addressReport ? "on (gpu_checkpoints)"
                : wantCheck         ? "asked for, not offered"
                                    : "off",
                robustSay);
    }

    VkPhysicalDeviceDynamicRenderingFeatures dyn = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .dynamicRendering = VK_TRUE,
    };
    if (g_vkr.deviceFault) {
        qFault.pNext = dyn.pNext;
        dyn.pNext = &qFault;
    }
    if (g_vkr.robust2) {
        qRobust2.pNext = dyn.pNext;
        dyn.pNext = &qRobust2;
    }
    if (g_vkr.addressReport) {
        qBind.pNext = dyn.pNext;
        dyn.pNext = &qBind;
    }
    if (g_vkr.presentWait) {
        qPw.pNext = dyn.pNext;
        qPid.pNext = &qPw;
        dyn.pNext = &qPid;
    }
    if (g_vkr.fifoLatestReady) {
        qLatest.pNext = dyn.pNext;
        dyn.pNext = &qLatest;
    }
    if (g_vkr.interlock) {
        qInterlock.pNext = dyn.pNext;
        dyn.pNext = &qInterlock;
    }
    if (g_vkr.rasterOrder) {
        qOrder.pNext = dyn.pNext;
        dyn.pNext = &qOrder;
    }
    if (g_vkr.localRead) {
        qLocal.pNext = dyn.pNext;
        dyn.pNext = &qLocal;
    }
    VkPhysicalDeviceVulkan12Features f12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &dyn,
        .timelineSemaphore = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 f2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &f12,
        .features =
            {
                .dualSrcBlend = g_vkr.dualSrcBlend ? VK_TRUE : VK_FALSE,
                .samplerAnisotropy = g_vkr.anisotropy ? VK_TRUE : VK_FALSE,
                .textureCompressionBC = g_vkr.bc ? VK_TRUE : VK_FALSE,
                .sampleRateShading = g_vkr.sampleRate ? VK_TRUE : VK_FALSE,
                /* a fragment shader storing to an image (the interlock's
                 * destination read) */
                .fragmentStoresAndAtomics = g_vkr.interlock ? VK_TRUE : VK_FALSE,
                /* gpu_robust (on by default on Android): out-of-range
                 * buffer reads give zero and writes are dropped instead of
                 * reaching other memory */
                .robustBufferAccess = g_vkr.robust ? VK_TRUE : VK_FALSE,
            },
    };
#ifdef ICO_RHI_MOLTENVK
    VkPhysicalDevicePortabilitySubsetFeaturesKHR subset = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR,
    };
    if (portability) {
        VkPhysicalDeviceFeatures2 query = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &subset,
        };
        vkGetPhysicalDeviceFeatures2(g_vkr.phys, &query);
        subset.pNext = &f12;
        f2.pNext = &subset;
    }
#endif
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = g_vkr.queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };
    VkDeviceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &f2,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
        .enabledExtensionCount = extCount,
        .ppEnabledExtensionNames = exts,
    };
    if (!VKR_CHECK(vkCreateDevice(g_vkr.phys, &ci, NULL, &g_vkr.device))) {
        return false;
    }
#ifndef ICO_RHI_MOLTENVK
    volkLoadDevice(g_vkr.device);
#endif
    vkGetDeviceQueue(g_vkr.device, g_vkr.queueFamily, 0, &g_vkr.queue);
    if (g_vkr.presentWait) {
        g_vkr.waitForPresent =
            (PFN_vkWaitForPresentKHR)vkGetDeviceProcAddr(g_vkr.device, "vkWaitForPresentKHR");
        if (!g_vkr.waitForPresent) {
            VKR_LOG("display timing (present wait): off, vkWaitForPresentKHR not found");
            g_vkr.presentWait = false;
        }
    }
    if (g_vkr.presentWaitFake) {
        g_vkr.presentWait = true; /* the extensions stay off (vkr_present_wait_fake) */
    }
    /* the loss diagnostics' entry points (through the device, so a build
     * linked straight to its driver needs none of them) */
    if (g_vkr.deviceFault) {
        g_vkr.getFaultInfo = (PFN_vkGetDeviceFaultInfoEXT)vkGetDeviceProcAddr(
            g_vkr.device, "vkGetDeviceFaultInfoEXT");
    }
    if (g_vkr.checkpoints) {
        g_vkr.cmdSetCheckpoint =
            (PFN_vkCmdSetCheckpointNV)vkGetDeviceProcAddr(g_vkr.device, "vkCmdSetCheckpointNV");
        g_vkr.getCheckpointData = (PFN_vkGetQueueCheckpointDataNV)vkGetDeviceProcAddr(
            g_vkr.device, "vkGetQueueCheckpointDataNV");
        g_vkr.checkpoints = g_vkr.cmdSetCheckpoint && g_vkr.getCheckpointData;
    }
    if (g_vkr.addressReport) {
        VkDebugUtilsMessengerCreateInfoEXT mci = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT,
            .pfnUserCallback = vkr_bind_callback,
        };
        if (!VKR_CHECK(
                vkCreateDebugUtilsMessengerEXT(g_vkr.instance, &mci, NULL, &g_vkr.bindMessenger))) {
            g_vkr.bindMessenger = VK_NULL_HANDLE;
        }
    }
    {
        /* GPU timestamps when the queue writes them */
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g_vkr.phys, &qn, NULL);
        VkQueueFamilyProperties *qp = calloc(qn ? qn : 1, sizeof(*qp));
        uint32_t bits = 0;
        if (qp) {
            vkGetPhysicalDeviceQueueFamilyProperties(g_vkr.phys, &qn, qp);
            if (g_vkr.queueFamily < qn) {
                bits = qp[g_vkr.queueFamily].timestampValidBits;
            }
            free(qp);
        }
        g_vkr.timestamps = bits > 0 && g_vkr.props.limits.timestampPeriod > 0.0f;
        g_vkr.timestampPeriod = g_vkr.props.limits.timestampPeriod;
        g_vkr.timestampMask = bits >= 64 ? ~0ull : ((1ull << bits) - 1ull);
    }
    if (core13) {
        g_vkr.cmdBeginRendering = vkCmdBeginRendering;
        g_vkr.cmdEndRendering = vkCmdEndRendering;
    } else {
        g_vkr.cmdBeginRendering = vkCmdBeginRenderingKHR;
        g_vkr.cmdEndRendering = vkCmdEndRenderingKHR;
    }

    VkSemaphoreTypeCreateInfo tci = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0,
    };
    VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &tci};
    if (!VKR_CHECK(vkCreateSemaphore(g_vkr.device, &sci, NULL, &g_vkr.timeline))) {
        return false;
    }
    g_vkr.timelineValue = 0;

    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = g_vkr.queueFamily,
    };
    return VKR_CHECK(vkCreateCommandPool(g_vkr.device, &pci, NULL, &g_vkr.oneShotPool));
}

/* The PCI vendor ids of the tile-based GPUs: Qualcomm Adreno (and Mesa's
 * Turnip, which reports Qualcomm's id), ARM Mali, Imagination PowerVR,
 * Samsung Xclipse, Apple (MoltenVK) and Broadcom VideoCore */
static bool vkr_is_tiler(uint32_t vendor)
{
    switch (vendor) {
    case 0x5143: /* Qualcomm */
    case 0x13B5: /* ARM */
    case 0x1010: /* Imagination */
    case 0x144D: /* Samsung */
    case 0x106B: /* Apple */
    case 0x14E4: /* Broadcom */
        return true;
    default:
        return false;
    }
}

static void vkr_fill_limits(void)
{
    const VkPhysicalDeviceLimits *l = &g_vkr.props.limits;
    RhiLimits *o = &g_vkr.limits;
    uint32_t align = (uint32_t)l->minUniformBufferOffsetAlignment;
    if ((uint32_t)l->minStorageBufferOffsetAlignment > align) {
        align = (uint32_t)l->minStorageBufferOffsetAlignment;
    }
    o->uniformAlign = align < 16u ? 16u : align;
    o->maxTextureSize = l->maxImageDimension2D;
    {
        uint32_t rt = l->maxImageDimension2D;
        const uint32_t lims[4] = {l->maxFramebufferWidth, l->maxFramebufferHeight,
                                  l->maxViewportDimensions[0], l->maxViewportDimensions[1]};
        for (int i = 0; i < 4; i++) {
            rt = lims[i] < rt ? lims[i] : rt;
        }
        o->maxRenderTargetSize = rt;
    }
    o->dualSourceBlend = g_vkr.dualSrcBlend; /* optional */
    o->stencilWrap = true;                   /* core Vulkan */
    /* a D24 depth copies out as packed 24-bit words, not floats */
    o->depthReadback = g_vkr.dsFormat == VK_FORMAT_D32_SFLOAT_S8_UINT;
    o->depthStencilFormatName = g_vkr.dsFormat == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32S8";
    o->depthSampled = g_vkr.dsSampled;
    o->tiler = vkr_is_tiler(g_vkr.props.vendorID);
#ifdef __ANDROID__
    /* every Android GPU counts as tile-based, whatever its vendor id: the
     * list above names the known ones, and a phone GPU missing from it
     * would otherwise take the desktop defaults */
    o->tiler = true;
#endif
    o->copyRowPitchAlign = 1; /* bufferRowLength is in texels; the pitch is width * texel size */
    o->copyOffsetAlign = 4;   /* bufferOffset: texel size, and 4 for depth/stencil */
    /* images carry mipLevels, views and barriers span every level, and
     * vkCmdCopyBufferToImage takes the level (vk_resource.c, vk_cmd.c) */
    o->textureMips = true;
    o->maxAnisotropy = g_vkr.anisotropy ? l->maxSamplerAnisotropy : 1.0f;
    /* dynamic uniform buffers (rhi_cmd_set_bind_group_offsets) */
    o->maxDynamicUniforms = l->maxDescriptorSetUniformBuffersDynamic;
    o->maxStorageRange = l->maxStorageBufferRange;
    /* indexed draws (VkPhysicalDeviceFeatures::fullDrawIndexUint32: the
     * limit is 2^32 - 1 when the device has the feature, at least 2^24 - 1
     * without it); logged on the device line */
    o->maxDrawIndexedIndexValue = l->maxDrawIndexedIndexValue;
    o->fullDrawIndexUint32 = g_vkr.fullDrawIndex;
    /* texture packs: the BC formats (vkr_create_device enabled the feature) */
    o->bcTextures = g_vkr.bc;
    /* multisampling (vkr_msaa_counts, at device creation) */
    o->msaaCounts = g_vkr.msaaCounts | 1u;
    o->sampleRateShading = g_vkr.sampleRate;
    /* the destination read (vkr_create_device) */
    o->fragmentInterlock = g_vkr.interlock;
    o->rov = false;
    o->rasterOrderAttachment = g_vkr.rasterOrder;
    /* ICO_VK_FAKE_LIMITS=min: the limits the renderer reads at the values
     * every Vulkan device is required to meet (the specification's Required
     * Limits table), where phone GPUs sit while desktop ones and lavapipe are
     * far past them: offsets aligned to 256 (minUniformBufferOffsetAlignment
     * and minStorageBufferOffsetAlignment are at most 256), storage ranges of
     * 2^27 bytes, 4096-texel images, 8 dynamic uniform buffers, 16x
     * anisotropy.  Each is only ever lowered, so the device stays valid for
     * what the renderer then does (tests: rd_pixel_minlimits,
     * rd_present_minlimits).  The descriptor layouts themselves are fixed
     * and inside the minimums (rd_replay.c: three sets, at most four uniform
     * buffers and one storage buffer a stage, two sampled images and a
     * sampler, four dynamic uniform buffers, no push constants). */
    if (g_vkr.fakeMinLimits) { /* read in vkr_fake_limits */
        o->uniformAlign = o->uniformAlign > 256u ? o->uniformAlign : 256u;
        o->maxTextureSize = o->maxTextureSize < 4096u ? o->maxTextureSize : 4096u;
        o->maxRenderTargetSize = o->maxRenderTargetSize < 4096u ? o->maxRenderTargetSize : 4096u;
        o->maxDynamicUniforms = o->maxDynamicUniforms < 8u ? o->maxDynamicUniforms : 8u;
        o->maxStorageRange =
            o->maxStorageRange < (1ull << 27) ? o->maxStorageRange : (uint64_t)1 << 27;
        o->maxAnisotropy = o->maxAnisotropy < 16.0f ? o->maxAnisotropy : 16.0f;
        VKR_LOG("ICO_VK_FAKE_LIMITS=min: offsets aligned to %u, storage ranges up to %llu bytes, "
                "images up to %u texels, %u dynamic uniform buffers",
                o->uniformAlign, (unsigned long long)o->maxStorageRange, o->maxTextureSize,
                o->maxDynamicUniforms);
    }
}

/* ------------------------------------------------------------- lifecycle */
/* rhi_set_vulkan_loader's function; outside g_vkr, which rhi_init clears */
static PFN_vkGetInstanceProcAddr s_loaderGipa;

void rhi_set_vulkan_loader(void *getInstanceProcAddr)
{
    s_loaderGipa = (PFN_vkGetInstanceProcAddr)getInstanceProcAddr;
}

bool rhi_init(const RhiDeviceDesc *desc)
{
    if (g_vkr.initialised) {
        return true;
    }
    memset(&g_vkr, 0, sizeof(g_vkr));
    VkResult vr;
    /* a driver the program loaded (rhi_set_vulkan_loader) first */
    PFN_vkGetInstanceProcAddr gipa = s_loaderGipa;
#ifdef ICO_RHI_HAVE_SDL
    if (!gipa && desc->sdlWindow) {
        /* SDL loaded the Vulkan loader for the window (SDL_WINDOW_VULKAN);
         * share it so both see the same instance functions */
        gipa = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
        if (!gipa) {
            VKR_LOG("SDL_Vulkan_GetVkGetInstanceProcAddr: %s", SDL_GetError());
            return false;
        }
    }
#endif
#ifdef ICO_RHI_MOLTENVK
    /* SDL's surface and the backend must use the same statically linked driver. */
    if (gipa && gipa != vkGetInstanceProcAddr) {
        VKR_LOG("the Vulkan loader differs from the linked MoltenVK driver");
        return false;
    }
    vr = VK_SUCCESS;
#else
    if (gipa) {
        volkInitializeCustom(gipa);
        vr = VK_SUCCESS;
    } else {
        vr = volkInitialize();
    }
#endif
    if (vr != VK_SUCCESS) {
        VKR_LOG("no Vulkan loader (libvulkan / vulkan-1.dll) found");
        return false;
    }
    vkr_scan_injectors(); /* rhi_injector_name */
    if (!vkr_create_instance(desc)) {
        rhi_shutdown();
        return false;
    }
    if (desc->sdlWindow) {
#ifdef ICO_RHI_HAVE_SDL
        g_vkr.window = desc->sdlWindow;
#ifdef __ANDROID__
        /* SDL's surface call goes through its own loader, which is
           not the one a driver the program loaded answers to */
        if (!vkr_create_window_surface(desc->sdlWindow, &g_vkr.surface)) {
            VKR_LOG("no surface on the window (no native window yet, or the call failed)");
            rhi_shutdown();
            return false;
        }
#else
        if (!SDL_Vulkan_CreateSurface((SDL_Window *)desc->sdlWindow, g_vkr.instance, NULL,
                                      &g_vkr.surface)) {
            VKR_LOG("SDL_Vulkan_CreateSurface: %s", SDL_GetError());
            rhi_shutdown();
            return false;
        }
#endif
#endif
    }
    if (!vkr_pick_device() || !vkr_create_device()) {
        rhi_shutdown();
        return false;
    }
    vkr_fill_limits();
    vkr_pipeline_cache_init();
    if (!vkr_pool_init(&g_vkr.buffers, "buffer", 4096, sizeof(VkrBuffer)) ||
        !vkr_pool_init(&g_vkr.textures, "texture", 8192, sizeof(VkrTexture)) ||
        !vkr_pool_init(&g_vkr.samplers, "sampler", 256, sizeof(VkSampler)) ||
        !vkr_pool_init(&g_vkr.shaders, "shader", 512, sizeof(VkrShader)) ||
        !vkr_pool_init(&g_vkr.layouts, "bind group layout", 64, sizeof(VkrLayout)) ||
        !vkr_pool_init(&g_vkr.pipelines, "pipeline", 1024, sizeof(VkrPipeline))) {
        rhi_shutdown();
        return false;
    }
    if (!vkr_frames_init()) {
        rhi_shutdown();
        return false;
    }
    g_vkr.initialised = true;
    if (g_vkr.surface) {
        int w = 0, h = 0;
#ifdef ICO_RHI_HAVE_SDL
        SDL_GetWindowSizeInPixels((SDL_Window *)g_vkr.window, &w, &h);
#endif
        if (!vkr_swapchain_create((uint32_t)w, (uint32_t)h, desc->vsync)) {
            rhi_shutdown();
            return false;
        }
    }
    VKR_LOG("%s (Vulkan %u.%u.%u%s)%s; index values up to %u (full 32-bit range %s)",
            g_vkr.adapterName, VK_API_VERSION_MAJOR(g_vkr.apiVersion),
            VK_API_VERSION_MINOR(g_vkr.apiVersion), VK_API_VERSION_PATCH(g_vkr.apiVersion),
            s_driverText, g_vkr.surface ? "" : ", headless", g_vkr.limits.maxDrawIndexedIndexValue,
            g_vkr.limits.fullDrawIndexUint32 ? "offered" : "not offered");
    return true;
}

void rhi_shutdown(void)
{
    if (g_vkr.device) {
        vkDeviceWaitIdle(g_vkr.device);
        vkr_present_wait_shutdown(); /* the thread waiting on presents */
        vkr_swapchain_destroy();
        vkr_frames_shutdown();
        vkr_release_all_objects();
        if (g_vkr.oneShotPool) {
            vkDestroyCommandPool(g_vkr.device, g_vkr.oneShotPool, NULL);
        }
        if (g_vkr.timeline) {
            vkDestroySemaphore(g_vkr.device, g_vkr.timeline, NULL);
        }
        vkr_pipeline_cache_shutdown();
        vkDestroyDevice(g_vkr.device, NULL);
    }
    vkr_pool_free(&g_vkr.buffers);
    vkr_pool_free(&g_vkr.textures);
    vkr_pool_free(&g_vkr.samplers);
    vkr_pool_free(&g_vkr.shaders);
    vkr_pool_free(&g_vkr.layouts);
    vkr_pool_free(&g_vkr.pipelines);
    if (g_vkr.instance) {
        if (g_vkr.surface) {
            vkDestroySurfaceKHR(g_vkr.instance, g_vkr.surface, NULL);
        }
        if (g_vkr.messenger) {
            vkDestroyDebugUtilsMessengerEXT(g_vkr.instance, g_vkr.messenger, NULL);
        }
        if (g_vkr.bindMessenger) {
            vkDestroyDebugUtilsMessengerEXT(g_vkr.instance, g_vkr.bindMessenger, NULL);
        }
        vkDestroyInstance(g_vkr.instance, NULL);
    }
    vkr_alloc_table_free();
    uint32_t errors = g_vkr.validationErrors;
    memset(&g_vkr, 0, sizeof(g_vkr));
    g_vkr.validationErrors = errors; /* readable after shutdown, for tests */
}

RhiBackendKind rhi_backend(void)
{
    return RHI_BACKEND_VULKAN;
}

const RhiLimits *rhi_limits(void)
{
    return &g_vkr.limits;
}

const char *rhi_adapter_name(void)
{
    return g_vkr.adapterName;
}

bool rhi_device_lost(void)
{
    return g_vkr.deviceLost;
}

void rhi_get_stats(RhiStats *out)
{
    if (out) {
        *out = g_vkr.stats;
        out->memoryLive = g_vkr.memLive;
        out->memoryPeak = g_vkr.memPeak;
        out->memoryLiveBytes = g_vkr.memLiveBytes;
        out->memoryPeakBytes = g_vkr.memPeakBytes;
        out->memoryLimit = g_vkr.props.limits.maxMemoryAllocationCount;
    }
}

bool rhi_timestamps_supported(void)
{
    return g_vkr.timestamps;
}

/* The rhi_create_backend entry (port/rhi/rhi_backend.h). */
RHI_BACKEND_DEFINE(rhi_backend_vk, "vulkan");
