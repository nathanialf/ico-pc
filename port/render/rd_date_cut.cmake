# rd_date_cut.cmake: DATE's passes without the depth their draws never touch
# (rd_date_cut.c, rd_date_cut.h; docs/port/WORK_BUFFERS.md, "DATE's passes
# without the depth"), included from rd_perf_clock.cmake after ico_render and
# rd_replay_tool (port/render/CMakeLists.txt is at the 256 KiB cap,
# tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_date_cut.c)

# the validation layer for the device tests, as port/render/CMakeLists.txt's
# _env (set later there)
set(_dc_env "")
set(_dc_vvl "${ICO_DEPS_DIR}/vulkan-validation")
if(NOT WIN32 AND EXISTS "${_dc_vvl}/VkLayer_khronos_validation.json")
    set(_dc_env "VK_ADD_LAYER_PATH=${_dc_vvl}" "VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true"
                "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true"
                "ICO_VK_VALIDATION=1")
endif()
if(ICO_SANITIZE MATCHES "address")
    list(APPEND _dc_env "ASAN_OPTIONS=detect_leaks=0")
endif()

# rd_date_cut_test: the rule on made-up commands (CPU), the start-up set's
# no-depth twins of the screen keys a cut draw takes (CPU), and on a Vulkan
# device a frame in the shadow composites' shape with the cut off and on:
# SCENE byte for byte, 2 passes without the depth, the same passes and
# snapshots, the depth and stencil bytes asked lower, the snapshots' reasons
# and the 10 s text (77 without a device, after the CPU checks)
add_executable(rd_date_cut_test test/rd_date_cut_test.c)
target_compile_options(rd_date_cut_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_include_directories(rd_date_cut_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                    "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_date_cut_test PRIVATE ico_render)
if(ICO_RUN_TESTS)
    add_test(NAME rd_date_cut COMMAND rd_date_cut_test)
    set_tests_properties(rd_date_cut PROPERTIES SKIP_RETURN_CODE 77)
    if(_dc_env)
        set_tests_properties(rd_date_cut PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
endif()

# rd_date_cut_ab (test/rd_date_cut_ab.cmake): the dumps of
# tools/date_cut_dumps.txt with the cut off and on (ICO_RD_DATE_CUT=0 and 1)
# on the devices without the in-pass read (the Mali's, the Adreno's and the
# PowerVR phone's settings with the snapshot path forced: ICO_VK_FAKE_NO_DST,
# ICO_RD_DATE_PATH=snapshot, ICO_VK_FAKE_LIMITS=mali or ICO_VK_FAKE_ADRENO=1),
# the fixed-function blend, the store plan and its poison: every target the
# same bytes, the same draws, passes, snapshots, colour and copy bytes, the
# depth and stencil bytes lower by what the cut's passes left out, the cut
# passes each dump expects; --keys: the cut asks for no pipeline the start-up
# set lacks.  rd_date_cut_ab_gl: the same pairs on OpenGL (llvmpipe: DATE by
# the snapshot there, no fake).  A listed dump that is absent fails.
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/date_cut_ab")
    add_test(NAME rd_date_cut_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/date_cut_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/date_cut_ab" -DBACKEND=vulkan
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_cut_ab.cmake")
    set_tests_properties(rd_date_cut_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 7200)
    if(_dc_env)
        set_tests_properties(rd_date_cut_ab PROPERTIES ENVIRONMENT "${_dc_env}")
    endif()
    if(ICO_RHI_OPENGL AND ICO_SHADERS_GLSL AND TARGET ico_rhi_opengl)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gl/date_cut_ab")
        add_test(NAME rd_date_cut_ab_gl
                 COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                         "-DLIST=${CMAKE_SOURCE_DIR}/tools/date_cut_dumps.txt"
                         "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gl/date_cut_ab" -DBACKEND=opengl
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_date_cut_ab.cmake")
        set_tests_properties(rd_date_cut_ab_gl PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                             TIMEOUT 7200 LABELS gl
                             ENVIRONMENT "ICO_RHI_BACKEND=opengl;LIBGL_ALWAYS_SOFTWARE=1;SDL_VIDEO_DRIVER=offscreen")
    endif()
endif()

# the late passes on SCENE (rd_late_pass.c in ico_render) and their tests
include(rd_late_pass.cmake)
