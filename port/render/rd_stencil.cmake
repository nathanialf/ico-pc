# rd_stencil.cmake: the stencil loaded and stored inside the shadow window
# only (rd_stencil_selftest.c, rd_stencil_selftest.h; docs/port/WORK_BUFFERS.md
# "The stencil outside the shadow window"), included from rd_perf_clock.cmake
# after ico_render and rd_replay_tool (port/render/CMakeLists.txt is at the
# 256 KiB cap, tools/check_no_rom.sh).
target_sources(ico_render PRIVATE rd_stencil_selftest.c)

# the validation layer for the device tests, as port/render/CMakeLists.txt's
# _env (set later there)
set(_sc_env "")
set(_sc_vvl "${ICO_DEPS_DIR}/vulkan-validation")
if(NOT WIN32 AND EXISTS "${_sc_vvl}/VkLayer_khronos_validation.json")
    set(_sc_env "VK_ADD_LAYER_PATH=${_sc_vvl}" "VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true"
                "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true"
                "ICO_VK_VALIDATION=1")
endif()
if(ICO_SANITIZE MATCHES "address")
    list(APPEND _sc_env "ASAN_OPTIONS=detect_leaks=0")
endif()

# rd_stencil_selftest_test: the 10 s clause and the --stats words (CPU), then
# on a Vulkan device with the local read (77 otherwise) the fog's and the
# stencil's self-tests on the local path with ICO_RD_STENCIL_PACKED=0 (the
# self-test passes: its eight texels the GS's and the forced replay's; the
# cut's replay asks no stencil store at 2 passes or more and no stencil load
# at 1 or more), the decision by path and switch on this class, no validation
# error; rd_stencil_selftest_poison the same with ICO_RD_STORE_POISON=1 (the
# stencil the cut leaves undefined cleared to 0x5A)
add_executable(rd_stencil_selftest_test test/rd_stencil_selftest_test.c)
target_compile_options(rd_stencil_selftest_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                                                        ${ICO_PORT_WARNINGS})
target_include_directories(rd_stencil_selftest_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                            "${CMAKE_SOURCE_DIR}/port/rhi")
target_link_libraries(rd_stencil_selftest_test PRIVATE ico_render)
if(COMMAND ico_copy_sdl3_dll)
    ico_copy_sdl3_dll(rd_stencil_selftest_test)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME rd_stencil_selftest COMMAND rd_stencil_selftest_test)
    set_tests_properties(rd_stencil_selftest PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                         "${_sc_env};ICO_RD_FOG_PATH=local;ICO_RD_STENCIL_PACKED=0")
    add_test(NAME rd_stencil_selftest_poison COMMAND rd_stencil_selftest_test)
    set_tests_properties(rd_stencil_selftest_poison PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                         "${_sc_env};ICO_RD_FOG_PATH=local;ICO_RD_STENCIL_PACKED=0;ICO_RD_STORE_POISON=1")
endif()

# rd_stencil_ab (test/rd_stencil_ab.cmake): the dumps of tools/stencil_dumps.txt
# (machine-local; a listed dump that is absent fails) with the stencil forced
# (ICO_RD_STENCIL_PACKED=1) and inside the shadow window only (=0) at the
# PowerVR phone's profile on lavapipe and the Mali's, under the store plan's
# poison: every target the same bytes, the same draws, passes and colour MB,
# the depth and stencil MB stored and loaded lower by exactly what the cut's
# stats name.  No device: SKIP (77).
if(ICO_RUN_TESTS AND TARGET rd_replay_tool)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/stencil_ab")
    add_test(NAME rd_stencil_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/stencil_dumps.txt"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/stencil_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_stencil_ab.cmake")
    set_tests_properties(rd_stencil_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 5400)
    if(_sc_env)
        set_tests_properties(rd_stencil_ab PROPERTIES ENVIRONMENT "${_sc_env}")
    endif()
endif()
