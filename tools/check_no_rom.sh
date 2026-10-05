#!/usr/bin/env bash
# =============================================================================
# check_no_rom.sh
#
# IP-safety scan. Refuses to let disc data, extracted assets, or other
# IP-tainted files slip into a commit. Run before pushing; the pre-commit
# hook runs it on staged files.
#
# Heuristics (intentionally conservative: false positives are fine, false
# negatives are not):
#
#   1. File extensions associated with PS2 disc dumps and extracted assets.
#   2. PS2 boot-ELF naming patterns (SLUS-/SLES-/SLPS-/SLPM-/SCUS-/...) and
#      the PAL disc's reference files (MAIN.MAP, SRCFILE.TXT, TRFILE.TXT,
#      TRTABLE.BIN, SYSTEM.CNF), which docs/LEGAL.md keeps out of the tree.
#   3. Any tracked file > 256 KiB (C sources under ico2/ and sce/: 8 MiB),
#      except the named text files below.
#   4. ELF magic (\x7fELF) sniff regardless of extension.
#   5. Raw byte-array initializers in tracked C (D_<VMA> names), and any
#      large all-integer-literal array initializer in ico2/ and sce/ whatever
#      its name (tools/check_int_arrays.py, with a per-table reviewed
#      exemption list).
#   6. Any file whose path matches our gitignore patterns but is somehow
#      tracked anyway (checked against the patterns, not the index).
#
# Exit non-zero on any hit.
# =============================================================================
set -euo pipefail

# Files to inspect: staged ones if running pre-commit, otherwise everything
# tracked by git. Override with FILES env var for ad-hoc scans.
if [[ -n "${FILES:-}" ]]; then
    mapfile -t files < <(printf "%s\n" "$FILES" | tr ' ' '\n')
elif git diff --cached --name-only --quiet 2>/dev/null; then
    mapfile -t files < <(git ls-files)
else
    mapfile -t files < <(git diff --cached --name-only --diff-filter=ACMR)
fi

bad=0
note() { echo "check_no_rom: $*" >&2; bad=1; }

# --- 1. extension blocklist ---
# .pss/.ipu/.m2v: the disc's movie streams and their video elementary
# streams; .img/.dat: generic disc and memory-card image dumps.
ext_block_re='\.(bin|iso|cue|chd|mdf|mds|nrg|elf|irx|img|dat|pss|ipu|m2v|aifc|aiff|wav|adp|vag|vab|mid|seq|tm2|rgba16|rgba32|sym\.bak)$'
for f in "${files[@]}"; do
    [[ -z "$f" ]] && continue
    if [[ "$f" =~ $ext_block_re ]]; then
        note "blocked extension: $f"
    fi
done

# --- 2. PS2 boot-ELF naming patterns ---
ps2_boot_re='(^|/)(SLUS|SLES|SLPS|SLPM|SCUS|SCES|SCPS|PBPX)[-_][0-9A-Za-z._]+'
for f in "${files[@]}"; do
    [[ -z "$f" ]] && continue
    if [[ "$f" =~ $ps2_boot_re ]]; then
        note "PS2 boot-ELF naming: $f"
    fi
done
disc_file_re='(^|/)(MAIN\.MAP|SRCFILE\.TXT|TRFILE\.TXT|TRTABLE\.BIN|SYSTEM\.CNF)$'
for f in "${files[@]}"; do
    [[ -z "$f" ]] && continue
    if [[ "${f^^}" =~ $disc_file_re ]]; then
        note "disc reference file: $f"
    fi
done

# --- 3. size cap ---
# Non-source files keep the tight 256 KiB cap (catches stray binary/asset
# dumps). Tracked C source (ico2/ and sce/ *.c/*.h/*.c.inc) gets a
# higher 8 MiB ceiling: some TUs hold their data as typed C (word arrays,
# strings, structs), which pushes them past 256 KiB. Their CONTENT is still
# gated by rule #5 below (raw byte-array dumps banned), so a large .c is
# typed source, not laundered ROM. The 8 MiB ceiling stays as a backstop.
src_large_re='^(ico2|sce)/.*\.(c|h|c\.inc|inc)$'
for f in "${files[@]}"; do
    [[ -z "$f" ]] && continue
    [[ ! -f "$f" ]] && continue
    # Symbol-address files (config/symbol_addrs.<ver>.txt: pal, us, aug6) are
    # `Name = 0xADDR; // type:func` declarations, no byte data. The aug6 and
    # PAL ones are large (>256 KiB) only because those builds are fully named
    # from a disc-shipped MAIN.MAP (5k+ functions), unlike the USA retail
    # target's sparse file. Format-verified text, not laundered ROM; exempt the
    # size cap (content is still subject to the byte-array rules below).
    case "$f" in config/symbol_addrs.*.txt) continue ;; esac
    # config/ico.<ver>.yaml is a posterity record read by nothing: address
    # rows and their per-row reasoning comments, no byte data (the comments
    # pushed it past 256 KiB).
    case "$f" in config/ico.*.yaml) continue ;; esac
    size=$(wc -c < "$f")
    if [[ "$f" =~ $src_large_re ]]; then
        if (( size > 8388608 )); then
            note "tracked source >8MiB (suspicious): $f ($size bytes)"
        fi
    elif (( size > 262144 )); then
        note "tracked file >256KiB (suspicious): $f ($size bytes)"
    fi
done

# --- 4. ELF magic sniff ---
for f in "${files[@]}"; do
    [[ -z "$f" ]] && continue
    [[ ! -f "$f" ]] && continue
    head4=$(head -c 4 "$f" 2>/dev/null | od -An -tx1 -N4 | tr -d ' \n' || true)
    if [[ "$head4" == "7f454c46" ]]; then
        note "file starts with ELF magic: $f"
    fi
done

# --- 5. raw byte-array initializers in tracked C ---
# An `unsigned char D_<VMA>[N] = { 0x..., ... }` initializer is raw bytes
# from the original ELF, not a developer reconstruction. Tracked C under
# ico2/ and sce/ uses TYPED forms (string literals, ints, floats, named
# pointer arrays, struct literals); a byte-array initializer fails the
# commit, so the bytes are never laundered into the tracked tree. (The
# data-only members are generated at build time instead, from the user's own
# disc: tools/gen_data_c.py.)
#
# The match is on the byte-array SHAPE itself, with an
# `__attribute__((section(...)))` prefix OPTIONAL: a plain
# `unsigned char D_X[N] = { 0xAB, 0xCD, ... }` is just as much a raw dump.
# The shape requires the first brace element to be a hex byte AND at least
# one comma (>=2 elements), so the legitimate typed forms all pass:
#   - all-zero `{ 0 };`        (no comma, never matches)
#   - strings `= "...";`       (no brace, never matches)
#   - word/short `unsigned int D_X[N] = { 0x.., .. }`  (non-byte type)
#   - struct/typedef arrays    (non-byte type)
byte_type_re='(unsigned[[:space:]]+char|signed[[:space:]]+char|char|uint8_t|int8_t|u8|s8)'
attr_opt_re='(__attribute__[[:space:]]*\(\([[:space:]]*section[[:space:]]*\([[:space:]]*"\.\w+\.0x[0-9A-Fa-f]+"[[:space:]]*\)[[:space:]]*\)\)[[:space:]]+)?'
raw_byte_re="^[[:space:]]*${attr_opt_re}(const[[:space:]]+)?${byte_type_re}[[:space:]]+D_[0-9A-Fa-f]{8}[[:space:]]*\[[0-9]+\][[:space:]]*=[[:space:]]*\{[[:space:]]*0x[0-9A-Fa-f]{1,2}[[:space:]]*,[^}]*\}"
for f in "${files[@]}"; do
    [[ -z "$f" ]] && continue
    [[ ! -f "$f" ]] && continue
    # Only tracked C under ico2/ and sce/.
    case "$f" in
        ico2/*.c|ico2/*.c.inc|sce/*.c) ;;
        *) continue ;;
    esac
    if grep -nE "${raw_byte_re}" "$f" >/dev/null 2>&1; then
        note "raw byte-array initializer in tracked source: $f"
        note "  ICO data sections must be typed (string literal / int / float /"
        note "  named pointer array / struct), not raw bytes. See docs/LEGAL.md."
        grep -nE "${raw_byte_re}" "$f" 2>&1 | head -3 >&2
    fi
done

# --- 5b. large integer-array initializers, any name ---
# The D_<VMA> rule above only sees byte arrays named after their address. A
# table of 64 or more integer literals, of any integer type and any name, is
# the same shape (bytes or words copied out of the binary); the C the
# decompilation writes uses typed forms. tools/check_int_arrays.py finds them
# and keeps the reviewed exemptions per (file, array), each with its reason
# (docs/LEGAL.md, "Exemptions from the IP-safety scan").
int_scan=()
for f in "${files[@]}"; do
    [[ -z "$f" ]] && continue
    [[ ! -f "$f" ]] && continue
    case "$f" in
        ico2/*.c|ico2/*.h|ico2/*.inc|sce/*.c|sce/*.h|sce/*.inc) int_scan+=("$f") ;;
    esac
done
if (( ${#int_scan[@]} )); then
    py="$(command -v python3 || true)"
    if [[ -z "$py" ]]; then
        note "rule 5b needs python3 (tools/check_int_arrays.py)"
    elif ! hits="$("$py" "$(dirname "${BASH_SOURCE[0]}")/check_int_arrays.py" "${int_scan[@]}")"; then
        note "large integer-array initializer in tracked source:"
        printf "%s\n" "$hits" | head -20 >&2
        note "  a table of integer literals is the shape of copied bytes; write it"
        note "  typed, or review it and add an exemption with its reason to"
        note "  tools/check_int_arrays.py. See docs/LEGAL.md."
    fi
fi

# --- 6. gitignore-tracked anomalies ---
# `git check-ignore` without --no-index answers "not ignored" for any path
# that is in the index, so it can never flag a tracked file. --no-index
# tests the path against the patterns alone. Batched through --stdin.
mapfile -t ignored < <(printf "%s\n" "${files[@]}" | sed '/^$/d' |
    git check-ignore --no-index --stdin 2>/dev/null || true)
for f in "${ignored[@]}"; do
    [[ -z "$f" ]] && continue
    note "tracked file matches .gitignore: $f"
done

if (( bad )); then
    echo "" >&2
    echo "IP-safety scan FAILED. See messages above." >&2
    echo "If a hit is a false positive, document why and consider tightening" >&2
    echo "allowlists in tools/check_no_rom.sh rather than removing the check." >&2
    exit 1
fi

echo "check_no_rom: OK (${#files[@]} files scanned)"
