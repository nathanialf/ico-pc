/* rhi_pass_row.h: which Vulkan drivers record their render passes as
 * VkRenderPass objects although they offer dynamic rendering (the driver
 * rows of port/rhi/vk/vk_device.c, g_vkr.legacyPasses: Intel's below and
 * Qualcomm's, rhi_vk_pass_row_qualcomm).  No Vulkan header:
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

/* The second row (issue 123): Qualcomm's own Vulkan driver (vendor 0x5143
 * with VK_DRIVER_ID_QUALCOMM_PROPRIETARY, or with no driver id), every
 * Adreno and every version; Mesa's Turnip on the same GPUs reports its own
 * id and keeps dynamic rendering.  The Adreno 740 of issue 123 (driver
 * 512.676) and the Adreno 710 of issue 211 (512.800) hang inside a render
 * pass about once in 500 pictures (the stall's length fits the kernel
 * driver's hardware hang detector, 218 million cycles: docs/port/
 * ANDROID_DIAGNOSTICS.md, "Issue 123"), and every such run recorded its
 * passes with vkCmdBeginRendering:
 * the VkRenderPass path (the driver's since Vulkan 1.0; the one the
 * port's Mali-G52 and PowerVR B-Series users always take) removes that
 * path as a cause with the same attachments, ops, layouts and barriers, so
 * the same picture.  Unlike the Intel row it does not move the VU record's
 * store passes onto a colour target (RhiLimits.storePassTarget stays the
 * Intel row's).  Android's alone (android nonzero on an Android build):
 * both reporters' devices are Android ones, so Qualcomm's driver on
 * Windows on Snapdragon keeps dynamic rendering (vk_device.c says so where
 * it would match).  NULL when the device keeps dynamic rendering; else the
 * reason, a phrase for the device line. */
const char *rhi_vk_pass_row_qualcomm(uint32_t vendorId, uint32_t driverId, int android);

/* The override's value: -1 none (NULL, empty, "auto" or anything else), 0
 * ("0", "false", "no", "off"), 1 ("1", "true", "yes", "on"); case does not
 * matter. */
int rhi_vk_pass_override(const char *value);

#endif /* PORT_RHI_RHI_PASS_ROW_H */
