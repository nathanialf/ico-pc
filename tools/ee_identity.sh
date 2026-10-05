#!/usr/bin/env bash
# tools/ee_identity.sh [-r REV] [-k] (--all | FILE.c...)
#
# Optional developer tool: the EE identity check. Compiles each given C source
# with the period compiler (tools/compile_c.sh: ee-gcc 2.9-991111 and its
# assembler) twice, from the working tree and from a temporary worktree of
# REV (default HEAD), and compares the two objects' loaded sections
# (.text .data .rodata .sdata .bss .sbss .lit4 .lit8 .rodata.str*) and their
# relocations. Debug sections are left out (they carry the build paths).
# The port's sweeps (docs/port/SWEEP_*.md) use it to show a change to
# ico2/ left the code the PS2 compiler emits unchanged.
#
#   FILE.c   repo-relative sources, e.g. ico2/seki/src/Basic.c (ico2/ or sce/)
#   --all    every ico2/ C source of config/link_order.pal.txt
#   -r REV   the baseline revision (any git rev; default HEAD)
#   -k       keep the temporary worktree and objects (path printed)
#
# Needs tools/cc/ (tools/setup.sh), 32-bit libraries, gcc -m32 and
# mips-linux-gnu-objdump (docs/BUILDING.md, "Maintainers: EE identity
# check"). Nothing here is part of the port's build or of CI. Exit 0 when
# every file is identical, 1 on a difference, 2 on a usage or tool error.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
rev=HEAD
keep=0
all=0
files=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -r) rev="${2:?-r needs a revision}"; shift 2 ;;
        -k) keep=1; shift ;;
        --all) all=1; shift ;;
        -h|--help) sed -n 2,23p "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "ee_identity: unknown option $1" >&2; exit 2 ;;
        *) files+=("$1"); shift ;;
    esac
done
if [[ $all -eq 1 ]]; then
    mapfile -t files < <(grep -E '^ico2/.*\.c$' config/link_order.pal.txt)
fi
if [[ ${#files[@]} -eq 0 ]]; then
    echo "usage: tools/ee_identity.sh [-r REV] [-k] (--all | FILE.c...)" >&2
    exit 2
fi

objdump="${OBJDUMP:-mips-linux-gnu-objdump}"
command -v "$objdump" >/dev/null || { echo "ee_identity: $objdump not found (binutils-mips-linux-gnu)" >&2; exit 2; }
[[ -x tools/cc/ee-gcc2.9-991111/ee-gcc ]] || { echo "ee_identity: tools/cc/ is missing: run tools/setup.sh" >&2; exit 2; }
for f in "${files[@]}"; do
    [[ -f "$f" ]] || { echo "ee_identity: no such file in the working tree: $f" >&2; exit 2; }
done

mkdir -p build-host/tmp
export TMPDIR="$root/build-host/tmp"
tmp="$(mktemp -d "$TMPDIR/ee-identity.XXXXXX")"
wt="$tmp/wt"
cleanup() {
    if [[ $keep -eq 1 ]]; then
        echo "ee_identity: kept $tmp (worktree $wt)"
        return
    fi
    git -C "$root" worktree remove --force "$wt" >/dev/null 2>&1 || true
    git -C "$root" worktree prune >/dev/null 2>&1 || true
    rm -rf "$tmp"
}
trap cleanup EXIT

git worktree add --detach "$wt" "$rev" >/dev/null 2>&1 || { echo "ee_identity: cannot make a worktree of $rev" >&2; exit 2; }
# the toolchain is not tracked: share the working tree's
ln -s "$(readlink -f "$root/tools/cc")" "$wt/tools/cc"
mkdir -p "$wt/build"   # period_env.sh builds its preload library here

# the build's flags for a file are compile_c.sh's own: the working tree's script
# for the new side, the revision's for the old side
compile() { # <tree> <src> <out>
    (cd "$1" && tools/compile_c.sh "$2" "$3") >"$3.log" 2>&1
}
dump() { # <obj> -> sections and relocations, without file names
    "$objdump" -h -s -r -j .text -j .data -j .rodata -j .sdata -j .bss -j .sbss \
        -j .lit4 -j .lit8 -j .rodata.str1.4 -j .rodata.str1.1 "$1" 2>/dev/null |
        sed -e '/file format/d' |
        awk '/^ +[0-9]+ \./ { $6 = "-" } { print }'   # file offsets move with the debug sections
}

mkdir -p "$tmp/new" "$tmp/old"
same=0
diffs=()
errs=()
i=0
for f in "${files[@]}"; do
    i=$((i + 1))
    n="$(printf '%04d' "$i")"
    if ! git cat-file -e "$rev:$f" 2>/dev/null; then
        echo "NEW   $f (not in $rev)"
        continue
    fi
    compile "$root" "$f" "$tmp/new/$n.o" &
    compile "$wt" "$f" "$tmp/old/$n.o" &
    wait
    if [[ ! -f "$tmp/new/$n.o" || ! -f "$tmp/old/$n.o" ]]; then
        echo "ERROR $f (compile failed; see $tmp/*/$n.o.log, use -k)"
        errs+=("$f")
        continue
    fi
    dump "$tmp/new/$n.o" >"$tmp/new/$n.txt"
    dump "$tmp/old/$n.o" >"$tmp/old/$n.txt"
    if diff -q "$tmp/old/$n.txt" "$tmp/new/$n.txt" >/dev/null; then
        same=$((same + 1))
        [[ ${#files[@]} -le 20 ]] && echo "same  $f"
    else
        echo "DIFF  $f"
        diff -u "$tmp/old/$n.txt" "$tmp/new/$n.txt" | head -40
        diffs+=("$f")
    fi
done
echo "ee_identity: $same identical, ${#diffs[@]} different, ${#errs[@]} errors, against $rev"
[[ ${#errs[@]} -eq 0 ]] || exit 2
[[ ${#diffs[@]} -eq 0 ]]
