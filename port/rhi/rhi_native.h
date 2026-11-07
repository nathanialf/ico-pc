/* rhi_native.h: the native handles external compute code needs (AMD
 * FidelityFX through its Vulkan and D3D12 backends: docs/port/RHI.md,
 * "Native handles for external compute").
 *
 * An additive surface beside rhi.h: nothing here changes what a caller that
 * never asks sees, and the RHI itself still has no compute stage.  The
 * handles are returned as plain integers and pointers so this header names no
 * Vulkan or D3D12 type; the caller casts by `kind`.  Every call is "not
 * offered" (returns false, writes zeros) on the D3D11 and OpenGL backends and
 * on a Vulkan or D3D12 device that lacks the feature the call needs.
 *
 * The state contract: the caller (rd) moves each input texture to
 * RHI_STATE_SHADER_READ and the output to RHI_STATE_STORAGE with
 * rhi_cmd_barrier before the external dispatch and back after it; the
 * external code is told those native states (RhiNativeTexture.nativeState)
 * and must leave every texture in the state it was given.  Between
 * rhi_cmd_external_begin and rhi_cmd_external_end no render pass is open and
 * the external code may record anything into the native command list. */
#ifndef PORT_RHI_RHI_NATIVE_H
#define PORT_RHI_RHI_NATIVE_H

#include "rhi.h"

typedef enum RhiNativeKind {
    RHI_NATIVE_NONE = 0, /* not offered by this backend */
    RHI_NATIVE_VULKAN = 1,
    RHI_NATIVE_D3D12 = 2
} RhiNativeKind;

/* The device behind the RHI.
 *   Vulkan: instance VkInstance, device VkDevice, physicalDevice
 *     VkPhysicalDevice, queue the one graphics VkQueue (queueFamily its
 *     family; the RHI submits to it, so an external submit must be
 *     externally synchronised with it: the FidelityFX backends record into
 *     the RHI's command buffer and never submit), getInstanceProcAddr and
 *     getDeviceProcAddr the PFN_vkGet*ProcAddr of the loader/volk in use
 *     (VkDevice-level pointer for getDeviceProcAddr), apiVersion the
 *     VkPhysicalDeviceProperties apiVersion of the physical device.
 *   D3D12: device ID3D12Device*, queue ID3D12CommandQueue* (direct), the
 *     rest 0/NULL.
 * The handles are borrowed: valid from rhi_init to rhi_shutdown, never
 * released by the caller. */
typedef struct RhiNativeDevice {
    RhiNativeKind kind;
    void *instance;
    void *device;
    void *physicalDevice;
    void *queue;
    uint32_t queueFamily;
    uint32_t apiVersion;
    void *getInstanceProcAddr;
    void *getDeviceProcAddr;
} RhiNativeDevice;

/* Writes the device's handles; false (and *out zeroed, kind NONE) when the
 * backend offers none or the device is not up. */
bool rhi_native_device(RhiNativeDevice *out);

/* One texture's native resource.
 *   Vulkan: image the VkImage (a uint64_t on every host), format the
 *     VkFormat, nativeState[s] the VkImageLayout a texture is in while in
 *     RhiState s (the layout the backend transitions to), state unused
 *     (stateTracked false: the Vulkan backend does not track layouts; the
 *     caller knows the state from its own rhi_cmd_barrier calls).
 *   D3D12: image the ID3D12Resource* (as an integer), format the
 *     DXGI_FORMAT of the resource (typeless for the colour formats), state
 *     the D3D12_RESOURCE_STATES the backend has recorded after the
 *     barriers recorded so far (stateTracked true), nativeState[s] the
 *     D3D12_RESOURCE_STATES of RhiState s.
 * storage: the texture was made with RHI_TEX_STORAGE and the backend
 * granted it (Vulkan STORAGE_BIT, D3D12 ALLOW_UNORDERED_ACCESS); the flag is
 * ignored where the device or format cannot (rhi.h RHI_TEX_STORAGE).
 * False (zeroed) for an invalid handle or a backend with no native form. */
typedef struct RhiNativeTexture {
    RhiNativeKind kind;
    uint64_t image;
    uint32_t format;
    uint32_t width, height, mips, samples;
    bool storage;
    bool stateTracked;
    uint32_t state;
    uint32_t nativeState[RHI_STATE_COUNT];
} RhiNativeTexture;

bool rhi_native_texture(RhiTexture t, RhiNativeTexture *out);

/* The native command list behind an RHI list: a VkCommandBuffer or an
 * ID3D12GraphicsCommandList*, written to *out.  Valid until the list is
 * submitted.  False for an invalid list or a backend with none. */
bool rhi_native_command_list(RhiCommandList cl, void **out);

/* The bracket around external recording into the native command list.  The
 * caller is outside a render pass (a call inside one is refused: returns
 * false and the pair is not open).
 *
 * begin: orders the work recorded so far before the external work (Vulkan: a
 * memory barrier, every earlier stage's writes to compute shader reads and
 * writes; the RHI's pending image barriers go out first; D3D12: nothing, the
 * resource barriers are already in the list) and marks every binding the
 * backend caches as lost.
 * end: orders the external work before what follows (Vulkan: a memory
 * barrier from compute to every stage) and re-establishes what the backend
 * assumes bound: D3D12 the two descriptor heaps and the pipeline state;
 * Vulkan the pipeline, with every descriptor set, the viewport, the scissor,
 * the dynamic state and the vertex/index buffers re-bound at the next draw.
 * The next render pass and draw then record exactly what they would have
 * without the pair. */
bool rhi_cmd_external_begin(RhiCommandList cl);
void rhi_cmd_external_end(RhiCommandList cl);

/* What a device offers external compute.  ok is the conjunction the
 * FidelityFX backends need; the others say why it is false.
 *   Vulkan: apiVersion >= 1.1, subgroup basic and arithmetic operations in
 *     compute shaders, storage-image support for RGBA16F, and the two
 *     storage-image-without-format features, which the RHI enables where
 *     offered.  float16 reports shaderFloat16 as OFFERED by the device;
 *     the RHI does not enable it (the external code needs a device made with
 *     it, which this device is not).
 *   D3D12: shader model 6.2 or later, wave operations, typed UAV loads of
 *     additional formats.  float16 is true with shader model 6.2 and native
 *     16-bit support (D3D12_OPTIONS4.Native16BitShaderOpsSupported).
 *   D3D11 and OpenGL: ok false, why "not offered". */
typedef struct RhiComputeCaps {
    bool ok;
    bool apiVersionOk;
    bool subgroupOk;
    bool storageFormatsOk;
    bool float16;
    const char *why; /* NULL when ok, else a short reason */
} RhiComputeCaps;

/* Fills *out and returns out->ok. */
bool rhi_device_compute_caps(RhiComputeCaps *out);

#endif /* PORT_RHI_RHI_NATIVE_H */
