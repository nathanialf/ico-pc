# Building

How to build, test and package the PC port. The build is CMake with Ninja
and compiles the game's C under `ico2/` for the host, with `port/` standing
in for Sony's libraries. The PS2 ELF build of the decompilation (`./build.sh`,
`tools/build.sh`, `tools/gen_ninja.py` and the period linker) is gone from
this repository; what is left of the period toolchain is an optional
developer check, in the appendix at the end.

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
also fetches the period compilers unless `SKIP_TOOLCHAIN=1`, which only the
appendix needs. The build needs no disc image and no `baserom/`: the binary
holds no disc data. The game reads your PAL disc image (SCES-50760) at run
time: the first run extracts it into `ico.o2r` (docs/port/DATA.md), and the
data tables load from that archive (docs/port/DATA.md, "The data tables").

## EUC-JP sources

Some game sources carry Japanese text in EUC-JP, and their string literals
are the ROM's bytes. `.gitattributes` lists each of them with
`working-tree-encoding=EUC-JP`: git stores the file as UTF-8 and checks it
out as EUC-JP, which is what the compiler reads. An editor or a pipe that
rewrites one of these files as UTF-8 changes its bytes; change them with an
ASCII patch and `git apply`.

## Hooks

`tools/install_hooks.sh` (run by `tools/setup.sh`) installs a pre-commit hook
that runs, in order:

1. `tools/check_no_rom.sh`: refuses disc images, executables, extracted
   assets and large binaries;
2. `tools/format.sh --check` on the staged C;
3. the three freshness checks CI also runs: `tools/gen_data_desc.py
   --check` (`port/data/gen/`), `tools/gen_layout_asserts.py --check`
   (`port/test/layout_asserts.c`) and `tools/gen_sources.py --check`
   (`cmake/IcoSources.cmake`). Each needs pyelftools, so the hook uses
   `.venv/bin/python`. Regenerate with the same script without `--check`.

`tools/format.sh` formats the tracked C with the tracked `.clang-format` and
then `tools/format_layout.py`'s top-level blank-line layout.

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request, one Linux
job (`ubuntu-24.04`), with no secrets and no disc image:

| step | what |
| --- | --- |
| host packages, venv | gcc, the X11 and ALSA headers SDL3 builds against, `tools/requirements.txt` |
| cache + `tools/fetch_toolchain.sh` | `tools/toolchain/` is cached on the hash of `fetch_toolchain.sh` and `fetch_deps.sh` |
| `tools/check_no_rom.sh` | the IP scan over every tracked file |
| `tools/format.sh --check` | clang-format over the tracked C |
| `gen_data_desc.py`, `gen_layout_asserts.py`, `gen_sources.py` with `--check` | the generated files are fresh |
| `linux-x64` headless | configure with `-DICO_LINK_EXE=ON`, build, `ctest` |
| `linux-x64` window | `-DICO_HEADLESS=OFF -DICO_LINK_EXE=ON`, build, `ctest` |
| `linux-x64-clang` | build, `ctest` |
| `win-x64` | cross-compile (mingw-w64 gcc) with the window build and `ico_pc.exe`; the tests are built, not run |

A test that needs the disc (`vfs_disc`, `archive_disc`) or a Vulkan device
(the `rhi_vk*`, `rd_*`, `shaders_pixel`, `vu1` tests) exits 77 without it, and
`SKIP_RETURN_CODE 77` makes ctest report it as skipped, which passes
(`ctest` exits 0; checked with no `baserom/` and with
`VK_ICD_FILENAMES=/nonexistent`). `tables_loader` and `tables_manifest` are
not built without a base ELF. Run the same steps locally before pushing.

## Packages

`tools/package_win.sh <label>` and `tools/package_linux.sh <label>` build the
test packages for HEAD in a clean worktree (`dist/ico-pc-<label>-win.zip`,
`dist/ico-pc-<label>-linux.tar.gz`). docs/port/TESTING.md has what each
holds; docs/port/STEAMDECK.md covers running the Linux one.

## Host build

`CMakeLists.txt`, `CMakePresets.json` and `cmake/` compile the game's C under
`ico2/` for the host, with `port/` standing in for Sony's libraries. The
build never compiles `sce/` or `ico2/vusrc/` (no CMake file names either;
`cmake/IcoSources.cmake` lists `ico2/` sources only). Those two directories
stay in the tree as the reference the EE identity check compiles against
(`sce/` holds Sony's headers the game includes under the SDK names, and the
libraries' sources; `ico2/vusrc/` the five VU1 microprograms the renderer's
shaders were ported from). It writes only under `build-host/<preset>/`.
[`docs/port/BUILD_STATUS.md`](port/BUILD_STATUS.md) lists what compiles on
each preset and why the rest does not.

### Toolchains: `tools/fetch_toolchain.sh`

Run it once. It needs `curl`, `tar`, `sha256sum` and `dpkg-deb` and no root,
and fills `tools/toolchain/` (gitignored, about 1.6 GB):

| directory | what | from |
| --- | --- | --- |
| `llvm-mingw/` | clang 23, lld and the mingw-w64 UCRT runtime for x86-64 Windows (its i686 half is unused); the same clang targets Linux | [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) release 20260922, `ucrt-ubuntu-22.04-x86_64`, SHA-256 pinned |
| `mingw-gcc/` | mingw-w64 gcc 14 and binutils for x86-64 Windows | Debian 13 `gcc-mingw-w64-*-win32` 14.2.0-19+27+b1, `binutils-mingw-w64-*` 2.44-3+12+b1, `mingw-w64-*-dev` 12.0.0-5, SHA-256 pinned |
| `cmake/` | CMake 4.4.4 (`cmake`, `ctest`) | [Kitware's release](https://github.com/Kitware/CMake/releases/tag/v4.4.4) `cmake-4.4.4-linux-x86_64.tar.gz`, SHA-256 pinned from the release's `cmake-4.4.4-SHA-256.txt` |

The Debian packages come from `deb.debian.org`, falling back to
`snapshot.debian.org` once a version is superseded. `SKIP_MINGW_GCC=1` and
`SKIP_CMAKE=1` skip the mingw-gcc tree and CMake. The toolchain files take
`ICO_LLVM_MINGW` and `ICO_MINGW_GCC` from the environment to use copies
elsewhere. (The i386 sysroot and the i686 mingw-gcc of the retired 32-bit
presets are no longer fetched.)

The Linux presets also use the host's gcc 14 and glibc (Debian 13 in the
container). Ninja comes from `.venv/bin` (`tools/requirements.txt`) or the `PATH`.
The commands below use the pinned CMake; any CMake 3.25 or later on the
`PATH` (the presets' minimum) works the same.

### Presets

```sh
CMAKE=tools/toolchain/cmake/bin
$CMAKE/cmake --preset win-x64
$CMAKE/cmake --build --preset win-x64
$CMAKE/ctest --preset linux-x64   # the unit tests, on the Linux presets
```

`-B <dir>` after `--preset` builds a preset into another directory (each
work package uses its own under `build-host/`).

The unit tests (`ctest`): `fpenv` (`fpenv_test`), `sched`, `fiber`,
`fiber_guard`, `arena`, `memory` and `ios_chain` (`port/platform/test/`,
[`docs/port/PLATFORM.md`](port/PLATFORM.md)), and the other packages'
tests. `memory` and `ios_chain` compile the game's allocator and thread
layer and run on the host's own record layout. The Windows presets build the
test `.exe`s without running them.

| preset | target | compiler |
| --- | --- | --- |
| `linux-x64` | Linux x86-64 | host gcc 14 |
| `win-x64` | Windows x64 | `mingw-gcc` x86-64 |
| `asan` | Linux x86-64, `-fsanitize=address,undefined`, `-O1` | host gcc 14 |
| `fptrap` | `linux-x64` with float divide-by-zero and invalid unmasked in simulation mode | host gcc 14 |
| `linux-x64-clang`, `win-x64-clang` | the same two targets | llvm-mingw clang 23 (the host glibc for Linux) |

There is one architecture. The 32-bit host presets (`ref-m32`, `ref-m32-clang`,
`win-x86-ref`, `win-x86-ref-clang`) were the oracle for the 64-bit build and
were retired at Phase 2 exit, commit 36a1d73e, once the x64 traces matched
theirs over 3000 ticks.

GCC is the primary compiler because the game uses GNU C nested functions,
which clang does not implement; the clang presets compile fewer files until
those are rewritten ([`docs/port/BUILD_STATUS.md`](port/BUILD_STATUS.md) has
the counts and the trade-off). llvm-mingw ships no Linux sanitizer runtimes.

Windows builds must target mingw (gcc or llvm-mingw), never MSVC (clang-cl
or a `*-windows-msvc` triple): clang on MSVC targets evaluates call
arguments right to left, where ee-gcc and the mingw and Linux compilers go
left to right (package 0C, `docs/research/`), and the game's results depend
on that order at some call sites.

### Options

| cache variable | default | effect |
| --- | --- | --- |
| `ICO_HEADLESS` | `ON` | leaves out the renderer-owned sources (`ICO_RENDERER_SOURCES`) and defines `ICO_HEADLESS=1` |
| `ICO_STRICT_WARNINGS` | `OFF` | makes `-Wreturn-type`, `-Wimplicit-function-declaration` and `-Wstrict-prototypes` errors. While it is off, the C89-era diagnostics modern compilers make errors by default (implicit declarations and int, int/pointer conversions, incompatible pointers, return mismatches) are warnings, so every file that can compile does |
| `ICO_BUILD_BLOCKED` | `OFF` | also compiles `ICO_BLOCKED_SOURCES` (`cmake/IcoExclusions.cmake`), to recheck them |
| `ICO_LINK_EXE` | `OFF` | links `ico_pc` (`port/platform/main_host.c`), the game's program |
| `ICO_HEAP_STATS` | `OFF` | the game's allocator (`fumi/ios/memory.c`) reports to `port/platform/arena.c`, which logs each heap partition's high-water mark on stderr |
| `ICO_FPTRAP` | `OFF` | `fptrap` preset |
| `ICO_SANITIZE` | empty | `asan` preset: the `-fsanitize=` list |
| `ICO_BASE_ELF` | `baserom/pal/baseelf.elf` | the base ELF the loader's reference tests read; without it they are not built |

### Sources

`cmake/IcoSources.cmake` is written by `tools/gen_sources.py` from
`config/link_order.pal.txt` (the retail link's object list, kept as the
source list; nothing links with it any more): the `ico2/` C sources, one list per programmer
directory, the renderer-owned list, and the data-only members. Configure
warns when it is stale; rerun the script after changing the link order.

Each programmer directory is one object library with the include path
`tools/compile_c.sh` gives it (its own `include/`, then the others, then
`port/compat/` for the SDK header names), and `-fmacro-prefix-map` makes
`__FILE__` the period spelling (`src/main.c`), which the assert messages
print. The game's `main` is compiled as `ico_game_main`.

The game options (`cmake/IcoFlags.cmake`) are `-std=gnu11
-fno-strict-aliasing -fwrapv -ffp-contract=off -fno-fast-math
-fsigned-char -fno-common -fgnu89-inline`, and `ICO_HOST=1`. The game and
data TUs alone also take the EE's bit-field rule, `-mno-ms-bitfields` on
Windows (`port/` code keeps the platform ABI, which SDL's and Windows'
structs need). No configuration defines `NDEBUG`: the
retail game ran with its asserts.

### Data tables

The binary holds no disc data. The 73 data tables are defined empty
(`port/data/gen/table_defs.c`) and filled at boot from the boot ELF on the
user's disc (`port/data/tables.c`, docs/port/DATA.md). The generated
descriptors under `port/data/gen/` are committed and carry no disc bytes;
`tools/gen_data_desc.py --check` keeps them fresh. Configuring and building
need no base ELF and no pyelftools. Only the loader's reference test
(`tables_loader`) and `tables_manifest` need a base ELF
(`baserom/pal/baseelf.elf`, `ICO_BASE_ELF`: a maintainer step, see the
appendix) and are left out without one.

### Floating point

`port/platform/fpenv.c`: `ico_fpenv_sim_enter()` sets round toward zero
with flush-to-zero and denormals-are-zero (MXCSR; FPCR on arm64),
`ico_fpenv_host_enter()` restores the defaults. `fpenv_test` checks both,
and on `fptrap` that a division by zero raises SIGFPE.

## Appendix, maintainers: EE identity check

The port changes `ico2/` freely, but a change that should not alter what the
PS2 compiler emits (a type sweep, a rename, a host-only `#ifdef`) is checked
against the period compiler: `tools/ee_identity.sh`. It is optional and is
not part of the build or of CI.

```sh
sudo dpkg --add-architecture i386 && sudo apt-get update
sudo apt-get install gcc-multilib libc6:i386 libstdc++6:i386 zlib1g:i386 \
    binutils-mips-linux-gnu patch
tools/setup.sh                       # fetches the period compilers into tools/cc/
tools/ee_identity.sh ico2/seki/src/Basic.c ico2/seki/src/MicroCode.c
tools/ee_identity.sh -r 36a1d73e --all   # every ico2/ C source against a revision
```

For each file it compiles with `tools/compile_c.sh` (ee-gcc 2.9-991111 and
its assembler, run through `tools/period_env.sh`) from the working tree and
from a temporary worktree of the revision (default `HEAD`), and diffs the
sections `.text .data .rodata .sdata .bss .sbss .lit4 .lit8` and their
relocations. Debug sections are left out (they carry paths). Exit 0 when all
are identical. The packages' sweeps (`docs/port/SWEEP_2D.md` to
`SWEEP_2J.md`) did this by hand; on 2026-10-05 `--all` over the 219 sources of
`config/link_order.pal.txt` took 37 s on four cores, and against a revision 150
commits back it reported 42 sources different, so it does detect changes.

What stays and why:

| kept | for |
| --- | --- |
| `tools/compile_c.sh`, `tools/period_env.sh`, `tools/period_obstack.c` | the identity check's compile step (the preload library restores the obstack chunk size the original build had; ee-as's short-loop padding depends on it) |
| `tools/setup.sh`'s compiler fetch (ee-gcc 2.9-991111 and 2.96 into `tools/cc/`) | the compilers `compile_c.sh` runs; the 2.96 tree is only its SCE assembler, for `sce/` sources |
| `sce/`, `ico2/vusrc/` | the identity check compiles `sce/`'s sources and the game includes its headers under the SDK names; the VU1 sources are the shaders' reference. Not part of any host target |
| `config/link_order.pal.txt` and `config/data_*.pal.txt`, `config/link.pal.ld` | the source list `gen_sources.py` reads, the data members' schema, and the retail link script as documentation of the PS2 layout. Nothing links with them |
| `tools/extract_elf.sh`, `tools/extract_elf.py` | the maintainer step below |

Removed: `./build.sh`, `tools/build.sh`, `tools/gen_ninja.py`,
`tools/verify_elf.py` (the SHA-1 check of the PS2 link's inputs: the port's
extractor checks the disc itself, `port/data/extract.c`), the two GNU ld 2.10
patches and `tools/setup.sh`'s builds of ld 2.10 and dvp-as. Nothing links
a PS2 ELF or assembles the VU1 programs now. The decompilation
(<https://github.com/nathanialf/ico>) keeps that build.

The maintainer step for the loader's reference test and `gen_data_desc.py
--manifest`: they read the boot ELF from `baserom/pal/baseelf.elf`, which
`tools/extract_elf.sh` writes from `baserom/Ico_PAL.iso` (the disc's
`SCES_507.60`, SHA-1 `da3644c54c26fe760f3b6a591a5fc2eab396ed2b`, checked
against `config/sha1sums.txt`; pycdlib and `mips-linux-gnu-objcopy`). Nothing
under `baserom/` is committed (`docs/LEGAL.md`). Without it those two tests
are not built, and CI never has it.
