/*
 * port/audio/test/mix_gain_test.c
 *
 * The music, effects and film gains (mix_gain.h): every fixed-mode volume
 * word unchanged at 100 % (so the sndn2 and spu2_render_crc goldens hold),
 * the scaling of the signed 15-bit level, sweep words passed through, the
 * stream numbers' categories, the cache written to the SPU2's registers and
 * re-issued on a gain change.  Exit status 0 when every check passes.
 */
#include <stdio.h>

#include "../mix_gain.h"
#include "../spu2.h"
#include "../spu2_sd.h"

static int failures;

static void check_eq(long got, long want, const char *what)
{
    if (got != want) {
        failures++;
        printf("FAIL: %s: got %ld (0x%lX), want %ld (0x%lX)\n", what, got, (unsigned long)got, want,
               (unsigned long)want);
    }
}

/* the register as written: the SPU2 applies writes as timed events */
static uint16_t voll(int slot)
{
    spu2_apply_pending();
    return spu2_sd_get_param((uint16_t)(SPU2_SD_VPARAM_VOLL | SPU2_SD_VOICE(slot / 24, slot % 24)));
}

static uint16_t volr(int slot)
{
    spu2_apply_pending();
    return spu2_sd_get_param((uint16_t)(SPU2_SD_VPARAM_VOLR | SPU2_SD_VOICE(slot / 24, slot % 24)));
}

int main(void)
{
    unsigned v;
    int bad = 0;
    int slot;

    ico_audio_gain_reset();
    spu2_reset();

    /* the identity: every fixed word, and every word through an untagged,
       music and effects slot at the default gains */
    for (v = 0; v < 0x8000u; v++) {
        if (ico_audio_gain_scale((uint16_t)v, ICO_AUDIO_GAIN_ONE) != v) {
            bad++;
        }
    }
    check_eq(bad, 0, "scale at 4096 is the identity over 0..0x7FFF");
    ico_audio_tag_slot(3, ICO_AUDIO_CAT_MUSIC);
    ico_audio_tag_slot(4, ICO_AUDIO_CAT_EFFECTS);
    bad = 0;
    for (v = 0; v < 0x10000u; v++) {
        if (ico_audio_gain_apply(2, (uint16_t)v) != v ||
            ico_audio_gain_apply(3, (uint16_t)v) != v ||
            ico_audio_gain_apply(4, (uint16_t)v) != v) {
            bad++;
        }
    }
    check_eq(bad, 0, "apply at the default gains is the identity over 0..0xFFFF");
    check_eq((long)ico_audio_gain_pcm(0x7FFFu), 0x7FFF, "film at 100 %");
    check_eq((long)ico_audio_gain_pcm(0xFFFFFFFFu), (long)0xFFFFFFFFu, "film word at 100 %");

    /* scaling: the low 15 bits as a signed level, rounded */
    check_eq(ico_audio_gain_scale(0x3FFF, 2048), 0x2000, "0x3FFF at 50 %");
    check_eq(ico_audio_gain_scale(0x1000, 2048), 0x0800, "0x1000 at 50 %");
    check_eq(ico_audio_gain_scale(0x0001, 2048), 0x0001, "1 at 50 % rounds up");
    check_eq(ico_audio_gain_scale(0x3FFF, 0), 0, "at 0 %");
    check_eq(ico_audio_gain_scale(0x7000, 2048), 0x7800, "-0x1000 at 50 % is -0x800");
    check_eq(ico_audio_gain_scale(0x4000, 2048), 0x6000, "-0x4000 at 50 % is -0x2000");
    check_eq(ico_audio_gain_scale(0x7FFF, 2048), 0x0000, "-1 at 50 % rounds to 0");
    check_eq(ico_audio_gain_scale(0x2000, 410), 0x0334, "0x2000 at 10 % (410/4096)");
    check_eq(ico_audio_gain_scale(0x3FFF, 9999), 0x3FFF, "q above 4096 clamps");
    /* sweep words (bit 15) pass through at any gain */
    check_eq(ico_audio_gain_scale(0x8000, 2048), 0x8000, "sweep 0x8000 at 50 %");
    check_eq(ico_audio_gain_scale(0xC07F, 0), 0xC07F, "sweep 0xC07F at 0 %");

    /* the stream numbers: 101..104 are the hint voice */
    ico_audio_tag_stream(10, 1);
    ico_audio_tag_stream(11, 100);
    ico_audio_tag_stream(12, 101);
    ico_audio_tag_stream(13, 104);
    ico_audio_tag_stream(14, 105);
    check_eq(ico_audio_slot_tag(10), ICO_AUDIO_CAT_MUSIC, "stream 1 music");
    check_eq(ico_audio_slot_tag(11), ICO_AUDIO_CAT_MUSIC, "stream 100 music");
    check_eq(ico_audio_slot_tag(12), ICO_AUDIO_CAT_EFFECTS, "stream 101 effects");
    check_eq(ico_audio_slot_tag(13), ICO_AUDIO_CAT_EFFECTS, "stream 104 effects");
    check_eq(ico_audio_slot_tag(14), ICO_AUDIO_CAT_MUSIC, "stream 105 music");
    check_eq(ico_audio_slot_tag(47), ICO_AUDIO_CAT_NONE, "untagged");
    ico_audio_tag_slot(48, ICO_AUDIO_CAT_MUSIC);
    check_eq(ico_audio_slot_tag(48), ICO_AUDIO_CAT_NONE, "slot 48 out of range");

    /* the cache: written raw at 100 %, re-issued scaled on a change */
    ico_audio_gain_voice(3, 0x3FFF, 0x1000);  /* music */
    ico_audio_gain_voice(4, 0x2000, 0x2000);  /* effects */
    ico_audio_gain_voice(30, 0x3000, 0x0800); /* untagged, core 1 */
    check_eq(voll(3), 0x3FFF, "music VOLL raw at 100 %");
    check_eq(volr(3), 0x1000, "music VOLR raw at 100 %");
    check_eq(ico_audio_gain_reapply(), 3, "reapply re-issues the three cached slots");
    ico_audio_set_gain(ICO_AUDIO_CAT_MUSIC, 0.5);
    check_eq(ico_audio_gain_q12(ICO_AUDIO_CAT_MUSIC), 2048, "music 0.5 is 2048");
    check_eq(voll(3), 0x2000, "music VOLL at 50 %, at once");
    check_eq(volr(3), 0x0800, "music VOLR at 50 %, at once");
    check_eq(voll(4), 0x2000, "effects unchanged");
    check_eq(voll(30), 0x3000, "untagged unchanged");
    ico_audio_set_gain(ICO_AUDIO_CAT_EFFECTS, 0.0);
    check_eq(voll(4), 0, "effects at 0 %");
    check_eq(voll(3), 0x2000, "music unchanged by the effects gain");
    ico_audio_gain_voice(3, 0x1000, 0x1000); /* a new packet while at 50 % */
    check_eq(voll(3), 0x0800, "a new music packet at 50 %");
    ico_audio_set_gain(ICO_AUDIO_CAT_MUSIC, 1.0);
    check_eq(voll(3), 0x1000, "back to the raw word at 100 %");
    ico_audio_set_gain(ICO_AUDIO_CAT_EFFECTS, 7.0);
    check_eq(ico_audio_gain_q12(ICO_AUDIO_CAT_EFFECTS), 4096, "above 1 clamps");
    check_eq(voll(4), 0x2000, "effects back to raw");
    ico_audio_set_gain(ICO_AUDIO_CAT_EFFECTS, -1.0);
    check_eq(ico_audio_gain_q12(ICO_AUDIO_CAT_EFFECTS), 0, "below 0 clamps");
    ico_audio_set_gain(ICO_AUDIO_CAT_EFFECTS, 1.0);
    /* a sweep word through a slot at 50 %: written unchanged (logged once) */
    ico_audio_set_gain(ICO_AUDIO_CAT_MUSIC, 0.5);
    ico_audio_gain_voice(3, 0xC07F, 0x3FFF);
    check_eq(voll(3), 0xC07F, "sweep VOLL unscaled");
    check_eq(volr(3), 0x2000, "fixed VOLR scaled beside it");
    ico_audio_set_gain(ICO_AUDIO_CAT_MUSIC, 1.0);
    /* forgotten volumes are not re-issued */
    ico_audio_gain_forget();
    check_eq(ico_audio_gain_reapply(), 0, "nothing cached after forget");
    /* retagging moves a slot between gains */
    ico_audio_gain_voice(3, 0x2000, 0x2000);
    ico_audio_tag_slot(3, ICO_AUDIO_CAT_EFFECTS);
    ico_audio_set_gain(ICO_AUDIO_CAT_MUSIC, 0.5);
    check_eq(voll(3), 0x2000, "retagged slot: not the music gain");
    ico_audio_set_gain(ICO_AUDIO_CAT_EFFECTS, 0.5);
    check_eq(voll(3), 0x1000, "retagged slot: the effects gain");

    /* film: the PCM mixer's 32-bit words */
    ico_audio_set_gain(ICO_AUDIO_CAT_FILM, 0.5);
    check_eq((long)ico_audio_gain_pcm(0x7FFFu), 0x4000, "film 0x7FFF at 50 %");
    check_eq((long)ico_audio_gain_pcm(0), 0, "film 0 at 50 %");

    ico_audio_gain_reset();
    check_eq(ico_audio_gain_q12(ICO_AUDIO_CAT_MUSIC), 4096, "reset: music 100 %");
    check_eq(ico_audio_slot_tag(3), ICO_AUDIO_CAT_NONE, "reset: no tags");
    for (slot = 0; slot < ICO_AUDIO_GAIN_SLOTS; slot++) {
        if (ico_audio_slot_tag(slot) != ICO_AUDIO_CAT_NONE) {
            failures++;
        }
    }

    if (failures) {
        printf("mix_gain_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("mix_gain_test: ok\n");
    return 0;
}
