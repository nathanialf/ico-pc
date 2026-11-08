/* sheet_ref.h: font_sheet_ps on the CPU, the oracle of the sheet-text
 * tests (rd_pixel_test testSheetText, port/ui/test's menu comparisons).
 * rd.h rd_create_texture_sheet says what the shader draws;
 * this is the same arithmetic with the same constants (shader_consts.h
 * ICO_SHEET_*, checked against sheet_text.hlsli at compile time).
 *
 * cov is a w x h coverage strip (0..255 a texel, rows packed), the
 * texture's bytes; texels outside it have coverage 0.  style may be null:
 * rd_create_texture_sheet's default {1, 0, 255, 1}.  style->scale is
 * the strip texels a sheet texel: the Bayer threshold is the sheet texel's
 * (floor(x / scale), floor(y / scale)) and the rim
 * is the 1x rim of the sheet texels' mean coverage, quantised per sheet
 * texel and magnified bilinearly, the letters the texel's own coverage on
 * top (rd.h, "Scaled strips").
 *
 * sheetref_texel   the sheet texel (x, y), x and y any integers: grey
 *                  0..255 and alpha in GS units (0x80 full)
 * sheetref_sample  the texel value font_sheet_ps passes on at normalised
 *                  (u, v) (the interpolated UV of a pixel): the four sheet
 *                  texels around u * w - 0.5, v * h - 0.5 blended
 *                  bilinearly in float and rounded, as rgba (r = g = b =
 *                  the grey, a the alpha), before the texture function
 *
 * Looks.  The rim's reach and falloff, the level count and the dither
 * matrix are shader constants (shader_consts.h ICO_SHEET_*); a SheetLook
 * holds them as data, so a test can draw a candidate look the shader does
 * not have (ctest menu_look's ICO_MENU_LOOK_OUTLINE mode).  The default
 * look is the constants, and sheetref_texel / sheetref_sample are the
 * _look functions with it: one implementation.
 *
 * sheetref_texel_look, sheetref_sample_look  the same under look (NULL:
 *                  the default)
 * sheetref_rim_look  the 1x rim of texel (x, y) (the weighted dilation of
 *                  the coverage, 0..255, before the style's rim weight)
 * sheetref_texel_cr  the 1x texel at sheet texel (x, y) from its coverage c
 *                  and rim r (sheetref_rim_look's), so a caller that tries
 *                  many styles over one strip computes each rim once
 * sheetref_bayer8  the 8x8 Bayer matrix (common.hlsli gs_bayer8), 0..63 */
#ifndef PORT_RENDER_TEST_SHEET_REF_H
#define PORT_RENDER_TEST_SHEET_REF_H

#include <stdint.h>
#include "rd.h"
#include "shader_consts.h"

/* A look: the rim reaches rx texels across and ry down (at most
   ICO_SHEET_RX / RY: the tables' lengths, the strips' margins and the
   pages' gaps derive from those), weighted by wx[|dx|] * wy[|dy|] per
   mille (the entries past rx / ry are not read); the opacity and the
   rim-to-fill mix quantised to levels (2..256) against a threshold: bayer 4
   the 4x4 matrix (2b + 1 in 32nds of a step, font_sheet_ps's), 8 the 8x8
   one (2b + 1 in 128ths), 0 none (a half: rounding; a style with dither 0
   rounds under every matrix). */
typedef struct SheetLook {
    int rx, ry;
    uint16_t wx[ICO_SHEET_RX + 1], wy[ICO_SHEET_RY + 1];
    int levels;
    int bayer; /* 0 none, 4, 8 */
} SheetLook;

/* the shader's constants as a look */
const SheetLook *sheetref_default_look(void);

void sheetref_texel(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                    const RdSheetStyle *style, uint8_t *outGrey, uint8_t *outAlpha);
void sheetref_sample(const uint8_t *cov, uint32_t w, uint32_t h, float u, float v,
                     const RdSheetStyle *style, uint8_t rgba[4]);
void sheetref_texel_look(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                         const RdSheetStyle *style, const SheetLook *look, uint8_t *outGrey,
                         uint8_t *outAlpha);
void sheetref_sample_look(const uint8_t *cov, uint32_t w, uint32_t h, float u, float v,
                          const RdSheetStyle *style, const SheetLook *look, uint8_t rgba[4]);
uint32_t sheetref_rim_look(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                           const SheetLook *look);
void sheetref_texel_cr(uint32_t c, uint32_t r, int32_t x, int32_t y, const RdSheetStyle *style,
                       const SheetLook *look, uint8_t *outGrey, uint8_t *outAlpha);
uint32_t sheetref_bayer8(uint32_t x, uint32_t y);

#endif
