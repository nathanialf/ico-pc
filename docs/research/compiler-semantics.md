# R3: ee-gcc 2.9 vs modern compilers, semantic audit

Research note for work package 0C. It covers behaviour the game's C relies
on that changes when the compiler changes from ee-gcc 2.9-991111 (MIPS
R5900, EABI) to clang (or gcc) on x86, x86-64 and later AArch64. Owners of
the recommendations: 0A (flags, toolchain), 0B (source fixes), 2B (layout
asserts). Line numbers refer to commit `d9e456d4`. The working tree was
being edited by other packages while this was written, so the analysis ran
on a `git archive HEAD` export.

## Method

- **Period compiler.** Small probes compiled with the real
  `tools/cc/ee-gcc2.9-991111/ee-gcc` (`-O2 -G 0 -mips3 -EL`, through
  `tools/period_env.sh`). They show argument evaluation order, initializer
  order, type sizes, bitfield and `long long` layout, char signedness,
  float conversions and shift codegen.
- **Host compilers.** The same probes with Debian gcc 14.2 (`-m32` and
  x86-64) and with the clang 23.1.2 in `tools/toolchain/llvm-mingw` for the
  targets `i386-linux-gnu`, `x86_64-linux-gnu`, `i686-w64-mingw32`,
  `x86_64-w64-mingw32`, `i686-pc-windows-msvc`, `x86_64-pc-windows-msvc`,
  `mipsel-linux-gnu` and `aarch64-linux-gnu` (`-S -emit-llvm`).
- **Whole-tree passes over the 223 `ico2/**/*.c` files.** Include
  directories are those of `tools/compile_c.sh:139-155` (own directory,
  then sugipon, omori, common, ito, fumi, seki, script, then the 14 `sce/`
  archive directories, `-nostdinc`).
  1. `gcc -m32 -S -o /dev/null -O2 -Wall -Wextra` with pointer and int
     conversion errors demoted to warnings. This pass is needed for
     `-Wmaybe-uninitialized`, which only runs in the optimiser. **33 files
     fail** (list below), so they get no optimiser warnings.
  2. `gcc -m32 -fsyntax-only -Wtype-limits -Wchar-subscripts`, once with
     `-funsigned-char` and once with `-fsigned-char`. A plain `char`
     compared with `< 0` warns under the first, and a `char` compared with a
     constant above 127 warns under the second. I checked the technique on a
     probe file first.
  3. `clang --target=mips64el-linux-gnuabin32 -fsyntax-only -std=gnu89
     -ferror-limit=0` with `-Wuninitialized -Wsometimes-uninitialized
     -Wconditional-uninitialized -Wunsequenced -Wchar-subscripts
     -Wshift-count-overflow -Warray-bounds`. Clang's uninitialised analysis
     runs in the front end, and a MIPS target accepts the `$n` register
     names in the asm clobbers, so this covers files that pass 1 cannot.
  4. A Python scan (scratchpad `evalorder.py`, `eval2.py`, `multicall.py`)
     for full expressions containing two or more calls to random-number or
     pad functions. The function set is closed transitively over callers
     (895 functions after closure; deliberately conservative). The scan
     also counts calls with two or more call arguments.
- The 228 period objects in `build/ico2/` (`mips-linux-gnu-objdump -dr`)
  as ground truth for what ee-gcc emitted.

Files that fail pass 1 (MIPS register names in asm clobbers, `mode(TI)`,
`unable to emulate`, lvalue casts, two-line string literals): common/src
`debug.c debug_exception.c gamesys.c layout_texture.c`; fumi/src
`commonact.c enemy_act.c fieldCollision.c way_util.c`; seki/src
`BgAnimation.c EnemyInit.c Matrix.c Packet.c Primitive.c RegistPacket.c
Shadow.c Texture.c`; sugipon/src `a_p_1.c act_a_p_1.c box.c
clothAnimation.c darkVolume.c enemy.c geometryManager.c girlForceField.c
item.c matrixDrive.c motionManager.c motionManager2.c quaternion.c spider.c
torch.c weapon.c windField.c`. Rerun pass 1 on these after 0B and 1A.

## Findings

### 1. Type sizes and ABI facts of the EE (measured on ee-gcc)

| type | EE (ee-gcc 2.9) | i386 SysV | i686 mingw | x86-64 SysV | x86-64 mingw |
| --- | --- | --- | --- | --- | --- |
| `char` signed | yes | yes | yes | yes | yes |
| `long` | **8** | 4 | 4 | 8 | 4 |
| pointer | 4 | 4 | 4 | 8 | 8 |
| `long long`, `double` alignment in a struct | 8 | **4** | 8 | 8 | 8 |
| `long double` | 8 | 12 | 12 | 16 | 16 |
| `enum` | 4 | 4 | 4 | 4 | 4 |
| bitfields of mixed types share storage | yes | yes | **no (MS layout)** | yes | **no (MS layout)** |

Probe struct sizes: `{char; int:3}` is 4 on EE, i386 and x86-64 SysV, and
8 on all mingw and msvc targets. `{int; long long}` is 16 on EE and 12 on
i386 SysV. `-mno-ms-bitfields` on the mingw targets and `-malign-double`
on i386 SysV both bring the layout back to the EE's (all probes equal).

### 2. Argument and initializer evaluation order

Probe: `g(r(), r(), r())`, `h(ri(), ri(), ri())`,
`V v = { r(), r(), r(), 1 }` and `ri() - ri()`, with `r` returning a counter.

| compiler / target | call arguments | initializer list | binary operands |
| --- | --- | --- | --- |
| ee-gcc 2.9 MIPS | **left to right** | left to right | left first |
| clang, `*-linux-gnu`, `*-w64-mingw32`, aarch64 | left to right | left to right | left first |
| clang, `*-pc-windows-msvc` (clang-cl ABI) | **right to left** | left to right | left first |
| gcc 14, x86-64 and `-m32` | **right to left** | left to right | left first |

C leaves all of these unspecified (C11 6.5.2.2p10 for arguments, 6.7.9p23
for initializers), so the source has no say in it.

Sites that call the RNG more than once in one full expression (pass 4,
transitive):

| site | form | order matters |
| --- | --- | --- |
| `sugipon/src/motionViewer.c:507` | `MatrixDrive_ScaleMatrix(random_unit()*3+1, ×3)` | yes (arguments); debug viewer only |
| `sugipon/src/clothAnimation.c:610-612` | `VECTOR r = {pw*(rand()...), ×3, 0}` | yes (initializer) |
| `sugipon/src/waterDot.c:79` | `VECTOR v = {random_signed_b()*range, ×3, 1}` | yes (initializer) |
| `sugipon/src/worm.c:266-267` | `WormVec v = {len*(_GetRandom()*2-1), ×3, 0}` | yes (initializer) |
| `sugipon/src/worm.c:354` | `{ini->pos[i] + (_GetRandom()*2-1)*50, ×3, 1}` | yes (initializer) |
| `ito/src/act_bird.c:175`, `ito/src/lightning.c:386,392` | `f(g(...))` nesting | no (inner call is sequenced first) |
| `sugipon/src/frameDependSequence.c:261` | `execEff(tbl[... crt_random_unit()], entry)` | no (only one RNG call) |

No statement calls `iosPadRead` twice. Only one unsequenced `++`/`+=`
remains (`fumi/src/way_sys.c:327`, below). 281 calls in the tree pass two
or more call expressions as arguments (scratchpad `multicall.txt`). Most
are pure getters (`test_CURRENTROOT`, `MatrixDrive_GetMatrix`,
`GOBJ_*` macros), but nobody has proved all of them free of side effects.

### 3. Char signedness

Plain `char` is signed on the EE (probe: `(char)-1 < 0` is 1), and also on
x86, x86-64, Windows and Apple arm64. It is **unsigned** on AArch64 Linux
and Android. Pass 2 found **no** plain-`char` comparison against a negative
value or a constant above 127, and no plain-`char` array subscript, in the
190 files that compile. A grep over the 33 that do not found none either.
The tree has 714 plain-`char` scalar or array declarations, 613
`unsigned char` and 36 explicit `signed char`. The remaining risk is a
silent sign extension when a plain `char` holding a byte above 0x7F (EUC-JP
text, packed data) is widened to `int`. No compiler warning catches that.

### 4. Bitfields and 64-bit fields

189 bitfield members in 24 files. Most groups use a single base type
(`motionOrientManager.h:86-128` `unsigned int`, `StageAnimation.c:41-44`
`int`, `DisplayP2O.h:105-111` `unsigned long long`,
`DisplayP2O.h:160-162` `unsigned short`). Mixed-type neighbours exist
(`motionManager2.c:1248-1249` `signed char` then `unsigned char`;
`enemy.h:32-34` `int` then `unsigned int`). Under MS layout a change of
declared type starts a new storage unit, so any such pair changes size.
GCC/SysV allocation (LSB-first, packing across types) matches the EE on
every little-endian target once MS layout is off.

Structs with 64-bit members: 356 member declarations of `long long`,
`long`, `double` or a 128-bit type. Most are 64- or 128-bit views in unions
used for doubleword copies (`typedef.h:131,199,872,1055,...`, `flag.h:36`,
`sugiCommon.h:128`). Disc-overlay candidates with 64-bit fields:
`PObjMaterial` (`DisplayP2O.h:97`, union at 0x60) and `PObjModel`
(`DisplayP2O.h:146`, union at 0x30). Both are at 8-aligned offsets, so
field offsets agree, but `sizeof` and the stride of any array of them
depend on the 8-byte alignment. That is for R4 and 2B to pin with
`_Static_assert`.

The EE's `long` is 64-bit (probe `sizeof(long) == 8`), which turns these
into **semantic** differences rather than layout ones:

- `sugipon/src/darkVolume.c:84,91,93,127,171` (`(long)0xFE00 << 46` and
  similar), `ito/mpeg/mv_vibuf.c:46` (`(unsigned long)addr << 32`),
  `omori/src/camera-editor.c:214-216`. A 32-bit host `long` makes the shift
  undefined. These belong to 0B's `long` sweep (73 bare `long` uses).

### 5. Shifts the EE defined by accident

`fumi/src/commonact.c:2820`:
`act->flags18.ll = (act->flags18.ll & ~(1ULL << 57)) | (stuck << 57);` with
`int stuck`. ee-gcc compiles `stuck << 57` as `sll $5,$5,25`: a 32-bit
shift by 57 & 31, sign-extended to 64 bits (probe `sh.c`). With
`stuck == 1` the EE therefore **clears bit 57 and sets bit 25**. On x86
`shl` also masks the count to 5 bits, but the C is undefined, and clang
may fold it to anything. The port must spell out the EE result:
`((long long)(int)((unsigned)stuck << 25))`.

### 6. Reads past an object, relying on EE data layout

- `common/src/layout_action.c:1338-1358` (`gflagKeepState` and
  `gflagRestoreState`): loop `i < 20` over `static int keepFlagNo[5]`
  (`:149`). On the EE, elements 5 to 19 are the next statics in `.data`:
  `keyConfigCode[8]` (`:154`) and the first seven `keyConfigSlot[8]`
  (`:156`). So a load also saves and restores game flags 16, 128, 32, 64,
  8, 2, 1, 4 and the current key-config slot values (1, 2, 3, 4, 5, 0, 0 by
  default; the player can change them). The decomp comment at `:146-148`
  says this. gcc also flags it (`-Waggressive-loop-optimizations`,
  "iteration 5 invokes undefined behavior"), and at `-O2` gcc may cut the
  loop to five iterations. **Discrete-state hazard.** The port must keep
  the three arrays contiguous (one struct under `ICO_HOST`) and make the
  loop read from that struct.
- `ito/src/itou_boss.c:583`: `memset(gflag, 0, sizeof(gflag) +
  sizeof(capsule))` clears `static signed char gflag[16]` and the next
  static `CapsuleRec capsule[53]` (`:41-43`). The host must keep both in one
  struct, or the clear will hit unrelated memory.
- `fumi/src/fieldCollision.c:1563,1565`: `n[4] = 0.0f` and `out[4] = 0.0f`
  write one float past 4-element arrays ("the fourth store lands one float
  past n", decomp comment). On the EE that hits a neighbouring stack slot.
  The host should give both arrays 5 elements under `ICO_HOST`. Someone
  should also check whether the EE slot was live (it is not visible in the
  C).

### 7. Calls with missing arguments

`fumi/src/jimaku.c:382-386`: `jimakuEnd(JimakuArg *msg)` calls
`jimakuMgrEnd()` with no argument. `jimakuMgrEnd` is a K&R definition
(`:307`) that reads `p[0x4C/4]`. On the EE, `$a0` still holds `msg` at the
call (only `systemStatus[10] = 0` comes before it), so the callee gets
`msg`. The other call, at `:340`, passes `(int *)msg` explicitly. gcc
flags it ("'p' is used uninitialized"). Fix: pass `(int *)msg`. That is a
reconstruction relying on the EE ABI, so it goes upstream first under the
plan's rule. Other K&R definitions: `omori/src/chain.c:526`
(`pendulum_Process`, an `unsigned char` parameter promoted to int,
consistent with its prototype at `:32`, harmless) and
`fumi/sound/adpcm_init.c:288` (`AdpcmInterStereoVolumeSet` called with
two of its three arguments at `:480,482` and `script/src/script.c:1134`;
the decomp comment says the third, `vol`, is never read, so harmless). 32
`()` non-prototype declarations remain (for example `seki/src/Basic.c:6`
`extern void memcpy();`, `common/src/gamesys.c:268`). 0B should compile
with `-Wstrict-prototypes -Wdeprecated-non-prototype` and check the
argument count at each call.

### 8. Uninitialised reads

From passes 1 and 3, after removing reads that turned out to be assigned
through an out parameter or inline asm:

| site | what | EE behaviour | risk |
| --- | --- | --- | --- |
| `script/src/st13c.c:921,974` | `volatile int se;` never assigned; `soundSeDefStop(se)` | stops whatever handle the stale stack slot held | **medium**: sound state; host value differs |
| `fumi/src/jimaku.c:310` | see 7 | `msg` via `$a0` | **high** until fixed |
| `omori/src/camera-ico2.c:457-459` | `vDbg[3]` copied to `monitorCamera.dbgA` | stack garbage stored into persistent state | **medium** for the trace oracle: zero-initialise it or leave `dbgA` out of the trace hash |
| `fumi/ios/cdvd.c:576,607` | `kind` used by `debugCdvdLoadInfoSegAdd` when the name has no `.` | debug counter written at a garbage index (`common/src/debug.c:3178-3181`) | low (pack names carry extensions); init `kind = -1` and guard the debug add |
| `omori/src/brain.c:262-275` | `idx` set only if `b->cur` is found in `tgt[]` | register content | low (`cur` is drawn from `tgt`) |
| `ito/mpeg/mv_audiodec.c:167,272-273` | `p0/p1/n0/n1` | | none (replaced by `port/fmv`) |
| `common/src/debug_exception.c:622`, `seki/src/Light.c:551` | debug paths | | none |
| `sugipon/src/darkVolume.c:154`, `sugipon/src/motionManager2.c:393`, `fumi/src/act.c:606`, `fumi/ios/cdvd.c:348` | false positives (asm output, loop always runs, out parameter, guarded) | | none |

Functions that fall off the end and return an undefined value
(`-Wreturn-type`): `fumi/ios/memory.c:140` (`iosMallocSetPartitionName`;
callers at `fumi/ios/ios.c:94-97` ignore it), `seki/src/Basic.c:110`
(`freeseki` with a null pointer), `seki/src/GsBase.c:938,940`
(empty `gsb_ResetSnap`/`gsb_TakeSnap`), and the asm-return helpers in
`seki/src/Matrix.c:535-913` and `sugipon/src/matrixDrive.c:507-531`
(the value is in `$f0` from inline asm; the 1A transpiler replaces these).
`clothAnimation.c:1964-2003` is the same pattern. Before the warning is
made an error, check each caller for use of the return value.

### 9. Other observations

- `fumi/src/way_sys.c:327`:
  `way_probe(box[(k + 1) % 4], box[k += ofs])` reads and modifies `k`
  without sequencing. `ofs` is the constant 0 (`:240`), so it has no
  effect. Leave it.
- `omori/src/generator.c:602,619`: `(int)((z >> 5) & 0x1F) != -1` is always
  true (decomp-faithful). Same on every compiler.
- `omori/src/attackhit.c:369,536,540,555` and
  `common/src/charFileManager.c:274`: an array address tested against
  null. Always true; same everywhere.
- **GNU C extensions clang does not implement** (pass 3): **158 nested
  function definitions in 32 files** (`seki/src/Packet.c` 25,
  `sugipon/src/staticBlur.c` 23, `fumi/src/girl_act.c` 19,
  `fumi/src/commonact.c` 15, `sugipon/src/clothAnimation.c` 8,
  `seki/src/RegistPacket.c` 8, `sugipon/src/lineManager.c` 7,
  `common/src/debug.c` 7, `fumi/src/act-game.c` 6, `seki/src/Primitive.c`
  5, `fumi/src/enemy_act.c` 4, and 1 to 3 in 21 more), and **15 lvalue
  casts** (`common/src/debug.c:1191-1298`, `*((int *)w.ptr)++ = ...`).
  The plan names clang as the host compiler, so all of these must be
  rewritten: nested functions become `static` functions with the captured
  locals passed explicitly. Many of them read the enclosing frame
  (`debug.c:1096-1099` comment). That is a 0B item (or a gcc-only
  `ref-m32` until it is done).
- **1,253 non-static `inline` function definitions** in `.c`/`.inc` files.
  Under gnu89 rules (ee-gcc) each also emits an external definition.
  Under `-std=gnu11` each is only an inline definition, so calls that are
  not inlined become undefined references at link time. Add
  `-fgnu89-inline` to the game's flags.
- `-fno-strict-aliasing` is required: pass 1 reports 1,572
  `-Wstrict-aliasing` warnings.

### 10. Float to integer conversion

See `float-semantics.md`. ee-gcc compiles `(int)f` as one `cvt.w.s` (690
sites). It truncates and saturates at ±2^31. x86 `cvttss2si` returns
0x80000000 for out-of-range values of either sign; AArch64 saturates. The C
standard calls it undefined, so clang may also fold a constant
out-of-range conversion to anything. `(unsigned)f` calls `fptoui`
(negative gives 0); 3 sites, all with positive operands.

## Table of sites and handling

| # | site(s) | issue | risk | handling | owner |
| --- | --- | --- | --- | --- | --- |
| C1 | toolchain | argument order right to left on clang `*-windows-msvc` and on gcc | high (silently swaps RNG vector components) | build Windows with the mingw targets (llvm-mingw, `*-w64-mingw32`), never clang-cl or MSVC. Add a configure-time probe that fails the build when `g(r(),r())` is not evaluated left to right | 0A |
| C2 | the 5 RNG rows in section 2 | order-dependent RNG consumption | medium | also rewrite as sequenced temporaries (`float x = ...; float y = ...;`) under the PS2's left-to-right order. Same result under ee-gcc | 0B |
| C3 | toolchain | MS bitfield layout on mingw and msvc | high (layout) | `-mno-ms-bitfields` for all game TUs on Windows targets; layout asserts (2B) catch any leftovers | 0A |
| C4 | `ref-m32` (i386 SysV) | `long long`/`double` aligned to 4 | high (layout) | `-malign-double` for game TUs only (it changes the ABI of host structs such as `SDL_Event`, so never for port code that includes host headers), or make `ref-m32` the mingw i686 target, which needs no flag | 0A |
| C5 | all | plain `char` signedness | low today, high on AArch64 Linux and Android | `-fsigned-char` everywhere | 0A |
| C6 | 73 bare `long`, shift sites in section 4 | EE `long` is 64-bit | high | 0B's `long` sweep: `long` becomes `long long` or `int64_t` where the value is 64-bit | 0B |
| C7 | `commonact.c:2820` | `int << 57` | medium (flag bit 25 and 57) | spell out the EE result (section 5) | 0B (upstream first) |
| C8 | `layout_action.c:1338-1358` | reads 15 ints past `keepFlagNo` | **high** (game flags across load) | contiguous struct of `keepFlagNo`, `keyConfigCode` and `keyConfigSlot` under `ICO_HOST` | 0B |
| C9 | `itou_boss.c:583` | `memset` across two statics | high (memory corruption) | one struct | 0B |
| C10 | `fieldCollision.c:1563,1565` | stores past `n[4]`, `out[4]` | medium (stack corruption) | arrays of 5 under `ICO_HOST` | 0B |
| C11 | `jimaku.c:385` | call without an argument | high (null or garbage pointer) | pass `(int *)msg` | 0B (upstream first) |
| C12 | `st13c.c:974` | uninitialised `se` | medium | guard: stop nothing unless `se` was assigned. DIVERGENCES row | 0B |
| C13 | `camera-ico2.c:457-459` | garbage in `dbgA` | medium (trace noise) | zero-initialise `vDbg` under `ICO_HOST` | 0B / 2A |
| C14 | `cdvd.c:607` | `kind` uninitialised | low | `kind = -1`; guard the debug add | 0B |
| C15 | 158 nested functions, 15 lvalue casts | GNU extensions clang lacks | blocks the clang build | lambda-lift; rewrite the casts | 0B |
| C16 | 1,253 non-static `inline` | gnu89 inline semantics | link failure | `-fgnu89-inline` | 0A |
| C17 | `-Wreturn-type` list in section 8 | undefined return value | low | check callers, return explicitly | 0B / 1A |
| C18 | 690 `(int)f` | out-of-range conversion | low to medium | `fptrap` build; per-site `ps2_ftoi` | 1A |
| C19 | all | signed overflow, aliasing | medium | `-fwrapv -fno-strict-aliasing` (already in the plan) | 0A |

## Open questions

1. Is the EE stack slot behind `st13c.c:921 se` predictable (always the
   same caller frame)? If so, the guard in C12 could reproduce the actual
   value instead of "stop nothing". That needs an EE trace, which the plan
   does not have.
2. Was the stack slot after `n[4]`/`out[4]` in `fieldCollision.c` live on
   the EE? Reading the period object's stack frame layout
   (`build/ico2/fumi/src/fieldCollision.s`) would answer it statically.
3. Pass 1's optimiser warnings for the 33 files that fail it are still
   missing. Rerun after 0B and 1A.
