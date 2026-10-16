/*
 * port/audio/volume.c
 *
 * The output volume (audio_host.h): a 0..256 gain in 1/256 steps and the
 * integer scaling of 16-bit sample blocks; the output's play ratio and the
 * choice of its clock (ico_audio_pick_mode).  No platform dependencies, so
 * the Settings menu and the unit tests link it without the driver.
 */
#include "audio_host.h"

#include <string.h>

/* set on the main thread (Settings), read where each block is queued: on
   the device's thread on the pull path (out_sdl.c pull_cb) */
static int volume_q8 = 256;

void ico_audio_set_volume(double volume)
{
    int q;

    if (!(volume > 0.0)) { /* also NaN */
        q = 0;
    } else if (volume >= 1.0) {
        q = 256;
    } else {
        q = (int)(volume * 256.0 + 0.5);
    }
    __atomic_store_n(&volume_q8, q, __ATOMIC_RELAXED);
}

int ico_audio_volume_q8(void)
{
    return __atomic_load_n(&volume_q8, __ATOMIC_RELAXED);
}

int ico_audio_pick_mode(int window_build, const char *enable, int dumping, int repeatable,
                        const char *sync, const char **why)
{
    const char *w = "";
    int mode = ICO_AUDIO_MODE_SYNC;

    if (!window_build) {
        w = "headless build";
    } else if (enable != NULL && strcmp(enable, "0") == 0) {
        w = "audio=0";
    } else if (dumping) {
        w = "audio_dump";
    } else if (repeatable) {
        w = "fixed clock";
    } else if (sync != NULL && strcmp(sync, "1") == 0) {
        w = "[dev] audio_sync";
    } else {
        mode = ICO_AUDIO_MODE_PULL;
    }
    if (why != NULL) {
        *why = w;
    }
    return mode;
}

double ico_audio_play_ratio(double rateRatio, int queuedBytes, int targetBytes)
{
    if (targetBytes > 0 && queuedBytes > targetBytes) {
        return rateRatio * ICO_AUDIO_TRIM;
    }
    return rateRatio;
}

void ico_audio_scale(int16_t *dst, const int16_t *src, int count, int q8)
{
    int i;

    q8 = q8 < 0 ? 0 : q8 > 256 ? 256 : q8;
    for (i = 0; i < count; i++) {
        int v = ((int)src[i] * q8 + 128) >> 8;

        dst[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
}
