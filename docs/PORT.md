# The PC port

[ico-pc](https://github.com/nathanialf/ico-pc) is a fork of this repository
that is being turned into a native PC port (64-bit Linux and Windows). This
tree stays the byte-matched reference: it rebuilds the PAL boot ELF with the
period toolchain and holds no disc data. It is also the one source of the
game code for both builds: the port's `ico2/` and `sce/` are this
repository's, and the port compiles them with a modern host compiler and
`ICO_HOST` defined.

## The host branches in `ico2/` and `sce/`

The port's changes to the game code live here, and none of them changes
what the period compiler emits. They take three forms:

- **EE-neutral spellings.** A pointer-wide word is `ICO_WORD` or
  `ICO_WORD_PTR(T)` (`int` on the EE; `common/include/typedef.h`), a raw
  EE offset that has to stay spelled as one is `ICO_RAW` / `ICO_RAWP`, a
  literal allocation size is `ICO_MAX_SIZE` (both in
  `fumi/include/ee_view.h`), and a word inside a frozen disc record is one of
  the `common/include/eeword.h` forms. On the EE each expands to the tokens
  the code had before.
- **`#ifdef ICO_HOST`** (and `#ifdef ICO_RD`, the port's renderer, which
  only the host defines) with this tree's text in `#else`, where a host
  spelling would change the EE's code. The port's file-scope versions of GNU
  nested functions (clang has none) are the largest group: the lifted static
  is in the `ICO_HOST` arm and the nested original in `#else`, and a call
  site whose arguments changed is gated the same way.
- **Host-only declarations and helpers** inside those arms (`GifHost.h`,
  `Tim2.h`, the libsndn2 host seams in `sce/libsndn2/sound.c`).

The rule for a change to `ico2/` or `sce/` is the same whether it comes from
the port or from matching work: the byte-match gate (`./build.sh`,
`tools/check_elf.py --gate --require-elf-sha`) passes. A host change that
moves EE bytes goes under `#ifdef ICO_HOST` with the original text in
`#else`.

## What flows between the two

Port to this repository, through the gate above:

- reconstruction bugs the port finds (a wrong type, field, operand or
  control flow in a function); they are fixed here first, under this
  repository's rules (byte match, period toolchain, hooks);
- names for functions, globals, fields and constants;
- struct and union layouts, and header declarations;
- the port's host branches in `ico2/` and `sce/`, as described above.

What stays only in the port:

- the host build (CMake, host toolchain fetch, source list generation);
- the platform layer that replaces the PS2 hardware, IOP and libraries
  (pad, memory card, sound, CD/DVD, DMA, timers, threads);
- the renderer that replaces the GS and VU1 paths.

## How the port consumes this repository

The port has this repository as a remote named `upstream` and merges from
`main`:

```sh
git remote add upstream https://github.com/nathanialf/ico.git
git fetch upstream
git merge upstream/main
```

After the merge the port's `ico2/` and `sce/` equal `upstream/main`'s. Only
`main` is merged; the `ntsc` and `aug6` branches are not part of the port.

## Byte matching

The port does not check SHA-1s or compare against the base ELF; this
repository's gate does that for the shared code. The port's divergences from
the PS2 behaviour are listed in its `docs/port/DIVERGENCES.md`. The
byte-match gate here stays mandatory for every commit.
