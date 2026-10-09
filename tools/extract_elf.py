#!/usr/bin/env python3
"""
extract_elf.py: pull the EE-side boot ELF out of the user's ICO disc image,
record its SHA-1, and dump a hint about the compiler fingerprint.

Version-aware (tools/ico_version.py picks the branch's target):

    slug  disc image the user supplies   outputs
    ----  ----------------------------   -----------------------------------
    pal   baserom/Ico_PAL.iso            baserom/pal/baseelf.elf
          (plain 2048-byte-sector ISO)   + MAIN.MAP SRCFILE.TXT TRFILE.TXT
                                           SYSTEM.CNF (disc reference files)
    us    baserom/Ico_USA.bin + .cue     baserom/baseelf.elf
          (raw MODE1/2352 or MODE2/2352)

The boot ELF's name is never hard-coded: it comes from the disc's SYSTEM.CNF
`BOOT2` line (SCES_507.60 on PAL, SCUS_971.13 on the USA disc).

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
    detect_version, baserom_dir, baseelf_path,
)

REPO_ROOT = Path(__file__).resolve().parent.parent
VERSION = detect_version(REPO_ROOT)

# Where the user drops their own disc images (all gitignored).
DISC_DIR = REPO_ROOT / "baserom"
# Where THIS target's extracted files land: baserom/ for us, baserom/<ver>/
# otherwise (see tools/ico_version.py).
OUT_DIR = baserom_dir(REPO_ROOT, VERSION)
ELF = baseelf_path(REPO_ROOT, VERSION)
SHA1SUMS = REPO_ROOT / "config" / "sha1sums.txt"

# Per-target disc recipe.
#   image  : the ISO9660 image pycdlib reads. For a raw .bin/.cue pair this is
#            the cooked cache this script writes; for an already-cooked disc
#            image it IS the user's file.
#   raw_bin/raw_cue : the raw pair to cook from, or None when `image` is
#            supplied directly by the user.
#   extras : disc files copied into OUT_DIR alongside the ELF as untracked
#            reference material (linker map, source listings, SYSTEM.CNF).
DISCS = {
    "pal": {
        "image": DISC_DIR / "Ico_PAL.iso",
        "raw_bin": None,
        "raw_cue": None,
        # The PAL disc ships the 2002-01-16 EU master's linker map and source
        # listings. Reference only, and gitignored with the rest of baserom/.
        "extras": ("MAIN.MAP", "SRCFILE.TXT", "TRFILE.TXT", "SYSTEM.CNF"),
    },
    "us": {
        "image": DISC_DIR / "Ico_USA.iso",     # cooked cache (gitignored)
        "raw_bin": DISC_DIR / "Ico_USA.bin",
        "raw_cue": DISC_DIR / "Ico_USA.cue",
        "extras": (),
    },
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


def update_sha1sums(name: str, sha1: str) -> str | None:
    """Record (or verify) sha1 for `name` in config/sha1sums.txt.
    Returns the existing hash if there was a mismatch, else None.
    """
    lines: list[str] = []
    if SHA1SUMS.exists():
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


def resolve_iso(disc: dict) -> Path:
    """Return a 2048-byte-sector ISO9660 image, cooking the raw .bin if needed."""
    image: Path = disc["image"]
    raw_bin: Path | None = disc["raw_bin"]

    if raw_bin is None:
        # The user's image is already cooked (one 2048-byte sector per logical
        # sector): read it in place, never rewrite it.
        if not image.exists():
            raise SystemExit(
                f"extract_elf: missing {image}\n"
                f"  Copy your {VERSION.upper()} disc image:\n"
                f"    cp '/path/to/Ico ({VERSION.upper()}).iso'  {image}"
            )
        size = image.stat().st_size
        if size % LOGICAL_SECTOR_SIZE != 0:
            print(f"extract_elf: warning: {image.name} size {size} is not a "
                  f"multiple of {LOGICAL_SECTOR_SIZE}; is it a raw .bin?",
                  file=sys.stderr)
        return image

    cue: Path = disc["raw_cue"]
    if not raw_bin.exists():
        raise SystemExit(
            f"extract_elf: missing {raw_bin}\n"
            f"  Copy your disc image:\n"
            f"    cp '/path/to/Ico (USA).bin'  {raw_bin}\n"
            f"    cp '/path/to/Ico (USA).cue'  {cue}"
        )
    if not cue.exists():
        raise SystemExit(f"extract_elf: missing {cue}")
    if not image.exists():
        cook_iso(raw_bin, parse_cue_mode(cue), image)
        print(f"==> wrote {image}")
    return image


def main() -> int:
    disc = DISCS.get(VERSION)
    if disc is None:
        print(f"extract_elf: no disc recipe for target '{VERSION}' "
              f"(have: {', '.join(sorted(DISCS))}).", file=sys.stderr)
        print(f"  Place the extracted files under "
              f"{OUT_DIR.relative_to(REPO_ROOT)}/ by hand, or add a DISCS "
              f"entry for this target.", file=sys.stderr)
        return 1

    print(f"==> target: {VERSION} "
          f"(-> {OUT_DIR.relative_to(REPO_ROOT)}/baseelf.elf)")
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    # 1. resolve a cooked ISO9660 image (cooking a raw .bin/.cue if that's
    #    what the user supplied; idempotent either way)
    try:
        iso = resolve_iso(disc)
    except SystemExit as exc:
        print(str(exc), file=sys.stderr)
        return 1

    # 2. pull the boot ELF (its name comes from SYSTEM.CNF and varies by region)
    if not ELF.exists():
        boot_name = boot_path_from_system_cnf(iso)
        print(f"==> SYSTEM.CNF BOOT2: {boot_name}")
        print(f"==> extracting {boot_name} -> {ELF}")
        extract_iso_file(iso, boot_name, ELF)
    else:
        print(f"==> {ELF} already present")

    if not ELF.exists() or ELF.stat().st_size == 0:
        print("extract_elf: extraction produced empty ELF", file=sys.stderr)
        return 2

    # 3. SHA-1 record / verify. sha1sums.txt is keyed by BASENAME on every
    #    branch (each branch records only its own target's hashes).
    sha1 = sha1_of(ELF)
    print(f"==> {ELF.name} SHA-1: {sha1}")
    mismatch = update_sha1sums("baseelf.elf", sha1)
    if mismatch is not None:
        print("extract_elf: SHA-1 mismatch with recorded value!", file=sys.stderr)
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
