/* rhi_backend.h: how several backends of port/rhi/rhi.h share one build.
 *
 * Each backend library is compiled with RHI_BACKEND_PREFIX set to its short
 * name (vk, d3d12).  rhi.h then includes rhi_backend_names.h first, whose
 * macros rename every rhi_* entry point the backend defines to
 * rhi_<prefix>_*, so the backend's sources keep the plain rhi.h names.  The
 * backend registers its functions once with RHI_BACKEND_DEFINE (one line,
 * port/rhi/vk/vk_device.c and port/rhi/d3d12/d3d12_device.c), and
 * port/rhi/rhi_backend.c defines the real rhi_* functions as forwarders to
 * the table rhi_create_backend selected.
 *
 * Callers never include this header; it is for backends and the dispatcher.
 */
#ifndef PORT_RHI_RHI_BACKEND_H
#define PORT_RHI_RHI_BACKEND_H

#include "rhi.h"

/* X(name): every entry point of rhi.h a backend implements, in the order of
 * RhiBackendTable. */
#define RHI_BACKEND_FUNCS(X)                                                                       \
    X(init)                                                                                        \
    X(shutdown)                                                                                    \
    X(backend)                                                                                     \
    X(limits)                                                                                      \
    X(adapter_name)                                                                                \
    X(device_lost)                                                                                 \
    X(resize_swapchain)                                                                            \
    X(swapchain_format)                                                                            \
    X(swapchain_size)                                                                              \
    X(surface_poll_restart)                                                                        \
    X(acquire_backbuffer)                                                                          \
    X(present)                                                                                     \
    X(release_surface)                                                                             \
    X(recreate_surface)                                                                            \
    X(create_buffer)                                                                               \
    X(destroy_buffer)                                                                              \
    X(map_buffer)                                                                                  \
    X(unmap_buffer)                                                                                \
    X(create_texture)                                                                              \
    X(destroy_texture)                                                                             \
    X(create_sampler)                                                                              \
    X(destroy_sampler)                                                                             \
    X(create_shader)                                                                               \
    X(destroy_shader)                                                                              \
    X(create_bind_group_layout)                                                                    \
    X(destroy_bind_group_layout)                                                                   \
    X(create_bind_group)                                                                           \
    X(create_pipeline)                                                                             \
    X(destroy_pipeline)                                                                            \
    X(begin_commands)                                                                              \
    X(end_commands)                                                                                \
    X(submit)                                                                                      \
    X(wait_frame)                                                                                  \
    X(frame_slot)                                                                                  \
    X(wait_idle)                                                                                   \
    X(collect_garbage_now)                                                                         \
    X(cmd_barrier)                                                                                 \
    X(cmd_begin_render_pass)                                                                       \
    X(cmd_end_render_pass)                                                                         \
    X(cmd_set_viewport)                                                                            \
    X(cmd_set_scissor)                                                                             \
    X(cmd_set_pipeline)                                                                            \
    X(cmd_set_bind_group)                                                                          \
    X(cmd_set_bind_group_offsets)                                                                  \
    X(cmd_set_vertex_buffer)                                                                       \
    X(cmd_set_index_buffer)                                                                        \
    X(cmd_set_stencil_ref)                                                                         \
    X(cmd_set_blend_constant)                                                                      \
    X(cmd_draw)                                                                                    \
    X(cmd_draw_indexed)                                                                            \
    X(cmd_copy_buffer)                                                                             \
    X(cmd_copy_buffer_to_texture)                                                                  \
    X(cmd_copy_texture)                                                                            \
    X(cmd_copy_texture_to_buffer)                                                                  \
    X(cmd_begin_label)                                                                             \
    X(cmd_end_label)                                                                               \
    X(readback_texture)                                                                            \
    X(get_stats)                                                                                   \
    X(timestamps_supported)                                                                        \
    X(cmd_write_timestamp)                                                                         \
    X(read_timestamps)                                                                             \
    X(prefer_mailbox)                                                                              \
    X(present_mailbox)                                                                             \
    X(present_mode_name)                                                                           \
    X(set_pipeline_cache_path)                                                                     \
    X(set_vulkan_loader)                                                                           \
    X(injector_name)                                                                               \
    X(overlay_name)

/* One function pointer per entry point, typed from rhi.h's declaration (in
 * a backend's sources rhi_##n pastes to the renamed function, which has the
 * same type). */
#define RHI__TABLE_FIELD(n) __typeof__(rhi_##n) *n;

typedef struct RhiBackendTable {
    const char *name; /* "vulkan", "d3d12": the rhi_create_backend name */
    RHI_BACKEND_FUNCS(RHI__TABLE_FIELD)
} RhiBackendTable;

/* In a backend source compiled with RHI_BACKEND_PREFIX: defines the table
 * `sym` with every entry point of this backend. */
#define RHI__TABLE_INIT(n) .n = rhi_##n,
#define RHI_BACKEND_DEFINE(sym, nameString)                                                        \
    const RhiBackendTable sym = {.name = (nameString), RHI_BACKEND_FUNCS(RHI__TABLE_INIT)}

extern const RhiBackendTable rhi_backend_vk;
extern const RhiBackendTable rhi_backend_d3d12;

#endif /* PORT_RHI_RHI_BACKEND_H */
