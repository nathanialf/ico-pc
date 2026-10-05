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

#ifdef __cplusplus
}

#endif
#endif /* PORT_RHI_VK_RHI_VK_H */
