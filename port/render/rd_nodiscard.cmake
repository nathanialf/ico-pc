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
# test/fixtures/lists/vu_zero.txt under its profiles (corpus, mali,
# mali169; the Mali ones without dual-source blending, as the reporter's
# phone) with the twins off (ICO_RD_VU_NODISCARD=0) and on without their
# self-test (=1), --repeat 2: every target byte for byte, the twins' draws
# counted on the run with them (above 0 over each fixture and Mali profile:
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
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/vu_zero.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/nodiscard_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_nodiscard_ab.cmake")
    set_tests_properties(rd_nodiscard_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/nodiscard_ab")
        add_test(NAME rd_nodiscard_ab_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                         "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                         "-DLIST=${ICO_FIXTURE_DIR}/lists/vu_zero.txt"
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
#   rd_vuprepass_selftest_nodiscard           on: the plain case through a
#                                             twin, the alpha test's and the
#                                             clip plane's refused, the rule
#                                             refusing the band and DATE, the
#                                             pictures byte for byte, its own
#                                             pipelines released (0 alive
#                                             after it); the mark
#                                             "counting v0.0.0"
#   _nodiscard_sabotage                       the CPU's proof not looking at
#                                             the alpha test
#                                             (ICO_RD_VU_NODISCARD_SABOTAGE=
#                                             alpha): a broken twin's picture
#                                             caught, off for the run, the
#                                             modules and pipelines released,
#                                             the mark "failed v0.0.0"
#   _nodiscard_marked                         the install's mark
#                                             (ICO_NODISCARD_BLOCK, as
#                                             port/platform/nodiscard_mark.c
#                                             exports it after a run that
#                                             ended while trying): off, logged,
#                                             no module made
#   _nodiscard_dual                           a device with dual-source
#                                             blending (lavapipe as it is):
#                                             not applicable, nothing tried, no
#                                             module made, the mark not written
# Each run's start line ("discard-free twins: on|off, N modules of them made,
# P of their pipelines alive") says what exists.
if(ICO_RUN_TESTS AND DEFINED _vst AND TARGET rd_vuprepass_selftest_test)
    set(_ndon "rd: discard-free twins: on \\(self-test: [1-9][0-9]* pass(es)? through them, the alpha test's, the clip plane's and the eye-plane crossing's refused \\([1-9][0-9]*, [1-9][0-9]* and [1-9][0-9]*\\), the side band and DATE refused by the rule, pictures byte for byte, its [1-9][0-9]* pipelines released\\) on SCENE.*rd: discard-free twins: on, 2 modules of them made, 0 of their pipelines alive")
    set(_ndnone ".*rd: discard-free twins: off, 0 modules of them made, 0 of their pipelines alive")
    set(_ndmark "ICO_NODISCARD_VERSION=v0.0.0;ICO_NODISCARD_MARK=${CMAKE_CURRENT_BINARY_DIR}/nodiscard_mark")
    set(_ndenv ${_env_mali_pre} "ICO_VK_FAKE_NO_DUAL=1")
    add_test(NAME rd_vuprepass_selftest_nodiscard
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: discard-free twins: trying them: .*${_ndon}.*test: the discard-free twins' mark after the self-test: counting v0.0.0"
                     "-DNOT_=rd: discard-free twins: off" -P "${_vst}")
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
    add_test(NAME rd_vuprepass_selftest_nodiscard_dual
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: discard-free twins: off for the run \\(self-test not run: not applicable here: the device blends with two outputs, so no pass draws through a \\*_nodual build\\)${_ndnone}.*test: the discard-free twins' mark after the self-test: none"
                     "-DNOT_=rd: discard-free twins: (on|trying)" -P "${_vst}")
    set_tests_properties(rd_vuprepass_selftest_nodiscard rd_vuprepass_selftest_nodiscard_sabotage
                         rd_vuprepass_selftest_nodiscard_marked rd_vuprepass_selftest_nodiscard_dual
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vuprepass_selftest_nodiscard
                         PROPERTIES ENVIRONMENT "${_ndenv};${_ndmark}_on.txt")
    set_tests_properties(rd_vuprepass_selftest_nodiscard_sabotage
                         PROPERTIES ENVIRONMENT "${_ndenv};ICO_RD_VU_NODISCARD_SABOTAGE=alpha;${_ndmark}_sabotage.txt")
    set_tests_properties(rd_vuprepass_selftest_nodiscard_marked
                         PROPERTIES ENVIRONMENT "${_ndenv};ICO_NODISCARD_BLOCK=were being tried when it ended (a crash);${_ndmark}_marked.txt")
    set_tests_properties(rd_vuprepass_selftest_nodiscard_dual
                         PROPERTIES ENVIRONMENT "${_env};${_ndmark}_dual.txt")
endif()
