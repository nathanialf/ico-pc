#!/usr/bin/env bash
# tools/package_win.sh <label>
#
# Builds the Windows test package for HEAD: a clean worktree of HEAD under
# build-host/pkg-wt, win-x64 with the window build and
# -DICO_LINK_EXE=ON, staged under dist/stage/x64/ and zipped as
# dist/ico-pc-<label>-win.zip (root dir ico-pc-<label>/). Quiet; the log is
# build-host/pkg-<label>.log. Safe to re-run. Needs no baserom (the binary holds
# no disc data). ICO_PKG_FILES="path ..." overlays working-tree files on HEAD. See docs/port/TESTING.md.
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
    for f in ico_pc.exe ico_pc.map SDL3.dll; do
        [[ -f "build-host/$p/$f" ]] || fail "$p did not produce $f"
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
    cp "$wt/port/input/pad-boot.txt" "$d/pad-script.txt"
    cat > "$d/ico-pc.ini" <<INI
# ico-pc.ini: settings for the $label test build (a window).
# Lines are key=value; lines starting with # or ; are comments.
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

# The trace goes to logs/trace-<date>-<time>.txt; pad-script.txt next to
# the .exe is pressed automatically (it walks through the boot signs to the
# title and starts a New Game).
INI
done
{
    echo "# ICO PC port: test build $label ($date_str, commit ${commit:0:8})"
    echo
    cat <<'TESTMD'
This build opens a window and draws. It runs the game in
real time at 50 frames of game time per second (PAL), as the PS2 does, with
no sound yet. A scripted controller walks through the boot signs to the
title and starts a New Game, so you do not need a controller.

**Textures are real in this build.** The signs, the title logo, the menus,
the subtitles and the small font now draw with their real pictures, decoded
from the disc's texture files as the game loads them. The title screen and
the menus should look right: the same pictures, colours, transparency and
positions as on a PS2, in a 4:3 picture inside the 960 x 720 window (no
stretching). The 3D world is still not drawn: after New Game the screen
stays mostly black apart from the subtitles. If any 2D image still shows as
a rectangle with a coloured checker pattern, that is the old placeholder:
please note where (a screenshot is best) and send the log, whose `tex:`
lines name the texture.

You need your PAL disc image of ICO (SCES-50760, `.iso`). Nothing from the
disc is included here. You also need a graphics driver with Vulkan (any
current NVIDIA, AMD or Intel driver on Windows 10/11 has it).

## Run it

1. Unzip anywhere. There is one folder, `x64`, with the exe, `.map`
   file, `SDL3.dll`, `ico-pc.ini` and `pad-script.txt`. Keep its files
   together.
2. Tell it where the ISO is, as before: set `iso=` in `ico-pc.ini`, or put
   `Ico_PAL.iso` next to the exe, or do nothing and pick it in the file
   dialog the first run opens.
3. Double-click `x64\ico_pc_x64.exe`. After the disc check (a few seconds)
   a window titled **ICO** opens (960 x 720). Within a few seconds you
   should see the boot signs (language, then 50/60 Hz, each fading in,
   getting "pressed" by the script, and fading out), then the title screen
   with the ICO logo and its menu.
4. As soon as the title is up, **take a screenshot** of the window
   (Alt+PrtScn, then paste into Paint and save, or the Snipping Tool). The
   script starts a New Game from the title about 26 seconds after boot (game
   tick 661 on the reference run), so the title is only up for a few
   seconds. One screenshot of a boot sign as well would help. Say whether
   anything looks wrong compared with the PS2: wrong colours, a dark or
   see-through edge around a picture, blurry or blocky text, a picture in
   the wrong place, or anything stretched.
5. Close the window, or press **Escape**. The program ends.
6. Send back the `logs` folder (`x64\logs\`) and the screenshots.

## What the log will say

`logs\ico-pc.log` now also has lines starting `window:` (the window size and
the graphics card it used), `rd:` (the renderer), `gif:` (2D drawing the
port does not handle yet; each kind is listed once) and `tex:` (a texture
the port could not decode, or an image the game reads from a place that is
not a texture yet; each listed once). Then, as before:

- **It worked:** the last lines say `exit: the window was closed`.
- **No window, a message box about Vulkan:** the log's `rhi_vk:` lines say
  why (no Vulkan driver, or the card lacks a feature). Send the log.
- **It crashed:** a block starting `CRASH:` names the error and where, and
  a message box appears. Send the log; the `.map` files let me find the
  place.
- **It froze:** after 30 seconds without progress, a block starting
  `WATCHDOG:` says where it is stuck.

## Files

| file | what |
| --- | --- |
| `ico_pc_x64.exe` | the game with a window; they need `SDL3.dll` beside them and Windows' Vulkan driver |
| `SDL3.dll` | the window and input library (SDL 3, zlib licence) |
| `ico_pc_x64.map` | link maps for turning crash addresses into function names |
| `ico-pc.ini` | `iso=` (disc image path), `watchdog=30`; no `ticks=`, so it runs until you close it |
| `pad-script.txt` | the button presses, one line per change: `<tick> <buttons-hex>` |
| `logs\ico-pc.log` | written by each run (replaced on the next run) |
| `logs\trace-*.txt` | one line per game tick, as in the earlier tests |
TESTMD
} > "$stage/TEST.md"

# zip, root dir ico-pc-<label>/
rm -f "$zip"
pkgroot="$root/build-host/tmp/zip-$label"
rm -rf "$pkgroot"; mkdir -p "$pkgroot"
mkdir -p "$pkgroot/ico-pc-$label"
cp -a "$stage/x64" "$stage/TEST.md" "$pkgroot/ico-pc-$label/"   # not linux/ or a retired x86/
rm -rf "$pkgroot"/ico-pc-"$label"/*/logs
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
