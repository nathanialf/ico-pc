# Test programs: port/render/test

The renderer's test programs, one entry each: what it checks and its ctest names. This is the
`port/render/test` section of [TESTING_PROGRAMS.md](TESTING_PROGRAMS.md) in a file of its own (the
repo keeps every file under 256 KiB); the entries read as that page's do
([Reading the entries](TESTING_PROGRAMS.md#reading-the-entries)). The framework, labels, shards and
the render gates are [TESTING.md](TESTING.md)'s.


The tests whose pictures or counts are golden or are about something other than the region rule
(`rd_interp_test.c`, `rd_pixel_test.c`, `rd_filter_test.c`, `rd_state_test.c`, `rd_mesh_test.c`,
`rd_display_change_test.c`, and `rd_native_test.c` but for `checkStripCamera`) set
`ICO_RD_REGION_RULE=ps2` first: they hold the PS2's region rule's pictures. The camera rule, the
default, is tested by `vu1`, `rd_native` (`checkStripCamera`), `rd_region_stats`,
`rd_region_camera_ab` and `rd_sideband_cards`' camera half ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-camera-rule)).

- `fog_lut_test.c`: the depth fog's game side, the same on every compiler (`fog_lut`;
  `fog_lut_arm64` under qemu).
- `gltf_test.c`: the glTF reader and writer, bit-exact round trips, GLB, a Blender-style file,
  refusals (`gltf`).
- `gsb_cull_test.c`: the game's cull against the blended pictures' cameras and the windows that
  scale with the width; with the disc, a stage (`gsb_cull`, `gsb_cull_stage`), and the disc-wide
  sweep (`gsb_cull_sweep.1of8` .. `8of8`, 77 without the disc image). `--stage DISC` is st18a at
  the camera of the reporter's dump i76 and the 768 camera steps about it (every part a picture
  between the ticks shows is registered by this tick; the one-camera control leaves some out).
  `--sweep DISC` generalises it to every stage folder of the disc (`object/sdf/stNN?/model/`, the
  member index as the `rd_mesh` disc walk builds it, numbered in index order, shard q of 8 takes
  the stages numbered q mod 8) at the camera of every line of `tools/sweep_cameras.txt` (stage,
  dump path; a .rddump or an F12 zip; the dump's view and zoom; a line whose dump is absent is
  skipped; `builtin:i76` is the i76 camera of st18a) and the 768 steps about each, every 4th pair
  by default (`--sweep-step N`, 1 is the whole sweep), at 16:9 (688 GS pixels wide) and 48:9
  (2048). Per stage and aspect it counts the mechanisms that leave an in-view part or triangle
  undrawn: the box cull (parts a picture between the ticks shows that this tick did not
  register: `box_gap` not drawn, `box_hidden` drawn as the tick before's), the blend span (parts
  registered by three cameras against one, and the one-camera control's losses), and the region
  rule's drops through the CPU oracle (`vu1ref_region_draw` with the corners' real region bits,
  `extended` on as at every aspect wider than 4:3, the strip joins as `rd_mesh.c` joinStrips and
  the card test): triangles dropped for a corner behind the eye (step 6) and cards (step 5), at
  the ticks (`tick_behind`, `tick_card`) and at the pictures between (t = 1/16 .. 15/16 through the
  blended camera, `between_behind` after the ticks' rescue, `between_card`), these four counting
  only the drops that stay dropped; and the drops the region rule's side bands draw beside the 4:3
  picture (`tick_beside`, `between_beside`: the rule's last step,
  `VU1REF_DRAW_BESIDE`, on both swept aspects): a drop faces the eye by `vu1ref_region_facing` of
  the corners' h (float) with the camera matrix's sign (`vu1ref_region_mirror`; the parts sit at the
  identity) and the triangle's winding against the p2o strip records' normals (index at +6, summed,
  against cross(b - a, c - a) in strip order: the EE's table); a part without normals is never
  beside; the ticks' rescue comes first and a tick's "shown" never counts a beside draw. The two
  beside counts are printed in the table and the BASELINE LINES and never held (the baseline's
  beside lines are read and ignored); `--eye` adds the rasteriser's hole pixels. The table prints at the end of each shard with its BASELINE LINES;
  `tools/sweep_baseline.txt` holds them (`STAGE ASPECT MECH COUNT`, `sweep_step N`): a count above
  its line fails, below prints "baseline can be lowered", a stage with no line is held to 0 once
  the file has any. `--out DIR` (the folder exists) writes the report to `DIR/sweep-KofN.txt`,
  `--only STAGE` runs one stage. The winding per stage is `rd_mesh`'s `winding_disc` cases
  (`winding stage NAME` lines), not repeated here. The part's strip joins use the p2o's
  strip records for the VU stream's strips and the models are placed at the identity; both are
  approximations the table's reader should know.
- `modelpack_test.c`: model packs end to end on synthetic models under the build tree
  (`modelpack`).
- `rd_alpha_plan_test.c`: the alpha/blend planner on the CPU ([RENDER.md](PIPELINES.md),
  the alpha plan): every ATST x AREF x As range under AFAIL KEEP and the FB_ONLY split against
  `gs_alpha_pass` over every As of the range, the other AFAIL shapes pixel by pixel, the blend
  identities over every Cs and Cd (`gs_blend_reg_ch`, `gs_dst_channel`), a model of the
  fixed-function path over every blend mode, PABE, FBA, COLCLAMP, FIX and range (the planned
  passes store the pixel the plan before stores), the ranges of screen prims, meshes, grids and
  particles (a mesh's span kept for its replay when its record has no stream serial, across
  replays while its serial holds, read again under a new one), and `rdtex_alpha_range` against
  every texel and two- and three-texel blends (`rd_alpha_plan`).
- `rd_blur_test.c`: `staticBlur.c`'s full-screen effects on rd (`rd_blur`, sharded); its CPU
  part checks the open border's strips (`checkBorderStrips`: pixels 0, 1 and W - 2, W - 1 alone,
  each with column 2's or W - 3's U, plain and mirrored, the hardware sprite's S the same); the
  `above1x` shard runs the aura's chain at Enhanced 4x against the CPU model with the above-1x
  box (`above1x_ref.c`) for the decimations and the GS's bilinear at the finer grid for the work
  buffers drawn back over the frame, the `effects_scaled` shard the seven post modes (the glow's
  and the depth of field's pastes) at Enhanced 2x the same way, no B-spline in either, and every
  scale-1 comparison checks that no sprite took a kernel (`checkAbove1x`, `checkEffectsScaled`,
  `compareAll`); the `blur_fold` shard runs the seven post modes with the blur pairs folded
  (`RdProfile.blurFold`, `fx_blur2_ps`), every target against the CPU model of the two passes
  exactly, nine folds a chain, every pipeline created enumerated (`checkBlurFold`); the `fx_plan`
  shard runs the seven post modes and 40 motion blur frames with the frame-size sprites' plan
  (`rd_fx_plan.c`), every target against the CPU model exactly, the constant fold and the
  premultiplied paste counted, then with the scene passes readable and the destination by local
  read (`fx_local_ps`) where the device has local read, post mode 7's DATE subtract into SCENE among
  the local reads (`fxLocalDate` 1 or more, none without the local read), its self-test passing
  (`checkFxPlan`); the
  `fix128_skip` shard holds the plan off (it counts the skip's snapshots); the
  `motion_blur_smooth` shard (`rd_blur_smooth_test.inc`) runs Motion blur: Smooth against
  Original: a still picture within 1 at 1, 4 and 10 presents a tick, a moving bar's four presents
  a tick one DISPLAY under Original and four under Smooth with the ghost after the tick within 2,
  and the restart after a tick without the motion blur holding no ghost
  (`checkMotionBlurSmooth`); the `motion_blur_strength` shard (`rd_blur_strength_test.inc`)
  runs Motion blur strength over that bar under Original and Smooth: at 100 % the presents the
  bytes of the zeroed setting's, at 50 % of FIX 32 the bytes of the sprite recorded at FIX 16
  (every sprite counted, the last FIX 32 drawn as 16), Original's ghost after the tick within 1
  of the GS formula's at FIX 16 and 32 and Smooth's within 2 of Original's
  (`checkMotionBlurStrength`; `checkStrengthFix`, with the recording checks, the rounding's
  cases); `motion_blur_smooth_scaled` the accumulator and its target at
  SCENE's size at Enhanced 2x with the full-height scene, freed by `rd_shutdown`
  (`checkMotionBlurSmoothScaled`); the `aura_box` shard (`rd_blur_aura_box_test.inc`) draws the
  `above1x` shard's aura frames (the three feedback modes over hard edges, gradients and, every
  other frame, noise) at Enhanced 2x, at the phone's 2364 x 1080 scene (4.617 x 2.411 here, the
  box's edges off the texel grid by a fraction that moves from column to column) and at 1.5x, each
  from a fresh renderer with the aura's two-pass read forced off and on (`rd__aura_box_force`):
  every frame's FEED128 the same bytes, SCENE's and DISPLAY's hashes the same, reads in two passes
  counted with it on and none in one, none with it off, every pipeline created enumerated
  (`checkAuraBox`; [WORK_BUFFERS.md](WORK_BUFFERS.md#the-auras-feedback-read)); the `glow_edge_wide` shard runs the flare at 512, 848 and 2048
  GS pixels wide: one frame against the CPU model (which draws the replay's strip over the
  columns the blur's first pass leaves, `rd__glow_edge_strip`), ten strips a frame with the pairs
  the blur fold draws as one pass (which draw none) and none at 4:3, WORK1's left columns the bytes of its first drawn one, then a frame with nothing cleared
  in it replayed over magenta and over green work buffers (`rd__set_work_clear`): SCENE the same
  bytes both ways, and without the strip different in the left eighth of a wide frame alone
  (`checkGlowEdgeWide`; [WORK_BUFFERS.md](WORK_BUFFERS.md#the-glows-left-edge)). The
  `shrinks_wide_device` shard's model draws the strip too. The `blur_fold_wide` shard runs the
  flare at 512, 688, 2048 and 2560 GS pixels wide with the blur fold on: 9, 9, 8 and 8 pairs
  folded, every target against the CPU model (the strip in it), the same frame replayed with the
  fold off against the model again (the folded and unfolded chains the same bytes at every
  width), and with the strip off every target still the model's (`checkBlurFoldWide`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#the-blur-fold-on-a-wide-frame)). The `glow_fold` shard
  (`rd_blur_glow_fold_test.inc`) runs the seven post modes without the shine into WORK2 at
  Original, Enhanced 2x, the phone's 2364 x 1080 with the full height, and the phone's 848-pixel
  frame (NTSC, aspect 2.189) at 2364 x 1080 with Full pixel (WORK2's crop printed), each frame
  drawn without and with the sun's fans drawn into WORK2 before the mask (the test's fan stub, `s_fanWork2`), each with the glow's mask folded into its reduction (`rd__glow_fold_force(1)`) and the same
  frame replayed with it off: every target but WORK2 the same bytes both ways, at Original and 2x
  every target the CPU model's both ways (WORK2 too with the fold off), the fold counted in every
  frame that draws the glow and none kept, (e)'s frame with the shine kept for the lamps' reason,
  every pipeline created enumerated (`checkGlowFold`; [RENDER_POST.md](RENDER_POST.md#the-glows-mask-in-its-reduction));
  the recording part plans the chain without a device (`rd__glow_fold_plan` on post mode 7: the
  fill, the mask and the reduction in order, the fill's colour, the mask's test; with the fans the
  mask alone; the shine frame kept, `checkGlowFoldPlan`). At every setting post mode 7 runs once
  more without and with the fans with the switch not forced: folded exactly where the device
  profile's `glowFold` is on (off on the tile-based classes), the same bytes as the chain drawn.
  `rd_blur_glow_fold_tile` runs the case alone (`--only glow_fold`) under
  `ICO_VK_FAKE_LIMITS=moltenvk` (the Apple row) with `--glow-fold-default off`: the profile's glow
  fold off and the unforced frames drawn, not folded; `rd_blur_glow_fold_tile_on` the same with
  `ICO_RD_GLOW_FOLD=1` and `--glow-fold-default on` (the key folds them); the forced frames as in
  the shard both times. In every shard a replay that folded the mask leaves WORK2 out of
  `compareAll` (the fold does not draw it; WORK0, its reduction, is compared).
- `rd_fx_frame_test.c`: the frame-size sprites' plan on the CPU against `gs_math.hlsli`: the
  constant fold's channels over every blend mode, FIX, COLCLAMP, PABE and a set of RGBAQ against
  `gs_dst_channel` for every Cd and Ad, the premultiplied term through the fixed-function add and
  reverse subtract against `gs_blend_reg_ch` for every Cs, C and Cd of the four forms, and the keys
  `rd__fx_enumerate` adds (none with the plan off, the constant fold's seven without the frame and
  local modules, one of them the depth attached under Z ALWAYS without Z write), with no draw made
  without a device (`rd_fx_frame`).
- `rd_fades_fold_test.c`: the fades fold ([RENDER_POST.md](RENDER_POST.md#the-fades-fold)) on the CPU,
  without a device: `gs_fold_ch` (`gs_math.hlsli`) with no rounding term against `gs_blend_reg_ch`
  of ALPHA 0x44 and 0x64 for every Cs, Cd and factor 0 to 0x80 (a texel stepped at scale 1 is the
  texel the sprite stored under the exact blend), with the term 64 against the nearest level;
  `gs_fold_applies`'s rows (the count, a bar step on the bars' rows only, no bar); above 1x, a
  bilinear of four taps modelled as two lerps of 8-bit weights (rounded, then truncated): a step
  of factor 0 or 0x80 the same bytes before or after it (the count of channels two levels apart
  for a step between levels printed: the model's worst case; the plan takes one such step a row
  above 1x, and refuses a fade and bars both between levels on the bars' rows there);
  `rd__fades_fold_exact`'s
  table (the attachment read's usage bit under attachment, the storage read's under storage); the
  cases of `rd_gsbase` (the fade (200, 100, 7) at 50, the brightness at 12, the letterbox at 25 over
  rows 0 to 57 and from 455 on, and their chains) recorded through `rd_post`, planned, and every
  texel of rd_gsbase's gradient stepped as `fx_fold4` steps a tap against the sprites drawn in
  turn: byte for byte under the exact blend (the plan's rounding from `rd__fades_fold_exact` of
  the attachment mode), the nearest level under the fixed-function one, the chains of two steps
  between levels drawn there; and `rd__fades_fold_plan` on frames recorded as the game's
  post passes record them: the fade alone (its step, alpha, the nearest level under the
  fixed-function blend, the floor under an exact one, its command's ordinal), the fade and the
  letterbox (the bars one step, SCENE's rows 0 to 57 and from H - 57 on, under an exact blend;
  drawn under the fixed-function blend unless the bars are black; at scale 2 black bars and a
  black fade fold with the rows the replay's corner snap gives, 116 and 2H - 114, a fade between
  levels does not; at full height 1x black bars fold unless `rd__set_a1x_gs_keep` is off), the
  brightness, the fade and the letterbox (three steps in order, under an exact blend; the
  brightness and the fade drawn under the fixed-function one), and the frames it leaves alone: the keep
  sprite or a menu after the fade, text quads drawn into SCENE after it (the deferred text's
  quads leave it), a Z-testing sprite after the letterbox (one before it leaves the letterbox
  folding), the effects depth or a depth read by a later frame (the bars drawn, a fade after them
  folds), a draw into SCENE in list 12, MSAA, a factor above 0x80 (`rd_fades_fold`).
- `rd_vis_test.c`: the scene draws' visibility test ([RENDER.md](RENDER.md#culling)) on the CPU,
  without a device (also the MSAA epoch, `checkMsaaEpoch`: `rd__vis_msaa_may_close` true for DATE,
  a texture of SCENE's depth (texturing off too), SCENE's colour with another depth and SCENE's
  depth with another colour, false for SCENE's own pair, SCENE without a depth and another target's
  pair; on a recorded frame of off-screen draws under `rd__vis_set_msaa(1)` the four that may close
  the epoch replayed (`MSAA_EPOCH`) and the plain one skipped, under `rd__vis_set_msaa(0)` all five
  skipped; also the reason counts `rd__vis_why` of the frame case, summing to the draws
  walked, and a two-batch mesh drawn by batch ranges: the draw of the batch left of the frame
  skipped by its own box and counted `SKIP_RANGE`, the draws of the other batch and of both kept,
  the moved stream measured again): `rd__vis_box` over synthetic prelit, lit (with a sheet's tiles past its own
  count) and skinned streams (x, y, z and the pos.w range; a position not finite refused),
  `rd__vis_grid_box` over a Mesh3D buffer (its header and MSCNT qwords left out, a buffer too short
  refused), `rd__vis_skin`'s boxes by bone slot and largest `|w0 + w1 - 1|` (a weight outside
  [0, 1] refused); an orthographic block: a box 2.5 GS pixels past each side of a 512 x 448 target
  skipped beyond it, 1.5 kept, code 36's pos.w range, a box crossing w = 0 kept, one behind the
  eye skipped under code 32 and kept under code 36; random perspective cameras and boxes at four
  frame sizes in both rounding modes: every verdict seen, and every corner and 64 points inside a
  skipped box through the program's float product beyond both planes of its side; two bones (the
  blend inside the frame kept though one bone's box is outside it, outside skipped, weights summing
  to 0.6 kept near the frame) and random bones and weights through `vu_skin.hlsl`'s product; the
  VU references as the oracle: where the test skips a strip inside its box, `vu1ref_normal_c`
  (code 32, and code 36 with `mem[16..19]` the viewport times `mem[20..23]`) puts every output
  vertex in front of the eye, and every fan vertex, beyond the side (behind the eye for the near
  verdict), in both rounding modes; and `rd__vis_frame` on a frame built with
  `rd__init_record_only`: the skipped commands RDC_NOPs of a shadow copy with every other byte and
  the lists without a skip shared, code 34, particles, no target and list 11 kept,
  `rd__vis_culled` and `rd__vis_source`, the counts in the performance record, a stream written
  again bounded again, and the frame itself returned with the mode off, a pass window, number 0 or
  nothing skipped; the occlusion test (`rd_vis_hiz.c`): random cameras' vertices through the float
  product in both rounding modes, every one `rd__vis_hiz_vertex` takes inside the region test (code
  36: without a clip flag) with its ftoi4 position and its unsnapped one within `r` of the centre
  and its GS Z above the bound; random boxes, every corner and 64 points inside within
  `rd__vis_hiz_occludee`'s box and under its Z bound; 300 random triangles rasterised, the two ends
  of each row's covered run (one run a row) inside every placement of the corners within `r` (the
  64 extremes and 16 inside) by 0.25 GS pixel, the square grown by a pixel each way and half a
  pixel more up and down; the inner rule at a cell's corner (a right angle on it: the cells on its
  legs not covered, the next in covered; a leg 0.3 pixel out of a cell's grown edge covers it, 0.2
  does not); through the
  orthographic block a wall then a box behind it hidden, one GS Z unit in front, coplanar, inside
  the margin, inside the later mark's room, reaching past the wall's edge (a part-covered tile) and
  beside it kept, nothing hidden by an empty or reset record; near 2^31 the margin 20480 and a box
  hidden exactly when its bound and the margin stay under the wall's; and on a recorded frame under
  full: the box behind an opaque wall an RDC_NOP, one in front kept, kept behind a wall under an
  alpha test its As may fail (GREATER 0, alpha 0), blending, Z write off or a chain plane, under
  its own Z test ALWAYS, after a clear or a draw that may lower the depth (particles under ALWAYS
  with Z write), a wall under ALWAYS hiding, a wall under ATE GREATER 0x40 with AFAIL FB_ONLY whose
  As is 127 at every vertex hiding (the alpha plan's range), the walls themselves drawn, list 5's
  box kept; the counts (3 occluded, 4 tested, 5 resets); frustum alone hiding nothing; `ICO_RD_CULL_HIZ_TRIS` 0 (none) and 1 (the first wall's
  first triangle alone); `ICO_VK_DEPTH_ALWAYS` hiding nothing; a wall under ATE GREATER 0x60 with
  AFAIL FB_ONLY whose texture is not uploaded yet (record-only: dirty) hiding the box behind it when
  the texture's alpha is 0x80 throughout (`rd__vis_hiz_pending_scan`'s scan 0x80..0x80), not when
  one texel is 0x40 (`rd_vis`).
- `rd_vis_hiz_test.c`: the occlusion test's cheaper collect ([RENDER.md](RENDER.md#culling), the
  occlusion test's cost) on the CPU, without a device (`rd__init_record_only`, 512 x 448): over
  3000 random perspective cameras and boxes (code 32, and code 36's pos.w range) every triangle of
  three random points of the box whose corners `rd__vis_hiz_vertex` accepts has an area at most
  `rd__vis_hiz_screen_area`'s bound; 400 random meshes of up to 12 batches (tiny clusters, wide
  ones, ones crossing the eye plane) under clip modes 32, 34 and 36 and random batch ranges: the
  fast pass (`rd__vis_hiz_mesh_ex` with every batch's box and the check) puts out the full pass's
  triangles field for field in its order, none left wrongly, both shortcuts taken over 100 times;
  60 random frames of list 0 (walls near the eye, boxes far behind, a wall square and a tiny box
  inside its first triangle that it hides, draws under Z test ALWAYS, clears, draws of part of a
  mesh) under `ICO_RD_CULL_HIZ_FAST=0` and then three times with the fast collect and
  `ICO_RD_CULL_HIZ_CHECK=1`: the same NOPs, `cull why` and `cull occlusion` reasons and counts, no
  check wrong, every occluder draw of the second and third walk from the collect cache; a wall and
  the box it hides drawn again (a cache hit), after the wall's stream was written (a new serial: a
  miss, the box kept), back (hidden again), under a moved matrix (a miss) and after
  `rd__vis_hiz_cache_clear` (`rd_vis_hiz`).
- `rd_boot_present_test.c`: a saved start-up through the presenter, present by present; needs
  `ICO_BOOT_DUMP_DIR` (`rd_boot_present`).
- `rd_blend_selftest_test.c` with `rd_blend_selftest.cmake`: the exact blend's start-up self-test
  and the attachment read's Qualcomm rule; the mode left in force, the forest's tint under it, the
  targets without the attachment read where it is off, the self-test again at a new scene size;
  its log lines matched plain, sabotaged, under the fake Adreno, with each `ICO_RD_BLEND` and
  without a destination read (`rd_blend_selftest*`); its 22 samples include the after cell, an
  opaque cell drawn after the blended draws and read at an even and an odd texel column (`after
  cell, even column`, `after cell, odd column` in a failure line), which a driver that keeps a
  dual-source equation with blending off fails, and the overlap of two sprites of one LERP command
  (`one draw, second over first`: the read's rasterization order inside a draw). The glyph's
  shader is matched on both lines (`-DGLYPH`, issue 188): the 1x bake's `font_sheet_baked_ps` by
  default (`rd_blend_selftest`), `font_sheet_ps` under an exact mode asked
  (`rd_blend_selftest_blend_attachment`) and with `[dev] sheet_shader`
  (`rd_blend_selftest_sheet_shader`, `ICO_RD_SHEET_SHADER=1`). Under GS state as
  shader data (`ICO_RD_UBER=on`): the attachment read taken for it and kept
  (`rd_blend_selftest_uber`, the profile line's and the start line's words), kept when sabotaged
  (`rd_blend_selftest_uber_sabotage`: `[dev] uber = on` keeps it as `[dev] blend` does), and
  `ICO_RD_BLEND=fixed` turning it off (`rd_blend_selftest_uber_blend_fixed`). The fixed-function blend is the default
  everywhere, so the exact paths are covered by opt-in runs: `rd_blend_selftest_blend_attachment`,
  `_blend_exact` and `_blend_storage` name the mode, and `rd_pixel_attachment`,
  `rd_fog_attachment`, `rd_present_attachment`, `rd_crt_attachment` and `rd_perf_attachment` run
  `rd_pixel_test`, `rd_fog_test`, `rd_present_test`, `rd_crt_test` and `rd_perf_test` under
  `ICO_RD_BLEND=attachment` with the same expectations and hashes the exact blend had as the
  default (`port/render/CMakeLists.txt`). `rd_pixel_uber` runs `rd_pixel_test` under
  `ICO_RD_UBER=on` (GS state as shader data, [PIPELINES.md](PIPELINES.md#gs-state-as-data)): the
  attachment read's expectations, its exact-mode matrix leaving out and counting the cells the
  read draws its own way (the premultiplied subtracts' floor, DATE per primitive) and holding the
  subtracts within the blender's rounding of the fixed-function draw (each RGB channel the same or
  a level under, two where the two quads overlap; the alpha and the depth the same),
  `testUberSubtractOracle` (`port/render/test/rd_pixel_uber_test.inc`) comparing every pixel of
  the premultiplied subtracts (ALPHA modes 1 and 6: FIX 0x10, 0x55, 0x80 and 0xC0, or As; FBA off
  and on; a noise texture over another, both drawn 1:1 so every Cd, Cs and As is a texel the CPU
  holds) against a CPU oracle of the GS's blend, per channel `clamp(Cd + (((0 - Cs) F) >> 7), 0,
  255)` with the GS's arithmetic shift (`gs_blend_reg_ch`'s arithmetic, checked against the
  formula), COLCLAMP on, PABE off, the alpha As with FBA's MSB: through the colour read 0 pixels
  differ; on the blender (`rd_pixel`, `rd_pixel_attachment`, `rd_pixel_storage`, `rd_pixel_nodual`,
  where the subtracts stay there) every pixel is `clamp(Cd - ((Cs F) >> 7), 0, 255)`, one level
  above the oracle exactly where `Cs F` is not a multiple of 128 and never below (the first
  differing pixel is printed with its cell, Cd, Cs and F), `testDateStencil` checking the
  per-primitive DATE first, `testScreenRuns`' boundary case
  expecting its two DATE sprites in one draw (no snapshot to retake) and `testPipelines` looking
  each run-time key up as the read's normalised image. The pass-dump compare of the corpus
  under `ICO_RD_UBER=on` is made against baselines made with `ICO_RD_BLEND=attachment
  ICO_RD_UBER=off`: with `ICO_RD_UBER_PREMUL=0 ICO_RD_UBER_DATE=0` every target byte for byte at
  the whole scales, as shipped only the premultiplied subtracts and the overlapping DATE draws
  apart (a script of the release's evidence runs it; no ctest, the corpus is machine-local).
- `rd_vuprepass_selftest_test.c` with `rd_vuprepass_selftest.cmake`: the VU vertex and triangle
  pre-passes' start-up self-test at Enhanced, 16:9, scale 2 and at Original, 4:3, 1x: the
  pre-pass left on (it passes on lavapipe), turned off when its first command's vertex records are
  drawn a texel off (`ICO_RD_VU_PREPASS_SABOTAGE=1`), its triangles' slot A a slot off (`=tri`) or
  the tick stage's tick 1 at tick 0's camera (`=ticks`), or not tried when it is off by the
  settings (`ICO_RD_VU_PREPASS=0`); the summary text, the reachable set made after it (the main
  shaders', the pre-pass entries' and the five record stages' keys, or none of the pre-passes'),
  SCENE left to its clear; its log line matched (`rd_vuprepass_selftest`,
  `rd_vuprepass_selftest_plain`, `rd_vuprepass_selftest_sabotage`,
  `rd_vuprepass_selftest_sabotage_tri`, `rd_vuprepass_selftest_ticks`,
  `rd_vuprepass_selftest_off`); the device profile's class ends with the backend's name (`Vulkan):`,
  `OpenGL):`) and is the desktop column on OpenGL and Direct3D 11. With the pre-pass on, its draws
  are batched (`g_rd.vuPreBatch`) unless `ICO_RD_VU_PREPASS_BATCH=0`, which
  `rd_vuprepass_selftest_unbatched` sets (the self-test one draw an entry and stage). The draw
  merge is on (`g_rd.vuMerge`, the summary's `VU merge on`) unless `ICO_RD_VU_MERGE=0`, which
  `rd_vuprepass_selftest_unmerged` sets (both sides of the self-test's comparison one GPU draw a VU
  draw), or on Direct3D 11.
- `rd_vu_skin_check_test.c` with `rd_vu_skin_check.cmake`: the skinned draws' bone-block check
  ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md)). `judge` (no device): a case of 60 bones and 120 vertices
  through `vu1_ref`, the CPU's positions as the probe all right; one vertex on address 200 moved to
  where a zero matrix at 200 puts it: one wrong, address 200 named, `gives it: yes`; one moved where
  no zero matrix puts it: no address; the 16/16 pixel bound; the byte windows (`equal`, `differs at
  qword 5`, `differs at qwords 184..187`); every row of the decision table's words
  (`rd_vu_skin_check_judge`); the default (`rd__skin_check_default`): on only for vendor 0x5143
  with a window, off on Mali, PowerVR, NVIDIA, lavapipe, through MoltenVK and headless,
  `ICO_RD_VU_SKIN_CHECK` 1/true on anywhere, 0/false/off off, 2 every command. On a device at
  Original 4:3 1x: the start-up probe's `ok` line and
  the check's start line once each; a frame of one skinned command (the same bones and vertices
  through code 20, the world origin in front of the eye) checked through `rd__vu_skin_check_frame`:
  right, the bytes equal, its box check run with no sample outside (`rd_vu_skin_check`; under
  `ICO_GPU_ROBUST=1` and `ICO_RD_VU_SKIN_CHECK=1` the c2 variant drawn where lavapipe offers
  `VK_EXT_pipeline_robustness`, `rd_vu_skin_check_robust`); with `ICO_RD_VU_SKIN_SABOTAGE=200` wrong
  at address 200, the bytes equal, c1 right, `reads as: H1, address-dependent`
  (`rd_vu_skin_check_probe`; under robust access c2 right too, the pipeline robustness chain read
  back, `rd_vu_skin_check_probe_robust`); with `=200,ring`
  wrong at 200, the GPU's copy and the CPU's re-read `differs at qwords 184..187`, `reads as: H2, a
  port race` (`rd_vu_skin_check_ring`); no validation error. `twin` (issue 123): the same frame
  under the game's mode with the twins on (`rd__vu_skin_twins_set`), the scheduled probe off
  (`ICO_RD_VU_SKIN_INTERVAL_MS=off`) and `ICO_RD_VU_SKIN_SABOTAGE=200,ring`, replayed three times
  through `rd__vu_skin_check_replays`: the first replay's twins differ with nothing probed (`twins
  differ: ... the probe of this command: not probed`), the next replay's probe, targeted at that
  command, wrong at address 200 (`the probe targeted after a twin event`), the twins' end line
  (`rd_vu_skin_check_twin`); the same with `=200,nan`, the probe's wrong vertices dropped and
  address 200 named by its non-finite matrix (`reads as non-finite / dropped`,
  `rd_vu_skin_check_twin_nan`); `capture FOLDER`: the twin run with the sidecar written to FOLDER,
  the capture asked once (`rd_vu_skin_capture_due`), the sidecar's header (magic, version, the
  command, the flags: the check's data, the twins' counts, wrong, differ, targeted; A != B) and
  sections read back, the CPU's bytes the bones and the GPU's copy differing at qwords 184..187
  alone (`rd_vu_skin_check_capture`). Issue 123's ladder ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#the-ladder)),
  under the plain set (`ICO_RD_VU_SET=plain`): with `=200` the probe's column 3 (the bones through
  the ring at the draw's block) right where column 0 is wrong (`(c3) the bones through the ring:
  right (0 wrong); reads as: H1, address-dependent`, `rd_vu_skin_check_probe_c3`); the twin run
  with `=200,ring` drawing twin C and C differing from B as A does (`C (ring read) K samples`, the
  end line's `the ring read's twin (C) X drawn, Y read, Z differ from B`,
  `rd_vu_skin_check_twin_c`); the same with `ICO_RD_VU_BONES=ring`: the start line `rd: VU bones:
  through the ring (storage buffer) on the skinned entries ([dev] vu_bones = ring, set without the
  skin check's mark)`, the twins (both ring reads) differing with `A and B read the bones through
  the ring: step 1 of the ladder in force`, and `the ring read: not healed` at the end
  (`rd_vu_skin_check_twin_bones_ring`).
- `rd_vustage_ab.cmake`: every dump of `tools/vustage_dumps.txt` (the dump corpus, machine-local
  paths, `*` globs, a missing dump named and skipped, none at all `SKIP`) replayed by
  `rd_replay_tool --pass-dump --stats` under `ICO_RD_BLEND=fixed` the
  old way (`ICO_RD_VU_PREPASS=0`: the programs' entries) and through the record
  (the pre-passes, the main shaders, `vu_ps`), at Original 4:3 1x and Enhanced 4:3 2x: every named
  target's PNG byte for byte, no VU draw the old way with the record, no Direct3D 11
  debug-layer error and no refused record copy (`rhi_cmd_copy_texture_to_buffer: ... skipped`) in
  either run, the stats lines printed; a dump with no VU command (the four 2D dumps of the corpus) records no pre-pass and passes with 0
  passes; the records' camera is the one each command's draw uses (the walk follows the field's half
  pixel as `replayFrame` does). SKIP without the list, its dumps or a device (`rd_vustage_ab`).
- `rd_replay_tool --stats` also prints two counting lines (no draw or record changes): `VU merge:
  N draws in M runs (mean R a run); blocked by ...; per list: L:N/M ...` and `VU pre-pass batches:
  B distinct (stage, frame, stream, bones) of D draws`. A run is consecutive VU draws of one pass
  that could be one GPU draw: equal pipeline, texture and stream groups, DrawCB bytes (FNV-1a),
  scissor, FrameCB and target, each a single planned pass with no DATE stencil, no storage or
  destination-read pass and no skin, with no other draw between them; a draw that cannot join (or
  cannot merge at all) starts a run of its own, and the first key that differed is added to the
  `blocked by` counts. N counts every VU draw, so the mean is draws per run. The batches are the
  distinct (stage key, FrameCB, stream buffer, bones zero or not) of the pre-pass's draws; 0 with
  the pre-pass off. What the draw merge drew is the line after them: `VU merge: N draws in M GPU
  draws (K merged into G, A alone; merge on)`, N the draws through the merge builds, K of them in
  runs of two or more drawn as G GPU draws, A drawn one a GPU draw (`merge off` and zeros with
  `ICO_RD_VU_MERGE=0`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge)). Then code 36's pairs
  (one a batch): `VU fuse: N pairs fused, M kept apart (blend form K, plans differ L, passes P,
  dst/DATE D, no planes Q, no merge build B, fuse off F), S under one pipeline already; fuse on`, N
  the batches whose kick took its cut's pipeline, M those kept apart by the first reason that held,
  S those whose two draws had one plan without the fuse (`fuse off` and every pair with ABE off
  under `fuse off F` with `ICO_RD_VU_FUSE=0` or the merge off;
  [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-fused-scissor-pair)). And the store plan's: `stores: N
  depth and M colour stores dropped a replay of P passes (D with a depth; store plan on)`, N the
  passes whose depth (and stencil) was neither loaded nor stored, M the passes whose colour was
  (the shadow count's while it is made, about one a replay; every other colour is kept),
  P the passes the walk began and D those with a depth outside the MSAA epoch (`store plan off`
  and N 0 with `ICO_RD_STORE_PLAN=0`, `all` and N = D with `ICO_RD_STORE_PLAN=all`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)).
  After the shadow count's line, `constant
  reads on: N sprites drawn in a cleared source's colour, M target reads kept (the source drawn since
  its clear, or not cleared this replay)`: N the post sprites drawn untextured in the colour of a
  source target that held only its last clear, M those reading another target that were not (`off`
  and N 0 with `ICO_RD_CONSTANT_READS=0`, every read then counted in M;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#a-cleared-source-read-as-its-colour)).
- `rd_vubatch_ab.cmake`: the same dumps through the record twice, one point draw an entry and
  stage (`ICO_RD_VU_PREPASS_BATCH=0`) and in the pre-pass's batches, at Original 4:3 1x, Enhanced
  4:3 2x and Enhanced 16:9 2x under `ICO_RD_BLEND=fixed`: every named target's PNG byte for byte,
  every VU command through the record in both, the same vertices, no more pre-pass draws batched
  and fewer where one an entry made two or more; each run's draws printed, and the totals
  (`rd_vubatch_ab`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-pre-passs-batches)). SKIP as
  `rd_vustage_ab`.
- `rd_vumerge_ab.cmake`: the same dumps twice, every VU draw a GPU draw of its own
  (`ICO_RD_VU_MERGE=0`) and with the draw merge, through the record and through the programs' own
  entries (`ICO_RD_VU_PREPASS=0`, the tile-based GPUs' path), at Original 4:3 1x, Enhanced 4:3 2x
  and Enhanced 16:9 2x under `ICO_RD_BLEND=fixed`: every named target's PNG byte for byte, `merge
  off` with nothing merged against `merge on`, the merged run's GPU draws (the first stats line's)
  exactly the other's less what its runs joined (K less G of the `VU merge` line), and fewer over
  the whole list; each pair's draws printed (`rd_vumerge_ab`;
  [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge)). SKIP as `rd_vustage_ab`.
- `rd_vufuse_ab.cmake`: the same dumps and those of `tools/vufuse_dumps.txt` (the PowerVR phone's
  three frames) twice with the draw merge on, code 36's kicks under their own pipelines
  (`ICO_RD_VU_FUSE=0`) and the fused scissor pair, at five settings under `ICO_RD_BLEND=fixed`: the
  phone's profile (`ICO_RD_VU_PREPASS=0`, `--enhanced --resolution 2x --aspect 2.189 --present
  2364x1080`; a narrower dump skips it) and the same at its Resolution Auto (`--resolution 2364x1080
  --full-height --full-pixel`, a fractional scale: the no-planes twins), the programs' entries at
  Enhanced 4:3 2x, and the record at
  Enhanced 4:3 and 16:9 2x: every named target's PNG (and `PRESENT.png`) byte for byte, `fuse off`
  with no pair fused against `fuse on`, the same pairs counted by both (the `VU fuse` line's fused,
  kept apart and one-pipeline counts), the fused run's GPU draws exactly the other's less the
  difference of what the merge joined (K less G of each run's `VU merge` line), no pair kept apart
  for the pixel shader (`no planes 0`: every pixel shader a pair draws with reads the mark), at
  every setting that counted pairs with ABE off some fused and fewer GPU draws in total, and on
  every phone frame some fused at both phone settings; each pair's counts printed
  (`rd_vufuse_ab`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-fused-scissor-pair)). SKIP
  as `rd_vustage_ab`.
- `rd_vumerge_ab.cmake` and `rd_vufuse_ab.cmake` take `-DBLEND_ENV=` (the blend both runs draw
  with, `ICO_RD_BLEND=fixed` by default): `rd_vumerge_ab_uber`, `rd_vufuse_ab_uber` and their
  `_pixel` twins on `rd_pixel`'s frames run them under `ICO_RD_UBER=on`, the VU draws through the
  colour read, merged and fused (`port/render/rd_replay_vu.c` (`vuMergeDst`, `vuFuseVerdict`)).
- `rd_vumerge_mali.cmake` (label `mali`): the dumps of `tools/vumerge_mali_dumps.txt` (the Mali-G52
  reporter's frame of issue 164 in `/primary/dev/ico-dumps/164/`, the two phone frames
  `rd_vufuse_ab` reads and a 2026-10-07 capture; a line is the dump, `expect=IN/GPU` and the
  tool's setting arguments) on the plain device and under `ICO_VK_FAKE_LIMITS=mali
  ICO_VK_FAKE_NO_DUAL=1` (a phone's limits, every lerp draw two passes), under
  `ICO_RD_BLEND=fixed` and `ICO_RD_CULL=off` (the `expect=` figures count every VU draw of the
  frame; the visibility test would leave some out): every named target's PNG byte for byte, both
  fakes' lines in the faked run,
  the `VU merge` figures (draws, GPU draws, merged, into, alone) and the fused pairs the plain
  device's, and `expect=` the plain run's merged figure (826 into 87 on the 164 frame)
  (`rd_vumerge_mali`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge-without-dual-source-blending)).
  SKIP as `rd_vustage_ab`.
- `rd_noplanes_ab.cmake`: the dumps of `tools/noplanes_dumps.txt` (the title's and the boot's
  frames, each drawing one screen-prim triangle key with planes, and a capture drawing none; a line
  is the dump, `expect=same|fallback:N|report` and the tool's setting arguments) with the sprite
  planes builds and under `ICO_RD_NO_SPRITE_PLANES=1` (issue 161's fallback): the second run's line
  that the builds are off, `sprite no-planes 0` in the first; `expect=same` every named target's PNG
  byte for byte and no command through the fallback, `expect=fallback:N` byte for byte with N
  commands through it (the title's and the boot's dumps: 2), `expect=report` at least one command
  through it and the targets that differ listed (`rd_noplanes_ab`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#without-the-sprite-planes)). SKIP as `rd_vustage_ab`.
- `rd_occlusion_tool.cmake`: `rd_replay_tool --occlusion` ([RENDER.md](RENDER.md#culling)) on the
  first dump of the machine-local list `tools/occlusion_corpus.txt` that is here, at Original 4:3
  1x: the line `occlusion: hidden N of M list-0 draws, T of U triangles (index count / 3); hidden
  commands: S` with M above 0, N at most M, T at most U and S `none` or a `--nop` spec of list 0,
  the second line, the settings note, `--occlusion-out` holding S and `--occlusion-csv` its header
  and a line a command; then the plain replay and the replay with `--nop @SPEC`, each `--pass-dump`
  with `--present 640x480` under `ICO_RD_BLEND=fixed`: DISPLAY.png and PRESENT.png byte for byte
  (`rd_occlusion_tool`). SKIP without the list, a dump of it or a device.
- `rd_vu_skin_check_dump.cmake`: `rd_replay_tool --skin-check`
  ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md)) at the tool's default settings: on the machine-local
  corpus dump `frame-20261007-200706-v3086` (46 skinned commands) every command the replay draws
  checked, none wrong, the bytes equal and no box sample outside (`rd_vu_skin_check_dump`); on the
  issue 163 reporter's blended dump (`/primary/dev/ico-dumps/163/`) the same clean
  (`rd_vu_skin_check_163`), with `ICO_RD_VU_SKIN_SABOTAGE=200` the probe's zero matrix at address
  200 named with the bytes equal and `reads as: H1, address-dependent`
  (`rd_vu_skin_check_163_probe`), with `=200,ring` the bytes differing as the GPU copied them and as
  the CPU reads them and `reads as: H2, a port race` (`rd_vu_skin_check_163_ring`), and the deep
  dive's patched copy (its lanes on 200 moved to a zero bone) checked right, the CPU and the GPU
  reading the same stream (`rd_vu_skin_check_163_patched`); on issue 123's blended dump
  `frame-20261106-080033-v3910-blended` (copied to the same folder) clean with every twin equal (`rd_vu_skin_check_123`) and with
  `=200,nan` wrong at address 200, H2, the twins differing and the wrong vertices dropped
  (`rd_vu_skin_check_123_nan`); the same two under the Turnip reporter's profile on lavapipe (the
  attachment read, GS state as shader data, the VU pre-pass off), where the twins of the draws
  whose first pass reads the attachment with Z writes off go through their own pipelines: every twin
  equal and at least one through the draw's own pipeline (`rd_vu_skin_check_123_attach`), and with
  `=200,nan` the same with the twins differing and a `twins differ` line
  (`rd_vu_skin_check_123_nan_attach`; on this dump the three draws on bone 200 are twinned through
  the box check's pipeline, and each line says why). The summary line ends `twins: T read (O through
  the draw's own pipeline), D differ (E of them through it); V wrong vertices dropped
  (non-finite); the ring read: c3 right K (c3 drawn in M), ring twins R read, E differ from B`. Issue
  123's ladder on the same dump under the plain set with the pre-pass off
  (`ICO_RD_VU_SET=plain ICO_RD_VU_PREPASS=0`): with `=200` H1 with column 3 right and every twin C
  equal to B (`rd_vu_skin_check_123_c3`); with `=200,nan` twin C differing from B
  (`rd_vu_skin_check_123_nan_c`); with `ICO_RD_VU_BONES=ring` clean, the `rd: VU bones: through the
  ring` start line and `the ring read: healed (0 of T ...)` (`rd_vu_skin_check_123_bones_ring`), and
  with the NaN too the twins (both ring reads) differing and `not healed`
  (`rd_vu_skin_check_123_bones_ring_nan`). SKIP without the dump or a device.
- `rd_vu_bones_ring_ab.cmake`: issue 123's ladder, step 1 ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#the-ladder)):
  the three dumps of `/primary/dev/ico-dumps/163/` (issue 163's blended dump, its patched copy,
  issue 123's blended dump of 2026-11-06) through `rd_replay_tool --pass-dump --present 640x480` under the
  plain set with the VU pre-pass off and the skin check off, `ICO_RD_VU_BONES=cbuffer` against
  `=ring`, at Original and with the attachment read and GS state as shader data: every PNG byte for
  byte, each run's start line (`rd: VU bones: through VuBoneCB`, `rd: VU bones: through the ring
  (storage buffer) on the skinned entries`) (`rd_vu_bones_ring_ab`). SKIP without the dumps or a
  device.
- `rd_vu_skin_check_ab.cmake`: the skin check is designed not to change the picture
  ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md)); `rd_replay_tool --pass-dump` with it off
  (`ICO_RD_VU_SKIN_CHECK=0`) and on for every skinned command of every replay (`=2`: the probes,
  the box check inside the scene pass, the checked draw out of the draw merge) on the corpus dump
  `frame-20261007-200706-v3086` (46 skinned commands) and issue 163's blended dump, at Original
  presented at 640x480, the same with the attachment read and GS state as shader data
  (`ICO_RD_BLEND=attachment`, `ICO_RD_UBER=on`, the interlock faked off, the VU pre-pass off as on
  a tile-based GPU; the on run must say `GS state as shader data: on`) and at Enhanced 4:3 2x with MSAA 4: every PNG byte for byte, each on
  run's `VU skin check: at the end of the run: N checks drawn` with N at least 1, at least one box
  check drawn over all, the twins drawn in every on run (`VU skin check: twins at the end of the
  run: T drawn (O through the draw's own pipeline with its attachment read)`) and O above 0 over the
  attach runs: the twins through the draws' own pipelines with the write mask cleared leave every
  picture byte for byte; also on issue 123's blended dump (`rd_vu_skin_check_ab`). SKIP
  without the dumps or a device.
- `rd_region_stats.cmake`: `rd_replay_tool --region-stats`
  ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#region-statistics)) on the first two dumps of
  `tools/occlusion_corpus.txt` that are here, each at Original 4:3 1x and Enhanced 4:3 2x with a
  present: the frame's line (draws and triangles under the rule above 0, the draws drawing nothing a
  `--nop` spec of lists 0-6 or `none`, as many items as it counts, `--region-out` holding it), a
  draw's line and a list's in their shapes; then `--pass-dump` plain and with `--nop @SPEC`, every
  PNG byte for byte (the draws the CPU oracle says draw nothing draw no pixel); at Original the same
  spec with `--no-device` (`rd_region_stats`). The frame and draw lines' patterns include the side
  bands' group (`, beside N (behind-eye b, out-of-box x, card k)` after the drop's parenthesis) and
  the draw line's closing `; beside N, dropped back M`; at the test's 4:3 settings beside is always
  0. The patterns also hold `; back faces the card test drew F` inside the drop's
  parenthesis, the camera rule's group (`, drawn by the camera rule M (behind-eye b, out-of-box x,
  card k)`) and the draw line's closing `; drawn by the camera rule M`, and every step runs under
  each region rule (`ICO_RD_REGION_RULE` ps2 and camera: the camera rule's drawn drops make their
  draws draw, so each rule's spec is its own). SKIP without the list, a dump of it or a device.
- `rd_region_camera_ab.cmake` ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-camera-rule),
  [DIVERGENCES.md](DIVERGENCES.md) VD-29): the camera rule against the PS2's region rule on the
  dumps of `tools/sideband_dumps.txt` at their own aspect. 1. `tools/region_camera_report.py` at
  the Original preset 1x and Enhanced 2x (presented at 1280x960): `rd_replay_tool --pass-dump`
  under `ICO_RD_REGION_RULE=ps2` with `--region-mask` and under `camera` with `--stats`; every PNG
  compared; SCENE's differing pixels must all lie in the mask scaled to SCENE and grown by one GS
  pixel (red the triangles the PS2's rule drops, or draws beside the picture only, that the camera
  rule draws; green the back faces the card test drew that it drops; blue the sprites out of the
  box), and with an empty mask every target must be byte for byte the same; the other targets
  reported with how many of their changed pixels lie in the mask; the report in the build
  folder's `region_camera_ab/report.md`. 2. `--region-stats` under the camera rule at the Original
  preset 1x and Enhanced 1x, at the dump's aspect: the frame line's drawn (strip plus drawn by the
  camera rule), dropped and rescued counts the same in both presets. A missing list or dump, or no
  device: SKIP (77). TIMEOUT 3600 (`rd_region_camera_ab`). Under the PS2's rule these dumps drop
  nothing but issue 189's three cards (`--region-stats` at their own aspect, both presets; the
  phone frames' card test draws 0 to 2 triangles a frame).
- `rd_sideband_ab.cmake` (issue 29; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-region-rule), step 7):
  under the PS2's region rule (`ICO_RD_REGION_RULE=ps2` on both replays: the side bands are its),
  the region rule's side bands against the PS2's drop on the wide dumps of the machine-local list
  `tools/sideband_dumps.txt` (a path and the dump's own aspect a line: the PowerVR phone's 2.189
  frames, issue 179's 16:9 frame, issue 182's 16:10 frame, issue 189's 16:10 frame, the phone's
  one 4:3 frame as the control, and the issue-29 48:9 and 16:9 captures when they arrive under
  `/primary/dev/ico-dumps/29/`). Every dump at its own aspect, Original 1x and Enhanced 2x:
  `rd_replay_tool --pass-dump` with `ICO_RD_VU_BESIDE=0` (every VU draw keeps the PS2's drop) and
  without, then `tools/cmp_dirs_band.py A B --gs-width W --band-files SCENE.png --fog-from-a`, W the
  replay's `frame W GS wide`: SCENE's centre 512 GS pixels (512 x scale texels) identical byte for
  byte, the side columns reported (changed pixels, largest channel difference, bounding box), and
  every side pixel B changes one A left at its fog colour (A's most common side-column colour); the
  other targets are reported, not held (the post effects read across the band's edge). At 4:3 the
  band is the frame, so the whole SCENE must be identical. A missing list or dump is named and
  skipped; none here, or no device: SKIP (77). TIMEOUT 3600; serial like `rd_region_stats`
  (`rd_sideband_ab`). The dumps from before dump version 15 carry no EE facing table, so their
  prelit meshes have no side bands: only their lit and skinned drops can change there; the fresh
  captures carry the table.
- `rd_sideband_cards.cmake` (issue 189; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-region-rule), step
  7): the reporter's frame near stage 32's exit (machine-local,
  `/primary/dev/ico-dumps/189/frame-20261104-120509-v130039.rddump`, GS 624x448 at 16:10), whose
  only triangles the region rule drops are three of the mist quads' (`wf_smoke1`, draws 5:11 and
  5:22), cards. At Original 4:3, Enhanced 4:3 2x, Enhanced 16:10 1x and the reporter's settings
  (Enhanced 16:10 at 1920x1200, full height, Full pixel): `rd_replay_tool --pass-dump` with
  `ICO_RD_VU_BESIDE=0` and with the side bands, every target and the present byte for byte the same
  (a card keeps the drop beside the picture; with the cards drawn beside it, the mist there changes
  SCENE, AA0, DISPLAY and the present even at 4:3, since the dump's frame stays 624 GS pixels wide
  there and the softening's work buffer reads across the picture's edge); `--stats`'s `side bands: N
  draws with cards` at least 1; `--region-stats`'s frame line `drop 3 (behind-eye 0, out-of-box 0,
  card 3; ...)` and `beside 0 (behind-eye 0, out-of-box 0, card 0)`; and on the two 16:10 presents
  `tools/band_seam.py` (the mean luminance step across each edge of the picture, outside less
  inside, the edge's own column left out) within 1.5 levels (7.3 and 7.2 with the cards drawn
  beside the picture; 0.1 with the drop kept, the picture's own column-to-column step), all of
  it under `ICO_RD_REGION_RULE=ps2`. Then the camera rule at each setting: `--pass-dump --stats
  --region-stats` with `ICO_RD_REGION_RULE=camera`, the frame line `drop 0 (` and `drawn by the
  camera rule 3 (behind-eye 0, out-of-box 0, card 3)` (the mist's cards face the eye: drawn over the
  whole frame), `--stats`'s `region rule: the camera's: N VU draws` with N at least 1, and on the
  16:10 presents `band_seam.py` within 1.5 levels again (the mist inside the picture and beside it
  alike). No dump or no device: SKIP. TIMEOUT 900
  (`rd_sideband_cards`).
- `rd_vumerge_ab_pixel`, `rd_vufuse_ab_pixel` and `rd_vustage_ab_pixel`: the two scripts above and
  `rd_vustage_ab.cmake` on the frames `rd_pixel`
  leaves (fixture `rd_dump`; the list `rd_pixel_vu_dumps.txt` in the build folder):
  `rd_pixel_vu.rddump` (the VU seams' S4 at Original 1x: 64 code-32 draws of one prelit mesh
  under one state, a matrix each, which the merge joins), `rd_pixel_vu36.rddump` (S3's first
  case: a code-36 strip whose first triangle is cut and whose second is kicked) and
  `rd_pixel_frame.rddump` (no VU draw). Every machine has these, so unlike the two above they do
  not skip; the 16:9 and phone settings skip for the 4:3 frames by design. CI's Direct3D 11 job
  runs the same on WARP ([below](TESTING.md#the-render-suite-on-direct3d-11-warp)).
- `rd_vumerge_fallback.cmake` (ctest `rd_vumerge_fallback`, fixture `rd_dump`): `rd_replay_tool`
  on `rd_pixel_vu.rddump` with `--time 3` under `ICO_RD_BLEND=fixed`, with the merge, with
  `ICO_RD_VU_MERGE=0` and with `ICO_RD_FAKE_MERGE_FAIL=1` (the first pipeline made with a merge
  build kept as failed): the faked run logs one failure line naming the merge build and once
  `rd: VU merge: off: the driver could not compile <build> (<backend>); one GPU draw a VU draw
  from here`, and its timed replays draw what `ICO_RD_VU_MERGE=0` draws, more than the merge
  (`rd_replay_vu.c` `rd__vu_merge_fallback`).
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
  `rd_cull_ab_gl`; [RENDER.md](RENDER.md#culling)). SKIP as `rd_vustage_ab`.
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
  (`rd_vis_hiz_corpus`, TIMEOUT 1800; [RENDER.md](RENDER.md#culling)). SKIP as `rd_vustage_ab`.
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
  one run folded ([RENDER_POST.md](RENDER_POST.md#the-glows-mask-in-its-reduction)).
- `rd_dst_usage_test.c`: the blend policy's footprint: by default only the fixed-function blend
  exists (`g_rd.dstModes` OFF, no destination-read layout, shader twin or target usage, no
  readable pass, both exact modes refused); with `ICO_RD_BLEND=attachment` only that mode's
  layout, twins and `SCENE` usage, and a start-up set that grows when the mode is switched on
  (`rd_dst_usage`; 77 without a Vulkan device).
- `rd_crack_test.c` with `rd_crack.cmake`: the crack filler above 1x
  ([RENDER_POST.md](RENDER_POST.md#crack-filling-above-1x)). A 640 x 448 frame under Enhanced, two flat
  triangles sharing a long diagonal edge, the second one's edge one 12.4 step (1/16 GS pixel) to
  the right, two sprites 1.5 GS pixels apart, below them a concave fold (two strips whose depth
  falls steeply towards their shared line) and the fog, replayed with the filler off and then
  as the environment has it, SCENE read back: at 2x, 4x and 1800 x 1080 texels (2.8125 x 2.41;
  `rd_crack`, `rd_crack_4x`, `rd_crack_frac`) the edge shows texels of the clear's fog without
  the filler and none with it, each filled texel the colour of a drawn texel within the reach on
  its row or column after the fog (the fill drawn after lists 4-6) and its depth unchanged, the slit keeps every texel of the clear's fog, nothing
  the first replay drew changes (the fold among it), one fill pass is drawn and its count equals
  the texels changed; with `backdrop` (`rd_crack_backdrop`, `rd_crack_backdrop_4x`,
  `rd_crack_backdrop_frac`) a far plane writes Z over the frame first and the two triangles lie
  near each other in depth, so the cracks hold the plane's depth and colour and are filled the
  same (the discontinuity form) while the slit keeps the plane;
  Where the reach is 1 on both axes (`rd_crack`, `rd_crack_backdrop`, the 2x cases and their `_gl` twins) the
  frame is also replayed with the general shader (`rd__crack_fill_force_general`): every byte of colour and
  depth and the count are the reach-1 shader's (`crack_fill1_ps`), and the `rd: crack fill: the reach-1 shader`
  line is matched (`EXPECT2`).
  at 1x, under Original, under MSAA4 and with `ICO_RD_CRACK_FILL=0` (`rd_crack_1x`,
  `rd_crack_original`, `rd_crack_msaa`, `rd_crack_off`) no fill pass and the same bytes (under
  MSAA4 the frame has no shadow, so the epoch is still open at the fog and the skip line says so); under
  MSAA4 the frame is replayed again without anti-aliasing and the filler: along the edge the
  MSAA4 picture holds at most 5 % of its texels with the clear's depth (colours without a depth
  readback) and the slit keeps the clear's fog on every texel; the same
  2x run on the depth copy, the buffer path, D24S8 (no depth readback: colours only) and the
  two-pass blend (`rd_crack_depth_copy`, `rd_crack_buffer`, `rd_crack_d24s8`, `rd_crack_nodual`).
  Under the MoltenVK report (`ICO_VK_FAKE_LIMITS=moltenvk`, a tile-based profile) `rd_crack_tiler`
  and `rd_crack_tiler_on` (`ICO_RD_CRACK_FILL=1`) find the filler on by default, as on every
  class (the old `off on this tile-based GPU` line denied). `rd_crack_tiler_fold` (`rd_crack_test
  2 fold`, 77 without local read; the name is the old fold's) checks the fog's local read: one fill,
  drawn after lists 4-6 (`RdCrackCounts.late`), the fog by the local read in the same replay
  (`fogLocalDraws`), one render pass more than without the filler besides the count's (the fill's
  own: nothing breaks the scene pass at the fog), and SCENE's colour and depth byte-identical to a
  replay on the depth copy path; `rd_crack_tiler_fold_mali` the same under
  `ICO_VK_FAKE_LIMITS=mali` with `ICO_RD_FOG_PATH=local`. Both deny the old state line's `drawn
  inside the scene pass with the local fog`. The fill's place under the MoltenVK report:
  `rd_crack_glow` (`rd_crack_test 2 glow`) adds the glow's `cleanUpFB` after the fog (list 7: two
  untextured sprites into SCENE, Cs * As + Cd with RGBAQ 0, 0, 0, 0 under Z ALWAYS, then alpha 192
  at Z 1 under Z GEQUAL), so the fill is drawn after them with RGB alone (`RdCrackCounts.afterAlpha`
  1; the filled texel's RGB its neighbour's, its alpha `cleanUpFB`'s); `rd_crack_aura` (`2 aura`)
  adds the aura's paste (list 8: one colour into WORK0 by a sprite, then WORK0 over SCENE through
  (Cs - Cd) * As + Cd at As 32), so the fill is the first draw of the paste's pass (`pasted` 1, no
  fill pass of its own). Each replays the frame with the place pinned to the start of list 7
  (`rd__crack_fill_set_place(1)`): SCENE's colour and depth the same bytes, one fill pass there,
  and two render passes fewer (glow, on the fog's local read; at least one otherwise) or one (aura)
  with the place; the once lines are matched (`EXPECT2`), and `rd_crack_aura` denies the line that
  says the paste could not take the fill.
  Each run's `rd: crack fill:` state line is matched (`-DEXPECT`, `-DEXPECT2`; `-DDENY` must not
  match); `rd_replay_tool_crack` runs the tool on `rd_pixel`'s dump with `--enhanced --resolution
  2x --stats --no-crack-fill` and matches `crack fill: off (the replay tool's --no-crack-fill)`
  and the `stats: crack fill off: 0 passes, 0 pixels filled` line. `rd_crack_resolved_msaa2` and
  `rd_crack_resolved_msaa4` run the tool on the corpus room `lightning/rd-00300.rddump`
  (machine-local under `build-host/tmp/corpus/dumps/`, SKIP without it) with `--enhanced
  --resolution 2x --aa msaa2|msaa4 --stats`: its MSAA epoch closes at the shadow count's pass in
  list 3, before the fog, so the filler draws on the resolved picture: the state line ends `(drawn
  after the MSAA resolve)`, the stats line reads `crack fill on: 1 passes, M pixels filled` with M
  above 0, no replay left unfilled, `1 fills after lists 4-6` and `1 fills after the MSAA resolve`, and no `not drawn` line
  (`-DNEED_FILE`: SKIP without the dump; `-DNEED=rd: msaa: N samples`: SKIP where the device does
  not offer the count, which it then draws as off: lavapipe offers 4 only)
  ([MSAA.md](MSAA.md#the-crack-filler-after-the-resolve)).
- `rd_crt_test.c`: the CRT filter's options and its shader (`rd_crt`, `rd_crt_fixed`); downsampling
  changes no byte under the filter (`checkOff`); the colour adjustment before the filter
  (`checkTone`): every mode at both sizes with the four rows at 50 is the recorded hash (on
  llvmpipe; elsewhere the present with the rows at 0), Scanlines at saturation 1 greys a flat
  (150, 130, 110) to within a level a channel over the box (and keeps its hue at 50), the box's
  mean luma grows with brightness and with gamma (1 < 50 < 100), contrast 1 cuts the luma's
  spread along a row to under a quarter, and with the filter off a moved row changes the plain
  present.
- `rd_debug_test.c`: `debug.c` on the host, recorded (`rd_debug`); with the game variants' bind
  (`debug_VariableInit` hands `ico_variants_bind_debug` the five switches and `debug_MenuOpen`)
  and the menu opened through that opener drawn on each of three ticks with no pad (title and bug
  list), nothing while closed, Triangle closing it.
- `rd_display_change_test.c`: a display change while the game runs (`rd_display_change`, and
  `rd_display_change_gl`): a frame of VU meshes under linear filters, strips, sprites and the
  reduction; a fresh start at Original (1x), then a second start at Original with the start-up
  set, switched to Enhanced at an output whose box gives a fractional scale and back to Original.
  At each state two frames are recorded and replayed: neither replay makes a pipeline
  (`RdPerfRecord.pipelineCreates` 0, `rd__pipeline_count` unchanged after the frame's
  `rd_begin_frame`), the switch to Enhanced makes the no-planes twins at its `rd_begin_frame` and
  its second replay draws VU passes through `vu_np_ps` (where the twin is loaded and the blend is
  fixed-function), and back at Original the second replay's draws, render passes, pipeline binds,
  blur folds, glow passes, no-planes passes, above-1x draws and crack fill passes are the fresh
  start's and DISPLAY is the fresh start's, byte for byte
  ([DISPLAY.md](DISPLAY.md#video-options-and-presets), run-time changes). The switch runs with the
  renderer's own thread on (not on OpenGL) and a display progress function that presents a blank
  frame as the window's screen does: every call of it is on the test's thread, none inside a
  render-thread job, and it is called when the change made pipelines; `rhi_call_guard_settle`
  makes a busy guard's wait. The window's half (the
  present allowance started again) has no test: the window is not run in the tests.
- `rd_filter_test.c`: the model viewer's draw filter on synthetic keys (`rd_filter`).
- `rd_fog_selftest_test.c` with `rd_fog_selftest.cmake`: the fog's self-test and probe; its log line
  matched for each path, MSAA counts, odd widths and [Full pixel](ARCHITECTURE.md#terms)
  (`rd_fog_selftest*`; the local read's first under `ICO_RD_FOG_PATH=local`,
  `rd_fog_selftest_local`). With `rd_date_selftest.cmake` and `ICO_RD_DATE_PATH=stencil`, DATE's
  self-test, which the fog's first run runs first: `DATE: stencil path (self-test: stencil ok 5,
  snapshot ok 5)` matched once (`rd_date_selftest`; the fifth sample a DATM 1 command's own
  overlap, the alpha before the draw), and under `ICO_RD_UBER=on` `DATE: colour read path
  (self-test: colour read ok 5, stencil ok 5, snapshot ok 5)` (`rd_date_selftest_uber`: the
  colour read's fifth sample the GS's per-primitive one). The failure paths, under
  `ICO_VK_FAKE_LIMITS=moltenvk` (the profile's Apple row: `fogLocal` and `dateStencil` the
  profile's own, not `[dev]`'s): with `ICO_RD_DATE_SABOTAGE=1`, `DATE: snapshot path (self-test:
  stencil fail ...` and `profile: DATE stencil off (self-test)` (`rd_date_selftest_sabotage`);
  with `ICO_RD_FOG_SABOTAGE=local`, `local fail N, copy ok N`, the copy taken at both sizes and
  `profile: fog local off (self-test)` (`rd_fog_selftest_local_sabotage`). Both scripts take
  `-DEXTRA` and `-DEXTRA2`, regexes that must each match the log at least once. With
  `rd_shadow_selftest.cmake`, the shadow count's self-test, which the fog's first run and its run
  at the second scene size run: every `shadow count: one draw (self-test: equal to the seven draws
  over WxH, 142 band centres ok)` line matched, at least one, and the line saying how the stencil
  is read, `four texels a gather, equal to the seven draws byte for byte` or `a texel a load, as
  the gathers' count ...` (`rd_shadow_selftest`; `rd_shadow_selftest_cropped` and
  `rd_shadow_selftest_wide` the same lines at 624 wide with the display at 16:10, exact 614.40, and
  at 2048 with 48:9, through `-DARGS` and the tool's aspect argument); with
  `ICO_RD_SHADOW_SABOTAGE=1`, `shadow count: seven draws (self-test: one draw differs ...)` and
  `profile: shadow count one draw off (self-test)` (`rd_shadow_selftest_sabotage`); both with
  `_gl` twins. The desktop's
  local-read probe after the first self-tests (`ICO_RD_LOCAL_PROBE=1`, lavapipe being the desktop
  class with local read): with `ICO_RD_LOCAL_PROBE_SABOTAGE=fast` the local reads kept, the fog
  local at both sizes and `profile: local read on the desktop: kept (probe x vs y ms)`
  (`rd_local_probe_kept`); with `slow` turned off, `DATE stencil off (probe)`, every target made
  again and the fog in place at both sizes (`rd_local_probe_off`)
  ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-desktops-local-read-and-its-probe)).
- `rd_local_probe_test.c`: the local-read probe's verdict on synthetic times, no device: kept up to
  5 % slower, off above, kept with a time missing (`rd_local_probe_rule`).
- `rd_fog_test.c`: the depth fog of `fog_DrawFog` on the CPU and on the device, every depth path
  (`rd_fog`, `rd_fog_*`; the local read from the start in `rd_fog_local`, the other paths' replays
  the same bytes); at 2x a depth ramp's fog alpha equals the above-1x formula (the table
  interpolated, the ordered dither) texel for texel, and the 1x cases count no above-1x draw
  (`checkRamp`); the outer columns' probe: its count of GS Z 0 rows on D32F and D24 words, the texel
  each GS column is read at (1x, 2.8125x, and Full pixel's window with GS column 0 left out) and
  the line of the first 8 and last 8 GS columns on the CPU with each of the places those columns
  go (open, cropped, closed at 512 and at a wide 832, Full pixel's window with its `-` columns:
  `checkEdgeLine`), then with Full pixel on at 4:3 the cells' scene, two cells left at Z 0 at the
  frame's edges, gives 32 rows in each of the 16 columns and the left's run (`checkEdgeProbe`; on
  Vulkan and OpenGL, which copy a region of a depth to a buffer, else only on the buffer path), and
  with it off a line, if one was logged, says the border is closed.
- `rd_gsbase_test.c`: `GsBase.c`'s host path driving a synthetic scene (`rd_gsbase`; its post
  checks read SCENE back, so it draws list 11's last sprites whatever `ICO_RD_FADES_FOLD` says:
  the fold's DISPLAY is `rd_fades_fold`'s and `rd_fades_fold_ab`'s); photo mode's
  Deja vu draws the stage's sandstorm object once a frame with the vision's two nodes, gives back
  what it borrowed, adds no film grain, and draws nothing in stage 40, with the effect off or with
  photo mode closed (`checkDejaVeil`); its film reel on a row of `film_noise_l4.bga`'s shape: the
  first photo tick starts the row, each later tick runs it once on the filter's own frames, the
  game's frames and node records are the same after every tick and draw, the noise objects are
  drawn in model order before the veil with the evaluated nodes, their fade following the
  envelope's own frame tick by tick and wrapping at its length, nothing with the effect off, in
  stage 40 or with photo mode closed, the veil alone without the row (`checkDejaReel`; the
  evaluator there stands in for the game's). The wall test's flip hook on every disc's
  `gsb_UpdateGSSystem` copy, the check scripted: each flip passes `+0x380`'s translation, the stage
  and whether a game cut is pending since the flip before; a crossing marks the frame that flip
  closes (`RdCamera.cut` 2, `RD_SNAP_WALL` against the frame before), not the next;
  `gsb_ResetGSSystem` forgets the eye (`checkCameraWall`).
- `rd_still_test.c`: the still re-present's hash and decision (`rd_still`, no device): two ticks with
  the same sprites and a still camera hash the same at alpha 0.25, 0.5, 0.75 and 0.999 (the presenter's top), as a first and as
  a later present (the head copies are not in it); one colour word of the second tick, a sprite that
  moves, a moving camera and Motion blur: Smooth change or withhold it; `rd__reshow_why` for every
  input. The pictures: `rd_replay_tool DUMP out.png --interp 0.9 DUMP --interp-any --presents 3
  --present WxH` (a dump as its own tick before: nothing moves) writes the same PNGs with and without
  `ICO_RD_STILL_REPRESENT=0`, and its lines say which presents were `shown again`.
- `rd_replay_tool_still.cmake` (`rd_replay_tool_still`): the still re-present on a corpus dump as its own tick
  before, 4 presents at Enhanced 4:3 2x at 1280x960: presents 1 to 3 end `; shown again`, and all four pictures are the same bytes
  as with `ICO_RD_STILL_REPRESENT=0` (Not Run without the dump).
- `rd_replay_tool_glow_hold.cmake`: the glow held between ticks (`port/render/rd_glow_hold.c`)
  against its chain drawn at every present (`ICO_RD_GLOW_REDRAW=1`), on two consecutive godrays
  dumps (`build-host/tmp/pcsx2cmp/godrays/port2/keep/rd-01076` and `rd-01077`; Not Run without
  them) at Enhanced 4:3 2x, `--presents 2` with `ICO_RD_STILL_REPRESENT=0` (every present
  rendered) (`rd_replay_tool_glow_hold`). A still tick (rd-01076 as its own tick before): the lines
  say `glow drawn (the tick's first present)` for present 0 and `glow held` (or, with the switch,
  `glow redrawn ([dev] glow_redraw)`) for present 1, and SCENE and the presented picture of both
  presents are the same bytes held and redrawn. A moving tick (rd-01076 then rd-01077): present 0
  the same bytes both ways, and every pixel of present 1's SCENE that differs lies within 128
  texels of the glow's mask (WORK2 not at its fill level, either run's) (with Python and Pillow).
  `build-host/tmp/v094/after/v2-ab.sh` is the A/B over more pairs (statistics, crops, the region
  at 32, 64 and 128 texels, lavapipe timings).
- `rd_replay_tool_save_blended.cmake`: the blended present's frame F12 saves
  (`rd_dump_blended_arm`) replays alone as drawn: `rd_replay_tool CUR --interp T PREV
  --save-blended B` writes the frame built between the two after its replay, `rd_replay_tool B`
  replays it without `--interp`, and SCENE and the presented picture (1280x960) are the same bytes
  as the `--interp` run's. `rd_replay_tool_save_blended`: rd-01076 then rd-01077 (the godrays pair
  above) at alpha 0.5, Enhanced 4:3 2x; `rd_replay_tool_save_blended_163`: issue 163's pair
  (`build-host/tmp/v097/evidence/163/frame/`) at its present's alpha 0.999000013, 2x, full height,
  Full pixel (Not Run without the dumps). Both run the `--interp` again with `--render-thread
  sync` and `on` (`-DRT`): the interpolation and the replays on the render thread give the same
  picture and the same saved blended frame, byte for byte (the payload's alignment gaps are zeroed
  for this: the first threaded run's dump differed in 3 padding bytes the main thread's heap left).
- `rd_interp_test.c`: presentation between [ticks](ARCHITECTURE.md#terms) on two synthetic frames
  (`rd_interp`); the feedback head copies (`testFeedback`: `DISPLAY_HELD` and `FEED_HELD` at a
  tick's first present and the later ones, none for DISPLAY under Motion blur: Smooth, FEED128's
  unchanged, the motion blur's record as recorded; FIX' of 32 at 1, 2, 4 and 10 presents and at
  the ends; FIX' from time, `testSmoothFixDt` in `rd_interp_clock_test.inc` (`rd__blur_smooth_fix_dt`): at dt = tick / k equal to the k form for
  every FIX 0..128 and k 1..64, at 1.7 presents a tick FIX 32 drawn as 57 and the share a tick
  within 2 % of the game's for FIX 8..96, over the uneven gaps of issue 193's log (a 0.5 ms
  present among 14..20 ms ones) the trail's share within 5 % of the game's for that span, dt held
  to [tick / 64, tick], no tick or FIX 0 / 128 as recorded); its rotation cases live in `rd_interp_turn_test.inc` and its seams cases in
  `rd_interp_seams_test.inc`, included where they stood, and its bars case in
  `rd_interp_bars_test.inc` (`testBars`: the same screen sprite under another key in each tick is
  drawn once, whole; a taller one or another blend FIX still cross-fades,
  [WORK_BUFFERS.md](WORK_BUFFERS.md#screen-sprites-redrawn-under-a-new-key-between-ticks)), and
  its keeps in `rd_interp_memo_test.inc` (`testStreamMemos`, `port/render/rd_interp_mesh.c`: two
  meshes of the same bytes compared once and the answer kept, a rewrite compared again, one
  buffer the same shape uncompared, a record with no serial compared at every ask; a skinned
  bone's pivot its bound vertices' centroid, the same at the next present and after another
  mesh's, moved by a rewrite; a shadow volume paired at 0.75, then drawn at 0.25 and 0.75 from the
  kept pairs, the 0.75 volume byte for byte the one paired anew);
  rotation continuity (`testRotationContinuity`): a rigid draw and a bone turning
  170 then 190 degrees a tick stand at 265 degrees half way (on forward the long way, not back to
  85), at 217.5 at alpha 0.25 and at the tick's pose at alpha 1; 190 degrees with no turn before
  keeps the tick's pose; a 20 degree turn after another blends bit for bit as without one. Across
  a gap (`testRotationGap`): after a pair with no present, a camera cut or a discarded frame the
  wheel turning 200 degrees a tick continues the long way from the turn two ticks before, a turn
  8 ticks old serves and 9 does not, a pose the memory does not carry to keeps the tick's, a turn
  under the snap blends the short way. The trolley's wheels (`testRotationWheels`): two draws, of
  one key and of two, at 185 degrees a tick with their places in the list changing every tick,
  both forward the long way; the ride of `box.c` (0.5 a tick of push, 0.98 friction, radius 10)
  up to 125 degrees a tick blended every pair, then a camera cut, and each pair after it forward.
  The seams (`testStillSeams`, drawn through the game's screen and clip matrices): two still prelit
  parts with different model to world matrices, the camera orbiting 28 degrees, are still draws at
  alpha 0.5, each bit for bit the same part unmatched (the tick's block re-based, `camCurDraw`);
  their shared vertex at a cell's middle lands in the expected cell through `vu1_ref` (placement);
  two meshes of one model matrix with other centroids have bit-equal camera and model rows, and a
  point a quarter of an ulp from a cell's edge lands in one cell in both (the mechanism); at alpha 0
  the frame's view is the tick before's bit for bit and each part's rows lie within the still bound
  of the tick before's. The seams at alpha 0 (`testStillSeamsAlpha0`): the same two parts, their
  shared vertex at a cell's middle through the tick before's camera, (1) both matched, (2) both
  unmatched, (3) A matched and B unmatched (the wedge's case); in each the frame's view is the tick
  before's, both parts' camera and model rows are those of (2) bit for bit (the tick's block
  through the tick before's camera, `camCurDraw`), no picture is mixed, and the vertex lands in the
  same cell in both; B through the tick's own block and camera lands over 16 pixels away, so (3) fails on a renderer without the alpha 0 camera; frames
  without a camera count a mixed picture (`mixed` 1). The size flap (`testSeamsSizeTail`, Enhanced):
  B drawn with the region code at one tick and the scissor code at the other, so one payload
  carries a card section and the other not, at alpha 0 and 0.5: both parts blend (none mismatched
  by size, `sizeTailBlended` 1), both are still draws with the shared vertex in one cell, and B's
  card section is the tick's. The present clock (`testPresentClock`) notes
  its value before the clamp (`rd__present_clock_raw`), below 0 for a present before the tick's
  time, and `rd__alpha_bin` sorts the alphas into the 10 s line's bins. The present clock's step
  (`testPresentClockStep` in `rd_interp_clock_test.inc`, issue 193): a steady 60 Hz cadence with one present 0.5 ms after its
  neighbour keeps the step within 10 % of the cadence (the doubled present is not taken as the
  step), at most 2 resets for it, `rd__present_clock_k` 2 against a 30 Hz tick and the time since
  the present before and the tick noted once (`rd__present_clock_take`); the reporter's timing
  (presents 18..22 ms apart, a 0.5 ms one every 7th, every 50th a 120 ms one followed by a 0.5 ms
  one: the reset that, taking the 0.5 ms as the step, holds k at 64) gives k 2 at every tick's
  first present, the step in the cadence and at most 2 resets in a row; a step already stuck at
  0.5 ms is learnt again at the next present (18 ms, k 2) with the Frame rate's gap and without
  one (Uncapped); a 500 ms pause keeps the step, and without a clock value k is the counted
  presents. The ticks' eyes
  (`testEyeTicks`, on a device): alpha 0 carries the ticks' cameras as every present between the
  ticks, alpha 1 none.
- `rd_layout_test.c`: the layout frame from the game's layout calls (`rd_layout`).
- `rd_mesh_test.c`: the mesh path from `Packet.c` to the VU shaders (`rd_mesh`, sharded), each
  device case's frame replayed again with the VU vertex pre-pass switched, the picture byte for
  byte the same, and every pipeline created in the set of either setting (`gpuCase`); the
  `mesh_gen` shard: a save state load's generation floor (`rd_drop_vu_meshes`) applies once a
  slot, so a slot wraps through the full 16-bit cycle after it (0xFFF1..0xFFFF, 1, 2, ...); the
  content table's four sandstorm sheets and the tiles appended to them (`sheetTableChecks`), one
  sheet on the device at 4:3 byte for byte the same with and without the tiles and at 16:9 the
  columns beside it drawn (`sheetPixels`), and the `photo_veil` shard: the veil's two layers at
  60:9 drawing every pixel with the tiles (`photoVeilPixels`), and the `photo_reel` shard: the film
  reel's four specks of the deja dump at their places on the 16:9 frame, none tiled, every pixel
  inside a speck's own place and in the 4:3 picture's columns (`photoReelPixels`); in the
  recording phase the winding marks (`windingMarkChecks`, `ICO_VU_MARK_FLIP`,
  [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-winding-mark)): a prelit mesh without the EE's facing
  table unmarked (`facingFrom` none), with one marked on its kicks as the table says (the bits back
  through `rd__mesh_facing_bits`, a table of another vertex count refused, a copy taking the marks
  and their source), a lit strip wound one way marked at k - s = 2 and 4 from its normals, one of
  mixed normals triangle by triangle, a morph keeping the creation's marks, a table replacing a lit
  stream's own marks, the EE's builder (`rd_vu_facing_begin`, `_vertex`, `_attach`: a strip's
  bits, the table kept under the stream's hash and found again, a vertex without a normal voiding
  it, two tables under one hash leaving none), no other mark made; and the `winding_disc_0` ..
  `winding_disc_3` shards (77 without the disc image): the disc's strips' winding, a quarter of the
  models each, one line `winding: L lit triangles, A %, B %, C %; P prelit parts, E % edges
  consistent` and a line a stage in counts, each also feeding the parts' strips through the EE's
  builder and counting `facing marks M of N agree with the normals`, every one of them the check
  (`windingDiscChecks`).
- `rd_mip_test.c`: the Enhanced mip chain's size and its builder staying inside it (`rd_mip`).
- `rd_mirror_test.c`: mirror mode's flag and the flipped presenter output (`rd_mirror`).
- `rd_passring_test.c`: the pass ring the device-loss report prints: order, wrap, the replay table, the
  replays printed (the last three, whole at 430 passes a replay), the line format (the VU
  vertex pre-pass's entry among them, `testVuPre`) and the loss bisect's line; the pipeline
  note's hold (`testHold`: the shadow reduce's order, its pipeline looked up held leaving the
  seven draws' entry alone, its own entry at the GS size taking it when asked again); pure
  (`rd_passring`).
- `rd_msaa_test.c`: the anti-aliasing's MSAA against the frame without it (`rd_msaa`); with
  `msaa_resolve_ps` or `depth_resolve_ps` cleared for one call (a backend that cannot make one)
  the supported mask holds the supersampling values only; `testShadowReset`: a frame whose list 3
  holds the shadow count with a reset on SCENE's depth resolves once a replay
  (`RdPerfRecord.msaaResolves` 1, two when the reset's stencil clear closes the epoch again from
  inside `msaaEnd`), closes at list 3 (`msaaCloseAt` 4), records no shadow reset as a pass of its
  own under MSAA (`passKinds[RD_PERF_PASS_STENCIL]` 0: the volumes' pass takes it, issue 182) and
  leaves SCENE byte for byte as the same frame without the reset; `testShadowCount`: that frame's
  count (`rd__shadow_count_read`) by the seven draws, the one draw's gathers and its loads,
  without anti-aliasing and with MSAA4: the forms' counts byte for byte one count, MSAA4's equal
  to the count without anti-aliasing wherever the frame without it is one colour and one count
  over a 3x3, the epoch closed at list 3, no stencil reset pass, and gathers counted
  (`shadowGathered`) only where the stencil view takes them (`RhiLimits.stencilGather`; never on
  Direct3D, whose debug layer fails `rd_msaa` on a gather of the stencil plane); `rd_msaa_nvpath`
  runs the program with the reporter's NVIDIA profile of issue 182 forced
  (`ICO_RD_FOG_PATH=local`, `ICO_RD_DATE_PATH=stencil`, `ICO_RD_FX_DST_LOCAL=1`,
  `ICO_RD_SHADOW_ONE=0`) under the validation layer with its synchronisation checks; `testSkySlit`: two triangles of one surface over the clear with a 1/16
  pixel crack along their shared edge, the shadow count, then `cleanUpFB`'s glow mask (a sprite
  at GS Z 1, alpha 192, alpha only, GEQUAL without Z writes): MSAA4 marks no more texels along the
  crack than the frame without anti-aliasing (the covered-majority resolve,
  [MSAA.md](MSAA.md)) and some silhouette pixels' colours still differ; on any device, `testSuspend` (Options > Graphics >
  Apply's renderer switch): `rd_suspend` with a frame open, `rd_resume` carrying the scene size,
  the frame counter, the mirror mode and the GS state (its texture cleared), a texture id from
  before naming nothing, the DATE self-test's flag cleared, no frame open until
  `rd_resume_frame` opens one (once), whose temporary target is freed and which closes without
  a picture.
- `rd_native_test.c`: the native frame's centre rule on a synthetic frame at five widths, and in
  case s the region rule's strip test with the side bands (a behind-the-eye strip with the EE's
  facing table turned to the eye: pixels beside the 4:3 picture at W 688 and 2048, the picture's
  columns the frame without it, nothing at 512; turned away: nothing; a card across the picture's
  left edge turned to the eye: nothing at any width, issue 189), all under the PS2's region rule
  (`ICO_RD_REGION_RULE=ps2`, as every check but the next, the hashes included); the camera rule
  (`checkStripCamera`, `ICO_RD_REGION_RULE=camera`, at W 512 out of the box and Enhanced, 688
  Enhanced and Original, 2048 Enhanced): the wall's triangle with a vertex above the window, the
  card, the card across the picture's left edge and the behind-the-eye strip across it, each with
  the EE's table turning it to the eye, drawn clipped inside the picture and (wider than 4:3)
  beside it; turned away, nothing; without a table nothing, counted no-table at every width
  (`rd_native`).
- `rd_perf_test.c`: the renderer's CPU cost per replay and its steady state, and the start-up set
  through `rhi_create_pipelines`: its progress calls (from 0, never back, the total once at the
  end), a stop after the first batch and the rest made by the next call; last, the background
  finisher (`checkBackgroundSet`): the set stopped after its first batch by the low-memory function
  is offered, `rd_pipelines_background_start` takes it once (a second start returns 0), the memory
  function is asked once before the first batch, one batch adds at most 32 pipelines, the system's
  signal (the function's 2) ends the finisher with nothing more made, and a second stopped set run
  to the end leaves every key of the reachable set with a pipeline and none made on first use
  after it; off Vulkan (`rd_perf_gl`, `rd_perf_attachment_gl`: no background creation) the start
  returns 0 with the finisher off and the memory never asked, and every key is made on first use;
  the exact blend's
  counters (`RdPerfRecord.dstDraws`, `storagePasses`, `dstSwitches`) under each blend path the
  device has (none under the fixed-function blend, no storage pass or switch under the attachment
  read) and, under the storage read, the storage pass merge's DISPLAY bytes equal to the unmerged
  ones with no more passes or switches; the replay and record phases run without the VU vertex
  pre-pass (their barrier counts), a `prepass` phase replays the frame with it (the records still
  pending from before the switch dropped by replay number, since a record is pushed
  `RHI_FRAMES_IN_FLIGHT` replays after its own; nothing created after the phase's first two
  replays, one pre-pass a replay with the same vertices, every mesh command through it, steady
  barriers, DISPLAY byte for byte the picture without it), a `passes` phase times the record
  phase's last frame pass by pass (`rd__set_perf_passes`): each timed record's pieces begin with
  the uploads (the pre-pass second where the device takes it) and end with the display wait and
  the present (the present's own list, issue 179; the record's display wait within its GPU time),
  the pieces the pass ring also notes are its entries of that replay one for one and in order (a
  render pass at the ring's size), the pieces' draws and pipeline binds add up to the record's,
  their counts by kind are the record's `passKinds`, every piece has a GPU time and they add up
  to the record's GPU time within 10 percent and 0.5 ms; without the key a replay is timed only
  250 ms after the last timed one (`RD_PERF_PASS_EVERY_MS`), an untimed one has no pieces, and
  every replay counts one upload, one display wait and one present; without the list and effect stamps
  (`rd__set_perf_list_stamps`) the lists' times are 0, the GPU total is there and the pieces add
  up to it; before any device, the CSV header and line,
  the top-passes text (with a cut to a short buffer) and the passes-by-kind text, to the byte;
  issue 123's stalled replay against the block's (`rd_perf_pass_grow_text`: a piece 3.1x its
  mean, one 1.3x, one under 0.05 ms, one new to the block, one not timed; an empty block; every
  piece over; no GPU span; a cut; and issue 179's display wait named instead of the first pass,
  against no block mean and against its block's, shown without draws among the top passes and
  left out of the passes' counts; the GPU time pacing reads, the span less the display wait,
  `rd_perf_pace_gpu_ms`, never below 0, 0 without GPU times), the block's mean of a piece (`rd_perf_pass_agg_mean`) and the
  arm line with its backend, the arm's backend gate (`rd_perf_passes_arm_backend`: Vulkan and MoltenVK
  with a present split only, never OpenGL or Direct3D), a failure of these counted without a device (`checkPassTexts`). The reachable set is made with the pre-pass (`rd_perf`, `rd_perf_mali`).
- `rd_pixel_test.c`: rd through the Vulkan [RHI](ARCHITECTURE.md#terms), pixel by pixel; leaves the
  dump the tool tests replay (`rd_pixel`). Its pipelines case holds the reachable set under
  `RD_PIPELINE_REACHABLE_MAX` with both blends, the tile-based GPU's set too (its profile with
  local read and the fx plan, the pre-pass off, every state static, Enhanced at 2.8125 x 2.41071:
  1153 keys), that one also inside the RHI's pipeline pool
  less its headroom, and every pipeline the run made in that set (a `font_sheet_ps` key in the set
  `[dev] sheet_shader` makes: the sheet case draws its 1x frame through it under the switch; the
  failure names the key's pixel shader) (`testPipelines`). Its DATE case replays one frame through the stencil bit
  and through the snapshot (where the device has local read): one DATE command of two overlapping
  sprites over cells of alpha 0x80 and 0, SCENE the same bytes both ways and the snapshot's
  semantics within the draw (`testDateStencil`; it skips under `ICO_RD_BLEND=storage` and prints
  why: the storage read has no colour attachment to read, so DATE takes the snapshot there). Its alpha plan case replays one frame with the
  alpha/blend planner off and on: prelit quads of vertex alpha 0x80, 0x70 and 0x40 under the
  railing's state, a LERP FIX 0x80 sprite and a LERP sprite of As 0, SCENE's colour and depth the
  same bytes, three draws counted one-pass (`testAlphaPlan`); the railing keeps its split. Both
  replays run with the fixed-function blend (the mode forced off for them): under the storage read
  the unplanned quads stay on that blend while the planned ones take the exact read, a rounding
  apart, so under a mode asked a third replay (the plan on, that mode) must stay within 3 per
  channel of the planned fixed-function one with the depth equal; a mismatch prints the first
  differing pixel and both values.
  Its AA1 wedge case (`testAa1Wedge`, `rd_pixel_aa1_test.inc`) draws the drained room's ripple
  state (PRIM.AA1, ABE, `Cs * FIX + Cd` with FIX 0x60, Z write, DATE DATM 1, a flat colour of 64,
  so every fragment adds 48) over SCENE: a four-vertex strip with an acute corner and an
  octagonal ring of eight quads, at alpha 0xFF and again at 0x80, and checks every pixel's red is
  48 times the fragments a C model of PCSX2's `DrawTriangle` and `DrawEdgeTriangle` draws there
  (the GS's fill for the interiors) and its DATE keeps, the destination alpha tested as each
  fragment is written (`GSDrawScanline.cpp`), the alpha 0xFF wherever one lands, the As of the
  single edge pixels at 0x80 and the alpha of the last kept fragment within 1, and that the
  model's DATE rejects more than 400 fragments at 0x80 (473 at 450 pixels; DIVERGENCES.md VD-17:
  the port draws the strips primitive by primitive through DATE's stencil bit). It runs on the
  device's own DATE path and, where the device has local read and the blend is not the storage
  read, again through the stencil bit (SCENE made with DATE's local read, setups counted, no
  snapshot), the DATE stencil draws counted both times; it prints the pixels with 1, 2 and 3 or
  more fragments and the fragments the model's DATE rejects and keeps. Under GS state as shader
  data (`rd_pixel_uber`) the device's own path is the colour read: no stencil pair and no
  snapshot, the DATE draws counted as reading the alpha and the Z-writing strips drawn triangle by
  triangle, two draws each (at least 72 screen draws for the 36 triangles;
  `port/render/rd_replay_aa1.inc` (`aa1OrderDraw`)), the same bytes; the stencil cell then holds
  the colour read's DATE off (`g_rd.uberDate`), as `testDateStencil` does.
  `rd_state`'s `aa1 DATE pairs` case checks the ripple state's pairs are enumerated: TEST0 and
  TEST0_SET (Z write on and off) in the screen set, TEST1 and TEST1_CLR in DATE's set with
  `dateStencil` and not without; its tiler set allows DATE's screen twins 224 + 12 and counts one
  setup key of its own (the CLEAR key is the desktop set's).
  Its VU seams case (`testVuSeams`, at Original 1x and 4x and Enhanced 2.25x and 4x) counts the
  clear's pixels along edges two draws share: S1 a code-32 triangle beside a GIF one, S2 two
  meshes 0.01 GS pixel apart, S3 a code-36 cut triangle beside a kicked one, S4 8 x 8 quads under
  a matrix each, none in any mode; S5 a code-32 triangle the region test drops, drawn clipped
  beside an exact one, their shared vertex's GS Z saturated and its X and Y off the 1/16 grid:
  none above Original 1x, where the clipped triangle is drawn, and at Original 1x not drawn
  (`vu_corner_cut_position`). S6 (the main gate's bottom rail beside the 48:9 picture) draws a
  code-36 triangle with an x flag only and a corner whose GS Z saturates, then a GIF triangle at a
  depth between the cut's true depth and the saturated one: the VU's surface wins at the probe in
  every mode (`vu_triangle_out_pos`: such a triangle is GPU-clipped with the corner in the
  homogeneous form, where it was drawn as it is at Z 0x7FFFFFFF).
  Its present case (`testPresent`: the presenter's output pixel at the centre between its two
  DISPLAY neighbours) runs right after the reduction, before `testReductionUint` makes the
  targets again: a target made again holds whatever the driver's memory held until the next
  replay clears it (lavapipe hands the freed DISPLAY's memory back, llvmpipe's OpenGL gives
  zeros), so DISPLAY read after that is not the frame the presenter showed.
- `rd_present_test.c`: the display options through `GsBase.c` and the presenter (`rd_present`;
  the save state's picture, `checkShot`: the box of the open frame's present, CRT off and on, a
  later frame's present dropping it, one copy with the photo capture, replaced, cancelled and
  dropped at the shutdown; `checkShotDeferred`: the state host's deferred answer
  (`rd_capture_shot_arm`, `rd_capture_shot_take`) taken once with the box, no answer for a
  replaced or a cancelled request, a later frame's present dropping it as NULL, here and with the
  renderer's own thread presenting (not on OpenGL); `checkOverlayThread`: both overlay layers
  (the main one drawing through port/ui's font in overlay mode) of a picture `rd_present_post`
  hands over are collected on the test's thread before it returns, never in a render-thread job,
  once a layer a present; the test's own text in the font's overlay mode while the picture may
  still run gives all its quads; the output equals the one without the thread byte for byte;
  `rd_present_blank`'s layers are collected on the test's thread too (not on OpenGL);
  `checkFailedPresent`: with a host of the test's own that runs each picture handed over at the
  next drain, a present that presents is not counted by `rd_thread_failed_presents` and one that
  fails there (a movie shown by then) is, once; `checkWaitSplit`: with a host of the test's own
  whose picture ends at the next drain (5 ms), `rd_present_post`'s waits for the picture before
  are counted by whose (`RdThreadBlock` `freshWaits`/`furtherWaits`: one new frame's present of
  5 ms, two further presents' of 10 ms), another drain in neither, and `rd_frame_close_take`
  counts the frames closed since its last take and starts again;
  the scene limit's fallback checks drive the Vulkan backend's `vkr_test_fail_texels_above` and
  are skipped on Direct3D 12, OpenGL and Direct3D 11, which have no such hook);
  the colour adjustment (`rd_present_tone_test.inc`): `checkToneSettings` (0 and 50 neutral,
  the factors value / 50 in `tone.hlsli`'s order); `checkTone`: every row at 50 presents the rich
  frame as `checkOriginal` did, byte for byte; the exact window at 4:3 (Full pixel, one texel a
  pixel) at 1280x720 with one row moved (gamma 25 and 80, brightness 25 and 80, contrast 25 and
  80, saturation 1 and 80) puts every box pixel within a level of PCSX2's shade boost computed in
  double precision on the neutral present, the bars black; `blit_tone_ps` on both outputs is in
  the reachable set in both blend modes; `checkToneDepth`: with the effects depth forced the toned
  present is the toned present without it, byte for byte, and not the neutral one;
  the open border's outer columns (`rd_present_border_test.inc`, `checkOpenBorderEdges`): at
  2340x1080 (832 columns, the frame its exact width) and 4:3, 1x and 2x, the mirror on and off,
  SCENE red with green over GS columns 1 .. W - 2 presents green outer columns, byte for byte the
  present of SCENE green everywhere, and without the strips (`rd__set_blur_border_repeat`) only the
  outer columns differ; at 16:9 (cropped) and with Full pixel off the strips change no byte;
  DISPLAY's strips are their neighbour columns' bytes at 1x and one value a row at 2x; SCENE's
  own outer columns (`checkSceneBorderRepeat`): a fogged frame whose SCENE leaves GS columns 0, 1,
  W - 2, W - 1 at the clear (the fog paints them white) and a glow-like post sprite spreading SCENE
  eight GS pixels in from each edge presents, at 16:9 Enhanced 2x with Full pixel off (the border
  mask), at 16:9 2x with it on (whole scale: cropped), at 2340x1080 with it on (open) and off
  (closed on a wide frame), and in Full pixel's window at 2560x1080 (the window's scale 2.8125,
  `ox` 1.5: texels of columns 0 and 1 in it), byte for byte the frame drawn over every column,
  with one repeat a replay, and without the repeat (`rd__set_scene_border_repeat`) differs within
  14 GS pixels of the box's edges alone; each run's `rd__scene_border_state` is the one expected;
  at 4:3 with it off (the border closed on the PS2's frame) SCENE is kept as drawn; the 48:9 strips (`checkExactEdges`) are checked one
  value a row over the texels they cover by a whole texel (from 0 to below 2k - 0.75 and from
  k (W - 2.25) + 1 on, k texels a GS pixel); the present pins hold without the strips, and with
  them differ in the outer columns alone (`checkFullPixelOnPin`),
  an old file's `window_size` among them (ignored, kept as written);
  the targets' scales include the work buffers and shadow levels at their GS sizes at every scale
  and Water reflections' block: its GS size under Original, the scene's scale under Sharp (4x at
  4x), both the GS size at 1x (`checkScales`); downsampling in force with a scale or a fixed size,
  not at the `window` resolution or under the CRT filter, and the supersampling measured against
  the GS grid (4x + SSAA2 is 4x, 2x + SSAA4 is 4x, 1x + SSAA2 is 2x; `checkScales`); without
  anti-aliasing 4x into 640 x 480 and into 1600 x 1200 both take the window's reduce, byte for byte
  the CPU's box average (`cpuReduce`), and 1x into 320 x 240 keeps the plain blit
  (`checkAaOffReduce`); with downsampling at 2x and 4x into 640 x 480 and 2x into 512 x 384,
  DISPLAY is the run's without it byte for byte, the downsampled DISPLAY
  (`rd__read_present_downsample`) is `cpuReduce` of it byte for byte with the same passes and no
  window reduce (`rd__last_present_reduce`), 4x + SSAA2 and 1x + SSAA2 present the downsampled 4x
  and 2x byte for byte, and on smooth gradients the downsampled presents are within 4 LSB of the
  1x present (`checkDownsample`).
- `rd_raw_test.c`: the raw packet builders outside `seki` (`rd_raw`).
- `rd_replay_tool_list.cmake`: `--list --no-device` on `rd_pixel`'s dump (`rd_replay_tool_list`).
- `rd_replay_tool_pass_dump.cmake`: `--pass-dump`, `--target` and `--aspect` on that dump
  (`rd_replay_tool_pass_dump`).
- `rd_replay_tool_loss_bisect.cmake`: `--loss-bisect` on that dump runs clean on the device with one
  replay per pass and one to count them, `--passes 1-P` (the whole frame) draws the plain replay's
  picture and `--passes 1` stops the walk at pass 2; the second level's draw window: `--passes 1-P
  --draws 1-N` (every draw of the last pass) draws the plain picture, and one step with `--draws
  1-2` and one with `--mask-marks` run clean and name their last draw (`rd_replay_tool_loss_bisect`).
- `rd_replay_tool_pass_split.cmake`: that dump with `ICO_RD_PASS_SPLIT=8` draws the plain replay's
  DISPLAY picture byte for byte (`rd_replay_tool_pass_split`).
- `rd_replay_tool_prerotate.cmake`: the present into a turned swapchain through its headless twin
  (`ICO_VK_FAKE_TRANSFORM=90`, `180`, `270`: the picture turned into a texture of the turned size
  by the swapchain path's own pass and read back turned back on the CPU, `port/render/rd_prerotate.c`)
  is the identity present byte for byte, and each turned run names its twin; that dump at 640x480,
  1280x720, with `--overlay-test` (the port's UI layer) and with `--crt pvm`, and the dumps of
  `tools/prerotate_dumps.txt` that are on the machine at 640x480 and 2400x1080
  ([DISPLAY.md](DISPLAY.md#the-turned-swapchain)) (`rd_replay_tool_prerotate`).
- `rd_replay_tool_pass_merge.cmake`: that dump under `ICO_RD_BLEND=storage` with the storage pass
  merge draws `ICO_RD_PASS_MERGE=0`'s DISPLAY picture byte for byte at 1x and 2x, with no more
  passes or pass switches; skips without the storage read, as on a lavapipe before Mesa 25.2,
  which has no interlock; from 25.2 lavapipe has it and the test runs
  (`rd_replay_tool_pass_merge`).
- `rd_replay_tool_snap_swap.cmake`: that dump on each blend path the device has, at 1x and 2x:
  the staticBlur sprites' snapshot swap draws `ICO_RD_SNAP_SWAP=0`'s (the copy's) DISPLAY picture
  byte for byte, and its copies plus swaps are the copy's snapshots (`rd_replay_tool_snap_swap`).
- `rd_replay_tool_keys.cmake`: every dump of `ICO_DUMPS` (a list or a folder; skips without it)
  through `rd_replay_tool --keys` at 1x, `--enhanced --resolution 2x` and `--enhanced --resolution
  2340x1080`, on the fake phone limits (`ICO_VK_FAKE_LIMITS=mali`) and again with the tile-based
  GPU's paths forced (`ICO_RD_DATE_PATH=stencil`, `ICO_RD_FOG_PATH=local`, `ICO_RD_BLUR_FOLD=1`;
  the fake limits drop the local read, so the two cannot be combined, and a device without local
  read compares the desktop set again, which the test's output says), and with those paths under
  GS state as shader data (`ICO_RD_UBER=on`: the collapsed set, config `uber`). `--keys` walks the replay
  with nothing drawn (`rd__pipeline_gather`), makes the keys it names and compares them with
  `rd__enumerate_reachable`; the test fails on a key the set lacks, which on a device is one
  first-use stall (`firstUseLine`), never a wrong picture. The union of the asked keys is left in
  `rd_replay_tool_keys/keys-<config>-<size>.txt` (`rd_replay_tool_keys`).
- `rd_perf_gap_test.c`: the GPU's time between two timed replays (`port/render/rd_perf_gap.h`, issue 123): the first timed replay has none; a consecutive one's gap; a 340 ms idle GPU across a stall kept; 1.5 s kept with nothing skipped; two replays without timestamps between kept and counted (skipped 2); 61 s and 60 s refused (the replay the last all the same), just under 60 s kept; a first stamp before the one before ended (under 0) kept; a replay number not after the last (a restart) without one, measured from it after (`rd_perf_gap`).
- `rd_submit_queue_test.c`: the submit thread's queue (`port/render/rd_submit_queue.h`): jobs run in the order posted, numbered from 1, an empty one refused; at most `RD_SUBMIT_DEPTH` queued and running, the running job counted until it finishes (one at a time, a third post refused meanwhile); the finished jobs' times taken oldest first, once, the oldest dropped and counted past the depth, the present timed apart (`presentSplit`, the lock, the driver's call, the poll, the end: issue 123) with them; the longest present since the last take, once (1 ns for a 0 ms present, 0 for a job that presents nothing); the block's means, longest, depth, waits by the render thread and the others, lists submitted on the render thread, cleared by the take with the depth carried for the jobs still queued; the jobs queued and running of each frame slot (a running job counted until it finishes, the other slot's queued job not counted for this one) and the pictures begun beside the other slot's job (`testSlots`); a presenting picture's two lists carried through the queue (`RdSubmitJob.cl2`) and its tail's steps in order, the scene's list ended and submitted, then the present's own, then the present, a one-list job's end and submit with a present only when asked (`testTail`, `rd__sq_tail_steps`, issue 179) (`rd_submit_queue`).
- `rd_pipe_finish_test.c`: the background finisher's choices (`rd_pipe_finish.h`): a set of 1196 keys stopped after 33 gives its batches from key 33 in the set's order, 32 a batch, the keys made meanwhile and the failed ones skipped, a batch never mixing keys with and without a destination read (the other kind's key starts the next), every key not made taken exactly once, 37 batches for 1163 keys of one kind (the last of 11), none past the end; the step after a picture: a batch in flight waits, then is taken whatever the memory or a hold; the system's signal ends the finisher, no key left ends it, a hold (late step, slow picture) and a reading under the floor wait until their time, the memory is read only when nothing else holds the batch, then a batch begins; every step's words (`rd_pipe_finish`).
- `rd_cache_name_test.c`: the Vulkan pipeline cache file's name (`rd_cache_name.h`): two builds' tables of the same modules, each in its own memory, name the same file (no build in the name); one byte of a module changed (its first, a middle one, its last), a module a word longer or shorter, renamed, its stage changed, two swapped, one dropped or added, or `RD_PIPELINE_CACHE_FORMAT` raised each name another; names joined differently do not collide; the form `pipelines-<16 hex digits>.vkcache` (34 characters), a 35-byte buffer taken, 34 and NULL refused; an empty table still names a file (`rd_cache_name`).
- `rd_bisect_plan_test.c`: the loss bisect's second-level planner against oracles: a draw that
  faults with every draw before it (1, 731 and 1,312 of 1,312) found within 12 steps, the masked
  step's verdict, a killed step counted as lost, a step without a verdict planned again,
  contradicting prefixes ending the plan, 1,000 random oracles ending within 16 steps
  (`rd_bisect_plan`).
- `rd_depth_copy_dump.cmake`: a player dump with the fog replayed with the depth in place, copied
  and through a buffer, SCENE byte for byte the same (`rd_fog_depth_copy_dump`, `_d24s8`).
- `rd_shadow_test.c`: the shadow count on the stencil (`rd_shadow`); at scene scale 4 each
  composite, not flagged above 1x, against the float bilinear of its level within 3 LSB, then with
  `RdProfile.shadowBspline` ([dev] `shadow_bspline`) against the CPU B-spline within 3 (4 on the
  fixed-function blender) and a row across the shadow falling and rising once, the softening's AA0 (at its GS size) the area box of SCENE and its composite back the
  bilinear between AA0's texels, not counted above 1x (`checkComposite4x`, `checkSoftening4x`).
  Case m (with each case o run): the shadow volumes in the scene's open pass
  (`rd__shadow_merge_force` off and on) under `ICO_RD_STORE_POISON=1`, the synthetic volumes
  through both paths and Shadow.c's model through the one draw: the count, SHADOW0-2 and SCENE
  byte for byte, list 0's pass armed and the reset taken with it on (`shadowMergeArmed`,
  `shadowMerged` 1), one render pass fewer, the count target's new-target clear left undrawn
  (`shadowClearSkipped` 1; drawn as 0x5A under the poison), nothing armed, taken or left off.
  Case o: the count resolved in one draw from the stencil view against the seven stencil-tested
  draws (`RdProfile.shadowOne` flipped between two recordings of one frame), the count at its GS
  size (`rd__shadow_count_read`), SHADOW0-2 and SCENE byte for byte for the synthetic volumes,
  Shadow.c's model and a rectangle at four sub-pixel offsets, at 1x, at scale 2 and at 1931 x 1093
  texels (fractional on both axes), each recording resolved by the path asked
  (`g_rdPerf.shadowOne`, `shadowSeven`), the one draw with its stencil read four texels a gather
  and again a texel a load (`g_rd.shadowLoad`, counted by `shadowGathered`); the one draw's table for n = 0, 1, 63, 64, 128 and 255
  against 4 n mod 256 (`checkTable`, no device). Case p, with each case o run: the count target's
  colour neither loaded nor stored between the reset and the resolve
  (`rd__store_plan_count_color`), the synthetic volumes through the one draw and the seven draws
  under `ICO_RD_STORE_POISON=1` (the dropped load clears the colour to 0x5A) with the store plan
  off and on: the count, SHADOW0-2 and SCENE byte for byte, the colour dropped only with the plan
  (`shadowColorDropped`, `storeColorDropped`), and above 1x on the one draw the scaled count
  target poisoned at every texel (`poisonFrame`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-counts-colour)). Its `_gl` twin compares OpenGL's
  stencil view the same way; without the view the comparison prints a line and is skipped. Case x
  (`rd_shadow_test edges`, ctest `rd_shadow_edges` and its `_gl` twin): the shadow composites'
  two kernels at scale 2, the same frames (the synthetic volumes, Shadow.c's model, a rectangle at
  +7/16) recorded with one bilinear tap (the default) and with the B-spline (`shadowBspline`
  flipped between two recordings): SHADOW0-2 byte for byte, no pass above 1x with the bilinear and
  the three composites with the B-spline, SCENE byte for byte except within N of a shadow edge (a
  composited level not constant over level texels i0 - 2 to i0 + 3, the kernels' joint support and
  a tie: N = 3 level texels), some texels differing; the |d| histogram printed (`checkEdges2x`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-edges-above-1x)).
- `rd_state_test.c`: the recording half of rd without a GPU: the payload arena's alignment gap
  zeroed over a reused buffer's bytes (`testPayloadPad`), the sheet shader's keys in the set
  (`testSheetSet`, issue 188: no `font_sheet_ps` key on a tile-based profile at 1x and 2x with a
  1008-line output, the frame's 8 at a 3x scene, the overlay's 4 at 1080 lines, 10 with `[dev]
  sheet_shader`, a desktop profile's at 2x; `font_sheet_baked_ps`'s 10 in each; the family line's
  words), the VU fallback's set
  (`rd__vu_set_choose`, `testVuSetPolicy`: the stock Adreno 740's table row and two Turnip driver
  texts take the plain set with the start line's words, Qualcomm's driver without a row, lavapipe,
  RADV and NVIDIA the extended one, `[dev] vu_set = plain` (`ICO_RD_VU_SET=plain` or
  `ICO_RD_VU_PLAIN=1`) and the host's reason the plain one, `vu_set = extended` the extended one
  over each), the aura's measurement switch (`rd__aura_kernel_pick`, `testAuraKernelPick`: `[dev]
  aura_kernel = gs` drops the box of the aura's read of SCENE into FEED128 alone; unset, empty or
  `box` keeps it), the aura's two-pass switch (`rd__aura_box_pick`, `testAuraBox`: unset, empty,
  `auto` or another word the profile's, the on and off words over it, never on and no key of it in
  the set without a device, the pass list's name `ROWBOX`), the shadow count's colour
  (`rd__store_plan_count_color`, `testShadowCountColor`: under the plan the temporary count
  target's loaded colour goes DONT_CARE at its load and store, counted; a CLEAR, SCENE as the
  count, the plan off and no replay keep their ops; the pass list's `shadow count`), dump version 14's ticks' cameras (none in a
  the set without a device, the pass list's name `ROWBOX`), the pass area (`testPassArea`,
  `rd_state_pass_area_test.inc`: `rd__pass_area_cmd`'s boxes for a screen sprite and the aura's
  sprite on SCENE at 1x, at the phone's 848 x 448 GS frame in 2364 x 1080 texels and under Full
  pixel's crop hold every texel they cover and reach at most the pad past them; a VU grid, a draw
  on FEED128 and the sprite under DATE the whole target, a clear the end of a pass's commands; the
  pass begun at the sprite holds its box and the aura's, ends before the FEED128 draw, and its area
  is their union out to whole 32 x 32 tiles; none for a pass begun at a sprite the grid follows;
  `rd__pass_area_why`: none at the sprite, a 3D draw at the sprite before the grid and at the grid;
  the phone's paste (the aura's sprite over the frame's rect, FEED128's UVs) and list 11's
  full-frame fade, at 1x, the phone's frame and Full pixel: boxes of the whole target, the paste's
  run ending before the next FEED128 sprite, both kept whole because their draws cover the picture;
  `rd_perf_why_text`'s words for one replay and a mean over two, and cut to a small buffer; the
  switch's words), dump version 14's ticks' cameras (none in a
  tick's frame, a blended frame's round-trip), list order, defaults, state leakage,
  the alpha test's plan for every Z write, AFAIL, ATE and alpha mask (one draw where the test decides
  nothing, `testAlphaMerge`), the start-up set's VU keys by variant and with the VU vertex
  pre-pass (`testEnumerationVu`, `testEnumerationPrepass`: every program entry's key without the
  pre-pass has its `vu_main_vs` or `vu_particle_main_vs` twin with it, and the keys a phone made on
  first use, blend 4 and 12 under Z ALWAYS (as `vu_main_vs` keys) and the UI sprite under Z GEQUAL, are in the set), the tile-based
  profile's set against the desktop one, static, with dynamic depth and with dynamic depth and
  blend, with and without local read (`testTilerSet`: the desktop keys and exactly DATE's, the fog's
  local key and the fold key, none twice; printed beside the careful-set target; each set under
  `RD_PIPELINE_REACHABLE_MAX` and inside the RHI's pipeline pool less its headroom), the tile
  class's largest set (its profile with local read and the fx plan, stand-in modules), the
  pre-pass off and every state static at 2.8125 x 2.41071, 1153 keys: the 2x set
  and the `vu_np_ps` keys (the DATE pass's twins of them too), each with its `vu_ps` key in the
  set, under the same two bounds (`testTilerFracSet`), GS state as shader data's planner and set
  (`testUberSet`: a LERP, a premultiplied subtract, an unblended mask-7 pass and a DATE pass one
  key through the read; the tiler set at the phone's fractional scale at least four times smaller
  than the fixed-function one, no DATE key, the VU family at most a third, no plain twin of a key
  the read draws, the counts and families printed), and a dump's menu text sheets loading as the renderer makes them, a sheet with
  The profile line's Turnip note (`rd__turnip_note`): "; turnip: Mesa 26.3.0-devel" when `RhiLimits.mesa` holds the version, "; turnip: Mesa version not named by the driver" when it does not, nothing on Qualcomm's own driver or with no limits.
- `diag_test.c`: the crash report (also from a worker thread under a batch's pipeline note), assert, watchdog, progress (with the heartbeat's start-up set count), start-up activity file (the batch notes), device-loss restart and loss bisect (the loss noted in the activity file, the bisect stubbed: once, before the game, never again after a start ended inside it) cases, each in a child process; the symbolizer, the report file and `ico_log_limited` (above)
- `rd_tex_test.c`: the texture cache and `Texture.c` on synthetic TIM2 files, and a text texture
  that could not be uploaded named once in the log, the game's own textures never
  (`textUploadLogChecks`) (`rd_tex`). `slotReloadChecks` (issue 174): a slot `tex_Init` hands the
  next stage, loaded with a picture of the same size and format, gets a new texture (one create,
  no in-place update) and the old texture keeps its texels for one frame tick and is destroyed
  after two. `threadPixels`: thirty replays handed to the renderer's own
  thread without waiting (`rd__replay_frame_async`) while the test's thread, the game's side,
  records the next frame through the texture cache and updates a texture of its own (each change
  waiting for the replay's reads, `rd__texlock_wait`): every replay run there and SCENE as drawn on
  one thread; the counts printed (not on OpenGL). On lavapipe the frame slots are apart (the
  timeline sync), so each replay's walk runs while the submit thread ends and submits the one
  before (RHI.md "Frame slots and the submit thread"): the race check for that partition. `threadSync`: in the sync mode a picture is
  waited for and not counted as handed over (`rd_submit_async` false), with the thread on it is
  until the drain (not on OpenGL). `threadRoomAhead` (not on OpenGL; with
  `ICO_RD_SUBMIT_THREAD=0`, the output given a size so the pictures present): the
  display's wait ahead through `rd__thread_room_hook` in place of `rhi_wait_present_room`, a
  picture handed over that presented ends by making the next one's wait for room; held there, the
  test's `rd_update_texture` and `rdtex_find` make no read wait; the next picture takes it (none
  inside its reads), a picture after one that made none is counted inside; the 10 s clause's
  text; SCENE byte for byte across the pictures; with `ICO_RD_ACQUIRE_AHEAD=0` the seam is never
  asked. `threadOriginal` (not on OpenGL; run last, after `threadRoomAhead` so the pictures
  present): Frame rate Original with a gate recording where it ran; without the render thread
  thirty frames replayed inside the step (the reference SCENE and DISPLAY, the presents' records
  `RD_PRESENT_IN_STEP`); with it each frame `rd_end_frame` closes is handed over
  (`rd_submit_async` true after it, `rd_last_present_info` naming it at once), the gate on the
  test's thread every time, the frames it presented while told busy counted as waits for the
  picture before, the presents on the submit thread, the 10 s clause's words, and SCENE and
  DISPLAY byte for byte the reference's; the gate leaving out every second frame: 15 pictures of
  30, the same pixels; under `ICO_RD_ORIGINAL_ASYNC=0` every frame replayed inside the step
  (`rd_submit_async` false), the same pixels; and the busy skip made certain where there is a
  submit thread: one frame handed over, its submit job held unfinished inside the submit
  thread's hook (`rd__submit_test_hook`: after the job's submit, before its present, so
  `rd_thread_submit_unfinished` stays true; the harness's output is offscreen with no swapchain,
  so the job has no present of its own, and the held job is what the busy rule reads), as a
  driver's present that blocks (released after at most 10 s, so a wait for it fails rather than
  hangs); with the render thread's part of busy idle (`rd__thread_picture_busy_render`),
  `rd__thread_picture_busy` true (deleting the submit thread's part of it fails here); a gate
  answering `RD_GATE_BUSY` when told busy as `window_orig_gate` does: four frames while held are
  left out, and after the release one more is handed over, so the gate runs 6 times, `busySkips`
  is 4, 2 are handed over and run on the render thread, the clause says "skipped: render thread
  busy", and SCENE is the handed-over frames'. It is the render thread's program to run under
  ThreadSanitizer, as `audio_pull_test`: the `asan` preset with `-DICO_SANITIZE=thread` into a
  build folder of its own (`build-host/linux-x64-tsan`), `--target rd_tex_test`, run with
  `TSAN_OPTIONS=halt_on_error=1` under `setarch x86_64 -R`; a report is a failure.
- `rd_tex_pool_test.c`: the texture decode pool against the game thread's decode, 200 textures of
  every format and TEXA mode with levels and queued updates, the bounded queue's fallback, and on a
  device the uploads' alpha scans, byte for byte (`rd_tex_pool`; 77 without a device;
  [TEXTURES.md](TEXTURES.md#tests)).
- `rd_vu_frac_test.c`: the pixel side at a fractional scale, CPU only (`rd_vu_frac`): the truth
  table of `rd__vu_pixel_fs`, `rd__planes_dropped` and `rd__screen_planes_want` over the profile
  flag, the preset, the exact blend, nine scales (whole and fractional on either axis), the two
  filters, a texture with and without GS levels, the chain clip plane and the entries; the
  start-up set at 2.8125 x 2.41071 being the 2x set and one `vu_np_ps` twin per `vu_ps` key; the
  `vuNoPlanes` count; and in the embedded SPIR-V every input of `vu_np_ps`, `vu_texa_np_ps` and
  their `_nodual` builds at location 0, 1 or 2 with `vu_ps`'s type and interpolation there, and
  `vu_ps`'s inputs matching the outputs of every VU vertex shader that draws (the main shaders,
  the programs' entries and their plain builds).
- `rd_water_test.c`: the surfaces that render to and sample from textures (`rd_water`).
- `texpack_disc_test.c`: the PCSX2 names of every texture on the disc (the walk of
  `port/render/texpack_walk.h`), and how many of a pack's files they reach (`texpack_disc`).
- `texpack_e2e_test.c`: the texture pack end to end on the real pieces (`texpack_e2e`).
- `texpack_export_test.c`: the texture export's core on TIM2 files it makes: the names written are
  the level-alone candidates, each PNG at the padded size, a file already there kept, short and
  24-bit CLUTs refused, a repeated texture one file and a duplicate, the manifest's counts and
  flags, a second run writing nothing, a stopped and a dry run (`texpack_export`); `--disc` a dry
  export of the disc against `texpack_names.txt` and a cancel (`texpack_export_disc`).
- `texpack_load_test.c`: the DDS and PNG loaders on files built in memory; with a pack, its files
  (`texpack_load`, `texpack_pack_files`).
- `texpack_name_test.c`: XXH3, the swizzle and the names against PCSX2's rules (`texpack_name`).
- `texpack_test.c`: the pack's index and loader on fake files (`texpack`).
- Helpers: `mesh_env.h` (the mesh tests' game stubs), `vu_models.h` (synthetic models),
  `sheet_ref.c`/`.h` (the sheet-text shader on the CPU), `above1x_ref.c`/`.h` (the above-1x
  kernels and the fog's formula on the CPU, written from their definitions).

