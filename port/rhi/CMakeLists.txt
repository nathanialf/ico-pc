# port/rhi/CMakeLists.txt: the render hardware interface's backends and
# their tests.
#
# Libraries:
#   ico_rhi_vulkan  the Vulkan backend (option ICO_RHI_VULKAN, default ON)
#   ico_rhi_d3d12   the D3D12 backend (option ICO_RHI_D3D12, default ON for
#                   64-bit Windows: the win-x64 and win-x64-clang presets)
#   ico_rhi         rhi_backend.c, the rhi.h entry points forwarded to the
#                   backend rhi_create_backend selects; links the backends
#   ico_rhi_vk      what port/render and the shader tests link (the name
#                   predates the second backend): ico_rhi
# Each backend is compiled with RHI_BACKEND_PREFIX (rhi_backend_names.h).
#
# Dependencies come from tools/fetch_deps.sh (tools/toolchain/deps/):
# Vulkan-Headers and volk (the backend loads libvulkan / vulkan-1.dll at run
# time, so nothing links against a Vulkan import library) and SDL3 for the
# window surface. Without the Vulkan pieces the Vulkan backend is left out;
# without SDL3 the backends are built headless-only (rhi_init with a window
# fails). The D3D12 backend uses the mingw-w64 toolchain's own d3d12.h,
# dxgi1_6.h and libdxguid.a, and loads d3d12.dll and dxgi.dll at run time.

option(ICO_RHI_VULKAN "Build the Vulkan backend of port/rhi (ico_rhi_vulkan)" ON)
set(_ico_d3d12_default OFF)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_ico_d3d12_default ON)
endif()
option(ICO_RHI_D3D12 "Build the D3D12 backend of port/rhi (ico_rhi_d3d12; Windows only)"
       ${_ico_d3d12_default})

# --- The D3D12 plan test (CPU only, every target) ------------------------------
# The D3D12 backend's header-free bookkeeping (d3d12/d3d12_plan.c): state
# transitions, descriptor rings, layouts, the DXBC signature reader; with the
# shader table (ico_shaders, port/shaders) also every game vertex shader's
# DXIL inputs against its SPIR-V locations.
add_executable(rhi_d3d12_plan_test test/rhi_d3d12_plan_test.c d3d12/d3d12_plan.c)
target_include_directories(rhi_d3d12_plan_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/test/shaders")
target_compile_options(rhi_d3d12_plan_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(rhi_d3d12_plan_test PRIVATE m)
endif()
# (the table's DXIL: not with ICO_SHADERS_DXIL=OFF, cmake/IcoShaders.cmake)
function(_ico_rhi_plan_test_shaders)
    if(TARGET ico_shaders AND ICO_SHADERS_DXIL)
        target_link_libraries(rhi_d3d12_plan_test PRIVATE ico_shaders)
        target_compile_definitions(rhi_d3d12_plan_test PRIVATE ICO_HAVE_SHADER_TABLE=1)
    endif()
endfunction()
# port/shaders is configured after port/rhi
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _ico_rhi_plan_test_shaders)
if(ICO_RUN_TESTS)
    add_test(NAME rhi_d3d12_plan COMMAND rhi_d3d12_plan_test)
endif()

# --- SDL3 ---------------------------------------------------------------------
# One prefix per target: the source build for Linux x86-64, SDL's mingw
# development release for the two Windows targets, the NDK build for
# Android arm64. SDL3_DIR is set directly
# so the cross toolchains' CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY does not
# hide the user-local prefix.
set(_ico_sdl3_prefix "")
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/mingw/x86_64-w64-mingw32")
elseif(ANDROID AND CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    # tools/fetch_android.sh's NDK build
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/android-arm64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/linux-x64")
endif()
if(NOT SDL3_DIR AND _ico_sdl3_prefix AND EXISTS "${_ico_sdl3_prefix}/lib/cmake/SDL3/SDL3Config.cmake")
    set(SDL3_DIR "${_ico_sdl3_prefix}/lib/cmake/SDL3" CACHE PATH "SDL3 CMake package directory")
endif()
find_package(SDL3 CONFIG QUIET COMPONENTS SDL3-shared)
if(SDL3_FOUND)
    message(STATUS "ico: SDL3 ${SDL3_VERSION} from ${SDL3_DIR}")
    # the windowed ico_pc (top-level CMakeLists.txt) links and copies it too
    if(TARGET SDL3::SDL3-shared)
        set_target_properties(SDL3::SDL3-shared PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
else()
    message(STATUS "ico: SDL3 not found for this target (run tools/fetch_deps.sh); the RHI "
                   "backends are built headless-only")
endif()

# --- Vulkan headers and volk --------------------------------------------------
# Plain paths, not find_path: the cross toolchains re-root find_* searches
# into the mingw sysroot.
set(_ico_have_vk OFF)
if(ICO_RHI_VULKAN)
    set(ICO_VULKAN_INCLUDE_DIR "${ICO_DEPS_DIR}/vulkan-headers/include" CACHE PATH
        "Vulkan-Headers include directory")
    set(ICO_VOLK_DIR "${ICO_DEPS_DIR}/volk" CACHE PATH "volk source directory")
    if(NOT EXISTS "${ICO_VULKAN_INCLUDE_DIR}/vulkan/vulkan_core.h")
        set(ICO_VULKAN_INCLUDE_DIR "")
    endif()
    if(NOT EXISTS "${ICO_VOLK_DIR}/volk.c")
        set(ICO_VOLK_DIR "")
    endif()
    if(ICO_VULKAN_INCLUDE_DIR AND ICO_VOLK_DIR)
        set(_ico_have_vk ON)
    else()
        message(STATUS "ico: Vulkan RHI not built: Vulkan-Headers or volk missing under "
                       "${ICO_DEPS_DIR} (run tools/fetch_deps.sh)")
    endif()
else()
    message(STATUS "ico: Vulkan RHI disabled (ICO_RHI_VULKAN=OFF)")
endif()

# --- D3D12 headers ------------------------------------------------------------
set(_ico_have_d3d12 OFF)
if(ICO_RHI_D3D12)
    if(NOT WIN32)
        message(STATUS "ico: D3D12 RHI is Windows-only; not built for ${CMAKE_SYSTEM_NAME}")
    else()
        include(CheckIncludeFile)
        check_include_file(d3d12.h ICO_HAVE_D3D12_H)
        check_include_file(dxgi1_6.h ICO_HAVE_DXGI1_6_H)
        if(ICO_HAVE_D3D12_H AND ICO_HAVE_DXGI1_6_H)
            set(_ico_have_d3d12 ON)
        else()
            message(STATUS "ico: D3D12 RHI not built: the toolchain lacks d3d12.h or dxgi1_6.h")
        endif()
    endif()
endif()

if(NOT _ico_have_vk AND NOT _ico_have_d3d12)
    return()
endif()

set(_ico_rhi_debug_default "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug builds ask for the validation / debug layer even when the
    # caller's RhiDeviceDesc.debugLayers is false; ICO_VK_VALIDATION=0/1 and
    # ICO_D3D12_DEBUG=0/1 override.
    set(_ico_rhi_debug_default ICO_RHI_DEBUG_DEFAULT=1)
endif()

# --- ico_rhi_vulkan -------------------------------------------------------------
if(_ico_have_vk)
    set(_ico_vk_sources vk/vk_cmd.c vk/vk_device.c vk/vk_pipeline.c vk/vk_present_mode.c
                        vk/vk_resource.c vk/vk_swapchain.c)
    if(ANDROID)
        # the window's surface through the loader in use (a
        # driver the program loaded, or the system's)
        list(APPEND _ico_vk_sources vk/vk_surface_android.c)
    endif()
    add_library(ico_rhi_vulkan STATIC ${_ico_vk_sources} "${ICO_VOLK_DIR}/volk.c")
    target_include_directories(ico_rhi_vulkan
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
        PRIVATE "${ICO_VOLK_DIR}" "${ICO_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(ico_rhi_vulkan PRIVATE VK_NO_PROTOTYPES RHI_BACKEND_PREFIX=vk
                               ${_ico_rhi_debug_default})
    target_compile_options(ico_rhi_vulkan PRIVATE ${ICO_SEMANTIC_OPTIONS})
    set_source_files_properties(${_ico_vk_sources} PROPERTIES COMPILE_OPTIONS
                                "${ICO_PORT_WARNINGS}")
    target_link_libraries(ico_rhi_vulkan PRIVATE ${CMAKE_DL_LIBS})
    if(SDL3_FOUND)
        target_compile_definitions(ico_rhi_vulkan PRIVATE ICO_RHI_HAVE_SDL=1)
        target_link_libraries(ico_rhi_vulkan PUBLIC SDL3::SDL3)
    endif()
endif()

# --- ico_rhi_d3d12 --------------------------------------------------------------
if(_ico_have_d3d12)
    add_library(ico_rhi_d3d12 STATIC
        d3d12/d3d12_cmd.c
        d3d12/d3d12_device.c
        d3d12/d3d12_pipeline.c
        d3d12/d3d12_plan.c
        d3d12/d3d12_resource.c
        d3d12/d3d12_swapchain.c)
    target_include_directories(ico_rhi_d3d12 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(ico_rhi_d3d12 PRIVATE RHI_BACKEND_PREFIX=d3d12
                               ${_ico_rhi_debug_default})
    target_compile_options(ico_rhi_d3d12 PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    # the IIDs (IID_ID3D12Device, ...); d3d12.dll and dxgi.dll are loaded at
    # run time (d3d12_device.c)
    target_link_libraries(ico_rhi_d3d12 PRIVATE dxguid)
    if(SDL3_FOUND)
        target_compile_definitions(ico_rhi_d3d12 PRIVATE ICO_RHI_HAVE_SDL=1)
        target_link_libraries(ico_rhi_d3d12 PUBLIC SDL3::SDL3)
    endif()
    message(STATUS "ico: D3D12 RHI built (ico_rhi_d3d12)")
endif()

# --- ico_rhi: the dispatcher ----------------------------------------------------
add_library(ico_rhi STATIC rhi_backend.c)
target_include_directories(ico_rhi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_rhi PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(_ico_have_vk)
    target_compile_definitions(ico_rhi PRIVATE ICO_RHI_HAVE_VK=1)
    target_link_libraries(ico_rhi PUBLIC ico_rhi_vulkan)
endif()
if(_ico_have_d3d12)
    target_compile_definitions(ico_rhi PRIVATE ICO_RHI_HAVE_D3D12=1)
    target_link_libraries(ico_rhi PUBLIC ico_rhi_d3d12)
endif()

# port/render and port/shaders/test link ico_rhi_vk and test for it: keep
# the name, present when the Vulkan backend is (the renderer's tests need it).
if(_ico_have_vk)
    add_library(ico_rhi_vk INTERFACE)
    target_link_libraries(ico_rhi_vk INTERFACE ico_rhi)
endif()

# --- Tests ----------------------------------------------------------------------
# rhi_test_common.c: the exact-texel cells every backend must pass.
set(_ico_rhi_test_common test/rhi_test_common.c)

if(_ico_have_vk)
    set(_ico_vk_test_env "")
    # Every rhi.h enumeration maps to a Vulkan value (static asserts plus a
    # run over the tables). Needs only the headers.
    # With rhi_backend.c's layer classifiers (no backend linked).
    add_executable(rhi_vk_enum_test test/rhi_vk_enum_test.c rhi_backend.c)
    target_include_directories(rhi_vk_enum_test PRIVATE "${ICO_VULKAN_INCLUDE_DIR}")
    target_compile_options(rhi_vk_enum_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

    # The swapchain's present-mode choice (vk/vk_present_mode.c) over the
    # mode sets drivers offer; no device, no SDL.
    add_executable(rhi_vk_present_mode_test test/rhi_vk_present_mode_test.c
                   vk/vk_present_mode.c)
    target_include_directories(rhi_vk_present_mode_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                               "${ICO_VOLK_DIR}" "${ICO_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(rhi_vk_present_mode_test PRIVATE VK_NO_PROTOTYPES)
    target_compile_options(rhi_vk_present_mode_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                           ${ICO_PORT_WARNINGS})

    # Headless rendering on whatever device is present (lavapipe in the
    # container); exits 77 (skipped) when there is no Vulkan device.
    add_executable(rhi_vk_test test/rhi_vk_test.c ${_ico_rhi_test_common})
    target_include_directories(rhi_vk_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test/shaders")
    target_compile_options(rhi_vk_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(rhi_vk_test PRIVATE ico_rhi)

    # The window path (surface, swapchain, present, resize) through SDL's
    # offscreen video driver; needs SDL3.
    if(SDL3_FOUND)
        add_executable(rhi_vk_swapchain_test test/rhi_vk_swapchain_test.c)
        target_compile_options(rhi_vk_swapchain_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                               ${ICO_PORT_WARNINGS})
        target_link_libraries(rhi_vk_swapchain_test PRIVATE ico_rhi SDL3::SDL3)
    endif()

    # Windows: SDL3.dll beside the test programs
    if(SDL3_FOUND)
        ico_copy_sdl3_dll(rhi_vk_test rhi_vk_swapchain_test)
    endif()

    if(ICO_RUN_TESTS)
        add_test(NAME rhi_vk_enum COMMAND rhi_vk_enum_test)
        add_test(NAME rhi_vk_present_mode COMMAND rhi_vk_present_mode_test)
        add_test(NAME rhi_vk COMMAND rhi_vk_test)
        set_tests_properties(rhi_vk PROPERTIES SKIP_RETURN_CODE 77)
        # The validation layer tools/fetch_deps.sh unpacks, with
        # synchronisation validation on; the test fails on any validation
        # error.
        set(_ico_vvl "${ICO_DEPS_DIR}/vulkan-validation")
        if(NOT WIN32 AND EXISTS "${_ico_vvl}/VkLayer_khronos_validation.json")
            set(_ico_vk_test_env
                "VK_ADD_LAYER_PATH=${_ico_vvl};VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true")
        endif()
        if(ICO_SANITIZE MATCHES "address")
            # LeakSanitizer reports 112 bytes the Vulkan driver
            # (lavapipe/LLVM) allocates and still references when the loader
            # dlcloses it at vkDestroyInstance; with dlclose made a no-op the
            # report is gone, so the roots live in the unloaded driver, not in
            # the RHI.
            list(APPEND _ico_vk_test_env "ASAN_OPTIONS=detect_leaks=0")
        endif()
        if(_ico_vk_test_env)
            set_tests_properties(rhi_vk PROPERTIES ENVIRONMENT "${_ico_vk_test_env}")
        endif()
        # the same cells on a device made without dualSrcBlend
        # (the hook clears the feature at device creation); its own directory
        # for the pipeline cache file, which rhi_vk writes beside itself
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/nodual")
        add_test(NAME rhi_vk_nodual COMMAND rhi_vk_test
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/nodual")
        set(_ico_vk_nodual_env ${_ico_vk_test_env} "ICO_VK_FAKE_NO_DUAL=1")
        set_tests_properties(rhi_vk_nodual PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_nodual_env}")
        # the D24S8 depth-stencil fallback (ICO_VK_FAKE_D24S8=1)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/d24s8")
        add_test(NAME rhi_vk_d24s8 COMMAND rhi_vk_test
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/d24s8")
        set(_ico_vk_d24s8_env ${_ico_vk_test_env} "ICO_VK_FAKE_D24S8=1")
        set_tests_properties(rhi_vk_d24s8 PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_d24s8_env}")
        # the cells on a Mali-G68's limits, as a phone has them
        # (ICO_VK_FAKE_LIMITS=mali), then the limits cell: what goes past
        # them is refused by the limit's name
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mali")
        add_test(NAME rhi_vk_mali COMMAND rhi_vk_test
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mali")
        set(_ico_vk_mali_env ${_ico_vk_test_env} "ICO_VK_FAKE_LIMITS=mali")
        set_tests_properties(rhi_vk_mali PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_mali_env}")
        if(TARGET rhi_vk_swapchain_test)
            add_test(NAME rhi_vk_swapchain COMMAND rhi_vk_swapchain_test)
            set_tests_properties(rhi_vk_swapchain PROPERTIES SKIP_RETURN_CODE 77)
            if(_ico_vk_test_env)
                set_tests_properties(rhi_vk_swapchain PROPERTIES ENVIRONMENT
                                     "${_ico_vk_test_env}")
            endif()
        endif()
    endif()
endif()

# rhi_d3d12_test.exe: the same cells on D3D12, first on WARP, then on the
# hardware adapter, then the swapchain on a hidden window. Double-clicked it
# needs no console and no flags: it writes rhi_d3d12_test.log beside itself
# and shows the verdict in a message box. ctest runs it only on a native
# Windows build (ICO_RUN_TESTS).
if(_ico_have_d3d12)
    add_executable(rhi_d3d12_test test/rhi_d3d12_test.c ${_ico_rhi_test_common})
    target_include_directories(rhi_d3d12_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test/shaders")
    target_compile_options(rhi_d3d12_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(rhi_d3d12_test PRIVATE ico_rhi user32)
    target_link_options(rhi_d3d12_test PRIVATE -mwindows -static)
    if(SDL3_FOUND)
        target_compile_definitions(rhi_d3d12_test PRIVATE ICO_RHI_HAVE_SDL=1)
        target_link_libraries(rhi_d3d12_test PRIVATE SDL3::SDL3)
        ico_copy_sdl3_dll(rhi_d3d12_test)
    endif()
    if(ICO_RUN_TESTS)
        add_test(NAME rhi_d3d12 COMMAND rhi_d3d12_test --console)
        set_tests_properties(rhi_d3d12 PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
