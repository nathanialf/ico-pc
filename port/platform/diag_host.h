/*
 * port/platform/diag_host.h
 *
 * Diagnostics that make a failed test run explain itself, on by default
 * and cheap enough to stay on (docs/port/BOOT_DIAG.md, "What the log
 * contains"):
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
 *     boot started, or no new one for `later` seconds, samples where the
 *     main thread is (program counter and stack), dumps every thread and
 *     exits with code 4 (Windows: a message box first);
 *   - milestones and thread creation lines, timestamped with wall time,
 *     vsyncs and Main ticks.
 *
 * Every line goes straight to the log file with an unbuffered OS write, so
 * a crash or a kill leaves everything written before it.
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
/* Run on a fatal end (crash, abort, watchdog) after the report, to write
   the run's summary. Must not take locks the main thread may hold. */
void ico_diag_set_exit_hook(void (*fn)(const char *reason));

/* Starts the heartbeat and watchdog thread. first_s: seconds from now
   without any Main tick; later_s: seconds without a new Main tick after
   the first. 0 turns a limit off. */
void ico_diag_start(unsigned int first_s, unsigned int later_s);

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

/* --- The game's hooks (#ifdef ICO_HOST calls in ico2/, listed in
   docs/port/BOOT_DIAG.md) ------------------------------------------------ */

/* common/src/main.c: boot milestones and its static thread functions. */
void ico_host_milestone(const char *what);
void ico_host_name_func(void *func, const char *name);
/* fumi/ios/thread.c iosThreadCreate: the function thread `id` will run. */
void ico_host_thread_func(int id, void *func, int priority);
/* common/src/kanbanBoot.c kanbanBootMain: its two step variables, logged
   when they change. */
void ico_host_kanban_step(int boot_step, int mc_check_step);

#endif /* ICO_PLATFORM_DIAG_HOST_H */
