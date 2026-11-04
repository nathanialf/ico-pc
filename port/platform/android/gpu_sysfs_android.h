/*
 * port/platform/android/gpu_sysfs_android.h
 *
 * The kernel GPU driver's nodes read on Android (gpu_sysfs.h has what they
 * are and the pure part): probed once, then sampled every 250 ms on the
 * watchdog thread (diag_host.h ico_diag_set_poll_hook) into a ring the
 * window reads on its own thread under a lock held only for the copy.
 */
#ifndef ICO_PLATFORM_GPU_SYSFS_ANDROID_H
#define ICO_PLATFORM_GPU_SYSFS_ANDROID_H

#include <stdbool.h>
#include "gpu_sysfs.h"

/* The nodes probed (the first call; later calls give the same answer) and,
   when any sampled node is readable, the sampling started. Main thread. */
void gpu_sysfs_android_start(GpuSysfsInfo *info);
/* The ring's samples after sinceMs, and around [t0Ms, t1Ms] (gpu_sysfs.h
   gpu_sysfs_ring_block, gpu_sysfs_ring_around); false when nothing is
   sampled (no readable node, not started) */
bool gpu_sysfs_android_block(double sinceMs, GpuSysfsBlock *out);
bool gpu_sysfs_android_around(double t0Ms, double t1Ms, GpuSysfsAround *out);

#endif /* ICO_PLATFORM_GPU_SYSFS_ANDROID_H */
