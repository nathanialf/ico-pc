# tools/

Every file in this directory, with what it does. The port builds with CMake
(`../CMakeLists.txt`, `docs/BUILDING.md`); nothing here builds the game. The
PS2 ELF build's scripts (`../build.sh`, `build.sh`, `gen_ninja.py`,
`verify_elf.py`, the two GNU ld 2.10 patches and `setup.sh`'s ld and dvp-as
builds) were removed in package 5C. The tools work on the branch's target as
`ico_version.py` reports it (`main` is PAL retail, slug `pal`). `tools/cc/`
(the period compilers) and `tools/toolchain/` (the host build's compilers and
libraries) are fetched, not tracked.

### The host build's

| tool | what it does |
|---|---|
| `fetch_toolchain.sh` | fills `tools/toolchain/`: llvm-mingw, mingw-w64 gcc, CMake; calls `fetch_deps.sh` last. No root. Run once; CI caches the result |
| `fetch_deps.sh` | fills `tools/toolchain/deps/`: Vulkan-Headers, volk, SDL3 (Linux build and mingw release), DXC, libmpeg2, the validation layer (tests only) |
| `package_win.sh <label>` | the Windows test package for HEAD: `dist/ico-pc-<label>-win.zip` (docs/port/TESTING.md) |
| `package_linux.sh <label>` | the Linux package for HEAD: `dist/ico-pc-<label>-linux.tar.gz` with `ico_pc` and `libSDL3.so.0` (docs/port/STEAMDECK.md) |
| `gen_sources.py` | writes `cmake/IcoSources.cmake` from `config/link_order.pal.txt`; `--check` for CI and the hook |
| `gen_layout_asserts.py` | writes `port/test/layout_asserts.c` from the headers' offset comments; `--check` |
| `gen_data_desc.py` | writes the data tables' field descriptors under `port/data/gen/` from the schema, headers and symbol lists; `--check` (never opens the ELF) and `--check-manifest --elf` (maintainers) |
| `check_no_rom.sh` | IP guard: refuses disc images, PS2 executables, the disc's reference files (`MAIN.MAP`, `SRCFILE.TXT`, ...), extracted assets, large binaries and raw byte-array initializers in tracked C. CI runs it over the tree |
| `format.sh` / `format_layout.py` | clang-format with the tracked `.clang-format`, then the top-level blank-line layout; `--check` for CI and the hook |
| `install_hooks.sh` | writes the pre-commit hook: `check_no_rom.sh`, `format.sh --check` on the staged C, and the three `--check`s above |
| `setup.sh` | idempotent: the venv from `requirements.txt`, the period compilers (skip with `SKIP_TOOLCHAIN=1`), the git hooks |
| `requirements.txt` | the venv's packages: pyelftools, pycdlib, ninja, clang-format |
| `host_syntax_check.sh` | package 0B's front-end check with a 32-bit host gcc over `ico2/`; the CMake presets supersede it |

### Maintainers: the EE identity check and the base ELF

| tool | what it does |
|---|---|
| `ee_identity.sh` | `tools/ee_identity.sh [-r REV] (--all \| FILE.c...)`: compiles the files with ee-gcc from the working tree and from a temporary worktree of REV (default HEAD) and diffs the sections and relocations (docs/BUILDING.md, appendix) |
| `compile_c.sh` | compiles one C source the way the PS2 build did: ee-gcc 2.9-991111 with the flags of the source's origin (the game `-g -G 8 -fno-common`, Sony's SDK archives `-G 0`, libc, libm and libgcc `-G 0 -fno-builtin`), then the assembler of its archive; `DUMP_DIR` adds gcc's RTL dumps. `ee_identity.sh` runs it |
| `period_env.sh` / `period_obstack.c` | runs a period toolchain binary with `period_obstack.c` preloaded, which restores the obstack chunk size of the machine that built the game; ee-as's R5900 short-loop padding depends on it |
| `extract_elf.sh` / `extract_elf.py` | reads the user's disc image and writes `baserom/<ver>/baseelf.elf` and its `objcopy -O binary` view, and on PAL the disc's `MAIN.MAP`, `SRCFILE.TXT`, `TRFILE.TXT` and `SYSTEM.CNF`; checks the SHA-1s in `config/sha1sums.txt`. Needed only for the loader's reference test and `gen_data_desc.py --check-manifest`; the port itself reads the disc at run time |
| `ico_version.py` / `ico_version.sh` | the branch's target slug and its base file paths, for the Python and shell tools |
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
