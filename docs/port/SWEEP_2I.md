# Package 2I: the x64 bring-up to Phase 2's exit criterion

Scope: the x64-only NaN in skeleton node 33 that package 2H left open
(BOOT_DIAG.md, "Package 2H"), every further x64 divergence the per-tick traces
showed up to 3000 Main ticks, the literal-size allocations 2H listed, and one
`ICO_HEAP_ASAN` run. Base `7f67ed92`. All fixes are under `ico2/**`, each an
`#ifdef ICO_HOST` (or an `ICO_RAW` / `ICO_MAX_SIZE` view, the original on the
EE).

## Result

- `linux-x64` and `ref-m32`, headless, `ticks=3000`, `pad-boot.txt`: both run
  3000 Main ticks, 6007 vsyncs, to stage 3 (stage 1 -> 41 at tick 663, -> 42
  at 756, -> 43, -> 45, -> 40, -> 3). The per-tick traces
  (`logs/trace-*.txt`) are byte-identical: Phase 2's exit criterion.
- `asan` with `ICO_HEAP_ASAN` (BOOT_DIAG.md, "How it was found"): one report,
  fixed (below); the run after the fix is clean to 3000 ticks and its trace is
  also identical to `ref-m32`'s.
- `fptrap`: runs past the old stop (vsync 6) and past tick 117; it now stops
  at Main tick 1011 (stage 40) on a NaN reaching `_FTOI4Vector`
  (`port/math/matrix.c:134`, inlined; the caller did not resolve). Not
  followed further (remaining items).
- EE byte identity: the 20 touched TUs (`s_init.c`, `act-env.c`, `act-way.c`,
  `act.c`, `boyact.c`, `commonact.c`, `enemy_act.c`, `girl_act.c` (for
  `girl_brain_attract.c.inc`), `way_tool.c`, `camera-ico2.c`, `camera-root.c`,
  `script.c`, `GsBase.c`, `StageAnimation.c`, `flag.c` (for `flag.h`),
  `motionManager.c`, `motionManager2.c`, `motionOrientManager.c`,
  `staticBlur.c`, `weapon.c`) compile with `tools/compile_c.sh` to the same
  `.text`, `.data`, `.rodata`, `.sdata`, `.bss`, `.sbss`, `.lit4`, `.lit8` and
  relocations as a `HEAD` worktree (BOOT_DIAG.md, "Package 2I", says how
  it was run).

## The node 33 NaN

`InitMotionGeoInfo` (`sugipon/src/motionManager2.c`) initialises every
actor's motion root block with `*(MotionGeoInfo *)self = template`, a struct
copy of a file-local record over a `struct MotRoot` (typedef.h). The two
declare the same EE layout with different types: the template has
`int; WallCfg; int` at 0x10C where MotRoot has `char[20]`, a `Vec4` plane
(8-byte aligned) where MotRoot has a 16-byte aligned `Vec16`, and an `int`
`fixObj` where MotRoot has an `ICO_WORD`. On x64 `WallCfg` is 24 bytes, so
every field from `filter` (0x120) to `fixQuat` landed 8 or 16 bytes off.
`armTwist` read as 0 0 0 0, the kind 1 node (`motMan_getFinalMatrix.c.inc`,
`MultiQuaternion(cur, skelRoot->armTwist, cur)`) went to the zero quaternion,
every child followed, and node 33's `RegularizeQuaternion` divided by
`sqrt(0)`: NaN, then `windStrength[(int)NaN]` in `windField.c`. The host
template now has MotRoot's types at those three places, and a
`_Static_assert` checks size and four offsets. Found by probes on both
builds (node quaternions, then the twist inputs, then the two layouts
compared with `offsetof` on x64 and i386).

## The other divergences, in the order the traces showed them

| where | fault on a 64-bit host | now |
| --- | --- | --- |
| `seki/src/GsBase.c` `gsb_SetVSMatrixSub` | the fptrap stop: `vs[0]` (the zoom) is 0 before the first stage; `x / 0` (the PS2 also divides) | `ps2_div` for the four divisions by `vs[0]` (DIVERGENCES.md F5, F11) |
| `sugipon/src/staticBlur.c` `calcSun` | `1 / buf[3]` with a zero view matrix on a stage's first tick, then `Inf * 0` | `ps2_div` (F5) |
| `fumi/src/act.c` `act_check_mail`, `act_check_intr_list` | read the interrupt list at `self + 0x54` (GObj's mail box on the EE; on x64 the high half of `dlNext` and the low half of `dlPrev`); `BeforeFunc` walked the mail entries with the EE's 8-byte stride | the GObj's `mailBox` by name, `IosMailBox` fields |
| `fumi/src/act.c` `act_check_intr_list` | `*(int *)(p + 0xC)` on `SetMotionRequest`'s result and `*(unsigned short *)((char *)m + 0x16)` on an `IntrMail` (its `flags` is at 0x24 on x64) | `MotCtrl.shifted`, `m->flags >> 16` |
| `sugipon/src/motionOrientManager.c` `SetMotionRequest` | returned `GOBJ_SUB + 0x470`, the EE offset of `ctrl`; every caller reads it as a `struct MotCtrl *` | host returns `&GOBJ_SUB(self)->ctrl` |
| `fumi/src/act-env.c` | the `EnvMotion` view of the same record (0x110, 0x114, 0x130, 0x138) | `MotCtrl` fields `cliffHeight`, `cliffDist`, `wallFloorHeight`, `wallDist` |
| `script/src/script.c` `scpPlayWaitMotEnd` | `motReq + 0x5C` | `MotCtrl.frameEnd` |
| `fumi/src/commonact.c` `test_CURRENTROOT` | the scratch vector at Act + 0x100 spelled as `&((GObj *)act)->mailBox + 172` | `Act.pad100` |
| `sugipon/src/motionManager.c` `GetMatrixOfMotion` | `objLayout[*(int *)(self + 8) * 76 + 70]`: GObj + 8 is `labelType` on x64, and a `GenGeo` row is not 76 bytes there | `((GenGeo *)objLayout)[self->labelId].kind` |
| `fumi/sound/s_init.c` `soundVBlank`, `soundDataSegAllClose`, `soundDataSegNextStageNotUseClose` | walked `soundDataTbl` by 0x30 bytes to 768; `SqEntry` is 0x48 on x64. The ADPCM tick (`adpcmTickProc2`) never ran for the second entry on, so stage 41's event stream was never closed, the background CD manager kept a third entry, `stgmgrNextStagePreLoad` never preloaded stage 42, and its load took 28 ticks longer: the first trace difference, tick 802, gflag word 0 | `SQ_STRIDE` / `SQ_END` (`sizeof`) on the host |
| `fumi/src/commonact.c` `FALL_SUB` | GObj + 0x15C | `GOBJ_SUB` (the SIGSEGV at tick 433) |
| `fumi/src/boyact.c` `BOY_EXT`, `GOBJ_SUBSLOT`, `searchEnemy` | Act at GObj + 0x164, the `BoyExt` view of `EnemyBattleWork` (EE offsets), GObj + 0x15C, GObj + 0xC as `kind` | `BOY_EXT_F` names the `EnemyBattleWork` field (`liftLevel`, `liftedObj`, `word2C0`, `holdObj`, `holdPoint`); `GOBJ_SUB`; `kind` |
| `sugipon/include/flag.h` `FLAG_ALLOC_NODES` | stored the new node matrices and quaternions at Sub15C + 0xC and + 0x10 (on x64 `parent.node` and `nodeNum`), leaving `nodeMtx` pointing at the block just freed: the heap check at `memory.c:598`, tick 828 | `nodeMtx` / `nodeQuat` by name (`FLAG_NODE_WORD`) |
| `fumi/src/girl_brain_attract.c.inc` (2 sites), `fumi/src/act-way.c` | GObj + 0xC read as `kind` (0x10 on x64) | `kind` |
| `fumi/src/enemy_act.c` (4 sites) | GObj + 8 read as `labelId` (0xC on x64): `GetMotherGenerator` asserted in `GetStageFromLabel` at tick 1009 | `labelId` |
| `omori/src/camera-ico2.c` `targetAPrev`, `targetBPrev` | `float[3]` written by a 16-byte `sceVu0ScaleVector`; on the EE each sits in its own 16-byte `.bss` slot (`camera-ico2.o`: 0x490, 0x4A0), on the host the fourth word overran the next global (the `ICO_HEAP_ASAN` run's one report) | four words on the host |
| `omori/src/camera-root.c` `SetCameraMatrix` | `/ (zoomRangeMax - zoomRangeMin)` with an empty range (fptrap, tick 491) | `ps2_div` (F5) |
| `sugipon/src/weapon.c` `initializeQueenzSword` | `length * 0 / 0.0f`: +Fmax on the EE, NaN on the host (fptrap, tick 798) | `ps2_div` (F5) |

Static pass for the same raw GObj views (`grep -a` for
`((char *)self|gobj|obj|gop) + 0x..)` outside `ICO_RAW`): the two left
(`act.c:283`, `frameDependSequence.c:396`) are in `#else` (EE-only) branches.

## Literal-size allocations

Every `iosMalloc*` / `mallocseki*` call whose size is a literal or a literal
stride (`grep -a -nE` as in the package brief, plus `* N` strides), compared
with `sizeof` of the record on x64 and i386 (a script preprocesses each TU
with the preset's flags and reads `sizeof` back).

Converted (the record is runtime-class and larger on x64):

| site | record | EE | x64 |
| --- | --- | --- | --- |
| `seki/src/StageAnimation.c:1045` | `BgaPlayNode` | 64 | 80 (list links) |
| `fumi/src/way_tool.c:678` | `Act` (the way tool's cursor; debug only) | 0x850 | 0x9E0 |

Already right on the host (2D to 2H, or the size fits): `common/src/DObj.c:318`
(`LightMatrix` 256), `DObj.c:439` (53 bytes of `char`), `omori/src/lws_kyomi.c:43`
(`HintInfo` 16), `fumi/src/way_util.c:350` (95 `char`), `fumi/ios/thread.c:258,278`
and `fumi/ios/message.c:133` (`#ifdef ICO_HOST` sizes), `fumi/sound/s_init.c:1403`
(a `float[4]`), `seki/src/BgAnimation.c:384` (48 for `BgaParticleEnt`, 40 on
x64), `BgAnimation.c:2064` (host branch), `sugipon/src/box.c:449-455`,
`boy.c:607-613`, `a_p_1.c:239-282`, `cage.c:150`, `worm.c:392-398`,
`rope.c:156-162`, `enemyParts.c:202`, `chain.c:757`, `DObj.c:259,334,335`,
`flag.h` (64-byte matrices, 16-byte quaternions, 80-byte `DObjNode`, the same
on every host), `boy.c:446-452` (`float[100]`, 20 `LLVec`), `weapon.c:619-644`
(352 bytes of vectors), `worm.c:341,366` and `cage.c:102` (`ChainCfg` 80,
`WormVec` 16 on both), `moveColTest.c:15` (`short[6]`), `Primitive.c:32,321-342`
(`Fan2DVtx` 32, `Prim3DVec` 16), `Shadow.c:1323-1374` (`ShadowVtx`,
`ShadowPoly`, `ShadowRun` 16), `Packet.c:920-1269` (`PObjMaterial` 112,
`PacNode` 32, `PObjTexInfo` 80, `PacLine` 192), `Texture.c:867`
(`TexLevelPkt` 80), `clothAnimation.c:905-1929` (`ClothPoint` 64, vectors),
`flag.c:102` (the 48-byte anchor rows, no pointers), `multiBgaManager.c:36`
(`BgaDisp` 80), `stormTest.c:78-80`, `debug.c:1218` (quadwords).

Left as they are:

| site | why |
| --- | --- |
| `common/src/PObj.c:281,283,287,539` | in the EE-only `#else` of `MakePacket` / `InitPObj`; the host branches allocate `sizeof(Sub15C)`, `sizeof(LightMatrix)`, `sizeof(PObjModel)` |
| `fumi/src/act.c:277,374` | EE-only `#else` branches (the host has its own `actInitialize`) |
| `omori/src/camera-ico2.c:983` | the `.gcm` disc records: `CamSetFile` 16, `CamGroup` 76, `PinRec` 92 on every host (`_Static_assert` there) |
| `omori/src/camera-editor.c:1279` | the editor's 100-entry `PinRec` pool (9200), frozen records; debug only |
| `fumi/ios/mblock.c:32,36` | an 8 KB inflate block holding raw bytes |
| `seki/src/DmaPacket.c:16,18`, `seki/src/Packet.c:772`, `sugipon/src/streamMotionManager.c:417,418` | raw DMA, packet and stream byte buffers |
| `fumi/ios/ios.c:96-109` | the partition sizes, not records. Whether the larger x64 records fit them over a whole playthrough is not measured; 3000 ticks fit |

## Remaining

- `fptrap` stops at Main tick 1011 (stage 40): a NaN reaches `_FTOI4Vector`.
  The per-tick traces do not show it (they are identical through 3000
  ticks), and `ref-m32` was not probed for the same NaN. It is the next F5
  site to locate; `port/math` is outside this package.
- The `ICO_HEAP_ASAN` run checks writes only and only the paths these 3000
  ticks take. The raw-offset patterns fixed above (GObj + 8 / 0xC / 0x54 /
  0x15C / 0x164, byte strides over runtime tables, template copies between
  two declarations of one record) are worth a whole-tree `grep -a` pass
  beyond the files touched here; this package searched for the GObj forms
  only.
- 2H's `seki/src/Packet.c` line-record end mark (it writes past its block on
  the EE as well) is unchanged.
