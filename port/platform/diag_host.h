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
 *     faulting address as module+offset, the fault address, the current
 *     game thread and its last kernel call, return-address candidates from
 *     the stack, the last failure message, the heartbeat line and every
 *     thread to the log, then each offset's name from the map shipped
 *     beside the program (symmap.h) and the report file (report.h), then
 *     (Windows) shows a message box naming the report and exits with code
 *     3;
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
#include <stdint.h>

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
/* The renderer's start-up set while it is created (the window's progress
   function): the heartbeat adds "pipelines <done> of <total>, longest
   <longest_ms / 1000> s"; total 0 (when the set is done) takes it off. Any
   thread. */
void ico_diag_set_pipelines(unsigned int done, unsigned int total, unsigned int longest_ms);
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
/* The device's available and total memory in KB (/proc/meminfo's
   MemAvailable and MemTotal: Linux, Android); 0, or -1 (both -1) where
   unknown. Open and read only, as ico_diag_process_memory. */
int ico_diag_device_memory(long *avail_kb, long *total_kb);
/* "memory 191 MB (most 197 MB); device 320 MB available of 7632 MB" (the
   process's, then the device's; "...; device memory unknown" without
   /proc/meminfo), "" where the process's is unknown (Windows, macOS): the
   heartbeat's, the low-memory line's, the window's 10 s memory line's and
   report.txt's. */
void ico_diag_memory_text(char *out, size_t size);

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

/* A GPU hang: the watchdog fires (either limit, not the event pump's
   bound) while the main thread is blocked in a wait for the GPU and the
   work it waits for has not finished (IcoDiagGpu.waiting_for above
   .finished). The driver never returned and never reported a loss, so
   the end is the device loss it would have been: the WATCHDOG block's
   second line is "ico_pc: the graphics driver did not finish the picture
   (submission V has not finished after a W s wait)" (V: the submission
   the main thread waits for, W: how long it has waited), then "ico_pc:
   the GPU: the main thread has waited W s for submission V; the newest is
   S (replay R, frame F), the newest seen finished D; this end counts as a
   device loss (the loss dump; the next start counts the loss and does not
   replay the frame)"; after the threads and the symbols, dump() runs on
   the watchdog thread (the window's pass ring and loss dump, which notes
   the loss: ico_diag_note_device_loss), and when it noted none the loss
   is noted without a dump. The hang's loss line is its own ("loss dump
   (the driver hung): <zip>", or "graphics driver hung, no loss dump"):
   the next start counts it as a loss (ICO_DIAG_END_LOSS, so Android's
   robust step follows) and a report attaches its dump, but it is never
   replayed by the loss bisect (ico_diag_loss_dump stays NULL: the replay
   waits for the GPU with no limit, before the watchdog runs, and the
   frame the driver hung on can hang it the same way); the next start logs
   "ico_pc: the previous run ended because the graphics driver stopped
   responding (a GPU hang, counted as a device loss); its loss dump <zip>
   is not replayed (the replay could hang the same way)". The report
   file's "what ended the run" is "the graphics driver stopped responding
   (submission V has not finished after a W s wait)", the box
   the device loss's words, the end "the run ended: the graphics driver
   stopped responding (watchdog)" with the watchdog's exit code and no
   relaunch. Any other watchdog end with the main thread inside a present
   adds "ico_pc: the GPU: no wait for it in progress; the newest
   submission is S (replay R, frame F, made N s ago), the newest seen
   finished D" (its acquire or present call may be the one that has not
   returned). progress fills *out from the graphics backend without a
   call into the driver (port/rhi/rhi.h rhi_gpu_progress) and returns 1,
   or 0 when the backend does not track it (then no end is a hang); any
   thread. Either may be NULL. */
typedef struct IcoDiagGpu {
    unsigned long long submitted, finished, waiting_for;
    double waiting_s, submitted_ago_s;
    unsigned int replay, frame;
} IcoDiagGpu;

void ico_diag_set_gpu_hang(int (*progress)(IcoDiagGpu *out), void (*dump)(void));

/* The main thread is inside a call that waits on the operating system and
   runs none of the game's code (the window's event pump, SDL_PollEvent):
   ico_diag_os_wait_enter before it, ico_diag_os_wait_leave after it, on the
   main thread. what names it for the log ("the window's event pump"; a
   static string). The time spent inside counts towards neither watchdog
   limit (issue 105: Windows held the pump for 50 s at a time while the game
   idled). When one wait outlasts the limit that would have ended the run,
   the watchdog logs once "<what> has not returned for N s (the system is
   holding it: the game is not hung); waiting", and the heartbeat says how
   long it has been held; the run ends with the watchdog's report only when
   one wait passes the bound of ico_diag_set_os_wait_limit (default 600 s,
   0: never), so a true deadlock in the system is still reported. The
   bound holds only while a limit does (ico_diag_start with a limit not 0):
   with the watchdog off, and before ico_diag_start (Android's reporter
   runs from the start, ico_diag_start_reporter, while the start-up set's
   progress screen pumps), no wait ends the run; nor does time while the
   watchdog is paused (ico_diag_watchdog_pause, the app in the
   background). */
void ico_diag_os_wait_enter(const char *what);
void ico_diag_os_wait_leave(void);
void ico_diag_set_os_wait_limit(unsigned int seconds);
/* What the wait is doing, for the watchdog's "waiting" line and its report
   (the window: the part of the pump, the window message being handled,
   the last messages and events; pump_watch.h). Runs on the watchdog's
   thread while the main thread is held, so it may only read. */
void ico_diag_set_os_wait_describe(void (*fn)(char *out, size_t size));
/* Windows: while one such wait lasts, the watchdog samples the main
   thread at 2, 10 and 30 s into it and logs where it is, with the code
   modules on its stack innermost first (issue 105: dinput8.dll there says
   the DirectInput scan, hid.dll or setupapi.dll the HID enumeration):
     ico_pc: watchdog: the window's event pump has held the main thread
     for 2 s: at win32u.dll+0x1234; on its stack: DINPUT8.dll+0x5678, ...
   A module's name is read from its own export table (no loader lock, which
   the held thread may own). */

/* The report file's extra part (report.h IcoReportInputs.extra): fn fills
   out with text of its own (the window: SDL's log ring) when a report is
   written, on any thread and from a crash handler (no allocation, no
   locks). NULL: none. */
void ico_diag_set_report_extra(void (*fn)(char *out, size_t size));

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
   after it; a static buffer of 640 bytes, no allocation (a start-up
   batch's note names its keys, its modules and their sizes, and the
   heartbeat line repeats it while the start-up set is created). One writer
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
   the file. A start-up set made under the careful policy (the window's
   choice, ico_diag_startup_activity_careful) adds the line "set: careful"
   after the note, which the next start reads (ico_diag_startup_pipeline_set). A non-empty text is logged as "ico_pc: the previous run ended
   while: <note>" and the result is 1: this run creates its pipelines as
   the frames need them. relaunched != 0 (a restart after a driver crash,
   ICO_STARTUP_RETRY) logs and deletes it the same way but returns 0: the
   restart's own step decides. 0 when there is no file or it is empty.
   The file's first line is the version that wrote it
   (ico_diag_set_version), the note follows: a note from another version
   is logged as "ico_pc: the previous run ended while: <note> (version
   <old>); trying the start-up set again on <new>" and the result is 0, so
   a new version tries the start-up set again; a note that counts as an
   end while making the pipelines (ico_diag_startup_end_count) drops the
   last clause: the count decides, whatever the version. 0 as well: a
   note of a lazy run for one start only, its clean exit ("ico_pc: the
   previous run made the pipelines as the frames needed them for that
   start only; this one tries the start-up set again") or its end between
   two creations ("ico_pc: the previous run ended while: <note>; that lazy
   run was for that start only, so this one tries the start-up set
   again"), and an end while preparing the graphics in the background
   (ico_diag_startup_activity_background). A background mark beside any
   other note is logged as "ico_pc: the previous run was in the background
   when it ended (<line>)". It counts the previous run's end
   (ico_diag_startup_end_count). */
int ico_diag_startup_activity_begin(const char *path, int relaunched);
/* A start-up batch's compile times (rd.h rd_set_batch_fn: one line per
   batch, "VU batch 329-336 of 702 (compiler ...): 8 pipelines 0.41-2.10
   s, mean 1.29 s, ...; memory 185 MB (most 194 MB), pipeline cache 3104
   KB; policy batch 8, ...; vu_main_vs <w> words <i> instructions: 8 x
   1.10-2.10 s mean 1.62 s", "2D batch 193-224 of 527 ...", and a run without the set
   its lazy batches' "lazy batch 3, pipelines 41-57 of this run ..."): kept
   as the activity file's "compiled: " lines after the note, among the
   "system: " lines of ico_diag_note_low_memory (the last
   ICO_DIAG_ACTIVITY_BATCHES of both), written at once (a batch's note
   goes to the disc before the batch starts), so a device that froze in a
   batch or a start the system ended shows the sizes, times and memory of
   every batch it finished. The next start's
   ico_diag_startup_activity_begin logs each ("ico_pc: the previous run
   (version V) compiled: <line>"), then the last one again ("ico_pc: the
   previous run's last finished batch: <line>"). Cleared with the file. */
#define ICO_DIAG_ACTIVITY_BATCHES 24
void ico_diag_startup_activity_batch(const char *line);
/* The starts in a row that ended without a word while preparing the
   graphics (issues 93 and 107, and the ambiguous ends: a swipe from the
   recent apps, the system ending an app in the background, Task Manager),
   counted by ico_diag_startup_activity_begin from the previous run's note,
   by kind: ICO_DIAG_END_VU_PRE (the note names a pipeline of the VU vertex
   pre-pass or of the record path that draws from it,
   startup_retry_note_has_vu_pre) and ICO_DIAG_END_PIPELINES (else a
   pipeline being made, "creating pipeline ...", "creating pipelines ...",
   "creating the start-up pipelines"). A third count, ICO_DIAG_END_LOSS,
   is the starts in a row that lost the graphics device (the activity
   file's loss line, ico_diag_note_device_loss), whatever the note: a start
   without a loss breaks it, except the one start that ran with robust
   access on because of it (ico_diag_startup_end_persist), which leaves it.
   The counts live in "<activity file without .txt>-ends.txt" ("pipelines
   <n>", "vu_pre <n>", "loss <n>", "loss_robust <0|1>"; an older file's
   "vu_ext <n>" is dropped; a write that fails is logged once a run:
   "ico_pc: the count of starts that ended while preparing the graphics
   could not be written to <path> (pipelines <n>, vertex pre-pass <n>,
   device losses <n>)"). An end in the foreground counts one more and logs
   "ico_pc: starts in a row that ended while preparing the graphics (the
   pipelines): 1" (a loss: "ico_pc: starts in a row that lost the graphics
   device: 1"); the second in a row is due, and the count stays until
   ico_diag_startup_end_persist writes the setting or finds the device
   table rules that kind (a start that fails before the device is made
   leaves it for the next end).
   Not counted: a restart after a crash report or a device loss
   (relaunched), an end while the app was in the background (its mark,
   ico_diag_startup_activity_background; it breaks the row of its kind).
   Any other ended note breaks the row of the pipelines' ends; a finished
   start-up set or a lazy run's normal exit clears it, and clears the
   pre-pass's row when it built one of its pipelines (a run without the
   pre-pass leaves it).
   ico_diag_startup_end_count: this start's count of a kind, 0 (not
   counted), 1 (the first: the setting for this start only) or 2 (the
   second in a row: written by ico_diag_startup_end_persist). */
#define ICO_DIAG_END_PIPELINES 0
#define ICO_DIAG_END_VU_PRE 1
#define ICO_DIAG_END_LOSS 2
int ico_diag_startup_end_count(int kind);
/* The start-up set's policy for this start (issues 93 and 107), after
   ico_diag_startup_activity_begin (file_lazy: its result) and before the
   renderer starts; setting: [dev] pipeline_set ("full", "careful",
   "lazy", NULL when not set); config_name: the config file's name for the
   lines.
     ICO_DIAG_PIPESET_LAZY_SETTING    setting is "lazy";
     ICO_DIAG_PIPESET_CAREFUL_ONCE    the previous start ended in the
                                      foreground while a pipeline was being
                                      made (not one of the VU vertex
                                      pre-pass's, ico_diag_startup_vu_prepass),
                                      the first in a row
                                      (ico_diag_startup_end_count), and its
                                      file has no "set: careful" line and
                                      no low-memory stop's (it made the
                                      full set, or none): this
                                      start only makes the set under the
                                      careful policy, nothing written; its
                                      line comes after the device is made
                                      (ico_diag_startup_end_persist);
     ICO_DIAG_PIPESET_LAZY_ONCE       the same end, the first in a row, of a
                                      start whose file says "set: careful"
                                      or "set: stopped at the system's
                                      low-memory signal":
                                      this start only makes no set, nothing
                                      written, and the lazy run's notes say
                                      so (the next start tries the set
                                      again); its line likewise;
     ICO_DIAG_PIPESET_LAZY_NOW        the same end, the second in a row
                                      (whatever the ended start's set): the
                                      window writes [dev] pipeline_set =
                                      "lazy" after the device is made
                                      (ico_diag_startup_end_persist);
     ICO_DIAG_PIPESET_LAZY_FILE       file_lazy (the previous run of this
                                      version ended in a lazy run, or exited
                                      lazy, without a "this start only");
     ICO_DIAG_PIPESET_LAZY_LOW_MEMORY the same, the previous run's file
                                      saying its set stopped at the
                                      system's low-memory signal (issue 128,
                                      ico_diag_startup_activity_budget_stop;
                                      not after a restart): no set, the
                                      line kept in this start's file;
     ICO_DIAG_PIPESET_CAREFUL_SETTING setting is "careful" (and none of the
                                      above);
     ICO_DIAG_PIPESET_FULL            the start-up set.
   The device table's careful rule (port/rhi/rhi.h
   RhiLimits.carefulPipelines) is the window's, after the device is made:
   FULL there is the careful set. With setting "lazy" the pipelines' count
   is cleared (a deleted setting starts it again). */
#define ICO_DIAG_PIPESET_FULL 0
#define ICO_DIAG_PIPESET_LAZY_FILE 1
#define ICO_DIAG_PIPESET_LAZY_SETTING 2
#define ICO_DIAG_PIPESET_LAZY_NOW 3
#define ICO_DIAG_PIPESET_LAZY_ONCE 4
#define ICO_DIAG_PIPESET_CAREFUL_ONCE 5
#define ICO_DIAG_PIPESET_CAREFUL_SETTING 6
#define ICO_DIAG_PIPESET_LAZY_LOW_MEMORY 7
int ico_diag_startup_pipeline_set(int relaunched, int file_lazy, const char *setting,
                                  const char *config_name);
/* The VU vertex pre-pass off after an end inside one of its batches or
   pipelines, or of the record path that draws from it
   (startup_retry_note_has_vu_pre):
   this start's ICO_DIAG_END_VU_PRE count. 1: the first in a row, this
   start draws without the pre-pass (rd.h rd_set_vu_prepass_off) and
   nothing is written; 2: the second, the window writes [dev] vu_prepass =
   false after the device is made; both lines come from
   ico_diag_startup_end_persist ("ico_pc: the previous start ended while
   the graphics driver compiled the 3D vertex pre-pass's shaders (<note>);
   this start only draws without it (nothing is written to <config_name>);
   if a start that compiles them ends there again, [dev] vu_prepass = false
   is written for good", then "... this start and every start after it
   draw without it ([dev] vu_prepass = false in <config_name>); delete that
   line to try it again"); 0: neither. */
int ico_diag_startup_vu_prepass(void);
/* After the device is made, for a kind this start counted (else nothing,
   0): the window calls it for the pipelines on ICO_DIAG_PIPESET_CAREFUL_ONCE,
   ICO_DIAG_PIPESET_LAZY_ONCE and ICO_DIAG_PIPESET_LAZY_NOW, for the
   pre-pass's kind when ico_diag_startup_vu_prepass was 1 or 2, and on
   Android for the losses when ico_diag_startup_end_count(ICO_DIAG_END_LOSS)
   was (before the renderer starts). table: a rule that already holds for
   that kind (NULL when none does; the window passes none for the pipelines
   and the pre-pass, and for the losses "[dev] gpu_robust" when the player
   set it). With a rule nothing is written and the count is cleared (the
   setting would be redundant, and would outlive a change of the rule), the
   pipelines' lazy run no longer "for this start only": the first end
   "ico_pc: the previous start ended while preparing the graphics (<note>);
   nothing is counted or written to <config_name>: <rule> (<table>)", the
   second "ico_pc: the second start in a row ended while preparing the
   graphics (<note>); <setting> is not written to <config_name>: <rule>
   (<table>)" (the losses: "ico_pc: the previous start lost the graphics
   device; nothing is counted or written to <config_name>: robust access
   is as the settings say (<table>)"). Without one, the first logs what this
   start does: the pipelines on CAREFUL_ONCE "ico_pc: the previous start
   ended while preparing the graphics (<note>); this start only makes the
   start-up set the careful way (small batches on one thread, the pipeline
   cache written after each; nothing is written to <config_name>); if this
   start also ends while preparing them, [dev] pipeline_set = "lazy" is
   written for good", on LAZY_ONCE "ico_pc: the previous start ended while
   preparing the graphics (<note>); this start only makes the pipelines as
   the game needs them, with no start-up set (nothing is written to
   <config_name>); if this start also ends while preparing them, [dev]
   pipeline_set = "lazy" is written for good", the losses "ico_pc: the
   previous start lost the graphics device; this start only checks the
   bounds of every buffer and image read (robust access; nothing is written
   to <config_name>); if a start loses it again before one runs without a
   loss, [dev] gpu_robust = true is written for good" (and the count's file
   notes that this start ran with it, so its end without a loss leaves the
   row); the second calls persist() to write the setting (0 when it did;
   the count is then cleared) and one line says so ("ico_pc: the previous
   start ended while preparing the graphics (<note>); this start and every
   start after it make the pipelines as the game needs them, with no
   start-up set ([dev] pipeline_set = "lazy" in <config_name>); delete that
   line to try the start-up set again", the losses "ico_pc: the graphics
   device was lost in two starts in a row; this start and every start after
   it check the bounds of every buffer and image read ([dev] gpu_robust =
   true in <config_name>); delete that line to turn it off again"; ", which
   could not be written: this start only" when it failed: the count stays,
   and the pipelines' lazy run says "for this start only"). 1 when
   written. */
int ico_diag_startup_end_persist(int kind, const char *table, int (*persist)(void),
                                 const char *config_name);
/* The app went to the background (background != 0: Android's and iOS's
   will-enter-background, or ico_diag_note_low_memory at a level of 20 or
   more) or came back (0), any thread. The state is kept whether the file
   records or not: a recording that starts with the app in the background
   writes the mark then ("background: the app was in the background
   before preparing the graphics began (with N pipelines made)", logged
   the same way). While the activity file records, the first (checked
   again under the log's lock: one thread sets it) writes the mark "<activity file without .txt>-background.txt"
   (the version, "background: the app went to the background at pipelines
   N of M of the start-up set" or "with N pipelines made", then the
   system's lines that come while there) and logs "ico_pc: the app went to
   the background <where>; an end there is not counted as one while
   preparing the graphics"; the return deletes it ("ico_pc: back in the
   foreground; the background mark is deleted"). The next start reads and
   deletes the mark: an end while preparing the graphics with the mark is
   logged as "ico_pc: the previous run ended while: <note>, in the
   background (<line>); the system ends apps there, so it is not counted as
   an end while preparing the graphics: the start-up set as usual", and the
   mark's system lines the activity file lacks as "ico_pc: the previous
   run was told by the system in the background: <line>". The end of the
   recording deletes the mark. */
void ico_diag_startup_activity_background(int background);
/* The system asked the app to free memory (issue 93: Android's
   onTrimMemory, level its TRIM_MEMORY_* value, -1 when none is known):
   logged at once from any thread ("ico_pc: the system asked the app to
   free memory: low memory (onTrimMemory level 15, running critical;
   signal 1 of this run) at pipelines 201 of 527 of the start-up set;
   memory 102 MB (most 194 MB); device 320 MB available of 7632 MB"
   (ico_diag_memory_text), or "with N pipelines made" outside the
   set, ico_diag_set_pipelines_made; "low memory" for levels 5, 10, 15
   and -1 only, "the app's screen was hidden" for 20, "the app is in the
   background" for 40, 60 and 80), and kept for the activity file as a
   "system: " line among the batches' lines, written by the main thread's
   next ico_diag_startup_activity_poll; the next start logs it ("ico_pc:
   the previous run (version V) was told by the system: ..."). A level of
   20 or more marks the background at once
   (ico_diag_startup_activity_background), and every line while marked
   also goes into the mark from this thread (the main thread may be held
   there).
   Issue 128: level 15 ("running critical"), and level 10 ("running low")
   once the start-up set is past its first batch (ico_diag_set_pipelines'
   count above 0), also ask the set to stop (ico_diag_low_memory_stop);
   the flag is set before the line is logged, from any thread. */
void ico_diag_note_low_memory(int level);
/* The start-up set's loop (rd.h rd_set_low_memory_fn), between two
   batches: 1 when a low-memory signal asked the set to stop
   (ico_diag_note_low_memory), why holding the strongest level's name
   ("running critical", "running low"), else 0. A 1 marks the stop for
   ico_diag_startup_activity_budget_stop. Any thread. */
int ico_diag_low_memory_stop(char *why, size_t size);
/* On the main thread (the start-up screen's progress, the window's pump):
   a low-memory line waiting from ico_diag_note_low_memory goes into the
   activity file while it records. Cheap when none waits. */
void ico_diag_startup_activity_poll(void);
/* The pipelines made so far, for the low-memory line outside the start-up
   set. Any thread. */
void ico_diag_set_pipelines_made(unsigned int n);
/* The note the file held when ico_diag_startup_activity_begin read it,
   whatever its version ("" none).  A batch's note and any note naming a
   pipeline of the VU vertex pre-pass is written through to the disc
   before the batch starts (fsync), so it survives a device that froze and
   was rebooted. */
const char *ico_diag_startup_activity_prev(void);
/* This start's set is made under the careful policy (on != 0; the window,
   before ICO_DIAG_ACTIVITY_STARTUP): while the file records it holds the
   line "set: careful" after the note, so an end inside that set makes the
   next start lazy rather than careful again (ico_diag_startup_pipeline_set).
   Cleared when the recording stops. */
void ico_diag_startup_activity_careful(int on);
/* The careful set stopped at its memory budget (rd_precreate_pipelines
   called rd_pipelines_lazy): the set's row of ends is cleared as at its
   finish, and the rest of the run records as a lazy run for this start
   only (ICO_DIAG_ACTIVITY_LAZY, its notes and exit saying "for this start
   only", the "set: careful" line kept), so a freeze in a later creation is
   named and counted while the next start tries the set again.
   A set stopped at the system's low-memory signal (issue 128;
   ico_diag_low_memory_stop answered 1) records the rest of the run as a
   lazy run that is not for this start only, with the line "set: stopped
   at the system's low-memory signal (running critical)" after the note
   (and the careful line), and logs "ico_pc: the start-up set stopped at
   the system's low-memory signal (running critical); the next start
   makes the pipelines as the game needs them, with no start-up set". The
   next start of the same version that finds the line beside the lazy
   run's exit or its note between two creations logs "ico_pc: the previous
   start stopped preparing the graphics when the system ran low on memory
   (<note>); this start prepares them as the game needs them" and is lazy
   (ICO_DIAG_PIPESET_LAZY_LOW_MEMORY), its own file keeping the line; one
   ended inside a creation logs "ico_pc: the previous start had stopped
   preparing the graphics when the system ran low on memory (<line>)" and
   counts as an end while preparing the graphics, stepping from no set
   (ICO_DIAG_PIPESET_LAZY_ONCE, then LAZY_NOW). Another version tries the
   set again. */
void ico_diag_startup_activity_budget_stop(void);
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
   start stays lazy; "... for this start only (none was being created)"
   in a run whose policy was ICO_DIAG_PIPESET_LAZY_ONCE, so the next start
   tries the set again). 0: stops recording and deletes the file (the
   start-up set's end, which clears the count of ends in a row,
   ico_diag_startup_end_count). */
#define ICO_DIAG_ACTIVITY_STARTUP 1
#define ICO_DIAG_ACTIVITY_LAZY 2
void ico_diag_startup_activity_record(int mode);
/* A clean exit: stops recording; a lazy run keeps the file (its line
   saying the run exited lazy, which the next start logs as "ico_pc: the
   previous run created the pipelines as the frames needed them; this one
   does too"), so the lazy decision sticks until the version changes (a
   lazy run for one start only keeps one saying so, and the next start
   tries the set again); any other run deletes it. Either clears the count
   of ends in a row. */
void ico_diag_startup_activity_exit(void);
/* A device loss in this run (window_host.c device_lost_quit), after its
   loss dump was written (dump_zip: its path; NULL or "": none was): the
   activity file gets a third line, "loss dump: <zip>" (or one saying none
   was written), at once and kept through the run's end, whatever the
   file's note and mode; the next start's ico_diag_startup_activity_begin
   logs it and, when it names a dump and the version is the same, keeps
   the path for the loss bisect (ico_diag_loss_dump). Called during a GPU
   hang (ico_diag_set_gpu_hang) it writes the hang's own line instead,
   which is counted but never bisected. */
void ico_diag_note_device_loss(const char *dump_zip);
/* The loss dump the previous run left for this one (NULL: none). */
const char *ico_diag_loss_dump(void);
/* The loss bisect (docs/port/RENDER.md, "The loss bisect"), once, before
   the game: with a loss dump pending, the activity file's note says the
   bisect is running (so a run ended inside it does not bisect again at the
   next start), bisect(zip, line, size, user) replays the dump and returns
   one of the ICO_DIAG_BISECT_* with line saying what it found, which is
   logged as "ico_pc: loss bisect: <line>", and the file is deleted (the
   bisect is done; the start that follows writes its own).  -1 when there
   was nothing to bisect, else what bisect returned. */
#define ICO_DIAG_BISECT_NOT_RUN 0
#define ICO_DIAG_BISECT_LOST 1
#define ICO_DIAG_BISECT_CLEAN 2
int ico_diag_loss_bisect(int (*bisect)(const char *dump_zip, char *line, size_t line_size,
                                       void *user),
                         void *user);
/* The loss bisect's second level (docs/port/RENDER.md, "The loss bisect"):
   when the first level ends on a pass with draws, the draw within it, a
   step at a time (port/render/rd_bisect_plan.h: the marks masked, the
   prefix search, the draw alone, its classes), each step one replay of the
   lost frame on a renderer of its own, its verdict written to the state
   file logs/loss-bisect.txt before the next.  A start that ends inside a
   step (the device or the system ended it) continues at the next start:
   the step without a verdict counts as killed (lost).  Bounds: 16 steps,
   6 starts (a draw early in its pass takes about 12 lost steps of the
   prefix search, so the alone and class steps need a fifth), budget_ms of steps a start (the rest at the next start), and
   ICO_DIAG_BISECT2_LOSSES lost steps a start, so a start loses the device
   at most 4 times with the first level's loss (Windows restarts the
   whole system after 5 graphics resets in 60 s, its TdrLimitCount and
   TdrLimitTime defaults): the next step waits for the next start
   ("ico_pc: loss bisect: 3 lost steps in this start; step 5 waits for the
   next start"); the file is deleted at the end or when a bound trips.
   Logged: one line a
   step ("ico_pc: loss bisect: step 4 of 16: pass 1, draws 1..656 of 1312:
   lost in 0.6 s"), "continuing at step 7 (start 2 of 6; step 6 ended the
   run)", and one final line naming the draw and the verdict.

   The step function (number: the step's, 1 .. 16, for the progress
   screen) replays the frame of dump_zip with passes 1 .. pass - 1 and pass
   `pass`'s draws step->drawFirst .. drawLast through
   step->filter, and returns ICO_DIAG_BISECT2_LOST or _CLEAN with the note
   of the step's last draw (key and text, note_size bytes) and the wall time
   in *ms; ICO_DIAG_BISECT2_NOINIT when its renderer did not start (this
   start stops; the step is planned again at the next); _NOT_RUN when the
   dump cannot be replayed (the bisect ends). */
#define ICO_DIAG_BISECT2_NOT_RUN 0
#define ICO_DIAG_BISECT2_LOST 1
#define ICO_DIAG_BISECT2_CLEAN 2
#define ICO_DIAG_BISECT2_NOINIT 3
#define ICO_DIAG_BISECT2_STARTS 6
#define ICO_DIAG_BISECT2_LOSSES 3

struct RdBisectStep;
struct RdBisectKey;
typedef int (*IcoBisect2StepFn)(const char *dump_zip, uint32_t pass, uint32_t number,
                                const struct RdBisectStep *step, struct RdBisectKey *key,
                                char *note, size_t note_size, double *ms, void *user);

/* The state file's path (logs/loss-bisect.txt); before either call below. */
void ico_diag_loss_bisect2_path(const char *path);
/* 1 when the state file holds a bisect of this version to continue (read
   as untrusted: a file that does not parse, or of another version, is
   deleted and logged). */
int ico_diag_loss_bisect2_pending(void);
/* Starts the second level on pass `pass` (draws of passes) of dump_zip's
   lost frame, as the first level found it, or (dump_zip NULL) continues
   the pending one; runs steps until the end, a bound, the budget or a
   step's NOINIT. Returns ICO_DIAG_BISECT2_NOINIT when a step's renderer
   did not start, ICO_DIAG_BISECT2_NOT_RUN when no step ran, else
   ICO_DIAG_BISECT2_LOST when a step of this start lost the device (the
   caller's renderer starts after a loss), else ICO_DIAG_BISECT2_CLEAN. */
int ico_diag_loss_bisect2(const char *dump_zip, uint32_t pass, uint32_t draws, uint32_t passes,
                          IcoBisect2StepFn step, void *user, double budget_ms);
/* The relaunch's inputs: the step this run is at (StartupRetry, 0 for the
   first run), whether the build has the D3D12 backend, whether the player
   named the backend, whether the player named the blending (blend= /
   [dev] blend or ICO_RD_BLEND), and the backend in use; the window sets
   them before the renderer starts. */
/* Whether the run draws with the VU vertex pre-pass (after its self-test,
   rd.h rd_vuprepass_selftest): a device loss then takes the loss order's
   lossnoprepass step first (startup_retry.h StartupRetryRun.prepassWasOn).
   0 until set. */
void ico_diag_set_vu_prepass(int on);
/* Whether the run draws with robust access off by the device's default
   (port/rhi/rhi.h RhiLimits.robustDefaultOff), not by
   gpu_robust or ICO_GPU_ROBUST: a device loss then takes the loss order's
   robust step (startup_retry.h StartupRetryRun.robustOffDefault). The
   window sets it once the device is made; 0 until set. */
void ico_diag_set_robust_off_default(int on);
void ico_diag_set_startup_retry(int step, int have_d3d12, int backend_explicit, int blend_explicit,
                                const char *backend);
/* The renderer the player chose from the menu (Options > Graphics > Apply,
   window_switch.c): backend_explicit and backend as ico_diag_set_startup_retry
   sets them, the step and the rest kept (Keep: the choice named; a switch
   back: the start's own values). */
void ico_diag_set_backend_choice(int backend_explicit, const char *backend);
/* The renderer on trial between Apply and Keep (window_switch.c), NULL or
   "" when none: a driver crash while preparing the shaders or a device loss
   then writes the report and plans no restart (the settings file still
   names the renderer that ran before, which the next start uses), and says
   so in the log. */
void ico_diag_set_backend_trial(const char *backend);
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
   Linux inside an AppImage (host_fs.h ico_appimage_file trusts APPIMAGE):
   the program started again is the AppImage file, which mounts it anew
   (the mount of this run ends with the execve that closes the runtime's
   keep-alive pipe). Android and iOS: nothing (no relaunch). */
void ico_diag_prepare_relaunch(int argc, char **argv);
/* Tests: the program a relaunch starts (Linux and macOS, once prepared);
   NULL before ico_diag_prepare_relaunch, on Windows (a wide path), Android
   and iOS. */
const char *ico_diag_relaunch_program(void);
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

/* --- The symbols, the summary line and the report file ---------------------- */

/* The crash and watchdog reports name every offset they print
   (ico_pc_x64.exe+0x1b35e4) after their raw lines, from the program's
   symbol file (symmap.h), read only then:
     ico_pc: symbols from ico_pc_x64.map (each offset above in ico_pc_x64.exe, in order):
     ico_pc:   +0x1b35e4 _CopyVector+0x4 (vu0.c.obj)
   The file is the link map beside the program by default (its name with
   ".exe" replaced by ".map": ico_pc_x64.map, ico_pc.map), opened at
   ico_diag_init; a map of another build is detected (ico_diag_log must
   come out at +0x0) and not used. set_symbols_path replaces it (a ".gz"
   path is read as gzip; tests); set_symbols_fd (Android: the APK's stored
   assets/libmain.syms.gz, AAsset_openFileDescriptor64) reads length bytes
   from offset of fd (length -1: to the end), name for the lines. */
void ico_diag_set_symbols_path(const char *path);
#ifndef _WIN32
void ico_diag_set_symbols_fd(int fd, long long offset, long long length, int gzip,
                             const char *name);
#endif

/* The first screenful's one line of what a report needs (the window,
   after the renderer's start and its self-tests): "ico_pc: summary:
   <text>", kept for the report file's report.txt. */
void ico_diag_set_summary(const char *fmt, ...) ICO_DIAG_PRINTF(1, 2);
/* The disc this run plays, as the summary and the report file name it
   ("SCPS-11003 (the Japanese disc)"; copied, at most 95 bytes), set once
   the disc image is identified (main_host.c select_disc). A summary
   logged before that (Android opens the window first) names the disc as
   ICO_DIAG_DISC_UNKNOWN; it is logged again with the disc in its place.
   ico_diag_disc: the text, "" before. */
#define ICO_DIAG_DISC_UNKNOWN "not mounted yet"
void ico_diag_set_disc(const char *text);
const char *ico_diag_disc(void);
/* The version text ico_diag_set_version took ("unknown" before). */
const char *ico_diag_version(void);

/* The report file (report.h, logs/report-<date>-<time>.zip): written by
   the crash and watchdog reports themselves, and by this on the normal
   paths that end a run abnormally (a lost graphics device, graphics that
   could not start); what says which. Logged ("ico_pc: report: <name> in
   the logs folder ..."), the oldest beyond ICO_REPORT_KEEP deleted. The
   zip's file name, or NULL when none was written. */
const char *ico_diag_write_report(const char *what);
/* The last report's file name (NULL: none yet), for a box. */
const char *ico_diag_report_name(void);

/* --- Rate-limited lines ----------------------------------------------------- */

/* One helper for every line that may repeat (a per-frame or per-event
   condition): the first ICO_LOG_LIMIT_FIRST of a key are written, then
   every ICO_LOG_LIMIT_EVERY-th, each counted; the 8th says so ("8 of these
   so far; from here every 100th is written"), a later one its number; the
   end of the run (normal or fatal) writes one line with every key's count
   ("ico_pc: limited lines (key, count, written): rd: snap 412 (11
   written); ..."). The hot path is an atomic increment and a compare: the
   arguments are evaluated and the line formatted only when it is written.
     ico_log_limited("rd: interp snap", "rd: interp: snapped %s at tick %u", why, tick);
   key: a string literal naming the line (the summary's). Any thread.
   ico_log_limited_n(key, first, every, ...): the same with a key's own
   bounds (the first `first`, then every `every`-th), for a line that
   must stay in the log far longer (SDL's error lines: 64 and 1000). */
#define ICO_LOG_LIMIT_FIRST 8u
#define ICO_LOG_LIMIT_EVERY 100u

typedef struct IcoLogLimit {
    const char *key;
    unsigned int count;   /* every time the line's condition came */
    unsigned int written; /* the lines written */
    struct IcoLogLimit *next;
    unsigned int first, every; /* 0: ICO_LOG_LIMIT_FIRST, ICO_LOG_LIMIT_EVERY */
} IcoLogLimit;

static inline unsigned int ico_log_limit_first(const IcoLogLimit *l)
{
    return l->first != 0u ? l->first : ICO_LOG_LIMIT_FIRST;
}

static inline unsigned int ico_log_limit_every(const IcoLogLimit *l)
{
    return l->every != 0u ? l->every : ICO_LOG_LIMIT_EVERY;
}

static inline int ico_log_limit_take(IcoLogLimit *l)
{
    const unsigned int c = __atomic_add_fetch(&l->count, 1u, __ATOMIC_RELAXED);
    return c <= ico_log_limit_first(l) || c % ico_log_limit_every(l) == 0;
}

void ico_log_limit_write(IcoLogLimit *l, const char *fmt, ...) ICO_DIAG_PRINTF(2, 3);

#define ico_log_limited(key, ...)                                                                  \
    do {                                                                                           \
        static IcoLogLimit ico_ll_ = {key, 0u, 0u, 0, 0u, 0u};                                     \
        if (ico_log_limit_take(&ico_ll_)) {                                                        \
            ico_log_limit_write(&ico_ll_, __VA_ARGS__);                                            \
        }                                                                                          \
    } while (0)

#define ico_log_limited_n(key, first, every, ...)                                                  \
    do {                                                                                           \
        static IcoLogLimit ico_ll_ = {key, 0u, 0u, 0, first, every};                               \
        if (ico_log_limit_take(&ico_ll_)) {                                                        \
            ico_log_limit_write(&ico_ll_, __VA_ARGS__);                                            \
        }                                                                                          \
    } while (0)

/* Every key written so far as "key count (written written)", "; "
   between, into out; the number of keys. */
int ico_log_limit_summary(char *out, size_t size);
/* That as one log line, when there is any key (the run's end). */
void ico_log_limit_log_summary(void);

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
