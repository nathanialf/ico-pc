/*
 * port/audio/volume.c
 *
 * The output volume (audio_host.h): a 0..256 gain in 1/256 steps and the
 * integer scaling of 16-bit sample blocks.  No platform dependencies, so the
 * Settings menu and the unit test link it without the driver.
 */
#include "audio_host.h"

static volatile int volume_q8 = 256;

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
    volume_q8 = q;
}

int ico_audio_volume_q8(void)
{
    return volume_q8;
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
