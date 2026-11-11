# rd_perf_clock.cmake: a replay's GPU clock against the CPU's (rd_perf_clock.h,
# issue 123) and the performance lines by scene (rd_perf_rank.h, issues 179,
# 211 and 123), included from port/render/CMakeLists.txt after ico_render (a
# file of its own: port/render/CMakeLists.txt is at the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_perf_clock.c rd_perf_rank.c rd_perf_rare.c)

# rd_perf_clock_test: the verdict and its line from made-up slot clocks: the
# issue 123 stall (stale stamps at the wait's start, nothing for 250 ms, the
# stamps written and reset again, a restarted counter), the calibrated and
# the estimated start, the single submission wait's long wait (no trace) and
# the time from the last submit to the work done, stamps the GPU never wrote
# (the first and the last written named), the stalls' limiter apart from the
# idle restarts', a quiet replay (CPU only)
add_executable(rd_perf_clock_test test/rd_perf_clock_test.c rd_perf_clock.c)
target_include_directories(rd_perf_clock_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                      "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_perf_clock_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rd_perf_clock COMMAND rd_perf_clock_test)
endif()

# rd_perf_rare_test: a stalled replay's list 0 named by its pipelines
# (rd_perf_rare.h, issue 123): a replay's distinct pipelines and lookups, the
# block's replays each was seen in, the line word for word with a pipeline
# seen in 2 of 100 replays named first (CPU only)
add_executable(rd_perf_rare_test test/rd_perf_rare_test.c rd_perf_rare.c)
target_include_directories(rd_perf_rare_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_perf_rare_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rd_perf_rare COMMAND rd_perf_rare_test)
endif()

# rd_perf_rank_test: the 10 s block's performance lines and the run's
# profile from made-up records and pieces of issue 179's stage 3 play shape:
# the bound (GPU, render thread, game thread, at the rate, not measured),
# the byte-bound passes' fit and the copies' rate recovering their made-up
# constants, the ranked line word for word, the scene, classes, rates and
# slowest lines, the profile by scene with a full table; the Pixel 10 Pro
# Fold's stage 4 block (the vertex estimate refused, the glow first, an
# earlier block's rate), the vertex fit's floor and pre-pass cap, the submit
# thread's CPU apart, every candidate kept, the block in progress (CPU only)
add_executable(rd_perf_rank_test test/rd_perf_rank_test.c rd_perf_rank.c)
target_include_directories(rd_perf_rank_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                     "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_perf_rank_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rd_perf_rank COMMAND rd_perf_rank_test)
endif()

# rd_replay_tool_traffic_stats: --stats names one picture's attachment
# traffic (rhi_traffic.c, the perf scene line's figures), so a dump A/B can
# assert MB a picture; a copy counted once (lavapipe, the pixel frame dump)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    add_test(NAME rd_replay_tool_traffic_stats
             COMMAND rd_replay_tool "${CMAKE_CURRENT_BINARY_DIR}/rd_pixel_frame.rddump"
                     "${CMAKE_CURRENT_BINARY_DIR}/rd_replay_tool_traffic.png" --present 640x480
                     --stats)
    set_tests_properties(rd_replay_tool_traffic_stats PROPERTIES
                         PASS_REGULAR_EXPRESSION
                         "stats: traffic: colour [0-9]+\\.[0-9][0-9] MB loaded [0-9]+\\.[0-9][0-9] MB stored, depth and stencil [0-9.]+/[0-9.]+ MB loaded/stored, [0-9.]+ MB cleared, [0-9.]+ MB copied \\(each copy counted once: as many MB read as written\\), [1-9][0-9]* passes"
                         SKIP_RETURN_CODE 77 FIXTURES_REQUIRED rd_dump)
endif()

# DATE's passes without the depth (rd_date_cut.c in ico_render) and their tests
include(rd_date_cut.cmake)
