# Struct layouts on 32-bit and 64-bit hosts

Package 2B. The game's records were recovered from the ROM's load and store
offsets, and the headers carry those offsets as `/* 0xNN */` comments. This
page describes how they are checked on every host, which records must keep
the EE layout on 64-bit hosts, the pointer-width fixes made so far, and what
packages 2C, 5 and 2D-2F still have to do.

## Layout asserts

`tools/gen_layout_asserts.py` reads every header under `ico2/` (not
`ico2/vusrc/`; `sugipon/include/girlForceField.h` is skipped because it does
not compile on its own and holds no commented struct), with the host
build's view of `#ifdef ICO_HOST`, and writes `port/test/layout_asserts.c`:

- `OFF(T, member, 0xNN)`, an `offsetof` `_Static_assert`, for each struct
  member with an offset comment (a trailing `int a; /* 0x10 */` or a
  leading `/* 0x10 */ int a;`), and for each `pad<HEX>` / `_pad<HEX>`
  member, whose name is its offset. Members of inline nested structs and
  unions are asserted as `outer.inner`; their comments are absolute offsets
  when the value is at least the enclosing member's, else relative to it.
  Bit-fields are skipped.
- `SIZE(T, 0xNN)` where the size is known: `size=` in the classification
  file, or the first "N-byte" / "N bytes" in the comment right before the
  struct that is not an alignment ("16-byte aligned").
- Every struct the asserts touch must have a line in
  `config/struct_classes.txt`; the generator stops otherwise.

The test target `layout_asserts` (`port/test/CMakeLists.txt`) compiles the
file as the game compiles: the game's include path, `ICO_HOST`,
`ICO_SEMANTIC_OPTIONS` and `ICO_GAME_LAYOUT_OPTIONS` (`-malign-double` on
32-bit x86, `-mno-ms-bitfields` on Windows). A moved field fails the build.
ctest also runs `layout_asserts_fresh`, which fails when the committed C
does not match the headers and the classification. After changing either:

```sh
tools/gen_layout_asserts.py          # rewrite port/test/layout_asserts.c
tools/gen_layout_asserts.py --check  # what layout_asserts_fresh runs
tools/gen_layout_asserts.py --list   # every struct and the checks found
```

### Classes (`config/struct_classes.txt`)

`<name> <class> [size=] [base=] [pending=] [nosize]  # evidence`

| class | meaning | 32-bit presets | 64-bit presets |
| --- | --- | --- | --- |
| `runtime` | natural host layout, real pointers | asserted (the EE layout) | not asserted |
| `overlay` | read directly from disc or ELF bytes: frozen; a pointer field becomes a `u32` offset or is resolved by its loader | asserted | asserted, unless `pending=` |
| `save` | serialised into the save image or the card files: frozen | asserted | asserted |

`pending=<package>` marks a frozen struct that still has pointer fields:
its 64-bit asserts are compiled only with `-DICO_LAYOUT_PENDING=1` until
that package converts it and deletes the option. Every pending struct fails
on 64-bit today (checked with `ICO_LAYOUT_PENDING=1` on x86-64 gcc), and no
non-pending frozen struct does.

### Counts

| class | structs | offset asserts | size asserts |
| --- | --- | --- | --- |
| runtime | 109 | 1,477 | 35 |
| overlay | 74 (15 pending) | 570 | 67 |
| save | 5 | 22 | 2 |
| total | 188 | 2,069 | 104 |

Of the 2,069 offset asserts, 1,872 come from comments and 197 from `pad`
names. On the 32-bit presets all 2,173 hold (`ref-m32`, `win-x86-ref`); on
the 64-bit presets (`linux-x64`, `win-x64`) the 435 asserts of the 59
non-pending overlay structs and the 5 save structs hold.

### What the asserts found on 32-bit

No offset or size in a header disagrees with the compiled layout. The
first run showed conventions the generator now records rather than
layout errors:

- `JimakuSub` (`fumi/include/jimaku.h`): its comments are offsets within
  `JimakuArg`, where it sits at 0xC (the header says so); `base=0xC`.
- `McFileInfo` (`common/include/typedef.h`): the comment in front of it
  describes `McProductFile` (the 0x1F0-byte product file), which has no
  comment of its own. The comment sits on the wrong struct; worth moving
  upstream (comment only, no code change).
- `LightningNode`, `ViBuf`, `PrimParticleObj`: their comments' "N-byte"
  describes a member or element, not the struct (`nosize`).
- `PrimParticle`: `prim_InitParticle` allocates 416 bytes, the 0x198-byte
  record rounded to a quadword; not a size mismatch.

## Pointer-width types (`common/include/typedef.h`)

The EE build must stay byte-identical, and its code generation depends on
the declared types of these words (the header's notes record objects that
move when a word is retyped). The host types therefore come from two
macros whose EE expansion is the original `int`:

| macro | EE | host |
| --- | --- | --- |
| `ICO_WORD` | `int` | `__INTPTR_TYPE__`: a pointer-wide integer, so integer arithmetic on the field (byte offsets, masks) means the same |
| `ICO_WORD_PTR(T)` | `int` | `T`: for a word only ever converted to a pointer |

Retyped fields (32-bit layout unchanged, which the asserts confirm):

- `GObj.act` (0x164): `ICO_WORD_PTR(void *)`; `Act.work` (0x688):
  `ICO_WORD_PTR(void *)`. `GOBJ_ACT` and `GOBJ_WORK` (`act-game.h`) need no
  change.
- `Sub15C.nodeMtx`, `nodeQuat`, `colData`; `MotRoot.fixObj`; the `.i`
  members of `ActEnv.cageObj`, `ActEnv.sofaObj`, `Act.heldItem`,
  `Act.nextItem`; `PObjGObj`'s address words (`self`, `next`, `prev`, `fn`,
  `kindNext`, `dl`, `mailArg`, `sub`, `act`): `ICO_WORD`.
- `GOBJ_SUB(o)` reads `GObj.dobj` (already `Sub15C *`) directly on the host;
  the EE keeps its int-typed read. `commonact.c`'s `LADW` reads `Act.enemy`
  (0x680) on the host instead of `*(int *)(act + 0x680)`.

`-Wint-to-pointer-cast` and `-Wpointer-to-int-cast` on `linux-x64` (gcc 14,
HEAD and HEAD plus this package, the same tree otherwise): 4,112 and 371
before (4,483), 567 and 333 after (900). `GOBJ_ACT`, `GOBJ_SUB` and
`GOBJ_WORK` were 1,455, 1,434 and 258 of the 4,112. `-Wincompatible-pointer-types`
went from 66 to 84: the 18 new ones are the message-queue callers below,
which are real 64-bit faults the old `int` signature hid.

## The ios layer

- `fumi/include/message.h`: a message is `IosMsgWord`, `int` on the EE and
  `__INTPTR_TYPE__` on the host; the ring (`IosMsgQueue.buf`), `iosMsgSend`'s
  value, `iosMsgRecv`'s destination and `iosMsgSetEvent`'s value use it.
  `thread.c` sends and receives the dying thread's pointer through it and
  sizes the join queue as `sizeof(IosMsgQueue) + 8 * sizeof(IosMsgWord)`
  (80 bytes on a 32-bit host, as on the EE). `message.c` allocates the
  event thread as 16576 bytes or `sizeof(MsgEventThread)`, whichever is
  larger.
- `fumi/include/memory.h`, `fumi/ios/memory.c`: addresses are `IosMemAddr`
  (`unsigned int` on the EE, `__UINTPTR_TYPE__` on the host), and the
  record sizes are named: the block header `NODE_SIZE` (0x40), the
  partition record `PART_SIZE` (0x50) and what they imply (`PART_NEED` 144,
  `PART_MIN` 160, `NODE_QW` 4). On the host they come from the records;
  `IosMemNode` is 16-byte aligned there in place of its `pad3C`.
  A `_Static_assert` pins the EE values on 32-bit hosts. **On a 64-bit host
  the block header is 0x50 bytes and the partition record 0x70**, so every
  partition and block lands at a different offset than on the EE and each
  allocation costs 16 bytes more; partition sizes are unchanged.
  `iosReallocDebug`'s `node->size - 0x40 < n` is kept as written (it
  compares quadwords with 64 on the EE too, so a block under 65 quadwords
  cannot be shrunk).
- `ios.c` passes the arena addresses to `iosMallocInitPartition` without
  truncation.

`memory` and `ios_chain` pass on `ref-m32` with the EE partition addresses.
They still skip on 64-bit hosts (their expected addresses are the EE's). A
scratch run of the allocator on x86-64 with ASan and UBSan (a 64 MB buffer
above 4 GB, 20 rounds of 500 mixed and 128-aligned allocations freed in
random order, each round coalescing back to the starting free block, and a
realloc) passed; it is not committed.

## Byte identity of the PS2 build

Every edit keeps the EE's tokens or types: `#ifdef ICO_HOST` with the
original in `#else`, or typedefs of the original type. All 223 game objects
compiled with `tools/compile_c.sh` (ee-gcc 2.9) from HEAD and from HEAD plus
this package have identical `.text`, `.data`, `.rodata`, `.sdata`, `.sbss`,
`.bss`, `.lit4`, `.lit8` and `.rdata` bytes, relocations and symbol tables.

## Handoffs

### 2C, loader conversions

Pending structs (`pending=2C`): `BgaHeader` (`dobjs`, `roots`, `anim`),
`PObjPart` (the `.p2o` part record and its relocated pointers; R4 says
decode it into a runtime struct and freeze the file record), `CamGroup`
(`items`), `FcWallEnt` (`normal` at 0x4C; whether the loader relocates it
or sets it was not traced).

Disc records defined in `.c` files, so outside these asserts. 2C should
move each into a header with offset comments (or assert in place) and
classify it `overlay`: `ObjHdr`, `ObjRec`, `PObj`, `PObjSub`
(`common/src/PObj.c`), `Coll`, `Bone` (`common/src/charFileManager.c`),
`PackEnt` (`fumi/ios/cdvd.c`), `CamSetFile`
(`omori/src/camera-ico2.c`), `MotFileHdr`, `NodeRec`, `FacialRec`
(`sugipon/src/motionFileManager.c`), `BgaDObjEnt`, `BgaEnvEnt`,
`BgaMotion`, `BgaKey`, `BgaSdfCam` (`seki/src/BgAnimation.c`),
`Tim2Picture`, `Tim2Mipmap` (`seki/src/Texture.c`, whose u64 fields need
8-byte alignment). Their remaining pointer-cast warnings: `PObj.c` 31,
`charFileManager.c` 21, `StageAnimation.c` 52, `BgAnimation.c` 29.

### 5, runtime ELF table loader

`pending=5`: `ActModeRec`, `IntrMail`, `LtProp`, `LtProperty`,
`ObjKindEnt`, `PackKind`, `SeDef`, `SeEnvDef`, `StageAnimDef`, `StgPre`,
`GenGeo` (function pointers, or a run-time object slot). The other 50
ELF-table record types (and `FDSSlot`, an element of `MotionDef`) are
frozen and pass on 64-bit already. `HintDef`
(`omori/include/lws_kyomi.h`, ELF `unmapped_002ADBA0`) has no offset
comments, so nothing asserts it.

### 2D-2F, per-directory sweeps

Pointer-cast warnings left on `linux-x64`, by directory and file (the
sweeps' work list; a header line counts in the TU that expands it):

| package | dir | warnings | largest |
| --- | --- | --- | --- |
| 2D | `fumi/src` | 278 | act-game.c 64, commonact.c 60, girl_act.c 42, act.c 41, enemy_act.c 31, fieldCollision.c 19 |
| 2D | `fumi/ios`, `fumi/sound`, `fumi/isys` | 42 | s_init.c 18, cdvd.c 12, adpcm_init.c 4 |
| 2E | `sugipon/src` | 283 | girl.c 44, box.c 39, rope.c 28, cage.c 26, enemyParts.c 25, motionManager2.c 23, item.c 18 |
| 2F | `omori/src` | 75 | camera-editor.c 46, brain.c 17, chain.c 10 |
| 2F | `script/src` | 15 | e3.c 8, stageSEProc.c 4 |
| 2F | `ito/src` | 32 | queen.c 24, act_bird.c 6 |
| none | `common/src` (beyond 2C's) | 20 | DObj.c 8, sceneManager.c 9 |
| none | `seki/src` (beyond 2C's) | 16 | FileManager.c 12, EnemyInit.c 4 |

`common/src` and `seki/src` outside 2C's files belong to no package yet.

Runtime hazards the asserts cannot see, for the sweep that owns the file:

- **Message queues** (2D, and `common/`): callers keep `int` rings and
  `int` receive variables while `IosMsgWord` is 8 bytes on 64-bit, so the
  ring overflows: `common/src/StageManager.c:379`, `common/src/main.c:276,279`,
  `fumi/ios/cdvd.c:204,205,213,415,646,838,839,1179,1193`,
  `fumi/ios/mcard.c:773`, `fumi/ios/pad.c:718,720`, `fumi/src/jimaku.c:322`
  and `port/platform/test/ios_chain_test.c:108,111` (gcc lists each as
  `-Wincompatible-pointer-types` on 64-bit only). Declare those rings and
  variables `IosMsgWord`.
- **Two views of one record.** `PObjGObj` spells `GObj` as words and byte
  pads (`pad34[8]` over two pointers, `pad64[248]` over the mail box), so
  on 64-bit its offsets stop matching `GObj`'s; `PObj` and `PObjModel`
  (R4) and `GsysObjInfo` / `GamesysObjInfo` likewise. Replace the pads
  with the other view's member types or drop the view.
- **`Act.flags18`**: a function pointer (`afterProc`) shares the doubleword
  whose high word holds state flags; an 8-byte pointer overwrites them.
- **`GOBJ_SUB` / `GOBJ_ACT` with an int argument**: 31 expansions pass an
  `int` object (counted under `common/include` before attribution:
  sceneManager.c, queen.c, motionManager.c, commonact.c).
- **Save image**: `backStageSave` writes `backStageGirlTargetEnemyGop`, a
  `GObj *`, as 4 bytes (`common/src/backStage.c`); `MakeCharacterPacket`
  builds `characterPacket` with raw offsets (`fumi/src/boyact.c`).
  `BoyKidnapWork` and `CharPos` (boyact.c) are save records defined in a
  `.c` file.
- **Hard-coded allocation sizes cast to a struct** (`(T *)iosMalloc*(p, N)`),
  each of which must become `sizeof(T)` (or be checked equal on 32-bit):
  `AcbMgr` 16, `AcbWork` 12 (`attackCheckBoundary.c:191,40`), `CageWork` 80
  (`cage.c:100`), `CandleFlame` 8 (`candle.c:34`), `ChainSet` 0x10 and
  `Cloth4D` 0x300 (`clothAnimation.c:885,1880`), `PointBlur` 64
  (`enemyParts.c:119`), `PuddleWork` 208 (`puddle.c:78`), `WormVec` 160,
  `WormWork` 16 (`worm.c:364,339`), `AmbientVolume` 160, `Light` 80
  (`Light.c:1073,179,203`), `Fan2D` 12, `Mesh3D` 144, `PrimParticle` 416
  (`Primitive.c`), `CamSetFile` 16 (`camera-ico2.c:975`). 97
  `iosMalloc*` calls pass a literal size and 30 pass `sizeof`.
