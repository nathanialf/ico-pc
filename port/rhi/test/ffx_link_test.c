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
 * provider (FSR 3.1) and GPU memory queried, the context destroyed. Then
 * the upscale dispatch itself with the textures the port hands it
 * (upscaleDispatchCase: an RGBA8 colour, a D32_SFLOAT_S8_UINT depth read
 * through its depth aspect and an RGBA16F motion at 640x480 into an RGBA8
 * storage image at 960x720, four frames). The device and instance
 * destroyed. Exit 0 on success, 1 on a failure, 77
 * (ctest skip) with the reason when there is no loader, no Vulkan 1.2 device
 * or no device can be made.
 *
 * Validation: the Khronos layer is enabled when the loader finds it (the
 * ctest names the fetched one and turns synchronisation validation on,
 * port/rhi/CMakeLists.txt); with VK_EXT_debug_utils every error and
 * warning message is printed and counted, and any one fails the test. */
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
    /* errors and warnings alike: the layer's warnings name undefined
     * results too (a storage image's format against its view's) */
    s_validationErrors++;
    printf("ffx_link: validation %s: %s\n",
           (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "error" : "warning",
           data && data->pMessage ? data->pMessage : "?");
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

/* One memory type of the device for memoryTypeBits with the flags asked */
static uint32_t memoryType(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits,
                           VkMemoryPropertyFlags want)
{
    for (uint32_t t = 0; t < mp->memoryTypeCount; t++) {
        if (((bits >> t) & 1u) && (mp->memoryTypes[t].propertyFlags & want) == want) {
            return t;
        }
    }
    return UINT32_MAX;
}

/* The upscale dispatch with the textures the port hands it (port/render/
 * rd_fsr3.c): SCENE's colour RGBA8, SCENE's depth D32_SFLOAT_S8_UINT read
 * through its depth aspect, VELOCITY RGBA16F, all 640x480 and read where the
 * RHI leaves them (colour and velocity SHADER_READ_ONLY_OPTIMAL, the depth
 * DEPTH_STENCIL_READ_ONLY_OPTIMAL moved to SHADER_READ_ONLY_OPTIMAL around
 * the dispatch as rd_fsr3.c does), UPSCALED RGBA8 960x720 as a storage image
 * in GENERAL; the port's context flags (inverted depth, auto exposure) with
 * the debug checks on; four frames of FSR's own jitter, each input cleared
 * to a new colour, the output read back: not black, and its centre the
 * input's colour (a flat picture stays flat). Returns the failures. */
static int upscaleDispatchCase(VkDevice device, VkPhysicalDevice pd, uint32_t family)
{
    enum { RW = 640, RH = 480, DW = 960, DH = 720, FRAMES = 4, COLOR = 0, DEPTH, MOTION, OUT };

    int failures = 0;
    const unsigned messages0 = s_ffxMessages;
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, family, 0, &queue);

    static const VkFormat formats[4] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                        VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM};
    static const VkImageAspectFlags aspects[4] = {
        VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT};
    const VkImageUsageFlags sampled = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const VkImageUsageFlags usages[4] = {sampled | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                         sampled | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                         sampled | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    VkImage img[4] = {VK_NULL_HANDLE};
    VkDeviceMemory mem[4] = {VK_NULL_HANDLE};
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    ffxContext ctx = NULL;
    for (int i = 0; i < 4; i++) {
        const VkImageCreateInfo ic = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                      .imageType = VK_IMAGE_TYPE_2D,
                                      .format = formats[i],
                                      .extent = {i == OUT ? DW : RW, i == OUT ? DH : RH, 1},
                                      .mipLevels = 1,
                                      .arrayLayers = 1,
                                      .samples = VK_SAMPLE_COUNT_1_BIT,
                                      .tiling = VK_IMAGE_TILING_OPTIMAL,
                                      .usage = usages[i],
                                      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        VkMemoryRequirements mr;
        if (vkCreateImage(device, &ic, NULL, &img[i]) != VK_SUCCESS) {
            printf("FAIL ffx_link: dispatch: image %d not made\n", i);
            failures++;
            goto done;
        }
        vkGetImageMemoryRequirements(device, img[i], &mr);
        const VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                         .allocationSize = mr.size,
                                         .memoryTypeIndex = memoryType(&mp, mr.memoryTypeBits, 0)};
        if (vkAllocateMemory(device, &ai, NULL, &mem[i]) != VK_SUCCESS ||
            vkBindImageMemory(device, img[i], mem[i], 0) != VK_SUCCESS) {
            printf("FAIL ffx_link: dispatch: image %d memory\n", i);
            failures++;
            goto done;
        }
    }
    {
        const VkBufferCreateInfo bc = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                       .size = (VkDeviceSize)DW * DH * 4u,
                                       .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        VkMemoryRequirements mr;
        if (vkCreateBuffer(device, &bc, NULL, &readback) != VK_SUCCESS) {
            printf("FAIL ffx_link: dispatch: no readback buffer\n");
            failures++;
            goto done;
        }
        vkGetBufferMemoryRequirements(device, readback, &mr);
        const VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                         .allocationSize = mr.size,
                                         .memoryTypeIndex =
                                             memoryType(&mp, mr.memoryTypeBits,
                                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        if (ai.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(device, &ai, NULL, &readbackMem) != VK_SUCCESS ||
            vkBindBufferMemory(device, readback, readbackMem, 0) != VK_SUCCESS) {
            printf("FAIL ffx_link: dispatch: readback memory\n");
            failures++;
            goto done;
        }
    }
    {
        const VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                             .flags =
                                                 VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                             .queueFamilyIndex = family};
        const VkCommandBufferAllocateInfo cai = {.sType =
                                                     VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                 .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                 .commandBufferCount = 1};
        if (vkCreateCommandPool(device, &pci, NULL, &pool) != VK_SUCCESS) {
            failures++;
            goto done;
        }
        VkCommandBufferAllocateInfo a = cai;
        a.commandPool = pool;
        if (vkAllocateCommandBuffers(device, &a, &cb) != VK_SUCCESS) {
            failures++;
            goto done;
        }
    }

    /* the context as rd_fsr3.c makes it */
    struct ffxCreateBackendVKDesc backend = {
        .header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK},
        .vkDevice = device,
        .vkPhysicalDevice = pd,
        .vkDeviceProcAddr = vkGetDeviceProcAddr};
    struct ffxCreateContextDescUpscale up = {
        .header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, .pNext = &backend.header},
        .flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE |
                 FFX_UPSCALE_ENABLE_DEBUG_CHECKING,
        .maxRenderSize = {RW, RH},
        .maxUpscaleSize = {DW, DH},
        .fpMessage = onFfxMessage};
    ffxReturnCode_t rc = ffxCreateContext(&ctx, &up.header, NULL);
    int32_t phases = 0;
    struct ffxQueryDescUpscaleGetJitterPhaseCount pq = {
        .header = {.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT},
        .renderWidth = RW,
        .displayWidth = DW,
        .pOutPhaseCount = &phases};
    const ffxReturnCode_t prc = ffxQuery(NULL, &pq.header);
    printf("ffx_link: dispatch: context %ux%u -> %ux%u: %u; phase count %d (query %u)\n", RW, RH,
           DW, DH, rc, phases, prc);
    if (rc != FFX_API_RETURN_OK || !ctx || prc != FFX_API_RETURN_OK || phases != 18) {
        /* ceil(8 * 1.5^2) = 18 */
        printf("FAIL ffx_link: dispatch: the context or the phase count\n");
        failures++;
        goto done;
    }

    for (int frame = 0; frame < FRAMES; frame++) {
        const VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                             .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        vkResetCommandBuffer(cb, 0);
        vkBeginCommandBuffer(cb, &bi);
        VkImageMemoryBarrier b[4];
        for (int i = 0; i < 4; i++) {
            b[i] = (VkImageMemoryBarrier){.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                          .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                          .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .image = img[i],
                                          .subresourceRange = {aspects[i], 0, 1, 0, 1}};
        }
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL, 3, b);
        /* a flat colour that brightens every frame, a far depth, a motion of
         * two texels left */
        const VkClearColorValue color = {.float32 = {0.2f + 0.1f * (float)frame, 0.5f, 0.7f, 1.0f}};
        const VkClearColorValue motion = {.float32 = {-2.0f, 0.0f, 0.0f, 1.0f}};
        const VkClearDepthStencilValue depth = {0.25f, 0};
        const VkImageSubresourceRange cr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        const VkImageSubresourceRange dr = {aspects[DEPTH], 0, 1, 0, 1};
        vkCmdClearColorImage(cb, img[COLOR], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &cr);
        vkCmdClearDepthStencilImage(cb, img[DEPTH], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &depth, 1,
                                    &dr);
        vkCmdClearColorImage(cb, img[MOTION], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &motion, 1,
                             &cr);
        /* where the RHI leaves them (vk_enums.h vkr_state_layout: a depth
         * format's SHADER_READ is DEPTH_STENCIL_READ_ONLY_OPTIMAL) */
        for (int i = 0; i < 3; i++) {
            b[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b[i].newLayout = i == DEPTH ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        b[OUT].srcAccessMask = frame ? VK_ACCESS_TRANSFER_READ_BIT : 0;
        b[OUT].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        b[OUT].oldLayout = frame ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        b[OUT].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, NULL, 0, NULL, 4, b);
        /* rd_fsr3.c's own step: the depth into the layout FFX's descriptor
         * names, both aspects (no separate depth/stencil layouts) */
        VkImageMemoryBarrier db = b[DEPTH];
        db.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        db.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        db.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        db.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &db);

        struct FfxApiResource in[4];
        static const uint32_t ffxFormats[4] = {
            FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM, FFX_API_SURFACE_FORMAT_R32_FLOAT,
            FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT, FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM};
        for (int i = 0; i < 4; i++) {
            memset(&in[i], 0, sizeof(in[i]));
            in[i].resource = (void *)img[i];
            in[i].description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
            in[i].description.format = ffxFormats[i];
            in[i].description.width = i == OUT ? DW : RW;
            in[i].description.height = i == OUT ? DH : RH;
            in[i].description.depth = 1;
            in[i].description.mipCount = 1;
            in[i].description.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;
            in[i].state = FFX_API_RESOURCE_STATE_COMPUTE_READ;
        }
        in[DEPTH].description.usage =
            FFX_API_RESOURCE_USAGE_DEPTHTARGET | FFX_API_RESOURCE_USAGE_STENCILTARGET;
        in[OUT].description.usage = FFX_API_RESOURCE_USAGE_UAV;
        in[OUT].state = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
        float jx = 0.0f, jy = 0.0f;
        struct ffxQueryDescUpscaleGetJitterOffset jq = {
            .header = {.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET},
            .index = frame,
            .phaseCount = phases,
            .pOutX = &jx,
            .pOutY = &jy};
        (void)ffxQuery(&ctx, &jq.header);
        struct ffxDispatchDescUpscale d = {
            .header = {.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE},
            .commandList = (void *)cb,
            .color = in[COLOR],
            .depth = in[DEPTH],
            .motionVectors = in[MOTION],
            .output = in[OUT],
            .jitterOffset = {jx, jy},
            .motionVectorScale = {1.0f, 1.0f},
            .renderSize = {RW, RH},
            .upscaleSize = {DW, DH},
            .enableSharpening = true,
            .sharpness = 0.8f,
            .frameTimeDelta = 16.7f,
            .preExposure = 1.0f,
            .reset = frame == 0,
            /* inverted depth: FFX takes the far plane as cameraNear */
            .cameraNear = 10000.0f,
            .cameraFar = 1.0f,
            .cameraFovAngleVertical = 1.0f,
            .viewSpaceToMetersFactor = 1.0f};
        rc = ffxDispatch(&ctx, &d.header);
        db.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        db.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &db);
        VkImageMemoryBarrier ob = b[OUT];
        ob.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ob.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        ob.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        ob.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ob);
        const VkBufferImageCopy bic = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                       .imageExtent = {DW, DH, 1}};
        vkCmdCopyImageToBuffer(cb, img[OUT], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1,
                               &bic);
        vkEndCommandBuffer(cb);
        const VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                 .commandBufferCount = 1,
                                 .pCommandBuffers = &cb};
        if (rc != FFX_API_RETURN_OK || vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS ||
            vkQueueWaitIdle(queue) != VK_SUCCESS) {
            printf("FAIL ffx_link: dispatch: frame %d ffxDispatch %u\n", frame, rc);
            failures++;
            break;
        }
        uint8_t *p = NULL;
        if (vkMapMemory(device, readbackMem, 0, VK_WHOLE_SIZE, 0, (void **)&p) != VK_SUCCESS) {
            failures++;
            break;
        }
        uint64_t sum = 0;
        for (size_t i = 0; i < (size_t)DW * DH * 4u; i += 4u) {
            sum += (uint64_t)p[i] + p[i + 1] + p[i + 2];
        }
        const uint8_t *c = p + ((size_t)(DH / 2) * DW + DW / 2) * 4u;
        const int want = (int)(color.float32[0] * 255.0f + 0.5f);
        printf("ffx_link: dispatch: frame %d jitter %.4f, %.4f: mean %.1f, centre %u %u %u "
               "(input %d 128 178)\n",
               frame, (double)jx, (double)jy, (double)sum / ((double)DW * DH * 3.0), c[0], c[1],
               c[2], want);
        if (sum == 0u || c[0] < want - 3 || c[0] > want + 3 || c[1] < 125 || c[1] > 131 ||
            c[2] < 175 || c[2] > 182) {
            printf("FAIL ffx_link: dispatch: frame %d's output is not the flat input\n", frame);
            failures++;
        }
        vkUnmapMemory(device, readbackMem);
    }
    if (s_ffxMessages != messages0) {
        printf("FAIL ffx_link: dispatch: %u FFX messages\n", s_ffxMessages - messages0);
        failures++;
    }

done:
    if (ctx) {
        (void)ffxDestroyContext(&ctx, NULL);
    }
    vkDeviceWaitIdle(device);
    if (pool) {
        vkDestroyCommandPool(device, pool, NULL);
    }
    if (readback) {
        vkDestroyBuffer(device, readback, NULL);
    }
    if (readbackMem) {
        vkFreeMemory(device, readbackMem, NULL);
    }
    for (int i = 0; i < 4; i++) {
        if (img[i]) {
            vkDestroyImage(device, img[i], NULL);
        }
        if (mem[i]) {
            vkFreeMemory(device, mem[i], NULL);
        }
    }
    return failures;
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

    /* the dispatch with the port's formats (rd_fsr3.c) */
    failures += upscaleDispatchCase(device, pd, family);

    vkDeviceWaitIdle(device);
    vkDestroyDevice(device, NULL);
    if (messenger) {
        vkDestroyDebugUtilsMessengerEXT(instance, messenger, NULL);
    }
    vkDestroyInstance(instance, NULL);
    if (s_validationErrors) {
        printf("FAIL ffx_link: %u validation messages\n", s_validationErrors);
        failures++;
    }
    printf("ffx_link: %s (%u FFX messages, %u validation messages, validation layer %s)\n",
           failures ? "FAILED" : "ok", s_ffxMessages, s_validationErrors,
           validation && debugUtils ? "on" : "not found");
    return failures ? 1 : 0;
}
