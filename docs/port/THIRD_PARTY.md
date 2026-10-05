# Third-party code and tools

What the PC port builds against, links or runs at build/test time, with the
pinned version, licence and where it comes from. Licence policy:
[`docs/research/licences.md`](../research/licences.md) (no GPL; everything
statically linked into the MIT program must be MIT-compatible and its notice
must ship with the binary). Packages add their rows here.

Pins live in the fetch scripts; changing a version means changing the pin
(version and SHA-256) there and the row here.

## Linked into the program

| component | version | licence | fetched by | used for |
| --- | --- | --- | --- | --- |
| SDL3 | 3.4.18 | zlib (`LICENSE.txt` in the release) | `tools/fetch_deps.sh`: Linux x86-64 built from `SDL3-3.4.18.tar.gz`; Windows from `SDL3-devel-3.4.18-mingw.tar.gz` (both SHA-256 pinned, the digests GitHub lists for the release assets) | window, Vulkan surface (`SDL_Vulkan_CreateSurface`); audio output (`SDL_OpenAudioDeviceStream`, port/audio/out_sdl.c; the Linux build has the ALSA backend since 4B, stamp `SDL3-3.4.18+audio`, the Windows prebuilt WASAPI); later input. Linked as a shared library (`SDL3.dll`, `libSDL3.so.0`) |
| volk | tag `vulkan-sdk-1.4.363.0` | MIT (`LICENSE.md`, "Copyright (c) 2018-2026 Arseny Kapoulkine") | `tools/fetch_deps.sh`, GitHub tag archive, SHA-256 pinned | Vulkan meta-loader: `volk.c` is compiled into `ico_rhi_vk`; it loads `libvulkan.so.1` / `vulkan-1.dll` at run time, so the program has no link-time Vulkan dependency |
| Vulkan-Headers | tag `vulkan-sdk-1.4.363.0` | Apache-2.0 OR MIT (`LICENSE.md`; the headers carry `SPDX-License-Identifier: Apache-2.0 OR MIT`) | `tools/fetch_deps.sh`, GitHub tag archive, SHA-256 pinned | headers only; MIT chosen |
| Ittiam libmpeg2 | AOSP `platform/external/libmpeg2` tag `android-16.0.0_r4` = commit `a97c2a1f0a796dc32bed80d3353c69c5fc07c750` | Apache-2.0 (`LICENSE`, `NOTICE` "Copyright (C) 2015 The Android Open Source Project", `MODULE_LICENSE_APACHE2`; installed with the source) | `tools/fetch_deps.sh`: `git fetch --depth 1` of the tag from android.googlesource.com, the commit id checked, then the SHA-256 of `git archive --format=tar` of the commit (`3a8a3028cc5e0c8177d9c81e687318c3eb81c2a9fd3219d50c94eabf1a8f3ea1`, 2026-10-05); googlesource's `+archive` tarballs are not byte-stable (two downloads, two digests) and the GitHub mirror lacks the commit. Installed to `tools/toolchain/deps/libmpeg2/` (`common/`, `decoder/`, licence files; `SKIP_LIBMPEG2=1`, `LIBMPEG2_TAG`, `LIBMPEG2_COMMIT`, `LIBMPEG2_TAR_SHA256` override) | the FMV's MPEG-2 video decoder (`port/fmv/m2v.c`, docs/port/FMV.md). Compiled by `port/fmv/CMakeLists.txt` with each preset's compiler into the static library `ico_libmpeg2` (Linux gcc, mingw-w64 gcc, the sanitizer build): the generic C decoder only (the library's `riscv` function selectors, which select the generic routines), no SIMD; its `common/ithread.c` (pthreads) is not compiled and `port/fmv/ithread_single.c` (MIT, ours) provides the same functions for a single-threaded decoder. No library file is modified. Chosen over the FFmpeg fallback because it builds cleanly for both targets: nothing of FFmpeg is fetched or linked |

Binary-release obligations: libmpeg2's Apache-2.0 licence text and its
`NOTICE` content (Apache-2.0 section 4; the files are not modified, so no
change notices are owed; `ithread_single.c` is a separate file of ours, not
a modification), volk's MIT notice and (if MIT is not chosen for
them) the Vulkan headers' notice go into the release's third-party notices
file (packaging, Phase 5). SDL3's zlib licence asks for nothing in binaries;
its notice is included anyway.

The Vulkan loader and the GPU driver are the user's system components and
are not shipped.

## Build and test time only (nothing ships)

| component | version | licence | fetched by | used for |
| --- | --- | --- | --- | --- |
| Khronos validation layer | Debian 13 `vulkan-validationlayers` 1.4.309.0-1 | Apache-2.0 (upstream KhronosGroup/Vulkan-ValidationLayers `LICENSE.txt`) | `tools/fetch_deps.sh`, Debian package, SHA-256 pinned | the RHI tests run with it (and synchronisation validation) and fail on any error |
| X11 extension headers | Debian 13 `libxext-dev` 1.3.4-1+b3, `libxrandr-dev` 1.5.4-1+b3, `libxi-dev` 1.8.2-1, `libxcursor-dev` 1.2.3-1, `libxfixes-dev` 6.0.0-2+b4, `libxrender-dev` 0.9.12-1 | MIT/X11 | `tools/fetch_deps.sh`, scratch tree deleted after the SDL3 build | compiling SDL3's X11 backend on a host without them; SDL dlopens the libraries at run time |
| ALSA and PulseAudio headers | Debian 13 `libasound2-dev` 1.2.14-1+deb13u1, `libpulse-dev` 17.0+dfsg1-2+b1 (SHA-256 from the trixie `main/binary-amd64` Packages index, its InRelease signature checked against the Debian archive keyring, 2026-10-05) | LGPL-2.1+ (alsa-lib: the Debian copyright file; libpulse: LGPL-2.1+ per PulseAudio's licence, not re-checked here) | `tools/fetch_deps.sh`, same scratch tree as the X11 headers, deleted after the SDL3 build | compiling SDL3's ALSA backend (and its PulseAudio backend when the build host has `libpulse.so.0`); nothing of either is linked: SDL dlopens `libasound.so.2` / `libpulse.so.0` at run time |
| DXC | v1.9.2609 (`linux_dxc_2026_09_28.x86_x64.tar.gz`, SHA-256 `96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1`, the digest GitHub lists for the asset) | University of Illinois/NCSA plus bundled notices (`LICENSE-LLVM.txt`, `LICENSE-MS.txt` in the release; the release also holds `libdxil.so`, the DXIL validator/signer) | `tools/fetch_deps.sh` into `tools/toolchain/deps/dxc/` (`SKIP_DXC=1`, `DXC_VERSION`, `DXC_LINUX_FILE`, `DXC_LINUX_SHA256` override) | the shader toolchain (`cmake/IcoShaders.cmake`, docs/port/SHADERS.md): HLSL to SPIR-V and signed DXIL at build time, on the build host, for every preset. `port/rhi/test/shaders/gen_shaders.sh` takes `DXC=`. The embedded blobs are compiler output, not a derivative of the compiler; nothing of DXC is linked or shipped |

## Not used

Mesa lavapipe (MIT) runs the RHI tests in the container, but it is the
host's driver (`/usr/share/vulkan/icd.d/lvp_icd.json`), not something the
repository fetches.
