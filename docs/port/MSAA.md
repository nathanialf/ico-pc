# MSAA

How the renderer's multisampled anti-aliasing resolves the scene: where the multisampled part of a
frame ends, the rule that turns each pixel's samples into one colour and one depth, why the depth
is the part that matters to the game's later passes, and the lines and tests that show it. The
epoch itself (which passes draw multisampled, the sprites' half-texel move, the per-sample fog) is
[RENDER.md](RENDER.md#the-replay), "The MSAA epoch".

## Files

| File | What it holds |
| --- | --- |
| `port/render/rd_replay.c` | the epoch (the section "the MSAA epoch"; `msaaPass`, `msaaAttach`, `msaaEnd`) and the transition hook that closes it (`rd__transition`) |
| `port/render/rd_msaa.c` | the resolve pass (`rd__msaa_resolve`) and the close line (`rd__msaa_close_note`) |
| `port/render/rd_msaa_shadow.c` | the shadow count drawn on SCENE_MS's own depth and stencil (`rd__msaa_shadow_reset`, `rd__msaa_shadow_pass`, `rd__msaa_shadow_done`) and its self-test (`rd__msaa_shadow_selftest`) |
| `port/shaders/blit.hlsl` | the resolves (`msaa_resolve_ps`, `depth_resolve_ps`) and the depth spread over the samples (`depth_upsample_ps`) |
| `port/render/rd_pipeline.c` | the resolves' keys (`rd__msaa_resolve_key`), in the fog's family of the start-up set (`rd__enumerate_reachable_fog`) |
| `port/render/rd_core.c` | the counts offered (`rd_aa_supported_mask`) |
| `port/render/test/rd_msaa_test.c` | the epoch against the frame without anti-aliasing |
| `port/render/rd_msaa_shadow.cmake` | the shadow count's source and the `rd_msaa` rows under issue 209's profiles |

## Where the epoch closes

The epoch closes at the first command that reads or writes SCENE's own colour or depth outside a
pass into SCENE_MS (`port/render/rd_replay.c` (`msaaPass`, `rd__transition`)), or at the shadow
count's reset (`port/render/rd_msaa_shadow.c`, below). In gameplay that is the reset in list 3:
lists 0 to 2 are anti-aliased, and the fog, the list 5 additive meshes, the glow and the aura draw
on the resolved picture. A frame without a shadow keeps the epoch open to the fog, which then runs
per sample where the device shades per sample.

## The shadow count in the epoch

Shadow.c's count in list 3 is a reset of the stencil, the volumes (INCR and DECR of the stencil's
bits 0-5 where they pass the GEQUAL test against the scene's depth) and the resolve that turns the
stencil into the count's colours ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-count-in-one-draw)).
Under MSAA the reset closes the epoch (`RD_MSAA_CLOSE_COUNTED`: SCENE resolved at the same point of
the frame as before) and the count is drawn on SCENE_MS itself, whose colour the resolve has just
read and nothing reads again in the replay: one pass on SCENE_MS's colour (cleared to 0) and depth
(loaded; its stencil cleared to 0, the reset), the volumes and then the seven stencil-tested draws
in it at the epoch's sample count, then the colour resolve (`rhi_cmd_resolve_color`: each pixel
the mean of its samples) into the count target, which `shadowReduce` box-averages to the GS size
above 1x as before. Each volume is tested at every sample against the depth the scene's own draws
rasterised there; no depth `msaa_resolve_ps` wrote is depth-tested before the count is read, no
target is added (the count uses the epoch's own pair), and no pass sequence is left that only MSAA
makes. Inside a shadow every sample holds one count: the bytes of the frame without
anti-aliasing. On a shadow's edge the pixel takes the mean of its samples' counts, so the edge is
anti-aliased like the scene's ([DIVERGENCES.md](DIVERGENCES.md) VD-22).

Why (issues 209 and 182): the count after the resolve (the fallback below) closes the epoch at
its first volume's pass, `msaa_resolve_ps` writes SCENE's single-sampled depth by the shader (its
load DONT_CARE, the stencil loaded and kept), and the volumes are tested against that depth in a
pass of their own and the count read from SCENE's stencil. Two drivers failed at that point of
the frame and nowhere else: an Adreno 740 under a Turnip build ("PurpleVK" 26.3.0-devel) drew no
character shadow at MSAA 2x or 4x with the rest of the picture right (issue 209), and an RTX 4060
laptop on NVIDIA 610.74 lost the device at the first frame with a shadow (issue 182, with the
stencil clear then recorded as an empty pass of its own between the epoch's last pass and the
resolve; whether the sequence without it runs on that laptop was not reported). On lavapipe, under the validation layer with its synchronisation
checks, the same sequence drew the same shadows with and without MSAA4 under both reporters'
profiles (the main gate dump `maingate/frame-20261104-123603-v10827.rddump` at Enhanced 2x 16:9
with `--aa msaa4`: SHADOW0 equal to the frame without MSAA but at 618 of its 262144 texels, all at
the shadows' edges, by at most 57 levels; the corpus room `lightning/rd-00300.rddump` likewise),
so the device's handling of that sequence is the failing part; which step it is cannot be told
without the device. With the count in the epoch the same dump's SHADOW0 differs from the frame
without MSAA at 731 edge texels by at most 15 levels (SHADOW2 at 47 by at most 2), and every pass
target without MSAA is the same bytes as before.

The count after the resolve stays, as before, when the epoch closed before the reset (a read of
SCENE, no 3D draw yet), the reset is on another depth than SCENE's, the count target is not
SCENE's size and RGBA8, or the self-test below found the count in the epoch wrong on the device; a
line says which (the first 8, then every 100th). A command between the reset and the count that
ends the count's pass begins it again with its colour and stencil loaded (both are stored).

The self-test (`rd__msaa_shadow_selftest`, at start, at a change of the sample count and at a
remake of SCENE, with MSAA asked for and not held off by the fog's path; `port/render/rd_fog_path.c`)
draws a frame of its own: a ground at GS Z 0x40000000 over two thirds of the frame, the clear (Z 0)
elsewhere, then on WORK2 with SCENE's depth a reset and volumes whose front faces lie in front of
the ground and back faces behind it under GEQUAL, over the ground (counts 2 and 1) and over the
clear (count 0). It is replayed without MSAA (the reference), in the epoch and, when that differs,
after the resolve; a way passes when its count equals the reference's at every texel whose 3x3 is
one count there and the reference holds 2, 1 and 0 at its centres. The epoch's way stays when it
passes, else the resolve's when that passes, else the epoch's, with the line saying the shadows may
be missing. Unlike the shadow count's own self-test (Z ALWAYS, single-sampled), it depth-tests the
volumes against a depth the scene drew, under MSAA. `ICO_RD_MSAA_SHADOW=0|1` (the tests' way, no
config key) forces a way; `ICO_RD_MSAA_SHADOW_SABOTAGE=1` clears the epoch's stencil to 1 so its
self-test fails.

The count after the resolve (the fallback) reads SCENE's single-sampled stencil, whichever way it
is resolved (the seven stencil-tested draws or the one draw through the stencil view): the two
give the same bytes with anti-aliasing on or off.

## The resolve rule

A sample is uncovered when its depth equals the depth the epoch cleared SCENE_MS's depth to,
exactly: no draw reached it. The clear's value is recorded when a pass into SCENE_MS begins with
its depth's load op CLEAR (`port/render/rd_replay.c` (`msaaAttach`)) and handed to the shader
bit for bit (`port/render/rd_msaa.c` (`rd__msaa_resolve`)). A pixel of n samples with u uncovered:

- is uncovered when more than half its samples are (2u > n): its colour is the mean of all its
  samples and its depth the smallest of all, which is the clear's;
- otherwise takes the mean colour and the smallest depth of its covered samples. Where every
  sample is covered (a silhouette between two surfaces) that is the mean and smallest of all, so
  edge anti-aliasing stays.

| samples n | uncovered when u is |
| --- | --- |
| 2 | 2 |
| 4 | 3 or 4 |
| 8 | 5 to 8 |

A gap between two parts of the world thinner than half a pixel (the 1/16 pixel crack of
[DIVERGENCES.md](DIVERGENCES.md) VD-20) therefore resolves to the surfaces beside it. The depth is
what matters: the fog reads the clear's depth through its table as entry 0, the full fog colour,
and `ico2/sugipon/src/staticBlur.c` (`cleanUpFB`) marks every texel at the clear's depth as a glow
source (its second sprite at GS Z 1 with GEQUAL, alpha 192). With the smallest of all samples a
pixel with one sample in such a gap took the clear's depth: a line of fog along the seam and a
glow source beside every small opening onto the sky. A gap of half a pixel or more still takes the
clear's depth, as at 1x.

Without a clear of SCENE_MS's depth in the epoch (its depth loaded from SCENE) no sample can be
told apart as uncovered: the rule is off and every pixel takes the mean and the smallest of all
its samples, with a line once a run.

The depth is the smallest because the depth grows with GS Z and the game tests GEQUAL, and the
sample patterns are centred on the pixel centre: on a plane the smallest sample lies at or below
the centre's depth, so a surface the game draws again after the close with GEQUAL passes where it
passed at 1x.

## The crack filler after the resolve

The resolve closes gaps onto the clear thinner than half a pixel, not wider ones, and not a farther
surface seen through a seam: a pixel any of whose samples lies on that surface takes its depth
(the smallest covered sample), and the fog paints it at that surface's deeper level. Above 1x those pixels run along the seams as
dotted lines of mist, as without MSAA before the crack filler (issue 26 with MSAA on: a ledge's
top edge). The crack filler ([RENDER_POST.md](RENDER_POST.md#crack-filling-above-1x),
`port/render/rd_crack_fill.c`) therefore runs on the resolved picture: `doFog`
(`port/render/rd_replay.c`) passes the epoch's state at the fog (`RD_CRACK_MSAA_OPEN`,
`RD_CRACK_MSAA_RESOLVED` or `RD_CRACK_MSAA_NONE`, `port/render/rd_replay.h`), and the fill armed
there is drawn after lists 4-6 (not when an epoch is open at that point either), at the start of
list 7: with an MSAA setting the fill does not wait for the glow's `cleanUpFB` or the aura's paste
([its place](RENDER_POST.md#the-fills-place-after-list-7)). In gameplay the
epoch has closed at the shadow count's pass in list 3, so the filler reads SCENE's single-sampled
colour and depth as `msaa_resolve_ps` wrote them (SCENE's own textures: `depthSampled` is as SCENE
was made) and fills exactly as at the same scale without MSAA. The rules agree: the game clears
SCENE's depth to GS Z 0, which is depth 0.0 (`rd__gs_depth`), the epoch's `clearDepth`, so a pixel
the resolve leaves uncovered (the smallest of all its samples, the clear's) is the filler's
"unwritten" depth 0, and a pixel on a farther surface is the discontinuity form's crack. While the
epoch is still open at the fog (a frame without a shadow, where the fog may run per sample) the
filler is not drawn: its snapshot of SCENE would go through `rd__transition` and close the epoch
before the per-sample fog. Without MSAA and under the Original preset or at 1x nothing changes.

Before the change, on lavapipe with the existing `rd_replay_tool --enhanced --stats`, the corpus
room `lightning/rd-00300.rddump`: at 2x `crack fill on: 1 passes, 104 pixels filled` without
anti-aliasing and `0 passes, 0 pixels filled ... 1 fogged replays above 1x not filled` with
`--aa msaa4` (`not drawn on SCENE at scale 2x2: MSAA is asked for ...`, the epoch closed at list
3); at 4x 324 and 0; `queen/rd-00200.rddump` 19 and 0 at 2x, 66 and 0 at 4x. `--aa msaa2` draws as
off on lavapipe (it offers 4 samples only).

## The integer mean

Each channel's mean is integer: the k samples' 8-bit values summed and `(sum + k / 2) / k`, so
every backend rounds alike (`port/shaders/blit.hlsl` (`msaa_resolve_ps`)). The shader keeps
scalar accumulators only: no array-typed value, which some drivers' compilers fail on
(`tools/check_spirv.py`).

## One resolve a picture

On the count after the resolve (the fallback above), the close records what is open into SCENE_MS first (the pass, a clear pending on SCENE), then
resolves once (`port/render/rd_replay.c` (`msaaEnd`)); `s_ms.closing` keeps a transition of
SCENE's textures during the close from closing it a second time (without the guard every frame
with a shadow resolved twice). A shadow reset pending on SCENE's depth stays pending through the
close (issue 182): the resolve loads and keeps the stencil, and the next pass on the depth, the
volumes' pass, takes the reset as its stencil load op (CLEAR), as it does without anti-aliasing.
Recording the reset first as an empty pass of its own (the count target's colour neither loaded
nor stored, SCENE's depth loaded and stored, its stencil cleared) between the epoch's last pass
and the resolve would be a pass sequence only MSAA makes; it was the last thing recorded before
the three Vulkan device losses of issue 182 (an RTX 4060 laptop, MSAA 2x, NVIDIA 610.74:
each at the first frame with a shadow, `msaa: the epoch closes at list 3 (the shadow count's
pass ...)` the last renderer line). The sequence is valid Vulkan (the validation layer, with
synchronisation checks, is silent on lavapipe at 4x) and the loss was not reproduced without the
device, so removing it is a defensive change: the same bytes (`rd_msaa` `shadowreset`), one pass
fewer, and the pass order the reporter's driver runs without MSAA.

The shadow count after the close reads SCENE's own single-sampled stencil, never SCENE_MS's: the
seven stencil-tested draws test SCENE's depth after the resolve, and the one draw's stencil view
exists for single-sampled textures only (`RHI_ASPECT_STENCIL`, `port/rhi/rhi.h`). Where the view
takes no gather (`RhiLimits.stencilGather` false: Direct3D's `X32_TYPELESS_G8X24_UINT`, on which
the D3D11 debug layer stops any draw whose shader holds a gather, error 416, seen on the WARP run
of `rd_msaa`) the one draw binds `shadow_count_load_ps`, the same entry built with
no gather (`SHADOW_COUNT_LOAD`, `port/shaders/blit.hlsl`).

With both the colour and the depth drawn in the epoch the resolve is one fullscreen pass of
`msaa_resolve_ps` into SCENE's colour and depth (both loads DONT_CARE, the stencil loaded and
kept). The colour alone resolves by the device's resolve (`rhi_cmd_resolve_color`), the depth
alone by `depth_resolve_ps` (the smallest sample). The passes into SCENE_MS do not store its
stencil: the shadow window's stencil is SCENE's own.

Neither resolve shader asks the texture for its sample count: the count is a constant
(`DrawCB.g_param.x`), so the OpenGL translation stays GLSL 4.30 (`texelFetch` on a
`sampler2DMS`). Direct3D 11 and 12 read the multisampled texture through a `Texture2DMS` view
(`port/rhi/d3d11/d3d11_resource.c`, `port/rhi/d3d12/d3d12_pipeline.c`). A backend that cannot
make either module offers no MSAA count, the supersampling ones stay (`port/render/rd_core.c`
(`rd_aa_supported_mask`)).

## Log lines

| line | when |
| --- | --- |
| `msaa: the epoch closes at list L (why): lists 0-(L-1) anti-aliased and list L up to there, the fog and the glow run on the resolved picture (reported once a list and cause)` | the first close at each list for each cause (the shadow count's pass, a read or copy of SCENE, a pass on SCENE with another depth) |
| `msaa: the epoch stays open to the replay's end: every list anti-aliased ... (reported once)` | a replay whose epoch lasted to its end |
| `msaa: the epoch's depth was loaded from the scene, not cleared in it: ... (reported once)` | the rule off |
| `shadow count: under MSAA Nx the count reads the scene's own single-sampled stencil after the epoch's resolve: FORM (reported once a form and count)` | the first count of each form under each sample count (`port/render/rd_shadow_one.c` (`rd__shadow_msaa_note`)); FORM is `the seven stencil-tested draws, the volumes' pass taking the shadow reset as its stencil clear`, `one draw, the stencil four texels a gather`, or `one draw, a texel a load (...)` |
| `shadow count: under MSAA Nx drawn in the epoch on the multisampled scene's own depth and stencil (WxH, N samples): ... (reported once a count)` | the first count drawn in the epoch at each sample count (`port/render/rd_msaa_shadow.c`); the close line then names `a shadow reset: the count drawn after it on the multisampled scene's own depth and stencil` |
| `shadow count: under MSAA Nx after the epoch's resolve in frame F: WHY (the first 8, then every 100th: K so far)` | a count drawn the old way, and why |
| `shadow count: under MSAA Nx drawn in the epoch (self-test: in the epoch equal to the count without anti-aliasing at T texels away from edges; ...) on SCENE WxH at TWxTH texels` | each self-test: the way chosen (`drawn in the epoch`, `drawn after the epoch's resolve`, or `drawn in the epoch, but neither way matched: the shadows may be missing ...`), each way's result, the first texel that differed |
| `shadow count: the epoch's count pass begun again in frame F (...)` | a command between the reset and the count ended its pass (the first 8, then every 100th) |
| `window: msaa: N samples, resolves R per replay, the last closed at list L` | the 10 s summary (`port/platform/window_host.c`); R is 1.0 or below |
| `fog: in the MSAA epoch on the resolved picture (each pixel's smallest covered sample unless more than half its samples were uncovered: ...)` | once a run (`port/render/rd_replay.c` (`doFog`)) |
| `msaa: msaa_resolve_ps was not made on this device ...` | start-up, a module missing |
| `rd: crack fill: on above 1x (...); [dev] crack_fill = false turns it off (drawn after the MSAA resolve)` | the first fill on the resolved picture (`port/render/rd_crack_fill.c`); the 10 s summary adds `; A after the MSAA resolve`, `rd_replay_tool --stats` `; F fills after the MSAA resolve` |
| `rd: crack fill: not drawn on SCENE at scale SXxSY: the MSAA epoch is still open at the fog (...) (reported once)` | a fog inside the open epoch |

## Tests

`rd_msaa` (`port/render/test/rd_msaa_test.c`): `testShadowEpoch` (issue 209: the self-test
passes and keeps the epoch's way, the frame's count is drawn in the epoch, `rd__msaa_shadow_counts`
1, no one draw reads SCENE's stencil, the close at list 3; with `ICO_RD_MSAA_SHADOW=0`, the old
sequence, it fails), `testShadowReset` (one resolve a replay with a
shadow reset, the close at list 3, SCENE byte for byte the frame's without the reset, no stencil
reset pass of its own under MSAA), `testShadowCount` (the count by the seven draws, the one draw's
gathers and its loads, without anti-aliasing and with MSAA4: the forms byte for byte one count,
MSAA4's equal to the count without anti-aliasing away from edges, under MSAA every form the count
in the epoch, gathers only where `RhiLimits.stencilGather`), run again as `rd_msaa_tiler` (issue
209's profile bits on lavapipe: `ICO_RD_BLEND=attachment ICO_RD_UBER=on ICO_RD_FOG_PATH=copy
ICO_RD_DATE_PATH=stencil ICO_RD_SHADOW_MERGE=1`, the pass cuts on, `ICO_RD_STORE_POISON=1`,
`ICO_VK_FAKE_NO_DYNAMIC=1`), `rd_msaa_mvk` (the tile-based class, `ICO_VK_FAKE_LIMITS=moltenvk`),
`rd_msaa_shadow_sabotage` (`ICO_RD_MSAA_SHADOW_SABOTAGE=1`: the self-test chooses the count after
the resolve, whose counts stay right; `port/render/rd_msaa_shadow.cmake`), as `rd_msaa_nvpath` with the reporter's NVIDIA profile
forced (`ICO_RD_FOG_PATH=local`, `ICO_RD_DATE_PATH=stencil`, `ICO_RD_FX_DST_LOCAL=1`,
`ICO_RD_SHADOW_ONE=0`) under the validation layer, and
`testSkySlit` (the crack and `cleanUpFB`'s mask: no more glow sources along the crack with MSAA4
than without anti-aliasing, the silhouettes still anti-aliased), beside the epoch's other cases.
`rd_crack_msaa` (`port/render/test/rd_crack_test.c` `msaa`): along the crack the MSAA4 picture
holds at most 5 % of the clear's-depth texels of the frame without anti-aliasing, and the slit
wider than a pixel keeps the clear's fog; its frame has no shadow, so the epoch is open at the fog
and the filler's skip line is matched. `rd_crack_resolved_msaa2` and `rd_crack_resolved_msaa4`
(`port/render/CMakeLists.txt`, `port/render/test/rd_crack.cmake`): the replay tool on the corpus
room `lightning/rd-00300.rddump` at Enhanced 2x with `--aa msaa2` / `msaa4 --stats`, the fill drawn
after the resolve with more than 0 pixels filled (SKIP without the dump or where the count is not
offered).

## Measurements

Lavapipe (Mesa's CPU Vulkan driver, the only GPU in the build container) on the issue-102 dump
(`frame-20261015-161933-v4251.rddump`, 1376x896 at Enhanced 2x), `rd_replay_tool ... --stats
--pass-times`, three runs each, the machine otherwise loaded by one agent (load average 2.4-3.4):

| --aa | the resolve | `gpu:` total |
| --- | --- | --- |
| before (two resolves a picture, 2026-10-23) msaa4 | `MSAA resolve 2.0`: pass 4, the first full MSAA resolve, 9.1-9.9 ms; pass 6, the second full MSAA resolve (the double-resolve bug), 7.9-8.4 ms (17.3 ms together) | 539-546 ms |
| after, msaa4 | `MSAA resolve 1.0`: pass 5, one draw, 31.3-34.7 ms | 803-850 ms |
| after, off | no resolve | 604-738 ms |

On the CPU driver the one covered-majority pass costs about twice the two native resolves it
replaces: `vkCmdResolveImage` is a copy there, while `msaa_resolve_ps` reads four colour and four
depth samples per pixel and takes the integer mean. That is the wrong machine to judge it on: on a
GPU a 1376x896 fullscreen pass with eight loads per pixel is a fraction of a millisecond, and
the saving is the second resolve, the stencil clear's extra pass and the fog reading a depth
that holds no clear along cracks. The real measure is the 10 s summary on a player's
machine (`window: msaa: N samples, resolves R per replay ...` and the `effects:` line's
`MSAA resolve` entry): R must read 1.0 on gameplay frames (the double resolve read 1.0-1.8), and
the entry's cost is the number to compare between releases. Lavapipe offers 1 and 4 samples
only, so `--aa msaa2` draws as off there; the MSAA 2x numbers come from the rc on a Mac or a PC.
