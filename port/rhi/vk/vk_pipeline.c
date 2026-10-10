/* vk_pipeline.c: bind group layouts, transient bind groups and graphics
 * pipelines for the Vulkan backend. */
#include "vk_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void vkr_pipeline_cache_init(void)
{
    size_t size = 0;
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
        VKR_LOG("pipeline cache: %zu bytes from %s", size, s_cachePath);
    }
    free(blob);
}

void vkr_pipeline_cache_shutdown(void)
{
    if (!g_vkr.pipelineCache) {
        return;
    }
    size_t size = 0;
    void *blob = NULL;
    if (s_cachePath[0] && !g_vkr.deviceLost &&
        vkGetPipelineCacheData(g_vkr.device, g_vkr.pipelineCache, &size, NULL) == VK_SUCCESS &&
        size >= 32) {
        blob = malloc(size);
        if (blob &&
            vkGetPipelineCacheData(g_vkr.device, g_vkr.pipelineCache, &size, blob) != VK_SUCCESS) {
            free(blob);
            blob = NULL;
        }
    }
    if (blob) {
        char tmp[sizeof(s_cachePath) + 8];
        snprintf(tmp, sizeof(tmp), "%s.tmp", s_cachePath);
        FILE *fp = fopen(tmp, "wb");
        bool ok = fp && fwrite(blob, 1, size, fp) == size;
        if (fp) {
            ok = fclose(fp) == 0 && ok;
        }
        /* rename does not replace an existing file on Windows */
        remove(s_cachePath);
        if (!ok || rename(tmp, s_cachePath) != 0) {
            remove(tmp);
            VKR_LOG("pipeline cache: could not write %s", s_cachePath);
        }
        free(blob);
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
            VkImageView v =
                (b->aspect == RHI_ASPECT_DEPTH && t->depthView) ? t->depthView : t->view;
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
static VkPipelineColorBlendAttachmentState vkr_blend(const RhiBlendState *b, RhiFormat fmt)
{
    VkPipelineColorBlendAttachmentState s = {0};
    s.colorWriteMask = (VkColorComponentFlags)(b->writeMask & 0xFu);
    bool enable = b->enable;
    if (enable && vkr_formatMap[fmt].isInteger) {
        VKR_LOG("pipeline: blending on an integer target is ignored");
        enable = false;
    }
    if (!enable) {
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
    uint32_t n[2][4] = {{0}};
    uint32_t dyn = 0, ubo = 0, ssbo = 0, img = 0, smp = 0;
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
    const uint32_t stageMax[4] = {
        l->maxPerStageDescriptorUniformBuffers, l->maxPerStageDescriptorStorageBuffers,
        l->maxPerStageDescriptorSampledImages, l->maxPerStageDescriptorSamplers};
    static const char *const stageName[4] = {
        "maxPerStageDescriptorUniformBuffers", "maxPerStageDescriptorStorageBuffers",
        "maxPerStageDescriptorSampledImages", "maxPerStageDescriptorSamplers"};
    for (int s = 0; s < 2; s++) {
        uint32_t all = 0;
        for (int k = 0; k < 4; k++) {
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

RhiPipeline rhi_create_pipeline(const RhiPipelineDesc *d)
{
    RhiPipeline out = {0};
    if (!d || !vkr_valid_pipeline_desc(d)) {
        VKR_LOG("pipeline %s: invalid description", d && d->debugName ? d->debugName : "?");
        return out;
    }
    if (!vkr_pipeline_fits(d)) {
        return out;
    }
    VkrShader *vs = vkr_pool_get(&g_vkr.shaders, d->vertex.id);
    VkrShader *fs = vkr_pool_get(&g_vkr.shaders, d->fragment.id);
    if (!vs) {
        VKR_LOG("pipeline %s: no vertex shader", d->debugName ? d->debugName : "?");
        return out;
    }
    VkrPipeline *p = NULL;
    uint32_t id = vkr_pool_alloc(&g_vkr.pipelines, (void **)&p);
    if (!id) {
        return out;
    }

    /* pipeline layout: group i is descriptor set i */
    VkDescriptorSetLayout sets[RHI_MAX_BIND_SLOTS];
    for (uint32_t i = 0; i < d->layoutCount; i++) {
        VkrLayout *l = vkr_pool_get(&g_vkr.layouts, d->layouts[i].id);
        if (!l) {
            VKR_LOG("pipeline %s: invalid bind group layout %u", d->debugName ? d->debugName : "?",
                    i);
            vkr_pool_release(&g_vkr.pipelines, id);
            return out;
        }
        sets[i] = l->layout;
    }
    VkPipelineLayoutCreateInfo lci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = d->layoutCount,
        .pSetLayouts = sets,
    };
    if (!VKR_CHECK(vkCreatePipelineLayout(g_vkr.device, &lci, NULL, &p->layout))) {
        vkr_pool_release(&g_vkr.pipelines, id);
        return out;
    }
    p->layoutCount = d->layoutCount;

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs->module,
            .pName = vs->entry,
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fs ? fs->module : VK_NULL_HANDLE,
            .pName = fs ? fs->entry : "main",
        },
    };

    VkVertexInputBindingDescription vb[RHI_MAX_VERTEX_ATTRS];
    VkVertexInputAttributeDescription va[RHI_MAX_VERTEX_ATTRS];
    uint32_t vbCount =
        d->vertexBindingCount < RHI_MAX_VERTEX_ATTRS ? d->vertexBindingCount : RHI_MAX_VERTEX_ATTRS;
    for (uint32_t i = 0; i < vbCount; i++) {
        vb[i] = (VkVertexInputBindingDescription){
            d->vertexBindings[i].binding, d->vertexBindings[i].stride,
            d->vertexBindings[i].perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE
                                             : VK_VERTEX_INPUT_RATE_VERTEX};
    }
    for (uint32_t i = 0; i < d->vertexAttrCount; i++) {
        va[i] = (VkVertexInputAttributeDescription){
            d->vertexAttrs[i].location, d->vertexAttrs[i].binding,
            vkr_vertexFormatMap[d->vertexAttrs[i].format].vk, d->vertexAttrs[i].offset};
    }
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = vbCount,
        .pVertexBindingDescriptions = vb,
        .vertexAttributeDescriptionCount = d->vertexAttrCount,
        .pVertexAttributeDescriptions = va,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = vkr_topologyMap[d->topology].vk,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    /* The game never culls (rhi.h: cullNone is always true); front face is
     * irrelevant without culling. */
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const RhiDepthStencilState *dss = &d->depthStencil;
    VkPipelineDepthStencilStateCreateInfo ds = {
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
    VkPipelineColorBlendAttachmentState cba[RHI_MAX_COLOR_TARGETS];
    VkFormat colorFormats[RHI_MAX_COLOR_TARGETS];
    for (uint32_t i = 0; i < d->colorCount; i++) {
        cba[i] = vkr_blend(&d->blend[i], d->colorFormats[i]);
        colorFormats[i] = vkr_vk_format(d->colorFormats[i]);
    }
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = d->colorCount,
        .pAttachments = cba,
    };
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                  VK_DYNAMIC_STATE_STENCIL_REFERENCE,
                                  VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    VkPipelineDynamicStateCreateInfo dy = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = (uint32_t)(sizeof(dyn) / sizeof(dyn[0])),
        .pDynamicStates = dyn,
    };
    VkFormat depthFmt = vkr_vk_format(d->depthFormat);
    VkPipelineRenderingCreateInfo ri = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = d->colorCount,
        .pColorAttachmentFormats = colorFormats,
        .depthAttachmentFormat = depthFmt,
        .stencilAttachmentFormat =
            d->depthFormat == RHI_FMT_D32F_S8 ? depthFmt : VK_FORMAT_UNDEFINED,
    };
    VkGraphicsPipelineCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &ri,
        .stageCount = fs ? 2u : 1u,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dy,
        .layout = p->layout,
    };
    if (!VKR_CHECK(vkCreateGraphicsPipelines(g_vkr.device, g_vkr.pipelineCache, 1, &ci, NULL,
                                             &p->pipeline))) {
        vkDestroyPipelineLayout(g_vkr.device, p->layout, NULL);
        vkr_pool_release(&g_vkr.pipelines, id);
        return out;
    }
    if (g_vkr.debugUtils && d->debugName && VKR_FUNCTION_AVAILABLE(vkSetDebugUtilsObjectNameEXT)) {
        VkDebugUtilsObjectNameInfoEXT ni = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = VK_OBJECT_TYPE_PIPELINE,
            .objectHandle = VKR_H(p->pipeline),
            .pObjectName = d->debugName,
        };
        vkSetDebugUtilsObjectNameEXT(g_vkr.device, &ni);
    }
    out.id = id;
    return out;
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
