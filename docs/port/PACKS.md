# Packs

How the port loads texture packs made for PCSX2 and its own model packs, how it names a texture
and a model part so a pack file finds its target, what each loader accepts, what the log says, and
how the dumps that pack makers start from are written. The player's side is
[../TEXTURE_PACKS.md](../TEXTURE_PACKS.md) and [../MODEL_PACKS.md](../MODEL_PACKS.md).

## Files

| path | what it holds |
| --- | --- |
| `port/render/texpack_name.h`, `texpack_name.c` | the names PCSX2 gives textures: the hashes, the bits word, candidate names, parsing |
| `port/render/xxh3.h`, `xxh3.c` | XXH3-64, the hash of texture names and of model parts |
| `port/render/texpack.h`, `texpack.c` | the texture pack: folders, index, loader thread, RAM cache, pump, budget, dumps |
| `port/render/texpack_png.c`, `texpack_dds.c`, `texpack_image.c` | the PNG and DDS loaders and their shared image |
| `port/render/modelpack.h`, `modelpack.c` | the model pack: folders, index, glTF to VU conversion, replacement meshes, dumps |
| `port/render/gltf.h`, `gltf.c` | the glTF 2.0 reader and writer |
| `ico2/seki/src/Texture.c` | the texture hook (`texHostPack`) and the per-frame pump (`tex_ResetVram`) |
| `ico2/seki/src/Packet.c` | the mesh hooks: lookup at build, the dump, the morph decline |
| `port/platform/main_host.c` | `open_texture_pack`, `open_model_pack` |
| `tools/package_textures_lib.sh` | the folders and README notes of the release packages |
| `tools/test/package_textures_test.sh` | the test of that staging and the archive checks |

## Texture names

A texture is named as PCSX2 names it, so a PCSX2 pack for SCES-50760 (or, on the Japanese disc,
SCPS-11003, and on the Korean disc, SCPS-56001) loads unchanged
(`port/render/texpack_name.h`, header comment, which cites the PCSX2 functions each rule comes
from):

- **name**: `TEX0Hash-CLUTHash-bits` for the palette formats (PSMT8, PSMT4 and their H variants),
  `TEX0Hash-bits` for the direct ones; the hashes in lower-case hex without zero padding, `bits`
  as 8 hex digits, then `.png` or `.dds`. Extra file levels are `<name>-mip<N>.<ext>`; region
  textures are `<hash>[-<clut>]-r<W>x<H>-<bits>.<ext>` or the `-r<hex>` form PCSX2 also reads.
- **bits**: `PSM | TW << 6 | TH << 10 | TA0 << 15 | AEM << 23 | TA1 << 24`. The
  [TEXA](ARCHITECTURE.md#terms) fields are 0 except for PSMCT24 and PSMCT16/16S, whose names depend
  on the TEXA in force.
- **TEX0Hash**: XXH3-64 (seed 0, the default secret) over the level the draw binds, with the
  following levels appended when the name is a mip chain. A level at least one
  [GS](ARCHITECTURE.md#terms) block in size in a format that fills its 32-bit word (PSMCT32, PSMT8,
  PSMT4) is hashed as the 256-byte GS blocks covering it, swizzled as GS memory holds them
  (`texpack_block_offset`); any other level is read out linearly, palette formats one byte per
  index, direct formats as RGBA32 with TEXA applied. Texels inside the GS size but outside the
  TIM2's are hashed as zeros and the name is marked unstable.
- **CLUTHash**: XXH3-64 over 16 or 256 colours in index order, the CSM1 swizzle undone; a 16-bit
  CLUT is expanded with the TEXA in force, so its name depends on TEXA too. 24-bit CLUTs are not
  supported.
- **mips**: PCSX2 with hardware mipmapping hashes the bound level and the levels after it, without
  it the bound level alone. The game binds lower levels itself, so a bound level tries, in order:
  that level alone, its chain, then each lower level alone and as a chain, for packs dumped while
  the game showed a smaller level (`texpack_candidates`).

The candidates come once per TEXA mode where the name depends on it, at most 64 names
(`TEXPACK_MAX_CANDIDATES`). The index key is `(tex0Hash, clutHash, bits)`; `texpack_parse_name`
reads a file name back as PCSX2 does, returning region names apart and refusing `-mipN` files.
`xxh3_64` is a scalar C reimplementation of XXH3_64bits from xxHash v0.8.2, checked against
xxHash's own sanity vectors (`port/render/xxh3.c`, header comment;
`port/render/test/texpack_name_test.c`).

## Texture pack loading

`open_texture_pack` fills a `TexpackConfig` from the display options and the
[RHI](ARCHITECTURE.md#terms)'s limits and calls `texpack_init` once at start
(`port/platform/main_host.c` (`open_texture_pack`)). The game's side is `texHostPack`, called after
a texture is decoded: it builds the TIM2's `TexpackSource` (every level, the CLUT in CSM1 order),
asks for the candidates, and queues the first name the index has; with `[video] dump_textures` it
writes the level as drawn (`ico2/seki/src/Texture.c` (`texHostPack`)). It runs only with a pack
indexed and the option on, or the dump on.

### Folders

Each disc has its own serial folder, the one PCSX2 uses for it: `SCES-50760` for the European
disc, `SCPS-11003` for the Japanese one and `SCPS-56001` for the Korean one. The serial is the selected disc's `pack_serial`
(`port/core/region.h`), which `main_host.c` passes as `TexpackConfig.serial` once the disc is
identified (`open_texture_pack`; a `NULL` serial is also the selected disc's); a pack in another
disc's folder is not read. `texpack_init` walks, in order, in the
[user folder](ARCHITECTURE.md#terms) and the program folder:
`<user folder>/textures/<serial>/replacements`, then
`<program folder>/textures/<serial>/replacements`, then for each of the two
`textures/replacements` and the loose files directly in `textures/` (a root that is both, in
portable mode, is walked once). The two `replacements` folders are walked with every folder below
them, up to 16 deep; `textures/` itself gives only its own files, since below it are the serial
folders and the dumps (`port/render/texpack.c` (`texpack_init`, `walkFolder`)). The walk matches
`png` and `dds` in any case and does not follow folder links. The first file
of a name wins and later ones are counted as duplicates; `-mipN` files are counted and skipped,
because an RGBA8 image gets its own mip chain; region names are counted, never matched
(`port/render/texpack.h`, folders; `port/render/texpack.c` (`TEXPACK_WALK_DEPTH`)).

### The loader thread and the RAM cache

`texpack_request` queues a file for the loader thread; `texpack_pump`, run on the game
[fiber](ARCHITECTURE.md#terms) once a frame beside `rdtex_frame_tick` (from `tex_ResetVram`),
installs what finished through `rdtex_create_replacement` and `rdtex_replace`, provided the texture
still has the generation it was requested for. With `[video] texture_pack_precache` (default on) and
the pack switched on (`[video] texture_pack`), the thread also reads every indexed file into a RAM
cache from start, the PNGs first (subtitles and menus) and then the DDS files, up to
`[video] texture_pack_cache_mb` (0: half the computer's memory; Android: an eighth, at most 512 MB);
a file that does not fit is skipped. A request for a cached file is installed at once inside the
hook, so a texture shown for one frame is replaced from its first (`port/render/texpack.h`,
loading). Without SDL the same work runs inside `texpack_pump`, one request a call
(`port/render/texpack.c`, header comment). When the system reports low memory (Android), the cache
lets go of every image not being copied and stops reading ahead (`texpack_low_memory`).

### The budget

A replacement lives on the GPU only while its cache entry does, and its bytes count against
`[video] texture_pack_budget_mb` (default 2048, 128 to 65536), apart from the RAM cache's limit. A
replacement that would go over is declined, the game's texture kept, and the first refusal logged
(`port/render/texpack.h`, budget; `port/game/video_options.h`, the key table). A texture the
graphics card refused is not offered again in the run (`texpack_budget_release`).

### Mip levels

A pack image carries at most 16 levels (`TEXPACK_IMAGE_LEVELS`): a DDS file's own mips, or, for
an RGBA8 image with one level, the box chain `rdtex_replacement_mips` adds; the device never gets
more than the full chain. A compressed file without mips is drawn without them, logged once
(`port/render/rd_tex.c` (`rdtex_create_replacement`)). The loaders refuse an image wider or higher
than the device's largest texture when its header is read (`texpack_set_max_side`).

### Formats

- **PNG** (`port/render/texpack_png.c`, header comment): a reader on miniz's inflate that gives
  PCSX2's texels for its two cases, RGBA rows as they are (the alpha byte is GS alpha, 0x80
  opaque) and RGB rows with alpha 0x80, and extends the same to every colour type, bit depths 1 to
  16, the five filters, Adam7 and tRNS; everything without alpha gets 0x80. A critical chunk with a
  bad CRC refuses the file.
- **DDS** (`port/render/texpack_dds.c`, header comment): PCSX2's rules, so a file loads exactly
  when it loads there. DXT1/DXGI 71 is BC1, DXT2/DXT3/DXGI 74 BC2, DXT4/DXT5/DXGI 77 BC3, DXGI 98
  BC7, kept as blocks and refused when the device has no BC textures; the five uncompressed layouts
  of DirectXTex are converted to RGBA8 with PCSX2's alpha quirks (X8B8G8R8 gets 0x80, X8R8G8B8 and
  R8G8B8 0xFF). Level 0 of a BC file must be a multiple of 4 both ways.
- **The image** (`port/render/texpack_image.c`): one allocation holding every level, freed by
  `texpack_free_image`; its size is what the image costs in RAM and on the GPU
  (`TexpackImage.bytes`).

A new format is: a `TexpackKind` value (`port/render/texpack.h`), its extension in `fileKind` and
its loader call in `loadEntry` (`port/render/texpack.c`), and its place in the precache's order
(`texpack.c`, the loop over `TEXPACK_KIND_PNG` .. `TEXPACK_KIND_DDS`). The loader is a
`texpack_load_*` in `texpack.h` that fills a `TexpackImage` (`fmt` an `RD_TEXEL_*` the texture
cache uploads, every level in one `blob`) and logs a refusal as
`textures: %s: %s; the game's texture is kept`; its source joins the `ico_render` sources in
`port/render/CMakeLists.txt`. The name parser (`texpack_parse_name`) needs no change: it reads the
name up to the first `.` after it. The loaders' file cases live in the ctest `texpack_load`
(`port/render/test/texpack_load_test.c`).

### Log lines

The loaders write these lines to the log, exactly (`%` fields filled in). From `texpack.c`:

| line | when |
| --- | --- |
| `textures: %u replacements from %s` | per folder that added files (`walkFolder`) |
| `textures: no texture pack (looked in %s%s%s)` | nothing indexed (`texpack_init`); the fields are the standard folders, joined by `, ` |
| `textures: skipped %u files named like another, %u region textures, %u extra mip levels, %u files whose names are not texture names` | any file skipped at the walk |
| `textures: the graphics card has no compressed (BC) textures; the pack's DDS files of that kind are skipped` | a pack and no BC on the device |
| `textures: no loader thread (%s); replacements load between frames` | the thread could not start |
| `textures: reading the pack into memory in the background (up to %llu MB)` | the precache starts |
| `textures: %u replacements read into memory (%llu MB) in %.1f s` | the precache finished with everything in |
| `textures: %u replacements read into memory (%llu MB) in %.1f s; %u more did not fit in the %llu MB allowed (texture_pack_cache_mb) and load when the game shows them` | the precache finished with files left out |
| `textures: the system is low on memory; %u replacements (%llu MB) let go, the texture pack's cache stays at %llu MB and reads nothing more ahead` | `texpack_low_memory` freed something |
| `textures: cannot read %s` | a file could not be read |
| `textures: the texture pack's memory limit (%llu MB, texture_pack_budget_mb) is reached; textures past it stay the game's own` | the first budget refusal |
| `textures: %s could not be made into a texture (reported once)` | `rdtex_create_replacement` failed |
| `textures: texture %u replaced by %s` | every replacement, developer mode only |
| `textures: writing texture dumps to %s` | the first dump |
| `textures: cannot make %s; no texture dumps` | the dumps folder cannot be made |

`texpack_png.c` and `texpack_dds.c` write `textures: %s: %s; the game's texture is kept` for a
file they refuse, with the reason. The renderer's own lines carry its `rd: ` prefix
(`port/render/rd_core.c` (`rd__log`)): `rd: textures: "%s" is %ux%u, larger than this graphics
card takes (%u)` and `rd: textures: compressed textures without mipmaps are drawn without them
("%s" is the first)` (`port/render/rd_tex.c`), `rd: textures: the graphics card refused "%s"
(%ux%u); the game's own texture is shown instead` and `rd: textures: no room for the %llu MB upload
of "%s" (%ux%u); it waits for a later frame (reported once)` (`port/render/rd_replay.c`).

## The shipped README

The release packages stage seven folders with a `README.txt` in each, because a zip keeps only
files and the note is what keeps the folder: `textures/<serial>/replacements/` and
`models/<serial>/replacements/` for each of `SCES-50760`, `SCPS-11003` and `SCPS-56001`, and
`reshade/` (`tools/package_textures_lib.sh`
(`pkg_stage_folder_notes`, `pkg_note_rels`)). The textures note says the folder is for texture
packs and playing needs nothing in it; to copy a pack keeping its own folders so its files end up
under that disc's `textures/<serial>/replacements/` next to the program (the examples name
`SCES-50760`), a pack one level too high being
found too; that Options > Display > Texture pack then reads On and `logs/ico-pc.log` says how many
textures were found; that with portable mode the pack may also go in
`userdata/textures/SCES-50760/replacements/`; and that packs made for PCSX2 work as they are
(`pkg_stage_textures_readme`). The models note says the same for model packs and points at
`docs/MODEL_PACKS.md` (`pkg_stage_models_readme`).

The notes are written with Linux wording and forward slashes; `pkg_note_eol` with `crlf` turns
the slashes into backslashes and adds CRs for Windows. They land next to the program: in the
Windows zip under `ico-pc-<label>/<arch>/`, in the Linux archive under `ico-pc-<label>/`, and both
package scripts fail when a note is missing (`tools/package_win.sh`, `tools/package_linux.sh`,
the `pkg_note_rels` checks).

## Model packs

A model pack replaces the game's mesh parts with glTF files (`port/render/modelpack.h`, header
comment). A part is named by its mesh hash, `rd_vu_mesh_desc_hash`: XXH3-64 over the part's
tagless VU stream, written as 16 lower-case hex digits.

### Folders and names

The folders are those of texture packs with `models` for `textures`:
`<user folder>/models/<serial>/replacements` (the selected disc's serial, as for textures), then
the program folder's, then for each of the two
`models/replacements` and the loose files directly in `models/`; the `replacements` folders with
every folder below them, `.gltf` and `.glb` in any case (`port/render/modelpack.c`
(`modelpack_init`, `walkFolder`)). The first file of a hash wins
and later ones are duplicates; other files (the `.bin` beside a `.gltf`, readmes, pictures) are
passed over quietly. A model file is named `<hash16>.gltf` or `.glb`, or `<anything>-<hash16>`
with the extension (the last 16 characters before it); any other `.gltf` or `.glb` is counted as
not a model name (`port/render/modelpack.h`, folders and names).

### Loading and conversion

`modelpack_init` reads and converts every file at start, with no thread, before the game loads a
model, because meshes made at load look their parts up (`port/platform/main_host.c`
(`open_model_pack`)). Each primitive becomes VU strips drawing in place of the original's batch of
the same index: strips joined greedily on a shared last edge (VU meshes draw without culling, so
winding is free), equal vertices merged and degenerate triangles dropped; ST from `TEXCOORD_0`;
colour as the raw GS byte (128 normal brightness, alpha 127, (128, 128, 128) without `COLOR_0`);
normals normalised; the two largest of `JOINTS_0` and `WEIGHTS_0` renormalised, each joint as the
VU address `bone * 4 + 16` (`port/render/modelpack.h`, the conversion). A static mesh takes the
mesh node's world transform; a skinned mesh keeps its bind-pose positions. The layout is skinned
(5 quadwords a vertex) when a primitive has `JOINTS_0`, lit (4) when every primitive has `NORMAL`,
prelit (3) otherwise. The caps are 512 MB of converted meshes in all, 1,048,576 vertices and 4096
batches a mesh, and bones below 60, VU memory 16 to 255 (`MODELPACK_MAX_BYTES`,
`MODELPACK_MAX_BONES`).

The game's side is `ico2/seki/src/Packet.c`: `pac_hostBuild` looks the part's hash up and asks
`modelpack_create` for a replacement mesh, building the original when it gets none; `pac_HostDump`
writes a part; `pac_HostRefreshFor` declines a part whose shape changes every frame
(`modelpack_decline`), because the morph path cannot write a replaced mesh, and builds the original
again. The Options switch retires every replaced mesh when turned off and every original with an
entry when turned on, so the next draw builds the other one (`modelpack_set_enabled`).

### Static and skinned replacements, and what cannot change

`modelpack_create` first checks that the part hashes to the entry's hash (a different part gets
nothing, silently); then that the layouts agree, that the file has no more primitives than the
original has batches, that every bone it uses is below `min(boneCount, 60)` and that its pieces fit
its vertices, a mismatch declining the entry for that part with one log line
(`port/render/modelpack.c` (`modelpack_create`, `decline`)). So a replacement:

- keeps its part's kind: a skinned part takes only a skinned file and a static part only a static
  one; a lit part needs normals; a lit file may serve a prelit part, its normals dropped (Blender
  always exports them);
- has at most as many pieces as the original has batches;
- uses only the bones the model has, under the game's skeleton: a skinned file's own inverse bind
  matrices are ignored for the game's, with one log line when they differ
  (`modelpack_note_skeleton`);
- cannot replace a part that changes shape every frame;
- draws each piece with the texture and material of the original batch it stands in for, through
  the UVs and colours the file gives.

### Log lines

From `port/render/modelpack.c` unless named:

| line | when |
| --- | --- |
| `models: %u replacements from %s` | per folder that added files (`walkFolder`) |
| `models: no model pack (looked in %s%s%s)` | nothing indexed (`modelpack_init`); the fields as for textures |
| `models: skipped %u files named like another, %u files whose names are not model names` | duplicates or bad names at the walk |
| `models: %s skipped: %s` | a file `gltf_read` or the conversion refused, with the reason |
| `models: %s skipped: the model pack is over %llu MB` | the 512 MB cap reached |
| `models: %d replacements, model pack %s, dump models %s` | once at start (`port/platform/main_host.c` (`open_model_pack`)) |
| `models: %s not used for %s: %s` | the first decline of a file for a part (`decline`); the reasons follow |
| `models: %s has its own inverse bind matrices, which differ from the game's; the game's are used` | once per file (`modelpack_note_skeleton`) |
| `models: %s: the game could not build %s just now; trying again later` | the mesh could not be made, once per file |
| `models: %s replaced by %s` | every replacement, developer mode only |
| `models: model %s part %d changes shape every frame; the original is used` | a morphing part with an entry (`ico2/seki/src/Packet.c` (`pac_HostRefreshFor`)); without the model's name, `models: a model part (mesh %016llx) changes shape every frame; the original is used` |
| `models: writing model dumps to %s` | the first dump |
| `models: cannot make %s; no model dumps` | the dumps folder cannot be made |
| `models: cannot write the dump of %s: %s` | `gltf_write` failed |

The decline reasons are: `the original is skinned (it moves with bones) and this file is not`,
`this file has bone weights and the original does not move with bones`, `the original is lit and
needs normals, which this file lacks`, `the original is a %s part and this file a %s one`, `it has
%u pieces and the original %u`, `it uses bone %u and the model has %u`, and `its pieces do not fit
its vertices`.

### The glTF subset

`gltf_read` accepts (`port/render/gltf.h`, header comment):

- a `.gltf` whose buffers have a relative `uri` in its folder (no `data:` or other schemes, no
  absolute paths, no `..`), or a `.glb` with a JSON chunk and an optional BIN chunk; the two are
  told apart by the magic, not the extension;
- the first mesh node in a depth-first walk of the scene, its world transform taken from matrices
  or TRS; other mesh nodes are ignored;
- primitives of mode 4 (triangles) only, at most 4096, `POSITION` required, indices a multiple of
  3; `NORMAL`, `TEXCOORD_0`, `COLOR_0` (alpha dropped), `JOINTS_0`, `WEIGHTS_0` in float or
  normalized integer forms; other attributes and morph targets ignored;
- the mesh node's skin, joints mapped to bones by the node name `bone_NN` when every joint is so
  named, else by skin order;
- each file at most 256 MB, at most 16 buffers.

It refuses `data:` URIs, sparse accessors, other modes, a missing `POSITION`, short buffers, an
`extensionsRequired` entry, a version other than 2.x, and malformed JSON or GLB, each with a
one-line reason. Matrices need no transposing: a PS2 matrix, row-vector and row-major, is its glTF
matrix verbatim.

## Dumping

Both dumps need developer mode as well as their key (`port/platform/window_host.c` (`dumpTextures`,
`dumpModels`); the Settings rows appear only in developer mode, `port/ui/settings.c`):

- **`[video] dump_textures`** writes the bound level of each texture the game decodes as PNG under
  every name a pack would give it at that level, one per TEXA mode when the name depends on TEXA,
  into `<user folder>/textures/<serial>/dumps/<name>.png` (the selected disc's serial); a file already there is kept. The
  texels are the decoded, padded RGBA8 with raw GS alpha, what a PCSX2 dump holds
  (`port/render/texpack.h` (`texpack_dump`)). It works without a pack.
- **`[video] dump_models`** writes each part drawn, once per hash a session, as
  `<user folder>/models/<serial>/dumps/<hash16>.gltf` and `.bin`, and appends a line to
  `dumps/models.txt` (hash, model, part, ordinal, layout, vertices, batches, bones); a file on disk
  is kept (`port/render/modelpack.h` (`modelpack_dump`)). The dump's layout is the contract with
  pack makers: node 0 the mesh with the node matrix, joints named `bone_00`, `bone_01`, ...,
  `inverseBindMatrices` the game's cluster matrices verbatim, an `extras` block with the part's
  normal word, bone count, box and `colorScale` 128 (`port/render/gltf.h`, what `gltf_write`
  writes; `port/render/modelpack.c`, `modelpack_dump`, the extras).

The model viewer's save button arms a one-shot dump of every part of the object on screen, dumping
on or off, rewriting files already there (`modelpack_dump_object_once`,
`modelpack_dump_object_status`;
[GAME_FEATURES.md](GAME_FEATURES.md#the-model-viewer-and-characters)).

## Tests

`tools/test/package_textures_test.sh` (ctest `package_textures`, label `tools`; it builds nothing
and runs nothing of the game) stages the three folder notes for Windows and checks every line ends
in CRLF and the textures note has at least 7 lines, stages them for Linux and checks no CR, checks
that the zip and tar assertions pass on a present entry and fail on a wrong path or a missing one,
and goes on to the portable archives, `make_portable.sh`, the developer files kept out of the player
archives, and the version information (`tools/test/package_textures_test.sh`, the `ok` lines;
`CMakeLists.txt` (`package_textures`)). The loaders and names have their own C tests, listed in
[TESTING.md](TESTING.md#test-programs-by-area): `texpack_name`, `texpack_load`,
`texpack_pack_files` (`texpack_load_test --pack` on the files of `ICO_TEXPACK_DIR`), `texpack`,
`texpack_disc`, `texpack_e2e`, `gltf`, `modelpack`.
