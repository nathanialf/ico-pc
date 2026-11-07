#!/usr/bin/env bash
# =============================================================================
# tools/fetch_toolchain.sh
#
# Fetches the host toolchain for the PC port's CMake build (CMakePresets.json)
# into tools/toolchain/ (gitignored). Idempotent: a second run finds the
# pinned release and does nothing.
#
# llvm-mingw (https://github.com/mstorsjo/llvm-mingw, Apache-2.0 WITH
# LLVM-exception for LLVM, mingw-w64 runtime under its own permissive
# licences) provides clang, lld and the x86_64 mingw-w64 target
# with the UCRT runtime. The same clang compiles the Linux presets against the
# host's gcc sysroot (cmake/toolchains/x86_64-linux-clang.cmake).
#
# It also builds tools/toolchain/mingw-gcc from pinned Debian packages
# (dpkg-deb -x, no root): Debian's x86_64 mingw-w64 gcc for the GCC-family
# Windows presets (section 3 says why). The i386 sysroot and the i686
# mingw-gcc of the 32-bit presets went with them (36a1d73e).
#
# Section 4 unpacks Kitware's CMake release (BSD-3-Clause) into
# tools/toolchain/cmake, the CMake the presets' documented invocation uses
# (tools/toolchain/cmake/bin/cmake); the system's CMake 3.25 or later works
# too (CMakeLists.txt, cmake_minimum_required).
#
# Section 5 installs ccache (GPL-3.0-or-later; a build tool, nothing of it is
# shipped) at tools/toolchain/ccache/bin/ccache, the compiler launcher every
# CMakePresets.json preset names. Without it the build runs uncached.
#
# The host is x86-64 or arm64 (aarch64) Linux (tools/fetch_common.sh). An
# arm64 host takes Kitware's aarch64 build of the same CMake release. It
# skips section 1 unless SKIP_LLVM_MINGW=0 (the presets that use llvm-mingw
# build for x86-64; the pin of its aarch64 build is kept for that override)
# and section 3 unless SKIP_MINGW_GCC=0 (its Debian packages are amd64
# programs). tools/fetch_deps.sh picks its own per-host pieces.
#
# Overrides:
#   LLVM_MINGW_TAG, LLVM_MINGW_SHA256   pin another release
#   LLVM_MINGW_URL                      fetch from elsewhere
#   DEB_SNAPSHOT                        snapshot.debian.org fallback base
#   CMAKE_VERSION, CMAKE_SHA256         pin another CMake release
#   SKIP_LLVM_MINGW=1                   skip section 1 (the default on arm64)
#   SKIP_MINGW_GCC=1                    skip section 3 (the default on arm64)
#   SKIP_CMAKE=1                        skip section 4
#   CCACHE_VERSION, CCACHE_SHA256       pin another ccache release
#   SKIP_CCACHE=1                       skip section 5 (the default on arm64)
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/tools/toolchain"

# fetch, stamped, fetch_deb; the host (ICO_HOST_ARCH, by_arch)
# shellcheck source=tools/fetch_common.sh
source "$ROOT/tools/fetch_common.sh"

# the host builds of one llvm-mingw release; the SHA-256s are the digests
# GitHub lists for the assets
TAG="${LLVM_MINGW_TAG:-20260922}"
NAME="llvm-mingw-${TAG}-ucrt-ubuntu-22.04-$(by_arch x86_64 aarch64)"
SHA256="${LLVM_MINGW_SHA256:-$(by_arch \
    bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21 \
    07d21263c56bfe9a713db6fdb3f7434bf4c121a005e40397d3b4c0170fb06769)}"
URL="${LLVM_MINGW_URL:-https://github.com/mstorsjo/llvm-mingw/releases/download/${TAG}/${NAME}.tar.xz}"

mkdir -p "$DEST"
TMP="$(mktemp -d "$DEST/.fetch.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# --- 1. llvm-mingw -----------------------------------------------------------

if [[ "${SKIP_LLVM_MINGW:-$(by_arch 0 1)}" == "1" ]]; then
    if [[ -z "${SKIP_LLVM_MINGW:-}" ]]; then
        echo "==> llvm-mingw: its presets build for x86-64, not fetched on this host" \
            "(SKIP_LLVM_MINGW=0 fetches it)"
    else
        echo "==> SKIP_LLVM_MINGW=1; not fetching llvm-mingw"
    fi
elif stamped "$DEST/llvm-mingw" "$NAME"; then
    echo "==> llvm-mingw ${TAG} already at $DEST/llvm-mingw"
else
    fetch "$URL" "$SHA256" "$TMP/$NAME.tar.xz"
    echo "==> unpacking"
    tar -C "$TMP" -xJf "$TMP/$NAME.tar.xz"
    rm -rf "$DEST/llvm-mingw"
    mv "$TMP/$NAME" "$DEST/llvm-mingw"
    echo "$NAME" > "$DEST/llvm-mingw/.ico-release"
    "$DEST/llvm-mingw/bin/clang" --version | head -n 1
fi

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "fetch_toolchain: dpkg-deb not found; it unpacks the Debian packages below" >&2
    exit 1
fi

# --- 2. Debian packages -------------------------------------------------------
#
# fetch_deb (tools/fetch_common.sh) downloads, verifies and unpacks them.

# --- 3. mingw-w64 gcc, the GCC-family Windows presets ------------------------
#
# The gcc Windows preset (win-x64) compiles with GCC, the primary compiler
# (docs/BUILDING.md, "Compilers"). Debian's mingw-w64
# cross gcc 14 and binutils for the x86_64 target, unpacked with fetch_deb;
# gcc finds its own pieces relative to its executable, so the tree works
# from any directory. The g++ package (cc1plus, libstdc++.a and its headers,
# the same Debian revision as the gcc beside it) is for the FidelityFX FSR 3.1
# upscaler, the one C++ part of the program (port/rhi/ffx); the Windows
# program links the C++ runtime statically, so no libstdc++-6.dll ships. The
# trailing "g++" of the stamp makes a tree unpacked without it fetch again
# (CI's cache key hashes this file, so its cache rebuilds too).
MINGW_GCC="$DEST/mingw-gcc"
MINGW_GCC_ID="gcc-mingw-w64-14.2.0-19+27+b1/mingw-w64-12.0.0-5/binutils-2.44-3+12+b1/x86_64/g++"
MINGW_DEBS=(
    "pool/main/g/gcc-mingw-w64/gcc-mingw-w64-base_14.2.0-19+27+b1_amd64.deb 8d2c64b886ab4a435a78ddc4cd3b510f149b65fbd6be8472e8f460627562718a"
    "pool/main/g/gcc-mingw-w64/gcc-mingw-w64-x86-64-win32_14.2.0-19+27+b1_amd64.deb a4d7abc5c9c97380eeac348d3b94d522a04e80028bb825cd44df09d9ba90092f"
    "pool/main/g/gcc-mingw-w64/gcc-mingw-w64-x86-64-win32-runtime_14.2.0-19+27+b1_amd64.deb 73a27ee935bec4c81331c3e92c2399e11f161e75fe27898524dcf2bae77f97cb"
    "pool/main/g/gcc-mingw-w64/g++-mingw-w64-x86-64-win32_14.2.0-19+27+b1_amd64.deb 9d84ad232f7e63ac297287b273653b443a3cd7a0613c43a30b3de9f9e20b6425"
    "pool/main/b/binutils-mingw-w64/binutils-mingw-w64-x86-64_2.44-3+12+b1_amd64.deb b53f4a091b16a0d0952b8e3bd6027931641386258f96b37db1f0c62c787221d3"
    "pool/main/m/mingw-w64/mingw-w64-common_12.0.0-5_all.deb 97dce5d0d8aff1cade4786ad4ef5078345477a598829afcac2e7f2fbc5399990"
    "pool/main/m/mingw-w64/mingw-w64-x86-64-dev_12.0.0-5_all.deb 0bf89cf7454cccb49cd2a46a6f7b33896f6ae3a1a2fe33e02c40be6155bbb385"
)

if [[ "${SKIP_MINGW_GCC:-$(by_arch 0 1)}" == "1" ]]; then
    if [[ -z "${SKIP_MINGW_GCC:-}" ]]; then
        echo "==> mingw-gcc: amd64 programs, not fetched on this host" \
            "(SKIP_MINGW_GCC=0 fetches them)"
    else
        echo "==> SKIP_MINGW_GCC=1; not building $MINGW_GCC"
    fi
elif stamped "$MINGW_GCC" "$MINGW_GCC_ID"; then
    echo "==> mingw-w64 gcc already at $MINGW_GCC"
else
    rm -rf "$MINGW_GCC"
    mkdir -p "$TMP/mingw-gcc"
    for entry in "${MINGW_DEBS[@]}"; do
        fetch_deb "$entry" "$TMP/mingw-gcc"
    done
    mv "$TMP/mingw-gcc" "$MINGW_GCC"
    echo "$MINGW_GCC_ID" > "$MINGW_GCC/.ico-release"
    "$MINGW_GCC/usr/bin/x86_64-w64-mingw32-gcc-14-win32" --version | head -n 1
    echo "==> mingw-w64 gcc at $MINGW_GCC"
fi

# --- 4. CMake ----------------------------------------------------------------
#
# Kitware's portable Linux build for the host (x86_64 or aarch64), pinned by
# version and by the SHA-256 that release's cmake-<version>-SHA-256.txt lists. It runs from any
# directory (its modules are found relative to the executable).
CMAKE_VERSION="${CMAKE_VERSION:-4.4.4}"
CMAKE_SHA256="${CMAKE_SHA256:-$(by_arch \
    e5bb807f7728cb60cd8b27ebc97a2edb469b68655f21e844a600c3575b76f5bb \
    a1b6cc63636a0e55c63257cf3315a8a5f129e42fade25db1afea4ff8ab06f25e)}"
CMAKE_NAME="cmake-${CMAKE_VERSION}-linux-$(by_arch x86_64 aarch64)"
CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/${CMAKE_NAME}.tar.gz"
CMAKE_DIR="$DEST/cmake"

if [[ "${SKIP_CMAKE:-0}" == "1" ]]; then
    echo "==> SKIP_CMAKE=1; not fetching CMake"
elif stamped "$CMAKE_DIR" "$CMAKE_NAME"; then
    echo "==> CMake ${CMAKE_VERSION} already at $CMAKE_DIR"
else
    fetch "$CMAKE_URL" "$CMAKE_SHA256" "$TMP/$CMAKE_NAME.tar.gz"
    tar -C "$TMP" -xzf "$TMP/$CMAKE_NAME.tar.gz"
    rm -rf "$CMAKE_DIR"
    mv "$TMP/$CMAKE_NAME" "$CMAKE_DIR"
    echo "$CMAKE_NAME" > "$CMAKE_DIR/.ico-release"
    "$CMAKE_DIR/bin/cmake" --version | head -n 1
fi

# --- 5. ccache ---------------------------------------------------------------
#
# The project's static Linux x86-64 release, pinned by version and by the
# SHA-256 of the tarball. ccache publishes no Linux arm64 build: an arm64
# host builds without it (CMakeLists.txt drops the launcher when the file is
# missing; a ccache of the host's own can be linked in at
# tools/toolchain/ccache/bin/ccache). The cache itself stays at the default
# ~/.cache/ccache; the compiler's -g embeds absolute paths, so each checkout
# fills its own entries.
CCACHE_VERSION="${CCACHE_VERSION:-4.11.3}"
CCACHE_SHA256="${CCACHE_SHA256:-7766991b91b3a5a177ab33fa043fe09e72c68586d5a86d20a563a05b74f119c0}"
CCACHE_NAME="ccache-${CCACHE_VERSION}-linux-x86_64"
CCACHE_DIR_TOOL="$DEST/ccache"
if [[ "${SKIP_CCACHE:-$(by_arch 0 1)}" == "1" ]]; then
    echo "==> ccache: not fetched on this host or by request; the build runs without it"
elif stamped "$CCACHE_DIR_TOOL" "$CCACHE_NAME" && [[ -x "$CCACHE_DIR_TOOL/bin/ccache" ]]; then
    echo "==> ccache ${CCACHE_VERSION} already at $CCACHE_DIR_TOOL"
else
    fetch "https://github.com/ccache/ccache/releases/download/v${CCACHE_VERSION}/${CCACHE_NAME}.tar.xz" \
        "$CCACHE_SHA256" "$TMP/$CCACHE_NAME.tar.xz"
    tar -C "$TMP" -xJf "$TMP/$CCACHE_NAME.tar.xz"
    rm -rf "$CCACHE_DIR_TOOL"
    mkdir -p "$CCACHE_DIR_TOOL/bin"
    mv "$TMP/$CCACHE_NAME/ccache" "$CCACHE_DIR_TOOL/bin/ccache"
    echo "$CCACHE_NAME" > "$CCACHE_DIR_TOOL/.ico-release"
    "$CCACHE_DIR_TOOL/bin/ccache" --version | head -n 1
fi
# a size cap that a CCACHE_MAXSIZE in the environment overrides
if [[ -x "$CCACHE_DIR_TOOL/bin/ccache" ]]; then
    "$CCACHE_DIR_TOOL/bin/ccache" --set-config "max_size=${CCACHE_MAXSIZE:-5G}"
fi
"$ROOT/tools/fetch_deps.sh" # library dependencies: Vulkan headers, volk, SDL3
echo "==> done: $DEST"
