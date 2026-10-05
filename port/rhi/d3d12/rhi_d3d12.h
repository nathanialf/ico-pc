/* rhi_d3d12.h: test-only extras of the D3D12 backend; port/render never
 * includes it. */
#ifndef PORT_RHI_D3D12_RHI_D3D12_H
#define PORT_RHI_D3D12_RHI_D3D12_H

#include <stdbool.h>
#include <stdint.h>

/* Errors and corruption messages the D3D12 debug layer reported since the
 * last rhi_Init (still readable after rhi_Shutdown).  0 when the debug layer
 * is not installed: see rhi_d3d12_DebugLayerActive. */
uint32_t rhi_d3d12_DebugErrorCount(void);
bool rhi_d3d12_DebugLayerActive(void);
/* The device runs on WARP, the software adapter (still true after
 * rhi_Shutdown, until the next rhi_Init). */
bool rhi_d3d12_IsWarp(void);

#endif /* PORT_RHI_D3D12_RHI_D3D12_H */
