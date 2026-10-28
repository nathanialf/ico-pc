/* vk_resource.c: buffers, textures, samplers, shaders and deferred
 * destruction for the Vulkan backend.  Every buffer and texture has its own
 * VkDeviceMemory (vkr_allocate); there is no suballocation. */
#include "vk_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------ allocation table
 * Every VkDeviceMemory alive (one per buffer or image: there is no
 * suballocation), with the object bound to it, its size and debug name, and
 * the object's GPU address range when the address binding report gives it
 * (VkrState.addressReport).  Read by the device-loss report only
 * (vk_device.c vkr_lost_report): a fault address named by the driver
 * (VK_EXT_device_fault) is matched to the resource holding it.  An array,
 * the last entry moved into a freed one's place: a few hundred entries. */
typedef struct VkrAllocRec {
    uint64_t memory, object;
    uint64_t size;
    uint64_t base, span; /* GPU address range, 0: not reported */
    char name[32];
} VkrAllocRec;

static VkrAllocRec *s_allocs;
static uint32_t s_allocCount, s_allocCap;

/* a binding reported for a VkDeviceMemory before vkr_alloc_note had it
 * (drivers that report at vkAllocateMemory, inside vkr_allocate): taken by
 * the note that follows */
static struct {
    uint64_t handle, base, size;
} s_allocEarly;

void vkr_alloc_note(VkDeviceMemory memory, uint64_t object, VkDeviceSize size, const char *name)
{
    if (s_allocCount == s_allocCap) {
        const uint32_t cap = s_allocCap ? s_allocCap * 2u : 512u;
        VkrAllocRec *a = realloc(s_allocs, (size_t)cap * sizeof(*a));
        if (!a) {
            return; /* the table is diagnostics only: this one goes unnamed */
        }
        s_allocs = a;
        s_allocCap = cap;
    }
    VkrAllocRec *e = &s_allocs[s_allocCount++];
    memset(e, 0, sizeof(*e));
    e->memory = (uint64_t)memory;
    e->object = object;
    e->size = size;
    snprintf(e->name, sizeof(e->name), "%s", name ? name : "?");
    if (s_allocEarly.handle && s_allocEarly.handle == e->memory) {
        e->base = s_allocEarly.base;
        e->span = s_allocEarly.size;
        memset(&s_allocEarly, 0, sizeof(s_allocEarly));
    }
}

void vkr_alloc_forget(VkDeviceMemory memory)
{
    for (uint32_t i = 0; i < s_allocCount; i++) {
        if (s_allocs[i].memory == (uint64_t)memory) {
            s_allocs[i] = s_allocs[--s_allocCount];
            return;
        }
    }
}

void vkr_alloc_bound(uint64_t object, uint64_t base, uint64_t size, bool bound)
{
    /* the report names the buffer or image (at its bind), or the memory
     * (drivers that report at allocation) */
    for (uint32_t i = 0; i < s_allocCount; i++) {
        if (s_allocs[i].object == object || s_allocs[i].memory == object) {
            s_allocs[i].base = bound ? base : 0;
            s_allocs[i].span = bound ? size : 0;
            return;
        }
    }
    if (bound) {
        s_allocEarly.handle = object;
        s_allocEarly.base = base;
        s_allocEarly.size = size;
    }
}

void vkr_alloc_nearest(uint64_t address)
{
    const VkrAllocRec *in = NULL, *below = NULL;
    uint32_t known = 0;
    for (uint32_t i = 0; i < s_allocCount; i++) {
        const VkrAllocRec *e = &s_allocs[i];
        if (!e->span) {
            continue;
        }
        known++;
        if (address >= e->base && address - e->base < e->span) {
            in = e;
        } else if (e->base <= address && (!below || e->base > below->base)) {
            below = e;
        }
    }
    if (in) {
        VKR_LOG("  0x%016llx is in \"%s\" (%.0f KB at 0x%016llx, offset %llu)",
                (unsigned long long)address, in->name, (double)in->span / 1024.0,
                (unsigned long long)in->base, (unsigned long long)(address - in->base));
    } else if (below) {
        VKR_LOG("  0x%016llx is past every resource; the nearest below is \"%s\" (%.0f KB at "
                "0x%016llx, %llu bytes past its end)",
                (unsigned long long)address, below->name, (double)below->span / 1024.0,
                (unsigned long long)below->base,
                (unsigned long long)(address - below->base - below->span));
    } else {
        VKR_LOG("  0x%016llx: no resource address known (%u of %u allocations reported; the "
                "addresses come with gpu_checkpoints and the driver's binding report)",
                (unsigned long long)address, known, s_allocCount);
    }
}

void vkr_alloc_table_free(void)
{
    free(s_allocs);
    s_allocs = NULL;
    s_allocCount = s_allocCap = 0;
    memset(&s_allocEarly, 0, sizeof(s_allocEarly));
}

/* every vkFreeMemory, so the live counts stay right */
void vkr_free_memory(VkDeviceMemory memory, VkDeviceSize size)
{
    if (!memory) {
        return;
    }
    vkr_alloc_forget(memory);
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

/* One VkDeviceMemory of a kind: the memory types vkr_memory_candidates
   lists (vk_memory.c), each tried in turn while the driver refuses
   (VK_ERROR_OUT_OF_DEVICE_MEMORY: a full heap), so a full PCIe BAR window
   sends uploads to system memory instead of failing them.  A window that
   refused is tried last from then on (memTypeFull); the first fallback of
   each kind is logged.  *coherent: whether the type taken is
   host-coherent. */
static bool vkr_allocate(const VkMemoryRequirements *req, RhiMemory kind, VkDeviceMemory *out,
                         bool *coherent, uint64_t object, const char *name)
{
    *out = VK_NULL_HANDLE;
    if (!vkr_memory_room(req->size)) {
        return false;
    }
    uint32_t cand[VK_MAX_MEMORY_TYPES];
    const uint32_t n = vkr_memory_candidates(kind, req->memoryTypeBits, req->size, &g_vkr.memProps,
                                             g_vkr.memTypeFull, cand);
    if (n == 0) {
        VKR_LOG("no memory type for kind %d (bits 0x%x)", (int)kind, req->memoryTypeBits);
        return false;
    }
    VkResult r = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    for (uint32_t c = 0; c < n; c++) {
        const uint32_t type = cand[c];
        VkMemoryAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req->size,
            .memoryTypeIndex = type,
        };
        r = vkAllocateMemory(g_vkr.device, &ai, NULL, out);
        if (r == VK_SUCCESS) {
            if (c > 0 && (unsigned)kind < 3u && !g_vkr.memFallbackLogged[kind]) {
                const uint32_t refused = cand[0];
                const uint32_t heap = g_vkr.memProps.memoryTypes[refused].heapIndex;
                const unsigned mb = (unsigned)(g_vkr.memProps.memoryHeaps[heap].size >> 20);
                g_vkr.memFallbackLogged[kind] = true;
                if (kind == RHI_MEM_UPLOAD && vkr_memory_small_window(&g_vkr.memProps, refused)) {
                    VKR_LOG("the device-local host-visible heap is full (%u MB): uploads go "
                            "through system memory from now on",
                            mb);
                } else {
                    VKR_LOG("memory type %u (heap %u, %u MB) refused a %.0f KB allocation "
                            "(VkResult %d): memory type %u (heap %u) took it",
                            refused, heap, mb, (double)req->size / 1024.0, (int)r, type,
                            g_vkr.memProps.memoryTypes[type].heapIndex);
                }
            }
            if (c > 0) {
                g_vkr.stats.memoryFallbacks++;
            }
            if (coherent) {
                *coherent = (g_vkr.memProps.memoryTypes[type].propertyFlags &
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
            }
            vkr_memory_counted(req->size);
            vkr_alloc_note(*out, object, req->size, name);
            return true;
        }
        *out = VK_NULL_HANDLE;
        if (r == VK_ERROR_DEVICE_LOST) {
            break;
        }
        if (vkr_memory_small_window(&g_vkr.memProps, type)) {
            g_vkr.memTypeFull |= 1u << type;
        }
    }
    vkr_check(r, "vkAllocateMemory", __FILE__, __LINE__); /* the log line, device lost */
    VKR_LOG("a %.0f KB device memory allocation failed in %u memory type%s with %u alive "
            "(%.0f MB)",
            (double)req->size / 1024.0, n, n == 1 ? "" : "s", g_vkr.memLive,
            (double)g_vkr.memLiveBytes / 1048576.0);
    return false;
}

static void vkr_set_name(VkObjectType type, uint64_t handle, const char *name)
{
    if (!g_vkr.debugUtils || !name || !VKR_FUNCTION_AVAILABLE(vkSetDebugUtilsObjectNameEXT)) {
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
    case VKR_GARBAGE_FRAMEBUFFER:
        vkDestroyFramebuffer(d, (VkFramebuffer)h, NULL);
        break;
    case VKR_GARBAGE_RENDERPASS:
        vkDestroyRenderPass(d, (VkRenderPass)h, NULL);
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

static RhiBuffer createBuffer(const RhiBufferDesc *desc);

/* timed into RhiStats.createNs */
RhiBuffer rhi_create_buffer(const RhiBufferDesc *desc)
{
    const uint64_t t0 = vkr_now_ns();
    const RhiBuffer b = createBuffer(desc);
    g_vkr.stats.createNs += vkr_now_ns() - t0;
    return b;
}

static RhiBuffer createBuffer(const RhiBufferDesc *desc)
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
    if (!vkr_allocate(&req, desc->memory, &b->memory, &b->coherent, VKR_H(b->buffer),
                      desc->debugName) ||
        !VKR_CHECK(vkBindBufferMemory(g_vkr.device, b->buffer, b->memory, 0))) {
        vkDestroyBuffer(g_vkr.device, b->buffer, NULL);
        vkr_free_memory(b->memory, req.size);
        vkr_pool_release(&g_vkr.buffers, id);
        return out;
    }
    b->memSize = req.size;
    b->size = desc->size;
    b->kind = desc->memory;
    /* a device buffer a shader reads: a texture copied into it is made
     * visible to those reads (vk_cmd.c rhi_cmd_copy_texture_to_buffer) */
    const uint32_t reads = RHI_BUF_VERTEX | RHI_BUF_INDEX | RHI_BUF_UNIFORM | RHI_BUF_STORAGE_READ;
    b->shaderRead = desc->memory == RHI_MEM_DEVICE && (desc->usage & reads) != 0;
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

/* rhi.h: the GPU reads the mapped memory itself, so there is nothing to
 * write at a submit (Direct3D 11 uses the mark) */
void rhi_buffer_note_written(RhiBuffer h, uint64_t hi)
{
    (void)h;
    (void)hi;
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

/* The RGBA8_UINT view of an RHI_TEX_UINT_VIEW image (made MUTABLE_FORMAT
 * with the two formats listed), sampled only: the view's usage is
 * restricted to SAMPLED (Vulkan 1.1 VkImageViewUsageCreateInfo), so the
 * image's attachment and copy usages, which the UNORM format carries, are
 * not asked of the UINT one */
static bool vkr_create_uint_view(VkImage image, uint32_t mips, VkImageView *out)
{
    const VkImageViewUsageCreateInfo use = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = &use,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UINT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1},
    };
    return VKR_CHECK(vkCreateImageView(g_vkr.device, &vi, NULL, out));
}

/* vkr_test_fail_texels_above: 0 is off */
static uint64_t s_failTexelsAbove;

void vkr_test_fail_texels_above(uint64_t texels)
{
    s_failTexelsAbove = texels;
}

static RhiTexture createTexture(const RhiTextureDesc *desc);

/* timed into RhiStats.createNs */
RhiTexture rhi_create_texture(const RhiTextureDesc *desc)
{
    const uint64_t t0 = vkr_now_ns();
    const RhiTexture t = createTexture(desc);
    g_vkr.stats.createNs += vkr_now_ns() - t0;
    return t;
}

static RhiTexture createTexture(const RhiTextureDesc *desc)
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
    /* the destination read (RhiDstRead): asked for only where the device
     * has the path (RhiLimits), ignored elsewhere */
    if ((desc->usage & RHI_TEX_STORAGE) && g_vkr.interlock && desc->format == RHI_FMT_RGBA8_UNORM &&
        desc->samples <= 1u) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    /* an attachment its pass reads as an input attachment: the colour
     * (the exact blend's attachment read, DATE's stencil setup) or the
     * depth (the fog's local read), wherever the device has local read
     * (the attachment read implies it) */
    if ((desc->usage & RHI_TEX_INPUT_ATTACHMENT) && g_vkr.localRead &&
        (desc->usage & (RHI_TEX_RENDER_TARGET | RHI_TEX_DEPTH_STENCIL))) {
        usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    }
    /* rhi_readback_texture works on any texture */
    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkrTexture *t = NULL;
    uint32_t id = vkr_pool_alloc(&g_vkr.textures, (void **)&t);
    if (!id) {
        return out;
    }
    uint32_t mips = desc->mipLevels ? desc->mipLevels : 1u;
    /* multisampled: a count the device takes (RhiLimits.msaaCounts), one
     * mip, an attachment */
    const uint32_t samples = desc->samples > 1u ? desc->samples : 1u;
    if (samples > 1u &&
        ((g_vkr.msaaCounts & samples) == 0u || (samples & (samples - 1u)) != 0u || mips != 1u ||
         !(desc->usage & (RHI_TEX_RENDER_TARGET | RHI_TEX_DEPTH_STENCIL)))) {
        VKR_LOG("rhi_create_texture %s: %u samples not taken",
                desc->debugName ? desc->debugName : "", samples);
        vkr_pool_release(&g_vkr.textures, id);
        return out;
    }
    /* RHI_TEX_UINT_VIEW: an RGBA8_UNORM image also viewed as RGBA8_UINT
     * (the same texel class), the two formats listed so the driver can keep
     * its compression for exactly those (Vulkan 1.2 VkImageFormatListCreateInfo,
     * VK_KHR_image_format_list on a 1.1 device; without either the image is
     * only mutable, g_vkr.formatList, vkr_create_device) */
    static const VkFormat uintPair[2] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UINT};
    const bool uintView =
        (desc->usage & RHI_TEX_UINT_VIEW) && desc->format == RHI_FMT_RGBA8_UNORM &&
        vkFmt == VK_FORMAT_R8G8B8A8_UNORM && samples == 1u && (desc->usage & RHI_TEX_SAMPLED);
    const VkImageFormatListCreateInfo formatList = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = 2,
        .pViewFormats = uintPair,
    };
    VkImageCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = uintView && g_vkr.formatList ? &formatList : NULL,
        .flags = uintView ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0u,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vkFmt,
        .extent = {desc->width, desc->height, 1},
        .mipLevels = mips,
        .arrayLayers = 1,
        .samples = (VkSampleCountFlagBits)samples,
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
    if (!vkr_allocate(&req, RHI_MEM_DEVICE, &t->memory, NULL, VKR_H(t->image), desc->debugName) ||
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
    t->samples = samples;
    /* copy-only textures (staging, R8 copy targets) have no view: a view
     * needs a sampled or attachment usage */
    if ((desc->usage & (RHI_TEX_SAMPLED | RHI_TEX_RENDER_TARGET | RHI_TEX_DEPTH_STENCIL)) &&
        !vkr_create_view(t->image, vkFmt, fm->aspect, mips, &t->view)) {
        goto fail;
    }
    if ((fm->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) &&
        ((desc->usage & RHI_TEX_SAMPLED) || (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))) {
        /* a depth-stencil view cannot be sampled, nor read as an input
         * attachment (one aspect a descriptor): sampling and the fog's
         * local read see depth only */
        if (!vkr_create_view(t->image, vkFmt, VK_IMAGE_ASPECT_DEPTH_BIT, mips, &t->depthView)) {
            goto fail;
        }
    } else {
        t->depthView = VK_NULL_HANDLE;
    }
    /* the stencil read (RHI_ASPECT_STENCIL: the shadow count's one draw),
     * a sampled single-sampled depth-stencil texture's: its 8-bit value
     * as an unsigned integer in red.  Optional: a driver that refuses the
     * view (MoltenVK's X32_Stencil8 view, unverified) keeps the texture,
     * and RhiLimits.stencilView goes false for the device (logged once),
     * which turns the one draw off (rd_shadow_one.c: the seven draws
     * resolve the count; the stencil aspect is then never bound, so the
     * all-aspects view is never sampled) */
    t->stencilView = VK_NULL_HANDLE;
    if ((fm->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) && (desc->usage & RHI_TEX_SAMPLED) &&
        samples == 1u && g_vkr.limits.stencilView &&
        !vkr_create_view(t->image, vkFmt, VK_IMAGE_ASPECT_STENCIL_BIT, mips, &t->stencilView)) {
        t->stencilView = VK_NULL_HANDLE;
        g_vkr.limits.stencilView = false;
        VKR_LOG("texture %s: the stencil view was not made: no stencil views on this device "
                "from now (the shadow count takes the seven draws)",
                desc->debugName ? desc->debugName : "?");
    }
    t->uintView = VK_NULL_HANDLE;
    if (uintView && !vkr_create_uint_view(t->image, mips, &t->uintView)) {
        goto fail;
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
    if (t->depthView) {
        vkDestroyImageView(g_vkr.device, t->depthView, NULL);
    }
    if (t->stencilView) {
        vkDestroyImageView(g_vkr.device, t->stencilView, NULL);
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
    /* the VkRenderPass path's framebuffers on the view go first (a view
     * made later may get the same handle) */
    vkr_framebuffer_forget_view(t->view, false);
    vkr_defer(VKR_GARBAGE_VIEW, VKR_H(t->view));
    vkr_defer(VKR_GARBAGE_VIEW, VKR_H(t->depthView));
    vkr_defer(VKR_GARBAGE_VIEW, VKR_H(t->stencilView));
    vkr_defer(VKR_GARBAGE_VIEW, VKR_H(t->uintView));
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
    t->samples = 1;
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
    /* called after vkDeviceWaitIdle: destroy now, the framebuffers on the
     * view first */
    vkr_framebuffer_forget_view(t->view, true);
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
    /* the cached framebuffers and render passes, before the views */
    vkr_renderpass_shutdown();
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
            if (t->stencilView) {
                vkDestroyImageView(d, t->stencilView, NULL);
            }
            /* the RGBA8_UINT view (RHI_TEX_UINT_VIEW), as rhi_destroy_texture
             * frees it */
            if (t->uintView) {
                vkDestroyImageView(d, t->uintView, NULL);
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
