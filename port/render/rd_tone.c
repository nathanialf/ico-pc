/* rd_tone.c: the colour adjustment's settings (rd_tone.h; Options >
 * Picture: Gamma, Picture brightness, Contrast, Saturation, 1..100, 50 the
 * game's picture).
 *
 * The arithmetic is PCSX2's shade boost (port/shaders/tone.hlsli), done on
 * the GPU in the present: the box blit's twins blit_tone_ps and
 * blit_depth_tone_ps (rd_present.c) or, under the CRT filter, its source
 * reads (crt.hlsl c_toneAdj, c_toneOn; rd_crt.c compose).  The port's own
 * order: the adjustment acts on the finished picture before the CRT
 * filter.  Every value at 50 (or 0, a zeroed RdSettings) leaves the present
 * as it was byte for byte: the box blit keeps blit_ps and the CRT branch is
 * not taken. */
#include "rd_tone.h"

#include "rd_internal.h"

/* a setting as tone.hlsli's factor: PCSX2's value * (1 / 50) */
static float factorOf(uint8_t v)
{
    return v ? (float)v * (1.0f / 50.0f) : 1.0f;
}

static bool neutral(uint8_t v)
{
    return v == 0 || v == 50;
}

/* the value shown in the log: 0 as the 50 it reads as */
static unsigned shown(uint8_t v)
{
    return v ? v : 50u;
}

bool rd__tone_active_in(const RdSettings *s)
{
    return !neutral(s->toneGamma) || !neutral(s->toneBrightness) || !neutral(s->toneContrast) ||
           !neutral(s->toneSaturation);
}

bool rd__tone_active(void)
{
    return rd__tone_active_in(&g_rd.settings);
}

void rd__tone_params_in(const RdSettings *s, float out[4])
{
    out[0] = factorOf(s->toneBrightness);
    out[1] = factorOf(s->toneContrast);
    out[2] = factorOf(s->toneSaturation);
    out[3] = factorOf(s->toneGamma);
}

void rd__tone_params(float out[4])
{
    rd__tone_params_in(&g_rd.settings, out);
}

void rd__tone_log(const RdSettings *was, const RdSettings *now)
{
    if (was) {
        if (shown(was->toneGamma) == shown(now->toneGamma) &&
            shown(was->toneBrightness) == shown(now->toneBrightness) &&
            shown(was->toneContrast) == shown(now->toneContrast) &&
            shown(was->toneSaturation) == shown(now->toneSaturation)) {
            return;
        }
    } else if (!rd__tone_active_in(now)) {
        return;
    }
    rd__log("present: colour adjustment gamma %u, brightness %u, contrast %u, saturation %u "
            "(50 = the game's picture)%s",
            shown(now->toneGamma), shown(now->toneBrightness), shown(now->toneContrast),
            shown(now->toneSaturation),
            rd__tone_active_in(now) ? "" : ": off, the present without it");
}
