/*
 * port/platform/thread_stat.h
 *
 * Issue 179: how long a thread ran on a core and how long it waited for
 * one, from the kernel's per-thread schedstat (/proc/self/task/<tid>/
 * schedstat: "run_ns wait_ns slices", the time on a core, the time
 * runnable on a run queue, the times it was put on a core; Linux and
 * Android, the kernel's CONFIG_SCHED_INFO).  The window reads the game,
 * render and submit threads' values at each 10 s block (render_thread.c
 * ico_render_thread_block_line) and says the block's difference at the
 * end of the game-thread line:
 *   "; ran / waited for a core: game 3.9 s / 0.4 s, render 2.1 s / 1.8 s,
 *   submit 0.0 s / 0.0 s"
 * A thread that waited for a core about as long as it ran was kept off
 * the cores it may use by other threads (the scheduler, not the GPU); one
 * that ran little and waited little waited on something else (a lock, the
 * GPU, a present).  Once, when the file could not be read:
 *   "; ran / waited for a core: schedstat not readable (EACCES)"
 * Each read is open, read and close (procfs makes the text at the read).
 * The parser, the per-thread difference and the text are pure
 * (port/platform/test/thread_stat_test.c, with a fake root).
 */
#ifndef ICO_PLATFORM_THREAD_STAT_H
#define ICO_PLATFORM_THREAD_STAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ThreadStat {
    uint64_t runNs, waitNs, slices;
} ThreadStat;

/* "123 456 7\n": the three numbers; false unless there are three (len
   bytes read at most, no NUL needed) */
bool thread_stat_parse(const char *buf, size_t len, ThreadStat *out);
/* the calling thread's id (gettid; 0 off Linux) */
long thread_stat_tid(void);
/* the schedstat of thread tid under root (NULL: /proc/self/task): 0, or
   the errno of the open or the read (EINVAL: not three numbers; ENOSYS off
   Linux, and for tid 0) */
int thread_stat_read(const char *root, long tid, ThreadStat *out);

/* One thread's values between blocks: the thread id they were read for
   (0: none yet) and the last values */
typedef struct ThreadStatTrack {
    long tid;
    ThreadStat last;
} ThreadStatTrack;

/* now's values for tid against the last: the difference into delta and
   true when the last was the same thread's (a thread made again, or the
   first read, gives false); now kept as the last.  A count that went
   back (the kernel's) gives 0 for it */
bool thread_stat_track(ThreadStatTrack *t, long tid, const ThreadStat *now, ThreadStat *delta);

/* the block's part, n threads (who their words, have false: left out):
   "ran / waited for a core: game 3.9 s / 0.4 s, render 2.1 s / 1.8 s";
   "" when none has values; the length written */
size_t thread_stat_text(char *out, size_t cap, const char *const *who, const ThreadStat *delta,
                        const bool *have, int n);
/* "ran / waited for a core: schedstat not readable (EACCES)" for the
   errno err; the length written */
size_t thread_stat_fail_text(char *out, size_t cap, int err);

#endif /* ICO_PLATFORM_THREAD_STAT_H */
