/* d3d12_enums.h: the rhi.h enumerations mapped to D3D12, as tables indexed
 * by the RHI value, each entry with a `set` flag (as vk_enums.h; a hole
 * left by a new enumerator is caught by port/rhi/test/rhi_d3d12_plan_test.c
 * in the Windows build and by the static asserts below).  Include after
 * d3d12_internal.h's Windows headers. */
#ifndef PORT_RHI_D3D12_D3D12_ENUMS_H
#define PORT_RHI_D3D12_D3D12_ENUMS_H

#include "d3d12_plan.h"

/* Textures are created typeless and viewed typed, so RGBA8 UNORM and UINT
 * (and R8 UNORM and UINT) are one resource format: copies between them are
 * copies between identical formats.  The swapchain's buffers are the one
 * typed exception (flip-model swapchains take typed formats only). */
typedef struct DxFormatMap {
    bool set;
    DXGI_FORMAT resource; /* the format the resource is created with */
    DXGI_FORMAT view;     /* SRV / RTV format */
    DXGI_FORMAT dsv;      /* DSV format (depth formats) */
    uint32_t texelBytes;  /* bytes per texel of the copy plane (depth: plane 0) */
    bool depth, stencil;
    bool isInteger; /* blending not allowed */
} DxFormatMap;

static const DxFormatMap dx_formatMap[RHI_FMT_COUNT] = {
    [RHI_FMT_UNKNOWN] = {true, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, 0,
                         false, false, false},
    [RHI_FMT_RGBA8_UNORM] = {true, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM,
                             DXGI_FORMAT_UNKNOWN, 4, false, false, false},
    [RHI_FMT_RGBA8_UINT] = {true, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UINT,
                            DXGI_FORMAT_UNKNOWN, 4, false, false, true},
    [RHI_FMT_R8_UNORM] = {true, DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_UNKNOWN,
                          1, false, false, false},
    [RHI_FMT_R8_UINT] = {true, DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_UNKNOWN, 1,
                         false, false, true},
    [RHI_FMT_R16_UINT] = {true, DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_UNKNOWN,
                          2, false, false, true},
    [RHI_FMT_RGBA16F] = {true, DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_FLOAT,
                         DXGI_FORMAT_UNKNOWN, 8, false, false, false},
    [RHI_FMT_D32F] = {true, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT,
                      4, true, false, false},
    [RHI_FMT_D32F_S8] = {true, DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
                         DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 4, true, true, false},
    [RHI_FMT_BGRA8_UNORM] = {true, DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM,
                             DXGI_FORMAT_UNKNOWN, 4, false, false, false},
};

typedef struct DxVertexFormatMap {
    bool set;
    DXGI_FORMAT dxgi;
} DxVertexFormatMap;

static const DxVertexFormatMap dx_vertexFormatMap[RHI_VTX_COUNT] = {
    [RHI_VTX_F32x1] = {true, DXGI_FORMAT_R32_FLOAT},
    [RHI_VTX_F32x2] = {true, DXGI_FORMAT_R32G32_FLOAT},
    [RHI_VTX_F32x3] = {true, DXGI_FORMAT_R32G32B32_FLOAT},
    [RHI_VTX_F32x4] = {true, DXGI_FORMAT_R32G32B32A32_FLOAT},
    [RHI_VTX_U8x4_UNORM] = {true, DXGI_FORMAT_R8G8B8A8_UNORM},
    [RHI_VTX_U8x4_UINT] = {true, DXGI_FORMAT_R8G8B8A8_UINT},
    [RHI_VTX_U16x2_UINT] = {true, DXGI_FORMAT_R16G16_UINT},
    [RHI_VTX_U32x1] = {true, DXGI_FORMAT_R32_UINT},
};

typedef struct DxCompareMap {
    bool set;
    D3D12_COMPARISON_FUNC d3d;
} DxCompareMap;

static const DxCompareMap dx_compareMap[RHI_CMP_COUNT] = {
    [RHI_CMP_NEVER] = {true, D3D12_COMPARISON_FUNC_NEVER},
    [RHI_CMP_LESS] = {true, D3D12_COMPARISON_FUNC_LESS},
    [RHI_CMP_EQUAL] = {true, D3D12_COMPARISON_FUNC_EQUAL},
    [RHI_CMP_LEQUAL] = {true, D3D12_COMPARISON_FUNC_LESS_EQUAL},
    [RHI_CMP_GREATER] = {true, D3D12_COMPARISON_FUNC_GREATER},
    [RHI_CMP_NOTEQUAL] = {true, D3D12_COMPARISON_FUNC_NOT_EQUAL},
    [RHI_CMP_GEQUAL] = {true, D3D12_COMPARISON_FUNC_GREATER_EQUAL},
    [RHI_CMP_ALWAYS] = {true, D3D12_COMPARISON_FUNC_ALWAYS},
};

/* Blend factors.  `alpha` is the factor used in the alpha equation: D3D12
 * rejects *_COLOR factors there, and for the alpha channel the colour
 * factor and the alpha factor are the same number (SRC1_COLOR.a ==
 * SRC1_ALPHA), so the substitution is exact. */
typedef struct DxBlendFactorMap {
    bool set;
    D3D12_BLEND color;
    D3D12_BLEND alpha;
} DxBlendFactorMap;

static const DxBlendFactorMap dx_blendFactorMap[RHI_BF_COUNT] = {
    [RHI_BF_ZERO] = {true, D3D12_BLEND_ZERO, D3D12_BLEND_ZERO},
    [RHI_BF_ONE] = {true, D3D12_BLEND_ONE, D3D12_BLEND_ONE},
    [RHI_BF_SRC_COLOR] = {true, D3D12_BLEND_SRC_COLOR, D3D12_BLEND_SRC_ALPHA},
    [RHI_BF_ONE_MINUS_SRC_COLOR] = {true, D3D12_BLEND_INV_SRC_COLOR, D3D12_BLEND_INV_SRC_ALPHA},
    [RHI_BF_DST_COLOR] = {true, D3D12_BLEND_DEST_COLOR, D3D12_BLEND_DEST_ALPHA},
    [RHI_BF_ONE_MINUS_DST_COLOR] = {true, D3D12_BLEND_INV_DEST_COLOR, D3D12_BLEND_INV_DEST_ALPHA},
    [RHI_BF_SRC_ALPHA] = {true, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_SRC_ALPHA},
    [RHI_BF_ONE_MINUS_SRC_ALPHA] = {true, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA},
    [RHI_BF_DST_ALPHA] = {true, D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_DEST_ALPHA},
    [RHI_BF_ONE_MINUS_DST_ALPHA] = {true, D3D12_BLEND_INV_DEST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA},
    [RHI_BF_CONSTANT] = {true, D3D12_BLEND_BLEND_FACTOR, D3D12_BLEND_BLEND_FACTOR},
    [RHI_BF_ONE_MINUS_CONSTANT] = {true, D3D12_BLEND_INV_BLEND_FACTOR,
                                   D3D12_BLEND_INV_BLEND_FACTOR},
    [RHI_BF_SRC1_COLOR] = {true, D3D12_BLEND_SRC1_COLOR, D3D12_BLEND_SRC1_ALPHA},
    [RHI_BF_ONE_MINUS_SRC1_COLOR] = {true, D3D12_BLEND_INV_SRC1_COLOR, D3D12_BLEND_INV_SRC1_ALPHA},
    [RHI_BF_SRC1_ALPHA] = {true, D3D12_BLEND_SRC1_ALPHA, D3D12_BLEND_SRC1_ALPHA},
    [RHI_BF_ONE_MINUS_SRC1_ALPHA] = {true, D3D12_BLEND_INV_SRC1_ALPHA, D3D12_BLEND_INV_SRC1_ALPHA},
};

typedef struct DxBlendOpMap {
    bool set;
    D3D12_BLEND_OP d3d;
} DxBlendOpMap;

static const DxBlendOpMap dx_blendOpMap[RHI_BO_COUNT] = {
    [RHI_BO_ADD] = {true, D3D12_BLEND_OP_ADD},
    [RHI_BO_SUBTRACT] = {true, D3D12_BLEND_OP_SUBTRACT},
    [RHI_BO_REVERSE_SUBTRACT] = {true, D3D12_BLEND_OP_REV_SUBTRACT},
};

/* D3D12's INCR/DECR wrap; INCR_SAT/DECR_SAT clamp. */
typedef struct DxStencilOpMap {
    bool set;
    D3D12_STENCIL_OP d3d;
} DxStencilOpMap;

static const DxStencilOpMap dx_stencilOpMap[RHI_SO_COUNT] = {
    [RHI_SO_KEEP] = {true, D3D12_STENCIL_OP_KEEP},
    [RHI_SO_ZERO] = {true, D3D12_STENCIL_OP_ZERO},
    [RHI_SO_REPLACE] = {true, D3D12_STENCIL_OP_REPLACE},
    [RHI_SO_INCR_WRAP] = {true, D3D12_STENCIL_OP_INCR},
    [RHI_SO_DECR_WRAP] = {true, D3D12_STENCIL_OP_DECR},
    [RHI_SO_INCR_CLAMP] = {true, D3D12_STENCIL_OP_INCR_SAT},
    [RHI_SO_DECR_CLAMP] = {true, D3D12_STENCIL_OP_DECR_SAT},
    [RHI_SO_INVERT] = {true, D3D12_STENCIL_OP_INVERT},
};

typedef struct DxTopologyMap {
    bool set;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE type;
    D3D_PRIMITIVE_TOPOLOGY topo;
} DxTopologyMap;

static const DxTopologyMap dx_topologyMap[RHI_TOPO_COUNT] = {
    [RHI_TOPO_TRIANGLE_LIST] = {true, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST},
    [RHI_TOPO_TRIANGLE_STRIP] = {true, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                                 D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP},
    [RHI_TOPO_LINE_LIST] = {true, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
                            D3D_PRIMITIVE_TOPOLOGY_LINELIST},
    [RHI_TOPO_POINT_LIST] = {true, D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT,
                             D3D_PRIMITIVE_TOPOLOGY_POINTLIST},
};

/* d3d12_plan.h keeps D3D12_RESOURCE_STATES values without the header. */
_Static_assert(D3DP_STATE_COMMON == D3D12_RESOURCE_STATE_COMMON, "state");
_Static_assert(D3DP_STATE_VERTEX_AND_CONSTANT_BUFFER ==
                   D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
               "state");
_Static_assert(D3DP_STATE_INDEX_BUFFER == D3D12_RESOURCE_STATE_INDEX_BUFFER, "state");
_Static_assert(D3DP_STATE_RENDER_TARGET == D3D12_RESOURCE_STATE_RENDER_TARGET, "state");
_Static_assert(D3DP_STATE_DEPTH_WRITE == D3D12_RESOURCE_STATE_DEPTH_WRITE, "state");
_Static_assert(D3DP_STATE_DEPTH_READ == D3D12_RESOURCE_STATE_DEPTH_READ, "state");
_Static_assert(D3DP_STATE_NON_PIXEL_SHADER_RESOURCE ==
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               "state");
_Static_assert(D3DP_STATE_PIXEL_SHADER_RESOURCE == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
               "state");
_Static_assert(D3DP_STATE_INDIRECT_ARGUMENT == D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, "state");
_Static_assert(D3DP_STATE_COPY_DEST == D3D12_RESOURCE_STATE_COPY_DEST, "state");
_Static_assert(D3DP_STATE_COPY_SOURCE == D3D12_RESOURCE_STATE_COPY_SOURCE, "state");
_Static_assert(D3DP_STATE_GENERIC_READ == D3D12_RESOURCE_STATE_GENERIC_READ, "state");
_Static_assert(D3DP_STATE_PRESENT == D3D12_RESOURCE_STATE_PRESENT, "state");

#endif /* PORT_RHI_D3D12_D3D12_ENUMS_H */
