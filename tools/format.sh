#!/usr/bin/env bash
# Format (or check) the repo's C with clang-format and the tracked .clang-format.
#   tools/format.sh            # rewrite every tracked .c under the source roots
#   tools/format.sh --check    # exit 1 if any tracked .c is not formatted (pre-commit)
#   tools/format.sh FILE...    # format just these files
# The source roots are the game's (ico2/, sce/: .c and .c.inc, which also get
# tools/format_layout.py's top-level blank-line layout) and port/ (.c and .h,
# clang-format only; not port/third_party or the generated shader headers
# under port/rhi/test/shaders). Whitespace and line breaks only. The
# binary is the `clang-format` wheel in the venv (tools/setup.sh installs it);
# without it the script fails.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CF="$ROOT/.venv/bin/clang-format"
if [[ ! -x "$CF" ]]; then
    echo "format: $CF not found; run tools/setup.sh (it installs tools/requirements.txt)" >&2
    exit 1
fi
mode=format
if [[ "${1:-}" == "--check" ]]; then mode=check; shift; fi
if [[ $# -gt 0 ]]; then
    files=("$@")
else
    mapfile -t files < <(cd "$ROOT" && git ls-files 'ico2/*/*/*.c' 'ico2/*/*/*.c.inc' 'sce/*.c' 'sce/*/*.c' \
        'sce/*/*/*.c' 'sce/*/*/*/*.c' 'port/*.c' 'port/*.h' ':!port/third_party' \
        ':!port/rhi/test/shaders')
fi
cd "$ROOT"
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
        [[ -f "$f" ]] || continue
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
