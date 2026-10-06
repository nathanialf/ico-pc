# Runtime text, the layout extension, popups and the Settings menu

The port draws its own text: a typeface embedded in the program,
rasterised at run time and drawn through the renderer (`rd`) inside the
game's own layout system. That text serves the Settings menu (the player's
view is docs/port/SETTINGS.md), the achievement popups, and, by default,
the words of the game's own menus.

| file | what |
| --- | --- |
| `port/ui/font.c`, `font.h` | the font, the per-size glyph atlases, `ui_MeasureText`, `ui_DrawText` |
| `port/ui/layout_ext.c`, `layout_ext.h` | port rows past the ends of `texLayout` / `texProperty`, the draw hook |
| `port/ui/strings.c`, `strings.h`, `strings_{en,fr,de,it,es}.c` | `ui_Str(id)` in the game's five languages |
| `port/ui/popup.c`, `popup.h` | the popup queue, timing and drawing |
| `port/ui/ui_host.c`, `ui_host.h` | the window build's glue: the game's globals, the decoder flush, the per-vsync step |
| `port/ui/settings.c`, `settings.h` | the Settings menu: its port layouts, the entry rows and repoints, the screens' procs, the title layout, the quit and mirror-mode screens |
| `port/ui/ui_list.c`, `ui_list.h` | the scrolling list pages ("Lists" below): the slots, the refresh from the page's items, headings the cursor skips, the scrolling; the achievements and remap pages use it |
| `port/ui/menu_text.c`, `menu_text.h` | the game's menu text drawn with the port font ("Menu text" below) |
| `port/ui/embed_font.cmake` | turns the font file into a C array at build time |
| `port/ui/test/ui_test.c`, `settings_test.c`, `menu_text_test.c` | the tests (below) |
| `port/assets/fonts/` | `Arimo-Regular.ttf`, `OFL.txt` (Arimo's licence) |
| `port/third_party/stb/` | `stb_truetype.h` v1.26, `LICENSE` |

## The font

The font is **Arimo Regular** (SIL OFL 1.1). The PAL menus' lettering (the
vibration screen's rows and the Options rows, texFile 21 `menu_PAL_0x`) is
a plain neo-grotesque sans, Helvetica or Arial-like, light grey with a soft
dark rim baked around the letters, so the port uses an OFL sans with
Arial's metrics and draws the same kind of rim. Arimo is metrically
compatible with Arial, has every letter the five languages need (Latin-1,
Latin Extended-A: Œ œ Ÿ, ß, ñ, the grave and acute vowels), has GPOS pair
kerning, and its licence names no Reserved Font Name, so the subset keeps
its name. On the title, the game's "New Game" and the port's "Settings"
read as the same family with the same light-on-dark-rim treatment.

### The font file

`Arimo-Regular.ttf` is derived from `google/fonts`
`ofl/arimo/Arimo[wght].ttf` (the variable font, "Version 1.341"; last
commit to the file `d7b3b07542b00e0ec7c48886d305eb8f08ef89d5`, 2026-04-27;
SHA-256 of the download
`e43898b143ec826ac8cb4034816458a7047fbe0836558de2a1f8c6223ae3e0ca`;
`OFL.txt` from the same folder,
`11cce536cd2f3864d767003af5dcd739e2e15818cf2279b6175edeadd3960992`) with
fontTools 4.63.0:

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
recipe works for any face. `port/ui/CMakeLists.txt` turns the file into
`ui_font_data.c` (`const unsigned char ui_font_ttf[]`) in the build
directory at build time (`embed_font.cmake`, a `cmake -P` script, no extra
tools). Nothing generated is tracked, and the program needs no file beside
it. The licence text ships as `port/assets/fonts/OFL.txt` and is listed in
docs/port/THIRD_PARTY.md.

## Coordinates and metrics

**The grid.** `layout_texture.c` places rows on a 640-pixel by
226-field-line screen centred on (320, 113): `box.x = (dispX - 320) * 16`,
`box.y = (dispY - 113) * 16`, in 1/16 units, and
`gif_SpriteSensitiveOffset` maps 640 pixels to `ScreenWidth` and 224 field
lines to `ScreenHeight`. The text API uses that grid with half field lines
vertically, so both axes count "pixels" of a 640 x 448 frame shown at 4:3:

- x 0..640 left to right (`dispX`), y 0..452 top to bottom (`2 * dispY`),
  centre (320, 226);
- sizes (the em) are in y units; an x unit is 14/15 of a y unit on screen,
  so glyph quads are placed 15/14 wider (`UI_X_PER_Y`) and keep the
  typeface's proportions;
- to the GS: `x = center_X * 16 + (gx - 320) * 16 * ScreenWidth / 640`,
  `y = center_Y * 16 + (gy - 226) * 8 * ScreenHeight / 224` (12.4).

**Atlases.** There is one set per rasterised pixel size, built on demand:
the pixel size is `round(size * scale)`, where the scale is 1 in the
Original preset (an atlas pixel per y unit, about 1.14 GS lines) and the
output height / 448 in Enhanced (`ui_ScaleFor`; `ui_host.c` sets it from
`rd_GetSettings()` before each draw), so text is rasterised at the
output's density. Pages are shelf-packed, up to 4 per size and 16 sizes
(beyond that the nearest size stands in, logged once). A page is 512
texels wide, or 1024 or 2048 for pixel sizes above 85 or 170 (an Enhanced
4K output sets the menu's 27 units at about 130 px), and 8 texels less
high, so its height is never a power of two. That is deliberate: the
Enhanced texture filter gives power-of-two textures a box-filtered mip
chain with its alpha scaled up (RENDER_API.md, the display options
section), which would merge neighbouring glyphs and thicken the letters
wherever the atlas is minified. Cells are two texels apart and from the
page's edges; bilinear sampling inside a glyph quad reaches at most one
texel past the cell, which is zero coverage. Glyphs are cached per (size,
code point); kerning comes from GPOS pair adjustment
(`stbtt_GetGlyphKernAdvance`; at 40 px: AV -5.6, To -4.2 px).

**The atlas on rd** (package R8). `font.c` keeps each page's coverage
(0..255, `ui_FontPage`) and gives rd a one-byte-a-texel copy in GS alpha
units, `(c × 128 + 127) / 255` (`rd_CreateTextureR8`, RENDER_API.md
"Textures"): a page is created whole the first time it is drawn, in the
frame or on the overlay, and from then on a glyph rasterised into it is
uploaded as its cell alone (`rd_UpdateTextureRect`), so a new glyph costs
its own bytes, not a page. Screen prims and overlay prims with an R8
texture are drawn by `font_ps`, whose output is the bytes the RGBA8
atlas (white, the same alpha) gave through `sprite_ps`: the Original
preset's popups and menu text are unchanged, and a page takes a quarter
of the memory it took as RGBA8.

**Menu size (`UI_MENU_TEXT_SIZE` = 27).** The game's menu capitals on the
title measure 29 to 31 output pixels at 960 x 720, about 18.7 y units or
9.3 field lines in a 20-field-line row. Arimo's capitals are 0.688 em, so
the em is 27 (capitals 18.6 y units). Row boxes default to the Options
screen's 20 field lines (`dispH` 40) and a 400-pixel width.

**The halo.** The game's menu textures carry a dark soft rim around the
letters, which keeps them legible over the bright fogged title. Port rows
draw the same kind of rim (`UI_HALO`: eight copies 1.5 y units out, black
at a quarter of the row's alpha, under the label). The rim's width is in y
units, so it scales with the output like the game's baked rim.

## Drawing

`ui_DrawText(x, y, size, rgba, utf8, flags)` lays out UTF-8 (`\n` starts a
new line), aligns left, centre or right and top, middle-of-capitals or
baseline, and records one `rd_ScreenPrims(RD_PRIM_SPRITES, ...,
RD_SPACE_UI, RD_UV_FIXED_CONTINUOUS)` per atlas page into the current list:
TEX0 the atlas page, MODULATE with TCC RGBA, linear filtering, clamp, ABE
on, ALPHA 0x44 ((Cs - Cd) As + Cd), Z test ALWAYS, no Z write. The colour
is a GS colour (0x80 = 1.0); the atlas page is an R8 texture of the
coverage in GS alpha units (255 to 0x80), which `font_ps` reads as a white
texel with that alpha, so a label takes the same vertex colour as a
textured layout sprite. `UI_ADDITIVE` gives ALPHA 0x48 (the layout's
glow); `UI_KEEP_STATE` records only the texture, the sampler, ABE and the
sprites (for the layout hook, whose packet holds the game's state);
`ui_DrawTextXf` maps the quads through an affine `UiXform` (the glow's
stretch); `ui_DrawRect` is an untextured sprite (the popup panel).

`RD_UV_FIXED_CONTINUOUS` (`port/render/rd.h`) exempts the glyph quads from
the GS-pixel snapping `rd_replay.c` applies to sprites on a scaled target.
That snapping is right for the game's nearest-sampled sprites, but a glyph
quad has sub-pixel edges: snapped, each glyph loses up to 15/16 of a GS
pixel at its top and left and gains as much at its bottom and right, the
halo's eight copies are cut differently, and at Enhanced 4x the rim shows
straight edges and the letters step. Text is port content, not PS2
content, so it is drawn continuous; at scale 1 the snap does nothing, so
the Original preset is unaffected.

**Overlay mode** (package OV). Between `ui_BeginOverlay(ctx)` and
`ui_EndOverlay()`, called inside a presentation overlay callback
(RENDER_API.md, "The presentation overlay"), `ui_DrawText`, `ui_DrawTextXf`
and `ui_DrawRect` draw on the output through `rd_OverlayPrims` instead of
recording into the current list. The grid maps onto the 4:3 picture inside
the box the presenter drew DISPLAY into (the box itself in 4:3, its centred
4:3 part when the box is wider, where the game's UI is): x' = left + gx × W
/ 640, y' = box.y + (gy − 2) × box.h / 448, with W = min(box.w, box.h ×
4/3) and left = box.x + (box.w − W) / 2. That is where the list-11 path puts
a grid point after the reduction and the box blit: 226 is the frame's
centre line, so its 448 lines are grid y 2 to 450. The scale is
`ctx->boxScale` (box.h / 448) until `ui_EndOverlay`, so text is rasterised
at round(size × box.h / 448) pixels (a 26-unit title is 63 px at 1080
lines, 125 px at 2160), one atlas texel to one output pixel; each glyph
quad has its top-left corner rounded to a whole pixel and keeps the
bitmap's size, and rects have both corners rounded, so the glyphs are drawn
texel for pixel and sampled at their texel centres. The blend is
`ui_DrawText`'s (0x44, `UI_ADDITIVE` 0x48); there are no draw keys, the
state flags are ignored and nothing is mirrored. `ui_SetScale` meanwhile
sets the scale restored afterwards. Measuring in overlay mode uses the
overlay's scale, so a panel fits its text. The deferred menu text ("Menu
text", package DEF) is drawn through the same mode, from the renderer
`ui_InstallDeferredText` registers with `rd`.

Before each recording the game build runs `gif_HostFlush` (the record hook
`ui_host.c` installs): the register decoder emits what it still batches
into the current list and forgets the state it emitted, so its next
primitive re-sends PRIM and TEX0 after the text's own `rd_Texture` (the
pattern `DisplayFont.c`'s host path uses). Built headless (no `ICO_RD`),
the draw calls measure and record nothing.

## Layout extension

`texLayout[80]` and `texProperty[436]` are fixed-size arrays the loader
fills from the disc (`port/data/gen/table_defs.c`). Neither has room for
port rows:

- `texLayout` rows 66..79 are empty (`{434, 434, ...}`, no proc), but every
  stage's layout range covers them (`stageData` `layoutFirst..layoutLast`
  is 1..80 or 6..80), and both `layout_texture.c`
  (`lt_init_stage_textures`) and `kanban.c`
  (`init_textures_of_property_range`) look up the texture file of every
  property row of every layout in that range at each stage load, so a port
  row there would be looked up as a texture (`texFile[texFileNo]`, then
  `__assert` when `tex_GetTextureNo` fails).
- `texProperty` has no free row: 0..433 belong to the game's layouts, and
  434 and 435 are the subtitle rows `jimaku.c` writes.

So both tables are extended past their ends (`port/ui/layout_ext.h`):
layouts 80..111 and properties 436..691, held in port arrays. No stage
range reaches them, so no texture is ever looked up for a port row.

**API.** `lt_ext_AddLayout(&LtProp)` and `lt_ext_AddProperty(&LtProperty,
&LtExtText{strId | text, size, align})` append and return the index;
`lt_ext_SetText` and `lt_ext_SetStr` change a label at run time (values,
language); `lt_ext_Layout(i)` and `lt_ext_Prop(i)` are the lookups;
`lt_ext_IsPortProp(e)` tests for a port row; `lt_ext_Reset()` clears. A row
with neither `dispW` nor `texW` gets `dispW` 400, and one with neither
`dispH` nor `texH` gets `dispH` 40. A label wider than its row's box is
drawn smaller to fit, down to 60 % of its size (the widest line counts),
so a long option or a longer language stays in its column. Link fields
(`up`/`down`/`left`/`right`, the item links, `link`) may name game or port
indices freely.

**Fall-through sites** (`ico2/common/src/layout_texture.c`; the file stays
ASCII). `LT_LAYOUT(i)` and `LT_PROP(i)` are `(*lt_ext_Layout(i))` and
`(*lt_ext_Prop(i))` in place of the plain array accesses, used at every
index in
`display_texture_fade_cancel_chk`, `lt_draw_layout`,
`default_item_select`, `lt_reset_property_chain`, `texture_fading`,
`display_texture` (the selected-row test),
`display_primary_texture_layout`, `exec_layout_texture`,
`lt_init_stage_textures`' last line, `init_layout_texture`'s
current-layout reset, `lt_link_layout`, `lt_mask_property` and
`lt_default_mask_property`. The texture initialisation
(`lt_texture_no_of_property`, `init_textures_of_specified_property`, the
`D_0030D014` column alias and the stage loop) keeps the plain arrays,
because it only ever walks stage ranges. Out-of-range indices behave as
the plain access did (`curItem == -1` takes `&texProperty[-1]`, never
dereferenced). `layout_action.c` only touches constant game indices,
`kanban.c` its own layouts' rows and `jimaku.c` rows 434 and 435, so none
of them ever sees a port index.

**The draw hook** (`display_texture`). For a port row,
`tex_TransTexture` is skipped and `lt_ext_DrawRow(e, box, colour, 0)`
replaces the textured `gif_SpriteSensitiveOffset`. Everything around it is
the game's code: the packet state (Z test off, Z write off, ALPHA 0x44,
TEX1 0x60), the colour (`ltCursorColor`'s fade alpha or the highlight,
`~reductionCol` with its -16 step, the halving of an unselected selectable
row), the cursor sparkle (the points drawn around a selected unselectable
row) and the glow: `lt_glow_sprite` calls `lt_ext_DrawRow(row, stretched
box, glow colour, 1)` after its own `gif_SetAlpha(1, 5, 0)` (ALPHA 0x48),
which maps the label through the same stretch the sprite would get. The
middle of the label's capitals sits 2 y units (a field line) above the
centre of the row's box, where the game's 20-texel menu rows have theirs
(texel 9.0 of 20; English New Game is 9.5), so a port row on the game's
grid lines up with the game's rows. The label is aligned left at `dispX`
(or centred or right, `LtExtText.align`); `centerX` centres the box as
for a texture row.

**Chained-row selection** (`display_texture`). A port
row drawn from a layout with no cursor of its own (`curItem` < 0) takes
its selection and dimming from the current layout's cursor (`LT_CUR_NO`:
`current_layout_id` in place of the drawn layout in the `sel` and
`ownerItem` tests). The Settings entry rows are such rows: they sit in a
one-row port layout chained after the Options or title layout, and the
current layout's cursor moves onto them through the item links. Game rows
and port rows of a layout with a cursor are unchanged.

## Settings menu

`port/ui/settings.c` builds the menu once the game's tables are loaded:
`init_layout_texture` calls `ui_SettingsInstall()`,
which builds the port layouts the first time and repoints the game's rows
each time. Installing is idempotent and happens only when the loaded
tables look like the PAL ones (layout 58 is rows 297..333, 325's up item is
324, 12 starts at 49, 13 at 51); otherwise nothing is touched and one line
is logged.

**Entry.** A game layout's rows are one contiguous range, so the
"Settings" row cannot join the Options layout (58, rows 297..332) or the
title's (12: rows 49, 50; 13: row 51). It is a port row in a one-row port
layout on the game layout's `link` chain, drawn with it, and the game's
item links are repointed at it:

| from | repointed | the row |
| --- | --- | --- |
| Options 58 | `325.downItem` and `300.upItem` (both were 300 / 325, the wrap) to the row; `58.link` to the row layout (was -1) | up 325, down 300, `right` the Settings layout, `left` 57 (Triangle back to the pause menu, as every Options row). y 165 (325's) in place of 325, which `lt_property_visible` hides until the game is cleared, and one Options pitch (20 field lines) below it, 185, once it shows (the entry proc sets it from the loaded rows). Right-aligned ending at x 357 (`OPTIONS_LABELS_END`), where the Options labels' letters end |
| Title 12 "Continue / New Game" | `50.downItem` to the row (was -1); `12.link` to the row layout, which links on to 11 (was 11) | up 50, down the "Quit to desktop" row; centred, at the game rows' size (27) and box height in a 400-pixel box, on the title's pitch below New Game; masked by default as 49 to 51 are |
| Title 13 "New Game" | `51.downItem` to the row (was -1); `13.link` to the row layout, then 11 (was 11) | up 51; the same place |

`lt_property_visible`'s skip (it follows `downItem` and `upItem` past the
hidden 300 and 325 before the game is cleared) gives the Options order
308, ..., 324, Settings, 308 before the game is cleared and 300, ..., 325,
Settings, 300 after. The title procs (`la_title_continue_or_new`,
`la_title_new_game_only`) mask the title's port rows with their own while
the card check runs (`ui_SettingsTitleMask`) and do not start or continue
a game on Cross or START while the cursor is on one
(`LA_HOST_NOT_SETTINGS_ROW`, `ui_SettingsEntryItem`); Cross there is
`default_item_select`'s, which follows the row's `right` link into the
menu with the game's sound and glow. Leaving the menu puts the cursor back
on the row: for 58 by setting `texLayout[58].defaultItem`, as
`la_key_config` and `la_adjust_screen` do for theirs; for the title by
setting its `defaultItem` for the switch and restoring the game's value on
the next frame.

**Why the title is re-spaced.** The PAL title has Continue (49) at
`dispY` 135, New Game (50, and 51 on the New-Game-only layout) at 165 and
the copyright line (48) at 195, one 30-line step below New Game, which
leaves no room for two more rows: squeezed between New Game and the
copyright line, the port rows' capitals end up 3 to 12 output pixels apart
at 960 x 720 and touch. The copyright line cannot simply move down either:
its sprite (rim and descenders included) leaves the picture more than 5
field lines below 195. So the port lays the whole title out on one pitch
(`placeTitle`, called at each install so the game's textures and the
classic menu text get the same places). In the loaded table (the disc is
not changed) Continue moves to 119, New Game (50 and 51) to 139, Settings
sits at 159, Quit at 179 and the copyright line at 198: one 20-field-line
pitch (the Options screen's), with the copyright 19 lines below Quit
because its capitals are smaller, which gives about the same space
between its capitals and Quit's as between the rows' (31 to 35 output
pixels at 960 x 720). The logo ends 384 pixels down at 960 x 720, so
119..198 leaves about 6 pixels at either end. The New-Game-only layout
keeps New Game at 139 with the slot above empty, and 51 keeps 50's y, so
the switch from 12 to 13 still pairs them in the fade-cancel check
(`display_texture_fade_cancel_chk` compares `dispY`). The install accepts
the title rows at the PAL places or at these (a second install, or tables
loaded again). Both port rows are masked by default (`defaultMask`, as 49
to 51), because `exec_layout_texture` resets every row of the chain to its
default mask each frame and the title's proc runs only in some fade
states; masked by default they show only when the proc shows New Game.

**Quit to desktop.** The Quit row's `right` link is the quit screen, a
port layout like the mirror screen: the header "Quit to desktop?" (y 112)
and Yes / No side by side (x 200 and 330, y 146; the game's own Yes / No
strings, `UI_STR_MT_YES` / `UI_STR_MT_NO`), the cursor on No. Its proc
(`quitScreenProc`): Cross on Yes plays `POSITIVE_SE`, writes what Settings
has pending (`ui_SettingsSave`) and calls the quit handler once; Cross on
No, Triangle or Circle play `NEGATIVE_SE`, call `la_host_leave()` and
return to the title layout with the cursor on the row. The handler
(`ui_SettingsSetQuitHandler`) is set by `ui_HostInit` in the window build:
it posts `SDL_EVENT_QUIT`, so the program ends through the same path as
closing the window (`main` returns and the atexit handlers
`ico_window_close`, `ico_ach_flush`, `ico_audio_host_shutdown` and the
summary run). Without one (headless) the request calls `exit(0)`, which
runs the same atexit handlers.

**Screens.** Each is a port layout (backdrop black at 0.6 as Options, fade
in 0.3 s, out 0.1 s) with one proc (`settingsProc`), a header row, label
rows right-aligned ending at x 344 and value rows from x 364:

| screen | rows |
| --- | --- |
| Settings | Display, Audio, Controls, Gameplay (open their screens), Language (value), Achievements (opens the list), Extras (opens its page; from the title only), Developer mode (value), Back; notes under Language and Developer mode. With the Extras row there are nine rows on a 17-line pitch from line 40 (the notes sit at 196); from the pause menu the row is masked (`defaultMask`), the eight left keep the 19-line pitch, and `layoutMain` relinks the rows' up and down items past it each tick, because `lt_property_visible` does not look at masks |
| Extras | Music, Models, Credits, Back; a note under Credits while it is locked |
| Music | the music gallery (docs/port/MUSIC.md): a scrolling list of 8 slots over the groups' headings (drawn at x 24, skipped by the cursor) and entries (x 40: the album's title or the file; column A at x 420, right-aligned: the in-game file), a status line at 184 (group, asset, Playing or Stopped) and a hint line at 204 |
| Display | Preset, Resolution, Aspect ratio, Fullscreen, Vertical sync, Texture filtering, Full-height picture, Frame rate, CRT filter (Off, Scanlines, Consumer TV, Trinitron, PVM: `[video] crt` and `crt_mode` in one row), CRT strength (0 to 100 % in tens), Video mode (changes only when Settings was opened from the title; from the pause menu its value reads "PAL 50 Hz (title only)" and Left and Right do nothing), Menu text, Back; 14 field lines apart from line 36 |
| Audio | Volume, Music volume, Effects volume, Sound output, Output device (the name cut with "…" where it would not fit the value box at the 60 % shrink), Back |
| Controls | Remap controls (opens the remap screen), Mouse sensitivity, Circle goes back (with a note), Back |
| Gameplay | Shadows never take Yorda (with OPTIONS.md's explanation as a note), Analogue stick fix, Back |
| Achievements | a scrolling list of 8 slots over the 30 entries and Back: title (hidden and locked: "???") and state (Unlocked, Locked); the selected one's description below; the header counts the unlocked |
| Remap controls | a scrolling list of 8 slots over the 24 targets, "Reset to defaults" and Back: the PS2 name, the keyboard and mouse sources, the gamepad sources; a hint line or "Press a key or button…" |

A stepped value sits centred between two arrow rows at fixed x (362 and
556), as the Options values sit between rows 309 and 310; the value and
arrow rows have the label as `ownerItem`, so the game's dimming lights
them with their label. Notes are rows masked by default and unmasked by
the proc while their label is selected; their text is wrapped to 580 x
units by measuring (`ui_MeasureText`), redone when the language changes.
Every port row gets a texel V of its own so the fade-cancel pairing never
matches two port rows. In the lists the slots' first and last item links
stop at the ends and the proc scrolls, wrapping at the ends of the list.

**Extras** (`UI_PAGE_EXTRAS`, docs/port/EXTRAS.md). Each entry is an
`UI_OPT_EXTRAS_*` row whose Cross calls a hook in `settings.c` (`extrasMusic`,
`extrasModels`, `extrasCredits`) that returns the layout to open, or -1 for
"not there yet" (logged as `extras: <entry> not available yet`;
`extrasModels` and `extrasCredits` are marked for packages MV and CRED).
`extrasMusic` opens the music gallery's page (`UI_PAGE_MUSIC`, a list page
over `kGalDef`; `gallery.h`, docs/port/MUSIC.md); leaving it puts the cursor
back on the Music row. The locked style is `rowLocked` (Credits while
`creditsUnlocked()` is false): `lt_ext_SetDim` greys the label and the value
row (the colour at half, whatever the cursor does; `lt_ext_RowDim` reads it
back), the value shows `UI_STR_ACH_LOCKED`, and the note
`UI_STR_EXTRAS_LOCKED_NOTE` unmasks while the cursor is on the row. `build()`
prints the layout extension's use under developer mode (`lt_ext_PropCount`
of `LT_EXT_MAX_PROPERTIES` 256, `lt_ext_LayoutCount` of 32); with Extras it is
154 properties and 13 layouts, and `settings_test` asserts both are under the
limits.

**Lists** (`ui_list.h`). A list page is a window of `UI_LIST_SLOTS` (8) slots
over N items, described by a `UiListDef`: `count`, `fill` (label, column A and
B of item k, as text or a string id), optional `heading` (shown, never
selected), optional `input` (a press on item k; `UI_LIST_PASS` lets the list
scroll) and optional `decorate` (the page's header and status line, given the
item under the cursor). `ui_ListBuild` adds the rows (label, columns and
item links per slot, then the status line), `ui_ListRefresh` fills them, and
`ui_ListProc` is one tick of input: first a cursor on a heading goes on in the
direction it came from (the layout moves the cursor after the proc, so this
happens on the next tick; the window scrolls to show the item), then
`input`, then the scrolling (at the first or last slot the window moves by one,
wrapping at the ends). The achievements and remap pages are `kAchDef` and
`kRemapDef` in `settings.c`; their snapshots did not change when the code moved
out of `settings.c` (`settings_render`). The music gallery (`kGalDef`) uses
`heading` for its group headings and `input` for Cross, Square and the
Left and Right group jumps.

**Buttons**, as the game's menus have them (`la_game_option`,
`default_item_select`): up and down move the cursor on the item links
(wrapping like the Options rows); left and right (`0x8000`, `0x2000`, the
stick through `lt_analog2Pad`) step the selected value, with `CUR_SE`;
Cross on a section follows its `right` link (`POSITIVE` sound, the glow);
Cross on Back and Triangle or Circle anywhere go back with `NEGATIVE_SE`,
after `la_host_leave()` (`lt_set_item_select_func(0)`,
`actionStarted = 0`, what every game proc does before it returns a
layout). Circle (0x20) is taken on every port screen whatever
`[game] circle_back` says (`PAD_BACK` in `settings.c`); a remap capture in
progress binds it instead, as it binds any press.

**Circle in the game's menus.** The PAL game backs out with Triangle in
two ways: `default_item_select` follows the selected row's `left` link on
0x10 (the Options rows 300 to 330 to 57, the Settings entry row to 57, the
memory card screens' Back rows to 21 or 28), and the `la_*` procs test
0x10 themselves (the table in DIVERGENCES.md, "Optional features"). In the
port both test `lt_ext_BackButtons()` instead (`LT_BACK_BUTTONS` in
`layout_texture.c`, `LA_BACK` in `layout_action.c`): 0x30 while
`[game] circle_back` is on (the default), 0x10 when it is off, which is
the PS2's test. Checked against the PAL tables (`texLayout` at 0x00533FE8
and `texProperty` at 0x0030CFF8 of the boot ELF, the procs named from
`port/data/gen/ee_symbols.c`), no layout proc and no `default_item_select`
path reads 0x20 except `la_key_config` (layout 59, where Circle is one of
the buttons it assigns and its rows have no `left` link), so that screen
is left alone, as is `la_adjust_screen` (60, where Triangle resets the
brightness to 7 rather than cancelling). The title (12, 13), the vibration
screen, the boot card prompt (5) and game over (62) have no Triangle
cancel. `la_save_confirm_yesno` and `PSH_POSITIVE_OR_NEGATIVE` test 0x10
but no layout uses them. `kanban.c`'s boot screens are not changed. The
movement guard (`flags & 0x50` before the item links) still tests
Triangle alone.

**Values and setters.** `ui_SettingsStep` sets the display options
through `ico_video_get` / `ico_video_set` (the window applies them at its
next pump). Video mode toggles `systemStatus[0]` and calls
`gsResetFunc(0)` as the boot's step 201 does, and sets
`[video] video_mode` (only from the title: `canStep` refuses the row when
`s_origin` is the pause layout, and `rawValue` appends
`UI_STR_VIDEO_MODE_TITLE_ONLY`). Language steps `NonLinearCameraMove` 2..6 as the
boot's step 102 stores it, sets the port strings' language at once
(`ui_SetLanguage`) and `[game] language` (`ico_sysconf_set_language`).
The stick fix, Shadows never take Yorda and developer mode go through
`ico_opt_set_*` and their `[gameplay]` keys; mouse sensitivity through the
live binding table (`ico_input_live_bindings`); volume, music and effects
as `[audio] volume`, `music` and `effects` (the gains live, through
`ico_audio_set_volume` and `ico_audio_set_gain`); Sound output through
`ico_opt_set_output_mode`, `[audio] output` and `soundOutputModeSet`;
Output device through `[audio] device` and `ico_audio_sdl_reopen`. Frame rate steps through Original, Uncapped, 60, 120,
144 and 240; a cap from the file that is not listed steps to the nearest
listed value in the direction pressed. Leaving any screen saves what
changed (`ui_SettingsSave`): `ico_input_write_bindings` for the bindings,
then `ico_video_save` (which writes the whole file) or `ico_config_save`,
then the live bindings are reloaded from what was written. The card system
file's `cameraMove` and `palMode` are written by `product_write`
(`fumi/ios/mcard.c`) from `NonLinearCameraMove` and `systemStatus[0]` at
the game's next system save.

**Remap capture** (`UiRemapCapture`): Cross on a target row records
`ico_input_last_press`'s sequence number; each Main tick the proc steps the
capture, which binds the next press (`ico_bindings_assign`) or gives up
after 250 ticks (10 s at 25 ticks a second). While it waits, and for 3
ticks after, the proc sets `lt_item_select_disable` so the press neither
moves nor confirms. docs/port/INPUT.md, "Remap screen", has the input
side.

## Menu text

The game's own menus draw every word as a sprite cut from a pre-rendered
sheet. By default the port draws those words with Arimo instead, at the
place, size, colour and fade the sprite had, so the game's menus and the
Settings menu read alike and change language at once; the logo, the
copyright line, the backgrounds, the button glyphs and the other artwork
stay the original textures. `[game] classic_menu_text = true` (Settings >
Display, "Menu text: Port font / Classic"; docs/port/CONFIG.md) restores
the textures at run time.

| file | what |
| --- | --- |
| `port/ui/menu_text.c`, `menu_text.h` | the table (texel rectangle, string, metrics per text rectangle; texProperty row to rectangle), `ui_MenuTextItemOf`, `ui_MenuTextDraw`, the classic switch |
| `port/ui/layout_ext.c` | `lt_ext_IsTextRow` (a port row or a table row), `lt_ext_DrawTextRow` (dispatch to `lt_ext_DrawRow` or `ui_MenuTextDraw`) |
| `ico2/common/src/layout_texture.c` | `display_texture` and `lt_glow_sprite`: a text row's sprite is replaced by `lt_ext_DrawTextRow`; `tex_TransTexture` still runs for every game row |
| `ico2/common/src/kanban.c` | `display_texture`: the same for the boot screens' signs (the card prompts, Yes / No, the language and 50 / 60 Hz screens); an ASCII-only patch of the EUC-JP file |
| `port/game/options.c`, `.h` | `ico_opt_classic_menu_text` (`[game] classic_menu_text`, default false) |

**Where the text comes from.** The sheets are
`text/menu_PAL_{EG,FR,GR,IT,SP}/menu_PAL_01..04.tm2` and `scei.tm2` (one
set per language, packed in `STGTTL.DF` and `STGLOG.DF`),
`text/title.tm2` (one for all languages, in `COMMON.DF`) and
`text/buttons.tm2`. A `texProperty` row names its sheet through `texFile`
(the base name only, so the loaded language's sheet is used) and its texel
rectangle (`texU`, `texV`, `texW`, `texH`); the five language sheets share
the row geometry. Every word was transcribed by eye from the sheets
decoded from a disc (kept outside the repository): the wording,
capitalisation, punctuation and line breaks are the sheets' (so
"Continue" is "Charger" / "Laden" / "Carica" / "Cargar" on the title, the
Spanish preview's "Vagoneta__1" keeps its double underscore, the English
preview names row 142 "Trolley 2" and row 173 "Trolley 1" while the other
four languages number them the other way, and Italian's Dark / Light are
the symbols "–" and "+"). The strings are `UI_STR_MT_*` in
`strings_{en,fr,de,it,es}.c` (79 strings). Nothing of the disc is in the
repository: the table holds rectangles, sizes and positions measured on
the sheets, and the transcribed words.

**The table (`menu_text.c`).** 87 text rectangles, drawn by 124
`texProperty` rows (several rows draw one rectangle: Yes / No on six
prompts, Back on the save screens). Per rectangle: the string, the
alignment, the ink (light letters with the dark rim, or black letters on
the white panel), the em, the anchor, the line pitch and, per language,
the first line's capital middle. They were measured on the sheets: each
line's capital top and baseline (the em is the capital height less 0.7
texel of antialiasing over Arimo's 0.688; a 20-texel menu row gives 13.5
texels, 27 y units), the ink's left, centre and right edges across the
five languages (the edge that stays put is the alignment: the Options
labels end at one x, the pause items start at one x, the title and
prompts are centred), and the line pitch (15 texels on the panel
prompts). Sizes per row class: 27 y units for the 20-texel menu rows
(title, vibration, pause, Options, key config, adjust screen, Yes / No,
OK, Back, the save screens' Resume / End Game); 30 for the boot screens'
language names and 50 / 60 Hz (`title.tm2`'s heavier capitals); 33 for
"Save?" and "Continue ?"; 24 for the white-panel prompts, the card check
prompts, "Loading" / "Saving" / "Formatting" and the save headers; 27 for
the save preview's location names; 21 for "Accessing"; 18 for "MEMORY
CARD slot 1 / 2".

| screen (layouts) | rows |
| --- | --- |
| boot signs, `kanban.c` (0 language, 1 TV, 3 / 4 card check, 5 confirm) | 12: the five language names, 50 Hz, 60 Hz, "No Memory Card (PS2) inserted", the two 360 KB prompts, Yes, No |
| title (12, 13) and vibration (9) | 6: Continue, New Game (twice), Vibration, Activate, Deactivate |
| memory card, save and load (14 to 47) | 69: the slot list's header and "MEMORY CARD slot 1 / 2", the preview's 16 location names, "Accessing", "Do not remove…", OK, Back, the load / save / format prompts and their failures, "Save?", "Loading", "Saving", "Formatting", "File saved.", the overwrite and format confirmations, Yes / No, Resume Game / End Game |
| pause (56, 57) | 3: Options, Back, End Game |
| Options (58) | 13: the header, Film Effect, Sound, Stereo, Mono, Vibration, Activate, Deactivate, Hold Type, Button Configuration, Brightness, Players, Back |
| key config (59) | 9: the header, Jump, Attack, Action, Release, Hold hand / Call, Zoom, OK, Default |
| adjust screen (60) | 6: Brightness, the hint, Dark, Light, OK, Default |
| end confirm (61) and game over (62) | 6: "The game will end. Is this okay?", Yes, No, "Continue ?", Yes, No |

Left as textures, with the reason (the full list is the comment at the top
of `menu_text.c`): the ICO logo (31, 37); the LANGUAGE and TV headers (25,
32: lettering inside the swash artwork); the 50 / 60 Hz notes (35, 36:
text inside speech-bubble artwork); "Sony Computer Entertainment Europe
Presents" (46) and the copyright line (48), the corporate lettering of a
credit and a legal notice; the slot numbers and the preview's digits
(52..71, 74..136: digit tiles placed one glyph at a time); the Options
value tiles (the film effect's 0..4, the hold type's A / B, 1 / 2
players) and arrows; R1 / R2 / L1 / L2 on the key config screen (outlined
button labels, like the button glyphs); `buttons.tm2`; the panels, bars,
backdrops, the brightness markers and ruler, 1 x 1 placeholders, the
preview location rows whose rectangle is blank on every sheet, a stray
bubble corner (433) and the subtitle rows (434, 435).

**The hook.** `display_texture` (`layout_texture.c`) sets `ltHostTextRow`
for `lt_ext_IsTextRow(e)`: a port row, or a game row for which
`ui_MenuTextItemOf(e)` finds an item (not in classic mode; the row's index
is in the table; its texel rectangle is the one the table was measured on,
so tables that are not the PAL ones keep their textures). For a game row
`tex_TransTexture` runs as before, so the VRAM and packet bookkeeping is
the texture path's; only `gif_SpriteSensitiveOffset` is replaced by
`lt_ext_DrawTextRow(e, box, ofs, colour, 0)`, and `lt_glow_sprite` passes
its stretched box the same way. Everything around it is the game's (the
packet state, the colour and fade, the cursor sparkle, the glow).
`kanban.c`'s `display_texture` does the same with its own box and inset.
`ui_MenuTextDraw` maps the item's texel coordinates through the sprite's
box and texel rectangle (so either caller's half-texel inset is
honoured): each line goes at the item's anchor and capital middle,
`UI_VALIGN_MIDDLE`, aligned left, centred or right, at the em times the
vertical scale rounded to whole y units (one atlas per size). Arimo is
about 1.3 times wider than the sheets' lettering at the same capital
height, so a line longer than the room its anchor leaves in the rectangle
is set smaller to fit, down to 60 %, as the Settings rows are. Light rows
draw with `UI_HALO` in the sprite's colour; dark rows draw black (the
colour's RGB zeroed, its alpha kept) without a rim and skip the additive
glow. The draws are keyed by the row and the pass for the presenter's
interpolation, as the port rows are. The language is `ui_GetLanguage()`,
which follows `NonLinearCameraMove`, so a language change in Settings
shows in the game's menus at once (the textures follow only at their next
load).

**Classic.** `ui_SettingsInstall` hands `ico_opt_classic_menu_text` to
`ui_MenuTextSetClassic` before the first layout draws; the Display row
steps it, sets the key and the switch, and is saved when the screen is
left.

**At the output's resolution (package DEF).** Drawn into list 11 as glyph
quads, a row lands in SCENE at the scene's resolution, is halved by the
reduction and scaled into the presentation box, so in the Enhanced preset
the text was soft (three pixels between background and ink on an edge at
1080p, `font_edge`). Now each text line the hook draws, game row or port
row, goes through `ui_DrawTextDeferred` (`font.h`): it records, in place in
the list, an item (`rd_DeferredText`, an `RDC_OVERLAY_TEXT` command: the
string, the grid anchor, the size, the flags with the halo and, for the
glow pass, `UI_ADDITIVE`, the glow's stretch, and the colour after the
row's fade and dimming), then the same glyph quads as before, marked as
that item's (`rd_DeferredTextQuads`). What a present does with them is
decided when it replays (RENDER_API.md "The deferred text pass"):

- Enhanced, with the renderer installed (`ui_InstallDeferredText(1)`,
  `ui_host.c` at start-up; the replay tool and the tests install it too):
  the quads are skipped and, after the box blit, `font.c` lays each item out
  on the output through its overlay mode, rasterised at the box's scale and
  every glyph's corner on a whole output pixel, so the glyphs are drawn a
  texel a pixel (at most one pixel between background and ink on an edge,
  at 1080p and 2160p). The item keeps its place in the frame's order: the
  row's scissor, the reduction's border crop, and the passes recorded after
  it in lists 11 and 12 apply to it (a fade to black darkens it with the
  scene, the demo letterbox cuts it in its bands, the brightness step lifts
  it, a KEEP drops it, the reduction's stage tint colours it, as each did
  to the quads). The glow pass is an
  additive item with the stretch; the white panel's dark prompts are plain
  items. The mirror mode does not move it: the quads are pre-flipped and the
  present flips them back, the item is drawn unflipped where the quads end
  up.
- Original, classic menu text, no renderer, or a replay without a present:
  the items are ignored and the quads draw exactly as before (the Original
  present of a frame with an item is byte-identical to the frame without
  one, `font_edge`; the corpus's Original and mirror renders are unchanged).

`[game] classic_menu_text` therefore also keeps the Settings rows (port
rows, which have no texture) on the quad path: classic is the PS2's look
throughout. The interpolation blends an item's anchor, stretch and colour
between ticks by its key, as the quads' (RENDER_API.md "Frame rate and
interpolation").

What the deferred order cannot reproduce, and why it is accepted:

- **A tint above 1.0 that clamps.** The reduction multiplies the picture by
  the stage's tint (149 / 128 on the title) and the GS clamps the result.
  Where a text pixel is only partly covered (its edge, the halo, a dimmed
  row) over a scene bright enough to clamp, the quads' pixel was the tinted
  blend, clamped, and the deferred one is the clamped scene blended with
  the tinted text: a little darker. Fully covered pixels match.
- **Draws after the text in lists 11 and 12.** An item is drawn after the
  whole picture, so a draw recorded after it that overlaps it (the film
  noise of a cleared game, the cursor sparkle of `kanban.c`'s boot signs,
  the loading bar and the developer overlay in list 12) is now under the
  text instead of over it. The fade, letterbox, brightness and keep passes
  are applied to the item (above); the others are small (the sparkle's
  points), faint (the grain at its alpha) or seldom share the screen with a
  menu row (the loading bar, the developer overlay), and the text staying
  readable above them is no loss.
- **Reads of DISPLAY.** DISPLAY no longer holds the text: the motion blur,
  which feeds DISPLAY back into SCENE, leaves no trail of a menu row over
  gameplay, and a keep frame's retained picture (the stage load, the card
  checks) no longer shows the last full frame's rows, darkened, under the
  live ones (the overlay fixed the same for the popups). An F12 screenshot
  of DISPLAY (`rd_DumpOnDemand`) has no menu text in Enhanced.

Settings > Display > "Menu text: Classic" brings back the quads, and with
them the PS2's order, for anyone who wants it. The subtitles and the staff
roll still draw quads; `ui_DrawTextDeferred` is the call that moves them
(TODO.md, "Port font everywhere").

## Strings

`ui_Str(id)` returns the current language's UTF-8 string, `ui_StrIn(lang,
id)` a given language's; a missing entry falls back to English and an
unknown id gives "". `ui_host.c` sets the language from the game's own
choice, `NonLinearCameraMove` (2 English, 3 French, 4 German, 5 Italian,
6 Spanish, the numbering `texFile`'s language column uses), English before
it is set. The ids (`strings.h`) cover the Settings menu's titles,
sections, options, values and notes, the remap screen's columns, prompts
and the PS2 button and direction names, the quit and mirror-mode screens,
the test popup, "Achievement unlocked", the achievements' 30 titles and 30
descriptions (`UI_STR_ACH_<NAME>`, `UI_STR_ACH_<NAME>_DESC`; listed in
docs/port/ACHIEVEMENTS.md), and the game's menu words (`UI_STR_MT_*`). The
French, German, Italian and Spanish strings of the port's own text are the
author's translations (docs/TODO.md). Gamepad source names on the remap
screen are `UI_STR_PAD_*` (position names: South, East, West, North, L1 to
R3, "D-pad Up", "L-stick Left"), "Uncapped" and "fps" are
`UI_STR_VAL_UNCAPPED` and `UI_STR_FPS_UNIT`. The music gallery's group
names, hint line, Playing and Stopped and its "tables not loaded" status are
`UI_STR_GAL_*`; its entries' labels are the game's own names (files,
`seDef` names) and the album's titles, in every language.

## Popups

`ui_PopupPush(title, body)` queues a popup (8 deep, 127 bytes each);
`ui_PopupVsync()` steps the clock: 15 vsyncs sliding in from the right
edge of the 4:3 picture (smoothstep, the alpha following), 200 held, 15
sliding out (0.3 s, 4 s, 0.3 s at 50 Hz), then the next. The panel is
anchored 18 x units from the right edge and 30 y units from the top, as
wide as the longer of title (26 y units) and body (21) plus 14 units each
side, at most the picture's width less the margins: a dark translucent
panel (GS 6, 6, 9, alpha 0x5C), a 1.5-unit hairline in the menu's warm
grey on top, the title in warm white and the body in a lighter grey.

**Where it is drawn.** On the presentation overlay (RENDER_API.md, "The
presentation overlay"): `ui_HostInit` registers an overlay callback with
`rd_SetPresentOverlay`, and the presenter calls it at every present, after
it has drawn DISPLAY into the box; the callback calls
`ui_PopupDrawOverlay(ctx)`, which draws the panel and its text in overlay
mode (font.h `ui_BeginOverlay`). So:

- it is drawn at the output's resolution, never reduced or tinted with the
  frame, over the game's UI and fade;
- it is never in DISPLAY: a keep frame (pause) draws DISPLAY back without a
  popup in it, so the stale popup under the live one cannot happen, by
  construction;
- it is never mirrored: the mirror mode flips only the box blit, and the
  overlay comes after it, so the popup reads normally and stays at the
  right of the picture without a pre-flip;
- it is drawn at each present, with the queue as it is then; it is not
  interpolated (no keys), so with the frame rate uncapped its slide steps
  at the vsync clock that drives it;
- it is not part of the game's frame, so frame dumps (`dump_every`, F12)
  and `rd_replay_tool` replays of them do not show it (the tool's
  `--overlay-test` draws a test pattern on the overlay), and it is not
  drawn over the movies (`rd_video.c` presents without the overlay).

**Hooks.** `port/platform/window_host.c` calls `ui_HostInit()` after
`rd_Init` (the font, the hooks, the overlay, the popup test switch, the
quit handler),
`ui_HostVsync(ico_host_main_ticks())` at the end of `ico_window_pump` once
per vsync, after the simulation step (the test trigger and the popup
clock), and `ui_HostShutdown()` before `rd_Shutdown` (it unregisters the
overlay). The developer key `[dev] popup_test = true` (ini
`popup_test=1`; exported as `ICO_UI_POPUP_TEST`) queues the test popup
("Test popup", "Runtime text: Éléphant, Größe, señor, città, cœur") at
Main tick 100 and every 150 ticks after, so any run's frame dumps catch
one.

## Tests

- `ui_test` (ctest `ui`; exit 77 without a Vulkan device after the CPU
  checks): the font parses and "H" at 40 px is 27 to 29 px tall with solid
  stems and a clear gutter; kerning; UTF-8 decoding of the five languages'
  letters and of malformed sequences (each gives U+FFFD and consumes one
  byte); `ui_MeasureText` and `ui_ScaleFor`; every string id in every
  language non-empty, valid UTF-8 and drawable; the layout extension with
  the real `layout_texture.c` over fake tables (the fall-through, a port
  layout drawn with its halo copies and labels, no texture transfer, the
  cursor and the glow, the sparkle, no undecoded register write); the
  popup queue and slide; overlay mode at 1080 and 2160 lines and in a 16:9
  box (the grid onto the 4:3 picture, glyph quads on whole pixels at the
  bitmap's size, the pixel size, rects snapped, the scale restored), and a
  popup drawn on the overlay recording nothing into the frame's lists (list
  12 included), at the picture's right, the same with the mirror on; on the
  device, text drawn into SCENE inside its
  measured bounds and blended exactly as the GS formula, and at Enhanced
  4x (SCENE 2048 x 2048) the title's rows compared texel by texel with a
  CPU reference of the recorded quads at a 960-line and, with trilinear
  filtering, a 2160-line output (none more than 6 levels off, nothing
  outside the quads); the atlas page shape; the atlas uploads, in the frame
  and on the overlay (a page R8 holding the coverage in GS units, five new
  glyphs five rectangle updates and no whole-page update, known glyphs
  none); a popup on the overlay of a
  1920 x 1080 present (at the picture's right, text in the panel, nothing
  changed outside it). It writes `ui_test_scene.png`, `ui_test_scene4x.png`,
  `ui_test_scene4x_plain.png` and `ui_test_popup.png` beside itself.
  `rd_present` (port/render) draws a glyph through overlay mode on a real
  present too.
- `settings_test` (ctest `settings`, CPU): the menu built over fake tables
  shaped like the PAL ones and run by the real `layout_texture.c`: each
  screen's rows and labels; the repoint and its idempotence, and tables
  that are not PAL left alone; navigation through the Options entry row
  before and after the game is cleared; the language switch; a remap
  capture and its write and reload; every value through
  `ui_SettingsStep` and what `ui_SettingsSave` writes; the capture state
  machine; the title placement from the table data (`testPlacement`: one
  pitch, equal spacing between capitals, the copyright within 5 field
  lines of 195, the Options row's right edge); the quit flow; Circle on
  every port screen and, with `circle_back` on and off, in the Options
  layout; the boot-skip language and video-mode mapping.
- `settings_render` (ctest `settings_render`, exit 77 without a Vulkan
  device): `settings_test.c` built with `SETTINGS_RENDER`, the menu run
  through `rd`; each screen's SCENE written as `settings_<screen>.png`,
  including the quit screen and the Settings and Display screens at
  Enhanced 4x full height; no undecoded register write; package DEF: every
  screen (Settings, Display, Audio, Controls, Gameplay, Achievements,
  Extras, the mirror and quit screens) presented at Enhanced 1920 x 1080,
  16:9, with the menu text deferred (`settings_<screen>_1080.png`, items
  recorded) and classic (`settings_<screen>_1080_classic.png`, no item: the
  toggle switches the path).
- `font_edge_test` (ctest `font_edge`, package DEF, exit 77 without a
  Vulkan device): a deferred "H" and an overlay "H" presented at Enhanced
  1920 x 1080 and 3840 x 2160: on rows through the stems every edge has at
  most one pixel between background and ink (the quad path has three);
  the mirror leaves the deferred text in place; the Original present of a
  frame with a deferred row and a popup is byte-identical to the frame
  drawn with plain quads and to the frame with no renderer; a fade at 0x80
  hides a row and at 0x40 halves it, the letterbox cuts it at its band, a
  KEEP after it drops it and a keep frame's row after its KEEP is drawn.
  Writes `font_edge_<lines>.png`, `font_edge_<lines>_quads.png`,
  `font_edge_original.png` and `font_edge_letterbox.png`.
- `menu_text_test` (ctest `menu_text`, exit 77 without a Vulkan device
  after the CPU checks): the table's integrity in every language; with the
  disc image (`ICO_DISC_IMAGE`), every row's rectangle against the boot
  ELF's `texProperty`; every string drawable; through the real
  `layout_texture.c`, New Game and Continue drawn as text and the
  copyright line as a texture, classic mode drawing only textures, a row
  whose rectangle differs keeping its texture, and a port row lining up
  vertex for vertex with the game's OK row; package DEF: each text row an
  `RDC_OVERLAY_TEXT` item before its glyph quads, all of them marked as
  the item's, the present laying the items out with the quads' glyphs at
  1920 x 1080, a fade after the rows an op after the items, a keep frame's
  rows before its KEEP laying out nothing, classic mode no item and no op;
  on the device, the title's New
  Game row painted inside its rectangle and rim only
  (`menu_text_scene.png`).

Open items for the UI are in docs/TODO.md.
