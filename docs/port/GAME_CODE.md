# The game code

How the game's own C under `ico2/` is compiled for the host: which files, with which options and
warnings, how a host path is written inside the game sources, the record-layout conventions of a
64-bit build, the audits that guard them, and how the tree relates to the decompilation it came
from.

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

One program plays every disc listed in `config/regions.txt`, one row per game region in a fixed
order: the slug, the decompilation's macro for it, the disc id, the boot ELF and the product name.
Row 0 is the European disc (`pal`, SCES-50760), the base; row 1 is the Japanese disc (`jpn`,
`VERSION_JPN`, SCPS-11003). The region is chosen once, when the disc image is identified, before
any game code runs, and the game reads it as `ico_region`, the row's index (0 until a region is
selected, so a test that never selects one plays the European disc's code).

The decompilation builds each region's ELF from one tree, with the differences in inline
`#ifdef VERSION_<SLUG>` blocks. Here they are run-time forms, of three kinds, all from
`ico2/common/include/region.h` (which `typedef.h` includes):

| form | meaning |
| --- | --- |
| `ICO_PAL`, `ICO_JPN` | true on that region (`ico_region == ICO_REGION_<SLUG>`), for a statement that runs on one region: `if (ICO_JPN) { ... }` |
| `PAL_JPN(pal, jpn)` | a selector: the value of the region being played. The first argument is always the European one (the decompilation's `#else` arm), then one per region the macro names, in the table's order; a region a selector does not name takes the European value |
| `LN(pal, jpn)` | the decompilation's per-region `__LINE__` numbers for the debug allocators and asserts, the same selector |

They are expressions, so none of them can size an array, label a `case`, or follow `#if`. One
in-memory layout serves every region: the European one, which is the larger.

`region.h` is generated from `config/regions.txt` by `tools/region_blocks.py gen-header` (the
conversion tool, which also turns upstream's blocks into these forms at each merge); until that
tool lands the file is written by hand in the shape it generates. Do not edit it by hand once the
tool is in the tree.

The port's own code names game data by number in many places (a layout, a menu row, a stage, a
story flag). Those numbers differ between the discs, so it reads them from the selected region's
table, `ico_region_cur()` (`port/core/region.h`, an `IcoRegionInfo`; one file per row,
`port/core/region_<slug>.c`, each value resolved by name against that disc's own tables).

### Adding a build target

Add the row to `config/regions.txt` and regenerate `region.h` (new `ICO_<SLUG>`,
`ICO_REGION_<SLUG>` and the selector macros that name it; nothing existing changes), then add
`port/core/region_<slug>.c` with the target's `IcoRegionInfo` and list it in `region.c`'s
`ico_region_info[]` and `port/core/CMakeLists.txt`. `region_select` checks the table against
`config/regions.txt` row for row. The data tables, the disc row and the game code's conversion
follow; their steps are written here as each of them lands.

## EUC-JP sources

41 game sources carry Japanese text in EUC-JP, and their string literals are the ROM's bytes.
`.gitattributes` lists each with `working-tree-encoding=EUC-JP`: git stores UTF-8 and checks the
file out as EUC-JP, which is what the compiler reads. An editor or a pipe that rewrites one of them
as UTF-8 changes its bytes; change them with an ASCII patch and `git apply`. `.clang-format` never
splits a string literal (`BreakStringLiterals: false`) or reflows inside `assert`
(`WhitespaceSensitiveMacros`), the tools that read these files work on bytes or latin-1
(`strip_host_gates.py`, `check_call_types.py`), and a Mac checkout needs `EUC-JP-MS`
([BUILDING.md](../BUILDING.md#checking-out-on-a-mac)).
