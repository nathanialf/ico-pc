/* vk_pipeline.c: bind group layouts, transient bind groups and graphics
 * pipelines for the Vulkan backend. */
#include "vk_internal.h"
#include <stdlib.h>
#include <string.h>

#define VKR_H(x) ((uint64_t)(x))

static VkShaderStageFlags vkr_Stages(uint32_t rhiStages)
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
RhiBindGroupLayout rhi_CreateBindGroupLayout(const RhiBindGroupLayoutDesc *desc)
{
    RhiBindGroupLayout out = {0};
    if (!desc || desc->slotCount > 16) {
        return out;
    }
    VkrLayout *l = NULL;
    uint32_t id = vkr_PoolAlloc(&g_vkr.layouts, (void **)&l);
    if (!id) {
        return out;
    }
    VkDescriptorSetLayoutBinding b[16];
    for (uint32_t i = 0; i < desc->slotCount; i++) {
        const RhiBindSlot *s = &desc->slots[i];
        if (s->type >= RHI_BIND_COUNT) {
            vkr_PoolRelease(&g_vkr.layouts, id);
            return out;
        }
        b[i] = (VkDescriptorSetLayoutBinding){
            .binding = s->slot + vkr_bindTypeMap[s->type].shift,
            .descriptorType = vkr_bindTypeMap[s->type].vk,
            .descriptorCount = 1,
            .stageFlags = vkr_Stages(s->stages),
        };
        l->slots[i] = *s;
    }
    l->slotCount = desc->slotCount;
    VkDescriptorSetLayoutCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = desc->slotCount,
        .pBindings = b,
    };
    if (!VKR_CHECK(vkCreateDescriptorSetLayout(g_vkr.device, &ci, NULL, &l->layout))) {
        vkr_PoolRelease(&g_vkr.layouts, id);
        return out;
    }
    out.id = id;
    return out;
}

void rhi_DestroyBindGroupLayout(RhiBindGroupLayout h)
{
    VkrLayout *l = vkr_PoolGet(&g_vkr.layouts, h.id);
    if (!l) {
        return;
    }
    vkr_Defer(VKR_GARBAGE_SET_LAYOUT, VKR_H(l->layout));
    vkr_PoolRelease(&g_vkr.layouts, h.id);
}

/* ---------------------------------------------------- transient bind groups
 * Allocated from the frame slot's descriptor pools, which are reset as a
 * whole when the slot is reused (rhi_WaitFrame).  The handle id is
 * (frame tag << 20) | (index + 1); a handle from another frame fails the
 * lookup. */
static bool vkr_NewDescriptorPool(VkrFrame *f)
{
    if (f->descPoolCount == VKR_DESC_POOLS_MAX) {
        VKR_LOG("descriptor pools exhausted for this frame");
        return false;
    }
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192},
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

static uint32_t vkr_FrameTag(void)
{
    return (uint32_t)(g_vkr.frameIndex & VKR_GEN_MASK);
}

VkDescriptorSet vkr_GetBindGroup(RhiBindGroup bg)
{
    VkrFrame *f = vkr_CurFrame();
    uint32_t idx = bg.id & VKR_INDEX_MASK;
    if (idx == 0 || idx > f->setCount || (bg.id >> VKR_GEN_SHIFT) != vkr_FrameTag()) {
        return VK_NULL_HANDLE;
    }
    return f->sets[idx - 1];
}

static VkImageLayout vkr_SampledLayout(const VkrTexture *t)
{
    return vkr_StateLayout(RHI_STATE_SHADER_READ, t->rhiFormat);
}

RhiBindGroup rhi_CreateBindGroup(const RhiBindGroupDesc *desc)
{
    RhiBindGroup out = {0};
    VkrLayout *l = desc ? vkr_PoolGet(&g_vkr.layouts, desc->layout.id) : NULL;
    if (!l || desc->bindingCount > 16) {
        return out;
    }
    VkrFrame *f = vkr_CurFrame();
    if (f->setCount == f->setCap) {
        uint32_t cap = f->setCap ? f->setCap * 2u : 1024u;
        VkDescriptorSet *s = realloc(f->sets, cap * sizeof(*s));
        if (!s) {
            return out;
        }
        f->sets = s;
        f->setCap = cap;
    }
    if (f->setCount >= VKR_INDEX_MASK) {
        return out;
    }
    if (f->descPoolCount == 0 && !vkr_NewDescriptorPool(f)) {
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
        } else if (vkr_NewDescriptorPool(f)) {
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
        case RHI_BIND_STORAGE_BUFFER: {
            VkrBuffer *buf = vkr_GetBuffer(b->buffer);
            if (!buf) {
                VKR_LOG("bind group: slot %u: invalid buffer", b->slot);
                continue;
            }
            bi[n] =
                (VkDescriptorBufferInfo){buf->buffer, b->offset, b->size ? b->size : VK_WHOLE_SIZE};
            w[n].pBufferInfo = &bi[n];
            break;
        }
        case RHI_BIND_SAMPLED_TEXTURE: {
            VkrTexture *t = vkr_GetTexture(b->texture);
            if (!t) {
                VKR_LOG("bind group: slot %u: invalid texture", b->slot);
                continue;
            }
            VkImageView v =
                (b->aspect == RHI_ASPECT_DEPTH && t->depthView) ? t->depthView : t->view;
            ii[n] = (VkDescriptorImageInfo){VK_NULL_HANDLE, v, vkr_SampledLayout(t)};
            w[n].pImageInfo = &ii[n];
            break;
        }
        case RHI_BIND_SAMPLER: {
            VkSampler *s = vkr_PoolGet(&g_vkr.samplers, b->sampler.id);
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
    f->sets[f->setCount++] = set;
    g_vkr.stats.bindGroups++;
    out.id = (vkr_FrameTag() << VKR_GEN_SHIFT) | f->setCount;
    return out;
}

/* -------------------------------------------------------------- pipelines */
static VkPipelineColorBlendAttachmentState vkr_Blend(const RhiBlendState *b, RhiFormat fmt)
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

static VkStencilOpState vkr_Stencil(const RhiStencilFace *f, const RhiDepthStencilState *ds)
{
    VkStencilOpState s = {
        .failOp = vkr_stencilOpMap[f->fail].vk,
        .passOp = vkr_stencilOpMap[f->pass].vk,
        .depthFailOp = vkr_stencilOpMap[f->depthFail].vk,
        .compareOp = vkr_compareMap[f->compare].vk,
        .compareMask = ds->stencilReadMask,
        .writeMask = ds->stencilWriteMask,
        .reference = 0, /* dynamic: rhi_CmdSetStencilRef */
    };
    return s;
}

static bool vkr_ValidPipelineDesc(const RhiPipelineDesc *d)
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

RhiPipeline rhi_CreatePipeline(const RhiPipelineDesc *d)
{
    RhiPipeline out = {0};
    if (!d || !vkr_ValidPipelineDesc(d)) {
        VKR_LOG("pipeline %s: invalid description", d && d->debugName ? d->debugName : "?");
        return out;
    }
    VkrShader *vs = vkr_PoolGet(&g_vkr.shaders, d->vertex.id);
    VkrShader *fs = vkr_PoolGet(&g_vkr.shaders, d->fragment.id);
    if (!vs) {
        VKR_LOG("pipeline %s: no vertex shader", d->debugName ? d->debugName : "?");
        return out;
    }
    VkrPipeline *p = NULL;
    uint32_t id = vkr_PoolAlloc(&g_vkr.pipelines, (void **)&p);
    if (!id) {
        return out;
    }

    /* pipeline layout: group i is descriptor set i */
    VkDescriptorSetLayout sets[RHI_MAX_BIND_SLOTS];
    for (uint32_t i = 0; i < d->layoutCount; i++) {
        VkrLayout *l = vkr_PoolGet(&g_vkr.layouts, d->layouts[i].id);
        if (!l) {
            VKR_LOG("pipeline %s: invalid bind group layout %u", d->debugName ? d->debugName : "?",
                    i);
            vkr_PoolRelease(&g_vkr.pipelines, id);
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
        vkr_PoolRelease(&g_vkr.pipelines, id);
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
        .front = vkr_Stencil(&dss->front, dss),
        .back = vkr_Stencil(&dss->back, dss),
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };
    VkPipelineColorBlendAttachmentState cba[RHI_MAX_COLOR_TARGETS];
    VkFormat colorFormats[RHI_MAX_COLOR_TARGETS];
    for (uint32_t i = 0; i < d->colorCount; i++) {
        cba[i] = vkr_Blend(&d->blend[i], d->colorFormats[i]);
        colorFormats[i] = vkr_formatMap[d->colorFormats[i]].vk;
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
    VkFormat depthFmt = vkr_formatMap[d->depthFormat].vk;
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
    if (!VKR_CHECK(
            vkCreateGraphicsPipelines(g_vkr.device, VK_NULL_HANDLE, 1, &ci, NULL, &p->pipeline))) {
        vkDestroyPipelineLayout(g_vkr.device, p->layout, NULL);
        vkr_PoolRelease(&g_vkr.pipelines, id);
        return out;
    }
    if (g_vkr.debugUtils && d->debugName && vkSetDebugUtilsObjectNameEXT) {
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

void rhi_DestroyPipeline(RhiPipeline h)
{
    VkrPipeline *p = vkr_PoolGet(&g_vkr.pipelines, h.id);
    if (!p) {
        return;
    }
    vkr_Defer(VKR_GARBAGE_PIPELINE, VKR_H(p->pipeline));
    vkr_Defer(VKR_GARBAGE_PIPELINE_LAYOUT, VKR_H(p->layout));
    vkr_PoolRelease(&g_vkr.pipelines, h.id);
}
