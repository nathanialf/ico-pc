# R5: dependency licences and provenance

Work package 0C. Checked 2026-10-05. Licence identifiers are the ones GitHub
reports for the repository (`gh api repos/<owner>/<repo> --jq .license`)
unless the text says the file itself was read; where GitHub reports
`NOASSERTION` the licence file was read.

## What the repository's own rules require of new code

- `LICENSE`: MIT, "Copyright (c) 2026 the ico decompilation contributors".
  New port code (`port/`, tools) is MIT under that notice. No per-file
  header is required by the licence.
- `docs/LEGAL.md`: no disc data, no ELF bytes, no bulk symbol/map tables, no
  leaked SDKs or source; public open-licence code may be used "the same way"
  as reverse-engineering references, i.e. read and re-derive, and newlib and
  libgcc members "follow newlib's and GCC's published source, whose licences
  permit it" (`docs/LEGAL.md`, "Public reverse-engineering material").
- Plan (`pure-questing-lark.md`, Architecture/Dependencies): no GPL code;
  LGPL FFmpeg only as a dynamically loaded fallback.
- Consequence: every third-party library vendored into the port must be
  MIT-compatible for static linking into a binary distributed under MIT,
  and its notice must ship with the binary (a `THIRD_PARTY_NOTICES` file
  produced by the packaging package, Phase 5).

## Summary table

| dependency | licence | source checked | obligations in a binary release | verdict |
|---|---|---|---|---|
| SDL3 | zlib | https://github.com/libsdl-org/SDL (GitHub: `Zlib`; latest release `release-3.4.18`, 2026-10-02) | none mandatory (zlib licence asks only that the notice stays in source distributions and altered versions are marked) | adopt |
| minicoro | Unlicense (public domain) or MIT-0, choice | https://github.com/edubart/minicoro `LICENSE` (read; GitHub shows `NOASSERTION` because of the dual text); header says v0.2.0, 2023-11-15; last push 2024-12-07 | none | adopt, with the technical notes below |
| miniz | MIT | https://github.com/richgel999/miniz (GitHub: `MIT`; release 3.1.2, 2026-07-01) | keep notice | adopt |
| Ittiam libmpeg2 | Apache-2.0 | https://android.googlesource.com/platform/external/libmpeg2 (AOSP), mirror https://github.com/ittiam-systems/libmpeg2 (GitHub: `Apache-2.0`; `NOTICE` read: "Copyright (C) 2015 The Android Open Source Project", Apache 2.0; last commit 2026-07-01) | ship `LICENSE` and `NOTICE`; state changes if modified (Apache-2.0 s.4) | adopt (Apache-2.0 is compatible with distribution of an MIT-licensed program as a combined binary; the MIT code stays MIT) |
| stb_truetype | MIT or Unlicense, choice | https://github.com/nothings/stb `LICENSE` (read) | none if Unlicense is chosen | adopt |
| rcheevos | MIT | https://github.com/RetroAchievements/rcheevos (GitHub: `MIT`; v12.5.0, 2026-09-14) | keep notice | licence fine; use is a policy question (retroachievements.md) |
| DXC | University of Illinois/NCSA (LLVM release licence) plus bundled third-party notices | https://github.com/microsoft/DirectXShaderCompiler `LICENSE.TXT` (read; GitHub shows `NOASSERTION`); latest release v1.9.2609, 2026-09-29, assets `dxc_2026_09_29.zip`, `linux_dxc_2026_09_28.x86_x64.tar.gz` | build-time only: nothing ships. Compiler output (SPIR-V/DXIL blobs) is not a derivative of the compiler | adopt as a pinned build tool |
| SPIRV-Cross | Apache-2.0 | https://github.com/KhronosGroup/SPIRV-Cross (GitHub: `Apache-2.0`) | build-time only if used to emit MSL for a future Mac backend; nothing ships | optional; not needed for Vulkan (SPIR-V from DXC) or D3D12 (DXIL from DXC) |
| newlib (rand, qsort, libm float functions) | per-file: Red Hat BSD (rand), UCB 3-clause BSD (qsort), SunPro fdlibm notice (libm) | see newlib section | notices in the binary's third-party file; see the finding below about the tree's copies | keep the tree's copies, add notices |

## SDL3

zlib licence (https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt).
Permissive, static linking fine, no notice required in binaries. Covers
window, gamepad (with rumble), audio output and pref paths as the plan
needs.

## minicoro

Dual Unlicense / MIT-0 (`LICENSE` in the repository: "ALTERNATIVE 1 -
Public Domain (www.unlicense.org)"; alternative 2 is MIT No Attribution).
No obligations.

Technical notes that matter for package 1B (from reading `minicoro.h`
at https://raw.githubusercontent.com/edubart/minicoro/main/minicoro.h):

- Backend selection (`minicoro.h` around line 378): on `_WIN32` it uses the
  assembly switch only for x86-64 (GCC or MSVC); every other Windows target,
  including the 32-bit `win-x86-ref` preset, falls back to Windows fibers
  (`MCO_USE_FIBERS`). On Linux with GCC/clang the assembly switch is used for
  x86-64, i386, ARM EABI, AArch64 and RISC-V. So the 32-bit Windows oracle
  and the 64-bit Windows build use different context-switch code.
- A grep of `minicoro.h` for `mxcsr`, `fnstcw`, `fldcw`, `ldmxcsr`, `fpcr`
  and `fpscr` finds nothing: the assembly switch does not save or restore
  the floating-point control state. With all game fibers on one OS thread
  and one FP environment set once (float-semantics.md) this is harmless;
  a fiber must never change MXCSR or the x87 control word locally. Windows
  fibers only switch FP state when created with `FIBER_FLAG_FLOAT_SWITCH`
  (Win32 `CreateFiberEx` documentation,
  https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createfiberex);
  minicoro's fiber path should be checked for which flag it passes.
- The project is quiet (last push 2024-12-07). It is a single header the
  port would vendor and own; an unmaintained upstream is acceptable.

## miniz

MIT (https://github.com/richgel999/miniz/blob/master/LICENSE). Active
(release 3.1.2, 2026-07-01). Used for the extracted asset archive.

## MPEG-2 decoding

### Ittiam libmpeg2 (AOSP `external/libmpeg2`)

Apache-2.0 (`NOTICE`, `MODULE_LICENSE_APACHE2` in the repository root).
Maintained as part of AOSP (last commit 2026-07-01, "Optimize memset calls
and clean unused assembly function pointers"). Generic C decoder plus
optional `arm`, `x86` and `riscv` assembly selected by
`impeg2d_function_selector_*.c`; the port should build the generic C path
first (portable to Mac/iOS/Android, matches the plan's "no x86-only
choices") and enable SIMD later. It is fuzzed in AOSP (`fuzzer/`), which
matters for a decoder fed from a user-supplied disc.

Apache-2.0 obligations for a binary: include the licence text and the
`NOTICE` content, and mark modified files (Apache-2.0 sections 4(a)-(d),
https://www.apache.org/licenses/LICENSE-2.0). Apache-2.0 code may be
combined with MIT code in one binary; the combined binary's notices must
carry both.

### Alternatives

| candidate | licence | status | fit |
|---|---|---|---|
| libmpeg2 (Michel Lespinasse, the original "libmpeg2") | GPL-2.0-or-later (https://en.wikipedia.org/wiki/Libmpeg2) | unmaintained since 2008 per the article | excluded: GPL |
| FFmpeg `mpeg12dec` | LGPL-2.1-or-later (GPL if built with GPL parts) | active | only as the plan's dynamically loaded fallback |
| pl_mpeg | MIT (`README.md`: "Single-file MIT licensed library") https://github.com/phoboslab/pl_mpeg | active (push 2025-12-30) | MPEG-1 video only ("MPEG1 Video decoder, MP2 Audio decoder, MPEG-PS demuxer"); does not decode MPEG-2 |
| write our own | MIT | n/a | see below |

No maintained, permissively licensed, MPEG-2-capable decoder other than
Ittiam's was found (searched GitHub and the Wikipedia libmpeg2 article; not
exhaustive).

Writing a small decoder (inference, sized from the specification, not
measured): the PS2 IPU decodes MPEG-2 Main Profile at Main Level content
(4:2:0, I/P/B pictures). A decoder for exactly what the game's streams
use, frame pictures only if that holds, is a VLC table set, inverse
quantisation (MPEG-2 rules, `intra_dc_precision`, `q_scale_type`,
alternate scan), IDCT, and motion compensation with half-pel prediction, plus
field prediction if any stream uses it. pl_mpeg shows the MPEG-1 subset fits
in about 4,000 lines including audio and demux; MPEG-2 adds the extension
headers and field/interlace modes. Feasible, but the IDCT accuracy rules
(IEEE 1180) and field motion compensation are where bugs hide, and a
fuzzed decoder exists. Recommendation: adopt Ittiam libmpeg2; revisit only
if its build or API proves hostile. Before that, the FMV package (Phase 4)
should confirm the streams' actual profile: the movies are members of
`DFDATAS/DATA.DF` (the ISO root holds only that pack plus the boot files,
checked with pycdlib), so the check needs the pack reader.

Patents: the last US MPEG-2 video patent expired on 2018-02-13
(https://www.phoronix.com/news/MPEG-2-Last-Patents-Expire;
https://www.theregister.com/2018/02/15/world_farewells_the_last_mpeg2_patent/).
The pool administrator is now Via LA (https://www.via-la.com/licensing-programs/mpeg-2/);
a search summary states one Malaysian patent remains listed until 2035,
not verified against Via LA's patent list. Treat MPEG-2 decoding as
unencumbered except possibly in Malaysia.

## stb_truetype

`stb` `LICENSE` (read): "ALTERNATIVE A - MIT License ... ALTERNATIVE B -
Public Domain (www.unlicense.org)". Choose the Unlicense, no obligations.
The font it rasterises (plan: "new port-owned open-licence font") is a
separate licence decision; OFL-1.1 fonts may be bundled with an MIT program
as long as the font is not sold by itself (https://openfontlicense.org/).

## rcheevos

MIT (https://github.com/RetroAchievements/rcheevos/blob/develop/LICENSE).
The licence is not the obstacle; RetroAchievements' standalone policy is
(retroachievements.md).

## DXC and SPIRV-Cross (build time only)

DXC: `LICENSE.TXT` is the LLVM release licence (University of
Illinois/NCSA Open Source License) followed by "Copyrights and Licenses for
Third Party Software Distributed with LLVM" (read from
https://github.com/microsoft/DirectXShaderCompiler/blob/main/LICENSE.TXT).
Used only to compile HLSL to SPIR-V and DXIL in the build; neither DXC nor
its libraries ship. Open question for the renderer package: whether the
DXIL that the Linux-hosted DXC emits is signed/validated acceptably for
D3D12 without the Windows `dxil.dll` validator, and the licence of whatever
validator binary the release archives carry (not checked here; the release
body lists only the archive names).

SPIRV-Cross: Apache-2.0. Only needed if a Metal backend is added later
(SPIR-V to MSL). Build-time only.

## newlib: the functions the game needs

The tree already carries reconstructions of the newlib members the game
linked: `sce/libc/stdlib/rand.c`, `sce/libc/stdlib/qsort.c`,
`sce/libm/math/{ef_atan2,wf_atan2,ef_acos,wf_acos,ef_asin,wf_asin,ef_fmod,wf_fmod,ef_sqrt,sf_sin,kf_sin,kf_cos,sf_atan,ef_rem_pio2,kf_rem_pio2,sf_fabs,sf_floor,sf_isnan}.c`
and `sce/libm/common/{sf_copysign,sf_scalbn,s_matherr,s_lib_ver}.c`. There is
no `sf_cos.c` in the tree: either `cosf` is not linked or it comes from
another member; the math package should check the link map before assuming
`cosf` exists (open question).

Upstream licences (read from
https://sourceware.org/git/?p=newlib-cygwin.git, `HEAD`, 2026-10-05):

| function | upstream file | upstream licence |
|---|---|---|
| `rand`, `srand` | `newlib/libc/stdlib/rand.c` | no notice in the file; `COPYING.NEWLIB` item (1), Red Hat, BSD licence |
| `qsort` | `newlib/libc/search/qsort.c` | Regents of the University of California, 3-clause BSD (retain notice in source; reproduce in binary documentation) |
| `atan2f`, `acosf`, `asinf`, `sinf`, `cosf`, `fmodf`, `sqrtf` | `newlib/libm/math/ef_*.c`, `kf_*.c`, `sf_*.c`, `wf_*.c` | "Copyright (C) 1993 by Sun Microsystems, Inc. ... Permission to use, copy, modify, and distribute this software is freely granted, provided that this notice is preserved." |

Finding: a grep for "copyright" over `sce/libm` finds no file carrying a
notice (0 of 22 files), and `sce/libc/stdlib/{rand,qsort}.c` carry none
either; they open with `/* libc.a member rand.o */` and similar. They are
reconstructions of the linked binary that, per `docs/LEGAL.md`, "follow
newlib's ... published source", so they reproduce that source's
structure. The conservative reading is that the fdlibm and UCB notices
should be preserved in these files, and in the port's binary notices
(UCB clause 2). Recommendation for the owning package (1A, which moves or
wraps these for the port): add the upstream notice to each reconstructed
newlib file when it is carried into `port/` (an ASCII-only edit; these are
not EUC-JP files), and list newlib in the third-party notices. This is a
licence-hygiene recommendation, not a legal opinion.

Determinism note (for 1A): the port must keep these exact implementations
rather than the host libm, because results feed discrete state (plan,
Math). `rand` here is the 31-bit LCG `s = s * 0x41C64E6D + 0x3039`, state at
`_impure_ptr + 0x58` (`sce/libc/stdlib/rand.c:15`), a raw-offset access the
64-bit sweep must keep pointing at a real `int` field.

## Open questions

1. `cosf`: which member provides it (no `sf_cos.c` in `sce/libm/math`).
2. DXIL validation/signing when compiling on the Linux container.
3. minicoro's Windows-fiber path: confirm it creates fibers without
   `FIBER_FLAG_FLOAT_SWITCH`, so the FP environment set on the sim thread
   is shared by all fibers.
4. The FMV streams' MPEG-2 profile (frame vs field pictures), to confirm
   the decoder needs no features beyond libmpeg2's MP@ML support.
