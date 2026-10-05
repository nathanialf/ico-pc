# Package 0E: nested functions and `break` sites

## Result

- 143 GNU nested functions (plus 1 that the finder missed, `Cling` in
  `commonact.c`, which has no `inline` keyword) in 27 files became file-scope
  `static` functions with captured variables passed explicitly. Names are kept
  except where noted. Not touched (package 1A): `clothAnimation.c`,
  `lineManager.c`, `quaternion.c`, `motionManager2.c`.
- `ICO_BREAK()` (`ico2/common/include/typedef.h`): `__asm__ __volatile__("break")`
  on the EE, `__builtin_trap()` under `ICO_HOST`. Used by `fumi/sound/s_init.c`
  and `script/src/warpGirl.c`. `fumi/ios/memory.c` is 1B's.
- `sugipon/src/motionManager.c` includes `GifPacket.h` before first use, so
  the `gif_*` implicit declarations are gone.
- `cmake/IcoExclusions.cmake` and `BUILD_STATUS.md` updated.

## Compiled game TUs (of 188)

| preset family | before | after |
| --- | --- | --- |
| gcc (`linux-x64`, ...) | 167 | 169 |
| clang (`linux-x64-clang`, `win-x64-clang`, ...) | 140 | 156 |

Measured on `linux-x64`, `linux-x64-clang` and `win-x64-clang` in
`build-host/0e-<preset>`: all build with exit 0. `tools/host_syntax_check.sh`
passes 223/223; `tools/format.sh --check` passes.

Newly compiling under clang (14 nested-function-only TUs): act-env, act-game,
act-parallel-control, act-way, boyact, girl_act, way_sys, camera-editor,
chain, op, st22a, FileManager, handManager, motionOrientManager.
`enemy_act`, `boy` and `motionViewer` are now blocked on clang only by
sugiCommon.h's VU0 asm. `commonact`, `box`, `item`, `motionManager` are
likewise blocked only by that asm.

## Renames and non-trivial captures

- `girl_act.c`: the four `GetSafePosition` copies became
  `Danger_{Bomb,Gondola,Box,Rotobject}_GetSafePosition`. `girl_act_hand.c.inc`
  is now included at file scope. `setNext`/`checkWarning` take `int *next`;
  `isHideRecheck` takes `float *rad` (taking `&rad` forces it to memory on
  the EE); `setMode` takes `int *cnt`.
- `RegistPacket.c`: functions prefixed with their parent
  (`reg_setNMatrixPacket_setMatrix`, ...).
- `commonact.c` written captures passed by pointer: `FlyStep`
  (`needInit`, `acc`, `mode`, `fc`), `completeEmergency` (`cnt104`),
  `emergencyCheck` (`wait`, `ringidx`, `ringcnt`, `flags`).
- `op.c` `tick(int *pt)` writes `t`.
- `sugipon/box.c`: two identical nested `isNearItem` became one shared
  static.
- Loop counters used only inside the nested function became locals
  (`camera-editor.c dispBox`, `Primitive.c drawDisc/drawSide`,
  `RegistPacket.c` `pack`).
- `staticBlur.c`: `testAA`, `subWork1ToCurrentFB`, `pasteBackLightShadowToFB`
  were never called; they are now unused statics.

## EE build: the ELF changes

`ninja build/ico.elf` in a private copy of the tree (the shared `build/` was
not touched) does not link: `tools/verify`-style symbol checks report
231+ `ent[].act: no global symbol at 0x...` for the function-pointer entries
of the actor tables, because the functions moved. So the hoisted statics
change the layout under ee-gcc 2.9 and `sha1sum build/ico.elf` no longer equals
`da3644c5...` (the original ELF was left in place and unchanged).

`.text` of the object is identical (objdump, no relocation text) for:
`s_init`, `st22a`, `warpGirl`, `box`, `motionOrientManager`. It differs for
every other changed file: debug, act-env, act-game, act-parallel-control,
act-way, boyact, commonact, enemy_act, girl_act, way_sys, camera-editor, chain,
op, DisplayFont, FileManager, Primitive, RegistPacket, Texture, boy,
handManager, item, motionManager, motionViewer, staticBlur. Function-level
differences are the former nested functions (now separate symbols) and the
enclosing functions' sizes/addresses. The data tables are address-keyed, so
the EE link needs regenerated tables if byte-for-byte or EE builds still matter.

## Notes

- `gcc -Wpedantic` based finder misses nested functions without `inline`;
  clang compile is the authoritative check, and all 0E files pass it (only
  sugiCommon.h asm errors remain where listed).
- `seki/src/Texture.c:280` has an existing clang `-Wint-conversion` error
  outside this package; it shows when Texture.c is compiled with clang
  (renderer-owned, not in the headless build).
- A direction message about package 1A (no transpiler for VU0 math) reached
  this package; it has no bearing on 0E and no action was taken.
