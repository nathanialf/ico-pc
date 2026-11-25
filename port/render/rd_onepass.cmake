# rd_onepass.cmake: the one-pass blend (rd_onepass.c the pure part,
# rd_onepass_run.c the renderer's, rd_onepass_selftest.c the device check;
# rd_onepass.h; docs/port/PIPELINES.md, "The one-pass blend"; issue 179),
# included at the end of port/render/CMakeLists.txt after rd_fastps.cmake (a
# file of its own: port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_onepass.c rd_onepass_run.c rd_onepass_selftest.c)

# rd_onepass_test (test/rd_onepass_test.c, CPU): the blender model (the
# carried alpha against the alpha pass's for every As at every precision,
# rounded to nearest), the proof on the planner's own pair and each refusal
# alone, the classes (plain, a cut or kick drawn apart, a fused kick or cut)
# and the classes in use from the arms' verdicts, the run's choice without a
# device under =full, =plain and the check's arms (the alpha pass dropped,
# the passes after it moved down, the unfuse), the ways back (held, =0, the
# loss ladder, the install's mark, a background fault kept for the run), the
# fast pixel build taking a one-pass key and the dynamic blend state folding
# the bit, the 10 s clause's and the stats line's words
add_executable(rd_onepass_test test/rd_onepass_test.c)
target_include_directories(rd_onepass_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                   "${CMAKE_SOURCE_DIR}/port/rhi"
                                                   "${CMAKE_SOURCE_DIR}/port/shaders"
                                                   "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(rd_onepass_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_onepass_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_onepass_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_onepass COMMAND rd_onepass_test)
endif()

# The device check (rd_onepass_selftest.c) through rd_vuprepass_selftest_test
# and test/rd_vuprepass_selftest.cmake, the Mali fakes without dual-source
# blending, the install's mark in a file of each test's own under the build
# folder:
#   rd_vuprepass_selftest_onepass         every state in the pipeline keys
#                                         (ICO_VK_FAKE_NO_DYNAMIC=1, the
#                                         phone's): a plain frame with pairs
#                                         in one pass under A, a code 36
#                                         frame fused today and unfused under
#                                         B and C with pairs in one pass
#                                         (the reporter's pattern drawn);
#                                         A, B and C the same bytes over the
#                                         13 cases (lavapipe's measured
#                                         truth: the frames' modules are
#                                         REF's in every arm, so only the
#                                         blend state and the fuse differ),
#                                         D none, both classes in use, the
#                                         mark "counting"
#   _onepass_dynamic                      the same with lavapipe's dynamic
#                                         blend state (the bit folded into
#                                         the colour pass's pipeline)
#   _onepass_sabotage                     the constant 127/255 in A and C
#                                         (ICO_RD_ONEPASS_SABOTAGE=constant):
#                                         both differ, nothing in use, off
#                                         for the run, the mark "failed"
#   _onepass_cut                          127/255 in C on the code 36 frames
#                                         (=cut): C differs, A the same
#                                         bytes: the plain class alone in use
#   _onepass_unchecked                    every one-pass pipeline taken as
#                                         not made (=unmade): A and C not
#                                         checked after their retries, no
#                                         verdict, nothing in use, the mark
#                                         removed (never "failed")
#   _onepass_sliver                       arm C's picture given a texel on the
#                                         code 36 frames today's drew nothing
#                                         on (=sliver): C differs (a frame
#                                         empty through two passes is
#                                         compared), the plain class alone
#   _onepass_readfail                     the first frame's reference read
#                                         failed without a device loss
#                                         (=read): A not checked, no
#                                         verdict, the mark removed
#   _onepass_marked                       the install's mark
#                                         (ICO_ONEPASS_BLOCK): off, no check,
#                                         the mark not written
if(ICO_RUN_TESTS AND DEFINED _vst AND TARGET rd_vuprepass_selftest_test)
    set(_opA "the check's arm A \\(one pass on the pairs outside code 36\\) over 13 cases: ")
    set(_opB "the check's arm B \\(code 36 unfused where C unfuses it, its two passes kept\\) over 13 cases: ")
    set(_opC "the check's arm C \\(A, and code 36 unfused with its cut and kick pairs in one pass\\) over 13 cases: ")
    set(_opD "rd: one-pass blend: the check's arm D \\(one pass on a fused code 36 pair\\): none")
    set(_opMeasured "(the same bytes|differs)")
    set(_opDrawn "rd: one-pass blend: the check, case [0-9]+ of 13 [^:]*: 32 A [1-9][0-9]* pairs, [0-9]+ texels differ[^:]*, 36 fused [1-9][0-9]*, B unfused [1-9][0-9]*, [0-9]+ texels differ, C unfused [1-9][0-9]*, [1-9][0-9]* pairs, [0-9]+ texels differ")
    set(_opMark "test: the one-pass blend's mark after the self-test: ")
    set(_opNot "over 13 cases: (not checked|not tested|nothing fused|its control did not differ|the device failed)")
    set(_openv ${_env_mali_pre} "ICO_VK_FAKE_NO_DUAL=1")
    set(_opmark "ICO_ONEPASS_VERSION=v0.0.0;ICO_ONEPASS_MARK=${CMAKE_CURRENT_BINARY_DIR}/onepass_mark")
    foreach(_op onepass onepass_dynamic)
        add_test(NAME rd_vuprepass_selftest_${_op}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test>
                         -DMODE=on "-DLINE=${_vst_on}"
                         "-DEXTRA=${_opDrawn}.*${_opA}the same bytes.*${_opB}the same bytes.*${_opC}the same bytes.*${_opD}.*rd: one-pass blend: on \\(check: in use: the pairs outside code 36 and code 36's cut and kick pairs drawn apart, arms A the same bytes, B the same bytes, C the same bytes, D none\\).*rd: one-pass blend: on \\(the pairs outside code 36 and code 36's cut and kick pairs drawn apart\\), 0 of its pipelines alive.*${_opMark}counting v0.0.0"
                         "-DNOT_=${_opNot}" -P "${_vst}")
    endforeach()
    add_test(NAME rd_vuprepass_selftest_onepass_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: one-pass blend: ICO_RD_ONEPASS_SABOTAGE: the constant's alpha 127/255 in the check's arms A and C.*${_opA}differs: [1-9][0-9]* of [0-9]+ frames.*${_opC}differs: [1-9][0-9]* of [0-9]+ frames.*rd: one-pass blend: off for the run \\(check: nothing in use, arms A differs, B the same bytes, C differs, D none\\).*rd: one-pass blend: off \\(for the run \\(its check: A differs.*${_opMark}failed v0.0.0"
                     "-DNOT_=rd: one-pass blend: on \\(" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_onepass_cut
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_opA}the same bytes.*${_opC}differs: [1-9][0-9]* of [0-9]+ frames.*rd: one-pass blend: on \\(check: in use: the pairs outside code 36, arms A the same bytes, B the same bytes, C differs, D none\\).*rd: one-pass blend: on \\(the pairs outside code 36\\), 0 of its pipelines alive.*${_opMark}counting v0.0.0"
                     "-DNOT_=in use: the pairs outside code 36 and code 36" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_onepass_unchecked
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_opA}not checked: [1-9][0-9]* frames still without a pipeline after a retry.*${_opC}not checked: [1-9][0-9]* frames still without a pipeline after a retry.*rd: one-pass blend: off for the run \\(check: nothing in use, arms A not checked, B the same bytes, C not checked, D none, no verdict: its mark removed, nothing written as failed, the check runs again at the next start\\).*${_opMark}none"
                     "-DNOT_=over 13 cases: differs|rd: one-pass blend: on \\(" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_onepass_sliver
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_opA}the same bytes.*${_opC}differs: [1-9][0-9]* of [0-9]+ frames.*rd: one-pass blend: ICO_RD_ONEPASS_SABOTAGE=sliver: arm C's picture changed on [1-9][0-9]* frames where today's two passes drew nothing.*rd: one-pass blend: on \\(check: in use: the pairs outside code 36, arms A the same bytes, B the same bytes, C differs, D none\\).*${_opMark}counting v0.0.0"
                     "-DNOT_=in use: the pairs outside code 36 and code 36" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_onepass_readfail
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_opA}not checked: [1-9][0-9]* frames still without a pipeline after a retry \\(case 1 at 32.*rd: one-pass blend: off for the run \\(check: nothing in use, arms A not checked, B the same bytes, C the same bytes, D none, no verdict: its mark removed, nothing written as failed, the check runs again at the next start\\).*${_opMark}none"
                     "-DNOT_=over 13 cases: differs|rd: one-pass blend: on \\(|device was lost" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_onepass_marked
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: one-pass blend: off on this install \\(was on when it ended \\(a crash\\)\\).*rd: one-pass blend: off \\(on this install \\(was on when it ended \\(a crash\\)\\)\\), 0 of its pipelines alive.*${_opMark}none"
                     "-DNOT_=rd: one-pass blend: the check" -P "${_vst}")
    set_tests_properties(rd_vuprepass_selftest_onepass rd_vuprepass_selftest_onepass_dynamic
                         rd_vuprepass_selftest_onepass_sabotage rd_vuprepass_selftest_onepass_cut
                         rd_vuprepass_selftest_onepass_unchecked
                         rd_vuprepass_selftest_onepass_sliver
                         rd_vuprepass_selftest_onepass_readfail
                         rd_vuprepass_selftest_onepass_marked
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vuprepass_selftest_onepass_sliver PROPERTIES ENVIRONMENT
                         "${_openv};ICO_VK_FAKE_NO_DYNAMIC=1;ICO_RD_ONEPASS_SABOTAGE=sliver;${_opmark}_sliver.txt")
    set_tests_properties(rd_vuprepass_selftest_onepass_readfail PROPERTIES ENVIRONMENT
                         "${_openv};ICO_VK_FAKE_NO_DYNAMIC=1;ICO_RD_ONEPASS_SABOTAGE=read;${_opmark}_readfail.txt")
    set_tests_properties(rd_vuprepass_selftest_onepass PROPERTIES ENVIRONMENT
                         "${_openv};ICO_VK_FAKE_NO_DYNAMIC=1;${_opmark}_on.txt")
    set_tests_properties(rd_vuprepass_selftest_onepass_dynamic PROPERTIES ENVIRONMENT
                         "${_openv};${_opmark}_dynamic.txt")
    set_tests_properties(rd_vuprepass_selftest_onepass_sabotage PROPERTIES ENVIRONMENT
                         "${_openv};ICO_VK_FAKE_NO_DYNAMIC=1;ICO_RD_ONEPASS_SABOTAGE=constant;${_opmark}_sabotage.txt")
    set_tests_properties(rd_vuprepass_selftest_onepass_cut PROPERTIES ENVIRONMENT
                         "${_openv};ICO_VK_FAKE_NO_DYNAMIC=1;ICO_RD_ONEPASS_SABOTAGE=cut;${_opmark}_cut.txt")
    set_tests_properties(rd_vuprepass_selftest_onepass_unchecked PROPERTIES ENVIRONMENT
                         "${_openv};ICO_VK_FAKE_NO_DYNAMIC=1;ICO_RD_ONEPASS_SABOTAGE=unmade;${_opmark}_unchecked.txt")
    set_tests_properties(rd_vuprepass_selftest_onepass_marked PROPERTIES ENVIRONMENT
                         "${_openv};ICO_VK_FAKE_NO_DYNAMIC=1;ICO_ONEPASS_BLOCK=was on when it ended (a crash);${_opmark}_marked.txt")
endif()

# rd_onepass_ab (test/rd_onepass_ab.cmake): every fixture of
# test/fixtures/lists/onepass.txt (committed frames of the Mali-G52's dumps
# whose VU textures let the proof run; a listed fixture that is absent fails)
# under the Mali's class with every state in the pipeline keys and with
# lavapipe's dynamic blend state, every pair in two passes
# (ICO_RD_ONEPASS=0) against both classes in one pass without the check
# (=full): every target byte for byte, the same traffic, pairs in one pass
# over the list at each setting, code 36's among them (the path ran: the
# counts printed per fixture), none on the run without it; and =full with the
# in-play judge's off arm on every replay (ICO_RD_AB=off): no pair in one
# pass, each kept as "timed without it" (the judge's way back taken), every
# target the first run's.  No device: SKIP.
include(rd_fixture.cmake)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/onepass_ab")
    add_test(NAME rd_onepass_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                     "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/onepass.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/onepass_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_onepass_ab.cmake")
    set_tests_properties(rd_onepass_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
    set(_op_ab_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _op_ab_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    if(_op_ab_env)
        set_tests_properties(rd_onepass_ab PROPERTIES ENVIRONMENT "${_op_ab_env}")
    endif()
endif()
