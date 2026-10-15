/* rd_bisect_plan.h: the loss bisect's second level, the draw within the
 * pass (docs/port/RENDER.md, "The loss bisect").
 *
 * The first level (rd.h rd_loss_bisect) names the pass of the lost frame
 * whose draws lose the device.  The second narrows within it: each step is
 * one replay of the frame on a renderer of its own (passes 1 .. P - 1 as
 * the first level ran them, then draws drawFirst .. drawLast of pass P that
 * pass the step's filter), and its verdict is lost or clean.  This planner
 * says which step comes next from the steps so far and their verdicts, so
 * a run ended by a step (the state file, port/platform/diag_host.c
 * ico_diag_loss_bisect2_*) continues where it was:
 *
 *   1. MASK_MARKS over draws 1 .. N: the pass with the VU draws' marks
 *      left out (issue 95's hypothesis: clean means the marks did it);
 *   2. the prefix search: draws 1 .. m, halving between the longest clean
 *      prefix (0: the passes before P ran clean) and the shortest lost one
 *      (N: the first level lost the whole pass), until they are one apart:
 *      draw D = the shortest lost prefix (11 steps for 1,312 draws);
 *   3. draw D alone (passes 1 .. P - 1 and D);
 *   4. the class filters over draws 1 .. D, from D's note: only the draws
 *      of D's vertex shader, of its clip mode, the skinned draws, the
 *      draws of its texture; then, when no step of this level lost the
 *      whole prefix 1 .. D = N, that prefix again.
 *
 * Bounded at RD_BISECT_PLAN_STEPS steps.  A killed step (the process ended
 * inside it) counts as lost.  Prefix verdicts that contradict each other
 * (a clean prefix at least as long as a lost one: the device lost by
 * something other than the draws) end the plan, which says so.
 *
 * Pure: no device, no renderer, no file (ctest rd_bisect_plan). */
#ifndef PORT_RENDER_RD_BISECT_PLAN_H
#define PORT_RENDER_RD_BISECT_PLAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RD_BISECT_PLAN_STEPS 16

/* the step's kind */
typedef enum RdBisectKind {
    RD_BISECT_STEP_MASK = 1, /* draws 1 .. N, the marks masked */
    RD_BISECT_STEP_PREFIX,   /* draws 1 .. m */
    RD_BISECT_STEP_ALONE,    /* draw D alone */
    RD_BISECT_STEP_CLASS,    /* draws 1 .. D of one class (filter) */
    RD_BISECT_STEP_AGAIN     /* draws 1 .. N again (the first level's verdict checked) */
} RdBisectKind;

/* the draws a step keeps (rd_internal.h rd__replay_set_window_draws) */
typedef enum RdBisectFilter {
    RD_BISECT_ALL = 0,
    RD_BISECT_MASK_MARKS, /* every draw; the VU draws' marks left out */
    RD_BISECT_VS_ONLY,    /* the draws of vertex shader arg (RdVsKind) */
    RD_BISECT_CLIP_ONLY,  /* the VU draws of clip mode arg (VU_CLIP_*) */
    RD_BISECT_SKINNED_ONLY,
    RD_BISECT_TEX_ONLY /* the draws binding texture id arg */
} RdBisectFilter;

typedef enum RdBisectVerdict {
    RD_BISECT_V_NONE = 0, /* not run yet */
    RD_BISECT_V_CLEAN,
    RD_BISECT_V_LOST,
    RD_BISECT_V_KILLED /* the run ended inside it: counts as lost */
} RdBisectVerdict;

typedef struct RdBisectStep {
    uint8_t kind;   /* RdBisectKind */
    uint8_t filter; /* RdBisectFilter */
    uint32_t drawFirst, drawLast;
    uint32_t arg; /* the filter's value */
} RdBisectStep;

/* what the planner reads of draw D (the replay's per-draw note) */
typedef struct RdBisectKey {
    uint8_t valid;
    uint8_t vu;      /* a VU draw: clip and skinned mean something */
    uint8_t skinned; /* RDC_SKINNED */
    uint8_t vs;      /* RdVsKind */
    uint8_t clip;    /* VU_CLIP_* (vu_draw.z & 3) */
    uint32_t tex;    /* the bound texture's id, 0 none */
} RdBisectKey;

typedef struct RdBisectPlan {
    uint32_t draws; /* N: the pass's draws (the first level's count) */
    uint32_t count; /* steps planned so far */
    RdBisectStep step[RD_BISECT_PLAN_STEPS];
    uint8_t verdict[RD_BISECT_PLAN_STEPS]; /* RdBisectVerdict */
    /* each step's note of its last draw (drawLast): D's comes from the
     * lost prefix ending at D or from D alone */
    RdBisectKey key[RD_BISECT_PLAN_STEPS];
} RdBisectPlan;

typedef enum RdBisectEnd {
    RD_BISECT_END_NO = 0,       /* not done: *step is the next */
    RD_BISECT_END_DONE,         /* every step it plans has run */
    RD_BISECT_END_BOUND,        /* RD_BISECT_PLAN_STEPS steps ran */
    RD_BISECT_END_NOT_MONOTONE, /* the prefix verdicts contradict each other */
    RD_BISECT_END_NOTHING       /* no draw to search (N = 0) */
} RdBisectEnd;

/* What the steps found, for the final line. */
typedef struct RdBisectResult2 {
    RdBisectEnd end;
    uint32_t draw; /* D, 0 when the search did not finish */
    int maskClean; /* the MASK_MARKS step: 1 clean, 0 lost, -1 not run */
    int alone;     /* draw D alone: 1 lost (faults alone), 0 clean, -1 not run */
} RdBisectResult2;

void rd_bisect_plan_init(RdBisectPlan *p, uint32_t draws);
/* The next step into *step (and counted in p->step, verdict NONE), or the
 * end (*step untouched) with what was found in *res.  The verdict of the
 * step before must be set (rd_bisect_plan_set). */
RdBisectEnd rd_bisect_plan_next(RdBisectPlan *p, RdBisectStep *step, RdBisectResult2 *res);
/* the verdict of step i (0-based) and the note of its last draw (NULL:
 * none; a lost prefix ending at D, or D alone, gives D's) */
void rd_bisect_plan_set(RdBisectPlan *p, uint32_t i, RdBisectVerdict v, const RdBisectKey *key);
/* What the steps so far found (also when not done). */
void rd_bisect_plan_result(const RdBisectPlan *p, RdBisectResult2 *res);

/* "draws 1..656 of 1312", ", marks masked", ", vertex shader 5 only", ... */
void rd_bisect_step_text(const RdBisectStep *s, uint32_t draws, char *out, size_t size);
/* "faults alone" / "does not fault alone" / "not finished" (the draw's
 * verdict) */
const char *rd_bisect_alone_text(const RdBisectResult2 *r);
/* "clean with the marks masked" / "lost with the marks masked" / "" */
const char *rd_bisect_mask_text(const RdBisectResult2 *r);
/* the step kind's and filter's names in the state file (rd_bisect_kind_parse:
 * -1 unknown) */
const char *rd_bisect_kind_name(uint8_t kind);
int rd_bisect_kind_parse(const char *s);
const char *rd_bisect_filter_name(uint8_t filter);
int rd_bisect_filter_parse(const char *s);

#endif
