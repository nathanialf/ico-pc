# port/ui: the port's runtime text, layout extension, strings and popups
# (Phase 6, package 6B; docs/port/UI.md).
#
#   ico_ui       the game's copy, compiled into ico_pc: ICO_RD in the window
#                build (draws through port/render), measure-only headless;
#                ui_host.c (the game's globals, the decoder flush) in the
#                window build only
#   ico_ui_rd    the same sources with ICO_RD for ui_test (needs ico_render)
#
# The font file is embedded as a C array generated at build time
# (embed_font.cmake), so the program needs no file beside it.

set(_ui_font "${CMAKE_SOURCE_DIR}/port/assets/fonts/EBGaramond-Regular.ttf")
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
set_source_files_properties(layout_ext.c PROPERTIES COMPILE_OPTIONS "-Wno-strict-prototypes")

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

if(TARGET ico_pc)
    if(NOT ICO_HEADLESS AND TARGET ico_render)
        ico_ui_library(ico_ui ON ui_host.c)
    else()
        ico_ui_library(ico_ui OFF)
    endif()
    target_sources(ico_pc PRIVATE $<TARGET_OBJECTS:ico_ui>)
    target_include_directories(ico_pc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(NOT TARGET ico_render)
    message(STATUS "ico: ui_test not built (needs port/render)")
    return()
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
    set(_ico_vvl "${ICO_DEPS_DIR}/vulkan-validation")
    if(NOT WIN32 AND EXISTS "${_ico_vvl}/VkLayer_khronos_validation.json")
        set_tests_properties(ui PROPERTIES ENVIRONMENT
                             "VK_ADD_LAYER_PATH=${_ico_vvl};ICO_VK_VALIDATION=1")
    endif()
endif()
