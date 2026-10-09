#!/usr/bin/env bash
# Install the IP-safety scan, the format check, the generated-file freshness
# checks and the game-source gates as a git pre-commit hook (opt-in). Re-run
# any time the hook body changes.
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
#   2. tools/format.sh --check --staged
#                                   staged C must be clang-formatted; it
#                                   checks the staged blobs, not the worktree
#   3. freshness of the generated files (the same three CI runs):
#        tools/gen_data_desc.py --check       port/data/gen/
#        tools/gen_layout_asserts.py --check  the 64-bit layout asserts
#        tools/gen_sources.py --check         cmake/IcoSources.cmake
#      Regenerate with the same script without --check.
#   4. tools/strip_host_gates.py --check   no ICO_HOST conditional in ico2/
#                                          or sce/
#   5. tools/stack_overread_audit.py       no reliance on the PS2's stack
#                                          layout in ico2/
#   6. tools/check_call_types.py           calls and declarations agree with
#                                          the definitions (as CI)
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"

"$ROOT/tools/check_no_rom.sh"

# Staged C must be clang-formatted. Fix with: tools/format.sh FILE
"$ROOT/tools/format.sh" --check --staged

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

"$PY" "$ROOT/tools/stack_overread_audit.py" >/dev/null || {
    "$PY" "$ROOT/tools/stack_overread_audit.py"
    echo "pre-commit: a local is read past its end or only written: see tools/stack_overread_audit.py" >&2
    exit 1
}

"$PY" "$ROOT/tools/check_call_types.py" >/dev/null || {
    "$PY" "$ROOT/tools/check_call_types.py"
    echo "pre-commit: a call or a declaration disagrees with its definition: see tools/check_call_types.py" >&2
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
