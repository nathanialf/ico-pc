# The skin check

The skinned draws' bone-block check (issue 163): on every Vulkan device the game runs on, the
renderer draws one skinned command again every 100 ms as a probe beside the picture and compares
what the GPU computed with the CPU's copy of the same program, so a reporter's log names the layer
that placed a skinned vertex wrong without anything to switch on. Nothing it draws reaches a
picture.

Why it exists: issue 163's streak on Mesa's Turnip driver on an Adreno 740 was one draw of Yorda's
mesh in which every vertex weighting bone address 200 was placed as if that bone's matrix (VuBoneCB
qwords 184..187) had read as zeros, while the bytes the CPU wrote were right; a copy of the
reporter's dump with only those weight lanes moved to a zero bone replays on lavapipe to within
3,130 of 6,193,536 pixels of the device's picture (the unpatched replay differs by 45,934). Which
layer zeroed the read, the driver's uniform read, the ring's bytes under the GPU or robust access
alone, a replay cannot tell. The check tells it from the device itself.

## Files

| path | what it holds |
| --- | --- |
| `port/render/rd_vu_skin_check.h` | the check's API: the judgement's pure functions, the replay's hooks, the tool's entry |
| `port/render/rd_vu_skin_check.c` | the in-game check (S2) and the box check (S3): the pick, the note, the probes, the harvest, the lines |
| `port/render/rd_vu_skin_judge.c` | the judgement (pure): the CPU's positions through `vu1_ref`'s cluster program, a probe against them, the zero-matrix address, the byte windows, the decision table's words |
| `port/render/rd_vu_skin_selftest.c` | the start-up probe (S1) and the check's start line |
| `port/render/rd_replay_vu.c` (`doVuDraw`, `vuDraw`) | the pick before a skinned command's draw, the note after it |
| `port/render/rd_replay.c` (`rd__wait_frame`, `replayFrame`) | the harvest once the slot's fence let the replay go; the probes after the replay's last pass |
| `port/rhi/vk/vk_pipeline.c`, `vk_device.c` | `RhiPipelineDesc.uniformRobustOff`: `VK_EXT_pipeline_robustness`'s uniform-buffer robustness off on one pipeline, enabled where robust access is on and the device offers it (`RhiLimits.uniformRobustOff`) |

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
nothing), inside an occlusion query whose scissors cover the draw's scissor less the CPU's box of
its vertices widened by 2 GS pixels (four bands). Any sample means the GPU placed a vertex where the
CPU does not, through the real pipeline family, in the scene pass's own execution. Drawn only where
the box is closed (every vertex of the draw's batches in front of the eye: a clipped corner behind
it reaches anywhere), the pass an ordinary one (no storage pass, no DATE through the stencil bit, no
stencil mode) and its pipeline made (compiled in the background at its key's first check: no
hitch).

**S1, the start-up probe.** An early warning, not the decider. A VuBoneCB of 60 distinct bones (each
a turn, a scale, a shear and a move of its own) and 120 vertices whose weight qwords name every
address 16..252 in both lanes (lane x the vertex mod 60, lane z (7 v + 13) mod 60) with mixed
weights, drawn in probe mode through the cluster entries of codes 20, 22 and 24 (the plain set's
where the fallback takes it) with the block at 64 ring offsets, one in each 64-byte phase of a 4 KB
page (a coarser uniform alignment: its own phases), under the device's robust access, in a frame of
its own read back at once; each of the 192 probes against `vu1_ref`. It runs first in
`rd_vuprepass_selftest`, whatever the pre-pass, and writes the check's start line. A wrong probe
turns nothing off.

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
| `H1, address-dependent: the driver's uniform read at that offset` | the bytes equal, the probe wrong, c1 right |
| `H1 under robust access only: the driver's uniform read with robustness on` | the bytes equal, the probe wrong, c1 wrong, c2 right |
| `H1, the driver's uniform read of this draw: wrong at a fresh offset too, and not robust access alone` | the bytes equal, every probe wrong |
| `H1, ... (robustness off not tried)` | c1 wrong, c2 not drawn (robust access off, or the extension not offered) |
| `the probe drew right: a wrong picture would be the scene pass's execution, not the bone read` | the probe right; a box check's samples outside then say the scene pass placed the vertex (the binning pass, the pipeline's link with its pixel shader) |

The conditional fixes each row points to (a storage-buffer read of the bones on Turnip, the
uniform robustness off on the VU pipelines, the ring's slot order) are not built until a log says
which row it is.

## The lines

At start, from `rd_vuprepass_selftest`:

- `rd: VU skin probe: ok (120 vertices x 64 offsets in P page phases, codes 20 22 24, plain set) in T ms`
- `rd: VU skin probe: wrong at offset O (page phase P), code C, plain set: N vertices, address A read as zero (got (x, y, z) want (x, y, z)) in T ms`
  (or `..., no zero matrix explains them ...`; `not run (<why>)` when its pipelines or frame could
  not be made)
- `rd: VU skin check: on (one skinned draw every 100 ms, in turn, ...; variants: a fresh ring offset, uniform robustness off (VK_EXT_pipeline_robustness) | not offered | not needed (robust access off); the box check by occlusion query | not offered; [dev] vu_skin_check = false turns it off)`
- on a window's Vulkan device with the check off: `rd: VU skin check: off ([dev] vu_skin_check = false, ICO_RD_VU_SKIN_CHECK=0)`

On an event (a wrong probe, or the block's bytes differing), the first 8, then every 100th:

- `rd: VU skin check: frame F command L:I mesh NAME #ID (V vertices): N vertices wrong of M, address A: got (x, y, z) want (x, y, z) (a zero matrix at A gives it: yes|no; it gives K of them); the bone block as the GPU copied it: equal | differs at qwords Q..R; as the CPU reads it after the fence: equal | differs at qwords Q..R | not read (...); code C through MODULE (event E)`
  (X and Y in GS pixels, absolute; Z the GS Z; A the address whose zero matrix gives the most wrong
  vertices, else the first wrong vertex's lane x)
- `rd: VU skin check: frame F command L:I variants: (c1) the block at a fresh ring offset (page phase P, the draw's D): right|wrong (N wrong); (c2) uniform robustness off: right | wrong | not run (robust access off) | not offered (no VK_EXT_pipeline_robustness) | not run (its pipeline not made yet) (N wrong); reads as: WORDS`
- `rd: VU skin check: frame F command L:I mesh ...: every vertex right (M compared); the bone block as the GPU copied it: ...; reads as: WORDS (event E)` (bytes that differed, every vertex right)
- `rd: VU skin check: frame F command L:I drew N samples outside its box (texels X,Y WxH, the CPU's vertices + 2 GS pixels; the probe right|wrong too) (event E)`

In the 10 s summary, when the check ran: `rd: VU skin check: P checks, W wrong, B blocks whose bytes
differed (R changed after the draw); Q box checks, S with samples outside the box; variants on the
wrong ones: c1 right K, c2 right L (c2 drawn in M checks); largest difference on a right vertex
D/16 pixel`.

## Switches

- `[dev] vu_skin_check = false` (`ICO_RD_VU_SKIN_CHECK=0`): off, for an A/B
  ([CONFIG.md](CONFIG.md)). On by default on a window's Vulkan device; off in a headless run
  unless `ICO_RD_VU_SKIN_CHECK=1` (the tests) or `rd_replay_tool --skin-check`. Not on Direct3D or
  OpenGL.
- `ICO_RD_VU_SKIN_SABOTAGE=A` (tests): the check's own probe of the draw's block reads the bone at
  address A as zeros while the draw's bytes are right (H1's form); `A,ring` zeroes the bone in the
  ring after the check kept the bytes, so the draw and the probe both read zeros (H2's form).
- `rd_replay_tool DUMP OUT.png --skin-check` ([RENDER.md](RENDER.md)): every skinned command of the
  dump checked (80 a replay, each read back at once, the box checks waiting for their pipelines),
  the lines above on stderr, then `<dump>: skin check: N skinned commands, C checks, W wrong
  (address A), B blocks whose bytes differed (R changed after the draw), X of Y box checks with
  samples outside; on the wrong ones c1 right K, c2 right L (c2 drawn in M); reads as: WORDS`. A
  is the address the last wrong check named, WORDS that check's decision (a check whose bytes
  differed but whose probe drew right, a block whose changed bone the mesh does not use, gives its
  words only while no check was wrong).

What it cannot see: the check compares the device with the CPU over the same stream and bytes, so
a dump whose stream was patched (the deep dive's copy with the lanes on 200 moved to a zero bone)
checks right everywhere: both sides read the patched stream. On lavapipe the sabotage stands in for
the device's wrong read.

## Tests

`rd_vu_skin_check_test.c` with `rd_vu_skin_check.cmake` (`rd_vu_skin_check_judge`,
`rd_vu_skin_check`, `rd_vu_skin_check_robust`, `rd_vu_skin_check_probe`, `rd_vu_skin_check_ring`)
and `rd_vu_skin_check_dump.cmake` (`rd_vu_skin_check_dump`, `rd_vu_skin_check_163`,
`rd_vu_skin_check_163_probe`, `rd_vu_skin_check_163_ring`, `rd_vu_skin_check_163_patched`);
[TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md).
