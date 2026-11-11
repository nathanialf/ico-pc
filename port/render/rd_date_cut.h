/* rd_date_cut.h: the rule that finds the passes a DATE snapshot opens whose
 * draws never touch the depth (rd_date_cut.c; docs/port/WORK_BUFFERS.md,
 * "DATE's passes without the depth").  Pure: no renderer state, so
 * port/render/test/rd_date_cut_test.c drives it with made-up commands.
 *
 * On the DATE snapshot path (a device without the in-pass read: Arm,
 * Qualcomm, the desktop's OpenGL) each DATE draw whose target's alpha was
 * written since the last snapshot ends the open pass for the R8 copy
 * (rd_date_cut.c rd__date_snapshot), and the pass after it begins again with
 * the target's depth attached, loaded and stored, whatever its draws do with
 * it.  A run is the commands from such a DATE draw (its start) up to the
 * next command that ends that pass for certain (its end, not in it); a run
 * is "depth-free" when every draw in it is a screen prim on the run's target
 * that neither tests the depth (ZTE off or ZTST ALWAYS) nor writes it, takes
 * no PRIM.AA1 (its stencil pairs) and samples no depth view, and every other
 * command in it is a state command or draws nothing.  The commands that end
 * the pass for certain: a draw on another colour target (its own pass), and
 * a DATE draw on the run's target after a draw of the run wrote alpha (its
 * snapshot is retaken: rd__date_retake, the write serial moved).  Anything
 * else (a copy, a clear, a post sprite, the fog, the shadow commands, a VU
 * draw on the target, a draw that uses the depth) ends the run undecided:
 * not depth-free, its passes as before.  The replay's end closes a run.
 *
 * The rule only predicts: the replay's hook (rd_date_cut.c
 * rd__date_cut_depth) begins a pass without the depth only for a draw of a
 * depth-free run, and a draw that needs the depth while such a pass is open
 * begins a pass of its own with it (bindDrawEx: the pass's depth differs),
 * so a wrong prediction costs a pass, never a byte. */
#ifndef PORT_RENDER_RD_DATE_CUT_H
#define PORT_RENDER_RD_DATE_CUT_H

#include <stdbool.h>
#include <stdint.h>

/* a command as the rule reads it */
enum {
    RD_DC_STATE,  /* a state command, or one that draws nothing (a NOP, a folded sprite) */
    RD_DC_SCREEN, /* a screen prim drawn */
    RD_DC_DRAW,   /* a VU draw (mesh, skinned, grid, particles) */
    RD_DC_OTHER   /* anything else: a copy, a clear, a post sprite, the fog, a shadow command */
};

typedef struct RdDateCutCmd {
    uint8_t kind;    /* RD_DC_* */
    uint32_t target; /* the colour target in force (its id) */
    bool date;       /* DATE on (a screen prim) */
    bool
        depthFree; /* a screen prim that neither tests nor writes the depth, no AA1, no depth view */
    bool alpha;    /* it writes the target's alpha (the colour mask's A) */
    bool hasDepth; /* the state's depth is a depth target (a pass would attach it) */
} RdDateCutCmd;

typedef struct RdDateCutScan {
    bool open;       /* a run is open */
    bool alpha;      /* a draw of the open run wrote its target's alpha */
    uint32_t target; /* the open run's target */
    uint32_t start;  /* its first command's ordinal */
} RdDateCutScan;

/* what a step did */
enum {
    RD_DC_STEP_NONE = 0,
    RD_DC_STEP_FREE = 1,  /* a depth-free run [*start, ord) closed */
    RD_DC_STEP_UNDEC = 2, /* the open run ended undecided (not depth-free) */
};

/* command c (ordinal ord) under the scan s: the run it closes, if any, in
 * *start (the run is [*start, ord)); c itself may then open the next run */
static inline int rd_date_cut_step(RdDateCutScan *s, const RdDateCutCmd *c, uint32_t ord,
                                   uint32_t *start)
{
    const bool opens = c->kind == RD_DC_SCREEN && c->date && c->depthFree && c->hasDepth;
    int did = RD_DC_STEP_NONE;
    if (s->open && c->kind != RD_DC_STATE) {
        const bool here = c->target == s->target;
        if (!here && (c->kind == RD_DC_SCREEN || c->kind == RD_DC_DRAW)) {
            did = RD_DC_STEP_FREE; /* a draw on another target: its own pass */
        } else if (here && c->kind == RD_DC_SCREEN && c->depthFree) {
            if (c->date && s->alpha) {
                did = RD_DC_STEP_FREE; /* the snapshot retaken: the pass ends */
            } else {
                s->alpha = s->alpha || c->alpha; /* in the run */
                return RD_DC_STEP_NONE;
            }
        } else {
            did = RD_DC_STEP_UNDEC;
        }
        *start = s->start;
        s->open = false;
    }
    if (!s->open && opens) {
        s->open = true;
        s->alpha = c->alpha;
        s->target = c->target;
        s->start = ord;
    }
    return did;
}

/* The scales the cut takes: whole texels per GS pixel on both axes.  At a
 * fractional scale (the PowerVR phone's Resolution Auto, 2364x1080 for a
 * 848x448 frame: 2.79 x 2.41) lavapipe drew the second shadow composite one
 * level apart at four texels with the depth attached and without it (round 3
 * of v0.11.5's merge check: the Pixel's v3677 at that setting, SCENE
 * (354,897), (354,905), (354,906), (23,974), every other package's switch off
 * and the store plan's poison off alike): the two pipelines' fragment code,
 * the same shader, rounds the above-1x sample apart where it lands on a tie.
 * The integer scales (1x, 2x, 3x) were byte-equal on every dump, so the cut
 * keeps to them: when in doubt the pass keeps its depth. */
static inline bool rd_date_cut_scale_ok(float sx, float sy)
{
    return sx >= 1.0f && sy >= 1.0f && sx == (float)(uint32_t)sx && sy == (float)(uint32_t)sy;
}

/* the replay's end at ordinal ord: the open run is depth-free ([*start, ord)) */
static inline int rd_date_cut_finish(RdDateCutScan *s, uint32_t ord, uint32_t *start)
{
    (void)ord;
    if (!s->open) {
        return RD_DC_STEP_NONE;
    }
    s->open = false;
    *start = s->start;
    return RD_DC_STEP_FREE;
}

#endif /* PORT_RENDER_RD_DATE_CUT_H */
