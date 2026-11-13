/*
 * port/platform/window_bench.h
 *
 * A release candidate's bench, the window's side (docs/port/
 * ANDROID_DIAGNOSTICS.md, "The bench"; bench_plan.h decides which pictures
 * are kept, port/render/rd_bench_run.h runs them).  ICO_RC_TOOLS on Android
 * alone: the source and every call are inside "#if ICO_RC_TOOLS &&
 * defined(__ANDROID__)", the root CMakeLists.txt adds window_bench.c,
 * bench_plan.c and rd_bench_run.c to the program under
 * "if(ICO_RC_TOOLS AND ANDROID)" alone, and tools/check_rc_text.py
 * --selftest (the rc_tools_gate ctest) checks both.  And off even there
 * unless the build's WINDOW_BENCH_DEFAULT is 1 (a variant's define, as
 * window_capture.c's WINDOW_CAPTURE_DEFAULT).  window_host.c calls
 *   window_bench_start    where the Share a bug report row is installed
 *   window_bench_picture  for each replay's record (perf_drain)
 *   window_bench_pump     in the pump, beside share_report_pump: keeps an
 *                         armed frame, and runs the bench at the title
 *                         (the opening's frames) or at a pause (the paused
 *                         picture), holding the pump until it ends
 * All on the window's thread (the host loop's).
 */
#ifndef ICO_PLATFORM_WINDOW_BENCH_H
#define ICO_PLATFORM_WINDOW_BENCH_H

#ifndef ICO_RC_TOOLS
#define ICO_RC_TOOLS 0
#endif

#if ICO_RC_TOOLS && defined(__ANDROID__)
struct RdPerfRecord;

/* the start line, once a process */
void window_bench_start(void);
/* one replay's record: its plan, volume and frame, the opening or not */
void window_bench_picture(const struct RdPerfRecord *r);
/* the pump's turn: keep, or run (the bench holds the pump while it runs) */
void window_bench_pump(void);
#endif

#endif /* ICO_PLATFORM_WINDOW_BENCH_H */
