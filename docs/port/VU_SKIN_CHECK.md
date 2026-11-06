# The skin check

The skinned draws' bone-block check (issue 163): on an Adreno with a window (Qualcomm's driver or
Turnip, issue 163's devices), the renderer draws one skinned command again every 100 ms as a probe
of its own and compares what the GPU computed with the CPU's copy of the same program, so a
reporter's log names the layer that placed a skinned vertex wrong without anything to switch on.
It is designed not to change the picture (`rd_vu_skin_check_ab` replays the dumps with it on for
every skinned command and compares the pictures byte for byte). Off on every other device: issue
179's Mali-G52 is GPU-bound, and the check's GPU work is not spent there.

On Turnip (Mesa's driver on an Adreno, issue 123's and 163's reporters) the check also twins EVERY
skinned draw inside its own pass (the twins, [What it checks](#what-it-checks)), probes one command a present instead of
one every 100 ms, aims that probe at a command whose twins differed, and captures its first failure
of the run by itself (the capture, same section).

Why it exists: issue 163's streak on Mesa's Turnip driver on an Adreno 740 was one draw of Yorda's
mesh in which every vertex weighting bone address 200 was placed as if that bone's matrix (VuBoneCB
qwords 184..187) had read as zeros, while the bytes the CPU wrote were right; a copy of the
reporter's dump with only those weight lanes moved to a zero bone replays on lavapipe to within
3,130 of 6,193,536 pixels of the device's picture (the unpatched replay differs by 45,934). Which
layer zeroed the read, the driver's uniform read, the ring's bytes under the GPU or robust access
alone, a replay cannot tell. The check tells it from the device itself.

Why the twins: in a run of issue 123 (the reporter's newest log) the check ran 394 times with nothing wrong while the
present it shipped lost Yorda's left shin (bone address 200 of the same mesh). The probe sampled one
command every 100 ms in a round robin over about 42 (that draw about 9 times in the run, about 0.6 %
of the presents), drew after the picture through a pipeline of its own (never inside the tiled scene
pass), and the box check counts only samples outside the CPU's box, so a missing part is invisible to
it. The replay of that run's blended dump on lavapipe (the issue's evidence folder)
fits the device's picture with bone 200 read as a non-finite or huge matrix (every triangle on it
dropped: 992 pixels of 2.46 M differ, against 2,492 for the clean replay and 53,262 for a zero bone
200), where issue 163's streak was a zero read of the same 64 bytes: a read of other memory, not a
fixed value. The judgement names both forms ([What it checks](#what-it-checks)).

## Files

| path | what it holds |
| --- | --- |
| `port/render/rd_vu_skin_check.h` | the check's API: the judgement's pure functions, the replay's hooks, the tool's entry |
| `port/render/rd_vu_skin_check.c` | the in-game check (S2) and the box check (S3): the pick, the note, the probes, the harvest, the lines |
| `port/render/rd_vu_skin_capture.c` | the capture on failure: the sidecar's writer, `rd_set_vu_skin_capture_dir`, `rd_vu_skin_capture_due` |
| `port/platform/window_host.c` (`skin_capture_pump`, `frame_dump`), `window_pipe_finish.c` (`window_pipe_skin_check`), `share_report.c` | the capture's frame dump for the next present with the sidecar in its zip; the sidecar's folder (the logs folder); the share menu's file list |
| `port/render/rd_vu_skin_judge.c` | the judgement (pure): the CPU's positions through `vu1_ref`'s cluster program, a probe against them, the zero-matrix address, the byte windows, the decision table's words |
| `port/render/rd_vu_skin_selftest.c` | the start-up probe (S1) and the check's start line; `rd_vu_skin_probe_failed` |
| `port/platform/window_host.c` (`skin_probe_after`, `skin_check_persist_off`), `startup_retry.c` (`startup_retry_note_has_skin_probe`), `diag_host.c` | an end or a device failure inside the start-up probe: `[dev] vu_skin_check = false` |
| `port/render/rd_replay_vu.c` (`doVuDraw`, `vuDraw`) | the pick before a skinned command's draw (and the twins' arm, `g_rdSkinTwin`), the note after it; an armed draw is kept out of the draw merge |
| `port/render/rd_replay.c` (`rd__wait_frame`, `replayFrame`) | the harvest once the slot's fence let the replay go; the probes after the replay's last pass |
| `port/render/rd_profile.c` (`rd__profile_adreno`, `rd__profile_vendor_adreno`) | the device an Adreno: the default's one question |
| `port/rhi/vk/vk_pipeline.c`, `vk_device.c` | `RhiPipelineDesc.uniformRobustOff`: `VK_EXT_pipeline_robustness`'s uniform-buffer robustness off on one pipeline, enabled where robust access is on, the device offers it and the check can run (vendor 0x5143, or `ICO_RD_VU_SKIN_CHECK` on: any other device is made as before) (`RhiLimits.uniformRobustOff`) |

## What it checks

**S2, the in-game check.** At most every 100 ms (wall clock), one skinned command of the replay in a
round robin over the replay's skinned commands (Yorda's 26 draws and Ico's all come round within a
few seconds). Before its draw (`rd__vu_skin_pick`) the CPU's side is kept: VU memory 0..35 from the
payload and the stream's vertices of the draw's batches (at most 1,024) from the mesh. After its
draw, inside its pass (`rd__vu_skin_note`): the 3,840 bytes the ring holds at the draw's VuBoneCB
offset (what `cbCopy` wrote), the draw's VuCB with `vu_draw.x` set, its group 1 and its DrawCB
offset; the CPU's positions of the vertices through `vu1_ref`'s cluster program
(`rd__skin_reference`) from those bytes. After the replay's last pass (`rd__vu_skin_end`), one pass
on the check's own RGBA8_UINT target (48 x 1,024 texels) draws three probes of the command side by
side, each through the probe layout of `vu_common.hlsli` (`vu_probe_out`: a one-pixel triangle a
field, here fields 0..2, GS X, Y and Z, through an index list of those fields only; column 16 c,
row the vertex less the draw's first, by FrameCB's exact-viewport form):

| column | the probe | what it isolates |
| --- | --- | --- |
| 0 | the draw's VuCB with `ICO_VU_PROBE`, its group 1 and the SAME VuBoneCB offset, through the vertex module the draw's key took (the program entry's, the plain set's where the fallback takes it) | the draw's own read |
| 1, c1 | the same with the CPU's bytes copied to a fresh ring offset of another 4 KB page phase | the address and the page phase |
| 2, c2 | the same as column 0 through a pipeline with uniform-buffer robustness off (`VK_EXT_pipeline_robustness`; made in the background, drawn where robust access is on and the device offers it) | robust access alone |

Where the vertex pre-pass is on (a desktop device under `[dev] vu_skin_check = true`; it is off on
the tile-based GPUs, so on an Adreno the check's own default never meets it), the draw itself goes
through `vu_main_vs` with the pre-pass's record, while the probe goes through the program's own
cluster entry: there S2 checks the program entry's read of the same bytes, and S3 (below) the
real draw's placement. The probe pass is not in the pass ring (`rd__passring_*`): a device loss
during it names the scene's last pass, not the probe's.

Then the target and the ring's 3,840 bytes at the draw's offset are copied into the slot's readback
buffer. When the fence has let the slot go (`rd__vu_skin_harvest`, in `rd__wait_frame` of the
replay that is about to take the slot's ring again, before it writes it: never a stall), three
comparisons: (a) the GPU's copy of the block against the bytes kept; (b) the ring as the CPU reads
it now against them (a write while the GPU used the ring); (c) each probe's X, Y, Z against the
CPU's, a vertex wrong when X or Y is more than a GS pixel (16/16) off or Z more than 1/1024 of the
CPU's (at least 16). A vertex is compared where the CPU's is in front of the eye and nothing
saturated. For the wrong vertices, each of their weight lanes' addresses is tried as a zero
matrix on the CPU (`rd__skin_judge`): the address whose zero matrix gives the most wrong vertices'
positions is named, and whether it gives the first wrong one's.

**S3, the box check.** The checked command is drawn again inside its own pass right after its draw,
through its own pipeline key with the colour mask 0 and Z writes off, without the destination read
(so GS state as shader data cannot put the mask into the shader: the fixed-function mask writes
nothing), inside an occlusion query (check i's is query i: the pools are made by the start-up probe for 80,
`rhi_occlusion_prepare`, so no frame stalls at the first box check and each slot resets and reads
only those) whose scissors cover the draw's scissor less the CPU's box of
its vertices widened by 2 GS pixels (four bands). Any sample means the GPU placed a vertex where the
CPU does not, through the draw's own vertex module and rasterisation, in the scene pass's own
execution. What it does not cover: on a draw that reads its attachment (GS state as shader data,
the Turnip reporter's profile, or the exact blend), the box is drawn through the plain fragment
entry (`vu_ps`), not the draw's attachment-read link (`vu_ps_dstatt`): with the read, the write
mask is the shader's (`rd__uber_normalise`), so a mask of 0 in the key would still write colour,
and drawing the read with a shader-data mask of 0 would write back the pixel it read inside the
picture's pass, which the A/B cannot clear for every device. So on that profile a clean box clears
the vertex placement, not the decision table's last row (the pipeline's link with its
attachment-read shader); the log says so once (`rd: VU skin check: the box check of a draw that
reads its attachment ... is drawn through vu_ps without that read ...`) and each box line names the
fragment entry drawn. Drawn only where
the box is closed (every vertex of the draw's batches in front of the eye: a clipped corner behind
it reaches anywhere), the pass an ordinary one (no storage pass, no DATE through the stencil bit, no
stencil mode) and its pipeline made (compiled in the background at its key's first check: no
hitch). Its pipelines are told apart by the key as the pass makes it (`rd__pipeline_key_norm`:
the pass's sample count, the dynamic state, as `rd__get_pipeline`'s cache), so a key checked in
an MSAA pass and again in a single-sampled one is two pipelines, never one bound in a pass of
another sample count.

A wrong probe vertex whose X, Y or Z the shader saturated (a NaN takes `0x80000000`, any non-finite
or huge product saturates: `vu_common.hlsli` `vu_ftoi`) is a dropped vertex: the line adds `reads as
non-finite / dropped: N of them (a non-finite matrix at A drops K)`, A being the candidate address
whose NaN matrix, run on the CPU, drops the most of them (`rd__skin_reference_fill`); where no zero
matrix explains the wrong vertices, that address is the one named.

**The twins.** Where they run (`rd__vu_skin_twins_on`: Turnip, `rd__driver_is_turnip`, with the
check on by default; `ICO_RD_VU_SKIN_CHECK=2` and the tool's and tests' mode 2 anywhere), every
skinned draw of a replay (up to 128, `RD_SKIN_TWINS_MAX`) is drawn twice more right after itself,
inside its own pass, nothing written, each inside an occlusion query (twin t's are queries 80 + 3 t
and 80 + 3 t + 1; + 2 is kept for the ring read's twin of the next step; `RD_SKIN_QUERIES` = 464
are prepared at start): A with the draw's own bone offset, B with the same 3,840 bytes copied at
note time to a fresh ring offset of another 4 KB page phase (the ring is host-coherent and
append-only within a replay; the probe's c1 reuses that block). A draw that reads its attachment
(GS state as shader data, the exact blend: the Turnip reporter's profile) with Z writes off is
twinned through its OWN pipeline with a copy of its DrawCB whose write mask (WMASK, `rd_dst_read.c`
`dstPass`) is cleared: every channel is then the value the fragment read
(`gs_math.hlsli` `gs_dst_channel`), UNORM8 k / 255 written back as k (`common.hlsli`
`gs_dst_out_reg`), in rasterization order after the draw's own writes (the attachment read exists
only where `rasterOrderAttachment` holds), so the pixel stays what the draw left; the twins see the
scene pass's binning and the pipeline's link exactly as the draw. Any other draw is twinned through
the box check's pipeline (the key without the destination read, colour mask 0, Z write off,
compiled in the background; until it exists the twin is not drawn and counted as such). No twin in
a storage, DATE, stencil or multisampled pass, nor for a draw through a merge build's table (it
reads its bones through the table, not the b3 offset). Both twins clip alike, so unlike the box
check there is no behind-the-eye gate. Equal bytes give equal counts; when A's and B's differ
(by count where the device's queries are precise, `RhiLimits.occlusionPrecise`, else as zero or
not) the line `twins differ` names the command, both counts and page phases, the fragment entry
and whether it was the draw's own pipeline, and what the probe of the same draw in that replay
said; the command becomes the probe's target for the next 8 replays (whatever the clock and the
schedule; nothing else is probed meanwhile). A non-finite read drops A's triangles, a zero read
moves them: both change A's count and not B's.

**The schedule.** One probe every 100 ms elsewhere; on Turnip one a present
(`ICO_RD_VU_SKIN_INTERVAL_MS` over both; `off` leaves only the targeted probes). The command is
chosen vertex-weighted: a cursor over the last replay's skinned vertices, moved on by the golden
ratio of the total after each probe, picks the command whose vertices hold it, so a mesh is probed
in proportion to its vertices.

**The capture.** The run's first failure (twins that differ, or a wrong check) is captured once: a
sidecar `logs/vu-skin-fail-<frame>-<list>-<index>.bin` (`RdSkinCaptureHeader` in
`rd_vu_skin_check.h`: the command, the twins' counts, the probe's wrong and dropped vertices and
addresses, the decision's words, then the sections: the CPU's 3,840 bone bytes, the GPU's copy, the
ring after the fence, the draw's VuCB, VU memory 0..35, the stream's slice, the probe's rows; about
150 KB), then a frame dump of the next present asked of the window (`rd_vu_skin_capture_due`, polled
beside the bug-report picture's pump: F12's dump, its zip with the sidecar added, the presented
PNG). After a twin event whose command was not probed in that replay the capture waits for the
targeted probe's harvest (its rows and the GPU's copy), at most 8 replays and the slots in flight,
then takes the twin's data alone. The share menu of an rc build lists the newest sidecar too.
Nothing is asked of the player.

**S1, the start-up probe.** An early warning, not the decider. A VuBoneCB of 60 distinct bones (each
a turn, a scale, a shear and a move of its own) and 120 vertices whose weight qwords name every
address 16..252 in both lanes (lane x the vertex mod 60, lane z (7 v + 13) mod 60) with mixed
weights, drawn in probe mode through the cluster entries of codes 20, 22 and 24 (the plain set's
where the fallback takes it) with the block at 64 ring offsets, one in each 64-byte phase of a 4 KB
page (a coarser uniform alignment: its own phases), under the device's robust access, in a frame of
its own read back at once; each of the 192 probes against `vu1_ref`. It runs first in
`rd_vuprepass_selftest`, whatever the pre-pass, and writes the check's start line. A wrong probe
turns nothing off. It is inside the start-up ladders as the pre-pass's self-test is: its notes
(`creating pipeline rd vu skin probe (...)` while its pipelines compile, `the VU skin check's
start-up probe (rd vu skin probe): ...` while it draws and reads back) go to the crash report and
the activity file, so a crash, a freeze, the system's end or a device loss inside it makes the next
start write `[dev] vu_skin_check = false` and run without the check; a probe in which the device
was lost or the picture not read back turns the check off for the run and the window writes the
same setting at once ([PLATFORM.md](PLATFORM.md), `rd_vu_skin_probe_failed`).

**Its own compiles** (c2's three pipelines, a box check's for each new key) follow issue 161's
rule for background compiles (`rd_vu_skin_background`, set by the window from
`port/platform/window_pipe_finish.c` (`window_pipe_skin_check`)): one job at a time, each begun
only when `ico_diag_background_memory` says neither the system's low-memory signal (none for the
rest of the run) nor the free-memory floor now (asked again at the next check), and none at all
where the start-up set stopped for memory (this start's at the floor, or the last start's, which
makes this one set-less) on Qualcomm's own driver. Until a pipeline exists its part is not drawn:
the check runs without its box check or c2, said once (`rd: VU skin check: its box check and its
c2 variant are not compiled in the background (<why>): the check runs without them`, `... no
background compile for the rest of the run (<why>) ...`, `... a background compile waits for
memory (<why>) ...`). The check holds 22 pipelines outside the renderer's cache at most, counted
in the pool's headroom ([RHI.md](RHI.md)); keys past the 16 box slots are not box-checked.

The twins' cost: twice each skinned draw's vertex work and rasterisation inside the scene pass,
no colour written, one 3,840-byte copy and a DrawCB copy in the ring a draw (about 1 MB a slot for
128, counted by `rd__vu_skin_ring_bytes`), 2 x 8 bytes of query results a draw; no CPU reference.
On Turnip the probe once a present adds about 200 KB of readback a present.

The cost: one probe pass of three times the draw's vertices times nine one-pixel triangles, one
3,840-byte copy, a 48 x 1,024 target's copy, every 100 ms; the box check four scissored redraws of
one command with nothing written; on the CPU the cluster program over at most 1,024 vertices, and
on a wrong check one more run of it for each candidate address.

## The decision table

The log's `reads as:` words, for the first wrong check and every event after it:

| the log says | means |
| --- | --- |
| `H2, a port race: the ring's bytes changed after the draw was recorded` | the CPU's re-read differs: the ring was written while the GPU used it (the slot and fence order) |
| `H2, the memory: the GPU read other bytes than the CPU wrote and still holds` | only the GPU's copy differs: the memory as the GPU sees it |
| `H1, address-dependent: the driver's uniform read at that offset` | the bytes equal, the probe wrong, c1 right (c2 wrong or not run) |
| `H1, address-dependent under robust access: right at a fresh offset and right without uniform robustness (either fix: a storage read or robustness off)` | the bytes equal, the probe wrong, c1 and c2 right: the read is wrong only with robust access on at that offset; a storage-buffer read of the bones or robustness off would each do, the second byte-identical by construction |
| `H1 under robust access only: the driver's uniform read with robustness on` | the bytes equal, the probe wrong, c1 wrong, c2 right |
| `H1, the driver's uniform read of this draw: wrong at a fresh offset too, and not robust access alone` | the bytes equal, every probe wrong |
| `H1, ... (robustness off not tried)` | c1 wrong, c2 not drawn (robust access off, or the extension not offered) |
| `the probe drew right: a wrong picture would be the scene pass's execution, not the bone read` | the probe right; a box check's samples outside then say the scene pass placed the vertex (the binning pass, the pipeline's link with its pixel shader). On a draw that reads its attachment the box is drawn without that read, so a clean box there does not clear the link with `vu_ps_dstatt` (S3 above) |

The conditional fixes each row points to (a storage-buffer read of the bones on Turnip, the
uniform robustness off on the VU pipelines, the ring's slot order) are not built until a log says
which row it is.

## The lines

At start, from `rd_vuprepass_selftest`:

- `rd: VU skin probe: ok (120 vertices x 64 offsets in P page phases, codes 20 22 24, plain set) in T ms`
- `rd: VU skin probe: wrong at offset O (page phase P), code C, plain set: N vertices, address A read as zero (got (x, y, z) want (x, y, z)) in T ms`
  (or `..., no zero matrix explains them ...`; `not run (<why>)` when its pipelines or frame could
  not be made)
- with the twins: `rd: VU skin check: on (every skinned draw twinned in its own pass through its own
  pipeline (A the draw's bone offset, B the same bytes at a fresh offset; counted by occlusion query,
  precise | zero or not only); one probe a present, targeted after a twin event, ...; the first
  failure captured (a sidecar and a frame dump); WHY)`
- `rd: VU skin check: on (one skinned draw every 100 ms, in turn, ...; variants: a fresh ring offset, uniform robustness off (VK_EXT_pipeline_robustness) | not offered | not needed (robust access off); the box check by occlusion query | not offered; WHY)`, WHY saying why it is on: `on by default on an Adreno with a window (Qualcomm's driver|Turnip); [dev] vu_skin_check = false turns it off`, or `[dev] vu_skin_check = true (ICO_RD_VU_SKIN_CHECK=1)`
- `rd: VU skin check: off (the start-up probe failed: the device was lost in it | its picture could not be read back)`, then `window: VU skin check: the start-up probe failed on this device (...); [dev] vu_skin_check = false is written to <config> ...`; at the next start after an end inside the probe `window: VU skin check: the previous run ended inside its start-up probe (<note>); ...`
- on a window's Vulkan device with the check off: `rd: VU skin check: off (not an Adreno: ICO_RD_VU_SKIN_CHECK=1 / [dev] vu_skin_check = true turns it on)`, `off ([dev] vu_skin_check = false (ICO_RD_VU_SKIN_CHECK=0))` or `off (the device was lost)`

On an event, the first 8, then every 100th, of each kind on its own count (a wrong check's two lines, a check whose bytes differed with every vertex right, a box check's samples outside), so the many changed blocks of an H2 run never use up a wrong check's lines:

- `rd: VU skin check: frame F command L:I mesh NAME #ID (V vertices): N vertices wrong of M, address A: got (x, y, z) want (x, y, z) (a zero matrix at A gives it: yes|no; it gives K of them); the bone block as the GPU copied it: equal | differs at qwords Q..R; as the CPU reads it after the fence: equal | differs at qwords Q..R | not read (...); code C through MODULE (wrong check E)`
  (X and Y in GS pixels, absolute; Z the GS Z; A the address whose zero matrix gives the most wrong
  vertices, else the first wrong vertex's lane x)
- `rd: VU skin check: frame F command L:I variants: (c1) the block at a fresh ring offset (page phase P, the draw's D): right|wrong (N wrong); (c2) uniform robustness off: right | wrong | not run (robust access off) | not offered (no VK_EXT_pipeline_robustness) | not run (its pipeline not made yet) (N wrong); reads as: WORDS`
- `rd: VU skin check: frame F command L:I mesh ...: every vertex right (M compared); the bone block as the GPU copied it: ...; reads as: WORDS (bytes event E)` (bytes that differed, every vertex right)
- `rd: VU skin check: frame F command L:I mesh NAME #ID (V vertices): twins differ: A (the draw's
  offset, page phase P) N samples, B (fresh, phase Q) M samples[, C (ring read) K samples]; through
  FS with the draw's attachment read (its own pipeline) | with colour and Z writes off; the probe of
  this command: right | wrong at address A | not probed; reads as: WORDS (twin event E)` (WORDS:
  `the draw's bone read at its own offset ...` with a wrong probe, `the scene pass's execution: the
  twins differ inside the pass while the probe of the same bytes drew right outside it` with a right
  one, else `the bone read inside the scene pass differs ...; the probe of this command follows in
  the next replays`); a wrong check's line ends `; the probe targeted after a twin event` when it
  was
- `rd: VU skin check: captured logs/vu-skin-fail-F-L-I.bin and asked for a frame dump` (or `no
  sidecar written (WHY); asked for a frame dump`), then the window's `window: VU skin check: a
  failure was captured (...)` and, after the dump, `window: VU skin check: the capture: SIDECAR and
  the frame dump ZIP (PNG), the presented picture at the next present; the sidecar is in the zip`
- `rd: VU skin check: frame F command L:I drew N samples outside its box (texels X,Y WxH, the CPU's vertices + 2 GS pixels; through FS[ without the draw's attachment read]; the probe right|wrong too) (box event E)`

At the renderer's shutdown, when the run drew a check: `rd: VU skin check: at the end of the run: D
checks drawn (B box checks drawn), R read, W wrong, X blocks whose bytes differed, Y box checks
with samples outside` (a check of the last replays is not read: its slot was not recycled), and with
twins drawn `rd: VU skin check: twins at the end of the run: T drawn (O through the draw's own
pipeline with its attachment read), R read, D differ; K checks targeted after a twin event`.

In the 10 s summary, when the check ran: `rd: VU skin check: P checks, W wrong, B blocks whose bytes
differed (R changed after the draw); Q box checks, S with samples outside the box, N not drawn
(their pipeline not made yet; F with no slot left for the key); variants on the wrong ones: c1
right K, c2 right L (c2 drawn in M checks); largest difference on a right vertex D/16 pixel[; T twins, D differ (first at frame F), N not drawn
(their pipeline not made yet)]`.

## Switches

- On by default only on an Adreno (vendor 0x5143: Qualcomm's driver and Turnip alike; not
  through MoltenVK) with a window (`rd__skin_check_default`, pinned by `rd_vu_skin_check_judge`).
  `[dev] vu_skin_check = false` (`ICO_RD_VU_SKIN_CHECK=0`, also `false`, `off`, `no`): off, for an
  A/B; `true` (`=1`, `true`, `on`, `yes`): on on any Vulkan device, headless too (the tests);
  `ICO_RD_VU_SKIN_CHECK=2`: every skinned command of every replay (`rd_vu_skin_check_ab`)
  ([CONFIG.md](CONFIG.md)). `rd_replay_tool --skin-check` turns it on for its own replays. Not on
  Direct3D or OpenGL.
- `ICO_RD_VU_SKIN_INTERVAL_MS=N` (tests, A/Bs): the scheduled probe every N ms (0: one a present),
  `off`: none but the targeted ones.
- `ICO_RD_VU_SKIN_SABOTAGE=A,nan` (tests): as `A,ring` with the bone made NaN instead of zeros (the form
  of issue 123's picture); the twins' A reads it too.
- `ICO_RD_VU_SKIN_SABOTAGE=A` (tests): the check's own probe of the draw's block reads the bone at
  address A as zeros while the draw's bytes are right (H1's form); `A,ring` zeroes the bone in the
  ring after the check kept the bytes, so the draw and the probe both read zeros (H2's form).
- `rd_replay_tool DUMP OUT.png --skin-check` ([RENDER.md](RENDER.md)): every skinned command of the
  dump checked (80 a replay, each read back at once, the box checks waiting for their pipelines),
  the lines above on stderr, then `<dump>: skin check: N skinned commands, C checks, W wrong
  (address A), B blocks whose bytes differed (R changed after the draw), X of Y box checks with
  samples outside; on the wrong ones c1 right K, c2 right L (c2 drawn in M); reads as: WORDS;
  twins: T read, D differ (O through the draw's own pipeline); V wrong vertices dropped
  (non-finite)`. A
  is the address the last wrong check named, WORDS that check's decision (a check whose bytes
  differed but whose probe drew right, a block whose changed bone the mesh does not use, gives its
  words only while no check was wrong).

What the twins see and still cannot: they see a bone read that depends on the draw's offset or page
phase INSIDE the scene pass, through the draw's own pipeline where it reads its attachment (the
binning pass and the pipeline's link included), on every skinned draw of every present. They cannot
see a fault that reads both offsets alike (a wrong read whatever the address: the probe's c1 row
says that one), a fault in a draw's second pass (the no-dual pair: the twins redraw the first), a
draw with Z writes that reads its attachment (twinned through the box pipeline, not its own), or
anything under the vertex pre-pass (the draw reads the pre-pass record, not the bones; off on the
tile-based GPUs). What the check as a whole cannot see: it compares the device with the CPU over the same stream and bytes, so
a dump whose stream was patched (the deep dive's copy with the lanes on 200 moved to a zero bone)
checks right everywhere: both sides read the patched stream. On lavapipe the sabotage stands in for
the device's wrong read.

## Tests

`rd_vu_skin_check_test.c` with `rd_vu_skin_check.cmake` (`rd_vu_skin_check_judge`,
`rd_vu_skin_check`, `rd_vu_skin_check_robust`, `rd_vu_skin_check_probe`,
`rd_vu_skin_check_probe_robust`, `rd_vu_skin_check_ring`, `rd_vu_skin_check_twin`,
`rd_vu_skin_check_twin_nan`, `rd_vu_skin_check_capture`)
and `rd_vu_skin_check_dump.cmake` (`rd_vu_skin_check_dump`, `rd_vu_skin_check_163`,
`rd_vu_skin_check_163_probe`, `rd_vu_skin_check_163_ring`, `rd_vu_skin_check_163_patched`, `rd_vu_skin_check_123`,
`rd_vu_skin_check_123_nan` on issue 123's blended dump
`frame-20261106-080033-v3910-blended`), and
`rd_vu_skin_check_ab.cmake` (`rd_vu_skin_check_ab`: the pictures with the check on for every
skinned command against off, byte for byte, at Original, with the attachment read and GS state as
shader data, and with MSAA, the twins drawn in every on run and through the draws' own pipelines in
the attachment-read runs), `share_report` (the newest sidecar in the share list);
[TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md).
