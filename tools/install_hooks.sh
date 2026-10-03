#!/usr/bin/env bash
# Install the IP-safety scan and the byte gate as git hooks (opt-in):
#   - pre-commit: refuses a local commit that breaks the build
#   - pre-push:   refuses to push a ref whose tip does not rebuild the base,
#                 the backstop for commits made with --no-verify
# Re-run any time either hook body changes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# In a linked worktree `$ROOT/.git` is a FILE pointing at the main repo, and
# the hooks live in the main repo's shared hook dir. Ask git for that dir so
# running this from a worktree refreshes the hooks every checkout uses.
GITDIR="$(cd "$ROOT" && git rev-parse --git-common-dir 2>/dev/null || true)"
case "$GITDIR" in "") GITDIR="$ROOT/.git" ;; /*) ;; *) GITDIR="$ROOT/$GITDIR" ;; esac
HOOK="$GITDIR/hooks/pre-commit"
PUSH_HOOK="$GITDIR/hooks/pre-push"

if [[ ! -d "$GITDIR" ]]; then
    echo "install_hooks: $ROOT is not a git repo (no git dir)" >&2
    exit 1
fi

cat > "$HOOK" <<'EOF'
#!/usr/bin/env bash
# Auto-installed by tools/install_hooks.sh. Runs, in order:
#   1. tools/check_no_rom.sh        IP-safety scan of the staged files
#   2. tools/check_dev_native.py    refuses codegen-steering constructs in staged C
#   3. tools/format.sh --check      staged C must be clang-formatted
#   4. tools/build.sh setup         verify the base ELF and ROM SHA-1s, write build.ninja
#   5. ninja                        build every object from source, link, and run
#                                   tools/check_elf.py --gate --require-elf-sha
#                                   (the byte gate and the whole ELF's SHA-1)
#
# Steps 4 and 5 run only when a staged path can affect the build (ico2/, sce/,
# config/, tools/, baserom/). `build.sh setup` deletes build/ first, so the gate
# is always a from-scratch build of the staged tree.
#
# Bypass with --no-verify only for a commit that cannot touch the build and
# when the build is known green.
set -e
ROOT="$(git rev-parse --show-toplevel)"

"$ROOT/tools/check_no_rom.sh"

# Staged C must be developer-native: no K&R definitions, no empty do-while
# loop notes, no empty asm, no symbol aliases, no register pins or asm blocks
# in functions the January listing proves were compiled C. tools/dev_native_allow.txt holds the
# ROM-proven exceptions with their reasons.
python3 "$ROOT/tools/check_dev_native.py"

# Staged C must be clang-formatted. Formatting can move bytes under -g, which
# the byte gate below catches. Fix with: tools/format.sh FILE
STAGED_C=$(git diff --cached --name-only --diff-filter=ACMR -z | tr '\0' '\n' |
    grep -E '^(ico2/[^/]+/[^/]+|sce(/[^/]+){0,3})/[^/]+\.c(\.inc)?$' || true)
if [[ -n "$STAGED_C" ]]; then
    # shellcheck disable=SC2086
    "$ROOT/tools/format.sh" --check $STAGED_C
fi

# Pure-docs commits (docs/, README.md, .gitignore, ...) skip the build gate.
BUILD_SENSITIVE=$(git diff --cached --name-only -z |
    tr '\0' '\n' |
    grep -E '^(ico2/|sce/|config/|tools/|baserom/)' ||
    true)
if [[ -z "$BUILD_SENSITIVE" ]]; then
    exit 0
fi

NINJA="$ROOT/.venv/bin/ninja"
if [[ ! -x "$NINJA" ]]; then
    NINJA="$(command -v ninja || true)"
fi
if [[ -z "$NINJA" ]]; then
    echo "pre-commit: ninja not on PATH and .venv/bin/ninja missing: run tools/setup.sh" >&2
    exit 1
fi

echo "pre-commit: tools/build.sh setup ..."
if ! "$ROOT/tools/build.sh" setup >/dev/null; then
    echo "" >&2
    echo "pre-commit: SETUP FAILED: the base ELF or ROM SHA-1 check or the" >&2
    echo "  build.ninja generation errored. Run \`tools/build.sh setup\` to see why." >&2
    exit 1
fi

echo "pre-commit: ninja (byte gate) ..."
if ! "$NINJA" -C "$ROOT"; then
    echo "" >&2
    echo "pre-commit: BUILD FAILED: the staged tree does not rebuild the base ELF" >&2
    echo "  byte for byte. Fix the build (or rebase) before committing." >&2
    echo "  Bypass with \`git commit --no-verify\` only if you understand why" >&2
    echo "  ninja is failing and have a follow-up commit ready that fixes it." >&2
    exit 1
fi
EOF
chmod +x "$HOOK"
echo "Installed pre-commit hook at $HOOK"

# pre-push: re-run the byte gate against the tip of each ref being
# pushed. Catches commits that bypassed pre-commit via --no-verify.
# `git push --no-verify` skips it in turn.
cat > "$PUSH_HOOK" <<'EOF'
#!/usr/bin/env bash
# Auto-installed by tools/install_hooks.sh. Per-push byte gate.
#
# For each ref being pushed whose commits touch the build, requires the
# working tree to be at that ref's tip with no uncommitted change to a
# build path, runs tools/build.sh setup + ninja
# (which ends in tools/check_elf.py --gate --require-elf-sha), and refuses the
# push if the rebuilt ELF differs from the base. This is the backstop against commits authored with
# `git commit --no-verify` that broke the byte-identical round-trip.
#
# Bypass with `git push --no-verify` ONLY when pushing a known-broken
# commit that has a follow-up fix queued.
set -e
ROOT="$(git rev-parse --show-toplevel)"

remote="$1"
url="$2"

NINJA="$ROOT/.venv/bin/ninja"
if [[ ! -x "$NINJA" ]]; then
    NINJA="$(command -v ninja || true)"
fi
if [[ -z "$NINJA" ]]; then
    echo "pre-push: ninja not on PATH and .venv/bin/ninja missing: run tools/setup.sh" >&2
    exit 1
fi

# Read refs from stdin (git push protocol): "<local-ref> <local-sha> <remote-ref> <remote-sha>"
push_failed=0
while read local_ref local_sha remote_ref remote_sha; do
    # Skip ref deletion (local_sha is all zeros)
    if [[ "$local_sha" =~ ^0+$ ]]; then
        continue
    fi
    # Check if any commits being pushed touch build-sensitive paths
    if [[ "$remote_sha" =~ ^0+$ ]]; then
        # New branch: check all commits in local_sha
        range="$local_sha"
    else
        range="$remote_sha..$local_sha"
    fi
    BUILD_SENSITIVE=$(git diff --name-only "$range" 2>/dev/null |
        grep -E '^(ico2/|sce/|config/|tools/|baserom/)' ||
        true)
    if [[ -z "$BUILD_SENSITIVE" ]]; then
        continue
    fi

    echo "pre-push: build-sensitive changes in $local_ref: re-running the byte gate ..."
    # The gate builds the working tree, so it must be the tip being pushed:
    # HEAD at that commit and no uncommitted change under a build path.
    head_sha=$(git rev-parse HEAD)
    if [[ "$head_sha" != "$local_sha" ]]; then
        echo "pre-push: working tree HEAD ($head_sha) doesn't match pushed ref tip ($local_sha)." >&2
        echo "  Check out the pushed commit and re-run \`tools/build.sh setup && ninja\` manually first." >&2
        push_failed=1
        continue
    fi
    if ! git diff --quiet HEAD -- ico2 sce config tools; then
        echo "pre-push: uncommitted changes under ico2/, sce/, config/ or tools/;" >&2
        echo "  the gate would build them instead of $local_sha. Commit or set them aside first." >&2
        push_failed=1
        continue
    fi
    if ! "$ROOT/tools/build.sh" setup >/dev/null 2>&1; then
        echo "pre-push: SETUP FAILED on $local_ref: refusing push." >&2
        push_failed=1
        continue
    fi
    if ! "$NINJA" -C "$ROOT" >/dev/null 2>&1; then
        echo "pre-push: BYTE GATE FAILED on $local_ref: refusing push." >&2
        echo "  The rebuilt ELF differs from the base. The commit being pushed" >&2
        echo "  was likely authored with \`git commit --no-verify\`. Fix the build" >&2
        echo "  (or rebase to drop the bad commit) before pushing." >&2
        push_failed=1
        continue
    fi
    echo "pre-push: byte gate OK on $local_ref"
done

exit $push_failed
EOF
chmod +x "$PUSH_HOOK"
echo "Installed pre-push hook at $PUSH_HOOK"
