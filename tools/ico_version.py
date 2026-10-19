"""Shared version-slug detection and per-version paths for ICO tooling.

The targets and where their files are:

    branch   slug   link order                    base files
    ------   ----   ---------------------------   -----------------------------
    main     pal    config/link_order.pal.txt     baserom/pal/baseelf.elf
    main     jpn    config/link_order.jpn.txt     baserom/jpn/baseelf.elf
    ntsc     us     (the ntsc branch's config)    baserom/baseelf.elf
    aug6     aug6   (the aug6 branch's config)    baserom/aug6/baseelf.elf

`us` keeps its base files at the top of `baserom/` (it was the first target);
every other slug gets its own `baserom/<slug>/` subdirectory. `baserom/` is
gitignored and can hold every target's files side by side.

An explicit `VERSION` environment variable wins; otherwise the slug is the
first one in VERSIONS order whose `config/link_order.<slug>.txt` exists
(configured_versions()); this tree carries pal's and jpn's (upstream's
records; tools/gen_sources.py reads pal's as the one source inventory), so it
is pal.

DISCS is upstream's table of what each target's disc image is called and how
tools name it in messages (the file under baserom/, the product code, the
region, the boot file when SYSTEM.CNF names another, the reference files
copied beside the ELF). Every target's recorded SHA-1s are
config/sha1sums.<slug>.txt (sha1sums_path()).
"""
from __future__ import annotations
import os
import sys
from pathlib import Path

# Detection order.
VERSIONS = ("pal", "jpn", "us", "aug6", "jun26")

# Per-target disc image.
#   region  : how messages describe the disc
#   product : the product code printed on it ("" when it carries none)
#   where   : the region in the name a dump of it usually goes by,
#             as in `Ico (Europe).iso`
#   image   : the file the user copies under baserom/
#   cue     : the raw image's cue sheet, or "" when `image` is already a
#             cooked ISO9660 image
#   boot    : the ELF's path on the disc, when it is not the file the disc's
#             SYSTEM.CNF BOOT2 line names (absent: SYSTEM.CNF's)
#   extras  : disc files copied beside the ELF as reference material (the
#             build reads none of them)
DISCS = {
    "pal": {
        "region": "PAL retail",
        "product": "SCES-50760",
        "where": "Europe",
        "image": "Ico_PAL.iso",
        "cue": "",
        # The 2002-01-16 EU master's linker map and source listings.
        "extras": ("MAIN.MAP", "SRCFILE.TXT", "TRFILE.TXT", "SYSTEM.CNF"),
    },
    "jpn": {
        "region": "Japanese retail",
        "product": "SCPS-11003",
        "where": "Japan",
        "image": "Ico_JPN.bin",
        "cue": "Ico_JPN.cue",
        # The JPN disc ships no map or listing.
        "extras": ("SYSTEM.CNF",),
    },
    "us": {
        "region": "USA retail",
        "product": "SLUS-20218",
        "where": "USA",
        "image": "Ico_USA.bin",
        "cue": "Ico_USA.cue",
        "extras": ("SYSTEM.CNF",),
    },
    "aug6": {
        "region": "Aug-6-2001 prototype",
        "product": "",
        "where": "prototype",
        "image": "Ico_AUG6.bin",
        "cue": "Ico_AUG6.cue",
        # A linker map and the trace listing; this disc has no SRCFILE.TXT.
        "extras": ("MAIN.MAP", "TRFILE.TXT", "SYSTEM.CNF"),
    },
    "jun26": {
        "region": "Jun-26-2001 demo",
        "product": "SCUS-97158",
        "where": "demo",
        "image": "Ico_JUN26.iso",
        "cue": "",
        # A demo among other games on the Official U.S. PlayStation
        # Magazine's disc (issue 49, a DVD); its SYSTEM.CNF boots the disc's
        # menu, SCUS_971.58, which starts the ICO demo from its own
        # directory. The build's stamp is "Jun 26 2001" "20:01:21". The
        # directory has no SYSTEM.CNF, map or listing.
        "boot": "CUS97113/SCPS_000.00",
        "extras": ("SYSTEM.CNF",),
    },
}


def configured_versions(repo_root: Path) -> list[str]:
    """The slugs this tree carries a link order for, in VERSIONS order."""
    return [v for v in VERSIONS
            if (repo_root / "config" / f"link_order.{v}.txt").exists()]


def detect_version(repo_root: Path) -> str:
    """The target slug: $VERSION, else the first configured one, else us."""
    v = os.environ.get("VERSION")
    if v:
        return v
    have = configured_versions(repo_root)
    return have[0] if have else "us"


def baserom_dir(repo_root: Path, version: str) -> Path:
    """Directory holding this target's extracted base files."""
    if version == "us":
        return repo_root / "baserom"
    return repo_root / "baserom" / version


def baseelf_path(repo_root: Path, version: str) -> Path:
    """The extracted boot ELF of this target."""
    return baserom_dir(repo_root, version) / "baseelf.elf"


def sha1sums_path(repo_root: Path, version: str) -> Path:
    """The recorded SHA-1s of this target's base files,
    config/sha1sums.<version>.txt (keyed by basename)."""
    return repo_root / "config" / f"sha1sums.{version}.txt"


_KEYS = {
    "version": lambda root, v: v,
    "baserom_dir": lambda root, v: baserom_dir(root, v).relative_to(root),
    "baseelf": lambda root, v: baseelf_path(root, v).relative_to(root),
}


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    root = Path(__file__).resolve().parent.parent
    ver = detect_version(root)
    if not argv:
        for k, fn in _KEYS.items():
            print(f"ICO_{k.upper()}={fn(root, ver)}")
        return 0
    for key in argv:
        if key not in _KEYS:
            print(f"ico_version: unknown key '{key}' (have: {', '.join(_KEYS)})",
                  file=sys.stderr)
            return 2
        print(_KEYS[key](root, ver))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
