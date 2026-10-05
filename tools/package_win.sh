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
    for f in ico_pc.exe ico_pc.map SDL3.dll port/rhi/rhi_d3d12_test.exe port/render/rd_replay_tool.exe; do
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
    # the R6c backend checks (docs/port/TESTING.md, "Renderer wave 6: D3D12"),
    # in their own folder with the SDL3.dll they need beside them
    rm -rf "$d/tools"; mkdir -p "$d/tools"
    cp "$b/port/rhi/rhi_d3d12_test.exe" "$b/port/render/rd_replay_tool.exe" "$b/SDL3.dll" "$d/tools/"
    cp "$wt/port/rhi/test/compare_png.ps1" "$d/tools/"
    # the repository stores the .cmd with LF (it has no labels or goto, so
    # cmd.exe runs it either way); the package gets CRLF, what Notepad and
    # cmd.exe expect of a batch file
    sed 's/$/\r/' "$wt/port/rhi/test/compare_backends.cmd" > "$d/tools/compare_backends.cmd"
    # no pad-script.txt: this is a playable build; the scripted pad is a
    # developer tool (port/input/pad-boot.txt) and would play the game by itself
    rm -f "$d/pad-script.txt"
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

# The trace goes to logs/trace-<date>-<time>.txt. Port settings (display,
# input bindings, gameplay options) live in config.toml in the pref folder;
# the in-game Settings menu edits them.
INI
done
{
    echo "# ICO PC port: test build $label ($date_str, commit ${commit:0:8})"
    echo
    cat <<'TESTMD'
This is a playable build. It opens a window and runs the game in real
time at 50 frames of game time per second (PAL), as the PS2 does, with
sound, a controller or the keyboard and mouse, saves, the movies and the
new Settings menu. Nothing from the disc is included: the first run asks
for your PAL disc image of ICO (SCES-50760, `.iso`), checks it and
extracts it once (about 15 seconds) into `ico.o2r` in
`%APPDATA%\ico-pc\ico-pc\`, where the config, saves and achievements
also live. You need a graphics driver with Vulkan (any current NVIDIA, AMD
or Intel driver on Windows 10/11 has it).

## Run it

1. Unzip anywhere. There is one folder, `x64`, with the exe, `.map`
   file, `SDL3.dll` and `ico-pc.ini`. Keep its files together.
2. Double-click `x64\ico_pc_x64.exe`. The first run opens a file dialog
   for the ISO (or set `iso=` in `ico-pc.ini`, or put `Ico_PAL.iso` next to
   the exe), shows a small progress window while it extracts, then opens
   the game window (960 x 720, 4:3).
3. Play. Gamepad: any pad SDL recognises, PS2 buttons by position (South =
   Cross). Keyboard: WASD move (Shift walks), Space = Cross, E = Circle,
   Q = Square, R = Triangle, Enter = Start, Backspace = Select, arrows =
   D-pad, IJKL = right stick, the mouse pans the camera. Alt+Enter toggles
   fullscreen; Escape quits.
4. **Settings** is a row on the title screen and on the pause menu's
   Options page: display preset (Original = PS2-exact; Enhanced unlocks
   resolution, aspect, filtering, full height, frame rate), audio volume,
   controls (remap, stick fix, mouse sensitivity), gameplay (the Yorda
   option), language, achievements, developer mode. Mirror mode is asked
   at New Game.
5. When something looks or sounds wrong compared with the PS2, note where
   (a screenshot is best) and keep the `logs` folder (`x64\logs\`).

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
| `tools\rhi_d3d12_test.exe` | optional: the Direct3D 12 backend's own tests (below) |
| `tools\rd_replay_tool.exe`, `tools\compare_backends.cmd`, `tools\compare_png.ps1` | optional: render frame dumps on Vulkan and D3D12 and compare them (below) |
| `tools\SDL3.dll` | a copy for the two `.exe` files in `tools\` |
| `logs\ico-pc.log` | written by each run (replaced on the next run) |
| `logs\trace-*.txt` | one line per game tick (a developer record) |

## Backend checks (optional, `x64\tools\`)

The Direct3D 12 backend has not run on Windows yet; these check it. Each
step is independent of the game and needs no disc image.

1. Double-click `tools\rhi_d3d12_test.exe`. It runs the renderer's pixel
   cells on D3D12 (the WARP software adapter, then the GPU), a swapchain on
   a hidden window, and Vulkan for comparison, writes
   `rhi_d3d12_test.log` beside itself and ends with a message box (PASSED, or
   which cell failed). Send back the log. "skipped" lines are not failures.
2. To use D3D12 in the game, set `backend=d3d12` in `ico-pc.ini` (the
   default and the fallback are Vulkan); `logs\ico-pc.log` then says
   `D3D12 on <adapter>`.
3. To compare the two backends on the same frames: add `dump_every=N`
   (say 100) to `ico-pc.ini`, run the game, close it, and copy the `dumps\`
   folder it made into `tools\`. Double-click `tools\compare_backends.cmd`:
   it renders each dump on both backends with `rd_replay_tool.exe` and
   compares the PNGs with `compare_png.ps1` (PowerShell), writes
   `compare_backends.log` (opened in Notepad) and the images in
   `compare_out\`. Send back the log. Dumps hold pictures from your disc:
   do not share them.
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
