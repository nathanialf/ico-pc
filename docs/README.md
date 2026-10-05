# docs/

An index of this directory: what each file is for.

| file | what it is |
| --- | --- |
| [`BUILDING.md`](BUILDING.md) | the host build: quickstart, presets and options, the hooks, CI, packages, and the maintainers' appendix on the optional EE identity check |
| [`LEGAL.md`](LEGAL.md) | what the repository may and may not contain, why the data tables are loaded from the user's disc at run time and never committed, and which references were used. Read it before contributing |
| [`HEADERS.md`](HEADERS.md) | which headers the disc attests, the code includes, where the other declarations live, and the derived-name tokens |

## Port notes

`docs/port/` holds the port's own notes; other files may be added.

**Overview and process**

| file | what it is |
| --- | --- |
| [`port/DIVERGENCES.md`](port/DIVERGENCES.md) | divergences from the PS2 original |
| [`port/BUILD_STATUS.md`](port/BUILD_STATUS.md) | what the host build compiles per preset, and what links |
| [`port/TESTING.md`](port/TESTING.md) | test checkpoints, `ico-pc.ini` / `config.toml` keys, `tools/package_win.sh`, `tools/package_linux.sh`, the D3D12 checks, CI |
| [`port/THIRD_PARTY.md`](port/THIRD_PARTY.md) | every third-party dependency: version, licence, provenance, use |
| [`port/STEAMDECK.md`](port/STEAMDECK.md) | the Linux package and running it on a Steam Deck: where the archive, config and saves live |

**Platform, data and runtime libraries**

| file | what it is |
| --- | --- |
| [`port/PLATFORM.md`](port/PLATFORM.md) | the platform layer: fibers for the kernel threads, simulated vsync, the heap arena |
| [`port/DATA.md`](port/DATA.md) | the disc layer: VFS, ISO9660 and archive backends, the first-run extractor, libcdvd and SIF hosts, the runtime table loader |
| [`port/LOADERS.md`](port/LOADERS.md) | disc-format loaders on a 64-bit host: the pointer slots, patterns and what each loader needs |
| [`port/LAYOUT.md`](port/LAYOUT.md) | struct layouts on the host and the EE, and the layout asserts |
| [`port/MATH.md`](port/MATH.md) | VU0 maths on the host (`port/math/`) |
| [`port/HW_ADDRESS_SITES.md`](port/HW_ADDRESS_SITES.md) | sites in `ico2/` that touch EE hardware or fixed link addresses |
| [`port/CONFIG.md`](port/CONFIG.md) | `config.toml`, the ini, language and the port clock |
| [`port/INPUT.md`](port/INPUT.md) | the input layer: SDL3 gamepads, keyboard and mouse to a virtual DualShock 2; the pad script |
| [`port/SAVES.md`](port/SAVES.md) | the memory card over host files |
| [`port/AUDIO.md`](port/AUDIO.md) | the software SPU2, the SNDN2DRV host, the SDL3 output and the volume |
| [`port/FMV.md`](port/FMV.md) | video playback: the PSS demuxer, libmpeg2, audio and pacing |
| [`port/HEADLESS_STUBS.md`](port/HEADLESS_STUBS.md) | what the headless build stubs (libgraph and libdma, libgcc's `fptodp`) and why the renderer layer is not stubbed |

**Renderer and display**

| file | what it is |
| --- | --- |
| [`port/RENDER_API.md`](port/RENDER_API.md) | the render API (`rd`) design: the GS rules, passes and entry points |
| [`port/SHADERS.md`](port/SHADERS.md) | the HLSL shaders, compiled to SPIR-V and DXIL at build time |
| [`port/VU1_PROGRAMS.md`](port/VU1_PROGRAMS.md) | what the five VU1 microprograms compute |
| [`port/DISPLAY.md`](port/DISPLAY.md) | the display options: Original and Enhanced presets, resolution, aspect, filtering, frame rate |

**Features**

| file | what it is |
| --- | --- |
| [`port/OPTIONS.md`](port/OPTIONS.md) | the gameplay options (stick fix, shadows never take Yorda, mirror, developer mode) |
| [`port/SETTINGS.md`](port/SETTINGS.md) | the Settings menu: pages, boot screens, controller remap |
| [`port/UI.md`](port/UI.md) | runtime text, the layout extension, popups and the font |
| [`port/ACHIEVEMENTS.md`](port/ACHIEVEMENTS.md) | the built-in achievements over the typed game-state interface |
| [`port/DEVELOPER_MODE.md`](port/DEVELOPER_MODE.md) | developer mode: the debug menu, `host0:` files, snapshots |

**Bring-up and sweep reports**

| file | what it is |
| --- | --- |
| [`port/BOOT_DIAG.md`](port/BOOT_DIAG.md) | Phase 1 boot failures, their causes and fixes |
| [`port/SWEEP_0B.md`](port/SWEEP_0B.md) | package 0B: the front-end check over `ico2/` |
| [`port/SWEEP_0E.md`](port/SWEEP_0E.md) | package 0E: nested functions and `break` sites |
| [`port/SWEEP_2D.md`](port/SWEEP_2D.md) | package 2D: the 64-bit sweep of `fumi/src` and `fumi/isys` |
| [`port/SWEEP_2E.md`](port/SWEEP_2E.md) | package 2E: the 64-bit sweep of `sugipon`, `ito/src` and `fumi/sound` |
| [`port/SWEEP_2F.md`](port/SWEEP_2F.md) | package 2F: the 64-bit sweep of `omori`, `script` and `common/src` |
| [`port/SWEEP_2I.md`](port/SWEEP_2I.md) | package 2I: the x64 bring-up to Phase 2's exit criterion |
| [`port/SWEEP_2J.md`](port/SWEEP_2J.md) | package 2J: the 32-bit presets retired, and the tree-wide raw-offset pass |

`docs/research/` holds research notes done for the port; other files may be added.

| file | what it is |
| --- | --- |
| [`research/sndn2drv.md`](research/sndn2drv.md) | R1: SNDN2DRV.IRX packet semantics |
| [`research/float-semantics.md`](research/float-semantics.md) | R2: EE FPU and VU0 float semantics |
| [`research/compiler-semantics.md`](research/compiler-semantics.md) | R3: ee-gcc 2.9 vs modern compilers, semantic audit |
| [`research/loader-census.md`](research/loader-census.md) | R4: loader census |
| [`research/licences.md`](research/licences.md) | R5: dependency licences and provenance |
| [`research/retroachievements.md`](research/retroachievements.md) | R6: RetroAchievements feasibility |

The decompilation's note on how it relates to the port is
[`docs/PORT.md`](https://github.com/nathanialf/ico/blob/main/docs/PORT.md).

`tools/README.md` describes every tool.
