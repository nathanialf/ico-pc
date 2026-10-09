# port/platform/CMakeLists.txt: the platform sources ico_platform takes
# beyond the top-level CMakeLists.txt's, and the platform tests.

target_sources(ico_platform PRIVATE
    arena.c
    diag_host.c
    fiber.c
    hotkeys.c
    kernel_host.c
    pace_policy.c
    sched.c
    vsync_hooks.c
    window_lifecycle.c)
# diag_host.c's heartbeat and watchdog thread (pthreads on POSIX).
find_package(Threads REQUIRED)
target_link_libraries(ico_platform PUBLIC Threads::Threads)
if(TARGET ico_pc)
    # A link map beside the program, shipped with test builds, to turn the
    # crash report's executable offsets into symbols.
    if(APPLE)
        target_link_options(ico_pc PRIVATE "LINKER:-map,$<TARGET_FILE_DIR:ico_pc>/ico_pc.map")
    else()
        target_link_options(ico_pc PRIVATE "LINKER:-Map=$<TARGET_FILE_DIR:ico_pc>/ico_pc.map")
    endif()
endif()
# host_loop.c calls into the game (its main, ico_vsync, systemStatus), so it
# goes into the program beside main_host.c, ahead of the game library on the
# link line, rather than into ico_platform, which the game itself needs.
if(TARGET ico_pc)
    target_sources(ico_pc PRIVATE host_loop.c)
endif()
# The commit the program was built from (ICO_BUILD_COMMIT in the
# generated ico_build_commit.h, "git describe --always --dirty"), for the pad
# recording's header (main_host.c). Checked at every build; the header only
# changes when the commit does.
if(TARGET ico_pc)
    set(_ico_commit_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_custom_target(ico_build_commit
        COMMAND "${CMAKE_COMMAND}" -DSRC=${CMAKE_SOURCE_DIR}
                -DOUT=${_ico_commit_dir}/ico_build_commit.h
                -P "${CMAKE_CURRENT_SOURCE_DIR}/build_commit.cmake"
        BYPRODUCTS "${_ico_commit_dir}/ico_build_commit.h"
        COMMENT "Build commit"
        VERBATIM)
    add_dependencies(ico_pc ico_build_commit)
    target_include_directories(ico_pc PRIVATE "${_ico_commit_dir}")
endif()
# port/null/gfx_null.c (the graphics floor under the game's packet code, in
# both builds) is linked into ico_pc by the top-level CMakeLists.txt.

# ICO_HEAP_STATS: memory.c reports allocations to arena.c, which logs each
# partition's high-water mark.
option(ICO_HEAP_STATS "Log the game heap's per-partition high-water marks" OFF)
if(ICO_HEAP_STATS)
    target_compile_definitions(ico_fumi PRIVATE ICO_HEAP_STATS=1)
endif()

# --- Tests ------------------------------------------------------------------
foreach(t sched fiber arena diag hotkeys pace_policy lifecycle)
    add_executable(${t}_test test/${t}_test.c)
    target_include_directories(${t}_test PRIVATE "${ICO_COMPAT_DIR}")
    target_compile_options(${t}_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(${t}_test PRIVATE ico_platform)
    if(ICO_RUN_TESTS)
        add_test(NAME ${t} COMMAND ${t}_test)
    endif()
endforeach()
if(ICO_RUN_TESTS)
    set_tests_properties(diag PROPERTIES SKIP_RETURN_CODE 77)
endif()

# The Android build's file layout (android/android_paths.c), pure C, so it
# is tested here on the native builds.
add_executable(android_paths_test test/android_paths_test.c android/android_paths.c)
target_include_directories(android_paths_test PRIVATE android)
target_compile_options(android_paths_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME android_paths COMMAND android_paths_test)
endif()

# The Android build's graphics driver packages (gpu_driver.c),
# pure C over host_fs, miniz and json.h, so it is tested on the native builds;
# it writes under the build folder
add_executable(gpu_driver_test test/gpu_driver_test.c gpu_driver.c)
target_compile_options(gpu_driver_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(gpu_driver_test PRIVATE ico_host_fs ico_json ico_miniz)
if(ICO_RUN_TESTS)
    add_test(NAME gpu_driver COMMAND gpu_driver_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# host_fs.h on a non-ASCII (UTF-8) path in the build tree
add_executable(host_fs_test test/host_fs_test.c)
target_compile_options(host_fs_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(host_fs_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME host_fs COMMAND host_fs_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(ICO_RUN_TESTS AND NOT WIN32)
    # a stack overflow in a fiber must hit the guard page
    add_test(NAME fiber_guard COMMAND fiber_test guard)
    set_tests_properties(fiber_guard PROPERTIES SKIP_RETURN_CODE 77)
endif()

# The game's allocator (fumi/ios/memory.c) over the arena, compiled as the
# game compiles it; the test checks the partition layout for the host's
# record sizes.
add_executable(memory_test test/memory_test.c "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/memory.c")
ico_include_dirs(fumi _ico_fumi_incs)
target_include_directories(memory_test PRIVATE ${_ico_fumi_incs})
target_compile_definitions(memory_test PRIVATE ${ICO_GAME_DEFINITIONS} ICO_HEAP_STATS=1)
target_compile_options(memory_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(memory_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME memory COMMAND memory_test)
endif()

# The game's thread, message and memory layers on the scheduler, driven by
# simulated vsyncs.
add_executable(ios_chain_test test/ios_chain_test.c
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/thread.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/message.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/memory.c")
target_include_directories(ios_chain_test PRIVATE ${_ico_fumi_incs})
target_compile_definitions(ios_chain_test PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(ios_chain_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(ios_chain_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME ios_chain COMMAND ios_chain_test)
endif()

# The window mode requests' readback (window_video.c) on SDL's
# offscreen video driver. SDL3 is found by port/rhi, configured after this
# directory, so the test is defined once the top level is done; exit 77
# (skipped) without the offscreen driver.
function(_ico_window_video_test)
    if(NOT TARGET SDL3::SDL3-shared)
        return()
    endif()
    set(_dir "${CMAKE_SOURCE_DIR}/port/platform")
    add_executable(window_video_test "${_dir}/test/window_video_test.c" "${_dir}/window_video.c")
    target_compile_options(window_video_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(window_video_test PRIVATE SDL3::SDL3-shared)
    ico_copy_sdl3_dll(window_video_test)
    if(ICO_RUN_TESTS)
        add_test(NAME window_video COMMAND window_video_test)
        set_tests_properties(window_video PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _ico_window_video_test)
