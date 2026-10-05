/*
 * port/audio/test/volume_test.c
 *
 * The output volume (audio_host.h): the 0..1 -> 0..256 mapping and clamp,
 * and the integer scaling of sample blocks.  Exit status 0 when every check
 * passes.
 */
#include <stdio.h>

#include "../audio_host.h"

static int failures;

static void check_eq(long got, long want, const char *what)
{
    if (got != want) {
        failures++;
        printf("FAIL: %s: got %ld, want %ld\n", what, got, want);
    }
}

int main(void)
{
    int16_t in[6] = {0, 1000, -1000, 32767, -32768, 255};
    int16_t out[6];
    int i;

    check_eq(ico_audio_volume_q8(), 256, "default is full");
    ico_audio_set_volume(0.0);
    check_eq(ico_audio_volume_q8(), 0, "0.0");
    ico_audio_set_volume(0.5);
    check_eq(ico_audio_volume_q8(), 128, "0.5");
    ico_audio_set_volume(0.1);
    check_eq(ico_audio_volume_q8(), 26, "0.1 rounds to 26/256");
    ico_audio_set_volume(1.0);
    check_eq(ico_audio_volume_q8(), 256, "1.0");
    ico_audio_set_volume(7.5);
    check_eq(ico_audio_volume_q8(), 256, "above 1 clamps");
    ico_audio_set_volume(-3.0);
    check_eq(ico_audio_volume_q8(), 0, "below 0 clamps");
    ico_audio_set_volume(-0.0);
    check_eq(ico_audio_volume_q8(), 0, "zero again");

    ico_audio_scale(out, in, 6, 256);
    for (i = 0; i < 6; i++) {
        check_eq(out[i], in[i], "256 is a copy");
    }
    ico_audio_scale(out, in, 6, 128);
    check_eq(out[0], 0, "half: 0");
    check_eq(out[1], 500, "half: 1000");
    check_eq(out[2], -500, "half: -1000");
    check_eq(out[3], 16384, "half: 32767 rounds up");
    check_eq(out[4], -16384, "half: -32768");
    check_eq(out[5], 128, "half: 255 rounds to 128");
    ico_audio_scale(out, in, 6, 0);
    for (i = 0; i < 6; i++) {
        check_eq(out[i], 0, "0 is silence");
    }
    ico_audio_scale(out, in, 6, 9999);
    for (i = 0; i < 6; i++) {
        check_eq(out[i], in[i], "gain above 256 clamps to unity (no overflow)");
    }
    ico_audio_scale(out, in, 6, -5);
    check_eq(out[1], 0, "negative gain clamps to 0");
    ico_audio_scale(in, in, 6, 64); /* in place */
    check_eq(in[1], 250, "in place: quarter");
    check_eq(in[3], 8192, "in place: quarter of full scale");

    if (failures == 0) {
        printf("volume_test: ok\n");
    }
    return failures != 0;
}
