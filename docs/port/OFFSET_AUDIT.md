# Running the EE's code on a 64-bit host

The game's records were recovered from the PS2 binary's loads and stores,
and much of the decompiled code reaches into them the way the binary did:
by EE byte offset (`(char *)act + 0x4B0`), by EE stride (`i * 0x30` over a
table), through an `int` that holds a pointer, or by copying one record
whole over another record type that had the same EE layout. On the EE,
where pointers and `int` are both four bytes, all of that is correct. On a
64-bit host every record that holds a pointer is laid out differently, so
each of those spellings reads or writes the wrong bytes.

This file describes how the port keeps such code correct: the spelling
conventions used in `ico2/`, the two audits that check every site the host
compiles (`tools/offset_audit.py` and `tools/template_audit.py`, both run by
ctest), the sites where the host spelling differs from the EE's, and what
the audits cannot see. The layout classes and the layout asserts are in
`LAYOUT.md`; the disc-format loaders, which have their own conversions, are
in `LOADERS.md`.

## Conventions in `ico2/`

`ico2/` is the port's own source, compiled only for the host
(`docs/BUILDING.md`, "The game code"): a host fix is made directly in the
code, with no `ICO_HOST` conditional and no PS2 spelling kept beside it.
These are the forms such a fix takes:

- **A field instead of an offset.** The code names the field.
- **`ICO_RAW` and `ICO_RAWP`** (`ico2/fumi/include/ee_view.h`).
  `ICO_RAW(T, p, off, field)` is `field`, with the original
  `*(T *)((char *)p + off)` kept in the text as its arguments so the offset
  audit can check that `field` is the one at EE offset `off`; `ICO_RAWP` is
  the address form. Most of these sites date from when the tree also had to
  compile for the EE; a plain field is as good where no audit is wanted.
- **Nested functions.** clang has no GNU nested functions, so the code has
  a file-scope static with the captured variables passed as parameters
  (written captures by pointer).
- **Pointer-wide words.** A word that holds an object but was `int` in the
  original becomes `ICO_WORD` (a pointer-wide integer, `intptr_t`) or
  `ICO_WORD_PTR(T)` (the pointer type `T`)
  (`ico2/common/include/typedef.h`). Message rings and their receive
  variables are `IosMsgWord` (`fumi/include/message.h`). Words inside
  frozen disc records stay four bytes and hold arena offsets
  (`ICO_EEWORD`, `ICO_EEW`, `ICO_EEPTR`, `ico2/common/include/eeword.h`;
  `LOADERS.md`).
- **Sizes.** A literal allocation size for a record that is wider on the
  host becomes `sizeof(T)`, or `ICO_MAX_SIZE(T, lit)` (`ee_view.h`: the
  larger of the literal and `sizeof(T)`).
  Pointer tables are sized with `sizeof(T *)`.

The records whose host layout departs from the EE's early are the ones the
actor code views most: `GObj` and `Sub15C` from offset 0x4, `Act` from 0x4,
`ActWork` from 0x374 (`floorObj`). Any raw offset into those four is wrong on
the host unless it goes through one of the forms above. The recurring
faults found this way were `GObj + 8` or `+ 0xC` read as `labelId` or
`kind`, `GObj + 0x15C` and `+ 0x164` read as the display object and the
`Act`, `GObj + 0x54` read as the mail box, byte strides over tables of
records that hold pointers (`SqEntry` is 0x30 bytes on the EE and 0x48 on
the host), and template records copied whole over another record type.

## The offset audit (`tools/offset_audit.py`)

The audit looks at every site the host compiles, whether or not a run
reaches it, and the `offset_audit` ctest keeps its findings at zero.

### What it checks

Every `ico2/` unit of the game libraries in a configured build's
`compile_commands.json` is preprocessed with its own host flags and
`-DICO_OFFSET_AUDIT=1`. With that define `ee_view.h` makes `ICO_RAW` and
`ICO_RAWP` leave a marker holding their EE offset (the define is never used
to build the game). In each function body of an `ico2/` source, and in the
initialiser of each file-scope declaration, the audit finds:

| kind | spelling | host access offset |
| --- | --- | --- |
| `cast`, `intcast` | `(char *)x + 0xNN`, `(int *)x + N`, `(int)x + 0xNN` | the literal (a pointer through a 32-bit `int` is TRUNC) |
| `index`, `deref` | `((float *)x)[N]`, `*(int *)x` | N times the element; a 4-byte access to a field that is 8 bytes on the host is a MISMATCH |
| `view_index`, `view_plus` | `f[N]`, `q + N`, `f` a local or a parameter assigned `(float *)x` or `(char *)x + K` | K plus N elements |
| `recast`, `lview`, `slot` | `((V *)x)->m`, `s->m` with `V *s = (V *)GOBJ_ACT(self)`, `((CagePtr *)&o->dobj)->f[81]` | V's host `offsetof(m)` |
| `storage` | `(ClipColReq *)act->flyClip`: a record kept whole in a byte buffer | only `sizeof(V) <= sizeof(buffer)` |
| `ico_raw` | `ICO_RAW(T, p, off, field)`, `ICO_RAWP` | the field's host offset; the field must be the one at EE `off` from `p` |
| `pad` | a `pad<HEX>` or `unk<HEX>` member read or written (not copied whole) | NOFIELD: give it a name |
| `clear` | `memset`, `memcpy`, `memmove`, `bzero`, `bcopy` (and the builtins) whose size is made of literals, over a record, a span from a member (`&s->bits58`) or a whole array of pointers | the host span of what the EE span covered (below) |
| `unproto` | a call with arguments through a `()` declaration | the gnu23 pass (below) |

An index may be spelled as a constant expression, a byte offset over the
element size (`q[0x500 / 4]`, `((float *)x)[0x10 / 4]`): the bracket
parser (`bracket_const`) takes a literal, `N / M` or `N * M`. An offset or
index with a variable stride (`(char *)x + j * 0x50 + 0x10`,
`f[i * 20 + 9]`, `i << 4`) is checked at its first step, every variable
at 1 (`var_terms`, `bracket_linear`), as an `ICO_RAW` offset in one index
variable is: a stride that is the EE size of a record lands on element 1
of the array, whose host offset is one host `sizeof` on.

A clear or copy with a literal size N from EE offset S of a record covers
the EE span S..S+N. On the host the same N must reach the end of the last
member that span covers and stop before the next member (or the record's
end); short of it is **PARTIAL**, past the next member's start is
**OVERRUN**. A span ending inside a member (a pad, a doubleword) is taken
at the same byte when that member's host size is its EE size. N a whole
number of records from a record's start covers that many host records. For
an array of pointers named whole (`memset(wpA, 0, 8)` over `WayPoint
*wpA[2]`) the EE size is 4 bytes an element, so N equal to it must be the
host `sizeof`. A size spelled with `sizeof`, `offsetof` or `ICO_MAX_SIZE`
is the host's own and is not a site.

For each site the compiler gives the operand's type: the operand is wrapped
in a statement expression that initialises a `struct __icoA_<k> *` from it,
and gcc's diagnostic names the type. A `char *`, `float *` or `int` operand
is followed back to its assignments in the function (or, for a parameter, to
the arguments of every call in the unit). Two untyped words take their
pointee from the game's macros: `GObj.act` is an `Act` (`GOBJ_ACT`),
`Act.work` an `ActWork` (`GOBJ_WORK`). A `void *` table element or member
(`sys[2]`, `w->obj`, not a call's result) that the function casts to
exactly one record type elsewhere (`(ChainNode *)sys[2]`) is a view of that
record. An operand in a file-scope initialiser (where gcc takes no statement
expression) is typed in a function appended to the unit.

The record's EE layout is read from the headers' and the `.c` files' offset
comments with `gen_layout_asserts.py`'s parser; a member without a comment
follows the previous one at its EE size (pointers and `ICO_WORD`s are 4
bytes); `pad<HEX>` names give their own offset; members of a record type are
expanded into that record's fields. The access's EE offset then names a
field (`Sub15C` EE 0x50 is `matrix[3][0]`). A probe appended to the unit
compiles `__builtin_offsetof(T, field)` and `sizeof` on the host (in a unit
that includes every header when the unit only declares the record), and
each site is classified:

- **MISMATCH**: the access's host offset is not the named field's host
  offset;
- **NOFIELD**: no named field is at that EE offset (a gap without a
  comment, inside a pad, past the record);
- **TRUNC**: a pointer goes through `(int)`;
- **UNRESOLVED**: a view the audit could not resolve (a view of a record
  with no EE layout, an `ICO_RAW` field it cannot parse);
- **NARROW**: a pointer-wide value converted to 32 bits or fewer (the
  `narrow` pass, below);
- **PARTIAL** / **OVERRUN**: a constant-size clear or copy whose host span
  stops short of the members the EE span covered / runs into the next one;
- **UNPROTO**: a call through an unprototyped declaration that passes a
  pointer, a pointer-wide integer or a float (the `unproto` pass, below);
- **OK** / **SKIP**: the access is the named field on the host / not a view
  of a record (a vector's lanes, a matrix row, an EE word table).

Any of the first eight fails the run (exit 1).

```sh
cmake --preset linux-x64 -B build-host/<dir>                 # compile_commands.json
tools/offset_audit.py --build build-host/<dir>               # findings, exit 1 on any
tools/offset_audit.py --build build-host/<dir> --all         # every site
tools/offset_audit.py --build build-host/<dir> --skipped     # with the reasons
tools/offset_audit.py --build build-host/<dir> ico2/fumi/src/act.c
tools/offset_audit.py --build build-host/<dir> --dump Sub15C # a record's EE fields
tools/offset_audit.py --build build-host/<dir> --no-narrow   # skip the narrow pass
```

ctest runs it as `offset_audit` on native builds (`port/test/CMakeLists.txt`;
about 40 s with the `narrow` and `unproto` passes). Its work files go to
`<build>/offset_audit/`. The tree audits about 18,400 sites in 214 units
with no finding: among them 23 variable strides and 27 constant-size clears
and copies over records or pointer arrays (87 more are over vectors and
byte buffers), 12 calls through `()` declarations, and no raw offset in a
static initialiser.

### Pointer-wide values in 32 bits (the `narrow` pass)

The offset checks see a pointer through `int` only when it is cast next to
a constant. A pointer stored in an `int` member with no cast at all
(`BoxWork.colData`, DIVERGENCES.md D10) or an explicit `(int)` of an
`ICO_WORD` warns under no compiler flag. So `offset_audit.py` also compiles
each unit at `-O1 -fdump-tree-ssa-lineno` (its own flags, in
`<build>/offset_audit/`) and reads gcc's early SSA dump, which spells every
conversion, implicit or cast, with its operand's declared type. On an LP64
host `ICO_WORD` is `long int` and the EE's doublewords `long long int`, so
the two do not mix; the pass runs only there (`__SIZEOF_LONG__` 8 and
`__INTPTR_TYPE__` `long int`; otherwise it says it did not run).

A site is **NARROW** when a value is converted to `int`, `unsigned int` or a
narrower integer and it is a pointer, or a `long` that a load, a call (not
a `size_t` one) or a pointer cast produced. Differences, shifts, masks and
divisions of addresses (sizes, quadword counts) are not reported. Reviewed
32-bit quantities are listed in `NARROW_OK` in the script with the reason
(the scene object word `SceneObj.obj` read as a kind or a table row, a
debug quadword count, the start-stage number) and count as OK. With the two
known defects put back (`box.c`'s `InitBoxGeo` store into
`BoxWork.colData`, `cdvd.c`'s `(int)p < (int)limit`) the pass reports
exactly those sites.

What the `narrow` pass does not see: a pointer read through a 4-byte view
(`*(int *)&p`; the offset checks' MISMATCH covers the common forms). A
`-flto -Wlto-type-mismatch` link of the Linux build lists the declarations
that disagree with their definitions; none of them passes a pointer where
an `int` is read. A `-Wconversion -Wpointer-to-int-cast` build for Windows
x64 (32-bit `long`) found no `long` holding a pointer.

### Calls through unprototyped declarations (the `unproto` pass)

A declaration `T f()` or a slot `T (*fn)()` takes any arguments, after the
default promotions: a pointer goes as 8 bytes, an `int` as 4, a `float` as
a `double`. On the EE an `int` and a pointer are the same 4 bytes, so a
callee that reads an `int` where the caller passed a pointer (or the
reverse) worked there and reads other bytes here. `-Wstrict-prototypes`
warns at the declarations (2,756 warnings at 966 lines in a full build's
log, nearly all function-pointer slots in `typedef.h`, `act.h`,
`gobj_process.h` and the generated data tables), not at the calls. C23 reads `()` as `(void)`, so the pass compiles each unit again
as gnu23 (gcc 14; gcc 13's gnu2x, after a probe that the diagnostic is
there) and every call with arguments through such a declaration is a "too
many arguments" error. The audit probes those calls' argument types: a
call whose arguments are all `int`-sized integers cannot pass a pointer or
a float and is OK; any other must be listed in `UNPROTO_OK` in the script
with what its callees read, or it is UNPROTO. A listed entry that matches
no call fails the run. The pass needs gcc; on another compiler it says it
did not run (the ctest fails on gcc builds when it does not).

The tree has 12 such calls. The six `GsBase.c` debug-menu calls pass an
`int`. The other six are listed: the memory-card debug menu (`debug.c`
`fn(&mc)`: three handlers read the `McMgr *`, `debug_mcFormat` and
`debug_mcUnformat` take an `int` port they never read), the memory-card
segment handlers (`mcard.c` `e->save`, `e->load`: pointers), the thread and
process bodies (`thread.c` `obj->func(arg)`, `obj_manager.c`
`p->func(g2)`: the bodies read a `GObj *`, a `void *` or an `ICO_WORD`;
those declared with an `int`, `subBoyBrainMain`, `subAP1Control`,
`actSt07aChanWay1`/`2`, `actSt07aGirlWay`, `actSt10rGirlWay`, never read
it) and the stage SE procs (`s_init.c` `self->proc(self)`: a `SeSlot *`,
an `int *`, an `ICO_WORD` or nothing). Four declarations whose callees agree were given
prototypes instead: `layout_action.c` `NEGATIVE_SE(void)` (one call passed
a 0 it never read), `girl_act.c` `GirlBrainClearTarget(void *, void *)`
and `motionManager.c` `dispSkelton(GObj *)` (arguments not read), and
`frameDependSequence.c` `fireFDSSlot`'s `int (*fn)(int, void *)`, with
`execWeaponLightOff` taking the two arguments it does not read.

### Limits

- A view through a `void *` table is typed only by a cast of the same
  expression in the same function: before its fix `rope.c`'s `HoldRope`
  read `(char *)sys[2] + w1 * 0x50` with nothing naming `sys[2]` a
  `ChainNode`, which the audit would not see. A call's result is never
  typed this way (`BgAnimation.c`'s `bga_objPtr` returns a different record
  per object type), and a cast in one `case` types the same expression in
  the other cases of the switch. A view of a record with no EE layout is
  reported as UNRESOLVED.
- A variable stride is checked at its first step only; a stride in a
  parenthesised sum with a non-literal term (`(w1 * 0x50 +
  ROPE_EX_OFS)`) is not a site.
- A constant-size clear over a record with no EE layout (a local union,
  a struct of vectors), or over a byte or vector buffer, is not checked;
  of pointer-sized data only a whole array of pointers is.
- The EE layout of a member without a comment comes from its type's EE
  size; a typedef the audit does not know (bit-fields after the first
  word, some libvu0 and libgraph types) stops the layout until the next
  comment.
- `UNPROTO_OK` says what the callees of a listed slot read; a new callee
  stored in a listed slot (a thread body taking an `int` it reads) is not
  seen, only a new call site is.
- The pointee table (`GObj.act`, `Act.work`) is the only type knowledge not
  read from the code.

### Where the host spelling differs

The third column is what the original (EE) spelling read or wrote when
compiled for the host; the code has the fourth (some sites keep the EE
offset in the text through `ICO_RAW`/`ICO_RAWP`).

| file | EE offset | the EE spelling on the host | host spelling |
| --- | --- | --- | --- |
| `fumi/ios/mcdata.c` | the 64-byte rounding of a stack buffer | `(int)buf` truncates the pointer | `__UINTPTR_TYPE__` arithmetic |
| `sugipon/src/box.c` `alignPosition`, `worm.c` `GetWormCaptureVector` | Sub15C 0x50, 0x58 | `matrix[2][*]` | `matrix[3][0]`, `[3][2]`, `matrix[3]` |
| `seki/src/Light.c`, `RegistPacket.c` | Sub15C 0x860 | `(char *)o + 0x860` | `shadowDir` |
| `sugipon/src/flag.c`, `motMan_getFinalMatrix.c.inc`, `motionOrientManager.c` | Sub15C 0xA0, 0x470 | raw offsets | `root.pos`, `ctrl` |
| `sugipon/src/windmill.c` | Sub15C 0, 4 (`WmWork`) | owner as `int` at 0, node at 4 | `parent.obj`, `parent.node`; `owner` is `ICO_WORD` |
| `sugipon/src/enemy.c` | Sub15C 0x854 | `*(int *)&model =` an implicitly declared `int` | `model = GetPObjAddress()`, declared in `charFileManager.h` |
| `sugipon/src/rope.c` | ChainNode + w1 * 0x50, floats 9 and 13 | raw offsets | `ex[w1].v0.y`, `ex[w1].v1.y` |
| `sugipon/src/clothAnimation.c` debug draw | ChainNode 0x10 + j * 0x50, 0x30, 0x80 | raw offsets | `ex[j].w`, `ex[0].v1`, `ex[1].v1` |
| `fumi/src/commonact.c` | Sub15C 0x654, 0x5F8, float 81 (0x144); Act 0x38, 0x4C | `FlyLimitSub`, `FlyCtlJ`, `CagePtr->f[81]`, `LadderMotWork`, `StoneSub` views | `ctrl.noFieldClip`, `ctrl.floorAttr`, `root.step[1]`, `pushDir`, `modeFrame` |
| `fumi/src/commonact.c` `IsFlyTimeOver` | | an `int` parameter given `(ICO_WORD)self` | `GObj *` |
| `fumi/src/act-game.c` `hand_able_connect` | ActWork 0x540..0x561 | `h[0x540]` | `handCl.on/hit/attr/hit2/attr2` |
| `fumi/src/act-game.c` | Act 0x74 (bit 18's object) | an `int`, read back as a `GObj *` | `statusVal18` is `ICO_WORD` |
| `fumi/src/act-game.c` `PAIR_GetPosition_BOY`, `PAIR_GetPosition_BOY_DITCH` | Act 0x500, 0x4C0; 0x510, 0x520 (`[0x500 / 4]` index form) | the boy's way-walk fields (all zero in play), so `actGirlPulledReady` and `actGirlDitch3mReady` sent the girl to the world origin instead of under the boy's ledge | `env.cliffStepPos`, `env.cliffOrient`, `env.ditchPos`, `env.ditchDir` |
| `fumi/src/act-game.c` `ORQ` / `ORM` (`ActOrientTest`) | Act 0x478 | the wish words are at 0x548 on the host | index `&((Act *)s)->wish0` |
| `fumi/src/act-game.c` mails 263 to 265 | ActWork 2064, 2112, 2160 | not `effRec` | `ICO_RAWP` to `&effRec[0..2]` |
| `fumi/src/act-game.c` flyer mail 298 | `&w1 + 0x80` | the middle of `ClipWork.filter` | `w1.wall` into `MotOriTarget.wall` |
| `fumi/src/act-wish.c` `chkOrient` | Act 0x4B0 | `s + 0x4B0` | `env.wallOrient` |
| `fumi/src/act.c` | GProc 4; ActWork 0x8B0 via `(int)GOBJ_ACT(self) + 0x688` | raw; a pointer through `int` | `owner`; `intrReq` |
| `fumi/src/act.c` `act_check_mail`, `act_check_intr_list` | GObj 0x54; `IntrMail` 0x16; MotCtrl 0xC | the high half of `dlNext`; the EE's 8-byte mail stride | the GObj's `mailBox`, `IosMailBox` fields, `m->flags >> 16`, `MotCtrl.shifted` |
| `fumi/src/enemy_act.c` | Sub15C root + 752; GObj 0x15C, 8; Act 0x20 (`EnemyBrainWork`) | raw offsets | `root.lookPos`; `dobj`; `labelId`; `flags20` |
| `fumi/src/boyact.c` | ActWork 0x8D0, 0x8C0; GObj 8, 0xC, 0x15C; Act at GObj + 0x164 | raw offsets and the `BoyExt` view | `cliffOrient`, `&intrReq.b.wall`; `labelId`, `kind`, `GOBJ_SUB`; `EnemyBattleWork` fields by name (`BOY_EXT_F`) |
| `fumi/src/girl_act.c` | ActWork 0x520..0x528, 0x3B0 (`ActPara`), 0x448; Act 0x180, 0x184, 0xD0, 0xD4, 0x350, 0x1E0, 0x48 (`actGirlStart`); GObj 0x15C | raw offsets; 8-byte pointers into 4-byte EE slots | `hintPosX/Y/Z`, `turnMailWait`, `stopFrames`; `heldItem.i`, `nextItem.i`, `mainMail`, `mail`, `wayMode`, `life`, `actKind`; `dobj` |
| `fumi/src/girl_brain_attract.c.inc` | EnemyBattleWork 0x230, 0x240 (`GirlSofaWork`); Sub15C 0; GObj 0xC | wrote `clingReq`/`clingTarget`/`liftedObj`; read 4 bytes of `parent.obj`; `labelId` | `readyPosX..Z`, `readyDirX..Z`; `parent.obj` and its `kind` |
| `fumi/src/act-way.c`, `act-env.c` | GObj 0xC; the `EnvMotion` view (0x110, 0x114, 0x130, 0x138) | `labelId`; other `MotCtrl` bytes | `kind`; `cliffHeight`, `cliffDist`, `wallFloorHeight`, `wallDist` |
| `fumi/src/commonact.c` | Act 0x4B0, 0x680, 0x100; GObj 0x15C; debug overlay offsets | not `env`, `enemy`, the scratch vector, the display object | `&GOBJ_ACT(self)->env`, `enemy->stonePair`, `Act.curRoot`, `GOBJ_SUB`, `ctrl.motion`, `actMode` |
| `sugipon/src/motionOrientManager.c` `SetMotionRequest` | Sub15C 0x470 | returns the EE offset of `ctrl` | `&GOBJ_SUB(self)->ctrl` |
| `sugipon/src/motionManager.c` `GetMatrixOfMotion` | `objLayout[*(int *)(self + 8) * 76 + 70]` | `labelType`, and the wrong `GenGeo` stride | `((GenGeo *)objLayout)[self->labelId].kind` |
| `sugipon/include/flag.h` `FLAG_ALLOC_NODES` | Sub15C 0xC, 0x10 | `parent.node` and `nodeNum`, leaving `nodeMtx` pointing at a freed block | `nodeMtx` / `nodeQuat` by name |
| `sugipon/src/enemy.c` `EnemySetfDisappear` | Sub15C 12 | the high half of `parent` | `nodeMtx` |
| `sugipon/src/worm.c` `SetWormReduceRatio` | WormWork 8 | the `src` pointer | `reduce` |
| `script/src/script.c` `scpPlayWaitMotEnd` | `motReq + 0x5C` | another `MotCtrl` field | `MotCtrl.frameEnd` |
| `script/src/st08a.c`, `script.c` | GObj 80 (`drawMask`) | `((int *)gobj)[80 / 4]` | `SCP_CLEAR_DRAWMASK` / `SCP_SET_DRAWMASK` |
| `script/src/st17a.c` | `((int *)boyGObj)[87] + 1256` | not `ctrl.noStand` | `GOBJ_SUB(boyGObj)->ctrl.noStand` |
| `fumi/sound/s_init.c` `soundVBlank`, `soundDataSegAllClose`, `soundDataSegNextStageNotUseClose` | `soundDataTbl` by 0x30 to 768 | `SqEntry` is 0x48 on the host: the ADPCM tick missed the second entry on | `SQ_STRIDE` / `SQ_END` (`sizeof`) |
| `fumi/ios/pad.c` | IosPadBuf 0..3 | `*(unsigned int *)b >> 12` over `pad0` | `termId >> 4` |
| `fumi/ios/cdvd.c` | | `bgRunning` held the running request in an `int` | `ICO_WORD` |
| `fumi/ios/cdvd.c` `iosCdvdBackGroundMgrDeleteRequestGet`, `iosCdvdBackGroundMgrEntryNum` | | walked `bgReqTable` while `(int)p < (int)limit`: the low 32 bits of two host addresses | `(ICO_WORD)p < (ICO_WORD)limit` |
| `fumi/ios/cdvd.c` `iosCdvdBackGroundMgrGetRunning`, `iosCdvdDiskReady`, `iosCdvdLoad` (no callers) | | returned or took a request address as `int` | `ICO_WORD` return and parameters |
| `fumi/ios/shockdriver.c` `ShockDriver_GetShockVoiceMax` (no callers) | ShockVoiceSet 0 | `(int)arr[idx]`, then `*(int *)p` | `arr[idx]->top.half[4]` |
| `sugipon/src/box.c` `InitBoxGeo`, `ReInitBoxGeo` | BoxWork 0x2C | `colData`, an `int`, held `Sub15C.colData` and gave it back truncated (DIVERGENCES.md D10) | `ICO_WORD`, width asserted |
| `omori/src/ebrain.c` `eBrainGetTarget` | .bss `boyTargets` 0x380, `girlTargets` 0x400 | index -1 stored into another file's storage (DIVERGENCES.md D13) | the EE's neighbour word cleared by name |
| `common/src/DObj.c` `CSVSYSTEM_InitDObj`, `initGeometryState` | the 0x880-byte `DObjRecord` template; a stack GObj stand-in | too small on the host, every field after the first pointer misplaced | `sizeof(Sub15C)`, the template's non-zero words by name; a real `GObj` (`DOBJ_D`) |
| `omori/src/chain.c` `InitChainGeo` | 28 doublewords over `ChainRecord`; `(i << 5) + (int)cw->node` | pointers copied as words; truncated node addresses | a typed struct copy; `CHAIN_NODE_ADDR` |
| `omori/src/brain.c` | `(int)&brainGirl + 0x28` | a truncated address | `BRAIN_TGT_ADDR` |
| `omori/src/mail-add-data.c` | `EnemyBattleWork.pad0[84]` (a count and 10 entries) | 16-byte entries overrun into `speedRatio` | `InitMailAdditionalData` allocates the table from the enemy work's partition |
| `omori/src/generator.c` | `GenWork` 112 bytes; `w->hard = (int)pointer` | too small; truncated | `ICO_MAX_SIZE`; the host stores `!= 0` (the word is only tested against 0) |
| `common/src/staffroll.c` | a 16-byte stride over `StaffRollEntry` | the entry is 24 bytes | indexed by entry |
| `common/src/layout_action.c` | `McMgr` at 0x10, 0x24, 0x4C, 0x50, 0x454, 0x47C; `port` at `p + 8` | `McMgr` holds a pointer at 0x48 | `result`, `segment`, `sum`, `readSum`, `dirName`, `path`, `port` |
| `common/src/kanban.c`, `layout_texture.c` | `LtProperty` stride 0x70, `texData` at `texNo - 4` | `LtProperty` is 0x78 with an 8-byte `texData` | fields by name |
| `common/src/sceneManager.c` | `*(int *)&gobj->dobj = (int)dobj`; the parent link at `GOBJ_SUB(self) + 0/4` | truncated stores | direct stores; `ICO_WORD_PTR(GObj *)` |
| `common/src/icoMisc.c` | `(unsigned int)e > 0x1FEFFF0` | an EE address test | `!ico_arena_contains(e, 0)` |
| `script/src/st04a.c` | `ActSt04A`, `PObjGObjSt04A` pad views of `Act` and `GObj` | the torch actor's words in the wrong places | `Act.torchAnim`, `Act.doorCamera` (unused by this actor), `wish0.w[0]` (where the EE keeps it) |
| `script/src/stageSEProc.c` | `SEObj`, a pad view of `s_init.c`'s `SeSlot` | wrong for a record with pointers | `SeSlot` itself, exported from `fumi/include/s_init.h` |
| `ito/src/itou_boss.c` `itou_boss_gflag_init` | one `memset` over `gflag[16]` and `capsule[53]` | assumes the EE linker's order: ran 3,392 bytes past `gflag` | two `memset`s |
| `fumi/ios/shockdriver.c` `Init_Shock` | `ShockDriver` is `int[4]` used as a `ShockMgr` | too small (24 bytes on the host) | a static `ShockMgr` |
| `omori/src/camera-ico2.c` `targetAPrev`, `targetBPrev` | `float[3]` written by a 16-byte `sceVu0ScaleVector` | the fourth word overran the next global (on the EE each sits in its own 16-byte `.bss` slot) | four words |
| `fumi/sound/s_init.c` `soundSeEnvNotUseClose` | StgPre 0x110, 0x114, by the EE's 0x194 stride (`D_005F5E60[a * 404]`) | `StgPre` is larger on the host (`endproc` and `initproc` are pointers), so for any stage but 0 the SE environment range came from the wrong record: a garbage range, and a SIGSEGV there (`StageManager`, `exit_stage`) on 14 of 167 stage transitions (package X5) | `stageData[a].seEnvFirst`, `.seEnvLast` |
| `fumi/sound/soundManager.c` `sndInit` | StgPre 0x18C (`*(unsigned short *)((char *)&stageData[idx] + 0x18C)`) | 8 bytes short of the host's record (two pointers precede it): the low half of `handCameraRate`, not the reverb depth | `stageData[idx].reverbDepth` |
| `fumi/src/act-game.c` `_ACTCharStatus_Clear` | Act 0x58..0x90 | `memset(&s->bits58, 0, 0x38)` cleared a short prefix of the host's wider span: `gobj80`, `gobj84` and `statusObj` kept stale objects the EE clears (the final review; the `clear` check reports it) | `offsetof(Act, paraStatus) - offsetof(Act, bits58)` |
| `fumi/src/act-env.c` `ditchProbe`, `act-game.c` `ACTCheckCollis_CI`, `commonact.c` `getLandOffset`, `actCommonEdgeHang` | ClipWork 0xC0 | the EE size of a 240-byte host record: its tail stayed stack garbage (the final review; the `clear` check reports them) | `ICO_MAX_SIZE(ClipWork, 0xC0)` |
| `fumi/src/way_util.c` `set_bridge` | `WayPoint *wpA[2]`, `*wpB[2]`, cleared with 8 bytes | the first pointer only: when one end found a single candidate its second stayed stack garbage, and when both ends' nearest points coincide that garbage is tested against 0 and dereferenced as the bridge's way point (found by the `clear` check) | `sizeof(wpA)`, `sizeof(wpB)` |
| `seki/src/BgAnimation.c` `bga_CalcObject`, types 8 and 9 (the ambient volumes) | `BgaObj.rscale`, 0x70 | `BgaObj`'s `mtx` and `quat` pointers put `rscale` at 0x7C on the host, so `1 / scale` went into the volume's `size[3]`, `lightScale` and padding and `size` (0x70) was never written: every ambient volume's extent was whatever the heap held (zero on a cold boot, so every point was inside the first volume; the last stage's words after a stage change, a NaN in `light_getAmbientLight` on stages 27 and 49, package X5) | `BgaLightEnv.size` (0x70, asserted against `AmbientVolume.size` in `Light.c`) |

Fields that were pads and are named because code reads them: `Act.bits60`
(0x60), `Act.curRoot` (0x100, `test_CURRENTROOT`'s buffer), `Act.torchAnim`
(0x470), `ActWork.cliffOrient` (0x8D0), `IosPadBuf.status`/`termId` (0, 1),
`PObjModel.spare` and `PObj.spare` (0x2C), `GirlBrainWork.inWarningCheck`
(0x58F1), `GProc.arg` (0x20; on the host `pad20` would be the `func`
pointer).

Raw offsets that remain are pointer-independent and right on every host:
collision rows read through `ico_eeptr` (`fieldCollision.c`), `CamGroup`
(76) and `PinRec` (92) strides (asserted), motion file elements, float
matrices in the scratchpad (`matrixptr + 0x80`), vector and quaternion
components (`*(float *)((char *)out + 0xC)`), vertex records,
`gameSysObjInfo` rows (0x40, saved), and the save records `BoyKidnapWork`
and `characterPacket`.

## Whole-record copies (`tools/template_audit.py`)

The offset audit checks member accesses. A record copied whole over
storage of another record type has no member access to check, and two of
the port's worst 64-bit bugs came from it: `InitMotionGeoInfo`'s
`MotionGeoInfo` template over `MotRoot` (node 1's quaternion zeroed, then a
NaN skeleton) and `InitMotionStateInfo`'s
`*(MotionStateInfo *)self = motionStateInfoTemplate` over `MotCtrl`
(`floorFit` 0 for every actor: DIVERGENCES.md D7). On the EE both layouts
coincide; on the host they differ when one side has a `Vec4` (8-byte aligned
by its `long long` view) where the other has `float[4]`, an `int` where the
other has a pointer, or other padding. The second audit finds every such
copy and requires its layout to be asserted where it is made.

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
  T: `*(BoyWork *)boyInfo`, `*(McName *)mp->path`), or E is pointer
  arithmetic (storage carved from a file image or a buffer:
  `(PinRec *)(groups + n)`). Listed with `--all`; such a buffer has no
  layout of its own to disagree;
- otherwise the copy needs a **registration** in its unit: a
  `_Static_assert` naming `sizeof(T)` and U, and, for every named member m
  of T (pad members excepted), one naming `offsetof(T, m)` and U. A record
  that only moves bytes (a union, a single member, 64-bit words only:
  `ICO_QW`, `Blob64`, `DObjBlk40`) on either side needs the size only.
  `ico2/fumi/include/ee_view.h` spells them: `ICO_LAYOUT_AT(T, tm, U, um)`,
  `ICO_LAYOUT_AT_FROM(T, tm, U, base, um)`, `ICO_LAYOUT_SIZE(T, U)`;
- **UNREGISTERED**: a registration is missing (the members without one are
  named);
- **MISMATCH**: the audit's own host probe differs: a member T and U share
  by name at different offsets, T longer than U (or than the room after U's
  member), or a 16-byte aligned T (the host compiler copies it with aligned
  SSE moves, `movaps`) over a variable or a type with less alignment.

MISMATCH and UNREGISTERED fail the run (exit 1), as do the findings of
the `void *` views below. ctest runs it as `template_audit` next to
`offset_audit` on native builds (about 20 s). The tree has about 200 record
copies, all OK, SAME or RAW, and 10 views through `void *`, all OK or
SAME.

```sh
tools/template_audit.py --build build-host/<dir>          # findings, exit 1 on any
tools/template_audit.py --build build-host/<dir> --all    # every record copy
tools/template_audit.py --build build-host/<dir> ico2/sugipon/src/motionManager2.c
```

Run on a `motionManager2.c` with the old template it reports
`MotionStateInfo over struct MotCtrl (host view/storage): dir 192/188,
lastDir 208/204, ...` (MISMATCH) and `MotionGeoInfo over struct MotRoot: no
offsetof assertion for 76 of 80 members` (UNREGISTERED).

### Alignment fixes

Three copies were right only because of where something happened to be
placed:

| site | storage | why | host fix |
| --- | --- | --- | --- |
| `omori/src/camera-root.c` `InitCamera`, `CameraSetTargetGObj` (4 copies) | `cameraSet`, `targetCameraSet` (`CameraSet2`, 4-byte aligned) | `CamTgt` is `aligned(16)`; gcc copies it with `movdqa`/`movaps`, so the statics must be 16-byte aligned, which the linker only happened to do | the two statics are `aligned(16)` |
| `sugipon/src/multiBgaManager.c` `InitMultiBgaManager`, `stageMultiBgaManager.c` `InitStageMultiBgaManager` | `InitialBgaMultiAnimeState` (`BgaAnimeState`, 4-byte aligned) | read through `BgaDisp` (`aligned(16)`) with `movdqa` | declared `aligned(16)` in `multiBgaManager.h` |
| `script/src/st04a.c` `finishCallBackFunc` | each node's `MotIk` (4-byte aligned, heap) | an `Mtx44` (`aligned(16)`) store; right only because `iosMalloc` returns 16-byte aligned blocks | `__builtin_memcpy` of the same 64 bytes |

### Registrations

| file | copy | registered |
| --- | --- | --- |
| `sugipon/src/motionManager2.c` | `MotionGeoInfo` template over `MotRoot` | all 80 members: 74 by name, `rot`/`nextPos`/`fieldPos` at `quat`/`move`/`footPos`, `word10C`/`wall110`/`word11C` and `vec1E0`/`vec1F0` inside MotRoot's pads; size |
| `sugipon/src/motionManager2.c` | `MotionStateInfo` template over `MotCtrl` | all 98 members and the size (the template spells `MotCtrl`'s alignment and width: `MsiVec4`, `ICO_WORD`) |
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

### Views through `void *`

A record handed on through a `void *` has no type the copy checks can
read: the climb mail passes `&enemy->climbOrient` to
`ActSendMail_WithAdditionalData`, and `actCommonRopeClimbEnd1` reads
`Act.intrData` whole as a `ClimbEndRec` (DIVERGENCES.md D8, found only by
a replay). The audit treats each such `void *` as a channel and pairs its
writers with its readers across all the units:

- **The mail data.** A writer is the data argument of
  `ActSendMail_WithAdditionalData` (`MAIL_SENDERS`, with its mail number
  when it is a literal); a reader is a cast to a record of `Act.intrData`
  or of `GetMailAdditionalData()`'s result, or of a local assigned from
  them, under the `case N:` of mail N when it has one.
- **A `void *` member** R.m: `x->m = E` writes, `(T *)x->m` reads. A
  member written with more than one record type (an actor's work in
  `Sub15C.work`, one per object kind) cannot be paired by type and is left
  out.

A writer's storage is its argument's static type: a record (`&rec`, a
`T *`) or a record from a member (`&x->f`, an array member `x->f`, which
is R from f); a `void *` or `char *` local is followed to its assignments,
and a `void *` the function casts to one record elsewhere
(`(MotOriTarget *)s->addData`) has that type. Data that is not a record (a
`float`, `void **`) is left out. A mail reader under `case N:` reads mail
N's writers; any other reader is paired with the writers whose storage
holds the reader's record on the EE: element by element (arrays split),
each of the reader's elements over one of the writer's at the same EE
offset, of the same size and a kind the EE stores the same way (a 4-byte
`int` over a pointer is such a pair; a `float` over a pointer is not; the
writer's pads take any element; a union needs one member that holds).
Each pair is then checked on the host: every element at the host offset of
its partner less the writer's member offset, of the same host size, the
record within the storage, and a 16-byte aligned record on a 16-byte
boundary. A difference is MISMATCH. A mail reader that no writer holds is
UNPAIRED and a mail writer whose storage cannot be typed UNTYPED, unless
`tools/template_audit_allow.txt` lists the site with a reason (a line
matching no site is STALE); the list is empty.

| reader | writers | pair |
| --- | --- | --- |
| `commonact.c` `actCommonRopeClimbEnd1`: `ClimbEndRec` | `commonact.c` chain and cage climbs: `&enemy->climbOrient` | `EnemyBattleWork` from `climbOrient`, 12 elements; with `climbOrient`'s `aligned(16)` removed it is a MISMATCH (the quadword alignment, `climbCol` and `obj` 4 bytes off) |
| `boyact.c` `actBoyReadyMove`: `BoyMoveOrder` | `act-game.c` mails 263 to 265: `&effRec[0..2]` | `ActWork` from `effRec[k]` (`ActEffRec`; `_2C` over its pad) |
| `commonact.c` `ACTGetOrientFromIntrK` mail 298: `MotOriTarget` | `act-game.c` flyer mail 298: `s->addData`, cast to `MotOriTarget` there | SAME |
| `commonact.c` `ACTGetOrientFromIntrK` mail 54: `MotOriTarget` | `boyact.c` sofa mail 0x36: `&sofaWallHit` | `WallCfg`, as the union's `wall` |
| `Shadow.c`: `ClusterPoly` | `Shadow.c`: `ShadowPoly *` into the `void *` `polys` | 4 elements |

The commonact.c assertions of the D8 view stay (`ClimbEndRec` from
`climbOrient`, the quadword offset). The pairing also found a dead view:
`enemy_act.c` `subEnemyBrain_Irregular` cast `GObj.act` (written only with
an `Act`) to an `EnemyBrainWork` whose `flags` at 0x20 is `Act.flags20` on
the EE but not on the host; the view was never read and is removed.

### Copies the audit cannot see, asserted in place

- `sugipon/src/rope.c`: `ropeChainInit` (`RopeTemplate`) goes to
  `InitChains(void *)`, which reads it as `ChainCfg`. The host layouts agree
  (node 0x10, step 0x14, root 0x20, weight 0x40, 0x50 bytes); asserted in
  `rope.c`.
- `fumi/src/boyact.c`: `boyInfoDefault` (a `BoyWork`) resets the `BoyInfo`
  kept in `long long boyInfo[24]`; asserted that the template covers the
  whole host record (96 bytes), that the pointers start at 0x20, and that
  `BoyKidnapWork` is the character packet's size.

Not a layout fault: `sugipon/src/clothAnimation.c` copies a texture's
`TexData` into the cloth through `TexBlob` (89 doublewords, the EE size); on
the host `TexData` is wider, so the copy keeps only its head. The copy is
only ever read for its name at offset 0 (`tex_GetTextureNo`), which the head
holds.

Copies out of the runtime-loaded ELF tables: the loader writes each table
in its own declared type's host layout (DATA.md, "The data tables"), so a
copy out of a table is a copy out of that type. The only view copies of a
loaded table are `motionOrientManager.c`'s `motionLimitDef` rows, SAME.
File data laid over records (`memcpy(&GlobalStageSetting, ...)`, `.pef`
packages, camera sets, TIM2 headers) is RAW: those records' host layouts
must equal the file's EE layout, which `port/test/layout_asserts.c` checks
per record.

### Limits

- A copy through a `void *` parameter (`InitChains`) carries no record
  type; such storage is RAW or not seen. A `void *` member written with
  several record types (`Sub15C.work`) is not paired, and mail data sent by
  another function than `ActSendMail_WithAdditionalData` is not a writer.
- An implicit conversion between record pointers at a call
  (`-Wincompatible-pointer-types`) followed by a whole copy in the callee is
  not seen: the callee's copy is of its own parameter type.
- Static initialisers are not scanned (they cannot copy records).
- The member list of T comes from its definition in the unit (anonymous
  members flattened); a T with no definition there is UNREGISTERED.
- A registration is recognised by what it names, not by what it compares;
  the compiler checks the comparison.

## Allocations

Every `iosMalloc*` and `mallocseki*` call whose size is a literal or a
literal stride has been compared with `sizeof` of the record on the host.
Records with pointers use `sizeof` or `ICO_MAX_SIZE` (for example
`PacHeader`, `PObjGroup`, `MatLine`, `PacLineSet` in `Packet.c`; `Fan2D`,
`Mesh3D`, `PrimParticle` in `Primitive.c`; `Light` and `AmbientVolume`;
`BgaPlayNode`; the way tool's cursor `Act`). The literal sizes that remain
are records without pointers (the same on every host), raw byte buffers
(DMA, packet and stream buffers, the 8 KB inflate block), the `.gcm` disc
records (`CamSetFile` 16, `CamGroup` 76, `PinRec` 92, asserted), and the
partition sizes in `fumi/ios/ios.c`, which the
host enlarges (PLATFORM.md, "Heap").

Open items are in `docs/TODO.md` (Game code).
