/*
 * port/fmv/movie.c
 *
 * The host's movie player: movie_init, movie_proc and movie_end with
 * ito/mpeg/mv_main.c's signatures and return values, for Main's movie path
 * (common/src/main.c: movie_init(&movieFile[mpegPlay * 0x20], 720, 576 or
 * 480, 36, 12, mono, mpegPlayInitColor), then ret = movie_proc(
 * movie_abort_check), ret == 1 marking the demo skipped).
 *
 * What the PS2 player did and what this one keeps:
 *
 *   file     strFileOpen -> iosCdvdDirectStOpen: "DFDATAS/<base name>"
 *            looked up in DATA.DF's directory, streamed with libcdvd's
 *            sceCdSt* through a 576-sector IOP ring.  Here: the same
 *            directory read through the port's VFS (no assert on a missing
 *            name: movie_init returns -1), the same sceCdSt* calls, the same
 *            IOP allocation for the ring, 64 KB reads.
 *   video    libmpeg's sceMpegDemuxPssRing + the IPU (sceMpegGetPicture)
 *            in a decode thread.  Here: pss.c and libmpeg2 (m2v.c), on this
 *            fiber, a picture per free display slot.
 *   timing   mv_disp.c's vblank handler: two vsyncs per picture, five
 *            pictures decoded ahead, the end when the last one is decoded.
 *            Here: movie_pace.c, driven once per simulated vsync; the
 *            timing depends only on the stream's picture count, so it is
 *            the same with or without decoding or a window.
 *   audio    mv_audiodec.c: the PCM after the 40-byte header goes through an
 *            EE ring into a 24576-byte IOP buffer read by SNDN2DRV's PCM
 *            channels 0 and 1 (SgStPcmOpen with 0x10400: 0x400 bytes a
 *            callback, step shift 1, i.e. 512-byte interleave blocks),
 *            preset full, then topped up behind SgStPcmIopReadAddr(0) in
 *            1 KB units.  Here: the same calls in the same order with the
 *            same numbers, the IOP copy a memcpy into port/data's IOP RAM.
 *   abort    movie_abort_check polled once a vsync after 11 pictures.
 *
 * The decode thread, the interrupt and DMA handler juggling, the GS
 * register setup and the read-starved pause (sceCdStStat never runs low on
 * the host) have no host counterpart.
 *
 * Headless: pictures are decoded (set ICO_FMV_DECODE=0 to skip decoding)
 * and dropped.  The window build hands them to rd_VideoFrame
 * (port/render/rd_video.c).
 */
#include <eekernel.h>
#include <eeregs.h>
#include <libcdvd.h>
#include <sifrpc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/ico_endian.h"
#include "diag_host.h"
#include "iop_ram.h"
#include "m2v.h"
#include "movie_pace.h"
#include "pss.h"
#include "sched.h"
#include "sound.h"
#include "vfs.h"

#ifdef ICO_RD

#include "rd_video.h"

#elif !defined(ICO_HEADLESS)
/* a window build that compiled this file without ICO_RD would drop every
   picture (port/fmv/CMakeLists.txt gives it the define) */
#error "movie.c: neither ICO_RD (window build) nor ICO_HEADLESS is defined"
#endif

/* the game's side */
void gsb_ClearFrameBuffer(void);
int sceGsSyncV(int mode);
int movie_init(char *name, int imageW, int imageH, int dbx, int dby, int mono, int clearCol);
void movie_end(void);
int movie_proc(int (*poll)(void));

#define READ_CHUNK 65536   /* readMpeg: strFileRead(strf, p, 65536, &eof) */
#define ST_SECTORS 576     /* iosCdvdDirectStOpen: sceCdStInit(576, 36, ...) */
#define IOP_PCM_SIZE 24576 /* audioDecCreate: size */
#define PCM_VOLUME 0x3FFF  /* movie_init: initAll(..., 0x3FFF, ...) */

typedef struct ByteBuf {
    uint8_t *p;
    size_t len, pos, cap; /* bytes [pos, len) are live */
} ByteBuf;

typedef struct Slot {
    int valid;
    uint32_t w, h;
    uint8_t *planes; /* y (w * h), u, v ((w+1)/2 * (h+1)/2 each), tightly packed */
    size_t cap;
} Slot;

static struct {
    int open;
    int imageW, imageH, mono;
    uint32_t lsn, size; /* the PSS on the disc */
    uint32_t read;      /* bytes read from it */
    int stMem;          /* the IOP block of the stream ring */
    int eof;            /* every byte read and parsed, or the end code seen */
    ByteBuf in;         /* read, not yet parsed */
    ByteBuf es;         /* video elementary stream */
    size_t scanPos;     /* the unit scan's position in es (absolute offset into es.p) */
    uint64_t units;     /* complete pictures found by the scan */
    int64_t total;      /* pictures in the stream, -1 until the end is reached */
    /* decoding */
    int decode;
    IcoM2v *dec;
    size_t decPos;     /* the decoder's position in es */
    uint64_t decUnits; /* access units handed to the decoder */
    int decFlushed;
    Slot slots[ICO_MOVIE_SLOTS];
    int slotPut, slotShow;
    uint32_t decodedFrames;
    int loggedPicture; /* the first picture's size logged */
    /* audio (mv_audiodec.c's AudioDec) */
    uint8_t hdr[ICO_PSS_AUDIO_HEADER_SIZE];
    int hdrBytes;
    IcoPssAudioHeader ah;
    ByteBuf pcm; /* the EE side: PCM not yet sent to the IOP */
    int aState;  /* 0 header, 1 preset, 2 playing */
    int iopBuf;  /* IOP address */
    uint32_t sentTotal, iopPos;
    uint32_t pcmSent; /* every byte sent, for the log */
    int pcmInited, ch0Open, ch1Open;
    int sawAudio;
    /* pacing */
    IcoMoviePace pace;
    uint32_t vsyncs;
    int callerPri;
} mv;

/* --- byte buffers ------------------------------------------------------------- */

static int bb_reserve(ByteBuf *b, size_t more)
{
    if (b->pos > 0 && b->pos >= b->len / 2) {
        memmove(b->p, b->p + b->pos, b->len - b->pos);
        b->len -= b->pos;
        b->pos = 0;
    }
    if (b->len + more > b->cap) {
        size_t cap = b->cap ? b->cap : 1u << 16;
        uint8_t *np;

        while (cap < b->len + more) {
            cap *= 2;
        }
        np = realloc(b->p, cap);
        if (np == NULL) {
            return -1;
        }
        b->p = np;
        b->cap = cap;
    }
    return 0;
}

static void bb_append(ByteBuf *b, const uint8_t *src, size_t n)
{
    if (n == 0 || bb_reserve(b, n) != 0) {
        return;
    }
    memcpy(b->p + b->len, src, n);
    b->len += n;
}

static void bb_free(ByteBuf *b)
{
    free(b->p);
    memset(b, 0, sizeof(*b));
}

/* The ES buffer is shared by the unit scan and the decoder: it may drop
   only what both have passed. */
static void es_compact(void)
{
    size_t keep = mv.decode ? (mv.decPos < mv.scanPos ? mv.decPos : mv.scanPos) : mv.scanPos;

    if (keep > 0 && keep >= mv.es.len / 2) {
        memmove(mv.es.p, mv.es.p + keep, mv.es.len - keep);
        mv.es.len -= keep;
        mv.scanPos -= keep;
        mv.decPos -= mv.decPos >= keep ? keep : mv.decPos;
    }
}

/* --- the file ------------------------------------------------------------------ */

/* DATA.DF's directory (fumi/ios/cdvd.c unifile_read_func): a count, then
   per file 32 bytes of name, the byte offset in DATA.DF and the size. */
static int find_in_datadf(const char *base, uint32_t *lsn, uint32_t *size)
{
    IcoVfs *vfs = ico_vfs_disc();
    IcoVfsEntry df;
    uint8_t sec[ICO_VFS_SECTOR];
    uint8_t *dir = NULL;
    uint32_t count, bytes, secs, i;
    int found = -1;

    if (vfs == NULL || ico_vfs_stat(vfs, "DFDATAS/DATA.DF", &df) != 0) {
        return -1;
    }
    if (ico_vfs_read_sectors(vfs, df.lsn, 1, sec) != 0) {
        return -1;
    }
    count = ico_le32(sec);
    if (count == 0 || count > 4096) {
        return -1;
    }
    bytes = 4 + count * 40;
    secs = ico_vfs_size_to_sectors(bytes);
    dir = malloc((size_t)secs * ICO_VFS_SECTOR);
    if (dir == NULL || ico_vfs_read_sectors(vfs, df.lsn, secs, dir) != 0) {
        free(dir);
        return -1;
    }
    for (i = 0; i < count; i++) {
        const uint8_t *e = dir + 4 + i * 40;
        char nm[33];
        uint32_t off = ico_le32(e + 32);
        uint32_t sz = ico_le32(e + 36);
        size_t k;

        memcpy(nm, e, 32);
        nm[32] = '\0';
        for (k = 0; base[k] != '\0' && nm[k] != '\0'; k++) {
            char a = base[k], b = nm[k];
            if (a >= 'a' && a <= 'z') {
                a = (char)(a - 32);
            }
            if (b >= 'a' && b <= 'z') {
                b = (char)(b - 32);
            }
            if (a != b) {
                break;
            }
        }
        if (base[k] == '\0' && nm[k] == '\0') {
            *lsn = df.lsn + off / ICO_VFS_SECTOR;
            *size = sz;
            found = 0;
            break;
        }
    }
    free(dir);
    return found;
}

/* One strFileRead: up to 64 KB of whole sectors from the stream, the bytes
   past the file's end dropped. */
static int read_chunk(void)
{
    int err = 0;
    int got;
    uint32_t want;

    if (mv.read >= mv.size) {
        return 0;
    }
    if (bb_reserve(&mv.in, READ_CHUNK) != 0) {
        /* out of memory: the stream ends here (demux_until would otherwise
           retry the read for ever) and the movie ends cleanly */
        ico_diag_log("fmv: no memory for the stream at byte %u of %u: the movie ends", mv.read,
                     mv.size);
        mv.read = mv.size;
        return -1;
    }
    got = sceCdStRead(READ_CHUNK / ICO_VFS_SECTOR, mv.in.p + mv.in.len, 1, &err) * ICO_VFS_SECTOR;
    if (got <= 0 || err != 0) {
        ico_diag_log("fmv: stream read failed at byte %u of %u (error %d)", mv.read, mv.size, err);
        mv.read = mv.size;
        return -1;
    }
    want = mv.size - mv.read;
    if ((uint32_t)got > want) {
        got = (int)want;
    }
    mv.in.len += (size_t)got;
    mv.read += (uint32_t)got;
    return got;
}

/* --- demux ---------------------------------------------------------------------- */

static void audio_put(const uint8_t *p, size_t n)
{
    mv.sawAudio = 1;
    if (mv.hdrBytes < ICO_PSS_AUDIO_HEADER_SIZE) {
        size_t k = (size_t)(ICO_PSS_AUDIO_HEADER_SIZE - mv.hdrBytes);
        if (k > n) {
            k = n;
        }
        memcpy(mv.hdr + mv.hdrBytes, p, k);
        mv.hdrBytes += (int)k;
        p += k;
        n -= k;
        if (mv.hdrBytes == ICO_PSS_AUDIO_HEADER_SIZE) {
            int ok = ico_pss_audio_header(mv.hdr, &mv.ah);
            ico_diag_log("fmv: audio %s type %u, %u Hz, %u channels, interleave %u, %u bytes",
                         ok == 0 ? "SShd" : "(bad header)", mv.ah.type, mv.ah.rate, mv.ah.channels,
                         mv.ah.interleave, mv.ah.data_size);
            if (ok != 0 || mv.ah.type != 1 || mv.ah.rate != 48000 || mv.ah.channels != 2 ||
                mv.ah.interleave != 512) {
                ico_diag_log("fmv: the audio is not what SNDN2DRV's PCM path plays (16-bit "
                             "little-endian, 48 kHz, 2 x 512-byte blocks); sent as is, as on "
                             "the PS2");
            }
            mv.aState = 1; /* audioDecEndPut: the header is in */
        }
    }
    bb_append(&mv.pcm, p, n);
}

/* Parses what has been read.  Returns 1 when something was consumed. */
static int parse_input(void)
{
    int any = 0;

    while (!mv.eof && mv.in.len > mv.in.pos) {
        IcoPssPacket pkt;
        const uint8_t *b = mv.in.p + mv.in.pos;
        size_t n = mv.in.len - mv.in.pos;
        long r = ico_pss_next(b, n, &pkt);

        if (r == 0) {
            if (mv.read >= mv.size) {
                mv.in.pos = mv.in.len; /* a truncated last packet */
            }
            break;
        }
        if (r < 0) {
            mv.in.pos += ico_pss_resync(b, n);
            any = 1;
            continue;
        }
        if (pkt.kind == ICO_PSS_VIDEO) {
            bb_append(&mv.es, pkt.data, pkt.len);
        } else if (pkt.kind == ICO_PSS_AUDIO && pkt.sub_id == 0xA0) {
            audio_put(pkt.data, pkt.len); /* videoDecSetStream(&videoDec, 2, 0, ...) */
        } else if (pkt.kind == ICO_PSS_END) {
            mv.eof = 1;
        }
        mv.in.pos += (size_t)r;
        any = 1;
    }
    if (!mv.eof && mv.read >= mv.size && mv.in.pos >= mv.in.len) {
        mv.eof = 1;
    }
    return any;
}

/* Counts the complete pictures in es past the scan position; at the end
   of the stream, the rest is one more unit when it holds a picture. */
static void scan_units(void)
{
    for (;;) {
        long s = ico_pss_es_find_start(mv.es.p + mv.scanPos, mv.es.len - mv.scanPos);
        long e;

        if (s < 0) {
            break;
        }
        mv.scanPos += (size_t)s;
        e = ico_pss_es_unit_end(mv.es.p + mv.scanPos, mv.es.len - mv.scanPos);
        if (e < 0) {
            break;
        }
        mv.scanPos += (size_t)e;
        mv.units++;
    }
    if (mv.eof && mv.total < 0) {
        /* the last unit: from the scan position to the end, if it has a
           picture start code */
        const uint8_t *p = mv.es.p + mv.scanPos;
        size_t n = mv.es.len - mv.scanPos, i;

        for (i = 0; i + 4 <= n; i++) {
            if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1 && p[i + 3] == 0) {
                mv.units++;
                break;
            }
        }
        mv.scanPos = mv.es.len;
        mv.total = (int64_t)mv.units;
    }
}

/* Reads and parses until cond() holds or the stream ends. */
static void demux_until(int (*cond)(void))
{
    while (!cond() && !mv.eof) {
        if (!parse_input() || mv.in.pos >= mv.in.len) {
            if (mv.read < mv.size) {
                if (read_chunk() < 0) {
                    continue;
                }
            }
            parse_input();
        }
        scan_units();
    }
    scan_units();
}

/* a picture is there for the next free slot, the decoder's next unit and
   one more are in es (the decoder may run a picture ahead), and the audio
   has enough for the next send */
static int have_next_picture(void)
{
    return mv.units > (uint64_t)mv.pace.decoded + 1 &&
           (!mv.sawAudio ? mv.es.len > 0 : mv.pcm.len - mv.pcm.pos >= IOP_PCM_SIZE);
}

/* --- decoding ---------------------------------------------------------------- */

/* The decoder's next access unit in es, or 0 when none is complete. */
static int next_unit(const uint8_t **p, size_t *n)
{
    for (;;) {
        const uint8_t *b = mv.es.p + mv.decPos;
        size_t len = mv.es.len - mv.decPos;
        long s = ico_pss_es_find_start(b, len);
        long e;

        if (s < 0) {
            return 0;
        }
        b += s;
        len -= (size_t)s;
        mv.decPos += (size_t)s;
        if (b[3] == 0xB7) { /* sequence end code: no picture */
            mv.decPos += 4;
            continue;
        }
        e = ico_pss_es_unit_end(b, len);
        if (e < 0) {
            if (!mv.eof) {
                return 0;
            }
            e = (long)len;
        }
        *p = b;
        *n = (size_t)e;
        mv.decPos += (size_t)e;
        mv.decUnits++;
        return e > 0;
    }
}

static void keep_frame(Slot *s, const IcoM2vFrame *f)
{
    uint32_t cw = (f->w + 1) / 2, ch = (f->h + 1) / 2, y;
    size_t need = (size_t)f->w * f->h + 2 * (size_t)cw * ch;
    uint8_t *d;

    if (!mv.loggedPicture) {
        /* the decoded size beside movie_init's display area: a picture
           larger than the area is fitted into it (rd_video.c drawPicture) */
        mv.loggedPicture = 1;
        ico_diag_log("fmv: picture %u x %u in a %d x %d area", f->w, f->h, mv.imageW, mv.imageH);
    }
    if (s->cap < need) {
        uint8_t *np = realloc(s->planes, need);
        if (np == NULL) {
            s->valid = 0;
            return;
        }
        s->planes = np;
        s->cap = need;
    }
    d = s->planes;
    for (y = 0; y < f->h; y++) {
        memcpy(d + (size_t)y * f->w, f->y + (size_t)y * f->pitch[0], f->w);
    }
    d += (size_t)f->w * f->h;
    for (y = 0; y < ch; y++) {
        memcpy(d + (size_t)y * cw, f->u + (size_t)y * f->pitch[1], cw);
        memcpy(d + (size_t)cw * ch + (size_t)y * cw, f->v + (size_t)y * f->pitch[2], cw);
    }
    s->w = f->w;
    s->h = f->h;
    s->valid = 1;
}

/* the decoder's next unit is complete in es (it runs up to two units ahead
   of the pictures it has put out: an I or P picture is held until the next
   reference arrives) */
static int decoder_has_unit(void)
{
    return mv.units > mv.decUnits;
}

/* sceMpegGetPicture: the next picture in display order into the slot. */
static void decode_into(Slot *s)
{
    IcoM2vFrame f;

    s->valid = 0;
    if (!mv.decode || mv.dec == NULL) {
        return;
    }
    for (;;) {
        const uint8_t *p;
        size_t n;
        int r;

        if (!mv.decFlushed && !mv.eof && !decoder_has_unit()) {
            demux_until(decoder_has_unit);
        }
        if (!mv.decFlushed && next_unit(&p, &n)) {
            r = ico_m2v_decode(mv.dec, p, n, &f);
            if (r == 1) {
                keep_frame(s, &f);
                mv.decodedFrames++;
                return;
            }
            continue;
        }
        if (!mv.eof) {
            return; /* not enough data: the slot shows nothing new */
        }
        mv.decFlushed = 1;
        if (ico_m2v_flush(mv.dec, &f) == 1) {
            keep_frame(s, &f);
            mv.decodedFrames++;
        }
        return;
    }
}

/* Fills the free slots (decBitStrm0's loop). */
static void fill_slots(void)
{
    while (ico_movie_pace_room(&mv.pace)) {
        demux_until(have_next_picture);
        if (mv.total >= 0 ? mv.pace.decoded >= (uint64_t)mv.total
                          : mv.units <= (uint64_t)mv.pace.decoded) {
            return;
        }
        decode_into(&mv.slots[mv.slotPut]);
        mv.slotPut = (mv.slotPut + 1) % ICO_MOVIE_SLOTS;
        ico_movie_pace_put(&mv.pace);
        es_compact();
    }
}

/* --- audio (mv_audiodec.c) -------------------------------------------------- */

static void iop_write(uint32_t dst, const uint8_t *src, uint32_t n)
{
    if (n > 0 && ico_iop_range_ok(dst, n)) {
        memcpy(ico_iop_ptr(dst), src, n);
    }
}

/* audioDecSendToIOP: whole kilobytes of PCM into the IOP buffer, the free
   room in front of the read position when playing. */
static void audio_send(void)
{
    uint32_t room, avail, n, first;

    if (mv.aState == 0 || mv.iopBuf == 0) {
        return;
    }
    if (mv.aState == 1) {
        room = IOP_PCM_SIZE - mv.sentTotal;
    } else {
        uint32_t rd = (uint32_t)SgStPcmIopReadAddr(0);
        room = (rd + IOP_PCM_SIZE - mv.iopPos - 1024) % IOP_PCM_SIZE;
        room = room / 1024 * 1024;
    }
    avail = (uint32_t)(mv.pcm.len - mv.pcm.pos);
    /* the EE ring holds at most 49152 bytes (audioDecCreate: bufsize) */
    if (avail > 49152) {
        avail = 49152;
    }
    avail = avail / 1024 * 1024;
    if (room < 1024 || avail < 1024) {
        return;
    }
    n = avail < room ? avail : room;
    first = IOP_PCM_SIZE - mv.iopPos;
    if (first > n) {
        first = n;
    }
    iop_write((uint32_t)mv.iopBuf + mv.iopPos, mv.pcm.p + mv.pcm.pos, first);
    iop_write((uint32_t)mv.iopBuf, mv.pcm.p + mv.pcm.pos + first, n - first);
    mv.pcm.pos += n;
    mv.sentTotal += n;
    mv.pcmSent += n;
    mv.iopPos = (mv.iopPos + n) % IOP_PCM_SIZE;
    if (mv.pcm.pos > (1u << 20)) {
        bb_reserve(&mv.pcm, 0);
    }
}

static int audio_is_preset(void)
{
    return !mv.sawAudio ? mv.eof : mv.sentTotal >= IOP_PCM_SIZE;
}

/* audioDecStart */
static void audio_start(void)
{
    if (!mv.sawAudio) {
        return;
    }
    SgStPcmLseek(0, 0);
    SgStPcmLseek(1, 0);
    if (mv.mono) {
        int half = PCM_VOLUME / 2;
        SgStPcmVolume(3, half, half);
    } else {
        SgStPcmVolume(1, 0, PCM_VOLUME);
        SgStPcmVolume(2, PCM_VOLUME, 0);
    }
    SgStPcmPlay(3);
    mv.aState = 2;
}

/* audioDecReset */
static void audio_reset(void)
{
    SgStPcmVolume(3, 0, 0);
    SgStPcmStop(3);
    mv.aState = 0;
    mv.sentTotal = 0;
    mv.iopPos = 0;
}

/* --- display ------------------------------------------------------------------ */

static void show_clear(unsigned int col)
{
#ifdef ICO_RD
    uint8_t rgba[4] = {(uint8_t)col, (uint8_t)(col >> 8), (uint8_t)(col >> 16),
                       (uint8_t)(col >> 24)};
    rd_VideoClear(rgba);
#else
    (void)col;
#endif
}

static void show_slot(const Slot *s)
{
#ifdef ICO_RD
    if (s->valid) {
        uint32_t cw = (s->w + 1) / 2, ch = (s->h + 1) / 2;
        const uint8_t *y = s->planes;
        const uint8_t *u = y + (size_t)s->w * s->h;
        const uint8_t *v = u + (size_t)cw * ch;
        uint32_t pitch[3] = {s->w, cw, cw};
        rd_VideoFrame(y, u, v, pitch, s->w, s->h);
    }
#else
    (void)s;
#endif
}

/* --- the entry points ------------------------------------------------------------ */

static void close_all(void)
{
    int i;

    if (mv.stMem != 0) {
        sceCdStStop(); /* strFileClose */
        sceSifFreeIopHeap(mv.stMem);
        mv.stMem = 0;
    }
    if (mv.iopBuf != 0) {
        sceSifFreeIopHeap(mv.iopBuf);
        mv.iopBuf = 0;
    }
    if (mv.ch0Open) {
        SgStPcmClose(0);
    }
    if (mv.ch1Open) {
        SgStPcmClose(1);
    }
    if (mv.pcmInited) {
        SgStPcmQuit();
    }
    mv.ch0Open = mv.ch1Open = mv.pcmInited = 0;
    ico_m2v_destroy(mv.dec);
    mv.dec = NULL;
    bb_free(&mv.in);
    bb_free(&mv.es);
    bb_free(&mv.pcm);
    for (i = 0; i < ICO_MOVIE_SLOTS; i++) {
        free(mv.slots[i].planes);
        memset(&mv.slots[i], 0, sizeof(mv.slots[i]));
    }
    mv.open = 0;
}

int movie_init(char *name, int imageW, int imageH, int dbx, int dby, int mono, int clearCol)
{
    struct ThreadParam st;
    const char *base, *env;
    CdRMode mode;
    int pcm[4];

    (void)dbx;
    (void)dby;
    if (mv.open) {
        close_all();
    }
    memset(&mv, 0, sizeof(mv));
    mv.total = -1;
    mv.imageW = imageW;
    mv.imageH = imageH;
    mv.mono = mono;
    env = getenv("ICO_FMV_DECODE");
    mv.decode = !(env != NULL && strcmp(env, "0") == 0);
    ico_movie_pace_init(&mv.pace);
    mv.pace.on_show = ico_diag_note_progress;

    ReferThreadStatus(GetThreadId(), &st);
    mv.callerPri = st.currentPriority;

    /* dispCreate: one vsync, then the movie's display; dispClear */
    sceGsSyncV(0);
#ifdef ICO_RD
    rd_VideoSetDisplay((uint32_t)imageW, (uint32_t)imageH);
#endif
    show_clear((unsigned int)clearCol);

    /* strFileOpen: the base name under DFDATAS/ */
    base = strrchr(name, '/');
    base = base != NULL ? base + 1 : name;
    ico_diag_log("fmv: movie_init %s (%d x %d, %s)", name, imageW, imageH,
                 mono ? "mono" : "stereo");
    if (find_in_datadf(base, &mv.lsn, &mv.size) != 0) {
        ico_diag_log("fmv: %s is not in DATA.DF's directory", base);
        return -1;
    }
    mv.stMem = sceSifAllocIopHeap(ST_SECTORS * ICO_VFS_SECTOR + 16);
    memset(&mode, 0, sizeof(mode));
    sceCdStInit(ST_SECTORS, 36, (void *)(uintptr_t)((mv.stMem + 15) & ~15));
    if (!sceCdStStart((int)mv.lsn, &mode)) {
        ico_diag_log("fmv: sceCdStStart(%u) failed", mv.lsn);
        close_all();
        return -1;
    }
    mv.open = 1;

    if (mv.decode) {
        mv.dec = ico_m2v_create();
        if (mv.dec == NULL) {
            ico_diag_log("fmv: the MPEG-2 decoder could not be created; pictures are dropped");
        }
    }

    /* audioDecCreate */
    mv.iopBuf = sceSifAllocIopHeap(IOP_PCM_SIZE);
    if (mv.iopBuf == 0) {
        ico_diag_log("fmv: no IOP memory for the PCM buffer");
    } else {
        memset(ico_iop_ptr((uint32_t)mv.iopBuf), 0, IOP_PCM_SIZE);
        SgStPcmInit();
        mv.pcmInited = 1;
        pcm[0] = 0;
        pcm[1] = 0x10400;
        pcm[2] = mv.iopBuf;
        pcm[3] = IOP_PCM_SIZE;
        mv.ch0Open = SgStPcmOpen(pcm) == 0;
        pcm[0] = 1;
        pcm[2] = mv.iopBuf + 0x200;
        mv.ch1Open = SgStPcmOpen(pcm) == 0;
        if (!mv.ch0Open || !mv.ch1Open) {
            close_all();
            return -1;
        }
        SgStPcmSetEffect(8);
    }
    return 0;
}

/* One vsync of the busy loop: the higher-priority threads run at the
   vblank as on the PS2, and same-priority ones get the CPU as readMpeg's
   RotateThreadReadyQueue gave it to them. */
static int next_vsync(void)
{
    RotateThreadReadyQueue(mv.callerPri);
    ico_sched_spin_vsync();
    mv.vsyncs++;
    return (int)((*GS_CSR >> 13) & 1);
}

static int readMpeg(int (*poll)(void))
{
    int abort = 0;

    /* the whole movie runs inside this call, inside one Main tick: the
       watchdog hears from it through the progress count instead (each
       preroll vsync here, each shown picture through pace.on_show) */
    ico_diag_set_movie(1);

    /* preroll: the ring full and the IOP buffer preset (no vsync passes
       on the host: the reads are immediate) */
    for (;;) {
        int full, preset;

        fill_slots();
        audio_send();
        full = !ico_movie_pace_room(&mv.pace) ||
               (mv.total >= 0 && mv.pace.decoded >= (uint64_t)mv.total);
        preset = audio_is_preset();
        if ((full && preset) || mv.eof) {
            break;
        }
        next_vsync();
        ico_diag_note_progress();
    }
    /* startDisplay(1): wait for a vblank that reports the even field */
    while (sceGsSyncV(0) == 1) {
        mv.vsyncs++;
    }
    mv.vsyncs++;
    ico_movie_pace_start(&mv.pace);
    audio_start();

    for (;;) {
        int field = next_vsync();
        int ev = ico_movie_pace_vblank(&mv.pace, field);

        if (ev == ICO_PACE_SHOW) {
            show_slot(&mv.slots[mv.slotShow]);
        } else if (ev == ICO_PACE_FREE) {
            mv.slotShow = (mv.slotShow + 1) % ICO_MOVIE_SLOTS;
        }
        if (ico_movie_pace_poll_due(&mv.pace) && poll != NULL && poll() != 0) {
            mv.pace.abort_pending = 1; /* videoDecAbort */
            abort = 1;
        }
        if (mv.pace.abort_pending &&
            ico_movie_pace_check_end(&mv.pace, mv.total, ev == ICO_PACE_FREE)) {
            break;
        }
        if (!mv.pace.abort_pending) {
            fill_slots();
        }
        audio_send();
        if (ico_movie_pace_check_end(&mv.pace, mv.total, ev == ICO_PACE_FREE)) {
            break;
        }
    }

    gsb_ClearFrameBuffer();
    if (mv.sawAudio) {
        audio_reset();
    }
    ico_diag_set_movie(0);
    return abort;
}

void movie_end(void)
{
    show_clear(0x80000000u); /* dispClear(&display, 0x80000000) */
    close_all();
}

int movie_proc(int (*poll)(void))
{
    int r = 0;

    if (mv.open) {
        char presents[64] = "";
#ifdef ICO_RD
        uint32_t failed0, failed1;
        const uint32_t presents0 = rd_VideoPresents(&failed0);
#endif
        r = readMpeg(poll);
#ifdef ICO_RD
        {
            /* what reached the window, beside the pictures the timing
               showed: fewer presents than shown pictures means the screen
               missed some (or decoding was off) */
            const uint32_t presents1 = rd_VideoPresents(&failed1);
            snprintf(presents, sizeof presents, ", %u movie presents (%u failed)",
                     presents1 - presents0, failed1 - failed0);
        }
#endif
        ico_diag_log("fmv: movie_proc %s after %u vsyncs: %u pictures in the stream%s, %u "
                     "into the display ring, %u shown, %u decoded (%u decoder errors), %u PCM "
                     "bytes sent%s",
                     r ? "aborted" : "played", mv.vsyncs,
                     (unsigned)(mv.total >= 0 ? mv.total : (int64_t)mv.units),
                     mv.total >= 0 ? "" : " so far", mv.pace.decoded, mv.pace.shown,
                     mv.decodedFrames, ico_m2v_errors(mv.dec), mv.pcmSent, presents);
    } else {
        ico_diag_log("fmv: movie_proc without an open movie: returns 0");
    }
    movie_end();
    return r;
}
