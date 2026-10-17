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
| `port/shaders/vu_common.hlsli` | what the VU shaders share: VuCB, flags, the region rule |
| `port/shaders/vu_{prelit,lit,skin,grid,particle}.hlsl` | one shader per program |
| `port/shaders/test/vu1_test.c` | the references against hand traces and against the shaders |
| `port/render/test/rd_native_test.c` | the native frame's centre rule, the region rule's strip test |

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
([RENDER.md](RENDER.md#the-vertex-and-triangle-pre-passes)). The morph path rewrites a mesh's stream each [tick](ARCHITECTURE.md#terms)
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

A `VU_MARK_HIDE` triangle never reaches the rule (`vu_content_hidden`). A drawn triangle with a
vertex outside takes code 36's corner rule (`vu_cut_position`). A grid's strips share rows with
their neighbours, so the replay marks every triangle of a grid of two strips or more joined, as the
draw's own mark (`vu_mark.y`; `port/render/rd_replay_vu.c` (`doVuDraw`)).

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
steps 1 to 6 up to the tick cameras (the cut only for a triangle not wholly inside) and the card
test's condition, `vu_tri_tick_pre_vs` one tick's `vu_tick_shown` a draw; the triangle's draws
then resolve the stored verdicts as integers in each corner (`vu_main_vs`: a tick that showed it
clips a triangle the blended camera drops, a tick that sent it to the card test sends it there, the
card verdict decides `VU_RULE_STRIP`). A sprite's drop is evaluated once by `vu_particle_pre_vs`
and read by `vu_particle_main_vs`. A command drawn the old way evaluates the rule in every corner's
invocation of the program's entry (`vu_mesh_out`, `vu_particle_vs`). Both give the same verdict
from the same functions on the same values ([RENDER.md](RENDER.md#the-vertex-and-triangle-pre-passes);
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
tick cameras (`vu_X_plain_vs`, `ICO_VU_PLAIN`). A draw with neither `VU_F_EXTENDED` nor
`VU_F_TICKS` takes the plain one and gets the same picture. A device the renderer keeps to the
plain ones (the Adreno 740, whose driver froze the whole device compiling the extended ones, or
`[dev] vu_plain_only`) draws every state with the plain variant: there a triangle that reaches into
the picture and fails the region test is not drawn in any state (no card test: the wide picture's
edge at the old 4:3 border may open), and the pictures between two ticks take the blended camera's
rule alone ([RENDER.md](RENDER.md#the-vu-shader-variants)).

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
shaders, which have no card test (`g_rd.vuPlainOnly`, [RENDER.md](RENDER.md#the-vu-shader-variants)),
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
range, so the two ticks' payloads of a draw keep one size and pair. The vertex stream is not
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
`vu1ref_strip_card`) must agree with the reference on every vertex and strip. Part c: `RdVuCommon` against
the VU parameter block and the `VuCB` mirror. Exit 77 when parts a and c pass and there is no Vulkan
device (`port/shaders/test/vu1_test.c`, header comment).

`rd_native_test` (`port/render/test/rd_native_test.c`) renders one synthetic frame in absolute GS
geometry at native widths 512, 688, 1376, 2048 and 2560, in Original 1x and Enhanced 1x and 2x. It
checks that the 4:3 columns equal the 512-wide render byte for byte, that a narrower frame is the
centre of a wider one, that SCENE has no step across either edge of the 4:3 band, the strip test (a
wall drawn as the PS2 out of the box and clipped in every other state; a card and a behind-the-eye
strip adding nothing; a card joined to a passing strip or through another packet of its part drawn
as the wall), and the attribute planes' anchor (`port/render/test/rd_native_test.c`, header
comment). Running the tests is [TESTING.md](TESTING.md).
