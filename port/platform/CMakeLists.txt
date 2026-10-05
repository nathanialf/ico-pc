# port/platform/CMakeLists.txt: package 1B's part of the platform layer (the
# fiber scheduler, the EE kernel calls, the EE RAM arena, the host loop and
# the headless graphics seam) and its tests. docs/port/PLATFORM.md describes
# them.

target_sources(ico_platform PRIVATE
    arena.c
    diag_host.c
    fiber.c
    kernel_host.c
    sched.c
    vsync_hooks.c)
# diag_host.c's heartbeat and watchdog thread (pthreads on POSIX).
find_package(Threads REQUIRED)
target_link_libraries(ico_platform PUBLIC Threads::Threads)
if(TARGET ico_pc)
    # A link map beside the program, shipped with test builds, to turn the
    # crash report's executable offsets into symbols (docs/port/BOOT_DIAG.md).
    target_link_options(ico_pc PRIVATE "LINKER:-Map=$<TARGET_FILE_DIR:ico_pc>/ico_pc.map")
endif()
# host_loop.c calls into the game (its main, ico_vsync, systemStatus), so it
# goes into the program beside main_host.c, ahead of the game library on the
# link line, rather than into ico_platform, which the game itself needs.
if(TARGET ico_pc)
    target_sources(ico_pc PRIVATE host_loop.c)
endif()
# port/null/gfx_null.c (the headless graphics floor) is linked into ico_pc
# by the top-level CMakeLists.txt.

# ICO_HEAP_STATS: memory.c reports allocations to arena.c, which logs each
# partition's high-water mark.
option(ICO_HEAP_STATS "Log the game heap's per-partition high-water marks" OFF)
if(ICO_HEAP_STATS)
    target_compile_definitions(ico_fumi PRIVATE ICO_HEAP_STATS=1)
endif()

# --- Tests ------------------------------------------------------------------
set(_ico_run_tests OFF)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    set(_ico_run_tests ON)
endif()

foreach(t sched fiber arena diag)
    add_executable(${t}_test test/${t}_test.c)
    target_include_directories(${t}_test PRIVATE "${ICO_COMPAT_DIR}")
    target_compile_options(${t}_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(${t}_test PRIVATE ico_platform)
    if(_ico_run_tests)
        add_test(NAME ${t} COMMAND ${t}_test)
    endif()
endforeach()
if(_ico_run_tests)
    set_tests_properties(diag PROPERTIES SKIP_RETURN_CODE 77)
endif()
if(_ico_run_tests AND NOT WIN32)
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
if(_ico_run_tests)
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
if(_ico_run_tests)
    add_test(NAME ios_chain COMMAND ios_chain_test)
endif()
