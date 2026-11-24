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
 *     loaded, 6.2 MB stored, 12.0 Mpx, 31.0k VU triangles drawn again by a
 *     later pass, 2.1k shadow volume triangles
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
 *   where: explained by the rate card, upper bounds, not a split
 *     (fragments, loads and stores not counted): list 0 49.0 ms measured:
 *     its triangles at most 49.4 ms (91.1k x 541.76 ns, each with its three
 *     corners and one 2x2 quad of fragments; 31.0k of them drawn again by a
 *     later pass, 0.0k the shadow volumes'), its corners alone at most 32.3
 *     ms (273.3k x 118.29 ns: the card's corner also holds one pre-pass
 *     vertex), passes 0.0 ms (1.0 x 0.003 ms, an empty SCENE pass each);
 *     list 3 15.0 ms measured: its triangles at most 6.6 ms (...), ..., so
 *     at least 8.4 ms of it is not vertices or triangles by the card:
 *     fragments, loads and stores, or a stall
 *
 * The last line puts the block's counts a picture against the rate card's
 * units (rd_ratecard.h, measured at start-up; issue 179: on a tiler the
 * timestamps cannot split a pass by draw), per list that drew VU or
 * shadow volume triangles (the others left out), each term on its own and
 * never summed (the triangle's unit holds its corners: a sum would count
 * them twice):
 *   triangles  the list's VU triangles, those drawn again by a draw's
 *              later passes and the shadow volumes', times the card's "a
 *              triangle binned" (ns: a small visible triangle less a large
 *              one, its three corners and its 2x2 quad of fragments
 *              inside): with the VU pre-pass on (card and block) an upper
 *              bound of the list's vertex and triangle work, as play's
 *              programs' own work is then in the pre-pass, timed apart,
 *              and every corner runs the same main shader;
 *   corners    3 a triangle each pass, times the card's "a vertex" (ns a
 *              corner drawn off the target, (V - E) over 3 x 2048 x 8: the
 *              draws' main shader, one pre-pass vertex and the record's
 *              load, a third of an off-target triangle's clipping): an
 *              upper bound of the corners' share inside the triangles'
 *              (the pre-pass vertex in it is timed apart in play; the index
 *              compaction's and shared vertices' savings are not taken
 *              off).  Taking play's own ns a pre-pass vertex (the pre-pass
 *              piece over its vertices) off it is not sound: that figure
 *              carries the pre-pass's fixed work and play's programs, not
 *              the card's prelit vertex, so the rest would bound nothing;
 *   passes     the list's render passes a timed picture times the card's
 *              "a pass with nothing drawn" (ms: SCENE cleared, a replay's
 *              span); no figure where the list's first pass is not SCENE
 *              at the card's size, or the list was not timed pass by pass.
 * With the pre-pass off (card and block) the two are "about", not bounds:
 * play's programs are not the card's prelit one.  Where the triangles'
 * bound is below the measured time the line says how much at least is
 * something else (fragments, loads and stores, a stall); only then.  A
 * term the card has no figure for (not measured, a difference not above 0,
 * another unit, the card's pre-pass state not the block's) says "no rate
 * card figure (why)", never a number; no card this run (or one of another
 * graphics API) says so for the whole line.
 *
 * Pure (port/render/test/rd_perf_where_test.c).
 */
#ifndef RD_PERF_WHERE_H
#define RD_PERF_WHERE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_perf_rank.h"

#define RD_PERF_WHERE_LINES 7

struct RdRateCard; /* rd_ratecard.h */

/* One of the card's units for the clause: ok with v (the card's median in
   its unit: ns a corner, ns a triangle, ms a pass), else why it has none */
typedef struct RdPerfWhereRate {
    bool ok;
    double v;
    char why[176];
} RdPerfWhereRate;

typedef struct RdPerfWhereRates {
    bool have;                              /* a card this run, of this graphics API */
    char none[160];                         /* !have: why */
    RdPerfWhereRate vertex, triangle, pass; /* ns a corner, ns a triangle, ms a pass */
    int prepass;                            /* the card's vertex timed with the VU pre-pass:
                                               1 on, 0 off, -1 its label does not say */
    uint32_t tw, th;                        /* the card's SCENE, texels */
} RdPerfWhereRates;

/* The card (rd_rate_card_get's; NULL: none this run) as the clause's rates:
   an item not measured, at or below 0 or not in its unit has no figure;
   backend the block's graphics API (rhi_backend_kind_name; NULL not
   checked): a card whose device text names another has none at all */
void rd_perf_where_rates_of(const struct RdRateCard *c, const char *backend, RdPerfWhereRates *out);

/* One list's terms a picture (the last line's): bounds, never summed */
typedef struct RdPerfWhereExplained {
    double ms;                      /* the list's GPU ms a picture (the GPU line's) */
    double tris, again, shadow;     /* its VU triangles, drawn again, shadow volumes' */
    double corners, passes;         /* 3 (tris + again); render passes a timed picture */
    double vertexMs, triMs, passMs; /* each term, < 0 no figure */
    bool bound;                     /* the corner and triangle terms upper bounds (the VU
                                       pre-pass on, for the card and the block); else
                                       "about" */
    double gapMs;                   /* bound: ms - triMs where the triangles' bound is below
                                       the measured time, else < 0 */
    char vertexWhy[224], triWhy[224], passWhy[224]; /* no figure: why */
} RdPerfWhereExplained;

/* list (0 .. RD_LIST_COUNT - 1) of the block under the rates (r->have);
   false when it drew no VU or shadow volume triangle (or no rates) */
bool rd_perf_where_explained(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                             const RdPerfWhereRates *r, int list, RdPerfWhereExplained *out);

typedef struct RdPerfWhereExtra {
    const char *(*postName)(int post); /* an effect's name (rd_perf_post_name); NULL "effect N" */
    const char *attempts;              /* the attempts' line's text, NULL "not known" */
    const char *phone;                 /* the phone's state, NULL "not read" */
    const char *stalls;                /* the stalls' count and largest piece, NULL "none" */
    const RdPerfWhereRates *rates;     /* the rate card's (rd_perf_where_rates_of); NULL none */
} RdPerfWhereExtra;

/* line (0 .. RD_PERF_WHERE_LINES - 1) of the table, without "window: ";
   0 past the last (the length otherwise, snprintf's count cut to fit) */
int rd_perf_where_text(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                       const RdPerfRankVerdict *v, const RdPerfWhereExtra *x, unsigned line,
                       char *buf, size_t n);

#endif /* RD_PERF_WHERE_H */
