# The port and the decompilation

This repository, ico-pc, is the native PC port (64-bit Linux and Windows).
Its game code started as a fork of the ICO decompilation
(<https://github.com/nathanialf/ico>), which stays the byte-matched
reference: the decompilation rebuilds the PAL boot ELF with the period
toolchain and holds no disc data.

## `ico2/` is the port's source

`ico2/` (and `sce/` and `ico2/vusrc/` where the port compiles them) is the
port's own source, compiled only for the host with a modern compiler. A
change to it is a platform change made directly in the code: there are no
`#ifdef ICO_HOST` / `#else` arms carrying the PS2 spelling, and the tree is
not required to compile for the PS2 or to match the ROM
([`BUILDING.md`](BUILDING.md), "The game code"; `tools/strip_host_gates.py
--check` enforces the first part).

## Reconstruction fixes (the decompilation is upstream for these only)

Anything about what the original program is, not about where it runs, is
decided in the decompilation:

- reconstruction bugs (a wrong type, field, operand or control flow in a
  function);
- names for functions, globals, fields and constants;
- struct and union layouts, and header declarations.

Such a fix flows one way, case by case:

1. it is found in the port (a bug report, a trace that drifts, a crash);
2. it is verified in the decompilation against the ROM with that
   repository's tooling (its byte-match build and checks), so the fix keeps
   the match;
3. it is committed there under its rules (byte match, period toolchain,
   hooks);
4. it is applied here by hand, to the host form of the code, with a
   reference to the decompilation's commit.

A change found to be a platform matter (the host's pointer width, the
renderer, the platform layer) stays here and does not go to the
decompilation.

## What only the port has

- the host build (CMake, the host toolchain fetch, source list generation);
- the platform layer that replaces the PS2 hardware, the IOP and Sony's
  libraries (pad, memory card, sound, CD/DVD, DMA, timers, threads);
- the renderer that replaces the GS and VU1 paths;
- the host form of the game code in `ico2/`.

## Byte matching

The port does not check SHA-1s of its build or compare against the base
ELF. Its divergences from the PS2 behaviour are listed in
[`port/DIVERGENCES.md`](port/DIVERGENCES.md). Byte matching is the
decompilation's gate, and only for the reconstruction fixes above.
