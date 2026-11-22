/* rd_ab_run.h: the in-play A/B judge in the renderer (rd_ab.h the judge;
 * docs/port/PIPELINES.md, "The zero-area cull timed in play").
 *
 * The judge lives for the run, but a renderer made again (a Graphics API
 * switch, the device made again: rd__ab_renderer from rd__vu_prepass_init)
 * forgets every verdict and the carried arm and ends a trial unjudged, so
 * no verdict of one backend or device decides another's (the trials run
 * still count toward the cap).  The measure is the scene's GPU work: the
 * span less the present's pass and the display wait (rd_ab_scene_ms).
 * A replay's arms are chosen at its record's start (rd__perf_begin:
 * rd__ab_begin) and kept in its record (RdPerfRecord abTrial, abSeq,
 * abStage, abOff), so its GPU span, read back RHI_FRAMES_IN_FLIGHT replays
 * later (rd__perf_collect_gpu: rd__ab_collect), is the arm's it was
 * recorded with.  A subject reads its arm while the replay is made
 * (rd__ab_off) and says when it changed anything (rd__ab_used).  The
 * stage comes from the scene ledger's stage switch (rd_ledger_live.c
 * rd_ledger_scene: rd__ab_scene), the first frame of the new stage with it.
 *
 * The ways back: a device lost at a replay's start stops every trial and
 * leaves each subject at its off arm for the run (logged; the tests fake
 * one with ICO_RD_AB_FAKE_LOSS=N, the loss found at replay N's start).
 * A subject's own self-test, mark and run-time checks still decide whether
 * it is used at all: one that is off is never put on trial (rd_ab_allow).
 * The attempt ledger's rows (attempt_ledger.h ICO_ATT_AB_ZERO,
 * ICO_ATT_AB_NODISCARD). */
#ifndef PORT_RENDER_RD_AB_RUN_H
#define PORT_RENDER_RD_AB_RUN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the subjects, in their registration order (rd_ab_run.c) */
enum {
    RD_AB_ZERO = 0,     /* the zero-area cull (rd_vu_zero.h) */
    RD_AB_NODISCARD = 1 /* the discard-free twins (rd_nodiscard.h) */
};

struct RdPerfRecord;

/* the scene ledger's stage switch (any thread): stage from frame first on */
void rd__ab_scene(int stage, uint32_t first);
/* a replay's record begins (rd__perf_begin, r its record with replay,
   frame and keep set): its arms into r */
void rd__ab_begin(struct RdPerfRecord *r);
/* a renderer made (rd__vu_prepass_init): from the second on, everything
   the judge learnt forgotten (rd_ab_forget), logged */
void rd__ab_renderer(void);
/* the replay's record ends (rd__perf_end): no arm read after it */
void rd__ab_end(void);
/* a record's GPU times read back (rd__perf_collect_gpu) */
void rd__ab_collect(const struct RdPerfRecord *r);
/* subject's arm in the replay being made: true at its off arm (false
   outside a replay) */
bool rd__ab_off(int subject);
/* subject changed something in the replay being made */
void rd__ab_used(int subject);
/* the replay being made is not one to time subject on after all (the
   discard-free twins: a pass found its twin not made yet) */
void rd__ab_unused(int subject);
/* the judge's start line (after the self-tests: rd__vu_zero_start_line)
   and the attempt ledger's row */
void rd__ab_start_line(void);
/* the run's arms ("A/B (alt): zero-area cull: N replays with it, M
   without ..."; rd_replay_tool --stats); the length */
size_t rd__ab_stats_text(char *out, size_t cap);

#endif
