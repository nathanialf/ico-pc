#include "debug.h"
#include "ios.h"
#include "adpcm_init.h"
#include <sifrpc.h>
#include "s_init.h"
#include "debug_exception.h"
#include "cdvd.h"
#include <assert.h>
#include <sound.h>

/* port/audio/mix_gain.h: the voices of stream `no` are music, or effects for
   Yorda's hint voice (101 to 104), for the port's gains */
extern void ico_audio_tag_stream(int slot, int no);
static inline void adpcmDiskNotReady(void);
static inline void adpcmDiskReturnReady(void);
static inline int adpcmOpenProc(CdvdBgReq *bg, AdpcmOpenReq *open);
static inline void adpcmOpenDiskNotReady(void);

/* the 2 KB-aligned base of the IOP stream buffers, the two buffers' in-use
   flags, the pause request and the IOP heap block the base was cut from;
   then the two stream records and the four SPU slots the streams play on */
static int adpcmIopBase; /* derived name */

static int adpcmIopBuffUsed[2]; /* derived name */

static int adpcmPause; /* derived name */

static int adpcmIopHeap; /* derived name */

static AdpcmStream adpcmStream[2]; /* derived name */

static int adpcmSpuSlot[4]; /* derived name */

/* PC port (package CRED): one stream opened
   part way in.  After ico_adpcm_set_start(no, bytes), the next open of
   stream `no` reads from `bytes` (rounded down to a 2 KB sector, which keeps
   the 0x400-byte channel interleave) instead of from its start: AdpcmOpen
   seeks the first fill of the ring there, AdpcmOpenSync seeks the next
   fill past it and counts the skipped bytes as played.  Nothing differs
   while it is unset (-1); only the Extras credits set it. */
static int adpcmStartNo = -1;

static int adpcmStartBytes;

void ico_adpcm_set_start(int no, int bytes)
{
    adpcmStartNo = no;
    adpcmStartBytes = bytes > 0 ? bytes & ~0x7FF : 0;
}

void AdpcmStreamFree(void)
{
    sceSifFreeIopHeap(adpcmIopHeap);
}

void adpcmTickProc2(SqEntry *obj)
{
    AdpcmStream *self = obj->stream;
    int i;

    if (iosCdvdDiskStatusGet() == 0 && adpcmPause == 0) {
        for (i = 0; i < self->n; i++) {
            char *ch = (char *)self->ch;
            int ofs = i * 4;
            int no = obj->num;
            SgStAdpcmChannelPitch(1LL << *(int *)(ch + ofs), adpcmFile[no].pitch);
        }
    } else {
        for (i = 0; i < self->n; i++) {
            char *ch = (char *)self->ch;
            int ofs = i * 4;
            SgStAdpcmChannelPitch(1LL << *(int *)(ch + ofs), 0);
        }
        return;
    }
    if (self->loopNum != 0) {
        int addr = SgStAdpcmIopReadAddr(self->ch[0]);
        int delta;

        if (addr >= self->lastAddr) {
            delta = addr - self->lastAddr;
        } else {
            delta = self->ringSize - self->lastAddr + addr;
        }
        self->lastAddr = addr;
        if (delta != 0) {
            self->remain -= delta;
            if (self->remain <= 0) {
                self->loopCount += 1;
                self->remain += self->dataSize - self->loopStart;
            }
            if (self->loopNum != 0 && self->loopCount >= self->loopNum) {
                soundDataClose(obj);
                return;
            }
        }
    }
    if (self->fadeStep != 0) {
        int d = AdpcmVolumeGet(obj) - self->fadeStep;

        if (d < 0) {
            d = 0;
        }
        if (d == 0) {
            soundDataClose(obj);
            return;
        }
        AdpcmVolumeSet(obj, d);
    }
}

/* adpcm_init.o's .rodata opens with these three named objects: the two
   messages are printed further down the file than the strings that follow
   them. */
static const char adpcmSrcFile[] = __FILE__; /* derived name */

static const char adpcmNoAllocMsg[] = "AdpcmIopBuffAlloc not alloc\n"; /* derived name */

/* the IOP area is reserved but unused, so it is being freed */
static const char adpcmFreeIopMsg[] =
    "IOP領域が確保されているのにもかかわらず,使われていなので解放します\n"; /* derived name */

int debugAdpcmOn = 1;

SqEntry *adpcmDataSet(ICO_WORD_PTR(void *) src, int no, int bank, int ch, int size, int iopBuf,
                      int loopNum)
{
    AdpcmChReq req;
    SqEntry *obj;
    AdpcmStream *p;
    int i;
    int j;

    if (size > 0x5C000) {
        size = 0x5C000;
    }
    obj = soundDataAreaGet(no, bank, 2, ch);
    for (i = 0; i < 2; i++) {
        int *q = (int *)((char *)adpcmStream + i * 0x58);
        if (q[0] == 0) {
            goto found;
        }
    }
    debug_assert(adpcmSrcFile, 363);
    __assert(adpcmSrcFile, 363, "0");
found:
    p = (AdpcmStream *)((char *)adpcmStream + i * 0x58);
    p->used = 1;
    obj->stream = p;
    p->n = adpcmFile[no].channels;
    switch (p->n) {
    case 1:
        p->chAttr = 0x10000;
        break;
    case 2:
        p->chAttr = 0x20000;
        break;
    case 4:
        p->chAttr = 0x40000;
        break;
    default:
        debug_assert(adpcmSrcFile, 381);
        __assert(adpcmSrcFile, 381, "0");
    }
    p->mask = 0;
    for (j = 0; j < p->n; j++) {
        req.spuAddr = soundBufAdpcmChAlloc(obj, &req.ch);
        p->ch[j] = req.ch = adpcmSpuSlot[req.ch];
        ico_audio_tag_stream(p->ch[j], no);
        req.attr = p->chAttr | 2;
        req.iopAddr = iopBuf + (0x800 / p->n) * j;
        req.iopSize = 0x5C000;
        req.vol = 0x4000;
        SgStAdpcmOpen(&req);
        if (p->chAttr == 0x10000) {
            p->volR[j] = 0x3FFF;
            p->volL[j] = 0x3FFF;
        } else if ((j & 1) == 0) {
            p->volL[j] = 0x3FFF;
            p->volR[j] = 0;
        } else {
            p->volR[j] = 0x3FFF;
            p->volL[j] = 0;
        }
        p->fadeStep = 0;
        SgStAdpcmChannelVolume(1LL << p->ch[j], p->volL[j], p->volR[j]);
        SgStAdpcmChannelPitch(1LL << p->ch[j], adpcmFile[no].pitch);
        p->mask |= 1LL << p->ch[j];
    }
    if (size < 0x5C000) {
        p->seekSize = size;
    } else {
        p->seekSize = 0;
    }
    p->pitch = adpcmFile[no].pitch;
    p->iopBuf = iopBuf;
    p->ringSize = 0x5C000;
    p->dataSize = adpcmFile[no].sectors << 11;
    p->loopStart = adpcmFile[no].loopStart << 11;
    p->lastAddr = 0;
    p->remain = adpcmFile[no].sectors << 11;
    p->loopNum = loopNum;
    p->loopCount = 0;
    if (size != 0) {
        Ee2Iop((ICO_WORD)src, iopBuf, size);
    }
    p->bg = iosCdvdBackGroundMgrAdd((char *)&adpcmFile[no], adpcmTickProc, obj, adpcmDiskNotReady,
                                    adpcmDiskReturnReady, obj, 0, 0);
    iosCdvdBackGroundMgrSeek(p->bg, size);
    return obj;
}

void AdpcmPlay(AdpcmStream *self)
{
    debug_StdPrintfDummy("AdpcmPlay\n");
    SgStAdpcmPlay(self->mask);
}

void AdpcmStop(AdpcmStream *self)
{
    SgStAdpcmStop(self->mask);
}

inline int AdpcmIopBuffAlloc(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (adpcmIopBuffUsed[i] == 0) {
            goto found;
        }
    }
    debug_StdPrintfDummy(adpcmNoAllocMsg);
    return 0;
found:
    adpcmIopBuffUsed[i] = 1;
    return adpcmIopBase + i * 0x5C000;
}

void AdpcmOpen(AdpcmOpenReq *self, int no, int ch, int loopNum)
{
    int req;

    debug_StdPrintfDummy("AdpcmOpen id%d \n", no);
    req = (no & 0xFFFF) | 0x110000;
    if (soundDataAreaSearch(&req) != 0) {
        self->bg = 0;
        if (no == adpcmStartNo) {
            adpcmStartNo = -1; /* PC port (CRED): already open, not kept */
        }
        return;
    }
    self->ch = ch;
    self->id = no;
    self->iopBuf = AdpcmIopBuffAlloc();
    if (self->iopBuf != 0) {
        self->bg = iosCdvdBackGroundMgrAdd((char *)&adpcmFile[no], adpcmOpenProc, self,
                                           adpcmOpenDiskNotReady, 0, self, 0, 0);
        if (no == adpcmStartNo) {
            iosCdvdBackGroundMgrSeek(self->bg, adpcmStartBytes); /* PC port (CRED) */
        }
    } else {
        self->bg = 0;
        if (no == adpcmStartNo) {
            adpcmStartNo = -1; /* PC port (CRED): not opened, not kept */
        }
        debug_StdPrintfDummy("%s\n", (char *)&adpcmFile[no]);
    }
    self->loopNum = loopNum;
}

static inline void AdpcmIopBuffFree(AdpcmStream *self) /* derived name */
{
    int adr = self->iopBuf;
    int no = (adr - adpcmIopBase) / 0x5C000;

    if (no >= 3) {
        debug_assert(adpcmSrcFile, 143);
        __assert(adpcmSrcFile, 143, "0");
    }
    adpcmIopBuffUsed[no] = 0;
}

void AdpcmClose(SqEntry *obj)
{
    AdpcmStream *self = obj->stream;
    int i;
    int j;

    if (self != 0 && self->bg != 0) {
        iosCdvdBackGroundMgrDelete(self->bg);
        self->bg = 0;
        AdpcmStop(self);
        for (i = 0; i < self->n; i++) {
            char *ch = (char *)self->ch;
            int ofs = i * 4;
            SgStAdpcmClose(*(int *)(ch + ofs));
        }
        AdpcmIopBuffFree(self);
        soundBufAdpcmFree(obj);
        for (j = 0; j < 2; j++) {
            AdpcmStream *p = &adpcmStream[j];
            if (p->used != 0 && p == self) {
                goto found;
            }
        }
        debug_assert(adpcmSrcFile, 605);
        __assert(adpcmSrcFile, 605, "0");
    found:
        adpcmStream[j].used = 0;
        self->mask = 0;
    }
}

/* the levels come back out of the record the caller just wrote */
void AdpcmInterStereoVolumeSet(void *stream, int ch)
{
    char *st = (char *)stream;
    int j = ch + 1;
    short *r = (short *)(st + ch * 2);
    short *q = (short *)(st + j * 2);
    short lv = r[0x1E];
    short rv = q[0x20];

    if (debugAdpcmOn == 0) {
        rv = 0;
        lv = 0;
    }
    if (*(int *)(st + 0x38) == 0x10000) {
        return;
    }
    if (soundOutputModeGet() == 0) {
        int *p = (int *)(st + 8);
        int *c = p + ch;
        SgStAdpcmChannelVolume(1LL << *c, lv, 0);
        p += j;
        SgStAdpcmChannelVolume(1LL << *p, 0, rv);
    } else {
        int *p = (int *)(st + 8);
        int *c = p + ch;
        SgStAdpcmChannelVolume(1LL << *c, lv, rv);
        p += j;
        SgStAdpcmChannelVolume(1LL << *p, lv, rv);
    }
}

void AdpcmInterLeaveVolumeSet(SqEntry *self, int idx, int vol)
{
    char *b = (char *)self->stream;
    short *q = (short *)(b + (idx * 2 + 1) * 2);
    short *r = (short *)(b + idx * 4);
    q[0x20] = vol;
    r[0x1E] = vol;
    AdpcmInterStereoVolumeSet(b, idx * 2);
}

void AdpcmVolumeSet(SqEntry *self, int vol)
{
    AdpcmInterLeaveVolumeSet(self, 0, vol);
}

inline void adpcmPauseRequest(int val)
{
    adpcmPause = val;
}

inline void AdpcmStreamHeap(void)
{
    int r = iosSifAllocIopHeapDebug(0xB8800, adpcmSrcFile, 68);
    adpcmIopHeap = r;
    if (r & 0x7FF) {
        adpcmIopBase = (r / 0x800 + 1) * 0x800;
    } else {
        adpcmIopBase = r;
    }
}

inline void AdpcmStreamInit(void)
{
    int i;
    int *p;

    for (i = 0, p = adpcmSpuSlot; i < 4; i++, p++) {
        *p = SgGetSpuSlotMalloc(1);
    }
    AdpcmStreamHeap();
    SgStAdpcmInit();
    for (i = 0; i < 2; i++) {
        *(int *)((char *)adpcmStream + i * 0x58) = 0;
    }
    for (i = 0; i < 2; i++) {
        adpcmIopBuffUsed[i] = 0;
    }
    adpcmPause = 0;
}

inline int AdpcmNotUseIopAreaFree(void)
{
    int cnt = 0;
    int i;
    unsigned char buf[2];
    AdpcmStream *p = adpcmStream;
    AdpcmStream *end = p + 2;

    *(short *)buf = 0;

    do {
        if (p->used != 0) {
            int no = (p->iopBuf - adpcmIopBase) / 0x5C000;
            if (no < 3) {
                buf[no] = 1;
            }
        }
        p++;
    } while ((ICO_WORD)p < (ICO_WORD)end);

    i = 0;
    do {
        if (buf[i] == 0) {
            if (adpcmIopBuffUsed[i] != 0) {
                debug_StdPrintfDummy(adpcmFreeIopMsg);
                cnt++;
                adpcmIopBuffUsed[i] = 0;
            }
        }
        i++;
    } while (i < 2);
    return cnt;
}

inline SqEntry *AdpcmOpenSync(AdpcmOpenReq *self)
{
    SqEntry *r;
    debug_StdPrintfDummy("AdpcmOpensync\n");
    if (self->bg != 0)
        goto body;
    return 0;
body:
    if (self->bg->readFunc != 0) {
        return (SqEntry *)-1;
    }
    debug_StdPrintfDummy("AdpcmOpensync done\n");
    iosCdvdBackGroundMgrDelete(self->bg);
    r = adpcmDataSet(0, self->id, 0x11, self->ch, 0, self->iopBuf, self->loopNum);
    if (self->id == adpcmStartNo) {
        /* PC port (CRED): the ring holds the stream from adpcmStartBytes */
        iosCdvdBackGroundMgrSeek(r->stream->bg, adpcmStartBytes + 0x5C000);
        r->stream->remain -= adpcmStartBytes;
        adpcmStartNo = -1;
        return r;
    }
    iosCdvdBackGroundMgrSeek(r->stream->bg, 0x5C000);
    return r;
}

inline void AdpcmFadeCloseAll(short step)
{
    AdpcmStream *p = adpcmStream;
    AdpcmStream *end = p + 2;
    do {
        if (p->used != 0) {
            p->fadeStep = step;
        }
        p++;
    } while ((ICO_WORD)p < (ICO_WORD)end);
}

inline int AdpcmUseAreaGet(void)
{
    int count = 0;
    int *p = adpcmIopBuffUsed;
    int n = 1;
    do {
        int v = *p;
        int next = count + 1;
        p++;
        n--;
        if (v != 0)
            count = next;
    } while (n >= 0);
    return count;
}

inline int AdpcmFreeAreaGet(void)
{
    int count = 0;
    int *p = adpcmIopBuffUsed;
    int n = 1;
    do {
        int v = *p;
        int next = count + 1;
        p++;
        n--;
        if (v == 0)
            count = next;
    } while (n >= 0);
    return count;
}

inline void AdpcmInterStereoVolumeSetAll(void)
{
    int i;
    for (i = 0; i < 176; i += 0x58) {
        int *p = (int *)((char *)adpcmStream + i);
        if (*p != 0) {
            int v = *(int *)((char *)p + 0x38);
            if (v == 0x20000)
                goto call0;
            if (v != 0x40000)
                goto skip;
            AdpcmInterStereoVolumeSet(p, 2);
        call0:
            AdpcmInterStereoVolumeSet(p, 0);
        skip:;
        }
    }
}

inline short AdpcmInterLeaveVolumeGet(SqEntry *self, int idx)
{
    char *base = (char *)self->stream;
    base += idx * 4;
    return *(short *)(base + 0x3C);
}

inline short AdpcmVolumeGet(SqEntry *self)
{
    return self->stream->volL[0];
}

inline int adpcmTickProc(CdvdBgReq *self, SqEntry *obj)
{
    AdpcmStream *st = obj->stream;
    int size;
    int cur = SgStAdpcmIopReadAddr(st->ch[0]);

    if (cur != st->seekSize) {
        if (cur > st->seekSize) {
            size = cur - st->seekSize;
        } else {
            size = st->ringSize - st->seekSize;
        }
        if (size > 0x1EAAA || cur < st->seekSize) {
            iosCdvdBackGroundReadIOPm(self, (void *)(ICO_WORD)(st->iopBuf + st->seekSize), size);
        } else {
            size = 0;
        }
        if (self->pos >= st->dataSize) {
            iosCdvdBackGroundMgrSeek(self, st->loopStart + (self->pos - st->dataSize));
        }
        st->seekSize = st->seekSize + size;
        if (st->seekSize >= st->ringSize) {
            st->seekSize = 0;
        }
    }
    return 0;
}

static inline void adpcmDiskNotReady(void) {}

static inline void adpcmDiskReturnReady(void) {}

static inline int adpcmOpenProc(CdvdBgReq *bg, AdpcmOpenReq *open)
{
    iosCdvdBackGroundReadIOPm(bg, (void *)(ICO_WORD)open->iopBuf, 376832);
    return 1;
}

static inline void adpcmOpenDiskNotReady(void) {}
