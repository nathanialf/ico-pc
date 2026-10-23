#!/usr/bin/env bash
# tools/package_android.sh <label>
#
# Builds the signed Android package for HEAD: a clean worktree of HEAD under
# build-host/pkg-android-wt (no baserom, no uncommitted work), the Gradle
# release build (android/, arm64-v8a), zip-aligned for 16 KB pages and signed
# with the upload keystore, as dist/ico-pc-<label>-android.apk. The APK
# carries assets/libmain.syms.gz (nm -n of the unstripped libmain.so, its
# code symbols, gzip-compressed and stored as it is), from which a crash or
# watchdog report names its functions on the phone (port/platform/symmap.h).
# The unstripped libmain.so, the link map (ico_pc.map) and libmain.syms.gz
# are staged under dist/stage/android/ with VERSION.txt, for turning crash
# offsets into function names (llvm-symbolizer). Quiet; the log is
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

pkg_name=package_android
# shellcheck source=package_common_lib.sh
. "$(dirname "${BASH_SOURCE[0]}")/package_common_lib.sh"
pkg_begin "${1:-}"
log="$root/build-host/pkg-android-$label.log"
wt="$root/build-host/pkg-android-wt"
stage="$root/dist/stage/android"
apk="$root/dist/ico-pc-$label-android.apk"
ksdir="$root/../android-keystores"
ks="$ksdir/upload.jks"
ksreadme="$ksdir/readme.txt"
: > "$log"

cleanup() {
    pkg_remove_worktree
    rm -f "$root/build-host/tmp/android-$label-aligned.apk" "$root/build-host/tmp/android-$label-syms.apk"
    rm -rf "$root/build-host/tmp/android-$label-syms"
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
pkg_make_worktree "$commit"

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

# ICO_GRADLE_ARGS: extra Gradle properties (e.g. a diagnostic switch) for a
# diagnostic package that still carries the release signature (an unsigned
# debug build cannot be installed over the release without losing the saves).
# The rc-only tooling (icoRcTools) is the label's alone: on for a label ending
# in -rc<N> (tools/check_rc_text.py; docs/BUILDING.md, "Release candidate
# tools"), never from ICO_GRADLE_ARGS.
[[ "${ICO_GRADLE_ARGS:-}" != *icoRcTools* ]] ||
    fail "ICO_GRADLE_ARGS names icoRcTools: the rc tooling follows the label alone"
rc_flag="$(python3 -I "$wt/tools/check_rc_text.py" --gradle-flag "$label")" ||
    fail "tools/check_rc_text.py --gradle-flag"
echo "package_android: $rc_flag" >>"$log"
# shellcheck disable=SC2086
(cd android && run ./gradlew --no-daemon assembleRelease "-PicoLabel=$label" "$rc_flag" ${ICO_GRADLE_ARGS:-})
unsigned="$wt/android/app/build/outputs/apk/release/app-release-unsigned.apk"
[[ -f "$unsigned" ]] || fail "Gradle did not produce app-release-unsigned.apk"
# a release's APK holds no rc row (libmain.so, classes.dex and the resources,
# each entry inflated), an rc's does
run python3 -I "$wt/tools/check_rc_text.py" --label "$label" "$unsigned"
so="$(ls "$wt"/android/app/build/intermediates/cxx/*/*/obj/arm64-v8a/libmain.so 2>/dev/null | head -n 1 || true)"
map="$(ls "$wt"/android/app/build/intermediates/cxx/*/*/obj/arm64-v8a/ico_pc.map "$wt"/android/app/.cxx/*/*/arm64-v8a/ico_pc.map 2>/dev/null | head -n 1 || true)"
[[ -n "$so" && -f "$so" ]] || fail "no unstripped libmain.so under android/app/build/intermediates/cxx"
[[ -n "$map" && -f "$map" ]] || fail "no ico_pc.map beside libmain.so (android/app/build/intermediates/cxx) or under android/app/.cxx"
cd "$root"

# the crash report's symbol table (port/platform/symmap.h): nm -n of the
# unstripped libmain.so, its code symbols only ("<address> T|t|W|w <name>",
# the addresses from the library's start), gzip-compressed and added to the
# APK stored (not deflated again), so the program reads it straight out of
# the APK file with a descriptor when a report is written
# (host_android.c ico_android_open_symbols); never read at start
llvm_nm="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm"
[[ -x "$llvm_nm" ]] || fail "no llvm-nm in the NDK ($llvm_nm)"
symdir="$root/build-host/tmp/android-$label-syms"
rm -rf "$symdir"
mkdir -p "$symdir"
"$llvm_nm" -n --defined-only "$so" > "$symdir/nm.txt" 2>>"$log" || fail "llvm-nm $so"
awk 'NF == 3 && $2 ~ /^[TtWw]$/' "$symdir/nm.txt" > "$symdir/libmain.syms"
grep -q ' T ico_diag_init$' "$symdir/libmain.syms" ||
    fail "libmain.syms lacks ico_diag_init (the symbol lines' anchor)"
gzip -9 -n -c "$symdir/libmain.syms" > "$symdir/libmain.syms.gz"
echo "package_android: libmain.syms: $(wc -l < "$symdir/libmain.syms") symbols, $(wc -c < "$symdir/libmain.syms.gz") bytes gzipped" >>"$log"
withsyms="$root/build-host/tmp/android-$label-syms.apk"
cp "$unsigned" "$withsyms"
run python3 - "$withsyms" "$symdir/libmain.syms.gz" <<'PY'
import sys, zipfile
apk, gz = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(apk, "a") as z:
    if "assets/libmain.syms.gz" in z.namelist():
        sys.exit("the APK already holds assets/libmain.syms.gz")
    z.write(gz, "assets/libmain.syms.gz", compress_type=zipfile.ZIP_STORED)
PY

# 16 KB zip alignment, then the signature (v2 and v3; zipalign first, because
# signing must come last)
aligned="$root/build-host/tmp/android-$label-aligned.apk"
rm -f "$aligned" "$apk"
run "$bt/zipalign" -P 16 -f 4 "$withsyms" "$aligned"
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
# the symbol table is in the signed APK, stored, so it can be read in place
run python3 - "$apk" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    i = z.getinfo("assets/libmain.syms.gz")
    if i.compress_type != zipfile.ZIP_STORED:
        sys.exit("assets/libmain.syms.gz is compressed in the APK")
PY
certs="$("$bt/apksigner" verify --print-certs "$apk" 2>&1 | grep -E 'certificate SHA-(1|256) digest' || true)"
[[ -n "$certs" ]] || fail "apksigner printed no certificate digest"
echo "package_android: signer: $certs" >>"$log"

# stage: the unstripped library and the link map next to the APK
commit8="${commit:0:8}"
date_str="$(date +%Y-%m-%d)"
mkdir -p "$stage"
rm -f "$stage"/libmain.so "$stage"/ico_pc.map "$stage"/libmain.syms.gz "$stage"/VERSION.txt
cp "$so" "$stage/libmain.so"
cp "$map" "$stage/ico_pc.map"
cp "$symdir/libmain.syms.gz" "$stage/libmain.syms.gz"
printf 'ico-pc %s\nbuilt %s\ncommit %s\n' "$label" "$date_str" "$commit" > "$stage/VERSION.txt"

echo "$apk"
echo "built from $commit ($commit8)"
echo "$certs"
echo "symbols: $stage/libmain.so, $stage/ico_pc.map, $stage/libmain.syms.gz (also in the APK)"
