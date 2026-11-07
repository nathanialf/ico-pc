#ifndef RD_UPSCALE_TEMPORAL_H
#define RD_UPSCALE_TEMPORAL_H

#include "rd.h"

/* FSR 3.1's rows (Options > Picture > Upscaling = FSR 3.1 and Upscaling
 * quality, issue 99): which settings turn the temporal upscaler on, at what
 * ratio, and what the others give way to.  The upscaler itself (rd_fsr3.c,
 * FSR3HOST) asks rd_upscale_temporal_supported(); until it exists that is
 * false everywhere, so the value is stored and shown, and drawn as Off. */

/* The ratio of an RdUpscaleQuality: 1.0 Native AA, 1.5 Quality, 1.7
 * Balanced, 2.0 Performance; an unknown value is Native AA */
float rd__upscale_quality_ratio(int quality);

/* FSR 3.1 in force for the settings: asked for (RD_UPSCALE_FSR3), supported
 * on the device, no CRT filter (it draws its own 1x box) and Downsampling
 * off (one or the other).  MSAA and SSAA read as off while this is true
 * (rd__aa_requested).  crt: the CRT filter in force */
bool rd__upscale_temporal_on(const RdSettings *st, bool crt);

/* The starting ratio for the settings: the quality's, but 1.0 (Native AA
 * only) under the Original preset or with the scene at 1x or less (sceneScale:
 * the scene's scale in force, the larger axis) */
float rd__upscale_temporal_ratio(const RdSettings *st, float sceneScale);

/* rd__apply_display's part: note gets ", upscaling fsr3 quality (ratio R)"
 * in force, ", upscaling fsr3 off (REASON)" asked for but not in force, ""
 * otherwise; true when that changed since the last call (the display line
 * is logged again).  crt, down: the CRT filter and Downsampling in force */
bool rd__upscale_temporal_apply(const RdSettings *st, bool crt, bool down, float sceneScale,
                                char *note, size_t noteSize);

#endif
