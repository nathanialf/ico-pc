#!/usr/bin/env python3
"""
tm2_sheets.py: decode the game's text sheets from the user's disc image to
PNGs, for transcribing their words (docs/port/UI.md, "Menu text" and
"Subtitles").

The PAL disc keeps every word the game shows as a pre-rendered texture:

  - the subtitles: DFDATAS/DATA.DF's ten `data_<LL><SS>.jim` members
    (jimaku.c's `jimakuFileName[]`: LL = EG FR GR IT SP, SS = 01 for the
    first run, 02 after the game is cleared), each a run of 0x8800-byte
    blocks holding one TIM2 (jimaku.c reads 0x8440 bytes of each and makes
    one texture of it);
  - the menu sheets: `text/menu_PAL_<LL>/*.tm2`, `text/title.tm2`,
    `text/exp.tm2` and `text/buttons.tm2`, members of the packs
    COMMON.DF, STGTTL.DF and STGLOG.DF (raw deflate streams; a 16-byte
    header whose first word is the member count, 0x224-byte entries
    {id, kind, word08, size, name[532]}, then the members back to back:
    fumi/ios/cdvd.c iosCdvdMgrPackLoad).

DATA.DF's directory is walked as fumi/ios/cdvd.c `unifile_read_func` walks
it: a member count, then 40 bytes a member (32 of name, the byte offset in
DATA.DF, the size).

TIM2 decoding covers PSMCT32, PSMCT24, PSMCT16 and the indexed PSMT8 and
PSMT4 with a 16-, 24- or 32-bit CLUT, CSM1 (the 8-bit CLUT's 32-entry
blocks stored with entries 8..15 and 16..23 swapped) or CSM2 (linear).
GS alpha (0x80 = opaque) becomes 0..255. Only the top mip level is written.

Usage:
    .venv/bin/python tools/tm2_sheets.py [--iso baserom/Ico_PAL.iso]
        [--out build/sheets] [--only jim|menu] [--dark]

Writes build/sheets/jim/data_<LL><SS>/<block>.png (three digits) and
build/sheets/<member path>.png. --dark also writes each picture flattened
over black as <name>_dark.png (the light lettering is easier to read so).
build/ is gitignored: nothing written here may be committed (docs/LEGAL.md).

Needs python3 and pycdlib (tools/requirements.txt); the PNGs are written
with zlib alone.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

import pycdlib

REPO = Path(__file__).resolve().parent.parent
SECTOR = 2048
JIM_BLOCK = 0x8800
JIM_READ = 0x8440
LANGS = ("EG", "FR", "GR", "IT", "SP")
PACKS = ("COMMON.DF", "STGTTL.DF", "STGLOG.DF")


# --------------------------------------------------------------- the disc


def datadf_dir(iso_path: Path):
    """(file object, DATA.DF's byte offset, {name: (offset, size)})"""
    iso = pycdlib.PyCdlib()
    iso.open(str(iso_path))
    rec = iso.get_record(iso_path="/DFDATAS/DATA.DF;1")
    base = rec.extent_location() * SECTOR
    iso.close()
    f = open(iso_path, "rb")
    f.seek(base)
    (count,) = struct.unpack("<I", f.read(4))
    raw = f.read(count * 40)
    members = {}
    for i in range(count):
        e = raw[i * 40:(i + 1) * 40]
        name = e[:32].split(b"\0")[0].decode("ascii")
        off, size = struct.unpack("<II", e[32:40])
        members[name] = (off, size)
    return f, base, members


def read_member(f, base, members, name):
    off, size = members[name]
    f.seek(base + off)
    return f.read(size)


def pack_members(blob: bytes):
    """[(name, bytes)] of a .DF pack (a raw deflate stream)"""
    d = zlib.decompressobj(-15).decompress(blob)
    (count,) = struct.unpack("<I", d[:4])
    p = 16 + count * 0x224
    out = []
    for i in range(count):
        e = d[16 + i * 0x224:16 + (i + 1) * 0x224]
        size = struct.unpack("<i", e[12:16])[0]
        name = e[16:].split(b"\0")[0].decode("ascii", "replace")
        out.append((name, d[p:p + size]))
        p += size
    return out


# --------------------------------------------------------------- TIM2


def _clut_colour(raw: bytes, i: int, bits: int):
    if bits == 32:
        r, g, b, a = raw[i * 4:i * 4 + 4]
    elif bits == 24:
        r, g, b = raw[i * 3:i * 3 + 3]
        a = 0x80
    else:
        (v,) = struct.unpack("<H", raw[i * 2:i * 2 + 2])
        r = (v & 31) << 3
        g = ((v >> 5) & 31) << 3
        b = ((v >> 10) & 31) << 3
        a = 0x80 if v & 0x8000 else 0
    return (r, g, b, min(255, a * 255 // 0x80))


def tim2_decode(data: bytes):
    """[(width, height, RGBA bytes)] of every picture of a TIM2 file"""
    if data[:4] != b"TIM2":
        raise ValueError("not a TIM2 file")
    align, count = data[5], struct.unpack("<H", data[6:8])[0]
    p = 0x80 if align == 1 else 0x10
    pics = []
    for _ in range(count):
        (total, clut_size, image_size, header_size, clut_colors, _fmt, _mips,
         clut_type, image_type, w, h) = struct.unpack("<IIIHHBBBBHH", data[p:p + 24])
        img = data[p + header_size:p + header_size + image_size]
        clut = data[p + header_size + image_size:p + header_size + image_size + clut_size]
        out = bytearray(w * h * 4)
        if image_type in (4, 5):
            cbits = {1: 16, 2: 24, 3: 32}[clut_type & 0x3F]
            n = clut_colors
            pal = [_clut_colour(clut, i, cbits) for i in range(n)]
            if image_type == 5 and not (clut_type & 0x80) and n >= 32:
                # CSM1: each 32-entry block keeps 8..15 and 16..23 swapped
                sw = list(pal)
                for i in range(n):
                    j = i & 31
                    if 8 <= j < 16:
                        sw[i] = pal[i + 8]
                    elif 16 <= j < 24:
                        sw[i] = pal[i - 8]
                pal = sw
            for y in range(h):
                for x in range(w):
                    k = y * w + x
                    if image_type == 5:
                        ix = img[k]
                    else:
                        byte = img[k >> 1]
                        ix = (byte >> 4) if (k & 1) else (byte & 15)
                    out[k * 4:k * 4 + 4] = bytes(pal[ix] if ix < len(pal) else (0, 0, 0, 0))
        elif image_type in (1, 2, 3):
            bits = {1: 16, 2: 24, 3: 32}[image_type]
            for k in range(w * h):
                out[k * 4:k * 4 + 4] = bytes(_clut_colour(img, k, bits))
        else:
            raise ValueError(f"image type {image_type} not handled")
        pics.append((w, h, bytes(out)))
        p += total
    return pics


# --------------------------------------------------------------- PNG


def png_write(path: Path, w: int, h: int, rgba: bytes, dark: bool = False):
    if dark:
        flat = bytearray(w * h * 4)
        for k in range(w * h):
            r, g, b, a = rgba[k * 4:k * 4 + 4]
            flat[k * 4:k * 4 + 4] = bytes((r * a // 255, g * a // 255, b * a // 255, 255))
        rgba = bytes(flat)
    rows = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def write_pics(stem: Path, data: bytes, dark: bool) -> int:
    pics = tim2_decode(data)
    for i, (w, h, rgba) in enumerate(pics):
        name = stem.name if len(pics) == 1 else f"{stem.name}_{i}"
        png_write(stem.with_name(name + ".png"), w, h, rgba)
        if dark:
            png_write(stem.with_name(name + "_dark.png"), w, h, rgba, dark=True)
    return len(pics)


# --------------------------------------------------------------- main


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--iso", type=Path, default=REPO / "baserom" / "Ico_PAL.iso")
    ap.add_argument("--out", type=Path, default=REPO / "build" / "sheets")
    ap.add_argument("--only", choices=("jim", "menu"))
    ap.add_argument("--dark", action="store_true")
    a = ap.parse_args()
    if not a.iso.is_file():
        print(f"tm2_sheets: no disc image at {a.iso}", file=sys.stderr)
        return 1
    f, base, members = datadf_dir(a.iso)
    written = 0
    if a.only in (None, "jim"):
        for lang in LANGS:
            for st in ("01", "02"):
                name = f"data_{lang}{st}.jim"
                blob = read_member(f, base, members, name)
                n = len(blob) // JIM_BLOCK
                for b in range(n):
                    blk = blob[b * JIM_BLOCK:b * JIM_BLOCK + JIM_READ]
                    if blk[:4] != b"TIM2":
                        continue
                    written += write_pics(a.out / "jim" / name[:-4] / f"{b:03d}", blk, a.dark)
                print(f"{name}: {n} blocks")
    if a.only in (None, "menu"):
        seen = set()
        for pack in PACKS:
            for name, data in pack_members(read_member(f, base, members, pack)):
                if not name.startswith("text/") or not name.endswith(".tm2") or name in seen:
                    continue
                seen.add(name)
                written += write_pics(a.out / name[:-4], data, a.dark)
                print(f"{pack}: {name}")
    f.close()
    print(f"tm2_sheets: {written} pictures under {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
