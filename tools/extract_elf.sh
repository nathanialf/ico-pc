#!/usr/bin/env bash
# =============================================================================
# tools/extract_elf.sh
#
# Extracts the target's EE-side boot ELF from the user's ICO disc image and
# records its SHA-1 to config/sha1sums.<slug>.txt (creating the file or the
# entry on first run, verifying against the existing entry on subsequent
# runs). The target is VERSION, or the first configured one
# (tools/ico_version.py, whose DISCS table names each target's disc):
#   pal  : baserom/Ico_PAL.iso         -> baserom/pal/baseelf.elf
#                                         + MAIN.MAP SRCFILE.TXT
#                                           TRFILE.TXT SYSTEM.CNF
#   jpn  : baserom/Ico_JPN.bin + .cue  -> baserom/jpn/baseelf.elf
#                                         + SYSTEM.CNF
#   kor  : baserom/Ico_KOR.iso         -> baserom/kor/baseelf.elf
#                                         + SYSTEM.CNF
#   cn   : baserom/Ico_CN.iso          -> baserom/cn/baseelf.elf
#                                         + SYSTEM.CNF
#   ntsc : baserom/Ico_USA.bin + .cue  -> baserom/ntsc/baseelf.elf
#                                         + SYSTEM.CNF
#   aug6 : baserom/Ico_AUG6.bin + .cue -> baserom/aug6/baseelf.elf
#                                         + MAIN.MAP TRFILE.TXT
#                                           SYSTEM.CNF
#   jun26: baserom/Ico_JUN26.iso       -> baserom/jun26/baseelf.elf
#                                         + SYSTEM.CNF
# The boot file name comes from the disc's SYSTEM.CNF BOOT2 line, or for a
# demo on a disc shared with other games from its DISCS row's `boot`
# (jun26: CUS97113/SCPS_000.00; SYSTEM.CNF boots the disc's menu).
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
