# port/data: the disc VFS (vfs.c) and its ISO9660 backend, the libcdvd host
# layer the game's fumi/ios/cdvd.c and seki/src/FileManager.c call, the SIF
# host layer (no IOP reboot, no IRX modules, RPC to host servers) and the
# IOP RAM stand-in. docs/port/DATA.md describes the design.
add_library(ico_port_data STATIC
    cdvd_host.c
    iop_ram.c
    iso9660.c
    sif_host.c
    vfs.c)
# port/platform is searched for package 1B's host_loop.h (the vsync
# callback list cdvd_host.c registers with when it exists).
target_include_directories(ico_port_data
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${ICO_COMPAT_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(ico_port_data PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
# cdvd_host.c registers with ico_platform's host loop. Tests that drive the
# vsync themselves define ico_host_on_vsync_register and pull nothing from it.
target_link_libraries(ico_port_data INTERFACE ico_platform)

if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_port_data)
endif()

add_executable(vfs_test test/vfs_test.c)
target_include_directories(vfs_test PRIVATE "${ICO_COMPAT_DIR}")
target_compile_options(vfs_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(vfs_test PRIVATE ico_port_data)
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    # a synthetic image built by the test: runs everywhere, no disc needed
    add_test(NAME vfs_synthetic COMMAND vfs_test synth "${CMAKE_CURRENT_BINARY_DIR}")
    # the user's disc, read at run time; skipped (77) when it is absent
    set(ICO_DISC_IMAGE "${CMAKE_SOURCE_DIR}/baserom/Ico_PAL.iso" CACHE FILEPATH
        "The PAL disc image (SCES-50760) the disc tests read, never committed")
    add_test(NAME vfs_disc COMMAND vfs_test disc "${ICO_DISC_IMAGE}")
    set_tests_properties(vfs_disc PROPERTIES SKIP_RETURN_CODE 77)
endif()

# --- The data tables' runtime loader (docs/port/DATA.md, "The data tables") --
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
    if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
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
