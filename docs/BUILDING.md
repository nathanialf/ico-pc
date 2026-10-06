# Building

How to build, test and package the PC port. The build is CMake with Ninja.
It compiles the game's C under `ico2/` for the host, with `port/` standing in
for the PS2 hardware and Sony's libraries. The PS2 ELF build belongs to the
decompilation (<https://github.com/nathanialf/ico>); this repository has
none (["The game code"](#the-game-code)).

## Quickstart (Debian 13, Ubuntu 24.04)

```sh
sudo apt-get install build-essential curl xz-utils python3 python3-venv \
    libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev \
    libxfixes-dev libxrender-dev libasound2-dev
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
tools/fetch_toolchain.sh          # about 1.6 GB under tools/toolchain/, no root
CMAKE=tools/toolchain/cmake/bin
$CMAKE/cmake --preset linux-x64 -DICO_HEADLESS=OFF -DICO_LINK_EXE=ON
$CMAKE/cmake --build build-host/linux-x64
$CMAKE/ctest --test-dir build-host/linux-x64
```

`tools/setup.sh` does the venv step and installs the git hooks (below); it
also checks for a MIPS objcopy unless `SKIP_TOOLCHAIN=1`, and only the
appendix needs that.

The build needs no disc image and no `baserom/`: the program holds no disc
data. The game reads the player's own PAL disc image (SCES-50760) at run
time. The first run extracts it into the archive `ico.o2r`, and the data
tables load from that archive at boot ([`port/DATA.md`](port/DATA.md)).

## Toolchains: `tools/fetch_toolchain.sh`

Run it once. It needs `curl`, `tar`, `sha256sum` and `dpkg-deb`, no root,
and fills `tools/toolchain/` (gitignored, about 1.6 GB):

| directory | what | from |
| --- | --- | --- |
| `llvm-mingw/` | clang 23, lld and the mingw-w64 UCRT runtime for x86-64 Windows; the same clang targets Linux | [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) release 20260922, `ucrt-ubuntu-22.04-x86_64`, SHA-256 pinned |
| `mingw-gcc/` | mingw-w64 gcc 14 and binutils for x86-64 Windows | Debian 13 `gcc-mingw-w64-*-win32` 14.2.0-19+27+b1, `binutils-mingw-w64-*` 2.44-3+12+b1, `mingw-w64-*-dev` 12.0.0-5, SHA-256 pinned |
| `cmake/` | CMake 4.4.4 (`cmake`, `ctest`) | [Kitware's release](https://github.com/Kitware/CMake/releases/tag/v4.4.4) `cmake-4.4.4-linux-x86_64.tar.gz`, SHA-256 pinned |
| `deps/` | SDL3, volk, the Vulkan headers, the validation layer, DXC and libmpeg2 | `tools/fetch_deps.sh`, which `fetch_toolchain.sh` runs last ([`port/THIRD_PARTY.md`](port/THIRD_PARTY.md)) |

The Debian packages come from `deb.debian.org`, falling back to
`snapshot.debian.org` once a version is superseded. `SKIP_MINGW_GCC=1` and
`SKIP_CMAKE=1` skip the mingw-gcc tree and CMake; the header of each script
lists its other overrides. The toolchain files take `ICO_LLVM_MINGW` and
`ICO_MINGW_GCC` from the environment to use copies elsewhere.

The Linux presets use the host's gcc 14 and glibc. Ninja comes from
`.venv/bin` (`tools/requirements.txt`) or the `PATH`. Any CMake 3.25 or later
on the `PATH` works in place of the pinned one.

## Presets

```sh
CMAKE=tools/toolchain/cmake/bin
$CMAKE/cmake --preset win-x64
$CMAKE/cmake --build --preset win-x64
$CMAKE/ctest --preset linux-x64   # the unit tests, on the Linux presets
```

`-B <dir>` after `--preset` builds a preset into another directory, which is
how several configurations of one preset live side by side under
`build-host/`. The build writes nothing outside its build directory.

| preset | target | compiler |
| --- | --- | --- |
| `linux-x64` | Linux x86-64, headless by default | host gcc 14 |
| `win-x64` | Windows x64, window build | `mingw-gcc` x86-64 |
| `asan` | Linux x86-64, `-fsanitize=address,undefined`, `-O1` | host gcc 14 |
| `fptrap` | `linux-x64` with float divide-by-zero and invalid unmasked in the simulation | host gcc 14 |
| `linux-x64-clang`, `win-x64-clang` | the same two targets, headless | llvm-mingw clang 23 (the host glibc for Linux) |

The port targets 64-bit x86 only.

### Options

| cache variable | default | effect |
| --- | --- | --- |
| `ICO_HEADLESS` | `ON` on every preset except `win-x64` | `OFF` is the window build: the game draws through `port/render` (`ICO_RD=1`) into an SDL3 window. `ON` is the headless build for trace and test runs: no window and no renderer, `ICO_HEADLESS=1` ([`port/TESTING.md`](port/TESTING.md), [`port/HEADLESS_STUBS.md`](port/HEADLESS_STUBS.md)). Both compile the same game sources |
| `ICO_LINK_EXE` | `OFF` | links `ico_pc` (`port/platform/main_host.c`), the game's program. Without it only the libraries and the tests are built |
| `ICO_STRICT_WARNINGS` | `OFF` | makes `-Wreturn-type`, `-Wimplicit-function-declaration` and `-Wstrict-prototypes` errors. While it is off, the C89-era diagnostics that modern compilers make errors by default (implicit declarations and int, int/pointer conversions, incompatible pointers, return mismatches) are warnings |
| `ICO_BUILD_BLOCKED` | `OFF` | also compiles the sources `cmake/IcoExclusions.cmake` leaves out (the list is empty today; it is the place to park a game source that stops compiling) |
| `ICO_HEAP_STATS` | `OFF` | the game's allocator (`fumi/ios/memory.c`) reports to `port/platform/arena.c`, which logs each heap partition's high-water mark |
| `ICO_FPTRAP` | `OFF` | set by the `fptrap` preset |
| `ICO_SANITIZE` | empty | the `-fsanitize=` list; the `asan` preset sets `address,undefined` |
| `ICO_RHI_D3D12` | `ON` for 64-bit Windows | builds the Direct3D 12 renderer backend next to the Vulkan one |
| `ICO_BASE_ELF` | `baserom/pal/baseelf.elf` | the base ELF the data loader's reference tests read; without it they are not built |
| `ICO_DXC`, `ICO_DEPS_DIR` | the fetched copies | the shader compiler and the dependency tree ([`port/SHADERS.md`](port/SHADERS.md)) |

## Compilers

GCC is the primary compiler, and the clang presets build the same sources;
CI builds both so neither drifts. The game originally used GNU C nested
functions, which clang does not implement; they have all been rewritten as
file-scope functions, so no game source needs GCC any more. llvm-mingw ships
no Linux sanitizer runtimes, so the `asan` preset is GCC either way.

Windows builds must target mingw (gcc or llvm-mingw), never MSVC (clang-cl
or a `*-windows-msvc` triple). Clang targeting MSVC evaluates call arguments
right to left, where ee-gcc and the GNU and mingw compilers go left to
right, and the game's results depend on that order at some call sites
([`research/compiler-semantics.md`](research/compiler-semantics.md)).

## How the game is compiled

`cmake/IcoSources.cmake` is written by `tools/gen_sources.py` from
`config/link_order.pal.txt`, the retail link's object list kept as the
source list (nothing links with it). It holds the `ico2/` C sources, one
list per programmer directory, the data-only members, and
`ICO_EE_ONLY_SOURCES`: the PS2's FMV player under `ito/mpeg/`, which
`port/fmv` replaces. Of `sce/` the build compiles only
`sce/libsndn2/sound.c`, the Sg sequencer (`port/audio`); the rest of `sce/`
and `ico2/vusrc/` stay in the tree as the decomp's sources, the SDK headers
the game includes, and the reference for the renderer's shaders. Configure
warns when the generated list is stale; rerun the script after changing the
link order.

Each programmer directory is one object library with the include path the
period build gave it (its own `include/`, then the others, then
`port/compat/` for the SDK header names), and `-fmacro-prefix-map` makes
`__FILE__` the period spelling (`src/main.c`), which the assert messages
print. The game's `main` is compiled as `ico_game_main`;
`port/platform/main_host.c` is the program's entry point. `ico_pc` links
the game objects (`ico_game`), the platform layer and the port's libraries,
plus the hardware floor that both builds keep in the program itself
(`port/null/gfx_null.c`, `port/null/libgcc_null.c`; HEADLESS_STUBS.md).

The game options (`cmake/IcoFlags.cmake`) are `-std=gnu11
-fno-strict-aliasing -fwrapv -ffp-contract=off -fno-fast-math
-fsigned-char -fno-common -fgnu89-inline` and `ICO_HOST=1`; the file's
comments say why each is there. The game and data objects alone also take
the EE's bit-field rule, `-mno-ms-bitfields`, on Windows; `port/` code keeps
the platform ABI, which SDL's and Windows' structs need. No configuration
defines `NDEBUG`: the retail game ran with its asserts, and
`port/compat/assert.h` routes them to `ico_assert`
(`port/platform/assert_host.c`), which logs and stops the run.

Rules for code that the game and the port share:

- A record that game code and `port/` code both read (SDK parameter blocks,
  the pad buffer, card directory entries) must have a layout that does not
  depend on `-mno-ms-bitfields`, or the `port/` side must be compiled with
  the game's layout options too. [`port/LAYOUT.md`](port/LAYOUT.md) covers
  record layouts and their asserts.
- `port/compat/eeregs.h` maps the EE's hardware registers to plain memory,
  so a loop that polls one (`GS_CSR`, a DMA channel's busy bit) never sees
  it change; such loops are replaced at their call sites
  ([`port/HW_ADDRESS_SITES.md`](port/HW_ADDRESS_SITES.md)).
- VU0 and R5900 inline assembly has C bodies over `port/math`
  ([`port/MATH.md`](port/MATH.md)); the compiled game sources hold no EE
  assembly and no EE opcode wrappers.
- The simulation runs with the floating-point environment
  `port/platform/fpenv.c` sets: `ico_fpenv_sim_enter()` selects round toward
  zero with flush-to-zero and denormals-are-zero (MXCSR; FPCR on arm64),
  `ico_fpenv_host_enter()` restores the host defaults for the window, SDL
  and the renderer. `fpenv_test` checks both, and on `fptrap` that a
  division by zero raises SIGFPE. Why, and where the port still differs
  from the EE, is in [`port/MATH.md`](port/MATH.md) and
  [`port/DIVERGENCES.md`](port/DIVERGENCES.md).

While `ICO_STRICT_WARNINGS` is off the build prints many C89-era warnings,
most of them `-Wstrict-prototypes`. The three warnings that option promotes
can become errors once their counts reach zero.

## The game code

`ico2/` (and `sce/` and `ico2/vusrc/` where the port compiles them) is the
port's own source. It started as the decompilation's `main`, but it is only
ever compiled for the host, so a change to it is a platform change made
directly in the code: the host form replaces the original spelling. There
are no `#ifdef ICO_HOST` / `#else` arms carrying the PS2 text, and the tree
is not expected to compile for the PS2 or to match the ROM. `ICO_HOST` stays
defined in the build (headers under `port/` test it); conditionals that
select between host build variants (`ICO_RD`, `ICO_HEADLESS`,
`ICO_HEAP_ASAN`, `ICO_FPTRAP`) stay too. `tools/strip_host_gates.py` is the
record of how the gated tree became this one (968 sites); its `--check`
runs in CI and in the pre-commit hook and fails on any `ICO_HOST`
conditional under `ico2/`, `sce/` or `vusrc/`.

The conventions that still matter on the host are in
[`port/OFFSET_AUDIT.md`](port/OFFSET_AUDIT.md), "Conventions in `ico2/`":
`ICO_WORD` for a word that holds an address (pointer-wide), `ICO_RAW` /
`ICO_RAWP` for a view of a record at an EE offset, `ICO_MAX_SIZE` for a host
record wider than the original literal, the layout asserts
([`port/LAYOUT.md`](port/LAYOUT.md)) and the template audit.

The decompilation (<https://github.com/nathanialf/ico>) is upstream for
reconstruction fixes only: a wrong type, field, operand or control flow in
a function, a name, a struct layout. Such a fix flows one way:

1. it is found in the port (a bug, a trace, a crash);
2. it is verified in the decompilation against the ROM with that
   repository's tooling (its byte-match build and checks);
3. it is committed there under its rules;
4. it is applied here by hand, to the host form of the code, with a
   reference to the decompilation's commit.

Nothing is merged from the decompilation, and platform changes never go
back to it. [`PORT.md`](PORT.md) has the same rule from the port's side.

## Data tables

The program holds no disc data. The game's data tables are defined empty
(`port/data/gen/table_defs.c`) and filled at boot from the boot ELF on the
player's disc (`port/data/tables.c`, [`port/DATA.md`](port/DATA.md)), each
range checked against the CRC-32 in `config/tables_manifest.txt`. The
generated descriptors under `port/data/gen/` are committed and carry no disc
bytes; `tools/gen_data_desc.py --check` keeps them fresh. Configuring and
building need no base ELF and no pyelftools. Only the loader's reference
test (`tables_loader`) and `tables_manifest` need a base ELF
(`ICO_BASE_ELF`, a maintainer step in the appendix), and they are left out
without one.

## EUC-JP sources

Some game sources carry Japanese text in EUC-JP, and their string literals
are the ROM's bytes. `.gitattributes` lists each of them with
`working-tree-encoding=EUC-JP`: git stores the file as UTF-8 and checks it
out as EUC-JP, which is what the compiler reads. An editor or a pipe that
rewrites one of these files as UTF-8 changes its bytes; change them with an
ASCII patch and `git apply`.

## Hooks

`tools/install_hooks.sh` (run by `tools/setup.sh`) installs a pre-commit
hook that runs, in order:

1. `tools/check_no_rom.sh`: refuses disc images, executables, extracted
   assets and large binaries ([`LEGAL.md`](LEGAL.md));
2. `tools/format.sh --check` on the staged C;
3. the three freshness checks CI also runs: `tools/gen_data_desc.py
   --check` (`port/data/gen/`), `tools/gen_layout_asserts.py --check`
   (`port/test/layout_asserts.c`) and `tools/gen_sources.py --check`
   (`cmake/IcoSources.cmake`). Each needs pyelftools, so the hook uses
   `.venv/bin/python`. Regenerate with the same script without `--check`;
4. `tools/strip_host_gates.py --check`: no `ICO_HOST` conditional in the
   game sources ([The game code](#the-game-code)).

`tools/format.sh` formats the tracked C with the tracked `.clang-format` and
then applies `tools/format_layout.py`'s top-level blank-line layout.

## Tests

`ctest` runs the unit tests on the Linux presets; the Windows presets build
the test executables without running them. A test that needs the disc
(`vfs_disc`, `archive_disc`) or a Vulkan device (the `rhi_vk*`, `rd_*`,
`shaders_pixel` and `vu1` tests) exits 77 without one, and
`SKIP_RETURN_CODE 77` makes ctest report it as skipped, which passes.
[`port/TESTING.md`](port/TESTING.md) covers running the game for tests.

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request: one Linux
job (`ubuntu-24.04`), no secrets, no disc image.

| step | what |
| --- | --- |
| host packages, venv | gcc 14 (the runner's default gcc 13 also configures, since `cmake/IcoFlags.cmake` drops warning flags a compiler does not know), the X11, ALSA and PulseAudio headers SDL3 builds against, lavapipe (`mesa-vulkan-drivers`, `libvulkan1`) so the render tests run on a CPU Vulkan device, `tools/requirements.txt` |
| cache and `tools/fetch_toolchain.sh` | `tools/toolchain/` is cached on the hash of `fetch_toolchain.sh` and `fetch_deps.sh`, restored and saved as separate steps so a cold fetch is saved even when a later step fails |
| `tools/check_no_rom.sh` | the IP scan over every tracked file |
| `tools/format.sh --check` | clang-format over the tracked C |
| `tools/strip_host_gates.py --check` | no `ICO_HOST` conditional in `ico2/`, `sce/`, `vusrc/` |
| `gen_data_desc.py`, `gen_layout_asserts.py`, `gen_sources.py` with `--check` | the generated files are fresh |
| `linux-x64` headless | configure with `-DICO_LINK_EXE=ON`, build, `ctest` |
| `linux-x64` window | `-DICO_HEADLESS=OFF -DICO_LINK_EXE=ON` into `build-host/linux-x64-window`, build, `ctest` |
| `linux-x64-clang` | build, `ctest` |
| `win-x64` | cross-compile the window build and `ico_pc.exe`; the tests are built, not run |

Run the same steps locally before pushing.

## Worktrees

`tools/worktree.sh add <id>` creates `/primary/dev/ico-pc-wt/<id>` on a new
branch `pkg/<id>` from `main` and symlinks the untracked inputs a fresh
worktree needs from the main checkout: `baserom`, `tools/toolchain` (compilers
and dependencies), `.venv` and `build-host/tmp` (the build and game-run locks,
scratch). Nothing else untracked is needed: the generated sources are
tracked. `.gitignore` ignores the links (`/baserom`, `/.venv`, `/tools/toolchain`
besides the directory forms), so `git status` stays clean. `add` refuses an
existing path or branch. Where `/tmp` is a small tmpfs, export
`TMPDIR=/primary/dev/ico-pc/build-host/tmp` for every build, `ctest` and game run. Build in `<worktree>/build-host/<dir>`.
`tools/worktree.sh drop <id>` removes the worktree and deletes `pkg/<id>`
only if it is merged into `main`; otherwise it keeps the branch and says so.

## Packages

`tools/package_win.sh <label>` and `tools/package_linux.sh <label>` build
the packages for HEAD in a clean worktree (`dist/ico-pc-<label>-win.zip`,
`dist/ico-pc-<label>-linux.tar.gz`). [`port/TESTING.md`](port/TESTING.md)
says what each holds; [`port/STEAMDECK.md`](port/STEAMDECK.md) covers
running the Linux one.

## Appendix, maintainers: the base ELF

What stays in the tree besides the host build, and why:

| kept | for |
| --- | --- |
| `sce/`, `ico2/vusrc/` | the game includes `sce/`'s headers under the SDK names; the VU1 sources are the shaders' reference. Only `sce/libsndn2/sound.c` is in a host target (`ico_sndn2`) |
| `config/link_order.pal.txt`, `config/data_*.pal.txt`, `config/link.pal.ld` | the source list `gen_sources.py` reads, the data members' schema, and the retail link script as a record of the PS2 layout. Nothing links with them |
| `tools/extract_elf.sh`, `tools/extract_elf.py` | the maintainer step below |

Nothing here compiles for the PS2, links a PS2 ELF or assembles the VU1
programs; the decompilation keeps that build.

The loader's reference test and `gen_data_desc.py --manifest` read the boot
ELF from `baserom/pal/baseelf.elf`, which `tools/extract_elf.sh` writes from
`baserom/Ico_PAL.iso` (the disc's `SCES_507.60`, SHA-1
`da3644c54c26fe760f3b6a591a5fc2eab396ed2b`, checked against
`config/sha1sums.txt`; it needs pycdlib and `mips-linux-gnu-objcopy`).
Nothing under `baserom/` is committed ([`LEGAL.md`](LEGAL.md)). Without it
those two tests are not built, and CI never has it.
