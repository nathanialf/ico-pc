/*
 * port/audio/sndn2_host.h
 *
 * The host replacement for SNDN2DRV.IRX, the IOP sound driver behind the
 * Sg sequencer's RPC.  It is a host SIF server (port/data/sif_host.h)
 * under the IRX's server id: RPC 0x65 runs one packet (the init), RPC 0x64
 * runs a tick (a page of 16-byte packets, then the ADPCM stream scheduler
 * and event queue, then the 0x200-byte reply page).  Every packet becomes
 * the libsd calls the IRX makes, on the software SPU2 (spu2_sd.h).
 *
 * Threading: none.  The server runs on the game's sound fiber, the SPU2
 * renders on the host context at each vsync (audio_host.h); both are the
 * one simulation thread, so the calls never overlap.
 */
#ifndef ICO_PORT_AUDIO_SNDN2_HOST_H
#define ICO_PORT_AUDIO_SNDN2_HOST_H

#include <stdint.h>

#define ICO_SNDN2_SERVER_ID 0x736E646Eu /* "sndn" */
#define ICO_SNDN2_RPC_TICK 0x64
#define ICO_SNDN2_RPC_INIT 0x65
#define ICO_SNDN2_REPLY_SIZE 0x200

/* The reply page (R1, "Mailbox"). */
#define ICO_SNDN2_REPLY_ENVX 0x000   /* 48 words: ENVX & 0x7FFF, slot = core * 24 + voice */
#define ICO_SNDN2_REPLY_STREAM 0x0C0 /* 48 words: ADPCM stream IOP read offsets */
#define ICO_SNDN2_REPLY_PCM 0x180    /* 16 words: PCM channel read offsets */
#define ICO_SNDN2_REPLY_XFER 0x1C0   /* the transfer counter of the last 0x20/0x21 */

#define ICO_SNDN2_PITCH_ENTRIES 608

/* First call: power the SPU2 on (spu2_reset), load the pitch table (below)
   and register the server with the host SIF.  Later calls re-register only.
   SgSndn2RemoteInit calls it before binding. */
void ico_sndn2_host_register(void);

/* Return the SPU2 and the driver to power-on and forget the pitch table
   (tests). */
void ico_sndn2_host_reset(void);

/* The server function itself (ico_sif_register_server's IcoSifServerFn). */
void *ico_sndn2_host_serve(unsigned int rpc_number, void *send, int ssize, int rsize);

/* The page the last tick returned (tests). */
const uint8_t *ico_sndn2_host_last_reply(void);

/* --- The pitch table (R1, "Pitch") ------------------------------------------
   608 u16 in 1/16-semitone steps, T[208] = 0x1000.  It is data in the
   user's SNDN2DRV.IRX (file offset 0x3900, 0x4C0 bytes) and is read from
   the disc at registration; without it the driver uses floor(4096 *
   2^((i - 208) / 192)), which R1 found 1 low or high in 32 entries. */
#define ICO_SNDN2_PITCH_IRX 1
#define ICO_SNDN2_PITCH_FORMULA 2

/* Load from the mounted disc (port/data/vfs.h); falls back to the formula
   and says so once on stderr.  Returns the source. */
int ico_sndn2_pitch_load(void);
/* Use `table` (608 entries), or the formula when NULL (tests). */
void ico_sndn2_pitch_set(const uint16_t *table);
int ico_sndn2_pitch_source(void);
const uint16_t *ico_sndn2_pitch_table(void);
/* The formula's entry i. */
uint16_t ico_sndn2_pitch_formula(int i);
/* Command 0x04's PITCH word from its packet words. */
uint16_t ico_sndn2_pitch_compute(uint32_t w2, uint32_t w3);
/* How many 0x04 packets indexed outside the table (clamped). */
unsigned ico_sndn2_pitch_clamps(void);

/* The IRX's 0x2000-byte staging buffer (ADPCM fills and the PCM AutoDMA
   ring share it, R1), for tests. */
const uint8_t *ico_sndn2_staging(void);

#endif
