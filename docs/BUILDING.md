# Building

How to build, test and package the PC port. The build is CMake with Ninja. It compiles the game's C
under `ico2/` for the host, with `port/` standing in for the PS2 hardware and Sony's libraries. The
PS2 ELF build belongs to the decompilation (<https://github.com/nathanialf/ico>); this repository
has none ([port/GAME_CODE.md](port/GAME_CODE.md)). How the port works, module by module, is in the
developer documents: start at [port/README.md](port/README.md) for the index and
[port/ARCHITECTURE.md](port/ARCHITECTURE.md) for the whole picture.

## Quickstart (Debian 13, Ubuntu 24.04)

```sh
sudo apt-get install build-essential curl xz-utils python3 python3-venv \
    libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev \
    libxfixes-dev libxrender-dev libasound2-dev libdbus-1-dev libxss-dev
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
tools/fetch_toolchain.sh          # about 1.6 GB under tools/toolchain/, no root
CMAKE=tools/toolchain/cmake/bin
$CMAKE/cmake --preset linux-x64 -DICO_HEADLESS=OFF -DICO_LINK_EXE=ON
$CMAKE/cmake --build build-host/linux-x64 --target all tests
$CMAKE/ctest --test-dir build-host/linux-x64 -j4
```

The test programs are not part of the default target: `--target all tests`
builds the game and every test, plain `--build` only the game and the tools
(see ["Building a few targets"](#building-a-few-targets)).

`tools/setup.sh` does the venv step and installs the git pre-commit hook
([port/CONVENTIONS.md](port/CONVENTIONS.md#the-pre-commit-hook)).

The build needs no disc image and no `baserom/`: the program holds no disc
data. The game reads the player's own PAL disc image (SCES-50760) at run
time. The first run extracts it into the archive [`ico.o2r`](port/ARCHITECTURE.md#terms), and the
data tables load from that archive at boot (`port/platform/main_host.c` (`mount_game_data`);
`port/data/extract.h`; [port/DATA.md](port/DATA.md#the-first-run)).

## Toolchains: `tools/fetch_toolchain.sh`

Run it once. It needs `curl`, `tar`, `sha256sum` and `dpkg-deb`, no root,
and fills `tools/toolchain/` (gitignored, about 1.6 GB):

| directory | what | from |
| --- | --- | --- |
| `llvm-mingw/` | clang 23, lld and the mingw-w64 UCRT runtime for x86-64 Windows; the same clang targets Linux | [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) release 20260922, `ucrt-ubuntu-22.04-x86_64`, SHA-256 pinned (not fetched on an arm64 host unless `SKIP_LLVM_MINGW=0`, which takes the pinned `-aarch64` build) |
| `mingw-gcc/` | mingw-w64 gcc 14 and binutils for x86-64 Windows (x86-64 hosts only) | Debian 13 `gcc-mingw-w64-*-win32` 14.2.0-19+27+b1, `binutils-mingw-w64-*` 2.44-3+12+b1, `mingw-w64-*-dev` 12.0.0-5, SHA-256 pinned |
| `cmake/` | CMake 4.4.4 (`cmake`, `ctest`) | [Kitware's release](https://github.com/Kitware/CMake/releases/tag/v4.4.4) `cmake-4.4.4-linux-x86_64.tar.gz` (`-linux-aarch64` on an arm64 host), SHA-256 pinned |
| `ccache/` | ccache 4.11.3, the compiler launcher every preset names (`tools/toolchain/ccache/bin/ccache`) | [ccache's release](https://github.com/ccache/ccache/releases/tag/v4.11.3) `ccache-4.11.3-linux-x86_64.tar.xz`, SHA-256 pinned (no arm64 build exists: that host builds without it; `SKIP_CCACHE=1` skips it elsewhere) |
| `deps/` | SDL3, volk, the Vulkan headers, the validation layer, DXC, SPIRV-Cross, libmpeg2, libchdr and the AppImage tools (`deps/appimage/<x86_64\|aarch64>/`: appimagetool unpacked, and the AppImage runtime) | `tools/fetch_deps.sh`, which `fetch_toolchain.sh` runs last ([`THIRD_PARTY.md`](THIRD_PARTY.md)) |

The pins the fetch scripts share (the SDL3 version and its SHA-256,
`DEB_SNAPSHOT`, and the `fetch_deb` helper that downloads a Debian package
into the caller's temporary directory) are in `tools/fetch_common.sh`. The
Debian packages come from `deb.debian.org`, falling back to
`snapshot.debian.org` once a version is superseded. `SKIP_MINGW_GCC=1` and
`SKIP_CMAKE=1` skip the mingw-gcc tree and CMake; the header of each script
lists its other overrides. The toolchain files take `ICO_LLVM_MINGW` and
`ICO_MINGW_GCC` from the environment to use copies elsewhere.

The Linux SDL3 (`deps/sdl3/linux-<arch>/`) is built with OpenGL, GLX and EGL for the OpenGL
renderer: `tools/fetch_deps.sh` unpacks the headers of Debian 13's `libgl-dev`, `libglx-dev` and
`libegl-dev` (SHA-256 pinned) when the host has none, configures SDL with `SDL_OPENGL` and
`SDL_OPENGLES`, and stops when GLX or EGL did not come out. SDL loads `libGL`, `libEGL` and the
driver at run time, so the program links no GL library. Its stamp (`.ico-release`) ends in `+gl`; a
tree whose stamp lacks it is built again by the next run.

The Linux presets use the host's `gcc` and glibc (`CMAKE_C_COMPILER` `gcc`, `CMakePresets.json`):
gcc 14 on Debian 13. On Ubuntu 24.04, whose `gcc` is 13, add `-DCMAKE_C_COMPILER=gcc-14` (package
`gcc-14`) as CI does (`.github/workflows/ci.yml`, `ICO_CC`); gcc 13 also builds, since
`cmake/IcoFlags.cmake` drops the warning flags a compiler does not know. Ninja comes from
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
    libpipewire-0.3-dev libspa-0.2-dev \
    libdbus-1-dev libxss-dev \
    libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev \
    libdrm-dev libgbm-dev libegl-dev
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
tools/fetch_toolchain.sh          # builds DXC from source: a few minutes
CMAKE=tools/toolchain/cmake/bin
$CMAKE/cmake --preset linux-arm64 -DCMAKE_C_COMPILER=gcc-14
$CMAKE/cmake --build --preset linux-arm64 --target all tests
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
  the pinned Debian packages (the X11, audio, D-Bus and GL headers when the
  host has none, the validation layer). SDL picks the backend at run time.
- Microsoft publishes no aarch64 Linux DXC, so `tools/fetch_deps.sh` builds
  the pinned release's tag from source (the commit id and the SHA-256 of its
  `git archive` checked, as libmpeg2's and libchdr's, and the same two checks
  on each of its three submodules) into `deps/dxc/` with the
  host's g++: a few minutes on 8 cores. It builds the `dxc` program and
  `libdxcompiler.so` only; there is no `libdxil.so`, and the arm64 preset
  compiles no DXIL (`ICO_SHADERS_DXIL=OFF`).
- The preset is the [window build](port/ARCHITECTURE.md#terms) with the program linked, Vulkan and OpenGL
  (`ICO_RHI_D3D12=OFF`, `ICO_RHI_D3D11=OFF`), where `android-arm64` is Vulkan only (`ICO_RHI_OPENGL=OFF` too). The movie decoder uses
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

Xcode, CMake and Ninja are required on the Mac building these targets. `tools/fetch_moltenvk.sh`
installs the SHA-256-pinned MoltenVK 1.4.2, builds SDL3 3.4.18 as a static library for the selected
SDK, and runs `tools/fetch_deps.sh --game-only` for the pinned libchdr and libmpeg2 source trees:

```sh
tools/fetch_moltenvk.sh ios
cmake --preset ios-moltenvk
cmake --build --preset ios-moltenvk
```

Use `macos` and the `macos-moltenvk` preset for a native Apple silicon renderer build. That preset
builds `ico_rhi`, with the Vulkan backend linked to MoltenVK (`ICO_RHI_MOLTENVK`). The iOS package
targets ARM64 devices, iOS 15 or newer; the macOS target is ARM64, macOS 12 or newer. The pinned iOS
package has no simulator slice.

To compile the renderer and its HLSL shaders too, supply a **macOS host** DXC executable (it runs
during the build, even for iOS):

```sh
cmake --preset ios-moltenvk -DICO_DXC=/absolute/path/to/dxc
cmake --build build-host/ios-moltenvk --target ico_render
```

The app includes `SDL3/SDL_main.h` and creates an `SDL_WINDOW_VULKAN` window. SDL uses the
exported, linked `vkGetInstanceProcAddr` to create its Metal-backed Vulkan surface; the backend
enables portability enumeration and the device's portability subset (`port/rhi/vk/vk_device.c`). There is no Metal backend of
its own. `ICO_MOLTENVK_ROOT` overrides the extracted release root. Apple package notices come from
`tools/gen_notices.py --platform ios --out NOTICES.txt` (or `macos`).

For a Mac without a host DXC, build it once from source (Git, Xcode, CMake and Ninja; a substantial
LLVM build). The `ios-app` and `macos-app` presets default `ICO_DXC` to the result,
`tools/toolchain/deps/dxc-build/bin/dxc`; these are the steps the Apple packages workflow runs:

```sh
git clone --depth 1 --branch v1.9.2602 --recurse-submodules \
  https://github.com/microsoft/DirectXShaderCompiler tools/toolchain/deps/dxc-src
cmake -S tools/toolchain/deps/dxc-src -B tools/toolchain/deps/dxc-build -G Ninja \
  -C tools/toolchain/deps/dxc-src/cmake/caches/PredefinedParams.cmake \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_SPIRV_CODEGEN=ON \
  -DHLSL_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF -DSPIRV_BUILD_TESTS=OFF \
  "-DCMAKE_CXX_FLAGS=-Wno-unknown-warning-option -Wno-invalid-specialization"
cmake --build tools/toolchain/deps/dxc-build --target dxc --parallel 6
```

### Checking out on a Mac

The game sources `.gitattributes` checks out as EUC-JP ([port/GAME_CODE.md](port/GAME_CODE.md#euc-jp-sources)) need
macOS's `EUC-JP-MS` instead: macOS's iconv does not keep their ASCII backslashes under `EUC-JP`. For
these files `EUC-JP-MS` gives the same bytes as `EUC-JP` does on Linux. Set it in the clone's own
attributes, which take precedence over the tracked file, and check the files out again:

```sh
attrs="$(git rev-parse --git-path info/attributes)"
mkdir -p "$(dirname "$attrs")"
sed -n 's/ working-tree-encoding=EUC-JP$/ working-tree-encoding=EUC-JP-MS/p' .gitattributes >>"$attrs"
files=$(sed -n 's/ working-tree-encoding=EUC-JP$//p' .gitattributes)
rm -f $files && git checkout -- $files
```

### macOS application

With a macOS host DXC (above), build the native Apple silicon application bundle with:

```sh
tools/fetch_moltenvk.sh macos
cmake --preset macos-app
cmake --build --preset macos-app
```

The result is `build-host/macos-app/ICO.app`, an unsigned ARM64 app bundle that starts from Finder
or the command line. On the first start it asks for the player's own PAL SCES-50760 disc image; the
verified game data goes to the user's Application Support folder, and no image is bundled with the
app.

`tools/package_macos.sh <label> [CMake arguments...]` builds the distributable, ad-hoc-signed
bundle with its notices: `dist/ICO.app`, `dist/ico-pc-<label>-macos-arm64.zip`,
`dist/ico-pc-<label>-macos-arm64.dmg` and the link map `dist/ico-pc-<label>-macos-arm64.map`, which
turns a crash offset into a function name. The label (`vX.Y.Z`) gives the bundle its version, as
`tools/package_win.sh` does; without one the version is all zeros and the files are
`ico-pc-macos-arm64.*`. The package is not notarized.

### iPhone IPA for LiveContainer

Build the checkout into an unsigned IPA:

```sh
tools/package_ios.sh <label> -DICO_DXC=/absolute/path/to/macos/host/dxc
```

The label (`vX.Y.Z`) gives the bundle its version, as for macOS, and the files their names. Omit
`-DICO_DXC` when the `ios-app` preset's default DXC exists. The script writes
`dist/ico-pc-<label>-ios-unsigned.ipa`, with the licences inside the bundle, and the link map
`dist/ico-pc-<label>-ios.map`. No provisioning profile or signing identity is needed.

iOS and macOS blend through the fixed-function path by default, because the renderer takes that
path on MoltenVK (`rd__dst_init` in `port/render/rd_dst_read.c`; `[dev] blend` or `ICO_RD_BLEND`
still forces any mode); every other platform picks its blend path from the device. The iOS entry
point (`port/platform/ios/main_ios.c`) sets nothing for it. It runs the game on a guarded 32 MB [fiber](port/ARCHITECTURE.md#terms) stack on SDL's
UIKit main thread.

Import the IPA into LiveContainer. Copy your own extracted `ico.o2r` into the guest app's
**Documents** folder through LiveContainer's data-folder access, then start ICO; an `Ico_PAL.iso`,
`.chd`, `.bin` or `.cue` in that folder is extracted on the first start instead
([port/DATA.md](port/DATA.md#the-first-run)). The IPA holds no game data. Settings
(`config.toml`), saves and logs also live in Documents. The app takes touch and controller input,
runs full screen in landscape, and pauses audio and rendering in the background. iOS takes the
mobile defaults (`port/game/video_options.c` (`ico_video_default_framerate`); the mobile rules
apply under `__ANDROID__` and `ICO_IOS`): a 60 FPS cap and automatic resolution for the Enhanced
preset; a fixed Resolution
such as 2x stays as chosen (2x draws about four times the pixels of 1x). iOS builds write no
benchmark CSV and no per-effect GPU timings (`port/platform/window_host.c`, `rd__perf_post` in
`port/render/rd_perf.c`). A problem report needs the log, the device and iOS
versions, the app build and the graphics settings.

## Presets

```sh
CMAKE=tools/toolchain/cmake/bin
$CMAKE/cmake --preset linux-x64
$CMAKE/cmake --build --preset linux-x64 --target all tests
$CMAKE/ctest --preset linux-x64   # the tests run on the Linux presets only
$CMAKE/cmake --preset win-x64
$CMAKE/cmake --build --preset win-x64 --target all tests   # the test programs are built, not run
```

The test presets are `linux-x64`, `linux-arm64`, `asan`, `fptrap` and `linux-x64-clang`
(`CMakePresets.json`, `testPresets`); a build preset's targets are `all` unless it names others, so
`--target all tests` is what builds the test programs.

`-B <dir>` after `--preset` builds a preset into another directory, which is
how several configurations of one preset live side by side under
`build-host/`. The build writes nothing outside its build directory but ccache's cache
(`~/.cache/ccache`).

| preset | target | compiler |
| --- | --- | --- |
| `linux-x64` | Linux x86-64, headless by default | host `gcc` |
| `win-x64` | Windows x64, window build | `mingw-gcc` x86-64 |
| `asan` | Linux x86-64, `-fsanitize=address,undefined`, `-O1` | host `gcc` |
| `fptrap` | `linux-x64` with float divide-by-zero and invalid unmasked in the simulation | host `gcc` |
| `linux-x64-clang`, `win-x64-clang` | the same two targets, headless | llvm-mingw clang 23 (the host glibc for Linux) |
| `linux-arm64` | Linux arm64 (aarch64), built on an arm64 host ([below](#linux-arm64)); window build, `ICO_LINK_EXE=ON`, no Direct3D 12, no Direct3D 11 and no DXIL shaders | host `gcc` |
| `android-arm64` | Android arm64-v8a, `libmain.so` alone: a compile check without Gradle (`tools/fetch_android.sh` first); window build, `ICO_LINK_EXE=ON`, Vulkan only (no Direct3D 12, Direct3D 11 or OpenGL) and no DXIL shaders | NDK clang |
| `ios-moltenvk`, `macos-moltenvk` | iOS 15 and macOS 12 arm64, the renderer libraries over MoltenVK, built on a Mac ([above](#apple-vulkan-renderer-moltenvk)) | Xcode clang |
| `ios-app`, `macos-app` | the iOS app (unsigned) and the macOS app bundle, `ICO_LINK_EXE=ON` | Xcode clang |

The port targets x86-64 (Windows and Linux) and arm64 (Linux, Android, iOS
and macOS).

### Options

| cache variable | default | effect |
| --- | --- | --- |
| `ICO_HEADLESS` | `ON` on every preset except `win-x64`, `linux-arm64`, `android-arm64` and the four Apple presets (`OFF` without a preset) | `OFF` is the window build: the game draws through `port/render` (`ICO_RD=1`) into an SDL3 window. `ON` is the [headless build](port/ARCHITECTURE.md#terms) for trace and test runs: no window and no renderer, `ICO_HEADLESS=1`. Both compile the same game sources |
| `ICO_LINK_EXE` | `OFF` | links `ico_pc` (`port/platform/main_host.c`), the game's program. Without it only the libraries and the tests are built |
| `ICO_STRICT_WARNINGS` | `OFF` | makes `-Wreturn-type`, `-Wimplicit-function-declaration` and `-Wstrict-prototypes` errors. While it is off, the C89-era diagnostics that modern compilers make errors by default (implicit declarations and int, int/pointer conversions, incompatible pointers, return mismatches) are warnings |
| `ICO_BUILD_BLOCKED` | `OFF` | also compiles the sources `cmake/IcoExclusions.cmake` leaves out (the lists are empty; they are the place to park a game source that stops compiling) |
| `ICO_HEAP_STATS` | `OFF` | the game's allocator (`fumi/ios/memory.c`) reports to `port/platform/arena.c`, which logs each heap [partition](port/ARCHITECTURE.md#terms)'s high-water mark |
| `ICO_FPTRAP` | `OFF` | set by the `fptrap` preset |
| `ICO_SANITIZE` | empty | the `-fsanitize=` list; the `asan` preset sets `address,undefined` |
| `ICO_RHI_VULKAN` | `ON` | builds the Vulkan renderer backend |
| `ICO_RHI_D3D12` | `ON` for 64-bit Windows | builds the Direct3D 12 renderer backend next to the Vulkan one |
| `ICO_RHI_D3D11` | `ON` for 64-bit Windows | builds the Direct3D 11 renderer backend next to the others (it compiles; it has not run on any Windows machine). It needs the toolchain's `d3d11.h`, `d3d11_1.h`, `dxgi1_6.h` and `d3dcompiler.h`, and turns `ICO_SHADERS_HLSL_SOURCE` on, which embeds the HLSL sources the backend compiles at run time |
| `ICO_RHI_OPENGL` | `ON` except on Android and Apple targets (their presets set it `OFF`) | builds the OpenGL 4.3 renderer backend (`ico_rhi_opengl`, [port/RHI.md](port/RHI.md#opengl)) where SDL3 is found; its context and entry points come through SDL3, so nothing links a GL library. It also turns `ICO_SHADERS_GLSL` on |
| `ICO_RHI_MOLTENVK` | `ON` on Apple | links the Vulkan backend to MoltenVK |
| `ICO_SHADERS_DXIL` | `ON` | also compiles the shaders to DXIL for the Direct3D 12 backend; the arm64 and Apple presets turn it off |
| `ICO_SHADERS_GLSL` | `ON` with `ICO_RHI_OPENGL` when `tools/fetch_deps.sh` fetched SPIRV-Cross, else `OFF` | also translates the shaders to GLSL 4.30 with SPIRV-Cross for the OpenGL backend (`ICO_SPIRV_CROSS` names another executable; `SKIP_SPIRV_CROSS=1` skips the fetch). With `ICO_RHI_OPENGL` on and this off, the configure step warns: the OpenGL backend is built but has no shaders and cannot draw |
| `ICO_VERSION_LABEL` | empty | with `ICO_LINK_EXE`: the package label (`vX.Y.Z` or `vX.Y.Z-rcN`). Its numbers go into the Windows version information and the Apple bundles' Info.plist; the label itself, written to `generated/ico_version.h`, is shown in the log header and on the Settings menu's Main page, and is empty (nothing shown) when it does not parse as a version (`CMakeLists.txt`, `ICO_VERSION_LABEL_C`). The package scripts and `android/app/build.gradle` pass it; a build without it has an all-zero version |
| `ICO_RC_TOOLS` | `OFF` | the release candidates' own tooling, for rc packages alone ([Release candidate tools](#release-candidate-tools)); `ON` defines `ICO_RC_TOOLS=1` for every source, and a non-empty `ICO_VERSION_LABEL` that does not end in `-rcN` is refused |
| `ICO_BASE_ELF` | `baserom/pal/baseelf.elf` | the base ELF the data loader's reference tests read; without it they are not built |
| `ICO_BASE_ELF_JPN` | `baserom/jpn/baseelf.elf` | the Japanese disc's base ELF (SCPS-11003), for the same tests on that disc |
| `ICO_BASE_ELF_KOR` | `baserom/kor/baseelf.elf` | the Korean disc's base ELF (SCPS-56001), for the same tests on that disc |
| `ICO_DISC_IMAGE`, `ICO_DISC_IMAGE_JPN`, `ICO_DISC_IMAGE_KOR`, `ICO_TEXPACK_DIR` | see [Tests](#tests) | the disc images and the texture pack folder the tests read |
| `ICO_DXC`, `ICO_DEPS_DIR` | the fetched copies | the shader compiler and the dependency tree |
| `ICO_MOLTENVK_ROOT`, `ICO_LIBCHDR_DIR` | the fetched copies | MoltenVK's extracted release and the libchdr source tree (`cmake/IcoMoltenVK.cmake`, `port/data/CMakeLists.txt`) |

At run time, the environment variable `ICO_TTY` (any value) turns on the
game's `scePrintf` output on stdout, which the retail game sent to the
development kit's TTY; it is off otherwise (`port/platform/kernel_host.c`,
`scePrintf`). Release programs have no console, so use it with `--console` on a program started
from a terminal.

## Building a few targets

A change builds only the targets it touches.

```sh
tools/dev_build.sh . ico_pc rd_present_test -- -R '^rd_present$'
tools/dev_build.sh /path/to/worktree ico_pc tests -- -L render -j4
```

`tools/dev_build.sh` configures one build tree per checkout, builds exactly the named targets under
a shared lock, runs `ctest` with the arguments after `--` and prints one summary line
([port/TESTING.md](port/TESTING.md#the-developer-build-helper)). The full build and the full `ctest`
stay the gate on `main`.

- **Tests.** `cmake --build <dir>` builds the game and the tools (`rd_replay_tool`, `mc_import`);
  `--target tests` builds every test program, `--target rd_present_test` one, and CI builds
  `--target all tests`. `ctest` on a tree whose test programs were not built reports each of those
  tests as "Not Run" and fails. The gate runs `ctest -j4` (CI passes `--parallel 4`; the tests use
  well under 1 GB each, `rd_pixel` the most at about 0.8 GB, so four at once fit in 12 GB).
  `ctest -L render` runs one area, `ctest -N -L <label>` lists it, and `-R '^rd_blur\.'` selects
  all of one sharded test's shards. How the `tests` target, the labels and the shards work is
  [port/TESTING.md](port/TESTING.md#the-framework)'s.
- **ccache.** Every preset sets `CMAKE_C_COMPILER_LAUNCHER` to
  `${sourceDir}/tools/toolchain/ccache/bin/ccache`, installed by
  `tools/fetch_toolchain.sh`. The cache is `~/.cache/ccache` (no `base_dir`):
  the compiler's `-g` records the absolute source directory, so each checkout
  (and the package worktree `build-host/pkg-wt`) fills its own entries and a
  rebuild of unchanged files in the same directory is served from the cache.
  The object files are the ones the compiler writes; `ccache -s` shows the
  hit rate. Where the file is missing (an arm64 host, a toolchain not yet
  fetched) configure prints a line and the build runs without it. A worktree
  shares the main checkout's toolchain by a link:
  `ln -s <main>/tools/toolchain <worktree>/tools/toolchain`.
- **Shaders.** Each of the HLSL shader steps lists the headers it includes
  from the compiler itself (`dxc -MF` depfile), so editing one header
  rebuilds only the shaders that include it.
- **Packages.** `tools/package_win.sh` and `tools/package_linux.sh` build
  `ico_pc mc_import`; the
  Android package goes through Gradle, which builds `ico_pc` and the three
  libadrenotools hook libraries (`main_hook`, `hook_impl`, `file_redirect_hook`).

## Compilers

GCC is the primary compiler, and the clang presets build the same sources; CI builds both so neither
drifts. Every game source compiles with both (`cmake/IcoExclusions.cmake` blocks none). llvm-mingw
ships no Linux sanitizer runtimes, so the `asan` preset is GCC either way.

Windows builds target mingw (gcc or llvm-mingw), never MSVC (clang-cl or a `*-windows-msvc`
triple): the presets and toolchain files are the GNU ABI ones (`CMakePresets.json`,
`cmake/toolchains/`), and only those are built and tested in CI.

## How the game is compiled

The game's C under `ico2/` is compiled for the host from source lists generated from the retail
link order, with the semantics options and warnings policy, audits and data tables that
[port/GAME_CODE.md](port/GAME_CODE.md#what-is-compiled) describes.

## EUC-JP sources

Some game sources are checked out as EUC-JP and must keep their bytes
([port/GAME_CODE.md](port/GAME_CODE.md#euc-jp-sources); on a Mac,
[Checking out on a Mac](#checking-out-on-a-mac)). The formatting, naming and hook rules are in
[port/CONVENTIONS.md](port/CONVENTIONS.md).

## Tests

`ctest` runs the unit tests on the Linux presets; the Windows presets build the test programs
without running them. The test programs are in the `tests` target, not in `all`
([Building a few targets](#building-a-few-targets)), and each test carries its area's label
(`ctest -L render`). A test that needs something the machine may lack is reported as skipped,
which passes ([port/TESTING.md](port/TESTING.md#the-framework)): the disc (`vfs_disc`,
`archive_disc`, `texpack_disc` and the other disc tests), a Vulkan device (most `rd_*` tests,
`rhi_vk`, `shaders_pixel`, `vu1`), Pillow or saved frame dumps. Which tests a change needs is
[port/TESTING.md](port/TESTING.md#which-tests-a-change-needs)'s.

- `ICO_DISC_IMAGE` (cache variable, default `baserom/Ico_PAL.iso`) is the PAL disc image the disc
  tests read, and `ICO_DISC_IMAGE_JPN` (default `baserom/Ico_JPN.iso`) the Japanese one
  (SCPS-11003) and `ICO_DISC_IMAGE_KOR` (default `baserom/Ico_KOR.iso`) the Korean one
  (SCPS-56001). A worktree without `baserom/` configures with `-DICO_DISC_IMAGE=/nonexistent
  -DICO_BASE_ELF=/nonexistent` and the same for `ICO_DISC_IMAGE_JPN`, `ICO_BASE_ELF_JPN`, `ICO_DISC_IMAGE_KOR` and `ICO_BASE_ELF_KOR`
  (`tools/dev_build.sh` does), so no test can start the game.
- `ICO_TEXPACK_DIR` (cache variable, default `build-host/tmp/texpack/SCES-50760/replacements`; the
  environment variable of the same name sets the default at the first configure) is a PCSX2 texture
  pack's replacements folder. When it exists, `texpack_disc`, `texpack_e2e` and `texpack_pack_files`
  match the pack's files against the disc's textures. No pack file is ever committed.
- `ICO_BASE_ELF` adds the data loader's reference tests
  ([Appendix](#appendix-maintainers-the-base-elf)).
- `ICO_FOG_DUMP` (cache variable, default `dist/dumps/frame-20261009-083248-v13836.rddump`) is a
  player dump with the depth fog for `rd_fog_depth_copy_dump` (skipped without it).
- `ICO_BOOT_DUMP_DIR` (cache variable, default empty) is a start-up's frame dumps for
  `rd_boot_present`; `ICO_BOOT_DUMP_OLD` (default `OFF`) says they come from a build whose frames 118
  to 120 draw stage 1 (`port/render/CMakeLists.txt`).

The Android code is tested on Linux where it can be: `android_paths` (the files folder layout),
`iso_import` (the first start's copy of the chosen disc image), `lifecycle` (background and
foreground on fake operations) and `touch` (the touch overlay's mapping). On a device the log is
mirrored to logcat: `adb logcat -s ico-pc`.

The GPU paths a desktop driver does not take are tested on the Linux Vulkan device (lavapipe in
CI) by forcing them; each variant runs the same tests and expectations again under an environment
variable:

| tests | environment | read in | what it forces |
| --- | --- | --- | --- |
| `*_nodual` (`rd_pixel`, `rd_present`, `rd_fog`, `rd_shadow`, `rd_water`, ..., `shaders_pixel`) | `ICO_RD_NO_DUAL=1` | `port/render/rd_core.c` | blending in two passes, for GPUs without dual-source blending |
| `rhi_vk_nodual` | `ICO_VK_FAKE_NO_DUAL=1` | `port/rhi/vk/vk_device.c` | the dual-source feature cleared at device creation |
| `rhi_vk_d24s8` | `ICO_VK_FAKE_D24S8=1` | `port/rhi/vk/vk_device.c` | the D24S8 depth fallback when D32F_S8 is missing |
| `*_mali` (`rd_layout`, `rd_tex`, `rd_gsbase`, `rd_shadow`, `rd_fog`, `rd_water`, `rd_raw`, `rd_interp`, `rd_mirror`, `rd_perf`, `rd_pixel`, `rd_debug`, `rd_present`, `rd_crt`, `rd_mesh` sharded, `shaders_pixel`, `vu1`, `rd_video`, `rhi_vk`; `port/render/CMakeLists.txt`, `_ico_mali_tests`) | `ICO_VK_FAKE_LIMITS=mali` | `port/rhi/vk/vk_device.c` | a Mali-G68's limits (four descriptor sets, 16 sampled images a stage, 256-byte storage offsets, 4096 memory allocations); `rhi_vk_mali` checks that what goes past them is refused by the limit's name, and `rd_perf_mali` creates the whole reachable pipeline set |
| `*_minlimits` (`rd_pixel`, `rd_present`, `rd_mesh`, `rd_interp`) | `ICO_VK_FAKE_LIMITS=min` | `port/rhi/vk/vk_device.c` | the limits every Vulkan device must meet |
| `*_storage` (`rd_pixel`, `rd_fog`, `rd_present`) | `ICO_RD_BLEND=storage` | `port/render/rd_dst_read.c` | the exact blend through a storage-image read |
| `*_fixed` (`rd_pixel`, `rd_fog`, `rd_present`, `rd_crt`) | `ICO_RD_BLEND=fixed` | `port/render/rd_dst_read.c` | the fixed-function blend (it rounds up); `rd_present` and `rd_crt` compare that blend's recorded hashes |

`ICO_VK_FAKE_NO_DST=1` and `ICO_DX_FAKE_NO_DST=1` report a device that cannot read the pixel it
draws over. The start-up log names the blend path [rd](port/ARCHITECTURE.md#terms) took on the device (`rd: blend: exact (attachment
read)`, `exact (interlock)`, `exact (rov)` or `fixed-function (rounds up)`; `rd__blend_exact_name`
in `port/render/rd_dst_read.c`).

How the VU programs treat a triangle outside the GS region is in
[port/VU1_PROGRAMS.md](port/VU1_PROGRAMS.md). The frame-dump checks, the coverage corpus, F12
captures and `rd_replay_tool` are in [port/TESTING.md](port/TESTING.md).

## Continuous integration

`.github/workflows/ios.yml` (the "Apple packages" workflow) runs when a release is published, or
by hand (`workflow_dispatch`) with a `tag` input naming an existing release
(`gh workflow run ios.yml -f tag=<tag>`); never for a push or a pull request. On `macos-15` it
builds the tag's source with `tools/package_ios.sh <tag>` (the tag must be `vX.Y.Z`), building the
pinned DXC from source on a cache miss, and a second job on Ubuntu attaches
`ico-pc-<tag>-ios-unsigned.ipa` to that release (`--clobber`, so a re-run replaces it). An
independent pair of jobs does the same for the macOS app with `tools/package_macos.sh <tag>`,
attaching `ico-pc-<tag>-macos-arm64.zip` and `.dmg` (the app is ad-hoc signed, not notarized; its
link map stays in the run's artifact for 30 days). The workflow reads the repository only; the
upload jobs alone may write to it. No secret, signing identity or game data is used.

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
| host packages, venv | gcc 14 (the runner's default gcc 13 also configures, since `cmake/IcoFlags.cmake` drops warning flags a compiler does not know), the X11, ALSA, PulseAudio and PipeWire (`libpipewire-0.3-dev`, `libspa-0.2-dev`) headers SDL3 builds against, lavapipe (`mesa-vulkan-drivers`, `libvulkan1`) so the render tests run on a CPU Vulkan device, the GL headers (`libgl-dev`, `libglx-dev`, `libegl-dev`) and llvmpipe (`libgl1-mesa-dri`, `libgl1`, `libegl1`) so the OpenGL tests run on a CPU OpenGL device, `tools/requirements.txt` |
| cache and `tools/fetch_toolchain.sh` | `tools/toolchain/` is cached on the hash of `fetch_toolchain.sh`, `fetch_deps.sh`, `fetch_common.sh` and `fetch_android.sh`, restored and saved as separate steps so a cold fetch is saved even when a later step fails |
| ccache | `~/.cache/ccache` is restored before the builds and saved after them (also when a build fails), keyed by the job or preset and the same four scripts' hash, with the commit as the last part so every run saves its entries, each capped at 5 GiB (`CCACHE_MAXSIZE` in the workflow's `env`; `tools/fetch_toolchain.sh` sets the same cap for a local ccache); the `linux-arm64` job has no ccache |
| `tools/check_no_rom.sh` | the IP scan over every tracked file |
| `tools/format.sh --check` | clang-format over the tracked C |
| `tools/strip_host_gates.py --check` | no `ICO_HOST` conditional in `ico2/` or `sce/` |
| `tools/region_blocks.py` (`--selftest`, then `check`) | no compile-time region block (`#ifdef VERSION_JPN`) or conditional on the region in `ico2/` or `sce/`, the run-time region forms legal, `ico2/common/include/region.h`, `port/core/region_table.c` and the per-region glue fresh against `config/regions.txt`; exit 0 required ([GAME_CODE.md](port/GAME_CODE.md#regions)) |
| `tools/stack_overread_audit.py` (`--selftest`, then the tree) | no game code in `ico2/` that relies on the PS2's stack layout: a matrix or vector call that reads or writes past the local it is given, or a local that is only written; checked exceptions are in `tools/stack_overread_allow.txt` |
| `tools/struct_cast_audit.py` (`--selftest`, then the tree) | no fixed array, buffer or smaller record in `ico2/` cast to a struct whose host size is larger (records holding pointers are wider on the host than the [EE](port/ARCHITECTURE.md#terms)'s 4-byte pointers: `ClipWork` is 0xF0 bytes, not 0xC0), sized by compiling the file with the host compiler; checked exceptions are in `tools/struct_cast_allow.txt` |
| `tools/check_call_types.py` | no call through a function-pointer cast that passes fewer arguments than the function it reaches reads, and no `extern` in an `ico2/` `.c` file that differs from its definition in parameter count, integer width or return width; checked exceptions are in `tools/check_call_types_allow.txt` |
| `gen_data_desc.py`, `gen_layout_asserts.py`, `gen_sources.py` with `--check` | the generated files are fresh |
| `linux-x64` headless | configure with `-DICO_LINK_EXE=ON`, build `all tests` (the targets `all` and `tests`), `ctest` |
| `linux-x64` window | `-DICO_HEADLESS=OFF -DICO_LINK_EXE=ON` into `build-host/linux-x64-window`, build, `ctest` |
| `linux-x64-clang` | build, `ctest` |
| `win-x64` | cross-compile the window build; `ico_pc.exe` and `SDL3.dll` must exist; the tests are built, not run |
| `extra`: `asan` | AddressSanitizer + UBSan (Debug, `-O1`), headless with `-DICO_LINK_EXE=ON`, build, `ctest` |
| `extra`: `fptrap` | float divide-by-zero and invalid trap, headless with `-DICO_LINK_EXE=ON`, build, `ctest` |
| `extra`: `win-x64-clang` | cross-compile the window build (`-DICO_HEADLESS=OFF`) with llvm-mingw clang; `ico_pc.exe` and `SDL3.dll` must exist |

The `linux-arm64` job builds the `linux-arm64` preset on GitHub's arm64
runner and runs its tests, with its own toolchain cache
(`toolchain-arm64-...`, the same four scripts; a cold fetch builds DXC from
source) that it saves. Its host packages are `linux`'s plus the Wayland and
KMSDRM headers, `g++` and `git` ([Linux arm64](#linux-arm64)). Of the hygiene
checks it runs only `tools/check_no_rom.sh`. The gating `ctest` leaves out the render tests
that compare filtered pixels with a tolerance of 1 or 2 (`rd_pixel`,
`rd_gsbase`, `rd_present`, `rd_crt` and every variant of them, such as
`rd_present_depth_copy` and the `_nodual`, `_minlimits` and `_mali` runs, and
`rd_replay_tool` and `rd_replay_tool_list`, which need `rd_pixel`'s dump):
lavapipe on an arm64 host filters a step differently from lavapipe on
x86-64 and misses them by an LSB or two. A second step runs those tests
alone and does not gate (`continue-on-error`), so their output is still in
the log. The rest gate as on `linux`: `fog_lut` runs natively,
`fog_lut_arm64` skips (it looks for an x86-64 NDK and qemu), and the tests
that need the disc or saved frames skip (`fmv_fields`, `rd_boot_present`,
`rd_fog_depth_copy_dump`) or run only their synthetic part (`shadow_spawn`,
`sndn2`, `fmv`).

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

`.github/workflows/pages.yml` publishes the download page,
<https://nathanialf.github.io/ico-pc/>, on every push to main that touches
`site/` and by hand (`workflow_dispatch`). The page is one static file,
`site/index.html`, with no build step: in the visitor's browser it reads the
releases list from GitHub's API (unauthenticated, kept ten minutes in the
tab), picks the file for the visitor's device and shows the chosen version's
release notes, so a new release needs no change to the page. The job uploads
`site/` with `actions/upload-pages-artifact` and deploys it with
`actions/deploy-pages` (permissions `pages: write` and `id-token: write`, the
`github-pages` environment, one deployment at a time). It only works once
the repository's Pages source is set to GitHub Actions (Settings > Pages >
Build and deployment > Source: **GitHub Actions**), a one-time setting.

## Packages

`tools/package_win.sh <label>` and `tools/package_linux.sh <label>` build the packages for HEAD in a
clean worktree (`dist/ico-pc-<label>-win.zip`, `dist/ico-pc-<label>-linux.tar.gz`, and the
AppImage below). Neither contains
game data; both carry the README, the player guides, the licence files and the save importer
(`x64/tools/mc_import.exe` with its `SDL3.dll` and a short `README.txt`), and a make-portable script
(`x64/make_portable.cmd` in the zip, `make_portable.sh` in the tar; the sources are
`tools/portable/`). Each script also writes a second archive from the same stage, the portable
download: `dist/ico-pc-<label>-win-portable.zip` and `dist/ico-pc-<label>-linux-portable.tar.gz`. It
differs in three ways: its `ico-pc.ini` has `portable=1` on (the game makes `userdata` beside the
program at the first start), the README has a paragraph saying so, and it has no make-portable
script. Both scripts check each archive's entries afterwards, and
`tools/test/package_textures_test.sh` (ctest `package_textures`) covers the helpers. The archives
hold no developer programs: no `rhi_d3d12_test`, `rd_replay_tool` or `compare_backends`
(developer programs in a download make browsers and Windows flag it; the scripts assert their
absence). The link map ships beside the program in both archives (`x64/ico_pc_x64.map`,
`ico_pc.map`; the scripts assert it is there): a crash or watchdog report names its functions from
it on the player's machine ([PLATFORM.md](port/PLATFORM.md#the-symbol-lines)). It stays in
`dist/stage` too, and `tools/package_win.sh` builds only `ico_pc` and `mc_import`. The
Windows program carries version information (company, product, file version `x.y.z.N`: N is the
release candidate's number for a label `vX.Y.Z-rcN`, 0 for `vX.Y.Z`; `ico_version_from_label` in
`cmake/IcoVersion.cmake`), from `-DICO_VERSION_LABEL=<label>` which `package_win.sh` passes
(`port/platform/win/ico_pc.rc`; a build without the label has an all-zero version). It has no icon
and no signature. `ico_pc.manifest` has no `dpiAwareness`: SDL3 sets the process's DPI awareness
itself when the window system starts. `tools/package_linux.sh` packages for the host's architecture:
on an arm64 host it builds `linux-arm64` into `dist/ico-pc-<label>-linux-arm64.tar.gz` (stage
`dist/stage/linux-arm64/`, log `build-host/pkg-linux-arm64-<label>.log`) and the portable
`dist/ico-pc-<label>-linux-arm64-portable.tar.gz`, the same contents with the aarch64 program and
SDL3 (and, in `NOTICES.txt`, the notices of the Wayland protocol files SDL3 is built with). On
either architecture it fails when a shipped binary (`ico_pc`, `tools/mc_import`, `libSDL3.so.0`)
needs a glibc symbol version newer than 2.39, Ubuntu 24.04's and the oldest system the README names;
the log has each binary's newest.

Last, from the same stage, `tools/package_linux.sh` writes the AppImage,
`dist/ico-pc-<label>-linux-x86_64.AppImage` (`-linux-aarch64.AppImage` on arm64), with its
`.sha256` beside it, written by the script on both architectures. The AppDir is `AppRun` (a link to
`usr/bin/ico_pc`), `usr/bin/` with `ico_pc`, `ico_pc.map`, `libSDL3.so.0` and `mc_import`,
`usr/share/doc/ico-pc/` with the README, the guides, `LICENSE`, `NOTICES.txt`, `THIRD_PARTY.md`
and `VERSION.txt`, and `ico-pc.desktop`, `ico-pc.svg` and `.DirIcon` (a link to the icon) from
`tools/appimage/`; no `ico-pc.ini`, folder notes or make-portable script, because the program
keeps all of those beside the AppImage file ([PLATFORM.md](port/PLATFORM.md#folders-ico-pcini-and-the-user-folder)).
appimagetool runs unpacked from `tools/toolchain/deps/appimage/<arch>/squashfs-root` with its own
`mksquashfs` and `--runtime-file` pointing at the pinned runtime, so packaging needs no FUSE and
downloads nothing; `SOURCE_DATE_EPOCH` is the commit's time, so the same commit and stage give the
same file. The script then unpacks the result with its own `--appimage-extract` (no FUSE either)
into `build-host/tmp/`, fails unless the file list is exactly the one above, the two links point
where they should and `usr/bin/ico_pc` is the staged program, and runs the `readelf -d` checks on
it as on the archive's. The icon is the Android launcher's placeholder disc
(`android/app/src/main/res/drawable/ic_launcher_foreground.xml`), the same path data. Both tools
are pinned in `tools/fetch_deps.sh` (section 2b; `SKIP_APPIMAGE=1` skips it), to tagged releases
rather than the projects' moving `continuous` ones; when a download does not match its pin, the
fetch fails with "refresh the pin".

The release's arm64 archive is built by CI on GitHub's arm64 runner. Pushing a `v*` tag runs `ci.yml`, and its `linux-arm64` job,
once its gating tests pass, runs `tools/package_linux.sh <tag>` and uploads
both archives (normal and portable), the aarch64 AppImage, their `.sha256` files and the packaging log as the artifact
`ico-pc-<tag>-linux-arm64` (kept 30 days). Pull requests and pushes to main
never package. To fetch it (the artifact keeps the `dist/` and
`build-host/` folders):

```sh
gh run list --workflow ci.yml --branch <tag> --limit 1     # the tag's run id
gh run download <run-id> -n ico-pc-<tag>-linux-arm64 -D build-host/tmp/ci-arm64
mv build-host/tmp/ci-arm64/dist/ico-pc-<tag>-linux-arm64*.tar.gz* build-host/tmp/ci-arm64/dist/ico-pc-<tag>-linux-aarch64.AppImage* dist/
(cd dist && sha256sum -c ico-pc-<tag>-linux-arm64.tar.gz.sha256 ico-pc-<tag>-linux-arm64-portable.tar.gz.sha256 ico-pc-<tag>-linux-aarch64.AppImage.sha256)
tar -xzOf dist/ico-pc-<tag>-linux-arm64.tar.gz ico-pc-<tag>/VERSION.txt  # the tag's commit
```

Before the archive is attached to the release it is tried on an arm64
handheld: first start from a disc image, play, sound, gamepad, quit. Its `ico_pc.map` symbolizes aarch64 crash addresses with a
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
fail when one is missing from the archive). `BUILDING.md`, `LEGAL.md`,
`THIRD_PARTY.md` and `docs/port/` are for developers and are linked from the README by their
GitHub address; only the Linux package carries `THIRD_PARTY.md`, at its
root. The APK carries no documents besides `assets/VERSION.txt` and
`assets/NOTICES.txt`: Android players read the guides on GitHub. Release
notes are drafted in `docs/RELEASE_NOTES_<tag>.md`.

`tools/package_android.sh <label>` does the same for Android
(`dist/ico-pc-<label>-android.apk`, log `build-host/pkg-android-<label>.log`):
`tools/fetch_android.sh`, `assembleRelease -PicoLabel=<label>`,
`zipalign -P 16 -f 4`, then `apksigner sign` (v2 and v3) and
`apksigner verify --print-certs` (the certificate digests go in the log and on
screen) and `zipalign -c -P 16 -v 4`. Before the alignment it adds
`assets/libmain.syms.gz` to the APK, stored: `llvm-nm -n --defined-only` of the unstripped
`libmain.so`, its code symbols (`T`, `t`, `W`, `w`), gzip-compressed, from which a crash or
watchdog report names its functions on the phone
([PLATFORM.md](port/PLATFORM.md#the-symbol-lines)); the signed APK is checked to hold it stored. The
unstripped `libmain.so`, `ico_pc.map`, `libmain.syms.gz` and `VERSION.txt` are left in
`dist/stage/android/`.

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

Before publishing, scan each desktop archive on [VirusTotal](https://www.virustotal.com/)
and read the result. The game is unsigned, so a few engines may flag it
anyway; for a flag from Microsoft Defender or Chrome's Safe Browsing, send
the file to the false-positive forms: Microsoft
(https://www.microsoft.com/en-us/wdsi/filesubmission) and Google
(https://safebrowsing.google.com/safebrowsing/report_error/). The release
notes may link the VirusTotal results (the FAQ tells players they can check
there).

A release carries twelve files. `gh release create` uploads nine: the Windows, Linux x86-64 and
Linux arm64 archives, each with its portable twin, the x86-64 and aarch64 AppImages (the arm64 pair
and the aarch64 AppImage come from CI, above), and the APK. The `.sha256` files are for checking
the copies before the upload and are not attached. Publishing the release starts the Apple packages workflow, which attaches three more: the IPA
and the macOS zip and dmg ([Continuous integration](#continuous-integration); run it by hand with
`gh workflow run ios.yml -f tag=<tag>` when the release event does not start it):

```sh
gh release create <tag> dist/ico-pc-<tag>-win.zip dist/ico-pc-<tag>-win-portable.zip dist/ico-pc-<tag>-linux.tar.gz dist/ico-pc-<tag>-linux-portable.tar.gz dist/ico-pc-<tag>-linux-arm64.tar.gz dist/ico-pc-<tag>-linux-arm64-portable.tar.gz dist/ico-pc-<tag>-linux-x86_64.AppImage dist/ico-pc-<tag>-linux-aarch64.AppImage dist/ico-pc-<tag>-android.apk
```

## Release candidate tools

A release candidate (a label ending in `-rcN`) carries tooling a release never does. Today that is
one row: on Android, Settings > Main > **Share a bug report**, directly below Save a picture for a
bug report, which opens the system's share menu with `logs/ico-pc.log`, `logs/ico-pc-previous.log`,
the newest `dumps/*.zip` and `dumps/*-present.png` and the newest `logs/report-*.zip`, each when it
is there (`port/platform/share_report.c`; a phone's files folder cannot be reached without a
computer). The gate is mechanical at every step:

- The switch is CMake's `ICO_RC_TOOLS` (default `OFF`) and Gradle's `icoRcTools` (default `false`;
  it passes `-DICO_RC_TOOLS` to the native build, sets `BuildConfig.RC_TOOLS` and enables the
  report `FileProvider` in `AndroidManifest.xml` through the `icoRcTools` manifest placeholder; the
  provider entry is always in the manifest but disabled, so the system never starts it).
- Every package script takes it from `tools/check_rc_text.py --cmake-flag <label>` (or
  `--gradle-flag`), which gives `ON` for a label matching `-rc[0-9]+$` and `OFF` for every other
  label, and passes it explicitly every time, so a reused build folder keeps no `ON`.
  `tools/package_android.sh` refuses an `ICO_GRADLE_ARGS` that names `icoRcTools`. CMake (with
  `ICO_LINK_EXE`) and Gradle refuse the switch with a non-empty label that is not an rc's.
- After the build, each script runs `tools/check_rc_text.py --label <label>` on what it built (the
  Linux and Windows program, the unsigned APK with every entry inflated, the macOS and iOS
  executables): for a release label the row's English text, "Share a bug report", must be absent
  (UTF-8 and UTF-16), for an rc label present; the script fails otherwise. CI's debug APK is
  checked the same way with the label `ci`.
- In the sources, the row, its strings in all seven tables, the host's handler and the JNI call are
  inside `#if ICO_RC_TOOLS` (`port/ui/settings.h` and `strings.h` default it to 0);
  `ui_settings_set_share_report_handler` exists in every build and keeps nothing at 0.
- ctests: `rc_tools_gate` (the checker's own cases, and the scripts, `CMakeLists.txt`,
  `build.gradle` and the manifest using it), `rc_tools_binary` (this build's `ico_pc` holds the text
  exactly when `ICO_RC_TOOLS` is on), `settings_extra` (`testShareReport`: at 0 the Main page has
  its sixteen rows and no string is the row's text) and `share_report` (the files picked).

The other settings tests pin the Main page's sixteen rows of the default build; ctest is run on a
build with `ICO_RC_TOOLS` off.

## Appendix, maintainers: the base ELF

What stays in the tree besides the host build, and why:

| kept | for |
| --- | --- |
| `sce/`, `ico2/vusrc/` | the game includes `sce/`'s headers under the SDK names; the [VU1](port/ARCHITECTURE.md#terms) sources are the shaders' reference. Only `sce/libsndn2/sound.c` is in a host target (`ico_sndn2`) |
| `config/link_order.<slug>.txt`, `config/data_*.pal.txt`, `config/link.pal.ld` | the source list `gen_sources.py` reads, the data members' schema, and the retail link script as a record of the PS2 layout. Nothing links with them |
| `config/symbol_addrs.pal*.txt`, `config/struct_classes.txt`, `config/tables_manifest.txt` | the generators' inputs (`tools/gen_data_desc.py`, `tools/gen_data_c.py`, `tools/gen_layout_asserts.py`) |
| `config/ico.pal.yaml` | the PS2 build's splat layout, a record; nothing reads it (its first line) |
| `tools/extract_elf.sh`, `tools/extract_elf.py` | the maintainer step below |

Nothing here compiles for the PS2, links a PS2 ELF or assembles the VU1
programs; the decompilation keeps that build.

The loader's reference test and `gen_data_desc.py --manifest` read the boot
ELF from `baserom/pal/baseelf.elf`, which `tools/extract_elf.sh` writes from
`baserom/Ico_PAL.iso` (the disc's `SCES_507.60`, SHA-1
`da3644c54c26fe760f3b6a591a5fc2eab396ed2b`, checked against
`config/sha1sums.<slug>.txt`; it needs pycdlib, from `tools/requirements.txt`, and no MIPS tools).
Nothing under `baserom/` is committed ([`LEGAL.md`](LEGAL.md)). Without it
those two tests are not built, and CI never has it.
