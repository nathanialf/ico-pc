# Developer mode

The retail ELF still carries the development build's debug menu
(`common/src/debug.c`: `debug_Menu`, the 27-entry `debugMenu`, the 76-entry
`debugOption` table and the tools they open), but its `Main` never calls
`debug_Menu`: the development build's call was compiled out, and its debug
text calls became the empty `debug_PrintfDummy`. Developer mode restores
both on the host.

| | |
| --- | --- |
| switch | `config.toml` `[gameplay] developer_mode = true` (default `false`), or Settings > Developer mode; `ico_opt_developer_mode()` / `ico_opt_set_developer_mode()` (`port/game/options.h`, docs/port/OPTIONS.md) |
| option file | `config.toml` `[dev] debug_option` (int, default 0), `ico_opt_debug_option()` |
| host0: files | `<pref>/dev/` (`port/data/sifdev_host.c`; `<pref>` is the per-user folder, docs/port/CONFIG.md) |
| snapshots | `<pref>/dev/screenshots/snapNNNNNNN.png` |
| achievements | suspended while it is on and for the rest of the run: no counter advances and nothing unlocks (docs/port/ACHIEVEMENTS.md, "Suspension") |
| trace | the first line of every trace is `# developer_mode 0` or `1` (`port/platform/trace_host.c`) |

## What it changes

With `developer_mode` off nothing differs from the retail game: the
`debug_Menu` call is not made, `debug_PrintfDummy` prints nothing,
`debugSceOpen` opens the retail build's `cdrom0:` path (which finds
nothing), and `debug_VariableInit` sets the retail values. A boot run with
the mode off and with it on (SELECT never pressed) produces identical
traces, so turning it on does not change the simulation by itself.

With it on:

1. **The menu.** `Main` (`common/src/main.c`) calls `debug_Menu()` every
   tick, after `ExecKeyInput()` (the pad has been read) and before
   `ExecIcoMisc()` (whose layout code can clear `pad[0].flags`). Where the
   development build made the call is not known; the comment in `Main`
   records only that it did. Until SELECT is pressed `debug_Menu` reads the
   pad and returns, so the game runs as without the mode.
2. **Its text.** `debug_PrintfDummy` formats and draws through the debug
   font (`debug_PrintFont`), as `debug_Printf` does. That shows the menu,
   its pages and tools, and the development build's status lines that went
   through the dummy (`icoMisc.c`'s `DISK ERROR` and `IOP BUFF OVER`,
   `commonact.c`'s emergency lines, `act.c`'s coordinates, and others). The
   calls that stayed `debug_Printf` in retail print in every build, as on
   the PS2, gated by the option table (mostly `DebugFont`).
3. **host0:.** `debugSceOpen` opens `host0:<name>`, a file under
   `<pref>/dev/`.
4. **The option file.** With `[dev] debug_option` not 0,
   `debug_VariableInit` ends with `debug_GetDebugOption`, which loads
   `<pref>/dev/thisIsYourDebugOption`: the file the Debug Mode page writes
   when TRIANGLE is pressed (76 pairs of `#<name>` and value lines, the
   development build's format). A file with a wrong name or count is
   ignored and the retail values stay. This replaces the development
   build's check for that file on the PC side of the kit.

`[dev] start_stage` is independent of developer mode: it feeds
`debug_TryToGetStartStage` in every build, and `Main` then applies
`[video] video_mode` to `systemStatus[0]` as the boot's step 200 would
have (docs/port/CONFIG.md).

## Using the menu

The pad bits are the game's logical word (docs/port/INPUT.md: CROSS 0040,
START 0800).

| button | menu | lists (csv windows) | Debug Mode page |
| --- | --- | --- | --- |
| SELECT (0100) | opens the menu (when R2 is not held) | back | back |
| UP / DOWN | | cursor (repeat) | cursor (repeat) |
| RIGHT / LEFT | | | value + / - (wraps) |
| CIRCLE (0020) | | choose | back to the menu, prints the table to the (empty) TTY |
| CROSS (0040) | | back | back |
| TRIANGLE (0010) | | | save `thisIsYourDebugOption` |
| SQUARE (0080) | | toggles the text backdrop (`debug_font_flag` bit 2) | |

Holding R2 (0002) freezes the lists. The game keeps running under the menu
and sees the same presses, as in the development build.
`port/input/pad-boot.txt` never presses SELECT, so a scripted boot does not
open the menu.

## The menu entries

"Host" says what the entry does on the host. "Untested" means the entry
compiles and has its host paths, but no run or test has opened it.

| # | entry | host | files (under `<pref>/dev/`) |
| --- | --- | --- | --- |
| 0 | Debug Mode | works: the 76 options (below); `rd_debug_test` checks the page and the file round trip | `thisIsYourDebugOption` (written by TRIANGLE; read at boot with `debug_option`) |
| 1 | Free Camera | untested (`CameraSetMode(1)`; SELECT leaves) | |
| 2 | Stage Select | untested: the 106 `stageData` rows; CIRCLE switches stage (rows whose data file has `NOCD_` are refused, as on the PS2) | |
| 3 | Target Object | untested (`debug_menu.c`: camera on the chosen object, which blinks) | |
| 4 | Stage Setting | untested (`GsBase.c` `gsb_StageSetting`) | read/write `object/stagesetting/<key2>.ssb`, `<key2>.lock`; append `object/stagesetting/change.txt` |
| 5 | Way Test | untested (`fumi/src/way_tool.c` `debug_WayTool`) | write `test.wp`, `way0000.txt`; read `test.wp` |
| 6 | Camera Editor | untested (`omori/src/camera-editor.c`) | write `ico2Data/<camera set name>` (the editor's camera sets in their host record layout), `a.txt` (the text dump) |
| 7 | Motion Viewer | untested (`sugipon/src/motionViewer.c`) | |
| 8 | Effect Tool | untested (`sugipon/src/effectTool.c`) | write `particleEffectFile[id].path` (the effect's own path) |
| 9 | TextureList | untested (`seki/src/Texture.c` `tex_ListTool`) | |
| 10 | Snap Shot | works when SnapSize is not None (Debug Mode page): a PNG of the last presented frame (the renderer's DISPLAY target, the reduced 512 x 224/256 picture); window build only, the headless build writes nothing. SnapForm (TIM2/BMP) and the SnapSize tiling are ignored; `rd_debug_test` checks the PNG | write `screenshots/snapNNNNNNN.png` |
| 11 | Memory Card | untested: LOAD, SAVE, DELETE, FORMAT, UNFORMAT and TEST act on the host card (`port/save/mc_host.c`, docs/port/SAVES.md); FORMAT and DELETE destroy saves | |
| 12 | STAFF ROLL TEST | untested (`staffRollStart`) | |
| 13 | ADPCM TEST | untested: the 105 `adpcmFile` streams | |
| 14 | SE TEST | untested: the `seDef` sounds at the boy | |
| 15 | REVERB TEST | untested (UP/DOWN change the depth) | |
| 16 | Game Over | only closes the menu (the retail body) | |
| 17 | Ending Demo | only closes the menu (the retail body) | |
| 18 | BackStage Test | untested (`backStageProcessInStage` with 10,000,000 s away) | |
| 19 | LoadINFO | untested: per-stage load sizes, which `fumi/ios/cdvd.c` counts in every build | |
| 20 | Chara Info | untested (`_ACTDebugPrint` of the chosen actor) | |
| 21 | Pad2 Control | untested (`CurrentTargetGObjSub`) | |
| 22 | DispBox | untested (`DebugDispBox` at the boy) | |
| 23 | DispBall | untested (a wire sphere and `scpTriggerPosBall`) | |
| 24 | Collision Test | untested (a ray moved with the right stick, `ClipCollision`) | |
| 25 | Hint Start | untested (the first hint object) | |
| 26 | Tsuresari Time Zero | untested (`backStageDebugTimeZero`) | |

These parts of the development tooling are unavailable on the host because
they needed the development kit or its build:

- **The exception screen** (`debug_exception.c`,
  `debug_exception_screen.c.inc`): the EE kernel's debug handlers, a
  register dump and call trace from `TRTABLE.BIN`/`TRFILE.TXT`/
  `SRCFILE.TXT`, drawn straight to the GS. The host compiles
  `debug_exception.c` with host bodies only: a fault goes to the host crash
  handler (docs/port/BOOT_DIAG.md); `debug_assert`, `debug_assertMessage`
  and "IOP DEAD" report to the log and abort, where the PS2 hung on the
  screen; `debug_SetExceptionMessage` keeps its message for that report.
- **The profiler bars** (`DebugBar`, `DebugBarProfileType`,
  `DebugBarStartItem`, `DebugBarScale`, `BrainBar`'s timing): the
  development `Main` recorded them (`debug_SetBar`, `debug_SetBar2`) and the
  GIF DMA interrupt latched EE timer 0 (`debug_SetDmaCallback`). Retail
  calls neither `debug_SetBar` nor `debug_DispBar`, and the host installs
  no DMA handler.
- **The VU1 register dumps** (`debug_DispVu1FReg`, `IReg`, `SReg`): there
  is no VU1; they return at once (they printed to the TTY only).
- **The log file** (`debug_openLog`): retail's body opens nothing.

## The options (Debug Mode page)

All 76 options are edited in place; the ones whose reader was compiled out
of the retail code do nothing. The readers were found by searching `ico2`
for each variable outside `debug.c`.

- **No reader** (no effect): DebugFrameStep (`debug_frame`), Printf,
  DebugBarProfileType, DebugBarStartItem, DebugBarScale, CollisionRayDisp
  (only through its change function, `ChangeFieldCollisionDebugMode`, which
  does act), CharaTarget, GBrainInfo, Jimaku Test, RippleRoughness,
  Scissoring, DispClusterModel, DispNormalModel, DispLwsModel, DISP ENEMY
  STATE, GIRL PAD CONTROL (only through `ChangeGirlControlMode`, which sets
  `girlControlMode` when turned on).
- **Read by the game**: the rest, for example FrameStep and NTSC/PAL
  (`systemStatus[1]`, `[0]`; a change resets the GS with `gsResetFunc`),
  DebugFont/2/3 (the debug text of many files), WallCheck, FieldCollision,
  Skelton, the hair and chain tuning, BrainOnOff, ENEMY BATTLE TYPE, NEW
  QUEEN BATTLE, ONE HIT ONLY, IGNORE DODGE, GAME CLEAR COUNT
  (`gFlagGameClear`), Specular, ShadowOff, FullScreenEffect, DispParticle,
  DispMesh, DISPLAY BRIGHTNESS (`systemStatus[11]`). These change the
  simulation or the picture as in the development build. Whether each
  debug drawing is complete on the renderer has not been checked option by
  option.

## How the drawing reaches the renderer

On the PS2 the debug font is a VU1 routine (`ico2/vusrc/vu1_common.h`:
SET_FONT_OFFSET, START_DEBUG_FONT, SPACE_DEBUG_FONT, codes 8, 10 and 12 of
every resident program). `debug_MakeFont` builds one VIF packet per glyph
(its outlined 8 x 8 bitmap as point quadwords), and `debug_PrintCharacter`
chains a state packet and the glyphs into list 12. On the host
`debug_PrintCharacter` still builds and chains them, and then runs the
routine in C over the same packets (`debugHostFontSet`,
`debugHostFontGlyph`): each glyph's GIF packet (PRIM 0x40, points with ABE;
RGBAQ and XYZ2 per point) goes to the GS register decoder through
`gif_HostWriteRegs` (docs/port/RENDER_API.md, the seki layer), in order
with the list's other writes. The text backdrop, the font window and the
menu's other drawing (`gif_Sprite`, `gif_Line`, `prim_DispWireSphere`) are
`gif_*` calls the decoder already handles. The glyph packets live in a host
buffer (`debugHostFontMem`) rather than the stage partition, because on the
host the extra blocks exhausted the "stage" partition in larger stages.

The csv windows (`_debug_SelectCsvWindow` and its wrappers) take the PS2
layout of each table (stride, text offset, `char *` or inline text); on
the host `csvWindowHost` converts it (the comment in `debug.c` lists the
tables), and the value functions of `debug_SelectCsvWindowVal` are called
as returning a `char *`. The memory card entries use the global `mc` where
the PS2 passed its address through an `int`.

## Tests

- `rd_debug_test` (ctest `rd_debug`): the font's points, the menu, the
  option page, the font window, the option file round trip, no undecoded
  register; with a Vulkan device (lavapipe in CI) a glyph's pixels and the
  snapshot PNG.
- `options_test`: `developer_mode` and `debug_option` read from
  `config.toml`.

Most menu entries are untested on the host; docs/TODO.md tracks this.
