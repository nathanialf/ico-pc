#!/usr/bin/env python3
"""tools/texture_catalog.py NAMES --serial SERIAL --out FILE.csv [--european PAL_NAMES]
                           [--check]

Writes the texture sheet of one disc: one CSV row per replacement file a
texture pack can hold, from the texpack_names.txt the disc walker writes
(build-host/linux-x64/port/render/texpack_disc_test <image>; one line per
TIM2 texture):

    <member> <type> <clutType> <WxH> <levels> clut <colors>/<bytes> scroll <a> <b> : <name>@<level>[/chain] ...

The tokens after " : " are the names PCSX2 gives the texture; a "/chain" name
is one PCSX2 gives with mipmapping on and is left out (the game and the export
use the level-alone names).  "refused" stands for a 24-bit palette, which has
no name.

A name is one file: `<name>.png` in textures/<serial>/replacements.  Several
textures on the disc carry the same name (the same picture in many stage
packs, or a smaller level of one texture that is another texture's full size),
so the sheet has one row per name with where it is first used and how many
other textures it covers.  The rows keep the disc's order.

--european names the PAL disc's file: a Japanese or Korean sheet then gets a
column saying whether a pack made for the European disc already has the file.
--check regenerates in memory and exits 1 when the file on disk differs.
Output is repeatable.
"""
import argparse
import csv
import io
import re
import sys

FORMAT_NAMES = {
    "PSMT4": "16 colours",
    "PSMT8": "256 colours",
    "PSMCT32": "32-bit",
    "PSMCT24": "24-bit",
    "PSMCT16": "16-bit",
}
FULL_PALETTE = {"PSMT4": 16, "PSMT8": 256}

COLUMNS = ["file", "size", "format", "first used by", "where", "also used by", "notes"]


class Tex:
    __slots__ = ("path", "place", "fmt", "size", "levels", "names", "refused", "scroll",
                 "colors")


def place_of(member):
    """What the texture belongs to (a stage, a character, the menu, a
    subtitle file) and the path shown for it."""
    shown = member.replace("/model/../", "/")
    if member.startswith("object/sdf/"):
        return member.split("/")[2], shown[len("object/sdf/"):]
    if "@" in member:  # a record of a subtitle file
        return member.split("@")[0], shown
    parts = member.split("/")
    if len(parts) == 1:
        return parts[0], shown
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
            if len(w) < 5:
                raise SystemExit("%s:%d: short line" % (path, n))
            t = Tex()
            t.place, t.path = place_of(w[0])
            t.fmt, t.size, t.levels = w[1], w[3], int(w[4])
            t.colors = 0
            m = re.search(r"clut (\d+)/", left)
            if m:
                t.colors = int(m.group(1))
            m = re.search(r"scroll (\d+) (\d+)", left)  # the scroll flag, then the unstable flag
            t.scroll = bool(m) and m.group(1) != "0"
            t.refused = right.strip() == "refused"
            t.names = []
            if not t.refused:
                for tok in right.split():
                    if tok.endswith("/chain"):
                        continue
                    name, level = tok.rsplit("@", 1)
                    t.names.append((name, int(level)))
            out.append(t)
    return out


def level_size(size, level):
    w, h = size.split("x")
    return "%dx%d" % (max(1, int(w) >> level), max(1, int(h) >> level))


def european_names(path):
    return set(name for t in parse(path) for name, _ in t.names)


def build(texs, euro):
    """The rows: one per name, in the order the names first appear."""
    rows = {}
    order = []
    for t in texs:
        if t.refused:
            short = t.fmt in FULL_PALETTE and 0 < t.colors < FULL_PALETTE[t.fmt]
            why = "short palette" if short else "24-bit palette"
            order.append({
                "file": "",
                "size": t.size,
                "format": FORMAT_NAMES.get(t.fmt, t.fmt),
                "first used by": t.path,
                "where": t.place,
                "also used by": "0",
                "notes": "no name (%s): cannot be replaced" % why,
            })
            continue
        for name, level in t.names:
            if name in rows:
                r = rows[name]
                r["_count"] += 1
                r["_places"].add(t.place)
                continue
            notes = []
            if level > 0:
                notes.append("size %d of a %s texture" % (level, t.size))
            elif t.levels > 1:
                notes.append("has %d smaller sizes (their own rows)" % (t.levels - 1))
            if t.scroll:
                notes.append("colours cycle: only the first colours are on the disc")
            r = {
                "file": name + ".png",
                "size": level_size(t.size, level),
                "format": FORMAT_NAMES.get(t.fmt, t.fmt),
                "first used by": t.path,
                "where": t.place,
                "also used by": "",
                "notes": "; ".join(notes),
                "_count": 0,
                "_places": set([t.place]),
            }
            rows[name] = r
            order.append(r)
    for r in order:
        if "_count" in r:
            r["also used by"] = str(r["_count"])
            places = sorted(r["_places"])
            r["where"] = " ".join(places)
            if euro is not None:
                r["in the European pack"] = "yes" if r["file"][:-4] in euro else "no"
            del r["_count"]
            del r["_places"]
        elif euro is not None:
            r["in the European pack"] = ""
    return order


def render(rows, euro):
    columns = list(COLUMNS)
    if euro is not None:
        columns.insert(6, "in the European pack")
    out = io.StringIO()
    w = csv.DictWriter(out, fieldnames=columns, lineterminator="\n")
    w.writeheader()
    for r in rows:
        w.writerow(r)
    return out.getvalue()


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("names")
    ap.add_argument("--serial", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--european")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    texs = parse(a.names)
    euro = european_names(a.european) if a.european else None
    rows = build(texs, euro)
    text = render(rows, euro)
    files = sum(1 for r in rows if r["file"])
    if a.check:
        try:
            with open(a.out, encoding="utf-8", newline="") as f:
                same = f.read() == text
        except OSError:
            same = False
        print("%s: %s" % (a.out, "up to date" if same else "DIFFERS"))
        return 0 if same else 1
    with open(a.out, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print("%s: %d textures on %s, %d files to draw, %d bytes" %
          (a.out, len(texs), a.serial, files, len(text.encode("utf-8"))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
