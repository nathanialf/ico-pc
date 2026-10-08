# port/shaders/test: the shader toolchain's tests.
#   shaders_table  every embedded entry compiled for SPIR-V and signed DXIL
#   gs_math        gs_math.hlsli as C against port/test/gs_blend_test.c's formulas
#   shaders_pixel  sprite, blit and blend_int drawn through the Vulkan RHI
#                  (skips, exit 77, without a Vulkan device)
#   vu1            the VU1 program shaders against port/render/vu1_ref: hand
#                  traces (CPU), probe and rendered strips (Vulkan; exit 77
#                  without a device once the CPU part passes)
set(_ico_shaders_run_tests OFF)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    set(_ico_shaders_run_tests ON)
endif()

add_executable(shaders_table_test shaders_table_test.c)
target_compile_options(shaders_table_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(shaders_table_test PRIVATE ico_shaders)

add_executable(gs_math_test gs_math_test.c)
target_include_directories(gs_math_test PRIVATE "${ICO_SHADER_DIR}")
target_compile_options(gs_math_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

if(TARGET ico_rhi_vk)
    add_executable(shaders_pixel_test shaders_pixel_test.c)
    target_include_directories(shaders_pixel_test PRIVATE "${ICO_SHADER_DIR}")
    target_compile_options(shaders_pixel_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(shaders_pixel_test PRIVATE ico_shaders ico_rhi_vk)
    if(WIN32 AND SDL3_FOUND)
        add_custom_command(TARGET shaders_pixel_test POST_BUILD
                           COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                                   "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:shaders_pixel_test>"
                           VERBATIM)
    endif()
endif()

if(TARGET ico_rhi_vk)
    add_executable(vu1_test vu1_test.c)
    target_include_directories(vu1_test PRIVATE "${ICO_SHADER_DIR}" "${CMAKE_SOURCE_DIR}/port/render")
    target_compile_options(vu1_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(vu1_test PRIVATE ico_shaders ico_rhi_vk ico_vu1_ref)
    if(WIN32 AND SDL3_FOUND)
        add_custom_command(TARGET vu1_test POST_BUILD
                           COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                                   "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:vu1_test>"
                           VERBATIM)
    endif()
endif()

if(_ico_shaders_run_tests)
    add_test(NAME shaders_table COMMAND shaders_table_test)
    add_test(NAME gs_math COMMAND gs_math_test)
    if(TARGET shaders_pixel_test)
        add_test(NAME shaders_pixel COMMAND shaders_pixel_test)
        set_tests_properties(shaders_pixel PROPERTIES SKIP_RETURN_CODE 77)
        set(_ico_vvl "${ICO_DEPS_DIR}/vulkan-validation")
        set(_env "")
        if(NOT WIN32 AND EXISTS "${_ico_vvl}/VkLayer_khronos_validation.json")
            set(_env "VK_ADD_LAYER_PATH=${_ico_vvl};VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true")
        endif()
        if(ICO_SANITIZE MATCHES "address")
            list(APPEND _env "ASAN_OPTIONS=detect_leaks=0")
        endif()
        if(_env)
            set_tests_properties(shaders_pixel PROPERTIES ENVIRONMENT "${_env}")
        endif()
        # package AN-E: the two-pass blend on a device made without dualSrcBlend
        add_test(NAME shaders_pixel_nodual COMMAND shaders_pixel_test --nodual)
        set(_env_nodual ${_env} "ICO_VK_FAKE_NO_DUAL=1")
        set_tests_properties(shaders_pixel_nodual PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_env_nodual}")
        # v0.4.2 (Android): on a Mali-G68's limits (ICO_VK_FAKE_LIMITS=mali)
        add_test(NAME shaders_pixel_mali COMMAND shaders_pixel_test)
        set(_env_mali ${_env} "ICO_VK_FAKE_LIMITS=mali")
        set_tests_properties(shaders_pixel_mali PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_env_mali}")
    endif()
    if(TARGET vu1_test)
        add_test(NAME vu1 COMMAND vu1_test)
        set_tests_properties(vu1 PROPERTIES SKIP_RETURN_CODE 77)
        if(_env)
            set_tests_properties(vu1 PROPERTIES ENVIRONMENT "${_env}")
        endif()
        add_test(NAME vu1_mali COMMAND vu1_test)
        set(_env_vu1_mali ${_env} "ICO_VK_FAKE_LIMITS=mali")
        set_tests_properties(vu1_mali PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_env_vu1_mali}")
    endif()
endif()
