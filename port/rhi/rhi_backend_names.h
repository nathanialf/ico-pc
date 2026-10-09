/* rhi_backend_names.h: renames rhi.h's entry points for a backend library.
 *
 * rhi.h includes this file when RHI_BACKEND_PREFIX is defined (each backend
 * library is compiled with it: vk, d3d12), so a backend's definition of
 * rhi_init becomes rhi_<prefix>_init and so on; port/rhi/rhi_backend.h
 * describes the scheme.  Every entry point a backend implements
 * (RHI_BACKEND_FUNCS, rhi_backend.h) must be listed here.
 */
#ifndef PORT_RHI_RHI_BACKEND_NAMES_H
#define PORT_RHI_RHI_BACKEND_NAMES_H

#ifdef RHI_BACKEND_PREFIX

#define RHI__CAT3(a, b, c) a##b##c
#define RHI__XCAT3(a, b, c) RHI__CAT3(a, b, c)
#define RHI__NAME(fn) RHI__XCAT3(rhi_, RHI_BACKEND_PREFIX, _##fn)

/* clang-format off */
#define rhi_init                RHI__NAME(init)
#define rhi_shutdown            RHI__NAME(shutdown)
#define rhi_backend             RHI__NAME(backend)
#define rhi_limits              RHI__NAME(limits)
#define rhi_adapter_name         RHI__NAME(adapter_name)
#define rhi_device_lost          RHI__NAME(device_lost)
#define rhi_resize_swapchain     RHI__NAME(resize_swapchain)
#define rhi_swapchain_format     RHI__NAME(swapchain_format)
#define rhi_swapchain_size       RHI__NAME(swapchain_size)
#define rhi_surface_poll_restart  RHI__NAME(surface_poll_restart)
#define rhi_acquire_backbuffer   RHI__NAME(acquire_backbuffer)
#define rhi_present             RHI__NAME(present)
#define rhi_release_surface      RHI__NAME(release_surface)
#define rhi_recreate_surface     RHI__NAME(recreate_surface)
#define rhi_create_buffer        RHI__NAME(create_buffer)
#define rhi_destroy_buffer       RHI__NAME(destroy_buffer)
#define rhi_map_buffer           RHI__NAME(map_buffer)
#define rhi_unmap_buffer         RHI__NAME(unmap_buffer)
#define rhi_create_texture       RHI__NAME(create_texture)
#define rhi_destroy_texture      RHI__NAME(destroy_texture)
#define rhi_create_sampler       RHI__NAME(create_sampler)
#define rhi_destroy_sampler      RHI__NAME(destroy_sampler)
#define rhi_create_shader        RHI__NAME(create_shader)
#define rhi_destroy_shader       RHI__NAME(destroy_shader)
#define rhi_create_bind_group_layout  RHI__NAME(create_bind_group_layout)
#define rhi_destroy_bind_group_layout RHI__NAME(destroy_bind_group_layout)
#define rhi_create_bind_group     RHI__NAME(create_bind_group)
#define rhi_create_pipeline      RHI__NAME(create_pipeline)
#define rhi_destroy_pipeline     RHI__NAME(destroy_pipeline)
#define rhi_begin_commands       RHI__NAME(begin_commands)
#define rhi_end_commands         RHI__NAME(end_commands)
#define rhi_submit              RHI__NAME(submit)
#define rhi_wait_frame           RHI__NAME(wait_frame)
#define rhi_frame_slot           RHI__NAME(frame_slot)
#define rhi_wait_idle            RHI__NAME(wait_idle)
#define rhi_cmd_barrier          RHI__NAME(cmd_barrier)
#define rhi_cmd_begin_render_pass  RHI__NAME(cmd_begin_render_pass)
#define rhi_cmd_end_render_pass    RHI__NAME(cmd_end_render_pass)
#define rhi_cmd_set_viewport      RHI__NAME(cmd_set_viewport)
#define rhi_cmd_set_scissor       RHI__NAME(cmd_set_scissor)
#define rhi_cmd_set_pipeline      RHI__NAME(cmd_set_pipeline)
#define rhi_cmd_set_bind_group     RHI__NAME(cmd_set_bind_group)
#define rhi_cmd_set_bind_group_offsets RHI__NAME(cmd_set_bind_group_offsets)
#define rhi_cmd_set_vertex_buffer  RHI__NAME(cmd_set_vertex_buffer)
#define rhi_cmd_set_index_buffer   RHI__NAME(cmd_set_index_buffer)
#define rhi_cmd_set_stencil_ref    RHI__NAME(cmd_set_stencil_ref)
#define rhi_cmd_set_blend_constant RHI__NAME(cmd_set_blend_constant)
#define rhi_cmd_draw             RHI__NAME(cmd_draw)
#define rhi_cmd_draw_indexed      RHI__NAME(cmd_draw_indexed)
#define rhi_cmd_copy_buffer       RHI__NAME(cmd_copy_buffer)
#define rhi_cmd_copy_buffer_to_texture RHI__NAME(cmd_copy_buffer_to_texture)
#define rhi_cmd_copy_texture      RHI__NAME(cmd_copy_texture)
#define rhi_cmd_copy_texture_to_buffer RHI__NAME(cmd_copy_texture_to_buffer)
#define rhi_cmd_begin_label       RHI__NAME(cmd_begin_label)
#define rhi_cmd_end_label         RHI__NAME(cmd_end_label)
#define rhi_readback_texture     RHI__NAME(readback_texture)
#define rhi_get_stats            RHI__NAME(get_stats)
#define rhi_timestamps_supported RHI__NAME(timestamps_supported)
#define rhi_cmd_write_timestamp   RHI__NAME(cmd_write_timestamp)
#define rhi_read_timestamps      RHI__NAME(read_timestamps)
#define rhi_prefer_mailbox       RHI__NAME(prefer_mailbox)
#define rhi_present_mailbox      RHI__NAME(present_mailbox)
#define rhi_present_mode_name     RHI__NAME(present_mode_name)
#define rhi_set_pipeline_cache_path RHI__NAME(set_pipeline_cache_path)
#define rhi_set_vulkan_loader     RHI__NAME(set_vulkan_loader)
#define rhi_injector_name        RHI__NAME(injector_name)
#define rhi_overlay_name         RHI__NAME(overlay_name)
/* clang-format on */

#endif /* RHI_BACKEND_PREFIX */

#endif /* PORT_RHI_RHI_BACKEND_NAMES_H */
