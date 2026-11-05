/* rd_tone.h: the colour adjustment (Options > Picture: Gamma, Picture
 * brightness, Contrast, Saturation; rd.h RdSettings.toneGamma,
 * toneBrightness, toneContrast, toneSaturation; rd_tone.c). */
#ifndef ICO_RD_TONE_H
#define ICO_RD_TONE_H

#include <stdbool.h>

#include "rd.h"

/* On when any of the four settings is neither 0 nor 50 (0 reads as 50, the
 * game's picture).  Off, the present's box blit is blit_ps (blit_depth_ps
 * with the effects depth) and the CRT filter skips the step: the present
 * byte for byte the one without the rows.  On, the box blit is
 * blit_tone_ps (blit_depth_tone_ps) and the CRT filter adjusts its source
 * (crt.hlsl c_toneOn). */
bool rd__tone_active(void);
bool rd__tone_active_in(const RdSettings *s);
/* tone.hlsli's four factors, in its order: [0] brightness, [1] contrast,
 * [2] saturation, [3] gamma; each setting / 50 (PCSX2's
 * GSDevice::ShadeBoost scaling), 0 as 50 (1.0) */
void rd__tone_params(float out[4]);
void rd__tone_params_in(const RdSettings *s, float out[4]);
/* The log line of a change of the four between was and now (none when
 * they are equal), and of now at start-up with was NULL (only when on) */
void rd__tone_log(const RdSettings *was, const RdSettings *now);

#endif
