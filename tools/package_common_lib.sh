# shellcheck shell=bash
# tools/package_common_lib.sh: the plumbing the three package scripts share
# (package_win.sh, package_linux.sh, package_android.sh), sourced from the
# script's own tools/ directory before anything else. Functions only; the
# script sets pkg_name (its name, the prefix of its messages) first.
#
#   pkg_begin <label>        the label check, then root (the repository) as
#                            the current directory, build-host/tmp and dist/
#                            made, TMPDIR inside build-host/
#   fail <message>           the failure line, the log's tail, exit 1 ($log)
#   run <command...>         the command's output to $log, fail if it fails
#   pkg_remove_worktree      remove the package worktree $wt
#   cleanup                  the EXIT trap's default: pkg_remove_worktree
#   pkg_make_worktree <commit>
#                            a clean worktree of <commit> at $wt with the
#                            untracked .venv and toolchain linked in, then
#                            ICO_PKG_FILES="path ..." copied over it (working-
#                            tree files, to try a change before it is
#                            committed; each logged as "overlay <path>")

pkg_begin() {
    label="${1:-}"
    if [[ ! "$label" =~ ^[A-Za-z0-9._-]+$ ]]; then
        echo "usage: tools/$pkg_name.sh <label>" >&2
        exit 2
    fi
    root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    cd "$root"
    mkdir -p build-host/tmp dist
    # temporary files stay inside build-host/
    export TMPDIR="$root/build-host/tmp"
}

fail() { echo "$pkg_name: FAILED: $1 (see $log)" >&2; tail -n 30 "$log" >&2; exit 1; }
run() { "$@" >>"$log" 2>&1 || fail "$*"; }

pkg_remove_worktree() {
    git -C "$root" worktree remove --force "$wt" >>"$log" 2>&1 || rm -rf "$wt"
    git -C "$root" worktree prune >>"$log" 2>&1 || true
}
cleanup() { pkg_remove_worktree; }

pkg_make_worktree() {
    local f
    cleanup
    run git worktree add --detach "$wt" "$1"
    ln -s "$root/.venv" "$wt/.venv"
    ln -s "$root/tools/toolchain" "$wt/tools/toolchain"
    for f in ${ICO_PKG_FILES:-}; do
        [[ -f "$root/$f" ]] || fail "ICO_PKG_FILES: no such file $f"
        mkdir -p "$(dirname "$wt/$f")"
        cp "$root/$f" "$wt/$f"
        echo "$pkg_name: overlay $f" >>"$log"
    done
}
