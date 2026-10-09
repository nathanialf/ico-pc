/* vk_resource.c: buffers, textures, samplers, shaders and deferred
 * destruction for the Vulkan backend.  Every buffer and texture has its own
 * VkDeviceMemory (vkr_allocate); there is no suballocation. */
#include "vk_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ memory types */
bool vkr_find_memory_type(uint32_t typeBits, VkMemoryPropertyFlags want,
                          VkMemoryPropertyFlags avoid, uint32_t *out)
{
    for (uint32_t i = 0; i < g_vkr.memProps.memoryTypeCount; i++) {
        VkMemoryPropertyFlags f = g_vkr.memProps.memoryTypes[i].propertyFlags;
        if ((typeBits & (1u << i)) && (f & want) == want && (f & avoid) == 0) {
            *out = i;
            return true;
        }
    }
    return false;
}

/* every vkFreeMemory, so the live counts stay right */
void vkr_free_memory(VkDeviceMemory memory, VkDeviceSize size)
{
    if (!memory) {
        return;
    }
    vkFreeMemory(g_vkr.device, memory, NULL);
    if (g_vkr.memLive) {
        g_vkr.memLive--;
    }
    g_vkr.memLiveBytes = g_vkr.memLiveBytes > size ? g_vkr.memLiveBytes - size : 0;
}

/* Past the device's allocation limit
   (VkPhysicalDeviceLimits.maxMemoryAllocationCount; 4096 on phone GPUs and
   some desktop drivers) a line says so once per time it is reached, and the
   driver is still asked (it may allow more); under ICO_VK_FAKE_LIMITS the
   allocation is refused, as a strict driver would. */
static bool vkr_memory_room(VkDeviceSize size)
{
    const uint32_t limit = g_vkr.props.limits.maxMemoryAllocationCount;
    if (limit && g_vkr.memLive >= limit) {
        g_vkr.lastLimit = "maxMemoryAllocationCount";
        if (!g_vkr.memLimitLogged) {
            g_vkr.memLimitLogged = true;
            VKR_LOG("%u device memory allocations alive (%.0f MB), the most the device allows "
                    "(maxMemoryAllocationCount %u): a %.0f KB allocation %s",
                    g_vkr.memLive, (double)g_vkr.memLiveBytes / 1048576.0, limit,
                    (double)size / 1024.0,
                    g_vkr.fakeLimits ? "is refused (ICO_VK_FAKE_LIMITS)"
                                     : "goes past it (the driver may refuse it)");
        }
        return g_vkr.fakeLimits == NULL;
    }
    return true;
}

static void vkr_memory_counted(VkDeviceSize size)
{
    g_vkr.memLive++;
    g_vkr.memLiveBytes += size;
    if (g_vkr.memLive > g_vkr.memPeak) {
        g_vkr.memPeak = g_vkr.memLive;
    }
    if (g_vkr.memLiveBytes > g_vkr.memPeakBytes) {
        g_vkr.memPeakBytes = g_vkr.memLiveBytes;
    }
    if (g_vkr.memNextLog == 0) {
        g_vkr.memNextLog = 1024;
    }
    if (g_vkr.memLive >= g_vkr.memNextLog) {
        VKR_LOG("%u device memory allocations alive (%.0f MB; the device allows %u)", g_vkr.memLive,
                (double)g_vkr.memLiveBytes / 1048576.0,
                g_vkr.props.limits.maxMemoryAllocationCount);
        g_vkr.memNextLog += 1024;
    }
    if (g_vkr.memLimitLogged && g_vkr.memLive + 64 < g_vkr.props.limits.maxMemoryAllocationCount) {
        g_vkr.memLimitLogged = false; /* the next time the limit is reached is logged too */
    }
}

static bool vkr_allocate(const VkMemoryRequirements *req, RhiMemory kind, VkDeviceMemory *out,
                         bool *coherent)
{
    uint32_t type = 0;
    if (!vkr_memory_room(req->size)) {
        return false;
    }
    const VkMemoryPropertyFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    const VkMemoryPropertyFlags hc = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    bool ok = false;
    switch (kind) {
    case RHI_MEM_DEVICE:
        ok = vkr_find_memory_type(req->memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                                  &type) ||
             vkr_find_memory_type(req->memoryTypeBits, 0, 0, &type);
        break;
    case RHI_MEM_UPLOAD:
        /* device-local and host-visible (resizable BAR / UMA) first, then
         * plain host memory; always coherent so writes need no flush */
        ok = vkr_find_memory_type(req->memoryTypeBits,
                                  hv | hc | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &type) ||
             vkr_find_memory_type(req->memoryTypeBits, hv | hc, 0, &type);
        break;
    case RHI_MEM_READBACK:
        ok = vkr_find_memory_type(req->memoryTypeBits, hv | hc | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                  0, &type) ||
             vkr_find_memory_type(req->memoryTypeBits, hv | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0,
                                  &type) ||
             vkr_find_memory_type(req->memoryTypeBits, hv | hc, 0, &type);
        break;
    }
    if (!ok) {
        VKR_LOG("no memory type for kind %d (bits 0x%x)", (int)kind, req->memoryTypeBits);
        return false;
    }
    if (coherent) {
        *coherent = (g_vkr.memProps.memoryTypes[type].propertyFlags & hc) != 0;
    }
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req->size,
        .memoryTypeIndex = type,
    };
    if (!VKR_CHECK(vkAllocateMemory(g_vkr.device, &ai, NULL, out))) {
        VKR_LOG("a %.0f KB device memory allocation failed with %u alive (%.0f MB)",
                (double)req->size / 1024.0, g_vkr.memLive, (double)g_vkr.memLiveBytes / 1048576.0);
        *out = VK_NULL_HANDLE;
        return false;
    }
    vkr_memory_counted(req->size);
    return true;
}

static void vkr_set_name(VkObjectType type, uint64_t handle, const char *name)
{
    if (!g_vkr.debugUtils || !name || !vkSetDebugUtilsObjectNameEXT) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT ni = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = name,
    };
    vkSetDebugUtilsObjectNameEXT(g_vkr.device, &ni);
}

/* ------------------------------------------------------ deferred destroy */
static void vkr_destroy_one(const VkrGarbage *e);

static void vkr_defer_sized(VkrGarbageKind kind, uint64_t handle, uint64_t size);

void vkr_defer(VkrGarbageKind kind, uint64_t handle)
{
    vkr_defer_sized(kind, handle, 0);
}

void vkr_defer_memory(VkDeviceMemory memory, VkDeviceSize size)
{
    vkr_defer_sized(VKR_GARBAGE_MEMORY, (uint64_t)memory, size);
}

static void vkr_defer_sized(VkrGarbageKind kind, uint64_t handle, uint64_t size)
{
    if (!handle) {
        return;
    }
    VkrFrame *f = vkr_cur_frame();
    if (f->garbageCount == f->garbageCap) {
        uint32_t cap = f->garbageCap ? f->garbageCap * 2u : 64u;
        VkrGarbage *g = realloc(f->garbage, cap * sizeof(*g));
        if (!g) {
            /* the frame's list cannot grow: park the entry in the fixed
             * overflow array, destroyed with this slot's garbage.  Never
             * destroy the frame's garbage here: an unsubmitted command
             * list may still use it. */
            const uint32_t slot = (uint32_t)(f - g_vkr.frames);
            if (g_vkr.overflowCount == VKR_GARBAGE_OVERFLOW) {
                /* full: the other slots' entries are free after an idle */
                VKR_LOG("out of memory for deferred destroys; waiting idle");
                vkDeviceWaitIdle(g_vkr.device);
                uint32_t keep = 0;
                for (uint32_t i = 0; i < g_vkr.overflowCount; i++) {
                    VkrGarbage *e = &g_vkr.overflow[i];
                    if (e->slot != slot) {
                        vkr_destroy_one(e);
                    } else {
                        g_vkr.overflow[keep++] = *e;
                    }
                }
                g_vkr.overflowCount = keep;
            }
            if (g_vkr.overflowCount == VKR_GARBAGE_OVERFLOW) {
                /* every entry belongs to the current frame: nothing can be
                 * destroyed safely, so this one object leaks */
                VKR_LOG("deferred destroy overflow full; leaking one object");
                return;
            }
            VkrGarbage *e = &g_vkr.overflow[g_vkr.overflowCount++];
            e->kind = kind;
            e->handle = handle;
            e->size = size;
            e->slot = slot;
            return;
        }
        f->garbage = g;
        f->garbageCap = cap;
    }
    f->garbage[f->garbageCount].kind = kind;
    f->garbage[f->garbageCount].handle = handle;
    f->garbage[f->garbageCount].size = size;
    f->garbageCount++;
}

static void vkr_destroy_one(const VkrGarbage *e)
{
    VkDevice d = g_vkr.device;
    const uint64_t h = e->handle;
    switch (e->kind) {
    case VKR_GARBAGE_BUFFER:
        vkDestroyBuffer(d, (VkBuffer)h, NULL);
        break;
    case VKR_GARBAGE_IMAGE:
        vkDestroyImage(d, (VkImage)h, NULL);
        break;
    case VKR_GARBAGE_VIEW:
        vkDestroyImageView(d, (VkImageView)h, NULL);
        break;
    case VKR_GARBAGE_MEMORY:
        vkr_free_memory((VkDeviceMemory)h, e->size);
        break;
    case VKR_GARBAGE_SAMPLER:
        vkDestroySampler(d, (VkSampler)h, NULL);
        if (g_vkr.samplersLive) {
            g_vkr.samplersLive--;
        }
        break;
    case VKR_GARBAGE_PIPELINE:
        vkDestroyPipeline(d, (VkPipeline)h, NULL);
        break;
    case VKR_GARBAGE_PIPELINE_LAYOUT:
        vkDestroyPipelineLayout(d, (VkPipelineLayout)h, NULL);
        break;
    case VKR_GARBAGE_SET_LAYOUT:
        vkDestroyDescriptorSetLayout(d, (VkDescriptorSetLayout)h, NULL);
        break;
    case VKR_GARBAGE_SHADER:
        vkDestroyShaderModule(d, (VkShaderModule)h, NULL);
        break;
    }
}

void vkr_destroy_garbage(VkrFrame *f)
{
    for (uint32_t i = 0; i < f->garbageCount; i++) {
        vkr_destroy_one(&f->garbage[i]);
    }
    f->garbageCount = 0;
    /* the entries that overflowed from this slot (vkr_defer) */
    const uint32_t slot = (uint32_t)(f - g_vkr.frames);
    uint32_t keep = 0;
    for (uint32_t i = 0; i < g_vkr.overflowCount; i++) {
        VkrGarbage *e = &g_vkr.overflow[i];
        if (e->slot == slot) {
            vkr_destroy_one(e);
        } else {
            g_vkr.overflow[keep++] = *e;
        }
    }
    g_vkr.overflowCount = keep;
}

/* Handles are cast through uintptr_t-sized integers: on 32-bit targets
 * non-dispatchable handles are uint64_t already, on 64-bit they are
 * pointers. */
#define VKR_H(x) ((uint64_t)(x))

/* --------------------------------------------------------------- buffers */
VkrBuffer *vkr_get_buffer(RhiBuffer b)
{
    return vkr_pool_get(&g_vkr.buffers, b.id);
}

RhiBuffer rhi_create_buffer(const RhiBufferDesc *desc)
{
    RhiBuffer out = {0};
    if (!desc || desc->size == 0) {
        return out;
    }
    VkBufferUsageFlags usage = 0;
    if (desc->usage & RHI_BUF_VERTEX) {
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (desc->usage & RHI_BUF_INDEX) {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (desc->usage & RHI_BUF_UNIFORM) {
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (desc->usage & RHI_BUF_STORAGE_READ) {
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (desc->usage & RHI_BUF_COPY_SRC) {
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (desc->usage & (RHI_BUF_COPY_DST | RHI_BUF_READBACK)) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    if (desc->memory == RHI_MEM_DEVICE) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT; /* the only way to fill it */
    }
    if (desc->memory == RHI_MEM_UPLOAD) {
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT; /* staging for texture uploads */
    }
    if (desc->memory == RHI_MEM_READBACK) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VkrBuffer *b = NULL;
    uint32_t id = vkr_pool_alloc(&g_vkr.buffers, (void **)&b);
    if (!id) {
        return out;
    }
    VkBufferCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc->size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (!VKR_CHECK(vkCreateBuffer(g_vkr.device, &ci, NULL, &b->buffer))) {
        vkr_pool_release(&g_vkr.buffers, id);
        return out;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vkr.device, b->buffer, &req);
    if (!vkr_allocate(&req, desc->memory, &b->memory, &b->coherent) ||
        !VKR_CHECK(vkBindBufferMemory(g_vkr.device, b->buffer, b->memory, 0))) {
        vkDestroyBuffer(g_vkr.device, b->buffer, NULL);
        vkr_free_memory(b->memory, req.size);
        vkr_pool_release(&g_vkr.buffers, id);
        return out;
    }
    b->memSize = req.size;
    b->size = desc->size;
    b->kind = desc->memory;
    if (desc->memory != RHI_MEM_DEVICE) {
        if (!VKR_CHECK(vkMapMemory(g_vkr.device, b->memory, 0, VK_WHOLE_SIZE, 0, &b->mapped))) {
            b->mapped = NULL;
        }
    }
    vkr_set_name(VK_OBJECT_TYPE_BUFFER, VKR_H(b->buffer), desc->debugName);
    g_vkr.stats.buffersCreated++;
    g_vkr.stats.memoryAllocs++;
    out.id = id;
    return out;
}

void rhi_destroy_buffer(RhiBuffer h)
{
    VkrBuffer *b = vkr_get_buffer(h);
    if (!b) {
        return;
    }
    vkr_defer(VKR_GARBAGE_BUFFER, VKR_H(b->buffer));
    vkr_defer_memory(b->memory, b->memSize); /* freeing memory unmaps it */
    vkr_pool_release(&g_vkr.buffers, h.id);
    g_vkr.stats.buffersDestroyed++;
    g_vkr.stats.memoryFrees++;
}

void *rhi_map_buffer(RhiBuffer h)
{
    VkrBuffer *b = vkr_get_buffer(h);
    if (!b || !b->mapped) {
        return NULL;
    }
    if (!b->coherent) {
        /* readback memory without HOST_COHERENT: make GPU writes visible */
        VkMappedMemoryRange r = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = b->memory,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        vkInvalidateMappedMemoryRanges(g_vkr.device, 1, &r);
    }
    return b->mapped;
}

void rhi_unmap_buffer(RhiBuffer h)
{
    (void)h; /* persistently mapped; upload memory is always coherent */
}

/* -------------------------------------------------------------- textures */
VkrTexture *vkr_get_texture(RhiTexture t)
{
    return vkr_pool_get(&g_vkr.textures, t.id);
}

static bool vkr_create_view(VkImage image, VkFormat fmt, VkImageAspectFlags aspect, uint32_t mips,
                            VkImageView *out)
{
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt,
        .subresourceRange = {aspect, 0, mips, 0, 1},
    };
    return VKR_CHECK(vkCreateImageView(g_vkr.device, &vi, NULL, out));
}

/* vkr_test_fail_texels_above: 0 is off */
static uint64_t s_failTexelsAbove;

void vkr_test_fail_texels_above(uint64_t texels)
{
    s_failTexelsAbove = texels;
}

RhiTexture rhi_create_texture(const RhiTextureDesc *desc)
{
    RhiTexture out = {0};
    if (!desc || desc->format <= RHI_FMT_UNKNOWN || desc->format >= RHI_FMT_COUNT ||
        desc->width == 0 || desc->height == 0) {
        return out;
    }
    if (rhi_format_is_block(desc->format) &&
        (!g_vkr.limits.bcTextures ||
         (desc->usage & (RHI_TEX_RENDER_TARGET | RHI_TEX_DEPTH_STENCIL)))) {
        /* texture packs: BC needs the device feature (RhiLimits.bcTextures)
         * and is sampled and copied into only, never drawn to */
        return out;
    }
    if (s_failTexelsAbove && (uint64_t)desc->width * desc->height > s_failTexelsAbove) {
        return out; /* the test's stand-in for a device that is out of memory */
    }
    const VkrFormatMap *fm = &vkr_formatMap[desc->format];
    const VkFormat vkFmt = vkr_vk_format(desc->format);
    VkImageUsageFlags usage = 0;
    if (desc->usage & RHI_TEX_SAMPLED) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (desc->usage & RHI_TEX_RENDER_TARGET) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (desc->usage & RHI_TEX_DEPTH_STENCIL) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (desc->usage & RHI_TEX_COPY_SRC) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (desc->usage & RHI_TEX_COPY_DST) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    /* rhi_readback_texture works on any texture */
    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkrTexture *t = NULL;
    uint32_t id = vkr_pool_alloc(&g_vkr.textures, (void **)&t);
    if (!id) {
        return out;
    }
    uint32_t mips = desc->mipLevels ? desc->mipLevels : 1u;
    VkImageCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vkFmt,
        .extent = {desc->width, desc->height, 1},
        .mipLevels = mips,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!VKR_CHECK(vkCreateImage(g_vkr.device, &ci, NULL, &t->image))) {
        vkr_pool_release(&g_vkr.textures, id);
        return out;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_vkr.device, t->image, &req);
    if (!vkr_allocate(&req, RHI_MEM_DEVICE, &t->memory, NULL) ||
        !VKR_CHECK(vkBindImageMemory(g_vkr.device, t->image, t->memory, 0))) {
        goto fail;
    }
    t->memSize = req.size;
    t->format = vkFmt;
    t->rhiFormat = desc->format;
    t->aspects = fm->aspect;
    t->width = desc->width;
    t->height = desc->height;
    t->mips = mips;
    /* copy-only textures (staging, R8 copy targets) have no view: a view
     * needs a sampled or attachment usage */
    if ((desc->usage & (RHI_TEX_SAMPLED | RHI_TEX_RENDER_TARGET | RHI_TEX_DEPTH_STENCIL)) &&
        !vkr_create_view(t->image, vkFmt, fm->aspect, mips, &t->view)) {
        goto fail;
    }
    if ((fm->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) && (desc->usage & RHI_TEX_SAMPLED)) {
        /* a depth-stencil view cannot be sampled; sampling sees depth only */
        if (!vkr_create_view(t->image, vkFmt, VK_IMAGE_ASPECT_DEPTH_BIT, mips, &t->depthView)) {
            goto fail;
        }
    } else {
        t->depthView = VK_NULL_HANDLE;
    }
    vkr_set_name(VK_OBJECT_TYPE_IMAGE, VKR_H(t->image), desc->debugName);
    g_vkr.stats.texturesCreated++;
    g_vkr.stats.memoryAllocs++;
    out.id = id;
    return out;

fail:
    if (t->view) {
        vkDestroyImageView(g_vkr.device, t->view, NULL);
    }
    vkDestroyImage(g_vkr.device, t->image, NULL);
    vkr_free_memory(t->memory, req.size);
    vkr_pool_release(&g_vkr.textures, id);
    return out;
}

void rhi_destroy_texture(RhiTexture h)
{
    VkrTexture *t = vkr_get_texture(h);
    if (!t || t->swapchain) {
        return;
    }
    vkr_defer(VKR_GARBAGE_VIEW, VKR_H(t->view));
    vkr_defer(VKR_GARBAGE_VIEW, VKR_H(t->depthView));
    vkr_defer(VKR_GARBAGE_IMAGE, VKR_H(t->image));
    vkr_defer_memory(t->memory, t->memSize);
    vkr_pool_release(&g_vkr.textures, h.id);
    g_vkr.stats.texturesDestroyed++;
    g_vkr.stats.memoryFrees++;
}

uint32_t vkr_register_swapchain_image(VkImage image, VkFormat fmt, RhiFormat rf, uint32_t w,
                                      uint32_t h)
{
    VkrTexture *t = NULL;
    uint32_t id = vkr_pool_alloc(&g_vkr.textures, (void **)&t);
    if (!id) {
        return 0;
    }
    t->image = image;
    t->format = fmt;
    t->rhiFormat = rf;
    t->aspects = VK_IMAGE_ASPECT_COLOR_BIT;
    t->width = w;
    t->height = h;
    t->mips = 1;
    t->swapchain = true;
    if (!vkr_create_view(image, fmt, VK_IMAGE_ASPECT_COLOR_BIT, 1, &t->view)) {
        vkr_pool_release(&g_vkr.textures, id);
        return 0;
    }
    return id;
}

void vkr_release_swapchain_image(uint32_t id)
{
    RhiTexture h = {id};
    VkrTexture *t = vkr_get_texture(h);
    if (!t) {
        return;
    }
    /* called after vkDeviceWaitIdle: destroy now */
    vkDestroyImageView(g_vkr.device, t->view, NULL);
    vkr_pool_release(&g_vkr.textures, id);
}

/* -------------------------------------------------------------- samplers */
RhiSampler rhi_create_sampler(const RhiSamplerDesc *desc)
{
    RhiSampler out = {0};
    if (!desc) {
        return out;
    }
    const uint32_t samplerLimit = g_vkr.props.limits.maxSamplerAllocationCount;
    if (samplerLimit && g_vkr.samplersLive >= samplerLimit) {
        /* as vkr_memory_room: refused under ICO_VK_FAKE_LIMITS only */
        g_vkr.lastLimit = "maxSamplerAllocationCount";
        if (!g_vkr.samplerLimitLogged) {
            g_vkr.samplerLimitLogged = true;
            VKR_LOG("%u samplers alive, the most the device allows (maxSamplerAllocationCount "
                    "%u): a new one %s",
                    g_vkr.samplersLive, samplerLimit,
                    g_vkr.fakeLimits ? "is refused (ICO_VK_FAKE_LIMITS)" : "goes past it");
        }
        if (g_vkr.fakeLimits) {
            return out;
        }
    }
    VkSampler *s = NULL;
    uint32_t id = vkr_pool_alloc(&g_vkr.samplers, (void **)&s);
    if (!id) {
        return out;
    }
    const VkFilter filt[2] = {VK_FILTER_NEAREST, VK_FILTER_LINEAR};
    const VkSamplerAddressMode wrap[2] = {VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    bool aniso = g_vkr.anisotropy && desc->maxAnisotropy > 1.0f;
    float maxAniso = desc->maxAnisotropy;
    if (aniso && maxAniso > g_vkr.props.limits.maxSamplerAnisotropy) {
        maxAniso = g_vkr.props.limits.maxSamplerAnisotropy;
    }
    VkSamplerCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filt[desc->mag & 1],
        .minFilter = filt[desc->min & 1],
        .mipmapMode = desc->mip == RHI_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                     : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = wrap[desc->s & 1],
        .addressModeV = wrap[desc->t & 1],
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = desc->lodBias,
        .anisotropyEnable = aniso ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = aniso ? maxAniso : 1.0f,
        .minLod = desc->minLod,
        .maxLod = desc->maxLod,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    };
    if (!VKR_CHECK(vkCreateSampler(g_vkr.device, &ci, NULL, s))) {
        vkr_pool_release(&g_vkr.samplers, id);
        return out;
    }
    g_vkr.samplersLive++;
    out.id = id;
    return out;
}

void rhi_destroy_sampler(RhiSampler h)
{
    VkSampler *s = vkr_pool_get(&g_vkr.samplers, h.id);
    if (!s) {
        return;
    }
    vkr_defer(VKR_GARBAGE_SAMPLER, VKR_H(*s));
    vkr_pool_release(&g_vkr.samplers, h.id);
}

/* --------------------------------------------------------------- shaders */
RhiShader rhi_create_shader(const RhiShaderDesc *desc)
{
    RhiShader out = {0};
    if (!desc || !desc->bytecode || desc->bytecodeSize < 20 || (desc->bytecodeSize & 3u)) {
        return out;
    }
    if (*(const uint32_t *)desc->bytecode != 0x07230203u) {
        VKR_LOG("shader %s is not SPIR-V", desc->debugName ? desc->debugName : "?");
        return out;
    }
    VkrShader *s = NULL;
    uint32_t id = vkr_pool_alloc(&g_vkr.shaders, (void **)&s);
    if (!id) {
        return out;
    }
    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = desc->bytecodeSize,
        .pCode = desc->bytecode,
    };
    if (!VKR_CHECK(vkCreateShaderModule(g_vkr.device, &ci, NULL, &s->module))) {
        vkr_pool_release(&g_vkr.shaders, id);
        return out;
    }
    s->stage = desc->stage;
    snprintf(s->entry, sizeof(s->entry), "%s", desc->entryPoint ? desc->entryPoint : "main");
    vkr_set_name(VK_OBJECT_TYPE_SHADER_MODULE, VKR_H(s->module), desc->debugName);
    out.id = id;
    return out;
}

void rhi_destroy_shader(RhiShader h)
{
    VkrShader *s = vkr_pool_get(&g_vkr.shaders, h.id);
    if (!s) {
        return;
    }
    /* modules are only needed during pipeline creation, but a deferred
     * destroy keeps the rule uniform */
    vkr_defer(VKR_GARBAGE_SHADER, VKR_H(s->module));
    vkr_pool_release(&g_vkr.shaders, h.id);
}

/* ---------------------------------------------------------------- teardown */
void vkr_release_all_objects(void)
{
    VkDevice d = g_vkr.device;
    for (uint32_t i = 0; i < g_vkr.buffers.next; i++) {
        if (g_vkr.buffers.live[i]) {
            VkrBuffer *b = (VkrBuffer *)(g_vkr.buffers.data + (size_t)i * sizeof(VkrBuffer));
            vkDestroyBuffer(d, b->buffer, NULL);
            vkr_free_memory(b->memory, b->memSize);
        }
    }
    for (uint32_t i = 0; i < g_vkr.textures.next; i++) {
        if (g_vkr.textures.live[i]) {
            VkrTexture *t = (VkrTexture *)(g_vkr.textures.data + (size_t)i * sizeof(VkrTexture));
            if (t->view) {
                vkDestroyImageView(d, t->view, NULL);
            }
            if (t->depthView) {
                vkDestroyImageView(d, t->depthView, NULL);
            }
            if (!t->swapchain) {
                vkDestroyImage(d, t->image, NULL);
                vkr_free_memory(t->memory, t->memSize);
            }
        }
    }
    for (uint32_t i = 0; i < g_vkr.samplers.next; i++) {
        if (g_vkr.samplers.live[i]) {
            vkDestroySampler(d, ((VkSampler *)g_vkr.samplers.data)[i], NULL);
        }
    }
    for (uint32_t i = 0; i < g_vkr.shaders.next; i++) {
        if (g_vkr.shaders.live[i]) {
            vkDestroyShaderModule(d, ((VkrShader *)g_vkr.shaders.data)[i].module, NULL);
        }
    }
    for (uint32_t i = 0; i < g_vkr.pipelines.next; i++) {
        if (g_vkr.pipelines.live[i]) {
            VkrPipeline *p = &((VkrPipeline *)g_vkr.pipelines.data)[i];
            vkDestroyPipeline(d, p->pipeline, NULL);
            vkDestroyPipelineLayout(d, p->layout, NULL);
        }
    }
    for (uint32_t i = 0; i < g_vkr.layouts.next; i++) {
        if (g_vkr.layouts.live[i]) {
            vkDestroyDescriptorSetLayout(d, ((VkrLayout *)g_vkr.layouts.data)[i].layout, NULL);
        }
    }
}
