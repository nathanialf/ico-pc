# rd_display_change_2x.cmake: rd_display_change_2x_test and its rows,
# included from port/render/CMakeLists.txt inside if(ICO_RUN_TESTS), after
# the render tests' validation environment (_env). A file of its own:
# port/render/CMakeLists.txt is at the 256 KiB cap (tools/check_no_rom.sh).
#
# The Intel HD 530 crash (issue 123's report, v0.11.1: igvk64.dll inside
# vkCmdBeginRendering in the first picture after a live switch from 1x to 2x;
# port/rhi/rhi_pass_row.h): a live Original 1x -> Enhanced 2x switch with the
# render thread, the frame holding VU meshes through the record-stores
# pre-pass (the store passes on their throwaway target, rd_vu_rec_sink.h), a
# DATE draw, the shadow count, the fog and the crack fill, under the
# validation layer when the toolchain has it: no validation error, every part
# drawn after the switch, the render pass breadcrumb (rhi_pass_crumb.h) of the
# last pass.
#   rd_display_change_2x         dynamic rendering (lavapipe's Vulkan 1.4)
#   rd_display_change_2x_legacy  the reporter's path: a Vulkan 1.3+ device with
#                                its passes as VkRenderPass objects
#                                (ICO_VK_LEGACY_PASSES=1, [dev] legacy_passes,
#                                the Intel driver row's)
#   rd_display_change_2x_vk11    through a Vulkan 1.1 driver's absences
#                                (ICO_VK_FAKE_API=1.1: VkRenderPass objects,
#                                the fence ring), label vk11
add_executable(rd_display_change_2x_test test/rd_display_change_2x_test.c)
target_compile_options(rd_display_change_2x_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                       ${ICO_PORT_WARNINGS})
target_include_directories(rd_display_change_2x_test PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_display_change_2x_test PRIVATE ico_render ico_pass_crumb)
ico_copy_sdl3_dll(rd_display_change_2x_test)

set(_dc2 ${_env} "ICO_RD_VU_PREPASS=1" "ICO_RD_VU_RECORD=stores")
set(_dc2_legacy ${_dc2} "ICO_VK_LEGACY_PASSES=1")
set(_dc2_vk11 ${_dc2} "ICO_VK_FAKE_API=1.1")
add_test(NAME rd_display_change_2x COMMAND rd_display_change_2x_test)
add_test(NAME rd_display_change_2x_legacy COMMAND rd_display_change_2x_test --legacy)
add_test(NAME rd_display_change_2x_vk11 COMMAND rd_display_change_2x_test --legacy)
set_tests_properties(rd_display_change_2x rd_display_change_2x_legacy rd_display_change_2x_vk11
                     PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(rd_display_change_2x PROPERTIES ENVIRONMENT "${_dc2}")
set_tests_properties(rd_display_change_2x_legacy PROPERTIES ENVIRONMENT "${_dc2_legacy}")
set_tests_properties(rd_display_change_2x_vk11 PROPERTIES ENVIRONMENT "${_dc2_vk11}")
get_test_property(rd_display_change_2x_vk11 LABELS _labels)
set_tests_properties(rd_display_change_2x_vk11 PROPERTIES LABELS "${_labels};vk11")
