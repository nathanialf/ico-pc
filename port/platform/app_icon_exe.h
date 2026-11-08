/*
 * port/platform/app_icon_exe.h
 *
 * The icon on the Windows program file (docs/port/APP_ICON.md, "The exe's
 * icon"): a running program cannot rewrite its own file, so the game starts
 * tools\ico_icon_stamp.exe as it exits, and that helper writes the .ico the
 * title screen's capture made into the exe's icon resources once the exe
 * is free.  The exe carries the stamp (RT_RCDATA "ICO_ICON_STAMP", the
 * icon's label and disc: icon_stamp.h) the next start reads.
 *
 * Windows only (app_icon_exe.c is compiled into the Windows window build
 * alone); main_host.c calls both from open_window.
 */
#ifndef ICO_PLATFORM_APP_ICON_EXE_H
#define ICO_PLATFORM_APP_ICON_EXE_H

#ifdef __cplusplus
extern "C" {
#endif

/* At start: reads the exe's own stamp and logs it ("the exe carries the
 * icon (stamp S)" or "the exe has no icon yet"), and the helper's last line
 * from logs\icon-stamp.log when there is one. */
void ico_app_icon_exe_start(void);

/* An end-of-run step (main_host.c at_shutdown, registered before the
 * window's own so it runs after the window has closed): when the current
 * disc's .ico exists under <user folder>/icon/ and its stamp ("<.txt>
 * <disc>") differs from the exe's, starts tools\ico_icon_stamp.exe beside
 * the exe, detached, and logs it.  Runs once a run. */
void ico_app_icon_exe_at_exit(void);

#ifdef __cplusplus
}
#endif
#endif /* ICO_PLATFORM_APP_ICON_EXE_H */
