#include "debug.h"
#include "Matrix.h"
#include <assert.h>
/* string.h's memcpy: through the unprototyped declaration below the int
   size would be passed where the definition reads a size_t */
#include <string.h>

/* The allocator's partition (none selected yet) and the running total of
   what partition 0 has handed out. */
static int mallocPartition = -1; /* derived name */

static int mallocTotal = 0; /* derived name */

/* memory.h is not included: its iosFree and iosReallocDebug disagree with
   the calls below (see them); iosMallocDebug as memory.h declares it */
struct IosMemPart;

extern void *iosMallocDebug(struct IosMemPart *part, int size, const char *file, int line);
/* as in memory.h */
extern void *iosFree(void *ptr);
extern void *iosReallocDebug(void *ptr, unsigned int size);

#include "Basic.h"
#include "ios.h"
#include "main.h"
#include "debug_exception.h"
#include <libdma.h>

void dma_init(void)
{
    union U { /* field names derived */
        int i;
    } *p;

    sceDmaReset(1);
    dmaVif = sceDmaGetChan(1);
    p = (union U *)dmaVif;
    p->i |= 0x40;
    dmaGif = sceDmaGetChan(2);
    p = (union U *)dmaGif;
    p->i |= 0x40;
    dmaFSp = sceDmaGetChan(8);
    p = (union U *)dmaFSp;
    p->i |= 0x40;
    debug_SetDmaCallback();
}

void matrix_init(void)
{
    matrixptr = (char *)ICO_SPR_ADDR(0);
    _UnitMatrix(matrixptr);
}

inline void malloc_SetPartition(int val)
{
    mallocPartition = val;
}

inline int malloc_GetPartition(void)
{
    return mallocPartition;
}

inline void resetmallocseki(void) {}

inline void *mallocseki(int size)
{
    void *ptr = 0;

    if (mallocPartition == -1) {
        debug_StdPrintfDummy("set partition first!\n");
        debug_assert("src/Basic.c", 372);
        __assert("src/Basic.c", 372, "0");
    }

    switch (mallocPartition) {
    case 0:
        mallocTotal += size + 0x30;
        ptr = iosMallocDebug(ios_partition_common, size, "src/Basic.c", 379);
        break;
    case 1:
        ptr = iosMallocDebug(ios_partition_seki, size, "src/Basic.c", 382);
        break;
    }
    return ptr;
}

inline void *mallocsekistage(int size)
{
    int save = mallocPartition;
    void *r;

    mallocPartition = 1;
    r = mallocseki(size);
    mallocPartition = save;
    return r;
}

/* iosReallocDebug is (void *ptr, unsigned int size) in memory.h; the EE
   call below passes (ptr, size) in the two parameters it names size and
   align, and the other two arguments go unread */
void *reallocseki(void *ptr, int size)
{
    return iosReallocDebug(ptr, size);
}

inline int freeseki(void *ptr)
{
    if (ptr != 0) {
        iosFree(ptr);
    }
    return 0;
}

void malloc_MemCpy(void *dst, void *src, int size)
{
    memcpy(dst, src, size);
}

/* The DMA channel handles and the screen fade state. */
DmaChan *dmaVif = 0;

DmaChan *dmaGif = 0;

DmaChan *dmaFSp = 0;

int fadeStatus = 0;

float fadeSpeed = 0.0f;

int fadeContinue = 0;

unsigned char fadeColor[4] = {0};

/* the EE scratchpad (0x70000000, 16 KB) as plain memory; see ICO_SPR_ADDR */
char ico_scratchpad[16 * 1024] __attribute__((aligned(16)));
