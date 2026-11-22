# rd_nodiscard.cmake: the discard-free twins (rd_nodiscard.c the pure part,
# rd_nodiscard_run.c the renderer's, rd_nodiscard_selftest.c the start-up
# self-test; rd_nodiscard.h; docs/port/PIPELINES.md, "The discard-free
# twins"; issue 179), included at the end of port/render/CMakeLists.txt after
# rd_ab.cmake (a file of its own: port/render/CMakeLists.txt is near the
# 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_nodiscard.c rd_nodiscard_run.c rd_nodiscard_selftest.c)

# rd_nodiscard_test (test/rd_nodiscard_test.c, CPU): the eligibility table
# (a pass the twins exist for, every refusal reason in its order: plane, DATE
# by the snapshot and by the stencil bit, band, alpha, clipped; the side
# band impossible under ICO_VU_NO_BESIDE, clip mode none and particles; the
# alpha test off, ALWAYS and the AFAIL merge's one draw against a test that
# can fail; clip mode none alone on every device, the camera rule's region
# test, the scissor and particles refused as clipped), the passes without a
# twin (other entries, no *_nodual build, a destination read), the reasons'
# and modules' names; and the run's state
# without a device (held, on after a passed self-test, =0, =1, the loss
# ladder, the install's mark, a failed self-test kept off for the run): the
# 10 s clause's and the stats line's words
add_executable(rd_nodiscard_test test/rd_nodiscard_test.c)
target_include_directories(rd_nodiscard_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                     "${CMAKE_SOURCE_DIR}/port/rhi"
                                                     "${CMAKE_SOURCE_DIR}/port/shaders")
target_compile_options(rd_nodiscard_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_nodiscard_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_nodiscard COMMAND rd_nodiscard_test)
endif()

# rd_nodiscard_ab (test/rd_nodiscard_ab.cmake): every fixture of
# test/fixtures/lists/nodiscard.txt under its profiles (corpus, mali,
# mali169; the Mali ones without dual-source blending, as the reporter's
# phone) with the twins off (ICO_RD_VU_NODISCARD=0) and on without their
# self-test (=1), --repeat 2: every target byte for byte, the twins' draws
# counted on the run with them (above 0 over the list under each Mali profile:
# they ran), none and "not applicable here" on the corpus profile (dual-source
# blending), nothing of them on the run without them.  rd_nodiscard_ab_gl:
# the same on OpenGL (llvmpipe), every fixture at the corpus setting without
# dual-source blending (ICO_GL_FAKE_NO_DUAL=1), above 0 over the list.  No
# device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/nodiscard_ab")
    add_test(NAME rd_nodiscard_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                     "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/nodiscard.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/nodiscard_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_nodiscard_ab.cmake")
    set_tests_properties(rd_nodiscard_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    # rd_nodiscard_clean: the shipping path: both runs with the game's
    # start-up self-tests (--game-selftests), so the run with them measures
    # the clipper (lavapipe: clean) and the camera rule's passes take a
    # twin: the verdict clean, every target byte for byte, and per fixture
    # and profile at least its list line's clean_min of that class
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/nodiscard_clean")
    add_test(NAME rd_nodiscard_clean
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                     "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/nodiscard.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/nodiscard_clean" -DBACKEND=vulkan
                     -DMODE=clean -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_nodiscard_ab.cmake")
    set_tests_properties(rd_nodiscard_clean PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 3600)
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/nodiscard_ab")
        add_test(NAME rd_nodiscard_ab_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                         "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                         "-DLIST=${ICO_FIXTURE_DIR}/lists/nodiscard.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/nodiscard_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_nodiscard_ab.cmake")
        set_tests_properties(rd_nodiscard_ab_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 3600 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()

# The start-up self-test (rd_nodiscard_selftest.c) through
# rd_vuprepass_selftest_test and test/rd_vuprepass_selftest.cmake (the
# pre-pass's own line must still say on, LINE; EXTRA the twins' lines), the
# Mali fakes without dual-source blending (ICO_VK_FAKE_NO_DUAL=1), the
# install's mark in a file of each test's own under the build folder:
#   rd_vuprepass_selftest_nodiscard      on: the plain and textured cases
#                                        through both twins (each counted),
#                                        the alpha test's (untextured and
#                                        textured), the clip plane's and a
#                                        code 32 eye-plane crossing's
#                                        refused, the rule refusing the band
#                                        and DATE, the pictures byte for
#                                        byte, its own pipelines released;
#                                        the clipper check's verdict as
#                                        measured on lavapipe: clean (13
#                                        cases, both entries); the mark
#                                        "counting v0.0.0"
#   _nodiscard_clipper                   the clipper check's positive
#                                        control (ICO_RD_VU_NODISCARD_
#                                        SABOTAGE=clipper: its first case's
#                                        picture through the twins changed
#                                        in one byte): "not clean" with that
#                                        case named, clip mode none alone,
#                                        the twins still on, the mark
#                                        "counting v0.0.0" (never failed)
#   _nodiscard_notclean                  the verdict faked not clean
#                                        (ICO_RD_VU_NODISCARD_CLIPPER=
#                                        notclean) with what was measured,
#                                        clean, in its line
#   _nodiscard_sabotage                  the CPU's proof not looking at the
#                                        alpha test (ICO_RD_VU_NODISCARD_
#                                        SABOTAGE=alpha): a broken twin's
#                                        picture caught, off for the run,
#                                        released, the mark "failed v0.0.0"
#   _nodiscard_marked                    the install's mark
#                                        (ICO_NODISCARD_BLOCK, as
#                                        port/platform/nodiscard_mark.c
#                                        exports it): off, no module made
#   _nodiscard_dual                      a device with dual-source blending
#                                        (lavapipe as it is): not
#                                        applicable, nothing made, the mark
#                                        not written
#   _nodiscard_fallback                  the same device after the blend's
#                                        self-test fell back to the two-pass
#                                        blend (ICO_RD_BLEND_SABOTAGE=fixed,
#                                        run first as the game does): still
#                                        not applicable (the twins are for
#                                        a device without dual-source
#                                        blending by its own features),
#                                        nothing made, the mark not written
#   _nodiscard_empty                     a clipper check case that draws
#                                        nothing (ICO_RD_VU_NODISCARD_
#                                        SABOTAGE=empty: the first case off
#                                        the frame): "not clean" naming it
#                                        ("drew nothing: not tested"), clip
#                                        mode none alone, the mark
#                                        "counting v0.0.0"
# Each run's start line ("discard-free twins: on|off, N modules of them made,
# P of their pipelines alive") says what exists.
if(ICO_RUN_TESTS AND DEFINED _vst AND TARGET rd_vuprepass_selftest_test)
    set(_ndon "rd: discard-free twins: on \\(self-test: [1-9][0-9]* pass(es)? through them, [1-9][0-9]* through vu_ps_nodual_nd and [1-9][0-9]* through vu_texa_ps_nodual_nd, the alpha test's, the clip plane's and the eye-plane crossing's refused \\([1-9][0-9]*, [1-9][0-9]* and [1-9][0-9]*\\), the side band and DATE refused by the rule, pictures byte for byte, its [1-9][0-9]* pipelines released\\) on SCENE[^\n]*")
    # the verdict lavapipe measures (the merge check of 2026-11-22 17:40)
    set(_ndclean "rd: discard-free twins: the clipper check: clean \\(13 cases crossing the eye plane at codes 32 and 36, untextured and textured under TEXA, [1-9][0-9]* passes through both twins, the pictures and their depth byte for byte\\): the camera rule's passes take them too")
    set(_ndclip "rd: discard-free twins: the clipper check: not clean \\(an edge nearly on the centre view ray at code 32 differs, the first at texel [0-9]+,[0-9]+\\): clip mode none alone takes them")
    set(_ndempty "rd: discard-free twins: the clipper check: not clean \\(an edge nearly on the centre view ray at code 32 drew nothing: not tested\\): clip mode none alone takes them")
    # the texels each case drew (the fewest of its four frames): every case
    # drew on the measured run; the controls stop at the first case
    set(_nddrawn "rd: discard-free twins: the clipper check's texels drawn by each case \\(the fewest of its frames, 13 of 13 cases reached\\): [1-9][0-9]*(, [1-9][0-9]*)(, [1-9][0-9]*)(, [1-9][0-9]*)(, [1-9][0-9]*)(, [1-9][0-9]*)(, [1-9][0-9]*)(, [1-9][0-9]*)(, [1-9][0-9]*)(, [1-9][0-9]*)(, [1-9][0-9]*)(, [1-9][0-9]*)(, [1-9][0-9]*)[^0-9,]")
    set(_nddrawn1 "rd: discard-free twins: the clipper check's texels drawn by each case \\(the fewest of its frames, 1 of 13 cases reached\\): [1-9][0-9]*[^0-9,]")
    set(_nddrawn0 "rd: discard-free twins: the clipper check's texels drawn by each case \\(the fewest of its frames, 1 of 13 cases reached\\): 0[^0-9,]")
    set(_ndfake "rd: discard-free twins: the clipper check: not clean \\(ICO_RD_VU_NODISCARD_CLIPPER, measured clean\\): clip mode none alone takes them")
    set(_ndstart "rd: discard-free twins: on, 2 modules of them made, 0 of their pipelines alive")
    set(_ndcount "test: the discard-free twins' mark after the self-test: counting v0.0.0")
    set(_ndnone ".*rd: discard-free twins: off, 0 modules of them made, 0 of their pipelines alive")
    set(_ndmark "ICO_NODISCARD_VERSION=v0.0.0;ICO_NODISCARD_MARK=${CMAKE_CURRENT_BINARY_DIR}/nodiscard_mark")
    set(_ndenv ${_env_mali_pre} "ICO_VK_FAKE_NO_DUAL=1")
    add_test(NAME rd_vuprepass_selftest_nodiscard
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: discard-free twins: trying them: .*${_nddrawn}.*${_ndon}.*${_ndclean}.*${_ndstart}.*${_ndcount}"
                     "-DNOT_=rd: discard-free twins: (off|vu_[a-z_]+_nd drew nothing|the clipper check: not clean)" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_nodiscard_clipper
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_nddrawn1}.*${_ndon}.*${_ndclip}.*${_ndstart}.*${_ndcount}"
                     "-DNOT_=rd: discard-free twins: (off|the clipper check: clean)" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_nodiscard_notclean
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_ndon}.*${_ndfake}.*${_ndstart}.*${_ndcount}"
                     "-DNOT_=rd: discard-free twins: (off|the clipper check: clean)" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_nodiscard_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: discard-free twins: their modules and pipelines released.*rd: discard-free twins: off for the run \\(self-test: the pictures differ with them, the first at texel [0-9]+,[0-9]+, its [1-9][0-9]* pipelines released\\).*rd: discard-free twins: off, 2 modules of them made, 0 of their pipelines alive.*test: the discard-free twins' mark after the self-test: failed v0.0.0"
                     "-DNOT_=rd: discard-free twins: on \\(" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_nodiscard_marked
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: discard-free twins: off on this install \\(were being tried when it ended \\(a crash\\)\\)${_ndnone}.*test: the discard-free twins' mark after the self-test: none"
                     "-DNOT_=rd: discard-free twins: (on|trying)" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_nodiscard_fallback
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: blend: two-pass fallback on.*rd: discard-free twins: off for the run \\(self-test not run: not applicable here: the device has dual-source blending \\(the two-pass blend is its fallback\\): the twins are for a device without it\\)${_ndnone}.*test: the discard-free twins' mark after the self-test: none"
                     "-DNOT_=rd: discard-free twins: (on|trying)" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_nodiscard_empty
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_nddrawn0}.*${_ndon}.*${_ndempty}.*${_ndstart}.*${_ndcount}"
                     "-DNOT_=rd: discard-free twins: (off|the clipper check: clean)" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_nodiscard_dual
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: discard-free twins: off for the run \\(self-test not run: not applicable here: the device blends with two outputs, so no pass draws through a \\*_nodual build\\)${_ndnone}.*test: the discard-free twins' mark after the self-test: none"
                     "-DNOT_=rd: discard-free twins: (on|trying)" -P "${_vst}")
    set_tests_properties(rd_vuprepass_selftest_nodiscard rd_vuprepass_selftest_nodiscard_clipper
                         rd_vuprepass_selftest_nodiscard_notclean
                         rd_vuprepass_selftest_nodiscard_sabotage
                         rd_vuprepass_selftest_nodiscard_marked rd_vuprepass_selftest_nodiscard_dual
                         rd_vuprepass_selftest_nodiscard_fallback
                         rd_vuprepass_selftest_nodiscard_empty
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vuprepass_selftest_nodiscard_fallback
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_BLEND_SABOTAGE=fixed;${_ndmark}_fallback.txt")
    set_tests_properties(rd_vuprepass_selftest_nodiscard_empty
                         PROPERTIES ENVIRONMENT "${_ndenv};ICO_RD_VU_NODISCARD_SABOTAGE=empty;${_ndmark}_empty.txt")
    set_tests_properties(rd_vuprepass_selftest_nodiscard
                         PROPERTIES ENVIRONMENT "${_ndenv};${_ndmark}_on.txt")
    set_tests_properties(rd_vuprepass_selftest_nodiscard_clipper
                         PROPERTIES ENVIRONMENT "${_ndenv};ICO_RD_VU_NODISCARD_SABOTAGE=clipper;${_ndmark}_clipper.txt")
    set_tests_properties(rd_vuprepass_selftest_nodiscard_notclean
                         PROPERTIES ENVIRONMENT "${_ndenv};ICO_RD_VU_NODISCARD_CLIPPER=notclean;${_ndmark}_notclean.txt")
    set_tests_properties(rd_vuprepass_selftest_nodiscard_sabotage
                         PROPERTIES ENVIRONMENT "${_ndenv};ICO_RD_VU_NODISCARD_SABOTAGE=alpha;${_ndmark}_sabotage.txt")
    set_tests_properties(rd_vuprepass_selftest_nodiscard_marked
                         PROPERTIES ENVIRONMENT "${_ndenv};ICO_NODISCARD_BLOCK=were being tried when it ended (a crash);${_ndmark}_marked.txt")
    set_tests_properties(rd_vuprepass_selftest_nodiscard_dual
                         PROPERTIES ENVIRONMENT "${_env};${_ndmark}_dual.txt")
endif()
