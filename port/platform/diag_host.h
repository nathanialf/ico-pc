/*
 * port/platform/diag_host.h
 *
 * Diagnostics that make a failed test run explain itself, on by default
 * and cheap enough to stay on:
 *
 *   - crash handlers: an access violation, illegal instruction, divide
 *     error, stack overflow (Windows: a vectored handler for faults in the
 *     executable and the unhandled-exception filter; POSIX: SIGSEGV, SIGBUS,
 *     SIGILL, SIGFPE, SIGTRAP on an alternate stack) or abort() (assert,
 *     ico_assert, the scheduler's fatal errors) writes the cause, the
 *     faulting address as module+offset (look it up in ico_pc.map or with
 *     addr2line on the exact executable), the fault address, the current
 *     game thread and its last kernel call, return-address candidates from
 *     the stack, the last failure message, the heartbeat line and every
 *     thread to the log, then (Windows) shows a message box naming the log
 *     and exits with code 3;
 *   - a heartbeat: every 2 s of wall time, from its own OS thread (so a
 *     game thread that spins without a kernel call cannot stop it), one
 *     line of progress, with "no progress" once it repeats unchanged;
 *   - a watchdog on the same thread: no Main tick `first` seconds after
 *     boot started, or neither a new one nor other progress (a movie's
 *     pictures, ico_diag_note_progress) for `later` seconds, samples where the
 *     main thread is (program counter and stack), dumps every thread and
 *     exits with code 4 (Windows: a message box first);
 *   - milestones and thread creation lines, timestamped with wall time,
 *     vsyncs and Main ticks.
 *
 * Every line goes straight to the log file with an unbuffered OS write, so
 * a crash or a kill leaves everything written before it.
 *
 * The C library's stdout and stderr are fully buffered on Windows
 * (host_config.h ico_host_redirect_output) and the host loop flushes
 * them once per vsync; a crash, abort or watchdog report first writes out
 * what they hold (from a helper thread it waits on for at most half a
 * second, since the stopped thread may hold a stream lock), and a milestone
 * on the main thread flushes them before its own line, so the log keeps
 * its order.
 */
#ifndef ICO_PLATFORM_DIAG_HOST_H
#define ICO_PLATFORM_DIAG_HOST_H

#include <stddef.h>

#ifdef __GNUC__
#define ICO_DIAG_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define ICO_DIAG_PRINTF(a, b)
#endif

/* Opens log_path for appending (NULL: standard error) and installs the
   crash handlers. Call once, as early as possible. */
void ico_diag_init(const char *log_path);
/* The log path the message boxes name. */
const char *ico_diag_log_path(void);

/* The game-side state the heartbeat prints and the watchdog reads:
   status writes one line (no newline); main_ticks and vsyncs count. Any
   may be NULL. They run on the watchdog thread, so they may only read
   plain variables. */
typedef void (*IcoDiagStatusFn)(char *out, size_t size);
void ico_diag_set_sources(IcoDiagStatusFn status, unsigned int (*main_ticks)(void),
                          unsigned int (*vsyncs)(void));
/* A second sign of life besides the Main ticks, for work that runs a long
   time inside one Main tick: a movie plays whole inside one pass of Main's
   loop (common/src/main.c), so port/fmv/movie.c notes each picture it shows
   and each vsync of its preroll. The watchdog's later limit counts from
   whichever moved last; the Main ticks themselves are untouched. */
void ico_diag_note_progress(void);
/* The count ico_diag_note_progress raised (tests). */
unsigned int ico_diag_progress(void);
/* 1 while a movie plays, 0 after: the heartbeat and the watchdog's reason
   say so. */
void ico_diag_set_movie(int playing);
/* Run on a fatal end (crash, abort, watchdog) after the report, to write
   the run's summary. Must not take locks the main thread may hold. */
void ico_diag_set_exit_hook(void (*fn)(const char *reason));
/* Android: a crash or the watchdog there ends the run with no
   window of its own, and the system shows nothing. box shows the player a
   message and returns when it is dismissed; flush writes into the log what
   the process wrote to stderr just before (the C library's abort message,
   "FORTIFY: ..." or "stack corruption detected"), without waiting on a lock
   for long. Both run on the watchdog thread (ico_diag_start), the crashing
   thread waiting for them for a bounded time; either may be NULL.
   Linux: box only, for the watchdog's end (on the watchdog thread); a
   crash there ends in its signal handler and shows none. Windows shows
   its own box; macOS and iOS none. */
void ico_diag_set_fatal_ui(void (*box)(const char *text), void (*flush)(void));
/* The process's resident memory and its peak in KB (/proc/self/status:
   Linux, Android); 0, or -1 where unknown. Async-signal-safe. */
int ico_diag_process_memory(long *rss_kb, long *peak_kb);

/* The window's rd_present is bracketed by these two. The time
   spent inside a present does not count towards either watchdog limit: a
   graphics driver or an effects injector (ReShade) may compile shaders inside
   its first presents for minutes. A single present is excused for at most
   300 s, so a driver that hangs for good is still stopped; the report says
   when the main thread is inside a present. Only the part of one present
   above 1 s is excused, and only after ico_diag_set_effects_program(1): a
   plain driver's slow present is a stall. */
void ico_diag_set_effects_program(int loaded);
void ico_diag_present_enter(void);
void ico_diag_present_leave(void);

/* Starts the heartbeat and watchdog thread. first_s: seconds from now
   without any Main tick; later_s: seconds without a new Main tick or new
   progress after the first tick. 0 turns a limit off. */
void ico_diag_start(unsigned int first_s, unsigned int later_s);
/* Starts the same thread earlier with both limits off (the heartbeat
   only), so a crash before ico_diag_start still has it (Android: the crash
   handler's log flush and box go through it). ico_diag_start then sets the
   limits and counts them from its own call. */
void ico_diag_start_reporter(void);
/* 1 while the game is meant to stand still (Android: the app in the
   background), 0 when it runs again: while paused neither watchdog limit
   counts and the heartbeat is quiet. Any thread. */
void ico_diag_watchdog_pause(int paused);
/* The thread that called ico_diag_init is about to end (Android: Quit,
   with the process possibly kept): from now on the watchdog neither
   samples nor signals it. */
void ico_diag_main_thread_end(void);

/* One line to the log, written at once (a newline is added). */
void ico_diag_log(const char *fmt, ...) ICO_DIAG_PRINTF(1, 2);
/* A boot milestone: the line gets the wall time, vsyncs, Main ticks and
   the current thread. */
void ico_diag_milestone(const char *fmt, ...) ICO_DIAG_PRINTF(1, 2);
/* Keeps the last failure message (an assertion, a scheduler error) for the
   crash report that follows it. */
void ico_diag_set_failure(const char *fmt, ...) ICO_DIAG_PRINTF(1, 2);

/* What the host is doing in a step where a crash would be someone
   else's (the graphics driver building a pipeline, rd_pipeline.c): one
   line, printed by the crash and watchdog reports as "ico_pc: while:
   <note>" after the CRASH or WATCHDOG line. Set before the step, cleared
   after it; a static buffer of 160 bytes, no allocation. One writer
   thread. A crash while the note starts with "creating pipeline" or
   "creating the start-up pipelines" is a graphics driver crash while
   preparing the shaders: the handler starts the game again with the next
   step of startup_retry.h when there is one (ico_diag_prepare_relaunch),
   else shows a box saying that the driver crashed. */
void ico_diag_set_activity(const char *fmt, ...) ICO_DIAG_PRINTF(1, 2);
void ico_diag_clear_activity(void);
/* The note ("" when none; tests). */
const char *ico_diag_activity(void);
/* The start-up activity file (logs/startup-activity.txt; issue 93): while
   it records, each note (ico_diag_set_activity) replaces the file's text
   with one write into the file, kept open from the first note to the end
   of the recording, so a start that is ended without a crash report (the
   system ends the process, the driver exits it) still says on the next
   start what it was doing.
   ico_diag_startup_activity_begin, once before the renderer starts: keeps
   path for the writes, reads what the previous run left there and deletes
   the file. A non-empty text is logged as "ico_pc: the previous run ended
   while: <note>" and the result is 1: this run creates its pipelines as
   the frames need them. relaunched != 0 (a restart after a driver crash,
   ICO_STARTUP_RETRY) logs and deletes it the same way but returns 0: the
   restart's own step decides. 0 when there is no file or it is empty.
   The file's first line is the version that wrote it
   (ico_diag_set_version), the note follows: a note from another version
   is logged as "ico_pc: the previous run ended while: <note> (version
   <old>); trying the start-up set again on <new>" and the result is 0, so
   a new version tries the start-up set again. */
int ico_diag_startup_activity_begin(const char *path, int relaunched);
/* The running version's text (the "ico_pc: version" log line's: "<label>
   (commit <c>)" or "development build (commit <c>)"); copied, at most 127
   bytes. */
void ico_diag_set_version(const char *version);
/* ICO_DIAG_ACTIVITY_STARTUP: around the start-up set's creation: each
   pipeline's note (the set's own "(i of m)" notes between two are not
   written; "creating the start-up pipelines" until the first), and a
   cleared note keeps the file's last one. ICO_DIAG_ACTIVITY_LAZY: a run that
   creates the pipelines as the frames need them, for the whole run; a
   cleared note writes a line saying none is being created (so the next
   start stays lazy). 0: stops recording and deletes the file (the start-up
   set's end). */
#define ICO_DIAG_ACTIVITY_STARTUP 1
#define ICO_DIAG_ACTIVITY_LAZY 2
void ico_diag_startup_activity_record(int mode);
/* A clean exit: stops recording; a lazy run keeps the file (its line
   saying the run exited lazy, which the next start logs as "ico_pc: the
   previous run created the pipelines as the frames needed them; this one
   does too"), so the lazy decision sticks until the version changes; any
   other run deletes it. */
void ico_diag_startup_activity_exit(void);
/* The relaunch's inputs: the step this run is at (StartupRetry, 0 for the
   first run), whether the build has the D3D12 backend, whether the player
   named the backend, whether the player named the blending (blend= /
   [dev] blend or ICO_RD_BLEND), and the backend in use; the window sets
   them before the renderer starts. */
void ico_diag_set_startup_retry(int step, int have_d3d12, int backend_explicit, int blend_explicit,
                                const char *backend);
/* The graphics device was lost (window_host.c device_lost_quit; the normal
   path, not a crash): the loss step of startup_retry.h this run takes
   next, logged. 1 when there is one: the program then starts again with
   it at exit, after the end-of-run steps (registered by
   ico_diag_prepare_relaunch), and the window shows no box; 0 when nothing
   is left, the platform cannot start the game again (Android, iOS) or the
   relaunch was not prepared: the window shows its box. blend_was_fixed:
   the run drew with the fixed-function blend (rd_blend_exact). */
int ico_diag_device_lost_restart(int blend_was_fixed);
/* Removes ICO_STARTUP_RETRY from this process's environment once the
   window has read it, so nothing started from here inherits the step (the
   relaunch's own environment was captured without it at start-up). */
void ico_diag_forget_startup_retry_env(void);
/* loaded: the renderer created its pipeline cache from the file at path
   (rhi_pipeline_cache_loaded), which the cache step deletes; path NULL
   keeps the path given before. */
void ico_diag_set_pipeline_cache(int loaded, const char *path);
/* Captures what starting the program again needs (Windows: the module
   path, GetCommandLineW and the environment block; Linux and macOS: the
   executable's path, argv and the environment's pointers), so the crash
   path does not allocate. Call once after ico_diag_init with main's argc
   and argv (kept: they must live as long as the process); registers the
   relaunch after a device loss with atexit (ico_diag_device_lost_restart).
   Android and iOS: nothing (no relaunch). */
void ico_diag_prepare_relaunch(int argc, char **argv);
/* Tests: once the relaunch is prepared, fn(value) stands in for starting
   the program again (value: ICO_STARTUP_RETRY's), returning 0 as a started
   run would; NULL: the real relaunch. */
void ico_diag_set_relaunch_stub(int (*fn)(const char *value));

/* Names a thread function for the thread lines; game threads are named by
   the function iosThreadCreate runs (ico_host_thread_func). */
void ico_diag_name_func(void *func, const char *name);
/* Seconds since ico_diag_init. */
double ico_diag_uptime(void);
/* A monotonic clock in nanoseconds (QueryPerformanceCounter, CLOCK_MONOTONIC)
   for timing the host's work; its zero is arbitrary. */
unsigned long long ico_diag_now_ns(void);

/* A NaN reached game code that the PS2 never fed one (its FPU
   has no NaN): site names the check, caller is a return address (NULL if
   none), logged as module+offset (libmain.so+0x..., ico_pc+0x...) once
   ico_diag_init has found the program's extent, else as a plain pointer.
   The first fault from each (site, caller) pair is logged, up to 32 pairs;
   every fault is counted. Returns 1 when this call wrote the line (the
   caller may add a line of its own), else 0. Game thread only. */
int ico_diag_float_fault(const char *site, const void *caller);
/* The faults counted so far (tests). */
unsigned int ico_diag_float_faults(void);

#ifdef _WIN32
/* Windows: puts the vectored exception handler in, once the file dialog is
   behind us (shell extensions in it may raise and handle their own
   faults); main_host.c calls it. */
void ico_diag_arm_vectored(void);
#endif

/* --- The game's hooks (calls in ico2/) ----------------------------------- */

/* common/src/main.c: boot milestones and its static thread functions. */
void ico_host_milestone(const char *what);
void ico_host_name_func(void *func, const char *name);
/* fumi/ios/thread.c iosThreadCreate: the function thread `id` will run. */
void ico_host_thread_func(int id, void *func, int priority);
/* common/src/kanbanBoot.c kanbanBootMain: its two step variables, logged
   when they change. */
void ico_host_kanban_step(int boot_step, int mc_check_step);

#endif /* ICO_PLATFORM_DIAG_HOST_H */
