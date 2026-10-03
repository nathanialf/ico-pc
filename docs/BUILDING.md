# Building

The long form of the README's quickstart: what the host needs, what each step
does, and how the hooks use the build.

## Host

A 64-bit Linux host. The period compilers are 32-bit i386 binaries, and
`tools/period_env.sh` builds a 32-bit preload library with `gcc -m32`, so the
host needs 32-bit libraries and multilib gcc. On Debian or Ubuntu:

```sh
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install git curl xz-utils make patch gcc gcc-multilib \
    python3 python3-venv libc6:i386 libstdc++6:i386 zlib1g:i386 \
    binutils-mips-linux-gnu
```

`binutils-mips-linux-gnu` provides `mips-linux-gnu-objcopy`, which writes the
ROM views (`tools/extract_elf.py`, `tools/compile_c.sh`, the ninja `rom`
rule). Other distributions need the equivalent packages. The first run needs
network access to fetch the tools listed below.

You also need your own image of the PAL disc, SCES-50760, as a plain ISO. The
build accepts exactly one revision:

| file | SHA-1 (`config/sha1sums.txt`) |
| --- | --- |
| `baserom/pal/baseelf.elf`, the disc's `SCES_507.60` | `da3644c54c26fe760f3b6a591a5fc2eab396ed2b` |
| `baserom/pal/baseelf.rom`, its `objcopy -O binary --gap-fill=0x00` view | `a401d1e5a20b1659189a8b1026a8eb35811dc9ca` |

## One command

```sh
mkdir -p baserom
cp "/path/to/Ico (Europe).iso" baserom/Ico_PAL.iso
./build.sh
```

`./build.sh` runs the steps below that are not already done, in order, and
stops with a non-zero exit at the first failure. On a four-core host with
the toolchain already installed and `baserom/pal/` already extracted,
`rm -rf build build.ninja && tools/build.sh setup && .venv/bin/ninja` took
22 s and 39 s in two runs; a fresh clone, with the toolchain downloads of
section 1, took 92 s on the same host (both measured on the decompilation
before the fork). A second `./build.sh` on the built tree rebuilds nothing
(`ninja: no work to do.`). The sections
below describe each step; each can also be run on its own.

## 1. Host setup: `tools/setup.sh`

`./build.sh` runs it when the venv, ninja or any of the four tools under
`tools/cc/` is missing. It is idempotent: a second run finds each tool and
skips it.

1. Creates `.venv/` and installs `tools/requirements.txt`: pyelftools (ELF
   reading), pycdlib (the ISO reader), ninja and clang-format.
2. Fetches **ee-gcc 2.9-991111-01** into `tools/cc/ee-gcc2.9-991111/` from
   the `decompme/compilers` releases. Its bundled `as` (ee-as 2.9-991111)
   assembles the game, libc, libm and libgcc. It also fetches **ee-gcc 2.96**
   into `tools/cc/ee-gcc2.96/` for its bundled SCE assembler
   (2.10-ee-001003-1), which assembles Sony's SDK archives; that compiler is
   never run. The script warns when a compiler is present but does not run
   (missing 32-bit libraries).
3. Checks for `mips-linux-gnu-objcopy`.
4. Builds the linker and the VU assembler from public GPL source (next
   section). On a four-core host the dvp-as source fetch takes about 20 s,
   the dvp-as build about 30 s and ld 2.10 about 17 s (`tools/setup.sh`
   comments).
5. Installs the git hook (`tools/install_hooks.sh`, below).

`SKIP_TOOLCHAIN=1` skips steps 2 to 4.

### The linker and the DVP assembler

| tool | source | licence | built as |
| --- | --- | --- | --- |
| `tools/cc/binutils-2.10-ee/bin/ld` | GNU binutils 2.10, `https://ftp.gnu.org/gnu/binutils/binutils-2.10.tar.gz` (sha256 pinned in `tools/setup.sh`), with `tools/binutils-2.10-ee.patch` and `tools/binutils-2.10-dvp-ld.patch` | GPL-2.0-or-later | `--target=mipsel-elf`, `make all-ld` |
| `tools/cc/dvp-as/bin/dvp-as` | ps2dev `binutils-gdb`, branch `dvp-v2.45.1`, commit `3eb45ea37f0efd498d1de3cf9562de07197aefa8` (`https://github.com/ps2dev/binutils-gdb`) | GPL-3.0-or-later | `--target=dvp`, `make all-gas` |

The first patch backports, from ps2dev's `binutils-2.14-PS2.patch`
(`github.com/ps2dev/ps2toolchain` commit `aa984e7`), the R5900 machine (so
the inputs' `e_flags` mach bits 0x00920000 survive the link) and the DVP
overlay section types (`.DVP.ovlytab` with its `sh_link`, `.DVP.ovlystrtab`,
`.DVP.overlay.*`). The second is the linker side of the overlays: dvp-as
fills `.DVP.ovlytab` itself with relocations, and the Cygnus linker in the GPL
ee-gcc 2.9-991111 combined tree (`ld/emultempl/elf32.em` `place_orphan`,
published as `github.com/polybiusproxy/parappa2_gcc` commit `620426a`) gives
every `.DVP.overlay.*` orphan its own output section at address 0, as the
retail ELF has them. Target `mipsel-elf` writes `elf32-littlemips`, the output
format MAIN.MAP names.

Neither build needs bison, flex or makeinfo. The 2.10 tarball ships its
generated parsers, but its configure stops when it finds no lex or yacc, so
the script answers that probe with cache variables and make never regenerates
the shipped files. The dvp target's gas has no generated parser, and
`MAKEINFO=true` skips the manuals. The 2.10 tree's `config.sub` and
`config.guess` predate x86_64 hosts; the dvp clone's copies replace them. The
2.10 sources need `gcc -std=gnu89 -fcommon`. Each tool has a stamp keyed on
its commit, or on the tarball and patch hashes, and is rebuilt when that
changes.

## 2. The base ELF: `tools/extract_elf.sh`

`./build.sh` runs it when `baserom/pal/baseelf.elf` is missing, and says so
plainly when `baserom/Ico_PAL.iso` is missing too. The script reads the
disc's ISO9660 filesystem with pycdlib (`tools/extract_elf.py`), extracts the
boot file named by `SYSTEM.CNF`'s `BOOT2` line, and writes
`baserom/pal/baseelf.elf` and its `objcopy -O binary` view
`baserom/pal/baseelf.rom`. It checks both SHA-1s against
`config/sha1sums.txt` and stops on a mismatch. It also copies the disc's
`MAIN.MAP`, `SRCFILE.TXT`, `TRFILE.TXT` and `SYSTEM.CNF` into `baserom/pal/`
as reference material (`docs/LEGAL.md`). Everything under `baserom/` is
gitignored.

## 3. `tools/build.sh setup`

`./build.sh` runs it when `build.ninja` is missing, and otherwise runs
`tools/build.sh verify`, which only checks the two SHA-1s; `build.ninja`
rewrites itself when `tools/gen_ninja.py` or one of its inputs changes.

`setup` deletes `build/` and ninja's state, verifies the SHA-1s of the base
ELF and ROM (`tools/verify_elf.py`), and writes `build.ninja` with
`tools/gen_ninja.py` from these inputs:

- `config/link_order.pal.txt`: every object of the link in the retail link's
  order, one source per line, with the data-only members as `data:` lines.
  `tools/gen_ninja.py` fails if a tracked `.c`, `.s`, `.S` or `.dsm` under
  `ico2/` or `sce/` is missing from it.
- `config/link.pal.ld`: the hand-written linker script, which places each
  section at the base ELF's addresses and defines the symbols the link needs.
- `config/data_members.pal.txt`: the data-only members (member, section,
  address range, MAIN.MAP's names at their offsets).
- `config/data_schema.pal.txt`: the data-only members written as C (member,
  section, element type, the game header that defines it, element count,
  MAIN.MAP's names).

`tools/build.sh` also has `verify` (the SHA-1s only), `regen` (rewrite
`build.ninja` only), `clean` (delete `build/`), `distclean` (also
`build.ninja` and ninja's state).

## 4. `ninja`

`.venv/bin/ninja`, or any ninja on the PATH. `build.ninja` does the following
(`tools/gen_ninja.py`):

- compiles every C source under `ico2/` and `sce/` with `tools/compile_c.sh`:
  ee-gcc 2.9-991111 with the flags of the source's origin (the game
  `-g -G 8 -fno-common`; Sony's archives `-G 0`; libc, libm and libgcc
  `-G 0 -fno-builtin`), then the assembler of its archive. Both run through
  `tools/period_env.sh`, which preloads `tools/period_obstack.c` to restore
  the obstack chunk size of the machine that built the game; ee-as's R5900
  short-loop padding depends on it;
- assembles each hand-written `.s` (`sce/crt0.s`, `sce/libkernl/klib.s`,
  `sce/libkernl/tlbtrap.s` and the R5900 string functions under
  `sce/libc/machine/r5900/`) with its archive's assembler and `-G`;
- assembles the five VU1 microprograms: the period cpp reads each program's
  text, `ico2/vusrc/<stem>.vsm`, from standard input in `ico2/` with
  `-Ivusrc` (it includes `vusrc/vu1_common.h` and, in normal_c and normal_l,
  `vusrc/scissorcommcut.h`) and writes `build/ico2/vusrc/<stem>.i`; dvp-as,
  run from `ico2/` with `-I../build/ico2`, assembles `vusrc/<stem>.dsm`, the
  DMA tags around `.include "vusrc/<stem>.i"`. The overlay section names
  dvp-as writes hash the name of the file it is reading and the line: the
  `.dsm` path for the first overlay of each program, the empty name cpp gives
  standard input and the include's path for the others;
- writes the data-only members from the base ELF into `build/data/`. Each
  of the 73, all listed in `config/data_schema.pal.txt`, is written as C by
  `tools/gen_data_c.py`: an initialized array of its record type per section,
  with every pointer named after the symbol at its address, floats as the
  shortest decimal that reads back to the same bits and names as string
  literals; a member's own string pool (staffroll_dat's) is written as the
  literals its pointers name, and the compiler lays it out; a count the
  schema marks `count-of=` (staffroll_dat's line count) is written as
  `sizeof` over the table, not read from the ELF. The addresses
  come from a first link, `build/ico.layout.elf`, in which a zero stand-in of
  the member's size takes its place. The C compiles with the game's flags
  like any `ico2/` source, each section of the object is checked against the
  member's ROM range with its relocations applied, and a label a source
  spells inside the member (`D_<VMA>`) is bound to the member's symbol plus
  its offset by `build/data/<member>.alias.ld`;
- links with ld 2.10 and `config/link.pal.ld`, once to `build/ico.syms.elf`
  (symbols kept, with the map `build/ico.pal.map`) and once stripped to
  `build/ico.elf`, as the base is.

Nothing checks `build/ico.elf` against the disc's ELF: this repository does
not require a byte-identical build. ninja does not
track header or `.c.inc` dependencies (the VU includes under `ico2/vusrc/`
are listed on the cpp step): after editing one, run
`tools/build.sh clean` before `ninja`.

## EUC-JP sources

Some game sources carry Japanese text in EUC-JP, and their string literals
are the ROM's bytes. `.gitattributes` lists each of them with
`working-tree-encoding=EUC-JP`: git stores the file as UTF-8 and checks it
out as EUC-JP, which is what the compiler reads. An editor or a pipe that
rewrites one of these files as UTF-8 changes its bytes; change them with an
ASCII patch and `git apply`.

## Hooks

`tools/install_hooks.sh` (run by `tools/setup.sh`) installs a pre-commit
hook that runs `tools/check_no_rom.sh` (refuses disc images, executables,
extracted assets and large binaries) and `tools/format.sh --check` on the
staged C.

`tools/format.sh` formats the tracked C with the tracked `.clang-format` and
then `tools/format_layout.py`'s top-level blank-line layout.

## Running the rebuilt ELF (optional)

Any PS2 emulator can run `build/ico.elf` as a sanity check, with
`baserom/Ico_PAL.iso` as the disc for the game's data files.
