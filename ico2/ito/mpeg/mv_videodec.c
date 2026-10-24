#include "mv_defs.h"
#include "mv_videodec.h"
#include "debug.h"
#include "memory.h"
#include "mv_disp.h"
#include "mv_sub.h"
#include "mv_vibuf.h"
#include "mv_vobuf.h"
#include "typedef.h"
#include "ios.h"
#include "mv_main.h"
#include <libmpeg.h>
#include <string.h>

static void Free(int addr);

/* the MPEG library's callbacks, which videoDecCreate registers */
static inline int mpegError(sceMpeg *mp, void *cbdata, void *anyData)
{
    MvCbErr *cb = cbdata;

    debug_StdPrintfDummy("%s\n", cb->message);
    return 1;
}

static inline int mpegNodata(sceMpeg *mp, void *cbdata, void *anyData)
{
    VideoDec *dec = anyData;

    switchThread();
    viBufAddDMA(&dec->vibuf);
    return 1;
}

static inline int mpegStopDMA(sceMpeg *mp, void *cbdata, void *anyData)
{
    VideoDec *dec = anyData;

    viBufStopDMA(&dec->vibuf);
    return 1;
}

static inline int mpegRestartDMA(sceMpeg *mp, void *cbdata, void *anyData)
{
    VideoDec *dec = anyData;

    viBufRestartDMA(&dec->vibuf);
    return 1;
}

static inline int mpegTS(sceMpeg *mp, void *cbdata, void *anyData)
{
    MvCbTs *cb = cbdata;
    VideoDec *dec = anyData;
    ViTs ts;

    viBufGetTs(&dec->vibuf, &ts);
    cb->pts = ts.pts;
    cb->dts = ts.dts;
    return 1;
}

static void free_buf(VideoDec *self)
{
    Free(self->buf);
}

int videoDecCreate(VideoDec *self)
{
    int p;

    p = alloc_zeroed(PAL_JPNKORCN(1868288, 1557248), 64);
    self->buf = p;
    if (p == 0) {
        return -1;
    }
    sceMpegCreate(&self->mpeg, (void *)p, PAL_JPNKORCN(1868288, 1557248));
    sceMpegAddCallback(&self->mpeg, 0, mpegError, 0);
    sceMpegAddCallback(&self->mpeg, 1, mpegNodata, self);
    sceMpegAddCallback(&self->mpeg, 2, mpegStopDMA, self);
    sceMpegAddCallback(&self->mpeg, 3, mpegRestartDMA, self);
    sceMpegAddCallback(&self->mpeg, 5, mpegTS, self);
    self->state = 0;
    return viBufCreate(&self->vibuf) == 0 ? 0 : -1;
}

static void videoDecBeginPut(VideoDec *self, void **addr1, int *size1, void **addr2, int *size2)
{
    viBufBeginPut(&self->vibuf, addr1, size1, addr2, size2);
}

static void videoDecEndPut(VideoDec *self, int n)
{
    viBufEndPut(&self->vibuf, n);
}

typedef struct Code4 { /* field names derived */
    char b[4];
} Code4; /* derived name */

int videoDecFlush(VideoDec *self)
{
    Code4 code = {{0x00, 0x00, 0x01, 0xB7}}; /* the MPEG sequence end code */
    void *p0;
    int n0;
    void *p1;
    int n1;

    videoDecBeginPut(self, &p0, &n0, &p1, &n1);
    if (n0 + n1 < 4) {
        return 0;
    }
    videoDecEndPut(self, copy2area((char *)uncached_accel_addr((int)p0), n0,
                                   (char *)uncached_accel_addr((int)p1), n1, code.b, 4, 0, 0));
    viBufFlush(&self->vibuf);
    if (self->state == 0) {
        self->state = 2;
    }
    return 1;
}

int videoCallback(sceMpeg *mp, void *cbdata, void *anyData)
{
    MvCbStr *pkt = cbdata;
    MvCbArg *arg = anyData;
    ViTs ts;
    void *p0;
    int n0;
    void *p1;
    int n1;
    ReadBuf *b = arg->rb;
    VideoDec *dec = arg->dec;
    int rd = (int)pkt->data;
    int base = (int)b->data;
    unsigned int len = pkt->len;
    unsigned int first = base + b->size - rd;
    unsigned int rest;
    int n;

    if (len < first) {
        first = len;
    }
    rest = len - first;
    videoDecBeginPut(dec, &p0, &n0, &p1, &n1);
    n = copy2area((char *)uncached_accel_addr((int)p0), n0, (char *)uncached_accel_addr((int)p1),
                  n1, (char *)rd, first, (char *)base, rest);
    if (n > 0) {
        ts.pts = pkt->pts;
        ts.dts = pkt->dts;
        ts.pos = (int)p0 - (int)dec->vibuf.data;
        ts.len = n;
        if (viBufPutTs(&dec->vibuf, &ts) == 0) {
            ErrMessage("pts buffer overflow\n");
        }
    }
    videoDecEndPut(dec, n);
    return 0 < n;
}

static int decBitStrm0(VideoDec *dec, MvDispEnv *disp, VoBuf *vo)
{
    int ret = 1;
    VoData *p;
    int w;
    int h;
    int ox;
    int oy;
    int hh;
    int i;
    int j;

    while (!sceMpegIsEnd(&dec->mpeg)) {
        /* the decoder's state, as videoDecGetState reads it */
        if (dec->state == 1) {
            ret = -1;
            debug_StdPrintfDummy("decode thread: aborted\n");
            break;
        }
        while ((p = voBufGetData(vo)) == 0) {
            switchThread();
        }
        if (sceMpegGetPicture(&dec->mpeg, (unsigned int)p, PAL_JPNKORCN(1620, 1350)) < 0) {
            ErrMessage("sceMpegGetPicture() decode error");
            ret = -1;
            break;
        }
        if (dec->mpeg.frameCount == 0) {
            debug_StdPrintfDummy("movie %d x %d\n", dec->mpeg.width, dec->mpeg.height);
            w = dec->mpeg.width;
            ox = (disp->imageWidth - w) >> 1;
            h = dec->mpeg.height;
            oy = (disp->imageHeight - h) >> 2;
            hh = h >> 1;
            for (i = 0; i < vo->max; i++) {
                for (j = 0; j < 2; j++) {
                    dispSetTags(disp, (int)vo->tag[i].packet[j], (int)&vo->data[i], j, ox, oy, w,
                                hh, w, h);
                }
            }
        }
        voBufIncCount(vo);
        switchThread();
    }
    sceMpegReset(&dec->mpeg);
    return ret;
}

static void Free(int addr)
{
    /* the buffers are kept as addresses (alloc_zeroed), some at their
       uncached-accelerated alias; the heap takes the block back with the
       segment bits off */
    iosFree((void *)phys_addr(addr));
}

int videoDecDelete(VideoDec *self)
{
    viBufDelete(&self->vibuf);
    sceMpegDelete(&self->mpeg);
    free_buf(self);
    return 1;
}

int videoDecSetStream(VideoDec *self, int type, int ch, sceMpegCallback fn, void *data)
{
    sceMpegAddStrCallback(&self->mpeg, type, ch, fn, data);
    return 1;
}

void videoDecAbort(VideoDec *self)
{
    self->state = 1;
}

int videoDecGetState(VideoDec *self)
{
    return self->state;
}

int videoDecIsFlushed(VideoDec *self)
{
    int ret = 0;
    if (viBufCount(&self->vibuf) == 0) {
        ret = sceMpegIsRefBuffEmpty(&self->mpeg) != 0;
    }
    return ret;
}

void videoDecMain(void *thArg)
{
    MvThreadArg *arg = thArg;

    viBufReset(&arg->dec->vibuf);
    voBufReset(arg->vo);
    decBitStrm0(arg->dec, arg->disp, arg->vo);
    arg->dec->state = 3;
}
