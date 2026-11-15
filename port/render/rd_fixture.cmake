# rd_fixture.cmake: the committed test frames (rd_fixture.h, rd_fixture.c,
# rd_fixture_make.c, tools/rd_fixture_tool.c; docs/port/TESTING_FIXTURES.md),
# included by every test family that replays a fixture (rd_shadow_box.cmake
# first), after ico_render.  Host-side only: nothing here is in the game.
include_guard(GLOBAL)

# ico_rd_fixture: the format, the patterns and generators, the expansion and
# the maker (the maker walks the commands with rd__apply_state and checks them
# with rd_dump.c's rule, rd__dump_cmd_valid)
add_library(ico_rd_fixture STATIC rd_fixture.c rd_fixture_make.c)
target_include_directories(ico_rd_fixture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                                                 "${CMAKE_SOURCE_DIR}/port/shaders"
                                                 "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_options(ico_rd_fixture PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_rd_fixture PUBLIC ico_render ico_miniz)

# rd_fixture_tool: expand, check, make, remake, selfcheck, props (the
# source's head); an F12 or loss .zip read through platform/dump_zip.c
add_executable(rd_fixture_tool tools/rd_fixture_tool.c ../platform/dump_zip.c)
target_compile_options(rd_fixture_tool PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_fixture_tool PRIVATE ../platform)
target_link_libraries(rd_fixture_tool PRIVATE ico_rd_fixture ico_render ico_miniz ico_host_fs)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_fixture_tool)
endif()

# rd_fixture_test: the format on a frame the test builds (CPU only)
add_executable(rd_fixture_test test/rd_fixture_test.c)
target_compile_options(rd_fixture_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_fixture_test PRIVATE ico_rd_fixture ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_fixture_test)
endif()

# The fixtures (port/render/test/fixtures/*.rdfix) and their manifest
# (fixtures.txt: each one's source dump and declared properties)
set(ICO_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
set(ICO_FIXTURE_HELPERS "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fixture_helpers.cmake")
if(ICO_RUN_TESTS)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture_test")
    add_test(NAME rd_fixture_format
             COMMAND rd_fixture_test "${CMAKE_CURRENT_BINARY_DIR}/fixture_test")
    # rd_fixtures_check: every committed fixture holds no art, declares its
    # properties, expands to the SHA-256 it pins, and the manifest names
    # exactly the files (CPU only: every suite)
    file(GLOB _ico_fixtures CONFIGURE_DEPENDS "${ICO_FIXTURE_DIR}/*.rdfix")
    add_test(NAME rd_fixtures_check
             COMMAND rd_fixture_tool check-dir "${ICO_FIXTURE_DIR}" "${ICO_FIXTURE_DIR}/fixtures.txt"
                     ${_ico_fixtures})
    # rd_fixtures_selfcheck: the window check of each fixture against the dump
    # it was made from, where the source dumps are (ICO_FIXTURE_SOURCES, the
    # developer's folder); elsewhere SKIP (77)
    add_test(NAME rd_fixtures_selfcheck
             COMMAND rd_fixture_tool selfcheck-dir "${ICO_FIXTURE_DIR}"
                     "${ICO_FIXTURE_DIR}/fixtures.txt")
    set_tests_properties(rd_fixtures_selfcheck PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
endif()
