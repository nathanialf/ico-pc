# ico-pc

A native PC port of **ICO** (Sony Computer Entertainment, 2001), built from
the C source of the [ICO decompilation](https://github.com/nathanialf/ico).

> [!IMPORTANT]
> This project is not affiliated with Sony Interactive Entertainment or Team
> Ico. *ICO* is a trademark of its owners. No game data is in this
> repository: you supply your own PAL disc image (SCES-50760), and the port
> reads the game's assets from it. Read [`docs/LEGAL.md`](docs/LEGAL.md).

## Status

The game is playable on 64-bit Windows and Linux, including the Steam
Deck, from your own image of the PAL disc. The game's C
runs natively on a platform layer that replaces the PS2 hardware and Sony's
libraries; it draws through a Vulkan renderer (a Direct3D 12 backend is
built for Windows too), plays its sound and films, and reads its data from
an archive that the first run extracts from your disc image. The behaviour
target is the original game: the same logic, timing and pictures, with every
improvement (resolution, widescreen, frame interpolation, 60 Hz, controller
remapping, gameplay options, achievements) behind a setting that defaults to
the original. What is known not to be done or not yet verified, such as the
D3D12 backend on real hardware and native-speaker review of the
translations, is listed in [`docs/TODO.md`](docs/TODO.md).

## Building and running

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
checks it and extracts the game's data once; no command-line options are
needed. Windows builds are cross-compiled from Linux (`--preset win-x64`).
[`docs/BUILDING.md`](docs/BUILDING.md) has the presets, the tests, CI and
the packages; [`docs/port/STEAMDECK.md`](docs/port/STEAMDECK.md) covers the
Linux package and the Steam Deck.

## Documentation

[`docs/README.md`](docs/README.md) is the index. Start with
[`docs/port/CONFIG.md`](docs/port/CONFIG.md) for the settings,
[`docs/port/DIVERGENCES.md`](docs/port/DIVERGENCES.md) for where the port
knowingly differs from the PS2, and
[`docs/port/TESTING.md`](docs/port/TESTING.md) for testing and reporting
bugs.

## Layout

```
ico2/          the game: one directory per programmer, each with src/ and
               include/ (common, fumi, ito, omori, script, seki, sugipon)
ico2/vusrc/    the five VU1 microprograms, the renderer's shaders' reference
sce/           Sony's runtime libraries, newlib and libgcc as the game linked
               them: the reference for the optional EE identity check, not
               part of any host build
port/          the platform layer, renderer, audio, input, data and the rest
               of the port
cmake/         the host build's toolchain files and source lists
config/        the source list (link order), data-table schema and manifest,
               SHA-1s
tools/         toolchain fetch, packaging, generators and checks
               (tools/README.md)
docs/          documentation (docs/README.md)
baserom/       local only, gitignored: your disc image
build-host/    local only, gitignored: build output
```

## The decompilation

This repository is a fork of the decompilation, which stays the byte-matched
reference and is the one source of the game code: `ico2/` and `sce/` here
equal its `main`, host changes included (gated so the PS2 build is
unchanged). Names, types, fixes and host changes land there first and come
into the port with `git merge upstream/main`. [`docs/PORT.md`](docs/PORT.md) describes the relationship.

## Legal and licence

The code in this repository is MIT licensed ([`LICENSE`](LICENSE)). The
licence covers the code written for this project and grants no rights in the
game, its data or anything else owned by Sony Interactive Entertainment or
Team Ico. [`docs/LEGAL.md`](docs/LEGAL.md) says what may and may not be in
the repository, and [`docs/port/THIRD_PARTY.md`](docs/port/THIRD_PARTY.md)
lists the third-party code the program uses.
