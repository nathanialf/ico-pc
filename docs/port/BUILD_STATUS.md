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
| `ref-m32` | host gcc 14 `-m32` | 169 / 188 | 19 | 73 |
| `win-x86-ref` | mingw-w64 gcc 14 (i686) | 169 / 188 | 19 | 73 |
| `linux-x64` | host gcc 14 | 169 / 188 | 19 | 73 |
| `win-x64` | mingw-w64 gcc 14 (x86-64) | 169 / 188 | 19 | 73 |
| `asan` | host gcc 14 | 169 / 188 | 19 | 73 |
| `fptrap` | host gcc 14 | 169 / 188 | 19 | 73 |
| `ref-m32-clang` | llvm-mingw clang 23 | 156 / 188 | 32 | 73 |
| `win-x86-ref-clang` | llvm-mingw clang 23 | 156 / 188 | 32 | 73 |
| `linux-x64-clang` | llvm-mingw clang 23 | 156 / 188 | 32 | 73 |
| `win-x64-clang` | llvm-mingw clang 23 | 156 / 188 | 32 | 73 |

Every preset configures and builds with exit status 0 (blocked sources
excluded). `fpenv_test` passes on the Linux presets; the Windows `.exe`s are
built but not run here.

## Compiler choice: GCC primary, clang later

The game had GNU C nested functions (package 0C counted 158 in 32 files),
which gcc compiles and clang does not implement. Package 0E rewrote them
(`docs/port/SWEEP_0E.md`); the remaining clang gap is sugiCommon.h's VU0
asm (13 more TUs than gcc). Until that lands, the four main presets use
gcc 14 and the clang family stays as `*-clang` presets.

Trade-off: the plan wanted clang everywhere (one compiler for Linux and
Windows, and later macOS, iOS and Android, where Apple's clang is the only
choice). Nested functions no longer block that in the 0E files; the
remaining blocker is the VU0 asm in sugiCommon.h and the 1A files. The clang presets also have no Linux sanitizer runtimes
(llvm-mingw ships none), so `asan` is gcc either way.

Argument evaluation order (package 0C): ee-gcc, gcc and clang on GNU and
mingw targets evaluate call arguments left to right on x86; clang targeting
MSVC (clang-cl, `*-windows-msvc`) goes right to left. The Windows builds
must stay mingw (gcc or llvm-mingw), never MSVC-targeted.

## Blocked sources (`cmake/IcoExclusions.cmake`)

### Every compiler (19)

| source | reason | owner |
| --- | --- | --- |
| `fumi/ios/memory.c` | `__asm__("break")` x4 (EE debug trap) | 0B |
| `ito/src/itou_sub.c` | VU0 macro inline asm (`.set noreorder`, VU0 registers) | 1A |
| `seki/src/BgAnimation.c` | VU0 inline asm (`$2` clobber) | 1A |
| `sugipon/src/stormTest.c` | VU0 inline asm (`lqc2`) | 1A |
| `sugipon/src/motionManager2.c` | VU0 inline asm of its own and sugiCommon.h's | 1A |
| `fumi/src/commonact.c`, `fumi/src/fieldCollision.c`, `sugipon/src/{a_p_1,act_a_p_1,box,enemy,geometryManager,girlForceField,item,motionManager,spider,torch,weapon,windField}.c` | call sugiCommon.h's VU0 helpers | 1A |

`sugipon/include/sugiCommon.h` defines `plane_distance`,
`distance_squared`, `distance_squared_b` and `distance_squared_xz` as
`static __inline__` functions whose bodies are VU0 macro-mode assembly
(`lqc2`, `vmul`, `qmfc2`, `mtc1` into an `"=f"` output). gcc only rejects
them where they are called; clang rejects them in every TU that includes the
header. Converting these four functions to C (1A's transpiler, or by hand
over `port/math`) unblocks 14 of the 21 under gcc and the 10 clang-only
sugiCommon.h TUs listed below.

`s_init.c` and `warpGirl.c` now use `ICO_BREAK()` (`typedef.h`) and compile;
`motionManager.c` includes `GifPacket.h` before first use (package 0E). Both
no longer appear in the blocked list.

### Clang only (13 more)

The GNU nested functions are gone from every game source except
`sugipon/{clothAnimation,lineManager,quaternion,motionManager2}.c` (package
1A, `motionManager2.c` is still listed above); package 0E turned them into
file-scope statics.

sugiCommon.h's asm in a TU that never calls it (fixed with sugiCommon.h,
1A): `fumi/src/enemy_act.c`, `ito/src/{act_bird,itou_boss}.c`,
`omori/src/enemy-control.c`, `script/src/script.c`, `sugipon/src/{boy,
enemyParts,frameDependSequence,motionViewer,pool,rope,waterDot,windManager}.c`.

### Renderer-owned, not compiled while `ICO_HEADLESS` (35)

`seki/src/{GsBase,GifPacket,DmaPacket,DisplayList,DisplayFont,RegistPacket,
Packet,MicroCode,Texture,Shadow,ZFog,Primitive,Light,DisplayP2O,Matrix}.c`,
`ito/mpeg/*.c` (9), `sugipon/src/{staticBlur,darkVolume,particleEffect,
matrixDrive,quaternion,clothAnimation,lineManager}.c`,
`ito/src/{lightning,queen_barrier_disp}.c`, `common/src/debug.c`,
`common/src/debug_exception.c`. Their owners are the renderer waves and 1A
(`Matrix.c`, `matrixDrive.c`, `quaternion.c`, `clothAnimation.c`).

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
