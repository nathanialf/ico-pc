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

`tools/setup.sh` does the venv step and installs the git hooks (below).

The build needs no disc image and no `baserom/`: the program holds no disc
data. The game reads the player's own PAL disc image (SCES-50760) at run
time. The first run extracts it into the archive `ico.o2r`, and the data
tables load from that archive at boot.

## Toolchains: `tools/fetch_toolchain.sh`

Run it once. It needs `curl`, `tar`, `sha256sum` and `dpkg-deb`, no root,
and fills `tools/toolchain/` (gitignored, about 1.6 GB):

| directory | what | from |
| --- | --- | --- |
| `llvm-mingw/` | clang 23, lld and the mingw-w64 UCRT runtime for x86-64 Windows; the same clang targets Linux | [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) release 20260922, `ucrt-ubuntu-22.04-x86_64`, SHA-256 pinned (not fetched on an arm64 host unless `SKIP_LLVM_MINGW=0`, which takes the pinned `-aarch64` build) |
| `mingw-gcc/` | mingw-w64 gcc 14 and binutils for x86-64 Windows (x86-64 hosts only) | Debian 13 `gcc-mingw-w64-*-win32` 14.2.0-19+27+b1, `binutils-mingw-w64-*` 2.44-3+12+b1, `mingw-w64-*-dev` 12.0.0-5, SHA-256 pinned |
| `cmake/` | CMake 4.4.4 (`cmake`, `ctest`) | [Kitware's release](https://github.com/Kitware/CMake/releases/tag/v4.4.4) `cmake-4.4.4-linux-x86_64.tar.gz` (`-linux-aarch64` on an arm64 host), SHA-256 pinned |
| `deps/` | SDL3, volk, the Vulkan headers, the validation layer, DXC, libmpeg2 and libchdr | `tools/fetch_deps.sh`, which `fetch_toolchain.sh` runs last ([`THIRD_PARTY.md`](THIRD_PARTY.md)) |

The pins the fetch scripts share (the SDL3 version and its SHA-256,
`DEB_SNAPSHOT`, and the `fetch_deb` helper that downloads a Debian package
into the caller's temporary directory) are in `tools/fetch_common.sh`. The
Debian packages come from `deb.debian.org`, falling back to
`snapshot.debian.org` once a version is superseded. `SKIP_MINGW_GCC=1` and
`SKIP_CMAKE=1` skip the mingw-gcc tree and CMake; the header of each script
lists its other overrides. The toolchain files take `ICO_LLVM_MINGW` and
`ICO_MINGW_GCC` from the environment to use copies elsewhere.

The Linux presets use the host's gcc 14 and glibc. Ninja comes from
`.venv/bin` (`tools/requirements.txt`) or the `PATH`. Any CMake 3.25 or later
on the `PATH` works in place of the pinned one.

The host is x86-64 or arm64 (aarch64) Linux; `tools/fetch_common.sh` names it
(`ICO_HOST_ARCH`) and picks the pin of each pair. An arm64 host is described
under ["Linux arm64"](#linux-arm64).

## Linux arm64

The `linux-arm64` preset builds on an arm64 Linux host with its own gcc 14
(no cross toolchain): a Raspberry Pi 5 class machine, an arm64 container
(`docker run --platform linux/arm64 ubuntu:24.04`, native on Apple silicon,
under qemu-user elsewhere) or GitHub's `ubuntu-24.04-arm` runner. The program
needs the glibc of its build host or newer, so build on the oldest system the
package must run on (Ubuntu 24.04 is glibc 2.39). Besides the quickstart's
packages it needs the Wayland and KMSDRM headers and `wayland-scanner`:

```sh
sudo apt-get install build-essential gcc-14 g++ git curl xz-utils python3 python3-venv \
    libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev \
    libxfixes-dev libxrender-dev libasound2-dev libpulse-dev \
    libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev \
    libdrm-dev libgbm-dev libegl-dev
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
tools/fetch_toolchain.sh          # builds DXC from source: a few minutes
CMAKE=tools/toolchain/cmake/bin
$CMAKE/cmake --preset linux-arm64 -DCMAKE_C_COMPILER=gcc-14
$CMAKE/cmake --build --preset linux-arm64
$CMAKE/ctest --preset linux-arm64
```

Use a separate clone for an arm64 host or container, not the checkout an
x86-64 host uses: `tools/toolchain/`, `.venv/` and `build-host/` hold
programs for the machine that made them, and `tools/fetch_toolchain.sh` on
one architecture replaces the other's CMake and DXC in place.

What differs from an x86-64 host:

- `tools/fetch_toolchain.sh` takes the aarch64 build of the same CMake
  release and leaves out `llvm-mingw/` and `mingw-gcc/`: the presets that
  use them (`win-x64`, `win-x64-clang`, `linux-x64-clang`) build for x86-64
  and are for x86-64 hosts, and mingw-gcc's Debian packages are amd64
  programs. `SKIP_LLVM_MINGW=0` and `SKIP_MINGW_GCC=0` fetch them anyway.
- `tools/fetch_deps.sh` builds SDL3 into `deps/sdl3/linux-arm64/` with the
  Wayland and KMSDRM video backends as well as X11 (it stops when SDL's
  configure leaves either out), writes the Wayland protocol files' notices
  beside it for the package's `NOTICES.txt`, and takes the arm64 builds of
  the pinned Debian packages (the X11 and audio headers when the host has
  none, the validation layer). SDL picks the backend at run time. Wayland
  is reported working on one handheld (Sway on ROCKNIX); KMSDRM, with no
  desktop at all, is untested.
- Microsoft publishes no aarch64 Linux DXC, so `tools/fetch_deps.sh` builds
  the pinned release's tag from source (the commit id and the SHA-256 of its
  `git archive` checked, as libmpeg2's and libchdr's, and the same two checks
  on each of its three submodules) into `deps/dxc/` with the
  host's g++: a few minutes on 8 cores. It builds the `dxc` program and
  `libdxcompiler.so` only; there is no `libdxil.so`, and the arm64 preset
  compiles no DXIL (`ICO_SHADERS_DXIL=OFF`).
- The preset is the window build with the program linked, Vulkan only
  (`ICO_RHI_D3D12=OFF`), as `android-arm64`. The movie decoder uses
  libmpeg2's NEON routines, as on Android.

## Android: `tools/fetch_android.sh`

Run it once after `tools/fetch_toolchain.sh` (it uses the pinned CMake and the
dependencies that script fetched). It needs the Android SDK
(`ANDROID_HOME`, else `ANDROID_SDK_ROOT`, else `~/Android/Sdk`) and installs
the packages below with `sdkmanager`; `SKIP_SDK_INSTALL=1` only checks them.

| what | pin |
| --- | --- |
| NDK | `ndk;28.2.13676358` (r28c; arm64 libraries get 16 KB pages by default) |
| platform, build-tools | `platforms;android-35`, `build-tools;35.0.0` (`aapt2`, `zipalign`, `apksigner`) |
| SDL3 for arm64 | built from the release source with the NDK into `tools/toolchain/deps/sdl3/android-arm64/` |
| `tools/toolchain/android.env` | the paths and versions above, read by the CMake toolchain file and `android/app/build.gradle` |
| `android/local.properties` | `sdk.dir` and `cmake.dir` for this checkout (gitignored); every checkout runs the script once |

The Gradle project is `android/` (arm64-v8a only, `minSdk` 29, `targetSdk`
35). `cd android && ./gradlew --no-daemon assembleDebug [-PicoLabel=<label>]`
builds the debug APK into `android/app/build/outputs/apk/debug/`;
`assembleRelease` builds `app-release-unsigned.apk`. `-PicoLabel` sets the
version name (default: `git describe`); `icoNativeJobs` sets the native build
parallelism. `cmake --preset android-arm64` configures the native code alone,
for a compile check without Gradle. The unstripped `libmain.so` is under
`android/app/build/intermediates/cxx/RelWithDebInfo/<hash>/obj/arm64-v8a/`
and the link map `ico_pc.map` under
`android/app/.cxx/RelWithDebInfo/<hash>/arm64-v8a/`.

## Apple Vulkan renderer (MoltenVK)

Xcode, CMake and Ninja are required on the Mac building these targets.
The dependency script installs SHA-256-pinned MoltenVK 1.4.2 and builds
SDL3 3.4.18 as a static library for the selected SDK:

```sh
tools/fetch_moltenvk.sh ios
cmake --preset ios-moltenvk
cmake --build --preset ios-moltenvk
```

Use `macos` and the `macos-moltenvk` preset for a native Apple silicon
renderer build. These presets build `ico_rhi`, with the existing Vulkan
backend linked to MoltenVK; they do not produce an app or IPA. The iOS
package targets ARM64 devices, iOS 15 or newer; the macOS target is ARM64,
macOS 12 or newer. The pinned iOS package has no simulator slice.

To compile the shared renderer and its existing HLSL shaders too, supply
a **macOS host** DXC executable (it runs during the build, even for iOS):

```sh
cmake --preset ios-moltenvk -DICO_DXC=/absolute/path/to/dxc
cmake --build build-host/ios-moltenvk --target ico_render
```

The app includes `SDL3/SDL_main.h` and creates an `SDL_WINDOW_VULKAN`
window. SDL uses the exported, linked
`vkGetInstanceProcAddr` to create its Metal-backed Vulkan surface; the
backend enables portability enumeration and the device's portability
subset. No custom Metal backend is used. `ICO_MOLTENVK_ROOT` can override
the extracted release root. Apple package notices can be generated with
`tools/gen_notices.py --platform ios --out NOTICES.txt` (or `macos`).

### iPhone IPA for LiveContainer

Build the current working checkout into an unsigned IPA:

```sh
tools/package_ios.sh -DICO_DXC=/absolute/path/to/macos/host/dxc
```

The `ios-app` preset defaults to the host DXC at
`tools/toolchain/deps/dxc-build/bin/dxc`; omit the argument if it exists.
The script builds the app and writes `dist/ico-pc-ios-unsigned.ipa`, with
licenses inside the bundle and a separate `dist/ico-pc-ios.map` for crash
offsets. No provisioning profile or signing identity is required.
The dependency fetch also installs the pinned libchdr and libmpeg2 source
trees, using `tools/fetch_deps.sh --game-only`.

For a Mac without a host DXC, build it once from source (requires Git,
Xcode, CMake and Ninja; this is a substantial LLVM build):

```sh
git clone --depth 1 --branch v1.9.2602 --recurse-submodules \
  https://github.com/microsoft/DirectXShaderCompiler tools/toolchain/deps/dxc-src
cmake -S tools/toolchain/deps/dxc-src -B tools/toolchain/deps/dxc-build -G Ninja \
  -C tools/toolchain/deps/dxc-src/cmake/caches/PredefinedParams.cmake \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_SPIRV_CODEGEN=ON \
  -DHLSL_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF \
  -DSPIRV_BUILD_TESTS=OFF \
  -DCMAKE_CXX_FLAGS=-Wno-invalid-specialization
cmake --build tools/toolchain/deps/dxc-build --target dxc --parallel 6
```

Import the IPA into LiveContainer. Copy your own extracted `ico.o2r` into
the guest app's **Documents** folder through LiveContainer's data-folder
access, then launch ICO. A PAL `Ico_PAL.iso` or `Ico_PAL.chd` in that folder
can also be extracted on first launch. Game data is not included in the IPA.
Settings, saves and logs also live in Documents. The app uses the existing
touch controls and controller input, runs fullscreen in landscape, and
pauses audio/rendering while in the background. iOS uses the existing mobile
defaults: a 60 FPS cap and adaptive resolution for the Enhanced preset.
Selecting a fixed resolution such as 2× keeps that resolution; choose
Auto to let the existing GPU-cost controller lower it when necessary.
At 2× the scene contains roughly four times the pixels of 1×. For a
performance report, enable `[dev] perf_log = true` in `ico-pc.ini` and
include `logs/ico-pc-perf.csv` along with the normal log.

The wrapper uses SDL's UIKit entry point and a guarded 32 MB coroutine
stack on the main thread. Shared game angle conversions explicitly go
through `int` before `short`, preserving the EE's halfword wrapping on
ARM64. Git uses `EUC-JP-MS` for Japanese source files to preserve C
backslashes with macOS iconv.

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
| `linux-arm64` | Linux arm64 (aarch64), built on an arm64 host ([below](#linux-arm64)); window build, `ICO_LINK_EXE=ON`, no Direct3D 12 and no DXIL shaders | host gcc 14 |
| `android-arm64` | Android arm64-v8a, `libmain.so` alone: a compile check without Gradle (`tools/fetch_android.sh` first); window build, `ICO_LINK_EXE=ON`, no Direct3D 12 and no DXIL shaders | NDK clang |

The port targets x86-64 (Windows and Linux) and arm64 (Linux and Android).

### Options

| cache variable | default | effect |
| --- | --- | --- |
| `ICO_HEADLESS` | `ON` on every preset except `win-x64`, `linux-arm64` and `android-arm64` | `OFF` is the window build: the game draws through `port/render` (`ICO_RD=1`) into an SDL3 window. `ON` is the headless build for trace and test runs: no window and no renderer, `ICO_HEADLESS=1`. Both compile the same game sources |
| `ICO_LINK_EXE` | `OFF` | links `ico_pc` (`port/platform/main_host.c`), the game's program. Without it only the libraries and the tests are built |
| `ICO_STRICT_WARNINGS` | `OFF` | makes `-Wreturn-type`, `-Wimplicit-function-declaration` and `-Wstrict-prototypes` errors. While it is off, the C89-era diagnostics that modern compilers make errors by default (implicit declarations and int, int/pointer conversions, incompatible pointers, return mismatches) are warnings |
| `ICO_BUILD_BLOCKED` | `OFF` | also compiles the sources `cmake/IcoExclusions.cmake` leaves out (the list is empty today; it is the place to park a game source that stops compiling) |
| `ICO_HEAP_STATS` | `OFF` | the game's allocator (`fumi/ios/memory.c`) reports to `port/platform/arena.c`, which logs each heap partition's high-water mark |
| `ICO_FPTRAP` | `OFF` | set by the `fptrap` preset |
| `ICO_SANITIZE` | empty | the `-fsanitize=` list; the `asan` preset sets `address,undefined` |
| `ICO_RHI_VULKAN` | `ON` | builds the Vulkan renderer backend |
| `ICO_RHI_D3D12` | `ON` for 64-bit Windows | builds the Direct3D 12 renderer backend next to the Vulkan one |
| `ICO_BASE_ELF` | `baserom/pal/baseelf.elf` | the base ELF the data loader's reference tests read; without it they are not built |
| `ICO_DXC`, `ICO_DEPS_DIR` | the fetched copies | the shader compiler and the dependency tree |

At run time, the environment variable `ICO_TTY` (any value) turns on the
game's `scePrintf` output on stdout, which the retail game sent to the
development kit's TTY; it is off otherwise. Release programs have no
console, so use it with `--console` on a program started from a terminal.

## Compilers

GCC is the primary compiler, and the clang presets build the same sources;
CI builds both so neither drifts. The game originally used GNU C nested
functions, which clang does not implement; they have all been rewritten as
file-scope functions, so no game source needs GCC any more. llvm-mingw ships
no Linux sanitizer runtimes, so the `asan` preset is GCC either way.

Windows builds must target mingw (gcc or llvm-mingw), never MSVC (clang-cl
or a `*-windows-msvc` triple). Clang targeting MSVC evaluates call arguments
right to left, where ee-gcc and the GNU and mingw compilers go left to
right, and the game's results depend on that order at some call sites.

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
(`port/null/gfx_null.c`, `port/null/libgcc_null.c`).

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
  the game's layout options too.
- `port/compat/eeregs.h` maps the EE's hardware registers to plain memory,
  so a loop that polls one (`GS_CSR`, a DMA channel's busy bit) never sees
  it change; such loops are replaced at their call sites.
- VU0 and R5900 inline assembly has C bodies over `port/math`; the compiled game sources hold no EE
  assembly and no EE opcode wrappers.
- The simulation runs with the floating-point environment
  `port/platform/fpenv.c` sets: `ico_fpenv_sim_enter()` selects round toward
  zero with flush-to-zero and denormals-are-zero (MXCSR; FPCR on arm64),
  `ico_fpenv_host_enter()` restores the host defaults for the window, SDL
  and the renderer. `fpenv_test` checks both, and on `fptrap` that a
  division by zero raises SIGFPE.

While `ICO_STRICT_WARNINGS` is off the build prints many C89-era warnings,
most of them `-Wstrict-prototypes`. The three warnings that option promotes
can become errors once their counts reach zero.

The port's own code (`port/`, every target compiled with
`ICO_PORT_WARNINGS`) builds with `-Wall -Wextra -Wno-unused-parameter
-Werror` on every preset, gcc and clang: a new warning there fails the
build. The game code (`ico2/`, the generated data tables) and the port
tests that compile game sources into their executable keep
`ICO_GAME_WARNINGS`, which only warn. A port TU that includes a game header
still declaring an unprototyped function (`typedef.h`, `thread.h`, `act.h`,
`s_init.h`, `debug.h`) adds `-Wno-strict-prototypes` for itself in its
`CMakeLists.txt`; no other warning is switched off for port code.

## The game code

`ico2/` (and `sce/libsndn2/sound.c`, the one `sce/` file the port compiles)
is the port's own source. It started as the decompilation's `main`, but it is only
ever compiled for the host, so a change to it is a platform change made
directly in the code: the host form replaces the original spelling. There
are no `#ifdef ICO_HOST` / `#else` arms carrying the PS2 text, and the tree
is not expected to compile for the PS2 or to match the ROM. `ICO_HOST` stays
defined in the build (headers under `port/` test it); conditionals that
select between host build variants (`ICO_RD`, `ICO_HEADLESS`,
`ICO_HEAP_ASAN`, `ICO_FPTRAP`) stay too. `tools/strip_host_gates.py` is the
record of how the gated tree became this one (968 sites); its `--check`
runs in CI and in the pre-commit hook and fails on any `ICO_HOST`
conditional under `ico2/` or `sce/`.

The conventions that still matter on the host:
`ICO_WORD` for a word that holds an address (pointer-wide), `ICO_RAW` /
`ICO_RAWP` for a view of a record at an EE offset, `ICO_MAX_SIZE` for a host
record wider than the original literal, the layout asserts
and the template audit.

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
back to it.

## Data tables

The program holds no disc data. The game's data tables are defined empty
(`port/data/gen/table_defs.c`) and filled at boot from the boot ELF on the
player's disc (`port/data/tables.c`), each
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
`working-tree-encoding=EUC-JP-MS`: git stores the file as UTF-8 and checks it
out as EUC-JP-MS, which is what the compiler reads. This variant preserves
ASCII C backslashes with macOS iconv. An editor or a pipe that
rewrites one of these files as UTF-8 changes its bytes; change them with an
ASCII patch and `git apply`.

## Hooks

`tools/install_hooks.sh` (run by `tools/setup.sh`) installs a pre-commit
hook that runs, in order:

1. `tools/check_no_rom.sh`: refuses disc images, executables, extracted
   assets, large binaries and integer tables ([`LEGAL.md`](LEGAL.md)). It
   reads the staged blobs (`git show :path`), so what it judges is what
   is committed;
2. `tools/format.sh --check --staged`: the staged C must be formatted; it
   checks the staged blobs, not the worktree;
3. the three freshness checks CI also runs: `tools/gen_data_desc.py
   --check` (`port/data/gen/`), `tools/gen_layout_asserts.py --check`
   (`port/test/layout_asserts.c`) and `tools/gen_sources.py --check`
   (`cmake/IcoSources.cmake`). `gen_data_desc.py` needs pyelftools, so the
   hook uses `.venv/bin/python`. Regenerate with the same script without
   `--check`;
4. `tools/strip_host_gates.py --check`: no `ICO_HOST` conditional in the
   game sources ([The game code](#the-game-code));
5. `tools/stack_overread_audit.py`: no game code that relies on the PS2's
   stack layout (the CI table below says what it looks for);
6. `tools/check_call_types.py`: calls and declarations agree with the
   definitions, as in CI.

`tools/format.sh` formats the tracked C with the tracked `.clang-format` and
then applies `tools/format_layout.py`'s top-level blank-line layout. With
`--check` it only reports (exit 1 if a file would change); `FILE...` limits
it to those files, and `--check --staged [FILE...]` judges the staged
version of those files, or of every staged file the script owns. A FILE that
does not exist is an error.

## Conventions

Formatting. `tools/format.sh` runs clang-format with the tracked
`.clang-format` (LLVM style, 4 spaces, 100 columns) over the C the script
owns; the hook and CI check it. Use `/* */` comments only. A comment says
what the code does and why, without release or package tags (git history
keeps those); an issue number may stay where it explains a behaviour.

Names are `prefix_snake_case`, the prefix being the module's. The main
prefixes:

| prefix | module |
| --- | --- |
| `ico_` | `port/platform`, `port/game`, `port/data`, `port/input`, `port/audio`, `port/config` |
| `rd_`, `rd__` (internal) | `port/render` |
| `rhi_`, `vkr_`, `d3dp_` | `port/rhi` and its Vulkan and Direct3D 12 backends |
| `ui_` | `port/ui`, the menus |
| `texpack_`, `modelpack_`, `gltf_` | the texture pack, model pack and glTF code in `port/render` |

Every name `port/` declares with a module prefix uses `prefix_snake_case`;
generated files and the names the game itself defines keep their spelling.
The older mixed-case prefixed names were renamed by
`tools/rename_port_symbols.py` with its table `tools/rename_table.txt`;
`--check` with that table lists any mixed-case name left.

Log lines start with the module's prefix and a colon, for example
`ico_pc:` (the host: `main_host.c`, `host_config.c`, `diag_host.c`,
`kernel_host.c`), `rd:`, `rhi:`, `rhi_vk:`, `rhi_d3d12:`, `window:`,
`video:`, `photo:`, `config:`, `audio:`, `input:`, `textures:`, `models:`,
`credits:`, `appearance:`, `settings:`, `mc:`. A test's own lines start
with the test's name. Player guides quote some of these lines
(`textures:`, `models:` and `window: an effects program is loaded`), so
change their text together with the guide.

A message box is titled `ICO PC` on every platform (Android's launcher
name is `ICO`).

New tests use the shared `CHECK` header of their module where one exists
(`port/test/ico_check.h`; `port/ui/test/settings_fixture.h` for the Options
tests) instead of defining their own.

New Python tools start with a docstring, use `argparse` and run with
`python3 -I`. Shell scripts start with `set -euo pipefail`.

## Tests

`ctest` runs the unit tests on the Linux presets; the Windows presets build
the test executables without running them. A test that needs the disc
(`vfs_disc`, `archive_disc`, `texpack_disc`) or a Vulkan device (the
`rhi_vk*`, `rd_*`, `shaders_pixel` and `vu1` tests) exits 77 without one,
and `SKIP_RETURN_CODE 77` makes ctest report it as skipped, which passes.
The disc tests read `ICO_DISC_IMAGE` (default `baserom/Ico_PAL.iso`).
`texpack_disc` also matches a PCSX2 texture pack's file names against the
disc's textures when `ICO_TEXPACK_DIR` (default
`build-host/tmp/texpack/SCES-50760/replacements`) exists; no pack file is
ever committed. `ICO_TEXPACK_DIR` is a cache variable, and the environment
variable of the same name sets its default at the first configure; it feeds
`texpack_disc`, `texpack_e2e` and `texpack_pack_files`.

The Android code is tested on Linux, where it can be: `android_paths`
(the files folder layout), `iso_import` (the first start's copy of the
chosen disc image), `lifecycle` (background and foreground on fake
operations) and `touch` (the touch overlay's mapping). The paths of a
phone GPU are tested on the Linux Vulkan device by forcing them: the
`*_nodual` tests run the render and shader tests again with
`ICO_RD_NO_DUAL=1` (blending in two passes, for GPUs without dual-source
blending such as Mali and PowerVR), `rhi_vk_nodual` with
`ICO_VK_FAKE_NO_DUAL=1` (the feature cleared at device creation), and
`rhi_vk_d24s8` with `ICO_VK_FAKE_D24S8=1` (the D24S8 depth fallback when
D32F_S8 is missing), and the `*_mali` tests run the render tests (all but
`rd_blur`), the shader, VU, movie and RHI tests again with
`ICO_VK_FAKE_LIMITS=mali`, which clamps the
device's limits to a Mali-G68's (four descriptor sets, 16 sampled images,
samplers and storage buffers a stage, 256-byte storage offsets, 4096 memory
allocations; `rhi_vk_mali` also checks that what goes past them is refused
by the limit's name, and `rd_perf_mali` creates the whole reachable pipeline
set). On a device, the log is mirrored to logcat: `adb logcat -s ico-pc`.

## Continuous integration

`.github/workflows/ci.yml` runs on every push to main, every pull request and
every pushed `v*` tag (which also packages for arm64, see
[Packages](#packages)): two Linux
jobs (`ubuntu-24.04`), the `linux-arm64` job (`ubuntu-24.04-arm`) and the
`android` job side by side, no secrets, no disc image. `linux` runs the steps
below in order; `extra` is a matrix of three runners, each with the same host
packages, venv and (restored, never saved) toolchain cache, that builds one
more preset; `linux-arm64` and `android` are described after the table.

| step | what |
| --- | --- |
| host packages, venv | gcc 14 (the runner's default gcc 13 also configures, since `cmake/IcoFlags.cmake` drops warning flags a compiler does not know), the X11, ALSA and PulseAudio headers SDL3 builds against, lavapipe (`mesa-vulkan-drivers`, `libvulkan1`) so the render tests run on a CPU Vulkan device, `tools/requirements.txt` |
| cache and `tools/fetch_toolchain.sh` | `tools/toolchain/` is cached on the hash of `fetch_toolchain.sh`, `fetch_deps.sh`, `fetch_common.sh` and `fetch_android.sh`, restored and saved as separate steps so a cold fetch is saved even when a later step fails |
| `tools/check_no_rom.sh` | the IP scan over every tracked file |
| `tools/format.sh --check` | clang-format over the tracked C |
| `tools/strip_host_gates.py --check` | no `ICO_HOST` conditional in `ico2/` or `sce/` |
| `tools/stack_overread_audit.py` (`--selftest`, then the tree) | no game code in `ico2/` that relies on the PS2's stack layout: a matrix or vector call that reads or writes past the local it is given, or a local that is only written; checked exceptions are in `tools/stack_overread_allow.txt` |
| `tools/check_call_types.py` | no call through a function-pointer cast that passes fewer arguments than the function it reaches reads, and no `extern` in an `ico2/` `.c` file that differs from its definition in parameter count, integer width or return width; checked exceptions are in `tools/check_call_types_allow.txt` |
| `gen_data_desc.py`, `gen_layout_asserts.py`, `gen_sources.py` with `--check` | the generated files are fresh |
| `linux-x64` headless | configure with `-DICO_LINK_EXE=ON`, build, `ctest` |
| `linux-x64` window | `-DICO_HEADLESS=OFF -DICO_LINK_EXE=ON` into `build-host/linux-x64-window`, build, `ctest` |
| `linux-x64-clang` | build, `ctest` |
| `win-x64` | cross-compile the window build and `ico_pc.exe`; the tests are built, not run |
| `extra`: `asan` | AddressSanitizer + UBSan (Debug, `-O1`), headless with `-DICO_LINK_EXE=ON`, build, `ctest` |
| `extra`: `fptrap` | float divide-by-zero and invalid trap, headless with `-DICO_LINK_EXE=ON`, build, `ctest` |
| `extra`: `win-x64-clang` | cross-compile the window build (`-DICO_HEADLESS=OFF`) with llvm-mingw clang; `ico_pc.exe` and `SDL3.dll` must exist |

The `linux-arm64` job builds the `linux-arm64` preset on GitHub's arm64
runner and runs its tests, with its own toolchain cache
(`toolchain-arm64-...`, the same four scripts; a cold fetch builds DXC from
source) that it saves. Its host packages are `linux`'s plus the Wayland and
KMSDRM headers, `g++` and `git` ([Linux arm64](#linux-arm64)). The hygiene
checks are `linux`'s alone. The gating `ctest` leaves out the render tests
that compare filtered pixels with a tolerance of 1 or 2 (`rd_pixel`,
`rd_gsbase`, `rd_present`, `rd_crt` and every variant of them, such as
`rd_present_depth_copy` and the `_nodual`, `_minlimits` and `_mali` runs, and
`rd_replay_tool` and `rd_replay_tool_list`, which need `rd_pixel`'s dump):
lavapipe on an arm64 host filters a step differently from lavapipe on
x86-64 and misses them by an LSB or two. A second step runs those tests
alone and does not gate (`continue-on-error`), so their output is still in
the log. The rest gate as on `linux`: `fog_lut` runs natively,
`fog_lut_arm64` skips (it looks for an x86-64 NDK and qemu), and the tests
that need the disc or saved frames (`shadow_spawn`, `fmv_fields`,
`rd_boot_present`, `rd_fog_depth_copy_dump`) skip. On an Adreno 650 (Turnip)
all of the left-out tests but `rd_pixel`'s sheet-magnification check pass.

The `android` job (90 minutes at most) builds the debug APK and checks its
structure; the game is not run:

| step | what |
| --- | --- |
| Java, wrapper validation | Temurin 17; `gradle/actions/wrapper-validation` checks `gradle-wrapper.jar` |
| host packages, venv, cache | as `linux`, with its own cache key (`toolchain-android-...`, the same four scripts) that this job saves |
| `tools/fetch_toolchain.sh` | with `SKIP_SDL3_LINUX=1 SKIP_SDL3_MINGW=1 SKIP_VALIDATION_LAYER=1` |
| `tools/fetch_android.sh` | NDK, platform, build-tools and SDL3 for arm64, in the runner's preinstalled SDK |
| `tools/check_no_rom.sh` | the IP scan |
| `./gradlew --no-daemon assembleDebug -PicoLabel=ci` | the debug APK |
| `tools/check_android_flags.sh` | the game's semantics options reach the NDK clang for every `ico2/` and `port/` source; no game unit writes an object it declares const (`tools/check_const_writes.py`: clang deletes such stores where gcc keeps them; the `const_write_audit` test runs it on native clang builds); no game or `port/math` object holds a fused multiply-add; no object stores onto its stack protector's guard (`tools/check_stack_guard.py`: only the Android build has `-fstack-protector-strong`, so a write past a local array ends the run on a phone alone) |
| APK checks | `lib/arm64-v8a/libmain.so`, `libSDL3.so`, `assets/VERSION.txt` and `assets/NOTICES.txt` are in the APK; `libSDL3.so` and exactly three libadrenotools hook libraries (`libmain_hook.so`, `libhook_impl.so`, `libfile_redirect_hook.so`) are there; every `LOAD` segment of every library has alignment `0x4000` (`llvm-readelf -lW`); `zipalign -c -P 16 -v 4`; `aapt2 dump badging` shows `minSdkVersion:'29'` and `targetSdkVersion:'35'`; the manifest (`aapt2 dump xmltree`) has `resizeableActivity` true, a theme, `extractNativeLibs` true and a `configChanges` list that includes `density`; `libmain.so` exports `SDL_main` (`llvm-nm -D`); `strings libmain.so` finds no `DXBC` |
| artifact `ico-pc-android-debug` | the APK, the unstripped `libmain.so` and `ico_pc.map`, kept 14 days |

Run the same steps locally before pushing.

## Packages

`tools/package_win.sh <label>` and `tools/package_linux.sh <label>` build
the packages for HEAD in a clean worktree (`dist/ico-pc-<label>-win.zip`,
`dist/ico-pc-<label>-linux.tar.gz`). Neither contains game data; both carry
the README, the player guides, the licence files and the save importer.
`tools/package_linux.sh` packages for the host's architecture: on an arm64
host it builds `linux-arm64` into `dist/ico-pc-<label>-linux-arm64.tar.gz`
(stage `dist/stage/linux-arm64/`, log `build-host/pkg-linux-arm64-<label>.log`),
the same contents with the aarch64 program and SDL3 (and, in
`NOTICES.txt`, the notices of the Wayland protocol files SDL3 is built
with). On either architecture it fails when a shipped binary (`ico_pc`,
`tools/mc_import`, `libSDL3.so.0`) needs a glibc symbol version newer than
2.39, Ubuntu 24.04's and the oldest system the README names; the log has
each binary's newest.

The release's arm64 archive is built by CI, since the maintainer's machine
is x86-64. Pushing a `v*` tag runs `ci.yml`, and its `linux-arm64` job,
once its gating tests pass, runs `tools/package_linux.sh <tag>` and uploads
the archive, its `.sha256` and the packaging log as the artifact
`ico-pc-<tag>-linux-arm64` (kept 30 days). Pull requests and pushes to main
never package. To fetch it (the artifact keeps the `dist/` and
`build-host/` folders):

```sh
gh run list --workflow ci.yml --branch <tag> --limit 1     # the tag's run id
gh run download <run-id> -n ico-pc-<tag>-linux-arm64 -D build-host/tmp/ci-arm64
mv build-host/tmp/ci-arm64/dist/ico-pc-<tag>-linux-arm64.tar.gz* dist/
(cd dist && sha256sum -c ico-pc-<tag>-linux-arm64.tar.gz.sha256)
tar -xzOf dist/ico-pc-<tag>-linux-arm64.tar.gz ico-pc-<tag>/VERSION.txt  # the tag's commit
```

Ask someone with an arm64 handheld to try that archive before attaching it
to the release (first start from a disc image, play, sound, gamepad,
quit). Its `ico_pc.map` symbolizes aarch64 crash addresses with a
symbolizer that reads aarch64, such as llvm-mingw's `llvm-symbolizer` or
`llvm-addr2line` on the x86-64 host.
The three package scripts share `tools/package_common_lib.sh` (the label
check, the log, `fail` and `run`, and the clean worktree with the
`ICO_PKG_FILES` overlay); the guides and the package's `ico-pc.ini` (its
text is `pkg_write_ini`) come from `tools/package_docs_lib.sh`, and the
folder notes and the archive checks from `tools/package_textures_lib.sh`.

The documents: `README.md` is the players' front page, and the player
guides it links are `docs/FAQ.md`, `CONTROLS.md`, `OPTIONS.md`,
`TEXTURE_PACKS.md`, `MODEL_PACKS.md`, `RESHADE.md`, `ANDROID.md`,
`PORTABLE_MODE.md` and `TROUBLESHOOTING.md`. They link each other with
relative links, so they read the same on GitHub and in a package. Both
package scripts copy them unchanged into a `docs/` folder at the package
root, beside `README.md` (the list is `pkg_player_docs` in
`tools/package_docs_lib.sh`; a new guide goes there too, and the scripts
fail when one is missing from the archive). `BUILDING.md`, `LEGAL.md` and
`THIRD_PARTY.md` are for developers and are linked from the README by their
GitHub address; only the Linux package carries `THIRD_PARTY.md`, at its
root. The APK carries no documents besides `assets/VERSION.txt` and
`assets/NOTICES.txt`: Android players read the guides on GitHub. Release
notes are drafted in `docs/RELEASE_NOTES_<tag>.md`.

`tools/package_android.sh <label>` does the same for Android
(`dist/ico-pc-<label>-android.apk`, log `build-host/pkg-android-<label>.log`):
`tools/fetch_android.sh`, `assembleRelease -PicoLabel=<label>`,
`zipalign -P 16 -f 4`, then `apksigner sign` (v2 and v3) and
`apksigner verify --print-certs` (the certificate digests go in the log and on
screen) and `zipalign -c -P 16 -v 4`. The unstripped `libmain.so`, `ico_pc.map`
and `VERSION.txt` are left in `dist/stage/android/`.

The signing key is never in the repository. The script reads it from
`android-keystores/` next to the repository folder (`../android-keystores/`
from the root): the upload keystore (a `.jks` file, key alias `upload`) and
`readme.txt` beside it, whose first line starting with `password` (as
`password: <value>`) holds the password; without such a line the first
non-empty line is used. The script stops at once when either file is
missing. The password goes to `apksigner` through the environment and is
never printed or logged, and the keystore is never copied. `tools/check_no_rom.sh`
fails on a tracked key file or keystore password. To turn a crash offset from
a release build into a function name, run
`llvm-symbolizer --obj=dist/stage/android/libmain.so <offset>` (from the NDK's
`toolchains/llvm/prebuilt/linux-x86_64/bin/`) with the stage folder of the same
build.

A release uploads the three packages:

```sh
gh release create <tag> dist/ico-pc-<tag>-win.zip dist/ico-pc-<tag>-linux.tar.gz dist/ico-pc-<tag>-android.apk
```

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
`config/sha1sums.txt`; it needs pycdlib, from `tools/requirements.txt`, and no MIPS tools).
Nothing under `baserom/` is committed ([`LEGAL.md`](LEGAL.md)). Without it
those two tests are not built, and CI never has it.
