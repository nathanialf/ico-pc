#!/usr/bin/env bash
# Install the IP-safety scan, the format check and the generated-file
# freshness checks as a git pre-commit hook (opt-in). Re-run any time the hook body changes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# In a linked worktree `$ROOT/.git` is a FILE pointing at the main repo, and
# the hooks live in the main repo's shared hook dir. Ask git for that dir so
# running this from a worktree refreshes the hooks every checkout uses.
GITDIR="$(cd "$ROOT" && git rev-parse --git-common-dir 2>/dev/null || true)"
case "$GITDIR" in "") GITDIR="$ROOT/.git" ;; /*) ;; *) GITDIR="$ROOT/$GITDIR" ;; esac
HOOK="$GITDIR/hooks/pre-commit"

if [[ ! -d "$GITDIR" ]]; then
    echo "install_hooks: $ROOT is not a git repo (no git dir)" >&2
    exit 1
fi

cat > "$HOOK" <<'EOF'
#!/usr/bin/env bash
# Auto-installed by tools/install_hooks.sh. Runs, in order:
#   1. tools/check_no_rom.sh        IP-safety scan of the staged files; with
#                                   something staged it reads the staged
#                                   blobs (git show :path), not the worktree
#   2. tools/format.sh --check      staged C must be clang-formatted
#   3. freshness of the generated files (the same three CI runs):
#        tools/gen_data_desc.py --check       port/data/gen/
#        tools/gen_layout_asserts.py --check  the 64-bit layout asserts
#        tools/gen_sources.py --check         cmake/IcoSources.cmake
#      Regenerate with the same script without --check.
#   4. tools/strip_host_gates.py --check   no ICO_HOST conditional in ico2/,
#                                          sce/, vusrc/
set -e
ROOT="$(git rev-parse --show-toplevel)"

"$ROOT/tools/check_no_rom.sh"

# Staged C must be clang-formatted. Fix with: tools/format.sh FILE
STAGED_C=$(git diff --cached --name-only --diff-filter=ACMR -z | tr '\0' '\n' |
    grep -E '^(ico2/[^/]+/[^/]+|sce(/[^/]+){0,3})/[^/]+\.c(\.inc)?$|^port/.+\.[ch]$' |
    grep -v -E '^port/(third_party|rhi/test/shaders)/' || true)
if [[ -n "$STAGED_C" ]]; then
    # shellcheck disable=SC2086
    "$ROOT/tools/format.sh" --check $STAGED_C
fi

# The generators need pyelftools (tools/requirements.txt): the venv's python.
PY="$ROOT/.venv/bin/python"
[[ -x "$PY" ]] || PY=python3
for gen in gen_data_desc gen_layout_asserts gen_sources; do
    "$PY" "$ROOT/tools/$gen.py" --check || {
        echo "pre-commit: tools/$gen.py --check failed: run tools/$gen.py and stage its output" >&2
        exit 1
    }
done

"$PY" "$ROOT/tools/strip_host_gates.py" --check >/dev/null || {
    "$PY" "$ROOT/tools/strip_host_gates.py" --check
    echo "pre-commit: ico2/ is host code: write the host form, not an ICO_HOST conditional" >&2
    exit 1
}
EOF
chmod +x "$HOOK"
echo "Installed pre-commit hook at $HOOK"

# The byte-gate pre-push hook of the decompilation is gone; remove one left
# behind by an earlier install.
if [[ -f "$GITDIR/hooks/pre-push" ]] && grep -q "tools/install_hooks.sh" "$GITDIR/hooks/pre-push"; then
    rm -f "$GITDIR/hooks/pre-push"
    echo "Removed the old pre-push hook at $GITDIR/hooks/pre-push"
fi
