#!/usr/bin/env bash
# tools/package_android.sh <label>
#
# Builds the signed Android package for HEAD: a clean worktree of HEAD under
# build-host/pkg-android-wt (no baserom, no uncommitted work), the Gradle
# release build (android/, arm64-v8a), zip-aligned for 16 KB pages and signed
# with the upload keystore, as dist/ico-pc-<label>-android.apk. The unstripped
# libmain.so and the link map (ico_pc.map) are staged under
# dist/stage/android/ with VERSION.txt, for turning crash offsets into
# function names (llvm-symbolizer). Quiet; the log is
# build-host/pkg-android-<label>.log. Safe to re-run. Builds only: it never
# runs the game.
#
# The keystore is not in the repository. It is read from
# $root/../android-keystores/upload.jks (alias upload), and its password from
# the first line of $root/../android-keystores/readme.txt that starts with
# "password" (as "password: <value>"), else the first non-empty line. The
# password is handed to apksigner through the environment, never printed,
# never logged, and the keystore is never copied.
#
# ICO_PKG_FILES="path ..." copies those working-tree files over the HEAD
# worktree before the build, to try a change before it is committed (the
# package then names HEAD but holds the change; the log lists the files).
set -euo pipefail
set +x

label="${1:-}"
if [[ ! "$label" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "usage: tools/package_android.sh <label>" >&2
    exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
mkdir -p build-host/tmp dist
export TMPDIR="$root/build-host/tmp"   # system /tmp is nearly full
log="$root/build-host/pkg-android-$label.log"
wt="$root/build-host/pkg-android-wt"
stage="$root/dist/stage/android"
apk="$root/dist/ico-pc-$label-android.apk"
ksdir="$root/../android-keystores"
ks="$ksdir/upload.jks"
ksreadme="$ksdir/readme.txt"
: > "$log"

fail() { echo "package_android: FAILED: $1 (see $log)" >&2; tail -n 30 "$log" >&2; exit 1; }
run() { "$@" >>"$log" 2>&1 || fail "$*"; }
cleanup() {
    git -C "$root" worktree remove --force "$wt" >>"$log" 2>&1 || rm -rf "$wt"
    git -C "$root" worktree prune >>"$log" 2>&1 || true
    rm -f "$root/build-host/tmp/android-$label-aligned.apk"
}
trap cleanup EXIT

[[ -x "$root/tools/toolchain/cmake/bin/cmake" ]] ||
    fail "no toolchain: run tools/fetch_toolchain.sh and tools/fetch_android.sh"
[[ -f "$ks" ]] || fail "no keystore at $ks (it lives outside the repository)"
[[ -f "$ksreadme" ]] || fail "no password file at $ksreadme"

# the password: the first "password" line (password: <value>), else the first
# non-empty line; nothing here is echoed
pw=""
while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%$'\r'}"
    if [[ "$line" =~ ^password[\ :=]+(.*)$ ]]; then
        pw="${BASH_REMATCH[1]}"
        break
    fi
done < "$ksreadme"
if [[ -z "$pw" ]]; then
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%$'\r'}"
        if [[ -n "${line//[[:space:]]/}" ]]; then pw="$line"; break; fi
    done < "$ksreadme"
fi
[[ -n "$pw" ]] || fail "no password in $ksreadme"

commit="$(git rev-parse HEAD)"
echo "package_android: $label from $commit" >>"$log"

# clean worktree of HEAD; only the untracked toolchain and venv are shared
cleanup
run git worktree add --detach "$wt" "$commit"
ln -s "$root/.venv" "$wt/.venv"
ln -s "$root/tools/toolchain" "$wt/tools/toolchain"

for f in ${ICO_PKG_FILES:-}; do
    [[ -f "$root/$f" ]] || fail "ICO_PKG_FILES: no such file $f"
    mkdir -p "$(dirname "$wt/$f")"
    cp "$root/$f" "$wt/$f"
    echo "package_android: overlay $f" >>"$log"
done

export PATH="$wt/.venv/bin:$PATH"
cd "$wt"
# android/local.properties for this checkout, and tools/toolchain/android.env
run tools/fetch_android.sh
# shellcheck disable=SC1091
. "$wt/tools/toolchain/android.env"
bt="$ICO_ANDROID_BUILD_TOOLS"
for t in zipalign apksigner; do
    [[ -x "$bt/$t" ]] || fail "no $t in $bt: run tools/fetch_android.sh"
done

# ICO_GRADLE_ARGS: extra Gradle properties, e.g. "-PicoHandProbe=ON" for a
# diagnostic package that still carries the release signature (an unsigned
# debug build cannot be installed over the release without losing the saves)
# shellcheck disable=SC2086
(cd android && run ./gradlew --no-daemon assembleRelease "-PicoLabel=$label" ${ICO_GRADLE_ARGS:-})
unsigned="$wt/android/app/build/outputs/apk/release/app-release-unsigned.apk"
[[ -f "$unsigned" ]] || fail "Gradle did not produce app-release-unsigned.apk"
so="$(ls "$wt"/android/app/build/intermediates/cxx/*/*/obj/arm64-v8a/libmain.so 2>/dev/null | head -n 1 || true)"
map="$(ls "$wt"/android/app/build/intermediates/cxx/*/*/obj/arm64-v8a/ico_pc.map "$wt"/android/app/.cxx/*/*/arm64-v8a/ico_pc.map 2>/dev/null | head -n 1 || true)"
[[ -n "$so" && -f "$so" ]] || fail "no unstripped libmain.so under android/app/build/intermediates/cxx"
[[ -n "$map" && -f "$map" ]] || fail "no ico_pc.map beside libmain.so (android/app/build/intermediates/cxx) or under android/app/.cxx"
cd "$root"

# 16 KB zip alignment, then the signature (v2 and v3; zipalign first, because
# signing must come last)
aligned="$root/build-host/tmp/android-$label-aligned.apk"
rm -f "$aligned" "$apk"
run "$bt/zipalign" -P 16 -f 4 "$unsigned" "$aligned"
# the password goes in through the environment: a command line (and the
# failure message above) would show it
ICO_KS_PASS="$pw" run "$bt/apksigner" sign --ks "$ks" --ks-key-alias upload \
    --ks-pass env:ICO_KS_PASS --key-pass env:ICO_KS_PASS \
    --v2-signing-enabled true --v3-signing-enabled true --out "$apk" "$aligned"
unset pw
rm -f "$apk.idsig"

"$bt/apksigner" verify --verbose --print-certs "$apk" >>"$log" 2>&1 ||
    fail "apksigner verify"
run "$bt/zipalign" -c -P 16 -v 4 "$apk"
certs="$("$bt/apksigner" verify --print-certs "$apk" 2>&1 | grep -E 'certificate SHA-(1|256) digest' || true)"
[[ -n "$certs" ]] || fail "apksigner printed no certificate digest"
echo "package_android: signer: $certs" >>"$log"

# stage: the unstripped library and the link map next to the APK
commit8="${commit:0:8}"
date_str="$(date +%Y-%m-%d)"
mkdir -p "$stage"
rm -f "$stage"/libmain.so "$stage"/ico_pc.map "$stage"/VERSION.txt
cp "$so" "$stage/libmain.so"
cp "$map" "$stage/ico_pc.map"
printf 'ico-pc %s\nbuilt %s\ncommit %s\n' "$label" "$date_str" "$commit" > "$stage/VERSION.txt"

echo "$apk"
echo "built from $commit ($commit8)"
echo "$certs"
echo "symbols: $stage/libmain.so, $stage/ico_pc.map"
