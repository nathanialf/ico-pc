#!/usr/bin/env bash
# tools/build.sh: top-level orchestration for the ICO decomp.
#
# The target slug and the base ELF/ROM paths come from tools/ico_version.sh
# (`main` = PAL retail, slug pal). Override with VERSION=<slug> for ad-hoc runs.
#
# Inner-loop build is `ninja` (or `.venv/bin/ninja` if not on PATH): every
# object from its own source, linked by config/link.pal.ld in the order of
# config/link_order.pal.txt (tools/gen_ninja.py writes build.ninja). This
# script only handles the one-shots that do not belong in the build graph.
#
# Subcommands:
#   setup       Delete build/, verify the base ELF and ROM SHA-1s, write build.ninja.
#   verify      Just verify the base ELF and ROM SHA-1s.
#   regen       Just rewrite build.ninja.
#   clean       rm -rf build/.
#   distclean   clean + remove build.ninja and ninja's state.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

VENV_PY="${ROOT}/.venv/bin/python"

# shellcheck source=tools/ico_version.sh
. "${ROOT}/tools/ico_version.sh"
ico_version_init "${ROOT}"
VERSION="${ICO_VERSION}"
export VERSION
BASEELF="${BASEELF:-${ICO_BASEELF}}"
BASEROM="${BASEROM:-${ICO_ROM}}"

regen_ninja() {
    echo "==> writing build.ninja"
    "${VENV_PY}" tools/gen_ninja.py
}

verify_base() {
    echo "==> verifying the base ELF and ROM SHA-1s"
    "${VENV_PY}" tools/verify_elf.py --target "${BASEELF}"
    "${VENV_PY}" tools/verify_elf.py --target "${BASEROM}"
}

setup() {
    echo "==> clean build/ (full rebuild)"
    rm -rf build .ninja_log .ninja_deps
    verify_base
    regen_ninja
}

do_clean() {
    rm -rf build
}

do_distclean() {
    do_clean
    rm -f build.ninja .ninja_log .ninja_deps
}

cmd="${1:-help}"
case "$cmd" in
    setup)      setup ;;
    verify)     verify_base ;;
    regen)      regen_ninja ;;
    clean)      do_clean ;;
    distclean)  do_distclean ;;
    help|*)
        cat <<EOF
usage: $0 <subcommand>

  setup       delete build/, verify the base ELF and ROM, write build.ninja
  verify      verify the base ELF and ROM SHA-1s only
  regen       rewrite build.ninja from config/link_order.${VERSION}.txt
  clean       rm -rf build/
  distclean   clean + delete build.ninja and ninja's state

Build with: ninja
EOF
        ;;
esac
