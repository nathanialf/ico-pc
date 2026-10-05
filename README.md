# ico-pc

A native PC port of **ICO** (Sony Computer Entertainment, 2001), built from
the C source of the [ICO decompilation](https://github.com/nathanialf/ico).

> [!IMPORTANT]
> This project is not affiliated with Sony Interactive Entertainment or Team
> Ico. *ICO* is a trademark of its owners. No game data is in this
> repository: you supply your own PAL disc image (SCES-50760), and the port
> reads the game's assets from it. Read [`docs/LEGAL.md`](docs/LEGAL.md).

## Status

In progress. The game's C builds natively for 64-bit Linux and Windows, runs
on a platform layer that replaces the PS2 hardware and Sony's libraries,
draws through a Vulkan renderer, plays sound and FMVs, and reads your own disc
image on the first run into a local archive. The renderer's remaining effects
and its Direct3D 12 backend, and the PC features below, are still to come
(`docs/port/` has the state of each part). The PS2 ELF build of the decompilation is not part of
this repository any more; the decompilation keeps it.

## Goal

The game code under `ico2/` compiles with a modern host compiler and runs
natively, on top of a platform layer that replaces the PS2 hardware and
Sony's runtime libraries. The behaviour target is the original game:
the same logic, timing and output, with improvements kept optional.

## Work ahead

- [x] **Host build.** CMake and Ninja compile `ico2/` for 64-bit Linux and
      Windows (`docs/BUILDING.md`).
- [x] **Runtime libraries.** Replacements for what the game takes from
      `sce/`: kernel and threads, IOP RPC, DMA and packets, the GS, controllers,
      memory card, the disc, video and sound (`port/`, `docs/port/`).
- [x] **Renderer.** The GS packets and the five VU1 microprograms in
      `ico2/vusrc/` turned into a modern graphics API (`docs/port/RENDER_API.md`).
      The game draws through the Vulkan backend. The Direct3D 12 backend is
      written and builds for Windows, but it has not been run yet
      (`docs/port/TESTING.md`, "Renderer wave 6: D3D12").
- [x] **Assets.** The first run verifies your disc image and extracts what the
      game reads into a local archive, `ico.o2r`, and the data tables load from
      its boot ELF (`docs/port/DATA.md`). Nothing from the disc is committed or
      compiled into the program.
- [x] **Input, audio, saves and video playback** on the host.
- [x] **Packaging and CI.** `tools/package_win.sh`, `tools/package_linux.sh`
      (`docs/port/STEAMDECK.md`), and `.github/workflows/ci.yml`.
- [ ] **PC features**, behind options that default to the original
      behaviour (`docs/port/DISPLAY.md`, `docs/port/SETTINGS.md`). Landed:
      resolution, aspect ratio, texture filtering and full height, the
      Settings menu, controller remapping, the gameplay options and the
      achievements. Still open: the frame rate option (frames drawn between
      the game's updates) is in progress and not committed, and none of this
      has been tested on Windows yet.

## Building

```sh
git clone https://github.com/nathanialf/ico-pc.git
cd ico-pc
sudo apt-get install build-essential curl xz-utils python3 python3-venv \
    libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev \
    libxfixes-dev libxrender-dev libasound2-dev
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
tools/fetch_toolchain.sh
tools/toolchain/cmake/bin/cmake --preset linux-x64 -DICO_HEADLESS=OFF -DICO_LINK_EXE=ON
tools/toolchain/cmake/bin/cmake --build build-host/linux-x64
```

The build needs no disc image. Run `build-host/linux-x64/ico_pc`: the first
run asks for your own image of the PAL disc (SCES-50760, a plain `.iso`),
checks it and extracts the game's data once; no options are needed.
[`docs/BUILDING.md`](docs/BUILDING.md) has the presets, the tests, CI and the
optional PS2-compiler identity check (`tools/ee_identity.sh`);
[`docs/port/STEAMDECK.md`](docs/port/STEAMDECK.md) covers the Linux package.

## Layout

```
ico2/          the game: one directory per subsystem, each with src/ and
               include/ (common, fumi, ito, omori, script, seki, sugipon)
ico2/vusrc/    the five VU1 microprograms and their shared includes
sce/           Sony's runtime libraries, newlib libc and libm, libgcc and
               crt0.s, as the game linked them: the EE identity check's
               reference, not part of any host target
port/          the platform layer, renderer, audio, input and the rest of the port
cmake/         the host build's toolchain files and source lists
config/        source list (link order), data-member lists, SHA-1s
tools/         toolchain fetch, packaging, generators and checks (tools/README.md)
docs/          documentation (docs/README.md)
baserom/       local only, gitignored: your disc image (and, for maintainers,
               its extracted boot ELF)
build-host/    local only, gitignored: build output
```

## Syncing with the decompilation

The decompilation keeps improving names, types and headers. Pull its
changes into the port with:

```sh
git remote add upstream https://github.com/nathanialf/ico.git
git fetch upstream
git merge upstream/main
```

The decompilation's description of this relationship is
[`docs/PORT.md`](https://github.com/nathanialf/ico/blob/main/docs/PORT.md).

## PC port

[ico-pc](https://github.com/nathanialf/ico-pc) is a fork of this repository
that is being turned into a native PC port. This tree remains the
byte-matched reference. Reconstruction bugs the port finds are fixed here
first, under this repository's rules, and then merged into the port. See
[`docs/PORT.md`](docs/PORT.md).

## Legal and licence

The code in this repository is MIT licensed ([`LICENSE`](LICENSE)). The
licence covers the code written for this project and grants no rights in the
game, its data or anything else owned by Sony Interactive Entertainment or
Team Ico. [`docs/LEGAL.md`](docs/LEGAL.md) says what may and may not be in
the repository.
