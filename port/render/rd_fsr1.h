/* rd_fsr1.h: AMD FidelityFX Super Resolution 1.0 at the present (Options >
 * Picture > Upscaling: FSR 1.0; rd.h RdSettings.upscale, upscaleSharpness;
 * rd_fsr1.c, port/shaders/fsr1.hlsl). */
#ifndef ICO_RD_FSR1_H
#define ICO_RD_FSR1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd.h"
#include "rhi.h"

/* AMD's FsrEasuConOffset (ffx_fsr1.h), the same float operations in the
 * same order, its four uint4 as con[0..3]: a viewport vpW x vpH of an input
 * texture inW x inH at offset offX, offY upscaled to outW x outH */
void rd__fsr1_easu_con(uint32_t con[4][4], float vpW, float vpH, float inW, float inH, float outW,
                       float outH, float offX, float offY);
/* AMD's FsrRcasCon: stops of sharpness taken away (0 the sharpest);
 * con[0] 2^-stops as a float's bits, con[1] the same as two halves (AMD's
 * truncating table, ffx_a.h AU1_AH2_AF2; the 32-bit path reads con[0]) */
void rd__fsr1_rcas_con(uint32_t con[4], float stops);
/* The Sharpness row (0..100, clamped) as RCAS's stops: 2 (1 - s / 100);
 * half a stop more when the picture is stretched past 2x on either axis
 * (scale: the larger of box / source across and down), where EASU is past
 * AMD's range (up to 2x a side) and RCAS would sharpen its softness */
float rd__fsr1_stops(unsigned sharpness, float scale);

/* rd__apply_display's part: FSR 1.0 in force for the settings
 * (RD_UPSCALE_FSR1, no CRT filter, window false: not the "window"
 * resolution); true when that or the sharpness changed since the last call
 * (the display line is logged again).  note gets ", upscaling fsr1
 * (sharpness S)" in force, ", upscaling fsr1 off (...)" asked for but not in
 * force, "" otherwise */
bool rd__fsr1_apply(const RdSettings *st, bool window, char *note, size_t noteSize);

/* The present's step after the window's reduce (rd_present.c
 * rd__present_record): *src (*sw x *sh, crop texels past the exact width
 * at each side) EASU-upscaled into a texture of the box's size and RCAS
 * sharpened into a second; false (nothing recorded) when it is not in force,
 * the source does not have fewer texels than the box on both axes, the
 * one-texel-a-pixel window applies (snapped) or a pipeline is missing.  On
 * true *src, *sw, *sh are the second texture's (box->w x box->h), drawn
 * into the box one texel a pixel (nearest, no crop; the mirror and the
 * colour adjustment stay the box blit's) */
bool rd__fsr1_record(RhiCommandList cl, RhiTexture *src, uint32_t *sw, uint32_t *sh,
                     const RhiRect *box, float crop, bool snapped);
/* A present begins: the last present's note cleared */
void rd__fsr1_present_begin(void);
/* The last present's FSR 1.0 pass, false when none ran (or no present yet):
 * src w, h (the texture EASU read, the crop included), dst w, h (the box) */
bool rd__last_present_fsr1(uint32_t out[4]);
/* The tests: the last RCAS output (box size, RGBA8) read back; false
 * without a device or before FSR 1.0 drew */
bool rd__read_fsr1(void *dst, size_t dstSize, uint32_t *w, uint32_t *h);
/* The textures freed (rd_shutdown); the last present's note stays, as the
 * reduces' do, for the tests that read it after the shutdown */
void rd__fsr1_shutdown(void);

#endif
