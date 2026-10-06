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
have (docs/port/CONFIG.md). Like Stage Select's refusal of the `NOCD_` rows,
`Main` refuses a start stage whose data file is not on the disc (`NOCD_` or
`ONLYSAMPLE_` in `stageData[].dataFile`: 64 to 87, 89, 90, 92 to 102), logs
`start_stage N (name) refused` and boots normally; booted, those stages die
building their objects at Main tick 66.

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

"Host" says what the entry does on the host; "opened on the host" is what
a headless run found (below).

| # | entry | host | files (under `<pref>/dev/`) | opened on the host |
| --- | --- | --- | --- | --- |
| 0 | Debug Mode | works: the 76 options (below); `rd_debug_test` checks the page and the file round trip | `thisIsYourDebugOption` (written by TRIANGLE; read at boot with `debug_option`) | tested (`rd_debug_test`) |
| 1 | Free Camera | (`CameraSetMode(1)`; SELECT leaves) |  | opens, SELECT leaves |
| 2 | Stage Select | the 106 `stageData` rows; CIRCLE switches stage (rows whose data file has `NOCD_` are refused, as on the PS2) |  | opens; CIRCLE on row 12 switched stage 11 to 12 at tick 244, the menu closed and the stage ran on (280 ticks); row 0 closes the menu without switching |
| 3 | Target Object | (`debug_menu.c`: camera on the chosen object, which blinks) |  | opens, SELECT leaves |
| 4 | Stage Setting | (`GsBase.c` `gsb_StageSetting`) | read/write `object/stagesetting/<key2>.ssb`, `<key2>.lock`; append `object/stagesetting/change.txt` | opens, SELECT leaves; wrote nothing under `dev/` in 90 ticks |
| 5 | Way Test | (`fumi/src/way_tool.c` `debug_WayTool`) | write `test.wp`, `way0000.txt`; read `test.wp` | opens, SELECT leaves; wrote nothing under `dev/` (no waypoints were set) |
| 6 | Camera Editor | (`omori/src/camera-editor.c`) | write `ico2Data/<camera set name>` (the editor's camera sets in their host record layout), `a.txt` (the text dump) | opens but cannot be driven or left: every control reads `pad[1]`, which the host never connects (port 0 only); SELECT and CROSS do nothing, the menu stays in the entry for the rest of the run (no crash, exit 0). TODO.md |
| 7 | Motion Viewer | (`sugipon/src/motionViewer.c`) |  | opens, SELECT leaves |
| 8 | Effect Tool | (`sugipon/src/effectTool.c`) | write `particleEffectFile[id].path` (the effect's own path) | opens, SELECT leaves; wrote no effect file |
| 9 | TextureList | (`seki/src/Texture.c` `tex_ListTool`) |  | opens, SELECT leaves |
| 10 | Snap Shot | works when SnapSize is not None (Debug Mode page): a PNG of the last presented frame (the renderer's DISPLAY target, the reduced 512 x 224/256 picture); window build only, the headless build writes nothing. SnapForm (TIM2/BMP) and the SnapSize tiling are ignored; `rd_debug_test` checks the PNG | write `screenshots/snapNNNNNNN.png` | tested (`rd_debug_test`) |
| 11 | Memory Card | LOAD, SAVE, DELETE, FORMAT, UNFORMAT and TEST act on the host card (`port/save/mc_host.c`, docs/port/SAVES.md); FORMAT and DELETE destroy saves |  | opens; CIRCLE on the first item (LOAD) with an empty card folder, 100 ticks, SELECT leaves; no card file written. FORMAT, DELETE and SAVE were not pressed |
| 12 | STAFF ROLL TEST | (`staffRollStart`) |  | opens and returns to the menu by itself on the next tick (`staffRollStart` returned non-zero); no stage change |
| 13 | ADPCM TEST | the 105 `adpcmFile` streams |  | opens, SELECT leaves (sound output not captured) |
| 14 | SE TEST | the `seDef` sounds at the boy |  | opens, SELECT leaves (sound output not captured) |
| 15 | REVERB TEST | (UP/DOWN change the depth) |  | opens, SELECT leaves |
| 16 | Game Over | only closes the menu (the retail body) |  | opens and returns by itself on the next tick |
| 17 | Ending Demo | only closes the menu (the retail body) |  | opens and returns by itself on the next tick |
| 18 | BackStage Test | (`backStageProcessInStage` with 10,000,000 s away) |  | opens and returns to the menu by itself on the next tick; no stage change |
| 19 | LoadINFO | per-stage load sizes, which `fumi/ios/cdvd.c` counts in every build |  | opens, SELECT leaves |
| 20 | Chara Info | (`_ACTDebugPrint` of the chosen actor) |  | opens, SELECT leaves |
| 21 | Pad2 Control | (`CurrentTargetGObjSub`) |  | opens, SELECT leaves; pad 2 is not connected, so nothing can be steered |
| 22 | DispBox | (`DebugDispBox` at the boy) |  | opens, SELECT leaves |
| 23 | DispBall | (a wire sphere and `scpTriggerPosBall`) |  | opens, SELECT leaves |
| 24 | Collision Test | (a ray moved with the right stick, `ClipCollision`) |  | opens, SELECT leaves |
| 25 | Hint Start | (the first hint object) |  | opens and closes the menu at once (stay flag 1); no crash |
| 26 | Tsuresari Time Zero | (`backStageDebugTimeZero`) |  | opens and closes the menu at once (stay flag 1); no crash |

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

## The entries opened on the host

Every entry except Debug Mode and Snap Shot (tested above) was opened in
the headless `linux-x64` build, from a boot of stage 11 (`start_stage=11`,
`[gameplay] developer_mode = true` in a `config.toml` beside the executable,
the corpus plain run's other settings) with a pad script: at Main tick 150
SELECT (0100) opens the menu with the cursor on row 0; DOWN (4000) for rows
0 to 13, or UP (1000) from row 0 (it wraps to row 26) for the rest, one
press every 5 ticks; CIRCLE (0020) chooses; 60 ticks later SELECT, CROSS
(0040), CROSS leave the entry and close the menu whatever state it reached
(SELECT leaves a tool; the menu list takes SELECT or CROSS as back). The
menu has no text in the headless build, so the observation was a temporary,
uncommitted probe in `debug_Menu` that printed `menuState` and `menuSelect`
to the log on every change; the table's "opens" is that state 2 was reached
on the row and "leaves" that the state went back. Six runs were made:
the first, without the probe, showed that the log records nothing of the
menu; the next five, with it, opened 25 entries (the camera editor's stuck
state, below, cost a rerun of four entries behind it). Every run ended
`exit: ticks= reached`, exit code 0, with no `CRASH`, `WATCHDOG` or
assertion line, and the trace's stage column changed only for Stage Select
(11 to 12). The log shows nothing else for any entry (the tools' text goes to the
debug font, not the log), and no entry wrote a file under `dev/`, the card
folder or `screenshots/` in these runs; the editors' files need edits the
scripts did not make. The sound entries' output was not captured.

A scripted pad is port 0 only. Entries that read `pad[1]` (the camera
editor, Pad2 Control) therefore cannot be steered on the host.

### Camera editor capacity

Not measured: the editor cannot be driven without pad 2 (above), and it
logs no count. By the code (`omori/src/camera-editor.c`,
`_CameraEdit_add_box`) a box needs a 9,200-byte block from
`ios_partition_root` (`iosMallocDebugNoAssert`; 9,280 bytes with the host's
0x50-byte block header, `fumi/ios/ios.c`), a set holds at most 100 boxes,
and the add reports "not added" when the heap is out of room. The root's own
space after the partitions carved from it is
25,755,632 - 25,032,706 = 722,926 bytes (the EE sizes, before the host's
extra, which goes to the four partitions), which is at most 77 boxes before
the root's other allocations (thread stacks created in the root, such as the
0x18000 InitIcoMisc stack), so the heap, not the 100-box limit, is the
bound on the host as on the EE.
