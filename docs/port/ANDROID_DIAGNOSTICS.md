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
| `port/platform/window_stall.c`, `window_stall.h` | the window's side: the open line, the block lines, the around lines, the present stall lines |
| `port/platform/present_stall_block.c`, `present_stall_block.h` | the present stalls per 10 s block and per run against the GPU span band and the display's rate (`present_stall_block_test`, every platform) |
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
  needs 3 timed replays whose largest count is at least twice the smallest above 0 (a replay with no VU work, a keep frame or a menu, does not count as the smallest); with fewer replays the mean alone (`(2 replays)`), and with a narrower spread (a cutscene: issue 179's `-64.0 ms + 2.42 ms (R^2 0.07)` was noise read as a cost) the mean and `; fit: not enough spread (x N..M thousand vertex invocations)`.
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
  the summary line repeats it (`VU pre-pass on (self-test: ok; record stores)`). In play: `rd: VU pre-pass: the
  record holds N slots (R rows, K KB a frame slot)` each time the record grows (the scene's
  memory for it, twice: one a frame slot), the 10 s `window: VU: ...` line's `(pre-pass: each
  vertex once)` in place of `(pre-pass off: 9 a triangle)`, and the per-replay line's `VU pre-pass
  N vertices in N draws (F draws the old way)`: F above 0 in a scene says commands left the record
  (past its 1,024 rows, a missing pipeline, a state mismatch; each logged once as `rd: VU pre-pass:
  ...`). Decides whether the record path draws the Mali's scenes, and at what cost (the VU line's
  slope and the span against issue 179's log of 9 a triangle).
- **The record's write** ([PIPELINES.md](PIPELINES.md#the-vertex-and-triangle-pre-passes), the
  record's two writes). Issue 179's Mali log spent 34 ms of the boat's 130 in the pre-pass itself:
  three 16-byte render targets a point (48 bytes a pixel) and six texture-to-buffer copies a replay
  (`14 copies` in the per-replay line). Where the driver offers vertex stores, the pre-pass's
  vertex shaders store the record into its buffer themselves. Read, in order: the start line's end,
  `...; the record stored by the vertex shaders into the frame slot's record buffer: no render
  targets, no copies (...)`, or `the record drawn into 3 RGBA32_UINT render targets and copied ...
  (no vertex stores: the device has no vertexPipelineStoresAndAtomics ...)`, with the Vulkan
  backend's own `no vertexPipelineStoresAndAtomics: the VU record is drawn into render targets and
  copied` before it (the driver does not offer the feature: the targets stay); the self-test's
  verdict ending `, record stores` (or a line `rd: VU pre-pass: the record stored by the vertex
  shaders failed the self-test (<reason, first texel>) ...` and the verdict ending `, record
  targets, the stores failed the self-test`: the driver stores wrong, the targets were judged
  instead); the summary's `VU pre-pass on (self-test: ok; record stores)`. In play: the per-replay
  line's `copies` 6 fewer a replay than with the targets (8 where that log's boat had 14); the GPU
  top passes' two `VU pre-pass record 1024xN ... ms` entries (pass V and pass T: their sum against
  that log's 7 to 13 ms each is what the stores bought); the 10 s `window: VU: ...` line, which
  ends `; record stores`, its fit's first number (the fixed cost: 21.5 ms in that log against 15.2
  without the triangle pass) and its slope. With `[dev] vu_prepass_min` set, the same line's `;
  pre-pass skipped N frames under M slots` says how many frames drew the old way for being small.
  Decides whether the stores are the faster write on the phone, and whether a floor for small
  frames is worth setting.
- **The record by compute dispatches** ([PIPELINES.md](PIPELINES.md#the-vertex-and-triangle-pre-passes),
  the record's three writes; [RHI.md](RHI.md#compute-dispatch-the-vu-record)). Issue 179's log
  of 2026-11-09 (stage 3 cutscene) showed the Mali-G52 MC2 (r32p1) without vertex stores (`no vertex
  stores: the device has no vertexPipelineStoresAndAtomics` on the start line): the record stayed
  in the targets, `VU pre-pass record 1024x103 ... 7.14 ms` in the top passes, 14.6 ms of 64 ms GPU
  a replay. There the record is now written by compute dispatches. The lines to read in the next
  Mali log, in order: the Vulkan backend's `compute record stores: offered (queue compute yes,
  vertex stores no)` (or `not offered: <why>`: the targets stay); the start line's end `...; the
  record stored by compute dispatches into the frame slot's record buffer: no render targets, no
  copies (no vertex stores: ...)`; `rd: VU pre-pass: the record holds N slots (... stored by
  compute dispatches)` as the record grows; the self-test's verdict ending `, record compute` (or
  `rd: VU pre-pass: the record stored by compute dispatches failed the self-test (<reason, first
  texel>) ...` and `, record targets, the compute failed the self-test`: the driver's compute builds
  store wrong, the targets were judged instead; a compute pipeline that could not be made is a
  `pipeline rd vu pre-pass (compute): ...` line from the backend with the driver's error); the
  summary's `VU pre-pass on (self-test: ok; record compute)`; the display line's `VU pre-pass on
  (record compute)`. In play: the per-replay counts' `copies` 6 fewer and `passes` 2 fewer a replay
  than with the targets, `dispatches` the pre-pass's draws (`rd_replay_tool --stats`: `..., N
  copies, D dispatches; ...`); the GPU top passes' two `VU pre-pass record 1024xN ... ms` entries
  (the two dispatch runs: their sum against the 14.6 ms of that log is what the compute write
  bought); the 10 s `window: VU: ...` line ending `; record compute`. A start that ends in the
  driver's compile of a compute build, or a crash or device loss in one, takes the restart's
  targets rung first ([PIPELINES.md](PIPELINES.md#the-record-stores-restart-rung)): `[dev]
  vu_record = "targets"` in `config.toml` after two such ends. Decides whether the compute
  write runs on the phone, draws right, and how much of the 64 ms it saves.
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
  a later block arms another when its work is at least 1.5 times the work of the block of
  the last dump, or when it is the first block over the bound in a stage other than the last
  dump's block's (the line then reads `in a scene layout (the first slow block of stage 40): frame
  ...`; `window_slow_scene_new_stage`), up to three a run (the issue 179 run's cage, stage 40 at
  90.9 ms after the boat's 99.7 ms, was refused by the 1.5 times rule alone and so its list 3,
  36 % of its picture, was never dumped) (the issue's run wrote one dump, for the first block over 40 ms, a
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
- **The cull on a phone** (a tile-based profile defaults to `frustum`: the start line says `rd: cull: frustum (...; the profile's default: ...)`, the profile line ends `cull frustum (the profile: ...)`, and the 10 s cull line ends `(profile default)` instead of `([dev] cull)` with no `; occlusion:` part; `[dev] cull = full` turns the test on). **gpu_robust in the log:** `ico_pc: [dev] keys kept from an earlier start: gpu_robust = true` followed (after the config line) by `ico_pc: [dev] gpu_robust = true in <config.toml> has no gpu_robust_by mark: set by hand ...` says the key is the player's or was left without the mark and costs bounds checks on every 3D load; with the mark (`gpu_robust_by = "losses; <version>; <clean starts>; <run or ->"`) the game's own lines `robust access on (set by the game ...): N clean starts of 3` and `robust access turned off ...` say where it is.
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

## The upscaler's pictures in a phone's bug report

FSR 3.1 runs on a phone's Vulkan driver too (Adreno, Mali, Xclipse with Vulkan 1.2). With it in
force, Settings > Save a picture for a bug report also writes, beside `-present.png` in the dumps
folder, `-scene.png` (the picture before the upscaler, render size), `-velocity.png` (the motion
vectors), `-depth.png` (the depth the upscaler received, a 24-bit step in R, G, B; not written
where the driver cannot read depth back, which the line says) and `-composite.png` (the upscaler's
output, before the game's 2D layer), each halved until at most 2560 wide, and adds them to the
report's zip while it stays under 24 MB (in an rc build Share a bug report offers the zip; a release build has no such row, so the zip is taken from the `dumps` folder). Lines:
`upscale capture: frame N: ...` (the sizes, or why a picture is missing) and `window: bug report:
the upscaler's pictures: ... K of 4 in <zip>`. Reading them: a flaw in `-scene.png` is the port's;
one only in `-composite.png` is the upscaler's. The upscaler runs in full precision by default on
every device; `[dev] fsr3_fp16 = on` in the config's `[dev]` table opts into its half-precision
speed path (the display line then says `fp16 on (dev)`). The 10 s line `upscale point: 10 s: the N pictures dispatched
held: ...` names the effects the upscaled frames held ([DISPLAY.md](DISPLAY.md#fsr-31-upscaling),
"The upscaler's pictures").

## Issue 123's freezes: the start-up set and the GPU's time

The issue's log of 2026-11-09 (Adreno 740 on Qualcomm's driver 512.676.1, Custom 3x, 3048x2032 at
120 Hz) had two
causes in it, each on its own lines. The lines to read, in the log's order:

- `ico_pc: the previous start stopped preparing the graphics when the system ran low on memory
  (...); this start resumes them where it stopped`, then `window: start-up set: resumed at pipeline
  N of M after the low-memory stop of <date> (batches of B, stop K in a row; ...; the pipeline cache
  file <n> bytes, loaded by the renderer: yes|no)` and `rd: pipelines: the start-up set resumes at
  pipeline N of M ...`: the set is made before play again after an earlier start stopped it
  ([PIPELINES.md](PIPELINES.md#stop-and-resume)). The reporter's log read "this start prepares
  them as the game needs them" there, then `rd: pipelines: no start-up set (the previous start's
  set stopped when the system ran low on memory)`: every pipeline was compiled in play.
- `window: pipelines: the start-up set stopped for memory at pipeline N of M (batches of B, stop K
  in a row): memory <process> MB (most <peak> MB); device <avail> MB available of <total> MB;
  (memory budget: ...); ... the next start resumes at pipeline N in batches of B/2`: a start that
  still cannot finish the set, with the process's and the device's memory and the place; `stop K`
  counts the starts in a row (each halves the batch, never under 4).
- `rhi_vk: pipeline cache <path> is from another driver for this GPU (the driver's cache id
  differs: ...)`, `... is for another device (vendor ... device ...)`, `... is not a cache this
  driver reads`, or `rhi_vk: pipeline cache: the shader set changed with this version (N older
  files dropped): the cache starts empty once`: why the cache started empty (the reporter's log
  read "for another device or driver", one line for the first two). The reporter's previous run
  named no compiler in its batches (`compiler unnamed by the driver`) and built the VU pre-pass's
  record modules, which this start's driver does not (the device table turns the pre-pass off on
  Qualcomm's driver 512.676.1): that run was under another driver (one picked in the app's driver
  list: which one the log does not say), so its cache file was refused.
- `rd: pipelines: compiled lazy batch B, pipelines ...; slowest ...; by family vu 17, program 12;
  27 of them in the start-up set`: in a run without the set, the families compiled in play and how
  many of them a set made before play would have made.
- `rd: slow walk T ms (replay R, frame F): pipelines made P ms (N), ...` and `window: slow replay
  ... walk W, ... N pipelines created`: a picture that compiled pipelines in its walk (the
  reporter's freeze: 29 pipelines, 1311.9 of 1359.6 ms).
- The 10 s presents line's end: `; the GPU takes 37.7 ms a picture at this resolution (3x): 2x
  would fit the game's rate (37.7 ms = 3.8 ms of vertex work by the fit + 33.9 ms that scales with
  the pixels: 3.8 + 33.9 x (2/3)^2 = 18.8 ms, under 30.0 ms, 0.9 of the 33.3 ms step at 30.0 game
  fps; a hint: nothing changes by itself)`, written only when the block presented fewer pictures
  than the game made, skipped presents (late ones and, at frame rate Original, those the render
  thread's busy picture left out: `frames not presented N: ..., the render thread busy N`; the
  Mali-G52 run of issue 179 had only busy ones and no hint printed) and the GPU span is over the step
  (`port/platform/window_scene.c` `window_scene_gpu_hint_text`): the vertex part is the block's VU
  fit's slope times its mean invocations (the `window: VU: ...` line's fit), the rest is taken to
  scale with the pixels, and the step is the largest offered Resolution step whose estimate is at
  most 0.9 of the step. At a scale that is not a Resolution row's (Window, Auto, or an upscaler's
  render scale such as 3.02) no row is named: `; the GPU takes 40.0 ms a picture at this render
  scale (2.5x, not a Resolution row's: Window, Auto or an upscaler): a lower Resolution (or
  Upscaling quality) would fit the game's rate at a render scale of 2x or under (...)`. Without a fit line (under 3 timed replays, or the block's invocations not
  spread by a factor of 2, which the fit needs) no step is named, since a vertex-bound block would
  read as pixels: `; GPU-bound at 3x (the GPU takes 37.7 ms a picture, the step 33.3 ms at 30.0 game
  fps): no resolution hint (fit: not enough spread)`; the reporter's heavy blocks (370k-382k
  invocations) read that way. A hint only: the resolution never changes by itself (the owner's rule).
  The reporter's blocks at 3x: 25.6-25.8 presented fps against 30.0 game fps, 210-218 late skips,
  GPU span 35.5-38.0 ms; the fit `34.4 ms + 0.01 ms a thousand vertex invocations` over 376k
  (R^2 0.05: the invocations hardly varied in that scene, so the slope is small and uncertain; a
  block of play read `3.5 ms + 0.10 ms`, R^2 0.94, over 159k).

## The dump worker, the report's bundle and the clock-back words (issues 123 and 179)

- **Who writes the automatic dumps.** The skin check's capture and the slow-scene
  dump are written by a worker; the cost the game paid for them was the issue 123 run's "the
  game step was late by 818 ms" after the capture and the issue 179 run's resyncs dropping 890 /
  1460 / 841 ms, the file write, the log flush and the zip on the game thread. The game thread now copies the frame (and the tick before) into
  memory (`rd_dump_frame_mem`, `rd_dump_retained_frame_mem`: a memory copy and the wait for the
  render thread every dump has, at most 192 MiB) and hands it to one worker thread
  (`port/platform/dump_auto.c`), which builds the zip, adds the skin check's sidecar, prunes the
  slow zips to the newest three and notes the result; the game thread prints it at its next vsync.
  F12 and the Settings bug-report picture (user actions) are unchanged.
- **The line.** `window: slow-scene dump: 3.1 ms on the game thread, 912 ms on the worker (7234408
  bytes of frame into slow-20261110-120000.zip)` (and `skin-check capture` the same), then the
  existing text line. Read the game-thread figure against the "late by" lines: it is the whole cost
  the game paid. `window: <tag>: zip not written: <why> (...; the .rddump is kept)` keeps the frame
  as a loose file in `logs/`. A job offered while one is written is dropped, never queued:
  `window: slow-scene dump: dropped, the worker is still writing the one before (N dropped so far;
  one dump is written at a time)` (the first 8, then every 100th). The skin check's capture waits
  for the worker instead (once a run). The worker is joined when the window closes; a crash while it
  writes can leave a partly written zip.
- **The bug report brings the run's zips.** The Settings picture's zip also holds the newest three
  of this run's automatic zips that are still on disk (stored, within 12 MiB together) and
  `automatic-captures.txt` naming every one: its size and `included` or why it was left out. The log
  says the same: `window: bug report: 3 of 5 automatic captures added (...)`, or `no automatic
  capture of this run is still on disk`. The 179 reporter attached the report zip while three
  slow-scene zips stayed on the phone.
- **"the GPU's timestamp clock went back N ms".** The stall line's old words were `begun 20699.4 ms
  before the one before ended`: the first stamp of the stalled replay was 14-36 s before the last
  stamp of the replay before it, about the time since the previous stall (replays 785, 1572, 2021 at
  30 a second are 26 s apart, the figures were 20.7, 26.2, 14.9 s). A replay cannot begin before
  the one before it began, so the clock was not read wrong: the Adreno's timestamp counter was
  restarted at the stall, and the figure was how long it had run since its last restart. The gap is
  now not measured in that case (`rd_perf_gap_step_ex`): the stall line reads `the GPU's timestamp
  clock went back 20699.4 ms since the replay before (a counter restart: no gap to measure)` and the
  log gets `rd: GPU timestamps: the clock went back 20699.4 ms at replay 785 (restart 1 of the run;
  ...)` (first 8, then every 100th). A restart at every stall is more evidence for a power-state
  change of the GPU at the stall (a hypothesis; the kernel nodes are unreadable there).

## Issue 123's present stalls: the frame rate pin and the stall lines

- **Why the pin left the display at 120 Hz.** The latest issue 123 log's Frame rate row was Original
  and the one before's Uncapped; the pin asked a rate only for a number, so both logs say `frame rate
  pin: on` with no `asked` line after it, and `display refresh: 120.00 Hz ... 0 changes this run`
  in every block: no vote was ever made. It also asked with FIXED_SOURCE, the video value (the NDK
  reference: DEFAULT "should be used when displaying game content"). Original now asks the game's
  own field rate, 59.94 Hz (50 Hz in a European disc's 50 Hz mode), which the 30 pictures a second
  divide, with DEFAULT and the exact rate, and the window says what the display took:
  `window: frame rate pin: the display runs 60.00 Hz with 59.94 Hz asked: the ask taken (run time
  10.2 s)`, or `2 x the ask (the system kept a multiple; ...)`, or `not a multiple of the ask`.
  The system may keep 120 Hz all the same (a multiple, which Android's guide says it may pick);
  the line says which. The words and the documentation lines are in
  [DISPLAY.md](DISPLAY.md#the-refresh-rate-and-the-frame-rate-pin).
- **The stall lines.** From the run's first present of 250 ms or more, every 10 s block has
  `window: present stalls: N over 250 ms in 10.0 s (...)`: each stall's length, the GPU idle before
  the stalled replay (`not measured (the GPU clock restarted)` when the Adreno's counter restarted,
  as it did at every stall of the latest log), the GPU clock restarts in the block, the block's GPU
  span (mean, range, 5 ms band) against the stalled replays' spans, the display's rate, the pin's
  ask and the kernel driver's GPU clock where readable (`GPU clock not readable` on the reporter's
  phone: KGSL is EACCES there). At exit: `window: present stalls this run: ... a minute; by display
  rate: ...; by the block's GPU span: ...`.
- **Reading them.** The hypothesis is a GPU power-state change at the stall (each stalled replay's
  passes about 3x their block's means, the timestamp counter restarted, the GPU idle about 6 ms
  before nearly every replay at 30 fps). The run line answers two questions from one log: are the
  stalls a minute different at 60 Hz than at 120 Hz (the pin taken against not), and do they sit in
  one span band (the 26-35 ms band of an earlier deep dive) while other bands have none. Restarts
  without a stall in the same blocks would say the power change happens without the stall too.
- **No keep-busy attempt.** Nothing in the logs ties the stalls to the GPU's idle time:
  the replays before each stall were steady (5.8 ms idle on average, the same as the blocks without
  a stall), only the first stall followed a long pause (the 818 ms late step of the skin-check
  dump), and the idle before the stalled replay itself cannot be measured because the counter
  restarted. Android documents no app-side control of the GPU's power state, so work submitted
  only to keep the GPU awake would cost power and heat on every phone against a cause not shown.
  The lines above are what would show it.

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
