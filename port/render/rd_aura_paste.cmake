# rd_aura_paste.cmake: the aura's paste as the alpha alone over the rows that
# read only proven alpha 0 rows of FEED128 (rd_aura_paste.c, rd_aura_paste.h;
# docs/port/RENDER_POST.md "The aura's paste as its constant part"), included
# from rd_late_pass.cmake (port/render/CMakeLists.txt is at the 256 KiB cap).
target_sources(ico_render PRIVATE rd_aura_paste.c)

# rd_aura_paste_test: the row proof's pure parts (CPU): the kernels' taps, the
# GS's V, the addressing, and the chain on the Pixel frame's geometry (WORK0
# rows 0-111 into FEED128, the paste's rows [0, 1062) under CLAMP)
add_executable(rd_aura_paste_test test/rd_aura_paste_test.c)
target_compile_options(rd_aura_paste_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_aura_paste_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                      "${CMAKE_SOURCE_DIR}/port/rhi"
                                                      "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_aura_paste_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_aura_paste COMMAND rd_aura_paste_test)
endif()

# rd_aura_paste_ab (test/rd_aura_paste_ab.cmake): the fixtures of
# test/fixtures/lists/aura_paste.txt (rd_fixture.cmake; a listed fixture that
# is absent fails) with ICO_RD_AURA_PASTE=0 and =1, --pass-dump --stats, at the PowerVR
# phone's profile (ICO_VK_FAKE_LIMITS=moltenvk), Enhanced 2x 4:3 under the same
# fakes and a desktop 16:9 window without them (no local read: the paste whole):
# every target and PRESENT byte for byte, the same passes and attachment bytes,
# the off run splitting nothing, each fixture's declared splits at the phone's
# setting (the path ran: aura_paste_split).  No device: SKIP.
include(rd_fixture.cmake)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/aura_paste_ab")
    add_test(NAME rd_aura_paste_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                     "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/aura_paste.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/aura_paste_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_aura_paste_ab.cmake")
    set_tests_properties(rd_aura_paste_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 7200)
    if(_dc_env)
        set_tests_properties(rd_aura_paste_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
endif()

# the crack fill's swap and early leave (rd_crack_fill.c) and their A/B
include(rd_crack_work.cmake)
