# VU0 maths on the host

The PS2 game does its vector and matrix maths on VU0 in macro mode, through
inline assembly in the game sources and through Sony's libvu0. The host build
replaces all of it with ordinary C over `float[4]` and `float[4][4]` in
`port/math/`. There is no translator and no emulated VU0 register file: each
routine was read in assembly and written again as the C it computes, keeping
the operation order, the fields it writes and its W handling. This document
records, per routine, what the assembly did that is not obvious from the
routine's name, so the C can be audited against it.

Sources: `docs/research/float-semantics.md` (the float semantics and the
R-register vectors), the assembly in the decompilation's game sources
(the port's `ico2/` has only the C bodies), and this repository's
`sce/libvu0/libvu0.c` for the sceVu0 sequences.

## Layout

| file | contents |
| --- | --- |
| `port/math/ps2float.h`, `ps2float.c` | the PS2 float behaviours plain C would get wrong: `ps2_add`, `ps2_sub`, `ps2_mul`, `ps2_div`, `ps2_sqrt`, `ps2_rsqrt`, `ps2_ftoi`, `ps2_ftoi4`, `ps2_itof`, `ps2_max`, `ps2_min` |
| `port/math/ico_math.h` | the current matrix (`ico_current_matrix`), `ico_apply_matrix`, the R register accessors, the quaternion helpers the game's host branches call; included by `ico2/common/include/typedef.h` |
| `port/math/vector_inline.h` | static inline helpers for the header/inline asm in `sugiCommon.h` and `clothAnimation.c` |
| `port/math/matrix_stack.c` | `seki/src/Matrix.c`'s current-matrix routines and its push stack, `ico_vu0_registers_push/pop` |
| `port/math/matrix.c` | the rest of `Matrix.c` (vectors, matrices, `_Sqrt`, R-register random numbers, `_RemakeNormal`) |
| `port/math/matrix_drive.c` | `sugipon/src/matrixDrive.c`'s VU0 and quadword-copy routines |
| `port/math/quaternion.c` | `sugipon/src/quaternion.c`'s VU0 routines |
| `port/math/cloth.c` | `sugipon/src/clothAnimation.c`'s external VU0 routines |
| `port/math/libvu0.c` | the `sceVu0*` entry points (signatures of `port/compat/libvu0.h`) |
| `port/math/newlib/` | newlib's `rand`, `qsort` and float libm, renamed `ico_*` (below) |
| `port/math/softdouble.h`, `softdouble.c` | the EE's soft-float `double` arithmetic, integer only (below, "Doubles") |
| `port/math/test/` | `math_test`, `newlib_test`, `softdouble_test` (ctest `math`, `newlib`, `softdouble`) |

In the game sources, a routine whose body was all assembly is gone (the
host gets it from `port/math`), and a routine that mixed C and assembly has
the C body in place of the assembly. `typedef.h` includes `ico_math.h`; the
VU0/R5900 opcode wrappers (`VU0_*`, `QCOPY16`, `SYNC`, `DI`, `EI`) are not
defined, so assembly that reaches the host build fails to compile.

## Float semantics

The sim thread runs with round toward zero and FTZ/DAZ
(`port/platform/fpenv.c`) and the build has `-ffp-contract=off`. Under that
mode plain C float arithmetic already gives the PS2's rounding direction,
flushes denormals, lands an overflow on FLT_MAX (the port's Fmax), and
`a * b + c` rounds the product before the sum, which is VU0's unfused
multiply-add. That is why the simulation runs in that mode rather than
calling a helper for every operation: the PS2's rounding comes for free, and
only the cases below need code. The helpers cover what remains:

- `ps2_add(a, b)`, `ps2_sub(a, b)`: the EE adder (PCSX2 PR #12001's
  `PS2Float` model): the smaller operand keeps one bit below its alignment
  shift and the sum truncates, so an effective subtraction can be 1 ulp
  larger in magnitude than IEEE round toward zero, and an operand 25 or
  more binades smaller drops. Integer arithmetic, so the host's rounding
  mode does not reach it. Used in the newlib copies (`port/math/newlib`,
  DIVERGENCES.md F18) and the collision arithmetic (F19); the rest of the
  game code stays plain C (F1).
- `ps2_mul(a, b)`: the multiplier (PR #12001's `PS2Float::Mul`, its Booth
  partial products summed with the low 15 bits discarded): at most one ulp
  below IEEE round toward zero, and not commutative, so the operands go in
  the instruction's order (fs, ft). `1.0f * x` with 1.0 as fs is usually one
  ulp below x. Used in the collision arithmetic only (F19).
- `ps2_div(a, b)`: a divisor with a zero exponent (zero or denormal) gives
  +-Fmax with the sign of `a ^ b`, 0/0 included (VU0 `vdiv`, EE `div.s`);
  a zero dividend gives a signed zero; otherwise the SRT divider of PR
  #12001 (`PS2Float::Div`, `ps2float.c`): the truncated quotient or one ulp
  above it, as the hardware picks (1/3 is 0x3EAAAAAB; DIVERGENCES.md F2).
- `ps2_sqrt(x)`: `sqrt(|x|)` by PR #12001's SRT root (`PS2Float::Sqrt`);
  zero or denormal gives +0 (VU0 `vsqrt`).
- `ps2_rsqrt(a, b)`: `ps2_div(a, ps2_sqrt(b))`, as PR #12001's `Rsqrt`;
  `b` zero gives +-Fmax, or a signed zero when `a` is also zero (PCSX2's
  rule, float-semantics.md open question 5).
- `ico_apply_matrix_ps2`, `ico_set_transpose_matrix_ps2` (`matrix_stack.c`,
  `matrix_drive.c`): `_ApplyMatrix` and `MatrixDrive_SetTransposeMatrix`
  with every VU0 product through `ps2_mul` and every sum through `ps2_add`
  (`vmulax`, then `vmadda*` as a truncated product followed by the adder).
  About 75 times the cost of `ico_apply_matrix` (427 ns against 5.6 ns an
  apply), so only `fumi/src/fieldCollision.c`'s rays use them (F19); every
  other apply stays plain C.
- `ps2_ftoi`, `ps2_ftoi4`: truncate, saturate at +-2^31 by sign (x86 gives
  0x80000000 for both signs); an exponent-255 pattern saturates by its sign
  like `cvt.w.s`.
- `ps2_operand(x)`: a float read from raw memory (a stale stack word, a
  heap word nothing wrote) that has exponent 255 becomes +-Fmax. The EE and
  VU0 have no Inf or NaN and treat such a word as a number of about
  +-2^128; the host would read it as Inf or NaN and spread it. Every matrix
  apply reads w through it (`ico_apply_matrix`, DIVERGENCES.md F17), and
  the sites that read other possibly-unwritten words call it directly
  (F5, F13, F14).

Every division the assembly did through `vdiv` is `ps2_div(1.0f, w)` followed
by a multiply, never `x / w`: the PS2 rounds the reciprocal and then the
product. Plain C `/` and `(int)` in the game's own code are converted to
the helpers site by site, where the `fptrap` preset or a reading shows the
PS2 result differs (DIVERGENCES.md F5). The accepted differences are
DIVERGENCES.md F1-F4 and F7-F10.

## The current matrix

`Matrix.c` keeps a "current matrix" in VU0 registers vf4-vf7 between calls,
one row per register, and pushes it to VU0 data memory through vi15. On the
host it is `ico_current_matrix[4][4]` and a 64-entry stack
(`matrix_stack.c`). VU0 data memory is 4 KB (256 quadwords) addressed modulo
its size, so the stack index wraps after 64 matrices as the PS2's did;
`_InitCurrentMatrix` and `_UnitCurrentMatrix` reset it (`viaddi vi15, vi0, 0`).

Convention (all routines): row `i` of `a x b` is `a` applied to row `i` of
`b`, and `apply(m, v) = m[0]*v[0] + m[1]*v[1] + m[2]*v[2] + m[3]*v[3]`,
summed left to right, all four fields (`vmulax`, `vmadday`, `vmaddaz`,
`vmaddw`). `ico_apply_matrix` is that sum. Where the assembly builds a
rotation or scale matrix in registers and multiplies by it, the C builds the
same full matrix (zeros and ones included) and multiplies, so the zero terms
are added as on the PS2.

### `_PushVu0Registers` / `_PopVu0Registers`

On the PS2 these save vf1-vf31 to VU0 memory and load them back. They wrap
the scheduler thread's `gsb_UpdateGSSystem` (`common/src/main.c`), the
sound thread's tick (`fumi/sound/soundManager.c`) and `iosPadGetStick`
(`fumi/ios/pad.c`), which the game calls directly and which runs `FSqrt`
and `sceVu0Normalize`; on the PS2 both write vf4, a row of the current
matrix. On the host every routine keeps its values in C locals, so the only
register content left is the current matrix, and the host versions save and
restore it (`ico_vu0_registers_push/pop`). This is cheap and deliberately
not a no-op: with cooperative fibers there is no preemption, but the pad
call is a plain nested call, and a no-op would also let any scheduler or
sound fiber code that sets the current matrix leak into the game fiber. The
PS2 did not save Q, R, I or the integer registers, and neither does the
host. `Matrix.c`'s depth assert stays live.

## Routine notes

Fields not listed as written keep their previous value; "w copied" means the
destination's w is the source's w because the assembly's destination mask
left it out of the operation and stored the loaded register whole.

### seki/src/Matrix.c

| routine | what the assembly does |
| --- | --- |
| `_TransCurrentMatrix(v)` | row 3 = apply(cur, v), all four fields, using v[3] (not 1) |
| `_SetTransCurrentMatrix(v)`, `_ClearTransCurrentMatrix` | row 3 = v / (0,0,0,1) |
| `_RotCurrentMatrixX/Y/Z(a)` | cur = cur x R with R rows X: (1,0,0,0), (0,c,s,0), (0,-s,c,0), (0,0,0,1); Y: (c,0,-s,0), (0,1,0,0), (s,0,c,0); Z: (c,s,0,0), (-s,c,0,0); c, s from `GetTableCos/Sin`; -s is `0 - s` (vsubx) |
| `_ScaleCurrentMatrix(x,y,z)` | cur = cur x diag(x, y, z, 1) |
| `_MulCurrentMatrixR(m)` | cur = cur x m (row i: cur applied to m's row i) |
| `_MulCurrentMatrixL(m)` | cur = m x cur (row i: m applied to cur's row i) |
| `_ApplyCurrentMatrix` | apply(cur, src), all four fields |
| `_RotTransPersCurrentMatrix` | v = apply(cur, src); q = 1/v.w (vdiv); xyz *= q; w stored as computed |
| `_RotTransCurrentMatrix` | no caller. As above, then xyz to 28.4 integers (vftoi4), w the float; keeps the last two projected points (vf11/vf12) as history. It also forms a cross product of the last two edges in $f0, reading the 28.4 integers as floats (denormals, so zero on the PS2) under a `void` prototype; the host drops that dead value |
| `_TransposeCurrentMatrix` | full 4x4 transpose (moves done as `0 + x`, F8) |
| `_TransposeRotationCurrentMatrix` | 3x3 only; w column of rows 0-2 and row 3 kept |
| `_InverseCurrentMatrix` | 3x3 transposed (w column kept); row 3 = apply(cur', (-tx, -ty, -tz, 1)) with the last term `vf0 * 1`, so row 3's w = rows 0-2's w column applied to -t, plus 1 |
| `_NormalizeVector` | xyz * vrsqrt(1, x*x + y*y + z*z); w copied |
| `_InnerProduct` | (ax*bx + ay*by) + az*bz; the PS2 function had no C `return` (value in $f0); the host returns it |
| `_OuterProduct` | a x b with VU0's pairing (`a.y*b.z - b.y*a.z`, ...); w = 0 (`vsub.w vf3, vf3, vf3`) |
| `_AddVector`, `_SubVector`, `_ScaleVector` | all four fields |
| `_AddVectorXYZ`, `_SubVectorXYZ`, `_ScaleVectorXYZ`, `_ScaleVector2XYZ` | xyz; w copied from the first source |
| `_InterVector(d, a, b, t)` | a*t + b*(1 - t), all four fields: t weights **a** |
| `_InterVectorXYZ` | the same for xyz, w copied from a |
| `_GetNorm`, `_GetLength` | sqrt((x*x + y*y) + z*z) (vsqrt) |
| `_GetLengthXY`, `_GetLengthXZ` | two-term forms |
| `_FTOI4Vector`, `_FTOI0Vector` | all four fields, saturating |
| `_MulMatrix(d, a, b)` | d = a x b; both inputs loaded before any store, so d may alias either |
| `_UnitRotation` | rows 0-2 of the identity; row 3 untouched |
| `_TransposeMatrix` | pure bit moves (MMI `pextlw`/`pcpyld`) |
| `_InversMatrix` | rows 0-2 = transposed 3x3 with w = 0; row 3 xyz = `0 - (R^T t)`, w = src's row-3 w |
| `_Sqrt` | `vsqrt`: sqrt(|x|) |
| `_MakeNormalLightMatrix` | the three vectors negated (`* -1`), normalised with vrsqrt, transposed into columns; row 3 = (s0.w, s1.w, s2.w, 1): the sources' w pass through |
| `_InitRandom(seed)` | R = seed + 1.0 (VU add) masked to 23 bits, then XOR with (seed + seed) |
| `_GetRandom` | advance R once, return R - 1.0 = (R & 0x7FFFFF) / 2^23 |
| `_GetRandomVector`, `_GetRandomVector0` | no caller. Three advances (x, y, z) / one advance written to x, y and z. The PS2 also stored a stale vf1.w; the host leaves dst[3] alone |
| `_RemakeNormal` via `_MakeNormal3/4/5` | edges from vertex 0; each edge's squared length is summed x, **z**, then y; normalised with vrsqrt; triangle: n1 x n2; quad: (n1 x n2 + n2 x n3) * 0.5; pentagon: four crosses summed in order, * 0.25. **In the quad and pentagon the third (and fourth) "normalised edge" is the register holding the edge's squares** (x replaced by the squared length) times its 1/length (`vmulq.xyz $vf23, $vf18, Q`), not the edge itself. That is what the binary does, and the host keeps it |
| `_PushVu0Registers`, `_PopVu0Registers` | above |

### sugipon/src/matrixDrive.c

| routine | notes |
| --- | --- |
| `CopyVector`, `CopyIVector`, `CopyMatrix` | 16 / 64-byte copies |
| `CopyMatrixUncached` | the PS2 wrote through `dst | 0x20000000` (uncached alias); the host copies |
| `AddVectorXYZ`, `SubVectorXYZ` | xyz, w copied from a |
| `UnitRotation` | identity with row 3 kept |
| `FSqrt`, `VectorLength`, `GetPointDistance` | vsqrt of (x*x + y*y) + z*z (GetPointDistance subtracts all four fields first) |
| `VectorLengthSquare` | (x*x + y*y) + z*z |

### sugipon/src/quaternion.c

| routine | notes |
| --- | --- |
| `MultiQuaternion(o, a, b)` | w = a.w*b.w - ((ax*bx + ay*by) + az*bz); xyz = (b*a.w + a*b.w) + b x a (the cross term is `b.y*a.z - a.y*b.z` etc.); o may alias |
| `RotQuaternionX/Y/Z`, `RotQuaternionEAX/EAZ` | their asm is `MultiQuaternion(self, self, axisQuat)` inline; the host calls it |
| `GetMatrixFromQuaternion*` | q is scaled by sqrt(2) (`quatToMatrixScale` = {1, 1, 1, 1.41421356}), after a reorder to (y, z, x) through the ACC register; with X, Y, Z, W the scaled components: row 0 = (1 - (Z*Z + Y*Y), X*Y - Z*W, Z*X + Y*W, 0), row 1 = (X*Y + Z*W, 1 - (X*X + Z*Z), Y*Z - X*W, 0), row 2 = (Z*X - Y*W, Y*Z + X*W, 1 - (Y*Y + X*X), 0). Row 3 is the C caller's (`ZeroPoint`, or `pos` with w = 1) |
| `RegularizeQuaternion`, `GetQuaternionCosRadian`, `GetQuaternionMagnitude` | four-term dot, x + y + z + w in that order |
| `DivQuaternion` | the game calls `MultiQuaternion` through a cast to `void (*)(int, int, int)`, which truncates pointers on a 64-bit host; the host branch calls it directly |

### sugipon/src/clothAnimation.c and sugipon/include/sugiCommon.h

| routine | notes |
| --- | --- |
| `plane_distance` | ((p.x*n.x + p.y*n.y) + p.z*n.z) + n.w |
| `distance_squared`, `_b` | (dx*dx + dy*dy) + dz*dz |
| `distance_squared_xz`, `xzLengthSquare`, `getXZLengthSquare` | dx*dx + dz*dz |
| `fSqrtInv_i`, `xzInvLength_i`, `getXZInvLength` | vrsqrt(1, .) |
| `scaleVectorXZ_i`, `scaleVectorXZ` | x and z scaled, y and w copied |
| `subAndGetInvLength_i`, `subAndGetInvLength` | d = a - b (all four fields), returns vrsqrt of its xyz length |
| `scaleAndAddVectorXYZ_i`, `scaleAndAddVectorXYZ` | (a.xyz + b.xyz * k, a.w) |
| `tensionMove`, `tensionMoveNoReduce` | buf = a - b; inv = vrsqrt; out = (b.xyz + buf.xyz * (k * inv), b.w), the first only when inv < lim |
| `FSqrtInv(void)` | declared `void (void)` but computes 1/sqrt($f12) into $f0; no callers, so the host version does nothing |

### Other game files

| site | notes |
| --- | --- |
| `sugipon/src/motionManager2.c` `CopyMotionWithNodeHrc` | not VU0: its GNU nested function `copyMotionWithNodeHrc` is a file-scope function with the captures as parameters (same recursion order), so clang compiles the file |
| `sugipon/src/motionManager2.c` `motSqrtStart/End` | the root travels in VU0's Q between the two calls; the host keeps it in a file static |
| `ito/src/itou_sub.c` `apply_matrix_w1` | apply(m, (v.xyz, 1)) (row 3 times `vf0.w`), all four fields |
| `sugipon/src/stormTest.c` `StormStoreI4` | `_FTOI4Vector` |
| `sugipon/src/enemy.c` `dispEnemyObject` | z of the part origin through the current matrix, times 1/w, to int; the PS2 store also wrote a stale vf13 into the entry's other words, which nothing reads |
| `seki/src/BgAnimation.c` `_RotTransCurrentMatrixYXZ` | row 3 = apply(cur, t), then cur x Ry, x Rx, x Rz, each a full matrix (traced from the hand-interleaved block) |
| `sugipon/src/lineManager.c` `perspLine` | a point behind z = 1 slides to z = 1 (`q = (1 - a.z) / (b.z - a.z)` by vdiv, a.xy += (b - a).xy * q, a.z = 1, a.w kept); both ends then apply(cur, .) with all four fields times 1/w |
| `lineManager.c` `clipAtX/Y` | the edge point at x (y): q by vdiv, the other two fields `(b - a) * q + a`; the PS2 stored a stale register word in d.w, the host leaves it |
| `seki/src/Shadow.c` | `applyWeightedVtx`: dst.xyz += apply(cur, (src.xyz, 1)).xyz * w. `applyCurrentMatrixV`: apply(cur, (src.xyz, 1)). The shadow-volume helpers keep vf1 (direction), vf10-vf15 (last three top/bottom vertices) and vf20-vf25 (their projections) between calls; the host keeps them in a file-static struct. `loadVolumeMatrix` sets the current matrix, as on the PS2. The projection is apply with w = 1, then all four fields times 1/w; the facing value is dir . ((v0 - v1) x (v2 - v1)) summed x, y, z; the edge slides are `ob*(1 - r) + oa*r` (or the mirror) plus the screen origin in all four fields |

### port/math/libvu0.c (sceVu0*)

Implemented: ApplyMatrix, MulMatrix (dst may alias), OuterProduct (w = 0),
InnerProduct, Normalize (vsqrt then vdiv 1/x: two roundings, w = 0; unlike
`_NormalizeVector`, which keeps w and uses vrsqrt), TransposeMatrix,
InversMatrix (same sequence as `_InversMatrix`), DivVector(XYZ) (times
1/q), InterVector(XYZ) (t weights a), Add/Sub/Mul/ScaleVector(XYZ),
TransMatrix, CopyVector/Matrix/VectorXYZ, FTOI0/4, ITOF0/4 (`(float)i`
then * 1/16), UnitMatrix, RotMatrixX/Y/Z, RotMatrix (Z, then Y, then X),
ClampVector (vmax with min, then vmini with max), RotTransPers (projection,
then vftoi4 in all four fields; mode != 0 gives integer z and w via
vftoi0), RotTransPersN, CameraMatrix, NormalLightMatrix, LightColorMatrix,
and `sceVpu0Reset` as a no-op (VIF0 hardware).

`sceVu0RotMatrix[XYZ]` do not use the game's sine table. They reduce the
angle with the EE FPU (`pi/2 + a` for a < 0, else `pi/2 - a`, with pi/2 =
0x3FC90FDB), evaluate libvu0's odd polynomial (coefficients 1/9!, -1/7!,
1/5!, -1/3! as stored) on it, which yields the cosine p, and take the sine
as `+-sqrt(1 - p*p)` with the sign chosen by the branch. Each power term is
built by repeated multiplication by a^2 and the sum is a + t3 + t5 + t7 + t9
in that order.

Not implemented (no caller in the game, and status-flag based):
`sceVu0ClipScreen`, `sceVu0ClipScreen3`, `sceVu0ClipAll`; nor the plain C
members `sceVu0ViewScreenMatrix`, `sceVu0DropShadowMatrix` and libvu0's
`memclr` (no game caller; on the PS2 `sce/libdma`'s `sceDmaReset` calls it).

## Register side effects

On the PS2 most libvu0 routines and several game helpers (`FSqrt`,
`VectorLength`, `GetPointDistance`, `AddVectorXYZ`, `SubVectorXYZ`,
`apply_matrix_w1`, the cloth helpers, `_InversMatrix`, `apply_m34`) write
vf4-vf7, where `Matrix.c` keeps the current matrix. The host routines touch
only their arguments. The two differ only where the EE reads the current
matrix after such a write without loading it again (DIVERGENCES.md F10).

`tools/vu0_clobber_scan.py` decides that on the EE's code: it disassembles
the retail ELF's `.text` (checked equal to the decomp's symbol build) with
`mips-linux-gnu-objdump -m mips:5900` and runs a per-function dataflow over
the control-flow graph (delay slots, jump tables from `.rodata`, callee
summaries, the push stack), tracking each of the 16 lanes of vf4-vf7 as
"set by a current-matrix routine the host implements", "the caller's",
"written by a routine the host does not mirror", "after a thread switch"
(the EE kernel does not save VU0 registers), "after an indirect call" or
"popped from a caller's push". The header comment lists the rules. Over all
5767 functions it finds no current-matrix read of a lane another routine
wrote: every reader in the game loads the matrix itself
(`_SetCurrentMatrix`/`_InitCurrentMatrix`) before reading it, or is a leaf
reader (`gsb_ClipBox`, `gif_Draw*`, `reg_dispPoint`/`reg_dispLine`,
`setLight`, `draw`/`drawHT`, `makeRefractST`, `bga_CalcObject`) whose every
caller loads it immediately before the call. The only lanes read after a
foreign write are libvu0's own register passing: `_sceVu0ecossin` returns
cos and sin in vf4/vf5 to `sceVu0RotMatrix{X,Y,Z}` and first multiplies
vf4.yzw by vf0.x = 0 (0x25D768), so the stale lanes drop out (VU0 has no
NaN or infinity) and are overwritten before use.

Each candidate of the earlier name-based scan (call order, routines
resolved transitively by name, no control flow), with the sequence the
scan saw (EE addresses of the calls) and where the reads on that path take
the matrix from:

| candidate | verdict | evidence | action |
| --- | --- | --- | --- |
| `seki/src/BgAnimation.c` `bga_CalcObject` | false positive | the two hits are in different `switch` cases; its reads take the matrix from the caller, `bga_CalcAnimation`, which sets it (`_SetCurrentMatrix`/`_InitCurrentMatrix`) right before the call (0x209934) | none |
| `fumi/src/way_tool.c` `draw_way_group`, `way_toolDL` | false positive | `sceVu0UnitMatrix` (0x21764C) then `DrawLine` (0x217664); the read is in `_getLine`, which sets the current matrix first | none |
| `fumi/src/enemy_act.c` `subEnemyCollision` | false positive | set in `ACTGame_CommonLoop` (0x164B88), clobber in `CommonAttackCenter` (0x164B94), read under `DispMultiBgaManagerWithKind` (0x164E70): the readers there (`bga_CalcAnimation`, the `reg_set*MatrixPacket` and shadow routines) each set the matrix before reading; the `_ACTWait` thread switches are followed by no read before a set | none |
| `ito/src/queen.c` `QueenBallDL` | false positive | set in `SetupDarkVolume` (0x1A44BC), clobber in `p2o_DispVU1Default` (0x1A44CC) and `sceVu0Normalize`/`sceVu0OuterProduct`, read under `stage_DispBgAnimation` (0x1A44EC, 0x1A45DC): every reader there sets the matrix first (`bga_CalcAnimation`, `reg_setMMatrixPacket`, `reg_setNMatrixPacket`, `light_getAmbientLight`, `_getLine`, `renderViewCoordZSphere`) | none |
| `seki/src/RegistPacket.c` `reg_DispObj` | false positive | set in `reg_dispPointLineObj` (0x1243D0), clobber in `reg_dispMObj` (0x1243D8), read in `reg_dispNObj` (0x124424): `reg_dispNObj` and `reg_setNMatrixPacket` load the object matrix with `_SetCurrentMatrix` before reading | none |
| `sugipon/src/boy.c` `BoyDL` | false positive | set in `stage_PlayBgAnimation` (0x1CC53C), clobber in `p2o_DispVU1` (0x1CC578), read in `dispSubParts` (0x1CC580): its readers (`reg_set*MatrixPacket`, `reg_dispNObj`, `light_getAmbientLight`, `shadow_Entry*`) set first; `SetLimitedPoolReflactionMesh`/`DispLimitedPoolReflactionMesh` set with `_SetCurrentMatrix` too | none |
| `sugipon/src/boy.c` `BoyGeo` | false positive | set in `ExecMotionOrient` (0x1CC2B8, debug lines through `DrawLineG`), clobber in `synchronizeMotionOutputOriginForGirl` (0x1CC2C0), read under `SetActressLight` (0x1CC2E4): the readers set first; `CylinderCollision`, `HandManager`, `actionOfWater` only clobber | none |
| `sugipon/src/clothAnimation.c` `GetChainAnimation` | false positive | nothing under it sets or reads the current matrix on the EE (`sceVu0UnitMatrix`, `AddVectorXYZ`, `sceVu0ScaleVector`, `calc2`, `sceVu0ApplyMatrix` only clobber) | none |
| `sugipon/src/enemy.c` `DisplayEnemy` | false positive | set in `reg_DispEnemy` (0x1D9878), clobber in `DispEnemyEye` (0x1D989C), read in the second `DispEnemyEye` (0x1D98A4): it reads through `reg_DispMultiPri` -> `reg_setMMatrixPacket`, which sets first; `dispEnemyObject` sets with `_SetCurrentMatrix` (0x1D8DB4) before its inline reads | none |
| `sugipon/src/flag.c` `FlagDL` | false positive | set in `p2o_DispVU1Multi` (0x1DBB50), clobber in `light_MakeLightMatrix` (0x1DBB60), read in `DispClothMesh` (0x1DBB78): the read is `prim_makeNormal`'s, after its own `_SetCurrentMatrix` | none |
| `sugipon/src/girl.c` `GirlGeo` | false positive | set in `ExecMotionOrient` (0x1DD7C4), clobber in `SetActressLight` (0x1DD7D8) and `FSqrt` (0x1DD8E8), read in `execClothes` (0x1DD944): the reads are `getCloth4D_preProcess`'s and `_getLine`'s, each after its own `_SetCurrentMatrix` | none |
| `sugipon/src/item.c` `ItemDL` | false positive | set in `stage_DispBgAnimation` (0x1DFFC4), clobber in `bombSparkSE` (0x1E0064, `sceVu0Normalize` in the sound position), read in `p2o_DispVU1` (0x1E0084): its readers set first | none |
| `sugipon/src/pool.c` `PoolDL` | false positive | set and clobber in the two `DispMultiBgaManagerWithKind` calls (0x10C5AC, 0x10C5BC), read in `updatePoolGeo` (0x10C650): `updatePoolGeo` and `dispPool` load the matrix with `_SetCurrentMatrix` before reading | none |
| `sugipon/src/switch.c.inc` `FloorLeverDL` | false positive | set in `p2o_DispVU1` (0x1C6300), clobber in `GetRootMatrix`/`MatrixDrive_RotMatrixZ`/`X` (0x1C631C-0x1C632C), read in `p2o_DispVU1DObj` (0x1C6348): its readers set first | none |
| `sugipon/src/switch.c.inc` `WallLeverDL` | false positive | as `FloorLeverDL` (0x1C66B8, 0x1C66D4-0x1C66E4, 0x1C6700) | none |
| `sugipon/src/weapon.c` `dispLaserSword` | false positive | set in `p2o_DispVU1` (0x2022F8), clobber in `MatrixDrive_TransMatrix`/`ScaleMatrix` (0x202338, 0x202358), read in `p2o_DispVU1DObj` (0x202374, 0x2023CC): its readers set first | none |
| `sugipon/src/windmill.c` `InitWindMillGeo` | false positive | set in `SetFlag4PointFixID` (0x204458), clobber in `CreateLayoutedGObj` (0x20448C), read in the next `SetFlag4PointFixID` (0x2044B0): the read is `prim_makeNormal`'s, after its own `_SetCurrentMatrix` | none |

Side finding, no effect: `bga_CalcSdfCamera` calls `_InitCurrentMatrix`
between its `_PushCurrentMatrix` and `_PopCurrentMatrix`; the init resets
vi15 to 0, so the pop loads quadwords 252-255 of VU0 memory (the host pops
stack slot 63, the same addresses) and `stage_CalcAnimationNoParent`
returns with that matrix. The scan finds no read of it before the next set.

## Doubles

The EE has no double hardware. ee-gcc compiled every `double` operation as
a call to libgcc's soft float (`sce/libgcc/dp-bit.c`, `fp-bit.c`: `dpadd`,
`dpsub`, `dpmul`, `dpdiv`, `dpcmp`, `litodp`, `dptoli`, `dptofp`,
`fptodp`), which rounds to nearest even, reads a denormal operand as zero
(`NO_DENORMALS`, defined at the top of `dp-bit.c`), truncates a result below
2^-1022 into a denormal, and in `dptoli` truncates and saturates. On the
host the same C would run as SSE doubles under the simulation thread's
round-toward-zero mode (DIVERGENCES.md F6). The object census in
float-semantics.md lists the functions that call those routines: 33 in 20
files, plus the two static inline helpers expanded into them
(`lt_glow_sprite` in `display_texture`, `battleRangeScale` in
`Battle_isCurrentStatus`):

| file | functions |
| --- | --- |
| common/src/debug | debug_PrintFontf |
| common/src/layout_texture | display_texture (lt_glow_sprite) |
| fumi/src/act-wish | ACTGetWish_FromPad |
| fumi/src/boyact | actBoyRun, actBoyWalk |
| fumi/src/commonact | WithMailFunc_FallDead, actCommonFall |
| fumi/src/enemy_act | Battle_isCurrentStatus (battleRangeScale), NakaBoss, actEnemyKidnapEnd |
| fumi/src/girl_act | HandMgr_Speed (girl_act_hand.c.inc), subGirlBrain_Attract (girl_brain_attract.c.inc) |
| omori/src/attackhit | inner_check |
| omori/src/brain | brainLevelProcess |
| omori/src/camera-ico2 | monitorMonitorCamera |
| omori/src/camera-root | SetCameraMatrix |
| omori/src/chain | chain_simulate_term_{down,moveup,free,loop,swingready,swingstart}, pendulum_Process |
| script/src/script | scpWoodSrh |
| script/src/st04a, st04e, st05e, st06a, st13b, st25a | actSt04aGateChk, actSt04eSeChk, actSt04eWaterFlagOn, actSt05eWaterFlagOn, actSt06aSuimonFlagOn, actSt13bConte02, actSt13bElev2Chk, actSt25aElevChk |
| sugipon/src/waterDot | setWaterDot |

**`port/math/softdouble.c`** computes the EE's results with integer
arithmetic only, on the IEEE bit patterns (`uint64_t` for a double):
`ico_dadd`, `ico_dsub`, `ico_dmul`, `ico_ddiv`, `ico_dcmp`, `ico_i2d`,
`ico_d2i`, `ico_d2f`/`ico_d2f_bits`, `ico_f2d`/`ico_f2d_bits`, and
`ICO_D(lit)` for the bits of a double constant. `ico_dcmp` returns
`dpcmp`'s -1, 0 or 1 (1 when either operand is a NaN), and each call site
tests it against 0 with the source's operator, as the compiled code did
(the census objects pass the operands in source order, the constant
second). It is written from the IEEE-754 definition plus the library's
policies (the header comment of `softdouble.c` lists them: 8 guard bits
with a sticky bit, the product's and quotient's tie rule, the NaN signs and
the static NaN for invalid operations, `dptofp`'s sticky bit and float
packing, `fptodp`'s NaN fraction); no libgcc code is copied, so the file is
MIT. For results in the normal range the library is plain round-to-nearest
IEEE (20 million random pairs of normal operands: no difference from the
host's own round-to-nearest add, subtract, multiply or divide); it differs
in the denormal policy and the NaN details.

The double expressions in the listed functions are written as calls in the
original evaluation order: each `float` operand that C promoted goes
through `ico_f2d`, each `int` through `ico_i2d`, a result assigned to a
float through `ico_d2f`, to an int through `ico_d2i` (`GetTableSin`'s
`short` argument takes the low half of the int, as on the EE), and a
compound `x *= 0.8` becomes `x = ico_d2f(ico_dmul(ico_f2d(x), ICO_D(0.8)))`.
`actSt13bConte02`'s `... * 1.0` is `ico_d2i(ico_i2d(...))`: ee-gcc folded the
multiply (the object has `litodp` and `dptoli` there and no `dpmul`), and a
product by 1.0 is exact in the library anyway. Variadic float arguments
(`debug_Printf`, `debug_StdPrintfDummy`, `sprintf` in `debug_PrintFontf`)
are passed as `ico_dval(ico_f2d(x))`, and `debug_PrintFontf` reads its `%f`
argument as `ico_d2f(ico_dbits(va_arg(ap, double)))`. Every `ICO_D`
constant's bits are the ones in the EE object (its `.rodata`, or an
immediate for a constant whose low 48 bits are zero).

The gate is `tools/softdouble_gate.py` (ctest `softdouble_gate`): in those
35 functions' source, no `double` keyword (bar `va_arg(ap, double)`) and no
unsuffixed floating constant outside `ICO_D()`; with the build's
`compile_commands.json`, the 20 files compiled with `-Wdouble-promotion
-Wfloat-conversion` give no warning about a double inside them. Run on the
sources before this change it reports 59 double constants and 2 `(double)`
casts from the source pass and 71 warnings from the compile pass.

The proof is `port/math/test/softdouble_test.c` (ctest `softdouble`), whose
oracle is the library itself: `sce/libgcc/dp-bit.c` and `fp-bit.c`, copied
into the build by `port/math/test/dpbit_harness.cmake` and compiled with
their exported names prefixed `ref_`. The reconstructions match the EE
objects byte for byte, so a few of their functions are declared `void` or
`int` and leave a 64-bit result in `v0` (or a float in `$f0`) from their
last call; the harness gives those a host return type and a `return`, and
spells the EE's 64-bit `long` arguments `long long`, each edit checked to
apply exactly once. Linux builds only: the libraries' bitfield unions need
GCC's layout, not mingw's `ms_struct`. The test compares about 65 million
results bit for bit: every pair of 30 special values (zeros, denormals,
Infs, quiet and signalling NaNs of both signs, the int and float edges), 1.5
million rounds of random pairs (random bits; chosen sign, exponent and
fraction classes; pairs close enough to cancel or to straddle the 64-bit
alignment limit; pairs whose product or quotient lands at the denormal or
overflow edge), `d2i` around +-2^31 and +-0.5, `d2f` at the rounding ties
and their neighbours for every float exponent from the float denormal range
to overflow, `i2d` at the int edges and on a stride through every int, and
`f2d` on every float exponent and 4 million random floats.

## newlib copies

`port/math/newlib/` holds `ico_rand`/`ico_srand` (`sce/libc/stdlib/rand.c`,
state 1 at start as in `sce/libc/reent/impure.c`), `ico_qsort`
(`sce/libc/stdlib/qsort.c`, same comparison and swap order), and in
`ico_libm.c` `ico_atan2f`, `ico_acosf`, `ico_asinf`, `ico_sinf`,
`ico_cosf`, `ico_fmodf`, `ico_sqrtf` with their fdlibm helpers made static.
Licence notices: SunPro fdlibm for the libm file, the UCB BSD notice for
qsort, newlib's Red Hat notice for rand (written from the texts quoted in
`docs/research/licences.md`).
`port/compat/math.h` and `stdlib.h` `#include_next` the host header and,
under `ICO_HOST`, include `port/compat/ico_libc.h`, whose macros map the
game's calls to the copies without touching call sites.

Not a straight copy: ee-gcc constant-folded parts of these functions with
round to nearest (`ef_sqrt`'s rounding probe, the constant returns of
`ef_atan2` and `ef_acos`), and host compilers fold a different set. The
copies spell out the folded values the EE objects contain (checked with
objdump) and keep `sf_atan`'s `atanhi[3] + atanlo[3]` a run-time sum as on
the EE. The X/Open wrapper results (`_LIB_VERSION` is `_XOPEN_`, `matherr`
returns 0) are reproduced: `atan2f(0, 0)` = +0, `acosf`/`asinf` out of
domain = 0, `fmodf(x, 0)` = 0x7FB00000 (the folded `0.0/0.0` passed
through `dptofp`; from the reconstructed libgcc, not checked on hardware).

**`cosf` is not linked in the PS2 game.** The decompilation's link map lists
`sf_sin.o`, `kf_cos.o` and `kf_sin.o` but no `sf_cos.o` and no `cosf`
symbol; the ELF has no `cosf` (nor `sqrtf`), and no object of the game or
of `sce/` references either. `ico_cosf` exists for
completeness, written in `sf_sin.c`'s style, and is unverified.

## Tests

`ctest` runs `math_test`, `newlib_test` and `softdouble_test` (ctest `math`,
`newlib`, `softdouble`; the last is described under "Doubles") and the
`softdouble_gate` check.
`math_test` runs in the sim FP mode and checks:

- the helpers bit for bit: division by +-0, 0/0 and a denormal divisor
  (+-Fmax), 1/3 rounding toward zero, sqrt/rsqrt of negative, zero and
  denormal inputs, ftoi saturation at +-2^31 and Fmax, ftoi4, and the mode
  itself (overflow to Fmax, denormal results and inputs flushed);
- multiply-add not fused (`(1 + 2^-12)^2 - 1` = 2^-11 exactly);
- R: seed bits 0x3F9E0651, seed + 1 = 0x400F0328, R after vrinit
  0x3F8F0328, after vrxor 0x3F910579, the next six R values and
  `_GetRandom` results from float-semantics.md;
- matrix multiply, apply, current-matrix multiply/apply/translate, transpose,
  inverse, quaternion rows and product, normalise, inner/outer product,
  lengths and interpolation against double-precision references (relative
  tolerance 2e-6, a few ulp); sceVu0 and Matrix.c twins bit-equal; aliasing
  (dst == source) for the multiplies;
- exact results: identity multiplies, `_InitCurrentMatrix`, quarter-turn
  rotations about X, Y and Z, push/pop and the register save/restore,
  projection with w = 0 giving Fmax rather than Inf, the triangle normal and
  the quad normal's squared-edge term;
- stale w lanes (`test_stale_w`): an apply whose w holds an exponent-255
  pattern gives the rotation alone over a zero translation row, as on the
  EE.

`sceVu0RotMatrix[XYZ]` are checked against `cos`/`sin` at 81 angles over
[-pi, pi] (tolerance 2e-5: libvu0's polynomial is that accurate).

Open items (the untriaged register candidates, the R LFSR on hardware)
are in `docs/TODO.md`.
