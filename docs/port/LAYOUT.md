# Struct layouts on the host and the EE

The game's records were recovered from the ROM's load and store offsets, and
the headers carry those offsets as `/* 0xNN */` comments. On the EE every
pointer and `int` is 4 bytes; on the 64-bit host a pointer is 8, so every
record that holds a pointer is laid out differently. This page describes
which records must keep the EE layout on the host, how that is checked,
and the pointer-width types the headers use. How code that reaches into a
record by EE offset is kept correct is in `OFFSET_AUDIT.md`; the file
formats are in `LOADERS.md`.

## Classes (`config/struct_classes.txt`)

Every record the asserts touch has a line in `config/struct_classes.txt`:

`<name> <class> [size=] [base=] [pending=] [nosize]  # evidence`

| class | meaning | asserted on the host |
| --- | --- | --- |
| `runtime` | natural host layout, real pointers; the game only ever reaches it through C | no; its asserts document the EE layout and compile with `-DICO_LAYOUT_EE=1` (a build with the EE's ILP32 layout) |
| `overlay` | read directly from disc or ELF bytes, so frozen; a pointer field becomes a 32-bit word (`ICO_EEWORD`) or is resolved by its loader | yes |
| `save` | serialised into the save image or the card files, so frozen | yes |

`pending=<name>` marks a frozen record that still has pointer fields: its
host asserts compile only with `-DICO_LAYOUT_PENDING=1`. No record is
pending today. `size=` gives a size the comments do not, `base=` says the
comments are offsets within an enclosing record (`JimakuSub` sits at 0xC of
`JimakuArg`), and `nosize` says a comment's "N-byte" describes a member or
element rather than the record (`LightningNode`, `ViBuf`,
`PrimParticleObj`, `BgaSdfCam`, `MotFileHdr`). There are about 125 runtime,
83 overlay and 6 save records.

The ELF data tables' record types are not frozen on the host: the table
loader writes each table in its own type's host layout (`DATA.md`, "The
data tables"), so records such as `SeDef`, `GenGeo` or `ObjKindEnt` hold
8-byte function pointers on the host and are `runtime`.

## Layout asserts

`tools/gen_layout_asserts.py` reads every header under `ico2/` (not
`ico2/vusrc/`; `sugipon/include/girlForceField.h` is skipped because it does
not compile on its own and holds no commented struct), and writes
`port/test/layout_asserts.c`:

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
- Every struct the asserts touch must have a line in the classification
  file; the generator stops otherwise.

The file holds about 2,240 offset and 120 size asserts. The test target
`layout_asserts` (`port/test/CMakeLists.txt`) compiles it as the game
compiles: the game's include path, `ICO_HOST`, `ICO_SEMANTIC_OPTIONS` and
`ICO_GAME_LAYOUT_OPTIONS` (`-mno-ms-bitfields` on Windows, so bit-fields
pack as on the EE). A moved field in a frozen record fails the build. ctest
also runs `layout_asserts_fresh`, which fails when the committed C does not
match the headers and the classification. After changing either:

```sh
tools/gen_layout_asserts.py          # rewrite port/test/layout_asserts.c
tools/gen_layout_asserts.py --check  # what layout_asserts_fresh runs
tools/gen_layout_asserts.py --list   # every struct and the checks found
```

Records defined in a `.c` file are outside the generator's view; the ones
that matter are asserted in place (`_Static_assert` next to the
definition), and `PackEnt` (`fumi/ios/cdvd.c`, a disc record of 4-byte
words) is the one disc record that is not.

## Pointer-width types (`common/include/typedef.h`)

Where the original code holds an address in an `int` field, the port
declares the field with a macro that names the intent:

| macro | was | is |
| --- | --- | --- |
| `ICO_WORD` | `int` | `__INTPTR_TYPE__`: a pointer-wide integer, so integer arithmetic on the field (byte offsets, masks) means the same |
| `ICO_WORD_PTR(T)` | `int` | `T`: for a word only ever converted to a pointer |

Examples: `GObj.act` (0x164) and `Act.work` (0x688) are
`ICO_WORD_PTR(void *)`, read through `GOBJ_ACT` and `GOBJ_WORK`
(`act-game.h`); `Sub15C.nodeMtx`, `nodeQuat` and `colData`, `MotRoot.fixObj`,
the `.i` members of `ActEnv.cageObj`, `ActEnv.sofaObj`, `Act.heldItem` and
`Act.nextItem` are `ICO_WORD`. `GOBJ_SUB(o)` reads `GObj.dobj` (already a
`Sub15C *`) directly on the host.

## The ios layer

- `fumi/include/message.h`: a message is `IosMsgWord`, `int` on the EE and
  `__INTPTR_TYPE__` on the host; every ring (`IosMsgQueue.buf`), every
  receive variable, `iosMsgSend`'s value, `iosMsgRecv`'s destination and
  `iosMsgSetEvent`'s value use it. `thread.c` sends and receives the dying
  thread's pointer through it and sizes the join queue as
  `sizeof(IosMsgQueue) + 8 * sizeof(IosMsgWord)`. `message.c` allocates the
  event thread as 16,576 bytes or `sizeof(MsgEventThread)`, whichever is
  larger.
- `fumi/include/memory.h`, `fumi/ios/memory.c`: addresses are `IosMemAddr`
  (`unsigned int` on the EE, `__UINTPTR_TYPE__` on the host), and the
  record sizes are named: the block header `NODE_SIZE` (0x40), the
  partition record `PART_SIZE` (0x50) and what they imply (`PART_NEED` 144,
  `PART_MIN` 160, `NODE_QW` 4). On the host they come from the records;
  `IosMemNode` is 16-byte aligned there in place of its `pad3C`. A
  `_Static_assert` pins the EE values wherever pointers are 4 bytes. On the
  host the block header is 0x50 bytes and the partition record 0x70, so
  every partition and block lands at a different offset than on the EE and
  each allocation costs 16 bytes more (PLATFORM.md, "Heap").
  `iosReallocDebug`'s `node->size - 0x40 < n` is kept as written (it
  compares quadwords with 64 on the EE too, so a block under 65 quadwords
  cannot be shrunk).
- `ios.c` passes the arena addresses to `iosMallocInitPartition` without
  truncation.

## Header comments to know about

- `McFileInfo` (`common/include/typedef.h`): the comment in front of it
  describes `McProductFile` (the 0x1F0-byte product file), which has no
  comment of its own. The comment sits on the wrong struct; a fix belongs in
  the decompilation (comment only).
- `PrimParticle`: `prim_InitParticle` allocates 416 bytes on the EE, the
  0x198-byte record rounded to a quadword; not a size mismatch.
- `GirlWork`'s and `BoxWork`'s offset comments are EE offsets; their
  `ICO_WORD` members move the host offsets after them (neither is frozen).
