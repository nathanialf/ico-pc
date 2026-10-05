#!/usr/bin/env python3
"""tools/check_int_arrays.py FILE...

Rule 5b of tools/check_no_rom.sh: flags any large array initialiser of an
integer type in C source whose elements are all integer literals, whatever
the array is called. Such a table is the shape of bytes or words copied out
of the binary (code, data, a symbol table), as opposed to the typed forms
the decompiled source uses (strings, floats, struct literals, named
pointers, enum names, expressions).

An initialiser is flagged when
  - the declaration's type is an integer type (char, short, int, long,
    signed/unsigned, uint8_t..uint64_t, int8_t..int64_t, u8..u64, s8..s64,
    u_char, u_short, u_int, u_long, size_t, uintptr_t),
  - the declarator is an array (`name[...]`, any number of dimensions),
  - every element of the brace body (nested braces included) is an integer
    literal (decimal, hex or octal, optional sign and U/L suffixes), and
  - there are at least MIN_ELEMENTS of them.

Exemptions are per (path, array name), each with its reason, in EXEMPT
below: a new table in an exempted file, or an exempted table renamed, is
flagged again. Prints one line per hit (path:line: name[count]) and exits 1
if any. docs/LEGAL.md, "Exemptions from the IP-safety scan", explains the
list.
"""

import re
import sys

MIN_ELEMENTS = 64

_SCE = (
    "sce/ is the reconstruction of the period SDK and toolchain library "
    "members the retired PS2 build linked; the PC port compiles no sce/ "
    "source (CMakeLists.txt, cmake/IcoSources.cmake), so none of it is in "
    "the shipped program. "
)
_ICO2 = (
    "the .data/.rodata of a game CODE member, written as typed C in the "
    "decompiled source and compiled into the port (docs/LEGAL.md keeps the "
    "data-only members out; code members' own tables are part of the "
    "re-derived source). "
)

# (path, array name) -> reason. Every entry is reviewed; see
# tools/check_no_rom.sh rule 5 and docs/LEGAL.md.
EXEMPT = {
    ("sce/libkernl/intr.c", "alarm_handler"): _SCE
    + "libkernl intr.o's .data: the kernel-mode alarm handler (0x740 bytes of "
    "R5900 code and its tables) that InitAlarm copies to 0x80076000, plus the "
    "entry stub and syscall table. The EE kernel stub data of the library "
    "member, kept as the record of that member; it has no C spelling.",
    ("sce/libgcc/_divdi3.c", "__clz_tab"): _SCE
    + "GCC's published longlong.h count-leading-zeros table (GPL with the "
    "runtime library exception), computed values, not game data.",
    ("sce/libgcc/_moddi3.c", "__clz_tab"): _SCE + "as _divdi3.c.",
    ("sce/libgcc/_udivdi3.c", "__clz_tab"): _SCE + "as _divdi3.c.",
    ("sce/libgcc/_umoddi3.c", "__clz_tab"): _SCE + "as _divdi3.c.",
    ("sce/libm/math/ef_rem_pio2.c", "two_over_pi"): _SCE
    + "fdlibm's published table of the bits of 2/pi (Sun notice), a "
    "mathematical constant.",
    ("sce/libmpeg/var.c", "_defIQM"): _SCE
    + "the default intra quantiser matrix of ISO/IEC 13818-2 (MPEG-2), "
    "section 6.3.11; a published standard's table.",
    ("sce/libmpeg/var.c", "_defNIQM"): _SCE
    + "the default non-intra quantiser matrix of ISO/IEC 13818-2 (all 16).",
    ("ico2/fumi/src/commonact.c", "intrMotion"): _ICO2
    + "motion numbers per interrupt kind, indexed by the kind.",
    ("ico2/seki/src/EnemyInit.c", "enemymodel01Kind15"): _ICO2
    + "-1-terminated part index lists per shadow kind.",
    ("ico2/seki/src/EnemyInit.c", "enemymodel01Kind4"): _ICO2 + "as Kind15.",
    ("ico2/seki/src/EnemyInit.c", "enemymodel01Kind10"): _ICO2 + "as Kind15.",
    ("ico2/seki/src/DisplayFont.c", "fontKerning"): _ICO2
    + "per character, the first and last column of its cell in the font "
    "texture (metrics; the texture itself is loaded from the disc).",
    ("ico2/common/src/debug.c", "fontBitmap"): _ICO2
    + "the 8x8 1bpp debug-overlay font (256 glyphs). Glyph bitmaps are the "
    "closest thing to an asset on this list: flagged for review in F2; "
    "replacing it with a port-owned font is the fix if the review says so.",
    ("ico2/common/src/debug_exception.c", "dbgFont"): _ICO2
    + "the exception report's 8x8 font; as debug.c fontBitmap (flagged for "
    "review in F2).",
}

INT_TYPE = (
    r"(?:(?:unsigned|signed)\s+)?(?:long\s+long|long|short|char|int)(?:\s+int)?"
    r"|unsigned|signed"
    r"|u?int(?:8|16|32|64|ptr)_t|size_t"
    r"|[us](?:8|16|32|64)|u_(?:char|short|int|long)"
)
DECL_RE = re.compile(
    r"(?:^|[;{}])[ \t]*"  # start of a declaration (line start or after ; { })
    r"((?:(?:static|const|volatile|extern|register)\s+)*)"
    r"(?:" + INT_TYPE + r")"
    r"\s+(?:(?:const|volatile)\s+)*"
    r"(?P<name>[A-Za-z_]\w*)\s*(?P<dims>(?:\[[^\]]*\]\s*)+)"
    r"(?:/\*.*?\*/\s*)?"  # a trailing /* derived name */ before the =
    r"(?:__attribute__\s*\(\(.*?\)\)\s*)?"
    r"=\s*\{",
    re.S | re.M,
)
INT_LIT = re.compile(r"^[+-]?(?:0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]*$")


def strip_comments(src):
    """Blank out comments and string/char literals, keeping offsets."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + re.sub(r"[^\n]", " ", src[i + 1 : j - 1]) + c)
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def body_at(src, start):
    """The text between the brace at src[start] and its match."""
    depth = 0
    for j in range(start, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[start + 1 : j]
    return src[start + 1 :]


def scan(path):
    """[(line, name, count)] for every flagged initialiser in path."""
    hits = []
    try:
        with open(path, encoding="latin-1") as f:
            src = strip_comments(f.read())
    except OSError:
        return hits
    for m in DECL_RE.finditer(src):
        brace = m.end() - 1
        body = body_at(src, brace)
        elems = [e.strip() for e in re.split(r"[,{}]", body)]
        elems = [e for e in elems if e]
        if len(elems) < MIN_ELEMENTS:
            continue
        if all(INT_LIT.match(e) for e in elems):
            line = src.count("\n", 0, m.start("name")) + 1
            hits.append((line, m.group("name"), len(elems)))
    return hits


def main(argv):
    bad = False
    for path in argv[1:]:
        for line, name, count in scan(path):
            if (path, name) in EXEMPT:
                continue
            print(f"{path}:{line}: {name}[{count} integer literals]")
            bad = True
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
