# port/ui/embed_font.cmake: a file as a C array, at build time, so the
# program carries its fonts without a file beside it.
#   cmake -DIN=<font.ttf> -DOUT=<font_data.c> [-DNAME=<symbol>] -P embed_font.cmake
# Writes `const unsigned char <NAME>[]` and `<NAME>_size` (NAME ui_font_ttf
# by default: Arimo; ui_font_ja_ttf: the Japanese text's font).
#
# The font's subset (the recipe that made Arimo-Regular.ttf) holds these
# code points; tools/font_audit.py checks the port's text against this line:
# subset: U+0020-007E,U+00A0-017F,U+0192,U+02C6,U+02DC,U+2013-2014,U+2018-201E,U+2020-2022,U+2026,U+2030,U+2039-203A,U+20AC,U+2122
# The Japanese text's font (port/assets/fonts/NotoSansJP-Regular-subset.ttf,
# made by tools/subset_font_ja.py, which writes the next line) holds these;
# font.c draws from it the characters Arimo has no glyph for:
# subset-ja: U+0020-007E,U+25A1,U+25B3,U+25CB,U+3000-303F,U+3041-30FF,U+4E00,U+4E0A-4E0B,U+4E0D,U+4E21,U+4E2D,U+4E86,U+4EBA,U+4EE3,U+4EE5,U+4F11,U+4F4D,U+4F53,U+4F5C,U+4F7F,U+4FDD,U+4FEE,U+500B,U+5012,U+505C,U+5076,U+50BE,U+50CD,U+50CF,U+5148-5149,U+5165,U+5168,U+5175,U+5185,U+518D,U+5199,U+51FA,U+5206-5207,U+521D,U+5225,U+5236,U+524A,U+524D,U+5263,U+5272,U+529B,U+52A0,U+52B9,U+52D5,U+534A,U+5373,U+53BB,U+53CD,U+53D6,U+53E3-53E4,U+53F3,U+5404,U+5408,U+540C-540D,U+5411,U+5468,U+56DE,U+56F2,U+5730,U+5782,U+57CE,U+5834,U+5854,U+5883,U+5893,U+58F0,U+5909,U+5927,U+5929,U+5931,U+5973,U+59CB,U+5B57-5B58,U+5B9A,U+5B9F,U+5BB6,U+5BB9,U+5BFE,U+5C02,U+5C04,U+5C0F,U+5D16,U+5DDE,U+5DE6,U+5DEE,U+5E2F,U+5E38,U+5E55,U+5E7C,U+5E83,U+5EA6,U+5EAD,U+5F15,U+5F37,U+5F3E,U+5F53,U+5F62,U+5F71,U+5F8C,U+5FC5,U+6027,U+611F,U+61B6,U+623B,U+6249,U+624B,U+6280,U+629C,U+62BC,U+62FE,U+63A2,U+63A5,U+63CF,U+64CD,U+653E,U+6551,U+6557,U+6570,U+6574-6575,U+6587,U+65AD,U+65B9,U+65C5,U+65E5,U+6607,U+660E,U+6620,U+6642,U+666E,U+6697,U+66F4,U+66F8,U+66FF-6700,U+671F,U+672A-672C,U+6765,U+6771,U+677F,U+679C,U+67F1,U+67FB,U+697D,U+6A19,U+6A4B,U+6B21,U+6B27,U+6B62-6B63,U+6BB5,U+6BD4,U+6C17,U+6C34,U+6C7A,U+6CD5,U+6D5C,U+6D88,U+6DF1,U+6E08,U+6E21,U+6E80,U+6E90,U+6E96,U+6EDD,U+7070,U+70B9,U+7121,U+7136,U+7206,U+7248,U+725B,U+7269,U+72E9,U+7387,U+738B,U+74B0,U+751F,U+7528,U+753B,U+754C,U+7570,U+75B2,U+767A,U+767D,U+76EE,U+76F4,U+771F,U+7740,U+77E5,U+77F3,U+7802,U+793A,U+79F0,U+79FB,U+7A7A,U+7AEF,U+7BC4,U+7D05,U+7D20,U+7D3A,U+7D42,U+7D66,U+7D9A,U+7DD1-7DD2,U+7DDA,U+7E2E,U+7E3E,U+7F6E,U+7FFC,U+8005,U+808C,U+8131,U+81EA,U+8272,U+8336,U+8449,U+8535,U+866B,U+884C,U+885B,U+8868,U+88AB,U+88DC,U+897F,U+8981,U+898B,U+8996,U+89A7,U+89D2,U+89E3,U+8A00,U+8A08,U+8A18,U+8A2D,U+8A33,U+8A66,U+8A9E,U+8AAC-8AAD,U+8AB0,U+8ABF,U+8CEA,U+8D08,U+8D64,U+8D70,U+8D77,U+8DB3,U+8ECA,U+8EE2,U+8EFD,U+8F2A,U+8FBA,U+8FBC,U+8FD4,U+8FFD,U+900F,U+9014,U+901A,U+901F,U+9045,U+904A,U+9069,U+9078,U+90E8,U+90ED,U+914D,U+91CE-91CF,U+91D1,U+9332,U+9396,U+9577,U+9580,U+958B,U+9593,U+95D8,U+964D,U+9650,U+9664,U+968E,U+96A0,U+96E2,U+96FB,U+9752,U+9762,U+97F3,U+985E,U+98A8,U+9AA8,U+9AD8,U+9B54,U+9CE5,U+9ED2,U+FF01-FF5E
if(NOT IN OR NOT OUT)
    message(FATAL_ERROR "embed_font.cmake: IN and OUT are required")
endif()
if(NOT NAME)
    set(NAME ui_font_ttf)
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
     "const unsigned char ${NAME}[${size}] = {\n    ${bytes}\n};\n"
     "const unsigned int ${NAME}_size = ${size}u;\n")
file(RENAME "${OUT}.tmp" "${OUT}")
