#!/usr/bin/env python3
"""
extract_elf.py: pull the EE-side boot ELF out of the user's ICO disc image,
record its SHA-1, and dump a hint about the compiler fingerprint.

Version-aware (tools/ico_version.py picks the target, VERSION or the first
configured one, and its DISCS table names each target's disc):

    slug  disc image the user supplies   outputs under baserom/<slug>/
    ----  ----------------------------   -----------------------------------
    pal   baserom/Ico_PAL.iso            baseelf.elf
          (plain 2048-byte-sector ISO)   + MAIN.MAP SRCFILE.TXT TRFILE.TXT
                                           SYSTEM.CNF (disc reference files)
    jpn   baserom/Ico_JPN.bin + .cue     baseelf.elf + SYSTEM.CNF
    kor   baserom/Ico_KOR.iso            baseelf.elf + SYSTEM.CNF
          (plain 2048-byte-sector ISO)
    cn    baserom/Ico_CN.iso             baseelf.elf + SYSTEM.CNF
          (plain 2048-byte-sector ISO)
    ntsc  baserom/Ico_USA.bin + .cue     baseelf.elf + SYSTEM.CNF
    aug6  baserom/Ico_AUG6.bin + .cue    baseelf.elf
                                           + MAIN.MAP TRFILE.TXT SYSTEM.CNF
    jun26 baserom/Ico_JUN26.iso          baseelf.elf + SYSTEM.CNF
          (plain 2048-byte-sector ISO)

A raw .bin (MODE1/2352 or MODE2/2352, as its .cue says) is cooked once into
baserom/Ico_<REGION>.iso beside it, which is then read like the PAL image.

The boot ELF's name is never hard-coded: it comes from the disc's SYSTEM.CNF
`BOOT2` line (SCES_507.60 on PAL, SCPS_110.03 on the Japanese disc,
SCPS_560.01 on the Korean disc, SCPS_550.01 on the Asian disc, SCUS_971.13 on the USA disc
and the prototype). A demo on a disc shared with other games is started by the
disc's menu, which SYSTEM.CNF boots instead, so its DISCS row names the
ELF's path on the disc as `boot` (jun26: CUS97113/SCPS_000.00).

Raw .bin sectors are cooked by extracting the 2048-byte user data of each
sector and feeding the resulting ISO9660 image to pycdlib; an image that is
already 2048-byte-sector ISO9660 is read directly.

Nothing copyrighted enters the repository: everything written lands under
baserom/, which is gitignored wholesale.

Idempotent: re-runs are cheap once baseelf.elf exists.
"""

from __future__ import annotations

import hashlib
import io
import re
import sys
from pathlib import Path

import pycdlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ico_version import (  # noqa: E402
    DISCS, detect_version, baserom_dir, baseelf_path,
    disc_name, disc_image, disc_cue, sha1sums_path,
)

REPO_ROOT = Path(__file__).resolve().parent.parent
VERSION = detect_version(REPO_ROOT)

# Where THIS target's extracted files land: baserom/<ver>/ (see
# tools/ico_version.py).
OUT_DIR = baserom_dir(REPO_ROOT, VERSION)
ELF = baseelf_path(REPO_ROOT, VERSION)
SHA1SUMS = sha1sums_path(REPO_ROOT, VERSION)


def disc_recipe(version: str) -> dict | None:
    """This target's disc recipe, from tools/ico_version.py's DISCS table.

    image   : the ISO9660 image pycdlib reads. For a raw .bin/.cue pair this
              is the cooked cache this script writes beside it (same name,
              .iso); for an already-cooked disc image it IS the user's file.
    raw_bin/raw_cue : the raw pair to cook from, or None when `image` is
              supplied directly by the user.
    extras  : disc files copied into OUT_DIR alongside the ELF as untracked
              reference material (linker map, source listings, SYSTEM.CNF).
    name, where : how messages name the disc, and the region in the name a
              dump of it usually goes by.
    boot    : the ELF's path on the disc when SYSTEM.CNF does not name it
              (a demo on a disc shared with other games), else None.
    """
    if version not in DISCS:
        return None
    user = disc_image(REPO_ROOT, version)
    cue = disc_cue(REPO_ROOT, version)
    return {
        "image": user if cue is None else user.with_suffix(".iso"),
        "raw_bin": None if cue is None else user,
        "raw_cue": cue,
        "extras": DISCS[version]["extras"],
        "name": disc_name(version),
        "where": DISCS[version]["where"],
        "boot": DISCS[version].get("boot"),
    }


SYSTEM_CNF = "SYSTEM.CNF"

LOGICAL_SECTOR_SIZE = 2048
RAW_SECTOR_SIZE = 2352


def parse_cue_mode(cue_path: Path) -> str:
    """Return e.g. 'MODE1/2352' or 'MODE2/2352' from the cue file."""
    text = cue_path.read_text(errors="replace")
    m = re.search(r"TRACK\s+\d+\s+(MODE\d/\d+)", text)
    if m is None:
        raise SystemExit(f"extract_elf: can't parse mode from {cue_path}")
    return m.group(1).upper()


def cook_iso(bin_path: Path, mode: str, out_path: Path) -> None:
    """Convert a raw .bin to a cooked .iso (one 2048-byte sector per logical sector)."""
    if mode == "MODE1/2352":
        data_offset = 16  # 12 sync + 4 header
    elif mode == "MODE2/2352":
        # Mode 2 Form 1: 12 sync + 4 header + 8 subheader = 24
        data_offset = 24
    elif mode == "MODE1/2048":
        # already cooked: just copy / symlink
        out_path.write_bytes(bin_path.read_bytes())
        return
    else:
        raise SystemExit(f"extract_elf: unsupported mode {mode}")

    size = bin_path.stat().st_size
    if size % RAW_SECTOR_SIZE != 0:
        print(f"extract_elf: warning: bin size {size} is not a multiple of "
              f"{RAW_SECTOR_SIZE}; truncating", file=sys.stderr)
    n_sectors = size // RAW_SECTOR_SIZE
    print(f"extract_elf: cooking {bin_path.name} "
          f"({mode}, {n_sectors} sectors → {n_sectors * LOGICAL_SECTOR_SIZE} bytes)")

    with bin_path.open("rb") as src, out_path.open("wb") as dst:
        # Iterate in chunks of 256 sectors for I/O efficiency.
        chunk_sectors = 256
        chunk_size = chunk_sectors * RAW_SECTOR_SIZE
        for _ in range(0, n_sectors, chunk_sectors):
            buf = src.read(chunk_size)
            if not buf:
                break
            for i in range(0, len(buf), RAW_SECTOR_SIZE):
                dst.write(buf[i + data_offset : i + data_offset + LOGICAL_SECTOR_SIZE])


def extract_iso_file(iso_path: Path, iso_member: str, out_path: Path) -> None:
    """Use pycdlib to extract a named file from an ISO9660 image."""
    iso = pycdlib.PyCdlib()
    iso.open(str(iso_path))
    try:
        # ISO9660 stores filenames with version suffix, e.g. ;1.
        candidate_paths = [f"/{iso_member};1", f"/{iso_member}"]
        last_err: Exception | None = None
        for p in candidate_paths:
            try:
                buf = io.BytesIO()
                iso.get_file_from_iso_fp(buf, iso_path=p)
                out_path.write_bytes(buf.getvalue())
                return
            except pycdlib.pycdlibexception.PyCdlibException as exc:
                last_err = exc
                continue
        raise SystemExit(
            f"extract_elf: '{iso_member}' not found at any of {candidate_paths} "
            f"({last_err})"
        )
    finally:
        iso.close()


def boot_path_from_system_cnf(iso_path: Path) -> str:
    """Read SYSTEM.CNF and return the boot ELF filename it points at.

    SYSTEM.CNF format:
        BOOT2 = cdrom0:\\SCES_507.60;1
        VER = 1.00
        VMODE = PAL
    """
    iso = pycdlib.PyCdlib()
    iso.open(str(iso_path))
    try:
        buf = io.BytesIO()
        iso.get_file_from_iso_fp(buf, iso_path=f"/{SYSTEM_CNF};1")
        text = buf.getvalue().decode("ascii", errors="replace")
    finally:
        iso.close()

    for line in text.splitlines():
        line = line.strip()
        if not line.upper().startswith("BOOT2"):
            continue
        # everything after '=', strip 'cdrom0:\' and ';1'
        _, _, rhs = line.partition("=")
        rhs = rhs.strip()
        rhs = re.sub(r"^cdrom0:\\?", "", rhs, flags=re.IGNORECASE)
        rhs = rhs.lstrip("\\/")
        rhs = rhs.split(";")[0]
        return rhs
    raise SystemExit(f"extract_elf: no BOOT2 line in {SYSTEM_CNF}")


def sha1_of(path: Path) -> str:
    h = hashlib.sha1()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha1sums_header() -> list[str]:
    """The four comment lines a new config/sha1sums.<ver>.txt starts with."""
    d = DISCS.get(VERSION, {})
    return [
        "# Format: <SHA-1>  <filename>",
        f"# Target {VERSION}. ICO {disc_name(VERSION) or VERSION}.",
        f"# Files live under baserom/{VERSION}/ (gitignored); the names below are basenames.",
        f"# baseelf.elf: EE boot ELF extracted from the {d.get('region', VERSION)} disc.",
    ]


def update_sha1sums(name: str, sha1: str) -> str | None:
    """Record (or verify) sha1 for `name` in config/sha1sums.<ver>.txt,
    creating the file with its header when the target has none.
    Returns the existing hash if there was a mismatch, else None.
    """
    lines: list[str] = []
    if not SHA1SUMS.exists():
        lines = sha1sums_header()
    else:
        for line in SHA1SUMS.read_text().splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                lines.append(line)
                continue
            parts = stripped.split()
            if len(parts) >= 2 and parts[1] == name:
                if parts[0] != sha1:
                    return parts[0]
                # already recorded and matching
                return None
            lines.append(line)
    lines.append(f"{sha1}  {name}")
    SHA1SUMS.write_text("\n".join(lines) + "\n")
    return None


def dump_comment_section(elf_path: Path) -> None:
    """Best-effort .comment dump for compiler fingerprint hint."""
    try:
        from elftools.elf.elffile import ELFFile
    except ImportError:
        return
    try:
        with elf_path.open("rb") as f:
            elf = ELFFile(f)
            sec = elf.get_section_by_name(".comment")
            if sec is None:
                return
            data = sec.data()
            # .comment is a series of NUL-terminated strings.
            strings = [s.decode("utf-8", errors="replace") for s in data.split(b"\x00") if s]
            print()
            print("==> .comment section (compiler fingerprint hint):")
            for s in strings:
                print(f"     {s!r}")
    except Exception as exc:
        print(f"extract_elf: .comment dump failed ({exc})", file=sys.stderr)


def copy_reference_files(iso_path: Path, names: tuple[str, ...]) -> None:
    """Pull the disc's reference files (linker map, listings, SYSTEM.CNF).

    Best-effort per file: a disc that doesn't carry one is not an error (the
    build never reads these: they're provenance/reference material for
    symbol recovery, and live under the gitignored baserom/).
    """
    for name in names:
        dst = OUT_DIR / name
        if dst.exists() and dst.stat().st_size > 0:
            continue
        try:
            extract_iso_file(iso_path, name, dst)
        except SystemExit as exc:
            print(f"extract_elf: note: no {name} on this disc ({exc})",
                  file=sys.stderr)
            continue
        print(f"==> {name} -> {dst.relative_to(REPO_ROOT)} "
              f"({dst.stat().st_size} bytes)")


def _rel(p: Path) -> str:
    return str(p.relative_to(REPO_ROOT))


def resolve_iso(disc: dict) -> Path:
    """Return a 2048-byte-sector ISO9660 image, cooking the raw .bin if needed."""
    image: Path = disc["image"]
    raw_bin: Path | None = disc["raw_bin"]

    if raw_bin is None:
        # The user's image is already cooked (one 2048-byte sector per logical
        # sector): read it in place, never rewrite it.
        if not image.exists():
            raise SystemExit(
                f"extract_elf: missing {_rel(image)}\n"
                f"  Copy your own image of the {disc['name']}:\n"
                f"    cp '/path/to/Ico ({disc['where']}).iso'  {_rel(image)}"
            )
        size = image.stat().st_size
        if size % LOGICAL_SECTOR_SIZE != 0:
            print(f"extract_elf: warning: {image.name} size {size} is not a "
                  f"multiple of {LOGICAL_SECTOR_SIZE}; is it a raw .bin?",
                  file=sys.stderr)
        return image

    cue: Path = disc["raw_cue"]
    missing = [p for p in (raw_bin, cue) if not p.exists()]
    if missing:
        raise SystemExit(
            f"extract_elf: missing {' and '.join(_rel(p) for p in missing)}\n"
            f"  Copy your own image of the {disc['name']}, the .bin and its .cue:\n"
            f"    cp '/path/to/Ico ({disc['where']}).bin'  {_rel(raw_bin)}\n"
            f"    cp '/path/to/Ico ({disc['where']}).cue'  {_rel(cue)}"
        )
    if not image.exists():
        cook_iso(raw_bin, parse_cue_mode(cue), image)
        print(f"==> wrote {image}")
    return image


def main() -> int:
    disc = disc_recipe(VERSION)
    if disc is None:
        print(f"extract_elf: no disc recipe for target '{VERSION}' "
              f"(have: {', '.join(DISCS)}).", file=sys.stderr)
        print(f"  Place the extracted files under "
              f"{OUT_DIR.relative_to(REPO_ROOT)}/ by hand, or add a DISCS "
              f"row for this target to tools/ico_version.py (and its shell "
              f"twin).", file=sys.stderr)
        return 1

    print(f"==> target: {VERSION} "
          f"(-> {OUT_DIR.relative_to(REPO_ROOT)}/baseelf.elf)")

    # 1. resolve a cooked ISO9660 image (cooking a raw .bin/.cue if that's
    #    what the user supplied; idempotent either way)
    try:
        iso = resolve_iso(disc)
    except SystemExit as exc:
        print(str(exc), file=sys.stderr)
        return 1
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # 2. pull the boot ELF (its name comes from SYSTEM.CNF and varies by
    #    region; a demo on a shared disc names its own in the DISCS row)
    if not ELF.exists():
        boot_name = disc["boot"]
        if boot_name is not None:
            print(f"==> boot file (DISCS row): {boot_name}")
        else:
            boot_name = boot_path_from_system_cnf(iso)
            print(f"==> SYSTEM.CNF BOOT2: {boot_name}")
        print(f"==> extracting {boot_name} -> {ELF}")
        extract_iso_file(iso, boot_name, ELF)
    else:
        print(f"==> {ELF} already present")

    if not ELF.exists() or ELF.stat().st_size == 0:
        print("extract_elf: extraction produced empty ELF", file=sys.stderr)
        return 2

    # 3. SHA-1 record / verify. config/sha1sums.<ver>.txt is keyed by
    #    BASENAME, one file per target.
    sha1 = sha1_of(ELF)
    print(f"==> {ELF.name} SHA-1: {sha1}")
    mismatch = update_sha1sums("baseelf.elf", sha1)
    if mismatch is not None:
        print(f"extract_elf: SHA-1 mismatch with recorded value!", file=sys.stderr)
        print(f"  recorded: {mismatch}", file=sys.stderr)
        print(f"  actual:   {sha1}", file=sys.stderr)
        print("Either your disc image is a different revision, or the recorded",
              file=sys.stderr)
        print(f"hash is wrong. Do not blindly update {SHA1SUMS}: consult the",
              file=sys.stderr)
        print("maintainer first.", file=sys.stderr)
        return 4
    print(f"==> SHA-1 recorded/verified in {SHA1SUMS.relative_to(REPO_ROOT)}")

    # 4. disc reference files (linker map / source listings / SYSTEM.CNF)
    if disc["extras"]:
        copy_reference_files(iso, disc["extras"])

    # 5. compiler fingerprint hint
    dump_comment_section(ELF)

    print()
    print(f"extract_elf: done. ELF at {ELF}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
