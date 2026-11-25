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
 *   where: explained by the rate card (the card's units times the counts,
 *     not bounds, not a split; fragments, loads and stores not counted):
 *     list 0 49.0 ms measured: its triangles about 49.4 ms (91.1k x 541.76
 *     ns, each with its three corners and one 2x2 quad of fragments; 31.0k
 *     of them drawn again by a later pass, 0.0k the shadow volumes'), its
 *     corners alone about 32.3 ms (273.3k x 118.29 ns, inside the
 *     triangles'; the card's corner also holds one pre-pass vertex),
 *     passes about 0.0 ms (1.0 x 0.003 ms, an empty SCENE pass each), so
 *     the card's units account for about 49.4 of 49.0 ms (the triangles
 *     and the passes); list 3 15.0 ms measured: ..., so the card's units
 *     account for about 6.6 of 15.0 ms (the triangles alone)
 *
 * The last line puts the block's counts a picture against the rate card's
 * units (rd_ratecard.h, measured at start-up; issue 179: on a tiler the
 * timestamps cannot split a pass by draw), per list that drew VU or
 * shadow volume triangles (the others left out), each term on its own:
 *   triangles  the list's VU triangles, those drawn again by a draw's
 *              later passes and the shadow volumes', times the card's "a
 *              triangle binned" (ns: a small visible triangle less a large
 *              one, its three corners and its 2x2 quad of fragments
 *              inside);
 *   corners    3 a triangle each pass, times the card's "a vertex" (ns a
 *              corner drawn off the target, (V - E) over 3 x 2048 x 8: the
 *              draws' main shader, one pre-pass vertex and the record's
 *              load, a third of an off-target triangle's clipping): inside
 *              the triangles' figure, never added to it;
 *   passes     the list's render passes a timed picture times the card's
 *              "a pass with nothing drawn" (ms: SCENE cleared, a replay's
 *              span); no figure where the list's first pass is not SCENE
 *              at the card's size, or the list was not timed pass by pass;
 *   account    the triangles' figure plus the passes' (where it has one):
 *              "the card's units account for about N of Y ms".
 * Every term is "about": an estimate by the card's units, not a bound
 * either way.  Each would be an upper bound of the list's own work only
 * if all of these held, and none is checked here (docs/port/
 * ANDROID_DIAGNOSTICS_LEDGERS.md "Where the time went" lists them):
 *   - the same pipeline class: play's draws run the card's main shader
 *     (vu_main_vs or its merge build) with fragment work a triangle no
 *     larger than the card's one 2x2 quad;
 *   - the same target size and binning: the list's pass is SCENE at the
 *     card's tw x th (checked for the passes' term only) and each of play's
 *     triangles is binned into no more tiles than the card's small one
 *     (play's are larger, so this one fails in general);
 *   - the same cache state: play's records and indices hit the caches as
 *     the card's 2048-triangle mesh does;
 *   - the same pre-pass state, on for both: play's programs' own work is
 *     then in the pre-pass, timed apart (another state gives no figure;
 *     both off gives figures of the card's prelit program, not play's);
 *   - the card's frames not compacted (the index compaction held off for
 *     them) while play's compaction only removes work: the counts are the
 *     draws' lists at full length, fillers included;
 *   - the blend's passes as they ran: the "drawn again" count takes the
 *     plan's passes after the one-pass blend has merged a pair
 *     (rd_replay_vu.c vuDraw: rd__op_take before rd__perf_vu_draw), and
 *     the card draws with the one-pass blend held off;
 *   - the same clocks: the GPU at the card's frequency, not throttled;
 *   - the parts add: nothing of the list overlaps them (a tiler's binning
 *     of one pass runs beside another's fragments).
 * So the line never says "at least N ms is not vertices or triangles":
 * that needs the triangles' figure to be such a bound.  A term the card
 * has no figure for (not measured, a difference not above 0, another
 * unit, the card's pre-pass state not the block's) says "no rate card
 * figure (why)", never a number; no card this run (or one of another
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

/* One list's terms a picture (the last line's): estimates by the card's
   units, not bounds */
typedef struct RdPerfWhereExplained {
    double ms;                      /* the list's GPU ms a picture (the GPU line's) */
    double tris, again, shadow;     /* its VU triangles, drawn again, shadow volumes' */
    double corners, passes;         /* 3 (tris + again); render passes a timed picture */
    double vertexMs, triMs, passMs; /* each term, < 0 no figure */
    bool prepass;                   /* the VU pre-pass on for the card and the block (the
                                       card's corner then holds one pre-pass vertex) */
    double cardMs;                  /* triMs plus passMs (where >= 0): what the card's
                                       units account for; < 0 no triangles' figure */
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
