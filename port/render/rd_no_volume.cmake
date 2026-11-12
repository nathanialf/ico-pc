# rd_no_volume.cmake: a shadow window with no volume keeps SCENE's pass open
# (rd_no_volume.c, rd_no_volume.h; docs/port/RENDER_POST.md "A shadow window
# with no volume"), included from rd_perf_clock.cmake after ico_render and
# rd_replay_tool (port/render/CMakeLists.txt is at the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_no_volume.c)

# rd_no_volume_test: CPU only: the scan on the corpus title frame's list 3 (taken,
# the levels and composites left undrawn), a strip in the window, a foreign
# reader of a level, a sprite of the chain the box scan named neither, a clear or
# a draw into the chain, a draw inside the window, a second window, a resolve
# elsewhere, a level writing a depth, a composite's DATE by the snapshot, an
# unknown command, a level target cleared before the window; the gate; the 10 s
# clause and the --stats words
add_executable(rd_no_volume_test test/rd_no_volume_test.c)
target_compile_options(rd_no_volume_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_no_volume_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                     "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_no_volume_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_no_volume_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_no_volume COMMAND rd_no_volume_test)
endif()

# rd_no_volume_ab (test/rd_no_volume_ab.cmake): the dumps of
# tools/no_volume_dumps.txt (machine-local; a listed dump that is absent fails)
# with ICO_RD_NO_VOLUME=0 and 1 at the PowerVR phone's profile (with the late
# passes), the desktop's at 1x, the desktop's with DATE through the stencil bit,
# and the Mali's: every target the same bytes; where a window is taken the passes
# and the attachment traffic fall by what the code leaves out, elsewhere they are
# the same; rd_no_volume_ab_gl the same at 1x on OpenGL (llvmpipe: not taken).
# No device: SKIP (77).
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    set(_nv_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _nv_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/no_volume_ab")
    add_test(NAME rd_no_volume_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/no_volume_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/no_volume_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_no_volume_ab.cmake")
    set_tests_properties(rd_no_volume_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 7200)
    if(_nv_env)
        set_tests_properties(rd_no_volume_ab PROPERTIES ENVIRONMENT "${_nv_env}")
    endif()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/no_volume_ab")
        add_test(NAME rd_no_volume_ab_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/no_volume_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/no_volume_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_no_volume_ab.cmake")
        set_tests_properties(rd_no_volume_ab_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 7200 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()
