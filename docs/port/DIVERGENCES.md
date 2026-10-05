# Divergences from the PS2 original

The port's standard (docs/research/float-semantics.md): discrete game state
(integers, flags, RNG state, object and state-machine state, timers) is
identical to the PS2 PAL release, SCES-50760; floating-point results follow
the PS2's documented semantics (EE FPU and VU0) as closely as the port
implements them. Every known case where the port's behaviour or output can
differ from the original, deliberately or not, is listed here, with where it
happens and why. Options that are off by default (widescreen, interpolation,
stick fix, the Yorda option, mirror mode) are not divergences of the default
game and are listed under their own heading only for reference.

## How to add an entry

One row per divergence. Give the site (`file:line` or function), what the
PS2 does, what the port does, whether discrete state can be affected, how it
was found (static audit, unit test, trace diff, user report) and the status
(accepted, to fix, fixed in <commit>).

## Default build

| id | site | PS2 behaviour | port behaviour | affects discrete state | found by | status |
|---|---|---|---|---|---|---|

## Floating point

| id | site | PS2 behaviour | port behaviour | affects discrete state | found by | status |
|---|---|---|---|---|---|---|
| F1 | every float add/sub, VU0 and C | the adder drops shifted-out bits, so an effective subtraction can come out 1 ulp larger in magnitude than IEEE round-toward-zero (float-semantics.md) | IEEE round toward zero | possibly, through comparisons | static (R2) | accepted; closable by an exact adder model in port/math/ps2float.h |
| F2 | VU0 `vdiv`/`vsqrt`/`vrsqrt` (port/math: ps2_div, ps2_sqrt, ps2_rsqrt), EE `div.s` | iterative divider and root: the truncated quotient T or T+1 | IEEE round toward zero; rsqrt as two rounded steps (a / sqrt(b)) | possibly, through comparisons | static (R2) | accepted |
| F3 | overflow and divide-by-zero results (ps2_div, ps2_rsqrt, RTZ overflow) | Fmax 0x7FFFFFFF | 0x7F7FFFFF (FLT_MAX); 0x7FFFFFFF is not a host float | no known site (both saturate in a float-to-int conversion) | static (R2) | accepted |
| F4 | float expressions the compiler folds | ee-gcc folds some literal expressions with round-to-nearest, the rest run toward zero | clang/gcc may fold a different set (port/math/test/math_test.c shows host gcc folding `ps2_div(1.0f, 3.0f)` to 0x3EAAAAAB where the run-time result is 0x3EAAAAAA) | unknown | unit test | accepted; compare per function if a trace points there |
| F5 | C `/` by zero and out-of-range `(int)` in game code (824 `div.s`, 690 conversions) | +-Fmax / saturate by sign | Inf or NaN / 0x80000000 on x86 | possibly | static (R2) | to fix per site as the fptrap preset finds them (ps2_div, ps2_ftoi in port/math/ps2float.h) |
| F6 | `double` arithmetic in 33 functions in 20 files (docs/port/MATH.md, "Doubles") | libgcc dp-bit soft float: round to nearest, denormal inputs read as zero, `dptofp` rounds to nearest | host double under the sim thread's round-toward-zero mode | yes (e.g. frame-count scaling, camera and chain physics) | static (R2) | to fix (follow-up package: softdouble.c) |
| F7 | VU0 R register (port/math/matrix.c `_GetRandom`, 31 sites; `ico_vu0_random_set` for ito/src/lightning.c) | hardware LFSR | PCSX2's taps 4 and 22, unverified on hardware; matches R2's vectors | yes, if the polynomial is wrong | static (R2), unit test | accepted pending a hardware check |
| F8 | port/math: values the VU0 code formed as `0 + x`, `x * 1` or a register move | VU0 arithmetic flushes a denormal and turns -0 into +0 in `0 + x`; a `vmove` copies bits | the C copies the value (or folds `x * 1`), so a -0 or a denormal entry in a matrix or vector passes through unchanged | no (sign of zero and denormals only) | design (docs/port/MATH.md) | accepted |
| F9 | port/math loads (`lqc2`/`lq`) | the address's low 4 bits are ignored (16-byte alignment forced) | the exact address is read | only if the game passed a misaligned address, which on the PS2 read the aligned quadword | design | accepted |
| F10 | VU0 registers shared between routines (MATH.md, "Register side effects") | sceVu0*, FSqrt, VectorLength, AddVectorXYZ, apply_matrix_w1 and the cloth helpers overwrite vf4-vf7, the current matrix; stale register words reach memory in a few stores (enemy.c projection, lineManager clipAtX/clipAtY w, _GetRandomVector w) | routines touch only their arguments; the current matrix is a C array; stale words are not written | only where the game set the current matrix, called one of those routines and read the current matrix again: a static scan found 17 candidate call sequences, not yet triaged | static scan | to triage (MATH.md lists them) |

## Platform (audio, video, input, saves, timing)

| id | site | PS2 behaviour | port behaviour | affects discrete state | found by | status |
|---|---|---|---|---|---|---|

## Optional features (off by default)

| option | hook | what changes |
|---|---|---|
