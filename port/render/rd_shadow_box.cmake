# rd_shadow_box.cmake: the shadow count and composites drawn only over the
# volumes' box (rd_shadow_box.c, rd_shadow_box.h; docs/port/WORK_BUFFERS.md "The
# shadows over the volumes' box"), included from rd_perf_clock.cmake after
# ico_render and rd_replay_tool (port/render/CMakeLists.txt is at the 256 KiB
# cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_shadow_box.c)

# rd_shadow_box_test: CPU only: the count's box from 12.4 vertices, a sprite's
# axis map (the Pixel frame's level 0, a clamped edge, a repeated edge, a box no
# UV reaches, a constant UV, a flipped sprite), a level's whole cover, a
# composite's box and the UV offset, union and clip, the 10 s clause and the
# --stats words
add_executable(rd_shadow_box_test test/rd_shadow_box_test.c)
target_compile_options(rd_shadow_box_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_shadow_box_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                      "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_shadow_box_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_shadow_box_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_shadow_box COMMAND rd_shadow_box_test)
endif()

# rd_shadow_box_ab (test/rd_shadow_box_ab.cmake): the dumps of
# tools/shadow_box_dumps.txt (machine-local; a listed dump that is absent fails)
# with ICO_RD_SHADOW_BOX=0 and 1 at the PowerVR phone's profile, the Mali's, the
# B-spline composites at 2x and MSAA 4x at 2x: every target the same bytes, the
# same draws and passes, the phone's counts and composites boxed as listed;
# rd_shadow_box_ab_gl the same at 1x on OpenGL (llvmpipe).  No device: SKIP (77).
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    set(_sb_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _sb_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shadow_box_ab")
    add_test(NAME rd_shadow_box_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/shadow_box_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/shadow_box_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_box_ab.cmake")
    set_tests_properties(rd_shadow_box_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 7200)
    if(_sb_env)
        set_tests_properties(rd_shadow_box_ab PROPERTIES ENVIRONMENT "${_sb_env}")
    endif()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/shadow_box_ab")
        add_test(NAME rd_shadow_box_ab_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/shadow_box_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/shadow_box_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_box_ab.cmake")
        set_tests_properties(rd_shadow_box_ab_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 7200 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()
