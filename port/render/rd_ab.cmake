# rd_ab.cmake: the in-play A/B judge (rd_ab.c the judge, pure; rd_ab_run.c
# the renderer's side; rd_ab.h, rd_ab_run.h; docs/port/PIPELINES.md, "The
# zero-area cull timed in play"; issue 179), included at the end of
# port/render/CMakeLists.txt after rd_vu_zero.cmake (a file of its own:
# port/render/CMakeLists.txt is near the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_ab.c rd_ab_run.c)

# rd_ab_test (test/rd_ab_test.c, CPU): made-up streams of replays with two
# or three pictures in flight: the verdicts (5 % cheaper on, 3 % dearer off,
# no difference off, 40 % outliers at 3-5x with a 5 % saving still on, the
# work rising 1 % a picture no false on), the trial's cost bounded, the
# trial 2 s into a stage once the subject had work, one verdict a subject a
# run kept for every stage, the 12-trial cap, a fake second subject (one
# trial at a time), the stop's way back, the modes, the trial only in
# scenery (not the title, the logo or a menu) and the first scenery stage,
# and the rate requirement on a GPU-bound stream (tick 40 ms, 60 s;
# pictures of 33, 37.5, 48 and 60 ms; a subject saving or costing 3, 5 and
# 10 %; one stage, four stages, four stages flipping): the judge at least
# the worse fixed arm, at least the best per stage less the pictures lost on
# the worse arm while unjudged (from the two fixed-arm runs) less 1, and the
# better arm's after the last verdict within 1
add_executable(rd_ab_test test/rd_ab_test.c rd_ab.c)
target_include_directories(rd_ab_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rd_ab_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(rd_ab_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_ab COMMAND rd_ab_test)
endif()

# rd_ab_zero_ab (test/rd_ab_zero_ab.cmake): every fixture of
# test/fixtures/lists/vu_zero.txt under its profiles (corpus, mali,
# mali169) with the cull on and checked (ICO_RD_VU_ZERO=1) and the judge's
# arms fixed on, fixed off and alternating (ICO_RD_AB=on|off|alt, --repeat 2
# and 4): every target byte for byte across the arms, both arms ran (the
# judge's replay counts, the cull's draws refused for the trial, slots
# culled on the arm with it), every check 0 differ.  rd_ab_zero_stop: the
# way back (ICO_RD_AB_FAKE_LOSS=1: every trial stopped, every replay at the
# off arm, logged).  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    foreach(_m ab stop)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ab_zero_${_m}")
        add_test(NAME rd_ab_zero_${_m}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                         "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                         "-DLIST=${ICO_FIXTURE_DIR}/lists/vu_zero.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/ab_zero_${_m}" -DMODE=${_m}
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_ab_zero_ab.cmake")
        set_tests_properties(rd_ab_zero_${_m} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 3600)
    endforeach()
endif()
