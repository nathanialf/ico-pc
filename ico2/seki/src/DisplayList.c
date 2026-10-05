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

#define DL_DEBUG 0 /* derived name */

/* One priority's display list: whether a DMA tag is open, where the open tag
   is, the address and quadword count it refers to and its tag id, the start
   of the list's buffer and the write pointer. */
typedef struct {           /* field names derived */
    int open;              /* 0x00 */
    int tag;               /* 0x04 */
    long long addr;        /* 0x08 */
    unsigned int qwc;      /* 0x10 */
    int pad14;             /* 0x14 */
    unsigned long long id; /* 0x18 */
    int start;             /* 0x20 */
    int cur;               /* 0x24 */
} DlEntry;                 /* derived name */

/* The bank the list is built into and the priority it is building at, then the 13
   list entries, the two banks of 13 buffer heads they are reloaded from, and
   the eight-deep priority stack. */
static int dlBank; /* derived name */

static int dlPriority; /* derived name */

static DlEntry dlEntries[13]; /* derived name */

static int dlBufferHead[2][13]; /* derived name */

/* The depth of the priority stack below. */
static int dlStackDepth = 0; /* derived name */

static int dlPriorityStack[8]; /* derived name */

/* The size of each priority's list buffer. */
static const int dlBufferSize[13] = {
    /* derived name */
    81920, 14336, 30720, 4096, 16384, 65536, 40960, 12288, 26624, 14336, 4096, 28672, 86016,
};

void dl_Init(void)
{
    int i;
    int j;
    dlPriority = 0;
    dlStackDepth = 0;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 13; j++) {
            dlBufferHead[i][j] = ICO_UNCACHED_ACCEL(
                ICO_ADDR(iosMallocDebug(ios_partition_common, dlBufferSize[j], __FILE__, 393)));
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
        int *p = dlBufferHead[i];
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
    int *src = dlBufferHead[flag];
    DlEntry *dst = dlEntries;
    int i;
    dlBank = flag;
    dlPriority = 0;
    for (i = 12; i >= 0; i--) {
        int v = *src;
        dst->open = 0;
        src++;
        dst->cur = v;
        dst->start = v;
        dst++;
    }
    dpk_SwapBuffer();
    gif_Init();
    mc_Reset();
}

void dl_Swap(void)
{
    int i;
    int j;
    int stride = 0x28;
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
    if (fbKeep) {
        sceDmaSend(dmaVif, ICO_PHYS(dlEntries[11].start));
    } else {
        sceDmaSend(dmaVif, ICO_PHYS(dlEntries[0].start));
    }
    dl_Clear();
}

inline void dl_SetDLPriority(int pri)
{
    if (pri < 0) {
        dlPriority = 0;
    } else if (pri >= 0xD) {
        dlPriority = 0xC;
    } else {
        dlPriority = pri;
    }
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
        dlPriority = dlPriorityStack[dlStackDepth - 1];
        dlStackDepth--;
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
    unsigned int end = entry->cur;
    unsigned int start = entry->tag;
    unsigned int count = (end - start) >> 4;
    debug_StdPrintfDummy("dldma %d\n", count - 1);
}

inline void dl_OpenDma(int id, void *addr, int qwc)
{
    DlEntry *entry = &dlEntries[dlPriority];
    int old;

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
    old = entry->cur;
    entry->id = id;
    entry->qwc = qwc;
    entry->open = 1;
    entry->addr = ICO_PHYS(ICO_ADDR(addr));
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
