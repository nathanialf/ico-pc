# port/save: the memory card: libmc over a
# host folder (mc_host.c), replacing port/null's empty card. The game's own
# fumi/ios/mcard.c runs on it unchanged but for one ICO_HOST line, the
# [audio] output hook (port/game/options.h ico_opt_output_card).
add_library(ico_save STATIC mc_host.c)
# host_config.h (saves=) and host_loop.h (the vsync callback list)
target_include_directories(ico_save
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${ICO_COMPAT_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(ico_save PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
# iSignalSema and the vsync callback list; ico_host_saves_dir is host_config.c,
# which ico_pc (and the test) compile themselves
# ico_region: the selected disc's card folder (region.h mc_card_suffix)
target_link_libraries(ico_save PUBLIC ico_platform ico_region)

if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_save)
endif()

# ICO's saves (each disc's directory, port/core's mc_save_dir) out of a raw
# card image (.ps2/.bin) or a .psu into a card folder, and its command line,
# tools/mc_import.c. Needs only the UTF-8 file calls.
add_library(ico_save_import STATIC mc_import.c)
target_include_directories(ico_save_import PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_save_import PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_save_import PUBLIC ico_host_fs ico_region)
add_executable(mc_import "${CMAKE_SOURCE_DIR}/tools/mc_import.c")
target_compile_options(mc_import PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(mc_import PRIVATE ico_save_import)

# libmc's calls as mcard.c issues them, the import of synthetic card images
# and .psu files, then mcard.c itself (compiled
# with the game's options; ico_options for its [audio] output hook) on the
# scheduler over the card folder.
add_executable(save_test
    test/save_test.c
    ../platform/host_config.c
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/mcard.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/thread.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/message.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/memory.c")
ico_include_dirs(fumi _ico_save_incs)
target_include_directories(save_test PRIVATE ${_ico_save_incs} "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_definitions(save_test PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(save_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(save_test PRIVATE ico_save ico_save_import ico_platform ico_options)
if(ICO_RUN_TESTS)
    # one run per disc (the region's slug; the importer's command line, run
    # by the test on its synthetic files), each in its own folder, since the
    # test's card folders sit in the working directory: save (the European
    # disc, BESCES-50760ico) and save_jpn (the Japanese disc,
    # BISCPS-11003ico, a 0x1E8-byte product file) and save_kor (the Korean
    # disc, BISCPS-56001ico, the same 0x1E8 record) and save_cn (the Asian disc,
    # BISCPS-55001ico, the same).  save_ntsc and save_aug6 (the North American
    # disc and the August 2001 preview, both BASCUS-97113ico; the preview's in a
    # card folder of its own, memcard-preview, which save_aug6 checks) and save_jun26 (the
    # June 2001 demo, BISCUS-97113ico) have a 0x18C-byte product file of their
    # own layout, which save_test part B lays out byte by byte, and game blocks
    # of 25596 (20480 on the demo) bytes with no option words.
    foreach(_slug pal jpn kor ntsc aug6 jun26 cn)
        if(_slug STREQUAL "pal")
            set(_name save)
        else()
            set(_name save_${_slug})
        endif()
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_name}")
        add_test(NAME ${_name} COMMAND save_test ${_slug} $<TARGET_FILE:mc_import>
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_name}")
    endforeach()
endif()

# The rescue of an old save state's progress as a memory-card save
# (state_recover.h): the side file tools/state_recover.py writes, this
# program's copies of the globals the card code and the save image need, and
# the game's own card code (fumi/ios/mcard.c and mcdata.c, compiled with the
# game's options as save_test compiles them) on the scheduler over a card
# folder.  state_recover_tool is its command line; it stays in every build
# (cmake/IcoTests.cmake ICO_KEEP_IN_ALL), since tools/state_recover.py runs it.
add_library(ico_state_recover STATIC
    state_recover.c
    ../platform/host_config.c
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/mcard.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/mcdata.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/thread.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/message.c"
    "${CMAKE_SOURCE_DIR}/ico2/fumi/ios/memory.c")
ico_include_dirs(fumi _ico_recover_incs)
target_include_directories(ico_state_recover PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                           PRIVATE ${_ico_recover_incs} "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_definitions(ico_state_recover PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(ico_state_recover PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(ico_state_recover PUBLIC ico_save ico_platform ico_options)
add_executable(state_recover_tool tools/state_recover_tool.c)
target_compile_options(state_recover_tool PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(state_recover_tool PRIVATE ico_state_recover)

# state_recover_test (its header lists the cases): unit SLUG on each disc
# (the side file, the globals and their refusals, the card writes through
# mcard.c laid out byte by byte and read back through the game's loader),
# each in its own folder; then, for the European disc, the chain the user
# runs: `files` writes three builds' state files and symbol tables (an ELF
# library inside a package, GNU ld's ELF and PE maps, a functions-only nm
# table), tools/state_recover.py reads each (the package's run also writes
# the card through state_recover_tool; the nm table is refused), and
# `check` compares the side files and the card save with the fixture.
add_executable(state_recover_test test/state_recover_test.c)
target_include_directories(state_recover_test PRIVATE ${_ico_recover_incs}
                           "${CMAKE_SOURCE_DIR}/port/platform" "${CMAKE_SOURCE_DIR}/port/test")
target_compile_definitions(state_recover_test PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(state_recover_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(state_recover_test PRIVATE ico_state_recover)
if(ICO_RUN_TESTS)
    foreach(_slug pal jpn kor ntsc aug6 jun26 cn)
        if(_slug STREQUAL "pal")
            set(_name state_recover)
        else()
            set(_name state_recover_${_slug})
        endif()
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_name}")
        add_test(NAME ${_name} COMMAND state_recover_test unit ${_slug}
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_name}")
    endforeach()
    set(_chain "${CMAKE_CURRENT_BINARY_DIR}/state_recover_chain")
    file(MAKE_DIRECTORY "${_chain}")
    add_test(NAME state_recover_files COMMAND state_recover_test files WORKING_DIRECTORY "${_chain}")
    set_tests_properties(state_recover_files PROPERTIES FIXTURES_SETUP state_recover_files)
    if(Python3_EXECUTABLE)
        set(_recover_py "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/tools/state_recover.py")
        add_test(NAME state_recover_py_apk
                 COMMAND ${_recover_py} state_v1.ics fake.apk --disc pal --side side_apk.rescue
                         --card card --tool $<TARGET_FILE:state_recover_tool>
                 WORKING_DIRECTORY "${_chain}")
        add_test(NAME state_recover_py_map
                 COMMAND ${_recover_py} state_v2.ics ico_pc.map --side side_map.rescue
                 WORKING_DIRECTORY "${_chain}")
        add_test(NAME state_recover_py_win
                 COMMAND ${_recover_py} state_win.ics x64.map --side side_win.rescue
                 WORKING_DIRECTORY "${_chain}")
        set_tests_properties(state_recover_py_apk state_recover_py_map state_recover_py_win
                             PROPERTIES FIXTURES_REQUIRED state_recover_files
                                        FIXTURES_SETUP state_recover_sides)
        add_test(NAME state_recover_py_refuse
                 COMMAND ${_recover_py} state_v1.ics code_only.syms --side side_refused.rescue
                 WORKING_DIRECTORY "${_chain}")
        set_tests_properties(state_recover_py_refuse PROPERTIES
                             FIXTURES_REQUIRED state_recover_files
                             PASS_REGULAR_EXPRESSION "refused: .*names no data symbols")
        add_test(NAME state_recover_check COMMAND state_recover_test check
                 WORKING_DIRECTORY "${_chain}")
        set_tests_properties(state_recover_check PROPERTIES
                             FIXTURES_REQUIRED "state_recover_files;state_recover_sides")
    endif()
endif()
