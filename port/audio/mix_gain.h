/*
 * port/audio/mix_gain.h
 *
 * The music and effects gains (docs/port/AUDIO.md, "Gains and output
 * mode"): the SPU2 renders one stereo mix, so the port scales the voices'
 * own volume registers by the category of sound each voice is playing.
 *
 *   - Each of the 48 voice slots carries a category tag, set where the game
 *     decides what plays on it: the sequencer's volume packet
 *     (_SgSeqSeVolume, sce/libsndn2/sound.c) tags a BGM sequence's voice
 *     music and an SE sequence's voice effects; an ADPCM stream's open
 *     (adpcmDataSet, ico2/fumi/sound/adpcm_init.c) tags its voices music,
 *     or effects for Yorda's hint voice (streams 101 to 104).
 *   - The driver's two voice volume writes, packet 0x01 (sndn2_host.c) and
 *     packet 0x40 (stream.c), go through ico_audio_gain_voice, which keeps
 *     the raw VOLL/VOLR and writes them scaled by the slot's gain.  At
 *     100 % the written word is the raw word, bit for bit.
 *   - A volume word in sweep mode (bit 15) is written unscaled (logged once
 *     when a gain is below 100 %); the game's voice volumes are all fixed
 *     levels.
 *   - The film category scales the PCM mixer's channel volumes
 *     (stream.c pcm_mix), not a slot; no Settings row sets it, so films
 *     follow the master volume alone.
 *   - A gain change re-issues the cached volumes of the slots it covers,
 *     so it is heard at once.
 *
 * Everything runs on the simulation thread (the game's fibers, the driver
 * and the render share it, audio_host.h), so nothing is locked.
 */
#ifndef ICO_PORT_AUDIO_MIX_GAIN_H
#define ICO_PORT_AUDIO_MIX_GAIN_H

#include <stdint.h>

enum {
    ICO_AUDIO_CAT_NONE = -1, /* untagged: unscaled */
    ICO_AUDIO_CAT_MUSIC = 0,
    ICO_AUDIO_CAT_EFFECTS,
    ICO_AUDIO_CAT_FILM,
    ICO_AUDIO_CAT_COUNT
};

#define ICO_AUDIO_GAIN_SLOTS 48
#define ICO_AUDIO_GAIN_ONE 4096 /* q12 */

/* The category of what slot (0..47) plays; out of range is ignored. */
void ico_audio_tag_slot(int slot, int cat);
int ico_audio_slot_tag(int slot);
/* The ADPCM stream hook: stream number `no` (adpcmFile's index) on slot:
   101..104 (event2/hint*.int, Yorda's hint voice) effects, the rest music. */
void ico_audio_tag_stream(int slot, int no);

/* `gain` 0.0 .. 1.0 (clamped, NaN as 0), stored in 1/4096 steps.  A change
   re-issues the cached volumes of that category's slots. */
void ico_audio_set_gain(int cat, double gain);
int ico_audio_gain_q12(int cat);

/* A voice volume word scaled by q12 (pure): bit 15 clear, the low 15 bits
   are a signed level (the SPU2's fixed mode), scaled with rounding; bit 15
   set (sweep mode) is returned unchanged.  q12 = 4096 is the identity. */
uint16_t ico_audio_gain_scale(uint16_t vol, int q12);
/* vol scaled by the gain of slot's category (unscaled when untagged). */
uint16_t ico_audio_gain_apply(int slot, uint16_t vol);

/* The driver's voice volume write: cache voll/volr as the raw values of
   slot and write VOLL/VOLR scaled (spu2_sd_set_param). */
void ico_audio_gain_voice(int slot, uint16_t voll, uint16_t volr);
/* Re-issue every cached volume; returns how many slots were written. */
int ico_audio_gain_reapply(void);
/* Forget the cached volumes (the driver's init, 0x1E, and its reset). */
void ico_audio_gain_forget(void);

/* A PCM channel volume (the film mixer's 32-bit word) scaled by the film
   gain; the identity at 100 %. */
uint32_t ico_audio_gain_pcm(uint32_t vol);

/* Power-on state: no tags, every gain 100 %, nothing cached (tests). */
void ico_audio_gain_reset(void);

#endif
