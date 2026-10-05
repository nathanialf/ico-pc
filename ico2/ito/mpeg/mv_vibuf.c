#include "mv_defs.h"
#include "memory.h"
#include <eekernel.h>
#include "mv_sub.h"
#include "typedef.h"
#include "mv_vibuf.h"
#include <eeregs.h>

static void Free(int addr);

/* Write the IPU output channel's CHCR (0x1000B000) as setIpuInChcr below
   writes the input channel's. */
static __inline__ void setIpuOutChcr(int chcr) /* derived name */
{
    DIntr();
    *D_ENABLEW = *D_ENABLER | 0x10000;
    *D3_CHCR = chcr;
    *D_ENABLEW = *D_ENABLER & 0xFFFEFFFF;
    SYNC();
    EI();
}

/* Write the IPU input channel's CHCR (0x1000B400) with the DMA controller
   held (D_ENABLER/D_ENABLEW bit 16) and interrupts off, as libmpeg's setD4_CHCR
   does, closing with SYNC and EI. */
static __inline__ void setIpuInChcr(int chcr) /* derived name */
{
    DIntr();
    *D_ENABLEW = *D_ENABLER | 0x10000;
    *D4_CHCR = chcr;
    *D_ENABLEW = *D_ENABLER & 0xFFFEFFFF;
    SYNC();
    EI();
}

/* one quadword of the DMA tag list, as two doublewords or four words */
typedef union { /* field names derived */
    unsigned long long ul[2];
    int w[4];
} QWord; /* derived name */

/* One 16-byte DMA source-chain tag: the data address in the upper word, the
   tag id and quadword count below. */
static __inline__ void setDmaTag(char *tag, int i, int addr, int qwc, int id) /* derived name */
{
    ((QWord *)tag)[i].ul[0] =
        ((unsigned long long)addr << 32) | ((unsigned long long)id << 28) | qwc;
}

/* The ring sector a DMA address points into, or 0 once the chain has run
   onto the tag after the last one. */
static __inline__ int getDmaSector(ViBuf *self, unsigned int madr) /* derived name */
{
    if (madr == phys_addr((int)((QWord *)self->dmaTag + self->nSector + 1))) {
        return 0;
    }
    return (madr - (unsigned int)self->data) / 2048;
}

/* this file's own free_buf; mv_videodec.c and mv_vobuf.c have theirs */
static void free_buf(ViBuf *self)
{
    Free((int)self->data);
    Free((int)self->dmaTag);
    Free((int)self->ts);
}

int viBufCreate(ViBuf *self)
{
    struct SemaParam sem;
    int data;
    int tag;
    int ts;
    /* the ring geometry: 256 sectors of 2048 bytes, and a 512-entry
       timestamp ring */
    int nSector = 256;
    int tsMax = 512;

    self->created = 0;

    data = alloc_zeroed(524288, 64);
    if (data == 0) {
        return -1;
    }
    tag = alloc_zeroed(4112, 64);
    if (tag == 0) {
        return -1;
    }
    ts = alloc_zeroed(12288, 4);
    if (ts == 0) {
        return -1;
    }

    self->data = (char *)data;
    self->dmaTag = (char *)uncached_accel_addr(tag);
    self->nSector = nSector;
    self->size = nSector << 11;

    self->ts = (ViTs *)ts;
    self->tsMax = tsMax;

    sem.initCount = 1;
    sem.maxCount = 1;

    self->sema = CreateSema(&sem);

    self->created = 1;

    viBufReset(self);

    self->total = 0;

    return 0;
}

int viBufReset(ViBuf *self)
{
    int i;

    self->rdSector = 0;
    self->nReady = 0;
    self->wOffset = 0;
    self->running = 1;

    self->tsCount = 0;
    self->tsWr = 0;
    for (i = 0; i < self->tsMax; i++) {
        self->ts[i].pts = -1;
        self->ts[i].dts = -1;
        self->ts[i].pos = 0;
        self->ts[i].len = 0;
    }

    for (i = 0; i < self->nSector; i++) {
        setDmaTag(self->dmaTag, i, phys_addr((int)(self->data + i * 2048)), 128, 3);
    }
    setDmaTag(self->dmaTag, i, phys_addr((int)self->dmaTag), 0, 2);

    *D4_QWC = 0;
    *D4_MADR = phys_addr((int)self->data);
    *D4_TADR = phys_addr((int)self->dmaTag);
    setIpuInChcr(5);

    return 1;
}

/* Hand out the region the caller may write next, as up to two runs: the one
   that ends at the top of the ring and, if it wraps, the one that starts at
   the bottom.  Two sectors are held back so the writer never overruns the
   reader. */
void viBufBeginPut(ViBuf *self, void **addr1, int *size1, void **addr2, int *size2)
{
    int keep;
    int pos;
    int room;
    int len;

    WaitSema(self->sema);

    keep = self->nReady + 2;
    pos = (self->rdSector + self->nReady) << 11;
    room = (self->nSector - keep) << 11;

    pos = (pos + self->wOffset) % self->size;
    len = room - self->wOffset;

    if (self->size - pos >= len) {
        *addr1 = self->data + pos;
        *size1 = len;

        *size2 = 0;
        *addr2 = 0;
    } else {
        *addr1 = self->data + pos;
        *size1 = self->size - pos;
        *addr2 = self->data;
        *size2 = len - (self->size - pos);
    }
    SignalSema(self->sema);
}

void viBufEndPut(ViBuf *self, int n)
{
    WaitSema(self->sema);
    self->wOffset += n;
    self->total += n;
    SignalSema(self->sema);
}

/* Retire the sectors the IPU DMA has consumed and chain the whole sectors
   written since the last call onto the tag list, restarting the channel when
   it had run dry. */
int viBufAddDMA(ViBuf *self)
{
    int chcr;
    int d;
    int w;
    int n;
    int prev;
    int i;
    int sector;
    int id;
    int restart = 0;

    WaitSema(self->sema);

    if (self->running == 0) {
        ErrMessage("DMA ADD not active\n");
        return 0;
    }

    setIpuInChcr(5);
    chcr = *D4_CHCR;

    sector = getDmaSector(self, *D4_MADR);
    d = (sector + self->nSector - self->rdSector) % self->nSector;
    self->rdSector = (self->rdSector + d) % self->nSector;
    self->nReady -= d;

    w = (self->rdSector + self->nReady) % self->nSector;
    n = self->wOffset / 2048;
    self->wOffset -= n * 2048;

    if (n > 0) {
        prev = (self->rdSector + self->nReady - 1 + self->nSector) % self->nSector;
        setDmaTag(self->dmaTag, prev, (int)(self->data + prev * 2048), 128, 3);
        restart = 1;
    }

    for (sector = w, i = 0; i < n; i++) {
        id = (i == n - 1) ? 0 : 3;
        setDmaTag(self->dmaTag, sector, (int)(self->data + sector * 2048), 128, id);
        sector = (sector + 1) % self->nSector;
    }

    self->nReady += n;

    if (self->nReady != 0) {
        if (restart) {
            chcr = (chcr & 0x0FFFFFFF) | 0x30000000;
        }
        setIpuInChcr(chcr | 0x100);
    }

    SignalSema(self->sema);

    return 1;
}

/* Stop both IPU DMA channels and keep their registers and the IPU's bit
   position for viBufRestartDMA, as libipu's sceIpuStopDMA does, in the ring's
   save area. */
int viBufStopDMA(ViBuf *self)
{
    WaitSema(self->sema);

    self->running = 0;
    setIpuInChcr(5);

    self->inMadr = *D4_MADR;
    self->inTadr = *D4_TADR;
    self->inQwc = *D4_QWC;
    self->inChcr = *D4_CHCR;

    while (*IPU_CTRL & 0xF0) {}

    setIpuOutChcr(0);

    self->outMadr = *D3_MADR;
    self->outQwc = *D3_QWC;
    self->outChcr = *D3_CHCR;
    self->bitPos = *IPU_BP;
    self->ipuCtrl = *IPU_CTRL;

    SignalSema(self->sema);

    return 1;
}

/* Restart the IPU input DMA saved by viBufStopDMA, rewound by the bytes still
   sitting in the IPU FIFO (the fifo and ifc fields of IPU_BP), re-chaining
   from the tag of the sector the rewound address falls in: libipu's
   sceIpuRestartDMA over the ring. */
int viBufRestartDMA(ViBuf *self)
{
    int cmd;
    int fifo;
    int ifc;
    unsigned int madr;
    int tadr;
    unsigned int qwc;
    int chcr;
    int id;
    int now;
    int sector;

    cmd = self->bitPos & 0x7F;
    fifo = (self->bitPos >> 16) & 3;
    ifc = (self->bitPos >> 8) & 0xF;
    madr = self->inMadr - ((fifo + ifc) << 4);
    qwc = self->inQwc + (fifo + ifc);
    tadr = self->inTadr;
    chcr = self->inChcr | 0x100;

    WaitSema(self->sema);

    if (madr < (unsigned int)self->data) {
        qwc = ((unsigned int)self->data - madr) / 16;
        madr += self->nSector << 11;
        tadr = phys_addr((int)self->dmaTag);
        id = (self->inMadr == (int)self->data ||
              self->inMadr == (int)(self->data + (self->nSector << 11)))
                 ? 0
                 : 3;
        chcr = (self->inChcr & 0x0FFFFFFF) | (id << 28) | 0x100;
        if ((self->nSector - self->rdSector) % self->nSector < 0 ||
            (self->nSector - self->rdSector) % self->nSector >= self->nReady) {
            self->rdSector = self->nSector - 1;
            self->nReady++;
        }
    } else {
        if ((now = getDmaSector(self, self->inMadr)) != (sector = getDmaSector(self, madr))) {
            tadr = phys_addr((int)((QWord *)self->dmaTag + now));
            qwc = ((unsigned int)self->data + (now << 11) - madr) / 16;
            id =
                ((unsigned int)self->data +
                     (self->inMadr - (unsigned int)self->data) % (self->nSector << 11) ==
                 (unsigned int)self->data + ((self->rdSector + self->nReady) % self->nSector << 11))
                    ? 0
                    : 3;
            chcr = (self->inChcr & 0x0FFFFFFF) | (id << 28) | 0x100;
            if ((sector + self->nSector - self->rdSector) % self->nSector < 0 ||
                (sector + self->nSector - self->rdSector) % self->nSector >= self->nReady) {
                self->rdSector = sector;
                self->nReady++;
            }
        }
    }

    if (self->outMadr != 0 && self->outQwc != 0) {
        *D3_MADR = self->outMadr;
        *D3_QWC = self->outQwc;
        setIpuOutChcr(self->outChcr | 0x100);
    }

    if (self->nReady != 0) {
        while (*IPU_CTRL < 0) {}
        *IPU_CMD = cmd;
        while (*IPU_CTRL < 0) {}
    }

    *D4_MADR = madr;
    *D4_TADR = tadr;
    *D4_QWC = qwc;
    if (self->nReady != 0) {
        setIpuInChcr(chcr);
    }

    *IPU_CTRL = self->ipuCtrl;

    self->running = 1;

    SignalSema(self->sema);

    return 1;
}

void viBufFlush(ViBuf *self)
{
    WaitSema(self->sema);
    self->wOffset = (self->wOffset + 2047) / 2048 * 2048;
    SignalSema(self->sema);
}

/* Does the entry's byte position still lie inside the run of ts->len bytes
   the reader just consumed at ts->pos, measured around a size-byte ring? */
static __inline__ int tsRunCovers(int pos, ViTs *t, int size) /* derived name */
{
    return (pos + size - t->pos) % size < t->len;
}

/* Walk the live timestamps oldest first and charge the run described by `ts`
   against them, retiring any entry the run swallows whole. */
static int viBufModifyPts(ViBuf *self, ViTs *ts)
{
    ViTs *e;
    int idx;
    int size;
    int ok;
    int n;
    int m;

    idx = (self->tsWr - self->tsCount + self->tsMax) % self->tsMax;
    size = self->nSector << 11;
    ok = 1;

    if (self->tsCount > 0) {
        for (;;) {
            e = &self->ts[idx];

            if (e->len == 0 || ts->len == 0)
                break;

            if (tsRunCovers(e->pos, ts, size)) {
                n = ts->pos + ts->len - e->pos;
                n = e->len < n ? e->len : n;

                e->pos = (e->pos + n) % size;
                e->len -= n;

                if (e->len == 0) {
                    if (e->pts >= 0) {
                        e->pts = -1;
                        e->dts = -1;
                        e->pos = 0;
                        e->len = 0;
                    }
                    m = self->tsCount - 1;
                    if (m < 0)
                        m = 0;
                    self->tsCount = m;
                }
            } else {
                ok = 0;
            }

            idx = (idx + 1) % self->tsMax;
            if (!ok)
                break;
        }
    }
    return 0;
}

/* Hand back the PTS/DTS pair covering the byte the IPU is reading right now,
   and retire it.  The read position is the DMA address the IPU_TO channel has
   reached, less what is still sitting in the IPU's input FIFO. */
int viBufGetTs(ViBuf *self, ViTs *out)
{
    unsigned int madr;
    unsigned int bp;
    int bitPos;
    int fp;
    int ifc;
    int size;
    unsigned int pos;
    int n;
    int i;
    int j;
    int wr;
    int found;
    int d;
    ViTs *e;

    madr = *D4_MADR;
    bp = *IPU_BP;
    bitPos = self->bitPos & 0x7F;
    fp = (bp >> 16) & 3;
    ifc = (bp >> 8) & 0xF;
    madr -= (fp + ifc) << 4;

    size = self->nSector << 11;

    found = 0;

    WaitSema(self->sema);

    out->pts = -1;
    out->dts = -1;

    pos = (madr + (bitPos >> 3) + size - (unsigned int)self->data) % size;

    n = self->tsCount;
    wr = self->tsWr;

    for (i = 0; i < n && !found; i++) {
        j = (wr - n + self->tsMax + i) % self->tsMax;

        e = &self->ts[j];

        if (tsRunCovers(pos, e, size)) {
            out->pts = e->pts;
            out->dts = e->dts;
            e->pts = -1;
            e->dts = -1;

            found = 1;
            d = self->tsCount;
            if (d >= 2)
                d = 1;
            self->tsCount -= d;
        }
    }

    SignalSema(self->sema);

    return 1;
}

/* this file's own Free (mv_defs.h) */
static void Free(int addr)
{
    /* the buffers are kept as addresses (alloc_zeroed), some at their
       uncached-accelerated alias; the heap takes the block back with the
       segment bits off */
    iosFree((void *)phys_addr(addr));
}

/* Stop the IPU input DMA, clear its registers and release the ring. */
int viBufDelete(ViBuf *self)
{
    setIpuInChcr(5);

    *D4_QWC = 0;
    *D4_MADR = 0;
    *D4_TADR = 0;

    if (self->created) {
        DeleteSema(self->sema);
    }
    self->created = 0;

    free_buf(self);

    return 1;
}

int viBufCount(ViBuf *self)
{
    int ret;
    WaitSema(self->sema);
    ret = (self->nReady << 11) + self->wOffset;
    SignalSema(self->sema);
    return ret;
}

/* Record one PTS/DTS pair against the bytes the caller just wrote.  Returns 0
   only when the timestamp ring is full. */
int viBufPutTs(ViBuf *self, ViTs *ts)
{
    int ret = 0;

    WaitSema(self->sema);

    if (self->tsCount < self->tsMax) {
        viBufModifyPts(self, ts);

        if (ts->pts >= 0 || ts->dts >= 0) {
            self->ts[self->tsWr].pts = ts->pts;
            self->ts[self->tsWr].dts = ts->dts;
            self->ts[self->tsWr].pos = ts->pos;
            self->ts[self->tsWr].len = ts->len;

            self->tsCount++;
            self->tsWr = (self->tsWr + 1) % self->tsMax;
        }
        ret = 1;
    }
    SignalSema(self->sema);

    return ret;
}
