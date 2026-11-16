/*
 * port/platform/android/last_run_android.h
 *
 * Android 11 and later: last-run.txt's summary (last_run.h) handed to
 * ActivityManager.setProcessStateSummary at each phase change, through
 * IcoActivity.processStateSummary(byte[]), so the system's exit record of
 * the process (ApplicationExitInfo.getProcessStateSummary) carries where
 * the run was even when the app's files are lost.
 */
#ifndef ICO_PLATFORM_LAST_RUN_ANDROID_H
#define ICO_PLATFORM_LAST_RUN_ANDROID_H

/* The hook set (last_run_set_summary_hook) and the current summary handed
   over once; called at the window's open (gpu_sysfs_android_start) */
void last_run_android_start(void);

#endif /* ICO_PLATFORM_LAST_RUN_ANDROID_H */
