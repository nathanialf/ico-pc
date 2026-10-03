# ico-pc

A native PC port of **ICO** (Sony Computer Entertainment, 2001), built from
the C source of the [ICO decompilation](https://github.com/nathanialf/ico).

> [!IMPORTANT]
> This project is not affiliated with Sony Interactive Entertainment or Team
> Ico. *ICO* is a trademark of its owners. No game data is in this
> repository: you supply your own PAL disc image (SCES-50760), and the port
> reads the game's assets from it. Read [`docs/LEGAL.md`](docs/LEGAL.md).

## Status

Early. The tree is the decompilation as it was when this port was forked,
and nothing runs natively yet. It still builds a PS2 ELF with the period
toolchain, which runs in an emulator as a reference for the game's
behaviour. The build does not require it to match the disc byte for byte:
the game code is free to change.

## Goal

The game code under `ico2/` compiles with a modern host compiler and runs
natively, on top of a platform layer that replaces the PS2 hardware and
Sony's runtime libraries. The behaviour target is the original game:
the same logic, timing and output, with improvements kept optional.

## Work ahead

- [ ] **Host build.** A second build (CMake or similar) that compiles
      `ico2/` for 64-bit Linux and Windows next to the PS2 build, and
      fixes what that exposes: 32-bit pointer and `int` assumptions,
      MIPS/R5900-specific inline assembly and 128-bit types, alignment.
- [ ] **Runtime libraries.** Replacements for what the game takes from
      `sce/`: kernel and threads (libkernl), IOP RPC (sif), DMA and packets (libdma,
      libpkt), the GS (libgraph), controllers (libpad), memory card
      (libmc), the disc (libcdvd), video (libmpeg, libipu), sound
      (libsndn2) and the VU0 maths (libvu0).
- [ ] **Renderer.** The GS packets and the five VU1 microprograms in
      `ico2/vusrc/` (cluster, mesh, normal_c, normal_l, particle) turned
      into a modern graphics API.
- [ ] **Assets.** Read the game's files from the user's disc image at run
      time. The data tables the build already generates from the user's
      ELF (`build/data/`) are the model: nothing from the disc is committed.
- [ ] **Input, audio, saves and video playback** on the host.
- [ ] **PC features:** resolution, aspect ratio, frame rate and controller
      remapping, behind options that default to the original behaviour.

## Building the PS2 ELF

```sh
git clone https://github.com/nathanialf/ico-pc.git
cd ico-pc
mkdir -p baserom
cp "/path/to/Ico (Europe).iso" baserom/Ico_PAL.iso
./build.sh
```

`./build.sh` installs the period toolchain under `tools/cc/` on the first
run, extracts the boot ELF from the disc image and builds `build/ico.elf`.
The build reads the game's data tables from the extracted ELF, so it
accepts only the PAL retail disc (SHA-1s below). The host
needs a 64-bit Linux with 32-bit libraries, a host gcc,
`mips-linux-gnu-objcopy` and network access for the first run.
[`docs/BUILDING.md`](docs/BUILDING.md) lists the packages and each step.

| file | SHA-1 |
| --- | --- |
| `baserom/pal/baseelf.elf` (the disc's `SCES_507.60`) | `da3644c54c26fe760f3b6a591a5fc2eab396ed2b` |
| `baserom/pal/baseelf.rom` (`objcopy -O binary`) | `a401d1e5a20b1659189a8b1026a8eb35811dc9ca` |

## Layout

```
ico2/          the game: one directory per subsystem, each with src/ and
               include/ (common, fumi, ito, omori, script, seki, sugipon)
ico2/vusrc/    the five VU1 microprograms and their shared includes
sce/           Sony's runtime libraries, newlib libc and libm, libgcc and
               crt0.s, as the game linked them
config/        link order, linker script, data-member lists, SHA-1s
tools/         setup, extraction and build scripts (tools/README.md)
docs/          documentation (docs/README.md)
baserom/       local only, gitignored: your disc image and extracted ELF
build/         local only, gitignored: build output
```

## Syncing with the decompilation

The decompilation keeps improving names, types and headers. Pull its
changes into the port with:

```sh
git remote add upstream https://github.com/nathanialf/ico.git
git fetch upstream
git merge upstream/main
```

## Legal and licence

The code in this repository is MIT licensed ([`LICENSE`](LICENSE)). The
licence covers the code written for this project and grants no rights in the
game, its data or anything else owned by Sony Interactive Entertainment or
Team Ico. [`docs/LEGAL.md`](docs/LEGAL.md) says what may and may not be in
the repository.
