# TODO

Work that is wanted but not done, and facts that are not yet verified. Each
entry says what, why it matters, and where it would go. Finished work is not
listed here; `git log` has it. The other documents keep at most a one-line
pointer to this file.

## Display

- **CRT filter.** A display option (Settings > Display) that renders the
  presented picture through a CRT shader: scanlines, phosphor mask, a
  little bloom and curvature, tuned so the Original preset at 4:3 looks
  like the PS2 on a period television. Off by default. It is a present-time
  post pass in `port/render/rd_present.c` over the DISPLAY target, with
  its own HLSL in `port/shaders/`; it must not touch the scene passes, so
  frame dumps and the Original pixels stay what they are. Strength and
  mask type as config keys (`[video] crt`, `crt_strength`) with Settings
  rows, documented in DISPLAY.md and CONFIG.md.

## Platforms

- **Run the D3D12 backend on Windows hardware.** It builds, but it has never
  run on a real device (`port/rhi/d3d12/README.md`). It also has no GPU
  counters, timestamps or mailbox present: `rhi_GetStats`,
  `rhi_TimestampsSupported` and `rhi_PresentMailbox` are stubs in
  `port/rhi/d3d12/d3d12_device.c`, so its performance lines carry CPU phases
  only. Run the three checks in docs/port/TESTING.md ("The renderer
  backends"), then add timestamp queries in `port/rhi/d3d12/`.
- **Try the Linux package on a Steam Deck.** docs/port/STEAMDECK.md
  describes what the package is built to do; the steps (first-run file
  dialog through zenity, Game Mode, the built-in controls) have not been
  checked on a Deck.
- **Shaders build only on a Linux x86-64 host.** DXC is pinned for Linux
  x86-64 alone (`tools/fetch_deps.sh`). Building on Windows or arm64 would
  need another DXC pin and a path in `cmake/IcoShaders.cmake`.
- **D3D12 headers without LGPL text.** The D3D12 backend compiles against
  the mingw-w64 headers, parts of which are LGPL-2.1+ (docs/port/THIRD_PARTY.md).
  If the project wants none, Microsoft's DirectX-Headers (MIT) can replace
  them, pinned in `tools/fetch_deps.sh`.

## Renderer

- **FMV upload slot counter.** `presentVideo` in `port/render/rd_video.c`
  picks its upload buffer with its own `s_v.counter++ % RHI_FRAMES_IN_FLIGHT`,
  independent of the RHI frame slot `rd__WaitFrame` waits for, so a movie
  present can write an upload buffer the GPU may still be reading. Index by
  the RHI's current frame slot.
- **TEXA before filtering for RGB24 and RGBA16 textures.** `sprite_ps` and
  `vu_ps` expand TEXA after the sampler filters, where the GS expands before.
  With AEM, bilinear edges between texels of different alpha differ (the
  dark-volume composite is off by up to 52 LSB on a one-pixel rim). A manual
  four-tap filter with TEXA per texel, as `fx_sprite_ps` does, would fix it
  (`port/shaders/sprite.hlsl`, `vu_common.hlsli`).
- **Perspective-correct STQ on screen prims.** Lightning strips carry
  Q = 1/w, but screen prims divide per vertex (`RD_ONCE_STQ` in
  `rd_replay.c`), so a bolt receding in depth maps its texture affinely. Pass
  Q to the sprite vertex shader and divide per pixel.
- **FBMSK scope.** `rd_FrameHead` and the `rd_Post` kinds record no FBMSK, so
  `darkVolume.c`'s PSMCT24 mask ends at its composite (`dvHostBlockEnd`)
  rather than at the next FRAME write as on the GS; list-10 draws between the
  dark volume and the anti-alias pass then write SCENE alpha on rd but not on
  the PS2. Record FBMSK 0 in `rd_FrameHead` and the post kinds
  (`rd_frame.c`, `rd_post.c`).
- **Exact motion blur loop.** The loop includes `RD_POST_REDUCTION`, a
  hardware-filtered sprite (1 LSB off). Routing the reduction through the
  `fx_sprite_ps` model would make it exact end to end (`rd_post.c`,
  `rd_blur.c`).
- **Shadow level 1 at high scales.** At 4x the first blur step samples the
  scaled count with 2x2 taps of a 4x4 footprint, so the shadow's integral
  varies 2.6% frame to frame instead of 2.0%. Box-reduce the count to the
  level-1 size first (`doShadowResolve` in `rd_replay.c`).
- **Stale FBP 0x142 mapping.** `gsTargetOfFbp` in `seki/src/GifPacket.c`
  still maps FBP 0x142 to SHADOW0, now blur level 1. Nothing uses it,
  because `Shadow.c` bypasses the decoder; remove the case or point it at
  the count target.
- **PRIM.AA1 not decoded.** GS edge antialiasing is missing on the ripple
  strips, `lineManager.c`'s lines and the particles. It needs the decoder
  (`GifPacket.c`) and a coverage-as-alpha path in the shaders.
- **Ad blend modes 8 to 11.** Modes 8 to 10 draw at half strength (Ad/255
  instead of Ad/128) and mode 11 leaves Cd unchanged. They are reachable
  only through a stage's BGA lightning record; survey the disc's lightning
  records and, if any uses them, add an exact path with a destination
  snapshot in `rd_replay.c`.
- **Performance.** Each VU draw creates its own bind group; dynamic uniform
  offsets (Vulkan `UNIFORM_BUFFER_DYNAMIC`, D3D12 root CBVs) would make it one
  set per layout per frame. `vkr_OrderWrites` (`port/rhi/vk/vk_cmd.c`) puts
  a global barrier before every render pass and copy, about 100 per frame
  in a typical stage; per-target hazard tracking would remove most. Runs of
  screen-prim commands under the same state are one draw each; merging them
  must keep the AFAIL-split and DATE boundaries.

## Interpolation (`port/render/rd_interp.c`)

- **Unmatched draws hold.** Screen prims and shadow volumes with no partner
  in the other tick stand at the tick, so a packet drawn in the previous
  frame and culled in the current one disappears half way.
- **Particle order.** Particle batches match by list order, so an emitter
  that inserts a batch ahead of another snaps both.
- **Light matrices.** The light matrices blend element by element, so a
  fast turn dims the lighting half way.
- **Morph limits.** A mesh rewritten more than once between two presents
  keeps only its last version, and past 64 morph draws per present
  (`RD_INTERP_SCRATCH`) the live stream is used.
- **Unkeyed 2D.** `kanban.c`'s signs, `staffroll.c` (keyed only by string
  through `font_Print`) and `debug.c` are not keyed, so they step at the
  tick rate.

- **Rope-top presentation and the 60 Hz climb rate.** The floaty stand-up
  at the top of a rope was a simulation defect (DIVERGENCES.md F15) and the
  headless replay is clean now, but the drawn pose over that transition was
  not dumped at the Enhanced preset with interpolation on, and the chain
  climb's phase step at 60 Hz (`TestChainUpDown` in `ico2/omori/src/chain.c`,
  `30.0f / ((60 - systemStatus[0] * 10) / systemStatus[1])`) was not compared
  against the 50 Hz value. Dump one climb-top (`[dev] dump_from`) and look at
  the half-way frames; compare the phase step in both video modes.

## Widescreen and mirror

- **Reflections at 16:9.** The reflections' render-to-texture targets stay
  4:3, so a puddle at the side of a 16:9 frame shows its reflection's
  clamped edge (`rd_water.c`, `puddle.c`, `pool.c`).
- **Narrow UI scissor.** A UI scissor narrower than the screen is not
  widened; it clips less, never more, and none has been seen.
- **Port UI at output resolution.** The port's popups and text draw into
  list 12 of the game frame (`port/ui/popup.c`), so they render at the
  scene's resolution inside the 4:3 box, are halved by the reduction, and a
  paused keep frame shows the last popup under the live one. Wanted: an
  overlay hook (`rd_OverlayPrims` or `rd_SetPresentOverlay(fn, user)`) called
  from `rd__PresentRecord` in `port/render/rd_present.c` after the scale
  blit, never mirrored, also in the headless output for `rd_ReadDisplay`
  and the replay tool; then move `popup.c` onto it.
- **Mirror glyph edges.** The reduction samples at u = x + 0.75, which is
  not mirror-symmetric, so mirrored UI glyph edges blend with the
  neighbour on the other side.
- **Mirror debug font.** `debug.c`'s list-11 font reads mirrored in
  developer mode.
- **Film stereo follows mirror mode, not `mirror_fmv`.** With mirror mode on
  and `[game] mirror_fmv = false` the film picture plays unflipped, but
  `ico_audio_pan_mirror` (`port/audio/audio_host.c`) still swaps the
  channels, because `port/audio` cannot see that a film is playing. A
  film-active flag from `port/fmv/movie.c` should gate the swap.

## Game code and 64-bit safety

- **Test stages 88 and 91 under ASan.** After the all-stage boot sweep every
  stage boots clean in the `fptrap` and heap-ASan builds except the test
  stages 88 and 91, which still report under ASan (commit 48ffde63). Boot
  each with `start_stage` in the heap-ASan build (docs/port/BOOT_DIAG.md)
  and fix the finding or record it in DIVERGENCES.md.
- **`rootUpdateY` copies the floor normal on a miss.**
  `sugipon/src/motMan_rootUpdate.c.inc` `rootUpdateY` copies `w.normal` into
  `skelRoot->plane` after `getFieldCollision(&w)` whether or not the ray hit,
  and `w` is not initialised, so on a miss the plane may be a stale stack
  value (the same class as DIVERGENCES F15 and F16). Settle from the ROM
  what the EE's frame holds there, then zero or seed `w` under `ICO_HOST`.
- **`template_audit.py` cannot follow `void *` data.** It does not see
  whole-record views through `void *` mail data
  (`ActSendMail_WithAdditionalData` to `Act.intrData` to `ClimbEndRec`) or
  `void *` work tables; DIVERGENCES D8 was found only by a replay. Teach
  `tools/template_audit.py` the mail-data senders and readers, or give the
  copy a type through an `ICO_HOST` accessor.
- **`offset_audit.py` blind spots.** It does not see raw variable-stride
  offsets (`base + j * 0x50`), `void *` table views or static initialisers,
  and neither audit pass sees calls through unprototyped declarations, which
  could pass a full pointer to a callee that reads an `int`. Prototype the
  declarations in the headers (the count under `-Wstrict-prototypes` is in
  the build log), or extend `tools/offset_audit.py`.
- **`st13c.c` stops an SE handle nobody wrote.** `script/src/st13c.c`
  `actSt13cSekizoChk` calls `soundSeDefStop(se)` with `volatile int se` never
  written, reading stack garbage on the PS2 and the host alike. Decide the
  PS2's effective value and pass it under `ICO_HOST` (-1 if none).
- **`stageSEProc.c` keeps a hand copy of `SeSlot`.** Its `SEObj` copies the
  private `SeSlot` host layout of `fumi/sound/s_init.c` field for field and
  breaks silently if `SeSlot` changes. Export `SeSlot` from
  `fumi/include/s_init.h` and drop the copy.
- **Freed model images.** Whether anything reads a model image's tables
  after the loader frees the image is not settled
  (docs/research/loader-census.md). The decoded tables live outside the
  arena, so the `asan` preset covers them; the image itself is in the arena
  and only `ICO_HEAP_ASAN` with reads instrumented would see it, which the
  documented recipe turns off.
- **Arena base lookups.** Every EE-word read calls `ico_arena_base()`
  (`common/include/eeword.h`). Caching the base in a global read inline
  would remove a call from a hot path.

## Floating point

- **Doubles (DIVERGENCES F6).** 33 functions in 20 files do `double`
  arithmetic that the PS2 ran through libgcc's dp-bit soft float (round to
  nearest); the host uses SSE doubles under the simulation's
  round-toward-zero mode, which can change frame-count scaling and camera
  and chain physics. Write `port/math/softdouble.c` from
  `sce/libgcc/dp-bit.c` and route those expressions through it under
  `ICO_HOST` (docs/port/MATH.md, "Doubles").
- **Register side effects (F10).** 15 candidate call sequences where a
  routine clobbers vf4 to vf7 between setting and reading the current matrix
  are not triaged (list in docs/port/MATH.md, "Register side effects").
- **VU0 R register (F7).** The LFSR uses PCSX2's taps 4 and 22, not checked
  on hardware; the random sequences depend on it (`port/math/matrix.c`).
- **Exact adder (F1, optional).** An exact model of the PS2 adder in
  `port/math/ps2float.h` would close the 1-ulp difference in effective
  subtraction.

## Audio

- **Windows audio push cost.** A Windows window-build log showed the
  per-vsync `audio` phase at 6 to 11 ms, while the SPU2 render itself takes
  under 1 ms; the phase also covers `ico_audio_sdl_push` (SDL's stream lock,
  `port/audio/out_sdl.c`). Running `spu2_bench.exe` on that machine and
  timing the push separately in `audio_host.c` would show where the time
  goes.
- **Output latency.** The 50 to 70 ms key-on-to-device figure is computed
  from the queue target in `out_sdl.c`, not measured. A loopback measurement
  on Windows and the Steam Deck would confirm or retune it.
- **Block loads at the end of sound RAM.** `voice_load_block` and
  `chunk_load_block` in `port/audio/spu2.c` decode 16 bytes at
  `&S.ram[addr * 2]` without wrapping, so a voice at halfword 0xFFFF9 to
  0xFFFFF reads past `ram[]`. Wrap or copy at the end of RAM, and re-check
  `spu2_render_crc`'s golden values.
- **libsd values from the disc (A9, A14).** The reverb presets and the
  `sceSdInit` register values come from ps2sdk and psx-spx rather than the
  disc's `LIBSD.IRX`. Read them from the player's disc at extraction
  (`spu2_reverb_set_preset` exists for this).
- **Driver details to settle (A3, A4, A8, A16, A19).** Read SNDN2DRV at 0x248C
  to settle whether a stream cancel touches a queued KEYON; measure the SPU2
  DMA rate (uploads take one frame on the host, which can shift load timing);
  check whether any VAG on the disc uses ADPCM filters 5 to 7; capture the
  exponential-decrease envelope's rounding on hardware.
- **A low-frequency offset after loud events.** A WAV dump of the boot run
  shows a sub-5 Hz offset of up to -5300 on the right channel for about 6 s
  after the loud cut into the opening. An offline reverb test rules out the
  reverb; it is probably in the program material, which only a PS2 capture
  can settle.

## Films

- **Play the 60 Hz and 576-line films.** In 60 Hz mode the title plays
  stage 57, `advertise.pss` (29.97 fps), and `pal_advertise576.pss`
  (stage 59) also exists; both have been inspected but not played. Run with
  `[video] video_mode = "60hz"` or `start_stage=57` / `59` and record the
  result in docs/port/FMV.md.
- **Colour conversion against a real IPU.** `port/shaders/yuv.hlsl`
  reproduces PCSX2's reference model bit-exactly, but nobody has compared
  it with PS2 output, so film colours could be a step off.

## Input and saves

- **Mouse capture on the title and pause screens.** Capture is decided by
  `boyGObj != NULL && game_pause == 0 && data_loading == 0` in
  `port/platform/window_host.c`, derived from the sources, not observed. If
  it is wrong the cursor hides in a menu or the camera moves under it.
- **Gamepad names on the remap screen.** They show SDL's English position
  names (South, LShoulder, LX-) from `port/input/bindings.c` and `keys.def`;
  they should become `ui_Str` ids in `port/ui/strings*.c`.
- **Import card images and save archives.** `port/save/mc_host.c` reads only
  folder cards; `.ps2`/`.bin` images and `.psu`/`.max`/`.cbs` archives must
  be converted with another tool first. An importer in `tools/` or in the
  first-run flow would unpack the game's save folder into the saves folder.
- **A second card in port 1.** Port 1 always reports no card. A second
  folder (`[paths] saves2`) in `mc_host.c` would let the game list both.
- **`IosMcLock` signalling.** The host signals it once per vsync so
  `iosMcMgrSync` progresses; whether libmc or the IOP does this on the PS2
  is unknown and affects how fast card requests poll. Settle it from libmc's
  disassembly and note it in SAVES.md and DIVERGENCES.md.

## Configuration and text

- **Write `config.toml` on the first run.** `ico_config_save` is called only
  by the Settings menu, so a player who never opens Settings has no file to
  edit by hand. Save it (perhaps with a commented template) after the ini
  loads in `port/platform/main_host.c`, and document it in CONFIG.md.
- **Native-speaker review of the French, German, Italian and Spanish port
  strings.** The port's own strings in `port/ui/strings_{fr,de,it,es}.c`
  (Settings, notes, achievements, popups) are the author's translations.
  The menu words transcribed from the game's own sheets need no review.
- **R8 font atlas.** Atlas pages are uploaded as RGBA8 (four times the
  memory, a whole-page `rd_UpdateTexture` for every new glyph) and drawn by
  `sprite_ps`, because `port/render/rd.h` has no R8 texture or font-shader
  selection. Wanted: `rd_CreateTextureR8`, a way for `rd_ScreenPrims` to draw
  with `font_ps`, and a sub-rectangle update; then switch `port/ui/font.c`.

## Gameplay features

- **"Shadows never take Yorda" in play.** The hooks in `omori/src/ebrain.c`,
  `common/src/backStage.c` and `fumi/src/enemy_act.c` are covered only by
  `options_test`; no recorded play-through confirms the first shadow
  encounter (`st03t.c`) and the later rooms. A pad script or a play-through
  past `st03t`'s trigger would.
- **Achievement identities.** Weapon kind 4 as the sword, kinds 8 and 9 as
  the blade of light, item kind 3 at the beach (`shore_secret`), game flag
  338 as the Queen's death and st04b/st05b as the symmetrical halls
  (`east_and_west`) are inferred from the code. A wrong guess gives an
  achievement a trigger that never fires; confirm in play and fix
  `port/game/achievements.c` if needed.
- **The later parts of the opening.** The `opening` achievement checks only
  `op.c`'s three parts; the later opening stages have their own skip loops,
  which are not hooked, so skipping them still counts as watching. Add
  `ico_gs_signal` calls in those scripts and a condition in
  `achievements.c`.
- **Run state per save.** The fresh-run challenges (`never_taken`,
  `unbroken`) need one session from New Game to the ending, because run
  state lives in memory (`port/game/gamestate.c`). Per-slot run state in
  `achievements.toml`, keyed like the `[mirror] slot_N` entries, would lift
  that.
- **Developer menu entries.** Of the 27 entries in `common/src/debug.c`'s
  `debugMenu`, only Debug Mode and Snap Shot are tested (`rd_debug_test`);
  Free Camera, Stage Select, the editors, Memory Card, the sound tests and
  the rest compile but have not been opened on the host. The camera editor
  takes a 9,200-byte block per box from the root partition, so its capacity
  should be measured too. Results go in DEVELOPER_MODE.md's table.

## Licences

- **newlib notices.** The notices in `port/math/newlib/` were written from
  the texts quoted in `docs/research/licences.md`; diff them word for word
  against the upstream fdlibm, UCB and newlib texts.

## From the final code review

- **EE identity policy for `ico2/`.** Commit 4324315c lifted the GNU nested
  functions to file-scope statics outside `ICO_HOST`, so the EE objects and
  link change (LAYOUT.md "The EE objects must not change" and MATH.md's
  gated pattern disagree with BUILDING.md's "The port changes `ico2/`
  freely"). Either re-gate the lifts (`#ifdef ICO_HOST` lift, `#else` the
  nested original, as `motionManager2.c`) or state that only `ICO_HOST`
  edits after a named revision must be EE-neutral, and run
  `tools/ee_identity.sh` with `tools/cc` over 83cb591a, d20be3fe, 718d1cc2,
  6f50fa17 and the review's `ico2/` edits.
- **A host-identifier gate for `ico2/`.** Fail CI when `ps2_*`, `ico_*`,
  `rd_*`, `_Static_assert` or `ICO_LAYOUT_*` appear outside `ICO_HOST` /
  `ICO_RD` in `ico2/` (two EE breaks, `particleEffect.c` `ps2_div` and
  `chain.c` `ps2_ftoi`, got in without one).
- **Partial clears in the audits.** Extend `tools/offset_audit.py` or
  `template_audit.py` to flag `memset`/`memcpy` with a literal size over a
  record, or over a span from a field, whose host size differs (the review
  found `_ACTCharStatus_Clear`'s 0x38 and four `ClipWork` 0xC0 clears).
- **Warnings as errors in `port/`.** Add `-Werror` to `ICO_PORT_WARNINGS`
  (`cmake/IcoFlags.cmake`) once the game headers that port TUs include
  (`typedef.h`, `thread.h`, `act.h`, `s_init.h`, `debug.h`) stop raising
  `-Wstrict-prototypes`, or compile those TUs with `-Wno-strict-prototypes`.
- **CI coverage.** Add the `asan` and `fptrap` presets (build and ctest) and
  a `win-x64-clang` build to `.github/workflows/ci.yml`.
- **`check_no_rom.sh` scope.** Rule 5b scans only `ico2/` and `sce/`; widen
  it to `port/` (exempting `port/data/extract.c`'s DATA.DF manifest) and
  read staged blobs (`git show :path`) in pre-commit mode.
- **Vulkan swapchain: a pending acquire on recreate.** `vkr_SwapchainCreate`
  drops `acquireWaitPending` while the frame's acquire semaphore may still
  be signalled; submit an empty batch that waits on it first.
- **Deferred destruction out of memory.** `vkr_Defer` / `dx_Defer` destroy or
  drop objects still referenced by unsubmitted lists when the garbage list
  cannot grow; keep a fixed overflow array.
- **D3D12 buffer copy states.** `d3dp_BufferBeginCopyDst` assumes COMMON at
  list start; settle with one debug-layer run over a stage load.
- **Side-effect-free saves lookup.** `ico_host_saves_dir` re-runs
  `ico_ini_load` (env exports, mkdir) on the game fiber from `sceMcInit`;
  split a pure layered read from the one-time export in `main_host.c`.
- **Checked path joins.** `ico_path_join` (`host_config.c`) truncates
  silently; return an error and check it where files are created.
- **Directory sync and Windows errno.** After `ico_rename_replace`, fsync the
  directory on POSIX; on Windows map `GetLastError()` to errno there.
- **Settings mid-run: video mode and language.** Both rows are reachable from
  the pause menu and change discrete state mid-run (tick rate for armed
  timers, language-selected objects); add DIVERGENCES rows or restrict them
  to the title.
- **m2v resolution change.** On `IVD_RES_CHANGED`, reset the decoder and
  reallocate the planes (`port/fmv/m2v.c`).
- **`sg/sound.c` host UB.** Unsigned spellings for the `<< 24` packet words
  and 8-byte alignment of `sgComContext` under `ICO_HOST` (needs an EE
  identity check).
- **`vu1ref_Particle` bounds.** Pass the input's qword count and clamp the
  particle count to it (`rd_mesh.c` `rd_DrawVuParticles`).
- **BGA last key (F12).** Log when the host's last-key path runs with
  `f > k->time` or with `linear` differing from the next record, then amend
  F12.
- **Small cleanups.** `dl_OpenDma(int, const void *, int)`; clear
  `texHost.bind` for a freed id in `tex_FreeTexture`; drop the dead `ICO_RD`
  `tex_TransTextureDefocus`, `RdPresentPreset.interpolate`,
  `UI_OPT_REMAP_RESET`; share `mouse_names[]` and `vsel()`; localise
  "Uncapped"/"fps"; `rd_perf` GPU records keyed by RHI frame index.

## Checks that need a PS2 or a play-through

These cannot be settled in code. Each needs a capture from a PS2 or a
session that reaches the scene, compared with the port's output.

- **Fog.** The fog's Z byte (bits 16 to 23) is derived from the documented
  GS memory layouts, and the title's strong fog haze has not been compared
  with a PS2 screen.
- **Effect sprite rounding.** The `fx_sprite_ps` model of the GS sprite UV
  step and bilinear rounding is unconfirmed.
- **The brightness sprite.** Whether the GS draws `gsb_controlBrightness`'s
  sprite, whose second corner lies above and left of the first, is unknown.
  If it does not, the brightness step never showed on the PS2 and the
  Original preset should skip it.
- **Field parity.** FIELD toggling every vsync in the non-interlaced mode is
  the host model's assumption (`port/platform/host_loop.c`).
- **Effect strength.** The strength of the puddle reflection and of the
  title's particle clouds is unconfirmed.
- **Scenes no recorded session has reached** on the renderer: depth of field
  and glow (post modes 2 to 7), the dark volume (the Queen's stage, game
  over), lightning, film noise, the pool and the Queen's barrier, the menu
  bands at 448 lines (60 Hz), and a subtitle and the staff roll under the
  mirror.
