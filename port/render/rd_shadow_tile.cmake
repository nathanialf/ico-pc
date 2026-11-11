# rd_shadow_tile.cmake: the shadow composites culled outside a GPU tile mask
# (rd_shadow_tile.c, rd_shadow_tile.h, blit.hlsl shadow_tile_mask_ps and
# shadow_tile_mark_ps; docs/port/RENDER_POST.md "The shadow tiles"), included
# from rd_perf_clock.cmake after ico_render and rd_replay_tool.
target_sources(ico_render PRIVATE rd_shadow_tile.c)

set(_tl_env "")
set(_tl_vvl "${ICO_DEPS_DIR}/vulkan-validation")
if(NOT WIN32 AND EXISTS "${_tl_vvl}/VkLayer_khronos_validation.json")
    set(_tl_env "VK_ADD_LAYER_PATH=${_tl_vvl}" "VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true"
                "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true"
                "ICO_VK_VALIDATION=1")
endif()
if(ICO_SANITIZE MATCHES "address")
    list(APPEND _tl_env "ASAN_OPTIONS=detect_leaks=0")
endif()

# rd_shadow_tile_test: on a Vulkan device with DATE through the stencil bit
# (lavapipe with ICO_RD_DATE_PATH=stencil; 77 without): the 10 s clause and the
# --stats words (CPU), then the self-test's three frames (a shadow in a corner,
# shadows at the frame's edges, no shadow), each with the B-spline composites
# off and on: SCENE byte for byte with the tiles off and on (a later DATE DATM 1
# sprite reading bit 6 included), the corner's and the edges' composite drawn
# over a mask with dead tiles; and the start-up self-test's ctest twin
# (rd_shadow_tile_selftest: rd_fog_selftest's start line "shadow tiles: on
# (ICO_RD_SHADOW_TILE=1; self-test ok: ...)"); no validation error
add_executable(rd_shadow_tile_test test/rd_shadow_tile_test.c)
target_compile_options(rd_shadow_tile_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_shadow_tile_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                       "${CMAKE_SOURCE_DIR}/port/rhi"
                                                       "${CMAKE_SOURCE_DIR}/port/shaders")
target_link_libraries(rd_shadow_tile_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_shadow_tile_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_shadow_tile COMMAND rd_shadow_tile_test frames)
    set_tests_properties(rd_shadow_tile PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                         "${_tl_env};ICO_RD_DATE_PATH=stencil")
    add_test(NAME rd_shadow_tile_selftest COMMAND rd_shadow_tile_test startup)
    set_tests_properties(rd_shadow_tile_selftest PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                         "${_tl_env};ICO_RD_DATE_PATH=stencil;ICO_RD_SHADOW_TILE=1")
endif()

# rd_shadow_tile_ab (test/rd_shadow_tile_ab.cmake): tools/shadow_tile_dumps.txt
# (tools/shadow_box_dumps.txt's frames and the rc3 bridge and cage captures;
# machine-local, a listed dump that is absent fails) with ICO_RD_SHADOW_TILE=0
# and 1 at the PowerVR phone's profile, the Mali's (no stencil bit: nothing
# tiled), and the B-spline at 2x, 3x and 4x: every target the same bytes, the
# draws the same, the passes more by the masks drawn, the bridge's live tiles
# under the bound its measured box gives.  No device: SKIP (77).  No OpenGL
# twin: the tiles need DATE's stencil bit, which the OpenGL backend has not.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shadow_tile_ab")
    add_test(NAME rd_shadow_tile_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/shadow_tile_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/shadow_tile_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_shadow_tile_ab.cmake")
    set_tests_properties(rd_shadow_tile_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 7200)
    if(_tl_env)
        set_tests_properties(rd_shadow_tile_ab PROPERTIES ENVIRONMENT "${_tl_env}")
    endif()
endif()
