/*
 * port/ui/settings_upscale.c
 *
 * The Upscaling rows' helpers (settings_upscale.h).
 */
#include "settings.h"
#include "settings_upscale.h"

#include <stdio.h>

#include "strings.h"

#ifdef ICO_RD
#include "rd.h" /* rd_upscale_temporal_supported */
#endif

static int (*s_temporalQuery)(void); /* FSR 3.1 supported, when installed */

void ui_settings_set_temporal_query(int (*fn)(void))
{
    s_temporalQuery = fn;
}

int ui__temporal_supported(void)
{
    if (s_temporalQuery) {
        return s_temporalQuery() != 0;
    }
#ifdef ICO_RD
    return rd_upscale_temporal_supported();
#else
    return 0;
#endif
}

int ui__temporal_on(const IcoVideoOptions *o)
{
    return o->upscale == ICO_UPSCALE_FSR3 && ui__temporal_supported() && !o->crt && !o->downsample;
}

const char *ui__upscale_text(const IcoVideoOptions *o, int opt, char *buf, unsigned size)
{
    static const int qStr[ICO_UPSCALE_Q_COUNT] = {
        UI_STR_VAL_UPSCALE_Q_NATIVE, UI_STR_VAL_UPSCALE_Q_QUALITY, UI_STR_VAL_UPSCALE_Q_BALANCED,
        UI_STR_VAL_UPSCALE_Q_PERFORMANCE};
    if (opt == UI_OPT_UPSCALE_QUALITY) {
        return ui_str(qStr[o->upscaleQuality >= 0 && o->upscaleQuality < ICO_UPSCALE_Q_COUNT
                               ? o->upscaleQuality
                               : ICO_UPSCALE_Q_NATIVE]);
    }
    if (o->upscale == ICO_UPSCALE_FSR3) {
        /* a saved FSR 3.1 the device cannot run: shown as saved, drawn as Off */
        snprintf(buf, size, "%s%s", ui_str(UI_STR_VAL_UPSCALE_FSR3),
                 ui__temporal_supported() ? "" : ui_str(UI_STR_AA_UNAVAILABLE));
        return buf;
    }
    return ui_str(o->upscale == ICO_UPSCALE_FSR1 ? UI_STR_VAL_UPSCALE_FSR1 : UI_STR_OFF);
}

void ui__upscale_step(IcoVideoOptions *o, int opt, int dir)
{
    if (opt == UI_OPT_UPSCALE_QUALITY) {
        o->upscaleQuality =
            ((o->upscaleQuality + dir) % ICO_UPSCALE_Q_COUNT + ICO_UPSCALE_Q_COUNT) %
            ICO_UPSCALE_Q_COUNT;
        return;
    }
    /* Off, FSR 1.0, FSR 3.1 (only where the device runs it), around; a saved
       FSR 3.1 the device cannot run steps on to the next value that is
       offered (the stored word is never rewritten otherwise) */
    for (int n = 0; n < ICO_UPSCALE_COUNT; n++) {
        o->upscale =
            ((o->upscale + dir) % ICO_UPSCALE_COUNT + ICO_UPSCALE_COUNT) % ICO_UPSCALE_COUNT;
        if (o->upscale != ICO_UPSCALE_FSR3 || ui__temporal_supported()) {
            break;
        }
    }
}

int ui__upscale_row_shown(int opt)
{
    const int up = ico_video_upscale();
    if (opt == UI_OPT_UPSCALE_QUALITY) {
        return up == ICO_UPSCALE_FSR3 && ui__temporal_supported();
    }
    /* Sharpness: nothing to sharpen with Upscaling Off, or FSR 3.1 the
       device cannot run */
    return up == ICO_UPSCALE_FSR1 || (up == ICO_UPSCALE_FSR3 && ui__temporal_supported());
}
