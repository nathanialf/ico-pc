/*
 * port/audio/stream_watch.h
 *
 * What the log says about the ADPCM streams and the packets around them,
 * so a report of music that played silently can be settled from the log
 * alone (docs/port/AUDIO.md, "What the log says about streams"):
 *
 *   - adpcm: 43 voices 2,3 vol 3fff/3fff env 7fff/7fff
 *       two driver ticks after the stream's key-on: each voice's volume (the
 *       louder side of its VOLL/VOLR registers as the SPU2 holds them, the
 *       Music volume setting included) and its envelope (ENVX);
 *   - adpcm: 43 at half: voices 2,3 vol ... env ...
 *       once the driver has fed half the file's bytes to the voices;
 *   - adpcm: 43 at close: voices 2,3 vol ... env ...
 *       when the stream's stop reaches the driver, before its key-off.
 *   At most those three per stream.  The game's own lines (adpcm_init.c)
 *   name the open (`adpcm: open 43 slot 0 voices 2,3`) and the close.
 *
 *   - sndn2: key-off from the sequencer (<function>) on stream voice V
 *       a sequencer key-off (sce/libsndn2/sound.c) hit one of the four
 *       voices the streams own (slot state 3);
 *   - sndn2: packet page full ... (sndn2_host.c, ico_sndn2_pk_full);
 *   - sndn2: the first packet carried past the page's 255 ..., and the
 *       first stream packet carried (open, volume, pitch, play, stop),
 *       once a run each (issue 101: without the carry a stream opened on a
 *       full page keeps the volume the last stream left).
 *   The key-on line ends with the page's fill at the open:
 *   `(opened on a page of 37 packets)`, or `(opened on a full page:
 *   carried past its 255)`.
 *
 * The heartbeat carries `adpcm: 43@2 vol 3fff/3fff env 7fff/7fff, -` (the
 * game's two stream slots: stream @ first voice, or `-`), and the end of
 * the run one `audio: streams: ...` line with the counts (the streams
 * opened on a full page and the stream packets carried among them).
 *
 * Everything but ico_audio_stream_status runs on the simulation thread;
 * ico_audio_stream_status is the heartbeat thread's plain read of a text
 * the simulation publishes once per driver tick.
 */
#ifndef ICO_PORT_AUDIO_STREAM_WATCH_H
#define ICO_PORT_AUDIO_STREAM_WATCH_H

#include <stddef.h>
#include <stdint.h>

/* The game's open (adpcmDataSet, ico2/fumi/sound/adpcm_init.c): stream `no`
   in the game's stream slot `slot` (0, 1) on `n` voices, `data_bytes` the
   file's length.  The watch starts when the driver sees the open packet
   (0x3E) for voices[0]. */
void ico_audio_stream_watch(int slot, int no, const int *voices, int n, int data_bytes);

/* The heartbeat's part (no leading separator): `adpcm: 43@2 vol .../...
   env .../..., -`. */
void ico_audio_stream_status(char *out, size_t size);

typedef struct IcoAudioStreamStats {
    unsigned int opened;     /* ico_audio_stream_watch calls */
    unsigned int keyoffs;    /* sequencer key-offs on stream voices */
    unsigned int pages_full; /* EE packet pages that reached 255 packets */
    unsigned int carried;    /* packets past the 255th, carried to the driver */
    unsigned int dropped;    /* packets past the carry list's room, lost */
    /* issue 101: stream packets (0x3E open, 0x40 volume, 0x41 pitch, 0x42
       play, 0x43 stop) among the carried ones, and the game's streams
       whose open packet ran on a full page or was carried past it */
    unsigned int stream_carried;
    unsigned int opens_on_full;
} IcoAudioStreamStats;

void ico_audio_stream_stats(IcoAudioStreamStats *out);

/* Where the lines go: NULL (the default) is ico_diag_log.  Tests capture. */
typedef void (*IcoAudioLogSink)(const char *line);
void ico_audio_stream_log_sink(IcoAudioLogSink sink);

/* Forget every watch and count (ico_sndn2_host_reset). */
void ico_audio_stream_watch_reset(void);

/* --- the driver's side (stream.c, sndn2_host.c) ----------------------------- */
void sw_open(uint32_t voice);  /* 0x3E for `voice` */
void sw_keyon(uint32_t voice); /* the queued KEYON keyed `voice` on */
void sw_played(uint32_t voice, uint32_t bytes);
void sw_stop(uint32_t voice); /* 0x43 for `voice`, before its key-off */
void sw_tick(void);           /* start of each driver tick */
void sw_page_full(void);
/* a packet past the full page's 255th, carried (cmd: its command) */
void sw_carried(int cmd);
/* the page the driver runs now: its packet count, and 1 while it runs the
   packets carried past it (sndn2_host.c), for the open's page fill */
void sw_page(int fill, int carried);
void sw_dropped(void);
void sw_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
