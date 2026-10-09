#!/usr/bin/env bash
# =============================================================================
# fog_arm64_diff.sh
#
# The depth fog's game side on arm64: port/render/test/fog_lut_test.c with
# ico2/seki/src/ZFog.c built by the NDK's clang for arm64 (the Android
# build's compiler and semantics options) and run under qemu-aarch64 (user
# mode): its own checks (the three player dumps' fog tables and sprites, the
# parameter sweep), then its printed tables and sprites against the host
# build's (the program ctest built, gcc or clang), byte for byte.  A
# difference is game code whose result depends on the compiler or the
# target, as issue 19's deleted stores and BgAnimation.c's (short)(float)
# were.
#
# Usage: tools/fog_arm64_diff.sh HOST-FOG-LUT-TEST [out-dir]
#   HOST-FOG-LUT-TEST  the host build's fog_lut_test program
#   out-dir            the arm64 program and the outputs
#                      (default build-host/tmp/fog-arm64)
# Environment:
#   NDK   the NDK's llvm prebuilt dir (default: ANDROID_NDK_ROOT's, else
#         tools/toolchain/android.env's, else the newest under ~/Android/Sdk)
#   QEMU  qemu-aarch64, user mode (default: the one on PATH)
#   CC_DB_A64  an android-arm64 compile_commands.json whose ZFog.c flags are
#         used (default build-host/android-arm64/; the flags below, copied
#         from it, when it is missing)
# Exit status: 0 when the arm64 program passes and prints what the host one
# prints, 1 otherwise, 77 when there is no NDK clang or no qemu-aarch64.
# =============================================================================
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
host=${1:?usage: fog_arm64_diff.sh HOST-FOG-LUT-TEST [out-dir]}
out=${2:-$root/build-host/tmp/fog-arm64}
mkdir -p "$out"
out=$(cd "$out" && pwd)

ndk=${NDK:-}
if [ -z "$ndk" ]; then
    ndkroot=${ANDROID_NDK_ROOT:-}
    if [ -z "$ndkroot" ] && [ -f "$root/tools/toolchain/android.env" ]; then
        ndkroot=$(sed -n 's/^ANDROID_NDK_ROOT="\(.*\)"$/\1/p' "$root/tools/toolchain/android.env")
    fi
    for d in "$HOME"/Android/Sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64 \
        "${ndkroot:-/nonexistent}/toolchains/llvm/prebuilt/linux-x86_64"; do
        if [ -x "$d/bin/clang" ]; then
            ndk=$d
        fi
    done
fi
if [ -z "$ndk" ] || [ ! -x "$ndk/bin/clang" ]; then
    echo "fog_arm64_diff: no NDK clang (set NDK): skipped"
    exit 77
fi
qemu=${QEMU:-$(command -v qemu-aarch64 || true)}
if [ -z "$qemu" ] || [ ! -x "$qemu" ]; then
    echo "fog_arm64_diff: no qemu-aarch64 (set QEMU): skipped"
    exit 77
fi

# the Android build's flags for ZFog.c, the source, -o, -c, -Werror and the
# dependency outputs dropped, its checkout's paths moved to this one
db=${CC_DB_A64:-$root/build-host/android-arm64/compile_commands.json}
flags=$(python3 -I - "$db" "$root" <<'PY'
import json, os, shlex, sys
db, root = sys.argv[1:3]
want = "ico2/seki/src/ZFog.c"
if not os.path.isfile(db):
    sys.exit(0)
for e in json.load(open(db)):
    f = e["file"]
    if not f.endswith("/" + want) or "/rd_fog_test.dir/" in e.get("output", ""):
        continue
    src = f[: -len(want) - 1]
    args = e.get("arguments") or shlex.split(e["command"])
    res, skip = [], False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in ("-o", "-c", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a == f or a.startswith("-Werror") or a.startswith("-fmacro-prefix-map") or a in ("-MD", "-MMD"):
            continue
        res.append(a.replace(src + "/", root + "/"))
    print(" ".join(shlex.quote(a) for a in res))
    break
PY
)
if [ -z "$flags" ]; then
    flags="--target=aarch64-none-linux-android29 -DICO_HOST=1 -DICO_RD=1 -g -DANDROID -fdata-sections -ffunction-sections -funwind-tables -fstack-protector-strong -no-canonical-prefixes -D_FORTIFY_SOURCE=2 -O2 -std=gnu11 -fPIC -fno-strict-aliasing -fwrapv -ffp-contract=off -fno-fast-math -fsigned-char -fno-common -fgnu89-inline"
fi
incs=""
for d in ico2/seki/include ico2/sugipon/include ico2/omori/include ico2/common/include \
    ico2/ito/include ico2/fumi/include ico2/script/include port/compat port/shaders port/math \
    port/rhi port/render; do
    incs="$incs -I$root/$d"
done
echo "fog_arm64_diff: arm64 flags: $flags"
eval "\"$ndk/bin/clang\" $flags $incs -w -static -c \"$root/ico2/seki/src/ZFog.c\" -o \"$out/ZFog.o\""
eval "\"$ndk/bin/clang\" $flags $incs -w -static -c \"$root/port/render/test/fog_lut_test.c\" -o \"$out/fog_lut_test.o\""
"$ndk/bin/clang" --target=aarch64-none-linux-android29 -static "$out/fog_lut_test.o" "$out/ZFog.o" \
    -o "$out/fog_lut_test_a64"

status=0
"$qemu" "$out/fog_lut_test_a64" || status=1
"$qemu" "$out/fog_lut_test_a64" print >"$out/a64.txt" || status=1
"$host" print >"$out/host.txt" || status=1
if cmp -s "$out/a64.txt" "$out/host.txt"; then
    echo "fog_arm64_diff: arm64 = host ($(wc -l <"$out/a64.txt") lines)"
else
    diff "$out/host.txt" "$out/a64.txt" >"$out/host-a64.diff" || true
    echo "fog_arm64_diff: arm64 differs from the host: $out/host-a64.diff"
    head -c 2000 "$out/host-a64.diff"
    status=1
fi
exit $status
