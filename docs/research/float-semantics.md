# EE FPU and VU0 float semantics

A research note written before the port was built, kept as the reasoning
behind `port/math/`, `port/platform/fpenv.c` and the float rows of
docs/port/DIVERGENCES.md; it is not updated as the code changes. Codes such
as 0A and 1A name the work items the recommendations were assigned to at
the time (the compiler flags and floating-point environment, and the VU0
maths). Line numbers in `ico2/` and `sce/` refer to commit `d9e456d4`.

## Agreed standard

- **Discrete state identical.** Integers, flags, RNG state, counters, object
  lists and everything derived from them must match the PS2 exactly.
- **Floats match PS2 semantics as documented.** Where a documented PS2
  behaviour can be reproduced cheaply and deterministically it is
  reproduced. Where it cannot (the undocumented LSB behaviour of add, sub,
  div and sqrt), the port uses a fixed host behaviour, and the difference
  is recorded.
- **Every known difference is logged** in `docs/port/DIVERGENCES.md`, with
  its cause, the affected sites and how it could be closed later.

There is no emulator in the loop (plan, "Verification"), so this note gives
the documented semantics and the reasoning. It cannot give measured truth.
Statements marked **(inference)** are my reasoning, not something a source
states.

## What was examined

Local:

- `ico2/common/src/main.c:149` (`_InitRandom(1.2345678f)`),
  `ico2/seki/src/Matrix.c:901-1028` (`_Sqrt`, `_InitRandom`, `_GetRandom`,
  `_GetRandomVector`, `_GetRandomVector0`),
  `ico2/sugipon/src/matrixDrive.c:495` (`FSqrt`),
  `ico2/ito/src/lightning.c:303-315` (writes R directly with
  `ctc2 ..., $vi20`), `ico2/sugipon/src/darkVolume.c:58-75` (`vdiv`,
  `vftoi4` projection).
- `sce/libgcc/dp-bit.c` and `sce/libgcc/fp-bit.c`: the soft-float routines
  the game linked. `dp-bit.c:14` defines `NO_DENORMALS`. `dp-bit.c:66-123`
  is the pack/round step, `:286-397` the multiply, `:577-601` `dptoli`.
  `fp-bit.c:569` is `fptoui`.
- `sce/libc/stdlib/rand.c` and `sce/libc/reent/impure.c` (newlib `rand`;
  the seed starts at 1).
- The period compiler itself (`tools/cc/ee-gcc2.9-991111`), run on small
  test files in the scratchpad to see the code it emits for float
  conversions, `a*b+c`, constant folding and `double`.
- The game objects of the existing build (`build/ico2/**/*.o`), checked with
  `mips-linux-gnu-objdump -dr`. They give an exact census of the FPU
  instructions and soft-float calls the game uses.

External (read as references only; PCSX2 is GPL-3.0-or-later and no code
from it is reproduced here or may be copied into the port):

- ps2tek, "EE COP1" section: https://psi-rockin.github.io/ps2tek/
- Gregory Gaines, "Emulating PS2 Floating-Point Numbers: IEEE 754
  Differences (Part 1)":
  https://www.gregorygaines.com/blog/emulating-ps2-floating-point-nums-ieee-754-diffs-part-1/
- PCSX2 EE FPU interpreter, `pcsx2/FPU.cpp` (master, fetched 2026-10-05):
  https://github.com/PCSX2/pcsx2/blob/master/pcsx2/FPU.cpp
- PCSX2 VU interpreter, `pcsx2/VUops.cpp` (master, fetched 2026-10-05):
  https://github.com/PCSX2/pcsx2/blob/master/pcsx2/VUops.cpp
- PCSX2 PR #12001, "[Soft-Float] Initial Interpreter Implementation of Ps2's
  floating point unit specification" (draft, hardware-tested):
  https://github.com/PCSX2/pcsx2/pull/12001
- PS2Recomp issue #28, "COP1/COP2 (FPU + VU0 macro) translator audit vs the
  PCSX2 interpreter": https://github.com/rrisque/PS2Recomp/issues/28

Sony's EE Core User's Manual and VU User's Manual are not public. They are
cited only where the public sources above quote or describe them.

## What the game actually uses

This census comes from the object files of the period build (`objdump -dr`
over the 228 game objects). The game's float maths reaches the hardware in
three ways.

**EE FPU (COP1), from compiled C.** `mul.s` 2204, `add.s` 1387,
`sub.s` 1249, `div.s` 824, `cvt.s.w` 1144, float to int conversion 690
(ee-gcc emits `cvt.w.s`, which binutils prints as `trunc.w.s` for the
R5900), compares `c.lt.s` 2333, `c.le.s` 298, `c.eq.s` 191. ee-gcc **never
emits** `madd.s`, `msub.s`, `madda.s`, `adda.s`, `mula.s`, `rsqrt.s`,
`max.s`, `min.s` or `sqrt.s` from C (all 0). A test compile of `a*b+c` gives
`mul.s` followed by `add.s`. A float to unsigned conversion calls the
soft-float `fptoui` (3 sites: `default_item_select` and `texture_fading` in
`layout_texture.c`, `actAP1Start`). `sqrtf` is never called. Square roots go
through VU0 (next point).

**VU0 macro mode (COP2), from inline asm and raw words.** Mnemonics counted
in `ico2/` and `sce/libvu0`: `vmove` 117, `vsub*` 105, `vmula*`/`vmadd*` about
310, `vmul*` 127 (of which `vmulq` 30), `vadd*` about 200, `vopmula`/`vopmsub` 48,
`vrsqrt` 21, `vdiv` 17, `vftoi4` 14, `vftoi0` 5, `vitof0`/`vitof4` 2,
`vclipw` 3, `vmaxx`/`vminix` 2 each, `vrnext` 5, `vrinit` 1, `vrxor` 1.
`vsqrt` appears 9 times as raw instruction words (`VU0_WORD(0x4A0103BD)` and
friends in `_Sqrt`, `Matrix.c:907`, and `FSqrt`, `matrixDrive.c:501`). The
game calls `_Sqrt` from 55 sites and `FSqrt` from 63.

**Soft-float double (libgcc `dp-bit`).** The EE has no double hardware.
ee-gcc defines `__mips_single_float`, and `double` is 8 bytes
(`sizeof(long double)` is 8 too). Every double operation is a call. Game
functions calling double arithmetic (`dpadd`, `dpsub`, `dpmul`, `dpdiv`,
`dpcmp`, `litodp`, `dptoli`, `dptofp`), from the object relocations:

| file | function | calls |
| --- | --- | --- |
| common/src/debug | debug_PrintFontf | dptofp |
| common/src/layout_texture | display_texture | dpmul x6, dptoli x3 |
| fumi/src/act-wish | ACTGetWish_FromPad | dpcmp |
| fumi/src/boyact | actBoyRun, actBoyWalk | dpmul, dpsub, dptofp (each) |
| fumi/src/commonact | WithMailFunc_FallDead | dpcmp |
| fumi/src/commonact | actCommonFall | dpmul x2, dptofp x2 |
| fumi/src/enemy_act | Battle_isCurrentStatus | dpmul x2, dptofp x2 |
| fumi/src/enemy_act | NakaBoss, actEnemyKidnapEnd | dpcmp x3, dpcmp |
| fumi/src/girl_act | HandMgr_Speed, subGirlBrain_Attract | dpcmp (each) |
| omori/src/attackhit | inner_check | dpmul, dptofp |
| omori/src/brain | brainLevelProcess | dpcmp x2, dpsub, dptofp |
| omori/src/camera-ico2 | monitorMonitorCamera | dpdiv x2, dptofp x2 |
| omori/src/camera-root | SetCameraMatrix | dpmul, dptoli, litodp |
| omori/src/chain | chain_simulate_term_{down,moveup} | dpcmp, dpdiv x2, dpmul x2, dptoli |
| omori/src/chain | chain_simulate_term_{free,loop,swingready,swingstart} | dpcmp x2..x4 |
| omori/src/chain | pendulum_Process | dpcmp x2, dpmul x2 |
| script/src/script | scpWoodSrh | dpcmp x2 |
| script/src/st04a, st04e (2), st05e, st06a, st13b (2), st25a | stage water/gate/elevator checks | dpmul, dptoli, litodp |
| sugipon/src/waterDot | setWaterDot | dpmul, dpsub, dptofp, litodp |

That is 33 functions in 20 files. A further 240 or so `fptodp` calls are
float to double promotions, almost all for variadic debug prints. Promotion
is exact, so they do not matter except for the denormal case below.

Explicit `double` in the source is rare: two `(double)` casts
(`fumi/src/girl_brain_attract.c.inc:457`, `fumi/src/commonact.c:3779`) and
the `fptodp`/`dptofp` prototypes (`sugipon/src/motMan_getFinalMatrix.c.inc:1023`,
`common/src/debug.c:681,2241`). Every other site is an unsuffixed literal in
a float expression, for example `waterDot.c:72`
`1.0 - (256 - n) * 0.00078125` and `camera-ico2.c:449-450`
`... / 30.0`, `3.0 / ...`. The object census above is the authoritative
list. A source grep for unsuffixed literals over-counts, because constant
folding removes most of them.

## Documented semantics

### Common to EE FPU and VU

- **No NaN and no infinity.** An exponent field of 255 is an ordinary
  exponent, so the format reaches about 2^128 (0x7FFFFFFF). ps2tek ("NaNs
  and Infinities do not exist on the PS2") and Gaines say the same.
- **No denormals.** An operand with exponent 0 is read as a zero of the same
  sign. A result that would be denormal becomes a signed zero, and the U
  flag is set. Sources: ps2tek; PCSX2 `FPU.cpp:78-82` (underflow keeps only
  the sign), `VUops.cpp:440-456` (`vuDouble` maps exponent 0 to signed
  zero). PR #12001 confirms on hardware that the multiplier flushes
  denormal inputs too.
- **Rounding is always toward zero, but not exactly IEEE round-toward-zero.**
  ps2tek: the mode is forced to round-towards-zero, and "the
  least-significant-bit may vary after a round". Gaines: it differs from
  IEEE RTZ "usually restricted to the least significant bit only". The
  FCR31 RM field reads back hard-wired to chop (PR #12001). PCSX2's
  `vuADD_TriAceHack` (`VUops.cpp:464-492`) handles one known case:
  when the exponents differ by 25 or more, the smaller operand is replaced
  by a signed zero before the add. Games (Tri-Ace titles) depend on this.
- **Overflow clamps.** An overflowing result becomes ±Fmax and sets O. The
  hardware Fmax is 0x7FFFFFFF. PCSX2 uses 0x7F7FFFFF, the IEEE FLT_MAX
  (`FPU.cpp:14,66-68`, `VUops.cpp:448-452`), because a host float cannot
  hold 0x7FFFFFFF as a number.

### EE FPU (COP1)

- **`div.s` by zero** (divisor exponent 0) gives ±Fmax with the sign of
  dividend XOR divisor. It sets D (dividend non-zero) or I (0/0)
  (`FPU.cpp:107-115`). There is no NaN: 0/0 is ±Fmax, not 0.
- **`sqrt.s`** of a negative number gives sqrt(|x|) and sets I. Of ±0
  (or a denormal) it gives a signed zero (`FPU.cpp:359-368`). The game
  never uses it.
- **`rsqrt.s`**: zero gives ±Fmax with D set. A negative gives
  x / sqrt(|y|) with I set (`FPU.cpp:339-357`). The game never uses it.
- **`cvt.w.s`** (the R5900's only float to int conversion) truncates
  toward zero. When the exponent is at or above 2^31 it saturates:
  positive to 0x7FFFFFFF, negative to 0x80000000 (`FPU.cpp:254-258`;
  PS2Recomp #28 found a translator that rounded to nearest and broke
  Shadow of the Colossus).
- **`cvt.s.w`** rounds toward zero, which shows for |i| > 2^24.
- **`madd.s`/`msub.s`** are not fused in PCSX2's interpreter. The product
  is stored to a float (rounded) and then added (`FPU.cpp:271-277`). The
  game never emits them, so this does not matter for the EE side.
- **Compares** (`c.lt.s` and so on) are ordinary ordered compares. Without
  NaN there is no unordered case.

### VU0 (macro mode)

- **`vadd`/`vsub`/`vmul`**: the common rules above. The operands pass
  through `vuDouble` (exponent 0 becomes zero, exponent 255 is clamped
  when PCSX2's overflow option is on).
- **`vmadd`/`vmsub`/`vopmsub`, ACC forms**: PCSX2 computes
  `acc + fs*ft` as one C float expression (`VUops.cpp:706, 745, 866-868`).
  Without contraction that rounds the product to float before the add.
  PR #12001 finds that hardware keeps sticky flags from internal events,
  including the product flush, and describes the product as a separate
  rounded step. **(inference)** The VU multiply-add is not fused: round the
  product, then add.
- **`vdiv Q`**: a zero divisor gives ±Fmax (sign XOR). Q is 0x7F7FFFFF or
  0xFF7FFFFF in PCSX2, and 0/0 also gives ±Fmax, with status bit D (x/0) or
  I (0/0) (`VUops.cpp:931-956`). Otherwise fs/ft, then clamped.
  PR #12001: the hardware divider is iterative. The result is either the
  truncated 24-bit quotient T or T+1, depending on the operands, so it is
  not always the IEEE RTZ quotient.
- **`vsqrt Q`**: sqrt(|ft|), I set for a negative (`VUops.cpp:958-968`).
  The same T/T+1 remark applies.
- **`vrsqrt Q`**: ft = 0 gives ±Fmax if fs ≠ 0, and ±0 with I if fs = 0
  (`VUops.cpp:970-990`). Otherwise fs / sqrt(|ft|).
- **`vftoi0/4/12/15`**: multiply by 2^n (exact apart from overflow), then
  truncate. When the scaled exponent is at or above 2^31 the result
  saturates to 0x7FFFFFFF or 0x80000000 by sign (`VUops.cpp:876-888`).
- **`vitof0/4/12/15`**: int to float (rounding toward zero on hardware),
  then multiply by 2^-n (`VUops.cpp:895-902`).
- **`vclip`**: compares |x|, |y|, |z| of fs against |w| of ft with strict
  "greater than". It sets six judgement bits (+x, -x, +y, -y, +z, -z) and
  shifts the 24-bit clip flag register left by 6 first, so it holds the
  last four judgements (`VUops.cpp:909-925`). PCSX2 compares the IEEE bit
  patterns as sign-magnitude integers, which orders correctly with no NaN.
  A denormal |w| is treated as the largest denormal. The game uses
  `vclipw` 3 times.
- **`vmax`/`vmini`**: compare as floats. PS2Recomp #28 found a translator
  that compared the raw bits as integers, which orders negative numbers
  wrongly; do not do that.
- **The R register** is a 23-bit LFSR presented as a float in [1, 2): the
  exponent and sign bits read as 0x3F800000.
  - `vrinit R, fs.f`: R = 0x3F800000 | (fs.f bits & 0x7FFFFF).
  - `vrxor R, fs.f`: R = 0x3F800000 | ((R ^ fs.f bits) & 0x7FFFFF).
  - `vrnext ft.dest, R`: advance once, then write R to **every** field in
    dest. One instruction advances once, so `vrnext.xyz` writes three equal
    values.
  - `vrget`: write R without advancing.
  - Advance (PCSX2 `VUops.cpp:1301-1309`, described in words): take bit 4
    and bit 22 of R, shift R left by one, XOR the two taken bits into
    bit 0, keep the low 23 bits, OR in 0x3F800000. PCSX2's comment credits
    a now-defunct site (project-fao.org). PS2Recomp #28 says an incorrect
    LFSR broke Shadow of the Colossus and fixed it to match PCSX2.
    **This polynomial has not been verified on hardware by any source I
    found. It is the largest single uncertainty for "discrete state
    identical".**
  - `ctc2 rt, $vi20` (write R from a GPR, used at
    `ito/src/lightning.c:309` with a float in [1, 2) in the GPR):
    **(inference)** it stores the same masked form as `vrinit`. For a value
    already in [1, 2) the result is the same either way.

## The game's RNGs

The game has two generators. Both are discrete state.

1. **newlib `rand()`** (`sce/libc/stdlib/rand.c`), 38 call sites in the
   objects. It is the LCG `s = s*0x41C64E6D + 0x3039` (wrapping 32-bit),
   returning `s & 0x7FFFFFFF`, with its state in `_impure_ptr->rand_next`
   (offset 0x58). The seed starts at 1 (`sce/libc/reent/impure.c`) and the
   game never calls `srand`. Integer only, so it is easy to reproduce
   exactly. It needs `-fwrapv` or unsigned arithmetic.
2. **VU0 R** (`_GetRandom`, `Matrix.c:1002`, 31 call sites through wrappers
   such as `random_unit` and `sugiRandom`). `_InitRandom(1.2345678f)` at
   `main.c:149`, run once after the boot threads are set up, does:
   `vf1.x = seed`; `vf2.x = vf1.x + vf0.w` (seed + 1.0, a VU add);
   `vf1.x = vf1.x + vf1.x` (2·seed); `vrinit R, vf2.x`; `vrxor R, vf1.x`.
   `_GetRandom` is `vrnext.x` then subtract 1.0 (`vsubw.x` with
   vf0.w = 1.0). The subtraction is exact because both operands are in
   [1, 2), so the returned float is exactly `(R & 0x7FFFFF) / 2^23`.
   `lightning.c:303-315` re-seeds R from a float seed folded into [1, 2).

   Test vectors for 1A, computed from the steps above and the PCSX2 LFSR
   (scratchpad script). seed = 0x3F9E0651. seed + 1.0 = 0x400F0328 (the
   exact sum ends in one dropped bit. RTZ and RNE agree here, because the
   kept bit is even, so the add-rounding question does not affect the
   seed). 2·seed = 0x401E0651. R after `vrinit` = 0x3F8F0328. R after
   `vrxor` = 0x3F910579. The next six R values are 0x3FA20AF3, 0x3FC415E7,
   0x3F882BCF, 0x3F90579E, 0x3FA0AF3D, 0x3FC15E7B, so the first
   `_GetRandom()` values are 0.26595914, 0.53191841, 0.06383693,
   0.12767386, 0.25534785, 0.51069582.

   These vectors pin the implementation to the documented model. They do
   not prove that model matches hardware. A PS2 owner could check the first
   value on hardware with any homebrew that runs the same five
   instructions. That is listed as an open question.

`_GetRandomVector` (three separate `vrnext`) and `_GetRandomVector0`
(`vrnext.xyz`, three equal components) are declared in `Matrix.h:73-74` but
have no callers.

## Host side: what the port must do

### Sim-thread FP environment (`port/platform/fpenv.c`, 0A)

Set on the OS thread that runs the game fibers, before the first game call,
and checked with an assert at every `ico_vsync`:

- **x86/x86-64 (SSE):** MXCSR RC = toward zero (11b), FTZ = 1, DAZ = 1, all
  exceptions masked (the `fptrap` preset unmasks some, below).
- **AArch64 (future Mac and Android):** FPCR.RMode = RZ (0b11), FPCR.FZ = 1.
  On AArch64, FZ flushes both inputs and outputs, which covers DAZ.
- **No x87.** The 32-bit builds (`ref-m32`, `win-x86-ref`) must compile with
  `-msse2 -mfpmath=sse`, or float temporaries carry 80-bit precision and
  the x87 control word, and nothing in this note holds.
- Compiler flags: `-ffp-contract=off` (mandatory: ee-gcc never contracts,
  see the census. Clang's default `-ffp-contract=on` fuses `a*b+c` on any
  FMA target, including every AArch64 and x86-64-v3), `-fno-fast-math`,
  and no `-ffinite-math-only` or `-freciprocal-math`. Do not use LTO on
  game code (next point).
- **Constant folding (inference, measured on ee-gcc).** ee-gcc folds float
  literal expressions at compile time with round-to-nearest, even through
  inlined calls: `1.0f/3.0f` and an inlined `d(1.0f, 3.0f)` both give
  0x3EAAAAAB. At run time IEEE round toward zero gives 0x3EAAAAAA (PCSX2
  PR #12001's hardware-derived divider gives 0x3EAAAAAB: DIVERGENCES.md F2).
  Clang without `-frounding-math` also folds with round-to-nearest, so
  literal folding agrees. The leftover risk is expressions clang can fold
  but ee-gcc evaluated at run time (more aggressive propagation,
  cross-TU inlining under LTO). Those come out round-to-nearest on the
  host where the PS2 truncated. `-frounding-math` is **not** recommended:
  it would stop clang folding literals that ee-gcc folded, and so move the
  difference to the much larger set of literal expressions. Log the
  leftover as a divergence. The 32-bit vs 64-bit trace oracle cannot see
  it, because both builds use the same compiler.
- Host code that runs on the sim thread (VFS, SDL calls, logging) runs
  under RTZ/FTZ too. That is harmless for the I/O paths. Any host maths
  library called from the sim thread (audio resampling, the renderer)
  should not be: those run on their own threads, or the call saves and
  restores the environment (`fpenv_push_host()`/`fpenv_pop()`).

With RTZ and FTZ/DAZ on the host:

| PS2 behaviour | Host result | Matches? |
| --- | --- | --- |
| denormal in / out flushed to signed zero | DAZ/FTZ | yes |
| overflow clamps to Fmax | IEEE RTZ overflow gives FLT_MAX (0x7F7FFFFF), not Inf | yes for PCSX2's Fmax. Hardware Fmax 0x7FFFFFFF is not representable: divergence |
| magnitude add (same signs), mul (inference: see below) | IEEE RTZ | add: yes (see below). mul: probably |
| effective subtraction where bits of the smaller operand are shifted out | IEEE RTZ | **no**: PS2 can be 1 ulp larger in magnitude |
| x/0, 0/0 | Inf, NaN | **no**: needs the helper or the trap build |
| float to int out of range | x86 gives 0x80000000 for both signs (AArch64 saturates like the PS2) | **no** on x86 for positive overflow |
| int to float, > 2^24 | `cvtsi2ss` follows MXCSR, so RTZ | yes |
| exponent-255 operands | read as Inf or NaN | **no**, but such values only come from data or bit tricks |

Add/sub reasoning **(inference)**: model the PS2 adder as "align the
smaller operand by right shift and drop the bits shifted past the kept
guard bits, add, normalise, truncate". The TriAce rule (diff ≥ 25 means the
small operand becomes zero) is the extreme case of that model. When both
operands have the same sign, dropping low bits of the addend before a
truncating add gives the same result as truncating the exact sum, so it
equals IEEE RTZ. When the signs differ, dropping low bits of the subtrahend
makes the result's magnitude too large, so the PS2 can return 1 ulp more
(in magnitude) than IEEE RTZ. Example: `1.0f - 2^-30` is 0x3F800000 on the
PS2 (TriAce rule) and 0x3F7FFFFF under IEEE RTZ. IEEE round-to-nearest
would give 0x3F800000 here, which is why some emulator users find "nearest"
fixes certain games. Neither IEEE mode is exact.

### `port/math/vu0_ops.h` helpers (1A)

All helpers work on `float`/`uint32_t` with the sim-thread environment
above. Every transpiled VU0 instruction goes through them (never raw host
operators), so an exact model can later be dropped in one place.

1. `vu_in(x)`: operand normaliser. Exponent 0 becomes a signed zero;
   exponent 255 becomes ±FLT_MAX. With DAZ on, the first is free; the
   second is a compare.
2. `vu_out(x)`: result clamp. Inf becomes ±FLT_MAX with the O flag. With
   RTZ, IEEE overflow already lands on FLT_MAX, but Inf can still come from
   an Inf operand.
3. `vu_add/sub/mul(a, b)`: host op under RTZ, then `vu_out`. Reserve an
   `ICO_PS2_EXACT_ADD` build switch for the PR #12001 adder model (the
   effective-subtraction case) once that model is final. Until then this
   is a logged divergence.
4. `vu_madd(acc, a, b)` / `vu_msub`: `vu_add(acc, vu_mul(a, b))`, two
   rounded steps, never `fmaf`.
5. `vu_div(fs, ft)`: if ft's exponent is 0, return ±FLT_MAX with sign
   `fs^ft` (including 0/0) and set the D or I status bit. Otherwise
   `fs/ft` under RTZ, then clamp.
6. `vu_sqrt(ft)`: `sqrtf(fabsf(ft))` under RTZ (exponent 0 gives +0), I if
   negative. `vu_rsqrt(fs, ft)`: ft = 0 gives ±FLT_MAX (fs ≠ 0) or ±0
   (fs = 0), otherwise `fs / sqrtf(fabsf(ft))`. Note this is two rounded
   steps under RTZ, against the hardware's single iterative step (T/T+1),
   so it is a divergence on the last bit.
7. `vu_ftoi(n, x)`: scale by 2^n, then truncate, saturating at |x| ≥ 2^31 to
   0x7FFFFFFF / 0x80000000 by sign. Never a bare C cast (UB, and
   0x80000000 on x86).
8. `vu_itof(n, i)`: `(float)i` (RTZ via MXCSR), then × 2^-n.
9. `vu_clip(fs, w, &clipflag)`: shift the flag register left 6 and mask to
   24 bits; set the six bits with strict `>` against |w|; use the
   sign-magnitude bit compare described above.
10. `vu_max/vu_mini`: float compares on normalised operands.
11. R register: `vu_rinit`, `vu_rxor`, `vu_rnext(dest_mask)` (one advance,
    every masked lane written), `vu_rget`, and `ctc2 vi20` mapped to
    `vu_rinit` on the GPR's bits. State lives in the single `ico_vu0`
    struct, so the trace hash covers it.
12. Unit tests: the R test vectors above; div/sqrt/rsqrt special cases;
    ftoi saturation at ±2^31; clip flag history over four calls; madd not
    fused (pick `a*b` whose RTZ rounding changes `acc + a*b`).

### EE FPU from compiled C (0A, 1A)

C float code is compiled by clang straight to host instructions, so no
helper sits in the way. That leaves three hazards, each with a plan:

- **Division by zero and invalid operations.** 824 `div.s` sites. The
  `fptrap` preset unmasks FE_DIVBYZERO and FE_INVALID (FE_INVALID also
  catches 0/0, `inf-inf`, `sqrtf(<0)` and out-of-range `cvttss2si`). Every
  trap found in user testing becomes a `ps2_divf`/`ps2_ftoi` call at that
  site (ASCII patch, `ICO_HOST`-guarded, sent upstream first if it is a
  reconstruction issue) plus a DIVERGENCES row. A known candidate is
  `fumi/src/fieldCollision.c:1545-1546`, which divides by `va->z - vb->z`
  when `va->x == vb->x`. A degenerate edge would give 0/0 there.
- **Float to int.** 690 sites. x86 differs only when a positive value is
  ≥ 2^31 (0x80000000 instead of 0x7FFFFFFF). The trap build catches it.
  AArch64 `fcvtzs` already saturates like the PS2.
- **Float to unsigned.** The PS2 calls `fptoui`, which returns 0 for
  negatives and 0xFFFFFFFF above 2^32 (`fp-bit.c:569-600`). The 3 sites
  convert values that are positive by construction (frame counts in
  `layout_texture.c:262,353`; `act_a_p_1`). Low risk; log.
- **libm.** Keep newlib's `atan2f` (25 calls), `acosf` (8), `asinf` (3),
  `fmodf` (3), `sinf` (1) and `qsort` (8) compiled from source as the plan
  says. They then run under the same RTZ environment as on the EE. Do not
  substitute the host libm. Its results differ in the last bit, and on
  Windows they differ between CRTs.

### Soft-float doubles (1A)

This is a real difference. On the EE, `double` arithmetic is libgcc's
`dp-bit` (`sce/libgcc/dp-bit.c`): round-to-nearest-even with 8 guard bits
and a sticky bit in multiply (`:93-97`). Denormal **inputs** are read as
zero (`NO_DENORMALS`, `:14, 141-145`). A tiny result is **truncated** into
a denormal output (`:86-96`, no rounding). `dptoli` truncates and
saturates to ±2^31 (`:577-601`). `dptofp` rounds to nearest. On the host
under the sim environment above, SSE double arithmetic would run **RTZ**
(MXCSR applies to double too) with FTZ. So every one of the 33 functions in
the table above would round differently from the PS2 on any inexact
result, and `dptofp` would truncate instead of rounding. That is common.
Examples: `0.04` times a frame count, `r1 = .../30.0`.

Recommendation, in order of preference:

1. Build `port/math/softdouble.c` from the algorithm in
   `sce/libgcc/dp-bit.c`, which is integer-only and deterministic. Rewrite
   the double expressions in those 33 functions as calls to it
   (`ico_dmul`, `ico_dsub`, `ico_ddiv`, `ico_dcmp`, `ico_i2d`, `ico_d2i`,
   `ico_d2f`, `ico_f2d`) behind an `ICO_HOST` macro, so that the PS2 build
   keeps the plain C. The results are then bit-identical to the PS2 by
   construction. The list is closed: the object census is exhaustive for
   linked game code.
2. If source edits are refused: wrap each of the 33 functions' double
   sections in a scoped `fpenv` switch to round-to-nearest with FTZ/DAZ
   off. That matches `dp-bit` except for denormal input flushing, sticky
   corner cases and the truncating denormal output. Log those.

Unit test: compile `softdouble.c` and `sce/libgcc/dp-bit.c` (the
reconstruction, built for the host with its `long` arguments mapped to
64-bit) and compare them on random and edge operands. Also compare against
SSE round-to-nearest double to measure how often option 2 would differ.

## Open questions

1. Is the R LFSR (taps at bits 4 and 22, as PCSX2 has it) right on
   hardware? A PS2 owner could check this by running a homebrew that
   executes `vrinit`, `vrxor` and `vrnext` with the `main.c:149` seed and
   prints R, and comparing with the vectors above. Every particle,
   lightning bolt and "random" choice that goes through `_GetRandom`
   depends on it.
2. The exact PS2 adder, divider and square-root LSB rules. PR #12001 is
   still a draft. When it lands, port its model (as a description, not GPL
   code) into `vu0_ops.h` behind `ICO_PS2_EXACT_ADD`, and decide whether C
   float code should go through it too (a clang pass or source rewrite of
   2,636 add/sub sites. Probably not worth it).
3. Is the multiplier exactly IEEE RTZ on normal operands? No source
   states this; Gaines' later parts may.
4. Fmax: PCSX2 clamps to 0x7F7FFFFF, while the hardware's largest value is
   0x7FFFFFFF. Is there any game path where the difference shows up in
   discrete state (for example a ±Fmax quotient converted to int: both
   saturate, so probably not)?
5. Does hardware `vdiv`/`vrsqrt` 0/0 give ±Fmax (PCSX2 FPU and VU `vdiv`) or
   0 (PCSX2 `vrsqrt` 0/0)? The port follows PCSX2 per instruction.

## Recommended rows for `docs/port/DIVERGENCES.md`

| id | area | PS2 | port | sites | closable by |
| --- | --- | --- | --- | --- | --- |
| F1 | float add/sub | PS2 adder drops shifted-out bits (effective subtraction can be 1 ulp larger in magnitude) | IEEE RTZ | all float add/sub, VU0 and C | PR #12001 model in `vu0_ops.h` |
| F2 | div, sqrt, rsqrt LSB | iterative, T or T+1 | IEEE RTZ (rsqrt in two steps) | `vdiv` 17, `vrsqrt` 21, `vsqrt` 9 words, `div.s` 824 | exact divider model |
| F3 | Fmax | 0x7FFFFFFF | 0x7F7FFFFF | overflow and divide-by-zero results | not representable on host |
| F4 | constant folding | ee-gcc folds some float expressions RNE, the rest run RTZ | clang may fold a different set | unknown | compare `-S` output per function if a trace mismatch points there |
| F5 | C `/` by zero, out-of-range `(int)` | ±Fmax / saturate | Inf, NaN / 0x80000000 | found by the `fptrap` build | per-site helper |
| F6 | soft double | dp-bit RNE, denormal input flush, truncating denormal output | host double (only if option 2 above is taken) | 33 functions | `softdouble.c` |
| F7 | R LFSR | hardware polynomial | PCSX2's taps 4 and 22 | `_GetRandom` 31 sites, lightning re-seed | hardware test |
