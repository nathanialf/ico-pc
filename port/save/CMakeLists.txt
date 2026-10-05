# port/save: the memory card (Phase 4D, docs/port/SAVES.md): libmc over a
# host folder (mc_host.c), replacing port/null's empty card. The game's own
# fumi/ios/mcard.c runs on it unchanged.
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

# libmc's calls as mcard.c issues them, then mcard.c itself (compiled
# unchanged, with the game's options) on the scheduler over the card folder.
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
target_link_libraries(save_test PRIVATE ico_save ico_platform)
if(WIN32)
    target_link_libraries(save_test PRIVATE comdlg32 user32)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME save COMMAND save_test)
endif()
