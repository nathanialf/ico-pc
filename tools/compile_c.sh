#!/usr/bin/env bash
# tools/compile_c.sh <src.c> <out.o>
#
# Compile one C source: ee-gcc 2.9-991111 to a .s, then the assembler of the
# source's archive to the object. The retired PS2 build's ninja rule ran it per
# object; tools/ee_identity.sh runs it now.

set -eu

[ $# -eq 2 ] || { echo "usage: $0 <src.c> <out.o>" >&2; exit 2; }
SRC="$1"
OUT="$2"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

EEGCC_DIR="${EEGCC_DIR:-${ROOT}/tools/cc/ee-gcc2.9-991111}"
EEGCC_LIB="${EEGCC_DIR}/gcc-lib/ee/2.9-ee-991111-01/"
CC="${EEGCC_DIR}/ee-gcc"

# TWO ASSEMBLERS, SELECTED PER ARCHIVE BY THE DISC'S LINK. MAIN.MAP takes
# libc.a, libm.a and libgcc.a from the studio's ee-gcc 2.9-991111-01 install and
# every other archive from Sony's SDK install (/usr/local/sce/ee/lib, version
# strings PsIIlib* 2200 and 2240 in the ELF). The game and the compiler-install
# libraries were assembled by the assembler bundled with that compiler,
# EE_AS_OLD: 142 game TUs and 12 libc/libm TUs give the ROM's bytes only under
# it. The SDK-install archives were compiled by a compiler code-identical to it (SCE's
# later 2.96 is ruled out by size) but assembled by a later gas that fills
# reorder-mode branch delay slots: sceGsSyncPath, sceScfSetT10kConfig and
# cmd_sem_init are the compiler's own output plus that swap, and all 58 archive
# TUs measured are byte-identical under both. EE_AS_SDK is SCE's own
# 2.10-ee-001003-1 assembler (tools/setup.sh fetches it), which reproduces all
# three; the bytes prove the behaviour, not which gas binary Sony's library
# build ran. The selection is by archive only, never per TU or per function.
# EE_AS_OLD's delay-slot reorder is less aggressive than 2.10's: it does not
# hoist a preceding unaligned store (sdl/sdr/...) into a `j <func>` tail-call
# delay slot, and 0 of the ROM's 783 tail calls carry one there.
EE_AS_OLD="${ROOT}/tools/cc/ee-gcc2.9-991111/bin/as"
# SDK-install archive assembler (see the paragraph above).
EE_AS_SDK="${ROOT}/tools/cc/ee-gcc2.96/bin/as"

# Small-data threshold. The game (ico2/) was built at -G 8. Every SDK archive
# under sce/ was built at -G 0: no SDK function in the ROM, in any of the twelve
# archives, makes a gp-relative access, the libm and libscf members give the
# ROM's bytes only at -G 0 (li.s expands to lui/ori/mtc1, short strings and NaNs land
# in .rodata rather than .sdata), and the whole tree is byte-identical with
# every sce/ member at -G 0 (measured 2026-09-16). A library's own build
# setting is a fact of that archive, not a per-function lever.
# -fno-builtin follows the install, as the assembler does. MAIN.MAP's LOAD
# list shows the game linked against prebuilt archives, never compiling them.
# The compiler-install libc.a, libm.a and libgcc.a carry newlib's own build
# flag: it keeps libm calling fabsf where a builtin would inline it, and six
# libm members (ef_asin, ef_atan2, ef_rem_pio2, sf_atan, wf_acos, wf_asin)
# change with builtins live. The SDK-install archives (/usr/local/sce/ee/lib)
# were built with builtins live: libpad's scePadStateIntToStr and
# scePadReqIntToStr copy "" with the lbu/sb pair that is gcc's inline
# strcpy of a constant string, which no -fno-builtin spelling emits (a
# constant-string read folds to a zero store), and every other SDK member
# (72 .c files, measured 2026-09-30 with objdump -dr) is byte-identical
# either way. The game compiled plain, which is what expands the aligned
# six-byte memcpy in layout_action (measured 2026-09-18).
# The same split decides -g: the studio compiled the game with line information and Sony's
# archives were built without it. Measured: Info-ZIP's plain huft_build text,
# the shape the listing's line map shows, gives the ROM's 498 words only with
# -g (a line note left after the deleted break keeps cse_around_loop off the
# body's load); every other game TU is byte-identical with or without
# it; the January listing, a build with line information for certain, agrees
# with the retail words on that region where the no-g build does not; and with
# -g the SDK archives lose the delay-slot fills their assembler gave them, so
# they were built without it. Per origin only, never per TU.
# The game also compiled -fno-common. MAIN.MAP's *(.scommon) rows come only
# from SDK and newlib objects (libscedemo.o's argv_copy/argc_copy, libgcc's
# _ctors.o, libc's sbrkr.o errno) and no game object sits in COMMON: the
# game's zero-valued globals are in their own TU's .sdata or .data, emitted
# after the rest of the file in first-declaration order, which is what a
# tentative definition does when it cannot be common (act-game's
# floorGObj_/wallGObj_ACTCheckCollis_*, isys's list heads, girl_act's GirlInfo
# and girlcalled at the ends of their runs). No game .s carried a .comm before
# the flag; with it every object kept its code and data bytes (measured
# 2026-09-30; the one object it moved was geometryManager's charGObjList, a
# static that a redundant later extern declaration turned from .bss into
# .data, and that declaration is gone).
case "${1:-}" in
    sce/libc/*|*/sce/libc/*|sce/libm/*|*/sce/libm/*|sce/libgcc/*|*/sce/libgcc/*)
        GNUM=0; BUILTIN="-fno-builtin"; DBG=""; COMMON="" ;;   # compiler-install archives
    sce/*|*/sce/*) GNUM=0; BUILTIN=""; DBG=""; COMMON="" ;;    # SDK-install archives
    *) GNUM=8; BUILTIN=""; DBG="-g"; COMMON="-fno-common" ;;
esac
# The SDK's and newlib's own public headers, reconstructed under sce/<archive>/
# by public naming (the members and the game TUs include them as <libdma.h>).
SCE_INCS=""
for _a in libc libm libvu0 libkernl libpkt libgraph libdma libpad libscf libmpeg libmc libipu libcdvd libsndn2; do
    SCE_INCS="${SCE_INCS} -I${ROOT}/sce/${_a}"
done
CFLAGS="-S ${DBG} ${COMMON} -G ${GNUM} -O2 -mips3 -EL ${BUILTIN} -nostdinc${SCE_INCS}"
# DUMP MODE: `DUMP_DIR=<dir> tools/compile_c.sh <src> <obj>` adds -da (every RTL
# pass dump) under the SAME per-origin flags and assembler, then moves the dumps gcc wrote
# beside the source into DUMP_DIR so nothing lands under ico2/ or sce/. DUMP_FLAGS may add
# further dump-only options (-d<letters> or -fsched-verbose-N); anything else is refused
# because it would change the code. The object is still produced and is a real measurement.
# Running cc1 or ee-gcc by hand would lose these flags.
if [ -n "${DUMP_DIR:-}" ]; then
    mkdir -p "${DUMP_DIR}"
    for _f in ${DUMP_FLAGS:-}; do
        case "${_f}" in
            -d[a-zA-Z]*|-fsched-verbose-[0-9]*) ;;
            *) echo "compile_c.sh: DUMP_FLAGS may only carry dump options (-d<letters>, -fsched-verbose-N), not '${_f}'" >&2; exit 2 ;;
        esac
    done
    CFLAGS="${CFLAGS} -da ${DUMP_FLAGS:-}"
    # gcc 2.9 writes <basename>.c.<pass> in the compile's working directory (the programmer
    # directory for ico2/, ito/ for ito, the member's directory for sce/): move them into
    # DUMP_DIR on exit, success or failure, so nothing is ever left under ico2/ or sce/.
    _collect_dumps() {
        [ -n "${DUMP_CWD:-}" ] && [ -n "${SRC_ABS:-}" ] || return 0
        _base="$(basename "${SRC_ABS}")"
        for _d in "${DUMP_CWD}/${_base}."*; do
            [ -f "${_d}" ] || continue
            case "${_d}" in *.c.[a-z0-9]*) mv -f "${_d}" "${DUMP_DIR}/" ;; esac
        done
    }
    trap _collect_dumps EXIT
fi
# -mabi=eabi is what the ee-gcc driver itself passes its assembler (its specs:
# `*abi_gas_asm_spec: %{mabi=*} %{!mabi=*:-mabi=eabi}`); both assemblers take it
# and it sets only the EF_MIPS_ABI_EABI64 bit (0x4000) of e_flags.
EE_ASFLAGS="-EL -mcpu=5900 -mabi=eabi -G ${GNUM}"

S="${OUT%.o}.s"

mkdir -p "$(dirname "${OUT}")"

# ico2/<programmer>/<kind>/<file>.c : compile it the way the original build
# did, from inside the programmer's own directory with RELATIVE -I entries to
# the sibling programmers' include dirs. ee-gcc bakes the spelling it is given
# into __FILE__, so both the source argument (`src/main.c`, not
# `ico2/common/src/main.c`) and the header spelling (`../ito/include/mv_defs.h`)
# have to match what the 2002 link recorded.
ICO2_PROG=""
case "${SRC}" in
    /*) echo "compile_c.sh: give the source as a repo-relative path (ico2/..., sce/...), run from the tree root; got '${SRC}'" >&2; exit 2 ;;
    ico2/*/*) ICO2_PROG="${SRC#ico2/}"; ICO2_PROG="${ICO2_PROG%%/*}" ;;
esac
if [ -n "${ICO2_PROG}" ]; then
    SRC_REL="${SRC#ico2/${ICO2_PROG}/}"
    SRC_ABS="${SRC}"; case "${SRC_ABS}" in /*) ;; *) SRC_ABS="${ROOT}/${SRC_ABS}";; esac
    S_ABS="${S}"; case "${S_ABS}" in /*) ;; *) S_ABS="${ROOT}/${S_ABS}";; esac
    # Search order: the programmer's own include dir first, then the
    # cross-programmer dirs the listing shows their TUs reaching into, then
    # (from CFLAGS) the sce/ archive headers.
    ICO2_INCS=""
    for _p in "${ICO2_PROG}" sugipon omori common ito fumi seki script; do
        [ -d "${ROOT}/ico2/${_p}/include" ] || continue
        case " ${ICO2_INCS} " in *" -I../${_p}/include "*) continue ;; esac
        ICO2_INCS="${ICO2_INCS} -I../${_p}/include"
    done
    # shellcheck disable=SC2086
    DUMP_CWD="${ROOT}/ico2/${ICO2_PROG}"
    ( cd "${ROOT}/ico2/${ICO2_PROG}" \
      && "${ROOT}/tools/period_env.sh" "${CC}" -B "${EEGCC_LIB}" ${ICO2_INCS} ${CFLAGS} -o "${S_ABS}" "${SRC_REL}" )
else
    # sce/<archive>/<member>.c : the vendor archives were built member by member
    # from inside the member's own directory, so __FILE__ is the bare name
    # (measured 2026-09-16: libscf.o's assert strings carry "libscf.c").
    SRC_ABS="${SRC}"; case "${SRC_ABS}" in /*) ;; *) SRC_ABS="${ROOT}/${SRC_ABS}";; esac
    S_ABS="${S}";    case "${S_ABS}"   in /*) ;; *) S_ABS="${ROOT}/${S_ABS}";; esac
    # shellcheck disable=SC2086
    DUMP_CWD="$(dirname "${SRC_ABS}")"
    ( cd "$(dirname "${SRC_ABS}")" && "${ROOT}/tools/period_env.sh" "${CC}" -B "${EEGCC_LIB}" ${CFLAGS} -o "${S_ABS}" "$(basename "${SRC_ABS}")" )
fi

# Dump mode: the dumps are moved out by the EXIT trap armed above (it runs whether or not the
# compile or the assembly succeeded, so a failed run never leaves <basename>.c.<pass> files in
# the tree).
if [ -n "${DUMP_DIR:-}" ]; then
    echo "compile_c.sh: dumps in ${DUMP_DIR}" >&2
fi

# The assembler reads cc1's .s as it is: no step rewrites compiler or assembler
# output. The ROM's encodings that once looked like rewrites are the period
# assembler's own: ee-as 2.9-991111 encodes `move` as `daddu $r,$s,$0`, puts a
# single-operand `break N` code in the low field, emits `cvt.w.s` as the ROM's
# COP1 word (function 0x24, which modern objdump prints as trunc.w.s), and takes
# the VU0 registers ACC, Q and R spelled bare. The `nop` the ROM has in the
# `jr $31` slot after a game inline-asm block comes from the source: the game's
# VU0 template wraps its body in `.set noreorder` / `.set reorder`
# (ico2/common/include/typedef.h, ico2/seki/src/Matrix.c), while sce/libvu0's
# own templates do not and carry `sqc2` in 26 of their return slots.

# Assembler by archive, as described at EE_AS_OLD above: the game and the
# compiler-install libraries (libc, libm, libgcc) on the assembler bundled with
# the compiler, the SDK-install archives on SCE's 2.10-ee assembler.
case "${SRC}" in
    sce/libc/*|*/sce/libc/*|sce/libm/*|*/sce/libm/*|sce/libgcc/*|*/sce/libgcc/*)
        SELECTED_EE_AS="${EE_AS_OLD}" ;;   # compiler-install archives (MAIN.MAP)
    sce/*|*/sce/*)
        SELECTED_EE_AS="${EE_AS_SDK}" ;;   # SDK-install archives (/usr/local/sce/ee/lib)
    *)
        SELECTED_EE_AS="${EE_AS_OLD}" ;;   # the game
esac

# The selected assembler is the only one for this source, with no fallback. A
# modern gas fills delay slots that ee-as 2.9-991111 leaves bare, and the
# game's slots are bare in the ROM. If the assembler rejects the .s, the defect
# is in the source; fail so ninja stops.
# shellcheck disable=SC2086
if "${ROOT}/tools/period_env.sh" "${SELECTED_EE_AS}" ${EE_ASFLAGS} -o "${OUT}" "${S}" 2>"${OUT}.aserr"; then
    rm -f "${OUT}.aserr"
else
    echo "compile_c.sh: assembler ${SELECTED_EE_AS} REJECTED ${S}" >&2
    grep -iE 'error' "${OUT}.aserr" | head -20 >&2 || head -20 "${OUT}.aserr" >&2
    rm -f "${OUT}.aserr" "${OUT}"
    echo "  Fix the source; the assembler is chosen by archive and has no fallback." >&2
    exit 1
fi
