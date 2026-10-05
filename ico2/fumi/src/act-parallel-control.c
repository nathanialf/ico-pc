#include "act-parallel-control.h"
#include "debug.h"
#include "debug_exception.h"
#include <assert.h>

/* the parallel-action ids copied out of the layout table */
static int parallelIds[86]; /* derived name */

#ifdef ICO_HOST

static int resolve(int v, int n) /* derived name */
{
    if (v > 0xFFFF) {
        int idx = v - 0x10000;
        int m = idx;
        int k = 0;

        if (randomMotionKind[m] != 0x47B) {
            do {
                m++;
                k++;
            } while (randomMotionKind[m] != 0x47B);
        }
        if (k == 0) {
            v = 0x47B;
        } else {
            int e = idx + n % k;

            v = randomMotionKind[e];
        }
    }
    return v;
}

#endif

/* Fill tbl from the parallel-motion table: for each bit set in mask, each
   row's motion for that bit, resolved by the nested helper (an id above
   0xFFFF picks one entry of a random-motion run, by n), replaces the row's
   entry unless it is 0x47B. */
void ActPara_MakeTbl(int *tbl, unsigned long long mask, int n)
{
    int i;
    int j;
    int val;

#ifndef ICO_HOST
    inline int resolve(int v) /* derived name */
    {
        if (v > 0xFFFF) {
            int idx = v - 0x10000;
            int m = idx;
            int k = 0;

            if (randomMotionKind[m] != 0x47B) {
                do {
                    m++;
                    k++;
                } while (randomMotionKind[m] != 0x47B);
            }
            if (k == 0) {
                v = 0x47B;
            } else {
                int e = idx + n % k;

                v = randomMotionKind[e];
            }
        }
        return v;
    }

#endif
    for (i = 0; i < 44; i++) {
        if (((mask >> i) & 1) == 1) {
            for (j = 0; j < 86; j++) {
                val = parallelMotionTbl[j].motion[i];
#ifdef ICO_HOST
                val = resolve(val, n);
#else
                val = resolve(val);
#endif
                if (val != 0x47B) {
                    tbl[j] = val;
                }
            }
        }
    }
}

void ActPara_InitSystem(void)
{
    int i;
    for (i = 0; i <= 85; i++) {
        parallelIds[i] = parallelMotionTbl[i].motion[0];
    }
    /* a compiled-out overflow check: its message and __FILE__ stay in .rodata
       and its "0" in .sdata */
    if (0) {
        /* too many parallel motions (way too many) */
        debug_StdPrintfDummy("並列モーションが増えすぎました（大森）");
        debug_assert(__FILE__, 103);
        __assert(__FILE__, 103, "0");
    }
}

int *ActPara_GetDefTbl(void)
{
    return parallelIds;
}

int ActPara_StatusToFlag(int a0, int a1)
{
    int v = a0 ? 9 : 1;
    return a1 ? (v | 4) : v;
}

void ActPara_DebugOut(void) {}
