# port/save: the memory card (Phase 4D, docs/port/SAVES.md): libmc over a
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
target_link_libraries(ico_save PUBLIC ico_platform)

if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_save)
endif()

# Package S1: ICO's save out of a raw card image (.ps2/.bin) or a .psu into a
# card folder (docs/port/SAVES.md, "Importing saves"), and its command line,
# tools/mc_import.c. Needs only the UTF-8 file calls.
add_library(ico_save_import STATIC mc_import.c)
target_include_directories(ico_save_import PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_save_import PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_save_import PUBLIC ico_host_fs)
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
if(WIN32)
    target_link_libraries(save_test PRIVATE comdlg32 user32)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    # the importer's command line, run by the test on its synthetic files
    add_test(NAME save COMMAND save_test $<TARGET_FILE:mc_import>)
endif()
