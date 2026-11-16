# rd_hold.cmake: the paused scene's hold (rd_hold.h, rd_hold_key.c, rd_hold.c;
# docs/port/RENDER.md "The paused scene's hold"; issue 123), included from
# port/render/CMakeLists.txt after ico_render and rd_replay_tool.
target_sources(ico_render PRIVATE rd_hold_key.c)

# rd_hold_test: the CPU half on frames recorded without a device: the change
# counters (a texture and a mesh made, written with the same bytes, with others,
# destroyed), the lists key (another menu, the menu recorded first: the same;
# one scene colour word, one state command, one command more: another), the
# state key (a setting), the frame's rules (a plain frame, the aura's FEED128
# carried, the motion blur's DISPLAY, list 11 with a Z test, reading WORK0,
# drawn into WORK0, a keep frame) and the decision for every input
add_executable(rd_hold_test test/rd_hold_test.c)
target_compile_options(rd_hold_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_hold_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                "${CMAKE_SOURCE_DIR}/port/rhi"
                                                "${CMAKE_SOURCE_DIR}/port/shaders"
                                                "${CMAKE_SOURCE_DIR}/port/platform")
target_link_libraries(rd_hold_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_hold_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_hold COMMAND rd_hold_test)
endif()
