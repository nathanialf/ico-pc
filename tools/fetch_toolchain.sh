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
# licences) provides clang, lld and the i686 and x86_64 mingw-w64 targets
# with the UCRT runtime. The same clang compiles the Linux presets against the
# host's gcc sysroot (cmake/toolchain-linux-clang.cmake).
#
# It also builds two trees from pinned Debian packages (dpkg-deb -x, no
# root): tools/toolchain/sysroot-i386, the 32-bit glibc and libgcc pieces the
# i386 Linux presets need, and tools/toolchain/mingw-gcc, Debian's mingw-w64
# gcc for the GCC-family Windows presets (section 3 says why).
#
# Section 4 unpacks Kitware's CMake release (BSD-3-Clause) into
# tools/toolchain/cmake, the CMake the presets' documented invocation uses
# (tools/toolchain/cmake/bin/cmake); the system's CMake 3.25 or later works
# too (CMakeLists.txt, cmake_minimum_required).
#
# Overrides:
#   LLVM_MINGW_TAG, LLVM_MINGW_SHA256   pin another release
#   LLVM_MINGW_URL                      fetch from elsewhere
#   DEB_SNAPSHOT                        snapshot.debian.org fallback base
#   CMAKE_VERSION, CMAKE_SHA256         pin another CMake release
#   SKIP_SYSROOT=1, SKIP_MINGW_GCC=1    skip section 2 or 3
#   SKIP_CMAKE=1                        skip section 4
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/tools/toolchain"

TAG="${LLVM_MINGW_TAG:-20260922}"
NAME="llvm-mingw-${TAG}-ucrt-ubuntu-22.04-x86_64"
SHA256="${LLVM_MINGW_SHA256:-bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21}"
URL="${LLVM_MINGW_URL:-https://github.com/mstorsjo/llvm-mingw/releases/download/${TAG}/${NAME}.tar.xz}"

STAMP="$DEST/llvm-mingw/.ico-release"

case "$(uname -m)" in
    x86_64) ;;
    *) echo "fetch_toolchain: only x86_64 Linux hosts are pinned here; set LLVM_MINGW_URL" >&2; exit 1 ;;
esac

mkdir -p "$DEST"
TMP="$(mktemp -d "$DEST/.fetch.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# --- 1. llvm-mingw -----------------------------------------------------------

if [[ -f "$STAMP" && "$(cat "$STAMP")" == "$NAME" ]]; then
    echo "==> llvm-mingw ${TAG} already at $DEST/llvm-mingw"
else
    echo "==> fetching $URL"
    curl -fL --retry 3 -o "$TMP/$NAME.tar.xz" "$URL"
    echo "${SHA256}  $TMP/$NAME.tar.xz" | sha256sum -c -
    echo "==> unpacking"
    tar -C "$TMP" -xJf "$TMP/$NAME.tar.xz"
    rm -rf "$DEST/llvm-mingw"
    mv "$TMP/$NAME" "$DEST/llvm-mingw"
    echo "$NAME" > "$STAMP"
    "$DEST/llvm-mingw/bin/clang" --version | head -n 1
fi

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "fetch_toolchain: dpkg-deb not found; it unpacks the Debian packages below" >&2
    exit 1
fi

# --- 2. i386 Linux sysroot for the ref-m32 preset ----------------------------
#
# The container's 64-bit Debian has no 32-bit glibc headers or crt files and
# no root to install them. Debian's biarch packages carry exactly that piece:
# libc6-dev-i386 (gnu/stubs-32.h and the lib32 crt and link stubs),
# libc6-i386 (the lib32 shared objects), lib32gcc-14-dev (libgcc and
# crtbegin for -m32) and lib32gcc-s1. They are unpacked with dpkg-deb -x into
# tools/toolchain/sysroot-i386 and overlaid on the host's /usr/include with
# symlinks, so the sysroot is the host's headers plus the 32-bit pieces. The
# packages are pinned by version and SHA-256; deb.debian.org drops superseded
# versions, so snapshot.debian.org is the fallback.
SYSROOT="$DEST/sysroot-i386"
SYSROOT_ID="glibc-2.41-12+deb13u4/gcc-14.2.0-19"
DEB_SNAPSHOT="${DEB_SNAPSHOT:-https://snapshot.debian.org/archive/debian/20261004T000000Z}"
DEBS=(
    "pool/main/g/glibc/libc6-dev-i386_2.41-12+deb13u4_amd64.deb 703dfda170c766dd82986f12b6494fb4c8f51f2ab93bb214d0be02c69e4e7869"
    "pool/main/g/glibc/libc6-i386_2.41-12+deb13u4_amd64.deb 50c1653ce29519e1fcd8742f2c67f5edad8d4c3aed3d1ffbea2b7c3497a809d2"
    "pool/main/g/gcc-14/lib32gcc-14-dev_14.2.0-19_amd64.deb d43f9b0990676e3ef534267fe018285daa2127d1ffa05a344886af6777950d2f"
    "pool/main/g/gcc-14/lib32gcc-s1_14.2.0-19_amd64.deb aa7674a786d58a1efaacb3ce377c06a2d3153c1fa7360b08375c7dd67c90c3fe"
)

# fetch_deb "<pool path> <sha256>" <dir>: download, verify, unpack into dir.
fetch_deb() {
    local path="${1% *}" sum="${1#* }" dir="$2" deb
    deb="$TMP/$(basename "$path")"
    echo "==> fetching $(basename "$path")"
    if ! curl -fsL --retry 3 -o "$deb" "http://deb.debian.org/debian/${path//+/%2B}"; then
        curl -fsL --retry 3 -o "$deb" "${DEB_SNAPSHOT}/${path//+/%2B}"
    fi
    echo "${sum}  ${deb}" | sha256sum -c -
    dpkg-deb -x "$deb" "$dir"
    rm -f "$deb"
}

if [[ "${SKIP_SYSROOT:-0}" == "1" ]]; then
    echo "==> SKIP_SYSROOT=1; not building $SYSROOT"
elif [[ -f "$SYSROOT/.ico-release" && "$(cat "$SYSROOT/.ico-release")" == "$SYSROOT_ID" ]]; then
    echo "==> i386 sysroot already at $SYSROOT"
else
    rm -rf "$SYSROOT"
    mkdir -p "$TMP/sysroot"
    for entry in "${DEBS[@]}"; do
        fetch_deb "$entry" "$TMP/sysroot"
    done
    # Overlay: every host header the packages do not provide becomes a
    # symlink to /usr/include, one directory level deep where the packages
    # add files (usr/include/x86_64-linux-gnu/gnu).
    overlay() { # overlay <dir in sysroot> <host dir>
        local dst="$1" src="$2" e name
        mkdir -p "$dst"
        for e in "$src"/*; do
            name="$(basename "$e")"
            if [[ -d "$dst/$name" && ! -L "$dst/$name" && -d "$e" ]]; then
                overlay "$dst/$name" "$e"
            elif [[ ! -e "$dst/$name" && ! -L "$dst/$name" ]]; then
                ln -s "$e" "$dst/$name"
            fi
        done
    }
    # The biarch package's usr/include/{sys,bits,gnu,...} entries are
    # symlinks into x86_64-linux-gnu, as Debian's own /usr/include has them.
    overlay "$TMP/sysroot/usr/include" /usr/include
    # glibc's lib32 libc.so and libm.so are linker scripts naming
    # /lib32/libc.so.6 and /lib/ld-linux.so.2; lld resolves those under
    # --sysroot, so give the merged-/usr layout its root-level links.
    ln -s usr/lib32 "$TMP/sysroot/lib32"
    mkdir -p "$TMP/sysroot/lib"
    ln -s ../usr/lib32/ld-linux.so.2 "$TMP/sysroot/lib/ld-linux.so.2"
    mv "$TMP/sysroot" "$SYSROOT"
    echo "$SYSROOT_ID" > "$SYSROOT/.ico-release"
    echo "==> i386 sysroot at $SYSROOT"
fi

# --- 3. mingw-w64 gcc, the GCC-family Windows presets ------------------------
#
# The *-gcc presets compile with GCC, which accepts the game's GNU nested
# functions (clang does not; docs/port/BUILD_STATUS.md). Debian's mingw-w64
# cross gcc 14 and binutils for both targets, unpacked like the sysroot;
# gcc finds its own pieces relative to its executable, so the tree works
# from any directory.
MINGW_GCC="$DEST/mingw-gcc"
MINGW_GCC_ID="gcc-mingw-w64-14.2.0-19+27+b1/mingw-w64-12.0.0-5/binutils-2.44-3+12+b1"
MINGW_DEBS=(
    "pool/main/g/gcc-mingw-w64/gcc-mingw-w64-base_14.2.0-19+27+b1_amd64.deb 8d2c64b886ab4a435a78ddc4cd3b510f149b65fbd6be8472e8f460627562718a"
    "pool/main/g/gcc-mingw-w64/gcc-mingw-w64-i686-win32_14.2.0-19+27+b1_amd64.deb 918cc18b87ccaa197a2abf53d00382cb48779a99ea5c9c10413099d92a4dfa1f"
    "pool/main/g/gcc-mingw-w64/gcc-mingw-w64-i686-win32-runtime_14.2.0-19+27+b1_amd64.deb 1bf47cc4727c2be08930cbc7eb8b3a37a67eb5cf02ee72a3834c2fd2ebfc6bbc"
    "pool/main/g/gcc-mingw-w64/gcc-mingw-w64-x86-64-win32_14.2.0-19+27+b1_amd64.deb a4d7abc5c9c97380eeac348d3b94d522a04e80028bb825cd44df09d9ba90092f"
    "pool/main/g/gcc-mingw-w64/gcc-mingw-w64-x86-64-win32-runtime_14.2.0-19+27+b1_amd64.deb 73a27ee935bec4c81331c3e92c2399e11f161e75fe27898524dcf2bae77f97cb"
    "pool/main/b/binutils-mingw-w64/binutils-mingw-w64-i686_2.44-3+12+b1_amd64.deb a8b90b43c53371e436e804a2335bfef77824dbd3d40c74e01acbed04ad333310"
    "pool/main/b/binutils-mingw-w64/binutils-mingw-w64-x86-64_2.44-3+12+b1_amd64.deb b53f4a091b16a0d0952b8e3bd6027931641386258f96b37db1f0c62c787221d3"
    "pool/main/m/mingw-w64/mingw-w64-common_12.0.0-5_all.deb 97dce5d0d8aff1cade4786ad4ef5078345477a598829afcac2e7f2fbc5399990"
    "pool/main/m/mingw-w64/mingw-w64-i686-dev_12.0.0-5_all.deb ddd5f65c29014ae67b89df146a8016f38056a9d8eadbccd17cae4c00ccd01528"
    "pool/main/m/mingw-w64/mingw-w64-x86-64-dev_12.0.0-5_all.deb 0bf89cf7454cccb49cd2a46a6f7b33896f6ae3a1a2fe33e02c40be6155bbb385"
)

if [[ "${SKIP_MINGW_GCC:-0}" == "1" ]]; then
    echo "==> SKIP_MINGW_GCC=1; not building $MINGW_GCC"
elif [[ -f "$MINGW_GCC/.ico-release" && "$(cat "$MINGW_GCC/.ico-release")" == "$MINGW_GCC_ID" ]]; then
    echo "==> mingw-w64 gcc already at $MINGW_GCC"
else
    rm -rf "$MINGW_GCC"
    mkdir -p "$TMP/mingw-gcc"
    for entry in "${MINGW_DEBS[@]}"; do
        fetch_deb "$entry" "$TMP/mingw-gcc"
    done
    mv "$TMP/mingw-gcc" "$MINGW_GCC"
    echo "$MINGW_GCC_ID" > "$MINGW_GCC/.ico-release"
    "$MINGW_GCC/usr/bin/i686-w64-mingw32-gcc-14-win32" --version | head -n 1
    echo "==> mingw-w64 gcc at $MINGW_GCC"
fi

# --- 4. CMake ----------------------------------------------------------------
#
# Kitware's portable Linux x86-64 build, pinned by version and by the SHA-256
# that release's cmake-<version>-SHA-256.txt lists. It runs from any
# directory (its modules are found relative to the executable).
CMAKE_VERSION="${CMAKE_VERSION:-4.4.4}"
CMAKE_SHA256="${CMAKE_SHA256:-e5bb807f7728cb60cd8b27ebc97a2edb469b68655f21e844a600c3575b76f5bb}"
CMAKE_NAME="cmake-${CMAKE_VERSION}-linux-x86_64"
CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/${CMAKE_NAME}.tar.gz"
CMAKE_DIR="$DEST/cmake"

if [[ "${SKIP_CMAKE:-0}" == "1" ]]; then
    echo "==> SKIP_CMAKE=1; not fetching CMake"
elif [[ -f "$CMAKE_DIR/.ico-release" && "$(cat "$CMAKE_DIR/.ico-release")" == "$CMAKE_NAME" ]]; then
    echo "==> CMake ${CMAKE_VERSION} already at $CMAKE_DIR"
else
    echo "==> fetching $CMAKE_URL"
    curl -fL --retry 3 -o "$TMP/$CMAKE_NAME.tar.gz" "$CMAKE_URL"
    echo "${CMAKE_SHA256}  $TMP/$CMAKE_NAME.tar.gz" | sha256sum -c -
    tar -C "$TMP" -xzf "$TMP/$CMAKE_NAME.tar.gz"
    rm -rf "$CMAKE_DIR"
    mv "$TMP/$CMAKE_NAME" "$CMAKE_DIR"
    echo "$CMAKE_NAME" > "$CMAKE_DIR/.ico-release"
    "$CMAKE_DIR/bin/cmake" --version | head -n 1
fi
"$ROOT/tools/fetch_deps.sh" # library dependencies: Vulkan headers, volk, SDL3
echo "==> done: $DEST"
