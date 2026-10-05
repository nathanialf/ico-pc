# port/game: the port's gameplay options (Phase 6A, docs/port/OPTIONS.md).
# One module holds stick_fix, yorda_safe and mirror; ico_input, ico_sndn2 and
# the game's hooks (ico2/, under ICO_HOST) read it. The config reader is
# port/config's; host_config.c is compiled by ico_pc and the tests.
add_library(ico_options STATIC options.c)
target_include_directories(ico_options PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ico_options PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_options PUBLIC ico_config)
if(TARGET ico_pc)
    target_link_libraries(ico_pc PRIVATE ico_options)
endif()

# Defaults are the original behaviour, the config and run-time setters, and
# eBrainGetTarget (ico2/omori/src/ebrain.c, compiled into the test) under the
# yorda_safe hook.
add_executable(options_test test/options_test.c ../platform/host_config.c)
ico_include_dirs(omori _ico_options_incs)
target_include_directories(options_test PRIVATE ${_ico_options_incs}
    "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_definitions(options_test PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(options_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS} -Wno-unused-parameter -Wno-unused-variable
                       -Wno-unused-function)
target_link_libraries(options_test PRIVATE ico_options)
if(WIN32)
    target_link_libraries(options_test PRIVATE comdlg32 user32)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME options COMMAND options_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()
