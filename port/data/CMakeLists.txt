# port/data: the disc VFS (vfs.c) and its ISO9660 backend, the libcdvd host
# layer the game's fumi/ios/cdvd.c and seki/src/FileManager.c call, the SIF
# host layer (no IOP reboot, no IRX modules, RPC to host servers) and the
# IOP RAM stand-in.
# miniz (MIT, port/third_party/miniz, docs/THIRD_PARTY.md): the ZIP
# reader and writer behind the extracted archive (archive.c, extract.c).
# Third-party code: upstream's split sources as released (plus the
# miniz_export.h its build would generate), warnings off; the
# zlib-compatible names are left out so nothing can clash with a system zlib.
set(_miniz "${CMAKE_SOURCE_DIR}/port/third_party/miniz")
add_library(ico_miniz STATIC "${_miniz}/miniz.c" "${_miniz}/miniz_tdef.c"
                             "${_miniz}/miniz_tinfl.c" "${_miniz}/miniz_zip.c")
target_include_directories(ico_miniz PUBLIC "${CMAKE_SOURCE_DIR}/port/third_party/miniz")
target_compile_definitions(ico_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES=1)
target_compile_options(ico_miniz PRIVATE -w)

# libchdr (BSD-3-Clause): reads .chd disc images, the compressed form
# PCSX2 users keep their discs in (iso9660.c). tools/fetch_deps.sh installs
# the source tree (tools/toolchain/deps/libchdr); it is compiled here with
# each preset's own compiler, statically, with the decoders it bundles
# (LZMA, miniz for Deflate, zstd, dr_flac), so the program needs no system
# zlib or zstd (package_linux.sh's NEEDED allow-list has neither). Its
# options are upstream's defaults (raw CD sectors and subcode decoded, block
# CRCs checked). Its miniz is a second copy beside ico_miniz with the same
# function names, so chdr_miniz_names.h, forced into every one of its
# files, gives that copy's exported functions a chdr_ prefix; its LZMA is
# already prefixed (CHDR_) and nothing else in the port defines zstd's or
# libchdr's names.
set(ICO_LIBCHDR_DIR "${ICO_DEPS_DIR}/libchdr" CACHE PATH "libchdr source tree (tools/fetch_deps.sh)")
if(NOT EXISTS "${ICO_LIBCHDR_DIR}/include/libchdr/chd.h")
    message(FATAL_ERROR "port/data needs libchdr at ${ICO_LIBCHDR_DIR}: run tools/fetch_deps.sh")
endif()
set(_chdr "${ICO_LIBCHDR_DIR}")
add_library(ico_libchdr STATIC
    ${_chdr}/src/libchdr_bitstream.c
    ${_chdr}/src/libchdr_cdrom.c
    ${_chdr}/src/libchdr_chd.c
    ${_chdr}/src/libchdr_codec_cdfl.c
    ${_chdr}/src/libchdr_codec_cdlz.c
    ${_chdr}/src/libchdr_codec_cdzl.c
    ${_chdr}/src/libchdr_codec_cdzs.c
    ${_chdr}/src/libchdr_codec_flac.c
    ${_chdr}/src/libchdr_codec_huff.c
    ${_chdr}/src/libchdr_codec_lzma.c
    ${_chdr}/src/libchdr_codec_zlib.c
    ${_chdr}/src/libchdr_codec_zstd.c
    ${_chdr}/src/libchdr_flac.c
    ${_chdr}/src/libchdr_huffman.c
    ${_chdr}/deps/lzma-25.01/src/LzmaDec.c
    ${_chdr}/deps/miniz-3.1.1/miniz.c
    ${_chdr}/deps/zstd-1.5.7/zstddeclib.c)
target_include_directories(ico_libchdr PUBLIC "${_chdr}/include")
# miniz as upstream's deps/miniz-3.1.1/CMakeLists.txt builds it: inflate only
target_compile_definitions(ico_libchdr PRIVATE MINIZ_NO_ARCHIVE_APIS MINIZ_NO_DEFLATE_APIS
                                               MINIZ_NO_STDIO MINIZ_NO_TIME)
target_compile_options(ico_libchdr PRIVATE
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/chdr_miniz_names.h")
# third-party code: its own warnings are not ours to fix
target_compile_options(ico_libchdr PRIVATE -w)
set_target_properties(ico_libchdr PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ico_json: json.h's RFC 8259 reader, no dependencies; read
# by archive.c (meta.json) and port/render's glTF loader.
add_library(ico_json STATIC json.c)
target_include_directories(ico_json PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_json PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})

# json_test: numbers (signs, fractions, exponents, the integer views),
# nesting and the depth limit, escapes, malformed input (CPU only).
add_executable(json_test test/json_test.c)
target_compile_options(json_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(json_test PRIVATE ico_json)
if(ICO_RUN_TESTS)
    add_test(NAME json COMMAND json_test)
endif()

# archive.c is the VFS backend over ico.o2r; extract.c the first-run
# extractor that writes it (it calls host_config.c's SHA-1, which ico_pc and
# archive_test compile).
add_library(ico_port_data STATIC
    archive.c
    cdvd_host.c
    df_pack.c
    extract.c
    iop_ram.c
    iso9660.c
    sif_host.c
    sifdev_host.c
    vfs.c)
# port/platform is searched for host_loop.h (the vsync
# callback list cdvd_host.c registers with when it exists).
target_include_directories(ico_port_data
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${ICO_COMPAT_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(ico_port_data PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
# cdvd_host.c registers with ico_platform's host loop. Tests that drive the
# vsync themselves define ico_host_on_vsync_register and pull nothing from it.
target_link_libraries(ico_port_data INTERFACE ico_platform)
target_link_libraries(ico_port_data INTERFACE ico_miniz)
target_link_libraries(ico_port_data PRIVATE ico_libchdr)
target_link_libraries(ico_port_data PUBLIC ico_json)
target_include_directories(ico_port_data PRIVATE "${CMAKE_SOURCE_DIR}/port/third_party/miniz")
target_compile_definitions(ico_port_data PRIVATE MINIZ_NO_ZLIB_COMPATIBLE_NAMES=1)

if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_port_data)
endif()

add_executable(vfs_test test/vfs_test.c)
target_include_directories(vfs_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(vfs_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(vfs_test PRIVATE ico_port_data)
if(ICO_RUN_TESTS)
    # a synthetic image built by the test: runs everywhere, no disc needed
    add_test(NAME vfs_synthetic COMMAND vfs_test synth "${CMAKE_CURRENT_BINARY_DIR}")
    # the user's disc, read at run time; skipped (77) when it is absent
    add_test(NAME vfs_disc COMMAND vfs_test disc "${ICO_DISC_IMAGE}")
    set_tests_properties(vfs_disc PROPERTIES SKIP_RETURN_CODE 77)
endif()

# The archive: extraction into
# a temporary archive and mount, compared with the ISO backend. `synth` uses
# vfs_test's synthetic image and runs everywhere; `disc` reads the user's
# image and is skipped (77) without it.
add_executable(archive_test test/archive_test.c ../platform/host_config.c)
target_include_directories(archive_test PRIVATE "${ICO_COMPAT_DIR}"
                           "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(archive_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(archive_test PRIVATE ico_port_data)
if(ICO_RUN_TESTS)
    add_test(NAME archive_synthetic COMMAND archive_test synth "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME archive_disc COMMAND archive_test disc "${ICO_DISC_IMAGE}"
                                       "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(archive_disc PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
endif()

# iso_import_test: the Android first start's copy of the
# chosen disc image (port/platform/android/iso_import.c), on Linux with SDL's
# memory streams for the content:// one: the copy byte for byte with its
# progress, a cancel leaving nothing, a write failure's reason, the space
# arithmetic. The Linux window build only; SDL3 is found after this
# directory (port/rhi), so the target is made at the end of the configure.
function(_ico_data_iso_import_test)
    if(NOT TARGET SDL3::SDL3-shared)
        return()
    endif()
    add_executable(iso_import_test "${CMAKE_SOURCE_DIR}/port/data/test/iso_import_test.c"
                                   "${CMAKE_SOURCE_DIR}/port/platform/android/iso_import.c")
    target_include_directories(iso_import_test PRIVATE "${CMAKE_SOURCE_DIR}/port/data"
                               "${CMAKE_SOURCE_DIR}/port/platform/android")
    target_compile_options(iso_import_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(iso_import_test PRIVATE SDL3::SDL3-shared)
    add_test(NAME iso_import COMMAND iso_import_test "${CMAKE_BINARY_DIR}/port/data")
endfunction()
if(NOT ICO_HEADLESS AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _ico_data_iso_import_test)
endif()

# --- The data tables' runtime loader --
# The game's copy (port/data/tables.c and port/data/gen/) is compiled into
# ico_game by the top-level CMakeLists.txt. Here: its tests.
include("${CMAKE_CURRENT_SOURCE_DIR}/gen/tables.cmake")
set(ICO_DATA_PYTHON "${Python3_EXECUTABLE}")
if(EXISTS "${CMAKE_SOURCE_DIR}/.venv/bin/python")
    set(ICO_DATA_PYTHON "${CMAKE_SOURCE_DIR}/.venv/bin/python")
endif()
if(ICO_DATA_PYTHON)
    # port/data/gen/ must be what the generator writes from the committed
    # schema, headers, symbol lists and manifest. --check never opens the ELF,
    # so this also shows the generated files carry no disc bytes.
    add_test(NAME data_desc_fresh
             COMMAND "${ICO_DATA_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/gen_data_desc.py" --check
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# The reference: the PS2 build's tables, written by tools/gen_data_c.py from
# the user's base ELF (never committed) and compiled under renamed symbols
# (ico_ref_<name>), against which tables_test compares every byte the loader
# writes. Built only where the ELF and pyelftools are present; the game's
# build needs neither.
set(ICO_BASE_ELF "${CMAKE_SOURCE_DIR}/baserom/pal/baseelf.elf" CACHE FILEPATH
    "The PAL base ELF the loader test's reference tables are written from")
set(_ico_have_elftools 1)
if(ICO_DATA_PYTHON)
    execute_process(COMMAND "${ICO_DATA_PYTHON}" -c "import elftools"
                    RESULT_VARIABLE _ico_have_elftools OUTPUT_QUIET ERROR_QUIET)
endif()
if(ICO_DATA_PYTHON AND _ico_have_elftools EQUAL 0 AND EXISTS "${ICO_BASE_ELF}")
    set(_gen "${CMAKE_SOURCE_DIR}/tools/gen_data_c.py")
    set(_deps "${_gen}" "${ICO_BASE_ELF}"
        "${CMAKE_SOURCE_DIR}/config/data_schema.pal.txt"
        "${CMAKE_SOURCE_DIR}/config/data_members.pal.txt"
        "${CMAKE_SOURCE_DIR}/config/symbol_addrs.pal.txt"
        "${CMAKE_SOURCE_DIR}/config/symbol_addrs.pal.data.txt")
    set(_refs "")
    foreach(m ${ICO_DATA_MEMBERS})
        set(out "${CMAKE_CURRENT_BINARY_DIR}/ref/${m}.c")
        add_custom_command(
            OUTPUT "${out}"
            COMMAND "${ICO_DATA_PYTHON}" "${_gen}" --c "${m}" --symbol-map
                    --elf "${ICO_BASE_ELF}" --out "${out}"
            DEPENDS ${_deps}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Generating reference data table ${m}"
            VERBATIM)
        list(APPEND _refs "${out}")
    endforeach()
    set(_renames "")
    foreach(sym ${ICO_TABLE_SYMBOLS})
        list(APPEND _renames "${sym}=ico_ref_${sym}")
    endforeach()
    set_source_files_properties(${_refs} PROPERTIES COMPILE_DEFINITIONS "${_renames}")
    add_executable(tables_test test/tables_test.c tables.c gen/table_defs.c gen/table_desc.c
                               gen/ee_symbols.c ${_refs})
    ico_include_dirs(sugipon _incs)
    target_include_directories(tables_test PRIVATE ${_incs} "${CMAKE_CURRENT_SOURCE_DIR}"
                               "${CMAKE_SOURCE_DIR}")
    target_compile_definitions(tables_test PRIVATE ${ICO_GAME_DEFINITIONS})
    set_source_files_properties(gen/ee_symbols.c TARGET_DIRECTORY tables_test
                                PROPERTIES COMPILE_DEFINITIONS ICO_EE_SYMBOLS_STUBS=1)
    target_compile_options(tables_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                           ${ICO_GAME_WARNINGS})
    target_link_libraries(tables_test PRIVATE ico_port_data)
    if(ICO_RUN_TESTS)
        # through the VFS from the disc image when it is present, else the ELF
        add_test(NAME tables_loader
                 COMMAND tables_test "${ICO_BASE_ELF}" "${ICO_DISC_IMAGE}")
        add_test(NAME tables_manifest
                 COMMAND "${ICO_DATA_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/gen_data_desc.py"
                         --check-manifest --elf "${ICO_BASE_ELF}"
                 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endif()
else()
    message(STATUS "ico: no base ELF or pyelftools: the table loader's reference test is not built")
endif()
