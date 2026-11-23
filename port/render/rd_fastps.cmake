# rd_fastps.cmake: the fast pixel build (rd_fastps.c the pure part,
# rd_fastps_run.c the renderer's, rd_fastps_selftest.c the start-up
# self-test; rd_fastps.h; docs/port/PIPELINES.md, "The fast pixel build";
# issue 179), included at the end of port/render/CMakeLists.txt after
# rd_nodiscard.cmake (a file of its own: port/render/CMakeLists.txt is near
# the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_fastps.c rd_fastps_run.c rd_fastps_selftest.c)

# rd_fastps_test (test/rd_fastps_test.c, CPU): the eligibility table at the
# DrawCB (the set's forms through it: opaque, the two-pass lerp's passes,
# the premultiplied forms, FBA, PABE, C1_DST, a discard-free twin's key; every
# refusal reason in its order: texture (untextured, DECAL, TCC off), texel,
# levels (GS levels, nearest, the grid), DATE (DATE, DATM, DATE_DST), plane,
# alpha, crop; the passes it does not exist for: vu_texa_ps, vu_np_ps, a
# destination read, nodual 0, a fast key already), the reasons' names, the
# judge's use, the DrawCB bits held against the shader's own lines
# (port/shaders/common.hlsli VU_FAST_FIXED_MASK, VU_FAST_FIXED), and the run's
# state without a device (held, =0, =1, the loss ladder, the install's mark,
# a failed self-test kept off): the 10 s clause's and the stats line's words
add_executable(rd_fastps_test test/rd_fastps_test.c)
target_include_directories(rd_fastps_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                  "${CMAKE_SOURCE_DIR}/port/rhi"
                                                  "${CMAKE_SOURCE_DIR}/port/shaders")
target_compile_options(rd_fastps_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rd_fastps_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_fastps
             COMMAND rd_fastps_test "${CMAKE_SOURCE_DIR}/port/shaders/common.hlsli")
endif()

# rd_fastps_ab (test/rd_fastps_ab.cmake): every fixture of
# test/fixtures/lists/fastps.txt under its profiles (corpus, mali, mali169,
# desktop; all but corpus without dual-source blending) with the build off
# (ICO_RD_VU_FASTPS=0) and on without its self-test (=1), --repeat 2: every
# target byte for byte, its passes counted on the run with it (above 0 over
# the list under each profile without dual-source blending: it ran; the
# count printed per fixture), none and "not applicable here" on corpus,
# nothing of it on the run without it.  rd_fastps_ab_gl: the same on OpenGL
# (llvmpipe), every fixture at the corpus setting without dual-source
# blending (ICO_GL_FAKE_NO_DUAL=1).  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fastps_ab")
    add_test(NAME rd_fastps_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                     "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                     "-DLIST=${ICO_FIXTURE_DIR}/lists/fastps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/fastps_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fastps_ab.cmake")
    set_tests_properties(rd_fastps_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 3600)
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/fastps_ab")
        add_test(NAME rd_fastps_ab_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                         "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                         "-DLIST=${ICO_FIXTURE_DIR}/lists/fastps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/fastps_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_fastps_ab.cmake")
        set_tests_properties(rd_fastps_ab_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 3600 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()

# The start-up self-test (rd_fastps_selftest.c) through
# rd_vuprepass_selftest_test and test/rd_vuprepass_selftest.cmake, the Mali
# fakes without dual-source blending, the install's mark in a file of each
# test's own under the build folder:
#   rd_vuprepass_selftest_fastps          on: the set's six passes through
#                                         vu_ps_nodual_fast (the twins off)
#                                         and, the twins on for the run, six
#                                         through vu_ps_nodual_fast_nd; the
#                                         texture's, nearest filter's, alpha
#                                         test's and clip plane's cases
#                                         refused; the pictures and the
#                                         depth (through the probe) byte for
#                                         byte; its pipelines released; the
#                                         mark "counting v0.0.0"
#   _fastps_sabotage                      the CPU's rule not looking at DECAL
#                                         (ICO_RD_VU_FASTPS_SABOTAGE=decal):
#                                         the picture differs, caught, off
#                                         for the run, released, the mark
#                                         "failed v0.0.0"
#   _fastps_marked                        the install's mark
#                                         (ICO_FASTPS_BLOCK): off, no module
#                                         made, the mark not written
if(ICO_RUN_TESTS AND DEFINED _vst AND TARGET rd_vuprepass_selftest_test)
    set(_fpon "rd: fast pixel build: on \\(self-test: ([6-9]|[1-9][0-9]+) passes through vu_ps_nodual_fast and [0-9]+ through vu_ps_nodual_fast_nd[^,]*, the texture's, the nearest filter's, the alpha test's and the clip plane's refused \\(([3-9]|[1-9][0-9]+), [1-9][0-9]*, [1-9][0-9]* and [1-9][0-9]*\\), the colours and the depth \\(through the probe\\) byte for byte, its [1-9][0-9]* pipelines released\\) on SCENE[^\n]*")
    set(_fpstart "rd: fast pixel build: on, 2 modules of it made, 0 of its pipelines alive")
    set(_fpnone ".*rd: fast pixel build: off, 0 modules of it made, 0 of its pipelines alive")
    set(_fpmark "ICO_FASTPS_VERSION=v0.0.0;ICO_FASTPS_MARK=${CMAKE_CURRENT_BINARY_DIR}/fastps_mark")
    set(_fpenv ${_env_mali_pre} "ICO_VK_FAKE_NO_DUAL=1")
    add_test(NAME rd_vuprepass_selftest_fastps
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: fast pixel build: trying it: .*${_fpon}.*${_fpstart}.*test: the fast pixel build's mark after the self-test: counting v0.0.0"
                     "-DNOT_=rd: fast pixel build: off" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_fastps_sabotage
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: fast pixel build: its modules and pipelines released.*rd: fast pixel build: off for the run \\(self-test: the pictures differ with it, the first at texel [0-9]+,[0-9]+, its [1-9][0-9]* pipelines released\\).*rd: fast pixel build: off, 2 modules of it made, 0 of its pipelines alive.*test: the fast pixel build's mark after the self-test: failed v0.0.0"
                     "-DNOT_=rd: fast pixel build: on \\(" -P "${_vst}")
    add_test(NAME rd_vuprepass_selftest_fastps_marked
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_vuprepass_selftest_test> -DMODE=on
                     "-DLINE=${_vst_on}"
                     "-DEXTRA=rd: fast pixel build: off on this install \\(was being tried when it ended \\(a crash\\)\\)${_fpnone}.*test: the fast pixel build's mark after the self-test: none"
                     "-DNOT_=rd: fast pixel build: (on|trying)" -P "${_vst}")
    set_tests_properties(rd_vuprepass_selftest_fastps rd_vuprepass_selftest_fastps_sabotage
                         rd_vuprepass_selftest_fastps_marked
                         PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
    set_tests_properties(rd_vuprepass_selftest_fastps
                         PROPERTIES ENVIRONMENT "${_fpenv};${_fpmark}_on.txt")
    set_tests_properties(rd_vuprepass_selftest_fastps_sabotage
                         PROPERTIES ENVIRONMENT "${_fpenv};ICO_RD_VU_FASTPS_SABOTAGE=decal;${_fpmark}_sabotage.txt")
    set_tests_properties(rd_vuprepass_selftest_fastps_marked
                         PROPERTIES ENVIRONMENT "${_fpenv};ICO_FASTPS_BLOCK=was being tried when it ended (a crash);${_fpmark}_marked.txt")
endif()
