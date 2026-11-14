# rd_shadowres.cmake: Options > Effects > Shadow detail: High (rd_shadowres.c,
# rd_shadowres.h, port/shaders/shadowres.hlsl; docs/port/RENDER_POST.md
# "Shadow detail: High"), included at the end of port/render/CMakeLists.txt (a
# file of its own: that file is near the 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_shadowres.c)

# rd_shadow_high_test: CPU only: the scales (the count's, the levels' odd whole
# one), the box weights and the tent, the step and the tent at scale 1 (the
# GS's sprite, the identity), the straight edge swept across a SHADOW2 texel at
# 2x, 2.41x, 3x and 4x (High's penumbra as wide as Original's mean, steady over
# the phases, its middle in place), the 10 s clause and the --stats words
add_executable(rd_shadow_high_test test/rd_shadow_high_test.c)
target_compile_options(rd_shadow_high_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_shadow_high_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                       "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_shadow_high_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_shadow_high_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_shadow_high COMMAND rd_shadow_high_test)
endif()

# rd_shadow_high_ab (test/rd_shadow_high_ab.cmake): the dumps of
# tools/shadow_high_dumps.txt (machine-local; a listed dump that is absent
# fails) with Shadow detail Original and High: Original v0.11.6's PNGs
# (tools/shadow_high_goldens.txt), High the same bytes at 1x, above 1x one shadow
# at the scene's scale with its passes and no other, its change confined to the
# shadow (test/shadow_high_check.py), [dev] shadow_bspline doing nothing to it;
# rd_shadow_high_ab_gl the 1x and 2x pairs on OpenGL (llvmpipe).  No device:
# SKIP (77).
if(ICO_RUN_TESTS AND TARGET rd_replay_tool AND Python3_FOUND)
    set(_sh_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _sh_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    set(_sh_args "-DLIST=${CMAKE_SOURCE_DIR}/tools/shadow_high_dumps.txt"
                 "-DGOLDENS=${CMAKE_SOURCE_DIR}/tools/shadow_high_goldens.txt"
                 "-DPY=${Python3_EXECUTABLE}"
                 "-DCHECK=${CMAKE_CURRENT_SOURCE_DIR}/test/shadow_high_check.py")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shadow_high_ab")
    add_test(NAME rd_shadow_high_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_sh_args}
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/shadow_high_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_high_ab.cmake")
    set_tests_properties(rd_shadow_high_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 10800)
    if(_sh_env)
        set_tests_properties(rd_shadow_high_ab PROPERTIES ENVIRONMENT "${_sh_env}")
    endif()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/shadow_high_ab")
        add_test(NAME rd_shadow_high_ab_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_sh_args}
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/shadow_high_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_high_ab.cmake")
        set_tests_properties(rd_shadow_high_ab_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 10800 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()
