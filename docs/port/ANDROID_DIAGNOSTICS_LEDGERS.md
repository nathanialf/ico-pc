# Android diagnostics: ranking the costs and the attempts

The issue and attempt ledgers and the cost counters of [ANDROID_DIAGNOSTICS.md](ANDROID_DIAGNOSTICS.md), kept in a file of their own (that file is near the size gate).

## Ranking the costs and the attempts (issues 179 and 123)

Each problem the Mali-G52 (issue 179), Adreno 740 (issue 123) and Pixel 10 Pro Fold logs showed
has a question that the next log has to answer by itself. The lines below are what answer them.
Each one is printed once per 10 s block or on an event (the first 8, then every 100th). Every
counter is an increment in a hot path, and none of them reads anything from the GPU while it runs.

### The issue ledger

| Problem the logs showed | The question the next log answers | The line and field that answers it |
| --- | --- | --- |
| Mali-G52: a black picture from the first 3D picture on, every picture's GPU work stopping after `list 0's start` (stamps unwritten, clock restarts), the touch controls flickering | Is the picture complete? | The block's ledger row `picture health on N 3D pictures with stamps unwritten (M GPU clock restarts)`: 0 and 0 in every block with 3D is a complete picture, with the ledger's `index compaction off by default (off by default on the Mali-G52: it blacked out the picture there ...)`; no `rd: picture health: 3D pictures are coming back unfinished ...` line in the log or at the top of `report.txt`'s key lines (it names the GPU attempts on and robust access when it comes) ([The picture-health report](ANDROID_DIAGNOSTICS.md#the-picture-health-report-issue-179)) |
| Mali-G52: half of list 0's triangles drawn twice (`two-pass blend 811.2 draws drawn twice (29k triangles' vertex work twice), 0.0 drawn once by the colour pass` at :1423); list 0 12.0-54.7 ms in stage 42, 10.3-20.9 in stage 3's play | How many pairs went to one pass, and did list 0 fall? | `window: two-pass blend: P pairs drawn in one pass of Q, kept two-pass: ...`, the `costs a picture` line's `drawn once by the colour pass` up and `drawn twice` down, `compare:`'s `list 0` against those figures |
| Mali-G52: the device lost at `vkDeviceWaitIdle` (-4) as the app went to the background; the next start said `no lost start` | Was the loss at backgrounding counted? | In the run: `ico_pc: the graphics device was lost at the GPU wait as the app went to the background; counted now as a start that lost it ...` right after `rhi_vk: device lost`; at the next start: `ico_pc: starts in a row that lost the graphics device: 1` and `ico_pc: loss ladder: 1 start(s) in a row lost the graphics device before this one ...` (the loss ladder's steps), and the VU shaders line without `no lost start` |
| Mali stage 42: list 0 at 47-77 ms a picture for 52-77k triangles | Is list 0 bound by its vertices or by its pixels? | `window: costs a picture:`: `boxes against triangles: list 0 N triangles in M Mpx of boxes (K times its target's ...)`, which you set against `window: compare:`'s `list 0` ms across blocks. `window: VU: ...; the draws' vu_main_vs N more (3 a triangle ...)` and the `perf scene` line's `(the VU pre-pass's; the draws' own N more, 3 a triangle)` give the draws' own vertex work. `rhi_vk: pipeline statistics: offered` or `not offered` says whether a later release could sample the split |
| Mali: robust access left on by a key without the mark | Did the checks come off, and is robust access on, and where from? | The start's `ico_pc: robust access turned off ([dev] gpu_robust = true in <config.toml> had no gpu_robust_by mark: ...)` (once: the key is removed) and the attempt ledger's `the old robust-access line taken off on (...)` (`refused by its self-test (config.toml could not be written: ...)` when the file could not be written, then the next start tries again; `not applicable here` on the starts after), then `rhi_vk: loss diagnostics: ... robust access off (the default; ...)` and `rd: profile: ... robust access off ...` at the start, and in every block the compare line's robust clause. Robust access on again shows as `robust access on, ... (the restart after the graphics card stopped responding)` (a start after a loss) or `ico_pc: robust access on (set by the game after 2 lost starts on <version>): N clean starts of 3 before it turns off` |
| Mali: single passes of 100-280 ms, presents blocked 254-385 ms | What coincided with each one? | `window: stall: <the pass or the present>; at that moment: ...` for each stall (the cores and their clocks, the core waits in the last second, the driver's threads, memory against the floor, memory pressure, thermal headroom, the classes), and once per block `window: stalls by what coincided: N in the block (...)` |
| Mali: device memory under its floor; the process swinging 538-1067 MB while graphics stays at 335 MB | Which part of the process swings? | `window: memory in the block: the device's least N MB available against the floor F MB (under it at u of r reads); the process a-b MB resident: anonymous ..., file-backed ..., shared ...; the C heap ...; beside them the graphics allocations ..., the dumps' capture blocks ...` |
| Mali: three automatic captures of 47-56 ms on the game thread | Which step costs the game thread? | `window: slow-scene dump: the game thread's parts: the measure M ms, the copy C ms (each without its drain); the worker's block made in P ms; two steps: frame N copied into the worker's block` (or `grown here: ...`), after the capture line |
| Mali: the game, render and both audio threads on the two fast cores | Where do the threads run, and does audio keep up on the slow cores? | `window: render thread cores: ...` (the few-cores rule's sets and moves); the game thread line's `ran / waited for a core: game ..., render ..., submit ..., SDL audio ..., system audio ...` and after it `; run time on the faster cores 6-7: submit N % of T s, SDL audio ..., system audio ..., <the driver's threads> ..., pipeline workers ...` ([The faster cores' share of run time](#the-faster-cores-share-of-run-time)); `audio: ... ms of silence (... ms held back behind the game) ...; the sound chip's emulation E ms a callback on average (longest L ms; R renders) against the device's period of P ms`; and the ledger's `few-cores rule on N threads moved` |
| Few-core phones: start-up of 62.2 s (51.2 s of it pipelines, cold cache) | How long does the pipeline set take, on which cores, and with how much CPU time? | `rd: pipelines: N of the reachable set's M created at start-up in T ms ...` (wall time), followed by `window: pipeline set: made in T s ...; its N workers on every core ... while it ran ...; the process's CPU time C s over it (B cores busy on average)` |
| Adreno 740 hangs | Did this release's attempt run, and how many stalls were there with it on? | The start lines that name the hung draw (`rd: GPU clock: ...`); the attempt ledger's row `Qualcomm passes as VkRenderPass objects on (<the row's reason>) N passes` (`not applicable here (not Qualcomm's own driver)` elsewhere, Turnip included); the ledger line's `; stalls N in the block (the Adreno attempt on)`; and each stall's `coincided with: ..., the GPU's own sign of a reset` with the tally's `the GPU's own sign of a reset N` |
| Adreno 740 hangs (unchanged by the `VkRenderPass` path and by the high queue priority, which was taken off) | Are the hangs as many without the priority, and does the rest of the tablet still slow down? | The start's `rhi_vk: queue priority: the driver's default (medium), not asked: ...; the game asks no device for a global priority (...)` and the offer line's `the game's one queue made at priority 1.0 with no global priority asked (the driver's default, medium)`; the attempt ledger's `Qualcomm queue at high priority removed (...)` at the start and, in each block, `Qualcomm passes as VkRenderPass objects on N passes (S stalls in the block)` with the clause `; stalls S in the block (the Adreno attempt on)`; the tally's `H of them hangs` summed over the blocks against the 15 hangs (waits of 382-412 ms) with the priority granted (each with `list 0's end` first unwritten in the `rd: GPU clock:` lines) |
| Retroid Pocket 6 (Adreno 740, driver 512.676.53, Android 13): the run ends at every start with no crash line, loss or watchdog end, its GPU reset count rising (47 to 66 across two runs) | The high priority is gone: does the device still end the run at start, and how many GPU resets come at start and in the run? | The start's `rhi_vk: queue priority: the driver's default (medium), not asked: ...` (the priority not asked) and the attempt ledger's `Qualcomm queue at high priority removed (...)`; then whether the log reaches play and its blocks (`window: attempts: ...`), or ends at the start-up set or the first ticks as before; the GPU reset count at the start and in each stall's line where the kernel driver is readable, against 47 and 66 in the two runs with the priority granted |
| Pixel under Uncapped at 120 Hz: motion sped up and slowed (pictures shown a mix of one and two refreshes apart) | Which moment is drawn for each picture, and how many land on the refresh predicted for them? | The attempt ledger's `in-between pictures for the screen's refresh on (...) N pictures placed` and `in-between pictures on their own timer on (Android: ...)`; the `on the glass` line's `drawn (the screen's predicted refresh)` measure (its deviation and its share above 1.5) and its clause's `the refresh predicted against the one shown over S pictures: H on it (P %)` with the reasons a picture was `drawn as otherwise` ([PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#the-on-glass-line)) |
| Adreno 740: three skin-check `wrong` verdicts right after 400 ms stalls with the GPU's own sign of a reset, each reading the readback's bytes from an earlier check (`not written again`); the first started the run's capture (44 ms on the game thread, about 1 s on the worker), and more stalls followed | Do the stall clusters remain without the captures? | The attempt ledger's row `no capture after a stall on (a skin check read after a GPU reset is skipped, not captured) N checks skipped after a reset` in the block that holds the stalls, the ledger line's `; stalls N in the block`, each skipped check's `rd: VU skin check: frame F command L:I ...: skipped after a reset: WHY; ...` and the 10 s `rd: VU skin check: P checks, W wrong, K skipped after a reset, ...`; no `VU skin check: captured` or `window: skin-check capture:` line after a stall; then whether `window: stall:` lines still come in clusters a few replays apart ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#skipped-after-a-reset)). Every stall of a block has its line (up to 350 a block; the tally says how many were left out past it), so a cluster's members are each named with their replay and `(a hang)` or `(not a hang: ...)`, and the tally's `H of them hangs` per block shows whether the hangs themselves come in clusters or only the presents after them |
| Adreno 740: every hang's first unwritten stamp is `list 0's end`, the present's own list's stamps written after it | Which submission hangs, and was the present waiting on it? | Each `window: stall:` line's clause `the submissions around it (from the stall's start): replay R's K of N (lists 0-12, P pieces; S stamps, U unwritten; it holds the first unwritten stamp) submitted at -T ms, signalled at or before +T ms ...; replay R's 2 of 2 (the present's list, ...; S stamps, all written) ...; replay R+1's 2 of 2 (1 list, the stalled present waits on it) ...; in flight at the stall's start: S seen, U not known`: the submission index that holds the first unwritten stamp across the hangs (always 1 of 2, the scene's, or not), whether the present's list after it ran (its stamps written), and how long before the stall each was submitted. The earlier submissions' own signals stay unknown (no Vulkan call reads them; the slot's last one's is its wait's end) ([Which submission hangs](ANDROID_DIAGNOSTICS.md#issue-123-the-hang-detector-and-the-pass-path)) |
| Adreno 740 tablet at 3x: shadow cost above 1x, list 3 8.6-14.4 ms against 3.4-4.1 the release before (the full-size count pass up to 10.69 ms) | Is list 3 back near 3.4-4.1 ms? | `rd: shadow count: above 1x <form>, timed ...` (the three medians); the 10 s shadow clause's `the count N from the stencil, F full-size passes, ...`; list 3 in `window: GPU ms per replay: lists 0-12` ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-count-from-the-stencil)) |
| Pixel 10 Pro Fold under Uncapped in the cage: two 10 s blocks at exactly one picture a tick, the render thread's CPU and the GPU's work a picture up by half to double, the cores 2-6 clock down, the hint over target either way; the second release candidate's workload notices taken and nothing moved | Do stretches at one picture a tick remain with the threads unpinned? | The attempt ledger's `pins let go at one picture a tick on (...) N times let go (M taken again)`; `window: pins let go: ...` and `window: pins taken again: ...`; `window: pin trace (let go, second k): ...` (pictures a tick, the cores the render and game threads ran on, those cores' clocks, the cores each may run on) against the `pin trace (pinned, at one a tick)` seconds; `window: worst second of the block: ...; the render thread's cores: ...; pins let go ...` (seconds stuck pinned against stuck let go in each block); the ranked line's clock clause ([One picture a tick](ANDROID_DIAGNOSTICS.md#one-picture-a-tick-and-the-worst-second-the-owners-pixel)) |
| The owner's "slow down on movement of objects like the cage": draws left at their tick's position were 0.1-0.5 % of keyed draws, unnamed; the first release candidate's stair-steppers (`chain4`, `pot`, `taimatsu1`) were still objects the test misjudged | Which objects are drawn at their tick's position, and does any really move every other tick? | `rd: interp names: draws left at their tick's position over 250 frames (...; counted at each frame's first present) ...`: the top five names of each reason and the stair-steppers with their cadence `[moved M of J, alternating A]` (A near M near half of J: a real every-other-tick object) ([by name](ANDROID_DIAGNOSTICS.md#the-draws-left-at-their-ticks-position-by-name)) |
| Retroid Pocket 6 (issue 215): every run ends with no crash line, device loss or watchdog end | Who ended the run? | The start's `ico_pc: the previous run ended: <how> (<Android's part>); it was at <phase> ...` (Android's own exit record matched by the pid in `last-run.txt`, else our own end mark or the files alone), `ico_pc: Android's exit record <i> of <n>: ...` and `exit-info.txt` in the report ([PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#how-the-previous-run-ended)) |
| Mali-G52 and the Adreno 740 tablet: lines judged against the wrong interval, stalls of no class, the right wall | Their questions | [The issue ledger, continued](PLATFORM_DIAGNOSTICS.md#the-issue-ledger-continued) |
| Adreno 740: 10 hangs in 22 s with the pause menu open, each in a scene replay's list 0 (the reporter's last log) | With the scene held, do the hangs stop (they need scene work) or continue on submissions of lists 11-12 alone (the cause is outside the picture's content)? | Each `window: stall:` line's submissions `menu open, scene held` or `menu open, scene replayed` with the one that `holds the first unwritten stamp`; `window: still scene: N pictures from the held scene ...` per block; the tally's `H of them hangs`; the ledger's `the paused scene held under the menu on (while paused) N pictures held (M drawn in full while paused)` ([RENDER.md](RENDER.md#the-paused-scenes-hold)) |
| Pixel opening: lamps, dark and sun views | What does each block cost, and which cut fired? | `window: compare: ...; GPU ms a picture: ...; glow plans: lamps N, dark N, whole N`, `passes` and `breaks` a picture, and the ledger line's per-cut fired counts |

### The attempt ledger

`port/platform/attempt_ledger.h`: one row per behaviour that this release changes on purpose for a
device class. Each row has a name, its state on this device (on, off by default, refused by its
self-test, not applicable here, not in this build, removed, or not reported) with a reason, and up to two counts. Two lines
print it:

- At the first 10 s block, and again whenever a state changes (the first 8 changes, then every
  100th): `window: attempts at the start: few-cores rule on (2 cores faster than the slowest);
  pipeline set on every core on (2 cores faster than the slowest); slow-scene capture in two steps
  on (the copy into a block the dump worker made); flat outputs at the provoking corner on (every
  device and backend with the VU pre-pass); one DATE set-up not reported; ...`. The line is
  written in parts that each fit the window's 4 KB buffer (`ico_attempt_start_part`): whole rows
  each, the second part beginning `window: attempts at the start (continued): ...`. With the
  25 rows the longest each row's reason can be (its decider's words, the device's own: the pass
  row's 162 bytes, the subpass probe's 255) made 3927 bytes, one line; with the rows
  added since (the queue priority, no capture after a stall, picture health, the pins let go) a
  device class's line may take a second part (`attempt_ledger_test` holds it to one or two, every row whole).
- In every block: `window: attempts: few-cores rule on 3 threads moved; pipeline set on every core
  on 0 workers (0 sets); slow-scene capture in two steps on 1 slow-scene captures (0 grown here);
  flat outputs at the provoking corner on (static); ...; stalls 3 in the block`. Rows that are not reported are left
  out, and so are the rows not in this build and the removed ones. Where the Adreno row is
  reported, the stalls clause adds `(the Adreno attempt on)` or `off`, and the row's second count
  is the block's stalls (`Qualcomm passes as VkRenderPass objects on 812 passes (2 stalls in the
  block)`).
- A removed row keeps its place so two logs compare row by row: it says `removed` at the start
  with why (`Qualcomm queue at high priority removed (taken off, issues 215 and 123)`) and has no
  count; the block's stalls are counted on the passes' row. The start line is written in 4 KB
  parts of whole rows (`ico_attempt_start_part`), so a longer line takes another part and is never
  cut.

Which device gets which attempt is the device table's ([DEVICE_TABLE.md](DEVICE_TABLE.md)), and
so are a row's words where no row of it holds (`rhi_device_choice_na_words`).

To add a row, a package adds one enum value and one table row with its words, a decider
(`ico_attempt_decide_*`: the row's state and its reason from the facts its owner has, so the unit
test can set every row as a device of each class would) called where it decides, and
`ico_attempt_fire` / `ico_attempt_fire2` where it fires. Each call is a relaxed atomic, so any
thread can make it, hot paths included. The rows and where their states are set:

| Row | State set by | Counted by |
| --- | --- | --- |
| few-cores rule, pipeline set on every core | `render_thread.c` `place_init` (`ico_attempt_decide_cores`: on with one or two faster cores and the pins allowed; not applicable with three or more, cores of one speed, or off Android) | the keeper's and the sweep's moves; the sets' workers |
| slow-scene capture in two steps | `window_perf_rank.c` at the first block (every platform) | `window_dump_auto.c`: the slow-scene dumps alone (the second count those grown on the game thread). F12's dump and the skin check's capture are made another way and not counted (issue 123's log of 2026-11-14 said `0 captures` beside two of those, under the row's earlier name `capture in two steps`) |
| flat outputs at the provoking corner | `rd_vu_flat0.c` at the renderer's start (`ico_attempt_decide_flat0`), and `window_perf_rank.c` every block with the pre-pass as it is then (`ico_attempt_flat0_reach`): `on (every device and backend with the VU pre-pass)`, `off by default (ICO_RD_VU_FLAT0=0: the tests' twins)`, `not applicable here (the VU pre-pass is off here: vu_main_vs, the shader it changes, draws only through the pre-pass's record)` where the pre-pass is off (the Adreno 740 on Qualcomm's own driver by its device table; a start whose self-test or record turned it off), which the row's earlier decider printed as `on` | static |
| one DATE set-up | `rd_date_one.c` at each replay's scan (off by the tests' switch, not applicable under MSAA or the exact blend) | the window, from the record's `dateOneShared` and `dateOneRefused` |
| empty composites skipped | `rd_shadow_box.c` at each replay's scan | the window, from `emptySkipped` |
| no-volume break | `rd_no_volume.c` at each replay's scan: `on (the PowerVR D-Series DXT-48-1536)` on the device two phone release candidates showed clean with it, `off by default (shown clean only on the PowerVR D-Series DXT-48-1536's title and pause menu; not tried on this device)` on the rest of the PowerVR and MoltenVK class, `not applicable here (not the PowerVR or MoltenVK class under Vulkan)` elsewhere, or the other gates' reason (the DATE path, MSAA ...) with the class allowed | the window, from `noVolumeBreak` |
| Qualcomm passes as VkRenderPass objects | `window_perf_rank.c` every block (`ico_attempt_decide_pass_objects`: on with `RhiLimits.passObjectsRow`'s reason while Qualcomm's row holds the device's passes, `not applicable here (not Qualcomm's own driver)` otherwise) | the block's growth of `RhiStats.passObjects`, only while the row holds; second the block's stalls (`ico_attempt_block_text`'s, `window_stall_tally_take`'s count), whatever the state |
| Qualcomm queue at high priority | nowhere: `removed` from the start (`attempt_ledger.c` `startState`, its words `ICO_ATT_QPRIO_REMOVED_WHY`); the high global priority it reported on Qualcomm's own driver under Android was taken off (issues 215 and 123: it did not change the hangs) | none (its stall count is the passes row's second) |
| subpass read | `rd_subpass_probe.c` at DATE's self-test (`ico_attempt_decide_subpass`): `off by default` on a class the probe does not run on this release (`off on this driver this release: ...` on the Arm and Qualcomm drivers, `probed on the Intel driver row only this release` elsewhere), `on` with the device's reason (kept by an earlier probe, on for this start's probe, `[dev] subpass_read = on`), `refused by its self-test` (DATE's self-test on the read failed, or this start's or an earlier start's probe refused it), `not applicable here` off the route (`not a VkRenderPass path without local read ...`) or where it cannot be decided (no GPU timestamps, no verdict file, not probed at this start) | the window, from the record's `dateStencilTakes` while the row is on |
| blend constant | `not in this build` from the start (left out of the block line; measured: no draw of the Mali reporter's pictures to convert, [PIPELINES.md](PIPELINES.md)) | none |
| index compaction | `rd_vu_compact.c` at the pre-pass's start and after the record's self-test (`ico_attempt_decide_compaction`): `off by default (off by default on the Mali-G52: it blacked out the picture there ... (issue 179; RD_VU_COMPACT_ARM_DEFAULT 0))` on the Mali-G52; `on` on the compute record of the PowerVR D-Series DXT-48-1536 (or a Mali-G52 under `ICO_RD_VU_COMPACT=1`) where the class's define allows and the self-test passed (its pictures equal, at the play record's size with the lists from the fourth plane's middle, and no stamp of its own pictures unwritten) (`the Mali-G52, its start-up self-test passed`, `the PowerVR D-Series DXT-48-1536, ...`; `ICO_RD_VU_COMPACT=1: the tests' switch` without a self-test), `refused by its self-test` when the record's test failed with it and passed without it, `off by default` on every other device (`measured on the Mali-G52 only ...`), by the define, before the self-test or where none ran (`its start-up self-test not run (yet)`) or by `ICO_RD_VU_COMPACT=0`, `not applicable here` with the record path that holds (`record path stores: vertex shader stores`, `record path targets: render targets`, `the VU pre-pass is off`) | the triangles left out (drop and outside, every list), from the counts read back when the frame slot comes round |
| VU record by compute on the PowerVR | `rd_vu_record.c` at the record's start (`ico_attempt_decide_vu_compute`): `on (the PowerVR D-Series DXT-48-1536: compute dispatches in place of the vertex stores)` on the one PowerVR it ran on, `off by default (not the PowerVR D-Series DXT-48-1536 it was shown on: the vertex stores (its self-test judges the record's bytes, not a device lost on a driver not tried))` on the rest of the PowerVR class under Vulkan with both stores, `on` by the tests' switch, `refused by its self-test` when the compute record failed it (the vertex stores then), `not applicable here` elsewhere (no vertex stores, no compute stores, another class, a `[dev] vu_record` by name) | `rd_vu_record.c` `rd__replay_dispatch`: the dispatches |
| turn in the present's blit | `rd_present.c` at each present (`ico_attempt_decide_turn_fold`): `not applicable here` without a pre-rotated swapchain, `off by default` until the define is flipped or with its self-test not run, `refused by its self-test`, `on` with it passed | `rd_turn_fold.c`: the presents folded (the second count: those on a turned swapchain that kept the turn pass) |
| VU record sized from need | `rd_vu_rec_early.c` at the start-up preparation (`ico_attempt_decide_rec_need`: on where the run's need made a smaller record than the memory bound allowed; not applicable with the pre-pass off, `the record already held the run's need at start-up: nothing to make` (the owner's Pixel: 1024 rows held, its largest; the first release candidate said "under the free-memory floor ..." there with 774 MB above it), nothing made for the memory (`the free memory under its floor or the system's low-memory signal, or no larger record within the memory bound`), or a bound no larger than the need) | the record's growths in play (`rd_replay_vu.c` recEnsure, a record that existed grown) |
| in-between pictures for the GPU's predicted end | `rd_pace_gpu_live.c` on the poster's thread at each change (`ico_attempt_decide_pace_gpu`): `not applicable here (the pictures are paced here (vsync on, a frame rate cap, the Original frame rate or the render thread off): ...)` while the window does not present pictures as made, `off by default (the owner's phone felt worse with it in ...; kept for measurement)` once a picture's GPU span and first submit came back (`on (ICO_RD_PACE_GPU=1: ...)` with the tests' switch), `not applicable here (no GPU span or slot clock on this backend ...)` after 600 measurements back without one (OpenGL, Direct3D 11, timestamps off: the hand-over less the mean drain kept); the start line `rd: in-between pictures for the GPU's predicted end: off by default (...); the GPU per picture the median of the last 15 measured spans, ...; computed and reported, not drawn (ICO_RD_PACE_GPU=1 draws it)` at each change (the first 8, then every 100th) | `rd_pace_gpu_live.c` `rd_pace_gpu_shift`: the pictures the model placed, drawn or not (the second count: those held at its bound); the evenness line's last clause has the rest ([PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#the-evenness-line)) |
| in-between pictures on their own timer | `rd_pace_rules.c` on the poster's thread at each change (`ico_attempt_decide_as_made_timer`): `not applicable here (the pictures are paced here: ...)` while paced, `on (Android: each picture's moment from its own timer)` as made on Android (drawn where the screen's rule does not place the picture), `off by default (not Android: the hand-over rule kept, the timer measured on a phone only; computed and reported)` as made elsewhere (`ICO_RD_AS_MADE_TIMER=0|1` over both); the start line `rd: in-between pictures on their own timer: on (...); each picture's moment the one before plus the mean hand-over interval ...` | `rd_pace_rules.c`: the pictures it gave a moment, drawn or not (the second count: those held inside their frame's tick); the on-glass line has its speed ([PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#the-on-glass-line)) |
| in-between pictures for the screen's refresh | `rd_pace_screen_live.c` on the poster's thread at each change (`ico_attempt_decide_pace_screen`): `not applicable here (the pictures are paced here: ...)` while paced, `not applicable here (no GPU end to predict from on this backend ...)` where the GPU queue's model has no slot clock, `not applicable here (the display gives no times back (no VK_GOOGLE_display_timing: desktop, OpenGL, Direct3D): ...)` after 600 pictures as made without a display time, `on (Android: each picture drawn for the refresh the screen is predicted to show it at (measured on Android phones only))` once both came back on Android, `off by default (not Android: measured on Android phones only; computed and reported on the on-glass line as a what-if)` on any other build whose display gives its times back (`ICO_RD_PACE_SCREEN=0|1` over both); the start line `rd: in-between pictures for the screen's refresh: on (...); each picture drawn for the time the screen will show it: ...` at each change (the first 8, then every 100th) | `rd_pace_screen_live.c` `rd_pace_screen_step`: the pictures it placed, drawn or not (the second count: those held at their frame pair's tick's edges); the on-glass line's clause has the rest ([PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#the-on-glass-line)) |
| an even number of refreshes a picture | `rd_pace_even_live.c` on the poster's thread at each change (`ico_attempt_decide_pace_even`): `not applicable here (the pictures are paced here: the present clock and the display)` while paced, `not applicable here (no GPU end to aim from on this backend ...)` without a slot clock, `not applicable here (the display gives no times back ...)` after 600 pictures as made without a display time, `off by default (three phone rcs did not land pictures on their refresh)` once both came back (`on (every picture shown for the same whole number of refreshes, the number from the measured picture time)` with `ICO_RD_PACE_EVEN=1`); the start line `rd: an even number of refreshes a picture: off by default (three phone rcs did not land pictures on their refresh); not used: the pictures as made (ICO_RD_PACE_EVEN=1 turns it on for the tests)` (with it on, the rule in full: `...; with pictures as made, every picture shown for the same N refreshes: ...`) at each change (the first 8, then every 100th) | `rd_pace_even_live.c` `rd_pace_even_take_live`: the pictures aimed at a refresh (the second count: those late for it at the hand-over); the on-glass line's clause has the rest |
| the glow's paste by its own entry | `rd_paste_spec.c` at start-up and after its self-test (`ico_attempt_decide_paste_spec`): `not applicable here` where `fx_paste_ps` is not made (OpenGL, Direct3D), `off by default` with `ICO_RD_PASTE_SPEC=0`, the define at 0 or its self-test not run yet, `refused by its self-test` where the two entries' bytes differed on the device, `on` with it passed | `rd_paste_spec.c` at each premultiplied add: the pastes drawn by `fx_paste_ps` (the second count: those that kept `fx_frame_ps`, the 10 s `window: paste:` line says why) |
| the glow chain in three passes | `not in this build` from the start (measured, not built: [RENDER_POST.md](RENDER_POST.md#the-glow-chain-in-three-passes-measured-not-built)) | none |
| the shadow volumes by the stencil-only entry | `rd_volume_fs.c` at start-up and after its self-test (`ico_attempt_decide_volume_fs`): `not applicable here` off Vulkan, `off by default` with `ICO_RD_VOLUME_FS=0`, the define at 0 or its self-test not run yet, `refused by its self-test` where the volumes' count by `date_clear_ps` differed from `sprite_ps`'s on the device, `on` with it passed | `rd_volume_fs.c` at each volume draw: the volume draws by `date_clear_ps` (the second count: those that kept `sprite_ps` where it is offered: a multisampled pass, the self-test, the pipeline) |
| the shadow composites by their own entry | `rd_comp_spec.c` at start-up and after its self-test (`ico_attempt_decide_comp_spec`): `not applicable here` off Vulkan or without the dual-source fixed-function blend (the two-pass or the exact blend), `off by default` with `ICO_RD_COMP_SPEC=0`, the define at 0 or its self-test not run yet, `refused by its self-test` where SCENE's bytes differed between `sprite_ps` and `sprite_comp_ps` on the device, `on` with it passed | `rd_comp_spec.c` at each composite and level drawn: the draws by `sprite_comp_ps` (the second count: those of their forms that kept `sprite_ps` where it is offered; the 10 s `window: composites:` line says why) |
| the two-pass blend in one pass | `rd_onepass.c` after its self-test (`ico_attempt_decide_onepass`): `on` passed, `refused by its self-test`, `not applicable here` with dual-source blending, `off by default` with `ICO_RD_ONEPASS=0` or not run yet | pairs in one pass (second: kept two-pass; the 10 s `window: two-pass blend:` line gives the reasons) |
| no capture after a stall | `rd_vu_skin_selftest.c` at the skin check's start line (`ico_attempt_decide_skin_skip`): `on (a skin check read after a GPU reset is skipped, not captured)` where the skin check runs (an Adreno with a window, or the tests' switch), `not applicable here (the skin check is off here)` where it is off or its start-up probe failed; not reported where the probe never ran (not Vulkan) | `rd_vu_skin_check.c` at each check skipped after a reset (`rd__skin_skip`: the readback not written again, or read in the span of the GPU's own sign of a reset) ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#skipped-after-a-reset)) |
| picture health | `rd_health_live.c` at the first collected record (`ico_attempt_decide_picture_health`: `on (counting: 16 of 32 3D pictures unfinished are said once)` on Vulkan, `not applicable here (only Vulkan's GPU stamps say a picture came back unfinished)` elsewhere); a report, nothing switched ([The picture-health report](ANDROID_DIAGNOSTICS.md#the-picture-health-report-issue-179)) | the judged 3D pictures with stamps unwritten (the second count: the GPU clock restarts with work queued) |
| pins let go at one picture a tick | `window_clock_trap.c` every block (`ico_attempt_decide_pin_release`): `on (a second at one picture a tick with a picture's CPU raised lets the render and game threads run on any faster core until 10 s pass without one)` under the keeper's rule for three or more faster cores, `not applicable here` with `Android's core placement only`, `one or two cores faster than the slowest: the few-cores rule keeps its pins` or `nothing pinned here (...)` ([One picture a tick](ANDROID_DIAGNOSTICS.md#one-picture-a-tick-and-the-worst-second-the-owners-pixel)) | `window_clock_trap.c`: the times let go (the second count: taken again) |
| the shadow levels' count from the stencil | `rd_shadow_count.c` at start (`ico_attempt_decide_shadow_count`: `on`, or `not applicable here` without a stencil view) and at the trial's choice (`refused` with the form timed cheaper) ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-count-from-the-stencil)) | `rd_shadowres.c`: steps from the stencil (second: full-size passes drawn after all) |
| the camera rule for static meshes on the plain set | `rd_pipeline.c` `rd__vu_policy_init` at each device start (`ico_attempt_decide_plain_camera`): `on (static meshes and grids by the camera, skinned draws keep the PS2's drops (issue 163))` on a device that keeps the VU plain set (the Adreno 740 on Qualcomm's driver, Turnip) under the camera rule, `not applicable here ([dev] region_rule = ps2: ...)` there under the PS2's rule, `not applicable here (this device draws with the extended set ...)` elsewhere | `rd_plain_camera.c` `rd__plain_camera_note` from `rd_replay_vu.c` `doVuDraw`: the mesh and grid draws by the camera (the second count: the skinned draws kept); [PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#issue-123s-right-wall-the-camera-rule-on-the-plain-set) |
| the paused scene held under the menu | `rd_hold.c` at the first paused picture and when the device's verdict changes (`ico_attempt_decide_scene_hold`): `on (while paused)` on Vulkan where list 11's draws are not joined to the open pass, `not applicable here` with the reason (not Vulkan, the join, MSAA, the exact blend, a temporal upscaler, ...); `refused by its self-test` when the first held picture differed from the same picture in full ([RENDER.md](RENDER.md#the-paused-scenes-hold)) | the pictures held; second count those drawn in full while paused |
| the old robust-access line taken off | `window_robust.c` `window_robust_start` at the window's first start (`ico_attempt_decide_robust_line`): `on ([dev] gpu_robust = true without the game's mark removed from config.toml: robust access off from this start)`, `refused by its self-test (config.toml could not be written: robust access stays on, the next start tries again)` (the refusal is the file's), `refused by its self-test (gpu_robust = true in config.toml is in a hand-written form the game cannot take out ...: delete the line by hand)` (the text without the lines the removal knows, `[dev]`'s two keys and the root's dotted `dev.gpu_robust` / `dev.gpu_robust_by`, parsed as config.toml is read, still says true: nothing is written and the start says `ico_pc: robust access stays on: gpu_robust = true in config.toml is written in a form the game does not write and could not take out ...`; a marked key's too), `not applicable here (no [dev] gpu_robust = true without the game's mark in config.toml)`; a later start of the window in the same run changes it only when it finds such a key | static |

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
file on the game or render thread for it. Every stall gets its line while stalls are rare: a
block's first 16 (`STALL_RARE_LINES`, four times issue 123's busiest block) while the run has
written fewer than 500 such lines (`STALL_RUN_LINES`; that tablet's 13 stalls in 325 s are 144 an
hour). Past that, the older rule over those stalls: the first 8, then every 100th. A block with
lines left out ends its tally `; N stall lines left out (W written: ...)`; the tally counts every
stall. The old rule alone left issue 123's stalls 9-13 of its log of 2026-11-14 in its tallies:

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
  its fastest (where readable), the GPU's own sign of a reset, one piece grew, the GPU slow as a
  whole ([the pieces](PLATFORM_DIAGNOSTICS.md#issue-179s-judging-lines)), or none of these. Each block,
  beside the attempt ledger: `window: stalls by what coincided: 3 in the block, 0 of them hangs
  (the GPU's own sign of a reset with a stamp unwritten) and 3 other; all 3 by class: a core wait
  over 100 ms 1, memory under the floor 2, memory pressure rising 1, the GPU clock low 0 (not
  readable), the GPU's own sign of a reset 0, one piece grew 0, the GPU slow as a whole 0, none of
  these 1`. The hangs are the stalls whose line
  ends its sign with `(a hang)`: as many as the block's `rd: GPU clock:` stall lines with stamps
  `came back unwritten` (13 in issue 123's log of 2026-11-14, against its tallies' 15 stalls).
- **The GPU's own sign of a reset** (`rd_perf_clock.h` `rd_perf_reset_sign`, one test for these
  lines and the skin check's span): the stalled replay, or one of the two before it, had recorded
  stamps come back unwritten after its work was signalled finished (a hang), ran its first list
  twice, or saw the GPU's timestamp counter go back while the GPU was kept busy. The line names
  the hung replay and the restart apart: `the GPU's own sign of a reset at replay 2074: 20 stamps
  came back unwritten after its work was signalled finished (the first: list 0's end), and at
  replay 2075: the GPU's clock restarted (a hang)`, or without a stamp unwritten in the three
  replays `... at replay 10911: the GPU's clock restarted (not a hang: no stamp unwritten in the
  stalled replay or the two before it)`.
- **A counter restart after a span with no GPU work is not a sign.** The Adreno's counter
  restarts whenever the GPU idles (seven times at issue 123's start-up, and after the reporter's
  own capture: the 155 ms present at its log's :2736 (2026-11-14) counted as a reset). A restart is
  judged idle when the replay's first submit came after the replay before had ended (its slot's
  wait's end, or the wait's start where it had finished before), or, where the times cannot say,
  when the window noted a span the game was not presenting since the replay before (the Frame rate
  counter's notes, `ui_fps_gap`: the app back in the foreground, a pause of the host such as a
  swapchain made again, a capture written; `rd.h` `rd_perf_idle_note`). A replay queued while the
  one before still ran is busy whatever the notes say. Such a restart is said and not counted:
  `the GPU's clock restarted at replay 10621 after 1150.0 ms with no GPU work: not a sign of a
  reset` (or `after a span the game was not presenting (a pause, a capture, the app away)`), and
  its `rd: GPU clock: ... (restart line N of the run)` line ends `; submitted N ms after the replay
  before had ended: the GPU idle, not a sign of a reset`.
- **The submissions around it** (`rhi.h` `RhiTimestampClock.sub`, the records' `gpuSub`; see
  [Which submission hangs](ANDROID_DIAGNOSTICS.md#issue-123-the-hang-detector-and-the-pass-path)): for the stalled
  replay and the two before it, each submission's place in its replay, its lists (`lists 0-12` and
  the pieces timed in it, or `the present's list`, where its replay was a stall; else its count of
  lists), whether a present waits on it (`the stalled present waits on it` for the stalled
  present's own), its stamps and those unwritten, which one holds the first unwritten stamp, when
  it was submitted and when its signal was seen (`signalled at +T ms (the slot's wait's end)`,
  `signalled at or before +T ms (finished when the slot's wait began)`, or for an earlier
  submission of its replay `signalled at or before +T ms (its replay's last's; not seen apart)`),
  all from the stall's start; then `in flight at the stall's start: S seen, U not known`.

### The present latency line

`port/platform/present_latency.h`, every block after the present stalls' line, on every platform
(issue 123):

`window: present latency in the block: 519 presents (519 from the hand-over), mean 34.6 ms,
longest 365.2 ms (replay 2075); of the mean: before the present 18.0 ms (the slot's wait for the
GPU 15.6 ms, the acquire 0.0 ms), the present 16.6 ms, the driver's present call 15.4 ms of it;
over 518 presents with the picture's GPU end seen, the call 12.0 ms while the picture's GPU work
still ran and 3.4 ms after its end (the display's)`

From the picture's hand-over to the render thread (the replay's start where it was not handed
over: `N from the replay's start`) to the present's end. The part before the present holds the
replay's CPU work, its frame slot's wait for the GPU and the acquire of the window's image. The
driver's present call is split at the picture's own GPU end, where its slot's wait blocked and saw
it end: the part while that work still ran is at most a wait for the GPU, the rest is the display's
(the compositor's queue, the refresh). Where no wait blocked, `the call not split`. Its cost: a few
adds a presented record on the window's thread. The line has no clause on the queue's global
priority: every queue is made at the driver's default (issues 215 and 123).

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

On Android, the log thread reads `/proc/self/status`'s `RssAnon`, `RssFile`, `RssShmem` and
`VmSwap` and the C heap's `mallinfo` once a second, and keeps the block's extremes. The window
prints them after the memory line, and the next block starts from the next read:

`window: memory in the block: the device's least 501 MB available against the floor 641 MB (under
it at 8 of 10 reads); the process 538-1067 MB resident: anonymous 410-520 MB, file-backed 120-540 MB,
shared 5-5 MB (/proc/self/status, once a second); swapped out 0-310 MB (VmSwap: still allocated);
the C heap 380 MB in use and 60 MB mapped (at the last read); beside them the graphics allocations
335 MB, the dumps' capture blocks 0.0 MB, the loss dump's frame copies 13.0 MB`

The numbers in this example are made up. The line's values pass through a last-value cell
(`sys_cell.h`) that once held 6 of its 14, which cut them, so the window never printed it; the
cell holds 16 now. How to read a swing:
- **Swapped out rising as anonymous falls:** the kernel moved the process's pages to swap (zram
  on a phone) under memory pressure. The memory is still the process's; nothing was freed. Touching
  those pages again (a stage load, a cutscene's data) reads them back, and the GPU's work can wait
  for it.
- **File-backed swinging:** mapped files' pages (the program's own code, the graphics driver's
  libraries) dropped by the system and read back. The game data is read with `fread`, never
  mapped, so its pages are not in the process's figures.
- **Anonymous swinging with swapped out flat:** the process's own allocations. The largest the code
  makes are the frame ring (3 frames, 7-13 MB each on the Mali), the renderer's texture and mesh
  copies (a stage's), the loss dump's frame copies (at most 4 of 16 MiB, held while the GPU is
  behind; the line names them), the slow-scene capture's block (the capture blocks) and, during a
  pipeline batch, the driver's compiler (about 70 MB a thread, given back after the batch).
  Without a texture pack nothing of the port's own moves hundreds of MB in a block.

Every block, on every platform, the VU pre-pass's record follows:

`window: VU record memory: 512 rows held (64 MB over the frame slots; 512 rows made at start-up); a
picture of the block needed at most 214 rows (248 in the run); 0 growths in play in the run; the
upload rings 128 MB`

The rows held as the last replay began and their bytes (every frame slot, and the three render
targets where the record is drawn into them), what the start-up made, the most rows a picture of
the block needed (pass V's and pass T's) and of the run, the record's growths in play, and the two
frame slots' upload rings. A block whose pictures need more than the rows held grows the record
(its own `the record holds` line); a run whose need stays far under the held rows is memory the
record holds for nothing.

Every block where the index compaction ran (the compute record; [PIPELINES.md](PIPELINES.md#the-index-compaction)):

`window: index compaction: triangles left out as drawing nothing, a picture: 9866 of 22169 in list
0 (44.5 %), by class: the game's drops 3370, outside the target or the depth range 6496; the other
lists 1200 of 30000 (4.0 %) (25 pictures read back, two frame slots behind)`

The triangles the keep stage judged a picture (each code 36 triangle twice: its cut and its kick
draw), those it left out by class, list 0 apart, from the counts each workgroup stores and a copy
reads back when the frame slot comes round. The draws' invocation figures (`window: VU: ...`,
`perf scene`, `compare`) count a left-out triangle as one invocation (its filler) and say so:
`3 a triangle, after the index compaction 1 a triangle it left out: A of B`.
The start lines: `rd: VU index compaction: off (off by default on the Mali-G52: it blacked out the picture there ... (issue 179; RD_VU_COMPACT_ARM_DEFAULT 0); the Mali-G52 (Mali-G52 MC2, vendor 0x13B5): RD_VU_COMPACT_ARM_DEFAULT 0)` at the pre-pass's start on the Mali-G52; `rd: VU index compaction: to be tried by the start-up self-test, on where it passes: ...` (on the Pixel `the PowerVR D-Series DXT-48-1536 (...): RD_VU_COMPACT_DEFAULT 1`; on every other device `off (off by default: measured on the Mali-G52 only (and on the PowerVR D-Series DXT-48-1536's compute record); not the Mali-G52 or the PowerVR D-Series DXT-48-1536 (<device name>, vendor 0x....): off by default)`: the class the decision was taken for, by the Vulkan device name, and the define that took it), then `rd: VU index compaction: on: passed its start-up self-test with the record's (...)`, `rd: VU index compaction: refused by its start-up self-test (the record's test with it: <the difference>; without it the test passed): every draw reads its own index list` or, where the self-test returned before it ran, `rd: VU index compaction: off: the start-up self-test did not run (no SCENE target) (its start-up self-test not run (yet); ...)`. Off after the self-test (refused, or not run), the fourth record plane is given up: the record is made again without it.

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
not be asked`. Captures in two steps and fallbacks are the ledger's `slow-scene capture in two steps` counts.

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

Each stall line is 600 bytes to a few KB, and those lines are rate-limited (above). The start adds the ledger's
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
