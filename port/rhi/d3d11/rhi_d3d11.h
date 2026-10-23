/* rhi_d3d11.h: test-only extras of the Direct3D 11 backend (as
 * rhi_d3d12.h); port/render never includes it.  The adapter is chosen with
 * ICO_D3D11_ADAPTER (warp, an index or a part of the adapter's name), the
 * debug layer with ICO_D3D11_DEBUG=0/1, as the D3D12 backend's
 * ICO_D3D12_ADAPTER and ICO_D3D12_DEBUG. */
#ifndef PORT_RHI_D3D11_RHI_D3D11_H
#define PORT_RHI_D3D11_RHI_D3D11_H

#include <stdbool.h>
#include <stdint.h>

/* Errors and corruption messages the D3D11 debug layer reported since the
 * last rhi_init (still readable after rhi_shutdown).  0 when the debug layer
 * is not installed: see rhi_d3d11_debug_layer_active. */
uint32_t rhi_d3d11_debug_error_count(void);
bool rhi_d3d11_debug_layer_active(void);
/* The device runs on WARP, the software rasteriser (D3D_DRIVER_TYPE_WARP;
 * still true after rhi_shutdown, until the next rhi_init). */
bool rhi_d3d11_is_warp(void);
/* The feature level the device was made at (0xb000 for 11_0, 0xb100 for
 * 11_1; 0 before the first rhi_init). */
uint32_t rhi_d3d11_feature_level(void);

#endif /* PORT_RHI_D3D11_RHI_D3D11_H */
