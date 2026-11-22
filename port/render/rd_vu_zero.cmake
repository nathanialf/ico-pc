# rd_vu_zero.cmake: the zero-area cull (rd_vu_zero.c, rd_vu_zero_run.c,
# rd_vu_zero_selftest.c, rd_vu_zero.h; docs/port/PIPELINES.md, "The zero-area
# cull"; issue 179), included at the end of port/render/CMakeLists.txt (a file
# of its own: port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_vu_zero.c rd_vu_zero_run.c rd_vu_zero_selftest.c)

# rd_vu_zero_test (test/rd_vu_zero_test.c): the pure parts (CPU): the
# lattice word of the run's targets (1x and 2x and 3x at 16:9, the field's
# half line, an odd width, the Full pixel crop) and every refusal; the test
# against a brute-force rasteriser with every corner moved a sixteenth each
# way (no culled triangle covers a sample point); the self-test's cases; the
# record check; vu_ftoi4's edges; the run's state (held, ICO_RD_VU_ZERO=0
# and 1, the loss ladder over both) and the VU line's clause
add_executable(rd_vu_zero_test test/rd_vu_zero_test.c)
target_compile_options(rd_vu_zero_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_vu_zero_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                   "${CMAKE_SOURCE_DIR}/port/rhi"
                                                   "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_vu_zero_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_vu_zero COMMAND rd_vu_zero_test)
endif()

# rd_vu_zero_ab (test/rd_vu_zero_ab.cmake): every fixture of
# test/fixtures/lists/vu_zero.txt (issue 179's Mali-G52 stage 42 capture,
# issue 123's Adreno frame) with the cull on and every entry checked
# (ICO_RD_VU_ZERO=1) against it off (=0), merged and one draw a command,
# under the corpus settings (4:3 at 1x, Enhanced 16:9 and 4:3 at 2x, the
# motion vectors) and the Mali fakes (the record by compute dispatches; 1x at
# 21:9 and at 16:9 with half height): every target byte for byte, every
# check 0 differ, each run's cull line printed, the culled slots of each
# fixture's profile at least its vu_zero_culled (the path ran), and at least
# one slot culled at 2x over the list.  rd_vu_zero_gl: the same on OpenGL
# (llvmpipe), the corpus settings.  rd_vu_zero_fault: the run-time check's
# way back (ICO_RD_VU_ZERO_SABOTAGE=check: off for the run, logged).  No
# device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    foreach(_m ab fault)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_zero_${_m}")
        add_test(NAME rd_vu_zero_${_m}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                         "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                         "-DLIST=${ICO_FIXTURE_DIR}/lists/vu_zero.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_zero_${_m}" -DBACKEND=vulkan
                         -DMODE=${_m} -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_zero_ab.cmake")
        set_tests_properties(rd_vu_zero_${_m} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 3600)
    endforeach()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/vu_zero_ab")
        add_test(NAME rd_vu_zero_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                         "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                         "-DLIST=${ICO_FIXTURE_DIR}/lists/vu_zero.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/vu_zero_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_zero_ab.cmake")
        set_tests_properties(rd_vu_zero_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 3600 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()

# The start-up self-test (rd_vu_zero_selftest.c) through
# rd_vuprepass_selftest_test and test/rd_vuprepass_selftest.cmake (the
# pre-pass's own line must still say on, LINE; EXTRA the cull's line):
#   rd_vuprepass_selftest_zero        the extended variant (Enhanced 16:9 at
#                                     2x with the Full pixel crop): on, the
#                                     GPU's flags the CPU's answers, the
#                                     pictures byte for byte
#   _zero_plain                       Original 4:3 at 1x: on
#   _zero_mali                        the Mali fakes, the record by compute
#                                     dispatches: on
#   _zero_lattice                     the rule stage given a lattice half a
#                                     texel off (ICO_RD_VU_ZERO_SABOTAGE):
#                                     off for the run, the answers differ
#   _zero_missing                     no record copied: off for the run, no
#                                     answer read back
#   _zero_lossrow                     a start after a lost one (ICO_LOSS_ROW=1,
#                                     as diag_host.c exports it; lavapipe is
#                                     no phone, so the pre-pass stays on): the
#                                     cull off for this start, its self-test
#                                     not run, no module of it made
#   _zero_marked                      the install's mark (ICO_VU_ZERO_BLOCK, as
#                                     port/platform/vu_zero_mark.c exports it
#                                     after a run that ended while trying):
#                                     off, logged, no module of it made
#   _zero_never                       the cull off (ICO_RD_VU_ZERO=0): no
#                                     module or pipeline of it made, so the
#                                     modules made are the build's without it
#   _zero_refused                     a refusal by the settings (the
#                                     vertices off the grid,
#                                     ICO_RD_VU_OFFGRID=1, as MSAA, a scale
#                                     that is not whole or jitter refuse):
#                                     not tried in this run, no module made
#                                     and the install's mark not written
#   _zero_picture                     the picture without the cull changed
#                                     (ICO_RD_VU_ZERO_SABOTAGE=picture): off
#                                     for the run by the picture compare,
#                                     released, the mark "failed <version>"
#   _zero_switch                      the renderer made again in the process
#                                     (a Graphics API switch: rd_suspend,
#                                     rd_resume) and the self-test run again:
#                                     on both times, its read back buffers
#                                     made again for the second try and none
#                                     alive after either, the mark "counting
#                                     <version>" after each, and the in-play
#                                     judge's forgetting at the second
#                                     renderer logged (rd_ab_run.h)
# Each run's start line ("VU zero-area cull: on|off, N modules of it made, P
# of its pipelines alive") says what exists: 1 and 1 where it passed, 1 and 0
# where its self-test released it, 0 and 0 where it was never tried.
if(ICO_RUN_TESTS AND DEFINED _vst AND TARGET rd_vuprepass_selftest_test)
    set(_zon "rd: VU zero-area cull: on \\(self-test: [1-9][0-9]* of [0-9]+ triangle slots culled as the CPU answers, code 34 and code 32, pictures byte for byte, lattice 0x[0-9A-F]+\\) on SCENE.*rd: VU zero-area cull: on, 1 modules of it made, 1 of its pipelines alive")
    # the release is logged as the verdict turns the cull off, before the
    # self-test's own line
    set(_zrel "rd: VU zero-area cull: its module and pipeline released.*")
    set(_zgone ".*rd: VU zero-area cull: off, 1 modules of it made, 0 of its pipelines alive")
    set(_znone ".*rd: VU zero-area cull: off, 0 modules of it made, 0 of its pipelines alive")
    add_test(NAME rd_vuprepass_selftest_zero
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" "-DEXTRA=${_zon}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_zero_plain
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     -DVARIANT=plain "-DLINE=${_vst_on}" "-DEXTRA=${_zon}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_zero_mali
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" "-DEXTRA=${_zon}" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_zero_lattice
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_zrel}rd: VU zero-area cull: off for the run \\(self-test: the GPU's answers differ from the CPU's in [1-9][0-9]* of [0-9]+ triangle slots: triangle slot [0-9]+: the GPU ${_zgone}"
                     "-DNOT_=rd: VU zero-area cull: on" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_zero_missing
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_zrel}rd: VU zero-area cull: off for the run \\(self-test: no answer read back from the GPU\\)${_zgone}"
                     "-DNOT_=rd: VU zero-area cull: on" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_zero_lossrow
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU zero-area cull: off for this start \\(the loss ladder: 1 start in a row before this one lost the graphics device or hung its driver, ICO_LOSS_ROW 1, nothing written to the settings\\)${_znone}"
                     "-DNOT_=rd: VU zero-area cull: (on|trying)" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_zero_marked
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU zero-area cull: off on this install \\(was being tried when it ended \\(a crash\\)\\)${_znone}"
                     "-DNOT_=rd: VU zero-area cull: (on|trying)" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_zero_never
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}" "-DEXTRA=${_znone}"
                     "-DNOT_=rd: VU zero-area cull: (on|trying)" -P "${_vst}")
    # the install's mark in the build folder (each test its own file; the
    # program removes it first) and the version the renderer writes in it
    # the second try's start line counts both tries' modules
    string(REPLACE "on, 1 modules of it made" "on, 2 modules of it made" _zon2 "${_zon}")
    set(_zmark "ICO_VU_ZERO_VERSION=v0.0.0;ICO_VU_ZERO_MARK=${CMAKE_CURRENT_BINARY_DIR}/vu_zero_mark")
    add_test(NAME rd_vuprepass_selftest_zero_refused
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: VU zero-area cull: off for the run \\(self-test not run: these settings give SCENE no lattice \\(off grid\\): nothing written, tried in a later run whose settings allow it\\)${_znone}.*test: the zero-area cull's mark after the self-test: none"
                     "-DNOT_=rd: VU zero-area cull: (on|trying)" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_zero_picture
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=${_zrel}rd: VU zero-area cull: off for the run \\(self-test: the pictures differ with the cull, the first at texel [0-9]+,[0-9]+\\)${_zgone}.*test: the zero-area cull's mark after the self-test: failed v0.0.0"
                     "-DNOT_=rd: VU zero-area cull: on \\(" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_zero_switch
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     -DVARIANT=switch "-DLINE=${_vst_on}.*rd: VU pre-pass: ${_vst_on}"
                     "-DEXTRA=${_zon}.*test: the zero-area cull's mark after the self-test: counting v0.0.0.*rd: A/B: the renderer was made again \\(a Graphics API switch or the device made again\\): every verdict and the carried arm forgotten.*rd: VU zero-area cull: trying it: .*${_zon2}.*test: the renderer made again: the zero-area cull on before, on after, its read back buffers [1-9][0-9]* made before the switch, [1-9][0-9]* after, alive 0, 0, 0.*test: the zero-area cull's mark after the second self-test: counting v0.0.0"
                     -P "${_vst}")
    set(_zst rd_vuprepass_selftest_zero rd_vuprepass_selftest_zero_plain
             rd_vuprepass_selftest_zero_mali rd_vuprepass_selftest_zero_lattice
             rd_vuprepass_selftest_zero_missing rd_vuprepass_selftest_zero_lossrow
             rd_vuprepass_selftest_zero_marked rd_vuprepass_selftest_zero_never
             rd_vuprepass_selftest_zero_refused rd_vuprepass_selftest_zero_picture
             rd_vuprepass_selftest_zero_switch)
    set_tests_properties(${_zst} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vuprepass_selftest_zero rd_vuprepass_selftest_zero_plain
                         PROPERTIES ENVIRONMENT "${_env}")
    set_tests_properties(rd_vuprepass_selftest_zero_mali
                         PROPERTIES ENVIRONMENT "${_env_mali_pre};ICO_RD_VU_RECORD=compute")
    set_tests_properties(rd_vuprepass_selftest_zero_lattice
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_ZERO_SABOTAGE=lattice")
    set_tests_properties(rd_vuprepass_selftest_zero_missing
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_ZERO_SABOTAGE=missing")
    set_tests_properties(rd_vuprepass_selftest_zero_lossrow
                         PROPERTIES ENVIRONMENT "${_env};ICO_LOSS_ROW=1")
    set_tests_properties(rd_vuprepass_selftest_zero_marked
                         PROPERTIES ENVIRONMENT "${_env};ICO_VU_ZERO_BLOCK=was being tried when it ended (a crash)")
    set_tests_properties(rd_vuprepass_selftest_zero_never
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_ZERO=0")
    set_tests_properties(rd_vuprepass_selftest_zero_refused
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_OFFGRID=1;${_zmark}_refused.txt")
    set_tests_properties(rd_vuprepass_selftest_zero_picture
                         PROPERTIES ENVIRONMENT "${_env};ICO_RD_VU_ZERO_SABOTAGE=picture;${_zmark}_picture.txt")
    set_tests_properties(rd_vuprepass_selftest_zero_switch
                         PROPERTIES ENVIRONMENT "${_env};${_zmark}_switch.txt")
endif()
