# tools/

Every file in this directory, with what it does. The tools work on the
branch's target as `ico_version.py` reports it (`main` is PAL retail, slug
`pal`). `tools/cc/` is fetched and built by `setup.sh` and is
not tracked. `../build.sh` at the repository root runs `setup.sh` and
`extract_elf.sh` when their output is missing, then `build.sh setup` (or
`build.sh verify` once `build.ninja` exists) and ninja.

| tool | what it does |
|---|---|
| `setup.sh` | idempotent host setup: the venv from `requirements.txt`; the period compilers ee-gcc 2.9-991111 and ee-gcc 2.96 (for its SCE 2.10 assembler) from decompme/compilers into `tools/cc/`; a check for `mips-linux-gnu-objcopy`; GNU ld 2.10 with the two patches below and ps2dev's dvp-as, built from public source; the git hooks |
| `requirements.txt` | the venv's Python packages: pyelftools, pycdlib, ninja, clang-format |
| `binutils-2.10-ee.patch` | the R5900 machine and the DVP overlay section types for GNU ld 2.10, backported from ps2dev's `binutils-2.14-PS2.patch`; applied by `setup.sh` |
| `binutils-2.10-dvp-ld.patch` | the Cygnus "sky" ld's DVP rule for GNU ld 2.10: each `.DVP.overlay.*` orphan gets its own output section at address 0 (from the GPL ee-gcc 2.9-991111 combined tree's `ld/emultempl/elf32.em`); applied by `setup.sh` after the first |
| `install_hooks.sh` | writes the pre-commit hook: `check_no_rom.sh` and `format.sh --check` on the staged C |
| `extract_elf.sh` / `extract_elf.py` | reads the user's disc image and writes `baserom/<ver>/baseelf.elf`, its `objcopy -O binary` view `baseelf.rom`, and on PAL the disc's `MAIN.MAP`, `SRCFILE.TXT`, `TRFILE.TXT` and `SYSTEM.CNF`; records or checks the SHA-1s in `config/sha1sums.txt` |
| `ico_version.py` / `ico_version.sh` | the branch's target slug (the first `config/link_order.<slug>.txt` that exists) and its base file paths, for the Python and shell tools |
| `build.sh` | `setup` (delete `build/`, verify the base ELF and ROM SHA-1s with `verify_elf.py`, write `build.ninja`), `verify` (the SHA-1s alone), `regen`, `clean`, `distclean` |
| `gen_ninja.py` | writes `build.ninja` from `config/link_order.pal.txt`: a compile rule per C source (`compile_c.sh`), an assemble rule per `.s` source with its archive's assembler, the period cpp (from standard input) and dvp-as for the VU1 programs in `ico2/vusrc/`, the data-only members (`gen_data_c.py` for the schema's members: a stand-in, a layout link, the C, its check), and the link with GNU ld 2.10 and `config/link.pal.ld` |
| `compile_c.sh` | compiles one C source: ee-gcc 2.9-991111 with the flags of the source's origin (the game `-g -G 8 -fno-common`, Sony's SDK archives `-G 0`, libc, libm and libgcc `-G 0 -fno-builtin`), then the assembler of its archive (ee-as 2.9-991111 for the game, libc, libm and libgcc; SCE's 2.10 assembler for the SDK archives); `DUMP_DIR` adds gcc's RTL dumps under the same flags |
| `period_env.sh` / `period_obstack.c` | runs a period toolchain binary with `period_obstack.c` preloaded, which restores the obstack chunk size of the machine that built the game; ee-as's R5900 short-loop padding depends on it |
| `gen_data_c.py` | reads `config/data_members.pal.txt` (its table parser also serves `gen_ninja.py`) and writes each data-only member `config/data_schema.pal.txt` types as one C translation unit from the user's own base ELF (`build/data/<member>.c`, record types from the header its rows name, pointers named from the layout link `build/ico.layout.elf`, or with `--symbol-map` from the committed symbol lists, its own strings as literals), its zero stand-in for that link, the linker assignments for `D_<VMA>` labels inside it, and the check of the compiled object against its ROM ranges; nothing it writes is committed |
| `verify_elf.py` | checks a file's SHA-1 against `config/sha1sums.txt` |
| `check_no_rom.sh` | IP guard: refuses disc images, PS2 executables, the disc's reference files (`MAIN.MAP`, `SRCFILE.TXT`, ...), extracted assets, large binaries and raw byte-array initializers in tracked C |
| `format.sh` / `format_layout.py` | clang-format with the tracked `.clang-format`, then the top-level blank-line layout; `--check` for the pre-commit hook |

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
