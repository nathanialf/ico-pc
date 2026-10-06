# port/ui: the port's runtime text, layout extension, strings and popups
# (Phase 6, package 6B; docs/port/UI.md).
#
#   ico_ui       the game's copy, compiled into ico_pc: ICO_RD in the window
#                build (draws through port/render), measure-only headless;
#                ui_host.c (the game's globals, the decoder flush) in the
#                window build only
#   ico_ui_rd    the same sources with ICO_RD for ui_test (needs ico_render)
#   settings.c, ui_list.c   the Settings menu (6C) and its list pages, in ico_ui and settings_test: it
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
set_source_files_properties(layout_ext.c menu_text.c settings.c ui_list.c PROPERTIES COMPILE_OPTIONS
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
    target_sources(${name} PRIVATE settings.c ui_list.c)
    foreach(_lib ${_ui_settings_deps})
        target_include_directories(${name} PRIVATE
                                   $<TARGET_PROPERTY:${_lib},INTERFACE_INCLUDE_DIRECTORIES>)
    endforeach()
endfunction()

if(TARGET ico_pc)
    if(NOT ICO_HEADLESS AND TARGET ico_render)
        ico_ui_library(ico_ui ON ui_host.c)
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
    target_sources(ico_pc PRIVATE $<TARGET_OBJECTS:ico_ui>)
    target_include_directories(ico_pc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ico_pc PRIVATE ${_ui_settings_deps})
endif()

# --- settings_test (CPU) ------------------------------------------------------
# The Settings menu built over fake game tables shaped like the PAL ones and
# run by the real layout_texture.c (measure-only text, the gif calls
# stubbed): the rows of each page, value cycling into the setters and the
# texts, the remap capture, the boot-skip mapping over a synthetic config,
# and the navigation repoint. docs/port/UI.md, "Settings menu".
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

if(NOT TARGET ico_render)
    message(STATUS "ico: ui_test not built (needs port/render)")
    return()
endif()

# rd_present_test's overlay check (package OV, port/render/CMakeLists.txt)
# draws a glyph through font.c's overlay mode: font.c and the embedded font
# alone, with ICO_RD
if(TARGET rd_present_test)
    add_library(ico_ui_font_rd OBJECT font.c "${_ui_font_c}")
    target_include_directories(ico_ui_font_rd PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
                               PRIVATE "${CMAKE_SOURCE_DIR}/port/third_party/stb")
    target_compile_definitions(ico_ui_font_rd PRIVATE ICO_HOST=1 ICO_RD=1)
    target_compile_options(ico_ui_font_rd PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_PORT_WARNINGS})
    target_link_libraries(ico_ui_font_rd PUBLIC ico_render)
    target_sources(rd_present_test PRIVATE $<TARGET_OBJECTS:ico_ui_font_rd>)
    target_include_directories(rd_present_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

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
# menu_text_test (P3): the menu text table (against the disc's texProperty
# when the image is there), the strings, the hook through the real
# layout_texture.c (text for a table row, the texture for another, classic
# mode) on the recording, then a title row into SCENE on a Vulkan device
# (exit 77 without one)
add_executable(menu_text_test test/menu_text_test.c $<TARGET_OBJECTS:ico_ui_rd>
    "${CMAKE_SOURCE_DIR}/ico2/common/src/layout_texture.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/GifPacket.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DisplayList.c"
    "${CMAKE_SOURCE_DIR}/ico2/seki/src/DmaPacket.c")
target_include_directories(menu_text_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" ${_ico_ui_game_incs}
                           ${_ico_ui_seki_incs} "${CMAKE_SOURCE_DIR}/port/shaders"
                           "${CMAKE_SOURCE_DIR}/port/math" "${CMAKE_SOURCE_DIR}/port/rhi")
target_compile_definitions(menu_text_test PRIVATE ICO_HOST=1 ICO_RD=1)
target_compile_options(menu_text_test PRIVATE ${ICO_SEMANTIC_OPTIONS} ${ICO_GAME_LAYOUT_OPTIONS}
                       ${ICO_GAME_WARNINGS})
target_link_libraries(menu_text_test PRIVATE ico_render ico_math)
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
    set_tests_properties(settings_render PROPERTIES SKIP_RETURN_CODE 77)
    if(NOT DEFINED ICO_DISC_IMAGE)
        set(ICO_DISC_IMAGE "${CMAKE_SOURCE_DIR}/baserom/Ico_PAL.iso" CACHE FILEPATH
            "The PAL disc image (SCES-50760) the disc tests read, never committed")
    endif()
    add_test(NAME menu_text COMMAND menu_text_test "${ICO_DISC_IMAGE}"
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(menu_text PROPERTIES SKIP_RETURN_CODE 77)
    set(_ico_vvl "${ICO_DEPS_DIR}/vulkan-validation")
    if(NOT WIN32 AND EXISTS "${_ico_vvl}/VkLayer_khronos_validation.json")
        set_tests_properties(ui PROPERTIES ENVIRONMENT
                             "VK_ADD_LAYER_PATH=${_ico_vvl};ICO_VK_VALIDATION=1")
        set_tests_properties(menu_text PROPERTIES ENVIRONMENT
                             "VK_ADD_LAYER_PATH=${_ico_vvl};ICO_VK_VALIDATION=1")
    endif()
endif()
