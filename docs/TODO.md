# TODO

Work that is wanted but not done, and facts that are not yet verified. Each
entry says what, why it matters, and where it would go. Finished work is not
listed here; `git log` has it. The other documents keep at most a one-line
pointer to this file. Most of what is left needs something the code cannot
give: Windows or Steam Deck hardware, a PS2 capture, a play-through, a
native speaker. The rest needs no hardware and is left for a later version
(marked so below): F1's exact adder, the ADPCM filter scan, the camera
editor's second pad, the two research items (freed model images,
`IosMcLock`), and the follow-ups the packages and the final review filed.

## Platforms

- **Run the D3D12 backend on Windows hardware (needs Windows hardware).** It builds, but it has never
  run on a real device (`port/rhi/d3d12/README.md`). It also has no GPU
  counters, timestamps or mailbox present: `rhi_GetStats`,
  `rhi_TimestampsSupported` and `rhi_PresentMailbox` are stubs in
  `port/rhi/d3d12/d3d12_device.c`, so its performance lines carry CPU phases
  only. Run the three checks in docs/port/TESTING.md ("The renderer
  backends"), then add timestamp queries in `port/rhi/d3d12/`.
- **Try the Linux package on a Steam Deck (needs a Steam Deck).** docs/port/STEAMDECK.md
  describes what the package is built to do; the steps (first-run file
  dialog through zenity, Game Mode, the built-in controls) have not been
  checked on a Deck.
- **Shaders build only on a Linux x86-64 host (needs Windows or arm64 hardware to pin).** DXC is pinned for Linux
  x86-64 alone (`tools/fetch_deps.sh`). Building on Windows or arm64 would
  need another DXC pin and a path in `cmake/IcoShaders.cmake`.
- **D3D12 headers without LGPL text (a licence decision, needs a Windows build to check).** The D3D12 backend compiles against
  the mingw-w64 headers, parts of which are LGPL-2.1+ (docs/port/THIRD_PARTY.md).
  If the project wants none, Microsoft's DirectX-Headers (MIT) can replace
  them, pinned in `tools/fetch_deps.sh`.

## Interpolation (`port/render/rd_interp.c`)

- **Rope-top presentation, drawn pose.** No script reaches the top of a
  rope or chain: the recorded session (`build-host/tmp/s4/session.txt`) has
  none in ticks 4200 to 4730 of its chain stage (DIVERGENCES.md F15's note).
  Record a climb with F12 at the top, replay it with `dump_interp=1` and
  `dump_from` there, and look at `rd_replay_tool --interp 0.5` frames at the
  Enhanced preset. The 60 Hz phase step is settled (the same 30 units a
  second as at 50 Hz).

## Game code and 64-bit safety

- **Freed model images (research; left for a later version, no hardware needed).** Whether anything reads a model image's tables
  after the loader frees the image is not settled
  (docs/research/loader-census.md). The decoded tables live outside the
  arena, so the `asan` preset covers them; the image itself is in the arena
  and only `ICO_HEAP_ASAN` with reads instrumented would see it, which the
  documented recipe turns off.

## Floating point

- **VU0 R register (F7; needs a PS2).** The LFSR uses PCSX2's taps 4 and 22, not checked
  on hardware; the random sequences depend on it (`port/math/matrix.c`).
- **Exact adder (F1, optional; left for a later version, no hardware needed).** An exact model of the PS2 adder in
  `port/math/ps2float.h` would close the 1-ulp difference in effective
  subtraction.

## Audio

- **Windows audio push cost (needs Windows hardware).** A Windows window-build log showed the
  per-vsync `audio` phase at 6 to 11 ms, while the SPU2 render itself takes
  under 1 ms; the phase also covers `ico_audio_sdl_push` (SDL's stream lock,
  `port/audio/out_sdl.c`). Running `spu2_bench.exe` on that machine and
  timing the push separately in `audio_host.c` would show where the time
  goes.
- **Output latency (needs Windows and Steam Deck hardware).** The 50 to 70 ms key-on-to-device figure is computed
  from the queue target in `out_sdl.c`, not measured. A loopback measurement
  on Windows and the Steam Deck would confirm or retune it.
- **Driver details (A4, A8, A19; need a PS2).** Measure the SPU2 DMA
  rate (uploads take one frame on the host, which can shift load timing),
  and capture the exponential-decrease envelope's rounding on hardware.
- **ADPCM filters 5 to 7 on the disc (A3; filed by package AU1; left for a later version: a disc scan, no hardware needed).** Scan the disc's VAG bodies and
  `.int` streams for blocks whose filter nibble is 5 to 7 or whose shift is
  13 to 15 (a static read of the data, no hardware needed); none found
  means A3 never applies.
- **A low-frequency offset after loud events (needs a PS2 capture).** A WAV dump of the boot run
  shows a sub-5 Hz offset of up to -5300 on the right channel for about 6 s
  after the loud cut into the opening. An offline reverb test rules out the
  reverb; it is probably in the program material, which only a PS2 capture
  can settle.

## Films

- **Colour conversion against a real IPU (needs a PS2 capture).** `port/shaders/yuv.hlsl`
  reproduces PCSX2's reference model bit-exactly, but nobody has compared
  it with PS2 output, so film colours could be a step off.

## Input and saves

- **Mouse capture on the title and pause screens (needs a play-through on a desktop).** Capture is decided by
  `boyGObj != NULL && game_pause == 0 && data_loading == 0` in
  `port/platform/window_host.c`, derived from the sources, not observed. If
  it is wrong the cursor hides in a menu or the camera moves under it.
- **The importer on real cards; `.max` and `.cbs` (needs real card images; package S1).** `mc_import`
  (docs/port/SAVES.md, "Importing saves") is tested only on synthetic
  `.ps2` images and `.psu` files built to the same description it reads.
  Import a real PCSX2 `.ps2`, a plain `.bin` dump and a uLaunchELF `.psu`
  holding ICO's save, and load the save in the game. `.max` (LZARI) and
  `.cbs` are refused; decode them once sample files are at hand.
- **`IosMcLock` signalling (research; left for a later version: needs libmc's disassembly, no hardware).** The host signals it once per vsync so
  `iosMcMgrSync` progresses; whether libmc or the IOP does this on the PS2
  is unknown and affects how fast card requests poll. Settle it from libmc's
  disassembly and note it in SAVES.md and DIVERGENCES.md.

## Configuration and text

- **Native-speaker review (needs native speakers) of the French, German, Italian and Spanish port
  strings.** The port's own strings in `port/ui/strings_{fr,de,it,es}.c`
  (Settings, notes, achievements, popups) are the author's translations.
  The menu words transcribed from the game's own sheets need no review.

## Gameplay features

- **"Shadows never take Yorda" in play (needs a play-through).** The hooks in `omori/src/ebrain.c`,
  `common/src/backStage.c` and `fumi/src/enemy_act.c` are covered only by
  `options_test`; no recorded play-through confirms the first shadow
  encounter (`st03t.c`) and the later rooms. A pad script or a play-through
  past `st03t`'s trigger would.
- **Achievement identities (need a play-through).** Weapon kind 4 as the sword, kinds 8 and 9 as
  the blade of light, item kind 3 at the beach (`shore_secret`), game flag
  338 as the Queen's death and st04b/st05b as the symmetrical halls
  (`east_and_west`) are inferred from the code. A wrong guess gives an
  achievement a trigger that never fires; confirm in play and fix
  `port/game/achievements.c` if needed.
- **Camera editor needs a second pad (a defect found by the DEV package; left for a later version: a code change, checked with a pad script).** The Developer menu's Camera Editor
  (`omori/src/camera-editor.c`) reads only `pad[1]`, which the host never
  connects (`port/input/pad_host.c`: port 0 slot 0), so it opens but cannot
  be driven or left: every control, including the exit (`menu` thread,
  `exit_f`), is a pad 2 press. Repro: `[gameplay] developer_mode = true`,
  `start_stage=11`, a pad script pressing SELECT (0100) at tick 150, DOWN
  six times (row 6), CIRCLE: the menu stays in the
  editor for the rest of the run (docs/port/DEVELOPER_MODE.md). Its box
  capacity ("memory full") could not be measured for the same reason. Map a
  second pad or a keyboard layer to `pad[1]` in developer mode, or let
  SELECT on pad 1 leave the editor. Pad2 Control has the same limit.

## Follow-ups from the final code review

Each was filed by the review or by a package and not done; what each needs
is in its title.

- **D3D12 buffer copy states (needs Windows hardware).** `d3dp_BufferBeginCopyDst` assumes COMMON at
  list start; settle with one debug-layer run over a stage load.
- **The Queen's barrier block at 16:9 (left for a later version; no hardware needed).** The
  widescreen reflections widen the puddle's and the pool's blocks by 1 / f
  and map the game's 4:3 u for draws that sample them
  (docs/port/RENDER_API.md, section 13, `RdTargetRec.wideBlock`,
  `gs_block_uv`), but the barrier's 512x256 block, which has no depth
  buffer, is not widened. At 16:9 the barrier's refraction therefore samples
  a 4:3 block under a widened scene: its picture is stretched across the
  wider screen. Widen the block as the others are (or show it needs no
  widening) and add the barrier at 16:9 to `rd_water`, which checks the
  puddle and the pool only.
- **A near-white Enhanced frame on the Music page (unverified; left for a later version, no hardware needed).**
  Package UIFIX reported one frame drawn nearly white in the Enhanced
  preset at Main tick 1000 with Settings > Extras > Music open; it was not
  reproduced or explained. Repro: the window build, `[video] preset =
  "enhanced"`, a pad script with `port/input/pad-boot.txt`'s presses up to
  tick 440 then Down (4000) at 560, Cross (0040) at 585, Down six times from
  630 every 15 ticks, Cross at 720 and Cross at 775 (Music opens at 785:
  `port/ui/test/headless_common.py` `PadScript.to_extras`, as
  `gallery_sweep.py` writes it), `ticks=1010`, `dump_every=1`,
  `dump_from=990`; replay the dumps around tick 1000 with `rd_replay_tool`
  and compare them with the frames before and after.

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
