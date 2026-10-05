#!/usr/bin/env bash
# tools/package_linux.sh <label>
#
# Builds the Linux package for HEAD: a clean worktree of HEAD under
# build-host/pkg-linux-wt (no baserom, no uncommitted work), preset
# linux-x64 with the window build (-DICO_HEADLESS=OFF -DICO_LINK_EXE=ON),
# staged under dist/stage/linux/ and archived as dist/ico-pc-<label>-linux.tar.gz
# (root dir ico-pc-<label>/). Quiet; the log is build-host/pkg-linux-<label>.log.
# Safe to re-run. Builds only: it never runs the game. See docs/port/TESTING.md
# and docs/port/STEAMDECK.md.
#
# ICO_PKG_FILES="path ..." copies those working-tree files over the HEAD
# worktree before the build, to try a change before it is committed (the
# archive then names HEAD but holds the change; the log lists the files).
#
# SDL3 ships as a shared library (libSDL3.so.0) beside the program, found
# through an $ORIGIN run path, so the archive runs wherever it is unpacked.
# libvulkan and the C library are the host's (docs/port/STEAMDECK.md).
set -euo pipefail

label="${1:-}"
if [[ ! "$label" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "usage: tools/package_linux.sh <label>" >&2
    exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
mkdir -p build-host/tmp dist
export TMPDIR="$root/build-host/tmp"   # system /tmp is nearly full
log="$root/build-host/pkg-linux-$label.log"
wt="$root/build-host/pkg-linux-wt"
stage="$root/dist/stage/linux"
tgz="$root/dist/ico-pc-$label-linux.tar.gz"
cmake="$root/tools/toolchain/cmake/bin/cmake"
[[ -x "$cmake" ]] || cmake="$(command -v cmake || true)"
: > "$log"

fail() { echo "package_linux: FAILED: $1 (see $log)" >&2; tail -n 30 "$log" >&2; exit 1; }
run() { "$@" >>"$log" 2>&1 || fail "$*"; }
cleanup() {
    git -C "$root" worktree remove --force "$wt" >>"$log" 2>&1 || rm -rf "$wt"
    git -C "$root" worktree prune >>"$log" 2>&1 || true
}
trap cleanup EXIT

[[ -n "$cmake" ]] || fail "no cmake: run tools/fetch_toolchain.sh"
[[ -f "$root/tools/toolchain/deps/sdl3/linux-x64/lib/libSDL3.so.0" ]] ||
    fail "no SDL3 for linux-x64: run tools/fetch_toolchain.sh"

commit="$(git rev-parse HEAD)"
echo "package_linux: $label from $commit" >>"$log"

# clean worktree of HEAD; only the untracked toolchain and venv are shared
cleanup
run git worktree add --detach "$wt" "$commit"
ln -s "$root/.venv" "$wt/.venv"
ln -s "$root/tools/toolchain" "$wt/tools/toolchain"

for f in ${ICO_PKG_FILES:-}; do
    [[ -f "$root/$f" ]] || fail "ICO_PKG_FILES: no such file $f"
    mkdir -p "$(dirname "$wt/$f")"
    cp "$root/$f" "$wt/$f"
    echo "package_linux: overlay $f" >>"$log"
done

# keep the user's existing iso= line
iso=""
if [[ -f "$stage/ico-pc.ini" ]]; then
    line="$(grep -m1 -E '^iso=' "$stage/ico-pc.ini" || true)"
    iso="${line#iso=}"
fi

export PATH="$wt/.venv/bin:$PATH"
cd "$wt"
# $ORIGIN run path: libSDL3.so.0 is found beside the program. The build-tree
# run path (the toolchain's absolute lib dir) is left out of the binary.
run "$cmake" --preset linux-x64 -DICO_HEADLESS=OFF -DICO_LINK_EXE=ON \
    -DCMAKE_SKIP_BUILD_RPATH=ON "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,'\$ORIGIN'"
run "$cmake" --build build-host/linux-x64 --target ico_pc
b="$wt/build-host/linux-x64"
[[ -f "$b/ico_pc" ]] || fail "linux-x64 did not produce ico_pc"
cd "$root"

# the binary must need only SDL3 and system libraries, and find SDL3 beside itself
needed="$(readelf -d "$b/ico_pc" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')"
echo "package_linux: NEEDED: $needed" >>"$log"
for n in $needed; do
    case "$n" in
        libSDL3.so.0|libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|ld-linux-x86-64.so.2) ;;
        *) fail "unexpected shared library dependency $n" ;;
    esac
done
readelf -d "$b/ico_pc" | grep -E 'RUNPATH|RPATH' | grep -q '\$ORIGIN' || fail "no \$ORIGIN run path"
if readelf -d "$b/ico_pc" | grep -E 'RUNPATH|RPATH' | grep -q "$root"; then
    fail "the run path names the build tree"
fi

# stage
commit8="${commit:0:8}"
date_str="$(date +%Y-%m-%d)"
mkdir -p "$stage"
rm -f "$stage"/ico_pc "$stage"/ico_pc.map "$stage"/libSDL3.so* "$stage"/README.txt
cp "$b/ico_pc" "$stage/ico_pc"
chmod 755 "$stage/ico_pc"
[[ -f "$b/ico_pc.map" ]] && cp "$b/ico_pc.map" "$stage/ico_pc.map"
sdl="$root/tools/toolchain/deps/sdl3/linux-x64/lib"
cp -L "$sdl/libSDL3.so.0" "$stage/libSDL3.so.0"
chmod 755 "$stage/libSDL3.so.0"
for f in LICENSE; do cp "$wt/$f" "$stage/$f"; done
[[ -f "$wt/docs/port/THIRD_PARTY.md" ]] && cp "$wt/docs/port/THIRD_PARTY.md" "$stage/THIRD_PARTY.md"
cat > "$stage/ico-pc.ini" <<INI
# ico-pc.ini: optional settings, key=value; lines starting with # or ; are
# comments. Everything works without editing this file.
#
# iso: the full path of your PAL disc image (SCES-50760), for example
# iso=/home/deck/Games/Ico_PAL.iso . Leave it empty and the program asks for
# it in a file dialog the first run (it needs zenity), then saves your choice
# here. A file named Ico_PAL.iso next to ico_pc is also found. The image is
# read once, to extract the game's data into ico.o2r in the per-user folder
# (~/.local/share/ico-pc/ico-pc/); later runs do not need it.
iso=$iso

# watchdog: seconds without game progress before the program writes a report
# to logs/ico-pc.log and stops. 0 turns it off.
watchdog=30
INI
cat > "$stage/README.txt" <<TXT
ICO PC port ($label, $date_str, commit $commit8)

Nothing from the game's disc is included. You need your own PAL disc image
of ICO (SCES-50760) as an .iso file, and a Vulkan driver (Mesa RADV on the
Steam Deck, or the NVIDIA / AMD / Intel driver on a desktop).

Run:  ./ico_pc        (no options; or add it to Steam as a non-Steam game)

The first run asks for the .iso (or reads iso= in ico-pc.ini, or finds
Ico_PAL.iso next to ico_pc), checks it, extracts the game data once (about
870 MB, ~/.local/share/ico-pc/ico-pc/ico.o2r) and starts the game.
Settings: ~/.local/share/ico-pc/ico-pc/config.toml. Saves:
~/.local/share/ico-pc/ico-pc/memcard/. Logs: logs/ico-pc.log beside ico_pc.

Keep ico_pc and libSDL3.so.0 together. Unpack where you can write (not
/usr). See docs/port/STEAMDECK.md in the source tree for the details.

ico_pc.map maps crash addresses to function names; send it with logs/.
TXT

# archive, root dir ico-pc-<label>/, files owned by root, names sorted
rm -f "$tgz"
pkgroot="$root/build-host/tmp/tar-$label"
rm -rf "$pkgroot"; mkdir -p "$pkgroot"
cp -a "$stage" "$pkgroot/ico-pc-$label"
rm -rf "$pkgroot/ico-pc-$label/logs"
tar -C "$pkgroot" --sort=name --owner=0 --group=0 --numeric-owner \
    -czf "$tgz" "ico-pc-$label" >>"$log" 2>&1 || fail "tar"
rm -rf "$pkgroot"

echo "$tgz"
echo "built from $commit"
