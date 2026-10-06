# tools/

Every file in this directory, with what it does. The port builds with CMake
(`../CMakeLists.txt`, `docs/BUILDING.md`); nothing here builds the game. The
PS2 ELF build's scripts were removed in package 5C (docs/BUILDING.md lists
them); none is described below. The tools work on the branch's target as
`ico_version.py` reports it (`main` is PAL retail, slug `pal`).
`tools/toolchain/` (the host build's compilers and libraries) is fetched, not
tracked.

### The host build's

| tool | what it does |
|---|---|
| `fetch_toolchain.sh` | fills `tools/toolchain/`: llvm-mingw, mingw-w64 gcc, CMake; calls `fetch_deps.sh` last. No root. Run once; CI caches the result |
| `fetch_deps.sh` | fills `tools/toolchain/deps/`: Vulkan-Headers, volk, SDL3 (Linux build and mingw release), DXC, libmpeg2, the validation layer (tests only) |
| `package_win.sh <label>` | the Windows test package for HEAD: `dist/ico-pc-<label>-win.zip`, with the D3D12 backend checks under `x64/tools/` (docs/port/TESTING.md) |
| `package_linux.sh <label>` | the Linux package for HEAD: `dist/ico-pc-<label>-linux.tar.gz` with `ico_pc` and `libSDL3.so.0` (docs/port/STEAMDECK.md) |
| `gen_sources.py` | writes `cmake/IcoSources.cmake` from `config/link_order.pal.txt`; `--check` for CI and the hook |
| `gen_layout_asserts.py` | writes `port/test/layout_asserts.c` from the headers' offset comments; `--check` |
| `gen_data_desc.py` | writes the data tables' field descriptors under `port/data/gen/` from the schema, headers and symbol lists; `--check` (never opens the ELF) and `--check-manifest --elf` (maintainers) |
| `check_no_rom.sh` | IP guard: refuses disc images, PS2 executables, the disc's reference files (`MAIN.MAP`, `SRCFILE.TXT`, ...), extracted assets and movie streams, large binaries, raw byte-array initializers in tracked C, any large integer-literal table in `ico2/`/`sce/` (`check_int_arrays.py`, with its reviewed per-table exemptions; docs/LEGAL.md "Exemptions") and tracked files that match `.gitignore`. CI runs it over the tree |
| `check_int_arrays.py FILE...` | rule 5b of `check_no_rom.sh`: flags array initializers of 64+ integer literals, whatever their name, except the listed exemptions |
| `gen_notices.py --platform {linux,windows} --out FILE` | writes a package's `NOTICES.txt` (every third-party component and its licence text) from `tools/notices/manifest.json`; both package scripts run it (docs/port/THIRD_PARTY.md) |
| `format.sh` / `format_layout.py` | clang-format with the tracked `.clang-format`, then the top-level blank-line layout; `--check` for CI and the hook |
| `strip_host_gates.py` | the record of how `ico2/`, `sce/` and `vusrc/` became host-only source: resolves every `ICO_HOST` conditional with `ICO_HOST` defined (keeps the host arm, drops the EE arm and the directives), byte-exact outside them. `--check` fails if one remains (CI and the hook); `--dry-run` counts the sites (docs/BUILDING.md, "The game code") |
| `install_hooks.sh` | writes the pre-commit hook: `check_no_rom.sh`, `format.sh --check` on the staged C, the three `--check`s above and `strip_host_gates.py --check` |
| `setup.sh` | idempotent: the venv from `requirements.txt`, a check for a MIPS objcopy (skip with `SKIP_TOOLCHAIN=1`), the git hooks |
| `requirements.txt` | the venv's packages: pyelftools, pycdlib, ninja, clang-format |
| `mc_import.c` | the `mc_import` program (a CMake target of the host build, built beside `ico_pc`; the Windows test package carries it under `x64/tools/`, the Linux package does not): `mc_import [--overwrite] --to SAVES FILE...` writes ICO's save (`BESCES-50760ico`) from a raw PS2 card image (`.ps2`/`.bin`, with or without ECC spares) or a `.psu` into the card folder SAVES, and lists the other entries it skipped; `.max` and `.cbs` are recognised and refused. The reader is `port/save/mc_import.c`; docs/port/SAVES.md, "Importing saves" |
| `worktree.sh add <id> \| drop <id>` | a git worktree `/primary/dev/ico-pc-wt/<id>` on a new branch `pkg/<id>` from main, with `baserom`, `tools/toolchain`, `.venv` and `build-host/tmp` symlinked to the main checkout; `drop` removes it and deletes the branch only if merged (docs/BUILDING.md, "Worktrees") |
| `font_audit.py` | every code point the port's text uses (`port/ui/strings_*.c`, `subtitles.c`, `model_viewer_table.c`, the corpus in `port/ui/test/font_corpus/`) lies inside the unicode ranges the embedded font subset was built with (`port/ui/embed_font.cmake`); runs without a build (docs/port/UI.md, "The font") |
| `softdouble_gate.py [--root DIR] [--build DIR]` | the gate of docs/port/MATH.md, "Doubles": the 33 game functions that did `double` arithmetic through the EE's soft float, and the two helpers expanded into them, must do it through `port/math/softdouble.h` and use no host double arithmetic |
| `host_syntax_check.sh` | package 0B's front-end check with a 32-bit host gcc over `ico2/` (`-m32`: the 32-bit presets are retired, so it needs a multilib gcc); the CMake presets supersede it |

### Maintainers: the base ELF

| tool | what it does |
|---|---|
| `extract_elf.sh` / `extract_elf.py` | reads the user's disc image and writes `baserom/<ver>/baseelf.elf` and its `objcopy -O binary` view, and on PAL the disc's `MAIN.MAP`, `SRCFILE.TXT`, `TRFILE.TXT` and `SYSTEM.CNF`; checks the SHA-1s in `config/sha1sums.txt`. Needed only for the loader's reference test and `gen_data_desc.py --check-manifest`; the port itself reads the disc at run time |
| `ico_version.py` / `ico_version.sh` | the branch's target slug and its base file paths, for the Python and shell tools |
| `tm2_sheets.py [--iso ISO] [--out DIR] [--only jim\|menu] [--dark]` | decodes the game's text pictures from the user's disc to PNGs under `build/sheets/` (gitignored), for transcribing their words (docs/port/UI.md "Menu text", "Subtitles"): walks DATA.DF's directory as `cdvd.c`'s `unifile_read_func` does, splits the ten subtitle files `jimakuFileName[]` names (`data_<LL><SS>.jim`) at 0x8800 bytes (one TIM2 a block, `jim/data_<LL><SS>/<block>.png`) and inflates COMMON.DF, STGTTL.DF and STGLOG.DF to take their `text/*.tm2` members (`menu_PAL_<LL>/*`, `title`, `exp`, `buttons`). TIM2: PSMCT32/24/16 and PSMT8/PSMT4 with a 16-, 24- or 32-bit CLUT (the 8-bit CLUT's CSM1 swizzle undone), GS alpha scaled to 255, the top mip level; `--dark` also writes each over black. Needs pycdlib; writes PNGs with zlib alone. Nothing it writes may be committed |
| `vu0_clobber_scan.py --elf BASE --syms SYMS [--candidates F...]` | the F10 check (docs/port/MATH.md, "Register side effects"): disassembles the base ELF's `.text` with `mips-linux-gnu-objdump -m mips:5900`, names it from a byte-identical symbol build (the decomp's `build/ico.syms.elf`), and runs a per-function dataflow over vf4-vf7 that reports every current-matrix read of a lane another routine wrote; with `--candidates`, each function's first set-clobber-read call sequence and where its reads take the matrix from. Exit 1 on a finding |
| `gen_data_c.py` | reads `config/data_members.pal.txt` and writes each data-only member `config/data_schema.pal.txt` types as C from the base ELF, with the committed symbol lists (`--symbol-map`, below). `gen_data_desc.py` imports its parsers and the loader's reference test compiles its output; nothing it writes is committed |

## `gen_data_c.py --symbol-map`

The PS2 build names each pointer in a data table after the symbol the layout
link `build/ico.layout.elf` places at its address, which needs the period
linker. The host build has no such link, so `--c` (and `--check`) also take
`--symbol-map` in place of `--layout ELF`:

```sh
.venv/bin/python tools/gen_data_c.py --c MEMBER --symbol-map --out build/data/MEMBER.c
```

The retail ELF is stripped and has no symbols to read. The map is built
from:

1. `config/symbol_addrs.pal.txt` and `config/symbol_addrs.pal.data.txt`
   (splat's `name = 0xADDR; // type:func` lines; `type:func` marks a
   function, a line with `can_be_referenced:False` is skipped, and a name
   listed at two addresses is treated as two files' statics, which the
   layout link does not export either);
2. the data members' own symbols in `config/data_members.pal.txt`;
3. `SUPPLEMENT` in `gen_data_c.py`: globals a table points at that neither
   list names (one today, `scpDummyGObj`), to move into
   `config/symbol_addrs.pal.data.txt`;
4. any `--extra-symbols FILE` in the same splat format (repeatable).

A pointer resolves exactly as in the layout mode: one global (functions for
function pointers, objects otherwise; `D_`, `func_` and `jtbl_` placeholders
never), or the member fails with the same errors.

With this mode the two symbol lists stop being posterity only: a rename in
`ico2/` of a function or object a data table points at must be made in them
too. A stale entry shows up as a link error in the host build (an `extern`
of a name nothing defines) and as a difference from the layout mode in the
PS2 build. On 2026-10-05 the lists named 6,338 globals; 1,058 of them are
not globals of the layout link (statics or old names) and three sit at
another address there (`display`, `iosPadActRequestEnable`,
`InitialObjPointer`); none of these is a table's pointer target.

Comparison, 2026-10-05: all 73 schema members written with `--layout
build/ico.layout.elf` and with `--symbol-map` into two directories; `diff -r`
reports no difference, and the layout-mode output equals the `build/data/*.c`
the build had written. `--check --symbol-map` passes for all 73 objects.
The tables point at 914 distinct addresses (913 functions, one object).
