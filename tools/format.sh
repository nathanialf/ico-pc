#!/usr/bin/env bash
# Format (or check) the repo's C with clang-format and the tracked .clang-format.
#   tools/format.sh                   # rewrite every C source the script owns (below)
#   tools/format.sh --check           # exit 1 if any of them is not formatted (CI)
#   tools/format.sh FILE...           # format just these files
#   tools/format.sh --check FILE...   # check just these files
#   tools/format.sh --check --staged [FILE...]
#                                     # check the staged version of these files, or of
#                                     # every staged file the script owns (pre-commit)
# The source roots are the game's (ico2/, sce/: .c and .c.inc, which also get
# tools/format_layout.py's top-level blank-line layout) and port/ (.c and .h,
# clang-format only; not port/third_party or the generated shader headers
# under port/rhi/test/shaders). Whitespace and line breaks only. The
# binary is the `clang-format` wheel in the venv (tools/setup.sh installs it);
# without it the script fails. A FILE that does not exist (in the index, with
# --staged) is an error.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
CF="$ROOT/.venv/bin/clang-format"
if [[ ! -x "$CF" ]]; then
    echo "format: $CF not found; run tools/setup.sh (it installs tools/requirements.txt)" >&2
    exit 1
fi
mode=format
staged=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --check) mode=check; shift ;;
        --staged) staged=1; shift ;;
        *) break ;;
    esac
done
if [[ $staged == 1 && $mode != check ]]; then
    echo "format: --staged only checks (use --check --staged)" >&2
    exit 2
fi
# the files the script owns, as git pathspecs
owned=('ico2/*/*/*.c' 'ico2/*/*/*.c.inc' 'sce/*.c' 'sce/*/*.c' 'sce/*/*/*.c' 'sce/*/*/*/*.c'
    'port/*.c' 'port/*.h' ':!port/third_party' ':!port/rhi/test/shaders')
files=()
if [[ $# -gt 0 ]]; then
    # resolved against the caller's directory, then made repo-relative
    for f in "$@"; do
        abs="$(realpath -m -- "$f")"
        case "$abs" in
            "$ROOT"/*) rel="${abs#"$ROOT"/}" ;;
            *) rel="$abs" ;;
        esac
        if [[ $staged == 1 ]]; then
            if [[ "$rel" == /* ]] || ! git -C "$ROOT" cat-file -e ":$rel" 2>/dev/null; then
                echo "format: $f is not in the index" >&2
                exit 1
            fi
        elif [[ ! -f "$abs" ]]; then
            echo "format: $f does not exist" >&2
            exit 1
        fi
        files+=("$rel")
    done
elif [[ $staged == 1 ]]; then
    mapfile -d '' -t files < <(git -C "$ROOT" diff --cached --name-only --diff-filter=ACMR -z -- "${owned[@]}")
else
    mapfile -t files < <(cd "$ROOT" && git ls-files "${owned[@]}")
fi
workdir="$ROOT"
if [[ $staged == 1 ]]; then
    # the staged blobs (git show :path) in a scratch tree with the staged
    # .clang-format at its root, so a partly staged file is judged as it
    # will be committed
    workdir="$(mktemp -d)"
    trap 'rm -rf "$workdir"' EXIT
    git -C "$ROOT" show ":.clang-format" > "$workdir/.clang-format"
    for f in "${files[@]}"; do
        mkdir -p "$workdir/$(dirname "$f")"
        git -C "$ROOT" show ":$f" > "$workdir/$f"
    done
fi
cd "$workdir"
# the layout pass is the game sources' convention only
layout_files=()
for f in "${files[@]}"; do
    [[ "$f" == port/* ]] || layout_files+=("$f")
done
PY="$ROOT/.venv/bin/python"
LAYOUT="$ROOT/tools/format_layout.py"
if [[ "$mode" == check ]]; then
    bad=0
    for f in "${files[@]}"; do
        [[ -f "$f" ]] || continue # tracked but deleted in the worktree
        if ! "$CF" --dry-run -Werror "$f" >/dev/null 2>&1; then
            echo "format: $f is not clang-formatted (run tools/format.sh $f)" >&2
            bad=1
        fi
    done
    # top-level blank-line layout (include, declaration and preprocessor blocks)
    if [[ ${#layout_files[@]} -gt 0 ]]; then
        "$PY" "$LAYOUT" --check "${layout_files[@]}" || bad=1
    fi
    exit $bad
fi
"$CF" -i "${files[@]}"
if [[ ${#layout_files[@]} -gt 0 ]]; then
    "$PY" "$LAYOUT" "${layout_files[@]}"
fi
