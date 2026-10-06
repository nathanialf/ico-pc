# port/game: the port's gameplay options (Phase 6A, docs/port/OPTIONS.md) and
# achievements (6E, below).
# One module holds stick_fix, yorda_safe and mirror; ico_input, ico_sndn2 and
# the game's hooks (ico2/, under ICO_HOST) read it. The config reader is
# port/config's; host_config.c is compiled by ico_pc and the tests.
# video_options.c: the display options (renderer wave 7, R7a; docs/port/DISPLAY.md)
# photo_mode.c: photo mode's state and camera (package PHOTO; DISPLAY.md
# "Photo mode"); it uses rd.h's RdCamera struct only, so port/render is on
# the include path of the headless build too (nothing of rd is linked)
add_library(ico_options STATIC options.c video_options.c photo_mode.c)
target_include_directories(ico_options PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_SOURCE_DIR}/port/render")
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

# Photo mode's state and camera (package PHOTO): the orbit, the clamps, the
# zoom, the keys, the capture's name. CPU only.
add_executable(photo_test test/photo_test.c ../platform/host_config.c)
target_include_directories(photo_test PRIVATE "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_options(photo_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(photo_test PRIVATE ico_options)
if(NOT WIN32)
    target_link_libraries(photo_test PRIVATE m)
endif()
if(WIN32)
    target_link_libraries(photo_test PRIVATE comdlg32 user32)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME photo COMMAND photo_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# The boot's card check and signs (ico2/common/src/kanbanBoot.c, compiled
# into the test) for a first and a later boot: no reload of stage 1, no
# gsResetFunc, no second card check, and the language and 60 Hz main.c sets
# from the config in force throughout. CPU only.
add_executable(kanban_boot_test test/kanban_boot_test.c ../platform/host_config.c)
ico_include_dirs(common _ico_kanban_boot_incs)
target_include_directories(kanban_boot_test PRIVATE ${_ico_kanban_boot_incs}
    "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_definitions(kanban_boot_test PRIVATE ${ICO_GAME_DEFINITIONS})
target_compile_options(kanban_boot_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(kanban_boot_test PRIVATE ico_config)
if(WIN32)
    target_link_libraries(kanban_boot_test PRIVATE comdlg32 user32)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME kanban_boot COMMAND kanban_boot_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# --- Achievements (Phase 6E, docs/port/ACHIEVEMENTS.md) -----------------------
# ico_gamestate.h (port/include) is the read-only game-state view and the
# signal queue; the game's one-line hooks (ICO_HOST) include it from common,
# fumi, script and sugipon. gamestate.c is built twice: with ICO_GS_LIVE into
# the program (it reads the game's globals and objects, so it takes the game's
# include path and record layout), and without it into the test (a synthetic
# sampler). achievements.c is port code: the table, the checks, the file and
# the popups (port/ui's strings and queue, which ico_pc holds).
# credits.c (package CRED, docs/port/EXTRAS.md "Credits"): the Extras
# credits' flag, lock and start, and the staff roll's port credit lines, in
# the same library (gamestate.c suspends the achievements on its flag);
# credits_live.c, the playback's engine, reads and writes the game's state,
# so it is built into the program with the game's include path, as
# gamestate.c is.
set(_ico_gs_include "${CMAKE_SOURCE_DIR}/port/include")
foreach(_p common fumi script sugipon)
    if(TARGET ico_${_p})
        target_include_directories(ico_${_p} PRIVATE "${_ico_gs_include}")
    endif()
endforeach()

add_library(ico_achievements STATIC achievements.c credits.c)
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
    add_library(ico_credits_live OBJECT credits_live.c)
    target_include_directories(ico_credits_live PRIVATE "${_ico_gs_include}" ${_ico_gs_game_incs})
    target_compile_definitions(ico_credits_live PRIVATE ${ICO_GAME_DEFINITIONS})
    target_compile_options(ico_credits_live PRIVATE ${ICO_SEMANTIC_OPTIONS}
                           ${ICO_GAME_LAYOUT_OPTIONS} ${ICO_PORT_WARNINGS} -Wno-strict-prototypes)
    target_sources(ico_pc PRIVATE $<TARGET_OBJECTS:ico_credits_live>)
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

# --- The model viewer (package MV, docs/port/EXTRAS.md) -----------------------
if(TARGET ico_pc)
    add_library(ico_model_viewer_live OBJECT model_viewer.c model_viewer_table.c)
    ico_include_dirs(sugipon _ico_mv_game_incs)
    target_include_directories(ico_model_viewer_live PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                               ${_ico_mv_game_incs})
    if(NOT ICO_HEADLESS)
        target_include_directories(ico_model_viewer_live PRIVATE "${CMAKE_SOURCE_DIR}/port/render")
    endif()
    target_compile_definitions(ico_model_viewer_live PRIVATE ${ICO_GAME_DEFINITIONS})
    # port/ui's "strings.h" by -iquote, so <strings.h> stays the C library's
    target_compile_options(ico_model_viewer_live PRIVATE ${ICO_SEMANTIC_OPTIONS}
                           ${ICO_GAME_LAYOUT_OPTIONS} ${ICO_PORT_WARNINGS} -Wno-strict-prototypes
                           -iquote "${CMAKE_SOURCE_DIR}/port/ui")
    target_sources(ico_pc PRIVATE $<TARGET_OBJECTS:ico_model_viewer_live>)
endif()
# model_viewer_headless: the headless program from the boot through
# Settings > Extras > Models to the first model and back to the title, its
# log checked (test/model_viewer_headless.cmake; docs/port/TESTING.md). The
# disc image is needed (skipped without it); one game run, so alone.
if(TARGET ico_pc AND ICO_HEADLESS AND (NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
                                       OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME))
    if(NOT DEFINED ICO_DISC_IMAGE)
        set(ICO_DISC_IMAGE "${CMAKE_SOURCE_DIR}/baserom/Ico_PAL.iso" CACHE FILEPATH
            "The PAL disc image (SCES-50760) the disc tests read, never committed")
    endif()
    add_test(NAME model_viewer_headless
             COMMAND "${CMAKE_COMMAND}" "-DICO_PC=$<TARGET_FILE:ico_pc>" "-DISO=${ICO_DISC_IMAGE}"
                     "-DPAD=${CMAKE_CURRENT_SOURCE_DIR}/test/model_viewer_pad.txt"
                     "-DDIR=${CMAKE_CURRENT_BINARY_DIR}/model_viewer_run"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/test/model_viewer_headless.cmake")
    set_tests_properties(model_viewer_headless PROPERTIES RUN_SERIAL TRUE TIMEOUT 300
                         SKIP_REGULAR_EXPRESSION "model_viewer_headless: skipped")
endif()

# credits_test (package CRED; docs/port/EXTRAS.md "Credits", UI.md "Staff
# roll"): the game's staffroll.c over a short table in the disc's forms,
# with the font stubbed to record the posted and printed lines: the port
# credit's lines come after the disc's, the heading then the name in the
# roll's forms, and the roll ends after them; then the lock ([dev]
# unlock_credits through the ini) and a start with no engine; then
# credits_live.c's failure paths over stubs of the game. CPU only.
ico_include_dirs(common _ico_credits_incs)
add_executable(credits_test test/credits_test.c credits.c credits_live.c
    "${CMAKE_SOURCE_DIR}/ico2/common/src/staffroll.c" ../platform/host_config.c)
target_include_directories(credits_test PRIVATE ${_ico_credits_incs} "${_ico_gs_include}"
                           "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/port/platform")
target_compile_definitions(credits_test PRIVATE ICO_HOST=1)
target_compile_options(credits_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(credits_test PRIVATE ico_config ico_platform)
if(WIN32)
    target_link_libraries(credits_test PRIVATE comdlg32 user32)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME credits COMMAND credits_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()
