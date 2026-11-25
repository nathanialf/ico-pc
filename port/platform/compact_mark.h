/*
 * port/platform/compact_mark.h
 *
 * The index compaction's mark on the install, for its Mali-G52 form (issue
 * 179; port/render/rd_vu_compact.h): logs/compact-mark.txt beside the
 * activity file, one stage word and the version that was running when the
 * renderer wrote it ("trying v0.11.16"), read at the next start.  The
 * stages, the end kinds and the table are the zero-area cull's
 * (vu_zero_mark.h, whose pure functions this one calls):
 *
 *   trying <v>    written and synced before its own start-up test's first
 *                 replay with it, until the test has passed
 *   counting <v>  written and synced when the test passed: on, its first
 *                 RD_VU_CMP_PROBATION pictures not yet read back
 *   on <v>        RD_VU_CMP_PROBATION pictures drawn with it (not synced)
 *   failed <v>    its own start-up test refused it (the lists read back,
 *                 the pictures or the stamps; synced); a test that could
 *                 not check removes the file instead
 *   blocked <v>   written here: it stays off on this install while version
 *                 v runs
 *
 * The next start (compact_mark_begin, diag_host.c before the zero-area
 * cull's mark) classes the previous run's end as the cull's mark does
 * (vu_zero_mark_end_kind: last-run.txt's end, Android's exit record) and
 * judges it by vu_zero_mark_blocks: while trying every end but a clean
 * close blocks; while counting or on only a crash, an ANR, a signal other
 * than SIGKILL, a device loss, a GPU hang or the watchdog do; failed and
 * blocked always.  Its test runs before the other attempts' self-tests
 * write their marks, so the step runs first and reads theirs as the
 * previous run left them (vu_zero_mark_stage_beside): at counting or on,
 * an end while the cull, the twins, the fast pixel build or the one-pass
 * blend said trying, or the rate card was measuring, is theirs, and their
 * steps read its stage (compact_mark_stage_read) the same way,
 * and it is tried again.  A block holds only while the version in the mark
 * is the running one.  The line ("the previous run: index compaction:
 * ...") is logged when a mark was found; ICO_COMPACT_MARK (the file's
 * path), ICO_COMPACT_VERSION (the version to write) and, when it is kept
 * off, ICO_COMPACT_BLOCK (its words) go to the environment for the
 * renderer.  Nothing here is a setting; the file is never in a config.
 */
#ifndef ICO_PLATFORM_COMPACT_MARK_H
#define ICO_PLATFORM_COMPACT_MARK_H

#include <stddef.h>

/* The decision (pure): the mark's text (NULL none), the end's kind
   (VU_ZERO_END_*) and its words, this version, the stage another attempt's
   mark said at that end (VU_ZERO_STAGE_*; trying: the end is theirs) and
   whose (NULL: "another attempt was"): the new mark's text into out (""
   removes the file), the line's words into line, *block 1 when the
   compaction stays off */
void compact_mark_decide(const char *mark, int endKind, const char *endWords, const char *version,
                         int besideStage, const char *besideWho, char *out, size_t outSize,
                         char *line, size_t lineSize, int *block);

/* The start's step on files with Android's record given (the tests): reads
   the mark beside activityPath, the other attempts' marks as the previous
   run left them and that run's last-run-previous.txt, writes the decision
   (synced), exports the environment, logs one line through log (without
   "ico_pc: ") when a mark was found.  Returns 1 when it stays off. */
int compact_mark_step(const char *activityPath, const char *version, int exitReason, int exitStatus,
                      void (*log)(const char *line));
/* the mark's stage (VU_ZERO_STAGE_*) as the last compact_mark_step read it,
   before it was rewritten (VU_ZERO_STAGE_NONE before any): the other
   attempts' steps, which run after it, take an end while it said trying as
   its own (its test can run after theirs wrote their marks: a graphics API
   switch runs the start-up self-tests again) */
int compact_mark_stage_read(void);
/* compact_mark_step with exit_info_last_reason (exit_info_begin ran before
   it, window_host.c) */
int compact_mark_begin(const char *activityPath, const char *version,
                       void (*log)(const char *line));

#endif
