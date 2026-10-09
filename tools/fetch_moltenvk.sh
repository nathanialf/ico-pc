#!/usr/bin/env bash
# Pinned Apple renderer dependencies; default is the iPhone device build.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/tools/toolchain/deps"
PLATFORM="${1:-ios}"
case "$PLATFORM" in
    ios)
        SDK=iphoneos
        MIN_OS=15.0
        SUM=b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1
        ;;
    macos)
        SDK=macosx
        MIN_OS=12.0
        SUM=f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e
        ;;
    *) echo 'Usage: tools/fetch_moltenvk.sh [ios|macos]' >&2; exit 1 ;;
esac
[[ "$(uname -s)" == Darwin ]] || { echo 'Xcode on macOS is required.' >&2; exit 1; }
for tool in cmake ninja xcrun curl shasum; do command -v "$tool" >/dev/null; done
source "$ROOT/tools/fetch_common.sh"
mkdir -p "$DEST/moltenvk" "$DEST/sdl3"
TMP="$(mktemp -d "$DEST/.moltenvk.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
if ! stamped "$DEST/moltenvk/$PLATFORM" 'v1.4.2'; then
    fetch "https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-${PLATFORM}.tar" \
        "$SUM" "$TMP/moltenvk.tar"
    mkdir "$TMP/mvk"
    tar -xf "$TMP/moltenvk.tar" -C "$TMP/mvk"
    rm -rf "$DEST/moltenvk/$PLATFORM"
    mv "$TMP/mvk" "$DEST/moltenvk/$PLATFORM"
    echo 'v1.4.2' > "$DEST/moltenvk/$PLATFORM/.ico-release"
fi
LICENSE_DIR="$DEST/moltenvk/$PLATFORM/licenses"
mkdir -p "$LICENSE_DIR"
# The revisions embedded in MoltenVK v1.4.2 (ExternalRevisions/).
while read -r repo revision file sum; do
    name="${repo}-LICENSE.txt"
    if [[ ! -f "$LICENSE_DIR/$name" ]]; then
        fetch "https://raw.githubusercontent.com/KhronosGroup/${repo}/${revision}/${file}" \
            "$sum" "$TMP/$name"
        mv "$TMP/$name" "$LICENSE_DIR/$name"
    fi
done <<'LICENSES'
SPIRV-Cross 6c09849fe88c48eaed08413aa022aaa136a3a057 LICENSE cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30
SPIRV-Tools 0d6fd73ca73830ccab5fa1f00ed5ed40124e2c55 LICENSE cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30
Vulkan-Headers e3b1eec08173d6b825cd3ac88c885a63b621504a LICENSES/MIT.txt 1ca3502222d967f3be5751c55f6b7ee735b5383909c3b501495f54b216dbf227
LICENSES
SDL_PREFIX="$DEST/sdl3/${PLATFORM}-arm64"
if ! stamped "$SDL_PREFIX" "SDL3-${SDL3_VERSION}+moltenvk"; then
    fetch "https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}/SDL3-${SDL3_VERSION}.tar.gz" \
        "$SDL3_SRC_SHA256" "$TMP/sdl.tar.gz"
    tar -xzf "$TMP/sdl.tar.gz" -C "$TMP"
    CMAKE_SYSTEM=Darwin
    [[ "$PLATFORM" != ios ]] || CMAKE_SYSTEM=iOS
    cmake -S "$TMP/SDL3-${SDL3_VERSION}" -B "$TMP/sdl-build" -G Ninja \
        -DCMAKE_SYSTEM_NAME="$CMAKE_SYSTEM" -DCMAKE_OSX_SYSROOT="$(xcrun --sdk "$SDK" --show-sdk-path)" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_OS" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF \
        -DSDL_VULKAN=ON -DSDL_METAL=ON
    cmake --build "$TMP/sdl-build" -j "${ICO_BUILD_JOBS:-6}"
    cmake --install "$TMP/sdl-build"
    cp "$TMP/SDL3-${SDL3_VERSION}/LICENSE.txt" "$SDL_PREFIX/LICENSE.txt"
    echo "SDL3-${SDL3_VERSION}+moltenvk" > "$SDL_PREFIX/.ico-release"
fi
"$ROOT/tools/fetch_deps.sh" --game-only
echo "MoltenVK v1.4.2 and SDL3 ${SDL3_VERSION} ready for ${PLATFORM}/arm64."
