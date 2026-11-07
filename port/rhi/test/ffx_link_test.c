/* ffx_link_test.c: the FidelityFX FSR 3.1 upscaler (ico_ffx_upscaler,
 * port/rhi/ffx) linked into a C program and run on a real Vulkan device.
 *
 * Raw Vulkan through volk, not the port's RHI: an instance and a device on
 * the first physical device with Vulkan 1.2 (lavapipe in the container),
 * with the features the FFX Vulkan backend reads from the device enabled
 * where the device has them (16-bit floats, subgroup size control,
 * synchronization2, non-uniform storage buffer indexing, dedicated
 * allocation with get_memory_requirements2, AMD's buffer marker and
 * coherent memory): the backend acts on what the device lists, not on what
 * was enabled, so a listed extension left off is used anyway (a NULL entry
 * point). Then, through ffx-api: the
 * render size the Quality mode gives at 1280x960 (853x640, the 1.5 ratio),
 * the backend and an upscale context created for a 640x480 render and a
 * 1280x960 output (the context takes the two sizes, not a mode), its
 * provider (FSR 3.1) and GPU memory queried, the context destroyed, the
 * device and instance destroyed. Exit 0 on success, 1 on a failure, 77
 * (ctest skip) with the reason when there is no loader, no Vulkan 1.2 device
 * or no device can be made.
 *
 * Validation: the Khronos layer is enabled when the loader finds it (the
 * ctest names the fetched one and turns synchronisation validation on,
 * port/rhi/CMakeLists.txt); with VK_EXT_debug_utils every error message is
 * printed and counted, and any error fails the test. */
#include <volk.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>

#define SKIP 77

static unsigned s_validationErrors;
static unsigned s_ffxMessages;

static VKAPI_ATTR VkBool32 VKAPI_CALL onDebug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT types,
                                              const VkDebugUtilsMessengerCallbackDataEXT *data,
                                              void *user)
{
    (void)types;
    (void)user;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        s_validationErrors++;
        printf("ffx_link: validation error: %s\n", data && data->pMessage ? data->pMessage : "?");
    }
    return VK_FALSE;
}

static void onFfxMessage(uint32_t type, const wchar_t *message)
{
    s_ffxMessages++;
    printf("ffx_link: ffx %s: %ls\n", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning",
           message ? message : L"?");
}

static bool hasExtension(const VkExtensionProperties *list, uint32_t n, const char *name)
{
    for (uint32_t i = 0; i < n; i++) {
        if (strcmp(list[i].extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

int main(void)
{
    if (volkInitialize() != VK_SUCCESS) {
        printf("ffx_link: skip: no Vulkan loader\n");
        return SKIP;
    }
    if (volkGetInstanceVersion() < VK_API_VERSION_1_2) {
        printf("ffx_link: skip: the loader is older than Vulkan 1.2\n");
        return SKIP;
    }

    /* the instance, with debug utils when the loader has it */
    uint32_t nInstExt = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &nInstExt, NULL);
    VkExtensionProperties *instExt = calloc(nInstExt ? nInstExt : 1, sizeof *instExt);
    if (!instExt) {
        return 1;
    }
    vkEnumerateInstanceExtensionProperties(NULL, &nInstExt, instExt);
    bool debugUtils = hasExtension(instExt, nInstExt, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    free(instExt);
    const char *instNames[1] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "ffx_link_test",
                             .apiVersion = VK_API_VERSION_1_2};
    /* the Khronos layer when the loader finds it (the ctest's
     * VK_ADD_LAYER_PATH names the fetched one): turned on here, as the
     * port's Vulkan backend does for its tests */
    static const char *const layerName = "VK_LAYER_KHRONOS_validation";
    uint32_t nLayers = 0;
    vkEnumerateInstanceLayerProperties(&nLayers, NULL);
    VkLayerProperties *layers = calloc(nLayers ? nLayers : 1, sizeof *layers);
    if (!layers) {
        return 1;
    }
    vkEnumerateInstanceLayerProperties(&nLayers, layers);
    bool validation = false;
    for (uint32_t i = 0; i < nLayers; i++) {
        validation |= strcmp(layers[i].layerName, layerName) == 0;
    }
    free(layers);
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                .pApplicationInfo = &app,
                                .enabledLayerCount = validation ? 1u : 0u,
                                .ppEnabledLayerNames = &layerName,
                                .enabledExtensionCount = debugUtils ? 1u : 0u,
                                .ppEnabledExtensionNames = instNames};
    VkInstance instance = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&ici, NULL, &instance);
    if (vr != VK_SUCCESS) {
        printf("ffx_link: skip: vkCreateInstance failed (%d)\n", (int)vr);
        return SKIP;
    }
    volkLoadInstance(instance);
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if (debugUtils) {
        VkDebugUtilsMessengerCreateInfoEXT mci = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
            .pfnUserCallback = onDebug};
        vkCreateDebugUtilsMessengerEXT(instance, &mci, NULL, &messenger);
    }

    /* the first device with Vulkan 1.2 and a compute queue */
    uint32_t nPhys = 0;
    vkEnumeratePhysicalDevices(instance, &nPhys, NULL);
    VkPhysicalDevice phys[8];
    if (nPhys > 8) {
        nPhys = 8;
    }
    if (nPhys) {
        vkEnumeratePhysicalDevices(instance, &nPhys, phys);
    }
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props;
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < nPhys && pd == VK_NULL_HANDLE; i++) {
        vkGetPhysicalDeviceProperties(phys[i], &props);
        if (props.apiVersion < VK_API_VERSION_1_2) {
            continue;
        }
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(phys[i], &nq, NULL);
        VkQueueFamilyProperties q[16];
        if (nq > 16) {
            nq = 16;
        }
        vkGetPhysicalDeviceQueueFamilyProperties(phys[i], &nq, q);
        for (uint32_t f = 0; f < nq; f++) {
            if ((q[f].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
                pd = phys[i];
                family = f;
                break;
            }
        }
    }
    if (pd == VK_NULL_HANDLE) {
        printf(
            "ffx_link: skip: no Vulkan 1.2 device with a graphics and compute queue (%u devices)\n",
            nPhys);
        if (messenger) {
            vkDestroyDebugUtilsMessengerEXT(instance, messenger, NULL);
        }
        vkDestroyInstance(instance, NULL);
        return SKIP;
    }
    printf("ffx_link: device \"%s\" Vulkan %u.%u.%u\n", props.deviceName,
           VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
           VK_API_VERSION_PATCH(props.apiVersion));

    /* the extensions and features the backend reads, enabled where present */
    uint32_t nDevExt = 0;
    vkEnumerateDeviceExtensionProperties(pd, NULL, &nDevExt, NULL);
    VkExtensionProperties *devExt = calloc(nDevExt ? nDevExt : 1, sizeof *devExt);
    if (!devExt) {
        return 1;
    }
    vkEnumerateDeviceExtensionProperties(pd, NULL, &nDevExt, devExt);
    /* the backend reads the device's extension list, not what was enabled:
     * every extension it acts on must be enabled when listed (a listed
     * VK_KHR_dedicated_allocation makes it call
     * vkGetBufferMemoryRequirements2KHR, which a device without
     * VK_KHR_get_memory_requirements2 enabled returns as NULL) */
    const char *wanted[] = {
        VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,  VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
        VK_AMD_BUFFER_MARKER_EXTENSION_NAME,        VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME};

    enum { WANTED = sizeof wanted / sizeof wanted[0] };

    const char *enable[WANTED];
    uint32_t nEnable = 0;
    bool have[WANTED] = {false};
    for (uint32_t i = 0; i < WANTED; i++) {
        if (hasExtension(devExt, nDevExt, wanted[i])) {
            enable[nEnable++] = wanted[i];
            have[i] = true;
        }
    }
    bool haveF16 = have[0], haveSubgroup = have[1], haveSync2 = have[2], haveIndexing = have[3];
    bool haveCoherent = have[7];
    if (have[4] && !have[5]) {
        printf("ffx_link: skip: VK_KHR_dedicated_allocation without "
               "VK_KHR_get_memory_requirements2\n");
        if (messenger) {
            vkDestroyDebugUtilsMessengerEXT(instance, messenger, NULL);
        }
        vkDestroyInstance(instance, NULL);
        free(devExt);
        return SKIP;
    }
    free(devExt);
    VkPhysicalDeviceShaderFloat16Int8Features f16 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDeviceSubgroupSizeControlFeatures subgroup = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES};
    VkPhysicalDeviceSynchronization2Features sync2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
    VkPhysicalDeviceDescriptorIndexingFeatures indexing = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    VkPhysicalDeviceCoherentMemoryFeaturesAMD coherent = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    void **tail = &features.pNext;
    if (haveCoherent) {
        *tail = &coherent;
        tail = &coherent.pNext;
    }
    if (haveF16) {
        *tail = &f16;
        tail = &f16.pNext;
    }
    if (haveSubgroup) {
        *tail = &subgroup;
        tail = &subgroup.pNext;
    }
    if (haveSync2) {
        *tail = &sync2;
        tail = &sync2.pNext;
    }
    if (haveIndexing) {
        *tail = &indexing;
        tail = &indexing.pNext;
    }
    vkGetPhysicalDeviceFeatures2(pd, &features);
    /* only what the backend uses, from what the device has: the other
     * reported features go back off */
    VkPhysicalDeviceFeatures supported = features.features;
    memset(&features.features, 0, sizeof features.features);
    features.features.shaderInt16 = supported.shaderInt16;
    features.features.shaderStorageImageWriteWithoutFormat =
        supported.shaderStorageImageWriteWithoutFormat;
    f16.shaderInt8 = VK_FALSE;
    subgroup.computeFullSubgroups = VK_FALSE;
    {
        VkPhysicalDeviceDescriptorIndexingFeatures keep = {
            .sType = indexing.sType,
            .pNext = indexing.pNext,
            .shaderStorageBufferArrayNonUniformIndexing =
                indexing.shaderStorageBufferArrayNonUniformIndexing};
        indexing = keep;
    }
    printf("ffx_link: enabled fp16=%u subgroupSizeControl=%u synchronization2=%u "
           "storageBufferNonUniform=%u\n",
           (unsigned)f16.shaderFloat16, (unsigned)subgroup.subgroupSizeControl,
           (unsigned)sync2.synchronization2,
           (unsigned)indexing.shaderStorageBufferArrayNonUniformIndexing);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                   .queueFamilyIndex = family,
                                   .queueCount = 1,
                                   .pQueuePriorities = &priority};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                              .pNext = &features,
                              .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qci,
                              .enabledExtensionCount = nEnable,
                              .ppEnabledExtensionNames = enable};
    VkDevice device = VK_NULL_HANDLE;
    vr = vkCreateDevice(pd, &dci, NULL, &device);
    if (vr != VK_SUCCESS) {
        printf("ffx_link: skip: vkCreateDevice failed (%d)\n", (int)vr);
        if (messenger) {
            vkDestroyDebugUtilsMessengerEXT(instance, messenger, NULL);
        }
        vkDestroyInstance(instance, NULL);
        return SKIP;
    }
    volkLoadDevice(device);

    int failures = 0;

    /* the render size Quality gives at 1280x960, through the query */
    uint32_t rw = 0, rh = 0;
    struct ffxQueryDescUpscaleGetRenderResolutionFromQualityMode rq = {
        .header = {.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE},
        .displayWidth = 1280,
        .displayHeight = 960,
        .qualityMode = FFX_UPSCALE_QUALITY_MODE_QUALITY,
        .pOutRenderWidth = &rw,
        .pOutRenderHeight = &rh};
    ffxReturnCode_t rc = ffxQuery(NULL, &rq.header);
    printf("ffx_link: Quality at 1280x960 renders %ux%u (query %u)\n", rw, rh, rc);
    if (rc != FFX_API_RETURN_OK || rw != 853 || rh != 640) {
        /* 1280 / 1.5 and 960 / 1.5, truncated as FSR does */
        printf("FAIL ffx_link: the Quality render size is %ux%u, want 853x640\n", rw, rh);
        failures++;
    }

    /* the backend and the context: the brief's 640x480 -> 1280x960 */
    struct ffxCreateBackendVKDesc backend = {
        .header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK},
        .vkDevice = device,
        .vkPhysicalDevice = pd,
        .vkDeviceProcAddr = vkGetDeviceProcAddr};
    struct ffxCreateContextDescUpscale up = {
        .header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, .pNext = &backend.header},
        .flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE,
        .maxRenderSize = {640, 480},
        .maxUpscaleSize = {1280, 960},
        .fpMessage = onFfxMessage};
    ffxContext ctx = NULL;
    rc = ffxCreateContext(&ctx, &up.header, NULL);
    printf("ffx_link: ffxCreateContext 640x480 -> 1280x960: %u\n", rc);
    if (rc != FFX_API_RETURN_OK || ctx == NULL) {
        printf("FAIL ffx_link: ffxCreateContext returned %u\n", rc);
        failures++;
    } else {
        struct ffxQueryGetProviderVersion pv = {
            .header = {.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION}};
        rc = ffxQuery(&ctx, &pv.header);
        printf("ffx_link: provider %s (0x%llx), query %u\n", pv.versionName ? pv.versionName : "?",
               (unsigned long long)pv.versionId, rc);
        if (rc != FFX_API_RETURN_OK || !pv.versionName || strncmp(pv.versionName, "3.1.", 4) != 0) {
            printf("FAIL ffx_link: the provider is not FSR 3.1\n");
            failures++;
        }
        FfxApiEffectMemoryUsage mem = {0};
        struct ffxQueryDescUpscaleGetGPUMemoryUsage mq = {
            .header = {.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE},
            .gpuMemoryUsageUpscaler = &mem};
        rc = ffxQuery(&ctx, &mq.header);
        printf("ffx_link: GPU memory %.1f MB (%.1f MB aliasable), query %u\n",
               (double)mem.totalUsageInBytes / (1024.0 * 1024.0),
               (double)mem.aliasableUsageInBytes / (1024.0 * 1024.0), rc);
        if (rc != FFX_API_RETURN_OK || mem.totalUsageInBytes == 0) {
            printf("FAIL ffx_link: no GPU memory reported\n");
            failures++;
        }
        rc = ffxDestroyContext(&ctx, NULL);
        printf("ffx_link: ffxDestroyContext: %u\n", rc);
        if (rc != FFX_API_RETURN_OK) {
            printf("FAIL ffx_link: ffxDestroyContext returned %u\n", rc);
            failures++;
        }
    }

    vkDeviceWaitIdle(device);
    vkDestroyDevice(device, NULL);
    if (messenger) {
        vkDestroyDebugUtilsMessengerEXT(instance, messenger, NULL);
    }
    vkDestroyInstance(instance, NULL);
    if (s_validationErrors) {
        printf("FAIL ffx_link: %u validation errors\n", s_validationErrors);
        failures++;
    }
    printf("ffx_link: %s (%u FFX messages, %u validation errors, validation layer %s)\n",
           failures ? "FAILED" : "ok", s_ffxMessages, s_validationErrors,
           validation && debugUtils ? "on" : "not found");
    return failures ? 1 : 0;
}
