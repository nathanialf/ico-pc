# Runtime text, layout extension and popups (Phase 6, package 6B)

The port's own text: a typeface embedded in the program, rasterised at run
time and drawn through `rd` inside the game's layout system (the Settings
menu, package 6C) and as notification popups (achievements, package 6E).

| file | what |
| --- | --- |
| `port/ui/font.c`, `font.h` | the font, the per-size glyph atlases, `ui_MeasureText`, `ui_DrawText` |
| `port/ui/layout_ext.c`, `layout_ext.h` | port rows past the ends of `texLayout` / `texProperty`, the draw hook |
| `port/ui/strings.c`, `strings.h`, `strings_{en,fr,de,it,es}.c` | `ui_Str(id)` in the game's five languages |
| `port/ui/popup.c`, `popup.h` | the popup queue, timing and drawing |
| `port/ui/ui_host.c`, `ui_host.h` | the window build's glue: the game's globals, the decoder flush, the per-vsync step |
| `port/ui/embed_font.cmake` | the font file as a C array at build time |
| `port/ui/test/ui_test.c` | the test (below) |
| `port/assets/fonts/` | `EBGaramond-Regular.ttf`, `OFL.txt` |
| `port/third_party/stb/` | `stb_truetype.h` v1.26, `LICENSE` |

## The font

**EB Garamond Regular** (SIL OFL 1.1). Chosen from the brief's candidates
(Cormorant Garamond, EB Garamond, Crimson Pro), all three rendered at 20 and
32 px with the five languages' accents before choosing:

- Cormorant Garamond (lightest weight 300 by default, Medium 500 tried) has a
  small x-height (0.386 em) and hairlines that thin out at the sizes the menu
  uses (capitals about 19 field-line halves, see Metrics), and old-style
  figures.
- Crimson Pro is sturdy but reads as a book face with a large x-height (0.43
  em) and squarer forms.
- EB Garamond keeps the classical humanist serif with enough contrast left at
  small sizes, has every letter the five languages need (Latin-1 and Latin
  Extended-A: Œ œ Ÿ, ß, ñ, the grave and acute vowels), real GPOS kerning,
  and no Reserved Font Name, so a subset may keep its name.

**What the run showed about the game's own lettering.** The brief describes
the original menu text as a thin serif/humanist face. In the title frame of
this package's run (below) the vibration screen's rows ("Vibration",
"Activate", "Deactivate"; `texProperty` 43-45) are drawn from the same
texture sheet and with the same row geometry as the Options screen (texFile
21, `menu_PAL_0x`, 20-texel rows, `dispH` 40; row 44 is the very texel
rectangle of Options row 316): that lettering is a plain neo-grotesque sans
(Helvetica/Arial-like), light grey with a soft dark rim baked around the
letters. A serif therefore contrasts with the game's menu text rather than
matching it. Swapping the face is a one-file change (the `.ttf` in
`port/assets/fonts/` and `_ui_font` in `port/ui/CMakeLists.txt`; the metrics
constants below scale with the cap height). An OFL sans with Arial-like
proportions (Arimo, Liberation Sans) would match the PAL menus. That is a
decision for the orchestrator or the user (open item 1).

### The font file

`EBGaramond-Regular.ttf` is derived from `google/fonts`
`ofl/ebgaramond/EBGaramond[wght].ttf` (the variable font, version 1.003;
provenance and digests in THIRD_PARTY.md) with fontTools 4.63.0:

```python
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
f = instancer.instantiateVariableFont(TTFont("EBGaramond[wght].ttf"), {"wght": 400})
f.save("inst.ttf")
# pyftsubset inst.ttf --unicodes="U+0020-007E,U+00A0-017F,U+0192,U+02C6,U+02DC,
#   U+2013-2014,U+2018-201E,U+2020-2022,U+2026,U+2030,U+2039-203A,U+20AC,U+2122"
#   --layout-features=kern --no-hinting --desubroutinize --name-IDs='*'
#   --output-file=sub.ttf
f = TTFont("sub.ttf")
for l in f["GPOS"].table.LookupList.Lookup:   # stb_truetype reads lookup type 2 only
    if l.LookupType == 9:
        l.SubTable = [st.ExtSubTable for st in l.SubTable]
        l.LookupType = l.SubTable[0].LookupType
        l.SubTableCount = len(l.SubTable)
f.save("EBGaramond-Regular.ttf")
```

The result is 50,988 bytes, 388 glyphs. `port/ui/CMakeLists.txt` turns it
into `ui_font_data.c` (`const unsigned char ui_font_ttf[]`) in the build
directory at build time (`embed_font.cmake`, a `cmake -P` script, no extra
tools); nothing generated is tracked, and the program needs no file beside
it. The licence text ships in `port/assets/fonts/OFL.txt` and must go into the
release's third-party notices (THIRD_PARTY.md).

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

**Menu size (`UI_MENU_TEXT_SIZE` = 29).** Measured on the title frame of the
run (dump 600 through the presenter at 960 x 720, where a y unit is 1.607
output pixels): the capitals of the vibration rows are 29 to 31 output
pixels tall ("A" of "Activate" 29, "V" of "Vibration" 31), about 18.7 y
units or 9.3 field lines in a 20-field-line row. EB Garamond's capitals are
0.65 em, so the em is 29 (capitals 18.9 y units). Widths differ: "Vibration"
is about 92 x units in the game's face and 116 in EB Garamond at 29, the
serif face being wider set. Row boxes default to the Options screen's 20
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
`texH` gets `dispH` 40. Link fields (`up`/`down`/`left`/`right`, the item
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
unlocked". The translations are the author's, not reviewed by native
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

- the font parses; "H" at 40 px is 28 px tall on the baseline with solid
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
  1589 and 3381 pixels, the acute of the É above the capitals; in the stems
  of a 150-unit "I" the colour (100, 50, 25) over (20, 40, 60) is exactly the
  MODULATE result at alpha 0x80 (199, 99, 49) and within one step of the GS
  formula `((Cs - Cd) * As >> 7) + Cd` at alpha 0x40 (worst difference 1 over
  106 pixels: the hardware blender's rounding of the dual-source LERP, as in
  RENDER_API.md section 7). It writes `ui_test_scene.png` beside itself.

Results (2026-10-05): the window build on Linux (gcc, lavapipe, validation
layer) 51 of 51 tests passed with `ui`; the headless Linux build of HEAD with
this package 49 of 49 (two disc tests skipped: no disc in that tree);
`win-x64` builds `ico_pc.exe` and `ui_test.exe`.

## Run (2026-10-05)

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

## Open items for the Settings package (6C)

1. **The typeface.** The PAL menus' lettering is a sans (The font, above).
   Keep EB Garamond as a deliberate contrast, or swap in an OFL sans with
   Arial-like proportions (Arimo, Liberation Sans) to match; the swap is the
   `.ttf` and one CMake path, then the cap-height constant.
2. **`.gitignore` ignores `port/assets/`.** Its `assets/` rule (extracted
   game assets) matches `port/assets/fonts/`, so the font and its licence
   are not seen by `git status` and `tools/check_no_rom.sh` (rule 6, tracked
   files matching an ignore pattern) would reject them once forced in. The
   rule needs an exception (`!/port/assets/` with `/port/assets/**` re-ignored
   except `fonts/`, or the folder moved), outside this package's files.
3. **Entering the menu.** A game layout's rows are one contiguous range, so
   a "Settings" item cannot be appended to the Options layout (58, rows
   297..332). Options: a port layout chained from 58 by `link` (drawn and
   navigated with it; the item links may cross between game and port rows),
   or repointing an existing row's `right`/`left` at a port layout from the
   loaded data at boot. Either is 6C's call; both work with the extension.
4. **Translations** need a native-speaker review; the French, German,
   Italian and Spanish strings are the author's.
5. **The rd entry points** (Requested rd API): the R8 atlas through
   `font.hlsl`, and the post-present overlay for popups.
6. **Values that change** (On/Off, a resolution): `lt_ext_SetText` /
   `lt_ext_SetStr` on the value row from the layout's `la_*` proc; a
   language switch only needs the labels redrawn, since `ui_Str` reads the
   language at draw time.
7. Long labels are not clipped or wrapped in a row; 6C sizes `dispW` (or
   measures with `ui_MeasureText`).
