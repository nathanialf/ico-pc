# port/ui: the port's runtime text, layout extension, strings, popups and
# menus.
#
#   ico_ui              the game's copy, compiled into ico_pc: ICO_RD in the
#                       window build (draws through port/render, with
#                       ui_host.c and touch_ui.c), measure-only headless;
#                       with the Settings menu's sources (ico_ui_settings:
#                       settings.c, ui_list.c, ui_hint.c, gallery.c,
#                       photo_ui.c, ui_mouse.c, ui_mouse_geom.c,
#                       touch_edit_ui.c)
#   ico_gallery_live    gallery_play.c, the music gallery's engine (the
#                       game's sound calls), in ico_pc
#   ico_ui_cpu          the CPU copy with the Settings sources, for the
#                       settings tests
#   ico_ui_rd           the sources with ICO_RD for ui_test and
#                       menu_text_test (needs ico_render)
#   ico_ui_settings_rd  ico_ui_rd with the Settings sources, for
#                       settings_render
#   ico_ui_font_rd      font.c alone with ICO_RD, for the render tests and
#                       rd_replay_tool
#
# The font files (Arimo, Noto Sans JP's subset for the characters Arimo
# has no glyph for: the Japanese text, the language names, the button
# marks, Noto Sans KR's for the Hangul neither has: the Korean text, and
# Noto Sans TC's for the Chinese text's ideographs) are
# embedded as C arrays generated at build time (embed_font.cmake), so the
# program needs no file beside them. _ui_font_srcs: the four, in every
# program that compiles font.c.
#
# The string tables (strings.c and strings_<lang>.c, _ui_string_srcs) read
# the selected disc's pack serial (port/core), so every program that
# compiles them links ico_region.

set(_ui_font "${CMAKE_SOURCE_DIR}/port/assets/fonts/Arimo-Regular.ttf")
set(_ui_font_c "${CMAKE_CURRENT_BINARY_DIR}/ui_font_data.c")
add_custom_command(
    OUTPUT "${_ui_font_c}"
    COMMAND "${CMAKE_COMMAND}" "-DIN=${_ui_font}" "-DOUT=${_ui_font_c}" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
    DEPENDS "${_ui_font}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
    COMMENT "Embedding the UI font"
    VERBATIM)
# the Japanese text's font (Noto Sans JP, the subset tools/subset_font_ja.py
# makes): the characters Arimo has no glyph for (font.c)
set(_ui_font_ja "${CMAKE_SOURCE_DIR}/port/assets/fonts/NotoSansJP-Regular-subset.ttf")
set(_ui_font_ja_c "${CMAKE_CURRENT_BINARY_DIR}/ui_font_ja_data.c")
add_custom_command(
    OUTPUT "${_ui_font_ja_c}"
    COMMAND "${CMAKE_COMMAND}" "-DIN=${_ui_font_ja}" "-DOUT=${_ui_font_ja_c}" -DNAME=ui_font_ja_ttf
            -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
    DEPENDS "${_ui_font_ja}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
    COMMENT "Embedding the UI font for Japanese"
    VERBATIM)
# the Korean text's font (Noto Sans KR, the subset tools/subset_font_ko.py
# makes): the Hangul neither Arimo nor the Japanese font has (font.c)
set(_ui_font_ko "${CMAKE_SOURCE_DIR}/port/assets/fonts/NotoSansKR-Regular-subset.ttf")
set(_ui_font_ko_c "${CMAKE_CURRENT_BINARY_DIR}/ui_font_ko_data.c")
add_custom_command(
    OUTPUT "${_ui_font_ko_c}"
    COMMAND "${CMAKE_COMMAND}" "-DIN=${_ui_font_ko}" "-DOUT=${_ui_font_ko_c}" -DNAME=ui_font_ko_ttf
            -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
    DEPENDS "${_ui_font_ko}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
    COMMENT "Embedding the UI font for Korean"
    VERBATIM)
# the Chinese text's font (Noto Sans TC, the subset tools/subset_font_zh.py
# makes): the ideographs the Japanese font lacks, and all of the Chinese
# text's while the language is Chinese (font.c)
set(_ui_font_zh "${CMAKE_SOURCE_DIR}/port/assets/fonts/NotoSansTC-Regular-subset.ttf")
set(_ui_font_zh_c "${CMAKE_CURRENT_BINARY_DIR}/ui_font_zh_data.c")
add_custom_command(
    OUTPUT "${_ui_font_zh_c}"
    COMMAND "${CMAKE_COMMAND}" "-DIN=${_ui_font_zh}" "-DOUT=${_ui_font_zh_c}" -DNAME=ui_font_zh_ttf
            -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
    DEPENDS "${_ui_font_zh}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
    COMMENT "Embedding the UI font for Chinese"
    VERBATIM)
set(_ui_font_srcs "${_ui_font_c}" "${_ui_font_ja_c}" "${_ui_font_ko_c}" "${_ui_font_zh_c}")
set(_ui_string_srcs strings.c strings_en.c strings_fr.c strings_de.c strings_it.c strings_es.c
                    strings_ja.c strings_ko.c strings_zh.c)

set(_ui_sources
    font.c
    layout_ext.c
    menu_font.c
    menu_text.c
    popup.c
    ${_ui_string_srcs}
    ${_ui_font_srcs})

# layout_ext.h includes the game's layout_texture.h (LtProp, LtProperty),
# whose typedef.h has unprototyped declarations the port warnings reject
ico_include_dirs(common _ico_ui_game_incs)
set_source_files_properties(layout_ext.c menu_font.c menu_text.c popup.c settings.c ui_list.c
                            ui_hint.c gallery.c photo_ui.c ui_mouse.c touch_ui.c ui_fps.c
                            touch_edit_ui.c PROPERTIES
                            COMPILE_OPTIONS "-Wno-strict-prototypes")

function(ico_ui_library name rd)
    add_library(${name} OBJECT ${_ui_sources} ${ARGN})
    target_include_directories(${name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                               PRIVATE "${CMAKE_SOURCE_DIR}/port/third_party/stb"
                                       ${_ico_ui_game_incs})
    target_compile_definitions(${name} PRIVATE ICO_HOST=1)
    # the selected disc's pack serial (strings.c); the programs link it
    target_link_libraries(${name} PUBLIC ico_region)
    # the game's record layout (LtProperty's bit-fields: -mno-ms-bitfields on
    # Windows), since layout_ext.c shares the game's tables
    target_compile_options(${name} PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                                           ${ICO_PORT_WARNINGS})
    if(rd)
        target_compile_definitions(${name} PRIVATE ICO_RD=1)
        target_link_libraries(${name} PUBLIC ico_render)
    endif()
endfunction()

# the game's layout_texture.c reads its tables through layout_ext.h
target_include_directories(ico_common PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# the Settings menu's view of the port's option modules (includes only; the
# programs link the libraries)
set(_ui_settings_deps ico_achievements ico_audio ico_input ico_options ico_config)
function(ico_ui_settings name)
    # the mouse pointer in the menus (ui_mouse.c, its arithmetic
    # in ui_mouse_geom.c; the pointer's state is port/input's pointer.c);
    # the port text's language from [ui] language and the disc (ui_lang.c);
    # the Edit layout page's side of the touch layout editor (touch_edit_ui.c)
    target_sources(${name} PRIVATE settings.c ui_list.c ui_hint.c gallery.c photo_ui.c ui_mouse.c
                                   ui_mouse_geom.c ui_lang.c touch_edit_ui.c)
    foreach(_lib ${_ui_settings_deps})
        target_include_directories(${name} PRIVATE
                                   $<TARGET_PROPERTY:${_lib},INTERFACE_INCLUDE_DIRECTORIES>)
    endforeach()
endfunction()

if(TARGET ico_pc)
    if(NOT ICO_HEADLESS AND TARGET ico_render)
        # touch_ui.c, the touch overlay's drawing (reads port/input's
        # touch.h through ico_ui_settings' include path); ui_fps.c, the
        # Frame rate counter's
        ico_ui_library(ico_ui ON ui_host.c touch_ui.c ui_fps.c)
        if(TARGET SDL3::SDL3-shared)
            # ui_host.c posts SDL_EVENT_QUIT for the title's "Quit to
            # desktop" (the headers only; ico_pc links SDL3)
            target_compile_definitions(ico_ui PRIVATE ICO_UI_HAVE_SDL=1)
            target_include_directories(ico_ui PRIVATE
                $<TARGET_PROPERTY:SDL3::SDL3-shared,INTERFACE_INCLUDE_DIRECTORIES>)
        endif()
    else()
        ico_ui_library(ico_ui OFF)
    endif()
    ico_ui_settings(ico_ui)
    target_include_directories(ico_ui PRIVATE
        $<TARGET_PROPERTY:ico_port_data,INTERFACE_INCLUDE_DIRECTORIES>)
    target_sources(ico_pc PRIVATE $<TARGET_OBJECTS:ico_ui>)
    # the music gallery's engine (gallery_play.c): the game's sound calls, so
    # the game's include path, definitions and record layout
    add_library(ico_gallery_live OBJECT gallery_play.c)
    ico_include_dirs(common _ico_gallery_incs)
    target_include_directories(ico_gallery_live PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                               ${_ico_gallery_incs}
                               $<TARGET_PROPERTY:ico_audio,INTERFACE_INCLUDE_DIRECTORIES>
                               $<TARGET_PROPERTY:ico_port_data,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(ico_gallery_live PRIVATE ${ICO_GAME_DEFINITIONS})
    target_compile_options(ico_gallery_live PRIVATE ${ICO_SEMANTIC_OPTIONS}
                           ${ICO_GAME_LAYOUT_OPTIONS} ${ICO_PORT_WARNINGS} -Wno-strict-prototypes)
    target_sources(ico_pc PRIVATE $<TARGET_OBJECTS:ico_gallery_live>)
    target_include_directories(ico_pc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ico_pc PRIVATE ${_ui_settings_deps})
endif()

# --- settings_test (CPU) ------------------------------------------------------
# The Settings menu built over fake game tables shaped like the PAL ones and
# run by the real layout_texture.c (measure-only text, the gif calls
# stubbed): the rows of each page, value cycling into the setters and the
# texts, the remap capture, the boot-skip mapping over a synthetic config,
# and the navigation repoint.
ico_ui_library(ico_ui_cpu OFF)
ico_ui_settings(ico_ui_cpu)
ico_include_dirs(common _ico_ui_settings_incs)
# ico_settings_test(<name> <main.c>): a settings test program over the shared
# fixture (settings_fixture.c) and sources, and its ctest <name> without the
# _test suffix
function(ico_settings_test name main_c)
    add_executable(${name} ${main_c} test/settings_fixture.c
        $<TARGET_OBJECTS:ico_ui_cpu>
        "${CMAKE_SOURCE_DIR}/ico2/common/src/layout_texture.c"
        "${CMAKE_SOURCE_DIR}/port/game/gamestate.c"
        "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                               ${_ico_ui_settings_incs} "${CMAKE_SOURCE_DIR}/port/platform"
                               "${CMAKE_SOURCE_DIR}/port/include")
    target_compile_definitions(${name} PRIVATE ICO_HOST=1)
    target_compile_options(${name} PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                           ${ICO_GAME_WARNINGS})
    target_link_libraries(${name} PRIVATE ${_ui_settings_deps} ico_platform ico_math)
    if(NOT WIN32)
        target_link_libraries(${name} PRIVATE m)
    endif()
    if(ICO_RUN_TESTS)
        string(REGEX REPLACE "_test$" "" _test "${name}")
        add_test(NAME ${_test} COMMAND ${name} "${CMAKE_CURRENT_BINARY_DIR}")
        # the Japanese and the Korean disc's runs (the fixture's tables at
        # that disc's numbers, from its region row)
        add_test(NAME ${_test}_jpn COMMAND ${name} "${CMAKE_CURRENT_BINARY_DIR}" jpn)
        add_test(NAME ${_test}_kor COMMAND ${name} "${CMAKE_CURRENT_BINARY_DIR}" kor)
        # and the Asian disc's (the Korean program on the Japanese records)
        add_test(NAME ${_test}_cn COMMAND ${name} "${CMAKE_CURRENT_BINARY_DIR}" cn)
    endif()
endfunction()
ico_settings_test(settings_test test/settings_test.c)

# settings_extras_test: the Extras and Characters pages and the pages that
# hide the title's logo
ico_settings_test(settings_extras_test test/settings_extras_test.c)
# settings_states_test: the Save states page, the pause menu's row at pitch 16
# and the Continue screen's Square prompt over a fake host and a fake Continue
# screen; Extras has no Save states row (the other discs' runs: the pause
# rows, Extras and the prompt on their numbers)
ico_settings_test(settings_states_test test/settings_states_test.c)
# settings_extra_test: more Settings menu tests; each settings test file
# stays well under the source size cap (tools/check_no_rom.sh), so new
# settings tests go in settings_extra_test.c
ico_settings_test(settings_extra_test test/settings_extra_test.c)
# settings_discs_test: the rows each disc's Options has, from the title and
# the pause menu (docs/port/GAME_FEATURES.md, Options on each disc): the
# European run and the six others (the three above and the 2001 discs below)
ico_settings_test(settings_discs_test test/settings_discs_test.c)
if(ICO_RUN_TESTS)
    # the three 2001 discs (the North American disc, the August 2001 preview,
    # the June 2001 demo): Extras > Game variants on each (settings_extra_test
    # main; the other runs' checks are the Japanese program's)
    foreach(_slug ntsc aug6 jun26)
        add_test(NAME settings_extra_${_slug}
                 COMMAND settings_extra_test "${CMAKE_CURRENT_BINARY_DIR}" ${_slug})
        add_test(NAME settings_discs_${_slug}
                 COMMAND settings_discs_test "${CMAKE_CURRENT_BINARY_DIR}" ${_slug})
        # settings_test's and settings_extras_test's other-disc runs (the
        # disc's own confirm face, its title and pause rows, its Extras)
        add_test(NAME settings_${_slug} COMMAND settings_test "${CMAKE_CURRENT_BINARY_DIR}" ${_slug})
        add_test(NAME settings_extras_${_slug}
                 COMMAND settings_extras_test "${CMAKE_CURRENT_BINARY_DIR}" ${_slug})
        add_test(NAME settings_states_${_slug}
                 COMMAND settings_states_test "${CMAKE_CURRENT_BINARY_DIR}" ${_slug})
    endforeach()
    # the settings executables write the same settings_test.toml in
    # this directory: never in parallel
    set_tests_properties(settings settings_extras settings_extra settings_jpn settings_extras_jpn
                         settings_extra_jpn settings_kor settings_extras_kor settings_extra_kor
                         settings_cn settings_extras_cn settings_extra_cn settings_extra_ntsc
                         settings_extra_aug6 settings_extra_jun26 settings_discs settings_discs_jpn
                         settings_discs_kor settings_discs_cn settings_discs_ntsc settings_discs_aug6
                         settings_discs_jun26 settings_ntsc settings_aug6 settings_jun26
                         settings_extras_ntsc settings_extras_aug6 settings_extras_jun26
                         settings_states settings_states_jpn settings_states_kor settings_states_cn
                         settings_states_ntsc settings_states_aug6 settings_states_jun26
                         PROPERTIES RESOURCE_LOCK settings_toml)
endif()

# --- gallery_test (CPU) --------------------------------------------------------
# The music gallery's list (gallery.c) built from the
# PAL base ELF's tables as the game loads them (port/data/tables.c, as
# tables_test does): keys, group order and headings, duplicates, the asset
# names, the glyphs; with the disc image, the streams on the disc and every
# listed bank in a pack. Exit 77 without the ELF.
set(_gal_data "${CMAKE_SOURCE_DIR}/port/data")
add_executable(gallery_test test/gallery_test.c gallery.c font.c ${_ui_string_srcs}
    ${_ui_font_srcs} "${_gal_data}/tables.c" ${ICO_TABLE_SOURCES})
ico_include_dirs(sugipon _ico_gal_incs)
target_include_directories(gallery_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" ${_ico_gal_incs}
                           "${_gal_data}" "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/port/audio"
                           "${CMAKE_SOURCE_DIR}/port/third_party/stb")
# CPU only: the window build's ICO_RD would pull rd.h into font.c
set(_gal_defs ${ICO_GAME_DEFINITIONS})
list(REMOVE_ITEM _gal_defs ICO_RD=1)
target_compile_definitions(gallery_test PRIVATE ${_gal_defs} ICO_HOST=1)
set_source_files_properties(${ICO_TABLE_EE_SYMBOLS} TARGET_DIRECTORY gallery_test
                            PROPERTIES COMPILE_DEFINITIONS ICO_EE_SYMBOLS_STUBS=1)
target_compile_options(gallery_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(gallery_test PRIVATE ico_port_data ico_region)
if(NOT WIN32)
    target_link_libraries(gallery_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME gallery COMMAND gallery_test "${ICO_BASE_ELF}" "${ICO_DISC_IMAGE}")
    set_tests_properties(gallery PROPERTIES SKIP_RETURN_CODE 77)
    # every disc's stream paths equal row for row (the names are keyed by the
    # row), then the list from the Japanese and the Korean disc's tables and
    # image
    add_test(NAME gallery_jpn COMMAND gallery_test --region jpn "${ICO_BASE_ELF}"
                                      "${ICO_BASE_ELF_JPN}" "${ICO_DISC_IMAGE_JPN}")
    add_test(NAME gallery_kor COMMAND gallery_test --region kor "${ICO_BASE_ELF}"
                                      "${ICO_BASE_ELF_KOR}" "${ICO_DISC_IMAGE_KOR}")
    add_test(NAME gallery_cn COMMAND gallery_test --region cn "${ICO_BASE_ELF}"
                                      "${ICO_BASE_ELF_CN}" "${ICO_DISC_IMAGE_CN}")
    set_tests_properties(gallery_jpn gallery_kor gallery_cn PROPERTIES SKIP_RETURN_CODE 77)
    # the gallery in the headless game: needs the disc image and the
    # headless program
    if(TARGET ico_pc AND ICO_HEADLESS AND Python3_EXECUTABLE)
        # every stream, three effects of every bank and one entry of each
        # group through the gallery in one headless run (gallery_sweep.py,
        # with headless_common.py): each stream once to its end with
        # its total, R1 and L1, each group under its name and sounding, the
        # title restored; the table in gallery_sweep/table.md
        add_test(NAME gallery_sweep
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/gallery_sweep.py"
                         "$<TARGET_FILE:ico_pc>" "${ICO_DISC_IMAGE}"
                         "${CMAKE_CURRENT_BINARY_DIR}/gallery_sweep"
                         --report "${CMAKE_CURRENT_BINARY_DIR}/gallery_sweep/table.md")
        set_tests_properties(gallery_sweep PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                             TIMEOUT 3600)
        # Extras > Credits in the headless game (credits_headless.py): the
        # ending's staff scenes from the title and back, no save
        add_test(NAME credits_headless
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/credits_headless.py"
                         "$<TARGET_FILE:ico_pc>" "${ICO_DISC_IMAGE}"
                         "${CMAKE_CURRENT_BINARY_DIR}/credits_headless")
        set_tests_properties(credits_headless PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                             TIMEOUT 600)
    endif()
endif()

# model_viewer_test (port/game/test): the model viewer's table
# (port/game/model_viewer_table.c) against its motion-kind blocks and the
# game's table sizes, and its names and words in the six languages with a
# glyph for every character in the port font. CPU only.
add_executable(model_viewer_test "${CMAKE_SOURCE_DIR}/port/game/test/model_viewer_test.c"
    "${CMAKE_SOURCE_DIR}/port/game/model_viewer_table.c" font.c ${_ui_string_srcs}
    ${_ui_font_srcs})
target_include_directories(model_viewer_test PRIVATE "${CMAKE_SOURCE_DIR}/port/game"
                           "${CMAKE_SOURCE_DIR}/port/third_party/stb")
# port/ui's "strings.h" by -iquote, so <strings.h> stays the C library's
target_compile_options(model_viewer_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS}
                       -iquote "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(model_viewer_test PRIVATE ico_region)
if(NOT WIN32)
    target_link_libraries(model_viewer_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME model_viewer COMMAND model_viewer_test)
    # the table's Japanese model column against the PAL ELF's and each other
    # disc's modelData (tools/gen_mv_region.py --check; the Korean disc reads
    # the Japanese column; tools/gen_data_c.py reads the ELFs, so pyelftools:
    # the repository's .venv when there is one)
    set(_mv_py "${Python3_EXECUTABLE}")
    if(EXISTS "${CMAKE_SOURCE_DIR}/.venv/bin/python")
        set(_mv_py "${CMAKE_SOURCE_DIR}/.venv/bin/python")
    endif()
    if(_mv_py)
        foreach(_slug jpn kor cn)
            string(TOUPPER "${_slug}" _up)
            add_test(NAME model_viewer_${_slug}
                     COMMAND "${_mv_py}" "${CMAKE_SOURCE_DIR}/tools/gen_mv_region.py" --check
                             --region ${_slug} --pal-elf "${ICO_BASE_ELF}"
                             --elf "${ICO_BASE_ELF_${_up}}"
                     WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
            set_tests_properties(model_viewer_${_slug} PROPERTIES SKIP_RETURN_CODE 77)
        endforeach()
    endif()
endif()

# model_viewer_cam (port/game/test): the viewer's camera step
# (port/game/model_viewer_cam.c): L2/R2 zoom, the left stick's vertical move,
# the clamps and the dead zone. CPU only.
add_executable(model_viewer_cam_test "${CMAKE_SOURCE_DIR}/port/game/test/model_viewer_cam_test.c"
    "${CMAKE_SOURCE_DIR}/port/game/model_viewer_cam.c")
target_include_directories(model_viewer_cam_test PRIVATE "${CMAKE_SOURCE_DIR}/port/game")
target_compile_options(model_viewer_cam_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(model_viewer_cam_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME model_viewer_cam COMMAND model_viewer_cam_test)
endif()

# strings_complete: every language's table defines every UiStrId with the
# English entry's printf conversions (ui_str_in would fall back to English
# without a word), the serial notes with each disc's serial (ui_str_serial),
# the sheet language, the port text's language from [ui] language and the
# disc (ui_lang.c, its two inputs stubbed), the Japanese line-break rules.
# CPU only, no game data.
add_executable(strings_complete_test test/strings_complete_test.c ui_lang.c ${_ui_string_srcs})
# port/ui's "strings.h" and port/game's "options.h" by -iquote, so
# <strings.h> stays the C library's
target_compile_options(strings_complete_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS}
                       "SHELL:-iquote ${CMAKE_CURRENT_SOURCE_DIR}"
                       "SHELL:-iquote ${CMAKE_SOURCE_DIR}/port/game")
target_link_libraries(strings_complete_test PRIVATE ico_region)
if(ICO_RUN_TESTS)
    add_test(NAME strings_complete COMMAND strings_complete_test)
endif()

# model_viewer_effects (port/game/test, issue 104): a motion's effects with
# no stage animation loaded. frameDependSequence.c and stageMultiBgaManager.c
# compiled in, every call they make a stub, stage_MakePlayBgAnimation
# returning no node: the guard of the stage animation entries (no write, the
# slot kept, its log line once), and every motion of every Extras > Models
# model stepped through ExecFrameDependSequence in the game (each stage
# animation skipped) and in the viewer (no stage animation or particle, the
# sounds and vibration the game's). model_viewer_effects runs on synthetic
# motion tables; model_viewer_effects_elf on the PAL base ELF's (the table
# loader, port/data/tables.c), where BOY SWIM must reach the guard, and exits
# 77 without the ELF. CPU only. Issue 136: model_viewer_motion.c too, the
# viewer's list rule (a node-blend motion needs its partners loaded), checked
# in the ELF run.
set(_mve_data "${CMAKE_SOURCE_DIR}/port/data")
add_executable(model_viewer_effects_test
    "${CMAKE_SOURCE_DIR}/port/game/test/model_viewer_effects_test.c"
    "${CMAKE_SOURCE_DIR}/port/game/model_viewer_table.c"
    "${CMAKE_SOURCE_DIR}/port/game/model_viewer_motion.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/frameDependSequence.c"
    "${CMAKE_SOURCE_DIR}/ico2/sugipon/src/stageMultiBgaManager.c"
    "${_mve_data}/tables.c" ${ICO_TABLE_SOURCES})
ico_include_dirs(sugipon _ico_mve_incs)
target_include_directories(model_viewer_effects_test PRIVATE ${_ico_mve_incs}
                           "${CMAKE_SOURCE_DIR}/port/game" "${_mve_data}" "${CMAKE_SOURCE_DIR}")
# CPU only, as gallery_test: no ICO_RD
set(_mve_defs ${ICO_GAME_DEFINITIONS})
list(REMOVE_ITEM _mve_defs ICO_RD=1)
target_compile_definitions(model_viewer_effects_test PRIVATE ${_mve_defs} ICO_HOST=1)
set_source_files_properties(${ICO_TABLE_EE_SYMBOLS} TARGET_DIRECTORY
                            model_viewer_effects_test PROPERTIES COMPILE_DEFINITIONS
                            ICO_EE_SYMBOLS_STUBS=1)
# port/ui's "strings.h" (model_viewer_table.c) by -iquote, so <strings.h>
# stays the C library's
target_compile_options(model_viewer_effects_test PRIVATE ${ICO_SEMANTIC_OPTIONS}
                       ${ICO_GAME_LAYOUT_OPTIONS} ${ICO_GAME_WARNINGS}
                       -iquote "${CMAKE_CURRENT_SOURCE_DIR}")
# ico_math: newlib's rand (crt_random_unit)
target_link_libraries(model_viewer_effects_test PRIVATE ico_math ico_port_data)
if(NOT WIN32)
    target_link_libraries(model_viewer_effects_test PRIVATE m)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME model_viewer_effects
             COMMAND model_viewer_effects_test "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME model_viewer_effects_elf
             COMMAND model_viewer_effects_test "${CMAKE_CURRENT_BINARY_DIR}" "${ICO_BASE_ELF}")
    set_tests_properties(model_viewer_effects_elf PROPERTIES SKIP_RETURN_CODE 77)
endif()

# ui_mouse_test: the mouse pointer's arithmetic in the menus
# (ui_mouse_geom.c): the pointer into the layouts' grid (4:3 and a
# pillarboxed 1920 x 1080), the rows' boxes and the hit test. CPU only.
add_executable(ui_mouse_test test/ui_mouse_test.c ui_mouse_geom.c)
target_include_directories(ui_mouse_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ui_mouse_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(ICO_RUN_TESTS)
    add_test(NAME ui_mouse COMMAND ui_mouse_test)
endif()

if(NOT TARGET ico_render)
    message(STATUS "ico: ui_test not built (needs port/render)")
    return()
endif()

# font.c and the embedded font alone, with ICO_RD: rd_present_test's overlay
# check (port/render/CMakeLists.txt) draws a glyph through its
# overlay mode; rd_replay_tool's overlay test draws text through it;
# font_edge_test (below) draws both
add_library(ico_ui_font_rd OBJECT font.c ${_ui_font_srcs})
target_include_directories(ico_ui_font_rd PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                           PRIVATE "${CMAKE_SOURCE_DIR}/port/third_party/stb")
target_compile_definitions(ico_ui_font_rd PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(ico_ui_font_rd PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(ico_ui_font_rd PUBLIC ico_render)
foreach(_t rd_present_test rd_replay_tool)
    if(TARGET ${_t})
        target_sources(${_t} PRIVATE $<TARGET_OBJECTS:ico_ui_font_rd>)
        target_include_directories(${_t} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endforeach()

# --- Tests ----------------------------------------------------------------------
# ui_test: the atlas and glyph metrics, UTF-8, measuring, the string tables,
# the layout extension through the real layout_texture.c (with GifPacket.c,
# DisplayList.c, DmaPacket.c as the window build has them, the rest of the
# game stubbed) on the recording, then on a Vulkan device (77 without one)
# "ICO" and an accented word drawn through rd: coverage inside the expected
# bounds and the blend of a known colour on a known background.
ico_ui_library(ico_ui_rd ON)
ico_include_dirs(seki _ico_ui_seki_incs)
add_executable(ui_test test/ui_test.c $<TARGET_OBJECTS:ico_ui_rd>
    "${CMAKE_SOURCE_DIR}/ico2/common/src/layout_texture.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(ui_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" ${_ico_ui_game_incs}
                           ${_ico_ui_seki_incs} "${CMAKE_SOURCE_DIR}/port/shaders"
                           "${CMAKE_SOURCE_DIR}/port/math" "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(ui_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(ui_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
# the menus' sheet strips checked against
# font_sheet_ps's CPU reference (port/render/test/sheet_ref.h)
target_link_libraries(ui_test PRIVATE ico_render ico_math ico_sheet_ref ico_region)

# settings_render: settings_photo_test.c with SETTINGS_RENDER, the Settings
# screens through the real layout code and rd on a Vulkan device, each
# screen's SCENE written as settings_<screen>.png beside it (exit 77 without a
# device)
ico_ui_library(ico_ui_settings_rd ON)
ico_ui_settings(ico_ui_settings_rd)
add_executable(settings_render test/settings_photo_test.c test/settings_fixture.c
    $<TARGET_OBJECTS:ico_ui_settings_rd>
    "${CMAKE_SOURCE_DIR}/ico2/common/src/layout_texture.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c"
    "${CMAKE_SOURCE_DIR}/port/game/gamestate.c"
    "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(settings_render PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" ${_ico_ui_game_incs}
                           ${_ico_ui_seki_incs} "${CMAKE_SOURCE_DIR}/port/shaders"
                           "${CMAKE_SOURCE_DIR}/port/math" "${CMAKE_SOURCE_DIR}/port/rhi"
                           "${CMAKE_SOURCE_DIR}/port/platform" "${CMAKE_SOURCE_DIR}/port/include")
target_compile_definitions(settings_render PRIVATE ICO_HOST=1 ICO_RD=1 SETTINGS_RENDER=1)
target_compile_options(settings_render PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(settings_render PRIVATE ico_render ico_math ${_ui_settings_deps}
                      ico_platform ico_region)
# menu_text_test: the menu text table (against the disc's
# texProperty when the image is there), the strings, the hook through the real
# layout_texture.c (the table's rows a text strip, the others their texture
# sprite) on the recording, the page cache's shelf reclaim under a string
# that changes every frame (issue 136), then a port row into SCENE on a Vulkan
# device (exit 77 without one)
add_executable(menu_text_test test/menu_text_test.c $<TARGET_OBJECTS:ico_ui_rd>
    "${CMAKE_SOURCE_DIR}/ico2/common/src/layout_texture.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(menu_text_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/test" ${_ico_ui_game_incs}
                           ${_ico_ui_seki_incs} "${CMAKE_SOURCE_DIR}/port/shaders"
                           "${CMAKE_SOURCE_DIR}/port/math" "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(menu_text_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(menu_text_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(menu_text_test PRIVATE ico_render ico_math ico_region)
# font_coverage_test: every character of the
# text the port draws (the six languages' tables, the port's roll lines and
# the corpus, test/font_corpus) has a glyph in one of the embedded subsets
# (Arimo, Noto Sans JP), a Japanese character the second's own; with the
# base ELF, the sound names too. CPU only. The strings and
# the font are compiled again here (font.c from ico_ui_font_rd) so the test
# needs nothing of the game but the ELF's table loader.
ico_include_dirs(common _ico_cov_incs)
ico_include_dirs(sugipon _ico_cov_data_incs)
add_executable(font_coverage_test test/font_coverage_test.c $<TARGET_OBJECTS:ico_ui_font_rd>
    ${_ui_string_srcs}
    "${CMAKE_SOURCE_DIR}/port/game/credits.c" "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(font_coverage_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/test" ${_ico_cov_incs} "${CMAKE_SOURCE_DIR}/port/include"
                           "${CMAKE_SOURCE_DIR}/port/game" "${CMAKE_SOURCE_DIR}/port/platform")
set_source_files_properties(test/font_coverage_test.c PROPERTIES COMPILE_OPTIONS
                            "-Wno-strict-prototypes")
target_compile_definitions(font_coverage_test PRIVATE ICO_HOST=1)
target_compile_options(font_coverage_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(font_coverage_test PRIVATE ico_render ico_config ico_platform ico_region)
set(_ico_cov_elf "")
if(DEFINED ICO_BASE_ELF AND EXISTS "${ICO_BASE_ELF}")
    set(_ico_cov_elf "${ICO_BASE_ELF}")
    target_sources(font_coverage_test PRIVATE
        "${CMAKE_SOURCE_DIR}/port/data/tables.c" ${ICO_TABLE_SOURCES})
    set_source_files_properties(${ICO_TABLE_EE_SYMBOLS} PROPERTIES
                                COMPILE_DEFINITIONS ICO_EE_SYMBOLS_STUBS=1)
    target_include_directories(font_coverage_test PRIVATE ${_ico_cov_data_incs}
                               "${CMAKE_SOURCE_DIR}/port/data" "${CMAKE_SOURCE_DIR}")
    target_compile_definitions(font_coverage_test PRIVATE FONT_COVERAGE_ELF=1
                               ${ICO_GAME_DEFINITIONS})
    target_link_libraries(font_coverage_test PRIVATE ico_port_data)
endif()
if(ICO_RUN_TESTS)
    add_test(NAME font_coverage
             COMMAND font_coverage_test "${CMAKE_CURRENT_SOURCE_DIR}/test/font_corpus"
                     ${_ico_cov_elf})
    set_tests_properties(font_coverage PROPERTIES SKIP_RETURN_CODE 77)
endif()
# menu_look: the menus' Arimo text against the game's
# menu sheets: the sheets read from the disc (test/menu_sheets.c, a TIM2
# reader), the item strips (menu_font.c
# ui__menu_strip_raster) through the shader's CPU reference (ico_sheet_ref),
# compared per item in the five languages, with the survey that pinned the
# sheet constants.  Needs the base ELF to configure, and the disc image to
# run (exit 77 without it, so a worktree skips it); writes its pictures to
# <build>/menu_look/<lang>/.  ICO_MENU_LOOK_FIT=1 runs the constants' search.
if(_ico_cov_elf AND TARGET ico_sheet_ref)
    add_executable(menu_look_test test/menu_look_test.c test/menu_sheets.c menu_font.c menu_text.c
        ${_ui_string_srcs} $<TARGET_OBJECTS:ico_ui_font_rd>
        "${CMAKE_SOURCE_DIR}/port/data/tables.c" ${ICO_TABLE_SOURCES})
    # ee_symbols.c's ICO_EE_SYMBOLS_STUBS and menu_font.c/menu_text.c's
    # -Wno-strict-prototypes are set above, in this directory
    set_source_files_properties(test/menu_look_test.c test/menu_sheets.c
                                PROPERTIES COMPILE_OPTIONS "-Wno-strict-prototypes")
    target_include_directories(menu_look_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                               "${CMAKE_CURRENT_SOURCE_DIR}/test" ${_ico_cov_incs}
                               ${_ico_cov_data_incs} "${CMAKE_SOURCE_DIR}/port/data"
                               "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/port/include"
                               "${CMAKE_SOURCE_DIR}/port/platform" "${CMAKE_SOURCE_DIR}/port/render"
                               "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/shaders"
                               "${CMAKE_SOURCE_DIR}/port/math")
    target_compile_definitions(menu_look_test PRIVATE ICO_HOST=1 ${ICO_GAME_DEFINITIONS})
    target_compile_options(menu_look_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                           ${ICO_GAME_WARNINGS})
    target_link_libraries(menu_look_test PRIVATE ico_render ico_sheet_ref ico_port_data ico_platform
                          ico_region)
    if(NOT WIN32)
        target_link_libraries(menu_look_test PRIVATE m)
    endif()
    if(ICO_RUN_TESTS)
        add_test(NAME menu_look COMMAND menu_look_test "${_ico_cov_elf}" "${ICO_DISC_IMAGE}"
                 "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(menu_look PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
# menu_region_test: the Japanese and Korean discs' menus as the port reads
# them, over the disc's boot ELF's tables (the loader's set of that disc):
# text, the layouts the region table names run their procedures, the title,
# pause and memory card rows and the glyph rows are where
# port/core/region_<slug>.c says, and no row of the disc is drawn from the
# menu text table; look, with the disc image: each glyph rectangle holds a
# picture, the sheets' light ink measured and held to menu_font.c's
# kSheetInk[the row's menu_sheet] (UI_SHEET_JA, UI_SHEET_KO), and the port's
# words' geometry held to the disc's. Exit 77 without the ELF (or, for look,
# the disc).
if((ICO_BASE_ELF_JPN OR ICO_BASE_ELF_KOR) AND TARGET ico_sheet_ref)
    add_executable(menu_region_test test/menu_region_test.c test/menu_sheets.c menu_font.c menu_text.c
        ${_ui_string_srcs} $<TARGET_OBJECTS:ico_ui_font_rd>
        "${CMAKE_SOURCE_DIR}/port/data/tables.c" ${ICO_TABLE_SOURCES})
    set_source_files_properties(test/menu_region_test.c PROPERTIES COMPILE_OPTIONS
                                "-Wno-strict-prototypes")
    target_include_directories(menu_region_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                               "${CMAKE_CURRENT_SOURCE_DIR}/test" ${_ico_cov_incs}
                               ${_ico_cov_data_incs} "${CMAKE_SOURCE_DIR}/port/data"
                               "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/port/include"
                               "${CMAKE_SOURCE_DIR}/port/platform" "${CMAKE_SOURCE_DIR}/port/render"
                               "${CMAKE_SOURCE_DIR}/port/rhi" "${CMAKE_SOURCE_DIR}/port/shaders"
                               "${CMAKE_SOURCE_DIR}/port/math")
    target_compile_definitions(menu_region_test PRIVATE ICO_HOST=1 ${ICO_GAME_DEFINITIONS})
    target_compile_options(menu_region_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                           ${ICO_GAME_WARNINGS})
    target_link_libraries(menu_region_test PRIVATE ico_render ico_sheet_ref ico_port_data
                          ico_platform ico_region)
    if(NOT WIN32)
        target_link_libraries(menu_region_test PRIVATE m)
    endif()
    if(ICO_RUN_TESTS)
        add_test(NAME menu_text_jpn COMMAND menu_region_test text jpn "${ICO_BASE_ELF_JPN}")
        add_test(NAME menu_look_jpn COMMAND menu_region_test look jpn "${ICO_BASE_ELF_JPN}"
                 "${ICO_DISC_IMAGE_JPN}")
        add_test(NAME menu_text_kor COMMAND menu_region_test text kor "${ICO_BASE_ELF_KOR}")
        add_test(NAME menu_look_kor COMMAND menu_region_test look kor "${ICO_BASE_ELF_KOR}"
                 "${ICO_DISC_IMAGE_KOR}")
        add_test(NAME menu_text_cn COMMAND menu_region_test text cn "${ICO_BASE_ELF_CN}")
        add_test(NAME menu_look_cn COMMAND menu_region_test look cn "${ICO_BASE_ELF_CN}"
                 "${ICO_DISC_IMAGE_CN}")
        set_tests_properties(menu_text_jpn menu_look_jpn menu_text_kor menu_look_kor menu_text_cn
                             menu_look_cn PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
# font_edge_test: an overlay line of text presented at Enhanced
# 1920 x 1080 and 3840 x 2160, each glyph edge at most one pixel between
# background and ink, and the mirror mode
add_executable(font_edge_test test/font_edge_test.c $<TARGET_OBJECTS:ico_ui_font_rd>)
target_include_directories(font_edge_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(font_edge_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(font_edge_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(font_edge_test PRIVATE ico_render)
ico_copy_sdl3_dll(font_edge_test ui_test)
if(ICO_RUN_TESTS)
    add_test(NAME ui COMMAND ui_test WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(ui PROPERTIES SKIP_RETURN_CODE 77)
    # the glyphs and the pad convention on the Japanese and Korean discs (CPU)
    add_test(NAME ui_jpn COMMAND ui_test jpn WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME ui_kor COMMAND ui_test kor WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME ui_cn COMMAND ui_test cn WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME settings_render COMMAND settings_render "${CMAKE_CURRENT_BINARY_DIR}"
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(settings_render PROPERTIES RESOURCE_LOCK settings_toml)
    set_tests_properties(settings_render PROPERTIES SKIP_RETURN_CODE 77)
    # the Japanese and Korean discs' title and main page (the fixture at their numbers)
    add_test(NAME settings_render_jpn COMMAND settings_render "${CMAKE_CURRENT_BINARY_DIR}" jpn
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME settings_render_kor COMMAND settings_render "${CMAKE_CURRENT_BINARY_DIR}" kor
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME settings_render_cn COMMAND settings_render "${CMAKE_CURRENT_BINARY_DIR}" cn
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # and the three 2001 discs' (Cross confirms, Circle goes back to the title)
    foreach(_slug ntsc aug6 jun26)
        add_test(NAME settings_render_${_slug}
                 COMMAND settings_render "${CMAKE_CURRENT_BINARY_DIR}" ${_slug}
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endforeach()
    set_tests_properties(settings_render_jpn settings_render_kor settings_render_cn
                         settings_render_ntsc settings_render_aug6 settings_render_jun26 PROPERTIES
                         RESOURCE_LOCK settings_toml SKIP_RETURN_CODE 77)
    add_test(NAME menu_text COMMAND menu_text_test "${ICO_DISC_IMAGE}"
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(menu_text PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME font_edge COMMAND font_edge_test "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(font_edge PROPERTIES SKIP_RETURN_CODE 77)
    set(_ico_vvl "${ICO_DEPS_DIR}/vulkan-validation")
    set(_env "")
    if(NOT WIN32 AND EXISTS "${_ico_vvl}/VkLayer_khronos_validation.json")
        set(_env "VK_ADD_LAYER_PATH=${_ico_vvl}" "ICO_VK_VALIDATION=1")
    endif()
    # the Vulkan driver's leak at vkDestroyInstance (port/rhi/CMakeLists.txt)
    set(_asan_env "")
    if(ICO_SANITIZE MATCHES "address")
        set(_asan_env "ASAN_OPTIONS=detect_leaks=0")
        set_tests_properties(settings_render PROPERTIES ENVIRONMENT "${_asan_env}")
    endif()
    list(APPEND _env ${_asan_env})
    if(_env)
        set_tests_properties(ui menu_text font_edge PROPERTIES ENVIRONMENT "${_env}")
    endif()
endif()
