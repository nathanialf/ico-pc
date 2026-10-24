# The port's host build: compiles the game for Linux, Windows, Android (libmain.so), iOS and macOS
# hosts. Presets are in CMakePresets.json; docs/BUILDING.md describes them. (The PS2 ELF build
# is the decompilation's, not this repository's.)
cmake_minimum_required(VERSION 3.25)

# The presets name the pinned ccache (tools/fetch_toolchain.sh) as the compiler
# launcher. Where it is not there (an arm64 host, a checkout that has not
# fetched the toolchain) the build runs without it, never fails on it.
if(CMAKE_C_COMPILER_LAUNCHER AND NOT EXISTS "${CMAKE_C_COMPILER_LAUNCHER}"
   AND CMAKE_C_COMPILER_LAUNCHER MATCHES "/tools/toolchain/ccache/")
    message(STATUS "ico: ${CMAKE_C_COMPILER_LAUNCHER} not found (tools/fetch_toolchain.sh): no ccache")
    unset(CMAKE_C_COMPILER_LAUNCHER CACHE)
    unset(CMAKE_C_COMPILER_LAUNCHER)
endif()

project(ico_pc LANGUAGES C)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_compile_definitions(ICO_IOS=1)
endif()

include(cmake/IcoFlags.cmake)
include(cmake/IcoSources.cmake)
include(cmake/IcoExclusions.cmake)

# ICO_HEADLESS=OFF (the default) is the windowed ico_pc: the seki layer
# draws through port/render (ICO_RD) into an SDL3 window. ON is the headless
# build of the trace and test runs: the original packet code, no window, no
# Vulkan. In CMakePresets.json the window presets (ICO_HEADLESS OFF) are
# linux-arm64, win-x64, android-arm64, ios-moltenvk, macos-moltenvk, ios-app
# and macos-app; the others (linux-x64, asan, fptrap, linux-x64-clang,
# win-x64-clang) are headless.
option(ICO_HEADLESS "Build the headless ico_pc (no window, no renderer)" OFF)
option(ICO_FPTRAP "Unmask float divide-by-zero and invalid in simulation mode" OFF)
option(ICO_BUILD_BLOCKED "Also compile the sources cmake/IcoExclusions.cmake blocks" OFF)
option(ICO_LINK_EXE "Link the ico_pc executable (fails while symbols are unresolved)" OFF)
# ICO_RC_TOOLS: a release candidate's own tooling, compiled in an rc build
# alone (docs/BUILDING.md, "Release candidate tools"): today the Android Main
# row "Share a bug report" (port/ui/settings.h). tools/package_*.sh turn it on
# for a label ending in -rc<N> and nothing else, and check every package's
# program for the row's text (tools/check_rc_text.py); a release label with
# it on is refused below. OFF: ICO_RC_TOOLS is 0 in every source (the
# headers' default), and none of it is compiled.
option(ICO_RC_TOOLS "Compile the release candidates' tooling (rc packages only)" OFF)
if(ICO_RC_TOOLS)
    add_compile_definitions(ICO_RC_TOOLS=1)
endif()

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

# The source lists must match the union of config/link_order.*.txt.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_sources.py" --check
                    RESULT_VARIABLE _ico_gen_rc)
    if(NOT _ico_gen_rc EQUAL 0)
        message(WARNING "cmake/IcoSources.cmake is out of date: run tools/gen_sources.py")
    endif()
endif()
file(GLOB _ico_link_orders CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/config/link_order.*.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_ico_link_orders})

set(ICO_COMPAT_DIR "${CMAKE_SOURCE_DIR}/port/compat")
set(ICO_GAME_DEFINITIONS ICO_HOST=1)
if(ICO_HEADLESS)
    list(APPEND ICO_GAME_DEFINITIONS ICO_HEADLESS=1)
else()
    # the seki layer's host path onto rd.h
    list(APPEND ICO_GAME_DEFINITIONS ICO_RD=1)
endif()
if(ICO_FPTRAP)
    set(ICO_FPTRAP_DEFINITION ICO_FPTRAP=1)
endif()

# The include path of a programmer's sources, as the period build gave it:
# its own include directory first, then the others in the order the period
# build searched them, then the SDK headers (here port/compat).
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
    # One host source list: every game source
    # but ICO_EE_ONLY_SOURCES (ito/mpeg, replaced by port/fmv) is compiled
    # in both the headless and the window build.
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
    if(NOT ICO_HEADLESS)
        target_include_directories(ico_${prog} PRIVATE "${CMAKE_SOURCE_DIR}/port/render")
    endif()
    target_compile_definitions(ico_${prog} PRIVATE ${ICO_GAME_DEFINITIONS})
    # __FILE__ reads as the period build spelled it (src/main.c, ios/pad.c):
    # the period build compiled from inside the programmer's directory.
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
# The binary holds no disc data (docs/LEGAL.md). The 73 data-only members
# the PS2 build compiles from the user's ELF (tools/gen_data_c.py) are, here,
# uninitialised arrays in PAL's layout (port/data/gen/table_defs.c) that
# port/data/tables.c fills at boot from the boot ELF on the user's disc,
# decoding each record field by field through the disc's generated
# descriptor set (port/data/gen/<slug>/table_desc.c, one per row of
# config/regions.txt) and resolving pointer words through that set's address
# registry (port/data/gen/<slug>/ee_symbols.c). Those files hold only types,
# offsets, sizes, names and the CRCs of the manifests
# (config/tables_manifest*.txt); tools/gen_data_desc.py writes them and the
# data_desc_fresh test keeps them current. port/data/gen/tables.cmake lists
# them (ICO_TABLE_SOURCES) for every program that loads the tables.
# Configuring and building need no ELF.
#
# No table is const on the host: the loader writes them all, and the game
# writes three of the PS2 build's const .rodata tables (stageTable,
# motionLimitDef, seDef) which worked on the EE (no page
# protection). The generated definitions rename a header's `extern const`
# declaration around the #includes. A table the game writes must not be
# declared const where it is written either: clang deletes stores to an
# object declared const (motionLimitDef's header declares it without const
# for that; tools/check_const_writes.py looks for others).
set(ICO_DATA_GEN_DIR "${CMAKE_SOURCE_DIR}/port/data/gen")
include("${ICO_DATA_GEN_DIR}/tables.cmake")
add_library(ico_data OBJECT ${ICO_TABLE_SOURCES} "${CMAKE_SOURCE_DIR}/port/data/tables.c")
ico_include_dirs(sugipon incs)
# the generated files spell the record headers repo-relative ("ico2/...")
target_include_directories(ico_data PRIVATE ${incs} "${CMAKE_SOURCE_DIR}/port/data"
                           "${CMAKE_SOURCE_DIR}")
target_compile_definitions(ico_data PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(ico_data PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
# the loader is port code: the port's warning set
set_source_files_properties("${CMAKE_SOURCE_DIR}/port/data/tables.c" TARGET_DIRECTORY ico_data
                            PROPERTIES COMPILE_OPTIONS "${ICO_PORT_WARNINGS}")
list(APPEND ICO_GAME_OBJECT_LIBS ico_data)

# --- The platform layer -----------------------------------------------------
add_library(ico_platform STATIC
    port/platform/assert_host.c
    port/platform/clock.c
    port/platform/fpenv.c
    port/platform/hwregs.c)
target_include_directories(ico_platform PUBLIC port/platform PRIVATE "${ICO_COMPAT_DIR}")
# host_fs.c: file calls on UTF-8 paths (wide on Windows). Its own library because host_config.c, its main user, is
# compiled into many tests that do not link ico_platform (linked to each of
# them at the end of this file).
add_library(ico_host_fs STATIC port/platform/host_fs.c)
target_include_directories(ico_host_fs PUBLIC port/platform)
target_compile_options(ico_host_fs PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_platform PUBLIC ico_host_fs)
target_compile_definitions(ico_platform PRIVATE ${ICO_FPTRAP_DEFINITION})
target_compile_options(ico_platform PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

# --- The game library and the program ---------------------------------------
set(ICO_GAME_OBJECTS "")
foreach(lib ${ICO_GAME_OBJECT_LIBS})
    list(APPEND ICO_GAME_OBJECTS $<TARGET_OBJECTS:${lib}>)
endforeach()
add_library(ico_game STATIC ${ICO_GAME_OBJECTS})
set_target_properties(ico_game PROPERTIES LINKER_LANGUAGE C)
# the game code reads the region's index, ico_region (port/core)
target_link_libraries(ico_game PUBLIC ico_region)

if(ICO_LINK_EXE)
    # main_host.c: the program's main and its options; trace_host.c: the
    # per-Main-tick trace; host_config.c: the exe
    # directory, ico-pc.ini, the ISO check, the log and the Windows dialogs.
    set(_ico_pc_sources port/platform/main_host.c port/platform/trace_host.c
                        port/platform/host_config.c)
    # The package label (tools/package_win.sh, package_linux.sh,
    # package_macos.sh, package_ios.sh and android/app/build.gradle pass
    # -DICO_VERSION_LABEL=<label>): the version numbers x.y.z of a label that
    # starts with [v]x.y.z, 0.0.0 otherwise (a development build). The
    # Windows version information and the Apple bundles' Info.plist carry
    # them; the log and the Options menu show the label itself.
    set(ICO_VERSION_LABEL "" CACHE STRING "Package label for the program's version information")
    # x.y.z.N: N is the release candidate's number (v0.7.0-rc2 -> 0.7.0.2), 0
    # for the final; 0.0.0.0 for a development build (cmake/IcoVersion.cmake)
    include(cmake/IcoVersion.cmake)
    ico_version_from_label("${ICO_VERSION_LABEL}" ICO_VER_NUMERIC ICO_VER_TEXT)
    # the rc tooling never goes into a package with a release's label
    if(ICO_RC_TOOLS AND NOT ICO_VERSION_LABEL STREQUAL "" AND
       NOT ICO_VERSION_LABEL MATCHES "-rc[0-9]+$")
        message(FATAL_ERROR "ICO_RC_TOOLS=ON with ICO_VERSION_LABEL=${ICO_VERSION_LABEL}: "
                            "the rc tooling is for a label ending in -rc<N> alone")
    endif()
    if(ICO_VER_TEXT MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)\\.")
        set(ICO_VER_XYZ "${CMAKE_MATCH_1}")
    else()
        set(ICO_VER_XYZ "0.0.0")
    endif()
    # The label as given (v0.7.1-rc1) for the log and the Options menu, only
    # when it parses as a version: "" otherwise (a development build; CI's
    # "ci" artifacts too), and the menu then shows nothing.
    if(ICO_VER_TEXT STREQUAL "0.0.0.0")
        set(ICO_VERSION_LABEL_C "")
    else()
        string(REGEX REPLACE "[\"\\\\]" "" ICO_VERSION_LABEL_C "${ICO_VERSION_LABEL}")
    endif()
    configure_file(port/platform/ico_version.h.in "${CMAKE_BINARY_DIR}/generated/ico_version.h"
                   @ONLY)
    if(ANDROID)
        # Android: the program is libmain.so, which SDL's Java side
        # (SDLActivity, android/) loads after libSDL3.so; it calls SDL_main
        # (port/platform/android/main_android.c). The target keeps the name
        # ico_pc, so everything that adds to it applies.
        # host_android.c: the app's folders, the log mirror to logcat, the
        # message box; android_paths.c the file layout (tested on every
        # platform, port/platform/CMakeLists.txt). ICO_HOST_FORCE_PORTABLE:
        # host_config.c keeps everything in the app's files folder.
        # iso_import.c: the first start's copy of the disc image chosen in
        # the system's picker (tested on Linux as iso_import).
        add_library(ico_pc SHARED ${_ico_pc_sources} port/platform/android/main_android.c
                                  port/platform/android/host_android.c
                                  port/platform/android/android_paths.c
                                  port/platform/android/iso_import.c)
        set_target_properties(ico_pc PROPERTIES OUTPUT_NAME main)
        target_compile_definitions(ico_pc PRIVATE ICO_HOST_FORCE_PORTABLE=1)
        target_include_directories(ico_pc PRIVATE port/platform/android)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # iOS: ICO_IOS comes from the top (every library sees it)
        add_executable(ico_pc MACOSX_BUNDLE ${_ico_pc_sources} port/platform/ios/main_ios.c)
        target_compile_definitions(ico_pc PRIVATE ICO_HOST_FORCE_PORTABLE=1)
        set_target_properties(ico_pc PROPERTIES
            OUTPUT_NAME ICO
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/port/platform/ios/Info.plist.in"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${ICO_VER_XYZ}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${ICO_VER_XYZ}"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO)
    elseif(APPLE)
        add_executable(ico_pc MACOSX_BUNDLE ${_ico_pc_sources})
        set_target_properties(ico_pc PROPERTIES
            OUTPUT_NAME ICO
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/port/platform/macos/Info.plist.in"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${ICO_VER_XYZ}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${ICO_VER_XYZ}")
    else()
        add_executable(ico_pc ${_ico_pc_sources})
    endif()
    # ico_version.h (above): ICO_VERSION_LABEL for main_host.c
    target_include_directories(ico_pc PRIVATE "${CMAKE_BINARY_DIR}/generated")
    # The hardware floor: libgraph's vsync
    # wait, libdma, and libgcc's fptodp. Both builds: the window build draws
    # through rd above it. In the program itself rather than a library, since
    # they call back into the game. debug.c and debug_exception.c are game
    # sources (cmake/IcoSources.cmake).
    target_sources(ico_pc PRIVATE port/null/gfx_null.c port/null/libgcc_null.c)
    set_source_files_properties(port/null/gfx_null.c port/null/libgcc_null.c TARGET_DIRECTORY
                                ico_pc PROPERTIES INCLUDE_DIRECTORIES "${ICO_COMPAT_DIR}")
    if(ICO_HEADLESS)
        target_compile_definitions(ico_pc PRIVATE ICO_HEADLESS=1)
    endif()
    target_compile_options(ico_pc PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(ico_pc PRIVATE ico_game ico_platform ico_region)
    if(ANDROID)
        target_link_libraries(ico_pc PRIVATE log android m dl)
    elseif(NOT WIN32)
        target_link_libraries(ico_pc PRIVATE m)
    else()
        # A double-clicked program: GUI subsystem (no console window; output
        # goes to logs/ico-pc.log) and the GCC runtime linked in so the .exe
        # needs only system DLLs. The file dialog and message box libraries
        # come with host_config.c (ico_link_host_fs below).
        target_link_options(ico_pc PRIVATE -mwindows)
        # The application manifest (port/platform/win/ico_pc.manifest): the
        # UTF-8 active code page for paths,
        # compiled by the toolchain's windres.
        enable_language(RC)
        # The version information (port/platform/win/ico_pc.rc): the package
        # label above as the numbers x.y.z.N and the text "x.y.z.N" (0.0.0.0
        # for a development build), from the generated ico_version.h.
        target_sources(ico_pc PRIVATE port/platform/win/ico_pc.rc)
        set_property(SOURCE port/platform/win/ico_pc.rc APPEND PROPERTY COMPILE_OPTIONS
                     "-I${CMAKE_BINARY_DIR}/generated")
        set_property(SOURCE port/platform/win/ico_pc.rc APPEND PROPERTY OBJECT_DEPENDS
                     "${CMAKE_BINARY_DIR}/generated/ico_version.h")
        set_property(SOURCE port/platform/win/ico_pc.rc APPEND PROPERTY OBJECT_DEPENDS
                     "${CMAKE_SOURCE_DIR}/port/platform/win/ico_pc.manifest")
        target_include_directories(ico_pc PRIVATE port/platform/win)
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_link_options(ico_pc PRIVATE -static-libgcc -static)
        endif()
    endif()
endif()

# --- Tests ------------------------------------------------------------------
enable_testing()
include(cmake/IcoTests.cmake) # before the first add_test: labels, and the test programs leave `all`
# ctest runs the test programs only where they can run: a native build, a
# cross build with an emulator, or a cross build for the host's own system.
# Every directory below reads this one switch.
set(ICO_RUN_TESTS OFF)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    set(ICO_RUN_TESTS ON)
    set(ICO_DISC_IMAGE "${CMAKE_SOURCE_DIR}/baserom/Ico_PAL.iso" CACHE FILEPATH
        "The PAL disc image (SCES-50760) the disc tests read, never committed")
    set(ICO_DISC_IMAGE_JPN "${CMAKE_SOURCE_DIR}/baserom/Ico_JPN.iso" CACHE FILEPATH
        "The Japanese disc image (SCPS-11003) the disc tests read, never committed")
    set(ICO_DISC_IMAGE_KOR "${CMAKE_SOURCE_DIR}/baserom/Ico_KOR.iso" CACHE FILEPATH
        "The Korean disc image (SCPS-56001) the disc tests read, never committed")
    set(ICO_DISC_IMAGE_NTSC "${CMAKE_SOURCE_DIR}/baserom/Ico_USA.iso" CACHE FILEPATH
        "The North American disc image (SCUS-97113) the disc tests read, never committed")
endif()
add_executable(fpenv_test port/platform/fpenv_test.c)
target_compile_options(fpenv_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_compile_definitions(fpenv_test PRIVATE ${ICO_FPTRAP_DEFINITION})
target_link_libraries(fpenv_test PRIVATE ico_platform)
if(ICO_RUN_TESTS)
    add_test(NAME fpenv COMMAND fpenv_test)
    if(ICO_FPTRAP)
        add_test(NAME fpenv_trap COMMAND fpenv_test trap)
    endif()
endif()

# the release packages' textures folder staging and archive checks (no build, no game)
find_program(ICO_BASH bash)
if(ICO_BASH AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME package_textures COMMAND ${ICO_BASH} ${CMAKE_SOURCE_DIR}/tools/test/package_textures_test.sh)
endif()

# the documentation gate: links, anchors and wording of README.md and docs/
# (no build, no game)
if(Python3_FOUND)
    add_test(NAME docs_check COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/check_docs.py")
    add_test(NAME texture_catalog COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/test/texture_catalog_test.py")
endif()

# the rc-only tooling's gate (ICO_RC_TOOLS above): the checker's own cases
# and the package scripts' use of it (rc_tools_gate), and this build's
# program holding the rc row's text exactly when ICO_RC_TOOLS is on
# (rc_tools_binary; absent in the default build)
if(Python3_FOUND)
    add_test(NAME rc_tools_gate
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/check_rc_text.py" --selftest)
    # (the row's text is in the menus' tables, which the window build links;
    # a headless rc build is not checked for it)
    if(TARGET ico_pc AND NOT ANDROID AND NOT CMAKE_CROSSCOMPILING AND
       (NOT ICO_RC_TOOLS OR NOT ICO_HEADLESS))
        if(ICO_RC_TOOLS)
            set(_ico_rc_expect present)
        else()
            set(_ico_rc_expect absent)
        endif()
        add_test(NAME rc_tools_binary
                 COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/check_rc_text.py"
                         --expect ${_ico_rc_expect} "$<TARGET_FILE:ico_pc>")
    endif()
endif()

# On arm64 Android (and arm64 Linux) the movie decoder
# (port/fmv, Ittiam libmpeg2) uses the library's own NEON routines, which are
# assembly: the language is enabled here, in the top directory, as CMake
# asks.  Apple and Windows arm64 keep the C routines (the library's
# assembly is written for ELF).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|AARCH64)$" AND NOT APPLE AND NOT WIN32)
    enable_language(ASM)
    set(ICO_LIBMPEG2_NEON ON)
else()
    set(ICO_LIBMPEG2_NEON OFF)
endif()

# ico_copy_sdl3_dll(<target>...): on Windows, SDL3.dll beside each program
# after it is linked, so it runs from the build directory. SDL3::SDL3-shared
# is found by port/rhi and made global there, so call this after port/rhi
# is configured (or from a deferred function).
function(ico_copy_sdl3_dll)
    if(WIN32 AND TARGET SDL3::SDL3-shared)
        foreach(t IN LISTS ARGN)
            add_custom_command(TARGET ${t} POST_BUILD
                               COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                                       "$<TARGET_FILE:SDL3::SDL3-shared>" "$<TARGET_FILE_DIR:${t}>"
                               VERBATIM)
        endforeach()
    endif()
endfunction()

# The libraries, DXC and the validation layer tools/fetch_deps.sh fetches
set(ICO_DEPS_DIR "${CMAKE_SOURCE_DIR}/tools/toolchain/deps" CACHE PATH
    "Prefix of the dependencies tools/fetch_deps.sh installs")

# the SPIR-V gate over the shaders the build compiled (port/shaders,
# tools/check_spirv.py): vertex shaders in v0.5.2's forms (no vector of
# booleans, no any()/all(), no boolean select) and opcode envelope (no loop,
# no dot, no logical-not-equal, no undef), every module within its size
# budget (the sizes and the shape census printed), no array-typed values, no
# local arrays in a vertex shader
if(Python3_FOUND AND EXISTS "${ICO_DEPS_DIR}/dxc/bin/dxc")
    add_test(NAME spirv_check COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/check_spirv.py" --sizes "${CMAKE_BINARY_DIR}/shaders")
endif()

# --- port/core: the game region (config/regions.txt) -------------------------
add_subdirectory(port/core)

# --- port/math: the VU0 maths in C and the newlib copies ---------------------
add_subdirectory(port/math)

add_subdirectory(port/data)
add_subdirectory(port/null)

# --- The port's modules -----------------------------------------------------
add_subdirectory(port/platform)
add_subdirectory(port/test)
add_subdirectory(port/rhi)
add_subdirectory(port/shaders)
add_subdirectory(port/render)
add_subdirectory(port/audio)
add_subdirectory(port/game)
# after port/render, which finds SDL3 (input_sdl.c is the window build's)
add_subdirectory(port/input)
add_subdirectory(port/config)
add_subdirectory(port/save)
add_subdirectory(port/fmv)
add_subdirectory(port/ui) # runtime text, layout extension, popups, the menus

# --- The window build: ico_pc on port/render --------------------------------
if(TARGET ico_pc AND NOT ICO_HEADLESS)
    if(NOT TARGET ico_render OR NOT TARGET SDL3::SDL3)
        message(FATAL_ERROR "The window build of ico_pc needs port/render (DXC, Vulkan-Headers, "
                            "volk) and SDL3 for this target: run tools/fetch_deps.sh, or "
                            "configure with -DICO_HEADLESS=ON")
    endif()
    target_sources(ico_pc PRIVATE port/platform/window_host.c port/platform/window_switch.c
                                  port/platform/window_video.c
                                  port/platform/dump_zip.c)
    if(ICO_RC_TOOLS AND ANDROID)
        # an rc build's Share a bug report: the files it offers (share_report.h)
        target_sources(ico_pc PRIVATE port/platform/share_report.c)
    endif()
    # The display's vblanks for the vsync lock's log (display_clock.h): a
    # DXGI output's on Windows, none elsewhere
    if(WIN32)
        # and the window's messages for the event pump's long-call lines,
        # the display and the computer kept awake (window_pump_win.h)
        target_sources(ico_pc PRIVATE port/platform/display_clock_win.c
                                      port/platform/window_pump_win.c)
    else()
        target_sources(ico_pc PRIVATE port/platform/display_clock_none.c)
    endif()
    # ico_texpack_export: Options > Extras > Export textures (texpack_export.h)
    target_link_libraries(ico_pc PRIVATE ico_render ico_texpack_export ico_miniz SDL3::SDL3)
    if(NOT ANDROID AND NOT APPLE)
        # Extras > Add to Steam: Windows, Linux and the Steam Deck (window_host.c
        # offers the row on the same platforms)
        target_sources(ico_pc PRIVATE port/platform/steam_shortcut.c)
        if(WIN32)
            target_link_libraries(ico_pc PRIVATE advapi32)
        endif()
    endif()
    if(ANDROID)
        # The player's own graphics driver (Options > Graphics
        # driver), loaded with libadrenotools (BSD-2-Clause, Billy Laws;
        # with liblinkernsbypass, docs/THIRD_PARTY.md), the source
        # tools/fetch_android.sh pins (ICO_ADRENOTOOLS_SRC in
        # tools/toolchain/android.env). It is C++: the program links the
        # NDK's static C++ runtime (ANDROID_STL c++_static in the toolchain
        # file and android/app/build.gradle). Its hook libraries (main_hook,
        # hook_impl, file_redirect_hook) are their own .so files, built
        # because Gradle names them as targets and packed beside libmain.so.
        # -DICO_ADRENOTOOLS_SRC=<dir> overrides; else android.env's, read at
        # every configure so a new pin is picked up
        if(NOT ICO_ADRENOTOOLS_SRC)
            set(_ico_android_env "${CMAKE_SOURCE_DIR}/tools/toolchain/android.env")
            if(EXISTS "${_ico_android_env}")
                file(STRINGS "${_ico_android_env}" _ico_adt_line REGEX "^ICO_ADRENOTOOLS_SRC=")
                string(REGEX REPLACE "^ICO_ADRENOTOOLS_SRC=\"?([^\"]*)\"?$" "\\1"
                                     ICO_ADRENOTOOLS_SRC "${_ico_adt_line}")
            endif()
        endif()
        if(NOT EXISTS "${ICO_ADRENOTOOLS_SRC}/CMakeLists.txt"
           OR NOT EXISTS "${ICO_ADRENOTOOLS_SRC}/lib/linkernsbypass/CMakeLists.txt")
            message(FATAL_ERROR "The Android build needs libadrenotools at "
                                "\"${ICO_ADRENOTOOLS_SRC}\": run tools/fetch_android.sh")
        endif()
        enable_language(CXX)
        add_subdirectory("${ICO_ADRENOTOOLS_SRC}" adrenotools EXCLUDE_FROM_ALL)
        target_sources(ico_pc PRIVATE port/platform/gpu_driver.c
                                      port/platform/android/gpu_driver_android.c)
        target_link_libraries(ico_pc PRIVATE adrenotools ico_json ico_miniz)
        set_target_properties(ico_pc PROPERTIES LINKER_LANGUAGE CXX)
    endif()
    ico_copy_sdl3_dll(ico_pc)
endif()

# --- host_fs for every target that compiles host_config.c -------------------
# and, on Windows, the libraries of its file dialog and message box
function(ico_link_host_fs dir)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS targets)
        get_target_property(type ${t} TYPE)
        if(type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY)$")
            get_target_property(srcs ${t} SOURCES)
            if(srcs MATCHES "host_config\\.c")
                target_link_libraries(${t} PRIVATE ico_host_fs)
                if(WIN32)
                    target_link_libraries(${t} PRIVATE comdlg32 user32)
                endif()
            endif()
        endif()
    endforeach()
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(d IN LISTS subdirs)
        ico_link_host_fs("${d}")
    endforeach()
endfunction()
ico_link_host_fs("${CMAKE_SOURCE_DIR}")

# --- ico_region for every program that compiles game sources -----------------
# The game code reads the region's index, ico_region (ico2/common/include/
# region.h), which port/core's ico_region defines: ico_pc has it with
# ico_game (above), and each test program that compiles a source from ico2/
# links it here, so one added later is covered (deferred: some directories
# create their test programs in deferred calls, registered before this one)
function(ico_link_region dir)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS targets)
        get_target_property(type ${t} TYPE)
        if(NOT type MATCHES "^(EXECUTABLE|SHARED_LIBRARY)$")
            continue()
        endif()
        get_target_property(srcs ${t} SOURCES)
        get_target_property(tdir ${t} SOURCE_DIR)
        foreach(src IN LISTS srcs)
            if(src MATCHES "^\\$<")
                continue()
            endif()
            get_filename_component(abs "${src}" ABSOLUTE BASE_DIR "${tdir}")
            string(FIND "${abs}" "${CMAKE_SOURCE_DIR}/ico2/" at)
            if(at EQUAL 0)
                target_link_libraries(${t} PRIVATE ico_region)
                break()
            endif()
        endforeach()
    endforeach()
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(d IN LISTS subdirs)
        ico_link_region("${d}")
    endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL ico_link_region "${CMAKE_SOURCE_DIR}")

# The test programs build with `--target tests`, not with `all` (cmake/IcoTests.cmake)
# (deferred: some directories create their test programs in deferred calls)
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL ico_finalize_tests)
