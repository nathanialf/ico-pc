# The PC port

[ico-pc](https://github.com/nathanialf/ico-pc) is a fork of this repository
that is being turned into a native PC port (64-bit Linux and Windows). This
tree stays the byte-matched reference: it rebuilds the PAL boot ELF with the
period toolchain and holds no disc data. The port keeps the game code under
`ico2/` but builds it with a modern host compiler and drops byte matching.

## What flows upstream (port to this repo)

Anything about what the original program is, not about where it runs:

- reconstruction bugs the port finds (a wrong type, field, operand or
  control flow in a function); they are fixed here first, under this
  repository's rules (byte match, period toolchain, hooks), then merged
  into the port;
- names for functions, globals, fields and constants;
- struct and union layouts, and header declarations.

## What stays only in the port

- the host build (CMake, host toolchain fetch, source list generation);
- the platform layer that replaces the PS2 hardware, IOP and libraries
  (pad, memory card, sound, CD/DVD, DMA, timers, threads);
- the renderer that replaces the GS and VU1 paths;
- host-only edits to `ico2/` that exist to compile on a modern compiler or
  a 64-bit target, and everything that follows from dropping byte matching.

## How the port consumes this repository

The port has this repository as a remote named `upstream` and merges from
`main`:

```sh
git remote add upstream https://github.com/nathanialf/ico.git
git fetch upstream
git merge upstream/main
```

Conflicts in `ico2/` are resolved in favour of keeping the port's host
changes while taking the upstream names, types and fixes. Only `main` is
merged; the `ntsc` and `aug6` branches are not part of the port.

## Byte matching

The port does not check SHA-1s or compare against the base ELF. Its
divergences from the PS2 behaviour are listed in the port's
`docs/port/DIVERGENCES.md`. Nothing in this repository changes for the
port: the byte-match gate here stays mandatory for every commit.
