# rd_pass_area.cmake: the frame the pass area's A/B needs (rd_pass_area.c;
# docs/port/WORK_BUFFERS.md, "The pass area"), included at the end of
# port/render/CMakeLists.txt (a file of its own: that file is near the 256 KiB
# cap, tools/check_no_rom.sh).
#
# rd_pass_area_frame (test/rd_pass_area_frame.c): records, without a device,
# a frame whose passes on SCENE take render areas (flat sprites after a draw
# on another target, no clear left to take) and dumps it; the fixture
# fx-pass-area is made from that dump (docs/port/TESTING_FIXTURES.md,
# "Hand-writing a fixture"), and rd_pass_area_ab replays the fixture.  The
# ctest of the same name writes the dump into the build tree (CPU only).
add_executable(rd_pass_area_frame test/rd_pass_area_frame.c)
target_compile_options(rd_pass_area_frame PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_pass_area_frame PRIVATE "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_pass_area_frame PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_pass_area_frame)
endif()
if(ICO_RUN_TESTS)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pass_area_frame")
    add_test(NAME rd_pass_area_frame
             COMMAND rd_pass_area_frame "${CMAKE_CURRENT_BINARY_DIR}/pass_area_frame")
endif()
