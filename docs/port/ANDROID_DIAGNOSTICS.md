# Android diagnostics

What the Android build adds to the log so a stutter's cause can be read off one log (issue 123):
the kernel GPU driver's state sampled around each present that blocked, and the thermal state and
screen recording the app's Java side reports. The window's lines that use them are in
[DISPLAY.md](DISPLAY.md#the-stats-lines); the watchdog and the heartbeat are in
[PLATFORM.md](PLATFORM.md#diagnostics). Nothing here runs on other platforms.

| File | What it holds |
| --- | --- |
| `port/platform/gpu_sysfs.c`, `gpu_sysfs.h` | the kernel GPU driver's nodes, the pure part: the parser, the devfreq entry's name rule, the sample ring with the CPU cores' clocks, the block and around reductions, the lines (`gpu_sysfs_test`, every platform) |
| `port/platform/android/gpu_sysfs_android.c`, `gpu_sysfs_android.h` | the probe (KGSL, else the GPU's devfreq entry, else the candidates' paths; the cores' `scaling_cur_freq`), the sampler thread and the ring's lock |
| `port/platform/thread_stat.c`, `thread_stat.h` | a thread's time on a core and waiting for one from the kernel's `schedstat` (`thread_stat_test`; Linux and Android) |
| `port/platform/cpu_place.c`, `cpu_place.h` | the game thread's CPU time (`cpu_place_thread_cpu_ns`) and the threads line (`cpu_place_threads_format`; `cpu_place_test`) |
| `port/platform/thermal_text.c`, `thermal_text.h` | the thermal status names and the thermal and screen recording lines (`thermal_text_test`, every platform) |
| `port/platform/android/host_android.c` | `ico_host_thermal` and the two native methods the Java listeners call |
| `port/platform/window_stall.c`, `window_stall.h` | the window's side: the open line, the block lines, the around lines |
| `android/app/src/main/java/com/defnf/icopc/IcoActivity.java` | the thermal getters and the two listeners |

## The kernel GPU driver's sampler

On Android a small thread of its own (`ico-gpu-sysfs`) reads the kernel GPU driver's nodes every
250 ms (`GPU_SYSFS_PERIOD_MS`) under `/sys/class/kgsl/kgsl-3d0/` (the clock from `gpuclk`,
`clock_mhz` or the devfreq node's `cur_freq`, `gpu_busy_percentage`, `reset_count`,
`thermal_pwrlevel`, `temp`; on a phone without KGSL the GPU's devfreq entry and the candidates
below), and the CPU cores' clocks beside them, each read by open, read and close as `ico_diag_device_memory` reads
`/proc/meminfo`, into a ring of 64 samples on the monotonic clock the renderer's records use
(`port/platform/android/gpu_sysfs_android.c`, `port/platform/gpu_sysfs.h`). The window probes the
nodes once at its open (`window_stall_gpu_open`), writes which were readable (some devices' SELinux
policy denies apps these nodes: `not readable (EACCES)`), and starts the thread only when a sampled
node or a CPU clock is readable; it stops and joins it at its close (`window_stall_gpu_close`, also on an open that
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

### A phone without KGSL, and the CPU clocks

Issue 179's phone (Motorola G24 Power: MediaTek Helio G85, Mali-G52 MC2) logged only `window:
kernel GPU driver: /sys/class/kgsl/kgsl-3d0 not present`: KGSL is Qualcomm's. Without it the
probe takes the `/sys/class/devfreq` entry whose name says it is the GPU's
(`gpu_sysfs_devfreq_name_rank`: one holding `mali` first, else one holding `gpu` but not `bw` nor
`bus`, since Qualcomm's `gpubw` is the bus's bandwidth; any case; the lowest name on a tie) and
reads its `cur_freq` (Hz) and `load` every sample and its `max_freq` (Hz) once. On any phone
without KGSL it also tries three candidate paths: the Mali driver's
`/sys/class/misc/mali0/device/utilization` and MediaTek GED's
`/sys/kernel/ged/hal/gpu_utilization` (busy, their first number) and
`/sys/kernel/ged/hal/current_freqency` (the kernel's spelling; its last number read as kHz). The
clock comes from the first readable of `cur_freq` and GED's, busy from the first of `load`, the
Mali node and GED's. **None of these names or formats is verified on a device**: the open line
says for every node `readable`, `not readable (EACCES)` or `not present`, and quotes the first text
read of `load` and of the three paths, so the first log from such a phone says what it has:

```
window: kernel GPU driver: /sys/class/devfreq/13040000.mali: readable cur_freq, max_freq; not present load, /sys/class/misc/mali0/device/utilization, /sys/kernel/ged/hal/gpu_utilization, /sys/kernel/ged/hal/current_freqency; the fastest clock 950 MHz; CPU clocks of cores 0-7 sampled with it; sampled every 250 ms
window: kernel GPU driver: /sys/class/kgsl/kgsl-3d0 not present, no GPU entry in /sys/class/devfreq; not present /sys/class/misc/mali0/device/utilization, ...; CPU clocks of cores 0-7 sampled every 250 ms
```

The CPU cores' clocks (`/sys/devices/system/cpu/cpuN/cpufreq/scaling_cur_freq`, kHz, cores 0 to
15) are sampled with the GPU's nodes on every phone, KGSL's too, and the sampler runs for them
alone where no GPU node is readable (`CPU clocks not readable (EACCES)` once where cpu0's is
denied). The block line ends with them by speed class (the cores of one `cpuinfo_max_freq`, the
`window: cores:` line's classes): `; CPU clocks: cores 0-5 1000-1800 MHz (average 1500), cores 6-7
1600-2000 MHz (average 1900)`, and the around line with the core the game thread was on as the
blocked present's record popped: `; CPU core 6 (the game thread's): 2000 MHz before, 1600 MHz
during, 2000 MHz after`.

## Issue 179's lines: what each decides

These lines answer the questions issue 179's logs left open (a Mali phone's scenes at a few
pictures a second), with nothing for the player to switch on. Each line, how to read it, and the
question it decides:

- **The step's CPU time** (`window: game thread: step 38.4 ms (CPU 12.1 ms of it; closing a frame
  ...` in the 10 s block, `window: slow step 64.1 ms (CPU 9.8 ms of it) at vsync ...` on each slow
  step; `cpu_place_thread_cpu_ns`, the game thread's own clock read at the end of one pace and at
  the start of the next). A step far longer than its CPU time waited (for the render thread, a
  present, a core); one near it was busy computing. Decides whether a long step is the game's own
  work or a wait.
- **Ran and waited for a core** (the end of the 10 s game-thread line: `; ran / waited for a core:
  game 3.9 s / 0.4 s, render 2.1 s / 1.8 s, submit 0.0 s / 0.0 s`; the kernel's
  `/proc/self/task/<tid>/schedstat`, `thread_stat.h`; once `schedstat not readable (EACCES)` where
  the phone denies it). Over a 10 s block: a thread that waited for a core about as long as it ran
  was held off its cores by other threads, the scheduler's doing, and the two-core pin (the game
  and render threads kept to cores 6-7 on the Helio G85) is the question; a thread that ran little
  and waited little waited on something else (the GPU, a present, a lock). Decides the pin.
- **The threads and their cores** (`window: threads: 31; kept to cores 6-7: ico_pc (the game
  thread), ico-render; on 0-7: ico-submit, mali-cmar-backend, mali-utility-wo, mali-mem-purge,
  ico-gpu-sysfs and 24 more`; once after the game thread's pin, again at a 10 s block when the
  text changed; `cpu_place_threads_format` over `/proc/self/task/*/comm` and `status`'s
  `Cpus_allowed_list`). Says which threads share the kept cores (the driver's own threads among
  them or not), beside the ran/waited numbers.
- **The GPU's clock and load and the CPU clocks** (above). A GPU clock at its top with load near
  100 % through a scene is the GPU at its limit (the vertex work below decides what to cut); a
  clock well under `the fastest clock` with the long frames is the GPU's governor not raising it;
  the game thread's core's clock low during a stall is the CPU side throttled.
- **List 0 against the triangles** (the 10 s `window: VU: ...` line gains `; list 0 GPU 41.2 ms a
  replay, 12.3 ms + 0.41 ms a thousand triangles (R^2 0.80, 38 replays)`: the block's timed
  replays' scene list GPU time fitted by least squares to their VU triangles,
  `window_scene_vu_fit`). Read it as: each thousand triangles of the VU draws cost the second
  number of ms of list 0 on this GPU, and the first number is the rest (pixels, passes). A high
  R^2 (0.7 and over) with a large slope says the vertex work sets list 0's time: on a tiler with
  the VU pre-pass off each triangle shades 9 vertex invocations (the VU line's words), so the
  pre-pass (each vertex once) would cut most of it; a low R^2 or a small slope says it would not.
  Decides the VU pre-pass default on Mali (held back this release). The fit needs 3 timed replays with
  different triangle counts; with fewer the mean alone (`(2 replays)`).
- **Images in use** (the 10 s first line's `present mailbox (images: 10, in use 1)`;
  `RhiStats.swapAcquired`, Vulkan only). The swapchain's images and those the game holds
  (acquired, not yet presented) as the block ends; while the render thread is busy, the count at
  its last hand-over. It counts only the game's own: images the compositor holds after a present
  are not visible to an app, so a compositor holding images shows as the image wait (the replays
  line's `acquire`) with `in use` at 0 or 1, not here.
- **The display wait** (the replays line's GPU part `uploads 2.1, lists 0-10 55.2, 11-12
  1.0, display wait 38.4, present 2.0`, the per-replay counts' `submits 2.0`, the start line `rd:
  present: the present's pass is a list of its own, submitted after the scene's; only its submit
  waits for the display's image ...`; [RENDER.md](RENDER.md#the-presents-own-list)). The issue's last
  log, with a picture in one list, showed the GPU's wait for the window's image inside the first scene pass that wrote colour
  (uploads 38.8 to 84.5 ms, 52 to 59 % of every stage-42 picture, L658 and L707 of the evidence):
  the acquire's semaphore held the whole picture's one list. Now only the present's own list waits.
  How to read the next log: uploads back at 2 to 5 ms and list 0 at its own cost say the scene no
  longer waits; the wait itself shows as `display wait`, or, on a GPU whose timestamps run ahead of
  the wait as the Mali's did (its first stamp came before the wait), as a `present` far above the
  1.7 to 3.5 ms it took there. The span (`GPU N ms`) still holds it, and the picture rate then says
  whether the scene's work filled the wait: a GPU time a picture near the scene's own (stage 42:
  about 64 ms against 132) is the fix working; an unchanged span with the wait moved into the
  present says the presentation engine's hold was our own backlog, not the compositor's. Decides
  whether the image wait was the limit of the scenes.

## Issues 123 and 163's lines: the skin check on an Adreno

On an Adreno (Qualcomm's own driver and Turnip alike) the skinned draws' check
([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md)) watches every skinned draw
of every picture and captures its first failure by itself; nothing is asked of the player. The
lines and what each decides:

- **The start line** (`rd: VU skin check: on (every skinned draw twinned in its own pass through
  its own pipeline ...; one probe a present` on Turnip, `one probe every 100 ms` on Qualcomm's own
  driver): the twins run; the probe's interval is the only difference between the two drivers.
- **The 10 s block** (`rd: VU skin check: P checks, W wrong, ...; T twins, D differ (first at frame
  F), N not drawn ...`): T near the skinned draws of the block's pictures (up to 128 a picture) says
  every draw was watched; D 0 over a run in which the player saw Yorda flicker says the fault is not
  a read that depends on the bone block's place in the pass (a wrong read at every offset shows in
  the probe's lines instead); N above 0 for long says the twins of draws without the attachment
  read waited for their pipeline.
- **`twins differ`** (`rd: VU skin check: frame F command L:I mesh testmodel #65641 (763
  vertices): twins differ: A ... N samples, B ... M samples; through FS with the draw's attachment
  read (its own pipeline); the probe of this command: ...; reads as: ...`): the same bytes drew
  different samples at the draw's bone offset and at a fresh one, inside the scene pass, through the
  draw's own pipeline. `the probe ...: wrong at address 200` names the bone (and its next line
  whether the read was zeros or non-finite: `reads as non-finite / dropped`); `right` says the fault
  is the scene pass's execution, not the bone read (the binning pass, the pipeline's link).
- **The capture** (`rd: VU skin check: captured .../logs/vu-skin-fail-F-L-I.bin and asked for a
  frame dump`, then `window: VU skin check: the capture: ... and the frame dump ...zip (...png)`):
  the sidecar (the bytes, the probe's rows, the twins' counts) and F12's dump of the next present
  with the sidecar in its zip, once a run. The files a report needs are those two and the log.

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
