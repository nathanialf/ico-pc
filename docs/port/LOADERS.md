# Disc-format loaders on a 64-bit host

The game's loaders read a file into a heap block and then relocate it in
place: every offset the file holds is turned into an address by adding the
block's address, and several loaders store heap addresses into 4-byte
slots of the file's records. The game then reads those words back as
pointers. On a 64-bit host an address does not fit in 4 bytes. This page
says how each format is handled and what the rest of the port may rely on.
The census these conversions follow is `docs/research/loader-census.md`;
the layout classes are in `LAYOUT.md`; raw offsets into runtime records
(not files) are in `OFFSET_AUDIT.md`.

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

The host heap is one block, the simulated EE RAM (`port/platform/arena.h`,
PLATFORM.md "Heap"), and every allocation the game makes, the GObj table
included, lies in it. A word is therefore the address's offset in that
block, which needs no context to resolve, so each read converts locally.
A word is never 0 for a heap address (the EE heap starts at 0x760000), so
the game's `== 0` tests on words keep their meaning. This is one reason the
port keeps the arena rather than giving the game the host's `malloc`.

**(b) Host record decoded from the frozen one.** Where the file is freed as
soon as the loader returns and the game keeps a copy of a record, the copy
is a runtime record (natural host layout, real pointers) that the loader
fills field by field from the frozen file record.

Each format below says which pattern it uses. Every edit keeps the EE
build's tokens: either `#ifdef ICO_HOST` with the original in `#else`, or a
macro whose EE expansion is the original cast.

## Formats

### p2o family (p2o, p2c, p2g, plo, p2v) and p2s: pattern (b), with (a) inside the image

Files: `ico2/common/src/PObj.c`, `ico2/seki/include/DisplayP2O.h`,
`ico2/common/src/charFileManager.c` (`ReadModelFile`,
`ReadVolumeModelFile`, `ReadShadowModelFile`).

- The file records live in `DisplayP2O.h` with offset comments and are
  frozen: `ObjHdr` (0x18), `ObjEnt` (0x10, the polygon table entry) and
  `ObjRec` (0x180, the part record, with every address an `ICO_EEWORD`).
  The host's `AllocPObj` relocates the image exactly as the EE does, each
  relocated word an EE word.
- `PObjPart` (the model's part table, `PObjModel.parts`) is class
  `runtime`. The EE copies each 0x180-byte `ObjRec` into it whole; the host
  decodes it field by field (`DecodePObjPart`): addresses become pointers,
  everything else is copied, including the padding. The display code reads
  the part by name.
- The strip and morph tables are tables of words in the image, and the
  display walks them as pointer tables (`Packet.c`'s copy for deformable
  models, `RegistPacket.c`'s `reg_setShape`). The host decodes them into
  pointer tables in one `malloc`ed block per model (outside the arena, so
  the game's heap is untouched) and frees the block where the loader frees
  the image (`PObj_FreeImageTables`, called after each `iosFree` of a model
  image).
- The polygon table (`ObjEnt`, a 16-byte stride, the vertex-weight list
  address at 0x00 and the cluster number at 0x04) stays in the image as EE
  words: `Packet.c`'s `pac_getWeight` reads `ObjEnt.p` with `ICO_EEPTR`;
  the other offsets it forms by hand (`polys`, `mats`) are `ICO_WORD`. The
  deformable-model copy sizes its pointer tables with `sizeof(void *)`, and
  the packet builder's own addresses (`pacWork` tags and cursor,
  `pac_makeStrip`'s `pkt` and `dst`, `pac_moveToSeki`) are pointer-wide
  (`PacAddr`/`ICO_WORD`).
- `PObj.c` describes the model and its display object with its own views.
  On the host they are pinned to the shared definitions: `PObjPkt` is
  `Sub15C`, `PObjSub` is a union over `PObjPart`, and `PObj` mirrors
  `PObjModel` member for member, with `_Static_assert`s that every offset
  and the size agree. `PObjModel.box` is 16-byte aligned on the host (it is
  at 0x50 on the EE). The sizes `MakePacket` and `AllocPObj` wrote as
  numbers (0x880, 0x100, 80 a part, 0xD0) are `sizeof(Sub15C)`,
  `sizeof(LightMatrix)`, `sizeof(struct DObjNode)` and `sizeof(PObjModel)`
  on the host; a `_Static_assert` where pointers are 4 bytes checks they are
  the EE's numbers.
- `PObj.image` holds the image's EE word (only tested against 0 and
  printed).
- The loaders' `int name` parameter receives a `char *` from `cdvd.c`'s
  dispatch (`PackKind.func`); it is `ICO_WORD` (pointer-wide) on the host,
  as are `InitPObj`'s `h` and `name`, so the model's name survives a 64-bit
  call.

### Shadow models (`seki/src/Shadow.c`)

Pattern (a): `ShadowPoly.pts` and `ClusterPoly.run` are `ICO_EEWORD`
fields read with `ICO_EEPTR`; the 0x10-byte file entries keep their size,
with `_Static_assert`s; the strip table is sized `sizeof(ShadowRun *)`.

### cl (collision)

Pattern (a). Files: `ico2/common/src/charFileManager.c`
(`ReadCollisionFile`), `ico2/fumi/include/fieldCollision.h`,
`ico2/fumi/src/fieldCollision.c`.

- The image stays resident in its file layout: the `FcColl` head, the
  0x50-byte `FcWallEnt` walls, the 0x70-byte `FcFloorEnt` floors and two
  32x32 grids of block-list words. `FcColl` in `fieldCollision.h` is the
  one definition of the head; `charFileManager.c` and `fieldCollision.c`'s
  `FuzioCtx` and `FcWallSet` views use it on the host (the EE keeps its
  typed views).
- `ReadCollisionFile` relocates the same words as the EE (the head's `wcl
  fcl wblk fblk ofs` and every nonzero grid cell), each an EE word, and
  stores each wall's sine/cosine pair (a `mallocseki` block) at
  `FcWallEnt.normal` (0x4C) as an EE word.
- Readers go through `FUZIO_WALLS`, `FUZIO_FLOORS`, `FUZIO_WBLK`,
  `FUZIO_FBLK`, `FUZIO_OFS`, `FCWS_NWALL`, `FCWS_WALLS` and
  `FC_WALL_NORMAL`, which expand to the original field reads on the EE.
  `MapCollisionData` and `LoadCollision` store `ICO_EEW` words for the
  same readers.
- `Sub15C.colData` (pointer-wide on the host) stays a real pointer to the
  image; `CSVSYSTEM_ReadCharFiles` stores it without truncating, and so
  does every record that saves it (`BoxWork.colData`, DIVERGENCES.md D10).

### bga and cam (stage animation)

Pattern (a). Files: `ico2/seki/include/BgAnimation.h`,
`ico2/seki/src/BgAnimation.c`, `ico2/seki/src/StageAnimation.c`.

- The `.bga` image is loaded once and kept (`stageTable[id].data`). Its
  records live in `BgAnimation.h` with offset comments and are frozen:
  `BgaHeader`, `BgaEnvEnt`, `BgaDObjEnt`, `BgaKey`, `BgaMotion`,
  `BgaPtKey`, `BgaPtMotion`, `BgaExtKey`, `BgaExtMotion`.
- `bga_InitData` relocates in place; every relocated or game-stored word is
  an EE word: the header's `dobjs`, `roots` and `anim`; a node's `u` (the
  object it drives: a particle record, a `Sub15C`, a `GObj`, a light
  record), `env`, `child`, `sibling` and `motion`; an envelope's `data`; a
  motion's `key`. They are read with `ICO_EEPTR` and the header macros
  `BGA_ANIM`, `BGA_ROOT`, and written with `ICO_EEW` / `BGA_W`.
- The root list (`mallocseki((n + 1) * 4)`) stays an array of 4-byte words.
  `BgaAnim`, the 0x30-byte heap record, is a runtime record and keeps a
  real pointer.
- The one object a node may point at that is not in the arena is the static
  `bgaDummyLight` (light types 6 and 11 while paused): it has a reserved
  word, 0x10 (never a heap address), through `bga_objWord`/`bga_objPtr`.
- The env-type-6 key normalisation runs unchanged, the same float
  operations in the same order.
- `stage_SetParentOfGObj*` copied {GObj *, node} into `BgaAnim` as one
  8-byte block; the host copies the two fields.
- The `.cam` SDF camera (`BgaSdfCam`, `BgaSdfKey`) holds no addresses and is
  used as loaded.
- The two files' views of runtime records (`BgaGObj`, `BgaGeom`,
  `BgaAnimObj`, `BgaAnimGeom`, `BgaObj`, `StageAnimation.c`'s `STG_SUB` and
  raw `+0x15C` reads) read `GObj` and `Sub15C` by name on the host
  (`BGA_GOBJ_GEOM`, `BGA_GEOM_NAME`, `BGA_GEOM_MTX`, ...). The raw +0x30,
  +0x34 and +0x80 reads of light records in `bga_calcEnvelope` stay: `Light`
  and `AmbientVolume` are private to `Light.c`, and each offset lies before
  the record's first pointer, so it holds on every host.

### mob (motion)

Pattern (a). Files: `ico2/sugipon/src/motionFileManager.c`,
`ico2/sugipon/include/motionFileManager.h`,
`ico2/sugipon/src/motionManager2.c`, `motionOrientManager.c`.

- The image stays resident in `motionTable[]`. `MotFileHdr`, `FacialRec`
  and `NodeRec` live in `motionFileManager.h` with offset comments.
- `InitMotionFile` relocates, in the EE's order, header words 1-4, the
  facial table and its entries, every node-list slot, the word inside
  format 2/5 nodes and `nTable`/`lastTable` inside format 3/6 nodes, each
  an EE word (the host versions of `pursueNodeList`, `relocFacialTable` and
  `relocMotionFile`; the EE's in `#else`).
- Every reader of those words (`_getMotion` for all six formats,
  `getMotionRootPos`, `getShapeMotion`, `getShapeGeometry`,
  `CheckMotionIncludeFacialData`, `assertMotionNodeCount`) goes through
  `ICO_EEPTR`. Other files read only word 0, the frame count, which is not
  an address.

### gcm (camera sets)

Pattern (a). Files: `ico2/omori/src/camera-ico2.c`,
`ico2/omori/include/camera-editor.h`, `ico2/omori/src/camera-editor.c`.

- `ReadCameraSet` converts file versions 0-3 into one heap block: the
  16-byte `CamSetFile` head, `count` 76-byte `CamGroup` records, then the
  92-byte `PinRec` pins. Every reader walks the block with these literal
  strides, so the records are frozen; host `_Static_assert`s pin the three
  sizes.
- `CamGroup.items` (0x48), the only address, is an EE word, read and
  written through `CAMGROUP_ITEMS(g)` / `CAMGROUP_SET_ITEMS(g, p)`
  (`camera-editor.h`; plain field accesses on the EE).
- The debug camera editor (developer mode) keeps two copies of a set. On the
  PS2 they sit at the development kit's fixed addresses 0x3000000 and
  0x30E27E0 (a `CamMgr`, 100 groups of 76 bytes and 100 pin blocks of 9,200
  bytes each), outside the 32 MB the host simulates. On the host
  `cameraSetOrg` and `cameraSetEdit` are static `CamMgr` records, each with a
  static `CamGroup[100]` (`CAMSET_T`, `CS_COUNT`, `CS_ITEMS` read a set's two
  words; the EE keeps `int *` and the original `set[0]`, `set[1]`), and a
  box's pin block is a 9,200-byte block from `ios_partition_root`
  (`iosMallocDebugNoAssert`), taken when the box is added and freed when it
  is deleted or the set is re-initialised. When the root runs out,
  `CameraEdit_add_box` returns its existing "cannot add any more" result.
  The editor's `(int)mgr->items` arithmetic is `ICO_WORD`, and its menu
  thread record is 160 bytes on the host (an `IOSThread` is 152).

### svd (shock voice sets): pattern (b)

`ReadShockFile` puts a `ShockVoiceSet` (four real pointers into the image, a
runtime record) in front of the image. It allocates
`size + sizeof(ShockVoiceFile)` (16 on the EE, 32 on the host), so the
image starts after the host-sized header.

### hd and sq (sound headers)

`soundHDDataSet` and `soundSQDataSet` (`fumi/sound/s_init.c`) hand the
image's address to the sound library. On the PS2 `SgVabOpenFakeBody`
relocates header words 0x10 and 0x18-0x24 into the slots at 0x30 and
0x38-0x44, and `SgBgmOpen` keeps the sequence's address. The host sound
library (`port/audio/sg/sound.c`, AUDIO.md) keeps its own record of these
addresses (pattern (b)) and never writes an 8-byte address into the
header's 4-byte slots; the game side needs no change. The `.bd` buffer and
the ADPCM sources go to `soundBDDataSet`, `adpcmDataSet` and `Ee2Iop` as
pointer-wide words (`ICO_WORD_PTR(void *)`).

`.bd`, `.int`, `.pef`, `.ssb`, `.skb`, `.tm2` and `.zzz` hold no addresses
(loader-census.md section 3). `ReadStageSettingFile` traps if a file is
larger than `GlobalStageSetting` (never on this disc), and
`SetParticleEffectPackage` bounds the `.pef` copy to its 160-byte slot
(DIVERGENCES.md D5).

## Records that are not loader formats

### `PObjGObj` and `GObj`

`PObjGObj` (`typedef.h`) was a word view of the game object, with byte pads
standing for `GObj`'s pointers; on a 64-bit host its offsets do not match.
On the host it is `typedef struct GObj PObjGObj`, so the record has one
definition; the EE keeps the original view.

### `Act.flags18`

`Act.flags18` (0x18) is one doubleword: the actor's after-proc in its low
word, state flags in bits 32-63. An 8-byte function pointer there would
overwrite the flags. On the host the union holds only the flags and the
after-proc is `afterProcHost`, appended at the end of `Act` so no member
moves. Every store, test and call of the after-proc goes through
`ACT_AFTER_PROC(a)` (an lvalue, next to `Act` in `typedef.h`); the raw
`+ 0x18` forms stay in the EE's `#else` branches. No code writes the whole
doubleword to set or clear the after-proc (every `ll` assignment masks or
ORs the old value), so the split is not observable. The flag bits are read
through `flags18.ll` / `flags20.ll` on the host.

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
0x810230: the stage partition at 0x8101A0 (PLATFORM.md, "Partitions") plus
its 0x50-byte record and the block's 0x40-byte header.

The host therefore writes `0x810230 + index * 0x174` (0 for none), the
index being the entry's position in the host's table, and on load maps a
word back to the entry with that index. A word that names no entry is read
back as the PS2 would, as an address (`ICO_EEPTR`). The host finds its table
as the stage partition's first block and checks the block's allocation line
in gobj.c; if that ever stops holding it prints a warning once, since the
saved word would then no longer match the PS2's. The saved bytes equal the
PS2's for the same state, so there is no DIVERGENCES.md entry.

## Classification

The file records above are `overlay` in `config/struct_classes.txt` and
asserted on every preset by `port/test/layout_asserts.c` (LAYOUT.md):
`ObjHdr`, `ObjEnt`, `ObjRec`; `BgaHeader`, `BgaEnvEnt`, `BgaDObjEnt`,
`BgaKey`, `BgaMotion`, `BgaPtKey`, `BgaPtMotion`, `BgaExtKey`,
`BgaExtMotion`, `BgaSdfKey`, `BgaSdfCam`; `FcColl`, `FcWallEnt`;
`CamSetFile`, `CamGroup`; `MotFileHdr`, `FacialRec`, `NodeRec`;
`Tim2Picture`, `Tim2Mipmap`. `PObjPart` is `runtime`. `PackEnt`
(`fumi/ios/cdvd.c`) is a disc record still defined in a `.c` file, so the
asserts do not see it.

## Allocation sizes

| site | EE | host |
| --- | --- | --- |
| `PObj.c` `MakePacket` | `mallocseki(0x880)`, `(0x100)`, `partCount * 80` | `sizeof(Sub15C)`, `sizeof(LightMatrix)`, `partCount * sizeof(struct DObjNode)` |
| `PObj.c` `AllocPObj` | `mallocseki(0xD0)` | `sizeof(PObjModel)` |
| `charFileManager.c` `ReadShockFile` | `size + 16` | `size + sizeof(ShockVoiceFile)` |
| `charFileManager.c` `ReadStageSettingFile` | unbounded `memcpy` into `GlobalStageSetting` | traps if `size > sizeof(GlobalStageSetting)` |
| `BgAnimation.c` lightning record | `352` | `max(sizeof, 352)` (the EE record is 0x158) |
| `BgAnimation.c` root list | `(n + 1) * 4` | unchanged: an array of 4-byte words |
| `BgAnimation.c` particle record | `48` | unchanged: `BgaParticleEnt` has no pointers |
| `camera-ico2.c` camera set | `16 + count * 76 + total * 92` | unchanged, with `_Static_assert`s on the three record sizes |

Open items are in `docs/TODO.md` (Game code).
