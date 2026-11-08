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
- **The GPU's work against the vertex invocations** (the 10 s `window: VU: ...` line gains `; GPU work
  51.8 ms a replay, 16.0 ms + 0.17 ms a thousand vertex invocations (R^2 0.81, 70 replays)`: the
  block's timed replays' GPU work, the span less the display wait and the present (the slow-scene
  rule's quantity), fitted by least squares to the invocations the VU line counts: the pre-pass's
  vertices with it on, 9 a triangle with it off; `window_scene_vu_fit`). It replaced the line of
  list 0 against the triangles: on a Mali the stamps trade work between the stretch before list 0
  and list 0 at a constant sum, so list 0 alone measures where a stamp landed (issue 179's log:
  `uploads` 7.9 and list 0 83.3, then 55.9 and 33.2). Read it as: each thousand invocations cost
  the second number of ms of GPU work, and the first number is the rest (pixels, passes). A high
  R^2 (0.7 and over) with a large slope says the vertex work sets the GPU's time; the same line
  with the pre-pass on (the invocation count then smaller) says what the pre-pass bought. The fit
  needs 3 timed replays with different counts; with fewer the mean alone (`(2 replays)`).
- **Before list 0** (the replays line's GPU part and the top passes on a tile-based GPU: `before
  list 0 2.1` where other devices print `uploads 2.1`; the passes CSV's `kind` stays `uploads`).
  The stretch up to the first scene list's stamp: the uploads and the GPU's work still in flight.
  On a Mali it moves against list 0 at a constant sum, so read the span and the GPU work line,
  not that figure.
- **The acquire line** (`window: acquire: the display's image was free at the present's submit in
  41 of 60 pictures`, once a 10 s block, Vulkan only; the acquire's fence, asked at the submit of
  the present's list). All or nearly all: the presentation engine returns the image before the
  picture needs it, so the wait on the display's image is not what holds the pictures (issue 179's
  Mali: a wait of 0.06 to 0.15 ms and the GPU idle 0.0 to 0.1 ms between pictures). A low share:
  the pictures wait for the display's image. Absent: not Vulkan, or every acquire was made ahead.
  The replays line's GPU part also says `VU pre-pass off` where the GPU line showed `0.00` for an
  off pre-pass.
- **The VU pre-pass's verdict** (start lines; [PIPELINES.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)).
  A tile-based GPU takes the pre-pass as any other device, and the start-up self-test judges
  it. Read, in order: `rd: VU pre-pass: on: each mesh and grid vertex and triangle
  and each sprite evaluated once before the lists ...` (the device and the settings allow it; `off
  by the settings ...` or `off on this device (its colour targets hold 32 bytes ...)` say why not);
  then the self-test's `rd: VU pre-pass: on (self-test: ... byte for byte over 4 replays in both
  frame slots, ...) ... in <t> ms` or `rd: VU pre-pass: off (self-test: <reason>) ...`, the reason
  naming the first replay that differed, its frame slot, its first differing texel and the colour
  it drew against the one it should have (`the pictures differ: in replay 2 of 4 (frame slot 1),
  ...`: a record read stale or across two pictures differs from replay 2 on; replay 1 says the
  record is wrong within one picture), or `a pipeline could not be made (replay <k> of 4: ...)`;
  the summary line repeats it (`VU pre-pass on (self-test: ok)`). In play: `rd: VU pre-pass: the
  record holds N slots (R rows, K KB a frame slot)` each time the record grows (the scene's
  memory for it, twice: one a frame slot), the 10 s `window: VU: ...` line's `(pre-pass: each
  vertex once)` in place of `(pre-pass off: 9 a triangle)`, and the per-replay line's `VU pre-pass
  N vertices in N draws (F draws the old way)`: F above 0 in a scene says commands left the record
  (past its 1,024 rows, a missing pipeline, a state mismatch; each logged once as `rd: VU pre-pass:
  ...`). Decides whether the record path draws the Mali's scenes, and at what cost (the VU line's
  slope and the span against issue 179's log of 9 a triangle).
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
- **The slow-scene file** (`window: slow scene: the block's GPU work 50.3 ms a replay (bound 40;
  display wait 3.1, present 2.0, span 55.4) in a scene layout: frame 4812 (its vertex work 682.1
  thousand against the block's mean 598.0 thousand) written to logs/slow-20261106-120000.zip (dump
  2 of 3 this run); attach it with the log`; `slow_scene_check`, `slow_scene_poll` and
  `slow_scene_write` in `window_host.c`, the rule in
  `window_scene.h` (`window_slow_scene_due`, `window_slow_scene_pick`)). A cutscene cannot be
  dumped by hand (no pause menu, F12 needs the keyboard), and the issue's cage scene (stage 40)
  spent 22 to 35 ms in list 3 for 103 to 132 draws with no frame to look at. At the end of a 10 s
  block the game arms a dump when a window is up, no movie is playing, the block's steps were
  mostly in a scene layout, at least 8 replays were timed, the mean GPU work a replay (the span
  less the display wait and less the present pass, so the wait for the screen's image does not
  trigger it even on a driver whose stamps run ahead of that wait and put it in the present
  figure, as the Mali's do) is over `[dev] slow_scene_dump_ms` (default 40, the Original rate's
  period; 0 is off) and the run is 30 s old. The first block over the bound arms the first dump;
  a later block arms another only when its work is at least 1.5 times the work of the block of
  the last dump, up to three a run (the issue's run wrote one dump, for the first block over 40 ms, a
  44.9 ms stage, and none of the 112 to 144 ms boat blocks after it). An armed dump waits for the
  first replay after the block whose CPU-side vertex work (the VU line's quantity: the pre-pass's
  vertices with it on, the triangles times 9 with it off) is at or above the block's mean of it a
  replay, and writes that replay's frame. The renderer notes a replay's counts as it ends it
  (`rd_perf_last_replay_work`) and the window looks at the note every vsync, while the frame is
  still the last closed or the one before (the two the renderer keeps); a GPU span cannot pick
  it, as a replay's timestamps come two or more replays later, after the ring has dropped its
  frame. A replay whose frame has left the ring by the next look is passed over for the next one
  (`window: slow scene: frame F had left the renderer's ring before it was looked at: ...`, the
  first 8 times and every 100th); an arm that finds no such replay is dropped at the next block's
  end, where the rule runs again. The file is that one frame and a copy of the log,
  `logs/slow-<date>-<time>.zip`, pruned to the newest three, and the newest goes into a crash or
  hang report and the rc share menu's set. Written in releases. What to send: the log and the
  zips; replay one with `rd_replay_tool` as a loss dump (the frame is the zip's first
  `.rddump`). The line's work and span against the block's `GPU` line say whether the frame is
  the cost or the wait; the frame number matches the replays after it, and the line's vertex work
  against the block's mean says how heavy the picked replay was. When three files exist, the
  last is the heaviest block of the run (each needed 1.5 times the one before). Decides what the
  cage's list 3 draws are, and what the boat scene's 109 to 144 ms are.
- **The occlusion test's cost** (the 10 s line's cull part, `; cull: N of M scene draws skipped a
  replay (F frustum, O occluded), T ms (occlusion H) ([dev] cull)`, now followed by `; occlusion:
  collect C ms (settle S, alpha A, vertices V ms for Nv vertices, Nt triangles kept of Na; cache Hc
  of Dc draws), raster R ms (Nc cells), test X ms (Nd draws tested); occluders used U, refused:
  state/chain/target/bound/program/alpha/small/no-tri a/b/c/d/e/f/g/h; past the budget B`, each
  figure the mean a replay; [RENDER.md](RENDER.md#culling)). The issue's log showed 14.4 to 16.1
  ms of cull a picture on the render thread, 12.8 to 14.7 of it the occlusion test, for 0 draws
  occluded in its scene blocks (L596, L634, L678 and L727 of the evidence log); the code says the
  cost was the collect's vertex pass (every vertex of every candidate occluder projected in full
  before nearly all its triangles were dropped as too small). The collect now leaves those
  triangles before projecting them, and keeps a draw's triangles from one picture to the next while
  nothing it reads changed, with the same occluders and verdicts (`rd_cull_ab`,
  `rd_vis_hiz_corpus`). How to read the next log: `C` against `R` and `X` says which part of `H` is
  left; `V` most of `C` with `Nv` large against `Na` says the screen boxes left little (the vertex
  pass across threads is the next step); `S` large says the texture decodes were behind, not the
  test (it is in `T`, not in `H`); `Hc` near `Dc` on a still scene says the cache holds. Decides
  whether the occlusion test still costs the render thread a share of the 40 ms Original cap in
  the scenes.

## Issues 123 and 163's lines: the skin check on an Adreno

On an Adreno (Qualcomm's own driver and Turnip alike) the skinned draws' check
([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md)) watches every skinned draw
of every picture and captures its first failure by itself; nothing is asked of the player. The
lines and what each decides:

- **The start line** (`rd: VU skin check: on (every skinned draw twinned in its own pass through
  its own pipeline ...; thinned to one a picture ... after 2000 replays with none differing ...; one
  probe a present (every 100 ms once thinned)` on Turnip, `one probe every 100 ms` on Qualcomm's own
  driver): the twins run; the probe's interval is the only difference between the two drivers.
- **What the check costs** (the 10 s line `window: VU: D draws, T triangles, V vertex invocations
  (...); twins N draws, W vertex invocations a picture; GPU work ...`): N and W the twins' own
  draws and vertex invocations a picture (9 a triangle). Several dozen draws a picture with the
  GPU work near the frame's budget says the check's share; once thinned (`rd: VU skin check: twins
  thinned to one a picture after P clean replays ...`, the 10 s block's `, thinned to one a picture`)
  N falls to 1 to 3 a picture. `rd: VU skin check: twins re-armed by a twin event ...` says a fault
  came back and every draw is watched again; the 10 s block's `every skinned draw (P of 2000 clean
  replays to thin)` how far the run is from thinning.
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
- **The ring read beside the picture** (the variants line's `(c3) the bones through the ring:
  right | wrong`, the event line's `C (ring read) K samples`, the 10 s block's `ring twins differ E
  (of R read)`): where the program entries are the plain set (Turnip's), the same bones read through
  the ring, a storage buffer. `c3 right` where the probe is wrong, or C equal to B where A differs,
  says the ladder's step 1 clears the fault; C differing too says it does not.
- **The ladder** ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#the-ladder)): `window: VU bones: [dev] vu_bones
  = ring is written (the skin check saw N twin events at bone address A over P presents); ...; in
  force from the next picture`, `window: VU bones: switching to the ring read now (the renderer made
  again in place, ...)` and `window: the renderer made again in place on vulkan (...)` (step 1 in
  the same run), then `rd: VU bones: through the ring (storage buffer) on the skinned entries
  ([dev] vu_bones = ring, written by the skin check at <version> frame F; ...)` from the new
  renderer and at every later start with `window: VU skin check ladder: step 1 of 2 (the ring read)
  for this start ...`; its twins' event
  lines end `A and B read the bones through the ring: step 1 of the ladder in force`, and the run
  ends with `rd: VU skin check: the ring read: healed (0 of T ...) | not healed (D of T ...)`. Still
  differing on a Turnip package: `window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin) for the
  next start (the driver library is loaded once a process): ...`, then at that start `gpu driver: TU_DEBUG=nobin set before
  libvulkan_freedreno.so loads ...` and `window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin)
  for this start; step 1 (ring read) did not clear the twins (D of T)`; still differing there:
  `window: VU skin check ladder: both tried, neither cleared the twins (...); [dev] turnip_debug =
  off is written ... and the ladder stops`. Step 2 takes 8 twin events under step 1, not one.
  Healed under step 2 with step 1 in force writes the re-test (`window: VU skin check ladder: step 2
  (TU_DEBUG=nobin) healed the twins with step 1 (ring read) beside it: ...; [dev] turnip_debug =
  retest is written ...`); the next start runs without nobin (`gpu driver: TU_DEBUG=nobin not set at
  this start ([dev] turnip_debug = retest ...)`, `window: VU skin check ladder: the re-test of step 2
  at this start: ...`), and ends with `... the re-test without TU_DEBUG=nobin kept the twins equal:
  ...; [dev] turnip_debug is removed from ...` or `... the twins differ again (D of T); [dev]
  turnip_debug = nobin is written ..., marked needed`. A `turnip_debug = nobin` an older ladder
  wrote (no word trial or needed in its mark) is re-tested the same way at the next start. A
  record draw's twins (the VU pre-pass on) say `the program's own vertex entry's ring build: the
  draw read the pre-pass record ...` and end `A and B read the bones through the ring, as the
  pre-pass record did`: the read the picture took. `healed` over 2,000 presents says the step in force
  holds; whether `TU_DEBUG` reaches the driver through the package loader is not verified, so the
  twins' counts under it are the answer. The kept-keys start line (`ico_pc: [dev] keys kept from an
  earlier start: ... vu_bones = ring, vu_bones_by = ...`) shows the chain in a reporter's log.

## Issue 123's GPU hang: the loss dump's lines

A GPU hang (the watchdog's `ico_pc: WATCHDOG: the GPU: submission N not finished after 15 s`)
writes the pass ring and then the loss dump from the watchdog's thread
([RENDER.md](RENDER.md#the-loss-dump)). Each slow step is logged and the log flushed before it
begins, so the last line says which one a hang of the handler itself is in:

- `window: loss dump: writing frame N to .../logs/loss-<stamp>-lost.rddump`, then `... writing
  frame M (the one before) to ...-before.rddump`: the frames' writes (the CPU's copies of the
  commands, texels and meshes, onto the phone's shared storage);
- `window: loss dump: zipping K MB (the two frames and the log) at store level`: the zip, stored
  (no deflate), K the files' size together;
- `window: loss dump: frames N and M (the one before) written to <zip> in T s`: done, T from the
  first step.

Issue 123's hang log had none of these lines (the zip was deflated then) although the game thread
went on for 10 s (`0 presents ... 300 game frames`), so the write or the deflate on the watchdog's
thread held the run's end; which one is what the next such log says. The report's `ico_pc: the last
start-up step:` line is `window: start: up in T s; the game runs` once the start is over, not the
fog self-test's step.

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
