# rd_cpu_phase.cmake: the render thread's CPU phases a picture (rd_cpu_phase.c,
# rd_cpu_phase.h: the interpolation's, the upload's and the walk's phases and the
# per-draw look-ups, the window's "window: CPU phases ..." line), included from
# rd_perf_clock.cmake after ico_render (port/render/CMakeLists.txt is at the
# 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_cpu_phase.c)

# rd_cpu_phase_test: CPU only: two pictures folded (pictures, ticks, the hash's
# count, the clock reads, the look-ups), the line's words, no line without a
# picture
add_executable(rd_cpu_phase_test test/rd_cpu_phase_test.c)
target_compile_options(rd_cpu_phase_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_cpu_phase_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rd_cpu_phase_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_cpu_phase_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_cpu_phase COMMAND rd_cpu_phase_test)
endif()
