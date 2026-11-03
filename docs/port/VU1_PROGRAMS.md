# VU1 programs

ICO draws its models through five [VU1](ARCHITECTURE.md#terms)
[microprograms](ARCHITECTURE.md#terms). The port runs no VU1: each program is a shader that reads
the same vertex stream and the same VU data memory, and a CPU reference of each program is the
shaders' test oracle. This document says what the programs compute, how the mesh path feeds the
shaders, and what the port draws of what the programs drop.

## Files

| path | what it holds |
| --- | --- |
| `ico2/vusrc/{normal_c,normal_l,cluster,mesh,particle}.vsm` | the program texts, labelled and commented |
| `ico2/vusrc/vu1_common.h` | the routines every program carries: `SET_GSREGISTER`, `SET_UVOFFSET`, the debug font |
| `ico2/vusrc/scissorcommcut.h` | `SCISSOR_COMMON`: the six-plane clipper of normal_c and normal_l |
| `ico2/vusrc/*.dsm` | the DMA wrappers of the decompilation's PS2 build |
| `ico2/seki/src/MicroCode.c` | program selection (`mc_SetMicroCode`), residency, the host VIF reader |
| `port/render/rd_mesh.h`, `rd_mesh.c` | the mesh path: meshes, per-list VU images, the draws, the strip marks |
| `port/render/vu1_ref/vu1_ref.h` | the CPU references' interface, and the CPU mirror of the region rule |
| `port/render/vu1_ref/vu1_*.c` | the references: state and uploads, normal, cluster, mesh, particle, scissor |
| `port/shaders/vu_common.hlsli` | what the VU shaders share: VuCB, flags, the region rule, the pre-pass's batch reads |
| `port/shaders/vu_{prelit,lit,skin,grid,particle}.hlsl` | one shader per program |
| `port/shaders/test/vu1_test.c` | the references against hand traces and against the shaders |
| `port/render/test/rd_native_test.c` | the native frame's centre rule, the region rule's strip test |
| `port/render/tools/rd_replay_region.c` | `rd_replay_tool --region-stats`: the region rule's verdict on every triangle of a dump, on the CPU |

## How the programs run

`mc_TransMicroCode` uploads a program into every list whose resident program differs; the ids are 1
normal_c, 2 normal_l, 3 cluster, 4 mesh, 5 particle (`ico2/seki/src/MicroCode.c`
(`mc_TransMicroCode`)). A draw is `MSCALF code` (a BEGIN entry: constants into registers, then the
end bit) followed by batches, each a [VIF](ARCHITECTURE.md#terms) UNPACK to TOP and an `MSCNT`,
which resumes at the program's START loop. Each program begins with an entry table of branches, one
per code (`ico2/vusrc/mesh.vsm`, header comment). Codes 0 and 2 are the same in every program:
`SET_GSREGISTER` copies a counted run of quadwords and `xgkick`s it as a
[GIF](ARCHITECTURE.md#terms) packet; `SET_UVOFFSET` writes `mem[2].xy`, the texture's UV scroll
(`ico2/vusrc/vu1_common.h`).

On the host `MicroCodeAddress[]` is all zeros: nothing in this tree assembles `ico2/vusrc`.
`mc_TransMicroCode` tells [rd](ARCHITECTURE.md#terms) the resident program of each list
(`rd_vu_program`), and `mc_HostDma` passes each MSCAL code to `rd_vu_call` with the quadwords the
UNPACKs left at TOP; code 0 goes to the [GS](ARCHITECTURE.md#terms) register decoder instead
([GS.md](GS.md#the-vu1-side-of-the-chains); `ico2/seki/src/MicroCode.c` (`mcHostVif`)). At
[replay](ARCHITECTURE.md#terms), `doVuDraw` binds the draw's vertex shader and draws the batch range
of the mesh with the recorded `VuCB` ([RENDER.md](RENDER.md#the-replay); `port/render/rd_replay.c`
(`doVuDraw`)).

## The programs

| id | program | code | entry | RdProg | shader entry |
| --- | --- | --- | --- | --- | --- |
| 1 | normal_c (prelit) | 32 | `BEGIN_NORMAL_C`: region test | `RD_PROG_PRELIT` | `vu_prelit_vs` |
| | | 34 | `BEGIN_NORMAL_C_NOCLIP`: no test | `RD_PROG_PRELIT` | `vu_prelit_vs` |
| | | 36 | `BEGIN_SCISSOR_C`: clipping | `RD_PROG_PRELIT` | `vu_prelit_vs` |
| 2 | normal_l (lit) | 32 | `BEGIN_NORMAL_L` | `RD_PROG_LIT` | `vu_lit_vs` |
| | | 34 | `BEGIN_NORMAL_L_SPEC` | `RD_PROG_LIT_SPEC` | `vu_lit_spec_vs` |
| | | 36 | `BEGIN_SCISSOR_L` | `RD_PROG_LIT` | `vu_lit_vs` |
| | | 38 | `BEGIN_NORMAL_REF` | `RD_PROG_REFLECT` | `vu_reflect_vs` |
| 3 | cluster (skinned) | 20 | `BEGIN_CLUSTER_0` | `RD_PROG_SKIN` | `vu_skin_vs` |
| | | 22 | `BEGIN_CLUSTER_0_SPEC` | `RD_PROG_SKIN_SPEC` | `vu_skin_spec_vs` |
| | | 24 | `BEGIN_CLUSTER_1_SPEC` (debug flag) | `RD_PROG_SKIN_SPEC` | `vu_skin_debug_vs` |
| 4 | mesh (grids) | 20 | `BEGIN_MESH_NOLIGHT` | `RD_PROG_GRID` | `vu_grid_vs` |
| | | 22 | `BEGIN_MESH_LIGHT` | `RD_PROG_GRID_LIT` | `vu_grid_lit_vs` |
| | | 24 | `BEGIN_MESH_LIGHT_SPEC` (debug flag) | `RD_PROG_GRID_LIT` | `vu_grid_spec_vs` |
| 5 | particle | 18 | `BEGIN_PARTICLE` | `RD_PROG_PARTICLE` | `vu_particle_vs` |

(`port/render/rd_mesh.h`, "programs"; `port/render/rd.h` (`RdProg`)). The pair (program, code)
selects the row, not the code alone; a pair with no row is logged once and not drawn
(`port/render/rd_mesh.c` (`rd__vu_row`, `rd_vu_draw_from_state`)). `vu_ps` is the pixel shader of
every program, `vu_texa_ps` for 24- and 16-bit textures under AEM (`port/shaders/vu_prelit.hlsl`).

**normal_c** transforms each vertex (pos, ST, colour) by the model to [GS](ARCHITECTURE.md#terms)
screen matrix
(`mem[16..19]`), divides by w, scales `ST + mem[2]` by 1/w, and sends the colour as it is. Code
32 runs the region test; code 34 has none, so X and Y wrap to 16 bits as on the GS
(`port/render/vu1_ref/vu1_normal.c` (`normalCLoop`); `port/shaders/vu_prelit.hlsl`).

**normal_l** adds lighting (pos, normal, ST, colour): `l = max(L1 * n, 0)`, `c = max(L2 * l, 0)`
with L2's fourth column the ambient term, `rgb = col.rgb * c`, each lane at most 255. Code 34's
specular raises `c` to the fourth power and has no ambient column. Code 38 maps the view-space
reflection to ST through `mem[24..27]` (model to view) and the inverse view's rotation, and sends
the colour as it is (`port/render/vu1_ref/vu1_normal.c` (`litColour`, `specColour`,
`normalRef`); `port/shaders/vu_lit.hlsl`).

**Code 36** (normal_c and normal_l) tests each triangle against the clip space of `mem[20..23]`:
trivially rejected, drawn by the strip, or cut by `SCISSOR_COMMON` through six planes (Z-, Z+, X-,
X+, Y-, Y+) and kicked as a fan (PRIM 0x5D, ABE on) before the batch's own packet. The
[replay](ARCHITECTURE.md#terms) keeps that order by drawing a code-36 batch twice: the cut triangles
first, then the strip's (`ico2/vusrc/scissorcommcut.h`; `port/render/vu1_ref/vu1_scissor.c`;
`port/render/rd_mesh.h` ("the recorded payload")).

**cluster** blends position and normal over two bones, `P = B0 (pos, 1) w0 + B1 (pos, 1) w1`;
the bones are VU memory 16.. (bone i at `16 + 4i`), the vertex weights hold the bones' VU
addresses as raw ints, the blended normal is lit by L1 and L2 as in normal_l
(`max(L1 * N, 0)`, then `max(L2 * L, 0)`), and `mem[2].w` is the fade alpha. It always
runs the region test, against `mem[0]` and `mem[1]` (`port/render/vu1_ref/vu1_cluster.c`;
`port/shaders/vu_skin.hlsl`).

**mesh** draws `prim_DispMesh3D`'s procedural grids (cloth, flags, the queen's barrier): one
batch is one strip, `in[1]` the batch colour, then pos, [normal,] ST per vertex; no strip flag is
read. Always the region test (`port/render/vu1_ref/vu1_mesh.c`; `port/shaders/vu_grid.hlsl`).

**particle** draws `prim_DispParticle`'s billboards: a header (count, two GIF tags, the clip
window's minimum and maximum, size scale and UV step), then two quadwords per particle,
`(x, y, z, size)` and `(u, v, grey, alpha)`, each a GS sprite (PRIM 0xD6: TME, ABE, AA1) between
two corners with flat colour. A particle is skipped when its alpha is 0 or a corner falls outside
its x/y window (1024..3071, set by `prim_InitParticleByPartition`) or its w outside the w window
(`port/render/vu1_ref/vu1_particle.c`; `port/shaders/vu_particle.hlsl`;
`ico2/seki/src/Primitive.c` (`prim_InitParticleByPartition`)).

## VU memory in the shaders

Each draw carries VU1 data memory 0..35 as the program reads it, `VuCB.vu_mem`
(`port/shaders/vu_common.hlsli`, header comment):

| VU memory | contents |
| --- | --- |
| 0..15 | the common block (`RdVuCommon`): constants, the UV offset in `mem[2]`, the GIF tag, world to GS screen, viewport, inverse view |
| 16..19 | normal: model to GS screen; mesh: `SET_MESH_MATRIX`; particle: its first matrix |
| 20..23 | normal: model to clip (the scissor's space); particle: the screen matrix |
| 24..27 | normal: model to view (code 38) |
| 28..31 | light matrix L1 |
| 32..35 | light colour matrix L2, the fourth column the ambient term |

The cluster, mesh and particle programs keep their `SET_*` uploads in VF registers; `vuImage`
copies those registers into the slots the normal programs use, so every shader reads the same map
(`port/render/rd_mesh.c` (`vuImage`)). The bones of a skinned draw go in `VuBoneCB` (VU memory
16..255), and the card test's verdicts after them ([The card test](#the-card-test)). `vu_draw`,
`vu_batch` and `vu_strip` describe the stream layout, the clip mode and flags, a chain clip plane
and where the card test's bits are (`port/shaders/vu_common.hlsli` (`VuCB`)).

## The mesh path

The principle is that the GPU gets what the VU got: the vertex stream is the VIF UNPACK payload
Packet.c or Primitive.c built, the per-draw block is the VU memory the program reads, and the
MSCALF code `mc_SetMicroCode` chose picks the shader entry and the clip mode. Nothing is derived
again from semantic transforms, so no rounding differs from the PS2's before the shader runs
(`port/render/rd_mesh.h`, header comment).

**Static meshes.** One mesh per model packet, built once from the packet's UNPACK payloads
(`rd_create_vu_mesh`): the batches' vertices without their GIF tags, numbered across the mesh, and
an index list with, for each vertex k of a batch with k >= 2 and neither k nor k-1 carrying the
strip flag (`ST.w < 1`), the three indices `ICO_VU_INDEX(K, corner) = K * 4 + corner`
(`port/shaders/shader_consts.h` (`ICO_VU_INDEX`)). The values are dense: nothing but the kick and the
corner, so a draw's values span about four times its mesh's vertices, far below 2^24 - 1, the
largest index value every Vulkan device takes (`maxDrawIndexedIndexValue` without
`fullDrawIndexUint32`). Each shader invocation has all three
vertices of its triangle, so the per-triangle decisions (region test, scissor) are the same in all
three (`port/render/rd_mesh.h` (`rd_create_vu_mesh`); `port/shaders/vu_common.hlsli`, header
comment): the vertex pre-pass evaluates each vertex once into a record the triangle stage reads
its three corners from, or without it the program's entry evaluates all three
([RENDER.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)). The morph path rewrites a mesh's stream each [tick](ARCHITECTURE.md#terms)
(`rd_update_vu_mesh`), and a mesh no frame drew for a while may be evicted, which tells the seki
side to build it again (`rd_vu_mesh_valid`) (`port/render/rd_mesh.h`).

**The triangles' marks.** At creation `rd_mesh.c` marks triangles in a byte per kick
(`RdMeshRec.kickMark`; `port/shaders/shader_consts.h` `ICO_VU_MARK_*`, which the shaders spell
`VU_MARK_*` in `port/shaders/vu_common.hlsli`). A draw binds the bytes right after the mesh's
vertices in its stream, sixteen kicks a qword (`rd__mesh_write_marks`; the device arena and the ring
alike), and `vu_mark` says where: `x` their first qword after `vu_draw.x`, `z` their qwords (0: the
mesh has no marked triangle, and no read is made), `y` the marks every triangle of the draw carries
(a grid's). `vu_kick_marks` reads the triangle's qword (its index clamped into the bytes), picks the
word and the byte, as `vu_strip_card` picks the card bits, so the read stays in the
[envelope](SHADERS.md#the-envelope). The index values carry no mark: a bit above the kick would
make a draw's values span up to 2^30, and a Mali GPU sizes a draw's vertex storage by that span
(issue 95). The marks:
- `ICO_VU_MARK_LATER` (4): a triangle that overlaps an earlier triangle of its mesh in the
  same plane (double-sided faces split along other diagonals). `vu_later_out` raises its depth past
  ftoi4's rounding bound so the later face wins every frame, as on an exact GS
  (`port/render/rd_mesh.c` (`markLaterOverlaps`));
- `ICO_VU_MARK_HIDE` (2): a triangle of the content table, drawn nowhere
  ([RENDER.md](RENDER.md#content-tables); `markContentHidden`);
- `ICO_VU_MARK_JOINED` (1): a triangle of a strip that shares a vertex position with a
  strip of another shape in its mesh, so it is not a card (`port/render/rd_mesh.c`
  (`markJoinedStrips`)), joined across a part's packets at replay
  ([RENDER.md](RENDER.md#strip-joining); `port/render/rd_join.c`).

**The VU image per list.** VU memory and the VF registers persist across draws and lists on the
PS2, but every list's chain re-establishes what its draws read: the common block is referenced
from all 13 lists, and each object chains its own matrix and light packets into every list it
draws in. rd keeps one VU image per list at record time (a `Vu1Ref`): the common block, the
`SET_*` uploads, the UV offset, the resident program and the last BEGIN code. A draw takes its
`VuCB` from its list's image at its position (`port/render/rd_mesh.h` ("VU1 state per list");
`port/render/rd_mesh.c` (`rd_vu_draw_from_state`, `vuImage`)).

**Draws.** `rd_draw_vu_mesh` records an `RDC_MESH` (or `RDC_SKINNED` with bones) with the block and
the batch range. The GS state a batch draws with ([TEX0](ARCHITECTURE.md#terms), TEX1, CLAMP, ALPHA,
TEST, the batch tag's ABE and TME) is the state block at the command's position: the material
packets reach the GS through `SET_GSREGISTER` in order with the meshes, so they are ordinary rd
state that leaks as on the GS (`port/render/rd_mesh.h` (`rd_draw_vu_mesh`)). Grids
(`rd_draw_vu_grid`) and particles (`rd_draw_vu_particles`) copy the frame's packet buffer into the
command; particles are drawn non-indexed, six vertices each (`port/render/rd_mesh.h`).

**Not reproduced.** The particle end-tag quirk is not reproduced
([DIVERGENCES.md](DIVERGENCES.md#video), VD-3); `rd_draw_vu_particles` runs the CPU reference on a
copy of the image and a later skinned draw in that list logs once (`port/render/rd_mesh.c`, header
comment). normal_c's code 38 lands on `SET_GSREGISTER`'s first bundle and has no row
(`port/render/rd_mesh.h`).

## The CPU oracles

`port/render/vu1_ref/` does what each program's instructions do to one input batch and returns
what it writes to the GS: per vertex the PACKED ST, RGBAQ and XYZ2 with the ADC bit, plus the
region test's verdict and the clip w for the tests. The code cites the `.vsm` lines it follows
(`port/render/vu1_ref/vu1_ref.h`, header comment). The arithmetic is plain C float in the
caller's rounding mode (the simulation runs round toward zero), compiled with
`-ffp-contract=off` so every multiply and add rounds on its own as the VU's FMAC does; division,
rsqrt and float to int go through `port/math/ps2float.h`, and matrices are applied in the VU's
accumulator order (`port/render/vu1_ref/vu1_ref.h`). The helpers are one instruction or one
accumulator chain each, named after it (`port/render/vu1_ref/vu1_ref_internal.h`).

`Vu1Ref` holds VU memory, the VF and VI registers; the `SET_*` routines are the upload functions
(`vu1ref_normal_set_matrix`, `vu1ref_cluster_set_matrix`, ...), and the loops are `vu1ref_normal_c`,
`vu1ref_normal_l`, `vu1ref_cluster`, `vu1ref_mesh`, `vu1ref_particle`
(`port/render/vu1_ref/vu1_ref.c`, `vu1_normal.c`, `vu1_cluster.c`, `vu1_mesh.c`, `vu1_particle.c`,
`vu1_scissor.c`). `vu1ref_static_kicks` (`port/render/vu1_ref/vu1_ref.h`) is the strip rule the
index list encodes. Besides the tests, `rd_mesh.c` keeps its per-list images in a `Vu1Ref`, runs
`vu1ref_particle` for the end-tag check, and decides the card test with `vu1ref_region_inside` and
`vu1ref_strip_card`: the vertex shaders' own region test, not the programs' ([The card
test](#the-card-test)).

## The region rule

**On the PS2.** The triangle loops (codes 32 and 38, cluster, mesh) test each vertex against the
GS region: X and Y in 0..4094 exclusive and `0 < w < 16777214` (cluster: against `mem[0]` and
`mem[1]`). A vertex outside sets ADC on itself and the next two, so no triangle using it is drawn
(`port/render/rd_mesh.h` (`RdVuClip`); `port/shaders/vu_common.hlsli` (`vu_region_rule`)).
The particle program skips a sprite whose corners or w fall outside its windows.

**In the port.** The test is the program's own on the position as computed, at every aspect,
since the projection does not change with the width. What the port then draws of a triangle
([the region rule](ARCHITECTURE.md#terms)), in this order (`port/shaders/vu_common.hlsli`
(`vu_region_rule`, `vu_region_rule_cut`); CPU mirror `port/render/vu1_ref/vu1_ref.h`
(`vu1ref_region_draw`)):

1. All three vertices inside: drawn exactly, on the ftoi4 grid.
2. Photo mode's free camera (`VU_F_BEHIND_EYE`): drawn, clipped by the GPU at the native frustum.
3. Its GPU-clipped extent lies wholly beside the camera's picture (GS 2048 -+ 256 across, by the
   native height): drawn, clipped. This happens only on a frame wider than 4:3.
4. It reaches into the picture, [out of the box](ARCHITECTURE.md#terms) (`VU_F_EXTENDED` clear): not
   drawn, as the PS2 does not draw it. At 4:3 the frame is the picture, so the picture is the PS2's.
5. It reaches into the picture in any other state (the Enhanced [preset](ARCHITECTURE.md#terms), a
   wider [native frame](ARCHITECTURE.md#terms) or a [scene scale](ARCHITECTURE.md#terms) above 1),
   every vertex in front of the eye: drawn, clipped, unless it belongs to a card, a free-standing
   strip (no `VU_MARK_JOINED`) none of whose triangles passes the test ([The card
   test](#the-card-test), `vu_strip_card`). A wall whose strip passes or is joined is drawn; an
   artist's card parked at the picture's edge stays out.
6. A vertex behind the eye and reaching into the picture: not drawn in any state.
7. The side bands (issue 29), last, on every triangle steps 4 to 6 drop (after the card test and,
   between two ticks, the ticks' rescue): on a frame wider than the camera's picture, a triangle
   that faces the eye is drawn clipped as in step 3, but only beside the picture
   (`VU_RULE_CLIP_BESIDE`, `vu_rule_beside`): `VuVSOut.plane.y` is 1 on it and `vu_pixel_tests`
   discards its pixels in the picture's columns (`vu_pixel_in_picture`: the half-open range of
   the 4:3 frame's own columns; the exact viewport puts a GS integer coordinate on a texel's
   centre, so the band's left edge is its first column's centre and belongs to the band), every VU pixel shader alike (the
   `_nodual` twins, the no-planes twins `vu_np_ps` and `vu_texa_np_ps`, the `_dstatt` builds). A
   discarded pixel writes no colour, depth or stencil, so inside the picture the PS2's drop stands
   pixel for pixel; beside it the triangle fills what was a hole (the sea at the far left of the old
   bridge at 48:9, whose only triangles there have a vertex behind the eye; walls beside the camera).
   A triangle that faces away keeps the drop everywhere: the opening's cliff `st26a_p1`, filmed from
   inside the rock, shows the eye its back faces. Facing (`vu_front_facing`; CPU mirror
   `vu1ref_region_facing`): the sign of the homogeneous determinant of the three corners' h centred
   on GS 2048 (rows `x - 2048 w`, `y - 2048 w`, `w`), which is the eye's side of the triangle's plane
   times the sign of the matrix h is made with and stays exact across `w = 0`; turned by
   `VU_F_MIRROR` (that matrix's 3x3 determinant of its x, y and w rows below 0: a reflection camera,
   a mirrored model; `vu1ref_region_mirror`) and by the kick's `VU_MARK_FLIP` ([The winding
   mark](#the-winding-mark)); front when the result is negative, never for an edge-on triangle. The
   frame wider than the picture: `vu_frame_wider` (the bound target's GS extent, a Full pixel
   target's less its left-out texels, against GS 2048 -+ the picture's half-width). Off
   (`VU_F_NO_BESIDE`, `rd_replay_vu.c` `vuBesideOn` and `vuDraw`) on a frame not wider than 4:3
   (the picture is the frame: the 4:3 picture is the PS2's by construction), for a grid (no winding
   marks: its strips wind both ways), for a mesh without winding marks (`RdMeshRec.facingFrom`
   none), for a pass whose pixel shader is a `_dstrov` build (the exact blend's storage read writes
   depth before the shader runs, so a discarded pixel would leave its depth: the PS2's drop there),
   and under `ICO_RD_VU_BESIDE=0` (the before/after compare). CPU strips and particles are
   unchanged (no w left to clip; drawn whole). The free camera (step 2) draws everything clipped
   already.

A `VU_MARK_HIDE` triangle never reaches the rule (`vu_content_hidden`). A drawn triangle with a
vertex outside takes code 36's corner rule (`vu_corner_cut_position`; in a state other than out of
the box a corner whose GS Z alone saturated keeps its GS position, as the exact neighbour has it).
A coordinate that is not a number converts to `0x80000000` (`vu_ftoi`, an ordered compare picks the
saturated value: [SHADERS.md](SHADERS.md#float-to-integer)), so its corner has no GS position on
every driver.
A grid's strips share rows with their neighbours, so the replay marks every triangle of a grid of
two strips or more joined, as the draw's own mark (`vu_mark.y`; `port/render/rd_replay_vu.c`
(`doVuDraw`)).

Under `VU_F_TICKS` (a picture between two ticks, through a blended camera) a triangle the rule
drops at the blended camera is drawn, clipped, when either tick's camera showed it on the frame;
a card stays a card. At each tick's own picture the rule is unchanged. The ticks are tested only
for a triangle the blended camera drops, tick 0 then tick 1 (the second not when the first showed
it), each with its own camera's vertices, one z cut, and the picture's and the frame's rows
(`port/shaders/vu_common.hlsli` (`vu_region_rule`, `vu_tick_shown`, `vu_cut_z`, `vu_cut_rows`;
[SHADERS.md](SHADERS.md#the-envelope)); `port/render/vu1_ref/vu1_ref.h` (`vu1ref_region_between`)).

`rd_replay.c` sets `VU_F_EXTENDED` from `vuExtended` (not the Original preset, a frame wider
than 512, or a scene scale above 1) and `VU_F_BEHIND_EYE` from the frame camera's `freeCamera`
(`port/render/rd_replay_vu.c` (`doVuDraw`); `port/render/rd_pipeline.c` (`rd__vu_extended`)).

**Where it is computed.** A command the pre-passes record has the rule evaluated once a triangle,
before the lists, by the record stages, with the FrameCB its draws bind: `vu_tri_rule_pre_vs`
steps 1 to 6 up to the tick cameras (the cut only for a triangle not wholly inside), the card
test's condition and step 7's (`VU_TF_BESIDE`: the draw has side bands, the frame is wider than the
picture and the triangle faces the eye, stored where the verdict up to the ticks is a drop or the
card test), `vu_tri_tick_pre_vs` one tick's `vu_tick_shown` a draw where the blended
verdict can be `VU_RULE_DROP` (not under the card test with every corner in front of the eye); the
triangle's draws
then resolve the stored verdicts as integers in each corner (`vu_main_vs`: a tick that showed it
clips a triangle the blended camera drops, a tick that sent it to the card test sends it there, the
card verdict decides `VU_RULE_STRIP`, then a final drop with `VU_TF_BESIDE` under a draw without
`VU_F_NO_BESIDE`, which a `_dstrov` pass sets after the pre-pass, is drawn beside the picture).
A sprite's drop is evaluated once by `vu_particle_pre_vs`
and read by `vu_particle_main_vs`. A command drawn the old way evaluates the rule in every corner's
invocation of the program's entry (`vu_mesh_out`, `vu_particle_vs`). Both give the same verdict
from the same functions on the same values ([RENDER.md](PIPELINES.md#the-vertex-and-triangle-pre-passes);
`port/shaders/vu_tri.hlsl`, `port/shaders/vu_particle.hlsl`).

**Particles.** A sprite the VU skips for its x/y window is, out of the box, not drawn when its quad
reaches the camera's picture and drawn whole when wholly beside it; under the free camera and in
every other state it is drawn whole and the GPU clips the quad, since a sprite is an effect and
never a card (`port/shaders/vu_particle.hlsl` (`vu_particle_vs`); `port/render/vu1_ref/vu1_ref.h`
(`vu1ref_particle_draw`)). Whether an effect is updated and drawn at all stays the PS2's gate on
the GS region (0..4095) in every state, because the update draws from the shared random generator
and is game logic (`ico2/sugipon/src/particleEffect.c` (`particleEffectOffScreen`)).

**CPU strips.** The strips the [EE](ARCHITECTURE.md#terms) projects (`gif_DrawStripF` and the
others) follow the same rule with `isInScreen` as the test: drawn beside the picture on a wide
frame, drawn clipped outside out of the box unless the strip is a card, and the PS2's packets out of
the box (`ico2/seki/src/GifPacket.c` (`gifHostBeside`); `ico2/seki/include/GifPacket.h`
(`gif_HostExtended`)).

**Content.** Stage faces that relied on the 4:3 picture in another way (closing planes beside the
picture) are hidden by the content table ([RENDER.md](RENDER.md#content-tables))
(`port/render/rd_content_hide.c`).

**The two variants** (issue 107). Each VU vertex shader is built with the whole rule (`vu_X_vs`,
the extended variant) and without the two steps out of the box never takes, the card test and the
tick cameras (`vu_X_plain_vs`, `ICO_VU_PLAIN`). Both have step 7, the side bands (the largest plain
module, `vu_lit_merge_plain_vs`, is 24,871 words with them, under `tools/check_spirv.py`'s 25,700).
A draw with neither `VU_F_EXTENDED` nor `VU_F_TICKS` takes the plain one and gets the same picture.
A device the renderer keeps to the plain ones (the Adreno 740 on Qualcomm's driver, whose driver
froze the whole device compiling the extended ones; Mesa's Turnip driver, where the extended ones
drew a stray vertex as a sliver; or `[dev] vu_set = plain`) draws every state with the plain
variant: there a triangle that reaches into the picture and fails the region test is not drawn
inside the picture in any state (no card test: the wide picture's edge at the old 4:3 border may
open), but one facing the eye is drawn beside it as everywhere (step 7), and the pictures between
two ticks take the blended camera's rule alone ([RENDER.md](PIPELINES.md#the-fallback-entries)).

## The card test

Rule 5's card test is decided on the CPU, once per [tick](ARCHITECTURE.md#terms), and the vertex
shaders read the verdicts: a walk over the strip in every vertex (two scans of the stream for its
ends, then up to 1,024 vertex transforms, in loops) has no place in a vertex shader inside the
envelope ([SHADERS.md](SHADERS.md#the-envelope)).

**The strips.** When a mesh is made, `rd_mesh.c` lists its free-standing strips
(`RdMeshRec.cardStrips`): the runs of vertices between two strip flags of a batch (`ST.w < 1` on a
strip's first vertex, as the index list reads it), three vertices or more, none of whose triangles
carries `ICO_VU_MARK_JOINED` then. The part joining at replay ([RENDER.md](RENDER.md#strip-joining))
can mark more strips joined; those still get a verdict, and the shader's joined test overrules it. A
grid's batch is one strip, and only a grid of one strip can be a card (`port/render/rd_mesh.c`
(`cardStrips`)).

**The verdicts.** `rd_draw_vu_mesh` and `rd_draw_vu_grid`, in any state but out of the box
(`rd__vu_extended`, the state `VU_F_EXTENDED` stands for) and not where every VU draw takes the plain
shaders, which have no card test (`g_rd.vuPlainOnly`, [RENDER.md](PIPELINES.md#the-fallback-entries)),
test each free-standing strip of the
draw's batches at the draw's camera (its `RdVuBlock`, and a skinned draw's bones) with the vertex
shaders' region test (`vu1ref_region_inside`: single precision rounded to nearest as on the GPU, the
products and sums in the VU's order, `1 / w` with the VU's zero rule; the simulation's own rounding,
toward zero, is set aside for it). The first triangle with all three vertices inside ends a strip
(`vu1ref_strip_card`), so most strips cost three vertex transforms. A strip with no passing
triangle is a card (`port/render/rd_mesh.c` (`cardsMake`, `meshCards`, `gridCards`);
`port/render/vu1_ref/vu1_ref.c`).

**The payload.** The verdicts follow the materials in the draw's payload (`port/render/rd_mesh.h`
(`RdVuCards`)): the span of vertices from the draw's first free-standing strip to its last, and two
bits for each vertex of a card's strip, the verdict at the tick before and at this tick, sixteen
vertices a word. The draw records both as this tick's; when the interpolation blends the draw from
the tick before's, it puts that tick's own verdicts in the first bits (`rd__vu_cards_merge`;
`port/render/rd_interp.c` (`blendVu`)). The section's size depends only on the mesh and the batch
range, but a draw records it only under the region rule, so a part the game switches to the scissor
code at one tick has a section at the other alone; the interpolation pairs the two payloads by their
headers and each one's own size ([RENDER.md](RENDER.md#camera-blending)). The vertex stream is not
touched (the bits are numbered by the index list's vertices), so a dump keeps its format: a dump
whose draws have no verdicts (one recorded out of the box or with the plain shaders only, or one
written before them) gets them at
load from each draw's own payload and mesh, whatever settings it replays at
(`rd__vu_cards_fill_frame`; `port/render/rd_dump.c`).

**The shader's side.** `doVuDraw` copies the bits into `VuBoneCB` when a strip of the draw is a card
at either tick and the extended shaders are in use (not `g_rd.vuPlainOnly`): after the bones a skinned mesh's weights read (`RdMeshRec.boneQwUsed`), from qword 0
for the other programs; `vu_strip` holds the first vertex covered, 1 (the card test applies), the
vertices covered and the first qword. A draw with more vertices than the block holds covers the
first ones and logs once. A skinned draw whose weights reach bone 59 (address 252) or an address
below 16 fills the block with bones (`boneQwUsed` 240), so it has no room for the bits: its strips
are drawn as no card, logged once (`card verdicts: a skinned draw's bones fill VuBoneCB`). The game
allows bones 0..59 (`ico2/seki/src/Packet.c` (`pac_makeClusterStrip`) writes `bone * 4 + 16` and
asserts the bone is below 60), so bone 59 is possible; an address below 16 would need a negative
bone. Neither is in the 145 dumps on the maintainer's machine (4,875 skinned draws, bones 0..58;
2026-10-15); `rd_stream_audit` reports the range and any draw that fills the block. `vu_strip_card` reads a vertex's two bits; a strip is a card when it was
one at either tick, so a card never enters the picture between the ticks
(`port/render/rd_replay_vu.c` (`doVuDraw`); `port/shaders/vu_common.hlsli` (`vu_strip_card`,
`vu_rule_card`)).

**The cost.** The window's 10 s statistics line gives a step's average (`strip verdicts N strips, M
triangles, T ms a step`), and a slow step's line the step's own, inside the game threads' time
([DISPLAY.md](DISPLAY.md#the-stats-lines); `rd_vu_card_counts`, `port/render/rd.h`).

## The winding mark

A triangle's facing is not in its kick: the GS draws a strip's triangles k-2, k-1, k with the
winding alternating from one to the next, so of two neighbours in a strip one has its corners
clockwise on the screen and the other counter-clockwise, front-facing both (`ico2/seki/src/Packet.c`
(`pac_makeNormalStrip`): `ST.w` 0 on a strip's first vertex). Nothing culls back faces anywhere
(`port/rhi/rhi.h` `cullNone`), so nothing needed the facing until the region rule's side bands
(step 7 of [The region rule](#the-region-rule): a dropped triangle drawn beside the 4:3 band when it
faces the camera, the opening's cliff `st26a_p1`, filmed from inside, kept dropped).

`ICO_VU_MARK_FLIP` (`port/shaders/shader_consts.h`, `VU_MARK_FLIP` in
`port/shaders/vu_common.hlsli`) is the fourth bit of a kick's mark byte, beside `JOINED` (1),
`HIDE` (2) and `LATER` (4): the byte a kick holds eight bits, and the marks are bytes after the
stream since issue 95's fix, not index bits, so the index values are unchanged. It says the corners
k-2, k-1, k wind against the triangle's front, the side its normals point to: the triangle is
marked when `cross(b - a, c - a)` of the model positions of k-2, k-1, k in that order points away
from the sum of their three normals (the dot product below 0, in double). The marks come from the
normals for every program:

- **the EE's table** (`ico2/seki/src/Packet.c` (`pac_hostFacing`), `port/render/rd_mesh.h`
  (`rd_vu_facing_begin`, `rd_vu_facing_vertex`, `rd_vu_facing_attach`)): while a packet's strips
  are written, each strip record's position (+4) and normal (+6) index give the bit of every kick, in
  the order the strip writes its vertices (the title logo's reordered strips included). A prelit
  part's stream has no normals, but its records do: every part on the disc has normals (0 prelit
  parts in the disc's winding cases below), so the table covers every program. The table is kept
  under the stream's mesh hash (`rd_vu_mesh_desc_hash`), so a mesh built again from the same packet
  (an eviction, a morph copy, a model pack refusal) finds it; it goes to the mesh in
  `RdVuMeshDesc.facing` (`port/render/rd_mesh.c` (`rd__mesh_set_facing`), `RdMeshRec.facingFrom`
  `RD_MESH_FACING_TABLE`). A record whose normal index is outside the part's normals voids the
  packet's table (logged); a hash two different tables were kept under has none (logged). The store
  lives in the port's memory, not the game's, so a save state does not carry it: a state loaded into
  a fresh run builds its meshes from packets this run never made, whose prelit meshes have no table
  (lit and skinned ones use their normals). Dumps carry the table from version 15
  (`port/render/rd_dump.c`), so a prelit mesh replays with its facing.
- **without a table**, a lit or skinned stream's own normals (the same numbers: the stream's normal
  qword is the record's; `markWinding`, `RD_MESH_FACING_NORMALS`); a prelit stream has no marks and
  no facing (`RD_MESH_FACING_NONE`): its draws carry `VU_F_NO_BESIDE`, the PS2's drop.
- a morph (`rd_update_vu_mesh`) keeps the creation's marks, as `JOINED` does; the interpolation's
  copies take their source's and its `facingFrom` (`rd__vu_mesh_copy_marks`).

Every strip mesh with a marked kick binds mark bytes (a byte a vertex, rounded up to a qword); the
bytes ride in the mesh's own upload.

**No parity rule.** A strip built one way would have every triangle of one parity face one side of
`cross(b - a, c - a)`, so a prelit strip's facing could come from its triangles' places. The disc
says otherwise. `rd_mesh`'s `winding_disc_0` .. `winding_disc_3` cases (`port/render/test/rd_mesh_test.c`
(`windingDiscChecks`), each a quarter of the `*.p2o` models, 77 without the image) measure, for each
part with normals, the share of triangles of strips wound A (the even triangles, k - s = 2, 4, ..,
face the way `cross(b - a, c - a)` points, the odd ones away), B (the reverse), C (a strip with
both) and D (degenerate), and the edges two strips of a part share, consistent when the two
triangles, each as the GS winds it, traverse the edge in opposite directions. On the PAL disc
(2026-11-03, the four slices summed):

| disc | lit triangles | A | B | C | D | prelit parts | lit parts' shared edges consistent |
| --- | --- | --- | --- | --- | --- | --- | --- |
| PAL | 3,076,540 | 74.06 % | 25.36 % | 0.57 % | 0.00 % | 0 | 2,237,860 of 2,626,011 (85.22 %) |

The phase is per strip (a strip is almost never mixed: C under 1 %), A about 3 to 1, so no global
phase marks a strip; every part has normals, so the facing comes from them for every program. The
same cases feed every part's strips through the EE's builder, as `Packet.c` does, and count per
stage how many non-degenerate triangles' bits agree with the normals' verdict
(`winding stage NAME: ..; facing marks M of N agree with the normals`): all of them, which each
slice's check holds.

## Region statistics

`rd_replay_tool --region-stats` (`port/render/tools/rd_replay_region.c`) runs the region rule on the
CPU over every VU draw of lists 0..6 under it (prelit, lit, skinned and grid draws whose clip mode is
`REGION`), at the replayed frame's camera (with `--interp` the blended camera and the ticks'):
each corner's region test (`vu1ref_region_position`, `vu1ref_region_inside_at`), the cut
(`vu_cut_z`, `vu_cut_rows` in double precision in GS pixels), the card test (`vu1ref_strip_passes`,
`ICO_VU_MARK_JOINED`, the part joins `rd__join_parts` makes before the replay) and the ticks'
cameras (`vu_tick_shown`). Per draw: the triangles drawn exactly, clipped (free camera or beside
the picture), clipped by the card test, dropped with the step that dropped them (a vertex behind the
eye, step 6; out of the box, step 4; a card, step 5), and rescued by a tick's camera, the drops
drawn beside the picture by step 7 by the step that dropped them, and the sign of the determinant
of the x, y and w rows of the draw's model-to-GS matrix (VU memory 16..18, the cluster's 4..6:
negative where the projection mirrors the model). The draw line reads `L:I mesh|skinned|grid NAME
tris T: exact E clip C strip S drop D (behind-eye B, out-of-box X, card K; rescued at a tick R),
beside N (behind-eye b, out-of-box x, card k) det -|+[; hidden H]; beside N, dropped back D[ (no
side bands)]`: drop counts the triangles that stay dropped (facing away or edge-on, or the draw has
no side bands), beside the front-facing drops (`vu1ref_region_facing` with the draw's det sign as
`ICO_VU_MIRROR` and the kick's `ICO_VU_MARK_FLIP`). A draw has side bands when the replay's frame is
wider than 4:3, the bound target's frame is wider than the camera's picture, its mesh's winding
marks came from normals (`RdMeshRec.facingFrom` not none; never a grid) and `ICO_RD_VU_BESIDE` is
not 0; the `_dstrov` passes' drop is not modelled. Totals per list and for the frame (the same
beside group), which also names the draws that draw nothing as a `--nop` spec (`--region-out FILE`
writes it; a beside triangle draws). `--region-png FILE` paints the dropped triangles of the frame
camera's draws as the GPU would have drawn them (red behind the eye, yellow out of the box, cyan a
card, magenta the whole cut polygon of a drop drawn beside the picture, whose part in the band the
GPU discards) on the native frame, the 4:3 band outlined. The cliff: `rd_replay_tool DUMP
--region-stats --mesh st26a_p1` on an opening capture at a wide aspect prints `beside 0` on every
`st26a_p1` draw when its faces turn away from the eye there. A triangle on the picture's edge to a rounding can take the other verdict than
the shader's; the `rd_region_stats` test checks the draws the stats say draw nothing against the
replay ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).

## The pre-pass's batches

The vertex and triangle pre-passes ([RENDER.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)) make
one point draw for each command and stage they record: a mesh or grid command's vertex draw, a
particle command's two sprite draws (slots A and B), and in the triangle pass the rule, the two tick
and the planes draws of each mesh or grid command, about a thousand point draws a frame. Each draw
binds its own VuCB (with `vu_tri.w` its stage's) and VuBoneCB as dynamic offsets into the upload
ring, so the vertex work is small and the cost is the draws themselves (the GTX 750 Ti of issue 126
spent 2.0 to 2.7 ms a picture on 1,074 of them under Vulkan, about 12 ms under OpenGL).

With the batches (`g_rd.vuPreBatch`, the default; `port/render/rd_replay_vu.c` (`vuPrepassRun`,
`preItem`, `preBatchesDraw`, `preBatchDraw`)) those draws become items: each still copies its VuCB
into the ring exactly as before (its record fields, its row assignment, slots A, B and C and the
vertex base are unchanged), and the items of a pass are ordered by stage key, FrameCB and group 1
(the stream's buffer, the record and the ring), keeping the record's order within each. Every run of
up to 64 items with the same three is one point draw through the stage's batch build,
`vu_X_batch_pre_vs` (the ten `vu_X_pre_vs` entries and the four record stages compiled with
`ICO_VU_PRE_BATCH`). The batch's table, one `uint4` an item (x the item's first `SV_VertexID` in the
draw, y its VuCB's qword index in the ring, z its VuBoneCB's, w 0), is copied into the ring, and
the batch's DrawCB (slot b1, which no record stage reads otherwise) carries the table's qword index
in `aux0.x` and its length in `aux0.y`. Group 1 binds the ring once more at t5, from the replay's VU
base (`s_ringBase`: the ring's offset, 256-aligned, when `rd__vu_prepass` begins, after the texture
and mesh uploads, so a frame whose texture uploads grow the ring past the device's largest storage
range still reaches every block the VU draws put after them) to that range (`ringRange`), as a
`StructuredBuffer<uint4>` (`port/shaders/vu_common.hlsli` (`vu_ring`): the tables and VuCB's
`uint4` vectors read as integers, the float vectors through `asfloat`;
[SHADERS.md](SHADERS.md#integer-lanes-never-through-a-float-load)); every index into it counts
from the base. On Direct3D 11 (issue 126) a bound storage range is written to the GPU up to the
ring's written mark at every submit, so with neither reader of t5 (the pre-pass or its batches
off: `[dev] vu_prepass = false`, `vu_prepass_batch = false`, a failed self-test; and the merge off:
`[dev] vu_merge = false`, or a merge module missing) its VU groups leave t5 unbound (the layout
keeps the slot) and a submit writes only the bytes its draws stream; with either on, the bytes t5
adds are the ones the batch and merge builds read ([BACKENDS.md](BACKENDS.md#what-direct3d-11-costs-the-cpu-a-picture)).

A batch build begins with `vu_pre_batch_entry`: a binary search over the table's x of six fixed
steps (no loop: [SHADERS.md](SHADERS.md#the-envelope)) finds the vertex's item, its VuCB's vectors
(`vu_draw`, `vu_batch`, `vu_strip`, `vu_mark`, `vu_rec`, `vu_lo`, `vu_hi`, `vu_tri`) are loaded into
statics under the cbuffer's names, and `vu_mem`, `vu_ticks` and `vu_bone` are read an element at a
time through `VU_MEM`, `VU_TICK` and `VU_BONE` (in the other builds `vu_mem[i]` and so on, the
modules unchanged word for word). The vertex is then the item's own (`SV_VertexID` less the item's
x), and every function runs as in the item's own draw on the same bytes, so each item writes the
words it wrote alone, at the same texels. The items of a pass write disjoint texels, so their new
order does not change the record. Skinned (cluster) items batch like the others: their bones are
read from their VuBoneCB in the ring. The batch builds are 2,338 to 17,220 words, within the
pre-pass entries' plain budget.

An item whose VuCB or VuBoneCB lies past the range t5 binds, or a batch whose table or DrawCB does
not fit the ring, leaves its command to the old way (its program's entry), as a ring that is full
does for one draw an entry. `[dev] vu_prepass_batch = false` (`ICO_RD_VU_PREPASS_BATCH=0`) keeps
one draw an entry and stage through the `vu_X_pre_vs` builds, as before the batches (an A/B; the
same pipeline keys, `port/render/rd_pipeline.c` takes the module by the flag, set once at start).
The start line says which: `rd: VU pre-pass: on: ...; its draws batched: one point draw a run of up
to 64 entries with the same stage, FrameCB and group 1 ...` or `... its draws one an entry and
stage ([dev] vu_prepass_batch = false, ICO_RD_VU_PREPASS_BATCH=0)`, and the summary line adds `;
pre-pass draws batched` or `; pre-pass draws one an entry` after the pre-pass's state. The 10 s
statistics and `rd_replay_tool --stats` count the draws made (`RdPerfRecord.vuPreDraws`: one a
batch); the start-up self-test checks the draws one an entry and stage would make
(`rd__vu_prepass_entry_draws`).

## The draw merge

Every VU command is at least one GPU draw, whatever the game's culling said of it
([RENDER.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)), and its cost on the GPU is per draw,
not per triangle: a PowerVR phone spent about 11 us a draw, 9.4 to 11.9 ms a picture on list 0's
~900 draws at 2x (a reporter's log). Consecutive draws very often share every state but their
VuCB: the same pipeline, the same stream buffer (one mesh arena chunk holds many meshes), the same
textures, the same DrawCB bytes, FrameCB and scissor (the `--stats` line `VU merge: N draws in M
runs` counts them: about half of the VU draws of a gameplay frame could join the draw before them).

With the merge (`g_rd.vuMerge`, the default; `port/render/rd_replay_vu.c` (`vuDraw`, `vuRunTake`,
`rd__replay_vu_flush`)) such a draw is held in an open run instead of drawn. A draw joins the run
when it has one planned pass (or, without dual-source blending, a colour pass and its alpha pass:
[below](#the-draw-merge-without-dual-source-blending)), an index list, a merge build (below), no DATE through the stencil bit,
no destination read (but the attachment read under GS state as shader data, [PIPELINES.md](PIPELINES.md#gs-state-as-data):
read in rasterization order, a merged draw's triangles see each earlier one's writes as the draws one by one would; `vuMergeDst`), is outside the loss bisect's window and the pass split, and has the same pipeline key, group 1 (the stream buffer, the record, the ring),
group 2 (the textures and the sampler), DrawCB bytes, FrameCB, scissor, render pass and GPU timing
effect as the run, up to 16 draws whose raised index values (below) stay within the device's
(`maxDrawIndexedIndexValue`). Anything else that records ends the run first
(`rd__replay_vu_flush`): the replay's `endPass` and `rd__replay_flush_run`, the walk before every
command but the VU draws and the state commands, a new timing effect, the list's end, a VU draw
under DATE (its snapshot or its stencil bit is recorded as it is bound) and every VU draw drawn
alone. The run's draw sets the run's scissor where the draw being bound set its own since, and sets
it back after.

The run is one indexed draw. A run of one draws its own index list. A longer run copies its draws'
index lists into the ring one after the other: the first draw's values as they are, each later
draw's raised by `next - lo` (its lowest value `lo` moved to one past the highest value before it),
so the draws' values are disjoint and in order. A table of one `uint4` a draw goes into the ring
too: x the draw's first raised value (draw 0: the table's length), y its VuCB's qword index in t5,
z its VuBoneCB's, w its raise. The VuCB of every draw is copied into the ring as before, and its
`mark[3]` lane, which nothing else reads, holds the table's index; b2 is bound at the first draw's.
A draw alone through a merge build gets a table of one (its VuCB, raise 0). The span check of issue
95 (`rd__span_range`) measures the run's raised values.

The merge builds are the mesh and grid programs' entries and the main shader compiled with
`ICO_VU_MERGE` (`vu_X_merge_vs` for `prelit`, `lit`, `lit_spec`, `reflect`, `grid`, `grid_lit` and
`grid_spec`, their `vu_X_merge_plain_vs` where the fallback takes the plain variant
([SHADERS.md](SHADERS.md#the-variants)), `vu_merge_main_vs`). Each entry begins with
`VU_MERGE_ENTRY(vid)` (in the other builds `vid` itself, the modules unchanged word for word):
`vu_merge_entry` reads the table's index from b2's `mark.w`, finds the index value's draw by a
binary search over x of four fixed steps, loads its VuCB's vectors into the statics the batch build
uses and reads `vu_mem`, `vu_ticks` and `vu_bone` an element at a time from t5
([The pre-pass's batches](#the-pre-passs-batches)), and returns the value less the raise: the draw's
own `kick * 4 + corner`. Every function then runs on the same bytes as the draw's own, so the
triangles, their order and every pixel are the same. The pixel shaders read nothing of VuCB (the
main shader hands the planes over as flat varyings); code 36's cut and kick draws merge like any
other, each reading its own `CUT_ONLY` / `KICK_ONLY` flag through the table, where they share a
pipeline key ([The fused scissor pair](#the-fused-scissor-pair)).

Every pipeline of those vertex shaders takes its merge build while the merge is on (the keys are the
same: `port/render/rd_pipeline.c` takes the module by `g_rd.vuMerge`, set once at start, so the
start-up set and the pipeline count do not change). The skinned programs and the particles have no
merge build: their draws are one a command, as before. The merge serves both paths: the record's
main shader and the programs' own entries (the tile-based GPUs', whose pre-pass is off, and any
device whose start-up self-test turned it off). It is off by `[dev] vu_merge = false` (`ICO_RD_VU_MERGE=0`: one GPU draw a VU draw
through the plain builds, an A/B), or when a merge module is missing. On Direct3D 11 a module is
only its description until the first pipeline that uses it (the backend compiles the HLSL with
FXC in its pipeline workers), so the start line adds `(Direct3D 11: compiled at their first
pipeline, t5 bound)` and a build FXC refuses shows as that pipeline's `pipeline creation failed
...` line, which then ends `; vertex shader vu_X_merge_vs, the draw merge's build ([dev] vu_merge
= false avoids it)`. Its draws are skipped in that replay only: the failure (on Direct3D 11, or on
any backend under `ICO_RD_FAKE_MERGE_FAIL=1`, which keeps the first pipeline made with a merge
build as failed; not a full pipeline pool) turns the merge and the fuse off at the start of the
next replay, before it asks for a pipeline (`rd__vu_merge_fallback`): the device idle, every
pipeline dropped (a key names its vertex shader, not the module, so a key made with a merge build
or its failed entry would otherwise stay), the merge builds destroyed, and once `rd: VU merge: off:
the driver could not compile vu_X_merge_vs (Direct3D 11); one GPU draw a VU draw from here`. From
there every VU draw is drawn one a command through the plain builds, and the start-up set's keys
are made again on first use. A failure of the key's pixel shader also takes this path (the
renderer cannot tell which module the driver refused); the plain key then fails too and its own
line says so. `rd_vumerge_fallback` (ctest and the WARP job) fakes the failure. The start line
says which: `rd: VU merge: on: consecutive mesh and grid draws with the same pipeline,
textures, DrawCB, FrameCB and scissor drawn as one indexed draw of up to 16, ...` or `rd: VU merge:
off ...`, and the summary line has `VU merge on` or `VU merge off` after the pre-pass. `rd_replay_tool
--stats` prints `VU merge: N draws in M GPU draws (K merged into G, A alone; merge on)` beside the
count of what could join, and the window's 10 s line `VU merge: N draws a replay in G GPU draws, A
alone (kept apart by pipeline P, texture group T, DrawCB D, split/DATE/dst S, scissor/FrameCB F)`
(`RdPerfRecord.vuMergeIn`, `vuMergeGpu`, `vuMergeAlone`, and `vuMergeBlocked` per replay: the
first key on which a draw differed from the one before it in the same pass, the reasons `--stats`
prints after `blocked by`), so a phone's log says why its draws stay alone. The merge builds are 6,755 to
52,266 words (`vu_lit_merge_vs`), within their budgets, about 550 to 920 words above the plain builds.
Every table, raise, flags word and VuCB `uint4` vector they read from t5 is loaded as an integer
(`vu_ring` is `StructuredBuffer<uint4>`): read through a float load, those words are denormal
floats, which a driver may flush to zero (issue 164 on a Mali-G52;
[SHADERS.md](SHADERS.md#integer-lanes-never-through-a-float-load)).

## The fused scissor pair

Code 36 (`normal_c` and `normal_l`'s scissor programs) kicks the fans `SCISSOR_COMMON` clips while
its loop runs, before the batch's own strip, and the fans' PRIM (the common block's 0x5D) has ABE
on. So each of its batches is two draws (`port/render/rd_replay_vu.c` `doVuDraw`): first the
triangles the VU clips (`ICO_VU_CUT_ONLY`) under the state with PRIM.ABE forced on, then the
triangles the strip kicks (`ICO_VU_KICK_ONLY`) under the state as it is; each draw runs the whole
batch through the vertex shader and leaves the other's triangles degenerate. Where the state's ABE
is off the two draws differ in the pipeline key's blend and alternate, so the draw merge cannot join
any of them: on the PowerVR phone's 2x frame 560 of 701 VU draws a replay were such pairs, and 47
draws merged.

Fused (`g_rd.vuFuse`, on with the merge; `vuFuseVerdict`), the kick draw takes its cut's state, so
its pipeline, key and DrawCB, with `ICO_VU_FUSED` (2048) beside `ICO_VU_KICK_ONLY` in its VuCB. The
merge builds of `vu_prelit_vs`, `vu_lit_vs` and `vu_main_vs` mark its triangles (`vu_common.hlsli`
`vu_fused_out`: `VuVSOut.r.w` 1, a flat lane that was always 0, and the plane distance -1 where the
draw has no clip plane, see below) and `vu_ps` and `vu_texa_ps` write their pixels as `gs_dual_out`
writes a PABE pixel whose As has its MSB clear (`vu_dual_out_fused`):
c0 = Cs with FBA's MSB, c1 1.0 for the lerps ((Cs - Cd) * 1 + Cd) and 0 for Cd*FIX + Cs
(`DF_C1_DST`), so the blender stores Cs, which is what the kick's own pipeline (blending off) stores
from the same c0. The draws, their triangles and their order are unchanged (each batch's cuts, then
its kicks; the merge keeps the draws' order), so where a cut and a kick overlap the result is the
same; the colour, alpha, depth and stencil arithmetic is the same: the picture is the same byte for
byte. The cut and its kick now share every key, so the merge joins consecutive batches of a mesh,
up to 16 draws (8 batches) a GPU draw. The vertex work is unchanged (each batch still runs through
the vertex shader twice, once a draw); what goes is the GPU draws.

At a fractional scale (the phone's Resolution Auto, 2364x1080 with Full height and Full pixel:
4.617x2.109) the VU draws go through the no-planes twins `vu_np_ps` and `vu_texa_np_ps`
(`port/render/rd_pixel_plan.c`), which read locations 0 to 2 of `VuVSOut` alone and so not `r`.
Their mark is the plane distance's sign: `VuVSOut.plane.x` is the chain's clip-plane distance only
where the draw has a clip plane (`vu_batch.z`, `ICO_DF_PLANE`), and otherwise 1 and read by nothing,
so `vu_fused_out` sets it to -1 on a fused kick's triangles where `vu_batch.z` is 0. A constant over
the triangle, its interpolation is negative exactly on those triangles. A twin never draws a
clip-plane draw (`rd__vu_pixel_fs` keeps the planes for `ICO_DF_PLANE`), and under a clip plane a
pixel with a negative distance is discarded before its output in any case. The twins' dual-source
builds take the mark as one more argument of `vu_dual_out_f` (`fused` not 0 joins the PABE rule's
unblended case: the same c0 and c1 as `vu_dual_out_fused`); every unfused pixel's arithmetic is
unchanged, and every other build keeps the function as it was (`VU_FUSED_ARG`, `VU_FUSED_OR`). The
`_nodual` twins take the mark the same way ([The draw merge without dual-source
blending](#the-draw-merge-without-dual-source-blending)). On
the phone's three frames the existing tool counted every pair kept apart for the twins at Resolution
Auto (257, 355 and 398 pairs) where 2x fused them all.

The verdict is taken once a command (its state, target and pass are the same for every batch) and
counted once a batch. A pair is fused only where all of these hold, else kept apart for the first
that fails:

| reason | the pair is kept apart when |
|---|---|
| fuse off | `[dev] vu_fuse = false` (`ICO_RD_VU_FUSE=0`), or the draw merge off (`vu_merge = false`, a merge module missing) |
| no merge build | the draw's vertex shader or the fallback's has no merge build (only those mark the kicks) |
| passes | either state's plan is not one pass (the alpha test's split), the cut's two passes without dual-source blending excepted (a colour pass and its alpha pass, joined back for these rules; kept apart where the alpha pass's Z test is not the colour pass's: GREATER with Z write) |
| dst/DATE | a destination read (the exact blend) or DATE through the stencil bit; not the attachment read under GS state as shader data, where both draws go through it |
| blend form | the cut's blend is not a lerp or Cd*FIX + Cs: the premultiplied adds and subtracts (ONE, ONE) and the Ad forms cannot drop Cd per pixel (`rd_pipeline.c` `keyBlendPath`, `blendState`); under GS state as shader data every form fuses: the cut and the kick have one key, and the kick's marked pixels take the ALPHA register `0x00` (Cs) in the shader, what the kick's own pass (`csOnly`) stores (`port/shaders/vu_common.hlsli` `VU_RETURN` under `ICO_DST_MODE` 2) |
| no planes | the cut's pixel shader reads no mark: neither `vu_ps` / `vu_texa_ps` (`r.w`) nor a no-planes twin without `ICO_DF_PLANE` (the plane's sign). None expected: a twin never draws a clip plane's draw |
| plans differ | the kick's plan is not the cut's with the blend off: the cut's pass with the key's blend, the flags `FIX_FACTOR`, `PREMUL` and `C1_DST`, the clamped FIX and `dstWant` as the alpha plan's blending off sets them (`rd_alpha_plan.c`) must equal the kick's pass in every other field (shaders, depth, mask, alpha test, flags) |

A pair whose two plans are equal already (the state's ABE on, or the alpha plan's blending off on
the cut) is counted as under one pipeline already and drawn as before: it merged without the fuse.

The start line names it after the merge's words: `rd: VU merge: on: ...; code 36's cut and kick
draws of a batch fused: the kicks under the cut's pipeline, their pixels unblended, ...` or `...; the
fuse off ([dev] vu_fuse = false, ICO_RD_VU_FUSE=0): ...`; the summary line has `VU fuse on` or `VU
fuse off` after `VU merge`. `rd_replay_tool --stats` prints `VU fuse: N pairs fused, M kept apart
(blend form K, plans differ L, passes P, dst/DATE D, no planes Q, no merge build B, fuse off F), S
under one pipeline already; fuse on`, and the window's 10 s line `VU fuse: N pairs a replay fused, S
one pipeline already, M kept apart (...)` with the same reasons (`RdPerfRecord.vuFusePairs`,
`vuFuseSame`, `vuFuseKept`). The marks cost the five merge builds of those entries 93 to 108 words
(46 to 51 for `r.w`, 47 to 57 for the plane's sign: `vu_lit_merge_vs` 51,950, `vu_lit_merge_plain_vs`
23,904, `vu_merge_main_vs` 6,827), `vu_ps` and `vu_texa_ps` 142 each and `vu_np_ps` and
`vu_texa_np_ps` 44 each (4,927 and 11,063); the other builds (the destination-read twins, every
build without `ICO_VU_MERGE`) are the same word for word, and the `_nodual` twins 34 to 44 words
more ([below](#the-draw-merge-without-dual-source-blending)). `rd_vufuse_ab` is its byte gate
([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).

## The draw merge without dual-source blending

Issue 164: a Mali-G52 phone (Vulkan 1.1, no `dualSrcBlend`) logged `VU merge: 34 draws a replay in
10 GPU draws, 1047 alone`, 1,979 draws and 1,928 pipeline binds a replay. Its dump replays the same
way on lavapipe under `ICO_VK_FAKE_NO_DUAL=1` alone (`1097 draws in 1073 GPU draws (34 merged into
10, 1063 alone)`; `blocked by ... DrawCB 358`; `VU fuse: 0 pairs fused, 471 kept apart (... passes
471)`), and the same under `ICO_VK_FAKE_LIMITS=mali` with it; the Mali limits alone (256-byte
offsets, four descriptor sets, 16 images a stage, index values to 2^24 - 1) merge as the desktop does
(`826 merged into 87`). So the limit was the missing dual-source blending, not a device limit:
without it every lerp and Cd*FIX + Cs draw is two passes (`port/render/rd_pipeline.c`
`rd__expand_no_dual`: a colour pass whose c0.a is the factor, mask RGB, then an alpha pass writing
As with blending off, no Z write, under a Z test that admits the Z the colour pass left), and the
merge took a draw of one planned pass only, the fuse a cut of one pass only. Code 36's cuts
(PRIM.ABE forced on, so lerps) stayed two passes apart from their one-pass kicks, and the draws
alternated: nothing joined.

So the merge and the fuse take the two passes (`port/render/rd_replay_vu.c` `vuMergePasses`,
`vuNoDualJoin`):

- A draw of two passes, the colour pass and its alpha pass (`rd__no_dual_second`), is held in the
  run as a one-pass draw is; the run keeps both passes' keys, pipelines and DrawCBs (a draw joins
  when all of them are the run's) and draws the colour pass over every draw's raised index list,
  then the alpha pass over the same list, one draw counted as `vuDraw` counts the pair. The pixels
  are the same: no colour pass reads the stored alpha (these blends never read Ad), and the alpha
  passes write no Z. With Z write off the depth does not move inside the run, so each draw's alpha
  pass admits the fragments its colour pass did. With Z write on, under GEQUAL (and GREATER, whose
  alpha pass takes GEQUAL) the depth only grows, the fragments that pass carry it, and the alpha left
  is the last fragment's at the final depth, whether the alpha passes follow each draw or the run;
  under ALWAYS the last fragment's either way. That is the argument the two passes already rest on
  for one draw's triangles; the run is a longer list of them under the same state.
- The fuse joins a cut's two passes back into the pass the planner made before the split (the mask
  RGB plus A, `ICO_DF_NODUAL_FACTOR` off) for its rules, and fuses as with dual-source blending. The
  kick then draws through the cut's two passes, and `vu_ps`, `vu_texa_ps` and their no-planes twins'
  `_nodual` builds take the mark (`r.w`, or the plane's sign) as `vu_dual_out_f`'s `fused` argument:
  the colour pass's factor 1.0 for the lerps and 0 for Cd*FIX + Cs, so the blender stores Cs, and
  the alpha pass's As with FBA's MSB, what the kick's own pass (blending off) stores. A cut whose
  alpha pass's Z test differs (GREATER with Z write) stays apart: there a fragment at the stored Z
  would write its alpha, which the kick's one pass does not. The dual-source builds are the same
  word for word; the four `_nodual` builds grow 34 to 44 words (`vu_ps_nodual` 5,606,
  `vu_texa_ps_nodual` 11,749, `vu_np_ps_nodual` 4,992, `vu_texa_np_ps_nodual` 11,128).

Expected on the 164 dump under the fakes: `1097 draws in 358 GPU draws (826 merged into 87, 271
alone)` and `397 pairs fused`, the desktop's figures. `rd_vumerge_mali` (`port/render/test/rd_vumerge_mali.cmake`,
label `mali`) is its gate: the dumps of the machine-local list `tools/vumerge_mali_dumps.txt` (the
164 frame in `/primary/dev/ico-dumps/164/`, the two phone frames `rd_vufuse_ab` reads and a capture)
on the plain device and under `ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DUAL=1`, under
`ICO_RD_BLEND=fixed`: every named target byte for byte, the merge's figures and the fused pairs the
plain device's, and the 164 frame's `826 merged into 87`.

## Tests

`vu1` (`port/shaders/test/vu1_test.c`) has three parts. Part a: hand traces, a few vertices per
program followed through the `.vsm` by hand, which the CPU reference must give exactly. Part b, on
the Vulkan [RHI](ARCHITECTURE.md#terms): every vertex shader in its probe mode (`VU_F_PROBE`) writes
each vertex's GS X, Y, Z, RGBA, STQ and region test, compared with the reference (X, Y within 1/16
pixel, Z and STQ within 2^-20 relative, colours within 1, the region test exact); a strip drawn
through the shipped entry and `vu_ps` against the reference's triangles drawn through the sprite
shaders, every pixel within 1 LSB; and the port's rule over the VU (`vu1ref_region_draw`,
`vu1ref_region_between`, `vu1ref_particle_draw`), checked by coverage, with the card verdicts
handed to the shader as `doVuDraw` hands them; the CPU's card test (`vu1ref_region_inside`,
`vu1ref_strip_card`) must agree with the reference on every vertex and strip. The side bands
(step 7): part a's rule rows (a vertex behind the eye facing the eye and away, out of the box, a
card, the same at 4:3), the rule between two ticks with a drop beside the picture, and the facing
(`vu1ref_region_facing`: winding, a corner at another w, a corner behind the eye, `MIRROR`, `FLIP`,
edge-on) and the matrix's sign (`vu1ref_region_mirror`); part b's `DEPART_EYE_EDGE` cases (the
target across the picture's right edge, GS 2272..2336, a vertex behind the eye, normal_c and
normal_l, both presets) with the kicks' `FLIP` marks of a surface turned to the eye (its drops drawn
beside the picture: pixels right of the edge, none left of it, by coverage against the cut polygons
outside the picture's columns) and away from it (none drawn); every other case draws with
`VU_F_NO_BESIDE`. Part c: `RdVuCommon` against
the VU parameter block and the `VuCB` mirror. Exit 77 when parts a and c pass and there is no Vulkan
device (`port/shaders/test/vu1_test.c`, header comment).

`rd_native_test` (`port/render/test/rd_native_test.c`) renders one synthetic frame in absolute GS
geometry at native widths 512, 688, 1376, 2048 and 2560, in Original 1x and Enhanced 1x and 2x. It
checks that the 4:3 columns equal the 512-wide render byte for byte, that a narrower frame is the
centre of a wider one, that SCENE has no step across either edge of the 4:3 band, the strip test (a
wall drawn as the PS2 out of the box and clipped in every other state; a card and a behind-the-eye
strip adding nothing; a card joined to a passing strip or through another packet of its part drawn
as the wall; the side bands: a behind-the-eye strip across the picture's left edge with the EE's
facing table turned to the eye adds pixels beside the picture at W 688 and 2048 and none inside it
or at 512, turned away none), and the attribute planes' anchor (`port/render/test/rd_native_test.c`, header
comment). Running the tests is [TESTING.md](TESTING.md).

`rd_mesh` (`port/render/test/rd_mesh_test.c`) checks the winding marks on synthetic strips (a
prelit mesh without a table: no marks, no facing; with the EE's table: its kicks' bits, the table
back for the dump, a table of another vertex count refused, a copy taking the marks and their
source; a lit strip wound one way, one of mixed normals, a morph keeping the creation's marks, a
table replacing a lit stream's own marks; the EE's builder over a strip, the table kept under the
stream's hash and found again, a vertex without a normal voiding it, two tables under one hash
leaving none) and, with the disc image, measures the disc's winding and the EE's table's agreement
with the normals ([The winding mark](#the-winding-mark)). `rd_region_stats`
(`port/render/test/rd_region_stats.cmake`) runs `rd_replay_tool --region-stats` on the first two
dumps of `tools/occlusion_corpus.txt` at Original 4:3 1x and Enhanced 4:3 2x: the lines' shapes,
and every pass dump the same with the draws the stats say draw nothing left out
([Region statistics](#region-statistics)).

`rd_vubatch_ab` (`port/render/test/rd_vubatch_ab.cmake`) replays the dumps of
`tools/vustage_dumps.txt` through the record one draw an entry (`ICO_RD_VU_PREPASS_BATCH=0`) and in
batches, at Original 4:3 1x, Enhanced 4:3 2x and Enhanced 16:9 2x: every target byte for byte, the
same vertices, fewer pre-pass draws batched. `rd_vuprepass_selftest_unbatched` runs the pre-pass's
start-up self-test one draw an entry; the other `rd_vuprepass_selftest` runs check that the
default draws batched ([The pre-pass's batches](#the-pre-passs-batches)).

`rd_vumerge_ab` (`port/render/test/rd_vumerge_ab.cmake`) replays the same dumps with every VU draw a
GPU draw of its own (`ICO_RD_VU_MERGE=0`) and with the merge, through the record and through the
programs' own entries (`ICO_RD_VU_PREPASS=0`), at Original 4:3 1x, Enhanced 4:3 2x and Enhanced 16:9
2x: every target byte for byte, the merged run's GPU draws exactly the other's less what its runs
joined, and fewer over the list. `rd_vuprepass_selftest_unmerged` runs the start-up self-test with
the merge off; the other `rd_vuprepass_selftest` runs check that it is on and draw through it
([The draw merge](#the-draw-merge)).

`rd_vufuse_ab` (`port/render/test/rd_vufuse_ab.cmake`) replays the same dumps and the PowerVR
phone's frames (`tools/vufuse_dumps.txt`) with the merge on, code 36's kicks under their own
pipelines (`ICO_RD_VU_FUSE=0`) and fused, at the phone's profile (the programs' own entries, 2x at
2.189 presented at 2364x1080, and Resolution Auto: 2364x1080 with Full height and Full pixel, the
no-planes twins), through the programs' own entries at Enhanced 4:3 2x and through the record at
Enhanced 4:3 and 16:9 2x: every target byte for byte, the same pairs counted, none kept apart for
the pixel shader (no planes 0), the fused run's GPU draws exactly the other's less what the merge
joined beyond it, at each setting with pairs some fused and fewer GPU draws, and on each phone frame
some fused at both phone settings. The `rd_vuprepass_selftest` runs check the fuse is on with the
merge unless `ICO_RD_VU_FUSE=0` ([The fused scissor pair](#the-fused-scissor-pair)).
