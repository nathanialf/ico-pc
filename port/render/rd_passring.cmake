# rd_passring.cmake: ico_passring, the pass ring (rd_passring.h: what the GPU
# was last given to do), included from port/render/CMakeLists.txt beside
# ico_bisect_plan, before the renderer's own targets (a file of its own:
# port/render/CMakeLists.txt is at the 256 KiB cap, tools/check_no_rom.sh).
# Pure C, built in every configuration: the renderer (ico_render, which links
# it) writes it, and the platform's crash report (port/platform/diag_host.c
# report_gpu_passes) prints its last entries beside the render pass
# breadcrumb (port/rhi/rhi_pass_crumb.h), so ico_platform links it too.
add_library(ico_passring STATIC rd_passring.c)
target_include_directories(ico_passring PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_passring PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_platform PUBLIC ico_passring)
