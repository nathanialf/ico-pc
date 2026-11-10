# The skin check

The skinned draws' bone-block check (issue 163): on an Adreno with a window (Qualcomm's driver or
Turnip, issue 163's devices), the renderer draws one skinned command again every 100 ms as a probe
of its own and compares what the GPU computed with the CPU's copy of the same program, so a
reporter's log names the layer that placed a skinned vertex wrong without anything to switch on.
It is designed not to change the picture (`rd_vu_skin_check_ab` replays the dumps with it on for
every skinned command and compares the pictures byte for byte). Off on every other device: issue
179's Mali-G52 is GPU-bound, and the check's GPU work is not spent there.

On every Adreno with a window (Qualcomm's own driver and Turnip, Mesa's driver, issue 123's and
163's reporters) the check also twins EVERY skinned draw inside its own pass (the twins, [What it
checks](#what-it-checks)), aims its probe at a command whose twins differed, and captures its first
failure of the run by itself (the capture, same section); on Turnip it also probes one command a
present instead of one every 100 ms. Once the run has a verdict (2,000 replays with twins read and
none differing) the twins thin out to one skinned draw a picture through the cheap box pipeline and
Turnip's probe goes back to every 100 ms; a twin event twins every draw again ([The
thinning](#what-it-checks)). Where the program entries are the plain set (Turnip's) it also
draws the bones through the ring, a storage buffer, beside the picture (column 3 of the probe and a
third twin, C), and when the twins differ the game climbs a ladder of two fixes by itself across
starts, each written to `config.toml` with a mark ([The ladder](#the-ladder)).

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
| `port/render/rd_vu_bones.c` | the ladder's renderer side: the cluster entries' ring builds for column 3 and twin C (`rd__vu_bones_ring_shader`), the start line of `[dev] vu_bones`, the run's evidence for the window (`rd.h` `rd_vu_bones_evidence`), the ring read's verdict at the end |
| `port/shaders/vu_common.hlsli` (`ICO_VU_BONES_RING`), `port/shaders/CMakeLists.txt` | the ring builds `vu_skin_ring_plain_vs`, `vu_skin_spec_ring_plain_vs`, `vu_skin_debug_ring_plain_vs`: the plain variants with `VU_BONE(i)` read as `vu_ring[vu_mark.w + i]` (t5) |
| `port/render/rd_pipeline.c` (`rd__vu_policy_init`), `rd_replay_vu.c` (`vuSource`) | `[dev] vu_bones = ring`: the ring builds in place of the cluster entries' plain variants, the block's t5 index in `IcoVuCB.mark[3]` |
| `port/platform/bones_ladder.c`, `window_bones.c` | the ladder across starts (pure: the step, the mark, the lines, step 1's switch guards) and the window's part (the evidence read from the pump, the keys written, step 1's switch asked) |
| `port/platform/window_switch.c` (`ico_window_switch_restart`) | step 1 in the run: the renderer made again in place through Options > Graphics > Apply's switch |
| `port/platform/android/gpu_driver_android.c` (`tu_debug_apply`) | `[dev] turnip_debug = nobin`: `TU_DEBUG=nobin` set before a Turnip package loads |
| `port/render/rd_vu_skin_trace.c` | the trace (pure): the two stamps (the probe target's clear, the ring's 16 bytes), the checked blocks kept (`RdSkinTrace`, the last 64), the search of a differing copy's bytes and its words ([The trace](#the-trace)) |
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
| 3, c3 | the same as column 0 through the cluster entry's ring build (the bones read through t5, a storage buffer, from the draw's own block; made in the background, drawn where the program entries are the plain set) | whether the ring read clears it: the ladder's step 1 tested beside the picture |

Every column draws with a VuCB copy of its own whose mark lane (`IcoVuCB.mark[3]`, which no
cbuffer build reads) holds the ring index of the block that column reads: column 3 reads its bones
there, and under `[dev] vu_bones = ring` every column does.

Where the vertex pre-pass is on (every device whose pre-pass self-test passed, tile-based GPUs and
so the Adreno too), the draw itself goes through `vu_main_vs` with the pre-pass's
record, while the probe goes through the program's own cluster entry: there S2 checks the program
entry's read of the same bytes (through VuBoneCB, where the record's batch builds read them through
the ring), and S3 (below) the real draw's placement. The probe pass is not in the pass ring (`rd__passring_*`): a device loss
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

**The twins.** Where they run (`rd__vu_skin_twins_on`: an Adreno with a window, Qualcomm's driver
and Turnip alike, the check's own default rule; `ICO_RD_VU_SKIN_CHECK=2` and the tool's and tests' mode 2 anywhere), every
skinned draw of a replay (up to 128, `RD_SKIN_TWINS_MAX`) is drawn twice more right after itself,
inside its own pass, nothing written, each inside an occlusion query (twin t's are queries 80 + 3 t
and 80 + 3 t + 1, and 80 + 3 t + 2 for C below; `RD_SKIN_QUERIES` = 464
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
reads its bones through the table, not the b3 offset). A draw through the VU pre-pass's record
(`vu_main_vs`: every device whose pre-pass self-test passed, tile-based GPUs too) reads the record, not
its bones, so twins through its own key would read the same record at either offset and never
differ: its twins go through the program's own vertex entry (the key `vuDraw`'s fallback takes,
which ignores the record) in the box check's form, and the line says `(the program's own vertex
entry: the draw read the pre-pass record)`. The record is made by the pre-pass's batch builds by
default (`g_rd.vuPreBatch`; `[dev] vu_prepass_batch = false` or a missing batch module: one an
entry), and those read every entry's bones through the ring (t5, `vu_common.hlsli`
`ICO_VU_PRE_BATCH`), not the uniform block: so the twins of a record draw read them the
same way, through the entry's ring build (twin C's pipeline table, A's and B's VuCB copies carrying
their blocks' ring indices, no C), where the ring builds exist (the plain set; elsewhere the uniform
read as before). Their line says `(the program's own vertex entry's ring build: the draw read the
pre-pass record and its batch builds read the bones through the ring)` and ends `; A and B read the
bones through the ring, as the pre-pass record did`, and their event reaches the ladder as a ring
read that did not clear it (through the uniform read they would test a read the picture does not
take, and the ladder would act on it). Both twins clip alike, so unlike the box
check there is no behind-the-eye gate. Equal bytes give equal counts; when A's and B's differ
(by count where the device's queries are precise, `RhiLimits.occlusionPrecise`: the device's
`occlusionQueryPrecise` enabled at device creation and `VK_QUERY_CONTROL_PRECISE_BIT` passed to
each begin, `vk_cmd.c`; lavapipe and Turnip offer it) the line `twins differ` names the command, both counts and page phases, the fragment entry
and whether it was the draw's own pipeline, and what the probe of the same draw in that replay
said; the command becomes the probe's target for the next 8 replays (whatever the clock and the
schedule; nothing else is probed meanwhile). A non-finite read drops A's triangles, a zero read
moves them: both change A's count and not B's.

**Twin C, the ring read.** Where the ring builds exist (the plain set), a third draw right after
A and B through the twin's own key with the cluster entry's ring build as its vertex module (its
pipeline compiled in the background, 8 slots, `RD_VU_SKIN_RING_PIPES`), with a VuCB copy whose
mark lane holds the draw's own block's ring index: the bones the draw read, read as a storage
buffer. C against B (the same bytes through the uniform read at a fresh offset) says whether the
ring read gives what the draw should have read: equal, the ladder's step 1 would clear it. The
event line prints C's count; the 10 s block counts the C twins that differ from B. Under `[dev]
vu_bones = ring` the draw itself reads through the ring: A and B then carry VuCB copies with their
own block's index (A the draw's, B the fresh one), so A against B tests the ring read in the pass,
and there is no C.

**The thinning.** The twins cost every skinned draw two or three more draws inside the scene pass
(on Turnip through the draw's own attachment-reading pipeline): issue 123's reporter lost 30 fps
to 15 on Turnip at Frame rate Original. Once the run has a verdict they thin out
(`rd_vu_skin_check.h` `RD_SKIN_THIN_REPLAYS`, 2,000, the ladder's `BONES_HEALED_REPLAYS`): after
2,000 replays with twins read and none differing, one skinned command a replay is twinned, in turn
over the replay's skinned commands, always through the box check's pipeline (colour and Z writes
off; never the draw's own), and Turnip's scheduled probe is every 100 ms again. A twin event sets
the count back to zero: every skinned draw is twinned again until 2,000 more clean replays. The
count starts again with the renderer (the ladder's evidence does too). The game's mode only (mode
2, the tool's and the A/Bs', twins every command always). The 10 s VU line says what the twins
cost a picture (`window: VU: ...; twins N draws, V vertex invocations a picture; GPU work ...`,
`RdPerfRecord.skinTwinDraws`, `skinTwinInvocations`, 9 invocations a triangle).

**The schedule.** One probe every 100 ms elsewhere; on Turnip one a present until the twins are
thinned (`ICO_RD_VU_SKIN_INTERVAL_MS` over both; `off` leaves only the targeted probes). The command is
chosen vertex-weighted: a cursor over the last replay's skinned vertices, moved on by the golden
ratio of the total after each probe, picks the command whose vertices hold it, so a mesh is probed
in proportion to its vertices.

**The capture.** The run's first failure (twins that differ, or a wrong check) is captured once a
run (a renderer made again in the run, by Options > Graphics > Apply or the ladder's step 1, keeps
it taken): a
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
rest of the run) nor the free-memory floor now (asked again at the next check). Where the start-up
set stopped for memory (this start's at the floor, or the last start's, which makes this one
set-less) on Qualcomm's own driver they are compiled carefully: one at a time, each begun only
above the floor and at least 2 s after the last one began (`SK_CAREFUL_GAP_MS`): the run
of issue 123's report stopped its set 4 MB under the floor
(1,943 MB available, floor 1,947 MB), played with 2.2 GB free, and had neither a box check nor a
twin when its check failed. Until a pipeline exists its part is not drawn: the check runs without
its box check, twins or c2, said once (`rd: VU skin check: its box check, its twins' pipelines and
its c2 variant are compiled carefully (<why>): one at a time, each only above the free-memory floor
and 2 s after the last; until then the check runs without them`, `... no background compile for
the rest of the run (<why>) ...`, `... a background compile waits for memory (<why>) ...`). The check holds 33 pipelines outside the renderer's cache at most (the probe's 3, c2's 3, column
3's 3, 16 box checks and twin C's 8), counted
in the pool's headroom ([RHI.md](RHI.md)); keys past the 16 box slots are not box-checked.

The twins' cost: twice each skinned draw's vertex work and rasterisation inside the scene pass,
no colour written, one 3,840-byte copy and a DrawCB copy in the ring a draw (about 1 MB a slot for
128, counted by `rd__vu_skin_ring_bytes`), 2 x 8 bytes of query results a draw; no CPU reference.
On Turnip the probe once a present adds about 200 KB of readback a present.

The cost: one probe pass of three times the draw's vertices times nine one-pixel triangles, one
3,840-byte copy, a 48 x 1,024 target's copy, every 100 ms; the box check four scissored redraws of
one command with nothing written; on the CPU the cluster program over at most 1,024 vertices, and
on a wrong check one more run of it for each candidate address.

## The trace

Issue 123's report (Adreno 740, Qualcomm's driver 512.676.1): `403 vertices wrong of 403 ...
the bone block as the GPU copied it: differs at qwords 0..195; as the CPU reads it after the fence:
equal`, c1 wrong too, `reads as: H2, the memory`, recorded in replay 785, the same replay whose
present blocked 340 ms and whose GPU timestamps restarted (the stall lines of that log). Qwords
0..195 are every qword that mesh's block uses (196 of 240; the rest are zeros in both), and the
probe's first wrong vertex sits about 19 and 54 GS pixels from the CPU's: a whole block of the same
shape, not a few bytes. That log could not tell three paths apart: the readback not written by
the replay (its rows and copy an earlier check's), the GPU reading the ring as an earlier replay
left it, or the GPU reading other memory. The trace (`rd_vu_skin_trace.c`):

- **The clear stamp.** The probe target is cleared to `(0xA5, the replay's number bits 0..7, 8..15,
  16..23)`, not zeros; no probe draws field 15 of a column, so the readback's last texel names the
  replay whose copy wrote the rows.
- **The ring stamp.** At the replay's end the CPU writes 16 bytes into the ring (`RdSkinRingStamp`:
  magic, replay, ring slot, ring id) and the copy of them into the readback is the last copy, after
  the bone blocks.
- **The search.** A GPU copy that differs from the bytes kept is looked for: in the slot's readback
  as the last harvest whose clear stamp was its own left it (check 0), all zeros, in the checked
  blocks of the last 64 checks (`RdSkinTrace`, newest first; the check's own never), then in the
  check's own ring (while its replay's bytes are there) and the other frame slot's ring as they are
  at the harvest, at every uniform-alignment offset within the bytes the slot's last replay wrote
  (the draw's own offset first, at most 64 MB a ring; only for an event line: the ring's memory may
  be uncached on the device). Else `other memory`, with its zero qwords, qwords equal to the bytes
  kept and non-finite lanes.

So the next log decides the path by itself:

| the stamps and the search say | means |
| --- | --- |
| `the readback was not written by this replay's copies (the probe target's clear stamp: replay R's ...)` | the GPU did not run the replay's copies (a submission dropped or cut short, e.g. a GPU fault recovered by the kernel) or the CPU read the readback before they landed: the check's verdict is not about the bones; the ladder is not fed |
| the clear stamp right, `the ring's stamp reads replay R's` (`H2, the memory: the GPU read the ring as an earlier replay left it`) | the copies ran but the GPU saw the ring's memory as replay R left it: stale memory under the GPU (a cache or mapping of the upload memory) |
| both right, the copy `the block checked in replay R ...` or `the block at ring offset O of frame slot S's ring ...` | the GPU read another block: an offset or slot the GPU used that the CPU did not |
| both right, `no block ...: other memory` | other memory: neither the check's blocks nor the rings |

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

The storage-buffer read of the bones is built as the ladder's step 1 ([The ladder](#the-ladder)),
tested beside the picture by column 3 and twin C before it is switched in; the uniform robustness
off on the VU pipelines and the ring's slot order are not built until a log says which row it is.

## The lines

At start, from `rd_vuprepass_selftest`:

- `rd: VU skin probe: ok (120 vertices x 64 offsets in P page phases, codes 20 22 24, plain set) in T ms`
- `rd: VU skin probe: wrong at offset O (page phase P), code C, plain set: N vertices, address A read as zero (got (x, y, z) want (x, y, z)) in T ms`
  (or `..., no zero matrix explains them ...`; `not run (<why>)` when its pipelines or frame could
  not be made)
- with the twins (an Adreno with a window, mode 2): `rd: VU skin check: on (every skinned draw
  twinned in its own pass through its own pipeline (A the draw's bone offset, B the same bytes at a
  fresh offset; counted by occlusion query, precise | zero or not only); thinned to one a picture
  through the box check's pipeline after 2000 replays with none differing, every draw again after a
  twin event; one probe a present (every 100 ms once thinned) (Turnip) | every 100 ms (elsewhere),
  targeted after a twin event, ...; the first
  failure captured (a sidecar and a frame dump); WHY)`
- `rd: VU skin check: on (one skinned draw every 100 ms, in turn, ...; variants: a fresh ring offset, uniform robustness off (VK_EXT_pipeline_robustness) | not offered | not needed (robust access off); the box check by occlusion query | not offered; WHY)`, WHY saying why it is on: `on by default on an Adreno with a window (Qualcomm's driver|Turnip); [dev] vu_skin_check = false turns it off`, or `[dev] vu_skin_check = true (ICO_RD_VU_SKIN_CHECK=1)`
- `rd: VU skin check: off (the start-up probe failed: the device was lost in it | its picture could not be read back)`, then `window: VU skin check: the start-up probe failed on this device (...); [dev] vu_skin_check = false is written to <config> ...`; at the next start after an end inside the probe `window: VU skin check: the previous run ended inside its start-up probe (<note>); ...`
- on a window's Vulkan device with the check off: `rd: VU skin check: off (not an Adreno: ICO_RD_VU_SKIN_CHECK=1 / [dev] vu_skin_check = true turns it on)`, `off ([dev] vu_skin_check = false (ICO_RD_VU_SKIN_CHECK=0))` or `off (the device was lost)`

On an event, the first 8, then every 100th, of each kind on its own count (a wrong check's two lines, a check whose bytes differed with every vertex right, a box check's samples outside), so the many changed blocks of an H2 run never use up a wrong check's lines:

- `rd: VU skin check: frame F command L:I mesh NAME #ID (V vertices): N vertices wrong of M, address A: got (x, y, z) want (x, y, z) (a zero matrix at A gives it: yes|no; it gives K of them); the bone block as the GPU copied it: equal | differs at qwords Q..R; as the CPU reads it after the fence: equal | differs at qwords Q..R | not read (...); code C through MODULE (wrong check E); recorded in replay R (frame slot S) after submission N, read with M finished`
  (X and Y in GS pixels, absolute; Z the GS Z; A the address whose zero matrix gives the most wrong
  vertices, else the first wrong vertex's lane x). The last clause (for issue 123's H2
  lines, the ring read back as zeros or stale values) places the check against the GPU's work:
  N is the backend's submissions when the check was recorded (`rhi_gpu_progress`, before its
  replay's own submit, so that replay is submission N + 1 or later), M the newest finished when it
  was read; M at or past N + 1 says the GPU had finished the replay when its bytes were read, below
  it that the port read early. 0 and 0 where the backend does not count them.
- `rd: VU skin check: frame F command L:I variants: (c1) the block at a fresh ring offset (page phase P, the draw's D): right|wrong (N wrong); (c2) uniform robustness off: right | wrong | not run (robust access off) | not offered (no VK_EXT_pipeline_robustness) | not run (its pipeline not made yet) (N wrong); (c3) the bones through the ring: right | wrong | not drawn (N wrong); reads as: WORDS`
- `rd: VU skin check: frame F command L:I mesh ...: every vertex right (M compared); the bone block as the GPU copied it: ...; reads as: WORDS (bytes event E); recorded in replay R (frame slot S) after submission N, read with M finished` (bytes that differed, every vertex right; the last clause as above)
- `rd: VU skin check: frame F command L:I mesh NAME #ID (V vertices): twins differ: A (the draw's
  offset, page phase P) N samples, B (fresh, phase Q) M samples[, C (ring read) K samples]; through
  FS with the draw's attachment read (its own pipeline) | with colour and Z writes off (the program's
  own vertex entry: the draw read the pre-pass record | the program's own vertex entry's ring build:
  the draw read the pre-pass record and its batch builds read the bones through the ring | the box
  check's pipeline: the twins thinned | the box check's pipeline: the draw does not
  read its attachment | the box check's pipeline: the draw reads its attachment but writes Z); the probe of
  this command: right | wrong at address A | not probed; reads as: WORDS (twin event E)` (WORDS:
  `the draw's bone read at its own offset ...` with a wrong probe, `the scene pass's execution: the
  twins differ inside the pass while the probe of the same bytes drew right outside it` with a right
  one, else `the bone read inside the scene pass differs ...; the probe of this command follows in
  the next replays`); a wrong check's line ends `; the probe targeted after a twin event` when it
  was; under `[dev] vu_bones = ring` the event's parenthesis ends `; A and B read the bones through
  the ring: step 1 of the ladder in force`, for a record draw's ring twins `; A and B read the bones
  through the ring, as the pre-pass record did`
- `rd: VU skin check: twins thinned to one a picture after P clean replays (through the box check's
  pipeline, in turn over the skinned draws[; the probe every 100 ms again]; re-armed by a twin
  event)` and `rd: VU skin check: twins re-armed by a twin event: every skinned draw twinned again
  until 2000 replays with none differing (re-armed N times)`
- `rd: VU pre-pass: the skinned commands are drawn the old way, through their entries' ring builds
  ([dev] vu_bones = ring with the pre-pass's batches off: its own entries read VuBoneCB)` (once a
  run)
- `rd: VU skin check: captured logs/vu-skin-fail-F-L-I.bin and asked for a frame dump` (or `no
  sidecar written (WHY); asked for a frame dump`), then the window's `window: VU skin check: a
  failure was captured (...)` and, after the dump, `window: VU skin check: the capture: SIDECAR and
  the frame dump ZIP (PNG), the presented picture at the next present; the sidecar is in the zip`
- `rd: VU skin check: frame F command L:I the GPU's copy of the bone block (frame slot S's ring,
  offset O of U written, C MB; recorded in replay R): WHERE; Z of 240 qwords zero, K of 240 qwords as
  kept, N of 960 lanes not finite; B KB of the rings searched` after a line whose copy differs (the
  same budget), WHERE one of `the bytes the slot's readback held from the check of replay Q (not
  written again)`, `all zeros`, `the block checked in replay Q (frame slot T, ring offset Y[, the
  draw's own offset]; mesh NAME #ID, command L:I): another replay's block`, `the block at ring offset
  Y of frame slot T's ring as it is now (replay Q's[, the draw's own offset])`, `no block the
  check kept or the rings hold: other memory` ([The trace](#the-trace))
- `rd: VU skin check: frame F command L:I replay R (frame slot S, readback of N checks): the readback
  was not written by this replay's copies (the probe target's clear stamp: replay Q's | no stamp
  (bits 0..23); the ring's stamp: ...): the GPU did not run them, or they were read before they
  landed; recorded after submission N, read with M finished (stamp event E)` or `...: the copies ran
  (the clear stamp is this replay's) but the ring's stamp reads replay Q's | no stamp: the GPU read
  the ring's memory as it was before this replay wrote it; ...` (its own budget: the first 8, then
  every 100th)
- `rd: VU skin check: frame F command L:I drew N samples outside its box (texels X,Y WxH, the CPU's vertices + 2 GS pixels; through FS[ without the draw's attachment read]; the probe right|wrong too) (box event E)`

At the renderer's shutdown, when the run drew a check: `rd: VU skin check: at the end of the run: D
checks drawn (B box checks drawn), R read, W wrong, X blocks whose bytes differed, Y box checks
with samples outside; U readbacks not written by their replay, O ring stamps of an earlier replay`
(a check of the last replays is not read: its slot was not recycled), and with
twins drawn `rd: VU skin check: twins at the end of the run: T drawn (O through the draw's own
pipeline with its attachment read), R read, D differ; K checks targeted after a twin event; the ring
read's twin (C) X drawn, Y read, Z differ from B; thinned N times, re-armed M times[ (thinned at the
end)]`; under `[dev] vu_bones = ring`, when twins were
read, `rd: VU skin check: the ring read: healed (0 of T twins differ over P replays)` or `... not
healed (D of T twins differ over P replays)`.

In the 10 s summary, when the check ran: `rd: VU skin check: P checks, W wrong, B blocks whose bytes
differed (R changed after the draw); Q box checks, S with samples outside the box, N not drawn
(their pipeline not made yet; F with no slot left for the key); variants on the wrong ones: c1
right K, c2 right L (c2 drawn in M checks); largest difference on a right vertex D/16 pixel[;
readbacks not written by their replay U, ring stamps of an earlier replay O (only when one was)][; T twins, D differ (first at frame F), N not drawn
(their pipeline not made yet), ring twins differ E (of R read) | A and B through the ring, thinned
to one a picture (P clean replays) | every skinned draw (P of 2000 clean replays to thin)]`. The
window's 10 s VU line carries the twins' cost: `window: VU: D draws, T triangles, V vertex
invocations (...); twins N draws, W vertex invocations a picture; GPU work ...` (nothing without a
twin drawn).

## The ladder

Issue 123's self-healing on the devices where the twins differ (Mesa's Turnip on an Adreno: Yorda's
shin, the user's decision of 2026-11-06: two steps, the port's robust access never touched). The
renderer counts the run's evidence (`port/render/rd_vu_bones.c`, `rd.h` `rd_vu_bones_evidence`);
the window's pump reads it (`port/platform/window_bones.c`) and decides with
`bones_ladder_step` (`port/platform/bones_ladder.c`, pure):

| the run's evidence and the keys in force | the action |
| --- | --- |
| a twin event whose C equalled B, or a wrong check with equal bytes whose column 3 drew right | step 1: `[dev] vu_bones = ring` written and put in force in the run (the renderer made again in place), and for every later start |
| two twin events whose ring read differed (C differing from B, or a pre-pass record draw's twins, which read the bones through the ring as the record did), or the twins differing twice while the probe of the same command drew right and no probe wrong (the scene pass's execution) | step 1 skipped: step 2, `[dev] turnip_debug = nobin` (marked trial), where the next start can set it, else the ladder stops |
| 8 twin events with no evidence either way (the ring read's pipelines not made yet) | step 1 |
| step 1 in force and 8 twin events (not one: a stray event is not enough for a step that costs every pass its binning) | step 2 (marked trial) where the next start can set it (Android, a Turnip package), else the ladder stops |
| step 2 in force and the twins still differing | `[dev] turnip_debug = off` written: "both tried, neither cleared the twins", the ladder stops (a later start says so and writes nothing) |
| step 2 (trial) and step 1 in force and no twin differing over 2,000 presents | `[dev] turnip_debug = retest` written: the next start runs without `TU_DEBUG=nobin` |
| the re-test (`turnip_debug = retest`, or `nobin` without the word trial or needed: an older ladder's or set by hand) on a Turnip package, no twin differing over 2,000 presents | `turnip_debug` and its mark removed (left empty): step 2 not needed |
| the re-test and 8 twin events | `[dev] turnip_debug = nobin` marked needed: in force at every later start, never re-tested |
| any other step in force and no twin differing over 2,000 presents (replays with twins read) | healed: said once, the key kept |
| a wrong check whose bytes changed under the GPU (H2, a port race) | the ladder does not act |
| no twin differing and no step in force | nothing |
| step 1 would be next, but the program entries are not the plain set on Vulkan (the evidence's `ringUsable`: the renderer would refuse the ring builds) | nothing written, no restart; said once (`window: VU skin check ladder: waits: the ring read needs the plain shader set (...): step 1 not written (D of T twins differ)`); step 2's evidence still acts |

**Step 1, the bones through the ring, in the run.** When the window writes the key it hands
`ring` and the mark to the renderer through `rd_set_vu_bones` (taken by the next `rd_init` over
`ICO_RD_VU_BONES` and `ICO_RD_VU_BONES_BY`: no environment is written while the renderer's, the
audio's and the driver's threads may read it) and asks for the renderer to be made again in place (`port/platform/window_switch.h` `ico_window_switch_restart`):
the same backend and card through Options > Graphics > Apply's own switch, run in the event pump
between two steps of the game and never inside a present (`rd_suspend` with the game's state kept,
the window made again, `rd_init`, `rd_resume`), so no shader id is added and no pipeline in flight
is retired. Guards (`bones_restart_step`): not while a movie plays and not in the run's first 30 s
(the switch waits, said once: `... the switch to the ring read waits for the movie's end | the run's
first 30 s (else the next start reads it)`), at most once a run, and not when the switch machinery
refuses (photo mode, the model viewer, the touch layout editor, a switch asked already: its own line
`window: the renderer not made again in place (...): WHY`, then the next start reads the key). The
new renderer's evidence starts from zero, with the ring builds in force, so the ladder goes on in
the same run (step 2 is written there when the twins still differ). At every later start
`rd__vu_policy_init` reads `[dev]
vu_bones = ring` (exported as `ICO_RD_VU_BONES=ring`) and, where the program entries are the plain
set (Turnip's always; the ring builds are plain variants), puts the cluster entries' ring builds
(`vu_skin_ring_plain_vs`, `vu_skin_spec_ring_plain_vs`, `vu_skin_debug_ring_plain_vs`:
`vu_common.hlsli` `ICO_VU_BONES_RING`) in place of their plain variants: the same pipeline keys and
cache, every skinned draw's VuBoneCB bytes read through t5 (the ring the VU layout binds on every
device) from the qword `vuSource` puts in `IcoVuCB.mark[3]`. The same words are computed
(`rd_vu_bones_ring_ab`: every picture byte for byte; `vu1_test`: probe and pictures as the plain
variant's). A skinned draw whose block t5 does not hold whole is not drawn (counted, said once:
`rd: VU bones: a skinned draw's bone block (ring offset O) lies past what t5 holds: not drawn ...`).
With the vertex pre-pass on (every device whose self-test passed, tile-based GPUs too) the record is
made by the pre-pass's batch builds by default, which read the bones through the ring already
(`ICO_VU_PRE_BATCH`: `VU_BONE(i)` = `vu_ring[vu_pre_b + i]`), step 1 or not: so the real skinned
draws read the ring there whatever the key. With the batches off (`[dev] vu_prepass_batch = false`,
or a batch module missing) the pre-pass's one-an-entry entries read VuBoneCB; under step 1 the
skinned commands then stay out of the record and draw the old way through their entries' ring
builds (`vuPrepassRun`, said once a run: `rd: VU pre-pass: the skinned commands are drawn the old
way, through their entries' ring builds ...`), the rest through the record
(`rd_vu_bones_ring_ab_prepass`: every picture byte for byte with the pre-pass on, batched and not).
Column 3 and twin C test the fix beside the picture before it is switched in.

**Step 2, `TU_DEBUG=nobin`, at the next start** (the driver library is loaded once a process: a
renderer made again in place keeps it). At the next start `port/platform/android/gpu_driver_android.c`
(`tu_debug_apply`, before `adrenotools_open_libvulkan`) reads `[dev] turnip_debug = nobin` and sets
`TU_DEBUG=nobin` in the process environment (appended to a value already set), only when the
package's library is Turnip's (`libvulkan_freedreno.so`); Mesa's documentation lists `nobin` among
`TU_DEBUG`'s options (docs.mesa3d.org/drivers/freedreno.html: the binning pass off). Whether the
variable reaches the driver through the package loader on Android is not verified: the twins under
it are the indicator. Written with the mark's word `trial` after 8 twin events under step 1 (or
straight away on the evidence above); `turnip_debug = off` when the twins still differ under it.

**The re-test of step 2** (nobin binds every pass in system memory, a cost no step should keep
without evidence). Healed under step 2
with step 1 in force (2,000 presents, no twin differing) writes `turnip_debug = retest`; that start
sets no `TU_DEBUG` (the loader says `gpu driver: TU_DEBUG=nobin not set at this start ([dev]
turnip_debug = retest, ...): the skin check re-tests whether it is needed`), step 1 alone in force;
2,000 presents there with no twin differing remove the key and its mark (left empty, which the
loader and the kept-keys line take for absent: the config store has no removal); 8 twin events
write `nobin` back with the word `needed`, kept for every later start and never re-tested. The
re-test is judged only under a Turnip package. A `turnip_debug = nobin` with neither word (an older
ladder's, or set by hand) is a re-test at the next start, said so. Healed under step 2 without
step 1 (step 1 skipped) keeps the key: nothing else would cover the fault. No step 3: `[dev]
gpu_robust` is never touched.

**The marks.** Each key the ladder writes carries `<key>_by = "vu_skin_check <version> frame F; D of
T twins differed"` (F the frame of the run's last twin event, D of T the run's twins that differed
of those read; `turnip_debug = nobin`'s mark ends `; trial` or `; needed`), and the start-up line `ico_pc: [dev] keys kept from an earlier start: ...` lists
`vu_bones`, `vu_bones_by`, `turnip_debug`, `turnip_debug_by` with the other kept keys. The keys
stay across versions; deleting them starts the ladder again.

The lines:

- start, renderer: `rd: VU bones: through the ring (storage buffer) on the skinned entries ([dev]
  vu_bones = ring, written by the skin check at <version> frame F; D of T twins differed | set
  without the skin check's mark)`, or `rd: VU bones: through VuBoneCB` (with `([dev] vu_bones = ring
  not used: WHY)` when asked on the extended set or with a ring build missing)
- start, window (first pump): `window: VU skin check ladder: step 1 of 2 (the ring read) for this
  start ([dev] vu_bones = ring, ...)`, `window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin)
  for this start; step 1 (ring read) did not clear the twins (D of T)`, `window: VU skin check
  ladder: [dev] turnip_debug = nobin, but TU_DEBUG was not set at this start (...)`, `window: VU skin
  check ladder: step 2 of 2 (TU_DEBUG=nobin) for this start, needed (the re-test without it saw the
  twins differ): kept, not re-tested; ...`, `window: VU skin check ladder: the re-test of step 2 at
  this start: TU_DEBUG=nobin not set ([dev] turnip_debug = retest | [dev] turnip_debug = nobin
  without the word trial or needed: taken as retest, MARK)[; step 1 (ring read) in
  force]; 2000 presents with no twin differing remove the key, 8 twin events write nobin back as
  needed`, `window: VU skin
  check ladder: both tried, neither cleared the twins ([dev] turnip_debug = off, ...); the ladder
  stops; step 1 (ring read) stays in force`
- start, Android loader: `gpu driver: TU_DEBUG=nobin set before LIB loads ([dev] turnip_debug =
  nobin, turnip_debug_by = MARK): whether the driver reads it through the package loader is not
  verified; the skin check's twins under it are the indicator`, or `gpu driver: [dev] turnip_debug =
  nobin, but the package's library is LIB, not Turnip's libvulkan_freedreno.so: TU_DEBUG is not set`,
  or at a re-test `gpu driver: TU_DEBUG=nobin not set at this start ([dev] turnip_debug = retest |
  nobin without the word trial or needed, taken as retest[, turnip_debug_by = MARK]): the skin check
  re-tests whether it is needed`
- step 1 written: `window: VU bones: [dev] vu_bones = ring is written (the skin check saw N twin
  events at bone address A over P presents); the ring read beside the picture cleared it K times |
  the ring read untested beside the picture: its pipelines not made yet; in force from the next
  picture (CONFIG)`, then `window: VU bones: switching to the ring read now (the renderer made again
  in place, as Options > Graphics > Apply does); [dev] vu_bones = ring written for every later
  start` and the switch's own `window: the renderer made again in place on BACKEND (issue 123's
  ladder: the bones through the ring), S s`; when the switch waits `...; the switch to the ring read
  waits for WHAT (else the next start reads it) (CONFIG)`; when it is refused `...; the next start
  reads the skinned draws' bones through the ring (CONFIG)`
- step 2 written: `window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin) for the next start
  (the driver library is loaded once a process):
  [dev] turnip_debug = nobin is written to CONFIG; step 1 (ring read) did not clear the twins (D of
  T)` (or `was skipped: ...` with why)
- step 2 failed: `window: VU skin check ladder: both tried, neither cleared the twins (D of T under
  TU_DEBUG=nobin); [dev] turnip_debug = off is written to CONFIG and the ladder stops`
- stopped: `window: VU skin check ladder: both tried, neither cleared the twins (...): the ladder
  stops`, or `... step 2 (TU_DEBUG=nobin) needs a Turnip driver package on Android, not this
  device's driver: the ladder stops`
- healed: `window: VU skin check ladder: step 1 (ring read) | step 2 (TU_DEBUG=nobin) healed the
  twins: 0 of T differ over P presents[; [dev] turnip_debug = nobin is kept (needed: the re-test
  without it saw the twins differ) | (step 1 not in force: nothing else would cover the fault)]`
- re-test written: `window: VU skin check ladder: step 2 (TU_DEBUG=nobin) healed the twins with step
  1 (ring read) beside it: 0 of T differ over P presents; [dev] turnip_debug = retest is written to
  CONFIG: the next start runs without TU_DEBUG=nobin, to see whether step 1 alone keeps them equal`
- re-test passed: `window: VU skin check ladder: the re-test without TU_DEBUG=nobin kept the twins
  equal: 0 of T differ over P presents; [dev] turnip_debug is removed from CONFIG: step 2 not
  needed[; step 1 (ring read) stays in force]`
- re-test failed: `window: VU skin check ladder: the re-test without TU_DEBUG=nobin: the twins differ
  again (D of T); [dev] turnip_debug = nobin is written to CONFIG, marked needed: step 2 for every
  later start, not re-tested again`
- a port race: `window: VU skin check ladder: the wrong reads are the ring's bytes changing under the
  GPU (H2, a port race: N checks), not the driver's read: the ladder does not act`

## Switches

- On by default only on an Adreno (vendor 0x5143: Qualcomm's driver and Turnip alike; not
  through MoltenVK) with a window (`rd__skin_check_default`, pinned by `rd_vu_skin_check_judge`).
  `[dev] vu_skin_check = false` (`ICO_RD_VU_SKIN_CHECK=0`, also `false`, `off`, `no`): off, for an
  A/B; `true` (`=1`, `true`, `on`, `yes`): on on any Vulkan device, headless too (the tests);
  `ICO_RD_VU_SKIN_CHECK=2`: every skinned command of every replay (`rd_vu_skin_check_ab`)
  ([CONFIG.md](CONFIG.md)). `rd_replay_tool --skin-check` turns it on for its own replays. Not on
  Direct3D or OpenGL. The VU pre-pass's start-up self-test's replays are never picked, checked,
  twinned or counted in the round robin (`rd__vu_prepass_selftest_running`: issue
  123's hang log twinned the self-test's skinned toy mesh, `rd vu pre-pass self-tes`, which armed
  the capture and a frame dump and made a probe target of a mesh never seen again).
- `[dev] vu_bones = ring` (`ICO_RD_VU_BONES=ring`; `cbuffer` or absent: VuBoneCB), with its mark
  `vu_bones_by`, and `[dev] turnip_debug = nobin | retest | off` with `turnip_debug_by`: the ladder's keys,
  written by the game ([The ladder](#the-ladder), [CONFIG.md](CONFIG.md)); a player may set or
  delete them by hand (a key without the game's mark is named so in the start line).
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
  twins: T read (O through the draw's own pipeline), D differ (E of them through it); V wrong
  vertices dropped (non-finite); the ring read: c3 right K (c3 drawn in M), ring twins R read, E
  differ from B`. A
  is the address the last wrong check named, WORDS that check's decision (a check whose bytes
  differed but whose probe drew right, a block whose changed bone the mesh does not use, gives its
  words only while no check was wrong).

What the twins see and still cannot: they see a bone read that depends on the draw's offset or page
phase INSIDE the scene pass, through the draw's own pipeline where it reads its attachment (the
binning pass and the pipeline's link included), on every skinned draw of every present. They cannot
see a fault that reads both offsets alike (a wrong read whatever the address: the probe's c1 row
says that one), a fault in a draw's second pass (the no-dual pair: the twins redraw the first), a
draw with Z writes that reads its attachment (twinned through the box pipeline, not its own), or
the pre-pass record's own path (under the vertex pre-pass the twins draw through the program's
entry, not the record the draw read: they read the bones the way the record did, through the ring
under the batch builds, but in the scene pass, not in the pre-pass's own pass V). Once thinned they
see one skinned draw a picture, through the box pipeline. Where
the device does not offer precise occlusion counts the twins compare only zero against not zero
(the start line says `zero or not only`): a fault that drops part of a draw that still draws
something (the missing shin) is then invisible to them, and the probe alone sees it. What the check as a whole cannot see: it compares the device with the CPU over the same stream and bytes, so
a dump whose stream was patched (the deep dive's copy with the lanes on 200 moved to a zero bone)
checks right everywhere: both sides read the patched stream. On lavapipe the sabotage stands in for
the device's wrong read.

## Tests

`rd_vu_skin_check_test.c` with `rd_vu_skin_check.cmake` (`rd_vu_skin_check_judge`,
`rd_vu_skin_check`, `rd_vu_skin_check_robust`, `rd_vu_skin_check_probe`,
`rd_vu_skin_check_probe_robust`, `rd_vu_skin_check_ring`, `rd_vu_skin_check_twin`,
`rd_vu_skin_check_twin_nan`, `rd_vu_skin_check_capture`, `rd_vu_skin_check_stale` (the careful
compile rule draws a box check; each slot's readback not written again names the replay whose
stamps it holds), `rd_vu_skin_check_thin` (the thinning:
three twins a replay until the clean replays, one after, re-armed by the zeroed bone), and the
ladder's `rd_vu_skin_check_probe_c3`, `rd_vu_skin_check_twin_c`, `rd_vu_skin_check_twin_bones_ring`,
`rd_vu_skin_check_twin_record` (a pre-pass record draw's twins through the ring build)),
`rd_vuprepass_selftest_skincheck` (`rd_vuprepass_selftest.cmake`: the check on for every skinned
command around the pre-pass's self-test, no check of a frame and no end-of-run count logged: the
self-test's replays are left out)
and `rd_vu_skin_check_dump.cmake` (`rd_vu_skin_check_dump`, `rd_vu_skin_check_163`,
`rd_vu_skin_check_163_probe`, `rd_vu_skin_check_163_ring`, `rd_vu_skin_check_163_patched`, `rd_vu_skin_check_123`,
`rd_vu_skin_check_123_nan`, `rd_vu_skin_check_123_attach`, `rd_vu_skin_check_123_nan_attach`,
`rd_vu_skin_check_123_c3`, `rd_vu_skin_check_123_nan_c`, `rd_vu_skin_check_123_bones_ring`,
`rd_vu_skin_check_123_bones_ring_nan` on issue 123's blended dump
`frame-20261106-080033-v3910-blended`), `rd_vu_bones_ring_ab.cmake` (`rd_vu_bones_ring_ab`: the
three dumps of `/primary/dev/ico-dumps/163/` under the plain set with the pre-pass off, the bones
through the ring against VuBoneCB, and both with the check on for every skinned command (column 3
and twin C drawn, the twins through the ring), every picture byte for byte;
`rd_vu_bones_ring_ab_prepass` the same with the pre-pass on, its batch builds on and off),
`vu1_test` (the ring builds' probe
and pictures as the plain variants'), `bones_ladder` (the ladder over sequences of starts, step 2's threshold, the re-test removed or
needed, the unmarked nobin taken as a re-test, the mark and its words, the lines word for word),
`window_scene` (the VU line's twins part), `host_config` (the keys), and
`rd_vu_skin_check_ab.cmake` (`rd_vu_skin_check_ab`: the pictures with the check on for every
skinned command against off, byte for byte, at Original, with the attachment read and GS state as
shader data, and with MSAA, the twins drawn in every on run and through the draws' own pipelines in
the attachment-read runs), `share_report` (the newest sidecar in the share list);
[TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md).
