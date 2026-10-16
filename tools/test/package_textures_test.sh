#!/usr/bin/env bash
# Tests the textures-folder staging and archive checks of the release
# packages (tools/package_textures_lib.sh) on a temporary directory; builds
# nothing and runs nothing of the game.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../package_textures_lib.sh
. "$here/tools/package_textures_lib.sh"
# shellcheck source=../package_docs_lib.sh
. "$here/tools/package_docs_lib.sh"
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1" >&2; exit 1; }
rel="$pkg_textures_rel/README.txt"

# Windows: CRLF, in the zip
w="$t/win"; mkdir -p "$w/pkg/ico-pc-x/x64" "$t/out"
pkg_stage_folder_notes "$w/stage" crlf
for r in "${pkg_note_rels[@]}"; do [[ -f "$w/stage/$r/README.txt" ]] || bad "no note staged at $r"; grep -q $'\r' "$w/stage/$r/README.txt" || bad "no CR in the Windows note at $r"; done
[[ -s "$w/stage/$rel" ]] || bad "win: README.txt not staged"
[[ "$(wc -l < "$w/stage/$rel")" -ge 7 ]] || bad "win: README.txt too short"
[[ "$(grep -c $'\r$' "$w/stage/$rel")" == "$(wc -l < "$w/stage/$rel")" ]] || bad "win: not all CRLF"
ok "win staging, CRLF"
mkdir -p "$w/pkg/ico-pc-x/x64/$pkg_textures_rel"
cp -a "$w/stage/$rel" "$w/pkg/ico-pc-x/x64/$rel"
pkg_zip_dir "$w/pkg" ico-pc-x "$t/out/a.zip"
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
pkg_stage_folder_notes "$l/stage" lf
for r in "${pkg_note_rels[@]}"; do [[ -f "$l/stage/$r/README.txt" ]] || bad "no note staged at $r"; grep -q $'\r' "$l/stage/$r/README.txt" && bad "a CR in the Linux note at $r"; done
! grep -q $'\r' "$l/stage/$rel" || bad "linux: has CR"
mkdir -p "$l/pkg/ico-pc-x/$pkg_textures_rel"; cp -a "$l/stage/$rel" "$l/pkg/ico-pc-x/$rel"
tar -C "$l/pkg" --sort=name --owner=0 --group=0 --numeric-owner -czf "$t/out/a.tgz" ico-pc-x
pkg_assert_tar_has "$t/out/a.tgz" "ico-pc-x/$rel" || bad "tar check misses a present entry"
pkg_assert_tar_has "$t/out/a.tgz" "ico-pc-x/textures/other" && bad "tar check accepts a wrong path"
ok "linux staging LF, tar check"

# portable archives: the ini, the README paragraph, the archive checks
for plat in windows linux; do
    pkg_write_ini "$t/n-$plat.ini" "$plat" "x.iso"
    pkg_write_ini "$t/p-$plat.ini" "$plat" "x.iso" portable
    ! grep -qE '^portable=1$' "$t/n-$plat.ini" || bad "$plat: the normal ini has portable=1 on"
    [[ "$(grep -cE '^portable=1$' "$t/p-$plat.ini")" == 1 ]] || bad "$plat: the portable ini lacks portable=1"
    grep -q '^iso=x.iso$' "$t/p-$plat.ini" || bad "$plat: the portable ini lost iso="
    grep -q 'make_portable' "$t/n-$plat.ini" || bad "$plat: the normal ini does not name the script"
done
ok "pkg_write_ini portable argument"
printf '# ICO\n' > "$t/README.md"; pkg_portable_readme_note "$t/README.md"
grep -q 'portable download' "$t/README.md" || bad "README note missing"
ok "README paragraph"
q="$t/arc"; mkdir -p "$q/ico-pc-x/x64"
cp "$t/p-windows.ini" "$q/ico-pc-x/x64/ico-pc.ini"; sed 's/$/\r/' "$here/tools/portable/make_portable.cmd" > "$q/ico-pc-x/x64/m.cmd"
pkg_zip_dir "$q" ico-pc-x "$t/out/c.zip"
pkg_assert_zip_line "$t/out/c.zip" ico-pc-x/x64/ico-pc.ini '^portable=1$' || bad "zip line check misses a present line"
pkg_assert_zip_line "$t/out/c.zip" ico-pc-x/x64/ico-pc.ini '^portable=0$' && bad "zip line check accepts an absent line"
pkg_assert_zip_line "$t/out/c.zip" ico-pc-x/x64/m.cmd '\r$' || bad "CRLF script not seen as CRLF"
pkg_assert_zip_lacks "$t/out/c.zip" ico-pc-x/x64/make_portable.cmd || bad "zip lacks check rejects a missing entry"
pkg_assert_zip_lacks "$t/out/c.zip" ico-pc-x/x64/m.cmd && bad "zip lacks check accepts a present entry"
mkdir -p "$q/ico-pc-x/x64"; cp "$t/p-linux.ini" "$q/ico-pc-x/ico-pc.ini"
tar -C "$q" --sort=name --owner=0 --group=0 --numeric-owner -czf "$t/out/c.tgz" ico-pc-x
pkg_assert_tar_line "$t/out/c.tgz" ico-pc-x/ico-pc.ini '^portable=1$' || bad "tar line check misses a present line"
pkg_assert_tar_line "$t/out/c.tgz" ico-pc-x/ico-pc.ini '^portable=0$' && bad "tar line check accepts an absent line"
pkg_assert_tar_lacks "$t/out/c.tgz" ico-pc-x/make_portable.sh || bad "tar lacks check rejects a missing entry"
pkg_assert_tar_lacks "$t/out/c.tgz" ico-pc-x/ico-pc.ini && bad "tar lacks check accepts a present entry"
ok "portable archive checks"

# make_portable.sh on a temporary install and a temporary home
m="$t/mp"; mkdir -p "$m/game" "$m/home/.local/share/ico-pc/ico-pc/memcard"
cp "$here/tools/portable/make_portable.sh" "$m/game/"; chmod 755 "$m/game/make_portable.sh"
echo save > "$m/home/.local/share/ico-pc/ico-pc/memcard/s"
HOME="$m/home" XDG_DATA_HOME= "$m/game/make_portable.sh" </dev/null >"$m/out1" 2>&1 || bad "make_portable.sh failed on a fresh copy"
[[ -d "$m/game/userdata" ]] || bad "make_portable.sh made no userdata"
[[ -f "$m/home/.local/share/ico-pc/ico-pc/memcard/s" ]] || bad "make_portable.sh moved files without asking"
echo x > "$m/game/userdata/f"
if HOME="$m/home" "$m/game/make_portable.sh" </dev/null >"$m/out2" 2>&1; then bad "make_portable.sh accepted a non-empty userdata"; fi
rm "$m/game/userdata/f"
ok "make_portable.sh refuses and creates"
# The player archives carry no developer files (browsers and Windows flag a
# bundle that does): the zip file list of package_win.sh and the tar list
# of package_linux.sh name only the game, mc_import, the notes and the docs,
# and both scripts assert the developer files are absent.
zl="$(sed -n '/^    files=(/,/^    \[\[ "\$variant" == portable \]\] || files/p' "$here/tools/package_win.sh")"
[[ -n "$zl" ]] || bad "package_win.sh: the zip file list was not found"
for n in tools/mc_import.exe tools/SDL3.dll tools/README.txt ico_pc_; do grep -q "$n" <<<"$zl" || bad "package_win.sh: the zip list lacks $n"; done
for n in rhi_d3d12_test rd_replay_tool compare_backends compare_png; do
    if grep -q "$n" <<<"$zl"; then bad "package_win.sh: the zip list names $n"; fi
    grep -q "pkg_assert_zip_lacks.*\$n\|\"[^\"]*$n" "$here/tools/package_win.sh" || bad "package_win.sh does not assert $n absent"
done
# the map ships beside the program (crash reports symbolize their stacks from it)
grep -q '\.map' <<<"$zl" || bad "package_win.sh: the zip list lacks the map"
grep -q 'ico_pc.map' "$here/tools/package_linux.sh" || bad "package_linux.sh does not ship the map"
if grep -q 'pkg_assert_tar_lacks "$out" "ico-pc-$label/ico_pc.map"' "$here/tools/package_linux.sh"; then bad "package_linux.sh still asserts the map absent"; fi
# the version information: the label's numbers reach the resource
grep -q 'ICO_VERSION_LABEL' "$here/tools/package_win.sh" || bad "package_win.sh does not pass the label to the build"
grep -q 'VERSIONINFO' "$here/port/platform/win/ico_pc.rc" || bad "ico_pc.rc has no version information"
# an rc build has its own number in the fourth field, the final has 0
cm="$(command -v cmake || true)"
[[ -n "$cm" ]] || cm="$here/tools/toolchain/cmake/bin/cmake"
if [[ -x "$cm" ]]; then
    vout="$("$cm" "-DLABELS=v0.6.2-rc3;v0.6.2;0.6.2;v0.6.2-rc12;dev;" -P "$here/cmake/IcoVersion.cmake" 2>&1)"
    for want in "v0.6.2-rc3 0,6,2,3 0.6.2.3" "v0.6.2 0,6,2,0 0.6.2.0" "0.6.2 0,6,2,0 0.6.2.0" \
        "v0.6.2-rc12 0,6,2,12 0.6.2.12" "dev 0,0,0,0 0.0.0.0"; do
        grep -qxF -- "-- $want" <<<"$vout" || bad "version label: no line '$want' in: $vout"
    done
    ok "an rc label gives its own file version, the final 0"
fi
ok "player archives carry no developer files; the program has version information"
echo "package_textures_test: OK"
