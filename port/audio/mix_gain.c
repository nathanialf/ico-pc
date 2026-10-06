/*
 * port/audio/mix_gain.c
 *
 * The music and effects gains on the voices' volume registers
 * (mix_gain.h).
 */
#include "mix_gain.h"

#include <stdio.h>

#include "sndn2_internal.h"
#include "spu2_sd.h"

static signed char tag[ICO_AUDIO_GAIN_SLOTS];
static int gain_q12[ICO_AUDIO_CAT_COUNT] = {ICO_AUDIO_GAIN_ONE, ICO_AUDIO_GAIN_ONE,
                                            ICO_AUDIO_GAIN_ONE};

static struct {
    int valid;
    uint16_t l, r;
} cache[ICO_AUDIO_GAIN_SLOTS];

static int tags_ready;
static int logged_sweep;

static void ensure_tags(void)
{
    int i;

    if (!tags_ready) {
        for (i = 0; i < ICO_AUDIO_GAIN_SLOTS; i++) {
            tag[i] = ICO_AUDIO_CAT_NONE;
        }
        tags_ready = 1;
    }
}

void ico_audio_tag_slot(int slot, int cat)
{
    ensure_tags();
    if (slot >= 0 && slot < ICO_AUDIO_GAIN_SLOTS && cat >= ICO_AUDIO_CAT_NONE &&
        cat < ICO_AUDIO_CAT_COUNT) {
        tag[slot] = (signed char)cat;
    }
}

int ico_audio_slot_tag(int slot)
{
    ensure_tags();
    return slot >= 0 && slot < ICO_AUDIO_GAIN_SLOTS ? tag[slot] : ICO_AUDIO_CAT_NONE;
}

void ico_audio_tag_stream(int slot, int no)
{
    ico_audio_tag_slot(slot, no >= 101 && no <= 104 ? ICO_AUDIO_CAT_EFFECTS : ICO_AUDIO_CAT_MUSIC);
}

/* floor(n / 4096) for either sign (no reliance on >> of a negative) */
static int64_t floor_q12(int64_t n)
{
    return n >= 0 ? n / 4096 : -((-n + 4095) / 4096);
}

uint16_t ico_audio_gain_scale(uint16_t vol, int q12)
{
    int32_t level;

    if ((vol & 0x8000u) != 0) {
        return vol;
    }
    q12 = q12 < 0 ? 0 : q12 > ICO_AUDIO_GAIN_ONE ? ICO_AUDIO_GAIN_ONE : q12;
    level = (int32_t)((vol & 0x7FFFu) ^ 0x4000u) - 0x4000; /* bits 0-14, signed */
    level = (int32_t)floor_q12((int64_t)level * q12 + 2048);
    return (uint16_t)((uint32_t)level & 0x7FFFu);
}

static int slot_q12(int slot)
{
    int c = ico_audio_slot_tag(slot);

    return c == ICO_AUDIO_CAT_NONE ? ICO_AUDIO_GAIN_ONE : gain_q12[c];
}

uint16_t ico_audio_gain_apply(int slot, uint16_t vol)
{
    int q = slot_q12(slot);

    if (q == ICO_AUDIO_GAIN_ONE) {
        return vol;
    }
    if ((vol & 0x8000u) != 0 && !logged_sweep) {
        logged_sweep = 1;
        fprintf(stderr,
                "audio: slot %d volume 0x%04X is a sweep: written unscaled by the music/effects "
                "gain\n",
                slot, vol);
    }
    return ico_audio_gain_scale(vol, q);
}

static void write_slot(int slot)
{
    spu2_sd_set_param(vsel((uint32_t)slot, SPU2_SD_VPARAM_VOLL),
                      ico_audio_gain_apply(slot, cache[slot].l));
    spu2_sd_set_param(vsel((uint32_t)slot, SPU2_SD_VPARAM_VOLR),
                      ico_audio_gain_apply(slot, cache[slot].r));
}

void ico_audio_gain_voice(int slot, uint16_t voll, uint16_t volr)
{
    if (slot < 0 || slot >= ICO_AUDIO_GAIN_SLOTS) {
        return;
    }
    cache[slot].valid = 1;
    cache[slot].l = voll;
    cache[slot].r = volr;
    write_slot(slot);
}

static int reapply(int cat)
{
    int n = 0;
    int i;

    for (i = 0; i < ICO_AUDIO_GAIN_SLOTS; i++) {
        if (cache[i].valid && (cat == ICO_AUDIO_CAT_COUNT || ico_audio_slot_tag(i) == cat)) {
            write_slot(i);
            n++;
        }
    }
    return n;
}

int ico_audio_gain_reapply(void)
{
    return reapply(ICO_AUDIO_CAT_COUNT);
}

void ico_audio_gain_forget(void)
{
    int i;

    for (i = 0; i < ICO_AUDIO_GAIN_SLOTS; i++) {
        cache[i].valid = 0;
    }
}

void ico_audio_set_gain(int cat, double gain)
{
    int q;

    if (cat < 0 || cat >= ICO_AUDIO_CAT_COUNT) {
        return;
    }
    if (!(gain > 0.0)) { /* also NaN */
        q = 0;
    } else if (gain >= 1.0) {
        q = ICO_AUDIO_GAIN_ONE;
    } else {
        q = (int)(gain * ICO_AUDIO_GAIN_ONE + 0.5);
    }
    if (q != gain_q12[cat]) {
        gain_q12[cat] = q;
        if (cat != ICO_AUDIO_CAT_FILM) { /* the PCM mixer reads it per half */
            reapply(cat);
        }
    }
}

int ico_audio_gain_q12(int cat)
{
    return cat >= 0 && cat < ICO_AUDIO_CAT_COUNT ? gain_q12[cat] : ICO_AUDIO_GAIN_ONE;
}

uint32_t ico_audio_gain_pcm(uint32_t vol)
{
    int q = gain_q12[ICO_AUDIO_CAT_FILM];

    if (q == ICO_AUDIO_GAIN_ONE) {
        return vol;
    }
    return (uint32_t)floor_q12((int64_t)(int32_t)vol * q + 2048);
}

void ico_audio_gain_reset(void)
{
    int i;

    tags_ready = 0;
    ensure_tags();
    for (i = 0; i < ICO_AUDIO_CAT_COUNT; i++) {
        gain_q12[i] = ICO_AUDIO_GAIN_ONE;
    }
    ico_audio_gain_forget();
    logged_sweep = 0;
}
