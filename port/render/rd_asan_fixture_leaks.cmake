# rd_asan_fixture_leaks.cmake: under AddressSanitizer, the leak check off for
# every test that replays the committed fixtures with rd_replay_tool (a
# -DFIXTURES= argument and $<TARGET_FILE:rd_replay_tool> in its command, as
# tools/ci_shard.py reads_fixtures counts them), included last from
# port/render/CMakeLists.txt so that it follows every set_tests_properties
# that sets a test's ENVIRONMENT (the OpenGL twins set theirs whole).
#
# Why: v0.11.7 made these tests run in CI (docs/port/TESTING_FIXTURES.md), and
# CI run 43805687280's asan shards failed 30 of them on the driver's exit leak
# alone, the one described at rd_blend_selftest (port/render/CMakeLists.txt):
# every leaked object's allocating frame is in an unloaded module (llvmpipe
# or lavapipe, dlclosed at exit) or libLLVM, called from glDrawArrays
# (rhi_gl_cmd_draw, gl_cmd.c), SDL_EGL_InitializeOffscreen, the Vulkan
# loader's vkEnumeratePhysicalDevices (vkr_pick_device) or a driver thread
# (asan_thread_start with no frame of the port's); 56, 128 and 2680 byte
# objects; no report names a frame of the port's as the allocator. An LSan
# suppression cannot name the unloaded frames, and the leak races the exit
# (the replay tool's tests: clean in run 43548413539, leaking in 43554209427
# on the same sources), so any of the family can hit it: the check is off for
# the family, as for rd_blend_selftest's and the replay tool's twelve.
# The tests' own checks (the pictures, the counters, the A/B relations) and
# AddressSanitizer's other reports and UBSan stay on.
if(ICO_SANITIZE MATCHES "address")
    get_property(_afl_tests DIRECTORY PROPERTY TESTS)
    foreach(_afl_t IN LISTS _afl_tests)
        # the command as add_test was given it (cmake/IcoTests.cmake keeps it)
        get_property(_afl_cmd GLOBAL PROPERTY "ICO_TEST_COMMAND_${_afl_t}")
        string(FIND "${_afl_cmd}" "-DFIXTURES=" _afl_fx)
        string(FIND "${_afl_cmd}" "$<TARGET_FILE:rd_replay_tool>" _afl_tool)
        if(_afl_fx GREATER -1 AND _afl_tool GREATER -1)
            get_test_property(${_afl_t} ENVIRONMENT _afl_env)
            if(NOT "${_afl_env}" MATCHES "ASAN_OPTIONS=detect_leaks=0")
                set_property(TEST ${_afl_t} APPEND PROPERTY ENVIRONMENT
                                                    "ASAN_OPTIONS=detect_leaks=0")
            endif()
        endif()
    endforeach()
endif()
