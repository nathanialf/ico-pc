/*
 * port/platform/vu_zero_mark.h
 *
 * The zero-area cull's mark on the install (issue 179; port/render/
 * rd_vu_zero.h): logs/vu-zero-mark.txt beside the activity file, one word
 * the renderer writes as it tries the cull, read at the next start.
 *
 *   trying   written and synced before the cull's one module is made (its
 *            self-test follows): a run that ends other than cleanly with it
 *            (a crash, a device loss, a GPU hang, the watchdog, or no end
 *            written: killed) leaves it
 *   failed   its self-test or a check refused it (written and synced)
 *   on       RD_VU_ZERO_PROBATION pictures drawn with it
 *   blocked <version>   written here: a run of that version ended while
 *            trying, or the cull failed: the cull stays off on this install
 *            until the version changes
 *
 * vu_zero_mark_begin, at the start (diag_host.c, after last_run_begin kept
 * the previous run's last-run.txt as last-run-previous.txt, whose end it
 * reads): the decision, one "the previous run: zero-area cull: ..." line,
 * ICO_VU_ZERO_MARK (the file's path, for the renderer) and, when the cull
 * is kept off, ICO_VU_ZERO_BLOCK (its words) in the environment.  Nothing
 * here is a setting; the file is never in a config.
 */
#ifndef ICO_PLATFORM_VU_ZERO_MARK_H
#define ICO_PLATFORM_VU_ZERO_MARK_H

#include <stddef.h>

/* the decision from the mark's text (mark, NULL none), the previous run's
   end (last_run.h LAST_RUN_END_*, -1 not read) and this version: the new
   mark's text into out ("" removes the file), the line's words into line,
   *block 1 when the cull stays off (its words in line too), *lossRow 1 when
   the previous run's end counts as a lost start (the watchdog with the
   cull on); the pure part, tested on every platform */
void vu_zero_mark_decide(const char *mark, int prevEnd, const char *version, char *out,
                         size_t outSize, char *line, size_t lineSize, int *block, int *lossRow);

/* The start's step (above): reads the mark beside activityPath and the
   previous run's last-run-previous.txt, writes the decision (synced),
   exports the environment, logs one line through log (without "ico_pc: ").
   Returns 1 when the previous run's end counts as a lost start. */
int vu_zero_mark_begin(const char *activityPath, const char *version,
                       void (*log)(const char *line));

#endif
