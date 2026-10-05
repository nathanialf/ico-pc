# port/game: the port's gameplay options (Phase 6A, docs/port/OPTIONS.md) and
# achievements (6E, below).
# One module holds stick_fix, yorda_safe and mirror; ico_input, ico_sndn2 and
# the game's hooks (ico2/, under ICO_HOST) read it. The config reader is
# port/config's; host_config.c is compiled by ico_pc and the tests.
# video_options.c: the display options (renderer wave 7, R7a; docs/port/DISPLAY.md)
add_library(ico_options STATIC options.c video_options.c)
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

# --- Achievements (Phase 6E, docs/port/ACHIEVEMENTS.md) -----------------------
# ico_gamestate.h (port/include) is the read-only game-state view and the
# signal queue; the game's one-line hooks (ICO_HOST) include it from common,
# fumi, script and sugipon. gamestate.c is built twice: with ICO_GS_LIVE into
# the program (it reads the game's globals and objects, so it takes the game's
# include path and record layout), and without it into the test (a synthetic
# sampler). achievements.c is port code: the table, the checks, the file and
# the popups (port/ui's strings and queue, which ico_pc holds).
set(_ico_gs_include "${CMAKE_SOURCE_DIR}/port/include")
foreach(_p common fumi script sugipon)
    if(TARGET ico_${_p})
        target_include_directories(ico_${_p} PRIVATE "${_ico_gs_include}")
    endif()
endforeach()

add_library(ico_achievements STATIC achievements.c)
target_include_directories(ico_achievements
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${_ico_gs_include}"
    PRIVATE "${CMAKE_SOURCE_DIR}/port/platform")
# port/ui's "strings.h" by -iquote, so <strings.h> stays the C library's
target_compile_options(ico_achievements PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS}
                       -iquote "${CMAKE_SOURCE_DIR}/port/ui")
target_link_libraries(ico_achievements PUBLIC ico_options ico_config ico_platform)

if(TARGET ico_pc)
    add_library(ico_gamestate_live OBJECT gamestate.c)
    ico_include_dirs(common _ico_gs_game_incs)
    target_include_directories(ico_gamestate_live PRIVATE "${_ico_gs_include}"
                               "${CMAKE_CURRENT_SOURCE_DIR}" ${_ico_gs_game_incs})
    if(NOT ICO_HEADLESS)
        target_include_directories(ico_gamestate_live PRIVATE "${CMAKE_SOURCE_DIR}/port/render")
    endif()
    target_compile_definitions(ico_gamestate_live PRIVATE ${ICO_GAME_DEFINITIONS} ICO_GS_LIVE=1)
    # typedef.h has the game's unprototyped declarations
    target_compile_options(ico_gamestate_live PRIVATE ${ICO_SEMANTIC_OPTIONS}
                           ${ICO_GAME_LAYOUT_OPTIONS} ${ICO_PORT_WARNINGS} -Wno-strict-prototypes)
    target_sources(ico_pc PRIVATE $<TARGET_OBJECTS:ico_gamestate_live>)
    target_link_libraries(ico_pc PRIVATE ico_achievements)
endif()

# The view over a synthetic state, the signals, every achievement's condition
# through crafted transitions (once, suspended, yorda_safe counting, a version 1 file), the file round
# trip and the popup rate limit. CPU only.
add_executable(achievements_test test/achievements_test.c gamestate.c
    ../ui/strings.c ../ui/strings_en.c ../ui/strings_fr.c ../ui/strings_de.c
    ../ui/strings_it.c ../ui/strings_es.c ../platform/host_config.c)
target_include_directories(achievements_test PRIVATE "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(achievements_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS}
                       -iquote "${CMAKE_SOURCE_DIR}/port/ui")
target_link_libraries(achievements_test PRIVATE ico_achievements)
if(WIN32)
    target_link_libraries(achievements_test PRIVATE comdlg32 user32)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME achievements COMMAND achievements_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()
