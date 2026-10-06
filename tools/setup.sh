#!/usr/bin/env bash
# =============================================================================
# tools/setup.sh
#
# One-shot host setup. Idempotent: running it twice is fine.
#
#   1. Create a Python venv at .venv and install tools/requirements.txt.
#   2. Check for a MIPS objcopy (the ROM view of the extracted base ELF,
#      for the maintainers' tools; the port's own build needs none of it,
#      docs/BUILDING.md).
#   3. Install the git hooks (tools/install_hooks.sh).
#
# This script downloads no disc data, no assets, and no ICO-specific files.
#
# Skip flag:
#   SKIP_TOOLCHAIN=1   skip step 2
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

# --- 2. MIPS binutils (objcopy writes baserom/pal/baseelf.rom, the ROM view
#        of the base ELF)

if [[ "${SKIP_TOOLCHAIN:-0}" == "1" ]]; then
    :
elif command -v mips-linux-gnu-objcopy >/dev/null 2>&1; then
    echo "==> using mips-linux-gnu-objcopy"
else
    cat <<'EOF' >&2

==> mips-linux-gnu-objcopy is not on PATH.

Quickest fix on Debian/Ubuntu:

    sudo apt-get install binutils-mips-linux-gnu

That gives you mips-linux-gnu-objcopy (the ROM view of the extracted base
ELF).

EOF
fi

# --- 3. git hooks --------------------------------------------------------------

if [[ -d .git ]]; then
    bash tools/install_hooks.sh
else
    echo "==> not a git repo yet; skipping hook install"
fi

echo
echo "Setup complete. The port builds with CMake (docs/BUILDING.md)."
