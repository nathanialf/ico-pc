# rd_paste_spec.cmake: the glow's paste drawn by its own entry, fx_paste_ps
# (rd_paste_spec.c, rd_paste_spec.h; docs/port/RENDER_POST.md, "The glow's
# paste by its own entry"), included from rd_lamps.cmake after ico_render and
# rd_replay_tool (port/render/CMakeLists.txt is near the 256 KiB cap,
# tools/check_no_rom.sh).  _dc_env is rd_date_cut.cmake's validation layer env.
target_sources(ico_render PRIVATE rd_paste_spec.c)

# rd_paste_spec_test: the pure parts (CPU): the bits fx_paste_ps holds, the
# gate on the paste's DrawCB and key (each held word refused with its
# reason), the start-up set's key and its twin, the switch's words, the
# attempt ledger's row
add_executable(rd_paste_spec_test test/rd_paste_spec_test.c)
target_compile_options(rd_paste_spec_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_paste_spec_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                      "${CMAKE_SOURCE_DIR}/port/rhi"
                                                      "${CMAKE_SOURCE_DIR}/port/shaders"
                                                      "${CMAKE_SOURCE_DIR}/port/platform")
target_link_libraries(rd_paste_spec_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_paste_spec COMMAND rd_paste_spec_test)
endif()

# rd_paste_spec_ab (test/rd_paste_spec_ab.cmake): every dump of
# tools/lamps_dumps.txt and tools/glow_light_dumps.txt (machine-local; a
# listed dump that is absent fails where /primary/dev/ico-dumps exists) with
# the paste by fx_paste_ps (ICO_RD_PASTE_SPEC=1) against fx_frame_ps
# (ICO_RD_PASTE_SPEC=0) at the PowerVR phone's setting (MoltenVK fakes, the
# late pass on), a desktop Enhanced 2x at 16:9 and Original 4:3 at 1x: every
# PNG byte for byte, the same draws and passes, the self-test passed, at least
# one paste by it; rd_paste_spec_sabotage the phone's setting with the
# self-test sabotaged (ICO_RD_PASTE_SPEC=sabotage): refused, the same bytes;
# rd_paste_spec_gl the phone's setting on OpenGL (llvmpipe): off there (the
# module is Vulkan's alone), the same bytes.  No device: SKIP.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    foreach(_ps ab sabotage)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/paste_spec_${_ps}")
        set(_ps_sab "")
        if(_ps STREQUAL "sabotage")
            set(_ps_sab -DSABOTAGE=1)
        endif()
        add_test(NAME rd_paste_spec_${_ps}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/lamps_dumps.txt"
                         "-DLIST2=${CMAKE_SOURCE_DIR}/tools/glow_light_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/paste_spec_${_ps}" -DBACKEND=vulkan
                         ${_ps_sab} -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_paste_spec_ab.cmake")
        set_tests_properties(rd_paste_spec_${_ps} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 14400)
        if(_dc_env)
            set_tests_properties(rd_paste_spec_${_ps} PROPERTIES ENVIRONMENT "${_dc_env}")
        endif()
    endforeach()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/paste_spec_ab")
        add_test(NAME rd_paste_spec_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/lamps_dumps.txt"
                         "-DLIST2=${CMAKE_SOURCE_DIR}/tools/glow_light_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/paste_spec_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_paste_spec_ab.cmake")
        set_tests_properties(rd_paste_spec_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 14400 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()
