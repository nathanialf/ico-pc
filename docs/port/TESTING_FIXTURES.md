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

### The placement rules (opt-in)

A room's lamps are pieces of the room's model, whose origin is off the picture, so a lamp generated
about that origin is dropped by the visibility test and never lights the glow; and a generated sun
fan is always on the picture, so a frame whose sun was off it never has the glow's dark plan. A
test of the glow's light chooses otherwise with two lines in its fixture's rule file. A rule file
without them makes the fixture it always made, and every fixture made before they existed is the
same bytes.

- `@lamps place`: every mesh that a non-skinned draw puts into WORK2 (the glow's lamps) is
  generated about a point that lands on WORK2. The rule reads only that draw: its placement matrix
  M (VU memory 16..19), the scissor and the target's GS size in the state at the draw, and the
  mesh's number n (0, 1, ...) among the meshes so drawn. The place is the scissor's box cut in four
  each way, the meshes on its three inner columns and rows in turn: X = 2048 - gsW / 2 + x0 +
  (x1 - x0 + 1) (1 + n mod 3) / 4, Y the same down with (n / 3) mod 3. The centre is the
  model-space point M sends to (X, Y) at the w of the model's origin (the object keeps its
  distance from the eye and moves across the picture; |w| for an origin behind the eye; for a
  matrix without perspective the point of that line nearest the origin). The cube's size is found
  about the centre as it is about an origin, and the centre is rounded to the cube's lattice step
  (at most half a step, a sixteenth of the half side), so every vertex is still an exact float. No
  vertex and no bound of the model is read. A mesh the rule cannot place (w 0, a singular matrix, a
  centre behind the eye or past 2^23 steps) keeps its origin and is counted in the maker's line.
- `@fans off`: every CPU-projected prim (not a rectangle, not text) drawn into WORK2 (the sun's
  fans) is generated as the same ribbon 64 GS pixels wide, 8 pixels past the target's right edge:
  the fans of a sun that is off the picture.

A rule that matches no draw fails the make. The fixture keeps the result at the end of its
skeleton (the placed meshes' ids and centres in lattice steps, the moved prims' payload offsets):
`rd_fixture_tool info` says `N meshes placed on WORK2, N prims beside it`, the maker's line
`placement: N meshes drawn into WORK2 generated on it (K kept their origin), N prims drawn into
WORK2 generated beside it`.

## The format

`port/render/rd_fixture.h` is the reference. In short: the head (`ICORDFX1`, the format, the
expanded dump's version, the expansion's SHA-256 and size), the notes (`key=value`: `source=`,
`maker=`, `rule=`, `selfcheck=`, `expect=`), and the deflated skeleton: the dump's own layout with
every texture's texels replaced by its pattern name, every mesh's name and stream by the generator's
name, its size exponent and bone count, the facing tables by their sizes, and at the end the size
exponent of each grid and particle draw (then, only in a fixture made with a placement rule, the
placement section). A fixture is made from a dump of any version the loader
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
| `volume_fs_taken` | `volume fs on (...): N volume draws by date_clear_ps` (the on word alone) |
| `comp_spec_seen`, `comp_spec_taken` | `M composites and levels by sprite_comp_ps of N` (N seen), `comp spec on (...): M composites and levels` (M taken, the on word alone) |
| `shadow_tile_composites` | `shadow tiles on: N composites over the tile mask` |
| `shadow_merge_counts` | `shadow volumes in the scene's pass on: N of M counts` |
| `paste_spec_seen`, `paste_spec_taken` | `M pastes by fx_paste_ps of N` (N seen), `paste spec on (...): M pastes by fx_paste_ps` (M taken, the on word alone) |
| `lamps_own_depth_draws` | `lamps lowres on: WORK2 ... under SCENE ..., N draws into WORK2 on its own depth` (the on word alone) |
| `glow_fold_folded` | `glow: WORK2 fold on: N folded` (the on word alone) |
| `lamps_fillpaste_fills` | `lamps fillpaste on: N crack fills drawn in the paste's pass` |
| `present_scene_skipped` | `present from SCENE on: N reductions not drawn` |
| `lamps_area_passes` | `lamps ... (self-test ...): N passes with the lamps' area` |
| `lamps_bands_lazy` | `N bands left lazy` |
| `lamps_owed` | `N WORK0 alpha sprites owed` |
| `crack_msaa_fills` | `crack fill on: ... N fills after the MSAA resolve` |
| `pass_area_passes`, `pass_area_covered` | `pass area on: N passes with an area`, `pass area on: ... (..., M its draws cover the picture)` (the on word alone) |
| `clear_carried`, `clear_skipped`, `last_store_dropped` | `clear carry on: N clears taken by the next pass`, `clear skip on: N clears skipped`, `last store on: N depth stores dropped at the last touch` (each its on word alone) |
| `aura_box_reads` | `aura box on: N feedback reads in two passes` |
| `vu_compact_left_out` | `VU index compaction on: N of M list 0 triangles left out` (the on word alone) |
| `prepass_draws` | `VU pre-pass on: P passes, V vertices in N draws` |
| `vu_merged_draws` | `VU merge: D draws in G GPU draws (N merged into M` (the draws the merge's runs joined) |
| `store_depth_dropped` | `stores: N depth and M colour stores dropped` |
| `constant_reads_served` | `constant reads on: N sprites drawn in a cleared source's colour` |
| `vu_record_dispatches` | `C copies, N dispatches` (the first stats line: the record by compute dispatches) |
| `side_band_card_draws` | `side bands: N draws with cards` (under `ICO_RD_REGION_RULE=ps2`: the draws the side bands' mode holds that carry card verdicts; 0 where the frame is not wider than 4:3) |
| `fog_path` | `fog local draws N (fog path P)` (the word: the fog's depth path in force) |
| `aura_pastes_whole` | `aura paste on: S split (...), N drawn whole` |
| `interp_keyed_blended` | `K keyed draws: N blended` (the `--interp` run's first line) |
| `interp_tick_cam_draws` | `...beside blended ones; N VU draws with the ticks' cameras` (the `--interp` run's) |
| `water_blocks_kept`, `water_targets_clamped` | the water line (`--list`) with Full pixel on: `N render-target reads clamped, M block reads kept on REPEAT` |
| `aura_paste_split` | `aura paste on: N split` |
| `noplanes_fallback` | `sprite no-planes N` (the commands through the plain sprite builds) |
| `prerotate_turned` | the log's `turned twin: the WxH picture turned N degrees` |
| `turn_fold_cases`, `turn_fold_turned` | the log's `turn in the blit: self-test passed (N cases`, `picture's box blit drawn turned N degrees` |
| `bench_variants` | `bench: done; every variant ran: N of M` |

The fixtures' declared properties are their manifest line's; `rd_fixtures_check` holds the two
equal.

### A list's own properties

A property belongs on the fixture when it holds wherever the fixture is replayed. What one test's
settings make of a fixture another family owns is declared on that test's list line instead, as the
same `QUANTITY OP VALUE` words after the name: `rd_fixture_list_props(NAME)`
(`port/render/test/rd_fixture_list_props.cmake`) joins them to the fixture's own before
`rd_fixture_require`, a line's word replacing the fixture's for the same quantity. So a later
family never rewrites a fixture file an earlier one pinned. A line may also carry the test's own
switch for that fixture, read with `rd_fixture_list_word`: the glow's lists use `nocull`, which
replays the fixture with `ICO_RD_CULL=off` in every run. The generated meshes sit about their
models' origins, so the visibility test drops the lamps' meshes the game's frame kept, and a frame
whose lamps are dropped takes the glow's dark plan; with the test off the lamps are drawn into
WORK2 and the frame takes the lamps plan (mask not drawn, the chain drawn). Their cubes do not
reach WORK2's picture, so a fixture's lamps carry no light of their own: the light in the glow's
fixtures is the sun's fans', a white ribbon over the middle of the frame.

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
| `rd_date_cut_ab`, `rd_date_cut_ab_gl` | `lists/date_cut.txt` (a fixture, the passes the cut takes on it, `narrow` for a frame drawn narrower than 21:9) | `fx-datecut-6100`, `fx-datecut-slow137`, `fx-mali-17008` (the Mali-G52's frames, the shadow's three composites under DATE: `date_snapshots>=1`, `date_cut_passes>=1`), `fx-mali-frame`, `fx-shadow-wide` (`date_snapshots>=1`; narrow, so not at the Mali's setting nor on OpenGL) | `date_snapshots` on the on run of every pair and `date_cut_passes` where the cut acts (not at the phone's fractional scale); the list's passes (2: the second and third composite) pinned on every fixture; every target byte for byte |
| `rd_date_subpass_ab` | `lists/date_subpass.txt` | `fx-mali-17008`, `fx-mali-frame`, `fx-shadow-wide` (`date_stencil_draws>=1`, `date_snapshots>=1`) | `date_stencil_draws` on the stencil run (DATE through the bit by the subpass read), `date_snapshots` on the snapshot run; every target byte for byte |
| `rd_subpass_ab` | `lists/subpass.txt` (a fixture, its profiles, `nodate` for a window with no volume) | `fx-mali-slow541`, `fx-mali-frame` (both profiles), `fx-opening-dark`, `fx-opening-sun` (the Pixel's) (`date_stencil_draws>=1`, `date_snapshots>=1`), `fx-lamps-novolume` (`nodate`: `shadow_volume_draws=0`) | `date_stencil_draws` on the on run and `date_snapshots` on the off run; the `nodate` fixture draws no DATE either way and its `shadow_volume_draws` is checked; each profile has a fixture drawing DATE through the read; every target byte for byte |
| `rd_shadow_fine_ab`, `rd_shadow_fine_ab_gl` | `lists/shadow_fine.txt` (a fixture, its composites' `--nop` spec, its settings), `lists/shadow_fine_goldens.txt` (the release before's pictures of the fixtures: a fixture, a setting, a PNG, its SHA-256) | `fx-adreno`, `fx-adreno-prev`, `fx-adreno-blended`, `fx-gate`, `fx-gate-prev`, `fx-gate-blended`, `fx-gate2`, `fx-gate2-prev`, `fx-gate2-blended` (the main gate above 1x, one shadow window with volumes: `shadow_levels>=1`, `shadow_level_texels>=1`) | `shadow_levels` and `shadow_level_texels` on the default run of every pair above 1x and on the forced run at 1x; every golden of the list matched by the `ICO_RD_SHADOW_FINE=0` run; the pairs' own checks |
| `rd_stencil_ab` | `lists/stencil.txt` | `fx-shadow-wide`, `fx-opening-dark`, `fx-opening-sun`, `fx-mali-frame`, `fx-maingate`, `fx-phone-4657` (`shadow_volume_draws>=1`, `stencil_window_stores>=1`) | `shadow_volume_draws` and `stencil_window_stores` on the `=0` run at both settings |
| `rd_volume_fs_ab`, `rd_volume_fs_sabotage` | `lists/volume_fs.txt` | `fx-shadow-wide`, `fx-opening-sun`, `fx-mali-frame`, `fx-maingate` (`shadow_volume_draws>=1`; `fx-maingate` also `volume_fs_taken>=1`), `fx-lamps-novolume` (`shadow_volume_draws=0`) | `shadow_volume_draws` and `volume_fs_taken` on the `=1` run at the phone, desktop and Mali settings (every volume draw the run shows is by the entry there); the sabotage twin `shadow_volume_draws` |
| `rd_comp_spec_ab`, `rd_comp_spec_sabotage` | `lists/comp_spec.txt` | the same five (`fx-maingate` also `comp_spec_seen>=1`, `comp_spec_taken>=1`) | `shadow_volume_draws`, `comp_spec_seen` and `comp_spec_taken` on the `=1` run at the phone setting (a fixture whose run shows volume draws has a draw by the entry there); the sabotage twin without `comp_spec_taken` |
| `rd_shadow_tile_ab` | `lists/shadow_tile.txt` (the composites over the mask and the live-texel bounds a fixture) | `fx-shadow-wide`, `fx-tile-2809`, `fx-maingate`, `fx-phone-4657` (`shadow_volume_draws>=1`; the last three `shadow_tile_composites>=1`) | `shadow_volume_draws` and `shadow_tile_composites` on the `=1` run at the phone setting; the list's count (3) and bounds |
| `rd_shadow_merge_ab` | `lists/shadow_merge.txt` | `fx-phone-4657`, `fx-maingate`, `fx-tile-2809` (`shadow_volume_draws>=1`, `shadow_merge_counts>=1`) | `shadow_volume_draws` and `shadow_merge_counts` on the `=1` run at both settings |
| `rd_glow_light_ab` | `lists/glow_light.txt` | `fx-opening-sun`, `fx-gate-102` (the sun's fans: `glow_plan=whole`), `fx-opening-dark`, the PowerVR phone's blended frame of 2026-11-04, `fx-wide-189`, `fx-boot-0400` (`glow_plan=dark`), `fx-lamps-novolume`, `fx-163-blended` (the cull off, the list's `nocull`: `glow_plan=lamps`) | `glow_plan` on the plan's run at every setting; a dark, a lamps and a whole run at each of the five pinned settings; RGB the same against the chain drawn whole |
| `rd_glow_blur_ab`, `rd_glow_blur_gl` | `lists/glow_blur.txt` | `fx-gate-102`, `fx-boot-0200` (the fans' chain; not `fx-opening-sun`, whose generated fans saturate five times the texels its source does, past bounds measured on the game's frames), `fx-163-blended`, `fx-boot-0400` (the cull off: the lamps' chain) (`glow_chains>=1`), `fx-opening-dark`, `fx-lamps-novolume`, the PowerVR phone's blended frame of 2026-11-04, `fx-wide-189` (`glow_chains=0`) | `glow_chains` on the glowblur arm at every measured setting the fixture runs at; VD-33's bounds, the passes, the shimmer |
| `rd_paste_spec_ab`, `rd_paste_spec_gl`, `rd_paste_spec_sabotage` | `lists/paste_spec.txt` | `fx-opening-sun`, `fx-gate-102`, `fx-boot-0200`, `fx-lamps-novolume` and `fx-163-blended` (the cull off) (`paste_spec_seen>=1`, `paste_spec_taken>=1`), the PowerVR phone's blended frame of 2026-11-04 (`paste_spec_seen>=1`), `fx-opening-dark`, `fx-wide-189` (`paste_spec_seen=0`) | `paste_spec_seen` and `paste_spec_taken` on arm 1 at the phone's setting (the OpenGL and sabotage twins `paste_spec_seen`); every target byte for byte |
| `rd_lamps_lowres_ab` | `lists/lamps_lowres.txt` | `fx-opening-sun`, `fx-boot-0200` (the fans at the GS size), `fx-163-blended` (the cull off, a lamps frame) (`lamps_own_depth_draws>=1`) | `lamps_own_depth_draws` on the lowres arm at every measured setting; VD-32's bounds; not in a merge check (thirty settings a fixture) |
| `rd_glow_cheap_ab` | `lists/glow_cheap.txt` | `fx-gate-102`, `fx-issue91-1279`, `fx-issue91-1358` (the fans), `fx-163-blended`, `fx-wide-189` (no fans) (`glow_passes>=1`, `glow_fold_folded>=1`) | `glow_passes` on the run at the defaults and `glow_fold_folded` on the fold's run at every setting; every target byte for byte against the run with the clear carry off, two glow passes fewer than the count run at the phone's setting (three without the fans) |
| `rd_lamps_fillpaste_ab` | `lists/lamps.txt` (the word `nocull` after a name: `ICO_RD_CULL=off` in every arm, the generated lamps kept in the glow's chain) | `fx-lamps-novolume`, `fx-opening-dark`, `fx-boot-0400` (each `nocull`, `lamps_fillpaste_fills>=1`), `fx-lamps-title`, `fx-lamps-opening` (the cull on, `lamps_fillpaste_fills>=1`), `fx-opening-sun`, `fx-boot-0200`, `fx-boot-0600` (`lamps_fillpaste_fills=0`: the chain drawn whole) | `lamps_fillpaste_fills` on the `+fillpaste` run at the phone setting |
| `rd_present_scene_ab` | `lists/present_scene.txt` | `fx-shadow-wide`, `fx-phone-4657`, `fx-tile-2809` (`present_scene_skipped>=1`), `fx-maingate` (`present_scene_skipped=0`: kept for the open border's repeated columns) | `present_scene_skipped` on the `=1` run at the phone setting |
| `rd_lamps_ab`, `rd_lamps_gl` | `lists/lamps_ab.txt` (the words `seq=whole`, `seq=lamps`, `seq=dark`: the sequences' frames) | `fx-lamps-title`, `fx-lamps-opening` (the placement rules; `glow_plan=lamps`, `lamps_area_passes>=1`, `lamps_bands_lazy>=1`, `lamps_owed=0`), `fx-lamps-title-dark`, `fx-opening-dark` (`glow_plan=dark`, `lamps_owed>=1`, the others 0), `fx-boot-0200`, `fx-opening-sun` (`glow_plan=whole`, `lamps_bands_lazy>=1`, the others 0) | `glow_plan`, `lamps_bands_lazy`, `lamps_owed` on the run with the parts on at every setting; `lamps_area_passes` at the phone, poison and desktop settings and in both sequences |
| `rd_crack_work_ab`, `rd_crack_work_gl` | `lists/crack_work.txt` (after a name `swapN`, `tapN`: the swaps and early-leave draws at the phone's setting) | `fx-phone-4761` (the PowerVR phone's bridge, the fill in the aura's paste pass), `fx-phone-7583` (its cage under the dark plan, the fill held), `fx-163-blended`, `fx-lightning-0300`, `fx-title-0300` (`crack_pixels>=1`) | `crack_pixels` on the on run of every pair; a swap or an early leave on every fixture over its settings; the list's `swapN`, `tapN`; every target byte for byte |
| `rd_fill11_ab`, `rd_fill11_gl` | `lists/fill11.txt` (after a name `phoneN`, `deskN`, `glN`: the fills drawn in list 11's pass; `nop SPEC`) | `fx-boot-0200`, `fx-title-0300`, `fx-title-dark-0300` (the menu with the sun off the picture, `fans_off.rules.txt`: the fill drawn in list 11's pass at 4:3 with its motion blur and depth-tested text NOPed), `fx-phone-4761`, `fx-phone-7583` (`crack_pixels>=1`) | `crack_pixels` on the on run of every pair; the switch deciding every fixture's fill on at least one on run; the list's counts at each setting; every target byte for byte |
| `rd_late_pass_ab`, `rd_late_pass_gl` | `lists/late_pass.txt` (after a name `heldN`, `joinN`, or `none`) | `fx-phone-7583`, `fx-163-blended` (a fill held), `fx-phone-4761` (`none`) (`crack_pixels>=1`), `fx-gate-102` (list 11 joined; its generated geometry leaves no crack: `crack_pixels=0` on its line) | `crack_pixels` on the on run of every pair; at the phone's setting the late pass acting on every fixture but a `none` one, which it must leave alone; the list's counts; every target byte for byte or within VD-31's bound where list 11 joined |
| `rd_crack_resolved_msaa2`, `rd_crack_resolved_msaa4` | (one fixture, `port/render/CMakeLists.txt`) | `fx-lightning-0300` (the corpus's lightning room: its MSAA epoch closes before the fog; `crack_pixels>=1`, `crack_msaa_fills>=1`) | `crack_pixels`, `crack_msaa_fills` at Enhanced 2x with MSAA |
| `rd_crack_late_ab` | `lists/crack_late.txt` (a fixture and its settings), `lists/crack_late_goldens.txt` (the release before's pictures of the fixtures: a fixture, a setting, a PNG, its SHA-256) | `fx-boot-0200`, `fx-lightning-0300` (the gate's Enhanced 2x 4:3), `fx-phone-4657`, `fx-phone-4761` (the PowerVR phone's Resolution Auto) (`crack_pixels>=1`) | `crack_pixels` on every run; every PNG its golden; the fill at the start of list 7 the same bytes |
| `rd_pass_area_ab` | `lists/pass_area.txt` (the word `phone` marks the PowerVR phone's frames) | `fx-phone-4657`, `fx-phone-15677`, `fx-phone-2381` (`phone`), `fx-163-blended` (`pass_area_passes=0`, `pass_area_covered>=1`: the aura's paste and list 11's fades draw the frame's rect; no fixture, and no source measured, takes an area at these settings) | `pass_area_passes` and `pass_area_covered` on the on run at every setting the fixture shows; every target byte for byte; the phone's frames an area or `its draws cover the picture` |
| `rd_pass_cut_ab` | `lists/pass_cut.txt` (the word `phone` marks the PowerVR phone's frames) | `fx-corpus-lightning`, `fx-corpus-plain`, `fx-corpus-puddle`, `fx-corpus-queen` (`clear_carried>=1`, the others 0), `fx-gate-102` (`clear_carried>=1`, `last_store_dropped>=1`), `fx-phone-4657`, `fx-phone-15677`, `fx-phone-2381` (`phone`; all three `>=1`) | `clear_carried`, `clear_skipped` on the all-on run, `last_store_dropped` there or, on a phone frame at the phone's setting, on the run with the crack fill off; every target byte for byte; each cut cutting something over the phone's frames |
| `rd_aura_box_ab` | `lists/aura_box.txt` | `fx-phone-4657`, `fx-phone-15677`, `fx-phone-2381`, the PowerVR phone's frame of 2026-11-04, `fx-163-blended`, `fx-mali-slow541` (`aura_box_reads>=1`) | `aura_box_reads` on the two-pass run at every setting the fixture shows; every target byte for byte; one pass and one draw more a read |
| `rd_pass_objects_ab` | `lists/pass_objects.txt` | `fx-163-blended`, `fx-lamps-novolume`, `fx-opening-dark`, `fx-opening-sun`, `fx-mali-slow541`, `fx-mali-frame` (`passes>=1` on the line) | `passes` on the run on Qualcomm's row (each a `VkRenderPass` object there) with its device line; every target byte for byte, the same draws and passes |
| `rd_vu_flat0_ab`, `rd_vu_flat0_gl`, `rd_vu_compact_ab` | `lists/vu_flat0.txt` (a fixture, its profiles, the properties both tests check), `lists/vu_compact_counts.txt` (each pair's counts: a fixture, a profile, a setting, the merge, then N M D O) | `fx-gate-102` (the corpus profile), `fx-mali-frame`, `fx-mali-slow541` (the Mali-G52's and the corpus profile), `fx-lamps-novolume`, `fx-opening-dark`, `fx-opening-sun` (the Pixel's) (`prepass_vertices>=1`, `vu_compact_left_out>=1`, `region_rule_draws>=1`; `fx-gate-102`, dumped before the facing tables, `region_rule_draws=0`) | `prepass_vertices` on every `ICO_RD_VU_FLAT0=1` run compared; `vu_compact_left_out` and `region_rule_draws` on every `ICO_RD_VU_COMPACT=1` run compared, and every pair's counts equal to its line (a pair without a line, or a line no pair took, fails); a fixture with no pair compared under one of its profiles fails; every target byte for byte |
| `rd_vustage_ab`, `rd_vubatch_ab`, `rd_vumerge_ab`, `rd_vumerge_ab_uber` | `lists/vustage.txt` | `fx-vu-3086`, `fx-vu-3112`, `fx-vu-1985` (the 2026-10-07 captures), `fx-loss95` (issue 95's loss frame), `fx-deja` (the deja vu 48:9 frame): each made with `vu_opaque.rules.txt` (a VU draw's texture `opaque`: the alpha plan decides an alpha-tested VU draw in one pass, which the merge and the fused pair need), `prepass_vertices>=1`, `prepass_draws>=1`, `vu_merged_draws>=1` | `prepass_vertices` on the record's run (`rd_vustage_ab`); `prepass_vertices`, `prepass_draws` on the batched run (`rd_vubatch_ab`); `vu_merged_draws` on the merged run through the record (`rd_vumerge_ab`, `_uber`) |
| `rd_vufuse_ab`, `rd_vufuse_ab_uber` | `lists/vufuse.txt` (`extra`: a phone frame, fused at both phone settings) | `fx-loss95`, `fx-deja`, `fx-phone-4103`, `fx-mobile-164` (`vu_fuse_pairs>=1`) | `vu_fuse_pairs` on every fused run |
| `rd_vumerge_mali` | `lists/vumerge_mali.txt` (a fixture, `expect=IN/GPU` from a run of it, the tool's arguments) | `fx-mobile-164` (issue 164's Mali-G52 frame), `fx-phone-2199`, `fx-phone-4103`, `fx-vu-3112` (`vu_merged_draws>=1`) | `vu_merged_draws` on the plain run (the cull off); the list's figure on both runs |
| `rd_store_plan_ab` | `lists/store_plan.txt` | `fx-vu-3086`, `fx-vu-3112`, `fx-vu-1985`, `fx-deja`, `fx-mobile-164`, `fx-vu-plain-0300` (`store_depth_dropped>=1`) | `store_depth_dropped` on the planned run at every setting the fixture shows |
| `rd_thread_ab` | `lists/thread.txt` (`prev=NAME`: the tick before's fixture, the blended present) | `lists/vustage.txt`'s, `fx-phone-4103`, `fx-phone-2199`, `fx-gate` with `fx-gate-prev` (`prepass_vertices>=1`) | `prepass_vertices` on the render thread's `on` run at both settings |
| `rd_constant_reads_ab` | `lists/constant_reads.txt` (`extra`: a phone frame) | `fx-phone-4103`, `fx-phone-2199`, `fx-phone-4657`, `fx-mobile-164`, `fx-deja` (the aura with no aura object: `constant_reads_served>=1`) | `constant_reads_served` on every default run |
| `rd_vurecord_ab`, `_compute`, `_compute_class`, `rd_vuprepass_min_ab`, `rd_vu_rec_early_ab`, `_compute`, `_targets` | `lists/vustage.txt` | as above, and `vu_record_dispatches>=1` | `prepass_vertices` on both runs (`rd_vuprepass_min_ab`: the run under every frame), `vu_record_dispatches` on a run with the record by compute dispatches (`-DFX_A`, `-DFX_B`) |
| `rd_scope_ab`, `rd_scope_ab_tiler` | `lists/scope.txt` | the three captures, `fx-loss95`, `fx-scope-179-lost`, `fx-scope-179-before` (issue 179's Mali-G52 loss frames at the first 3D scene) (`prepass_vertices>=1`) | `prepass_vertices` on both runs |
| `rd_vu_rec_size_ab`, `_mali` | `lists/vu_rec_size.txt` | `fx-mali-slow541`, `fx-mali-frame`, `fx-lamps-novolume`, `fx-opening-dark`, `fx-opening-sun`, `fx-vu-plain-0300` (the corpus's largest pre-pass record) (`prepass_vertices>=1`, `vu_record_dispatches>=1`) | `prepass_vertices` on both runs, `vu_record_dispatches` on `_mali`'s |
| `rd_vurecord_ab_compute_class_opening` | `lists/vu_opening.txt` | `fx-lamps-novolume`, `fx-opening-dark`, `fx-opening-sun` | `prepass_vertices` on both runs, `vu_record_dispatches` on the compute run |
| `rd_sideband_ab`, `rd_region_camera_ab` | `lists/wide.txt` (after a name the frame's aspect, the replays' `--aspect`) | `fx-wide-6818` (the PowerVR phone's one 4:3 frame: the control; `side_band_card_draws=0`), `fx-phone-4657` (2.189), `fx-wide-189` (16:10), `fx-mali-frame` (2.37), `fx-maingate` (48:9) (`side_band_card_draws>=1`; all `region_rule_draws>=1`) | `side_band_card_draws` on the side bands' replay at both settings (`rd_sideband_ab`), `region_rule_draws` on the camera rule's replay at both settings (`rd_region_camera_ab`): the modes in force. The generated geometry drops no triangle either rule decides (`--region-stats`: drop 0, beside 0, drawn by the camera rule 0 on every committed fixture), so a drop drawn beside the picture or by the camera rule is not exercised; on the game's frames the old list had none for the side bands and three cards (issue 189) for the camera rule |
| `rd_replay_tool_still` | `lists/replay_still.txt` | `fx-boot-0200` (`draws>=1` on the line) | `draws` on the run with the re-show on; presents 1 to 3 shown again, every picture byte for byte |
| `rd_replay_tool_glow_hold` | `lists/glow_hold.txt` (a role a fixture: `still`, `prev`, `move`; `nocull`: `ICO_RD_CULL=off` for that case) | `fx-boot-0200` (`still`: `glow_chains>=1`, `glow_plan=whole`), `fx-adreno-prev`, `fx-adreno` (`prev`, `move`, `nocull`: `glow_chains>=1`, `glow_plan=lamps`) | `glow_chains` and `glow_plan` on each fixture replayed alone at its case's setting; the held and redrawn lines, the pictures as before |
| `rd_replay_tool_save_blended`, `rd_replay_tool_save_blended_163` | `lists/save_blended.txt`, `lists/save_blended_163.txt` (a role a fixture: `prev`, `cur`) | `fx-gate-prev`, `fx-gate` (`interp_keyed_blended>=1`); `fx-adreno-prev`, `fx-adreno` (`interp_keyed_blended>=1`, `interp_tick_cam_draws>=1`) | on the `--interp` run of SCENE; the saved frame replayed alone, and on the render thread, byte for byte |
| `rd_fog_depth_copy_dump`, `_d24s8` | `lists/fog_depth_copy.txt` | `fx-adreno` (`draws>=1`, `aura_pastes_whole>=1`) | `fog_path` on every run (the path asked for), `draws` and `aura_pastes_whole` on the copy's run; a run with the copy's reads sabotaged changes SCENE; SCENE byte for byte in place, copied, through a buffer |
| `rd_replay_tool_ripple`, `_447`, `_436`, `_436_ps2`, `_436_1x`, `_436_1x_ps2`, `rd_replay_tool_mirror_floor` | the registration's `-DFIXTURE` (port/render/CMakeLists.txt) | `fx-ripple-13782`, `fx-ripple-84109`, `fx-ripple-83439` (issue 106's drained room: the puddle's block read by the ripples on REPEAT, `water_blocks_kept>=1`), `fx-puddle-0200` (the pool's grids over its scene copy, no block: `water_blocks_kept=0`, `water_targets_clamped>=1`) | the declared water quantities on the replay's water line; every block read on REPEAT kept; the block's scale in the log; the picture's PNG the fixture's own as the release before's tool drew it. The block's reflection does not reach a fixture's picture (the scene's scale and the GS size give one picture), so the picture pins the frame as a whole, not the water |
| `rd_fades_fold_ab` | `lists/fades_fold.txt` | `fx-phone-4657`, `fx-ripple-83439`, `fx-mobile-164`, `fx-vu-3086` (`fades_folded>=1`, the line's) | `fades_folded` on every folded run; VD-27's rule on every target |
| `rd_aura_paste_ab` | `lists/aura_paste.txt` | `fx-phone-4657`, `fx-shadow-wide`, `fx-mobile-164`, `fx-163-blended` (`aura_paste_split=1`), `fx-vu-3086` (`aura_paste_split=0`: the paste drawn whole) | `aura_paste_split` on the on run at the phone's setting; every PNG byte for byte, the passes and traffic equal |
| `rd_noplanes_ab` | `lists/noplanes.txt` (`expect=`, then the tool's arguments) | `fx-title-0200`, `fx-title-0350`, `fx-boot-0600` (`noplanes_fallback=2`), `fx-vu-3112` (`noplanes_fallback=0`) | `noplanes_fallback` on the run without the builds; every target byte for byte |
| `rd_replay_tool_prerotate` | `lists/prerotate.txt` | `fx-phone-4657`, `fx-boot-0200` (`prerotate_turned>=90`) | `prerotate_turned` on every turned run; each turned present the identity's bytes |
| `rd_turn_fold_ab` | `lists/turn_fold.txt` (`win43`: also at 1024x768) | `fx-lamps-novolume`, `fx-opening-dark`, `fx-opening-sun`, `fx-mali-slow541`, `fx-mali-frame` (cropped: the turn pass kept, `turn_fold_cases=6`), `fx-vu-3086` (`win43`, `turn_fold_turned>=90`) | `turn_fold_cases` on every fold run, `turn_fold_turned` where it folded; every PNG byte for byte |
| `rd_bench_ab` | `lists/bench.txt` (the variant lines after a bar) | `fx-lamps-novolume`, `fx-opening-sun` (`bench_variants`) | `bench_variants` on the bench run; every variant ran, the picture after the bench the one before |
