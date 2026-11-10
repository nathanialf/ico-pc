# rd_perf_clock.cmake: a replay's GPU clock against the CPU's (rd_perf_clock.h,
# issue 123), included from port/render/CMakeLists.txt after ico_render (a
# file of its own: port/render/CMakeLists.txt is at the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_perf_clock.c)

# rd_perf_clock_test: the verdict and its line from made-up slot clocks: the
# issue 123 stall (stale stamps at the wait's start, nothing for 250 ms, the
# stamps written and reset again, a restarted counter), the calibrated and
# the estimated start, stamps the GPU never wrote, a quiet replay (CPU only)
add_executable(rd_perf_clock_test test/rd_perf_clock_test.c rd_perf_clock.c)
target_include_directories(rd_perf_clock_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                      "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_perf_clock_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rd_perf_clock COMMAND rd_perf_clock_test)
endif()
