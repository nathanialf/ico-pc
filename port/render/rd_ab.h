/* rd_ab.h: the in-play A/B judge (issue 179; docs/port/PIPELINES.md, "The
 * zero-area cull timed in play").
 *
 * An attempt that is exact by construction (the same picture on both arms)
 * but whose cost depends on the device is timed while the game is played,
 * and kept on a stage only where it is faster there.  Each attempt is a
 * subject (rd_ab_subject: the zero-area cull first; the registry takes
 * more), with two arms: on (today's path) and off (the path without it).
 *
 * The judge, pure (no GPU, no clock, no renderer state: fed by
 * port/render/rd_ab_run.c, tested by port/render/test/rd_ab_test.c):
 *   - each replay asks its arms (rd_ab_begin) with its stage, the time and
 *     whether it is a keep frame; its scene's GPU work (rd_ab_scene_ms), its work (the pre-pass's
 *     vertices and the VU triangles) and whether each subject changed
 *     anything in it come back when its GPU times are read (rd_ab_collect),
 *     with the tag it was recorded under, so a span is always the arm's it
 *     was recorded with however many pictures are in flight;
 *   - a trial starts RD_AB_START_MS into a stage, for one subject at a
 *     time that may be tried (rd_ab_allow), changed something in that
 *     stage's pictures and has no verdict there; at most RD_AB_TRIALS a
 *     run;
 *   - the trial's replays (keep frames left out) alternate on, off, off,
 *     on (ABBA, which cancels a drift of the content); replays 2k and 2k+1
 *     of the trial, both collected, both with GPU times, both with the
 *     subject in use, are a pair, compared when their work is within
 *     RD_AB_WORK_TOL of each other and neither span is over RD_AB_LONG
 *     times the trial's running median span (dropped and counted
 *     otherwise; the median from at least RD_AB_MEDIAN_MIN spans, the
 *     stage's own before the trial included);
 *   - a pair's measure: ln((span on / work on) / (span off / work off));
 *   - the verdict: at RD_AB_EARLY pairs, all of them one way and the
 *     median at least RD_AB_GAIN that way; else at RD_AB_PAIRS pairs: on
 *     when the median is RD_AB_GAIN or more in favour of the attempt and
 *     RD_AB_FAVOUR of them favour it, off otherwise (no difference: off,
 *     the path without it); kept for the stage for the run;
 *   - a stage without a verdict of its own (not judged yet, or judged
 *     nothing) runs on the arm of the run's most recent verdict for the
 *     subject (none yet in the run: on), its trial still 2 s in, and its
 *     own verdict replaces the carried arm there and from then on;
 *   - no verdict: fewer than RD_AB_PAIRS_MIN pairs after RD_AB_GIVE_UP
 *     trial replays (no more trials there, the carried arm kept), the
 *     stage ended or the
 *     subject was turned off before (not judged; tried again at the next
 *     visit while trials are left); between RD_AB_PAIRS_MIN and
 *     RD_AB_PAIRS pairs at either end, the verdict on those pairs with
 *     RD_AB_FAVOUR scaled;
 *   - rd_ab_stop (a device loss): every trial stopped, every subject at
 *     its off arm for the rest of the run.
 * The lines (rd_ab_take_line; "rd: " added by the renderer):
 *   A/B: zero-area cull, stage 42: 32 pairs, with it 1.62 ms, without 1.55
 *     ms of the scene's GPU work (the present and its display wait left
 *     out) a thousand vertices and triangles (the median pair 4.3 % dearer
 *     with it, 25 of 32 cheaper without), 3 dropped by a long picture, 0
 *     by unequal work: off for this stage (it started with it, the arm
 *     stage 3's verdict left)
 *   A/B: zero-area cull, stage 42: no verdict (600 replays, 11 pairs; 30
 *     dropped by a long picture, 4 by unequal work, 120 replays not
 *     measured): no more trials here, it stays without it, the arm stage
 *     41's verdict left
 *   A/B: every trial stopped for the run (the graphics device was lost):
 *     each attempt at its off arm from here (zero-area cull)
 *   A/B: 12 trials run, the most a run: a stage not judged keeps the arm
 *     of the run's last verdict
 * The modes (ICO_RD_AB, the tests' switch): unset the judge; on every
 * replay at the on arm (today's path); off every replay at the off arm;
 * alt every replay alternating on, off, off, on from the first, no trial,
 * no verdict, no stage needed (the replay tool's A/B, rd_ab_zero_ab). */
#ifndef PORT_RENDER_RD_AB_H
#define PORT_RENDER_RD_AB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The constants, all here (the owner's to review; the hand-back and
   PIPELINES.md say where each comes from) */
#define RD_AB_START_MS 2000.0 /* a trial's start into a stage (past its fade and first pictures) */
#define RD_AB_TRIALS 12       /* trials a run at most */
#define RD_AB_WORK_TOL 0.10   /* a pair's work within 10 % of the larger */
#define RD_AB_LONG 2.0        /* a pair with a span over twice the running median: dropped */
#define RD_AB_MEDIAN_MIN 8u   /* spans before a running median is trusted */
#define RD_AB_HIST 64u        /* the running median's last spans */
#define RD_AB_EARLY 8u        /* the early verdict's pairs (all one way) */
#define RD_AB_PAIRS 32u       /* the verdict's pairs */
#define RD_AB_FAVOUR 22u      /* of RD_AB_PAIRS, those that must favour the attempt */
#define RD_AB_GAIN 0.02       /* the median's least gain (ln: about 2 %) */
#define RD_AB_GIVE_UP 600u    /* a trial's replays at most */
#define RD_AB_PAIRS_MIN 16u   /* fewer pairs than this at a trial's end: no verdict */

#define RD_AB_SUBJECTS 4 /* subjects at most (a bit each in a tag's mask) */
#define RD_AB_MEMORY 32  /* verdicts kept (stage, subject) */
#define RD_AB_LINES 6    /* lines waiting for rd_ab_take_line */
#define RD_AB_LINE 640   /* a line's bytes */

/* the modes (ICO_RD_AB) */
enum { RD_AB_AUTO = 0, RD_AB_FIXED_ON, RD_AB_FIXED_OFF, RD_AB_ALT };

/* a verdict for a stage */
enum { RD_AB_V_NONE = 0, RD_AB_V_ON, RD_AB_V_OFF, RD_AB_V_KEPT };

/* what a replay was recorded under (kept in its record) */
typedef struct RdAbTag {
    uint32_t trial; /* the trial's serial, 0 none (not a trial's replay) */
    uint32_t seq;   /* its place in the trial (0, 1, ...) */
    int32_t stage;  /* its stage (-1 not known) */
    uint8_t off;    /* the subjects at their off arm (a bit a subject) */
    uint16_t epoch; /* the renderer it was drawn by (rd_ab_forget moves it) */
} RdAbTag;

/* a replay's GPU times, read back */
typedef struct RdAbIn {
    RdAbTag tag;
    uint8_t used; /* the subjects that changed anything in it (a bit each) */
    bool keep;    /* a keep frame */
    bool valid;   /* its GPU span measured */
    double work;  /* the pre-pass's vertices and the VU triangles */
    double gpuMs; /* its GPU work: the scene's alone (rd_ab_scene_ms) */
} RdAbIn;

typedef struct RdAbTrial {
    bool active;
    int subject;
    int32_t stage;
    uint32_t serial, seq;
    /* the replay waiting for its pair (even seq) */
    bool havePend, pendOff;
    uint32_t pendSeq;
    double pendWork, pendMs;
    double hist[RD_AB_HIST];
    uint32_t histN, histAt;
    double r[RD_AB_PAIRS], mOn[RD_AB_PAIRS], mOff[RD_AB_PAIRS];
    uint32_t pairs, dropLong, dropWork, notMeasured, offReplays;
} RdAbTrial;

typedef struct RdAbSubject {
    char name[48];
    bool allowed;
    uint32_t stagesOn, stagesOff, trials; /* verdicts and trials this run */
    uint64_t armOn, armOff;               /* replays at each arm while allowed */
    /* the run's last verdict (RD_AB_V_ON, RD_AB_V_OFF; RD_AB_V_NONE none
       yet) and its stage: the arm a stage without a verdict of its own
       starts on and keeps until its own */
    int8_t carry;
    int32_t carryStage;
} RdAbSubject;

typedef struct RdAb {
    int mode;
    bool stopped;
    char stopWhy[160];
    RdAbSubject subj[RD_AB_SUBJECTS];
    int nSubj;
    int32_t stage;   /* the stage of the replays now (-1 none yet) */
    double stageMs;  /* its first replay's time */
    uint8_t usedNow; /* the subjects seen in use in it */
    double hist[RD_AB_HIST];
    uint32_t histN, histAt; /* the stage's spans outside a trial */

    struct {
        int32_t stage;
        int8_t subject;
        uint8_t verdict;
    } mem[RD_AB_MEMORY];

    int nMem;
    RdAbTrial t;
    uint32_t serial, trials;
    uint16_t epoch; /* rd_ab_forget's count: a record of an earlier renderer is not measured */
    bool capSaid;
    uint32_t altSeq;
    uint64_t offTrial; /* off-arm replays the trials made, the run */
    char lines[RD_AB_LINES][RD_AB_LINE];
    int lineHead, lineN;
} RdAb;

/* the judge empty, in a mode (RD_AB_AUTO ...) */
void rd_ab_init(RdAb *a, int mode);
/* the mode of ICO_RD_AB's value ("on", "off", "alt"; NULL or another
   word: RD_AB_AUTO) and its words */
int rd_ab_mode_of(const char *env);
const char *rd_ab_mode_name(int mode);
/* a subject registered ("zero-area cull"): its id (0, 1, ...), -1 when
   the registry is full; may be tried once allowed */
int rd_ab_subject(RdAb *a, const char *name);
/* whether subject id may be tried now (its own self-test passed, not off
   for the run); a trial of a subject no longer allowed ends unjudged */
void rd_ab_allow(RdAb *a, int id, bool allowed);
/* a replay begins: its arms (tag.off) and its tag, for stage at nowMs */
RdAbTag rd_ab_begin(RdAb *a, int32_t stage, double nowMs, bool keep);
/* a replay's GPU times, read back under the tag it was begun with */
void rd_ab_collect(RdAb *a, const RdAbIn *in);
/* the renderer was made again (a Graphics API switch, the device made
   again): a trial running ends unjudged, every verdict and the carried arm
   are forgotten (each subject back on its default, with it), the stage
   starts again (its trial 2 s from its next replay), and the records the
   earlier renderer drew are not measured; the trials already run still
   count toward RD_AB_TRIALS, a stop stays; its line (why) */
void rd_ab_forget(RdAb *a, const char *why);
/* the GPU work a replay's measure takes: its span less the present's own
   pass and the display wait before it (rd.h RdPerfRecord gpuPresentMs,
   gpuDisplayWaitMs: on a Mali the wait for the window's image shows in the
   present pass, so neither is work the subject can change), 0 when nothing
   is left */
double rd_ab_scene_ms(double spanMs, double displayWaitMs, double presentMs);
/* every trial stopped, every subject at its off arm for the run (why) */
void rd_ab_stop(RdAb *a, const char *why);
/* subject id's verdict for stage (RD_AB_V_*) */
int rd_ab_verdict(const RdAb *a, int id, int32_t stage);
/* the next line waiting, false none */
bool rd_ab_take_line(RdAb *a, char *out, size_t cap);
/* "A/B: the in-play judge on: ... may try: zero-area cull; ..." (the
   start line: the mode, the subjects, the constants); the length */
size_t rd_ab_start_text(const RdAb *a, char *out, size_t cap);
/* the run's replays at each arm by subject ("A/B (alt): zero-area cull: N
   replays with it, M without"; the replay tool's stats); the length */
size_t rd_ab_stats_text(const RdAb *a, char *out, size_t cap);

#endif
