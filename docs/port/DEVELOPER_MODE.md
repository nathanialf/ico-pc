# Developer mode (renderer wave 6, package R6a)

The retail ELF still carries the development build's debug menu
(`common/src/debug.c`: `debug_Menu`, the 27-entry `debugMenu`, the 76-entry
`debugOption` table and the tools they open), but `Main` never calls
`debug_Menu`: the development build's call was compiled out, and its debug
text calls became the empty `debug_PrintfDummy`. Developer mode restores
both on the host.

| | |
| --- | --- |
| switch | `config.toml` `[gameplay] developer_mode = true` (default `false`); `ico_opt_developer_mode()` / `ico_opt_set_developer_mode()` (`port/game/options.h`, docs/port/OPTIONS.md) |
| option file | `config.toml` `[dev] debug_option` (int, default 0), `ico_opt_debug_option()` |
| host0: files | `<pref>/dev/` (`port/data/sifdev_host.c`; `<pref>` is the per-user folder, docs/port/CONFIG.md) |
| snapshots | `<pref>/dev/screenshots/snapNNNNNNN.png` |
| achievements | suspended while on (the achievements package reads `ico_opt_developer_mode()`) |
| trace | the first line of every trace is `# developer_mode 0` or `1` (`port/platform/trace_host.c`) |

## What it changes

With `developer_mode` false nothing differs from the build before R6a: the
`debug_Menu` call is not made, `debug_PrintfDummy` prints nothing,
`debugSceOpen` opens the retail build's `cdrom0:` path (which finds nothing),
`debug_VariableInit` sets the retail values. Measured: the headless
4000-tick boot run (`pad-boot.txt`) gives the same trace lines as the build
before R6a, with developer mode off and on (below).

With it true:

1. **The menu.** `Main` (`common/src/main.c`) calls `debug_Menu()` every
   tick, after `ExecKeyInput()` (the pad has been read) and before
   `ExecIcoMisc()` (whose layout code can clear `pad[0].flags`). Where the
   development build made the call is not known; the comment in `Main` only
   records that it did. Until SELECT is pressed `debug_Menu` reads the pad and
   returns, so the game runs as without the mode.
2. **Its text.** `debug_PrintfDummy` formats and draws through the debug font
   (`debug_PrintFont`), as `debug_Printf` does. That shows the menu, its
   pages and tools, and the development build's status lines that went
   through the dummy (icoMisc.c's `DISK ERROR` and `IOP BUFF OVER`,
   commonact.c's emergency lines, act.c's coordinates, ...). The calls that
   stayed `debug_Printf` in retail print in every build, as on the PS2,
   gated by the option table (mostly `DebugFont`).
3. **host0:.** `debugSceOpen` opens `host0:<name>`, a file under
   `<pref>/dev/`.
4. **The option file.** With `[dev] debug_option` not 0, `debug_VariableInit`
   ends with `debug_GetDebugOption`, which loads
   `<pref>/dev/thisIsYourDebugOption`: the file the Debug Mode page writes
   when TRIANGLE is pressed (76 pairs of `#<name>` and value lines, the
   development build's format). A file with a wrong name or count is ignored
   (the retail values stay). This takes the place of the development build's
   check for that file on the PC side of the kit; 0 (the default) keeps the
   retail values.

`[dev] start_stage` (renderer wave 5) is independent of developer mode: it
feeds `debug_TryToGetStartStage` in every build.

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
and sees the same presses (the development build was the same).
`port/input/pad-boot.txt` never presses SELECT, so a scripted boot does not
open the menu.

## The menu entries

"Host" says what happens on the host; "not exercised" means the entry
compiles and has its host paths but no run or test has opened it yet.

| # | entry | host | files (under `<pref>/dev/`) |
| --- | --- | --- | --- |
| 0 | Debug Mode | works: the 76 options (below); `rd_debug_test` checks the page and the file round trip | `thisIsYourDebugOption` (write: TRIANGLE; read at boot with `debug_option`) |
| 1 | Free Camera | not exercised (`CameraSetMode(1)`; SELECT leaves) | |
| 2 | Stage Select | not exercised: the 106 `stageData` rows, CIRCLE switches stage (rows whose data file has `NOCD_` are refused, as on the PS2) | |
| 3 | Target Object | not exercised (`debug_menu.c`: camera on the chosen object, which blinks) | |
| 4 | Stage Setting | not exercised (`GsBase.c` `gsb_StageSetting`) | read/write `object/stagesetting/<key2>.ssb`, `<key2>.lock`; append `object/stagesetting/change.txt` |
| 5 | Way Test | not exercised (`fumi/src/way_tool.c` `debug_WayTool`) | write `test.wp`, `way0000.txt`; read `test.wp` |
| 6 | Camera Editor | not exercised (`omori/src/camera-editor.c`) | write `ico2Data/<camera set name>` (camera-editor.c's sets, package 2F's host records), `a.txt` (the text dump) |
| 7 | Motion Viewer | not exercised (`sugipon/src/motionViewer.c`) | |
| 8 | Effect Tool | not exercised (`sugipon/src/effectTool.c`) | write `particleEffectFile[id].path` (the effect's own path) |
| 9 | TextureList | not exercised (`seki/src/Texture.c` `tex_ListTool`) | |
| 10 | Snap Shot | works when SnapSize is not None (Debug Mode page): a PNG of the last presented frame (rd's DISPLAY target, the reduced 512 x 224/256 picture); window build only, the headless build writes nothing. SnapForm (TIM2/BMP) and the SnapSize tiling are ignored; `rd_debug_test` checks the PNG | write `screenshots/snapNNNNNNN.png` |
| 11 | Memory Card | not exercised: LOAD, SAVE, DELETE, FORMAT, UNFORMAT and TEST act on the host card (`port/save/mc_host.c`, the `saves` folder, docs/port/SAVES.md); FORMAT and DELETE destroy saves | |
| 12 | STAFF ROLL TEST | not exercised (`staffRollStart`) | |
| 13 | ADPCM TEST | not exercised: the 105 `adpcmFile` streams | |
| 14 | SE TEST | not exercised: the `seDef` sounds at the boy | |
| 15 | REVERB TEST | not exercised (UP/DOWN change the depth) | |
| 16 | Game Over | does nothing but close the menu (the retail body) | |
| 17 | Ending Demo | does nothing but close the menu (the retail body) | |
| 18 | BackStage Test | not exercised (`backStageProcessInStage` with 10,000,000 s away) | |
| 19 | LoadINFO | not exercised: per-stage load sizes, which `fumi/ios/cdvd.c` counts in every build | |
| 20 | Chara Info | not exercised (`_ACTDebugPrint` of the chosen actor) | |
| 21 | Pad2 Control | not exercised (`CurrentTargetGObjSub`) | |
| 22 | DispBox | not exercised (`DebugDispBox` at the boy) | |
| 23 | DispBall | not exercised (a wire sphere and `scpTriggerPosBall`) | |
| 24 | Collision Test | not exercised (a ray moved with the right stick, `ClipCollision`) | |
| 25 | Hint Start | not exercised (the first hint object) | |
| 26 | Tsuresari Time Zero | not exercised (`backStageDebugTimeZero`) | |

Unavailable on the host (they needed the development kit or its build):

- **The exception screen** (`debug_exception.c`, `debug_exception_screen.c.inc`):
  the EE kernel's debug handlers, a register dump and call trace from
  `TRTABLE.BIN`/`TRFILE.TXT`/`SRCFILE.TXT`, drawn straight to the GS. The
  host compiles `debug_exception.c` with host bodies only: a fault is the
  host crash handler's (docs/port/BOOT_DIAG.md), `debug_assert`,
  `debug_assertMessage` and "IOP DEAD" report to the log and abort (the PS2
  hung on the screen), `debug_SetExceptionMessage` keeps its message for
  that report.
- **The profiler bars** (`DebugBar`, `DebugBarProfileType`,
  `DebugBarStartItem`, `DebugBarScale`, `BrainBar`'s timing): the
  development `Main` recorded them (`debug_SetBar`, `debug_SetBar2`) and
  the GIF DMA interrupt latched EE timer 0 (`debug_SetDmaCallback`); retail
  calls neither `debug_SetBar` nor `debug_DispBar`, and the host installs no
  DMA handler.
- **The VU1 register dumps** (`debug_DispVu1FReg`, `IReg`, `SReg`): no VU1;
  they return at once (they printed to the TTY only).
- **The log file** (`debug_openLog`): retail's body opens nothing.

## The options (Debug Mode page)

All 76 are edited in place; the ones whose reader was compiled out of the
retail code do nothing. Readers found by
`grep -a -rlE '\b<variable>\b' ico2 --include=*.c --include=*.inc`
(debug.c excluded):

- **No reader** (no effect): DebugFrameStep (`debug_frame`), Printf,
  DebugBarProfileType, DebugBarStartItem, DebugBarScale, CollisionRayDisp
  (only through its change function, `ChangeFieldCollisionDebugMode`, which
  does act), CharaTarget, GBrainInfo, Jimaku Test, RippleRoughness,
  Scissoring, DispClusterModel, DispNormalModel, DispLwsModel, DISP ENEMY
  STATE, GIRL PAD CONTROL (only through `ChangeGirlControlMode`, which
  sets `girlControlMode` when turned on).
- **Read by the game**: the rest, e.g. FrameStep and NTSC/PAL
  (`systemStatus[1]`, `[0]`; a change resets the GS with `gsResetFunc`),
  DebugFont/2/3 (the debug text of many files), WallCheck, FieldCollision,
  Skelton, the hair and chain tuning, BrainOnOff, ENEMY BATTLE TYPE, NEW
  QUEEN BATTLE, ONE HIT ONLY, IGNORE DODGE, GAME CLEAR COUNT
  (`gFlagGameClear`), Specular, ShadowOff, FullScreenEffect, DispParticle,
  DispMesh, DISPLAY BRIGHTNESS (`systemStatus[11]`). These change the
  simulation or the picture as in the development build; whether each
  debug drawing is complete on rd is not checked option by option.

## How the drawing reaches rd

The debug font is a VU1 routine on the PS2 (`ico2/vusrc/vu1_common.h`:
SET_FONT_OFFSET, START_DEBUG_FONT, SPACE_DEBUG_FONT, codes 8, 10 and 12 of
every resident program): `debug_MakeFont` builds one VIF packet per glyph
(its outlined 8 x 8 bitmap as point quadwords), `debug_PrintCharacter`
chains a state packet and the glyphs into list 12. On the host
(`ICO_RD`) `debug_PrintCharacter` still builds and chains them, and runs the
routine in C over the same packets (`debugHostFontSet`,
`debugHostFontGlyph`): each glyph's GIF packet (PRIM 0x40, points with ABE;
RGBAQ and XYZ2 per point) goes to the GS register decoder through
`gif_HostWriteRegs` (docs/port/RENDER_API.md section 9), in order with the
list's other writes. The text backdrop, the font window, the menu's other
drawing (`gif_Sprite`, `gif_Line`, `prim_DispWireSphere`) are `gif_*`
calls, which the decoder already decodes. The glyph packets live in a host
buffer, not the stage partition (debug.c `debugHostFontMem`: on the host the
extra blocks exhausted the "stage" partition in stage 42 of the boot run).

The csv windows (`_debug_SelectCsvWindow` and its wrappers) take the PS2
layout of each table (stride, text offset, char * or inline text); on the
host `csvWindowHost` converts it (debug.c's comment lists the tables), and
the value functions of `debug_SelectCsvWindowVal` are called as returning a
`char *`. The memory card entries use the global `mc` where the PS2 passed
its address through an `int`.

## Runs and tests (2026-10-05)

- `rd_debug_test` (ctest `rd_debug`): the font's points, the menu, the
  option page, the font window, the option file round trip, no undecoded
  register; on lavapipe a glyph's pixels and the snapshot PNG.
- `options_test`: `developer_mode` and `debug_option` read from
  `config.toml`.
- Headless `linux-x64`, 4000 ticks, `pad-boot.txt`
  (`build-host/r6a-run-{base,off,on}`): the build before R6a, R6a with
  developer mode off, and on: all three exit at 4000 Main ticks, 8007
  vsyncs, stage 3; the trace lines are identical (SHA-1 of the lines after
  the `#` header lines `8a9d403e1a33fbfaefd05961598cb173b2359d21`).
- The window build run is in docs/port/RENDER_API.md section 9 (R6a).
