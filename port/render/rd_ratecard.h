/* rd_ratecard.h: the rate card (issue 179 diagnostics; docs/port/
 * RENDER_PERF.md, "The rate card").
 *
 * At start-up, after the self-tests, the device times small synthetic
 * frames through the game's own pixel and vertex builds (rd_ratecard_run.c)
 * and prints its unit costs once: a shaded fragment, a fragment hidden by
 * depth with discard and through the discard-free twin, a vertex, a
 * triangle binned, the 2x2-quad factor, a pass with nothing drawn, a
 * blended fragment.  Each a median of RD_RC_SAMPLES replays' GPU times
 * (their timestamps, rd__perf_gpu_ms_of) with the least and the largest
 * beside it.  A diagnostic only: nothing in play reads it this release.
 *
 * Once per device, driver, backend, SCENE size and version: the result
 * file (ICO_RATECARD_FILE, logs/ratecard.txt, from the platform:
 * diag_host.c) holds "measuring" while it runs and "done" with the card
 * after; a later start with the same key reprints the card ("from the run
 * of <date>"); a start after a run that ended while measuring skips it
 * ("blocked", until the key changes).  ICO_RD_RATECARD=1 (the tests) runs
 * it without a file; ICO_RD_RATECARD_SABOTAGE=end (the tests) ends the
 * process inside it, as a hang would.
 */
#ifndef RD_RATECARD_H
#define RD_RATECARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* timed replays a frame (the median of these; one untimed before) */
#define RD_RC_SAMPLES 5

/* the median, least and largest of n times (ms); n 0: all 0 */
typedef struct RdRcStat {
    uint32_t n;
    double med, min, max;
} RdRcStat;

RdRcStat rd_rc_stat(const double *ms, uint32_t n);

/* a unit cost: the medians' difference over count units, times scale (ms
 * to ns: 1e6), its spread from the extremes ((a.min - b.max) .. (a.max -
 * b.min), over count, times scale); not ok (why set) when either has fewer
 * than RD_RC_SAMPLES times, count is not above 0 or a number is not finite.
 * b.n 0: a alone (no difference) */
typedef struct RdRcUnit {
    bool ok;
    double med, lo, hi;
    char why[120];
} RdRcUnit;

RdRcUnit rd_rc_unit(RdRcStat a, RdRcStat b, double count, double scale);
/* the ratio of two units (med over med; the spread the extremes' ratios);
 * not ok when either is not, or the divisor's median is not above 0 */
RdRcUnit rd_rc_ratio(RdRcUnit a, RdRcUnit b);
/* not measured, for why */
RdRcUnit rd_rc_none(const char *why);

/* the card's items, in the printed order */
enum {
    RD_RC_SHADE,         /* a shaded fragment, the ordinary build (vu_ps or its _nodual) */
    RD_RC_SHADE_TEXA,    /* a shaded fragment, the textured build under TEXA */
    RD_RC_SHADE_TWIN,    /* a shaded fragment through the discard-free twin */
    RD_RC_SHADE_RGBA,    /* a shaded fragment, the ordinary build on the fast build's own form
                          (textured RGBA32, MODULATE with TCC, a linear filter) */
    RD_RC_SHADE_FAST,    /* the same through the fast pixel build (rd_fastps.h) */
    RD_RC_SHADE_FAST_ND, /* the same through its discard-free form, the twins' hold on */
    RD_RC_HIDDEN,        /* a fragment hidden by depth, the ordinary build (with discard) */
    RD_RC_HIDDEN_TWIN,   /* a fragment hidden by depth, through the twin */
    RD_RC_VERTEX,        /* a vertex: a corner drawn off the target */
    RD_RC_TRIANGLE,      /* a triangle binned: small against large, the same coverage */
    RD_RC_QUAD,          /* the 2x2-quad factor: the small layer's cost over the large's */
    RD_RC_PASS,          /* a pass with nothing drawn */
    RD_RC_PASS_MB,       /* a MB loaded or stored */
    RD_RC_BLEND,         /* a blended fragment (the two-pass blend or dual-source) */
    RD_RC_BLEND_RATIO,   /* the blended fragment over the opaque one */
    RD_RC_ITEMS
};

typedef struct RdRcItem {
    char label[112]; /* "a shaded fragment, vu_ps_nodual" */
    char unit[4];    /* "ns", "ms", "x" */
    RdRcUnit u;
} RdRcItem;

typedef struct RdRateCard {
    bool have;             /* measured or read back */
    char device[160];      /* the adapter, the driver, the backend */
    uint32_t w, h, tw, th; /* SCENE's GS size and texels */
    char sized[200];       /* what the frames were sized to */
    double totalMs;        /* the card's wall time */
    char date[32];         /* when it was measured ("2026-11-23 10:15") */
    RdRcItem item[RD_RC_ITEMS];
} RdRateCard;

/* The card's lines, each "rate card: ..." and '\n'-ended (rd__log adds
 * "rd: "): the head (the device, the size, what it was sized to, the
 * time), then one line an item, "<label>: <med> <unit> (min <lo>, max
 * <hi>)" or "<label>: not measured (<why>)".  The count written (at most
 * cap - 1). */
size_t rd_rc_text(const RdRateCard *c, char *out, size_t cap);

/* The card's numbers back from its lines (rd_rc_text's form, a reprint's
 * suffix allowed): the head's sizes and time, then item i from the i-th
 * item line (its value, unit and spread, or not measured); false when the
 * lines are not that form (fewer than RD_RC_ITEMS item lines). */
bool rd_rc_parse(const char *lines, RdRateCard *c);

/* The result file's decision (pure): text the file's (NULL none), key this
 * start's (device, driver, backend, size, version).  RD_RC_MEASURE: measure
 * (no file, another key); RD_RC_REPRINT: the card saved for this key, its
 * lines from *lines (into text) and its date; RD_RC_SKIP: a run with this
 * key ended while measuring (or was skipped since): not measured again, its
 * date.  The file's form: line 1 "measuring|done|blocked <date>", line 2
 * "key <key>", then (done) the card's lines. */
enum { RD_RC_MEASURE = 0, RD_RC_REPRINT, RD_RC_SKIP };

int rd_rc_decide(const char *text, const char *key, char *date, size_t dateSize,
                 const char **lines);
/* the file's text for state ("measuring", "done", "blocked") at date with
 * key and (done) the card's lines; the count written */
size_t rd_rc_file_text(const char *state, const char *date, const char *key, const char *lines,
                       char *out, size_t cap);

/* The renderer's side (rd_ratecard_run.c): measured, reprinted or skipped
 * per the file, the block logged; nothing without ICO_RATECARD_FILE or
 * ICO_RD_RATECARD=1.  Called by the window after the start-up self-tests
 * (window_host.c) and by the replay tool's --game-selftests. */
void rd_rate_card(void);
/* The card of this start (measured or read back) for a later reader (the
 * window's where table, DIAG8): false when there is none */
bool rd_rate_card_get(RdRateCard *out);

#endif
