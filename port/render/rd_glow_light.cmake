# rd_glow_light.cmake: the glow drawn only where it can add light
# (rd_glow_light.c; docs/port/RENDER_POST.md, "The glow where it adds
# light"), included from rd_msaa_shadow.cmake after ico_render (a file of
# its own: port/render/CMakeLists.txt is at the 256 KiB cap,
# tools/check_no_rom.sh).  The rd_blur case glow_light (the CPU GS model) is
# in rd_blur_test.c's shards.
target_sources(ico_render PRIVATE rd_glow_light.c)

# rd_glow_light_ab (test/rd_glow_light_ab.cmake): every glow dump of
# tools/glow_light_dumps.txt (machine-local; a listed dump that is absent
# fails) with the plan and with the chain drawn whole (ICO_RD_GLOW_LIGHT=0)
# at the phone's setting, Original and Enhanced 2x 4:3: RGB 0 levels on
# every target and the present, the alpha only on WORK1, WORK2, WORK3,
# WORK3_HELD and SCENE in frames whose WORK2 kept the fill, every byte at
# 1x, the glow's passes by plan; under the validation layer where the
# toolchain has it
if(ICO_RUN_TESTS AND Python3_FOUND)
    set(_gl_env "")
    if(NOT WIN32 AND EXISTS "${ICO_DEPS_DIR}/vulkan-validation/VkLayer_khronos_validation.json")
        set(_gl_env "VK_ADD_LAYER_PATH=${ICO_DEPS_DIR}/vulkan-validation"
                    "VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true"
                    "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true"
                    "ICO_VK_VALIDATION=1")
    endif()
    if(ICO_SANITIZE MATCHES "address")
        list(APPEND _gl_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/glow_light_ab")
    add_test(NAME rd_glow_light_ab
             COMMAND "${CMAKE_COMMAND}" -DTOOL=$<TARGET_FILE:rd_replay_tool>
                     "-DLIST=${CMAKE_SOURCE_DIR}/tools/glow_light_dumps.txt"
                     "-DPY=${Python3_EXECUTABLE}"
                     "-DDIFF=${CMAKE_CURRENT_SOURCE_DIR}/test/png_level_diff.py"
                     "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/glow_light_ab"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/rd_glow_light_ab.cmake")
    set_tests_properties(rd_glow_light_ab PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
                         TIMEOUT 10800)
    if(_gl_env)
        set_tests_properties(rd_glow_light_ab PROPERTIES ENVIRONMENT "${_gl_env}")
    endif()
endif()
