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

uint64_t vkr_NowNs(void)
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

bool vkr_Check(VkResult r, const char *what, const char *file, int line)
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
bool vkr_PoolInit(VkrPool *p, const char *name, uint32_t cap, uint32_t elemSize)
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

void vkr_PoolFree(VkrPool *p)
{
    free(p->data);
    free(p->gen);
    free(p->live);
    free(p->freeList);
    memset(p, 0, sizeof(*p));
}

uint32_t vkr_PoolAlloc(VkrPool *p, void **out)
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

void *vkr_PoolGet(const VkrPool *p, uint32_t id)
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

void vkr_PoolRelease(VkrPool *p, uint32_t id)
{
    if (!vkr_PoolGet(p, id)) {
        return;
    }
    uint32_t idx = (id & VKR_INDEX_MASK) - 1u;
    p->live[idx] = 0;
    p->gen[idx] = (uint16_t)((p->gen[idx] + 1u) & VKR_GEN_MASK);
    p->freeList[p->freeCount++] = idx;
}

/* ------------------------------------------------------- debug messenger */
static VKAPI_ATTR VkBool32 VKAPI_CALL vkr_DebugCallback(
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

uint32_t rhi_vk_ValidationErrorCount(void)
{
    return g_vkr.validationErrors;
}

/* ---------------------------------------------------------------- instance */
static bool vkr_HasLayer(const char *name)
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

static bool vkr_HasInstanceExt(const char *name)
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

static bool vkr_CreateInstance(const RhiDeviceDesc *desc)
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
        if (vkr_HasLayer("VK_LAYER_KHRONOS_validation")) {
            layers[layerCount++] = "VK_LAYER_KHRONOS_validation";
        } else {
            VKR_LOG("debugLayers: VK_LAYER_KHRONOS_validation not installed; continuing without");
        }
        if (vkr_HasInstanceExt(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
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
            .pfnUserCallback = vkr_DebugCallback,
        };
        VKR_CHECK(vkCreateDebugUtilsMessengerEXT(g_vkr.instance, &mci, NULL, &g_vkr.messenger));
    }
    return true;
}

/* ------------------------------------------------------- device selection */
static bool vkr_DeviceHasExt(VkPhysicalDevice pd, const char *name)
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

static bool vkr_FormatOk(VkPhysicalDevice pd, VkFormat f, VkFormatFeatureFlags need)
{
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(pd, f, &fp);
    return (fp.optimalTilingFeatures & need) == need;
}

/* Returns a score (higher is better) or -1 when the device cannot run the
 * renderer.  *outQueue receives the graphics(+present) queue family. */
static int vkr_RateDevice(VkPhysicalDevice pd, uint32_t *outQueue, const char **why)
{
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(pd, &p);
    if (p.apiVersion < VK_API_VERSION_1_2) {
        *why = "Vulkan 1.2 not supported";
        return -1;
    }
    bool core13 = p.apiVersion >= VK_API_VERSION_1_3;
    if (!core13 && !vkr_DeviceHasExt(pd, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        *why = "no dynamic rendering";
        return -1;
    }
    if (g_vkr.surface && !vkr_DeviceHasExt(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        *why = "no swapchain extension";
        return -1;
    }
    /* package AN-E: dualSrcBlend is optional (rd's two-pass blend fallback
     * covers its absence, RhiLimits.dualSourceBlend); a device with it is
     * preferred over an otherwise equal one */
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
    if (!vkr_FormatOk(pd, VK_FORMAT_R8G8B8A8_UNORM,
                      rt | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) ||
        !vkr_FormatOk(pd, VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
        !vkr_FormatOk(pd, VK_FORMAT_R8_UNORM, rt) || !vkr_FormatOk(pd, VK_FORMAT_D32_SFLOAT, ds) ||
        !vkr_FormatOk(pd, VK_FORMAT_D32_SFLOAT_S8_UINT, ds)) {
        *why = "a required format (RGBA8, RGBA8_UINT, R8, D32F, D32F_S8) is missing";
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

static bool vkr_PickDevice(void)
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
        int score = vkr_RateDevice(pds[i], &q, &why);
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
    vkGetPhysicalDeviceMemoryProperties(g_vkr.phys, &g_vkr.memProps);
    snprintf(g_vkr.adapterName, sizeof(g_vkr.adapterName), "%s", g_vkr.props.deviceName);
    return true;
}

static bool vkr_CreateDevice(void)
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
    /* package AN-E: ICO_VK_FAKE_NO_DUAL=1 leaves the feature off, as on a
     * device without it (tests: rhi_vk_nodual) */
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
        vkGetPhysicalDeviceFormatProperties(g_vkr.phys, vkr_formatMap[f].vk, &fp);
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
        /* package P1: GPU timestamps when the queue writes them */
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

static void vkr_FillLimits(void)
{
    const VkPhysicalDeviceLimits *l = &g_vkr.props.limits;
    RhiLimits *o = &g_vkr.limits;
    uint32_t align = (uint32_t)l->minUniformBufferOffsetAlignment;
    if ((uint32_t)l->minStorageBufferOffsetAlignment > align) {
        align = (uint32_t)l->minStorageBufferOffsetAlignment;
    }
    o->uniformAlign = align < 16u ? 16u : align;
    o->maxTextureSize = l->maxImageDimension2D;
    o->dualSourceBlend = g_vkr.dualSrcBlend; /* package AN-E: optional */
    o->stencilWrap = true;                   /* core Vulkan */
    o->depthReadback = true;
    o->copyRowPitchAlign = 1; /* bufferRowLength is in texels; the pitch is width * texel size */
    o->copyOffsetAlign = 4;   /* bufferOffset: texel size, and 4 for depth/stencil */
    /* R7a: images carry mipLevels, views and barriers span every level, and
     * vkCmdCopyBufferToImage takes the level (vk_resource.c, vk_cmd.c) */
    o->textureMips = true;
    o->maxAnisotropy = g_vkr.anisotropy ? l->maxSamplerAnisotropy : 1.0f;
    /* package PA */
    o->maxDynamicUniforms = l->maxDescriptorSetUniformBuffersDynamic;
    o->maxStorageRange = l->maxStorageBufferRange;
    /* texture packs: the BC formats (vkr_CreateDevice enabled the feature) */
    o->bcTextures = g_vkr.bc;
}

/* ------------------------------------------------------------- lifecycle */
bool rhi_Init(const RhiDeviceDesc *desc)
{
    if (g_vkr.initialised) {
        return true;
    }
    memset(&g_vkr, 0, sizeof(g_vkr));
    VkResult vr;
#ifdef ICO_RHI_HAVE_SDL
    if (desc->sdlWindow) {
        /* SDL loaded the Vulkan loader for the window (SDL_WINDOW_VULKAN);
         * share it so both see the same instance functions */
        PFN_vkGetInstanceProcAddr gipa =
            (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
        if (!gipa) {
            VKR_LOG("SDL_Vulkan_GetVkGetInstanceProcAddr: %s", SDL_GetError());
            return false;
        }
        volkInitializeCustom(gipa);
        vr = VK_SUCCESS;
    } else
#endif
    {
        vr = volkInitialize();
    }
    if (vr != VK_SUCCESS) {
        VKR_LOG("no Vulkan loader (libvulkan / vulkan-1.dll) found");
        return false;
    }
    if (!vkr_CreateInstance(desc)) {
        rhi_Shutdown();
        return false;
    }
    if (desc->sdlWindow) {
#ifdef ICO_RHI_HAVE_SDL
        g_vkr.window = desc->sdlWindow;
        if (!SDL_Vulkan_CreateSurface((SDL_Window *)desc->sdlWindow, g_vkr.instance, NULL,
                                      &g_vkr.surface)) {
            VKR_LOG("SDL_Vulkan_CreateSurface: %s", SDL_GetError());
            rhi_Shutdown();
            return false;
        }
#endif
    }
    if (!vkr_PickDevice() || !vkr_CreateDevice()) {
        rhi_Shutdown();
        return false;
    }
    vkr_FillLimits();
    vkr_PipelineCacheInit();
    if (!vkr_PoolInit(&g_vkr.buffers, "buffer", 4096, sizeof(VkrBuffer)) ||
        !vkr_PoolInit(&g_vkr.textures, "texture", 8192, sizeof(VkrTexture)) ||
        !vkr_PoolInit(&g_vkr.samplers, "sampler", 256, sizeof(VkSampler)) ||
        !vkr_PoolInit(&g_vkr.shaders, "shader", 512, sizeof(VkrShader)) ||
        !vkr_PoolInit(&g_vkr.layouts, "bind group layout", 64, sizeof(VkrLayout)) ||
        !vkr_PoolInit(&g_vkr.pipelines, "pipeline", 1024, sizeof(VkrPipeline))) {
        rhi_Shutdown();
        return false;
    }
    if (!vkr_FramesInit()) {
        rhi_Shutdown();
        return false;
    }
    g_vkr.initialised = true;
    if (g_vkr.surface) {
        int w = 0, h = 0;
#ifdef ICO_RHI_HAVE_SDL
        SDL_GetWindowSizeInPixels((SDL_Window *)g_vkr.window, &w, &h);
#endif
        if (!vkr_SwapchainCreate((uint32_t)w, (uint32_t)h, desc->vsync)) {
            rhi_Shutdown();
            return false;
        }
    }
    VKR_LOG("%s (Vulkan %u.%u.%u)%s", g_vkr.adapterName, VK_API_VERSION_MAJOR(g_vkr.apiVersion),
            VK_API_VERSION_MINOR(g_vkr.apiVersion), VK_API_VERSION_PATCH(g_vkr.apiVersion),
            g_vkr.surface ? "" : ", headless");
    return true;
}

void rhi_Shutdown(void)
{
    if (g_vkr.device) {
        vkDeviceWaitIdle(g_vkr.device);
        vkr_SwapchainDestroy();
        vkr_FramesShutdown();
        vkr_ReleaseAllObjects();
        if (g_vkr.oneShotPool) {
            vkDestroyCommandPool(g_vkr.device, g_vkr.oneShotPool, NULL);
        }
        if (g_vkr.timeline) {
            vkDestroySemaphore(g_vkr.device, g_vkr.timeline, NULL);
        }
        vkr_PipelineCacheShutdown();
        vkDestroyDevice(g_vkr.device, NULL);
    }
    vkr_PoolFree(&g_vkr.buffers);
    vkr_PoolFree(&g_vkr.textures);
    vkr_PoolFree(&g_vkr.samplers);
    vkr_PoolFree(&g_vkr.shaders);
    vkr_PoolFree(&g_vkr.layouts);
    vkr_PoolFree(&g_vkr.pipelines);
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

RhiBackendKind rhi_Backend(void)
{
    return RHI_BACKEND_VULKAN;
}

const RhiLimits *rhi_Limits(void)
{
    return &g_vkr.limits;
}

const char *rhi_AdapterName(void)
{
    return g_vkr.adapterName;
}

bool rhi_DeviceLost(void)
{
    return g_vkr.deviceLost;
}

void rhi_GetStats(RhiStats *out)
{
    if (out) {
        *out = g_vkr.stats;
    }
}

bool rhi_TimestampsSupported(void)
{
    return g_vkr.timestamps;
}

/* The rhi_CreateBackend entry (port/rhi/rhi_backend.h). */
RHI_BACKEND_DEFINE(rhi_backend_vk, "vulkan");
