# Disc-format loaders on a 64-bit host

Package 2C. The game's loaders read a file into a heap block and then
relocate it in place: every offset the file holds is turned into an address
by adding the block's address, and several loaders store heap addresses into
4-byte slots of the file's records. The game then reads those words back as
pointers. On a 64-bit host an address does not fit in 4 bytes. This page
says how each format was converted, what the rest of the port may rely on,
and what is left for later packages. The census these conversions follow is
docs/research/loader-census.md (R4); the layout classes are
docs/port/LAYOUT.md.

## The two patterns

**(a) Frozen record, EE address words.** The file's records keep the EE
layout (class `overlay`, asserted on every preset). A field that holds an
address is declared `ICO_EEWORD(T)`: `T` on the EE, a 32-bit word on the
host. The loader's arithmetic is unchanged except for the conversions
(`ico2/common/include/eeword.h`):

| macro | EE | host |
| --- | --- | --- |
| `ICO_EEWORD(T)` | `T` | `unsigned int` |
| `ICO_EEW(p)` | `(int)(p)` | `p`'s offset in the EE RAM arena; 0 for null; traps for a pointer outside the arena |
| `ICO_EEPTR(T, w)` | `(T)(w)` | `(T)(arena base + w)`; null for 0 |

The host heap is one block, the simulated EE RAM (`port/platform/arena.h`),
and every allocation the game makes, the GObj table included, lies in it. A
word is therefore the address's offset in that block: on a 32-bit host this
is exactly the address the PS2 held (the allocator reproduces the EE's
addresses there), and on a 64-bit host it is the address the host allocator
gave the block in the simulated RAM. The readers need no context to resolve
a word, so the conversion is local to each read. The same code runs on the
32-bit and 64-bit hosts, which keeps `win-x86-ref` a meaningful oracle for
`win-x64`. A word is never 0 for a heap address (the EE heap starts at
0x760000), so the game's `== 0` tests on words keep their meaning.

**(b) Host record decoded from the frozen one.** Where the file is freed as
soon as the loader returns and the game keeps a copy of a record, the copy is
a runtime record (natural host layout, real pointers) that the loader fills
field by field from the frozen file record.

Each format below says which pattern it uses. Every edit keeps the EE build
byte-identical: either `#ifdef ICO_HOST` with the original in `#else`, or a
macro whose EE expansion is the original cast.

## Formats

### p2o family (p2o, p2c, p2g, plo, p2v) and p2s: pattern (b), with (a) inside the image

Files: `ico2/common/src/PObj.c`, `ico2/seki/include/DisplayP2O.h`,
`ico2/common/src/charFileManager.c` (`ReadModelFile`,
`ReadVolumeModelFile`, `ReadShadowModelFile`).

- The file records moved from `PObj.c` into `DisplayP2O.h` with offset
  comments and are frozen: `ObjHdr` (0x18), `ObjEnt` (0x10, the polygon
  table entry) and `ObjRec` (0x180, the part record, with every address an
  `ICO_EEWORD`). The host's `AllocPObj` relocates the image exactly as the
  EE does, each relocated word an EE word, so the image's bytes after
  loading are the PS2's on a 32-bit host.
- `PObjPart` (the model's part table, `PObjModel.parts`) is now class
  `runtime`. The EE copies each 0x180-byte `ObjRec` into it whole; the host
  decodes it field by field (`DecodePObjPart`): addresses become pointers,
  everything else is copied, including the padding. The display code reads
  the part by name and needs no change.
- The strip and morph tables are tables of words in the image, and the
  display walks them as pointer tables (`Packet.c`'s copy for deformable
  models, `RegistPacket.c`'s `reg_setShape`). The host decodes them into
  pointer tables in one `malloc`ed block per model (outside the arena, so
  the game's heap is untouched) and frees the block where the loader frees
  the image (`PObj_FreeImageTables`, called after each `iosFree` of a model
  image). The EE's tables live in the freed image, so a read after the load
  reads freed memory on both; whether any such read exists is R4's open
  question 1, which an ASan run of the host build answers.
- The polygon table (`ObjEnt`, a 16-byte stride, the vertex-weight list
  address at 0x00 and the cluster number at 0x04) stays in the image as EE
  words: `Packet.c`'s `pac_getWeight` reads `ObjEnt.p` with `ICO_EEPTR`
  (done, package 2G).
- `PObj.c` describes the model and its display object with its own views.
  On the host they are pinned to the shared definitions: `PObjPkt` is
  `Sub15C`, `PObjSub` is a union over `PObjPart`, and `PObj` mirrors
  `PObjModel` member for member, with `_Static_assert`s that every offset
  and the size agree on every host. `PObjModel.box` is 16-byte aligned on
  the host (it is at 0x50 on the EE). The sizes `MakePacket` and `AllocPObj`
  wrote as numbers (0x880, 0x100, 80 a part, 0xD0) are `sizeof(Sub15C)`,
  `sizeof(LightMatrix)`, `sizeof(struct DObjNode)` and `sizeof(PObjModel)`
  on the host, with a 32-bit `_Static_assert` that they are the EE's.
- `PObj.image` holds the image's EE word (only tested against 0 and
  printed).
- The loaders' `int name` parameter receives a `char *` from `cdvd.c`'s
  dispatch (`PackKind.func`); it is `ICO_WORD` (pointer-wide) on the host,
  as are `InitPObj`'s `h` and `name`, so the model's name survives a
  64-bit call.

### cl (collision)

Pattern (a). Files: `ico2/common/src/charFileManager.c`
(`ReadCollisionFile`), `ico2/fumi/include/fieldCollision.h`,
`ico2/fumi/src/fieldCollision.c`.

- The image stays resident in its file layout: the `FcColl` head, the
  0x50-byte `FcWallEnt` walls, the 0x70-byte `FcFloorEnt` floors and two
  32x32 grids of block-list words. `FcColl` is new in `fieldCollision.h`
  and is the one definition of the head: `charFileManager.c`'s `Coll` is
  `FcColl`, and its `Bone` was `FcWallEnt` under another name and is gone;
  on the host `fieldCollision.c`'s `FuzioCtx` and `FcWallSet` views are
  `FcColl` too (the EE keeps its typed views).
- `ReadCollisionFile` relocates the same words as the EE (the head's `wcl
  fcl wblk fblk ofs` and every nonzero grid cell), each an EE word, and
  stores each wall's sine/cosine pair (a `mallocseki` block) at
  `FcWallEnt.normal` (0x4C) as an EE word.
- Readers go through `FUZIO_WALLS`, `FUZIO_FLOORS`, `FUZIO_WBLK`,
  `FUZIO_FBLK`, `FUZIO_OFS`, `FCWS_NWALL`, `FCWS_WALLS` and
  `FC_WALL_NORMAL`, which expand to the original field reads on the EE.
- `Sub15C.colData` (pointer-wide on the host, 2B) stays a real pointer to
  the image; `CSVSYSTEM_ReadCharFiles` stores it without truncating.
  `clothAnimation.c:463` reads the wall count at +8, which is unchanged.

### bga and cam (stage animation)

Pattern (a). Files: `ico2/seki/include/BgAnimation.h`,
`ico2/seki/src/BgAnimation.c`, `ico2/seki/src/StageAnimation.c`.

- The `.bga` image is loaded once and kept (`stageTable[id].data`). Its
  records moved from `BgAnimation.c` into `BgAnimation.h` with offset
  comments and are frozen: `BgaHeader`, `BgaEnvEnt`, `BgaDObjEnt`,
  `BgaKey`, `BgaMotion`, `BgaPtKey`, `BgaPtMotion`, `BgaExtKey`,
  `BgaExtMotion`.
- `bga_InitData` still relocates in place; every relocated or game-stored
  word is an EE word: the header's `dobjs`, `roots` and `anim`; a node's
  `u` (the object it drives: a particle record, a `Sub15C`, a `GObj`, a
  light record), `env`, `child`, `sibling` and `motion`; an envelope's
  `data`; a motion's `key`. They are read with `ICO_EEPTR` and the header
  macros `BGA_ANIM`, `BGA_ROOT`, and written with `ICO_EEW` / `BGA_W`.
- The root list (`mallocseki((n + 1) * 4)`) stays an array of 4-byte words,
  so its size is right on every host. `BgaAnim`, the 0x30-byte heap record,
  is a runtime record and keeps a real pointer.
- The one object a node may point at that is not in the arena is the static
  `bgaDummyLight` (light types 6 and 11 while paused): it has a reserved
  word, 0x10 (never a heap address), through `bga_objWord`/`bga_objPtr`.
- The env-type-6 key normalisation runs unchanged, the same float
  operations in the same order (R4).
- `stage_SetParentOfGObj*` copied {GObj *, node} into `BgaAnim` as one
  8-byte block; the host copies the two fields.
- The `.cam` SDF camera (`BgaSdfCam`, `BgaSdfKey`, now in the header) holds
  no addresses and is used as loaded.
- The two files' views of runtime records (`BgaGObj`, `BgaGeom`,
  `BgaAnimObj`, `BgaAnimGeom`, `BgaObj`, `StageAnimation.c`'s `STG_SUB` and
  raw `+0x15C` reads) read `GObj` and `Sub15C` by name on the host
  (`BGA_GOBJ_GEOM`, `BGA_GEOM_NAME`, `BGA_GEOM_MTX`, ...). `stage_SetLoopFlag`
  and the `stageTable` row index use `StageAnimDef`'s names and size. The raw
  +0x30, +0x34 and +0x80 reads of light records in `bga_calcEnvelope` stay:
  `Light` and `AmbientVolume` are private to `Light.c`, and each offset lies
  before the record's first pointer, so it holds on every host.

### mob (motion)

Pattern (a). Files: `ico2/sugipon/src/motionFileManager.c`,
`ico2/sugipon/include/motionFileManager.h`,
`ico2/sugipon/src/motionManager2.c`.

- The image stays resident in `motionTable[]`. `MotFileHdr`, `FacialRec`
  and `NodeRec` moved into `motionFileManager.h` with offset comments.
- `InitMotionFile` relocates, in the EE's order, header words 1-4, the
  facial table and its entries, every node-list slot, the word inside
  format 2/5 nodes and `nTable`/`lastTable` inside format 3/6 nodes, each
  an EE word (the host versions of `pursueNodeList`, `relocFacialTable`
  and `relocMotionFile`; the EE's in `#else`).
- `_getMotion` (all six formats), `getMotionRootPos` and `getShapeMotion`
  read the words with `ICO_EEPTR`. Other files read only word 0, the frame
  count, which is not an address.
- `CheckMotionIncludeFacialData` compares the header's own word plus 16
  with the relocated `typeList` word, the EE's test in EE terms. Its only
  caller is `relocMotionFile` (R4 open question 3: settled).

### gcm (camera sets)

Pattern (a). Files: `ico2/omori/src/camera-ico2.c`,
`ico2/omori/include/camera-editor.h`, `ico2/omori/src/camera-editor.c`.

- `ReadCameraSet` converts file versions 0-3 into one heap block: the
  16-byte `CamSetFile` head (moved into `camera-editor.h`), `count` 76-byte
  `CamGroup` records, then the 92-byte `PinRec` pins. Every reader walks
  the block with these literal strides, so the records are frozen.
- `CamGroup.items` (0x48), the only address, is an EE word, read and
  written through `CAMGROUP_ITEMS(g)` / `CAMGROUP_SET_ITEMS(g, p)`
  (`camera-editor.h`; plain field accesses on the EE).
- The allocation `16 + count * 76 + total * 92` keeps its numbers; host
  `_Static_assert`s pin `sizeof(CamSetFile)`, `sizeof(CamGroup)` and
  `sizeof(PinRec)` to them.
- The debug camera editor keeps its two copies of the set at fixed dev-kit
  addresses (0x3000000 and 0x30E27E0), outside the 32 MB arena; it cannot
  run on the host until developer mode (Phase 6) gives it heap memory. Its
  `items` accesses are converted all the same.

### svd (shock voice sets): pattern (b), already

`ReadShockFile` puts a `ShockVoiceSet` (four real pointers into the
image, a runtime record) in front of the image and allocated `size + 16`,
16 being the header's EE size. It now allocates
`size + sizeof(ShockVoiceFile)` (16 on the EE and 32-bit hosts, 32 on
64-bit hosts), so the image starts after the host-sized header. The EE code
is unchanged.

### hd and sq (sound headers): left to Phase 4

`soundHDDataSet` and `soundSQDataSet` (`fumi/sound/s_init.c`) keep the
image's address and hand it to the sound library: `SgVabOpenFakeBody`
relocates header words 0x10 and 0x18-0x24 into the slots at 0x30 and
0x38-0x44, and `SgBgmOpen` keeps the sequence's address
(`sce/libsndn2/sound.c`, R4). Neither function is in the host build: the
host links `port/null/snd_null.c`'s stubs, which touch nothing. The
Phase 4 host sound library (`port/audio/sg`) must keep its own record of
these addresses (pattern (b)) and never write an 8-byte address into the
header's 4-byte slots; the game side needs no change. `.bd`, `.int`,
`.pef`, `.ssb`, `.skb`, `.tm2` and `.zzz` hold no addresses (R4 section 3).

## Records that are not loader formats

### `PObjGObj` and `GObj`

`PObjGObj` (`typedef.h`) was a word view of the game object, with byte pads
standing for `GObj`'s pointers; on a 64-bit host its offsets stop matching.
No game code uses it. On the host it is now `typedef struct GObj
PObjGObj`, so the record has one definition; the EE keeps the original
view. Its layout asserts are gone with it (it has no members of its own on
the host).

### `Act.flags18`

`Act.flags18` (0x18) is one doubleword: the actor's after-proc in its low
word, state flags in bits 32-63. An 8-byte function pointer there would
overwrite the flags. The EE and 32-bit-host layout is unchanged (the union
stays, the 32-bit layout asserts hold). On hosts with 8-byte pointers the
union holds only the flags and the after-proc is `afterProcHost`, appended
at the end of `Act` so no member moves. Every store, test and call of the
after-proc goes through `ACT_AFTER_PROC(a)` (an lvalue, next to `Act` in
`typedef.h`): `commonact.c` (the named stores, and the raw stores of
`actAfterForceRope`, `actAfterDown`, `actAfterRopeJump`), `boyact.c`
(two), `girl_act.c` (two), `act.c` (the clear in `actInitialize` and the
test, call and clear in the actor loop); the raw `+ 0x18` forms stay in the
EE's `#else` branches. No code writes the whole doubleword to set or clear
the after-proc (every `ll` assignment masks or ORs the old value), so the
split is not observable.

### The back-stage save word

`backStageSave` (`common/src/backStage.c`) writes
`backStageGirlTargetEnemyGop`, a `GObj *`, into the save image as 4 bytes,
and `backStageLoad` reads it back as the pointer. The save format is
frozen, and the host must write the PS2's bytes. On the PS2 the word is the
EE address of the carrier's entry in the GObj table (`fumi/isys/gobj.c`,
0x174 bytes an entry). That table is the first block allocated in the stage
partition after each stage's reset (`StageManager.c`: `stop_free_resources`
resets the partition, then `stage_initialize` calls `isysInitialize`, whose
first allocation is `isysGObjAlloc`'s table), so its PS2 address is always
0x810230: the stage partition at 0x8101A0 (`port/platform/test/memory_test.c`
checks the partitions' EE addresses) plus its 0x50-byte record and the
block's 0x40-byte header. Running `memory.c` on the 32-bit host with
iosInitialize's partitions, a reset and the 320-entry allocation gives
0x810230; on the 64-bit host the same sequence puts the table at 0x810050.

The host therefore writes `0x810230 + index * 0x174` (0 for none), the index
being the entry's position in the host's table, and on load maps a word
back to the entry with that index. A word that names no entry is read back
as the PS2 would, as an address (`ICO_EEPTR`). The host finds its table as
the stage partition's first block and checks the block's allocation line
(gobj.c's 174); if a future change makes that false it prints a warning
once, since the saved word would then no longer match the PS2's. The saved
bytes equal the PS2's for the same state, so there is no
docs/port/DIVERGENCES.md entry.

## Classification changes (`config/struct_classes.txt`)

- Converted, `pending=2C` removed: `BgaHeader`, `CamGroup`, `FcWallEnt`
  (`overlay`, now asserted on 64-bit too); `PObjPart` moved to `runtime`.
- New `overlay` records (moved into headers): `ObjHdr` (`size=0x18`),
  `ObjEnt`, `ObjRec` (`DisplayP2O.h`); `BgaEnvEnt`, `BgaDObjEnt`,
  `BgaKey`, `BgaMotion`, `BgaPtKey`, `BgaPtMotion`, `BgaExtKey`,
  `BgaExtMotion`, `BgaSdfKey`, `BgaSdfCam` (`nosize`, its key array runs
  on) (`BgAnimation.h`); `FcColl` (`fieldCollision.h`); `CamSetFile`
  (`size=0x10`, `camera-editor.h`); `MotFileHdr` (`nosize`: the comment's
  "16-byte header" is the header without the facial word at 0x10),
  `FacialRec`, `NodeRec` (`motionFileManager.h`).
- `PObjGObj` removed (on the host it has no members of its own).
- `port/test/layout_asserts.c` regenerated: 205 structs, 2,156 offset and
  114 size asserts (runtime 109 / 1,480 / 36, overlay 91 / 654 / 76, save
  5 / 22 / 2). No 2C struct is pending; only the `pending=5` ELF-table
  records remain.
- Records still defined in `.c` files and so outside the asserts: `PackEnt`
  (`fumi/ios/cdvd.c`), `Tim2Picture` and `Tim2Mipmap`
  (`seki/src/Texture.c`), outside this package's files.

## Hand-written sizes

| site | was | now (host) |
| --- | --- | --- |
| `PObj.c` `MakePacket` | `mallocseki(0x880)`, `(0x100)`, `partCount * 80` | `sizeof(Sub15C)`, `sizeof(LightMatrix)`, `partCount * sizeof(struct DObjNode)` |
| `PObj.c` `AllocPObj` | `mallocseki(0xD0)` | `sizeof(PObjModel)` |
| `charFileManager.c` `ReadShockFile` | `size + 16` | `size + sizeof(ShockVoiceFile)` (every host) |
| `charFileManager.c` `ReadStageSettingFile` | unbounded `memcpy` into `GlobalStageSetting` | traps if `size > sizeof(GlobalStageSetting)` (never on this disc, R4 s.2) |
| `BgAnimation.c` lightning record | `352` | `max(sizeof, 352)`, 352 on 32-bit hosts (the EE record is 0x158) |
| `BgAnimation.c` root list | `(n + 1) * 4` | unchanged: an array of 4-byte words |
| `BgAnimation.c` particle record | `48` | unchanged: `BgaParticleEnt` has no pointers, 40 bytes on every host |
| `camera-ico2.c` camera set | `16 + count * 76 + total * 92` | unchanged, with host `_Static_assert`s on the three record sizes |

A 32-bit host `_Static_assert` in `PObj.c` checks that the `sizeof`s equal
the EE's numbers.

## Verification

- EE: all 223 game sources compiled with `tools/compile_c.sh` (ee-gcc 2.9)
  from the tree before this package and after it have identical `.text`,
  `.data`, `.rodata`, `.sdata`, `.sbss`, `.bss`, `.lit4`, `.lit8` and
  `.rdata` bytes, relocations and symbol tables.
- Host: `ref-m32`, `linux-x64`, `win-x86-ref` and `win-x64` compile every
  game source and `layout_asserts` (all four hold every generated assert);
  ctest passes on `ref-m32` and `linux-x64`, `layout_asserts_fresh`
  included.
- `-Wint-to-pointer-cast` plus `-Wpointer-to-int-cast` on `linux-x64`, per
  file, before and after: `PObj.c` 31 to 0, `charFileManager.c` 21 to 0,
  `BgAnimation.c` 29 to 0, `StageAnimation.c` 52 to 0,
  `motionFileManager.c` 7 to 0, `motionManager2.c` 23 to 3,
  `fieldCollision.c` 19 to 18, `commonact.c` 59 to 56.
- A scratch test (not committed: it needs stubs for the seki allocator)
  built `PObj.c` with a synthetic two-part `.p2o` image in the arena and
  checked the relocated image words, every decoded `PObjPart` field, the
  strip and morph tables and the bounding boxes. It passes on the 32-bit
  host and, under ASan and UBSan, on the 64-bit host.

## Open items

For 2D (`fumi`):
- `Act`'s flag bits are still read through raw `+0x18` views
  (`ActStatus`/`ActStatusWord` casts): `enemy_act.c` 573, 575, 578, 580,
  1487, 2744, 2745, 2760, 2761; `act.c` 289-300; `commonact.c` 2288,
  2316, 2367, 4664, 4665; `girl_act.c` 1295, 1307, 1380, 1531, 2369. Wrong
  on 64-bit, where `Act` is runtime layout and `flags18` is not at 0x18.
- `actInitialize` allocates the `Act` with a literal 0x850; on 64-bit it
  must be at least `sizeof(Act)` plus the raw extension words used past it
  (0x68C, 0x690).
- `fieldCollision.c`: the raw `pad0[348]` views `FcWallSub`/`FcWallObj`
  (about 1373-1386, debug draw), the filter-function casts (1224, 1230,
  1256), `ClipPlane(int work)` (1319-1328), `(int)GOBJ_SUB` (1668-1674).
- `enemy_act.c:1116` passes `(int)target` to `stage_SetParentOfGObj`.
- `soundBDDataSet` and `adpcmDataSet` (`fumi/sound`) take the buffer as an
  `int` (the `-Wint-conversion` warnings at `charFileManager.c`'s calls).
- `gobj.c:263` forms the entry address with `(int)gobjTable`.

For 2E (`sugipon`):
- `motionManager2.c:2072` `root.fixObj = (int)obj` truncates;
  1932-1933 (`.smb` offset arithmetic) are correct in value.
- `particleEffect.c` `SetParticleEffectPackage`: bound the `.pef` copy to
  its 160-byte slot (R4 s.2).

For 2F (`omori`):
- `camera-editor.c`: raw `(int)mgr->items` arithmetic (1279, 1285, 1297,
  1325) and the editor's sets at fixed dev-kit addresses outside the arena
  (Phase 6 developer mode must move them onto the heap; `ICO_EEW` traps on
  them).

For the renderer (Phase 3), package 2G:
- [x] `Packet.c`'s `pac_getWeight` reads `ObjEnt.p` (the polygon table's
  vertex-weight list) with `ICO_EEPTR`; the other offsets it forms by hand
  (`polys`, `mats`) use `ICO_WORD`.
- [x] `Packet.c`'s deformable-model copy sizes its pointer tables as
  `stripCount * sizeof(void *)` and `morphCount * sizeof(void *)`; the
  decoded tables 2C builds hold host pointers. The packet builder's own
  addresses (`pacWork` tags and cursor, `pac_makeStrip`'s `pkt` and `dst`,
  `pac_moveToSeki`) are `PacAddr`/`ICO_WORD`, pointer-wide on the host.
- [x] `Shadow.c`: `ShadowPoly.pts` and `ClusterPoly.run` are `ICO_EEWORD`
  fields read with `ICO_EEPTR` (the 0x10-byte file entries keep their size,
  with `_Static_assert`s); the strip table is sized `sizeof(ShadowRun *)`.
- [x] `Light.c`: `lastLight` and `flatLightSlot[]` are `ICO_WORD`.
- [x] Other seki casts: `Basic.c` `reallocseki`/`freeseki` (host
  signatures), `EnemyInit.c` (the kind records are read as the typed
  array on the host), `FileManager.c` and `RegistPacket.c` (`ICO_WORD`).
  Left, in renderer wave 2's files or not pointer-to-int: `DisplayList.c`,
  `DmaPacket.c`, `Texture.c`, `GsBase.c`, `MicroCode.c` (`MicroCodeAddress`
  and `dl_OpenDma` take a word; `MicroCodeAddress` is zeros on the host),
  `DisplayP2O.c:71` (`sceDmaSend` with that word), the `const` discards into
  `dl_OpenDma` in `RegistPacket.c`, and `BgAnimation.c:1868,1882`
  (`BgaAnim.obj` is a `BgaAnimObj *` passed as a `GObj *`; same pointer).
- R4's open question 1 (does anything read a part's image pointers after
  the image is freed) is for an ASan run of a host boot; the decoded strip
  and morph tables are freed at the same point as the image.

For Phase 4: the host sound library keeps its own record of the `.hd` and
`.sq` addresses (above).

For Phase 5 (runtime table loader): `StageAnimDef.data` and
`LtProperty.texData` are run-time pointer slots in tables the loader will
read byte for byte; on the host they can hold EE words (`eeword.h`) or move
to a parallel host array. `eeword.h` covers heap addresses only; the
function-pointer fields need the address-to-function map R4 describes.

General:
- Every word read on the host calls `ico_arena_base()`; Phase 7's hot-path
  cleanup can cache the base.
- Not converted here, outside this package's files: `PackEnt`
  (`cdvd.c`), `Tim2Picture` and `Tim2Mipmap` (`Texture.c`), which are disc
  records defined in `.c` files (LAYOUT.md's hand-off list).
- The back-stage save word rests on the GObj table being the stage
  partition's first block; the host warns once if that stops holding.
