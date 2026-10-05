# Package 2D: the 64-bit sweep of `fumi/src` and `fumi/isys`

Scope: `ico2/fumi/src/**`, `ico2/fumi/isys/**`, the fumi headers, the
message-ring callers in `fumi/ios` (`cdvd.c`, `mcard.c`, `pad.c`) and the
regenerated `port/test/layout_asserts.c`. Method and numbers below were taken
with the host gcc 14 on `linux-x64` and `ref-m32`
(`-Wint-to-pointer-cast -Wpointer-to-int-cast -Wincompatible-pointer-types`).

## Result

- EE byte identity: every `ico2` TU (223 objects, not only the touched ones,
  because `typedef.h`, `act-game.h`, `enemy_act.h` and others changed) was
  compiled with `tools/compile_c.sh` from this tree and from a `HEAD`
  worktree; `.text`, `.data`, `.rodata`, `.sdata`, `.bss` and relocations are
  identical for all 223. `tools/format.sh --check` passes.
- `linux-x64` and `ref-m32` build, `ctest` passes (27 and 26 tests).
  `layout_asserts_fresh` passes after `tools/gen_layout_asserts.py`; the
  regenerated file also carries another package's `Tim2.h` records.
- Pointer-width warnings in `fumi/src` and `fumi/isys` on `linux-x64`
  (unique lines, per file, before / after): act-env 12/3, act-game 70/0,
  act-way 4/1, act-wish 2/0, act 45/0, boyact 18/2, commonact 64/2,
  enemy_act 31/0, fieldCollision 19/1, girl_act 48/2, jimaku 6/0, way_sys 1/1,
  way_tool 1/0, way_util 2/0, gobj 3/2, gobj_process 2/0. Total 328 / 14.
  The 14 left are listed under "Left" (implicit `memset`, same-width pointer
  type mismatches). `ref-m32` after: 8 of the same kind.
- `fumi/ios`: the `int` rings and receive variables of `cdvd.c` (six rings,
  `msg`, `local`, `buf[4]`), `mcard.c` (`mcMsgRing`, the `pp` receive) and
  `pad.c` (`padDevMgrMsgBuf`, `local_buf`) are `IosMsgWord`; `jimaku.c`
  likewise (`jimakuMsgBuf`, `msg`). The `iosMsgSend(q, pointer)` calls still
  warn (`-Wint-conversion`) but pass the value intact.

## How the edits are made

- Where a named field compiles to the same EE bytes, the code uses the field.
  Where it does not (the EE gcc 2.9's scheduling and `MEM_IN_STRUCT` bit depend
  on how an access is spelled) the access goes through
  `ico2/fumi/include/ee_view.h`: `ICO_RAW(T, p, off, field)` (EE: the original
  `*(T *)((char *)p + off)`, host: `field`) and `ICO_RAWP`. About 60 sites
  use them (63 lines). Where a statement needs a host-only spelling there is
  an `#ifdef ICO_HOST` with the original in `#else` (`actInitialize`,
  `actInitialize_only_charcter`, `GetSaveSofaLayoutID`, the after-proc
  stores, the `RopeJump`/`Bar` Sub15C views, `DrawGObjWallCollision`).
- With `ICO_OFFSET_AUDIT` defined (only `tools/offset_audit.py` defines it,
  when it preprocesses the game) `ICO_RAW`/`ICO_RAWP` keep their EE offset
  in the text next to the field, and the audit checks that the field is the
  one at that offset (docs/port/OFFSET_AUDIT.md, package F1). F1 fixed the
  sites the boot never reached that the sweep's warnings could not show.
- `ICO_MAX_SIZE(T, lit)` (also in `ee_view.h`): `max(sizeof(T), lit)`, the
  literal on the EE, `sizeof` where a host record is wider.
- Words that hold an object but are `int` on the EE became
  `ICO_WORD_PTR(T)` (EE `int`, host pointer) or `ICO_WORD` (EE `int`, host
  `intptr_t`), so the EE code generation is unchanged.

## Sizes converted

| where | was | now |
| --- | --- | --- |
| `act.c` `actInitialize` (Act) | `0x850` | `ICO_MAX_SIZE(Act, 0x850)`, host path; `Act` is now 0x850 on the EE too (see fields) |
| `act.c` `actInitialize_only_charcter` (ActWork + history) | `0x980` | `ICO_MAX_SIZE(ActWork, 0x980)` |
| `act.c` `actInitialize_ext_charcter` (EnemyBattleWork) | `0x400` | `sizeof(EnemyBattleWork)` (0x400 on the EE once `boss[5]` is a field) |
| `way_tool.c:677` cursor actor | `0x850` | `ICO_MAX_SIZE(Act, 0x850)` |
| `gobj_process.c` pool and strides | `0x94` | `sizeof(GProc)` (0x94 on the EE) |
| `boyact.c` `boyInfo` (BoyInfo view) | `long long[12]` | `long long[24]` on the host (BoyInfo has two pointers) |

`way_util.c`'s `95`/`94 * 94 * sizeof(int)` tables are element counts, not
record sizes; `94 * sizeof(int *)` is already pointer-wide.

## Fields named, retyped or added (headers)

`typedef.h` (`Act`): `bits58` (0x58), `soundFlag` (0x138), `wayTarget`
(0x404), `wayFromX/Y/Z` (0x410..0x418), `wayDetailFlag` (0x430),
`wayGoalY` (0x434), `barObj` (0x15C), `addData` (0x68C), `flyClip[0x1C0]`
(0x690, the flyer's `ClipColReq`, 16-aligned, so the record is 0x850 bytes
on the EE). Retyped to `ICO_WORD_PTR(...)`: `intrData`, `reserved`,
`statusTarget`, `statusOther`, `statusObj`; `attackTurn` is `ICO_WORD`.
`ActEffRec`, `HandClInfo` (moved out of act-game.c), `HandModeCmd` and
`BossPart` are new records, registered in `config/struct_classes.txt`.
`act-game.h` (`ActWork`): `lastPosX/Y/Z`, `handPosX/Z/W`, `effRec[3]`
(0x810), `handCl` (0x540), `wayHold`, `wayDirX/Y/Z`, `modeHist`,
`frameHist`, `prevHist` (0x900..0x978); `hideObj` is `GObj *`.
`enemy_act.h` (`EnemyBattleWork`): `corrMode`, `liftToggle`, `liftPhase`,
`liftTimer`, `frontPosX/Y/Z`, `slipFront`, `word2C0`, `holdObj`,
`holdPoint`, `word298`, `handConnect`, `handDisconnect`, `boss[5]`
(0x360); `target`, `clingReq`, `clingTarget`, `liftedObj` are
`ICO_WORD_PTR(GObj *)`. Signatures now taking a pointer-wide word:
`_ACTCharStatus_Set(..., ICO_WORD val)`, `ACTSearchEnemy`/`ACTSearchGObj`
(`ICO_WORD *out_id`), `ACTWayExec_Position(..., ICO_WORD tgt)`,
`ACTGame_isWeaponEnableCatchfire`, `actEnemy_GetClingTarget`,
`EnemyUtil_isOtherStatus`, the `ClipFloor/ClipWall(Field)CheckCB` filter
(`ICO_WORD_PTR(ClipFilterFn)`), `ClipPlane(ClipWork *)`,
`isysGObjProcPausePtr/ActivePtr(void *, void (*)())`,
`afterCommonCling/Revive`, `afterGirlHand`.

Callers outside this package that still pass `(int)pointer` to those words
(`script/src/script.c:1492`, the `_ACTCharStatus_Set` callers in
omori/sugipon/script, `ACTSearchGObj`'s callers) are for 2E/2F's sweeps; the
signatures now accept a pointer-wide value.

## LOADERS.md items for 2D

- `Act` flag-bit views (`+0x18`, `+0x20`): all through `flags18.ll` /
  `flags20.ll` now; the EE-only spellings stay in `#else` branches.
- `actInitialize` size: done (see above).
- `fieldCollision.c`: `ClipPlane(ClipWork *)`, the filter casts, the
  `FcWallObj`/`FcWallSub` debug-draw views (host path through `GOBJ_SUB`),
  `(int)GOBJ_SUB` (1676..): done. `MapCollisionData` and `LoadCollision` store
  `ICO_EEW` words, matching the `ICO_EEPTR` readers (`FCWS_WALLS`).
- `enemy_act.c:1116` `(int)target`: `q.i[0]` is `ICO_WORD`.
- `gobj.c:263`: `(ICO_WORD)gobjTable`.
- `soundBDDataSet`/`adpcmDataSet` (`fumi/sound`): not touched, outside this
  package's ownership.

## Left (judgement calls and open items)

1. `act-env.c:359,501`, `gobj.c:22`: implicit `memset` (no `<string.h>`
   in those TUs); not a width issue. Adding the include risks EE output.
2. `act-way.c:221` `RequestWayBegin(self, ...)`, `way_sys.c:380`
   `GetRootPosition(pos, g)`, `boyact.c:2259` `debug_NMarker(work, ...)`,
   `boyact.c:2366` `MoveFloatingBox(..., n + 0x30)` (an `int` offset built
   from a pointer): same-width pointer type mismatches, no truncation.
3. `fieldCollision.c:1158` `((void (*)(void))ClipWall)()` calls ClipWall with
   no argument through a cast (the EE leaves garbage in `a0`); the host passes
   garbage too. Needs a decision on what the game means (the caller
   `ClipWallRD`).
4. `boyact.c` ~1809 `_RotyGV` is called through an `int`-returning cast on the
   EE (float comes back in `$f0`, the int read is whatever `$v0` held). The
   host path calls `(int)_RotyGV(...)`, which is the evident intent but not a
   measured behaviour.
5. `girl_act.c` `GirlListEnt` (`void *obj` first) is natural layout on the
   host, so `GirlBrainWork`'s lists are 0x38 bytes an entry, not 0x30; every
   access in the file is typed now, but the struct is defined in the `.c` and
   has no layout assert.
6. `boyact.c` `characterPacket[8]` and `BoyKidnapWork`/`CharPos` are save
   records kept byte-for-byte (4-byte words); `MakeCharacterPacket` reads
   `sub->sofa->labelId` through the object now. `BoyInfo` is a runtime record
   over `boyInfo[]`; its bit-field layout (`layoutID : 32` followed by 1-bit
   members) was not checked against the 64-bit gcc ABI, only the 32-bit one.
7. Raw offsets that remain are into frozen disc records or are
   pointer-independent: `fieldCollision.c:1298,1482,1534,1685,1687,1805`
   (collision wall and floor entries, 4-byte words), the vector words
   `(char *)out + 0x4/0x8/0xC` (`act-env.c`, `act-game.c`, `girl_act.c:75..83`),
   `act-game.c:2268` (`U64ag` copy from the mail record `w1 + 0x80`), the
   `gamesysObjInfoGet` record reads in `boyact.c` ~1621 (save record).
8. `act-game.c` `ORQ`/`ORM` macros (`(s) + 0x478`, `ActStatusWord` array) are
   raw views of the five wish words; wish0..wish4 are contiguous 8-byte words
   on the host, so the offsets agree, but the macros take a `char *`.
9. `girl_act.c:3704,3713` and `act-way.c:59` read a doubleword over
   `ActWork.stopFrames` + its pad (and `EnemyBattleWork.word298` +
   `stoneLevel` in `boyact.c`): correct on the host only because both words
   are adjacent ints (natural layout puts no padding between them).
10. The `rd_pixel` failure seen on `ref-m32` mid-package belonged to
    `port/render` and was gone by the end; nothing in this package touches it.
