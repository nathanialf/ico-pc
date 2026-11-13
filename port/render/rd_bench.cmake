# rd_bench.cmake: the rc-only bench (docs/port/ANDROID_DIAGNOSTICS.md, "The
# bench"), included at the end of port/render/CMakeLists.txt (a file of its
# own: that file is near the 256 KiB cap, tools/check_no_rom.sh).
#   rd_bench.c      the frame kept, loaded, classified and its variants set
#                   (in ico_render: nothing calls it but the two below, so a
#                   release program does not link it)
#   rd_bench_run.c  the loop over one kept frame; with
#                   ../platform/bench_plan.c in rd_replay_tool (--bench K)
#                   here, and in the program with ICO_RC_TOOLS on Android
#                   alone (the root CMakeLists.txt)
target_sources(ico_render PRIVATE rd_bench.c)
if(TARGET rd_replay_tool)
    target_sources(rd_replay_tool PRIVATE rd_bench_run.c ../platform/bench_plan.c)
endif()

# rd_bench_test: the classes over a frame built by hand (no device): each
# knock-out's mask makes exactly its class's commands NOPs and every other
# command is left as it was; a list's knock-out takes its actions and none of
# its state; the commands are whole again after the baseline's mask
add_executable(rd_bench_test test/rd_bench_test.c)
target_compile_options(rd_bench_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_bench_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                 "${CMAKE_SOURCE_DIR}/port/rhi"
                                                 "${CMAKE_SOURCE_DIR}/port/shaders"
                                                 "${CMAKE_SOURCE_DIR}/port/platform")
target_link_libraries(rd_bench_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_bench COMMAND rd_bench_test)
endif()

# rd_bench_ab (test/rd_bench_ab.cmake): rd_replay_tool --bench 1 on lavapipe
# over rd_pixel's dump and the machine-local tools/bench_dumps.txt (the
# PowerVR phone's opening: a lamps and a sun picture, at the phone's
# setting; a listed dump that is absent fails where /primary/dev/ico-dumps
# exists): every variant ran, and the presented picture of a baseline replay
# after the bench is byte for byte the one before it (every switch and
# command restored).  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bench_ab")
    add_test(NAME rd_bench_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DDUMP=${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/bench_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/bench_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_bench_ab.cmake")
    set_tests_properties(rd_bench_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                                                FIXTURES_REQUIRED rd_dump TIMEOUT 7200)
    if(_env)
        set_tests_properties(rd_bench_ab PROPERTIES ENVIRONMENT "${_env}")
    endif()
endif()
