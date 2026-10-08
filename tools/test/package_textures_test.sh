#!/usr/bin/env bash
# Tests the textures-folder staging and archive checks of the release
# packages (tools/package_textures_lib.sh) on a temporary directory; builds
# nothing and runs nothing of the game.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../package_textures_lib.sh
. "$here/tools/package_textures_lib.sh"
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1" >&2; exit 1; }
rel="$pkg_textures_rel/README.txt"

# Windows: CRLF, in the zip
w="$t/win"; mkdir -p "$w/pkg/ico-pc-x/x64" "$t/out"
pkg_stage_textures_readme "$w/stage" crlf
[[ -s "$w/stage/$rel" ]] || bad "win: README.txt not staged"
[[ "$(wc -l < "$w/stage/$rel")" -ge 7 ]] || bad "win: README.txt too short"
[[ "$(grep -c $'\r$' "$w/stage/$rel")" == "$(wc -l < "$w/stage/$rel")" ]] || bad "win: not all CRLF"
ok "win staging, CRLF"
mkdir -p "$w/pkg/ico-pc-x/x64/$pkg_textures_rel"
cp -a "$w/stage/$rel" "$w/pkg/ico-pc-x/x64/$rel"
python3 -I - "$w/pkg" ico-pc-x "$t/out/a.zip" <<'PY'
import os, sys, zipfile
base, top, out = sys.argv[1:4]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for dp, _, fs in os.walk(os.path.join(base, top)):
        for f in sorted(fs):
            p = os.path.join(dp, f); z.write(p, os.path.relpath(p, base))
PY
pkg_assert_zip_has "$t/out/a.zip" "ico-pc-x/x64/$rel" || bad "zip check misses a present entry"
pkg_assert_zip_has "$t/out/a.zip" "ico-pc-x/$rel" && bad "zip check accepts a wrong path"
rm -rf "$w/pkg/ico-pc-x/x64/textures"
python3 -I - "$w/pkg" ico-pc-x "$t/out/b.zip" <<'PY'
import os, sys, zipfile
base, top, out = sys.argv[1:4]
with zipfile.ZipFile(out, "w") as z:
    z.writestr(top + "/x64/a.txt", "a")
PY
pkg_assert_zip_has "$t/out/b.zip" "ico-pc-x/x64/$rel" && bad "zip check accepts a zip without it"
ok "zip check passes and fails correctly"

# Linux: LF, in the tar
l="$t/lin"; mkdir -p "$l/pkg/ico-pc-x"
pkg_stage_textures_readme "$l/stage" lf
! grep -q $'\r' "$l/stage/$rel" || bad "linux: has CR"
mkdir -p "$l/pkg/ico-pc-x/$pkg_textures_rel"; cp -a "$l/stage/$rel" "$l/pkg/ico-pc-x/$rel"
tar -C "$l/pkg" --sort=name --owner=0 --group=0 --numeric-owner -czf "$t/out/a.tgz" ico-pc-x
pkg_assert_tar_has "$t/out/a.tgz" "ico-pc-x/$rel" || bad "tar check misses a present entry"
pkg_assert_tar_has "$t/out/a.tgz" "ico-pc-x/textures/other" && bad "tar check accepts a wrong path"
ok "linux staging LF, tar check"
echo "package_textures_test: OK"
