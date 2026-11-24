/*
 * port/platform/fastps_mark.h
 *
 * The fast pixel build's mark on the install (issue 179; port/render/
 * rd_fastps.h): logs/fastps-mark.txt beside the activity file, one
 * stage word and the version that was running when the renderer wrote it
 * ("trying v0.11.15"), read at the next start.  The stages, the end kinds
 * and the table are the zero-area cull's (vu_zero_mark.h, whose pure
 * functions this one calls):
 *
 *   trying <v>    written and synced before the build's two modules are
 *                 made, until its start-up self-test has passed
 *   counting <v>  written and synced when the self-test passed: the build
 *                 on, its first RD_FPS_PROBATION pictures not yet drawn
 *   on <v>        RD_FPS_PROBATION pictures drawn with it (not synced)
 *   failed <v>    its start-up self-test refused it: a module or one
 *                 of its pipelines not made, or its pictures differed
 *                 (synced; a pipeline not made in play only turns it off
 *                 for the run, nothing written)
 *   blocked <v>   written here: the build stays off on this install while
 *                 version v runs
 *
 * The next start (fastps_mark_begin, diag_host.c right after the
 * twins' mark) classes the previous run's end as the cull's mark does
 * (vu_zero_mark_end_kind: last-run.txt's end, Android's exit record) and
 * judges it by vu_zero_mark_blocks: while trying every end but a clean
 * close blocks; while counting or on only a crash, an ANR, a signal other
 * than SIGKILL, a device loss, a GPU hang or the watchdog do (a clean
 * close, a close from the recent apps list, a kill by the system, an
 * update: tried again); failed and blocked always.  Each mark blocks only
 * its own subject: at counting or on, an end while the cull's mark said
 * trying (vu_zero_mark_stage_read: the cull's step read it first) is the
 * cull's, and the build is tried again.  A block holds only
 * while the version in the mark is the running one.  The line ("the
 * previous run: fast pixel build: ...") is logged when a mark was found;
 * ICO_FASTPS_MARK (the file's path), ICO_FASTPS_VERSION (the version
 * to write) and, when the build is kept off, ICO_FASTPS_BLOCK (its
 * words) go to the environment for the renderer.  Nothing here is a
 * setting; the file is never in a config.
 */
#ifndef ICO_PLATFORM_FASTPS_MARK_H
#define ICO_PLATFORM_FASTPS_MARK_H

#include <stddef.h>

/* The decision (pure): the mark's text (NULL none), the end's kind
   (VU_ZERO_END_*) and its words, this version: the new mark's text into
   out ("" removes the file), the line's words into line, *block 1 when the
   build stays off */
void fastps_mark_decide(const char *mark, int endKind, const char *endWords, const char *version,
                        char *out, size_t outSize, char *line, size_t lineSize, int *block);
/* the same with the cull's or the twins' stage at that end (vu_zero_mark_blocks_beside:
   an end while the cull was trying is the cull's) */
void fastps_mark_decide_beside(const char *mark, int endKind, const char *endWords,
                               const char *version, int cullStage, char *out, size_t outSize,
                               char *line, size_t lineSize, int *block);

/* the build's stage as the last step read it (onepass_mark.c judges by it) */
int fastps_mark_stage_read(void);

/* The start's step on files with Android's record given (the tests): reads
   the mark beside activityPath and the previous run's last-run-previous.txt,
   writes the decision (synced), exports the environment, logs one line
   through log (without "ico_pc: ") when a mark was found.  Returns 1 when
   the build stays off. */
int fastps_mark_step(const char *activityPath, const char *version, int exitReason, int exitStatus,
                     void (*log)(const char *line));
/* fastps_mark_step with exit_info_last_reason (exit_info_begin ran
   before it, window_host.c) */
int fastps_mark_begin(const char *activityPath, const char *version, void (*log)(const char *line));

#endif
