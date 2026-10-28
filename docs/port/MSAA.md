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
| `port/shaders/blit.hlsl` | the resolves (`msaa_resolve_ps`, `depth_resolve_ps`) and the depth spread over the samples (`depth_upsample_ps`) |
| `port/render/rd_pipeline.c` | the resolves' keys (`rd__msaa_resolve_key`), in the fog's family of the start-up set (`rd__enumerate_reachable_fog`) |
| `port/render/rd_core.c` | the counts offered (`rd_aa_supported_mask`) |
| `port/render/test/rd_msaa_test.c` | the epoch against the frame without anti-aliasing |

## Where the epoch closes

The epoch closes at the first command that reads or writes SCENE's own colour or depth outside a
pass into SCENE_MS (`port/render/rd_replay.c` (`msaaPass`, `rd__transition`)). In gameplay that is
the shadow count's pass in list 3, which binds SCENE's depth with the count's colour: lists 0 to 2
are anti-aliased, and the fog, the list 5 additive meshes, the glow and the aura draw on the
resolved picture. A frame without a shadow keeps the epoch open to the fog, which then runs per
sample where the device shades per sample.

The shadow count is read after that close, from SCENE's single-sampled stencil, whichever way it
is resolved (the seven stencil-tested draws or the one draw through the stencil view,
[WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-count-in-one-draw)): the two give the same bytes with
anti-aliasing on or off, so the one draw needs no MSAA fallback.

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
top edge). The crack filler ([RENDER.md](RENDER.md#crack-filling-above-1x),
`port/render/rd_crack_fill.c`) therefore runs on the resolved picture: `doFog`
(`port/render/rd_replay.c`) passes the epoch's state at the fog (`RD_CRACK_MSAA_OPEN`,
`RD_CRACK_MSAA_RESOLVED` or `RD_CRACK_MSAA_NONE`, `port/render/rd_replay.h`). In gameplay the
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

The close records what is open into SCENE_MS first (the pass, a clear pending on SCENE) and a
shadow reset pending on SCENE's depth (its stencil clear as a pass of its own), then resolves
once (`port/render/rd_replay.c` (`msaaEnd`)). The reset's stencil clear transitions SCENE's depth,
which is the hook that closes the epoch; `s_ms.closing` keeps that transition from closing it a
second time from inside the first close. Without the guard every frame with a shadow resolved
twice.

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
| `window: msaa: N samples, resolves R per replay, the last closed at list L` | the 10 s summary (`port/platform/window_host.c`); R is 1.0 or below |
| `fog: in the MSAA epoch on the resolved picture (each pixel's smallest covered sample unless more than half its samples were uncovered: ...)` | once a run (`port/render/rd_replay.c` (`doFog`)) |
| `msaa: msaa_resolve_ps was not made on this device ...` | start-up, a module missing |
| `rd: crack fill: on above 1x (...); [dev] crack_fill = false turns it off (drawn after the MSAA resolve)` | the first fill on the resolved picture (`port/render/rd_crack_fill.c`); the 10 s summary adds `; A after the MSAA resolve`, `rd_replay_tool --stats` `; F fills after the MSAA resolve` |
| `rd: crack fill: not drawn on SCENE at scale SXxSY: the MSAA epoch is still open at the fog (...) (reported once)` | a fog inside the open epoch |

## Tests

`rd_msaa` (`port/render/test/rd_msaa_test.c`): `testShadowReset` (one resolve a replay with a
shadow reset, the close at list 3, SCENE byte for byte the frame's without the reset) and
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
