#!/usr/bin/env bash
# =============================================================================
# arm64_diff.sh
#
# The hand-holding differential harnesses: the game's hand-holding code built
# for x86-64 and for arm64, each with its own build's compile flags, run over
# the same inputs; the outputs are diffed.  A difference is game code whose
# result depends on the target (issue 19: Ico and Yorda's arms raised on
# Android only).  tools/arm64_diff/ikdiff.c runs the arm IK;
# tools/arm64_diff/followdiff.c runs Yorda following Ico by the hand
# (actGirlHand on a fiber, the hand manager, the steering helpers) and the
# hand target it leads to.  A developer tool, not a test: the arm64 programs
# run under qemu-aarch64 (user mode), which ctest does not have.
#
# Usage: tools/arm64_diff.sh [out-dir]
#   out-dir   where the objects, programs and outputs go
#             (default build-host/arm64-diff)
# Environment:
#   NDK        the NDK's llvm prebuilt dir (default: the r28 one under
#              ~/Android/Sdk, or ANDROID_NDK_ROOT's)
#   QEMU       qemu-aarch64, user mode (default: the one on PATH)
#   ICO_BASE_ELF  the PS2 game's main ELF: the joint limits (ikdiff) and the
#              data tables (followdiff) come from it, as the game loads them
#              (default: baserom/pal/baseelf.elf if there, else synthetic
#              limits and zero tables)
#   CC_DB_X64, CC_DB_A64, CC_DB_X64_CLANG  compile_commands.json of the
#              linux-x64, android-arm64 and linux-x64-clang builds whose
#              flags are used (default: build-host/<preset>/; when one is
#              missing the flags below, copied from them, are used)
# The x86-64 programs are built with the linux-x64 build's gcc and, when the
# llvm-mingw clang is there, the linux-x64-clang build's clang, and with the
# NDK's own clang for x86-64 (the arm64 compiler with the other backend).
# Exit status: 0 when every output equals the arm64 one, 1 otherwise.
# =============================================================================
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/build-host/arm64-diff}
mkdir -p "$out"
out=$(cd "$out" && pwd)

ndk=${NDK:-}
if [ -z "$ndk" ]; then
    for d in "${ANDROID_NDK_ROOT:-/nonexistent}/toolchains/llvm/prebuilt/linux-x86_64" \
        "$HOME"/Android/Sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64; do
        if [ -x "$d/bin/clang" ]; then
            ndk=$d
        fi
    done
fi
[ -n "$ndk" ] && [ -x "$ndk/bin/clang" ] || {
    echo "arm64_diff: no NDK clang (set NDK)"
    exit 2
}
qemu=${QEMU:-$(command -v qemu-aarch64 || true)}
[ -n "$qemu" ] && [ -x "$qemu" ] || {
    echo "arm64_diff: no qemu-aarch64 (set QEMU)"
    exit 2
}
elf=${ICO_BASE_ELF:-}
if [ -z "$elf" ] && [ -f "$root/baserom/pal/baseelf.elf" ]; then
    elf=$root/baserom/pal/baseelf.elf
fi

# flags: <db> <file> -> the compile command of that file with the source,
# -o and -c dropped, -Werror dropped, paths under the db's checkout moved to
# this one.  Prints nothing when the db or the entry is missing.
flags() {
    python3 - "$1" "$2" "$root" <<'EOF'
import json, os, shlex, sys
db, want, root = sys.argv[1:4]
if not os.path.isfile(db):
    sys.exit(0)
for e in json.load(open(db)):
    f = e["file"]
    if not f.endswith("/" + want):
        continue
    src = f[: -len(want) - 1]
    args = e.get("arguments") or shlex.split(e["command"])
    res = []
    skip = False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in ("-o", "-c"):
            skip = True
            continue
        if a == f or a.startswith("-Werror") or a.startswith("-fmacro-prefix-map"):
            continue
        res.append(root if a == src else a.replace(src + "/", root + "/"))
    print(args[0], " ".join(shlex.quote(a) for a in res))
    break
EOF
}

# the same flags when no build is configured (the json entries as of v0.4.4)
common="-O2 -g -std=gnu11 -fno-strict-aliasing -fwrapv -ffp-contract=off -fno-fast-math -fsigned-char -fno-common -fgnu89-inline"
incs="-I$root/ico2/sugipon/include -I$root/ico2/omori/include -I$root/ico2/common/include -I$root/ico2/ito/include -I$root/ico2/fumi/include -I$root/ico2/seki/include -I$root/ico2/script/include -I$root/port/compat"
android="--target=aarch64-none-linux-android29 --sysroot=$ndk/sysroot -g -DANDROID -fdata-sections -ffunction-sections -funwind-tables -fstack-protector-strong -no-canonical-prefixes -D_FORTIFY_SOURCE=2 -Wformat -O2 -g -std=gnu11 -fPIC -fno-strict-aliasing -fwrapv -ffp-contract=off -fno-fast-math -fsigned-char -fno-common -fgnu89-inline"
fallback_game_x64="/usr/bin/gcc -DICO_HEADLESS=1 -DICO_HOST=1 $incs -I$root/port/include $common"
fallback_math_x64="/usr/bin/gcc $common"
fallback_game_a64="$ndk/bin/clang -DICO_HOST=1 -DICO_RD=1 $incs -I$root/port/render -I$root/port/include $android"
fallback_math_a64="$ndk/bin/clang $android"
fallback_data_x64="$fallback_game_x64 -I$root/port/data -I$root"
fallback_data_a64="$fallback_game_a64 -I$root/port/data -I$root"
fallback_plat_x64="/usr/bin/gcc -I$root/port/platform -I$root/port/compat $common"
fallback_plat_a64="$ndk/bin/clang -I$root/port/platform -I$root/port/compat $android"

db_x64=${CC_DB_X64:-$root/build-host/linux-x64/compile_commands.json}
db_a64=${CC_DB_A64:-$root/build-host/android-arm64/compile_commands.json}
db_x64c=${CC_DB_X64_CLANG:-$root/build-host/linux-x64-clang/compile_commands.json}
game_x64=$(flags "$db_x64" ico2/sugipon/src/motionManager.c)
math_x64=$(flags "$db_x64" port/math/ps2float.c)
game_a64=$(flags "$db_a64" ico2/sugipon/src/motionManager.c)
math_a64=$(flags "$db_a64" port/math/ps2float.c)
game_x64c=$(flags "$db_x64c" ico2/sugipon/src/motionManager.c)
math_x64c=$(flags "$db_x64c" port/math/ps2float.c)
data_x64=$(flags "$db_x64" port/data/tables.c)
data_a64=$(flags "$db_a64" port/data/tables.c)
data_x64c=$(flags "$db_x64c" port/data/tables.c)
plat_x64=$(flags "$db_x64" port/platform/fiber.c)
plat_a64=$(flags "$db_a64" port/platform/fiber.c)
plat_x64c=$(flags "$db_x64c" port/platform/fiber.c)
[ -n "$data_x64" ] || data_x64=$fallback_data_x64
[ -n "$data_a64" ] || data_a64=$fallback_data_a64
[ -n "$plat_x64" ] || plat_x64=$fallback_plat_x64
[ -n "$plat_a64" ] || plat_a64=$fallback_plat_a64
[ -n "$game_x64" ] || game_x64=$fallback_game_x64
[ -n "$math_x64" ] || math_x64=$fallback_math_x64
[ -n "$game_a64" ] || game_a64=$fallback_game_a64
[ -n "$math_a64" ] || math_a64=$fallback_math_a64
echo "arm64_diff: x86-64 game flags: $game_x64"
echo "arm64_diff: arm64 game flags: $game_a64"

game_src="ico2/sugipon/src/handManager.c ico2/sugipon/src/matrixDrive.c ico2/sugipon/src/quaternion.c ico2/sugipon/src/tableSin.c"
math_src="port/platform/fpenv.c port/math/libvu0.c port/math/matrix.c port/math/matrix_drive.c port/math/matrix_stack.c port/math/ps2float.c port/math/quaternion.c port/math/softdouble.c port/math/newlib/ico_libm.c"

# build <name> <game-cc+flags> <math-cc+flags> <link-extra>
build() {
    local name=$1 game=$2 math=$3 ldx=$4 d=$out/$1 s o objs
    mkdir -p "$d"
    objs=""
    for s in $game_src tools/arm64_diff/ikdiff_data.c; do
        o=$d/$(echo "$s" | tr '/' '_').o
        eval "$game -w -c $root/$s -o $o"
        objs="$objs $o"
    done
    for s in $math_src; do
        o=$d/$(echo "$s" | tr '/' '_').o
        eval "$math -w -c $root/$s -o $o"
        objs="$objs $o"
    done
    eval "$game -w -c $root/ico2/sugipon/src/motionManager.c -o $d/motionManager.o"
    # GetGeometryOfMotion sets the current motion's record before it calls
    # GetMatrixOfMotion; the harness calls the second only and sets the
    # record itself: the static made global in the object, its code as built
    "$ndk/bin/llvm-objcopy" --globalize-symbol=skelMotDef \
        --redefine-sym skelMotDef=ikdiff_skelMotDef "$d/motionManager.o"
    eval "$game -w -c $root/tools/arm64_diff/ikdiff.c -o $d/ikdiff_path.o"
    eval "$game -w -DIKDIFF_UNITS -c $root/tools/arm64_diff/ikdiff.c -o $d/ikdiff_units.o"
    # every call the harness does not answer: a stub that stops it, named
    : >"$d/stubs.c"
    local p errlim=""
    case "$ldx" in *-static* | *lld*) errlim="-Wl,--error-limit=0" ;; esac
    for p in path units; do
        local mm=""
        [ $p = path ] && mm=$d/motionManager.o
        local undef
        undef=$( (eval "$game -w $ldx $errlim $objs $mm $d/ikdiff_$p.o -o $d/ikdiff_$p -lm" 2>&1 || true) |
            sed -n "s/.*undefined \(reference to\|symbol:\) [\`']*\([A-Za-z_][A-Za-z0-9_]*\).*/\2/p" | sort -u)
        for u in $undef; do
            grep -q "^void $u(void)" "$d/stubs.c" ||
                echo "void $u(void) { extern void ikdiff_stub(const char *); ikdiff_stub(\"$u\"); }" >>"$d/stubs.c"
        done
    done
    eval "$game -w -c $d/stubs.c -o $d/stubs.o"
    eval "$game -w $ldx $objs $d/motionManager.o $d/ikdiff_path.o $d/stubs.o -o $d/ikdiff_path -lm"
    eval "$game -w $ldx $objs $d/ikdiff_units.o $d/stubs.o -o $d/ikdiff_units -lm"
}

run() {
    local name=$1 runner=$2 p
    for p in path units; do
        $runner "$out/$name/ikdiff_$p" "$elf" >"$out/$name.$p.txt" ||
            echo "arm64_diff: $name's ikdiff_$p stopped (status $?)" | tee -a "$out/$name.$p.txt"
    done
}

follow_src="ico2/fumi/src/girl_act.c ico2/fumi/src/act-game.c ico2/fumi/src/commonact.c ico2/fumi/src/fieldCollision.c ico2/sugipon/src/geometryManager.c ico2/sugipon/src/motionManager2.c ico2/omori/src/gv.c $game_src"
data_src="port/data/tables.c port/data/gen/table_defs.c port/data/gen/table_desc.c port/data/gen/ee_symbols.c"

# build_follow <name> <game-cc+flags> <math-cc+flags> <data-cc+flags>
#   <platform-cc+flags> <link-extra>: followdiff, the game's hand-holding
#   walk.  The calls the linked code makes and nothing answers are stubs
#   that stop it, named; the objects it reads and nothing defines are zero
#   (a relocation that is a call or a jump makes a symbol a function).
build_follow() {
    local name=$1 game=$2 math=$3 data=$4 plat=$5 ldx=$6 d=$out/$1/follow s o objs
    mkdir -p "$d"
    objs=""
    for s in $follow_src tools/arm64_diff/followdiff.c; do
        o=$d/$(echo "$s" | tr '/' '_').o
        eval "$game -w -c $root/$s -o $o"
        objs="$objs $o"
    done
    for s in $math_src; do
        o=$d/$(echo "$s" | tr '/' '_').o
        eval "$math -w -c $root/$s -o $o"
        objs="$objs $o"
    done
    for s in $data_src; do
        o=$d/$(echo "$s" | tr '/' '_').o
        eval "$data -w -c $root/$s -o $o"
        objs="$objs $o"
    done
    eval "$plat -w -c $root/port/platform/fiber.c -o $d/fiber.o"
    objs="$objs $d/fiber.o"
    local errlim="" undef calls u
    case "$ldx" in *-static* | *lld*) errlim="-Wl,--error-limit=0" ;; esac
    undef=$( (eval "$game -w $ldx $errlim $objs -o $d/followdiff -lm" 2>&1 || true) |
        sed -n "s/.*undefined \(reference to\|symbol:\) [\`']*\([A-Za-z_][A-Za-z0-9_]*\).*/\2/p" | sort -u)
    calls=$("$ndk/bin/llvm-objdump" -r $objs |
        sed -n 's/^[0-9a-f]* \(R_X86_64_PLT32\|R_AARCH64_CALL26\|R_AARCH64_JUMP26\) \([A-Za-z_][A-Za-z0-9_]*\).*/\2/p' |
        sort -u)
    : >"$d/stubs.c"
    for u in $undef; do
        if echo "$calls" | grep -qx "$u"; then
            echo "void $u(void) { extern void ikdiff_stub(const char *); ikdiff_stub(\"$u\"); }" >>"$d/stubs.c"
        else
            echo "unsigned char $u[262144] __attribute__((aligned(64)));" >>"$d/stubs.c"
        fi
    done
    eval "$game -w -fno-common -c $d/stubs.c -o $d/stubs.o"
    eval "$game -w $ldx $objs $d/stubs.o -o $d/followdiff -lm"
}

run_follow() {
    local name=$1 runner=$2
    $runner "$out/$name/follow/followdiff" "$elf" >"$out/$name.follow.txt" ||
        echo "arm64_diff: $name's followdiff stopped (status $?)" | tee -a "$out/$name.follow.txt"
}

build a64 "$game_a64" "$math_a64" "-static"
run a64 "$qemu"
build_follow a64 "$game_a64" "$math_a64" "$data_a64" "$plat_a64" "-static"
run_follow a64 "$qemu"
build x64-gcc "$game_x64" "$math_x64" ""
run x64-gcc ""
build_follow x64-gcc "$game_x64" "$math_x64" "$data_x64" "$plat_x64" ""
run_follow x64-gcc ""
# the NDK's clang for x86-64: the arm64 compiler with the other backend
to_x64() {
    echo "$1" | sed "s/--target=aarch64-none-linux-android29/--target=x86_64-none-linux-android29/"
}
build x64-ndk "$(to_x64 "$game_a64")" "$(to_x64 "$math_a64")" "-static"
run x64-ndk ""
build_follow x64-ndk "$(to_x64 "$game_a64")" "$(to_x64 "$math_a64")" "$(to_x64 "$data_a64")" \
    "$(to_x64 "$plat_a64")" "-static"
run_follow x64-ndk ""
names="x64-gcc x64-ndk"
if [ -n "$game_x64c" ] && [ -n "$math_x64c" ]; then
    build x64-clang "$game_x64c" "$math_x64c" "-fuse-ld=lld"
    run x64-clang ""
    if [ -n "$data_x64c" ] && [ -n "$plat_x64c" ]; then
        build_follow x64-clang "$game_x64c" "$math_x64c" "$data_x64c" "$plat_x64c" "-fuse-ld=lld"
        run_follow x64-clang ""
    fi
    names="$names x64-clang"
fi

# Lines are tagged by their first word.  Three tags differ by design:
#   md-turnobj  MatrixDrive_Turn*ObjectMatrix* keep their first angle in a
#               local the turn leaves alone for a direction on its axis: the
#               stack word the build left there (none of them is on the
#               hand path: the collision ray display, the camera editor and
#               a_p_1.c)
#   lim-rate0   limitHPAngleAndSetB at a rate of 0 converts +-Inf to int
#               (x86-64 INT_MIN, arm64 saturates); every call passes 1
#   quat-int    an int past a short handed to a short parameter through an
#               int declaration: the x86-64 clang callee trusts the caller's
#               extension, gcc and arm64 callees extend it themselves
status=0
for n in $names; do
    for p in path units follow; do
        [ -f "$out/$n.$p.txt" ] || continue
        if cmp -s "$out/a64.$p.txt" "$out/$n.$p.txt"; then
            echo "arm64_diff: $p: arm64 = $n ($(wc -l <"$out/a64.$p.txt") lines)"
            continue
        fi
        diff "$out/a64.$p.txt" "$out/$n.$p.txt" >"$out/$n.$p.diff" || true
        tags=$(sed -n 's/^< \([^ ]*\).*/\1/p' "$out/$n.$p.diff" | sort | uniq -c |
            awk '{printf "%s%s %d", sep, $2, $1; sep = ", "}')
        echo "arm64_diff: $p: arm64 differs from $n: $tags ($out/$n.$p.diff)"
        # (not grep -q: under pipefail its early exit fails the pipeline)
        unknown=$(sed -n 's/^[<>] \([^ ]*\).*/\1/p' "$out/$n.$p.diff" |
            grep -v '^\(md-turnobj\|lim-rate0\|quat-int\)$' || true)
        [ -z "$unknown" ] || status=1
    done
done
[ $status = 0 ] && echo "arm64_diff: no difference beyond the three known ones"
exit $status
