/*
 * port/math/newlib/qsort.c
 *
 * ico_qsort: a host copy of newlib's qsort as the game linked it
 * (sce/libc/stdlib/qsort.c, libc.a member qsort.o), which is the 4.4BSD
 * qsort (Bentley and McIlroy, "Engineering a Sort Function") of newlib's
 * libc/search/qsort.c. Its notice:
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The sequence of comparisons and of the element pairs exchanged is the
 * EE's, so elements that compare equal end in the same order. Two things
 * differ, neither visible in the result:
 *   - the EE swaps 8 bytes at a time when the base and the element size
 *     are 8-aligned (swaptype 0 and 1) and bytes otherwise; this copy always
 *     swaps bytes, which moves the same data;
 *   - the EE's counts are 32-bit unsigned (its size_t) and its d and r are
 *     int; here they are size_t. Every value involved is non-negative and
 *     far below 2^31 for any array the game sorts, so the comparisons and
 *     quotients are the same.
 */
#include <stddef.h>

#include "ico_newlib.h"

typedef int (*ico_qsort_cmp)(const void *, const void *);

static void ico_qsort_swapfunc(char *a, char *b, size_t n)
{
    do {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    } while (--n > 0);
}

#define qswap(a, b) ico_qsort_swapfunc((a), (b), es)
#define qvecswap(a, b, n)                                                                          \
    if ((n) > 0)                                                                                   \
    ico_qsort_swapfunc((a), (b), (n))

static char *ico_qsort_med3(char *a, char *b, char *c, ico_qsort_cmp cmp)
{
    return cmp(a, b) < 0 ? (cmp(b, c) < 0 ? b : (cmp(a, c) < 0 ? c : a))
                         : (cmp(b, c) > 0 ? b : (cmp(a, c) < 0 ? a : c));
}

void ico_qsort(void *a, size_t n, size_t es, int (*cmp)(const void *, const void *))
{
    char *pa;
    char *pb;
    char *pc;
    char *pd;
    char *pl;
    char *pm;
    char *pn;
    size_t d;
    size_t r;
    int c;
    int swap_cnt;

loop:
    swap_cnt = 0;
    if (n < 7) {
        for (pm = (char *)a + es; pm < (char *)a + n * es; pm += es) {
            for (pl = pm; pl > (char *)a && cmp(pl - es, pl) > 0; pl -= es) {
                qswap(pl, pl - es);
            }
        }
        return;
    }
    pm = (char *)a + (n / 2) * es;
    if (n > 7) {
        pl = (char *)a;
        pn = (char *)a + (n - 1) * es;
        if (n > 40) {
            d = (n / 8) * es;
            pl = ico_qsort_med3(pl, pl + d, pl + 2 * d, cmp);
            pm = ico_qsort_med3(pm - d, pm, pm + d, cmp);
            pn = ico_qsort_med3(pn - 2 * d, pn - d, pn, cmp);
        }
        pm = ico_qsort_med3(pl, pm, pn, cmp);
    }
    qswap((char *)a, pm);
    pa = pb = (char *)a + es;

    pc = pd = (char *)a + (n - 1) * es;
    for (;;) {
        while (pb <= pc && (c = cmp(pb, a)) <= 0) {
            if (c == 0) {
                swap_cnt = 1;
                qswap(pa, pb);
                pa += es;
            }
            pb += es;
        }
        while (pb <= pc && (c = cmp(pc, a)) >= 0) {
            if (c == 0) {
                swap_cnt = 1;
                qswap(pc, pd);
                pd -= es;
            }
            pc -= es;
        }
        if (pb > pc) {
            break;
        }
        qswap(pb, pc);
        swap_cnt = 1;
        pb += es;
        pc -= es;
    }
    if (swap_cnt == 0) { /* switch to insertion sort */
        for (pm = (char *)a + es; pm < (char *)a + n * es; pm += es) {
            for (pl = pm; pl > (char *)a && cmp(pl - es, pl) > 0; pl -= es) {
                qswap(pl, pl - es);
            }
        }
        return;
    }

    pn = (char *)a + n * es;
    r = (size_t)(pa - (char *)a) < (size_t)(pb - pa) ? (size_t)(pa - (char *)a)
                                                       : (size_t)(pb - pa);
    qvecswap((char *)a, pb - r, r);
    r = (size_t)(pd - pc) < (size_t)(pn - pd) - es ? (size_t)(pd - pc) : (size_t)(pn - pd) - es;
    qvecswap(pb, pn - r, r);
    if ((r = (size_t)(pb - pa)) > es) {
        ico_qsort(a, r / es, es, cmp);
    }
    if ((r = (size_t)(pd - pc)) > es) {
        a = pn - r;
        n = r / es;
        goto loop;
    }
}
