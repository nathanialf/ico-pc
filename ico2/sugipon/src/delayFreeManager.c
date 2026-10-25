#include "debug.h"
#include "memory.h"
#include "debug_exception.h"
#include "region.h"
#include <assert.h>

/* three frames of pointers queued for a delayed iosFree */
static void
    *delayFreeBuffer[3][384]; /* derived name; the 2001 builds use 256 and 128 of each frame */

/* the frame being filled and its entry count */
static int delayFreeNo = 0; /* derived name */

static int delayFreeCount = 0; /* derived name */

#include "delayFreeManager.h"

static inline void ClearDelayFreeBuffer(int no) /* derived name */
{
    int i;

    for (i = PAL_NTSC_AUG6(384, 256, 128) - 1; i >= 0; i--) {
        delayFreeBuffer[no][i] = 0;
    }
}

static inline void FreeDelayFreeBuffer(int no) /* derived name */
{
    int i;

    for (i = 0; delayFreeBuffer[no][i] != 0; i++) {
        iosFree(delayFreeBuffer[no][i]);
    }
}

inline void InitDelayFree(void)
{
    ClearDelayFreeBuffer(0);
    ClearDelayFreeBuffer(1);
    ClearDelayFreeBuffer(2);
    delayFreeCount = 0;
    delayFreeNo = 0;
}

inline void ExecDelayFree(void)
{
    int no = (delayFreeNo + 2) % 3;

    FreeDelayFreeBuffer(no);
    ClearDelayFreeBuffer(no);
    delayFreeCount = 0;
    delayFreeNo = no;
}

void EntryDelayFree(void *p)
{
    delayFreeBuffer[delayFreeNo][delayFreeCount++] = p;
    if (delayFreeCount >= PAL_NTSC_AUG6(384, 256, 128)) {
        debug_StdPrintfDummy("[33mERROR!!! TOO MANY DELAY FREE LIST ENTRY!!! EXIT...[m\n");
        debug_assert("src/delayFreeManager.c", 51);
        __assert("src/delayFreeManager.c", 51, "0");
    }
}
