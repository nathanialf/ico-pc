# Loader census

A research note written before the 64-bit conversion, kept as the survey
behind docs/port/LOADERS.md, docs/port/LAYOUT.md and the runtime table
loader (docs/port/DATA.md); it is not updated as the code changes. Codes
such as 2B and 2C name the work items it sized at the time (struct
classification and loader conversions). Everything below was read from the
tree at `d9e456d4`; line numbers are for that commit. Statements marked
**(inference)** were not traced to the end.

No table values or asset bytes are reproduced here. The per-format member
counts in section 2 are counts over the user's own extracted packs (outside
the repository, `/primary/dev/ico/assets/disc/dfdatas/MANIFEST.tsv`), the
same kind of shape fact `docs/LEGAL.md` allows for element counts.

## 1. What was examined

- Pack dispatch: `ico2/fumi/ios/cdvd.c:494-622` (`PackEnt`, `findPackKind`,
  `iosCdvdMgrPackLoad`), `ico2/fumi/include/cdvd.h:87-95` (`PackKind`),
  `build/data/init-func.c` (generated, 26 rows, not committed).
- Loaders: `ico2/common/src/charFileManager.c:106-687`.
- Per-format consumers:
  `ico2/common/src/PObj.c:174-371` (`AllocPObj`, `InitPObj`, `MakePacket`),
  `ico2/seki/include/DisplayP2O.h:60-174` (`PObjPart`, `PObjModel`),
  `ico2/seki/src/Shadow.c:1106-1179` (`shadow_MakeObjectData`),
  `ico2/seki/src/Packet.c:1021-1064` (morph/cloth copy-out),
  `ico2/seki/src/Texture.c:66-94, 799-1064` (TIM2 headers, `tex_makeTexturePacket`, `tex_initTextureSub`),
  `ico2/seki/src/StageAnimation.c:133-172` (`stage_ApplyData`),
  `ico2/seki/src/BgAnimation.c:40-249, 251-279, 296-314, 396-413` (`bga_InitData`, SDF camera, particle record),
  `ico2/seki/include/BgAnimation.h:18-48` (`BgaAnim`, `BgaHeader`),
  `ico2/sugipon/src/motionFileManager.c:42-157` (`pursueNodeList`, `relocMotionFile`),
  `ico2/sugipon/src/particleEffect.c:600-647` (`SetParticleEffectPackage`),
  `ico2/fumi/sound/s_init.c:286-340, 465-508` (`soundDataOpenChk`, `soundBDDataSet` and kin),
  `ico2/fumi/include/s_init.h:17-45` (`SqEntry`),
  `sce/libsndn2/sound.c:2024-2054, 2093-2130` (`SgVabOpenFakeBody`, `SgBgmOpen`),
  `ico2/fumi/sound/adpcm_init.c:105-170` (`adpcmDataSet`),
  `ico2/fumi/ios/shockdriver.c:350-356`, `ico2/fumi/include/shockdriver.h:88-104`,
  `ico2/omori/src/camera-ico2.c:111-147, 948-1010`, `ico2/omori/include/camera-editor.h:18-50`,
  `ico2/common/include/typedef.h:530-547` (`SkelNode`), `:828-907` (`StageSetting`).
- Loads outside the pack table: `ico2/fumi/src/jimaku.c:219`,
  `ico2/fumi/sound/adpcm_init.c:185,231`, `ico2/sugipon/src/streamMotionManager.c:459`,
  `ico2/fumi/ios/mcdata.c:56`, `ico2/seki/src/FileManager.c:73-244`.
- The 73 ELF data tables: `config/data_schema.pal.txt`,
  `config/data_members.pal.txt`, `tools/gen_data_c.py` (its `Header`
  type model was reused by a scratch script that walks each record type and
  classifies every pointer word in the base ELF by target section; only the
  classification is reported here).
- Save path, for the classification only: `ico2/common/src/gamesys.c:50-79, 301-321, 511-530`.

## 2. The pack container

`DFDATAS/*.DF` is a 16-byte header whose first word is the member count,
then `count` directory entries of 0x224 bytes (`PackEnt`: `id`, `kind`,
`word08`, `size`, `name[532]`), then the members back to back
(`cdvd.c:571-608`). The loader is chosen from the extension after the last
`.` in the member name by a linear search of `initFunc` (`cdvd.c:508-539`);
an unmatched extension goes to `temp_loadfunc`, which reads and discards
(`cdvd.c:485-492`). Loaders are called with the open handle and must
consume exactly `size` bytes; several read into a null buffer to skip
(`iosCdvdHandlerRead(h, 0, size)`). `PackEnt` has no pointers or 64-bit
fields: a pure disc overlay, read once into a temporary buffer.

Member counts over the user's 172 packs (20,265 members), by extension:

| ext | members | loader row used? |
|---|---|---|
| mob | 7087 | yes |
| tm2 | 4616 | yes |
| p2o | 3806 | yes |
| bga | 1958 | yes |
| p2s | 748 | yes |
| cl | 687 | yes |
| cam | 255 | yes |
| skb | 214 | yes |
| p2c | 213 | yes |
| hd | 149 | yes |
| gcm | 68 | yes |
| zzz | 63 | yes |
| ssb | 61 | yes |
| bd | 57 | yes |
| pef | 53 | yes |
| bd3, bd4 | 46 each | yes |
| p2g | 23 | yes |
| plo | 5 | yes |
| svd | 1 | yes |
| sq, bd2, bd5, bd6, p2v, int | 0 | rows exist, no pack member uses them |
| ico, sys (ICON.DF), jim (OTHERS1.DF) | 3, 1, 1 | no row: `temp_loadfunc` skips them; they are read by name elsewhere (mcdata.c, jimaku.c) |

There is no `textbin` extension anywhere in the tree (`grep -ari textbin
ico2/` finds nothing); subtitles are the `.jim` stream. `.int` (ADPCM),
`.smb` (stream motion) and `.pss` are loose disc files read through
`iosCdvdBackGroundMgrAdd`, not pack members.

Size checks that matter for host bounds: every `.pef` member is at most 160
bytes, the slot size `SetParticleEffectPackage` copies into without a bound
(`particleEffect.c:646`); every `.ssb` is at most 304 bytes against
`sizeof(StageSetting)` = 0x1D0, which `ReadStageSettingFile` `memcpy`s
without a bound (`charFileManager.c:684`). Both hold on this disc; the port
should assert them.

## 3. The 26 loader rows

Columns: **reloc** = the loader (or its callee) rewrites file offsets into
absolute 32-bit addresses inside the loaded buffer; **host ptrs** = after
loading, game code dereferences pointer-typed fields of the file image or of
records copied verbatim out of it; **64-bit** = the on-disc layout has 8-byte
fields; **buffer** = what happens to the read buffer.

| ext | loader (charFileManager.c) | callee / header struct | reloc | host ptrs | 64-bit | buffer | class |
|---|---|---|---|---|---|---|---|
| p2o p2c p2g plo | `ReadModelFile` :106 | `InitPObj` → `AllocPObj` (PObj.c:273-371); `ObjHdr` (:207), `ObjRec` (:221), poly/strip/morph tables | yes, PObj.c:282-338: `objTbl`, `texTbl` and each texture entry, each object's `vtx nrm uv col mats texDefs polys strips lines morphs`, every `ObjEnt.p`, strip and morph entry | yes: each 0x180-byte `ObjRec` is copied by value into the runtime `PObjModel.parts` (`AllocPObjSubs`, PObj.c:250-258) and `PObjPart` (DisplayP2O.h) is used with those pointers | no | freed right after `InitPObj` (:142) | disc overlay (`ObjHdr`, `ObjRec`/`PObjPart` as read); `PObjModel` runtime-only |
| p2v | `ReadVolumeModelFile` :145 | same as p2o | yes | yes | no | freed (:175) | as p2o (unused on this disc) |
| p2s | `ReadShadowModelFile` :181 | `AllocPObj` then `shadow_MakeObjectData` (Shadow.c:1106-1179) | yes (AllocPObj) | copy-out: vertices, polys (with their `pts` pointers) and strip runs are copied to new allocations; `ShadowPoly` (16 B, holds a pointer) and the strip table (`mallocseki(stripCount * 4)`) are pointer-sized by hand | no | freed (:212) | disc overlay in, runtime-only out |
| tm2 | `ReadTextureFile` :215 | `tex_InitTexture` → `tex_initTextureSub` (Texture.c:997), `tex_makeTexturePacket` (:900), `tex_makeCopyImage` (:799) | no offsets; CLUT reordered in place (`tex_convertClutCSM2ToCSM1`, :728) | no: header copied into `TexData.pic`/`.mip`, pixels copied to new allocations; `TexData.tim2` keeps the freed file address and is only tested for non-null (Texture.c:917, 1099) | **yes**: `Tim2Picture.GsTex0/GsTex1`, `Tim2Mipmap.GsMiptbp1/2` (u64) | freed (:243) | disc overlay (TIM2 is a public format; fields 8-aligned in the file) |
| skb | `ReadSkeltonFile` :258 | `SkelNode[]` (typedef.h:537, 0x40 B), `-1` mirror terminator | no; links are indices | no pointers | no | kept, shared between models with the same path | disc overlay; a byte checksum over `j*64` bytes is stored in `skelSum` (:292), so any re-layout must checksum the original bytes |
| cl | `ReadCollisionFile` :308 | `Coll` (:32), `Bone` (:25, 0x50 stride), two 32x32 int block grids | yes: `wcl fcl wblk fblk ofs` (:336-340) and every nonzero grid cell (:347-360) | yes: grid cells and `wcl` are walked as pointers; **the loader also writes a heap pointer into each file record** (`Bone.sinCos` at 0x4C, :361-366) | no | kept | disc overlay with runtime fields |
| bga | `ReadStageAnimationFile` :376 | `stage_ApplyData` (StageAnimation.c:133) → `bga_InitData` (BgAnimation.c:170-249); `BgaHeader` (BgAnimation.h:35), `BgaDObjEnt`, `BgaEnvEnt`, `BgaMotion`, `BgaKey` | yes: `dobjs`, each `motion`, `env`, every env `data`, the first word behind env types 0-3 and 6, the `u.next` chain | yes, and the loader stores heap pointers into the file: `roots` (a `(n+1)*4` int array, :154), `anim` (:185), later `u.obj` (a 48-byte heap record, :405), `child`/`sibling` (:96-119); `stage_Init` writes `group`/`cut`/`mode` into the header (StageAnimation.c:244-246) | the heap particle record (`BgaParticleBits`, :299-314) is read and written as a doubleword; not in the file | kept; `stageTable[id].data` points at it | disc overlay, heavily mutated; also **floats are rewritten in place at load** (env type 6 key normalisation, :204-228), so the conversion must keep the same float operations in the same order |
| cam | `ReadStageAnimationFile` :376 | `stage_ApplyData` stores the raw buffer; `bga_InitSdfCamera` (BgAnimation.c:269) checks the "SDF" tag; `BgaSdfCam` (:261) | no | no pointers; `frame` and `mode` are written at run time | no | kept | disc overlay with runtime fields |
| mob | `ReadMotionFile` :406 | `InitMotionFile` → `relocMotionFile`, `pursueNodeList` (motionFileManager.c:53-157); `MotFileHdr`, `FacialRec`, `NodeRec` | yes: header words 1-4, the facial table, every node-list slot, and for node formats 2/5 and 3/6 the words inside the node | yes: `nodeList` is a zero-terminated array of 4-byte slots turned into pointers; `CheckMotionIncludeFacialData` (:91-100) compares `self+16` against the already relocated `typeList` as unsigned 32-bit addresses | no | kept in `motionTable[id]` (static, dynamic or swap partition) | disc overlay |
| pef | `ReadParticleEffectFile` :442 | `SetParticleEffectPackage` (particleEffect.c:639): defaults, then `memcpy` of the file into a 160-byte `PEPackage` slot | no | no pointers | no | freed (:448) | disc overlay (`PEPackage`, 0xA0) |
| sq | `ReadSoundSqFile` :559 | `soundSQDataSet` → `SgBgmOpen` (sound.c:2093): checks "SSsq" at 0xC, stores `(int)sq` in the sequence context | no | the sequencer walks the EE copy through an int address | not checked | kept | disc overlay (unused on this disc) |
| hd | `ReadSoundHdFile` :497 | `soundHDDataSet` → `SgVabOpenFakeBody` (sound.c:2024-2054): checks "SShd" at 0xC, stores `(int)hd` in the VAB context | yes, into separate header slots: words at 0x10, 0x18-0x24 are offsets, their absolute addresses are written to 0x30, 0x38-0x44 | yes, by the EE sequencer (R1 covers the IOP side) | not checked | kept (`semiCommonHdBuf` for the semi-common bank) | disc overlay |
| bd bd2-bd6 | `ReadSoundBdFile` :451 | `soundBDDataSet` (s_init.c:465): 64-byte rounded, DMA'd to SPU RAM in 0x78000 chunks; `SqEntry.bd` keeps the EE address as `int` | no | no (SPU payload) | no | freed after the DMA (:484) | opaque payload |
| int | `ReadSoundAdpcmFile` :599 | `adpcmDataSet` (adpcm_init.c:105), size capped at 0x5C000 | no | no | no | freed (:616) | opaque payload (unused as a pack member) |
| svd | `ReadShockFile` :623 | `Init_ShockVoiceSet` (shockdriver.c:350): four pointers into the image from 16-bit offsets at half-words 1, 3, 5 | no in-place rewrite; pointers live in a `ShockVoiceSet` header the loader places in front of the image | yes, through the side header | no | kept; allocated as `size + 16`, the 16 being `sizeof(ShockVoiceSet)` on the EE | disc overlay; `ShockVoiceFile` runtime-only |
| gcm | `ReadCamerasetFile` :652 | `AddPluralCameraSet` → `ReadCameraSet` (camera-ico2.c:948-1010): converts versions 0-3 into a new block; `CamSetFile` (:111), `CamGroup` (camera-editor.h:41, 0x4C), `PinRec` (0x5C) | no; the converted copy gets `CamGroup.items` written (:147) | yes, in the copy | no | freed (:666); the copy is allocated as `16 + count*76 + total*92` (:975) | `CamSetFile`/`CamGroup`/`PinRec` disc overlay; the converted block uses the same records with a live pointer |
| ssb | `ReadStageSettingFile` :677 | `memcpy` into `GlobalStageSetting` (`StageSetting`, typedef.h:828-907) | no | no pointers | no | freed (never: buffer leaks, `iosMallocDebug` without free, :682) | disc overlay (frozen layout) |
| zzz | `ReadEndCheckFile` :669 | read and freed; a pack terminator | no | no | no | freed | none |

Hand-written sizes tied to these formats (64-bit hazards for 2C/2D-2F):
`mallocseki(0xD0)` for `PObj` (PObj.c:293), `0x880` and `0x100` and
`partCount * 80` in `MakePacket` (:184-190), `sizeof(PObjSub)` 0x180
copies (:254), shadow `stripCount * 4` (Shadow.c:1157), BGA roots
`(n + 1) * 4` (BgAnimation.c:154), BGA particle record `48` (:405),
`ShockVoiceFile` `size + 16` (charFileManager.c:633, 643), camera set
`16 + count*76 + total*92` (camera-ico2.c:975). The PObj-side types
`PObj` (PObj.c:33) and `PObjModel` (DisplayP2O.h:146) are two views of
the same allocation and disagree at 0x28 (`pkt` vs `dobj`).

**Freed images.** Model, volume, shadow and texture images are freed as soon
as the loader returns, while `PObjModel.image` (PObj.c:263) and `TexData.tim2`
still hold the address. Both are only tested against zero in the code read
here. Whether any `PObjPart` pointer into the freed model image
(`uv`, `col`, `mats`, `texDefs`, `lines`) is dereferenced after load is
not settled: `Packet.c` reads them while building packets during
`MakePacket`, and copies `vtx`/`nrm`/`strips`/`morphs` out for deformable
models (Packet.c:1021-1064). **(inference)** All later reads go through the
built packets; an ASan run of the 32-bit build in 2C will confirm.

## 4. The 73 ELF data tables

73 members, 74 schema rows (`staffroll_dat` has a second, `count-of` row).
Census by element type, from the record types in the owners' headers and a
classification of every pointer-typed word in the base ELF:

- **No 8-byte fields** (`long long`, `double`) in any record type.
- **No int-typed field holds addresses**: every 4-byte integer field was
  checked for nonzero values that all fall in the ELF's address range; none
  does (heuristic: a field with mixed small values and addresses would pass
  unnoticed).
- **61 members have no pointer fields.** Their records are integers,
  floats, bit-fields and fixed `char[]` names (paths are inline arrays, for
  example `PObjMdl.path[48]`, `TexRec.path[48]`). They can be loaded
  byte-for-byte on any host once the record layout is frozen with
  `_Static_assert`s.
- **12 members have pointer fields:**

| member | record | pointer fields | target in the ROM |
|---|---|---|---|
| act-intrlist | `IntrMail` | `accept`, `extra`, `handler`, `motion` | functions |
| obj-kind-data | `ObjKindEnt` | 11 function pointers (`ai`, `before`, `create`, `dl`, `geo`, `hotInit`, `infoInit`, `infoLoad`, `start`, `uniqDataSet`, `afterGeo`; the last is null in every row) | functions |
| obj-layout | `GenGeo` | `proc` (function), `outGObj` (object) | functions; `outGObj` points into `.sdata` (`scpDummyGObj`, `gen_data_c.py` `SUPPLEMENT`) |
| texture-layout | `LtProp` | `proc` | functions |
| act-mode-def | `ActModeRec` | `ent[].act` | functions |
| init-func | `PackKind` | `func` | functions |
| se-env | `SeEnvDef` | `proc` | functions |
| sedef | `SeDef` | `check` | functions |
| stage-all | `StgPre` | `initproc`, `endproc` | functions |
| staffroll_dat | `char *` | the element | the member's own string pool |
| tex-property | `LtProperty` | `texData` | null in every row; filled at run time |
| stage-anim | `StageAnimDef` | `data` | null in every row; filled at run time by `stage_ApplyData` |

So: 9 members carry function pointers, 1 carries string pointers, 3 carry
object pointers, of which 2 are always null in the ROM and are run-time
slots. No table points into another table.

**Writes into `.rodata` tables.** The generator defines `rodata` rows
`const`. At least two are written at run time, which works on the EE (no
page protection) and faults on a host where `const` data is read-only:

- `stageTable` (`stage-anim`): `stage_ApplyData` stores the loaded file
  address into `obj->data` (StageAnimation.c:158, 160) through a non-const
  `extern StageAnimDef stageTable[]` (:85).
- `motionLimitDef` (`motion-limit-def`): rows are swapped and rewritten in
  place (motionOrientManager.c:1436-1446), and `(int)&motionLimitDef[i]` is
  stored in an int field (:1433).

29 `rodata` symbols have their address taken or a non-const `extern`
(a grep for `&sym[`, `= sym` and non-const `extern` declarations); only the
two above were confirmed as writes. The rest need the same check (2B).

## 5. Classification summary

| class (plan's terms) | members |
|---|---|
| runtime-only (natural layout, real pointers) | `PObj`/`PObjModel`, `PObjPkt`, `ShockVoiceSet`/`ShockVoiceFile` header, `BgaAnim`, the BGA particle record, `SqEntry`, `TexData`, `CharFile`, `StageAnim`, the shadow copies |
| disc overlay, read-only after load | `PackEnt`, `SkelNode`, `PEPackage`, `StageSetting`, TIM2 headers (u64 fields), `CamSetFile` (input side), `.bd`/`.int` payloads |
| disc overlay, relocated in place or carrying run-time pointers | `ObjHdr`, `ObjRec`/`PObjPart` (and the poly/strip/morph tables), `Coll` + block grids + `Bone.sinCos`, `BgaHeader`/`BgaDObjEnt`/`BgaEnvEnt`/`BgaMotion`, `MotFileHdr`/node list/`FacialRec`/`NodeRec`, the VAB header slots 0x30-0x44, `CamGroup.items` in the converted set |
| disc overlay with run-time state fields | `BgaHeader` (`mode`, `frame`, `group`, `cut`), `BgaSdfCam` (`frame`, `mode`) |
| ELF tables, pointer-free | 61 members |
| ELF tables with function/string pointers | 10 members (9 function, 1 string) |
| ELF tables with run-time object pointer slots | `tex-property`, `stage-anim`, plus `obj-layout.outGObj` |
| save-serialised | none of the loaded formats; the card image is built by `gameSysMemoryFuncList` (gamesys.c:50-69: version, gflag, object info, generator info, hint info, character info, back stage) into `gameSysMainSaveBuff[25596]` (2B owns it) |

## 6. Recommendations

**2C (loader conversions).** Nine formats need work on a 64-bit host:
p2o family, p2s, cl, bga, mob, hd (EE side), gcm, svd, and the model copy
into `PObjModel.parts`. Two strategies, chosen per format:

1. *Offset fields plus accessors* (cheapest where the image stays resident:
   cl, bga, mob, hd, cam). The file structs keep their EE layout; every field
   the loader relocates becomes a `u32` offset, and code reads it through
   `ICO_PTR(base, off)`. The relocation loops become no-ops, or validation
   only, on every host, so 32-bit and 64-bit builds run the same code. Where
   the loader stores a heap pointer *into* the file (`Bone.sinCos`,
   `BgaHeader.roots`/`anim`, `BgaDObjEnt.u.obj`/`child`/`sibling`), store it
   in a parallel host-side array indexed by record number instead of the
   4-byte slot.
2. *Decode to a native record* (where the image is freed: p2o family, p2s,
   gcm). `AllocPObj` already copies each `ObjRec` into `PObjPart`; make
   `PObjPart` a runtime-only struct with real pointers and fill it field by
   field from the frozen disc record. Same for `CamGroup` in
   `ReadCameraSet` and for `ShockVoiceFile` (size the header with
   `sizeof`).

The `mob` node list is the awkward case: a zero-terminated array of 4-byte
slots in the file, each turned into a pointer, with formats 2/5 also writing
a pointer into the node. Keep it as offsets (strategy 1) and rewrite
`_getMotion`'s readers, rather than allocating a parallel pointer array.
Keep the order of the in-place float edits in `bga_InitData` (env type 6)
exactly as it is: it runs once per load and determines animation weights.

Replace every hand-written size in section 3 with `sizeof`, and assert the
two unbounded copies (`.pef` <= 160, `.ssb` <= `sizeof(StageSetting)`).

**2B (classification).** Freeze with `_Static_assert` (size and every
offset the loaders use): `PackEnt`, `SkelNode`, `PEPackage`, `StageSetting`,
`Tim2Picture`, `Tim2Mipmap`, `ObjHdr`, `ObjRec`, `Coll`, `Bone`,
`BgaHeader`, `BgaDObjEnt`, `BgaEnvEnt`, `BgaMotion`, `BgaKey`, `BgaSdfCam`,
`MotFileHdr`, `NodeRec`, `CamSetFile`, `CamGroup` (disc side), `PinRec`.
TIM2's u64 fields sit on 8-byte boundaries in the file; i386 ILP32 aligns
`long long` struct members to 4, so `Tim2Picture` and `Tim2Mipmap` need an
explicit `aligned(8)` on those fields (or `-malign-double`) in the 32-bit
build. Audit the 27 remaining `rodata` tables flagged in section 4.

**5 (runtime table loader).** 61 of 73 tables are flat records: load them
byte-for-byte into frozen structs. The other 12 need only two kinds of
fix-up: a function-pointer field is a ROM address to be mapped to a host
function (a generated address-to-symbol table over the 9 members'
function fields, from the symbol list the generator already reads), and
`staffroll_dat` is a string table to be rebuilt as host `char *`. The three
object-pointer fields are null or a single known global
(`scpDummyGObj`). Generate `stage-anim` and `motion-limit-def` as
non-const (or every table as non-const); this is a correctness issue on the
host, not a style one.

## 7. Open questions

1. Is any `PObjPart` pointer into the freed model image (`uv`, `col`,
   `mats`, `texDefs`, `lines`) read after `ReadModelFile` returns? Not
   traced through `Packet.c`'s line-part path (Packet.c:1234-1237) or
   `RegistPacket.c`. An ASan run of the host build settles it.
2. Which of the 27 other `rodata` tables whose address escapes are written?
   (Grep in section 4; only `stageTable` and `motionLimitDef` confirmed.)
3. `CheckMotionIncludeFacialData` compares a buffer address against a
   relocated pointer as unsigned 32-bit numbers. With offsets it becomes
   `typeList_offset > 16`; confirm no other caller passes an unrelocated
   header.
4. The sequencer's use of the relocated VAB header slots (0x30-0x44) and of
   the `.sq` image is in `sce/libsndn2/sound.c`, which moves to
   `port/audio/sg/` (R1 / Phase 4). Does it read any other header word as an
   address?
5. Rows for `sq`, `bd2`, `bd5`, `bd6`, `p2v` and `int` have no members in
   the PAL packs. Keep them (cheap), but they cannot be tested with real
   data.
6. `ReadStageSettingFile` never frees its buffer (charFileManager.c:682):
   about 300 bytes a stage from the seki partition. Original behaviour, so
   it stays; the port's arena accounting should expect it.
