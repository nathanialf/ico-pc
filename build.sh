#!/usr/bin/env bash
# build.sh: build the PAL boot ELF from a clean clone in one command.
#
#   1. tools/setup.sh, when the toolchain under tools/cc/ or the venv is
#      missing. Then a check for mips-linux-gnu-objcopy, a host package.
#   2. tools/extract_elf.sh, when baserom/pal/baseelf.elf is missing. It reads
#      baserom/Ico_PAL.iso, the user's own image of the PAL disc.
#   3. tools/build.sh setup when build.ninja is missing (delete build/, verify
#      the base ELF and ROM SHA-1s, write build.ninja); otherwise
#      tools/build.sh verify, the SHA-1s alone. build.ninja rewrites itself
#      when gen_ninja.py or its config inputs change.
#   4. ninja: compile, assemble and link build/ico.elf.
#      ninja does not track header or .c.inc dependencies: after editing one,
#      run tools/build.sh clean (or setup) before ./build.sh.
#
# Exits non-zero when any step fails. tools/build.sh keeps the individual
# subcommands (setup, regen, clean, distclean).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

ISO="baserom/Ico_PAL.iso"
BASEELF="baserom/pal/baseelf.elf"
NINJA=".venv/bin/ninja"

toolchain_ok() {
    [[ -x .venv/bin/python && -x "$NINJA" ]] &&
    [[ -x tools/cc/ee-gcc2.9-991111/ee-gcc ]] &&
    [[ -x tools/cc/ee-gcc2.96/bin/as ]] &&
    [[ -x tools/cc/binutils-2.10-ee/bin/ld ]] &&
    [[ -x tools/cc/dvp-as/bin/dvp-as ]]
}

if ! toolchain_ok; then
    echo "==> build.sh: toolchain incomplete, running tools/setup.sh"
    ICO_FROM_BUILD_SH=1 tools/setup.sh
    if ! toolchain_ok; then
        echo "build.sh: tools/setup.sh finished but the toolchain is still incomplete;" >&2
        echo "  see its output above (32-bit host libraries, network access)." >&2
        exit 1
    fi
fi

# The ROM view (build.ninja's rom rule) needs a host package setup.sh cannot
# install; say so before ninja fails on it.
if ! command -v mips-linux-gnu-objcopy >/dev/null 2>&1; then
    echo "build.sh: mips-linux-gnu-objcopy is not on PATH;" >&2
    echo "  install it (Debian/Ubuntu: sudo apt-get install binutils-mips-linux-gnu)." >&2
    exit 1
fi

if [[ ! -f "$BASEELF" ]]; then
    if [[ ! -f "$ISO" ]]; then
        cat >&2 <<EOF
build.sh: $BASEELF is missing and there is no disc image to extract it from.
  Copy your own image of the PAL disc (SCES-50760) to $ISO:
    mkdir -p baserom
    cp "/path/to/Ico (Europe).iso" $ISO
  then run ./build.sh again. Nothing under baserom/ is ever committed.
EOF
        exit 1
    fi
    echo "==> build.sh: extracting the base ELF from $ISO"
    tools/extract_elf.sh
fi

if [[ -f build.ninja ]]; then
    tools/build.sh verify
else
    tools/build.sh setup
fi
"$NINJA"
