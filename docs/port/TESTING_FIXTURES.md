# Test fixtures

Every test gets its input from the repository. The tests that replay a frame of the game read a
**fixture**: a committed file that keeps the sequence of operations of one frame the game drew and
none of the game's art. A fixture expands, on any machine, to the frame dump the
[replay tool](ARCHITECTURE.md#terms) reads, byte for byte the same everywhere (its SHA-256 is
pinned in the file). The dumps the fixtures were made from stay on the developer's machine; no test
reads them. How the tests are wired in general is [TESTING.md](TESTING.md)'s.

## Files

| path | what it holds |
| --- | --- |
| `port/render/rd_fixture.h`, `rd_fixture.c` | the format, the patterns and generators, the payload roles, the expansion, the check |
| `port/render/rd_fixture_make.c` | the maker (a fixture from a dump) and the window check |
| `port/render/tools/rd_fixture_tool.c` | the host tool: `expand`, `check`, `check-dir`, `make`, `remake`, `selfcheck`, `selfcheck-dir`, `props`, `set-props`, `info`, `rules`, `patterns` |
| `port/render/rd_fixture.cmake` | the library, the tool, `rd_fixture_format`, `rd_fixtures_check`, `rd_fixtures_selfcheck` |
| `port/render/test/rd_fixture_helpers.cmake` | what a test script calls: the list, the expansion, the declared properties and their quantities |
| `port/render/test/rd_fixture_test.c` | `rd_fixture_format`: the format on a frame the test builds |
| `port/render/test/fixtures/*.rdfix` | the fixtures |
| `port/render/test/fixtures/fixtures.txt` | the manifest: each fixture's source dump and declared properties |
| `port/render/test/fixtures/lists/*.txt` | the fixture lists the tests read (names, never paths) |
| `tools/rdfix_check.py` | a fixture parses with no art section (`tools/check_no_rom.sh` rule 8, `rdfix_check`) |
| `tools/check_test_paths.py`, `tools/check_test_paths_allow.txt` | no test names a path outside the repository (`test_paths_check`), and the lists not converted yet |

## What a fixture keeps

The frame's structure, verbatim from the dump ([rd_dump.c](../../port/render/rd_dump.c)'s layout):

- the head: the frame's GS size, aspect, widths, region, number, keep flag and camera, the start and
  end GS state blocks;
- the 13 command lists in order: every state delta (TEST, ALPHA, TEXA, FILTER, WRAP, TEXTURE,
  COLORMASK, TARGET, SCISSOR, ...), every draw, clear, copy, post record and shadow reset or
  resolve, in the order the game issued them;
- in the payload, the ranges the commands name as structure: the sprites and screen rectangles the
  effect code draws (positions, texture coordinates, colours), copy rectangles, post records with
  the reduction's corners and the fog's table, the VU draws' headers and blocks (the placement
  matrices, the lights, the common block), the bone poses, the materials, the grid strips' VIF and
  GIF tag and closing qwords, the particle headers and each particle's size, atlas cell, grey and
  alpha, the text items' layout and colour;
- the texture headers (id, kind, source format, TEXA, width, height, target, format and style
  word) and GS level counts, the temporary targets, the meshes' vertex counts, layouts, index lists
  and batch records, the tick cameras of a blended frame, which meshes have a facing table.

The numbers recorded from play (the camera, each object's placement, the bone poses, the light
values) are kept: they are the frame's operations, not its art.

## What a fixture does not hold

No texel, no vertex and no text of the game, and nothing derived from them (no statistic, no
histogram, no colour, no extent, no alpha copied from the original):

- every image texture is a **pattern name** chosen by a rule on the state of the draws that use it
  (below); its size, format and level count are kept;
- every mesh vertex stream, CPU-projected triangle, shadow volume, grid vertex and particle position
  is **generated** by a fixed rule from counts, formats and topology alone (below);
- a text item's characters become `x` (its length kept), a mesh's name `m0001`, `m0002`, ...;
- the VU draws' card verdicts are dropped (the loader makes them from the generated geometry) and
  the facing tables' bits are zero;
- the world prims (recorded, never replayed) and every payload byte no command names are zero.

`rd_fixture_tool check` proves it on the file alone: the skeleton parses to its end with no array
in place of a name, and every payload byte a command does not name as structure is zero.

## The patterns

| name | RGBA (a one-byte format: the alpha for the alpha patterns, else the red) | for |
| --- | --- | --- |
| `grey` | 128, 128, 128, 128 | a texture no draw uses |
| `gradient` | red across, green down, blue 128, alpha 128 | blended draws |
| `checker` | 8-texel cells of 64 and 192, alpha 128 | the default; sprites |
| `bright` | 255 over the middle half, 32 around it, alpha 128 | additive draws: the glow's threshold sees light |
| `alpha_ramp` | 160 grey, alpha 0 to 255 across (one byte: the alpha) | alpha-tested draws: every reference is crossed |
| `date_split` | 128 grey, alpha 0x80 (top bit set) on the left half, 0x40 (clear) on the right | draws under DATE |
| `opaque` | 96, 160, 224, alpha 128 | rules that want an opaque colour |
| `clear` | 128 grey, alpha 0 | rules that want a transparent one |
| `ramp` | (x + y) across 0 to 255 | R8 images |
| `glyphs` | 4-texel cells of 0 and 255 | font sheets (coverage) |

Each GS level is the same pattern at the level's own size. Every texel is integer arithmetic on its
coordinates and the size.

## The texture rules

A rule line is a pattern name and the conditions that must all hold (`!c`: must not); the first
line that matches a texture names its pattern. A texture's conditions are gathered over every draw
that samples it, from the GS state at that draw (the maker walks the lists with
`rd__apply_state`): never from its texels.

| condition | a draw of the texture ... |
| --- | --- |
| `ate` | has the alpha test on (not ALWAYS) |
| `date` | tests the destination alpha (DATE) |
| `additive` | blends with the source added (Cs*FIX+Cd, Cd*FIX+Cs, Cs*As+Cd, Cs*Ad+Cd) |
| `abe` | blends |
| `fba` | forces the alpha's top bit (FBA) |
| `rgb` | takes the colour only (TCC RGB) |
| `screen`, `sprite`, `text`, `vu` | is a CPU-projected prim, a screen rectangle, glyph quads, a VU draw |
| `used` / `unused` | exists / does not |
| `r8`, `sheet`, `rgba8`, `mip` | (the texture) is R8, a font sheet, RGBA8, has GS levels |

The default rules (`rd_fixture_tool rules`):

```
glyphs     sheet
ramp       r8
date_split date
alpha_ramp ate
bright     additive
gradient   abe
checker    sprite
grey       unused
checker
```

A family that needs other data writes a rule file in `port/render/test/fixtures/` and names it on
the fixture's manifest line (`rules=FILE`). The rules a fixture was made with are kept in its notes
(`rule=`).

## The generated geometry

The command stream carries no box for a mesh (the game's cull box is computed from the model), so
nothing of a model's shape or extent is used:

- **Meshes** (prelit, lit, skinned): the same vertex count, layout, index list and batches. Vertex
  positions lie on a lattice of step 2^(k-3) inside the cube of half side 2^k about the model's
  origin; k is the first power of two (from -12 to 24) whose cube's corners reach 24 GS pixels from
  the origin's place on screen through the first draw's placement matrix (VU memory 16..19; a
  skinned draw: its first bone, then the common block's world to screen), 0 when the origin is
  behind the eye. Strips run along the lattice's rows, one strip a row band. The strip flags (ST.w
  0 on a strip's first vertex) are rebuilt from the kept index list: a vertex starts a strip where
  neither it nor the next one kicks. Normals are the unit axes (one a strip), ST on a 1/16 grid,
  the colour (112, 120, 136, 127).
- **Skinned meshes**: each vertex weighted half to two bones, `16 + 4 (s mod n)` and
  `16 + 4 ((s + 1) mod n)` for strip s, n the fewest bones of the mesh's skinned draws: always
  valid for the pose the frame holds.
- **Grid strips** (pools, water): the colour qword and each strip's vertices on the lattice, at the
  size the grid draw's matrix asks (kept per draw in the fixture); the VIF, GIF tag and closing
  qwords are structure.
- **Particles**: x, y and z on a 5-wide lattice at a quarter of the size the particle matrix asks;
  the rest of each particle is structure.
- **CPU-projected prims** (not rectangles): a ribbon of triangles over the middle half of the
  target, in front of everything (GS Z 0xFFFFFFFF), grey; glyph quads: a row of 8-pixel cells from
  the top left over the sheet's first cell.
- **Shadow volumes**: each volume command's increments tile a footprint of a quarter of the target
  each way about its middle (moved by a sixteenth of the width per command, five places), at the
  farthest Z (they always pass the count's GEQUAL); its decrements tile the same footprint at Z 0
  (they pass only where nothing was drawn). So the count lands 1 on the receiving surface inside
  the footprint and 0 over the cleared sky, the volumes' box is smaller than the frame, and the
  triangle counts are the source's. The vertices carry their triangle's place (1, 2, ...) as the
  recorder tags them.

Every float is written from its bits (an integer times a power of two, exact), so no compiler or
CPU changes a byte.

## The format

`port/render/rd_fixture.h` is the reference. In short: the head (`ICORDFX1`, the format, the
expanded dump's version, the expansion's SHA-256 and size), the notes (`key=value`: `source=`,
`maker=`, `rule=`, `selfcheck=`, `expect=`), and the deflated skeleton: the dump's own layout with
every texture's texels replaced by its pattern name, every mesh's name and stream by the generator's
name, its size exponent and bone count, the facing tables by their sizes, and at the end the size
exponent of each grid and particle draw. A fixture is made from a dump of any version the loader
reads (3 on: the 80- and 84-byte state blocks of versions 3 and 4 to 8, no GS levels field before
9) and expands to that version's layout.

## Declared properties: the path ran

A test only means something if its fixture's data takes the path under test, so every fixture
declares what it carries (`expect=QUANTITY OP VALUE`, OP one of `>=`, `<=`, `>`, `<`, `=`, `!=`)
and every test checks the ones it measures against the replay's `--stats`
(`rd_fixture_require`). A declared property that does not hold, a quantity the stats do not show,
or a fixture that declares none of the test's quantities fails the test. A fixture on which the
path does not run is a failed conversion: choose another source frame or a rule file, never a
weaker property.

The quantities (`rd_fixture_helpers.cmake`, each the first match of its regex in the stats lines;
add a row when a family needs one):

| quantity | the stats words |
| --- | --- |
| `draws`, `passes` | `N draws, M passes` |
| `shadow_volume_draws` | `volume draws by date_clear_ps of N` (the volume draws seen, either entry) |
| `shadow_box_counts`, `shadow_box_composites`, `shadow_box_empty` | `shadow box on: N counts and M composites ... (E with nothing to draw` |
| `shadow_count_one` | `shadow count: N in one draw` |
| `shadow_level_texels`, `shadow_levels` | `shadow levels drawn: N texels`, `shadow levels: N shadows` |
| `date_snapshots`, `date_stencil_draws` | `DATE snapshots N`, `DATE stencil draws N` |
| `glow_passes`, `glow_plan`, `glow_chains` | `glow N passes`, `light plan on: WORD`, `glow blur on: N chains` |
| `crack_pixels` | `crack fill on: ... N pixels filled` |
| `frustum_culled`, `occluded` | `scene draws skipped (N frustum, M occluded` |
| `vu_merge_draws`, `vu_fuse_pairs`, `prepass_vertices`, `region_rule_draws` | the VU merge, fuse, pre-pass and region rule lines |
| `fades_folded` | `fades fold on: N sprites folded` |
| `date_one_shared`, `empty_composites_skipped` | `DATE one set-up on: N shared`, `empty composites on: N skipped` |
| `no_volume_kept`, `no_volume_break_taken` | `no volume on: N kept`, `break on: N taken` |
| `stencil_window_stores` | `stencil inside the shadow window only: N stores` |
| `date_cut_passes` | `DATE cut on: N passes begun without the depth` |

The fixtures' declared properties are their manifest line's; `rd_fixtures_check` holds the two
equal.

## Using fixtures in a test

The registration passes the tool, the folder, the helpers and a list:

```
-DFIXTURE_TOOL=$<TARGET_FILE:rd_fixture_tool> -DFIXTURES=${ICO_FIXTURE_DIR}
-DFIXTURE_HELPERS=${ICO_FIXTURE_HELPERS} -DLIST=${ICO_FIXTURE_DIR}/lists/<family>.txt
```

(`include(rd_fixture.cmake)` first: it defines both variables and the tool). The script:

```
include("${FIXTURE_HELPERS}")
rd_fixture_list("${LIST}" names)
foreach(name IN LISTS names)
  rd_fixture_expand(${name} "${OUT}/fixtures" dump)   # into the build tree
  ... replay "${dump}" in both arms, compare ...
  rd_fixture_require(${name} "${stdout}" quantity ...)  # the path ran
endforeach()
math(EXPR failures "${failures} + ${RDFX_FAILURES}")
```

A list names fixtures by name, a line each; any path in a list fails. A listed fixture that is
absent or does not expand to its pin fails. There is no skip for a missing input: only "no usable
device" skips (77).

## Making a fixture from a dump

On the developer's machine, with the source dumps in a folder outside the repository (the
**sources folder**, `ICO_FIXTURE_SOURCES`; its manifest lists every file with its size and SHA-256):

1. Copy the dump into the sources folder (keep its relative name), then add the fixture's line to
   `port/render/test/fixtures/fixtures.txt`: its name, the source's name below the folder, and the
   properties it is meant to carry.
2. `rd_fixture_tool remake port/render/test/fixtures/fixtures.txt "$ICO_FIXTURE_SOURCES"
   port/render/test/fixtures NAME` makes it (one-off: `rd_fixture_tool make DUMP OUT.rdfix
   --source NAME --expect PROP ... --selfcheck`). The maker prints what it decided (the patterns
   by count, the meshes' sizes, the generated spans) and runs the window check; a fixture whose
   check finds a hit is not written.
3. Replay the expansion (`rd_fixture_tool expand`) with `--stats` at the family's settings and
   check that every declared property holds; then run the family's tests.
4. Keep the fixture well under the 256 KiB file cap (`tools/check_no_rom.sh`).

**The window check** (`selfcheck`): every 32-byte, 4-byte aligned window with at least three
distinct words of the generated bytes of the expansion (texels, levels, streams, generated payload
ranges) is looked up among every such window of the source's art (its texels, levels, streams, the
same payload ranges and its card verdicts). The result is kept in the fixture's notes
(`selfcheck=`); `rd_fixtures_selfcheck` repeats it where the sources folder is.

## Hand-writing a fixture

Where no dump shows a property (or the only one is gone), a fixture is made from a frame built by
code: a test program records the frame through the `rd_*` calls (as `rd_pixel_test.c` writes its
own dumps: `rd_begin_frame`, the state and draw calls, `rd_dump_frame`), and the maker turns that
dump into a fixture as above (its source line names the program's output under the sources
folder). The frame's own data is already generic; the fixture still holds only its structure.
`rd_fixture_tool set-props` changes a fixture's declared properties without remaking it (the
expansion does not change); its manifest line must say the same.

## The checks

- `rd_fixture_format` (`rd_fixture_test.c`, CPU): a version 17 frame (and the same frame as
  version 8, its own layout and size) built in memory with
  pseudo-random art: made, checked, expanded twice to the same bytes and its pin; the structure kept
  byte for byte; each generator's output as described above; the window check clean; art written
  into a generated range and a changed pin refused; the file round trip; SHA-256 against FIPS
  180-2's vectors.
- `rd_fixtures_check` (CPU, every suite): every committed fixture holds no art, declares a
  property, expands to its pin; the manifest names exactly the folder's fixtures, with the same
  properties.
- `rd_fixtures_selfcheck`: the window check of each fixture against its source in
  `ICO_FIXTURE_SOURCES`; SKIP (77) where that folder is not set.
- `rdfix_check` and `tools/check_no_rom.sh` rule 8: each tracked fixture parses with no art section
  (Python, no build).
- `test_paths_check` and `test_paths_selftest` (`tools/check_test_paths.py`, also in the
  pre-commit hook and CI's hygiene steps): no test list, test script or registration names a path
  outside the repository (an absolute path, a home folder, a path that climbs out of the tree, the
  machine-local dump folders), printing the file and line. Its allow-list names the files not
  converted yet; the guard fails when one of them names no such path any more, so the list only
  shrinks.

## The tests on fixtures

| test | list | fixtures | the properties it checks |
| --- | --- | --- | --- |
| `rd_shadow_box_ab`, `rd_shadow_box_ab_gl` | `lists/shadow_box.txt` | `fx-shadow-wide` (a character's shadow at 21:9: `shadow_volume_draws>=1`, `shadow_box_counts>=1`, `shadow_box_composites>=1`), `fx-lamps-novolume` (a shadow window with no volume: `shadow_volume_draws=0`) | `shadow_volume_draws`, `shadow_box_counts`, `shadow_box_composites` on the box-on replay at the phone setting (OpenGL: its one setting); every target byte for byte; equal draws and passes with volumes, with none (an empty box) fewer or equal, fewer where the count was boxed |
| `rd_date_one_ab`, `rd_empty_skip_ab`, `rd_no_volume_break_ab` and their `_gl` twins | `lists/shadow_cuts.txt` | `fx-shadow-wide`, `fx-opening-dark`, `fx-opening-sun`, `fx-mali-frame`, `fx-mali-slow541` (shadow windows with volumes: `shadow_volume_draws>=1`, `date_snapshots>=1`, `date_stencil_draws>=1`, `date_one_shared>=1`, `empty_composites_skipped=0`, `no_volume_break_taken=0`), `fx-lamps-novolume` (no volume: `shadow_volume_draws=0`, `date_one_shared>=1`, `empty_composites_skipped>=1`, `no_volume_break_taken>=1`) | on the on run the item's quantity (`date_one_shared`, `empty_composites_skipped`, or `no_volume_break_taken` where DATE takes the stencil bit) with `shadow_volume_draws`; on the off run `date_snapshots` or `date_stencil_draws` by the setting's DATE path where declared; every target byte for byte and the counts the code leaves out |
| `rd_no_volume_ab`, `rd_no_volume_ab_gl` | `lists/no_volume.txt` | `fx-boot-0400`, `fx-boot-0600`, `fx-lamps-novolume` (`shadow_volume_draws=0`, `no_volume_kept>=1`), `fx-boot-0200`, `fx-shadow-wide` (`shadow_volume_draws>=1`, `no_volume_kept=0`) | `no_volume_kept` and `shadow_volume_draws` on the `=1` run where DATE takes the stencil bit, `shadow_volume_draws` elsewhere; the declared `no_volume_kept` is the windows the test expects taken |
| `rd_stencil_ab` | `lists/stencil.txt` | `fx-shadow-wide`, `fx-opening-dark`, `fx-opening-sun`, `fx-mali-frame` (`shadow_volume_draws>=1`, `stencil_window_stores>=1`) | `shadow_volume_draws` and `stencil_window_stores` on the `=0` run at both settings |
| `rd_date_cut_ab`, `rd_date_cut_ab_gl` | `lists/date_cut.txt` (a fixture, the passes the cut takes on it, `narrow` for a frame drawn narrower than 21:9) | `fx-datecut-6100`, `fx-datecut-slow137`, `fx-mali-17008` (the Mali-G52's frames, the shadow's three composites under DATE: `date_snapshots>=1`, `date_cut_passes>=1`), `fx-mali-frame`, `fx-shadow-wide` (`date_snapshots>=1`; narrow, so not at the Mali's setting nor on OpenGL) | `date_snapshots` on the on run of every pair and `date_cut_passes` where the cut acts (not at the phone's fractional scale); the list's passes (2: the second and third composite) pinned on every fixture; every target byte for byte |
| `rd_date_subpass_ab` | `lists/date_subpass.txt` | `fx-mali-17008`, `fx-mali-frame`, `fx-shadow-wide` (`date_stencil_draws>=1`, `date_snapshots>=1`) | `date_stencil_draws` on the stencil run (DATE through the bit by the subpass read), `date_snapshots` on the snapshot run; every target byte for byte |
| `rd_subpass_ab` | `lists/subpass.txt` (a fixture, its profiles, `nodate` for a window with no volume) | `fx-mali-slow541`, `fx-mali-frame` (both profiles), `fx-opening-dark`, `fx-opening-sun` (the Pixel's) (`date_stencil_draws>=1`, `date_snapshots>=1`), `fx-lamps-novolume` (`nodate`: `shadow_volume_draws=0`) | `date_stencil_draws` on the on run and `date_snapshots` on the off run; the `nodate` fixture draws no DATE either way and its `shadow_volume_draws` is checked; each profile has a fixture drawing DATE through the read; every target byte for byte |
| `rd_shadow_fine_ab`, `rd_shadow_fine_ab_gl` | `lists/shadow_fine.txt` (a fixture, its composites' `--nop` spec, its settings), `lists/shadow_fine_goldens.txt` (the release before's pictures of the fixtures: a fixture, a setting, a PNG, its SHA-256) | `fx-adreno`, `fx-adreno-prev`, `fx-adreno-blended`, `fx-gate`, `fx-gate-prev`, `fx-gate-blended`, `fx-gate2`, `fx-gate2-prev`, `fx-gate2-blended` (the main gate above 1x, one shadow window with volumes: `shadow_levels>=1`, `shadow_level_texels>=1`) | `shadow_levels` and `shadow_level_texels` on the default run of every pair above 1x and on the forced run at 1x; every golden of the list matched by the `ICO_RD_SHADOW_FINE=0` run; the pairs' own checks |
