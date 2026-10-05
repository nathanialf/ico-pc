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
#   deps/sdl3/mingw/       SDL's own mingw development release: the
#                          x86_64-w64-mingw32 and i686-w64-mingw32 prefixes
#   deps/vulkan-validation/ the Khronos validation layer (Apache-2.0) from
#                          Debian 13, for the RHI tests only; never shipped
#   deps/dxc/              DirectX Shader Compiler (Linux x86-64 release, NCSA):
#                          the shader toolchain's build tool (cmake/IcoShaders.cmake);
#                          never linked or shipped
#
# docs/port/THIRD_PARTY.md records the versions and licences.
#
# Overrides:
#   VULKAN_SDK_TAG, VULKAN_HEADERS_SHA256, VOLK_SHA256
#   SDL3_VERSION, SDL3_SRC_SHA256, SDL3_MINGW_SHA256
#   DXC_VERSION, DXC_LINUX_FILE, DXC_LINUX_SHA256
#   SKIP_SDL3_LINUX=1, SKIP_SDL3_MINGW=1, SKIP_VALIDATION_LAYER=1, SKIP_DXC=1
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

# fetch <url> <sha256> <file>: download and verify.
fetch() {
    echo "==> fetching $1"
    curl -fL --retry 3 -o "$3" "$1"
    echo "${2}  $3" | sha256sum -c -
}

# stamped <dir> <id>: true when <dir> already holds release <id>.
stamped() {
    [[ -f "$1/.ico-release" && "$(cat "$1/.ico-release")" == "$2" ]]
}

# unpack_debs <dest> "<pool path> <sha256>"...: fetch pinned Debian packages
# (deb.debian.org, else DEB_SNAPSHOT), check them and unpack them into
# <dest>. Their lib*.so development links, relative to their own lib dir,
# are pointed at the host's runtime libraries (dangling when the host has
# none, so CMake does not find them).
unpack_debs() {
    local dest="$1" entry path sum deb so tgt
    shift
    command -v dpkg-deb >/dev/null 2>&1 || {
        echo "fetch_deps: dpkg-deb not found; cannot unpack the Debian header packages" >&2
        exit 1
    }
    DEB_SNAPSHOT="${DEB_SNAPSHOT:-https://snapshot.debian.org/archive/debian/20261004T000000Z}"
    for entry in "$@"; do
        path="${entry% *}"
        sum="${entry#* }"
        deb="$TMP/$(basename "$path")"
        echo "==> fetching $(basename "$path")"
        if ! curl -fsL --retry 3 -o "$deb" "http://deb.debian.org/debian/${path//+/%2B}"; then
            curl -fsL --retry 3 -o "$deb" "${DEB_SNAPSHOT}/${path//+/%2B}"
        fi
        echo "${sum}  ${deb}" | sha256sum -c -
        dpkg-deb -x "$deb" "$dest"
    done
    for so in "$dest"/usr/lib/x86_64-linux-gnu/lib*.so; do
        [[ -L "$so" ]] || continue
        tgt="$(readlink "$so")"
        tgt="${tgt##*/}"
        rm -f "$so"
        if [[ -e "/usr/lib/x86_64-linux-gnu/$tgt" ]]; then
            ln -s "/usr/lib/x86_64-linux-gnu/$tgt" "$so"
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
# Release 3.4.18. The SHA-256s are the digests GitHub lists for the release
# assets (gh api repos/libsdl-org/SDL/releases/latest).
SDL3_VERSION="${SDL3_VERSION:-3.4.18}"
SDL3_SRC_SHA256="${SDL3_SRC_SHA256:-9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3}"
SDL3_MINGW_SHA256="${SDL3_MINGW_SHA256:-049b9dd711e3fb3f8d84feb998dfec54a75e17cf539c4a80ff4d058d839771cf}"
SDL3_BASE="https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}"

# Linux x86-64: built from source. The host has no SDL3 package and no root;
# the X11 backend builds against the host's libX11/libxcb headers, the
# others are probed by SDL's CMake and left out when their headers are
# missing. SDL loads its video and audio backends with dlopen, so the
# library runs on hosts with more or fewer of them.
SDL3_LINUX="$DEST/sdl3/linux-x64"
# "+audio": the build with the ALSA/PulseAudio headers (Phase 4B); a tree
# stamped by an older run of this script (X11 only, no audio backend) is
# rebuilt.
SDL3_LINUX_ID="SDL3-${SDL3_VERSION}+audio"
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
    echo "==> SDL3 ${SDL3_VERSION} (linux-x64) already at $SDL3_LINUX"
else
    fetch "${SDL3_BASE}/SDL3-${SDL3_VERSION}.tar.gz" "$SDL3_SRC_SHA256" "$TMP/SDL3.tar.gz"
    tar -C "$TMP" -xzf "$TMP/SDL3.tar.gz"
    # X11 extension headers SDL's X11 backend needs and a stock host may not
    # have (the container has libx11-dev but not libxext-dev). Unpacked from
    # pinned Debian 13 packages into a scratch tree for this build only; the
    # libraries themselves are the host's (SDL dlopens them by soname).
    X11DEV="$TMP/x11dev"
    mkdir -p "$X11DEV"
    if ! [[ -f /usr/include/X11/extensions/Xext.h ]]; then
        X11_DEBS=(
            "pool/main/libx/libxext/libxext-dev_1.3.4-1+b3_amd64.deb e6bd898976d762a6955ec465870e7f12a3b149f4c5b231b0687e293ed5726532"
            "pool/main/libx/libxrandr/libxrandr-dev_1.5.4-1+b3_amd64.deb e408e4f8c77135725e9055d445635587225839a929d348c04b85adfa84bdefb1"
            "pool/main/libx/libxi/libxi-dev_1.8.2-1_amd64.deb be3ca6202327858845aa0a8d70374b08dae5c89250ad8997256e02b2833501d3"
            "pool/main/libx/libxcursor/libxcursor-dev_1.2.3-1_amd64.deb 57f2fc575496edc9f3dd286423494499d231a8b1cdb417bb8401f6b66b584a26"
            "pool/main/libx/libxfixes/libxfixes-dev_6.0.0-2+b4_amd64.deb 1070e0721765992c5355a9585f6eae9e0d11437a7d2b4a2c28123e6e5b0c0b2f"
            "pool/main/libx/libxrender/libxrender-dev_0.9.12-1_amd64.deb 55121741c44e03cbeb03dabb5721ded1341c987f055d7cae991de7431c9edfa6"
        )
        unpack_debs "$X11DEV" "${X11_DEBS[@]}"
    fi
    # Audio backend headers (Phase 4B, docs/port/AUDIO.md "Output"): SDL
    # builds a backend only when its headers are found and dlopens the
    # library by soname at run time. ALSA is the one this port needs (it
    # also reaches PulseAudio and PipeWire through their ALSA plugins);
    # PulseAudio's headers come along and SDL enables that backend when the
    # build host has libpulse.so.0. Pinned Debian 13 packages; the SHA-256s
    # are the trixie main/binary-amd64 Packages index's (its InRelease
    # signature checked with the Debian archive keyring, 2026-10-05).
    if ! [[ -f /usr/include/alsa/asoundlib.h ]]; then
        AUDIO_DEBS=(
            "pool/main/a/alsa-lib/libasound2-dev_1.2.14-1+deb13u1_amd64.deb dfa8e8a133ef8704093d03464d86a3e86d26232baf1e3827aa030cb68c437e1c"
            "pool/main/p/pulseaudio/libpulse-dev_17.0+dfsg1-2+b1_amd64.deb bcbd91aae55d1794f8fe8b1e766ec810173a6415b2165642bf756858e20ab7c4"
        )
        unpack_debs "$X11DEV" "${AUDIO_DEBS[@]}"
    fi
    mkdir -p "$X11DEV/usr/include" "$X11DEV/usr/lib/x86_64-linux-gnu"
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
        -DCMAKE_LIBRARY_PATH="$X11DEV/usr/lib/x86_64-linux-gnu" \
        -DCMAKE_INSTALL_PREFIX="$SDL3_LINUX" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF \
        -DSDL_INSTALL_DOCS=OFF \
        -DSDL_X11_XSCRNSAVER=OFF -DSDL_X11_XTEST=OFF \
        -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON | tee "$TMP/sdl3-configure.log"
    # the audio backends this build has (the configure summary)
    grep -E "SDL_(ALSA|PULSEAUDIO|PIPEWIRE|JACK|SNDIO|OSS)\b.*: *(ON|OFF)" "$TMP/sdl3-configure.log" || true
    if ! grep -qE "SDL_ALSA\b.*: *ON *$" "$TMP/sdl3-configure.log"; then
        echo "fetch_deps: SDL3 configured without ALSA: no audio on Linux" >&2
        exit 1
    fi
    "$CMAKE_BIN" --build "$TMP/sdl3-build" --parallel
    "$CMAKE_BIN" --install "$TMP/sdl3-build"
    cp "$TMP/SDL3-${SDL3_VERSION}/LICENSE.txt" "$SDL3_LINUX/"
    echo "$SDL3_LINUX_ID" > "$SDL3_LINUX/.ico-release"
    echo "==> SDL3 ${SDL3_VERSION} (linux-x64) at $SDL3_LINUX"
fi

# Windows: SDL's prebuilt mingw development release. It holds one prefix per
# target (x86_64-w64-mingw32/, i686-w64-mingw32/) with lib/cmake/SDL3, the
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
    # Keep the two target prefixes and the licence; the tarball also carries
    # the full source tree, which the build does not use.
    rm -rf "$SDL3_MINGW"
    mkdir -p "$SDL3_MINGW"
    for d in x86_64-w64-mingw32 i686-w64-mingw32 LICENSE.txt; do
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
VVL_ID="vulkan-validationlayers-1.4.309.0-1"
VVL_DEB="pool/main/v/vulkan-validationlayers/vulkan-validationlayers_1.4.309.0-1_amd64.deb"
VVL_SHA256="ff7e18ef011ff448ac799daf16134a676ccf9c5987e1a9f4b779611ed5fc3b0d"
if [[ "${SKIP_VALIDATION_LAYER:-0}" == "1" ]]; then
    echo "==> SKIP_VALIDATION_LAYER=1; not fetching the validation layer"
elif stamped "$VVL" "$VVL_ID"; then
    echo "==> validation layer already at $VVL"
elif ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "==> dpkg-deb not found; skipping the validation layer"
else
    DEB_SNAPSHOT="${DEB_SNAPSHOT:-https://snapshot.debian.org/archive/debian/20261004T000000Z}"
    deb="$TMP/$(basename "$VVL_DEB")"
    echo "==> fetching $(basename "$VVL_DEB")"
    if ! curl -fsL --retry 3 -o "$deb" "http://deb.debian.org/debian/${VVL_DEB}"; then
        curl -fsL --retry 3 -o "$deb" "${DEB_SNAPSHOT}/${VVL_DEB}"
    fi
    echo "${VVL_SHA256}  ${deb}" | sha256sum -c -
    rm -rf "$VVL"
    mkdir -p "$TMP/vvl" "$VVL"
    dpkg-deb -x "$deb" "$TMP/vvl"
    mv "$TMP/vvl/usr/lib/x86_64-linux-gnu/libVkLayer_khronos_validation.so" "$VVL/"
    sed "s|\"library_path\": *\"[^\"]*\"|\"library_path\": \"$VVL/libVkLayer_khronos_validation.so\"|" \
        "$TMP/vvl/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json" \
        > "$VVL/VkLayer_khronos_validation.json"
    echo "$VVL_ID" > "$VVL/.ico-release"
    echo "==> validation layer at $VVL"
fi

# --- 4. DXC (build tool) ---------------------------------------------------------
#
# The shader compiler cmake/IcoShaders.cmake runs on the build host: HLSL to
# SPIR-V and DXIL. Microsoft's Linux x86-64 release; the SHA-256 is the digest
# GitHub lists for the asset. Installed at deps/dxc/{bin,lib}.
DXC_VERSION="${DXC_VERSION:-v1.9.2609}"
DXC_LINUX_FILE="${DXC_LINUX_FILE:-linux_dxc_2026_09_28.x86_x64.tar.gz}"
DXC_LINUX_SHA256="${DXC_LINUX_SHA256:-96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1}"
DXC_DIR="$DEST/dxc"
if [[ "${SKIP_DXC:-0}" == "1" ]]; then
    echo "==> SKIP_DXC=1; not fetching DXC"
elif stamped "$DXC_DIR" "$DXC_VERSION-$DXC_LINUX_FILE"; then
    echo "==> DXC ${DXC_VERSION} already at $DXC_DIR"
else
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
    echo "$DXC_VERSION-$DXC_LINUX_FILE" > "$DXC_DIR/.ico-release"
    echo "==> DXC ${DXC_VERSION} at $DXC_DIR"
fi
echo "==> deps done: $DEST"
