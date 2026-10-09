/*
 * port/audio/sndn2_internal.h
 *
 * What sndn2_host.c (the packet dispatcher and the reply page) and
 * stream.c (the ADPCM and PCM stream engines) share.  Packet words follow the SNDN2DRV.IRX's
 * packet layout.
 */
#ifndef ICO_PORT_AUDIO_SNDN2_INTERNAL_H
#define ICO_PORT_AUDIO_SNDN2_INTERNAL_H

#include <stdint.h>

#include "spu2_sd.h"

#define SNDN2_SLOTS 48 /* core * 24 + voice */
#define SNDN2_PCM_CHANNELS 16
#define SNDN2_STAGING_SIZE 0x2000

/* libsd's voice selector for a slot (the IRX's V(x)): `param | core | voice << 1`. */
static inline uint16_t vsel(uint32_t slot, uint16_t param)
{
    return (uint16_t)(param | SPU2_SD_VOICE(slot / 24, slot % 24));
}

/* The IRX's .bss staging buffer at 0x5140. */
uint8_t *sndn2_staging_buf(void);

/* One diagnostic line per kind of anomaly, then silence. */
void sndn2_log_once(int *flag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* --- ADPCM streams (stream.c) ----------------------------------------------- */
void st_adpcm_reset(void);                                   /* power-on (tests) */
void st_adpcm_init(void);                                    /* 0x3C */
void st_adpcm_open(uint32_t w1, uint32_t w2, uint32_t w3);   /* 0x3E */
void st_adpcm_close(uint32_t slot);                          /* 0x3F */
void st_adpcm_volume(uint32_t m0, uint32_t m1, uint32_t w3); /* 0x40 */
void st_adpcm_pitch(uint32_t m0, uint32_t m1, uint32_t w3);  /* 0x41 */
void st_adpcm_play(uint32_t m0, uint32_t m1);                /* 0x42 */
void st_adpcm_stop(uint32_t m0, uint32_t m1);                /* 0x43 */
/* the refill scheduler (0x2778) and the event queue runner (0x2A54) */
void st_adpcm_tick(void);
uint32_t st_adpcm_read_off(int slot);

/* --- PCM streams (stream.c) ------------------------------------------------- */
void st_pcm_init(void);                                          /* 0x46 */
void st_pcm_quit(void);                                          /* 0x47 */
void st_pcm_open(uint32_t id, uint32_t w2, uint32_t w3);         /* 0x48 */
void st_pcm_close(uint32_t ch);                                  /* 0x49 */
void st_pcm_volume(uint32_t mask, uint32_t l, uint32_t r);       /* 0x4A */
void st_pcm_play(uint32_t mask);                                 /* 0x4B */
void st_pcm_stop(uint32_t mask);                                 /* 0x4C */
void st_pcm_lseek(uint32_t ch, uint32_t off);                    /* 0x4D */
void st_pcm_effect(uint32_t id);                                 /* 0x4E */
void st_pcm_bufmode(uint32_t mask, uint32_t off, uint32_t mode); /* 0x4F */
uint32_t st_pcm_read_off(int ch);

#endif
