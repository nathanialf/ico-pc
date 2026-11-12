# The renderer's performance records

The per-replay performance record, its GPU timestamps, the 10 s lines built from it and the measurements kept from the phones. The rest of the subject is [RENDER.md](RENDER.md)'s.

A record (`RdPerfRecord`) is filled while a replay runs (its CPU phases and counts), closed with the
RHI's counters, and then waits in a slot per frame in flight until its GPU timestamps can be read
without waiting. The timestamps carry the effect whose work they end, so the record has a GPU time
per list, for the uploads, for the VU vertex pre-pass (`gpuPrepassMs`, from `RD_PERF_TS_PREPASS`
after the uploads to list 0: its passes and, under the render targets, its copies; under the
vertex stores the passes alone; both stamps are bottom-of-pipe, so the first waits for the
replay before as well, and a record that ran beside the replay before's fragment work, which the
barriers allow, shows as a pre-pass near 0: [RHI.md](RHI.md#buffer-barriers-scopes);
`RD_PERF_TS_LISTS` also sends the barriers held back from the record, so its end does not join
list 0's first pass barrier), for the present, and per picture effect
(`RD_PERF_POST_*`: reduction,
fog, motion blur, aura, glow, depth of field, softening, CRT, MSAA resolve, crack fill)
(`port/render/rd_perf.c`; `port/render/rd.h` (`RdPerfRecord`, `rd_perf_pop`)). An effect is the
one the colour target in force belongs to (the softening's and the aura's targets) or the one the
last post sprite names, and a post sprite's ends with its list: the next list starts with its
target's effect, so the motion blur, list 7's last sprite, is the sprite alone and lists 8 to 11
are no effect's (`port/render/rd_replay.c` (`perfTargetPost`, `Replay.perfTargetEffect`, the list
loop)). The same effect (`Replay.perfPost`) also decides what counts as a glow pass (`glowPasses`,
below) and labels each pass in the device-loss pass history (`passPush`), so both follow that
rule too: a pass begun in a later list is counted and labelled by its target's effect, not by the
last post sprite of an earlier list. Nothing drawn depends on it. The window sums them
into its 10 s statistics line, and `[dev] perf_log` writes one line per replay
([DISPLAY.md](DISPLAY.md#the-perf-csv)). The [replay tool](ARCHITECTURE.md#terms)'s `--stats` prints
the same record.

The record also carries the GPU's time before its replay (`gpuGapNs`: its first stamp less the
last stamp of the timed replay before it, the same GPU clock) and, issue 123, how many replays
without timestamps came between (`gpuGapSkipped`: a record whose slot was recycled before its stamps
were read is queued without them, and those are the stall's neighbours); the gap is kept across
them, then holding their GPU work too, and refused only past 60 s either way (`rd_perf_gap_step`,
`port/render/rd_perf_gap.h`; a new device starts it again). A replay whose first stamp is before the
first stamp of the replay before it is not a gap but a restarted clock (the Adreno's counter at a
stall, issue 123: the stall lines printed the counter's run time as "begun 20699.4 ms before the one
before ended"): no gap is measured and `gpuGapBackNs` holds how far back it went
(`rd_perf_gap_step_ex`). The present is timed apart where the
backend can (`presentSplit`: 1 with the driver's call, 2 the lock's wait only, no present call
made; `presentLockMs`, `presentCallMs`, `presentPollMs`: the swapchain
lock's wait, `vkQueuePresentKHR`, the surface poll, `rhi_present_split` on Vulkan) with its end on
the renderer's monotonic clock (`presentEndMs`, the clock of `startMs` and of the window's kernel
GPU driver samples), carried from the submit thread's job (`RdSubmitJob`, `rd__perf_submit_times`)
or filled by the render thread's own present (`rd__present_split_take`). The window's lines around
a present that blocked read them ([DISPLAY.md](DISPLAY.md#the-stats-lines)).

A present's interpolation time (`interpMs`) is measured around `rd__interp_frame` by `rd_present`
and charged to the replay that follows (`port/render/rd_perf.c` (`rd__perf_interp_ms`);
`port/render/rd_interp.c` (`rd_present`)); every present does all of its work at its alpha.

The record also counts the exact blend's cost and the glow's passes (issue 103): `dstDraws`, the
draws recorded through a destination-read shader (`dstBind`); `storagePasses`, the passes begun
without the colour attached; `dstSwitches`, the passes ended and begun again on the same targets
to change between a storage pass and one with the colour (`dstPassMode`; 0 on the attachment read
and the fixed-function blend); `glowPasses`, the render passes begun while the glow (flare, bloom,
the sun's eye blur) draws; `snapshots` and `snapSwaps`, target snapshots copied and the staticBlur
sprites' swaps ([above](RENDER_POST.md#post-passes)); `a1xDraws`, the draw passes that took the above-1x kernels
(the box, the B-spline, the fog's table between its entries: [Above 1x](RENDER_POST.md#above-1x); each planned
pass of a draw counts, so a draw split in two by the AFAIL split or the two-pass blend counts
twice, and the line's "draws resampled above 1x" are such passes), 0 at scale 1. The
window's per-replay line carries them after the exact blends:

```
window: per replay 1803 draws, 61 passes, ..., 0.0 DATE snapshots, 0.0 exact blends, 412 destination-read draws, 28 storage passes, 27 pass switches for the exact blend, glow 40 passes, 6.0 snapshots copied and 4.0 swapped, 3.0 draws resampled above 1x, VU pre-pass 5120 vertices in 180 draws (0.0 draws the old way); in the block ...
```

(the numbers there show the fields, not a measurement). The GPU line before it carries the glow,
fog and the other effects' milliseconds; the two together say what a slow card spends its replay
on. `--stats` prints the counts as a second line, `<dump>: stats: D destination-read draws, S
storage passes, P pass switches for the exact blend; glow N passes; snapshots C copied, W swapped;
A draws resampled above 1x`, and the VU vertex and triangle pre-passes as a third, `<dump>: stats:
VU pre-pass on: 2 passes, V vertices in D draws; F mesh and grid draws the old way; S sprites, T
triangle slots; K VU draws with the ticks' cameras` (`vuPrePasses`, `vuPreVertices`, `vuPreDraws`,
`vuPreFallbacks`, `vuPreSprites`, `vuTriSlots`, `vuTickDraws`: the draws a frame blended between
two ticks draws under the ticks' cameras, `port/render/rd_replay_vu.c` (`doVuDraw`);
[The vertex and triangle pre-passes](PIPELINES.md#the-vertex-and-triangle-pre-passes)), with the
record's write after it, `<dump>: stats: VU record stores: stored by the vertex shaders into the
frame slot's record buffer: ...; pre-pass skipped N frames under M slots (the largest L)` (`targets`
and its words under the render targets, `compute` and its words under the compute dispatches; the
skips are `[dev] vu_prepass_min`'s, `port/render/rd_vu_record.c`); the first stats line counts the
replay's compute dispatches after its copies (`..., C copies, N dispatches; screen prims ...`:
`RdPerfRecord.dispatches`, `RhiStats.dispatches`, the record's under the compute dispatches, 0
otherwise);
the GPU line (`window: GPU ms per replay: ...`) ends with `; VU pre-pass <ms>`, as `--stats`'s
`gpu:` line does
(`port/render/rd.h` (`RdPerfRecord`); `port/platform/window_host.c` (`perf_log`)).

The pixel diet's six counts are in the record too: `alphaOnePass` and `alphaOpaque` (draws the alpha plan
drew in one pass where the plan had two, and without the test and the blending), `fxConstant`,
`fxAdditive` and `fxLocal` (the post sprites drawn by the constant fold with the black sprite's clear, the
premultiplied forms and the local read: [Post passes](RENDER_POST.md#post-passes)) and `vuNoPlanes` (the VU passes drawn
through the no-planes pixel shaders: [Screen planes](RENDER.md#screen-planes)). Each is 0 with its flag off or where
its mechanism does not apply. `rd_replay_tool --stats` prints one line, `<dump>: stats: pixel diet: alpha plan
N one-pass, M opaque; fx plan C constant, A additive, L local; vu no-planes D`, and the 10 s block prints
`window: pixel diet per replay: alpha plan N one-pass, M opaque; fx plan C constant, A additive, L local; vu
no-planes D` after the `window: per replay` line, every block
(`port/render/rd.h` (`RdPerfRecord`); `port/render/tools/rd_replay_tool.c`; `port/platform/window_host.c`
(`perf_log`)). With the profile line (`rd: profile: ... alpha plan on; fx plan on; fx local read ...; VU
planes ...`) the two say from a log alone which of the steps a device took.

### The per-pass timing

The list and effect stamps are written where a list or an effect changes, which is often inside
an open render pass (a pass on SCENE runs on across lists 0 to 2 and into list 4). A GPU that
draws a pass's pixels after the pass ends (a tile-based phone GPU; lavapipe) then puts that work
in the stretch where the pass ends, not where it was recorded (and a driver may end the pass at
the stamp and begin it again, loading and storing the target once more). Measured on lavapipe with the
issue 102 Mac frame at Original 1x: the fog effect reads 39.6 ms, while removing the fog's command
(`--nop 4:17`) leaves the frame's total where it was (248.6 against 248.7 ms) and removing it with
the sprite after it (`--nop 4:17 --nop 4:23`) takes 6.8 ms off; the list-4 stretch still reads
33.4 ms with both gone. So a list's or an effect's time on such a GPU is not its own cost.
`[dev] perf_list_stamps = false` (`ICO_RD_PERF_LIST_STAMPS=0`) writes only the stamps outside
every pass (the replay's start, the pre-pass's, the lists' start, the present's end), logging
`rd: GPU list and effect timestamps: off ...` once: the lists' and effects' times read 0 while the
GPU total of a present and the per-pass timing stay, so a phone's `window: N replays: ... GPU N
ms` with and without it says what the stamps inside passes cost there (`port/render/rd_perf.c`
(`stamp`)).

The per-pass timing measures each piece of GPU work between stamps written outside any render
pass. The replay names each piece as it begins it (`rd__perf_pass`, before the piece records
anything): the uploads at the head (`uploads`), the VU vertex pre-pass and its copies
(`VU pre-pass`), every render pass of the lists (`render pass`: `beginPass`, a clear's own pass,
the COLCLAMP 0 wrap's, the shadow count's one draw and its reduction above 1x, both named
`shadow count WxH` in the lists below, not the count target's `list 3 temporary`:
`RD_PERF_ID_SHADOW_COUNT`, [WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-count-in-one-draw)), a target copied into another
(`copy`, `doCopy`), a target copied into its snapshot (`snapshot`, `takeSnap`), the DATE
snapshot's pass (`DATE snapshot`, `dateSnapshot`), the fog's depth read made ready (`fog depth
read`: the copy path's copy, the buffer path's transfers, the table's upload, before the fog's
pass), the exact blend (`exact blend`, `doExact`), the MSAA epoch's spreads and resolves (`MSAA
resolve`, `msaaUpsample`, `msaaEnd`), a shadow reset's stencil clear as a pass of its own
(`stencil clear`) and the present (`present`: DISPLAY onto the window and the CRT filter), with
the display wait before it (`display wait`, at the window's size: no pass, the stretch from the
scene list's last command to the present's own list's first, [below](#the-presents-own-list))
(`port/render/rd.h` (`RD_PERF_PASS_*`, `RdPerfPass`); `port/render/rd_replay.c`,
`port/render/rd_replay_vu.c` (`rd__vu_prepass`)). Every replay counts its pieces by kind and its
render passes by effect (`RdPerfRecord.passKinds`, `passEffects`). A timed replay also writes a
timestamp at each piece's start, `RD_PERF_TS_PASS0` (32) plus the piece's index, keeping the
barriers the backend holds back held (so the barriers recorded are an untimed replay's), and one
after the last piece (`rd__perf_pass_end`, before the last list ends); a piece's GPU time is its stamp
to the next one's, so it includes the barriers before its first command and nothing of another
piece. Its draws and pipeline binds are the RHI's counters between the two, its CPU time the
replay's recording of it. Each frame slot has 256 timestamps (`RHI_MAX_TIMESTAMPS`,
[RHI.md](RHI.md#the-interface)), so a replay times up to 223 pieces; a replay that begins more times
the rest with the last and says so (`passPartial`). The pieces wait for their times with the
record and then in a queue of 8 replays for `rd_perf_passes_of` (`port/render/rd_perf.c`, "the
passes").

Which replays are timed: every replay with `[dev] perf_passes = true` (`ICO_RD_PERF_PASSES=1`,
[CONFIG.md](CONFIG.md)); otherwise the first replay at least 250 ms after the last timed one
(`RD_PERF_PASS_EVERY_MS`), at most 40 in a 10 s block, for the window's summary, until the first
present that blocks over 50 ms on Vulkan (issue 123): from then on every replay is timed until a
stalled one is compared or 3 blocks pass with none (`rd_perf_passes_arm`, no CSV; the window says so once, `window: every replay
timed pass by pass from here on Vulkan (a present blocked 359 ms at replay 2941): the next stalled
replay's passes are compared with the block's`), so the next stalled replay has its pieces. Only a
record that carries the present split arms it (`rd_perf_passes_arm_backend`: the Vulkan backend,
MoltenVK included): never on OpenGL, where every stamp of a timed replay reads the GL clock
(`glGetInteger64v(GL_TIMESTAMP)`, which may hold the CPU until the driver has taken the commands
before it), nor on Direct3D. Not on iOS (as the effect stamps). At start the renderer says which:

```
rd: GPU pass timing: one replay every 250 ms, each pass timed apart, for the 10 s summary; [dev] perf_passes = true times every replay
rd: GPU pass timing: every replay, each pass timed apart ([dev] perf_passes = true, ICO_RD_PERF_PASSES=1; the window writes ico-pc-passes.csv)
rd: GPU pass timing: off (no GPU timestamps)
```

The window's 10 s block has three lines from it, after the `GPU ms per replay` line
(`port/platform/window_host.c` (`perf_log`, `perf_passes_add`); `port/render/rd_perf.c`
(`rd_perf_pass_agg_text`, `rd_perf_pass_kinds_text`)):

```
window: GPU top passes: list 0 SCENE 848x448 420 draws 4.12 ms, glow WORK1 256x128 10.0 passes 10 draws 1.21 ms, ... (5 of 47.0 passes a replay, 16 timed replays; passes under 0.05 ms: 31.0 a replay, 0.62 ms)
window: passes per replay 47.0: render passes 32.0 (lists 9.0, reduction 1.0, fog 1.0, glow 17.0), uploads 1.0, VU pre-pass 1.0, copy 15.0, snapshot 0.5, DATE snapshot 1.5, fog depth read 1.0, present 1.0
window: CPU walk ms per replay by list 0-12: 1.10 0.05 ... 0.20; uploaded KB per replay: textures 57, meshes 27 (interpolated 20), uniform blocks 820, VU streams 30, other 61
```

(the numbers show the fields, not a measurement). A timed replay whose present blocked 250 ms or more
also gets, before its pieces join the block's, `window: stalled replay 2941 (present 352 ms, GPU
71.3 ms against the block's 27.0): the first pass 52.1 ms (the block's 1.2: it waits for the
display's image), the other 46 timed passes 27.0 ms (the block's 25.8); list 0 SCENE 3048x2032 52.1
ms (the block's 1.2), ...; 31 of
45 passes over 2x their block mean, the rest within 1.3x; 2 under 0.05 ms left out (16 timed
replays in the block so far)` (the first 8, then every 100th; `rd_perf_pass_grow_text`,
`rd_perf_pass_agg_mean`; `port/platform/window_stall.c`): its display wait apart (`the display
wait 38.4 ms (the block's 20.1), the other 46 timed passes ...`: the present's own list alone waits
for the window's image), or in a record without one its first render pass, since the GPU's wait
for the display's image (the acquire's semaphore at the first colour write) landed inside that
pass's stamps before the present had a list of its own, against the other timed pieces summed with the sum of their block means (the
first pass grown alone is the display holding the image, not a slow GPU), then its five most
expensive pieces each with
the block's mean of a piece like it (`(new to the block)` for one the block has none like), then
how many of its pieces with a block mean of 0.05 ms or more took over twice that mean, and the
largest ratio of the rest. Every piece grown about alike is the GPU running slow as a whole (its
clock, another client between its passes); one or a few grown by hundreds of ms is one piece of
work the GPU stopped in (a fault's recovery); none grown with the replay's span long is time
outside the pieces, before the first (the display's image) or after the last. The top passes are the timed replays' pieces
summed by what they are (label, target, size; the label is the effect for a render pass of one,
`list N` for another render pass, else the kind), the five most expensive, each per timed replay
with its passes (where it is not one a replay) and draws; the tail says how many pieces a replay
has and how many of them took under 0.05 ms, the size of a tile-based GPU's fixed cost of a pass.
The second line is the count by kind for every replay (no timestamps needed), from which the
fixed part of a tiler's frame can be worked out. The third splits the walk's CPU time by list
(each list from the lists' start or the list before to its end, binds included:
`RdPerfRecord.walkListMs`) and the upload ring's bytes by what they carry (`uploadTextureBytes`,
`meshUploadBytes` and of it `uploadInterpBytes`, the interpolation's scratch meshes,
`uploadUniformBytes`: every DrawCB, VuCB, VuBoneCB and FrameCB written (`uniformGroup`,
`rd_replay_vu.c` `cbCopy`), `uploadStreamBytes`: the VU streams without a mesh and the grids'
index lists; other: the screen prims' vertices, the fog's table, alignment). With `[dev]
perf_passes` the window also writes every piece of every replay to `ico-pc-passes.csv`
([DISPLAY.md](DISPLAY.md#the-perf-csv)). The replay tool's `--pass-times` (with `--stats`) times
the warm replay pass by pass and prints one line a piece, `<dump>: pass N: <label> <target> WxH,
list L, D draws, B pipeline binds, GPU <us> us, CPU <us> us`, then `<dump>: gpu top passes: ...`
and `<dump>: passes ...` as the window has them (`port/render/tools/rd_replay_tool.c`).

#### The present's own list

A presenting replay records two command lists: the scene's (the uploads, the pre-pass, lists 0
to 12) and the present's pass alone (`port/render/rd_submit.c` `rd__submit_frame`). The scene's
list ends on the render thread (`rhi_end_commands` is a slot call,
[RHI.md](RHI.md#the-interface), "Frame slots and the submit thread") before the present's begins, and both go to
the submit thread's job (`RdSubmitJob.cl`, `cl2`), whose tail ends and submits them in that order
and then presents (`rd_submit_queue.h` `rd__sq_tail_steps`; the inline path runs the same steps).
On Vulkan the acquire's semaphore is waited on by the submit of the list that uses the window's
image only (`port/rhi/vk/vk_cmd.c` `vkr_submit_batch`, `VkrCmdList.swapImage`): a semaphore wait
holds `COLOR_ATTACHMENT_OUTPUT` for every command of its batch, so with one list the scene's
off-screen passes waited for the presentation engine too, and on issue 179's Mali that wait was
38 to 84 ms of every 88 to 143 ms scene picture, in whichever pass wrote colour first. Now the
scene's passes run while the presentation engine still holds the image, and only the present's
pass waits. Direct3D and OpenGL submit the two lists as they come (two submits a picture were
already normal there). The pixels are those of one list: the second list's opening barrier orders
it after the first, the DATE and MSAA epochs closed before, and the present reads DISPLAY through
barriers of its own. At start the renderer says so:

```
rd: present: the present's pass is a list of its own, submitted after the scene's; only its submit waits for the display's image (Vulkan: the acquire semaphore at COLOR_ATTACHMENT_OUTPUT)
```

and, should the backend have no list left for the second (`VKR_MAX_CMD_LISTS` a frame slot),
`rd: present: no command list left for the present's own list (N times so far): this picture is
not presented` (the first, then every 100th; its image is the next acquire's). The display wait is
`RdPerfRecord.gpuDisplayWaitMs`: from the scene list's last stamp to the present's list's first
(`RD_PERF_TS_PRESENT_BEGIN`), inside the replay's span (`gpuMs`; the pace policy reads the span less this wait,
`rd_perf_pace_gpu_ms`, so the wait for the screen's image does not count against the GPU:
[DISPLAY.md](DISPLAY.md)), and the `display wait` piece of a timed replay
(`display wait window WxH N ms` among the top passes). It holds the wait where the GPU writes that
stamp only once its batch's wait is over (a driver that waits whole batches); where the wait holds
colour output alone, as issue 179's Mali log shows (its first stamp came before the wait, which
sat in the uploads piece when the picture was one list), the stamp is early and the wait shows in the `present` figure instead:
a present far above its usual 2 to 4 ms there is the display's image. The window's replays line
carries both (`... 11-12 1.0, display wait 38.4, present 2.0`), the per-replay counts the queue
submits (`submits 2.0`: the two lists), the CSV `gpu_display_wait_ms` and `submits`
([DISPLAY.md](DISPLAY.md#the-perf-csv)), and the replay tool's `gpu:` line `display wait` before
`present`. Without the list stamps (`[dev] perf_list_stamps = false`) the present's list's stamp
is not written and the display wait reads 0 (the present figure keeps the lists, as before).

### The pixel cuts' records

The [integer forms](SHADERS.md#integer-forms) (REPEAT's mask, `fx_sprite_ps`'s division and its
coverage at scale 1) give the same bytes and shorten the executed path of `fx_sprite_ps`,
`sprite_texa_ps`, `vu_texa_ps` and the GS-mipmapped sprite entries. Their timings, from
`rd_replay_tool DUMP out.png --stats` on lavapipe (Mesa 25.0.7, `ICO_RD_BLEND=fixed`): the issue 95
title frame (`loss-20261015-100428-lost.rddump`, Original 1x 4:3) and the issue 102 Mac frame
(`frame-20261015-161933-v4251.rddump`, 688x448 16:9). The tool without the cuts (8c97d3cd8) and the
tool with them ran interleaved, three pairs per setting on the same host, the fastest of each: a
lavapipe timing is wall time on shared cores, so only an interleaved pair is comparable (a first
"after" run taken beside other work came out 2.4 times slower than its "before" on the Mac frame).
GPU milliseconds a replay; lists 0, 3, 4, 7 and 11; the effects glow and fog. The motion blur's
effect time is not comparable across the pair: without the cuts it took lists 8 to 11 with it
(the attribution ended at the next TARGET, [above](RENDER.md#performance-records)); list 7 holds the
sprite on both sides.

| frame, setting | without the cuts: total; lists 0, 3, 4, 7, 11; glow, fog | with the cuts |
| --- | --- | --- |
| title, Original 1x | 131.9; 43.6, 11.4, 8.6, 60.6, 2.0; 53.4, 8.5 | 123.2; 48.1, 11.6, 5.3, 50.6, 1.9; 44.4, 5.2 |
| title, Original 1x, `ICO_RD_NO_DUAL=1` | 158.4; 56.0, 16.8, 10.6, 62.9, 4.2; 55.1, 10.5 | 150.5; 59.8, 20.4, 9.8, 48.5, 4.7; 42.7, 9.8 |
| title, `--enhanced --resolution 2x --present 1376x896` | 467.9; 47.1, 55.5, 30.1, 308.3, 8.2; 277.9, 30.0 | 416.0; 45.4, 63.2, 24.2, 257.2, 7.4; 234.3, 24.2 |
| Mac, Original 1x | 284.9; 77.6, 15.3, 67.9, 68.4, 36.3; 57.3, 67.9 | 247.0; 90.4, 14.8, 39.6, 51.0, 36.7; 43.2, 39.6 |
| Mac, `--enhanced --resolution 2x --present 1376x896` | 809.2; 89.5, 70.7, 120.2, 343.1, 141.3; 303.1, 120.2 | 686.6; 74.7, 75.0, 72.1, 290.6, 132.8; 250.3, 72.0 |
| Mac, the same with `--glow-original` (those tools' work buffers at their GS sizes) | 771.8; 77.5, 71.6, 124.0, 322.7, 135.1; 283.6, 123.9 | 666.7; 79.6, 77.0, 70.2, 260.4, 134.4; 229.2, 70.1 |

On this proxy the glow chain (list 7) runs 16 to 25 percent shorter, the fog's list 4 (the fog
sprite and the depth copy's read) 20 to 45 percent, the whole frame 5 to 15 percent; the 3D
lists 0 and 3 move inside the run-to-run spread (no vertex shader changed; list 0's draws are
the same). The alpha-test merge (the AFAIL split under [Pipelines](RENDER.md#pipelines)) is in the "with" column
too: 9 fewer draws on the title frame, 42 on the Mac frame, the pictures byte for byte the same.

The module sizes (`tools/check_spirv.py --sizes`, words / instructions / blocks):
`fx_sprite_ps` 8,536 / 2,058 / 350 without the cuts, 9,323 / 2,260 / 390 with them;
`sprite_texa_ps` 10,181 / 2,401 / 378 and 11,501 / 2,761 / 468; `vu_texa_ps` 10,346 / 2,443 /
384 and 11,666 / 2,803 / 474; `sprite_ps` 7,548 / 1,792 / 269 and 7,812 / 1,864 / 287. The
modules grow (each REPEAT coordinate keeps both forms behind its branch) while a pixel runs
fewer instructions: no `OpSDiv` in `fx_sprite_ps` (a division is 19 straight-line instructions,
one of them a float divide), no `OpSRem` on a power-of-two size, no float divide at scale 1.

The two per-draw lookups hash 8 bytes a step (`port/render/rd_hash.c` `rd__hash_words`: the
uniform blocks' cache, `rd_replay.c` `uniformOffset`, every DrawCB of 144 bytes; the pipeline
index, `rd_pipeline.c` `keyHash`, every draw's key): a 64-bit multiply and xorshift a word where
FNV-1a took a multiply a byte, 18 dependent steps for a DrawCB in place of 144. The hash only picks
a slot (a hit is still `memcmp` or `keyEqual`), so the bytes every draw reads are the same on every
device; which blocks share a slot moves, so a replay may write a repeated block to the ring once
more or once less (`uploadUniformBytes`, the ring's bytes). No `DIVERGENCES.md` row. Its saving is
an estimate (about 0.3 to 0.7 ms a picture in the cage at the phone's 700-800 MHz, the design's
C.2) until the CPU phases' line below measures the walk.

### The CPU phases

The 10 s line `window: N replays: CPU ...` splits a picture's render-thread time into interp, upload,
walk, bind and submit; `port/render/rd_cpu_phase.c` splits the first three further, for the next
CPU cuts to choose among (the design's C.3), in one line after the late passes' and the aura
paste's (`rd_cpu_phase_line`, `port/platform/window_host.c` `perf_log`):

```
window: CPU phases ms a picture over N pictures (R a tick): interp copy C, index I, blend B, morph M, unmatched U, hash H (K of the pictures hashed), the rest F; upload cull X, ring G, acquire and begin A, join J, meshes E, scans: layer L, fades D, the store plan's walk S, present plan P, clears K; walk pre-pass V, late uploads T, lists W; per picture P pipeline look-ups (H hits), B uniform blocks looked up (Z hits); 22.0 clock reads a picture
```

- interp (`rd_interp.c` `rd__interp_frame`): copy is `copyFrame`, the snap test and the keyed draws'
  count; index `buildIndex` and `indexCur` (the pairing's tables); blend the cameras' set-up and
  every draw's `matchOf` and blend (the pairing's per-draw half is inside blend: splitting it would
  take a clock read a draw); morph the other mesh draws' streams; unmatched `unmatchedPass`; hash
  `outHash` on a still picture (K: how many took it); the rest `feedback`, the held glow and the
  velocity's attach;
- upload (`rd_replay.c` `replayFrame`): cull `rd__vis_frame`; ring the overlay's collection and the
  ring's size; acquire and begin the present's acquire, the command list and the late uploads'
  reserve; join `rd__join_parts`; meshes `rd__upload_meshes`; the scans `rd__layer_scan`,
  `rd__fades_fold_scan` and the store plan's one walk with every consumer (pass area, pass cut, the
  glow's fold and light plan, DATE's cut, the shadow box, the window with no volume, the late
  pass, the volumes' merge); present plan `rd__present_scene_begin`; clears
  `rd__clear_new_targets`;
- walk: pre-pass `rd__vu_prepass`'s recording, late uploads `rd__upload_late`, lists the lists to
  `rd__store_plan_end` (bind time inside them, as in the CPU line);
- the per-draw look-ups: `rd__get_pipeline`'s index probes and hits, `uniformOffset`'s cached
  blocks (256 bytes or less) and hits; R is pictures over the distinct frames replayed.

The cost of the timers: a clock read (`rd__now_ms`: `clock_gettime(CLOCK_MONOTONIC)`,
`QueryPerformanceCounter` on Windows) at each group's start and each phase's end: 8 an
interpolated picture, 11 an upload, 3 a walk, the line's last figure; none a draw. A read is tens of
nanoseconds on the phones' vDSO clocks [not measured here], so under a microsecond a picture; the
look-ups are a plain increment each, folded into the 10 s atomics once a picture.

A shadow window with no volume ([RENDER_POST.md](RENDER_POST.md#a-shadow-window-with-no-volume))
fills `noVolumeKept`, `noVolumeOpen` (with the open pass's `noVolumePassW` x `noVolumePassH` and
`noVolumePassDepth`), `noVolumeClears`, `noVolumeLevelTexels`, `noVolumeSkipped`, `noVolumeWith`
and `noVolumeWhy[]` (`RD_NV_WHY_*`, `port/render/rd_no_volume.h`); the window's `; no volume:` clause
and the tool's `stats: no volume ...` line read them. On the corpus title frame rd-00400 at the
phone's recipe the window it takes is 4 passes and about 44 MB of attachment traffic a picture in
the design's `--nop` measurement (a pass less with the four zero clears the code records: see the
section). Off by default in this release (the PowerVR phone flickered with it on, cause not
found); its saving on the phone was not measured.

### Performance notes

**The render thread's reads.** On the PowerVR phone a picture's interpolation, upload and walk took
10.8 ms (4.24, 0.99 and 5.53 in its 10 s line), more than the second picture a frame leaves them.
Sampled on lavapipe (the replay tool's main thread, 300 replays of the phone's F12 dumps at its
profile), five things a picture redid that its inputs had not
changed are kept or made once, each giving the same bytes:

- **One walk before the replay.** The store plan, the pass area and the passes cut each walked the
  replayed lists with their state; `rd__store_plan_scan` now applies each command's state once and
  hands the command to `rd__pass_area_scan_cmd` and `rd__pass_cut_scan_cmd` beside its own notes
  (each begun by its `_begin`, false when off or in a pass window, and closed by its `_end`;
  `rd__pass_area_scan` and `rd__pass_cut_scan` are the three parts over a frame, for the tests).
  The fades fold still plans first: the others read its sprites. These walks are timed in the
  record's `upload`, not its `walk` (`replayFrame`: the upload phase ends after
  `rd__clear_new_targets`); together they were about 0.06 ms of lavapipe's 0.27 ms upload.
- **A stream's serial.** Every write of a mesh's stream (its creation, `rd_update_vu_mesh`, the
  interpolation's scratch copy and its blend) gives the mesh a new `RdMeshRec.streamSerial`
  (`port/render/rd_mesh.c` (`rd__mesh_stream_written`)), and what is computed from the stream
  alone is kept while the serial holds. The alpha plan's span of a mesh (`rd_alpha_plan.c`
  `meshSpanMemo`, a cache line a vertex) was read at every replay: 0.28 ms of lavapipe's 1.8 ms
  walk. A skinned mesh's bone pivots (`rd__interp_skin_pivots`) were made at every present: 0.12
  ms. A record with no serial (a test's own) keeps the old rule.
- **The same shape.** `morphDraw` compared a matched mesh's two stream buffers whole at every
  present; the answer is kept by both meshes, serials and buffers (`rd__interp_streams_same`). The
  replay tool loads each dump's meshes on their own, so its `--interp` compares two copies of the
  same bytes for every matched mesh (0.28 ms a present on lavapipe); the game shares one mesh
  between ticks where it did not change and compares only a morphing part's twins.
- **The shadow volumes' pairs.** The part of `blendPrisms` that t does not change (the two ticks'
  prisms by tag, their medians, the banded alignment, the world shift: `prismSet`) is kept with a
  copy of its inputs, and a later present of the tick whose inputs are the same bytes copies the
  pairs back (`prismSetKept`): 0.35 ms a present on lavapipe.
- **The draw-merge count.** `vuMergeNote` (counting only) compares the last VU draw's DrawCB byte
  for byte instead of a 64-bit FNV made one byte a step at every VU draw: 0.12 ms a replay.

The interpolation's summary says how much of that work an earlier present's served
(`port/render/rd_interp_mesh.c` (`rd__interp_keeps_log`), from `presentLog`; the spans counted
since the line before). It is one of the `rd: interp:` lines that `presentLog` writes every 250
interpolated game frames (`RD_INTERP_LOG_FRAMES`, after the `presents drew a mixed picture` line),
not a part of the window's 10 s block, and it is written with zeros too; with interpolation off
(Frame rate Original) there is no such summary and the spans' counts wait for the next one. A
phone's log has it at each summary: `204 of 3000 comparisons of two ticks' mesh streams,
2273 of 9244 skinned meshes' pivots, 67 of 979 shadow volumes' prism pairs; the replays' mesh
alpha spans: 46190 of 54853 kept`:

```
interp: kept from an earlier present (the rest made again): A of B comparisons of two ticks' mesh streams, C of D skinned meshes' pivots, E of F shadow volumes' prism pairs; the replays' mesh alpha spans: G of H kept
```

The blends' per-vertex loops (`lerpFloats`) are vectorised by the Android build's clang at `-O2`
(four lanes, `-Rpass=loop-vectorize`). What is left of the walk on lavapipe is mostly the driver's
command recording (lavapipe's own code on the main thread); the phone's driver is another, so the
phone's share of these cuts is not lavapipe's.

**The UINT views of the RGBA8 targets.** `fx_sprite_ps` reads each texel as a float and turns
it back into an integer (`floor(x * 255 + 0.5)` a channel, `fx_load`), which gives the stored byte
for every byte. A target made with `RHI_TEX_UINT_VIEW` has an `RGBA8_UINT` view of the same image
as well (Vulkan: `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` with the two formats listed and a second,
sampled-only view, `port/rhi/vk/vk_resource.c` (`vkr_create_uint_view`); D3D12: the RGBA8 resources
are typeless already and the view is a UINT SRV, `port/rhi/d3d12/d3d12_pipeline.c`), bound with
`RHI_ASPECT_UINT`. `RhiLimits.uintView` is true on Vulkan and false on D3D12: the D3D12 view is
implemented but has not run on a Windows machine (the CI only cross-compiles it), so no D3D12 target
has it and `[dev] uint_view` asks for it there in vain (`port/rhi/d3d12/d3d12_device.c`
(`dx_fill_limits`)). Which targets have it is the
profile's (`port/render/rd_core.c` (`rd__target_create_gpu`)): the work class under
`RdProfile.uintViewWork`, the scene class under `uintViewScene` (a tiler's driver may give up its
compression on a mutable-format image, so a frame-sized target takes it only where the profile
says), the snapshot as its target (`port/render/rd_replay.c` (`snapEnsure`)). A staticBlur sprite
whose every texture has the view draws through `fx_sprite_u_ps` (`ICO_UINT_SRC=1`: `Texture2D<uint4>`
loads), the same bytes (`port/render/rd_replay.c` (`doBlurSpriteDraw`, `blurUintGroup`);
`port/render/rd_pipeline.c` (`rd__blur_key`, whose six enumerated keys get their twins while a
target can have the view)). The start line `rd: UINT view: work targets on|off, scene targets
on|off (the profile); the device's view and fx_sprite_u_ps present|absent` says what is in force,
`--stats` counts the sprites (`... staticBlur sprites through the UINT views`), and `rd_blur`'s
u case and `rd_pixel`'s uint case check the bytes against the CPU model with the views on.

**The blur sprite's per-draw constants.** `fx_sprite_ps` takes its UV step's divides from the
reciprocals the CPU rounds once a draw (`gs_div_trunc_rcp`, [SHADERS.md](SHADERS.md#integer-forms))
and `fx_texel`'s wrap size as integers (`DrawCB.aux1`, `port/render/rd_replay.c` (`blurAux1`)), the
same values the shader computed per pixel.

**The heavy stage 6 room and the title menu on the PowerVR phone.** The phone's log of 2026-11-02
(Resolution at the window's 2366 x 1080, the Enhanced preset) put the stage 6 room with Yorda at
21 ms of GPU work a picture (top passes `list 3 SCENE 51 draws 6.20 ms`, `aura SCENE 2 draws 4.64`,
`list 0 SCENE 280 draws 3.54`, `list 11 SCENE 2 draws 1.30`, `list 3 temporary 8 draws 1.18`) and
the title menu at 10 ms (`list 3 SCENE 7 draws 2.00`, `glow WORK2 1 draw 1.23`, `list 11 SCENE 1
draw 1.16`, `list 0 136 draws 1.06`, the present 0.97). The lists' and effects' times overlap (list
8 is the aura; a pass's time carries the tile work of the draws before it), so the passes are the
measure. Replayed on lavapipe at the phone's profile (its F12 dump of that room,
`build-host/tmp/v098/evidence/gpuround/accounting-heavy.csv` on the maintainer's machine: each
pass's draws, loads and stores), the room moves about 240 MB a picture through the tiles: list 0's
pass stores SCENE's colour and depth (20 MB), the volumes' pass loaded and stored the depth again
(23 MB), the pass after the shadow count loads and stores colour and depth (41 MB) and holds the
three shadow composites, the fog, lists 4-6 and the stencil setups of DATE, the crack fill's copy
of the colour (20 MB), the aura's paste pass (20 MB, with the fill's reads), the aura's feedback
read of the whole picture (10 MB), list 11's pass (20 MB), the reduction (20 MB) and the present
(20 MB). At about 16 MB a millisecond (list 11's two flat full-screen sprites, 20 MB in 1.30 ms)
that is most of the 21 ms: the room is bandwidth-bound, and list 0's 280 draws are a small part of
its 3.54 ms (the culling measurement below: not worth a culler). Two of those passes and a clear
are cut: the shadow volumes are drawn in list 0's pass and the count target's new-target clear is
left undrawn ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-volumes-in-the-scenes-pass): the
volumes' pass, about 1.2 ms, and 9.7 MB written, the same picture) and the brightness step is applied by the
reduction above 1x too ([the fades fold](RENDER_POST.md#the-fades-fold): list 11's pass in every
frame of play and on the menus, about 1.2-1.3 ms, within a level at the phone's resolution). What
was looked at and left:

- The aura's paste (`aura SCENE`): in that room AURA_WORK holds only its clear in every frame (the
  constant read's count, 1.0 a replay), so the paste's alpha is 0 and it leaves the colour as it
  is; it still writes SCENE's alpha (0), which the reduction carries into DISPLAY, and its pass
  holds the crack fill, so dropping it saves only its shading (a texel of FEED128 a pixel), not a
  pass. The feedback read after it feeds the next frame's paste.
- The pass after the shadow count (`list 3 SCENE`) cannot join list 0's: the count reads the
  stencil as a texture into a target of its own size, which ends the pass. Its composites are
  full-frame sprites whose alpha test (A > 0) keeps them to the shadow; drawn over the shadow's
  box only they would shade fewer pixels but move the same bytes.
- The title menu's glow mask (`glow WORK2`, 10 MB written and SCENE read at full size): folding the
  mask into the reduction that reads it is exact but needs a shader that takes the mask's own
  bilinear and alpha test inside the box ([the glow's passes](RENDER.md#the-glows-passes)), not done here.

Measured or worked out, not done:

- **The depth as D24S8 on every device** (measured, not done). A D24S8 depth would take 4 bytes a
  texel to load or store against D32F_S8's 5 to 8 (a full-size load or store at 2366 x 1080 from
  12.8-20.4 MB to 10.2 MB). It is not the same picture: every ZBUF the game writes is PSMZ32
  (`port/render/rd.h` (`RdZFormat`), `ico2/seki/src/GsBase.c` (`gsb_SetFrame`): the PSM nibble 0),
  and the game's Z spans far more than 24 bits (its Z row, `port/shaders/vu_common.hlsli`'s note
  on `gsb_SetVSMatrixSub`: GS Z the ftoi4 of zf / z + zn, zf about 1.07e9, so about 2^24 some 1000
  units from the eye, 2^28 at 64 and saturating at 0x7FFFFFFF within about 8; the UI at 0xFFFFFF9B
  and 0xFFFFFFFF). The float depth holds z
  exactly below 2^24 and to 24 significant bits above (`gs_z_to_depth`, GS_ZSCALE_32F); a 24-bit
  integer depth holds 2^24 steps over the whole range, so `rd__target_z_scale`'s 2^-32 on D24S8
  steps 256 GS units everywhere (the layers the Queen's mist and hair needed apart, the comment of
  `gs_z_to_depth`), and no linear scale both keeps 1 GS unit below 2^24 and reaches the near
  geometry and the UI band (the homogeneous clip form needs it linear). Measured on lavapipe with
  the existing fallback (`ICO_VK_FAKE_D24S8=1`, main's replay tool against the stored pass dumps
  of the corpus, `build-host/tmp/v098/evidence/cheap/ab.sh d24`): at Original 4:3, 25 of 30 dumps
  change, SCENE on 0.002 to 0.17 % of its pixels by up to 214 levels, DISPLAY up to 120 (objects
  showing through what should hide them, the Z test's outcome turned); at the PowerVR phone's
  Resolution Auto (its profile's fakes, the eight wide dumps) the present on 0.007 to 0.04 % of its
  pixels by up to 107 levels, the shadow levels too (the volumes' Z test). The D24S8 fallback stays
  for devices without D32F_S8 (`port/rhi/vk/vk_device.c`), with that picture.
- **One pass for a constant-alpha lerp without dual-source blending.** On such a device a LERP
  is two passes (`rd__expand_no_dual`). Where As is the same over the draw (untextured;
  textured with TCC RGB; a mesh whose vertex alphas are all equal) the blend factors could be
  `RHI_BF_CONSTANT` and `RHI_BF_ONE_MINUS_CONSTANT` with the constant As / 128 (FIX / 128,
  or 1.0 and 0 for a PABE-unblended draw): one pass, the same bytes where the blender holds the
  constant at the fragment alpha's precision, which a phone's driver need not, so it would ship
  behind a switch, off, until a Mali device shows the same bytes both ways. It needs a blend in
  `RdPipelineKey`, `pipelineDesc`, a per-pass constant, the mesh's alpha scan at upload, and
  about a hundred more keys in the set without dual-source blending.
- **The CRT's triad gain at a large output.** Under x curvature `crt_ps` scans `2 k0 + 1` box
  pixels a pixel (`triadGain`, `k0 = ceil(box / grid) + 1`): at the 6016-pixel box of a 5K
  display over the 682-column grid of a 16:9 frame, 21 warps for the 9 that fall in the pixel's
  source column. `warpOf`'s x is affine in the box x along a row (its factor depends on y
  alone), so the columns of one source pixel are one run whose ends can be solved for; a scan
  from one before the run to one after it would visit the same hits in about half the steps.
  Not done: the ends' float rounding needs its own proof that no hit falls outside them.
- **The crack fill's pass break on a tile-based GPU** (measured, then changed: the fill is drawn
  after lists 4-6, [Crack filling above 1x](RENDER_POST.md#crack-filling-above-1x)). Before the change, on the
  fog's local path the fill ended the scene's pass for its two copies and began it again with
  SCENE's colour and depth loaded. The PowerVR phone's perf CSV (`[dev] perf_passes`), Resolution
  Auto, SCENE 2366 x 1080, 2079 replays with the fill
  (`build-host/tmp/v098/evidence/perf/crack-pass-summary.py`): list 3's last SCENE pass 2.08 ms
  median, the copies (`snapshot`) 1.10 ms, the pass begun again (labelled `fog`: the fill, the fog
  and lists 4-6, 32 draws median) 4.10 ms; the break added the store of colour and depth at list
  3's end (10.2 + 20.4 MB at 8 bytes a depth texel), the copies (61 MB read and written) and the
  load of both again (30.7 MB). The move is not the same bytes: the fog above 1x dithers by the
  pixel's own position (`port/shaders/fog_lut.hlsl`, the `gs_bayer8` index from `i.pos`) and lists
  4-6 Z-test and blend against the filled texel; the difference is measured (the section above,
  [DIVERGENCES.md](DIVERGENCES.md) VD-20). Not done, by reading: conservative rasterisation
  instead of the fill (`VK_EXT_conservative_rasterization`: overestimation grows every edge,
  silhouettes included, extrapolates UVs and depth there and blends a shared edge twice) and the
  fill's copy from the reduced SCENE copy (the half-texel offset found when it was tried). The
  colour swap (SCENE's colour swapped with its snapshot as `blurSwapTarget` does, the fill
  writing every texel) needs that every texel make an RGBA8 round trip through a
  float output, which the Vulkan specification lets round to either neighbour, so it is taken
  only where a start-up self-test of all 256 levels passes on the device itself, else the copy stays
  ([The crack fill's snapshot by the swap](RENDER_POST.md#the-crack-fills-snapshot-by-the-swap)).
  `build-host/tmp/v098/evidence/perf/crack-pass.sh` measures the pass structure with and without
  the fill on lavapipe under the PowerVR and the Qualcomm-driver profiles.
- **The crack fill in list 11's pass** (the same bytes,
  [RENDER_POST.md](RENDER_POST.md#the-crack-fill-in-list-11s-pass)). The PowerVR phone's title
  (the owner's log of 2026-11-12, stage 1: 333 of 505 pictures under the glow's DARK plan) drew the fill in its own
  pass (1.0 a picture, 1.23 ms, 9.6 MB stored) and list 11 in another (2.69 ms, 9.6 MB loaded and
  9.8 stored). Where nothing between touches SCENE the fill's draw is held to list 11's first pass,
  drawn there over a scratch depth: one pass and about 19.5 MB of colour (the fill's store, list
  11's load) fewer a DARK title picture at 2366 x 1080, and list 11's depth load. Not measured on
  the phone (no dump of its current title on this machine; the corpus title's motion blur draws
  into SCENE before list 11); `rd_fill11_ab` asserts the passes and bytes on lavapipe from the
  code. Off by default in this release: on the PowerVR phone the title's right part flickered black
  with it on (cause not found).
- **The fog's cost per frame.** On lavapipe the fog of the Mac frame costs several times the
  title frame's at under three times the pixels; the fog pass count (`rd__fog_plan`) and its
  depth source (`rd__fog_source`) are where to look before its shader, which is about 60
  instructions a pixel.

**On a tile-based GPU** the times per list and per effect are where the work ended, not where it
was drawn: a tiler renders a pass's geometry when the pass ends, so the time lands on the timestamp
after the pass break. The fog shows it (issue 95, the Galaxy A12's log, Mali-G52, `fog: depth
path copy at start (tile-based GPU)`): it is one RD_POST_FOG a fogged frame (list 4; the title
frame's `4:42`), one draw, and on the copy path one whole copy of SCENE's depth (512 x 448 D32S8,
about 1.8 MB), after `endPass` closes the scene's pass (`port/render/rd_replay.c` (`doFog`);
`port/render/rd_core.c` (`rd__sampled_depth`)). The log's fog time equals list 4's time in every
line, and it swings with the scene, not the fog: 8.8-9.3 ms, then 70-172 ms, then 0.00 when the
scene has no fog, while lists 3 and 11 and the glow climb to 150-330 ms in the same lines: the
pass break moved, and the scene's tiles are paid at the next one. The fog's own work is a
full-screen sprite and the copy, a few MB of traffic. The copy and in-place paths break the pass
there (in place must end the pass to sample the depth too; the copy adds only the transfer). The
local path keeps the depth on the tile: the fog draws in the scene's pass and reads the depth as an
input attachment (dynamic rendering local read), with no pass break and no copy ([the fog's local
read](RENDER.md#the-replay)). The profile takes it wherever the device has local read: every tile-based
class and desktop Vulkan after its probe (`port/render/rd_profile.c` (`rd__profile_init`:
`fogLocal`)); the PowerVR phone's log says `fog: depth path local (self-test: local ok 1,
...)`. A device whose driver offers no local read takes the copy: Qualcomm's own driver (issue
177's Adreno: `local read no`, `fog local off`, `fog: depth path copy`), and D3D12, D3D11 and
OpenGL, which have none. Above 1x under Enhanced the crack filler is drawn after lists 4-6: its
neighbourhood reads need a copy of SCENE's colour, made where the scene pass ends anyway, and its
own pass holds SCENE's colour alone, or it is the first draw of the aura's paste pass
([Crack filling above 1x](RENDER_POST.md#the-fills-place-after-list-7)). On a
tiler, compare the sum of the
lists, not one list or effect (the log's GPU top passes and GPU ms per replay lines now say so).

### Culling

**What the game culls.** Each object's box, then each part's box, goes through `gsb_ClipBox` against
the camera's frustum (`ico2/seki/src/RegistPacket.c` (the object at :606, every part at :1165),
`ico2/seki/src/GsBase.c` (`gsb_ClipBox`, :3107-3156)): the eight corners through the current matrix,
and a box is dropped only when every corner is outside one plane by the drop margin
(`gsbHostDropFlags`, :2892). The planes tested are +x, -x, +y, -y and the near side (`all & 0x2F`,
:3143: bit 4, the far side, is not in the mask), so nothing is dropped for its distance. A packet's
own box picks its clip program but never drops it: a packet wholly outside the view is drawn through
the clipping program and the GPU clips it away (`reg_clipPacketBoundingBox`, `RegistPacket.c`
:422-477). There is no occlusion test, no room, portal or visibility set, and one stage is resident.

**What the measurement finds.** `rd_replay_tool --occlusion` (below; `port/render/rd_occlusion.c`)
puts every GPU draw of list 0 inside an occlusion query of its own ([RHI.md](RHI.md#the-interface),
"Occlusion queries") during the picture's replay and maps each query to the command that drew it:
`g_rdStoreOrd`, the command the walk replays (its index in list 0, which the walk takes first), or
for a screen-prim run's draw, which `flushScreenRun` records after its command, the command that
opened the run (`ScreenRun.ord`, `g_rdDrawOwner`). The measurement runs under `ICO_RD_VU_MERGE=0`,
`ICO_RD_BLEND=fixed` and the screen-prim runs off (`rd__set_screen_merge`), so each draw command
makes its own GPU draws. A draw command (screen prims, mesh, skinned, grid, particles) whose draws
all passed 0 samples was hidden when it drew: left out of the replay (`--nop`) it changes no pixel,
the saving a culler that tests against the depth drawn so far could make. Hidden includes what draws
nothing for another reason: a packet outside the view (above), off the scissor, or with every
fragment discarded. The same measurement under `ICO_VK_DEPTH_ALWAYS=1` (every depth test passes,
[RHI.md](RHI.md#vulkan)) hides only those, so the commands hidden as drawn and not under the switch
are the ones the depth hid, the share an occlusion culler can take; `tools/occlusion_corpus.py`
separates the two from the tool's `--occlusion-csv` tables. Draws of other commands in list 0 (a
clear drawn as a quad, a fill) are counted apart and are not candidates. Only list 0 is measured:
the passes after it (the fog, glow, aura, fades, shadows and the present) draw the whole frame and
no test against the scene's depth leaves them out.

**The sweep.** The cull of every stage on the disc, not only st18a, is measured by `gsb_cull_test
--sweep` (ctest `gsb_cull_sweep.*`, main only: it needs the disc image): per stage and aspect, the
parts the box cull leaves out of a picture between the ticks, what the blend span saves, and the
triangles the region rule does not draw at the ticks and between them, against the stored counts
of `tools/sweep_baseline.txt` ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md), `gsb_cull_test.c`).

**The gate and the numbers.** `tools/occlusion_corpus.py` runs the dumps of
`tools/occlusion_corpus.txt` at Original 4:3 1x and Enhanced 4:3 2x: the measurement as drawn and
without the depth, then the plain replay and the replays with the hidden commands left out (`--nop`
`@FILE`) under `--pass-dump` (every target byte for byte) and `--stats --gpu-repeat K` (list 0's GPU
median of K warm replays on lavapipe). A culler of list 0 is built only when the median share the
depth hid is at least 15 % of the list-0 draws and 10 % of the list-0 triangles over the gameplay
dumps and leaving those commands out takes at least 10 % off list 0's GPU time, with every compared
pass dump identical. The report, a row a dump and setting with the medians and the verdict, is
written beside the run's logs (`--out`, by default under
`build-host/tmp/<release>/evidence/perf/occlusion.md` on the maintainer's machine).

**What it found.** The corpus run (33 dumps of the list, each
at both settings: 66 rows, every one timed and every compared pass dump identical) failed the gate
by far. The median share of list 0's draw commands the depth hid was 1.6 % (1.9 % at Original, 1.5 %
at Enhanced 2x) and of its triangles 0.7 %; leaving those commands out took a median 1.3 % off list
0's GPU time (0.5 % and 3.4 %), inside lavapipe's noise (the 8-draw title dumps, which hide nothing,
move from -20 % to +25 % between replays). On the gameplay dumps of 368 to 1802 draws (the phone's
three, issues 164, 163, 173, 102, 91 and 146, the sandstorm sheets') the depth hid 0 to 4.9 % of the
draw commands and 0 to 18.8 % of the triangles (the 18.8 % is five commands of one dump, issue 163's,
9600 triangles); the godrays sequence's dumps 0 to 12.7 % and 0 to 9.7 %. Most of what draws nothing is hidden for another
reason: a median 54.2 % of the commands and 41.9 % of the triangles draw nothing under every depth
test too (outside the view, since the game drops only whole objects and parts and lets the GPU clip
the rest, above; off the scissor; or every fragment discarded), which a test against the depth
cannot save. On lavapipe those draws cost little (judged on that share the gate fails as well: no
measured GPU gain), but a tile-based GPU pays each draw's vertex and binning work whatever it
covers: 60 to 150 us a draw of list 0 on a Mali-G52, about 12 on the PowerVR phone, and issue 179's
frame (a Mali-G52 MC2) has 1068 of its 1381 list-0 draws (77 %) and 70 % of its triangles outside
the view. That share is the visibility test's below, on the CPU. The scene draws its far side
little, so an occlusion query or a depth pre-test per command would cost more than the 1-2 % the
depth hides; the measurement (`--occlusion`, `tools/occlusion_corpus.py`) is the ceiling any
occlusion test is judged against.

**The port's visibility test.** `port/render/rd_vis.c` (`rd_vis.h`) tests every mesh, skinned and
grid command of lists 0 to 10 on the CPU once a picture, before anything else reads the frame
(`replayFrame`, after `rd__diag_frame`: `f = rd__vis_frame(f, keep)`), and leaves out of the replay
those whose every vertex lies wholly beyond one side of the bound target or behind the eye, so they
cost no pre-pass entry, no VU draw, no ring bytes and no GPU work.

- *The bound.* A mesh's positions are qword 0 of each vertex of its stream (the `RD_VU_QW_*`
  layouts). The box of every vertex of the stream (the draws' batches and a sheet's tiles all lie in
  it) and the range of the w lane are kept per mesh by its id, its stream serial
  (`RdMeshRec.streamSerial`, new at every write of the stream: creation, `rd_update_vu_mesh`'s
  morphs, the interpolation's scratch copies and their blend), its vertex count and layout; a record
  whose serial is 0 is bounded at every draw. A skinned stream keeps a box per bone slot (the qwords
  `vu_skin.hlsl` reads for a weight's VU address, `min(a - 16, 236)`), only when every weight lies in
  [0, 1], with the largest `|w0 + w1 - 1|`. A grid's vertices are in the command's payload (the
  frame's copy of the Mesh3D buffer: per strip the VIF qword, the tag and the colour, the vertices,
  MSCNT), bounded at every draw.
- *The test.* Every program gives a vertex the homogeneous position `h = M (x, y, z, w)` in the VU's
  order (`vu_mat`; `M` VU memory 16..19, `w` 1, or `pos.w` under code 36; cluster
  `mem[4..7] (w0 B0 (p, 1) + w1 B1 (p, 1), 1)`), linear in the position, so every vertex's `h` is a
  convex combination of the box's corners' (8, or 16 over code 36's w range and 1), computed in
  double. The target's rectangle is its GS extent in the programs' coordinates: XYOFFSET
  (`2048 - gsW / 2`, `2048 - gsH / 2`, `frameRefresh`) to XYOFFSET plus the larger of the state's
  size and the target record's, a row more down for the frame head's half pixel. A side is passed
  when two planes through the eye, at 2 and at 1 GS pixel beyond the edge (left:
  `h.x - (x0 - 2) h.w` and `h.x - (x0 - 1) h.w`), are both negative at every corner by more than
  2^-18 of the sum of the absolute products (the shader's float rounding of `h` and of the
  clip-space mapping, with room for a GPU whose sums round to within an ulp); behind the eye when
  `h.w` is. For a skinned draw a corner is a slot's box corner through its bone and then `mem[4..7]`,
  and a vertex whose weights sum to `s` lies `1 - s` of the way to the blend's origin, so a plane's
  bound is `(1 - e) m + e |c|` (`m` the corners' largest value, `c` the plane at `mem[7]`, `e` the
  largest `|w0 + w1 - 1|`, kept below 0.5).
- *Why nothing it skips draws a pixel.* A corner the GPU gets at its GS position (every corner of a
  triangle drawn exactly, and a clipped triangle's corner in front of the eye with X and Y not
  saturated, `vu_vtx_position`) has `w > 0`: the region test needs `0 < w` (cluster: `mem[0].w < w`,
  and a draw whose `mem[0].w` is negative is not skipped), the GS position and the extended snap
  need `w > 0`, and code 36 is skipped only with every corner in front of the eye. So its `x / w`
  is left of `x0 - 2`, its ftoi4 X within 1/16 of that (a saturated X keeps its side), and it lies
  beyond the second plane. A corner in the homogeneous form (behind the eye, saturated, or below GS
  Z 0, `vu_homogeneous_position`) is `h` through a linear map: beyond the second plane at any `w`.
  The GPU draws the points of the clip-space triangle with `w > 0`, convex combinations of its
  corners, so left of `x0 - 1`, a pixel and more from the target's first sample (GS `x0` on the
  pixel-centre convention, `x0 - 0.5 / scale` at worst under MSAA or without the half-pixel origin).
  Behind the eye every vertex has `w < 0`: no GS position, and every homogeneous corner is clipped
  away. The chain plane, the later mark's depth bias, the card test, the tick cameras' rule and the
  content table only leave out or move depth, never a position. So the verdict depends only on the
  target's GS size and the payload, the same at every preset, scale and aspect.
- *The batches' box.* A draw reads the batches `firstBatch` to `firstBatch + batchCount - 1` of its
  mesh (all of them when `batchCount` is 0, as `doVuDraw` does), and a batch's indices lie in its
  own vertices (`RdVuBatchRec`). A draw of part of a mesh whose whole stream's box reaches the
  target is tested again with the union of its batches' boxes (kept per batch on the mesh's entry,
  made again with the stream's serial); a mesh carrying a sheet's tiles (`tiledFrom`, which a draw
  out of the box reads too) or a batch outside the stream keeps the whole stream's verdict. The
  same test on a subset of the vertices, so the same argument. `ICO_RD_CULL_RANGE=0` turns it off
  for a comparison (`rd_cull_ab` runs with it on).
- *Code 34.* A mesh or grid draw under clip mode none (X and Y wrap to 16 bits) is never skipped by
  the plane test. Under `full` it is a candidate for the occlusion test like any other mesh or grid
  draw, as an occludee (its box must lie in front of the eye and inside the GS region 1 to 4094,
  so no wrap can happen) and as an occluder (`rd__vis_hiz_mesh` admits it: a triangle counts only
  with all three corners accepted by the same vertex rule as code 32, which keeps them inside the
  region where the wrap changes nothing and the program draws them at their GS positions). A skinned
  draw under it stays exempt.
- *Not tested.* Particles; a skinned draw under code 34; code 36 behind the eye
  (its triangles without a clip flag are drawn at their GS positions whatever `w` is); a draw with
  no target, a payload or a layout the test does not read, or a bound not finite; lists 11 and 12;
  a pass window (the bisect's and the tool's `--passes`: the passes as recorded) and a frame never
  recorded (number 0: the self-tests' frames).
- *Under MSAA.* With samples in force (`rd_aa_samples(rd__aa_mode()) > 1`) a draw whose state could
  close [the MSAA epoch](MSAA.md#where-the-epoch-closes) where the replay of every draw closes it is replayed as
  recorded (`rd__vis_msaa_may_close`; the reason `kept: may close the MSAA epoch` in the tool's
  `cull why`): DATE on (the snapshot reads SCENE), a texture that is a view of SCENE (texturing on
  or off), or a colour and depth pair touching SCENE other than SCENE's own pair (SCENE's colour with
  another depth, SCENE's depth with another colour). A NOP has no pass and no transition, so
  skipping such a draw would keep the epoch open past it and multisample the draws up to the real
  close, which the replay of every draw draws single-sampled (other edge pixels after the
  covered-majority resolve). The rule holds whether the epoch is open at that draw or not
  (conservative). `rd_vis` checks it on a recorded frame; `rd_cull_ab` compares byte for byte under
  MSAA 4x on the first dump and on the dumps of `tools/cull_msaa_dumps.txt`, chosen where the epoch
  closes inside lists 0 to 2.
- *Under either region rule.* The argument above is about what the GPU draws of a triangle,
  exactly or clipped, so it holds for the triangles the camera rule draws where the PS2 dropped
  them ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-camera-rule)): they are clipped triangles of the same
  draw, and a draw wholly beyond a side of its target or behind the eye draws none of them. The
  occlusion test's occluders are the exactly drawn triangles alone, which the rule does not change.
  The game's own box cull (above) is the camera's frustum and stays the visibility authority of
  whole objects and parts under either rule.
- *The shadow frame.* The skipped commands are left out as the replay tool's `--nop` leaves them:
  the type becomes `RDC_NOP` and every other field, every index and so every ordinal
  (`g_rdStoreOrd`, the pre-pass entries' commands) stays. `rd__vis_frame` returns a copy of the
  frame whose lists with a skipped command are copies (the payload and the other lists shared), and
  every scan of the lists after it (the store plan, the passes' areas and cuts, the crack fill's
  place, the ring's estimate, the meshes' upload, the vertex pre-pass, the blur fold, the constant
  reads, the list loop) treats an `RDC_NOP` as nothing. What decides across draws or across replays
  reads the frame as recorded (`rd__vis_source`, `rd__vis_culled`): the strip joining over a part's
  packets (`rd_join.c`: a skipped packet's strips still join the others', so the card verdicts are
  those of every replay), the fades fold's plan (its reads of SCENE's depth before the clear) and
  the store plan's carried read (a skipped draw's read of a depth before its clear keeps the stores
  from then on, as it does when the draw is replayed).
- *The occlusion test* (`[dev] cull = full`; `port/render/rd_vis_hiz.c`, whose header holds the
  argument). In list 0, after the frustum test, a mesh or grid draw whose every sample lies behind
  what the opaque draws before it wrote to its depth buffer fails its GEQUAL test at every sample
  and writes nothing (colour, depth, alpha, stencil), so it is skipped too. The measured ceiling is
  small (the depth hides a median 1.6 % of list 0's draws over the occlusion corpus, "What it
  found" above); the test is built exact, and is cheap only against what it could save (below).
  - *The record.* Cells of 2 x 2 GS pixels over the bound target (4 GS pixels round it), each a
    lower bound of the GS Z the depth buffer holds at every sample in it (0 nothing known; larger is
    nearer), in tiles of 8 x 8 cells keeping their least cell (worked out again when one of theirs
    rose). Larger is nearer and GEQUAL only raises the depth, so the bounds hold until something
    may lower it: the record starts at 0 at the head of list 0, and is reset to 0 (always a lower
    bound) when the colour target, the depth target or the frame size changes, at every clear,
    copy, exact blend, shadow command and post stub, and at every draw that writes Z under a test
    other than GEQUAL, GREATER or NEVER (ZTE off is ALWAYS). Only a PSMZ32 depth buffer (the 24-
    and 16-bit formats clamp at their top); off under `ICO_VK_DEPTH_ALWAYS`.
  - *The occluders.* An RDC_MESH of the prelit or lit program (codes 32 and 36) the frustum test
    keeps, with Z write on, Z test GEQUAL or ALWAYS, no blending, no DATE, no PABE, every colour
    channel written (`colorMask` 0xF, FBMSK 0), no chain plane, and an alpha test that passes at
    every pixel: none, ALWAYS, or the alpha plan's range of As for the draw (`rd_alpha_plan.c
    rd__alpha_range_of_draw`, what `doVuDraw` plans the same draw's passes with: the texture's
    alpha over every level the device holds, the function and every vertex's alpha) passing the
    test whole. The scene's opaque meshes draw under ATE GREATER 0x60 with AFAIL FB_ONLY (a
    failing pixel writes its colour but not its depth; every VU draw of list 0 in the corpus dumps
    v1700, v10103 and v5836), so without the range there would be no occluder; with it 63 of the
    65 prelit and lit code-32/36 meshes of v1700's list 0 and 99 of v5836's 132 have a texture
    whose alpha stays above it (`rd_replay_tool --list`). The walk runs before the replay's
    uploads (`replayFrame`: `rd__vis_frame`, then `rd__upload_dirty_textures`), so a texture not
    uploaded yet (dirty: every texture of a dump's first replay, a texture new or changed this
    picture in the game) has no scan for the plan; the occluder test then settles the decodes in
    flight (`rdtex_settle`, which the ring's estimate does right after anyway) and takes the scan the
    upload will make from the CPU copy (`rd__vis_hiz_pending_scan`: level 0 whole and the GS levels;
    a box chain's alpha stays inside level 0's range, the kept alpha coverage, `rd__mip_boost`,
    refused), composed with the vertices' range as the plan composes it, and the vertices' range
    itself as well (a failed upload draws untextured). Of
    its triangles in the index ranges `doVuDraw` draws, those drawn exactly in every state: all
    three vertices inside the program's region test (code 32: `0 < x, y < 4094`, `0 < w <
    16777214`; code 36: no clip flag in `mem[20..23]`'s space) with a margin the float rounding
    cannot cross, in front of the eye, GS Z inside ftoi4's range; not hidden by the content table,
    not raised by the later mark, not a sheet's tile. Such a triangle is `VU_RULE_EXACT` at its GS
    positions in every state: each corner lies within `r` of `x / w - 1/32` on each axis (ftoi4's
    truncation, the float product's error, the GPU's snap: 1/8 GS pixel covers a 4-bit sub-pixel
    grid on texels up to two GS pixels). A cell counts as covered when its square, grown by a GS
    pixel each way (a sample's depth may come from a sample up to a GS pixel away: a texel's
    samples about its centre, which the MSAA epoch loads from the scene and resolves into it, the
    epoch closing in the middle of list 0 when something reads SCENE) and by half a GS pixel more
    up and down (a draw with the frame head's half line puts a sample half a pixel lower in GS
    terms than one without), lies inside the triangle for every placement of its corners
    within `r` and by 0.25 GS pixel more: then the GPU rasterises every pixel or MSAA sample in it,
    at every scale (no face culling anywhere in the port). The cell rises to `floor(least corner
    Z) - 1` (the triangle's depth there interpolates its corners'). Only triangles whose screen box
    covers a 64th of the frame count, the largest first within `ICO_RD_CULL_HIZ_TRIS` (12000 a
    picture), inside the draw's scissor. A wall under ALWAYS resets the record first and counts
    only when every triangle it draws is exact, each cell at the least bound of them all (a sample
    holds whichever triangle came last).
  - *The occludees.* A mesh (prelit, lit, specular or reflection) or grid draw of list 0 under
    GEQUAL whose stream's bound (above) has every corner in front of the eye: every vertex is a
    convex combination of the corners, so its `x / w` and its `Z = 16 z / w` lie between the
    corners' (the mediant), and so do the GPU's points (exact corners within 1/16 plus the snap,
    homogeneous ones exactly, clipped points convex combinations). The corners' box, grown by
    those amounts, holds every sample the draw can touch, inside `(0, 4094)`; `Zmax` bounds every
    depth it can write, below 0xFFFF0000. It is hidden when every cell the box touches holds more
    than `Zmax + 4096 + Zmax / 2^17` (the depth buffer's steps: 256 GS Z units on D24, 2^-24 of the
    value on D32F; the interpolation's float error either way), with `max(Zmax / 256, 1024)` more
    for a mesh with the later mark (`vu_later_bias`'s cap). Then at every sample `depth >= cell >
    Zmax + margin`: GEQUAL fails at any rounding inside the margin. A coplanar decal or second pass
    is kept (strict compare and margin), and so is a draw touching a cell no single triangle covered
    whole (a cell is never the union of two triangles: a seam's samples may be neither's). Skinned
    draws and particles are not tested.
  - *Order and cost.* Before the walk, list 0 is read once for the occluders' triangles (the budget's
    sort over the whole list); the walk then tests each kept draw of list 0 against the record and
    puts its own triangles in after. The reset touches only the tiles written. The test's time is
    its own figure in the lines below (its target: 0.4 ms a picture on a phone's big core).
  - *Where the time goes, and the cheaper collect* (issue 179). Issue 179's Mali-G52 phone
    logged 14.4 to 16.1 ms of cull a picture, 12.8 to 14.7 of it the occlusion test, for 0 draws
    occluded in its scene blocks (the 10 s lines at L596, L634, L678 and L727 of the evidence
    log). The test's own split: the collect ran once a picture over every
    mesh draw of list 0, and for each candidate that passed the state, target, bound, program,
    alpha and box rules projected every vertex of the range it draws through
    `rd__vis_hiz_vertex` (in double: 32 multiplies and 6 divides a vertex, 32 more for code 36's
    clip matrix) and only then dropped nearly every triangle at the 64th of the frame; the
    rasterisation of the few kept and the occludees' tests are small beside it. On this machine
    the plain pass over the occlusion corpus (lavapipe, x86, the replay tool's `--stats`) takes a
    median 2.9 ms of cull a picture on a dump's first replay (2.1 the occlusion test; the textures
    not uploaded yet are scanned for the alpha proof) and 0.9 (0.75) on its third. With
    `ICO_RD_CULL_HIZ_FAST` on (the default; `0` the plain pass, a comparison) the collect makes
    the same occluders at less cost, each step exact:
    - *A batch's screen box.* For each batch of the draw, `rd__vis_hiz_screen_area` bounds the
      screen box of any triangle whose three corners lie in the batch's vertices and are accepted:
      the batch's box (kept per batch on the mesh's entry, as the frustum test's batches' box)
      through the matrix, every corner in front of the eye by the tolerance, so a vertex's exact
      `x / w` lies between the corners' (the mediant); the accepted corner `x = p - 1/32` is
      within 2^-32 of `p`'s exact value (its `w` and `x` clear their tolerances of 2^-18), far
      inside the 1/1024 GS pixel of room, and inside the region test's bounds, to which the box is
      cut. A batch whose box area is under `minArea` marks its vertices, and a triangle with all
      three corners in one marked batch is left unprojected: its own box lies in the batch's, each
      side at most the batch's and rounding monotone, so its area is under `minArea` too. A batch
      with a corner behind the eye is not marked; a sheet's tiles (`tiledFrom`) and a record of
      serial 0 have no batch boxes.
    - *A triangle's screen box.* Before the full projection, the corners' `x` and `y` exactly as
      `rd__vis_hiz_vertex` makes them (the same products in the VU's order, the same divide and
      offset: the same bits, under `-ffp-contract=off` and without excess precision,
      `cmake/IcoFlags.cmake`) and the area as `rd__vis_hiz_tri` takes it: under `minArea` the
      triangle is left, as the full pass would leave it at the area. Equal by construction, so no
      bound is needed. A later-marked triangle or a sheet's tile, never kept, is left unread too.
      A draw under Z test ALWAYS reads `lbAll` and whether every triangle is exact, so it takes
      the full pass.
    - *The walk's verdicts.* `cmdVerdict` of an occluder candidate in the collect is kept (its
      verdict, reason, counts and bound) and the walk takes it for the same command instead of
      working it out again (the same state at that command, the entries kept by stream version).
    - *The pending scans.* A texture not uploaded yet is scanned for the alpha proof once a
      collect, not once a draw (`rd__vis_hiz_scan_memo`). The decodes' settle before the collect
      (`rdtex_settle`, a wait for the decode workers) is timed apart, outside the occlusion
      figure.
    - *The collect cache* (`rd_vis_hiz_fast.c`). A draw's occluder triangles are a function of
      the mesh's stream, index list, batches and marks (new only with a new stream serial), the
      sheet's tile bounds, the draw's batch range and clip mode, VU memory 16..23, `minArea`, the
      extended state and the pass; a draw whose key is all of these byte for byte takes the
      triangles kept from an earlier picture (a still camera, a paused or re-shown picture): 512
      sets of 4 entries, at most 32768 triangles kept in all (past it the entries no draw of the
      picture read go).
    `ICO_RD_CULL_HIZ_CHECK=1` works out in full every triangle the fast collect leaves and every
    cache hit, and counts a difference. The proof: `rd_vis_hiz` (random meshes, cameras and frames
    against the plain pass, field for field), `rd_vis_hiz_corpus` (every row of the occlusion
    corpus at three settings, the third the cache's: the same 26 counts as the plain pass and the
    table, no check wrong) and `rd_cull_ab` on Vulkan and OpenGL (every picture byte for byte).
    Not taken: a triangle's box as the cells it covers (claims cells it does not), `1 / w`
    multiplies (other bits), coarser cells or fewer occluders (culls less), the vertex pass on
    other threads (measure first: the line below says what is left).
- *The numbers and the default.* Over the corpus of the gate (`rd_cull_ab`, 2026-11-03: 33 dumps,
  every run of them) the frustum test left out 3137 of 12470 scene draws, 25 % of them, and the
  occlusion test 3 more; 2838 targets of those runs equal the off run's byte for byte. The skipped
  share is the draws that put nothing on the screen (above: a median 54 % of the commands of list 0,
  77 % of the frame of issue 179: they cost a tile-based GPU 60 to 150 us each on a Mali-G52, whatever
  they cover), so the saving is the CPU's per-draw walk, the vertex pre-pass and the GPU's vertex and
  binning work of draws that cover nothing; it is largest on a phone that is draw-bound, and
  unmeasured on one until a phone log with the test on sits beside one with `[dev] cull = off`:
  `tools/cull_log_compare.py` reads list 0's GPU ms and the walk's ms from the two. The occlusion
  test is far smaller and costs more: after the alpha proof of the occluders (a texture not
  uploaded yet is scanned from the CPU copy), the replay tool's second replay on this machine
  (lavapipe, x86) found 0 to 1 draw hidden a dump, at 0.5 to 2.3 ms a picture, above the 0.4 ms a
  phone's big core can spare. The default is `full` on an immediate-mode GPU (the maintainer's call: the occlusion test
  ships on). The phone's log decided the other half (issue 179, a Mali-G52: the cull cost 7.49 ms of
  CPU a replay, 6.80 of it the occlusion test, for 0 occluded in the block): a tile-based
  profile (`RdProfile.tiler`) defaults to `frustum`; `[dev] cull = full` keeps the test there. The pictures
  are the same either way (the test only skips what the depth hides: `rd_cull_ab`, `rd_vis_hiz_corpus`;
  `rd_vis` checks the default per class). If it must pay, the next steps are
  a union rule over adjacent triangles (a cell is covered today only by one triangle whole, so the
  cells on a wall's internal diagonals never count) and the occludee's box per batch.
- *The gate.* `rd_cull_ab` ([TESTING.md](TESTING.md)) replays the dumps of
  `tools/occlusion_corpus.txt` and `tools/native_frame_dumps.txt` with the test off, frustum and
  full at Original 4:3, Enhanced 4:3 2x, Enhanced 2x at a wide dump's own aspect and once with MSAA
  4x: every target's PNG of both byte for byte the off run's, the full run's frustum count the
  frustum run's, and draws skipped over the corpus (the occluded total reported). The A/B is meant to run on both lavapipe backends, Vulkan and OpenGL (the OpenGL run waits on a
  fix to the cull path under that backend, to be written up when it lands). `rd_vis` holds the
  bounds, the planes under random cameras and the VU references (`vu1_ref`) as the oracle, and the
  occlusion test's vertex and occludee bounds against the float product, the rasterisation's
  inner rule against every corner placement, its verdicts and the frame cases.
- *The key and the lines.* `[dev] cull = off | frustum | full` (`ICO_RD_CULL`; full by default:
  the frustum skip with the occlusion test; frustum by default on a tile-based profile, where the start
  line adds `the profile's default: ...`, the profile line `cull frustum (the profile: ...)` and the 10 s
  line `(profile default)` in place of `([dev] cull)`). The start line `rd: cull: full ([dev] cull, ICO_RD_CULL): ...`
  (under full a second, `rd: cull: the occlusion test: up to N occluder triangles a picture
  (ICO_RD_CULL_HIZ_TRIS), cells of 2 GS pixels`), the summary's `cull frustum`, the backend's 10 s
  line `; cull: N of M scene draws skipped a replay (F frustum, O occluded), T ms (occlusion H)
  ([dev] cull)` and `rd_replay_tool --stats`'s `stats: cull frustum: N of M scene draws skipped (F
  frustum, O occluded), T ms; not tested: E exempt, U unbounded; skipped beyond the left ...;
  occlusion: K occluder triangles (B past the budget), C cells, D draws tested, R resets, H ms`
  (`--cull off|frustum|full` sets the variable; `--occlusion` replays with it off). Where the
  occlusion test ran, the 10 s line goes on after `([dev] cull)` (so `tools/cull_log_compare.py`'s
  `CULL_RE` reads the part before as it did), each figure the mean a replay: `; occlusion: collect
  C ms (settle S, alpha A, vertices V ms for Nv vertices, Nt triangles kept of Na; cache Hc of Dc
  draws), raster R ms (Nc cells), test X ms (Nd draws tested); occluders used U, refused:
  state/chain/target/bound/program/alpha/small/no-tri a/b/c/d/e/f/g/h; past the budget B`. `H` is
  `C + R + X` (and the walk's bookkeeping around them); `S` is the decodes' settle before the
  collect, in neither `H` nor `C` (in `T`); `A` the occluders' alpha proofs and `V` the vertex
  pass, both inside `C`; `Nv` the vertices projected (in full and for the screen box alone), `Na`
  the triangles read and `Nt` those kept as occluders before the budget; `Hc of Dc` the occluder
  draws that took their triangles from the collect cache of those that reached the vertex pass;
  `R` the rasterisation of the walk's occluders and `X` the occludees' tests; then the occluder
  candidates by the rule that refused them (`stats: cull occlusion:`'s names). A collect costing
  most of `T` with `V` most of it says the vertex pass is still the cost (`Nv` against `Na`: how
  much the screen boxes left); `S` large says the decode workers were behind; `Hc` near `Dc` on a
  still scene says the cache holds. `rd_replay_tool --stats` prints the same parts after the
  reasons: `stats: cull occlusion cost: collect C ms (settle S, alpha A, vertices V ms: F in full,
  B for the screen box alone; N triangles read, K kept, L left by their batch's box, M by their
  own; cache Hc of Dc draws), raster R ms, test X ms; fast on|off, check: N checked, W wrong`.
- *The skin check.* `rd_replay_tool --skin-check` checks every skinned command of the dump
  through the skinned draws' bone-block check and prints `<dump>: skin check: ...`
  ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#switches)).
- *Why each command was or was not skipped.* `rd_replay_tool --stats` follows that line with
  `stats: cull why: <reason> <count>; ...`: one reason a scene draw command, the counts summing to
  M (`rd_vis.h`, `RD_VIS_WHY_*`). Skipped: by the stream's box, by the batches' box (what only the
  batches' box took). Not tested: particles, the 2D lists (11, 12), no target, a skinned draw under clip none (code 34).
  Occluded: by the occlusion test. A mesh or grid under code 34 is `code 34 reaches`.
  Not bounded: no payload, clip unknown, no mesh (no record, a program the layout does not fit, a
  batch range outside it), no bound (none or not finite), skinned refused, grid refused. Replayed
  because the bound reaches the target: skinned, grid, scissor mesh (code 36, which is never
  skipped behind the eye), mesh of several batches (a whole-mesh box too loose for the draw's
  packets where the batches' box does not help), one-batch mesh (the draw is partly or wholly in
  the rectangle, or its triangles are, through the box, outside it). `stats: cull occlusion:` follows
  (`RD_VIS_HZ_*`): list 0's mesh commands as occluder candidates by the first rule that refused
  them (state, chain plane, no PSMZ32 depth, skipped or unbounded, program, alpha test not proven,
  box under a 64th of the frame, no exact triangle) or `occluder used`, and the list-0 draws the
  frustum test kept by their outcome (occluded; not a mesh or grid under GEQUAL; no record; no
  occluder yet; box refused; box outside the record; a cell uncovered; within the margin).
  `tools/cull_corpus.py` runs
  the corpus under `--cull off|frustum|full` with `--occlusion`'s count of the draws that put no
  sample on the screen beside it, and writes `build-host/tmp/v0101/evidence/perf/cull.md`
  ([TESTING.md](TESTING.md)).
