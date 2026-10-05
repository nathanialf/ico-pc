/*
 * port/fmv/test/fmv_test.c
 *
 * The FMV player's parts without the game (docs/port/FMV.md, "Tests"):
 *
 *   pss      a synthetic PSS built here (MPEG-2 packs, a system header,
 *            video PES with PTS/DTS, Sony audio PES with the FF A0 00 00
 *            framing and the SShd/SSbd header, padding, the end code), fed
 *            to ico_pss_next in every split from 1 byte up: the elementary
 *            streams and time stamps come back intact
 *   units    access-unit cutting of an MPEG video ES (sequence, GOP,
 *            picture headers, a sequence end code)
 *   decode   a hand-built MPEG-2 Main Profile intra stream (32 x 32, four
 *            macroblocks, DC-only blocks of known values) through libmpeg2:
 *            the planes come back with exactly those values
 *   pace     the display state machine: two vsyncs per picture at 50 Hz,
 *            five ahead, the end when the last picture is decoded, the
 *            abort at the next freed slot, the poll from 11 pictures
 *   disc     with the PAL image (argv[1]) present: the real
 *            pal_advertise.pss demuxed whole (picture count, audio header,
 *            byte totals) and its first 60 pictures decoded without error
 *
 * Exit 0 when every check passes (the disc part is skipped without the
 * image).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m2v.h"
#include "movie_pace.h"
#include "pss.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- a byte buffer --------------------------------------------------------- */

typedef struct Buf {
    uint8_t *p;
    size_t n, cap;
} Buf;

static void put(Buf *b, const void *src, size_t n)
{
    if (n == 0) {
        return;
    }
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 256;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, src, n);
    b->n += n;
}

static void put8(Buf *b, unsigned v)
{
    uint8_t c = (uint8_t)v;
    put(b, &c, 1);
}

/* --- a bit writer ------------------------------------------------------------ */

typedef struct Bits {
    Buf *b;
    uint32_t acc;
    int nacc;
} Bits;

static void bits(Bits *w, uint32_t v, int n)
{
    while (n-- > 0) {
        w->acc = (w->acc << 1) | ((v >> n) & 1u);
        if (++w->nacc == 8) {
            put8(w->b, w->acc);
            w->acc = 0;
            w->nacc = 0;
        }
    }
}

static void bits_align(Bits *w)
{
    while (w->nacc != 0) {
        bits(w, 0, 1);
    }
}

static void start_code(Bits *w, unsigned code)
{
    bits_align(w);
    bits(w, 0x000001, 24);
    bits(w, code, 8);
}

/* --- PSS writing ------------------------------------------------------------- */

static void pts5(Buf *b, unsigned prefix, int64_t ts)
{
    put8(b, (prefix << 4) | (unsigned)(((ts >> 30) & 7) << 1) | 1);
    put8(b, (unsigned)(ts >> 22));
    put8(b, (unsigned)(((ts >> 15) & 0x7F) << 1) | 1);
    put8(b, (unsigned)(ts >> 7));
    put8(b, (unsigned)((ts & 0x7F) << 1) | 1);
}

static void pack_header(Buf *b, unsigned stuffing)
{
    static const uint8_t h[13] = {0, 0, 1, 0xBA, 0x44, 0, 4, 0, 4, 1, 1, 0x26, 0x63};
    unsigned i;

    put(b, h, sizeof(h));
    put8(b, 0xF8 | stuffing);
    for (i = 0; i < stuffing; i++) {
        put8(b, 0xFF);
    }
}

/* A PES packet, MPEG-2 header with PTS (and DTS) when given (>= 0). */
static void pes(Buf *b, unsigned id, int64_t pts, int64_t dts, const uint8_t *pre, size_t npre,
                const uint8_t *data, size_t n)
{
    Buf h = {0};
    size_t len;

    put8(&h, 0x81);
    put8(&h, pts >= 0 ? (dts >= 0 ? 0xC0 : 0x80) : 0);
    put8(&h, pts >= 0 ? (dts >= 0 ? 10 : 5) : 0);
    if (pts >= 0) {
        pts5(&h, dts >= 0 ? 3 : 2, pts);
        if (dts >= 0) {
            pts5(&h, 1, dts);
        }
    }
    len = h.n + npre + n;
    put8(b, 0);
    put8(b, 0);
    put8(b, 1);
    put8(b, id);
    put8(b, (unsigned)(len >> 8));
    put8(b, (unsigned)len);
    put(b, h.p, h.n);
    put(b, pre, npre);
    put(b, data, n);
    free(h.p);
}

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void test_pss(void)
{
    static const uint8_t sony[4] = {0xFF, 0xA0, 0x00, 0x00};
    static const uint8_t sysh[] = {0,    0, 1,    0xBB, 0,    9,    0x80, 0x93,
                                   0x31, 0, 0x21, 0x7F, 0xE0, 0xE7, 0x06};
    uint8_t es[3000], pcm[2600], ahdr[40];
    Buf s = {0}, gotEs = {0}, gotAud = {0};
    size_t i;
    int split;

    for (i = 0; i < sizeof(es); i++) {
        es[i] = (uint8_t)(i * 7 + 3);
    }
    es[0] = 0;
    es[1] = 0;
    es[2] = 1;
    es[3] = 0xB3;
    for (i = 0; i < sizeof(pcm); i++) {
        pcm[i] = (uint8_t)(i * 13 + 1);
    }
    memcpy(ahdr, "SShd", 4);
    le32(ahdr + 4, 24);
    le32(ahdr + 8, 1);
    le32(ahdr + 12, 48000);
    le32(ahdr + 16, 2);
    le32(ahdr + 20, 512);
    le32(ahdr + 24, 0xFFFFFFFFu);
    le32(ahdr + 28, 0xFFFFFFFFu);
    memcpy(ahdr + 32, "SSbd", 4);
    le32(ahdr + 36, sizeof(pcm));

    pack_header(&s, 0);
    put(&s, sysh, sizeof(sysh));
    pes(&s, 0xE0, 4724, 1124, NULL, 0, es, 1000);
    {
        /* the audio header and the first PCM in one packet, as on the disc */
        Buf first = {0};
        put(&first, ahdr, 40);
        put(&first, pcm, 1000);
        pes(&s, 0xBD, 4724, -1, sony, 4, first.p, first.n);
        free(first.p);
    }
    pack_header(&s, 3);
    pes(&s, 0xE0, -1, -1, NULL, 0, es + 1000, 1000);
    pes(&s, 0xBE, -1, -1, NULL, 0, pcm, 0); /* padding (an empty PES-shaped packet) */
    pes(&s, 0xBD, 8324, -1, sony, 4, pcm + 1000, 1600);
    pack_header(&s, 0);
    pes(&s, 0xE0, 8324, -1, NULL, 0, es + 2000, 1000);
    put(&s, "\0\0\1\xB9", 4);

    for (split = 1; split <= 4096; split = split < 16 ? split + 1 : split * 3) {
        size_t have = 0, pos = 0;
        int packs = 0, audio = 0, video = 0, ends = 0, others = 0, ptsOk = 1;

        gotEs.n = gotAud.n = 0;
        while (pos < s.n) {
            IcoPssPacket pkt;
            long r;

            if (have < s.n && have - pos < (size_t)split * 2) {
                have = have + (size_t)split > s.n ? s.n : have + (size_t)split;
            }
            r = ico_pss_next(s.p + pos, have - pos, &pkt);
            if (r == 0) {
                if (have == s.n) {
                    CHECK(0, "split %d: truncated at %zu", split, pos);
                    break;
                }
                have = have + (size_t)split > s.n ? s.n : have + (size_t)split;
                continue;
            }
            CHECK(r > 0, "split %d: lost sync at %zu", split, pos);
            if (r < 0) {
                break;
            }
            switch (pkt.kind) {
            case ICO_PSS_PACK:
                packs++;
                break;
            case ICO_PSS_VIDEO:
                video++;
                put(&gotEs, pkt.data, pkt.len);
                if (video == 1) {
                    ptsOk &= pkt.has_pts && pkt.pts == 4724 && pkt.has_dts && pkt.dts == 1124;
                } else if (video == 2) {
                    ptsOk &= !pkt.has_pts;
                } else {
                    ptsOk &= pkt.has_pts && pkt.pts == 8324 && !pkt.has_dts;
                }
                break;
            case ICO_PSS_AUDIO:
                audio++;
                ptsOk &= pkt.sub_id == 0xA0;
                put(&gotAud, pkt.data, pkt.len);
                break;
            case ICO_PSS_END:
                ends++;
                break;
            default:
                others++;
                break;
            }
            pos += (size_t)r;
        }
        CHECK(packs == 3 && video == 3 && audio == 2 && ends == 1 && others == 2,
              "split %d: packs %d video %d audio %d end %d other %d", split, packs, video, audio,
              ends, others);
        CHECK(ptsOk, "split %d: time stamps or sub-stream", split);
        CHECK(gotEs.n == sizeof(es) && memcmp(gotEs.p, es, sizeof(es)) == 0,
              "split %d: video ES differs (%zu bytes)", split, gotEs.n);
        CHECK(gotAud.n == 40 + sizeof(pcm) && memcmp(gotAud.p + 40, pcm, sizeof(pcm)) == 0,
              "split %d: audio differs (%zu bytes)", split, gotAud.n);
    }
    {
        IcoPssAudioHeader h;
        CHECK(gotAud.n >= 40 && ico_pss_audio_header(gotAud.p, &h) == 0, "audio header");
        CHECK(h.type == 1 && h.rate == 48000 && h.channels == 2 && h.interleave == 512 &&
                  h.data_size == sizeof(pcm) && h.interleave_start == 0xFFFFFFFFu,
              "audio header fields %u %u %u %u %u", h.type, h.rate, h.channels, h.interleave,
              h.data_size);
    }
    /* garbage in front of a pack: -1, then resync finds the pack */
    {
        uint8_t junk[64];
        IcoPssPacket pkt;
        memset(junk, 0x55, sizeof(junk));
        memcpy(junk + 20, s.p, 14);
        CHECK(ico_pss_next(junk, sizeof(junk), &pkt) == -1, "junk is not a packet");
        CHECK(ico_pss_resync(junk, sizeof(junk)) == 20, "resync at the pack header");
    }
    free(s.p);
    free(gotEs.p);
    free(gotAud.p);
    printf("pss: done\n");
}

/* --- ES units ---------------------------------------------------------------- */

static void test_units(void)
{
    /* seq hdr, GOP, pic 0 (+ ext + slice), pic 1, pic 2, seq hdr, pic 3, end */
    /* clang-format off */
    static const uint8_t es[] = {
        0, 0, 1, 0xB3, 1, 2, 3, 4, 0, 0, 1, 0xB5, 9, 9,             /* 0: sequence + extension */
        0, 0, 1, 0xB8, 7, 7, 7, 7,                                  /* 14: GOP */
        0, 0, 1, 0x00, 1, 1, 0, 0, 1, 0xB5, 8, 8,                   /* 22: picture 0 + ext */
        0, 0, 1, 0x01, 0xAA, 0xBB,                                  /* 34: slice */
        0, 0, 1, 0x00, 2, 2, 0, 0, 1, 0x01, 0xCC,                   /* 40: picture 1 + slice */
        0, 0, 1, 0x00, 3, 3, 0, 0, 1, 0x01, 0xDD,                   /* 51: picture 2 + slice */
        0, 0, 1, 0xB3, 5, 5, 0, 0, 1, 0x00, 4, 0, 0, 1, 0x01, 0xEE, /* 62: seq + picture 3 */
        0, 0, 1, 0xB7                                               /* 78: end */
    };
    /* clang-format on */
    long e;
    size_t pos = 0;
    long ends[4] = {40, 51, 62, 78};
    int k;

    for (k = 0; k < 4; k++) {
        e = ico_pss_es_unit_end(es + pos, sizeof(es) - pos);
        CHECK(e >= 0 && (long)pos + e == ends[k], "unit %d ends at %ld, expected %ld", k,
              (long)pos + e, ends[k]);
        if (e < 0) {
            return;
        }
        pos += (size_t)e;
    }
    /* the end code alone: no picture, no boundary */
    CHECK(ico_pss_es_unit_end(es + pos, sizeof(es) - pos) == -1, "end code is no unit");
    /* a unit whose end is not in the buffer yet */
    CHECK(ico_pss_es_unit_end(es + 51, 8) == -1, "incomplete unit");
    CHECK(ico_pss_es_find_start(es + 4, 10) == 4, "find start");
    printf("units: done\n");
}

/* --- a hand-built MPEG-2 intra stream ------------------------------------------ */

/* dct_dc_size_luminance / chrominance VLCs (ISO/IEC 13818-2 tables B-12,
   B-13) for sizes 0..8 */
static const struct {
    uint32_t code;
    int len;
} kDcLum[9] = {{4, 3}, {0, 2}, {1, 2}, {5, 3}, {6, 3}, {14, 4}, {30, 5}, {62, 6}, {126, 7}},
  kDcChr[9] = {{0, 2}, {1, 2}, {2, 2}, {6, 3}, {14, 4}, {30, 5}, {62, 6}, {126, 7}, {254, 8}};

static void dc_block(Bits *w, int chroma, int diff)
{
    int size = 0, a = diff < 0 ? -diff : diff;

    while (a >> size) {
        size++;
    }
    bits(w, chroma ? kDcChr[size].code : kDcLum[size].code,
         chroma ? kDcChr[size].len : kDcLum[size].len);
    if (size > 0) {
        bits(w, (uint32_t)(diff > 0 ? diff : diff + (1 << size) - 1), size);
    }
    bits(w, 2, 2); /* end of block, table B-14 (intra_vlc_format 0) */
}

/* One I picture, 32 x 32, every macroblock flat: luma Y[mb], chroma Cb, Cr
   per macroblock. DC precision 8 bits: the predictor starts each slice at
   128 and a block's value is 128 + the running sum of its differences. */
static void build_stream(Buf *out, const int Y[4], const int Cb[4], const int Cr[4])
{
    Bits w = {out, 0, 0};
    int row, col;

    start_code(&w, 0xB3);
    bits(&w, 32, 12);
    bits(&w, 32, 12);
    bits(&w, 2, 4);     /* 4:3 */
    bits(&w, 3, 4);     /* 25 fps */
    bits(&w, 1000, 18); /* bit rate */
    bits(&w, 1, 1);
    bits(&w, 112, 10); /* vbv */
    bits(&w, 0, 3);    /* constrained, no matrices */
    start_code(&w, 0xB5);
    bits(&w, 1, 4);    /* sequence extension */
    bits(&w, 0x48, 8); /* Main@Main */
    bits(&w, 1, 1);    /* progressive */
    bits(&w, 1, 2);    /* 4:2:0 */
    bits(&w, 0, 4);
    bits(&w, 0, 12);
    bits(&w, 1, 1);
    bits(&w, 0, 8);
    bits(&w, 0, 1);
    bits(&w, 0, 7);
    start_code(&w, 0xB8);
    bits(&w, 1 << 12, 25); /* time code with its marker */
    bits(&w, 1, 1);        /* closed */
    bits(&w, 0, 1);
    start_code(&w, 0x00);
    bits(&w, 0, 10);
    bits(&w, 1, 3); /* I */
    bits(&w, 0xFFFF, 16);
    bits(&w, 0, 1);
    start_code(&w, 0xB5);
    bits(&w, 8, 4); /* picture coding extension */
    bits(&w, 0xFFFF, 16);
    bits(&w, 0, 2); /* intra_dc_precision 8 bits */
    bits(&w, 3, 2); /* frame picture */
    bits(&w, 0, 1);
    bits(&w, 1, 1); /* frame_pred_frame_dct */
    bits(&w, 0, 5); /* concealment, q_scale_type, intra_vlc_format, alternate_scan, rff */
    bits(&w, 1, 1); /* chroma_420_type */
    bits(&w, 1, 1); /* progressive_frame */
    bits(&w, 0, 1);
    for (row = 0; row < 2; row++) {
        int py = 128, pb = 128, pr = 128;

        start_code(&w, (unsigned)(row + 1));
        bits(&w, 8, 5); /* quantiser_scale_code */
        bits(&w, 0, 1);
        for (col = 0; col < 2; col++) {
            int mb = row * 2 + col, k;

            bits(&w, 1, 1); /* address increment 1 */
            bits(&w, 1, 1); /* intra */
            for (k = 0; k < 4; k++) {
                dc_block(&w, 0, Y[mb] - py);
                py = Y[mb];
            }
            dc_block(&w, 1, Cb[mb] - pb);
            pb = Cb[mb];
            dc_block(&w, 1, Cr[mb] - pr);
            pr = Cr[mb];
        }
    }
    start_code(&w, 0xB7);
}

static void test_decode(void)
{
    static const int Y[4] = {16, 235, 100, 180}, Cb[4] = {128, 90, 240, 16},
                     Cr[4] = {128, 200, 16, 240};
    Buf s = {0};
    IcoM2v *d = ico_m2v_create();
    IcoM2vFrame f;
    long e;
    int got = 0, r;
    uint32_t aw, ah, asp;

    CHECK(d != NULL, "decoder created");
    if (d == NULL) {
        return;
    }
    build_stream(&s, Y, Cb, Cr);
    e = ico_pss_es_unit_end(s.p, s.n);
    CHECK(e > 0 && (size_t)e == s.n - 4, "one unit before the end code (%ld of %zu)", e, s.n);
    r = ico_m2v_decode(d, s.p, (size_t)e, &f);
    CHECK(r >= 0, "decode returned %d", r);
    if (r == 1) {
        got = 1;
    } else {
        got = ico_m2v_flush(d, &f) == 1;
    }
    CHECK(got, "a picture came out");
    ico_m2v_seq_info(d, &aw, &ah, &asp);
    CHECK(aw == 32 && ah == 32 && asp == 2, "sequence info %u x %u aspect %u", aw, ah, asp);
    if (got) {
        int x, yy, bad = 0;
        CHECK(f.w == 32 && f.h == 32, "picture %u x %u", f.w, f.h);
        for (yy = 0; yy < 32 && f.w == 32 && f.h == 32; yy++) {
            for (x = 0; x < 32; x++) {
                int mb = (yy / 16) * 2 + x / 16;
                if (f.y[yy * f.pitch[0] + x] != Y[mb]) {
                    bad++;
                }
                if (x < 16 && yy < 16 &&
                    (f.u[yy * f.pitch[1] + x] != Cb[(yy / 8) * 2 + x / 8] ||
                     f.v[yy * f.pitch[2] + x] != Cr[(yy / 8) * 2 + x / 8])) {
                    bad++;
                }
            }
        }
        CHECK(bad == 0, "%d samples differ from the coded DC values", bad);
    }
    CHECK(ico_m2v_errors(d) == 0, "%u decode errors", ico_m2v_errors(d));
    ico_m2v_destroy(d);
    free(s.p);
    printf("decode: done\n");
}

/* --- pacing ---------------------------------------------------------------- */

/* Runs the state machine like movie.c: returns the vsyncs from the display
   start to the end; *shown the pictures shown. abortAt: the vsync the poll
   returns 1 (0 = never). */
static uint32_t run_pace(int total, uint32_t abortAt, uint32_t *shown, uint32_t *firstShow,
                         uint32_t *pollFrom, int *ended)
{
    IcoMoviePace p;
    uint32_t v = 0, lastShow = 0;
    int field = 0;

    ico_movie_pace_init(&p);
    while (ico_movie_pace_room(&p) && (int)p.decoded < total) {
        ico_movie_pace_put(&p);
    }
    ico_movie_pace_start(&p);
    *firstShow = 0;
    *pollFrom = 0;
    for (;;) {
        int ev;
        v++;
        field ^= 1; /* the display starts after an even field: the next is odd */
        ev = ico_movie_pace_vblank(&p, field);
        if (ev == ICO_PACE_SHOW) {
            if (*firstShow == 0) {
                *firstShow = v;
            } else if (v - lastShow != 2) {
                printf("FAIL: picture %u shown %u vsyncs after the last\n", p.shown, v - lastShow);
                failures++;
            }
            lastShow = v;
        }
        if (ico_movie_pace_poll_due(&p)) {
            if (*pollFrom == 0) {
                *pollFrom = v;
            }
            if (abortAt != 0 && v >= abortAt) {
                p.abort_pending = 1;
            }
        }
        if (p.abort_pending && ico_movie_pace_check_end(&p, total, ev == ICO_PACE_FREE)) {
            break;
        }
        if (!p.abort_pending) {
            while (ico_movie_pace_room(&p) && (int)p.decoded < total) {
                ico_movie_pace_put(&p);
            }
        }
        if (ico_movie_pace_check_end(&p, total, ev == ICO_PACE_FREE)) {
            break;
        }
        if (v > 100000) {
            break;
        }
    }
    *shown = p.shown;
    *ended = p.ended;
    return v;
}

static void test_pace(void)
{
    uint32_t shown, first, pollFrom, v;
    int ended;

    /* the disc's movie: 3438 pictures */
    v = run_pace(3438, 0, &shown, &first, &pollFrom, &ended);
    CHECK(ended == 1, "played out (%d)", ended);
    CHECK(first == 2, "first picture on the second vblank (even field), got %u", first);
    /* the last picture goes into the ring when picture N - 6 frees its slot:
       N - 5 pictures shown, 2 vsyncs each */
    CHECK(shown == 3438 - 5, "shown %u", shown);
    CHECK(v == 2 * (3438 - 5) + 1, "vsyncs %u", v);
    /* 25 pictures a second at 50 Hz: 137.3 s */
    CHECK(v / 50 == 137, "%u vsyncs = %u s", v, v / 50);
    /* the poll starts once 11 pictures are decoded: the sixth slot frees on
       vsync 13 and the decoder refills it after that vsync's poll, so the
       first poll is on vsync 14 */
    CHECK(pollFrom == 2 * 6 + 2, "poll from vsync %u", pollFrom);

    /* abort at vsync 100 (an odd field: picture 49's second field went out
       at 100 - 1?): the end comes at the next freed slot */
    v = run_pace(3438, 100, &shown, &first, &pollFrom, &ended);
    CHECK(ended == 2, "aborted (%d)", ended);
    CHECK(v == 101 && shown == 50, "abort: end at vsync %u, %u shown", v, shown);
    /* abort on a vsync that freed a slot ends at once */
    v = run_pace(3438, 101, &shown, &first, &pollFrom, &ended);
    CHECK(ended == 2 && v == 101, "abort on a freeing vsync: %u", v);

    /* a stream shorter than the ring: everything decoded before the start */
    v = run_pace(3, 0, &shown, &first, &pollFrom, &ended);
    CHECK(ended == 1 && shown == 0 && v == 1, "3 pictures: end at once (%u, %u shown)", v, shown);

    /* the timing is the stream's: same with any decode outcome (the state
       machine has no input for it); determinism over two runs */
    {
        uint32_t s2, f2, p2, v2;
        int e2;
        v = run_pace(500, 0, &shown, &first, &pollFrom, &ended);
        v2 = run_pace(500, 0, &s2, &f2, &p2, &e2);
        CHECK(v == v2 && shown == s2 && v == 2 * 495 + 1, "500 pictures: %u vsyncs", v);
    }
    printf("pace: done\n");
}

/* --- the disc ------------------------------------------------------------------ */

static void test_disc(const char *iso)
{
    FILE *f = iso != NULL ? fopen(iso, "rb") : NULL;
    uint8_t hdr[4];
    uint32_t count, i, lsn = 0, size = 0;
    const uint32_t dfLsn = 19771; /* DATA.DF on the PAL disc (docs/port/DATA.md) */
    uint8_t *buf;
    size_t pos = 0, n;
    Buf es = {0}, aud = {0};
    long pictures = 0;
    IcoM2v *d;
    int decoded = 0, packets = 0;

    if (f == NULL) {
        printf("disc: SKIP (no image)\n");
        return;
    }
    fseek(f, (long)dfLsn * 2048, SEEK_SET);
    if (fread(hdr, 1, 4, f) != 4) {
        fclose(f);
        return;
    }
    count =
        (uint32_t)hdr[0] | (uint32_t)hdr[1] << 8 | (uint32_t)hdr[2] << 16 | (uint32_t)hdr[3] << 24;
    for (i = 0; i < count && i < 4096; i++) {
        uint8_t e[40];
        if (fread(e, 1, 40, f) != 40) {
            break;
        }
        if (strcmp((const char *)e, "pal_advertise.pss") == 0) {
            lsn = dfLsn + ((uint32_t)e[32] | (uint32_t)e[33] << 8 | (uint32_t)e[34] << 16 |
                           (uint32_t)e[35] << 24) /
                              2048;
            size = (uint32_t)e[36] | (uint32_t)e[37] << 8 | (uint32_t)e[38] << 16 |
                   (uint32_t)e[39] << 24;
        }
    }
    if (size == 0) {
        printf("disc: SKIP (pal_advertise.pss not in DATA.DF: not the PAL disc)\n");
        fclose(f);
        return;
    }
    buf = malloc(size);
    fseek(f, (long)lsn * 2048, SEEK_SET);
    n = fread(buf, 1, size, f);
    fclose(f);
    CHECK(n == size, "read %zu of %u", n, size);
    while (pos < n) {
        IcoPssPacket pkt;
        long r = ico_pss_next(buf + pos, n - pos, &pkt);
        if (r <= 0) {
            CHECK(0, "parse stopped at %zu (%ld)", pos, r);
            break;
        }
        if (pkt.kind == ICO_PSS_VIDEO) {
            put(&es, pkt.data, pkt.len);
        } else if (pkt.kind == ICO_PSS_AUDIO) {
            put(&aud, pkt.data, pkt.len);
        }
        packets++;
        pos += (size_t)r;
        if (pkt.kind == ICO_PSS_END) {
            break;
        }
    }
    CHECK(pos == n, "parsed %zu of %zu bytes", pos, n);
    {
        IcoPssAudioHeader h;
        CHECK(aud.n >= 40 && ico_pss_audio_header(aud.p, &h) == 0, "disc audio header");
        CHECK(h.type == 1 && h.rate == 48000 && h.channels == 2 && h.interleave == 512 &&
                  h.data_size == aud.n - 40,
              "disc audio: type %u %u Hz %u ch interleave %u, %u data bytes, %zu carried", h.type,
              h.rate, h.channels, h.interleave, h.data_size, aud.n - 40);
    }
    /* pictures: every unit, the last one running to the end */
    d = ico_m2v_create();
    pos = 0;
    while (pos < es.n) {
        long s0 = ico_pss_es_find_start(es.p + pos, es.n - pos), e;
        IcoM2vFrame fr;
        if (s0 < 0) {
            break;
        }
        pos += (size_t)s0;
        if (es.p[pos + 3] == 0xB7) {
            pos += 4;
            continue;
        }
        e = ico_pss_es_unit_end(es.p + pos, es.n - pos);
        if (e < 0) {
            e = (long)(es.n - pos);
        }
        pictures++;
        if (d != NULL && pictures <= 60 && ico_m2v_decode(d, es.p + pos, (size_t)e, &fr) == 1) {
            decoded++;
            CHECK(fr.w == 720 && fr.h == 480, "disc picture %u x %u", fr.w, fr.h);
        }
        pos += (size_t)e;
    }
    printf("disc: %d packets, %zu video bytes, %zu audio bytes, %ld pictures, %d of the first 60 "
           "decoded, %u errors\n",
           packets, es.n, aud.n, pictures, decoded, ico_m2v_errors(d));
    CHECK(pictures == 3438, "pictures %ld", pictures);
    /* display order lags the coded order by the reference picture held
       back (I/P before the B pictures that precede them in display) */
    CHECK(decoded >= 58 && ico_m2v_errors(d) == 0, "decoded %d, %u errors", decoded,
          ico_m2v_errors(d));
    ico_m2v_destroy(d);
    free(buf);
    free(es.p);
    free(aud.p);
}

int main(int argc, char **argv)
{
    test_pss();
    test_units();
    test_decode();
    test_pace();
    test_disc(argc > 1 ? argv[1] : NULL);
    if (failures != 0) {
        printf("fmv_test: %d failures\n", failures);
        return 1;
    }
    printf("fmv_test: all passed\n");
    return 0;
}
