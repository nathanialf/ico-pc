#!/usr/bin/env bash
# =============================================================================
# tools/extract_elf.sh
#
# Extracts this branch's EE-side boot ELF from the user's ICO disc image and
# records its SHA-1 to config/sha1sums.txt (creating the entry on first run,
# verifying against the existing entry on subsequent runs). Which disc and
# where the outputs land follow the branch's target (tools/ico_version.py):
#   main / pal : baserom/Ico_PAL.iso        -> baserom/pal/baseelf.elf
#                                              + MAIN.MAP SRCFILE.TXT
#                                                TRFILE.TXT SYSTEM.CNF
#   ntsc / us  : baserom/Ico_USA.bin + .cue -> baserom/baseelf.elf
# The boot file name comes from the disc's SYSTEM.CNF BOOT2 line.
#
# Wrapper that delegates to tools/extract_elf.py: pure-Python implementation
# that handles MODE1/2352 and MODE2/2352 .bin/.cue images (and already-cooked
# 2048-byte-sector ISOs) without external CD tools (bchunk / isoinfo / 7z),
# via pycdlib.
#
# Reads no copyrighted material into the repository: the ELF lives under
# baserom/, which is gitignored.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VENV_PY="$ROOT/.venv/bin/python"
if [[ ! -x "$VENV_PY" ]]; then
    echo "extract_elf: $VENV_PY not found: run tools/setup.sh first" >&2
    exit 1
fi

exec "$VENV_PY" "$ROOT/tools/extract_elf.py" "$@"
