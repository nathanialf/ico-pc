# Developer documents

How the PC port works, for contributors: what stands in for each part of the PS2, where that code
lives and how it is tested. Start with [ARCHITECTURE.md](ARCHITECTURE.md); its last section,
[Terms](ARCHITECTURE.md#terms), is the glossary every other document links to. The table below
uses the glossary's words; each is defined there.

## Documents

In reading order.

| File | What it answers |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | What runs, what stands in for each PS2 component, and the Terms glossary. |
| [PLATFORM.md](PLATFORM.md) | Entry points, fibers and the scheduler, the host loop, the arena, folders and diagnostics. |
| [GAME_CODE.md](GAME_CODE.md) | How `ico2/` is compiled: the sources, options, host paths, layout asserts and audits. |
| [CONVENTIONS.md](CONVENTIONS.md) | The pre-commit hook, formatting, names, log lines, tests and scripts. |
| [DATA.md](DATA.md) | The disc layer: the VFS, disc images, `ico.o2r`, the extractor, libcdvd, SIF and the data tables. |
| [MATH.md](MATH.md) | The FP environment, the PS2 float models, the soft `double`, the libm copies and VU0 in C. |
| [GS.md](GS.md) | What the port adds under the game's GS layer: the register decoder and the hand-offs to rd. |
| [VU1_PROGRAMS.md](VU1_PROGRAMS.md) | The five VU1 microprograms, their shaders, their CPU oracles and the region rule. |
| [RENDER.md](RENDER.md) | rd: recording, state, the replay, pipelines, post passes, dumps and the replay tool. |
| [MSAA.md](MSAA.md) | The MSAA resolve: where the multisampled part of a frame ends, the covered-majority rule, one resolve a picture, its lines and tests. |
| [WORK_BUFFERS.md](WORK_BUFFERS.md) | The post effects' work buffers: sizes per frame width, the glow chain's rects, its left edge on a wide frame, what stays across frames and the work clear. |
| [TEXTURES.md](TEXTURES.md) | Texture decodes off the game thread: the decode pool, the job a store makes, the settle, its bound, key, lines and tests. |
| [DISPLAY.md](DISPLAY.md) | The native frame, the video options and presets, pacing and the vsync lock, presentation, interpolation and the CRT filter. |
| [RHI.md](RHI.md) | The render hardware interface and its Vulkan, Direct3D 12, Direct3D 11, OpenGL and MoltenVK backends. |
| [BACKENDS.md](BACKENDS.md) | The four backends as the renderer sees them: its branches, limits and policies, the OpenGL and Direct3D 11 profile rows, the window and the fallback order. |
| [SHADERS.md](SHADERS.md) | The HLSL sources, how they are compiled and embedded, the binding scheme and the shared constants. |
| [AUDIO.md](AUDIO.md) | The software SPU2, libsd, the sound driver as a host server, streams, mixing and the output. |
| [FMV.md](FMV.md) | The movie player: the PSS demuxer, the MPEG-2 decoder, pacing and the picture on screen. |
| [INPUT.md](INPUT.md) | Devices, bindings, the virtual pad and libpad, pad scripts and recordings, mouse and touch. |
| [SAVES.md](SAVES.md) | The memory cards as folders, where they live and the PS2 save importer. |
| [CONFIG.md](CONFIG.md) | Every key of `config.toml` and `ico-pc.ini`, which layer wins and when the files are written. |
| [UI.md](UI.md) | The port's menus: the layout extension, the Settings menu, strings, the font, popups, touch and mouse. |
| [GAME_FEATURES.md](GAME_FEATURES.md) | Gameplay options and their hooks, achievements, photo mode, the model viewer, gallery and credits. |
| [PACKS.md](PACKS.md) | Texture and model packs: names, loading, formats, log lines and dumping. |
| [TESTING.md](TESTING.md) | The test framework, every test program, the render gates, dumps, traces and CI. |
| [DIVERGENCES.md](DIVERGENCES.md) | Every known way the port's behaviour or output differs from the PS2 game. |

## Elsewhere

- [../BUILDING.md](../BUILDING.md): building, running the tests, CI and packaging.
- [../LEGAL.md](../LEGAL.md): what the repository may and may not contain, and the references its
  source was written from.
- [../THIRD_PARTY.md](../THIRD_PARTY.md): the third-party code and its licences.
- [../../README.md](../../README.md): the player's introduction and the guides.

## How these documents are written

They state only what the tree at HEAD does and pass the `docs_check` gate
(`python3 -I tools/check_docs.py`).

- A behaviour is cited by its file and symbol in the form `` `path` (`symbol`) ``, for example
  `` `port/platform/sched.c` (`ico_sched_run`) ``: the path in code, then the symbols, a section
  of the file or "header comment" in parentheses. Several citations in one place are separated by
  semicolons. Symbols, never line numbers.
- Each term is defined once in [ARCHITECTURE.md](ARCHITECTURE.md#terms) "Terms" and linked from
  its first use in a document's prose. A document's opening Files table (and this index) may use a
  term before that first link.
