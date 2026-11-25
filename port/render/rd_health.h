/* rd_health.h: the picture-health report (rd_health.c the pure rule,
 * rd_health_live.c the renderer's part; docs/port/ANDROID_DIAGNOSTICS.md
 * "The picture-health report"; issue 179).  It reports only: nothing is
 * switched off by it, but every collected record with VU draws outside the
 * self-tests is handed to the index compaction's Mali-G52 form
 * (rd_vu_compact.h rd__vu_compact_play_health), which turns itself off at
 * the first one with stamps unwritten.
 *
 * v0.11.6 drew a black picture on the Mali-G52 MC2 of issue 179's reporter
 * (driver v1.r32p1): from the first 3D picture on, every picture's GPU work
 * stopped after "list 0's start" or "the VU pre-pass's start" (16 of 19 of
 * its recorded stamps came back unwritten after its work was signalled
 * finished, from replay 149 on without a break in both of its logs), its
 * timestamp counter restarted, and the fences still signalled.  The log said
 * so only in the GPU clock lines, which are limited after the first 32.  The
 * report counts such pictures in every block and says once, in a line the
 * bug report's key lines carry, when they are most of them.
 *
 * The rule (rd_health_pic): a judged picture is a presented one with VU draws
 * (3D; the start-up self-tests present nothing).  It is unfinished when its
 * record carries the GPU's own sign of a reset (rd_perf_clock.h
 * rd_perf_reset_counted: recorded stamps unwritten after its work was
 * signalled finished, its first list run twice, or a counter restart with
 * work queued; a restart after a span with no GPU work, the app away, a pause
 * or a capture, is not one).  RD_HEALTH_TRIGGER of the last RD_HEALTH_WINDOW
 * judged pictures unfinished meets it, once a run.  Checked against the four
 * logs: the Mali-G52's two v0.11.6 runs meet it at replay 164 (the 16th
 * unfinished 3D picture, about 0.6 s of 3D at 25 fps); the same phone's
 * v0.11.5 run has no unwritten stamp and no restart; the Adreno 740's
 * v0.11.5 and v0.11.6 runs have 12 and 13 isolated hangs in about 5 minutes
 * (at most 3 within 6 replays) and at most 4 restarts in a 10 s block: never
 * 16 of 32.
 *
 * The tests' fake ICO_RD_HEALTH_FAKE=selftest (docs/port/TESTING.md; no
 * setting): every record collected inside the index compaction's start-up
 * self-test read as a hang (the self-test then fails); =play every record
 * with VU draws outside the self-tests (the compaction's form then turns
 * off in play). */
#ifndef PORT_RENDER_RD_HEALTH_H
#define PORT_RENDER_RD_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#define RD_HEALTH_WINDOW 32u
#define RD_HEALTH_TRIGGER 16u

typedef struct RdHealth {
    uint32_t win, winN; /* the last RD_HEALTH_WINDOW judged pictures, 1 unfinished */
    bool met;           /* the rule was met (said once) */
} RdHealth;

void rd_health_reset(RdHealth *h);
/* one collected record: judged (presented with VU draws) and its sign
   (RdPerfRecord.gpuResetSign, rd_perf_clock.h RD_RESET_*); true the first
   time RD_HEALTH_TRIGGER of the last RD_HEALTH_WINDOW judged pictures are
   unfinished (*fails, *judgedN: the window's then; either may be NULL),
   false otherwise and every time after */
bool rd_health_pic(RdHealth *h, bool judged, int sign, uint32_t *fails, uint32_t *judgedN);

/* ---------------------------------------------- the renderer's own */
struct RdPerfRecord;
/* a collected record (rd_perf.c rd__perf_collect_gpu): counted in the
   attempt ledger's row, the line once when the rule is met */
void rd__health_note(const struct RdPerfRecord *r);
/* the start-up self-test's window (rd_vuprepass_selftest.c, the index
   compaction's run): every record collected inside it, presented or not;
   end returns those with the GPU's own sign of a reset (*n: collected) */
void rd__health_selftest_begin(void);
uint32_t rd__health_selftest_end(uint32_t *n);

#endif /* PORT_RENDER_RD_HEALTH_H */
