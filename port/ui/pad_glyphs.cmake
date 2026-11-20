# port/ui/pad_glyphs.cmake (included by port/ui/CMakeLists.txt): the
# button pictures of other controllers (issue 187, pad_glyphs.c), the 24
# files of port/assets/buttons/<set>/<button>.svg embedded as C arrays at
# build time by embed_font.cmake: ui_pad_<set>_<button>_svg[] and _size,
# generated in the build directory (not tracked).  Configure stops when a
# listed file is missing: a set the program was built without could not be
# complete.
#
#   _ui_pad_glyph_data   the 24 generated sources (every program that
#                        compiles pad_glyphs.c compiles them too)
set(_ui_pad_glyph_sets xbox nintendo wii_classic)
set(_ui_pad_glyph_buttons cross circle square triangle l1 r1 l2 r2)
set(_ui_pad_glyph_data "")
foreach(_set ${_ui_pad_glyph_sets})
    foreach(_btn ${_ui_pad_glyph_buttons})
        set(_svg "${CMAKE_SOURCE_DIR}/port/assets/buttons/${_set}/${_btn}.svg")
        if(NOT EXISTS "${_svg}")
            message(FATAL_ERROR
                    "port/ui/pad_glyphs.cmake: the button picture port/assets/buttons/${_set}/"
                    "${_btn}.svg is missing (each set has the eight cross, circle, square, "
                    "triangle, l1, r1, l2, r2: port/assets/buttons/CREDIT.txt)")
        endif()
        set(_name "ui_pad_${_set}_${_btn}_svg")
        set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_name}.c")
        add_custom_command(
            OUTPUT "${_out}"
            COMMAND "${CMAKE_COMMAND}" "-DIN=${_svg}" "-DOUT=${_out}" -DNAME=${_name} -P
                    "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
            DEPENDS "${_svg}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake"
            COMMENT "Embedding the button picture ${_set}/${_btn}.svg"
            VERBATIM)
        list(APPEND _ui_pad_glyph_data "${_out}")
    endforeach()
endforeach()
