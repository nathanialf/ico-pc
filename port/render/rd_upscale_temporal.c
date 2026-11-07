/* FSR 3.1's rows: the support query (a stub until FSR3HOST), the quality
 * ratios, the exclusions and the display line's words (rd_upscale_temporal.h) */

#include "rd_internal.h"
#include "rd_upscale_temporal.h"

#include <stdio.h>
#include <string.h>

bool rd_upscale_temporal_supported(void)
{
    /* The temporal upscaler is not built into this version: false on every
     * backend.  The line says so once (the Options page asks on every
     * frame) so a reporter's log shows why the row is "(off: not
     * available)" */
    static bool told;
    if (!told) {
        told = true;
        rd__log("upscaling: FSR 3.1 is not available (the temporal upscaler is not in this "
                "version); a saved \"fsr3\" is drawn as off and kept in the config");
    }
    return false;
}

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

bool rd__upscale_temporal_apply(const RdSettings *st, bool crt, bool down, float sceneScale,
                                char *note, size_t noteSize)
{
    const bool asked = st->upscale == RD_UPSCALE_FSR3;
    const bool on = rd__upscale_temporal_on(st, crt);
    const float ratio = on ? rd__upscale_temporal_ratio(st, sceneScale) : 0.0f;
    if (note && noteSize) {
        note[0] = '\0';
        if (on) {
            static const char *const names[RD_UPSCALE_Q_COUNT] = {"native AA", "quality",
                                                                  "balanced", "performance"};
            const int q = st->upscaleQuality < RD_UPSCALE_Q_COUNT ? st->upscaleQuality : 0;
            snprintf(note, noteSize, ", upscaling fsr3 %s (ratio %.1f)", names[q], (double)ratio);
        } else if (asked) {
            snprintf(note, noteSize, ", upscaling fsr3 off (%s)",
                     crt                        ? "the CRT filter draws its own box"
                     : (down || st->downsample) ? "Downsampling is on"
                                                : "not available");
        }
    }
    const bool changed = !s_inited || st->upscale != s_asked || on != s_on || ratio != s_ratio;
    s_inited = true;
    s_asked = st->upscale;
    s_on = on;
    s_ratio = ratio;
    return changed;
}
