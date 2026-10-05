# Package 2F: the 64-bit sweep of `omori`, `script` and `common/src`

Scope: `ico2/omori/**`, `ico2/script/**`, `ico2/common/src/**` except the
files other packages own (`main.c`, `debug.c`, `debug_exception*`, `PObj.c`,
`charFileManager.c`, `backStage.c`), the headers they use, and the
regenerated `port/test/layout_asserts.c`. Method as package 2D
(`SWEEP_2D.md`): host gcc 14, `linux-x64` and `ref-m32` built with
`CFLAGS="-Wint-to-pointer-cast -Wpointer-to-int-cast -Wincompatible-pointer-types"`.

## Result

- EE byte identity: all 223 `ico2` TUs compiled with `tools/compile_c.sh`
  from this tree and from a clean `HEAD` worktree
  (`build-host/2f-ee/head`); `.text`, `.data`, `.rodata`, `.sdata`, `.sbss`,
  `.bss`, `.lit4`, `.lit8` and relocations are identical for all 223.
- `linux-x64` and `ref-m32` build; `ctest` passes (29 and 28 tests, the
  skips are the usual ones). `layout_asserts_fresh` passes after
  `tools/gen_layout_asserts.py` (the committed file was stale because of
  `Tim2.h`; the regenerated one also carries other packages' header changes:
  211 structs, 2,240 offset and 118 size asserts). `tools/format.sh --check`
  passes.
- Pointer-width warnings in the three directories (unique lines, the three
  flags above), `linux-x64`: 120 before, 1 after (`backStage.c:290`, not
  ours). `ref-m32`: 9 before, 1 after (the same line). Per file before:
  camera-editor.c 46, brain.c 17, chain.c 10, DObj.c 9, e3.c 8, sceneManager.c
  7, StageManager.c 6, stageSEProc.c 4, main.c 2, st04a.c 2, st17a.c 2, one
  each in backStage.c, icoMisc.c, layout_action.c, layout_texture.c,
  ebrain.c, generator.c, script.c; all 0 after.
- No struct was named or retyped in `typedef.h`, and `config/struct_classes.txt`
  needed no entry (no new header record).

## What was wrong beyond the warnings

`grep -a` for raw views found faults the warning flags do not show. Each is
an `#ifdef ICO_HOST` (or an `ICO_WORD` / `ICO_WORD_PTR` type, `int` on the EE)
with the original kept for the EE.

| where | fault on a 64-bit host | now |
| --- | --- | --- |
| `DObj.c` `CSVSYSTEM_InitDObj` | the display object was allocated and initialised as the EE's 0x880-byte `DObjRecord` template; `Sub15C` is 0x9B0 bytes on x64, so the allocation was too small and every field after the first pointer was misplaced | host: `sizeof(Sub15C)`, zeroed, the template's non-zero words set by name (`disp`, `colRotate`, `cylinderOn`, `modelId`, `accessary`, `parent.node`) |
| `DObj.c` `initGeometryState` | the stack stand-in for a GObj (`pad[348]` then a union at 0x15C) is not a `GObj` on the host, so `SetMotionDirection` read the wrong slot | host: a real `GObj`, reached through `DOBJ_D(p)` (the EE expands to the original `p->data.d`) |
| `DObj.c` node arrays | `(i * 80 + (int)self->nodes)` | `DOBJ_NODE_AT` (index on the host), `_Static_assert` that `DObjNode` is 80 bytes |
| `chain.c` `InitChainGeo` | the chain record was filled by copying 28 doublewords over a `ChainRecord` that holds pointers; the node array was addressed as `(i << 5) + (int)cw->node` | host: struct copy of the typed template; `CHAIN_NODE_ADDR` (five sites); `chainClimb[12]` is 16-byte aligned on the host (it is cast to a record of 16-byte vectors; a 32-bit host would not align it) |
| `brain.c` | `brainGirl.tgt` taken as `(int)&brainGirl + 0x28` (three functions) | `BRAIN_TGT_ADDR` (the field on the host) |
| `mail-add-data.c` | the additional-data table lives in the first 84 bytes of the enemy work (`EnemyBattleWork.pad0[84]`: a count and 10 entries of a mail and a 4-byte pointer); entries are 16 bytes here and would overrun into `speedRatio` and the fields after it | host: `InitMailAdditionalData` ignores the passed block and allocates the table (`ios_partition_seki`, the enemy work's partition and lifetime) |
| `generator.c` | `GenWork` allocated as a literal 112 bytes; `w->hard = (int)pointer` truncates | `ICO_MAX_SIZE(GenWork, 112)`; the host stores `!= 0` (the word is only tested against 0) |
| `staffroll.c` | the roll walks `StaffRollEntry` by a 16-byte byte stride; the entry holds a pointer (24 bytes here) | host: index by entry |
| `layout_action.c` `_la_mcard_error_check`, `_la_memory_card_check` and four more sites | `McMgr` is a runtime record (a pointer at 0x48 moves `sum`, `readSum`, `buf`, `dirName`, `path`, `dir` on x64) but fields were read at `w + 0x10/0x24/0x4C/0x50/0x454/0x47C`, and `port` at `p + 8` | host: named fields (`result`, `segment`, `sum`, `readSum`, `dirName`, `path`, `port`) |
| `layout_action.c` `gflagKeepState` / `gflagRestoreState` | walk 20 words from `keepFlagNo[5]` (`-Waggressive-loop-optimizations`); the EE reads `keyConfigCode` and `keyConfigSlot` after it (research note s.6 of `compiler-semantics.md`) | host: the three tables are one `keepWords[21]` array, so a load keeps the same 20 words; the three names are macros over it |
| `layout_action.c` / `layout_texture.c` | the item-select callback was an `int` holding a function address | `ICO_WORD_PTR(LtSelectFn)` (`layout_texture.h`) |
| `script/st04a.c` | private `ActSt04A` and `PObjGObjSt04A` pad views of `Act` and `GObj` | host: `Act` and `GObj`. The torch actor's three words move: `torchAnim` to `Act.pad470`, `torchObj` to `Act.doorCamera` (a door-script field this actor does not use; the EE's 4-byte pointer at 0x474 has no room), `torchFlag` to `wish0.w[0]` (0x478, where the EE keeps it, so the alias with the wish word is kept) |
| `script/stageSEProc.c` | `SEObj` is a pad view of the sound slot (`SeSlot`, `fumi/sound/s_init.c`), which holds pointers | host: a field-for-field copy of `SeSlot`'s natural layout. **It must follow `SeSlot`;** exporting `SeSlot` from `s_init.h` would let the copy go (see the open items) |
| `script/st08a.c`, `script.c` | `((int *)gobj)[80 / 4]` (`drawMask`) written 22 times | `GObj.drawMask` through `SCP_CLEAR_DRAWMASK` / `SCP_SET_DRAWMASK` and in `scpDispOnAllWithKind` |
| `script/st17a.c` | `((int *)boyGObj)[87] + 1256` (`ctrl.noStand`) | host: `GOBJ_SUB(boyGObj)->ctrl.noStand` |
| `script/script.c`, `st04a`, `stageSEProc` | `(int)target` into `_SCPMoveCharactorByWay` (2D's `ACTWayExec_Position(..., ICO_WORD tgt)` caller), `GetCameraPos((int)self)`, `stageSE08ataimatsu(int)`, `nodeMtx` passed as a pointer | `ICO_WORD` / casts; `_SCPMoveCharactorByWay` takes `ICO_WORD tgt` (`script.h`) |
| `script/e3.c` | `(int)boyGObj != 0` (8 sites) | `(ICO_WORD)` |
| `sceneManager.c` | `*(int *)&gobj->dobj = (int)dobj`; `initParentLink` held GObjs in `int` and wrote the parent link at `GOBJ_SUB(self) + 0/4` | host: direct stores; `ICO_WORD_PTR(GObj *)` variables |
| `StageManager.c` | the stage-manager ring and its receive were `int`; the init thread was a `unsigned int[28]` handed to `iosThreadCreateS` (an `IOSThread` is 152 bytes on x64) | `IosMsgWord` ring, `(IosMsgWord)&msg` send, `(IosMsgWord *)&msg` receive; host: a real `IOSThread` |
| `main.c:96,285` (not ours, fixed as asked) | `scheduler`'s ring and receive were `int` | `IosMsgWord` |
| `icoMisc.c` | `(unsigned int)e > 0x1FEFFF0` (free-list sanity test) | host: `!ico_arena_contains(e, 0)` |
| `ebrain.c`, `ebrain.h` | `eBrainStatusSet` returned `(int)slot` | `ICO_WORD` |

## The camera editor (`camera-editor.c`)

The editor's two copies of the camera set lived at the dev kit's fixed
addresses 0x3000000 and 0x30E27E0 (a `CamMgr`, 100 groups of 76 bytes and
100 pin blocks of 9,200 bytes each, 0xE27E0 bytes). Those are outside the
simulated 32 MB, and `CamGroup.items` is an EE word (`eeword.h`), which
`ICO_EEW` refuses outside the arena. On the host:

- `cameraSetOrg` and `cameraSetEdit` are static `CamMgr` records
  (`CAMSET_T`, `CS_COUNT`, `CS_ITEMS` read a set's two words; the EE keeps
  `int *` and the original `set[0]`, `set[1]`). Each has a static
  `CamGroup[100]` array; `ConvertCameraSetBuffer` points `items` at it.
- A box's pin block is a 9,200-byte heap block from `ios_partition_root`
  (`iosMallocDebugNoAssert`), taken when the box is added, freed when it is
  deleted and when `ConvertCameraSetBuffer` re-initialises the set. The
  pin-block arrays and their flags are not used.
- All `(int)mgr->items` arithmetic (`_CameraEdit_add_pin`,
  `_CameraEdit_free_box_pool`, `_CameraEdit_BOX_p`) is `ICO_WORD`;
  `saveEditedDataBinary` takes an `ICO_WORD`; `curmenu` is `ICO_WORD`;
  `MenuThread` holds a 160-byte thread record on 64-bit hosts (the EE's 112
  is too small for an 152-byte `IOSThread`; 32-bit hosts keep 112 and the
  `MenuThread.parent` assert).

Capacity, to decide in Phase 6: the root partition has about 700 KB left
after the other partitions (their sizes are in `fumi/ios/ios.c`), which holds about 76 pin
blocks, so about 38 boxes in the two sets together. When it runs out,
`CameraEdit_add_box` returns -1, its existing "cannot add any more" result.
A real camera set for a stage should fit; the editor must run with the
partitions' sizes known before it ships. The file writes still use the dev
kit's `host0:` (`debugSceOpen`, `sceWrite`) and are Phase 6's. The editor's
code was compiled only; nothing was run.

## bga, cam and mob consumers

2C's `ICO_EEWORD` fields are `CamGroup.items`, the `FcColl` words and
`FcWallEnt.normal`, the `Bga*` records, the `ObjRec`/`ObjEnt` words of
`DisplayP2O.h` and the `MotFileHdr`/`FacialRec` words. `grep -a` of every name
in `omori`, `script` and `common/src` (outside `PObj.c` and
`charFileManager.c`): the only consumers are `camera-ico2.c`
(2C's, through `CAMGROUP_ITEMS`), `camera-editor.c` (now through the same
macros), and `chain.c`, which reads word 0 of a motion image
(`*motionTable[id]`, a host pointer to the image; the frame count, not an
address). No `colData` contents, `stageTable`, `BgaHeader`, `PObjPart` or
polygon-table reader exists in these directories. The ELF record views of
`MotionDef` (`motionKind`, in `script.c`, `chain.c`, `icoMisc.c`) are
pointer-free and keep their 404-byte stride.

## Open items

1. **`self->proc()` in `fumi/sound/s_init.c:869`** (2D's file). The stage-sound
   procs (`stageSE*` in `stageSEProc.c`, `fumi/sound` others) take the slot as
   their argument, but the slot's `int (*proc)()` is called with no
   arguments; on the EE `$a0` still holds `self`. On a host it is garbage, so
   every `stageSE` proc reads a wild `self`. The call must pass the slot
   (`self->proc(self)` is valid for an unprototyped pointer).
2. **`SeSlot`** (private to `s_init.c`) should be exported from `s_init.h` so
   `SEObj` in `stageSEProc.c` can be dropped; until then the host copy must be
   kept in step.
3. `debug.c:2779` reads `McDirEnt` rows at `(char *)mc + (i << 6) + 0x4C0`
   (`McMgr.dir`); wrong on a 64-bit host (`McMgr.segArg` is 8 bytes).
4. `backStage.c:290` `SetInfoSpKidnapGenerator(g2->work)` passes the
   `GamesysObjInfo` work words (4-byte) as `short *`: same width, a
   type mismatch only. Its `(char *)g1 - (char *)gameSysObjInfo) >> 6`
   relies on the 0x40-byte save record, which is asserted.
5. `sceneManager.c` `GetRealModelId` (static, never called) reads a
   `GenGeo` by `+0x46` / `+0x2C`; `GenGeo` is `pending=5` and wider on a
   64-bit host. It is dead code; convert it if it is ever called.
6. Left as they are: the `-Wstrict-prototypes` lines (`GobjProc.c`,
   `layout_action.c:118`, `stageSEProc.c:52,99`, `camera-editor.c:27`) and
   `icoMisc.c:248`'s `%x` with an `IosMemPart *` (a trace string; changing it
   alters `.rodata`).
7. `layout_action.c` `_la_mcard_error_check` and the other `McMgr` accesses
   assume `mcard.c` and `kanbanBoot.c` use the same record by name; neither
   was in this package.
8. The `ConvertCameraSetBuffer` caller is not in these directories (the
   debug menu); the editor was never run on the host.
