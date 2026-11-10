/* rhi_pass_row.h: which Vulkan drivers record their render passes as
 * VkRenderPass objects although they offer dynamic rendering (the driver
 * row of port/rhi/vk/vk_device.c, g_vkr.legacyPasses).  No Vulkan header:
 * built and tested on every host (port/rhi/test/rhi_vk_pass_row_test.c).
 *
 * The row: Intel's own Windows driver (vendor 0x8086 with
 * VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, or with the id unknown on a
 * Windows build) at Vulkan 1.3.215 or older, the version of Intel's final
 * drivers for its 6th to 10th generation graphics (101.2115 to 101.2141).
 * An Intel HD Graphics 530 on driver 101.2125 (Vulkan 1.3.215) crashed in
 * vkCmdBeginRendering (igvk64.dll+0x5cfab9, reading 0x68) in the first
 * picture after a live switch from 1x to 2x; other ports report the same
 * address on the same drivers (driveclub-recomp issue 315, SavageWindRecomp
 * issue 622, totk-pc-recomp issue 4).  The VkRenderPass path never calls
 * vkCmdBeginRendering, and loses nothing there: those drivers have no local
 * read (the one feature tied to dynamic rendering).
 *
 * The override: ICO_VK_LEGACY_PASSES (ico-pc.ini [dev] legacy_passes): 1 or
 * true VkRenderPass objects on any device, 0 or false dynamic rendering
 * wherever the device offers it (the row off); anything else, or unset,
 * the row. */
#ifndef PORT_RHI_RHI_PASS_ROW_H
#define PORT_RHI_RHI_PASS_ROW_H

#include <stdint.h>

/* Vulkan's version packing (VK_MAKE_API_VERSION with variant 0) */
#define RHI_VK_API(major, minor, patch)                                                            \
    (((uint32_t)(major) << 22) | ((uint32_t)(minor) << 12) | (uint32_t)(patch))

/* the newest API version the row covers */
#define RHI_VK_PASS_ROW_INTEL_API RHI_VK_API(1, 3, 215)

/* NULL when the device keeps dynamic rendering; else the reason, a phrase
 * for the device line ("Intel's Windows driver at Vulkan 1.3.215 or older,
 * ..."). vendorId and driverId as rhi_driver_version.h's; apiVersion the
 * device's (VkPhysicalDeviceProperties.apiVersion); windowsBuild nonzero on
 * a Windows build. */
const char *rhi_vk_pass_row(uint32_t vendorId, uint32_t driverId, uint32_t apiVersion,
                            int windowsBuild);

/* The override's value: -1 none (NULL, empty, "auto" or anything else), 0
 * ("0", "false", "no", "off"), 1 ("1", "true", "yes", "on"); case does not
 * matter. */
int rhi_vk_pass_override(const char *value);

#endif /* PORT_RHI_RHI_PASS_ROW_H */
