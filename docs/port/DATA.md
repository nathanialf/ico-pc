# Game data: the disc layer

How the host build reads the game's disc (package 1C): the VFS, the
ISO9660 backend used in dev mode, the libcdvd and SIF host layers under the
game's unchanged cdvd manager, IOP RAM, and the interface Phase 5's archive
backend implements. No disc data is in this repository (`docs/LEGAL.md`);
everything below is structure, sector numbers and hashes, and the tests read
the user's own image at run time.

## Layers

```
game     fumi/ios/cdvd.c        manager thread, request queue, 7-slot background
                                table, stream manager, 200-entry directory cache,
                                DATA.DF directory (unifile_read_func)
         seki/src/FileManager.c boot: file_Init, file_LoadCDFile
         fumi/sound/adpcm_init.c ADPCM streams into IOP memory (sceCdReadIOPm)
------------------------------- the seam: libcdvd / sifrpc / sifdev symbols
port     port/data/cdvd_host.c  sceCd* over the VFS disc
         port/data/sif_host.c   sceSif* (no IOP), IOP heap, SIF DMA, RPC to host servers
         port/data/iop_ram.c    ico_iop_ram (2 MB) and the IOP heap
         port/data/vfs.c        paths, byte reads, the disc slot
         port/data/iso9660.c    backend: the user's ISO (dev mode)
         (Phase 5)              backend: the extracted archive
         port/data/tables.c     the 73 data tables, from the boot ELF on the disc
```

The seam is the SDK's own function names, as declared in
`port/compat/libcdvd.h`, `sifrpc.h` and `sifdev.h`: the game calls
`sceCdRead`, the host build links `port/data`'s definition. `cdvd.c` is not
edited; everything above the seam (the queue, the threads, the cache, the
retry and drive-recovery paths) runs as written. `FileManager.c` has one
`#ifdef ICO_HOST` block (below).

## The VFS (`port/data/vfs.h`)

The game addresses the disc as libcdvd does: look a file up by ISO9660 path
and get its first logical sector number (LSN) and byte size, then read
whole 2048-byte sectors by LSN. It also does its own LSN arithmetic: the
DATA.DF directory gives byte offsets inside DATA.DF, and
`unifile_read_func` (`cdvd.c`) caches each member as
`DATA.DF's LSN + offset / 2048`. So a backend serves one flat LSN space
numbered as on the disc, and the VFS keeps sector semantics:

| call | what |
|---|---|
| `ico_vfs_mount(backend, location)` / `ico_vfs_unmount` | open a volume through a backend table |
| `ico_vfs_set_disc` / `ico_vfs_disc` | the volume the libcdvd layer reads |
| `ico_vfs_normalize` | any spelling below to `DIR/FILE` |
| `ico_vfs_stat`, `ico_vfs_open`, `ico_vfs_size` | look-up: LSN, size, ISO date, directory flag, name |
| `ico_vfs_read(file, offset, dst, len)` | bytes, clipped to the file |
| `ico_vfs_read_sectors(vfs, lsn, count, dst)` | whole sectors anywhere on the volume |
| `ico_vfs_lsn_to_offset`, `ico_vfs_offset_to_lsn`, `ico_vfs_size_to_sectors` | sector maths (2048 bytes) |

Path spellings, all case-insensitive: `DFDATAS/DATA.DF`, `\DFDATAS\DATA.DF;1`
(what `chgFileName` in `cdvd.c` and `file_LoadCDFile` in `FileManager.c`
produce), `cdrom0:\SYSTEM.CNF;1` (SYSTEM.CNF and `FileManager.c`'s module
paths). Device prefix, leading separators and the `;version` suffix are
dropped, `\` becomes `/`.

The names the game looks up on the disc itself are few: `\DFDATAS\DATA.DF;1`
(the manager's first request), `\SCES_507.60;1` (the disc check), and
whatever `file_LoadCDFile` is handed. Every other `\DFDATAS\<NAME>;1` the
game asks for (packs, `.int` ADPCM, `.smb` stream motion, `.pss` movies)
resolves in `cdvd.c`'s own directory cache from DATA.DF's directory and
never reaches the VFS by name; a name missing from that cache falls through
to `sceCdSearchFile`, which fails on this disc exactly as on the PS2.

### Backend table

```c
typedef struct IcoVfsBackend {
    const char *name;
    int (*mount)(void **state, const char *location);
    void (*unmount)(void *state);
    int (*lookup)(void *state, const char *path, IcoVfsEntry *out);   /* "DIR/FILE" */
    int (*read_sectors)(void *state, uint32_t lsn, uint32_t count, void *dst);
    uint32_t (*volume_sectors)(void *state);
} IcoVfsBackend;
```

### Backend 1: ISO9660 (`port/data/iso9660.c`)

Read-only, over a host file, no external library. It reads the primary
volume descriptor (ECMA-119 8.4) and walks directories from the root
record (9.1); no path table, no Joliet or Rock Ridge, no multi-extent files
(refused with a message). 64-bit file offsets on every host (`fseeko` with
`_FILE_OFFSET_BITS=64`, `_fseeki64` on Windows). PS2 mastering records a
directory's length as the bytes in use, not a sector multiple; the walk
reads every sector the length touches and stops at a zero length byte in
each.

### Backend 2 (Phase 5): the archive

The first-run extractor's archive must look like the disc to everything
above `read_sectors`:

1. **Keep disc LSNs.** Store, per file, its disc path, first LSN and byte
   size; `lookup` answers from that table and `read_sectors` maps an LSN
   range onto the file containing it. DATA.DF must stay one contiguous LSN
   range (`DATA.DF's LSN + offset / 2048` is computed by the game).
   Store whole sectors, so the tail of a file's last sector reads as it did
   from the disc; a range that touches no stored file fails (`-1`).
2. **`volume_sectors`** reports the disc's volume size, so the end-of-disc
   checks match.
3. **What to store:** `SYSTEM.CNF` (the disc identification below reads its
   BOOT2 line), the boot file it names, `SCES_507.60`, **with its bytes**
   (the table loader reads the whole ELF through `ico_vfs_open`/
   `ico_vfs_read` at boot, "The data tables" below; 5,515,680 bytes), and
   `DFDATAS/DATA.DF`. Keeping only the 75 table ranges would also do (about
   1.1 MB, every range in `config/tables_manifest.txt`), but then the archive
   needs its own container for them and `tables.c` a second entry point;
   storing the file is simpler and the loader already checks every range's
   CRC-32. Anything
   else the port extracts for its own use (the SNDN2DRV pitch table,
   `docs/research/sndn2drv.md`) is not a disc file to the game.
4. Give it its own backend table (say `ico_vfs_archive`), mount it with
   `ico_vfs_mount` and hand it over with `ico_vfs_set_disc` before the game
   boots; the libcdvd layer then never opens the ISO.

## libcdvd on the host (`port/data/cdvd_host.c`)

The subset the game links (found with `nm -u` over `ico_game`):
`sceCdInit`, `sceCdMmode`, `sceFsReset`, `sceCdDiskReady`, `sceCdStatus`,
`sceCdGetDiskType`, `sceCdGetError`, `sceCdBreak`, `sceCdSearchFile`,
`sceCdRead`, `sceCdReadIOPm`, `sceCdSync`, `sceCdReadClock`,
`sceCdStInit`, `sceCdStStart`, `sceCdStRead`, `sceCdStStop`, plus
`sceCdStStat` (the renderer-owned `mv_main.c`), `sceCdStSeek`,
`sceCdStPause`, `sceCdStResume`, `sceCdStream` and `sceCdSyncS` for
completeness.

| call | host answer |
|---|---|
| `sceCdInit` | mounts the default image if no disc is set (`ICO_ISO`, else `baserom/Ico_PAL.iso`), registers the vsync completion once; 1 |
| `sceCdDiskReady` | 2 (complete) with a disc, 6 (not ready) without |
| `sceCdStatus` | 0x0A (pause, idle) with a disc, 0 without |
| `sceCdGetDiskType` | 0x14 (PS2 DVD, `cdvd.c`'s `cdDiskType`) when SYSTEM.CNF's BOOT2 names a file on the disc, else 0 (no disc) |
| `sceCdSearchFile` | VFS look-up; writes libcdvd's 0x24-byte record (lsn, size, name[16], date[8], flag), never more |
| `sceCdRead`, `sceCdReadIOPm` | transfer now, completion at the next vsync (below); 0 while a command is in flight |
| `sceCdSync` | even mode (blocking): from a game thread, waits (WaitSema) for the vsync that ends the command, then 0; from the host context, ends it at once. Odd mode (poll): 1 until the next vsync |
| `sceCdGetError` | 0, or 0x01 aborted, 0x12 no disc, 0x20 bad address, 0x30 read error, 0x32 past the end (the codes `FileManager.c` names) |
| `sceCdReadClock` | a fixed clock, 2002-01-01 00:00:00 in BCD, stat 0; `ico_cdvd_host_set_clock_source` replaces it |
| stream calls | read straight from the disc at the stream cursor; `sceCdStStat` reports the ring as full |

**Timing.** On the PS2 a read starts and `sceCdSync` waits for it. The host
copies the sectors when `sceCdRead` returns (no caller can look before it
syncs) and models completion so the game's control flow is the drive's:

- a poll (`sceCdSync(1)`, the background reader in `iosCdvdBackGroundRead`
  and `...ReadIOPm`) sees the command busy until the next simulated vsync,
  so the reader's `cdWait` sleeps once, as it did while the drive worked.
  The completion is `ico_cdvd_host_vsync`, registered with
  `ico_host_on_vsync_register` (`port/platform/host_loop.h`, package 1B).
  The host loop runs it after the vblank interrupt's handlers and before
  the woken threads run, so a cdvd thread woken by the vblank handler finds
  its read complete;
- a blocking wait (`sceCdSync(0)`: the stream manager, `file_LoadCDFile`)
  blocks the calling thread on a semaphore until the same vsync, as
  libcdvd's does (`sce/libcdvd/cdvd000.c` `sceCdSync`: it loops on
  `sceCdDelayThread`, which is `CreateSema`, `SetAlarm`, `WaitSema`), so
  the other threads run meanwhile, the same-priority Main among them.
  Package 1C completed it on the spot instead, on the assumption that a
  blocking caller only waits; that was wrong: a stage load then ran from
  start to end without a Main tick, and the load thread clipped against a
  collision list Main had built before StageManager removed every object
  (a NULL `dobj` in `fieldCollision.c` `_Clip`, the Phase 1 x86 crash at
  tick ~95; `docs/port/BOOT_DIAG.md`). From the host context (the unit
  tests) the wait still completes at once.

**Disc identification.** `cdvd.c` keeps its check
(`iosCdvdDiskReadyBlock`, `cdWait`): drive type 20 and a search for
`\SCES_507.60;1`. On the host the type comes from the image's SYSTEM.CNF
(BOOT2 must name a file present on the disc), and the search goes through
the VFS, so the check identifies the image as it identified the disc. The
check only runs after the drive reports not-ready, which the host never
does with an image mounted; the authoritative check is the extractor's
hash (Phase 5, below).

**No disc.** With no image, `sceCdDiskReady` says not ready and
`sceCdStatus` stopped, and `file_Init` waits at its disc wait as a console
with an empty drive did. `cdvd_host.c` prints the path it tried once.

## Boot (`seki/src/FileManager.c`)

`file_Init` under `ICO_HOST` keeps `sceSifInitRpc`, `sceCdInit`,
`sceCdMmode` and one disc wait, and leaves out the `IOPRP224.IMG` reboot and
the six `sceSifLoadModule` calls (SIO2MAN, PADMAN, MCMAN, MCSERV, LIBSD,
SNDN2DRV). `file_LoadCDFile`, the boot-time loader, is unchanged and reads
through `sceCdSearchFile` and `sceCdRead`. `file_Init` makes no SYSTEM.CNF
read of its own. The SIF calls also report success when called
(`port/data/sif_host.c`): reboot and sync 1, module loads a positive id.

## SIF and IOP RAM

`port/data/iop_ram.h`: `ico_iop_ram`, 2 MB, 64-byte aligned. An IOP address
(what `sceSifAllocIopHeap` returns and the game passes to `sceCdReadIOPm`,
`SgStAdpcmOpen`, `sceSifSetDma`) is an offset into it after stripping the
kseg bits (`ico_iop_phys`); `ico_iop_ptr` and `ico_iop_addr` convert both
ways. The IOP heap is first-fit with 16-byte granularity from 0x10000 to
the end of RAM (about 1.94 MB). That is more than the PS2 left free after
its modules, which is not modelled; the game's allocations at boot (the
sound area 0x78000, the ADPCM rings 0xB8800) and the movie stream buffer
(576 sectors + 16) are the sizes the tests exercise.

`sceSifSetDma` copies EE memory into IOP RAM at once; `sceSifDmaStat`
reports every transfer finished. RPC: `ico_sif_register_server(sid, fn)`
installs a host function as the IOP server for a service id;
`sceSifBindRpc` answers the bind (nonzero `serve`) only for a registered
id, `sceSifCallRpc` runs the server synchronously and copies its reply, and
`sceSifCheckStatRpc` reports every call finished. The sifdev calls
(`sceOpen`, `sceRead`, ...) served the dev kit's `host0:` and fail.

## Null devices (`port/null/`)

| file | stands in for | answers |
|---|---|---|
| `pad_null.c` | libpad | init and port open succeed; `scePadGetState` 0 (disconnected), so `pad.c` keeps both ports in their error state and hands the game zero buttons |
| `mc_null.c` | libmc | removed in Phase 4D: the memory card is `port/save/mc_host.c`, libmc over a host folder (`docs/port/SAVES.md`); port 1 there still answers as the null card did (-10, type 0) |
| `snd_null.c` | the Sg API and SNDN2DRV's RPC server | IOP half: server 0x736E646E, takes the init and tick calls, replies with a zeroed 0x200 page except the transfer counter at +0x1C0, echoed from the last 0x20/0x21 packet. EE half (until the sequencer moves to `port/audio/sg/`): `_SgSndn2Remote`, `SgSndn2RemoteInit/Sync` bind and call the IOP half through the host SIF; the rest accept requests and report nothing playing |
| `scf_null.c` | libscf | `sceScfGetLanguage` returns `ico_scf_language`, default 1 (English in libscf's numbering) |

`gfx_null.c` is package 1B's.

## Facts about the PAL disc relied on

Read from the user's image (`baserom/Ico_PAL.iso`, SCES-50760) with the
reader above; sector numbers and sizes only.

| fact | value |
|---|---|
| image size | 907,706,368 bytes |
| image SHA-1 (extractor's verification value) | `1017b53f6e80f41f823369b0be1d8c69f7e16dc6` |
| volume descriptors | primary at LSN 16, terminator at LSN 17; no supplementary (no Joliet) |
| logical block size | 2048 |
| volume space | 443,216 sectors |
| root directory | LSN 261, recorded length 1,168 bytes (not a sector multiple) |
| `DFDATAS` directory | LSN 262, recorded length 152; one file, `DATA.DF;1` |
| `SYSTEM.CNF;1` | LSN 287, 56 bytes; BOOT2 names `SCES_507.60` |
| `SCES_507.60;1` | LSN 762, 5,515,680 bytes; read through the VFS it hashes to `da3644c5...` (`config/sha1sums.txt`, `baseelf.elf`) |
| `DATA.DF;1` | LSN 19,771, 867,184,640 bytes, one extent |
| DATA.DF directory | 193 entries, every one sector-aligned and inside DATA.DF. With DATA.DF's own entry that is 194 of the 200 slots in `iosCdvdSrhBuff`, which `unifile_read_func` fills without a bound |
| `IOPRP224.IMG`, `SIO2MAN.IRX`, `PADMAN.IRX`, `MCMAN.IRX`, `MCSERV.IRX`, `LIBSD.IRX`, `SNDN2DRV.IRX` | present in the root (no longer read on the host) |

## The data tables (`port/data/tables.c`, package 5B)

The game's 73 data-only members (`config/data_schema.pal.txt`: 74 schema
rows over 75 rows of `config/data_members.pal.txt`) are ELF data: actor
modes, motion and sound definitions, object layouts, stage lists, the staff
roll. The PS2 build compiles them as C written from the user's ELF
(`tools/gen_data_c.py`). The host binary holds none of their bytes: it
defines each table as an uninitialised array, and before the game starts
the loader fills them from the boot ELF on the user's disc.

**Generated, committed (`port/data/gen/`, by `tools/gen_data_desc.py`).**
Types, offsets, sizes, names and CRCs only:

| file | holds |
| --- | --- |
| `table_defs.c` | `T name[count];` for every table (in `.bss`), the string-pool buffers, and `staffRollNameDataNum` (derived as `sizeof` less 2, as the PS2 build derives it) |
| `table_desc.c` | per record type, a field list `{path, host_offset, host_size, ee_offset, ee_size, count, kind, signed, bit, width, setter}`; per row, its member, symbol, section, EE range, CRC-32, first record offset, count, record type and host array |
| `ee_symbols.c` | the registry: the 914 distinct EE addresses the tables' pointer words hold (913 functions, 1 object, `scpDummyGObj`), sorted, each with the host symbol |
| `table_desc.h` | the descriptor types and `ICO_TABLE_SYMBOLS(X)` |
| `tables.cmake` | the table names, for the test's renamed reference |

The field lists come from the same parse of the record headers
`gen_data_c.py` does (`gen_data_c.Header`, EE layout rules). Each record is
flattened: scalars and arrays of scalars are one field each (`count` for
the array), arrays of records are expanded by index (`ent[2].act`), a
pointer-free union is copied as its bytes, a bit-field is one field with its
bit position and a generated setter (`((SeDef *)r)->procRan = v`), so its
host placement is the compiler's. `host_offset` and `host_size` are
`offsetof`/`sizeof` expressions, evaluated by the host compiler. The EE
offsets and sizes are asserted in `table_desc.c`: for pointer-free records
on every host, for records with pointers on a 32-bit host (`UINTPTR_MAX`;
checked with the i686 mingw compiler), and every scalar's host size equals
its EE size everywhere.

Coverage: 73 members, 68 record types, 75 rows (72 table rows, the
`staffroll_dat` string pool in `.rodata` and the head of its `.sdata`, and
its `count-of` row), 29,701 records. Field kinds used: int (1/2/4 bytes,
signed or not, arrays), float, union bytes (`MotionDef`'s three flag
words: `modeBits`, `flags`, `flags2`), bit-fields (in 9 record types:
`ActModeRec`, `AttackKindEntry`, `EnemyDef`, `LtProperty`, `PObjMdl`, `SeBank`,
`SeDef`, `SeEnvDef`, `StgPre`), 913 function targets over 9
members' fields, 1 object target (`GenGeo.outGObj`), char pointers into the
member's own string pool (`staffRollNameData`). No 8-byte field exists; the
generator refuses one, a pointer inside a union, and a bit-field wider than
32 bits.

**The manifest (`config/tables_manifest.txt`).** Per row, the CRC-32 (zlib)
of the ELF's bytes in its range, and the registry's `func`/`obj` lines (the
address and the symbol the committed symbol lists name there). Written by
`tools/gen_data_desc.py --manifest`, the only mode that reads the ELF; the
CRCs and names are not disc data (`docs/LEGAL.md`).

**Loading.** `ico_pc` mounts the disc, then calls `ico_tables_load_vfs`
(port/platform/main_host.c) before the window, the pad, the trace and the
game's `main`: nothing has read a table yet. The loader reads `SCES_507.60`
through the VFS (any backend: the ISO today, the archive later), finds
`.data`, `.rodata` and `.sdata` in its section headers, and:

1. checks every row's range against its section and its CRC-32 against the
   manifest, for all rows before writing any, so a wrong or modified ELF
   leaves the tables empty and `ico_pc` stops with the row, both CRCs and
   the reason (`ico_host_fatal`);
2. copies the string-pool rows into their buffers;
3. decodes each record field by field into the host array: integers,
   floats and unions copied (the EE and the hosts are little-endian; the
   loader refuses to compile on a big-endian host), bit-fields extracted from
   their EE unit and stored through the setter, function-pointer words
   looked up in the registry (binary search), object-pointer words resolved
   first into the member's own string pool (staffroll: a pointer to pool
   offset k becomes the host pool copy + k), then in the registry. A nonzero
   word the registry does not hold stops the load with the table, element,
   field, the address and the word's own address.

The 64-bit record layouts (8-byte function pointers in `SeDef`, `GenGeo`,
`ObjKindEnt` and the rest) are what the game's code is compiled against;
the loader writes exactly them. `StageAnimDef.data` and
`LtProperty.texData`, null in the ROM, are real host pointer slots, filled
at run time as before.

**const.** Every table is defined non-const: the loader writes them all,
and the game writes three of the PS2 build's const `.rodata` tables
(`stageTable` StageAnimation.c:154-160, `motionLimitDef`
motionOrientManager.c:1436-1446, `seDef` s_init.c:1031,1085,850), which
worked on the EE (no page protection). This replaces the old
`ICO_DATA_WRITABLE` list (`gen_data_c.py --writable`). Headers that declare
a table `extern const` (40 tables) are not edited: `table_defs.c` and
`table_desc.c` `#define` each such name to `<name>_header_decl` around their
`#include`s, so the header's declaration names an unused symbol and the
non-const definition does not conflict. Other translation units still see
`extern const`, which only stops them writing through that declaration; the
object itself is writable.

**`nodeLimit`.** The 2E scheme stays: `SetNodeRotationLimitDataTable` stores
a one-based `motionLimitDef` row in the 4-byte slot (docs/port/SWEEP_2E.md).
The table now lives in host `.bss`, still outside the arena, so an EE word
still cannot name it; nothing here needs it to change.

**Tests.**

- `data_desc_fresh`: `tools/gen_data_desc.py --check` regenerates
  `port/data/gen/` in memory and compares; that mode never opens the ELF, so
  the files are a function of the committed text (schema, headers, symbol
  lists, manifest) and carry no disc bytes.
- `tables_manifest` (when the base ELF is present): the manifest equals a
  recomputation from `baserom/pal/baseelf.elf`.
- `tables_loader` (`port/data/test/tables_test.c`, built when the base ELF
  and pyelftools are present): `gen_data_c.py --symbol-map` writes the 73
  members into the build directory, compiled under renamed symbols
  (`ico_ref_<name>`); the registry is compiled with stub definitions of its
  914 symbols, so both sides resolve pointers to the same addresses. The
  loader reads `SCES_507.60` through the VFS from the disc image (or the
  ELF file without one) and every byte of every host table must equal the
  compiled table's (the staff roll's char pointers compare by string). Then
  a copy of the ELF with one byte of `seDef` flipped must be refused naming
  `sedef` and its CRC, with the tables unchanged, and a non-ELF refused.

**Regenerating.** After changing the schema, a record header, the symbol
lists or `config/data_members.pal.txt`: `tools/gen_data_desc.py --manifest`
(needs the ELF, if a range or pointer target changed) then
`tools/gen_data_desc.py`. The output is clang-formatted by the generator.

## Tests

`port/data/test/vfs_test.c` (ctest `vfs_synthetic`, `vfs_disc`) and
`port/null/test/null_devices_test.c` (`null_devices`):

- `vfs_synthetic` builds a 40-sector ISO9660 image in the build directory
  (a SYSTEM.CNF, a boot file, a DATA.DF with a three-entry directory, a
  directory spanning two sectors with an unaligned length) and runs the VFS,
  the libcdvd layer (search records, poll vs blocking completion through
  the registered vsync callback, IOP-memory reads, the stream, errors, the
  clock) and the SIF layer (IOP heap, DMA, boot calls) against it. Needs no
  disc.
- `vfs_disc` reads the user's image: finds SYSTEM.CNF, SCES_507.60, DATA.DF
  and the modules, hashes the ELF through the VFS against `config/
  sha1sums.txt`, walks DATA.DF's directory as `unifile_read_func` does, and
  checks the libcdvd layer identifies the disc. It exits 77 (skipped) when
  the image is absent. `ICO_DISC_IMAGE` (CMake cache) names another path.
- `null_devices` checks the four null devices, including the sound
  server's reply page and transfer-counter echo.

## Divergences

For `docs/port/DIVERGENCES.md` (platform):

- Disc timing: every read command finishes at the next vsync, blocking or
  polled. Drive seek and transfer times are not modelled.
- `sceCdReadClock` reports a fixed time until the port clock lands, so the
  save serial (`mcMakeSerial`, `layout_action.c`) is constant.
- No pad, no card, no audio in the headless build (null devices).
