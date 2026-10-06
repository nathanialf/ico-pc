/*
 * port/fmv/pss.h
 *
 * The PS2 movie container ("PSS"): an MPEG-2 program stream (ISO/IEC
 * 13818-1 pack headers and PES packets) whose video is one MPEG-2 elementary
 * stream (stream id 0xE0) and whose audio is PCM in private stream 1 (0xBD)
 * in Sony's own framing.  What the game's three PSS files hold
 * (measured on the PAL disc):
 *
 *   - 16384-byte packs, MPEG-2 pack headers; a system header (0xBB) in the
 *     first pack, padding packets (0xBE), the end code 0x000001B9 last;
 *   - video: PES 0xE0, PTS (and DTS) on the packets that start a picture;
 *   - audio: PES 0xBD; every payload opens with 4 bytes, FF A0 00 00 in
 *     every packet of the disc's streams (0xA0 + n is the sub-stream libmpeg's
 *     sceMpegAddStrCallback(type 2, channel n) selects; ito/mpeg's
 *     pcmCallback skips the 4 bytes); after them the first packet starts
 *     with a 40-byte header (IcoPssAudioHeader) and the rest is PCM.
 *
 * Pure functions over byte buffers; no I/O, no allocation.
 */
#ifndef ICO_PORT_FMV_PSS_H
#define ICO_PORT_FMV_PSS_H

#include <stddef.h>
#include <stdint.h>

enum {
    ICO_PSS_PACK = 1, /* pack header (0xBA) */
    ICO_PSS_VIDEO,    /* PES 0xE0..0xEF: data/len = elementary stream bytes */
    ICO_PSS_AUDIO,    /* PES 0xBD with an A0..AF sub-stream: data/len after the 4-byte header */
    ICO_PSS_OTHER,    /* system header, padding, private stream 2, other PES */
    ICO_PSS_END       /* program end code 0xB9 */
};

typedef struct IcoPssPacket {
    int kind;
    uint8_t stream_id; /* the start code's last byte */
    uint8_t sub_id;    /* ICO_PSS_AUDIO: 0xA0 + channel */
    uint8_t has_pts, has_dts;
    int64_t pts, dts; /* 90 kHz */
    const uint8_t *data;
    size_t len;
} IcoPssPacket;

/* Parses the item at buf[0..len).  Returns the bytes it spans (> 0) with
   *pkt filled, 0 when buf ends inside it (more data is needed), or -1 when
   buf does not start with a pack, PES or end start code (lost sync: skip
   to ico_pss_resync). */
long ico_pss_next(const uint8_t *buf, size_t len, IcoPssPacket *pkt);
/* The offset of the next pack header (00 00 01 BA) in buf, or len when there
   is none (keep the last 3 bytes, a start code may straddle). */
size_t ico_pss_resync(const uint8_t *buf, size_t len);

/* The 40 bytes at the head of the audio stream: two chunks, "SShd" (24
   bytes of fields) and "SSbd" (the data size), little-endian words. */
typedef struct IcoPssAudioHeader {
    char id[4];           /* "SShd" */
    uint32_t header_size; /* 24 */
    uint32_t type;        /* 0 PCM big-endian, 1 PCM little-endian, 2 ADPCM (mv_audiodec.c) */
    uint32_t rate;        /* Hz */
    uint32_t channels;
    uint32_t interleave; /* bytes of one channel before the next channel's block */
    uint32_t interleave_start, interleave_end;
    char data_id[4];    /* "SSbd" */
    uint32_t data_size; /* PCM bytes that follow */
} IcoPssAudioHeader;

#define ICO_PSS_AUDIO_HEADER_SIZE 40

/* 0 when h40 is a well-formed header ("SShd", "SSbd"), else -1. */
int ico_pss_audio_header(const uint8_t *h40, IcoPssAudioHeader *out);
/* MPEG video elementary stream: access units.  A unit is a picture with
   the sequence/GOP headers and extensions in front of it; it ends where the
   next sequence header (B3), GOP header (B8), picture (00) or sequence end
   code (B7) starts after the unit's own picture start code.  Returns the
   offset in es[0..len) where the unit beginning at es[0] ends, or -1 when
   that boundary is not in the buffer yet (at the end of the stream the
   unit runs to len). */
long ico_pss_es_unit_end(const uint8_t *es, size_t len);
/* The offset of the first start code (00 00 01 xx) at or after es[0], or -1. */
long ico_pss_es_find_start(const uint8_t *es, size_t len);

#endif /* ICO_PORT_FMV_PSS_H */
