/*
 * port/render/rd_perf_where.h
 *
 * One table a perf scene block: where the time went (issue 179; the owner,
 * 2026-11-23: "make sure we have more diagnostics in the next release so
 * that we dont have a confusion of where to improve performance next
 * round").  The block's dozen long lines stay; this is the same block's
 * numbers in one place, a fixed format for the next reader, every line
 * beginning "where: " (after "window: "):
 *
 *   where: stage 42 play: limit GPU-bound; 9.5 of 25.0 fps (the game 25.0 of
 *     25.0); GPU work 103.9 ms (99 % of the 105.3 ms between presents);
 *     render thread 20.1 ms (19 %); game step 8.0 ms (40 % of its 20.0 ms
 *     tick)
 *   where: GPU a picture 105.3 ms (work 103.9 ms): list 0 49.0 ms 47 %
 *     [283 draws, 1.2 MB loaded, 3.4 MB stored, 0.35 Mpx]; list 3 ...;
 *     pre-pass 7.1 ms 7 %; before list 0 0.5 ms 0 %; present 2.7 ms 3 %;
 *     effects (inside their lists): reduction 2.4 ms 2 %, aura 4.2 ms 4 %;
 *     all: 70.1k triangles, 81.0k vertex invocations, 900 draws, 3.1 MB
 *     loaded, 6.2 MB stored, 12.0 Mpx
 *   where: CPU a picture: render thread 20.1 ms (walk 8.0, uploads 3.0,
 *     binds 2.0, interpolation 1.0, submit 6.1); submit thread 0.0 ms in 0
 *     pictures; present calls 1.2 ms; GPU waits 40.0 ms; game step 8.0 ms
 *   where: attempts: <the window's text>
 *   where: phone: <the window's text>; stalls: <the window's text>
 *   where: largest: GPU list 0 49.0 ms (47 % of the GPU span); CPU walk 8.0
 *     ms (8 % of the time between presents)
 *
 * Every number is the block's (rd_perf_rank.h RdPerfRankBlock): per
 * picture over its records (the GPU's over the timed ones, a list's counts
 * over the pictures timed pass by pass, [...] where none was), shares of
 * the per-picture GPU span; no new measurement.  A piece of 0 ms is left
 * out.  The effects sit inside their lists' times.
 *
 * The rate card's hook (another package may print "rd: rate card:" lines
 * with ns a fragment, a vertex and a triangle on the device): with x->rates
 * set the last line gains "; explained by the rate card: X of Y ms" once
 * rd_perf_where_explained_ms answers (it answers -1, not yet, here).
 *
 * Pure (port/render/test/rd_perf_where_test.c).
 */
#ifndef RD_PERF_WHERE_H
#define RD_PERF_WHERE_H

#include <stddef.h>

#include "rd_perf_rank.h"

#define RD_PERF_WHERE_LINES 6

/* a device's rates (the rate card's), ns; < 0 not known */
typedef struct RdPerfWhereRates {
    double nsPerFragment, nsPerVertex, nsPerTriangle;
} RdPerfWhereRates;

/* The ms of the block's GPU work a picture the rates explain; -1 not
   computed (the hook: not implemented here) */
double rd_perf_where_explained_ms(const RdPerfRankBlock *b, const RdPerfWhereRates *r);

typedef struct RdPerfWhereExtra {
    const char *(*postName)(int post); /* an effect's name (rd_perf_post_name); NULL "effect N" */
    const char *attempts;              /* the attempts' line's text, NULL "not known" */
    const char *phone;                 /* the phone's state, NULL "not read" */
    const char *stalls;                /* the stalls' count and largest piece, NULL "none" */
    const RdPerfWhereRates *rates;     /* NULL: no rate card */
} RdPerfWhereExtra;

/* line (0 .. RD_PERF_WHERE_LINES - 1) of the table, without "window: ";
   0 past the last (the length otherwise, snprintf's count cut to fit) */
int rd_perf_where_text(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                       const RdPerfRankVerdict *v, const RdPerfWhereExtra *x, unsigned line,
                       char *buf, size_t n);

#endif /* RD_PERF_WHERE_H */
