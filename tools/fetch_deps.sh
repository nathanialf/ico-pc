#!/usr/bin/env bash
# =============================================================================
# tools/fetch_deps.sh
#
# Fetches the PC port's library dependencies into tools/toolchain/deps/
# (gitignored), user-local, no root. tools/fetch_toolchain.sh calls it last;
# it can also run on its own. Idempotent: each item is stamped with its
# pinned version and skipped when the stamp matches.
#
#   deps/vulkan-headers/   Khronos Vulkan-Headers (Apache-2.0 OR MIT), the
#                          include/ tree only
#   deps/volk/             volk meta-loader (MIT), volk.c and volk.h; the
#                          Vulkan backend loads libvulkan / vulkan-1.dll at run
#                          time through it, so no import library is needed
#   deps/sdl3/linux-x64/   SDL3 (zlib) built from the release source tarball
#                          with the pinned CMake and the host gcc, installed
#                          as a CMake package (lib/cmake/SDL3); X11 video and
#                          ALSA (and PulseAudio when the host has libpulse)
#                          audio backends, loaded with dlopen
#   deps/sdl3/linux-arm64/ the same on an arm64 (aarch64) host, with the
#                          Wayland and KMSDRM video backends as well (the
#                          handhelds run a Wayland compositor or none)
#   deps/sdl3/mingw/       SDL's own mingw development release: its
#                          x86_64-w64-mingw32 prefix
#   deps/vulkan-validation/ the Khronos validation layer (Apache-2.0) from
#                          Debian 13, for the RHI tests only; never shipped
#   deps/dxc/              DirectX Shader Compiler (NCSA; Linux x86-64 release,
#                          built from the same tag's source on arm64):
#                          the shader toolchain's build tool (cmake/IcoShaders.cmake);
#                          never linked or shipped
#   deps/libmpeg2/         Ittiam libmpeg2 (Apache-2.0) source tree, the FMV
#                          decoder; compiled by port/fmv/CMakeLists.txt
#   deps/libchdr/          libchdr (BSD-3-Clause) source tree with its bundled
#                          LZMA, miniz and zstd decoders, the .chd disc image
#                          reader; compiled by port/data/CMakeLists.txt
#
# docs/THIRD_PARTY.md records the versions and licences.
#
# Overrides:
#   VULKAN_SDK_TAG, VULKAN_HEADERS_SHA256, VOLK_SHA256
#   SDL3_VERSION, SDL3_SRC_SHA256, SDL3_MINGW_SHA256
#   DXC_VERSION, DXC_LINUX_FILE, DXC_LINUX_SHA256 (x86-64 release)
#   DXC_COMMIT, DXC_TAR_SHA256 (arm64 source build)
#   LIBMPEG2_TAG, LIBMPEG2_COMMIT, LIBMPEG2_TAR_SHA256
#   LIBCHDR_TAG, LIBCHDR_COMMIT, LIBCHDR_TAR_SHA256, ZSTD_LICENSE_SHA256
#   SKIP_SDL3_LINUX=1, SKIP_SDL3_MINGW=1, SKIP_VALIDATION_LAYER=1, SKIP_DXC=1,
#   SKIP_LIBMPEG2=1, SKIP_LIBCHDR=1
#   DEB_SNAPSHOT     snapshot.debian.org fallback for the Debian packages
#   ICO_CMAKE        the CMake used for the SDL3 source build (default: the
#                    pinned one in tools/toolchain/cmake, else the PATH's)
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/tools/toolchain/deps"
mkdir -p "$DEST"
TMP="$(mktemp -d "$DEST/.fetch.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# fetch and stamped
# shellcheck source=tools/fetch_common.sh
source "$ROOT/tools/fetch_common.sh"

# unpack_debs <dest> "<pool path> <sha256>"...: fetch pinned Debian packages
# (fetch_deb, tools/fetch_common.sh) into <dest>. Their lib*.so development
# links, relative to their own lib dir,
# are pointed at the host's runtime libraries (dangling when the host has
# none, so CMake does not find them). DEB_ARCH and DEB_MULTIARCH
# (tools/fetch_common.sh) are the host's.
unpack_debs() {
    local dest="$1" entry so tgt
    shift
    command -v dpkg-deb >/dev/null 2>&1 || {
        echo "fetch_deps: dpkg-deb not found; cannot unpack the Debian header packages" >&2
        exit 1
    }
    for entry in "$@"; do
        fetch_deb "$entry" "$dest"
    done
    for so in "$dest/usr/lib/$DEB_MULTIARCH"/lib*.so; do
        [[ -L "$so" ]] || continue
        tgt="$(readlink "$so")"
        tgt="${tgt##*/}"
        rm -f "$so"
        if [[ -e "/usr/lib/$DEB_MULTIARCH/$tgt" ]]; then
            ln -s "/usr/lib/$DEB_MULTIARCH/$tgt" "$so"
        fi
    done
}

# --- 1. Vulkan-Headers and volk ----------------------------------------------
#
# Both are taken at the same Vulkan SDK tag so volk's generated entry points
# match the headers. GitHub's tag archives; the SHA-256s were taken from the
# archives on 2026-10-05.
VK_TAG="${VULKAN_SDK_TAG:-vulkan-sdk-1.4.363.0}"
VK_HEADERS_SHA256="${VULKAN_HEADERS_SHA256:-4a078be12bef21cfebc09d878b77a63cff9d68f899254a0b00d0e37ef73e7f7e}"
VOLK_SHA256="${VOLK_SHA256:-1547d8d74395d4048fb3f4a6313da56db626e89b55db238d0d7e8944c8a645f3}"

if stamped "$DEST/vulkan-headers" "$VK_TAG"; then
    echo "==> Vulkan-Headers ${VK_TAG} already at $DEST/vulkan-headers"
else
    fetch "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/${VK_TAG}.tar.gz" \
        "$VK_HEADERS_SHA256" "$TMP/vulkan-headers.tar.gz"
    tar -C "$TMP" -xzf "$TMP/vulkan-headers.tar.gz"
    rm -rf "$DEST/vulkan-headers"
    mkdir -p "$DEST/vulkan-headers"
    mv "$TMP/Vulkan-Headers-${VK_TAG}/include" "$DEST/vulkan-headers/include"
    cp "$TMP/Vulkan-Headers-${VK_TAG}/LICENSE.md" "$DEST/vulkan-headers/" 2>/dev/null || true
    cp -r "$TMP/Vulkan-Headers-${VK_TAG}/LICENSES" "$DEST/vulkan-headers/" 2>/dev/null || true
    echo "$VK_TAG" > "$DEST/vulkan-headers/.ico-release"
fi

if stamped "$DEST/volk" "$VK_TAG"; then
    echo "==> volk ${VK_TAG} already at $DEST/volk"
else
    fetch "https://github.com/zeux/volk/archive/refs/tags/${VK_TAG}.tar.gz" \
        "$VOLK_SHA256" "$TMP/volk.tar.gz"
    tar -C "$TMP" -xzf "$TMP/volk.tar.gz"
    rm -rf "$DEST/volk"
    mkdir -p "$DEST/volk"
    cp "$TMP/volk-${VK_TAG}/volk.c" "$TMP/volk-${VK_TAG}/volk.h" "$TMP/volk-${VK_TAG}/LICENSE.md" \
        "$DEST/volk/"
    echo "$VK_TAG" > "$DEST/volk/.ico-release"
fi

# --- 2. SDL3 -----------------------------------------------------------------
#
# The release tools/fetch_common.sh pins (SDL3_VERSION, SDL3_SRC_SHA256). The
# SHA-256s are the digests GitHub lists for the release assets (gh api
# repos/libsdl-org/SDL/releases/latest).
SDL3_MINGW_SHA256="${SDL3_MINGW_SHA256:-049b9dd711e3fb3f8d84feb998dfec54a75e17cf539c4a80ff4d058d839771cf}"
SDL3_BASE="https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}"

# Linux x86-64 and arm64: built from source. The host has no SDL3 package
# and no root; the X11 backend builds against the host's libX11/libxcb
# headers, the others are probed by SDL's CMake and left out when their
# headers are missing. SDL loads its video and audio backends with dlopen, so
# the library runs on hosts with more or fewer of them. On arm64 the Wayland
# and KMSDRM backends are required too: the build host needs their headers
# and wayland-scanner (docs/BUILDING.md, "Linux arm64").
SDL3_LINUX="$DEST/sdl3/linux-$ICO_HOST_ARCH"
# "+audio": the build with the ALSA/PulseAudio headers; a tree
# stamped by an older run of this script (X11 only, no audio backend) is
# rebuilt. "+wayland+kmsdrm": the arm64 build's video backends;
# "+wlnotices": the Wayland protocols' notices are copied beside it.
SDL3_LINUX_ID="SDL3-${SDL3_VERSION}+audio$(by_arch "" "+wayland+kmsdrm+wlnotices")"
SDL3_LINUX_VIDEO=()
if [[ "$ICO_HOST_ARCH" == "arm64" ]]; then
    SDL3_LINUX_VIDEO=(-DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_KMSDRM=ON)
fi
CMAKE_BIN="${ICO_CMAKE:-}"
if [[ -z "$CMAKE_BIN" ]]; then
    if [[ -x "$ROOT/tools/toolchain/cmake/bin/cmake" ]]; then
        CMAKE_BIN="$ROOT/tools/toolchain/cmake/bin/cmake"
    else
        CMAKE_BIN="cmake"
    fi
fi

if [[ "${SKIP_SDL3_LINUX:-0}" == "1" ]]; then
    echo "==> SKIP_SDL3_LINUX=1; not building $SDL3_LINUX"
elif stamped "$SDL3_LINUX" "$SDL3_LINUX_ID"; then
    echo "==> SDL3 ${SDL3_VERSION} (linux-$ICO_HOST_ARCH) already at $SDL3_LINUX"
else
    fetch "${SDL3_BASE}/SDL3-${SDL3_VERSION}.tar.gz" "$SDL3_SRC_SHA256" "$TMP/SDL3.tar.gz"
    tar -C "$TMP" -xzf "$TMP/SDL3.tar.gz"
    # X11 extension headers SDL's X11 backend needs and a stock host may not
    # have (the container has libx11-dev but not libxext-dev). Unpacked from
    # pinned Debian 13 packages into a scratch tree for this build only; the
    # libraries themselves are the host's (SDL dlopens them by soname).
    # One package per host architecture (DEB_ARCH), the same versions; the
    # arm64 SHA-256s are the trixie main/binary-arm64 Packages index's, read
    # as the amd64 ones were (2026-10-09).
    X11DEV="$TMP/x11dev"
    mkdir -p "$X11DEV"
    if ! [[ -f /usr/include/X11/extensions/Xext.h ]]; then
        X11_DEBS_amd64=(
            "pool/main/libx/libxext/libxext-dev_1.3.4-1+b3_amd64.deb e6bd898976d762a6955ec465870e7f12a3b149f4c5b231b0687e293ed5726532"
            "pool/main/libx/libxrandr/libxrandr-dev_1.5.4-1+b3_amd64.deb e408e4f8c77135725e9055d445635587225839a929d348c04b85adfa84bdefb1"
            "pool/main/libx/libxi/libxi-dev_1.8.2-1_amd64.deb be3ca6202327858845aa0a8d70374b08dae5c89250ad8997256e02b2833501d3"
            "pool/main/libx/libxcursor/libxcursor-dev_1.2.3-1_amd64.deb 57f2fc575496edc9f3dd286423494499d231a8b1cdb417bb8401f6b66b584a26"
            "pool/main/libx/libxfixes/libxfixes-dev_6.0.0-2+b4_amd64.deb 1070e0721765992c5355a9585f6eae9e0d11437a7d2b4a2c28123e6e5b0c0b2f"
            "pool/main/libx/libxrender/libxrender-dev_0.9.12-1_amd64.deb 55121741c44e03cbeb03dabb5721ded1341c987f055d7cae991de7431c9edfa6"
        )
        X11_DEBS_arm64=(
            "pool/main/libx/libxext/libxext-dev_1.3.4-1+b3_arm64.deb 8e4334abc63d18cb9074aa4571c8fa4aa43fced729fa9c2137179864bb4dd0f9"
            "pool/main/libx/libxrandr/libxrandr-dev_1.5.4-1+b3_arm64.deb d664c2210c38fd821cd4115e66411db6e02e0e42e684c04b274025f3a7150c54"
            "pool/main/libx/libxi/libxi-dev_1.8.2-1_arm64.deb 1dc1fa7078d6bf8874c64d33a4f568e2acd413e0eb2a15d54b3d134675f6619e"
            "pool/main/libx/libxcursor/libxcursor-dev_1.2.3-1_arm64.deb 8af61c44c13a06818187c8bcf25b7e99b554413878a9a025629d0792dc4569e7"
            "pool/main/libx/libxfixes/libxfixes-dev_6.0.0-2+b4_arm64.deb e74bfebdf2fda7e2d4ce82ea5e9f9affa0b32e3dff92432ab246d8424fe93371"
            "pool/main/libx/libxrender/libxrender-dev_0.9.12-1_arm64.deb dc1434f847bb727248d6ef219acd814c0b3fe0f846bf7db08320c18e34d09adb"
        )
        declare -n X11_DEBS="X11_DEBS_$DEB_ARCH"
        unpack_debs "$X11DEV" "${X11_DEBS[@]}"
    fi
    # Audio backend headers: SDL
    # builds a backend only when its headers are found and dlopens the
    # library by soname at run time. ALSA is the one this port needs (it
    # also reaches PulseAudio and PipeWire through their ALSA plugins);
    # PulseAudio's headers come along and SDL enables that backend when the
    # build host has libpulse.so.0. Pinned Debian 13 packages; the SHA-256s
    # are the trixie main/binary-amd64 Packages index's (its InRelease
    # signature checked with the Debian archive keyring, 2026-10-05).
    if ! [[ -f /usr/include/alsa/asoundlib.h ]]; then
        AUDIO_DEBS_amd64=(
            "pool/main/a/alsa-lib/libasound2-dev_1.2.14-1+deb13u1_amd64.deb dfa8e8a133ef8704093d03464d86a3e86d26232baf1e3827aa030cb68c437e1c"
            "pool/main/p/pulseaudio/libpulse-dev_17.0+dfsg1-2+b1_amd64.deb bcbd91aae55d1794f8fe8b1e766ec810173a6415b2165642bf756858e20ab7c4"
        )
        AUDIO_DEBS_arm64=(
            "pool/main/a/alsa-lib/libasound2-dev_1.2.14-1+deb13u1_arm64.deb fd28b8f24feac0a6f1d3e3181ea097741d3f291f95c193bffa179a28dca3b612"
            "pool/main/p/pulseaudio/libpulse-dev_17.0+dfsg1-2+b1_arm64.deb 6c6f69b2e021e82fb51bf8000d1ac8799a2511e81f39a93d8f6b43fb90ec4d0f"
        )
        declare -n AUDIO_DEBS="AUDIO_DEBS_$DEB_ARCH"
        unpack_debs "$X11DEV" "${AUDIO_DEBS[@]}"
    fi
    mkdir -p "$X11DEV/usr/include" "$X11DEV/usr/lib/$DEB_MULTIARCH"
    gen=()
    if command -v ninja >/dev/null 2>&1 || [[ -x "$ROOT/.venv/bin/ninja" ]]; then
        gen=(-G Ninja)
        PATH="$ROOT/.venv/bin:$PATH"
    fi
    rm -rf "$SDL3_LINUX"
    "$CMAKE_BIN" -S "$TMP/SDL3-${SDL3_VERSION}" -B "$TMP/sdl3-build" "${gen[@]}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=gcc \
        -DCMAKE_C_FLAGS="-I$X11DEV/usr/include" \
        -DCMAKE_INCLUDE_PATH="$X11DEV/usr/include" \
        -DCMAKE_LIBRARY_PATH="$X11DEV/usr/lib/$DEB_MULTIARCH" \
        -DCMAKE_INSTALL_PREFIX="$SDL3_LINUX" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF \
        -DSDL_INSTALL_DOCS=OFF \
        -DSDL_X11_XSCRNSAVER=OFF -DSDL_X11_XTEST=OFF \
        "${SDL3_LINUX_VIDEO[@]}" \
        -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON | tee "$TMP/sdl3-configure.log"
    # the audio backends this build has (the configure summary)
    grep -E "SDL_(ALSA|PULSEAUDIO|PIPEWIRE|JACK|SNDIO|OSS)\b.*: *(ON|OFF)" "$TMP/sdl3-configure.log" || true
    if ! grep -qE "SDL_ALSA\b.*: *ON *$" "$TMP/sdl3-configure.log"; then
        echo "fetch_deps: SDL3 configured without ALSA: no audio on Linux" >&2
        exit 1
    fi
    # the video backends this build has; arm64 needs Wayland and KMSDRM
    grep -E "SDL_(X11|WAYLAND|KMSDRM)\b.*: *(ON|OFF)" "$TMP/sdl3-configure.log" || true
    if [[ "$ICO_HOST_ARCH" == "arm64" ]]; then
        for b in WAYLAND KMSDRM; do
            if ! grep -qE "SDL_${b}\b.*: *ON *$" "$TMP/sdl3-configure.log"; then
                echo "fetch_deps: SDL3 configured without ${b} on arm64: install the" \
                    "packages docs/BUILDING.md (\"Linux arm64\") lists" >&2
                exit 1
            fi
        done
    fi
    "$CMAKE_BIN" --build "$TMP/sdl3-build" --parallel
    "$CMAKE_BIN" --install "$TMP/sdl3-build"
    cp "$TMP/SDL3-${SDL3_VERSION}/LICENSE.txt" "$SDL3_LINUX/"
    # The Wayland backend compiles code generated from every protocol file in
    # SDL's wayland-protocols/ into the library, and most of those files ask
    # for their notice in every copy: each file's <copyright> block, in one
    # file the arm64 package's NOTICES.txt takes (tools/notices/manifest.json)
    if [[ "$ICO_HOST_ARCH" == "arm64" ]]; then
        wl_notices="$SDL3_LINUX/wayland-protocols/NOTICES.txt"
        mkdir -p "${wl_notices%/*}"
        : >"$wl_notices"
        wl_count=0
        while IFS= read -r xml; do
            {
                echo "${xml##*/}"
                echo
                if grep -q '<copyright>' "$xml"; then
                    sed -n '/<copyright>/,/<\/copyright>/p' "$xml" |
                        sed -e '/<\/\{0,1\}copyright>/d' -e 's/^    //'
                else
                    echo "(this file carries no copyright notice)"
                fi
                echo
            } >>"$wl_notices"
            wl_count=$((wl_count + 1))
        done < <(find "$TMP/SDL3-${SDL3_VERSION}/wayland-protocols" -name '*.xml' | LC_ALL=C sort)
        if [[ "$wl_count" -eq 0 ]]; then
            echo "fetch_deps: no Wayland protocol files in SDL3 ${SDL3_VERSION}" >&2
            exit 1
        fi
        echo "==> the notices of $wl_count Wayland protocol files at $wl_notices"
    fi
    echo "$SDL3_LINUX_ID" > "$SDL3_LINUX/.ico-release"
    echo "==> SDL3 ${SDL3_VERSION} (linux-$ICO_HOST_ARCH) at $SDL3_LINUX"
fi

# Windows: SDL's prebuilt mingw development release. It holds one prefix per
# target; the x86_64-w64-mingw32/ one has lib/cmake/SDL3, the
# import library and SDL3.dll; it is built against the UCRT-neutral mingw
# runtime and links with both mingw-gcc and llvm-mingw.
SDL3_MINGW="$DEST/sdl3/mingw"
SDL3_MINGW_ID="SDL3-devel-${SDL3_VERSION}-mingw"
if [[ "${SKIP_SDL3_MINGW:-0}" == "1" ]]; then
    echo "==> SKIP_SDL3_MINGW=1; not fetching $SDL3_MINGW"
elif stamped "$SDL3_MINGW" "$SDL3_MINGW_ID"; then
    echo "==> SDL3 ${SDL3_VERSION} (mingw) already at $SDL3_MINGW"
else
    fetch "${SDL3_BASE}/SDL3-devel-${SDL3_VERSION}-mingw.tar.gz" "$SDL3_MINGW_SHA256" \
        "$TMP/SDL3-mingw.tar.gz"
    tar -C "$TMP" -xzf "$TMP/SDL3-mingw.tar.gz"
    # Keep the 64-bit prefix and the licence; the tarball also carries the
    # 32-bit prefix and the full source tree, which the build does not use.
    rm -rf "$SDL3_MINGW"
    mkdir -p "$SDL3_MINGW"
    for d in x86_64-w64-mingw32 LICENSE.txt; do
        mv "$TMP/SDL3-${SDL3_VERSION}/$d" "$SDL3_MINGW/"
    done
    echo "$SDL3_MINGW_ID" > "$SDL3_MINGW/.ico-release"
    echo "==> SDL3 ${SDL3_VERSION} (mingw) at $SDL3_MINGW"
fi

# --- 3. Khronos validation layer (Linux host, tests only) ---------------------
#
# The RHI tests run with VK_LAYER_KHRONOS_validation when it is present
# (port/rhi/CMakeLists.txt points VK_ADD_LAYER_PATH here). Debian 13's build
# (Apache-2.0), unpacked like the packages above; its manifest names the
# library by bare file name, so it is rewritten to the absolute path. Never
# shipped.
VVL="$DEST/vulkan-validation"
# the stamp names the architecture on arm64, so a tree another host
# architecture fetched is replaced rather than kept
VVL_ID="vulkan-validationlayers-1.4.309.0-1$(by_arch "" "_arm64")"
VVL_DEB="pool/main/v/vulkan-validationlayers/vulkan-validationlayers_1.4.309.0-1_${DEB_ARCH}.deb"
VVL_SHA256="$(by_arch \
    ff7e18ef011ff448ac799daf16134a676ccf9c5987e1a9f4b779611ed5fc3b0d \
    b81a8ada938d71a3344c52f77d94d031a2d7722fcbd066cca067c19124f312ee)"
if [[ "${SKIP_VALIDATION_LAYER:-0}" == "1" ]]; then
    echo "==> SKIP_VALIDATION_LAYER=1; not fetching the validation layer"
elif stamped "$VVL" "$VVL_ID"; then
    echo "==> validation layer already at $VVL"
elif ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "==> dpkg-deb not found; skipping the validation layer"
else
    mkdir -p "$TMP/vvl"
    fetch_deb "$VVL_DEB $VVL_SHA256" "$TMP/vvl"
    rm -rf "$VVL"
    mkdir -p "$VVL"
    mv "$TMP/vvl/usr/lib/$DEB_MULTIARCH/libVkLayer_khronos_validation.so" "$VVL/"
    sed "s|\"library_path\": *\"[^\"]*\"|\"library_path\": \"$VVL/libVkLayer_khronos_validation.so\"|" \
        "$TMP/vvl/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json" \
        > "$VVL/VkLayer_khronos_validation.json"
    echo "$VVL_ID" > "$VVL/.ico-release"
    echo "==> validation layer at $VVL"
fi

# --- 4. DXC (build tool) ---------------------------------------------------------
#
# The shader compiler cmake/IcoShaders.cmake runs on the build host: HLSL to
# SPIR-V and DXIL. Installed at deps/dxc/{bin,lib}.
#
# x86-64: Microsoft's Linux x86-64 release; the SHA-256 is the digest GitHub
# lists for the asset.
#
# arm64: Microsoft publishes no aarch64 Linux build, so the same release is
# built from source: the tag's commit, checked like libmpeg2 below (the
# commit id, then the SHA-256 of `git archive --format=tar` of it, taken
# 2026-10-09), with the three submodules the compiler needs at the commits
# that tree records, each checked the same way (googletest is left out with
# the tests). The dxc target
# alone, Release, with the host's g++ and Ninja; a few minutes on 8 cores.
# bin/dxc loads lib/libdxcompiler.so through its $ORIGIN/../lib run path, as
# in the x86-64 release. No libdxil.so: the arm64 presets compile no DXIL
# (ICO_SHADERS_DXIL=OFF).
DXC_VERSION="${DXC_VERSION:-v1.9.2609}"
DXC_LINUX_FILE="${DXC_LINUX_FILE:-linux_dxc_2026_09_28.x86_x64.tar.gz}"
DXC_LINUX_SHA256="${DXC_LINUX_SHA256:-96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1}"
DXC_COMMIT="${DXC_COMMIT:-01b62ad47db7dee3dd33f90e7339cf9e860b3f93}"
DXC_TAR_SHA256="${DXC_TAR_SHA256:-0b5eea39a24c8f9af67e750938dada85a504a2dff495f3d7a613d17fb3e35225}"
DXC_DIR="$DEST/dxc"
DXC_ID="$(by_arch "$DXC_VERSION-$DXC_LINUX_FILE" "$DXC_VERSION-$DXC_COMMIT-src")"
if [[ "${SKIP_DXC:-0}" == "1" ]]; then
    echo "==> SKIP_DXC=1; not fetching DXC"
elif stamped "$DXC_DIR" "$DXC_ID"; then
    echo "==> DXC ${DXC_VERSION} already at $DXC_DIR"
elif [[ "$ICO_HOST_ARCH" == "x64" ]]; then
    fetch "https://github.com/microsoft/DirectXShaderCompiler/releases/download/${DXC_VERSION}/${DXC_LINUX_FILE}" \
        "$DXC_LINUX_SHA256" "$TMP/dxc.tar.gz"
    mkdir -p "$TMP/dxc"
    tar -C "$TMP/dxc" -xzf "$TMP/dxc.tar.gz"
    rm -rf "$DXC_DIR"
    mkdir -p "$DXC_DIR"
    mv "$TMP/dxc/bin" "$TMP/dxc/lib" "$DXC_DIR/"
    for f in LICENSE-LLVM.txt LICENSE-MIT.txt LICENSE-MS.txt ThirdPartyNotices.txt; do
        [[ -f "$TMP/dxc/$f" ]] && cp "$TMP/dxc/$f" "$DXC_DIR/"
    done
    chmod +x "$DXC_DIR/bin/dxc"
    echo "$DXC_ID" > "$DXC_DIR/.ico-release"
    echo "==> DXC ${DXC_VERSION} at $DXC_DIR"
else
    for t in git g++; do
        command -v "$t" >/dev/null 2>&1 || {
            echo "fetch_deps: $t not found; it builds DXC on this host" >&2
            exit 1
        }
    done
    echo "==> fetching DXC ${DXC_VERSION} source"
    src="$TMP/dxc-src"
    git init -q "$src"
    git -C "$src" fetch -q --depth 1 \
        https://github.com/microsoft/DirectXShaderCompiler "refs/tags/${DXC_VERSION}"
    got="$(git -C "$src" rev-parse 'FETCH_HEAD^{commit}')"
    if [[ "$got" != "$DXC_COMMIT" ]]; then
        echo "fetch_deps: DXC ${DXC_VERSION} is $got, expected $DXC_COMMIT" >&2
        exit 1
    fi
    git -C "$src" archive --format=tar -o "$TMP/dxc.tar" "$DXC_COMMIT"
    echo "${DXC_TAR_SHA256}  $TMP/dxc.tar" | sha256sum -c -
    git -C "$src" -c advice.detachedHead=false checkout -q "$DXC_COMMIT"
    git -C "$src" submodule update -q --init --depth 1 \
        external/SPIRV-Headers external/SPIRV-Tools external/DirectX-Headers
    # the submodules the tree records, each checked like the tree itself: the
    # commit, then the SHA-256 of `git archive --format=tar` of it (taken
    # 2026-10-09)
    for entry in \
        "external/SPIRV-Headers 496543121ce6419f23d6fa5d7194ba66c36212d2 c44aa584944201e4bb4ea5a301cd735c41b7fedeb717e8625b97cd9d7315a4b8" \
        "external/SPIRV-Tools ef96ed763b43b59b33b31b362f09a02b729fa1c9 a3e5d1f5dbefc057022d1c9c29315e887cb4c491dffb3ee850cbfaa3725ac9c1" \
        "external/DirectX-Headers 980971e835876dc0cde415e8f9bc646e64667bf7 f7f5d15365443cbd8137445c3aedf8ccd31c3402f72c0fa7c16e7bf1c7977139"; do
        read -r sub_path sub_commit sub_sum <<<"$entry"
        got="$(git -C "$src/$sub_path" rev-parse HEAD)"
        if [[ "$got" != "$sub_commit" ]]; then
            echo "fetch_deps: DXC $sub_path is $got, expected $sub_commit" >&2
            exit 1
        fi
        git -C "$src/$sub_path" archive --format=tar -o "$TMP/dxc-sub.tar" "$sub_commit"
        echo "${sub_sum}  $TMP/dxc-sub.tar" | sha256sum -c -
        rm -f "$TMP/dxc-sub.tar"
    done
    gen=()
    if command -v ninja >/dev/null 2>&1 || [[ -x "$ROOT/.venv/bin/ninja" ]]; then
        gen=(-G Ninja)
        PATH="$ROOT/.venv/bin:$PATH"
    fi
    # LLVM's link steps take gigabytes each: one at a time. The generated
    # sources are the tree's own: DXC would regenerate them and compare the
    # result after a pass of whatever clang-format it finds (the venv's
    # 23.1.2 formats them differently and fails the build).
    "$CMAKE_BIN" -S "$src" -B "$TMP/dxc-build" "${gen[@]}" \
        -C "$src/cmake/caches/PredefinedParams.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DLLVM_PARALLEL_LINK_JOBS=1 \
        -DHLSL_DISABLE_SOURCE_GENERATION=ON \
        -DHLSL_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF \
        -DCLANG_INCLUDE_TESTS=OFF -DSPIRV_BUILD_TESTS=OFF >"$TMP/dxc-configure.log" 2>&1 || {
        tail -n 40 "$TMP/dxc-configure.log" >&2
        echo "fetch_deps: DXC configure failed" >&2
        exit 1
    }
    "$CMAKE_BIN" --build "$TMP/dxc-build" --parallel --target dxc
    rm -rf "$DXC_DIR"
    mkdir -p "$DXC_DIR/bin" "$DXC_DIR/lib"
    cp -L "$TMP/dxc-build/bin/dxc" "$DXC_DIR/bin/dxc"
    cp -L "$TMP/dxc-build/lib/libdxcompiler.so" "$DXC_DIR/lib/libdxcompiler.so"
    cp "$src/LICENSE.TXT" "$DXC_DIR/LICENSE-LLVM.txt"
    cp "$src/ThirdPartyNotices.txt" "$DXC_DIR/"
    "$DXC_DIR/bin/dxc" --version
    echo "$DXC_ID" > "$DXC_DIR/.ico-release"
    echo "==> DXC ${DXC_VERSION} (built from source) at $DXC_DIR"
fi
# --- 5. Ittiam libmpeg2 (the FMV decoder) ---------------------------------------
#
# AOSP platform/external/libmpeg2 (Apache-2.0), tag android-16.0.0_r4 =
# commit a97c2a1f0a796dc32bed80d3353c69c5fc07c750. googlesource's +archive
# tarballs are regenerated per request (two downloads on 2026-10-05 had
# different SHA-256s) and the GitHub mirror (ittiam-systems/libmpeg2) does
# not carry the commit, so the tag is fetched with git and checked twice:
# the commit id, and the SHA-256 of `git archive --format=tar` of it (the
# tree with fixed mtimes; taken 2026-10-05). The source tree is installed;
# port/fmv/CMakeLists.txt compiles it as a static library with each
# preset's own compiler (linux-x64, win-x64 mingw, asan): generic C, and
# the library's NEON assembly on arm64.
# docs/THIRD_PARTY.md has the versions.
LIBMPEG2_TAG="${LIBMPEG2_TAG:-android-16.0.0_r4}"
LIBMPEG2_COMMIT="${LIBMPEG2_COMMIT:-a97c2a1f0a796dc32bed80d3353c69c5fc07c750}"
LIBMPEG2_TAR_SHA256="${LIBMPEG2_TAR_SHA256:-3a8a3028cc5e0c8177d9c81e687318c3eb81c2a9fd3219d50c94eabf1a8f3ea1}"
LIBMPEG2_DIR="$DEST/libmpeg2"
if [[ "${SKIP_LIBMPEG2:-0}" == "1" ]]; then
    echo "==> SKIP_LIBMPEG2=1; not fetching libmpeg2"
elif stamped "$LIBMPEG2_DIR" "$LIBMPEG2_COMMIT"; then
    echo "==> libmpeg2 ${LIBMPEG2_TAG} already at $LIBMPEG2_DIR"
else
    command -v git >/dev/null 2>&1 || {
        echo "fetch_deps: git not found; cannot fetch libmpeg2" >&2
        exit 1
    }
    echo "==> fetching libmpeg2 ${LIBMPEG2_TAG}"
    git init -q "$TMP/libmpeg2.git"
    git -C "$TMP/libmpeg2.git" fetch -q --depth 1 \
        https://android.googlesource.com/platform/external/libmpeg2 "refs/tags/${LIBMPEG2_TAG}"
    got="$(git -C "$TMP/libmpeg2.git" rev-parse 'FETCH_HEAD^{commit}')"
    if [[ "$got" != "$LIBMPEG2_COMMIT" ]]; then
        echo "fetch_deps: libmpeg2 ${LIBMPEG2_TAG} is $got, expected $LIBMPEG2_COMMIT" >&2
        exit 1
    fi
    git -C "$TMP/libmpeg2.git" archive --format=tar -o "$TMP/libmpeg2.tar" "$LIBMPEG2_COMMIT"
    echo "${LIBMPEG2_TAR_SHA256}  $TMP/libmpeg2.tar" | sha256sum -c -
    rm -rf "$LIBMPEG2_DIR"
    mkdir -p "$LIBMPEG2_DIR"
    # the decoder's C sources and headers plus the licence files; not the
    # fuzzer, the test program or the build files
    tar -C "$LIBMPEG2_DIR" -xf "$TMP/libmpeg2.tar" common decoder LICENSE NOTICE \
        MODULE_LICENSE_APACHE2 METADATA
    echo "$LIBMPEG2_COMMIT" > "$LIBMPEG2_DIR/.ico-release"
    echo "==> libmpeg2 ${LIBMPEG2_TAG} at $LIBMPEG2_DIR"
fi
# --- 6. libchdr (the .chd disc image reader, issue 2) ---------------------------
#
# rtissera/libchdr (BSD-3-Clause), tag v0.3.0 (2026-04-24) = commit
# 93d8c239ff0d4e8d7722985992649fce12d2463b. Fetched and checked like
# libmpeg2: the commit id, then the SHA-256 of `git archive --format=tar` of
# it (taken 2026-10-07). The library bundles the decoders its codecs need
# (deps/lzma-25.01, public domain; deps/miniz-3.1.1, MIT; deps/zstd-1.5.7,
# BSD-3-Clause; include/dr_libs/dr_flac.h, public domain or MIT-0), so no
# system zlib or zstd is linked. The zstd copy carries no licence file, so
# zstd's LICENSE is taken from the same release's tag (facebook/zstd v1.5.7)
# and checked. port/data/CMakeLists.txt compiles the tree as a static library
# with each preset's own compiler. docs/THIRD_PARTY.md has the versions.
LIBCHDR_TAG="${LIBCHDR_TAG:-v0.3.0}"
LIBCHDR_COMMIT="${LIBCHDR_COMMIT:-93d8c239ff0d4e8d7722985992649fce12d2463b}"
LIBCHDR_TAR_SHA256="${LIBCHDR_TAR_SHA256:-591863ddda6c4a90192e66ac682d9f535ac9af79f2385e7c180af0b3b46e1394}"
ZSTD_LICENSE_SHA256="${ZSTD_LICENSE_SHA256:-7055266497633c9025b777c78eb7235af13922117480ed5c674677adc381c9d8}"
LIBCHDR_DIR="$DEST/libchdr"
if [[ "${SKIP_LIBCHDR:-0}" == "1" ]]; then
    echo "==> SKIP_LIBCHDR=1; not fetching libchdr"
elif stamped "$LIBCHDR_DIR" "$LIBCHDR_COMMIT"; then
    echo "==> libchdr ${LIBCHDR_TAG} already at $LIBCHDR_DIR"
else
    command -v git >/dev/null 2>&1 || {
        echo "fetch_deps: git not found; cannot fetch libchdr" >&2
        exit 1
    }
    echo "==> fetching libchdr ${LIBCHDR_TAG}"
    git init -q "$TMP/libchdr.git"
    git -C "$TMP/libchdr.git" fetch -q --depth 1 \
        https://github.com/rtissera/libchdr "refs/tags/${LIBCHDR_TAG}"
    got="$(git -C "$TMP/libchdr.git" rev-parse 'FETCH_HEAD^{commit}')"
    if [[ "$got" != "$LIBCHDR_COMMIT" ]]; then
        echo "fetch_deps: libchdr ${LIBCHDR_TAG} is $got, expected $LIBCHDR_COMMIT" >&2
        exit 1
    fi
    git -C "$TMP/libchdr.git" archive --format=tar -o "$TMP/libchdr.tar" "$LIBCHDR_COMMIT"
    echo "${LIBCHDR_TAR_SHA256}  $TMP/libchdr.tar" | sha256sum -c -
    fetch "https://raw.githubusercontent.com/facebook/zstd/v1.5.7/LICENSE" \
        "$ZSTD_LICENSE_SHA256" "$TMP/zstd-LICENSE"
    rm -rf "$LIBCHDR_DIR"
    mkdir -p "$LIBCHDR_DIR"
    # the library's sources, headers and bundled decoders plus its licence
    # and change log; not the CI files, the tests or the fuzzer
    tar -C "$LIBCHDR_DIR" -xf "$TMP/libchdr.tar" src include deps LICENSE.txt README.md \
        CHANGELOG.md
    cp "$TMP/zstd-LICENSE" "$LIBCHDR_DIR/deps/zstd-1.5.7/LICENSE"
    echo "$LIBCHDR_COMMIT" > "$LIBCHDR_DIR/.ico-release"
    echo "==> libchdr ${LIBCHDR_TAG} at $LIBCHDR_DIR"
fi
echo "==> deps done: $DEST"
