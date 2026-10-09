#!/usr/bin/env bash
# =============================================================================
# check_android_flags.sh
#
# Checks that the Android build compiles the game the way every other host
# does: the semantics options of cmake/IcoFlags.cmake (ICO_SEMANTIC_OPTIONS)
# reach the NDK clang for every ico2/ and port/ source, no game or
# port/math object holds a fused multiply-add, no game or port object
# stores onto its stack protector's guard (tools/check_stack_guard.py), and
# no game unit writes an object it declares const (tools/check_const_writes.py).
# aarch64 has fmadd/fmsub and clang contracts a * b + c into them unless
# -ffp-contract=off, which rounds once where the EE rounds twice; x86-64 at
# the default target has no FMA, so only an arm64 build would show it.
#
# Usage: tools/check_android_flags.sh [build-dir]
#   build-dir   a configured (and, for the object check, built) android-arm64
#               tree with compile_commands.json. Default: the Gradle tree
#               under android/app/.cxx, else build-host/android-arm64.
# The object check needs llvm-objdump from the NDK (tools/toolchain/
# android.env or ANDROID_NDK_ROOT) and is skipped when no object is built.
# =============================================================================
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
dir=${1:-}
if [ -z "$dir" ]; then
    # the Gradle build's tree: the one whose objects include the game's
    for d in "$root"/android/app/.cxx/*/*/arm64-v8a; do
        if [ -d "$d/CMakeFiles/ico_omori.dir" ]; then
            dir=$d
            break
        fi
    done
    [ -n "$dir" ] || dir="$root/build-host/android-arm64"
fi
cc="$dir/compile_commands.json"
[ -f "$cc" ] || { echo "check_android_flags: no $cc (configure the android-arm64 build first)"; exit 1; }

python3 - "$cc" "$dir" <<'EOF'
import json, os, shlex, sys
cc, build = sys.argv[1], os.path.realpath(sys.argv[2])
# compiled with the library's own options, not the game's: libmpeg2's thread
# shim (port/fmv/CMakeLists.txt)
exempt = {"port/fmv/ithread_single.c"}
need = ["-fno-strict-aliasing", "-fwrapv", "-ffp-contract=off", "-fno-fast-math", "-fsigned-char"]
banned = ["-ffast-math", "-Ofast", "-funsafe-math-optimizations", "-ffp-contract=fast",
          "-ffp-contract=on", "-fassociative-math", "-freciprocal-math", "-funsigned-char",
          "-fno-signed-char", "-fno-wrapv", "-fstrict-aliasing"]
bad = 0
seen = 0
for e in json.load(open(cc)):
    f = e["file"]
    # the tree may be another checkout's: the path from its ico2/ or port/
    cut = [f.find(s) for s in ("/ico2/", "/port/") if f.find(s) >= 0]
    # generated test oracles under the build tree (port/math's dp-bit) and
    # third-party code keep their own options
    if not cut or "third_party" in f or os.path.realpath(f).startswith(build + "/"):
        continue
    rel = f[min(cut) + 1:]
    if rel in exempt:
        continue
    args = e["arguments"] if "arguments" in e else shlex.split(e["command"])
    seen += 1
    missing = [o for o in need if o not in args]
    # the last -ffp-contract / char / wrapv option wins: a banned one after
    # the needed one overrides it
    late = [o for o in banned if o in args and
            max(i for i, a in enumerate(args) if a == o) >
            max([i for i, a in enumerate(args) if a in need] or [-1])]
    if missing or late:
        bad += 1
        if bad <= 20:
            print("%s: missing %s%s" % (rel, " ".join(missing) or "nothing",
                                        ", overridden by " + " ".join(late) if late else ""))
if seen == 0:
    print("check_android_flags: no ico2/ or port/ source in " + cc)
    sys.exit(1)
if bad:
    print("check_android_flags: %d of %d sources lack the game's semantics options" % (bad, seen))
    sys.exit(1)
print("check_android_flags: %d sources carry %s" % (seen, " ".join(need)))
EOF

# v0.4.4 AN-19e (issue 19): no game unit writes an object it declares const
# (tools/check_const_writes.py): clang deletes those stores where gcc keeps
# them, so only the clang builds, this one among them, would show it.
python3 "$root/tools/check_const_writes.py" --build "$dir"

# no fused multiply-add in the game's objects and port/math's
if [ -z "${ANDROID_NDK_ROOT:-}" ] && [ -f "$root/tools/toolchain/android.env" ]; then
    # shellcheck disable=SC1091
    . "$root/tools/toolchain/android.env"
fi
objdump="${ANDROID_NDK_ROOT:-}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump"
[ -x "$objdump" ] || objdump=$(command -v llvm-objdump || true)
objs=$(find "$dir" -name '*.o' \( -path '*/ico2/*' -o -path '*/port/math/*' \) -not -path '*/test/*' 2>/dev/null || true)
if [ -z "$objs" ]; then
    echo "check_android_flags: no objects built; fused multiply-add check skipped"
    exit 0
fi
[ -n "$objdump" ] || { echo "check_android_flags: no llvm-objdump for the object check"; exit 1; }
n=0
fused=0
for o in $objs; do
    n=$((n + 1))
    c=$("$objdump" -d --no-show-raw-insn "$o" | grep -cE '[[:space:]]f(n)?m(add|sub)[[:space:]]' || true)
    if [ "$c" != "0" ]; then
        echo "$o: $c fused multiply-add instructions"
        fused=$((fused + c))
    fi
done
if [ "$fused" != "0" ]; then
    echo "check_android_flags: $fused fused multiply-adds in the game's objects"
    exit 1
fi
echo "check_android_flags: no fused multiply-add in $n objects"

# No store onto a stack protector's guard word (tools/check_stack_guard.py):
# only the Android build has -fstack-protector-strong (the NDK's flags), so
# a write one element past a local array, harmless on the EE and on the
# desktop builds, ends the run on a phone when the function returns.
guarded=$(find "$dir" -name '*.o' \( -path '*/ico2/*' -o -path '*/port/*' \) -not -path '*/test/*' \
    -not -path '*third_party*' -not -path '*/deps/*' 2>/dev/null || true)
# shellcheck disable=SC2086
python3 "$root/tools/check_stack_guard.py" "$objdump" $guarded

