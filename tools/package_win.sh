#!/usr/bin/env bash
# tools/package_win.sh <label>
#
# Builds the Windows package for HEAD: a clean worktree of HEAD under
# build-host/pkg-wt, win-x64 with the window build and
# -DICO_LINK_EXE=ON, staged under dist/stage/x64/ and zipped as
# dist/ico-pc-<label>-win.zip (root dir ico-pc-<label>/, with VERSION.txt)
# and, from the same stage, dist/ico-pc-<label>-win-portable.zip (portable=1
# in ico-pc.ini, no make_portable.cmd). Quiet; the log is
# build-host/pkg-<label>.log. Safe to re-run. Needs no baserom (the binary holds
# no disc data). ICO_PKG_FILES="path ..." overlays working-tree files on HEAD.
set -euo pipefail

pkg_name=package_win
# shellcheck source=package_common_lib.sh
. "$(dirname "${BASH_SOURCE[0]}")/package_common_lib.sh"
pkg_begin "${1:-}"
log="$root/build-host/pkg-$label.log"
wt="$root/build-host/pkg-wt"
stage="$root/dist/stage"
zip="$root/dist/ico-pc-$label-win.zip"
zipp="$root/dist/ico-pc-$label-win-portable.zip"
: > "$log"

trap cleanup EXIT

commit="$(git rev-parse HEAD)"
echo "package_win: $label from $commit" >>"$log"

# clean worktree of HEAD, with the ICO_PKG_FILES overlay
pkg_make_worktree "$commit"

# the one architecture (its stage folder and program name) and its preset
a=x64
p=win-x64

# keep the user's existing iso= line
iso=""
if [[ -f "$stage/$a/ico-pc.ini" ]]; then
    line="$(grep -m1 -E '^iso=' "$stage/$a/ico-pc.ini" | tr -d '\r' || true)"
    iso="${line#iso=}"
fi

export PATH="$wt/.venv/bin:$PATH"
cd "$wt"
# ICO_VERSION_LABEL: the program's version information (Details tab)
run cmake --preset "$p" -DICO_LINK_EXE=ON "-DICO_VERSION_LABEL=$label"
run cmake --build "build-host/$p" --target ico_pc mc_import
for f in ico_pc.exe ico_pc.map SDL3.dll port/save/mc_import.exe; do
    [[ -f "build-host/$p/$f" ]] || fail "$p did not produce $f"
done
[[ -f tools/portable/make_portable.cmd ]] || fail "tools/portable/make_portable.cmd is missing from HEAD"
cd "$root"

# the textures folder's note and the archive checks
. "$wt/tools/package_textures_lib.sh"
# the player guides (docs/*.md)
. "$wt/tools/package_docs_lib.sh"

# stage
date_str="$(date +%Y-%m-%d)"
b="$wt/build-host/$p"; d="$stage/$a"
mkdir -p "$d"
rm -f "$d"/ico_pc*.exe "$d"/ico_pc*.map
cp "$b/ico_pc.exe" "$d/ico_pc_$a.exe"
# the link map ships beside the program in both zips: a crash or watchdog
# report names its functions from it on the player's machine
# (port/platform/symmap.h; the name is the program's with .map)
cp "$b/ico_pc.map" "$d/ico_pc_$a.map"
cp "$b/SDL3.dll" "$d/SDL3.dll"
# the save importer, in its own folder with the SDL3.dll it needs beside it
rm -rf "$d/tools"; mkdir -p "$d/tools"
cp "$b/port/save/mc_import.exe" "$b/SDL3.dll" "$d/tools/"
cat > "$d/tools/README.txt" <<'TXT'
mc_import.exe copies ICO's save (of the European or the Japanese disc) out
of a PS2 memory card file or save archive into the game's save folder.
Playing the game does not need it.
To run it, open a Command Prompt in this folder and give it the file.
Add --to with the folder to copy into, then the file's name, for example:
  mc_import.exe --to "%APPDATA%\ico-pc\ico-pc\memcard" Mcd001.ps2
The steps are in docs\PORTABLE_MODE.md, "Bring a PS2 save over".
SDL3.dll is a library mc_import.exe needs beside it.
TXT
sed -i 's/$/\r/' "$d/tools/README.txt"
# the make-portable script (stored with LF, CRLF in the package)
sed 's/$/\r/' "$wt/tools/portable/make_portable.cmd" > "$d/make_portable.cmd"
# the textures folder, with a note in it (the zip keeps files, so the
# note is what keeps the folder)
rm -rf "$d/textures" "$d/models" "$d/reshade"
pkg_stage_folder_notes "$d" crlf
# licences: the program's (LICENSE, MIT) and every third-party
# component's notice (NOTICES.txt, tools/gen_notices.py from
# tools/notices/manifest.json; docs/THIRD_PARTY.md)
cp "$wt/LICENSE" "$d/LICENSE.txt"
run "$root/.venv/bin/python" "$wt/tools/gen_notices.py" --platform windows \
    --root "$wt" --out "$d/NOTICES.txt"
# no pad-script.txt: this is a playable build; the scripted pad is a
# developer tool (port/input/pad-boot.txt) and would play the game by itself
rm -f "$d/pad-script.txt"
pkg_write_ini "$d/ico-pc.ini" windows "$iso"
# the player README and the guides it links, from the same commit
cp "$wt/README.md" "$stage/README.md"
pkg_stage_docs "$wt" "$stage" || fail "stage: a player guide is missing"
printf 'ico-pc %s\r\nbuilt %s\r\ncommit %s\r\n' "$label" "$date_str" "$commit" > "$stage/VERSION.txt"

# zip, root dir ico-pc-<label>/. The files staged above and nothing else:
# the stage folder is also where the build
# is tried, so it can hold Ico_PAL.iso or Ico_JPN.iso (found beside the exe), dumps\
# (frames from the disc) and logs\, none of which may be shipped.
# Two archives from the one stage: the normal one (with make_portable.cmd)
# and the portable one (ico-pc.ini with portable=1, a paragraph in the README,
# no make_portable.cmd).
for variant in normal portable; do
    if [[ "$variant" == portable ]]; then out="$zipp"; else out="$zip"; fi
    rm -f "$out"
    pkgroot="$root/build-host/tmp/zip-$label-$variant"
    rm -rf "$pkgroot"
    top="$pkgroot/ico-pc-$label"
    mkdir -p "$top/$a/tools"
    for r in "${pkg_note_rels[@]}"; do mkdir -p "$top/$a/$r"; done
    cp -a "$stage/README.md" "$top/README.md"
    cp -a "$stage/docs" "$top/docs"
    cp -a "$stage/VERSION.txt" "$top/VERSION.txt"
    files=("ico_pc_$a.exe" "ico_pc_$a.map" SDL3.dll ico-pc.ini LICENSE.txt NOTICES.txt
        tools/mc_import.exe tools/SDL3.dll tools/README.txt)
    for r in "${pkg_note_rels[@]}"; do files+=("$r/README.txt"); done
    [[ "$variant" == portable ]] || files+=(make_portable.cmd)
    for f in "${files[@]}"; do
        cp -a "$stage/$a/$f" "$top/$a/$f" || fail "stage: no $a/$f"
    done
    if [[ "$variant" == portable ]]; then
        pkg_write_ini "$top/$a/ico-pc.ini" windows "$iso" portable
        pkg_portable_readme_note "$top/README.md"
    fi
    pkg_zip_dir "$pkgroot" "ico-pc-$label" "$out" >>"$log" 2>&1 || fail "zip $variant"
    rm -rf "$pkgroot"
    for n in "${pkg_player_docs[@]}"; do
        pkg_assert_zip_has "$out" "ico-pc-$label/docs/$n.md" || fail "the $variant zip lacks docs/$n.md"
    done
    for r in "${pkg_note_rels[@]}"; do
        pkg_assert_zip_has "$out" "ico-pc-$label/$a/$r/README.txt" || fail "the $variant zip lacks $a/$r/README.txt"
    done
    # developer files stay out of the player zips
    pkg_assert_zip_has "$out" "ico-pc-$label/$a/ico_pc_$a.map" || fail "the $variant zip lacks $a/ico_pc_$a.map"
    for n in "$a/tools/rhi_d3d12_test.exe" "$a/tools/rd_replay_tool.exe" \
        "$a/tools/compare_backends.cmd" "$a/tools/compare_png.ps1"; do
        pkg_assert_zip_lacks "$out" "ico-pc-$label/$n" || fail "the $variant zip has $n"
    done
    if [[ "$variant" == portable ]]; then
        pkg_assert_zip_lacks "$out" "ico-pc-$label/$a/make_portable.cmd" || fail "the portable zip has make_portable.cmd"
        pkg_assert_zip_line "$out" "ico-pc-$label/$a/ico-pc.ini" '^portable=1$' || fail "the portable zip's ico-pc.ini lacks portable=1"
    else
        pkg_assert_zip_has "$out" "ico-pc-$label/$a/make_portable.cmd" || fail "the zip lacks $a/make_portable.cmd"
        pkg_assert_zip_line "$out" "ico-pc-$label/$a/ico-pc.ini" '^portable=1$' && fail "the normal zip's ico-pc.ini has portable=1"
        pkg_assert_zip_line "$out" "ico-pc-$label/$a/make_portable.cmd" '\r$' || fail "make_portable.cmd is not CRLF"
    fi
done

echo "$zip"
echo "$zipp"
echo "built from $commit"
