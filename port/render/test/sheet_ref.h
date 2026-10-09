/* sheet_ref.h: font_sheet_ps on the CPU, the oracle of the sheet-text
 * tests (rd_pixel_test testSheetText, port/ui/test's menu comparisons).
 * rd.h rd_CreateTextureSheet says what the shader draws;
 * this is the same arithmetic with the same constants (shader_consts.h
 * ICO_SHEET_*, checked against sheet_text.hlsli at compile time).
 *
 * cov is a w x h coverage strip (0..255 a texel, rows packed), the
 * texture's bytes; texels outside it have coverage 0.  style may be null:
 * rd_CreateTextureSheet's default {1, 0, 255, 1}.  style->scale is
 * the strip texels a sheet texel: the Bayer threshold is the sheet texel's
 * (floor(x / scale), floor(y / scale)) and the rim
 * is the 1x rim of the sheet texels' mean coverage, quantised per sheet
 * texel and magnified bilinearly, the letters the texel's own coverage on
 * top (rd.h, "Scaled strips").
 *
 * sheetref_Texel   the sheet texel (x, y), x and y any integers: grey
 *                  0..255 and alpha in GS units (0x80 full)
 * sheetref_Sample  the texel value font_sheet_ps passes on at normalised
 *                  (u, v) (the interpolated UV of a pixel): the four sheet
 *                  texels around u * w - 0.5, v * h - 0.5 blended
 *                  bilinearly in float and rounded, as rgba (r = g = b =
 *                  the grey, a the alpha), before the texture function */
#ifndef PORT_RENDER_TEST_SHEET_REF_H
#define PORT_RENDER_TEST_SHEET_REF_H

#include <stdint.h>
#include "rd.h"

void sheetref_Texel(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                    const RdSheetStyle *style, uint8_t *outGrey, uint8_t *outAlpha);
void sheetref_Sample(const uint8_t *cov, uint32_t w, uint32_t h, float u, float v,
                     const RdSheetStyle *style, uint8_t rgba[4]);

#endif
