# shellcheck shell=bash
# =============================================================================
# tools/fetch_common.sh
#
# The helpers tools/fetch_deps.sh and tools/fetch_android.sh share. Sourced,
# not run.
# =============================================================================

# fetch <url> <sha256> <file>: download and verify.
fetch() {
    echo "==> fetching $1"
    curl -fL --retry 3 -o "$3" "$1"
    echo "${2}  $3" | sha256sum -c -
}

# stamped <dir> <id>: true when <dir> already holds release <id>.
stamped() {
    [[ -f "$1/.ico-release" && "$(cat "$1/.ico-release")" == "$2" ]]
}
