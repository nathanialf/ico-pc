/* rhi_driver_version.h: a GPU driver's version number as people know it,
 * from the 32-bit driverVersion a Vulkan device reports. No Vulkan header:
 * built and tested on every host (port/rhi/test/rhi_driver_version_test.c).
 *
 * Each vendor packs the number its own way:
 *   NVIDIA's own driver: major 10 bits, minor 8, a third part 8, a fourth 6
 *     (581.57 is (581 << 22) | (57 << 14));
 *   Intel's Windows driver: major 18 bits, minor 14 (101.5333 is
 *     (101 << 14) | 5333);
 *   every other driver: the Vulkan version packing, major.minor.patch
 *     (major 10 bits, minor 10, patch 12).
 * driverId is VkDriverId (VK_KHR_driver_properties, core in Vulkan 1.2),
 * which tells NVIDIA's own driver from Mesa's NVK on the same vendor id;
 * 0 when unknown, and then the vendor id alone decides. */
#ifndef PORT_RHI_RHI_DRIVER_VERSION_H
#define PORT_RHI_RHI_DRIVER_VERSION_H

#include <stddef.h>
#include <stdint.h>

#define RHI_VENDOR_NVIDIA 0x10DEu
#define RHI_VENDOR_INTEL 0x8086u
#define RHI_VENDOR_QUALCOMM 0x5143u
#define RHI_VENDOR_APPLE 0x106Bu
/* VkDriverId values */
#define RHI_DRIVER_ID_NVIDIA_PROPRIETARY 4u
#define RHI_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS 5u
#define RHI_DRIVER_ID_QUALCOMM_PROPRIETARY 8u
#define RHI_DRIVER_ID_MOLTENVK 14u

/* The text into buf (always terminated when n > 0); returns buf. */
char *rhi_driver_version_text(uint32_t vendorId, uint32_t driverId, uint32_t driverVersion,
                              char *buf, size_t n);

/* 1 for Qualcomm's own Adreno driver: vendor 0x5143 with
 * VK_DRIVER_ID_QUALCOMM_PROPRIETARY, or with the id unknown (0).  Mesa's
 * Turnip on the same GPUs reports its own id (MESA_TURNIP) and is not
 * covered.  Its rasterization-order attachment read returns wrong
 * destination colours (issue 89: Adreno 830, driver 512.800.72), so
 * vk_device.c does not offer that read on it. */
int rhi_driver_is_qualcomm_proprietary(uint32_t vendorId, uint32_t driverId);

/* 1 for MoltenVK (Vulkan over Metal): VK_DRIVER_ID_MOLTENVK, or, on a build
 * that links MoltenVK (moltenvkBuild nonzero), Apple's vendor id 0x106B with
 * the id unknown (0).  The renderer's default blend path on it is the
 * fixed-function one (port/render/rd_dst_read.c rd__dst_init; issue 102). */
int rhi_driver_is_moltenvk(uint32_t vendorId, uint32_t driverId, int moltenvkBuild);

/* The bytes one pixel of a pass's colour targets may take together on the
 * Apple GPU MoltenVK names (VkPhysicalDeviceProperties.deviceName, Metal's
 * MTLDevice name: "Apple A10 GPU", "Apple M1 Pro"): Metal's implicit image
 * block a pixel and sample with several colour targets (Metal Feature Set
 * Tables, "Maximum implicit image block size, per pixel, per sample, when
 * using multiple color render targets"): 32 for the Apple2 and Apple3
 * families (A8 to A10), 64 for Apple4 to Apple6 (A11 to A13), 128 from
 * Apple7 (A14, M1) on.  0 for any other name (an Intel or AMD GPU of a
 * Mac, which states no such bound), and for a null one (RhiLimits
 * colorBytesPerPixel). */
uint32_t rhi_metal_color_bytes(const char *deviceName);

#endif /* PORT_RHI_RHI_DRIVER_VERSION_H */
