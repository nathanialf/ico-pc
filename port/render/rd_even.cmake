# rd_even.cmake: the pictures' evenness (rd_even.c, rd_even.h: the GPU ends'
# gaps on the refresh grid, the content's steps over them, the present
# clock's clamps and resets, the alpha's sample to the GPU's end; the
# window's "window: evenness in 10 s: ..." line), included from
# rd_perf_clock.cmake after ico_render (port/render/CMakeLists.txt is at the
# 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_even.c rd_rebase.c)

# rd_even_test: CPU only: ten made-up presents (GPU ends a quarter of a ms
# bin's middle apart, 1 to 5 refreshes and under half of one, one a bound,
# one with none, a chain restart) and five clock notes folded, the line word
# for word on an 8 ms grid; taken twice: no line; one more present with the
# refresh unknown; the hand-over's stamps taken once
add_executable(rd_even_test test/rd_even_test.c rd_even.c)
target_include_directories(rd_even_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_even_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(rd_even_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_even COMMAND rd_even_test)
endif()
