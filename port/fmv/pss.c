/*
 * port/fmv/pss.c
 *
 * pss.h.  Program stream syntax from ISO/IEC 13818-1 section 2.5.3 (pack
 * header, system header) and 2.4.3.6 (PES packet header); the MPEG-1 pack
 * and PES forms (ISO/IEC 11172-1) are accepted too, though the game's
 * streams use only the MPEG-2 ones.  Sony's audio framing is from the
 * game's own player (ito/mpeg/mv_audiodec.c: the 4 bytes pcmCallback skips,
 * the 40-byte header audioDecEndPut collects) and from the disc's streams.
 */
#include "pss.h"
#include "../include/ico_endian.h"
#include <string.h>

static uint32_t rd16be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | p[1];
}

/* A 33-bit time stamp in the 5-byte PES form. */
static int64_t ts33(const uint8_t *p)
{
    return ((int64_t)((p[0] >> 1) & 7) << 30) | ((int64_t)p[1] << 22) |
           ((int64_t)(p[2] >> 1) << 15) | ((int64_t)p[3] << 7) | (int64_t)(p[4] >> 1);
}

/* The PES header of an MPEG-2 or MPEG-1 packet whose body (after the
   6-byte start code and length) is b[0..n).  Sets the time stamps and
   returns the payload offset, or -1 when the header overruns the body. */
static long pes_header(const uint8_t *b, size_t n, IcoPssPacket *pkt)
{
    size_t i = 0;

    if (n >= 3 && (b[0] & 0xC0) == 0x80) {
        /* MPEG-2: flags, PES_header_data_length, then the fields */
        uint8_t flags = b[1];
        size_t hl = b[2];

        if (3 + hl > n) {
            return -1;
        }
        if ((flags & 0x80) != 0 && hl >= 5) {
            pkt->has_pts = 1;
            pkt->pts = ts33(b + 3);
            if ((flags & 0x40) != 0 && hl >= 10) {
                pkt->has_dts = 1;
                pkt->dts = ts33(b + 8);
            }
        }
        return (long)(3 + hl);
    }
    /* MPEG-1: stuffing 0xFF, an optional STD buffer field, then the stamps */
    while (i < n && b[i] == 0xFF) {
        i++;
    }
    if (i < n && (b[i] & 0xC0) == 0x40) {
        i += 2;
    }
    if (i >= n) {
        return -1;
    }
    if ((b[i] & 0xF0) == 0x20) {
        if (i + 5 > n) {
            return -1;
        }
        pkt->has_pts = 1;
        pkt->pts = ts33(b + i);
        i += 5;
    } else if ((b[i] & 0xF0) == 0x30) {
        if (i + 10 > n) {
            return -1;
        }
        pkt->has_pts = pkt->has_dts = 1;
        pkt->pts = ts33(b + i);
        pkt->dts = ts33(b + i + 5);
        i += 10;
    } else if (b[i] == 0x0F) {
        i++;
    } else {
        return -1;
    }
    return (long)i;
}

long ico_pss_next(const uint8_t *buf, size_t len, IcoPssPacket *pkt)
{
    uint8_t id;
    size_t total;
    long off;

    memset(pkt, 0, sizeof(*pkt));
    if (len < 4) {
        return 0;
    }
    if (buf[0] != 0 || buf[1] != 0 || buf[2] != 1) {
        return -1;
    }
    id = buf[3];
    pkt->stream_id = id;
    if (id == 0xB9) {
        pkt->kind = ICO_PSS_END;
        return 4;
    }
    if (id == 0xBA) {
        pkt->kind = ICO_PSS_PACK;
        if (len < 5) {
            return 0;
        }
        if ((buf[4] & 0xC0) == 0x40) {
            /* MPEG-2: 14 bytes and pack_stuffing_length */
            if (len < 14) {
                return 0;
            }
            total = 14 + (size_t)(buf[13] & 7);
        } else if ((buf[4] & 0xF0) == 0x20) {
            total = 12; /* MPEG-1 */
        } else {
            return -1;
        }
        return len < total ? 0 : (long)total;
    }
    if (id < 0xBB) {
        return -1; /* a video start code where a packet should be */
    }
    if (len < 6) {
        return 0;
    }
    total = 6 + rd16be(buf + 4);
    if (len < total) {
        return 0;
    }
    pkt->kind = ICO_PSS_OTHER;
    if (id == 0xBB || id == 0xBE || id == 0xBF || id == 0xBC || id == 0xF0 || id == 0xF1 ||
        id == 0xFF || id == 0xF2 || id == 0xF8) {
        return (long)total; /* no PES header (13818-1 table 2-21's exceptions) */
    }
    off = pes_header(buf + 6, total - 6, pkt);
    if (off < 0) {
        return (long)total; /* a damaged header: skipped */
    }
    pkt->data = buf + 6 + off;
    pkt->len = total - 6 - (size_t)off;
    if (id >= 0xE0 && id <= 0xEF) {
        pkt->kind = ICO_PSS_VIDEO;
    } else if (id == 0xBD && pkt->len >= 4) {
        /* Sony's 4-byte audio framing: FF A0 00 00 on the disc; the
           sub-stream is the byte that carries 0xA0 + n */
        uint8_t sub = pkt->data[0] == 0xFF ? pkt->data[1] : pkt->data[0];

        if ((sub & 0xF0) == 0xA0) {
            pkt->kind = ICO_PSS_AUDIO;
            pkt->sub_id = sub;
            pkt->data += 4;
            pkt->len -= 4;
        }
    }
    return (long)total;
}

size_t ico_pss_resync(const uint8_t *buf, size_t len)
{
    size_t i;

    for (i = 1; i + 4 <= len; i++) {
        if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1 && buf[i + 3] == 0xBA) {
            return i;
        }
    }
    return len > 3 ? len - 3 : 0;
}

int ico_pss_audio_header(const uint8_t *h, IcoPssAudioHeader *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->id, h, 4);
    out->header_size = ico_le32(h + 4);
    out->type = ico_le32(h + 8);
    out->rate = ico_le32(h + 12);
    out->channels = ico_le32(h + 16);
    out->interleave = ico_le32(h + 20);
    out->interleave_start = ico_le32(h + 24);
    out->interleave_end = ico_le32(h + 28);
    memcpy(out->data_id, h + 32, 4);
    out->data_size = ico_le32(h + 36);
    return memcmp(out->id, "SShd", 4) == 0 && memcmp(out->data_id, "SSbd", 4) == 0 ? 0 : -1;
}

long ico_pss_es_find_start(const uint8_t *es, size_t len)
{
    size_t i;

    for (i = 0; i + 4 <= len; i++) {
        if (es[i + 2] > 1) {
            i += 2; /* no start code can begin at i, i + 1 or i + 2 */
            continue;
        }
        if (es[i] == 0 && es[i + 1] == 0 && es[i + 2] == 1) {
            return (long)i;
        }
    }
    return -1;
}

long ico_pss_es_unit_end(const uint8_t *es, size_t len)
{
    size_t pos = 0;
    int seen_pic = 0;

    for (;;) {
        long s = ico_pss_es_find_start(es + pos, len - pos);
        uint8_t c;

        if (s < 0) {
            return -1;
        }
        pos += (size_t)s;
        c = es[pos + 3];
        if (seen_pic && (c == 0x00 || c == 0xB3 || c == 0xB8 || c == 0xB7)) {
            return (long)pos;
        }
        if (c == 0x00) {
            seen_pic = 1;
        }
        pos += 4;
    }
}
