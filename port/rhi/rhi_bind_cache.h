/* rhi_bind_cache.h: the bind cache's off switch, shared by every backend
 * (docs/port/RHI.md "The bind cache").  No graphics header: each backend
 * library compiles rhi_bind_cache.c (a static library member, taken once),
 * so the replay tool and the tests read the same switch.
 *
 * ICO_RHI_BIND_CACHE=0 (also false, off, no), which [dev] bind_cache =
 * false exports (port/platform/host_config.c dev_env), turns off every skip
 * of a bind or a state set that asks for what a command list already holds:
 * the pipeline, the bind groups and their offsets, the viewport, the
 * scissor, the vertex and index buffers, the stencil reference and the
 * blend constant, the dynamic depth and blend commands found in force, and
 * the objects a Direct3D 11 pipeline change leaves set.  Off, every bind is
 * recorded as given (a pipeline bind on Vulkan binds every descriptor set
 * again, whatever the layouts share), so a device whose driver mishandles a
 * kept binding can be bisected without a build.  Read once from the
 * environment, at the backend's rhi_init (the start-up line) before any
 * list is recorded. */
#ifndef PORT_RHI_RHI_BIND_CACHE_H
#define PORT_RHI_RHI_BIND_CACHE_H

#include <stdbool.h>

/* true (the default) unless ICO_RHI_BIND_CACHE says 0, false, off or no */
bool rhi_bind_cache_enabled(void);

/* the start-up line's words after "bind cache: ": "on", or "off ([dev]
 * bind_cache = false, ICO_RHI_BIND_CACHE=0)" */
const char *rhi_bind_cache_words(void);

#endif /* PORT_RHI_RHI_BIND_CACHE_H */
