# Package 2E: the 64-bit sweep of `sugipon`, `ito/src` and `fumi/sound`

Method and macros are 2D's (`docs/port/SWEEP_2D.md`): `ICO_RAW`, `ICO_RAWP`,
`ICO_MAX_SIZE` (`fumi/include/ee_view.h`), `ICO_WORD`, `ICO_WORD_PTR`, and
`ICO_EEPTR` for the 2C words (`common/include/eeword.h`).

## The two crashes

1. x86 `Main` read of 0x1B08298, then the assert "THE NUMBER OF NODE DATAS OF
   MOTION ... (0 SKELTONS)". Root cause: `assertMotionNodeCount`
   (`motionOrientManager.c`, inlined into `getMotionGeometry`) read the `.mob`
   header's node-list word as `(int *)md[3]`. 2C made that word an EE word
   (`MotFileHdr.nodeList`), so the raw read used an arena offset as a host
   address. Now `ICO_EEPTR(int *, md[3])`. No sugipon, ito, omori, script or
   common site reads a bga or cam word raw: `stage_*` and `BgaPlayNode` are
   pointer based, and the `bga_*` functions are static to seki. The "bga_GetMotion"
   frames in the stack were not the crash site. Other mob consumers
   (`_getMotion`, `getShapeMotion`, `getMotionRootPos`, `CheckMotionIncludeFacialData`,
   `getShapeGeometry`'s facial count) already used `ICO_EEPTR`; `getShapeGeometry`'s
   `**(int **)(mot + 0x10)` now goes through `ICO_EEPTR`. Unchanged files in seki.
2. x64 `Ee2Iop` crash: `soundBDDataSet(int bd, ...)`, `adpcmDataSet(int src, ...)`
   and `Ee2Iop(int ee, ...)` truncated the buffer. They take `ICO_WORD_PTR(void *)`
   (EE `int`, host pointer) and `ICO_WORD`; `SqEntry.bd` is `ICO_WORD_PTR(void *)`.
   `ReadSoundBdFile`/`ReadSoundAdpcmFile` already pass pointers, so
   `charFileManager.c` needed no edit.

## Pointer-width warnings (unique lines, `linux-x64`, before / after)

fumi/sound: adpcm_init 6/0, s_init 20/0. ito: act_bird 6/0, itou_boss 3/0,
lightning 1/0, queen 11/0, stage_orient 2/0 (pre-existing, gone). sugipon:
a_p_1 9/0, attackCheckBoundary 5/0, box 39/0 (+15 in box.h), boy 12/0, cage 26/0,
cageFix 1/0, candle 1/0, chandelier 1/0, clipCollisionManager 1/0,
clothAnimation 11/0, enemy 2/0, enemyParts 28/0, flag 4/0, frameDependSequence
1/0, geometryManager 2/0, girl 44/0, handManager 2/0, item 20/0, motMan_*.inc 22/0,
motionManager 14/0, motionManager2 11/0, motionOrientManager 23/0, motionViewer
1/0, particleEffect 1/0, pool 1/0, puddle 1/0, rope 28/0, spiderGroupManager 7/0,
streamMotionManager 2/0, switch.c.inc 2/0, waySystemManager 2/0, worm 7/0.
After: 0 in these directories on `linux-x64`; `ref-m32`, `win-x86-ref` and `win-x64`
also report none for them.

## EE identity

All 223 `ico2` TUs were compiled with `tools/compile_c.sh` from this tree and from a
clean `HEAD` worktree: `.text`, `.data`, `.rodata`, `.sdata`, `.bss` and relocations
are identical for all 223 (also after `tools/format.sh`). `flag.c` uses `__LINE__`,
so its edits keep the line count (macros in `flag.h`). `tools/format.sh --check`
passes for the owned directories. `layout_asserts_fresh` passes (the regenerated file
changes only the `adpcm_init.h` line numbers). `ctest`: 29 of 29 (`linux-x64`), 28
of 28 (`ref-m32`; skips are GPU tests).

## Raw offsets converted

Sub15C, GObj, Act, MotCtrl, MotRoot and `ChainSet` views by EE offset became
fields on the host (`ICO_RAW`, `ICO_RAWP`, `#ifdef ICO_HOST` blocks):
`motionOrientManager.c` (`ctrl`, `root`, `fdsFlags`, `morphWeight`, `local`),
`motionManager2.c` (`DispSkelton`, `nodeMtx` words, `FeedbackWallWorkInfoToBrainSystem`),
`frameDependSequence.c` (`setSEEnvironment`), `boy.c`, `box.c`, `flag.c`, `item.c`,
`rope.c` (`ChainNode.ex` offset, `ChainSet`), `cage.c`/`rope.c` (`DOBJ_NODEMTX_PTR`
in `sugiCommon.h`), `worm.c`, `handManager.c` (hand records), `ito/queen.c`
(`Act.motReq`, look-at slots), `a_p_1.c`, `act_a_p_1.c` (`GObj.mailBox`),
`particleEffect.c`. `SUBHANDLE_OF(g)` (`geometryManager.h`) replaces
`(SubHandle *)(g + 0x15C)`. Thread entries (`actClipCollisionCore`,
`actWaySystemCore`, `subQueenBrainMain`, `subQueenControl`, `gene_enemy`) take a
pointer-wide argument; `GProc` has a named `arg` word at 0x20 (was `pad20`, which
on a 64-bit host is the `func` pointer) that the two request functions use.

## Sizes converted

`ICO_MAX_SIZE(T, literal)` for `AcbWork`, `AcbMgr`, `AcbEntry`, `BoxWork`,
`CageWork`, `CandleFlame`, `ChainSet`, `Cloth4D`, `EnemyEye`, `EnemyFootPrintHead`,
`EnemyWork`, `FlagWork`, `ClothCfg`, `PointBlur`, `PoolWork`, `PuddleWork`,
`WormWork`, and every `ClipWork` clear (`0xC0`). Pointer tables use
`sizeof(T *)` (`LLVec **`, `ClothSet **`, `PrimParticle **`, `GObj **`,
`Prim3DVec **`, `float **`, `WormVec **`, cloth rows). `SetParticleEffectPackage`
bounds the `.pef` copy to its 160-byte slot (host only).

## Signatures and fields retyped (callers outside this package)

`DispSkelton(GObj *, ICO_WORD_PTR(void *))`, `ForTest_ForceShiftMotion`,
`GetClothAnimation(..., ICO_WORD_PTR(GObj *) wallOwner, ...)`,
`GetAP1Mode` returns `ICO_WORD_PTR(char *)`, `Init*Geo` constructors
(`InitCandleGeo`, `InitAttackCheckBoundaryGeo`, `InitBossCtrlGeo`) return
`ICO_WORD`, `SObjSimpleSetting.obj` is `ICO_WORD` (`common/include/sceneManager.h`,
one line, plus its `typedef.h` include), `GirlWork` cloth and ornament words are
`ICO_WORD`, `BoxWork.subGObj`, `ItemWork.holder`, `AcbEntry.obj`, the spider group
words and `LeverGeoWork.base` are `ICO_WORD_PTR`. `GProc.pad20` is `arg`
(`fumi/include/gobj_process.h`, one field).

## For 2F (do not edit here)

- `common/src/DObj.c:154`: `nodeLimit` is allocated `skelNodeNum << 2`; sugipon
  stores a 4-byte one-based row of `motionLimitDef` there on the host (see below),
  so the allocation is right. No change needed, listed so nobody widens it alone.
- Any caller that passes a pointer to `SObjSimpleSetting.obj`, `GetClothAnimation`'s
  wall owner, `DispSkelton` or `ForTest_ForceShiftMotion` now compiles without a
  cast on the host; casts left as `(int)` there will warn.
- No omori, script or common reader of a bga, cam or mob word was found outside
  the LOADERS.md `camera-editor.c` items. `omori/src/chain.c:1297,1324,1351` and
  `fumi/src/boyact.c` read `*motionTable[n]` (the frame count, a plain int): fine.
- During this work `omori/src/mail-add-data.c:89` (`ios_partition_seki`
  undeclared) and `common/src/layout_action.c:192` broke in the shared tree from
  other packages' in-progress edits; they are not from 2E.

## Left (judgement calls)

1. `SqEntry` is host-natural (three pointers), so `s_init.c` addresses
   `soundDataTbl` and `seSlotTbl` by field on the host and by the EE stride on the
   EE. `AdpcmStream` drops `pad2C` on a 64-bit host so its offsets (and the many raw
   `+0x38` views in `adpcm_init.c`) stay valid.
2. `nodeLimit` (host): `SetNodeRotationLimitDataTable` stores `i + 1`, and
   `_getFinalMatrix` reads `motionLimitDef[word - 1]`. `motionLimitDef` is ELF data
   outside the arena, so an EE word cannot name it. The Phase 5 table loader may
   replace this.
3. `SeDef` and `SeEnvDef` (`pending=5`) are still the EE layout plus a function
   pointer, so `sizeof(SeDef)` is 0x40 on the host until Phase 5; `debug_req` uses
   pointer subtraction on the host.
4. `Init*Geo` constructors and the thread entries are reached through ELF function
   tables, which need the function map of Phase 5; their types are ready.
5. Raw offsets that remain are pointer independent: `matrixptr + 0x80/0xC0/0x100`
   (float matrix table), vector words, `ExW` and `DObjNode` fields
   (`+0x3A` flags, scale, no pointers), GIF packet cursors in `darkVolume.c`,
   `particleEffect.c`, `lightning.c`, and quaternion helpers.
6. `enemy.c:464` `int *parts` (broken-flag words) and `clothAnimation.c`
   `mark` rows are ints, not pointers, so their `n * 4` sizes stay.
7. `GirlWork`'s and `BoxWork`'s offset comments are EE offsets; the `ICO_WORD`
   widening moves host offsets (runtime class, asserted only on 32-bit presets).
