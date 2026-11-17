/*
 * port/platform/set_size.h
 *
 * The start-up set sized by the device's own memory fall (issue 215).  On
 * Qualcomm's own driver the device's free memory falls with every pipeline
 * made and does not come back during the run (about 24 MB a VU pipeline
 * and 11-19 MB a 2D one on the issue 215 handheld, about 14 MB a VU one and
 * nothing for the 2D ones on the issue 123 tablet), so a set that runs
 * until the free-memory floor (memory_floor.h) leaves play below it.  This
 * rule acts on each start's own measurement, never on how an earlier run
 * ended:
 *
 *   measure  the fall of free memory (MemAvailable) a pipeline over each
 *            batch of the set, per family (2D: every pipeline without a VU
 *            vertex shader; VU), a batch whose pipelines are of both
 *            families not counted; a family's figure is the median of its
 *            batches, trusted after SET_SIZE_MIN_BATCHES of them;
 *   predict  each family's rest = its pipelines not made yet x its fall,
 *            against the headroom = free memory now - the floor - play's
 *            known need (the window's: the VU pre-pass's record at the
 *            run's need and the mesh arena's first chunk, where not made
 *            yet; rd_vu_rec_early.h rd_set_size_need: made already, as the
 *            pre-pass self-test's record, they are out of the free memory
 *            read and the start line names them);
 *   decide   the family of the next pipeline first, then the other: a
 *            family whose rest added to the rests kept so far does not fit
 *            stops now (its pipelines made on first use), else its rest is
 *            kept.  A family whose fall is 0 or below costs nothing and is
 *            never stopped, so where free memory does not fall with
 *            pipelines (other drivers, desktops) nothing changes; a family
 *            not trusted yet is measured first; a stopped family stays
 *            stopped for the set.  Without a floor (every system but
 *            Android, or the floor set off) the rule is off.
 *
 * The window (window_pipe_finish.c) reads the memory, logs the lines and
 * hands the stop to the renderer (rd.h rd_set_set_size_fn); the stop is the
 * set's own, for this start only (diag_host.c: no resume record, so the
 * next start measures again from its first pipeline).
 *
 * Pure but for the last part (set_size_publish, set_size_first_picture: the
 * first picture's line).  The unit test is port/platform/test/set_size_test.c.
 */
#ifndef ICO_PLATFORM_SET_SIZE_H
#define ICO_PLATFORM_SET_SIZE_H

#include <stddef.h>

/* the families */
#define SET_SIZE_2D 0
#define SET_SIZE_VU 1
#define SET_SIZE_FAMILIES 2

/* the batches of a family before its fall is trusted: the median of three
   outvotes one batch whose figure another process moved */
#define SET_SIZE_MIN_BATCHES 3u
/* one sample a batch, a batch of one pipeline at least, a set of at most
   the renderer's 4096 keys (rd_state.h RD_PIPELINE_CACHE_MAX): no family
   has more samples than this */
#define SET_SIZE_SAMPLES_MAX 4096u
/* the start and decision lines' buffer (diag_host.c ico_diag_log cuts at
   1022 bytes; with every number at its widest the decision line is 504
   bytes and the start line 585: set_size_test's room case) */
#define SET_SIZE_LINE_MAX 1000
/* the short words for last-run.txt's phase and the first picture's line
   (the longest 98 bytes: both families stopped at 999999.0 MB a pipeline,
   the most a text shows) */
#define SET_SIZE_WORDS_MAX 112
/* the stop's words the renderer puts in its budget (rd.h RdSetSizeFn) */
#define SET_SIZE_WHY_MAX 160

/* --- The pure part ------------------------------------------------------------ */

typedef struct SetSizeIn {
    long freeMb;                         /* free memory now, below 0 not read */
    long floorMb;                        /* the floor, 0 or below none */
    long needMb;                         /* play's known need not made yet */
    unsigned left[SET_SIZE_FAMILIES];    /* the family's pipelines not made yet */
    int next;                            /* the family of the next pipeline, -1 none */
    unsigned batches[SET_SIZE_FAMILIES]; /* the family's batches measured */
    double fallMb[SET_SIZE_FAMILIES];    /* their median, MB a pipeline */
    unsigned stopped;                    /* the families stopped earlier (bit 1 << family) */
} SetSizeIn;

typedef struct SetSizeOut {
    int active;                     /* a floor and a free figure: the rule decides */
    long headroomMb;                /* free - floor - need */
    long costMb[SET_SIZE_FAMILIES]; /* the rest's cost, -1 not trusted yet */
    long keptMb;                    /* the rests kept */
    unsigned stop;                  /* the families stopped after this decision */
    unsigned newly;                 /* of them, those this decision stopped */
} SetSizeOut;

/* The median of n values (the lower of the two middle ones for an even n),
   0 for none */
double set_size_median(const float *v, unsigned n);
/* The decision (out zeroed first) */
void set_size_decide(const SetSizeIn *in, SetSizeOut *out);

/* --- The set's measurement -------------------------------------------------------- */

typedef struct SetSizeRun {
    int active;
    long startMb, floorMb, lastMb, lowMb;
    unsigned lastMade[SET_SIZE_FAMILIES];
    unsigned n[SET_SIZE_FAMILIES];
    float fall[SET_SIZE_FAMILIES][SET_SIZE_SAMPLES_MAX];
    unsigned mixed;   /* batches of both families (not counted) */
    unsigned stopped; /* the families stopped */
    unsigned said;    /* the families whose first trusted verdict has its line */
    unsigned leftAtStop[SET_SIZE_FAMILIES], placeAtStop[SET_SIZE_FAMILIES];
    double fallAtStop[SET_SIZE_FAMILIES];
} SetSizeRun;

/* One ask, before each batch of the set (the renderer's counts) */
typedef struct SetSizeAsk {
    long freeMb;                      /* free memory now, below 0 not read */
    unsigned made[SET_SIZE_FAMILIES]; /* this set's pipelines made so far */
    unsigned left[SET_SIZE_FAMILIES]; /* its pipelines not made yet */
    int next;                         /* the family of the next pipeline not stopped, -1 none */
    long needMb;                      /* play's known need not made yet */
    unsigned place, total;            /* the next pipeline (0-based) and the set's size */
} SetSizeAsk;

/* The set's start: free memory freeMb, its floor floorMb (0 or below: the
   rule off for the set) */
void set_size_begin(SetSizeRun *r, long freeMb, long floorMb);
/* An ask: the batch since the last ask measured (one family's pipelines
   only), the decision taken; returns the families stopped (bit 1 <<
   family).  line: the decision's line when one is due (a family's first
   trusted verdict, or a stop), else ""; why: the newly stopped families'
   words for the renderer's budget, else "" (either may be NULL) */
unsigned set_size_ask(SetSizeRun *r, const SetSizeAsk *a, char *line, size_t lineSize, char *why,
                      size_t whySize);
/* The set's start line ("ico_pc: pipelines: the start-up set's size by the
   device's memory fall: on (...)", or "... off (<offWhy>): ..."): play's
   known need not made yet (recordMb, arenaMb) and what of it is made
   already (recordMadeMb, arenaMadeMb: made before the free figure was
   read, not counted again); the length written */
size_t set_size_rule_text(char *out, size_t size, int active, const char *offWhy, unsigned total,
                          long freeMb, long floorMb, long recordMb, long arenaMb, long recordMadeMb,
                          long arenaMadeMb);
/* The short words of the run's decision for last-run.txt's phase ("start-up
   set sized by the memory fall: VU left to first use (14.3 MB a pipeline)";
   "" before a verdict) */
size_t set_size_words(const SetSizeRun *r, char *out, size_t size);
/* "ico_pc: memory at the first picture: free 6213 MB of 11265 MB (the floor
   1915 MB), the lowest since the start 6150 MB; the <words>" ("start-up set
   made as each family fit" without words; lowMb below 0: not known) */
size_t set_size_first_picture_text(long freeMb, long totalMb, long lowMb, long floorMb,
                                   const char *words, char *out, size_t size);

/* --- The run's state for the first picture (the window sets it) ------------------- */

/* the rule was active for the start's set with floorMb, the lowest free
   memory its asks read, and its words */
void set_size_publish(int active, long floorMb, long lowMb, const char *words);
/* At the first picture (diag_host.c, once last_run_present moved the phase
   there): one line where the rule was active, the free memory then and the
   lowest since the start (last-run.txt's beats and the set's asks) */
void set_size_first_picture(void);

#endif /* ICO_PLATFORM_SET_SIZE_H */
