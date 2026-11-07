# port/rhi/ffx/sc: ico_ffx_sc, the FidelityFX SDK's shader compiler driver
# (FidelityFX_SC, MIT, AMD; sdk/tools/ffx_shader_compiler of the pinned SDK)
# built for the build host. It expands a shader's permutation options, runs
# glslangValidator once per permutation (GLSL to SPIR-V for the Vulkan
# backend), reflects each module's bindings (SPIRV-Reflect) and writes the
# permutation headers the upscaler's blob accessors include
# (port/rhi/ffx/CMakeLists.txt). Upstream builds it with MSVC only; the
# patch tools/ffx/patches/0001-shader-compiler-posix.patch gives it a POSIX
# path (file names, directories, main) and leaves its HLSL compilers
# (DXC/FXC through Windows COM) out off Windows, so here it compiles GLSL
# alone. A build tool: never linked into the program or shipped.
#
# Two ways in: add_subdirectory from port/rhi/ffx in a native build, or a
# project of its own that ExternalProject builds with the host's compiler
# when the port cross-compiles (the win-x64 presets). ICO_FFX_SRC is the
# installed SDK tree (tools/fetch_deps.sh: deps/fidelityfx/<tag>/src).
cmake_minimum_required(VERSION 3.20)
project(ico_ffx_sc LANGUAGES C CXX)

if(NOT ICO_FFX_SRC OR NOT EXISTS "${ICO_FFX_SRC}/sdk/tools/ffx_shader_compiler/src/ffx_sc.cpp")
    message(FATAL_ERROR "ico_ffx_sc needs ICO_FFX_SRC, the FidelityFX SDK tree tools/fetch_deps.sh installs")
endif()
set(_sc "${ICO_FFX_SRC}/sdk/tools/ffx_shader_compiler")

# upstream's file list, its POSIX half: no hlsl_compiler.cpp or
# DXBCChecksum.cpp (Windows COM / DXBC), tiny-process-library's Unix process
add_executable(ico_ffx_sc
    "${_sc}/src/ffx_sc.cpp"
    "${_sc}/src/glsl_compiler.cpp"
    "${_sc}/src/utils.cpp"
    "${_sc}/libs/MD5/md5.cpp"
    "${_sc}/libs/SPIRV-Reflect/spirv_reflect.c"
    "${_sc}/libs/tiny-process-library/process.cpp"
    "${_sc}/libs/tiny-process-library/process_unix.cpp")
set_target_properties(ico_ffx_sc PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
                                            CXX_EXTENSIONS OFF)
target_include_directories(ico_ffx_sc PRIVATE "${_sc}/src" "${_sc}/libs/MD5"
                                              "${_sc}/libs/SPIRV-Reflect"
                                              "${_sc}/libs/tiny-process-library")
# third-party code: its warnings are not ours to fix; no sanitizer (a build
# tool, run by the build)
target_compile_options(ico_ffx_sc PRIVATE -w -fno-sanitize=all)
target_link_options(ico_ffx_sc PRIVATE -fno-sanitize=all)
find_package(Threads REQUIRED)
target_link_libraries(ico_ffx_sc PRIVATE Threads::Threads)
