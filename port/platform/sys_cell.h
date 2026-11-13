/*
 * port/platform/sys_cell.h
 *
 * A last-value cell one thread writes and another reads without a lock
 * (a sequence lock over plain doubles): the Android log thread asks the
 * thermal state and reads /proc once a second (window_stall.c), and the
 * window's 10 s block reads the last answer and its age instead of making
 * the binder call and the file reads on the game thread (issue 123's
 * "freezes every 10 sec or so"; the owner's Pixel: 1.6-2.8 ms a block in
 * the kernel GPU and thermal part, rc4-rc9).
 *
 * One writer per cell.  A read that meets a write in progress or a torn
 * copy retries a few times and then reports nothing (0).  Every field is
 * read and written with the atomic builtins (no data race).
 *
 * Pure C, no globals: the unit test is port/platform/test/sys_cell_test.c.
 */
#ifndef ICO_PLATFORM_SYS_CELL_H
#define ICO_PLATFORM_SYS_CELL_H

#include <stdint.h>

/* 16: the memory block's cell (window_stall.c asks_more) holds 16 values;
   a put of more is cut to this, so its get must ask no more */
#define SYS_CELL_VALUES 16

typedef struct SysCell {
    uint32_t seq;  /* odd while written */
    uint32_t n;    /* values written, 0 none yet */
    double atMs;   /* when, ico_thread_now_ms */
    double tookMs; /* what the ask took on the writer */
    double v[SYS_CELL_VALUES];
} SysCell;

/* the writer: n values (at most SYS_CELL_VALUES) asked at atMs, the ask
   having taken tookMs */
void sys_cell_put(SysCell *c, double atMs, double tookMs, const double *v, uint32_t n);
/* any thread: the values (n of them), when and what they took; 0 when
   nothing was written yet or no consistent copy was had */
uint32_t sys_cell_get(const SysCell *c, double *v, uint32_t n, double *atMs, double *tookMs);

#endif /* ICO_PLATFORM_SYS_CELL_H */
