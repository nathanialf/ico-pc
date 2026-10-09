# port/input: the input layers (Phase 4C): the libpad
# subset (pad_host.c), the pad script reader (pad_script.c, the headless and
# test source), the virtual pad's arithmetic (vpad.c), the binding layer
# (bindings.c, input_config.c), the touch overlay's mapper (touch.c) and the
# mouse camera's capture rule and photo look accumulator (mouse_look.c), the
# camera speed and range the game's hand camera asks for (mouse_camera.c) and
# the menus' mouse pointer (pointer.c, v0.4.3 I17b). The SDL3 device layer
# (input_sdl.c) is part of the window build only; the headless build has no
# SDL dependency.
add_library(ico_input STATIC
    bindings.c
    input_config.c
    input_record.c
    mouse_camera.c
    mouse_look.c
    pad_host.c
    pad_script.c
    pointer.c
    touch.c
    vpad.c)
target_include_directories(ico_input
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${ICO_COMPAT_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(ico_input PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
# stick fix and mirror are port/game/options.c's
target_link_libraries(ico_input PUBLIC ico_options)
# input_record.c opens its file through host_fs (UTF-8 paths on Windows)
target_link_libraries(ico_input PRIVATE ico_host_fs)
if(NOT ICO_HEADLESS AND TARGET SDL3::SDL3-shared)
    target_sources(ico_input PRIVATE input_sdl.c)
    target_link_libraries(ico_input PUBLIC SDL3::SDL3-shared)
endif()

# binding resolution, quantisation, the stick fix, merging, the libpad buffer
# and rumble. config.toml's reader lives in host_config.c, which ico_pc
# compiles itself, so the test compiles it too.
add_executable(input_test test/input_test.c ../platform/host_config.c)
target_include_directories(input_test PRIVATE "${ICO_COMPAT_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(input_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(input_test PRIVATE ico_input)
if(WIN32)
    target_link_libraries(input_test PRIVATE comdlg32 user32)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME input COMMAND input_test)
endif()

# package Q1: the pad recording (input_record.c): its format, the round trip
# through the pad script reader, and the sample point in scePadRead
add_executable(input_record_test test/input_record_test.c ../platform/host_config.c)
target_include_directories(input_record_test PRIVATE "${ICO_COMPAT_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(input_record_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(input_record_test PRIVATE ico_input)
if(WIN32)
    target_link_libraries(input_record_test PRIVATE comdlg32 user32)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME input_record COMMAND input_record_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# package AN-G: the touch overlay's mapper (touch.c): layouts, the floating
# stick, buttons under several fingers, the look pad, visibility and fades,
# and the merged pad through ico_input_vpad_to_frame
add_executable(touch_test test/touch_test.c)
target_compile_options(touch_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(touch_test PRIVATE ico_input)
if(ICO_RUN_TESTS)
    add_test(NAME touch COMMAND touch_test)
endif()

# the mouse camera's speed and range for the hand camera: the step arithmetic
# and the gating on the mouse's stick driving the camera
add_executable(mouse_camera_test test/mouse_camera_test.c ../platform/host_config.c)
target_include_directories(mouse_camera_test PRIVATE "${ICO_COMPAT_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(mouse_camera_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(mouse_camera_test PRIVATE ico_input)
if(WIN32)
    target_link_libraries(mouse_camera_test PRIVATE comdlg32 user32)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME mouse_camera COMMAND mouse_camera_test)
endif()
