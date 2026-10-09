/*
 * port/platform/android/gpu_driver_android.h
 *
 * The Android build's own graphics driver (v0.4.3, package AN-22a, issue
 * 22): a community driver package for Adreno GPUs (Turnip and others,
 * port/platform/gpu_driver.h) the player added on the Settings page,
 * loaded through libadrenotools in place of the phone's driver.
 *
 * The start (window_host.c, before rd_Init):
 *   ico_gpu_driver_android_start reads [video] gpu_driver (empty: the
 *   phone's own driver).  When the trial marker (gpu_driver.h) still names
 *   that driver, the last start died inside it: [video] gpu_driver_failed
 *   takes its name, gpu_driver is emptied, the config saved and a message
 *   box shown; the phone's driver is used.  Else the marker is written and
 *   the driver opened (adrenotools_open_libvulkan), its
 *   vkGetInstanceProcAddr handed to rhi_SetVulkanLoader; a driver that
 *   does not open is treated as failed the same way.
 *   ico_gpu_driver_android_init_failed: rd_Init failed with that driver;
 *   recorded as failed, the box shown, the loader back to the system's (the
 *   caller runs rd_Init again).
 *   ico_gpu_driver_android_presented after each present: the marker goes
 *   after 300 (the pipelines made, a few seconds drawn).
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

#include "settings.h"

/* 1 when the player's driver is in use for this start (rd_Init next), 0
   for the phone's own. */
int ico_gpu_driver_android_start(void);
/* rd_Init failed with the player's driver (start returned 1). */
void ico_gpu_driver_android_init_failed(void);
/* After each present that reached the screen. */
void ico_gpu_driver_android_presented(void);
/* The Settings page's host; valid for the program's life. */
const UiGpuDriverHost *ico_gpu_driver_android_host(void);

#endif
