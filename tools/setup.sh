#!/usr/bin/env bash
# =============================================================================
# tools/setup.sh
#
# One-shot host setup. Idempotent: running it twice is fine.
#
#   1. Create a Python venv at .venv and install tools/requirements.txt.
#   2. Fetch the period compilers (ee-gcc 2.9-991111 and ee-gcc 2.96, for its
#      SCE 2.10 assembler) and check for a MIPS objdump/objcopy. They are the
#      optional EE identity check's (tools/ee_identity.sh); the port's own
#      build needs none of it (docs/BUILDING.md).
#   3. Install the git hooks (tools/install_hooks.sh).
#
# This script downloads no disc data, no assets, and no ICO-specific files.
# Everything pulled is from decompme/compilers.
#
# Skip flag:
#   SKIP_TOOLCHAIN=1   skip step 2 (the period compilers)
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PYTHON="${PYTHON:-python3}"
VENV="${VENV:-.venv}"

echo "==> ico setup at $ROOT"

# --- 1. Python venv ----------------------------------------------------------

if [[ ! -d "$VENV" ]]; then
    echo "==> creating venv at $VENV"
    "$PYTHON" -m venv "$VENV"
fi
# shellcheck disable=SC1091
source "$VENV/bin/activate"
python -m pip install --upgrade pip >/dev/null
python -m pip install -r tools/requirements.txt

# --- 2. EE GCC 2.9-991111 (the game's compiler, and its ee-as 2.9-991111
#       for the game, libc, libm and libgcc) and ee-gcc 2.96 (only its
#       bundled SCE 2.10 assembler, for the SDK-install sce/ archives) ----

EEGCC29_DIR="$ROOT/tools/cc/ee-gcc2.9-991111"
EEGCC29_BIN="$EEGCC29_DIR/ee-gcc"
EEGCC29_URL="${EEGCC29_URL:-https://github.com/decompme/compilers/releases/download/compilers/ee-gcc2.9-991111-01.tar.xz}"

EEGCC96_DIR="$ROOT/tools/cc/ee-gcc2.96"
EEGCC96_AS="$EEGCC96_DIR/bin/as"
EEGCC96_URL="${EEGCC96_URL:-https://github.com/decompme/compilers/releases/download/compilers/ee-gcc2.96.tar.xz}"

fetch_compiler() {
    local label="$1" dir="$2" url="$3" sentinel="$4"
    if [[ "${SKIP_TOOLCHAIN:-0}" == "1" ]]; then
        echo "==> SKIP_TOOLCHAIN=1; not fetching $label"
    elif [[ -f "$sentinel" ]]; then
        echo "==> $label already at $dir"
    elif command -v curl >/dev/null 2>&1; then
        echo "==> fetching $label from decompme/compilers"
        mkdir -p "$dir"
        if curl -fsSL "$url" | tar -xJ -C "$dir" --strip-components=1; then
            echo "==> $label extracted to $dir"
        else
            echo "==> $label fetch failed; install manually" >&2
            rm -rf "$dir"
        fi
    else
        echo "==> curl not available; skipping $label"
    fi
}

fetch_compiler "ee-gcc 2.9-991111" "$EEGCC29_DIR" "$EEGCC29_URL" "$EEGCC29_BIN"
fetch_compiler "ee-gcc 2.96 (for ee-as 2.10)" "$EEGCC96_DIR" "$EEGCC96_URL" "$EEGCC96_AS"

# Both compilers are 32-bit i386 ELFs. On a 64-bit Linux host they
# require multilib / 32-bit libc; check and warn so failures are obvious.
EEGCC_BIN="$EEGCC29_BIN"
if [[ -f "$EEGCC_BIN" ]] && ! "$EEGCC_BIN" --version >/dev/null 2>&1; then
    cat >&2 <<EOF
==> ee-gcc 2.9-991111 is installed but won't run on this host.
    It's a 32-bit i386 binary; you need 32-bit libc support, e.g.:

      sudo dpkg --add-architecture i386
      sudo apt-get update
      sudo apt-get install libc6:i386 libstdc++6:i386 zlib1g:i386

    Then re-run tools/setup.sh to verify.
EOF
fi

# --- 2b. MIPS binutils (the EE identity check's objdump; objcopy writes
#         baserom/pal/baseelf.rom, the ROM view of the base ELF)

if [[ "${SKIP_TOOLCHAIN:-0}" == "1" ]]; then
    :
elif command -v mips-linux-gnu-objcopy >/dev/null 2>&1; then
    echo "==> using mips-linux-gnu-objcopy"
else
    cat <<'EOF' >&2

==> mips-linux-gnu-objcopy is not on PATH.

Quickest fix on Debian/Ubuntu:

    sudo apt-get install binutils-mips-linux-gnu

That gives you mips-linux-gnu-objdump (tools/ee_identity.sh) and
mips-linux-gnu-objcopy (the ROM view of the extracted base ELF). Assembly is
not their job: every object is assembled by the period assemblers under
tools/cc/, fetched above.

EOF
fi

# --- 3. git hooks --------------------------------------------------------------

if [[ -d .git ]]; then
    bash tools/install_hooks.sh
else
    echo "==> not a git repo yet; skipping hook install"
fi

echo
echo "Setup complete. The port builds with CMake (docs/BUILDING.md); the period"
echo "compilers under tools/cc/ are for tools/ee_identity.sh only."
