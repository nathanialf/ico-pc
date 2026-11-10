/* vk_spirv_scan.h: what a SPIR-V module may touch, read from its words
 * (pure: no Vulkan, no device).
 *
 * The Vulkan backend's buffer barriers wait for the earlier reads of a
 * buffer at the stages that may read it (vk_cmd.c vkr_buf_earlier_stages).
 * A fragment shader can read a storage buffer only through a resource it
 * declares: a variable or pointer in the StorageBuffer (or
 * PhysicalStorageBuffer) storage class, a block decorated BufferBlock (the
 * SPIR-V 1.0 form DXC emits for a StructuredBuffer under vulkan1.0), or a
 * buffer device address (the PhysicalStorageBufferAddresses capability).
 * vkr_spirv_scan reports whether the module has a fragment entry point and
 * whether it declares any of those; rhi_create_shader (vk_resource.c) notes
 * the first fragment module that does, and from then on the barriers wait
 * for fragment work too.  Before it no fragment shader of the run can read a
 * storage buffer, so no barrier has to wait for one.
 *
 * Every word is checked against the module's length: a truncated or
 * malformed module (or one that is not SPIR-V) returns false, and the caller
 * then counts it as one that may read storage buffers. */
#ifndef PORT_RHI_VK_VK_SPIRV_SCAN_H
#define PORT_RHI_VK_VK_SPIRV_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct VkrSpirvUse {
    bool fragment;       /* an OpEntryPoint with the Fragment execution model */
    bool storageBuffers; /* a storage buffer declared (above) */
} VkrSpirvUse;

/* words: the module (words[0] the magic 0x07230203), count: its length in
 * words.  False (and *out all false) when the module cannot be read whole. */
bool vkr_spirv_scan(const uint32_t *words, size_t count, VkrSpirvUse *out);

#endif
