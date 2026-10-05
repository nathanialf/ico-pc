# VU1 programs

What ICO's five VU1 microprograms compute, read from the instructions in
`ico2/vusrc/{normal_c,normal_l,cluster,mesh,particle}.vsm`, `vu1_common.h`
and `scissorcommcut.h` (line numbers below are those files'). The `.vsm`
comments are machine-generated and wrong in places (cluster's "alt-color"
is the ST quadword, its "front/back face" slots are the ADC bit, particle's
`vf10.w = 0` is 1); where this document and a comment disagree, the
instruction is cited.

Implementations: CPU references `port/render/vu1_ref/` (`vu1_ref.h`), HLSL
`port/shaders/vu_*.hlsl` (`docs/port/SHADERS.md`, "VU1 programs"), the draw
interface `port/render/rd_mesh.h`. Test: `vu1`
(`port/shaders/test/vu1_test.c`).

## 1. How the programs run

**Upload.** `mc_TransMicroCode(id, mask)` (`seki/src/MicroCode.c`)
chains, per display list whose resident program differs, the VIF
BASE/OFFSET pair of `mc_setBaseOffset` and the program's DMA (`DMAret`,
`MPG 0`, the code; `normal_c.dsm`): every program is loaded at VU1 micro
address 0 and replaces the previous one. Program ids: 1 normal_c, 2
normal_l, 3 cluster, 4 mesh, 5 particle (`MicroCodeAddress[]`, `:27`).
VF/VI registers and data memory survive an upload.

| id | BASE / OFFSET (qw) | input buffers (TOPS) | output (`xgkick`) |
|---|---|---|---|
| 1, 2 normal | 0x100 / 0x180 | 256, 640 | TOP + 192 |
| 3 cluster | 0x100 / 0x180 | 256, 640 | TOP + 193 |
| 4 mesh | 0x10 / 0x1F8 | 16, 520 | TOP + 252 |
| 5 particle | 0x10 / 0x16A | 16, 378 | 544 (fixed) |

**Entry codes.** `MSCAL`/`MSCALF code` starts at micro address code * 8,
i.e. bundle `code` of the program's entry table, which is a column of
`b label` / delay-slot pairs (`normal_c.vsm:4-41`). Codes counted from the
tables:

| code | normal_c | normal_l | cluster | mesh | particle |
|---|---|---|---|---|---|
| 0 | SET_GSREGISTER | same | same | same | same |
| 2 | SET_UVOFFSET | same | same | same | same |
| 8, 10, 12 | debug font | same | same | same | same |
| 16 | SET_NORMAL_MATRIX | SET_NORMAL_MATRIX | SET_CLUSTER_MATRIX | SET_MESH_MATRIX | SET_PARTICLE_MATRIX |
| 18 | SET_NORMAL_LIGHT | SET_NORMAL_LIGHT | SET_CLUSTER_LIGHT | SET_MESH_LIGHT | BEGIN_PARTICLE |
| 20 | - | - | BEGIN_CLUSTER_0 | BEGIN_MESH_NOLIGHT | START_PARTICLE |
| 22 | - | - | BEGIN_CLUSTER_0_SPEC | BEGIN_MESH_LIGHT | - |
| 24 | - | - | BEGIN_CLUSTER_1_SPEC | BEGIN_MESH_LIGHT_SPEC | - |
| 32 | BEGIN_NORMAL_C | BEGIN_NORMAL_L | - | - | - |
| 34 | BEGIN_NORMAL_C_NOCLIP | BEGIN_NORMAL_L_SPEC | - | - | - |
| 36 | BEGIN_SCISSOR_C | BEGIN_SCISSOR_L | - | - | - |
| 38 | (SET_GSREGISTER's first bundle) | BEGIN_NORMAL_REF | - | - | - |

**BEGIN and START.** A draw is `MSCALF code` (the BEGIN entry: constants
into registers, then the E bit), followed by the batches, each a VIF
UNPACK to TOPS and `MSCNT` (`Packet.c pac_setVifEndCode`): MSCNT resumes
at the bundle after the E bit's delay slot, which is the START loop. The
loop ends with `xgkick` and `b BEGIN_...`, which re-runs the BEGIN tail up
to the E bit, so the next MSCNT starts at START again (`normal_c.vsm:189`).
State BEGIN loads (the UV offset `vf23 = mem[2]`) is therefore captured at
the MSCALF and at the end of every batch, not at the MSCNT.

**SET_GSREGISTER (code 0, `vu1_common.h:20-48`)** copies TOP + 0 (a GIF
tag) and then NLOOP quadwords to TOP + 191 and kicks them: an A+D packet
that reaches the GS in order with the mesh packets (path 1). The material,
texture (`Texture.c`), specular, reflection and dissolve register
packets (`RegistPacket.c`; `Shadow.c`'s reset) all take this route; the
host decodes them as GS register writes at their position.

**SET_UVOFFSET (code 2, `vu1_common.h:57-62`)** writes `mem[2].xy`; the
texture's `t->uv` packet (`Texture.c`) and `clearUVOffset`
(`Primitive.c`) send it. It stays in VU memory until the next one or
the next common block (`gsb_MakeCommonMatrix`), so it leaks between draws.

## 2. Program selection and code 18

`mc_SetMicroCode(mode, light, pass, clip, pri)` (`MicroCode.c`)
sends one `MSCALF code`; which program the code lands in was chosen
separately by `mc_TransMicroCode`: `reg_transMicroCode`
(`RegistPacket.c`) loads cluster when `model->disp`, else normal_c for
`lightMtx->mode == 0`, else normal_l; `prim_DispMesh3D` loads mesh,
`prim_DispParticle` particle.

| mode | light | pass | clip | code |
|---|---|---|---|---|
| 0 | 0 | | -1 / 2 / other | 34 / 36 / 32 |
| 0 | 3 | | | 38 |
| 0 | 1, 2 | 0 | 2 / other | 36 / 32 |
| 0 | 1, 2 | 1 (specular pass) | | 34 |
| 0 | 1, 2 | 2 (reflection pass) | | 38 |
| 1 | 0 | | | 20 |
| 1 | non-zero | 0 / 1 | | 20 (24 with `debug_specular_flag == 1`) / 22 |
| 2 (mesh) | 0 / non-zero | | | 20 / 22 (24 with `debug_specular_flag != 0`) |
| 3 (particle) | | | | 18 |

**Code 18.** Mode 3 is only `prim_DispParticle` (`Primitive.c`, after
`mc_TransMicroCode(5)`), so code 18 is
particle's BEGIN_PARTICLE (`particle.vsm:21`, `:58-61`): `nop` and the E
bit. It draws nothing; it leaves the PC after the E bit's delay slot so
the batch's `MSCNT` (the `0x17000000` at the end of `PrimParticleObj`,
`Primitive.c`) runs START_PARTICLE (`:79`). It is redundant: the
head packet's `MSCAL 16` (SET_PARTICLE_MATRIX, `Primitive.c`) falls
through into the same BEGIN_PARTICLE (`particle.vsm:43-61`). In every
other program code 18 is the light upload (SET_NORMAL_LIGHT,
SET_CLUSTER_LIGHT, SET_MESH_LIGHT), sent by the light packets
(`RegistPacket.c`, `Primitive.c`), never by
`mc_SetMicroCode`. The renderer program is `RD_PROG_PARTICLE`.

Combinations the table allows but data should not reach: normal_c with code
38 runs SET_GSREGISTER on a stale TOP; normal_l with code 34 for a light-0
material (clip -1) draws it with the specular loop. Neither is
reproduced: rd logs the pair once and does not draw the batch.

## 3. VU memory and registers the programs read

| VU memory | content | written by |
|---|---|---|
| 0 | (0, 0, 0, 1) | common block (`gsb_MakeCommonMatrix`, `RdVuCommon`) |
| 1 | (4095, 4095, 0, 16777215) | common block |
| 2 | xy UV offset, w cluster fade alpha, z unused | common block (0), SET_UVOFFSET, SET_CLUSTER_MATRIX |
| 3 | GIF tag 0x8000, 0x302EC000, 0x512, 0 (PRIM 0x5D fan) | common block |
| 4..7 | world to GS screen (+0x100) | common block |
| 8..11 | viewport (+0x340) | common block |
| 12..15 | inverse view (+0x380) | common block |
| 16..19 | normal: model to GS screen (+0x140 = +0x100 x node); also vf01..vf04 | SET_NORMAL_MATRIX |
| 20..23 | normal: model to clip (+0x200 x node; +0x200 = projection x view) | SET_NORMAL_MATRIX |
| 24..27 | normal: model to view (+0x80 x node) | SET_NORMAL_MATRIX |
| 28..31 | normal: light matrix L1 (lightMtx x node rotation) | SET_NORMAL_LIGHT |
| 32..35 | normal: light colour matrix L2 (lightMtx + 64) | SET_NORMAL_LIGHT |
| 37 | copy of mem[3], the scissor fan's GIF tag | SET_NORMAL_MATRIX (`normal_c.vsm:80-81`) |
| 16.. | cluster: bone i at 16 + 4i (nodeMtx x clusterMtx, world space) | SET_CLUSTER_MATRIX |
| 104..223 | scissor polygon buffers | SCISSOR_COMMON |
| 237..255 | scissor register save stack (vi14 = 256) | SCISSOR_COMMON_PUSHREGISTER |

Register-only uploads: cluster's SET_CLUSTER_MATRIX loads vf09..vf12 =
mem[4..7] and SET_CLUSTER_LIGHT vf13..vf16 (L1), vf17..vf20 (L2); mesh's
SET_MESH_MATRIX vf01..vf04 and the bounds vf13 = 0, vf14.xyw = (4094, 4094,
16777214), SET_MESH_LIGHT vf05..vf08 (L1, `setLight`'s lb), vf09..vf12 (L2,
la); particle's SET_PARTICLE_MATRIX vf01..vf04 (the caller's matrix:
`matrixptr + 0x100` from particleEffect.c, the enemy's own from enemy.c)
and vf05..vf08 (`matrixptr + 0xC0`, the screen matrix). The shaders' VuCB
puts these in the slots the normal programs use (vu_common.hlsli).

Matrices are column-major quadwords; every transform is the accumulator
chain `mulax c0, v.x; madday c1, v.y; maddaz c2, v.z; maddw c3, w`, i.e.
`((c0 x + c1 y) + c2 z) + c3 w` with each product and sum rounded. The w
term is `vf00.w` (1.0) in most loops, `pos.w` in the scissor loops, n.w for
normals into L1, l.w into L2.

## 4. Vertex input (the VIF UNPACK at TOP)

TOP + 0 is the batch's GIF tag; the loops read the count from its first
word (`ilwr.x`, `iand 0x7FFF`: NLOOP) and copy the whole quadword to the
output head. Packet.c's tag (`pac_setGifTag`, `:528`): EOP, PRE, PRIM
`0x0C | TME << 4 | ABE << 6` (triangle strip, Gouraud, STQ), PACKED, NREG 3
(ST, RGBAQ, XYZ2); with TME 0 the template has NREG 2 (RGBAQ, XYZ2), which
the 3-qword output would misread: untextured mesh strips assert in
`pac_error(..., 5)` instead. PRE resets the GS vertex queue, so every batch
starts a new strip and a batch's first two vertices never draw.

| program | per vertex (float4 each) | source |
|---|---|---|
| normal_c | pos (x, y, z, 1), ST (s, t, 1, f), colour (r, g, b, 127) | `pac_makeNormalStrip` (`Packet.c`) |
| normal_l | pos, normal, ST, colour | same, normal when lit, clustered or texRef |
| cluster | pos, normal, weights (int bone0 * 4 + 16, w0, int bone1 * 4 + 16, w1), ST, colour | `pac_makeClusterStrip` (`:381`) |
| mesh | batch: tag, colour (Mesh3D.col, alpha 128 sent as 127); per vertex pos, [normal,] ST | `prim_makePacketMesh3D` (`Primitive.c`) |
| particle | count, tag (NLOOP 1), tag (NLOOP 1, EOP), clip min (1024, 1024, 0, 1), clip max (3071, 3071, 0, 16777215), (size scale, du, dv, 0); per particle (x, y, z, size), (u, v, grey, alpha) | `prim_InitParticleByPartition` (`:668`), enemy.c, particleEffect.c |

ST.w `f` is the strip flag: 0 on the first vertex of each strip, 1
otherwise. Colours are 0..255 as floats; Packet.c's default is (128, 128,
128, 127). A batch holds at most 192 input quadwords (`pac_checkDivide`); a
strip never spans two batches.

## 5. What each program computes

Notation: `q = 1 / w` from `div q, vf00w, x` (a zero divisor gives
+-Fmax); `max0`, `min255` per lane; `ftoi0`/`ftoi4` truncate (x 1 or x 16)
and saturate at +-2^31.

### normal_c (prelit), codes 32, 34, 36

START_NORMAL_C (`:135-199`), START_NORMAL_C_NOCLIP (`:212-269`):

1. `p = M * (pos.xyz, 1)` (M = vf01..vf04 = mem[16..19]) `:152-155`.
2. `q = 1 / p.w` `:159`; `P.xyz = p.xyz * q`, `P.w = p.w` `:171`.
3. `STQ = (st.x + uv.x, st.y + uv.y, st.z + 0) * q`, w lane 0 (`:149`,
   `:157`, `:172`; uv = mem[2] at BEGIN, its z zeroed `:151`).
4. `RGBAQ = ftoi0(colour)`: no clamp (`:173`); the GS keeps the low 8 bits
   of each word, so 256 would wrap to 0.
5. `XYZ2 = ftoi4(P.xyz)`, ADC chosen below; X/Y are 12.4 in their low 16
   bits, Z the whole 32-bit word.
6. Region test, code 32 only (`:175-181`): `sub.xyw vf00, vf13, P` and
   `sub.xyw vf00, P, vf14`, `fmand` with 0xD0 (the x, y, w sign flags):
   inside iff `0 < X < 4094`, `0 < Y < 4094`, `0 < w < 16777214` (X, Y in
   pixels after the divide, w the clip w = view depth).
7. ADC (`:166-198`, counter vi05): strip flag (`ST.w - 1 < 0`, `fsand
   0x2`, `:156/:174` read at `:160/:178`) sets vi05 = 3; then vi05 -= 1;
   outside sets vi05 = 3 and ADC; else ADC iff vi05 > 0. Net effect: a
   triangle (k-2, k-1, k) is drawn iff vertex k has ADC clear iff none of
   k-2, k-1, k is outside and neither k-1 nor k starts a strip.

START_SCISSOR_C (code 36, `:314-399`) instead:

- `p = M * pos`, `c = M2 * pos` (M2 = mem[20..23] in vf13..vf16, w term
  pos.w) `:334-342`; `P = p * q` on all four lanes, `XYZ2 = ftoi4(P)` with
  the w word `ftoi4(P.w)` (16, ADC clear) `:348-352`; STQ and colour as
  above.
- Clip flags of `c` (`clipw.xyz`: x > |w| etc.) for the window k-2, k-1, k
  (`:343-346`; vf22..vf24, which START sets to (0, 0, 0, 1)); vi04, vi05,
  vi06 = "vertex has any flag" for the three (`fcand vi01, 0x3F`), shifted
  per vertex (`:338-339`, `:353`).
- Strip flag: vi04 = vi05 = 1, vi09 = 2 (`:352-356`).
- If vi04 + vi05 + vi06 > 0 (`:361`): the vertex gets ADC (`isw.w vi11 =
  0x8000`, `:399`), and unless the skip counter says the triangle is not
  complete yet, the six `fcor` tests (`:373-389`: one plane flag set on all
  three vertices) trivially reject it or SCISSOR_COMMON clips and draws it
  (`:391-396`).
- The skip counter: `iaddi vi09, vi09, -1` then `ibgtz vi09` (`:371-372`).
  A conditional branch reading a VI register that the instruction just
  before it wrote sees the value from before that write (PCSX2 models this
  as "branch VI delay", `microVU_Analyze.inl analyzeBranchVI`; it does not
  apply after flag instructions such as `fmand`/`fsand`). So the branch
  reads 2, then 1: the two vertices after a strip start skip the scissor,
  the third clips (k-2, k-1, k). The other reading would clip a triangle
  made of the start-up vector (0, 0, 0, 1) and the batch's first two
  vertices at the start of every scissor batch, reading the input buffer
  before TOP; the game would show garbage fans on every clipped object.
  The program's own `SET_GSREGISTER_LOOP` puts a `nop` between `iaddi`
  and `ibne` (`vu1_common.h:31-34`), the pattern that avoids the delay.

SCISSOR_COMMON (`scissorcommcut.h`): the triangle (clip-space positions,
the three output colours read back and converted with `itof0`, the input
ST with the UV offset added on xy) runs through six Sutherland-Hodgman
passes ZMINUS, ZPLUS, XMINUS, XPLUS, YMINUS, YPLUS (`:56-214`). Per edge
(CLIP_INTER `:240-301`): in/in stores the current vertex, in/out stores it
and the cut, out/in stores the cut. The cut (INTERPOLATE, LOOP_ROT_END
`:303-336`): `dc = cur.lane - s * cur.w`, `dn = next.lane - s * next.w`
(s = -1 minus planes, +1 plus planes), `t = |dc / (dn - dc)|`, every lane
of position, colour and STQ `(next - cur) * t + cur`. The out flags are
`clipw`'s (|w|), the distances use the signed w. DrawScissorPolygon
(`:342-387`): each vertex through the viewport mem[8..11] (`V * pos`, w
term pos.w), divided on all four lanes, `ftoi4.xyzw`; STQ = stq * q (xyz);
colour `ftoi0`; GIF tag = mem[37] with NLOOP = n, EOP (an empty
NLOOP-0 tag is kicked first). The fan is kicked during the loop, before
the batch's own packet (kicked after the loop), and its PRIM is mem[3]'s
0x5D: fan, Gouraud, TME and ABE always on, whatever the material's ABE.
In the game M2 = projection (`+0x1C0`, near 2, far 262144, x and y planes
at the 1500-unit screen, wider than the visible screen) x view x node, and
`viewport x M2 = M` (both z mappings are `a + b / z` through the same two
points).

### normal_l (lit), codes 32, 34, 36, 38

START_NORMAL_L (code 32, `:130-212`): as normal_c code 32 with

- `l = max0(L1 * n)` (L1 = vf05..vf08 = mem[28..31], w term n.w)
  `:154-161`;
- `c = max0(L2 * l)` (L2 = vf09..vf12 = mem[32..35], w term l.w: the
  fourth column is the ambient term) `:166-177`;
- `RGBAQ = ftoi0(min255(col.rgb * c.rgb, col.a))` `:182-190` (I = 255.0
  from the `loi` in the I-bit bundle `:182`);
- `STQ = (st.xy + uv.xy, st.z) * q` `:164`, `:176`.

There are three directional terms (the rows of L1 dotted with n) and an
ambient term through n.w; the light matrix is world space times the node
rotation (`RegistPacket.c`), so n is model space.

START_NORMAL_L_SPEC (code 34, `:241-317`, the specular pass in list 4):
`l = max0(L1 * n)`, `c = vf09 l.x + vf10 l.y + vf11 l.z + vf00 l.w` (no
ambient column, `:273-276`), `c = max0(c)`, `c = (c * c) * (c * c)` per
lane (`:283`, `:287`), `RGBAQ = ftoi0(min255(col.rgb * c, col.a))`. The
"specular" is the fourth power of each light's diffuse colour term; no
eye vector is involved.

START_SCISSOR_L (code 36, `:505-612`): START_SCISSOR_C with the code-32
colour.

START_NORMAL_REF (code 38, `:365-471`; light 3 materials and the
reflection pass): MV = mem[24..27] (vf05..vf08), the inverse view's
rotation mem[12..14] (vf09..vf11).

1. `v = MV * (pos, 1)`, `p = M * (pos, 1)` `:384-391`.
2. `d = v.xyz * rsqrt((v.x^2 + v.y^2) + v.z^2)` (`:392`, `:399-405`,
   `:412`): the eye-to-vertex direction in view space.
3. `n' = MV rotation * n` (`:393-396`, no translation).
4. `dn = (d.x n'.x + d.y n'.y) + d.z n'.z` (`:416`, `:420-422`).
5. `r = (n' dn + n' dn) + d dn` (`:426-428`), i.e. `dn (2 n' + d)`; not
   the mirror vector `d - 2 dn n'`, but what the program computes.
6. `W = invView rotation * r` (`:433-436`), `W.z += W.z` (`:440`),
   `W.xy += W.z` (`:444`), `S, T = W.xy * 0.125 + 1.0 * 0.25` (`:450-452`;
   the 1.0 is `rinit R, vf00w; rget vf25` `:440-441`; the upper instruction
   of an I-bit bundle reads the I the previous bundle loaded, so 0.125
   multiplies and 0.25 is added).
7. `STQ = (S, T, 1) * q` (`:404` vf31.x = 0 + q, `:456`); no UV offset.
8. `RGBAQ = ftoi0(colour)` unclamped (`:402`); region test and ADC as
   normal_c code 32.

### cluster (skinned), codes 20, 22, 24

START_CLUSTER_0 (code 20, `:171-291`), per vertex:

1. B0, B1 = the bones at the weight quadword's two VU addresses
   (`ilwr.x`/`ilwr.z`, `:188-199`).
2. `P = (B0 (pos, 1)) w0 + (B1 (pos, 1)) w1`, w = 1 (`:196-221`; the w
   term is vf00.w, not pos.w).
3. `N = (B0 rot n) w0 + (B1 rot n) w1`, w = 1 (`:208-223`; the w term
   vf00 * n.w only reaches the unused w lane).
4. `S = mem[4..7] * P` (the common world to GS screen; bones are world
   space) `:226-229`; `q = 1 / S.w`, `S.xyz *= q` `:235`, `:243`.
5. `l = max0(L1 * N)` (vf13..vf16; N.w = 1, so L1's fourth column always
   adds) `:232-241`; `c = max0(L2 * l)` with the ambient column (vf17..vf20)
   `:246-250`; `RGBAQ = ftoi0(min255(col.rgb * c, col.a))` `:257-266`.
6. `STQ = st.xyz * q`, w lane 0 (`:242`, `:255`): **no UV offset**: a
   scrolling texture does not scroll on a skinned mesh.
7. Region test against memory, not constants (`:253-265`): inside iff
   `mem[0].x < X < mem[1].x`, same for Y, `mem[0].w < w < mem[1].w`, i.e.
   `0 < X < 4095`, `0 < Y < 4095`, `1 < w < 16777215`.
8. ADC with counter vi07 (`:238-282`), same rule as vi05.

CLUSTER_0_SPEC (code 22, `:322-439`): `c = vf17 l.x + vf18 l.y + vf19 l.z +
vf00 l.w` (no ambient, `:391`), `max0`, squared twice (`:397`, `:401`).

CLUSTER_1_SPEC (code 24, `:482-611`, only with `debug_specular_flag == 1`):
the code-20 colour squared once (`:568`), alpha `col.a * mem[2].w`
(`:562`: the fade alpha of `reg_setCMatrixPacket`), and the UV offset
added (`:505`, `:596`); the strip flag sets vi07 = 2 after the decrement
(`:578`, `:588`), the same net rule.

**The "front/back face" slots (`cluster.vsm:460`).** Line 460 is the comment
of START_CLUSTER_1_SPEC; the instructions it describes are `:264-282`
(code 20), `:413-431` (22) and `:571-605` (24). The two "slots" are vf26
and vf27, both `ftoi4` of the same position, with `vf26.w = 0` (`mfir.w
vf26, vi00`, `:182`) and `vf27.w = 0x8000` (`mfir.w vf27, vi14` with vi14 =
0x8000, `:180-183`): the XYZ2 word whose bit 15 is ADC. Which one is stored
is decided by the region test (`fmand` of the `sub.xyw` sign flags against
mem[0]/mem[1]) and the vi07 counter. Nothing in any of the five programs
computes a cross product or a winding (no `opmula`/`opmsub`, no
screen-space determinant). It is XYZ2 kick selection: no facing test, no
winding handling for `RD_PROG_SKIN`; pipelines keep `cullNone` (the GS
does not cull).

### mesh (procedural grids), codes 20, 22, 24

One batch per grid strip (`prim_makePacketMesh3D`: tag NLOOP = stripLen,
PRIM from `prim_InitMesh3D`'s argument: 0x5C or 0x4C for cloth/flags, 0x1C
for the queen's barrier), the colour once per batch (`:161`, `lqi vf18`).

- MESH_NOLIGHT (code 20, `:337-396`): `RGBAQ = ftoi0(colour)` unclamped.
- MESH_LIGHT (code 22, `:147-221`): `l = max0(L1 * n)`, `c = L2 * l` with
  the ambient column and **no clamp** (`:184-187`; the `maxx` at `:216` is
  the next vertex's L1 result), `RGBAQ = ftoi0(min255(col.rgb * c, col.a))`.
- MESH_LIGHT_SPEC (code 24, `:240-317`, debug only): c squared once (`:284`).
- Position, STQ (UV offset added) and the region test as normal_c code 32
  (bounds from SET_MESH_MATRIX, `:87-90`); ADC with vi05, **no strip flag**
  (the loops have no `fsand`).

### particle, code 18 then START_PARTICLE

START_PARTICLE (`:79-149`), per particle (x, y, z, size), (u, v, grey,
alpha):

1. Alpha zero (`subx.w vf00, b, vf00x` and `fmand` with 1, the w zero flag,
   `:100-105`): skipped.
2. `h = M * (x, y, z, 1)` (`:97-104`; the comment's "w = 0" is `move.w vf10,
   vf00`, 1.0).
3. `e = S * (size k.x, size k.x, 0, 1)` (`:99-112`): with S the screen
   matrix, e.xy = (vs1 zoom size k.x, vs2 zoom size k.x), the half extent
   before the divide.
4. Corners `(h.xy -/+ e.xy, h.z) * q`, q = 1 / h.w (`:108-121`).
5. Both corners strictly inside the window: `clipMin < x, y` and `1 < h.w`,
   `x, y < clipMax` and `h.w < 16777215` (`:125-133`; 1024..3071 pixels
   from `Primitive.c`); else skipped.
6. Output: tag (NLOOP 1), RGBAQ `ftoi0(grey, grey, grey, alpha)`, ST0
   (u, v, 1), XYZ2 corner 0, ST1 (u + du, v + dv, 1), XYZ2 corner 1
   (`:106-139`); ST is not divided (Q = 1). PRIM 0xD6: sprite, TME, ABE,
   AA1. The last stored particle's tag is overwritten with the EOP tag
   (`:145`).

## 6. VU semantics this depends on

- **Pipelining.** The loops are software pipelined (vertex k+1's transform
  overlaps vertex k's stores). FMAC results are interlocked; every value is
  read after the write for the vertex it belongs to, so a sequential
  per-vertex evaluation (the references) gives the same results.
- **Flags.** `fsand`/`fmand` read the flags of the FMAC instruction issued
  four bundles earlier; every site places its flag-setting `sub` exactly
  there (`normal_c.vsm:174/:178`, `:175-176/:179-180`; `mesh.vsm:197-202`;
  `cluster.vsm:238/:242`, `:260-265`; `particle.vsm:100/:104`, `:125-132`).
  `max`, `mini`, `ftoi`, `clip` and `mr32` set no MAC flags.
- **Q.** `div` takes 7 cycles; reads of Q before then see the previous
  value. The loops either `waitq` or issue the next vertex's `div` after
  the last `mulq` of the current one. CLUSTER_1_SPEC (`:551`, `:558`)
  reads Q exactly 7 bundles after the `div` without `waitq`; taken as the
  new value (debug code only).
- **I register.** An I-bit bundle loads I for the following bundles; its
  own upper instruction reads the previous I (NORMAL_REF `:450-452`).
- **Branch VI delay.** See START_SCISSOR_C above; the only affected sites
  are `normal_c.vsm:371-372` and `normal_l.vsm:584-585`.
- **Leftover VI state.** vi05 (normal, mesh) and vi07 (cluster) are not set
  at START; their largest leftover is 3, which can only set ADC on a
  batch's first two vertices, which never draw. vi09 is reset by the strip
  flag of every batch's first vertex.
- **Conversions.** `ftoi4` saturates: GS Z = `ftoi4(Zscreen)` where the
  screen matrix already maps to the 32-bit range (`vsParam[5..6]` = 1 ..
  536870880, `GsBase.c`), so Z reaches 2^29 * 16 and saturates at
  0x7FFFFFFF for view depths below about 8 units (`zf / w + zn > 2^27`);
  everything nearer has the same Z. Colours are never clamped below 0
  except through `max0`, and the unlit paths never clamp at 255: the GS
  takes the low byte.

## 7. Findings for the renderer

1. The scissor programs draw clipped triangles before the batch's other
   triangles (fans are kicked during the loop). `rd_mesh.h` draws a scissor
   batch twice, cut triangles first (`ICO_VU_CUT_ONLY`, then
   `ICO_VU_KICK_ONLY`); merging batches loses this order.
2. Scissor fans always have ABE on (PRIM 0x5D from the common block's
   tag): a clipped triangle of a material without ABE blends with As where
   its unclipped neighbours do not, by (Cs - Cd) (128 - As) / 128: one or
   two LSB at Packet.c's vertex alpha 127, more where TCC brings in a
   texture alpha below 0x80 (alpha-tested materials). Since the fans are
   exactly the `ICO_VU_CUT_ONLY` draw, rd reproduces it by drawing that
   pass with ABE forced on (the ALPHA register in force, as the GS would);
   the shaders need nothing for it.
3. No UV scroll on skinned meshes (codes 20, 22) and on reflections (38).
4. A particle batch that draws nothing writes its EOP tag through the vi04
   of an earlier program: after a normal_c/normal_l/mesh loop (vi04 = 0)
   that is VU memory 0, the common block's (0, 0, 0, 1). Until the next
   common block upload the cluster region test then has a lower bound of
   (0, ~8.6e9 as a float, 0, 0): every skinned triangle drawn in the same
   list after an all-culled particle batch disappears. The reference
   models it (`vu1_particle.c`); the shaders take mem[0..1] from VuCB, and
   rd does not reproduce it (RENDER_API.md section 10).
5. SET_CLUSTER_MATRIX copies one quadword more than the packet holds (the
   count word is n + 1, `RegistPacket.c`, and the loop copies vi06
   quadwords from TOP + 1): bone slot nodeNum gets the following VIF data.
   Harmless (no weight points there).
6. Region bounds differ by program: normal and mesh 0 < x, y < 4094 and
   0 < w < 16777214 (constants), cluster 0 < x, y < 4095 and 1 < w <
   16777215 (mem[0], mem[1]), particle 1024 < x, y < 3071 per corner.
7. The z of `ftoi4` saturates near the camera (section 6): coplanar or
   near geometry within about 8 units shares Z 0x7FFFFFFF, and GEQUAL lets
   the later draw win.
8. `RENDER_API.md` section 9 says the screen matrix maps near/far to GS Z
   536870880 and 1; that is before the mesh programs' `ftoi4`, which
   multiplies by 16 (and saturates).

## 8. The shaders against the VU

Per vertex the shaders evaluate the same chains in the same order with
`precise` (no fused multiply-add, no reassociation), truncating `ftoi`
with the VU's saturation, the VU's divide-by-zero rule, `max`/`min`
clamps, and the same masks (`& 255` per RGBAQ word, `& 0xFFFF` on X/Y
where the GS would wrap). What cannot be identical:

| difference | bound |
|---|---|
| Rounding: the VU rounds toward zero (with its own last-bit behaviour, `float-semantics.md`), GPUs to nearest | measured on lavapipe against the reference in round toward zero: X/Y identical, colours identical, Z within 4.3e-7 relative, STQ within 7.8e-7 relative (`vu1` prints these). In general one rounding step per operation: X/Y can differ by one 1/16-pixel step where a value sits on a boundary, colours by one LSB |
| Division and `rsqrt` precision: Vulkan and D3D allow 2.5 ULP for `1/x` and `rsqrt` | a few ULP on q: below 1/16 pixel at GS coordinates up to 4096 except on a boundary; lavapipe divides exactly |
| Overflow: VU results clamp to Fmax, GPU results become Inf | only for degenerate input (a vertex at the eye in NORMAL_REF, w = 0) |
| Triangles the scissor programs clip against a Z plane (a vertex behind M2's near or beyond its far plane) | drawn with a homogeneous position and clipped by the GPU at GS Z = 2^32 (near, about 4 units in the game, VU: 2) and GS Z = 0 (about the far plane); the VU's fan interpolates colour and STQ linearly in screen space between clip-space-interpolated cut points, the GPU over the original triangle: measured in `vu1` ("prelit 36 near", depth ratio 4:1, random colours) 296 of 302 covered pixels differ, by up to 142; coverage agrees to one pixel. Real cases are geometry crossing the camera's near plane |
| Triangles clipped only on x/y planes | drawn as the GS would draw the unclipped triangle (the VU's guard band is wider than any target); identical when the three vertices share w, else the cut points' perspective-correct colours shift interior colours slightly |
| Scissor fan ABE (finding 2) | the cut-only pass with ABE forced (rd's pipeline choice) makes it exact; otherwise up to (Cs - Cd)(128 - As)/128 |
| Particle PRIM.AA1 | not reproduced (the GS antialiases lines and triangles; whether it affects sprites is not settled by any source used here) |
| GS fixed-point colour and STQ interpolation | float interpolation, colour rounded to nearest in the pixel shader (as `sprite_ps`) |

Exact by construction: which triangles draw (region test, strip flags, ADC
window, trivial reject; the vertex shader evaluates all three vertices of
its triangle), GS X/Y as 12.4 integers, GS Z as the saturated integer then
`gs_z_to_depth`, the colour wrap, the draw order inside a scissor batch.

## 9. Tests

`vu1` (`port/shaders/test/vu1_test.c`):

- hand traces: normal_c 32 and 34 (ADC window, region test, X wrap),
  scissor 36 (the VI-delay skip, trivial reject, one fan traced through
  SCISSOR_COMMON and DrawScissorPolygon), normal_l 32 and 34, NORMAL_REF,
  cluster 20/22/24 (blend, w > 1 bound, fade alpha, UV offset only in 24),
  mesh 20/22/24, particle (both drops, the EOP store, the empty-batch
  clobber of mem[0]), the static kick rule; exact;
- the shaders on lavapipe: every entry in probe mode against the reference
  (all values identical in round to nearest; the round-toward-zero
  differences above), and rendered strips against the reference's kicked
  triangles and fans drawn through `sprite_world_vs`/`sprite_ps`: within 1
  LSB on every pixel for 15 cases (two batches with strip restarts and a
  region drop or scissor cut each), the Z-plane scissor case measured;
- `RdVuCommon` matches section 12's quadword map and goes verbatim into
  VuCB and VU memory.
