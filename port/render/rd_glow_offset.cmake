# rd_glow_offset.cmake: how far the held glow's camera is from the present's
# (rd_glow_offset.c, rd_glow_offset.h; issue 123's right wall), on the glow's
# 250-frame line (rd_glow_hold.c), included at the end of
# port/render/CMakeLists.txt (that file is near the 256 KiB cap).
target_sources(ico_render PRIVATE rd_glow_offset.c)

# rd_glow_offset_test: CPU only: a screen-view against itself, turned by a
# yaw (384 GS pixels) and a pitch (128), its eye moved alone (0), the centre
# direction behind the other's eye refused; the median and largest over a
# window, the refused counted, the line word for word
add_executable(rd_glow_offset_test test/rd_glow_offset_test.c rd_glow_offset.c)
target_include_directories(rd_glow_offset_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_glow_offset_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(rd_glow_offset_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_glow_offset COMMAND rd_glow_offset_test)
endif()
