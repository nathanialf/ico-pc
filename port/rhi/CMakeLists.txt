# port/rhi/CMakeLists.txt: the render hardware interface's backends and
# their tests. port/rhi/vk/README.md describes the Vulkan backend.
#
# Dependencies come from tools/fetch_deps.sh (tools/toolchain/deps/):
# Vulkan-Headers and volk (the backend loads libvulkan / vulkan-1.dll at run
# time, so nothing links against a Vulkan import library) and SDL3 for the
# window surface. Without the Vulkan pieces the backend is left out; without
# SDL3 it is built headless-only (rhi_Init with a window fails).

option(ICO_RHI_VULKAN "Build the Vulkan backend of port/rhi (ico_rhi_vk)" ON)
set(ICO_DEPS_DIR "${CMAKE_SOURCE_DIR}/tools/toolchain/deps" CACHE PATH
    "Prefix of the dependencies tools/fetch_deps.sh installs")

if(NOT ICO_RHI_VULKAN)
    message(STATUS "ico: Vulkan RHI disabled (ICO_RHI_VULKAN=OFF)")
    return()
endif()

# --- Vulkan headers and volk --------------------------------------------------
# Plain paths, not find_path: the cross toolchains re-root find_* searches
# into the mingw sysroot.
set(ICO_VULKAN_INCLUDE_DIR "${ICO_DEPS_DIR}/vulkan-headers/include" CACHE PATH
    "Vulkan-Headers include directory")
set(ICO_VOLK_DIR "${ICO_DEPS_DIR}/volk" CACHE PATH "volk source directory")
if(NOT EXISTS "${ICO_VULKAN_INCLUDE_DIR}/vulkan/vulkan_core.h")
    set(ICO_VULKAN_INCLUDE_DIR "")
endif()
if(NOT EXISTS "${ICO_VOLK_DIR}/volk.c")
    set(ICO_VOLK_DIR "")
endif()
if(NOT ICO_VULKAN_INCLUDE_DIR OR NOT ICO_VOLK_DIR)
    message(STATUS "ico: Vulkan RHI not built: Vulkan-Headers or volk missing under "
                   "${ICO_DEPS_DIR} (run tools/fetch_deps.sh)")
    return()
endif()

# --- SDL3 ---------------------------------------------------------------------
# One prefix per target: the source build for Linux x86-64, SDL's mingw
# development release for the two Windows targets. SDL3_DIR is set directly
# so the cross toolchains' CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY does not
# hide the user-local prefix.
set(_ico_sdl3_prefix "")
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/mingw/x86_64-w64-mingw32")
elseif(WIN32)
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/mingw/i686-w64-mingw32")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/linux-x64")
endif()
if(NOT SDL3_DIR AND _ico_sdl3_prefix AND EXISTS "${_ico_sdl3_prefix}/lib/cmake/SDL3/SDL3Config.cmake")
    set(SDL3_DIR "${_ico_sdl3_prefix}/lib/cmake/SDL3" CACHE PATH "SDL3 CMake package directory")
endif()
find_package(SDL3 CONFIG QUIET COMPONENTS SDL3-shared)
if(SDL3_FOUND)
    message(STATUS "ico: SDL3 ${SDL3_VERSION} from ${SDL3_DIR}")
else()
    message(STATUS "ico: SDL3 not found for this target (run tools/fetch_deps.sh); the Vulkan "
                   "RHI is built headless-only")
endif()

# --- ico_rhi_vk -----------------------------------------------------------------
add_library(ico_rhi_vk STATIC
    vk/vk_cmd.c
    vk/vk_device.c
    vk/vk_pipeline.c
    vk/vk_resource.c
    vk/vk_swapchain.c
    "${ICO_VOLK_DIR}/volk.c")
target_include_directories(ico_rhi_vk
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${ICO_VOLK_DIR}" "${ICO_VULKAN_INCLUDE_DIR}")
target_compile_definitions(ico_rhi_vk PRIVATE VK_NO_PROTOTYPES)
target_compile_options(ico_rhi_vk PRIVATE ${ICO_SEMANTIC_OPTIONS})
set_source_files_properties(vk/vk_cmd.c vk/vk_device.c vk/vk_pipeline.c vk/vk_resource.c
                            vk/vk_swapchain.c PROPERTIES COMPILE_OPTIONS "${ICO_PORT_WARNINGS}")
target_link_libraries(ico_rhi_vk PRIVATE ${CMAKE_DL_LIBS})
if(SDL3_FOUND)
    target_compile_definitions(ico_rhi_vk PRIVATE ICO_RHI_HAVE_SDL=1)
    target_link_libraries(ico_rhi_vk PUBLIC SDL3::SDL3)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug builds ask for the validation layer even when the caller's
    # RhiDeviceDesc.debugLayers is false; ICO_VK_VALIDATION=0/1 overrides.
    target_compile_definitions(ico_rhi_vk PRIVATE ICO_RHI_DEBUG_DEFAULT=1)
endif()

# --- Tests ----------------------------------------------------------------------
set(_ico_rhi_run_tests OFF)
set(_ico_vk_test_env "")
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    set(_ico_rhi_run_tests ON)
endif()

# Every rhi.h enumeration maps to a Vulkan value (static asserts plus a run
# over the tables). Needs only the headers.
add_executable(rhi_vk_enum_test test/rhi_vk_enum_test.c)
target_include_directories(rhi_vk_enum_test PRIVATE "${ICO_VULKAN_INCLUDE_DIR}")
target_compile_options(rhi_vk_enum_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

# Headless rendering on whatever device is present (lavapipe in the
# container); exits 77 (skipped) when there is no Vulkan device.
add_executable(rhi_vk_test test/rhi_vk_test.c)
target_include_directories(rhi_vk_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test/shaders")
target_compile_options(rhi_vk_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rhi_vk_test PRIVATE ico_rhi_vk)

# The window path (surface, swapchain, present, resize) through SDL's
# offscreen video driver; needs SDL3.
if(SDL3_FOUND)
    add_executable(rhi_vk_swapchain_test test/rhi_vk_swapchain_test.c)
    target_compile_options(rhi_vk_swapchain_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                           ${ICO_PORT_WARNINGS})
    target_link_libraries(rhi_vk_swapchain_test PRIVATE ico_rhi_vk SDL3::SDL3)
endif()

# Windows: put SDL3.dll beside the test programs so they run from the
# build directory.
if(WIN32 AND SDL3_FOUND)
    foreach(t rhi_vk_test rhi_vk_swapchain_test)
        add_custom_command(TARGET ${t} POST_BUILD
                           COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                                   "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:${t}>"
                           VERBATIM)
    endforeach()
endif()

if(_ico_rhi_run_tests)
    add_test(NAME rhi_vk_enum COMMAND rhi_vk_enum_test)
    add_test(NAME rhi_vk COMMAND rhi_vk_test)
    set_tests_properties(rhi_vk PROPERTIES SKIP_RETURN_CODE 77)
    # The validation layer tools/fetch_deps.sh unpacks, with synchronisation
    # validation on; the test fails on any validation error.
    set(_ico_vvl "${ICO_DEPS_DIR}/vulkan-validation")
    if(NOT WIN32 AND EXISTS "${_ico_vvl}/VkLayer_khronos_validation.json")
        set(_ico_vk_test_env
            "VK_ADD_LAYER_PATH=${_ico_vvl};VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true")
    endif()
    if(ICO_SANITIZE MATCHES "address")
        # LeakSanitizer reports 112 bytes the Vulkan driver (lavapipe/LLVM)
        # allocates and still references when the loader dlcloses it at
        # vkDestroyInstance; with dlclose made a no-op the report is gone, so
        # the roots live in the unloaded driver, not in the RHI.
        list(APPEND _ico_vk_test_env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    if(_ico_vk_test_env)
        set_tests_properties(rhi_vk PROPERTIES ENVIRONMENT "${_ico_vk_test_env}")
    endif()
    if(TARGET rhi_vk_swapchain_test)
        add_test(NAME rhi_vk_swapchain COMMAND rhi_vk_swapchain_test)
        set_tests_properties(rhi_vk_swapchain PROPERTIES SKIP_RETURN_CODE 77)
        if(_ico_vk_test_env)
            set_tests_properties(rhi_vk_swapchain PROPERTIES ENVIRONMENT "${_ico_vk_test_env}")
        endif()
    endif()
endif()
