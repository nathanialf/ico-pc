#!/usr/bin/env bash
# tools/package_linux.sh <label>
#
# Builds the Linux package for HEAD: a clean worktree of HEAD under
# build-host/pkg-linux-wt (no baserom, no uncommitted work), preset
# linux-x64 with the window build (-DICO_HEADLESS=OFF -DICO_LINK_EXE=ON),
# staged under dist/stage/linux/ and archived as dist/ico-pc-<label>-linux.tar.gz
# (root dir ico-pc-<label>/), with tools/mc_import, README.md, the player guides in docs/ and VERSION.txt. Quiet; the log is build-host/pkg-linux-<label>.log.
# Safe to re-run. Builds only: it never runs the game.
#
# ICO_PKG_FILES="path ..." copies those working-tree files over the HEAD
# worktree before the build, to try a change before it is committed (the
# archive then names HEAD but holds the change; the log lists the files).
#
# SDL3 ships as a shared library (libSDL3.so.0) beside the program, found
# through an $ORIGIN run path, so the archive runs wherever it is unpacked.
# libvulkan and the C library are the host's.
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
run "$cmake" --build build-host/linux-x64 --target ico_pc mc_import
b="$wt/build-host/linux-x64"
[[ -f "$b/ico_pc" ]] || fail "linux-x64 did not produce ico_pc"
[[ -f "$b/port/save/mc_import" ]] || fail "linux-x64 did not produce mc_import"
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

# the textures folder's note and the archive checks
. "$wt/tools/package_textures_lib.sh"
# the player guides (docs/*.md)
. "$wt/tools/package_docs_lib.sh"

# stage
commit8="${commit:0:8}"
date_str="$(date +%Y-%m-%d)"
mkdir -p "$stage"
rm -f "$stage"/ico_pc "$stage"/ico_pc.map "$stage"/libSDL3.so* "$stage"/README.txt "$stage"/README.md
rm -rf "$stage/tools" "$stage/textures"
cp "$b/ico_pc" "$stage/ico_pc"
chmod 755 "$stage/ico_pc"
# the save importer (C and the C library only), as the Windows package's
# tools\mc_import.exe
mkdir -p "$stage/tools"
cp "$b/port/save/mc_import" "$stage/tools/mc_import"
chmod 755 "$stage/tools/mc_import"
[[ -f "$b/ico_pc.map" ]] && cp "$b/ico_pc.map" "$stage/ico_pc.map"
sdl="$root/tools/toolchain/deps/sdl3/linux-x64/lib"
cp -L "$sdl/libSDL3.so.0" "$stage/libSDL3.so.0"
chmod 755 "$stage/libSDL3.so.0"
# licences: the program's (LICENSE, MIT) and every third-party component's
# notice (NOTICES.txt, tools/gen_notices.py from tools/notices/manifest.json);
# THIRD_PARTY.md says where each comes from
rm -f "$stage/NOTICES.txt"
cp "$wt/LICENSE" "$stage/LICENSE"
run python3 "$wt/tools/gen_notices.py" --platform linux --root "$wt" --out "$stage/NOTICES.txt"
cp "$wt/docs/THIRD_PARTY.md" "$stage/THIRD_PARTY.md"
# the textures folder, with a note in it
pkg_stage_textures_readme "$stage" lf
cat > "$stage/ico-pc.ini" <<INI
# ico-pc.ini: optional settings, one key=value per line; lines starting
# with # or ; are notes. Everything works without editing this file.
#
# iso: the full path of your disc image of the PAL release (SCES-50760), a
# .iso or a .chd, for example iso=/home/deck/Games/Ico_PAL.iso . A file
# named Ico_PAL.iso or Ico_PAL.chd next to ico_pc is used before this line.
# Leave it empty and the first start asks for the image (the file dialog
# needs zenity), then saves your choice here. The image is read once, to
# copy the game's data into your user folder (~/.local/share/ico-pc/ico-pc/);
# later starts do not need it.
iso=$iso

# watchdog: if the game has not started this many seconds after launch, or
# stops responding for twice as long later, the program writes what it was
# doing to logs/ico-pc.log and closes. 0 turns it off.
watchdog=30

# portable: remove the # in front of the next line to keep your saves,
# settings and the game's data in a folder named userdata next to the
# program, instead of in your user folder. Making an empty userdata folder
# next to the program does the same, and portable=0 turns it off even when
# that folder exists. To move an existing install, close the game and copy
# everything from your user folder into userdata first.
# portable=1

# texture packs: see textures/SCES-50760/replacements/README.txt next to
# ico_pc.
#
# Everything else (display, sound, controls, gameplay, texture packs) is in
# the in-game Options menu, which saves it to config.toml in your user
# folder. With a problem report, send logs/ico-pc.log from next to ico_pc.
INI
# the player README and the guides it links, from the same commit
cp "$wt/README.md" "$stage/README.md"
pkg_stage_docs "$wt" "$stage" || fail "stage: a player guide is missing"
printf 'ico-pc %s\nbuilt %s\ncommit %s\n' "$label" "$date_str" "$commit" > "$stage/VERSION.txt"

# archive, root dir ico-pc-<label>/, files owned by root, names sorted.
# The files staged above and nothing else: the stage folder is also where
# the build is tried, so it can hold Ico_PAL.iso (found beside ico_pc),
# dumps/ (frames from the disc) and logs/, none of which may be shipped.
rm -f "$tgz"
pkgroot="$root/build-host/tmp/tar-$label"
rm -rf "$pkgroot"; mkdir -p "$pkgroot/ico-pc-$label"
mkdir -p "$pkgroot/ico-pc-$label/tools" "$pkgroot/ico-pc-$label/$pkg_textures_rel"
for f in ico_pc libSDL3.so.0 LICENSE NOTICES.txt THIRD_PARTY.md ico-pc.ini README.md VERSION.txt tools/mc_import "$pkg_textures_rel/README.txt"; do
    cp -a "$stage/$f" "$pkgroot/ico-pc-$label/$f" || fail "stage: no $f"
done
cp -a "$stage/docs" "$pkgroot/ico-pc-$label/docs"
if [[ -f "$stage/ico_pc.map" ]]; then
    cp -a "$stage/ico_pc.map" "$pkgroot/ico-pc-$label/ico_pc.map"
fi
tar -C "$pkgroot" --sort=name --owner=0 --group=0 --numeric-owner \
    -czf "$tgz" "ico-pc-$label" >>"$log" 2>&1 || fail "tar"
rm -rf "$pkgroot"
for n in "${pkg_player_docs[@]}"; do
    pkg_assert_tar_has "$tgz" "ico-pc-$label/docs/$n.md" || fail "the archive lacks docs/$n.md"
done
pkg_assert_tar_has "$tgz" "ico-pc-$label/$pkg_textures_rel/README.txt" \
    || fail "the archive lacks $pkg_textures_rel/README.txt"

echo "$tgz"
echo "built from $commit"
