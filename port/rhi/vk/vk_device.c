/* vk_device.c: instance, physical device selection, logical device, limits,
 * validation layers and the handle pools of the Vulkan backend. */
#include "vk_internal.h"
#include "rhi_vk.h"
#include "../rhi_backend.h"
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
        fprintf(stderr, "rhi_vk: device lost\n");
    }
    return false;
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

static bool vkr_create_instance(const RhiDeviceDesc *desc)
{
    const char *exts[16];
    uint32_t extCount = 0;
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
        } else {
            VKR_LOG("debugLayers: VK_LAYER_KHRONOS_validation not installed; continuing without");
        }
        if (vkr_has_instance_ext(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            exts[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            g_vkr.debugUtils = true;
        }
    }

    uint32_t loaderVersion = volkGetInstanceVersion();
    if (loaderVersion < VK_API_VERSION_1_2) {
        VKR_LOG("the Vulkan loader is %u.%u; 1.2 or later is required",
                VK_API_VERSION_MAJOR(loaderVersion), VK_API_VERSION_MINOR(loaderVersion));
        return false;
    }
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = desc->appName ? desc->appName : "ico",
        .applicationVersion = 1,
        .pEngineName = "ico rhi",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
        .enabledLayerCount = layerCount,
        .ppEnabledLayerNames = layers,
        .enabledExtensionCount = extCount,
        .ppEnabledExtensionNames = exts,
    };
    if (!VKR_CHECK(vkCreateInstance(&ci, NULL, &g_vkr.instance))) {
        return false;
    }
    volkLoadInstanceOnly(g_vkr.instance);

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
#undef VKR_FAKE_MAX
#undef VKR_FAKE_MIN
    g_vkr.fakeLimits = "mali";
    VKR_LOG("ICO_VK_FAKE_LIMITS=mali: the device's limits are clamped to a Mali-G68's");
}

/* The format behind RHI_FMT_D32F_S8.  The spec guarantees one of
 * D32_SFLOAT_S8_UINT and D24_UNORM_S8_UINT as an attachment; D32 is
 * preferred.  Either is taken only with the sampled and transfer uses too:
 * the fog and the effects depth sample the scene's depth or a copy of it
 * (rd_core.c rd__sampled_depth; on D24S8 always the copy), and the tests' depth
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
    bool core13 = p.apiVersion >= VK_API_VERSION_1_3;
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
    vkr_fake_limits();
    vkGetPhysicalDeviceMemoryProperties(g_vkr.phys, &g_vkr.memProps);
    snprintf(g_vkr.adapterName, sizeof(g_vkr.adapterName), "%s", g_vkr.props.deviceName);
    bool fake = false;
    g_vkr.dsFormat = vkr_choose_depth_stencil(g_vkr.phys, &fake);
    if (g_vkr.dsFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
        VKR_LOG("depth-stencil: D24S8 in use%s, 24-bit depth (the scene depth has less precision "
                "than the 32-bit float of D32S8)",
                fake ? " (ICO_VK_FAKE_D24S8)" : "");
    }
    return true;
}

static bool vkr_create_device(void)
{
    bool core13 = g_vkr.props.apiVersion >= VK_API_VERSION_1_3;
    g_vkr.apiVersion = g_vkr.props.apiVersion;
    const char *exts[4];
    uint32_t extCount = 0;
    if (g_vkr.surface) {
        exts[extCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }
    if (!core13) {
        exts[extCount++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
    }

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
    /* texture packs: BC1/2/3/7 come with textureCompressionBC (every
     * desktop GPU, the Deck, lavapipe); the formats' sampled and copy
     * features are checked too, so bcTextures promises what it says */
    g_vkr.bc = avail.textureCompressionBC == VK_TRUE;
    for (int f = RHI_FMT_BC1_UNORM; g_vkr.bc && f <= RHI_FMT_BC7_UNORM; f++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(g_vkr.phys, vkr_vk_format((RhiFormat)f), &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                          VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        g_vkr.bc = (fp.optimalTilingFeatures & need) == need;
    }

    VkPhysicalDeviceDynamicRenderingFeatures dyn = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .dynamicRendering = VK_TRUE,
    };
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
            },
    };
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
    volkLoadDevice(g_vkr.device);
    vkGetDeviceQueue(g_vkr.device, g_vkr.queueFamily, 0, &g_vkr.queue);
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
    o->tiler = vkr_is_tiler(g_vkr.props.vendorID);
    o->copyRowPitchAlign = 1; /* bufferRowLength is in texels; the pitch is width * texel size */
    o->copyOffsetAlign = 4;   /* bufferOffset: texel size, and 4 for depth/stencil */
    /* images carry mipLevels, views and barriers span every level, and
     * vkCmdCopyBufferToImage takes the level (vk_resource.c, vk_cmd.c) */
    o->textureMips = true;
    o->maxAnisotropy = g_vkr.anisotropy ? l->maxSamplerAnisotropy : 1.0f;
    /* dynamic uniform buffers (rhi_cmd_set_bind_group_offsets) */
    o->maxDynamicUniforms = l->maxDescriptorSetUniformBuffersDynamic;
    o->maxStorageRange = l->maxStorageBufferRange;
    /* texture packs: the BC formats (vkr_create_device enabled the feature) */
    o->bcTextures = g_vkr.bc;
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
    if (gipa) {
        volkInitializeCustom(gipa);
        vr = VK_SUCCESS;
    } else {
        vr = volkInitialize();
    }
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
    VKR_LOG("%s (Vulkan %u.%u.%u)%s", g_vkr.adapterName, VK_API_VERSION_MAJOR(g_vkr.apiVersion),
            VK_API_VERSION_MINOR(g_vkr.apiVersion), VK_API_VERSION_PATCH(g_vkr.apiVersion),
            g_vkr.surface ? "" : ", headless");
    return true;
}

void rhi_shutdown(void)
{
    if (g_vkr.device) {
        vkDeviceWaitIdle(g_vkr.device);
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
        vkDestroyInstance(g_vkr.instance, NULL);
    }
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
