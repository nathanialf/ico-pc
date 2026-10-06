#include "typedef.h"
#include "debug.h"
#include "memory.h"
#include "DmaPacket.h"
#include "GifPacket.h"
#include "MicroCode.h"
#include "DisplayList.h"
#include <eekernel.h>
#include <libdma.h>
#include "GsBase.h"
#include "ios.h"
#include "debug_exception.h"
#include "Basic.h"
#include <assert.h>
#include <stdio.h>

#ifdef ICO_RD

#include "GifHost.h"

#endif
#define DL_DEBUG 0 /* derived name */

/* One priority's display list: whether a DMA tag is open, where the open tag
   is, the address and quadword count it refers to and its tag id, the start
   of the list's buffer and the write pointer. */
/* ICO_WORD (typedef.h): the buffer addresses are ints on the EE and
   pointer-wide on the host; the offsets are the EE's. */
typedef struct {           /* field names derived */
    int open;              /* 0x00 */
    ICO_WORD tag;          /* 0x04 */
    long long addr;        /* 0x08 */
    unsigned int qwc;      /* 0x10 */
    int pad14;             /* 0x14 */
    unsigned long long id; /* 0x18 */
    ICO_WORD start;        /* 0x20 */
    ICO_WORD cur;          /* 0x24 */
} DlEntry;                 /* derived name */

/* The bank the list is built into and the priority it is building at, then the 13
   list entries, the two banks of 13 buffer heads they are reloaded from, and
   the eight-deep priority stack. */
static int dlBank; /* derived name */

static int dlPriority; /* derived name */

static DlEntry dlEntries[13]; /* derived name */

static ICO_WORD dlBufferHead[2][13]; /* derived name */

/* The depth of the priority stack below. */
static int dlStackDepth = 0; /* derived name */

static int dlPriorityStack[8]; /* derived name */

/* The size of each priority's list buffer. */
static const int dlBufferSize[13] = {
    /* derived name */
    81920, 14336, 30720, 4096, 16384, 65536, 40960, 12288, 26624, 14336, 4096, 28672, 86016,
};

/* PC port (DIVERGENCES.md D16): each list buffer gets DL_HOST_PAD bytes past
   the EE's size. Nothing checks the fill on the EE (dl_CheckDLOverflow is
   compiled out, DL_DEBUG): the test stage 88 (STGBOSS_TEST) fills list 10
   (4096 bytes; the shadows' eyes, DispEnemyEye) and its next tags land on
   what follows the buffer, the next block's header first. The host keeps
   them in the pad, logs each new 1 KB of fill past the EE's size, and drops
   a tag (dlHostSink) only when the pad is full too, keeping the last
   quadword for dl_Swap's chain tag (ids 1 and 7). */
#define DL_HOST_PAD 0x2000

static unsigned int dlHostPeak[13];

static unsigned long long dlHostSink[2] __attribute__((aligned(16)));

/* 1 when the tag about to open must be dropped */
static int dlHostFull(const DlEntry *entry, int id)
{
    unsigned int used = (unsigned int)(entry->cur - entry->start) + 0x10;
    unsigned int ee = (unsigned int)dlBufferSize[dlPriority];

    if (used <= ee) {
        return 0;
    }
    if (used > dlHostPeak[dlPriority]) {
        unsigned int peak = dlHostPeak[dlPriority];

        if (peak <= ee || ((used - ee - 1) >> 10) != ((peak - ee - 1) >> 10)) {
            fprintf(stderr,
                    "dl: list %d at %u bytes, past the EE's %u (DIVERGENCES D16; "
                    "logged per 1 KB)\n",
                    dlPriority, used, ee);
        }
        dlHostPeak[dlPriority] = used;
    }
    return id != 1 && id != 7 && used > ee + DL_HOST_PAD - 0x10;
}

void dl_Init(void)
{
    int i;
    int j;
    dlPriority = 0;
    dlStackDepth = 0;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 13; j++) {
            dlBufferHead[i][j] = (ICO_WORD)ICO_UNCACHED_ACCEL(ICO_ADDR(iosMallocDebug(
                ios_partition_common, dlBufferSize[j] + DL_HOST_PAD, __FILE__, 393)));
        }
    }
    dlBank = 0;
    for (i = 0; i < 13; i++) {
        dlEntries[i].start = dlEntries[i].cur = dlBufferHead[0][i];
        dlEntries[i].open = 0;
    }
    dpk_Init();
    dl_Clear();
}

inline void dl_Out(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        ICO_WORD *p = dlBufferHead[i];
        int j;
        for (j = 12; j >= 0; j--) {
            iosFree((void *)*p);
            p++;
        }
    }
}

void dl_Clear(void)
{
    int flag = dlBank ^ 1;
    ICO_WORD *src = dlBufferHead[flag];
    DlEntry *dst = dlEntries;
    int i;
    dlBank = flag;
    dlPriority = 0;
    for (i = 12; i >= 0; i--) {
        ICO_WORD v = *src;
        dst->open = 0;
        src++;
        dst->cur = v;
        dst->start = v;
        dst++;
    }
    dpk_SwapBuffer();
    gif_Init();
    mc_Reset();
#ifdef ICO_RD
    /* R2a: the frame boundary on rd.  A frame still open was never kicked
       (dl_Clear without dl_Swap): it is dropped. */
    rd_DiscardFrame();
    rd_BeginFrame();
#endif
}

void dl_Swap(void)
{
    int i;
    int j;
    int stride = sizeof(DlEntry);

#ifdef ICO_RD
    /* R2a: the kick.  rd replays lists 0..12 (11..12 with fbKeep, which
       starts the DMA at list 11) and presents; nothing is DMA'd. */
    gif_HostFlush();
    rd_EndFrame(fbKeep);
#endif
    dl_SetDLPriority(0xC);
    dl_OpenDma(7, 0, 0);
    dl_CloseDma();
    i = 0;
    do {
        DlEntry *e;
        dl_SetDLPriority(i);
        j = i + 1;
        e = (DlEntry *)((char *)dlEntries + j * stride);
        dl_OpenDma(1, (void *)(ICO_PHYS(e->start)), 0);
        dl_CloseDma();
        i = j;
    } while (j < 0xC);
    FlushCache(0);
#ifndef ICO_RD
    if (fbKeep) {
        sceDmaSend(dmaVif, (void *)ICO_PHYS(dlEntries[11].start));
    } else {
        sceDmaSend(dmaVif, (void *)ICO_PHYS(dlEntries[0].start));
    }
#endif
    dl_Clear();
}

inline void dl_SetDLPriority(int pri)
{
#ifdef ICO_RD
    /* R2a: what the decoder holds belongs to the list being left */
    gif_HostFlush();
#endif
    if (pri < 0) {
        dlPriority = 0;
    } else if (pri >= 0xD) {
        dlPriority = 0xC;
    } else {
        dlPriority = pri;
    }
#ifdef ICO_RD
    rd_SelectList(dlPriority);
#endif
}

void dl_PushPriority(void)
{
    if (dlStackDepth < 7) {
        dlStackDepth = dlStackDepth + 1;
        dlPriorityStack[dlStackDepth - 1] = dlPriority;
    } else {
        debug_StdPrintfDummy("dl_PushPriority:Stack Overflow.\n");
        debug_assert(__FILE__, 534);
        __assert(__FILE__, 534, "FALSE");
    }
}

void dl_PopPriority(void)
{
    if (dlStackDepth > 0) {
#ifdef ICO_RD
        gif_HostFlush();
#endif
        dlPriority = dlPriorityStack[dlStackDepth - 1];
        dlStackDepth--;
#ifdef ICO_RD
        rd_SelectList(dlPriority);
#endif
    } else {
        debug_StdPrintfDummy("dl_PopPriority:Stack Underflow.\n");
        debug_assert(__FILE__, 552);
        __assert(__FILE__, 552, "FALSE");
    }
}

inline int dl_GetPri(void)
{
    return dlPriority;
}

void dl_Debug(void)
{
    DlEntry *entry = &dlEntries[dlPriority];
    unsigned int end = (unsigned int)entry->cur;
    unsigned int start = (unsigned int)entry->tag;
    unsigned int count = (end - start) >> 4;
    debug_StdPrintfDummy("dldma %d\n", count - 1);
}

inline void dl_OpenDma(int id, const void *addr, int qwc)
{
    DlEntry *entry = &dlEntries[dlPriority];
    ICO_WORD old;

    /* the buffer overflow check, compiled out (DL_DEBUG is 0); the
       message-assert form this programmer writes in Packet.c and
       BgAnimation.c */
    if (DL_DEBUG) {
        debug_StdPrintfDummy("dl_CheckDLOverflow:Display List Buffer [%d] Full.\n", dlPriority);
        __assert(__FILE__, 613, "e");
    }
    if (entry->open) {
        dl_CloseDma();
    }
    if (dlHostFull(entry, id)) {
        entry->id = id;
        entry->qwc = qwc;
        entry->open = 1;
        entry->addr = (long long)ICO_PHYS((ICO_WORD)ICO_ADDR(addr));
        entry->tag = (ICO_WORD)dlHostSink;
        return;
    }
    old = entry->cur;
    entry->id = id;
    entry->qwc = qwc;
    entry->open = 1;
    entry->addr = (long long)ICO_PHYS((ICO_WORD)ICO_ADDR(addr));
    entry->tag = old;
    entry->cur = old + 0x10;
}

void dl_CloseDma(void)
{
    DlEntry *e = &dlEntries[dlPriority];
    long long addr = (e->addr & 0x7FFFFFFF) << 32;
    long long qwc;
    long long *p;

    switch (e->id) {
    case 0:
    case 6:
        qwc = ((unsigned int)(e->cur - e->tag) >> 4) - 1;
        if (qwc == 0) {
            e->cur = e->cur - 0x10;
            e->open = 0;
            return;
        }
        break;
    case 7:
        qwc = ((unsigned int)(e->cur - e->tag) >> 4) - 1;
        break;
    default:
        qwc = e->qwc;
        break;
    }
    p = (long long *)e->tag;
    switch (e->id) {
    case 0:
        p[0] = qwc | 0x10000000;
        break;
    case 1:
        p[0] = qwc | 0x20000000 | addr;
        break;
    case 2:
        p[0] = qwc | 0x30000000 | addr;
        break;
    case 3:
        p[0] = qwc | 0x40000000 | addr;
        break;
    case 4:
        p[0] = qwc | addr;
        break;
    case 5:
        p[0] = qwc | 0x50000000 | addr;
        break;
    case 6:
        p[0] = qwc | 0x60000000;
        break;
    case 7:
        p[0] = qwc | 0x70000000;
        break;
    }
    p[1] = 0;
    e->open = 0;
}
