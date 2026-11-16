/* rd_shadow_count.h: the shadow count at its full size above 1x, its form
 * (rd_shadow_count.c; issue 123, v0.11.8; docs/port/RENDER_POST.md "The
 * count from the stencil").
 *
 * Above 1x the first level's step (rd_shadowres.c) reads the count at the
 * scene's texels.  v0.11.7 drew it for that in a pass of its own after the
 * one draw (rd_shadow_one.c fullPass: 1728x1344 on the issue 123 tablet, up
 * to 10.69 ms there).  Three forms give the step the same bytes:
 *   RD_SC_STENCIL  no pass: the step reads the depth-stencil's stencil
 *                  through the count's table where the pass would have
 *                  written (shadowres_step_stencil_ps), guarded by
 *                  rd_shadow_one.c (the depth and count of one size, nothing
 *                  touching either before the step: else the pass after all);
 *   RD_SC_FULL     the full-size pass (v0.11.7's form);
 *   RD_SC_SEVEN    the seven stencil-tested draws at the full size, reduced
 *                  by box_reduce_ps (the count's form where the one draw is
 *                  off: rd_replay.c doShadowResolve).
 * Until the check at the first shadowed scene above 1x passed (below:
 * the stencil step's and the seven draws' first level compared byte for
 * byte with the full-size pass's on the device) every count takes the
 * full-size pass.  Then the timed replays (the perf lines' pass timing,
 * rd_perf.c) take the three forms in turn, RD_SC_TRIAL_N each, and the
 * stencil step is kept unless another form's median of list 3's GPU time
 * is lower by more than the margin (rd_sc_choose).  Without GPU timestamps
 * the stencil step is kept untimed.  ICO_RD_SHADOW_COUNT_FORM =
 * stencil | full | seven (the tests' switch, docs/port/TESTING.md) fixes it. */
#ifndef ICO_RD_SHADOW_COUNT_H
#define ICO_RD_SHADOW_COUNT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd.h"
#include "rhi.h"

enum { RD_SC_FULL = 0, RD_SC_STENCIL, RD_SC_SEVEN, RD_SC_FORMS };

/* the timed replays a form takes at the trial; its figure their median */
#define RD_SC_TRIAL_N 3
/* the trial's timed replays scheduled at most before it gives up (their
 * stamps unwritten or their pieces dropped): RD_SC_TRIAL_N each, four
 * times over */
#define RD_SC_TRIAL_MAX (4 * RD_SC_TRIAL_N * RD_SC_FORMS)

/* why the full-size pass was drawn after all on the stencil form (the
 * guard), or the chain refused (RD_SC_WHY_LIST) */
enum {
    RD_SC_WHY_SIZE = 0, /* the depth's texels are not the count's */
    RD_SC_WHY_DEPTH,    /* the depth was touched before the level's step */
    RD_SC_WHY_COUNT,    /* the count target was touched before the step */
    RD_SC_WHY_LIST,     /* one of them touched in another command list: the chain refused */
    RD_SC_WHY_PIPE,     /* the stencil step's shader or pipeline could not be made */
    RD_SC_WHY_AGAIN,    /* a second step of the chain after a touch: the chain refused */
    RD_SC_WHYS
};

/* the words: "the stencil step", "the full-size pass", "the seven draws" */
const char *rd_sc_form_word(int form);
/* "stencil", "full", "seven" (the env value and the clause's word) */
const char *rd_sc_form_name(int form);
const char *rd_sc_why_text(int why);

/* ---- pure (the CPU test's, rd_shadow_count_test) */
/* the median of v[0..n-1], 1 <= n <= RD_SC_TRIAL_N (the middle one of the
 * sorted values; of two, the lower) */
float rd_sc_median(const float *v, int n);
/* The choice, steady against noise: med[f] the median of form f's
 * RD_SC_TRIAL_N samples ms[f] (n[f] of them in; a form with fewer is not
 * measured, med 0), *margin the larger of RD_SC_MARGIN_SHARE of the
 * stencil step's median and the widest spread (max - min) of any measured
 * form's samples; the stencil step unless the full-size pass or the seven
 * draws has a median under the stencil step's by more than the margin
 * (then the lower of them; equal: the full-size pass); -1 (margin 0) when
 * the stencil step is not measured */
#define RD_SC_MARGIN_SHARE 0.10f
int rd_sc_choose(const float ms[RD_SC_FORMS][RD_SC_TRIAL_N], const uint8_t n[RD_SC_FORMS],
                 float med[RD_SC_FORMS], float *margin);
/* the texels (RGBA8, rows pitch bytes apart) of got that differ from ref's
 * over w x h, the first's place in *firstX, *firstY (left alone at 0) */
uint32_t rd_sc_check_differ(const uint8_t *ref, const uint8_t *got, uint32_t w, uint32_t h,
                            uint32_t pitch, uint32_t *firstX, uint32_t *firstY);
/* the k-th timed shadowed replay's form at the trial: the stencil step,
 * the seven draws and the full-size pass in turn, a form whose
 * RD_SC_TRIAL_N samples are in (got[f]) left out; -1 when all are in */
int rd_sc_trial_next(uint32_t k, const uint8_t got[RD_SC_FORMS]);
/* a timed replay's GPU time of list `list` in ms: the sum over its pieces
 * begun in that list (RdPerfPass.list); *written false when a piece of the
 * list came back without its stamps (gpuUs < 0) or the list has none */
double rd_sc_list_ms(const RdPerfPass *p, uint32_t n, int list, bool *written);

/* ---- the check (round 2): before any other form is drawn on a device,
 * the first presented replay above 1x draws, besides its picture (the
 * full-size pass and the first level from it, lvl[0]), the first level
 * again from the stencil and from the seven draws' full-size count into
 * scratch textures, and copies the three into one readback buffer
 * (rd_shadowres.c checkDraw); they are compared when that frame slot is
 * recycled (rd__shadow_count_collect from rd_perf.c, no wait).  The
 * stencil step differing keeps the full-size pass for the run (the
 * attempt ledger's row refused); the seven draws differing leave them out
 * of the trial; until it passes every replay takes the full-size pass. */
#define RD_SC_CHECK_TEXTURES 3 /* the stencil step's level, the seven draws' count and level */
#define RD_SC_CHECK_TRIES 8    /* check replays that could not draw it before it gives up */
bool rd__shadow_count_check_now(void);
/* the check replay did not draw it: counted (the depth touched before the
 * step; RD_SC_CHECK_TRIES of them refuse the stencil step for the run) or
 * not (no first level step in the picture) */
void rd__shadow_count_check_lost(const char *why, bool counted);
void rd__shadow_count_check_refuse(const char *why);
void rd__shadow_count_check_recorded(RhiBuffer rb, const RhiTexture tex[RD_SC_CHECK_TEXTURES],
                                     uint32_t w, uint32_t h, uint32_t pitch);
void rd__shadow_count_collect(uint32_t slot);
/* ICO_RD_SHADOW_COUNT_SABOTAGE=1 (tests): the step from the stencil reads
 * an empty box (every texel 0), so the check refuses it */
bool rd__shadow_count_sabotaged(void);

/* ---- the renderer's */
/* rd_perf.c: the replay being recorded is timed pass by pass (its pieces
 * come back to rd__shadow_count_trial RHI_FRAMES_IN_FLIGHT replays later) */
bool rd__perf_pass_on(void);
/* the line at the renderer's start and the attempt ledger's state */
void rd__shadow_count_log(void);
/* the form of this replay's count above 1x (rd_shadow_one.c, once a count;
 * a second count in one replay takes the first's) */
int rd__shadow_count_form(void);
/* the guard drew the full-size pass after all, or refused the chain
 * (RD_SC_WHY_LIST): counted, a line once a reason; a trial replay's
 * sample is then not the stencil form's and is dropped */
void rd__shadow_count_guard(int why);
/* rd_perf.c, a timed replay's pieces read back: its list 3 time to the
 * trial when it was one of the trial's replays */
void rd__shadow_count_trial(uint32_t replay, const RdPerfPass *p, uint32_t n);
/* the renderer's shutdown: the choice made again on the next device */
void rd__shadow_count_reset(void);
/* rd_replay_tool --fine-check: the full-size count also drawn after a step
 * from the stencil, so the check reads the count the step stood for */
void rd__shadow_count_set_keep(bool on);
bool rd__shadow_count_keep(void);
/* ICO_RD_SHADOW_COUNT_TOUCH=1 (tests only): the depth touched once before
 * each level step from the stencil, so the guard draws the pass after all */
bool rd__shadow_count_touch_test(void);
/* --stats' words: "shadow count above 1x: S from the stencil, F full-size
 * passes, V seven draws, G guard fallbacks (form stencil)" */
void rd__shadow_count_stats(const RdPerfRecord *rec, char *out, size_t size);

#endif
