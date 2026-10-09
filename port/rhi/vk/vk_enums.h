/* vk_enums.h: the rhi.h enumerations mapped to Vulkan, as tables indexed by
 * the RHI value.  Every table has one entry per enumerator (sized by the
 * *_COUNT sentinel) and each entry carries a `set` flag, so a hole left by a
 * new enumerator is caught by port/rhi/test/rhi_vk_enum_test.c rather than
 * silently mapping to Vulkan's zero value. */
#ifndef PORT_RHI_VK_VK_ENUMS_H
#define PORT_RHI_VK_VK_ENUMS_H
#if !defined(VK_NO_PROTOTYPES) && !defined(ICO_RHI_MOLTENVK)
#define VK_NO_PROTOTYPES
#endif

#include <vulkan/vulkan_core.h>
#include "../rhi.h"

typedef struct VkrFormatMap {
    bool set;
    VkFormat vk;
    uint32_t texelBytes;       /* bytes per texel of the copy aspect (BC: per 4x4 block) */
    VkImageAspectFlags aspect; /* all aspects of the format */
    bool isInteger;            /* blending not allowed */
} VkrFormatMap;

#define VKR_COLOR VK_IMAGE_ASPECT_COLOR_BIT
#define VKR_DS (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)

static const VkrFormatMap vkr_formatMap[RHI_FMT_COUNT] = {
    [RHI_FMT_UNKNOWN] = {true, VK_FORMAT_UNDEFINED, 0, 0, false},
    [RHI_FMT_RGBA8_UNORM] = {true, VK_FORMAT_R8G8B8A8_UNORM, 4, VKR_COLOR, false},
    [RHI_FMT_RGBA8_UINT] = {true, VK_FORMAT_R8G8B8A8_UINT, 4, VKR_COLOR, true},
    [RHI_FMT_R8_UNORM] = {true, VK_FORMAT_R8_UNORM, 1, VKR_COLOR, false},
    [RHI_FMT_R8_UINT] = {true, VK_FORMAT_R8_UINT, 1, VKR_COLOR, true},
    [RHI_FMT_R16_UINT] = {true, VK_FORMAT_R16_UINT, 2, VKR_COLOR, true},
    [RHI_FMT_RGBA16F] = {true, VK_FORMAT_R16G16B16A16_SFLOAT, 8, VKR_COLOR, false},
    [RHI_FMT_D32F] = {true, VK_FORMAT_D32_SFLOAT, 4, VK_IMAGE_ASPECT_DEPTH_BIT, false},
    [RHI_FMT_D32F_S8] = {true, VK_FORMAT_D32_SFLOAT_S8_UINT, 4, VKR_DS, false},
    [RHI_FMT_BGRA8_UNORM] = {true, VK_FORMAT_B8G8R8A8_UNORM, 4, VKR_COLOR, false},
    [RHI_FMT_BC1_UNORM] = {true, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 8, VKR_COLOR, false},
    [RHI_FMT_BC2_UNORM] = {true, VK_FORMAT_BC2_UNORM_BLOCK, 16, VKR_COLOR, false},
    [RHI_FMT_BC3_UNORM] = {true, VK_FORMAT_BC3_UNORM_BLOCK, 16, VKR_COLOR, false},
    [RHI_FMT_BC7_UNORM] = {true, VK_FORMAT_BC7_UNORM_BLOCK, 16, VKR_COLOR, false},
};

typedef struct VkrVertexFormatMap {
    bool set;
    VkFormat vk;
} VkrVertexFormatMap;

static const VkrVertexFormatMap vkr_vertexFormatMap[RHI_VTX_COUNT] = {
    [RHI_VTX_F32x1] = {true, VK_FORMAT_R32_SFLOAT},
    [RHI_VTX_F32x2] = {true, VK_FORMAT_R32G32_SFLOAT},
    [RHI_VTX_F32x3] = {true, VK_FORMAT_R32G32B32_SFLOAT},
    [RHI_VTX_F32x4] = {true, VK_FORMAT_R32G32B32A32_SFLOAT},
    [RHI_VTX_U8x4_UNORM] = {true, VK_FORMAT_R8G8B8A8_UNORM},
    [RHI_VTX_U8x4_UINT] = {true, VK_FORMAT_R8G8B8A8_UINT},
    [RHI_VTX_U16x2_UINT] = {true, VK_FORMAT_R16G16_UINT},
    [RHI_VTX_U32x1] = {true, VK_FORMAT_R32_UINT},
};

typedef struct VkrCompareMap {
    bool set;
    VkCompareOp vk;
} VkrCompareMap;

static const VkrCompareMap vkr_compareMap[RHI_CMP_COUNT] = {
    [RHI_CMP_NEVER] = {true, VK_COMPARE_OP_NEVER},
    [RHI_CMP_LESS] = {true, VK_COMPARE_OP_LESS},
    [RHI_CMP_EQUAL] = {true, VK_COMPARE_OP_EQUAL},
    [RHI_CMP_LEQUAL] = {true, VK_COMPARE_OP_LESS_OR_EQUAL},
    [RHI_CMP_GREATER] = {true, VK_COMPARE_OP_GREATER},
    [RHI_CMP_NOTEQUAL] = {true, VK_COMPARE_OP_NOT_EQUAL},
    [RHI_CMP_GEQUAL] = {true, VK_COMPARE_OP_GREATER_OR_EQUAL},
    [RHI_CMP_ALWAYS] = {true, VK_COMPARE_OP_ALWAYS},
};

typedef struct VkrBlendFactorMap {
    bool set;
    VkBlendFactor vk;
} VkrBlendFactorMap;

static const VkrBlendFactorMap vkr_blendFactorMap[RHI_BF_COUNT] = {
    [RHI_BF_ZERO] = {true, VK_BLEND_FACTOR_ZERO},
    [RHI_BF_ONE] = {true, VK_BLEND_FACTOR_ONE},
    [RHI_BF_SRC_COLOR] = {true, VK_BLEND_FACTOR_SRC_COLOR},
    [RHI_BF_ONE_MINUS_SRC_COLOR] = {true, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR},
    [RHI_BF_DST_COLOR] = {true, VK_BLEND_FACTOR_DST_COLOR},
    [RHI_BF_ONE_MINUS_DST_COLOR] = {true, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR},
    [RHI_BF_SRC_ALPHA] = {true, VK_BLEND_FACTOR_SRC_ALPHA},
    [RHI_BF_ONE_MINUS_SRC_ALPHA] = {true, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA},
    [RHI_BF_DST_ALPHA] = {true, VK_BLEND_FACTOR_DST_ALPHA},
    [RHI_BF_ONE_MINUS_DST_ALPHA] = {true, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA},
    [RHI_BF_CONSTANT] = {true, VK_BLEND_FACTOR_CONSTANT_COLOR},
    [RHI_BF_ONE_MINUS_CONSTANT] = {true, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR},
    [RHI_BF_SRC1_COLOR] = {true, VK_BLEND_FACTOR_SRC1_COLOR},
    [RHI_BF_ONE_MINUS_SRC1_COLOR] = {true, VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR},
    [RHI_BF_SRC1_ALPHA] = {true, VK_BLEND_FACTOR_SRC1_ALPHA},
    [RHI_BF_ONE_MINUS_SRC1_ALPHA] = {true, VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA},
};

typedef struct VkrBlendOpMap {
    bool set;
    VkBlendOp vk;
} VkrBlendOpMap;

static const VkrBlendOpMap vkr_blendOpMap[RHI_BO_COUNT] = {
    [RHI_BO_ADD] = {true, VK_BLEND_OP_ADD},
    [RHI_BO_SUBTRACT] = {true, VK_BLEND_OP_SUBTRACT},
    [RHI_BO_REVERSE_SUBTRACT] = {true, VK_BLEND_OP_REVERSE_SUBTRACT},
};

typedef struct VkrStencilOpMap {
    bool set;
    VkStencilOp vk;
} VkrStencilOpMap;

static const VkrStencilOpMap vkr_stencilOpMap[RHI_SO_COUNT] = {
    [RHI_SO_KEEP] = {true, VK_STENCIL_OP_KEEP},
    [RHI_SO_ZERO] = {true, VK_STENCIL_OP_ZERO},
    [RHI_SO_REPLACE] = {true, VK_STENCIL_OP_REPLACE},
    [RHI_SO_INCR_WRAP] = {true, VK_STENCIL_OP_INCREMENT_AND_WRAP},
    [RHI_SO_DECR_WRAP] = {true, VK_STENCIL_OP_DECREMENT_AND_WRAP},
    [RHI_SO_INCR_CLAMP] = {true, VK_STENCIL_OP_INCREMENT_AND_CLAMP},
    [RHI_SO_DECR_CLAMP] = {true, VK_STENCIL_OP_DECREMENT_AND_CLAMP},
    [RHI_SO_INVERT] = {true, VK_STENCIL_OP_INVERT},
};

typedef struct VkrTopologyMap {
    bool set;
    VkPrimitiveTopology vk;
} VkrTopologyMap;

static const VkrTopologyMap vkr_topologyMap[RHI_TOPO_COUNT] = {
    [RHI_TOPO_TRIANGLE_LIST] = {true, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST},
    [RHI_TOPO_TRIANGLE_STRIP] = {true, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP},
    [RHI_TOPO_LINE_LIST] = {true, VK_PRIMITIVE_TOPOLOGY_LINE_LIST},
    [RHI_TOPO_POINT_LIST] = {true, VK_PRIMITIVE_TOPOLOGY_POINT_LIST},
};

typedef struct VkrLoadOpMap {
    bool set;
    VkAttachmentLoadOp vk;
} VkrLoadOpMap;

static const VkrLoadOpMap vkr_loadOpMap[RHI_LOAD_COUNT] = {
    [RHI_LOAD_LOAD] = {true, VK_ATTACHMENT_LOAD_OP_LOAD},
    [RHI_LOAD_CLEAR] = {true, VK_ATTACHMENT_LOAD_OP_CLEAR},
    [RHI_LOAD_DONT_CARE] = {true, VK_ATTACHMENT_LOAD_OP_DONT_CARE},
};

/* Bind types: the descriptor type, and the shift added to the RHI slot to
 * give the Vulkan binding number.  HLSL b, t and s registers share one
 * number space per register space in D3D12 but not in Vulkan, so DXC is run
 * with -fvk-b-shift 0, -fvk-t-shift 16, -fvk-s-shift 32
 * (cmake/IcoShaders.cmake). */
#define VKR_SHIFT_B 0u
#define VKR_SHIFT_T 16u
#define VKR_SHIFT_S 32u

typedef struct VkrBindTypeMap {
    bool set;
    VkDescriptorType vk;
    uint32_t shift;
} VkrBindTypeMap;

static const VkrBindTypeMap vkr_bindTypeMap[RHI_BIND_COUNT] = {
    [RHI_BIND_UNIFORM_BUFFER] = {true, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VKR_SHIFT_B},
    [RHI_BIND_STORAGE_BUFFER] = {true, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VKR_SHIFT_T},
    [RHI_BIND_SAMPLED_TEXTURE] = {true, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VKR_SHIFT_T},
    [RHI_BIND_SAMPLER] = {true, VK_DESCRIPTOR_TYPE_SAMPLER, VKR_SHIFT_S},
    /* offsets at bind time (vkCmdBindDescriptorSets) */
    [RHI_BIND_UNIFORM_BUFFER_DYNAMIC] = {true, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                                         VKR_SHIFT_B},
};

/* Resource states: the image layout, and the pipeline stages and accesses
 * that touch an image in that state (the barrier's scope on either side).
 * Depth formats are sampled in a depth-stencil read-only layout, so for them
 * SHADER_READ and DEPTH_READ are the same layout (vkr_state_layout) and a
 * descriptor's layout never depends on which of the two states the caller
 * chose.  DEPTH_READ is read-only for stencil as well: a pass that tests
 * depth without writing it but writes stencil (shadow volumes) uses
 * DEPTH_WRITE with depthWrite = false in the pipeline. */
typedef struct VkrStateMap {
    bool set;
    VkImageLayout layout;
    VkPipelineStageFlags stages;
    VkAccessFlags access;
} VkrStateMap;

#define VKR_DEPTH_STAGES                                                                           \
    (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)
/* The stages that sample an image: fragment shaders only.  No vertex
 * shader of port/shaders (the *_vs entry points) or of the RHI tests'
 * rhi_test.hlsl (vs_main) reads a texture; they read uniforms and the VU
 * stream, a buffer, whose copies keep their own vertex-stage barriers
 * (rhi_cmd_copy_buffer).  So a barrier into or out of SHADER_READ or
 * DEPTH_READ does not hold the next pass's vertex work back for the
 * earlier pass's fragments.  A vertex shader that samples an image needs
 * VK_PIPELINE_STAGE_VERTEX_SHADER_BIT added here. */
#define VKR_SAMPLE_STAGES VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT

static const VkrStateMap vkr_stateMap[RHI_STATE_COUNT] = {
    [RHI_STATE_UNDEFINED] = {true, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0},
    [RHI_STATE_RENDER_TARGET] = {true, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
    [RHI_STATE_DEPTH_WRITE] = {true, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                               VKR_DEPTH_STAGES,
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
    [RHI_STATE_DEPTH_READ] = {true, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                              VKR_DEPTH_STAGES | VKR_SAMPLE_STAGES,
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                  VK_ACCESS_SHADER_READ_BIT},
    [RHI_STATE_SHADER_READ] = {true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VKR_SAMPLE_STAGES,
                               VK_ACCESS_SHADER_READ_BIT},
    [RHI_STATE_COPY_SRC] = {true, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT},
    [RHI_STATE_COPY_DST] = {true, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT},
    [RHI_STATE_PRESENT] = {true, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0},
};

/* RHI_FMT_D32F_S8 is the logical scene depth-stencil format; the device
 * backs it with D32_SFLOAT_S8_UINT or, where that is missing,
 * D24_UNORM_S8_UINT (vkr_formatMap holds the D32 entry, ds the chosen one).
 * Every Vulkan format read of an RhiFormat goes through vkr_vk_format
 * (vk_internal.h) or this. */
static inline VkFormat vkr_vk_format_with(RhiFormat f, VkFormat ds)
{
    return f == RHI_FMT_D32F_S8 ? ds : vkr_formatMap[f].vk;
}

static inline bool vkr_is_depth_format(RhiFormat f)
{
    return f == RHI_FMT_D32F || f == RHI_FMT_D32F_S8;
}

static inline VkImageLayout vkr_state_layout(RhiState s, RhiFormat f)
{
    if (s == RHI_STATE_SHADER_READ && vkr_is_depth_format(f)) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
    return vkr_stateMap[s].layout;
}

#endif /* PORT_RHI_VK_VK_ENUMS_H */
