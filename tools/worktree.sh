#!/usr/bin/env bash
# tools/worktree.sh add <id> | drop <id>
#
# add: a git worktree /primary/dev/ico-pc-wt/<id> on a new branch pkg/<id>
#      from main, with the untracked inputs a fresh worktree needs symlinked
#      to the main checkout: baserom, tools/toolchain, .venv and
#      build-host/tmp (locks, scratch). Build in <worktree>/build-host/.
# drop: removes the worktree; deletes pkg/<id> only if it is merged into main.
# See docs/BUILDING.md ("Worktrees").
set -euo pipefail

main=/primary/dev/ico-pc
base=/primary/dev/ico-pc-wt
cmd="${1:-}"
id="${2:-}"
if [[ ! "$cmd" =~ ^(add|drop)$ || ! "$id" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "usage: tools/worktree.sh add|drop <id>" >&2
    exit 2
fi
wt="$base/$id"
branch="pkg/$id"

case "$cmd" in
add)
    [[ ! -e "$wt" && ! -L "$wt" ]] || { echo "worktree: $wt already exists; use another id or drop it first" >&2; exit 1; }
    if git -C "$main" show-ref --verify --quiet "refs/heads/$branch"; then
        echo "worktree: branch $branch already exists" >&2; exit 1
    fi
    mkdir -p "$base"
    git -C "$main" worktree add -b "$branch" "$wt" main
    mkdir -p "$wt/build-host" "$main/build-host/tmp"
    ln -s "$main/baserom" "$wt/baserom"
    ln -s "$main/tools/toolchain" "$wt/tools/toolchain"
    ln -s "$main/.venv" "$wt/.venv"
    ln -s "$main/build-host/tmp" "$wt/build-host/tmp"
    echo "worktree: $wt on $branch"
    ;;
drop)
    [[ -e "$wt" ]] || { echo "worktree: $wt does not exist" >&2; exit 1; }
    git -C "$main" worktree remove --force "$wt"
    git -C "$main" worktree prune
    if git -C "$main" show-ref --verify --quiet "refs/heads/$branch"; then
        if git -C "$main" merge-base --is-ancestor "$branch" main; then
            git -C "$main" branch -d "$branch"
        else
            echo "worktree: removed $wt, but $branch is not merged into main; branch kept" >&2
            exit 1
        fi
    fi
    ;;
esac
