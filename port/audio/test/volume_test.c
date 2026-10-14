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

    /* the play ratio: the rate ratio, a quarter of a percent faster above
       the target (out_sdl.c passes its cushion: two vsyncs of 800 frames of
       4 bytes, 6400 bytes, at first; 23040 bytes at its 120 ms largest) */
    check_eq((long)(ico_audio_play_ratio(1.001, 3200, 6400) * 1e6 + 0.5), 1001000,
             "one vsync: as is");
    check_eq((long)(ico_audio_play_ratio(1.001, 6400, 6400) * 1e6 + 0.5), 1001000, "two: as is");
    check_eq((long)(ico_audio_play_ratio(1.0, 6404, 6400) * 1e6 + 0.5), 1002500,
             "above two: trimmed");
    check_eq((long)(ico_audio_play_ratio(1.0, 9999, 0) * 1e6 + 0.5), 1000000, "no block size");
    check_eq((long)(ico_audio_play_ratio(1.0, 20000, 23040) * 1e6 + 0.5), 1000000,
             "120 ms cushion is preserved");
    check_eq((long)(ico_audio_play_ratio(1.0, 24000, 23040) * 1e6 + 0.5), 1002500,
             "only surplus above 120 ms is trimmed");
    /* a queue grown to 2.2 vsyncs (a rate measured a little short over
       the blocks), pushed and played a vsync at a time: trimmed back to
       two vsyncs, then left alone */
    {
        const double vs = 3200.0;
        double q = 2.2 * vs, ratio = 1.0;
        int n;

        for (n = 0; n < 3600; n++) {
            ratio = ico_audio_play_ratio(1.0, (int)q, (int)(2 * vs));
            q += vs - vs * ratio; /* a vsync in, a vsync times the ratio out */
        }
        check_eq(q <= 2.0 * vs + 10.0 && q > 1.99 * vs, 1, "a surplus trimmed to two vsyncs");
        check_eq(ratio == 1.0, 1, "then the rate as is");
    }

    if (failures == 0) {
        printf("volume_test: ok\n");
    }
    return failures != 0;
}
