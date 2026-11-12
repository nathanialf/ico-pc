/*
 * port/platform/window_capture.h
 *
 * A release candidate's frames of the opening, the window's side
 * (capture_trigger.h decides when; ICO_RC_TOOLS on Android alone: the
 * source and every call are inside "#if ICO_RC_TOOLS && defined(__ANDROID__)",
 * the root CMakeLists.txt adds window_capture.c and capture_trigger.c to the
 * program under "if(ICO_RC_TOOLS AND ANDROID)" alone, and
 * tools/check_rc_text.py --selftest (the rc_tools_gate ctest) checks both).
 * Kept out of window_host.c, at its file size gate: window_host.c calls
 *   window_capture_start     where the Share a bug report row is installed
 *   window_capture_picture   for each replay's record (perf_drain)
 *   window_capture_due       at the top of report_picture_pump; on 1 it runs
 *                            frame_dump(2) (the bug-report picture's dump
 *                            without the automatic zips' bundle), then
 *   window_capture_taken     the line
 *   window_capture_run_start the share's "this run's zips" (share_report.h)
 * All on the window's thread (the host loop's: the pump and the pace).
 */
#ifndef ICO_PLATFORM_WINDOW_CAPTURE_H
#define ICO_PLATFORM_WINDOW_CAPTURE_H

#ifndef ICO_RC_TOOLS
#define ICO_RC_TOOLS 0
#endif

#if ICO_RC_TOOLS && defined(__ANDROID__)
struct RdPerfRecord;

/* The run's start: the trigger reset, the run's start time noted and the
   line "window: capture: on (a release candidate): ..."; once a process (a
   later call does nothing) */
void window_capture_start(void);
/* One replay's record (rd_perf_pop): its glow light plan and frame, with
   the game's stage and menu screen as they are now */
void window_capture_picture(const struct RdPerfRecord *r);
/* 1 when the pump takes an armed dump now (the trigger's moment noted, the
   frame number read): the caller runs frame_dump(2) and then
   window_capture_taken.  0 otherwise. */
int window_capture_due(void);
/* After frame_dump: the line naming the moment, the frame and the zip
   (found by the names frame_dump gives it), and the 2 s count's start */
void window_capture_taken(void);
/* The run's start in Unix seconds (window_capture_start's time; -1 before
   it): the zips of the dumps folder modified since are this run's */
long long window_capture_run_start(void);
#endif

#endif /* ICO_PLATFORM_WINDOW_CAPTURE_H */
