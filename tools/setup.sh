#!/usr/bin/env bash
# =============================================================================
# tools/setup.sh
#
# One-shot host setup. Idempotent: running it twice is fine.
#
#   1. Create a Python venv at .venv and install tools/requirements.txt.
#   2. Fetch the period compilers (ee-gcc 2.9-991111 and ee-gcc 2.96, for its
#      SCE 2.10 assembler), check for a MIPS objcopy, and build the period
#      linker (GNU ld 2.10) and dvp-as from public source.
#   3. Install the git hooks (tools/install_hooks.sh).
#
# This script downloads no disc data, no assets, and no ICO-specific files.
# Everything pulled is from open-source projects (GNU binutils, ps2dev,
# decompme/compilers).
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

# --- 2b. MIPS objcopy (baserom/pal/baseelf.rom, the ROM view of the base ELF)

if [[ "${SKIP_TOOLCHAIN:-0}" == "1" ]]; then
    :
elif command -v mips-linux-gnu-objcopy >/dev/null 2>&1; then
    echo "==> using mips-linux-gnu-objcopy"
else
    cat <<'EOF' >&2

==> mips-linux-gnu-objcopy is not on PATH.

Quickest fix on Debian/Ubuntu:

    sudo apt-get install binutils-mips-linux-gnu

That gives you mips-linux-gnu-objcopy, which writes the ROM view of the
extracted base ELF. Assembly and linking are not its job: every object is assembled by
the period assemblers under tools/cc/, fetched above, and the link is GNU ld
2.10, built below.

EOF
fi

# --- 2c. Period linker and DVP assembler, built from public source ---------
#
# The build (tools/gen_ninja.py) links with the linker era of the retail link
# and assembles the VU microprograms with a DVP assembler. Both are
# built here from public GPL source; no SDK file is fetched or used.
#
#   ld 2.10 + EE/DVP patch   GNU binutils 2.10 release tarball,
#                            https://ftp.gnu.org/gnu/binutils/binutils-2.10.tar.gz
#                            (GPL-2.0-or-later), sha256 pinned below, with
#                            tools/binutils-2.10-ee.patch applied (the R5900
#                            machine and the DVP overlay section types,
#                            backported from ps2dev's binutils-2.14-PS2.patch,
#                            github.com/ps2dev/ps2toolchain commit aa984e7),
#                            and tools/binutils-2.10-dvp-ld.patch (the Cygnus
#                            "sky" ld's rule placing each .DVP.overlay.*
#                            orphan at address 0, from the GPL ee-gcc
#                            2.9-991111 combined tree).
#                            Target mipsel-elf: its default output vector is
#                            elf32-littlemips, the one MAIN.MAP names.
#   dvp-as                   ps2dev's binutils-gdb, branch dvp-v2.45.1
#                            (GPL-3.0-or-later, the Cygnus "sky" DVP port
#                            carried forward), pinned to one commit below,
#                            configured --target=dvp, gas only.
#
# Neither tree needs bison, flex or makeinfo on the host. The 2.10 release
# tarball ships its generated parsers (ld/ldgram.c, ld/ldlex.c and the
# binutils/ ones); its configure still probes for lex and yacc and stops when
# neither exists, so the cache variables below answer the probe and make never
# regenerates the shipped files. The dvp target's gas has no generated parser
# at all (the itbl ones are MIPS-only), and MAKEINFO=true skips the manuals.
# The 2.10 tree's config.sub/config.guess predate x86_64 hosts; the dvp
# clone's copies (GNU config, same license) replace them.
#
# Measured on a 4-core host: dvp fetch ~20 s, dvp-as build ~30 s, ld 2.10
# build ~17 s. Rebuilt when the pinned commit or the patch changes (stamp).

BU210_URL="${BU210_URL:-https://ftp.gnu.org/gnu/binutils/binutils-2.10.tar.gz}"
BU210_SHA256="fd7d227c0dd15cf5448385e56b8ad8313cd491839834b57c0c086ac7b7819a15"
BU210_PATCH="$ROOT/tools/binutils-2.10-ee.patch"
BU210_DVP_PATCH="$ROOT/tools/binutils-2.10-dvp-ld.patch"
BU210_DIR="$ROOT/tools/cc/binutils-2.10-ee"
DVP_REPO="${DVP_REPO:-https://github.com/ps2dev/binutils-gdb}"
DVP_COMMIT="3eb45ea37f0efd498d1de3cf9562de07197aefa8"   # dvp-v2.45.1 "DVP changes"
DVP_DIR="$ROOT/tools/cc/dvp-as"
CCSRC="$ROOT/tools/cc/src"

build_dvp_as() {
    local stamp="$DVP_DIR/.stamp" want="$DVP_COMMIT"
    if [[ -x "$DVP_DIR/bin/dvp-as" && "$(cat "$stamp" 2>/dev/null)" == "$want" ]]; then
        echo "==> dvp-as already at $DVP_DIR"
        return 0
    fi
    echo "==> fetching ps2dev binutils-gdb $DVP_COMMIT (dvp-v2.45.1)"
    rm -rf "$CCSRC/dvp-binutils" "$CCSRC/build-dvp"
    mkdir -p "$CCSRC/dvp-binutils" "$CCSRC/build-dvp" "$DVP_DIR/bin"
    git -C "$CCSRC/dvp-binutils" init -q
    git -C "$CCSRC/dvp-binutils" fetch -q --depth 1 "$DVP_REPO" "$DVP_COMMIT"
    git -C "$CCSRC/dvp-binutils" checkout -q FETCH_HEAD
    echo "==> building dvp-as"
    ( cd "$CCSRC/build-dvp" &&
      ../dvp-binutils/configure --target=dvp --prefix="$DVP_DIR" \
          --disable-nls --disable-werror --disable-gdb --disable-gdbserver \
          --disable-sim --disable-gprof --disable-gprofng --disable-libdecnumber \
          --disable-readline --without-zstd > configure.log 2>&1 &&
      make -j"$(nproc)" all-gas MAKEINFO=true > make.log 2>&1 ) ||
        { echo "==> dvp-as build failed; see $CCSRC/build-dvp/*.log" >&2; return 1; }
    cp "$CCSRC/build-dvp/gas/as-new" "$DVP_DIR/bin/dvp-as"
    echo "$want" > "$stamp"
    echo "==> dvp-as installed at $DVP_DIR/bin/dvp-as"
}

build_ld210() {
    local stamp="$BU210_DIR/.stamp" want
    want="$BU210_SHA256 $(cat "$BU210_PATCH" "$BU210_DVP_PATCH" | sha256sum | cut -d' ' -f1)"
    if [[ -x "$BU210_DIR/bin/ld" && "$(cat "$stamp" 2>/dev/null)" == "$want" ]]; then
        echo "==> ld 2.10 already at $BU210_DIR"
        return 0
    fi
    mkdir -p "$CCSRC" "$BU210_DIR/bin"
    local tgz="$CCSRC/binutils-2.10.tar.gz"
    if [[ ! -f "$tgz" ]] || ! echo "$BU210_SHA256  $tgz" | sha256sum -c --status; then
        echo "==> fetching GNU binutils 2.10"
        curl -fsSL -o "$tgz" "$BU210_URL"
        echo "$BU210_SHA256  $tgz" | sha256sum -c --status ||
            { echo "==> binutils-2.10.tar.gz sha256 mismatch" >&2; return 1; }
    fi
    echo "==> building ld 2.10 with tools/binutils-2.10-ee.patch and tools/binutils-2.10-dvp-ld.patch"
    rm -rf "$CCSRC/binutils-2.10" "$CCSRC/build-ld210"
    tar -xzf "$tgz" -C "$CCSRC"
    ( cd "$CCSRC/binutils-2.10" && patch -s -p1 < "$BU210_PATCH" &&
      patch -s -p1 < "$BU210_DVP_PATCH" ) || return 1
    cp "$CCSRC/dvp-binutils/config.sub" "$CCSRC/dvp-binutils/config.guess" "$CCSRC/binutils-2.10/"
    mkdir -p "$CCSRC/build-ld210"
    ( cd "$CCSRC/build-ld210" &&
      ac_cv_prog_lex_root=lex.yy ac_cv_prog_LEX=flex ac_cv_prog_YACC="bison -y" LEXLIB= \
      CC="gcc -std=gnu89 -fcommon -w -g -O1" \
          ../binutils-2.10/configure --target=mipsel-elf --disable-nls > configure.log 2>&1 &&
      make -j"$(nproc)" all-ld MAKEINFO=true > make.log 2>&1 ) ||
        { echo "==> ld 2.10 build failed; see $CCSRC/build-ld210/*.log" >&2; return 1; }
    cp "$CCSRC/build-ld210/ld/ld-new" "$BU210_DIR/bin/ld"
    echo "$want" > "$stamp"
    echo "==> ld 2.10 installed at $BU210_DIR/bin/ld"
}

if [[ "${SKIP_TOOLCHAIN:-0}" == "1" ]]; then
    echo "==> SKIP_TOOLCHAIN=1; not building ld 2.10 / dvp-as"
else
    # dvp-as first: the 2.10 build takes its config.sub/config.guess.
    build_dvp_as && build_ld210 || echo "==> linker / dvp-as build incomplete" >&2
fi

# --- 3. git hooks --------------------------------------------------------------

if [[ -d .git ]]; then
    bash tools/install_hooks.sh
else
    echo "==> not a git repo yet; skipping hook install"
fi

echo
# Run from ../build.sh, which carries on with the extraction and the build
# itself: the next steps below would only repeat what it is about to do.
if [[ "${ICO_FROM_BUILD_SH:-0}" == "1" ]]; then
    echo "Setup complete; build.sh continues."
    exit 0
fi
echo "Setup complete. Next steps:"
# The disc image the extractor wants depends on this branch's target: PAL retail
# (main) reads a plain ISO, USA retail (ntsc) a .bin/.cue pair. Ask
# tools/ico_version.sh which target this working tree carries.
# shellcheck source=tools/ico_version.sh
. "$(dirname "$0")/ico_version.sh"
ico_version_init "$(cd "$(dirname "$0")/.." && pwd)"
case "$ICO_VERSION" in
    pal)  echo "  1. Place your disc image at baserom/Ico_PAL.iso" ;;
    us)   echo "  1. Place your disc image at baserom/Ico_USA.bin (+ .cue)" ;;
    *)    echo "  1. Place your $ICO_VERSION disc image under baserom/" ;;
esac
echo "  2. Run './build.sh' (or 'tools/extract_elf.sh', then"
echo "     'tools/build.sh setup && .venv/bin/ninja') to extract $ICO_BASEELF and build"
