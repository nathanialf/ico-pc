#!/usr/bin/env bash
# =============================================================================
# tools/fetch_android.sh
#
# Sets up the Android build (android/, preset android-arm64): user-local, no
# root, idempotent (each item is checked or stamped and skipped when it is
# already there). Run tools/fetch_toolchain.sh first: the SDL3 build below
# uses its pinned CMake, and the game build needs the dependencies
# tools/fetch_deps.sh fetched (Vulkan-Headers, volk, DXC, libmpeg2, libchdr).
#
#   Android SDK packages (sdkmanager, into $ANDROID_HOME):
#     ndk;28.2.13676358      NDK r28c (16 KB pages by default on arm64)
#     platforms;android-35   compileSdk 35
#     build-tools;35.0.0     aapt2, zipalign, apksigner
#   tools/toolchain/android-cmake/bin/
#                          links to the pinned CMake (tools/toolchain/cmake)
#                          and ninja (.venv/bin), the CMake the Android Gradle
#                          plugin runs (android/local.properties, cmake.dir)
#   tools/toolchain/deps/sdl3/android-arm64/
#                          SDL3 (zlib) built from the release source tarball
#                          with the NDK for arm64-v8a, android-29, shared only:
#                            lib/cmake/SDL3, include/   the CMake package
#                            jniLibs/arm64-v8a/libSDL3.so
#                            java/org/libsdl/app/       SDL's Java glue (the
#                                                       same release)
#                            LICENSE.txt
#   tools/toolchain/android.env
#                          KEY="value" lines, sourceable by a shell and read
#                          by cmake/toolchains/android-arm64.cmake and
#                          android/app/build.gradle (keys below)
#   android/local.properties
#                          sdk.dir and cmake.dir for Gradle (gitignored;
#                          written for this checkout)
#
# android.env keys: ANDROID_HOME, ANDROID_SDK_ROOT, ANDROID_NDK_ROOT,
# ANDROID_NDK_HOME, ICO_ANDROID_NDK_VERSION, ICO_ANDROID_PLATFORM,
# ICO_ANDROID_BUILD_TOOLS (the build-tools directory), ICO_ANDROID_CMAKE_DIR,
# ICO_ANDROID_CMAKE_VERSION, ICO_SDL3_ANDROID (the SDL3 prefix above).
#
# Overrides:
#   ANDROID_HOME (else ANDROID_SDK_ROOT, else ~/Android/Sdk)
#   ICO_ANDROID_NDK_VERSION, ICO_ANDROID_BUILD_TOOLS_VERSION,
#   ICO_ANDROID_COMPILE_SDK
#   SDL3_VERSION, SDL3_SRC_SHA256 (the same pins as tools/fetch_deps.sh)
#   SKIP_SDK_INSTALL=1      only check the SDK packages, never install
#   ICO_CMAKE               the CMake for the SDL3 build and for Gradle
#   ICO_JOBS                build parallelism (default 2)
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/tools/toolchain"
# tools/toolchain may be a link shared by several checkouts: the files below
# name its real path
TC="$(cd "$ROOT/tools/toolchain" && pwd -P)"
DEST="$TC/deps"
mkdir -p "$DEST"
TMP="$(mktemp -d "$DEST/.fetch-android.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# fetch and stamped
# shellcheck source=tools/fetch_common.sh
source "$ROOT/tools/fetch_common.sh"

die() {
    echo "fetch_android: $*" >&2
    exit 1
}

JOBS="${ICO_JOBS:-2}"
NDK_VERSION="${ICO_ANDROID_NDK_VERSION:-28.2.13676358}"
BUILD_TOOLS_VERSION="${ICO_ANDROID_BUILD_TOOLS_VERSION:-35.0.0}"
COMPILE_SDK="${ICO_ANDROID_COMPILE_SDK:-35}"
ANDROID_PLATFORM_LEVEL="android-29"

# --- 1. The SDK packages -------------------------------------------------------
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
[[ -d "$SDK" ]] || die "no Android SDK at $SDK (set ANDROID_HOME)"
SDK="$(cd "$SDK" && pwd -P)"
SDKMANAGER="$SDK/cmdline-tools/latest/bin/sdkmanager"
if [[ ! -x "$SDKMANAGER" ]]; then
    SDKMANAGER="$(command -v sdkmanager || true)"
fi

# sdk_package <package> <directory that holds it>
sdk_package() {
    if [[ -f "$2/source.properties" || -f "$2/package.xml" ]]; then
        echo "==> $1 already at $2"
        return
    fi
    [[ "${SKIP_SDK_INSTALL:-0}" != "1" ]] || die "$1 missing at $2 (SKIP_SDK_INSTALL=1)"
    [[ -n "$SDKMANAGER" ]] || die "$1 missing and no sdkmanager (install the SDK command-line tools)"
    echo "==> installing $1 with sdkmanager"
    yes | "$SDKMANAGER" --sdk_root="$SDK" --licenses >/dev/null || true
    yes | "$SDKMANAGER" --sdk_root="$SDK" --install "$1" >"$TMP/sdkmanager.log" 2>&1 || {
        tail -20 "$TMP/sdkmanager.log" >&2
        die "sdkmanager could not install $1"
    }
    [[ -f "$2/source.properties" || -f "$2/package.xml" ]] || die "$1 not found at $2 after sdkmanager"
}
NDK="$SDK/ndk/$NDK_VERSION"
sdk_package "ndk;$NDK_VERSION" "$NDK"
sdk_package "platforms;android-$COMPILE_SDK" "$SDK/platforms/android-$COMPILE_SDK"
sdk_package "build-tools;$BUILD_TOOLS_VERSION" "$SDK/build-tools/$BUILD_TOOLS_VERSION"
got="$(sed -n 's/^Pkg.Revision *= *//p' "$NDK/source.properties")"
[[ "$got" == "$NDK_VERSION" ]] || die "$NDK holds NDK $got, expected $NDK_VERSION"
NDK_TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
[[ -f "$NDK_TOOLCHAIN" ]] || die "no $NDK_TOOLCHAIN"

# --- 2. CMake and ninja for the Gradle plugin -----------------------------------
# The plugin runs <cmake.dir>/bin/cmake and expects ninja beside it.
CMAKE_BIN="${ICO_CMAKE:-}"
if [[ -z "$CMAKE_BIN" ]]; then
    if [[ -x "$TC/cmake/bin/cmake" ]]; then
        CMAKE_BIN="$TC/cmake/bin/cmake"
    else
        CMAKE_BIN="$(command -v cmake || true)"
    fi
fi
[[ -n "$CMAKE_BIN" && -x "$CMAKE_BIN" ]] || die "no CMake (run tools/fetch_toolchain.sh)"
CMAKE_BIN="$(readlink -f "$CMAKE_BIN")"
CMAKE_VERSION_GOT="$("$CMAKE_BIN" --version | sed -n 's/^cmake version \([0-9.]*\).*/\1/p')"
NINJA_BIN=""
if [[ -x "$ROOT/.venv/bin/ninja" ]]; then
    NINJA_BIN="$(readlink -f "$ROOT/.venv/bin/ninja")"
elif command -v ninja >/dev/null 2>&1; then
    NINJA_BIN="$(readlink -f "$(command -v ninja)")"
fi
[[ -n "$NINJA_BIN" ]] || die "no ninja (tools/setup.sh installs it into .venv)"
ACMAKE="$TC/android-cmake"
mkdir -p "$ACMAKE/bin"
ln -sfn "$CMAKE_BIN" "$ACMAKE/bin/cmake"
ln -sfn "$NINJA_BIN" "$ACMAKE/bin/ninja"
echo "==> CMake $CMAKE_VERSION_GOT and ninja for Gradle at $ACMAKE/bin"

# --- 3. SDL3 for arm64-v8a ------------------------------------------------------
#
# The release source tarball tools/fetch_deps.sh pins (the SHA-256 GitHub
# lists for the asset), built with the NDK's CMake toolchain. The Java glue
# (android-project/app/src/main/java/org/libsdl/app/) comes from the same
# tarball, so the Java and native halves are one release. SDL3.jar is not
# built: the app compiles the glue from source.
SDL3_VERSION="${SDL3_VERSION:-3.4.18}"
SDL3_SRC_SHA256="${SDL3_SRC_SHA256:-9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3}"
SDL3_BASE="https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}"
SDL3_ANDROID="$DEST/sdl3/android-arm64"
SDL3_ANDROID_ID="SDL3-${SDL3_VERSION}+ndk-${NDK_VERSION}+${ANDROID_PLATFORM_LEVEL}"
if stamped "$SDL3_ANDROID" "$SDL3_ANDROID_ID"; then
    echo "==> SDL3 ${SDL3_VERSION} (android-arm64) already at $SDL3_ANDROID"
else
    fetch "${SDL3_BASE}/SDL3-${SDL3_VERSION}.tar.gz" "$SDL3_SRC_SHA256" "$TMP/SDL3.tar.gz"
    tar -C "$TMP" -xzf "$TMP/SDL3.tar.gz"
    src="$TMP/SDL3-${SDL3_VERSION}"
    glue="$src/android-project/app/src/main/java/org/libsdl/app"
    [[ -f "$glue/SDLActivity.java" ]] || die "no SDLActivity.java in the SDL ${SDL3_VERSION} tarball"
    rm -rf "$SDL3_ANDROID"
    "$CMAKE_BIN" -S "$src" -B "$TMP/sdl3-build" -G Ninja \
        -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \
        -DCMAKE_TOOLCHAIN_FILE="$NDK_TOOLCHAIN" \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM="$ANDROID_PLATFORM_LEVEL" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,max-page-size=16384" \
        -DCMAKE_INSTALL_PREFIX="$SDL3_ANDROID" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF \
        -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF \
        -DSDL_INSTALL_DOCS=OFF -DSDL_ANDROID_JAR=OFF
    "$CMAKE_BIN" --build "$TMP/sdl3-build" --parallel "$JOBS"
    "$CMAKE_BIN" --install "$TMP/sdl3-build"
    mkdir -p "$SDL3_ANDROID/jniLibs/arm64-v8a" "$SDL3_ANDROID/java/org/libsdl/app"
    cp "$SDL3_ANDROID/lib/libSDL3.so" "$SDL3_ANDROID/jniLibs/arm64-v8a/libSDL3.so"
    cp "$glue"/*.java "$SDL3_ANDROID/java/org/libsdl/app/"
    cp "$src/LICENSE.txt" "$SDL3_ANDROID/"
    echo "$SDL3_ANDROID_ID" > "$SDL3_ANDROID/.ico-release"
    echo "==> SDL3 ${SDL3_VERSION} (android-arm64) at $SDL3_ANDROID"
fi

# --- 4. android.env and android/local.properties -------------------------------
for v in "$SDK" "$NDK" "$ACMAKE" "$SDL3_ANDROID"; do
    [[ "$v" != *[\"\\\$\`[:space:]]* ]] || die "path with a quote, space or \$ not supported: $v"
done
cat > "$TC/android.env.tmp" <<ENV
# written by tools/fetch_android.sh; do not edit
ANDROID_HOME="$SDK"
ANDROID_SDK_ROOT="$SDK"
ANDROID_NDK_ROOT="$NDK"
ANDROID_NDK_HOME="$NDK"
ICO_ANDROID_NDK_VERSION="$NDK_VERSION"
ICO_ANDROID_PLATFORM="$ANDROID_PLATFORM_LEVEL"
ICO_ANDROID_BUILD_TOOLS="$SDK/build-tools/$BUILD_TOOLS_VERSION"
ICO_ANDROID_CMAKE_DIR="$ACMAKE"
ICO_ANDROID_CMAKE_VERSION="$CMAKE_VERSION_GOT"
ICO_SDL3_ANDROID="$SDL3_ANDROID"
ENV
mv "$TC/android.env.tmp" "$TC/android.env"
echo "==> $TC/android.env"
cat > "$ROOT/android/local.properties" <<PROPS
# written by tools/fetch_android.sh for this checkout; not committed
sdk.dir=$SDK
cmake.dir=$ACMAKE
PROPS
echo "==> $ROOT/android/local.properties"
echo "==> android done"
