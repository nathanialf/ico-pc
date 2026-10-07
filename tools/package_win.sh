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

label="${1:-}"
if [[ ! "$label" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "usage: tools/package_win.sh <label>" >&2
    exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
mkdir -p build-host/tmp dist
export TMPDIR="$root/build-host/tmp"   # system /tmp is nearly full
log="$root/build-host/pkg-$label.log"
wt="$root/build-host/pkg-wt"
stage="$root/dist/stage"
zip="$root/dist/ico-pc-$label-win.zip"
: > "$log"

fail() { echo "package_win: FAILED: $1 (see $log)" >&2; tail -n 30 "$log" >&2; exit 1; }
run() { "$@" >>"$log" 2>&1 || fail "$*"; }
cleanup() {
    git -C "$root" worktree remove --force "$wt" >>"$log" 2>&1 || rm -rf "$wt"
    git -C "$root" worktree prune >>"$log" 2>&1 || true
}
trap cleanup EXIT

commit="$(git rev-parse HEAD)"
echo "package_win: $label from $commit" >>"$log"

# clean worktree of HEAD
cleanup
run git worktree add --detach "$wt" "$commit"
ln -s "$root/.venv" "$wt/.venv"
ln -s "$root/tools/toolchain" "$wt/tools/toolchain"
for f in ${ICO_PKG_FILES:-}; do   # working-tree files to try over HEAD (see package_linux.sh)
    [[ -f "$root/$f" ]] || fail "ICO_PKG_FILES: no such file $f"
    mkdir -p "$(dirname "$wt/$f")"
    cp "$root/$f" "$wt/$f"
done

# keep the user's existing iso= line
declare -A iso
for a in x64; do
    iso[$a]=""
    if [[ -f "$stage/$a/ico-pc.ini" ]]; then
        line="$(grep -m1 -E '^iso=' "$stage/$a/ico-pc.ini" | tr -d '\r' || true)"
        iso[$a]="${line#iso=}"
    fi
done

export PATH="$wt/.venv/bin:$PATH"
cd "$wt"
declare -A preset=([x64]=win-x64)
for a in x64; do
    p="${preset[$a]}"
    run cmake --preset "$p" -DICO_LINK_EXE=ON
    run cmake --build "build-host/$p"
    for f in ico_pc.exe ico_pc.map SDL3.dll port/rhi/rhi_d3d12_test.exe port/render/rd_replay_tool.exe port/save/mc_import.exe; do
        [[ -f "build-host/$p/$f" ]] || fail "$p did not produce $f"
    done
    for f in compare_backends.cmd compare_png.ps1; do
        [[ -f "port/rhi/test/$f" ]] || fail "port/rhi/test/$f is missing from HEAD"
    done
done
cd "$root"

# stage
date_str="$(date +%Y-%m-%d)"
for a in x64; do
    p="${preset[$a]}"; b="$wt/build-host/$p"; d="$stage/$a"
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
save folder (README.md, "Importing a PS2 save"). rhi_d3d12_test.exe checks
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
# ico-pc.ini: optional settings, key=value; lines starting with # or ; are
# comments. Everything works without editing this file.
#
# iso: the full path of your PAL disc image (SCES-50760), for example
# iso=C:\\Games\\Ico_PAL.iso . Leave it empty and the program asks for it
# in a file dialog the first time, then saves your choice here. A file
# named Ico_PAL.iso next to the .exe is also found.
iso=${iso[$a]}

# There is no ticks= line: the game runs until you close the window
# (or press Escape).

# watchdog: if no game tick has happened this many seconds after the game
# starts booting, or none for twice as long later, the program writes a
# report of where it is stuck to logs\\ico-pc.log and stops. 0 turns it off.
watchdog=30

# Display settings live in Options > Display and in config.toml in the user folder.
# With a problem report, send logs\\ico-pc.log from next to the program.

# Port settings (display, input bindings, gameplay options) live in
# config.toml in the pref folder; the in-game Options menu edits them.
INI
done
# the player README, from the same commit
cp "$wt/README.md" "$stage/README.md"
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
cp -a "$stage/VERSION.txt" "$pkgroot/ico-pc-$label/VERSION.txt"
for a in x64; do
    mkdir -p "$pkgroot/ico-pc-$label/$a/tools"
    for f in "ico_pc_$a.exe" "ico_pc_$a.map" SDL3.dll ico-pc.ini LICENSE.txt NOTICES.txt \
        tools/rhi_d3d12_test.exe tools/rd_replay_tool.exe tools/mc_import.exe tools/SDL3.dll tools/compare_png.ps1 \
        tools/compare_backends.cmd tools/README.txt; do
        cp -a "$stage/$a/$f" "$pkgroot/ico-pc-$label/$a/$f" || fail "stage: no $a/$f"
    done
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

echo "$zip"
echo "built from $commit"
