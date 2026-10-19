# Conventions

The rules the tree's own code follows: what the pre-commit hook checks, how C is formatted, how
`port/` names its symbols and its log lines, and how tests and scripts start. The game sources
under `ico2/` keep their own names and layout; [GAME_CODE.md](GAME_CODE.md) covers how they are
changed.

## Files

| path | what it holds |
| --- | --- |
| `tools/install_hooks.sh` | writes the pre-commit hook into the repository's shared hook directory (run by `tools/setup.sh`) |
| `tools/format.sh` | clang-format over the C the script owns, then the top-level layout pass |
| `tools/format_layout.py` | the top-level blank-line layout of the game's C |
| `.clang-format` | the clang-format style |
| `tools/rename_port_symbols.py`, `tools/rename_table.txt` | the check that lists any `port/` name not in `prefix_snake_case`, and the rename table it writes and applies |
| `port/test/ico_check.h` | `CHECK`, `CHECK_STR` and `ico_check_summary` for a port C test |
| `port/ui/test/settings_fixture.h` | the shared fixture and `CHECK` of the Options-screen tests |
| `cmake/IcoTests.cmake` | the `add_test` wrapper that labels every test and the `tests` target |

## The pre-commit hook

`tools/install_hooks.sh` installs `.git/hooks/pre-commit` (in a linked worktree, the main
repository's hook directory, `git rev-parse --git-common-dir`). The hook stops at the first check
that fails, in this order:

1. `tools/check_no_rom.sh`: the IP-safety scan (disc images, executables, extracted assets, large
   binaries, integer tables through `tools/check_int_arrays.py`; [LEGAL.md](../LEGAL.md)). With
   something staged it reads the staged blobs (`git show :path`), not the worktree.
2. `tools/format.sh --check --staged`: the staged C is formatted.
3. The three generated-file checks: `tools/gen_data_desc.py --check` (`port/data/gen/`),
   `tools/gen_layout_asserts.py --check` (`port/test/layout_asserts.c`), `tools/gen_sources.py
   --check` (`cmake/IcoSources.cmake`). They run under `.venv/bin/python` when it exists
   (`gen_data_desc.py` needs pyelftools); rerun the script without `--check` and stage its output.
4. `tools/strip_host_gates.py --check`: no `ICO_HOST` conditional in `ico2/` or `sce/`.
5. `tools/stack_overread_audit.py`: no reliance on the PS2's stack layout in `ico2/`.
6. `tools/check_call_types.py`: calls and declarations agree with the definitions.
7. `tools/struct_cast_audit.py`: no buffer cast to a struct wider on the host.
8. `tools/region_blocks.py --selftest`, then `tools/region_blocks.py check`: the converter still
   converts its fixture, no region conditional is in `ico2/` or `sce/`, and `region.h` and the
   region glue are fresh ([Region forms](#region-forms)). Raw `VERSION_` blocks that are all hand
   items (`check`'s exit 2) only print a warning with their count, so a merge's hand list can be
   worked commit by commit; CI requires exit 0.

Steps 4 to 8 are described in [GAME_CODE.md](GAME_CODE.md#audits). CI runs the same checks
([BUILDING.md](../BUILDING.md#continuous-integration)). Rerun `tools/install_hooks.sh` after its
hook text changes.

## Formatting

`tools/format.sh` formats the C it owns with the venv's `clang-format` (from
`tools/requirements.txt`; the script fails without it) and the tracked `.clang-format`: LLVM style,
4-space indent, 100 columns, Linux braces, `PointerAlignment: Right`, no reflowed comments, no
sorted includes, one blank line at most. Two settings keep the game's bytes: `BreakStringLiterals:
false` (literals reproduce the ROM's bytes, some in EUC-JP) and `WhitespaceSensitiveMacros:
[assert]` (an assert's text is stringified into its message).

The files it owns are the game's (`ico2/*/*/*.c`, `ico2/*/*/*.c.inc`, `sce/**/*.c`) and `port/`'s
`.c` and `.h`, except `port/third_party` and the generated shader headers under
`port/rhi/test/shaders` (`owned` in the script). The game's files also go through
`tools/format_layout.py`, which settles which top-level items sit together: `#include` lines,
declarations and preprocessor lines each form one block without blank lines, every other
top-level item is separated by exactly one blank line, and a comment directly above an item moves
with it; nothing inside a function body is touched.

```sh
tools/format.sh                       # rewrite every owned file
tools/format.sh FILE...               # rewrite these files
tools/format.sh --check [FILE...]     # exit 1 if a file would change (CI)
tools/format.sh --check --staged      # judge the staged blobs (the hook)
```

A `FILE` that does not exist (in the index, with `--staged`) is an error.

## Comments

C uses `/* */` comments only. A comment says what the code does and why, in the present tense: no
release numbers, package codes or history; git history keeps those. A file starts with a comment
that names its path or file name and says what it holds (`port/platform/arena.h`: "The simulated
EE main RAM the game's heap lives in."). A comment that names a behaviour
of another file names that file and the symbol. Host paths inside `ico2/` start their comment with
`PC port` ([GAME_CODE.md](GAME_CODE.md#host-paths-in-the-game-sources)).

## Names

`port/` names its functions and file-scope symbols `prefix_snake_case`: a lowercase module prefix,
one underscore (two for a module's internal names, `rd__log`), then lowercase words. Types and
struct tags are CamelCase (`IcoVfsEntry`, `RdFrame`); macros and enum constants are upper case
(`ICO_VFS_SECTOR`, `RD_*`).

| prefix | module |
| --- | --- |
| `ico_` | `port/platform`, `port/game`, `port/data`, `port/input`, `port/audio`, `port/config`, `port/save`, `port/math` |
| `rd_`, `rd__` | `port/render` |
| `rdtex_` | the texture cache (`port/render/rd_tex.c`) |
| `vu1ref_` | the CPU reference of the [VU1](ARCHITECTURE.md#terms) programs (`port/render/vu1_ref/`) |
| `texpack_`, `modelpack_`, `gltf_` | texture packs, model packs and the glTF loader (`port/render`) |
| `rhi_` | `port/rhi`, the backend-independent layer |
| `vkr_` | the Vulkan backend (`port/rhi/vk/`) |
| `dx_`, `d3dp_` | the Direct3D 12 backend and its header-free plan (`port/rhi/d3d12/`) |
| `ui_`, `ui__`, `lt_ext_`, `gallery_` | `port/ui`: the menus, the layout extension, the gallery |
| `pace_` | the frame pacing policy (`port/platform/pace_policy.c`) |

Names the game defines keep their spelling, including the host hooks the game files define for
`port/` (`gif_Host*`, `pac_Host*`, `mc_Host*`, `tex_HostTextureId`), and so do generated files and
the newlib constants under `port/math/newlib`. `tools/rename_port_symbols.py --check
tools/rename_table.txt` lists any `prefix_CamelCase` name `port/` declares; `--table` and `--apply`
build and apply a rename table (the tool's docstring gives the conversion rule and the collision
checks). Run `tools/format.sh` after an apply, since longer names rewrap lines.

## Region forms

The game sources choose by the disc at run time, never at compile time: no `#if`, `#ifdef` or
`#ifndef` on the region, its macros or `ico_region` (`tools/region_blocks.py check`;
[GAME_CODE.md](GAME_CODE.md#regions)). The forms, from `ico2/common/include/region.h`:

- A value: `PAL_JPN(pal, jpn)`, the European disc's value first, then one per named region in
  `config/regions.txt` order (`PAL_JPN_US(pal, jpn, us)` with a third row); a region the selector
  does not name takes the first. `framePlayed / PAL_JPN(2997.0f, 50.0f)`, `strcpy(file,
  PAL_JPN("SCES_507.60", "SCPS_110.03"))`. Never in a constant context: not after `>>` or `<<`,
  not in `sizeof`, an array dimension, a `case` label, a static initialiser or at file scope.
- Statements for two arms: `if (ICO_JPN) { ... } else { ... }`, the other region first and the
  European arm last; statements for the European disc only: `if (!ICO_JPN) { ... }` (`ICO_PAL`
  is the same test while the table has two rows; `!ICO_JPN` stays right when a row joins, since
  upstream's `#ifndef VERSION_JPN` arm is every other region's).
- `case` labels that shift by one delta: the European labels, and the switch head once,
  `switch (lt_current_property_item() + PAL_JPN(0, 30)) { /* JPN property ids 30 lower */`.
- A twin: the European body keeps the name, the other region's body is `name_jpn`, data twins
  are `name_pal`/`name_jpn` behind `#define name PAL_JPN(name_pal, name_jpn)`; `_pal` only under
  such a name-overlaying macro. Each function twin is a row of `config/region_twins.txt`.
- `LN(pal, jpn)`: upstream's per-disc `__LINE__` for the debug allocators and asserts.
- A layout difference is no form at all: the in-memory layout is the European one on every disc,
  so a struct, offset, bit position or member name takes the European arm.

## Log lines

The log is stderr: the file `logs/ico-pc.log` unless the program runs with `--console`. Android
mirrors it to logcat under the tag `ico-pc` (`port/platform/android/host_android.h`
(`ICO_ANDROID_LOG_TAG`)). A port log line starts with its module's prefix and a
colon; a test's own lines start with the test's name.

| prefix | written by |
| --- | --- |
| `ico_pc:` | the host program: `main_host.c`, `host_config.c`, `host_loop.c`, `diag_host.c`, `kernel_host.c`, `android/host_android.c`, `android/iso_import.c` |
| `window:` | `port/platform/window_host.c`, `window_video.c` |
| `video:` | `port/game/video_options.c` |
| `boot:` | `port/platform/window_host.c` (the first presents) |
| `fiber:`, `sched:`, `arena:`, `heap:`, `trace:` | `port/platform/fiber.c`, `sched.c`, `arena.c`, `trace_host.c` |
| `gpu driver:` | `port/platform/android/gpu_driver_android.c` |
| `rd:` | `port/render` through `rd__log` and `rd__log_once` (`rd_core.c`), with a topic after it (`rd: blend:`, `rd: fog:`) |
| `textures:`, `models:` | `port/render/texpack*.c`, `rd_tex.c`, `modelpack.c` |
| `dump:` | `port/render/rd_dump.c` |
| `rhi:`, `rhi_vk:`, `rhi_d3d12:` | `port/rhi/rhi_backend.c`, the Vulkan backend (`VKR_LOG`), the D3D12 backend (`DX_LOG`) |
| `vfs:`, `iso9660:`, `archive:`, `cdvd:`, `tables:`, `df_pack:` | `port/data` |
| `audio:`, `sndn2:` | `port/audio/audio_host.c`, `out_sdl.c`, `mix_gain.c`, `sndn2_host.c` |
| `fmv:` | `port/fmv/movie.c` |
| `input:` | `port/input/input_sdl.c`, `bindings.c`, `input_record.c` |
| `config:`, `scf:` | `port/config/config.c`, `sysconf.c` |
| `mc:` | `port/save/mc_host.c` |
| `photo:`, `credits:`, `appearance:`, `achievements:`, `options:`, `model_viewer:` | `port/game` |
| `ui:`, `settings:`, `gallery:`, `extras:` | `port/ui` |

The player guides quote some of these lines (`textures:` and `models:` in
[TEXTURE_PACKS.md](../TEXTURE_PACKS.md) and [MODEL_PACKS.md](../MODEL_PACKS.md)); change a quoted
line and its guide together.

A message box is titled `ICO PC` on Windows (`port/platform/host_config.c` (`ico_host_message_box`))
and Android (`port/platform/android/host_android.c`) and `ICO` on iOS (the same function and
`port/platform/ios/main_ios.c`); elsewhere `ico_host_message_box` writes the text to stderr.

## Tests

A test is a C program under `port/<area>/test/` (or a script under `tools/`) registered with
`add_test` in its directory's `CMakeLists.txt`, with a comment above the registration saying what it
checks. `cmake/IcoTests.cmake` wraps `add_test`: the test gets its area's label by directory
(`render`, `ui`, `audio`, `game`, `data`, `rhi`, `save` for `port/<area>`; `port/input`
(`port/config` and the top level are `platform` with `port/platform`); `port/math` (`port/null` and
`port/test` are `game`); `port/fmv` is `data`; `port/shaders` is `rhi`), a test whose command runs a
script from `tools/` also gets `tools`, and its program joins the `tests` target, so a new test
needs no extra line (`cmake/IcoTests.cmake` (`_ico_test_area`)). There is no `input` label:
`ctest -L platform` runs the input tests. [BUILDING.md](../BUILDING.md#tests) says how
to run them.

- The program's first comment names the file and lists what it checks, one line or item per case
  (`port/audio/test/adpcm_test.c`, `port/ui/test/ui_test.c`).
- A new test uses `port/test/ico_check.h` (`CHECK(cond)`, `CHECK(cond, fmt, ...)`, `CHECK_STR`,
  and `ico_check_summary(name)`, which prints `name: ok` or the failure count and returns the exit
  code), as `port/input/test/mouse_camera_test.c` does, instead of defining its own. The Options
  tests (`settings_test.c` and its siblings) share `port/ui/test/settings_fixture.h`, which has its
  own `CHECK` over the game's headers.
- A test that needs something the machine may lack (the disc image, a Vulkan device, Pillow, saved
  frames) exits 77 without it, and the registration sets `SKIP_RETURN_CODE 77`.
- A program whose cases take minutes is split into shards with `port/test/ico_shard.h` and
  registered with `ico_add_sharded_test` (`cmake/IcoTests.cmake`).

## Scripts

- A shell script that runs on its own starts with `#!/usr/bin/env bash`, a header comment with its
  usage lines, and `set -euo pipefail`. A file that is sourced (`tools/fetch_common.sh`,
  `tools/package_*_lib.sh`) starts with `# shellcheck shell=bash`, says it is sourced, and sets no
  shell options.
-  A Python tool starts with a docstring that says what it does and how it is called, and runs as
  `python3 -I tools/<name>.py`. A new tool parses its arguments with `argparse` (`--help` prints the
  usage); `check_call_types.py`, `check_int_arrays.py`, `check_stack_guard.py`, `format_layout.py`,
  `ico_version.py`, `stack_overread_audit.py` and `struct_cast_audit.py` read `sys.argv` directly,
  and `extract_elf.py` and `font_audit.py` take no arguments. A generator has `--check` (exit 1 when
  its output is stale); an audit with fixtures has `--selftest`; a test-time tool exits 77 when what
  it needs is missing.
- A tool that needs a third-party Python package takes it from `tools/requirements.txt` through the
  venv (`.venv/bin/python`).

## Where a change goes

| change | where to start |
| --- | --- |
| a new config key | [CONFIG.md](CONFIG.md): the reader, the writer and the `ico-pc.ini` mapping |
| a new Options row | [UI.md](UI.md): the Settings menu's pages and strings |
| a new gameplay option or achievement | [GAME_FEATURES.md](GAME_FEATURES.md) |
| a new shader | [SHADERS.md](SHADERS.md) |
| a new texture or model pack format | [PACKS.md](PACKS.md) |
| a pacing change | [DISPLAY.md](DISPLAY.md#pacing); the decisions go in `port/platform/pace_policy.c` and only the timing and I/O in `window_host.c` ([PLATFORM.md](PLATFORM.md#pacing)) |
| a host path in `ico2/` | [GAME_CODE.md](GAME_CODE.md#host-paths-in-the-game-sources) |
| a new test | [Tests](#tests) above; [TESTING.md](TESTING.md) lists the programs by area |

## Documents

`docs/port/` states what the tree at HEAD does, cites each behaviour in the form
[README.md](README.md#how-these-documents-are-written) gives, and links each term to
[Terms](ARCHITECTURE.md#terms). `python3 -I tools/check_docs.py` (ctest `docs_check`;
not in the pre-commit hook) checks links, anchors, history words, package codes and issue numbers
(`CMakeLists.txt` (`docs_check`); `tools/check_docs.py` (docstring)).
