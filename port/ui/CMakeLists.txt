# port/ui: the port's runtime text, layout extension, strings and popups
# (Phase 6, package 6B).
#
#   ico_ui       the game's copy, compiled into ico_pc: ICO_RD in the window
#                build (draws through port/render), measure-only headless;
#                ui_host.c (the game's globals, the decoder flush) in the
#                window build only
#   ico_ui_rd    the same sources with ICO_RD for ui_test (needs ico_render)
#   settings.c, ui_list.c, ui_hint.c   the Settings menu (6C), its list
#                pages and its button prompts, in ico_ui and settings_test: it
#                reads the port's option modules (port/game, port/input,
#                port/config) and the game's layout globals
#
# The font file is embedded as a C array generated at build time
# (embed_font.cmake), so the program needs no file beside it.

set(_ui_font "${CMAKE_SOURCE_DIR}/port/assets/fonts/Arimo-Regular.ttf")
set(_ui_font_c "${CMAKE_CURRENT_BINARY_DIR}/ui_font_data.c")
add_custom_command(
    OUTPUT "${_ui_font_c}"
    COMMAND "${CMAKE_COMMAND}" "-DIN=${_ui_font}" "-DOUT=${_ui_font_c}" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
    DEPENDS "${_ui_font}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
    COMMENT "Embedding the UI font"
    VERBATIM)

set(_ui_sources
    font.c
    game_font_build.c
    glyphs.c
    layout_ext.c
    menu_text.c
    popup.c
    strings.c
    strings_en.c
    strings_fr.c
    strings_de.c
    strings_it.c
    strings_es.c
    "${_ui_font_c}")

# layout_ext.h includes the game's layout_texture.h (LtProp, LtProperty),
# whose typedef.h has unprototyped declarations the port warnings reject
ico_include_dirs(common _ico_ui_game_incs)
set_source_files_properties(layout_ext.c menu_text.c settings.c ui_list.c ui_hint.c
                            gallery.c photo_ui.c game_font_disc.c PROPERTIES COMPILE_OPTIONS
                            "-Wno-strict-prototypes")

function(ico_ui_library name rd)
    add_library(${name} OBJECT ${_ui_sources} ${ARGN})
    target_include_directories(${name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                               PRIVATE "${CMAKE_SOURCE_DIR}/port/third_party/stb"
                                       ${_ico_ui_game_incs})
    target_compile_definitions(${name} PRIVATE ICO_HOST=1)
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
    target_sources(${name} PRIVATE settings.c ui_list.c ui_hint.c gallery.c photo_ui.c)
    foreach(_lib ${_ui_settings_deps})
        target_include_directories(${name} PRIVATE
                                   $<TARGET_PROPERTY:${_lib},INTERFACE_INCLUDE_DIRECTORIES>)
    endforeach()
endfunction()

if(TARGET ico_pc)
    if(NOT ICO_HEADLESS AND TARGET ico_render)
        # AN-G: touch_ui.c, the touch overlay's drawing (reads port/input's
        # touch.h through ico_ui_settings' include path)
        ico_ui_library(ico_ui ON ui_host.c game_font_disc.c touch_ui.c)
        if(TARGET SDL3::SDL3-shared)
            # Q2: ui_host.c posts SDL_EVENT_QUIT for the title's "Quit to
            # desktop" (the headers only; ico_pc links SDL3)
            target_compile_definitions(ico_ui PRIVATE ICO_UI_HAVE_SDL=1)
            target_include_directories(ico_ui PRIVATE
                $<TARGET_PROPERTY:SDL3::SDL3-shared,INTERFACE_INCLUDE_DIRECTORIES>)
        endif()
    else()
        ico_ui_library(ico_ui OFF)
    endif()
    ico_ui_settings(ico_ui)
    # GFONT: game_font_disc.c (the game face from the disc and the archive;
    # main_host.c calls it in the window build) reads port/data's headers;
    # ico_pc links ico_port_data
    if(NOT ICO_HEADLESS AND NOT TARGET ico_render)
        target_sources(ico_ui PRIVATE game_font_disc.c)
    endif()
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
add_executable(settings_test test/settings_test.c $<TARGET_OBJECTS:ico_ui_cpu>
    "${CMAKE_SOURCE_DIR}/ico2/common/src/layout_texture.c"
    "${CMAKE_SOURCE_DIR}/port/game/gamestate.c"
    "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(settings_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           ${_ico_ui_settings_incs} "${CMAKE_SOURCE_DIR}/port/platform"
                           "${CMAKE_SOURCE_DIR}/port/include")
target_compile_definitions(settings_test PRIVATE ICO_HOST=1)
target_compile_options(settings_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(settings_test PRIVATE ${_ui_settings_deps} ico_platform ico_math)
if(NOT WIN32)
    target_link_libraries(settings_test PRIVATE m)
endif()
if(WIN32)
    target_link_libraries(settings_test PRIVATE comdlg32 user32)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME settings COMMAND settings_test "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# --- gallery_test (CPU) --------------------------------------------------------
# The music gallery's list (gallery.c) built from the
# PAL base ELF's tables as the game loads them (port/data/tables.c, as
# tables_test does): keys, group order and headings, duplicates, the asset
# names, the glyphs; with the disc image, the streams on the disc and every
# listed bank in a pack. Exit 77 without the ELF.
set(_gal_data "${CMAKE_SOURCE_DIR}/port/data")
add_executable(gallery_test test/gallery_test.c gallery.c font.c strings.c strings_en.c
    strings_fr.c strings_de.c strings_it.c strings_es.c "${_ui_font_c}"
    "${_gal_data}/tables.c"
    "${_gal_data}/gen/table_defs.c" "${_gal_data}/gen/table_desc.c"
    "${_gal_data}/gen/ee_symbols.c")
ico_include_dirs(sugipon _ico_gal_incs)
target_include_directories(gallery_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" ${_ico_gal_incs}
                           "${_gal_data}" "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/port/audio"
                           "${CMAKE_SOURCE_DIR}/port/third_party/stb")
# CPU only: the window build's ICO_RD would pull rd.h into font.c
set(_gal_defs ${ICO_GAME_DEFINITIONS})
list(REMOVE_ITEM _gal_defs ICO_RD=1)
target_compile_definitions(gallery_test PRIVATE ${_gal_defs} ICO_HOST=1)
set_source_files_properties("${_gal_data}/gen/ee_symbols.c" TARGET_DIRECTORY gallery_test
                            PROPERTIES COMPILE_DEFINITIONS ICO_EE_SYMBOLS_STUBS=1)
target_compile_options(gallery_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(gallery_test PRIVATE ico_port_data)
if(NOT WIN32)
    target_link_libraries(gallery_test PRIVATE m)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    if(NOT DEFINED ICO_DISC_IMAGE)
        set(ICO_DISC_IMAGE "${CMAKE_SOURCE_DIR}/baserom/Ico_PAL.iso" CACHE FILEPATH
            "The PAL disc image (SCES-50760) the disc tests read, never committed")
    endif()
    add_test(NAME gallery COMMAND gallery_test "${ICO_BASE_ELF}" "${ICO_DISC_IMAGE}")
    set_tests_properties(gallery PROPERTIES SKIP_RETURN_CODE 77)
    # the gallery in the headless game: needs the disc image and the
    # headless program
    if(TARGET ico_pc AND ICO_HEADLESS AND Python3_EXECUTABLE)
        # every stream, three effects of every bank and one entry of each
        # group through the gallery in one headless run (gallery_sweep.py,
        # package MUS3; headless_common.py): each stream once to its end with
        # its total, R1 and L1, each group under its name and sounding, the
        # title restored; the table in gallery_sweep/table.md
        add_test(NAME gallery_sweep
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/gallery_sweep.py"
                         "$<TARGET_FILE:ico_pc>" "${ICO_DISC_IMAGE}"
                         "${CMAKE_CURRENT_BINARY_DIR}/gallery_sweep"
                         --report "${CMAKE_CURRENT_BINARY_DIR}/gallery_sweep/table.md")
        set_tests_properties(gallery_sweep PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                             TIMEOUT 3600)
        # Extras > Credits in the headless game (credits_headless.py, package
        # CRED): the ending's staff scenes from the title and back, no save
        add_test(NAME credits_headless
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/credits_headless.py"
                         "$<TARGET_FILE:ico_pc>" "${ICO_DISC_IMAGE}"
                         "${CMAKE_CURRENT_BINARY_DIR}/credits_headless")
        set_tests_properties(credits_headless PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                             TIMEOUT 600)
    endif()
endif()

# model_viewer_test (package MV, port/game/test): the model viewer's table
# (port/game/model_viewer_table.c) against its motion-kind blocks and the
# game's table sizes, and its names and words in the five languages with a
# glyph for every character in the port font. CPU only.
add_executable(model_viewer_test "${CMAKE_SOURCE_DIR}/port/game/test/model_viewer_test.c"
    "${CMAKE_SOURCE_DIR}/port/game/model_viewer_table.c" font.c strings.c strings_en.c
    strings_fr.c strings_de.c strings_it.c strings_es.c "${_ui_font_c}")
target_include_directories(model_viewer_test PRIVATE "${CMAKE_SOURCE_DIR}/port/game"
                           "${CMAKE_SOURCE_DIR}/port/third_party/stb")
# port/ui's "strings.h" by -iquote, so <strings.h> stays the C library's
target_compile_options(model_viewer_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS}
                       -iquote "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT WIN32)
    target_link_libraries(model_viewer_test PRIVATE m)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME model_viewer COMMAND model_viewer_test)
endif()

# model_viewer_cam (package A, port/game/test): the viewer's camera step
# (port/game/model_viewer_cam.c): L2/R2 zoom, the left stick's vertical move,
# the clamps and the dead zone. CPU only.
add_executable(model_viewer_cam_test "${CMAKE_SOURCE_DIR}/port/game/test/model_viewer_cam_test.c"
    "${CMAKE_SOURCE_DIR}/port/game/model_viewer_cam.c")
target_include_directories(model_viewer_cam_test PRIVATE "${CMAKE_SOURCE_DIR}/port/game")
target_compile_options(model_viewer_cam_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
if(NOT WIN32)
    target_link_libraries(model_viewer_cam_test PRIVATE m)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME model_viewer_cam COMMAND model_viewer_cam_test)
endif()

# game_font_test (package GFONT): the game face's builder on a synthetic
# sheet (the letters segmented, a touching pair split, the advances, the
# rim), the blob read back by font.c and the per-character fallback. CPU only.
add_executable(game_font_test test/game_font_test.c font.c game_font_build.c "${_ui_font_c}")
target_include_directories(game_font_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_SOURCE_DIR}/port/third_party/stb")
target_compile_options(game_font_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS}
                       -iquote "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT WIN32)
    target_link_libraries(game_font_test PRIVATE m)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME game_font COMMAND game_font_test)
endif()

if(NOT TARGET ico_render)
    message(STATUS "ico: ui_test not built (needs port/render)")
    return()
endif()

# font.c and the embedded font alone, with ICO_RD: rd_present_test's overlay
# check (package OV, port/render/CMakeLists.txt) draws a glyph through its
# overlay mode; rd_replay_tool (package DEF) installs its deferred text
# renderer; font_edge_test (below) draws both
add_library(ico_ui_font_rd OBJECT font.c "${_ui_font_c}")
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
# the button pictures (glyphs.c) on their own: the shapes of every kind
# against a sink, and the touch overlay's symbols against the old code
add_executable(glyphs_test test/glyphs_test.c glyphs.c $<TARGET_OBJECTS:ico_ui_font_rd>)
target_include_directories(glyphs_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_SOURCE_DIR}/port/third_party/stb")
target_compile_definitions(glyphs_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(glyphs_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(glyphs_test PRIVATE ico_render ico_math)
if(NOT WIN32)
    target_link_libraries(glyphs_test PRIVATE m)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME glyphs COMMAND glyphs_test)
endif()

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
target_link_libraries(ui_test PRIVATE ico_render ico_math)

# settings_render: settings_test.c with SETTINGS_RENDER, the Settings screens
# through the real layout code and rd on a Vulkan device, each screen's SCENE
# written as settings_<screen>.png beside it (exit 77 without a device)
ico_ui_library(ico_ui_settings_rd ON)
ico_ui_settings(ico_ui_settings_rd)
add_executable(settings_render test/settings_test.c $<TARGET_OBJECTS:ico_ui_settings_rd>
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
                      ico_platform)
# menu_text_test (P3, TXT2): the menu text table (against the disc's
# texProperty when the image is there), the strings, the hook through the real
# layout_texture.c (every game row its texture sprite, the port rows
# deferred text) on the recording, then a port row into SCENE on a Vulkan
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
target_link_libraries(menu_text_test PRIVATE ico_render ico_math)
# font_coverage_test: every character of the
# text the port draws (the five languages' tables, the port's roll lines and
# the corpus, test/font_corpus) has a glyph in the embedded subset; with the
# base ELF, the sound names too; with the disc, the game face and its
# per-user file (ui_GameFontPrepare). CPU only. The strings and
# the font are compiled again here (font.c from ico_ui_font_rd) so the test
# needs nothing of the game but the ELF's table loader.
ico_include_dirs(common _ico_cov_incs)
ico_include_dirs(sugipon _ico_cov_data_incs)
add_executable(font_coverage_test test/font_coverage_test.c $<TARGET_OBJECTS:ico_ui_font_rd>
    strings.c strings_en.c strings_fr.c strings_de.c strings_it.c strings_es.c
    "${CMAKE_SOURCE_DIR}/port/game/credits.c" "${CMAKE_SOURCE_DIR}/port/platform/host_config.c")
target_include_directories(font_coverage_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/test" ${_ico_cov_incs} "${CMAKE_SOURCE_DIR}/port/include"
                           "${CMAKE_SOURCE_DIR}/port/game" "${CMAKE_SOURCE_DIR}/port/platform")
set_source_files_properties(test/font_coverage_test.c PROPERTIES COMPILE_OPTIONS
                            "-Wno-strict-prototypes")
target_compile_definitions(font_coverage_test PRIVATE ICO_HOST=1)
target_compile_options(font_coverage_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(font_coverage_test PRIVATE ico_render ico_config ico_platform)
if(WIN32)
    target_link_libraries(font_coverage_test PRIVATE comdlg32 user32)
endif()
set(_ico_cov_elf "")
if(DEFINED ICO_BASE_ELF AND EXISTS "${ICO_BASE_ELF}")
    set(_ico_cov_elf "${ICO_BASE_ELF}")
    target_sources(font_coverage_test PRIVATE
        "${CMAKE_SOURCE_DIR}/port/data/tables.c" "${CMAKE_SOURCE_DIR}/port/data/gen/table_defs.c"
        "${CMAKE_SOURCE_DIR}/port/data/gen/table_desc.c"
        "${CMAKE_SOURCE_DIR}/port/data/gen/ee_symbols.c")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/port/data/gen/ee_symbols.c" PROPERTIES
                                COMPILE_DEFINITIONS ICO_EE_SYMBOLS_STUBS=1)
    # GFONT: the game face built from the disc image (game_font_disc.c over the
    # menu text table), when the image is there
    target_sources(font_coverage_test PRIVATE game_font_build.c game_font_disc.c menu_text.c)
    set_source_files_properties(game_font_disc.c menu_text.c TARGET_DIRECTORY font_coverage_test
                                PROPERTIES COMPILE_OPTIONS "-Wno-strict-prototypes")
    target_include_directories(font_coverage_test PRIVATE ${_ico_cov_data_incs}
                               "${CMAKE_SOURCE_DIR}/port/data" "${CMAKE_SOURCE_DIR}")
    target_compile_definitions(font_coverage_test PRIVATE FONT_COVERAGE_ELF=1
                               ${ICO_GAME_DEFINITIONS})
    target_link_libraries(font_coverage_test PRIVATE ico_port_data)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    if(NOT DEFINED ICO_DISC_IMAGE)
        set(ICO_DISC_IMAGE "${CMAKE_SOURCE_DIR}/baserom/Ico_PAL.iso" CACHE FILEPATH
            "The PAL disc image (SCES-50760) the disc tests read, never committed")
    endif()
    set(_ico_cov_disc "")
    if(_ico_cov_elf AND EXISTS "${ICO_DISC_IMAGE}")
        set(_ico_cov_disc "${ICO_DISC_IMAGE}")
    endif()
    add_test(NAME font_coverage
             COMMAND font_coverage_test "${CMAKE_CURRENT_SOURCE_DIR}/test/font_corpus"
                     ${_ico_cov_elf} ${_ico_cov_disc})
    # GFONT: with the disc, the game face it builds is written beside the
    # tests (gamefont.bin), which settings_render and menu_text then draw with
    # (fixture gamefont)
    set_tests_properties(font_coverage PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_SETUP gamefont
                         ENVIRONMENT "ICO_GAME_FONT_OUT=${CMAKE_CURRENT_BINARY_DIR}/gamefont.bin")
endif()
# font_edge_test (package DEF): a deferred menu row and an overlay line of
# text presented at Enhanced 1920 x 1080 and 3840 x 2160, each glyph edge at
# most one pixel between background and ink; the Original present with a
# deferred row and a popup recorded byte-identical to the plain quads
add_executable(font_edge_test test/font_edge_test.c game_font_build.c
               $<TARGET_OBJECTS:ico_ui_font_rd>)
target_include_directories(font_edge_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(font_edge_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(font_edge_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
target_link_libraries(font_edge_test PRIVATE ico_render)
if(WIN32 AND SDL3_FOUND)
    add_custom_command(TARGET font_edge_test POST_BUILD
                       COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                               "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:font_edge_test>"
                       VERBATIM)
endif()
if(WIN32 AND SDL3_FOUND)
    add_custom_command(TARGET ui_test POST_BUILD
                       COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                               "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:ui_test>"
                       VERBATIM)
endif()
if(NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR
   OR CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    add_test(NAME ui COMMAND ui_test WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(ui PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME settings_render COMMAND settings_render "${CMAKE_CURRENT_BINARY_DIR}"
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(settings_render PROPERTIES SKIP_RETURN_CODE 77
                         FIXTURES_REQUIRED gamefont)
    if(NOT DEFINED ICO_DISC_IMAGE)
        set(ICO_DISC_IMAGE "${CMAKE_SOURCE_DIR}/baserom/Ico_PAL.iso" CACHE FILEPATH
            "The PAL disc image (SCES-50760) the disc tests read, never committed")
    endif()
    add_test(NAME menu_text COMMAND menu_text_test "${ICO_DISC_IMAGE}"
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(menu_text PROPERTIES SKIP_RETURN_CODE 77 FIXTURES_REQUIRED gamefont)
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
