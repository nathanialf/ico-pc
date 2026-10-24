#!/usr/bin/env python3
"""tools/texture_catalog.py NAMES --serial SERIAL --out DIR [--european PAL_NAMES]
                           [--cap BYTES] [--check]

Writes the texture catalogue of one disc: docs/textures/<serial>/README.md and
one markdown file per group of textures (split when a file would pass the cap).
NAMES is the texpack_names.txt the disc walker writes
(build-host/linux-x64/port/render/texpack_disc_test <image>); one line per
TIM2 texture:

    <member> <type> <clutType> <WxH> <levels> clut <colors>/<bytes> scroll <a> <b> : <name>@<level>[/chain] ...

The names after " : " are the ones PCSX2 gives the texture; a "/chain" name is
one PCSX2 gives with mipmapping on and is left out here (the game and the
export use the level-alone names).  "refused" stands for a 24-bit palette.

--european names the PAL disc's file: the catalogue of a Japanese or Korean
disc then gets a European column (same / partly / new).
--check regenerates in memory and exits 1 when a file on disk differs, is
missing or is left over.  Output is sorted and repeatable.
"""
import argparse
import os
import re
import sys

CAP_DEFAULT = 240 * 1024  # the repository caps every file at 256 KiB

FORMAT_NAMES = {
    "PSMT4": "16 colours",
    "PSMT8": "256 colours",
    "PSMCT32": "32-bit",
    "PSMCT24": "24-bit",
    "PSMCT16": "16-bit",
}
FULL_PALETTE = {"PSMT4": 16, "PSMT8": 256}


class Tex:
    __slots__ = ("path", "group", "fmt", "size", "levels", "names", "refused",
                 "scroll", "colors", "group_index", "order")


def group_of(member):
    """The group a member belongs to and the path shown for it."""
    shown = member.replace("/model/../", "/")
    if member.startswith("object/sdf/"):
        return "object/" + member.split("/")[2], shown
    if "@" in member:  # a record of a subtitle file
        return member.split("@")[0], shown
    parts = member.split("/")
    if len(parts) == 1:
        return "root", shown
    return "/".join(parts[:-1]), shown


def parse(path):
    out = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line:
                continue
            if " : " not in line:
                raise SystemExit("%s:%d: no ' : ' in the line" % (path, n))
            left, right = line.split(" : ", 1)
            w = left.split()
            if len(w) < 6:
                raise SystemExit("%s:%d: short line" % (path, n))
            t = Tex()
            t.group, t.path = group_of(w[0])
            t.fmt, t.size, t.levels = w[1], w[3], int(w[4])
            t.colors = 0
            m = re.search(r"clut (\d+)/", left)
            if m:
                t.colors = int(m.group(1))
            t.scroll = bool(re.search(r"scroll (\d+) (\d+)", left)) and \
                re.search(r"scroll (\d+) (\d+)", left).groups() != ("0", "0")
            t.refused = right.strip() == "refused"
            t.names = []
            if not t.refused:
                for tok in right.split():
                    if tok.endswith("/chain"):
                        continue
                    t.names.append(tok)
            out.append(t)
    return out


def european_names(path):
    s = set()
    for t in parse(path):
        for tok in t.names:
            s.add(tok.rsplit("@", 1)[0])
    return s


def slug(group):
    s = re.sub(r"[^A-Za-z0-9_-]", "-", group.replace("/", "-"))
    return s


def order(texs):
    keys = sorted(set(t.group for t in texs))
    seen = {}
    for k in keys:
        sl = slug(k)
        if sl.lower() in seen or sl.lower() == "readme":
            raise SystemExit("group file name collision: %s" % sl)
        seen[sl.lower()] = k
    # stable sort keeps the names file's order inside a group
    ordered = sorted(texs, key=lambda t: t.group)
    for i, t in enumerate(ordered):
        t.order = i
        t.group_index = keys.index(t.group)
    return keys, ordered


def note_of(t):
    notes = []
    if t.refused:
        short = t.fmt in FULL_PALETTE and 0 < t.colors < FULL_PALETTE[t.fmt]
        notes.append("no name (short palette)" if short else "no name (24-bit palette)")
    if t.scroll:
        notes.append("colours cycle")
    if t.levels > 1:
        notes.append("several sizes")
    return ", ".join(notes)


def esc(s):
    return s.replace("|", "\\|")


def build(texs, serial, euro, cap):
    keys, ordered = order(texs)
    # the first texture to carry each name
    first = {}
    for t in ordered:
        for tok in t.names:
            nm = tok.rsplit("@", 1)[0]
            first.setdefault(nm, t)

    # rows per group, as a function of the link target of each holder
    pages_of = {}  # holder order -> file name, filled after pagination

    def link(holder):
        base = holder.path.rsplit("/", 1)[-1]
        return "[%s](%s)" % (esc(base), pages_of.get(holder.order, slug(holder.group) + ".md"))

    def row(t):
        names = [tok.rsplit("@", 1)[0] for tok in t.names]
        cell_names = "<br>".join("`%s`" % tok for tok in t.names) if t.names else "none"
        if not names:
            same = "-"
        else:
            parts = []
            for nm in names:
                h = first[nm]
                parts.append("first" if h is t else link(h))
            same = parts[0] if all(p == parts[0] for p in parts) else "<br>".join(parts)
        cols = [esc(t.path), FORMAT_NAMES.get(t.fmt, t.fmt), t.size, str(t.levels),
                cell_names, same]
        if euro is not None:
            if not names:
                cols.append("-")
            else:
                n_in = sum(1 for nm in names if nm in euro)
                cols.append("same" if n_in == len(names) else ("new" if n_in == 0 else "partly"))
        cols.append(note_of(t))
        return "| " + " | ".join(cols) + " |\n"

    head_cols = ["Texture", "Format", "Size", "Levels", "Names", "Same as"]
    if euro is not None:
        head_cols.append("European")
    head_cols.append("Notes")
    table_head = "| " + " | ".join(head_cols) + " |\n| " + " | ".join(
        "---" for _ in head_cols) + " |\n"

    by_group = {}
    for t in ordered:
        by_group.setdefault(t.group, []).append(t)

    def page_head(group, part, parts):
        title = group if parts == 1 else "%s (part %d of %d)" % (group, part, parts)
        return ("# %s: %s\n\n[Back to the catalogue of %s](README.md)\n\n" % (serial, title, serial)) + table_head

    def paginate():
        result = {}
        for k in keys:
            rows = [row(t) for t in by_group[k]]
            # pass 1: sizes; the part count is needed in the head, so assume at most 99
            chunks, cur, size = [], [], 0
            base = len(page_head(k, 99, 99).encode())
            for t, r in zip(by_group[k], rows):
                rb = len(r.encode())
                if cur and base + size + rb > cap:
                    chunks.append(cur)
                    cur, size = [], 0
                cur.append((t, r))
                size += rb
            if cur:
                chunks.append(cur)
            result[k] = chunks
        return result

    pages = paginate()
    for k in keys:
        for i, chunk in enumerate(pages[k]):
            fn = slug(k) + (".md" if i == 0 else "-%d.md" % (i + 1))
            for t, _ in chunk:
                pages_of[t.order] = fn
    pages = paginate()  # rows now carry their real links
    files = {}
    for k in keys:
        n = len(pages[k])
        for i, chunk in enumerate(pages[k]):
            fn = slug(k) + (".md" if i == 0 else "-%d.md" % (i + 1))
            files[fn] = page_head(k, i + 1, n) + "".join(r for _, r in chunk)

    # counts
    all_names = [tok.rsplit("@", 1)[0] for t in ordered for tok in t.names]
    uniq = set(all_names)
    stat = []
    for gi, k in enumerate(keys):
        ts = by_group[k]
        un = set(tok.rsplit("@", 1)[0] for t in ts for tok in t.names)
        earlier = sum(1 for nm in un if first[nm].group_index < gi)
        new_euro = sum(1 for nm in un if nm not in euro) if euro is not None else None
        stat.append((k, len(ts), len(un), earlier, new_euro))

    files["README.md"] = readme(serial, euro, ordered, all_names, uniq, stat, keys, pages)
    return files


def readme(serial, euro, ordered, all_names, uniq, stat, keys, pages):
    s = []
    a = s.append
    a("# Texture catalogue: %s\n\n" % serial)
    a("[Back to the texture pack guide](../../TEXTURE_PACKS.md)\n\n")
    a("This is a list of every picture on the disc `%s`: the name the game and PCSX2 give it, "
      "and which pictures share one name. A picture that shares a name with an earlier one is "
      "covered by the same replacement, so you draw it once.\n\n" % serial)
    a("## How to use it\n\n")
    a("- A replacement is a picture file named after the texture's name (`<name>.png`, or `.dds`) "
      "in the folder `textures/%s/replacements`. One file with that name replaces every texture "
      "that carries the name, wherever it shows. "
      "[How to put a pack in](../../TEXTURE_PACKS.md#put-a-texture-pack-in).\n" % serial)
    a("- To get the pictures themselves, choose **Options > Extras > Export textures** on the "
      "title screen. The game saves them under these names in `textures/%s/dumps`, with a list "
      "(`manifest.txt`) of what it saved and left out.\n" % serial)
    a("- The group files below list one texture per row: its place on the disc, its format and "
      "size, the name of each of its sizes (`name@size number`, 0 being the full size), the "
      "first texture that already carries the same name (**Same as**; `first` if it is the first) "
      "and notes. A path on the disc is shown without its `model/..` step.\n")
    if euro is not None:
        a("- The **European** column compares with the European disc: `same` when every name of "
          "the row also exists there (a pack made for the European disc already covers it), "
          "`new` when none does, `partly` when some do.\n")
    a("\n## Counts\n\n")
    a("- Textures: %d\n" % len(ordered))
    a("- Names (every size of every texture): %d\n" % len(all_names))
    a("- Different names: %d\n" % len(uniq))
    a("- Names that an earlier texture already carries: %d\n" % (len(all_names) - len(uniq)))
    nref = sum(1 for t in ordered if t.refused)
    a("- Textures with no name: %d\n" % nref)
    if euro is not None:
        same = sum(1 for t in ordered if t.names and all(tok.rsplit("@", 1)[0] in euro for tok in t.names))
        newr = sum(1 for t in ordered if t.names and not any(tok.rsplit("@", 1)[0] in euro for tok in t.names))
        part = sum(1 for t in ordered if t.names) - same - newr
        a("- Textures whose names the European disc has (same): %d\n" % same)
        a("- Textures with some names the European disc has (partly): %d\n" % part)
        a("- Textures with no name the European disc has (new): %d\n" % newr)
        a("- Different names the European disc does not have: %d\n" % sum(1 for n in uniq if n not in euro))
    a("\n## Groups\n\n")
    heads = ["Group", "Textures", "Different names", "Names seen in an earlier group"]
    if euro is not None:
        heads.append("Names not on the European disc")
    a("| " + " | ".join(heads) + " |\n| " + " | ".join("---" for _ in heads) + " |\n")
    for k, n, un, earlier, ne in stat:
        links = ", ".join("[%s](%s)" % (k if i == 0 else "part %d" % (i + 1),
                                        slug(k) + (".md" if i == 0 else "-%d.md" % (i + 1)))
                          for i in range(len(pages[k])))
        cols = [links, str(n), str(un), str(earlier)]
        if euro is not None:
            cols.append(str(ne))
        a("| " + " | ".join(cols) + " |\n")
    a("\n## What is not on the disc as a picture\n\n")
    a("Some pictures cannot be listed or exported, because the disc does not hold them:\n\n")
    a("- the fog, glow, blur and depth-of-field pictures the game draws while it runs;\n")
    a("- the movies;\n")
    a("- the later colours of the textures whose colours cycle (marked `colours cycle`); only the "
      "first colours are on the disc;\n")
    a("- names that combine several sizes of one texture (PCSX2 gives them with mipmapping on).\n\n")
    a("To get these, turn on **Developer mode** and **Dump textures** and play to the place where "
      "they show; the game saves them under their names as you go.\n")
    return "".join(s)


def write_or_check(files, out, check):
    bad = 0
    if check:
        have = set(f for f in os.listdir(out) if f.endswith(".md")) if os.path.isdir(out) else set()
        for fn in sorted(have - set(files)):
            print("left over: %s" % os.path.join(out, fn))
            bad = 1
        for fn, text in sorted(files.items()):
            p = os.path.join(out, fn)
            try:
                with open(p, encoding="utf-8", newline="") as f:
                    cur = f.read()
            except OSError:
                cur = None
            if cur != text:
                print("differs: %s" % p)
                bad = 1
        return bad
    os.makedirs(out, exist_ok=True)
    for fn in os.listdir(out):
        if fn.endswith(".md") and fn not in files:
            os.remove(os.path.join(out, fn))
    for fn, text in files.items():
        with open(os.path.join(out, fn), "w", encoding="utf-8", newline="") as f:
            f.write(text)
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("names")
    ap.add_argument("--serial", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--european")
    ap.add_argument("--cap", type=int, default=CAP_DEFAULT)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    texs = parse(a.names)
    euro = european_names(a.european) if a.european else None
    files = build(texs, a.serial, euro, a.cap)
    for fn, text in files.items():
        if len(text.encode()) >= 262144:
            raise SystemExit("%s is over the 256 KiB cap" % fn)
    rc = write_or_check(files, a.out, a.check)
    if a.check and rc == 0:
        print("%s: up to date (%d files)" % (a.out, len(files)))
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
