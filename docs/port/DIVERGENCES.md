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
| F5 | C `/` by zero and out-of-range `(int)` in game code (824 `div.s`, 690 conversions) | +-Fmax / saturate by sign | Inf or NaN / 0x80000000 on x86 | possibly | static (R2) | to fix per site as the fptrap preset finds them (ps2_div, ps2_ftoi in port/math/ps2float.h). Fixed in 2I (ps2_div, host only): `seki/src/GsBase.c` `gsb_SetVSMatrixSub` (zoom 0 before the first stage, F11), `sugipon/src/staticBlur.c` `calcSun` (zero view matrix on a stage's first tick), `omori/src/camera-root.c` `SetCameraMatrix` (empty zoom range), `sugipon/src/weapon.c` `initializeQueenzSword` (0 / 0.0f literal). Next fptrap stop: a NaN into `_FTOI4Vector` at Main tick 1011, stage 40 (caller not resolved; SWEEP_2I.md) |
| F6 | `double` arithmetic in 33 functions in 20 files (docs/port/MATH.md, "Doubles") | libgcc dp-bit soft float: round to nearest, denormal inputs read as zero, `dptofp` rounds to nearest | host double under the sim thread's round-toward-zero mode | yes (e.g. frame-count scaling, camera and chain physics) | static (R2) | to fix (follow-up package: softdouble.c) |
| F7 | VU0 R register (port/math/matrix.c `_GetRandom`, 31 sites; `ico_vu0_random_set` for ito/src/lightning.c) | hardware LFSR | PCSX2's taps 4 and 22, unverified on hardware; matches R2's vectors | yes, if the polynomial is wrong | static (R2), unit test | accepted pending a hardware check |
| F8 | port/math: values the VU0 code formed as `0 + x`, `x * 1` or a register move | VU0 arithmetic flushes a denormal and turns -0 into +0 in `0 + x`; a `vmove` copies bits | the C copies the value (or folds `x * 1`), so a -0 or a denormal entry in a matrix or vector passes through unchanged | no (sign of zero and denormals only) | design (docs/port/MATH.md) | accepted |
| F9 | port/math loads (`lqc2`/`lq`) | the address's low 4 bits are ignored (16-byte alignment forced) | the exact address is read | only if the game passed a misaligned address, which on the PS2 read the aligned quadword | design | accepted |
| F10 | VU0 registers shared between routines (MATH.md, "Register side effects") | sceVu0*, FSqrt, VectorLength, AddVectorXYZ, apply_matrix_w1 and the cloth helpers overwrite vf4-vf7, the current matrix; stale register words reach memory in a few stores (enemy.c projection, lineManager clipAtX/clipAtY w, _GetRandomVector w) | routines touch only their arguments; the current matrix is a C array; stale words are not written | only where the game set the current matrix, called one of those routines and read the current matrix again: a static scan found 17 candidate call sequences, not yet triaged | static scan | to triage (MATH.md lists them) |
| F11 | `seki/src/GsBase.c` `gsb_SetVSMatrixSub` with the zoom `vs[0]` = 0 (the boot frames before the first stage, from vsync 6) | `sx`, `sy`, `cx`, `cy` = Fmax (0x7FFFFFFF); `sx + sx` overflows to Fmax; `4 / Fmax` is about 2^-127, a denormal, read as 0: `proj[0]`, `proj[5]`, `projHalf[0]`, `projHalf[5]` = 0 | `ps2_div` gives FLT_MAX (F3); `4 / FLT_MAX` rounds toward zero to 2^-126 (FLT_MIN), a normal: those four terms are FLT_MIN. Before 2I the host gave Inf, then 0 for those terms and NaN for `viewport[0]` (0 * Inf) | no (the matrices are rebuilt once the stage sets the zoom; traces identical) | fptrap preset (2I) | accepted |

## Platform (audio, video, input, saves, timing)

| id | site | PS2 behaviour | port behaviour | affects discrete state | found by | status |
|---|---|---|---|---|---|---|
| A1 | port/audio/spu2.c `reverb_step` | the reverb unit computes left and right on alternate 48 kHz cycles (psx-spx, "Reverb Formula" notes) | both sides in one 24 kHz step | no (audio only; psx-spx measures the effect at 1-2 LSB) | design (4A, docs/port/AUDIO.md) | accepted |
| A2 | port/audio/spu2.c `reverb_step` | exact rounding and saturation points of the reverb datapath are unknown; vIIR = -0x8000 negates the stored result (psx-spx "Bug", "Reverb Precision") | every product shifted by 15 and every stage saturated; the vIIR = -0x8000 quirk not modelled | no (audio only) | design (4A) | accepted |
| A3 | port/audio/adpcm.c `adpcm_decode_block` | ADPCM filters 5-7 and shift 13-15 undocumented for SPU-ADPCM | filter 5-7 decode as 0; shift 13-15 as 9 (psx-spx's XA-ADPCM rule) | no | design (4A) | accepted; check against the disc's data if any VAG uses them |
| A4 | port/audio/spu2.c `spu2_env_tick` | exponential decrease step = step * level / 0x8000 (psx-spx), rounding not stated | arithmetic shift (floor), so the step never rounds to 0 and releases reach 0 | possibly: ENVX timing decides when `_SgSeqSeRrEnd` (`sound.c:1818`) frees a slot | design (4A) | accepted pending a hardware capture |
| A5 | port/audio/spu2.c `spu2_env_tick`, `key_on`, `adsr_tick` | envelope step counter behaviour across steps and phase changes not fully documented (psx-spx notes rates above 26 misbehave) | counter keeps its low 15 bits after a step; reset to 0 at key on and every phase change; psx-spx's increment formula for all rates | possibly (ENVX timing, as A4) | design (4A) | accepted |
| A6 | port/audio/spu2.c `key_on` | key on latency, and whether key on resets the ADPCM history, the interpolator and LSAX, are not documented | key on takes effect in the frame it is applied; ADPCM and interpolation history zeroed; LSAX left alone (set only by a loop-start flag or a write) | possibly (ENVX timing of the first frames) | design (4A) | accepted |
| A7 | port/audio/spu2.c `voice_nax` | NAX is the hardware's current read address | block address + 1 + (samples consumed in the block) / 4 | possibly: SNDN2DRV's stream scheduler compares NAX with the ring halves once per tick (sndn2drv.md, "ADPCM streams"); only a block-boundary tie could differ | design (4A) | accepted |
| A8 | port/audio/spu2.c `spu2_voice_trans` timing | SPU2 DMA takes real time (speed not measured) | a transfer completes one frame after it starts, or at `spu2_set_dma_rate` bytes per frame | possibly: `SgGetDmaTransferStatus` callers wait on it (sndn2drv.md, "Sample upload") | design (4A) | accepted; 4B decides the rate |
| A9 | port/audio/spu2_tables.c, spu2_sd.c `spu2_sd_set_effect_attr` | libsd's SPU2 effect presets are data in the disc's LIBSD.IRX; ECHO/DELAY use the attr's delay and feedback | psx-spx's published PS1 presets (Studio Large for libsd STUDIO_3, the game's mode); delay and feedback ignored (the game never sends 0x17/0x18, R1) | no (audio only) | design (4A) | to check against the disc's LIBSD.IRX; `spu2_reverb_set_preset` can load it |
| A10 | port/audio/spu2.c `core_frame` | ATTR bit 15 (SPU on) and bit 14 (mute) gate the output; their SPU2 sense is unconfirmed (psx-spx gives bit 14 as 1 = unmute on the PS1; ps2sdk names it SD_MUTE and sets it in sceSdInit) | both ignored: a core always outputs | no (the driver never uses mute, R1) | design (4A) | accepted |
| A11 | port/audio/spu2.c `memin_*` | the AutoDMA input reads ring buffers in sound RAM and its accesses can raise IRQs | the input reads a host ring (`spu2_memin_start`) in libsd's 0x800-byte layout; nothing is stored in sound RAM; no IRQ from it | no | design (4A) | accepted |
| A12 | port/audio/spu2.c `irq_check` | IRQ on every reverb buffer access as well (PCSX2 wiki "SPU2 is more than just sound!") | IRQ from voice block reads, transfers and the output write-backs only | no (the driver never enables the IRQ, R1) | design (4A) | accepted |
| A13 | port/audio/spu2.c `reverb_step` (disabled) | with ATTR bit 7 clear, the reverb still reads and runs a partial chain depending on vAPF1/vAPF2 (psx-spx "Reverb Disable") | the whole chain is computed from the buffer; only the writes stop | no (audio only) | design (4A) | accepted |
| A14 | port/audio/spu2_sd.c `spu2_sd_init` | sceSdInit's register values come from the disc's LIBSD.IRX | ps2sdk's clean-room libsd values (freesd.c); the idle voice block at byte 0x5000 is a silent loop | no (audio only) | design (4A) | to check against the disc's LIBSD.IRX (R1 open question 1) |

## Optional features (off by default)

| option | hook | what changes |
|---|---|---|
