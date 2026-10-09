"""Shared version-slug detection and per-version paths for ICO tooling.

One branch per target:

    branch   slug   link order                    base files
    ------   ----   ---------------------------   -----------------------------
    main     pal    config/link_order.pal.txt     baserom/pal/baseelf.{elf,rom}
    ntsc     us     (the ntsc branch's config)    baserom/baseelf.{elf,rom}
    aug6     aug6   (the aug6 branch's config)    baserom/aug6/baseelf.{elf,rom}

`us` keeps its base files at the top of `baserom/` (it was the first target);
every other slug gets its own `baserom/<slug>/` subdirectory. `baserom/` is
gitignored and can hold all three targets' files side by side.

An explicit `VERSION` environment variable wins; otherwise the slug is the
first one whose `config/link_order.<slug>.txt` exists. A working tree only
carries its own target's link order.
"""
from __future__ import annotations
import os
from pathlib import Path

# Detection order.
VERSIONS = ("pal", "us", "aug6")


def detect_version(repo_root: Path) -> str:
    v = os.environ.get("VERSION")
    if v:
        return v
    for ver in VERSIONS:
        if (repo_root / "config" / f"link_order.{ver}.txt").exists():
            return ver
    return "us"


def baserom_dir(repo_root: Path, version: str) -> Path:
    """Directory holding this target's extracted base files."""
    if version == "us":
        return repo_root / "baserom"
    return repo_root / "baserom" / version


def baseelf_path(repo_root: Path, version: str) -> Path:
    return baserom_dir(repo_root, version) / "baseelf.elf"


def rom_path(repo_root: Path, version: str) -> Path:
    """The `objcopy -O binary` view of the base ELF, the ROM SHA-1's subject."""
    return baserom_dir(repo_root, version) / "baseelf.rom"


_KEYS = {
    "version": lambda root, v: v,
    "baserom_dir": lambda root, v: baserom_dir(root, v).relative_to(root),
    "baseelf": lambda root, v: baseelf_path(root, v).relative_to(root),
    "rom": lambda root, v: rom_path(root, v).relative_to(root),
}


def main(argv: list[str] | None = None) -> int:
    import sys
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
