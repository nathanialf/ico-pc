# rd_vu_flat0.cmake: the main vertex shader's flat outputs at the provoking
# corner (port/shaders/vu_tri.hlsl vu_main_vs, ICO_VU_FLAT0; rd_vu_flat0.c,
# rd_vu_flat0.h; docs/port/PIPELINES.md, "Flat outputs at the provoking
# corner"), included at the end of port/render/CMakeLists.txt (a file of its
# own: port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_vu_flat0.c)

# rd_vu_flat0_test: the pure parts (CPU): the tests' switch's words, the
# start line's text in both forms and with a twin missing, the load counts'
# arithmetic
add_executable(rd_vu_flat0_test test/rd_vu_flat0_test.c)
target_compile_options(rd_vu_flat0_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_vu_flat0_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                    "${CMAKE_SOURCE_DIR}/port/rhi"
                                                    "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_vu_flat0_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_vu_flat0 COMMAND rd_vu_flat0_test)
endif()

# rd_vu_flat0_ab (test/rd_vu_flat0_ab.cmake): every dump of the machine-local
# tools/vu_flat0_dumps.txt (the 2026-10-07 captures, three reporters' frames,
# issue 179's four Mali-G52 dumps under the Arm fakes at its settings, the
# Pixel's three opening dumps at its setting; a listed dump that is absent
# fails where /primary/dev/ico-dumps exists) with the flat outputs at the
# provoking corner (ICO_RD_VU_FLAT0=1, the shipped build) against every
# corner computing them (=0, the twins), merged and one draw a command: every
# target byte for byte.  rd_vu_flat0_ab_pixel: the same on the frames rd_pixel
# leaves (every machine, CI's Direct3D 11 job too: tools/ci_d3d11_warp.sh).
# rd_vu_flat0_gl: the list on OpenGL (llvmpipe), the corpus settings.  No
# device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vu_flat0_ab"
         "${CMAKE_CURRENT_BINARY_DIR}/vu_flat0_ab_pixel")
    add_test(NAME rd_vu_flat0_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/vu_flat0_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_flat0_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_flat0_ab.cmake")
    set_tests_properties(rd_vu_flat0_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 7200)
    # the frames rd_pixel writes (rd_pixel_vu: 64 code-32 draws the merge
    # joins; rd_pixel_vu36: a code-36 cut and kick pair; rd_pixel_frame: no VU
    # draw, skipped), the corpus profile
    set(_vf0_list "${CMAKE_CURRENT_BINARY_DIR}/rd_vu_flat0_pixel_dumps.txt")
    file(WRITE "${_vf0_list}"
         "corpus rd_pixel_vu.rddump\ncorpus rd_pixel_vu36.rddump\ncorpus rd_pixel_frame.rddump\n")
    add_test(NAME rd_vu_flat0_ab_pixel
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> "-DLIST=${_vf0_list}"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vu_flat0_ab_pixel" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_flat0_ab.cmake")
    set_tests_properties(rd_vu_flat0_ab_pixel PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         FIXTURES_REQUIRED rd_dump TIMEOUT 600)
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/vu_flat0_ab")
        add_test(NAME rd_vu_flat0_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/vu_flat0_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/vu_flat0_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_vu_flat0_ab.cmake")
        set_tests_properties(rd_vu_flat0_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 7200 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()
