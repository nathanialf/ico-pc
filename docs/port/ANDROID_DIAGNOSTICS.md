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

### The barrier scopes and the submission wait (issues 179 and 211)

A Mali-G52 MC2 (Arm's driver) and an Adreno 710 (Qualcomm's driver) lost the graphics device at the
first pictures of the first 3D scene, three starts in a row each, once the buffer barriers were
narrowed and the polled submission wait came in; the release before both ran there. Every
tile-based GPU now runs the wide barrier scopes, the texture uploads before the VU pre-pass and the
single submission wait by default (RHI.md "Barrier scopes and the submission wait"). The start of a
log says which ran and why, one line each:

    rhi_vk: barriers: wide (a tile-based GPU, Arm, vendor 0x13B5): every buffer barrier waits for ...
    rhi_vk: submission wait: single (a tile-based GPU, Arm, vendor 0x13B5): one wait without a time limit for a frame slot, ...
    rhi_vk: timestamps: ...; calibrated with the CPU's clock: off (the single submission wait: single (tile-based)) ...

and the summary's profile words end `, barriers wide (tile-based), submission wait single
(tile-based)`. How to read the next log:

- Both lines `wide` / `single` and no loss: one of the two changes was the cause, or set off the
  driver's fault (the code reading for issue 179 found no other change to the GPU work of those
  pictures on the Mali; on the Adreno the skin check's twins and readbacks also run there).
  Which of the two is not told apart by a setting (there is none); nothing to ask for.
- Both lines present and the device still lost: neither was; the loss dump's lines (below) and
  the stall lines (issue 123's sections) are the evidence then.
- `narrow (a GPU that is not tile-based)` or `polled (...)` on a phone: the GPU was not taken for
  a tile-based one (its vendor id and the reason's words say why); `(ICO_VK_FAKE_SCOPES=..., a
  test)` only ever in a test.
- With the single wait the stall diagnostics measure less: a stalled replay's `rd: GPU clock`
  line has no trace (which of its stamps were written while the CPU waited, the longest stretch
  with nothing new, its first list's second run), and its start is placed from its wait's end,
  not on the CPU's clock. Its counter restarts, unwritten stamps and the wait's own time are kept,
  and a wait of 100 ms or more gets the line without a trace, with the time from the replay's last
  submit to the wait's return: `waited 340.0 ms for it from 38.0 ms after its last submit (no
  trace: the single submission wait reads nothing of its work while it runs); its work done about
  378.0 ms after its last submit (the wait's end)` (`rd_perf_clock.c`; the CPU's clock read around
  the wait the backend makes anyway, nothing of the slot read while it runs).

The loss ladder, on every device (desktops too) and never written to the settings: a start after a
lost one runs the wide scopes, after two lost starts in a row the single wait too. Its line,
before the backend's:

    ico_pc: loss ladder: 2 starts in a row lost the graphics device before this one: this start runs the wide barrier scopes and the single submission wait (the graphics backend's barriers and submission wait line says what ran); nothing is written to the settings

Until this fix a `gpu_robust = true` in the settings (by hand, or written by the game after two
lost starts) set the row back to 0 every start: both issue 179's and issue 211's logs said `starts in a row
that lost the graphics device: 1` start after start, so no rung past the first could fire. The row
now counts on (`ico_pc: the previous start lost the graphics device (2 in a row); nothing is written
to <path>: robust access is as the settings say ([dev] gpu_robust)`). The desktop's restarts after
a loss count in the activity file's `-ends.txt` as `loss_chain <n>` (`... before this one (this
game's restarts after a loss): ...`).

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
row and draws through the pre-pass). Which of the wide scopes or the single wait, if either, keeps
the 710 from losing the device is what its next log says by the lines above.

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
hash lookup a pass). The scene is the block's dominant layout with the stage for play and scenes
(`stage 3 play`, `stage 3 scene`, `title`, `pause`), `(keep frames)` when most of its pictures
were keep frames. The figures below are the unit test's made-up block of stage 3 play's shape
(`rd_perf_rank_test`), not a phone's.

- `window: perf scene: stage 3 play, 168 pictures (0 keep frames): 16.8 of 25.0 fps (the game 25.0
  of 25.0), GPU-bound; the GPU's work 59.0 ms a picture of the 59.5 ms between presents (99 %); the
  render thread's own CPU 13.3 ms (walk 9.2, submit 1.7, uploads 2.0, binds 0.4, interpolation
  0.0), its waits for the GPU 27.5 ms, its present calls 25.8 ms; the game step's CPU 2.9 ms of its
  40.0 ms tick; a picture's work done 61.0 ms after its last submit (longest 61.0), the slot wait
  blocked 168 times (27.0 ms on average, longest 27.0); passes asked a picture: 20.0 passes, colour
  8.0 MB loaded 9.0 MB stored, depth and stencil 4.0 MB loaded 2.0 MB stored, 3.0 MB cleared, 6.00
  Mpx of area, copies 1.0 MB (uncompressed; 1.0 passes with an attachment of no known format, at 4
  bytes a texel); 360 draws, 329 pipeline and 458 bind group binds, 46k vertex invocations, 1826 KB
  uploaded (1.0 textures), 3.0 DATE snapshots; GPU a picture: span 60.0 ms, ...; the pictures' GPU
  work median 59.5 ms, worst 63 ms`. Decides the limiting side, from the block alone: below 95 %
  of the target rate (Frame rate Original: the game's 25 or 30; with interpolation the display's
  refresh) or the game under its own rate, the GPU when its work (the span less the display wait)
  fills 80 % of the time between presents; else the render thread when its own CPU (interpolation,
  uploads, walk, binds, submit: not its waits) does; else the game thread when its step's CPU
  fills 80 % of its tick; else `bound by waits elsewhere` (the present calls, the slot waits).
  `a picture's work done N ms after its last submit` is the single wait's own return against the
  picture's last submit: the GPU's latency for a picture where the wait blocked (an upper bound
  where the work had finished first). The bytes are what the passes ASK for (LOAD loads, STORE
  stores, at the format's bytes a texel times the samples over the render area): Arm's AFBC or
  Qualcomm's UBWC move fewer, and a store nothing reads may be dropped by the driver, so they are
  the ceiling of the tile-memory traffic and the counters a saving scales with.
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
  (R^2 1.00, 168 pictures); the estimates use the byte-bound passes' fit (0.40 ms a MB)`: this
  device's constants with no extra work: a least-squares fit over the pieces whose time is their
  bytes (the DATE snapshots, the passes with no draw: a fixed cost a pass and a cost a MB moved),
  the copies' and snapshots' ms a MB, and the vertex fit of the VU line. The estimates take the
  fit's ms a MB where it holds (8 pieces, a spread in their bytes, R^2 0.3 or over), else the
  copies'. `no fit (N, or no spread in their bytes)` where every snapshot moved the same bytes.
- `window: perf slowest picture: replay 4821 (frame 4820), GPU span 63.1 ms over 20 pieces: list 3
  912x512 24.10 ms (70 draws, 66 binds; loads 4.1 MB, stores 4.1 MB), ... (the top 5 by GPU time;
  GPU time by pipeline is not measured)`: the block's slowest timed picture's costliest pieces.
- `window: perf ranked: stage 3 play: 16.8 of 25.0 fps (the game 25.0 of 25.0), GPU-bound (the
  GPU's work 59.0 ms a picture of the 59.5 ms between presents): 1. vertex work est. 23.0 ms (46k
  vertex invocations a picture at 0.500 ms a thousand, R^2 1.00); 2. list 3 22.4 ms measured (1.0
  passes, 61 draws, 61 binds; loads 3.6 MB, stores 3.6 MB, 0.47 Mpx); 3. list 0 20.1 ms measured
  (...) (each saving alone, they overlap; est. = this block's counters times its own rates)`:
  only in a block below the rate. The three largest candidates for the limiting side: for the GPU
  each class's measured time (its own ceiling), `tile loads est.` (the MB loaded a picture times
  the rate), `depth stores est.` (the depth and stencil stored), `pass count est.` (the passes
  times the fit's fixed cost a pass), `DATE breaks est.` (the snapshots' measured time plus the MB
  a scene pass loads again after each), `vertex work est.` (the invocations times the vertex fit's
  slope), the uploads; for the render thread its phases with their draws and binds; for the game
  thread its step. Decides the next release's work: the first entry is the largest measured or
  estimated saving on this phone in this scene.
- `window: performance profile (every 300 s of the run, 4 scenes, profile 1 of the run):` then one
  line a scene, `window: performance profile: stage 3 play: 18 blocks (180 s, 3024 pictures); a
  picture's GPU work median 47.5 ms, worst 109 ms; presented fps median 16.9, lowest block 12.4,
  of 25.0; GPU-bound in 18 blocks; the worst block: 12.4 of 25.0 fps ... 1. ...; 2. ...; 3. ...`,
  every 300 s of blocks and at the run's end (a phone's system often ends the app without one: the
  report keeps the latest anyway). The bug report's `report.txt` has a section `the performance
  lines by scene (the latest ranking and profile of each)` built from the log, so one zip carries
  the ranking of every scene.

What the lines cannot size, and why: overdraw (no fragment count without a query: the area
texels a pass covers are its floor), GPU time by pipeline or draw (no stamps inside a pass beyond
the lists' own), the robust-access cost (`gpu_robust` is the player's setting and an A/B would need
it changed), whether the pre-pass overlaps the picture before on the GPU (the stamps are written
once the work before them is done), the GPU's clock steps on a Mali whose kernel nodes are closed
(the kernel GPU driver line says which are readable; the thermal line gives the app's headroom).

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
  copied to memory first (`rd: loss keep: frame N (replay R) copied to memory ...`, the first eight
  and every hundredth), at most 4 copies of at most 16 MiB (64 MiB at the worst, normally
  nothing). A dump taken from the ring alone, for a loss at frame 129, held frames 132 and 131 and
  so replayed the wrong frame in the next start's bisect; a line `the lost frame F is not held any more ... writing
  frame X instead ... it is NOT the frame whose submission failed` says the true frame is gone,
  and the closing line says `NOT the lost one`;
- `window: loss dump: writing frame N, the lost one (replay R), to .../logs/loss-<stamp>-lost.rddump`,
  then `... writing frame M (the one replayed before it) to ...-before.rddump`: the frames' writes
  (the CPU's copies of the commands, texels and meshes, onto the phone's shared storage);
- `window: loss dump: zipping K MB (the two frames and the log) at store level`: the zip, stored
  (no deflate), K the files' size together;
- `window: loss dump: frames N (the lost one) and M (the one before) written to <zip> in T s`:
  done, T from the first step; then the prune line (`n loss zips ... : d deleted (names)`). The
  zip just written is never deleted: `the name order alone would have deleted this zip (the clock
  went back or the zone changed ...); it was kept` says it would have been.

The start after a loss replays the lost frame pass by pass (`ico_pc: loss bisect: ...`), and if
that runs clean, replays the frame before it and the lost one back to back with no wait for the GPU
between them, four times each, as the game's pictures overlap (the passes wait after each replay
and so never do): `window: loss bisect: overlap step: ...` says that it ran and whether the device
was lost, or `overlap step not run (...)`. When the zip cannot be read the line says which of
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
  ran twice, a stamp came back unwritten, or the counter restarted (the first 8, then every 100th):

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
