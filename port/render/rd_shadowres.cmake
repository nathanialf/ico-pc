# rd_shadowres.cmake: the shadow's levels at the scene's scale above 1x
# (rd_shadowres.c, rd_shadowres.h, port/shaders/shadowres.hlsl;
# docs/port/RENDER_POST.md "The shadow's levels above 1x"), included at the end
# of port/render/CMakeLists.txt (a file of its own: that file is near the
# 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_shadowres.c rd_shadowres_math.c)
# the full-size count's form above 1x (rd_shadow_count.h: the step from the
# stencil, the full-size pass, the seven draws, timed on the device; issue 123)
target_sources(ico_render PRIVATE rd_shadow_count.c)
# rd_replay_tool --fine-check: the levels the GPU drew against the CPU
# reference (tools/rd_replay_fine.c; rd_shadow_fine_ab reads its lines)
if(TARGET rd_replay_tool)
    target_sources(rd_replay_tool PRIVATE tools/rd_replay_fine.c)
endif()

# rd_shadow_fine: CPU only: the scales (the count's, the levels' odd whole
# one), the box weights and the tent, the step and the tent at scale 1 (the
# GS's sprite, the identity), the straight edge swept across a SHADOW2 texel at
# 2x, 2.41x, 2.8125x, 3x and 4x across with the rows at 1x (the penumbra as
# wide as the PS2 chain's mean, steady over the phases, its middle in place, its
# darkness kept), the 10 s clause and the --stats words
add_executable(rd_shadow_fine_test test/rd_shadow_fine_test.c)
target_compile_options(rd_shadow_fine_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_shadow_fine_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/test"
                                                       "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_shadow_fine_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_shadow_fine_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_shadow_fine COMMAND rd_shadow_fine_test)
endif()

# rd_shadow_fine_ab (test/rd_shadow_fine_ab.cmake): the fixtures of
# test/fixtures/lists/shadow_fine.txt (docs/port/TESTING_FIXTURES.md; a listed
# fixture that is absent fails), the default against ICO_RD_SHADOW_FINE=0: the
# PS2's sizes v0.11.6's PNGs of the fixtures
# (test/fixtures/lists/shadow_fine_goldens.txt), the default the same bytes at 1x, above
# 1x (3x, 4.10 x 2.41, 2.8125 x 2.41, 1 x 2, MSAA 4x, the B-spline) one shadow at
# the scene's scale with its passes and no other, its change confined to the
# shadow (test/shadow_fine_check.py), its levels the CPU reference's on the
# same count (rd_replay_tool --fine-check), the forced chain
# at 1x within a level, each fixture's levels drawn at the scene's scale (the
# path ran); rd_shadow_fine_ab_gl the 1x and 2x pairs on OpenGL (llvmpipe).
# No device: SKIP (77).
include(rd_fixture.cmake)
if(ICO_RUN_TESTS AND TARGET rd_replay_tool AND Python3_FOUND)
    set(_sf_env "")
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _sf_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    set(_sf_args -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                 "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                 "-DLIST=${ICO_FIXTURE_DIR}/lists/shadow_fine.txt"
                 "-DGOLDENS=${ICO_FIXTURE_DIR}/lists/shadow_fine_goldens.txt"
                 "-DPY=${Python3_EXECUTABLE}"
                 "-DCHECK=${CMAKE_CURRENT_SOURCE_DIR}/test/shadow_fine_check.py"
                 "-DDIFF=${CMAKE_CURRENT_SOURCE_DIR}/test/png_level_diff.py")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shadow_fine_ab")
    add_test(NAME rd_shadow_fine_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_sf_args}
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/shadow_fine_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_fine_ab.cmake")
    set_tests_properties(rd_shadow_fine_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 10800)
    if(_sf_env)
        set_tests_properties(rd_shadow_fine_ab PROPERTIES ENVIRONMENT "${_sf_env}")
    endif()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/shadow_fine_ab")
        add_test(NAME rd_shadow_fine_ab_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool> ${_sf_args}
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/shadow_fine_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_fine_ab.cmake")
        set_tests_properties(rd_shadow_fine_ab_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 10800 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()

# rd_shadow_count_check, rd_shadow_count_check_sabotage
# (test/rd_shadow_count_check.cmake): the check of the full-size count's
# forms on the device at the first shadowed scene (rd_shadow_count.h): passed
# on fx-adreno at the tablet's 3x path, and the step from the stencil made
# wrong (ICO_RD_SHADOW_COUNT_SABOTAGE=1) refused.  No device: SKIP (77).
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    foreach(_sab 0 1)
        set(_n rd_shadow_count_check)
        if(_sab)
            set(_n rd_shadow_count_check_sabotage)
        endif()
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_n}")
        add_test(NAME ${_n}
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         -DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool>
                         "-DFIXTURES=${ICO_FIXTURE_DIR}" "-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS}"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/${_n}" -DSABOTAGE=${_sab}
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_count_check.cmake")
        set_tests_properties(${_n} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 1800)
        if(_sf_env)
            set_tests_properties(${_n} PROPERTIES ENVIRONMENT "${_sf_env}")
        endif()
    endforeach()
endif()

# The tests whose expectations are the PS2-size chain above 1x (absolute
# shadow kernels or composites at 2x and 4x, the shadow tiles' masks of the
# named levels, and the stored pass dumps and baselines made before the rule)
# keep it: ICO_RD_SHADOW_FINE=0 (the tests' switch).  Each is in this list by
# name; one that does not exist in a configuration is left out.  A test's
# OpenGL twin (ico_rd_gl_twin, CMakeLists.txt) copied its environment before
# this file is included, so the twin (<name>_gl) takes the switch here too:
# CI run 43805687280 failed rd_shadow_edges_gl in every Linux job (its B-spline
# pass counts are the PS2-size chain's; at the scene's scale the levels take
# no B-spline pass).
foreach(_t rd_shadow rd_shadow_edges rd_shadow_nodual rd_shadow_vk11 rd_shadow_vk11s
           rd_shadow_vk10 rd_ssaa_shadow rd_ssaa_shadow_4096 rd_shadow_tile_ab
           rd_shadow_tile_selftest rd_glow_cheap_ab rd_crack_late_ab native_frame_gate_a
           native_frame_gate_b rd_no_volume_ab rd_no_volume_ab_gl)
    foreach(_v ${_t} ${_t}_gl)
        if(TEST ${_v})
            get_test_property(${_v} ENVIRONMENT _sfenv)
            if(NOT "ICO_RD_SHADOW_FINE=0" IN_LIST _sfenv)
                set_property(TEST ${_v} APPEND PROPERTY ENVIRONMENT "ICO_RD_SHADOW_FINE=0")
            endif()
        endif()
    endforeach()
endforeach()
