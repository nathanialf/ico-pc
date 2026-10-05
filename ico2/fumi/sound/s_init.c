#include "typedef.h"
#include "ee_view.h"
#include "s_init.h"
#include "debug.h"
#include "cdvd.h"
#include "ios.h"
#include "memory.h"
#include "pad.h"
#include "adpcm_init.h"
#include "soundManager.h"
#include "camera-root.h"
#include "matrixDrive.h"
#include "Primitive.h"
#include <sifrpc.h>
#include <string.h>
#include <libvu0.h>
#include <eekernel.h>
#include "GifPacket.h"
#include "debug_exception.h"
#include "main.h"
#include <assert.h>
#include <sound.h>

/* The slot's 0x04 status word, written both as a whole and bit by bit. */
typedef union SeFlag { /* field names derived */
    unsigned int all;

    struct {        /* field names derived */
        short vol1; /* the second volume, which level1 is panned to */
        unsigned int playMode
            : 8; /* soundSeDefPlay's fourth argument, what soundSePlayModeStop stops by */
        unsigned int audible : 1;       /* the sound is placed at its position */
        unsigned int placed : 1;        /* set once a position has given the volume */
        unsigned int levelHeight : 1;   /* the distance is taken at the camera's height */
        unsigned int stereo : 1;        /* panned by the angle to the camera */
        unsigned int rearFade : 1;      /* quieter the further it lies behind the camera */
        unsigned int soloMute : 1;      /* silenced while another slot plays solo */
        unsigned int maxVolumeType : 1; /* the curve past maxVolumeRange */
        unsigned int : 1;
    } bit;
} SeFlag; /* derived name */

typedef struct SeSlot { /* field names derived */
    unsigned short num; /* 0x00, bumped on each release: the handle's top byte */
    short vol0;         /* 0x02, the first volume SgSetSeVolDirect is given */
    SeFlag flag;        /* 0x04 */
    unsigned int owner; /* 0x08, soundSeDefPlay's second argument, -1 for a
                            stage environment sound */
    int padAct;         /* 0x0C, the iosPadActRequest handle */
    short handle;       /* 0x10, the SgSePlay or SgBgmOpen handle */
    short level0;       /* 0x12, the panned level vol0 follows */
    short level1;       /* 0x14, the panned level flag.bit.vol1 follows */
    char pad16[2];
    float volumeRate;     /* 0x18, the labels are debug_DispSEInfo's */
    float stereoRate;     /* 0x1C */
    float attenuator;     /* 0x20 */
    float maxVolumeRange; /* 0x24 */
    float volumeLength;   /* 0x28 */
    int (*proc)();        /* 0x2C, the environment row's proc */
    SqEntry *req;         /* 0x30, the data area it plays from */
    float *pos;           /* 0x34, a position vector: every reader passes it to
                            sceVu0CopyVector and soundSeEnvPlay stores an
                            allocated block in it */
    SeDef *src;           /* 0x38 */
    const SeEnvDef *env;  /* 0x3C, the sound-environment row the slot plays */
} SeSlot;                 /* derived name */

/* The TU's own .sbss and .bss, tentative definitions: the SPU buffer
   segments' next free addresses, the ADPCM and SE channel masks, the 16
   sound data areas and the 48 SE slots. */
static int bufSeg1Next; /* derived name */

static int bufSeg2Next; /* derived name */

static long long adpcmChMask;

static long long seChMask;

static SqEntry soundDataTbl[16]; /* derived name */

static SeSlot seSlotTbl[48]; /* derived name */

/* the SPU buffer's segment-0 allocation pointer and the top of segment 1
   (soundBufAlloc), the reverb depth and output mode the Set/Get pairs keep,
   the master SE volume rate, the environment-close request and the
   semi-common load flag */
static int bufSeg0Next = 0x5010; /* derived name */

static int bufSeg1Top = 0x1D9020; /* derived name */

static int reverbDepth = 10; /* derived name */

float soundSeEnvMasterVolRate = 1.0f;

int seEnvForceClose = 0;

static int outputMode = 0; /* derived name */

static int seSemiCommonLoaded = 0; /* derived name */

inline int Ee2Iop(ICO_WORD ee, int iop, int size)
{
    sceSifDmaData d;
    int x;
    debug_StdPrintfDummy("Spu2DmaWriteEe2Iop\n");
    debug_StdPrintfDummy("ee %x iop %x size %x\n", ee, iop, size);
    d.src = ee;
    d.dest = iop;
    d.size = size;
    d.u.attr = 0;
    FlushCache(0);
    x = sceSifSetDma(&d, 1);
    while (sceSifDmaStat(x) >= 0)
        ;
    debug_StdPrintfDummy("send SpuStEnv completed \n");
    FlushCache(0);
    return (x >= 0) ? 0 : -1;
}

int soundInit(void)
{
    int i;
    char *p;

    debug_StdPrintfDummy("SgInit()\n");
    SgInit();
    if (iosCdvdMediaType == 1) {
        SgSetDigitalOutputMode(0x80);
    } else {
        SgSetDigitalOutputMode(0x880);
    }
    debug_StdPrintfDummy("SgSetTickMode()\n");
    SgSetTickMode(60);
    SgSetReverbEndAddr(0, 0x1FFFFF);
    SgSetReverbEndAddr(1, 0x1DFFFF);
    SgSetReverbType(0, 4);
    SgSetReverbDepth(0, 0xCCC, 0xCCC);
    SgSetReverbType(1, 4);
    SgSetReverbDepth(0, 0xCCC, 0xCCC);
    SgSetMasterVol(0, 0, 0);
    SgSetMasterVol(1, 0, 0);
    for (i = 15; i >= 0; i--) {
#ifdef ICO_HOST
        *(int *)&soundDataTbl[i] = 0;
#else
        *(int *)&((char *)soundDataTbl)[i * 48] = 0;
#endif
    }
    adpcmChMask = 0;
    seChMask = 0;
    /* the request slot of every SeSlot, walked through a base pointer: that is
       what keeps the +0x30 out of the symbol's %hi/%lo and in the loop start value */
#ifdef ICO_HOST
    for (i = 47; i >= 0; i--) {
        seSlotTbl[i].req = 0;
    }
#else
    p = (char *)seSlotTbl;
    for (i = 47; i >= 0; i--) {
        *(int *)&p[i * 64 + 0x30] = 0;
    }
#endif
    AdpcmStreamInit();
    seEnvForceClose = 0;
    return 0;
}

void soundOutputModeSet(int mode)
{
    outputMode = mode;
    SgSetOutputMode(mode);
}

inline int soundOutputModeGet(void)
{
    return outputMode;
}

void soundReverbDepthSet(int depth)
{
    int val;
    reverbDepth = depth;
    val = (depth * 32767) / 100;
    SgSetReverbDepth(0, val, val);
    SgSetReverbDepth(1, val, val);
    SgSetMasterVol(0, 0x3FFF, 0x3FFF);
    SgSetMasterVol(1, 0x3FFF, 0x3FFF);
}

inline int soundReverbDepthGet(void)
{
    return reverbDepth;
}

void soundAllocIopHeap(void)
{
    int r = iosSifAllocIopHeapDebug(0x78000, __FILE__, 254);
    soundIopHeapAddrs = r;
    if (r < 0) {
        debug_StdPrintfDummy("\nCan't alloc heap \n");
    } else {
        debug_StdPrintfDummy("iop sound alloced 0x%x  size %d\n", r, 0x78000);
    }
}

void soundAllocIopFree(void)
{
    sceSifFreeIopHeap(soundIopHeapAddrs);
}

/* A free channel's allocation, shared by soundDataOpenChk and
   _soundSeDefPlay. */
static inline int seChAlloc(SqEntry *req) /* derived name */
{
    long long one = 1;
    long long bit;
    int i;

    for (i = 0; i < 48; i++) {
        bit = one << i;
        if ((seChMask & bit) == 0) {
            goto found;
        }
    }
    return -1;
found:
    seChMask |= bit;
    req->seMask |= bit;
    /* the slot indexed by byte offset */
#ifdef ICO_HOST
    seSlotTbl[i].flag.all &= 0xFDFFFFFF;
#else
    ((SeSlot *)&((char *)seSlotTbl)[i * 64])->flag.all &= 0xFDFFFFFF;
#endif
    return i;
}

/* A slot's request release, shared by soundDataOpenChk, soundDataClose and
   _soundSeDefStop.  seReqRelease takes the channel, not the slot, because
   _soundSeDefStop's copy recomputes the request word's address from it. */
static inline void seReqChClear(SqEntry *req, int ch) /* derived name */
{
    long long bit = (long long)1 << ch;

    if ((req->seMask & bit) != 0) {
        req->seMask &= ~bit;
        seChMask &= ~bit;
        seSlotTbl[ch].num = seSlotTbl[ch].num + 1;
        seSlotTbl[ch].req = 0;
    }
}

static inline void seReqRelease(int ch) /* derived name */
{
    SqEntry *req = seSlotTbl[ch].req;

    if (req != 0)
        seReqChClear(req, ch);
}

inline SqEntry *soundDataAreaSearch(int *pk)
{
    int i;
    SqEntry *r;
    for (i = 0; i < 16; i++) {
        r = &soundDataTbl[i];
        if (*(int *)&soundDataTbl[i] == *pk)
            goto found;
    }
    return 0;
found:
    return r;
}

inline SqEntry *soundDataAreaGet(int no, int bank, int mode, int seg)
{
    SqEntry *e;
    int hi = bank << 16;
    int key = (no & 0xFFFF) | hi;

    e = soundDataAreaSearch(&key);
    if (e == 0) {
        key = 0;
        e = soundDataAreaSearch(&key);
        if (e == 0) {
            debug_assert(__FILE__, 334);
            __assert(__FILE__, 334, "0");
        }
        memset(e, 0, ICO_MAX_SIZE(SqEntry, 0x30));
        e->num = no;
        e->bank = bank;
        e->seg = seg;
        e->mode = mode;
        e->vab = -1;
    }
    return e;
}

static void soundDataOpenChk(SqEntry *self)
{
    int ok = 0;
    int vab;
    int ch;
    int off;
    char *slot;
    short h;
    int hr;

    switch (self->mode) {
    case 0:
        if (self->bd != 0) {
            ok = (self->hd != 0);
        }
        break;
    case 1:
        if (self->bd != 0 && self->hd != 0) {
            ok = (self->sq != 0) ? self->mode : 0;
        }
        break;
    default:
        debug_assert(__FILE__, 358);
        __assert(__FILE__, 358, "0");
        break;
    }
    if (ok == 0) {
        return;
    }
    vab = SgVabOpenFakeBody(self->hd, self->spu.buf.addr);
    self->vab = vab;
    switch (self->mode) {
    case 0:
        SgSetSeMasterVol(vab, 127);
        debug_StdPrintfDummy("se open\n");
        return;
    case 1:
        ch = seChAlloc(self);
        if (ch < 0) {
            debug_StdPrintfDummy("bgm request buff over\n");
            return;
        }
        off = ch * 64;
        hr = SgBgmOpen(self->vab, self->sq);
#ifdef ICO_HOST
        seSlotTbl[ch].handle = hr;
#else
        slot = &((char *)seSlotTbl)[off];
        *(short *)(slot + 0x10) = hr;
#endif
        h = hr;
        if (h < 0) {
            seReqRelease(ch);
            debug_StdPrintfDummy("bgm not open\n");
            return;
        }
        SgSetBgmVol(h, 64, 0xFFFF);
        SgBgmPlay(h);
#ifdef ICO_HOST
        seSlotTbl[ch].req = self;
        seSlotTbl[ch].owner = 0;
#else
        *(SqEntry **)&((char *)seSlotTbl)[off + 0x30] = self;
        *(int *)&((char *)seSlotTbl)[off + 8] = 0;
#endif
        debug_StdPrintfDummy("bgm play\n");
        return;
    default:
        return;
    }
}

void soundBufAlloc(SqEntry *self, int size)
{
    switch (self->seg) {
    case 0:
        self->spu.buf.addr = bufSeg0Next;
        bufSeg0Next = bufSeg0Next + size;
        bufSeg1Next = bufSeg0Next;
        bufSeg2Next = bufSeg0Next;
        if (bufSeg0Next > 0x1D901F) {
            debug_assert(__FILE__, 412);
            __assert(__FILE__, 412, "0");
        }
        break;
    case 1:
        switch (self->mode) {
        case 1:
            self->spu.buf.addr = bufSeg1Next;
            bufSeg1Next = bufSeg1Next + size;
            if (bufSeg1Next > bufSeg1Top) {
                debug_assert(__FILE__, 420);
                __assert(__FILE__, 420, "0");
            }
            break;
        case 0:
            bufSeg1Top = bufSeg1Top - size;
            self->spu.buf.addr = bufSeg1Top;
            if (bufSeg1Top < bufSeg1Next) {
                debug_assert(__FILE__, 424);
                __assert(__FILE__, 424, "0");
            }
            break;
        default:
            debug_assert(__FILE__, 428);
            __assert(__FILE__, 428, "0");
        }
        break;
    case 2:
        switch (self->mode) {
        case 0:
            self->spu.buf.addr = bufSeg2Next;
            bufSeg2Next = bufSeg2Next + size;
            break;
        default:
            debug_assert(__FILE__, 443);
            __assert(__FILE__, 443, "0");
        }
        break;
    default:
        debug_assert(__FILE__, 448);
        __assert(__FILE__, 448, "0");
    }
    self->spu.buf.size = size;
}

void soundBufSegFree(int seg, int mode)
{
    switch (seg) {
    case 1:
        switch (mode) {
        case 1:
            bufSeg1Next = bufSeg0Next;
            return;
        case 0:
            bufSeg1Top = 0x1D9020;
            return;
        case 2:
            return;
        }
        debug_assert(__FILE__, 472);
        __assert(__FILE__, 472, "0");
        return;
    case 2:
        if (mode == 0) {
            bufSeg2Next = bufSeg0Next;
            return;
        }
        debug_assert(__FILE__, 482);
        __assert(__FILE__, 482, "0");
        return;
    }
    debug_assert(__FILE__, 487);
    __assert(__FILE__, 487, "0");
}

inline int soundBufAdpcmChAlloc(SqEntry *self, int *chp)
{
    int ch;
    long long bit = 1;

    for (ch = 0; ch < 64U; ch++) {
        if ((adpcmChMask & (bit << ch)) == 0)
            goto found;
    }
    debug_assert(__FILE__, 500);
    __assert(__FILE__, 500, "0");
found:
    self->spu.chMask |= bit << ch;
    adpcmChMask |= bit << ch;
    if (ch >= 5) {
        debug_StdPrintfDummy("soundBufAdpcmChAlloc over\n");
        debug_assert(__FILE__, 504);
        __assert(__FILE__, 504, "0");
    }
    *chp = ch;
    if (ch < 0) {
        return (ch << 14) + 0x1D9020;
    } else {
        return (ch << 14) + 0x1E0000;
    }
}

inline void soundBufAdpcmFree(SqEntry *self)
{
    long long mask = ~self->spu.chMask;
    adpcmChMask &= mask;
    self->spu.chMask = 0;
}

SqEntry *soundBDDataSet(ICO_WORD_PTR(void *) bd, int no, int bank, int mode, int seg, int size)
{
    int off = 0;
    SqEntry *e;
    int chunk;

    e = soundDataAreaGet(no, bank, mode, seg);
    e->bd = bd;
    size = (((size - 1) / 64) + 1) * 64;
    soundBufAlloc(e, size);
    while (size > 0) {
        chunk = (size > 0x78000) ? 0x78000 : size;
        SgGetDmaTransferStatus(1);
        Ee2Iop((ICO_WORD)bd + off, soundIopHeapAddrs, chunk);
        if (chunk >= 65) {
            SgDmaWrite(soundIopHeapAddrs, e->spu.buf.addr + off, chunk);
        } else {
            SgDmaWrite(soundIopHeapAddrs, e->spu.buf.addr + off, 0x50);
        }
        if (e->mode == 1) {
            SgGetDmaTransferStatus(1);
        }
        size = size - chunk;
        off = off + chunk;
    }
    soundDataOpenChk(e);
    return e;
}

inline SqEntry *soundHDDataSet(void *hd, int no, int bank, int mode, int seg)
{
    SqEntry *e = soundDataAreaGet(no, bank, mode, seg);
    e->hd = hd;
    soundDataOpenChk(e);
    return e;
}

inline SqEntry *soundSQDataSet(void *sq, int no, int bank, int mode, int seg)
{
    SqEntry *e = soundDataAreaGet(no, bank, mode, seg);
    e->sq = sq;
    soundDataOpenChk(e);
    return e;
}

void soundDataOpen(AdpcmOpenReq *work, int mode, int no, int ch, int loopNum)
{
    work->mode = mode;
    switch (mode) {
    case 0:
        debug_assert(__FILE__, 614);
        __assert(__FILE__, 614, "0");
        break;
    case 1:
        debug_assert(__FILE__, 617);
        __assert(__FILE__, 617, "0");
        break;
    case 2:
        AdpcmOpen(work, no, ch, loopNum);
        break;
    default:
        debug_assert(__FILE__, 623);
        __assert(__FILE__, 623, "0");
    }
}

SqEntry *soundDataOpenSync(AdpcmOpenReq *work)
{
    switch (work->mode) {
    case 0:
        debug_assert(__FILE__, 631);
        __assert(__FILE__, 631, "0");
        break;
    case 1:
        debug_assert(__FILE__, 634);
        __assert(__FILE__, 634, "0");
        break;
    case 2:
        return AdpcmOpenSync(work);
    default:
        debug_assert(__FILE__, 640);
        __assert(__FILE__, 640, "0");
    }
    return 0;
}

void soundDataClose(SqEntry *self)
{
    int i;
    short h;

    switch (self->mode) {
    case 0:
        SgVabClose(self->vab);
        break;
    case 1:
        i = 0;
        while (self->seMask != 0) {
            if ((int)((self->seMask >> i) & 1)) {
                h = seSlotTbl[i].handle;
                SgBgmStop(h, 1);
                SgBgmClose(h);
                seReqRelease(i);
            }
            i++;
        }
        SgVabClose(self->vab);
        iosFree(self->hd);
        if (self->sq != 0) {
            iosFree(self->sq);
        }
        break;
    case 2:
        AdpcmClose(self);
        break;
    }
    *(int *)self = 0;
}

void soundDataSegAllClose(int seg, int mode)
{
    int i;
    char *tbl;
    /* base in the loop header, as its sibling
       soundDataSegNextStageNotUseClose carries it */
    for (i = 0, tbl = (char *)soundDataTbl; i < 768; i += 0x30) {
        SqEntry *p = (SqEntry *)(tbl + i);
        if (*(int *)p != 0 && p->seg == seg && p->mode == mode) {
            soundDataClose(p);
        }
    }
    if (mode == 2)
        return;
    soundBufSegFree(seg, mode);
}

static void soundSeVolSet(SeSlot *self)
{
    int l;
    int r;
    int cur;
    int d;
    int vol;

    if (self->flag.all & 0x20000000) {
        r = 0;
        l = 0;
    } else {
        l = (int)((float)self->level0 * self->volumeRate);
        r = (int)((float)self->level1 * self->volumeRate);
    }
    if (self->owner == 0xFFFFFFFF && self->env != 0) {
        l = (int)((float)l * soundSeEnvMasterVolRate);
        r = (int)((float)r * soundSeEnvMasterVolRate);
    }
    if (l < 0) {
        l = 0;
    } else {
        l = (l < 4097) ? l : 4096;
    }
    if (r < 0) {
        r = 0;
    } else {
        r = (r < 4097) ? r : 4096;
    }
    cur = *(unsigned short *)&self->vol0;
    d = (short)(l - cur);
    if (((d < 0) ? -d : d) < 256 || (short)cur == -1) {
        self->vol0 = l;
    } else {
        self->vol0 = (d > 0) ? cur + 256 : cur - 256;
    }
    cur = *(unsigned short *)&self->flag;
    d = (short)(r - cur);
    if (((d < 0) ? -d : d) < 256 || (short)cur == -1) {
        *(short *)&self->flag = r;
    } else {
        *(short *)&self->flag = (d > 0) ? cur + 256 : cur - 256;
    }
    SgSetSeVolDirect(self->handle, self->vol0, *(short *)&self->flag);
    if (self->padAct != 0) {
        vol = (r < l) ? l : r;
        vol = (int)((float)vol * (1.0f / 4096.0f) * 255.0f);
        iosPadActVolumeSet(self->padAct, vol & 0xFF);
    }
}

/* The debug SE-info page: one row per editable field of the slot the pad is
   parked on, walked with the D-pad and nudged by `step`.  The row is a label
   followed by a nested value record. */
typedef int (*SeProcFn)(); /* derived name */

typedef struct DbgVal {       /* field names derived */
    ICO_WORD_PTR(void *) ptr; /* 0x04 */
    int type;                 /* 0x08 : 0 = int cell, 1 = float cell */
    int mode;                 /* 0x0C : 1 = colour the row when the value is past `dist` */
    float step;               /* 0x10 */
} DbgVal;                     /* derived name */

typedef struct DbgRow { /* field names derived */
    char *label;        /* 0x00 */
    DbgVal v;           /* 0x04 */
} DbgRow;               /* derived name */

static inline void soundSeEnvDefaultSet(SeSlot *self);

static void debug_DispSEInfo(void)
{
    /* dbg is a local debug switch, set from the debugger to keep the selected
       slot soloed.  The page's own state is local static: the selected row
       and the solo flag in .sbss, the edited centre in .bss. */
    static sceVu0FVECTOR center;
    static int curRow;
    static int solo;
    static int page = 0; /* derived name */
    static int show = 1; /* derived name */
    float v[4];
    float step = 0.0f;
    float dist;
    float *cam;
    SeSlot *self;
    SeSlot *p;
    int i;
    int num;
    int dbg = 0; /* local debug switch, see the test after the solo toggle */

    cam = GetCameraPos();
    if (pad[0].flags & 0x400) {
        show ^= 1;
    }
    if (show == 0) {
        return;
    }
    if (pad[0].rep & 0x20) {
        page = page + 1;
    }
    for (i = page;; i++) {
        if (i >= 48) {
            page = 0;
            return;
        }
        if (seSlotTbl[i].req != 0) {
            break;
        }
    }
    self = &seSlotTbl[i];
    page = i;
    if (pad[0].flags & 0x40) {
        solo ^= 1;
    }
    if (dbg) {
        solo = 1;
    }
    for (i = 0; i < 48; i++) {
#ifdef ICO_HOST
        if (seSlotTbl[i].req == 0) {
#else
        if (*(int *)&((char *)seSlotTbl)[i * 64 + 0x30] == 0) {
#endif
            continue;
        }
        p = &seSlotTbl[i];
        if (self == p) {
            self->flag.bit.soloMute = 0;
        } else {
            p->flag.bit.soloMute = solo;
        }
    }
    if (pad[0].flags & 0x80) {
        soundSeEnvDefaultSet(self);
    }
    if (self->pos != 0) {
        sceVu0CopyVector(center, self->pos);
        if (self->flag.bit.levelHeight == 1) {
            center[1] = cam[1];
        }
        sceVu0SubVector(v, cam, center);
        dist = FSqrt(sceVu0InnerProduct(v, v));
    } else {
        dist = center[0] = center[1] = center[2] = 0.0f;
    }
    {
        int sel = self->flag.bit.maxVolumeType;
        DbgRow *row;
        DbgRow *cur;
        DbgRow list[9] = {
            {"center x", {(ICO_WORD_PTR(void *)) & center[0], 1, 0, 10.0f}},
            {"center y", {(ICO_WORD_PTR(void *)) & center[1], 1, 0, 10.0f}},
            {"center x", {(ICO_WORD_PTR(void *)) & center[2], 1, 0, 10.0f}},
            {"volumeRate", {(ICO_WORD_PTR(void *)) & self->volumeRate, 1, 1, 0.1f}},
            {"max volume range", {(ICO_WORD_PTR(void *)) & self->maxVolumeRange, 1, 1, 10.0f}},
            {"attenuator", {(ICO_WORD_PTR(void *)) & self->attenuator, 1, 1, 10.0f}},
            {"volume length", {(ICO_WORD_PTR(void *)) & self->volumeLength, 1, 1, 10.0f}},
            {"max volume type", {(ICO_WORD_PTR(void *)) & sel, 0, 0, 1.0f}},
            {"stereo rate", {(ICO_WORD_PTR(void *)) & self->stereoRate, 1, 0, 0.05f}},
        };

        num = 9;

        if (pad[0].rep & 0x1000) {
            curRow = curRow - 1;
        }
        if (pad[0].rep & 0x4000) {
            curRow = curRow + 1;
        }
        curRow = (curRow + num) % num;
        cur = &list[curRow];
        if (pad[0].rep & 0x2000) {
            step = cur->v.step;
        }
        if (pad[0].rep & 0x8000) {
            step = -cur->v.step;
        }
        debug_PrintfDummy(10, 70, 0xFFFFFF00u, "req no %d %s %f\n", page, self->src, dist);
        for (i = 0, row = list; i < num; i++, row++) {
            int col = 0xFFFFFF00;

            if (curRow == i)
                strcpy((char *)v, ">");
            else
                strcpy((char *)v, " ");
            switch (row->v.type) {
            case 0:
                if (curRow == i) {
                    *(int *)row->v.ptr = (int)((float)*(int *)row->v.ptr + step);
                }
                debug_PrintfDummy(10, i * 10 + 80, col, "%s%8s = %d\n", v, row->label,
                                  *(int *)list[i].v.ptr);
                break;
            case 1:
                if (curRow == i) {
                    *(float *)row->v.ptr += step;
                }
                if (row->v.mode && dist != 0.0f && dist < *(float *)row->v.ptr) {
                    col = 0xFF000000;
                }
                debug_PrintfDummy(10, i * 10 + 80, col, "%s%8s = %f\n", v, row->label,
                                  *(float *)list[i].v.ptr);
                break;
            }
        }
        self->flag.bit.maxVolumeType = sel;
        if (self->pos != 0) {
            sceVu0CopyVector(self->pos, center);
            MatrixDrive_PushMatrix();
            {
                sceVu0IVECTOR col = {0x00, 0x10, 0x20, 0x80};

                gif_StartPacketPri(11);
                sceVu0UnitMatrix(MatrixDrive_GetMatrix());
                MatrixDrive_TransMatrixV((char *)center);
                prim_DispWireSphere(100.0f, col, 16, 8);
                gif_EndPacket();
            }
            MatrixDrive_PopMatrix();
        }
    }
}

static void sound3DParamSet(SeSlot *self)
{
    float v[4];
    float dist;
    int ang;
    float *cam;
    float *obj;
    float vol;
    float rate;
    float range;
    float front;
    float volL;
    float volR;
    int n;
    int a;
    int pan;
    int ret;

    self->level1 = 0x1000;
    self->level0 = 0x1000;
    if (self->proc != 0) {
#ifdef ICO_HOST
        /* the EE call leaves the slot in $a0, which every stageSE proc reads
           as its argument; the host passes it */
        ret = self->proc(self);
#else
        ret = self->proc();
#endif
        if (ret > 0) {
            self->flag.bit.audible = 1;
            self->flag.bit.placed = 0;
        } else if (ret < 0) {
            self->flag.bit.audible = 0;
        } else {
            self->level0 = 0;
            self->level1 = 0;
            self->flag.bit.audible = 0;
        }
        self->src->procRan = 1;
    }
    if ((self->flag.all & 0x02000000) && self->src->procRan == 0) {
        if (self->flag.all & 0x20000000) {
            SgSetSeVolDirect(self->handle, 0, 0);
        }
        return;
    }
    if ((self->flag.all & 0x01000000) == 0 || (obj = self->pos) == 0) {
        soundSeVolSet(self);
        return;
    }
    self->flag.all |= 0x02000000;
    if (self->flag.bit.levelHeight == 1) {
        cam = GetCameraPos();
        sceVu0CopyVector(v, self->pos);
        v[1] = cam[1];
        CameraGetOtherObjOffset(v, &dist, &ang);
    } else {
        CameraGetOtherObjOffset(obj, &dist, &ang);
    }
    if (dist > self->volumeLength) {
        vol = 0.0f;
    } else if (dist < self->maxVolumeRange) {
        vol = 1.0f;
    } else {
        dist = dist - self->maxVolumeRange;
        range = self->attenuator - self->maxVolumeRange;
        if (self->flag.bit.maxVolumeType == 0) {
            rate = dist / range;
            vol = 1.0f / (rate + 1.0f);
        } else {
            if (dist < range) {
                rate = dist / range;
                vol = 1.0f / (rate + 1.0f);
            } else {
                dist = dist - range;
                vol = 1.0f / (dist / ((self->volumeLength - self->maxVolumeRange) - range) + 1.0f) -
                      0.5f;
            }
        }
    }
    if (self->flag.bit.stereo == 1) {
        a = ang;
        pan = a;
        if (self->flag.bit.rearFade == 1) {
            a = (a <= -1) ? -a : a;
            front = (float)a * -0.0027777778f + 1.0f;
        } else {
            front = 1.0f;
        }
        if (pan >= 0) {
            volL = 1.0f;
            a = pan;
            if (a >= 91) {
                a = 180 - a;
            }
            volR = -(1.0f - self->stereoRate) / 90.0f * (float)a + 1.0f;
        } else {
            volR = 1.0f;
            a = -pan;
            if (a >= 91) {
                a = 180 - a;
            }
            volL = -(1.0f - self->stereoRate) / 90.0f * (float)a + 1.0f;
        }
    } else {
        volR = 1.0f;
        front = 1.0f;
        volL = 1.0f;
    }
    n = (int)(vol * 4096.0f * front);
    self->level0 = (float)n * volR;
    self->level1 = (float)n * volL;
    soundSeVolSet(self);
}

/* as in the generated sedef member, which defines the rows const; this TU
   writes procRan into them */
extern SeDef seDef[];

inline void soundSeGroupStop(int arg)
{
    SeSlot *p = seSlotTbl;
    int i = 0;
    do {
        SqEntry *req = p->req;
        if (req != 0) {
            if (p->owner == arg) {
                if (p->src->shockStop) {
                    if (req->mode == 0) {
                        soundSeDefStop(((int)p->num << 8) | i);
                    }
                }
            }
        }
        i++;
        p++;
    } while (i < 48);
}

/* The slot search between soundSeGroupStop and soundSeGroupGet, which
   _soundSeDefPlay inlines twice. */
static inline int se_find_slot(SeDef *src, SeSlot **out) /* derived name */
{
    SeSlot *p = seSlotTbl;
    int i;

    for (i = 0; i < 48; i++) {
        if (p->req != 0 && p->src == src) {
            goto found;
        }
        p++;
    }
    if (out != 0) {
        *out = 0;
    }
    return -1;
found:
    if (out != 0) {
        *out = p;
    }
    return i;
}

static int seGroupSerial = 0; /* derived name */

/* soundIopHeapAddrs is the last object of the TU's .sdata, after the group
   serial above, so its definition sits here although soundAllocIopHeap
   (declared through s_init.h) fills it first. */
int soundIopHeapAddrs = 0;

inline int soundSeGroupGet(void)
{
    int next = ((seGroupSerial + 1) & 0x0FFFFFFF) | 0x10000000;
    seGroupSerial = next;
    return next;
}

inline void soundSePlayModeStop(int arg)
{
    SeSlot *p = seSlotTbl;
    int i = 0;
    do {
        SqEntry *req = p->req;
        if (req != 0) {
            if (p->flag.bit.playMode == arg) {
                if (req->mode == 0) {
                    soundSeDefStopNoRelease(((int)p->num << 8) | i);
                }
            }
        }
        i++;
        p++;
    } while (i < 48);
}

static int _soundSeDefPlay(int kind, unsigned int owner, float *pos, int playMode,
                           const SeEnvDef *env, SeSlot **out, float vol)
{
    SeDef *src;
    const SeKind *def;
    SqEntry *e;
    SeSlot *slot;
    unsigned short *kp;
    ICO_WORD_PTR(SeProcFn) cb;

    /* the search key as a bank:num pair written field by field, bank first */
    union { /* field names derived */
        int all;

        struct {
            unsigned int num : 16;
            unsigned int bank : 16;
        } b;
    } key;

    int r;
    int ch;
    int t;

    src = &seDef[kind];
    kp = &seKind[src->kind];
    cb = 0;
    if (out != 0) {
        *out = 0;
    }
    if (env != 0) {
        cb = (ICO_WORD_PTR(SeProcFn))env->proc;
    }
    if (*kp == 0) {
        return -2;
    }
    def = &seList[*kp];
    if (kind == 0 || kind >= 1426) {
        return -2;
    }
    key.b.bank = 11;
    key.b.num = def->num;
    e = soundDataAreaSearch(&key.all);
    if (e == 0) {
        return -3;
    }
    t = e->seg;
    if (t == 1)
        playMode = (playMode != 0) ? playMode : t;
    switch (src->playMode) {
    case 1:
        if (se_find_slot(src, out) < 0) {
            break;
        }
        if (out == 0 || env == 0 || (*out)->env == 0 || memcmp(env, (*out)->env, 0x1C) == 0) {
            return -1;
        }
        goto stop_old;
    case 2:
    stop_old:
        r = se_find_slot(src, out);
        if (r != -1) {
            soundSeDefStop((seSlotTbl[r].num << 8) | r);
        }
        break;
    case 0:
        break;
    }
    ch = seChAlloc(e);
    if (ch < 0) {
        debug_StdPrintfDummy("se request buff over\n");
        return -1;
    }
    slot = &seSlotTbl[ch];
    if (out != 0) {
        *out = slot;
    }
    slot->flag.bit.playMode = playMode;
    slot->src = src;
    slot->flag.bit.audible = src->audible;
    if (vol < 0.0f) {
        slot->volumeRate = src->volume;
    } else {
        slot->volumeRate = vol;
    }
    slot->attenuator = 1000.0f;
    slot->maxVolumeRange = 500.0f;
    slot->volumeLength = 3000.0f;
    slot->pos = pos;
    slot->level0 = slot->level1 = 4096;
    slot->vol0 = slot->flag.bit.vol1 = -1;
    slot->flag.bit.soloMute = 0;
    slot->flag.bit.levelHeight = 0;
    slot->flag.bit.stereo = slot->flag.bit.rearFade = 1;
    slot->flag.bit.maxVolumeType = 1;
    slot->stereoRate = 0.1f;
    slot->proc = (int (*)())cb;
    slot->owner = owner;
    slot->env = env;
    if (stage_no == 37) {
        slot->volumeLength = 10000.0f;
    }
    if ((slot->handle = SgSePlay(e->vab, def->prog, def->tone)) < 0) {
        seReqChClear(e, ch);
        debug_StdPrintfDummy("se not open\n");
        return -1;
    }
    if (src->shock != 0) {
        slot->padAct = iosPadActRequest(boyPad, src->shock);
    } else {
        slot->padAct = 0;
    }
    slot->req = e;
    return (slot->num << 8) | ch;
}

inline int soundSeDefPlay(int kind, unsigned int owner, float *pos, int playMode)
{
    int idx = _soundSeDefPlay(kind, owner, pos, playMode, 0, 0, -1.0f);
    if (idx >= 0) {
        sound3DParamSet(&seSlotTbl[idx & 0xFF]);
    }
    return idx;
}

inline int soundSeDefPlayWithVolumeRate(int kind, unsigned int owner, float *pos, int playMode,
                                        float rate)
{
    int idx = _soundSeDefPlay(kind, owner, pos, playMode, 0, 0, rate);
    if (idx >= 0) {
        sound3DParamSet(&seSlotTbl[idx & 0xFF]);
    }
    return idx;
}

void _soundSeDefStop(int id, int noRelease)
{
    int ch = id & 0xFF;
    SeSlot *self = &seSlotTbl[ch];
    short h;
    SeDef *src;

    h = self->handle;
    if (h < 0)
        return;
    id = id >> 8;
    if (id != self->num)
        return;
    seReqRelease(ch);
    if (noRelease == 0) {
        SgSeStop(h);
    } else {
        SgSeStop(h | 0x8000);
    }
    src = self->src;
    if (src->shockStop == 1 || shockList[src->shock].life == 0) {
        if (self->padAct != 0)
            iosPadActStop(self->padAct);
    }
}

void soundSeDefStop(int id)
{
    _soundSeDefStop(id, 0);
}

void soundSeDefStopNoRelease(int id)
{
    _soundSeDefStop(id, 1);
}

/* sound.h leaves it out */
extern void SgSetSePitchDirect(unsigned int id, int pitch);

void soundSeDefPitchSet(int id, int pitch)
{
    SeSlot *entry;
    short h;
    entry = &seSlotTbl[id & 0xFF];
    h = entry->handle;
    if (h < 0)
        return;
    id = id >> 8;
    if (id != entry->num)
        return;
    SgSetSePitchDirect(h, pitch);
}

inline float soundSeDefVolumeRateGet(int id)
{
#ifdef ICO_HOST
    SeSlot *e = &seSlotTbl[id & 0xFF];
    if (e->handle >= 0) {
        goto check;
    }
fail:
    return 0.0f;
check:
    id = id >> 8;
    if (id != e->num) {
        goto fail;
    }
    return e->volumeRate;
#else
    int off = (id & 0xFF) * 64;
    char *e = (char *)seSlotTbl + off;
    if (*(short *)(e + 0x10) >= 0) {
        goto check;
    }
fail:
    return 0.0f;
check:
    id = id >> 8;
    if (id != *(unsigned short *)e) {
        goto fail;
    }
    return *(float *)((char *)seSlotTbl + off + 0x18);
#endif
}

inline void soundSeDefVolumeRateSet(int id, float rate)
{
#ifdef ICO_HOST
    SeSlot *e = &seSlotTbl[id & 0xFF];
    if (e->handle >= 0) {
        id = id >> 8;
        if (id == e->num) {
            e->volumeRate = rate;
        }
    }
#else
    int off = (id & 0xFF) * 64;
    char *e = (char *)seSlotTbl + off;
    if (*(short *)(e + 0x10) >= 0) {
        id = id >> 8;
        if (id == *(unsigned short *)e) {
            *(float *)((char *)seSlotTbl + off + 0x18) = rate;
        }
    }
#endif
}

inline void soundReqTickProc(void)
{
    SeSlot *p = seSlotTbl;
    int i = 0;
    do {
        if (p->req != 0) {
            int r = SgGetSlotStatus(1, p->handle);
            if (r == 0) {
                soundSeDefStop(((int)p->num << 8) | i);
            } else if (r & 2) {
                if (debug_seslotdisp_flag == 0) {
                    if (systemStatus[5] != 0 && p->owner != 0xFFFFFFFF && p->owner != 0xFFFFFFFE) {
                        p->flag.all |= 0x20000000;
                    } else {
                        p->flag.all &= 0xDFFFFFFF;
                    }
                }
                sound3DParamSet(p);
            }
        }
        i++;
        p++;
    } while (i < 48);
}

inline void soundVBlank(void)
{
    int i;
    for (i = 0; i < 768; i += 0x30) {
        SqEntry *p = (SqEntry *)((char *)soundDataTbl + i);
        if (p->bank == 17) {
            adpcmTickProc2(p);
        }
    }
}

inline void soundSeKindBuild(void)
{
    int i;
    int j;
    unsigned short num;
    char *e;

    for (i = 1419; i >= 0; i--) {
        seKind[i] = 0;
    }

    for (i = 0; i < 16; i++) {
        e = (char *)&soundDataTbl[i];
        if (*(unsigned short *)(e + 2) == 11) {
            num = *(unsigned short *)e;
            for (j = 0; j < 3837; j++) {
                const SeKind *p = &seList[j];
                if (p->num == num) {
                    seKind[p->idx] = j;
                }
            }
        }
    }
}

inline int soundSeSemiCommonLoadChk(void)
{
    return seSemiCommonLoaded;
}

static inline void soundSeEnvDefaultSet(SeSlot *self)
{
    const SeEnvDef *env = self->env;

    if (env->volumeRate != 0.0f) {
        self->volumeRate = env->volumeRate;
    } else {
        self->volumeRate = self->src->volume;
    }
    if (env->maxVolumeRange != 0.0f) {
        self->maxVolumeRange = env->maxVolumeRange;
    } else {
        self->maxVolumeRange = 500.0f;
    }
    if (env->attenuator != 0.0f) {
        self->attenuator = env->attenuator;
    } else {
        self->attenuator = 1000.0f;
    }
    if (env->volumeLength != 0.0f) {
        self->volumeLength = env->volumeLength;
    } else {
        self->volumeLength = 3000.0f;
    }
    self->flag.bit.maxVolumeType = env->maxVolumeType;
    self->flag.bit.levelHeight = env->levelHeight;
    self->flag.bit.stereo = env->stereo;
    self->stereoRate = 0.1f;
}

void soundSeEnvPlay(void)
{
    SeSlot *slot;
    int i;

    for (i = stageData[stage_no].seEnvFirst; i < stageData[stage_no].seEnvLast; i++) {
        const SeEnvDef *e = &seEnv[i];
        _soundSeDefPlay(e->se, 0xFFFFFFFF, 0, 0, e, &slot, -1.0f);
        if (slot != 0) {
            slot->env = e;
            soundSeEnvDefaultSet(slot);
            if (e->ownPos == 1) {
                slot->pos = iosMallocDebug(ios_partition_sound, 16, __FILE__, 1565);
            }
        }
    }
}

/* &stageData[0].seEnvFirst, the range soundSeEnvNotUseClose walks */
#ifdef ICO_HOST
#define D_005F5E60 ((char *)&stageData[0].seEnvFirst)
#else

extern char D_005F5E60[];

#endif

void soundSeEnvNotUseClose(int a, int b)
{
    const SeBank *p = 0;
    SeSlot *e;
    SqEntry *q;
    int ok;
    int i;
    int n;
    int k;
    int idx;
    int m;
    int j;
    int x;
    int *first;
    SqEntry *req;

    ok = 1;
    for (i = stageData[a].seSegFirst; i < stageData[a].seSegLast; i++) {
        if (seFile[i].loaded == 1) {
            p = &seFile[i];
            break;
        }
    }
    n = i;
    if (p != 0) {
        for (i = stageData[b].seSegFirst; i < stageData[b].seSegLast; i++) {
            if (seFile[i].loaded == 1) {
                if (strcmp(p->hdPath, seFile[i].hdPath) == 0) {
                    ok = 0;
                }
                break;
            }
        }
    }
    if (ok == 0) {
        idx = -1;
        for (k = 0; k < 16; k++) {
            q = &soundDataTbl[k];
            if (q->bank == 11) {
                if (q->num == i) {
                    idx = k;
                    break;
                }
            }
        }
        if (idx >= 0) {
            soundDataTbl[idx].num = n;
        }
    }
    if (seEnvForceClose != 0) {
        ok = 1;
    }
    for (m = 0; m < 48; m++) {
        e = &seSlotTbl[m];
        req = e->req;
        first = (int *)&D_005F5E60[a * 404];
        if (req != 0 && req->mode == 0 && e->owner == 0xFFFFFFFF) {
            for (j = *first; j < *(int *)&D_005F5E60[a * 404 + 4]; j++) {
                if (e->src == &seDef[seEnv[j].se]) {
                    if (ok == 0 || seFile[seList[seKind[e->src->kind]].num].loaded != 1) {
                        goto next;
                    }
                }
            }
            if (ok != 0 && seList[seKind[e->src->kind]].num >= 7 && a != 10) {
                soundSeDefStopNoRelease((e->num << 8) | m);
            } else if (e->src != &seDef[415] && e->src != &seDef[414]) {
                soundSeDefStop((e->num << 8) | m);
            }
        }
    next:;
    }
    if (seSemiCommonLoaded == 0) {
        soundDataSegAllClose(2, 0);
    }
    if (ok != 0) {
        if (p != 0 || seEnvForceClose != 0) {
            soundDataSegAllClose(2, 0);
            seSemiCommonLoaded = 2;
        } else {
            seSemiCommonLoaded = 0;
        }
    } else {
        seSemiCommonLoaded = 1;
    }
    seEnvForceClose = 0;
    for (m = 0; m < 48; m++) {
        SeSlot *s = &seSlotTbl[m];
        SqEntry *r = s->req;

        if (r != 0 && r->mode == 0 && s->env != 0) {
            x = *(int *)s->env;
            if (x < 430) {
                if (x >= 426) {
                    soundSeDefStop((s->num << 8) | m);
                }
            }
        }
    }
}

void soundDataSegNextStageNotUseClose(int mode, int stage)
{
    int i;
    int closed = 0;
    int found = 0;
    char *tbl;
    /* base in the loop header, not in a declaration of its own: that is what
       keeps the walk on the entry pointer and the test against the table end */
    for (i = 0, tbl = (char *)soundDataTbl; i < 768; i += 0x30) {
        SqEntry *p = (SqEntry *)(tbl + i);
        if (*(int *)p != 0 && p->seg == 1 && p->mode == mode) {
            found = 1;
            switch (mode) {
            case 0:
                break;
            case 1:
                if (stageData[stage].seSegData1 != p->num) {
                    closed++;
                    soundDataClose(p);
                    soundBufSegFree(1, 1);
                }
                break;
            case 2:
                if (stageData[stage].seSegData2 != p->num) {
                    soundDataClose(p);
                }
                break;
            default:
                debug_assert(__FILE__, 1741);
                __assert(__FILE__, 1741, "0");
                break;
            }
        }
    }
    if (mode == 1) {
        sndInitBgmCancelFlag = 0;
        if (found != 0 && closed == 0) {
            sndInitBgmCancelFlag = mode;
        }
    }
}

inline int debug_req(void)
{
#ifdef ICO_HOST
    SeSlot *e = seSlotTbl;
    int i = 0x2F;
    do {
        if (e->req != 0) {
            debug_StdPrintfDummy("num %d %d\n", e->handle, (unsigned int)(e->src - seDef));
        }
        e++;
        i--;
    } while (i >= 0);
#else
    char *e = (char *)seSlotTbl;
    int sz = 0x3C;
    int i = 0x2F;
    do {
        if (*(int *)(e + 0x30) != 0) {
            debug_StdPrintfDummy("num %d %d\n", *(short *)(e + 0x10),
                                 (unsigned int)(*(int *)(e + 0x38) - (int)seDef) / sz);
        }
        e += 0x40;
        i--;
    } while (i >= 0);
#endif
    ICO_BREAK();
}
