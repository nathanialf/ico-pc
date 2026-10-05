# Package 2J: the 32-bit presets retired, and the tree-wide raw-offset pass

Two parts. Base `36a1d73e` (the Phase 2 exit commit), working tree of
2026-10-05.

## Part 1: one architecture

The plan's Phase 2 exit: "delete the `ref-m32` and `win-x86-ref` presets and
any `#ifdef` paths that existed only for them". `36a1d73e` met the criterion
(x64 per-tick traces byte-identical to the 32-bit build's over 3000 ticks).

Removed:

- `CMakePresets.json`: `ref-m32`, `ref-m32-clang`, `win-x86-ref`,
  `win-x86-ref-clang` (configure, build and test presets).
- `cmake/toolchains/i686-linux-clang.cmake`, `i686-linux-gcc.cmake`,
  `i686-w64-mingw32.cmake`, `i686-w64-mingw32-gcc.cmake`.
- `cmake/IcoFlags.cmake`: the `-malign-double` branch of
  `ICO_GAME_LAYOUT_OPTIONS` and the `-msse2 -mfpmath=sse` branch of
  `ICO_SEMANTIC_OPTIONS` (both gated on 32-bit x86).
- `tools/fetch_toolchain.sh`: the i386 sysroot (four Debian packages and the
  overlay), the i686 mingw-gcc packages (compiler, runtime, binutils, dev),
  `SKIP_SYSROOT`. llvm-mingw, the x86_64 mingw-gcc, CMake and the deps stay.
  The mingw-gcc stamp changed, so the next run refetches that tree without
  the i686 half. `tools/toolchain/sysroot-i386/` was deleted from the
  working tree.
- `tools/package_win.sh`: the x86 half. One `x64/` folder
  (`ico_pc_x64.exe`), same layout otherwise; `TEST.md` text updated.
- Host code that existed only for 32-bit hosts: `port/platform/arena.c`
  (the 0x10000000 mmap hint and the "above 2 GB" warning),
  `arena.h`/`arena_test.c` (the below-2 GB claim and check),
  `fiber.c` (`force_align_arg_pointer` entry for Windows fibers on i386),
  `diag_host.c` (the i386 `ICO_ENTRY` realignment and the `Eip`/`Esp`/
  `REG_EIP`/`REG_ESP` register branches), `fiber.h` (the i386 and 32-bit
  Windows fiber backend text), `fpenv.c` (`__i386__`), `fpenv.h` and
  `port/math/test/newlib_test.c` (the `-msse2 -mfpmath=sse` notes and the
  i386 x87 NaN-quieting exception), `kernel_host.c` and `ico_libc.h`
  comments.
- `ico2/` branches: `typedef.h` (`Act.flags18.afterProc` and
  `afterProcHost` were gated on `__SIZEOF_POINTER__`; now on `ICO_HOST`
  alone, `ACT_AFTER_PROC` likewise), `omori/include/camera-editor.h`
  (`thread[sizeof(void *) == 4 ? 112 : 160]` is 160 under `ICO_HOST`),
  `fumi/include/adpcm_init.h` (the `__SIZEOF_POINTER__ == 4` `pad2C`).
  EE `#ifndef ICO_HOST` paths are untouched.

Kept on purpose:

- `_Static_assert(sizeof(void *) != 4 || ...)` in `memory.c`, `thread.c` and
  `PObj.c`: they hold the EE record sizes wherever pointers are 4 bytes,
  which includes the EE compile, so they are EE checks, not 32-bit host code.
- `ee_view.h` (`ICO_RAW`, `ICO_RAWP`, `ICO_MAX_SIZE`) and the `ICO_WORD`
  macros: host versus EE, not 32 versus 64.
- `tools/host_syntax_check.sh` (`gcc -m32 -fsyntax-only`) and
  `tools/period_env.sh` (a 32-bit preload for the period compilers): not
  host presets; the first is the 2B-era front-end gate, the second serves
  ee-gcc.
- `port/rhi/CMakeLists.txt`'s `i686-w64-mingw32` SDL3 prefix branch and
  `tools/fetch_deps.sh`'s i686 SDL3 folder: another package's files, and
  harmless (the branch is unreachable without a 32-bit Windows preset).
- `port/math/newlib/ico_libm.c` comments about the i386 ABI returning floats
  on the x87 stack (they document why an EE NaN pattern is unobservable
  there), and `port/input/pad-boot.txt`'s "Measured (package 1E, ref-m32...)"
  history.
- Historical docs that describe what the 32-bit presets measured
  (`BOOT_DIAG.md`, `SWEEP_2D.md` to `SWEEP_2I.md`, `LOADERS.md`, `AUDIO.md`,
  `HEADLESS_STUBS.md`, `docs/research/*`) are not rewritten.

Tests (`port/platform/test`):

- `ios_chain`: it asserts event order and thread state, never an EE
  address. It only skipped on 64-bit. The skip is gone and it runs on x64.
- `memory`: it asserted EE addresses (partition offsets 0x1BE7F60 and so on,
  block offsets, the 0x40-byte header). Kept and made to run on x64 by
  comparing against the 64-bit expected layout: `partition_layout()` follows
  memory.c's arithmetic for a record size pair, the test uses it with the
  host's `sizeof(IosMemNode)` (0x50) and partition record (0x70), and the
  same function with the EE's (0x40, 0x50) is checked against the
  hand-worked EE addresses and totals, so the EE oracle values stay in the
  file. Block offsets, usage counts and the realloc header are expressed in
  `sizeof(IosMemNode)`.
- `SKIP_RETURN_CODE 77` removed for both; `arena_test` lost its 32-bit
  below-2 GB check.

`tools/gen_layout_asserts.py`: the all-asserts-on-32-bit mode is
documentation now. `ICO_LAYOUT_EE` is no longer `__SIZEOF_POINTER__ == 4`; it
defaults to 0 and `-DICO_LAYOUT_EE=1` compiles the runtime structs' EE-layout
asserts for an EE-layout build. The overlay and save asserts, the
`ICO_LAYOUT_PENDING` handling and the generator are unchanged.
`port/test/layout_asserts.c` was regenerated (one runtime assert disappeared:
`AdpcmStream.pad2C`, which no longer exists under `ICO_HOST`).

Docs: BUILD_STATUS.md, BUILDING.md, TESTING.md, PLATFORM.md, LAYOUT.md carry
the retirement note (commit 36a1d73e).

## Part 2: the raw-offset pass

Method. `grep -a -nE` on the source finds forms but not whether the host
compiles them: much of `ico2/` keeps the EE spelling in an `#else` branch.
So the pass preprocessed every `ico2/` TU with the `linux-x64` flags
(`-E`, 227 files) and searched the expanded text, which lists only what the
host actually compiles, with the file and line of the macro use. Patterns:

- `+ 0x15C`, `+ 0x164`, `+ 0x54` (and 348, 356, 84);
- `(char *)x + 8`, `+ 0xC` and other small literals on byte views;
- `*(T *)(x + N)` and `((T *)x)[n]` on non-vector records;
- `... + i * N` byte strides over tables (N not a power of two small
  enough to be a matrix or vector);
- `*(T *)x = template` copies through a cast;
- `->act`, `->work`, `ActWork` plus a literal.

The expanded scan found nine host-active sites the earlier sweeps and the
plain `grep -a` had not (the three "act" ones were `ICO_RAW(...)` whose
`+ 0x4B0` and `+ 0x680` sat outside the macro, so the host kept the EE
offset). Each is fixed with a named field, `ICO_RAW` or `ICO_RAWP`
(the EE form is the original expression).

### Fixed

| file | site | fault on x64 | now |
| --- | --- | --- | --- |
| `fumi/src/act-game.c` | `ORQ` / `ORM` (87 uses in `ActOrientTest`): `(char *)act + 0x478` | Act's wish words are at 0x548 on x64 | host macros index `&((Act *)s)->wish0` |
| `fumi/src/act-game.c` | `ACTWORK(self) + 2064/2112/2160` (mails 263 to 265) | `ActWork.effRec` is not at 0x810 (ActWork widens from 0x374) | `ICO_RAWP` to `&effRec[0..2]` |
| `fumi/src/act-game.c` | flyer mail 298: `*(U64ag *)addData = *(U64ag *)(&w1 + 0x80)` and the word at +8 | copies the middle of `ClipWork.filter` (a `WallCfg` is 24 bytes) into a record the reader takes as a `MotOriTarget` | host copies `w1.wall` into `MotOriTarget.wall` (`addData` is never assigned in the tree, so the site is dead today) |
| `fumi/src/boyact.c` | `SwapBoyWeapon`: `*(int *)(newW + 8)`, `oldW + 8` | GObj + 8 is `labelType` | `labelId` |
| `fumi/src/boyact.c` | `actBoyCliffHesitate` and `+0x8C0` twice: `BOY_WALL(self) + 0x8D0 / 0x8C0` | ActWork offsets of `intrReq.b.wall` and the vector after `intrReq` | `&intrReq.b.wall`, `pad8D0` |
| `fumi/src/girl_act.c` | `actGirlStart`: `p + 0x180, 0x184, 0xD0, 0xD4, 0x350, 0x1E0, 0x48` on the Act | writes 8-byte pointers into 4-byte EE slots and ints into other fields | `heldItem.i`, `nextItem.i`, `mainMail`, `mail`, `wayMode`, `life`, `actKind` |
| `fumi/src/girl_act.c` | two reads of the boy's work at +0x448 (the stop-frame word) | wrong field | `stopFrames` |
| `fumi/src/girl_brain_attract.c.inc` | `*(int *)(tgt.obj + 0xC) == 32` (third site of the 2I `kind` bug) | GObj + 0xC is `labelId` | `kind` |
| `fumi/src/commonact.c` | `actMotDirToWall`: `act + 0x4B0` after an `ICO_RAW` act load | Act + 0x4B0 is not `env` | `&GOBJ_ACT(self)->env` |
| `fumi/src/commonact.c` | `actCommonStone`: `*(*(act + 0x680) + 0x2A0) = 0` | Act + 0x680 is not `enemy` | `enemy->stonePair` |
| `fumi/src/commonact.c` | debug overlay (`debug_font_flag`): carrier's `+0x15C` / `+0x4A0` and `+0x164` / `+0x34` | EE offsets | `ctrl.motion`, `actMode` |
| `sugipon/src/enemy.c` | `EnemySetfDisappear`: `*(char **)(sub + 12)` | Sub15C + 12 is the high half of `parent` on x64, not `nodeMtx` | `nodeMtx` |
| `sugipon/src/worm.c` | `SetWormReduceRatio`: `work + 8` | the `src` pointer of `WormWork` was overwritten with a float | `&WormWork.reduce` |

EE identity: see "Results" (all 223 TUs, because `typedef.h` changed).

### Looked at and left

Frozen records read by their disc layout (the EE offsets are right on every
host; they were not re-verified against the layout asserts one by one):

- `fumi/src/fieldCollision.c`: collision data rows (`j * 0x50`, `j * 0x70`,
  `+0x44`, `+0x48`, `+0x54`, `+0x60`), reached through `ico_eeptr`; the
  debug draw at 1534 is among them.
- `omori/src/camera-ico2.c`, `camera-editor.c`: `CamGroup` 76 and `PinRec` 92
  byte strides (`_Static_assert`ed, class overlay); `camera-editor.c:1279`
  is 2I's debug pool.
- `sugipon/src/motionManager2.c`, `motionFileManager.c`,
  `motMan_getFinalMatrix.c.inc`, `motionManager.c`: motion file elements
  (`* 32`, `+ 4`, `+ 8`) through `ico_eeptr` and `ico_eew`.
- `sugipon/src/clothAnimation.c` (`n * 80` collision rows, `* 48` points),
  `particleEffect.c` (`* 160` `PEPackage`, no pointers), `flag.c`,
  `rope.c`, `pool.c` (`matrixptr + 0x80...`, float matrices in the
  scratchpad), `enemy.c` (`vtx + cnt * 32`, vertex records),
  `common/src/gamesys.c` (`gameSysObjInfo` rows of 0x40, written to the save).
- `fumi/src/boyact.c` `MakeCharacterPacket` (`pkt + 0x8...`): ints of
  `BoyKidnapWork`, a save record.
- Vector, matrix and quaternion component writes
  (`*(float *)((char *)out + 0xC)` and friends, `nodeMtx + n * 64 + 0x30`):
  16-byte float groups, the same on every host.
- Template copies through a cast checked and fine: `boyInfoDefault` (the
  first 0x18 bytes only), `wayStepClear`, `characterPacketDefault`,
  `initialRotElem`, `initialBlendRot`, `initialGeoState`, `jointMtxInit`,
  `defaultPackage`, `cameraTargetDefault`, `handClInfoClear`: destination
  and template have the same scalar layout. `motionManager2.c` (2I) is the
  one that did not.

Not touched because it is dead:

- `common/src/sceneManager.c:126` `GetRealModelId`: `gen + 0x46`, `+ 0x2C`
  over a `GenGeo` row; static and never called.
- `fumi/src/act.c:118` `actSetInterrupt`: `*(int *)(self + 0)`; no caller.

### Remaining

- The pass is a text search over what the host preprocessor exposes. It
  finds literal offsets and strides. It cannot find a view whose offset sits
  in a named constant, a `#define` the TU never expands, or a table indexed
  by a computed byte offset (`objLayout[... * 76 + 70]` was 2I's find by
  eye). The 3000-tick run does not exercise these paths:
  `ActOrientTest` (87 sites), `actGirlStart`'s tail, `SetWormReduceRatio`,
  the carry and stone actions run later in the game or only on some stages.
  Nothing here was run past tick 3000 except the bounded run below.
- Several sites above (the `ORQ` / `ORM` words, `girl_act.c:3701`'s
  `stopFrames`) are logic reads, so a trace that was identical to the
  32-bit build to tick 3000 says nothing about them past where they first run.
- ActWork's first divergence is at 0x374 (`floorObj`), Act's at 0x4, Sub15C's
  at 0x4 (`ObjNode parent`) and GObj's at 0x4. Any further raw access to
  those four records is wrong on x64. A runtime check would help more than
  more grep: an `ICO_HOST` build of `ICO_RAW`'s EE form with an
  `_Static_assert(offsetof(...) == off)` per use would turn every remaining
  one into a compile error.
- `port/rhi/CMakeLists.txt` and `tools/fetch_deps.sh` still mention the
  i686 SDL3 prefix (not this package's files).

## Results

Build trees. The working tree was not buildable while this package ran
(another package's in-progress `port/audio/sg/sound.c` needs headers that
are not there yet, and `HEAD` 10b23d84 lists `port/null/snd_null.c` in its
CMake but no longer carries the file). So the four presets were built from a
worktree of `HEAD` (`build-host/2j-src`) with this package's files copied in
and `snd_null.c` restored from `36a1d73e`. The only differences from the
tree are other packages' uncommitted files.

| preset | build dir | result |
| --- | --- | --- |
| `linux-x64` | `build-host/2j-linux-x64` | builds; `ctest` 33 of 33 pass |
| `linux-x64-clang` | `build-host/2j-linux-x64-clang` | builds |
| `win-x64` | `build-host/2j-win-x64` | builds |
| `asan` | `build-host/2j-asan` | builds; `ctest` 28 of 33 |

`asan` failures, none from this package: `diag` (the test's deliberate null
store is a UBSan error under `-fno-sanitize-recover`; the test never reaches
its SIGSEGV), and `rd_layout`, `rd_tex`, `rd_gsbase`, `rd_mesh` (UBSan:
`union GifPkWord` accessed at a 4-byte-aligned address in
`seki/src/GifPacket.c:118` and `MicroCode.c:79`, the renderer package's
files).

The first `linux-x64` `ctest` had `ios_chain` segfaulting (`asan`: a
global-buffer-overflow). With the skip gone it ran on x64 for the first time
and its own buffers were wrong: `schedBuf` was `int[8]` and `msg` `int[4]`
for a queue of pointer-wide `IosMsgWord`s. Both are `IosMsgWord` now; the
event order string is the expected one on both presets.

`tools/format.sh --check` passes. `tools/gen_layout_asserts.py --check` is
current. `memory` passes on `linux-x64` with the 0x50 / 0x70 layout.

EE identity: `tools/compile_c.sh` (ee-gcc 2.9-991111, fetched into the
scratchpad because `tools/cc` pointed at an empty directory) on all 223
`ico2/` C sources of `config/link_order.pal.txt`, working tree against a
worktree of `36a1d73e`: the `.text`, `.data`, `.rodata`, `.sdata`, `.bss`,
`.sbss`, `.lit4`, `.lit8` and relocations of all 223 are identical.

Bounded game run (`linux-x64` with `ICO_LINK_EXE=ON`, headless,
`ticks=3000`, `pad-script.txt` = `port/input/pad-boot.txt`, ISO
`baserom/Ico_PAL.iso`, `timeout 600`): exit 0 in 1.2 s, 3000 Main ticks,
6007 vsyncs, stage 1 -> 41 at tick 663, -> 42, -> 43, -> 45, -> 40, -> 3 at
tick 1036. Its trace is byte-identical (`cmp`) to package 2I's
(`build-host/2i-linux-x64/logs/trace-20261005-055307.txt`). The fixes above
are therefore off the 3000-tick path or do not change a traced value.
