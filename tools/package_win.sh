#!/usr/bin/env bash
# tools/package_win.sh <label>
#
# Builds the Windows package for HEAD: a clean worktree of HEAD under
# build-host/pkg-wt, win-x64 with the window build and
# -DICO_LINK_EXE=ON, staged under dist/stage/x64/ and zipped as
# dist/ico-pc-<label>-win.zip (root dir ico-pc-<label>/, with VERSION.txt). Quiet; the log is
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
run cmake --preset "$p" -DICO_LINK_EXE=ON
run cmake --build "build-host/$p"
for f in ico_pc.exe ico_pc.map SDL3.dll port/rhi/rhi_d3d12_test.exe port/render/rd_replay_tool.exe port/save/mc_import.exe; do
    [[ -f "build-host/$p/$f" ]] || fail "$p did not produce $f"
done
for f in compare_backends.cmd compare_png.ps1; do
    [[ -f "port/rhi/test/$f" ]] || fail "port/rhi/test/$f is missing from HEAD"
done
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
cp "$b/ico_pc.map" "$d/ico_pc_$a.map"
cp "$b/SDL3.dll" "$d/SDL3.dll"
# the Direct3D 12 backend checks, in their own folder with the SDL3.dll they need beside them
rm -rf "$d/tools"; mkdir -p "$d/tools"
cp "$b/port/rhi/rhi_d3d12_test.exe" "$b/port/render/rd_replay_tool.exe" "$b/port/save/mc_import.exe" "$b/SDL3.dll" "$d/tools/"
cp "$wt/port/rhi/test/compare_png.ps1" "$d/tools/"
cat > "$d/tools/README.txt" <<'TXT'
Developer tools; playing the game needs none of them. mc_import.exe copies
ICO's save out of a PS2 memory card image or save archive into the game's
save folder (docs\PORTABLE_MODE.md, "Bring a PS2 save over"). rhi_d3d12_test.exe checks
the Direct3D 12 renderer against exact expected pixels and ends with a
message box giving the verdict (log: rhi_d3d12_test.log). rd_replay_tool.exe
renders a frame dump (F12 in the game writes one to dumps\) to a PNG, and
compare_backends.cmd, with compare_png.ps1, renders every dump in dumps\ on
Vulkan and on Direct3D 12 and compares the pictures (log:
compare_backends.log). SDL3.dll is the library these tools need beside them.
TXT
sed -i 's/$/\r/' "$d/tools/README.txt"
# the repository stores the .cmd with LF (it has no labels or goto, so
# cmd.exe runs it either way); the package gets CRLF, what Notepad and
# cmd.exe expect of a batch file
sed 's/$/\r/' "$wt/port/rhi/test/compare_backends.cmd" > "$d/tools/compare_backends.cmd"
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
cat > "$d/ico-pc.ini" <<INI
# ico-pc.ini: optional settings, one key=value per line; lines starting
# with # or ; are notes. Everything works without editing this file.
#
# iso: the full path of your disc image of the PAL release (SCES-50760), a
# .iso or a .chd, for example iso=C:\\Games\\Ico_PAL.iso . A file named
# Ico_PAL.iso or Ico_PAL.chd next to the program is used before this line.
# Leave it empty and the first start asks for the image, then saves your
# choice here. The image is read once, to copy the game's data into your
# user folder (%APPDATA%\\ico-pc\\ico-pc); later starts do not need it.
iso=$iso

# watchdog: if the game has not started this many seconds after launch, or
# stops responding for twice as long later, the program writes what it was
# doing to logs\\ico-pc.log and closes. 0 turns it off.
watchdog=30

# portable: remove the # in front of the next line to keep your saves,
# settings and the game's data in a folder named userdata next to the
# program, instead of in your user folder. Making an empty userdata folder
# next to the program does the same, and portable=0 turns it off even when
# that folder exists. To move an existing install, close the game and copy
# everything from your user folder into userdata first.
# portable=1

# texture packs: see textures\\SCES-50760\\replacements\\README.txt next to
# the program.
#
# Everything else (display, sound, controls, gameplay, texture packs) is in
# the in-game Options menu, which saves it to config.toml in your user
# folder. With a problem report, send logs\\ico-pc.log from next to the
# program.
INI
# the player README and the guides it links, from the same commit
cp "$wt/README.md" "$stage/README.md"
pkg_stage_docs "$wt" "$stage" || fail "stage: a player guide is missing"
printf 'ico-pc %s\r\nbuilt %s\r\ncommit %s\r\n' "$label" "$date_str" "$commit" > "$stage/VERSION.txt"

# zip, root dir ico-pc-<label>/. The files staged above and nothing else
# (not linux/ or a retired x86/): the stage folder is also where the build
# is tried, so it can hold Ico_PAL.iso (found beside the exe), dumps\
# (frames from the disc) and logs\, none of which may be shipped.
rm -f "$zip"
pkgroot="$root/build-host/tmp/zip-$label"
rm -rf "$pkgroot"
mkdir -p "$pkgroot/ico-pc-$label"
cp -a "$stage/README.md" "$pkgroot/ico-pc-$label/README.md"
cp -a "$stage/docs" "$pkgroot/ico-pc-$label/docs"
cp -a "$stage/VERSION.txt" "$pkgroot/ico-pc-$label/VERSION.txt"
mkdir -p "$pkgroot/ico-pc-$label/$a/tools" "$pkgroot/ico-pc-$label/$a/$pkg_textures_rel" "$pkgroot/ico-pc-$label/$a/$pkg_models_rel" "$pkgroot/ico-pc-$label/$a/$pkg_reshade_rel"
for f in "ico_pc_$a.exe" "ico_pc_$a.map" SDL3.dll ico-pc.ini LICENSE.txt NOTICES.txt \
    tools/rhi_d3d12_test.exe tools/rd_replay_tool.exe tools/mc_import.exe tools/SDL3.dll tools/compare_png.ps1 \
    tools/compare_backends.cmd tools/README.txt "$pkg_textures_rel/README.txt" "$pkg_models_rel/README.txt" "$pkg_reshade_rel/README.txt"; do
    cp -a "$stage/$a/$f" "$pkgroot/ico-pc-$label/$a/$f" || fail "stage: no $a/$f"
done
"$root/.venv/bin/python" - "$pkgroot" "ico-pc-$label" "$zip" >>"$log" 2>&1 <<'PY' || fail "zip"
import os, sys, zipfile
base, top, out = sys.argv[1:4]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for dp, _, fs in os.walk(os.path.join(base, top)):
        for f in sorted(fs):
            p = os.path.join(dp, f)
            z.write(p, os.path.relpath(p, base))
PY
rm -rf "$pkgroot"
for n in "${pkg_player_docs[@]}"; do
    pkg_assert_zip_has "$zip" "ico-pc-$label/docs/$n.md" || fail "the zip lacks docs/$n.md"
done
for r in "${pkg_note_rels[@]}"; do
    pkg_assert_zip_has "$zip" "ico-pc-$label/$a/$r/README.txt" || fail "the zip lacks $a/$r/README.txt"
done

echo "$zip"
echo "built from $commit"
