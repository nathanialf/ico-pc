# rd_list_draws.cmake: a list's draws by fragment entry and DATE mode (issue
# 179: list 3 the deja scene's most expensive list on the Mali-G52, its draws
# unnamed; rd_list_draws.c the table and the line, rd_list_draws_live.c the
# renderer's side; rd_list_draws.h), included at the end of
# port/render/CMakeLists.txt (a file of its own: that file is near the
# 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_list_draws.c rd_list_draws_live.c)

# rd_list_draws_test: CPU only: the stencil modes' DATE words, the table
# (out of range counted apart), the list picked (the most expensive, not
# list 0, none above 0), the line word for word, the rest summed past the
# kinds it names, a short buffer cut
add_executable(rd_list_draws_test test/rd_list_draws_test.c rd_list_draws.c)
target_include_directories(rd_list_draws_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                      "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(rd_list_draws_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rd_list_draws COMMAND rd_list_draws_test)
endif()
