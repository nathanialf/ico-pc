#!/usr/bin/env bash
# Build the current checkout into an unsigned IPA for LiveContainer.
# Additional arguments go to CMake (e.g. -DICO_DXC=/path/to/host/dxc).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

tools/fetch_moltenvk.sh ios
cmake --preset ios-app "$@"
cmake --build --preset ios-app --parallel "${ICO_BUILD_JOBS:-6}"

app="$root/build-host/ios-app/ICO.app"
[[ -x "$app/ICO" ]] || { echo "Missing iOS executable: $app/ICO" >&2; exit 1; }
plutil -lint "$app/Info.plist"
mkdir -p "$root/dist"
stage="$(mktemp -d "$root/build-host/ios-package.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/Payload"
ditto "$app" "$stage/Payload/ICO.app"
rm -f "$stage/Payload/ICO.app/ico_pc.map"
cp LICENSE "$stage/Payload/ICO.app/LICENSE"
python3 tools/gen_notices.py --platform ios --out "$stage/Payload/ICO.app/NOTICES.txt"
ditto -c -k --keepParent "$stage/Payload" "$stage/ico-pc-ios-unsigned.ipa"
mv "$stage/ico-pc-ios-unsigned.ipa" "$root/dist/ico-pc-ios-unsigned.ipa"
cp "$app/ico_pc.map" "$root/dist/ico-pc-ios.map"
echo "IPA: $root/dist/ico-pc-ios-unsigned.ipa"
