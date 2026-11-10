/* rhi_vk_pass_row_test.c: the render pass driver row
 * (port/rhi/rhi_pass_row.c), pure: the reporter's Intel HD Graphics 530
 * (vendor 0x8086, Intel's Windows driver 101.2125 at Vulkan 1.3.215) takes
 * VkRenderPass objects, as does every Intel Windows driver at or below
 * 1.3.215; a newer Intel driver, Mesa's ANV on the same GPU, another vendor
 * at 1.3.215 and an Intel GPU on a non-Windows build without a driver id
 * keep dynamic rendering; the override's words.
 * Exit 0 on success, 1 on a failure. */
#include "rhi_pass_row.h"
#include "rhi_driver_version.h"
#include "../../test/ico_check.h"

#define ANV 6u /* VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA */

int main(void)
{
    const uint32_t v13215 = RHI_VK_API(1, 3, 215);
    CHECK(v13215 == ((1u << 22) | (3u << 12) | 215u));
    CHECK(RHI_VK_PASS_ROW_INTEL_API == v13215);
    /* the reporter's: driver id 5 (Intel proprietary Windows), and the id
       unknown on a Windows build */
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, RHI_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, v13215, 1) !=
          NULL);
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, RHI_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, v13215, 0) !=
          NULL);
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, 0, v13215, 1) != NULL);
    /* older versions of the same driver */
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, 0, RHI_VK_API(1, 3, 200), 1) != NULL);
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, 0, RHI_VK_API(1, 2, 182), 1) != NULL);
    /* newer: Arc and 11th gen on (1.3.250 and up, 1.4) */
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, RHI_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS,
                          RHI_VK_API(1, 3, 216), 1) == NULL);
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, 0, RHI_VK_API(1, 3, 289), 1) == NULL);
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, 0, RHI_VK_API(1, 4, 0), 1) == NULL);
    /* Mesa's ANV on the same GPU, a Linux build without an id */
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, ANV, v13215, 1) == NULL);
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, ANV, v13215, 0) == NULL);
    CHECK(rhi_vk_pass_row(RHI_VENDOR_INTEL, 0, v13215, 0) == NULL);
    /* other vendors */
    CHECK(rhi_vk_pass_row(RHI_VENDOR_NVIDIA, RHI_DRIVER_ID_NVIDIA_PROPRIETARY, v13215, 1) == NULL);
    CHECK(rhi_vk_pass_row(0x1002u, 0, v13215, 1) == NULL);
    CHECK(rhi_vk_pass_row(RHI_VENDOR_QUALCOMM, RHI_DRIVER_ID_QUALCOMM_PROPRIETARY,
                          RHI_VK_API(1, 1, 128), 0) == NULL);
    /* the reason names the driver and the call */
    {
        const char *why = rhi_vk_pass_row(RHI_VENDOR_INTEL, 0, v13215, 1);
        CHECK(why != NULL && strstr(why, "Intel") != NULL &&
              strstr(why, "vkCmdBeginRendering") != NULL);
    }
    /* the override */
    CHECK(rhi_vk_pass_override(NULL) == -1);
    CHECK(rhi_vk_pass_override("") == -1);
    CHECK(rhi_vk_pass_override("auto") == -1);
    CHECK(rhi_vk_pass_override("maybe") == -1);
    CHECK(rhi_vk_pass_override("1") == 1);
    CHECK(rhi_vk_pass_override("true") == 1);
    CHECK(rhi_vk_pass_override("TRUE") == 1);
    CHECK(rhi_vk_pass_override("on") == 1);
    CHECK(rhi_vk_pass_override("yes") == 1);
    CHECK(rhi_vk_pass_override("0") == 0);
    CHECK(rhi_vk_pass_override("false") == 0);
    CHECK(rhi_vk_pass_override("Off") == 0);
    CHECK(rhi_vk_pass_override("no") == 0);
    CHECK(rhi_vk_pass_override("10") == -1);
    CHECK(rhi_vk_pass_override("truer") == -1);
    return ico_check_summary("rhi_vk_pass_row");
}
