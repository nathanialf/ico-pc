# rd_onepass.cmake: the two-pass blend's LERP As pairs drawn in one pass where the CPU proves
# the same bytes (rd_onepass.c, rd_onepass.h; docs/port/RENDER_POST.md, "The two-pass blend in
# one pass"; issue 179), included from rd_perf_clock.cmake after rd_comp_spec.cmake, after
# ico_render, rd_vuprepass_selftest_test and rd_replay_tool (port/render/CMakeLists.txt is near
# the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_onepass.c)

# rd_onepass_test: the pure parts (CPU, no device): the blend table (the carried alpha and the
# alpha pass's for every As 0..255 at every blender precision, rounded to nearest), the proof on
# the planner's own pairs (each refusal alone: As 129, FBA, PABE, LERP FIX, Cd*FIX + Cs, Z
# GREATER with Z write, another target format, a cut, kick or fused draw), the carry under the
# self-test's arms, the take (kept while off, dropped under arm 1, the counts), the words and
# the ledger's row
add_executable(rd_onepass_test test/rd_onepass_test.c)
target_compile_options(rd_onepass_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_onepass_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                   "${CMAKE_SOURCE_DIR}/port/rhi"
                                                   "${CMAKE_SOURCE_DIR}/port/shaders"
                                                   "${CMAKE_SOURCE_DIR}/port/platform")
target_link_libraries(rd_onepass_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_onepass_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_onepass COMMAND rd_onepass_test)
endif()

# rd_onepass_selftest, _static, _sabotage, _dual (test/rd_vuprepass_selftest.cmake through
# rd_vuprepass_selftest_test, whose rd_vuprepass_selftest runs this self-test after the
# pre-pass's, as the game's start does): with the two-pass blend forced (ICO_RD_NO_DUAL=1,
# lavapipe has dual-source blending) the self-test passes on SCENE (the same bytes both ways,
# at least 150 pairs in one pass, none in the kept replay, the pictures' stamps written) and the
# rule is on by the build's default, with lavapipe's dynamic blend state and without it
# (_static: ICO_VK_FAKE_NO_DYNAMIC=1, the Mali-G52's static pipelines); sabotaged
# (ICO_RD_ONEPASS=sabotage: the one-pass replay's constant alpha 127/255) SCENE differs and the
# rule stays off; with dual-source blending (no switch) the self-test does not run.  The
# pre-pass's own line matched loosely.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_vuprepass_selftest_test)
    set(_op_vst "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vuprepass_selftest.cmake")
    set(_op_pass "rd: two-pass blend: the one-pass rule \\(self-test: SCENE the same at [0-9]+ texels, [0-9]+x[0-9]+ at [0-9]+x[0-9]+ texels, [0-9]+ VU draws, [0-9]+ of [0-9]+ pairs in one pass, [1-9][0-9]* pictures' stamps written, [0-9]+ pipelines made for it, in [0-9.]+ ms\\): the same bytes.*rd: two-pass blend: a LERP As pair in one pass where its alpha is proven at most 128 \\(no FBA, no PABE\\): on \\(the build's default, self-test passed: ")
    foreach(_op selftest selftest_static)
        add_test(NAME rd_onepass_${_op}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test>
                         -DMODE=on "-DLINE=on \\(self-test: " "-DEXTRA=${_op_pass}" -P "${_op_vst}")
    endforeach()
    add_test(NAME rd_onepass_selftest_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=on \\(self-test: "
                     "-DEXTRA=rd: two-pass blend: the one-pass rule's self-test failed: SCENE differs at [1-9][0-9]* of [0-9]+ texels.*\\(ICO_RD_ONEPASS=sabotage\\).*: off until its self-test passes \\(ICO_RD_ONEPASS=sabotage, self-test failed at texel "
                     "-DNOT_=rd: two-pass blend: the one-pass rule \\(self-test: "
                     -P "${_op_vst}")
    add_test(NAME rd_onepass_selftest_dual
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=on \\(self-test: "
                     "-DEXTRA=rd: two-pass blend: a LERP As pair in one pass where its alpha is proven at most 128 \\(no FBA, no PABE\\): off until its self-test passes \\(the build's default, self-test not run: dual-source blending, no two-pass blend here\\)"
                     "-DNOT_=rd: two-pass blend: the one-pass rule"
                     -P "${_op_vst}")
    set(_op_env "ICO_RD_NO_DUAL=1")
    set(_op_dual "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _op_env "ASAN_OPTIONS=detect_leaks=0")
        list(APPEND _op_dual "ASAN_OPTIONS=detect_leaks=0")
    endif()
    set(_op_static ${_op_env} "ICO_VK_FAKE_NO_DYNAMIC=1")
    set(_op_sab ${_op_env} "ICO_RD_ONEPASS=sabotage")
    set_tests_properties(rd_onepass_selftest PROPERTIES ENVIRONMENT "${_op_env}")
    set_tests_properties(rd_onepass_selftest_static PROPERTIES ENVIRONMENT "${_op_static}")
    set_tests_properties(rd_onepass_selftest_sabotage PROPERTIES ENVIRONMENT "${_op_sab}")
    if(_op_dual)
        set_tests_properties(rd_onepass_selftest_dual PROPERTIES ENVIRONMENT "${_op_dual}")
    endif()
endif()

# rd_onepass_ab, rd_onepass_sabotage (test/rd_onepass_ab.cmake): the fixtures of
# test/fixtures/lists/onepass.txt (committed frames, rd_fixture.cmake,
# docs/port/TESTING_FIXTURES.md; a listed fixture that is absent fails) with the proven pairs in
# one pass (ICO_RD_ONEPASS=1) against every pair in two passes (=0) at the Mali reporter's class
# with static and dynamic blend state and at the PowerVR phone's fractional setting with the
# two-pass blend forced: every PNG byte for byte, the same pairs, passes and traffic, arm 1's
# draws at most arm 0's, the self-test passed, at least one pair in one pass over the list at
# each setting (the path ran); the sabotage at the Mali's class: refused, the same bytes.  No
# device: SKIP.
include(rd_fixture.cmake)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    set(_op_ab_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _op_ab_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    foreach(_op ab sabotage)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/onepass_${_op}")
        set(_op_sab_arg "")
        if(_op STREQUAL "sabotage")
            set(_op_sab_arg -DSABOTAGE=1)
        endif()
        add_test(NAME rd_onepass_${_op}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                         "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                         "-DLIST=${ICO_FIXTURE_DIR}/lists/onepass.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/onepass_${_op}"
                         ${_op_sab_arg} -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_onepass_ab.cmake")
        # TIMEOUT: the merge check's lavapipe run took 108 s (ab) and 27 s (sabotage); CI's
        # slowest suite runs such A/B tests at up to 2.82 times that (tools/ci_costs/asan.txt),
        # about 305 s; 1800 s leaves six times that and fails a hang inside the CI job's 120 min
        set_tests_properties(rd_onepass_${_op} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 1800)
        if(_op_ab_env)
            set_tests_properties(rd_onepass_${_op} PROPERTIES ENVIRONMENT "${_op_ab_env}")
        endif()
    endforeach()
endif()
