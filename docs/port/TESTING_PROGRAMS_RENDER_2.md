# Test programs: port/render/test, continued

The renderer's test programs, one entry each, continued from
[TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md) (the repo keeps every file under 256 KiB):
the store plan, the pass cuts, the late pass, the lamps, the glow and shadow families and the pacing
tests, from `rd_store_plan_test.c` to `rd_glow_light_ab.cmake`. The entries read as that page's do
([Reading the entries](TESTING_PROGRAMS.md#reading-the-entries)).

- `rd_store_plan_test.c` (`rd_store_plan`, lavapipe, 77 without a device): the store plan's keep
  (`rd_store_plan.c`, [WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)) at Enhanced 16:9 2x: after
  the start's self-tests (`rd_fog_selftest`: DATE, the fx local read, the shadow count, the fog) and
  again at a new scene size (`rd_reset_scene`), SCENE's depth is not kept (the shadow count's frame
  reads it with no clear of its own and once kept it for the whole run); a new SCENE
  (`clearPending`) read in list 3 before any clear is not kept, and the next frame (list 0's clear
  and a Z-tested draw, then list 7 on WORK3 and on SCENE without touching the depth) drops at least
  one depth store; a frame reading SCENE's depth with no clear keeps it, the cleared frame then drops
  none and its record says `storeKept` 1, SCENE, since that frame and the reason
  `RD_STORE_WHY_KEEP`; no validation error. Between those, the PowerVR phone's frame 5354 in shape
  (list 0's Z-tested draws, the fog's touch in a pass of its own, the aura's passes on WORK3, list
  11's two screen sprites on SCENE without a Z test or Z write) with [video] effects_depth on and no
  effects program, as on the phone: exactly one depth store dropped (list 11's SCENE pass), as a whole
  frame and as a keep frame; with `rd__force_effects_depth` (an effects program's stand-in) none, the
  record's reason `RD_STORE_WHY_EFFECTS`, and no keep left after it; replayed with its presents,
  every record `rd_perf_pop` returns holds the drops its replay counted and `rd_perf_backend_text`
  says `stores: 1 depth` (with the effects depth `stores: 0 depth` and `nothing dropped in N
  replays, N of them: kept for an effects program`). The drops' bytes are `rd_store_plan_ab`'s
  (next).
- `rd_pass_cut_test.c` (`rd_pass_cut`, lavapipe, 77 without a device): the passes cut
  (`rd_pass_cut.c`, [WORK_BUFFERS.md](WORK_BUFFERS.md#passes-cut)) at Enhanced 16:9 2x under
  `ICO_RD_STORE_POISON=1`, each forced on against off (`rd__pass_cut_force`): a clear of WORK3 then
  a sprite on it textured from WORK2 (drawn just before): the clear taken by WORK3's pass
  (`clearsCarried` 1, one walk pass fewer), WORK3 and WORK2 byte for byte; the clear skip's
  decision on a frame's scan (AURA_WORK cleared, WORK0's sprite reading it, the constant read's note
  for it): skipped, but not with a later read, a depth or the cut off; after a skip a read of
  AURA_WORK shows the clear's colour (its green fill gone), a later replay drawing its left half
  leaves the right half the clear's colour, a later replay clearing it first leaves that clear; the
  last store: list 0 clears SCENE and draws Z-tested, list 4 draws on WORK3, then on SCENE a sprite,
  the depth's last touch and another sprite: `storeLastDropped` 1 on, 0 off, one depth store more
  dropped, SCENE byte for byte; a copy or a draw on another target between: 1 (the pass begun at
  the touch), a DATE draw between: 0 with one last store counted as kept for a DATE draw
  (`lastStoreWhy`, its words `1 last stores kept (1 a DATE draw)`), none counted for the others; a
  draw textured from WORK3 between while WORK3 is not readable: 1 (the pass begun at that draw); the
  pass's first draw and one between textured from WORK3 (readable from the first on): 1, the pass
  begun at the first draw, and the guard's frame after it drops it again; post sprites
  (`cellPost`): the glow's `cleanUpFB` pair into SCENE (constant fold; the second Z GEQUAL at Z 1,
  the depth's last touch in a pass of its own) drops that pass's store (1 on, 0 off, no reason
  counted), the same pair as a motion blur's keeps it (0, one reason: a post sprite), the aura's
  DATE sprite (TEST 0x34000) after a Z GEQUAL screen sprite leaves that sprite the last touch (1,
  no reason), SCENE byte for byte in each; the pass area forced on:
  list 0's pass (it takes SCENE's clear) and list 4's (full-frame sprites, one the frame's height)
  counted as kept whole (`passAreaWhy`: `2 passes kept whole (1 a clear as its load, 1 its draws
  cover the picture)`), none with the area off, SCENE byte for byte; no validation error.
- `rd_pass_cut_ab.cmake`: the dumps of `tools/pass_cut_dumps.txt` (`tools/pass_area_dumps.txt`'s;
  machine-local, a missing one skipped) replayed with `--pass-dump --stats` under
  `ICO_RD_BLEND=fixed`, `ICO_RD_STORE_PLAN=1`, `ICO_RD_STORE_POISON=1` and `ICO_RD_FADES_FOLD=0`
  (list 11's sprites drawn, as the gate draws them: with the fold list 11's pass goes and the
  reduction's DISPLAY clear has no barrier left to be carried past) five times: every cut off
  (`ICO_RD_CLEAR_CARRY=0`, `ICO_RD_CLEAR_SKIP=0`, `ICO_RD_LAST_STORE=0`), each alone on, all on; at
  the phone's Resolution Auto with its paths (`ICO_RD_VU_PREPASS=0`, `ICO_RD_FOG_PATH=local`,
  `ICO_RD_DATE_PATH=stencil`; a narrower dump skips it) and at Enhanced 2x 4:3: every PNG
  (`PRESENT.png` and every named target) byte for byte against the off run, the off run cutting
  nothing, the same draws, the passes fewer by the clears carried and skipped, the depth stores
  dropped more by those at the last touch and fewer by at most one a clear carried or skipped (a
  cleared pass whose depth store the plan dropped goes with its carry: queen rd-00100 at Enhanced
  2x, 2 dropped with every cut off and 1 with its two clears carried); over the phone's frames at
  its setting each cut must cut something, the last touch counted on two more runs of those frames
  with `ICO_RD_CRACK_FILL=0` (every cut off, the last store alone on; compared byte for byte with
  each other), as the crack fill's read of SCENE's depth at list 7 keeps the fog's pass's depth
  store wherever it is drawn (`rd_pass_cut_ab`). SKIP as `rd_vustage_ab`.
- `rd_date_cut_test.c` (`rd_date_cut`, lavapipe, 77 without a device after its CPU cases): DATE's
  passes without the depth (`rd_date_cut.c`, `rd_date_cut.h`): the rule on made-up commands (the
  shadow's three composites then a mesh draw: two depth-free runs, the third undecided; a draw that
  tests the depth between two DATE draws; a DATE draw after no alpha write in the run; a post
  sprite, a copy, a VU draw elsewhere; no run without a depth target, a depth-using or AA1 draw;
  integer scales only, `rd_date_cut_scale_ok`);
  every screen-family key with an unused depth has its no-depth twin in the start-up set; a frame in
  the composites' shape and two variants (a Z GEQUAL draw after the third composite in its pass, one
  between the first two) (`ICO_RD_DATE_PATH=snapshot`, `ICO_RD_BLEND=fixed`,
  `ICO_RD_STORE_POISON=1`) with the cut off and on (`rd__date_cut_force`): SCENE byte for byte, the
  scan's runs by ordinal (`rd__date_cut_last_runs`: 2, 2 and 1), a pass without the depth a run,
  none begun again, the same passes, 3 snapshots (1 the first, 2 after an alpha write) and store-plan
  drops, the passes with a depth fewer by the runs, the depth and stencil bytes asked lower by 4 to 5
  bytes a texel each way of each cut pass, the 10 s text's `; DATE: 2.0 passes`; no validation error.
- `rd_aura_paste_test.c` (`rd_aura_paste`, CPU): the aura's paste split's row proof
  (`rd_aura_paste.c`): the kernels' taps (nearest, bilinear with a fraction of 0 and 8, the B-spline,
  the box), the GS's V truncated toward 0, the addressing (CLAMP, REPEAT, the backing clamp), and the
  chain on the Pixel frame's geometry: WORK0 rows 0-111 carried into FEED128 rows 0-111 (CLAMP; 1-111
  under REPEAT), the paste's rows [0, 1062) of 1080 (CLAMP both), [11, 1062) (the paste on REPEAT),
  [21, 1062) (command 47 on REPEAT), the measured band rows 1072-1079 inside the band.
- `rd_aura_paste_ab.cmake` (`rd_aura_paste_ab`): the dumps of `tools/aura_paste_dumps.txt`
  (machine-local; a listed dump that is absent FAILS) with `ICO_RD_AURA_PASTE=0` and `1`,
  `--pass-dump --stats`, `ICO_RD_VU_PREPASS=0`, at the phone's Resolution Auto and Enhanced 2x 4:3
  (both `ICO_VK_FAKE_LIMITS=moltenvk`) and a desktop 16:9 window: every PNG byte for byte, the same
  passes and attachment MB, the off run splitting nothing; the marked dump (the phone's cage frame) split once
  at the phone's setting with at most 32 band rows; at least one split over the list. No device:
  SKIP.
- `rd_late_pass_test.c` (`rd_late_pass`, CPU, record-only): the late passes' predicate
  (`rd_late_pass.c` `rd__late_pass_class`) on made-up commands: state, NOPs, overlay text, a clear
  or copy of another target, a sprite into another target (untextured, from another target, the
  glow's mask from SCENE in the alpha-only form) and a sprite on SCENE the light plan skips (or, in
  the scan, may skip) wait; the upscale point, a clear or copy of SCENE, SCENE's colour or depth view
  sampled otherwise, DATE, SCENE's depth written, a drawn sprite on SCENE, the reduction, the fog, a
  screen prim, a VU draw and a shadow command do not, each with its reason; the aura's paste lands.
  The store plan's scan (`rd__late_pass_scan`, `rd__late_pass_scan_end`) over a DARK room's order:
  the fill's read at the aura's paste held (the plan ignored, `RD_LATE_ANY_PLAN`), at the band with
  no DARK plan and switched off, at the walk's end with no paste, at a screen prim before the
  paste; `rd__fx_alpha_only`.
- `rd_crack_held` (`rd_crack_test 2 held`, `ICO_VK_FAKE_LIMITS=moltenvk`; registered by
  `rd_late_pass.cmake`): the glow's `cleanUpFB`, a sprite into WORK0 (the fill's snapshot there), the
  glow's mask (SCENE's alpha into WORK2: SCENE's border repeat before it on the 640 GS pixel frame)
  and the aura's paste, with a crack 1/16 GS pixel wide at GS x 2.5 (texel 5 at 2x, the repeat's
  source column); the late pass forced with the plan ignored: one fill held into the paste's pass,
  none early, no fill pass of its own; against the late pass off (`rd__late_pass_force(0)`) SCENE's
  colour and depth byte for byte, one render pass fewer, the crack filled and each copy the repeat
  laid of it the filled texel's bytes; the main checks leave out the repeat's outer columns.
- `rd_crack_heldglow` (`rd_crack_test 2 heldglow`, the same fakes): `rd_crack_held`'s frame with a
  sprite into WORK2 under a Z GEQUAL test of SCENE's depth after the mask (a lamp's stand-in, no Z
  write) and the glow's paste (WORK0 added over SCENE, Cs * FIX + Cd at FIX 128, the fx plan's
  premultiplied add) in place of the aura's: the fill held into the glow's paste pass
  (`latePassGlowHeld` 1), the same checks.
- `rd_crack_defer` (`rd_crack_test 2 defer`, the same fakes): `rd_crack_heldglow`'s frame with the
  aura's paste (list 8) after the glow's, and the aura's own sprite into WORK1 between them (without
  it the late pass off draws both pastes in one pass on SCENE): the glow's paste deferred to the aura's paste pass
  (`latePassDeferred` 1, none early) and the fill held on to it (`latePassHeld` 1, none in the
  glow's pass), the same checks with two render passes fewer than the late pass off.
- `rd_crack_join` (`rd_crack_test 2 join`, `ICO_VK_FAKE_LIMITS=moltenvk`; `rd_late_pass.cmake`; its
  untextured flat sprites at 2x come out the same bytes, the dumps' textured menu text within
  [DIVERGENCES.md](DIVERGENCES.md) VD-31's bound):
  `rd_crack_aura`'s frame with two list 11 commands after the paste (a blended sprite each into
  SCENE, Z ALWAYS, no Z write, right of the slit): with the late pass both drawn in the paste's pass
  without SCENE's depth (2 draws, 1 pass joined); against the join off
  (`rd__late_pass_force(RD_LATE_FILL)`) SCENE's colour and depth byte for byte, one render pass
  fewer.
- `rd_late_pass_ab.cmake` (`rd_late_pass_ab`, `rd_late_pass_gl`): the dumps of
  `tools/late_pass_dumps.txt` (machine-local; a listed dump that is absent FAILS, a glob that matches
  none too) with `ICO_RD_LATE_PASS=0` and `1`, `--pass-dump --stats`, `ICO_RD_VU_PREPASS=0`, at the
  PowerVR phone's Resolution Auto (2364x1080, Full height and pixel) and at Enhanced 2x 4:3, both under
  `ICO_VK_FAKE_LIMITS=moltenvk`; the `_gl` twin the phone's setting on OpenGL (llvmpipe, the aura's
  paste through a snapshot: no fill held in a DARK room, one held into the glow's paste pass in a
  LAMPS room, no list 11 join). Each pair: every PNG byte for
  byte, the same draws but the border's block draws the stats name, the passes fewer by exactly the fills held, the glow pastes deferred to the
  aura's pass and the passes list 11's draws joined, the crack fill's own passes by the fills held, the colour MB loaded and stored each fewer
  by at least the MB the stats name for the held fills and the deferred pastes and at most that and
  the joined passes' MB
  (within 0.02), the depth and stencil MB loaded not higher, stored not higher but by up to 1.25
  times the held fills' MB where a fill went into the glow's paste pass (the lamps' pass keeps its
  depth store), the same without a join or a glow pass fill, the copy MB the same,
  the off run holding and joining nothing; the dumps the list marks hold that many fills at the
  phone's setting (on OpenGL none held or drawn early) and join as many passes (`joinN`: issue 164's
  paused frame and issue 102's frame); where the on run joined a pass, SCENE, DISPLAY and PRESENT may
  differ by at most 3 levels on at most 800 pixels (`png_level_diff.py`; [DIVERGENCES.md](DIVERGENCES.md)
  VD-31, measured at most 537 pixels and 3 levels), every other target the same bytes; at least one
  fill held and one pass joined over the list on Vulkan. No device: SKIP.
- `rd_lamps_test.c` (`rd_lamps`; `rd_lamps.cmake`, CPU): the lamps' glow for less's pure parts
  (`rd_lamps.c`): `ICO_RD_LAMPS`'s words, the class's default (the band everywhere, the area on the
  PowerVR and desktop classes under Vulkan with the pass area, never another class, a faked phone
  vendor, OpenGL or D3D), the rects' union and 32 x 32 tiles, a GS box's texels on the boot
  corpus's 512 x 512 frame at 2364 x 1080 and on the phone's 848 x 448 frame under Full pixel's
  crop (every texel centre of the box inside the rect, a box off the target none), a lamp's box
  from its stream's bound through an orthographic and a perspective matrix
  (`rd__vis_hiz_occludee`, then the texels: every vertex's and every point's between two vertices
  inside; a corner behind the eye refused), the switch's words (owed, wholepad, fold4, lowres,
  fillhold; lowres and fillhold outside `1` and `all`) and the defaults (the band, owed and wholepad
  everywhere, fillhold nowhere; the fold of two pairs nowhere since the phone's 2026-11-12 measurement;
  lowres asked for by its word alone), the lamps' per-vertex box (`rd__lamps_vertex_box`: a
  diagonal stream whose bound's corner is behind the eye, which the corners' box refuses, boxed
  with every point between its vertices inside; a vertex behind the eye and a lane not finite
  refused), and the area's pick (the region unknown, nothing lit, WORK2 but a tile, the tiles
  round both).
- `rd_lamps_ab.cmake` (`rd_lamps_ab`, `rd_lamps_gl`; `rd_lamps.cmake`): the lamps dumps of
  `tools/lamps_dumps.txt` (machine-local; a listed dump that is absent FAILS: issue 163's two
  frames and the title's castle in four boot runs, 512 x 512 GS, not the phone's 848 x 448), each
  replayed twice in one run (`--repeat 2`: the first lamps replay of a renderer is whole, the area
  from the second) with `ICO_RD_LAMPS=0` and with the parts on (the Mali and OpenGL rows' "on" the
  class's defaults as `ICO_RD_LAMPS=noglowblur`: the glow blur, on by default, is not
  byte-identical and has its own A/B), `--pass-dump --stats`, at the
  PowerVR phone's profile (`ICO_VK_FAKE_LIMITS=moltenvk`, `ICO_RD_VU_PREPASS=0`,
  `ICO_RD_LATE_PASS=1`, Enhanced 2364x1080, Full height and pixel, `ICO_RD_LAMPS=1`), the same
  under `ICO_RD_LAMPS_POISON=1` and `ICO_RD_STORE_POISON=1`, lavapipe's desktop profile at
  Enhanced 2x 4:3 (`ICO_RD_LAMPS=1`) and the Mali fakes (`ICO_VK_FAKE_LIMITS=mali`,
  `ICO_VK_FAKE_VENDOR=0x13B5`, the class's default: no area); and two sequences on the boot
  corpus (`--prev rd-00200`, a whole frame, then `rd-00400` twice; `--prev rd-00400 --prev
  rd-00600`, a dark frame between, then `rd-00400`: the area on the last replay of each); the
  `_gl` twin the desktop setting on OpenGL (llvmpipe, the area off, the band lazy). Each pair:
  every PNG byte for byte, the same draws, the passes fewer by the bands left lazy exactly, the
  colour loaded the same, the colour stored and cleared fewer by the stats' saved bytes and the
  bands' (WORK2_PAD's texels x 4), the depth and stencil loaded and stored fewer by the saved
  bytes (two hundredths of a MB; not under the poison), the saved colour bytes 4 x the texels the
  area left; the off run no area and no band; one planned lamps pass a lamps replay, never whole
  for the region unknown; an area at least once at the phone, poison and desktop settings and in
  both sequences, none at the Mali and OpenGL settings, a band at every setting. Round 3 adds the
  corpus's dark (`rd-00600`) and whole (`rd-00200`) title frames: the passes fewer also by the dark
  plan's WORK0 alpha sprites owed and by the passes of two blur pairs (`fx_blur4_ps`), the same
  pairs folded as the off run, four passes of two where the off run folded nine pairs (at the
  phone, poison, desktop settings and the sequences; none at Mali and OpenGL), the colour loaded
  fewer by a WORK0 a pass of two pairs and stored fewer by that and the owed sprites' WORK0, an
  owed sprite at every setting. No device: SKIP.
- `rd_glow_blur_test.c` (`rd_glow_blur`; `rd_glow_blur.cmake`, CPU): the glow's blur in two
  passes, its pure parts (`rd_glow_blur.c`): the word `glowblur` of `ICO_RD_LAMPS` (alone, with
  others, not in `1`), its text, the default bits by `RD_GLOW_BLUR_DEFAULT`, every reason's text;
  the operator of the ten pairs as recorded at 4:3 (the boot corpus's `rd-00400`, WORK0 256 x 128)
  and on the phone's 848 x 448 (`v3677`, 424 x 128): rows summing to 1, offsets within 8 texels,
  nothing above 1 % left out of a row's taps, the gains (139/128)^10, (136/128)^10,
  (132/128)^10 and 1, the texel itself on the top row and (4:3) the left column, a spread above
  1 texel somewhere, the table's round trip; the two passes emulated as the shader computes them
  (the table decoded, hardware bilinear with 8-bit fractions, the across pass stored to 8 bits,
  the gains and the loss) against the chain in the GS's integers on a fixed picture of lamp-like
  blobs: WORK0's RGB at most 25 levels apart and 0.40 on average, alpha at most 8 (the evidence's
  Python mirror gave 21, 0.12 and 5); the refusals (a pass 1's colour, a pass 2 not covering
  WORK0, one pair, a constant added).
- `rd_glow_blur_ab.cmake` (`rd_glow_blur_ab`, `rd_glow_blur_gl`; `rd_glow_blur.cmake`): the glow's
  blur in two passes (DIVERGENCES.md VD-33) on every dump of `tools/glow_light_dumps.txt` and
  `tools/lamps_dumps.txt` (machine-local; a listed dump that is absent FAILS),
  `ICO_RD_LAMPS=pad+owed+wholepad` against `...+glowblur`, `--pass-dump --stats`,
  `ICO_RD_VU_PREPASS=0 ICO_RD_LATE_PASS=1`, at the phone's Resolution Auto under
  `ICO_VK_FAKE_LIMITS=moltenvk`, Enhanced 2x, 3x and 4x at 4:3, the Mali fakes (no blur fold),
  Original 4:3 at 1x (`--aspect 4:3`), Enhanced 1x and the poison; a dump the phone's setting leaves as recorded is checked there (every
  PNG the same bytes) and not run at the others. Where a chain was drawn in two passes: the passes
  fewer by the chain's as today draws them less two (2 P less the pairs folded), SHADOW0..2 and
  WORK2_PAD the same bytes, WORK0 within 48 levels of today's, WORK3 within 36, DISPLAY and PRESENT
  within 26 (RGB; measured: the sun frame v2278 46, 34 and 23; printed per dump, `glow blur measure:`, and as each setting's maxima), the
  shimmer at the phone's setting, 2x, Original 1x and Enhanced 1x (WORK3's change under `--jitter 0.5,0.5`) at most 5/4 of
  today's + 4 (every scale takes it, Original 1x included); the poison every PNG the
  same bytes; a chain in two passes over the list at each measured setting. `rd_glow_blur_gl`:
  the phone's setting on OpenGL (llvmpipe). No device: SKIP.
- `rd_paste_spec_test.c` (`rd_paste_spec`; `rd_paste_spec.cmake`, CPU): the glow's paste by its own
  entry (`port/render/rd_paste_spec.c`): the bits `fx_paste_ps` holds and reads; the gate on the
  phone's paste DrawCB taken, with any TFX, FBA, the As form, AREF without ATE and an unset source
  scale; each held flag, TEXFMT not RGBA32, ATE, a scaled or cropped source refused with its reason;
  a key with a depth, the subtract, another entry, format or sample count refused; the general key
  `rd__fx_enumerate`'s premultiplied add, its twin added by the module's stand-in (one key more);
  `ICO_RD_PASTE_SPEC`'s words; the attempt ledger's row through its decider.
- `rd_paste_spec_ab.cmake` (`rd_paste_spec_ab`, `rd_paste_spec_sabotage`, `rd_paste_spec_gl`;
  `rd_paste_spec.cmake`): every dump of `tools/lamps_dumps.txt` and `tools/glow_light_dumps.txt`
  (machine-local; a listed dump that is absent FAILS where `/primary/dev/ico-dumps` exists),
  `ICO_RD_PASTE_SPEC=0` against `=1`, `--pass-dump --stats`, at the phone's Resolution Auto under
  `ICO_VK_FAKE_LIMITS=moltenvk` with `ICO_RD_LATE_PASS=1`, Enhanced 2x at 16:9 and (the lamps list)
  Original 4:3 at 1x: every PNG the same bytes, the same draws and passes and pastes seen, arm 0 off,
  arm 1 on with its self-test passed, at least one paste by `fx_paste_ps` at the phone's setting and
  2x. `_sabotage`: the lamps list at the phone's setting with `=sabotage`: the self-test failed at a
  texel, no paste by it, the same bytes. `_gl` (label gl): the lamps list on OpenGL: off (the module
  is Vulkan's alone), the same bytes.
- `rd_volume_fs_test.c` (`rd_volume_fs`; `rd_volume_fs.cmake`, CPU): the shadow volumes' stencil-only entry (`rd_volume_fs.c`): the gate's reasons in order, the `date_clear_ps` twin (the fragment entry alone differs), the volume key without a device (`sprite_ps`, seen, none taken), no twin in the start-up set there, `ICO_RD_VOLUME_FS`'s words, the bench's force, the ledger's row.
- `rd_comp_spec_test.c` (`rd_comp_spec`; `rd_comp_spec.cmake`, CPU): the composites' own entry (`rd_comp_spec.c`): the planner's composite and level keys take their forms, each held field refused alone; the DrawCB's flags (each one `sprite_ps` reads refused, the others taken, the TEXA word); the twin; no device: seen, kept, none taken, no twin; the words; the ledger.
- `rd_comp_spec_ab.cmake` (`rd_comp_spec_ab`, `rd_comp_spec_sabotage`; `rd_comp_spec.cmake`): the lists of `rd_volume_fs_ab` and `glow_light_dumps.txt`, `ICO_RD_COMP_SPEC=0` against `=1` at the phone's setting and Enhanced 2x 16:9, the first list at the Mali class (off) and MSAA 4x: every PNG, draws, passes and traffic the same, arm 1 on, a draw by it on each volume dump at the phone. `rd_comp_spec_selftest`, `_selftest_sabotage`: as the volumes'.
- `rd_volume_fs_ab.cmake` (`rd_volume_fs_ab`, `rd_volume_fs_sabotage`; `rd_volume_fs.cmake`): every dump of `tools/shadow_cuts_dumps.txt`, `date_cut_dumps.txt`, `shadow_box_dumps.txt`, `no_volume_dumps.txt` and `lamps_dumps.txt` (an absent one FAILS), `ICO_RD_VOLUME_FS=0` against `=1` at the phone's setting, Enhanced 2x 16:9, the Mali class at 1x and MSAA 4x at 2x: every PNG, the draws, passes and traffic the same, arm 1's self-test passed, every single-sampled volume draw by `date_clear_ps`. `_sabotage`: the phone's setting: refused, the same bytes. `rd_volume_fs_selftest` and `_selftest_sabotage` (`test/rd_shadow_selftest.cmake` through `rd_fog_selftest_test`): the device self-test's passed line and the entry on; sabotaged, the Z ALWAYS frame differs and it stays off.
- `rd_vu_compact_test.c` (`rd_vu_compact`; `rd_vu_compact.cmake`, CPU): the index compaction's pure
  parts (`rd_vu_compact.c`): the plan's places (two calls one after the other in the plane, their
  workgroups), its raises (the first call's values from 0, the next one's past them, mod 2^32),
  its refusals (an empty list, a count not of triangles, no list, the plane's room for the lists
  and a counts texel a workgroup, the device's largest index value raised and not), 65 triangles
  two workgroups, the counts' base (the texel after the lists); the counts summed by list
  (`w` 1 list 0) and a picture with no workgroup; the window's line word for word, cut in a short
  buffer, 0 % with nothing judged; `ICO_RD_VU_COMPACT` unset, 0, `off`, 1, `on` and another word.
- `rd_vu_compact.cmake`'s self-test rows (`rd_vuprepass_selftest_compact`, `_compact_refused`,
  `_compact_mali`, `_compact_stores`; `test/rd_vuprepass_selftest.cmake`, lavapipe): the start-up
  self-test with the record by compute (`ICO_RD_VU_RECORD=compute`) passes with the compaction, the
  frame's copy of the prelit mesh past the target's right edge named in the verdict and the
  compaction's line `on: passed its start-up self-test`; with `ICO_RD_VU_COMPACT_SABOTAGE=1` the run
  with it fails, the run without it passes and the compaction is refused, the pre-pass on with the
  compute record; under the phone's limits and Arm's vendor id (`ICO_VK_FAKE_LIMITS=mali
  ICO_VK_FAKE_VENDOR=0x13B5`, synchronisation validation with the submit-time checks) it passes as
  the Arm class; with the vertex stores the compaction is not applicable and the frame has no copy.
- `rd_vu_compact_ab.cmake` (`rd_vu_compact_ab`, `rd_vu_compact_ab_pixel`; `rd_vu_compact.cmake`):
  the index compaction ([PIPELINES.md](PIPELINES.md#the-index-compaction)) on against off,
  `ICO_RD_VU_COMPACT=1` against `=0`, the record by compute in both (`ICO_RD_VU_RECORD=compute`),
  `--pass-dump --stats`, on every dump of `tools/vu_flat0_dumps.txt` (a listed dump that is absent
  FAILS where `/primary/dev/ico-dumps` exists): the corpus profile (Original 4:3 at 1x, Enhanced
  16:9 at 2x, at 16:9 with MSAA 4x, at 16:9 and 1280x720), the Arm fakes on issue 179's four
  Mali-G52 dumps at their settings, the Pixel's three opening dumps at its setting; with the draw
  merge, and without it at the first two corpus settings and the phones'. Every target's PNG the
  same bytes, the off run's `stats: VU index compaction off by default: not run`, the on run's
  `stats: VU index compaction on: N of M list 0 triangles left out (drop D, outside O)` with M above
  0 on the phones' dumps and N above 0 on the Mali's; the counts matched where
  `tools/vu_compact_counts.txt` has a line for the dump, profile, setting and merge, printed
  (`rd_vu_compact_ab: count ...`) where it has none. `rd_vu_compact_ab_pixel`: rd_pixel's frames,
  the corpus profile. No device: SKIP.
- `rd_vu_flat0_test.c` (`rd_vu_flat0`; `rd_vu_flat0.cmake`, CPU): the main vertex shader's flat
  outputs at the provoking corner, its pure parts (`rd_vu_flat0.c`): `ICO_RD_VU_FLAT0` unset, empty
  and 1 the shipped build, 0 the twins, any other word the shipped build and named; the start line
  in both forms (`VU flat outputs: provoking corner only (9 loads a corner-0 invocation, 4 at the
  others`, `VU flat outputs: every corner (ICO_RD_VU_FLAT0=0` naming both twins) and with a twin
  that could not be made, cut in a short buffer; the counts (corner 0 the 3 + 1 + 5 loads every
  corner read before, the others 5 fewer).
- `rd_vu_flat0_ab.cmake` (`rd_vu_flat0_ab`, `rd_vu_flat0_ab_pixel`, `rd_vu_flat0_gl`;
  `rd_vu_flat0.cmake`): `vu_main_vs` and `vu_merge_main_vs` reading the planes at corner 0 alone
  ([PIPELINES.md](PIPELINES.md#flat-outputs-at-the-provoking-corner)) against their twins with
  every corner computing them, `ICO_RD_VU_FLAT0=1` against `=0`, `--pass-dump --stats`, each with
  the draw merge and without it (`ICO_RD_VU_MERGE=0`), on every dump of
  `tools/vu_flat0_dumps.txt` (machine-local; each line its profiles and a path; a listed dump that
  is absent FAILS where `/primary/dev/ico-dumps` exists): the corpus profile (Original 4:3 at 1x,
  Enhanced 16:9 at 2x) on the 2026-10-07 captures, three reporters' frames and issue 179's four
  Mali-G52 dumps; those four again under the Arm fakes (`ICO_VK_FAKE_LIMITS=mali
  ICO_VK_FAKE_VENDOR=0x13B5`, the record by compute, 1x, 21:9, half height); the Pixel's three
  opening dumps at its setting (`ICO_VK_FAKE_LIMITS=moltenvk`, Enhanced 2364x1080, full height,
  full pixel). Every target's PNG (SCENE and DISPLAY among them) the same bytes, each run's line
  (`rd: VU flat outputs: every corner`, `... provoking corner only (9 loads ...`), `merge on)` or
  `merge off)`, a frame drawing nothing through the record (no `stats: VU pre-pass on: [1-9]`)
  skipped, a 4:3 frame the 16:9 setting cannot show skipped, targets compared under every profile
  with a dump. `rd_vu_flat0_ab_pixel`: rd_pixel's `rd_pixel_vu`, `rd_pixel_vu36` and
  `rd_pixel_frame` (every machine; CI's Direct3D 11 job runs the same on WARP,
  `tools/ci_d3d11_warp.sh`, `-DBACKEND=d3d11`, the debug layer's errors failing it).
  `rd_vu_flat0_gl`: the list on OpenGL (llvmpipe), every dump at the corpus settings (the fakes are
  Vulkan's). No device: SKIP.
- `rd_smallcuts_test.c` (`rd_smallcuts`; `rd_smallcuts.cmake`, CPU): the pure parts of the turn in
  the present's blit and the VU record by compute on the PowerVR class: `rd__turn_rect` for every
  quarter turn against `rd_prerotate.c`'s pixel table (every pixel of the rect lands inside the
  turned rect, the sizes swapped for odd turns) at the phone's window (2364x1080: the whole box, a
  4:3 box with bars, the border mask's inset) and odd sizes and corners, and the self-test's own
  90-degree case by value (7,5 11x7 in 48x30 is 18,7 7x11); `rd__turn_fold_why`'s reasons in the
  present's order (the CRT filter first, the box blit alone folds); `rd__vu_class_measured` (the
  Pixel's `PowerVR D-Series DXT-48-1536 MC1` yes; a B-Series, a Rogue and no name no) and
  `rd__vu_class_pick` through the ledger's decider (on by the build's default on the measured
  PowerVR, off by default on the rest of the class with its reason and with the define 0, on by
  `ICO_RD_VU_COMPUTE_CLASS=1`, off by `=0`, not applicable with a mode asked by name, without vertex
  stores or compute stores, or on another class).
- `rd_bench_test.c` (`rd_bench`; `rd_bench.cmake`, CPU): the rc bench's classes over a frame built
  by hand (a command of every class, list by list): `rd_bench_classify` against the test's table,
  each class's mask making NOPs of exactly its commands, list 7's mask its seven actions and none of
  its state, the baseline's mask the lists whole again, and `rd_bench_variants` without a device
  (the baseline, the six lists, the parts in order with the shadow chain's count, level and
  composite rows, no switch).
- `rd_bench_ab.cmake` (`rd_bench_ab`; `rd_bench.cmake`): `rd_replay_tool --bench 1` on lavapipe over
  rd_pixel's dump (640x480) and the machine-local `tools/bench_dumps.txt` (the Pixel's v746 and
  v2278 at the phone's setting; a listed dump that is absent FAILS where `/primary/dev/ico-dumps`
  exists): every variant ran, a baseline's presented picture after the bench byte for byte the one
  before it (unless two baselines before it already differ: a feedback effect, said so), and each
  variant line the list names. No device: SKIP.
- `rd_turn_fold_ab.cmake` (`rd_turn_fold_ab`; `rd_smallcuts.cmake`): the present's turn drawn by its
  box blit (`ICO_RD_TURN_FOLD=1`) against the turn pass (unset) through the headless twin
  (`ICO_VK_FAKE_TRANSFORM=90`, `180`, `270`), rd_pixel's dump and every dump of the machine-local
  `tools/turn_fold_dumps.txt` (the Pixel's three opening dumps and issue 179's three Mali-G52 dumps;
  a listed dump that is absent FAILS where `/primary/dev/ico-dumps` exists): the phone's setting
  (`ICO_VK_FAKE_LIMITS=moltenvk`, Enhanced 2364x1080, full height, full pixel) and a 16:9 window at
  its own resolution (1280x720, full pixel) fold or keep the turn pass for drawn text where the
  scene fills the box one texel a pixel, and keep it (`the box blit not one texel a pixel`) where
  the tool's display line says the frame is cropped (every listed dump) or its aspect is not the
  window's (rd_pixel's 4:3 frame); rd_pixel's dump at 1024x768, full pixel, folds; 640x480 at 1x
  keeps it; rd_pixel's dump with `--overlay-test` at 1024x768 keeps it (`the port's overlay or the
  deferred text drawn`); the fold's runs say its self-test passed
  (`6 cases`), the turn pass's runs never fold, every PNG pair the same bytes, at least one present
  folded. No device: SKIP.
- `rd_vuprepass_selftest_compute_class`, `_back`, `_off` (`rd_smallcuts.cmake`, through
  `test/rd_vuprepass_selftest.cmake`): the VU record by compute on the PowerVR class on lavapipe
  by the tests' switch: with `ICO_RD_VU_COMPUTE_CLASS=1` the rule's line (`the compute dispatches
  in place of the vertex stores (ICO_RD_VU_COMPUTE_CLASS=1: the tests' switch on another class`)
  and the self-test's `record compute`; under the stale sabotage the compute record goes back to the
  vertex stores first (`(the PowerVR class's compute record off: they failed the start-up
  self-test)`) before the verdict; with `=0` the rule's `the record as auto takes it` and no compute
  record.
- `rd_vurecord_ab_compute_class`, `_opening` (`rd_smallcuts.cmake`, through
  `test/rd_vu_env_ab.cmake`): `ICO_RD_VU_COMPUTE_CLASS=0` (the stores) against `=1` (the compute
  dispatches by the rule), every named target byte for byte, A's stats `VU record stores`, B's
  `VU record compute: ... (the PowerVR class's rule`; on rd_pixel's dump and
  `tools/vustage_dumps.txt`, and on `tools/lamps_dumps.txt` (the Pixel's opening dumps among them;
  `LIST_STRICT`). No device: SKIP.
- `rd_lamps_lowres_ab.cmake` (`rd_lamps_lowres_ab`; `rd_lamps.cmake`): the lamps' glow at the GS
  frame's size above 1x (`RD_LAMPS_LOWRES`, DIVERGENCES.md VD-32) on every glow dump of
  `tools/glow_light_dumps.txt` (machine-local; a listed dump that is absent FAILS),
  `ICO_RD_LAMPS=pad+owed+wholepad` against `...+lowres`, `--pass-dump --stats`,
  `ICO_RD_VU_PREPASS=0`, `ICO_VK_FAKE_LIMITS=moltenvk`, at the phone's Resolution Auto
  (2364x1080, Full height and pixel; again with `lowresbox`, `lowresbias`, `lowreshy`, `lowres4` and `lowresp`,
  printed only) Enhanced 2x, 2.5x (1440x1280), 3x and 4x at 4:3 and the Full height on a 16:9 window (1920x1080), and each dump once at Original 1x: the stats line
  `lamps lowres on|off`, no draw into WORK2 without a depth, a depth fill where a draw took
  WORK2's own, one such draw at least over the list at each setting, SHADOW0..2 and WORK2_PAD the
  same bytes; WORK3 (the glow the paste adds, one size at every scale) against Original 1x's
  (`lowres original: <setting> <dump> WORK3 ...`): with lowres's default form no farther than
  today's in its largest level difference and texels apart (margin 2 levels, 16 texels and 5 %),
  FAILING otherwise at the phone's, 2x, 3x and 4x; DISPLAY and PRESENT against today printed per
  dump (`lowres measure: ...`, `lowres maxima: ...`), no bound; at Enhanced 1x every PNG the
  same bytes and no draw on WORK2's own depth; at the phone's setting `...+lowres` against
  `...+lowres2x` (WORK2 at twice the GS size) at each of those settings against today's: WORK3
  at most 32 levels from today's (`lowres2x today: ...`), and at the phone's setting and 2x its
  WORK3's change under half a SCENE texel's shift (`--jitter 0.5,0.5`) at most twice today's plus 4
  levels (`lowres2x shimmer: ...`; at most 48 levels from today's and twice today's shimmer plus
  4), FAILING otherwise; `...+lowresms` (and `...+lowresms8` at the phone's
  setting and 2x) the same at the same settings
  on lavapipe's own desktop class (no fake), its stats `lowresms on` with a pass of 4 samples at
  least, and at Enhanced 1x and under `ICO_RD_LAMPS_POISON=1` every PNG the same bytes; the WORK3-against-Original lines printed for
  information only; at the phone's setting `...+lowres` against
  `...+lowres+fillhold` every PNG the same bytes with as many crack fills held at least (one
  more over the list), and `...+lowres` against it under `ICO_RD_LAMPS_POISON=1` every
  PNG the same bytes. No device: SKIP.
- `rd_crack_work_ab.cmake` (`rd_crack_work_ab`, `rd_crack_work_gl`; `rd_crack_work.cmake`): the
  crack fill's snapshot by the swap and its early leave (`rd_crack_fill.c` FILLSWAP, CRACKTAP). The
  dumps of `tools/crack_work_dumps.txt` (machine-local; a listed dump that is absent FAILS: the
  phone's bridge frame and three cage frames, issue 163's rooms, the corpus)
  with `ICO_RD_CRACK_SWAP` and `ICO_RD_CRACK_TAP2` both `0` and both `1`, `--pass-dump --stats`,
  `ICO_RD_VU_PREPASS=0`, at the PowerVR phone's Resolution Auto (`ICO_VK_FAKE_LIMITS=moltenvk` and `ICO_RD_LATE_PASS=1`, the
  phone's default: the fake leaves `RhiLimits.tiler` false on lavapipe) and
  on lavapipe's own profile at 1344x1008, 3x and 4x (4:3; scales 2.1 to 4, the fill in a pass of its
  own); the `_gl` twin the phone's setting on OpenGL. Each pair: every PNG byte for byte, the same
  draws, passes, fill passes and pixels filled, the off run swapping and leaving early nothing, the
  swap's self-test passed in both runs where the fill drew, the colour MB copied and loaded each
  fewer by the swapped snapshots' MB (within 0.02), the rest the same; the marked dumps' swaps
  (`swapN`: the bridge 1, a held fill 0) and early-leave draws (`tapN`) at the phone's setting; at
  least one of each over the list. No device: SKIP.
- `rd_crack_fill11`, `rd_crack_fill11_later`, `rd_crack_fill11_tiler`, `rd_crack_fill11_area`
  (`rd_crack_test 2 fill11`, `2 fill11z`, `2 fill11` under `ICO_VK_FAKE_LIMITS=moltenvk
  ICO_RD_PASS_AREA=0`, `2 fill11 area` under `ICO_VK_FAKE_LIMITS=moltenvk`; `rd_fill11.cmake`): the
  crack fill in list 11's pass (`rd_fill11.c`; the self-test's ctest twin). The fill's depth twins'
  start-up self-test is run (`rd__crack_swap_selftest` runs it) and must pass (its state line
  matched), then with the switch forced on (`rd__fill11_force(1)`) the frame draws the glow's
  `cleanUpFB`, a sprite into WORK0 (the fill's place, its snapshot there) and list 11's two blended
  sprites into SCENE under Z ALWAYS without a Z write (`fill11z`: a third with a Z write). Against the
  switch off: SCENE's colour and depth byte for byte, the off run with one fill pass and nothing held;
  `fill11` (lavapipe's desktop profile, and the tile class's fakes without the pass area): one fill
  drawn in list 11's pass (`RdFill11Counts.landed`), no fill pass of its own, one render pass fewer,
  the once line `drawn as the first draw of list 11's pass` matched; `fill11z` and `fill11 area`
  (the pass area on: list 11's pass would hold its sprites' tiles alone): the held fill drawn in a pass
  of its own at list 11's first draw (`RD_FILL11_WHY_LATER`, `RD_FILL11_WHY_AREA`), the same passes,
  the once line absent. Every `rd_crack_test` check of the filler holds too (the cracks filled, the
  count, the swap's `checkWork`).
- `rd_pass_objects_ab.cmake` (`rd_pass_objects_ab`; `rd_pass_objects.cmake`): the dumps of
  `tools/pass_objects_dumps.txt` (machine-local; a listed dump that is absent FAILS, a glob that
  matches none too: stage 12 on the issue 123 tablet, the Pixel's opening, issue 179's four dumps)
  replayed with `--pass-dump --stats` twice: dynamic rendering (`ICO_VK_LEGACY_PASSES=0`) and lavapipe
  put on Qualcomm's driver row (`ICO_VK_FAKE_PASS_ROW=qualcomm`: every pass a `VkRenderPass` object),
  at the 123 tablet's setting (Enhanced 3x, Full height, Full pixel, 3048x2032; `ICO_VK_FAKE_VENDOR=
  0x5143`, `ICO_RD_VU_PREPASS=0`) and the 179 phone's (1x at 21:9, 2400x1080; the Arm fakes): every
  PNG byte for byte, the same draws and passes, each run's device line naming its path (`render
  passes dynamic`, `render passes legacy (Qualcomm's own driver`); a dump drawn narrower than a
  setting's aspect is skipped there; at least one target compared. No device: SKIP.
- `rd_fill11_ab.cmake` (`rd_fill11_ab`, `rd_fill11_gl`; `rd_fill11.cmake`): the dumps of
  `tools/fill11_dumps.txt` (machine-local; a listed dump that is absent FAILS, a glob that matches none
  too; a line may give `phoneN`, `deskN`, `glN` and `nop SPEC`, the tool's `--nop` for both runs)
  with `ICO_RD_FILL11=0` against the switch on, `--pass-dump --stats`, `ICO_RD_VU_PREPASS=0`: at the
  PowerVR phone's Resolution Auto (2364x1080, Full height and pixel) under `ICO_VK_FAKE_LIMITS=moltenvk
  ICO_RD_LATE_PASS=1`, at Enhanced 2x 4:3 on lavapipe's own profile (both on runs with
  `ICO_RD_FILL11=1`: off by default in this release), and at the phone's setting on a Mali-G68's
  limits and an Arm phone's vendor (`ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_VENDOR=0x13B5`: the on run
  sets nothing and must draw no fill in list 11's pass, its self-test not run); the `_gl` twin
  Enhanced 2x 4:3 on OpenGL (llvmpipe, `ICO_RD_FILL11=1`). Each pair: every PNG byte for byte, the same draws, the passes and the crack fill's own
  passes each fewer by the fills drawn in list 11's pass exactly, the colour MB loaded and stored each
  fewer by the MB the stats name for them (within 0.02), the depth and stencil MB not more, the copies
  the same, the off run drawing and holding nothing, the self-test passed in every run that ran it;
  the marked dumps' counts (the corpus title's DARK frame with its motion blur a NOP: one at 4:3 and
  on OpenGL, none at the phone's Full pixel, whose border repeat at the mask draws the held fill
  first; the title's WHOLE, LAMPS and motion-blurred DARK frames and the gameplay dumps none at the
  settings marked); at least one fill drawn in list 11's pass over the list (desk or gl). No device:
  SKIP.
- `rd_date_cut_ab.cmake` (`rd_date_cut_ab`, `rd_date_cut_ab_gl`): the dumps of
  `tools/date_cut_dumps.txt` (machine-local; a listed dump that is absent FAILS, a glob that matches
  none too) with `ICO_RD_DATE_CUT=0` and `1`, `--pass-dump --stats`, under `ICO_RD_BLEND=fixed`,
  `ICO_RD_STORE_PLAN=1`, `ICO_RD_STORE_POISON=1`, `ICO_RD_DATE_PATH=snapshot`,
  `ICO_RD_VU_PREPASS=0`, `ICO_RD_DATE_ONE=0` (with one DATE set-up for the composites the cut has
  no run on them; `rd_date_one_ab` checks the two together), at the Mali-G52's setting (1x, 21:9, 1612x720; `ICO_VK_FAKE_LIMITS=mali`,
  `ICO_VK_FAKE_NO_DST=1`), the Adreno 740's (3x, 3:2, Full height and pixel, 3048x2032;
  `ICO_VK_FAKE_ADRENO=1`, `ICO_VK_FAKE_NO_DST=1`) and the PowerVR phone's Resolution Auto without
  its in-pass read (`ICO_VK_FAKE_NO_DST=1`: a fractional scale, 0 passes cut expected); the `_gl`
  twin the Mali's setting on OpenGL (llvmpipe).
  Each pair: every PNG byte for byte, the same draws, passes, snapshots, colour and copy MB; the
  off run cuts nothing, the on run begins no pass again, cuts the passes the list names (2 for the
  composites) or at least one where it takes two snapshots, and its depth and stencil MB loaded and
  stored each fall by 0.8 to 1.0 of the "at most" its stats line names; Vulkan also `--keys` at the
  Mali's present size without an aspect: the cut adds no key the start-up set lacks. No device: SKIP.
- `rd_date_one_test.c` (`rd_date_one`, lavapipe, 77 without a device after its CPU cases): one
  DATE set-up for the shadow composites (`rd_date_one.c`, `rd_date_one.h`) and the empty
  composites (`rd_shadow_box.c` `rd__shadow_box_empty_skip`). CPU: the bound and the group rule on
  the retail stage data (shadowDepth 72 with blends 40/80/120, 80 with 90/100/128: two shared), the
  debug menu's 128 x 128 (0x80 at every composite: two refused; 128 with shadowBlend[1] alone: the
  SHADOW0 composite is the last, two shared), the bound rounded up, DATM 1, FBA 1, COLCLAMP 0's
  wrap and a draw in no composite form (none kept), no alpha written (kept), force; a draw or a
  composite without DATE between, another target; the 10 s clause and the `--stats` words. On the
  device (`ICO_RD_DATE_PATH=snapshot`, `ICO_RD_BLEND=fixed`, `ICO_RD_STORE_POISON=1`, 1x, 512x448)
  frames in the shadow's shape built by hand (list 0 SCENE's depth and the alpha's top bit over its
  left half; list 3 a reset, a rectangle's volume, the resolve, SHADOW0 from the count, three
  composites of SHADOW0 under 0x3400D; list 4 a Z GEQUAL sprite over the bottom eighth, clear of
  the composites' pixels): d1 (shadowDepth 72, blends 40/80/120)
  off against on (`rd__date_one_force`): SCENE byte for byte, snapshots 3 (1 first, 2 written) against
  1 with 2 shared and 2 reused, 4 render passes and 4 draws fewer (2 R8 draws; the three composites,
  here all reading SHADOW0, one screen run once no snapshot ends their pass: 2 screen draws fewer),
  the DATE cut's passes 2 against 0,
  none begun again, the shared texels 2 x SCENE's; d2 (128 x 128): on refuses both (3 snapshots, 2
  refused, SCENE byte for byte), forced past the bound 2 shared, 1 snapshot and SCENE different (the
  test sees a wrong share); d3 (no volume, one DATE set-up off) the empty composites off against on
  (`rd__shadow_box_empty_force`): SCENE byte for byte, 3 skipped with DATE, snapshots 3 against 0,
  5 passes and 3 draws fewer; no validation error.
- `rd_shadow_cuts_ab.cmake` (`rd_date_one_ab`, `rd_empty_skip_ab`, `rd_no_volume_break_ab` and their
  `_gl` twins, `-DITEM=dateone|emptyskip|novolbreak`): the dumps of `tools/shadow_cuts_dumps.txt`
  (the Mali reporter's four dumps and the Pixel's three opening dumps, each "volume" or
  "novolume", v746 with the Mali setting's passes fewer under the empty composites, 6), then of
  `tools/date_cut_dumps.txt`, `tools/shadow_box_dumps.txt` and `tools/no_volume_dumps.txt` (their
  classes where those lists give them; machine-local; a listed dump that is absent FAILS), each item
  off against on with the other two items and the no-volume window's open mode off in both runs,
  `--pass-dump --stats` under `ICO_RD_STORE_PLAN=1`, `ICO_RD_VU_PREPASS=0`. Settings: dateone the
  Mali-G52's (`ICO_VK_FAKE_LIMITS=mali ICO_VK_FAKE_NO_DST=1 ICO_VK_FAKE_NO_DUAL=1`, 1x), the Adreno
  740's (3x, Full height and pixel), the PowerVR phone's Resolution Auto without its in-pass read
  (fractional) each by the snapshot (`ICO_RD_DATE_PATH=snapshot ICO_RD_BLEND=fixed
  ICO_RD_STORE_POISON=1`), and the phone's stencil path (`ICO_VK_FAKE_LIMITS=moltenvk
  ICO_RD_LATE_PASS=1`); emptyskip the Mali's and the phone's; novolbreak the phone's, lavapipe 1x with
  `ICO_RD_DATE_PATH=stencil`, the phone's with `ICO_RD_NO_VOLUME_POISON=1` (every PNG but SHADOW0-2
  compared), the phone's with `--repeat 2` (the second replay's targets holding the first's zeros)
  and the Mali's (not taken); the `_gl` twins at 1x on OpenGL. Every pair: every PNG byte for byte;
  from the code: dateone: off shares nothing, on keeps none to its own, begins no pass again; by
  the snapshot S shared means S snapshots and S draws fewer, 2 S passes fewer, the colour loaded and
  stored each S x 5 bytes a texel of the shared targets fewer, the depth and stencil the same at the
  integer scales and 4 to 5 bytes a texel fewer at the fractional one; by the stencil bit S set-ups
  and 2 S draws fewer, the same passes and traffic; "volume" and "novolume" dumps share 2;
  `--keys` at the Mali's: no key more missing; emptyskip: K skipped, D with DATE: by the snapshot D
  snapshots and draws fewer and D to 2 D passes fewer (v746 at the Mali's: 6), by the stencil bit D
  set-ups and 2 D draws fewer, the passes the same or one fewer (a "novolume" dump the same, and
  its traffic); "novolume" dumps skip 3, "volume" none; novolbreak: the listed windows taken at the
  stencil settings, none by the snapshot; a taken window: the passes off's less 4 a window plus the
  zero clears it recorded, at lavapipe 1x one fewer again (its fog reads the depth outside the pass:
  list 3's SCENE pass of the skipped composites is begun by nothing), the set-ups 3 fewer a
  "novolume" window, at `--repeat 2` no clear and
  the targets holding zero counted, none counted in a first replay; nothing taken: the same passes
  and traffic. Each item does something over the dumps. No device: SKIP.
- `rd_shadow_tile_test.c` (`rd_shadow_tile` with `frames`, `rd_shadow_tile_selftest` with
  `startup` and `ICO_RD_SHADOW_TILE=1`; both `ICO_RD_DATE_PATH=stencil`, lavapipe; 77 without a
  device, DATE's stencil bit or the one-draw count, after the CPU words): the shadow tiles
  (`rd_shadow_tile.c`): the 10 s clause and the `--stats` words from a made-up record; at a 640x448
  Enhanced scale-2 Full pixel 16:9 scene, `frames` replays the self-test's frames (a shadow in a
  corner, shadows at the frame's edges, no shadow), each with the B-spline composites off and on:
  SCENE byte for byte with the tiles off and on (a later DATE DATM 1 sprite included), the corner's
  and the edges' composite over a mask with dead tiles; `startup` runs `rd_fog_selftest`, whose
  first run takes the tiles' self-test, and checks it passed; no validation error.
- `rd_shadow_tile_ab.cmake` (`rd_shadow_tile_ab`): the dumps of `tools/shadow_tile_dumps.txt`
  (machine-local; a listed dump that is absent FAILS: `tools/shadow_box_dumps.txt`'s and the
  bridge v2809 and cage v4405 captures) with `ICO_RD_SHADOW_TILE=0` and `1`, `--pass-dump --stats`,
  under `ICO_RD_STORE_PLAN=1`, `ICO_RD_VU_PREPASS=0`, at the PowerVR phone's profile
  (`ICO_VK_FAKE_LIMITS=moltenvk`), the Mali-G52's (`ICO_VK_FAKE_LIMITS=mali`, `ICO_VK_FAKE_NO_DST=1`:
  nothing tiled) and the B-spline composites at Enhanced 2x, 3x and 4x (`ICO_RD_SHADOW_BSPLINE=1`,
  `ICO_RD_DATE_PATH=stencil`). Each pair: every PNG byte for byte, `=0` off with nothing, the
  passes more by at most the masks; at the phone setting v3677 and v2809 3 composites over the mask,
  v2809's mask texels live at most 3717 (SHADOW0), 1023 (SHADOW1) and 289 (SHADOW2) by its measured
  alpha boxes. No OpenGL twin: the path needs DATE's stencil bit. No device: SKIP.
- `rd_shadow_box_test.c` (`rd_shadow_box`, CPU): the shadows over the volumes' box
  (`rd_shadow_box.c`): the count's box from 12.4 vertices (margins, a clamped vertex, clipping);
  one axis through a sprite (the Pixel frame's level 0 across and down, its rows past the count's
  448 under a clamp, a repeated edge taken whole, a repeated axis inside, a box no UV reaches, a
  constant UV, a flipped sprite); a level's whole cover and one pixel short; a composite's box from
  SHADOW2 over the 848x448 frame, an empty source, the UV offset; union and clip; the 10 s clause
  and the `--stats` words from a made-up record.
- `rd_shadow_box_ab.cmake` (`rd_shadow_box_ab`, `rd_shadow_box_ab_gl`): the dumps of
  `tools/shadow_box_dumps.txt` (machine-local; a listed dump that is absent FAILS: the Pixel's
  v3677 and v4287, the main gate, the PowerVR phone's v4657 and v2381) with `ICO_RD_SHADOW_BOX=0`
  and `1`, `--pass-dump --stats`, under `ICO_RD_STORE_PLAN=1`, `ICO_RD_VU_PREPASS=0`,
  `ICO_RD_EMPTY_SKIP=0` (with the box on an empty composite would be skipped, which the box off
  cannot; `rd_empty_skip_ab` checks the skip), at the
  PowerVR phone's profile (`ICO_VK_FAKE_LIMITS=moltenvk`, Enhanced 2364x1080, Full height and
  pixel), the Mali-G52's (`ICO_VK_FAKE_LIMITS=mali`, `ICO_VK_FAKE_NO_DST=1`), the B-spline
  composites at Enhanced 2x (`ICO_RD_SHADOW_BSPLINE=1`) and MSAA 4x at Enhanced 2x; the `_gl` twin
  at 1x on OpenGL (llvmpipe). Each pair: every PNG byte for byte, the same draws and passes, `=0`
  off with nothing boxed, `=1` on; v3677 at the phone setting 1 count and 3 composites boxed; the
  phone setting boxes at least one of each over the dumps. No device: SKIP.
- `rd_even_test.c` (`rd_even`, CPU): the pictures' evenness line (`rd_even.c`): ten made-up
  presents with GPU ends at the middles of their quarter-ms bins (1, 2, 3, 5 and under half a
  refresh of 8 ms apart, one end a bound, one present with none, a frame number that went back),
  their content steps (one back), four handed over (the sample to the GPU's end 10, 18, 20 and 28
  ms, the hand-over to it 9, 16, 19 and 27, drains of 1, 2, 1 and 1 ms) and five present-clock notes (clamps below 0 and above 0.999,
  resets forward and back): the sums and the line word for word; a short buffer cut and
  terminated; taken twice: no line; one more present with the refresh unknown, word for word; the
  hand-over's stamps taken once; the steps over gaps not on the grid (2: mean 0.375, deviation
  0.125) and two re-base notes (the shift, the hold, both spreads) in the line; five content notes (a step
  equal to its gap, two repeats in a row, a step over twice its gap: 4 pairs, 2 repeats in 1 run,
  ratios 0 to 2.125, 3 outside 0.5 to 2). Linked with `rd_pace_gpu.c` for the line's last clause
  (left out with no picture as made).
- `rd_pace_gpu_test.c` (`rd_pace_gpu`, CPU): each in-between picture drawn for its GPU work's
  predicted end (`rd_pace_gpu.c`, `rd_pace_gpu_live.c`, `rd_pace_gpu.h`, with `rd_rebase.c`): the
  share and the median; the model's edges, each figure from the code: before its windows are full
  (0), an empty queue (q 0 and the shift 0 to the bit at every picture), one a tick with a full
  queue (0), two a tick with the second queued (q 3, shift 3), the mean's sixteenth, the bound from
  the latencies 24 to 33 (4.5), the ratio hold above (to twice the gap) and below (to half of it)
  after exact ends re-anchor the chain, a bound end, the prediction's error, the four restarts and
  a frame 8 on kept; 10 s simulations at 120 Hz (a frame every 33.37 ms; the window asks when the
  render thread is free, a further picture held when its reads would end past the next closing
  step; the GPU's start the later of the first submit and the end before; each end stamped where
  the render thread's slot wait for it blocked, collected at that wait) of the cage at two a tick,
  the brief's cage pattern (0.15-0.35 and max(0.506, that + 0.29-0.45) of a tick, the GPU 14.8-15.6
  ms), stages 3 and 5 at two to three a tick, three a tick, a GPU-bound one to two, one a tick and
  an idle GPU, the moments through `rd_rebase_raw` with and without the model: the same pictures
  and hand-overs, never back, no repeat, the mean shift within 16 times q's range over the
  pictures plus the held ones, every step's, the 2-refresh class's and the GPU's ends' deviation
  under half of today's (the cage .210 to .040, the brief's .149 to .068, stages 3 and 5 .101 to
  .039, three .119 to .048, one to two .170 to .066), one a tick and an idle GPU today's moments
  to the bit; the owner's cage log's runs (lines 3846-3851, 4109-4113 and 4275-4280 of its first candidate's log) (frame, alpha and GPU ms) after
  a warm cage: today's steps the log's alphas', the uneven two under half of today's distance from
  the GPU's ends, every step within the two shifts' bounds; the queue capped at one picture's G
  over unmeasured pictures; the renderer's side: paced (not applicable), 600 measurements without
  a clock (not applicable, the clause word for word), the ring's measurements, the row off by
  default (on under `ICO_RD_PACE_GPU=1`) with the shift drawn 0 and the model's computed, the
  clause word for word with an exact end's error of -2 ms and one shift at the bound, the block
  line's counts, a full ring's drop, the start line word for word and cut; the desktop's defaults
  (no display times, not Android: the screen's rule on but placing nothing, the own timer not
  drawn), the rules picked as `presentRebase` does over 600 cage pictures: the hand-over rule's
  value to the bit (skipped under a tests' switch or where the own timer is drawn).
- `rd_as_made_timer_test.c` (`rd_as_made_timer`, CPU): the own timer (`rd_as_made_timer.c`): its
  edges (the first picture on the target, a steady interval exactly, the quarter pull, a held
  slot taken whole, an error over half a tick, the minimum step, the restarts, the decision
  `rd_as_made_timer_decide` (on by default on Android, the hand-over rule elsewhere, the define 0,
  the tests' switch both ways, each with its words) and the switch against it); 10 s
  simulations with a 120 Hz mailbox display of the cage, the brief's cage pattern, stages 3 and 5,
  three a tick, the GPU-bound opening, one a tick, two to three a tick and back, a 200 ms stall,
  the three rules over the same pictures (every step, clamps, largest step, speed against the
  hand-overs, the GPU's ends and the display): the timer never back, no repeat, no clamp, held in
  its frame's tick at most 20 times in 10 s, within half a tick of the hand-over rule, evener than
  the hand-over wherever two or more a tick; the owner's cage log's three runs.
- `rd_glass_test.c` (`rd_glass`, CPU): the on-glass line (`rd_glass.c`): a pair per rule against the
  display's gap, its class, the hand-overs and the GPU's ends, the shares; not steps; the join of
  pictures and display times by id in either order with a present never shown; the line word for
  word; the screen's rule's measure, its score (on it, late, early, further), its ring, the display's own figures.
- `rd_pace_screen_test.c` (`rd_pace_screen`, CPU): the screen's predicted refresh (`rd_pace_screen.c`): the expected time on the glass, the latch's and the grid's fits, the score; the placement's edges and fall-backs (the value drawn otherwise to the bit); 10 s simulations of the owner's phone's regimes with a display model (grid, latch lead, mailbox, late times): the four rules' speed on the glass, the holds, never back, the content's rate over the glass; the log's later regimes (kept frames in runs, the cage's slow tail, a latch or GPU time that moves) with each refinement on in turn; the even cadence (`rd_pace_even.c`): N, its band, the hold, a late hand-over, the bias, and its simulations (shown a second, deviation, N's changes); the start lines (the screen's on and off, the even
  cadence's short off line and its full on line), the clauses, the ledger's rows (the screen's on by
  default); the even cadence off: no hold, no aim, no desired time, no clause, its row never on.
- `rd_cpu_phase_test.c` (`rd_cpu_phase`, CPU): the render thread's CPU phases (`rd_cpu_phase.c`):
  two pictures of one frame folded (2 pictures, 1 tick, 1 hashed, 6 clock reads, the look-ups
  folded once and `g_rdCpu` emptied), taken twice (empty), no line without a picture, and the
  line's words from a made-up window, character by character.
- `rd_hash_test.c` (`rd_hash`, CPU): the per-draw lookups' hash (`rd_hash.c` `rd__hash_words`,
  the uniform blocks' cache and the pipeline index): seven values on a little-endian host worked
  from its steps (no bytes under two seeds, 3, 8, 15 with a seed, a DrawCB's 144 under two seeds;
  a big-endian host skips them), the same 144 bytes at every alignment 0..7 the same value, the
  bytes past the length never read (lengths 0..40), two seeds two values.
- `rd_no_volume_test.c` (`rd_no_volume`, CPU): a shadow window with no volume (`rd_no_volume.c`):
  the scan on the corpus title frame rd-00400's list 3 (taken; the three levels and three
  composites left undrawn, nothing else), a strip in the window (a volume), a foreign reader of
  SHADOW1, a sprite of the chain the box scan named neither a level nor a composite, a clear of
  SHADOW2 and a draw into the count target after the resolve, a level into a chain target twice
  (the chain); a draw inside the window, a second window, a resolve on another target, a window
  never resolved, a resolve with no reset, a level writing a depth (the form); a composite's DATE by
  the snapshot (DATE); an unknown command after the resolve (unknown) and before the window (no
  matter); SHADOW0 or the count target cleared before the window (the chain); no window; the gate
  (the class, `ICO_RD_NO_VOLUME=0|1`, MSAA, the pass window, the exact blend, GS state as shader
  data, DATE); the 10 s clause and the `--stats` words from made-up records.
- `rd_no_volume_ab.cmake` (`rd_no_volume_ab`, `rd_no_volume_ab_gl`): the dumps of
  `tools/no_volume_dumps.txt` (machine-local; a listed dump that is absent FAILS: the corpus boot
  frames rd-00400 and rd-00600 and the Pixel's opening v746, a window with no volume, and rd-00200,
  the Pixel's v3677 and the cage frame v4651, windows with volumes) with `ICO_RD_NO_VOLUME=0` and
  `1`, `--pass-dump --stats`,
  under `ICO_RD_STORE_PLAN=1`, `ICO_RD_VU_PREPASS=0`, at the PowerVR phone's profile with its late
  passes (`ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_LATE_PASS=1`, Enhanced 2364x1080, Full height and
  pixel), lavapipe at 1x (DATE by the snapshot: not taken), lavapipe at 1x with
  `ICO_RD_DATE_PATH=stencil` (taken, the count target at scale 1) and the Mali-G52's
  (`ICO_VK_FAKE_LIMITS=mali`, `ICO_VK_FAKE_NO_DST=1`: not taken); the `_gl` twin at 1x on OpenGL
  (llvmpipe: not taken). Each pair: every PNG byte for byte; `=0` off with none kept; where nothing
  is taken the same passes and traffic figures; where a window is taken, from the code: a pass less
  a window at the phone setting (the count's, the three levels' and list 3's SCENE pass not begun,
  four zero clears begun) and two at the stencil row (the reset's own stencil pass too) less one a
  window whose composites `=0` skips as empty too (its fog reads the depth outside the pass: list
  3's SCENE pass begun in neither run), the colour stored down by the kept pass's W x H x 4 (not
  for those windows), the colour loaded by that and the levels' texels x 4,
  at the phone setting the cleared up by the levels' and the depth and stencil each way down by W x
  H x 5 (the Apple class loads and stores the stencil with the depth), each within the 0.01 MB
  rounding of two printed figures; the phone setting takes at least one window. No device: SKIP.
- `rd_stencil_selftest_test.c` (`rd_stencil_selftest`, `rd_stencil_selftest_poison`, lavapipe with
  `ICO_RD_FOG_PATH=local` and `ICO_RD_STENCIL_PACKED=0`, the second also `ICO_RD_STORE_POISON=1`;
  77 without a device or the local read, after its CPU cases): the stencil outside the shadow window
  (`rd_stencil_selftest.c`): the 10 s clause and the `--stats` words from a made-up record; at a
  640x448 Enhanced scale-2 Full pixel 16:9 scene, `rd_fog_selftest` on the local path, the stencil
  self-test passed (its eight texels the GS arithmetic and the forced replay's), the stencil not
  forced, the cut's replay with 2 or more stencil stores and 1 or more loads not asked (each at most
  a SCENE of texels); the decision on the desktop class: `=1` forced, unset with the local path
  forced (not the PowerVR class), the in-place path not, the copy and buffer paths forced, `=0` not;
  no validation error.
- `rd_stencil_ab.cmake` (`rd_stencil_ab`): the dumps of `tools/stencil_dumps.txt` (machine-local; a
  listed dump that is absent FAILS: the Pixel's v3677 and v4287, the PowerVR phone's v4657,
  the main gate, the drained-room ripples' 223424) with `ICO_RD_STENCIL_PACKED=1` and `0`,
  `--pass-dump --stats`, under `ICO_RD_STORE_PLAN=1`, `ICO_RD_STORE_POISON=1`,
  `ICO_RD_VU_PREPASS=0`, at the PowerVR phone's profile (`ICO_VK_FAKE_LIMITS=moltenvk`, Enhanced
  2364x1080, Full height and pixel) and the Mali-G52's (`ICO_VK_FAKE_LIMITS=mali`,
  `ICO_VK_FAKE_NO_DST=1`, 1x at 1612x720). Each pair: every PNG byte for byte, the same draws,
  passes, colour and copy MB; `=1` with the depth and nothing not asked; `=0` inside the window
  only; the depth and stencil MB stored and loaded fall by exactly the `=0` run's stats (MB
  stored, MB loaded) within two hundredths, the MB cleared rise by at most its MB loaded (the
  poisoned loads); each setting that compared a pair leaves out a stencil store. No device: SKIP.
- `rd_store_plan_ab.cmake`: the same dumps and those of `tools/vufuse_dumps.txt` (the phone's three
  frames) three times under
  `ICO_RD_BLEND=fixed` and `ICO_RD_STORE_POISON=1` (a depth or stencil load the plan leaves
  undefined clears to 0.5 and 0x5A instead: lavapipe keeps an attachment's contents whatever its
  load and store ops say, measured on Mesa 25.0.7, so without the poison a wrong plan draws the same
  bytes there): every depth store kept (`ICO_RD_STORE_PLAN=0`), the store plan, and every depth
  store dropped (`ICO_RD_STORE_PLAN=all`, a wrong plan), at three settings: the phone's profile
  (`ICO_RD_VU_PREPASS=0`, `ICO_RD_FOG_PATH=local`, `ICO_RD_DATE_PATH=stencil`, `--enhanced
  --resolution 2x --aspect 2.189 --present 2364x1080`; a narrower dump skips it) and the record at
  Enhanced 4:3 and 16:9 2x: with the plan every named target's PNG (and `PRESENT.png`) byte for
  byte against every store kept, `store plan off` with nothing dropped, `on` and `all` with the same
  passes and passes with a depth and the same colour stores dropped (the shadow count's colour,
  poisoned to 0x5A: no named target may show it), `all` dropping every depth store, the plan dropping some over the
  whole list, and the wrong plan changing at least one target over the whole list (the poison shows
  a dropped store that a later pass reads); each run's counts printed (`rd_store_plan_ab`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)). SKIP as `rd_vustage_ab`.
- `rd_cull_ab.cmake`: the dumps of `tools/occlusion_corpus.txt` and `tools/native_frame_dumps.txt`
  (the missing ones named and skipped) under `ICO_RD_BLEND=fixed` with `--pass-dump --stats`
  three times, `ICO_RD_CULL=off`, `frustum` and `full`, at Original 4:3 presented at 640x480,
  Enhanced 4:3 2x, Enhanced 2x at the dump's own aspect where `--no-device --list` says it is wider
  than 4:3, and with `--aa msaa4` (Enhanced 4:3 2x) on the first dump and on the dumps of
  `tools/cull_msaa_dumps.txt` (`-DMSAA_LIST`: frames whose MSAA epoch closes inside a list by a
  read of SCENE; one the corpus lists do not name runs that setting only): every named target's PNG (and
  `PRESENT.png`) of the frustum and the full runs byte for byte the off run's, the off run's `cull
  off: 0 of 0`, the frustum run's `cull frustum: N of M` with N at most M, the full run's `cull
  full: N of M (F frustum, O occluded)` with the frustum run's M, F its N and N = F + O, and draws
  skipped over the corpus (the occluded total printed); each run's counts printed;
  `-DMAX_DUMPS=n` takes the first n dumps (`rd_cull_ab`, TIMEOUT 5400, and its OpenGL twin
  `rd_cull_ab_gl`; [RENDER.md](RENDER_PERF.md#culling)). SKIP as `rd_vustage_ab`.
- `rd_vis_hiz_corpus.cmake`: the rows of `test/rd_vis_hiz_corpus.txt` (the dumps of
  `tools/occlusion_corpus.txt` and issue 179's frame at Original 4:3 640x480, Enhanced 4:3 2x and Enhanced 4:3 2x with
  `--repeat 3`, the last replay's counts, so the collect cache's; the missing dumps named and
  skipped) through `rd_replay_tool --cull full --stats` under `ICO_RD_BLEND=fixed`, once with
  `ICO_RD_CULL_HIZ_FAST=0` (the plain pass) and once with the fast collect and
  `ICO_RD_CULL_HIZ_CHECK=1`: the 26 counts of `stats: cull full:` (draws skipped, walked, frustum,
  occluded, occluder triangles, past the budget, cells, tested, resets) and `stats: cull
  occlusion:` (the 17 reasons) of the fast run equal to the plain run's and to the table's,
  `stats: cull occlusion cost:`'s `check: N checked, 0 wrong`, `fast off` and `fast on` as set;
  each row's cull, occlusion and collect ms of both runs printed, and their sums (the table made by
  the plain pass; made again only when the culler's rules change)
  (`rd_vis_hiz_corpus`, TIMEOUT 1800; [RENDER.md](RENDER_PERF.md#culling)). SKIP as `rd_vustage_ab`.
- `rd_thread_ab.cmake`: the same dumps and those of `tools/vufuse_dumps.txt` through
  `rd_replay_tool --render-thread off`, `sync` and `on` (every replay, the interpolation and the
  renderer's calls on a thread of the renderer's own, `port/render/rd_thread.h`), with
  `--pass-dump` at Original (640x480) and Enhanced 4:3 2x (1280x960), and for every F12 zip the
  blended present at 0.5 between its two frames: every target's PNG of `sync` and `on` byte for
  byte `off`'s, and the threaded runs saying `render thread: sync` / `on`; the blended present is
  made three times in a row in every mode (`--repeat 3`), the `on` run handing the three over
  back to back as the window does, and must say `submit thread: ended (N` with N at least 3
  (their submit and present on the submit thread, `port/render/rd_submit_thread.h`) and, where
  the device says `rhi_vk: frame slots: apart`, `submit thread: the frame slots apart` (the second
  and third recorded while the one before is submitted, RHI.md "Frame slots and the submit
  thread"); the last picture's PNG byte for byte `off`'s
  (`rd_thread_ab`; [PLATFORM.md](PLATFORM.md#the-render-thread)). SKIP as `rd_vustage_ab`.
- `rd_constant_reads_ab.cmake`: the same dumps and those of `tools/constant_reads_dumps.txt` (the
  PowerVR phone's two frames, the aura with no aura object) twice, every post sprite reading
  its source (`ICO_RD_CONSTANT_READS=0`) and a source holding only its clear drawn as its colour, at
  four settings under `ICO_RD_BLEND=fixed`: the phone's profile (`ICO_RD_VU_PREPASS=0`) at 2x
  (`--enhanced --resolution 2x --aspect 2.189 --present 2364x1080`) and at its Resolution Auto size
  (`--resolution 2364x1080 --full-height --full-pixel`, the same aspect and present; a narrower dump
  skips both), and the record at Enhanced 4:3 and 16:9 2x: every named target's PNG (and
  `PRESENT.png`) byte for byte, `constant reads off` with none served against `on`, the same target
  reads counted by both (served plus kept against the other's kept), and every phone frame served at
  both phone settings; each pair's counts printed (`rd_constant_reads_ab`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#a-cleared-source-read-as-its-colour)). SKIP as `rd_vustage_ab`.
- `rd_fades_fold_ab.cmake`: the dumps of `tools/fades_fold_dumps.txt` (the forest's
  `rd-00720` and `rd-01076`, the queen's `rd-00100`, the PowerVR phone's F12 zip `frame-20261028-173610-v4657`; machine-local) and,
  with `ICO_BOOT_DUMP_DIR`, the start-up's frames 115 to 123, each first listed (`--list
  --no-device`: list 11's last twelve commands in the log, so it shows which dumps end with the
  fade, the letterbox or the brightness), then replayed with `--pass-dump --stats` with list 11's
  last full-frame lerp sprites drawn into SCENE (`ICO_RD_FADES_FOLD=0`, the tests' way to draw
  them as before) and with the fold (the default; its log line must name the GS's floor under the exact blend and the
  nearest level under the fixed-function one, and a reduction that did not take a planned fold
  fails), under
  `ICO_RD_BLEND=attachment` and `fixed`, at Original (`--present 640x480`), Enhanced 2x
  (`--present 1280x960`) and the phone's Resolution Auto (`--resolution 2364x1080 --full-height
  --full-pixel --aspect 2.189`, `ICO_RD_VU_PREPASS=0`; a narrower dump skips it): `fades fold off`
  with none folded against `on`; every target's PNG but SCENE's (which holds the picture without
  the sprites by design) byte for byte where nothing folded and under the exact blend where the log
  says the steps met SCENE's texels before the GS filter, elsewhere within two levels on every
  channel (the measured bound, [DIVERGENCES.md](DIVERGENCES.md) VD-27), the channels a level apart
  printed per target by `port/render/test/png_level_diff.py`;
  some dump must fold (`rd_fades_fold_ab`; [RENDER_POST.md](RENDER_POST.md#the-fades-fold),
  [DIVERGENCES.md](DIVERGENCES.md) VD-27). SKIP as `rd_vustage_ab`.
- `rd_present_scene_ab.cmake`: the present from SCENE against the reduction drawn: the dumps of
  `tools/present_scene_dumps.txt` (the PowerVR phone's F12 zip `frame-20261110-194925-v3677` and
  its rc dump `v4287`, issue 164's paused frame, the main gate's frame; machine-local; a listed
  dump that is absent fails) replayed by `--pass-dump --stats` with `ICO_RD_PRESENT_SCENE=0` and
  `=1` at the phone's Resolution Auto under its profile fakes (`ICO_VK_FAKE_LIMITS=moltenvk
  ICO_RD_VU_PREPASS=0`), a 16:9 window (1920 x 1080) and Enhanced 2x 4:3: every target's PNG but
  `PRESENT.png` byte for byte (DISPLAY drawn on demand for its read), `PRESENT.png` within two
  levels (`port/render/test/png_level_diff.py`, its counts printed), the off run `off` with none
  skipped, a run with the reduction not drawn one pass and some colour MB stored fewer, 2x kept;
  on the first dump at the phone's setting the CRT filter, FSR 1.0, the test's temporal upscaler
  and Downsampling keep the reduction with their reasons and every PNG byte for byte, and the
  mirror takes it; the crack fill's swap as often in the on run as in the off run, and at the
  phone's setting some dump with the reduction not drawn and the swap taken in one replay (the
  display layer's switch on, its default: the layer needs a temporal upscaler); the first dump
  presented 31 times (`--time 30`) prints the 10 s `present from SCENE: N a replay` line with N
  above 0; some run must leave it undrawn (`rd_present_scene_ab`). SKIP without a device.
- `rd_crack_late_ab.cmake`: the crack fill after lists 4-6 against pass dumps of the build that drew
  it before the fog: each line of `tools/crack_late_dumps.txt` (`<setting> <dump> <baseline
  folder>`, machine-local: six corpus dumps at the gate's Enhanced 2x 4:3 against
  `build-host/tmp/v095/after/w3/pd`, and five of the PowerVR phone's frames at its Resolution Auto,
  2364x1080 at 2.189 under its profile's fakes, against `build-host/tmp/v098/evidence/cheap/base-phone`)
  replayed with `--pass-dump --stats` under `ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0
  ICO_RD_CULL=off` (the baselines' own: they replayed every draw, and the draws the visibility
  test leaves out keep every pixel but take their copies with them, so the phone's copy count is
  the baseline's minus one only with every draw replayed; the culler's own gate is `rd_cull_ab`):
  the stats line says `crack fill on: P passes` (P 0 where the fill was the
  first draw of the aura's paste pass, else 1) and `1 fills after lists 4-6`,
  or, on a dump with no fog of SCENE above 1x (`0 passes, 0 pixels filled (0 replays counted;
  scale 0x0, ...; 0 fogged replays above 1x not filled; 0 fills after lists 4-6`: the v3086 and
  v1985 frames), every target the baseline's bytes; every target within 64 levels on at most 0.2 %
  of its pixels (the bound, the pixels apart and the largest difference printed per target;
  [DIVERGENCES.md](DIVERGENCES.md) VD-20), or, past it, the run replayed once more with
  `ICO_RD_CRACK_FILL=0` and the glow's mask (WORK2, `cleanUpFB`'s depth test: the late fill leaves
  the crack's depth) that run's byte for byte, and each target past the bound no farther from the
  baseline than the no-fill run's (no more pixels apart, no larger difference); at the phone's
  setting the stats line's copies one fewer than the baseline log's and its passes no more. Every
  run that drew the fill is replayed again with `ICO_RD_CRACK_PLACE=7` (the fill at the start of
  list 7, the place before `rd__crack_fill_place` moved it after the glow's `cleanUpFB` or
  into the aura's paste pass): every PNG that replay's bytes, the run's passes no more than its,
  and at the phone's setting a run whose stats place the fill later (`1 fills in the aura's paste
  pass` or `1 fills after the glow's alpha`) at least one pass fewer. A
  missing dump or baseline is skipped; none: SKIP. The baselines are pictures of the fill before the
  fog; another change to the picture needs baselines of its own before this test can tell the two
  apart.
- `rd_aura_box_ab.cmake`: the dumps of `tools/aura_box_dumps.txt` (`tools/vustage_dumps.txt`'s and
  the PowerVR phone's three F12 zips `frame-20261028-{105852-v4103,151709-v2199,173610-v4657}`;
  machine-local) replayed with `--pass-dump --stats` under `ICO_RD_BLEND=fixed` with the aura's
  read of the picture into FEED128 in one pass (`ICO_RD_AURA_BOX=0`) and in two
  (`ICO_RD_AURA_BOX=1`, `rd_aura_box.c`), at the phone's Resolution Auto (`--enhanced --resolution
  2364x1080 --full-height --full-pixel --aspect 2.189 --present 2364x1080`,
  `ICO_RD_VU_PREPASS=0`; a narrower dump skips it) and at Enhanced 2x 4:3 (`--present 1280x960`):
  every PNG (FEED128, SCENE, `PRESENT.png` and the other named targets; ROWBOX is the renderer's
  own texture, never written) byte for byte, `aura box off: 0` against `on`, the two-pass run's
  passes and draws the other's plus one each a read in two passes; some dump must read in two
  passes (`rd_aura_box_ab`). SKIP as `rd_vustage_ab`.
- `rd_shadow_merge_ab.cmake`: the dumps of `tools/shadow_merge_dumps.txt` (`tools/vustage_dumps.txt`'s,
  the PowerVR phone's three F12 zips and its F12 frame of the heavy stage 6 room `frame-20261102-222516-v2325.zip` in
  the effort's evidence folder; machine-local, a missing one skipped) replayed with `--pass-dump
  --stats` under `ICO_RD_BLEND=fixed` and `ICO_RD_STORE_POISON=1` with the shadow volumes in a pass
  of their own (`ICO_RD_SHADOW_MERGE=0`) and in the scene's open pass (`=1`, `rd_shadow_merge.c`),
  at the phone's Resolution Auto under the MoltenVK fakes (a tile-based class) and at Enhanced 2x
  4:3: every PNG byte for byte (the count targets' clears the on run leaves undrawn are drawn with
  the poison's 0x5A there, so a reader would show), `shadow volumes in the scene's pass off: 0 of R
  counts, 0 passes armed; 0 count targets' clears not drawn` against `on: M of R`, the same draws,
  the on run's passes the off run's less M; some reset must be taken (`rd_shadow_merge_ab`). SKIP
  as `rd_vustage_ab`.
- `rd_pass_area_ab.cmake`: the dumps of `tools/pass_area_dumps.txt` (`tools/vustage_dumps.txt`'s,
  the PowerVR phone's three F12 zips and its F12 frame of 2026-11-02
  `frame-20261102-091017-v15677.rddump` in the effort's evidence folder; machine-local, a
  missing one skipped) replayed with `--pass-dump --stats` under `ICO_RD_BLEND=fixed` with the
  passes' render areas off (`ICO_RD_PASS_AREA=0`) and on (`=1`, `rd_pass_area.c`, on whatever the
  device: lavapipe clips every draw to the area, so an area that misses a texel shows), at the
  phone's Resolution Auto (as `rd_aura_box_ab`) and at Enhanced 2x 4:3: every PNG byte for byte,
  `pass area off: 0 passes` against `on`, the same draws, the on run's passes the off run's plus
  at most its passes ended early; the phone's frames (`frame-20261028-*`, `frame-20261102-*`, the
  phone's F12 frame of 2026-11-02 11:42 and its blended twin among them) at the phone's setting
  must give some pass an area or name `its draws cover the picture` among the passes kept whole
  (the tool's `--stats` clause; the aura's paste and list 11's fades are full-frame sprites, so the
  stage 6 frames have no area to give) (`rd_pass_area_ab`). SKIP as `rd_vustage_ab`.
- `rd_glow_cheap_ab.cmake`: the glow's cheaper passes ([RENDER.md](RENDER.md#the-glows-passes):
  WORK2's fill in the mask's pass, cleanUpFB in the fog's) against stored pass dumps, as no switch
  turns them off: the 14 glow dumps of `tools/glow_cheap_dumps.txt` (machine-local, a missing one
  skipped) replayed with `--pass-dump --stats` under `ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0` at
  the PowerVR phone's profile on lavapipe (`ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0`,
  2364x1080, Full height, Full pixel, the dump's own aspect, presented at 2364x1080; baselines
  `build-host/tmp/v098/evidence/glow/pd-phone`, made by the tool before the cuts) and at the gate's
  Original and Enhanced 2x 4:3 (baselines `build-host/tmp/v095/after/w3/pd`, named as
  `gate-wave.sh` names them): every baseline PNG the same bytes or within `-DBOUND` levels (0,
  `png_level_diff.py`), the `glow N passes` count at least two fewer than the baseline's at the
  phone's setting and no more at the others; each run's line prints the passes before and after
  and its largest difference. The run's own count meets the rule with the crack fill drawn (it
  is drawn after `cleanUpFB`'s two sprites, which stay in the fog's pass: [the fill's
  place](RENDER_POST.md#the-fills-place-after-list-7)), and at the phone's setting a run whose
  stats say the fill was drawn says `1 fills after the glow's alpha`. The seven frames the
  crack fill changed (v4251, the loss frame, rd-00720, the two issue-91 frames, v2567 and its
  previous frame) are compared at the phone's setting and Enhanced 2x with refreshed pictures
  (`-DBASE_REFRESH`, `build-host/tmp/v098/evidence/glow/pd-w15`: main 7832a2e4f's, each first
  checked to have the no-fill replay's WORK2 and no target farther from the old baseline than
  that replay), the old baseline's log still giving the passes before. A setting without a
  baseline is skipped; nothing compared: SKIP (`rd_glow_cheap_ab`). A baseline refresh for a
  later change to the glow's drawing replaces the folders. Those runs hold the glow's mask fold
  off (`ICO_RD_GLOW_FOLD=0`), and each dump at each setting runs again with it on
  (`ICO_RD_GLOW_FOLD=1`, into `<name>-<setting>-fold`): every PNG but `WORK2.png` (which the fold
  leaves undrawn) the same bytes as the run without it, `PRESENT.png` too at the phone's setting;
  a run whose stats say `glow: WORK2 fold on: 1 folded` has at least one glow pass fewer, a kept
  one (only the lamps drawn into WORK2 after the mask may keep it: any other reason fails) as
  many; the folds reading the sun's fans' WORK2 counted; at each setting that ran, at least
  one run folded ([RENDER_POST.md](RENDER_POST.md#the-glows-mask-in-its-reduction)). Its runs
  set `ICO_RD_GLOW_LIGHT=0` (the chain drawn whole, as the baselines drew it; so do
  `rd_crack_late_ab`'s).
- `rd_glow_light_ab.cmake`: the glow's light plan
  ([RENDER_POST.md](RENDER_POST.md#the-glow-where-it-adds-light)) against the chain drawn whole
  (`ICO_RD_GLOW_LIGHT=0`) on the 55 glow dumps of `tools/glow_light_dumps.txt` (machine-local,
  every glow dump on the maintainer's machine; a listed dump that is absent fails), each replayed
  twice with `--pass-dump --stats` at the PowerVR phone's profile on lavapipe
  (`ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0`, 2364x1080, Full height, Full pixel,
  presented at 2364x1080), at Original 4:3 and at Enhanced 2x 4:3 presented at 1280x960, each pair
  with the other packages' pass-dependent switches pinned off in both runs (`ICO_RD_LATE_PASS`,
  `ICO_RD_AURA_PASTE`, `ICO_RD_CRACK_SWAP`, `ICO_RD_PRESENT_SCENE`, `ICO_RD_SHADOW_BOX`,
  `ICO_RD_STENCIL_PACKED` = 0: without the glow's chain another pass is open at list 11, so the late
  pass's join, VD-31, would apply in one run and not the other), plus the phone's setting with every
  switch at its default, where SCENE, DISPLAY and PRESENT may differ within VD-31's bound (3 levels,
  800 pixels) and every other target's RGB not at all: every
  PNG's RGB 0 levels apart (`png_level_diff.py --channels RGB`), the alpha 0 levels on DISPLAY,
  PRESENT and every target but WORK1, WORK2, WORK3, WORK3_HELD and SCENE, and on those too where
  the whole run's WORK2 holds no alpha 192 (`--count-alpha 192`: no fill the mask kept), at every
  row (Original 4:3 at 1x, Enhanced 1x at the phone's profile, Enhanced 1x under a Mali device's
  identity `ICO_VK_FAKE_VENDOR=0x13B5 ICO_VK_FAKE_LIMITS=mali`, and an Original 1x row at the
  defaults with the VD-31 bound, besides the phone's and Enhanced 2x); at least one dark run at
  every row but the defaults'; under dark the stats' `N passes in the
  chain` at most 1 (WORK1's zero draw in a fresh renderer) and `glow N passes` fewer than the
  whole run's, one fewer under lamps, no more otherwise; at least one dark run at the phone's setting and
  at Enhanced 2x (`rd_glow_light_ab`; SKIP without a device).
