/* pan_test.c: the mirror mode's pan swap (renderer wave 7, R7c).
 *
 *   swap    ico_audio_pan_mirror swaps left and right of every frame when
 *           mirror is set, in place, and leaves the block alone when not
 *   twice   swapping twice gives the block back byte for byte
 *   option  ico_audio_set_mirror / ico_audio_mirror are ico_opt_mirror's
 *           value (the one ico_audio_host_vsync reads per block)
 */
#include <stdio.h>
#include <string.h>

#include "audio_host.h"
#include "options.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define FRAMES 960 /* one vsync at 50 Hz */

static void fill(int16_t *b)
{
    for (int i = 0; i < FRAMES; i++) {
        b[2 * i] = (int16_t)(i * 37 - 16000);       /* left: a ramp */
        b[2 * i + 1] = (int16_t)(-(i * 11) + 9000); /* right: another */
    }
    b[0] = -32768; /* the extremes survive */
    b[1] = 32767;
}

int main(void)
{
    static int16_t ref[FRAMES * 2], b[FRAMES * 2];

    fill(ref);
    memcpy(b, ref, sizeof(b));
    ico_audio_pan_mirror(b, FRAMES, 0);
    CHECK(memcmp(b, ref, sizeof(b)) == 0, "mirror off: the block is unchanged");

    ico_audio_pan_mirror(b, FRAMES, 1);
    int bad = 0;
    for (int i = 0; i < FRAMES; i++) {
        bad += b[2 * i] != ref[2 * i + 1] || b[2 * i + 1] != ref[2 * i];
    }
    CHECK(bad == 0, "mirror on: %d frames not swapped", bad);

    ico_audio_pan_mirror(b, FRAMES, 1);
    CHECK(memcmp(b, ref, sizeof(b)) == 0, "swapped twice: the block back");

    ico_audio_pan_mirror(b, 0, 1); /* nothing to do */
    ico_audio_pan_mirror(NULL, FRAMES, 1);
    CHECK(memcmp(b, ref, sizeof(b)) == 0, "empty and NULL blocks: no change");

    ico_audio_set_mirror(1);
    CHECK(ico_audio_mirror() == 1 && ico_opt_mirror() == 1, "the option on");
    ico_audio_set_mirror(0);
    CHECK(ico_audio_mirror() == 0 && ico_opt_mirror() == 0, "the option off");

    if (failures) {
        printf("pan_test: %d failures\n", failures);
        return 1;
    }
    printf("pan_test: ok\n");
    return 0;
}
