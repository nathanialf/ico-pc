/*
 * port/audio/test/adpcm_stream_test.c
 *
 * The game's ADPCM streams through the real driver: the game's own
 * adpcm_init.c (open, the ring refills, the tick's pitch, fade and loop
 * count, the close) on the real Sg sequencer (sce/libsndn2/sound.c), the
 * SNDN2DRV host (sndn2_host.c, stream.c) and the software SPU2, with only
 * the game's sound-data table, its SPU channel allocator and the disc
 * reader stood in for.  The disc reader serves synthesized ADPCM: every
 * stream is a square wave (filter 0, 14 samples at +7 then 14 at -7 a
 * block) at a shift of its own, so each stream's level in the mix is
 * known and a stream that plays silently shows.
 *
 * One simulated vsync (50 Hz) is, in the game's order: the SPU2 renders
 * 960 frames, the disc reader runs each background request once
 * (iosCdvdBackGroundMgr), soundVBlank runs adpcmTickProc2 for each open
 * stream, and the sound thread's SgCalledTickProc sends the packet page.
 *
 *   1. reopen after a fade (issue 101): a stream faded out to its close,
 *      then a new stream on the same two voices: its VOL is 0x3FFF, its
 *      ENVX rises, and it is heard at its own level;
 *   2. the ending's shape: six streams, each opened while the one before
 *      plays and that one closed at once, slots and voice pairs
 *      alternating; each stream's level, alone, against its amplitude;
 *      the log watch's lines for every stream;
 *   3. a stream of about a minute: stream 43's length (ed1, the first of
 *      the silent four: 57.8 s, 1423 sectors) at 1x pitch with loopNum 1:
 *      7 wraps of the 0x5C000 IOP ring, no 10 ms of silence from the key-on
 *      to the close, the close at the file's length, the half-way line.
 *      About 2,900 vsyncs.  The SPU2 renders 48 voices whatever their
 *      envelope, about 0.8 ms a vsync here, so this part takes about 2.4 s
 *      (the sea song's 265.6 s ran 13,254 vsyncs in 10.8 s with the same
 *      result: 35 wraps, no silence, the close 0.5 s before the file's
 *      end); the test prints its wall time.
 *
 * Exit status 0 when every check passes.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../../ico2/fumi/sound/adpcm_init.c"

#include "iop_ram.h"
#include "sif_host.h"
#include "sndn2_host.h"
#include "spu2.h"
#include "spu2_sd.h"
#include "stream_watch.h"

static int failures;
static int checks;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- the stream files --------------------------------------------------------- */

/* the ending's streams, their lengths in sectors as on the disc (ed1..ed4,
   the sea song) and two more for the shape; pitch 44100 is 1x */
const AdpcmDataRec adpcmFile[64] = {
    [43] = {.sectors = 1423, .pitch = 44100, .channels = 2},
    [44] = {.sectors = 1324, .pitch = 44100, .channels = 2},
    [45] = {.sectors = 379, .pitch = 44100, .channels = 2},
    [46] = {.sectors = 1056, .pitch = 44100, .channels = 2},
    [47] = {.sectors = 6536, .pitch = 44100, .channels = 2},
    [48] = {.sectors = 600, .pitch = 44100, .channels = 2},
};

/* the stream's square: amplitude (7 << 12) >> shift */
static int stream_shift(int no)
{
    return 2 + (no % 3); /* 43: 3, 44: 4, 45: 2, ... */
}

/* byte `off` of stream `no`'s file: 16-byte blocks of shift, flags 0 and
   14 bytes of nibbles (+7 in the first seven bytes, -7 = 9 in the rest) */
static uint8_t file_byte(int no, uint32_t off)
{
    uint32_t b = off & 15;

    if (b == 0) {
        return (uint8_t)stream_shift(no);
    }
    if (b == 1) {
        return 0;
    }
    return b < 9 ? 0x77 : 0x99;
}

/* --- the game's sound-data table and SPU channels (s_init.c's rules) ------------ */

static SqEntry sqTbl[16];
static long long chMask;

SqEntry *soundDataAreaGet(int no, int bank, int mode, int seg)
{
    int i;

    for (i = 0; i < 16; i++) {
        if (*(int *)&sqTbl[i] == 0) {
            memset(&sqTbl[i], 0, sizeof sqTbl[i]);
            sqTbl[i].num = (unsigned short)no;
            sqTbl[i].bank = (unsigned short)bank;
            sqTbl[i].mode = (unsigned short)mode;
            sqTbl[i].seg = (unsigned short)seg;
            return &sqTbl[i];
        }
    }
    return 0;
}

SqEntry *soundDataAreaSearch(int *pk)
{
    int i;

    for (i = 0; i < 16; i++) {
        if (*(int *)&sqTbl[i] == *pk) {
            return &sqTbl[i];
        }
    }
    return 0;
}

void soundDataClose(SqEntry *self)
{
    if (self->mode == 2) {
        AdpcmClose(self);
    }
    *(int *)self = 0;
}

int soundBufAdpcmChAlloc(SqEntry *self, int *chp)
{
    int ch;

    for (ch = 0; ch < 4 && (chMask >> ch & 1) != 0; ch++) {}
    CHECK(ch < 4);
    self->spu.chMask |= 1LL << ch;
    chMask |= 1LL << ch;
    *chp = ch;
    return 0x1E0000 + (ch << 14);
}

void soundBufAdpcmFree(SqEntry *self)
{
    chMask &= ~self->spu.chMask;
    self->spu.chMask = 0;
}

int soundOutputModeGet(void)
{
    return 0; /* stereo */
}

int Ee2Iop(ICO_WORD ee, int iop, int size)
{
    (void)ee;
    (void)iop;
    (void)size;
    return 0;
}

int iosSifAllocIopHeapDebug(int size, const char *file, int line)
{
    (void)file;
    (void)line;
    return (int)ico_iop_heap_alloc((uint32_t)size);
}

void __assert(const char *f, int l, const char *e)
{
    printf("FAIL: assert %s:%d %s\n", f, l, e);
    failures++;
}

void debug_assert(const char *f, int l)
{
    (void)f;
    (void)l;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

/* --- the disc reader (fumi/ios/cdvd.c's background manager) ---------------------- */

static CdvdBgReq bgTbl[7];

int iosCdvdDiskStatusGet(void)
{
    return 0;
}

static int file_of(const char *name)
{
    return (int)((const AdpcmDataRec *)(const void *)name - adpcmFile);
}

CdvdBgReq *iosCdvdBackGroundMgrAdd(const char *name, void *readFunc, void *readArg, void *readyFunc,
                                   void *resumeFunc, void *cbArg, void *closeFunc, void *closeArg)
{
    int i;

    for (i = 0; i < 7; i++) {
        CdvdBgReq *bg = &bgTbl[i];

        if (bg->name[0] == 0) {
            int no = file_of(name);

            memset(bg, 0, sizeof *bg);
            snprintf(bg->name, sizeof bg->name, "stream%d", no);
            bg->readFunc = (int (*)(CdvdBgReq *, void *))readFunc;
            bg->readArg = readArg;
            bg->readyFunc = (void (*)(CdvdBgReq *, void *, int))readyFunc;
            bg->resumeFunc = (void (*)(CdvdBgReq *, void *))resumeFunc;
            bg->cbArg = cbArg;
            bg->closeFunc = (int (*)(CdvdBgReq *, void *))closeFunc;
            bg->closeArg = closeArg;
            bg->size = adpcmFile[no].sectors << 11;
            bg->lsn = no; /* the file's number: file_byte's key */
            return bg;
        }
    }
    CHECK(0);
    return &bgTbl[0];
}

void iosCdvdBackGroundMgrDelete(CdvdBgReq *self)
{
    self->name[0] = 0;
}

void iosCdvdBackGroundMgrSeek(CdvdBgReq *self, int val)
{
    self->pos = val;
}

/* the file's bytes from pos into IOP RAM at `buf` (an IOP address) */
int iosCdvdBackGroundReadIOPm(CdvdBgReq *self, void *buf, int size)
{
    uint32_t iop = (uint32_t)(uintptr_t)buf;
    uint8_t *dst;
    int k;

    CHECK(ico_iop_range_ok(iop, (uint32_t)size));
    dst = ico_iop_ptr(iop);
    for (k = 0; k < size; k++) {
        dst[k] = file_byte(self->lsn, (uint32_t)(self->pos + k));
    }
    self->pos += size;
    return !(self->pos < self->size);
}

static void bg_run(void)
{
    int i;

    for (i = 6; i >= 0; i--) {
        CdvdBgReq *bg = &bgTbl[i];

        if (bg->name[0] != 0 && bg->readFunc != 0 && bg->readFunc(bg, bg->readArg) > 0) {
            bg->readFunc = 0;
        }
    }
}

/* --- the vsync and the output --------------------------------------------------- */

static unsigned long vsyncs;

/* the mix of the vsync just rendered */
static int16_t mixbuf[960 * 2];

static void vsync(void)
{
    int i;

    spu2_render(mixbuf, 960);
    vsyncs++;
    bg_run();
    for (i = 0; i < 16; i++) {
        if (sqTbl[i].bank == 0x11 && sqTbl[i].mode == 2 && sqTbl[i].stream != NULL) {
            adpcmTickProc2(&sqTbl[i]);
        }
    }
    SgCalledTickProc();
}

/* RMS of the louder channel over frames [a, b) of the vsync's mix */
static double rms(int a, int b)
{
    double s[2] = {0, 0};
    int i;

    for (i = a; i < b; i++) {
        s[0] += (double)mixbuf[2 * i] * mixbuf[2 * i];
        s[1] += (double)mixbuf[2 * i + 1] * mixbuf[2 * i + 1];
    }
    return __builtin_sqrt((s[0] > s[1] ? s[0] : s[1]) / (b - a));
}

/* `n` vsyncs; the RMS over them */
static double run(int n)
{
    double sum = 0;
    int i;

    for (i = 0; i < n; i++) {
        double r;

        vsync();
        r = rms(0, 960);
        sum += r * r;
    }
    return n > 0 ? __builtin_sqrt(sum / n) : 0;
}

/* the request daemon's open: AdpcmOpen, the ring's first fill, then
   AdpcmOpenSync and AdpcmPlay */
static SqEntry *open_stream(int no, int loopNum)
{
    AdpcmOpenReq req;
    SqEntry *e;
    int t;

    memset(&req, 0, sizeof req);
    AdpcmOpen(&req, no, 0, loopNum);
    CHECK(req.bg != 0);
    for (t = 0; t < 10; t++) {
        e = AdpcmOpenSync(&req);
        if (e != (SqEntry *)-1) {
            break;
        }
        vsync();
    }
    CHECK(e != NULL && e != (SqEntry *)-1);
    AdpcmPlay(e->stream);
    return e;
}

static int vol(int v, int right)
{
    return spu2_sd_get_param(
        (uint16_t)((right ? SPU2_SD_VPARAM_VOLR : SPU2_SD_VPARAM_VOLL) | SPU2_SD_VOICE(0, v)));
}

static int envx(int v)
{
    return spu2_sd_get_param((uint16_t)(SPU2_SD_VPARAM_ENVX | SPU2_SD_VOICE(0, v))) & 0x7FFF;
}

/* --- the log watch's lines ------------------------------------------------------- */

static char lines[64][200];
static int nlines;

static void catch_line(const char *line)
{
    if (nlines < 64) {
        snprintf(lines[nlines], sizeof lines[0], "%s", line);
    }
    nlines++;
    printf("  log: %s\n", line);
}

static int has_line(const char *prefix)
{
    int i;

    for (i = 0; i < nlines && i < 64; i++) {
        if (strncmp(lines[i], prefix, strlen(prefix)) == 0) {
            return 1;
        }
    }
    return 0;
}

static void power_on(void)
{
    ico_sif_host_reset();
    ico_sndn2_host_reset();
    ico_audio_stream_log_sink(catch_line);
    nlines = 0;
    memset(sqTbl, 0, sizeof sqTbl);
    memset(bgTbl, 0, sizeof bgTbl);
    chMask = 0;
    vsyncs = 0;
    CHECK(SgSndn2RemoteInit() == 0);
    SgInit();
    /* s_init.c's sound init: the master volumes the game sets once the
       sound is up */
    SgSetMasterVol(0, 0x3FFF, 0x3FFF);
    SgSetMasterVol(1, 0x3FFF, 0x3FFF);
    AdpcmStreamInit();
    CHECK(adpcmSpuSlot[0] == 0 && adpcmSpuSlot[1] == 1 && adpcmSpuSlot[2] == 2 &&
          adpcmSpuSlot[3] == 3);
    SgCalledTickProc();
}

/* --- 1. reopen after a fade --------------------------------------------------- */

static void test_reopen_after_fade(void)
{
    SqEntry *a;
    SqEntry *b;
    double ra;
    double rb;
    int t;
    int rose = 0;

    printf("1. reopen after a fade\n");
    power_on();
    a = open_stream(43, 0);
    ra = run(100);
    CHECK(ra > 300);
    /* the fade the request daemon starts (AdpcmFadeCloseAll), 0x200 a tick:
       32 ticks from 0x3FFF to the close */
    AdpcmFadeCloseAll(0x200);
    for (t = 0; t < 60 && adpcmStream[0].used != 0; t++) {
        vsync();
    }
    CHECK(adpcmStream[0].used == 0);
    CHECK(a->num == 0); /* closed */
    run(2);
    CHECK(vol(0, 0) < 0x400); /* the fade's last step stays in the register */
    b = open_stream(44, 0);
    CHECK(b->stream->ch[0] == 0 && b->stream->ch[1] == 1); /* the faded voices */
    vsync();
    spu2_apply_pending();
    CHECK(vol(0, 0) == 0x3FFF && vol(0, 1) == 0);
    CHECK(vol(1, 0) == 0 && vol(1, 1) == 0x3FFF);
    for (t = 0; t < 10; t++) {
        vsync();
        if (envx(0) > 0x4000 && envx(1) > 0x4000) {
            rose = 1;
            break;
        }
    }
    CHECK(rose);
    rb = run(100);
    /* 44's square is half 43's (shift 4 against 3) */
    printf("   43 rms %.0f, 44 after the fade rms %.0f\n", ra, rb);
    CHECK(rb > 0.35 * ra && rb < 0.65 * ra);
    CHECK(has_line("adpcm: 43 voices 0,1 vol 3fff/3fff env "));
    CHECK(has_line("adpcm: 43 at close: voices 0,1 vol "));
    CHECK(has_line("adpcm: 44 voices 0,1 vol 3fff/3fff env "));
    soundDataClose(b);
    run(3);
}

/* --- 2. the ending's shape ------------------------------------------------------ */

static void test_ending_shape(void)
{
    static const int order[] = {43, 44, 45, 46, 47, 48};
    double ratio[6];
    SqEntry *prev = NULL;
    char want[64];
    int k;

    printf("2. the ending's shape\n");
    power_on();
    for (k = 0; k < 6; k++) {
        int no = order[k];
        SqEntry *e = open_stream(no, 1);
        int amp = (7 << 12) >> stream_shift(no);
        double r;

        CHECK(adpcmNo[k & 1] == no);
        CHECK(e->stream->ch[0] == (k & 1) * 2);
        run(5);
        if (prev != NULL) {
            soundDataClose(prev); /* the scene cut: "now" */
        }
        run(10); /* the closed stream's release */
        r = run(75);
        ratio[k] = r / amp;
        printf("   stream %d alone: rms %.0f (square %d, ratio %.3f)\n", no, r, amp, ratio[k]);
        CHECK(r > 100);
        if (k == 0 || k == 1) {
            char hb[160];

            /* the heartbeat's part: the slot just opened, the other closed */
            ico_audio_stream_status(hb, sizeof hb);
            printf("   heartbeat: %s\n", hb);
            CHECK(strcmp(hb, k == 0 ? "adpcm: 43@0 vol 3fff/3fff env 7fff/7fff, -"
                                    : "adpcm: -, 44@2 vol 3fff/3fff env 7fff/7fff") == 0);
        }
        prev = e;
    }
    for (k = 1; k < 6; k++) {
        CHECK(ratio[k] > 0.8 * ratio[0] && ratio[k] < 1.2 * ratio[0]);
    }
    for (k = 0; k < 6; k++) {
        int v = (k & 1) * 2;

        snprintf(want, sizeof want, "adpcm: %d voices %d,%d vol 3fff/3fff env ", order[k], v,
                 v + 1);
        CHECK(has_line(want));
        if (k < 5) {
            snprintf(want, sizeof want, "adpcm: %d at close: voices %d,%d vol 3fff/3fff env ",
                     order[k], v, v + 1);
            CHECK(has_line(want));
        }
    }
    soundDataClose(prev);
    run(3);
}

/* --- 3. a stream of minutes ------------------------------------------------------ */

static void test_long_stream(void)
{
    const int no = 43;
    const double bytes = (double)(adpcmFile[no].sectors << 11);
    /* 28 samples in 16 bytes a channel, two channels, at 44.1 kHz */
    const double secs = bytes / (44100.0 / 28 * 16 * 2);
    SqEntry *e;
    uint32_t last = 0;
    int wraps = 0;
    long keyon = -1;
    long closed = -1;
    int quiet = 0;
    long t;
    clock_t c0 = clock();

    printf("3. a stream of %.1f s\n", secs);
    power_on();
    e = open_stream(no, 1);
    for (t = 0; t < (long)(secs * 50) + 200; t++) {
        uint32_t at;
        int w;

        vsync();
        if (adpcmStream[0].used == 0) {
            closed = (long)vsyncs;
            break;
        }
        at = (uint32_t)SgStAdpcmIopReadAddr(e->stream->ch[0]);
        if (at < last) {
            wraps++;
        }
        last = at;
        if (keyon < 0 && envx(0) > 0) {
            keyon = (long)vsyncs;
        }
        /* every 10 ms after the first vsync of sound */
        for (w = 0; keyon >= 0 && (long)vsyncs > keyon && w < 960; w += 480) {
            if (rms(w, w + 480) < 100) {
                quiet++;
            }
        }
    }
    printf("   key-on at vsync %ld, closed at vsync %ld (%.2f s; the file %.2f s), %d ring wraps, "
           "%d quiet 10 ms windows, %.2f s wall\n",
           keyon, closed, closed / 50.0, secs, wraps, quiet,
           (double)(clock() - c0) / CLOCKS_PER_SEC);
    CHECK(keyon >= 0 && keyon < 10);
    CHECK(closed > 0);
    /* the close lands at the file's length: the read address the loop count
       follows leads the voice by at most the ring's prefill */
    CHECK(closed / 50.0 > secs - 2.0 && closed / 50.0 < secs + 1.0);
    CHECK(wraps >= (int)(bytes / 0x5C000) - 1);
    CHECK(quiet == 0);
    CHECK(has_line("adpcm: 43 voices 0,1 vol 3fff/3fff env "));
    CHECK(has_line("adpcm: 43 at half: voices 0,1 vol 3fff/3fff env "));
    CHECK(has_line("adpcm: 43 at close: voices 0,1 vol 3fff/3fff env "));
}

int main(void)
{
    clock_t c0 = clock();
    char hb[160];

    test_reopen_after_fade();
    test_ending_shape();
    test_long_stream();
    ico_audio_stream_status(hb, sizeof hb);
    printf("heartbeat part after the close: %s\n", hb);
    printf("adpcm_stream_test: %d checks, %s, %.2f s\n", checks, failures ? "FAILED" : "ok",
           (double)(clock() - c0) / CLOCKS_PER_SEC);
    return failures != 0;
}
