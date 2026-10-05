# Host build status

What the host build (`CMakeLists.txt`, `docs/BUILDING.md` "Host build")
compiles, per preset, and why the rest does not. Measured 2026-10-05 on the
working tree with package 0B's front-end fixes in progress; rerun
`cmake --preset <p> -DICO_BUILD_BLOCKED=ON` and build with `-k 0` to
recheck, and delete entries from `cmake/IcoExclusions.cmake` as they compile.

## Counts

`config/link_order.pal.txt` lists 223 C sources under `ico2/`. The host
build takes 188 of them: 35 are renderer-owned (below) and left out while
`ICO_HEADLESS` is on. All 73 data tables compile on every preset.

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
revisited; the renderer-owned `clothAnimation.c`, `lineManager.c` and
`quaternion.c` still have nested functions (gcc only).

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

### Renderer-owned, not compiled while `ICO_HEADLESS` (35)

`seki/src/{GsBase,GifPacket,DmaPacket,DisplayList,DisplayFont,RegistPacket,
Packet,MicroCode,Texture,Shadow,ZFog,Primitive,Light,DisplayP2O,Matrix}.c`,
`ito/mpeg/*.c` (9), `sugipon/src/{staticBlur,darkVolume,particleEffect,
matrixDrive,quaternion,clothAnimation,lineManager}.c`,
`ito/src/{lightning,queen_barrier_disp}.c`, `common/src/debug.c`,
`common/src/debug_exception.c`. Their owners are the renderer waves and 1A
(`Matrix.c`, `matrixDrive.c`, `quaternion.c`, `clothAnimation.c`). The VU0
routines of `Matrix.c`, `matrixDrive.c`, `quaternion.c` and
`clothAnimation.c` are in `port/math` (library `ico_math`, linked into
`ico_pc`), so a headless build has them; the plain C functions of those
files are not built until the files leave this list. All of 1A's
renderer-owned files (also `Shadow.c` and `lineManager.c`) compile with gcc
and the game flags.

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

`ico_game` (static library of the compiled objects plus the data tables) and
`ico_platform` build; the `ico_pc` executable (`ICO_LINK_EXE=ON`) does not
link yet. In an earlier clang x86-64 build (138 TUs), 992 symbols were undefined across the game
objects: the SDK entry points (`sce*`, `Sg*`, the kernel calls), the
renderer-owned and blocked sources' functions, `_gp`, and the C library
functions `port/compat/ico_libc.h` lists. Phase 1 (1A, 1B, 1C) supplies them.

Package 1C's libraries (`docs/port/DATA.md`): `ico_port_data`
(`port/data/`: the disc VFS and ISO9660 reader, libcdvd over it, sifrpc and
sifdev with no IOP, IOP RAM) and `ico_port_null` (`port/null/{pad,mc,snd,
scf}_null.c`). Together they define every `sceCd*`, `sceSif*`, `scePad*`,
`sceMc*`, `sceScf*`, `Sg*` and sifdev symbol `ico_game` references (checked
with `nm` on `linux-x64`, 2026-10-05); `ico_pc` links both when
`ICO_LINK_EXE` is on. No source left the blocked list for this: `cdvd.c`,
`FileManager.c`, `pad.c` and `mcard.c` already compiled, and only
`FileManager.c` changed (`ICO_HOST` skips the IOP reboot and IRX loads).
Tests: `vfs_synthetic`, `vfs_disc` (the user's ISO, skipped when absent)
and `null_devices`.

## Phase 1 prerequisites

- `stageTable` and `motionLimitDef` are generated `const` by
  `tools/gen_data_c.py` but the game writes them at run time; on the host
  the writes would fault (read-only data). Package 0C found it; a Phase 1
  package owns the fix.
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
