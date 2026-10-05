/* rhi_vk_enum_test.c: every rhi.h enumeration has a Vulkan mapping in
 * port/rhi/vk/vk_enums.h.
 *
 * Compile time: each table is sized by its enum's *_COUNT sentinel, so the
 * static asserts below fail if a table is declared with a different size.
 * Run time: each entry's `set` flag must be true, which catches an
 * enumerator added to rhi.h without a designated initializer here (C fills
 * the hole with zeroes, and zero is a valid Vulkan value for most enums).
 * Spot checks pin the mappings the GS emulation depends on. */
#include "../vk/vk_enums.h"
#include <stdio.h>

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

_Static_assert(COUNT_OF(vkr_formatMap) == RHI_FMT_COUNT, "format table size");

_Static_assert(COUNT_OF(vkr_vertexFormatMap) == RHI_VTX_COUNT, "vertex format table size");

_Static_assert(COUNT_OF(vkr_compareMap) == RHI_CMP_COUNT, "compare table size");

_Static_assert(COUNT_OF(vkr_blendFactorMap) == RHI_BF_COUNT, "blend factor table size");

_Static_assert(COUNT_OF(vkr_blendOpMap) == RHI_BO_COUNT, "blend op table size");

_Static_assert(COUNT_OF(vkr_stencilOpMap) == RHI_SO_COUNT, "stencil op table size");

_Static_assert(COUNT_OF(vkr_topologyMap) == RHI_TOPO_COUNT, "topology table size");

_Static_assert(COUNT_OF(vkr_loadOpMap) == RHI_LOAD_COUNT, "load op table size");

_Static_assert(COUNT_OF(vkr_bindTypeMap) == RHI_BIND_COUNT, "bind type table size");

_Static_assert(COUNT_OF(vkr_stateMap) == RHI_STATE_COUNT, "state table size");

/* Enumerators the RHI defines but that do not get a Vulkan-side table: the
 * small ones are mapped by arrays inside the backend.  Their counts are
 * pinned so a new value forces a look at vk_resource.c. */
_Static_assert(RHI_FILTER_LINEAR == 1, "RhiFilter: vk_resource.c maps 2 values");

_Static_assert(RHI_WRAP_CLAMP == 1, "RhiWrap: vk_resource.c maps 2 values");

_Static_assert(RHI_ASPECT_DEPTH == 1, "RhiViewAspect: 2 values");

_Static_assert(RHI_STAGE_FRAGMENT == 1, "RhiShaderStage: 2 values");

static int failures;

#define CHECK_SET(table, n)                                                                        \
    do {                                                                                           \
        for (unsigned i_ = 0; i_ < (unsigned)(n); i_++) {                                          \
            if (!(table)[i_].set) {                                                                \
                printf("FAIL %s[%u] has no mapping\n", #table, i_);                                \
                failures++;                                                                        \
            }                                                                                      \
        }                                                                                          \
    } while (0)
#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        if ((long long)(a) != (long long)(b)) {                                                    \
            printf("FAIL %s == %lld, expected %s == %lld\n", #a, (long long)(a), #b,               \
                   (long long)(b));                                                                \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    CHECK_SET(vkr_formatMap, RHI_FMT_COUNT);
    CHECK_SET(vkr_vertexFormatMap, RHI_VTX_COUNT);
    CHECK_SET(vkr_compareMap, RHI_CMP_COUNT);
    CHECK_SET(vkr_blendFactorMap, RHI_BF_COUNT);
    CHECK_SET(vkr_blendOpMap, RHI_BO_COUNT);
    CHECK_SET(vkr_stencilOpMap, RHI_SO_COUNT);
    CHECK_SET(vkr_topologyMap, RHI_TOPO_COUNT);
    CHECK_SET(vkr_loadOpMap, RHI_LOAD_COUNT);
    CHECK_SET(vkr_bindTypeMap, RHI_BIND_COUNT);
    CHECK_SET(vkr_stateMap, RHI_STATE_COUNT);

    /* every real format maps to a defined VkFormat with a texel size */
    for (unsigned i = 1; i < RHI_FMT_COUNT; i++) {
        if (vkr_formatMap[i].vk == VK_FORMAT_UNDEFINED || vkr_formatMap[i].texelBytes == 0 ||
            vkr_formatMap[i].aspect == 0) {
            printf("FAIL vkr_formatMap[%u] incomplete\n", i);
            failures++;
        }
    }

    /* what the GS emulation relies on (docs/port/RENDER_API.md "GS to pipeline mapping") */
    CHECK_EQ(vkr_compareMap[RHI_CMP_GEQUAL].vk, VK_COMPARE_OP_GREATER_OR_EQUAL);
    CHECK_EQ(vkr_compareMap[RHI_CMP_GREATER].vk, VK_COMPARE_OP_GREATER);
    CHECK_EQ(vkr_stencilOpMap[RHI_SO_INCR_WRAP].vk, VK_STENCIL_OP_INCREMENT_AND_WRAP);
    CHECK_EQ(vkr_stencilOpMap[RHI_SO_DECR_WRAP].vk, VK_STENCIL_OP_DECREMENT_AND_WRAP);
    CHECK_EQ(vkr_stencilOpMap[RHI_SO_INCR_CLAMP].vk, VK_STENCIL_OP_INCREMENT_AND_CLAMP);
    CHECK_EQ(vkr_blendFactorMap[RHI_BF_SRC1_COLOR].vk, VK_BLEND_FACTOR_SRC1_COLOR);
    CHECK_EQ(vkr_blendFactorMap[RHI_BF_ONE_MINUS_SRC1_COLOR].vk,
             VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR);
    CHECK_EQ(vkr_blendFactorMap[RHI_BF_SRC1_ALPHA].vk, VK_BLEND_FACTOR_SRC1_ALPHA);
    CHECK_EQ(vkr_blendFactorMap[RHI_BF_ONE_MINUS_SRC1_ALPHA].vk,
             VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA);
    CHECK_EQ(vkr_blendFactorMap[RHI_BF_CONSTANT].vk, VK_BLEND_FACTOR_CONSTANT_COLOR);
    CHECK_EQ(vkr_blendOpMap[RHI_BO_REVERSE_SUBTRACT].vk, VK_BLEND_OP_REVERSE_SUBTRACT);
    CHECK_EQ(vkr_formatMap[RHI_FMT_RGBA8_UINT].vk, VK_FORMAT_R8G8B8A8_UINT);
    CHECK_EQ(vkr_formatMap[RHI_FMT_RGBA8_UINT].isInteger, 1);
    CHECK_EQ(vkr_formatMap[RHI_FMT_RGBA8_UNORM].isInteger, 0);
    CHECK_EQ(vkr_formatMap[RHI_FMT_D32F_S8].vk, VK_FORMAT_D32_SFLOAT_S8_UINT);
    CHECK_EQ(vkr_formatMap[RHI_FMT_D32F_S8].aspect,
             VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    CHECK_EQ(vkr_formatMap[RHI_FMT_R8_UNORM].vk, VK_FORMAT_R8_UNORM);
    CHECK_EQ(vkr_vertexFormatMap[RHI_VTX_U8x4_UNORM].vk, VK_FORMAT_R8G8B8A8_UNORM);
    CHECK_EQ(vkr_StateLayout(RHI_STATE_SHADER_READ, RHI_FMT_D32F),
             VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    CHECK_EQ(vkr_StateLayout(RHI_STATE_SHADER_READ, RHI_FMT_RGBA8_UNORM),
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /* the bindings of one group never collide across register classes */
    for (unsigned a = 0; a < RHI_BIND_COUNT; a++) {
        for (unsigned b = 0; b < RHI_BIND_COUNT; b++) {
            unsigned sa = vkr_bindTypeMap[a].shift, sb = vkr_bindTypeMap[b].shift;
            if (sa != sb && (sa > sb ? sa - sb : sb - sa) < 16) {
                printf("FAIL bind shifts %u and %u overlap\n", sa, sb);
                failures++;
            }
        }
    }

    if (failures) {
        printf("rhi_vk_enum_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("rhi_vk_enum_test: all rhi.h enumerations map to Vulkan\n");
    return 0;
}
