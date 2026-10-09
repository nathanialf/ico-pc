# cmake/IcoShaders.cmake: the build-time shader toolchain.
#
# One HLSL source per shader family, one DXC invocation per entry and target:
# SPIR-V for Vulkan, signed DXIL for D3D12. The blobs are embedded as C
# arrays in a generated shaders_gen.c/.h (table g_icoShaders). No runtime
# compiler. DXC is a build tool: it runs on the build host whatever the
# target preset is, and nothing links against it.
#
#   ico_add_shader(<name> <file.hlsl> <entry> <stage>)
#       stage: vertex | fragment (vs | ps accepted). <file> is relative to
#       the calling directory. <name> is the lookup key in the table.
#   ico_add_shader(<name> <file.hlsl> <entry> <stage> DEFINES <D=V>... SUFFIX <s>)
#       the same entry compiled with the macros given (DXC -D); the table key
#       is <name><s> (the *_nodual entries of the two-pass blend fallback,
#       ICO_NO_DUAL=1).
#   ico_shaders_library(<target>)
#       after the last ico_add_shader: builds the static library <target>
#       holding shaders_gen.c, with shaders_gen.h on its public include path.
#
# ICO_SHADERS_DXIL=OFF (the android-arm64 preset) compiles SPIR-V only: the
# table's dxil is NULL with dxil_len 0, for a target without the D3D12
# backend.
#
# DXC comes from tools/fetch_deps.sh (tools/toolchain/deps/dxc); ICO_DXC
# overrides the path. Without a compiler the shaders are left out
# (ICO_SHADERS_AVAILABLE is false), as the other fetched dependencies are.

set(ICO_DXC "" CACHE FILEPATH "DXC executable (default: tools/toolchain/deps/dxc/bin/dxc)")
option(ICO_SHADERS_DXIL "Also compile the shaders to DXIL for the D3D12 backend" ON)

set(ICO_SHADERS_AVAILABLE FALSE)
set(_ico_dxc "${ICO_DXC}")
if(NOT _ico_dxc)
    set(_ico_dxc "${ICO_DEPS_DIR}/dxc/bin/dxc")
endif()
if(EXISTS "${_ico_dxc}" AND Python3_EXECUTABLE)
    set(ICO_SHADERS_AVAILABLE TRUE)
endif()
set(ICO_DXC_EXE "${_ico_dxc}")
set(ICO_SHADER_DIR "${CMAKE_SOURCE_DIR}/port/shaders")
set(ICO_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")

# The flags that map HLSL registers to the Vulkan backend's bindings: b0.. at
# 0, t at 16, s at 32, u at 48 (the register number is the RHI slot).
set(ICO_DXC_SPIRV_FLAGS -spirv -fspv-target-env=vulkan1.2
    -fvk-b-shift 0 all -fvk-t-shift 16 all -fvk-s-shift 32 all -fvk-u-shift 48 all)
# -WX: a shader warning fails the build. -Zi is not used: no debug info in the
# embedded blobs.
set(ICO_DXC_COMMON_FLAGS -O3 -WX -I "${ICO_SHADER_DIR}")

set_property(GLOBAL PROPERTY ICO_SHADER_MANIFEST "")
set_property(GLOBAL PROPERTY ICO_SHADER_BLOBS "")

function(ico_add_shader name file entry stage)
    if(NOT ICO_SHADERS_AVAILABLE)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 4 _ico_sh "" "SUFFIX" "DEFINES")
    set(name "${name}${_ico_sh_SUFFIX}")
    set(_ico_sh_d "")
    foreach(_d IN LISTS _ico_sh_DEFINES)
        list(APPEND _ico_sh_d -D "${_d}")
    endforeach()
    string(TOLOWER "${stage}" stage_l)
    if(stage_l STREQUAL "vertex" OR stage_l STREQUAL "vs")
        set(stage_n vertex)
        set(profile vs_6_0)
    elseif(stage_l STREQUAL "fragment" OR stage_l STREQUAL "ps" OR stage_l STREQUAL "pixel")
        set(stage_n fragment)
        set(profile ps_6_0)
    else()
        message(FATAL_ERROR "ico_add_shader(${name}): stage must be vertex or fragment")
    endif()
    get_filename_component(src "${file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    file(GLOB _hlsli CONFIGURE_DEPENDS "${ICO_SHADER_DIR}/*.hlsli")
    set(spv "${ICO_SHADER_OUT_DIR}/${name}.spv")
    if(ICO_SHADERS_DXIL)
        set(dxil "${ICO_SHADER_OUT_DIR}/${name}.dxil")
        add_custom_command(
            OUTPUT "${spv}" "${dxil}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ICO_SHADER_OUT_DIR}"
            COMMAND "${ICO_DXC_EXE}" ${ICO_DXC_SPIRV_FLAGS} ${ICO_DXC_COMMON_FLAGS}
                    ${_ico_sh_d} -T ${profile} -E ${entry} "${src}" -Fo "${spv}"
            COMMAND "${ICO_DXC_EXE}" ${ICO_DXC_COMMON_FLAGS}
                    ${_ico_sh_d} -T ${profile} -E ${entry} "${src}" -Fo "${dxil}"
            DEPENDS "${src}" ${_hlsli} "${ICO_DXC_EXE}"
            COMMENT "DXC ${name} (${entry}, ${profile}): SPIR-V and DXIL"
            VERBATIM)
        set_property(GLOBAL APPEND PROPERTY ICO_SHADER_BLOBS "${spv}" "${dxil}")
    else()
        # "-": no DXIL for this shader (embed_shaders.py)
        set(dxil "-")
        add_custom_command(
            OUTPUT "${spv}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ICO_SHADER_OUT_DIR}"
            COMMAND "${ICO_DXC_EXE}" ${ICO_DXC_SPIRV_FLAGS} ${ICO_DXC_COMMON_FLAGS}
                    ${_ico_sh_d} -T ${profile} -E ${entry} "${src}" -Fo "${spv}"
            DEPENDS "${src}" ${_hlsli} "${ICO_DXC_EXE}"
            COMMENT "DXC ${name} (${entry}, ${profile}): SPIR-V"
            VERBATIM)
        set_property(GLOBAL APPEND PROPERTY ICO_SHADER_BLOBS "${spv}")
    endif()
    set_property(GLOBAL APPEND PROPERTY ICO_SHADER_MANIFEST
                 "${name}|${stage_n}|${entry}|${spv}|${dxil}")
endfunction()

function(ico_shaders_library target)
    if(NOT ICO_SHADERS_AVAILABLE)
        message(STATUS "ico: shaders not built: no DXC at ${ICO_DXC_EXE} (run tools/fetch_deps.sh)")
        return()
    endif()
    get_property(manifest GLOBAL PROPERTY ICO_SHADER_MANIFEST)
    get_property(blobs GLOBAL PROPERTY ICO_SHADER_BLOBS)
    list(JOIN manifest "\n" manifest_text)
    file(WRITE "${ICO_SHADER_OUT_DIR}/manifest.txt.tmp" "${manifest_text}\n")
    file(COPY_FILE "${ICO_SHADER_OUT_DIR}/manifest.txt.tmp" "${ICO_SHADER_OUT_DIR}/manifest.txt"
         ONLY_IF_DIFFERENT)
    file(REMOVE "${ICO_SHADER_OUT_DIR}/manifest.txt.tmp")
    execute_process(COMMAND "${ICO_DXC_EXE}" --version OUTPUT_VARIABLE _ver ERROR_VARIABLE _ver
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "[^\n]*" _ver_line "${_ver}")
    set(ICO_DXC_VERSION "${_ver_line}" CACHE INTERNAL "")
    add_custom_command(
        OUTPUT "${ICO_SHADER_OUT_DIR}/shaders_gen.c" "${ICO_SHADER_OUT_DIR}/shaders_gen.h"
        COMMAND "${Python3_EXECUTABLE}" "${ICO_SHADER_DIR}/embed_shaders.py"
                "${ICO_SHADER_OUT_DIR}/manifest.txt" "${ICO_SHADER_OUT_DIR}" "DXC ${ICO_DXC_VERSION}"
        DEPENDS ${blobs} "${ICO_SHADER_OUT_DIR}/manifest.txt" "${ICO_SHADER_DIR}/embed_shaders.py"
        COMMENT "Embedding shaders into shaders_gen.c"
        VERBATIM)
    add_library(${target} STATIC "${ICO_SHADER_OUT_DIR}/shaders_gen.c")
    target_include_directories(${target} PUBLIC "${ICO_SHADER_OUT_DIR}")
    target_compile_options(${target} PRIVATE ${ICO_SEMANTIC_OPTIONS})
    if(NOT ICO_SHADERS_DXIL)
        # the table's users that check the DXIL (shaders_table_test)
        target_compile_definitions(${target} INTERFACE ICO_SHADERS_NO_DXIL=1)
    endif()
    set(ICO_SHADERS_TARGET ${target} PARENT_SCOPE)
endfunction()
