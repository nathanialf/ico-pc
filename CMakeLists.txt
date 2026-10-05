# The PC port's host build. The PS2 build is ./build.sh (build.ninja); this
# one compiles the game for Linux and Windows hosts. Presets are in
# CMakePresets.json; docs/BUILDING.md ("Host build") describes them.
cmake_minimum_required(VERSION 3.25)
project(ico_pc LANGUAGES C)

include(cmake/IcoFlags.cmake)
include(cmake/IcoSources.cmake)
include(cmake/IcoExclusions.cmake)

option(ICO_HEADLESS "Build without the renderer-owned sources" ON)
option(ICO_FPTRAP "Unmask float divide-by-zero and invalid in simulation mode" OFF)
option(ICO_BUILD_BLOCKED "Also compile the sources cmake/IcoExclusions.cmake blocks" OFF)
option(ICO_LINK_EXE "Link the ico_pc executable (fails while symbols are unresolved)" OFF)
set(ICO_DATA_DIR "${CMAKE_SOURCE_DIR}/build/data" CACHE PATH
    "Directory of the generated data tables (<member>.c, tools/gen_data_c.py)")

# The retail game ran with its asserts live (the reconstruction calls
# __assert where the macro expanded), so no configuration defines NDEBUG:
# an assert's expression is evaluated as on the PS2.
foreach(cfg RELEASE RELWITHDEBINFO MINSIZEREL)
    string(REGEX REPLACE "(^| )[-/]DNDEBUG( |$)" " " CMAKE_C_FLAGS_${cfg} "${CMAKE_C_FLAGS_${cfg}}")
endforeach()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # gnu11
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The source lists must match config/link_order.pal.txt.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_sources.py" --check
                    RESULT_VARIABLE _ico_gen_rc)
    if(NOT _ico_gen_rc EQUAL 0)
        message(WARNING "cmake/IcoSources.cmake is out of date: run tools/gen_sources.py")
    endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_SOURCE_DIR}/config/link_order.pal.txt")

set(ICO_COMPAT_DIR "${CMAKE_SOURCE_DIR}/port/compat")
set(ICO_GAME_DEFINITIONS ICO_HOST=1)
if(ICO_HEADLESS)
    list(APPEND ICO_GAME_DEFINITIONS ICO_HEADLESS=1)
endif()
if(ICO_FPTRAP)
    set(ICO_FPTRAP_DEFINITION ICO_FPTRAP=1)
endif()

# The include path of a programmer's sources, as tools/compile_c.sh gives
# it: its own include directory first, then the others in the order the
# period build searched them, then the SDK headers (here port/compat).
function(ico_include_dirs prog out)
    set(dirs "")
    foreach(p ${prog} sugipon omori common ito fumi seki script)
        set(d "${CMAKE_SOURCE_DIR}/ico2/${p}/include")
        if(IS_DIRECTORY "${d}" AND NOT "${d}" IN_LIST dirs)
            list(APPEND dirs "${d}")
        endif()
    endforeach()
    list(APPEND dirs "${ICO_COMPAT_DIR}")
    set(${out} "${dirs}" PARENT_SCOPE)
endfunction()

# --- The game: one object library per programmer directory -----------------
set(ICO_GAME_OBJECT_LIBS "")
set(ICO_COMPILED_COUNT 0)
foreach(prog ${ICO_PROGRAMMERS})
    set(srcs ${ICO_SOURCES_${prog}})
    if(NOT ICO_HEADLESS)
        foreach(s ${ICO_RENDERER_SOURCES})
            if(s MATCHES "^ico2/${prog}/")
                list(APPEND srcs "${s}")
            endif()
        endforeach()
    endif()
    if(NOT ICO_BUILD_BLOCKED)
        list(REMOVE_ITEM srcs ${ICO_BLOCKED_SOURCES})
    endif()
    if(NOT srcs)
        continue()
    endif()
    list(LENGTH srcs n)
    math(EXPR ICO_COMPILED_COUNT "${ICO_COMPILED_COUNT} + ${n}")
    list(TRANSFORM srcs PREPEND "${CMAKE_SOURCE_DIR}/")
    add_library(ico_${prog} OBJECT ${srcs})
    ico_include_dirs(${prog} incs)
    target_include_directories(ico_${prog} PRIVATE ${incs})
    target_compile_definitions(ico_${prog} PRIVATE ${ICO_GAME_DEFINITIONS})
    # __FILE__ reads as the period build spelled it (src/main.c, ios/pad.c):
    # tools/compile_c.sh compiled from inside the programmer's directory.
    target_compile_options(ico_${prog} PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                           ${ICO_GAME_WARNINGS}
                           "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/ico2/${prog}/=")
    list(APPEND ICO_GAME_OBJECT_LIBS ico_${prog})
endforeach()
message(STATUS "ico: ${ICO_COMPILED_COUNT} game sources in the build")

# The game's main becomes ico_game_main; port/platform/main_host.c is the
# program's main.
set_source_files_properties("${CMAKE_SOURCE_DIR}/ico2/common/src/main.c" TARGET_DIRECTORY ico_common
                            PROPERTIES COMPILE_DEFINITIONS "main=ico_game_main")

# --- The data tables --------------------------------------------------------
# The 73 data-only members are C written from the user's base ELF
# (baserom/pal/baseelf.elf, never committed) by tools/gen_data_c.py in its
# --symbol-map mode, which needs no period link. The build writes them into
# <build>/data/. Without the base ELF or pyelftools, a directory of already
# generated tables (ICO_DATA_DIR, by default the PS2 build's build/data/) is
# used when complete; otherwise the data library is left out.
#
# The generated C includes its record headers as "../../ico2/<prog>/include/
# <h>.h". port/compat is on the include path and sits two levels below the
# tree root, so those spellings resolve from any output directory.
set(ICO_BASE_ELF "${CMAKE_SOURCE_DIR}/baserom/pal/baseelf.elf" CACHE FILEPATH
    "The PAL base ELF the data tables are read from")
set(ICO_DATA_PYTHON "${Python3_EXECUTABLE}")
if(EXISTS "${CMAKE_SOURCE_DIR}/.venv/bin/python")
    set(ICO_DATA_PYTHON "${CMAKE_SOURCE_DIR}/.venv/bin/python")
endif()
set(_ico_have_elftools 1)
if(ICO_DATA_PYTHON)
    execute_process(COMMAND "${ICO_DATA_PYTHON}" -c "import elftools"
                    RESULT_VARIABLE _ico_have_elftools OUTPUT_QUIET ERROR_QUIET)
endif()
set(ICO_DATA_SOURCES "")
if(ICO_DATA_PYTHON AND _ico_have_elftools EQUAL 0 AND EXISTS "${ICO_BASE_ELF}")
    set(_gen "${CMAKE_SOURCE_DIR}/tools/gen_data_c.py")
    set(_deps "${_gen}" "${ICO_BASE_ELF}"
        "${CMAKE_SOURCE_DIR}/config/data_schema.pal.txt"
        "${CMAKE_SOURCE_DIR}/config/data_members.pal.txt"
        "${CMAKE_SOURCE_DIR}/config/symbol_addrs.pal.txt"
        "${CMAKE_SOURCE_DIR}/config/symbol_addrs.pal.data.txt")
    foreach(m ${ICO_DATA_MEMBERS})
        set(out "${CMAKE_BINARY_DIR}/data/${m}.c")
        add_custom_command(
            OUTPUT "${out}"
            COMMAND "${ICO_DATA_PYTHON}" "${_gen}" --c "${m}" --symbol-map
                    --elf "${ICO_BASE_ELF}" --out "${out}"
            DEPENDS ${_deps}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Generating data table ${m}"
            VERBATIM)
        list(APPEND ICO_DATA_SOURCES "${out}")
    endforeach()
    message(STATUS "ico: data tables generated from ${ICO_BASE_ELF}")
else()
    set(ICO_DATA_MISSING "")
    # Each table is a configure dependency: when the PS2 build regenerates
    # or deletes build/data/, the next build re-runs this step.
    foreach(m ${ICO_DATA_MEMBERS})
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ICO_DATA_DIR}/${m}.c")
        if(EXISTS "${ICO_DATA_DIR}/${m}.c")
            list(APPEND ICO_DATA_SOURCES "${ICO_DATA_DIR}/${m}.c")
        else()
            list(APPEND ICO_DATA_MISSING "${m}")
        endif()
    endforeach()
    if(ICO_DATA_MISSING)
        set(ICO_DATA_SOURCES "")
        message(STATUS "ico: no data tables: ${ICO_BASE_ELF} or pyelftools (tools/setup.sh) is "
                       "missing and ${ICO_DATA_DIR} is incomplete; the data library is not built. "
                       "Extract your base ELF (tools/extract_elf.sh) and reconfigure.")
    else()
        message(STATUS "ico: data tables from ${ICO_DATA_DIR}")
    endif()
endif()
if(ICO_DATA_SOURCES)
    add_library(ico_data OBJECT ${ICO_DATA_SOURCES})
    ico_include_dirs(sugipon incs)
    target_include_directories(ico_data PRIVATE ${incs})
    target_compile_definitions(ico_data PRIVATE ${ICO_GAME_DEFINITIONS})
    target_compile_options(ico_data PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                           ${ICO_GAME_WARNINGS})
    list(APPEND ICO_GAME_OBJECT_LIBS ico_data)
endif()

# --- The platform layer -----------------------------------------------------
add_library(ico_platform STATIC
    port/platform/assert_host.c
    port/platform/fpenv.c
    port/platform/hwregs.c)
target_include_directories(ico_platform PUBLIC port/platform PRIVATE "${ICO_COMPAT_DIR}")
target_compile_definitions(ico_platform PRIVATE ${ICO_FPTRAP_DEFINITION})
target_compile_options(ico_platform PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

# --- The game library and the program ---------------------------------------
set(ICO_GAME_OBJECTS "")
foreach(lib ${ICO_GAME_OBJECT_LIBS})
    list(APPEND ICO_GAME_OBJECTS $<TARGET_OBJECTS:${lib}>)
endforeach()
add_library(ico_game STATIC ${ICO_GAME_OBJECTS})
set_target_properties(ico_game PROPERTIES LINKER_LANGUAGE C)

if(ICO_LINK_EXE)
    add_executable(ico_pc port/platform/main_host.c)
    target_compile_options(ico_pc PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(ico_pc PRIVATE ico_game ico_platform)
    if(NOT WIN32)
        target_link_libraries(ico_pc PRIVATE m)
    endif()
endif()

# --- Tests ------------------------------------------------------------------
enable_testing()
add_executable(fpenv_test port/platform/fpenv_test.c)
target_compile_options(fpenv_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_compile_definitions(fpenv_test PRIVATE ${ICO_FPTRAP_DEFINITION})
target_link_libraries(fpenv_test PRIVATE ico_platform)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME fpenv COMMAND fpenv_test)
    if(ICO_FPTRAP)
        add_test(NAME fpenv_trap COMMAND fpenv_test trap)
    endif()
endif()
