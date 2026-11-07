# port/rhi/ffx: the AMD FidelityFX SDK's FSR 3.1 upscaler built from source
# (MIT, "Copyright (C) 2024 Advanced Micro Devices, Inc."; v1.1.4 = FSR
# 3.1.4, docs/THIRD_PARTY.md), for the temporal upscaling of issue 99.
#
#   ico_ffx_sc         the SDK's shader compiler driver for the build host
#                      (port/rhi/ffx/sc; ExternalProject when cross-compiling)
#   ico_ffx_upscaler   static library: ffx-api (ffxCreateContext / ffxDispatch
#                      / ffxDestroyContext over ffx_upscale.h), the upscaler
#                      component, the SDK's shared code and its Vulkan backend
#                      with the backend's SPIR-V permutations made at build time
#   ffx_link_test      creates the Vulkan backend and an upscale context on
#                      lavapipe through raw Vulkan calls (77 when it cannot)
#
# Only the upscaler: no frame generation, optical flow or swapchain
# replacement, no FSR 2 provider; the SDK release has no FSR 4. tools/
# fetch_deps.sh installs the source subset (deps/fidelityfx/<tag>/src, with
# tools/ffx/patches/*.patch applied) and glslangValidator
# (deps/fidelityfx/<tag>/glslang/bin), which ico_ffx_sc runs.
#
# The permutation headers: for each of the ten FSR 3.1 passes
# (sdk/src/backends/vk/shaders/fsr3upscaler/*.glsl) ico_ffx_sc compiles the
# 2^6 option permutations upstream lists (CMakeCompileFSR3UpscalerShaders.txt)
# in four variants (wave32/wave64 x 32/16-bit; in GLSL they differ by
# FFX_HALF and the name alone), dedupes identical SPIR-V and writes one header
# per unique module plus <pass>_permutations.h (the key-to-module tables and
# each module's bindings), under the build directory's ffx/vk/. Upstream's
# arguments exactly: -reflection -deps=gcc -DFFX_GPU=1, its fixed options,
# then -compiler=glslang -e CS --target-env vulkan1.2 -S comp -Os -DFFX_GLSL=1.
#
# Linking: the library is C++17 in a C program. The Vulkan backend calls a
# few instance-level entry points by name (vkGetPhysicalDeviceProperties,
# vkEnumerateDeviceExtensionProperties, ...); volk.h is forced into every
# source, so those names are volk's function pointers, loaded by whoever
# links the library (ico_rhi_vulkan's volk.c in the program; the test
# compiles its own). On Linux the C++ runtime is linked statically into the
# consumer (-static-libstdc++ -static-libgcc), because
# tools/package_linux.sh allows no libstdc++.so.6 or libgcc_s.so.1 in the
# shipped program's NEEDED list; not under the sanitizers (test programs
# only, and libasan replaces operator new). Windows (mingw): the program is
# linked -static already (CMakeLists.txt).
#
# The D3D12 backend: not built yet. The pinned SDK makes its DXIL
# permutations with DXC through Windows COM (hlsl_compiler.cpp), which the
# POSIX ico_ffx_sc leaves out; docs/BUILDING.md has the state.
#
# ICO_FFX (CMakePresets.json): ON in linux-x64 and the presets that inherit
# it (asan, linux-x64-asan, fptrap), the build that proved it (docs/
# BUILDING.md). OFF (the option's default) in:
#   win-x64          the pinned mingw-w64 gcc toolchain has no C++ compiler
#                    (Debian's gcc-mingw-w64-x86-64-win32 without g++), so
#                    the SDK cannot be compiled for Windows yet
#   win-x64-clang, linux-x64-clang
#                    llvm-mingw's clang++ compiles the Vulkan sources
#                    (checked with -fsyntax-only) but the presets set no C++
#                    compiler and no build has linked it
#   linux-arm64, android-arm64
#                    the SDK is fetched on x86-64 hosts only; the row is a
#                    desktop feature
#   ios-*, macos-*   MoltenVK is untested with the FFX Vulkan backend

option(ICO_FFX "Build the FidelityFX FSR 3.1 upscaler from the fetched SDK source" OFF)
if(NOT ICO_FFX)
    message(STATUS "ico: FidelityFX upscaler not built (ICO_FFX=OFF)")
    return()
endif()
if(NOT _ico_have_vk)
    message(FATAL_ERROR "ICO_FFX needs the Vulkan backend's headers and volk (ICO_RHI_VULKAN)")
endif()

set(ICO_FFX_TAG "v1.1.4" CACHE STRING "The FidelityFX SDK tag tools/fetch_deps.sh installs")
set(ICO_FFX_DIR "${ICO_DEPS_DIR}/fidelityfx/${ICO_FFX_TAG}" CACHE PATH
    "The FidelityFX SDK install (tools/fetch_deps.sh)")
set(_ffx "${ICO_FFX_DIR}/src")
set(ICO_FFX_GLSLANG "${ICO_FFX_DIR}/glslang/bin/glslangValidator" CACHE FILEPATH
    "glslangValidator for the FidelityFX Vulkan permutations")
if(NOT EXISTS "${_ffx}/ffx-api/include/ffx_api/ffx_upscale.h" OR NOT EXISTS "${ICO_FFX_GLSLANG}")
    message(FATAL_ERROR "ICO_FFX needs the FidelityFX SDK and glslang at ${ICO_FFX_DIR}: run "
                        "tools/fetch_deps.sh (x86-64 hosts), or configure with -DICO_FFX=OFF")
endif()
enable_language(CXX)

# --- ico_ffx_sc: the shader compiler, for the build host ---------------------
if(CMAKE_CROSSCOMPILING)
    include(ExternalProject)
    set(_ico_ffx_sc_dir "${CMAKE_CURRENT_BINARY_DIR}/sc-host")
    set(_ico_ffx_sc_exe "${_ico_ffx_sc_dir}/ico_ffx_sc")
    # the host's own compilers: no toolchain file, a clean environment's cc/c++
    ExternalProject_Add(ico_ffx_sc
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sc"
        BINARY_DIR "${_ico_ffx_sc_dir}"
        CMAKE_ARGS -DICO_FFX_SRC=${_ffx} -DCMAKE_BUILD_TYPE=Release
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${_ico_ffx_sc_exe}"
        BUILD_ALWAYS OFF
        EXCLUDE_FROM_ALL ON)
else()
    set(ICO_FFX_SRC "${_ffx}")
    add_subdirectory(sc EXCLUDE_FROM_ALL)
    set(_ico_ffx_sc_exe "$<TARGET_FILE:ico_ffx_sc>")
endif()

# --- the Vulkan backend's permutation headers -------------------------------
set(_ffx_gpu "${_ffx}/sdk/include/FidelityFX/gpu")
set(_ffx_vk_out "${CMAKE_CURRENT_BINARY_DIR}/vk")
file(MAKE_DIRECTORY "${_ffx_vk_out}")
# CMakeCompileFSR3UpscalerShaders.txt's FSR3UPSCALER_BASE_ARGS and
# FSR3UPSCALER_PERMUTATION_ARGS, then CMakeShadersFSR3Upscaler.txt's Vulkan
# arguments; one compiler thread per run (ninja runs the forty side by side)
set(_ffx_sc_args
    -reflection -deps=gcc -DFFX_GPU=1
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
    -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
    -compiler=glslang -e CS --target-env vulkan1.2 -S comp -Os -DFFX_GLSL=1
    "-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}"
    "-DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT={0,1}"
    "-DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}"
    "-DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS={0,1}"
    "-DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH={0,1}"
    "-DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING={0,1}"
    -num-threads=1 "-glslangexe=${ICO_FFX_GLSLANG}")
file(GLOB _ffx_vk_shaders "${_ffx}/sdk/src/backends/vk/shaders/fsr3upscaler/*.glsl")
list(SORT _ffx_vk_shaders)
set(_ffx_vk_headers "")
foreach(_shader IN LISTS _ffx_vk_shaders)
    get_filename_component(_pass "${_shader}" NAME_WE)
    # name suffix and FFX_HALF of upstream's four variants
    foreach(_variant ":0" "_wave64:0" "_16bit:1" "_wave64_16bit:1")
        string(REPLACE ":" ";" _v "${_variant}")
        list(GET _v 0 _suffix)
        list(GET _v 1 _half)
        set(_out "${_ffx_vk_out}/${_pass}${_suffix}_permutations.h")
        add_custom_command(
            OUTPUT "${_out}"
            COMMAND "${_ico_ffx_sc_exe}" ${_ffx_sc_args} -name=${_pass}${_suffix}
                    -DFFX_HALF=${_half} "-I${_ffx_gpu}" "-I${_ffx_gpu}/fsr3upscaler"
                    "-output=${_ffx_vk_out}" "${_shader}"
            DEPENDS "${_shader}" ico_ffx_sc "${ICO_FFX_GLSLANG}"
            DEPFILE "${_out}.d"
            COMMENT "FidelityFX: ${_pass}${_suffix} SPIR-V permutations"
            VERBATIM)
        list(APPEND _ffx_vk_headers "${_out}")
    endforeach()
endforeach()
add_custom_target(ico_ffx_vk_permutations DEPENDS ${_ffx_vk_headers})

# --- ico_ffx_upscaler --------------------------------------------------------
add_library(ico_ffx_upscaler STATIC
    # ffx-api: the entry points and the FSR 3.1 provider; ico_ffx_provider.cpp
    # stands in for ffx_provider.cpp (that one's table names the frame
    # generation and FSR 2 providers and probes the AMD D3D12 driver)
    "${_ffx}/ffx-api/src/ffx_api.cpp"
    "${_ffx}/ffx-api/src/backends.cpp"
    "${_ffx}/ffx-api/src/validation.cpp"
    "${_ffx}/ffx-api/src/ffx_provider_fsr3upscale.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ico_ffx_provider.cpp"
    # the upscaler component
    "${_ffx}/sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp"
    # the SDK's shared code and the backend's blob table
    "${_ffx}/sdk/src/shared/ffx_assert.cpp"
    "${_ffx}/sdk/src/shared/ffx_message.cpp"
    "${_ffx}/sdk/src/shared/ffx_object_management.cpp"
    "${_ffx}/sdk/src/shared/ffx_breadcrumbs_list.cpp"
    "${_ffx}/sdk/src/backends/shared/ffx_shader_blobs.cpp"
    "${_ffx}/sdk/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp"
    # the Vulkan backend, and ico_ffx_stubs.cpp for the frame-generation
    # swapchain entry its interface table names (not built)
    "${_ffx}/sdk/src/backends/vk/ffx_vk.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ico_ffx_stubs.cpp")
add_dependencies(ico_ffx_upscaler ico_ffx_vk_permutations)
# out of `all`: built for what links it (ffx_link_test in `tests`, the
# program once the FSR 3.1 row's host code does), so a build that links
# neither does not run the shader compiler
set_target_properties(ico_ffx_upscaler PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(ico_ffx_upscaler PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
                                                  CXX_EXTENSIONS OFF)
target_include_directories(ico_ffx_upscaler
    PUBLIC "${_ffx}/ffx-api/include" "${ICO_VULKAN_INCLUDE_DIR}" "${ICO_VOLK_DIR}"
    PRIVATE "${_ffx}/sdk/include" "${_ffx}/sdk/include/FidelityFX/host"
            "${_ffx}/sdk/src/shared" "${_ffx}/sdk/src/backends/shared"
            "${_ffx}/sdk/src/components" "${_ffx}/sdk/src/components/fsr3upscaler"
            "${_ffx}/ffx-api/src"
            "${_ffx_vk_out}")
# FFX_API_STATIC: ffx_api.h's entry points without __declspec(dllexport)
# (tools/ffx/patches/0002); the program and the test include ffx_api.h with
# it too
target_compile_definitions(ico_ffx_upscaler
    PUBLIC FFX_API_STATIC VK_NO_PROTOTYPES
    PRIVATE FFX_BACKEND_VK FFX_FSR3UPSCALER FFX_GCC FFXDLL_VERSION_MAJOR=1
            FFXDLL_VERSION_MINOR=0 FFXDLL_VERSION_PATCH=1 CI_PIPELINE_ID=0)
# ico_ffx_compat.h: the MSVC library names the SDK calls (wcscpy_s, _countof,
# ...) where the C library has none; volk.h first, so vulkan.h's
# prototypes are volk's pointers. Third-party code: -w.
target_compile_options(ico_ffx_upscaler PRIVATE
    "SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/ico_ffx_compat.h\""
    "SHELL:-include volk.h" -w)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID AND NOT ICO_SANITIZE)
    target_link_options(ico_ffx_upscaler INTERFACE -static-libstdc++ -static-libgcc)
endif()

# --- ffx_link_test -----------------------------------------------------------
# The library linked into a C program and run: a Vulkan 1.2 instance and
# device on the first device (lavapipe in the container), the FFX Vulkan
# backend through ffx-api, an upscale context 640x480 -> 1280x960 (Quality),
# its GPU memory queried, destroyed. 77 (skip) with the reason when the
# loader, a Vulkan 1.2 device or a feature the backend needs is missing.
if(NOT WIN32)
    add_executable(ffx_link_test "${CMAKE_CURRENT_SOURCE_DIR}/../test/ffx_link_test.c"
                                 "${ICO_VOLK_DIR}/volk.c")
    target_compile_options(ffx_link_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    set_source_files_properties("${ICO_VOLK_DIR}/volk.c" PROPERTIES COMPILE_OPTIONS "-w")
    target_link_libraries(ffx_link_test PRIVATE ico_ffx_upscaler ${CMAKE_DL_LIBS})
    if(ICO_RUN_TESTS)
        add_test(NAME ffx_link_test COMMAND ffx_link_test)
        set_tests_properties(ffx_link_test PROPERTIES SKIP_RETURN_CODE 77)
        if(_ico_vk_test_env)
            set_tests_properties(ffx_link_test PROPERTIES ENVIRONMENT "${_ico_vk_test_env}")
        endif()
    endif()
endif()
