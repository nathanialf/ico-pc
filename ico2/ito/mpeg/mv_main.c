#include "mv_defs.h"
#include "mv_main.h"
#include "debug.h"
#include "GsBase.h"
#include <eekernel.h>
#include <libgraph.h>
#include <libmpeg.h>
#include "mv_audiodec.h"
#include "mv_strfile.h"
#include "mv_readbuf.h"
#include "mv_videodec.h"
#include <eeregs.h>
#include <libcdvd.h>

/* the DMA and interrupt channels the player takes over for the length of the
   stream and hands back one by one in movie_end. */
static int movieDmacChannel[] = {0, 1, 2, 3, 4, 8, 9}; /* derived name */

static int movieIntcChannel[] = {0, 1, 2, 4, 5, 6, 7}; /* derived name */

/* the video-out ring the decoder fills and mv_disp drains (mv_vobuf.h) */
VoBuf voBuf = {0};

/* the movie's display environment (mv_disp.h); debug_exception's `display`
   routine is that file's own static */
MvDispEnv display = {0};

/* The two callback argument pairs videoDecSetStream is handed by address,
   the priority the decode thread is created at and its id, the started flag,
   the interrupt and DMA enables and the D_CTRL word saved across playback,
   the caller's own priority, and the GS interrupt mask saved across
   playback (a 64-bit register). */
static MvCbArg videoCbArg; /* derived name */

static MvCbArg pcmCbArg; /* derived name */

static int decThreadPri; /* derived name */

static int decThreadId; /* derived name */

static int decThreadStarted; /* derived name */

static int savedIntc; /* derived name */

static int savedDmac; /* derived name */

static int savedDmaCtrl; /* derived name */

static int callerPri; /* derived name */

static long long savedIMR; /* derived name */

/* set once the movie has an audio stream */
static int movieHasAudio = 1; /* derived name */

/* frames left of a read-starved pause */
static int moviePauseCount = 0; /* derived name */

/* the debug frame counter */
static int movieFrameNo = 0; /* derived name */

/* The stream file the movie is read through, the read buffer, the video and
   audio decoder objects, the argument block the decode thread is started
   with, its 32 KB stack, and the DMA and interrupt enables saved per
   channel. */
/* the stream file's 32 KB sector buffer is a DMA target, so the record is
   64-byte aligned, as cdvd.c's iosCdvd */
static char mpegStrFile[33216] __attribute__((aligned(64))); /* derived name */

/* The stream file the movie is read through, the read buffer, the video and
   audio decoder objects, the argument block the decode thread is started
   with, its 32 KB stack, and the DMA and interrupt enables saved per
   channel. */
static char mpegStrFile[33216]; /* derived name */

static ReadBuf mpegReadBuf; /* derived name */

static VideoDec videoDec; /* derived name */

static AudioDec audioDec; /* derived name */

static MvThreadArg decThreadArg; /* derived name */

static int decThreadStack[8192]; /* derived name */

static int savedDmacs[8]; /* derived name */

static int savedIntcs[8]; /* derived name */

extern int _gp; /* linker-defined global pointer */

void switchThread(void)
{
    RotateThreadReadyQueue(decThreadPri);
}

static void proceedAudio(void)
{
    audioDecSendToIOP(&audioDec);
}

/* whether the audio side is ready: its first block is on the IOP, or there
   is no audio stream */
static inline int audioIsPreset(void) /* derived name */
{
    return movieHasAudio ? audioDecIsPreset(&audioDec) : 1;
}

static int readMpeg_aug6jun26(VideoDec *dec, ReadBuf *rb, char *strf, int (*poll)(void))
{
    void *p;
    int eof;
    void *q;
    int abort = 0;
    /* the read-starved pause is not finished in this build: nothing sets the
       counter, and the code that would act on it is not compiled in */
    int pauseCount = 0;
    int started = 0;
    int left;
    int n;
    int rd;
    int size;
    int len;

    /* the stream's byte count, a word of cdvd.c's stream record, which
       cdvd.h does not declare */
    left = *(int *)(strf + 0x8180);
    n = left;

    while (pauseCount != 0 || (left >= 5 && videoDecGetState(dec) != 3)) {
        if (dec->mpeg.frameCount >= 11) {
            if (poll() != 0) {
                abort = 1;
                videoDecAbort(&videoDec);
            }
        }
        size = readBufBeginPut(rb, &p);
        if (n > 0 && size > 65535) {
            rd = strFileRead(strf, p, 65536, &eof);
            if (eof != 0) {
                goto term;
            }
            readBufEndPut(rb, rd);
            n -= rd;
        }
        switchThread();
        len = readBufBeginGet(rb, &q);
        if (len > 0) {
            len = sceMpegDemuxPssRing(&dec->mpeg, q, len, (int)rb->data, rb->size);
            left -= len;
            readBufEndGet(rb, len);
        }
        proceedAudio();
        if (started == 0 && voBufIsFull(&voBuf) && audioIsPreset()) {
            startDisplay(1);
            if (movieHasAudio != 0) {
                audioDecStart(&audioDec);
            }
            started = 1;
        }
    }

    while (videoDecFlush(dec) == 0) {
        switchThread();
    }
    while (videoDecIsFlushed(dec) == 0 && videoDecGetState(dec) != 3) {
        switchThread();
    }

term:
    gsb_ClearFrameBuffer();
    endDisplay();
    if (movieHasAudio != 0) {
        audioDecReset(&audioDec);
    }
    return abort;
}

static int readMpeg(VideoDec *dec, ReadBuf *rb, char *strf, int (*poll)(void))
{
    if (ICO_AUG6JUN26) {
        return readMpeg_aug6jun26(dec, rb, strf, poll);
    }
    void *p;
    int eof;
    void *q;
    int abort = 0;
    int started = 0;
    int left;
    int n;
    int rd;
    int size;
    int len;

    /* the stream's byte count, a word of cdvd.c's stream record, which
       cdvd.h does not declare */
    left = *(int *)(strf + 0x8180);
    n = left;

    while (moviePauseCount != 0 || (left >= 5 && videoDecGetState(dec) != 3)) {
        if (sceCdStStat() < 32 && moviePauseCount == 0) {
            debug_StdPrintfDummy("movie pause\n");
            moviePauseCount = 30;
        }
        if (dec->mpeg.frameCount >= 11) {
            if (moviePauseCount == 1) {
                startDisplay(1);
                if (movieHasAudio != 0) {
                    audioDecResume(&audioDec);
                }
            } else if (moviePauseCount == 30) {
                endDisplay();
                if (movieHasAudio != 0) {
                    audioDecPause(&audioDec);
                }
            }
            if (moviePauseCount > 0) {
                moviePauseCount--;
            }
            if (poll() != 0) {
                abort = 1;
                videoDecAbort(&videoDec);
            }
        }
        size = readBufBeginPut(rb, &p);
        if (n > 0 && size > 65535) {
            rd = strFileRead(strf, p, 65536, &eof);
            if (eof != 0) {
                goto term;
            }
            readBufEndPut(rb, rd);
            n -= rd;
        }
        switchThread();
        len = readBufBeginGet(rb, &q);
        if (len > 0) {
            len = sceMpegDemuxPssRing(&dec->mpeg, q, len, (int)rb->data, rb->size);
            left -= len;
            readBufEndGet(rb, len);
        }
        proceedAudio();
        if (started == 0 && voBufIsFull(&voBuf) && audioIsPreset()) {
            startDisplay(1);
            if (movieHasAudio != 0) {
                audioDecStart(&audioDec);
            }
            started = 1;
        }
    }

    while (videoDecFlush(dec) == 0) {
        switchThread();
    }
    while (videoDecIsFlushed(dec) == 0 && videoDecGetState(dec) != 3) {
        switchThread();
    }

term:
    gsb_ClearFrameBuffer();
    endDisplay();
    if (movieHasAudio != 0) {
        audioDecReset(&audioDec);
    }
    return abort;
}

static int initAll(char *name, int imageW, int imageH, int dbx, int dby, int mono, int volume,
                   int clearCol)
{
    struct ThreadParam th;
    int ret = 0;

    if (!ICO_AUG6JUN26) {
        moviePauseCount = 0;
    }
    videoDec.intcHandler = -1;
    videoDec.dmacHandler = -1;
    decThreadStarted = 0;

    dispCreate(&display, imageW, imageH, dbx, dby);
    dispClear(&display, clearCol);

    savedDmaCtrl = *D_CTRL;
    debug_StdPrintfDummy("D_CTRL %x\n", *D_CTRL);
    *D_CTRL |= 3;
    *D_STAT = 4;

    debug_StdPrintfDummy("open movie file %s\n", name);
    if (strFileOpen(mpegStrFile, name) == 0) {
        return -1;
    }
    if (readBufCreate(&mpegReadBuf) != 0) {
        return -1;
    }
    sceMpegInit();
    if (videoDecCreate(&videoDec) != 0) {
        return -1;
    }
    if (movieHasAudio != 0) {
        if (audioDecCreate(&audioDec, mono, volume) != 0) {
            return -1;
        }
    }

    videoCbArg.rb = &mpegReadBuf;
    videoCbArg.dec = &videoDec;
    videoDecSetStream(&videoDec, 0, 0, videoCallback, &videoCbArg);
    if (movieHasAudio != 0) {
        pcmCbArg.rb = &mpegReadBuf;
        pcmCbArg.dec = &audioDec;
        videoDecSetStream(&videoDec, 2, 0, pcmCallback, &pcmCbArg);
    }

    if (voBufCreate(&voBuf) != 0) {
        return -1;
    }
    debug_StdPrintfDummy("create video decode thread\n");

    th.entry = videoDecMain;
    th.stack = decThreadStack;
    th.stackSize = 32768;
    th.initPriority = decThreadPri;
    th.gpReg = &_gp;
    th.option = 0;
    decThreadId = CreateThread(&th);
    debug_StdPrintfDummy("start thread\n");

    decThreadArg.dec = &videoDec;
    decThreadArg.disp = &display;
    decThreadArg.vo = &voBuf;
    StartThread(decThreadId, &decThreadArg);
    decThreadStarted = 1;

    DIntr();
    debug_StdPrintfDummy("add intc\n");
    videoDec.intcHandler = AddIntcHandler(2, vblankHandler, 0);
    if (videoDec.intcHandler < 0) {
        debug_StdPrintfDummy("add intc failed\n");
        ret = -1;
    } else {
        savedIntc = EnableIntc(2);
        debug_StdPrintfDummy("add dmac\n");
        videoDec.dmacHandler = AddDmacHandler(2, handler_endimage, 0);
        if (videoDec.dmacHandler < 0) {
            debug_StdPrintfDummy("add dmac failed\n");
            ret = -1;
        } else {
            savedDmac = EnableDmac(2);
        }
    }
    EIntr();
    return ret;
}

static void termAll(void)
{
    strFileClose(mpegStrFile);
    DIntr();
    if (savedDmac != 0) {
        DisableDmac(2);
    }
    savedDmac = 0;
    if (videoDec.dmacHandler >= 0) {
        RemoveDmacHandler(2, videoDec.dmacHandler);
    }
    if (savedIntc != 0) {
        DisableIntc(2);
    }
    savedIntc = 0;
    if (videoDec.intcHandler >= 0) {
        RemoveIntcHandler(2, videoDec.intcHandler);
    }
    EIntr();
    if (decThreadStarted != 0) {
        TerminateThread(decThreadId);
        DeleteThread(decThreadId);
    }
    readBufDelete(&mpegReadBuf);
    voBufDelete(&voBuf);
    videoDecDelete(&videoDec);
    audioDecDelete(&audioDec);
    dispDelete(&display);
    *D_CTRL = savedDmaCtrl;
}

/* the VU0 status register */
static inline int vu0Stat(void) /* derived name */
{
    int r;
    __asm__ __volatile__("cfc2.ni %0, $vi29" : "=r"(r));
    return r;
}

int movie_init(char *name, int imageW, int imageH, int dbx, int dby, int mono, int clearCol)
{
    struct ThreadParam st;
    unsigned int i;
    unsigned int j;

    while (vu0Stat() & 0x100) {}
    sceGsSyncPath(0, 0);

    ReferThreadStatus(GetThreadId(), &st);
    callerPri = st.currentPriority;
    decThreadPri = st.currentPriority;
    savedIMR = sceGsGetIMR();
    debug_StdPrintfDummy("sceGsGetIMR() %lx\n", sceGsGetIMR());

    DIntr();
    for (i = 0; i < 7; i++) {
        savedDmacs[i] = DisableDmac(movieDmacChannel[i]);
        debug_StdPrintfDummy("dmac %d %d\n", movieDmacChannel[i], savedDmacs[i]);
    }
    for (j = 0; j < 7; j++) {
        savedIntcs[j] = DisableIntc(movieIntcChannel[j]);
        debug_StdPrintfDummy("intc %d %d\n", movieIntcChannel[j], savedIntcs[j]);
    }
    EIntr();

    movieHasAudio = 1;
    if (initAll(name, imageW, imageH, dbx, dby, mono, 0x3FFF, clearCol) != 0) {
        debug_StdPrintfDummy("movie init failed\n");
        movie_end();
        return -1;
    }
    return 0;
}

void movie_end(void)
{
    unsigned int i;
    unsigned int j;

    dispClear(&display, 0x80000000);
    termAll();
    DIntr();
    debug_StdPrintfDummy("sceGsGetIMR() %lx\n", sceGsGetIMR());
    sceGsPutIMR(savedIMR);
    for (i = 0; i < 7; i++) {
        if (savedDmacs[i] != 0) {
            EnableDmac(movieDmacChannel[i]);
        }
    }
    for (j = 0; j < 7; j++) {
        if (savedIntcs[j] != 0) {
            EnableIntc(movieIntcChannel[j]);
        }
    }
    ChangeThreadPriority(GetThreadId(), callerPri);
    EIntr();
    debug_StdPrintfDummy("movie end\n");
}

int movie_proc(int (*poll)(void))
{
    int r;
    debug_StdPrintfDummy("= %d =\n", movieFrameNo++);
    r = readMpeg(&videoDec, &mpegReadBuf, mpegStrFile, poll);
    movie_end();
    return r;
}
