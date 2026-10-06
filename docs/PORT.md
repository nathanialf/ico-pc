# The port and the decompilation

This repository, ico-pc, is the native PC port (64-bit Linux and Windows).
It is a fork of the ICO decompilation
(<https://github.com/nathanialf/ico>), which stays the byte-matched
reference: the decompilation rebuilds the PAL boot ELF with the period
toolchain and holds no disc data. It is also the one source of the game
code: the port's `ico2/` and `sce/` equal the decompilation's `main`, and the
port builds them with a modern host compiler and `ICO_HOST` defined, without
byte matching.

## What goes upstream (port to decompilation)

Anything about what the original program is, not about where it runs:

- reconstruction bugs the port finds (a wrong type, field, operand or
  control flow in a function). They are fixed in the decompilation first,
  under its rules (byte match, period toolchain, hooks), then merged into
  the port;
- names for functions, globals, fields and constants;
- struct and union layouts, and header declarations;
- every host change to `ico2/` and `sce/`. It is spelled so the period
  compiler sees the decompilation's tokens (`ICO_WORD`, `ICO_RAW`,
  `ICO_MAX_SIZE`; [`port/OFFSET_AUDIT.md`](port/OFFSET_AUDIT.md)
  "Conventions in `ico2/`"), or it sits under `#ifdef ICO_HOST` (or
  `ICO_RD`) with the decompilation's text in `#else`, as the file-scope
  versions of GNU nested functions do. It lands in the decompilation through
  its byte-match gate and comes back with the next merge.

## What stays in the port

- the host build (CMake, the host toolchain fetch, source list generation);
- the platform layer that replaces the PS2 hardware, the IOP and Sony's
  libraries (pad, memory card, sound, CD/DVD, DMA, timers, threads);
- the renderer that replaces the GS and VU1 paths.

## Merging from the decompilation

The port has the decompilation as a remote named `upstream` and merges from
its `main`:

```sh
git remote add upstream https://github.com/nathanialf/ico.git
git fetch upstream
git merge upstream/main
```

After a merge `git diff upstream/main -- ico2 sce` is empty: a conflict
there means a change was made on the port's side, and it belongs in the
decompilation instead. Only `main` is merged; the decompilation's `ntsc` and
`aug6` branches are not part of the port.

## Byte matching

The port does not check SHA-1s of its build or compare against the base
ELF. Its divergences from the PS2 behaviour are listed in
[`port/DIVERGENCES.md`](port/DIVERGENCES.md). The shared code is held to
the decompilation's byte-match gate, mandatory there for every commit; the
optional EE identity check ([`BUILDING.md`](BUILDING.md), appendix) tries a
change against the period compiler before it goes upstream.
