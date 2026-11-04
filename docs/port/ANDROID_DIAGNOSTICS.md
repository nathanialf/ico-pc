# Android diagnostics

What the Android build adds to the log so a stutter's cause can be read off one log (issue 123):
the kernel GPU driver's state sampled around each present that blocked, and the thermal state and
screen recording the app's Java side reports. The window's lines that use them are in
[DISPLAY.md](DISPLAY.md#the-stats-lines); the watchdog and the heartbeat are in
[PLATFORM.md](PLATFORM.md#diagnostics). Nothing here runs on other platforms.

| File | What it holds |
| --- | --- |
| `port/platform/gpu_sysfs.c`, `gpu_sysfs.h` | the kernel GPU driver's nodes, the pure part: the parser, the sample ring, the block and around reductions, the lines (`gpu_sysfs_test`, every platform) |
| `port/platform/android/gpu_sysfs_android.c`, `gpu_sysfs_android.h` | the probe, the sampler thread and the ring's lock |
| `port/platform/thermal_text.c`, `thermal_text.h` | the thermal status names and the thermal and screen recording lines (`thermal_text_test`, every platform) |
| `port/platform/android/host_android.c` | `ico_host_thermal` and the two native methods the Java listeners call |
| `port/platform/window_stall.c`, `window_stall.h` | the window's side: the open line, the block lines, the around lines |
| `android/app/src/main/java/com/defnf/icopc/IcoActivity.java` | the thermal getters and the two listeners |

## The kernel GPU driver's sampler

On Android a small thread of its own (`ico-gpu-sysfs`) reads the kernel GPU driver's nodes every
250 ms (`GPU_SYSFS_PERIOD_MS`) under `/sys/class/kgsl/kgsl-3d0/` (the clock from `gpuclk`,
`clock_mhz` or the devfreq node's `cur_freq`, `gpu_busy_percentage`, `reset_count`,
`thermal_pwrlevel`, `temp`), each read by open, read and close as `ico_diag_device_memory` reads
`/proc/meminfo`, into a ring of 64 samples on the monotonic clock the renderer's records use
(`port/platform/android/gpu_sysfs_android.c`, `port/platform/gpu_sysfs.h`). The window probes the
nodes once at its open (`window_stall_gpu_open`), writes which were readable (some devices' SELinux
policy denies apps these nodes: `not readable (EACCES)`), and starts the thread only when a sampled
node is readable; it stops and joins it at its close (`window_stall_gpu_close`, also on an open that
fails after the probe) and starts it again at a later open. While the app is in the background (the
watchdog paused, `ico_diag_watchdog_paused`) the thread reads nothing. The window reads the ring on
its own thread under a lock held only for the copy. The lines are in
[DISPLAY.md](DISPLAY.md#the-stats-lines). Nothing is sampled elsewhere.

The reads are not on the watchdog's thread ([PLATFORM.md](PLATFORM.md#diagnostics)) on purpose: a
node that blocks (a KGSL or devfreq node behind a lock the driver holds through a fault's snapshot
and recovery, or a hung GPU, the moments the sampler is there to watch) would stop the GPU hang
limit and the Main-tick limit while it blocked, and a block over the watchdog's 5 s sleep gap would
then be taken as system sleep and subtracted from both. On its own thread such a block only delays
the samples; the window's lines then show the gap, and the window's close waits for the read
before it joins the thread.

## The thermal state and screen recording

The thermal state and screen recording come from the app's Java side (issue 123;
`port/platform/thermal_text.h`, `port/platform/android/host_android.c`,
`android/app/src/main/java/com/defnf/icopc/IcoActivity.java`). Two static Java methods answer the
native side's asks (`ico_host_thermal`, modeled on `ico_host_vibrate`: cached method ids, one failed
lookup remembered, local references deleted, exceptions cleared): `thermalHeadroom()`
(`PowerManager.getThermalHeadroom(0)`, Android 11; NaN otherwise or when the system is asked too
often, so the native side asks it at most once a second and reuses that answer for the lines
written in between) and `thermalStatus()` (`getCurrentThermalStatus`, Android 10; -1 otherwise). Two listeners
push the changes into `SDL_AtomicInt`s and log them at once, on the activity's thread
(`nativeThermalStatus`, an `OnThermalStatusChangedListener`; `nativeScreenRecording`, on Android 15
and later `WindowManager.addScreenRecordingCallback` with the `DETECT_SCREEN_RECORDING` permission,
a normal one). Both are added in `onStart` and removed in `onStop`, every API behind
`Build.VERSION.SDK_INT`, every native call wrapped as `nativeTrimMemory` is. The getters must run on
SDL's main thread, which is where the window writes its block and around lines. The around
lines come only where the kernel GPU driver's nodes are sampled (above), so on a phone that denies
them the thermal and recording words are in the block line and the change lines only. A Java side without
the methods costs one failed lookup; the block line then says `thermal: not available (...)` once.
What the bridge cannot read: SurfaceFlinger's composition type (whether the system composites the
window or scans it out directly) and the overlays some vendor builds (HyperOS) add are not available
to an app, so a recorder or overlay that does not announce itself through the Android 15 callback
shows nowhere in the log. Lines: `ico_pc: thermal status changed: MODERATE (2) -> SEVERE (3) (run
time 212.4 s)` (the first one, at registration, is from `UNKNOWN (-1)`), `ico_pc: screen recording:
visible (run time 40.1 s)`, and the window's, in [DISPLAY.md](DISPLAY.md#the-stats-lines).
