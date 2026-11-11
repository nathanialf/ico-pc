# port/rhi/CMakeLists.txt: the render hardware interface's backends and
# their tests.
#
# Libraries:
#   ico_rhi_vulkan  the Vulkan backend (option ICO_RHI_VULKAN, default ON)
#   ico_rhi_d3d12   the D3D12 backend (option ICO_RHI_D3D12, default ON for
#                   64-bit Windows: the win-x64 and win-x64-clang presets)
#   ico_rhi_d3d11   the Direct3D 11 backend (option ICO_RHI_D3D11, default ON
#                   for 64-bit Windows; compile-only in v0.8.6: it has not
#                   run on any Windows machine)
#   ico_rhi_opengl  the OpenGL backend (option ICO_RHI_OPENGL, default ON on
#                   the desktop targets other than Apple's; built where SDL3
#                   is found: its context and every entry point come
#                   through SDL, no GL library is linked)
#   ico_rhi         rhi_backend.c, the rhi.h entry points forwarded to the
#                   backend rhi_create_backend selects; links the backends
#   ico_rhi_vk      what port/render and the shader tests link (the name
#                   predates the second backend): ico_rhi
# Each backend is compiled with RHI_BACKEND_PREFIX (rhi_backend_names.h).
# Apple targets link MoltenVK directly, with its bundled Vulkan headers,
# instead of compiling volk. cmake/IcoMoltenVK.cmake selects the SDK slice;
# tools/fetch_moltenvk.sh also builds the target's static SDL3.
#
# Dependencies come from tools/fetch_deps.sh (tools/toolchain/deps/):
# Vulkan-Headers and volk (the backend loads libvulkan / vulkan-1.dll at run
# time, so nothing links against a Vulkan import library) and SDL3 for the
# window surface. Without the Vulkan pieces the Vulkan backend is left out;
# without SDL3 the backends are built headless-only (rhi_init with a window
# fails). The D3D12 backend uses the mingw-w64 toolchain's own d3d12.h,
# dxgi1_6.h and libdxguid.a, and loads d3d12.dll and dxgi.dll at run time;
# the Direct3D 11 backend its d3d11.h, d3d11_1.h, dxgi1_6.h and d3dcompiler.h,
# and loads d3d11.dll, dxgi.dll and d3dcompiler_47.dll at run time.

option(ICO_RHI_VULKAN "Build the Vulkan backend of port/rhi (ico_rhi_vulkan)" ON)
option(ICO_RHI_MOLTENVK "Link the Vulkan backend to MoltenVK on Apple platforms" ${APPLE})
if(ICO_RHI_MOLTENVK)
    include("${CMAKE_SOURCE_DIR}/cmake/IcoMoltenVK.cmake")
endif()
set(_ico_d3d12_default OFF)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_ico_d3d12_default ON)
endif()
option(ICO_RHI_D3D12 "Build the D3D12 backend of port/rhi (ico_rhi_d3d12; Windows only)"
       ${_ico_d3d12_default})
# The Direct3D 11 backend (port/rhi/d3d11, ico_rhi_d3d11, prefix d3d11): on
# by default where D3D12 is (64-bit Windows). It compiles the embedded HLSL
# at run time (d3d11/d3d11_shader.c), so the shader table carries the
# sources with it (cmake/IcoShaders.cmake ICO_SHADERS_HLSL_SOURCE).
option(ICO_RHI_D3D11 "Build the Direct3D 11 backend of port/rhi (ico_rhi_d3d11; Windows only)"
       ${_ico_d3d12_default})
# The OpenGL backend (port/rhi/gl, ico_rhi_opengl, prefix gl): OpenGL 4.3 core
# through SDL3 on Windows and Linux (docs/port/RHI.md, "OpenGL backend"); not
# on Android or Apple targets (their presets also set it OFF), and left out
# where SDL3 is not found.  It also turns the shader table's GLSL column on
# (cmake/IcoShaders.cmake ICO_SHADERS_GLSL).
set(_ico_gl_default ON)
if(ANDROID OR APPLE)
    set(_ico_gl_default OFF)
endif()
option(ICO_RHI_OPENGL "Build the OpenGL backend of port/rhi (ico_rhi_opengl)" ${_ico_gl_default})
set(_ico_have_gl OFF)
set(_ico_have_d3d11 OFF)

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

# --- The Direct3D 11 shader route test (CPU only, every target) ---------------
# d3d11/d3d11_shader.c's portable part: the macros, the include lookup, the
# flat-register rule over the test shaders (rhi_test_hlsl.h, checked against
# rhi_test.hlsl) and the game's sources (the embedded ones with
# ICO_SHADERS_HLSL_SOURCE, else port/shaders' files), the cache key over the
# shader table's entries and the cache file. ico_rhi_d3d11 links the same
# file (its run-time compile).
add_executable(rhi_d3d11_shader_plan_test test/rhi_d3d11_shader_plan_test.c
               d3d11/d3d11_shader.c)
target_include_directories(rhi_d3d11_shader_plan_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/test/shaders")
target_compile_definitions(rhi_d3d11_shader_plan_test PRIVATE
    ICO_RHI_TEST_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/shaders"
    ICO_GAME_SHADER_DIR="${CMAKE_SOURCE_DIR}/port/shaders"
    # the vendored shader headers the sources include by base name
    # (cmake/IcoShaders.cmake ICO_SHADER_THIRD_PARTY_DIRS; the Direct3D 11
    # sources embed them too)
    ICO_GAME_SHADER_THIRD_PARTY_DIR="${CMAKE_SOURCE_DIR}/port/third_party/fsr1")
target_compile_options(rhi_d3d11_shader_plan_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                       ${ICO_PORT_WARNINGS})
# the table's entries (hlslFile and defines are in every table; the sources
# only with ICO_SHADERS_HLSL_SOURCE, which ico_shaders says through
# ICO_SHADERS_NO_HLSL_SOURCE)
function(_ico_rhi_d3d11_shader_test_shaders)
    if(TARGET ico_shaders)
        target_link_libraries(rhi_d3d11_shader_plan_test PRIVATE ico_shaders)
        target_compile_definitions(rhi_d3d11_shader_plan_test PRIVATE ICO_HAVE_SHADER_TABLE=1)
    endif()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _ico_rhi_d3d11_shader_test_shaders)
if(ICO_RUN_TESTS)
    add_test(NAME rhi_d3d11_shader_plan COMMAND rhi_d3d11_shader_plan_test
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# --- The OpenGL backend's GLSL inflate (CPU only, every target) ----------------
# gl/gl_glsl.c: the table's deflated GLSL back to text (miniz). Nothing links
# it into a backend yet (the OpenGL backend's own package does).
add_executable(rhi_gl_glsl_test test/rhi_gl_glsl_test.c gl/gl_glsl.c)
target_include_directories(rhi_gl_glsl_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rhi_gl_glsl_test PRIVATE ico_miniz)
target_compile_options(rhi_gl_glsl_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rhi_gl_glsl COMMAND rhi_gl_glsl_test)
endif()

# --- The OpenGL backend's fence wait with a limit (CPU only, every target) -----
# gl/gl_sync_wait.c: gl_device.c's gl_wait_sync held to the GPU hang limit
# in slices with the window's pump between them, on a stub fence and a fake
# clock (rhi_gl_sync_wait_test.c).
add_executable(rhi_gl_sync_wait_test test/rhi_gl_sync_wait_test.c gl/gl_sync_wait.c)
target_include_directories(rhi_gl_sync_wait_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rhi_gl_sync_wait_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rhi_gl_sync_wait COMMAND rhi_gl_sync_wait_test)
endif()

# --- The driver version text (CPU only, every target) -----------------------
# rhi_driver_version.c: a Vulkan driverVersion as the vendor writes it (the
# adapter line of vk_device.c).
add_executable(rhi_driver_version_test test/rhi_driver_version_test.c rhi_driver_version.c)
target_include_directories(rhi_driver_version_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rhi_driver_version_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                       ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rhi_driver_version COMMAND rhi_driver_version_test)
endif()

# --- The render pass driver row (CPU only, every target) ---------------------
# rhi_pass_row.c: the drivers whose render passes are VkRenderPass objects
# although they offer dynamic rendering (Intel's Windows driver at Vulkan
# 1.3.215 or older), and the [dev] legacy_passes override's words
# (vk_device.c vkr_pass_row_apply).
add_executable(rhi_vk_pass_row_test test/rhi_vk_pass_row_test.c rhi_pass_row.c)
target_include_directories(rhi_vk_pass_row_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rhi_vk_pass_row_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rhi_vk_pass_row COMMAND rhi_vk_pass_row_test)
endif()

# --- The tile-based phones' rule (CPU only, every target) --------------------
# rhi_tile_rule.c: the barrier scopes and the submission wait a Vulkan start
# runs ([dev] barrier_scope, [dev] polled_wait, a tile-based GPU, the lost
# starts in a row before it; issues 179 and 211), and their lines' words.
add_executable(rhi_vk_tile_rule_test test/rhi_vk_tile_rule_test.c rhi_tile_rule.c)
target_include_directories(rhi_vk_tile_rule_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rhi_vk_tile_rule_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rhi_vk_tile_rule COMMAND rhi_vk_tile_rule_test)
endif()

# --- The SPIR-V scan (CPU only, every target) --------------------------------
# vk/vk_spirv_scan.c: whether a module is a fragment shader that declares a
# storage buffer (the buffer barriers' first scopes, vk_cmd.c), over the RHI
# tests' modules and hand-built ones.
add_executable(rhi_vk_spirv_scan_test test/rhi_vk_spirv_scan_test.c vk/vk_spirv_scan.c)
target_include_directories(rhi_vk_spirv_scan_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/test/shaders")
target_compile_options(rhi_vk_spirv_scan_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rhi_vk_spirv_scan COMMAND rhi_vk_spirv_scan_test)
endif()

# --- The render pass breadcrumb (CPU only, every target) ---------------------
# rhi_pass_crumb.c: the pass a backend last began, stored before the driver's
# begin call (vk_renderpass.c vkr_pass_begin) and printed by the crash report
# (port/platform/diag_host.c report_crash_block).  A library of its own: the
# Vulkan backend writes it, the platform layer, which links no backend, reads
# it.
add_library(ico_pass_crumb STATIC rhi_pass_crumb.c)
target_include_directories(ico_pass_crumb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_pass_crumb PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_platform PUBLIC ico_pass_crumb)
add_executable(rhi_vk_pass_crumb_test test/rhi_vk_pass_crumb_test.c)
target_compile_options(rhi_vk_pass_crumb_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(rhi_vk_pass_crumb_test PRIVATE ico_pass_crumb)
if(ICO_RUN_TESTS)
    add_test(NAME rhi_vk_pass_crumb COMMAND rhi_vk_pass_crumb_test)
endif()

# --- The [video] adapter rules (CPU only, every target) ---------------------
# rhi_adapter.c: the exact-name match the Vulkan and Direct3D backends force
# a pick with, and the device-list line's text. Each of those backends links
# its own copy (a static library member, taken once).
add_executable(rhi_adapter_match_test test/rhi_adapter_match_test.c rhi_adapter.c)
target_include_directories(rhi_adapter_match_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rhi_adapter_match_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                       ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME rhi_adapter_match COMMAND rhi_adapter_match_test)
endif()

# --- SDL3 ---------------------------------------------------------------------
# One prefix per target: the source builds for Linux x86-64 and arm64, SDL's
# mingw development release for the two Windows targets, the NDK build for
# Android arm64. SDL3_DIR is set directly
# so the cross toolchains' CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY does not
# hide the user-local prefix.
set(_ico_sdl3_prefix "")
if(ICO_RHI_MOLTENVK)
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/${_ico_mvk_platform}-arm64")
elseif(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/mingw/x86_64-w64-mingw32")
elseif(ANDROID AND CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    # tools/fetch_android.sh's NDK build
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/android-arm64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|AARCH64|arm64)$")
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/linux-arm64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_ico_sdl3_prefix "${ICO_DEPS_DIR}/sdl3/linux-x64")
endif()
if(NOT SDL3_DIR AND _ico_sdl3_prefix AND EXISTS "${_ico_sdl3_prefix}/lib/cmake/SDL3/SDL3Config.cmake")
    set(SDL3_DIR "${_ico_sdl3_prefix}/lib/cmake/SDL3" CACHE PATH "SDL3 CMake package directory")
endif()
if(ICO_RHI_MOLTENVK)
    find_package(SDL3 CONFIG REQUIRED GLOBAL COMPONENTS SDL3-static)
else()
    find_package(SDL3 CONFIG QUIET GLOBAL COMPONENTS SDL3-shared)
endif()
if(SDL3_FOUND)
    message(STATUS "ico: SDL3 ${SDL3_VERSION} from ${SDL3_DIR}")
else()
    message(STATUS "ico: SDL3 not found for this target (run tools/fetch_deps.sh); the RHI "
                   "backends are built headless-only")
endif()

# --- Vulkan headers and volk --------------------------------------------------
# Plain paths, not find_path: the cross toolchains re-root find_* searches
# into the mingw sysroot.
set(_ico_have_vk OFF)
if(ICO_RHI_VULKAN)
    if(ICO_RHI_MOLTENVK)
        set(ICO_VULKAN_INCLUDE_DIR "${ICO_MOLTENVK_ROOT}/MoltenVK/include")
    else()
        set(ICO_VULKAN_INCLUDE_DIR "${ICO_DEPS_DIR}/vulkan-headers/include" CACHE PATH
            "Vulkan-Headers include directory")
    endif()
    set(ICO_VOLK_DIR "${ICO_DEPS_DIR}/volk" CACHE PATH "volk source directory")
    if(NOT EXISTS "${ICO_VULKAN_INCLUDE_DIR}/vulkan/vulkan_core.h")
        set(ICO_VULKAN_INCLUDE_DIR "")
    endif()
    if(NOT EXISTS "${ICO_VOLK_DIR}/volk.c")
        set(ICO_VOLK_DIR "")
    endif()
    if(ICO_VULKAN_INCLUDE_DIR AND (ICO_VOLK_DIR OR ICO_RHI_MOLTENVK))
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

# --- Direct3D 11 headers ------------------------------------------------------
if(ICO_RHI_D3D11)
    if(NOT WIN32)
        message(STATUS "ico: D3D11 RHI is Windows-only; not built for ${CMAKE_SYSTEM_NAME}")
    else()
        include(CheckIncludeFile)
        check_include_file(d3d11.h ICO_HAVE_D3D11_H)
        check_include_file(d3d11_1.h ICO_HAVE_D3D11_1_H)
        check_include_file(dxgi1_6.h ICO_HAVE_DXGI1_6_H)
        check_include_file(d3dcompiler.h ICO_HAVE_D3DCOMPILER_H)
        if(ICO_HAVE_D3D11_H AND ICO_HAVE_D3D11_1_H AND ICO_HAVE_DXGI1_6_H AND
           ICO_HAVE_D3DCOMPILER_H)
            set(_ico_have_d3d11 ON)
        else()
            message(STATUS "ico: D3D11 RHI not built: the toolchain lacks d3d11.h, d3d11_1.h, "
                           "dxgi1_6.h or d3dcompiler.h")
        endif()
    endif()
endif()

# --- OpenGL: SDL3 makes the context and gives the entry points ---------------
if(ICO_RHI_OPENGL)
    if(SDL3_FOUND AND NOT ICO_RHI_MOLTENVK)
        set(_ico_have_gl ON)
    else()
        message(STATUS "ico: OpenGL RHI not built: it needs SDL3 for this target (run "
                       "tools/fetch_deps.sh)")
    endif()
endif()

if(NOT _ico_have_vk AND NOT _ico_have_d3d12 AND NOT _ico_have_gl AND NOT _ico_have_d3d11)
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
    set(_ico_vk_sources rhi_adapter.c rhi_bind_cache.c rhi_driver_version.c rhi_pass_row.c rhi_tile_rule.c vk/vk_cmd.c vk/vk_device.c vk/vk_memory.c vk/vk_native.c
                        vk/vk_pipeline.c vk/vk_present_mode.c vk/vk_renderpass.c vk/vk_resource.c
                        vk/vk_spirv_scan.c vk/vk_swapchain.c vk/vk_sync.c vk/vk_tsclock.c vk/vk_range.c)
    if(ANDROID)
        # the window's surface through the loader in use (a
        # driver the program loaded, or the system's)
        list(APPEND _ico_vk_sources vk/vk_surface_android.c)
    endif()
    if(NOT ICO_RHI_MOLTENVK)
        set(_ico_vk_loader_sources "${ICO_VOLK_DIR}/volk.c")
    endif()
    add_library(ico_rhi_vulkan STATIC ${_ico_vk_sources} ${_ico_vk_loader_sources})
    target_include_directories(ico_rhi_vulkan
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
        PRIVATE "${ICO_VOLK_DIR}" "${ICO_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(ico_rhi_vulkan PRIVATE RHI_BACKEND_PREFIX=vk
                               ${_ico_rhi_debug_default})
    if(ICO_RHI_MOLTENVK)
        target_compile_definitions(ico_rhi_vulkan PRIVATE ICO_RHI_MOLTENVK=1)
        target_link_libraries(ico_rhi_vulkan PUBLIC ico_moltenvk)
    else()
        target_compile_definitions(ico_rhi_vulkan PRIVATE VK_NO_PROTOTYPES)
    endif()
    target_compile_options(ico_rhi_vulkan PRIVATE ${ICO_SEMANTIC_OPTIONS})
    set_source_files_properties(${_ico_vk_sources} PROPERTIES COMPILE_OPTIONS
                                "${ICO_PORT_WARNINGS}")
    target_link_libraries(ico_rhi_vulkan PRIVATE ${CMAKE_DL_LIBS})
    # the crash report's breadcrumb (vk_renderpass.c writes it)
    target_link_libraries(ico_rhi_vulkan PUBLIC ico_pass_crumb)
    if(SDL3_FOUND)
        target_compile_definitions(ico_rhi_vulkan PRIVATE ICO_RHI_HAVE_SDL=1)
        target_link_libraries(ico_rhi_vulkan PUBLIC SDL3::SDL3)
    endif()
endif()

# --- ico_rhi_d3d12 --------------------------------------------------------------
if(_ico_have_d3d12)
    add_library(ico_rhi_d3d12 STATIC
        rhi_adapter.c
        rhi_bind_cache.c
        d3d12/d3d12_cmd.c
        d3d12/d3d12_device.c
        d3d12/d3d12_native.c
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

# --- ico_rhi_d3d11 --------------------------------------------------------------
# The device, the resources, the swapchain, the pipelines (d3d11_pipeline.c)
# and the command path (d3d11_cmd.c); d3d12_plan.c brings the handle pools,
# the bind group layouts and the DXBC input signature reader.
if(_ico_have_d3d11)
    add_library(ico_rhi_d3d11 STATIC
        rhi_adapter.c
        rhi_bind_cache.c
        d3d11/d3d11_cmd.c
        d3d11/d3d11_device.c
        d3d11/d3d11_pipeline.c
        d3d11/d3d11_resource.c
        d3d11/d3d11_shader.c
        d3d11/d3d11_swapchain.c
        d3d12/d3d12_plan.c)
    target_include_directories(ico_rhi_d3d11 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(ico_rhi_d3d11 PRIVATE RHI_BACKEND_PREFIX=d3d11
                               ${_ico_rhi_debug_default})
    target_compile_options(ico_rhi_d3d11 PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    # the IIDs (IID_ID3D11Device1, ...); d3d11.dll, dxgi.dll and
    # d3dcompiler_47.dll are loaded at run time (d3d11_device.c,
    # d3d11_shader.c)
    target_link_libraries(ico_rhi_d3d11 PRIVATE dxguid)
    if(SDL3_FOUND)
        target_compile_definitions(ico_rhi_d3d11 PRIVATE ICO_RHI_HAVE_SDL=1)
        target_link_libraries(ico_rhi_d3d11 PUBLIC SDL3::SDL3)
    endif()
    message(STATUS "ico: D3D11 RHI built (ico_rhi_d3d11)")
endif()

# --- ico_rhi_opengl -------------------------------------------------------------
# gl/gl_glsl.c inflates the shader table's GLSL with miniz (ico_miniz,
# port/data); d3d12/d3d12_plan.c gives the handle pools and the layout rules.
if(_ico_have_gl)
    set(_ico_gl_sources
        rhi_bind_cache.c
        gl/gl_cmd.c
        gl/gl_compile.c
        gl/gl_device.c
        gl/gl_glsl.c
        gl/gl_loader.c
        gl/gl_pipeline.c
        gl/gl_resource.c
        gl/gl_start_screen.c
        gl/gl_swapchain.c
        gl/gl_sync_wait.c
        d3d12/d3d12_plan.c)
    add_library(ico_rhi_opengl STATIC ${_ico_gl_sources})
    target_include_directories(ico_rhi_opengl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(ico_rhi_opengl PRIVATE RHI_BACKEND_PREFIX=gl ICO_RHI_HAVE_SDL=1
                               ${_ico_rhi_debug_default})
    target_compile_options(ico_rhi_opengl PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(ico_rhi_opengl PUBLIC SDL3::SDL3 PRIVATE ico_miniz)
    if(NOT WIN32)
        # d3d12_plan.c's floorf: lld does not take libm from SDL3's link line
        target_link_libraries(ico_rhi_opengl PUBLIC m)
    endif()
    message(STATUS "ico: OpenGL RHI built (ico_rhi_opengl)")
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
if(_ico_have_gl)
    target_compile_definitions(ico_rhi PRIVATE ICO_RHI_HAVE_GL=1)
    target_link_libraries(ico_rhi PUBLIC ico_rhi_opengl)
endif()
if(_ico_have_d3d11)
    target_compile_definitions(ico_rhi PRIVATE ICO_RHI_HAVE_D3D11=1)
    target_link_libraries(ico_rhi PUBLIC ico_rhi_d3d11)
endif()

# port/render and port/shaders/test link ico_rhi_vk and test for it: keep
# the name, present when the Vulkan backend is (the renderer's tests need it).
if(_ico_have_vk)
    add_library(ico_rhi_vk INTERFACE)
    target_link_libraries(ico_rhi_vk INTERFACE ico_rhi)
endif()

# --- Tests ----------------------------------------------------------------------
# rhi_test_common.c: the exact-texel cells every backend must pass.
set(_ico_rhi_test_common test/rhi_test_common.c test/rhi_test_native.c test/rhi_test_stores.c
    test/rhi_test_compute.c)

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
    if(ICO_RHI_MOLTENVK)
        target_compile_definitions(rhi_vk_present_mode_test PRIVATE ICO_RHI_MOLTENVK=1)
    else()
        target_compile_definitions(rhi_vk_present_mode_test PRIVATE VK_NO_PROTOTYPES)
    endif()
    target_compile_options(rhi_vk_present_mode_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                           ${ICO_PORT_WARNINGS})

    # The memory types each allocation kind tries (vk/vk_memory.c) over
    # hand-built heap tables: the order, the small BAR window rule, the
    # fallback past a full type; no device.
    add_executable(rhi_vk_memory_test test/rhi_vk_memory_test.c vk/vk_memory.c)
    target_include_directories(rhi_vk_memory_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                               "${ICO_VOLK_DIR}" "${ICO_VULKAN_INCLUDE_DIR}")
    if(ICO_RHI_MOLTENVK)
        target_compile_definitions(rhi_vk_memory_test PRIVATE ICO_RHI_MOLTENVK=1)
    else()
        target_compile_definitions(rhi_vk_memory_test PRIVATE VK_NO_PROTOTYPES)
    endif()
    target_compile_options(rhi_vk_memory_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

    # Headless rendering on whatever device is present (lavapipe in the
    # container); exits 77 (skipped) when there is no Vulkan device.
    add_executable(rhi_vk_test test/rhi_vk_test.c test/rhi_vk_scope.c test/rhi_vk_subpass.c
                               ${_ico_rhi_test_common})
    # the thread submit cell's second thread (rhi_test_common.c threadSubmitCell)
    target_link_libraries(rhi_vk_test PRIVATE ico_thread)
    target_include_directories(rhi_vk_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test/shaders")
    target_compile_options(rhi_vk_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(rhi_vk_test PRIVATE ico_rhi)

    # rhi_native.h (the handles external compute needs): textures granted
    # storage, the external begin/end bracket and the picture around it,
    # through the Vulkan backend on lavapipe; exits 77 without a device.
    add_executable(rhi_native_test test/rhi_native_test.c)
    target_include_directories(rhi_native_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test/shaders"
                               "${ICO_VOLK_DIR}" "${ICO_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(rhi_native_test PRIVATE VK_NO_PROTOTYPES)
    target_compile_options(rhi_native_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(rhi_native_test PRIVATE ico_rhi)

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
        ico_copy_sdl3_dll(rhi_vk_test rhi_native_test rhi_vk_swapchain_test)
    endif()

    if(ICO_RUN_TESTS)
        add_test(NAME rhi_vk_enum COMMAND rhi_vk_enum_test)
        add_test(NAME rhi_vk_present_mode COMMAND rhi_vk_present_mode_test)
        add_test(NAME rhi_vk_memory COMMAND rhi_vk_memory_test)
        add_test(NAME rhi_vk COMMAND rhi_vk_test)
        set_tests_properties(rhi_vk PROPERTIES SKIP_RETURN_CODE 77)
        # The validation layer tools/fetch_deps.sh unpacks, with
        # synchronisation validation on and its shader access analysis
        # (SYNCVAL_SHADER_ACCESSES_HEURISTIC: without it the checks take no
        # shader's buffer read into account, rhi_vk_scopes* below); the test
        # fails on any validation error.
        set(_ico_vvl "${ICO_DEPS_DIR}/vulkan-validation")
        if(NOT WIN32 AND EXISTS "${_ico_vvl}/VkLayer_khronos_validation.json")
            set(_ico_vk_test_env
                "VK_ADD_LAYER_PATH=${_ico_vvl};VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true"
                "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true")
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
        # the vertex shaders' stores alone (rhi_test_stores.c; the VU record's
        # write, port/render/rd_vu_record.h), under synchronisation validation:
        # a point draw stores through a pipeline without rasterization in a
        # pass without attachments, after and before copies of the buffer,
        # read back word for word; _vk11 through VkRenderPass objects (an
        # attachment-less legacy pass, ICO_VK_FAKE_API=1.1), _global with a
        # global barrier before every pass and copy (ICO_VK_GLOBAL_BARRIERS=1:
        # the bracket's barriers recorded at once); rhi_vk_nostores: a device
        # made without vertexPipelineStoresAndAtomics
        # (ICO_VK_FAKE_NO_VERTEX_STORES=1) reports none and the bracket refuses
        add_test(NAME rhi_vk_stores COMMAND rhi_vk_test stores)
        add_test(NAME rhi_vk_stores_vk11 COMMAND rhi_vk_test stores)
        add_test(NAME rhi_vk_stores_global COMMAND rhi_vk_test stores)
        add_test(NAME rhi_vk_nostores COMMAND rhi_vk_test nostores)
        set_tests_properties(rhi_vk_stores PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_test_env}")
        set(_ico_vk_stores11_env ${_ico_vk_test_env} "ICO_VK_FAKE_API=1.1")
        set_tests_properties(rhi_vk_stores_vk11 PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_stores11_env}")
        set(_ico_vk_storesg_env ${_ico_vk_test_env} "ICO_VK_GLOBAL_BARRIERS=1")
        set_tests_properties(rhi_vk_stores_global PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_storesg_env}")
        set(_ico_vk_nostores_env ${_ico_vk_test_env} "ICO_VK_FAKE_NO_VERTEX_STORES=1")
        set_tests_properties(rhi_vk_nostores PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_nostores_env}")
        # the compute shaders' stores (rhi_test_compute.c; the VU record's
        # write where the vertex shaders cannot store), under synchronisation
        # validation: one group stores after and before copies of the
        # buffer, read back word for word, and (with vertex stores too) the
        # bind-point rule: a store draw after a dispatch binds its own group
        # again; _vk11 on a Vulkan 1.1 device (ICO_VK_FAKE_API=1.1: legacy
        # passes around the store draws), _global with every barrier
        # recorded at once (ICO_VK_GLOBAL_BARRIERS=1), _novs on a device
        # without vertexPipelineStoresAndAtomics (the Mali-G52 of issue 179:
        # the dispatches alone); rhi_vk_nocompute: a device under
        # ICO_VK_FAKE_NO_COMPUTE_STORES=1 reports none, the bracket refuses
        # and a dispatch records nothing
        add_test(NAME rhi_vk_compute COMMAND rhi_vk_test compute)
        add_test(NAME rhi_vk_compute_vk11 COMMAND rhi_vk_test compute)
        add_test(NAME rhi_vk_compute_global COMMAND rhi_vk_test compute)
        add_test(NAME rhi_vk_compute_novs COMMAND rhi_vk_test compute)
        add_test(NAME rhi_vk_nocompute COMMAND rhi_vk_test nocompute)
        set_tests_properties(rhi_vk_compute PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_test_env}")
        set_tests_properties(rhi_vk_compute_vk11 PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_stores11_env}")
        set_tests_properties(rhi_vk_compute_global PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_storesg_env}")
        set_tests_properties(rhi_vk_compute_novs PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_nostores_env}")
        set(_ico_vk_nocompute_env ${_ico_vk_test_env} "ICO_VK_FAKE_NO_COMPUTE_STORES=1")
        set_tests_properties(rhi_vk_nocompute PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_nocompute_env}")
        # the buffer barriers' first scopes (rhi_vk_scope.c; issue 179), under
        # synchronisation validation: a picture's uploads, VU record and pass
        # after the picture before's hold no fragment work
        # (RhiStats.fragmentHolds 0), a texture upload over a sampled texture
        # one, and after a fragment shader that declares a storage buffer the
        # copy and the bracket wait for fragment work; _vk11 on VkRenderPass
        # objects and the fence ring (ICO_VK_FAKE_API=1.1), _global with every
        # barrier recorded at once, _novs without vertex stores (the Mali-G52
        # of issue 179: the compute dispatches alone).  The layer's shader
        # access analysis on (SYNCVAL_SHADER_ACCESSES_HEURISTIC): without it
        # the synchronisation checks take no shader's buffer read into
        # account (layer 1.4.309: a copy over a buffer a fragment shader
        # read, with no barrier at all, passes), so they could not see the
        # write after read a fragment stage left out of a copy's first scope
        # would be; with it they report one (checked with the fragment
        # stage taken out)
        set(_ico_vk_scopes_sv "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true")
        set(_ico_vk_scopes_env ${_ico_vk_test_env} ${_ico_vk_scopes_sv})
        set(_ico_vk_scopes11_env ${_ico_vk_stores11_env} ${_ico_vk_scopes_sv})
        set(_ico_vk_scopesg_env ${_ico_vk_storesg_env} ${_ico_vk_scopes_sv})
        set(_ico_vk_scopesn_env ${_ico_vk_nostores_env} ${_ico_vk_scopes_sv})
        add_test(NAME rhi_vk_scopes COMMAND rhi_vk_test scopes)
        add_test(NAME rhi_vk_scopes_vk11 COMMAND rhi_vk_test scopes)
        add_test(NAME rhi_vk_scopes_global COMMAND rhi_vk_test scopes)
        add_test(NAME rhi_vk_scopes_novs COMMAND rhi_vk_test scopes)
        set_tests_properties(rhi_vk_scopes PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_scopes_env};ICO_TEST_SCOPE_WANT=narrow")
        set_tests_properties(rhi_vk_scopes_vk11 PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_scopes11_env}")
        get_test_property(rhi_vk_scopes_vk11 LABELS _ico_scopes11_labels)
        set_tests_properties(rhi_vk_scopes_vk11 PROPERTIES LABELS "${_ico_scopes11_labels};vk11")
        set_tests_properties(rhi_vk_scopes_global PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_scopesg_env}")
        set_tests_properties(rhi_vk_scopes_novs PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_scopesn_env}")
        # the wide scopes (port/rhi/rhi_tile_rule.h; issues 179 and 211:
        # v0.11.2's, on every tile-based GPU by default since v0.11.4):
        # _arm on Arm's vendor id faked (ICO_VK_FAKE_VENDOR=0x13B5, the rule's
        # alone), _wide asked for ([dev] barrier_scope = wide), _arm_narrow
        # Arm faked with the narrow ones asked for (the setting rules); each
        # under the same synchronisation checks, ICO_TEST_SCOPE_WANT naming
        # the scopes the device must run (rhi_vk_scopes: narrow, lavapipe is
        # not tile-based).  rhi_vk_clock_arm: the recycled slot's clock alone
        # on Arm faked: the single wait, no trace, no calibrated pair
        # (ICO_TEST_POLLED_WANT=0)
        set(_ico_vk_arm "ICO_VK_FAKE_VENDOR=0x13B5")
        set(_ico_vk_scopesarm_env ${_ico_vk_scopes_env} ${_ico_vk_arm} "ICO_TEST_SCOPE_WANT=wide")
        set(_ico_vk_scopesw_env ${_ico_vk_scopes_env} "ICO_VK_BARRIER_SCOPE=wide"
                                "ICO_TEST_SCOPE_WANT=wide")
        set(_ico_vk_scopesan_env ${_ico_vk_scopes_env} ${_ico_vk_arm} "ICO_VK_BARRIER_SCOPE=narrow"
                                 "ICO_TEST_SCOPE_WANT=narrow")
        set(_ico_vk_clockarm_env ${_ico_vk_test_env} ${_ico_vk_arm} "ICO_TEST_POLLED_WANT=0")
        add_test(NAME rhi_vk_scopes_arm COMMAND rhi_vk_test scopes)
        add_test(NAME rhi_vk_scopes_wide COMMAND rhi_vk_test scopes)
        add_test(NAME rhi_vk_scopes_arm_narrow COMMAND rhi_vk_test scopes)
        add_test(NAME rhi_vk_clock_arm COMMAND rhi_vk_test clock)
        set_tests_properties(rhi_vk_scopes_arm PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_scopesarm_env}")
        set_tests_properties(rhi_vk_scopes_wide PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_scopesw_env}")
        set_tests_properties(rhi_vk_scopes_arm_narrow PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_scopesan_env}")
        set_tests_properties(rhi_vk_clock_arm PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_clockarm_env}")
        # a pass's own colour read in it with a depth target bound
        # (rhi_vk_subpass.c, DATE's stencil setup's shape) under
        # synchronisation validation with the shader access analysis on:
        # rhi_vk_subpass through dynamic rendering's local read; _vk11, _vk10
        # (with all-static pipelines) and _mali_vk11 (the Mali-G52's 1.1
        # limits) through the VkRenderPass path's subpass read, an
        # experiment off by default, turned on by ICO_VK_SUBPASS_READ=1 ([dev]
        # subpass_read = on: the colour also input attachment 0, a by-region
        # self-dependency, GENERAL); _vk11_off the 1.1 profile as shipped (the
        # read off, no local read: the cell not run); _legacy on VkRenderPass
        # objects of a 1.4 device as the Intel row gives them (off, the cell
        # not run), and with ICO_VK_SUBPASS_READ=1 (_legacy_on) the read on
        # them.  ICO_TEST_SUBPASS_WANT: the cell fails unless the read is on
        # (1) or off (0) as named
        set(_ico_vk_subpass_on "ICO_VK_SUBPASS_READ=1" "ICO_TEST_SUBPASS_WANT=1")
        set(_ico_vk_subpass11_env ${_ico_vk_scopes11_env} ${_ico_vk_subpass_on})
        set(_ico_vk_subpass11o_env ${_ico_vk_scopes11_env} "ICO_TEST_SUBPASS_WANT=0")
        set(_ico_vk_subpass10_env ${_ico_vk_scopes_env} "ICO_VK_FAKE_API=1.0"
                                  "ICO_VK_FAKE_NO_DYNAMIC=1" ${_ico_vk_subpass_on})
        set(_ico_vk_subpassm_env ${_ico_vk_scopes11_env} "ICO_VK_FAKE_LIMITS=mali"
                                 ${_ico_vk_subpass_on})
        set(_ico_vk_subpassl_env ${_ico_vk_scopes_env} "ICO_VK_LEGACY_PASSES=1"
                                 "ICO_TEST_SUBPASS_WANT=0")
        set(_ico_vk_subpasslo_env ${_ico_vk_scopes_env} "ICO_VK_LEGACY_PASSES=1"
                                  ${_ico_vk_subpass_on})
        foreach(_sp IN ITEMS "|${_ico_vk_scopes_env}" "_vk11|${_ico_vk_subpass11_env}"
                             "_vk11_off|${_ico_vk_subpass11o_env}"
                             "_vk10|${_ico_vk_subpass10_env}" "_mali_vk11|${_ico_vk_subpassm_env}"
                             "_legacy|${_ico_vk_subpassl_env}"
                             "_legacy_on|${_ico_vk_subpasslo_env}")
            string(FIND "${_sp}" "|" _spi)
            string(SUBSTRING "${_sp}" 0 ${_spi} _spn)
            math(EXPR _spi "${_spi} + 1")
            string(SUBSTRING "${_sp}" ${_spi} -1 _spe)
            add_test(NAME rhi_vk_subpass${_spn} COMMAND rhi_vk_test subpass)
            set_tests_properties(rhi_vk_subpass${_spn} PROPERTIES SKIP_RETURN_CODE 77
                                 ENVIRONMENT "${_spe}")
        endforeach()
        foreach(_spn _vk11 _vk11_off _mali_vk11)
            get_test_property(rhi_vk_subpass${_spn} LABELS _ico_sp_labels)
            set_tests_properties(rhi_vk_subpass${_spn} PROPERTIES LABELS "${_ico_sp_labels};vk11")
        endforeach()
        get_test_property(rhi_vk_subpass_vk10 LABELS _ico_sp_labels)
        set_tests_properties(rhi_vk_subpass_vk10 PROPERTIES LABELS "${_ico_sp_labels};vk10")
        # rhi_native.h on the same layer setup (a validation message fails it)
        add_test(NAME rhi_native COMMAND rhi_native_test)
        set_tests_properties(rhi_native PROPERTIES SKIP_RETURN_CODE 77)
        if(_ico_vk_test_env)
            set_tests_properties(rhi_native PROPERTIES ENVIRONMENT "${_ico_vk_test_env}")
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
        # the cells under the rule of one compile at a time
        # (ICO_VK_FAKE_SERIAL=1, vk_pipeline.c, issue 188): one compile
        # thread, the lock around module and pipeline creation, a creation on
        # the test's thread beside the background set
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/serial")
        add_test(NAME rhi_vk_serial COMMAND rhi_vk_test
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/serial")
        set(_ico_vk_serial_env ${_ico_vk_test_env} "ICO_VK_FAKE_SERIAL=1")
        set_tests_properties(rhi_vk_serial PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_serial_env}")
        # the cells with the bind cache off (ICO_RHI_BIND_CACHE=0,
        # rhi_bind_cache.c): every bind recorded as given, the same pictures,
        # no skip counted (rhi_test_common.c bindCacheCell, dynamicCell)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/nobindcache")
        add_test(NAME rhi_vk_nobindcache COMMAND rhi_vk_test
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/nobindcache")
        set(_ico_vk_nobindcache_env ${_ico_vk_test_env} "ICO_RHI_BIND_CACHE=0")
        set_tests_properties(rhi_vk_nobindcache PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_nobindcache_env}")
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
        # the cells on a Vulkan 1.1 driver's absences (ICO_VK_FAKE_API=1.1,
        # vk_device.c): the submissions tracked by the fence ring instead of
        # the timeline semaphore (vk_sync.c); the sync cell checks the kind.
        # Label vk11 (ctest -L vk11), with port/render's _vk11 twins
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk11")
        add_test(NAME rhi_vk_vk11 COMMAND rhi_vk_test
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk11")
        set(_ico_vk_vk11_env ${_ico_vk_test_env} "ICO_VK_FAKE_API=1.1")
        set_tests_properties(rhi_vk_vk11 PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_vk11_env}")
        get_test_property(rhi_vk_vk11 LABELS _ico_vk11_labels)
        set_tests_properties(rhi_vk_vk11 PROPERTIES LABELS "${_ico_vk11_labels};vk11")
        # the cells on a Vulkan 1.0 driver (ICO_VK_FAKE_API=1.0, issue 158):
        # a 1.0 instance with VK_KHR_get_physical_device_properties2, the
        # device at 1.0 with VK_KHR_maintenance1/2, the core 1.1 entry points
        # cleared, the fence ring and VkRenderPass objects as under =1.1, every
        # pipeline state static (as on Android).  Label vk10 (ctest -L vk10),
        # with port/render's _vk10 twins
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk10")
        add_test(NAME rhi_vk_vk10 COMMAND rhi_vk_test
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk10")
        set(_ico_vk_vk10_env ${_ico_vk_test_env} "ICO_VK_FAKE_API=1.0" "ICO_VK_FAKE_NO_DYNAMIC=1")
        set_tests_properties(rhi_vk_vk10 PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_vk10_env}")
        get_test_property(rhi_vk_vk10 LABELS _ico_vk10_labels)
        set_tests_properties(rhi_vk_vk10 PROPERTIES LABELS "${_ico_vk10_labels};vk10")
        # the 1.0 cells with the D32F attachment bit hidden
        # (ICO_VK_FAKE_NO_D32F_ATTACHMENT, an Adreno 505 on a 1.0.61
        # driver): the device is picked, RhiLimits.depth32Attachment is false
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk10nod32f")
        add_test(NAME rhi_vk_vk10_nod32f COMMAND rhi_vk_test
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk10nod32f")
        set_tests_properties(rhi_vk_vk10_nod32f PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_vk10_env};ICO_VK_FAKE_NO_D32F_ATTACHMENT=1")
        get_test_property(rhi_vk_vk10_nod32f LABELS _ico_vk10_labels)
        set_tests_properties(rhi_vk_vk10_nod32f PROPERTIES LABELS "${_ico_vk10_labels};vk10")
        # the same on a Vulkan 1.0 loader without
        # VK_KHR_get_physical_device_properties2 (ICO_VK_FAKE_API=1.0bare,
        # review finding 5): the instance goes on without it and the device
        # is made without the extended queries, every cell as under =1.0
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk10bare")
        add_test(NAME rhi_vk_vk10bare COMMAND rhi_vk_test
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk10bare")
        set(_ico_vk_vk10bare_env ${_ico_vk_test_env} "ICO_VK_FAKE_API=1.0bare"
            "ICO_VK_FAKE_NO_DYNAMIC=1")
        set_tests_properties(rhi_vk_vk10bare PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_vk_vk10bare_env}")
        get_test_property(rhi_vk_vk10bare LABELS _ico_vk10bare_labels)
        set_tests_properties(rhi_vk_vk10bare PROPERTIES LABELS "${_ico_vk10bare_labels};vk10")
        if(TARGET rhi_vk_swapchain_test)
            add_test(NAME rhi_vk_swapchain COMMAND rhi_vk_swapchain_test)
            set_tests_properties(rhi_vk_swapchain PROPERTIES SKIP_RETURN_CODE 77)
            if(_ico_vk_test_env)
                set_tests_properties(rhi_vk_swapchain PROPERTIES ENVIRONMENT
                                     "${_ico_vk_test_env}")
            endif()
            # the window path (acquire, the present batch, recreation) on
            # the fence ring
            add_test(NAME rhi_vk_swapchain_vk11 COMMAND rhi_vk_swapchain_test
                     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk11")
            set_tests_properties(rhi_vk_swapchain_vk11 PROPERTIES SKIP_RETURN_CODE 77
                                 ENVIRONMENT "${_ico_vk_vk11_env}")
            get_test_property(rhi_vk_swapchain_vk11 LABELS _ico_vk11_labels)
            set_tests_properties(rhi_vk_swapchain_vk11 PROPERTIES LABELS
                                 "${_ico_vk11_labels};vk11")
            # and on the Vulkan 1.0 profile
            add_test(NAME rhi_vk_swapchain_vk10 COMMAND rhi_vk_swapchain_test
                     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vk10")
            set_tests_properties(rhi_vk_swapchain_vk10 PROPERTIES SKIP_RETURN_CODE 77
                                 ENVIRONMENT "${_ico_vk_vk10_env}")
            get_test_property(rhi_vk_swapchain_vk10 LABELS _ico_vk10_labels)
            set_tests_properties(rhi_vk_swapchain_vk10 PROPERTIES LABELS
                                 "${_ico_vk10_labels};vk10")
        endif()
    endif()
endif()

# The FidelityFX FSR 3.1 upscaler (option ICO_FFX): the SDK built from the
# fetched source, its shader compiler for the build host and ffx_link_test.
# After the Vulkan tests: the test takes their validation environment.
add_subdirectory(ffx)

# rhi_gl_test: the same cells on OpenGL with the driver's debug output on
# (headless: a hidden window of the backend's own), the copies the cells do
# not make, then the swapchain on an SDL offscreen window; exits 77 (skipped)
# without an OpenGL context. In the container Mesa's llvmpipe
# (LIBGL_ALWAYS_SOFTWARE=1) on SDL's offscreen driver, which makes its
# contexts through EGL without a display.
if(_ico_have_gl)
    add_executable(rhi_gl_test test/rhi_gl_test.c ${_ico_rhi_test_common})
    # the thread submit cell's second thread (rhi_test_common.c threadSubmitCell)
    target_link_libraries(rhi_gl_test PRIVATE ico_thread)
    target_include_directories(rhi_gl_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test/shaders")
    target_compile_options(rhi_gl_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(rhi_gl_test PRIVATE ico_rhi SDL3::SDL3)
    ico_copy_sdl3_dll(rhi_gl_test)
    if(ICO_RUN_TESTS)
        set(_ico_gl_test_env "LIBGL_ALWAYS_SOFTWARE=1" "SDL_VIDEO_DRIVER=offscreen")
        if(ICO_SANITIZE MATCHES "address")
            # the OpenGL driver (Mesa, LLVM) keeps allocations past the
            # context's end, as the Vulkan driver does (rhi_vk above)
            list(APPEND _ico_gl_test_env "ASAN_OPTIONS=detect_leaks=0")
        endif()
        add_test(NAME rhi_gl COMMAND rhi_gl_test)
        set_tests_properties(rhi_gl PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_gl_test_env}")
        # the cells without dual-source blending (cells 0 and 1 keep the clear)
        add_test(NAME rhi_gl_nodual COMMAND rhi_gl_test)
        set(_ico_gl_nodual_env ${_ico_gl_test_env} "ICO_GL_FAKE_NO_DUAL=1")
        set_tests_properties(rhi_gl_nodual PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_gl_nodual_env}")
        # the cells with the bind cache off (ICO_RHI_BIND_CACHE=0, as
        # rhi_vk_nobindcache)
        add_test(NAME rhi_gl_nobindcache COMMAND rhi_gl_test)
        set(_ico_gl_nobindcache_env ${_ico_gl_test_env} "ICO_RHI_BIND_CACHE=0")
        set_tests_properties(rhi_gl_nobindcache PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_gl_nobindcache_env}")
        # rhi_vk_stores' twin (rhi_test_stores.c): SKIP (77) while the OpenGL
        # backend reports no vertex stores, which also checks that its store
        # bracket refuses
        add_test(NAME rhi_gl_stores COMMAND rhi_gl_test stores)
        set_tests_properties(rhi_gl_stores PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_gl_test_env}")
        # and rhi_vk_compute's (rhi_test_compute.c): SKIP (77) while the
        # OpenGL backend reports no compute stores, after checking that its
        # compute bracket refuses and a dispatch counts nothing (1 if not)
        add_test(NAME rhi_gl_compute COMMAND rhi_gl_test compute)
        set_tests_properties(rhi_gl_compute PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
                             "${_ico_gl_test_env}")
    endif()
endif()

# rhi_d3d12_test.exe: the same cells on D3D12, first on WARP, then on the
# hardware adapter, then the swapchain on a hidden window. Double-clicked it
# needs no console and no flags: it writes rhi_d3d12_test.log beside itself
# and shows the verdict in a message box. ctest runs it only on a native
# Windows build (ICO_RUN_TESTS).
if(_ico_have_d3d12)
    add_executable(rhi_d3d12_test test/rhi_d3d12_test.c ${_ico_rhi_test_common})
    # the thread submit cell's second thread (rhi_test_common.c threadSubmitCell)
    target_link_libraries(rhi_d3d12_test PRIVATE ico_thread)
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

# rhi_d3d11_test.exe: the same cells on Direct3D 11 (shaders compiled at run
# time from test/shaders/rhi_test.hlsl), first on WARP, then on the hardware
# adapter, then the swapchain on a hidden window, then Vulkan for comparison.
# Double-clicked it needs no console and no flags: it writes
# rhi_d3d11_test.log beside itself and shows the verdict in a message box.
# The cross builds build it and do not run it; ctest runs it only on a
# native Windows build (ICO_RUN_TESTS).
if(_ico_have_d3d11)
    add_executable(rhi_d3d11_test test/rhi_d3d11_test.c ${_ico_rhi_test_common})
    # the thread submit cell's second thread (rhi_test_common.c threadSubmitCell)
    target_link_libraries(rhi_d3d11_test PRIVATE ico_thread)
    target_include_directories(rhi_d3d11_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test/shaders")
    target_compile_options(rhi_d3d11_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(rhi_d3d11_test PRIVATE ico_rhi user32)
    target_link_options(rhi_d3d11_test PRIVATE -mwindows -static)
    if(SDL3_FOUND)
        target_compile_definitions(rhi_d3d11_test PRIVATE ICO_RHI_HAVE_SDL=1)
        target_link_libraries(rhi_d3d11_test PRIVATE SDL3::SDL3)
        ico_copy_sdl3_dll(rhi_d3d11_test)
    endif()
    if(ICO_RUN_TESTS)
        add_test(NAME rhi_d3d11 COMMAND rhi_d3d11_test --console)
        set_tests_properties(rhi_d3d11 PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
