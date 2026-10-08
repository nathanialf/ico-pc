/* rhi_vk.h: Vulkan-backend extras outside rhi.h, for tests and tools only.
 * Nothing in port/render may include it. */
#ifndef PORT_RHI_VK_RHI_VK_H
#define PORT_RHI_VK_RHI_VK_H

#include <stdint.h>

#ifdef __cplusplus

extern "C" {
#endif

/* Number of validation-layer errors reported since rhi_Init (still readable
 * after rhi_Shutdown).  Always 0 when the layer is not installed. */
uint32_t rhi_vk_ValidationErrorCount(void);

/* Package AN-D, for rhi_vk_swapchain_test: the next rhi_Present reports
 * `result` (a VkResult value) instead of what the driver returned, once;
 * the image is still presented.  RHI_VK_TEST_SUBOPTIMAL takes the
 * suboptimal path (a recreation only when the size changed),
 * RHI_VK_TEST_SURFACE_LOST the lost-surface path (a new surface and
 * swapchain).  vkr_TestSwapchainCreations counts the swapchains created
 * since the program started. */
#define RHI_VK_TEST_SUBOPTIMAL 1000001003      /* VK_SUBOPTIMAL_KHR */
#define RHI_VK_TEST_SURFACE_LOST (-1000000000) /* VK_ERROR_SURFACE_LOST_KHR */
#define RHI_VK_TEST_OUT_OF_DATE (-1000001004)  /* VK_ERROR_OUT_OF_DATE_KHR */
void vkr_TestForcePresentResult(int32_t result);
uint32_t vkr_TestSwapchainCreations(void);

#ifdef __cplusplus
}

#endif
#endif /* PORT_RHI_VK_RHI_VK_H */
