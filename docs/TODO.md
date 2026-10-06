# TODO

Work that is wanted but not done, and facts that are not yet verified. Each
entry says what, why it matters, and where it would go. Finished work is not
listed here; `git log` has it. The other documents keep at most a one-line
pointer to this file.

## Display

- **Photo mode.** A pause-time mode (a Settings toggle or a button chord
  from the pause menu) that freezes the simulation and lets the player move
  the camera freely: orbit, dolly and roll on the sticks, field of view,
  hide the HUD, optional depth of field, and a key that writes a PNG of
  the presented frame at the output resolution (the F12 readback path in
  `port/platform/hotkeys.c` already produces one). The camera override
  sits in the renderer's per-present camera (`port/render/rd_interp.c`
  builds one rigid camera per present; a photo-mode camera replaces it
  without touching the game's camera state), so leaving the mode restores
  the game's view exactly and no game state changes. Off in the Original
  preset's output path only while active. Config keys under `[photo]`;
  rows in `port/ui/settings.c`; documented in DISPLAY.md and SETTINGS.md.

## Extras (title-screen galleries)

- **Model viewer.** A title-screen entry that loads any character or object
  model from the disc (`PObj`/`charFileManager`, the pack loaders in
  LOADERS.md), shows it on a neutral background under the renderer's normal
  lighting, lets the player orbit and zoom, and lists that model's
  animations (the motion tables `motionKind` names, the skeleton's motion
  data) so each can be triggered and looped, with the current animation's
  name and frame shown. It runs the real motion system (`sugipon/src/
  motionManager2.c`, the retail build's own Motion Viewer in
  `motionViewer.c` is a starting point, DEVELOPER_MODE.md) on an otherwise
  empty stage, so the poses are the game's. Developer mode's debug menu
  entries stay as they are; this is a player-facing gallery.

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
- **Ad blend modes 8 to 11.** Modes 8 to 10 draw at half strength (Ad/255
  instead of Ad/128) and mode 11 leaves Cd unchanged. They are reachable
  only through a stage's BGA lightning record; survey the disc's lightning
  records and, if any uses them, add an exact path with a destination
  snapshot in `rd_replay.c`.
- **Performance.** `vkr_OrderWrites` (`port/rhi/vk/vk_cmd.c`) puts
  a global barrier before every render pass and copy, about 100 per frame
  in a typical stage; per-target hazard tracking would remove most. Runs of
  screen-prim commands under the same state are one draw each; merging them
  must keep the AFAIL-split and DATE boundaries.

## Interpolation (`port/render/rd_interp.c`)

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

## Game code and 64-bit safety

- **Test stages 88 and 91 under ASan.** After the all-stage boot sweep every
  stage with data on the disc (1 to 63, 88, 91, 103 to 105) boots clean in
  the `fptrap` and heap-ASan builds except the test stages 88 and 91, which
  still report under ASan (commit 48ffde63). Boot each with `start_stage` in
  the heap-ASan build (docs/port/BOOT_DIAG.md) and fix the finding or record it in DIVERGENCES.md.
- **`rootUpdateY` copies the floor normal on a miss.**
  `sugipon/src/motMan_rootUpdate.c.inc` `rootUpdateY` copies `w.normal` into
  `skelRoot->plane` after `getFieldCollision(&w)` whether or not the ray hit,
  and `w` is not initialised, so on a miss the plane may be a stale stack
  value (the same class as DIVERGENCES F15 and F16). Settle from the ROM
  what the EE's frame holds there, then zero or seed `w`.
- **`st13c.c` stops an SE handle nobody wrote.** `script/src/st13c.c`
  `actSt13cSekizoChk` calls `soundSeDefStop(se)` with `volatile int se` never
  written, reading stack garbage on the PS2 and the host alike. Decide the
  PS2's effective value and pass it (-1 if none).
- **Freed model images.** Whether anything reads a model image's tables
  after the loader frees the image is not settled
  (docs/research/loader-census.md). The decoded tables live outside the
  arena, so the `asan` preset covers them; the image itself is in the arena
  and only `ICO_HEAP_ASAN` with reads instrumented would see it, which the
  documented recipe turns off.

## Floating point

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
- **Driver details that need a PS2 (A4, A8, A19).** Measure the SPU2 DMA
  rate (uploads take one frame on the host, which can shift load timing),
  and capture the exponential-decrease envelope's rounding on hardware.
- **ADPCM filters 5 to 7 on the disc (A3).** Scan the disc's VAG bodies and
  `.int` streams for blocks whose filter nibble is 5 to 7 or whose shift is
  13 to 15 (a static read of the data, no hardware needed); none found
  means A3 never applies.
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

- **The port font everywhere.** The 124 menu rows use the port font
  (`[game] classic_menu_text` off), but the remaining in-game text still
  comes from the disc's textures or `DisplayFont.c`'s bitmap font: the
  signs (`kanban.c`), the staff roll, the save and load screens' values,
  the game over and Continue? prompts, the subtitles. Route each through
  `port/ui/menu_text.c`'s path with the same classic fallback, so one
  typeface covers the whole game at every scale. The menu rows are drawn
  at the output's resolution in Enhanced since package DEF (UI.md "Menu
  text", RENDER_API.md "The deferred text pass"): text drawn through
  `ui_DrawTextDeferred` (`port/ui/font.h`) is composited after the box
  blit, texel for pixel, in its list order. Package TXT routes the
  subtitles (`jimaku.c`) and the staff roll through that call.
- **Native-speaker review of the French, German, Italian and Spanish port
  strings.** The port's own strings in `port/ui/strings_{fr,de,it,es}.c`
  (Settings, notes, achievements, popups) are the author's translations.
  The menu words transcribed from the game's own sheets need no review.

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
- **m2v resolution change.** On `IVD_RES_CHANGED`, reset the decoder and
  reallocate the planes (`port/fmv/m2v.c`).
- **Small cleanups.** `rd_perf` GPU records keyed by RHI frame index.

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
