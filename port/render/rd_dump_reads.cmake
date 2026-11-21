# rd_dump_reads.cmake: the game thread's dumps that wait for the reads alone
# (rd_dump.c dumpFrame's reads-only mode, rd_thread.h rd_thread_reads_wait;
# issue 179), included from port/render/CMakeLists.txt after ico_render.

# rd_dump_reads_test: frames recorded without a device and a host of the
# test's own whose render thread is a thread per picture handed over: the
# slow-scene dump's three calls make no drain and the drained dump's bytes,
# with no picture handed over and with one whose reads are still open
# (each waits for those reads); RdFrame.sent set at the hand-over and
# cleared at the recycle.  Each picture's job starts 150 ms after the call
# is asked for: the order is checked, not a time.
add_executable(rd_dump_reads_test test/rd_dump_reads_test.c)
target_compile_options(rd_dump_reads_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_dump_reads_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                      "${CMAKE_SOURCE_DIR}/port/rhi"
                                                      "${CMAKE_SOURCE_DIR}/port/shaders"
                                                      "${CMAKE_SOURCE_DIR}/port/platform")
target_link_libraries(rd_dump_reads_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_dump_reads_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_dump_reads COMMAND rd_dump_reads_test)
endif()
