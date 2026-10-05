# The EE-offset audit (`tools/offset_audit.py`, package F1)

The 2D-2F sweeps checked what the boot ran in 3000 ticks. The final review
then found raw EE offsets on paths the boot never reaches (box, worm, rope,
the fly and ladder states, the girl's sofa attract, the shadow direction).
This audit looks at every site the host compiles, run or not, and the
`offset_audit` ctest keeps the count at zero.

## What it checks

Every `ico2/` unit of the game libraries in a configured build's
`compile_commands.json` is preprocessed with its own host flags and
`-DICO_OFFSET_AUDIT=1`. With that define `ee_view.h` makes `ICO_RAW` and
`ICO_RAWP` leave a marker holding their EE offset (it is never compiled into
the game). In each function body of an `ico2/` source the audit finds:

| kind | spelling | host access offset |
| --- | --- | --- |
| `cast`, `intcast` | `(char *)x + 0xNN`, `(int *)x + N`, `(int)x + 0xNN` | the literal (a pointer through a 32-bit `int` is TRUNC) |
| `index`, `deref` | `((float *)x)[N]`, `*(int *)x` | N times the element; a 4-byte access to a field that is 8 bytes on the host is a MISMATCH |
| `view_index`, `view_plus` | `f[N]`, `q + N`, `f` a local or a parameter assigned `(float *)x` or `(char *)x + K` | K plus N elements |
| `recast`, `lview`, `slot` | `((V *)x)->m`, `s->m` with `V *s = (V *)GOBJ_ACT(self)`, `((CagePtr *)&o->dobj)->f[81]` | V's host `offsetof(m)` |
| `storage` | `(ClipColReq *)act->flyClip`: a record kept whole in a byte buffer | only `sizeof(V) <= sizeof(buffer)` |
| `ico_raw` | `ICO_RAW(T, p, off, field)`, `ICO_RAWP` | the field's host offset; the field must be the one at EE `off` from `p` |
| `pad` | a `pad<HEX>` or `unk<HEX>` member read or written (not copied whole) | NOFIELD: give it a name |

For each site the compiler gives the operand's type: the operand is wrapped
in a statement expression that initialises a `struct __icoA_<k> *` from it,
and gcc's diagnostic names the type. A `char *`, `float *` or `int` operand is
followed back to its assignments in the function (or, for a parameter, to
the arguments of every call in the unit). Two untyped words take their
pointee from the game's macros: `GObj.act` is an `Act` (`GOBJ_ACT`), `Act.work`
an `ActWork` (`GOBJ_WORK`).

The record's EE layout is read from the headers' and the `.c` files' offset
comments with `gen_layout_asserts.py`'s parser; a member without a comment
follows the previous one at its EE size (pointers and `ICO_WORD`s are 4
bytes); `pad<HEX>` names give their own offset; members of a record type are
expanded into that record's fields. The access's EE offset then names a
field (`Sub15C` EE 0x50 is `matrix[3][0]`). A probe appended to the unit
compiles `__builtin_offsetof(T, field)` and `sizeof` on the host (in a unit
that includes every header when the unit only declares the record), and:

- **MISMATCH**: the access's host offset is not the named field's host offset;
- **NOFIELD**: no named field is at that EE offset (a gap without a
  comment, inside a pad, past the record);
- **TRUNC**: a pointer goes through `(int)`;
- **UNRESOLVED**: a view the audit could not resolve (a view of a record with
  no EE layout, an `ICO_RAW` field it cannot parse);
- **OK** / **SKIP**: the access is the named field on the host / not a view
  of a record (a vector's lanes, a matrix row, an EE word table).

Any of the first four fails the run (exit 1).

```sh
cmake --preset linux-x64 -B build-host/<dir>      # compile_commands.json
tools/offset_audit.py --build build-host/<dir>      # findings, exit 1 on any
tools/offset_audit.py --build build-host/<dir> --all          # every site
tools/offset_audit.py --build build-host/<dir> --skipped      # with the reasons
tools/offset_audit.py --build build-host/<dir> ico2/fumi/src/act.c
tools/offset_audit.py --build build-host/<dir> --dump Sub15C  # a record's EE fields
```

ctest runs it as `offset_audit` on native builds (`port/test/CMakeLists.txt`;
about 30 s). Its work files go to `<build>/offset_audit/`.

## Result

The final run (`linux-x64`, gcc 14, this package's tree):

```
offset_audit: 214 units, 18031 sites (cast 198, deref 554, ico_raw 218, index 167, intcast 13, lview 11303, pad 25, recast 3850, storage 3, view_index 553, view_plus 1147)
offset_audit: OK 865, MISMATCH 0, NOFIELD 0, TRUNC 0, UNRESOLVED 0, SKIP 17166
```

The same tool on `HEAD` (8f864ddd, with only the `ee_view.h` marker added so
the `ICO_RAW` sites are visible): 18056 sites, **OK 824, MISMATCH 63,
NOFIELD 39, TRUNC 2**, in 25 files. Of those 104:

- 18 NOFIELD were `commonact.c`'s `LadderWork` view, whose comments gave the
  enemy work's absolute offsets (0x290) while the host struct starts at
  `ladderUpStep`: the code was right, the comments now say so.
- 20 NOFIELD were reads and writes through a pad that the host keeps
  consistently but no code names (`st04a.c`'s torch animation in
  `Act.pad470` x11, `Act.pad60` x2, `GirlBrainWork.pad58F1` x2,
  `ActWork.pad8D0` x2, `Act.pad100`, `PObj.pad2C`, `IosPadBuf.pad0`): now
  named fields.
- 11 MISMATCH and 1 TRUNC were in the EE-only `p` argument of an `ICO_RAW`
  (`enemy_act.c:2814-2823`, `girl_act.c:4071`, `act.c:516`,
  `motMan_getFinalMatrix.c.inc:336`, `motionOrientManager.c:812`): never
  evaluated on the host, now spelled with the field too. 2 MISMATCH were the
  argument of a no-op debug print (`act.c:78`).
- The other 50 MISMATCH, 1 NOFIELD (`boyact.c:1194`) and 1 TRUNC
  (`mcdata.c:25`) read or wrote the wrong bytes on the host.

## Sites fixed

Each keeps the EE spelling (`ICO_RAW`/`ICO_RAWP`, or `#ifdef ICO_HOST` with
the original in `#else`); all 219 `ico2` objects are identical to `HEAD`'s
under ee-gcc 2.9 (`tools/ee_identity.sh --all`).

| file | EE offset | was on the host | now |
| --- | --- | --- | --- |
| `fumi/ios/mcdata.c:25` | the 64-byte rounding of a stack buffer | `(int)buf` truncated the pointer | `__UINTPTR_TYPE__` arithmetic |
| `sugipon/src/box.c` `alignPosition`, `worm.c` `GetWormCaptureVector` | Sub15C 0x50, 0x58 | `matrix[2][*]` | `matrix[3][0]`, `[3][2]`, `matrix[3]` |
| `seki/src/Light.c`, `RegistPacket.c` | Sub15C 0x860 | `(char *)o + 0x860` | `shadowDir` |
| `sugipon/src/flag.c`, `motMan_getFinalMatrix.c.inc`, `motionOrientManager.c` | Sub15C 0xA0, 0x470 | raw | `root.pos`, `ctrl` |
| `sugipon/src/windmill.c` | Sub15C 0, 4 (`WmWork`) | owner as `int` at 0, node at 4 | `parent.obj`, `parent.node`; `owner` is `ICO_WORD` |
| `sugipon/src/enemy.c:199` | Sub15C 0x854 | `*(int *)&model =` an implicitly declared `int` | `model = GetPObjAddress()`, declared in `charFileManager.h` |
| `sugipon/src/rope.c` | ChainNode + w1 * 0x50, floats 9 and 13 | raw | `ex[w1].v0.y`, `ex[w1].v1.y` |
| `sugipon/src/clothAnimation.c` debug draw | ChainNode 0x10 + j * 0x50, 0x30, 0x80 | raw | `ex[j].w`, `ex[0].v1`, `ex[1].v1` |
| `fumi/src/commonact.c` | Sub15C 0x654, 0x5F8, float 81 (0x144); Act 0x38, 0x4C | `FlyLimitSub`, `FlyCtlJ`, `CagePtr->f[81]`, `LadderMotWork`, `StoneSub` | `ctrl.noFieldClip`, `ctrl.floorAttr`, `root.step[1]`, `pushDir`, `modeFrame` |
| `fumi/src/commonact.c` `IsFlyTimeOver` | | an `int` parameter given `(ICO_WORD)self` | `GObj *` |
| `fumi/src/act-game.c` `hand_able_connect` | ActWork 0x540..0x561 | `h[0x540]` | `handCl.on/hit/attr/hit2/attr2` |
| `fumi/src/act-game.c` | Act 0x74 (bit 18's object) | an `int`, read back as a `GObj *` | `statusVal18` is `ICO_WORD` |
| `fumi/src/act-wish.c` `chkOrient` | Act 0x4B0 | `s + 0x4B0` | `env.wallOrient` |
| `fumi/src/act.c` | GProc 4; ActWork 0x8B0 via `(int)GOBJ_ACT(self) + 0x688` | raw; a pointer through `int` | `owner`; `intrReq` |
| `fumi/src/enemy_act.c` | Sub15C root + 752; GObj 0x15C; Act 0x20 (`EnemyBrainWork`) | raw | `root.lookPos`; `dobj`; `flags20` |
| `fumi/src/boyact.c:1194` | ActWork 0x8D0 | `(char *)work + 0x8D0` | `cliffOrient` (was `pad8D0`) |
| `fumi/src/girl_act.c` | ActWork 0x520..0x528, 0x3B0 (`ActPara`); GObj 0x15C | raw | `hintPosX/Y/Z`, `turnMailWait`; `dobj` |
| `fumi/src/girl_brain_attract.c.inc` | EnemyBattleWork 0x230, 0x240 (`GirlSofaWork`); Sub15C 0 | wrote `clingReq`/`clingTarget`/`liftedObj`; read 4 bytes of `parent.obj` | `readyPosX..Z`, `readyDirX..Z`; `parent.obj` and its `kind` |
| `fumi/ios/pad.c` | IosPadBuf 0..3 | `*(unsigned int *)b >> 12` over `pad0` | `termId >> 4` |
| `fumi/ios/cdvd.c` | | `bgRunning` held the running request in an `int` | `ICO_WORD` |

Fields added or renamed (headers; `port/test/layout_asserts.c` regenerated):
`Act.bits60` (0x60, was `pad60`), `Act.curRoot` (0x100, `test_CURRENTROOT`'s
buffer, was `pad100`), `Act.torchAnim` (0x470, was `pad470`),
`ActWork.cliffOrient` (0x8D0, was `pad8D0`), `IosPadBuf.status`/`termId`
(0, 1, was `pad0[2]`), `PObjModel.spare` and `PObj.spare` (0x2C, was
`pad2C`), `GirlBrainWork.inWarningCheck` (0x58F1, was `pad58F1`);
`Act.statusVal18` retyped `ICO_WORD`.

## Limits

- A view through a table of `void *` (`rope.c`'s `w->chains[2]`) or of a
  record with no EE layout carries no type: the audit reports a record view
  without an EE layout as UNRESOLVED, but cannot see that `void *` is a
  `ChainNode`. The rope site was fixed from the review.
- Offsets with a variable stride (`base + j * 0x50`) are checked only where
  `ICO_RAW` spells them (at index 1); raw ones are not seen.
- The EE layout of a member without a comment comes from its type's EE size;
  a typedef the audit does not know (bit-fields after the first word, some
  libvu0 and libgraph types) stops the layout until the next comment.
- Only function bodies are scanned; static initialisers are not.
- The pointee table (`GObj.act`, `Act.work`) is the only type knowledge not
  read from the code.
- A typedef whose name is followed by `__attribute__` (`} CamTgt
  __attribute__((aligned(16)));`) was taken for a typedef of
  `__attribute__`, so casts to it were not seen; fixed in package S4 (one
  more OK site: 18031 sites, OK 866, no finding).

## Whole-record copies (`tools/template_audit.py`, package S4)

The offset audit checks member accesses. A record copied whole over storage
of another record type has no member access to check, and two user-visible
x64 bugs came from it: `InitMotionGeoInfo`'s `MotionGeoInfo` template over
`MotRoot` (36a1d73e, node 1's quaternion zeroed) and `InitMotionStateInfo`'s
`*(MotionStateInfo *)self = motionStateInfoTemplate` over `MotCtrl`
(17741507, `floorFit` 0 for every actor: DIVERGENCES.md D7). On the EE both
layouts coincide; on the host they differ when one side has a `Vec4` (8-byte
aligned by its `long long` view) where the other has `float[4]`, an `int`
where the other has a pointer, or other padding. The second audit finds
every such copy and requires its layout to be asserted where it is made.

### What it checks

Each game unit is preprocessed with its host flags (the offset audit's unit
handling). In every function body of an `ico2/` source:

| kind | spelling |
| --- | --- |
| `deref`, `index` | `*(T *)E`, `((T *)E)[k]` used whole (assigned, read, returned; not under `&`, `sizeof` or a member access), T a struct or union |
| `lview` | `*p`, `p[k]` used whole, p a local assigned `(T *)E` in the function |
| `memcpy` | `memcpy`, `memmove`, `bcopy` (and the builtins) whose operands, with their `void *`/`char *` casts removed, point to records |
| `pair` | `*(T *)a = *(T *)b`: b's storage over a's (a template moved through a third type, `Blob64`) |

The compiler gives E's type. The storage U is E's pointee, or, for `&x.m`
whose pointee is not a record (or is smaller than T), the record that has
the member m: `*(MotOriReq *)&GOBJ_SUB(self)->root.wall` is `MotOriReq` over
`MotRoot` from `wall`. Then:

- **SAME**: U is T (a copy through its own type);
- **RAW**: U is not a record (a `char` or `int` buffer that only ever holds
  T: `*(BoyWork *)boyInfo`, `*(McName *)mp->path`), or E is pointer arithmetic
  (storage carved from a file image or a buffer: `(PinRec *)(groups + n)`).
  Listed with `--all`; such a buffer has no layout of its own to disagree;
- otherwise the copy needs a **registration** in its unit: a
  `_Static_assert` naming `sizeof(T)` and U, and, for every named member m of
  T (pad members excepted), one naming `offsetof(T, m)` and U. A record that
  only moves bytes (a union, a single member, 64-bit words only: `ICO_QW`,
  `Blob64`, `DObjBlk40`) on either side needs the size only.
  `ico2/fumi/include/ee_view.h` spells them: `ICO_LAYOUT_AT(T, tm, U, um)`,
  `ICO_LAYOUT_AT_FROM(T, tm, U, base, um)`, `ICO_LAYOUT_SIZE(T, U)`; all
  under `ICO_HOST`, so the EE objects do not change;
- **UNREGISTERED**: a registration is missing (the members without one are
  named);
- **MISMATCH**: the audit's own host probe differs: a member T and U share
  by name at different offsets, T longer than U (or than the room after U's
  member), or a 16-byte aligned T (the host compiler copies it with aligned
  SSE moves, `movaps`) over a variable or a type with less alignment.

MISMATCH and UNREGISTERED fail the run (exit 1). ctest runs it as
`template_audit` next to `offset_audit` on native builds (about 15 s).

```sh
tools/template_audit.py --build build-host/<dir>          # findings, exit 1 on any
tools/template_audit.py --build build-host/<dir> --all    # every record copy
tools/template_audit.py --build build-host/<dir> ico2/sugipon/src/motionManager2.c
```

Run on `motionManager2.c` as it was before 17741507, it reports
`MotionStateInfo over struct MotCtrl (host view/storage): dir 192/188,
lastDir 208/204, orientKind 224/220, floorFit 228/224, ...` (MISMATCH) and
`MotionGeoInfo over struct MotRoot: no offsetof assertion for 76 of 80
members` (UNREGISTERED).

### Result

`linux-x64`, gcc 14, this package's tree: 214 units, 202 record copies.

| | before | after |
| --- | --- | --- |
| OK | 1 (`MotionStateInfo`) | 57 |
| MISMATCH | 7 | 0 |
| UNREGISTERED | 49 | 0 |
| SAME | 7 | 7 |
| RAW | 138 | 138 |

Every registered pair's member offsets were compared on the host (probes
of `offsetof`/`sizeof` of each member pair compiled with the game flags)
before the assertions were written. No member offset differed: the two
bugs above were the only layout mismatches. The 7 MISMATCH were alignment:

| site | storage | was | now |
| --- | --- | --- | --- |
| `omori/src/camera-root.c` `InitCamera`, `CameraSetTargetGObj` (4 copies) | `cameraSet`, `targetCameraSet` (`CameraSet2`, 4-byte aligned) | `CamTgt` is `aligned(16)`; gcc copies it with `movdqa`/`movaps` on the two statics, which were 16-byte aligned only because the linker happened to place them so | the two statics are `aligned(16)` on the host |
| `sugipon/src/multiBgaManager.c` `InitMultiBgaManager`, `stageMultiBgaManager.c` `InitStageMultiBgaManager` | `InitialBgaMultiAnimeState` (`BgaAnimeState`, 4-byte aligned) | read through `BgaDisp` (`aligned(16)`) with `movdqa` | declared `aligned(16)` in `multiBgaManager.h` on the host |
| `script/src/st04a.c` `finishCallBackFunc` | each node's `MotIk` (4-byte aligned, heap) | an `Mtx44` (`aligned(16)`) store; right only because `iosMalloc` returns 16-byte aligned blocks with a 64-byte stride | `__builtin_memcpy` of the same 64 bytes on the host |

The registrations (all `ICO_HOST`):

| file | copy | registered |
| --- | --- | --- |
| `sugipon/src/motionManager2.c` | `MotionGeoInfo` template over `MotRoot` | all 80 members (was 4): 74 by name, `rot`/`nextPos`/`fieldPos` at `quat`/`move`/`footPos`, `word10C`/`wall110`/`word11C` and `vec1E0`/`vec1F0` inside MotRoot's pads; size |
| `common/include/typedef.h` (end) | `MotOriReq` over `MotRoot` from `wall`, 9 copies in 8 files (`act-env.c`, `act-game.c`, `girl_act.c`, `act_bird.c`, `queen.c`, `motionManager.c`, `motionOrientManager.c`, `motionViewer.c`) | `a`/`aw`/`b`/`bw` at `wall`/`wallCount`/`cliffWall`/`cliffWallCount`, ending before `aheadWall` |
| `common/src/DObj.c` | `DObjBlk40` over `MotIk` and a blend rotation, `DObjBlk20` over `Sub15C` from `streamScale` | sizes (to `motion`) |
| `common/src/layout_action.c` | `struct McPreview` over `McFileInfo` (3) | 5 members, size |
| `fumi/ios/memory.c` | `IosMemTag` over `IosMemPart`/`IosMemNode` tags (12), `IosMemNodeRec` over `IosMemNode` | tag size and offset; 9 members |
| `fumi/src/act-way.c` | `WayStep` template over `Act` from `wayNodeX` | 4 members (`state` at `wayFlags`), size to `wayLast` |
| `fumi/src/commonact.c` | `MotOriTarget` over `ClimbCol` | size, `wall.o`/`wall.elem` |
| `omori/src/camera-ico2.c` | `CamItemV0/1/2` file records over `PinRec` | each ends at the member the next version adds |
| `omori/src/camera-root.c` | `CamTgt` template over `CameraSet2` (4) | `pos`, `tgt` at `rotX`, size, alignment |
| `omori/src/chain.c` | `ChainPendTemplate` over `ChainPendulum` | size |
| `script/src/st04a.c` | `Mtx44` over `MotIk` | size |
| `seki/src/Primitive.c`, `Shadow.c` | `Qw128` over `Prim3DVec` (8), a zero `VECTOR` into a `Qw128` slot | size |
| `sugipon/src/clothAnimation.c` | `ClothHangCfg` rows over `ClothPoint` through `Blob64` | 9 members (`node` in the pad at 0x10, `posX/posY/posW` at `pos[0]/[1]/[3]`), sizes |
| `sugipon/include/multiBgaManager.h` | `InitialBgaMultiAnimeState` over `BgaDisp` slots (2) | 6 members, size, alignment |

Copies the audit cannot see, checked by reading and asserted in place:

- `sugipon/src/rope.c`: `ropeChainInit` (`RopeTemplate`) goes to
  `InitChains(void *)`, which reads it as `ChainCfg`. The host layouts agree
  (node 0x10, step 0x14, root 0x20, weight 0x40, 0x50 bytes; `ChainCfg`'s
  host layout is its EE one); asserted in `rope.c`.
- `fumi/src/boyact.c`: `boyInfoDefault` (a `BoyWork`) resets the `BoyInfo`
  kept in `long long boyInfo[24]`; asserted that the template covers the
  whole host record (96 bytes) and that the pointers start at 0x20, and that
  `BoyKidnapWork` is the character packet's size.

Not a layout fault, noted: `sugipon/src/clothAnimation.c` copies a
texture's `TexData` into the cloth through `TexBlob` (89 doublewords, the EE
size); on the host `TexData` is wider, so the copy keeps only its head. The
copy is only ever read for its name at offset 0 (`tex_GetTextureNo`), which
the head holds.

Copies from the runtime-loaded ELF tables: the loader writes each table in
its own declared type's host layout (DATA.md), so a copy out of a table is
a copy out of that type; copying one over another record type needs a
cast, which makes it a site like any other. The only such view copies of a
loaded table (config/data_members.pal.txt) are `motionOrientManager.c`'s
`motionLimitDef` rows, SAME (the cloth's `ClothHangCfg` rows, the `pair`
above, are compiled statics of `boy.c`). File data laid over records
(`memcpy(&GlobalStageSetting, ...)`, `.pef` packages, camera sets, TIM2
headers) is RAW: those records' host layouts must equal the file's EE
layout, which `port/test/layout_asserts.c` checks per record.

### Limits

- A copy through a `void *` parameter or table (`InitChains`, a `void *`
  work pointer) carries no record type; such storage is RAW or not seen.
- An implicit conversion between record pointers at a call
  (`-Wincompatible-pointer-types`) followed by a whole copy in the callee is
  not seen: the callee's copy is of its own parameter type.
- Static initialisers are not scanned (they cannot copy records).
- The member list of T comes from its definition in the unit (anonymous
  members flattened); a T with no definition there is UNREGISTERED.
- A registration is recognised by what it names, not by what it compares;
  the compiler checks the comparison.
