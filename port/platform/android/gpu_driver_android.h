/*
 * port/platform/android/gpu_driver_android.h
 *
 * The Android build's own graphics driver (issue 22): a community driver package for Adreno GPUs (Turnip and others,
 * port/platform/gpu_driver.h) the player added on the Settings page,
 * loaded through libadrenotools in place of the phone's driver.
 *
 * The start (window_host.c, before rd_init):
 *   ico_gpu_driver_android_start reads [video] gpu_driver (empty: the
 *   phone's own driver).  When the trial marker (gpu_driver.h) still names
 *   that driver, the last start died inside it: [video] gpu_driver_failed
 *   takes its name, gpu_driver is emptied, the config saved and a message
 *   box shown; the phone's driver is used.  Else the marker is written and
 *   the driver opened (adrenotools_open_libvulkan), its
 *   vkGetInstanceProcAddr handed to rhi_set_vulkan_loader; a driver that
 *   does not open is treated as failed the same way.
 *   ico_gpu_driver_android_init_failed: rd_init failed with that driver;
 *   recorded as failed, the box shown, the loader back to the system's (the
 *   caller runs rd_init again).
 *   ico_gpu_driver_android_presented after each present: the marker goes
 *   after 300 (the pipelines made, a few seconds drawn).
 *   ico_gpu_driver_android_end at a normal end (the window's close among
 *   the end-of-run steps): the marker goes too, since the driver did not
 *   crash; a crash or a fatal error (_exit) skips it and leaves the marker.
 *
 * The start-up choice (window_host.c, when no driver started and there is
 * no device, so the Settings page cannot be reached): a box offers a
 * driver package from the system's file picker
 * (ico_gpu_driver_android_choose: installed, chosen and saved as on the
 * Settings page, no trial marker left; the caller asks the player to start
 * the game again, since a package cannot be loaded after the phone's own
 * driver in the same process) or Quit.
 *
 * ico_gpu_driver_android_host: the Settings page's host (settings.h
 * UiGpuDriverHost): the installed drivers (<files>/drivers/ in the app's
 * internal folder, the only place the system lets the app load a library
 * from), the choice ([video] gpu_driver; a new choice clears
 * gpu_driver_failed), adding one from the system's file picker (copied on
 * a thread, then unpacked and checked), removing one.
 *
 * Compiled into libmain.so only.
 */
#ifndef ICO_PLATFORM_GPU_DRIVER_ANDROID_H
#define ICO_PLATFORM_GPU_DRIVER_ANDROID_H

#include <stddef.h>
#include "settings.h"

/* 1 when the player's driver is in use for this start (rd_init next), 0
   for the phone's own. */
int ico_gpu_driver_android_start(void);
/* rd_init failed with the player's driver (start returned 1). */
void ico_gpu_driver_android_init_failed(void);
/* The start-up choice: the system's file picker, waited for with SDL's
   events pumped (nothing drawn).  1 with the installed package's folder
   in folder, chosen and the config saved for the next start, the trial
   marker cleared; 0 with a sentence for the player in why (not a package,
   no space, no file chosen). */
int ico_gpu_driver_android_choose(char *folder, size_t n, char *why, size_t whyn);
/* After each present that reached the screen. */
void ico_gpu_driver_android_presented(void);
/* At a normal end, before the renderer shuts down: a trial still running
   ends as passed (the marker removed). */
void ico_gpu_driver_android_end(void);
/* The Settings page's host; valid for the program's life. */
const UiGpuDriverHost *ico_gpu_driver_android_host(void);

#endif
