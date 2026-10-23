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
# The table has three more columns for the other backends (the manifest
# line is name|stage|entry|spv|dxil|glsl|hlslfile|defines):
#   glsl      the GLSL translated from the SPIR-V for the OpenGL backend
#             (ICO_SHADERS_GLSL; deflated by embed_shaders.py, inflated by the
#             backend); "-" for an entry without one (an input-attachment
#             build, which GLSL cannot express), or everywhere with the
#             option off (glsl NULL, glsl_len 0)
#   hlslfile  the HLSL file's name and defines the entry's -D macros
#             ("A=1,B=2", "" for none): what the Direct3D 11 backend compiles
#             at run time from the embedded sources (ICO_SHADERS_HLSL_SOURCE:
#             g_icoShaderSources holds every file of port/shaders; off, the
#             table is empty)
#
# shaders_d3d11_check (ICO_SHADERS_D3D11_CHECK, default ON; a target outside
# `all`, which the CI's linux job builds): every entry compiled again by DXC to
# a discarded DXIL blob with -D ICO_D3D11=1 -HV 2018, the macro the Direct3D 11
# backend's run-time FXC compile defines (reg.hlsli's flat registers) and the
# language version nearest FXC's. It proves the D3D11 preprocessing compiles
# on the build hosts; FXC itself (shader model 5.0) runs only on Windows, at
# run time (docs/port/SHADERS.md, "Direct3D 11: run-time compile").
#
# DXC comes from tools/fetch_deps.sh (tools/toolchain/deps/dxc); ICO_DXC
# overrides the path. Without a compiler the shaders are left out
# (ICO_SHADERS_AVAILABLE is false), as the other fetched dependencies are.

set(ICO_DXC "" CACHE FILEPATH "DXC executable (default: tools/toolchain/deps/dxc/bin/dxc)")
option(ICO_SHADERS_DXIL "Also compile the shaders to DXIL for the D3D12 backend" ON)
option(ICO_SHADERS_GLSL "Also translate the shaders to GLSL for the OpenGL backend" OFF)
option(ICO_SHADERS_HLSL_SOURCE "Embed the HLSL sources for the Direct3D 11 backend's run-time compile"
       ${ICO_RHI_D3D11})
option(ICO_SHADERS_D3D11_CHECK "Add the shaders_d3d11_check target (DXC with ICO_D3D11=1)" ON)

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
set_property(GLOBAL PROPERTY ICO_SHADER_D3D11_CHECKS "")

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
    # the manifest's defines column: "A=1,B=2" (a comma, not the list's
    # semicolon, so the manifest line stays one string)
    list(JOIN _ico_sh_DEFINES "," _ico_sh_defines)
    get_filename_component(_ico_sh_hlslfile "${file}" NAME)
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
    set(spv "${ICO_SHADER_OUT_DIR}/${name}.spv")
    # The headers a shader includes come from DXC itself (-MF: a Makefile
    # dependency file whose target is the -Fo path, the SPIR-V output and the
    # command's first OUTPUT), so a header edit rebuilds only the shaders that
    # include it. Other generators (no DEPFILE for custom commands) depend on
    # every header.
    if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set(_ico_sh_dep "${ICO_SHADER_OUT_DIR}/${name}.d")
        set(_ico_sh_depfile DEPFILE "${_ico_sh_dep}")
        # -MF with -Fo writes only the dependency file (no output blob), so it
        # is a command of its own, after the compile
        set(_ico_sh_depcmd COMMAND "${ICO_DXC_EXE}" ${ICO_DXC_SPIRV_FLAGS} ${ICO_DXC_COMMON_FLAGS}
            ${_ico_sh_d} -T ${profile} -E ${entry} "${src}" -Fo "${spv}" -MF "${_ico_sh_dep}")
        set(_hlsli "")
    else()
        set(_ico_sh_depfile "")
        set(_ico_sh_depcmd "")
        file(GLOB _hlsli CONFIGURE_DEPENDS "${ICO_SHADER_DIR}/*.hlsli")
    endif()
    if(ICO_SHADERS_DXIL)
        set(dxil "${ICO_SHADER_OUT_DIR}/${name}.dxil")
        add_custom_command(
            OUTPUT "${spv}" "${dxil}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ICO_SHADER_OUT_DIR}"
            COMMAND "${ICO_DXC_EXE}" ${ICO_DXC_SPIRV_FLAGS} ${ICO_DXC_COMMON_FLAGS}
                    ${_ico_sh_d} -T ${profile} -E ${entry} "${src}" -Fo "${spv}"
            COMMAND "${ICO_DXC_EXE}" ${ICO_DXC_COMMON_FLAGS}
                    ${_ico_sh_d} -T ${profile} -E ${entry} "${src}" -Fo "${dxil}"
            ${_ico_sh_depcmd}
            DEPENDS "${src}" ${_hlsli} "${ICO_DXC_EXE}"
            ${_ico_sh_depfile}
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
            ${_ico_sh_depcmd}
            DEPENDS "${src}" ${_hlsli} "${ICO_DXC_EXE}"
            ${_ico_sh_depfile}
            COMMENT "DXC ${name} (${entry}, ${profile}): SPIR-V"
            VERBATIM)
        set_property(GLOBAL APPEND PROPERTY ICO_SHADER_BLOBS "${spv}")
    endif()
    # the GLSL column: "-" until the translation step (ICO_SHADERS_GLSL,
    # tools/spirv_to_glsl.py) fills it in
    set(glsl "-")
    set_property(GLOBAL APPEND PROPERTY ICO_SHADER_MANIFEST
                 "${name}|${stage_n}|${entry}|${spv}|${dxil}|${glsl}|${_ico_sh_hlslfile}|${_ico_sh_defines}")
    # shaders_d3d11_check's line for the entry (ico_shaders_library)
    set_property(GLOBAL APPEND PROPERTY ICO_SHADER_D3D11_CHECKS
                 "${name}|${profile}|${entry}|${src}|${_ico_sh_defines}")
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
    # the embedded HLSL sources (ICO_SHADERS_HLSL_SOURCE): every file of
    # port/shaders the entries include, by name
    set(_ico_sh_sources "")
    set(_ico_sh_sources_arg "")
    if(ICO_SHADERS_HLSL_SOURCE)
        file(GLOB _ico_sh_sources CONFIGURE_DEPENDS "${ICO_SHADER_DIR}/*.hlsl"
             "${ICO_SHADER_DIR}/*.hlsli" "${ICO_SHADER_DIR}/*.h")
        list(SORT _ico_sh_sources)
        set(_ico_sh_sources_arg --sources ${_ico_sh_sources})
    endif()
    add_custom_command(
        OUTPUT "${ICO_SHADER_OUT_DIR}/shaders_gen.c" "${ICO_SHADER_OUT_DIR}/shaders_gen.h"
        COMMAND "${Python3_EXECUTABLE}" "${ICO_SHADER_DIR}/embed_shaders.py"
                "${ICO_SHADER_OUT_DIR}/manifest.txt" "${ICO_SHADER_OUT_DIR}" "DXC ${ICO_DXC_VERSION}"
                ${_ico_sh_sources_arg}
        DEPENDS ${blobs} "${ICO_SHADER_OUT_DIR}/manifest.txt" "${ICO_SHADER_DIR}/embed_shaders.py"
                ${_ico_sh_sources}
        COMMENT "Embedding shaders into shaders_gen.c"
        VERBATIM)
    add_library(${target} STATIC "${ICO_SHADER_OUT_DIR}/shaders_gen.c")
    target_include_directories(${target} PUBLIC "${ICO_SHADER_OUT_DIR}")
    target_compile_options(${target} PRIVATE ${ICO_SEMANTIC_OPTIONS})
    if(NOT ICO_SHADERS_DXIL)
        # the table's users that check the DXIL (shaders_table_test)
        target_compile_definitions(${target} INTERFACE ICO_SHADERS_NO_DXIL=1)
    endif()
    if(NOT ICO_SHADERS_GLSL)
        target_compile_definitions(${target} INTERFACE ICO_SHADERS_NO_GLSL=1)
    endif()
    if(NOT ICO_SHADERS_HLSL_SOURCE)
        target_compile_definitions(${target} INTERFACE ICO_SHADERS_NO_HLSL_SOURCE=1)
    endif()
    # shaders_d3d11_check: every entry with ICO_D3D11=1, the blobs discarded
    # (written under shaders/d3d11_check, never embedded)
    if(ICO_SHADERS_D3D11_CHECK)
        get_property(_checks GLOBAL PROPERTY ICO_SHADER_D3D11_CHECKS)
        set(_chk_dir "${ICO_SHADER_OUT_DIR}/d3d11_check")
        set(_chk_cmds "")
        foreach(_c IN LISTS _checks)
            if(NOT _c MATCHES "^([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|(.*)$")
                message(FATAL_ERROR "shaders_d3d11_check: malformed line ${_c}")
            endif()
            set(_n "${CMAKE_MATCH_1}")
            set(_p "${CMAKE_MATCH_2}")
            set(_e "${CMAKE_MATCH_3}")
            set(_s "${CMAKE_MATCH_4}")
            set(_defs "${CMAKE_MATCH_5}")
            set(_d "")
            if(_defs)
                string(REPLACE "," ";" _defs "${_defs}")
                foreach(_x IN LISTS _defs)
                    list(APPEND _d -D "${_x}")
                endforeach()
            endif()
            list(APPEND _chk_cmds COMMAND "${ICO_DXC_EXE}" ${ICO_DXC_COMMON_FLAGS} -HV 2018
                 -D ICO_D3D11=1 ${_d} -T ${_p} -E ${_e} "${_s}" -Fo "${_chk_dir}/${_n}.dxil")
        endforeach()
        list(LENGTH _checks _chk_count)
        add_custom_target(shaders_d3d11_check
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_chk_dir}"
            ${_chk_cmds}
            COMMENT "DXC: ${_chk_count} shader entries with ICO_D3D11=1 (Direct3D 11 preprocessing)"
            VERBATIM)
    endif()
    set(ICO_SHADERS_TARGET ${target} PARENT_SCOPE)
endfunction()
