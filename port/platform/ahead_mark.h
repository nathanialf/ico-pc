/*
 * port/platform/ahead_mark.h
 *
 * The next picture submitted ahead (issue 179; port/render/rd_submit_thread.h
 * "ahead"): its mark on the install, logs/ahead-mark.txt beside the
 * activity file, so that a driver that loses the device (or hangs, or
 * crashes) with that order on does not lose it every second start.  The
 * loss ladder (ICO_LOSS_ROW) and the watchdog's mark hold the order off for
 * the one start after such an end; the start after that ran it again.
 *
 *   on <version>       written once a process, on the submit thread, as the
 *                      order first takes effect (rd_thread_submit_ahead_mark_fn,
 *                      before that job's first submit; flushed, not synced: a
 *                      crash or a loss leaves the system's page cache, which
 *                      still reaches the disc)
 *   blocked <version>  written at the next start when the run with "on" of
 *                      this version ended badly: the order stays off on this
 *                      install until the version changes
 *
 * The next start's decision (ahead_mark_decide, pure) from the mark, the
 * previous run's end (last-run-previous.txt's end field, last_run.h) and
 * Android's exit reason for that run (exit_info.h exit_info_last_reason,
 * -1 none):
 *   on, the previous run ended with a device loss, a GPU hang, the
 *       watchdog or a crash, or with no end written and Android's record
 *       saying it crashed (REASON_CRASH, REASON_CRASH_NATIVE) or stopped
 *       responding (REASON_ANR): blocked for this version (a mark of
 *       another version: removed, the order tried again);
 *   on, any other end: removed, the order tried again.  A clean close
 *       (last-run.txt's "clean", written by the window's close) is the
 *       mark's clearing; an ordinary close leaves no end written (a swipe
 *       from the recent apps, Force stop, the system ending the app in the
 *       background or for memory, an update) and Android's reason for it
 *       is none of the three above, so it never blocks;
 *   blocked of this version: kept, the order off; of another: removed;
 *   no mark: nothing (no line); anything else: removed (its line).
 * Every state logs one line at the start ("ico_pc: the previous run: the
 * next picture submitted ahead: ..."); the write logs one line once a
 * process.  Nothing here is a setting; the file is never in a config.
 */
#ifndef ICO_PLATFORM_AHEAD_MARK_H
#define ICO_PLATFORM_AHEAD_MARK_H

#include <stddef.h>

/* the file's name in the logs folder */
#define AHEAD_MARK_FILE "ahead-mark.txt"

/* The decision (above): mark (the file's text, NULL none), prevEnd
   (last_run.h LAST_RUN_END_*, -1 not read), androidReason (exit_info.h
   EXIT_INFO_REASON_*, -1 no record), version (this run's).  The new mark's
   text into out ("" removes the file), the line's words into line ("" with
   no mark: nothing logged); returns 1 when the order stays off this start
   (line then says why) */
int ahead_mark_decide(const char *mark, int prevEnd, int androidReason, const char *version,
                      char *out, size_t outSize, char *line, size_t lineSize);

/* 1 when the end counts as a bad end of the run that had the order in
   effect (above) */
int ahead_mark_bad_end(int prevEnd, int androidReason);

/* The start's step on the files: reads logsDir's mark and
   last-run-previous.txt, decides, writes the decision (synced), logs the
   line through log (without "ico_pc: ").  1 the order off this start, 0
   not, -1 no path.  Every call (the tests' starts). */
int ahead_mark_step(const char *logsDir, const char *version, int androidReason,
                    void (*log)(const char *line));

/* The process's start: ahead_mark_step once (later calls return the first
   one's answer), the path and version kept for the in-effect write.
   Returns NULL, or the words why the order stays off (for
   rd_thread_submit_ahead_prev; a static string). */
const char *ahead_mark_begin(const char *logsDir, const char *version, int androidReason,
                             void (*log)(const char *line));

/* The order in effect (any thread; once a process after ahead_mark_begin,
   later calls do nothing): "on <version>" written and its line logged */
void ahead_mark_in_effect(void);

#endif
