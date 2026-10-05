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
| SDL3 | 3.4.18 | zlib (`LICENSE.txt` in the release) | `tools/fetch_deps.sh`: Linux x86-64 built from `SDL3-3.4.18.tar.gz`; Windows from `SDL3-devel-3.4.18-mingw.tar.gz` (both SHA-256 pinned, the digests GitHub lists for the release assets) | window, Vulkan surface (`SDL_Vulkan_CreateSurface`); later input and audio. Linked as a shared library (`SDL3.dll`, `libSDL3.so.0`) |
| volk | tag `vulkan-sdk-1.4.363.0` | MIT (`LICENSE.md`, "Copyright (c) 2018-2026 Arseny Kapoulkine") | `tools/fetch_deps.sh`, GitHub tag archive, SHA-256 pinned | Vulkan meta-loader: `volk.c` is compiled into `ico_rhi_vk`; it loads `libvulkan.so.1` / `vulkan-1.dll` at run time, so the program has no link-time Vulkan dependency |
| Vulkan-Headers | tag `vulkan-sdk-1.4.363.0` | Apache-2.0 OR MIT (`LICENSE.md`; the headers carry `SPDX-License-Identifier: Apache-2.0 OR MIT`) | `tools/fetch_deps.sh`, GitHub tag archive, SHA-256 pinned | headers only; MIT chosen |

Binary-release obligations: volk's MIT notice and (if MIT is not chosen for
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
| DXC | v1.9.2609 (`linux_dxc_2026_09_28.x86_x64.tar.gz`, SHA-256 `96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1`, the digest GitHub lists for the asset) | University of Illinois/NCSA plus bundled notices (`LICENSE-LLVM.txt`, `LICENSE-MS.txt` in the release; the release also holds `libdxil.so`, the DXIL validator/signer) | `tools/fetch_deps.sh` into `tools/toolchain/deps/dxc/` (`SKIP_DXC=1`, `DXC_VERSION`, `DXC_LINUX_FILE`, `DXC_LINUX_SHA256` override) | the shader toolchain (`cmake/IcoShaders.cmake`, docs/port/SHADERS.md): HLSL to SPIR-V and signed DXIL at build time, on the build host, for every preset. `port/rhi/test/shaders/gen_shaders.sh` takes `DXC=`. The embedded blobs are compiler output, not a derivative of the compiler; nothing of DXC is linked or shipped |

## Not used

Mesa lavapipe (MIT) runs the RHI tests in the container, but it is the
host's driver (`/usr/share/vulkan/icd.d/lvp_icd.json`), not something the
repository fetches.
