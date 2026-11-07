/* FSR 3.1's rows: the quality ratios, the exclusions and the display line's
 * words (rd_upscale_temporal.h); the support query is rd_fsr3.c's */

#include "rd_fsr3.h"
#include "rd_internal.h"
#include "rd_upscale_temporal.h"

#include <stdio.h>
#include <string.h>

/* rd_upscale_temporal_supported() is rd_fsr3.c's (the device's answer) */

float rd__upscale_quality_ratio(int quality)
{
    switch (quality) {
    case RD_UPSCALE_Q_QUALITY:
        return 1.5f;
    case RD_UPSCALE_Q_BALANCED:
        return 1.7f;
    case RD_UPSCALE_Q_PERFORMANCE:
        return 2.0f;
    default:
        return 1.0f;
    }
}

bool rd__upscale_temporal_on(const RdSettings *st, bool crt)
{
    return st->upscale == RD_UPSCALE_FSR3 && !crt && !st->downsample &&
           rd_upscale_temporal_supported();
}

float rd__upscale_temporal_ratio(const RdSettings *st, float sceneScale)
{
    if (st->preset == RD_PRESET_ORIGINAL || !(sceneScale > 1.0f)) {
        return 1.0f;
    }
    return rd__upscale_quality_ratio(st->upscaleQuality);
}

static uint8_t s_asked;
static bool s_on, s_inited;
static float s_ratio;

bool rd__upscale_temporal_apply(const RdSettings *st, bool crt, bool down, float displayScale,
                                const char *detail, char *note, size_t noteSize)
{
    const bool asked = st->upscale == RD_UPSCALE_FSR3;
    const bool on = rd__upscale_temporal_on(st, crt);
    const float ratio = on ? rd__upscale_temporal_ratio(st, displayScale) : 0.0f;
    if (note && noteSize) {
        note[0] = '\0';
        if (on) {
            static const char *const names[RD_UPSCALE_Q_COUNT] = {"native AA", "quality",
                                                                  "balanced", "performance"};
            const int q = st->upscaleQuality < RD_UPSCALE_Q_COUNT ? st->upscaleQuality : 0;
            snprintf(note, noteSize, ", upscaling fsr3 %s (ratio %.1f%s)", names[q], (double)ratio,
                     detail ? detail : "");
        } else if (asked) {
            snprintf(note, noteSize, ", upscaling fsr3 off (%s%s)",
                     crt                        ? "the CRT filter draws its own box"
                     : (down || st->downsample) ? "Downsampling is on"
                                                : "not available: ",
                     crt || down || st->downsample ? "" : rd_upscale_temporal_why());
        }
    }
    const bool changed = !s_inited || st->upscale != s_asked || on != s_on || ratio != s_ratio;
    s_inited = true;
    s_asked = st->upscale;
    s_on = on;
    s_ratio = ratio;
    return changed;
}
