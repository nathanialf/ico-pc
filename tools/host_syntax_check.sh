#!/usr/bin/env bash
# tools/host_syntax_check.sh [-v] [--strict] [FILE.c...]
#
# Front-end check of ico2/**/*.c with the host gcc (32-bit, signed char, no
# strict aliasing) and ICO_HOST defined. Mirrors the period build's include
# search order (programmer's own include dir first, then the siblings, then
# sce/<archive>). Prints one line per failing file with its error count;
# -v also prints the diagnostics. Exit 1 if any file fails.
# Inline asm bodies are not checked here (package 1A rewrites them).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${HOST_CC:-gcc}"
# The decomp is K&R-era C: pointer/int mixing that gcc 14 turns into hard errors
# (incompatible-pointer-types, int-conversion, implicit-function-declaration,
# implicit-int) is demoted to warnings by default, as the host build will do.
# --strict leaves them as errors, to list what a strict compiler would reject.
DEMOTE="-Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=return-mismatch"
verbose=0
while [ $# -gt 0 ]; do
    case "$1" in
        -v) verbose=1; shift ;;
        --strict) DEMOTE=""; shift ;;
        *) break ;;
    esac
done
cd "$ROOT"
if [ $# -gt 0 ]; then files=("$@"); else
    mapfile -t files < <(find ico2 -name '*.c' | sort)
fi
SCE=""
for a in libc libm libvu0 libkernl libpkt libgraph libdma libpad libscf libmpeg libmc libipu libcdvd libsndn2; do
    SCE="$SCE -I$ROOT/sce/$a"
done
fail=0; total=0
for f in "${files[@]}"; do
    prog="${f#ico2/}"; prog="${prog%%/*}"
    src="${f#ico2/$prog/}"
    incs=""
    for p in "$prog" sugipon omori common ito fumi seki script; do
        [ -d "ico2/$p/include" ] || continue
        case " $incs " in *" -I../$p/include "*) continue ;; esac
        incs="$incs -I../$p/include"
    done
    total=$((total + 1))
    # shellcheck disable=SC2086
    out="$(cd "ico2/$prog" && "$CC" -m32 -std=gnu11 -fsyntax-only -nostdinc -fsigned-char \
        -fno-strict-aliasing -fgnu89-inline -Wno-all -Wimplicit-function-declaration $DEMOTE -DICO_HOST $incs $SCE "$src" 2>&1)"
    status=$?
    # An ICO_* macro whose header is not included becomes an implicit call that
    # the demoted check would let through; treat it as a failure.
    ico_miss="$(printf '%s\n' "$out" | grep -a "implicit declaration of function .ICO_" || true)"
    if [ $status -ne 0 ] || [ -n "$ico_miss" ]; then
        [ -n "$ico_miss" ] && out="$out"$'\n'"error: ICO_* macro used without typedef.h: $ico_miss"
        fail=$((fail + 1))
        echo "FAIL $f ($(printf '%s\n' "$out" | grep -ac 'error:') errors)"
        [ $verbose = 1 ] && printf '%s\n' "$out" | grep -a 'error' | sed 's/^/    /'
    fi
done
echo "$((total - fail))/$total pass"
[ $fail = 0 ]
