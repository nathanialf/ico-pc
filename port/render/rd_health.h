/* rd_health.h: the picture-health guard (rd_health.c the pure rule and the
 * mark, rd_health_live.c the renderer's part; port/platform/window_health.c
 * the window's; docs/port/ANDROID_DIAGNOSTICS.md "The picture-health
 * guard"; issue 179).
 *
 * v0.11.6 drew a black picture on the Mali-G52 MC2 of issue 179's reporter
 * (driver v1.r32p1): from the first 3D picture on, every picture's GPU work
 * stopped after "list 0's start" or "the VU pre-pass's start" (16 of 19 of
 * its recorded stamps came back unwritten after its work was signalled
 * finished, 410 such pictures in a 26 s run and 6800 in a 5 minute one,
 * from replay 149 on without a break), its timestamp counter restarted
 * (8 and over 1200 times), and v0.11.5 on the same phone had none of
 * either.  Which of v0.11.6's changes did it is not known (lavapipe draws
 * the reporter's loss dumps the same with each of them on and off), so the
 * game watches its own pictures and steps the newer GPU work off by itself.
 *
 * The rule (rd_health_pic, rd_health_take), on Vulkan (the slot clock's
 * stamps: rd_perf_clock.h): a judged picture is a presented one with VU
 * draws (3D; the start-up self-tests present nothing, 2D pictures are not
 * judged), from RD_HEALTH_SETTLE replays after the last step on.  It is
 * unfinished when its record carries the GPU's own sign of a reset
 * (rd_perf_clock.h rd_perf_reset_counted: recorded stamps unwritten after
 * its work was signalled finished, its first list run twice, or a counter
 * restart with work queued; a restart after a span with no GPU work, the
 * app away, a pause or a capture, is not one).  RD_HEALTH_TRIGGER of the
 * last RD_HEALTH_WINDOW judged pictures unfinished takes the next group
 * off; after a step, RD_HEALTH_CLEAN judged pictures in a row whose window
 * holds at most RD_HEALTH_CLEAN_FAILS unfinished confirm it ("pictures
 * complete again after turning off ...": that group is the culprit).
 * Checked against the four logs (the hand-back has the numbers): the
 * Mali-G52's two v0.11.6 runs are unfinished at every 3D picture from
 * replay 149 (the 16th at replay 164, about 0.6 s of 3D at 25 fps); the
 * same phone's v0.11.5 run has no unwritten stamp and no restart; the
 * Adreno 740's v0.11.5 and v0.11.6 runs have 12 and 13 isolated hangs in
 * about 5 minutes (at most 3 within 6 replays) and at most 4 restarts in a
 * 10 s block: never 16 of 32.
 *
 * The groups, from the likeliest cause to the least (the issue's order),
 * each a set of parts; a part is switched in the run (live) or from the
 * next start (its shaders or the device are made at the start):
 *   (a) the index compaction (live)
 *   (b) the VU record sized from need: the record grown to its largest, as
 *       v0.11.5 made it (live; the next start makes it so)
 *   (c) one DATE set-up (live); flat outputs at the provoking corner (the
 *       next start: the twins vu_all_main_vs and vu_merge_all_main_vs)
 *   (d) the shadow volumes' stencil-only entry, the shadow composites' own
 *       entry, the glow paste's own entry (live)
 *   (e) empty composites skipped (live); robust access on again, as the
 *       reporter's v0.11.5 ran it ([dev] gpu_robust, removed by v0.11.6:
 *       the next start; never written to the settings)
 * A group none of whose parts is on here is passed over.  When the trigger
 * finds no group left: "stepping everything off did not recover" once,
 * and no more steps in the run (no loop).
 *
 * The mark ([dev] picture_health and picture_health_by in config.toml,
 * written by the game alone, as gpu_robust_by and vu_bones_by are; removed
 * after an update): the groups off ("bce") and
 *   "picture health <version>; <state>; <evidence>"
 * with the state one of
 *   stepping           groups taken, no verdict yet
 *   culprit X          pictures complete again after turning X off
 *   pending X          X's next-start parts not seen yet: the next start
 *                      confirms them (complete from the start: culprit X)
 *   exhausted          every group off and pictures still unfinished
 * The next start begins with every part of the groups off and goes on from
 * the next group.
 *
 * The tests' fake ICO_RD_HEALTH_FAKE (docs/port/TESTING.md; no setting):
 * hang or restart: every judged picture read with that sign (the guard
 * steps through every group); idle: read as an idle restart (never
 * counted); a, b, c, d or e: read as a hang until that group is off, then
 * as it is (the guard recovers naming it); selftest: the start-up
 * self-test's pictures read as hangs (the index compaction's self-test
 * fails). */
#ifndef PORT_RENDER_RD_HEALTH_H
#define PORT_RENDER_RD_HEALTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RD_HEALTH_WINDOW 32u
#define RD_HEALTH_TRIGGER 16u
#define RD_HEALTH_CLEAN 64u
#define RD_HEALTH_CLEAN_FAILS 4u
/* replays after the one whose collection took a step: the replay in
   flight then was recorded before it (RHI_FRAMES_IN_FLIGHT 2), one more
   for margin */
#define RD_HEALTH_SETTLE 3u

/* the parts */
enum {
    RD_HEALTH_P_COMPACT = 0,
    RD_HEALTH_P_REC,
    RD_HEALTH_P_DATE_ONE,
    RD_HEALTH_P_FLAT0,
    RD_HEALTH_P_VOLUME_FS,
    RD_HEALTH_P_COMP_SPEC,
    RD_HEALTH_P_PASTE_SPEC,
    RD_HEALTH_P_EMPTY_SKIP,
    RD_HEALTH_P_ROBUST,
    RD_HEALTH_PARTS
};

/* the groups (a) to (e) */
#define RD_HEALTH_GROUPS 5

/* a group's parts (bits of RD_HEALTH_P_*), 0 out of range */
uint32_t rd_health_group_parts(int g);
/* the parts switched in the run (the rest from the next start) */
uint32_t rd_health_live_parts(void);
/* the group's letter ('a' .. 'e'), '?' out of range */
char rd_health_group_letter(int g);
/* a part's words ("the index compaction"), "?" out of range */
const char *rd_health_part_name(int p);
/* the groups holding any of parts (bits of groups) */
uint32_t rd_health_groups_of(uint32_t parts);
/* every part of the groups (bits) */
uint32_t rd_health_parts_of(uint32_t groups);
/* groups as letters ("bce"; "" none) into out (at least 6 bytes) */
void rd_health_letters(char *out, size_t size, uint32_t groups);
/* "(b) the VU record sized from need and (c) one DATE set-up" (parts, by
   group) into out; the length */
size_t rd_health_parts_text(char *out, size_t size, uint32_t parts);

/* ------------------------------------------------------------ the rule */
enum { RD_HEALTH_WATCH = 0, RD_HEALTH_AFTER, RD_HEALTH_DONE };

enum {
    RD_HEALTH_NONE = 0,
    RD_HEALTH_TRIGGER_HIT, /* rd_health_pic: the window says unfinished (rd_health_take next) */
    RD_HEALTH_STEP,        /* rd_health_take: groups off, live parts among them */
    RD_HEALTH_RECOVERED,   /* rd_health_pic: the last step confirmed */
    RD_HEALTH_EXHAUSTED    /* rd_health_take: nothing left to step in the run */
};

typedef struct RdHealthPic {
    uint32_t replay; /* the record's number (RdPerfRecord.replay) */
    bool judged;     /* presented with VU draws */
    int sign;        /* RdPerfRecord.gpuResetSign (rd_perf_clock.h RD_RESET_*) */
} RdHealthPic;

typedef struct RdHealth {
    int phase;
    int nextGroup;          /* the next group to try */
    uint32_t groups;        /* the groups taken (this start's mark's and the run's) */
    uint32_t off;           /* the parts off (taken; a next-start part from the next start) */
    uint32_t pendingGroups; /* groups whose next-start parts this run took */
    uint32_t lastGroups;    /* the group(s) the next confirmation names */
    bool fromStart;         /* the confirmation is of the start's mark */
    uint32_t culprit;       /* groups, 0 none confirmed */
    uint32_t settleFrom;    /* the first replay judged */
    uint32_t win, winN;     /* the last RD_HEALTH_WINDOW judged pictures, 1 unfinished */
    uint32_t clean;         /* judged pictures in a row with the window clean */
    uint32_t steps;         /* steps taken in the run */
} RdHealth;

typedef struct RdHealthAct {
    int kind;
    uint32_t fails, judged; /* the window's unfinished pictures of judged */
    uint32_t groups;        /* STEP / EXHAUSTED: the groups taken now */
    uint32_t live, next;    /* their parts switched now and from the next start */
    uint32_t culprit;       /* RECOVERED: the groups confirmed */
    bool fromStart;         /* RECOVERED: of the start's mark */
    uint32_t
        pendingGroups; /* EXHAUSTED: groups whose next-start parts are off from the next start */
} RdHealthAct;

void rd_health_reset(RdHealth *h);
/* one collected record: NONE, TRIGGER_HIT (call rd_health_take) or
   RECOVERED (out says what) */
int rd_health_pic(RdHealth *h, const RdHealthPic *p, RdHealthAct *out);
/* after TRIGGER_HIT: the next group with a part on here (applicable: the
   parts on now), and the groups after it while they have only next-start
   parts; STEP (live parts off now: settleFrom is the first replay judged
   after it) or EXHAUSTED (none left: no more steps) */
void rd_health_take(RdHealth *h, uint32_t applicable, uint32_t settleFrom, RdHealthAct *out);

/* ------------------------------------------------------------ the mark */
enum {
    RD_HEALTH_MARK_STEPPING = 0,
    RD_HEALTH_MARK_CULPRIT,
    RD_HEALTH_MARK_PENDING,
    RD_HEALTH_MARK_EXHAUSTED
};

typedef struct RdHealthMark {
    char version[48];
    uint32_t groups; /* the groups off */
    int state;
    uint32_t named; /* CULPRIT, PENDING: the groups named */
    char evidence[160];
} RdHealthMark;

/* the two keys' values; 0, or -1 when a buffer is too small */
int rd_health_mark_format(char *value, size_t valueSize, char *by, size_t bySize,
                          const RdHealthMark *m);
/* the keys' values (NULL or "" absent) into m; 0, -1 not the game's form
   (no group, a letter past e, no "picture health " mark, an unknown state) */
int rd_health_mark_parse(const char *value, const char *by, RdHealthMark *m);

/* what a start does with them: RD_HEALTH_START_NONE (absent),
   _KEEP (this version's: the start begins there), _DROP_VERSION (another
   version's: removed), _DROP_FORM (not the game's form: removed) */
enum {
    RD_HEALTH_START_NONE = 0,
    RD_HEALTH_START_KEEP,
    RD_HEALTH_START_DROP_VERSION,
    RD_HEALTH_START_DROP_FORM
};

int rd_health_mark_start(const char *value, const char *by, const char *version, RdHealthMark *m);
/* the rule's state at a start from a kept mark (NULL: none) */
void rd_health_begin(RdHealth *h, const RdHealthMark *m);
/* the mark of the state now (version, evidence given) */
void rd_health_mark_of(const RdHealth *h, const char *version, const char *evidence,
                       RdHealthMark *m);

/* ---------------------------------------------- the window's (rd_health_live.c) */
/* before rd_init (window_health.c): the start's kept mark (NULL none) and
   the game's version the marks carry ("dev" when NULL or "") */
void rd_health_set_start(const RdHealthMark *m, const char *version);
/* the mark to write when the guard's state changed since *seq (window's
   pump): true with both values; *seq moved */
bool rd_health_persist_take(uint32_t *seq, char *value, size_t valueSize, char *by, size_t bySize);

/* ---------------------------------------------- the renderer's own */
struct RdPerfRecord;
/* at every renderer's start, after its modules (rd_lamps.c): the run's
   guard begun once (its line), the parts it holds off said again to the
   ledger */
void rd__health_init(void);
/* a collected record (rd_perf.c rd__perf_collect_gpu, at a replay's start
   before its recording): judged, and a step taken there */
void rd__health_note(const struct RdPerfRecord *r);
/* the part is off (by the guard, this run or the start's mark) */
bool rd__health_part_off(int part);
/* the start-up self-test's window (rd_vuprepass_selftest.c, the index
   compaction's run): every record collected inside it, presented or not;
   end returns those with the GPU's own sign of a reset (*n: collected) */
void rd__health_selftest_begin(void);
uint32_t rd__health_selftest_end(uint32_t *n);

#endif /* PORT_RENDER_RD_HEALTH_H */
