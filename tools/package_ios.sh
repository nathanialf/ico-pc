#!/usr/bin/env bash
# Build the current checkout into an unsigned IPA for LiveContainer.
# tools/package_ios.sh [<label>] [CMake arguments...]
# <label> (v0.7.0) gives the bundle its version, as package_win.sh does
# (CMakeLists.txt ICO_VERSION_LABEL; 0.0.0 without one), and the files
# their names: dist/ico-pc-<label>-ios-unsigned.ipa and
# dist/ico-pc-<label>-ios.map (ico-pc-ios-unsigned.ipa and ico-pc-ios.map
# without one). The other arguments go to CMake (e.g.
# -DICO_DXC=/path/to/host/dxc).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

tools/fetch_moltenvk.sh ios
label=""
if [[ $# -gt 0 && "$1" != -* ]]; then
    label="$1"
    shift
fi
# ICO_RC_TOOLS: the rc-only tooling, on for a label ending in -rc<N> alone and
# given every time, so a build folder an rc configured keeps no ON
# (tools/check_rc_text.py; docs/BUILDING.md, "Release candidate tools")
rc_flag="$(python3 -I tools/check_rc_text.py --cmake-flag "$label")"
cmake --preset ios-app "-DICO_VERSION_LABEL=$label" "$rc_flag" "$@"
cmake --build --preset ios-app --parallel "${ICO_BUILD_JOBS:-6}"

app="$root/build-host/ios-app/ICO.app"
[[ -x "$app/ICO" ]] || { echo "Missing iOS executable: $app/ICO" >&2; exit 1; }
# a release's program holds no rc row, an rc's does
python3 -I tools/check_rc_text.py --label "$label" "$app/ICO"
plutil -lint "$app/Info.plist"
mkdir -p "$root/dist"
stage="$(mktemp -d "$root/build-host/ios-package.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/Payload"
ditto "$app" "$stage/Payload/ICO.app"
rm -f "$stage/Payload/ICO.app/ico_pc.map"
cp LICENSE "$stage/Payload/ICO.app/LICENSE"
python3 tools/gen_notices.py --platform ios --out "$stage/Payload/ICO.app/NOTICES.txt"
name="ico-pc${label:+-$label}-ios"
ditto -c -k --keepParent "$stage/Payload" "$stage/$name-unsigned.ipa"
mv "$stage/$name-unsigned.ipa" "$root/dist/$name-unsigned.ipa"
cp "$app/ico_pc.map" "$root/dist/$name.map"
echo "IPA: $root/dist/$name-unsigned.ipa"
