#!/usr/bin/env bash
# tools/package_linux.sh <label>
#
# Builds the Linux package for HEAD: a clean worktree of HEAD under
# build-host/pkg-linux-wt (no baserom, no uncommitted work), preset
# linux-x64 with the window build (-DICO_HEADLESS=OFF -DICO_LINK_EXE=ON),
# staged under dist/stage/linux/ and archived as dist/ico-pc-<label>-linux.tar.gz
# (root dir ico-pc-<label>/) and, from the same stage, as
# dist/ico-pc-<label>-linux-portable.tar.gz (portable=1 in ico-pc.ini, no
# make_portable.sh), with tools/mc_import, README.md, the player guides in docs/ and VERSION.txt. Quiet; the log is build-host/pkg-linux-<label>.log.
# Safe to re-run. Builds only: it never runs the game.
#
# ICO_PKG_FILES="path ..." copies those working-tree files over the HEAD
# worktree before the build, to try a change before it is committed (the
# archive then names HEAD but holds the change; the log lists the files).
#
# SDL3 ships as a shared library (libSDL3.so.0) beside the program, found
# through an $ORIGIN run path, so the archive runs wherever it is unpacked.
# libvulkan and the C library are the host's.
#
# The package is for the build host's architecture. On an arm64 (aarch64)
# host the preset is linux-arm64 (the window build is its default), and the
# names take -arm64: dist/ico-pc-<label>-linux-arm64.tar.gz, staged under
# dist/stage/linux-arm64/, logged to build-host/pkg-linux-arm64-<label>.log.
# The x86-64 names are as above; the portable archive is
# dist/ico-pc-<label>-linux-arm64-portable.tar.gz there.
#
# Last, the AppImage from the same stage: dist/ico-pc-<label>-linux-x86_64.AppImage
# (dist/ico-pc-<label>-linux-aarch64.AppImage on arm64) with its .sha256,
# made with the AppImage tools tools/fetch_deps.sh unpacks
# (tools/toolchain/deps/appimage/<arch>/) and checked by unpacking it again.
set -euo pipefail

pkg_name=package_linux
# shellcheck source=package_common_lib.sh
. "$(dirname "${BASH_SOURCE[0]}")/package_common_lib.sh"
pkg_begin "${1:-}"
case "$(uname -m)" in
    x86_64) arch=x64 suffix=linux preset=linux-x64 ldso=ld-linux-x86-64.so.2 ;;
    aarch64 | arm64) arch=arm64 suffix=linux-arm64 preset=linux-arm64 ldso=ld-linux-aarch64.so.1 ;;
    *)
        echo "package_linux: no Linux package for $(uname -m) hosts" >&2
        exit 1
        ;;
esac
log="$root/build-host/pkg-$suffix-$label.log"
wt="$root/build-host/pkg-$suffix-wt"
stage="$root/dist/stage/$suffix"
tgz="$root/dist/ico-pc-$label-$suffix.tar.gz"
tgzp="$root/dist/ico-pc-$label-$suffix-portable.tar.gz"
cmake="$root/tools/toolchain/cmake/bin/cmake"
[[ -x "$cmake" ]] || cmake="$(command -v cmake || true)"
: > "$log"

trap cleanup EXIT

[[ -n "$cmake" ]] || fail "no cmake: run tools/fetch_toolchain.sh"
[[ -f "$root/tools/toolchain/deps/sdl3/linux-$arch/lib/libSDL3.so.0" ]] ||
    fail "no SDL3 for linux-$arch: run tools/fetch_toolchain.sh"

commit="$(git rev-parse HEAD)"
echo "package_linux: $label from $commit" >>"$log"

# clean worktree of HEAD; only the untracked toolchain and venv are shared
pkg_make_worktree "$commit"

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
# ICO_VERSION_LABEL: the version the log and the Options menu show.
# ICO_RC_TOOLS: the rc-only tooling, on for a label ending in -rc<N> alone
# (tools/check_rc_text.py; docs/BUILDING.md, "Release candidate tools")
rc_flag="$(python3 -I "$wt/tools/check_rc_text.py" --cmake-flag "$label")" ||
    fail "tools/check_rc_text.py --cmake-flag"
echo "package_linux: $rc_flag" >>"$log"
run "$cmake" --preset "$preset" -DICO_HEADLESS=OFF -DICO_LINK_EXE=ON "-DICO_VERSION_LABEL=$label" \
    "$rc_flag" -DCMAKE_SKIP_BUILD_RPATH=ON "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,'\$ORIGIN'"
run "$cmake" --build "build-host/$preset" --target ico_pc mc_import
b="$wt/build-host/$preset"
[[ -f "$b/ico_pc" ]] || fail "$preset did not produce ico_pc"
# a release's program holds no rc row, an rc's does (the AppImage holds this one)
run python3 -I "$wt/tools/check_rc_text.py" --label "$label" "$b/ico_pc"
[[ -f "$b/port/save/mc_import" ]] || fail "$preset did not produce mc_import"
cd "$root"

# the binary must need only SDL3 and system libraries, and find SDL3 beside
# itself (checked again on the program the AppImage holds)
check_program_elf() {
    local needed n
    needed="$(readelf -d "$1" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')"
    echo "package_linux: NEEDED ($1): $needed" >>"$log"
    for n in $needed; do
        case "$n" in
            libSDL3.so.0|libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|"$ldso") ;;
            *) fail "unexpected shared library dependency $n in $1" ;;
        esac
    done
    readelf -d "$1" | grep -E 'RUNPATH|RPATH' | grep -q '\$ORIGIN' || fail "no \$ORIGIN run path in $1"
    if readelf -d "$1" | grep -E 'RUNPATH|RPATH' | grep -q "$root"; then
        fail "the run path of $1 names the build tree"
    fi
}
check_program_elf "$b/ico_pc"

# the oldest C library the package runs on: README.md says Ubuntu 24.04 or
# newer (glibc 2.39), so no shipped binary may need a newer symbol version.
# A build host newer than that usually still passes; the log has each
# binary's newest version.
glibc_floor=2.39
for f in "$b/ico_pc" "$b/port/save/mc_import" \
    "$root/tools/toolchain/deps/sdl3/linux-$arch/lib/libSDL3.so.0"; do
    v="$(objdump -T "$f" | sed -n 's/.*GLIBC_\([0-9][0-9.]*\).*/\1/p' | sort -Vu | tail -n 1)" ||
        fail "objdump -T $f"
    echo "package_linux: ${f##*/} needs glibc ${v:-(none)}" >>"$log"
    if [[ -n "$v" && "$(printf '%s\n' "$glibc_floor" "$v" | sort -V | tail -n 1)" != "$glibc_floor" ]]; then
        fail "${f##*/} needs glibc $v, newer than $glibc_floor (Ubuntu 24.04)"
    fi
done

# the textures folder's note and the archive checks
. "$wt/tools/package_textures_lib.sh"
# the player guides (docs/*.md)
. "$wt/tools/package_docs_lib.sh"

# stage
date_str="$(date +%Y-%m-%d)"
mkdir -p "$stage"
rm -f "$stage"/make_portable.sh "$stage"/ico_pc "$stage"/ico_pc.map "$stage"/libSDL3.so* "$stage"/README.txt "$stage"/README.md
rm -rf "$stage/tools" "$stage/textures"
cp "$b/ico_pc" "$stage/ico_pc"
chmod 755 "$stage/ico_pc"
# the save importer (C and the C library only), as the Windows package's
# tools\mc_import.exe
mkdir -p "$stage/tools"
cp "$b/port/save/mc_import" "$stage/tools/mc_import"
chmod 755 "$stage/tools/mc_import"
# the link map ships beside the program in both archives: a crash or
# watchdog report names its functions from it on the player's machine
# (port/platform/symmap.h)
[[ -f "$b/ico_pc.map" ]] || fail "$preset did not produce ico_pc.map"
cp "$b/ico_pc.map" "$stage/ico_pc.map"
sdl="$root/tools/toolchain/deps/sdl3/linux-$arch/lib"
cp -L "$sdl/libSDL3.so.0" "$stage/libSDL3.so.0"
chmod 755 "$stage/libSDL3.so.0"

# the audio backends: SDL3 loads PipeWire, PulseAudio and ALSA by soname at run time, so the names
# sit in the library as strings and the package needs none of the three (NEEDED stays libc/libm).
# A library built without a backend plays through ALSA's default device only, which on a PipeWire
# desktop without the ALSA plugins is not the sink the player listens on (issue 120).
for n in libpipewire-0.3.so.0 libpulse.so.0 libasound.so.2; do
    grep -aqF "$n" "$stage/libSDL3.so.0" ||
        fail "libSDL3.so.0 has no $n backend: run tools/fetch_deps.sh (rebuilds SDL3 with the audio backends)"
done

# licences: the program's (LICENSE, MIT) and every third-party component's
# notice (NOTICES.txt, tools/gen_notices.py from tools/notices/manifest.json);
# THIRD_PARTY.md says where each comes from
rm -f "$stage/NOTICES.txt"
cp "$wt/LICENSE" "$stage/LICENSE"
run python3 "$wt/tools/gen_notices.py" --platform linux --arch "$arch" --root "$wt" \
    --out "$stage/NOTICES.txt"
cp "$wt/docs/THIRD_PARTY.md" "$stage/THIRD_PARTY.md"
# the make-portable script
[[ -f "$wt/tools/portable/make_portable.sh" ]] || fail "tools/portable/make_portable.sh is missing from HEAD"
cp "$wt/tools/portable/make_portable.sh" "$stage/make_portable.sh"
chmod 755 "$stage/make_portable.sh"
# the textures, models and reshade folders, each with a note in it
pkg_stage_folder_notes "$stage" lf
pkg_write_ini "$stage/ico-pc.ini" linux "$iso"
# the player README and the guides it links, from the same commit
cp "$wt/README.md" "$stage/README.md"
pkg_stage_docs "$wt" "$stage" || fail "stage: a player guide is missing"
printf 'ico-pc %s\nbuilt %s\ncommit %s\n' "$label" "$date_str" "$commit" > "$stage/VERSION.txt"

# archive, root dir ico-pc-<label>/, files owned by root, names sorted.
# The files staged above and nothing else: the stage folder is also where
# the build is tried, so it can hold Ico_PAL.iso, Ico_JPN.iso or Ico_KOR.iso (found beside ico_pc),
# dumps/ (frames from the disc) and logs/, none of which may be shipped.
# Two archives from the one stage: the normal one (with make_portable.sh)
# and the portable one (ico-pc.ini with portable=1, a paragraph in the
# README, no make_portable.sh).
for variant in normal portable; do
    if [[ "$variant" == portable ]]; then out="$tgzp"; else out="$tgz"; fi
    rm -f "$out"
    pkgroot="$root/build-host/tmp/tar-$label-$variant"
    rm -rf "$pkgroot"; mkdir -p "$pkgroot/ico-pc-$label"
    top="$pkgroot/ico-pc-$label"
    mkdir -p "$top/tools"
    for r in "${pkg_note_rels[@]}"; do mkdir -p "$top/$r"; done
    files=(ico_pc ico_pc.map libSDL3.so.0 LICENSE NOTICES.txt THIRD_PARTY.md ico-pc.ini README.md VERSION.txt tools/mc_import)
    for r in "${pkg_note_rels[@]}"; do files+=("$r/README.txt"); done
    [[ "$variant" == portable ]] || files+=(make_portable.sh)
    for f in "${files[@]}"; do
        cp -a "$stage/$f" "$top/$f" || fail "stage: no $f"
    done
    cp -a "$stage/docs" "$top/docs"
    if [[ "$variant" == portable ]]; then
        pkg_write_ini "$top/ico-pc.ini" linux "$iso" portable
        pkg_portable_readme_note "$top/README.md"
    fi
    tar -C "$pkgroot" --sort=name --owner=0 --group=0 --numeric-owner \
        -czf "$out" "ico-pc-$label" >>"$log" 2>&1 || fail "tar $variant"
    rm -rf "$pkgroot"
    for n in "${pkg_player_docs[@]}"; do
        pkg_assert_tar_has "$out" "ico-pc-$label/docs/$n.md" || fail "the $variant archive lacks docs/$n.md"
    done
    for r in "${pkg_note_rels[@]}"; do
        pkg_assert_tar_has "$out" "ico-pc-$label/$r/README.txt" || fail "the $variant archive lacks $r/README.txt"
    done
    pkg_assert_tar_has "$out" "ico-pc-$label/ico_pc.map" || fail "the $variant archive lacks ico_pc.map"
    if [[ "$variant" == portable ]]; then
        pkg_assert_tar_lacks "$out" "ico-pc-$label/make_portable.sh" || fail "the portable archive has make_portable.sh"
        pkg_assert_tar_line "$out" "ico-pc-$label/ico-pc.ini" '^portable=1$' || fail "the portable archive's ico-pc.ini lacks portable=1"
    else
        pkg_assert_tar_has "$out" "ico-pc-$label/make_portable.sh" || fail "the archive lacks make_portable.sh"
        pkg_assert_tar_line "$out" "ico-pc-$label/ico-pc.ini" '^portable=1$' && fail "the normal archive's ico-pc.ini has portable=1"
    fi
done

# The AppImage, from the same stage: one file the player marks as a program
# and double-clicks. The runtime (deps/appimage/<arch>/runtime) mounts the
# read-only file system inside it and runs AppRun, a link to usr/bin/ico_pc,
# which finds libSDL3.so.0 beside itself ($ORIGIN) and its link map
# (/proc/self/exe). ico_pc takes the AppImage file's folder as its own
# (host_fs.c ico_appimage_file, host_config.c ico_host_exe_dir), so
# ico-pc.ini, logs/, userdata/, the disc image and the textures, models and
# reshade folders all go beside the file: none of them is inside, nor
# make_portable.sh. The guides, licences and VERSION.txt sit in
# usr/share/doc/ico-pc/. appimagetool runs unpacked (no FUSE needed) with
# its own mksquashfs and the pinned runtime, so nothing is downloaded;
# SOURCE_DATE_EPOCH (the commit's time) stamps the file system. Then the
# file is unpacked again with its own --appimage-extract (no FUSE either)
# and its file list, links and program are checked; the .sha256 is written
# beside it as CI writes the archives'.
case "$arch" in
    x64) appimage_arch=x86_64 ;;
    arm64) appimage_arch=aarch64 ;;
esac
ait="$root/tools/toolchain/deps/appimage/$appimage_arch"
appimage="$root/dist/ico-pc-$label-linux-$appimage_arch.AppImage"
[[ -x "$ait/squashfs-root/AppRun" && -f "$ait/runtime" ]] ||
    fail "no AppImage tools for $appimage_arch: run tools/fetch_deps.sh"
appdir="$root/build-host/tmp/appdir-$label"
doc="$appdir/usr/share/doc/ico-pc"
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$doc"
for f in ico_pc ico_pc.map libSDL3.so.0; do
    cp -a "$stage/$f" "$appdir/usr/bin/$f" || fail "stage: no $f"
done
cp -a "$stage/tools/mc_import" "$appdir/usr/bin/mc_import" || fail "stage: no tools/mc_import"
for f in README.md LICENSE NOTICES.txt THIRD_PARTY.md VERSION.txt; do
    cp -a "$stage/$f" "$doc/$f" || fail "stage: no $f"
done
cp -a "$stage/docs" "$doc/docs"
cp "$wt/tools/appimage/ico-pc.desktop" "$appdir/ico-pc.desktop"
cp "$wt/tools/appimage/ico-pc.svg" "$appdir/ico-pc.svg"
ln -s ico-pc.svg "$appdir/.DirIcon"
ln -s usr/bin/ico_pc "$appdir/AppRun"
rm -f "$appimage" "$appimage.sha256"
run env ARCH="$appimage_arch" SOURCE_DATE_EPOCH="$(git log -1 --format=%ct "$commit")" \
    "$ait/squashfs-root/AppRun" --no-appstream --runtime-file "$ait/runtime" "$appdir" "$appimage"
[[ -f "$appimage" ]] || fail "appimagetool did not write $appimage"
chmod 755 "$appimage"
rm -rf "$appdir"
# an ELF (the runtime) with the AppImage type 2 mark at byte 8
[[ "$(od -An -tx1 -N11 "$appimage" | tr -d ' \n')" == 7f454c46*414902 ]] ||
    fail "$appimage does not start with the AppImage runtime"
chk="$root/build-host/tmp/appimage-check-$label"
rm -rf "$chk"
mkdir -p "$chk"
(cd "$chk" && "$appimage" --appimage-extract >/dev/null 2>>"$log") || fail "$appimage --appimage-extract"
sq="$chk/squashfs-root"
want_list="$chk/want.txt"
{
    printf '%s\n' .DirIcon AppRun ico-pc.desktop ico-pc.svg \
        usr/bin/ico_pc usr/bin/ico_pc.map usr/bin/libSDL3.so.0 usr/bin/mc_import
    for f in README.md LICENSE NOTICES.txt THIRD_PARTY.md VERSION.txt; do
        echo "usr/share/doc/ico-pc/$f"
    done
    for n in "${pkg_player_docs[@]}"; do echo "usr/share/doc/ico-pc/docs/$n.md"; done
} | LC_ALL=C sort > "$want_list"
(cd "$sq" && find . -mindepth 1 \( -type f -o -type l \) -printf '%P\n' | LC_ALL=C sort) > "$chk/got.txt"
if ! diff -u "$want_list" "$chk/got.txt" >>"$log" 2>&1; then
    fail "the AppImage's files are not the expected list (the diff is in the log)"
fi
[[ "$(readlink "$sq/AppRun")" == usr/bin/ico_pc ]] || fail "the AppImage's AppRun is not a link to usr/bin/ico_pc"
[[ "$(readlink "$sq/.DirIcon")" == ico-pc.svg ]] || fail "the AppImage's .DirIcon is not a link to ico-pc.svg"
[[ -x "$sq/usr/bin/ico_pc" ]] || fail "the AppImage's ico_pc is not executable"
cmp -s "$sq/usr/bin/ico_pc" "$stage/ico_pc" || fail "the AppImage's ico_pc differs from the stage's"
check_program_elf "$sq/usr/bin/ico_pc"
rm -rf "$chk"
(cd "$root/dist" && sha256sum "${appimage##*/}" > "${appimage##*/}.sha256")
echo "package_linux: AppImage $appimage ($(stat -c %s "$appimage") bytes)" >>"$log"

echo "$tgz"
echo "$tgzp"
echo "$appimage"
echo "built from $commit"
