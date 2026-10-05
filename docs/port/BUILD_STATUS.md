# Host build status

What the host build (`CMakeLists.txt`, `docs/BUILDING.md` "Host build")
compiles, per preset, and why the rest does not. Measured 2026-10-05 on the
working tree with package 0B's front-end fixes in progress; rerun
`cmake --preset <p> -DICO_BUILD_BLOCKED=ON` and build with `-k 0` to
recheck, and delete entries from `cmake/IcoExclusions.cmake` as they compile.

## Counts

`config/link_order.pal.txt` lists 223 C sources under `ico2/`. The host
build takes 212 of them: since package 1D the headless build compiles the
renderer layer too (docs/port/HEADLESS_STUBS.md) and leaves out only 11
(`common/src/debug.c`, `debug_exception.c`, the 9 `ito/mpeg` files). All 73
data tables compile on every preset. The table below is the pre-1D count of
non-renderer sources.

| preset | compiler | game TUs compiled | blocked | data tables |
| --- | --- | --- | --- | --- |
| `ref-m32` | host gcc 14 `-m32` | 188 / 188 | 0 | 73 |
| `win-x86-ref` | mingw-w64 gcc 14 (i686) | 188 / 188 | 0 | 73 |
| `linux-x64` | host gcc 14 | 188 / 188 | 0 | 73 |
| `win-x64` | mingw-w64 gcc 14 (x86-64) | 188 / 188 | 0 | 73 |
| `asan` | host gcc 14 | 188 / 188 | 0 | 73 |
| `fptrap` | host gcc 14 | 188 / 188 | 0 | 73 |
| `ref-m32-clang` | llvm-mingw clang 23 | 188 / 188 | 0 | 73 |
| `win-x86-ref-clang` | llvm-mingw clang 23 | 188 / 188 | 0 | 73 |
| `linux-x64-clang` | llvm-mingw clang 23 | 188 / 188 | 0 | 73 |
| `win-x64-clang` | llvm-mingw clang 23 | 188 / 188 | 0 | 73 |

Every preset configures and builds with exit status 0 (blocked sources
excluded). `fpenv_test` passes on the Linux presets; the Windows `.exe`s are
built but not run here. After package 1A (2026-10-05, measured on
`linux-x64`, `ref-m32`, `win-x86-ref`, `win-x64`, `linux-x64-clang` and
`win-x64-clang`; the other presets share those compilers and were not
rebuilt): no game source is blocked on any compiler, and `math_test` and
`newlib_test` pass on the Linux presets (docs/port/MATH.md).

## Compiler choice: GCC primary, clang later

The game had GNU C nested functions (package 0C counted 158 in 32 files),
which gcc compiles and clang does not implement. Package 0E rewrote them
(`docs/port/SWEEP_0E.md`), and package 1A the VU0 asm and the one nested
function left in a non-renderer source (`motionManager2.c`), so clang and
gcc now compile the same 188 sources. The four main presets still use gcc
14 and the clang family stays as `*-clang` presets until that choice is
revisited; the renderer-owned package 1D rewrote the nested functions of `clothAnimation.c`, `lineManager.c`
and `quaternion.c`, so no game source has one left.

Trade-off: the plan wanted clang everywhere (one compiler for Linux and
Windows, and later macOS, iOS and Android, where Apple's clang is the only
choice). Nested functions and VU0 asm no longer block that in the
non-renderer sources. The clang presets also have no Linux sanitizer runtimes
(llvm-mingw ships none), so `asan` is gcc either way.

Argument evaluation order (package 0C): ee-gcc, gcc and clang on GNU and
mingw targets evaluate call arguments left to right on x86; clang targeting
MSVC (clang-cl, `*-windows-msvc`) goes right to left. The Windows builds
must stay mingw (gcc or llvm-mingw), never MSVC-targeted.

## Blocked sources (`cmake/IcoExclusions.cmake`)

### Every compiler (0)

None. The VU0 entries (`itou_sub.c`, `BgAnimation.c`, `stormTest.c`,
`motionManager2.c` and the 14 TUs that call `sugiCommon.h`'s helpers) left
the list with package 1A: `sugiCommon.h`'s `plane_distance`,
`distance_squared`, `distance_squared_b` and `distance_squared_xz` and every
other VU0 asm site in 1A's files have `ICO_HOST` bodies in C over
`port/math` (docs/port/MATH.md). Any VU0 or R5900 asm wrapper still used in
a host TU (`VU0_*`, `QCOPY16`) is now a compile error (`typedef.h`).

### Clang only (0)

None: the 13 clang-only sugiCommon.h TUs compile, and `motionManager2.c`'s
nested function has an `ICO_HOST` file-scope version.

### Renderer-owned, not compiled while `ICO_HEADLESS` (11)

`common/src/debug.c`, `common/src/debug_exception.c` (VU0 asm in the debug
font, bar and exception screen; `port/null/debug_null.c` stands in) and
`ito/mpeg/*.c` (9; the FMV player, `movie_*` stubbed in
`port/null/gfx_null.c`). The other 24 renderer-owned files (all of
`seki/src`, `sugipon/src/{staticBlur,darkVolume,particleEffect,matrixDrive,
quaternion,clothAnimation,lineManager}.c`, `ito/src/{lightning,
queen_barrier_disp}.c`) are compiled by the headless build as well
(`HEADLESS_SIM` in `tools/gen_sources.py`); the renderer waves still own
them. Package 1D gave the VU0 asm left in `GsBase.c`, `GifPacket.c`,
`darkVolume.c` and `lightning.c` `ICO_HOST` bodies.

## Warnings

While `ICO_STRICT_WARNINGS` is off, the C89-era diagnostics are warnings.
On `win-x86-ref` (gcc), 2,262 warnings: 2,128 `-Wstrict-prototypes`, 57
`-Wint-conversion`, 52 `-Wincompatible-pointer-types`, 7
`-Wbuiltin-declaration-mismatch` (libc functions redeclared with newlib or
K&R prototypes), 4 `-Wimplicit-function-declaration`, 1 `-Wreturn-type`.
`win-x64` adds the 64-bit findings for Phase 2: 2,748
`-Wint-to-pointer-cast` and 247 `-Wpointer-to-int-cast` (5,257 in all).
`-Wreturn-type -Wimplicit-function-declaration -Wstrict-prototypes` can
become errors (`ICO_STRICT_WARNINGS=ON`) once those three counts reach zero.

## Linking

`ico_pc` (`-DICO_LINK_EXE=ON`) links on every preset checked (package 1D,
2026-10-05, gcc 14 / mingw-w64 gcc 14 / llvm-mingw clang 23), with no
unresolved symbol:

| preset | `ico_pc` | notes |
| --- | --- | --- |
| `win-x86-ref` | links | the user's test build: GUI subsystem (`-mwindows`), `-static`, imports only system DLLs |
| `ref-m32` | links | |
| `linux-x64` | links | |
| `win-x64` | links | expected unreliable until Phase 2 (32-bit struct and pointer-in-int assumptions) |
| `linux-x64-clang` | links | clang compiles every game source |
| `asan` | builds | unit tests pass except another package's `rhi_vk` (a LeakSanitizer report inside llvmpipe) |

The game links against `ico_game` (212 game objects and the 73 tables),
`ico_platform`, `ico_math`, `ico_port_data`, `ico_port_null` and the
headless floor in the program itself (`port/null/gfx_null.c`: 13 hardware
and FMV stubs; `port/null/debug_null.c`: 27 functions and 59 variables of
`debug.c`). docs/port/HEADLESS_STUBS.md lists each and why. Nobody has run
it in the container (plan rule); the first run is the user's Phase 1
checkpoint (docs/port/TESTING.md).

Package 1C's libraries (`docs/port/DATA.md`): `ico_port_data`
(`port/data/`: the disc VFS and ISO9660 reader, libcdvd over it, sifrpc and
sifdev with no IOP, IOP RAM) and `ico_port_null` (`port/null/{pad,mc,snd,
scf}_null.c`, and package 1D's `port/input/pad_script.c`).

## Phase 1 prerequisites

- Done (1D): `stageTable`, `motionLimitDef` and `seDef` are written at run
  time; the host build defines them non-const (`gen_data_c.py --writable`,
  `ICO_DATA_WRITABLE` in `CMakeLists.txt`). The other 43 `.rodata` tables
  were checked and are only read.
- `-malign-double` applies to game and data TUs only on the 32-bit presets,
  `-mno-ms-bitfields` to game and data TUs only on Windows. Any record that
  game code and `port/` code both read (SDK parameter blocks, the pad
  buffer, card directory entries) must have a layout that does not depend on
  those options, or port/ must compile its users with them too.
- `port/compat/eeregs.h` maps the EE registers to plain memory, so code
  that polls one (`GS_CSR`, DMA `CHCR` busy bits) never sees it change; the
  owners of those loops replace them.
- `port/compat/assert.h` makes the game's `__assert` call
  `ico_assert` (`port/platform/assert_host.c`); no configuration defines
  `NDEBUG`.
- The simulation's round-toward-zero MXCSR mode also applies to `double`
  arithmetic, which the EE did in software with round-to-nearest (0C's
  double-site census decides how that is handled).
