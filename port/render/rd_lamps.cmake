# rd_lamps.cmake: the lamps' glow for less (rd_lamps.c, rd_lamps.h;
# docs/port/RENDER_POST.md, "The lamps' glow for less"), included from
# rd_late_pass.cmake after ico_render and rd_replay_tool
# (port/render/CMakeLists.txt is at the 256 KiB cap, tools/check_no_rom.sh).
# _dc_env is rd_date_cut.cmake's validation layer env.
target_sources(ico_render PRIVATE rd_lamps.c)

# rd_lamps_test: the pure parts (CPU): ICO_RD_LAMPS's words, the class's
# default, the rects' union and tiles, a GS box's texels on the corpus's and
# the phone's frames (every texel centre inside), a lamp's box from its
# stream's bound through a matrix, the area's pick
add_executable(rd_lamps_test test/rd_lamps_test.c)
target_compile_options(rd_lamps_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_lamps_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                 "${CMAKE_SOURCE_DIR}/port/rhi"
                                                 "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_lamps_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_lamps COMMAND rd_lamps_test)
endif()

# rd_lamps_ab (test/rd_lamps_ab.cmake): the dumps of tools/lamps_dumps.txt
# (machine-local; a listed dump that is absent fails), each replayed twice
# with ICO_RD_LAMPS=0 and with the parts on (--pass-dump --stats), at the
# PowerVR phone's profile on lavapipe (ICO_VK_FAKE_LIMITS=moltenvk), the same
# under the poisons (ICO_RD_LAMPS_POISON=1, ICO_RD_STORE_POISON=1), lavapipe's
# desktop profile, the Mali fakes, and two sequences on the boot corpus (a
# WHOLE frame before the lamps, a DARK one between two): every target and
# PRESENT byte for byte, passes fewer by the bands left lazy, the attachment
# bytes fewer by the area's saved bytes and the bands', an area where it is
# on and none where it is off; rd_lamps_gl: the desktop setting on OpenGL
# (llvmpipe), the area off there, the band lazy.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lamps_ab")
    add_test(NAME rd_lamps_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/lamps_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/lamps_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_lamps_ab.cmake")
    set_tests_properties(rd_lamps_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 7200)
    if(_dc_env)
        set_tests_properties(rd_lamps_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/lamps_ab")
        add_test(NAME rd_lamps_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/lamps_dumps.txt"
                             "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/lamps_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_lamps_ab.cmake")
        set_tests_properties(rd_lamps_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 7200 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()

# rd_lamps_lowres_ab (test/rd_lamps_lowres_ab.cmake): the lamps' glow at the
# GS frame's size above 1x (RD_LAMPS_LOWRES, DIVERGENCES.md VD-32) against
# today's chain on every glow dump of tools/glow_light_dumps.txt at the
# phone's setting, Enhanced 2x, 3x and 4x at 4:3: DISPLAY and PRESENT
# measured and printed per dump, FAILS above VD-32's bound; at 1x and under
# the poison every PNG the same bytes.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lamps_lowres_ab")
    add_test(NAME rd_lamps_lowres_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/glow_light_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/lamps_lowres_ab"
                     "-DPY=${Python3_EXECUTABLE}"
                     "-DDIFF=${CMAKE_CURRENT_SOURCE_DIR}/test/png_level_diff.py"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_lamps_lowres_ab.cmake")
    set_tests_properties(rd_lamps_lowres_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 14400)
    if(_dc_env)
        set_tests_properties(rd_lamps_lowres_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
endif()
