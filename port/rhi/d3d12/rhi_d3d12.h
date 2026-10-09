/* rhi_d3d12.h: test-only extras of the D3D12 backend; port/render never
 * includes it. */
#ifndef PORT_RHI_D3D12_RHI_D3D12_H
#define PORT_RHI_D3D12_RHI_D3D12_H

#include <stdbool.h>
#include <stdint.h>

/* Errors and corruption messages the D3D12 debug layer reported since the
 * last rhi_init (still readable after rhi_shutdown).  0 when the debug layer
 * is not installed: see rhi_d3d12_debug_layer_active. */
uint32_t rhi_d3d12_debug_error_count(void);
bool rhi_d3d12_debug_layer_active(void);
/* The device runs on WARP, the software adapter (still true after
 * rhi_shutdown, until the next rhi_init). */
bool rhi_d3d12_is_warp(void);

#endif /* PORT_RHI_D3D12_RHI_D3D12_H */
