# port/ui/embed_font.cmake: a file as a C array, at build time, so the
# program carries its font without a file beside it.
#   cmake -DIN=<font.ttf> -DOUT=<font_data.c> -P embed_font.cmake
# Writes `const unsigned char ui_font_ttf[]` and `ui_font_ttf_size`.
if(NOT IN OR NOT OUT)
    message(FATAL_ERROR "embed_font.cmake: IN and OUT are required")
endif()
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" hexlen)
math(EXPR size "${hexlen} / 2")
# 0x.., sixteen bytes a line
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n    " bytes "${bytes}")
get_filename_component(name "${IN}" NAME)
file(WRITE "${OUT}.tmp"
     "/* generated from ${name} by port/ui/embed_font.cmake: do not edit */\n"
     "const unsigned char ui_font_ttf[${size}] = {\n    ${bytes}\n};\n"
     "const unsigned int ui_font_ttf_size = ${size}u;\n")
file(RENAME "${OUT}.tmp" "${OUT}")
