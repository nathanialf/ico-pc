#!/usr/bin/env bash
# =============================================================================
# tools/dev_build.sh: build a few targets of one checkout and run a few tests.
#
#   tools/dev_build.sh <worktree-or-.> <target>... [-- <ctest args>]
#
#   tools/dev_build.sh . ico_pc rd_present_test -- -R rd_present
#   tools/dev_build.sh /path/to/worktree rd_blur_test -- -R '^rd_blur\.'
#   tools/dev_build.sh . ico_pc tests -- -L render -j4  (every test program, one area; -j4 is the gate's)
#   tools/dev_build.sh . ico_pc                        (build only)
#
# What it does, and nothing else:
#  1. configures <worktree>/build-host/linux-x64 with the linux-x64 preset once
#     and -DICO_LINK_EXE=ON, as CI and the main checkout do (a worktree with no baserom/ also gets -DICO_DISC_IMAGE=/nonexistent
#     -DICO_BASE_ELF=/nonexistent and the same for their _JPN and _KOR twins, so no test can start the game);
#  2. builds exactly the named targets at -j4 under flock on the shared build
#     lock (<worktree>/build-host/tmp/build.lock; ICO_BUILD_LOCK overrides);
#  3. runs ctest on that directory with the arguments after `--`
#     (-R <regex> or -L <label>; labels: render ui platform audio game data
#     rhi save tools), failing when they select no test, then prints a one-line
#     summary. ctest runs only what was built: a test whose program was not
#     named here reports "Not Run" and fails.
#
# The test programs are not part of the default build (docs/BUILDING.md,
# "Building one package"): name each one, or name `tests` for all of them.
# Compiles go through ccache (the preset's compiler launcher).
# =============================================================================
set -euo pipefail

usage() {
    sed -n '2,/^# ====/p' "$0" | sed 's/^# \{0,1\}//' | head -n -1 >&2
    exit 2
}
[[ $# -ge 2 ]] || usage

root="$(cd "$1" && pwd)"
shift
targets=()
while [[ $# -gt 0 && "$1" != "--" ]]; do
    targets+=("$1")
    shift
done
[[ $# -eq 0 ]] || shift # the --
ctest_args=("$@")
[[ ${#targets[@]} -gt 0 ]] || usage

dir="$root/build-host/linux-x64"
lock="${ICO_BUILD_LOCK:-$root/build-host/tmp/build.lock}"
mkdir -p "$(dirname "$lock")"

# the checkout's own CMake and Ninja when it has them (the preset puts .venv/bin on PATH)
cmake=cmake ctest=ctest
if [[ -x "$root/tools/toolchain/cmake/bin/cmake" ]]; then
    cmake="$root/tools/toolchain/cmake/bin/cmake"
    ctest="$root/tools/toolchain/cmake/bin/ctest"
fi
export PATH="$root/.venv/bin:$PATH"

cd "$root"
start=$SECONDS
if [[ ! -f "$dir/CMakeCache.txt" ]]; then
    extra=(-DICO_LINK_EXE=ON)
    if [[ ! -e "$root/baserom" ]]; then
        extra+=(-DICO_DISC_IMAGE=/nonexistent -DICO_BASE_ELF=/nonexistent
            -DICO_DISC_IMAGE_JPN=/nonexistent -DICO_BASE_ELF_JPN=/nonexistent
            -DICO_DISC_IMAGE_KOR=/nonexistent -DICO_BASE_ELF_KOR=/nonexistent)
    fi
    flock "$lock" "$cmake" --preset linux-x64 "${extra[@]}"
fi

echo "dev_build: building ${targets[*]} in $dir"
flock "$lock" "$cmake" --build "$dir" -j4 --target "${targets[@]}"
built=$((SECONDS - start))

if [[ ${#ctest_args[@]} -eq 0 ]]; then
    echo "dev_build: built ${targets[*]} in ${built}s (no tests requested)"
    exit 0
fi

tstart=$SECONDS
rc=0
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
"$ctest" --test-dir "$dir" --output-on-failure --no-tests=error "${ctest_args[@]}" 2>&1 | tee "$log" || rc=${PIPESTATUS[0]}
line="$(grep -E '[0-9]+% tests passed' "$log" | tail -n 1 || true)"
echo "dev_build: built ${targets[*]} in ${built}s; ctest ${ctest_args[*]}: ${line:-no summary} ($((SECONDS - tstart))s), exit $rc"
exit "$rc"
