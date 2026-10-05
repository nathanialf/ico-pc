# docs/

An index of this directory: what each file is for.

| file | what it is |
| --- | --- |
| [`BUILDING.md`](BUILDING.md) | the host build: quickstart, presets and options, the hooks, CI, packages, and the maintainers' appendix on the optional EE identity check |
| [`LEGAL.md`](LEGAL.md) | what the repository may and may not contain, why the data tables are generated from the user's ELF at build time, and which references were used. Read it before contributing |
| [`HEADERS.md`](HEADERS.md) | which headers the disc attests, the code includes, where the other declarations live, and the derived-name tokens |

## Port notes

`docs/port/` holds the port's own notes; other files may be added.

| file | what it is |
| --- | --- |
| [`port/DIVERGENCES.md`](port/DIVERGENCES.md) | divergences from the PS2 original |
| [`port/BUILD_STATUS.md`](port/BUILD_STATUS.md) | what the host build compiles per preset, and what links |
| [`port/TESTING.md`](port/TESTING.md) | test checkpoints, `ico-pc.ini` / `config.toml` keys, `tools/package_win.sh`, `tools/package_linux.sh`, CI |
| [`port/STEAMDECK.md`](port/STEAMDECK.md) | the Linux package and running it on a Steam Deck: where the archive, config and saves live |

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
