# The game code

How the game's own C under `ico2/` is compiled for the host: which files, with which options and
warnings, how a host path is written inside the game sources, the record-layout conventions of a
64-bit build, the audits that guard them, how the tree relates to the decompilation it came
from, and how the one program plays both discs (the regions).

## Files

| path | what it holds |
| --- | --- |
| `ico2/<programmer>/` | the game's sources, one directory per programmer (`common`, `fumi`, `ito`, `omori`, `script`, `seki`, `sugipon`), each with `src/` (or `ios/`, `isys/`, `sound/`, and `ito/mpeg/`, the PS2 movie player that is not compiled) and `include/` |
| `ico2/vusrc/` | the VU1 [microprograms](ARCHITECTURE.md#terms), kept as the reference for the renderer's shaders; never compiled |
| `sce/` | Sony's SDK headers the game includes, and `sce/libsndn2/sound.c`, the one `sce/` source compiled |
| `config/link_order.pal.txt` | the retail link's object list: the inventory of the game's translation units and data members |
| `cmake/IcoSources.cmake` | the host source lists, generated from the link order |
| `cmake/IcoFlags.cmake` | the semantics options, the record-layout options and both warning sets |
| `cmake/IcoExclusions.cmake` | game sources left out of the build (both lists empty) |
| `ico2/common/include/typedef.h` | the host seams every game source sees (`ICO_WORD`, `ICO_PHYS`, `ICO_QW`, ...) |
| `config/regions.txt`, `ico2/common/include/region.h` | the game regions the program plays and their run-time forms ([Regions](#regions)) |
| `port/core/` | the region's index `ico_region` and each region's numbers for the port's own code (`IcoRegionInfo`) |
| `ico2/fumi/include/ee_view.h` | `ICO_RAW`, `ICO_RAWP`, `ICO_MAX_SIZE` and the layout registrations |
| `port/compat/` | the SDK header names on the host (`assert.h`, `eeregs.h`, `libcdvd.h`, `sifrpc.h`, ...) |
| `port/test/layout_asserts.c`, `config/struct_classes.txt` | the generated layout asserts and the struct classification they read |
| `port/data/gen/`, `config/tables_manifest.txt` | the generated data-table descriptors and their CRC list |
| `tools/gen_sources.py`, `tools/gen_layout_asserts.py`, `tools/gen_data_desc.py` | the generators, each with `--check` |
| `tools/strip_host_gates.py`, `tools/stack_overread_audit.py`, `tools/struct_cast_audit.py`, `tools/check_call_types.py`, `tools/check_int_arrays.py`, `tools/offset_audit.py`, `tools/template_audit.py`, `tools/check_const_writes.py`, `tools/softdouble_gate.py` | the audits over the game sources |
| `.gitattributes` | the EUC-JP working-tree encoding of 41 game sources |
| `tools/region_blocks.py`, `config/region_twins.txt`, `tools/test/region_blocks/` | the converter of upstream's region blocks, the table of hand-made twins, and the converter's fixture |

## What is compiled

`tools/gen_sources.py` writes `cmake/IcoSources.cmake` from `config/link_order.pal.txt`
(`read_link_order`): every `ico2/...c` line is a source and every `data:<member>` line a data
member, in the retail link's order. Each source lands in the list of its programmer directory
(`ICO_SOURCES_common` ... `ICO_SOURCES_sugipon`, in `ICO_PROGRAMMERS` order), except the ones under
`ico2/ito/mpeg/` (`EE_ONLY_DIRS`), which go to `ICO_EE_ONLY_SOURCES`: the PS2 movie player, nine
files `port/fmv` replaces, listed so the inventory stays complete and never compiled. The 73
`ICO_DATA_MEMBERS` are the data-only members (see [Data tables](#data-tables)). `sce/` and
`ico2/vusrc/` never enter the lists. Configure runs `tools/gen_sources.py --check` and prints a
warning when the file is stale (`CMakeLists.txt`); rerun the script after editing the link order.

`CMakeLists.txt` builds one object library per programmer directory (`ico_<programmer>`) from its
list, minus `ICO_BLOCKED_SOURCES` unless `ICO_BUILD_BLOCKED=ON`, and prints the count
(`ico: N game sources in the build`). Each library gets:

- the include path the period build gave that directory (`ico_include_dirs`): its own `include/`,
  then the others in the order `sugipon omori common ito fumi seki script`, then `port/compat/` for
  the SDK header names; the [window build](ARCHITECTURE.md#terms) adds `port/render`;
- the definitions `ICO_HOST=1` and either `ICO_RD=1` (the window build) or `ICO_HEADLESS=1`
  (`ICO_GAME_DEFINITIONS`);
- `-fmacro-prefix-map=<source>/ico2/<programmer>/=`, so `__FILE__` reads as the period build
  spelled it (`src/main.c`, `ios/pad.c`), which the assert messages print.

`ico2/common/src/main.c` is compiled with `main=ico_game_main`; the program's `main` is in
`port/platform/main_host.c` (`main`, `ico_host_main`), and the game's `ico_game_main` runs on the
boot [fiber](ARCHITECTURE.md#terms). The object libraries and `ico_data` (the table definitions and
the loader) are collected into the static library `ico_game`. `ico_pc` (only with `ICO_LINK_EXE=ON`)
links `ico_game`, `ico_platform` and the port's libraries, plus `port/null/gfx_null.c` and
`port/null/libgcc_null.c` compiled into the program itself, since they call back into the game
(`CMakeLists.txt` (the game library and the program sections)).

`cmake/IcoExclusions.cmake` holds `ICO_BLOCKED_SOURCES` (fails with every compiler) and
`ICO_BLOCKED_SOURCES_CLANG` (fails with clang only). Both are empty: every game source compiles with
gcc and clang. The file is the place to park a source that stops compiling, with its reason
(`vu0-asm`, `nested-fn`, `decl`); `ICO_BUILD_BLOCKED=ON` compiles the parked sources anyway to
recheck them.

Of `sce/`, only `sce/libsndn2/sound.c`, the [EE](ARCHITECTURE.md#terms) side of the Sg sound
sequencer, is compiled, into `ico_sndn2` (`port/audio/CMakeLists.txt`). It takes the semantics
options and `ICO_HOST=1`; its target's port warning set is followed by the game's warning set and
five `-Wno-` switches for this file (`port/audio/CMakeLists.txt` (`ico_sndn2`)); its header comment
lists the
host seams it carries (pointer arrays where the EE kept addresses in 32-bit context words).

## Semantics options

`cmake/IcoFlags.cmake` (`ICO_SEMANTIC_OPTIONS`) holds the options that make the host compiler
treat the code as ee-gcc 2.9 did for the MIPS R5900 [EE](ARCHITECTURE.md#terms). The game, the
generated tables and every `port/` target take them:

| option | why |
| --- | --- |
| `-std=gnu11` | GNU extensions the reconstruction uses: statement expressions, `typeof`, zero-length arrays (`CMAKE_C_STANDARD 11` with `CMAKE_C_EXTENSIONS ON` in `CMakeLists.txt`) |
| `-fno-strict-aliasing` | the game reads records through other types |
| `-fwrapv` | signed overflow wraps, as on the EE's integer unit |
| `-ffp-contract=off` | no fused multiply-add: the EE rounds each step |
| `-fno-fast-math` | IEEE ordering; the EE's float semantics are added by `port/math`, never by the compiler |
| `-fsigned-char` | ee-gcc's MIPS default |
| `-fno-common` | the game was built that way |
| `-fgnu89-inline` | ee-gcc 2.9's `inline`: a plain `inline` definition is also the external one (`script.h` declares `scpFadeIn` and others `inline`, and other units call them); C99 semantics would leave them undefined at link |

`ICO_GAME_LAYOUT_OPTIONS` is the record-layout rule of the game and data units alone:
`-mno-ms-bitfields` on Windows, so bit-fields follow the GCC rules the EE used rather than the MS
rules mingw targets default to. `port/` keeps the platform ABI, which SDL's and the Windows SDK's
structs assume, so a record that game code and `port/` code both read (an SDK parameter block, the
pad buffer, a card directory entry) must have a layout that does not depend on the option, or the
`port/` side must be compiled with the layout options too (`ico_gallery_live` in
`port/ui/CMakeLists.txt` is).

No configuration defines `NDEBUG` (`CMakeLists.txt` strips it from the release flags): the retail
game ran with its asserts live, so an assert's expression is evaluated as on the PS2. See
[Asserts](#asserts).

## Warnings

There are two warning sets (`cmake/IcoFlags.cmake`):

- `ICO_GAME_WARNINGS`, for `ico2/`, `sound.c` and the generated tables: `-Wreturn-type
  -Wimplicit-function-declaration -Wstrict-prototypes` as warnings, plus `-Wno-error=` for the
  C89-era diagnostics modern compilers make errors by default (implicit declarations and `int`,
  int/pointer conversions, incompatible pointers, return mismatches), each added only when the
  compiler knows it (`check_c_compiler_flag`). Clang also gets `-Wno-invalid-source-encoding` for
  the EUC-JP literals. With `ICO_STRICT_WARNINGS=ON` the first three become errors; the option is
  off by default and no CMake preset sets it (`cmake/IcoFlags.cmake`; `CMakePresets.json`), and
  the game sources still raise these warnings, most of them `-Wstrict-prototypes`.
- `ICO_PORT_WARNINGS`, for every `port/` target: `-Wall -Wextra -Wno-unused-parameter -Werror` and
  the three diagnostics above as errors, on every preset and both compilers. A new warning in
  `port/` fails the build. A port unit that includes a game header still declaring an unprototyped
  function (`typedef.h`, `thread.h`, `act.h`, `s_init.h`, `debug.h`) adds `-Wno-strict-prototypes`
  for itself in its `CMakeLists.txt` (`port/ui/CMakeLists.txt` has several); no other warning is
  switched off for port code. Test programs that compile game sources into themselves take
  `ICO_GAME_WARNINGS` for those files.

## Host paths in the game sources

`ico2/` and `sce/libsndn2/sound.c` are only ever compiled for the host. A change to them that
exists because the code runs on a PC (a 64-bit pointer, a hardware register that is plain memory,
an SDK call served by `port/`) is made directly in the code: the host form replaces the original
spelling, with no `#ifdef ICO_HOST` / `#else` arm carrying the PS2 text, and the tree does not
compile for the PS2. `ICO_HOST` stays defined (`ICO_GAME_DEFINITIONS`), and headers under `port/`
may test it. `tools/strip_host_gates.py --check` fails on any `ICO_HOST` conditional under `ico2/`
or `sce/`; run without `--check`, it resolves such conditionals in place (keeping the live arm,
working on bytes so the EUC-JP files are untouched elsewhere).

Conditionals that choose between host build variants stay. In `ico2/` they are `ICO_RD` (the window
build's path onto `port/render`, about 200 sites, most in `seki/src`: `RegistPacket.c`, `GsBase.c`,
`Texture.c`, `Shadow.c`, `Primitive.c`, `GifPacket.c`, `DisplayList.c`, and `sugipon/src/pool.c`),
`ICO_HEAP_STATS` and `ICO_HEAP_ASAN` (`fumi/ios/memory.c`), and `ICO_OFFSET_AUDIT`
(`fumi/include/ee_view.h`); `sound.c` has one on `ICO_UNCACHED`. `ICO_HEAP_ASAN` is a preprocessor
switch, not a CMake option: defined by hand on an AddressSanitizer build (`memory.c` stops with an
error without `-fsanitize=address`), it poisons the game heap's block headers, slack and free areas;
`ICO_HEAP_ASAN_FREE=1` in the environment also poisons free areas' bodies.

A comment that starts `PC port` marks a host path and says what the original did and why the
host differs. `grep -a -rn "PC port" ico2/` finds about 400, most of them in `ico2/seki/`; they
cluster where the hardware was:

| directory | mostly |
| --- | --- |
| `ico2/seki/` | the [GS](ARCHITECTURE.md#terms) packet layer, textures, display lists, the file loader |
| `ico2/sugipon/` | effects and their packet code |
| `ico2/common/` | `debug.c`, `main.c`, the menus' layout code |
| `ico2/fumi/` | `ios/` (threads, cdvd, memory card, pad), actors |
| `ico2/omori/` | generators and objects |
| `ico2/script/` | the event scripts |
| `ico2/ito/` | the Queen's barrier |

Other host lines carry a short trailing comment instead (`/* (int) on the EE: pointer-wide on the
host */` in `ico2/fumi/ios/cdvd.c`). Use `grep -a`: the EUC-JP files otherwise read as binary.

## Pointer width and record views

The EE has 32-bit pointers; the host has 64-bit ones. `ico2/common/include/typedef.h` (the "Host
seams" block) and `ico2/fumi/include/ee_view.h` give the game code one spelling per case:

| name | where | what |
| --- | --- | --- |
| `ICO_WORD` | `typedef.h` | a struct field or local the original holds an address in as an `int`: `__INTPTR_TYPE__`, so the address survives and the integer arithmetic on it (byte offsets, masks) means the same |
| `ICO_WORD_PTR(T)` | `typedef.h` | the same for a word only ever converted to a pointer: the pointer type `T` |
| `ICO_PHYS(p)`, `ICO_UNCACHED(p)`, `ICO_UNCACHED_ACCEL(p)` | `typedef.h` | the physical, uncached and uncached-accelerated aliases of an EE address: the identity on the host's flat address space, so a pointer stays a pointer |
| `ICO_ADDR(p)`, `ICO_INVALID_PTR`, `ICO_SPR_ADDR(off)` | `typedef.h` | a pointer where the original held an int address; the all-ones "none" pointer; an address in the 16 KB scratchpad stand-in `ico_scratchpad` (`seki/src/Basic.c`) |
| `ICO_QW`, `ICO_UQW`, `ICO_POSTINC(T, p)`, `ICO_BREAK()` | `typedef.h` | the 128-bit quadword; ee-gcc's cast-as-lvalue post-increment; the debug trap |
| `ICO_RAW(T, p, off, field)`, `ICO_RAWP(T, p, off, field)` | `ee_view.h` | an access the original made at an EE offset (`*(T *)((char *)p + off)`), written as the named field; the offset stays in the text for `tools/offset_audit.py` |
| `ICO_MAX_SIZE(T, lit)` | `ee_view.h` | the size of a record the original allocated or cleared with a literal: the literal, or `sizeof(T)` when the host record is wider (`memset(&w, 0, ICO_MAX_SIZE(ClipWork, 0xC0))` in `fumi/src/commonact.c`) |
| `ICO_LAYOUT_AT`, `ICO_LAYOUT_AT_FROM`, `ICO_LAYOUT_SIZE` | `ee_view.h` | `_Static_assert` registrations that one record copied whole over another lines up on the host (asked for by `tools/template_audit.py`) |

## Hardware the game touches directly

Three kinds of code reached the EE's hardware without an SDK call; each has one host rule.

- **Registers.** `port/compat/eeregs.h` gives the register names of the SDK's `eeregs.h` as
  addresses in two plain memory blocks, the EE I/O page and the [GS](ARCHITECTURE.md#terms)
  privileged page (`ico_hw_eeio`, `ico_hw_gs`, defined in `port/platform/hwregs.c`). A store lands
  there and a load reads back the last store; nothing reacts. A loop that polls a register
  (`GS_CSR`, a [DMA](ARCHITECTURE.md#terms) channel's busy bit) therefore never sees it change, so
  such a loop is replaced at its call site by the `port/` code that owns the device.
- **Inline assembly.** The [VU0](ARCHITECTURE.md#terms) macro-mode and R5900 assembly of the game
  has C bodies over `port/math`; no compiled game source holds an `asm` statement (the only one left
  is in `ico2/ito/mpeg/mv_main.c`, an `ICO_EE_ONLY_SOURCES` file).
-  **Floating point.** The simulation runs in the environment `port/platform/fpenv.h` describes:
  `ico_fpenv_sim_enter` selects round toward zero with flush-to-zero and denormals-are-zero (MXCSR
  on x86-64, FPCR on arm64), the closest IEEE mode to the EE's FPU, and `ico_fpenv_host_enter`
  restores the host defaults for the platform layer, SDL and the renderer. The EE's other float
  differences (no infinities or NaNs, clamped results, its own division and square root) are
  `port/math`'s helpers, used at the call sites that need them. The `fptrap` CMake preset
  (`ICO_FPTRAP`) also unmasks divide-by-zero and invalid in simulation mode; `fpenv_test` checks
  both modes.

## Layout asserts

The game's headers carry the offsets the reconstruction recovered as `/* 0xNN */` comments on
struct members. `tools/gen_layout_asserts.py` reads every header and writes
`port/test/layout_asserts.c`: one `offsetof` assert per commented member and per `pad<HEX>` member,
and a `sizeof` assert where the size is known. Every struct it touches has a line in
`config/struct_classes.txt`:

```
<name> <class> [size=0xNN] [base=0xNN] [pending=<note>] [nosize] [# evidence]
```

The class is `runtime` (natural host layout, real pointers), `overlay` (read directly from disc or
ELF bytes, so frozen) or `save` (serialised into the save image or the card files, so frozen). On
the host only the `overlay` and `save` structs are asserted; the `runtime` ones compile only with
`-DICO_LAYOUT_EE=1` and document the EE layout. `pending=` holds a frozen struct's asserts back
(none is pending; `ICO_LAYOUT_PENDING` compiles them anyway). `port/test/CMakeLists.txt` compiles
the file as the game compiles (include path, definitions, layout options), so a frozen layout that
moves fails the build; `layout_asserts_fresh` (and the pre-commit hook and CI) runs `--check`.

## Audits

Each audit is a Python tool whose docstring says what it looks for; each fails (exit 1) on a
finding its allow list does not name. The allow lists of `stack_overread_audit.py`,
`struct_cast_audit.py`, `offset_audit.py` and `template_audit.py` also fail on an entry that
matches nothing, so they cannot go stale; `check_call_types.py`'s does not.

| tool | refuses | runs in |
| --- | --- | --- |
| `tools/strip_host_gates.py --check` | any `ICO_HOST` conditional under `ico2/` or `sce/` | pre-commit, CI |
| `tools/region_blocks.py check` | a `VERSION_` block or word under `ico2/` or `sce/`, a conditional on the region, a selector macro in a constant context, a file naming the region macros that does not reach `region.h`, a stale `region.h` or `port/core/region_table.c`, a missing per-region piece, a stale `config/region_twins.txt` row ([Regions](#regions)); `--selftest` runs `tools/test/region_blocks/`. The hook passes raw blocks that are all hand items with a warning; CI does not | pre-commit, CI |
| `tools/stack_overread_audit.py` | a call that reads or writes more bytes through a pointer to a local than the local holds (a 64-byte matrix copy from a 48-byte local), and a local that is only written: code that relied on the period compiler's stack layout. Allow list `tools/stack_overread_allow.txt`; `--selftest` runs `tools/test/stack_overread/` | pre-commit, CI |
| `tools/struct_cast_audit.py` | a fixed array or smaller variable cast to a struct larger on the host than it (a buffer sized for the EE's 0xC0-byte `ClipWork`, which is 0xF0 on the host); sizes come from compiling the file with probes. Allow list `tools/struct_cast_allow.txt`; `--selftest` runs `tools/test/struct_cast/` | pre-commit, CI |
| `tools/check_call_types.py` | a call through a function-pointer cast passing fewer arguments than the function reads, and an `extern` in an `ico2/` `.c` file whose parameter count, integer widths or return width differ from the definition (a missing argument is whatever the register held on x86-64 and arm64). Allow list `tools/check_call_types_allow.txt` | pre-commit, CI |
| `tools/check_int_arrays.py` | a large integer array whose initialiser is all integer literals: the shape of bytes copied out of the binary. Exemptions are per path and array name in the tool (`EXEMPT`) | `tools/check_no_rom.sh` (pre-commit, CI) |
| `tools/offset_audit.py --build DIR` | a raw EE-offset access (`(char *)x + 0xNN`, `((int *)x)[N]`, a scalar view of a record, `ICO_RAW` / `ICO_RAWP`, `pad<HEX>` members, constant-size clears and copies) whose host offset is not the named field's (MISMATCH, NOFIELD), a pointer through `int` (TRUNC), a pointer-wide value held in 32 bits (NARROW), a call through an unprototyped declaration (UNPROTO) | ctest `offset_audit` (native builds) |
| `tools/template_audit.py --build DIR` | a whole-record copy between two record types without `ICO_LAYOUT_*` registrations in its unit, or whose host layouts differ; also writer/reader pairs through `void *` (mail data) | ctest `template_audit` (native builds) |
| `tools/check_const_writes.py --build DIR` | a store, `memcpy`, `memmove` or `memset` into a global the unit declares `const`, found in clang's unoptimised IR (clang deletes such stores where gcc keeps them; the EE had no page protection) | ctest `const_write_audit` (native clang builds), `tools/check_android_flags.sh` |
| `tools/softdouble_gate.py` | a `double` keyword or unsuffixed float constant in the 33 game functions that did double arithmetic through the EE's soft float (they use `port/math/softdouble.h`); with `--build`, a `-Wdouble-promotion` warning inside them | ctest `softdouble_gate` (native builds) |

`tools/check_android_flags.sh` also runs `tools/check_stack_guard.py` on the Android build (no
store onto a stack protector's guard word; only that build has `-fstack-protector-strong`). The CI
jobs and the pre-commit hook are listed in [BUILDING.md](../BUILDING.md#continuous-integration)
and [CONVENTIONS.md](CONVENTIONS.md#the-pre-commit-hook).

## Asserts

The game's `assert.h` is newlib's: `assert()` calls `__assert(file, line, expression)`, and the
reconstructed sources also call `__assert` directly. `port/compat/assert.h` stands in for it on the
game's include path, defines `__assert` as `ico_assert` (away from glibc's `__assert`, whose
arguments are in another order) and redefines `assert` at every inclusion as the C library does.
`ico_assert` (`port/platform/assert_host.c`) prints newlib's message (`assertion "e" failed: file
"src/x.c", line N`) on stderr, records it as the run's failure (`ico_diag_set_failure`), and calls
`abort()`, which the crash handler reports with the same message.

## Data tables

The program holds no disc data. The 73 data-only members of the link order (the game's data
tables) are defined on the host without initialisers in `port/data/gen/table_defs.c`, so they land
in `.bss`, in PAL's layout; `port/data/tables.c` fills them at boot from the boot ELF on the
player's disc, record by record, as that disc's descriptor set describes
(`port/data/gen/<slug>/table_desc.c`, one per row of `config/regions.txt`), and resolves pointer
words through the set's address registry (`port/data/gen/<slug>/ee_symbols.c`). Each of the 75 rows
of a set's manifest (`config/tables_manifest.txt`, `tables_manifest.jpn.txt`: a member's address
range in a section, three for `staffroll_dat`) carries a CRC-32 the loader checks before writing
anything. The Japanese set maps the disc's own record layouts (`config/ee_layout.jpn.txt`) onto the
PAL host structs by field name ([DATA.md](DATA.md#the-two-descriptor-sets)). The generated files hold types, offsets, sizes, names and CRCs only;
`tools/gen_data_desc.py --check` (the `data_desc_fresh` test, the pre-commit hook and CI) keeps them
fresh, and never opens an ELF. [DATA.md](DATA.md#the-data-tables) describes the loader.

No table is `const` on the host: the loader writes them all, and the game writes three tables the
PS2 build kept in `.rodata` (`stageTable`, `motionLimitDef`, `seDef`). The generated definitions
rename a header's `extern const` declaration around the `#include`s (`table_defs.c`), and
`check_const_writes.py` looks for other writes to `const` objects (`CMakeLists.txt` (the
`ico_data` comment)).

The maintainer step: with the base ELF (`ICO_BASE_ELF`, default `baserom/pal/baseelf.elf`, written
by `tools/extract_elf.sh`) and pyelftools present, `port/data/CMakeLists.txt` generates each
member's reference table with `tools/gen_data_c.py --c <member> --symbol-map` into the build tree,
compiles them under renamed symbols (`ico_ref_<name>`, from `port/data/gen/tables.cmake`), and
builds `tables_test`, which compares every byte the loader writes with them (`tables_loader`).
`tables_manifest` checks the manifest against the ELF (`gen_data_desc.py --check-manifest`), and
`gen_data_desc.py --manifest` rewrites it. Without the ELF both tests are left out, and configure
says so. With the Japanese ELF as well (`ICO_BASE_ELF_JPN`, default `baserom/jpn/baseelf.elf`), a
second reference (`ico_ref_jpn_<name>`, `gen_data_c.py --version jpn --c <member>`: the Japanese
bytes in the PAL record's form, through the same map the descriptor set is written from) backs
`tables_loader_jpn` (`tables_test --set jpn`), and `tables_manifest_jpn` checks
`config/tables_manifest.jpn.txt`; both skip without that ELF.

## The decompilation

The decompilation (<https://github.com/nathanialf/ico>) is the byte-matched reference: it rebuilds
the PAL boot ELF with the period toolchain. Add it as the remote `upstream`
(`git remote add upstream https://github.com/nathanialf/ico.git`) and merge `upstream/main` into
`main`. What comes down that way is what the program is, not where it runs: a reconstruction bug
the port finds (a wrong type, field, operand or control flow), a name, a struct or union layout, a
header declaration.

Such a fix is made in the decompilation first, under its rules (byte match against the ROM, the
period toolchain, its hooks), and reaches this tree through the merge. Conflicts in `ico2/` resolve
to the host form here while taking the upstream names, types and fixes; `strip_host_gates.py`
removes any `ICO_HOST` conditional a merge brings in. A merge that applies cleanly is read too:
the decompilation spells an address carried in an int as `(int)p`, which truncates a 64-bit
pointer, so each added `(int)` cast of a pointer becomes the host form (`(IosMsgWord)` for
`iosMsgSend`, `ICO_WORD`, or no cast where the port's prototype already takes the pointer).
`gcc -fsyntax-only` over the game files with the flags from `compile_commands.json`, before and
after, finds them: each shows as a new `-Wpointer-to-int-cast`. The port's own changes (the host
build, `port/`, host paths in `ico2/`) never go up, and nothing here changes a byte the
decompilation builds. The decompilation's [docs/PORT.md](https://github.com/nathanialf/ico/blob/main/docs/PORT.md)
states the same contract from its side.

## Regions

The decompilation builds one ELF per disc from one tree: every difference between the European
disc (SCES-50760) and the other discs is an inline block on the targets' macros,
`#ifdef VERSION_JPN J #else P #endif`, with the European text in the `#else` arm (its
[docs/BUILDING.md](https://github.com/nathanialf/ico/blob/main/docs/BUILDING.md)). The Korean
disc (SCPS-56001) is a build of the Japanese program, so most of its blocks are shared arms,
`#if defined(VERSION_JPN) || defined(VERSION_KOR)`, with `#ifdef VERSION_KOR` nested inside one
where the Korean text differs (its
[docs/TARGETS.md](https://github.com/nathanialf/ico/blob/main/docs/TARGETS.md) "Korean retail"). The port
compiles one game core that plays every disc in `config/regions.txt`, so each block becomes a
run-time form on `ico_region`, the row of that table the identified disc selects (set once at disc
mount, before any game code runs). No game source holds a `VERSION_` block or a conditional on the
region; [CONVENTIONS.md](CONVENTIONS.md#region-forms) gives the forms.

`config/regions.txt` has one row per target in a fixed order: `slug upstream-macro disc-id
boot-elf product-name`, row 0 the European disc (`pal`, macro `-`), row 1 the Japanese disc (`jpn
VERSION_JPN SCPS-11003 SCPS_110.03`). `tools/region_blocks.py gen-header` writes
`ico2/common/include/region.h` from it (`gen_header`): `extern int ico_region`, `ICO_REGION_<SLUG>`
(the row index) and `ICO_REGION_COUNT`, `ICO_<SLUG>` (`ico_region == ICO_REGION_<SLUG>`), and for
every set of two or more non-PAL rows a predicate named by the set: a set's name is its rows'
slugs concatenated in table order, so `ICO_JPNKOR` is `(ICO_JPN || ICO_KOR)`. Then one selector
macro per way to group some of the non-PAL rows into arms, named `PAL_` and the arms' set names
joined by `_`, one argument per arm in table order after the PAL arm: for the rows pal, jpn, kor
these are `PAL_JPN(pal, jpn)`, `PAL_KOR(pal, kor)`, `PAL_JPNKOR(pal, jpnkor)` (one value for both
rows) and `PAL_JPN_KOR(pal, jpn, kor)`, whose test chain runs from the last arm down (three named
rows give 14 selectors; `selector_groupings`). With two rows the header holds `PAL_JPN` alone and is
byte for byte the two-row header. `LN(pal, jpn, kor)` is upstream's per-target `__LINE__` values
for the debug allocators and asserts, one per row (its arity is the row count; `check` holds
`region.h` to it), as the selector naming every row. A selector's first argument is the PAL arm; a
region it does not name takes the PAL arm, which is upstream's own rule, so a site never changes
when a target joins. `typedef.h` includes `region.h`, so every game source that sees `ICO_WORD`
sees the region.

The in-memory layout is the European one on every disc: `MotionDef`, `McProductFile`, `ActWork`,
`TexRec`, `MAX_CHARS` and every array take the PAL arm (the larger), and the Japanese ELF's tables
are converted into the PAL host structs when they load ([DATA.md](DATA.md)). A block that differs
only in layout (a struct, a raw offset, a bit position, a member name) therefore takes the PAL arm
for every region. Behaviour is per region: a statement only one disc runs runs only on that disc,
and an id that differs picks the region's value at run time.

The port's own code names game data by number in many places (a layout, a menu row, a stage, a
story flag). Those numbers differ between the discs, so it reads them from the selected region's
table, `ico_region_cur()` (`port/core/region.h`, an `IcoRegionInfo`; one file per row,
`port/core/region_<slug>.c`, each value resolved by name against that disc's own tables).

### The converter

`tools/region_blocks.py` reads bytes (the EUC-JP sources are never decoded) and parses the general
block shape: `#if C`, any number of `#elif C` arms, an optional `#else`, `#endif`, where a condition
`C` is a boolean expression over `defined(VERSION_X)` (or `defined VERSION_X`) with `||`, `&&`, `!`
and parentheses (`parse_condition`); `#ifdef VERSION_A` and `#ifndef VERSION_A` are the same as
`#if defined(VERSION_A)` and `#if !defined(VERSION_A)`. Upstream writes `#ifdef VERSION_JPN`,
`#if defined(VERSION_JPN) || defined(VERSION_KOR)`, `#if !defined(VERSION_JPN) &&
!defined(VERSION_KOR)` and `#elif defined(VERSION_A) || defined(VERSION_B)`. One region block may
sit in an arm of another (upstream's `#ifdef VERSION_KOR` inside a shared arm, whose `#else` is
then the Japanese text). A row no condition names takes the arm the PAL row takes. A macro that is
not a row (add the row first), a block nested two deep, a bare `VERSION_X` without `defined`, a
condition mixing in another macro, an `#elif` naming a row an earlier arm took, an `#elif` after
`#else`, or a directive naming a `VERSION_` macro any other way stops the tool with the line
(`find_blocks`).

The tool reads each block as per-row texts: for every row of the table, the lines that row's build
compiles. The arms are the distinct texts, each keyed by the set of rows that compile it
(`Arm.key`, a tuple of slugs in table order; the arm holding PAL's text is `pal`, with every row
that shares it). So `#if defined(VERSION_JPN) || defined(VERSION_KOR) J #else P #endif` has the
arms `jpnkor` and `pal`, and with the Korean text nested in it, `jpn`, `kor` and `pal`. A nested
block whose shared lines sit around the inner block is rebuilt flat in memory with those lines in
each arm (`flatten_blocks`), and goes back as written when no class converts it. Every class below
works on these arms: a selector names the sets whose value differs from PAL's, rows with one value
sharing an argument (`selector_call`), a statement form tests the set's predicate. `convert
[--root DIR] [PATH...]` rewrites each block by the first class that fits and keeps the PAL arm's
bytes and indentation:

| class | when | becomes |
| --- | --- | --- |
| `case-shift` | every top-level `case` label of a switch lies in a block, all with one delta `d` per row | the PAL labels, and the switch head once: `switch (e + PAL_JPN(0, d)) { /* JPN property ids d lower */`; per row, rows with one delta sharing it: `PAL_JPN_KOR(0, 30, 36)` with `/* JPN property ids 30 lower, KOR property ids 36 lower */`, `PAL_JPNKOR(0, 29)` with `/* JPNKOR ... */` (`class_a2_switches`, `switch_head_edit`) |
| `token` | the arms have the same tokens but for literals | each differing literal as `PAL_JPN(p, j)`, `PAL_JPNKOR(p, jk)` when the two rows agree on it, `PAL_JPN_KOR(p, j, k)` when they do not, suffixes kept (`class_a1_tokens`) |
| `initialised` | one arm declares a local with an initialiser, the other without | the initialised arm (`class_a1p_initialised`) |
| `span` | the arms differ in one balanced span | `PAL_JPN(span_pal, span_jpn)`, the span widened until it is a whole operand in its place (`class_a3p_span`, `span_ok`) |
| `if-else` | whole statements in two or more arms, no declaration or label at an arm's top level | `if (ICO_JPN) { J } else { P }` (a shared arm: `if (ICO_JPNKOR)`), named arms first in upstream's order and PAL last, so a later upstream edit lands line for line; three arms give `if (ICO_US) {...} else if (ICO_JPN) {...} else {...}` (`class_a3_statements`) |
| `one-region` | statements in one arm only | `if (ICO_JPN) { J }`, `if (ICO_JPNKOR) { JK }` or `if (!ICO_JPN) { P }`, `if (!ICO_JPNKOR) { P }` for the negated pair; an arm of declarations alone, whose initialisers call and assign nothing, is kept for every region (`class_a4_one_region`) |
| `file-scope` | one arm at file scope: includes, prototypes, externs, typedefs, definitions no other file-scope block defines | the arm, for every region (`class_a5a_file_scope`) |

The statement forms re-indent the arms one step and keep the blank lines at an arm's ends outside
the braces (`if_chain`). In a `token` block the PAL token stays alone, with a `layout` report line,
after `>>` or `<<` (a bit position: the loader moves the Japanese bits to PAL's), in an array
dimension, inside `sizeof`, after `+` or `-` in a statement with a `(char *)`, `(int *)`, `(float
*)` or `(short *)` cast (a raw offset), inside a struct, on a member name (the structs differ), and
on a delta of exactly 16 on a value of 0x200 and up (the Japanese `ActWork` has no `boxDir`)
(`deny_reason`, `offset16`, `is_member`). A `span` that reads a motion record's bit words keeps
PAL's span (`bit_word_span`). An automatic local's initialiser takes the selector; a static local's
cannot (it is a constant context) and is the `data-twin` hand item. A substituted numeric delta
outside the id families planning found (`DELTAS_KNOWN`: 2, 3, 4, 19, 23, 24, 28, 29, 30, 32, 48, and
the Korean property deltas 34, 35, 36 measured on upstream c2f4ed40e, where PAL - KOR is PAL - JPN
plus the six rows the Korean property table lacks) is a `review` line. A selector a converted line puts past column 100 is rewrapped by
`tools/format.sh` like any other line.

What no class fits stays a raw block and is a `hand` line of `report`, with a subkind naming its
pattern:

| subkind | the block | the hand form |
| --- | --- | --- |
| `layout-typedef` | a struct or typedef whose layouts differ, or a block inside one | the PAL arm |
| `define` | a `#define` per region | the PAL arm when it sizes something (`MAX_CHARS 1637`), else a selector at the uses |
| `data-twin` | file-scope data with different initialisers, a block inside a file-scope initialiser, a static local's initialiser, a name two file-scope blocks define | `name_pal[]` and `name_jpn[]` and `#define name PAL_JPN(name_pal, name_jpn)` (a scalar: `(*PAL_JPN(&a, &b))`); `check` refuses `sizeof(name)`, `&name` and a shift count |
| `extern-init` | a scalar defined with a different initialiser | the PAL initialiser, and `name = PAL_JPN(p, j);` first thing in its owner's init |
| `array-size` | an array dimensioned per region | the larger dimension |
| `function-twin` | a whole function per region with one name | the PAL body keeps the name, the other body becomes `static ... name_<set>(...)` (`name_jpn`, `name_jpnkor` for a shared arm, `name_kor`), the PAL function opens `if (ICO_JPN) { return name_jpn(args); }` (or `ICO_JPNKOR`), and the pair goes into `config/region_twins.txt`. The seven `_jpn` twins made before the Korean disc keep their names: each is the Japanese build's text, shared by the Korean disc after the rename below (`if (ICO_JPNKOR) { return name_jpn(args); }`) |
| `rename` | the arms name different functions or symbols | the PAL names stay, the other bodies get `_<set>` names, and the callers pick with a selector or a data twin |
| `decl` | a declaration in an arm with statements, declarators or prototypes that differ | hoist the declaration, gate the statements, or one `if` with `PAL_JPN` spans |
| `unbalanced` | arms that open or close a bracket, are not at a statement boundary, or hold a label | one `if` with `PAL_JPN` spans, or the enclosing statement per region |
| `large-variant` | an arm longer than 40 lines | the `if-else` form or a twin, by reading |
| `case-mixed` | a switch whose labels do not shift by one delta | `if`/`else` on the scrutinee, or per-region labels in two switches |
| `case-in-oneregion` | a `case` label in one region's arm only | the same |

`config/region_twins.txt` lists each hand-made twin as `path<TAB>function<TAB>twin`; a block that
upstream later puts inside either body is a `twin` line of `report` (mirror the change into the
other body), and `check` fails on a row whose path or functions do not exist.

`report [--root DIR]` prints `path:line<TAB>kind<TAB>subkind<TAB>detail` for every `hand`,
`layout`, `review` and `twin` line, `--all` adds one `auto` line per converted block (its class),
and `--bits` prints candidate rows for `config/motion_bits.<slug>.txt`: every differing numeral
after `>>` or `<<` on a line naming `modeBits`, `flags` or `flags2`, as `MotionModeBits jpn=16
pal=19 width=3 # act-env.c:696` (the line of the PAL arm; `bits_rows`), one row per row of a shared
arm (`jpn=` and `kor=` for a `jpnkor` arm). Run it before `convert`, which keeps only the PAL
numbers.

`check [--root DIR]` (pre-commit, CI) holds the tree to the rules: no `VERSION_` identifier under
`ico2/` or `sce/` in a `.c`, `.h`, `.inc`, `.s` or `.S` file, comments included, outside a raw
block's directives; no `#if`, `#ifdef`, `#ifndef` or `#elif` on `LN`, `ICO_<SLUG>`,
`ICO_REGION_*`, a selector macro or `ico_region`, and no `#define` of them outside `region.h`; no
selector or `LN` in a shift count (after `>>` or `<<`, also inside grouping parentheses:
`w >> (PAL_JPN(16, 19))`, `w >> (8 + PAL_JPN(8, 11))`), inside `sizeof`, in an array dimension, a
`case` label, a static local's initialiser or at file scope; the same for a data twin (a name the
file defines as `#define name PAL_JPN(...)` or `(*PAL_JPN(...))`), and no unary `&name` on the
plain form (`&name[i]` is fine); no function of `config/region_twins.txt` in a shift count, inside
`sizeof` or after a unary `&` (fixture: `tools/test/region_blocks/check_contexts.c`); every file naming the macros reaches `region.h`
through its includes, resolved in `ico_include_dirs` order (`CMakeLists.txt`), or is included only
by files that do; `region.h` equal to what `gen-header` writes; the twins rows; and `convert`
changing nothing; `LN` in `region.h` taking one argument per row; and the glue "Adding a build
target" names, each piece looked for once its mechanism is in the tree (`glue_problems`): for every
row `port/core/region_<slug>.c` defining `ico_region_info_<slug>`, a fresh
`port/core/region_table.c`, and an `[ICO_REGION_<SLUG>]` entry of `ico_discs[]` in
`port/data/discs.c` naming the slug; for every non-PAL row
`config/ee_layout.<slug>.txt` (once any `config/ee_layout.*.txt` exists) and
`port/data/gen/<slug>/table_desc.c`, with `ico_table_sets[]` in `port/data/gen/table_sets.c` in
the table's order (once that file exists). A missing piece names its recipe step. With no
`config/regions.txt` and no `region.h`, a tree that names none of the macros passes. Exit 0: every
rule holds and no block is left; exit 2: raw blocks remain and every one is a hand item (listed);
exit 1: a rule misfired or the tree needs `convert`, `gen-header` or `gen-table` (listed). CI
requires exit 0. The pre-commit hook (step 8) fails on exit 1 and on exit 2 only warns, printing
the count, so the hand list after a merge can be worked one commit at a time.

`gen-table` writes `port/core/region_table.c` from the table: an `extern` for each row's
`ico_region_info_<slug>` and `ico_region_info[ICO_REGION_COUNT]` in the table's order (`gen_table`);
`region.c` holds the look-ups over it.

`--selftest` converts `tools/test/region_blocks/fixture.c` in a scratch tree with that
directory's four-row `regions.txt` (pal, jpn, kor and a dummy us, so a fourth target's arity path is
tested) and `region_twins.txt`, compares the result, `report --all` and `report --bits` with
`expected.c`, `expected_report.tsv` and `expected_bits.txt` (the expected files are the
specification: each line is read by hand when they change), converts again (nothing may change),
runs `check` (exit 2), the `check_contexts.c` rules, `check`'s `LN` arity and `ico_discs[]`
problems, feeds the parser its malformed shapes and the forms it must read, checks `gen-header`'s
four-row selectors (14), the three-row header's shape and the two-row header against
`region_two_rows.h` byte for byte, `gen-table`'s rows, `rename` on `rename.c` against
`rename_expected.c` (idempotent, and a no-op for two rows), and runs `gcc -fsyntax-only` on the
result with none, `-DVERSION_JPN` and `-DVERSION_KOR`.

`rename [--root DIR] [--check] [--set SLUGS]` rewrites the two-row spelling for a table that gains
a row sharing the Japanese arm: under `ico2/` and `sce/` only (`region.h` is `gen-header`'s, the port
layer's `ICO_JPN` uses are decided one by one), `PAL_JPN(` becomes `PAL_JPNKOR(`, the identifier
`ICO_JPN` becomes `ICO_JPNKOR`, and `LN(a, b)` becomes `LN(a, b, b)`, balanced-parenthesis aware,
also inside a `#define`; comments and strings stay, and a second run changes nothing
(`rename_bytes`). The set is every named row of `config/regions.txt` unless `--set jpn,kor` names it:
the assumption is that each target after the Japanese one is a build of the Japanese program, as
the Korean one is (a row outside the set would take PAL's value in `LN`). `--check` prints per file
what would change and exits 1 if anything would. On main before the Korean merge it rewrites 98
files: 2543 `PAL_JPN(`, 168 `ICO_JPN` (one more sits in a comment) and 194 `LN(`.

### Merging upstream

The conversion runs on upstream's tree before the merge, so each conflict is the port's host line
against one converted line, and the next merge has an upstream commit as its base:

1. `git worktree add ../ico-upconv -b upstream-<rev>-converted upstream/main`.
2. From the port's tree, `python3 tools/region_blocks.py report --root ../ico-upconv --bits` for
   `config/motion_bits.<slug>.txt` and `report --root ../ico-upconv` for the hand list, then
   `convert --root ../ico-upconv` (a root without `config/regions.txt` takes the port's,
   `need_regions`), then `clang-format -i --style=file:<port>/.clang-format` from the port's venv
   over the files it changed, and commit there with `--no-verify` (upstream's tree has no
   `region.h`, and its hooks are upstream's).
3. Merge with an explicit base, so that a change of spelling on main (a target joining) is not a
   conflict at every site. On a branch from `main`: if the table gained a row sharing the Japanese
   arm, `tools/region_blocks.py rename` and `gen-header` first, committed alone (spelling only).
   Then B, the previous upstream base (the merge base of `main` and upstream/main) converted with
   the current table and renamed the same way, in its own worktree and committed with
   `--no-verify`; T, upstream/main converted (steps 1 and 2). For `ico2/` and `sce/`, a three-way
   merge with base B: `git read-tree -m -u B HEAD T` restricted to those paths (an index file of
   its own, `GIT_INDEX_FILE`, from trees narrowed to `ico2` and `sce`, or `git merge-file` per
   file), then `git merge-index -o git-merge-one-file -a`, so each conflict is upstream's change
   against a host edit. The other paths (tools, config, docs, `.github`) come from upstream/main
   directly, as a plain merge would take them: the port's tools are ours, upstream's
   `config/*.<slug>.*` theirs. The result is committed with `git commit-tree <tree> -p main -p
   upstream/main` (never with B or T as a parent), so the next merge has upstream/main as its
   base. In `ico2/`, a
   converted line against a host form keeps the port's form and carries the selector tokens into
   it (a `layout` PAL-only line against a port field access: the port's form outright); a
   converted `if`/`else` against a host form gets the seams in both arms; a raw hand block against
   port text: the PAL arm with the seams, the other arms dropped when they are a layout item, else
   kept raw with the seams in the PAL arm. Upstream's names, types and fixes win
   ([The decompilation](#the-decompilation)). Upstream's files that serve only its PS2 build
   (its build and ELF-check scripts, progress pages, per-target notes) stay deleted here; its
   [docs/TARGETS.md](https://github.com/nathanialf/ico/blob/main/docs/TARGETS.md) is the
   inventory of where each target's build differs from PAL's, the reading for a hand item. A
   merge that brings no new row needs no rename: B is then upstream's previous base converted, and
   the same three-way applies (or a plain `git merge --no-commit upstream-<rev>-converted`).
4. `python3 tools/region_blocks.py report` on the merged tree is the hand list; work it with the
   patterns above, adding each twin to `config/region_twins.txt`.
5. `check` exits 0 (the include walk also finds upstream files the port's tree must change, such as a
   game source naming `LN` without reaching `typedef.h`); then `strip_host_gates.py --check`, the generators' `--check`,
   `tools/format.sh`, and `gcc -fsyntax-only` over every game file (a name only a hand block's
   Japanese arm declares, or a selector in a constant context, shows as an error).

### Adding a build target

1. Add the row to `config/regions.txt` (slug, upstream macro, disc id, boot ELF, product name) and
   run `tools/region_blocks.py gen-header` and `gen-table`: `region.h` gains `ICO_REGION_<SLUG>`,
   `ICO_<SLUG>` and the selectors naming the new row, `port/core/region_table.c` the row's entry;
   nothing existing changes. From here `check` names each missing piece with its step.
2. `port/core/region_<slug>.c` with `const IcoRegionInfo ico_region_info_<slug>` (its numbers
   resolved by name against that disc's tables, as `region_jpn.c` documents per field), listed in
   `port/core/CMakeLists.txt` and declared in `port/core/region.h`; the disc row and manifest, the
   `baserom/<slug>/baseelf.elf` and `baserom/Ico_<SLUG>.iso` links and their cache variables.
   The host takes the row in these pieces: the `ico_discs[]` row in `port/data/discs.c` (disc id,
   boot ELF and its SHA-1s, DATA.DF manifest, archive name, and the image base `Ico_<SLUG>` from
   which `main_host.c` builds the image names it looks for beside the program, `.iso`, `.chd`,
   `.bin`, `.cue`), `port/core/region_<slug>.c` above, and the Settings Disc row's value strings
   (`port/ui/settings.c`, whose `_Static_assert(ICO_REGION_COUNT == 2 ...)` fails the build until
   they are added); the disc choice (`ico_host_choose_disc`, `find_beside` with
   `ico_host_choose_image`) takes any number of rows.
3. Data: the target's EE layout file `config/ee_layout.<slug>.txt`, symbol tables, table manifest,
   descriptor set `port/data/gen/<slug>/` and its entry in `ico_table_sets[]`, defaults for the
   fields it lacks, and `config/motion_bits.<slug>.txt` from `report --bits` when its bit words
   differ ([DATA.md](DATA.md)).
4. Game code: the merge above. Upstream's `#elif defined(VERSION_<SLUG>)` arms convert to the new
   selectors and to `else if (ICO_<SLUG>)` chains, arms shared with another row to the set's
   selectors and predicate (`PAL_JPNKOR`, `ICO_JPNKOR`); a row that shares the Japanese arm first
   takes `rename` on main (step 3). The hand list takes the same patterns, twins with `_<set>`
   names.
5. The per-region tests take the slug as a parameter and gain one `add_test` line each; a strings
   table only when the target brings a menu language.
6. One integration build, then a check on that disc.

## EUC-JP sources

41 game sources carry Japanese text in EUC-JP, and their string literals are the ROM's bytes.
`.gitattributes` lists each with `working-tree-encoding=EUC-JP`: git stores UTF-8 and checks the
file out as EUC-JP, which is what the compiler reads. An editor or a pipe that rewrites one of them
as UTF-8 changes its bytes; change them with an ASCII patch and `git apply`. `.clang-format` never
splits a string literal (`BreakStringLiterals: false`) or reflows inside `assert`
(`WhitespaceSensitiveMacros`), the tools that read these files work on bytes or latin-1
(`strip_host_gates.py`, `check_call_types.py`), and a Mac checkout needs `EUC-JP-MS`
([BUILDING.md](../BUILDING.md#checking-out-on-a-mac)).
