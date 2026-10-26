/* vk_pipeline.c: bind group layouts, transient bind groups and graphics
 * pipelines for the Vulkan backend. */
#include "vk_internal.h"
#include "rhi_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN

#include <io.h>
#include <windows.h>

#else

#include <dirent.h>
#include <errno.h>
#include <unistd.h>

#endif
#ifdef ICO_RHI_HAVE_SDL
#include <SDL3/SDL.h>
#endif

#define VKR_H(x) ((uint64_t)(x))

/* ------------------------------------------------------ the pipeline cache
 * rhi_set_pipeline_cache_path (rhi.h).  Kept across rhi_init, which clears
 * g_vkr. */
static char s_cachePath[1024];

void rhi_set_pipeline_cache_path(const char *path)
{
    s_cachePath[0] = '\0';
    if (path && strlen(path) + 5 < sizeof(s_cachePath)) {
        strcpy(s_cachePath, path);
    }
}

/* ------------------------------------------------ the other builds' caches
 * The window names the file pipelines-<build>.vkcache, so a cache written by
 * another build (its shader set differs; the driver would be handed the stale
 * blob and every new pipeline) is never read.  This removes them. */
static const char *vkr_base_name(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            b = p + 1;
        }
    }
    return b;
}

static bool vkr_ends_with(const char *s, const char *tail)
{
    const size_t n = strlen(s), t = strlen(tail);
    return n >= t && strcmp(s + n - t, tail) == 0;
}

/* a cache file of some build: pipelines*.vkcache, or its .tmp */
static bool vkr_is_cache_name(const char *name)
{
    return strncmp(name, "pipelines", 9) == 0 &&
           (vkr_ends_with(name, ".vkcache") || vkr_ends_with(name, ".vkcache.tmp"));
}

uint32_t vkr_cache_drop_others(const char *path, char *log, size_t logCap)
{
    uint32_t dropped = 0;
    char dir[1024];
    const char *cur = path ? vkr_base_name(path) : "";
    if (log && logCap) {
        log[0] = '\0';
    }
    /* only the pipelines-<build>.vkcache the window names: any other file
       name (a test's) is left alone along with its neighbours */
    if (!path || strncmp(cur, "pipelines", 9) != 0 || !vkr_ends_with(cur, ".vkcache") ||
        (size_t)(cur - path) >= sizeof(dir)) {
        return 0;
    }
    memcpy(dir, path, (size_t)(cur - path));
    dir[cur - path] = '\0';
    if (dir[0] == '\0') {
        strcpy(dir, ".");
    }
    char tmpName[sizeof(dir)];
    snprintf(tmpName, sizeof(tmpName), "%s.tmp", cur);

    /* names first, removals after: a removal while listing may skip entries */
    enum { MAXN = 64 };

    static char names[MAXN][256];
    uint32_t n = 0;
#ifdef _WIN32
    char pat[sizeof(dir) + 16];
    WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof(pat), "%.*spipelines*", (int)(cur - path), path);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
            VKR_LOG("pipeline cache: could not list %s; no other cache dropped", dir);
        }
        return 0;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && vkr_is_cache_name(fd.cFileName) &&
            strcmp(fd.cFileName, cur) != 0 && strcmp(fd.cFileName, tmpName) != 0 && n < MAXN &&
            strlen(fd.cFileName) < sizeof(names[0])) {
            strcpy(names[n++], fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) {
        if (errno != ENOENT) {
            VKR_LOG("pipeline cache: could not list %s; no other cache dropped", dir);
        }
        return 0;
    }
    for (struct dirent *e; (e = readdir(d)) != NULL;) {
        if (vkr_is_cache_name(e->d_name) && strcmp(e->d_name, cur) != 0 &&
            strcmp(e->d_name, tmpName) != 0 && n < MAXN && strlen(e->d_name) < sizeof(names[0])) {
            strcpy(names[n++], e->d_name);
        }
    }
    closedir(d);
#endif
    for (uint32_t i = 0; i < n; i++) {
        char full[sizeof(dir) + sizeof(names[0])];
        const size_t plen = (size_t)(cur - path);
        memcpy(full, path, plen);
        strcpy(full + plen, names[i]);
        if (remove(full) == 0) {
            VKR_LOG("pipeline cache: dropped %s (another version of the game); this start "
                    "prepares the graphics from nothing",
                    names[i]);
            if (log && logCap > strlen(log) + strlen(names[i]) + 2) {
                strcat(log, names[i]);
                strcat(log, "\n");
            }
            dropped++;
        } else {
            VKR_LOG("pipeline cache: could not drop %s", names[i]);
        }
    }
    return dropped;
}

/* the file's blob when its header (VkPipelineCacheHeaderVersionOne: header
 * size, version, vendor id, device id, UUID) names this device; NULL else */
static void *vkr_cache_load(size_t *size)
{
    *size = 0;
    FILE *fp = s_cachePath[0] ? fopen(s_cachePath, "rb") : NULL;
    if (!fp) {
        return NULL;
    }
    void *blob = NULL;
    long n = -1;
    if (fseek(fp, 0, SEEK_END) == 0) {
        n = ftell(fp);
    }
    if (n >= 32 && n <= (64l << 20) && fseek(fp, 0, SEEK_SET) == 0) {
        blob = malloc((size_t)n);
        if (blob && fread(blob, 1, (size_t)n, fp) != (size_t)n) {
            free(blob);
            blob = NULL;
        }
    }
    fclose(fp);
    if (!blob) {
        return NULL;
    }
    uint32_t h[4];
    memcpy(h, blob, sizeof(h));
    const uint8_t *uuid = (const uint8_t *)blob + 16;
    if (h[0] < 32 || h[0] > (uint32_t)n || h[1] != VK_PIPELINE_CACHE_HEADER_VERSION_ONE ||
        h[2] != g_vkr.props.vendorID || h[3] != g_vkr.props.deviceID ||
        memcmp(uuid, g_vkr.props.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
        VKR_LOG("pipeline cache %s is for another device or driver: starting empty", s_cachePath);
        free(blob);
        return NULL;
    }
    *size = (size_t)n;
    return blob;
}

/* rhi_pipeline_cache_loaded: the cache was created from the file's blob */
static bool s_cacheLoaded;
/* the size of the data the file holds as this run read or wrote it, 0
 * none (rhi_pipeline_cache_save skips a write of the same size) */
static size_t s_cacheFileSize;

bool rhi_pipeline_cache_loaded(void)
{
    return s_cacheLoaded;
}

void vkr_pipeline_cache_init(void)
{
    size_t size = 0;
    s_cacheLoaded = false;
    s_cacheFileSize = 0;
    vkr_cache_drop_others(s_cachePath, NULL, 0);
    void *blob = vkr_cache_load(&size);
    VkPipelineCacheCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = size,
        .pInitialData = blob,
    };
    if (vkCreatePipelineCache(g_vkr.device, &ci, NULL, &g_vkr.pipelineCache) != VK_SUCCESS) {
        g_vkr.pipelineCache = VK_NULL_HANDLE;
        if (blob) { /* the driver refused the data: start empty */
            ci.initialDataSize = 0;
            ci.pInitialData = NULL;
            if (vkCreatePipelineCache(g_vkr.device, &ci, NULL, &g_vkr.pipelineCache) !=
                VK_SUCCESS) {
                g_vkr.pipelineCache = VK_NULL_HANDLE;
            }
        }
    } else if (blob) {
        s_cacheLoaded = true;
        s_cacheFileSize = size;
        VKR_LOG("pipeline cache: %zu bytes from %s", size, s_cachePath);
    } else if (s_cachePath[0]) {
        /* the size at the start either way (issues 93 and 107: a start the
           system ended keeps what the last write saved) */
        VKR_LOG("pipeline cache: starting empty (%s holds none for this device)", s_cachePath);
    }
    free(blob);
}

/* fflush, then the file's bytes onto the disk (_commit on Windows, fsync
 * elsewhere; port/platform/host_fs.c ico_fsync, which port/rhi does not
 * link) */
static bool vkr_file_sync(FILE *fp)
{
    if (fflush(fp) != 0) {
        return false;
    }
#ifdef _WIN32
    return _commit(_fileno(fp)) == 0;
#else
    return fsync(fileno(fp)) == 0;
#endif
}

/* from renamed over to, replacing it in one step (MoveFileExA with
 * MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH on Windows, where
 * rename does not replace a file; rename(2) elsewhere) */
static bool vkr_rename_over(const char *from, const char *to)
{
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(from, to) == 0;
#endif
}

/* the cache's data to <path>.tmp, renamed over the file; its size in
 * bytes, 0 when nothing was written.  skipSame: not when the data's size
 * is the file's as this run read or wrote it (*same set when that skipped
 * the write).  sync: the bytes onto the disk before the move (the writes
 * during a run, which a device that freezes or a start the system ends
 * may follow); the exit's write leaves that to the system */
static size_t vkr_cache_write(bool skipSame, bool *same, bool sync)
{
    size_t size = 0;
    void *blob = NULL;
    if (same) {
        *same = false;
    }
    if (!g_vkr.pipelineCache || !s_cachePath[0] || g_vkr.deviceLost) {
        return 0;
    }
    if (vkGetPipelineCacheData(g_vkr.device, g_vkr.pipelineCache, &size, NULL) == VK_SUCCESS &&
        skipSame && size == s_cacheFileSize) {
        if (same) {
            *same = true;
        }
        return 0;
    }
    if (size >= 32) {
        blob = malloc(size);
        if (blob &&
            vkGetPipelineCacheData(g_vkr.device, g_vkr.pipelineCache, &size, blob) != VK_SUCCESS) {
            free(blob);
            blob = NULL;
        }
    }
    if (!blob) {
        return 0;
    }
    char tmp[sizeof(s_cachePath) + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", s_cachePath);
    FILE *fp = fopen(tmp, "wb");
    bool ok = fp && fwrite(blob, 1, size, fp) == size;
    if (fp) {
        /* the bytes on the disk before the move, and the move over the old
         * file (never a remove first): a device that freezes or a start the
         * system ends mid-write (issues 93 and 107) leaves the old cache or
         * the new one, never an empty or missing file */
        ok = ok && (!sync || vkr_file_sync(fp));
        ok = fclose(fp) == 0 && ok;
    }
    free(blob);
    if (!ok || !vkr_rename_over(tmp, s_cachePath)) {
        remove(tmp);
        VKR_LOG("pipeline cache: could not write %s", s_cachePath);
        return 0;
    }
    s_cacheFileSize = size;
    return size;
}

bool rhi_pipeline_cache_save(void)
{
    const uint64_t t0 = vkr_now_ns();
    bool same = false;
    const size_t size = vkr_cache_write(true, &same, true);
    if (size) {
        VKR_LOG("pipeline cache: %zu bytes written to %s in %.1f ms", size, s_cachePath,
                (double)(vkr_now_ns() - t0) / 1e6);
    } else if (same) {
        VKR_LOG("pipeline cache: the file already holds its %zu bytes; not written",
                s_cacheFileSize);
    }
    return size != 0 || same;
}

uint64_t rhi_pipeline_cache_size(void)
{
    size_t size = 0;
    if (!g_vkr.pipelineCache || g_vkr.deviceLost ||
        vkGetPipelineCacheData(g_vkr.device, g_vkr.pipelineCache, &size, NULL) != VK_SUCCESS) {
        return 0;
    }
    return (uint64_t)size;
}

void vkr_pipeline_cache_shutdown(void)
{
    if (!g_vkr.pipelineCache) {
        return;
    }
    /* the exit's write: not when the data's size is the file's as this run
     * read or wrote it (a run that made no new pipeline; 21 MB on an RTX
     * 3090, seconds on a USB stick), and not synced */
    const uint64_t t0 = vkr_now_ns();
    bool same = false;
    const size_t size = vkr_cache_write(true, &same, false);
    if (same) {
        VKR_LOG("pipeline cache unchanged, not written (%zu bytes in %s)", s_cacheFileSize,
                s_cachePath);
    } else if (size) {
        VKR_LOG("pipeline cache: %zu bytes written to %s at exit in %.1f ms", size, s_cachePath,
                (double)(vkr_now_ns() - t0) / 1e6);
    }
    vkDestroyPipelineCache(g_vkr.device, g_vkr.pipelineCache, NULL);
    g_vkr.pipelineCache = VK_NULL_HANDLE;
}

static VkShaderStageFlags vkr_stages(uint32_t rhiStages)
{
    VkShaderStageFlags f = 0;
    if (rhiStages & (1u << RHI_STAGE_VERTEX)) {
        f |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (rhiStages & (1u << RHI_STAGE_FRAGMENT)) {
        f |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    return f ? f : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
}

/* ----------------------------------------------------- bind group layouts */
RhiBindGroupLayout rhi_create_bind_group_layout(const RhiBindGroupLayoutDesc *desc)
{
    RhiBindGroupLayout out = {0};
    if (!desc || desc->slotCount > 16) {
        return out;
    }
    VkrLayout *l = NULL;
    uint32_t id = vkr_pool_alloc(&g_vkr.layouts, (void **)&l);
    if (!id) {
        return out;
    }
    VkDescriptorSetLayoutBinding b[16];
    for (uint32_t i = 0; i < desc->slotCount; i++) {
        const RhiBindSlot *s = &desc->slots[i];
        if (s->type >= RHI_BIND_COUNT) {
            vkr_pool_release(&g_vkr.layouts, id);
            return out;
        }
        b[i] = (VkDescriptorSetLayoutBinding){
            .binding = s->slot + vkr_bindTypeMap[s->type].shift,
            .descriptorType = vkr_bindTypeMap[s->type].vk,
            .descriptorCount = 1,
            .stageFlags = vkr_stages(s->stages),
        };
        l->slots[i] = *s;
        if (s->type == RHI_BIND_UNIFORM_BUFFER_DYNAMIC) {
            l->dynamicCount++;
        }
    }
    if (l->dynamicCount > RHI_MAX_DYNAMIC_OFFSETS) {
        VKR_LOG("bind group layout %s: %u dynamic uniforms (at most %d)",
                desc->debugName ? desc->debugName : "?", l->dynamicCount, RHI_MAX_DYNAMIC_OFFSETS);
        vkr_pool_release(&g_vkr.layouts, id);
        return out;
    }
    l->slotCount = desc->slotCount;
    VkDescriptorSetLayoutCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = desc->slotCount,
        .pBindings = b,
    };
    if (!VKR_CHECK(vkCreateDescriptorSetLayout(g_vkr.device, &ci, NULL, &l->layout))) {
        vkr_pool_release(&g_vkr.layouts, id);
        return out;
    }
    out.id = id;
    return out;
}

void rhi_destroy_bind_group_layout(RhiBindGroupLayout h)
{
    VkrLayout *l = vkr_pool_get(&g_vkr.layouts, h.id);
    if (!l) {
        return;
    }
    vkr_defer(VKR_GARBAGE_SET_LAYOUT, VKR_H(l->layout));
    vkr_pool_release(&g_vkr.layouts, h.id);
}

/* ---------------------------------------------------- transient bind groups
 * Allocated from the frame slot's descriptor pools, which are reset as a
 * whole when the slot is reused (rhi_wait_frame).  The handle id is
 * (frame tag << 20) | (index + 1); a handle from another frame fails the
 * lookup. */
static bool vkr_new_descriptor_pool(VkrFrame *f)
{
    if (f->descPoolCount == VKR_DESC_POOLS_MAX) {
        VKR_LOG("descriptor pools exhausted for this frame");
        return false;
    }
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192},
        /* rd_core makes a few dynamic groups a frame */
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8192},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 8192},
        /* the destination read's group: the target, one a pass */
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1024},
    };
    VkDescriptorPoolCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 8192,
        .poolSizeCount = (uint32_t)(sizeof(sizes) / sizeof(sizes[0])),
        .pPoolSizes = sizes,
    };
    if (!VKR_CHECK(
            vkCreateDescriptorPool(g_vkr.device, &ci, NULL, &f->descPools[f->descPoolCount]))) {
        return false;
    }
    f->descPoolCount++;
    return true;
}

static uint32_t vkr_frame_tag(void)
{
    return (uint32_t)(g_vkr.frameIndex & VKR_GEN_MASK);
}

VkDescriptorSet vkr_get_bind_group(RhiBindGroup bg)
{
    VkrFrame *f = vkr_cur_frame();
    uint32_t idx = bg.id & VKR_INDEX_MASK;
    if (idx == 0 || idx > f->setCount || (bg.id >> VKR_GEN_SHIFT) != vkr_frame_tag()) {
        return VK_NULL_HANDLE;
    }
    return f->sets[idx - 1];
}

uint32_t vkr_bind_group_dynamic_count(RhiBindGroup bg)
{
    VkrFrame *f = vkr_cur_frame();
    uint32_t idx = bg.id & VKR_INDEX_MASK;
    if (idx == 0 || idx > f->setCount || (bg.id >> VKR_GEN_SHIFT) != vkr_frame_tag()) {
        return 0;
    }
    return f->setDynamic[idx - 1];
}

static VkImageLayout vkr_sampled_layout(const VkrTexture *t)
{
    return vkr_state_layout(RHI_STATE_SHADER_READ, t->rhiFormat);
}

RhiBindGroup rhi_create_bind_group(const RhiBindGroupDesc *desc)
{
    RhiBindGroup out = {0};
    VkrLayout *l = desc ? vkr_pool_get(&g_vkr.layouts, desc->layout.id) : NULL;
    if (!l || desc->bindingCount > 16) {
        return out;
    }
    VkrFrame *f = vkr_cur_frame();
    if (f->setCount == f->setCap) {
        uint32_t cap = f->setCap ? f->setCap * 2u : 1024u;
        VkDescriptorSet *s = realloc(f->sets, cap * sizeof(*s));
        if (!s) {
            return out;
        }
        f->sets = s;
        uint8_t *d = realloc(f->setDynamic, cap);
        if (!d) {
            return out;
        }
        f->setDynamic = d;
        f->setCap = cap;
    }
    if (f->setCount >= VKR_INDEX_MASK) {
        return out;
    }
    if (f->descPoolCount == 0 && !vkr_new_descriptor_pool(f)) {
        return out;
    }
    VkDescriptorSet set = VK_NULL_HANDLE;
    for (;;) {
        VkDescriptorSetAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = f->descPools[f->descPoolCur],
            .descriptorSetCount = 1,
            .pSetLayouts = &l->layout,
        };
        VkResult r = vkAllocateDescriptorSets(g_vkr.device, &ai, &set);
        if (r == VK_SUCCESS) {
            break;
        }
        if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL) {
            VKR_CHECK(r);
            return out;
        }
        if (f->descPoolCur + 1 < f->descPoolCount) {
            f->descPoolCur++;
        } else if (vkr_new_descriptor_pool(f)) {
            f->descPoolCur = f->descPoolCount - 1;
        } else {
            return out;
        }
    }

    VkWriteDescriptorSet w[16];
    VkDescriptorBufferInfo bi[16];
    VkDescriptorImageInfo ii[16];
    uint32_t n = 0;
    for (uint32_t i = 0; i < desc->bindingCount; i++) {
        const RhiBinding *b = &desc->bindings[i];
        if (b->type >= RHI_BIND_COUNT) {
            continue;
        }
        w[n] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = b->slot + vkr_bindTypeMap[b->type].shift,
            .descriptorCount = 1,
            .descriptorType = vkr_bindTypeMap[b->type].vk,
        };
        switch (b->type) {
        case RHI_BIND_UNIFORM_BUFFER:
        case RHI_BIND_UNIFORM_BUFFER_DYNAMIC:
        case RHI_BIND_STORAGE_BUFFER: {
            VkrBuffer *buf = vkr_get_buffer(b->buffer);
            if (!buf) {
                VKR_LOG("bind group: slot %u: invalid buffer", b->slot);
                continue;
            }
            if (b->type == RHI_BIND_UNIFORM_BUFFER_DYNAMIC && !b->size) {
                /* the range is static and every dynamic offset adds to it */
                VKR_LOG("bind group: slot %u: a dynamic uniform needs a size", b->slot);
                continue;
            }
            bi[n] =
                (VkDescriptorBufferInfo){buf->buffer, b->offset, b->size ? b->size : VK_WHOLE_SIZE};
            w[n].pBufferInfo = &bi[n];
            break;
        }
        case RHI_BIND_SAMPLED_TEXTURE: {
            VkrTexture *t = vkr_get_texture(b->texture);
            if (!t) {
                VKR_LOG("bind group: slot %u: invalid texture", b->slot);
                continue;
            }
            /* RHI_ASPECT_UINT: the RGBA8_UINT view of an RHI_TEX_UINT_VIEW
             * texture; a texture without one keeps its own view (the
             * renderer asks only for textures made with the bit) */
            VkImageView v = (b->aspect == RHI_ASPECT_DEPTH && t->depthView) ? t->depthView
                            : (b->aspect == RHI_ASPECT_UINT && t->uintView) ? t->uintView
                                                                            : t->view;
            ii[n] = (VkDescriptorImageInfo){VK_NULL_HANDLE, v, vkr_sampled_layout(t)};
            w[n].pImageInfo = &ii[n];
            break;
        }
        case RHI_BIND_SAMPLER: {
            VkSampler *s = vkr_pool_get(&g_vkr.samplers, b->sampler.id);
            if (!s) {
                VKR_LOG("bind group: slot %u: invalid sampler", b->slot);
                continue;
            }
            ii[n] = (VkDescriptorImageInfo){*s, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
            w[n].pImageInfo = &ii[n];
            break;
        }
        case RHI_BIND_STORAGE_TEXTURE:
        case RHI_BIND_INPUT_ATTACHMENT: {
            /* the destination read: the target in RHI_STATE_STORAGE
             * (GENERAL) or RHI_STATE_RENDER_TARGET_READ (RENDERING_LOCAL_READ);
             * an input attachment is any of the pass's attachments under
             * local read (the exact blend's colour in rasterization order,
             * DATE's colour and the fog's depth after a barrier), a
             * depth-stencil one through its depth-only view (RHI_ASPECT_DEPTH) */
            VkrTexture *t = vkr_get_texture(b->texture);
            const bool storage = b->type == RHI_BIND_STORAGE_TEXTURE;
            const bool depthIn = !storage && b->aspect == RHI_ASPECT_DEPTH;
            const VkImageView v = t && depthIn ? (t->depthView ? t->depthView : VK_NULL_HANDLE)
                                  : t          ? t->view
                                               : VK_NULL_HANDLE;
            if (!t || !v || t->samples > 1u || (storage ? !g_vkr.interlock : !g_vkr.localRead)) {
                VKR_LOG("bind group: slot %u: invalid destination-read texture", b->slot);
                continue;
            }
            ii[n] = (VkDescriptorImageInfo){
                VK_NULL_HANDLE, v,
                vkr_state_layout(storage ? RHI_STATE_STORAGE : RHI_STATE_RENDER_TARGET_READ,
                                 t->rhiFormat)};
            w[n].pImageInfo = &ii[n];
            break;
        }
        default:
            continue;
        }
        n++;
    }
    if (n) {
        vkUpdateDescriptorSets(g_vkr.device, n, w, 0, NULL);
    }
    f->setDynamic[f->setCount] = (uint8_t)l->dynamicCount;
    f->sets[f->setCount++] = set;
    g_vkr.stats.bindGroups++;
    out.id = (vkr_frame_tag() << VKR_GEN_SHIFT) | f->setCount;
    return out;
}

/* -------------------------------------------------------------- pipelines */
/* b as Vulkan's attachment state; blending off on an integer target (the
 * pipeline's and rhi_cmd_set_blend_state's, vk_cmd.c) */
VkPipelineColorBlendAttachmentState vkr_blend_state(const RhiBlendState *b, bool intTarget)
{
    VkPipelineColorBlendAttachmentState s = {0};
    s.colorWriteMask = (VkColorComponentFlags)(b->writeMask & 0xFu);
    if (!b->enable || intTarget) {
        return s;
    }
    s.blendEnable = VK_TRUE;
    s.srcColorBlendFactor = vkr_blendFactorMap[b->srcColor].vk;
    s.dstColorBlendFactor = vkr_blendFactorMap[b->dstColor].vk;
    s.colorBlendOp = vkr_blendOpMap[b->colorOp].vk;
    s.srcAlphaBlendFactor = vkr_blendFactorMap[b->srcAlpha].vk;
    s.dstAlphaBlendFactor = vkr_blendFactorMap[b->dstAlpha].vk;
    s.alphaBlendOp = vkr_blendOpMap[b->alphaOp].vk;
    return s;
}

/* the dynamic blend values of attachment state a, as the pipeline's
 * defaults and rhi_cmd_set_blend_state make them (vk_cmd.c
 * vkr_flush_dynamic) */
void vkr_blend_dyn(const VkPipelineColorBlendAttachmentState *a, VkrBlendDyn *out)
{
    memset(out, 0, sizeof(*out));
    out->enable = a->blendEnable;
    out->eq.srcColorBlendFactor = a->srcColorBlendFactor;
    out->eq.dstColorBlendFactor = a->dstColorBlendFactor;
    out->eq.colorBlendOp = a->colorBlendOp;
    out->eq.srcAlphaBlendFactor = a->srcAlphaBlendFactor;
    out->eq.dstAlphaBlendFactor = a->dstAlphaBlendFactor;
    out->eq.alphaBlendOp = a->alphaBlendOp;
    out->mask = a->colorWriteMask;
}

static VkPipelineColorBlendAttachmentState vkr_blend(const RhiBlendState *b, RhiFormat fmt)
{
    if (b->enable && vkr_formatMap[fmt].isInteger) {
        VKR_LOG("pipeline: blending on an integer target is ignored");
    }
    return vkr_blend_state(b, vkr_formatMap[fmt].isInteger);
}

static VkStencilOpState vkr_stencil(const RhiStencilFace *f, const RhiDepthStencilState *ds)
{
    VkStencilOpState s = {
        .failOp = vkr_stencilOpMap[f->fail].vk,
        .passOp = vkr_stencilOpMap[f->pass].vk,
        .depthFailOp = vkr_stencilOpMap[f->depthFail].vk,
        .compareOp = vkr_compareMap[f->compare].vk,
        .compareMask = ds->stencilReadMask,
        .writeMask = ds->stencilWriteMask,
        .reference = 0, /* dynamic: rhi_cmd_set_stencil_ref */
    };
    return s;
}

static bool vkr_valid_pipeline_desc(const RhiPipelineDesc *d)
{
    if (d->topology >= RHI_TOPO_COUNT || d->colorCount > RHI_MAX_COLOR_TARGETS ||
        d->depthFormat >= RHI_FMT_COUNT || d->vertexAttrCount > RHI_MAX_VERTEX_ATTRS ||
        d->layoutCount > RHI_MAX_BIND_SLOTS) {
        return false;
    }
    for (uint32_t i = 0; i < d->colorCount; i++) {
        const RhiBlendState *b = &d->blend[i];
        if (d->colorFormats[i] <= RHI_FMT_UNKNOWN || d->colorFormats[i] >= RHI_FMT_COUNT ||
            b->srcColor >= RHI_BF_COUNT || b->dstColor >= RHI_BF_COUNT ||
            b->srcAlpha >= RHI_BF_COUNT || b->dstAlpha >= RHI_BF_COUNT ||
            b->colorOp >= RHI_BO_COUNT || b->alphaOp >= RHI_BO_COUNT) {
            return false;
        }
    }
    const RhiDepthStencilState *ds = &d->depthStencil;
    const RhiStencilFace *faces[2] = {&ds->front, &ds->back};
    for (int i = 0; i < 2; i++) {
        if (faces[i]->compare >= RHI_CMP_COUNT || faces[i]->pass >= RHI_SO_COUNT ||
            faces[i]->fail >= RHI_SO_COUNT || faces[i]->depthFail >= RHI_SO_COUNT) {
            return false;
        }
    }
    for (uint32_t i = 0; i < d->vertexAttrCount; i++) {
        if (d->vertexAttrs[i].format >= RHI_VTX_COUNT) {
            return false;
        }
    }
    /* the destination read needs its feature; the storage form has no
     * colour target (the shader writes the target) */
    if (d->dstRead >= RHI_DST_READ_COUNT ||
        (d->dstRead == RHI_DST_READ_STORAGE && (!g_vkr.interlock || d->colorCount != 0)) ||
        (d->dstRead == RHI_DST_READ_ATTACHMENT && (!g_vkr.rasterOrder || d->colorCount != 1))) {
        return false;
    }
    return ds->depthCompare < RHI_CMP_COUNT;
}

/* The pipeline within the device's limits (a phone GPU's are close to the
 * spec's minimums: four descriptor sets, 16 sampled images a stage;
 * ICO_VK_FAKE_LIMITS=mali sets them on any device), checked here so a
 * pipeline the device cannot take is a log line naming the limit and a
 * failed create (its draws are skipped, rd_pipeline.c) rather than invalid
 * use of the driver.  The renderer needs 3 sets, 4 dynamic uniforms and at
 * most 2 sampled images and 1 sampler a stage. */
static bool vkr_pipeline_fits(const RhiPipelineDesc *d)
{
    const VkPhysicalDeviceLimits *l = &g_vkr.props.limits;
    const char *name = d->debugName ? d->debugName : "?";
    /* per stage (vertex, fragment): uniform, storage, sampled image, sampler */
    uint32_t n[2][6] = {{0}};
    uint32_t dyn = 0, ubo = 0, ssbo = 0, img = 0, smp = 0, stoImg = 0, inAtt = 0;
    if (d->layoutCount > l->maxBoundDescriptorSets) {
        VKR_LOG("pipeline %s: %u descriptor sets, the device binds %u (maxBoundDescriptorSets)",
                name, d->layoutCount, l->maxBoundDescriptorSets);
        g_vkr.lastLimit = "maxBoundDescriptorSets";
        return false;
    }
    for (uint32_t i = 0; i < d->layoutCount; i++) {
        const VkrLayout *ly = vkr_pool_get(&g_vkr.layouts, d->layouts[i].id);
        if (!ly) {
            continue; /* rhi_create_pipeline reports it */
        }
        for (uint32_t j = 0; j < ly->slotCount; j++) {
            const RhiBindSlot *b = &ly->slots[j];
            const uint32_t st = b->stages ? b->stages : 3u;
            int k = 0;
            switch (b->type) {
            case RHI_BIND_UNIFORM_BUFFER_DYNAMIC:
                dyn++;
                /* fall through */
            case RHI_BIND_UNIFORM_BUFFER:
                ubo++;
                k = 0;
                break;
            case RHI_BIND_STORAGE_BUFFER:
                ssbo++;
                k = 1;
                break;
            case RHI_BIND_SAMPLED_TEXTURE:
                img++;
                k = 2;
                break;
            case RHI_BIND_SAMPLER:
                smp++;
                k = 3;
                break;
            case RHI_BIND_STORAGE_TEXTURE:
                stoImg++;
                k = 4;
                break;
            case RHI_BIND_INPUT_ATTACHMENT:
                inAtt++;
                k = 5;
                break;
            default:
                continue;
            }
            for (int s = 0; s < 2; s++) {
                if (st & (1u << s)) {
                    n[s][k]++;
                }
            }
        }
    }
    const uint32_t stageMax[6] = {
        l->maxPerStageDescriptorUniformBuffers, l->maxPerStageDescriptorStorageBuffers,
        l->maxPerStageDescriptorSampledImages,  l->maxPerStageDescriptorSamplers,
        l->maxPerStageDescriptorStorageImages,  l->maxPerStageDescriptorInputAttachments};
    static const char *const stageName[6] = {
        "maxPerStageDescriptorUniformBuffers", "maxPerStageDescriptorStorageBuffers",
        "maxPerStageDescriptorSampledImages",  "maxPerStageDescriptorSamplers",
        "maxPerStageDescriptorStorageImages",  "maxPerStageDescriptorInputAttachments"};
    for (int s = 0; s < 2; s++) {
        uint32_t all = 0;
        for (int k = 0; k < 6; k++) {
            all += n[s][k];
            if (n[s][k] > stageMax[k]) {
                VKR_LOG("pipeline %s: %u in the %s stage, the device allows %u (%s)", name, n[s][k],
                        s ? "fragment" : "vertex", stageMax[k], stageName[k]);
                g_vkr.lastLimit = stageName[k];
                return false;
            }
        }
        if (all + (s ? d->colorCount : 0) > l->maxPerStageResources) {
            VKR_LOG("pipeline %s: %u resources in the %s stage, the device allows %u "
                    "(maxPerStageResources)",
                    name, all, s ? "fragment" : "vertex", l->maxPerStageResources);
            g_vkr.lastLimit = "maxPerStageResources";
            return false;
        }
    }
    if (stoImg > l->maxDescriptorSetStorageImages || inAtt > l->maxDescriptorSetInputAttachments) {
        VKR_LOG("pipeline %s: %u storage images and %u input attachments; the device allows %u "
                "and %u (maxDescriptorSet*)",
                name, stoImg, inAtt, l->maxDescriptorSetStorageImages,
                l->maxDescriptorSetInputAttachments);
        g_vkr.lastLimit = "maxDescriptorSet";
        return false;
    }
    if (dyn > l->maxDescriptorSetUniformBuffersDynamic || ubo > l->maxDescriptorSetUniformBuffers ||
        ssbo > l->maxDescriptorSetStorageBuffers || img > l->maxDescriptorSetSampledImages ||
        smp > l->maxDescriptorSetSamplers) {
        VKR_LOG("pipeline %s: %u dynamic and %u uniform buffers, %u storage buffers, %u sampled "
                "images, %u samplers; the device allows %u, %u, %u, %u, %u (maxDescriptorSet*)",
                name, dyn, ubo, ssbo, img, smp, l->maxDescriptorSetUniformBuffersDynamic,
                l->maxDescriptorSetUniformBuffers, l->maxDescriptorSetStorageBuffers,
                l->maxDescriptorSetSampledImages, l->maxDescriptorSetSamplers);
        g_vkr.lastLimit = "maxDescriptorSet";
        return false;
    }
    if (d->colorCount > l->maxColorAttachments || d->colorCount > l->maxFragmentOutputAttachments) {
        VKR_LOG("pipeline %s: %u colour targets, the device allows %u (maxColorAttachments) and "
                "%u (maxFragmentOutputAttachments)",
                name, d->colorCount, l->maxColorAttachments, l->maxFragmentOutputAttachments);
        g_vkr.lastLimit = "maxFragmentOutputAttachments";
        return false;
    }
    if (d->vertexAttrCount > l->maxVertexInputAttributes ||
        d->vertexBindingCount > l->maxVertexInputBindings) {
        VKR_LOG("pipeline %s: %u vertex attributes in %u bindings, the device allows %u and %u",
                name, d->vertexAttrCount, d->vertexBindingCount, l->maxVertexInputAttributes,
                l->maxVertexInputBindings);
        g_vkr.lastLimit = "maxVertexInputAttributes";
        return false;
    }
    for (uint32_t i = 0; i < d->vertexBindingCount; i++) {
        if (d->vertexBindings[i].stride > l->maxVertexInputBindingStride) {
            VKR_LOG("pipeline %s: a %u-byte vertex stride, the device allows %u "
                    "(maxVertexInputBindingStride)",
                    name, d->vertexBindings[i].stride, l->maxVertexInputBindingStride);
            g_vkr.lastLimit = "maxVertexInputBindingStride";
            return false;
        }
    }
    return true;
}

/* One pipeline's creation, in three steps: vkr_pipe_prepare on the calling
 * thread (the checks, the pool slot, the pipeline layout and the create
 * info), vkr_pipe_compile on any thread (the driver's compile, through the
 * shared VkPipelineCache, which is internally synchronised), vkr_pipe_finish
 * on the calling thread (the result into the slot, or the slot back).
 * The create info points into the job itself, so a job does not move once
 * prepared. */
typedef struct VkrPipeJob {
    uint32_t id; /* the pool slot; 0: nothing to compile */
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkResult result;
    float ms;
    VkPipelineShaderStageCreateInfo stages[2];
    VkVertexInputBindingDescription vb[RHI_MAX_VERTEX_ATTRS];
    VkVertexInputAttributeDescription va[RHI_MAX_VERTEX_ATTRS];
    VkPipelineVertexInputStateCreateInfo vi;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineViewportStateCreateInfo vp;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo ms_;
    VkPipelineDepthStencilStateCreateInfo ds;
    VkPipelineColorBlendAttachmentState cba[RHI_MAX_COLOR_TARGETS];
    VkFormat colorFormats[RHI_MAX_COLOR_TARGETS];
    VkPipelineColorBlendStateCreateInfo cb;
    VkDynamicState dynStates[10];
    VkPipelineDynamicStateCreateInfo dy;
    VkPipelineRenderingCreateInfo ri;
    VkGraphicsPipelineCreateInfo ci;
} VkrPipeJob;

static const VkDynamicState vkr_pipe_dynamic[] = {
    VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    VK_DYNAMIC_STATE_BLEND_CONSTANTS};

/* RHI_DYN_DEPTH and RHI_DYN_BLEND (rhi.h RhiPipelineDesc.dynamic): the
 * states a pipeline leaves to the draw where the device has them
 * (g_vkr.dynDepth, dynBlend; vk_device.c), the description's values staying
 * the static ones a draw gets until a set (vk_cmd.c vkr_flush_dynamic) */
static const VkDynamicState vkr_pipe_dynamic_depth[] = {VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                                                        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                                                        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP};
static const VkDynamicState vkr_pipe_dynamic_blend[] = {VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
                                                        VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,
                                                        VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT};

/* a creation that failed other than for a full pool (RhiStats
 * pipelineFailures): code the driver's error, 0 for a description this
 * backend refused (its line already written) */
static void vkr_pipe_failed(VkResult code)
{
    g_vkr.stats.pipelineFailures++;
    g_vkr.stats.pipelineLastError = (int64_t)code;
}

static bool vkr_pipe_prepare(const RhiPipelineDesc *d, VkrPipeJob *j)
{
    memset(j, 0, sizeof(*j));
    if (!d || !vkr_valid_pipeline_desc(d)) {
        VKR_LOG("pipeline %s: invalid description", d && d->debugName ? d->debugName : "?");
        vkr_pipe_failed(VK_SUCCESS);
        return false;
    }
    if (!vkr_pipeline_fits(d)) {
        vkr_pipe_failed(VK_SUCCESS);
        return false;
    }
    VkrShader *vs = vkr_pool_get(&g_vkr.shaders, d->vertex.id);
    VkrShader *fs = vkr_pool_get(&g_vkr.shaders, d->fragment.id);
    if (!vs) {
        VKR_LOG("pipeline %s: no vertex shader", d->debugName ? d->debugName : "?");
        vkr_pipe_failed(VK_SUCCESS);
        return false;
    }
    VkrPipeline *p = NULL;
    uint32_t id = vkr_pool_alloc(&g_vkr.pipelines, (void **)&p);
    if (!id) {
        return false; /* counted by the pool (RhiStats pipelinePoolFull) */
    }

    /* pipeline layout: group i is descriptor set i */
    VkDescriptorSetLayout sets[RHI_MAX_BIND_SLOTS];
    for (uint32_t i = 0; i < d->layoutCount; i++) {
        VkrLayout *l = vkr_pool_get(&g_vkr.layouts, d->layouts[i].id);
        if (!l) {
            VKR_LOG("pipeline %s: invalid bind group layout %u", d->debugName ? d->debugName : "?",
                    i);
            vkr_pool_release(&g_vkr.pipelines, id);
            vkr_pipe_failed(VK_SUCCESS);
            return false;
        }
        sets[i] = l->layout;
    }
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = d->layoutCount,
        .pSetLayouts = sets,
    };
    const VkResult lr = vkCreatePipelineLayout(g_vkr.device, &lci, NULL, &p->layout);
    if (!vkr_check(lr, "vkCreatePipelineLayout", __FILE__, __LINE__)) {
        vkr_pool_release(&g_vkr.pipelines, id);
        vkr_pipe_failed(lr);
        return false;
    }
    p->layoutCount = d->layoutCount;
    p->dstRead = (uint8_t)d->dstRead;
    /* the dynamic groups this device takes; the blend only with one colour
     * target (rhi_cmd_set_blend_state sets attachment 0) */
    p->dynamic = 0;
    if ((d->dynamic & RHI_DYN_DEPTH) && g_vkr.dynDepth && d->depthFormat != RHI_FMT_UNKNOWN) {
        p->dynamic |= RHI_DYN_DEPTH;
    }
    if ((d->dynamic & RHI_DYN_BLEND) && g_vkr.dynBlend && d->colorCount == 1) {
        p->dynamic |= RHI_DYN_BLEND;
    }
    p->intTarget = d->colorCount > 0 && vkr_formatMap[d->colorFormats[0]].isInteger;
    j->id = id;
    j->layout = p->layout;

    j->stages[0] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vs->module,
        .pName = vs->entry,
    };
    j->stages[1] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fs ? fs->module : VK_NULL_HANDLE,
        .pName = fs ? fs->entry : "main",
    };

    uint32_t vbCount =
        d->vertexBindingCount < RHI_MAX_VERTEX_ATTRS ? d->vertexBindingCount : RHI_MAX_VERTEX_ATTRS;
    for (uint32_t i = 0; i < vbCount; i++) {
        j->vb[i] = (VkVertexInputBindingDescription){
            d->vertexBindings[i].binding, d->vertexBindings[i].stride,
            d->vertexBindings[i].perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE
                                             : VK_VERTEX_INPUT_RATE_VERTEX};
    }
    for (uint32_t i = 0; i < d->vertexAttrCount; i++) {
        j->va[i] = (VkVertexInputAttributeDescription){
            d->vertexAttrs[i].location, d->vertexAttrs[i].binding,
            vkr_vertexFormatMap[d->vertexAttrs[i].format].vk, d->vertexAttrs[i].offset};
    }
    j->vi = (VkPipelineVertexInputStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = vbCount,
        .pVertexBindingDescriptions = j->vb,
        .vertexAttributeDescriptionCount = d->vertexAttrCount,
        .pVertexAttributeDescriptions = j->va,
    };
    j->ia = (VkPipelineInputAssemblyStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = vkr_topologyMap[d->topology].vk,
    };
    j->vp = (VkPipelineViewportStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    /* The game never culls (rhi.h: cullNone is always true); front face is
     * irrelevant without culling. */
    j->rs = (VkPipelineRasterizationStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    j->ms_ = (VkPipelineMultisampleStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples =
            d->samples > 1u ? (VkSampleCountFlagBits)d->samples : VK_SAMPLE_COUNT_1_BIT,
    };
    const RhiDepthStencilState *dss = &d->depthStencil;
    j->ds = (VkPipelineDepthStencilStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = dss->depthTest ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = (dss->depthTest && dss->depthWrite) ? VK_TRUE : VK_FALSE,
        .depthCompareOp = vkr_compareMap[dss->depthCompare].vk,
        .stencilTestEnable = dss->stencilTest ? VK_TRUE : VK_FALSE,
        .front = vkr_stencil(&dss->front, dss),
        .back = vkr_stencil(&dss->back, dss),
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };
    for (uint32_t i = 0; i < d->colorCount; i++) {
        j->cba[i] = vkr_blend(&d->blend[i], d->colorFormats[i]);
        j->colorFormats[i] = vkr_vk_format(d->colorFormats[i]);
    }
    /* the static values, a dynamic pipeline's draw's until a set */
    memset(&p->depthDefault, 0, sizeof(p->depthDefault));
    p->depthDefault.test = j->ds.depthTestEnable;
    p->depthDefault.write = j->ds.depthWriteEnable;
    p->depthDefault.compare = j->ds.depthCompareOp;
    vkr_blend_dyn(&j->cba[0], &p->blendDefault);
    j->cb = (VkPipelineColorBlendStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        /* the attachment read as an input attachment in rasterization
         * order: no barrier between overlapping fragments (rhi.h RhiDstRead) */
        .flags =
            d->dstRead == RHI_DST_READ_ATTACHMENT
                ? VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT
                : 0u,
        .attachmentCount = d->colorCount,
        .pAttachments = j->cba,
    };
    uint32_t nDyn = 0;
    for (size_t i = 0; i < sizeof(vkr_pipe_dynamic) / sizeof(vkr_pipe_dynamic[0]); i++) {
        j->dynStates[nDyn++] = vkr_pipe_dynamic[i];
    }
    for (size_t i = 0; (p->dynamic & RHI_DYN_DEPTH) && i < 3; i++) {
        j->dynStates[nDyn++] = vkr_pipe_dynamic_depth[i];
    }
    for (size_t i = 0; (p->dynamic & RHI_DYN_BLEND) && i < 3; i++) {
        j->dynStates[nDyn++] = vkr_pipe_dynamic_blend[i];
    }
    j->dy = (VkPipelineDynamicStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = nDyn,
        .pDynamicStates = j->dynStates,
    };
    VkFormat depthFmt = vkr_vk_format(d->depthFormat);
    j->ri = (VkPipelineRenderingCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = d->colorCount,
        .pColorAttachmentFormats = j->colorFormats,
        .depthAttachmentFormat = depthFmt,
        .stencilAttachmentFormat =
            d->depthFormat == RHI_FMT_D32F_S8 ? depthFmt : VK_FORMAT_UNDEFINED,
    };
    j->ci = (VkGraphicsPipelineCreateInfo){
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &j->ri,
        .stageCount = fs ? 2u : 1u,
        .pStages = j->stages,
        .pVertexInputState = &j->vi,
        .pInputAssemblyState = &j->ia,
        .pViewportState = &j->vp,
        .pRasterizationState = &j->rs,
        .pMultisampleState = &j->ms_,
        .pDepthStencilState = &j->ds,
        .pColorBlendState = &j->cb,
        .pDynamicState = &j->dy,
        .layout = j->layout,
    };
    if (g_vkr.legacyPasses) {
        /* no dynamic rendering: the render pass of the pipeline's
         * compatibility class (vk_renderpass.c), made here on the calling
         * thread; the compile threads only read the handle */
        j->ci.pNext = NULL;
        j->ci.renderPass = vkr_renderpass_compat(
            d->colorCount, j->colorFormats,
            d->depthFormat != RHI_FMT_UNKNOWN ? depthFmt : VK_FORMAT_UNDEFINED,
            j->ms_.rasterizationSamples);
        j->ci.subpass = 0;
        if (!j->ci.renderPass) {
            VKR_LOG("pipeline %s: no render pass for its targets",
                    d->debugName ? d->debugName : "?");
            vkDestroyPipelineLayout(g_vkr.device, j->layout, NULL);
            vkr_pool_release(&g_vkr.pipelines, j->id);
            vkr_pipe_failed(VK_SUCCESS);
            j->id = 0;
            return false;
        }
    }
    return true;
}

/* Any thread: the driver call alone (no pool, no log) */
static void vkr_pipe_compile(VkrPipeJob *j)
{
    if (!j->id) {
        return;
    }
    const uint64_t t0 = vkr_now_ns();
    j->result =
        vkCreateGraphicsPipelines(g_vkr.device, g_vkr.pipelineCache, 1, &j->ci, NULL, &j->pipeline);
    j->ms = (float)((double)(vkr_now_ns() - t0) / 1e6);
}

static RhiPipeline vkr_pipe_finish(const RhiPipelineDesc *d, VkrPipeJob *j)
{
    RhiPipeline out = {0};
    if (!j->id) {
        return out;
    }
    VkrPipeline *p = vkr_pool_get(&g_vkr.pipelines, j->id);
    if (!vkr_check(j->result, "vkCreateGraphicsPipelines", __FILE__, __LINE__) || !p) {
        if (j->result == VK_SUCCESS) {
            vkDestroyPipeline(g_vkr.device, j->pipeline, NULL);
        }
        vkDestroyPipelineLayout(g_vkr.device, j->layout, NULL);
        vkr_pool_release(&g_vkr.pipelines, j->id);
        vkr_pipe_failed(j->result);
        return out;
    }
    p->pipeline = j->pipeline;
    if (g_vkr.debugUtils && d->debugName && VKR_FUNCTION_AVAILABLE(vkSetDebugUtilsObjectNameEXT)) {
        VkDebugUtilsObjectNameInfoEXT ni = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = VK_OBJECT_TYPE_PIPELINE,
            .objectHandle = VKR_H(p->pipeline),
            .pObjectName = d->debugName,
        };
        vkSetDebugUtilsObjectNameEXT(g_vkr.device, &ni);
    }
    out.id = j->id;
    return out;
}

RhiPipeline rhi_create_pipeline(const RhiPipelineDesc *d)
{
    VkrPipeJob j;
    if (!vkr_pipe_prepare(d, &j)) {
        return (RhiPipeline){0};
    }
    vkr_pipe_compile(&j);
    return vkr_pipe_finish(d, &j);
}

/* ------------------------------------------------- several at once (rhi.h)
 * The workers take the next job index from one counter until none is left;
 * each finished job counts and wakes the calling thread, which calls the
 * caller's wait function at most every RHI_PIPELINE_WAIT_MS. */
typedef struct VkrPipeWork {
    VkrPipeJob *jobs;
    uint32_t n;
#ifdef ICO_RHI_HAVE_SDL
    SDL_AtomicInt next, finished;
    SDL_Semaphore *wake;
#endif
} VkrPipeWork;

/* the workers' stack: a desktop main thread's (a driver's compiler may
 * recurse deeply; the platform's default for a new thread is 1 MB on
 * Windows and Android) */
#define VKR_PIPE_STACK_BYTES (8u * 1024u * 1024u)

#ifdef ICO_RHI_HAVE_SDL
static int SDLCALL vkr_pipe_worker(void *arg)
{
    VkrPipeWork *w = arg;
    for (;;) {
        const int i = SDL_AddAtomicInt(&w->next, 1);
        if (i < 0 || (uint32_t)i >= w->n) {
            break;
        }
        vkr_pipe_compile(&w->jobs[i]);
        SDL_AddAtomicInt(&w->finished, 1);
        SDL_SignalSemaphore(w->wake);
    }
    return 0;
}

static uint32_t vkr_pipe_threads_for(uint32_t jobs)
{
    int cores = SDL_GetNumLogicalCPUCores();
    uint32_t t = cores > 1 ? (uint32_t)cores - 1u : 1u;
    if (t > RHI_PIPELINE_THREADS_MAX) {
        t = RHI_PIPELINE_THREADS_MAX;
    }
    return t < jobs ? t : (jobs > 0 ? jobs : 1u);
}
#endif

uint32_t rhi_create_pipelines(const RhiPipelineDesc *descs, uint32_t n, RhiPipeline *out,
                              RhiPipelineBatch *batch)
{
    uint32_t made = 0, threads = 1;
    if (batch) {
        batch->threads = 1;
    }
    if (!descs || !out || n == 0) {
        return 0;
    }
    VkrPipeWork w;
    memset(&w, 0, sizeof(w));
    w.jobs = calloc(n, sizeof(VkrPipeJob));
    w.n = n;
    if (!w.jobs) {
        /* no room for the jobs: one at a time */
        for (uint32_t i = 0; i < n; i++) {
            out[i] = rhi_create_pipeline(&descs[i]);
            made += out[i].id != 0;
            if (batch && batch->ms) {
                batch->ms[i] = 0.0f;
            }
        }
        return made;
    }
    for (uint32_t i = 0; i < n; i++) {
        vkr_pipe_prepare(&descs[i], &w.jobs[i]);
    }
    bool done = false;
#ifdef ICO_RHI_HAVE_SDL
    /* a single pipeline with nobody waiting needs no thread */
    if (n > 1 || (batch && batch->wait)) {
        SDL_Thread *th[RHI_PIPELINE_THREADS_MAX];
        uint32_t started = 0;
        uint32_t want = vkr_pipe_threads_for(n);
        if (batch && batch->maxThreads > 0 && want > batch->maxThreads) {
            want = batch->maxThreads; /* the careful set: one compile at a time */
        }
        SDL_SetAtomicInt(&w.next, 0);
        SDL_SetAtomicInt(&w.finished, 0);
        w.wake = SDL_CreateSemaphore(0);
        SDL_PropertiesID props = w.wake ? SDL_CreateProperties() : 0;
        if (props) {
            SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                                   (void *)vkr_pipe_worker);
            SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "rhi-pipelines");
            SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER,
                                  VKR_PIPE_STACK_BYTES);
            SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, &w);
            for (; started < want; started++) {
                th[started] = SDL_CreateThreadWithProperties(props);
                if (!th[started]) {
                    break;
                }
            }
            SDL_DestroyProperties(props);
        }
        if (started > 0) {
            uint64_t last = vkr_now_ns();
            while ((uint32_t)SDL_GetAtomicInt(&w.finished) < n) {
                SDL_WaitSemaphoreTimeout(w.wake, (Sint32)RHI_PIPELINE_WAIT_MS);
                const uint64_t now = vkr_now_ns();
                if (batch && batch->wait &&
                    now - last >= (uint64_t)RHI_PIPELINE_WAIT_MS * 1000000ull) {
                    batch->wait(batch->ctx, (uint32_t)SDL_GetAtomicInt(&w.finished));
                    last = vkr_now_ns();
                }
            }
            for (uint32_t i = 0; i < started; i++) {
                SDL_WaitThread(th[i], NULL);
            }
            threads = started;
            done = true;
        } else if (w.wake) {
            VKR_LOG("pipelines: no worker thread (%s); creating them on this thread",
                    SDL_GetError());
        }
        if (w.wake) {
            SDL_DestroySemaphore(w.wake);
        }
    }
#endif
    if (!done) {
        /* the calling thread compiles them, the wait function between two */
        for (uint32_t i = 0; i < n; i++) {
            vkr_pipe_compile(&w.jobs[i]);
            if (batch && batch->wait && i + 1 < n) {
                batch->wait(batch->ctx, i + 1);
            }
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        out[i] = vkr_pipe_finish(&descs[i], &w.jobs[i]);
        made += out[i].id != 0;
        if (batch && batch->ms) {
            batch->ms[i] = w.jobs[i].ms;
        }
    }
    if (batch) {
        batch->threads = threads;
    }
    free(w.jobs);
    return made;
}

void rhi_destroy_pipeline(RhiPipeline h)
{
    VkrPipeline *p = vkr_pool_get(&g_vkr.pipelines, h.id);
    if (!p) {
        return;
    }
    vkr_defer(VKR_GARBAGE_PIPELINE, VKR_H(p->pipeline));
    vkr_defer(VKR_GARBAGE_PIPELINE_LAYOUT, VKR_H(p->layout));
    vkr_pool_release(&g_vkr.pipelines, h.id);
}
