# The renderer (rd)

[rd](ARCHITECTURE.md#terms) records what the game draws as [lists](ARCHITECTURE.md#terms) of
commands and [replays](ARCHITECTURE.md#terms) them on the [RHI](ARCHITECTURE.md#terms) with the
[GS](ARCHITECTURE.md#terms)'s rules for state, sampling, blending and depth. Callers:
[GS.md](GS.md); VU shaders: [VU1_PROGRAMS.md](VU1_PROGRAMS.md); sources: [SHADERS.md](SHADERS.md).

## Files

| path | what it holds |
| --- | --- |
| `port/render/rd.h` | the API the game's render layer calls; the model in its header comment |
| `port/render/rd_internal.h` | the command records, the context `g_rd`, what the rd files and tests share |
| `port/render/rd_state.h` | the finite GS state the game uses: blend, test, TEXA, wrap, filters, the pipeline key |
| `port/render/rd_core.c` | recording: the 13 lists, the payload arena, state deltas, targets, the frame ring |
| `port/render/rd_frame.c` | the flip's frame head, the Z scale per target, the VU block, the camera in FrameCB |
| `port/render/rd_tex.h`, `rd_tex.c` | the texture cache: GS formats decoded into rd textures |
| `port/render/rd_mesh.h`, `rd_mesh.c` | the VU mesh path (VU1_PROGRAMS.md) |
| `port/render/rd_replay.c` | a frame onto the RHI: every command's replay |
| `port/render/rd_submit.c`, `rd_submit.h` | a replay's end: the present's pass recorded, the list ended and submitted, the picture presented and the capture it copied written out |
| `port/render/rd_thread.c`, `rd_thread.h` | the renderer on the window's render thread: the picture and the calls posted there, the hand-over of the renderer's state (texLock), the retire list, the stats split, the renderer's own thread for the tools |
| `port/render/rd_submit_thread.c`, `rd_submit_thread.h`, `rd_submit_queue.c`, `rd_submit_queue.h` | the submit thread: a picture handed over has its list ended and submitted and its image presented there (Vulkan, Direct3D 12), the render thread free once its walk ends; its queue (pure) |
| `port/render/rd_replay_vu.c`, `rd_replay.h` | the replay's VU draws and the vertex and triangle pre-passes; what they share with `rd_replay.c` (the replay's state, a draw's setup, its helpers) |
| `port/render/rd_vuprepass_selftest.c` | the vertex and triangle pre-passes' start-up self-test |
| `port/render/rd_upload.c` | the upload ring and what a replay uploads before its first pass |
| `port/render/rd_pipeline.c` | pipeline keys from the state block, the pipeline cache, the reachable set |
| `port/render/rd_dst_read.c` | the exact GS blend read from the destination in the fragment shader |
| `port/render/rd_post.c` | `rd_post`: each post kind as the GS writes and sprites GsBase.c issues |
| `port/render/rd_blur.c` | staticBlur.c's sprites and the reduction's, for `rd__blur_post_sprite`; the open border's strips |
| `port/render/rd_blur_border.c` | the post sprites' replay (`rd__blur_post_sprite`): the fold, the reduction's open border and its outer columns, SCENE's outer columns repeated after the fog |
| `port/render/rd_blur_smooth.c` | Motion blur: Smooth: the motion blur's accumulator, its two passes and the sprite drawn over it (`rd__blur_smooth_sprite`) |
| `port/render/rd_crack_fill.c` | the crack filler above 1x: the pixels no draw covered (or a farther surface seen through a seam) between drawn ones filled after lists 4-6, and their count ([Crack filling above 1x](RENDER_POST.md#crack-filling-above-1x)) |
| `port/render/rd_shadow.c` | the shadow count on the stencil |
| `port/render/rd_water.c` | render-to-texture blocks, target aliases, camera scopes, pool STs |
| `port/render/rd_fog_path.c` | how the fog reads the scene depth; the fog probe and self-test |
| `port/render/rd_fades_fold.c` | the fades fold: list 11's last full-frame lerp sprites applied by the reduction ([The fades fold](RENDER_POST.md#the-fades-fold)) |
| `port/render/rd_content_hide.c` | the content table: stage faces the native frame must not show |
| `port/render/rd_content_extend.c` | the content table's sheets: stage overlays continued to the frame's edges |
| `port/render/rd_content_chain.c`, `rd_chain.h` | the chains drawn at their links' size |
| `port/render/rd_join.c` | the region rule's card test joined over a part's packets |
| `port/render/rd_screen_planes.c` | the attribute planes of screen-prim triangles |
| `port/render/rd_camera_blend.h` | the blended camera, shared with the game's cull |
| `port/render/rd_perf.c` | the per-replay performance records |
| `port/render/rd_vis.c`, `rd_vis.h` | the scene draws' visibility test: a mesh, skinned or grid command wholly off its target or behind the eye replayed as an `RDC_NOP` of a shadow frame ([Culling](#culling)) |
| `port/render/rd_occlusion.c` | every GPU draw of a replay (`rd__replay_draw`, `rd__replay_draw_indexed`: the pass window's take) and the hidden-draw measurement around them ([Culling](#culling)) |
| `port/render/rd_passring.h`, `rd_passring.c` | the pass ring: the last passes recorded, for the device-loss report |
| `port/render/rd_dump.c`, `rd_png.c` | frame dumps; a minimal PNG writer |
| `port/render/tools/rd_replay_tool.c` | [the replay tool](ARCHITECTURE.md#terms) |
| `port/render/tools/rd_replay_region.c` | the replay tool's `--region-stats`: the region rule's verdicts on the CPU |
| `port/render/vu1_ref/` | the VU1 programs' CPU references ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-cpu-oracles)) |
| `port/render/test/` | the renderer's tests ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md#portrendertest)) |
| `port/render/rd_interp.c`, `rd_present.c`, `rd_present_info.c`, `rd_crt.c`, `rd_video.c` | presentation ([DISPLAY.md](DISPLAY.md)); `rd_present_info.c` holds `rd_last_present_info`'s record |
| `port/render/rd_interp_mesh.c` | the interpolation's reads of a mesh's stream kept while its serial holds: a skinned mesh's bone pivots, a rigid mesh's centroid, the same-shape answer of two streams ([Performance notes](#performance-notes)) |
| `port/render/texpack*.c`, `modelpack.c`, `gltf.c`, `xxh3.c` | texture and model packs ([PACKS.md](PACKS.md)) |

## The model

The PS2 game builds 13 priority-ordered [DMA](ARCHITECTURE.md#terms) chain lists per frame and kicks
them in order 0..12 at `dl_Swap`, and the GS is one state machine, so a register written in one list
is still in force when the next starts. rd keeps both facts: a frame is 13 ordered command lists
(`RD_LIST_COUNT`) recorded by the game [fiber](ARCHITECTURE.md#terms) and replayed in order against
one persistent state block. Within a list submission order is draw order; rd never sorts by material
(`port/render/rd.h`).

| list | what draws in it |
| --- | --- |
| 0 | opaque world and characters |
| 1 | semitransparent skinned |
| 2 | semitransparent static, points and lines |
| 3 | shadow volumes, then the shadow composite (inherits state) |
| 4 | specular and reflection passes, puddle and pool reflection, fog |
| 5 | dissolving objects (inherits state) |
| 6 | particles |
| 7 | shine level 1, then depth of field and motion blur |
| 8, 9 | shine levels 2 and 3 |
| 10 | the softening chain |
| 11 | UI, subtitles, film noise, brightness, keep, fade, letterbox |
| 12 | debug font; the reduction at its end (it also applies list 11's last full-frame lerp sprites: [The fades fold](RENDER_POST.md#the-fades-fold)) |

(`port/render/rd.h`, "Lists").

**Coordinates.** Call sites pass what they passed to the GS: XYZ in 12.4 fixed point about the
2048, 2048 window centre, Z as the GS value the call site computed, UVs in 12.4 texels or STQ.
rd records them as they are; the replay converts them (`port/render/rd.h`, "Coordinates";
`port/render/rd_replay.c` (`rd__frame_group_ex`)).

**Defaults and leakage.** `rd_begin_frame` records the TEST, ZBUF, FBA and
[TEXA](ARCHITECTURE.md#terms) defaults
`gsb_SetGsDefault` writes, at the heads of every list but 3 and 5, through the ordinary state
calls (`port/render/rd_core.c` (`recordDefaults`)). The values are in
[GS.md](GS.md#the-gs-defaults-per-list).

**Keep frames.** `rd_end_frame(1)` replays lists 11 and 12 only, on top of the retained DISPLAY
(`port/render/rd.h` (`rd_end_frame`)). The game sets `fbKeep` from a stage change's fade-out through
the next stage's load (`ico2/common/src/StageManager.c`, the switch message) and on the boot's
memory card and language screens (`ico2/common/src/kanbanBoot.c`); `ico2/seki/src/DisplayList.c`
passes it to `rd_end_frame`. The pause menu is not a keep frame: a paused frame is recorded and
drawn whole, the pause layout's backdrop and rows on top of the scene.

**Threads.** Every `rd_*` call comes from the game fiber. The game's post passes are appended by the
same code inside the [vsync](ARCHITECTURE.md#terms) before `rd_end_frame`, so there is one hand-off
point. GPU work the game's calls cause (replay, pipeline creation, [present](ARCHITECTURE.md#terms))
runs through `rd_set_host_call`, which the window sets to `ico_sched_call_on_host` so the driver
runs on the host stack in the host FP mode (`port/render/rd.h` (`RdHostCall`);
`port/render/rd_core.c` (`rd__on_host`)). With the window's render thread on
([PLATFORM.md](PLATFORM.md#the-render-thread)) every picture is made there
(`port/render/rd_thread.c`): the window's present posts `rd_present` whole (`rd_present_post`),
the interpolation included, and `rd__on_host`'s calls, `rd__replay_frame`, `rd_read_display` and
`rd_resize_output` run there with the game thread waiting (`rd__render_call`); a display change's
pipelines stay on the game thread with the render thread idle (`onGameHost`: their progress screen
pumps the window's events). A picture handed
over owns the renderer's state until its walk and present's pass end (`rd__reads_done` in
`rd__submit_frame`); every game-thread call that changes the frames, textures, meshes, targets or
settings waits for that first (`rd__texlock_wait`), so the render thread reads the state as it
was when the picture was posted. The submit and the present then overlap the game's step and the
next picture on the render thread: they run on the submit thread
(`port/render/rd_submit_thread.h`). With Vulkan's frame slots apart (`RhiLimits.slotOverlap`, the
timeline sync; [RHI.md](RHI.md#the-interface), "Frame slots and the submit thread") the next
picture's fence wait waits only for the picture two before, whose slot it recycles
(`rd__submit_drain` -> `rd_thread_submit_wait_slot`), and its interpolation, recycle, acquire, uploads
and walk all run while the picture before is ended, submitted and presented: a picture holds the
render thread for its reads alone. The acquire waits for the picture before's present only where the
swapchain has no image to spare. Every call that is not a slot call (a texture or pipeline made or
destroyed, a readback, a submit, a swapchain rebuild) still waits for the submit thread, so the
queue receives every picture's list and present in order and the bytes are those of one thread. With
Direct3D 12, Vulkan's fence ring or `ICO_RD_SUBMIT_OVERLAP=0` the next picture's first graphics call
(its fence wait) waits for the submit thread's whole job, so what overlaps there is the
interpolation. The lines: `rd: submit thread: the frame slots apart (...)` or `the frame slots one at
a time (<why>: ...)` at the start, `rd: submit thread: the render thread's picture waited X ms for
frame slot S's picture two before to be submitted and presented (N such waits so far)` (first 8,
then every 100th), and the end's line counts the pictures begun while the one before was still
being submitted and those slot waits (`RdSubmitBlock.overlapped` has the 10 s count). At a PowerVR
phone's numbers (reads 9 ms, submit and present 7.7 ms, interpolation 3 ms) the render thread was
held 9 + 7.7 - 3 = 13.7 ms a picture, two pictures and the 9 ms closing step 36 ms of a 33.3 ms
frame; with the slots apart it is held for the reads, 9 x 2 + 9 = 27 ms (a model, not measured).
The window makes a further picture of a frame already shown only when its reads would end before the
next step and its end before that step's end, and, with vsync off or mailbox, not while a picture
runs past its estimate or the presents wait for the display or the GPU (PLATFORM.md, the render
thread's further presents; the present call's time of each picture: `rd__thread_note_present`).
The overlay's two layers (UI code: port/ui's font state and caches are the game thread's) of a
picture the game thread posts are collected there before the post (`port/render/rd_present.c`
(`rd__overlay_hand_over`)), and the picture's replay copies their batches after its deferred text
instead of calling the callbacks (`overlayCollect`). `rd_overlay_prims` keeps prims only inside
a collect on the calling thread; others are dropped and logged (`rd: overlay: N prims given outside
a present's overlay collect on this|the render thread were dropped (K times)`, the first 8, then
every 100th).
A GPU texture the game thread destroys goes to the retire list the render thread empties in its
next job (`rd__retire_texture`): a call's job at its start, a picture's at its first graphics call
(`rd__thread_retire_kept` from `rd__submit_drain`, after that call's wait for the submit thread).
A destroy is a graphics call that is not a slot call, and on the render thread it waits for the
submit thread, so destroying at a picture's start would make its interpolation wait behind the
picture before's submit and present; destroyed at the first graphics call, the interpolation
overlaps them on every picture, and the backend sees the same calls in the same order (the destroys
after the submit thread's job, before `rhi_wait_frame`). With the slots apart a picture after a
destroy therefore waits for the picture before's submit and present at its fence wait, as every
picture did before; so does one while the background pipelines finish (their step's calls).

What a picture holds the renderer for (the 10 s line's `a picture holds the renderer for R ms
typically (its mean M ms and half its deviation of D ms; C ms of its own, I ms of it the
interpolation; ...)`, each a typical time, `PaceRtTyp`: [DISPLAY.md](DISPLAY.md#further-presents-and-the-game-step)): from
its job's start to `rd__reads_done`, which is the interpolation, then the first graphics call's waits
(the submit thread's job on the picture before, or with the slots apart on the picture two before,
the slot's fence, the acquire: `waitMs` and
`acquireMs`), then the uploads and the walk. C is R less those waits; I is the interpolation
(`rd_present`'s own timer around `rd__interp_frame`), not a wait. The submit thread's wait therefore
counts in R only past the interpolation, and the line's `the render thread waited for it N times
(T ms)` says how much. On a PowerVR phone R and C differ by 0.5-2.7 ms (4 ms a picture where the
driver's present blocked behind the GPU), so a hand-over longer than the interpolation, uploads and
walk measured without the submit thread comes from the render thread's own CPU time, not from the
submit thread; the 10 s line's cores (PLATFORM.md, the render thread) say where each thread ran.
No game code and no frame data change threads; the pictures are the same bytes.
The hand-over, not the frame ring, is what the game thread waits on: `rd_begin_frame` and
`rd_end_frame` wait for a picture's reads like every other call that changes the state, so three
frame slots (`RD_FRAME_RING`: the two the interpolation blends and the one recorded) are never
short and the render thread pins none. A new frame's present waits only for the end of the
picture before it (its submit and present), and a present that did not wait would gain nothing:
its own reads still come after that end, and the game's next step waits for them at its first
call that changes a texture, a mesh or a frame. Taking the game thread off the GPU's backlog
needs the state the walk reads in versions of its own (a frame ring of four with pins, the
textures' and meshes' changes carried with the frame and applied by the render thread); the
design is in PLATFORM.md's render thread section.

## Recording

A list is an array of fixed-size `RdCmd` records (40 bytes: a type, seven bytes, the draw's key,
four words, two floats); variable data (vertices, transforms, post parameters) lives in the frame's
payload arena (rd's own buffer, not the EE [arena](ARCHITECTURE.md#terms)), addressed by byte
offset. Every field has a fixed width and there are no pointers, so a frame can be written to a file
as it is and a dump reads the same on 32-bit and 64-bit builds (`port/render/rd_internal.h`
(`RdCmd`, `RdCmdType`)). An append is aligned (8 bytes in `rd__frame_payload`, 16 in the blended
frame's `outAppend` and `shadowOut`, rd_interp.c) and the gap before it is zeroed: a reused buffer
or a fresh heap block holds whatever was there, and the payload is dumped and hashed whole, so
the same frame gives the same bytes on either thread (the render thread has its own malloc arena).

| kind | commands |
| --- | --- |
| state deltas | `RDC_TEST`, `RDC_BLEND`, `RDC_ABE`, `RDC_ZWRITE`, `RDC_FBA`, `RDC_PABE`, `RDC_COLCLAMP`, `RDC_TEXA`, `RDC_FILTER`, `RDC_WRAP`, `RDC_TEXTURE`, `RDC_TEXTURE_OFF`, `RDC_UVOFFSET`, `RDC_COLORMASK`, `RDC_TARGET`, `RDC_SCISSOR`, `RDC_ALPHA`, `RDC_SHADE`, `RDC_AA1` |
| actions | `RDC_CLEAR`, `RDC_SCREEN`, `RDC_EXACT_BLEND`, `RDC_COPY` |
| VU draws | `RDC_MESH`, `RDC_SKINNED`, `RDC_GRID`, `RDC_PARTICLES` |
| shadows | `RDC_SHADOW_RESET`, `RDC_SHADOW_STRIP`, `RDC_SHADOW_RESOLVE` |
| post and text | `RDC_POST_STUB` (fog and blur sprites), `RDC_OVERLAY_TEXT` (collected by the present) |
| recorded only | `RDC_WORLD_PRIMS` (tests; no replay model) |
| no-op | `RDC_NOP` (the unused copy of the frame head; `rd_replay_tool --nop`) |

State calls record deltas and change nothing at once (`port/render/rd_core.c`). The callers of
each are listed in `port/render/rd.h` ("state deltas"); the ones that set one GS register alone
(`rd_abe`, `rd_blend_func`, `rd_scissor`, `rd_sampler_tex1`, `rd_sampler_wrap`, `rd_gouraud`,
`rd_aa1`) exist for the register decoder (`port/render/rd.h`).

**The frame ring.** The two most recent closed frames are retained for
[interpolation](ARCHITECTURE.md#terms); the frame being recorded takes a third slot
(`port/render/rd_internal.h` (`RD_FRAME_RING`, 3); `port/render/rd_core.c`).

**`rd_begin_frame`** applies pending settings (a texture pack switched off reverts its replacements;
the model pack and its dump switch; a change of the targets' scales recreates them on the host stack
and drops the interpolation history), halves the [scene scale](ARCHITECTURE.md#terms) when a
scene-sized texture did not fit the GPU since the last frame, opens the slot after the last closed
frame with the persistent state as its start state, and records the list defaults
(`port/render/rd_core.c` (`rd_begin_frame`)).

**`rd_end_frame`** keeps the frame head in the first replayed list (`rd__frame_head_resolve`),
walks the frame's state deltas in replay order from its start state to get its end state (the next
frame's start: state leaks from frame to frame as GS registers do), and with interpolation off
replays and presents the frame once on the host stack. With `ICO_RD_DUMP_EVERY` set it also writes
dumps (below) (`port/render/rd_core.c` (`rd_end_frame`, `rd__walk`)). `rd_discard_frame` drops an
open frame unreplayed, as the PS2 drops a display list it never kicks (`port/render/rd.h`).

**Keys.** `RD_KEY(obj, part, ordinal)` names the same logical draw in consecutive frames so the
presenter can pair them; key 0 snaps (`port/render/rd.h` (`RdKey`)).

**The draw filter.** The model viewer shows one object: `rd_set_draw_filter` records a world draw
only when its key's object is in the set, and `rd_draw_filter_open` learns the set from the
object's own display list. UI and full-screen prims, post passes, state and targets are never
filtered (`port/render/rd.h` ("the draw filter")).

## The state

`port/render/rd_state.h` enumerates the GS state the game reaches, from the sources, and the
pipelines are built from these enumerations, not from the whole register space.

**ALPHA.** `out = ((A - B) * C >> 7) + D`, A, B, D from Cs, Cd, 0 and C from As, Ad, FIX. The
twelve modes of `gif_SetAlpha` (`alphaTable`) are `RdBlend`; modes 3 and 8 to 11 are reachable
only through the BGA lightning record, so every entry is kept. FIX is 0..255 with 0x80 = 1.0,
passed through unchanged (`port/render/rd_state.h` (`RdBlend`)).

| RdBlend | ALPHA | equation | used by |
| --- | --- | --- | --- |
| 0 | 0x68 | Cs*FIX + Cd | shadow count, dissolve in |
| 1 | 0x62 | Cd - Cs*FIX | dissolve out |
| 2 | 0x64 | (Cs-Cd)*FIX + Cd | letterbox, motion blur, softening, fades |
| 3 | 0x29 | Cd*FIX + Cs | disc data only |
| 4, 7 | 0x44 | (Cs-Cd)*As + Cd | default material |
| 5 | 0x48 | Cs*As + Cd | additive, specular |
| 6 | 0x42 | Cd - Cs*As | subtractive material |
| 8, 9, 10 | 0x58, 0x52, 0x54 | the Ad forms | disc data only |
| 11 | 0x49 | Cd*As + Cd | disc data only |

**TEST** is decomposed into `RdTestState` (ATE, ATST, AREF, AFAIL, DATE, ZTE, ZTST); the fourteen
literal values the game writes are named `RD_TEST_*` so call sites stay greppable
(`port/render/rd_state.h` (`rd_test_from_gs`)). **TEXA** has three modes (`RdTexA`: 80/80, 7F/81
AEM, 80/80 AEM). **CLAMP** is REPEAT or CLAMP per axis. **TEX1**'s filters are nearest or linear,
and its mipmap fields pack into one word (`rd_tex1_lod`: MMIN, MXL, LCM, L, K) that picks the GS
level of a texture with GS levels. **TFX** is MODULATE or DECAL here; HIGHLIGHT reaches only the
staticBlur sprites, which carry it in their record (`port/render/rd_state.h`).

`RdDrawState` is the full block a list replays against. `RdPipelineKey` (16 bytes) is what
selects a GPU pipeline: program, blend path, Z test and write, colour mask, stencil mode, target
format, topology, AA1, the no-dual-source form and the sample count. Everything else (AREF, FIX,
sampler settings, PABE, FBA, DATE) goes into uniforms (`port/render/rd_state.h` (`RdPipelineKey`,
`RD_PIPELINE_REACHABLE_MAX`)).

## Frames and targets

**The frame head.** `rd_frame_head` records the flip's draw environment (FRAME with
[FBMSK](ARCHITECTURE.md#terms) 0, PSMZ32 Z write on, the centred XYOFFSET, COLCLAMP 1, TEST 0x50000)
and its clear at the head of both list 0 and list 11, since whether the frame replays from 0 or from
11 is known only when it closes; `rd_end_frame` turns the copy that is not first into NOPs.
`rd_frame_flip` patches the clear colour and the half offset in place (`port/render/rd_frame.c`
(`recordHead`, `rd_frame_flip`, `rd__frame_head_resolve`)).

**Named targets** stand in for fixed VRAM regions. The scene-class ones (SCENE, DISPLAY, WORK2,
AURA_WORK, the DATE snapshot, DISPLAY_HELD) are as wide as the
[native frame](ARCHITECTURE.md#terms) `W` in GS pixels; the
work buffers are the PS2's sizes per 512 columns, so a buffer pixel covers the screen pixels it
covers on the PS2 at every aspect (`port/render/rd_core.c` (`namedTargetDesc`, `sceneClass`)):

| target | size (GS pixels) | stands for |
| --- | --- | --- |
| SCENE | W x H, with depth and stencil | FBP 0x40 / TBP 0x800 |
| DISPLAY | W x H/2 | FBP 0, the shown buffer, retained across frames |
| SHADOW0, 1, 2 | W/2 x 256, W/4 x 128, W/8 x 64 | the shadow blur levels |
| WORK0, WORK3 | W/2 x 128 | staticBlur work buffers |
| WORK1, AA0 | W/2 x 256 | blur work; softening level 1 |
| AA1, FEED128, AURA_TAP | W/4 x 128 | softening level 2; aura feedback |
| WORK2, AURA_WORK | W x H | flare mask; aura buffer (with SCENE's Z) |
| WORK2_PAD | W/2 x 64 | a band the game clears and never reads |
| DATE_SNAPSHOT | W x H, R8 | the alpha MSB copy for DATE |
| FEED_HELD, DISPLAY_HELD | as FEED128, DISPLAY | host only: a [tick](ARCHITECTURE.md#terms)'s first present's copy |

**Scale.** A scene-class target's texture is its GS size times the scene scale; the work buffers
and the shadow levels keep their GS sizes, the PS2's, at every scale ([below](#the-glow-size);
`port/render/rd_core.c` (`rd__target_scale_of`)). A temporary target of any other size is drawn
at its GS size, except a render-to-texture block under Water reflections: Sharp
([below](RENDER_POST.md#water-and-render-to-texture-blocks)). Under
[Full pixel](ARCHITECTURE.md#terms) a frame-wide scene target holds the frame's exact visible width
plus `RD_FULL_PIXEL_GUARD` texels each side, centred, so the window's pixels are its texels
(`port/render/rd_core.c` (`rd__target_scale_of`)). [Scene scale](ARCHITECTURE.md#terms) and the
display options are [DISPLAY.md](DISPLAY.md).

**Temporary targets** (`rd_temp_target`) live until the next `rd_begin_frame` unless kept, and
come from a pool of parked textures of the same size (`port/render/rd_core.c`
(`rd__temp_target_alloc`)). A target with a new texture starts cleared to zero colour and
stencil and depth 1.0, so a start's first frames never show the driver's old memory
(`port/render/rd_upload.c` (`rd__clear_new_targets`)). When the GPU cannot hold a scene-sized texture
the scale is halved between frames and `rd_scene_scale_lowered` reports it to the Resolution row
(`port/render/rd_core.c` (`rd__note_scene_pressure`, `rd_begin_frame`)).

**Z.** The shaders map GS Z to a depth that grows with it (`gs_z_to_depth`), so GS GEQUAL is the
GPU's GEQUAL. The scale is per target Z format: PSMZ32 is z * 2^-33 on a D32S8 buffer, z * 2^-32 on
the Vulkan D24S8 fallback; PSMZ24 2^-24; PSMZ16 2^-16 (`port/render/rd_frame.c`
(`rd__target_z_scale`, `rd__gs_depth`)).

**The VU block and the camera.** `rd_set_vu_common` keeps the last block of the open frame and
updates the VU image of all 13 lists; `rd__fill_camera_cb` puts the frame camera into FrameCB
(view, projection, their product, the eye, near, far, zoom, aspect) (`port/render/rd_frame.c`).

### The glow size

The work buffers (WORK0, WORK1, WORK3, AA0, AA1, FEED128, AURA_TAP, WORK2_PAD, FEED_HELD: the
glow's, the depth of field's, the softening's and the aura's chains) are the PS2's sizes at every
resolution, so each chain runs the PS2's passes on the PS2's texels, and a pass that draws a work
buffer back onto the scaled frame reads it with the GS's own filter at the frame's finer grid
(`port/render/rd_core.c` (`rd__target_scale_of`, `rd__work_class`); `port/render/rd_replay.c`
(`blurAbove1xSteps`), `rd_fx_plan.c` (`rd__sprite_above1x`)). The filter is the one the game sets: the glow's paste WORK3 -> SCENE
(`ico2/sugipon/src/staticBlur.c` (`pasteFullScreenFlare`); the title dump's 7:367) and the depth
of field's four pastes WORK0 -> SCENE (`pasteToFB`) draw under TEX1 0x60, MMAG and MMIN both
LINEAR (written by `eyeBlur` and `pasteToFB`; the dump's `FILTER 1 1` at 7:341), and the eye
blur's subtract into SCENE under 0x40, MMAG NEAREST, a single texel (`eyeBlur`). Above 1x each
SCENE texel therefore takes the GS's U and V at its own GS position (12.4, the texel's centre:
[Post passes](RENDER_POST.md#post-passes)'s scaled-target rule in `fx_sprite_ps`) and the GS's 4-bit bilinear
between the same four work texels the PS2 blends there (`gs_bilerp`): the PS2's 256 x 128 glow
magnified as the GS magnifies it, sampled more finely; at 1x it is the GS's picture byte for byte.
The softening's AA0 and AA1 back over SCENE (`sprite_ps`) take the draw's own sampler, as at 1x
(`rd__sprite_above1x`). The draws from SCENE, WORK2 or AURA_WORK into a work buffer keep the area box
([Above 1x](RENDER_POST.md#above-1x)). `rd_blur_test`'s `effects_scaled` and `above1x` cases compare every
target with a CPU model of that arithmetic at 2x and 4x to the bit, and `rd_shadow_test`'s 4x
softening the composite with the bilinear between AA0's texels
(`port/render/test/rd_blur_test.c` (`checkEffectsScaled`, `checkAbove1x`, `workBuffer`);
`port/render/test/rd_shadow_test.c` (`checkSoftening4x`)).

The glow's cost per frame on the issue 95 title dump (`loss-20261015-100428-lost.rddump`, 512 x
448 at 4:3): 33 `fx_sprite_ps` draws and two sprites drawn as clears (`blurSpriteClears`: the
WORK2_PAD band and WORK2's fill) in 35 render passes (`--stats`: `glow 35 passes` at every
scale). Four draws cover the frame (the two of `cleanUpFB` into SCENE, the alpha
mask SCENE -> WORK2, the paste), 29 a 256 x 128 work buffer (the reduction WORK2 -> WORK0, the 20
blur passes, the eye blur's five, its shrink, tint and `pasteWork0ToFB`). The fragments are the
frame-sized draws' texels plus the work draws'; the paste's texel loads are its texels times its
taps: the GS's bilinear four, against the tool of 2130df09c's work scale (2 at 2x and above) with
the B-spline down (and across at 3x) or, with its `--glow-original`, the work buffers at their GS
sizes and the B-spline both ways (16). GPU milliseconds of the glow effect, lavapipe (Mesa
25.0.7, `ICO_RD_BLEND=fixed`, the fastest of three serial runs of `rd_replay_tool DUMP out.png
--stats [--enhanced --resolution Nx --present 1376x896]`):

| scale | fragments: work scale; GS sizes | paste loads (taps): work scale; `--glow-original`; the GS's filter | glow ms: work scale; `--glow-original` | glow ms, the GS's filter |
| --- | --- | --- | --- | --- |
| 1x | 1.87 M; the same | 0.92 M (4) in all three | 46.7; the same | the same draws |
| 2x | 7.47 M; 4.62 M | 7.34 M (8); 14.68 M (16); 3.67 M (4) | 225.0; 181.4 | 93.6 |
| 3x | 12.06 M; 9.21 M | 33.03 M (16); 33.03 M (16); 8.26 M (4) | 440.7; 372.0 | 169.3 |

The work scale and `--glow-original` columns are the tool of 2130df09c; the last is the tool with
the work buffers at their GS sizes and the GS's filter at the paste. In one series on the same
host the 2130df09c tool's glow is 42.6 ms at 1x and 35.5 with the paste turned off (`--nop
7:367`), 175.6 and 68.4 at 2x with `--glow-original`, and 113.6 without the paste at 3x with it:
the 1x paste (the GS's bilinear over 229,376 texels and SCENE's snapshot) costs 7.1 ms, so at the
GS's filter it costs about 28 ms at 2x and 64 at 3x there. The last column's 2x and 3x are the
release's verification run, the tool after the change against 2130df09c's in three interleaved
pairs of replays on the same host, the fastest of each (GPU ms of the glow, lavapipe):

| frame | 2130df09c | its `--glow-original` | after |
| --- | --- | --- | --- |
| title 1x | 45.3 | 45.0 | 42.3 |
| title 2x | 221.4 | 178.8 | 93.6 |
| title 3x | 438.1 | 362.9 | 169.3 |
| Mac 2x | 253.7 | 219.6 | 106.0 |
| forest 2x | 195.7 | 192.8 | 94.0 |

At 1x every target is equal before and after on the three dumps of that run.
The other costs above 1x are the frame-sized draws (four, at the scene's scale) and the reduction
WORK2 -> WORK0, a box of 4 x 8 texels at 2x (`blurAbove1xSteps`).

### The glow's passes

On a tile-based GPU each render pass costs its loads and stores (a 2364 x 1080 colour target is
10.2 MB each way, about 1 ms per 10 MB on the PowerVR phone) and a fixed cost (about 0.24 ms for
each small work-buffer pass there). The PowerVR phone's 10 s line in a glow room at Resolution Auto
(2026-11-02): `glow 11.44` ms of a 31 ms picture, `glow WORK2 2366x1080 2.0 passes 1 draws 4.73 ms,
glow SCENE 2366x1080 3.0 passes 3 draws 3.54 ms, glow WORK0 424x128 12.0 passes 12 draws 2.90 ms`,
21 glow passes a replay. The chain at the phone's setting (`rd_replay_tool DUMP x --stats
--pass-times --enhanced --resolution 2364x1080 --full-height --full-pixel --present 2364x1080` with
`ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0`, the godrays frame rd-01077; `--list --no-device`
for the commands), the bytes at 2364 x 1080:

| pass | target | draws | what (`ico2/sugipon/src/staticBlur.c`) | reads | loads, stores |
| --- | --- | --- | --- | --- | --- |
| fog | SCENE | the fog's | list 4 | SCENE's depth (local) | |
| 1 | SCENE | 1 | `cleanUpFB`'s first sprite: SCENE's alpha 0 (the constant fold, Z ALWAYS) | | 10.2 MB, 10.2 MB |
| 2 | SCENE | 1 | `cleanUpFB`'s second: alpha 192 under Z GEQUAL (needs the depth) | SCENE's depth | 10.2 MB + the depth, 10.2 MB |
| 3 | WORK2_PAD | 0 | `fillWork2`'s band (a clear) | | 0, 0.1 MB |
| 4 | WORK2 | 0 | `fillWork2`'s fill (a clear) | | 0, 10.2 MB |
| 5 | WORK2 | 1 | `makeMaskPatternToWork2`: SCENE's alpha into WORK2 (fx plan, the alpha alone) | SCENE | 10.2 MB, 10.2 MB |
| 6 | WORK0 | 1 | `reduceWork2ToWork0` (the box above 1x) | WORK2 | 0.1 MB each way |
| 7 .. 15 | WORK0 | 1 each | nine `blur(n)` pairs folded (`fx_blur2_ps`) | WORK0's snapshot | 0.1 MB each way each |
| 16, 17 | WORK1, WORK0 | 1, 1 | the last pair as two passes (and the edge strip on a wide frame) | | |
| 18 | WORK3 | 1 | `eyeBlur` (with the sun: its ghosts, shrink into WORK1 and tint) | WORK0 | |
| 19 | WORK0 | 1 | `pasteWork0ToFB`: WORK0's alpha (the constant fold) | | |
| 20 | SCENE | 1 or 2 | `pasteFullScreenFlare`: WORK3 onto SCENE (the premultiplied add) | WORK3 | 10.2 MB, 10.2 MB |

Where the sun is on screen its fans (`dispSun`) draw into WORK2 between passes 4 and 5 and take the
fill as their pass's load op (6 of the 14 corpus glow frames); the issue 163 frames draw the lamps'
meshes into WORK2 after the mask, a pass of their own (they test SCENE's depth).

Two of the breaks moved bytes nothing needed, and are gone (the same bytes; `rd_glow_cheap_ab`):

- **WORK2's fill in the mask's pass** (pass 4). The mask reads SCENE, and the end of the open pass
  for that read (the mask's transition of its source in `fxDraw`, and SCENE's border repeat before
  it where the border is open) recorded the pending fill as a pass of its own. Both now end the pass
  through `rd__replay_end_pass_for` (`endPassFor`, the clear carry: on on the tile class), so the
  mask's pass takes the fill as its CLEAR load ([WORK_BUFFERS.md](WORK_BUFFERS.md#passes-cut)):
  one full-size store and load fewer (about 2 ms of the 4.73 by the arithmetic). Where the fans
  drew first nothing changes.
- **cleanUpFB in the fog's pass** (passes 1 and 2). The first sprite uses no depth, so the constant
  fold drew it in a pass without the depth, which ended the fog's pass, and the second (GEQUAL) began
  a third. `fxAttachDepth` (`port/render/rd_fx_plan.c`) draws a constant fold that uses no depth in
  the open pass on its target when that pass has the state's depth, through its key with the depth
  attached under Z ALWAYS without Z write (the test passes every fragment and writes nothing), so the
  second sprite's own key lands in the same pass: two full-size SCENE passes fewer where the fog's
  pass has the depth (the tile class's local fog; about 2.4 ms of the 3.54 by the measured share).
  Where the crack fill is drawn (above 1x with a fog: every phone frame) it waits past the two
  sprites (they write SCENE's alpha alone) and is drawn after them with RGB alone, the same bytes
  ([the fill's place](RENDER_POST.md#the-fills-place-after-list-7)), so the sprites stay in the
  fog's pass with the fill as well. Drawn at the start of list 7 (`ICO_RD_CRACK_PLACE=7`) the fill
  ends the fog's pass there and the two sprites take a pass each again (v4251 at the phone's profile on lavapipe: 22
  glow passes with the fill, 20 without).

What is left: the mask's pass (SCENE read, WORK2 stored, then read by the reduction), the paste's
SCENE pass (WORK3 is drawn after the fog's pass ends), and the twelve small WORK0 passes (the fixed
cost). Not done, and why: the mask at half size (its alpha test makes a hard-edged mask, and the
reduction's box over it moves by a whole texel at an edge: a difference no stored dump bounds
yet); the mask folded into the reduction (exact, but `fx_sprite_ps`'s box would compute the mask's
own bilinear and alpha test per texel it reads, a new shader form, and the fans and the lamps'
meshes draw into WORK2 in 9 of the 14 frames); two blur pairs in one pass (exact needs pass 1's
texels for pass 2's for pass 3's: 256 loads a texel or a window of intermediates per pixel; a blur
of a blur in floats differs by the GS's truncation at each of the four steps, up to about 4 levels
before the eye blur, and the twelve passes' fixed cost is about 2.9 ms). The glow pass count is the
`--stats` `glow N passes` and the 10 s `passes per replay` line's `glow`.

## Textures

The texture cache decodes the TIM2 bytes Texture.c keeps in memory into an rd texture when first
needed and keeps it until the content changes (`port/render/rd_tex.h`; decoded on worker threads:
[TEXTURES.md](TEXTURES.md)):

- **key**: (texture id, content generation, TEXA mode); the mode is one of `RdTexA` (alpha baked)
  or `RDTEX_TEXA_REPLAY` (the source format's alpha kept and TEXA applied by the shader at replay,
  so TEXA leaks between lists as on the GS).
- **decode**: PSMCT32, 24, 16, 16S, PSMT8, PSMT4, PSMT8H, PSMT4HL, PSMT4HH and the PSMZ formats;
  CLUTs in CSM1 or index order. Output RGBA8 with the raw GS alpha (0x80 = 1.0) for 32-bit texels,
  the A bit for 16-bit ones and 0 for 24-bit ones (`rdtex_decode`).
- **size**: padded with zero texels to 2^TW x 2^TH, so STQ and REPEAT wrap as on the GS.
- **update**: a new generation of the same size and format re-expands into the same rd texture;
  otherwise the old one is retired and destroyed two `rdtex_frame_tick` calls later. A slot loaded
  with another picture is dropped first ([GS.md](GS.md#textures)).
- **levels**: a mipmapped TEX1 stores the TIM2's own levels when they are authored pictures
  (`rdtex_store_levels`, `RDTEX_AUTHORED_RATIO`), sampled at the GS's level; otherwise the
  replay builds the further levels by a 2x2 box, colour weighted by alpha, with alpha coverage
  kept for alpha-tested textures (`port/render/rd_upload.c` (`uploadMips`);
  `port/render/rd_tex.h` (`rdtex_build_mip_chain`, `rdtex_keep_alpha_coverage`)).

**Sampling at the GS's level in the Original preset.** A linear-filtered draw of an image with
GS levels (`DF_GS_LOD`) is filtered as the GS filters, in one of two forms
(`port/render/rd_pipeline.c` (`rd__gs_filter_int`, `rd__texa_per_texel`)):

- **TEXA per texel**: a 24- or 16-bit source (or CLUT) under a TEXA whose alpha depends on the
  texel (AEM, or TA0 and TA1 apart: every mode but 80/80) goes through the TEXA entries
  (`sprite_texa_ps`, `vu_texa_ps`, and `sprite_stq_ps`'s four taps), whose integer filter loads
  the four texels of each level, expands TEXA, blends them with the GS's 4-bit weights across
  then down, each step truncated, and blends the two levels in 256ths (`port/shaders/common.hlsli`
  (`gs_texa_texture_lod`)).
- **The sampler on the GS's grid**: every other one (an RGBA32 texture; TEXA 80/80, which gives
  every texel the same alpha) takes the sampler (`ICO_DF_GS_BILINEAR_GRID`, `common.hlsli`
  (`gs_sample_gs_bilinear`, `gs_grid_level`)). Per level the coordinate is rounded to the GS's
  12.4 grid, `q = floor(uv * size * 16 + 0.5)`, as the integer filter rounds it, and handed to
  `SampleLevel` as `q / 16 / size`, exact in float for the power-of-two sizes of a TIM2 picture,
  so the sampler's texel pair and weight are the GS's (`(q - 8) >> 4` and `(q - 8) & 15`, four
  fraction bits, which every device's sub-texel precision holds); the sampler is nearest between
  levels (`port/render/rd_replay.c` (`gsLodSampler`)), and for MMIN 3 and 5 the shader samples
  the two levels and blends them by the LOD's fraction in 256ths, truncated, as the GS does. What
  is left is the GS's truncation after each of its three steps against the sampler's one rounded
  sum: the sampler's value is 0 to 2 levels above the GS's, never below, on a device whose filter
  is exact to within a level (`shaders_pixel`'s grid check, over every pair of 4-bit weights and
  texel values in steps of 17: 21 % of the samples +0, 74 % +1, 4 % +2; a flat area is +0, so the
  difference is a systematic half to one level where texels differ), and the blend of two levels
  keeps the bound. `rd_pixel`'s gs grid case checks it
  through the replay against the GS's arithmetic.

The replay counts both: `rd_replay_tool --stats` prints `texa draws N (TEXA per texel), M
GS-level draws on the GS's grid` (`RdPerfRecord` `texaDraws`, `gridDraws`, per planned pass, as
`a1xDraws`), and the first grid draw of a run logs `rd: GS levels (Original): ...` once.

## The replay

`rd__replay_frame` records one RHI command list per replay. The lists are walked in order 0..12
(11 and 12 for a keep frame) from the frame's start state; state commands update the state block
and actions draw (`port/render/rd_replay.c`). The replay's end, the present's pass, the end of the
list, the submit, the present and the capture's finish, is `rd__submit_frame`
(`port/render/rd_submit.c`), where a picture's reads of the renderer's state end (above):

| command | replay |
| --- | --- |
| `RDC_CLEAR` | a render pass with a load-op clear, or a pending clear folded into the next pass |
| `RDC_SCREEN` | GS window-space prims through `sprite_*_vs` / `sprite_ps` (`doScreen`) |
| `RDC_EXACT_BLEND` | `blend_int` on RGBA8_UINT copies of source and destination, copied back (`doExact`) |
| `RDC_COPY` | a texture copy (`doCopy`) |
| `RDC_MESH` .. `RDC_PARTICLES` | the VU1 program shaders (`rd__replay_vu`, `port/render/rd_replay_vu.c`) |
| `RDC_SHADOW_*` | the stencil count (`doShadowReset`, `doShadowStrip`, `doShadowResolve`) |
| `RDC_POST_STUB` | `RD_POST_FOG`: `doFog`; the reduction and the staticBlur kinds: `rd__blur_post_sprite` (`port/render/rd_blur_border.c`) |
| `RDC_OVERLAY_TEXT` | nothing here: the present collects it |
| anything else | `rd__not_implemented`: logged once and skipped (fatal in the tests) |

**GS sampling rules.** The GS samples a pixel at its integer coordinate and covers `x0 <= x < x1`
for a sprite; the GPU samples at x + 0.5 with the top-left rule. FrameCB's origin moves GS
coordinates half a pixel right and down, so a GS integer lands on a GPU pixel centre and the two
rules select the same pixels. The game's -4 corner nudges and +8 UV nudges stay in the data
(`port/render/rd_replay.c`, header comment).

**Screen prims.** `expand` turns sprites, fans and strips into triangle lists, points into
one-pixel sprites and line strips into line lists, after the mirror mode's UI flip. Sprites on a
scaled target have their corners snapped to whole GS pixels. Consecutive `RDC_SCREEN` commands
that bind the same things are one draw (`joinsRun`, `flushScreenRun`); every action that records
anything flushes the run first (`port/render/rd_replay.c` (`expand`, `doScreen`, `ScreenRun`)).
A triangle with STQ is drawn perspective-correct, and the screen-prim triangles of the sprite
shaders carry their own colour and STQ planes (below) (`port/render/rd_replay.c`).

**Passes.** Render passes stay open across draws to the same colour and depth pair and close on a
target change, a clear, a copy, or a draw that samples a target whose state must change. rd tracks
every texture's RHI state and records every transition; a backend adds no texture transition of its
own (D3D12 keeps a record only to fill in a transition from UNDEFINED,
[RHI.md](RHI.md#direct3d-12)). A draw that samples its own render target samples a copy taken just
before it, as the GS reads the live buffer (`port/render/rd_replay.c`, header comment).

**The pass area.** On a tile-based GPU a render pass on SCENE whose draws are all 2D sprites and
shapes with a known place (screen prims, the aura's paste) takes a render area of the 32 x 32 tiles
they touch (`RhiRenderPassDesc.area`, [RHI.md](RHI.md#vulkan)), so only those tiles are loaded and
stored; the pixels outside are untouched, which no draw of the pass reaches, so the picture is the
same bytes. The scan before the walk (`rd__pass_area_scan_cmd` in the store plan's one walk of the
lists, `rd__store_plan_scan`; [Performance notes](#performance-notes)) gives every command its box, `beginPass` asks for the union from its command on, and the walk ends such a pass
before the first command past it (`rd__pass_area_due`) ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-pass-area);
`port/render/rd_pass_area.c`, header comment; `[dev] pass_area`).

**Passes cut.** On a tile-based GPU three passes, or parts of them, that moved bytes nothing looks
at are cut, the picture the same bytes: a pending clear stays pending past the end of a pass for a
source's barrier and is taken as the next pass's load (the reduction draws DISPLAY in one pass, not
a clear's pass and a draw's), a clear whose only reader in the replay is a sprite the constant read
drew in its colour is not drawn (AURA_WORK's; drawn later where anything reads the target before
clearing it again), and the pass that holds a depth's last touch does not store it when the walk is
known to keep that pass open until the touch is done (the fog's pass on the phone)
([WORK_BUFFERS.md](WORK_BUFFERS.md#passes-cut); `port/render/rd_pass_cut.c`, header comment;
`[dev] clear_carry`, `clear_skip`, `last_store`).

**The exact viewport.** While a frame's lists replay, every pass has one viewport size, the
largest power of two the GPU takes, centred on the target, and FrameCB maps positions to it; the
scissor, always inside the target, bounds the drawing. Every step is exact for a GS position at an
integer scale, and the clip-space numbers are the same at every frame width, so the GPU clips the
same corners the same way whatever the width (`port/render/rd_replay.c` (`s_vpExact`)). The
present and the camera probe keep the plain mapping.

**Uploads.** Before the first pass the replay uploads pending textures and their levels through
the frame's upload ring, and the static meshes the frame draws into the device arena (once, and
again only after a morph rewrote them); the interpolation's scratch meshes stay on the ring
(`port/render/rd_upload.c` (`rd__upload_dirty_textures`, `rd__upload_meshes`)); the ring shrinks again ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-upload-ring-shrinks)). Every
`rd__ring_alloc` caller checks for `~0` (the ring is full) and skips its write and its draw or
copy, the untextured draws' white texel included, which the next replay uploads
(`port/render/rd_replay.c` (`rd__ring_alloc`); `port/render/rd_upload.c`
(`rd__upload_dirty_textures`)). A text texture (a coverage format: a menu text page or a glyph
page) that was not made, that the device refused, or that found the ring full is named once in
the log, `rd: text: a menu texture could not be uploaded (...)` (`port/render/rd_tex.c`
(`rdtex_text_upload_failed`), from `rd_create_texture_sheet` and `rd__upload_dirty_textures`; a
glyph page `rd_create_texture_r8` could not make logs the same words with `ui:`,
`port/ui/font.c` (`pageTexture`)).

**Bind groups.** Bind groups are valid only in the [frame slot](ARCHITECTURE.md#terms) they were
made in, so their caches are tagged with an epoch every `rhi_wait_frame` of the renderer starts anew
(`rd__wait_frame`). Uniform blocks are written once per replay per content (FNV-1a), and texture
groups are cached by (texture, sampler, DATE snapshot) (`port/render/rd_replay.c` (`s_bindEpoch`)).

**DATE snapshots.** TEST.DATE tests the destination alpha's MSB. The replay copies the bound
target's alpha MSB into the R8 `RD_TARGET_DATE_SNAPSHOT` over the area the draw can touch, and the
shader tests it at the fragment's own texel. The snapshot is retaken when the target changed or
anything may have written alpha since, so consecutive DATE draws see each other's writes; within
one draw, overlapping primitives see the snapshot (`port/render/rd_replay.c` (`dateSnapshot`,
`rd__date_retake`)).

**DATE through the stencil bit.** Where the profile has `dateStencil` ([The device
profile](#the-device-profile)), a DATE draw of a screen-prim or VU command into a scene-class RGBA8
target with a depth of its size bound takes no snapshot: inside the draw's own pass (begun with the
colour readable, `rd__pass_wants_readable`) bit 6 of the stencil (`RD_DATE_STENCIL_BIT`; the shadow
count keeps bits 0-5, [Shadows](RENDER_POST.md#shadows)) is made the destination alpha's MSB over the area the
draw can touch: `date_clear_ps` writes 0 into the bit (`RD_STENCIL_DATE_CLEAR`), a by-region
barrier makes the pass's colour writes visible (`rhi_cmd_local_read_barrier`, colour),
`date_stencil_ps` reads the alpha as an input attachment and writes 1 into the bit where the MSB is
set (`RD_STENCIL_DATE_SET`), and a second barrier orders that read before the next writes. The
draw's passes then test the bit (`RD_STENCIL_DATE_TEST1` for DATM 1, `TEST0` for DATM 0,
reference 0x40, read mask 0x40, no write) with `DF_DATE` off (`rd__date_stencil_pass`, in
`expandPasses`; `dstBind` sets the reference). The bit is kept by `rd__date_retake`'s rule on the
pass's own record (the same pass, target and write serial, the area held), so it is written again
exactly where a snapshot would be retaken, and the open screen run is drawn before the read.
Within one draw the alpha is the one before its first primitive: the snapshot's semantics, the
same bytes (not PRIM.AA1: **DATE and AA1**). Not inside the MSAA epoch, under the exact blend's storage read, on another target or
without a depth: the snapshot. The DATE self-test decides per device ([The fog path and its
self-test](RENDER_POST.md#the-fog-path-and-its-self-test)) (`port/render/rd_replay.c` (`dateStencilFor`,
`prepareDrawEx`, `bindDrawEx`); `port/render/rd_replay_tiler.c` (`rd__replay_date_take`);
`port/render/rd_pipeline.c` (`shadowStencil`,
`rd__date_setup_key`, `rd__enumerate_reachable_date`); `port/shaders/sprite.hlsl`
(`date_clear_ps`, `date_stencil_ps`)). `--stats`: `DATE snapshots N, DATE stencil draws M (K
setups)`.

**DATE through the colour read.** Under GS state as shader data ([GS state as shader
data](#gs-state-as-shader-data)) a DATE draw of a screen-prim or VU command into an RGBA8 target
made for the attachment read, outside the MSAA epoch, takes neither the snapshot nor the stencil
bit: its passes go through the `_dstatt` twins with `ICO_DF_DATE_DST`, and the shader tests DATE
against the alpha it reads from the attachment (`port/shaders/common.hlsli` (`GS_DATE_SNAP`,
`gs_dst_date_snap`); `port/render/rd_uber.c` (`rd__uber_date`); `port/render/rd_replay.c`
(`prepareDrawEx`: `Replay.dateDstNow`, `RdPerfRecord.dateDstDraws`)). The read is in
rasterization order, so each fragment sees what the draw's earlier primitives wrote: the GS's
DATE, primitive by primitive, where the snapshot and the stencil bit hold the alpha from before the
draw's first primitive (DIVERGENCES.md VD-17). A PRIM.AA1 draw under DATE needs no stencil pairs
then (`doScreen`: `aa1Date` off); one that writes Z, two draws otherwise (its triangles' own
pixels, then every fringe without Z write), is drawn triangle by triangle, each in its two draws,
so a triangle's pixel still meets the alpha an earlier triangle's fringe wrote (`aa1Order`,
`port/render/rd_replay_aa1.inc` (`aa1OrderPlan`, `aa1OrderDraw`); the first command logs
`aa1: a DATE draw's triangles drawn one at a time through the colour read ...`; ctest
`rd_pixel_uber` aa1 wedge). DATE's self-test checks it first ("colour read",
its fifth sample a DATM 1 command whose second sprite fails over its first), then the stencil bit
and the snapshot with it held off; a failure turns it off (`g_rd.uberDate`) and DATE draws as
above. ctest `rd_pixel_uber` (`testDateStencil`: the overlap of two sprites of one DATE command
takes the GS's colour), `rd_date_selftest_uber`.

**DATE and AA1.** The GS tests DATE against the alpha as it writes each pixel (PCSX2
`GSDrawScanline.cpp`), so in an AA1 strip under DATM 1 the edge pixel a triangle writes along a
shared edge (alpha below 0x80) turns away the next triangle's pixel there; testing every primitive
against the alpha before the draw added both rows (the ripples' bright outlines, `puddle.c`). A
PRIM.AA1 draw under DATE that writes alpha is drawn primitive by primitive inside its pass, each
primitive as two draws, the fragments that keep the alpha's MSB as DATE wants it and then those
that change it (`DF_AA1_MSB_SET`, `DF_AA1_MSB_CLEAR`), whose stencil carries bit 6 to the next; a
triangle's split again by Z write: up to four draws a triangle, at most 320 for the 223424 dump's
ripple command 4:147, no pass break. The stencil path keeps bit 6 the MSB (`TEST1` and
`TEST1_CLR`, `TEST0` and `TEST0_SET`); the snapshot path keeps `DF_DATE`, clears the bit over the
area first (`rd__replay_date_clear`) and marks the texels the draw changed (`TEST0`,
`TEST0_SET`). The storage read, the MSAA epoch and a depth not of the target's size keep one test
per draw, logged (`aa1: a DATE draw keeps DATE's one test per draw ...`; the first command drawn
so: `aa1: a DATE draw's primitives drawn one at a time ...`) (`port/render/rd_replay_aa1.inc`
(`aa1DatePlan`, `aa1DateDraw`), which says why it is exact; ctest `rd_pixel` aa1 wedge).

**The fog's table.** The replay's upload phase copies the first `RD_POST_FOG` record's 256-entry
table of the lists it draws into the LUT texture, outside any pass; `doFog` copies a table only
when its fog's is another (`port/render/rd_replay.c` (`rd__fog_lut_upload`, `fogLutCopy`)).

**The fog's local read.** On the `local` fog path ([The fog path and its
self-test](RENDER_POST.md#the-fog-path-and-its-self-test)) every pass on a scene-class target with its own
depth, the depth made for it (`RD_DST_USAGE_LOCAL_DEPTH`), binds the depth readable
(`RhiDepthAttachment.readable`, the texture in `RHI_STATE_RENDER_TARGET_READ`), and a fog whose
depth is its colour target's own (SCENE's, at its size) draws in that pass: a by-region barrier
(depth), `fog_lut_local_ps` reading the fragment's own depth texel as an input attachment, a
barrier back; the pass is neither ended nor begun again and the depth is not copied, and the
`fog copy` piece of the per-pass timing is not begun. The depth word and the arithmetic are the
copy path's (the sprite's UV addresses the fragment's own texel there), so the picture is the same
bytes. A fog on another depth, inside or before the MSAA epoch, or under an exact blend takes the
copy (`port/render/rd_replay.c` (`fogLocalFor`, `doFog`, `depthWantsReadable`);
`port/shaders/fog_lut.hlsl` (`FOG_LOCAL`)). `--stats`: `fog local draws N (fog path local)`.

**The blur pair fold.** Where the profile has `blurFold`, a pair of staticBlur.c's `blur(n)`
(pass 1 WORK0 -> WORK1 with the rect shrunk, pass 2 WORK1 -> WORK0 with the UVs shrunk) is drawn
as one pass into WORK0 through `fx_blur2_ps`, which computes each WORK1 texel pass 2 weighs from
WORK0 as pass 1 writes it (its UV, the four WORK0 taps, `gs_bilerp`, the texture function with
pass 1's colour, FBA, TEXA): the integers of the two passes in their order, so WORK0 is the same
bytes. At pass 1 the replay looks ahead over the list (`s_loop`): state commands, pass 2, state
commands, the next pass 1; it folds when both passes are textured from each other at scale 1
without a crop (the same size and origin), LINEAR, CLAMP, no blend, alpha test, DATE, destination
read or depth, the colour mask whole, the same flags, TEXA and TEX0 size, every WORK1 texel pass 2
weighs (a weight above 0) holding pass 1's bytes (its coverage, or the glow's edge strip), and the
next pass 1 leaving its bytes in those texels or more, so WORK1 ends as the unfolded chain's; the
loop then skips to pass 2 with the state applied. WORK0 is read through its snapshot (the swap
where the fold covers it whole). Of ten pairs 9 fold at 4:3 and 16:9, 8 at 48:9
([WORK_BUFFERS.md](WORK_BUFFERS.md#the-blur-fold-on-a-wide-frame)) (`port/render/rd_blur_border.c`
(`rd__blur_post_sprite`); `port/render/rd_replay_tiler.c` (`rd__replay_blur_fold`, `blurFoldPass`,
`blurFoldOk`); `port/shaders/fx_sprite.hlsl` (`fx_blur2_ps`);
`port/shaders/shader_consts.h` (`IcoDrawCB.aux0`)). Start line, once: `rd: blur fold: on (<class
or [dev] blur_fold>): each staticBlur pair but a chain's last drawn as one pass (fx_blur2_ps)`;
`--stats`: `blur pairs folded N`.

**Store ops.** Every pass stores its colour; a pass after its depth's last touch neither loads nor
stores the depth ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)); else the stencil's store
follows the shadow window (`rd__stencil_ops_packed`).

**Exact blends.** `RDC_EXACT_BLEND` (from `rd_post` with `exactInt`) runs the GS integer blend
through `blend_int` on RGBA8_UINT copies of both targets and copies the result back; source and
destination must be the same size (`port/render/rd_post.c`; `port/render/rd_replay.c`
(`doExact`)).

**AA1.** PRIM.AA1 on lines and triangles: `aa1Expand` adds a one-GS-pixel fringe along each edge
(outside it, along its minor axis) whose coverage runs from 1 on the edge to 0 and is the fragment's
alpha in `sprite_aa1_ps`; a triangle's own vertices carry `ICO_AA1_INTERIOR`. A draw whose fringes
need Z write off is split in two: every interior with the draw's Z write, then every fringe without
(`port/render/rd_replay_aa1.inc` (`aa1Band`, `aa1Fringe`, `aa1Expand`), included by
`port/render/rd_replay.c`). A triangle's fringe ends where PCSX2's software renderer ends it
(`GSRasterizer.cpp` `DrawTriangle`, `DrawEdgeTriangle`): an edge pixel is kept only where the
triangle's other two edge functions are above 0 (integer, 12.4, with the vertices sorted by y the
GS's way, `s_ysort`; negated for a clockwise triangle; plus 1 on a top-left edge:
`tl0 = y0 == y1 || !cw`, `tl1 = cw`, `tl2 = y1 != y2 && !cw`) and inside the edge's box, x from
floor(min x) to ceil(max x) and y from ceil(min y - 1) to floor(max y + 1) in GS pixels. The band is
drawn past both ends of its edge by a whole number of edge lengths, at least two GS pixels
(`aa1Reach`: the ends stay exactly on the edge's line, the attributes carried along it and clamped),
and `sprite_aa1_ps` (`aa1_wedge_keep`) keeps or discards each fragment with those tests on the
triangle's flat fields (`IcoSpriteAa1Vertex` `v0x`, `v0y`, `d1`, `d2`, `flags`: the sorted first
vertex, the other two less it as int16 pairs, the edge, clockwise and the three biases). On the
sample points' 1/16 grid the box is `X > min x - 16`, `X < max x + 16`, `Y >= min y - 16`,
`Y <= max y + 16` (12.4), which needs no XYOFFSET; the sample point is the GS integer coordinate on
the texel's centre (`aa1_pixel_xy`: XYOFFSET + 16 k at 1x exactly, the texel's own point rounded to
1/16 above 1x). Clipping the geometry instead would round the cuts to the 1/16 grid and flip pixel
centres. A triangle wider or taller than `ICO_AA1_CLIP_EXTENT` (32751 in 12.4) keeps its fringes
whole and unclipped (`flags` 0, as lines and interiors), so the integer tests stay inside 32 bits.
At 1x the pixels drawn are PCSX2's exactly (ctest `rd_pixel`, aa1 wedge: every pixel's fragment
count against a C model of `DrawTriangle` and `DrawEdgeTriangle`). A band ending in an axis-aligned
cap at each vertex, unclipped, draws pixels past the corner outside the wedge (the spurs at the
corners of the drained room's ripples, issue 106) and misses some inside it. The fringes of an edge
two triangles share are both drawn, as on the GS. DATE inside one AA1 draw is the snapshot's (**DATE
and AA1** above). The log says
`rd: aa1: triangle edges clipped to the GS's wedge and box (the first command: N triangles, N clipped, N vertices)`
once, and
`rd: aa1: a triangle W x H GS pixels is past the edge clip's reach (2046): its edges drawn unclipped (N so far)`
for the first three such triangles and then at each power of two.

**COLCLAMP 0.** Additive and subtractive screen prims under COLCLAMP 0 keep the low 8 bits of
every result (darkVolume.c's count). `doScreenWrap` draws them into an RGBA16F accumulator and then
resolves `(Cd + acc) mod 256` against a copy of the target (`port/render/rd_replay.c`
(`doScreenWrap`); `port/shaders/raw_wrap.hlsl`).

**VU draws.** `rd__replay_vu` binds the draw's VU block, the mesh stream (the device arena or the ring), the
bones for a skinned draw, and sets the draw's flags: a chain clip plane, the free camera's
`ICO_VU_BEHIND_EYE`, `ICO_VU_EXTENDED` outside [out of the box](ARCHITECTURE.md#terms)
(`rd__vu_extended`), and `ICO_VU_TICKS` for a draw seen through a camera blended between two
[ticks](ARCHITECTURE.md#terms). A mesh with marked triangles binds its mark bytes after its stream
(`vu_mark`, [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-mesh-path)). A grid draw builds its index list
here and marks every triangle of a grid of two strips or more joined, as the draw's own mark
(`port/render/rd_replay_vu.c` (`rd__replay_vu`, `doVuDraw`); `port/render/rd_pipeline.c` (`rd__vu_extended`)). At start the renderer logs `rd: VU
index values: dense (marks in the stream)`.
A mesh, grid or particle command the pre-passes recorded draws through `vu_main_vs` (or
`vu_particle_main_vs`) and `vu_ps` over that record ([The vertex and triangle
pre-passes](PIPELINES.md#the-vertex-and-triangle-pre-passes)).
What the flags do is [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-region-rule). `ICO_RD_VU_OFFGRID=1`, a
developer switch, lets the [VU1](ARCHITECTURE.md#terms) programs output X and Y off the GS's 1/16
grid on a scaled target under the Enhanced preset; otherwise every preset keeps the GS's
quantisation (`port/render/rd_interp.c` (`rd__vu_off_grid`); `port/render/rd_replay.c`
(`rd__frame_group_ex`)).

**The MSAA epoch.** With an MSAA setting (never under the Original
[preset](ARCHITECTURE.md#terms)), SCENE has a
multisampled twin, SCENE_MS. The epoch opens at the first pass into SCENE (with SCENE's depth or
none) and every such pass draws into SCENE_MS; it closes at the first command that reads or writes
SCENE's single-sampled colour or depth any other way (a texture read of SCENE, a DATE snapshot, a
copy or exact blend, the wrap's snapshot, the shadow count's pass, the end of the replay). Closing
resolves once under the covered-majority rule ([MSAA.md](MSAA.md)); a shadow reset pending then
stays pending, and the volumes' pass takes it as its stencil clear, as without MSAA, so the shadow
count after the close reads SCENE's own single-sampled stencil by the same passes (the line
`shadow count: under MSAA Nx the count reads ...` names its form once). Integer-aligned sprites drawn inside it are moved half a texel so they cover whole pixels; the
fog runs per sample inside the epoch when the device has sample-rate shading
(`port/render/rd_replay.c`, "the MSAA epoch"; `msaaPass`, `msaaEnd`, `msaaSpriteExpand`). Otherwise
the fog reads the resolved depth ([DIVERGENCES.md](DIVERGENCES.md) VD-22); the log says once a run which one ran (`fog: in the MSAA
epoch per sample ...` or `... on the resolved picture ...`, `port/render/rd_replay.c` (`doFog`)). When no
way of reading the multisampled depth passed the fog's self-test, rd draws without MSAA and
`rd_aa_msaa_blocked` says so (`port/render/rd.h` (`rd_aa_msaa_blocked`)).

### The pass ring

The replay notes what it gives the GPU in a ring of the last 2048 entries, always on (a few stores
each): every render pass (`beginPass`), target copy (`doCopy`), snapshot copy (`takeSnap`), exact
blend (`doExact`), the upload batch before the lists, the VU vertex and triangle pre-passes
(`rd_replay_vu.c` `rd__vu_prepass`: `RD_PASSK_VU_PRE`, `RD_PASSK_VU_TRI`) and the present. An entry holds its sequence
number, the replay number (the performance record's, `g_rdPerf.replay`) and frame number, the keep
flag, the list being walked, the colour and depth target ids and sizes (a copy's source and
destination; the present's DISPLAY and window sizes), the storage-pass flag, the picture effect
(`perfPost`), the last pipeline asked for while it was open (`rd__get_pipeline`) with its shaders
and destination read, and an upload batch's bytes. A table of the last 8 replays keeps each one's
first and last sequence numbers, so a number the ring has moved past still names its replay and
frame. Each entry's sequence number is also the marker the replay gives `rhi_cmd_checkpoint` right
after the pass begins (or before the copy), and every replay sets `rhi_set_submit_tag` to its replay
and frame numbers, so the backend's [loss report](RHI.md#device-loss) and the ring name the same
passes. Once the device is lost the replay returns before recording anything (`replayFrame`), so
the ring ends at the last replay given to the GPU. A replay records about 430 entries, so the ring
holds the last three replays whole while each stays under about 680: the last one recorded and the
two before it, the oldest of them the submission whose wait failed (`RHI_FRAMES_IN_FLIGHT` back).
The window prints the replay table and then the entries of those three replays (from the oldest
entry held when the ring has moved past the start of the first; at most the ring's 2048 lines)
after a device loss, before the box ([PLATFORM.md](PLATFORM.md#diagnostics))
(`port/render/rd_passring.h`, header comment; `port/render/rd_passring.c`
(`rd_passring_print_start`, `rd_passring_print`); `port/render/rd_replay.c` (`replayFrame`,
`beginPass`, `doCopy`, `takeSnap`, `doExact`); `port/render/rd_pipeline.c` (`rd__get_pipeline`)).

### The loss dump

On a device loss the window writes, after the pass ring and before the message, the frames the
renderer still holds as one zip in the logs folder, `logs/loss-<date>-<time>.zip`: the last
closed frame (the one the lost replay drew, or the tick it was blended towards) as
`loss-<date>-<time>-lost.rddump`, then the frame before it as `loss-<date>-<time>-before.rddump`
when the frame ring still holds it, then a copy of the log. They are ordinary dumps written from the CPU side
(the commands, the payload, the image texels and VU streams the renderer keeps), with the textures
as F12 writes them, so the writing needs no device; the lost frame is the zip's first `.rddump`,
the one `rd_replay_tool` and the loss bisect take. The newest three loss zips are kept, the older
ones deleted, so a device lost at every start cannot fill a phone. The activity file then records
the loss with the zip's path (or that none was written) for the next start
([PLATFORM.md](PLATFORM.md#diagnostics)); the message names the file and asks for it with the log
(`port/render/rd_dump.c` (`rd_dump_retained`); `port/platform/dump_zip.c` (`ico_loss_dump_zip`,
`ico_loss_dump_prune`); `port/platform/window_host.c` (`loss_dump`, `device_lost_quit`)).

A GPU hang writes the same zip: when the watchdog ends a run whose main thread waits for work the
GPU never finishes ([PLATFORM.md](PLATFORM.md#diagnostics), "A GPU hang"), the window writes the pass
ring and the loss dump from the watchdog thread. The main thread, the only one that writes the
frame ring, the texture and mesh records and the pass ring, is blocked inside the driver, so they
stand still while they are written; the texture pack's loader thread fills only its own queue, and
the pipeline compiler threads run only inside a batch the main thread waits for. Nothing in the
dump needs the device, so none of it calls the hung one. The lost frame is then the last closed
frame, the one whose replay the wait belongs to or the one after it. The activity file notes it with
the hang's own line, which the next start counts as a loss but does not bisect: the bisect runs
before the watchdog and waits for the GPU with no limit, and a driver that hangs at that frame would
hang it too (`port/platform/window_host.c` (`window_gpu_hang`); `port/platform/diag_host.c`
(`ico_diag_note_device_loss`)). The zip goes with the report for the developers' replay.

### The loss bisect

The start after a loss that left a loss dump (of the same version) finds the pass that loses the
device on that device, before the game; a GPU hang's dump is not bisected (see the loss dump above). Only a start with the player's own blending and backend
bisects: a run the loss restart started (its step's fixed-function blend or Direct3D 12) logs that
the bisect was not run and leaves the zip in the logs folder
([PLATFORM.md](PLATFORM.md#diagnostics)). A renderer of its own, sized to the dump's frame, loads the
lost frame and replays it with nothing presented. Its pipelines are made first as the start-up
set's are: a replay with no draw recorded names the keys it finds no pipeline for without compiling
any (`rd__pipeline_gather`), and those keys compile on worker threads in batches
(`rd__pipeline_create_gathered`, `rhi_create_pipelines`) while the window's "Preparing graphics"
screen shows and takes the window's events (`port/platform/window_host.c` (`bisect_progress`)),
again while a replay names new keys, at most four times. A close asked meanwhile stops the bisect,
and a loss in these replays is reported as a loss with no draw recorded. Then the bisect replays the
frame with no draw recorded, which counts the frame's passes (their clears and layout changes still
run, and any pipeline still missing is created), then as passes 1..k for k = 1, 2, ..., waiting for the GPU after each replay
(`rhi_wait_idle`, which reports a loss like any other call). The first replay that loses the device
names the pass, logged in the pass ring's words:
`ico_pc: loss bisect: pass 12 of 61, list 3, pipeline 409 (vs 7 fs 5 dst 0), pass colour 4
(640x448), depth 5 (640x448), 14 draws`, with a second line naming the pass whose first command
the walk stopped at (that command's work before its pass began, a fog depth copy for example, ran
too). A frame that runs to its end says so (`the whole frame (61 passes) ran one more pass at a
time without losing the device`). The bisect's renderer is then shut down, lost or not, and the game's
own starts as at any start: the bisect is diagnostics only. A driver that cannot make a device
again in the process that lost one ends that start with a box asking to start the game again
(`ico_window_open` returns -3, `port/platform/main_host.c` (`open_window`)); the bisect is done by
then, so the next start is a normal one. On Android this holds for a graphics driver the player
chose too: after a bisect that lost the device it is not recorded as failed nor turned off, and the
next start loads it again. The activity file says the bisect is
running while it runs, so a start ended inside it does not bisect again, and the file is deleted
when it is done ([PLATFORM.md](PLATFORM.md#diagnostics)) (`port/render/rd_dump.c`
(`rd_loss_bisect`); `port/platform/window_host.c` (`window_loss_bisect`, `ico_window_open`);
`port/platform/diag_host.c` (`ico_diag_loss_bisect`)).

The replay behind it is the pass window (`rd__replay_set_window`): every replay numbers its passes
in the order they begin (the [vertex and triangle pre-passes](PIPELINES.md#the-vertex-and-triangle-pre-passes) first when they run, then
render passes, copies, snapshots and exact blends: the pass ring's entries other than the uploads
and the present). With a window [first, last] the draws and the target and snapshot copies of the
passes outside it are not recorded (the pre-pass's record copies go with its draws, so the slot's
record buffer keeps what an earlier replay of the frame wrote; the uploads and the fog's depth copy
are not windowed), the passes themselves still begin, and the walk stops after the
command that began pass last + 1, which draws nothing. So a loss in the vertex pre-pass is pass 1
and one in the triangle pre-pass pass 2, not a loss with no draw recorded (`port/render/rd_replay_vu.c` (`rd__vu_prepass`,
`rd__replay_pass_push`)). [1, k] is the frame up to pass k, [k, k]
pass k alone on the targets as an earlier replay left them. `rd_replay_tool --loss-bisect` runs the
bisect on a dump, `--passes A[-B]` replays a window (`port/render/rd_replay.c` (`passPush`,
`drawCmd`, `copyCmd`, `replayFrame`); `port/render/rd_window.c`; `port/render/rd_internal.h`
(`rd__replay_set_window`, `g_rdWin`)).

**The second level: the draw.** When the first level ends on a pass that drew, the same start
narrows it to the draw, one step at a time, each step one replay of the lost frame on a renderer of
its own (`rd_init`, the step, `rd_shutdown`, lost or not: the reporter's log of issue 95 shows a
fresh device works after a loss on that driver). A step replays passes 1..P - 1 as the first level
ran them and, of pass P, the draws `drawFirst..drawLast` that pass the step's filter, numbered as
the pass asks for them (every draw call, the exact blend's second pass too: the first level's
count); then it waits for the GPU. The draw window (`rd__replay_set_window_draws`; the filters
`MASK_MARKS`, every VU draw with its triangles' marks left out, `vu_mark` zero; `VS_ONLY(vs)`;
`CLIP_ONLY(clip)`; `SKINNED_ONLY`; `TEX_ONLY(id)`) costs a normal replay nothing: the draw
counters test `s_win.first` as before. While it is set, `vuDraw` notes each VU draw of pass P (list,
command, mesh name, program, vertex and pixel shader, clip mode, batch, index range, the values'
span and marked triangles, the texture's id, size, format and mip levels, the pipeline key's words),
and the window keeps the note of the last draw it recorded (`RdDrawNote`, `rd_draw_note_text`).
The note's vertex shader is the program's own entry, also for a draw through a main shader and
for the pre-passes' draws (passes 1 and 2, noted by `rd__vu_prepass` with their vertex range), so
`VS_ONLY` separates the programs; the key's words name the pipeline drawn with. `MASK_MARKS`
masks the record stages' marks too.
The planner (`port/render/rd_bisect_plan.c`, pure) says which step comes next from the steps so far:
first the whole pass with the marks masked (the hypothesis test: clean means the marks did it),
then the prefix search, halving between the longest clean prefix (0) and the shortest lost one (N,
the first level's), 11 steps for 1,312 draws, then draw D alone, then D's classes over draws 1..D
(its vertex shader, its clip mode, the skinned draws, its texture), and, when no step lost the
whole pass again, that pass once more; at most 16 steps. Prefix verdicts that contradict each other
(a clean prefix as long as a lost one: something other than the draws loses the device) end it and
say so. Each step is logged (`ico_pc: loss bisect: step 4 of 16: pass 1, draws 1..656 of 1312:
lost in 0.6 s`), and one final line names the draw, its note and the verdict (`faults alone`, `does
not fault alone`, `not finished`), and how the masked step went (`clean with the marks masked`).
The steps' verdicts are written to `logs/loss-bisect.txt` before the next step, so a start ended
inside a step continues at the next start (that step counted as lost, `killed`); 6 starts, 90 s
of steps a start and 3 steps that lose the device a start at most (Windows restarts the system
after 5 graphics resets in 60 s; [PLATFORM.md](PLATFORM.md#diagnostics)). Between the steps the start-up
screen says "Checking the graphics problem (step 4 of 16)", drawn on the step's fresh renderer
before its replay, never on a lost device (`port/render/rd_dump.c` (`rd_loss_bisect_step`);
`port/render/rd_window.c` (`rd__win_take`, `winFilter`, `rd__win_vu_note`); `port/platform/window_host.c`
(`window_bisect_step`); `port/platform/diag_host.c` (`ico_diag_loss_bisect2`)).
`rd_replay_tool --loss-bisect --passes P --draws A-B [--mask-marks]` runs one step on a dump;
without `--loss-bisect`, `--draws` and `--mask-marks` shape a replay's picture.

**The pass split.** `ICO_RD_PASS_SPLIT=N` (`[dev] pass_split = N`; off by default) ends a render
pass and begins it again, both targets loaded, after every N VU draws, as the exact blend begins a
pass again (the scissor taken back), so a GPU that runs a pass's draws as one job gets jobs of N:
the fallback if a device still faults on a pass of many VU draws. It is refused, logged once, in a
multisampled pass, a storage pass, a pass the exact blend's attachment read made readable
(`port/render/rd_replay.c` (`rd__replay_exact_readable`)), and inside a draw that takes DATE
through the stencil bit (`Replay.dateStNow`: the bit lives in its pass only). A pass readable only
for DATE's stencil bit or the fog's local read (every SCENE pass where the profile has them) is split:
it is begun again readable, and the next DATE draw takes the bit again in it
(`port/render/rd_replay_tiler.c` (`rd__replay_date_take`)). Its passes are not
passes of the loss bisect's window. The picture is the same byte for byte
(`rd_replay_tool_pass_split`) (`port/render/rd_replay_vu.c` (`passSplit`); `port/render/rd_core.c`
(`rd_init`)).

### The stream bounds audit

`rd_stream_audit_test` walks every VU draw of the dumps in `tools/native_frame_dumps.txt` and the
boot corpus's title frame and computes on the CPU every vertex stream index the VU vertex shaders
can read for it, the way the shaders compute it (`vu_vertex_qw`, `vu_batch_qw`): the triangle's
three vertices (the clip plane's distance reads one of them), a grid's batch colour, a particle's
header and two qwords. The shipped shaders scan no strip: the card test's verdicts are bits the CPU
puts in VuBoneCB (`vu_strip_card`, its index clamped to qword 239). The audit also checks a strip
test's scans on the stream's own strip flags and its walk over the strip, taken to run for every
kick (`stripBounds`): reads the shipped shaders do not make, a superset that keeps the audit
conservative. Each index is checked against the draw's own stream,
the qwords the replay uploads for it (`doVuDraw`); a kick below 2 and a draw's index range past its
mesh's list fail too. VuCB's tick cameras and VU memory and VuBoneCB are fixed-size blocks the
indices cannot leave (static asserts; `vu_bone_mat` and `vu_strip_card` clamp). Reported, not
failed: the skinned draws' weight addresses (lowest and highest bone) and the draws whose bones fill
VuBoneCB, which leaves no room for the card bits ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)). Each triangle's
mark read (`vu_kick_marks`, one qword of the mark bytes after the vertices) is checked the same way.
The record stages read a triangle record for every kick from a draw's lowest to its highest, the
kicks its list does not draw included: the audit reads those kicks' marks and vertices too, and
the record's own texels are in range by construction (a command's slots are counted before the
pre-passes draw: `rd_replay_vu.c` (`vuPrepassRun`); [The vertex and triangle
pre-passes](PIPELINES.md#the-vertex-and-triangle-pre-passes)).
The audit also reports, per frame, the index span (the lowest and highest value of every indexed VU
draw, the widest, the draws past 2^24 - 1 by list, the values carrying the old mark bits), and fails
a span past 2^24 - 1 (Vulkan's least `maxDrawIndexedIndexValue`) and a kick listed twice in a draw
with different marks; the CPU mirror (the programs' references in `port/render/vu1_ref` run on each
prelit, lit and skinned batch at the draw's own VU memory and bones: clip w not finite or within
1e-6 of 0, GS X/Y past 16 bits, the scissor fans, a NaN clip w or bone failed, a NaN in VU memory
counted); and the textures list 0 binds (id, size, format, mip levels, whether the texels are
there). On the reporter's lost frame of issue 95 the widest span is 14,314 (the title scenery,
`st26a_p2`). The game measures the same at run time on one replay in 64 (`rd_core.c` `rd__span_draw`,
from each indexed VU draw's CPU index list): the first measured frame logs `rd: VU index span: frame
N (the first measured, one replay in 64 from here): the widest W over D indexed VU draws, K past
2^24 - 1; the device takes values up to M` (expected about 14,314 and 0), a later frame with a draw
past 2^24 - 1 logs its own line (the first 4), and a device loss logs `window: at the loss: VU
index span: the widest W (frame N) over F measured frames ..., K indexed VU draws past 2^24 - 1 of
D in G frames; ...` (`rd_index_span_text`), so a regression of the dense indices shows in a
player's log. `ICO_AUDIT_SPANS=1` lists each draw past the bound. A stream one vertex short fails it, in a
synthetic draw and on the first real mesh draw of the dumps (`port/render/test/rd_stream_audit_test.c`,
header comment). On the 27 listed dumps present on the maintainer's machine and the title frame
(2,964 VU draws, 115 of them the title's; 2026-10-15) no read leaves its stream.

## Pipelines

The pipeline key, the blend paths, the start-up set, the fallback entries and the vertex and triangle pre-passes have their own page: [PIPELINES.md](PIPELINES.md).

## Destination reads and blend paths

The GS blends in integers with an arithmetic shift, no clamp of C, then COLCLAMP, PABE and FBA;
the GPU's fixed-function blender computes in float and rounds to nearest, one level above the GS
on about half the channels of a LERP ([DIVERGENCES.md](DIVERGENCES.md) VD-7). The destination
read computes the GS result in the fragment shader from the pixel's own destination, read in
rasterization order, with blending off (`gs_dst_channel`, `port/shaders/gs_math.hlsli`)
(`port/render/rd_dst_read.c`). Two realisations (`port/rhi/rhi.h` (`RhiDstRead`);
`port/render/rd_dst_read.c`):

- **attachment**: the colour attachment read as an input attachment in rasterization order
  (Vulkan rasterization-order attachment access over dynamic rendering local read). Depth,
  stencil, discard and write mask stay fixed-function. Every pass on a readable RGBA8 target is
  begun readable.
- **storage**: the target as a rasterizer-ordered view (D3D12 ROV; Vulkan storage image under
  fragment shader interlock). The target is then no colour attachment, so a blended draw runs in a
  storage pass of its own with the depth attached. A draw that writes Z and can discard is split
  into colour without Z write then a depth-only pass when its Z test is ALWAYS; under
  GEQUAL/GREATER it stays on the fixed-function blend (logged once).

**The exact blend is an opt-in.** The fixed-function blend is the default on every device and
backend. Only `[dev] blend` / `ICO_RD_BLEND` ([CONFIG.md](CONFIG.md)) set to `attachment` or
`storage` (also `interlock`, `rov`) opts into a destination read, and only where the device has
it; `exact` takes the device's best (the attachment read, else the storage read) and logs
`rd: blend: [dev] blend = exact: the exact blend is an opt-in; attachment tried`; `fixed` (also
`off`, `0`) pins the default. The ask is read before the shader modules exist
(`port/render/rd_dst_read.c` (`rd__dst_asked_env`)), so `rd__gpu_init` makes the group-3 layout
and the `_dstrov` / `_dstatt` twins of the asked mode alone (`port/render/rd_replay.c`
(`rd__gpu_init`)), `g_rd.dstModes` holds at most the fixed-function blend and that mode
(`madeModes`), no RGBA8 target is made with `RHI_TEX_STORAGE` or `RHI_TEX_INPUT_ATTACHMENT` for the
blend unless asked (`rd__dst_target_usage`, `RdTargetRec.dstUsage` bits `RD_DST_USAGE_STORAGE` and
`RD_DST_USAGE_ATTACHMENT`; `RD_DST_USAGE_LOCAL_DATE` is DATE's local read, not the blend's), and
the start-up pipeline set has no destination-read key: `rd__enumerate_reachable` takes its
`enumerateAll` path when the mode is the fixed-function blend and the dual-source blend is in use
(`port/render/rd_pipeline.c`), and `rd__enumerate_dst` returns at once for it. The start line is
`rd: blend: fixed-function (the default; [dev] blend = attachment|storage opts into an exact
mode)`, `rd: blend: fixed-function ([dev] blend = fixed)` or `rd: blend: exact (attachment read)
([dev] blend)`; a word that is none of these or a mode the device lacks logs `rd: blend:
ICO_RD_BLEND=<v> is not fixed, attachment or storage; fixed-function` or `... is not on this
device; fixed-function` and keeps the default (`rd__dst_init`).

Every place that branches on the mode stays and is inert at the fixed-function blend: the pass's
mode (`dstModeFor`) is the fixed-function blend, `expandPassesMode` and `rd__expand_dst` copy the
passes, `dstPassMode` begins no storage pass (the `window: per replay` line's destination-read
draws, storage passes and pass switches read 0), `rd__pass_wants_readable` is false, the MSAA
epoch's exclusion and the fog drawn in it change nothing (`port/render/rd_replay.c`), and the
restart steps that take the fixed-function blend after a crash or a loss are skipped because the
run already drew with it (`port/platform/startup_retry.h`). A nonzero exact-blend field in
`window: per replay` means the player opted in.

What the exact blend draws when opted into: the screen prims, the VU meshes and the fog sprite
whose fixed-function blend is not the GS's (the LERPs, mode 3, the Ad forms, mode 11, PABE on the
premultiplied forms, COLCLAMP 0) on RGBA8 targets. What stays on the fixed-function blend: the
premultiplied forms, the MSAA epoch's passes, targets other than RGBA8, the port's presentation
overlay. Of the premultiplied forms the adds (modes 0 and 5) are exact on the blender (the shader
writes the GS term `(Cs F) >> 7` and the blender adds two whole levels); the subtracts (modes 1
and 6) are one level above the GS wherever `Cs F` is not a multiple of 128, because the GS's term
is `(0 - Cs) F >> 7`, an arithmetic shift that rounds the negative product down, while the blender
subtracts the truncated `(Cs F) >> 7` (`port/shaders/common.hlsli` (`gs_dual_out`);
`port/shaders/gs_math.hlsli` (`gs_blend_ch`); ctest `rd_pixel_uber` asserts it:
`testUberSubtractOracle` in `port/render/test/rd_pixel_uber_test.inc` holds every pixel of the
subtracts through the colour read to `clamp(Cd + (((0 - Cs) F) >> 7), 0, 255)` and, on the
blender, to the truncated form, one level above the GS exactly where `Cs F` is not a multiple of
128). The staticBlur sprites and `blend_int` keep their
own exact paths (`port/render/rd_dst_read.c`; `port/render/rd_replay.c` (`dstPassMode`,
`rd__pass_wants_readable`)).

Why the opt-in: the storage read ends and begins a pass at every switch between a shader-blended
and a hardware-blended draw (`dstPassMode`): 887 passes a replay on an RTX 3090 against 53 to 75
with the fixed-function blend, 432 against 40 on a GTX 1650, and on a contributor's M1 (MoltenVK,
interlock) 54 ms and 750 passes a replay against 14 ms and 49 with `ICO_RD_BLEND=fixed` (issue
102), for the last level of rounding on the LERP channels. Between the attachment read and the
fixed-function blend (`rd_replay_tool --pass-dump` of a corpus frame at Original 4:3 1x, each
way), about 95 % of SCENE's pixels differ, by one level on most, two on about one in eight, at
most 5 or 6 where several blend steps add up; WORK0 to WORK3 and the shadow levels are identical.

**The attachment read on Qualcomm's driver.** The attachment read is not offered on Qualcomm's own
Adreno driver (vendor 0x5143 with `VK_DRIVER_ID_QUALCOMM_PROPRIETARY`, or with the id unknown;
Mesa's Turnip on the same GPUs reports its own id and keeps it). On the Odin 3 (Adreno 830, driver
512.800.72, issue 89) it returned wrong destination colours: white boxes around the leaves' clear
texels, the title washed out, the menu panels white on white; the same title frame replays
through the same path on lavapipe with no validation error and within one level of
`ICO_RD_BLEND=fixed`, so the driver is wrong, not the port's use. The device line says so,
`rhi_vk: destination read: interlock no, rasterization-order attachment read off on this driver
(Qualcomm driver 512.800.72: wrong results on Adreno; ICO_VK_ATTACHMENT_READ=1 tries it); local
read yes`, so `[dev] blend = attachment` is refused there (`is not on this device`).
`ICO_VK_ATTACHMENT_READ=1` (`[dev] attachment_read = true`) offers the read anyway;
`ICO_VK_FAKE_ADRENO=1` takes the branch on any device, for tests (`port/rhi/vk/vk_device.c`;
`port/rhi/rhi_driver_version.h` (`rhi_driver_is_qualcomm_proprietary`); `port/rhi/rhi.h`
(`RhiLimits.rasterOrderOff`)). On MoltenVK (`RhiLimits.moltenvk`, `; MoltenVK` on the `rhi_vk:`
device line) the default is the same fixed-function blend as everywhere; an opt-in there behaves
as on any other device. `ICO_VK_FAKE_LIMITS=moltenvk` reports MoltenVK, with no attachment read, on
any device (`rd_blend_selftest_moltenvk_storage`).

**The blend self-test.** A driver whose read or dual-source blend returns the wrong colour draws a
wrong picture with no error anywhere, so `rd_blend_selftest` checks the blend in force on the
device at start, on every device, as the fog's self-test checks its depth reads ([The fog path and
its self-test](RENDER_POST.md#the-fog-path-and-its-self-test)). A frame of its own (never recorded or
presented) is drawn on SCENE at its real size and scale, MSAA held off, in a 256 x 96 GS pixel
block in its middle: eight cells of PCSX2's forest pixels under the forest's tint sprite
(`rd_pixel_test.c` `kTint`), a textured LERP quad whose clear texel must leave the colour behind it
(the leaf case), two overlapping LERP draws (the second must blend over the first's result), one
LERP command of two overlapping sprites (the second must blend over the first's result inside the
one draw: the read's rasterization order, which the draw merge and GS state as shader data rely
on) and a glyph through the text sheet's shader, then an opaque cell read at an even and an odd
texel column (a dual-source equation kept with blending off, [RHI.md](RHI.md#dynamic-state)). 22
samples are read back (one readback; its pipelines are keys of the start-up set). The fixed-function
blend runs first, the reference, within 2 levels of the GS arithmetic; if it fails, the two-pass
fallback (`rd_set_no_dual`) is tried and kept when it passes. Then, only when `[dev] blend` opted
into an exact mode the device has (`rd__dst_asked`), that mode runs, within 1 level of the GS
arithmetic and of the reference's glyph, and is kept whatever its result. The window runs it
before the start-up pipeline set and again whenever SCENE is made at a new size. One line names the
blend in force and every result:

```
rd: blend: fixed-function (self-test: fixed-function ok 1; exact modes not asked for: [dev] blend = attachment|storage tries one) on SCENE 640x448 at 1920x1344 texels in T ms
rd: blend: exact (attachment read) (self-test: attachment ok 1, fixed-function ok 1; kept by [dev] blend) on SCENE 640x448 at 1920x1344 texels in T ms
rd: blend: fixed-function (self-test: fixed-function fail N -> two-pass ok 1; exact modes not asked for: [dev] blend = attachment|storage tries one) on SCENE ...
```

and the `ico_pc: summary:` line carries `blend fixed-function (self-test: fixed-function ok 1)`.
A failing entry's worst sample, and a passing one's when it is not exact, gets a line of its own,
`rd: blend: self-test: attachment fail 241 at tint cell 0 (texel 625,625): 255,255,255,4 want
21,28,14,4` (read white by `ICO_RD_BLEND_SABOTAGE=attachment`). `ICO_RD_BLEND_SABOTAGE=`
`attachment,storage,fixed,twopass` makes those entries' samples read white, for the fallback's
tests; a run that took the two-pass fallback starts from the dual-source blend again at the next
SCENE size, so each line is whole (`port/render/rd_blend_selftest.c`; `port/render/rd.h`
(`rd_blend_selftest`); `port/platform/window_host.c`).

### GS state as shader data

Where the device orders the attachment read (Vulkan with `RhiLimits.rasterOrderAttachment`: Mali
with the ARM extension, Adreno on Turnip, lavapipe; not PowerVR, not Qualcomm's own driver, not
MoltenVK), `[dev] uber = auto` (the default, on a tile-based GPU; `on` anywhere the read is) asks
for the attachment read itself and draws every pass it can through it: the blend, the write mask
and DATE become shader data and leave the pipeline key, so the start-up set is several times
smaller ([PIPELINES.md](PIPELINES.md#gs-state-as-data) has the fields, the set, the device rule,
the log lines and the tests; `port/render/rd_uber.c`). The passes: every pass on an RGBA8 target
made for the read begins readable and takes a pending clear as its load op, a COLOR local-read
barrier after it (`beginPass`); the premultiplied forms and the passes without GS blending go
through the read too (`RdDrawPass.csOnly`: the register `0x00`), DATE reads the attachment's alpha
(**DATE through the colour read**, above), the VU draw merge and the fused scissor pair take the
read's draws (`port/render/rd_replay_vu.c` (`vuMergeDst`, `vuFuseVerdict`)). The picture is the
attachment read's, the GS's integers, with the premultiplied subtracts the GS's floor and DATE per
primitive; the MSAA epoch, other formats and the presentation overlay keep the fixed-function
blend, as under the exact blend. The blend line is `rd: blend: exact (attachment read) (GS state as
shader data)` and the self-test's `...; GS state as shader data)`; under `auto` a failing
attachment entry turns it off (`rd__uber_off`: `; GS state as shader data off: the colour read
failed`, the fixed-function blend); `[dev] uber = on` keeps it as `[dev] blend` keeps an asked
mode. The fades fold is off under it.

### The storage pass merge

Under the storage read a destination-read draw needs a pass without the colour attachment, and any
other draw a pass with it, so a frame that interleaves them ends and begins a pass (with its
barrier) at every change: on an RTX 3090 the title ran 887 passes a replay against 53 to 75 on the
attachment and fixed paths, and the GPU time of the logs held follows the pass count (issue 103).
The merge (`g_rd.passMerge`, off by default) keeps an open storage pass open: a draw that
`rd__expand_dst` would leave on the fixed-function blend is drawn in it through the
destination-read shader instead when the shader stores the same bytes (`rd__dst_merge_ok`):

- no GS blending: the shader's ALPHA register is then `0x00`, `((Cs - Cs) As >> 7) + Cs`, which is
  Cs (`RdDrawPass.csOnly`, `fillDrawCB`);
- the premultiplied add, modes 0 and 5 (exact on the blender, above);
- an RGBA8 target, an entry with the destination-read twins, a mask writing RGB, no stencil,
  single-sampled;
- not a draw that writes Z and can discard: the storage pass tests Z before the shader, so such a
  draw keeps the colour/depth-only split or the fixed-function blend as before.

The premultiplied subtracts stay on the fixed-function blend, which rounds them differently
(above): through the shader they would be the GS's bytes, not the current picture's. A draw that
does not qualify still begins a pass with the colour attached, and the next destination-read draw
a storage pass again. The merge changes only the storage mode's passes, so the attachment read and
the fixed-function blend draw as before. `ICO_RD_PASS_MERGE=1` (`[dev] storage_pass_merge = true`)
turns it on and logs `rd: storage pass merge: on (ICO_RD_PASS_MERGE=1)`; the start-up pipeline
set has the merged keys while it is on (`rd__enumerate_dst`). It is off by default until the A/B
on an RTX 3090 and the shot list have been run with it: the only logs with it on (an RTX 3090,
interlock) count 645 to 1189 pass switches a replay (`window: per replay` lines).
lavapipe has no fragment shader interlock, so the tests that compare the merge's picture with the unmerged one byte for byte
(`rd_replay_tool_pass_merge`, `rd_perf`) skip that part there and run on a device with the
storage read; the release check is the shot list on an interlock card with and without the key
(`port/render/rd_dst_read.c` (`rd__dst_merge_ok`, `rd__dst_merge`); `port/render/rd_replay.c`
(`expandPasses`, `dstPassMode`)).

## The device profile

One renderer with device-class profiles: after `rd__gpu_init`, `rd__profile_init` decides
`g_rd.profile` (`RdProfile`, `port/render/rd_internal.h`) once from `RhiLimits` (`tiler`,
`localRead`, `vendorId`, `moltenvk`, `uintView`, `driverText`) and the backend, and every
vendor-dependent choice of the renderer reads one of its flags; no other file of the renderer
branches on a vendor (`port/render/rd_profile.c`). The classes: D3D12 (the backend), Apple /
MoltenVK (`RhiLimits.moltenvk`), Adreno on Qualcomm's driver and Adreno on Turnip (vendor 0x5143;
Turnip names itself in the driver text), Mali (0x13B5), PowerVR (0x1010), any other tile-based GPU
(`RhiLimits.tiler`: Samsung, Broadcom, every Android GPU) on the PowerVR row, and the desktop
immediate-mode GPUs (NVIDIA, AMD, Intel, lavapipe) on Vulkan. OpenGL and Direct3D 11:
[BACKENDS.md](BACKENDS.md#the-device-profile-on-opengl-and-direct3d-11).

| flag | desktop Vulkan | D3D12 | Adreno (Qualcomm's driver) | Adreno (Turnip) | Mali | PowerVR, other tile-based | Apple / MoltenVK |
| --- | --- | --- | --- | --- | --- | --- | --- |
| blend | fixed-function; `[dev] blend` opts into an exact mode | fixed-function; `[dev] blend = storage` opts into the ROV | fixed-function | fixed-function | fixed-function | fixed-function | fixed-function |
| `fogLocal` | on (probe) | off | on where local read is offered | same | same | same | same |
| `dateStencil` | on (probe) | off | on where local read is offered | same | same | same | same |
| `storePlan` | on | on | on | on | on | on | on |
| `blurFold` | on | on | on | on | off (the G52 is bound by its ALU) | on | on |
| robust access | off | its own bounds rules | off | off | off | off | off |
| `localRead` | as offered | none | as offered | as offered | as offered | as offered | as offered |
| `uintViewWork`, `uintViewScene` | on where the backend has the view | off (`RhiLimits.uintView` false on D3D12) | off | off | off | off | off |
| `carefulSet` | off | off | off | off | off | off | off |
| the record path (the triangle pre-pass, `[dev] vu_prepass`) | on | on | off (the pictures between two ticks shook on a PowerVR phone; `[dev] vu_prepass = true` turns it on) | off | off | off | off |
| dynamic state (`[dev] dynamic_state`) | on where offered | none | off (the same phone; `[dev] dynamic_state = true` turns it on) | off | off | off | off |
| `alphaPlan` (`[dev] alpha_plan`) | on | on | on | on | on | on | on |
| `fxPlan` (`[dev] fx_plan`) | on | on | on | on | on | on | on |
| `fxDstLocal` (`[dev] fx_dst_local`) | on (probe) | off | on where local read is offered | same | same | same | same |
| `vuPlanesFrac` (`[dev] vu_planes`) | on | on | on | on | on | on | on |
| `crackFill` (`[dev] crack_fill`) | on | on | on | on | on | on | on |
| `shadowOne` (`[dev] shadow_one`) | on | off | on | on | on | on | on |
| `shadowBspline` (`[dev] shadow_bspline`) | off | off | off | off | off | off | off |

The blend row is `rd_dst_read.c`'s ([Destination reads and blend
paths](#destination-reads-and-blend-paths)), robust access the Vulkan backend's
(`port/rhi/vk/vk_device.c`; [RHI.md](RHI.md#device-loss)); the profile reports both. The other
flags are decisions: `fogLocal` offers the fog's `local` path, tried first by its self-test,
`dateStencil` DATE through the stencil bit, decided by its self-test, `blurFold` the folded
staticBlur pairs ([The replay](#the-replay)); `storePlan` [the store plan](WORK_BUFFERS.md#the-store-plan). `alphaPlan` offers the alpha/blend
planner ([The alpha plan](#pipelines)), `fxPlan` the frame-size post sprites drawn without their snapshot
([Post passes](RENDER_POST.md#post-passes)), `fxDstLocal` the post sprites that read their destination by the local read
inside the open pass (it needs `fx_local_ps` and is decided
by its self-test), and `vuPlanesFrac` the 3D pixel shaders without the attribute planes on a target at a
fractional scale ([Screen planes](#screen-planes)). A target is made with
`RHI_TEX_UINT_VIEW` by class when `uintViewWork` (the work buffers, `rd__work_class`) or
`uintViewScene` (the scene-class targets) is on, a scene-class one with
`RHI_TEX_INPUT_ATTACHMENT` and `RD_DST_USAGE_LOCAL_DATE` when `dateStencil` is on and the DATE
stencil shader is in the table (`rd__dst_target_usage`, narrowed to the scene class), and a
scene-class depth with `RHI_TEX_INPUT_ATTACHMENT` and `RD_DST_USAGE_LOCAL_DEPTH` when `fogLocal`
is on and `fog_lut_local_ps` is in the table (`port/render/rd_core.c` (`rd__target_create_gpu`)).
`uintView*` are off wherever `RhiLimits.uintView` is false, and `fogLocal` and `dateStencil`
wherever `RhiLimits.localRead` is. The local read is enabled through
`VK_KHR_dynamic_rendering_local_read`; the Vulkan 1.4 core feature is queried but the instance's
version is capped at 1.3, so a 1.4 driver that lists only the core feature has no local read
(`port/rhi/vk/vk_device.c`).

Each flag has a `[dev]` key, exported by the host ([CONFIG.md](CONFIG.md)): `fog_path = "local"`
(`ICO_RD_FOG_PATH=local`: `fogLocal` on; `inplace`, `copy` or `buffer`: off), `date_path =
"stencil"|"snapshot"` (`ICO_RD_DATE_PATH`), `store_plan` (`ICO_RD_STORE_PLAN=1|0`), `blur_fold`
(`ICO_RD_BLUR_FOLD=1|0`), `uint_view` (`ICO_RD_UINT_VIEW=1|work|0`: both views, the work buffers'
only, none), `alpha_plan` (`ICO_RD_ALPHA_PLAN=1|0`), `fx_plan` (`ICO_RD_FX_PLAN=1|0`), `fx_dst_local`
(`ICO_RD_FX_DST_LOCAL=1|0`), `vu_planes` (`ICO_RD_VU_PLANES=1|0`: 1 is `vuPlanesFrac` on, the planes dropped at a
fractional scale), `gpu_robust` (`ICO_GPU_ROBUST`). A flag a key set is marked `([dev])`; a key that
asks for what the device lacks leaves the flag off and logs `rd: profile: [dev] fog_path = local
(no local read): not on this device, the flag stays off` (`fx_dst_local` asked for without local read logs the same
line with `[dev] fx_dst_local (no local read)`). A self-test that finds its mechanism
wrong turns the flag off (`rd__profile_self_test_off`): the flag is marked `(self-test)` and the
line `rd: profile: <flag> off (self-test)` is logged. The start line, once a run (abridged):

```
rd: profile: tile-based GPU (Imagination, Vulkan): blend fixed-function; fog local on; DATE stencil; depth stores planned; blur fold on; robust access off (the default; gpu_robust = true turns it on); local read yes; uint views off; alpha plan on; fx plan on; fx local read on; VU planes whole scales only; pipelines the start-up set
```

On desktop Vulkan with local read, "(probe)" marks the three local-read flags where the start-up cost probe
found them slower, and both lines end with its words: [WORK_BUFFERS.md](WORK_BUFFERS.md#the-desktops-local-read-and-its-probe).

and the `ico_pc: summary:` line (abridged) carries the flags as in force after the blend's text
(`rd__profile_text`; `port/render/rd_core.c` (`rd_summary_text`)): `profile tile-based GPU
(Imagination, Vulkan): fog local on, DATE stencil, depth stores planned, blur fold on, local read
yes, uint views off, alpha plan on, fx plan on, fx local on, VU planes whole scales only`. The four planner flags read
`on`/`off` and `whole scales only`/`every scale` (`vuPlanesFrac` off); a flag a key or a self-test changed is marked as the others are. The Vulkan version is not in the class name (`RhiLimits` does not carry
it); the `rhi_vk:` device line has it.

## Post passes

The passes over a replayed frame and the effects that read its targets are in
[RENDER_POST.md](RENDER_POST.md): [Post passes](RENDER_POST.md#post-passes),
[Above 1x](RENDER_POST.md#above-1x), [Shadows](RENDER_POST.md#shadows),
[Water and render-to-texture blocks](RENDER_POST.md#water-and-render-to-texture-blocks),
[The fog path and its self-test](RENDER_POST.md#the-fog-path-and-its-self-test) and
[Crack filling above 1x](RENDER_POST.md#crack-filling-above-1x).

## Content tables

**Hidden stage faces.** A few stage faces relied on the PS2's 4:3 picture: closing planes that seal
a model where the 4:3 camera never looks. They are listed in `rd__content_hide` by the stream bits
of their corners; `markContentHidden` marks a triangle whose three vertices are black and corners of
one entry with `ICO_VU_MARK_HIDE`, drawn nowhere. Black alone does not identify such a face, and a
model pack's replacement never matches. An entry is listed only when 4:3 dumps with it hidden equal
those without, byte for byte, in Original 1x and Enhanced 2x (`tools/content_table_check.py decide`)
(`port/render/rd_content_hide.c`; `port/render/rd_mesh.c` (`markContentHidden`)). To add an entry:
find the mesh with `rd_replay_tool --list` and `--mesh` on a dump that shows the face, list its
black corners in `rd__content_hide`, and run `tools/content_table_check.py decide`.
`ICO_RD_CONTENT_HIDE` overrides the table's verbs for the check: comma-separated `off`, `hide` or
`table` for every entry, or `N=verb` for entry N; a bad value logs
`rd: ICO_RD_CONTENT_HIDE="..." not understood: the table as listed` and keeps the table
(`port/render/rd_mesh.c` (`rd__content_hide_set`, `rd__content_hide_verb`)).

**Sheets continued to the frame's edges.** A few stage models are sheets of tiles held in front of
the camera to cover the PS2's 4:3 picture, and stop short of the sides of a wider frame: the deja
vision's grain veil (mesh `sandstorm`, four quadrant sheets of four 128 x 112 tiles, each drawn in
two layers). They are listed in `rd__content_extend` by their vertex count, layout and the xxh3 of
their stream, so a model pack's replacement never matches. When a listed mesh is made,
`extendSheet` appends whole tiles in its one batch: the outer column repeated `cols` times outward
across, then the outer row `rows` times outward down, each tile's UV continued from the tile it
repeats so the texture runs on without a seam under REPEAT (`rd__sheet_tiles`). The mesh's own
vertices and triangles come first and are unchanged; the tiles are drawn with the draw's state,
matrices and UV offset, and take the region test as the sheet does. Out of the box (the Original
preset at 4:3 and 1x, `vuExtended`) a draw of a listed sheet stops at the game's own triangles
(`rd__vu_sheet_end`, `RdMeshRec.tiledIndexFrom`), so the appended tiles are never drawn there,
wherever the sheet is placed, and the picture is the PS2's; in every other state they are drawn,
clipped by the GPU at the frame. This is decided per draw, so an Options change needs no new mesh
(`port/render/rd_replay_vu.c` (`doVuDraw`); `port/render/rd_interp.c` (`scratchFor`) carries the
counts to a blended copy). A dump stores a listed sheet as the game made it, its own vertices,
triangles and batch (`port/render/rd_dump.c` (`rd__dump_frame`)), so the replay lays the tiles
again under `ICO_RD_CONTENT_EXTEND` as it is set, and `off` removes them. A morph
(`rd_update_vu_mesh`) writes the sheet's own vertices and lays the tiles again (`retileSheet`).
The veil's sheets reach GS X 2048 -+ 702 and Y 2048 -+ 717; three tiles across (351 GS pixels
each) and one down reach 2048 -+ 1755 and -+ 1075, past the widest frame (60:9, -+ 1280) and still
inside the GS window. An entry is listed only when the appended tiles lie wholly beside the 4:3
picture: `tools/content_table_check.py decide` replays every dump that draws the mesh with the
entries off and on and requires the 4:3 picture and every target's 4:3 band byte-identical
(Original 1x and Enhanced 2x), and on a dump wider than 4:3 the sides changed by the sheet as its
middle is (`port/render/rd_content_extend.c`; `port/render/rd_mesh.c` (`extendSheet`,
`rd__sheet_tiles`, `retileSheet`)). `ICO_RD_CONTENT_EXTEND` overrides the verbs as
`ICO_RD_CONTENT_HIDE` does, with `off`, `tiles` or `table` (`port/render/rd_mesh.c`
(`rd__content_extend_set`, `rd__content_extend_verb`)).

**Natural chains.** Two stage animations let a chain down by scaling a Null's Y, which squashes
the links. With the option on the Null takes no scale along the chain, its children move along
it instead (`rd_chain_null`), and each link gets a clip plane in its own space where the disc's
squashed top link begins (`rd_chain_link`); the VU shaders discard the pixels past it. Off, the
disc's matrices and no plane (`port/render/rd_chain.h`; `port/render/rd_content_chain.c`
(`rd__chains`); `ico2/seki/src/BgAnimation.c`). The table is checked against the disc by
`tools/content_table_check.py chains`, and against the Japanese disc by `--version jpn chains`
(its boot ELF, `port/data/gen/jpn/table_desc.c` and `port/core/region_jpn.c`'s
`chain_group_st10l`/`chain_group_st19a`; the same two chains, groups 731 and 636). The same run
reports how many of the stage animation files the disc's `stageTable` names its packs hold: 805
of 969 on the European disc and 801 of 946 on the Japanese one, every file missing from the
Japanese disc missing from the European one too (the development stages' rows). The Korean disc
has its own `--version kor chains` run (its boot ELF, `port/data/gen/kor/table_desc.c` and
`port/core/region_kor.c`), the test `content_table_chains_kor`; its counts are not recorded here.

## Strip joining

The [region rule](ARCHITECTURE.md#terms)'s card test treats a strip as free-standing when it shares
no vertex position with a strip of another shape (`rd_mesh.c` `markJoinedStrips`). Packet.c makes a
packet per material and texture of a part, so a wall strip with its own material shares its edges
with the rest of the wall only through the part's other packets. Once per replayed frame, before the
meshes are uploaded, `rd__join_parts` groups the VU draws whose keys name one object and part (the
key without its ordinal byte) and `rd__mesh_join_part` marks across their meshes. Every packet of a
part the game registers is drawn, so the group is the part's whole, live and in a dump's replay. Out
of the box the rule never reads the mark (`port/render/rd_join.c`). Whether a free-standing strip is
a card is decided on the CPU when the draw is recorded and travels in its payload
([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)); a strip the part joining marks is drawn whatever
its verdict.

## Screen planes

The GPU interpolates colour and ST from the pixel's place in the target, so its float rounding
depends on where the triangle sits. `doScreen` gives every screen-prim triangle its colour and S,
T, Q planes per GS pixel about its first corner, computed from the corners' GS positions and
values alone, and the shaders evaluate them at the pixel's offset from that corner: the same
numbers at every frame width. Sprites keep their own path, their colour being flat
(`port/render/rd_screen_planes.c` (`rd__plane_pass`); `port/shaders/sprite.hlsl`
(`sprite_planes`)). The VU programs' triangles do the same (`port/shaders/vu_common.hlsli`
(`vu_attr_planes`)).

Only such a pass carries the planes: `rd__plane_pass` turns its vertex shader into
`sprite_plane_*_vs` or `sprite_stq_plane_*_vs` and its fragment shader into `sprite_ps_planes`,
`sprite_texa_ps_planes` or `sprite_stq_ps_planes`, `sprite.hlsl`'s planes build
(`ICO_SPRITE_PLANES`, [SHADERS.md](SHADERS.md#the-families)), whose interface has the six plane
vectors after the colour and uv. Every other sprite, font, fog and wrap draw uses the plain build:
the position, the colour and uv or stq, two input locations. The plain build is the planes build
with the planes' zeros removed (the plain vertex shaders wrote zeros, and `sprite_plane_attrs`
returned at `p5.w == 0`), so the two builds draw the same bytes. The plane keys of the start-up set
are the same keys with the planes build's fragment id; their count does not change.

**Where the planes are used.** The planes' exactness holds at a whole scale: corners on the 1/16
grid and pixel centres on k + 0.5 give exact offsets only when the target's texels per GS pixel
are whole numbers. On a target whose scale is not a whole number on both axes (Enhanced at the
window's own size, 2364x1080 is 2.8125 x 2.41 a GS pixel) the planes buy nothing, and the
GPU's screen-space (noperspective) interpolation of the corners' values gives the same values
to a few ulp (a colour a level apart within about 3e-5 of a .5 boundary, an alpha-tested edge
pixel within a level of AREF on the other side; [DIVERGENCES.md](DIVERGENCES.md#video) VD-19). There, under
the Enhanced preset with the device profile's `vuPlanesFrac` (`[dev] vu_planes`), a VU pass of
`vu_ps` draws with `vu_np_ps` (its build without the planes' inputs, `ICO_VU_NO_PLANES`) and a
screen-prim triangle command with the plain sprite build. The planes stay for a draw with both
filters nearest (the GS's texel at a texel boundary), a texture with GS levels (the level by Q),
a VU draw with a chain clip plane (the discard at distance 0), `vu_texa_ps`'s draws, the STQ
screen triangles (their plain keys are not in the start-up set) and every draw under an exact
blend (the twin has no destination-read build) (`port/render/rd_pixel_plan.c`
(`rd__planes_dropped`, `rd__vu_pixel_fs`); `port/render/rd_screen_planes.c`
(`rd__screen_planes_want`)). Every whole scale, Original's 1x among them, keeps the planes
everywhere, so its pictures are unchanged. At a fractional scene scale (`g_rd.sceneSx`,
`sceneSy` at the start-up set) each `vu_ps` key of the start-up set has its `vu_np_ps` twin
(`rd__vu_pixel_fs_enum`); a later scale's are made at its change (DISPLAY.md). The log: the
profile's start line (`VU planes whole scales only`, or `every scale`), one `planes:` line a run
the first time a target drops them, and `vu no-planes N` (the VU passes drawn through `vu_np_ps`)
in `--stats` and the 10 s `window:` line.

## Camera blending

The presenter draws the pictures between two ticks through a camera blended from the two ticks'
cameras: the inverse views blended as affine matrices (the 3 x 3 split by polar decomposition
into a rotation, slerped, and a stretch, lerped; the eye lerped) and the projection lerped element
by element. The game's cull has to know that camera too, so these header-only functions are the
one implementation, included by `rd_interp.c` and `GsBase.c` in every build
(`port/render/rd_camera_blend.h` (`rdcb_blend_view`, `rdcb_blend_proj`, `rdcb_camera_jump`)). A
step past `RD_INTERP_CAMERA_TURN` (30 degrees) or `RD_INTERP_CAMERA_MOVE` (300 units) a tick is a
cut, and `RD_CULL_MARGIN` is the cull's frustum margin (`port/render/rd_camera_blend.h`).

**The eye never passes through the stage.** The game's cameras have no wall test of their own (their
sets and paths are designed), so a camera that moves less than those limits in a tick but past a
wall would put the blended eye behind the wall for the pictures in between, showing what is on the
other side for an instant. At every flip, on the game thread before `dl_Swap` closes the frame,
`gsbHostCullFlip` passes the eye (`+0x380`, the inverse view's translation) to
`ico_camera_wall_check`, which tests the step from the eye at the flip before against the stage's
collision: the walls and floors of the objects `MakeCollisionDependGObjList` gives the actors
(active, a stage layout object, displayed, with collision data), read from the live object table in
each node's frame through the block grids around the path, the walls `ClipWall`'s mode skips
skipped. A crossing either way (the game's own `ClipPlane` counts front to behind only) at a point
inside the element widened by `ICO_CAMERA_WALL_RADIUS` (4 units: the near plane's 2 and as much
again) makes `rd_camera_cut_wall` mark the frame that flip closes: `RdCamera.cut` 2, the snap
`RD_SNAP_WALL`, its pictures the tick's own as at the Original frame rate. The cut does not go
through `ico_video_camera_cut`, whose serial the window passes on after the step, to the frame the
next tick records. Not asked: a stage's first tick, a game cut pending since the flip before, photo
mode, a still eye and a step past `RD_INTERP_CAMERA_MOVE` (`RD_SNAP_CAMERA` already). The check runs
at every flip whatever the Frame rate and in the headless build, and only reads the game's memory
(`port/game/camera_wall.h`; `port/game/camera_wall.c` (`ico_camera_wall_step`,
`ico_camera_wall_cross_wall`, `ico_camera_wall_cross_floor`); `port/game/camera_wall_stage.c`
(`ico_camera_wall_check`, `stageQuery`); `ico2/seki/src/GsBase.c` (`gsbHostCullFlip`,
`gsbHostCullForget`); `port/render/rd_core.c` (`rd_camera_cut_wall`); `port/render/rd_interp.c`
(`rd__interp_snap`)). The log: `camera: between-tick blend skipped at stage N: the eye moved M cm
through a wall` (or `a floor`; rate-limited, with pictures blended) and `N where the camera's path
crossed the stage` in the 10 s `interp:` line's snaps; a dump keeps the 2 in its camera, so
`rd_replay_tool --interp` reports the snap as 9 (`RD_SNAP_WALL`).

**What the fog paints in a gap.** A pixel no draw covers keeps the clear's Z of 0, and the fog
sprite indexes its table by Z bits 16..23 (`port/shaders/fog_lut.hlsl`), so such a pixel takes
entry 0, the stage's full fog colour: near white in the light rooms. Neighbouring parts of the world
(a wall's or a floor's pieces, one draw per packet or texture) share their edge vertices, and the
vertex shader truncates each to the GS's 1/16 pixel grid (`port/shaders/vu_common.hlsli`
(`vu_ftoi4`)); two draws whose model to screen matrices differ in a bit can put a shared vertex in
two cells and leave a sliver of 1/16 GS pixel between them, which the fog paints. At a tick the
game's own matrices decide: most pieces of a stage carry the same model rows to the bit (one node
matrix), so their edges close, and pieces with other node matrices carry the rounding of their own
products, as on the PS2 ([DIVERGENCES.md](DIVERGENCES.md)). The fog probe line names entry 0
([The fog path and its self-test](RENDER_POST.md#the-fog-path-and-its-self-test)).

**Still draws take the tick's block.** Between ticks a matched VU draw's block is both ticks'
blocks re-based on the blended camera and blended, and a mesh's model matrices are then composed
as a turn about its own centroid (`rotateModel`), while a draw that is the tick's alone (unmatched,
jumped, unkeyed) is the tick's block re-based (`camCurDraw`). Done to the pieces of a wall, that
made pieces with identical rows at the tick come out a few ulps apart (another pivot, or the other
path), and the set of shared vertices that changed cells moved with the alpha at every present:
white lines flickering along edges while the camera moves, never with it still (a still camera
re-bases nothing). No row of a normal program's block is the object's alone (each model row is a
camera part times the object's model to world matrix, qw 4..15 are the camera's), so the two
ticks' rows of a wall never agree in bits under a moving camera; re-based on the blended camera
they agree to the rounding of the products. `stillRows` compares the model to screen rows (qw
16..19) on X, Y and W only, each against the largest of those three in its quadword (the GS Z row
carries zf, about 1.07e9, in the translation: `ico2/seki/src/GsBase.c` (`gsb_SetVSMatrixSub`)),
and the model to clip rows (qw 20..23) whole; not the model to view rows (qw 24..27), which some
list 5 draws carry from another object, while their clip rows are the frame's clip camera times
the W of their own model to screen rows in every dump measured. So compared, a still object's
re-based rows agree within 1.8e-7 of each quadword's largest magnitude in the corpus' blended
pairs, a moving object's 5.8e-3 or more apart. A part of the world (a normal program placed
through the view) whose rows agree within `RD_INTERP_STILL_REL` (2^-17) is a still draw. The clip
rows' magnitudes are about the view distance, so the bound holds a still part to 2^-17 of its
distance, about 0.004 GS pixels at the focal length of 500 (the model to screen rows alone would
allow 2^-17 x 2048, 0.016 pixels). A still draw's camera and model rows (qw 4..27) are the tick's
block re-based exactly as `camCurDraw` re-bases one (the projection taken to the blended one
under an easing zoom, as there), not the blend and no turn; its UV scroll and lights keep their blend. Every still
draw, matched or not, is then the same function of the tick's block and the blended camera, so
identical rows give identical bits across neighbouring draws. Moving objects keep the blend and
the turn (none at alpha 0); alpha 1 has no blended camera and is the tick's payload byte for byte,
and at alpha 0 the blended camera is the tick before's to the bit (below); an object
moving less than the bound is drawn at its tick's place, at most about 0.004 pixels from its blend
(`port/render/rd_interp.c` (`stillDraw`, `stillRows`, `blendVu`, `camCurDraw`)). The 10 s summary carries a line for them, and
`rd_replay_tool --interp` prints the same counts as its third line:

```
interp: N still draws (parts of the world that did not move while the camera did), each drawn from the tick's block through the blended camera; R of them the blend of the two ticks would have re-rounded (camera and model rows not the tick's through the blended camera, to the bit); W parts of the world drawn as the tick's beside blended ones; K VU draws with the ticks' cameras
```

R counts the still draws whose element-wise blend of qw 4..27, what they were drawn with before
the still draws (less the turn), differs in bits from the block they take: the draws the fix
changed. W counts the parts of the world `camCurDraw` re-based in a blended frame, K the draws
marked `RD_VU_TICK_CAMERAS` (`RdInterpStats` `stillDraws`, `stillReRounded`, `curWorld`, `tickCams`,
`port/render/rd_internal.h`).

**Alpha 0 is a present between the ticks.** On the timer path the present clock clamps a value
below 0 to exactly 0, which a present right after a tick closes often has
([DISPLAY.md](DISPLAY.md#interpolation-between-ticks)). `camSetup` takes t = 0 like any other t
below 1, with the tick before's camera exactly: the blended view is the tick before's view
(`s_cam.vt = s_cam.vp`), its inverse the inverse of that, `E_p` the identity and `E_c = Vc^-1 Vp`,
without `rd__blend_affine` (whose t = 0 is the tick before's view to about 1e-16, not its bits,
`port/render/rd_camera_blend.h` (`rdcb_blend_affine`)); the projection's lerp is the tick before's
there. So the frame's view is the tick before's bit for bit, every draw that is the tick's alone
(unmatched, snapped, unkeyed) is re-based onto that camera by `camCurDraw`, a still draw takes the
tick's block through it, a moving matched draw the tick before's block re-based (a few ulps from the
recorded one), the CPU-projected draws are re-projected and the draws of the tick before alone are
placed as at any other alpha, and the draws carry the ticks' cameras (`RD_VU_TICK_CAMERAS`). An
object that did not move is drawn where it stood at the tick before, whichever kind of draw it is. The
ripples' STs move with their vertices ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-ripples-between-ticks)).

**Payloads of two sizes.** A VU draw's payload carries its card section
([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)) only under the region rule (`port/render/rd_mesh.c`
(`cardKind`: clip mode REGION, in a state other than out of the box, `rd__vu_extended`)), and the
game switches a packet from the region code (32) to the scissor code (36) when its box crosses the
screen's edge (`ico2/seki/src/RegistPacket.c` (`reg_clipPacketBoundingBox`)), so a part of the
world along the picture's edge gains or loses the section between ticks. `blendVu` reads both
headers first and takes a draw as mismatched by size (`RD_MISMATCH_SIZE`) only when a payload is
shorter than its header says (the header, the block, the bones, the stream and the materials:
`vuFixedSize`); the header test then makes the two fixed parts equal, every offset after the block
derives from the headers, and the card sections stay each payload's own (`rd__vu_cards_merge` takes
each side's size, and leaves the tick's section as recorded when the tick before has none). Such a
draw blends like any other, a still part of the world among them. The 10 s summary's second line counts them (`N VU draws
blended across a change of payload size (a card section at one tick only)`,
`RdInterpStats.sizeTailBlended`), and `rd_replay_tool --interp`'s second line with the draws still
mismatched by size.

**The mixed picture.** A frame that blends without a blended camera (a frame without a camera, the
blend refused, `ICO_RD_S2_LEGACY=1`: `RdInterpStats.camOffWhy`, `RD_CAM_OFF_*`) draws its matched
parts of the world from the ticks' blocks as recorded, blended element by element, and the parts
that are the tick's alone from the tick's block, `camCurDraw` having no camera to re-base them on.
Both kinds in one picture stand up to a tick of camera motion apart, and the fog paints the gaps
between them, wedges where a piece entering the view (the game's cull) is the tick's and its
neighbour the tick before's. The counts are
`worldPrevRaw` and `worldCurRaw`, and `mixed` the smaller of the two when both are above 0 (the
pieces in the minority; whether they touch is not computed). The 10 s summary carries a line for
the presents that drew one, after the alpha line ([DISPLAY.md](DISPLAY.md#interpolation-between-ticks)),
and an event line for the first three of each block:

```
interp: M presents drew a mixed picture (parts of the world from both ticks' cameras, no blended camera): P parts from the tick before, Q from the tick, R in the minority; why no blended camera: no camera a, blend failed b
interp: present of frame F at alpha 0.4000 drew 212 parts of the world from the tick before and 4 from the tick: a mixed picture
```

`rd_replay_tool --interp` prints the same counts and why the frame had no blended camera at the end
of its third line (`port/render/rd_interp.c` (`camSetup`, `camCurDraw`, `mixedNote`)). The vertex shaders keep the order of their arithmetic: every VU vertex
shader carries `NoContraction` (HLSL `precise`) and no module `FPFastMathMode`, so no driver fuses
one draw's products and not another's (`tools/check_spirv.py`, the precise gate).

## Interpolation and presentation

With `RdSettings.interpolate` on, `rd_end_frame` only closes the frame and the host calls
`rd_present(alpha)` (the [present](ARCHITECTURE.md#terms)) as often as the display allows; what
blends and how is [DISPLAY.md](DISPLAY.md#interpolation-between-ticks)'s
(`port/render/rd_interp.c`). `rd_present.c` shows DISPLAY in a box of the frame's aspect on the
output, `rd_crt.c` replaces that blit under the CRT filter, and `rd_video.c` shows the FMV picture.
All of it is [DISPLAY.md](DISPLAY.md). Re-keyed screen prims:
[WORK_BUFFERS.md](WORK_BUFFERS.md#screen-sprites-redrawn-under-a-new-key-between-ticks).

**Rotation continuity.** A model matrix or a bone blends as a slerped rotation, and the slerp's
short way (at most 180 degrees) is the motion only while the object turns less than half a turn a
tick, and a turn over `RD_INTERP_TURN_SNAP` (120 degrees) with nothing to tell its direction keeps
the tick's pose. A wheel can turn more than that: the trolley's wheels reach 140 degrees a tick at
full speed (`box.c`: a push of 0.5 a tick against a friction of 0.98 is 24.5 units a tick, on wheels
of radius 10), and a faster wheel at 190 degrees a tick would blend back 170 the short way (issue
98). So each rigid draw's model matrix and each bone of a skinned draw remember the turn they made
between two ticks (a rotation vector in the object's own frame), and the next blend takes the way,
short or long, nearer that turn, when it lies within `RD_INTERP_TURN_KEEP` (60 degrees) of it and
turns at most `RD_INTERP_TURN_LONG` (300 degrees) (`rdcb_blend_affine_turn`): a turn over the snap
that continues the one remembered blends.

The memory is kept by the draw's identity, not its place in the list: its key, type and list, its
ordinal among the draws of that key, type and list in its tick (the order the pairing pairs them
in, so two draws of one key keep their own memories), and the bone. Each identity keeps its last
two turns with the tick each ended at and the pose it ended at, in one table allocated once
(`RD_TURN_CAP` identities; one whose newer turn ended more than `RD_INTERP_TURN_GAP` + 1 ticks
before is free). A pair takes the turn that ended at its first tick (the pair before, blended);
without one, when its turn would snap, it takes the last turn that ended 1 to `RD_INTERP_TURN_GAP`
(8) ticks before its first tick, when carrying that turn on that many ticks puts the draw within 60
degrees of its pose there. The second rule is what issue 98 needed: a pair that is not blended (a
camera cut, a fade, a present that never came, a draw missing or mismatched for a tick) remembers
nothing, and a turn kept at the tick's pose is never remembered (forward 190 degrees is back 170
the short way, so its direction is unknown and remembering it could make the next pair blend
backwards). Without the second rule one camera cut on the ride at full speed left both wheels at
the tick's pose for every pair until the trolley slowed below 120 degrees a tick: the reporter's
log shows 2 cuts and 246 turns kept at the tick's pose in the block, none continued the long way
(140 degrees a tick never needs it). A turn under the snap, or one with the memory of the pair
before, blends as without the second rule, bit for bit. A wheel first seen between 120 and 180
degrees a tick with no memory keeps the tick's pose until it slows below the snap, and one that
speeds up in steps under 60 degrees a tick carries its turn through 180. The camera keeps the
short way. The tick's own present is never blended, so the tick's pose is exact either way.

The 10 s summary counts the turns that kept the tick's pose (`snapped over 120 degrees`) and, only
when there were any, adds a line saying why (no memory of the draw, its memory older than 8 ticks,
the pose not where its memory carries it, further than 60 degrees from its memory, the long way
over 300 degrees), how many turns found no room in the table, and the three draws that did it most
by mesh name and key with their largest turn and the last reason (`RD_TURN_WHY_*`,
`RdInterpStats.turnWhy`, `snapNote`; counters only, nothing per frame). `rd_replay_tool --interp`
prints the pair's long ways and turns kept at the tick's pose (`port/render/rd_interp.c` (rotation
continuity: `turnFind`, `turnRecall`, `turnRecallGap`, `turnKeep`, `blendTurn`, `turnSnaps`),
`port/render/rd_camera_blend.h`, `port/render/rd_internal.h`).

## Performance records

A record (`RdPerfRecord`) is filled while a replay runs (its CPU phases and counts), closed with the
RHI's counters, and then waits in a slot per frame in flight until its GPU timestamps can be read
without waiting. The timestamps carry the effect whose work they end, so the record has a GPU time
per list, for the uploads, for the VU vertex pre-pass (`gpuPrepassMs`, from `RD_PERF_TS_PREPASS`
after the uploads to list 0: its pass and its copies), for the present, and per picture effect
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
[The vertex and triangle pre-passes](PIPELINES.md#the-vertex-and-triangle-pre-passes));
the GPU line (`window: GPU ms per replay: ...`) ends with `; VU pre-pass <ms>`, as `--stats`'s
`gpu:` line does
(`port/render/rd.h` (`RdPerfRecord`); `port/platform/window_host.c` (`perf_log`)).

The pixel diet's six counts are in the record too: `alphaOnePass` and `alphaOpaque` (draws the alpha plan
drew in one pass where the plan had two, and without the test and the blending), `fxConstant`,
`fxAdditive` and `fxLocal` (the post sprites drawn by the constant fold with the black sprite's clear, the
premultiplied forms and the local read: [Post passes](RENDER_POST.md#post-passes)) and `vuNoPlanes` (the VU passes drawn
through the no-planes pixel shaders: [Screen planes](#screen-planes)). Each is 0 with its flag off or where
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
(`stencil clear`) and the present (`present`: DISPLAY onto the window and the CRT filter)
(`port/render/rd.h` (`RD_PERF_PASS_*`, `RdPerfPass`); `port/render/rd_replay.c`,
`port/render/rd_replay_vu.c` (`rd__vu_prepass`)). Every replay counts its pieces by kind and its
render passes by effect (`RdPerfRecord.passKinds`, `passEffects`). A timed replay also writes a
timestamp at each piece's start, `RD_PERF_TS_PASS0` (32) plus the piece's index, keeping the
barriers the backend holds back held (so the barriers recorded are an untimed replay's), and one
after the last piece (`rd__perf_pass_end`, before the list ends); a piece's GPU time is its stamp
to the next one's, so it includes the barriers before its first command and nothing of another
piece. Its draws and pipeline binds are the RHI's counters between the two, its CPU time the
replay's recording of it. Each frame slot has 256 timestamps (`RHI_MAX_TIMESTAMPS`,
[RHI.md](RHI.md#the-interface)), so a replay times up to 223 pieces; a replay that begins more times
the rest with the last and says so (`passPartial`). The pieces wait for their times with the
record and then in a queue of 8 replays for `rd_perf_passes_of` (`port/render/rd_perf.c`, "the
passes").

Which replays are timed: every replay with `[dev] perf_passes = true` (`ICO_RD_PERF_PASSES=1`,
[CONFIG.md](CONFIG.md)); otherwise the first replay at least 250 ms after the last timed one
(`RD_PERF_PASS_EVERY_MS`), at most 40 in a 10 s block, for the window's summary. Not on iOS (as
the effect stamps). At start the renderer says which:

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

(the numbers show the fields, not a measurement). The top passes are the timed replays' pieces
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
(the attribution ended at the next TARGET, [above](#performance-records)); list 7 holds the
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
the same). The alpha-test merge (the AFAIL split under [Pipelines](#pipelines)) is in the "with" column
too: 9 fewer draws on the title frame, 42 on the Mac frame, the pictures byte for byte the same.

The module sizes (`tools/check_spirv.py --sizes`, words / instructions / blocks):
`fx_sprite_ps` 8,536 / 2,058 / 350 without the cuts, 9,323 / 2,260 / 390 with them;
`sprite_texa_ps` 10,181 / 2,401 / 378 and 11,501 / 2,761 / 468; `vu_texa_ps` 10,346 / 2,443 /
384 and 11,666 / 2,803 / 474; `sprite_ps` 7,548 / 1,792 / 269 and 7,812 / 1,864 / 287. The
modules grow (each REPEAT coordinate keeps both forms behind its branch) while a pixel runs
fewer instructions: no `OpSDiv` in `fx_sprite_ps` (a division is 19 straight-line instructions,
one of them a float divide), no `OpSRem` on a power-of-two size, no float divide at scale 1.

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
  bilinear and alpha test inside the box ([the glow's passes](#the-glows-passes)), not done here.

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
  writing every texel) would save the colour copy too, but every texel would make an RGBA8 round
  trip through a float output, which the Vulkan specification lets round to either neighbour,
  and the UINT view that would make it exact is off on tilers.
  `build-host/tmp/v098/evidence/perf/crack-pass.sh` measures the pass structure with and without
  the fill on lavapipe under the PowerVR and the Qualcomm-driver profiles.
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
read](#the-replay)). The profile takes it wherever the device has local read: every tile-based
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
- *Not tested.* Particles; code 34 (clip mode none: X and Y wrap to 16 bits); code 36 behind the eye
  (its triangles without a clip flag are drawn at their GS positions whatever `w` is); a draw with
  no target, a payload or a layout the test does not read, or a bound not finite; lists 11 and 12;
  a pass window (the bisect's and the tool's `--passes`: the passes as recorded) and a frame never
  recorded (number 0: the self-tests' frames).
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
- *The gate.* `rd_cull_ab` ([TESTING.md](TESTING.md)) replays the dumps of
  `tools/occlusion_corpus.txt` and `tools/native_frame_dumps.txt` with the test off and on at
  Original 4:3, Enhanced 4:3 2x, Enhanced 2x at a wide dump's own aspect and once with MSAA 4x:
  every target's PNG byte for byte, and draws skipped over the corpus. `rd_vis` holds the bounds,
  the planes under random cameras and the VU references (`vu1_ref`) as the oracle.
- *The key and the lines.* `[dev] cull = off | frustum | full` (`ICO_RD_CULL`; frustum by default;
  full asks for the occlusion test too, which is not built: it runs as frustum and the start line
  says so). The start line `rd: cull: frustum ([dev] cull, ICO_RD_CULL): ...`, the summary's `cull
  frustum`, the backend's 10 s line `; cull: N of M scene draws skipped a replay (F frustum, O
  occluded), T ms ([dev] cull)` and `rd_replay_tool --stats`'s `stats: cull frustum: N of M scene
  draws skipped (F frustum, O occluded), T ms; not tested: E exempt, U unbounded; skipped beyond
  the left ...` (`--cull off|frustum|full` sets the variable; `--occlusion` replays with it off).

## Dumps and the replay tool

A [.rddump](ARCHITECTURE.md#terms) is the in-memory frame, little-endian, versioned by
`RD_DUMP_VERSION` (14): the frame's native size and aspect, the disc it was drawn on (its region
slug (`pal`, `jpn` or `kor`), version 12; an older dump is the European disc's), its camera, start and end states, the
13 lists, the payload, the textures and temporary targets it references, its VU meshes and tick
cameras. Target
contents are not written: a frame that reads a retained target replays from what the loading
context holds. Loading creates the textures and targets anew and rewrites every id, and gives the VU
draws recorded without card verdicts theirs ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)), so
a dump replays alike at every setting. `rd_replay_tool` selects the dump's region
(`ico_region_select`) before it replays, so the reduction's crop and the border mask
(`rd__reduction_crop`) are that disc's. Dumps hold game assets and are never committed
(`port/render/rd_dump.c`, header comment).

Dumps are written by F12 and Shift+F12 ([PLATFORM.md](PLATFORM.md#hotkeys-and-dump-zips)) and by
`[dev] dump_every = N` (with `dump_dir`, `dump_interp`, `dump_from`), which the host hands to rd as
`ICO_RD_DUMP_EVERY`, `ICO_RD_DUMP_DIR`, `ICO_RD_DUMP_INTERP` and `ICO_RD_DUMP_FROM`; `dump_interp`
also writes the frame before (`-prev`) and the frame blended half way (`-i50`)
(`port/platform/window_host.c` (`frame_dump`); `port/platform/host_config.c` (`export_dump_keys`);
`port/render/rd_core.c` (`readDumpConfig`, `rd_end_frame`)). `rd_png.c` writes uncompressed PNGs for
the tool and the tests (`port/render/rd_png.c`).

`rd_replay_tool <dump> <out.png>` loads a dump (or the first `.rddump` in an F12 zip) and renders
it headless; `--interp T PREV` with PREV an F12 or loss zip takes the zip's second `.rddump`, the
tick before, so a report's zip rebuilds the pictures between its two ticks, and with T the alpha
F12's line names for its `-present.png` ([DISPLAY.md](DISPLAY.md#hotkeys-for-pictures)) the picture
that was presented (`port/render/tools/rd_replay_tool.c`, usage comment).

`--time N` measures the CPU side of a replay on a dump. After the picture is written the frame is
replayed N more times; with `--interp` the presenter's frame is built again before each
(`rd__interp_frame`: the first as a tick's first present, the others as its later presents, its
time charged to the replay as `rd_present` charges it). The tool prints one line from the N
records, each CPU phase's mean and least: `<dump>: time: N replays, CPU mean (min) ms: total,
interp, wait, acquire, upload, walk, bind, submit, present; per replay: ring KB,
uniform KB, interp KB, draws, pipeline binds, bind group binds`, then `<dump>: time: bind cache: X
pipeline, Y bind group and Z state repeats skipped a replay` (the binds the RHI found in force and
recorded nothing for, [RHI.md](RHI.md#the-bind-cache); `--stats` prints the replay's own as
`<dump>: stats: bind cache: ...`), then `<dump>: time: the first replay: interp X ms, total T`.
The walk column is what the bind cache moves: `build-host/tmp/v098/evidence/perf/bindcache.sh`
runs `--time 5` on three corpus dumps with two builds. With `--interp` it compares DISPLAY after the
first and the last of the N replays byte for byte and prints `pictures identical`, or the bytes
that differ and exits 1: a present later in a tick must draw the tick's first present's picture
(`timeReplays`). The lavapipe numbers are for comparing two builds on one host, not a device's
times.

The options:

| option | does |
| --- | --- |
| `--target NAME` | writes a named target instead of DISPLAY |
| `--pass-dump DIR` | writes every named target to `DIR/<NAME>.png`, and with `--present` the presented picture |
| `--dump-temps DIR` | writes every per-frame temp target (the water blocks among them: puddle.c's 344x256 reflection block with its depth) to `DIR/temp-<n>-<w>x<h>.png` right after the replay, before the frame's end frees them |
| `--present WxH` | renders the presenter into a W x H output |
| `--backend vulkan\|d3d12` | picks the RHI backend, to compare backends |
| `--enhanced`, `--aspect`, `--resolution`, `--full-height`, `--full-pixel`, `--border-mask` (Full pixel off as the game has it), `--water-sharp` (Water reflections: Sharp), `--filter`, `--mirror`, `--crt` | the display options a dump does not carry; the PS2 picture by default |
| `--list` | every command of the replayed lists, with textures, prims, boxes and state, then the water summary (`water: ...`, [above](RENDER_POST.md#water-and-render-to-texture-blocks)) |
| `--no-device` | loads into a record-only renderer for `--list`, `--mesh`, `--dump-textures`, `--region-stats` |
| `--nop SPEC` | turns commands into NOPs, to find the draw behind a pixel; SPEC is `L:A[-B]`, several separated by commas (`0:12-14,0:40`), or `@FILE` for a spec in a file; the state commands in a range stay |
| `--occlusion` | the hidden-draw measurement of list 0 ([Culling](#culling)): `<dump>: occlusion: hidden N of M list-0 draws, T of U triangles (index count / 3); hidden commands: SPEC` (SPEC a `--nop` spec, `none` when empty), then `<dump>: occlusion: H of K draw commands hidden; P % of the draws, Q % of the triangles; Q queries, X without a count, Y draws past the 8192 queries; Z draws of other commands not counted; measured under ...`; it sets `ICO_RD_VU_MERGE=0` and `ICO_RD_BLEND=fixed` and turns the screen-prim runs off (the `note:` line says so); Vulkan only (exit 77 elsewhere); not with `--presents`, `--loss-bisect`, `--keys` or `--no-device` |
| `--occlusion-out FILE`, `--occlusion-csv FILE` | with `--occlusion`: the spec written to FILE (for `--nop @FILE`); a line a command of list 0 that made a GPU draw, `cmd,type,draws,counted,triangles,samples` |
| `--region-stats` | the region rule's verdict on every triangle of the VU draws of lists 0-6 under it, on the CPU ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#region-statistics); `port/render/tools/rd_replay_region.c`): a line a draw, `<dump>: region: L:I mesh NAME tris T: exact E clip C strip S drop D (behind-eye B, out-of-box X, card K; rescued at a tick R) det -\|+`, a line a list, and the frame's, which ends in the draws that draw nothing as a `--nop` spec; with `--mesh NAME` only that mesh's draws get a line; with `--interp` at the blended camera and the ticks'; also with `--no-device` (the Original preset at 1x there) |
| `--region-png FILE`, `--region-out FILE` | imply `--region-stats`: the dropped triangles of the frame camera's draws painted as the GPU would draw them on the native frame (red a vertex behind the eye, yellow out of the box, cyan a card; the 4:3 band outlined in green); the draws that draw nothing written to FILE as a `--nop` spec (`none` for none) |
| `--gpu-repeat K` | with `--stats`: RHI_FRAMES_IN_FLIGHT + 1 + K replays after the picture and, after the `gpu:` line, `<dump>: gpu median of R replays: total X ms, list 0 Y ms, VU pre-pass Z ms` over the warm records that came back (the first replay after the picture left out) |
| `--mesh NAME` | a VU mesh vertex by vertex, with the kick, overlap and joined marks |
| `--dump-textures DIR` | every image texture as decoded |
| `--stats` | the replay's performance record, with GPU times |
| `--pass-times` | with `--stats`, the warm replay timed pass by pass ([The per-pass timing](#the-per-pass-timing)) |
| `--time N` | after the picture, the frame replayed N more times and the mean and least of each CPU phase of their records printed (below) |
| `--prev DUMP` | replays earlier dumps first so retained targets hold the run's pictures |
| `--no-half`, `--no-aa1` | the frame without the field's half pixel or without PRIM.AA1 |
| `--no-crack-fill` | the frame without the crack filler above 1x ([Crack filling above 1x](RENDER_POST.md#crack-filling-above-1x)): its A/B; `--stats` prints the filler's passes and the pixels it filled |
| `--interp T PREV` | the frame the presenter builds between PREV and the dump at alpha T (PREV an F12 or loss zip: its second `.rddump`, the tick before) |
| `--interp-any`, `--no-reproject` | with `--interp`: PREV taken as the tick before whatever its frame number; the CPU-projected draws blended in screen space |
| `--interp-nop-prev L:A[-B]` | with `--interp`, repeatable: turns commands A..B of list L of PREV into NOPs before the blend, as `--nop` does to the dump, so their draws in the dump are the tick's alone (unmatched): chosen parts of the world made the tick's in a blended picture, the [mixed picture](#camera-blending)'s case |
| `--aa MODE` | (with `--enhanced`) the anti-aliasing setting: off, ssaa2, ssaa4, msaa2, msaa4, msaa8 |
| `--crt-strength K` | after `--crt`, the strength 0..1 |
| `--overlay-test` | (with `--present`) a test pattern and a popup drawn as a presentation overlay |
| `--vu-ref L:I` | the CPU mirror's vertices of prelit draw I of list L |
| `--loss-bisect` | the [loss bisect](#the-loss-bisect) on the dump: the frame one pass longer each time, waiting for the GPU after each |
| `--keys` | the pipelines the replay asks for against the start-up set, one line a key (`in set` or `MISSING`), exit 1 on a miss ([The start-up set](PIPELINES.md#the-start-up-set)); needs a device, `<out.png>` unused (give `-`) |
| `--passes A[-B]` | replays passes A..B only (the [pass window](#the-loss-bisect)); A alone is pass A alone |

## Shader families

The entries the replay binds are named in `s_vsNames` and `s_fsNames` (`port/render/rd_replay.c`).
The families and what each entry draws are [SHADERS.md](SHADERS.md#the-families)'s.

## Tests

The renderer's tests are `port/render/test/`, listed with their ctest names in
[TESTING_PROGRAMS.md](TESTING_PROGRAMS.md#portrendertest); the pixel gates on dumps are
[TESTING.md](TESTING.md#render-gates). The variants run the same program under `ICO_RD_NO_DUAL=1`
(`_nodual`), `ICO_VK_FAKE_LIMITS=min` (`_minlimits`) or `mali` (`_mali`), `ICO_RD_BLEND=fixed`
(`_fixed`) or `storage` (`_storage`) and `ICO_VK_FAKE_D24S8=1` (`_d24s8`)
(`port/render/CMakeLists.txt`). `ctest --test-dir <build> -R '^rd_'` runs rd's own.
