# rd_hash.cmake: the per-draw lookups' hash (rd_hash.c, rd_hash.h: the uniform
# blocks' cache and the pipeline index, 8 bytes a step), included from
# rd_perf_clock.cmake after ico_render (port/render/CMakeLists.txt is at the
# 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_hash.c)

# rd_hash_test: CPU only: the values worked from the steps (little-endian), every
# alignment the same value, nothing past the length read
add_executable(rd_hash_test test/rd_hash_test.c)
target_compile_options(rd_hash_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_hash_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rd_hash_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_hash_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_hash COMMAND rd_hash_test)
endif()
