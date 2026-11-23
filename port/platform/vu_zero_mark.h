/*
 * port/platform/vu_zero_mark.h
 *
 * The zero-area cull's mark on the install (issue 179; port/render/
 * rd_vu_zero.h): logs/vu-zero-mark.txt beside the activity file, one stage
 * word and the version that was running when the renderer wrote it
 * ("trying v0.11.14"), read at the next start.
 *
 *   trying <v>    written and synced before the cull's one module is made,
 *                 until its start-up self-test has passed
 *   counting <v>  written and synced when the self-test passed: the cull on,
 *                 its first RD_VU_ZERO_PROBATION pictures not yet drawn
 *   on <v>        RD_VU_ZERO_PROBATION pictures drawn with it (not synced: a
 *                 lost "on" leaves "counting", judged the same way)
 *   failed <v>    the module could not be made, or the self-test or a check
 *                 refused it (synced)
 *   blocked <v>   written here: the cull stays off on this install while
 *                 version v runs
 *
 * The next start (vu_zero_mark_begin, diag_host.c right after
 * last_run_begin, which kept the previous run's last-run.txt as
 * last-run-previous.txt) classes the previous run's end from what the
 * "previous run" lines already read: last-run.txt's end (clean, a crash
 * from the crash handler's last words, a device loss, a GPU hang, the
 * watchdog; "clean, after: <end>" is that end) and, where it says nothing
 * (no end written), Android's exit record with that run's pid
 * (exit_info.h exit_info_last_reason).  The stage table
 * (vu_zero_mark_blocks):
 *
 *   trying        every end but a clean close blocks (a hang in the
 *                 driver's compiler the player swipes away, a kill, an end
 *                 with no evidence at all)
 *   counting, on  only an end the evidence calls bad blocks: a crash (ours
 *                 or Android's REASON_CRASH / REASON_CRASH_NATIVE), an ANR,
 *                 a signal other than SIGKILL, a device loss, a GPU hang, the
 *                 watchdog; a close from the recent apps list, Force stop, a
 *                 kill by the system (low memory, background, SIGKILL), an
 *                 update, the app ending itself, a clean close or no
 *                 evidence do not: the mark is removed and the cull runs its
 *                 self-test again at this start
 *   failed        blocks
 *   blocked       blocks
 *
 * Each mark blocks only its own subject: an end while the other attempt's
 * mark said trying (the discard-free twins' logs/nodiscard-mark.txt, read
 * here before either is rewritten) is that attempt's, so it does not block
 * this one at counting or on (vu_zero_mark_blocks_beside).
 *
 * A block holds only while the version in the mark is the running one: an
 * update clears it.  A mark without a version (none was handed over) is
 * taken as the running version's.  The line ("the previous run:
 * zero-area cull: ...") is logged when a mark was found; ICO_VU_ZERO_MARK
 * (the file's path), ICO_VU_ZERO_VERSION (the version to write) and, when
 * the cull is kept off, ICO_VU_ZERO_BLOCK (its words) go to the
 * environment for the renderer.  Nothing here is a setting; the file is
 * never in a config.
 */
#ifndef ICO_PLATFORM_VU_ZERO_MARK_H
#define ICO_PLATFORM_VU_ZERO_MARK_H

#include <stddef.h>

/* the mark's stage */
enum {
    VU_ZERO_STAGE_NONE, /* no mark */
    VU_ZERO_STAGE_TRYING,
    VU_ZERO_STAGE_COUNTING,
    VU_ZERO_STAGE_ON,
    VU_ZERO_STAGE_FAILED,
    VU_ZERO_STAGE_BLOCKED,
    VU_ZERO_STAGE_UNREAD, /* a text this program does not know */
    VU_ZERO_STAGES
};

/* how the previous run ended, from the evidence (vu_zero_mark_end_kind) */
enum {
    VU_ZERO_END_CLEAN,    /* last-run.txt: clean */
    VU_ZERO_END_CRASH,    /* last-run.txt: the crash handler's last words */
    VU_ZERO_END_LOSS,     /* last-run.txt: the graphics device lost */
    VU_ZERO_END_HANG,     /* last-run.txt: a GPU hang */
    VU_ZERO_END_WATCHDOG, /* last-run.txt: the watchdog */
    VU_ZERO_END_A_CRASH,  /* Android: REASON_CRASH, REASON_CRASH_NATIVE */
    VU_ZERO_END_A_ANR,    /* Android: REASON_ANR */
    VU_ZERO_END_A_SIGNAL, /* Android: REASON_SIGNALED, a signal other than SIGKILL */
    VU_ZERO_END_A_KILLED, /* Android: REASON_SIGNALED with SIGKILL (the system's or a user's
                             kill: a low-memory kill shows so where the device does not report
                             those as such) */
    VU_ZERO_END_A_USER,   /* Android: REASON_USER_REQUESTED, REASON_USER_STOPPED (the recent
                             apps list, Force stop) */
    VU_ZERO_END_A_SYSTEM, /* Android: low memory, excessive use, the freezer, a permission or
                             a dependency, initialisation, other */
    VU_ZERO_END_A_UPDATE, /* Android: REASON_PACKAGE_UPDATED, REASON_PACKAGE_STATE_CHANGE */
    VU_ZERO_END_A_SELF,   /* Android: REASON_EXIT_SELF */
    VU_ZERO_END_NONE,     /* no evidence: no end written and no record (old Android, a desktop
                             kill -9, the record missing or unreadable) */
    VU_ZERO_ENDS
};

/* The end kind from last-run.txt's end (LAST_RUN_END_*, -1 not read), its
   text (for "clean, after: <end>"; NULL none) and Android's record for that
   run (EXIT_INFO_REASON_*, -1 none; status its signal or exit code) */
int vu_zero_mark_end_kind(int lastEnd, const char *lastEndText, int exitReason, int exitStatus);
/* 1 when the end blocks the cull at that stage (the table above; the
   version is checked apart) */
int vu_zero_mark_blocks(int stage, int endKind);
/* vu_zero_mark_blocks with the other attempt's stage at that end (the
   cull's for the twins' mark, the twins' for the cull's; VU_ZERO_STAGE_NONE
   none): an end while the other was trying is the other's, so it does not
   block a subject that was counting or on (its own trying, failed and
   blocked stages are judged as before) */
int vu_zero_mark_blocks_beside(int stage, int endKind, int otherStage);
/* the end's words ("a crash", "Android: closed from the recent apps list or
   with Force stop (REASON_USER_REQUESTED)") */
void vu_zero_mark_end_words(int endKind, int exitReason, int exitStatus, char *out, size_t size);
/* the mark's stage (VU_ZERO_STAGE_*) and version (into ver; "" none) */
int vu_zero_mark_parse(const char *mark, char *ver, size_t verSize);

/* The decision (pure): the mark's text (NULL none), the end's kind and its
   words, this version: the new mark's text into out ("" removes the file),
   the line's words into line, *block 1 when the cull stays off */
void vu_zero_mark_decide(const char *mark, int endKind, const char *endWords, const char *version,
                         char *out, size_t outSize, char *line, size_t lineSize, int *block);
/* the same with the twins' stage at that end (vu_zero_mark_blocks_beside) */
void vu_zero_mark_decide_beside(const char *mark, int endKind, const char *endWords,
                                const char *version, int otherStage, char *out, size_t outSize,
                                char *line, size_t lineSize, int *block);
/* the stage of the cull's mark as the last vu_zero_mark_step read it,
   before it was rewritten (VU_ZERO_STAGE_NONE before any): the twins'
   step judges by it (nodiscard_mark.c) */
int vu_zero_mark_stage_read(void);
/* the stage of the mark file name beside activityPath as it is now
   (VU_ZERO_STAGE_NONE without one): a later step's mark read before that
   step rewrites it (the fast pixel build's, fastps_mark.h) */
int vu_zero_mark_stage_beside(const char *activityPath, const char *name);
/* who the words of a step's "... was being tried then" name, set by the
   step that called vu_zero_mark_decide_beside / nodiscard_mark_decide_beside
   (NULL: that function's own words) */
void vu_zero_mark_beside_words(const char *who);
/* the rate card's results file beside activityPath (logs/ratecard.txt,
   port/render/rd_ratecard.h) says "measuring": the run before ended inside
   the card, which then answers for that end (REV4R N3: the attempts keep
   their state; the card skips itself at its next start) */
int vu_zero_mark_ratecard_measuring(const char *activityPath);
/* the end of the run before as the cull's step classed it (VU_ZERO_END_*,
   -1 before the step) and its words (the rate card's start rule:
   ratecard_env.c) */
int vu_zero_mark_end_read(char *words, size_t size);
const char *vu_zero_mark_beside_who(const char *fallback);

/* The start's step on files with Android's record given (the tests): reads
   the mark beside activityPath and the previous run's
   last-run-previous.txt, writes the decision (synced), exports the
   environment, logs one line through log (without "ico_pc: ") when a mark
   was found.  Returns 1 when the cull stays off. */
int vu_zero_mark_step(const char *activityPath, const char *version, int exitReason, int exitStatus,
                      void (*log)(const char *line));
/* vu_zero_mark_step with exit_info_last_reason (exit_info_begin ran
   before it, window_host.c) */
int vu_zero_mark_begin(const char *activityPath, const char *version,
                       void (*log)(const char *line));

#endif
