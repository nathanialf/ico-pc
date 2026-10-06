# docs/

An index of this directory: what each file is for. `tools/README.md`
describes every tool.

| file | what it is |
| --- | --- |
| [`BUILDING.md`](BUILDING.md) | the host build: quickstart, toolchains, presets and options, compilers, the rules for shared code, hooks, tests, CI, packages, and the maintainers' EE identity check |
| [`LEGAL.md`](LEGAL.md) | what the repository may and may not contain, why the data tables are loaded from the player's disc at run time and never committed, and which references were used. Read it before contributing |
| [`HEADERS.md`](HEADERS.md) | which headers the disc attests, the code includes, where the other declarations live, and the derived-name tokens |
| [`PORT.md`](PORT.md) | how the port relates to the decompilation: `ico2/` and `sce/` are its code, what goes upstream, what stays here, how to merge |
| [`TODO.md`](TODO.md) | work wanted but not done, and facts not yet verified, with where each would go |

## Port notes (`port/`)

**Overview**

| file | what it is |
| --- | --- |
| [`port/DIVERGENCES.md`](port/DIVERGENCES.md) | every known way the port differs from the PS2 original, by default and behind options |
| [`port/TESTING.md`](port/TESTING.md) | how to test: the two builds, the test settings, replaying a session, the log and the trace, reporting a visual bug, the renderer backend checks, the packages |
| [`port/CONFIG.md`](port/CONFIG.md) | `config.toml` and `ico-pc.ini`: every key, precedence, language and the clock |
| [`port/THIRD_PARTY.md`](port/THIRD_PARTY.md) | every third-party dependency: version, licence, provenance, use |
| [`port/STEAMDECK.md`](port/STEAMDECK.md) | the Linux package and running it on a Steam Deck: where the archive, config and saves live |

**Platform, data and the 64-bit host**

| file | what it is |
| --- | --- |
| [`port/PLATFORM.md`](port/PLATFORM.md) | the platform layer: fibers for the kernel threads, simulated vsync, the heap arena and partitions, the FP mode |
| [`port/DATA.md`](port/DATA.md) | the disc layer: VFS, ISO9660 and archive backends, the first-run extractor, libcdvd and SIF hosts, the runtime table loader |
| [`port/LOADERS.md`](port/LOADERS.md) | disc-format loaders on a 64-bit host: the pointer slots, patterns and what each loader needs |
| [`port/LAYOUT.md`](port/LAYOUT.md) | struct layouts on the host and the EE, and the layout asserts |
| [`port/OFFSET_AUDIT.md`](port/OFFSET_AUDIT.md) | running the EE's code on a 64-bit host: the conventions in `ico2/`, the offset and template audits, every site where the host spelling differs |
| [`port/MATH.md`](port/MATH.md) | VU0 maths and the PS2 float helpers on the host (`port/math/`) |
| [`port/HW_ADDRESS_SITES.md`](port/HW_ADDRESS_SITES.md) | sites in `ico2/` that touch EE hardware or fixed link addresses |
| [`port/HEADLESS_STUBS.md`](port/HEADLESS_STUBS.md) | the headless build: what it stubs (libgraph and libdma, libgcc's `fptodp`) and why the renderer layer is not stubbed |
| [`port/BOOT_DIAG.md`](port/BOOT_DIAG.md) | diagnostics: what the log contains, crash reports and the watchdog, turning an offset into a function, the heap checker (`ICO_HEAP_ASAN`) and the `fptrap` preset |

**Devices**

| file | what it is |
| --- | --- |
| [`port/INPUT.md`](port/INPUT.md) | the input layer: SDL3 gamepads, keyboard and mouse to a virtual DualShock 2; the pad script and the pad recording |
| [`port/SAVES.md`](port/SAVES.md) | the memory card over host files |
| [`port/AUDIO.md`](port/AUDIO.md) | the software SPU2, the SNDN2DRV host, the SDL3 output and the volume |
| [`port/FMV.md`](port/FMV.md) | film playback: the PSS demuxer, libmpeg2, audio and pacing |

**Renderer and display**

| file | what it is |
| --- | --- |
| [`port/RENDER_API.md`](port/RENDER_API.md) | the renderer's design: the 13 lists, the backends, the GS rules, the passes, presets, interpolation, mirror, frame dumps |
| [`port/SHADERS.md`](port/SHADERS.md) | the HLSL shaders, compiled to SPIR-V and DXIL at build time |
| [`port/VU1_PROGRAMS.md`](port/VU1_PROGRAMS.md) | what the five VU1 microprograms compute |
| [`port/DISPLAY.md`](port/DISPLAY.md) | the display options: Original and Enhanced presets, resolution, aspect, filtering, frame rate |

**Features**

| file | what it is |
| --- | --- |
| [`port/OPTIONS.md`](port/OPTIONS.md) | the gameplay options (stick fix, shadows never take Yorda, mirror, developer mode) |
| [`port/SETTINGS.md`](port/SETTINGS.md) | the Settings menu: pages, boot screens, controller remap |
| [`port/UI.md`](port/UI.md) | runtime text, the layout extension, the title's layout, popups and the font |
| [`port/ACHIEVEMENTS.md`](port/ACHIEVEMENTS.md) | the built-in achievements over the typed game-state interface, and `achievements.toml` |
| [`port/DEVELOPER_MODE.md`](port/DEVELOPER_MODE.md) | developer mode: the debug menu, `host0:` files, snapshots |

## Research notes (`research/`)

Notes written before the parts of the port they informed were built. They
record the reasoning behind design decisions and are not updated as the code
changes.

| file | what it is |
| --- | --- |
| [`research/sndn2drv.md`](research/sndn2drv.md) | SNDN2DRV.IRX packet semantics |
| [`research/float-semantics.md`](research/float-semantics.md) | EE FPU and VU0 float semantics |
| [`research/compiler-semantics.md`](research/compiler-semantics.md) | ee-gcc 2.9 against modern compilers: a semantic audit |
| [`research/loader-census.md`](research/loader-census.md) | a census of the disc-format loaders |
| [`research/licences.md`](research/licences.md) | dependency licences and provenance |
| [`research/retroachievements.md`](research/retroachievements.md) | RetroAchievements feasibility |
