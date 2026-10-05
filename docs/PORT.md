# The port and the decompilation

This repository, ico-pc, is the native PC port (64-bit Linux and Windows).
It is a fork of the ICO decompilation
(<https://github.com/nathanialf/ico>), which stays the byte-matched
reference: the decompilation rebuilds the PAL boot ELF with the period
toolchain and holds no disc data. The port keeps the game code under
`ico2/` but builds it with a modern host compiler and does not byte-match.

## What goes upstream (port to decompilation)

Anything about what the original program is, not about where it runs:

- reconstruction bugs the port finds (a wrong type, field, operand or
  control flow in a function). They are fixed in the decompilation first,
  under its rules (byte match, period toolchain, hooks), then merged into
  the port;
- names for functions, globals, fields and constants;
- struct and union layouts, and header declarations.

## What stays in the port

- the host build (CMake, the host toolchain fetch, source list generation);
- the platform layer that replaces the PS2 hardware, the IOP and Sony's
  libraries (pad, memory card, sound, CD/DVD, DMA, timers, threads);
- the renderer that replaces the GS and VU1 paths;
- host-only edits to `ico2/` that exist to compile with a modern compiler or
  on a 64-bit target, and everything that follows from dropping byte
  matching.

## Merging from the decompilation

The port has the decompilation as a remote named `upstream` and merges from
its `main`:

```sh
git remote add upstream https://github.com/nathanialf/ico.git
git fetch upstream
git merge upstream/main
```

Conflicts in `ico2/` are resolved by keeping the port's host changes while
taking the upstream names, types and fixes. Only `main` is merged; the
decompilation's `ntsc` and `aug6` branches are not part of the port.

## Byte matching

The port does not check SHA-1s of its build or compare against the base
ELF. Its divergences from the PS2 behaviour are listed in
[`port/DIVERGENCES.md`](port/DIVERGENCES.md). A host-only change to `ico2/`
that should leave the PS2 compiler's output unchanged can be checked with
the optional EE identity check ([`BUILDING.md`](BUILDING.md), appendix). The
decompilation's byte-match gate is unaffected by the port and stays
mandatory there for every commit.
