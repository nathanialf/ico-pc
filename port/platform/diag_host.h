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
 * The C library's stdout and stderr are fully buffered on Windows (package
 * Q1, host_config.h ico_host_redirect_output) and the host loop flushes
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
unsigned int ico_diag_progress(void);
/* 1 while a movie plays, 0 after: the heartbeat and the watchdog's reason
   say so. */
void ico_diag_set_movie(int playing);
/* Run on a fatal end (crash, abort, watchdog) after the report, to write
   the run's summary. Must not take locks the main thread may hold. */
void ico_diag_set_exit_hook(void (*fn)(const char *reason));

/* Package R2: the window's rd_Present is bracketed by these two. The time
   spent inside a present does not count towards either watchdog limit: a
   graphics driver or an effects injector (ReShade) may compile shaders inside
   its first presents for minutes. A single present is excused for at most
   300 s, so a driver that hangs for good is still stopped; the report says
   when the main thread is inside a present. */
void ico_diag_present_enter(void);
void ico_diag_present_leave(void);

/* Starts the heartbeat and watchdog thread. first_s: seconds from now
   without any Main tick; later_s: seconds without a new Main tick or new
   progress after the first tick. 0 turns a limit off. */
void ico_diag_start(unsigned int first_s, unsigned int later_s);
/* 1 while the game is meant to stand still (Android: the app in the
   background), 0 when it runs again: while paused neither watchdog limit
   counts and the heartbeat is quiet. Any thread. */
void ico_diag_watchdog_pause(int paused);

/* One line to the log, written at once (a newline is added). */
void ico_diag_log(const char *fmt, ...) ICO_DIAG_PRINTF(1, 2);
/* A boot milestone: the line gets the wall time, vsyncs, Main ticks and
   the current thread. */
void ico_diag_milestone(const char *fmt, ...) ICO_DIAG_PRINTF(1, 2);
/* Keeps the last failure message (an assertion, a scheduler error) for the
   crash report that follows it. */
void ico_diag_set_failure(const char *fmt, ...) ICO_DIAG_PRINTF(1, 2);

/* Names a thread function for the thread lines; game threads are named by
   the function iosThreadCreate runs (ico_host_thread_func). */
void ico_diag_name_func(void *func, const char *name);
/* Seconds since ico_diag_init. */
double ico_diag_uptime(void);
/* A monotonic clock in nanoseconds (QueryPerformanceCounter, CLOCK_MONOTONIC)
   for timing the host's work; its zero is arbitrary. */
unsigned long long ico_diag_now_ns(void);

/* --- The game's hooks (#ifdef ICO_HOST calls in ico2/) ------------------------------------------------ */

/* common/src/main.c: boot milestones and its static thread functions. */
void ico_host_milestone(const char *what);
void ico_host_name_func(void *func, const char *name);
/* fumi/ios/thread.c iosThreadCreate: the function thread `id` will run. */
void ico_host_thread_func(int id, void *func, int priority);
/* common/src/kanbanBoot.c kanbanBootMain: its two step variables, logged
   when they change. */
void ico_host_kanban_step(int boot_step, int mc_check_step);

#endif /* ICO_PLATFORM_DIAG_HOST_H */
