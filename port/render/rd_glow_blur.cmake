# rd_glow_blur.cmake: the glow's blur pairs in two passes (rd_glow_blur.c,
# rd_glow_blur.h; docs/port/RENDER_POST.md, "The glow's blur in two passes";
# DIVERGENCES.md VD-33), included from rd_lamps.cmake after ico_render and
# rd_replay_tool (port/render/CMakeLists.txt is at the 256 KiB cap,
# tools/check_no_rom.sh).  _dc_env is rd_date_cut.cmake's validation layer env.
target_sources(ico_render PRIVATE rd_glow_blur.c)

# rd_glow_blur_test: the pure parts (CPU): the switch's word, the chain's
# operator per axis from the recorded passes (4:3 and the phone's 848), the
# table's round trip, the two passes emulated against the chain in the GS's
# integers on a fixed picture, the refusals
add_executable(rd_glow_blur_test test/rd_glow_blur_test.c)
target_compile_options(rd_glow_blur_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_glow_blur_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                     "${CMAKE_SOURCE_DIR}/port/rhi"
                                                     "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_glow_blur_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_glow_blur COMMAND rd_glow_blur_test)
endif()

# rd_glow_blur_ab (test/rd_glow_blur_ab.cmake): every dump of
# tools/glow_light_dumps.txt and tools/lamps_dumps.txt (machine-local; a
# listed dump that is absent fails) with the glow's blur in two passes against
# today's chain at the phone's setting, Enhanced 2x, 3x and 4x at 4:3 and the
# Mali fakes: WORK3, WORK0, DISPLAY and PRESENT within VD-33's bounds, the
# passes fewer by the chain's as today draws them less two, the shimmer at
# the phone's setting and 2x; at 1x and under the poison every PNG the same
# bytes; rd_glow_blur_gl the phone's setting on OpenGL (llvmpipe).  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/glow_blur_ab")
    add_test(NAME rd_glow_blur_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/glow_light_dumps.txt"
                     "-DLIST2=${CMAKE_SOURCE_DIR}/tools/lamps_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/glow_blur_ab" -DBACKEND=vulkan
                     "-DPY=${Python3_EXECUTABLE}"
                     "-DDIFF=${CMAKE_CURRENT_SOURCE_DIR}/test/png_level_diff.py"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_glow_blur_ab.cmake")
    set_tests_properties(rd_glow_blur_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 14400)
    if(_dc_env)
        set_tests_properties(rd_glow_blur_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/glow_blur_ab")
        add_test(NAME rd_glow_blur_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/glow_light_dumps.txt"
                         "-DLIST2=${CMAKE_SOURCE_DIR}/tools/lamps_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/glow_blur_ab" -DBACKEND=opengl
                         "-DPY=${Python3_EXECUTABLE}"
                         "-DDIFF=${CMAKE_CURRENT_SOURCE_DIR}/test/png_level_diff.py"
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_glow_blur_ab.cmake")
        set_tests_properties(rd_glow_blur_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 14400 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()
