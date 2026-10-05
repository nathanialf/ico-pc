# Runtime text, layout extension, popups and the Settings menu (Phase 6, 6B and 6C)

The port's own text: a typeface embedded in the program, rasterised at run
time and drawn through `rd` inside the game's layout system (the Settings
menu, package 6C, "Settings menu" below; the player's view is
docs/port/SETTINGS.md) and as notification popups (achievements, package
6E).

| file | what |
| --- | --- |
| `port/ui/font.c`, `font.h` | the font, the per-size glyph atlases, `ui_MeasureText`, `ui_DrawText` |
| `port/ui/layout_ext.c`, `layout_ext.h` | port rows past the ends of `texLayout` / `texProperty`, the draw hook |
| `port/ui/strings.c`, `strings.h`, `strings_{en,fr,de,it,es}.c` | `ui_Str(id)` in the game's five languages |
| `port/ui/popup.c`, `popup.h` | the popup queue, timing and drawing |
| `port/ui/ui_host.c`, `ui_host.h` | the window build's glue: the game's globals, the decoder flush, the per-vsync step |
| `port/ui/settings.c`, `settings.h` | the Settings menu (6C): its port layouts, the entry rows and repoints, the screens' procs |
| `port/ui/menu_text.c`, `menu_text.h` | the game's menu text rows drawn with the port font (P3, "Menu text" below) |
| `port/ui/embed_font.cmake` | the font file as a C array at build time |
| `port/ui/test/ui_test.c` | the test (below) |
| `port/ui/test/settings_test.c` | `settings_test` and, built with `SETTINGS_RENDER`, `settings_render` (below) |
| `port/ui/test/menu_text_test.c` | `menu_text_test` (P3, "Menu text") |
| `port/assets/fonts/` | `Arimo-Regular.ttf`, `OFL.txt` (Arimo's) |
| `port/third_party/stb/` | `stb_truetype.h` v1.26, `LICENSE` |

## The font

**Arimo Regular** (SIL OFL 1.1) since 6C. 6B shipped EB Garamond Regular,
chosen from three OFL serifs (Cormorant Garamond, EB Garamond, Crimson Pro)
before the game's own lettering had been looked at closely. 6B's run then
showed that the PAL menus' lettering (the vibration screen's rows,
`texProperty` 43-45, and the Options rows, texFile 21 `menu_PAL_0x`) is a
plain neo-grotesque sans (Helvetica/Arial-like), light grey with a soft dark
rim baked around the letters, so a serif contrasted with it. The decision
(orchestrator, 6C brief) was to swap in an OFL sans with Arial's metrics,
subset the same way, and keep the halo. Arimo is metrically compatible with
Arial, has every letter the five languages need (Latin-1, Latin
Extended-A: Œ œ Ÿ, ß, ñ, the grave and acute vowels), GPOS pair kerning,
and its licence names no Reserved Font Name, so the subset keeps its name.

In the 6C run's title frame (below, "Runs") the game's "New Game" and the
port's "Settings" row sit one above the other: the same family look, the
same light-on-dark-rim treatment, the port's row dimmed by display_texture
as an unselected row is.

### The font file

`Arimo-Regular.ttf` is derived from `google/fonts`
`ofl/arimo/Arimo[wght].ttf` (the variable font, "Version 1.341"; last
commit to the file `d7b3b07542b00e0ec7c48886d305eb8f08ef89d5`, 2026-04-27;
SHA-256 of the download
`e43898b143ec826ac8cb4034816458a7047fbe0836558de2a1f8c6223ae3e0ca`;
`OFL.txt` from the same folder,
`11cce536cd2f3864d767003af5dcd739e2e15818cf2279b6175edeadd3960992`) with
fontTools 4.63.0, as 6B derived EB Garamond:

```python
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset
f = instancer.instantiateVariableFont(TTFont("Arimo[wght].ttf"), {"wght": 400})
f.save("inst.ttf")
opts = subset.Options()
opts.layout_features = ["kern"]
opts.hinting = False
opts.desubroutinize = True
opts.name_IDs = ["*"]
opts.notdef_outline = True
s = subset.Subsetter(opts)
s.populate(unicodes=subset.parse_unicodes(
    "U+0020-007E,U+00A0-017F,U+0192,U+02C6,U+02DC,U+2013-2014,U+2018-201E,"
    "U+2020-2022,U+2026,U+2030,U+2039-203A,U+20AC,U+2122"))
f = TTFont("inst.ttf")
s.subset(f)
f.save("sub.ttf")
f = TTFont("sub.ttf")
for l in f["GPOS"].table.LookupList.Lookup:   # stb_truetype reads lookup type 2 only
    if l.LookupType == 9:
        l.SubTable = [st.ExtSubTable for st in l.SubTable]
        l.LookupType = l.SubTable[0].LookupType
        l.SubTableCount = len(l.SubTable)
f.save("Arimo-Regular.ttf")
```

The result is 24,884 bytes (SHA-256
`cc758a39948352ce4ee89e8f5fecf161d1c21744b314a0189bd280f9ca1dfca1`), 360
glyphs, 2048 units per em, capital height 1409 (0.688 em), one GPOS pair
lookup (format 1, 105 pairs; AV -152, To -227 units). Arimo's GPOS has no
extension lookups, so the last loop changes nothing; it is kept so the
recipe is the same for any face. `port/ui/CMakeLists.txt` turns the file
into `ui_font_data.c` (`const unsigned char ui_font_ttf[]`) in the build
directory at build time (`embed_font.cmake`, a `cmake -P` script, no extra
tools); nothing generated is tracked, and the program needs no file beside
it. The licence text ships in `port/assets/fonts/OFL.txt` and must go into
the release's third-party notices (THIRD_PARTY.md: its EB Garamond row is
to be replaced by Arimo's, open items).

## Coordinates and metrics

**The grid.** `layout_texture.c` places rows on a 640-pixel by 226-field-line
screen centred on (320, 113): `box.x = (dispX - 320) * 16`,
`box.y = (dispY - 113) * 16`, in 1/16 units, and `gif_SpriteSensitiveOffset`
maps 640 pixels to `ScreenWidth` and 224 field lines to `ScreenHeight`. The
text API uses that grid with half field lines vertically, so both axes count
"pixels" of a 640 x 448 frame shown at 4:3:

- x 0..640 left to right (`dispX`), y 0..452 top to bottom (`2 * dispY`),
  centre (320, 226);
- sizes (the em) are in y units; an x unit is 14/15 of a y unit on screen,
  so glyph quads are placed 15/14 wider (`UI_X_PER_Y`) and keep the
  typeface's proportions;
- to the GS: `x = center_X * 16 + (gx - 320) * 16 * ScreenWidth / 640`,
  `y = center_Y * 16 + (gy - 226) * 8 * ScreenHeight / 224` (12.4).

**Atlases.** One set per rasterised pixel size, built on demand: the pixel
size is `round(size * scale)`, the scale 1 in the Original preset (an atlas
pixel per y unit, about 1.14 GS lines) and output height / 448 in Enhanced
(`ui_ScaleFor`; `ui_host.c` sets it from `rd_GetSettings()` before each
draw). Pages are 512 x 512 with a one-texel gutter, shelf-packed, up to 4 per
size and 16 sizes (beyond that the nearest size stands in, logged once).
Glyphs are cached per (size, code point); kerning comes from GPOS pair
adjustment (`stbtt_GetGlyphKernAdvance`; at 40 px: AV -5.6, To -4.2 px).

**Menu size (`UI_MENU_TEXT_SIZE` = 27).** 6B measured the game's menu
capitals on its title frame (dump 600 through the presenter at 960 x 720):
29 to 31 output pixels ("A" of "Activate" 29, "V" of "Vibration" 31), about
18.7 y units or 9.3 field lines in a 20-field-line row. Arimo's capitals
are 0.688 em, so the em is 27 (capitals 18.6 y units; EB Garamond's 0.65
em had needed 29). Checked on the 6C run's title frame (dump 525 at 960 x
720, both rows in the same picture): the stem of the game's "N" of "New
Game" is 30 output pixels tall (rows 542 to 571), the port's "S" of
"Settings" 27 pixels (597 to 623, overshoot included) at that row's size
24, so 27 gives about 30. Row boxes default to the Options screen's 20
field lines (`dispH` 40) and a 400-pixel width.

**The halo.** The game's menu textures carry a dark soft rim around the
letters, which keeps them legible over the bright fogged title. Port rows draw
the same kind of rim (`UI_HALO`: eight copies 1.5 y units out, black at a
quarter of the row's alpha, under the label).

## Drawing

`ui_DrawText(x, y, size, rgba, utf8, flags)` lays out UTF-8 (`\n` starts a
new line), aligns left/centre/right and top/middle-of-capitals/baseline, and
records one `rd_ScreenPrims(RD_PRIM_SPRITES, ..., RD_SPACE_UI, uvFixed 1)` per
atlas page into the current list: TEX0 the atlas page, MODULATE with TCC
RGBA, linear filtering, clamp, ABE on, ALPHA 0x44 ((Cs - Cd) As + Cd), Z test
ALWAYS, no Z write. The colour is a GS colour (0x80 = 1.0): the atlas texels
are white with the coverage as GS alpha (255 -> 0x80), so a label takes the
same vertex colour as a textured layout sprite. `UI_ADDITIVE` gives ALPHA
0x48 (the layout's glow); `UI_KEEP_STATE` records only the texture, the
sampler, ABE and the sprites (the layout hook, whose packet holds the game's
state); `ui_DrawTextXf` maps the quads through an affine `UiXform` (the glow's
stretch); `ui_DrawRect` is an untextured sprite (the popup panel).

Before each recording, the game build runs `gif_HostFlush` (the record hook
`ui_host.c` installs): the register decoder emits what it still batches into
the current list and forgets the state it emitted, so its next primitive
re-sends PRIM and TEX0 after the text's own `rd_Texture` (the pattern
`DisplayFont.c`'s host path uses). Built headless (no `ICO_RD`), the draw
calls measure and record nothing.

### Requested rd API

`port/render/*.c` was not to be touched (R5c in flight), so two things are
done with what `rd.h` offers and should move to a proper entry point:

1. **R8 font textures and `font.hlsl`.** `rd.h` has no R8 texture and no way
   to select `font_vs`/`font_ps` for screen prims, so each atlas page is
   uploaded as RGBA8 (4x the memory; a whole-page `rd_UpdateTexture` when
   glyphs are added) and drawn by `sprite_ps`. Wanted: `rd_CreateTextureR8(w,
   h, coverage, name)` plus a texture flag or `rd_ScreenPrims` variant that
   draws with `font_ps` (coverage times vertex alpha, the blend in force),
   and a sub-rectangle update. `font.hlsl` needed no fix for this package.
2. **A post-present overlay.** The presenter draws DISPLAY to the output
   inside `rd_EndFrame` (`rd_replay.c` `rd__ReplayFrame`: `rd__PresentRecord`
   then `rhi_Present` in one call), so nothing outside `port/render` can draw
   after the scale and mirror. Wanted: `rd_OverlayPrims(...)` recording
   UI-font sprites in output pixels into a per-frame overlay list that
   `rd__PresentRecord` draws after the scale blit and before the transition
   to PRESENT (and into the headless output, so `rd_ReadDisplay` and the
   replay tool see it), never into DISPLAY, never mirrored; or a callback
   `rd_SetPresentOverlay(fn, user)` called there with the command list and
   the output size. `popup.c` computes its panel in grid units and would only
   change its draw calls.

## Layout extension

`texLayout[80]` and `texProperty[436]` are fixed-size arrays the loader fills
from the disc (`port/data/gen/table_defs.c`). Checked in the loaded data
(the PS2 build's generated `texture-layout.c` / `tex-property.c` from the
same ELF):

- `texLayout` rows 66..79 are empty (`{434, 434, ...}`, no proc), as the plan
  says, but every stage's layout range covers them (`stageData`
  `layoutFirst..layoutLast` is 1..80 or 6..80), and both
  `layout_texture.c` (`lt_init_stage_textures`) and `kanban.c`
  (`init_textures_of_property_range`) look up the texture file of every
  property row of every layout in that range at each stage load: a port row
  there would be looked up as a texture (`texFile[texFileNo]`, then
  `__assert` when `tex_GetTextureNo` fails).
- `texProperty` has no free row: 0..433 belong to the game's layouts, 434
  and 435 are the subtitle rows `jimaku.c` writes.

So both tables are extended past their ends (`port/ui/layout_ext.h`):
layouts 80..111 and properties 436..691, held in port arrays. No stage range
reaches them, so no texture is ever looked up for a port row.

**API.** `lt_ext_AddLayout(&LtProp)` and `lt_ext_AddProperty(&LtProperty,
&LtExtText{strId | text, size, align})` append and return the index;
`lt_ext_SetText` / `lt_ext_SetStr` change a label at run time (values,
language); `lt_ext_Layout(i)` / `lt_ext_Prop(i)` are the lookups;
`lt_ext_IsPortProp(e)` is the "isPortRow" test; `lt_ext_Reset()`. A row with
neither `dispW` nor `texW` gets `dispW` 400, one with neither `dispH` nor
`texH` gets `dispH` 40. Since 6C a label wider than its row's box is drawn
smaller to fit, down to 60 % of its size (the widest line counts), so a long
option or a longer language does not run out of its column. Link fields (`up`/`down`/`left`/`right`, the item
links, `link`) may name game or port indices freely.

**Fall-through sites** (`ico2/common/src/layout_texture.c`, under
`ICO_HOST`; the file stays ASCII): `LT_LAYOUT(i)` / `LT_PROP(i)` are
`(*lt_ext_Layout(i))` / `(*lt_ext_Prop(i))` on the host and the plain array
accesses otherwise, used at every index in
`display_texture_fade_cancel_chk`, `lt_draw_layout`, `default_item_select`,
`lt_reset_property_chain`, `texture_fading`, `display_texture` (the
selected-row test), `display_primary_texture_layout`,
`exec_layout_texture`, `lt_init_stage_textures`' last line,
`init_layout_texture`'s current-layout reset, `lt_link_layout`,
`lt_mask_property`, `lt_default_mask_property` (38 lines). The texture
initialisation (`lt_texture_no_of_property`,
`init_textures_of_specified_property`, the `D_0030D014` column alias and
the stage loop) keeps the plain arrays: it only ever walks stage ranges.
Out-of-range indices behave as the plain access did (`curItem == -1` takes
`&texProperty[-1]` as before, never dereferenced).

`ico2/common/src/layout_action.c` needs no change: its only table accesses
are `texLayout[14].curItem` (`:979`), `texLayout[17].defaultItem`
(`:1110`, `:1112`), `texLayout[18].defaultItem` (`:1648`, `:1650`) and
`texLayout[58].defaultItem` (`:2663`, `:2812`, `:3004`), constant game
indices. `kanban.c` (its own layouts' rows) and `jimaku.c` (rows 434, 435)
never see a port index either.

**The draw hook** (`display_texture`, under `ICO_HOST`). For a port row
(`lt_ext_IsPortProp(e)`), `tex_TransTexture` is skipped and
`lt_ext_DrawRow(e, box, colour, 0)` replaces the textured
`gif_SpriteSensitiveOffset`; everything around it is the game's code: the
packet state (Z test off, Z write off, ALPHA 0x44, TEX1 0x60), the colour
(`ltCursorColor`'s fade alpha or the highlight, `~reductionCol` with its
-16 step, the halving of an unselected selectable row), the cursor sparkle
(the points drawn around a selected unselectable row) and the glow:
`lt_glow_sprite` calls `lt_ext_DrawRow(row, stretched box, glow colour, 1)`
after its own `gif_SetAlpha(1, 5, 0)` (ALPHA 0x48), which maps the label
through the same stretch the sprite would get. The label is vertically
centred on the capitals in the row's box and aligned left at `dispX` (or
centred / right, `LtExtText.align`); `centerX` centres the box as for a
texture row.

**The chained-row selection** (6C, `display_texture`, under `ICO_HOST`). A
port row drawn from a layout that has no cursor of its own (`curItem` < 0)
takes the selection and the dimming from the current layout's cursor
(`LT_CUR_NO`: `current_layout_id` in place of the drawn layout in the
`sel` test and the `ownerItem` test). The Settings entry rows are such rows:
they sit in a one-row port layout chained after the Options or title
layout, and the current layout's cursor moves onto them through the item
links. Game rows and port rows of a layout with a cursor are unchanged.

## Settings menu (6C)

`port/ui/settings.c` builds the menu once the game's tables are loaded:
`init_layout_texture` calls `ui_SettingsInstall()` (under `ICO_HOST`),
which builds the port layouts the first time and repoints the game's rows
each time (idempotent, and only when the loaded tables look like the PAL
ones: layout 58 is rows 297..333, 325's up item is 324, 12 starts at 49,
13 at 51; otherwise nothing is touched and one line is logged).

**Entry.** A game layout's rows are one contiguous range, so the "Settings"
row cannot join the Options layout (58, rows 297..332) or the title's (12:
49, 50; 13: 51). It is a port row in a one-row port layout on the game
layout's `link` chain, drawn with it, and the game's item links are
repointed at it (UI.md open item 3 of 6B: the `link` chain, with the item
links crossing between game and port rows):

| from | repointed | the row |
| --- | --- | --- |
| Options 58 | `325.downItem` and `300.upItem` (both were 300 / 325, the wrap) -> row; `58.link` -> row layout (was -1) | up 325, down 300, `right` the Settings layout, `left` 57 (Triangle back to the pause menu, as every Options row); y 165, in place of 325, which `lt_property_visible` hides until the game is cleared, and 185 once it shows (the entry proc moves it); right-aligned ending at x 364 like the Options labels |
| Title 12 "Continue / New Game" | `50.downItem` -> row (was -1); `12.link` -> row layout -> 11 (was 11) | up 50; centred at y 181 (size 24), between "New Game" (165) and the copyright line (row 48 at 195, letters from about 205) |
| Title 13 "New Game" | `51.downItem` -> row (was -1); `13.link` -> row layout -> 11 (was 11) | up 51; the same place |

`lt_property_visible`'s skip (it follows `downItem` / `upItem` past the
hidden 300 and 325 before the game is cleared) gives the order 308, ...,
324, Settings, 308 before and 300, ..., 325, Settings, 300 after. The title
procs (`la_title_continue_or_new`, `la_title_new_game_only`) mask the
title's row with their own while the card check runs
(`ui_SettingsTitleMask`) and do not start or continue a game on Cross or
START while the cursor is on it (`LA_HOST_NOT_SETTINGS_ROW`): Cross there
is `default_item_select`'s, which follows the row's `right` link into the
menu with the game's sound and glow. Leaving the menu puts the cursor back
on the row: for 58 by setting `texLayout[58].defaultItem` as
`la_key_config` and `la_adjust_screen` do for theirs; for the title by
setting its `defaultItem` for the switch and restoring the game's value on
the next frame.

**Screens.** Each is a port layout (backdrop black at 0.6 as Options, fade
in 0.3 s, out 0.1 s) with one proc (`settingsProc`), header row, label rows
right-aligned ending at x 344 and value rows from x 364:

| screen | rows |
| --- | --- |
| Settings | Display, Audio, Controls, Gameplay (open their screens), Language (value), Achievements (opens the list), Developer mode (value), Back; notes under Language and Developer mode |
| Display | Preset, Resolution, Aspect ratio, Fullscreen, Vertical sync, Texture filtering, Full-height picture, Frame rate (read-only, only when `[video] framerate` is in the config), Video mode, Menu text (P3), Back; since P3 eleven rows, 17 field lines apart from line 36 |
| Audio | Volume, Back; a note that the output does not apply it yet |
| Controls | Remap controls (opens the remap screen), Analogue stick fix, Mouse sensitivity, Back |
| Gameplay | Shadows never take Yorda (+ OPTIONS.md's explanation as a note), Mirror mode "Chosen at New Game" (not selectable), Back |
| Achievements | a scrolling list of 8 slots over the 30 entries and Back: title (hidden and locked: "???") and state (Unlocked, Assisted, Locked); the selected one's description below; the header counts the unlocked |
| Remap controls | a scrolling list of 8 slots over the 24 targets, "Reset to defaults" and Back: the PS2 name, the keyboard and mouse sources, the gamepad sources; a hint line or "Press a key or button…" |

A stepped value sits centred between two arrow rows at fixed x (362 and
556), as the Options values sit between rows 309 and 310; the value and
arrow rows have the label as `ownerItem`, so the game's dimming lights them
with their label. Notes are rows masked by default and unmasked by the proc
while their label is selected; their text is wrapped to 580 x units by
measuring (`ui_MeasureText`), redone when the language changes. Every port
row gets a texel V of its own so the layout's fade-cancel pairing
(`display_texture_fade_cancel_chk`) never matches two port rows.

**Buttons**, as the game's menus have them (`la_game_option`,
`default_item_select`): up and down move the cursor on the item links
(wrapping like the Options rows); left and right (`0x8000`, `0x2000`, the
stick through `lt_analog2Pad`) step the selected value, with `CUR_SE`;
Cross on a section follows its `right` link (`POSITIVE` sound, the glow);
Cross on Back and Triangle anywhere go back with `NEGATIVE_SE`, after
`la_host_leave()` (layout_action.c: `lt_set_item_select_func(0)`,
`actionStarted = 0`, what every game proc does before it returns a layout).
In the lists the slots' first and last item links stop at the ends and the
proc scrolls (wrapping at the ends of the list).

**Values and setters.** `ui_SettingsStep`: the display options through
`ico_video_get` / `ico_video_set` (the window applies them at its next
pump); Video mode toggles `systemStatus[0]` and calls `gsResetFunc(0)` as
kanbanBoot.c step 201 does, and sets `[video] video_mode`; Language steps
`NonLinearCameraMove` 2..6 as step 102 stores it, sets the port strings'
language at once (`ui_SetLanguage`) and `[game] language`
(`ico_sysconf_set_language`); stick fix, Shadows never take Yorda and
developer mode through `ico_opt_set_*` and their `[gameplay]` keys; mouse
sensitivity in the live binding table (`ico_input_live_bindings`); volume
as `[audio] volume`. Leaving any screen saves what changed
(`ui_SettingsSave`): `ico_input_write_bindings` for the bindings, then
`ico_video_save` (which writes the whole file) or `ico_config_save`, then
the live bindings are reloaded from what was written. The card system
file's `cameraMove` and `palMode` are written by `product_write`
(`fumi/ios/mcard.c`) from `NonLinearCameraMove` and `systemStatus[0]` at
the game's next system save, as before.

**Remap capture** (`UiRemapCapture`): Cross on a target row records
`ico_input_last_press`'s sequence number; each Main tick the proc steps the
capture, which binds the next press (`ico_bindings_assign`: that device's
row for the target becomes the source alone, and the source leaves the
device's other targets) or gives up after 250 ticks (10 s at 25 ticks a
second). While it waits, and for 3 ticks after, the proc sets
`lt_item_select_disable` so the press neither moves nor confirms.

## Menu text (P3)

The game's own menus draw every word as a sprite cut from a pre-rendered
sheet. Since P3 those rows are drawn with Arimo instead, at the place, size,
colour and fade the sprite had, so the menus read like the Settings rows;
the logo, the copyright line, the backgrounds, the button glyphs and the
other artwork stay the original textures. On by default; `[game]
classic_menu_text = true` (Settings > Display, "Menu text: Port font /
Classic"; CONFIG.md) restores the textures at run time.

| file | what |
| --- | --- |
| `port/ui/menu_text.c`, `menu_text.h` | the table (texel rectangle, string, metrics per text rectangle; texProperty row to rectangle), `ui_MenuTextItemOf`, `ui_MenuTextDraw`, the classic switch |
| `port/ui/layout_ext.c` | `lt_ext_IsTextRow` (a port row or a table row), `lt_ext_DrawTextRow` (the dispatch to `lt_ext_DrawRow` or `ui_MenuTextDraw`) |
| `ico2/common/src/layout_texture.c` | `display_texture` and `lt_glow_sprite` (ICO_HOST): a text row's sprite is replaced by `lt_ext_DrawTextRow`; `tex_TransTexture` still runs for every game row |
| `ico2/common/src/kanban.c` | `display_texture` (ICO_HOST): the same for the boot screens' signs (the card prompts, Yes / No, the language and 50 / 60 Hz screens); ASCII-only patch of the EUC-JP file |
| `port/game/options.c`, `.h` | `ico_opt_classic_menu_text` (`[game] classic_menu_text`, default false) |
| `port/ui/test/menu_text_test.c` | the test (ctest `menu_text`, below) |

**Where the text comes from.** The sheets are `text/menu_PAL_{EG,FR,GR,IT,SP}/menu_PAL_01..04.tm2` and `scei.tm2` (one set per language, packed in `STGTTL.DF` and `STGLOG.DF`), `text/title.tm2` (one for all languages, in `COMMON.DF`) and `text/buttons.tm2`. A `texProperty` row names its sheet through `texFile` (the base name only, so the loaded language's sheet is used) and its texel rectangle (`texU`, `texV`, `texW`, `texH`); the five language sheets share the row geometry. For P3 the sheets were decoded from the user's disc (raw deflate packs, 4-bit TIM2 with a 16-colour CLUT) into PNGs in a scratch folder outside the repository, cut per rectangle for all five languages, and every word transcribed from them by eye: the wording, capitalisation, punctuation and line breaks are the sheets' (so "Continue" is "Charger" / "Laden" / "Carica" / "Cargar" on the title, the Spanish preview's "Vagoneta__1" keeps its double underscore, the English preview names row 142 "Trolley 2" and row 173 "Trolley 1" while the other four languages number them the other way, Italian's Dark / Light are the symbols "–" and "+"). The strings are `UI_STR_MT_*` in `strings_{en,fr,de,it,es}.c` (79 strings). Nothing of the disc is in the repository: the table holds rectangles, sizes and positions measured on the sheets, and the transcribed words.

**The table (`menu_text.c`).** 87 text rectangles, drawn by 124 `texProperty` rows (several rows draw one rectangle: Yes / No on six prompts, Back on the save screens). Per rectangle: the string, the alignment, the ink (light letters with the dark rim, or black letters on the white panel), the em, the anchor, the line pitch and, per language, the first line's capital middle. They were measured on the sheets: each line's capital top and baseline (the em is the capital height less 0.7 texel of antialiasing over Arimo's 0.688; a 20-texel menu row gives 13.5 texels, 27 y units, the `UI_MENU_TEXT_SIZE` 6B measured), the ink's left, centre and right edges across the five languages (the edge that stays put is the alignment: the Options labels end at one x, the pause items start at one x, the title and prompts are centred), and the line pitch (15 texels on the panel prompts). Sizes per row class: 27 y units for the 20-texel menu rows (title, vibration, pause, Options, key config, adjust screen, Yes / No, OK, Back, the save screens' Resume / End Game); 30 for the boot screens' language names and 50 / 60 Hz (title.tm2's heavier capitals); 33 for "Save?" and "Continue ?"; 24 for the white-panel prompts, the card check prompts, "Loading" / "Saving" / "Formatting" and the save headers; 27 for the save preview's location names; 21 for "Accessing"; 18 for "MEMORY CARD slot 1 / 2".

| screen (layouts) | rows |
| --- | --- |
| boot signs, kanban.c (0 language, 1 TV, 3 / 4 card check, 5 confirm) | 12: the five language names, 50 Hz, 60 Hz, "No Memory Card (PS2) inserted", the two 360 KB prompts, Yes, No |
| title (12, 13) and vibration (9) | 6: Continue, New Game (twice), Vibration, Activate, Deactivate |
| memory card, save and load (14 to 47) | 69: the slot list's header and "MEMORY CARD slot 1 / 2", the preview's 16 location names, "Accessing", "Do not remove…", OK, Back, the load / save / format prompts and their failures, "Save?", "Loading", "Saving", "Formatting", "File saved.", the overwrite and format confirmations, Yes / No, Resume Game / End Game |
| pause (56, 57) | 3: Options, Back, End Game |
| Options (58) | 13: the header, Film Effect, Sound, Stereo, Mono, Vibration, Activate, Deactivate, Hold Type, Button Configuration, Brightness, Players, Back |
| key config (59) | 9: the header, Jump, Attack, Action, Release, Hold hand / Call, Zoom, OK, Default |
| adjust screen (60) | 6: Brightness, the hint, Dark, Light, OK, Default |
| end confirm (61) and game over (62) | 6: "The game will end. Is this okay?", Yes, No, "Continue ?", Yes, No |

Left as textures, with the reason (the full list is the comment at the top of `menu_text.c`): the ICO logo (31, 37); the LANGUAGE and TV headers (25, 32: lettering inside the swash artwork); the 50 / 60 Hz notes (35, 36: text inside speech-bubble artwork); "Sony Computer Entertainment Europe Presents" (46) and the copyright line (48): the corporate lettering of a credit and a legal notice; the slot numbers and the preview's digits (52..71, 74..136: digit tiles placed one glyph at a time); the Options value tiles (the film effect's 0..4, the hold type's A / B, 1 / 2 players) and arrows; R1 / R2 / L1 / L2 on the key config screen (outlined button labels, like the button glyphs); `buttons.tm2` (Cross, Triangle and the rest); the panels, bars, backdrops, the brightness markers and ruler, 1 x 1 placeholders, the preview location rows whose rectangle is blank on every sheet, a stray bubble corner (433) and the subtitle rows (434, 435).

**The hook.** `display_texture` (layout_texture.c) sets `ltHostTextRow` for `lt_ext_IsTextRow(e)`: a port row as before, or a game row for which `ui_MenuTextItemOf(e)` finds an item (not in classic mode; the row's index is in the table; its texel rectangle is the one the table was measured on, so tables that are not the PAL ones keep their textures). For a game row `tex_TransTexture` runs as before, so the VRAM and packet bookkeeping is the texture path's; only `gif_SpriteSensitiveOffset` is replaced by `lt_ext_DrawTextRow(e, box, ofs, colour, 0)`, and `lt_glow_sprite` passes its stretched box the same way. Everything around it is the game's: the packet state, the colour (`ltCursorColor`'s fade, the highlight, `~reductionCol` and its -16 step, the halving of an unselected selectable row), the cursor sparkle and the glow. `kanban.c`'s `display_texture` does the same with its own box and inset. `ui_MenuTextDraw` maps the item's texel coordinates through the sprite's box and texel rectangle (so either caller's half-texel inset is honoured): x and y grid units per texel from the box and `uv`; each line at the item's anchor and capital middle, `UI_VALIGN_MIDDLE`, aligned left, centred or right; the size the em times the vertical scale, rounded to whole y units (one atlas per size). Arimo is about 1.3 times wider than the sheets' lettering at the same capital height, so a line longer than the room its anchor leaves in the rectangle is set smaller to fit, down to 60 %, as the Settings rows are. Light rows draw with `UI_HALO` (the rim) in the sprite's colour; dark rows draw black (the colour's RGB zeroed, its alpha kept) without a rim and skip the additive glow (black adds nothing there). The draws are keyed by the row and the pass for the presenter's blending (R7d), as the port rows are. The language is `ui_GetLanguage()`, which follows `NonLinearCameraMove`; a language change in Settings shows in the game's menus at once (the textures followed only at their next load).

**Classic.** `[game] classic_menu_text` is read by `ico_opt_classic_menu_text`; `ui_SettingsInstall` hands it to `ui_MenuTextSetClassic` before the first layout draws, and the Display row steps it, sets the key and the switch, and is saved when the screen is left.

**Test (`menu_text_test`, ctest `menu_text`, exit 77 without a Vulkan device after the CPU checks).** Every table row is a `texProperty` index, in order, its item's rectangle non-empty and on a sheet, every item used, its anchor and lines inside its rectangle in every language; with the disc image (`ICO_DISC_IMAGE`; skipped without it) every table row's rectangle equals the boot ELF's `texProperty` row (read at `0x0030CFF8` from `SCES_507.60`) and its `texFile` is a text sheet (0..5, 10..29); every string id exists in each of the five tables (no English fallback) and is drawable; through the real `layout_texture.c` on the recording (rows shaped like the PAL title's): New Game and Continue drawn as atlas text (eight halo copies and the letters each, 18 batches), the copyright line as one texture sprite, all three textures transferred; classic mode: three texture sprites, no text, three transfers; a row whose rectangle differs from the table's keeps its texture; no undecoded register write. On lavapipe: the title's New Game row through `exec_layout_texture` into SCENE: 2114 pixels painted (421 near white) inside the row's rectangle plus the rim's margin, 0 outside; `menu_text_scene.png` beside the test.

Results (2026-10-05, P3): the window build on Linux (gcc, lavapipe, validation layer) 66 of 66 tests passed, `menu_text` included; `settings_test` checks the Display page's 11 rows and the Menu text value (Port font, Classic, the key written, back); `win-x64` builds `ico_pc.exe`, `menu_text_test.exe`, `ui_test.exe`, `settings_test.exe`, `settings_render.exe`.

**Run (P3).** The window build on lavapipe, `SDL_VIDEODRIVER=offscreen`, `use_iso=1`, `ticks=650`, `dump_every=50`, `pad_script = port/input/pad-boot.txt`, `timeout 300`, a private pref folder: exit 0, no process left. Dumps 200, 450, 500, 550 and 600 replayed with `rd_replay_tool --present 960x720`, no command skipped: 200 is "Sony Computer Entertainment Europe Presents" (texture, as intended), 450 to 550 the title with the copyright line (texture) and the logo untouched, 550 the port's Settings row, 600 the mirror mode screen. No dumped frame holds a table row: New Game is masked at 500 and 550 (the title proc masks it until the card check ends) and the vibration screen falls between dumps 550 and 600 (this script reaches stage 41 40 ticks earlier than 6B's run, whose frame 600 showed it). The run allowance was one run, so the Arimo menu rows over the game are seen only in the test's SCENE and in `settings_render`'s screens; open item 8.

## Strings

`ui_Str(id)` returns the current language's UTF-8 string;
`ui_StrIn(lang, id)` a given one; a missing entry falls back to English, an
unknown id is "". `ui_host.c` sets the language from the game's own choice,
`NonLinearCameraMove` (2 English, 3 French, 4 German, 5 Italian, 6 Spanish:
the boot screen's choice in `kanbanBoot.c` steps 101-102, the numbering
`texFile`'s language column uses), English before it is set. The ids
(`strings.h`): the menu title and the five sections, Back, On/Off, the
display options (preset Original/Enhanced, fullscreen, resolution, aspect,
vsync, interpolation, filtering, full-height picture, mirror), the controls
options (remap, mouse camera, invert X/Y, vibration), the gameplay options of
docs/port/OPTIONS.md (stick fix; "Shadows never take Yorda" and the
explanation OPTIONS.md asks for), the five language names, skip boot
screens, developer mode and its note, the test popup and "Achievement
unlocked"; since 6C the Settings menu's (Audio, Achievements, volume and its
note, mouse sensitivity, video mode, PAL 50 Hz, 60 Hz, frame rate, Window,
Auto, Trilinear, Anisotropic, the mirror line, the language note, the remap
screen's columns, hint, prompt and reset, the PS2 button and direction
names, Locked, Unlocked); since 6E the achievements' 30 titles and 30 descriptions
(`UI_STR_ACH_<NAME>`, `UI_STR_ACH_<NAME>_DESC`) and the popup's "Assisted"
line (`UI_STR_ACH_ASSISTED`), listed in docs/port/ACHIEVEMENTS.md. The translations are the author's, not reviewed by native
speakers (open item 4).

## Popups

`ui_PopupPush(title, body)` queues (8 deep, 127 bytes each) a popup;
`ui_PopupVsync()` steps the clock: 15 vsyncs sliding in from the right edge
of the 4:3 picture (smoothstep, the alpha following), 200 held, 15 sliding
out (0.3 s, 4 s, 0.3 s at PAL's 50 Hz), then the next. The panel is anchored
18 x units from the right edge and 30 y units from the top, as wide as the
longer of title (26 y units) and body (21) plus 14 units each side, at most
the picture's width less the margins: a dark translucent panel (GS
6, 6, 9, alpha 0x5C), a 1.5-unit hairline in the menu's warm grey on top,
the title in warm white and the body in a lighter grey.

**Where it is drawn now.** Lacking the overlay entry point (Requested rd API,
item 2), `ui_PopupRecord` draws into list 12 of the open frame, once per game
frame (`ui_host.c` compares `frame_count`, then flushes the decoder into the
list it was recording, selects list 12, draws, writes back list 12's
defaults, TEST 0x50000 and Z write on, and restores the list). That puts the
popup after the game's UI and fade (list 11) and before the reduction, so:

- it is drawn at the scene's resolution and reduced with the frame (half
  height, the stage tint), not at output resolution;
- it reaches DISPLAY: a keep frame (pause) draws DISPLAY back, so the popup
  of the last full frame shows under the live one while paused;
- UI space: when mirror mode lands it is pre-flipped like the rest of the
  UI and reads correctly after the flip.

All three go away with the overlay.

**Hooks.** `port/platform/window_host.c` calls `ui_HostInit()` after
`rd_Init` (the font, the hooks, the popup test switch), `ui_HostVsync(
ico_host_main_ticks())` at the end of `ico_window_pump` once per vsync, after
the simulation step, and `ui_HostShutdown()` before `rd_Shutdown`. The
developer key `[dev] popup_test = true` (ini `popup_test=1`, CONFIG.md;
`host_config.c` exports `ICO_UI_POPUP_TEST`) queues the test popup ("Test
popup", "Runtime text: Éléphant, Größe, señor, città, cœur") at Main tick 100
and every 150 ticks after, so any run's frame dumps catch one. Since the
popup is part of the rd frame, the `dump_every` dumps and `rd_replay_tool`
show it; no separate readback path was needed.

## Tests

`ui_test` (`port/ui/CMakeLists.txt`; exit 77 without a Vulkan device after
the CPU checks):

- the font parses; "H" at 40 px is 27 to 29 px tall on the baseline (28 with Arimo since 6C; EB Garamond was checked at 25 to 28) with solid
  stems and a clear gutter; cached; kerning AV and To negative;
- UTF-8: 57 accented letters and symbols of the five languages decode to the
  right code points and all have glyphs; a truncated sequence, an overlong
  "/", a surrogate, a code point above U+10FFFF and a lone continuation byte
  each give U+FFFD and consume one byte;
- `ui_MeasureText` equals the advances plus kerning times 15/14; AV is
  kerned; twice the size is twice the width; the widest of two lines; the
  same width at scale 2; `ui_ScaleFor`;
- every string id in every language is non-empty, valid UTF-8 and drawable;
  the language numbers;
- the layout extension with the real `layout_texture.c` (and `GifPacket.c`,
  `DisplayList.c`, `DmaPacket.c` as the window build has them, the rest of
  the game stubbed, fake tables): the fall-through returns the game's rows
  below the counts and the port's above; a port layout of two rows run by
  `exec_layout_texture` makes no texture transfer and records, in list 11,
  the backdrop and per row eight halo copies and the label, as UI sprites
  with texel UVs from the atlas page, MODULATE/RGBA, ALPHA 0x44, Z write off,
  Z 0xFFFFFF9B, the selected row's colour 0x7F and the unselected one's 0x3F
  at alpha 127, starting at the row's `dispX`; a pad "down" moves the cursor
  and the next frame draws the label once more with ALPHA 0x48 (the glow);
  unselectable rows get the cursor sparkle; no undecoded register write;
- the popup queue: the test trigger at tick 100 and again 150 ticks later,
  the slide (off screen, in, held, out), FIFO order, the bound;
- on the device: "ICO" and "Éléphant" drawn into SCENE: every painted pixel
  inside the bounds the measure and line metrics give (none outside),
  1589 and 3381 pixels with EB Garamond, 1783 and 4634 with Arimo (6C), the
  acute of the É above the capitals; in the stems
  of a 150-unit "I" the colour (100, 50, 25) over (20, 40, 60) is exactly the
  MODULATE result at alpha 0x80 (199, 99, 49) and within one step of the GS
  formula `((Cs - Cd) * As >> 7) + Cd` at alpha 0x40 (worst difference 1 over
  106 pixels: the hardware blender's rounding of the dual-source LERP, as in
  RENDER_API.md section 7). It writes `ui_test_scene.png` beside itself.

Results (2026-10-05, 6B): the window build on Linux (gcc, lavapipe,
validation layer) 51 of 51 tests passed with `ui`; the headless Linux build
of HEAD with this package 49 of 49 (two disc tests skipped: no disc in that
tree); `win-x64` builds `ico_pc.exe` and `ui_test.exe`.

`settings_test` (6C, ctest `settings`, CPU): the menu built over fake game
tables shaped like the PAL ones and run by the real `layout_texture.c`
(measure-only text, the gif calls stubbed):

- the rows of each screen, in order, with their labels (Settings 8, Display
  9, or 10 with a `[video] framerate` key, the Frame rate row then reading
  the config's value; Audio 2, Controls 4, Gameplay 3, the two lists 8
  slots); sections open their screens; the rows wrap; each screen its own
  layout over a contiguous range of port rows;
- the repoint: 325 and 300 lead to the Options row and it back to them, its
  Triangle (57) and Cross (the menu) links, the link chains 58 -> row
  layout and 12, 13 -> row layout -> 11, the title rows 50 and 51; a second
  install changes nothing; tables that are not the PAL ones are left alone;
- through `exec_layout_texture`: in 58 before the game is cleared, down from
  324 lands on the Settings row (y 165), down again on 308, up from 308 on
  the row; Cross opens the menu; down to Language and right gives French
  (`NonLinearCameraMove` 3, the port's language, the value text
  "Français", the labels in French, `[game] language = "fr"`); left back
  to English; Triangle returns to 58 with the cursor on the row; cleared,
  the row moves to y 185; the menu opens on its first row again; Controls,
  Remap: row 1 "Cross" / "Space, Mouse L"; Cross starts a capture, a K press
  binds Cross to K and takes K off the right stick's down; the list scrolls
  past the eighth row; Triangle writes `input.kb.cross = "K"` and
  `input.kb.rstick_down = "none"` and leaves the untouched rows out; the
  reload reads K back;
- every value through `ui_SettingsStep`: preset, resolution (Window, 2x,
  wrap to 4x), aspect (16:9, Auto), filter (wrap to anisotropic), the three
  toggles, video mode (60 Hz with one `gsResetFunc`, `video_mode = "60hz"`,
  back to 50 Hz), volume 90 %, stick fix, Shadows never take Yorda,
  developer mode, mouse sensitivity 1.25, language wrap Spanish -> English;
  `ui_SettingsSave` writes `[video]`, `[gameplay]`, `[audio] volume`,
  `[input] mouse_sensitivity`, `[game] language` and `[video] video_mode`;
- the capture state machine: waiting, a gamepad button bound (and taken off
  the target it was on, the keyboard row kept), the cooldown, idle after; a
  press made before the capture started is not taken; the timeout leaves
  the binding; a mouse button; clear; the row text "Tab, Backquote" reads
  back through `ico_bindings_set`;
- the boot skip's mapping: libscf 0..5 to the game's 2, 2, 3, 6, 4, 5 and
  back; `language = "de"` and `video_mode = "60hz"` give 4 and 0 and win
  over a card's values; "it" / "pal50"; "auto" follows the host's locale
  (es_ES gives 6, ja_JP English) and keeps the card's values; an invalid
  `video_mode` is ignored; `ico_sysconf_set_language`.

`settings_render` (ctest `settings_render`, exit 77 without a Vulkan
device): the same file with `SETTINGS_RENDER`, the menu run through rd with
`GifPacket.c`, `DisplayList.c` and `DmaPacket.c` as the window build has
them; each screen's SCENE (512 x 512, written at 4:3) as
`settings_<screen>.png` beside the test; no undecoded register write.

Results (2026-10-05, 6C): the headless Linux build 58 of 58 and the window
build on Linux (lavapipe) 58 of 58 with `settings`, `settings_render` and
`ui` (the "H" check now 27 to 29 px for Arimo: 28); `win-x64` builds
`ico_pc.exe`, `settings_test.exe` and `ui_test.exe`.

## Runs

### 6B (2026-10-05)

The window build on lavapipe, `SDL_VIDEODRIVER=offscreen`, `pad-boot.txt`,
`use_iso=1`, `ticks=700`, `popup_test=1`, `dump_every=50`, `timeout 300`:
exit 0 at 700 Main ticks (stage 1, the title, until tick 623, then 41).
Dumps 150, 550 and 600 replayed with `rd_replay_tool --present 960x720`, no
command skipped. Frame 600 shows the title (the castle, the logo, the
vibration screen's three rows) with the test popup at the top right: the
panel from x 274 to 933 (622 grid units, the 18-unit margin) and y 47 to 177
of 960 x 720, the title's "T" 28 output pixels tall (the menu's capitals 29
to 31), the body's accents (É, ö,
ß, ñ, à, œ) clean and legible, the panel's darkening enough for white text
over the bright fog. Soft vertically: the reduction halves the scene and the
presenter doubles the lines (Popups, "Where it is drawn now"). Two things
seen there were changed afterwards and are not in that frame: the body ran
to the panel's right edge (the panel's width cap was 440 units; now the
picture's width less the margins) and the menu size, set before the
measurement, went from 30 to 29; the halo was added after it.

### 6C (2026-10-05)

**Headless** (`build-host/6c-run-headless`, `linux-x64`, `pad-boot.txt`,
`ticks=3000`, `timeout 600`): exit 0 at 3000 Main ticks, 6007 vsyncs, stage
3. The card check runs as before (steps 0 to 4, 100); step 101 now goes to
190 in one tick (`scf: language 1 from the system locale`, English, 2), and
step 200 to 202 in one; the boot ends at tick 192 (`mcCheckStep -1`) and
`kanbanBootEnd` at 358. Every stage switch is exactly 40 ticks earlier than
in 6E's run of the same script (`build-host/6e-run`): 1 -> 41 at 583 (was
623), 42 at 676 (716), 43 at 756 (796), 45 at 812 (852), 40 at 876 (916),
**stage 3 at 956** (996).

**Window** (`build-host/6c-run-window`, the window build on lavapipe,
`SDL_VIDEODRIVER=offscreen`, `use_iso=1`, `ticks=1600`, `dump_every=25`,
`timeout 600`, a private pref folder; the pad script is `pad-boot.txt` up
to tick 957 and then START, UP, CROSS, UP, CROSS from tick 1100 to open
Settings from the pause menu, then Controls, Remap, back, Display, Preset
right): exit 0 at 1600 ticks, every process gone, the same stage ticks as
the headless run (stage 3 at 956). Replayed with `rd_replay_tool --present
960x720`:

- frame 525: the title with "New Game" and, below it, the port's "Settings"
  row in Arimo, dimmed as an unselected row, the halo like the game's rim,
  clear of the copyright line; the capitals' heights (Metrics);
- frames 1125 to 1375: stage 3 opens with a cutscene (the cinema bars) that
  lasts until about tick 1375, so the pause presses at 1100 to 1185 fell in
  it and did nothing; from 1400 the boy stands in the courtyard. The
  Settings, Controls, remap and Display screens were therefore not reached
  in the game; the run allowance (two runs) was used. They were looked at
  through `settings_render` instead (below).

`settings_render`'s PNGs (the real layout code and rd on lavapipe, over a
flat colour; no game textures): the Settings screen with Language selected
(its value between the arrows, the note at the bottom), Display (nine rows,
the values centred between fixed arrows), Audio with its note, Controls,
Gameplay (the long Yorda label set smaller to fit, the mirror line dimmed,
the two-line note), Achievements (0 / 30, eight rows, the description of the
first), the remap screen (Cross "Space, Mouse L" / "South", the face
buttons, shoulders, the hint line) and a capture in progress ("…" on the
row, "Press a key or button…").

## Open items

1. **Entering Settings in a real run.** The 6C window run did not reach
   the in-game Settings screens (stage 3's opening cutscene, above). A run
   with the pause presses after tick 1400 (or START, UP, CROSS, UP, CROSS at
   the title, where the row is drawn) shows them over the game.
2. **Framerate.** Shown read-only from `[video] framerate`; R7b's
   `IcoVideoOptions.framerate` (original, uncapped, a cap) was in flight in
   the tree, so the row does not step it yet.
3. **Volume** (done in 7A): the SDL output applies `[audio] volume`, live
   (docs/port/AUDIO.md, "Output").
4. **THIRD_PARTY.md** (done in 7A): the font row is Arimo's.
5. **Translations** need a native-speaker review; the French, German,
   Italian and Spanish strings are the author's (6B's and 6C's).
6. **The rd entry points** (Requested rd API): the R8 atlas through
   `font.hlsl`, and the post-present overlay for popups.
7. Gamepad names on the remap screen are SDL's positions in English
   (South, LShoulder, LX-), not translated.
8. **Menu text in a real run (P3).** The P3 run's dumps did not catch a
   table row (Run, above). A run with `dump_every` small enough to land on
   the vibration screen (about Main tick 560 to 590 with `pad-boot.txt`),
   or one that opens the pause menu and Options, shows them over the game;
   the classic path needs no run (the unit test covers it).
9. **Settings row at the title during the card check.** In the P3 run the
   port's Settings row is drawn at frames 500 and 550 while New Game is
   still masked by `la_title_new_game_only`; 6C's `ui_SettingsTitleMask`
   was meant to hide it with the title's row. Not P3's change (the
   Settings row is a port row); to look at.

6B's open items 1 (the typeface: Arimo, above), 3 (entering the menu:
the link chain and repointed item links), 6 (values: `lt_ext_SetText` from
the procs) and 7 (long labels: shrink to fit) are done; 2 (`.gitignore`)
was settled outside 6B (`!port/assets/`).
