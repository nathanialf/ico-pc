# Pipelines

The pipeline key, the blend paths, the start-up set, the fallback entries and the vertex and triangle pre-passes of the renderer (`port/render/rd_pipeline.c`, `rd_replay_vu.c`). This page was part of [RENDER.md](RENDER.md); the other render pages link here.


The shaders take the alpha test, the AFAIL split, the texture function, TCC, TEXA, FBA, PABE,
DATE and the blend factor's source as uniforms, so the pipeline key is normalised: one
representative blend per blend path, and the AFAIL split carried by Z write and colour mask
(`port/render/rd_pipeline.c`, header comment). The blend paths on UNORM targets
(`port/render/rd_pipeline.c`):

| modes | path |
| --- | --- |
| 0, 1, 5, 6 (Cs*F + Cd, Cd - Cs*F) | `DF_PREMUL`: the shader writes `min((Cs*F) >> 7, 255)`, the blender adds or reverse-subtracts with ONE; exact for F up to 255 |
| 2, 4, 7 (the LERPs) | dual-source factor F/128, F above 0x80 clamped |
| 3 (Cd*FIX + Cs) | SRC1 as the destination factor, FIX clamped to 0x80 |
| 8, 9, 10 (Ad) | DST_ALPHA, which reads Ad/255 |
| 11 (Cd*As + Cd) | not representable: Cd unchanged, reported once |

PABE writes Cs unblended for a pixel whose As MSB is clear, through the second output; the
premultiplied and Ad forms cannot, and those pixels blend (reported once). On a device without
dual-source blending the LERPs and mode 3 draw in two passes: colour with the factor in c0.a, then
when the mask has A an alpha pass with blending off and no Z write under a Z test that admits the
colour pass's fragments (`port/render/rd_pipeline.c` (`rd__expand_no_dual`)). `ICO_RD_NO_DUAL=1`
forces the two-pass form on any device (`port/render/rd_core.c` (`rd_init`)). Where the destination
read is available these paths give way to it (next section), and where the device orders the
colour read the key keeps none of them ([GS state as data](#gs-state-as-data)).

**The AFAIL split.** An alpha-tested draw whose failing fragments write something other than its
passing ones is two draws: the passing fragments with the draw's Z write and mask, then the failing
ones under AFAIL (FB_ONLY: no Z; ZB_ONLY: no colour; RGB_ONLY: no Z, no alpha), the shader keeping
one side each (`gs_alpha_discard`, `g_mode.z` bits 16..17). Where the failing fragments write
exactly what the passing ones write the test decides nothing and the draw is one draw without it,
every fragment in the GS's primitive order: Z write off (ZBUF.ZMSK 1, or no depth attachment) with
AFAIL FB_ONLY, or with RGB_ONLY when the mask already leaves alpha out (`port/render/rd_pipeline.c`
(`rd__plan_screen_draw_ex`)). The GS User's Manual's TEST register: FB_ONLY updates the frame buffer
and not the Z buffer for a failing pixel, RGB_ONLY the frame buffer's RGB only, and with ZMSK 1 a
passing pixel updates the frame buffer only too. PCSX2 does the same: its software renderer skips
the test for FB_ONLY without Z write and for RGB_ONLY without Z write on a 24-bit frame
(`GSDrawScanline.cpp` `TestAlpha`), its hardware renderer sets ATST ALWAYS for FB_ONLY without Z
write and turns RGB_ONLY without alpha into FB_ONLY ("Failing alpha test is a NOP",
`GSRendererHW.cpp`). The split had drawn every failing fragment after every passing one, so where a
failing and a passing fragment of one draw met on a pixel the later one in the GS's order did not
always win (issues 95 and 103). DATE, FBA, PABE and the blend ride on the one draw as
on both. The merged draw's key is the passing draw's, which the failing draw
shares wherever the merge applies, so the merge adds and removes no pipeline key: the dual,
no-dual and exact-blend sets keep their counts. The scissor batches' two draws (code 36: the clipped fans with ABE forced on, then the
strip's kicks) are another state and another set of triangles, not this condition, and stay two
(`port/render/rd_replay_vu.c` (`doVuDraw`)). The split still orders the passing fragments first where
it remains: Z write on (each draw writes other buffers), RGB_ONLY with alpha written, ZB_ONLY with
colour written. In the dumps measured (2026-10-16) the merged draws are the Z-write-off alpha-tested
ones: meshes `si_hair`, `bmd-tuno`, `cloud`, `cloud2`, `wf_ab03`, `fogs`, the particles, and two
sprites of list 11 on the title frame; there 6 VU commands and 2 screen prims, of 172 VU commands
whose other 166 alpha-tested ones write Z. The log says once that the rule is in effect: `rd: alpha test: a draw with Z write off
whose failing pixels write what its passing pixels write (AFAIL FB_ONLY, RGB_ONLY without alpha) is
one draw without the test, in the GS's order (reported once)`; `rd__set_alpha_merge(false)` plans the
two draws (tests: ctest `rd_state` `testAlphaMerge`, the condition table, each merged key a key of the split plan, and the game's FB_ONLY state without Z write in the screen set; ctest `rd_pixel`
`testAlphaOrder`, an overlap the split drew in the other order).

**The alpha plan.** The planned passes above assume any source alpha; where the range of As a
draw's pixels can take decides the test or the blend for every pixel, the alpha/blend planner
draws only what that outcome needs, byte for byte what the planned passes write
(`port/render/rd_alpha_plan.c` (`rd__alpha_range_of_draw`, `rd__plan_screen_draw_alpha`), called
after the plan by `port/render/rd_replay_vu.c` (`vuDraw`, the range once a command in `doVuDraw`)
and `port/render/rd_replay.c` (`doScreen`), before the exact blend's and the two-pass blend's
expansions; a mesh's vertex span is kept while its stream's serial holds, `meshSpanMemo`,
[RENDER.md](RENDER.md#performance-notes)). The rules, each a fact of the GS's integer arithmetic and
of the fixed-function path:

| rule | condition over the range lo..hi | the plan | why the bytes are the same |
| --- | --- | --- | --- |
| every pixel passes | the test passes for every As (`gs_alpha_pass`) | the pass of the passing pixels without the test; the failing pixels' pass dropped | `gs_alpha_discard` keeps every pixel in the first and none in the second |
| every pixel fails | the test fails for every As | the failing pixels' pass without the test; the passing pixels' pass (and AFAIL KEEP's one pass) dropped | the other way round |
| factor one | a LERP with FIX 0x80 or As 0x80 over the range, or PABE with every As below 0x80 | blending off (the ABE-off pass) | `((Cs - Cd) * 0x80 >> 7) + Cd = Cs`, PABE writes Cs unblended (`gs_dst_channel`); the blender's `Cs * 1 + Cd * 0` is Cs; alpha is As whatever the equation |
| factor zero | a LERP or a premultiplied add or subtract (modes 0, 1, 2, 4, 5, 6, 7) with factor 0 | R, G and B masked, blending off | `((A - B) * 0 >> 7) + Cd = Cd`, the shader's `(Cs * 0) >> 7` is 0; alpha As |

The factor-zero rule applies only where the blend and the mask are dynamic state (`g_rd.dynBlend`), so the mask
makes no pipeline the start-up set lacks; the other rules give keys the plan already has (the test is a
uniform; the factor-one key is the state's with ABE off, which the VU and screen families enumerate). A pass
left writing nothing (mask 0, no Z write) is dropped. Not planned: DATE, PRIM.AA1, the text
shaders (their alpha is not As), and for the factor rules PABE over a range with and without the MSB,
COLCLAMP 0, mode 3, the Ad forms, mode 11 and the exact blend (`g_rd.dstMode`, whose passes blend
in the shader). One residual: a texel sampled at a coordinate that is not a number (a degenerate
triangle whose Q interpolates to 0 under the anisotropic sampler) tested as alpha 0 on the unplanned
path, where the plan trusts the range; two pixels of one 16:9 frame at the phone's scale, none at a
whole scale.

The range is the texture function of the texel's alpha and the vertex's: MODULATE `min((t * c) >>
7, 255)` is monotonic in both, DECAL with TCC takes the texel's, TCC RGB the vertex's. The
texel's is the least and the largest alpha over every texel of every level the device holds:
`rd_upload.c` scans each upload of a game texture (level 0 whole with its zero padding, which
REPEAT reaches, then each level `uploadMips` copies, after `rdtex_keep_alpha_coverage`, or the GS
levels) and of a pack replacement's RGBA8 levels, and keeps the scan with the texture's slot,
its id and its RHI texture (`port/render/rd_upload.c` (`texAlphaKeep`, `rd__tex_alpha_scan`));
a texture that is dirty (a CLUT scroll's new generation, a coverage change, a new level count),
refused, compressed, a render target or not uploaded in full is not known. The 24- and 16-bit
sources expand under the TEXA in force, as the shaders do (`port/render/rd_tex.c`
(`rdtex_alpha_range`): TA0, TA1 for a set A bit, 0 under AEM only where all three colour
channels' least texel is 0, since a filtered channel is at least its least texel). Every filter
the draws use weights texels with non-negative weights (bilinear, trilinear, anisotropic, the
GS's 4-bit steps of `gs_bilerp`, the levels in 256ths), so its result lies within that range. The
vertex's is the least and the largest alpha the vertex shader computes over the draw's vertices:
`vu_rgba`'s low byte of `vu_ftoi` of quadword 2's w (prelit), 3's (lit, specular, reflection) and
4's (skinned) after `min(w, 255)` where the program clamps, every vertex of the mesh's stream as
the replay finds it (one scan a mesh a replay, so a stream that blends between ticks is scanned
for each present), a grid's batch colours and a particle batch's alphas from the frame's payload,
a screen prim's vertices as drawn; under the cluster's code 24 (`vu_skin_vertex` scales by
`mem[2].w`, the product `precise`) both floats around an inexact product, the one a device's
multiply gives among them; a NaN is not known. The interpolation, the GPU's or the planes' (`vu_pixel_attrs`), is a convex
combination of the corners, and a vertex range whose ends differ is widened by one each way
against its float rounding. Anything not known is [0, 255], which decides nothing.

`[dev] alpha_plan = false` (`ICO_RD_ALPHA_PLAN=0`) turns the planner off (`RdProfile.alphaPlan`,
on in every class). `rd_replay_tool --stats` and the window's 10 s line count the draws it drew in
one pass where the plan had two and those it drew without the test and the blending (`pixel diet:
alpha plan N one-pass, M opaque`; `RdPerfRecord.alphaOnePass`, `alphaOpaque`). Tests: ctest
`rd_alpha_plan` (every ATST x AREF x range against `gs_alpha_pass`, the blend identities over every
Cs and Cd, a model of the fixed-function path over the blend modes, the ranges), ctest `rd_pixel`
`testAlphaPlan` (one frame replayed with the planner off and on, SCENE's colour and depth the same
bytes) and `testRailing` (the lattice keeps its split).

The cache holds `RD_PIPELINE_CACHE_MAX` keys (4096), the RHI's pipeline pool less the pool's
headroom for the pipelines made outside the cache (`port/rhi/rhi_pipeline_pool.h`
(`RHI_PIPELINE_POOL_CACHE`); [RHI.md](RHI.md#the-pipeline-pool)), so the pool never refuses a
pipeline the cache keeps. `RD_PIPELINE_REACHABLE_MAX`, half the cache (2048), is the ceiling the
tests hold every reachable set under (`rd__enumerate_reachable`), the other half left for the keys
made on first use. A key past the cache or one whose creation failed is logged and drawn as
nothing, never an abort, and a key that failed is never created again in the run ("The set against the pool" in
[The start-up set](#the-start-up-set); `port/render/rd_state.h`; `port/render/rd_pipeline.c`
(`rd__get_pipeline`)). `rd_precreate_pipelines` creates the whole
reachable set at start so the first frames do not stall on the driver. It hands the keys to
`rhi_create_pipelines` up to 32 at a time, so the driver compiles each batch on worker threads
([RHI.md](RHI.md#the-interface)); a batch holds keys with a destination read or keys without, never
both. The progress function runs while a batch compiles (about every 50 ms, with the count so far)
and between batches, and a nonzero return stops the set after the batch in flight; the rest is
created as the frames need it. The activity note names the batch: `creating pipelines 321-352 of
702: <first key> to <last key>; vertex <module> <words> words, ...; largest fragment <module>
<words> words` (a point key's words, the vertex pre-pass's and the record stages', end with
` pre`), put back after any pipeline the progress screen's frame creates in the middle of it. The log line after the set adds the compiles, the threads and the longest compile:
`pipelines: <made> of the reachable set's <n> created at start-up in <t> ms (<f> failed); <c>
compiled on <k> threads, the longest <s> s`, and two lines say where the time went, from the
workers' own times (`RhiPipelineBatch.ms`): `pipelines: the slowest: <s> s <key>; ...` (the ten
slowest compiles) and `pipelines: compile time by vertex shader: <entry> <s> s, ...` (summed over
the threads, the largest first) (`port/render/rd.h` (`rd_precreate_pipelines`,
`RdPipelineProgressFn`, `rd_precreate_longest_ms`); `port/render/rd_pipeline.c` (`preTimesLog`)).
On a PowerVR B-Series driver the batches compile on one thread
and every module and pipeline creation, the background finisher's and a first use's included,
takes one lock (`RhiLimits.serialPipelines`, `rhi_vk: pipelines: one at a time (PowerVR B-Series:
the driver's compiler is not thread-safe)`; issue 188; [RHI.md](RHI.md#the-interface), **One
compile at a time**), so the line reads `compiled on 1 thread`.
At start the renderer logs the embedded modules' count and the largest, `shaders: <n> modules,
largest <entry> <words> words`: a driver's compile time grows with a module's size, which
`spirv_check` holds to a budget ([SHADERS.md](SHADERS.md#the-envelope); `port/render/rd_replay.c`).

**Dynamic state.** Where the device has it the Z test, the Z write, the blend and the colour mask
are set at each draw instead of being part of the pipeline ([RHI.md](RHI.md#dynamic-state);
`RhiLimits.dynamicDepth`: Vulkan 1.3 or `VK_EXT_extended_dynamic_state`; `RhiLimits.dynamicBlend`:
`VK_EXT_extended_dynamic_state3`; D3D12 neither). The cache then holds the keys with those fields
normalised away (`port/render/rd_pipeline.c` (`rd__key_dynamic_normalise`)): a key with a depth
attachment and no stencil mode takes `ztst` ALWAYS and `zwrite` OFF, a key with one colour target
and no destination read takes `blend` `RD_BLEND_COUNT` and mask `0xF`; the shadow count's stencil
keys keep their depth state, the point keys (the vertex pre-pass's and the record stages', three
targets) every state, and `nodual` stays in the key (it picks the fragment module).
`rd__get_pipeline` normalises the key it is given (`drawKey`), the start-up set is enumerated
normalised (`rd__enumerate_reachable`, after the two-pass and destination-read expansions, so the
set holds the keys the draws' pipelines are made from), and every draw binds through
`rd__set_pipeline(cl, p, key)` with the key it planned, un-normalised: it binds and sets the depth
state and the blend state from that key through `depthCompare` and `blendState`, the functions the
static state is made with, so each draw gets the fixed-function tuple a pipeline of its own key
would have had and the picture is the same byte for byte. `rd__pipe_key_equal` compares two keys
modulo the normalisation (a key as a planner made it matches the set's). The draws that differed
only in those fields (the AFAIL halves, the Z-write alternations, the blend paths of one shader)
share one pipeline, so the set is smaller and fewer binds change the pipeline; `rd_replay_tool
--stats` prints `<n> dynamic state sets (dynamic depth on|off, blend on|off) (<a> for draws without
blending, <b> ...)`, the commands recorded ([RHI.md](RHI.md#dynamic-state)). The log says which at `rd_init`, `rd: pipelines: dynamic state:
depth and blend (the Z test, Z write, blend and colour mask set at each draw, out of the pipeline
keys)` (or `depth (...)`, `blend (...)`, `none (every state in the pipeline keys)`), and the
start-up set's line carries ` (dynamic depth, blend)` after `created at start-up`; the first-use
line prints the normalised key. `ICO_VK_FAKE_NO_DYNAMIC=1` (`=blend`: the blend alone) turns it off
on a device that has it, for an A/B of the same frames.

### GS state as data

The fields above multiply out over the families into the start-up set: on a PowerVR DXT-48 phone
1297 keys, of which the VU family's 363 are 11 vertex
shaders x 3 pixel shaders x 11 states: 2 Z writes x 4 blend paths, and the 3 blends of a
material TEST's RGB_ONLY half (colour mask 7, below); DATE's 514 its stencil twins (the VU keys'
264 with colour mask F, the screen's 224: 7 states x 2 spaces x 2 topologies x 4 blends x 2 pixel
shaders, and 26 more), the 2D family's 361 (blend x 4 and mask x 2 the largest factors).

**A material's own TEST.** The disc's model data writes a TEST before a skinned mesh or a grid
(the dumps' commands: TEST, ALPHA, CLAMP, FBA, the texture, ABE, then the draw); no game source
holds it as a literal, so the VU states were listed from the list defaults and the source's
TEST words alone. Most are ATE GREATER with AFAIL FB_ONLY (the list 1/2 default's split); some
are AFAIL RGB_ONLY under Z GEQUAL, AREF 0x01, 0x30, 0x40, 0x46 or 0x5A (the corpus dumps
`rd-00200` and `rd-00300`, the deja veil's dump and a phone's dump; ALPHA 0x44 and mode 0,
ABE on, and the code 36 kicks with ABE off). Their failing pixels' pass is a key of its own:
colour mask 7 (R, G, B), Z write off (`rd__plan_screen_draw_ex`, the AFAIL split). The VU family
lists it for every vertex shader under ABE off, the LERP and the premultiplied add (blend 12, 4
and 5), the three the dumps show, with `vu_ps`, `vu_texa_ps` and at a fractional scale `vu_np_ps`
(`port/render/rd_mesh.c` (`enumerateVuDraws`, `VU_TEST_AT_GT64_RGBONLY`)): 6 keys a vertex shader
at a whole scale, 9 at a fractional one. The passing pixels' pass is the FB_ONLY state's key. No
DATE twin: the material's TEST takes the list 4 default's place, DATE with it
(`rd__enumerate_reachable_date` twins the VU keys with colour mask F only). A PowerVR phone made
three of them on first use, 0.6 to 0.8 s each, the first time the game drew that way on entering
a room (its log: `program 0 blend 4 vs 4 fs 37 ... ztst 2 zwrite 1 mask 7` and its blend 12 and
grid twins, a 2.2 s walk).

**The shadow count's self-test.** Its volumes draw under TEST 0x30000 (`rd_shadow_selftest.c`
(`SB_TEST`): every face counts whatever SCENE's depth holds) into SCENE's RGBA8 colour; the game's
are under 0x50000 (`Shadow.c`). Where the profile has the one-draw count the shadow family also
lists those two volume keys (Z ALWAYS, the increment and the decrement;
`rd__enumerate_reachable_shadow`), which the phone made on first use in list 3 at start. The attachment read ([RENDER.md](RENDER.md#destination-reads-and-blend-paths))
computes the GS's blend in the fragment shader from the pixel it reads in rasterization order, so
where the device has that read the blend, the write mask and DATE can be shader data instead of
key fields (`port/render/rd_uber.c`, its header comment; `g_rd.uber`, `rd__uber_on`):

| field | under GS state as data | where |
| --- | --- | --- |
| blend path | every pass of a planned draw that the read can draw goes through the `_dstatt` twin with blending off (`RD_BLEND_COUNT`): the LERPs and the forms the exact blend took, and also the premultiplied adds and subtracts and the passes without GS blending (`RdDrawPass.csOnly`: the ALPHA register `0x00`, which is Cs); one pipeline draws every ALPHA register | `port/render/rd_dst_read.c` (`rd__expand_dst`, `uberPass`) |
| write mask | in DrawCB's WMASK bits (the shader writes a masked channel's Cd back, a UNORM8 texel read and written back being the same byte); the cache's key takes the mask `0xF` | `rd__uber_normalise`, `port/render/rd_pipeline.c` (`drawKey`) |
| DATE | tested against the alpha the fragment reads (`ICO_DF_DATE_DST`, `port/shaders/common.hlsli` (`GS_DATE_SNAP`, `gs_dst_date_snap`)); no R8 snapshot and no stencil bit are taken (`rd__uber_date`, `port/render/rd_replay.c` (`prepareDrawEx`)), so the DATE stencil keys and their setup keys are gone | `rd__uber_date` |
| two-pass blend | not used for these passes (nothing to split: blending is off); `nodual` 0 | `rd__expand_no_dual` skips a key with a read |

What stays in the key: the Z test and Z write, the formats, the topology and PRIM.AA1, the vertex
shader, the pixel shader's family (now with `vu_np_ps`'s twin, `vu_np_ps_dstatt`, so the pixel diet
at a fractional scale stays: `port/render/rd_pixel_plan.c` (`rd__vu_pixel_fs`, `rd__uber_np`)), the
shadow volumes' stencil. What keeps the fixed-function blend: the MSAA epoch's passes (a
multisampled attachment is not read), targets other than RGBA8, the presentation overlay
(`port/render/rd_replay.c` draws it on the output with its own plan), the post and blit keys, the
shadow counts. The fx plan's own forms of a sprite that reads the destination are not drawn under
an exact blend (`port/render/rd_fx_plan.c` (`rd__fx_plan_draw`)), so the set leaves them out
(`rd__fx_enumerate`); the fades fold is off under it (the reduction's sprite draws through the
read, whose twin has no fold: `rd: fades fold: off under GS state as shader data ...`, once).

**The start-up set.** `rd__enumerate_reachable` takes each key the families list and adds its
image through the read alone, normalised, the DATE stencil keys left out, the keys the read never
draws as the two-pass fallback makes them, and the overlay's plain keys
(`port/render/rd_dst_read.c` (`rd__enumerate_uber`); `rd__enumerate_reachable_overlay`). ctest
`rd_state` `testUberSet` holds the tiler profile's set at the phone's fractional scale (the
pre-pass off) to at least four times fewer keys than the fixed-function one, with no DATE key, a
VU family a third or less of the fixed-function one's, and no plain twin of a key the read draws;
it prints the counts and the families (the plan's estimate: about 150 to 165 of 1193, the VU
vertex shaders compiled 6 times each instead of 48, their 24 keys and the 24 DATE twins). The start-up set's log line is followed by
`rd: pipelines: GS state as shader data (the colour read in order: N keys in the start-up set
instead of M)`, M the fixed-function set the device would have made (`rd__uber_set_line`), and the
window's 10 s summary carries the pipelines made so far by family, `pipelines made by family: 2D a,
VU b, VU point c, shadow d, fog e, CRT f, fx g, DATE h (n, GS state as shader data)`
(`rd__uber_perf_note`, `RdPerfRecord.pipeFamily`; `port/render/rd_perf.c`
(`rd_perf_backend_text`)).

**The pass.** Every pass on an RGBA8 target made for the read begins readable
(`rd__exact_readable`, `rd__pass_wants_readable`) and takes a pending clear as its load op, as
DATE's stencil path does, with a COLOR local-read barrier right after the pass begins so the
first read sees the load op's write (`port/render/rd_replay.c` (`beginPass`);
[RHI.md](RHI.md#local-read)); the exact mode's own clear pass is not needed there.

**Merges.** The VU draw merge holds draws through the read where it is GS state as data
(`port/render/rd_replay_vu.c` (`vuMergeDst`)): a merged draw is one draw whose triangles keep their
order, and the read in rasterization order sees each earlier one's writes, as the draws one by one
would. The merge still needs one pipeline and one DrawCB, and the DrawCB carries the ALPHA
register, so draws of different blends do not merge. The fused scissor pair
([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-fused-scissor-pair); `vuFuseVerdict`) takes every blend
form under it: the cut and the kick have one key, the cut's DrawCB its ALPHA register and the
kick's own `0x00`; the kick drawn under the cut's DrawCB gives its marked pixels the register
`0x00` in the shader (`port/shaders/vu_common.hlsli` (`VU_RETURN` under `ICO_DST_MODE` 2), the mark
`VuVSOut.r.w` or the no-planes twin's negative plane), so its colour is Cs and its alpha, mask,
PABE and FBA the pixel's own: what the kick's own pass stores, byte for byte.

**The device rule.** Vulkan with `RhiLimits.rasterOrderAttachment` and local read: never on
Qualcomm's own driver (issue 89), on PowerVR (no rasterization order: `rhi_vk: destination read:
interlock no, rasterization-order attachment read no; local read yes`) or under MoltenVK; Mali
(`VK_ARM_rasterization_order_attachment_access`) and Turnip have it; lavapipe has it, which the
tests use. `[dev] uber` (`ICO_RD_UBER`): `auto` (the default) takes it on a tile-based GPU that has
the read, `on` on any device that has it, `off` never; a `[dev] blend` other than `attachment` or
`exact` turns it off, the start-up retry's `fixed` among them (`port/platform/startup_retry.h`). It
asks for the attachment read itself (`rd__dst_asked_env`), so the read's layout and twins are made
and `rd: blend: exact (attachment read) (GS state as shader data)` is the blend line. The log:
`rd: pipelines: GS state as shader data: on (raster-order read): the blend, the write mask and
DATE in the shader, the colour read in rasterization order`, or `off (<why>)` (`rd__uber_init`),
and the profile line ends with `GS state as shader data (raster-order read)` or `GS state in the
pipelines (shader data off (<why>))`. The blend's self-test checks the read before the set
([RENDER.md](RENDER.md#destination-reads-and-blend-paths)): under `auto` a failure turns GS state
as data off (`rd__uber_off`) and the fixed-function blend draws; DATE's self-test checks the
per-primitive test, a failure leaving DATE to the stencil bit or the snapshot (`g_rd.uberDate`).
`ICO_RD_UBER_DATE=0` and `ICO_RD_UBER_PREMUL=0` are the A/B switches against the attachment read's
own pictures (DATE through the snapshot or the bit, the premultiplied forms on the blender): with
both the collapse's machinery must draw the attachment read's bytes, which a `rd_replay_tool
--pass-dump` of the corpus under `ICO_RD_UBER=on` against baselines made with
`ICO_RD_BLEND=attachment ICO_RD_UBER=off` checks ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).

The picture is the attachment exact mode's, the GS's integer arithmetic: the blend equals
`ICO_RD_BLEND=attachment` ([DIVERGENCES.md](DIVERGENCES.md) VD-7 does not apply), the premultiplied
subtracts too (the GS's floor, one level under the blender's where `Cs F` is not a multiple of
128), and DATE is the GS's, primitive by primitive (VD-17 does not apply). No device here has the
read in hardware: the user's Turnip tablet or a Mali reporter's log is the device proof.

### The start-up set

The box blit with the effects depth (two keys, one per output format, `rd__present_depth_key`) is in the set only where `RhiLimits.depth32Attachment` is true; a device that cannot draw to a D32F depth buffer has neither the keys nor the effects depth (`rd: present: no effects depth (the device cannot draw to a D32F depth buffer)`, once).

The present's box average (`aa_reduce_ps`, one RGBA8 key, `rd_present.c` (`reduceInto`)) is in the set on every device: supersampling, a scaled scene with more texels than the box has pixels on both axes and Downsampling all draw it ([DISPLAY.md](DISPLAY.md)) (`rd_present`'s `checkReduceKey`).

The window builds the reachable set with `rd_precreate_pipelines` before the first frame, behind the
start-up screen, unless one of the rules below skips it (`port/platform/window_host.c`
(`ico_window_open`)). Without the set the window calls `rd_pipelines_lazy(why)` instead, which logs
`rd: pipelines: no start-up set <why>: pipelines are made as the game needs them`; there is no
start-up screen (the title comes at once), and each pipeline is made when a frame first needs it,
on the thread that draws, the frame waiting for its compile (`rd__get_pipeline`). The summary line's
renderer part ends with `pipelines the start-up set`, `pipelines the careful start-up set`,
`pipelines the careful start-up set, stopped at its budget (the rest on first use)` or `pipelines
on first use (no start-up set)` (`rd_summary_text`).

The window's choice (`port/platform/window_host.c` (`ico_window_open`)), once the device is made:
the careful set on a device of the table (`RhiLimits.carefulPipelines`), by `[dev] pipeline_set =
"careful"`, or for one start after a full set's end ([PLATFORM.md](PLATFORM.md#diagnostics));
no set for the rows of the table below; else the full set. For the careful set it calls
`rd_set_pipeline_policy(rd_pipeline_policy_careful())` before `rd_precreate_pipelines` (the memory
function, `rd_set_memory_fn(ico_diag_process_memory)`, is set before `rd_init`) and logs one line
with the policy's values, `<why>` as in the table's lines:

    window: pipelines: careful start-up set on this device (Adreno 740, issues 93 and 107): batches of 8 on 1 thread, the cache written after each, stopped past 128 MB of growth

(`by the settings ([dev] pipeline_set = "careful")`, `(the previous start ended while preparing
them; for this start only)`). `[dev] pipeline_set = "full"` does not override the table (`window:
[dev] pipeline_set = "full" does not override the device table (<reason>): the careful set`). The
start-up activity file then says `set: careful`, so an end inside the careful set makes the next
start lazy rather than careful again. When the budget stops the set (`rd_pipelines_lazy_on`), the
rest of the run is recorded as a lazy run's for that start only
(`ico_diag_startup_activity_budget_stop`); when the system's low-memory signal stopped it, as a lazy
run whose next start makes no set ([PLATFORM.md](PLATFORM.md#diagnostics)). Sprite planes last,
`[dev] pipeline_skip` (byte-identical): [WORK_BUFFERS.md](WORK_BUFFERS.md#without-the-sprite-planes).

With the set made, a pipeline a draw needs later is a key the set did not hold, made on the thread
that draws while the replay waits; on a driver that takes seconds a pipeline (up to 3.4 s for a VU
one on a PowerVR DXT-48-1536) that is a stall of the picture and of the game behind it. Each such
key is logged once (the cache keeps it), the first 8 and then every 100th, with the draw list the
replay was in, the whole key (the fields of the batch notes, then the Z test, the Z write, the
colour mask, the target format, the topology, the stencil mode, PRIM.AA1 and the two-pass blend's
flag) and the time it took (`port/render/rd_pipeline.c` (`firstUseLine`)):

    rd: pipelines: made on first use in list 0: program 0 blend 4 vs 15 fs 5 fmt 1/8 msaa 0 dst 0 ext 1, ztst <z> zwrite <w> mask <m> target <t> prim <p> stencil <s> aa1 <a> nodual <n> (vu_tri_vs + vu_ps) in 769.8 ms (not in the start-up set; 2 so far)

`no draw list (the present, a check)` stands for the list outside the replay's lists. The key is the
one `rd__enumerate_reachable` would need to hold. The line and its count are the pipelines made; a
creation that fails has its own line ("The set against the pool" in [The start-up set](#the-start-up-set)). On a PowerVR DXT-48-1536 a `vu_tri_vs`
pipeline made so takes about 750 ms (770 and 745 ms for the two keys of a room change in a phone's
log that named only the fields before the comma) and a program entry's up to 3.4 s, a visible
stall each; a 2D key the driver has cached, under 1 ms. That log's keys matched enumerated
keys in every field it printed, so they differed in one it did not: two `vu_tri_vs` keys (blend 4
and 12, ABE off) in list 0, and a UI-space sprite (`program 9 blend 4`, `sprite_ui_vs +
sprite_ps`, with the depth target) in list 11. For the two VU keys that field is the Z test or the
colour mask, the only two unprinted fields a VU draw's state changes: the Z write is enumerated
both ways, the target format follows `fmt 1`, the topology, stencil mode and PRIM.AA1 are fixed
for a VU draw (`vuDraw` plans triangles without AA1), and the two-pass blend's flag cannot touch
blend 12, which does not blend. The set now also holds the pre-pass's keys under
TEST Z ALWAYS (the one Z test a VU draw is seen to get that the set lacked, below; not confirmed
as those keys' state) and UI-space sprites under the TEST Z GEQUAL every list's head writes, with
the depth target (`ico2/seki/src/GsBase.c` (`gsb_setNormalReg`): a 2D draw of list 11 before
the list's own TEST write takes it); the whole key in the line names the state if one is still
missing. It later named colour mask 7 on program-entry keys of the same blends: a material TEST's
RGB_ONLY half ("A material's own TEST", above), now in the set. The program entries' keys before the pre-pass lacked the same states: every one of them
has its `vu_main_vs` or `vu_particle_main_vs` twin in the set (`rd_state_test`
`testEnumerationPrepass`). A pipeline made in
the vertex pre-pass is `in the vertex pre-pass (before list 0)` (`rd__vu_prepass_running`). A
replay whose walk (the vertex pre-pass and the recording of its lists, the `walk` phase of the
[slow replay line](DISPLAY.md#the-stats-lines)) takes over 500 ms gets one line, the first 8 and
then every 100th, saying where that time went: the pipelines made at first use, the textures and
buffers made (the RHI's `createNs`: their memory and objects; [RHI.md](RHI.md#the-interface)), each
counted over the walk alone, from `RD_PERF_TS_PREPASS` to the present recorded
(`rd__perf_walk_end`), and the rest. The lists' timestamps also give the list in the first-use line
(`port/render/rd_perf.c` (`walkBreakdown`, `rd__replay_list`)):

    rd: slow walk 2639.5 ms (replay 3402, frame 2431): pipelines made 2630.1 ms (1), textures and buffers made 3.2 ms (4 and 0), the rest 6.2 ms (1 so far)

(the numbers in both lines show the fields, not a measurement). Texture decodes run in the game's
step (the `slow step` line's `texture decodes`) and the replay's texture and mesh uploads in its
`upload` phase, before the walk, so neither is in it.

| the set is skipped | `<why>` in the line |
| --- | --- |
| the system reported low memory during the set, under any policy (below): the rest of the set | `(memory budget: the system reported low memory (running critical) after <i> of <n> keys; the rest is made as the game needs it)` |
| the set under a policy with a memory budget (below) grew the process past it, or a batch took over 10 s: the rest of the set | `(memory budget: the process grew <g> MB, from <a> to <b> MB, past the policy's <m> MB, after <i> of <n> keys)` or `(memory budget: a batch of <k> took <t> s, past 10 s; ...)` |
| `[dev] pipeline_set = "lazy"` (`ICO_PIPELINE_SET=lazy`, [CONFIG.md](CONFIG.md#ico-pcini)) | `by the settings ([dev] pipeline_set = "lazy")` |
| the previous start, of any version, ended without a word in the foreground while a pipeline of its careful set was being made (its activity file says `set: careful`), the first such end in a row: this start only, nothing written (after a full set's end the next start is careful instead, above; [PLATFORM.md](PLATFORM.md#diagnostics)) | `(the previous start ended while preparing them in the careful set; for this start only)` |
| such an end, whatever the set, the second in a row: the setting is written once the device is made ([PLATFORM.md](PLATFORM.md#diagnostics)) | `(the previous start ended while preparing them; [dev] pipeline_set = "lazy" from now on)` |
| the last run, of the same version, ran without the set and ended, or exited from it | `(the last run made them that way)` |
| the same, its set having stopped at the system's low-memory signal (its activity file's `set: stopped at the system's low-memory signal`) | `(the previous start's set stopped when the system ran low on memory)` |
| the crash order's `lazy` step | `(a restart after a graphics driver crash)` |

**The set against the pool.** The set's size depends on the device's profile and the scale: 446
keys at Original 4:3 with the fixed-function blend and every state static (the pre-pass on); the
tile-based profile ([The device profile](RENDER.md#the-device-profile)) adds the DATE, fog and fold keys, the
fx plan's 31 (`rd_fx_plan.c` (`rd__fx_enumerate`): under six depth states the alpha-only key, three
`fx_frame_ps` keys and, with local read, one `fx_local_ps` key, and the alpha-only key once more with
the depth attached under Z ALWAYS without Z write, for cleanUpFB's first sprite in the fog's pass) and,
with the pre-pass off (by the settings or after a failed self-test), the eleven program entries under every Z test, Z write and blend
(`rd_mesh.c` (`enumerateVuDraws`)); without dynamic state those fields stay in every key; and at a
fractional Enhanced scale every `vu_ps` key has its `vu_np_ps` twin (`rd_pixel_plan.c`
(`rd__vu_pixel_fs_enum`)). All four together give the largest set a device meets, 1153 keys on an
Adreno 710 at 2340x1080 (Custom, which maps to Enhanced: 2.8125 x 2.41071). Every set fits the
cache and so the RHI's pool: `rd_state_test` `testTilerSet` counts the tile profile's sets
(static, dynamic depth, dynamic depth and blend) and `testTilerFracSet` the tile class as
`rd_profile.c` makes it with local read (the alpha plan, the fx plan and its local read on; module
and layout stand-ins where the test has no device), the pre-pass off, at 2x (977 keys) and at
2.8125 x 2.41071 (1153), each under `RD_PIPELINE_REACHABLE_MAX` and inside `RHI_PIPELINE_POOL_MAX`
less `RHI_PIPELINE_POOL_HEADROOM`; `rd_pixel_test` `testPipelines` counts the same configuration on
a device with both blends (1153, and 1260 with the two-pass fallback), and the same configuration
with the pre-pass on, the default on a tile-based GPU (issue 179): the main shaders'
and the fifteen point keys in place of the program entries', so fewer keys and a smaller VU family
(the test prints both sets' totals and VU and VU point families and checks the pre-pass's is the
smaller; the Mali-G52 log of issue 179, pre-pass off, has VU 242). On a tile-based GPU the
aura's read in two passes (`port/render/rd_aura_box.c` (`rd__aura_box_keys`), `[dev] aura_box`)
adds two fx keys while it is on, `fx_aura_rows_ps` into the RGBA16_UINT ROWBOX and
`fx_aura_sum_ps` (the aura sprite's own key, Z ALWAYS, no depth, mask whole): 1155 on that
Adreno 710; the tests' counts keep it off (no device, or a desktop device whose profile leaves it
off). The set's first line names it against the pool (`rd_precreate_pipelines`):

    rd: pipelines: the start-up set's 1153 keys (pool 4192: the cache's 4096 and 96 for the pipelines made outside it; 0 already made)

and the next line splits the set by family (`port/render/rd_pipeline.c` (`rd__pipe_key_family`), by what
a key holds, not by the enumerator that listed it: a DATE twin of a VU or screen key counts as DATE,
the water's states as 2D):

    rd: pipelines: the start-up set by family: 2D <n>, VU <n>, VU point <n>, shadow <n>, fog <n>, CRT <n>, fx <n>, DATE <n>; font_sheet_ps: the menus' text at <s>x in the set|baked (not in the set), the overlay's at <o>x in the set|baked (not in the set)[ ([dev] sheet_shader)]

(a key is in one family, the first that holds of DATE, VU point, VU,
shadow, fog, CRT, fx, 2D).

**The sheet shader's keys.** `font_sheet_ps` (the menus' text above the bake's scales, 70k words
with its two-pass twin `font_sheet_ps_nodual`) is in the set only where a strip of a scale the bake
does not take can be drawn (`port/render/rd_tex.c` (`rd__sheet_shader_in_scene`,
`rd__sheet_shader_in_overlay`); issue 188: a PowerVR B-Series driver did not return from compiling
it, on a phone whose menus are baked): the frame's 8 text keys when the menus' scale
(`rd__sheet_scale_for` of the scene's texels a GS line, `port/ui/menu_font.c` (`mfScale`)'s
rounding: 2.11 is 2, 2.41 is 3) is not baked (`rd__sheet_bake_scale`: 1, and 2 on the tile-based
classes), the overlay's 4 when the output's height / 448 (the overlay's box is at most the output's
height) is not, and all of them with `[dev] sheet_shader`; `font_sheet_baked_ps`'s 10 stay in every
set. The family line's last part says which way each went, with the scales. A scale reached later
(Apply's resolution: the display change's set, `rd__display_pipelines`; a larger window: first use)
makes the key then. At 1080 output lines the overlay's popups draw at 3x, so a 1080p phone keeps
the overlay's 4 keys while its menus at Resolution Auto (2x) are baked. `rd_state_test`
`testSheetSet` asserts the counts on a tile-based and a desktop profile at 1x, 2x and 3x and at
448, 720, 1008 and 1080 output lines, and the line's words. The blend self-test, which the window runs before the set behind the start-up
screen, draws its glyph through `font_sheet_baked_ps` unless `font_sheet_ps` is what it checks
([RENDER.md](RENDER.md#destination-reads-and-blend-paths), **The blend self-test**), so no start
compiles `font_sheet_ps` where the set leaves it out. A creation that does not return before the
game starts ends the run after 10 s, 60 s on a driver whose compiles run one at a time, the time
held while the process grows ([PLATFORM.md](PLATFORM.md#diagnostics), **The pipeline limit**). `rd_state_test` `testTilerSet` asserts the families sum to the set's
count and to the count without local read, and prints them for each mode.
`rd_replay_tool --keys` compares the set with what a replay asks for: the keys the replay names
(`rd__pipeline_gather`, rounds as the loss bisect's `bisectPipelines`), one line each with its
family and `in set` or `MISSING`, and `exit 1` on a miss (ctest `rd_replay_tool_keys`, TESTING.md).
A key the set lacks costs one first-use stall, logged by `firstUseLine`, never a wrong picture.

The DATE twins of the VU keys stay for every VU entry. The TEST with the DATE bits that is a list's
default is list 4's, `RD_TEST_DATE1_Z_GEQUAL` (0x5C000), which only
`ico2/seki/src/GsBase.c` (`gsb_setSpecularReg`, called for list 4 in the list setup) writes; but
list 4 hosts the specular and the reflection passes (the list table above) and the C source has no
caller of `mc_SetMicroCode`, so the programs that draw there cannot be read from the game's code,
and with the vertex pre-pass the drawing keys carry `vu_main_vs` and `RD_PROG_PRELIT` whatever the
program (`port/render/rd_mesh.c` (`enumerateVuDraws`)), so a key cannot say "specular only". A
fold to the specular entries could be made only with the pre-pass off. `rd_replay_tool --keys` lists the VU keys with a DATE state a dump asks for.
The summary line's renderer part ends with ` (pool 4192, cache 4096)` after the set's words
(`rd_summary_text`). A creation that fails, in the set or at first use, is logged with the key, when
it was asked for and why, the first 8 and then every 100th with the count so far
(`port/render/rd_pipeline.c` (`keepPipeline`, `failReason`)):

    rd: pipeline creation failed on first use in list 0 (program 0 blend 4 vs 4 fs 5 fmt 1/8): the RHI's pipeline pool is full (4192 of 4192 alive); draws with this key are skipped (1 failed so far)

`in the start-up set` or `in a replay's gathered keys` in place of `on first use in list 0`; the
reason is the RHI's, read from `RhiStats` around the creation (`pipelinePoolFull`,
`pipelineFailures`, `pipelineLastError`): `the RHI's pipeline pool is full (<live> of <size>
alive)`, `the driver's error VkResult <n> (<name>)` (the signed value, named for the
results a pipeline's creation can return, `rd_pipeline.c` (`vkResultName`)) or `the driver's error
HRESULT 0x<8 hex digits>` (Direct3D 12), `the RHI refused its
description (its line above)`, or, with no RHI call, `its description was not made (a shader
module not loaded)`; a batch of the set shares one reason, and a batch that met both says `the
batch met a full pipeline pool (<n> times) and the driver's error ...`. A key that failed is
never created again in the run: it stays in the cache as an entry without a pipeline (`pipe.id`
0, no slot of the RHI's pool), found by the same hash look-up as a made key, so its draws, the set
and a replay's gathered keys find it in O(1) and skip it (`port/render/rd_pipeline.c`
(`keepPipeline`, `keyFailed`)); failed entries take cache room, and `rd__pipeline_count` counts
the made ones only. Past `RD_PIPELINE_FAIL_MAX` (1024, a quarter of the cache) failed entries,
`rd: pipelines: <n> keys failed, more than the 1024 remembered: no pipeline is created until the
cache is cleared (new keys are not drawn)` stops every creation until `rd__pipeline_cache_clear`,
so no key is retried on its every draw. A key the driver refused (not the pool) is not retried
after a scene change either, though a transient refusal (the driver out of memory while the
system takes pages back) then loses that key's draws for the run: no scene change reaches
`rd_pipeline.c` (the cut is the replayed frame's, `RdFrame.cut`, read by `rd_interp.c` and
`rd_core.c`), so a retry needs a hook called from the replay (`rd_replay.c`) that turns the
driver's failed entries back into misses once, which this file cannot add alone. The RHI's own lines and the
10 s numbers are in [RHI.md](RHI.md#the-pipeline-pool).

**The policy.** `rd_set_pipeline_policy(&p)` before `rd_precreate_pipelines` sets how the set is
made (`port/render/rd.h` (`RdPipelineSetPolicy`); `port/render/rd_pipeline.c` (`createKeys`)); zero
fields are the defaults. `batch`: keys a batch (`RD_PIPE_BATCH`, 32, and no more); `maxThreads`:
`RhiPipelineBatch.maxThreads`, the workers a batch may use (0: the backend's count,
[RHI.md](RHI.md#the-interface)); `cacheEachBatch`: the pipeline cache written after every batch, on
any platform (`cacheKeptPerBatch`); `memoryBudgetMB`: the process's resident memory is read before
and after every batch (`rd_set_memory_fn`), and once it has grown past the budget over its size
when the set began, or a batch took over `RD_SET_SLOW_BATCH_MS` (10 s), the set stops after that
batch and the rest goes the first-use way (`rd_pipelines_lazy` with the reason in the table
above). `rd_pipeline_policy_careful()` is the careful set: batches of 8, one worker thread, a 128
MB budget, the cache after each batch, the values for a device of `RhiLimits.carefulPipelines`.
A policy other than the defaults is logged once before the set, `rd: pipelines: the start-up set's
policy: batch 8, threads 1, budget 128 MB, cache each batch; with a budget the set stops past it or
after a batch over 10 s, the rest made on first use`, and each batch's line ends with `; policy
batch 8, threads 1, budget 128 MB, cache each batch`. Under any policy, with a low-memory function
set (`rd_set_low_memory_fn`: the window's `ico_diag_low_memory_stop`, on Android only), the set asks
it after every batch but the last and stops there on a yes, before the budget's rules (the growth
rule cannot fire while the system takes the process's pages back), logged once before the set as
`rd: pipelines: the start-up set stops at the system's low-memory signal (asked after each batch),
the rest made on first use`. A batch in flight is not cut short: the careful batches are 8
pipelines on one thread. The same function also says yes once the device's available memory is
under the free-memory floor: on Qualcomm's own driver 17 % of its memory and at most 2048 MB on a
device of more than 8 GB (issue 161's guard against that driver's compiler), on any other driver
the set's need with a margin, at most 512 MB above 8 GB (`port/platform/memory_floor.h`;
[WORK_BUFFERS.md](WORK_BUFFERS.md#without-the-sprite-planes) has the figures). A phone with much
memory reports little of it available while it runs well (1823 to 2647 MB of 15552 MB on a PowerVR
DXT-48-1536): a plain 17 % (2643 MB) stopped that phone's set after 33 of 1193 keys, so the title
waited 13.6 s for the pipelines made on first use, and the 2048 MB cap stopped it again at 1823
MB (a 2.2 s freeze in play). A stop at the floor on another driver than Qualcomm's own is for that
start only, and the rest is finished in the background (next section).

The table's row (`port/rhi/vk/vk_device.c` `kVuPlainOnly`; the fallback rule of the next section
stays on the same device):

| device | match | reason |
| --- | --- | --- |
| Qualcomm Adreno 740 on Qualcomm's own driver (not Turnip) | vendor 0x5143, device 0x43050A01 (what both devices report; not Mesa's other 0x4305xxxx ids: 0x43050A00 the Adreno A32, 0x43050B00 the 740v3, 0x43051401 the Adreno 750) | issue 93 (AYN Odin 2 Portal) and issue 107 (AYN Thor), driver 512.676.53: both logs ended inside the set's 2D keys. The set was then ordered as enumerated: the 327 screen keys, the 176 VU keys, then the 24 shadow, fog, water, CRT and blur keys (527 at Original 4:3 with the fixed-function blend). The Odin 2 Portal stopped at 201 finished (the batch of keys 193 to 224 in flight), its memory falling from 194 MB to 47 MB over 22 s without progress until the system ended the app, with no crash report; the Thor froze the whole device at 307 or later (the batch of keys 289 to 320). No one module explains it (the font sheet's fragment shader, 70,444 words, and the movie decoder's, 47,609 words, were as large in a release that started on the Thor), so on this device the set is made under the careful policy (above) |

**The pipeline cache.** The file is named by the shader set and the pipeline format, not the
build (`pipelines-<key>.vkcache`, `port/render/rd_cache_name.h`; [RHI.md](RHI.md#vulkan)): an
update whose shaders are the same reads the cache the last build wrote, so its start-up set is
made of cache hits and the screen shows for a moment; an update that changed a module starts the
set cold. Without the set the creations are counted into batches: a batch closes at
the window's next pump once no pipeline was made for 0.5 s, or 2 s after its first
(`rd_pipelines_lazy_tick`). The pipeline cache is then written to its file
(`rhi_pipeline_cache_save`, `rhi_vk: pipeline cache: <n> bytes written to <path> in <t> ms`), so a
start the system ends or a device that freezes keeps every batch before it and the next start
compiles only the rest, and the batch's line goes to the log and to the activity file as a
`compiled: ` line ([PLATFORM.md](PLATFORM.md#diagnostics)): `rd: pipelines: compiled lazy batch 3,
pipelines 41-57 of this run (compiler E031.41.03.47): 17 pipelines 0.02-0.61 s, mean 0.14 s, over
1.8 s; memory 210 MB (most 214 MB), pipeline cache 3104 KB; slowest 0.61 s <key> (<modules>)`. In
such a run each first-use pipeline's note goes to the disc before its compile (fsync,
`act_sync_note`), so a freeze inside one still names it. The cache's size at the start is logged
twice: by the backend (`rhi_vk: pipeline cache: <n> bytes from <path>`, or `rhi_vk: pipeline
cache: starting empty (<path> holds none for this device)`) and by `rd_pipelines_lazy` (`rd:
pipelines: the pipeline cache holds <n> KB at the start; it is written after each batch of new
pipelines ...`). On a phone or a tablet (Android, iOS) the start-up set also writes the cache after
a batch that ends 5 s or more after the last write (`RD_SET_SAVE_MS`), and once more at the set's
end by the window, so a phone's set that the system ends keeps most of its work and the next start
compiles only the rest; its batches' lines carry the cache's size (`pipeline cache 3104 KB`), so a
set the system ends shows how the cache grew batch by batch. On a desktop the start-up set writes
the cache once, at its end (the window's write), and its batches' lines say `pipeline cache not
measured`: the measure (`vkGetPipelineCacheData` over the whole cache) and the writes between
batches are kept there to the lazy path and to a device of the table (`cacheKeptPerBatch`). On an
RTX 3090 with a 21 MB cache, a measure after every batch and a write or compare every 5 s took a
warm start from 1.9 s to 16.7 s and 31.2 s, every pipeline a cache hit. The cache file is written
to `<path>.tmp`, flushed to the disk during a run, then moved over the old file in one step
(`vkr_cache_write`), so a freeze mid-write leaves the old cache or the new one, never an empty or
missing file; the exit's write is not flushed, and is skipped when the cache's size is unchanged
([RHI.md](RHI.md#vulkan)).

**The batches' lines.** Every batch of the set is logged as it completes, the 2D ones too: `rd:
pipelines: compiled 2D batch 193-224 of 527 (compiler ...): 32 pipelines 0.02-0.21 s, mean 0.08 s,
0.9 s on 8 threads; memory 185 MB (most 194 MB), pipeline cache not measured; slowest 0.21 s
<key> (<modules>); each (s): ...`; the VU ones as in the next section. The memory is the process's
resident memory and its peak (`rd_set_memory_fn`, `ico_diag_process_memory`; `memory unknown`
where the system does not say), the cache's size `rhi_pipeline_cache_size` (Vulkan's data size, 0
on Direct3D 12) on a phone or a tablet, on the lazy path and on a device of the table (`pipeline
cache 3104 KB`), `not measured` in a desktop's set (the paragraph above). A start the system ended shows whether the memory grew with the batches, and the
next start logs the last one again as `ico_pc: the previous run's last finished batch: <line>`.

### The background finisher

When the free-memory floor (not the system's signal) stopped the start-up set on another driver
than Qualcomm's own, the keys the set had not made are not left to first use: the window calls
`rd_pipelines_background_start(ico_diag_background_memory)` after the set
(`port/platform/window_pipe_finish.c`), and the renderer compiles them in the background
(`port/render/rd_pipeline.c`, the choices in `rd_pipe_finish.h`):

- **the keys**: the set's own keys in its order from where it stopped (`finOffer`), the ones made
  meanwhile on first use, failed or never made (sprite planes off) skipped; a batch is up to
  `RD_FINISH_BATCH` (32) keys, all with a destination read or all without, as the set's
  (`rd__finish_pick`). A display change re-enumerates the new state's reachable set and goes on
  from its start (`finRestart`; `display change: the background set goes on with the new state's
  keys (N of its M not made yet)`).
- **the threads**: `rhi_create_pipelines_begin(descs, n, RD_FINISH_THREADS, true)` makes on the
  calling thread what `rhi_create_pipelines` makes there (pool slots, layouts) and starts 4 worker
  threads at `SDL_THREAD_PRIORITY_LOW` that only call `vkCreateGraphicsPipelines` through the
  internally synchronised cache; `rhi_create_pipelines_end(jobs, false, ...)` takes them once every
  compile has ended ([RHI.md](RHI.md#the-interface)). 4, not the set's 7: on the DXT-48 the set's
  VU batches took the process to 694 MB of compilers' memory on 7 threads, and in play the game and
  render threads keep their cores.
- **when**: after each presenting replay, on the thread that draws (the render thread;
  `rd__pipe_finish_tick` at the end of `rd__replay_frame_here`), one action a picture: a batch in
  flight is taken once done (while some of its compiles have not returned the backend's count says
  so, `RhiGpuProgress.bgPipesLeft`, a read that does not wait behind the call guard for the submit
  thread's job as `rhi_create_pipelines_end` would even to answer "not yet"; only the begin and
  the taking end wait for it, twice a batch); else the memory function is asked (`ico_diag_background_memory`: 2
  when the system's "running critical" came at any time of the run or its "running low" once a
  pipeline was made, then the finisher ends; 1 when MemAvailable is under the floor now, asked again 5 s later,
  `RD_FINISH_FLOOR_MS`; 0 go) and the next batch begins. No batch begins for 1 s
  (`RD_FINISH_HOLD_MS`) after a game step more than 100 ms late (`window_lines.c`
  `window_hold_line`, the step lateness the window measures in `ico_window_pace`) or a picture over
  100 ms less the pipelines made in it (the render thread's own time,
  `rd_pipelines_background_hold`). A batch in flight is never cut.
- **a draw meanwhile**: a key the finisher has not made yet is made at once on the render thread as
  before (the same key compiling in the background is then destroyed when its batch ends); a
  background batch in flight is taken first by any `createKeys` (a replay's gathered keys) and
  destroyed with the cache (`rd__pipeline_cache_clear`: the shutdown, the VU merge's fallback).
- **the cache file**: written by the game thread's `rd_pipelines_lazy_tick` every 30 s while the
  finisher runs (`RD_FINISH_SAVE_MS`; a 39 MB file took 131 ms to write on the DXT-48) and at its
  end.
- **the crash note**: while a batch compiles the activity note is `creating pipelines A-B of M in the
  background: <first> to <last>; <modules>` (the set's batch form, so an end inside it is read as
  one inside the set's batch by the next start: `startup_retry.c`, `pipeline_skip.c`). It stays
  until the batch is taken, which needs a presented picture; the watchdog's pipeline limit counts
  it as a creation only while some of the batch's compiles have not returned, and each one that
  returns is progress (`gpu_hang.c` `ico_pipe_note_creating`, [WORK_BUFFERS.md](WORK_BUFFERS.md)),
  so a movie or a long load over a finished batch does not end the run.

The lines:

    rd: pipelines: the stopped set resumes in the background (N left, keys A-M of M): batches of up to 32 on 4 threads at low priority, one at a time between pictures; the free memory and the system's low-memory signal asked before each, none begun for 1.0 s after a late game step or a picture over 100 ms; a draw that needs a key not made yet still makes it at once
    rd: pipelines: compiled background VU batch 290-321 of 1196 (compiler ...): 32 pipelines ...
    rd: pipelines: the background set waits (N MB available of T MB, floor F MB): asked again in 5 s (K such waits)
    rd: pipelines: the background set is done: N pipelines made in B batches, F failed, X s compiling over Y s since it resumed (H waits for a late step or a slow picture, W for the free memory; P pictures found a batch still compiling without waiting for the submit thread); K keys left to first use
    rd: pipelines: the background set ends at the system's low-memory signal (running low): ...

How long it takes: the DXT-48's set that stopped at 1823 MB stopped after 33 of 1196 keys, 1163
left (2D 362, VU 264, DATE 514 and the small families). Cold (no cache), at the compile times of
that phone's full set (about 0.35
s a 2D or DATE key, 0.74 s a VU key, one thread), that is about 330 s for the 2D-like keys and 195 s
for the VU ones, so about 130 s on 4 threads, 37 batches of 32: one batch every 3.5 s on average,
about 2 s for a 2D batch and 6 s for a VU one, plus the holds. At the brief's one batch of 32 per 2
s the 1163 keys take 37 batches, about 74 s. With the cache warm from an earlier start (the stopped start's
case: the 33 keys of its first batch were cache hits at 0.00 s) every batch is cache hits and the whole
remainder takes seconds.

**A miss's compile on the pipeline threads (not built).** A pipeline a draw needs is made on the
render thread while the game thread waits for the hand-over. Compiling that one pipeline on a
worker with the render thread waiting on it gains nothing for that picture (the same 0.63-0.81 s).
The gain would come from making all of a picture's missing pipelines in one threaded batch before
its walk (in the phone's freeze 3 VU pipelines took 2.2 s one after another, about 0.8 s on 3 threads).
That needs the picture's keys before the walk, and no pass has them: the store plan's scan
(`rd_store_plan.c`) and the pass areas (`rd_pass_area.c`) read the commands' state but build no
`RdPipeKeyInt`; the keys are built by the draw paths as they go (40 `rd__get_pipeline` calls in 12
files: the VU merge and its fallback pairs, the two-pass blend's split, DATE, the shadow count, the
crack filler, the aura box, the tiler's folds), a pipeline asked for only once another was made
(the second pass of a fallback pair) among them. The one exact enumerator is a gathering replay
(`rd__pipeline_gather`, the loss bisect's), a whole walk with nothing drawn, which on a live picture
also runs its copies, clears, uploads and the frame's retained targets (the aura's FEED128, the
blur trail, the store plan) a second time and costs a second walk (3.3-4.2 ms on the phone) on
every picture to save a compile on a few. So it is not built: the finisher takes most misses away
instead, and a miss's key that a background batch is compiling is made again on the spot. In that
freeze the three keys were cache misses although the start before had compiled its whole set into
the same cache file, and that start never went to stage 4: whether they belong to the start-up set (the finisher would
have made them) or are keys outside it (made on first use either way) is not known from the logs.

### The fallback entries

Every device draws the VU programs through one module set, the record path's: `vu_main_vs` and
`vu_particle_main_vs` with `vu_ps` / `vu_texa_ps`, the vertex pre-pass's ten `vu_X_pre_vs` and the
record stages' four point entries ([The vertex pre-pass](#the-vertex-and-triangle-pre-passes)), each with the
region rule's every step (the card test, the tick cameras); there is no per-draw variant and no key
field for one. A draw the record path cannot take (a command past the record, a stage pipeline that
could not be made, a failed self-test, the pre-pass off) draws the old way, through its program's
own entry with the same pixel shaders (`vu_ps`, `vu_texa_ps`). Which module a
program entry's key is made from is decided once a run, at `rd_init` (`port/render/rd_pipeline.c`
(`rd__vu_policy_init`), `g_rd.vsFallback`):

- the extended build `vu_X_vs` elsewhere;
- the plain build `vu_X_plain_vs` (`port/shaders/vu_common.hlsli`, `ICO_VU_PLAIN`: no card test, no
  tick cameras) on a device of the table (`RhiLimits.vuFallbackPlain`, `kVuPlainOnly` in
  `port/rhi/vk/vk_device.c`), under Turnip (issue 163, [SHADERS.md](SHADERS.md#the-variants)), with `[dev] vu_set = plain` (`ICO_RD_VU_SET=plain`, `ICO_RD_VU_PLAIN=1`) or `rd_set_vu_plain_only`; only the
  ten mesh entries have it (the particles' `vu_particle_vs` is one build), and its modules are made
  only then. `[dev] vu_set = extended` (`ICO_RD_VU_EXTENDED=1`) gives the extended build whatever the others say.

The program entries' keys are in the start-up set only with the pre-pass off (then every VU draw is
the old way: the eleven entries with the legacy pixel shaders, 16 keys each, 176); with it they are
made on first use, by the fallback alone, and the record path's modules (the main shaders' keys,
the ten pre-pass keys and the five stage keys) are in the set instead (`rd_mesh.c`
(`rd__enumerate_reachable_vu`)). The pre-pass is on on every device the settings and the colour
bytes allow, tile-based GPUs included, unless its start-up self-test fails, so the
set is made after the self-test's verdict. The set at Original 4:3 with the fixed-function blend and
every state static is the 2D keys, `vu_main_vs`'s 32 and `vu_particle_main_vs`'s 32 (TEST Z
GEQUAL and Z ALWAYS, Z write on and off, four blends, `vu_ps` and `vu_texa_ps`; the particles' keys
carry `RD_PROG_PARTICLE`, the meshes' `RD_PROG_PRELIT`), the ten pre-pass keys and the five stage
keys (`rd__vu_stage_key`: the rule, the tick test under colour mask R and under G, the colour planes,
the particles' stage): the 2D part + 32 + 32 + 10 + 5, 446 with a 2D part of 367 (the VU part is
79 whatever the 2D part; `rd_state_test` prints both); with dynamic depth ([Pipelines](#pipelines)) the two drawing shaders' keys fall to 8 each,
with dynamic depth and blend to 2 each, the point keys staying 15 (`rd_state_test`
`testEnumerationPrepass`, `testDynamicNormalise` print each set with its 2D part).

The table:

| device | match | reason |
| --- | --- | --- |
| Qualcomm Adreno 740 on Qualcomm's own driver (not Turnip) | vendor 0x5143, device 0x43050A01 (what both devices report; not Mesa's other 0x4305xxxx ids: 0x43050A00 the Adreno A32, 0x43050B00 the 740v3, 0x43051401 the Adreno 750) | issue 107 (AYN Thor) and issue 93 (AYN Odin 2 Portal), driver 512.676.53, compiler E031.41.03.47: the whole device froze, needing a reboot, while the driver compiled the first pipelines with a VU vertex shader of 46,470 words and up; a set of at most 32,709 words built ([SHADERS.md](SHADERS.md#the-envelope)) |

The log at `rd_init`: `rd: device compiler: Compiler Version: E031.41.03.47` (the driver's own line,
Vulkan; `the driver names none` without one), then `rd: VU shaders: <set and why>: ...`, the set
(extended, or plain by the device table, after a lost start or by [dev]) and its reason first
([SHADERS.md](SHADERS.md#the-variants) quotes the forms). While the set is created the heartbeat line ends
with the batch in flight (`| creating pipelines 329-360 of 446: <first key> to <last key>; vertex
vu_main_vs <words> words (<n> instructions); largest fragment vu_texa_ps <n> words; compiler
E031.41.03.47`: each vertex shader's SPIR-V words and instructions, `rd__shader_instructions`, and
on a VU batch the driver's compiler), and each batch with a VU vertex shader is logged as it starts
(`rd: pipelines: compiling 329-360 of 446 (VU): ...`) and as it completes, with every pipeline's
compile time as the worker measured it: `rd: pipelines: compiled VU batch 329-360 of 446 (compiler
E031.41.03.47): 32 pipelines 0.41-2.10 s, mean 1.29 s, 12.3 s on 4 threads; memory 185 MB (most
194 MB), pipeline cache 3104 KB; vu_main_vs <words> words <n> instructions: 16 x 1.10-2.10 s mean
1.62 s; ...; each (s): 1.10 1.22 ...` (`batchTimes`; the 2D batches' lines: [The start-up
set](#the-start-up-set)). The same line without the times goes to the start-up activity file as a
`compiled: ` line ([PLATFORM.md](PLATFORM.md#diagnostics), `rd_set_batch_fn`), so the start after a
freeze logs every batch the frozen one finished. After the set the ten slowest compiles carry their
modules and the time by vertex shader each module's size; a log that stops inside the set names
the module the driver was compiling. The set is ordered: every 2D key first, then the VU keys
(`orderSet`).

### The vertex and triangle pre-passes

A mesh program's vertex shader draws the triangle `kick - 2, kick - 1, kick` at index value
`kick * 4 + corner` ([The stream bounds audit](RENDER.md#the-stream-bounds-audit), `shader_consts.h`
(`ICO_VU_INDEX`)), so no index repeats and the GPU reuses nothing: each of a triangle's three
invocations of a program's own entry evaluates all three of its corners in full (the program's
vertex function: two or three matrix products, the lighting, the divide, the conversions) and the
whole triangle math (`vu_mesh_out`: the region rule with the cut, the two tick cameras' tests, the
card test, the corner rule, the attribute planes, the plane distance, the later bias), and a VU
command drawn twice (the AFAIL split, the two passes without dual-source blending) or as two draws
a batch (the scissor batches' fans and kicks) does it again in each draw. The pre-passes do each
vertex's work once and each triangle's once, before the lists, into a record; the main vertex
shaders read a corner's and its triangle's record back and keep only what differs per corner
(`port/render/rd_replay_vu.c` (`rd__vu_prepass`, `doVuDraw`); `port/shaders/vu_common.hlsli` (the
record stages, `vu_pre_out`, `vu_rec_vertex`, `vu_rec_planes`); `port/shaders/vu_tri.hlsl`,
`port/shaders/vu_particle.hlsl`). The meshes (`RDC_MESH`, `RDC_SKINNED`), the grids (`RDC_GRID`)
and the particles (`RDC_PARTICLES`: a sprite's verdict and planes, six invocations a sprite) take
it. A command the visibility test left out of the replay (an `RDC_NOP` of the shadow frame,
[RENDER.md](RENDER.md#culling)) has no entry, no record rows and no draw: the pre-pass walks the
frame the replay walks.

**The vertex record.** Twelve 32-bit words a vertex, in three planes of `RGBA32_UINT` texels, one
texel a vertex (`shader_consts.h` (`ICO_VU_REC_*`)):

| plane | x | y | z | w |
| --- | --- | --- | --- | --- |
| 0 | `h.x` | `h.y` | `h.z` | `h.w` (the clip w) |
| 1 | `p.x` | `p.y` | GS Z (`gz`) | RGBA, a byte each, R lowest |
| 2 | S | T | Q | bit 0 the region test (`inside`), bit 1 `gsOk`, bits 2..7 the clip-space flags (`vu_clipw`) |

The floats are their bits (`asuint`), so the record holds exactly what the vertex function
computed. GS X and Y are not stored: the readers take them from `p` as `vu_divide` does,
`vu_ftoi4(p.x)`, a multiply by 16 (exact) and a truncation. A vertex's texel is its plane's
`base + v`, 1024 texels a row (`ICO_VU_REC_W`); `VuVtx` carries the clip-space flags (`clipf`)
instead of the clip-space position, which only `vu_clipw` read.

**The triangle record.** A record slot is one texel index in each of the three planes; a
triangle of kick `k` owns slot A at `vu_tri.x + k` and slot B at `vu_tri.y + k`, and under the
tick cameras (`ICO_VU_TICKS`) slot C at `vu_tri.z + k` (`IcoVuCB.tri`; `vu_tri.z` 0xFFFFFFFF
without them); a sprite `i` of a particle command owns slot A at `vu_tri.x + i` and slot B at
`vu_tri.y + i`. 24 words a triangle, 28 with slot C (`vu_common.hlsli` (`VU_TF_*`)):

| slot, plane | x | y | z | w |
| --- | --- | --- | --- | --- |
| A, 0 | the flags | the anchor's RGBA, a byte each, R lowest | `r.x` | `r.y` (the anchor in texels from the target's centre) |
| A, 1 | S across | T across | Q across | the plane distance across |
| A, 2 | S down | T down | Q down | the plane distance down |
| B, 0 | S at the anchor | T | Q | the plane distance at the anchor |
| B, 1 | R across | G across | B across | A across |
| B, 2 | R down | G down | B down | A down |
| C, 0 | tick 0's verdict | tick 1's | | |

The flags: bits 0..1 the region rule at the blended camera up to its tick cameras
(`VU_RULE_EXACT`, `CLIP`, `DROP`, `STRIP`), bits 2..3 the scissor mode's corner code
(`vu_triangle_out_pos`'s 0 culled, 1 the GS position, 2 the cut position) before the draw's
`ICO_VU_CUT_ONLY` / `ICO_VU_KICK_ONLY`, bit 4 the planes hold (scissor and clip mode none), bit 5
a corner has a clip flag (the scissor mode's cut triangles), bit 6 the card test's verdict
(`vu_rule_card`'s drop), bit 7 the chain plane culls it (all three distances below 0), bit 8 the
later bias applies (`vu_later_out`'s condition under the mark), bit 9 the content table hides it
(`VU_TRI_HIDE`), bits 10..11 the anchor (`vu_plane_anchor`), bit 12 every corner has its GS
position, bit 13 no corner's X or Y changed by clip mode none's 16-bit wrap, bit 14 a sprite the
port draws, bit 15 a drop of the triangle is drawn beside the picture (`VU_TF_BESIDE`, the region
rule's side bands: the draw has them, the frame is wider than the picture and the triangle faces
the eye, set where the verdict up to the ticks is a drop, not the card test: a card keeps the drop
beside the picture; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-region-rule),
step 7), bit 16 a drop of the triangle the camera rule draws (`VU_TF_CAMERA`: the draw has
`ICO_VU_CAMERA` and the triangle faces the eye or the draw is two-sided, `vu_drop_fate`; set where
the verdict up to the ticks is a drop, which under the camera rule is every final drop: it has no
card test; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-camera-rule)). A tick's verdict is `vu_tick_shown`'s (0 not shown, 1 through the card test, 2
drawn). The values a plane word holds are those `vu_attr_planes` and `vu_plane_out` (a sprite:
`vu_attr_planes_at`) compute; the anchor's colour is a whole number 0..255 a channel, so its bytes
carry it exactly. Each stage takes whole halves of the gradient basis (`vu_grad_basis`): a lane
nothing reads would be left undefined (`OpUndef`, which the envelope refuses).

**The passes.** After the uploads and the first clears, before list 0, `rd__vu_prepass` walks
the replayed lists. It follows the state block as the walk does (`rd__apply_state` from the
frame's start state, and after a TARGET command the field's half pixel the walk adds to a later
SCENE target set with the screen offset: `port/render/rd_replay.c` (`sceneKeepsHalf`,
`rd__replay_scene_keeps_half`)) and gives each VU command the FrameCB its draws will bind
(`port/render/rd_replay.c` (`rd__replay_vu_frame`, frameRefresh's: the target, its GS size and
offset, the depth's Z scale, the command's camera picture)), which the record stages read; it
sets up what `doVuDraw` binds (the stream in the mesh arena or the ring, VuCB's VU memory, the
bones and card bits in VuBoneCB: `vuSource`, shared by both, kept for the command's draws), finds
a mesh or grid command's vertices (the lowest kick less 2 to the highest of its index list, from
the CPU's list; a grid's every strip) and a particle command's sprites, and numbers the record's
slots: pass V's rows first (each command's vertices, then two slots a sprite), pass T's after
them (two slots a kick of the command's range, three under the tick cameras; a position of the
range that is no kick, a strip start, has its slots and is never read), for the commands in order
while both fit 1024 rows (the draws below batched:
[VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-pre-passs-batches)). Pass V (load don't care, store): a draw
a mesh or grid command, `n`
points through the program's pre-pass entry (`vu_X_pre_vs`, the program's own vertex function on
vertex `first + SV_VertexID`, its outputs as flat `uint4` varyings at the point's texel,
`PointSize` 1), and two draws a particle command through `vu_particle_pre_vs` (`vu_tri.w` 0 slot
A, 1 slot B), all with `vu_pre_ps`, which writes the three targets; the textures go to `COPY_SRC`
and pass V's rows of each plane are copied into the frame slot's record buffer, plane after plane
(`rhi_cmd_copy_texture_to_buffer`, which makes the copy visible to the shaders that read the
buffer: [RHI.md](RHI.md#the-interface)). Pass T (the same textures back to `RENDER_TARGET`, load
don't care: pass V's rows are in the buffer already; its render area its own rows from row `rowsV`,
`RhiRenderPassDesc.area`, so a tile-based GPU neither loads nor stores pass V's rows again, which it
would write back as don't-care contents nobody reads) reads the vertex records from the buffer and
draws, a mesh or grid command, a point a kick through `vu_tri_rule_pre_vs` (slot A), under the
tick cameras two through `vu_tri_tick_pre_vs` with the colour masks R and G (`vu_tri.w` 0 and 1:
slot C's x and y, each written without reading the other), and `vu_tri_planes_pre_vs` (slot B);
its rows are copied after pass V's. A frame slot has its own buffer, so a replay never rewrites
what the GPU may still read for the previous one; the textures are shared, ordered by their
barriers. Both grow by powers of two, from 16 rows (16,384 slots) to 1,024, every slot's buffer at
once, logged as `rd: VU pre-pass: the
record holds 32768 slots (32 rows, 1536 KB a frame slot)` (48 KB a row in each frame slot's buffer
and as much in the three shared textures; a scene of 100,000 vertices and 150,000 to 230,000
triangle slots, issue 179's boat scene, takes 256 or 512 rows, 12 to 24 MB a frame slot; the
1,024-row cap is 48 MB a slot); group 1 binds the record at t4 (to the
vertex and the pixel side), and a 256-byte buffer before there is one (the slot's upload ring when
even that could not be made, logged once: no shader reads t4 then). A record that cannot be made
draws that replay the old way and is asked for once more at the next replay; a second failure in
a row turns the pre-pass off for the run (`rd: VU pre-pass: off for the run: the record (R rows, K
KB a frame slot) could not be made twice; ...`), so no replay asks for it again (`recEnsure`,
`s_recFails`). A command past the record's rows (`rd: VU pre-pass: a frame past the record's 1024
rows: N of its M VU commands are drawn the old way`), one of whose pipelines is missing (its
pre-pass entry's or a record stage's: `... the pipeline of <entry> could not be made ...`), one
whose target has no texture, one whose state at its draw (target, depth, GS size, offset, camera
picture) is not the one the pre-pass's walk made its FrameCB from (`... a command's state at its
draw is not the pre-pass's ...`: the stages and the draw would place its planes half a pixel
apart), or a draw whose main shader's pipeline cannot be made (`... a
pipeline of vu_main_vs could not be made ...`) is drawn the old way, each logged once and counted
(`vuPreFallbacks`); while the loss bisect only names the keys a frame needs
(`rd__pipeline_gathering`) a missing pipeline is not a fallback, so the pre-passes' and the main
shaders' keys are the ones named. The pass ring holds the passes as `VU pre-pass, N vertices into
the record (1024xR)` and `VU triangle pre-pass, N triangle slots into the record (1024xR)`
(`RD_PASSK_VU_PRE`, `RD_PASSK_VU_TRI`), the loss bisect's window numbers them passes 1 and 2, and
the per-pass timing has a pre-pass piece for each ([The pass ring](RENDER.md#the-pass-ring), [The per-pass
timing](RENDER.md#the-per-pass-timing); `port/render/rd_replay_vu.c` (`vuPrepassRun`, `vuSource`,
`vuPreRange`, `vuPreRecord`, `recEnsure`)).

**The record stages.** `vu_tri_rule_pre_vs` reads the three corners (`vu_rec_vertex`) and the
kick's marks and evaluates, by the program's clip mode (`vu_rec.w`), the region rule as
`vu_region_rule` does up to its tick tail (the cut only for a triangle not wholly inside, under
real branches), the card test's condition, the scissor mode's corner code, or clip mode none's
wrap test; then `vu_plane_tri`, `vu_attr_planes` and, with the chain clip plane, `vu_plane_out`
and the three distances. `vu_tri_tick_pre_vs` evaluates `vu_tick_shown` for one tick for a
triangle of a region-mode draw under the tick cameras whose blended verdict can be
`VU_RULE_DROP`, else writes 0 (the main shader reads slot C only where the verdict is
`VU_RULE_DROP`). It cannot read slot A, which the rule stage writes in the same pass, so it tests
the verdict's conditions without the cut: not wholly inside, not `ICO_VU_BEHIND_EYE`, and not under
the card test (`ICO_VU_EXTENDED`, `vu_strip.y`) with every corner's `h.w > 0`, where
`vu_region_rule_cut` gives `VU_RULE_CLIP` or `VU_RULE_STRIP` whatever the cut says (a NaN `h.w`
fails the comparison and keeps the test). Under `ICO_VU_EXTENDED` (every setting but the Original
preset at 4:3 and 1x: `port/render/rd_pipeline.c` (`rd__vu_extended`)) that leaves the tick tests to the triangles with a corner at or behind the eye: the triangles beside the
4:3 picture in front of the eye, about half the visible ones on a wide frame, take none. The cut
itself is not repeated there: it would put the stage over the plain budget
([SHADERS.md](SHADERS.md#the-envelope)) and a second module's cut could round apart from the rule
stage's.
`vu_tri_planes_pre_vs` evaluates `vu_plane_tri`, `vu_attr_planes` and `vu_plane_out` again for
slot B's words. `vu_particle_pre_vs` evaluates `vu_particle` and `vu_particle_vs`'s drop and
planes. Every stage is a point draw of the pre-pass entries' shape (three `RGBA32_UINT` targets,
48 bytes a pixel, no depth, no blend).

**The main shaders.** `vu_main_vs` serves every mesh program at index value `kick * 4 + corner`:
it reads its corner's vertex record (three loads), slot A's first texel (one: the flags) and,
where the blended verdict is `VU_RULE_DROP` under the tick cameras, slot C (one); resolves the rule as
`vu_region_rule`'s tail and `vu_rule_card` do (a tick that drew the triangle clips it, one that
sent it to the card test sends it there, the card test's verdict decides `VU_RULE_STRIP`, a final
drop with `VU_TF_CAMERA` is drawn clipped over the whole frame (the camera rule), a final
drop with `VU_TF_BESIDE` under a draw without `ICO_VU_NO_BESIDE` is drawn clipped beside the
picture with `VuVSOut.plane.y` 1, the side bands' discard's mark), applies the scissor draw's `ICO_VU_CUT_ONLY` / `ICO_VU_KICK_ONLY` to the stored corner code, chooses the
position value by value as `vu_triangle_out_pos` does (`vu_corner_position`: the GS position, the
cut position of `vu_corner_cut_position`, clip mode none's wrapped one), culls past the chain plane and takes its corner's
distance (`vu_plane_distance`, one stream load), and adds the later bias from the three corners'
records where the flag says (`vu_later_bias`). The program's choices come from VuCB as for the
stages: `vu_rec.w` holds the clip mode the program uses (normal_c takes VuCB's, normal_l's code
32/36 the scissor mode or the region test, codes 34 and 38, the cluster and the grids the region
test), whether the chain clip plane and the later mark apply (normal_c and normal_l) and whether
the content table's HIDE mark is tested (normal_c and normal_l); `vu_lo` and `vu_hi` hold the
region test's bounds (the cluster's VU memory 0 and 1, the others 0 and (4094, 4094, 0,
16777214)) (`vuTriRule`). Its output is the programs' entries' `VuVSOut`: the position, the
colour, S, T, Q and the plane distance, interpolated in screen space, and, where the triangle's
planes hold (`exact` and the exact viewport, as `vu_attr_planes` makes them), the planes as flat
varyings: the five other plane texels of slots A and B read once a vertex (`vu_rec_planes`: slot
A's anchor RGBA bytes and `r`, `x1`, `y1`, slot B's `a1`, `x0`, `y0`), the words the stages wrote,
bit for bit. A flat varying stays in the tile on a tile-based GPU; a read by the triangle in every
pixel (six buffer loads a fragment) would cost the fill-heavy scenes most. `vu_particle_main_vs`
draws sprite `vid / 6` from `vu_particle` with slot A's verdict and the sprite's planes the same
way. `vu_ps` and `vu_texa_ps` take `VuVSOut` from either the main shaders or the programs' own
entries and evaluate the planes (`vu_pixel_attrs`); at a fractional scale `vu_np_ps` reads its
first three locations alone (`VuPSIn`, [Screen planes](RENDER.md#screen-planes)). A main shader's pipeline does not depend on
the program, so the key's program is normalised (`RD_PROG_PRELIT`) and the programs share their
pipelines.

**The keys.** With the pre-passes on, the VU keys are those of `vu_main_vs` and
`vu_particle_main_vs` with `vu_ps` and `vu_texa_ps` under the VU states, the ten pre-pass keys
(`rd__vu_pre_key`: the program's `vu_X_pre_vs` with `vu_pre_ps`, three `RGBA32_UINT` targets, no
depth, points, no blend) and the five record stage keys (`rd__vu_stage_key`: the rule, the tick
stage under the masks R and G, the planes and the sprite stage); the program entries' keys (with
the same `vu_ps`, `vu_texa_ps`) are made only for a command drawn the old way.
`[dev] vu_prepass = false` (`ICO_RD_VU_PREPASS=0`, [CONFIG.md](CONFIG.md#ico-pcini)) draws every
VU command the old way, for an A/B on a device.

**The devices.** Only what every device the port runs on has: render passes into colour targets,
point lists, a colour write mask of one channel, vertex and pixel shaders reading a read-only
storage buffer (as the stream already is), and buffer copies (a texture into a buffer: on
Direct3D 11, which has no such copy, the backend's own compute shader, [RHI.md](RHI.md#direct3d-11)).
No compute in the renderer (the RHI has none), no
storage writes from a vertex shader (`vertexPipelineStoresAndAtomics`, which the port never
enables and phones' drivers do not all offer), no vertex texture fetch (`tools/check_spirv.py`'s
vertex-shader opcode set holds no image instruction). `RGBA32_UINT` is a colour attachment and a
sampled format on every Vulkan device (the specification's required format support) and a render
target on Direct3D 12 and Direct3D 11 at feature level 11_0; three targets are inside every device's colour
attachments (at least 4 in Vulkan, 8 on Direct3D 12). The one bound the record meets is Metal's:
a pixel's colour targets may take 32 bytes together on Apple A9 and A10 GPUs, 64 on A11 to A13 and
128 from A14 and M1 (Metal Feature Set Tables, "Maximum implicit image block size, per pixel, per
sample, when using multiple color render targets"); the record's 48 bytes do not fit the first,
so under MoltenVK on an A9 or A10 the pre-pass is off (`RhiLimits.colorBytesPerPixel`;
`port/rhi/rhi_driver_version.c` (`rhi_metal_color_bytes`), from the Metal device's name) and the
log says so once. An Apple GPU name the table does not read is taken as unbounded (the families it
names only grow), logged as `rd: VU pre-pass: the Apple GPU "<name>" is not one the colour-target
table names: ...`; a pipeline Metal then refuses, or a wrong picture, is the start-up self-test's
to catch (below). A tile-based GPU takes the pre-pass as any other device (issue
179: a Mali-G52 drew its boat scene with 9 vertex shader invocations a triangle without it, vertex
bound). Reading the record's passes, copies, barriers and reads, and running them under
synchronisation validation, finds no rule broken; a PowerVR phone's pictures that shook between two
ticks through the record are the device-side fault the self-test's four replays across both frame
slots (below) are made to see: a record read stale or across two pictures fails it and the device
draws the old way. At start the renderer logs `rd: VU pre-pass: on: each mesh and grid vertex and
triangle and each sprite evaluated once before the lists (two passes), 48 bytes a record slot in 3
RGBA32_UINT targets, two slots a triangle (three under the tick cameras); [dev] vu_prepass = false
draws the old way`, or `rd: VU pre-pass: off by the settings ([dev] vu_prepass = false,
ICO_RD_VU_PREPASS=0): ...` or `off on this device (its colour targets hold 32 bytes a pixel
together, the record 48: an Apple A9 or A10 GPU): ...` (`port/render/rd_replay_vu.c`
(`rd__vu_prepass_init`)).

**The self-test.** The pre-passes' pipelines are shapes no other draw of the port has (vertex
shaders writing `PointSize` into a point list, flat `uint4` varyings into three `RGBA32_UINT`
targets under a one-channel mask, texture-to-buffer copies the next pass's vertex shaders and the
main shaders read in the same list, the record read in the pixel shader), so a driver that cannot
make one would leave every VU pipeline to be made on first use (the start-up set holds the main
shaders' keys, not the programs'), and one that miscompiles them would draw the scenery wrong with
no error. `rd_vuprepass_selftest` checks it on the device at start, after the blend's self-test
and before the start-up set (`port/platform/window_host.c`; in a run without the set, after
`rd_pipelines_lazy`): a frame of its own, built four times and never recorded, on SCENE at its real size and scale
without MSAA, draws three meshes made for it (`rd_create_vu_mesh`: a prelit strip through normal_c
code 32, a lit one through normal_l code 32 and a skinned one through cluster code 20 with two
bones, 12 vertices and 8 triangles each, overlapping at other depths, Z GEQUAL with Z write,
untextured) with `vu1_test`'s scene in VU memory, the prelit mesh again through a camera blended
between two ticks' (one vertex of each triangle behind the blended camera's eye, every triangle
inside at tick 1's camera, so each is drawn clipped through the tick stage, in front of the
others; left out where the fallback is the plain variant, which has no tick cameras) and four
particles. Replay k of the four is moved: every mesh 0.25 model units (2 GS pixels) a replay to the
right and the blended camera's x' and y' offsets 0.25 a replay, so each replay's records differ from
the one before. The four are replayed with the pre-passes off (the programs' entries, the
references), then the four with them on back to back as a run draws its pictures, each in the
next frame slot (the record's buffer is one a slot, so they alternate between both); the block is
copied out after each replay and read back after the four. A record that a replay did not write,
read before its copy landed or read from the other slot draws an earlier picture, which one replay
of one frame could not show. Their pipelines are made
first on the worker threads the start-up set uses, named by two replays that draw nothing
(`rd__pipeline_gather`, `rd__pipeline_create_gathered`, as the loss bisect makes its own): the
programs' entries are not in the start-up set with the pre-pass on, and on a cold driver cache a
PowerVR DXT-48-1536 compiles an extended VU program entry in about 1.3 to 3.4 s (its start-up
batches in a log), so in parallel the self-test costs about the slowest one, and next to nothing
once the pipeline cache holds them. It passes when every replay with the pre-passes drew every
command through them (`RdPerfRecord.vuPrePasses` 2, `vuPreDraws` 16, 11 without the tick copy,
`vuPreSprites` 4, `vuPreFallbacks` 0) and each replay's block equals its own reference's byte for
byte. A difference, a
pipeline the pre-passes could not make or a device error turns the pre-pass off for the run, so
the start-up set made next holds the programs' keys:

```
rd: VU pre-pass: on (self-test: prelit, lit, skin, the tick cameras and particles byte for byte over 4 replays in both frame slots, 48 vertices and 4 sprites in 16 pre-pass draws (2 passes, 90 triangle slots), <n> of <m> pixels drawn) on SCENE <w>x<h> at <tw>x<th> texels in <t> ms
rd: VU pre-pass: off (self-test: the pictures differ: in replay <k> of 4 (frame slot <s>), <n> of <m> bytes, the first at texel <x>,<y>: <r>,<g>,<b>,<a> want <r>,<g>,<b>,<a>) on SCENE ...
rd: VU pre-pass: off (self-test: a pipeline could not be made (replay <k> of 4: <p> passes, <d> of <w> pre-pass draws, <s> sprites, <f> draws the old way)) on SCENE ...
rd: VU pre-pass: off (self-test: the device failed in the replays with the pre-pass) on SCENE ...
```

A start whose pre-pass is already off (the settings, the device, the host's
`rd_set_vu_prepass_off`: a restart step or an end in its compile) runs nothing and logs `rd: VU
pre-pass: off (self-test not run)`; a frame it could not build, or one in which neither replay of a pair
drew the meshes, leaves the pre-pass on (`on (self-test not run: ...)`, `on (self-test
inconclusive: ...)`). The summary line carries the verdict (`VU pre-pass on (self-test: ok)`, `VU
pre-pass off (self-test: <reason>)`, [PLATFORM.md](PLATFORM.md#the-summary-line)). A crash in one
of its pipelines is noted as the start-up set's would be and takes the crash order's `noprepass`
step; a start that ended without a word in one of the pre-pass's pipelines or batches makes the
next start draw without it, and the second such end in a row writes `[dev] vu_prepass = false`; a
device loss in a run drawing with it takes the loss order's `lossnoprepass` step first
([PLATFORM.md](PLATFORM.md#diagnostics)) (`port/render/rd_vuprepass_selftest.c`;
`port/render/rd.h` (`rd_vuprepass_selftest`); `ICO_RD_VU_PREPASS_SABOTAGE=1` draws the first
command's vertex records a texel off, `=tri` its triangles' slot A a slot off, `=ticks` the tick
stage's tick 1 at tick 0's camera, each in every replay, `=stale` the record's copies left out from
the second replay on, which only the replays after the first can catch (a self-test of one replay
passes it); [TESTING.md](TESTING.md#environment-switches)).

**Why the picture is the same.** The pre-pass entry calls the program's vertex function, the same
source with the same `precise` operations, so each recorded word is the value the old entry
computed for that corner (the shaders keep the precise flags of `port/shaders/CMakeLists.txt`;
`precise` forbids contraction and reassociation of each operation it marks). The record stages
call the functions the program's entry calls (`vu_region_rule_cut`, `vu_cut_z` and `vu_cut_rows`
as the same branchless chain, `vu_tick_shown`, `vu_strip_card`, `vu_plane_anchor`,
`vu_grad_basis`, `vu_attr_planes`, `vu_plane_out`, `vu_plane_distance`), on the same record bits
(`vu_rec_vertex`), under the same VuCB, VuBoneCB and FrameCB (the command's draws'); splitting the
verdict across the stages computes some pure parts the entry skipped (tick 1 when tick 0 drew it,
the planes of a triangle whose planes do not hold, the tick test for a verdict that is not
`VU_RULE_DROP`), which nothing reads. The record carries the bits through integer targets and a
buffer copy with no float conversion (the anchor's colour as bytes of whole numbers), and the
readers take them back as `asfloat`: the main shader's corner values and the pixel shader's plane
words are the words the entry's corner computed and its flat varyings carried. The one value
recomputed from a record, GS X and Y from `p`, is `vu_divide`'s own operation on the same bits,
for a finite `p`: a NaN `p` (only a garbage stream gives one) converts as each module's
`OpConvertFToS` does, which SPIR-V leaves undefined (Direct3D makes it 0), so two modules may
differ there. The cut's `lerp`s and divide carry no `precise`, so a driver may contract them in
either module: the same exposure the program entries have against each other, which the tests
pin. The ten mesh programs' entries and the pre-pass entries compile to the same SPIR-V with the
record stages in the tree, and both ways draw with the one `vu_ps` (and `vu_texa_ps`) on the same
flat varyings: the main shaders hand over the very words the programs' entries computed and
carried. `vu1_test` draws every case both ways, probe and pictures, and compares them byte for
byte ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md#portshaderstest)); `rd_vustage_ab` compares every
target of the dump corpus both ways at Original 4:3 1x and Enhanced 4:3 2x.

**The saving.** Each vertex function runs once a vertex a command instead of three times an index
of each of the command's draws, and each triangle's verdict and planes once a triangle instead of
three times a draw; a main shader's invocation keeps its corner's loads, the position forms and
the chain plane: by the modules' size (`tools/check_spirv.py --sizes`, instructions) `vu_main_vs`
is 1,408 instructions (5,890 words) against `vu_prelit_vs`'s 10,729 (50,230), the stages 3,600
(rule, 16,515 words), 3,426 (tick, 16,485), 972 (planes, 4,269) and 2,071 (sprite, 8,801), every
one under the plain budget ([SHADERS.md](SHADERS.md#the-variants)). The interpolated outputs drop
from 36 floats to 8 and a flat word. The replay's record counts the pre-passes
(`RdPerfRecord.vuPreVertices`, `vuPreSprites`, `vuTriSlots`, `vuPreDraws`, `vuPrePasses`,
`vuPreFallbacks`, `gpuPrepassMs`); the window's 10 s lines and the replay tool's `--stats` print
them, so a log shows the pre-passes' own GPU time next to the lists' ([Performance
records](RENDER.md#performance-records)).

