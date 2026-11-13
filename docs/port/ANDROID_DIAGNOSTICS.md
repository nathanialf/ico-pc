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
| `port/platform/present_stall_block.c`, `present_stall_block.h` | the present stalls per 10 s block and per run (`present_stall_block_test`, every platform) |
| `port/render/rd_perf_clock.c`, `rd_perf_clock.h` | a replay's GPU clock against the CPU's and the `rd: GPU clock` line (`rd_perf_clock_test`, every platform; [Issue 123: what the GPU did during a stall](#issue-123-what-the-gpu-did-during-a-stall)) |
| `port/rhi/vk/vk_tsclock.c`, `vk_range.c` | the slot's clock (submit times, calibrated timestamps, the polled wait's trace) and the ranges robust access does not cover (`rhi_vk`) |
| `android/app/src/main/java/com/defnf/icopc/IcoActivity.java` | the thermal getters and the two listeners |
| `port/platform/attempt_ledger.c`, `attempt_ledger.h` | the attempt ledger's rows and lines, in `ico_thread` so the renderer and the window both count into it (`attempt_ledger_test`, every platform; [The attempt ledger](#the-attempt-ledger)) |
| `port/platform/stall_coincide.c`, `stall_coincide.h` | what a stall coincided with: the pass medians, the rule, the classes, the parsers of `/proc/pressure/memory` and `/proc/self/status`, the lines (`stall_coincide_test`, every platform; [What a stall coincided with](#what-a-stall-coincided-with)) |
| `port/render/rd_perf_more.c`, `rd_perf_more.h` | the cost counters' block sums and the costs and compare lines (`rd_perf_more_test`, every platform; [The cost counters](#the-cost-counters)) |
| `port/render/rd_perf_hooks.c`, `rd_perf_hooks.h` | the cost counters' increments: SCENE's pass breaks, the two-pass blend, the screen boxes by list |

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
- **What holds a picture behind the one before** ([RHI.md](RHI.md#buffer-barriers-scopes)).
  Issue 179's log of 2026-11-10 (stage 3: list 0 about 20 ms against about 15 before the compute
  record, the pre-pass about 3 ms against 13) raised the question whether the record's dispatches
  waited for the picture before's pixels: its bracket's barrier, the mesh uploads' copies and every
  command list's opening barrier all named a fragment-side stage beside the copy or compute stage,
  and on a tile-based GPU such a barrier keeps a picture's uploads, record and vertex work from
  running beside the previous picture's fragment work. They name only the stages that can touch the
  buffer. The `GPU between replays` line's `0 begun before the one before ended` cannot answer
  it: every Vulkan stamp is bottom-of-pipe, so a replay's first stamp waits for the replay before on
  a driver that keeps the rule ([DISPLAY.md](DISPLAY.md)), and that 0 is what the line reads on any
  such driver. Read instead, in the next Mali log:
  - `rhi_vk: barriers: one holds every later vertex, compute and copy command (the next picture's
    too) behind the fragment work before it: SITE (first scope 0xS, second 0xD, ...); K of this
    kind at this site` and its twin `one holds every later copy (vertex and compute work only
    through a copy's own barrier)` (each site and kind's first 8 only), and in every 10 s block the
    `window: CPU walk ms ...` line's `barriers holding fragment work per replay: V before vertex or
    compute work, C before copies only (vertex/copy by site: ...)`. A site whose per-replay count
    stays above 0 in play is a barrier that still serialises the pictures: a texture upload over a
    sampled texture (`a render pass's or copy's own barrier`, first scope 0x80, second 0x1000) is
    expected where textures stream; `the barriers held back for the next command` with a first
    scope holding 0x80 (the fragment shader) or 0x400 (colour output) and a second holding 0x800
    (compute) or 0x8 (vertex shader) would be the record or a pass's vertex work held. No such
    lines in a play block say no barrier holds the next picture's vertex and compute work behind
    the picture before's pixels.
  - The texture uploads and the fog's table are recorded after the VU pre-pass since this
    version (`port/render/rd_upload_late.h`: a texture upload over a sampled texture waited for
    the picture before's fragment work, and every later copy with it, the mesh uploads the record
    reads among them); their time still counts as `uploads` (a stamp of its own between the
    pre-pass and list 0, `RD_PERF_TS_LATE`), so the figures read as before, but for a pass-timed
    replay's pieces, where the `VU pre-pass record` piece now holds them (a piece of their own
    would change every pass count). Whether the record now
    overlaps the picture before is a hypothesis this log decides.
  - The pre-pass's own GPU time (`VU pre-pass X` in the replays line, the `VU pre-pass record`
    entries of the top passes): both its stamps are bottom-of-pipe, the first written once the
    mesh uploads and the picture before are done, so a record that ran during the picture before's
    fragment work shows as a pre-pass near 0 and a GPU span a replay that is not longer. A pre-pass
    that stays near its cost in that log (2.99 ms in its stage 3 block) with no hold lines says the
    driver ran it after the picture before anyway (or writes its stamps late); with hold lines,
    the site they name.
  - The span a replay and the presented rate against that log's (stage 3 play: GPU 49.6 ms a
    replay) decide whether anything was gained.
  Decides whether the barriers were what held the record behind the picture before, and which
  barrier to look at next.
- **DATE without ending the scene's pass** ([RHI.md](RHI.md#local-read), the subpass read;
  [RENDER.md](RENDER.md), DATE through the stencil bit). Issue 179's Mali-G52 (Vulkan 1.1.177,
  `render passes legacy`, `local read no`) drew DATE through the R8 snapshot: `passes per replay
  20.0 (render passes 13, VU pre-pass 2, DATE snapshot 3, present 1)` in its stage 3 block, each
  snapshot a store and reload of SCENE's 912x512 colour and D32S8 depth; list 3 cost 9-12 ms in
  play and 25-33 ms in the cage. The `VkRenderPass` path can read the pass's own colour as an
  input attachment of its subpass, but that is an experiment, off by default (its cost on a phone
  is not measured): the default log says `rhi_vk: render passes: the subpass read off (the default;
  [dev] subpass_read = on tries it)` and DATE stays the snapshot. A reporter who
  sets `subpass_read = on` under `[dev]` in `config.toml` tries it; read, in that log:
  - `rhi_vk: render passes: the subpass read: ...` after the device line (the RHI offers it), and
    `rhi_vk: render passes: the first with the subpass read: ...` once the scene's first pass is
    made.
  - The profile line's `DATE stencil` and `local read yes (the subpass read: a pass's colour with
    a depth target)`; then `rd: DATE: stencil path (self-test: stencil ok 5, snapshot ok 5)`: the
    device drew the stencil bit right on its SCENE. `DATE: snapshot path (self-test: stencil fail
    ...)` with `profile: DATE stencil off (self-test)`: the driver read the colour wrong; the run
    keeps the snapshot (the picture as with the snapshot path), and the line's samples are what to send on.
  - The `passes per replay` line: `DATE snapshot` near 0 and the render passes fewer by about
    as many (3 a replay in that block). The issue's own frame, replayed on lavapipe through this
    path (`rd_date_subpass_ab`): 3 DATE snapshots to 0, 51 passes to 46, every target byte for byte.
  - list 3's and list 0's times in the same scenes (stage 3 play, the cage) against that log's.
    The gain on the phone is not measured: a tile-based GPU pays for each snapshot's store and
    reload, but the subpass read keeps every RGBA8 render target in `GENERAL` and gives it the
    input-attachment usage, whose cost on a Mali is not known.
  - `rhi_vk: render pass: a local-read barrier in a pass without the subpass read ...` or `... the
    subpass read's class ... begun without its input attachment ...` should never print: either
    is a fault to report. `[dev] date_path = "snapshot"` in `config.toml` goes back to the
    snapshot for an A/B with the shape kept; taking `subpass_read` out removes all of it.
  Decides whether the scene's pass now stays open through DATE on the phone, whether the driver
  draws it right, and what it saves.
- **Images in use** (the 10 s first line's `present mailbox (images: 10, in use 1)`;
  `RhiStats.swapAcquired`, Vulkan only). The swapchain's images and those the game holds
  (acquired, not yet presented) as the block ends; while the render thread is busy, the count at
  its last hand-over. It counts only the game's own: images the compositor holds after a present
  are not visible to an app, so a compositor holding images shows as the image wait (the replays
  line's `acquire`) with `in use` at 0 or 1, not here.
- **The display wait** (the replays line's GPU part `uploads 2.1, lists 0-10 55.2, 11-12
  1.0, display wait 38.4, present 2.0`, the per-replay counts' `submits 2.0`, the start line `rd:
  present: the present's pass is a list of its own, submitted after the scene's; only its submit
  waits for the display's image ...`; [RENDER.md](RENDER_PERF.md#the-presents-own-list)). The issue's last
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
  figure the mean a replay; [RENDER.md](RENDER_PERF.md#culling)). The issue's log showed 14.4 to 16.1
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

### The barrier scopes and the submission wait (issues 179 and 211)

A Mali-G52 MC2 (Arm's driver) and an Adreno 710 (Qualcomm's driver) lost the graphics device at the
first pictures of the first 3D scene, three starts in a row each, on the release that narrowed the
buffer barriers and brought the polled submission wait. The Mali ran the release before there
without a loss (its log); there is no log of the Adreno 710 on the release before. Every tile-based
phone GPU (every Android GPU, iOS through MoltenVK) now runs the wide barrier scopes, the texture
uploads before the VU pre-pass and the single submission wait (RHI.md "Barrier scopes and the
submission wait"); Macs and desktops keep the narrow scopes and the polled wait. The start of a log
says which ran and why, one line each:

    rhi_vk: barriers: wide (a tile-based phone GPU, Arm, vendor 0x13B5): every buffer barrier waits for ...
    rhi_vk: submission wait: single (a tile-based phone GPU, Arm, vendor 0x13B5): one wait without a time limit for a frame slot, ...
    rhi_vk: timestamps: ...; calibrated with the CPU's clock: off (the single submission wait: single (tile-based phone)) ...

and the summary's profile words end `, barriers wide (tile-based phone), submission wait single
(tile-based phone)`. The other changes of that release to a phone's GPU work in that scene run as
before it too: the VU record's store passes begin without an attachment (the throwaway targets only
on the Intel driver row or with the subpass read on: `rd: VU pre-pass: the store passes begin without
an attachment ...`), and the skin check issues no ring stamp, no stamp clear value and none of its
own compiles after a memory stop (`... not compiled in the background (<why>; a tile-based phone GPU:
none, ...)`). What still differs on a phone, all CPU work: the range checks, the hold notes and
pass crumbs, the timestamp bookkeeping, the SPIR-V scan, the skin check's copy search, and the loss
keep's gate (one progress read a frame; a copy only after a slot wait blocked 100 ms or more). How
to read the next log:

- Both lines `wide` / `single` and no loss: one of the two changes (or the store passes' targets,
  or the skin check's stamps on the Adreno) was the cause, or set off the driver's fault. Nothing
  tells them apart by a setting (there is none); nothing to ask for.
- Both lines present and the device still lost: none of them was; the loss dump's lines (below)
  and the stall lines (issue 123's sections) are the evidence then, and the next start draws
  without the VU pre-pass (the ladder below).
- `narrow (not a tile-based phone GPU)` or `polled (...)` on a phone: the GPU was not taken for a
  phone's (the reason's words say why); `(ICO_VK_FAKE_SCOPES=..., a test)` only ever in a test.
- With the single wait the stall diagnostics measure less: a stalled replay's `rd: GPU clock`
  line has no trace (which of its stamps were written while the CPU waited, the longest stretch
  with nothing new, its first list's second run), and its start is placed from its wait's end,
  not on the CPU's clock. Its counter restarts, unwritten stamps and the wait's own time are kept,
  and a wait of 100 ms or more gets the line without a trace, with the time from the replay's last
  submit to the wait's return: `waited 340.0 ms for it from 38.0 ms after its last submit (no
  trace: the single submission wait reads nothing of its work while it runs); its work done about
  378.0 ms after its last submit (the wait's end)` (`rd_perf_clock.c`; the CPU's clock read around
  the wait the backend makes anyway, nothing of the slot read while it runs).

The loss ladder, never written to the settings. On a phone a start after one or more lost starts in
a row draws without the VU pre-pass (the scopes and the wait are already the wide and single ones;
the Adreno 740's device-table row does the same; OpenGL ES changes nothing):

    ico_pc: loss ladder: 1 start in a row lost the graphics device before this one: on Vulkan this start draws without the VU pre-pass (a tile-based phone GPU, ...); nothing is written to the settings
    rhi_vk: VU pre-pass: off for this start on a tile-based phone GPU (Qualcomm, vendor 0x5143; the previous start lost the graphics device; nothing written to the settings): ...
    rd: VU pre-pass: off by the loss ladder (a tile-based phone GPU; the previous start lost the graphics device: this start only; [dev] vu_prepass = true draws with it anyway): ...

On a desktop's Vulkan the rungs are the wide scopes after one lost start and the single wait too
after two (`... on Vulkan this start runs the wide barrier scopes and the single submission wait
...; Direct3D and OpenGL change nothing`). The row is the lost starts in a row, in its own slot of the
activity file's `-ends.txt` (`loss_row <n>`): until this fix a `gpu_robust = true` in the settings (by
hand, or written by the game after two lost starts) set it back to 0 every start, and the start that
wrote it did too, so both reports said `starts in a row that lost the graphics device: 1` start after
start. The row now counts on (`ico_pc: the previous start lost the graphics device (2 in a row);
nothing is written to <path>: robust access is as the settings say ([dev] gpu_robust)`); a start
that ran robust without a loss ends it while the robust step's own count goes on (`... lost the
graphics device: 1 (2 counted for robust access)`). A desktop's restarts after a loss count in it too
(`loss_chain <n>`, `... before this one (this game's restarts after a loss): ...`). The start-up
loss bisect replays the lost frame under the row the lost start ran; on a phone it skips its
overlap step (a loss it reproduced could end the start).

Issue 211's logs (the Adreno 710, two runs) against what the narrowing release changed on
Qualcomm's path: in each run three replays in a row stalled 270-400 ms with "9 of 66 written (the
last: the start of piece 4 of 39, list 0)" (the scene's first list, right after the VU pre-pass's
record), each with a timestamp counter restart and the kernel's reset count moving during the
third (`resets 645705` to `645706`); the skin check's readbacks of those replays came back
unwritten ("the GPU did not run them"), and the next submission, an unnumbered one after the last
numbered replay finished (`the GPU reached 447; no wait failed`), failed with `VkResult -4`, then
the photo's capture failed the same way. A failed `vkQueueSubmit` with `VK_ERROR_DEVICE_LOST` is the
device's, not the submission's (every later call may fail; the second failure here shows it), so
the backend cannot tell a capture's lost submission from the picture's and keep running; the
skin-check capture and the frame dump it asks for are CPU work on the frame record, and the photo
a readback after the loss had already happened. The stalls themselves are issue 123's shape (the
Adreno 740 showed them without a loss, with the VU pre-pass off by its device row; the 710 has no
row and draws through the pre-pass, so after a lost start the ladder turns it off). Whether the
changes above keep the 710 from losing the device is what its next log says by the lines above.

### The first 3D scene's allocations made at start-up (issues 179 and 211)

Both losses (issues 179 and 211) came in the first pictures after the VU pre-pass's record grew in play (issue
179: 16 to 256 rows at `report/ico-pc.log:378`; issue 211: 16 to 128 to 256 rows at
`report/ico-pc.log:530` and `:535`), each growth a new buffer for every frame slot, beside the mesh
arena's first 32 MB chunk (the third of `3 allocations, 3 buffers` in the slow-replay line of the
first 3D picture). Not proven as the cause; the start-up loss bisect, which makes the record at its
full size at once, never lost the device. Both are made before the game runs, after the start-up
set, in one line:
`rd: start-up: made before play in T ms: the VU pre-pass's record at R rows (S slots, M MB over
the 2 frame slots; ...), the mesh arena's first chunk (32 MB); N MB in all; the bound: at most half
of the A MB above the free-memory floor (V MB available, floor F MB); the system: ...`. The record's
own line then ends `; made at start-up, before play)`, and a later growth in play
`; grown in play past the R rows made at start-up)`. With the numbers of the two reports: the Mali
(815 MB available, floor 641 MB) gets 512 rows and the chunk (80 MB), the Adreno 710 (968 MB,
floor 908 MB) 256 rows (24 MB, what its first scene grew to) and no chunk. Under the floor or
after the system's low-memory signal nothing is made (`kept at 16 rows (...)`, the bound's words
say which) and play grows the record as before. What to read: no `the record holds` line between
the first 3D picture and a loss means the growth was not there; a loss in the same place with it
gone rules the growth out.

What else play makes is named from the start of play on (`rd: made in play outside the key cache
in replay R (WHERE): pipeline "NAME" (...)` and `... buffer "NAME" of S KB (...)`, the first of
each name, then its first 8 and every 100th; the run's end line counts every name;
[RENDER_POST.md](RENDER_POST.md#start-up)): the pipelines a slow replay counts with no
`made on first use` line beside it (issue 179's 5 at replay 1, issue 211's 3 at replay 47) and
the buffers of the first 3D pictures (`rd ring` is the upload ring growing; the VU record's and the
mesh arena's carry their own names). Whatever a phone's next log names there is what the start-up
could make before play.

### The performance lines (issues 179, 211 and 123)

The tile-based phones run the wide barrier scopes and the single wait again, so their next log is
the measurement the next speed work is chosen from. Five lines in each 10 s block (and the run's
profile) say where a picture's time goes, which side holds the rate back, and rank the candidate
savings from the block's own numbers, keyed by scene so one log covers the menus, play, the boat
and the cage apart (`port/render/rd_perf_rank.c`, `port/platform/window_perf_rank.c`). Nothing is
asked of the GPU for them: no query, timestamp, barrier or read beyond the timestamps every
picture writes and the replays already timed pass by pass (one every 250 ms), read where they were
read before; the attachment bytes are counted on the CPU from each pass's description
(`port/rhi/rhi_traffic.c`, at `rhi_cmd_begin_render_pass` and the texture copies, a lock and a
hash lookup a pass). The scene is the block's dominant layout with the stage number and the game's
name of the stage on every kind once a stage is loaded (`stage 4 play (st04a (...))`, `stage 4
scene (...)`, `stage 1 other (...)`: the logo's, the attract's and a cutscene's other layouts are told apart by
their stage; `title` before any stage), the vibration choice, the memory card's file list,
the Continue screen's file list, the load and the save's confirmation each with a word of its own
(`vibration setting`, `memory card files`, `Continue files`, `loading`, `save done`; `window_scene_key`,
`port/platform/window_scene.c`), `(keep frames)` when most of its pictures were keep frames. The
figures below are the unit test's made-up block of stage 3 play's shape (`rd_perf_rank_test`), not
a phone's.

- `window: perf scene: stage 3 play, 168 pictures (0 keep frames): 16.8 of 25.0 fps (the game 25.0
  of 25.0), GPU-bound; the GPU's work 59.0 ms a picture of the 59.5 ms between presents (99 %); the
  render thread's own CPU 13.3 ms (walk 9.2, submit 1.7, uploads 2.0, binds 0.4, interpolation
  0.0), its waits for the GPU 27.5 ms, its present calls 25.8 ms; the game step's CPU 2.9 ms of its
  40.0 ms tick; a picture's work done 61.0 ms after its last submit (longest 61.0), the slot wait
  blocked 168 times (27.0 ms on average, longest 27.0); passes asked a picture: 20.0 passes, colour
  8.0 MB loaded 9.0 MB stored, depth and stencil 4.0 MB loaded 2.0 MB stored, 3.0 MB cleared, 6.00
  Mpx of area, copies 1.0 MB (each copy once: as many read as written; uncompressed; 1.0 passes with an attachment of no known format, at 4
  bytes a texel); 360 draws, 329 pipeline and 458 bind group binds, 46k vertex invocations, 1826 KB
  uploaded (1.0 textures), 3.0 DATE snapshots; GPU a picture: span 60.0 ms, ...; the pictures' GPU
  work median 59.5 ms, worst 63 ms`. Decides the limiting side, from the block alone: below 95 %
  of the target rate (Frame rate Original: the game's 25 or 30; with interpolation the display's
  refresh) or the game under its own rate, the GPU when its work (the span less the display wait)
  fills 80 % of the time between presents; else the render thread when its own CPU (interpolation,
  uploads, walk, binds, and the submit only where the render thread submitted itself: not its
  waits) does; else the game thread when its step's CPU
  fills 80 % of its tick; else `bound by waits elsewhere` (the present calls, the slot waits).
  With the submit thread on (`RdPerfRecord.presentThread`), its submit and present calls are
  named apart and are not the render thread's CPU: `the render thread's own CPU 42.4 ms (walk 40.0,
  uploads 2.0, binds 0.4, interpolation 0.0), its waits for the GPU 27.5 ms; the submit thread's
  submit 1.7 ms and present calls 25.8 ms (100 of the 100 pictures)`. The replays line `window: N replays: CPU ...` sums
  every phase of a replay whichever thread ran it; with the submit thread on it says so after the
  fence waits: `(fence waits 15.95; the CPU adds the submit thread's submit and present in 277 of
  277 replays)`.
  `a picture's work done N ms after its last submit` is the single wait's own return against the
  picture's last submit: the GPU's latency for a picture where the wait blocked (an upper bound
  where the work had finished first). The bytes are what the passes ASK for (LOAD loads, STORE
  stores, at the format's bytes a texel times the samples over the render area): Arm's AFBC or
  Qualcomm's UBWC move fewer, and a store nothing reads may be dropped by the driver, so they are
  the ceiling of the tile-memory traffic and the counters a saving scales with.
- `rd_replay_tool --stats` prints one picture's figures of the same kind for a dump A/B: `<dump>:
  stats: traffic: colour 50.70 MB loaded 120.50 MB stored, depth and stencil 12.50/24.70 MB
  loaded/stored, 70.30 MB cleared, 9.90 MB copied (each copy counted once: as many MB read as
  written), 36 passes (1 with an attachment of no known format, at 4 bytes a texel)` (the example's
  figures are made up to the shape of a Pixel stage 4 picture).
- `window: perf classes a picture (12 timed replays): list 3 22.40 ms: 1.0 passes, 61 draws, 61
  binds, colour 1.8/3.6 MB, depth 1.8/0.0 MB loaded/stored, 0.0 MB cleared, 0.47 Mpx; list 0 20.10
  ms: ...; DATE snapshots 3.60 ms: 3.0 passes, ... (6 of 9 classes; all 59.90 ms)`: the timed
  pieces by class (a list, an effect, the shadow count, the DATE snapshots, the copies, the
  pre-pass, the uploads, the present), each with what its time scales with. Two blocks of the same
  scene that differ in a counter (another camera, another room) are a free A/B: ms over the
  counter's change. On a tile-based GPU a pass's stamps can carry the earlier passes' tile work
  (the top passes line says so): read a class against its own counters, the scene line for the
  picture.
- `window: perf rates (this device, from the block's own pictures): the DATE snapshots and passes
  with no draw 0.10 ms a pass + 0.40 ms a MB loaded or stored (R^2 1.00, 36 pieces); copies 0.25
  ms a MB (24.0 MB in 6.0 ms); the GPU's work 36.0 ms + 0.500 ms a thousand vertex invocations
  (R^2 1.00, 168 pictures without keep frames, 40.0k to 52.0k invocations; not used for an
  estimate); the estimates use the byte-bound passes' fit (0.40 ms a MB); the copies' rate is never
  used for attachment traffic`: this device's constants with no extra work: a least-squares fit
  over the pieces whose time is their bytes (the DATE snapshots, the passes with no draw: a fixed
  cost a pass and a cost a MB moved), the copies' and snapshots' ms a MB, and the vertex fit (the
  GPU's work against the vertex invocations, keep frames left out, with the invocations' range).
  The estimates take this block's fit's ms a MB where it holds (8 pieces, a spread in their bytes,
  R^2 0.3 or over), else the last fit that held earlier in the run (`the estimates use the rate of
  an earlier block of this run (0.10 ms a MB)`), else none (`the estimates use no rate (no fit in
  this block or an earlier one of the run: the traffic estimates left out)`: the MB are still in
  the scene and classes lines). Never the copies' rate: on a tile-based GPU a copy's piece carries
  the flush of the pass before it (a Pixel 10 Pro Fold's log read 0.01 ms a MB from the copies against
  0.05 to 0.16 from the same phone's fits, five to fifteen times low). `no fit (N, or no spread in
  their bytes)` where every snapshot moved the same bytes. The copies' MB count each copy once (the bytes read, as many
  written).
- `window: perf slowest picture: replay 4821 (frame 4820), GPU span 63.1 ms over 20 pieces: list 3
  912x512 24.10 ms (70 draws, 66 binds; loads 4.1 MB, stores 4.1 MB), ... (the top 5 by GPU time;
  GPU time by pipeline is not measured)`: the block's slowest timed picture's costliest pieces.
- `window: perf ranked: stage 3 play: 16.8 of 25.0 fps (the game 25.0 of 25.0), GPU-bound (the
  GPU's work 59.0 ms a picture of the 59.5 ms between presents): 1. list 3 22.4 ms measured (1.0
  passes, 61 draws, 61 binds; loads 3.6 MB, stores 3.6 MB, 0.47 Mpx); 2. list 0 20.1 ms measured
  (...); 3. DATE breaks est. 7.9 ms (3.0 a picture: 3.6 ms measured for the snapshots + 10.8 MB
  reloaded after them); no vertex work estimate (the invocations (40.0k to 52.0k a picture) are
  under the 2x spread the fit needs) (each saving alone, they overlap; est. = this block's counters
  times its own rates)`:
  only in a block below the rate. The three largest candidates for the limiting side: for the GPU
  each class's measured time (its own ceiling), `tile loads est.` (the MB loaded a picture times
  the rate), `depth stores est.` (the depth and stencil stored), `pass count est.` (the passes
  times the fit's fixed cost a pass), `DATE breaks est.` (the snapshots' measured time plus the MB
  a scene pass loads again after each), `vertex work est.` (the invocations times the vertex fit's
  slope, only from a fit of R^2 0.8 or over whose largest invocation count is twice its smallest,
  the rule of the VU line, keep frames left out; with the VU pre-pass on no more than the measured
  `VU pre-pass` class, `; capped at the measured VU pre-pass`), the uploads; for the render thread
  its phases with their draws and binds; for the game thread its step. Without a vertex estimate the
  line says why: `no vertex work estimate (the fit's R^2 0.27 is under 0.80, the invocations (23.0k
  to 36.6k a picture) are under the 2x spread the fit needs)` (a weaker rule once ranked a 24.8 ms
  vertex estimate first from an R^2 0.12 fit in a 31 ms picture whose pre-pass measured 1.5 ms). Every
  class and estimate is a candidate (34 classes can qualify; the table holds 48 and drops only the
  smallest past that). Decides the next release's work: the first entry is the largest measured or
  estimated saving on this phone in this scene.
- `window: performance profile (every 300 s of the run, 4 scenes, profile 1 of the run):` then one
  line a scene, `window: performance profile: stage 3 play: 18 blocks (180 s, 3024 pictures); a
  picture's GPU work median 47.5 ms, worst 109 ms; presented fps median 16.9, lowest block 12.4,
  of 25.0; GPU-bound in 18 blocks; the worst block: 12.4 of 25.0 fps ... 1. ...; 2. ...; 3. ...`,
  every 300 s of blocks, at the run's end, and as the app goes to the background or ends (`(the app
  going to the background, ...)`, `(the app ending, ...)`: the lifecycle's step before the log's
  flush, `window_lifecycle.c`; not twice within a second, so one swipe away writes one). Those
  three add the block in progress, which no 10 s line has yet: `window: performance profile: stage
  4 play (st04a (...)) (the block in progress): 10.0 s, 277 pictures (1 keep frames) not yet in a 10
  s block; a picture's GPU work median 31.5 ms, worst 38 ms; presented fps about 27.7 of 120.0,
  GPU-bound: 1. glow 11.6 ms measured (...); 2. ...; 3. ...` (the rate from its records' starts;
  `(the target rate not known before the first 10 s block)` in a run swiped away before one). The
  bug report's `report.txt` has a section `the performance lines by scene (the latest ranking and
  profile of each)` built from the log, so one zip carries the ranking of every scene: 16 scenes,
  the one seen longest ago giving its place to a new one (the run's latest 16).

What the lines cannot size, and why: overdraw (no fragment count without a query: the area
texels a pass covers are its floor), GPU time by pipeline or draw (no stamps inside a pass beyond
the lists' own), the robust-access cost (`gpu_robust` is the player's setting and an A/B would need
it changed), whether the pre-pass overlaps the picture before on the GPU (the stamps are written
once the work before them is done), the GPU's clock steps on a Mali whose kernel nodes are closed
(the kernel GPU driver line says which are readable; the thermal line gives the app's headroom).

### The glow's light plan

At every scale the glow's chain is not drawn where it adds no light
([RENDER_POST.md](RENDER_POST.md#the-glow-where-it-adds-light)). The 10 s block says how each
glow picture was drawn: `window: glow: light (every scale): 412 dark (3 passes in their chains), 0 lamps (mask not drawn), 37 whole
(37 the sun's fans on the picture); lazy clears drawn: 0` (a lazy clear drawn in play means
something read a glow buffer the plan left undrawn: the targets are named): the passes in the dark chains are WORK1's zero draws after a
picture that drew the chain (0 a picture in a steady dark room); the dark ones skip the mask's full-frame pass, the paste and the
small passes (on the PowerVR phone 11.6 ms of a 32 ms picture in the measured room), the lamps
ones the mask's pass; the whole ones name their reason (`a grey fill (the bloom)`, `the sun's
fans on the picture`, `SCENE's alpha read before it is rewritten`, `WORK1, WORK2 or WORK3 read
outside the chain`, `the chain not in its known form`, `anti-aliasing (MSAA)`, `the sun's tint or
glare with light in the chain`, `off since a frame read a planned frame's alpha`), at 1x as at
any other scale. A block of play whose glow rooms say whole for any reason but the fans or
the bloom names a form the scan did not expect: the once lines `rd: glow: light plan: the chain
drawn whole: <reason> (<detail>; frame N; ...)` say which command, and the player's F12 dump
replays it (`rd_replay_tool --stats` prints `glow: light plan on: <plan> (<reason>)`). The
`perf classes` line's glow class shows what the plan saved; the first dark frame is logged once
(`rd: glow: light plan: at every scale nothing in frame N puts light into the glow ...`).

### The late passes on SCENE

On a tile-based phone the crack fill of a glow room whose glow draws nothing (the dark plan above)
is drawn in the aura's paste pass instead of a pass of its own, and in a lamps room in the glow's
paste pass
([RENDER_POST.md](RENDER_POST.md#the-late-passes-on-scene)). The start line `rd: late pass: on (a
tile-based GPU's default): ...` (or `off (<why>)`) says whether it is in force; the 10 s block's
`window: late pass: N crack fills held into the aura's paste pass, G into the glow's, M drawn early
in a pass of their own (K <why>, ...); D glow pastes in the aura's paste pass, E drawn early; J
list 11 draws joined the open pass without SCENE's depth (P passes not begun)` counts the pictures it saved a pass in (N), those where something between the glow and the
aura drew on SCENE first (M, by reason: `a draw`, `a command on SCENE`, `the aura's paste pass could
not take it`, `SCENE's snapshot taken again`, ...), and the paused or menu pictures whose list 11
draws went into the aura's pass instead of a pass of their own (P, one a paused picture; a few texels of the menu's
text may land a shade apart, DIVERGENCES.md VD-31); its last clause, `N fills not
held (no paste in the frame takes them)`, counts the pictures of a room without the aura (the
opening) whose fill is drawn where it is due, with its snapshot by the swap. The crack fill's own 10 s
line then shows its `after the glow's alpha` count falling and `in the aura's paste pass` rising by
N, and `held past SCENE's border repeat` where the frame's outer columns are repeated; the `perf
classes` line's crack fill class loses its pass (its colour copy stays). Dark rooms with many
`drawn early` say a command the hold did not expect; the player's F12 dump replays it
(`rd_replay_tool --stats` prints `late pass on: N crack fills held ..., M drawn early (<why>)`).

### The lamps' glow for less

In a lamps room (the title's castle; `window: glow: light: ... L lamps`) the pass that draws the
lamps into WORK2 takes only the lamps' tiles and those the picture before lit, and WORK2_PAD's band
is no pass of its own ([RENDER_POST.md](RENDER_POST.md#the-lamps-glow-for-less)). The start lines
`rd: lamps: area on (self-test passed); band lazy on: ...` and `rd: lamps: area self-test passed:
...` say it is in force (`area off (not the PowerVR or a desktop class ...)` on the Arm, Qualcomm
and Apple phones, where only the band is; `area off (its self-test not passed)` with the
self-test's line naming the texel). The 10 s block's `window: lamps: N lamps passes on WORK2 with
an area (P % of their texels loaded and stored; X MB not cleared, ...), M whole (<why>); B bands
left lazy` counts the pictures that saved the traffic (N) and those drawn whole by reason (`the
WORK2 an earlier frame left not known`: the first lamps picture after a whole or other one, about
one a room entry; `a lamp whose screen box is not known`: its part in the line's last clause,
`boxes not known: k <why>` (`a vertex at or behind the eye`, `not a mesh or a screen prim`, `clip
mode none ...`), and once a part `rd: lamps: a lamp's screen box not known: <why> (command type
T at ordinal O, mesh "N" of V vertices, program P code C clip mode M, B vertices behind the eye;
frame F)` naming the draw; `another command writes WORK2 in the frame`; `the lamps and the lit region cover WORK2`).
Many whole pictures for one of the last three in a steady lamps room name a frame the player's F12
dump can replay (`rd_replay_tool --stats` prints `stats: lamps ...`). The `perf classes` line's
glow class shows the change: its depth loaded and colour cleared and stored fall by about the
share of WORK2 outside the lamps' tiles. The same line counts the round-3 cuts: `B bands left lazy` (lamps and
whole pictures), `O WORK0 alpha sprites owed (dark pictures: a pass fewer each)`, `F blur passes of
two pairs (a pass fewer each), P pairs drawn alone (k <why>)` (about one `the chain's last pair
next` a chain is expected; any other reason says what kept two pairs apart); the start lines `rd:
blur fold: two pairs a pass: self-test passed (...)` (or `failed at texel N` / `not run (...)`,
then one pair a pass) say whether that fold is in force. It is off by default: on the
PowerVR phone (2026-11-12) it made the lamps pictures 28.3 to 30.9 ms against 25.7 to 26.0 without (`GPU top
passes`: `glow WORK0 424x128 9.0 passes 9 draws 8.80 ms` against 12 passes in 3.2 ms). Under the
tests' `ICO_RD_LAMPS=...+lowres` a line of its own follows: `window: lamps low resolution: N
pictures with WORK2 WxH under SCENE SWxSH, D draws into WORK2 on its own depth, F depth fills
(depth_down_ps), X draws without a depth; H pictures whose lamps let the crack fill's hold pass
(fillhold); P WORK2 passes with 4 samples resolved in the pass, Q single-sampled (lowresms)` (on the PowerVR phone by default: P about one a lamps picture, Q 0 in a lamps room, a once-line naming the draw of any single-sampled pass; the start line `rd: lamps: low resolution on (the build's default; RD_LAMPS_MS_DEFAULT 1, ...): ... 4 samples resolved in the pass (lowresms)` and `rd: lamps: low resolution with 4 samples: self-test passed (...)`; fillhold off by default; WxH the GS frame's size, 848x448 at the phone's Auto; X is 0 in a healthy run, the
once line `rd: lamps: low resolution: a draw into WORK2 ... drawn without a depth (<why>)` names
the reason; with H the late pass's `N crack fills held ... into the glow's` rises by about one a
lamps picture and `glow WORK2 ...` in `GPU top passes` falls to the GS size).

Where the glow's blur in two passes is in force (the default, `RD_GLOW_BLUR_DEFAULT` 1,
or the tests' word `glowblur`; [RENDER_POST.md](RENDER_POST.md#the-glows-blur-in-two-passes), VD-33): the
start line `rd: glow blur: on: ...`, once `rd: glow blur: the glow's 10 blur pairs drawn as two
passes on WORK0 424x128 (...)` on the phone's Auto, and the 10 s `window: glow blur: N pictures
with the glow's blur in two passes (P pairs, about Q passes fewer), R chains drawn as recorded
(<counts by why>)`: N about the lamps and whole pictures of the block, R 0 there (a reason named
with its count otherwise, and once in the log). In `GPU top passes` `glow WORK0 424x128 N passes`
falls from about 12 to about 3 a lamps picture (the reduction, the down pass, the alpha sprite),
`glow WORK1 ...` stays at one (the across pass); the saving to expect is nine small passes, about
2 ms against the lamps pictures' 23 to 25.5 ms, less the two passes' own cost, which those lines
show.

### The crack fill's snapshot by the swap

The crack fill's start-up check prints `rd: crack fill: swap: on (self-test: 256 levels a channel
...)` where the phone keeps colours exactly through the fill's shader, else `swap: off (self-test
failed at texel N: ... for ...)` and the fill copies SCENE as before
([RENDER_POST.md](RENDER_POST.md#the-crack-fills-snapshot-by-the-swap)). The 10 s crack fill line's
`; N snapshots by the swap, M copied (K <why>)` says how often the copy was saved; in a dark glow
room the late pass holds the fill and the copy stays (`the draw held past the snapshot`).

### The crack fill in list 11's pass

Off by default in this release (the start line says `off (the default in this release: ...)`):
with it on (`ICO_RD_FILL11=1`), the crack fill of a picture with no glow and no aura (the
title's dark pictures) is drawn as the first draw of list 11's pass, over a scratch depth, instead
of a pass of its own ([RENDER_POST.md](RENDER_POST.md#the-crack-fill-in-list-11s-pass)). The start
line `rd: crack fill: with list 11: on (self-test passed: ...; the PowerVR class)` (or `off
(<why>)`: `not the PowerVR or a desktop class` on the Arm and Qualcomm phones, where it is off this
release, or the self-test's failure) says whether it is in force; once `rd: crack fill: with list
11: the scratch depth made (...)` and `rd: crack fill: drawn as the first draw of list 11's pass
...`. The 10 s line `rd: crack fill: with list 11: N drawn in list 11's pass over the scratch depth
(X MB of colour not loaded and as many not stored), K held and drawn in a pass of their own after
all (<why>), J kept in their own pass (<why>); S merged passes ended by a draw that uses SCENE's
depth` counts the pictures it saved a pass in (N), those where something before list 11 touched
SCENE after the fill was held (K: `a command that reads or writes SCENE`, `SCENE's depth bound,
written or DATE`, `SCENE's border repeat`, `list 11's pass holding a part of the picture`, `a later
draw of list 11 that uses SCENE's depth`, ...) and those not held at all (J: `the next command
reads or writes SCENE`, the usual reason in gameplay, `due after list 11`, ...). S is 0 unless a
draw the look ahead did not expect came into the merged pass. With it on, the `perf classes` line's
crack fill class loses the title's fill pass and list 11's class keeps one pass on SCENE; the
crack fill's own 10 s line still counts the fills (`after the glow's alpha`, `snapshots by the
swap`). The player's F12 dump replays it (`rd_replay_tool --stats` prints `crack fill with list 11
on|off (self-test ...): N drawn in list 11's pass ...`).

### The aura's paste as its constant part

In a room with no aura object on screen the aura's paste is drawn as the alpha alone over all but a
band of SCENE's bottom rows ([RENDER_POST.md](RENDER_POST.md#the-auras-paste-as-its-constant-part)).
The 10 s block's `window: aura paste: P drawn as the alpha alone (B band rows each), K read whole
(why)` says how often (B about 18 at the phone's 1080 rows); `read whole` names why the proof did not
hold (`its source not proven alpha 0`: an aura object on screen, the usual reason in the aura's own
rooms; `no local read`: a device that reads the paste's destination by a copy). The `perf classes`
line's aura class shows the shading it saved.

### DATE's passes without the depth (issues 179 and 123)

On a phone with `DATE snapshot` in its profile line (Arm, Qualcomm without the in-pass read) the
shadow's three composites end SCENE's pass three times a picture, and the passes after the first
two snapshots begin without SCENE's depth and stencil ([WORK_BUFFERS.md](WORK_BUFFERS.md#dates-passes-without-the-depth)).
What the log shows:

- once at the first replay: `rd: DATE cut: on: a DATE snapshot's pass whose draws neither test nor
  write the depth begins without it (the same picture)`, or `off (...)` with why (the stencil path
  or the shader's read in force: no pass ends for DATE there);
- once at the first such pass: `rd: DATE cut: SCENE's pass after a DATE snapshot begun without its
  depth (frame F, list 3) ...`;
- in each 10 s block's `window: per replay` line, after `stores:`: `; DATE: 2.0 passes a replay
  begun without the depth their draws never touch (up to M MB of depth and stencil loads and as many
  stores not asked), 0.0 begun again with it; snapshots 3.0 a replay taken (the first 1.0, another
  target 0.0, an alpha write since 2.0, an area not held 0.0, the copy used elsewhere 0.0), 0.0
  reused` in a block of play with the shadow on screen. 2.0 passes and M near 4.5 at the Mali's 1x
  (912 x 512 x 5 bytes, twice) say the cut fired in every picture; `begun again` above 0 names a
  frame whose prediction failed (a pass more each, the same picture: the line `rd: DATE cut: a draw
  that needs SCENE's depth after its pass began without it (frame F, list L)` names it, the first
  8 then every 100th);
- the `perf classes` line's list 3: its depth MB loaded and stored fall by about two thirds (two of
  its three SCENE passes) at the same passes; its ms against the last log's at the same scene is
  this device's price of that many MB (issue 179's rates line had no fit for it).

### The shadow tiles

Off by default on every device ([RENDER_POST.md](RENDER_POST.md#the-shadow-tiles): the owner's
PowerVR phone measured it slower). What the log shows:

- once at start, after the stencil's line: `rd: shadow tiles: off (not measured to help on a
  tile-based GPU; ICO_RD_SHADOW_TILE=1 turns it on for the tests)` (or `DATE by the snapshot
  here`, `the shaders are not on this backend`); with the tests' switch, `rd: shadow tiles: on
  (ICO_RD_SHADOW_TILE=1; self-test ok: SCENE the same bytes over N texels, L of M mask texels
  live)`, and then:
- once: `rd: shadow tiles: the first composite over the tile mask (command N, SHADOWk WxH, mask
  WxH, the set-up over WxH texels)`;
- a composite drawn whole: `rd: shadow tiles: a composite drawn whole: <why>`, the first 8 then
  every 100th;
- in each 10 s block's `window: per replay` line, after the shadow box's clause: `; shadow tiles:
  2.9 composites a replay over the tile mask (2.9 masks), 0.0 whole` with the shadows on screen;
  the `perf classes` line's list 3 shows what it saved. The live tiles are not counted on the
  device: `rd_replay_tool --stats` on the player's F12 frame prints them.

### The shadows over the volumes' box

On every device the shadow count's one draw and the three shadow composites are drawn over the
volumes' box alone ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadows-over-the-volumes-box)). What the
log shows:

- once, at the first boxed composite: `rd: shadow box: on: the shadow count and its composites
  drawn over the volumes' box (the first: command N, GS pixels ...); the same bytes`;
- a count or composite drawn whole: `rd: shadow box: drawn whole: <why> (command N; ...)`, the
  first 8 then every 100th: `the count by the seven draws` (a device whose count is the seven
  draws: no box anywhere), `the count in the MSAA epoch`, `the source holds no box` (another write
  to a level, a level not drawn), `the draw's form`;
- in each 10 s block's `window: per replay` line: `; shadow box: 1.0 counts and 3.0 composites a
  replay over the volumes' box (B % of their picture), 0.0 whole` in a block with the shadows on
  screen; B is the share of the composites' texels still drawn; the `perf classes` line's list 3
  shows what it saved.

### The CPU phases

Every 10 s block has, after the late passes' and the aura paste's lines, `window: CPU phases ms a
picture over N pictures (R a tick): interp copy ..., index ..., blend ..., ...; upload cull ...,
...; walk pre-pass ..., late uploads ..., lists ...; per picture P pipeline look-ups (H hits), B
uniform blocks looked up (Z hits); 22.0 clock reads a picture`
([RENDER_PERF.md](RENDER_PERF.md#the-cpu-phases)). It says where the CPU line's interp, upload and
walk go on the device: on the owner's phone the cage's 4.4 ms of interp and 5.3 ms of walk were
not split before. The look-ups say how many DrawCB blocks and pipeline keys a picture hashes (the
hash is 8 bytes a step since this release).

### A shadow window with no volume

Off by default on every device in this release (the PowerVR phone flickered black with it on,
cause not found: [RENDER_POST.md](RENDER_POST.md#a-shadow-window-with-no-volume)); with it on a
shadow window with no volume keeps SCENE's pass open and the count and levels are written as zero
clears. What the log shows:

- once a renderer, at the first replay with a window: `rd: shadows: no volume: off (the default in
  this release: the PowerVR phone flickered black with it on, cause not found): ...` (with it on:
  `on (the PowerVR or MoltenVK class, RD_NO_VOLUME_DEFAULT 1)`, `on (ICO_RD_NO_VOLUME=1)`; refused:
  `off (the composites' DATE by the R8 snapshot or the shader's read)`, ...);
- once: `rd: shadows: no volume: frame F, the window at commands R..S has no volume: its count
  (the reduced count) and levels owed as zero clears, SCENE's pass kept open (the same bytes;
  reported once)`;
- a window not taken: `rd: shadows: no volume: a window not taken in frame F (reset at command
  N): <why>`, the first 8 then every 100th;
- in each 10 s block's `window: per replay` line, after the shadow tiles' clause: `; no volume:
  0.0 windows a replay without a volume kept SCENE's pass open (...), 0.1 with volumes, 0.9 not
  taken (class 0.9)` at the title by default (the windows it would take counted under `class`).
  With it on, a release candidate's title block read `1.0 windows a replay ... kept SCENE's pass open (1.0 with it
  open, up to 42.9 MB ...)`, the pause menu's `0.8`.

The break mode (off by default until a phone release candidate: `RD_NO_VOLUME_BREAK_DEFAULT`
0) takes the same windows with SCENE's pass ended at the resolve as before
([RENDER_POST.md](RENDER_POST.md#a-shadow-window-with-no-volume)): its gate line reads `rd: shadows:
no volume: on with SCENE's pass ended at the resolve (...)`, and the 10 s clause above ends `, 1.0
with SCENE's pass ended at the resolve (C zero clears, H targets holding zero already)` (on the
title, picture after picture, C near 0 and H near 4: the count and the levels already hold the
zeros).

### One DATE set-up for the shadow composites, the empty composites

On every device ([RENDER_POST.md](RENDER_POST.md#one-date-set-up-for-the-shadow-composites)): the
second and third shadow composites use the first one's DATE snapshot (the Mali, Qualcomm's driver,
the PowerVR B class, Direct3D 11, OpenGL) or stencil set-up (the Pixel, the Adreno 710), and a
composite with nothing to draw (no volume) is skipped with its set-up. What the log shows:

- once a renderer: `rd: DATE one set-up: on: the shadow composites share one DATE snapshot or
  stencil set-up where none can set the alpha's top bit (...)` and `rd: shadows: empty composites:
  on: ...`; once each: `rd: DATE one set-up: the first shared (frame F, command C with command P's
  snapshot)` and `rd: shadows: empty composites: the first skipped (command C, ...)`;
- a composite kept to its own set-up (the stage-setting debug menu's 128 x 128 is the one retail
  way): `rd: DATE one set-up: a composite could write alpha 128 (level alpha up to L x vertex alpha
  A, frame F, command C): ...` or `rd: DATE one set-up: command C retakes its set-up as before:
  <why>`, the first 8 then every 100th;
- in each 10 s block's `window: per replay` line, after the DATE clause: `; DATE one set-up: 2.0
  composites a replay shared the set-up before them, 0.0 kept their own` in pictures with volumes
  (the DATE clause's snapshots then read about 1.0 a replay, `an alpha write since` near 0, where
  each composite taking its own reads 3.0 and 2.0); after the shadow box's clause: `; empty composites: 3.0 a replay
  skipped (3.0 with their DATE set-up)` in pictures with no volume.

### The stencil outside the shadow window (the PowerVR phone)

On the PowerVR class with the fog's local read (`fog local on` in the profile line, `rd: fog: depth
path local`) the stencil of SCENE's depth is loaded and stored only inside the shadow window once a
start-up self-test passed; every other class keeps it with the depth on every pass
([WORK_BUFFERS.md](WORK_BUFFERS.md#the-stencil-outside-the-shadow-window)). What the log shows:

- once at start, after the fog's and the shadow count's self-test lines: `rd: stencil: loaded and
  stored inside the shadow window only (self-test ok); 8 texels the GS's and the forced replay's;
  the fog by the local read, ...; fog path local, SCENE WxH texels`, or `rd: stencil: loaded and
  stored with the depth (<why>)`: `an Adreno GPU: ...`, `not the PowerVR class: ...`, `the fog reads
  a copy of the depth`, `self-test failed at SCENE sample N: <got> for <want> (...)` (with `rd:
  profile: stencil outside the shadow window off (self-test)` before it), `self-test error: ...`;
  on the desktop's in-place path `(the fog reads the depth in place: the desktop's path, never
  forced)`;
- a later fog path change that changes it: `rd: stencil: now loaded and stored ... (...; fog path P;
  N changes)`, the first 8 then every 100th;
- in each 10 s block's `window: per replay` line, after `stores:`: `; stencil: S stores and L loads
  a replay not asked with the depth's (X/Y MB), loaded and stored inside the shadow window only`.
  On the phone at 2364 x 1080 each such pass is 2.4 MB: S near the passes that keep SCENE's depth
  outside the window (list 3's and after) says the cut fired; no `; stencil:` clause says it is off
  (the clause is printed only where the cut is on);
- the `perf classes` line's list 3: its depth MB loaded and stored fall by a fifth (1 byte of the
  5 a texel) at the same passes.

A fog drawn at full strength on near geometry on such a phone (the Adreno 710's symptom) with the
cut's line in the log is this cut's: the self-test passed and the game's frames still read the
depth as far.

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
  N falls to 1 to 3 a picture. On a phone the twins start there (`rd: VU skin check: twins on (...)
  at one skinned draw a picture from the start (a tile-based phone GPU: ...)`), so a first start's
  3D scene carries one twin a picture, not every skinned draw for 2000 replays (issue 211's report
  had them all on when its device was lost). `rd: VU skin check: twins re-armed by a twin event ...` says a fault
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
- **Which bytes the GPU read** ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#the-trace)): a
  stamp line `rd: VU skin check: frame F command L:I replay R (...): the readback was not written by
  this replay's copies (the probe target's clear stamp: replay Q's ...)` says the replay's copies
  never landed (the check's wrong vertices are then an earlier replay's, not a bone read); `... the
  copies ran ... but the ring's stamp reads replay Q's` says the GPU saw the upload ring as an
  earlier replay left it. After a line whose copy differs, `rd: VU skin check: frame F command L:I
  the GPU's copy of the bone block (frame slot S's ring, offset O ...): WHERE` names the block it
  holds (the slot's readback as before, a block checked in replay Q, a block in a ring now, all
  zeros) or `other memory`. Compare R with the `a present blocked ... (replay R ...)` and `rd: GPU
  timestamps: the clock went back ... at replay R` lines: the one failure of issue 123's report was recorded in the
  replay whose present blocked 340 ms. Where the start-up set stopped for memory on Qualcomm's own
  driver the box check and the twins are now compiled carefully (`rd: VU skin check: its box check,
  its twins' pipelines and its c2 variant are compiled carefully (...)`), one at a time above the
  floor; that report's run had neither. There the twins start at one skinned draw a picture and a
  twin event twins every skinned draw for 30 replays only, at most 4 times a run (`rd: VU skin
  check: twins on (...) at one skinned draw a picture from the start (the careful rule: ...)`;
  [VU_SKIN_CHECK.md](VU_SKIN_CHECK.md), the thinning), so the check costs that GPU-bound phone no
  more than one twin a picture outside those stretches.
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

- `window: loss dump: the lost submission is replay R, frame F (<how the backend named it>; wait
  failed on frame W, first open frame O, newest numbered frame N); the frame before it is B; held
  now: frames A..Z (n, c of them copies in memory, K KB)` and `window: loss dump: copies in
  memory: ...`: which frame the loss belongs to and what the renderer still held. The ring holds
  the last three frames only; a frame the GPU had not finished when the ring reused its slot is
  copied to memory first while a slot wait has blocked 100 ms or more (`rd: loss keep: frame N
  copied to memory ...`, the first eight and every hundredth), at most 4 copies of at most 16 MiB,
  at most 4 tries in any 64 frames, nothing in steady play. So the lost frame is held only when a
  stall came before the loss (as in both reports); otherwise the frames the ring holds are written,
  as before. `copies in memory: not used` says a copy in progress did not finish in time. A dump taken from the ring alone, for a loss at frame 129, held frames 132 and 131 and
  so replayed the wrong frame in the next start's bisect; a line `the lost frame F is not held any more ... writing
  frame X instead ... it is NOT the frame whose submission failed` says the true frame is gone,
  and the closing line says `NOT the lost one`;
- `window: loss dump: writing frame N, the lost one (replay R), to .../logs/loss-<stamp>-lost.rddump`
  (`the best guess at the lost one` when the failed submit was outside a picture, as on the Adreno 710),
  then `... writing frame M (the one replayed before it) to ...-before.rddump`: the frames' writes
  (the CPU's copies of the commands, texels and meshes, onto the phone's shared storage);
- `window: loss dump: zipping K MB (the two frames and the log) at store level`: the zip, stored
  (no deflate), K the files' size together;
- `window: loss dump: frames N (the lost one) and M (the one before) written to <zip> in T s`:
  done, T from the first step; then the prune line (`n loss zips ... : d deleted (names)`). The
  zip just written is never deleted: `the name order alone would have deleted this zip (the clock
  went back or the zone changed ...); it was kept` says it would have been.

The start after a loss replays the lost frame pass by pass (`ico_pc: loss bisect: ...`). On a
desktop, if that runs clean, it replays the frame before it and the lost one back to back with no
wait for the GPU between them, four times each, as the game's pictures overlap: `window: loss
bisect: overlap step: ...` says that it ran and whether the device was lost. On a phone the line is
`overlap step not run (a phone: a loss it reproduced could end this start, ...)`: a driver that
makes no second device after a loss would end the start at a message box, and a phone cannot start
the game again by itself. When the zip cannot be read the line says which of
`cannot open <zip>: <errno text> (errno N)`, `<zip> is not a zip: N bytes, ...` and `<zip> is a zip
(...) with no frame number 1 (.rddump) inside; it holds: ...` it is.

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
  `automatic-captures.txt` naming every one: its size and `included` or why it was left out (`left
  out: gone` for one that could not be read as the report was made: the others still go in). The log
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

## The opening's frames in a release candidate

An rc build (`ICO_RC_TOOLS`, on Android alone; [BUILDING.md](../BUILDING.md#release-candidate-tools))
saves frames of the game's opening by itself, because the opening plays for about 80 s before the
title menu and cannot be paused, so the player cannot ask for a bug-report picture there. A release
has none of this. The build's `WINDOW_CAPTURE_DEFAULT` is 0 (the phone's frames are in): the capture is compiled in and does nothing, and the start prints `window: capture: off
(WINDOW_CAPTURE_DEFAULT 0: the opening's frames are not saved by themselves in this build)`; a later
round that needs new frames builds a variant with it 1, which prints the lines below.

- **When.** In the opening alone: `stage_no` 1 with the disc's scene layout in front
  (`ico_region_cur()->layout_scene`: the log's `menu screen 7 -> 55` on the European disc, 51 or 52
  on the others). Each replay's record (`rd_perf_pop`, read on the window's thread in `perf_drain`)
  carries its glow light plan (`RdPerfRecord.glowLight`, the 10 s line's `window: glow: light (every
  scale): N dark, L lamps, K whole`). Three moments, each once a run: **the lamps picture**, the
  first LAMPS picture after at least 60 LAMPS pictures in a row; **the sun picture**, the first WHOLE
  picture after at least 20 in a row (the sun's fans); **the later picture**, the first DARK picture
  after at least 60 DARK pictures in a row counted after the lamps picture was taken. A picture with
  no plan of the same frame as the one before (a later present of that tick) is passed over; of a
  new frame, or a picture outside the opening, it ends the count. The rc logs show about 38 replays
  a second in the opening, so 60 in a row is about 1.6 s.
- **How.** A moment arms the dump; the window's next pump takes it (`window_capture_due` at the top
  of `report_picture_pump`), so the picture that met the moment is drawn as usual and the dump is of
  the last closed frame at that pump. It waits while an earlier picture's presented PNG is still
  owed, and is dropped (to be armed again) if the opening ended first. Dumps are at least 2 s apart,
  counted from the end of the one before; at most 3 a run; a dump that fails is logged and not tried
  again.
- **What is written.** The Settings bug-report picture's dump (`frame_dump(2)`): `frame-<stamp>-
  v<vsync>.zip` in the dumps folder with the frame's `.rddump`, the tick before's `-prev.rddump`,
  the log, the next present's `-blended.rddump` when it is drawn between the ticks, and the
  presented picture (`-present.png`, at most 1920 px wide), added at the next presents as for the
  Settings picture. It does not carry the run's slow-scene or skin-check zips (the Settings
  picture's bundle; Share a bug report sends the newest slow-scene zip itself). The whole-frame PNG
  `frame-<stamp>-v<vsync>.png` and the `-present.png` stay loose in the folder, as for every
  Settings picture. **Share a bug report** sends every zip of the run (up to four).
- **The lines.** Once at the start: `window: capture: on (a release candidate): frames of the opening
  are saved by themselves (a lamps picture, a sun picture, a later picture) into the dumps folder
  and sent with "Share a bug report"`. For each dump, after the Settings picture's own `window: F12
  at vsync ...` and `window: bug report: ...` lines: `window: capture: the lamps picture (dump 1 of 3
  this run, after 60 lamps pictures in a row): frame 1234, frame-20261112-101010-v900.zip` (`the
  sun picture ... after 20 sun pictures (the glow drawn whole) in a row`, `the later picture ...
  after 60 dark pictures in a row after the lamps picture`; the count is the run when the moment was
  met). A dump whose zip is not in the dumps folder: `... frame N not saved (no zip; the lines above
  say why); the capture goes on`. No line names a moment that never came (a run with no WHOLE
  stretch has no sun picture); the 10 s glow line says which plans the run had.
- **Size.** The one phone dump on the build machine (a pause-menu picture,
  `frame-20261111-194104-v9031.zip`) is 5,808,167 bytes: three frames of 15,366,089 bytes each
  (the frame, the tick before, the blended present), a 646,832-byte log and the 5,052,850-byte
  presented picture, 51.8 MB unpacked. The opening's frames are not measured; the same order (about
  6 MB a zip, about 18 MB for three, plus the loose PNGs) is expected. Nothing prunes the dumps
  folder, so each rc run adds its three.
- **Cost.** The dump is the Settings picture's, on the game thread. In that phone's run the dump was
  followed by `the game step was late by 738 ms ... the sound was held back for 648 ms`, and the
  presented picture and the blended frame going into the zip a few presents later by `late by 722
  ms ... 848 ms`: about 1.5 s of hitches a dump, the sound held back with them. The frame saved is
  the one before the hitch, and the next moment cannot be met within 2 s of the dump's end, so no
  saved frame follows a hitch directly.

## Issue 123's present stalls: what the mechanisms predict

The reporter's run (Adreno 740, Qualcomm's own driver, Frame rate Original, a 120 Hz display that
never changed) has seven presents of 336-344 ms inside `vkQueuePresentKHR`, 5-69 s apart; the
run before (Frame rate Uncapped, a 37 ms GPU span) one of 404 ms in 150 s. What the two logs
settle:

- **The GPU completed nothing for the whole stall.** After every stall the next replay's wait for
  GPU completion (`fence` in the after line) is the stall's own length (341.4, 344.0, 340.9, 341.6,
  338.8, 337.1, 337.9 ms; 405.5 in the earlier run), while the present call held the submit
  thread: two threads blocked on the GPU for the same time. The swapchain lock and the surface poll
  were 0.0 ms and the display's image was free in every picture. So it is not the present queue
  alone (a display that holds buffers would leave the GPU completing work) and not a wait of the
  port's own around the present.
- **The GPU's timestamp counter restarted between two replays.** In six of the seven stalls the
  stalled replay's first stamp is earlier than the replay before's first (the restart words); its
  whole span is coherent, so the restart came before the stalled replay's first command, after
  the replay before's last. Its commands ran (all its stamps were written): none were dropped.
- **The clock was low after it.** Five stalled replays ran 85-91 ms against 27.5 (every pass
  about 3x), one ran 72 ms, and replay 1572 ran 30.9 ms (normal): the 3x is not universal; the
  replays after ran 30-71 ms, then normal.
- **The stall's length is nearly constant** (336-344 ms in one run whatever the content), which
  points at a timeout or a fixed recovery cost in the driver more than at work.
- **No device loss and no error**, and stalls never came 3 in 3 s.

Candidate mechanisms, against those facts:

1. **Slumber (the kernel driver's idle power-down).** The driver re-arms an idle timer each time
   the GPU goes idle and powers the GPU down when it expires; the default is 80 ms
   (`QCOM_KGSL_IDLE_TIMEOUT`). A wake starts the clocks low and the counter restarts (the counter
   is in the GX power domain, below). It needs the GPU idle 80 ms. At 30 pictures a second the
   replays were 33 ms apart in every stalled block but the first (replay 785 came after the 818 ms
   step of the skin check's dump), so slumber explains at most the first stall, and a wake is
   milliseconds, not 340. The new `the replay before began N ms earlier` figure settles it per
   stall: under 80 ms the GPU cannot have idled 80 ms.
2. **Inter-frame power collapse (IFPC).** The GMU collapses the GX domain between frames after a
   hysteresis of tens to hundreds of microseconds, well inside the 6 ms the GPU idles before almost
   every replay, so by those values it would happen on ordinary frames too, without a stall or
   a counter restart; a stall is not simply an IFPC. A failed or
   slow IFPC exit that the driver recovers from would look like mechanism 3.
3. **A GPU fault or hang and the driver's reset and replay.** Qualcomm's hardware-scheduler path
   restarts the GMU and the GPU and resubmits the in-flight submissions; a context that is not
   invalidated sees no error; the older dispatcher invalidated a context only after 3 recoveries
   in 3 s. This fits every fact: the GPU completes nothing for a fixed time (detection, snapshot,
   restart), the counter restarts (the GX domain is power-cycled), the replayed work runs at the
   restart's low clock, nothing is lost, no error. It is the best fit and it is not proven: the
   reset count is in the kernel's sysfs, which the app cannot read on that phone (EACCES), and the
   device offers no fault report (`VK_EXT_device_fault` not offered, the log's loss diagnostics
   line). What faulted is not known; the skin check's bone block that the GPU copied differently
   from the CPU's bytes in the first stall's replay is the one sign of a fault in the port's own
   work, and it was seen once.
4. **The present path (the window's buffer queue).** By default a Vulkan producer on Android is
   throttled at the present until the consumer has done with the buffer before; that makes a
   present wait for earlier work, which is what the 340 ms is, but it does not restart the GPU's
   counter or hold the GPU's other completions. Killed as the cause; it is how the stall reaches
   the present.
5. **Something the port submits around the present.** Killed by the split (lock 0.0, poll 0.0,
   call 340) and by the render thread's fence wait of the same length.

What the next log will say on its own (`present stalls:` per block, `present stalls this run:` at
exit, [DISPLAY.md](DISPLAY.md#the-refresh-rate-and-the-frame-rate-pin)): per stall the CPU-side
interval since the replay before (mechanism 1 needs 80 ms or more), the time the present held
past the replay's GPU span (mechanism 3: about the stall's length; mechanism 1: a few ms) and
whether the counter restarted, the display's rate, and at exit the counts of each kind and the
stalls a minute by display rate and by the block's GPU span band. Not done: a check that the
picture after a stall is the right picture (the skin check is the only GPU-side content check)
and the kernel's reset count (unreadable).

Sources: the kernel driver's idle timeout default (`QCOM_KGSL_IDLE_TIMEOUT` 80; `idle_timer`
re-armed with `jiffies + interval_timeout` when the GPU goes idle): Qualcomm's graphics-kernel
`kgsl_pwrctrl` in vendor trees, e.g.
https://git.evolution-x.org/Evolution-X-Devices/kernel_oneplus_8750-modules/src/branch/bka/qcom/opensource/graphics-kernel/kgsl_pwrctrl.h;
the counter's power domain: "CP_ALWAYS_ON counter falls under GX domain which is collapsed during
IFPC" (drm/msm "Switch to GMU AO counter",
https://lkml.indiana.edu/hypermail/linux/kernel/2507.2/09476.html); the IFPC hysteresis (about
78 us on a6xx, `GMU_PWR_COL_HYST`; about 300 us for the long hysteresis on a6xx-a8xx, drm/msm "Add
support for IFPC", https://lkml.indiana.edu/hypermail/linux/kernel/2507.2/09496.html); the reset
and replay ("Restart the gmu and gpu" and replay in-flight commands, `a6xx_hwsched_reset_replay` /
`gen7_hwsched_reset_replay`, Qualcomm graphics-kernel `adreno_gen7_hwsched.h`; resubmission after
hwsched recovery,
https://gitlab.e.foundation/e/devices/android_kernel_fairphone_fp5/-/commit/44ed382d7f42d2b8da5ef5850a12e80a5fff5e5a);
the fault throttle (3 recoveries in 3000 ms, the older `adreno_dispatch.c`,
https://android.googlesource.com/kernel/msm/+/4b39d42e9b31b3cded28ba71d1d979a8ea70de37/drivers/gpu/msm/adreno_dispatch.c);
the producer throttling ("vkQueuePresentKHR() calls will stall the CPU until the GPU is done
processing the previous frame", `ANativeWindow_setProducerThrottlingEnabled`,
https://developer.android.com/ndk/reference/group/a-native-window). These are vendor and
upstream kernel sources, not the Xiaomi kernel the phone runs; its values may differ.

## Issue 123: what the GPU did during a stall

These lines ask the one question the earlier ones could not answer: during the ~340 ms the GPU
finished nothing, did it not begin the stalled replay, or did it run part of it, stop, and run it
again? The second is the driver's reset and replay of the work in flight that the section above
ranks first; the first puts the cause outside the replay. Each answer is a line of its own.

What the two reporter logs settle (build-host/tmp/v0113/evidence/123/ico-pc-20261110.log, the latest;
build-host/tmp/v0112/evidence/123/ico-pc-v0111.log, the run before):

- **Nothing in the stalled replay's own work differs from its neighbours'.** Every one of the eight
  stalls (785, 1572, 2021, 3097, 3242, 5289, 5639; the run before's 2994) is in stage 12, and each stalled
  replay's line reads like the 300 around it: 2 textures and 6 meshes (57 KB) uploaded (12 meshes,
  143 KB in the run before), 0 pipelines created, 0 allocations, 0 buffers and 0 textures created, 3 DATE
  snapshots, the upload 1.3-1.5 ms, the walk 3.6-4.8 ms, the submit 0.2-0.3 ms. They come 145-2047
  replays apart with no period. Only 785 follows an event of the port's own (the skin check's
  capture and its 818 ms step). So no rare piece of the port's work (a creation, a cache write, a
  dump) lines up with them.
- **The present held 253-313 ms past the replay's own GPU span** (340 - 87.1, 344 - 30.9, ...; 404 -
  93.0 = 311 in the run before), and the replay after began 0.7-1.7 ms after it ended: for a quarter of a
  second the GPU ran none of the replay (or ran it and threw the result away).
- **The slow replays are slow by pass kind, not by a constant**: shadow count 5.1x, glow WORK2 5.6x,
  list 0 2.8x, the display wait 1.3x (replay 2021). A timestamp clock running at another rate would
  scale every pass alike, so the stamps are right; the bandwidth-bound passes slowing most fits a
  GPU and memory restarted at low clocks.

The port's own timestamps, checked against the specification
(https://docs.vulkan.org/spec/latest/chapters/queries.html):

- One pool per frame slot, reset at the start of the slot's first list on every use, after the
  slot's last submission was waited for (`vk_cmd.c` `rhi_begin_commands`, `vkr_recycle_frame`); the
  reset "defines an execution dependency between other query commands that reference the same
  query", in submission order (Query Operation), so the stamps of the slot's later lists and
  submissions follow it; each index written once a use (`rhi_ts_has`), as "all queries used by the
  command must be unavailable" asks; read once the slot's last submission finished, with the
  availability bit and no wait bit ("may return VK_NOT_READY if there are queries in the
  unavailable state"). No read races a reset in flight and none blocks; the occlusion pool is used
  the same way (indices past the range reset are dropped, `rhi_cmd_begin_occlusion`).
- The restart is not the port's: a pool reset clears availability, never the device's counter;
  the stalled replay's stamps are ordered among themselves; and each restart's figure is the time
  since the stall before (20.7 s since the counter's earlier start, then 26.2 s from 785 to 1572,
  14.9 s from 1572 to 2021): the counter starts again from about zero at each stall.
- Without `VK_KHR/EXT_calibrated_timestamps` the specification orders only timestamp writes "in the
  same submission"; with either enabled "this extends to timestamp writes across all submissions
  on the same logical device" (Timestamp Queries). The renderer compares the scene's list with the
  present's and one replay with the next, so a restart between submissions was legal driver
  behaviour all along. The backend enables the extension where offered and places the stamps
  on the CPU's clock.

On lavapipe, under the validation layer with synchronisation validation (the replay tool, serial,
on the two Adreno dumps of /primary/dev/ico-dumps/163 at 3x and 3048x2032, the Adreno branch faked,
robust access on, the plain VU set, the pre-pass off, the submit thread on, 4 replays each, the
skin check and every pass timed; once in lavapipe's own shape and once with the tile-based
profile's paths forced: aura box, pass area, the three pass cuts, the shadow volumes in the scene's
pass, glow fold off, UINT views off, fog copy, DATE snapshot): 0 errors and 0 warnings, and with
the slot clock and the range checks in the RHI the same pictures byte for byte and no command past its resource. Not covered there:
the profile class itself (chosen by the vendor id), the VkRenderPass path (the phone renders
dynamically) and pre-rotation (the phone's swapchain reports the identity transform, 0x1).

The new lines:

- **`rd: GPU clock: replay R: ...`** (`port/render/rd_perf_clock.c`, from the slot's clock,
  [RHI.md](RHI.md#vulkan)) when the CPU's wait for the replay blocked 100 ms or more, its first list
  ran twice, a stamp came back unwritten (a stalled replay: the first 32, then every 10th, ending
  `(stall line K of the run)`), or the counter restarted alone (the first 8, then every 100th, on
  its own count, ending `(restart line K of the run)`; start-up's idle restarts never use up
  the stalls' lines). A stamp that came back unwritten is named with the last one written before it
  (`(the first: list 0's end; the last written: the start of piece 5 of 35, list 0 SCENE 1728x1344
  (0 draws before it, 236 in it))`); a piece's name carries its target, size and the draws of the
  pieces before it. With it, on a stall, the hung list's pipelines (see
  [The stall and logging lines](#the-stall-and-logging-lines-issues-123-and-179)):

      rd: GPU clock: replay 2021: its first stamp 254.6 ms after its first submit (the calibrated
      clocks, within 2.5 us), the counter at 5.0 ms then (it went back 14936.7 ms since the replay
      before: a restart); 2 submissions; waited 300.0 ms for it from 38.0 ms after its last
      submit: at +50.0 ms 31 of 47 written (the last: the start of piece 30 of 46, glow SCENE
      1728x1344), none of its submissions finished; nothing changed for 230.0 ms from +50.0 ms
      with 31 written; its written stamps fell from 31 at +280.0 ms: its queries were reset again,
      its first list ran a second time (a reset and replay of the work in flight); points 31@+50.0
      0@+280.0 4@+284.0 ...; all 47 recorded stamps written

  How to read it:

  | The line says | Then |
  | --- | --- |
  | the first point "the previous use's", nothing changed for ~250 ms, no fall, the start ~250 ms after the submit | the GPU had not begun the replay: it was held by something before it, the GPU's own power path or another context's work (the compositor, another app); the replay's commands are not the cause |
  | the replay's own k stamps, nothing changed with k, the stamps fell and were written again | the replay's first list ran twice: a reset and replay. The piece after "the last" was in flight when the GPU stopped: the candidate for the fault |
  | stamps that "came back unwritten after its work was signalled finished" | the GPU skipped part of the replay (a driver skips the work that faulted): the first one named is where |
  | "its first stamp N ms after its first submit" with N a few ms | the GPU began at once: the stall is after the work (the present's queue) |

  The start comes "(the calibrated clocks)" where the device offers them (`rhi_vk: timestamps: ...;
  calibrated with the CPU's clock: on`) and the pair was read: at the first recycle, at a recycle
  whose wait blocked past 50 ms and at the few after it (not every replay), else "about ... (from
  its wait's end)", taken from when the CPU's wait returned, good to the wait's wake-up. "M of T
  recorded stamps came back unwritten" counts only the stamps of command lists that were submitted;
  a list recorded and never submitted is said apart (`N of the slot's lists with stamps were
  recorded and not submitted (left out)`). On a driver whose query read waits for the GPU
  (lavapipe does) the trace has one point, after the work.
- **The stall's entry** in `window: present stalls:` adds `the GPU began it N ms after its submit`
  and `its first list ran twice (a reset and replay)`, and the run's line adds `the GPU began the
  replay A-B ms after its submit (K placed), M whose first list ran twice`.
- **`rhi_vk: a command past its resource: ...`** (`port/rhi/vk/vk_range.c`): an indexed draw whose
  indices, or a copy whose buffer range or texture region, reaches past what it names. Robust
  access covers the shaders' reads and the vertex input ("access to vertex input data is bounds
  checked against the bound vertex buffer range", Robust Buffer Access,
  https://docs.vulkan.org/spec/latest/chapters/shaders.html), not the index fetch or a transfer,
  which a GPU may fault on. Any such line in a reporter's log is a port bug and the fault source
  to fix first; none was seen on lavapipe.

What remains unproven: which of the mechanisms above is the stall. Nothing in the port's code or
its lavapipe replays is a fault source; the code the audit did not read pass by pass is covered on
the device by the range checks, and descriptors cannot outlive their views (bind groups come from
the slot's own pools, reset after its fence; destroyed objects wait for their slot's next fence).
The next log's `GPU clock` line on a stalled replay decides between "not begun" and "run twice",
and in the second case names the piece in flight.

## The performance hints

The window tells Android each picture's work against its time budget through a performance hint
session ([PLATFORM.md](PLATFORM.md#performance-hints-android) has the mechanism). Three lines are
read together, from the same 10 s block:

- `window: performance hints: N reports (actual CPU a ms / GPU g ms of the D ms target; over target
  in K), the report calls c ms in all (longest l ms), target changed J times`: N should be about
  the block's presented pictures (the `window: N replays` line's count, less the replays that
  presented nothing); `a` is the render and submit threads' work a picture without their waits,
  `g` the GPU's (on API 35; the last timed picture's for the pictures between: the `GPU this
  picture's own in X of N` part says how many were their own), D the pacer's planned time a picture.
  K close to N says the session asked for more speed all block (expected while GPU-bound at
  uncapped: 19 ms against 7.81 ms); K at 0 lets the system lower the clocks. `c` and `l` are what
  the calls cost the hint thread (never the game or render thread); `failed (error 32)` is EPIPE
  (the system service gone), `error 22` EINVAL. J counts the frame-rate or refresh changes passed
  on.
- `window: kernel GPU driver: N samples; CPU clocks: cores 0-1 ..., cores 2-6 ..., core 7 ...`: the
  measure. With hints the cores the session's threads run on should stay higher for the same scene
  (on the Pixel 10 Pro Fold one run had core 7 at 2342 MHz throughout and the others 530-880 MHz;
  see below for why).
- `window: N replays: CPU ...: interp, upload, walk, ...`: the phases the clocks act on (the walk
  was 1.3 ms in the run with core 7 and 4.1 ms in the next in the same scene); the picture time
  follows the GPU line where the GPU is the limit.

Where the render thread runs matters for the clock line. On the Pixel 10 Pro Fold the render
thread asks for core 7 alone, and in six of the owner's seven runs on it
the kernel said yes and kept it to cores 2-7 (`window: render thread: ... could not be kept to the
fastest core 7 alone: the system said yes and keeps it to cores 2-7`; `window: threads: ...; kept
to cores 2-7: ico render`); one run kept it on core 7 (`kept to core 7: ico render`). The placement code did not
change between those builds. So core 7's clock is the render thread's clock only in a run whose
threads line says `kept to core 7`; in the others the render thread's clock is that of cores 2-6,
which ran at 650-850 MHz on average in those runs' blocks. Read
the threads line first, then the cores the session's threads are on. The keeper asks
for core 7 again after the start ([below](#the-threads-cores-after-the-start)): its block line
says where the threads were.

The start line says which calls the phone has (`API N`, `GPU durations reported yes|no`), the
threads' ids (the same ids the `window: threads:` line counts), the target and its basis, the game
mode, and the system's update rate; `off (...)` names why there is no session (an Android 12 or
older phone, a phone without hint support), and then the port runs exactly as before.

## The threads' cores after the start

The start pins the render thread to the fastest core alone (`window: render thread: the high
priority granted ...; kept to the fastest core 7 alone ...` or `could not be kept ...: the system
said yes and keeps it to cores 2-7`, [PLATFORM.md](PLATFORM.md#the-render-thread)). On the owner's
Pixel 10 Pro Fold (`0-1 2.2 GHz, 2-6 3.1 GHz, 7 3.8 GHz`) the kernel read back 2-7 in 20 of 25
starts; in the four starts that kept core 7 the old bridge's reads were
6.0-6.4 ms and it ran 67-71 fps with no back-off, against 8.3-8.6 ms and 48-59 fps otherwise. In
one of those (core 7 kept) the cage's slow steps were the game thread's own, 9-14 ms with the camera
moving, on whichever of cores 2-6 it landed, shared with the submit thread.

So the keeper (`port/platform/cpu_keep.h`, driven by `render_thread.c` from the log thread's
once-a-second tick) keeps three threads apart after the start, by the cores' top speeds only:

| on the Pixel | render thread | game thread (`ico-main`) | submit thread | every other thread of the game's |
|---|---|---|---|---|
| core 7 kept (the ideal) | 7 alone | 6 alone | 2-5 | 0-5 |
| core 7 refused alone | 6 alone | 5 alone | 2-4, 7 | 0-4, 7 |
| 7 not allowed (cpuset 0-6) | 6 alone | 5 alone | 2-4 | 0-4 |
| one-core pins widened (the pair) | 6-7 | 5 alone, or 0-5 when every one-core pin is widened | 2-4 | 0-4 |
| a pair widened too (the start's rule) | 2-7 | 0-7 | 2-6 | 0-7 |

The rule above needs three or more cores faster than the slowest (`CPU_KEEP_MIN_FAST`): the
Pixel's six, a 4 + 3 + 1 phone's four (7, 6, 4-5), 4 bigs over 4 littles. With one or two
(`cpu_keep_few`; a Helio G85, `0-5 1.8 GHz, 6-7 2.0 GHz`, issue 179's moto g24 power) the keeper
runs the few-cores rule instead. Before it the start's placement stood alone (game and
render threads both on 6-7, nothing kept after), and every thread the game thread made afterwards
inherited 6-7: issue 179's log's threads line read `kept to cores 6-7: ico-main (the game
thread), ico render, AudioTrack, SDLAudioP15`, the game and render threads each waited 0.2-1.0 s a
block for a core (the submit thread, free, 0.0-0.2 s), and the six slow cores held everything
else, the driver's threads too. The picture there is GPU-bound (97-100 % busy, the render
thread's CPU 16-21 ms of a 110 ms picture), so the two fast cores go to the two threads that
feed the GPU, one each, and everything else goes to the slow cores:

| 2 + 6 (Helio G85) | render thread | game thread | submit thread | every other thread of the process |
|---|---|---|---|---|
| the ideal | 7 alone | 6 alone | 0-5 | 0-5 |
| 7 refused alone | 6 alone | 6-7 | 0-5 | 0-5 |
| the game thread's 6 refused | 7 alone | 6-7 | 0-5 | 0-5 |
| one-core pins widened (the pair) | 6-7 | 6-7 | 0-5 | 0-5 |
| a pair widened too (the start's rule) | 6-7 | 6-7 | 0-7 | 0-7 |

With one faster core (1 + 7) the render thread is alone on it, the game thread on every core as
before, the rest on 0-6. "Every other thread of the process" is the audio threads (SDL's device
thread, which runs the mixing and the sound chip, and AAudio's `AudioTrack` thread), the log
thread, the workers, the driver's threads (`mali-cmar-backend` and its kin) and any thread made
later: the system's and the driver's threads are made where nothing of the game's puts them
back, so once a second the log thread moves every thread it finds on the render or game thread's
fast cores to the others' (`cpu_keep_held`, `cpu_place_sweep`: the process's thread ids from
`/proc/self/task`, each one's cores by `sched_getaffinity`). Android's UI thread (the process's
own id) is left to the system, as everywhere. The alternative, no pin at all, has no log on this
class; the nearest one (render and submit threads on the two, the game thread free) had the game's
step on the slow cores 15-62 % of the samples, and it would leave the audio threads free to take
the fast cores. The start says:

```
window: cores after the start: kept (2 cores faster than the slowest): the render thread on 7 alone, the game thread on 6 alone, the submit thread and every other thread of the process on 0-5 (the audio threads, the log thread, the workers, the driver's threads and those made later: the log thread moves a thread found on 6-7 there once a second; Android's UI thread is left to the system); where the system widens a one-core pin, the render and the game thread share 6-7
```

The keeper's try and block lines are those below; under the few-cores rule the block line ends
`, N moved to 0-5 by the few-cores rule once a second since the start (M refused)`, and the
threads line should read `kept to core 6: ico-main (the game thread); kept to core 7: ico render;
kept to cores 0-5: ...` once the first second has passed. The game thread line's run-queue part
names the two audio threads too (every phone, found by name in the thread list every 10 s):
`ran / waited for a core: game 2.6 s / 0.7 s, render 1.2 s / 0.3 s, submit 0.2 s / 0.0 s, SDL
audio 0.4 s / 0.1 s, system audio 0.1 s / 0.0 s`. Under the few-cores rule the game and render
threads' waits are the ones to read: near 0 means the rule holds; still 0.2-1.0 s means threads
outside the process (the system's) share the two cores. The 10 s block's keeper text on a phone
with every core of one speed reads `off (no core faster than the slowest)`. The game thread is SDL's `ico-main` thread, not Android's UI
thread: the touches, keys and the activity's lifecycle arrive on the UI thread, which is never
moved, and are queued for the game thread to take. Threads the game or render thread makes start
on their creator's cores (pthread_create(3)); the workers put themselves back on the others' cores
at their entry and are moved when those change, and a thread found on the render or game thread's
core alone (a driver's thread: `kept to core 7: ico render (2)` in the threads line) is moved off it
at the next thread list (every 10 s).

The keeper reads, once a second: the process's cores (`sched_getaffinity` of the process id), the
render thread's cpuset (`/proc/self/task/<tid>/cpuset`, its cores from `/dev/cpuset<set>/cpus`),
its utilisation clamps (`sched_getattr`), and the render and game threads' cores. It asks again
(the render thread's ideal core first, never a core the process or the cpuset forbids) at the
start, when a 3D scene begins, on focus gained, back from the background, when the cpuset or the
allowed cores change, when the system changed a thread's cores, when the submit thread starts,
and every 10 s while the render thread is not on the ideal core alone. Each pin is judged by the
cores read back after it.

When a try's outcome differs from the last one written (the first 32 such lines, then every
100th):

```
window: render thread cores: asked 7, read back 2-7, cpuset /top-app (cores 0-7), clamps 0-1024, the process may run on 0-7; re-pin refused (the system said yes and keeps it to cores 2-7); on 6 alone instead (kept); the game thread on 5 alone (kept), the submit thread on 2-4, 7 (kept), every other thread on 0-4, 7; why: the start
```

Every 10 s block:

```
window: render thread cores: in 10 s: now on 7 alone (the ideal), the game thread on 6 alone, the submit thread on 2-5, every other thread on 0-5; read back as set at 10 of 10 reads; tries in the block 1 (the ideal kept in 1), since the start 3 (kept in 1); changed by the system 0 times in the block, 1 since the start (the last: the render thread's from 7 to 2-7); cpuset /top-app (cores 0-7), clamps 0-1024; threads moved off the kept cores 9 since the start (0 had ended); the game thread's step 4.6 ms (CPU 4.1 ms of it) (the log thread's, up to 1 s old)
```

and the game thread line's cores part gains each thread's samples by core (one a step, a picture,
a submit job, from the thread's own `getcpu`): `the samples by core: G 6:100 %, R 7:100 %, S 2:24
3:26 4:25 5:25 %`.

How to read them:
- `re-pin kept` at the start and `now on 7 alone (the ideal)` in every block: the pin holds; the
  step rule's reads should be those of the starts that kept core 7 (6-7 ms on the bridge).
- `re-pin refused (the system said yes and keeps it to cores 2-7)` with `why: the start` and later
  `re-pin kept` with `why: a 3D scene began`, `focus gained` or `back from the background`: the
  widening depends on the moment (the cpuset's or the window's state); the `cpuset` part of both
  lines says whether the set changed between them (`/foreground` to `/top-app`).
- Refused at every try with the same cpuset and `the 10 s retry` counting up: the kernel widens a
  one-core pin to core 7 whatever the moment (a vendor rule); the render thread is then on 6
  alone, which the next `samples by core` part shows as `R 6:100 %`.
- `changed by the system N times`, with `the last: the render thread's from 7 to 2-7`: the pin held
  and the system widened it later. Read the start lines and the `window: performance hints:` line
  around it (the session's `setThreads`, a cpuset move) for what happened then.
- `clamps` other than `0-1024`: the system (the hint session, a game mode) set a utilisation
  clamp on the render thread.
- `cpuset not readable (errno 13)`: SELinux keeps the cpuset file from the app; the keeper still
  works from the process's cores.
- The game thread's step beside its cores: a step that stays long (9-14 ms in the cage) on its
  own core is the game's work at that core's clock, not a shared core.

## The step rule's back-off: outlier misses and its end

When the rate swings between two levels for seconds at a time on an unchanged scene (the owner's
Pixel 10 Pro Fold on the old bridge: 59 and 48 presented fps in turn), read the step rule's lines.
The rule ([DISPLAY.md](DISPLAY.md#further-presents-and-the-game-step)) holds a picture between
frames only when it would make a game step miss its period. A miss the plan made widens its margin,
and the third within 300 steps plans the whole period for at most 120 steps (the back-off).

- `window: step rule: the margin widened by L ms to W ms after a miss the pictures caused (...; K
  counted toward the back-off in the last 300 steps): ...; whether it counts is judged in 2 steps,
  by the GPU's times of the pictures around it; ...`: a miss of 2 ms or more.
- Two steps later one of these two follows:
  - `window: step rule: an outlier miss, not counted toward the back-off: the miss 2 steps ago
    (late by L ms) came with a picture the GPU took X ms against a typical Y ms (over 7/4 of the
    median of the 8 before it, ...)`: the GPU, not the plan. X far over Y on an unchanged scene is
    the hardware's slow picture (the old bridge on that phone: 26-39 ms against 13.5).
  - `window: step rule: the miss 2 steps ago (late by L ms) counted toward the back-off (the GPU's
    longest picture around it X ms against a typical Y ms: no outlier); K counted in the last 300
    steps (the third begins it)`: the plan's own doing. Without GPU timestamps the part reads `the
    GPU's times around it not known`, and every miss counts, as before this judging.
- `window: step rule: back-off of at most 120 steps after the third miss the pictures caused within
  300 steps, each counted (...): 2 steps ago late by L ms (the GPU's longest picture around it X ms
  against a typical Y ms), N steps ago ..., M steps ago ...: ...`: the three misses that began it.
- Its end is one of two lines:
  - `window: step rule: back-off ended after N steps: the pictures between frames fitted in 15
    consecutive frames (half a second of 33.4 ms frames); ...`: while backed off, the normal plan
    would have handed over each frame's first picture between frames for half a second. N near 30
    (the run itself) says the miss was a passing one.
  - `window: step rule: back-off ended at its limit of 120 steps (the pictures between frames
    fitted in the last K consecutive frames of the 15 needed); ...`: the pictures do not fit two a
    frame at this picture time. That is the scene or the GPU's speed, and the rate follows it.
- In the game thread's 10 s line, the clause `steps that missed their period: N (P the pictures': S
  under 2 ms, W widened the margin (B counted toward the back-off, C outliers at X ms against a
  typical Y ms), F the third in 300 steps; ...; Y steps planned at the whole period after the
  pictures' (back-offs ended: E by the pictures between frames fitting, M at the limit of 120
  steps))`. Y is the block's steps under the back-off (with the doubled back-off before this, up to
  480 in a row: 590 of a block's 600 on the old bridge). C with B near 0 says the misses were the
  GPU's slow pictures. B steady with M at its count says the pictures do not fit two a frame there.

Each line is rate-limited (the first 8, then every 100th). The counts in the 10 s clause are
complete.

## The evenness line on a 120 Hz phone

The owner's report on the Pixel 10 Pro Fold (120 Hz, uncapped, mailbox, 45 to 73 pictures a
second): motion "not always as smooth as the number suggests". The 10 s `window: evenness in 10
s: ...` line ([PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#the-evenness-line)) separates the
two causes the logs before it could not:

- **The glass grid.** At 67 pictures a second on 120 Hz the GPU's ends fall 1 or 2 refreshes apart
  in a mix. When the `1 refresh` and `2 refreshes` classes carry about the same mean content step,
  the 1-refresh pictures move about twice as far a refresh as the 2-refresh ones: the speed on the
  glass jumps picture to picture although the clock steps evenly. Many 1-refresh gaps with equal
  steps while the rate is high is this cause; a frame rate that divides 120 (60) puts nearly every
  gap at 2 refreshes.
- **The clock.** `back K times` above 0, `resets ... K back`, a wide `deviation` of every step, or
  many clamps at 0 with large amounts are the present clock moving the content unevenly by itself.
  The clock never moves the content back within a frame pair ([DISPLAY.md](DISPLAY.md) "The
  alpha"; the clock's `held` count says how often it would have), so a step back is one across
  frames.
- **The latency.** The `alpha's sample to the GPU's end` spread (10th to 90th) is how much the time
  from a picture's content to its GPU end varies; the drain before the hand-over is its first part
  (about 1.6 ms before a tick's first picture and 7 ms before a further one on the owner's phone,
  the game thread's `slot waits` split).

Read it beside the presented rate and the step rule's lines: a block whose rate swings with the
back-off (the section above) is the rate's unevenness, not the glass grid's.

With pictures as made (vsync off, uncapped) the alpha is taken at the hand-over less the mean
drain, not from the present clock: the `re-based` clause's `content's moment to the hand-over`
should spread about a ms, where `at the sample` shows the clock's own error (18 to 20 ms of spread on
the owner's phone with the clock's alpha, which reset at up to a fifth of the presents and clamped a
quarter of them at 0). The `content drawn against the hand-overs` clause counts the repeats (the
picture before shown again) and the step's ratio to its real gap: repeats near 0 and ratios near 1
are content following real time.

## The refresh rate under Uncapped

The owner's Pixel 10 Pro Fold ran four release candidates under Uncapped: two at 120 Hz and two at
60 Hz (`window: display refresh: 60.00 Hz, 2364x1080 "1"` from the first line to the last), because
Uncapped asked the system for nothing. Under Uncapped the game now asks for the highest rate the
display lists at its current size ([DISPLAY.md](DISPLAY.md#the-refresh-rate-and-the-frame-rate-pin)).
How to read a log:

- `window: frame rate pin: asked 120 Hz (fixed source; the display's highest of 60, 120 at
  1080x2364; the Frame rate row is Uncapped) with ANativeWindow_setFrameRateWithChangeStrategy:
  returned 0`: the ask, the rates it chose from (the display's own list at the current mode's
  physical size, so it can read 1080x2364 where SDL's line reads 2364x1080) and the call's return.
  `(refused)` after a return other than 0 is followed by `Uncapped asks for no rate: the system
  refused the ask (returned R), so it is not made again this run`.
- `window: frame rate pin: Uncapped asks for no rate: <reason>; the system chooses the refresh rate
  as before`: today's behaviour, once per reason: an Android 11 phone (no seamless-only call), the
  list could not be read, no mode at the current size, or one rate only. `Uncapped's ask waits for
  the window's focus`: no new ask while the window has no focus; the next 10 s block asks.
- `window: display refresh changed: 60.00 Hz -> 120.00 Hz, ...` after the ask: the panel switched.
  Its rate comes from the activity's display listener (`IcoActivity` `startDisplayWatch`), not
  from SDL, whose Android side reads the rate only when the surface changes size; no Android log
  in the evidence folders from before this listener holds a change line. Under Uncapped the
  `display refresh:` 10 s lines and every pacing figure read that rate; under a number or Original
  they read SDL's, as before the listener, so a numbered row's log is read as it always was.
- An ask with no change line after it, and the 10 s line still at 60 Hz: the system did not grant
  it (an ask is a vote; battery saver, a thermal limit or a vendor's own policy can keep the panel
  lower; nothing in the log can tell which). A change line with no ask before it: the system
  changed the rate by itself.
- What to compare: the presented rate in the 10 s block against the refresh. Under Uncapped with
  vsync on, presents are one a refresh at most, so at 60 Hz the rate cannot pass 60.

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

## Issue 123: the hang detector and the pass path

What the issue 123 tablet's logs (Adreno 740, Qualcomm's driver 512.676; the newest with 12 stalls
in 308 s) and issue 211's (Adreno 710, 512.800) say together, and the attempt that follows from
them. The analysis with every row is `build-host/tmp/v0116/design/ADRENO123_v0116.md` (not in the
repository).

- **The stall's length fits the kernel driver's hardware hang detector.** Qualcomm's kernel GPU
  driver arms the hang interrupt on the Adreno 7xx with `hang_detect_cycles = 0xcfffff`, units of
  16 cycles (`gen7_start()`, adreno_gen7.c, and adreno-gpulist.h in
  android.googlesource.com/kernel/msm-modules/graphics): 218,103,792 cycles. At the 710's 1010 MHz
  (its `kernel GPU driver` line before the hang) that is 216 ms; its polled trace measured "nothing
  changed for 211.8 ms" and "208.0 ms". The 740's stalls are 336-415 ms, nearly constant within one
  log at any load; its clock is not readable. The other detectors do not fit: the dispatcher's soft
  timer looks every 200 ms, a preemption that never completes is caught at 10 s
  (`ADRENO_PREEMPT_TIMEOUT`, adreno.h). So the GPU itself stops making progress inside the pass and
  the kernel resets it, skipping the rest of that submission (an inference with a measured fit on
  the 710).
- **Where.** Inside list 0's pass, 12 of 12 in the newest log (`the first: list 0's end`, with a
  later stamp written each time), in stage 12 and in stage 1's opening alike, with a list 0 of 3
  prelit pipelines as with stage 12's full one, no pipeline rare in its block, nothing created, in two
  of them nothing uploaded before it. Every such run began that pass with `vkCmdBeginRendering`,
  with robust access on or off, the VU pre-pass on (the 710) or off (the 740). So the hang follows
  the scene's first pass of a submission, not what it draws or the load.
- **The attempt (on for this class).** On Qualcomm's own driver every render pass is a
  `VkRenderPass` object (`rhi_pass_row.h` `rhi_vk_pass_row_qualcomm`; [RHI.md](RHI.md#vulkan),
  **Render passes**): the same attachments, ops, layouts, barriers and waits, the same picture
  (`rd_pass_objects_ab`), the driver's oldest way of beginning a pass in place of dynamic rendering's.
  Another hypothesis stays untouched: a power transition at the start of the first heavy pass after
  the GPU's idle gap before each replay, which no app can change.
- **Reading the next log.** The device line says `render passes legacy (Qualcomm's own driver
  ...)` and the start line `rhi_vk: render passes: VkRenderPass objects, not dynamic rendering:
  Qualcomm's own driver (...)`. Then the stalls (`rd: GPU clock: ... (stall line N of the run)`,
  `window: present stalls:`, the attempt ledger's `stalls N in the block`) against the newest log's
  12 in 308 s: about as many with the same `list 0's end` says the pass path is not the trigger; none
  in a comparable session says it was; a different first unwritten stamp says the hang moved with
  the path. `RhiStats.passObjects` (the passes begun as `VkRenderPass` objects) and
  `RhiLimits.passObjectsRow` (the row's reason) are the attempt's counter and state for the ledger.

## The stall and logging lines (issues 123 and 179)

What the logs of issues 123 (Adreno 740) and 179 (Mali-G52) on the barrier and wait defaults could
not say, and the hitches they showed. Every line is automatic (no setting), rate-limited, and costs
counters in the hot path.

- **The clock line's two limiters** (`port/render/rd_perf_clock.c` `rd_perf_clock_due`): a stalled
  replay (a slot wait of 100 ms or more, a second run of its first list, a stamp unwritten after its
  work was signalled finished) is written the first 32 times, then every 10th; a counter restart
  alone (an idle GPU's counter going back, everything written, nothing waited for) the first 8,
  then every 100th, each on its own count. Issue 123's start-up had seven idle restarts, which used
  up the old shared "first 8" and left its second and third stalls without a line.
- **The hung list's pipelines** (`port/render/rd_perf_rare.c`, kept by `rd_perf.c`): every pipeline
  list 0 asks for (`rd__get_pipeline`, through the pass ring's note) is kept per replay with its
  lookups, the window's 10 s block counts the replays each was seen in, and a stalled replay's
  clock line is followed by:

      rd: GPU clock: replay 2417's list 0 (the stalled replay): 41 pipelines in 236 lookups; seen in
      under 5 % of the block's 349 replays: pipeline 812 (vu_skin_vs/vu_ps) 3 lookups, in 2
      replays; the most used: pipeline 12 (vu_prelit_vs/vu_ps) 60 lookups, in 349 replays, ...
      (stall line 2 of the run)

  A draw that is drawn rarely and hangs the GPU when it is shows as the rare pipeline common to
  every such line. CPU data only; vertex counts are not in it (they are the draw code's).
- **The automatic captures' game-thread time split** (`port/platform/window_dump_auto.c`): the
  slow-scene dump's and the skin check's capture line gives the wait for the render thread's work
  in flight apart from the frame's own bytes: `window: slow-scene dump: 156.1 ms on the game thread
  (the render thread's work in flight 120.4 ms of it, the frame's bytes 35.7 ms), 1017 ms on the
  worker (...)`. The frame is serialised on the game thread because the ring frame names textures,
  meshes and targets the game thread changes from its next call on (`rd_dump.c` `dumpFrame` drains
  the render and submit threads first); the zip and the file writes were already the worker's. On a
  GPU-bound phone the drain could be the render thread's own slot wait; issue 179's next log
  measured it at 0.0 ms and the frame's bytes at 47.1-56.0 ms (8.6-10.3 MB), more than one 40 ms
  tick.
- **The slow-scene dump in two steps** (`port/render/rd_dump_mem.h`, `dump_auto.h`
  `ico_dump_auto_prepare`): the copy itself cannot leave the game thread (the frame's textures are
  written in place by its next calls, its meshes and targets freed, its ring slot reused), but the
  block it went into was grown on the game thread by doubling from 1 MiB, every page of every
  block touched there for the first time. Now the game thread first measures the frame
  (`rd_dump_retained_frame_bytes`: the same walk, nothing copied), the worker allocates a block of
  that size plus an eighth and 1 MiB and touches its pages, and at a later vsync, once the worker
  says it is made, the game thread copies the frame into it in one pass
  (`rd_dump_retained_frame_into`: exactly the dump's bytes, no allocation of the block, no file,
  no compression) and hands the job back for the zip. The frame is the one picked while the ring
  still holds it, else the next replay at or above the block's mean (the pick's own rule); the
  bytes are those of the old copy for the same frame. The line keeps its words: the game-thread
  figure is now both steps (the measure and the copy, each with its drain), the worker figure the
  block's making and the zip. Lines of their own: `window: slow-scene dump: frame F had grown past
  the worker's block of N bytes: copied into a block grown here instead` (the old way, once),
  `window: slow-scene dump: not written: the worker could not make a block of N bytes`, and
  `window: slow-scene dump: not written: no frame of the scene could be copied in 10000 ms (the
  worker's block of N bytes given up)`. The skin check's capture is unchanged.
- **The logging's own cost** (`port/platform/log_cost.c`, `window_lines.c`, the log thread's
  counts in `port/platform/android/host_android.c`), once a 10 s block:

      window: logging: 8.0 KB written to the log in the block (every thread's); the window thread's
      10 s lines 1.25 ms (formatting and writing; 0 during a present that blocked, 0 during a late
      game step); the log thread's file writes 42 in 14.5 ms, the longest 9.0 ms (from "rd: GPU
      clock"), 2 of 8 ms or more (1 during a present that blocked, 1 during a late game step);
      logcat 1.5 ms; the pipe at most 2 of 64 KB waiting (never full: no writer waited); captures 1:
      30.8 ms on the game thread (the longest 30.8 ms), 550 ms on the worker (0 during a present
      that blocked, 1 during a late game step); 1 presents blocked, 1 late game steps in the block

  The window thread's time is split into its parts, `(formatting and writing; of it the block's
  records D, the kernel GPU and thermal reads K, the memory reads M, the performance lines P, the
  other lines R; ...)`: the block's last record drain (with `[dev] perf_log`/`perf_passes` the
  CSVs are flushed there), the kernel GPU sampler's block and the thermal state (a JNI ask of
  `PowerManager` at most once a second), `/proc/self/status` and `/proc/meminfo`, the `perf`
  lines' fits and texts, the rest. Formatting the whole block is about 760 numbers (15 KB on the
  owner's Pixel), well under a millisecond on a phone core (0.16 ms on the test machine).

  How the log is written: on Android stdout and stderr are a pipe; each `fprintf` is one `write`
  into it (a copy into the kernel's pipe buffer) and the `ico-log` thread writes the file and
  logcat, so a slow file write (the app's external folder is FUSE-backed) holds the log thread, not
  the game, render or submit thread, until the pipe is full. "never full: no writer waited" is the
  proof for the block; "full" names the one case where logging can hold a game thread. The file is
  never synced; on a hard kill what is lost is what the pipe still held (normally nothing). On Linux
  and macOS each line is one `write` on its own thread; on Windows the streams are buffered and
  flushed once a vsync. A late game step is one 10 ms or more past its period; the overlaps say
  whether a slow write or a capture coincided with a stall, not that it caused it.
- **The piece before each top pass** (`window: GPU top passes:`, `rd_perf.c`
  `rd_perf_pass_agg_text_ex`; `perf classes`, `rd_perf_rank.c`): each entry is followed by the
  piece before it and how often that piece was a full-size render pass, e.g. `shadow count 576x448
  1 draws 3.46 ms after list 0 SCENE 1728x1344 (the last seen; the piece before was full size in 349 of 349)` and, in the classes line,
  `; after list 0 1728x1344 (the last seen; the piece before was full size in 349 of 349)`. On a tile-based GPU a stamp after a
  full-size pass may carry that pass's tile work (issue 123's 3.46 ms for one 576x448 draw), so a
  piece that always follows one and shrinks where it does not is carrying it.
- **Before list 0 and list 0 as one on a tile-based GPU**: the stamp that ends the uploads lands
  inside list 0's work there (the two trade at a constant sum: issue 179's picture with 128.38 ms
  "before list 0" beside a 6.06 ms list 0), so `perf classes`, `perf ranked` and `GPU top passes`
  give `before list 0 + list 0` (the split stays in the CSV), and the ranked line never names
  `uploads` as a saving there. The histogram's top bin prints as `worst 256 ms or more`.
- **The every-replay pass timing, bounded** (`port/platform/window_stall.c`): a present that
  blocks over 50 ms still arms it (`window: every replay timed pass by pass from here on ...`), but
  it now ends at the end of the block in which a stalled replay's passes were compared, or after 3
  blocks with no present blocking: `window: pass timing: back to one replay every 250 ms (the
  stalled replay compared; N times of the run); a present that blocks arms it again`. Issue 179's
  and issue 123's logs had it armed for the rest of the run (19-46 stamps a picture). While both
  kinds of picture are in a block, its cost is measured from the pictures themselves:
  `window: pass timing's cost: GPU span median 67.4 ms with every pass stamped (37 pictures), 64.1
  ms with the list stamps only (111 pictures): +3.3 ms (an estimate: different pictures of one
  block)`.
- **What the device offers** (`port/rhi/vk/vk_device.c` `vkr_offer_line`, once at the device's
  start, read-only queries, nothing enabled):

      rhi_vk: offered (read only, nothing enabled for it): calibrated time domains KHR: device,
      CLOCK_MONOTONIC, CLOCK_MONOTONIC_RAW; global queue priority KHR offered, its query too (the
      game's queue family 0's priorities: 4: low, medium, high, realtime); the game's one queue made
      at priority 1.0 with no global priority asked (the driver's default, medium); display timing:
      VK_GOOGLE_display_timing yes, VK_EXT_present_timing no, VK_KHR_present_id yes,
      VK_KHR_present_wait yes

  Whether the compositor could preempt the game's queue (issue 123's T2) and whether a stall could
  be placed on the CPU's clock if the polled wait were used again. The calibrated domains are
  listed whatever the submission wait; they are enabled only with the polled wait, as before. The
  display timing clause (the values above an example) says which of the means of learning when a
  picture reached the screen the device offers, for the evenness line's next step
  ([the evenness line on a 120 Hz phone](#the-evenness-line-on-a-120-hz-phone)); none is enabled
  for it (the present wait is enabled only with `ICO_VK_PRESENT_WAIT=1`). On Android the first
  frame rate pin line is followed by `window: display timing on Android (read only, nothing
  called): AChoreographer frame callbacks reachable (AChoreographer_getInstance yes,
  AChoreographer_postFrameCallback64 yes, AChoreographer_postVsyncCallback yes)`: whether
  libandroid.so has those functions (`dlsym` only; `port/platform/window_display_rate.c`,
  `display_rate.c` (`display_choreo_text`)).
- **The pause picture without its wait** (`port/render/rd_shot_late.h`, `rd_present.c`): on
  Vulkan on a tile-based GPU the pause menu's state picture is copied by the present's own list
  into a readback buffer and read when that frame slot's fence has been waited for by the replay
  that reuses it, as the skin check reads its readbacks. Issue 179's log had each pause's read-back
  hold the render thread 77-79 ms (`rd: state picture: ... read back in 77.6 ms`), a one-shot
  submission and a wait for the queue, after which the GPU idled 56.9 ms. Now: `rd: state picture:
  1612x720 from the 1612x720 output (the box at 0,0), read at its slot's recycle 2 pictures later,
  no wait (63.0 ms after its present)`. No new wait of any kind; a photo, a second request while one
  is pending, and the other backends keep the old read.
- **The thermal and memory asks off the game thread** (`port/platform/window_stall.c`,
  `sys_cell.c`, the log thread's tick in `host_android.c`): the owner's Pixel measured the 10 s
  print's kernel GPU and thermal part at 1.6-2.8 ms: two JNI calls into PowerManager
  (binder calls) on the game thread each block. The log thread, which every Android start has
  (`ico-log`; the GPU sampler runs only where a node is readable, the hints' thread only from API
  33), now asks the thermal state and reads `/proc/self/status` and `/proc/meminfo` once a second
  (not while the app is in the background), outside the mirror's lock, into lock-free last-value
  cells; the 10 s lines read the last answers. `window: the thermal state and the memory figures
  are asked once a second on the log thread; the 10 s lines read the last answers` at the start,
  the thermal line ends `(asked 0.4 s before on the log thread)`. Without the log thread they are
  asked on the game thread as before (never on both: `ico_host_thermal` keeps statics). At the
  start `window: device: Android R (API N), <manufacturer> <model> (<device>)`, from the system
  properties.
- **The other lines named, and the per-thread reads off the game thread**: the logging line's
  `the other lines R` is followed by its parts in order, `(the presents and layout lines a, the
  hold line b, the game thread line's parts, the audio line c, the display and slow scene lines d, the replays
  and VU lines e, the acquire and present lines f, the hints and display rate lines g, the GPU
  lines h, the passes, walk and pixel lines i, the rest j)`, and by `; the log thread's asks N (the
  thermal state T ms, the memory figures M ms the last time)`. Read formatter by formatter, those
  lines only format, except the game thread line's three `/proc/self/task/<tid>/schedstat` reads
  (and the thread list's scan of `/proc/self/task`, about 100 file reads every block): those were
  the bucket's cost on the owner's Pixel, 2.5 ms of the game thread line where every other family
  took 0.01-0.4 ms. They now run on the log thread's tick (`render_thread.c`
  `ico_render_thread_tick`: the three threads' schedstat into cells every second, the thread list
  every tenth second); the game thread line ends `(the threads' run times as the log thread read
  them up to N ms before)`, its deltas running between the log thread's reads, so up to a second
  more or less than the block. Its marks: `the hold line`, `the game thread line's cores and run
  times`, `the game thread line`, `the thread list`. Without the log thread they are read on the
  game thread as before.

## Ranking the costs and the attempts (issues 179 and 123)

Each problem the Mali-G52 (issue 179), Adreno 740 (issue 123) and Pixel 10 Pro Fold logs showed
has a question that the next log has to answer by itself. The lines below are what answer them.
Each one is printed once per 10 s block or on an event (the first 8, then every 100th). Every
counter is an increment in a hot path, and none of them reads anything from the GPU while it runs.

### The issue ledger

| Problem the logs showed | The question the next log answers | The line and field that answers it |
| --- | --- | --- |
| Mali stage 42: list 0 at 47-77 ms a picture for 52-77k triangles | Is list 0 bound by its vertices or by its pixels? | `window: costs a picture:`: `boxes against triangles: list 0 N triangles in M Mpx of boxes (K times its target's ...)`, which you set against `window: compare:`'s `list 0` ms across blocks. `window: VU: ...; the draws' vu_main_vs N more (3 a triangle ...)` and the `perf scene` line's `(the VU pre-pass's; the draws' own N more, 3 a triangle)` give the draws' own vertex work. `rhi_vk: pipeline statistics: offered` or `not offered` says whether a later release could sample the split |
| Mali: robust access set by hand | Is robust access on, and where from? | `rhi_vk: loss diagnostics: ... robust access on, buffers (gpu_robust)` and `rd: profile: ... robust access on, buffers (gpu_robust)` at the start, plus `ico_pc: [dev] gpu_robust = true ... has no gpu_robust_by mark: set by hand`, and in every block the compare line's `; robust access on, buffers (gpu_robust)` |
| Mali: single passes of 100-280 ms, presents blocked 254-385 ms | What coincided with each one? | `window: stall: <the pass or the present>; at that moment: ...` for each stall (the cores and their clocks, the core waits in the last second, the driver's threads, memory against the floor, memory pressure, thermal headroom, the classes), and once per block `window: stalls by what coincided: N in the block (...)` |
| Mali: device memory under its floor; the process swinging 538-1067 MB while graphics stays at 335 MB | Which part of the process swings? | `window: memory in the block: the device's least N MB available against the floor F MB (under it at u of r reads); the process a-b MB resident: anonymous ..., file-backed ..., shared ...; the C heap ...; beside them the graphics allocations ..., the dumps' capture blocks ...` |
| Mali: three automatic captures of 47-56 ms on the game thread | Which step costs the game thread? | `window: slow-scene dump: the game thread's parts: the measure M ms, the copy C ms (each without its drain); the worker's block made in P ms; two steps: frame N copied into the worker's block` (or `grown here: ...`), after the capture line |
| Mali: the game, render and both audio threads on the two fast cores | Where do the threads run, and does audio keep up on the slow cores? | `window: render thread cores: ...` (the few-cores rule's sets and moves); the game thread line's `ran / waited for a core: game ..., render ..., submit ..., SDL audio ..., system audio ...` and after it `; run time on the faster cores 6-7: submit N % of T s, SDL audio ..., system audio ..., <the driver's threads> ..., pipeline workers ...` ([The faster cores' share of run time](#the-faster-cores-share-of-run-time)); `audio: ... ms of silence (... ms held back behind the game) ...; the sound chip's emulation E ms a callback on average (longest L ms; R renders) against the device's period of P ms`; and the ledger's `few-cores rule on N threads moved` |
| Few-core phones: start-up of 62.2 s (51.2 s of it pipelines, cold cache) | How long does the pipeline set take, on which cores, and with how much CPU time? | `rd: pipelines: N of the reachable set's M created at start-up in T ms ...` (wall time), followed by `window: pipeline set: made in T s ...; its N workers on every core ... while it ran ...; the process's CPU time C s over it (B cores busy on average)` |
| Adreno 740 hangs | Did this release's attempt run, and how many stalls were there with it on? | The start lines that name the hung draw (`rd: GPU clock: ...`); the attempt ledger's row `Qualcomm passes as VkRenderPass objects on (<the row's reason>) N passes` (`not applicable here (not Qualcomm's own driver)` elsewhere, Turnip included); the ledger line's `; stalls N in the block (the Adreno attempt on)`; and each stall's `coincided with: ..., the GPU's own sign of a reset` with the tally's `the GPU's own sign of a reset N` |
| Pixel opening: lamps, dark and sun views | What does each block cost, and which cut fired? | `window: compare: ...; GPU ms a picture: ...; glow plans: lamps N, dark N, whole N`, `passes` and `breaks` a picture, and the ledger line's per-cut fired counts |

### The attempt ledger

`port/platform/attempt_ledger.h`: one row per behaviour that this release changes on purpose for a
device class. Each row has a name, its state on this device (on, off by default, refused by its
self-test, not applicable here, or not reported) with a reason, and up to two counts. Two lines
print it:

- At the first 10 s block, and again whenever a state changes (the first 8 changes, then every
  100th): `window: attempts at the start: few-cores rule on (2 cores faster than the slowest);
  pipeline set on every core on (2 cores faster than the slowest); capture in two steps on (the copy
  into a block the dump worker made); flat outputs at the provoking corner on (every device and
  backend); one DATE set-up not reported; ...`
- In every block: `window: attempts: few-cores rule on 3 threads moved; pipeline set on every core
  on 0 workers (0 sets); capture in two steps on 1 captures (0 grown here); flat outputs at the
  provoking corner on (static); ...; stalls 3 in the block`. Rows that are not reported are left
  out. Where the Adreno row is reported, the stalls clause adds `(the Adreno attempt on)` or `off`.

To add a row, a package adds one enum value and one table row with its words, a decider
(`ico_attempt_decide_*`: the row's state and its reason from the facts its owner has, so the unit
test can set every row as a device of each class would) called where it decides, and
`ico_attempt_fire` / `ico_attempt_fire2` where it fires. Each call is a relaxed atomic, so any
thread can make it, hot paths included. The rows and where their states are set:

| Row | State set by | Counted by |
| --- | --- | --- |
| few-cores rule, pipeline set on every core | `render_thread.c` `place_init` (`ico_attempt_decide_cores`: on with one or two faster cores and the pins allowed; not applicable with three or more, cores of one speed, or off Android) | the keeper's and the sweep's moves; the sets' workers |
| capture in two steps | `window_perf_rank.c` at the first block (every platform) | `window_dump_auto.c` |
| flat outputs at the provoking corner | `rd_vu_flat0.c` at the renderer's start | static |
| one DATE set-up | `rd_date_one.c` at each replay's scan (off by the tests' switch, not applicable under MSAA or the exact blend) | the window, from the record's `dateOneShared` and `dateOneRefused` |
| empty composites skipped | `rd_shadow_box.c` at each replay's scan | the window, from `emptySkipped` |
| no-volume break | `rd_no_volume.c` at each replay's scan: `off by default (off by default until a phone rc shows the PowerVR phone's title and pause menu clean with it)` on the PowerVR and MoltenVK class, `not applicable here (not the PowerVR or MoltenVK class under Vulkan)` elsewhere, or the other gates' reason (the DATE path, MSAA ...) with the class allowed | the window, from `noVolumeBreak` |
| Qualcomm passes as VkRenderPass objects | `window_perf_rank.c` every block (`ico_attempt_decide_pass_objects`: on with `RhiLimits.passObjectsRow`'s reason while Qualcomm's row holds the device's passes, `not applicable here (not Qualcomm's own driver)` otherwise) | the block's growth of `RhiStats.passObjects`, only while the row holds |
| Adreno attempt B | `not in this build` (one Adreno attempt was built) | none |
| subpass read, blend constant, index compaction | `not in this build` from the start (left out of the block line) | none |

The shadow cuts' counts come from the replay's record where the window reads it
(`window_perf_rank_record`), not from the cuts' hot paths.

### The cost counters

`port/render/rd_perf_more.h` and `rd_perf_hooks.c` count into the replay's record (`RdPerfRecord`
`noDual*`, `breakN`, `breakKB`, `listBox*`, `vuListTris`). The window adds each record into a
block sum and prints one line per block:

`window: costs a picture: two-pass blend 3.0 draws drawn twice (2.1k triangles' vertex work twice),
1.0 drawn once by the colour pass; SCENE's pass ended mid-picture 3.0 times (DATE 2.0, 9.2 MB; aura
1.0, 4.5 MB); DATE snapshots 3.0 (the first 1.0, an alpha write since 2.0), 0.0 reused, 0.0
composites with an empty box; boxes against triangles: list 0 50k triangles in 1.85 Mpx of boxes
(4.0 times its target's 0.47 Mpx GS; 112 draws, 6 without a box), list 3 2.0k triangles, no box
(...)`

- **The two-pass blend** clause appears only on a device without dual-source blending. A 2D
  draw's passes are counted as their DrawCB is filled (`rd_fx_plan.c` `rd__sprite_above1x`'s head
  runs once for every pass that is drawn). A VU draw is counted once per draw from its planned
  passes (`vuDraw`), because the draw merge can fill a pass twice when it refuses the pass. Its
  triangles count twice when it is drawn twice. Screen-box areas are not counted for these draws:
  the culler's boxes are made before the walk and are not carried to the draw.
- **The breaks** count every render pass on SCENE that begins after an earlier one in the same
  replay. The reason is the first piece of GPU work between the two: a DATE snapshot, the shadow
  count, levels or stencil pass, the aura, the reduction, the crack fill, the glow, the lamps' work
  buffers, copies, the present, `again` (nothing between), or `other`. The bytes are the stores of
  the SCENE pass that ended plus the loads of the one that began again, from the RHI's traffic
  counters. Those counters are read only around SCENE's passes. Only SCENE is tracked; other
  full-size targets are not.
- **DATE** shows the snapshots split by why each was taken (`rd_date_cut.c`'s reasons), the
  reuses, and the shadow composites whose box was empty (`shadowBoxEmpty`: set-ups whose draw then
  drew nothing).
- **The boxes**: for each VU draw command the culler keeps (`rd_vis.c` `cmdVerdict`), the box of
  the batches it reads is projected through its VU matrix and clipped to the target, in GS pixels
  (`rd_perf_box_area`: 8 corners, or 16 over the w range in the scissor mode; a corner at or behind
  the eye counts as the whole target). These boxes sit next to the list's triangles. A list whose
  boxes grow while its triangles stay flat is bound by fill. Particles and skinned draws have no
  box. With `[dev] cull` off, no draw has one.

`rhi_vk: pipeline statistics: offered (read only, nothing enabled: ...)` or `not offered` comes
once at the device's start. Sampling the vertex and fragment invocations is not done in this
release. The query pool would be begun and ended inside list 0's render passes, and on tile-based
phones every read of in-flight work stays off (the single submission wait). The line says which
devices could take a sampled replay later.

### What a stall coincided with

`port/platform/stall_coincide.h`. A stall is a timed replay's pass that runs over 8 times its
block's median for that pass (the same kind, target and list, with at least 5 seen) and over 40 ms,
or a present over 100 ms. Each stall writes one line from the last samples. Nothing is read from a
file on the game or render thread for it:

`window: stall: list 0 912x512 283.3 ms against the block's median 48.0 ms (replay 2709, GPU span
411.4 ms); at that moment: the game thread on core 6 (909 MHz), the render thread on core 7 (1443
MHz); in the last second the core waits: game 60 ms, render 40 ms, submit 5 ms, SDL audio 3 ms;
the driver's threads: mali-cmar-backe ran 120 ms, waited 30 ms; memory 501 MB available against
the floor 641 MB (under it); pressure some 12.3 %, full 4.1 % (avg10; some 3.2 % a second before:
rising); thermal headroom 0.80; coincided with: memory under the floor, memory pressure rising`

A present's line names its acquire, the swapchain lock, the driver's present call and the surface
poll apart, and the replay's GPU span. How long the present held past that span is in the block's
`present stalls` line. The samples:

- **Cores**: the game thread's core now (it writes the line). The render thread's core is the one
  at its last picture. Each core's clock comes from the kernel sampler's ring over the stall:
  the lowest during it, else the last sample before.
- **Core waits** of the game, render, submit, SDL audio and system audio threads over the log
  thread's last tick (`render_thread.c` `ico_render_thread_last_second`, the difference between
  two once-a-second schedstat reads).
- **The driver's threads**: the threads whose name looks like the graphics driver's (`mali...`,
  `kgsl...`, `adreno...`, `QGL...`, `pvr...`, `IMG...`, `gpu...`, `tu_...`). The first four are
  read each second. The whole list is printed once: `window: threads named like the graphics
  driver's: mali-cmar-backend, mali-utility-wo, ... (the first 4 are named at a stall with their
  run and wait times)`.
- **Memory** against the start-up set's floor. **Pressure** is `/proc/pressure/memory`'s avg10,
  read once a second on the log thread, or `not readable (errno N)`. **Thermal headroom** is the
  log thread's last ask.
- **The classes** (`coincided with:`): a core wait over 100 ms in the last second, memory under
  the floor, memory pressure rising (some avg10 above the reading before), the GPU clock under half
  its fastest (where readable), the GPU's own sign of a reset, or none of these. Each block,
  beside the attempt ledger: `window: stalls by what coincided: 3 in the block (a core wait over
  100 ms 1, memory under the floor 2, memory pressure rising 1, the GPU clock low 0 (not
  readable), the GPU's own sign of a reset 0, none of these 1)`.
- **The GPU's own sign of a reset**: the stalled replay, or one of the two before it, had recorded
  stamps come back unwritten after its work was signalled finished, ran its first list twice, or
  saw the GPU's timestamp counter go back (`rd_perf_clock.h`'s fields of the record). The line
  names it: `the GPU's own sign of a reset at replay 689: 19 stamps came back unwritten after its
  work was signalled finished`.

Issue 123's Adreno 740 log (Qualcomm's driver) had 12 stalls of 401-415 ms, 2 of them in stage 1's
opening. In every one the hung replay's `rd: GPU clock` line has 19-45 stamps unwritten after the
work was signalled finished (the first: list 0's end), and the next present blocked. KGSL is not
readable there (EACCES), so the GPU's clock is not known. The device had 1650-1716 MB available
against Qualcomm's floor of 1947 MB for the whole run. Read from those fields, each stall prints
`coincided with: memory under the floor, the GPU's own sign of a reset`, plus a core wait where one
crossed 100 ms. Without the reset class they would print only `memory under the floor`, which
every block of that run had whether it stalled or not. A tally with `the GPU's own sign of a reset`
equal to the stall count says the stalls are the GPU's own hangs, and the memory and wait classes
are the context around them.

Outside Android only the cores and the record's own numbers are known. The other parts say `not
sampled` or `not known`.

### The GPU clock where GED is denied

The probe (`gpu_sysfs.h`) also tries `/sys/kernel/gpu/gpu_clock` (MHz) and
`/sys/kernel/gpu/gpu_busy` (a percentage), and samples them when they are readable and no earlier
node gave a clock or a busy figure. It also reads `/sys/class/misc/mali0/device/gpuinfo` and
`/proc/gpufreqv2/gpufreq_status` once and quotes them. The open line says per node whether it is
readable (its first text quoted), not readable (with the errno) or not present. None of these
paths or formats has been checked on a device.

### Comparing two logs

`window: compare: stage 42 scene (24a4demo): 91 pictures (0 keep frames); a picture 52k triangles,
351 draws, 21.0 passes, 3.0 breaks; GPU ms a picture: span 110.0, before list 0 4.3, list 0 76.5,
list 3 10.1, aura 5.5, reduction 4.3, glow 0.0, crack fill 0.0, present 2.9, pre-pass 6.2; vertex
invocations a picture: the pre-pass's 85k, the draws' 156k (3 a triangle; never summed); glow
plans: none 91; robust access on, buffers (gpu_robust)`

This is one line per block with the fields that compare two logs of the same scene. The scene key
is `window_scene_key`'s, as the `perf scene` line uses. The GPU figures are the records' own,
averaged over the pictures with GPU times. The earlier logs' lines stay word for word, so they
line up with logs from before this line existed: `window: perf scene:` (with its new bracket
after the vertex invocations), `window: perf classes`, `window: GPU ms per replay: lists 0-12`,
`window: per replay N draws, N passes`, `window: passes per replay`, the `VU:` line's text up to
its new clause, `window: memory:`, `audio:` up to its new clause, and `window: present stalls`.

The Mali log printed `before list 0 + list 0 81.05 ms` in two blocks in a row. That is not a
stale value. The `perf classes` line is rebuilt in each block from that block's timed pieces
(`window_perf_rank.c` resets its block after the print). The `GPU top passes` line comes from a
separate sum that `perf_reset` clears. The two blocks had different timed replays (90 and 81),
different draws (315 and 410) and different list 0 and before-list-0 parts (76.46 + 4.3 and 76.53
+ 5.3 over every record). Two four-digit means of different sets matched.

### The memory in a block

On Android, the log thread reads `/proc/self/status`'s `RssAnon`, `RssFile` and `RssShmem` and the
C heap's `mallinfo` once a second, and keeps the block's extremes. The window prints them after
the memory line, and the next block starts from the next read:

`window: memory in the block: the device's least 501 MB available against the floor 641 MB (under
it at 8 of 10 reads); the process 538-1067 MB resident: anonymous 410-520 MB, file-backed 120-540 MB,
shared 5-5 MB (/proc/self/status, once a second); the C heap 380 MB in use and 60 MB mapped (at the
last read); beside them the graphics allocations 335 MB, the dumps' capture blocks 0.0 MB`

The numbers in this example are made up. A swing in the file-backed part is the game data's
mapped pages being read in and dropped by the system. A swing in the anonymous part is the
process's own allocations. Texture copies held by the renderer and audio buffers are not counted
apart.

### The pipeline set's cores

`render_thread.c` `ico_render_thread_pipeline_set`, called around `rd_precreate_pipelines` at
the start and at a switch of the graphics API.

At a device's first start the pipeline set runs before any cores' rule exists, because
`place_init` runs when the render thread starts, after the set. So the workers are on whatever
cores the system gives them: `window: pipeline set: made in 51.2 s with no cores' rule in force
(nothing pinned: its workers ran on any core the system gave them); the process's CPU time ... over it
(... cores busy on average)`.

A set made later, while the few-cores rule is in force (a graphics switch, or a start after a lost
device), is made while the game and render threads wait for it. Its workers (`rhi-pipelines`)
keep every core of the process, and the once-a-second sweep leaves them alone:
`window: pipeline set: made in 12.3 s; its 7 workers on every core 0-7 while it ran (the few-cores
rule's exception: the set is made before the game and render threads have steady work); ...;
pipeline workers made from here (lazy compiles, the background finisher) run on 0-5`. Pipeline
workers made after the set (lazy compiles, the background finisher during play) follow the rule.

### The slow-scene capture's parts

After the capture line: `window: slow-scene dump: the game thread's parts: the measure M ms, the
copy C ms (each without its drain); the worker's block made in P ms; two steps: frame N copied into
the worker's block`. If the copy fell back, the line ends `grown here: frame N had grown past the
worker's block`, `grown here: frame N missed the worker's block`, or `grown here: the worker could
not be asked`. Captures in two steps and fallbacks are the ledger's `capture in two steps` counts.

### The sound chip's time per callback

The pull path's 10 s line ends `; the sound chip's emulation 0.81 ms a callback on average (longest
3.20 ms; 1001 renders) against the device's period of 10.00 ms`. This is the SPU2's render inside
SDL's audio callback, timed around `pull_render`. When the average or the longest nears the
period, audio cannot keep up on the core it runs on. That shows up beside `ms of silence (... held
back behind the game)`.

### What these lines cost

Per block, a phone adds about 1.5 KB:

- the costs line: about 400 bytes
- the compare line: about 350 bytes
- the attempts line: about 350 bytes
- the stall tally: 150 bytes when there are stalls
- the memory-in-the-block line: about 330 bytes
- the audio clause: about 120 bytes

Each stall line is about 600 bytes, and those lines are rate-limited. The start adds the ledger's
start line (about 700 bytes), the pipeline set's line and the driver threads' line. The block
lines are formatted on the game thread inside the 10 s print. The logging line's `the display and
slow scene lines` and `the performance lines` parts include them, so the logging-cost clause still
counts them.

On the log thread, the extra file reads each second are `/proc/pressure/memory`,
`/proc/self/status`, up to four driver threads' `schedstat` files and `mallinfo`. The renderer
adds a few increments a draw, a box projection per kept VU command, and three `RhiStats`
snapshots per SCENE break.

### The faster cores' share of run time

The game and render threads sample their own core every step and picture (`the samples by core`).
The others cannot, so the log thread estimates their share once a second (`core_share.h`,
`render_thread.c` `share_tick`; Android, on any device whose cores are not all of one speed, the
keeper's three-or-more class too). The threads it covers are the submit thread, SDL's and the
system's audio threads, the first four threads named like the graphics driver's, and the pipeline
workers: the set's own ids while one runs, and the `rhi-pipelines` threads the last ten-second
list found. For each one it reads the schedstat run time and field 39 of
`/proc/self/task/<tid>/stat` (the core it last ran on), and adds that second's run time to the
last core's class. The game thread's 10 s line then adds `; run time on the faster cores 6-7:
submit 4 % of 0.3 s, SDL audio 0 % of 0.4 s, system audio 0 % of 0.1 s, mali-cmar-backe 1 % of 1.2
s, pipeline workers 100 % of 2.0 s (each thread's last core once a second, weighted by its run time
in that second)`.

The method's error: a second's run time goes whole to the core the thread was on when it was last
read. A thread that moved between the faster and slower cores within a second is counted entirely
on one side, so the error is at most the run time of the seconds in which it moved. A thread pinned
to one class has none. Pipeline workers that live less than ten seconds outside a set are missed.
The log thread makes two more file reads per thread per second: up to 30.
