#include "mv_defs.h"
#include "debug.h"
#include "mv_sub.h"
#include "mv_audiodec.h"
#include <eekernel.h>
#include <sifrpc.h>
#include <sound.h>
#include "memory.h"

static void Free(int addr);

/* the EE side ring audioDecCreate allocated */
static void free_buf(AudioDec *self)
{
    Free(self->ring);
}

#include "s_init.h"

int audioDecCreate(AudioDec *self, int mono, int volume)
{
    int pcm[4];
    int p;
    int size = 24576;
    int bufsize = 49152;

    self->pcmInited = 0;
    self->ch1Open = 0;
    self->ch0Open = 0;

    p = alloc_zeroed(bufsize, 64);
    if (p == 0) {
        return -1;
    }

    self->state = 0;
    self->headerBytes = 0;
    self->ring = p;
    self->ringSize = bufsize;
    self->writePos = 0;
    self->filled = 0;
    self->putTotal = 0;
    self->sentTotal = 0;
    self->iopSize = size;
    self->iopPos = 0;
    self->word50 = 0;

    if (ICO_AUG6) {
        /* the IOP side buffer is the sound system's heap block (s_init.c) */
        self->iopBuf = soundIopHeapAddrs;
        debug_StdPrintfDummy("adrs %d\n", soundIopHeapAddrs);
    } else {
        self->iopBuf = sceSifAllocIopHeap(size);
        if (self->iopBuf == 0) {
            debug_StdPrintfDummy("Cannot allocate IOP memory\n");
            return 0;
        }
    }
    debug_StdPrintfDummy("Allocate IOP memory 0x%08x\n", self->iopBuf);

    self->mono = mono;
    self->volume = volume;
    SgStPcmInit();
    self->pcmInited = 1;

    pcm[0] = 0;
    pcm[1] = 0x10400;
    pcm[2] = self->iopBuf;
    pcm[3] = size;
    if ((self->ch0Open = (SgStPcmOpen(pcm) == 0)) == 0) {
        return -1;
    }

    pcm[0] = 1;
    pcm[1] = 0x10400;
    pcm[2] = self->iopBuf + 0x200;
    pcm[3] = size;
    if ((self->ch1Open = (SgStPcmOpen(pcm) == 0)) == 0) {
        return -1;
    }

    SgStPcmSetEffect(8);
    return 0;
}

inline int audioDecDelete(AudioDec *self)
{
    if (self->iopBuf) {
        if (sceSifFreeIopHeap(self->iopBuf) < 0) {
            debug_StdPrintfDummy("iop heap free failed 0x%08x\n", self->iopBuf);
        }
        debug_StdPrintfDummy("free iop heap 0x%08x\n", self->iopBuf);
        self->iopBuf = 0;
    }
    if (self->ch0Open) {
        SgStPcmClose(0);
    }
    if (self->ch1Open) {
        SgStPcmClose(1);
    }
    if (self->pcmInited) {
        SgStPcmQuit();
    }
    return 1;
}

inline void audioDecReset(AudioDec *self)
{
    SgStPcmVolume(3, 0, 0);
    SgStPcmStop(3);
    *(volatile int *)&self->word50 = 0;
    *(volatile int *)&self->state = 0;
    *(volatile int *)&self->headerBytes = 0;
    *(volatile int *)&self->writePos = 0;
    *(volatile int *)&self->filled = 0;
    *(volatile int *)&self->putTotal = 0;
    *(volatile int *)&self->sentTotal = 0;
    *(volatile int *)&self->iopPos = 0;
}

static void audioDecEndPut(AudioDec *self, int n)
{
    unsigned int k;
    unsigned int cnt;
    unsigned int room;

    if (self->state == 0) {
        cnt = self->headerBytes;
        room = 40 - cnt;
        k = (room < n) ? room : n;
        cnt += k;
        self->headerBytes = cnt;
        if (cnt >= 40) {
            self->state = 1;
            debug_StdPrintfDummy("-------- audio information --------------------\n");
            debug_StdPrintfDummy(
                "[%c%c%c%c]\nheader size:                            %d\ntype(0:PCM big, 1:PCM "
                "little, 2:ADPCM): %d\nsampling rate:                          %dHz\nchannels:     "
                "                          %d\ninterleave size:                        "
                "%d\ninterleave start block address:         %d\ninterleave end block address:     "
                "      %d\n",
                self->id[0], self->id[1], self->id[2], self->id[3], self->headerSize, self->type,
                self->sampleRate, self->channels, self->interleaveSize, self->interleaveStart,
                self->interleaveEnd);
            debug_StdPrintfDummy("[%c%c%c%c]\ndata size:                              %d\n",
                                 self->dataId[0], self->dataId[1], self->dataId[2], self->dataId[3],
                                 self->dataSize);
        }
        n -= k;
    }
    self->writePos = (self->writePos + n) % self->ringSize;
    self->filled += n;
    self->putTotal += n;
}

inline int audioDecIsPreset(AudioDec *self)
{
    return self->sentTotal >= self->iopSize;
}

inline void audioDecStart(AudioDec *self)
{
    SgStPcmLseek(0, 0);
    SgStPcmLseek(1, 0);
    if (self->mono) {
        int half = self->volume / 2;
        SgStPcmVolume(3, half, half);
    } else {
        SgStPcmVolume(1, 0, self->volume);
        SgStPcmVolume(2, self->volume, 0);
    }
    SgStPcmPlay(3);
    self->state = 2;
}

/* one DMA transfer of n bytes from EE memory to IOP memory, waited out */
static inline void sendToIOP(char *src, char *dst, int n) /* derived name */
{
    sceSifDmaData dma;
    int id;

    if (n > 0) {
        dma.src = (unsigned int)src;
        dma.dest = (unsigned int)dst;
        dma.size = n;
        dma.u.attr = 0;
        FlushCache(0);
        id = sceSifSetDma(&dma, 1);
        while (sceSifDmaStat(id) >= 0) {}
    }
}

static int sendToIOP2area(char *dst0, int n0, char *dst1, int n1, char *src0, int m0, char *src1,
                          int m1)
{
    int total = m0 + m1;

    if (n0 + n1 < total) {
        int over = total - (n0 + n1);

        if (over >= m1) {
            m0 -= over - m1;
            m1 = 0;
            total = m0;
        } else {
            m1 -= over;
            total = m0 + m1;
        }
    }

    if (m0 >= n0) {
        sendToIOP(src0, dst0, n0);
        sendToIOP(src0 + n0, dst1, m0 - n0);
        sendToIOP(src1, dst1 + m0 - n0, m1);
    } else if (m1 >= n0 - m0) {
        sendToIOP(src0, dst0, m0);
        sendToIOP(src1, dst0 + m0, n0 - m0);
        sendToIOP(src1 + n0 - m0, dst1, m1 - (n0 - m0));
    } else {
        sendToIOP(src0, dst0, m0);
        sendToIOP(src1, dst0 + m0, m1);
    }
    return total;
}

int audioDecSendToIOP(AudioDec *self)
{
    char *p0;
    int n0;
    char *p1;
    int n1;
    char *q0;
    int m0;
    char *q1;
    int m1;
    int n = 0;
    int len;
    int rsz;
    int off;
    int lim;

    switch (self->state) {
    case 0:
        return 0;
    case 1:
        n0 = self->iopSize - self->sentTotal;
        p1 = 0;
        n1 = 0;
        p0 = (char *)(self->iopBuf + self->sentTotal % self->iopSize);
        break;
    case 2: {
        int rd = SgStPcmIopReadAddr(0);
        int sz = self->iopSize;
        int cur = self->iopPos;
        int want = (rd + sz - cur - 1024) % sz;
        int tail = sz - cur;

        want = want / 1024 * 1024;

        if (tail >= want) {
            n0 = want;
            p1 = 0;
            n1 = 0;
            p0 = (char *)(self->iopBuf + cur);
        } else {
            n0 = tail;
            n1 = want - n0;
            p0 = (char *)(self->iopBuf + cur);
            p1 = (char *)self->iopBuf;
        }
        break;
    }
    case 3:
        return 0;
    }

    len = self->filled;
    rsz = self->ringSize;
    off = (self->writePos - len + rsz) % rsz;
    lim = len / 1024 * 1024;
    m0 = rsz - off;
    q0 = (char *)(self->ring + off);
    if (lim < m0) {
        m0 = lim;
    }
    m1 = lim - m0;
    q1 = (char *)self->ring;

    if (n0 + n1 >= 1024 && m0 + m1 >= 1024) {
        n = sendToIOP2area(p0, n0, p1, n1, q0, m0, q1, m1);
    }

    self->filled = self->filled - n;
    self->sentTotal += n;
    self->iopPos = (self->iopPos + n) % self->iopSize;
    return n;
}

inline int audioDecPause(AudioDec *self)
{
    self->state = 3;
    SgStPcmVolume(3, 0, 0);
    SgStPcmStop(3);
    return 0;
}

inline void audioDecResume(AudioDec *self)
{
    audioDecStart(self);
}

int pcmCallback(sceMpeg *mp, void *cbdata, void *anyData)
{
    MvCbStr *pkt = cbdata;
    MvCbArg *arg = anyData;
    char *p0;
    int n0;
    char *p1;
    int n1;
    ReadBuf *b = arg->rb;
    AudioDec *ad = arg->dec;
    unsigned int rd = (unsigned int)pkt->data;
    int base = (int)b->data;
    int n;
    int first;
    int rest;
    int k;

    rd += 4;
    if (rd >= (unsigned int)(base + b->size)) {
        rd -= b->size;
    }
    n = pkt->len - 4;
    first = (base + b->size) - rd;
    if (n < first) {
        first = n;
    }
    rest = n - first;

    /* the two spans the next put fills: the rest of the header and the
       ring, or the free run of the ring up to its end and the part that
       wraps to its start */
    if (ad->state == 0) {
        p0 = (char *)ad + (ad->headerBytes + 4);
        n0 = 40 - ad->headerBytes;
        p1 = (char *)ad->ring;
        n1 = ad->ringSize;
    } else {
        int room = ad->ringSize - ad->filled;
        int wr = ad->writePos;
        int tail = ad->ringSize - wr;

        if (tail >= room) {
            tail = room;
            p1 = 0;
            n1 = 0;
            p0 = (char *)(ad->ring + wr);
        } else {
            n1 = room - tail;
            p0 = (char *)(ad->ring + wr);
            p1 = (char *)ad->ring;
        }
        n0 = tail;
    }

    k = copy2area(p0, n0, p1, n1, (char *)rd, first, (char *)base, rest);
    audioDecEndPut(ad, k);
    return 0 < k;
}

static void Free(int addr)
{
    /* the buffers are kept as addresses (alloc_zeroed), some at their
       uncached-accelerated alias; the heap takes the block back with the
       segment bits off */
    iosFree((void *)phys_addr(addr));
}
