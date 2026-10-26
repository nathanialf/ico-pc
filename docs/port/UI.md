# UI: the port's menus

How the port's own screens are built: the Settings menu, its lists and galleries, the New Game
and Quit screens, photo mode's screen, popups, the touch overlay and the mouse in menus. Every
port screen is a layout in an extension of the game's own layout tables, so the game's layout code
draws, navigates, fades and sounds it; the port supplies the rows, their words and the procs.

## Files

| path | what it holds |
| --- | --- |
| `port/ui/layout_ext.h`, `layout_ext.c` | the layout extension: port layouts and rows past the game's tables, text, glyph and rectangle rows, the text hook, the back buttons, pointer roles |
| `port/ui/settings.h`, `settings.c` | the Settings menu: the pages, the entry rows, the New Game and Quit screens, the values and their setters, the saves |
| `port/ui/ui_list.h`, `ui_list.c` | the scrolling list pages (Achievements, Remap, Music) |
| `port/ui/ui_hint.h`, `ui_hint.c` | a line of button prompts (glyphs and words) |
| `port/ui/strings.h`, `strings.c`, `strings_{en,fr,de,it,es,ja,ko,zh}.c` | the UI strings, one table per language; the serial notes; the Japanese, Chinese and Korean line-break rules |
| `port/ui/ui_lang.h`, `ui_lang.c` | the port text's language from `[ui] language` and the running disc |
| `port/ui/font.h`, `font.c` | Arimo, then Noto Sans JP, then Noto Sans KR, then Noto Sans TC (first of the three while the language is Chinese), through stb_truetype: atlases, measuring, plain text, the presentation overlay mode, the sheet rasteriser |
| `port/ui/menu_font.h`, `menu_font.c` | the menus' text in the look of the game's menu sheets: strip pages, inks, rims |
| `port/ui/menu_text.h`, `menu_text.c` | the table of the game's menu words, measured from the PAL sheets |
| `port/ui/ui_internal.h` | font.c's and menu_font.c's internals shared with `ui_host.c` and the tests |
| `port/ui/embed_font.cmake` | a font file as a C array at build time; the subsets' code points |
| `port/assets/fonts/Arimo-Regular.ttf`, `OFL.txt` | the font and its licence |
| `port/assets/fonts/NotoSansJP-Regular-subset.ttf`, `NotoSansJP-OFL.txt` | the second font (the characters Arimo lacks) and its licence |
| `tools/subset_font_ja.py` | makes and checks the second font's subset |
| `port/assets/fonts/NotoSansKR-Regular-subset.ttf`, `NotoSansKR-OFL.txt` | the third font (the Hangul the other two lack) and its licence |
| `tools/subset_font_ko.py` | makes and checks the third font's subset |
| `port/assets/fonts/NotoSansTC-Regular-subset.ttf`, `NotoSansTC-OFL.txt` | the fourth font (the Chinese table's ideographs) and its licence |
| `tools/subset_font_zh.py` | makes and checks the fourth font's subset |
| `port/ui/popup.h`, `popup.c` | the notification popups |
| `port/ui/touch_ui.h`, `touch_ui.c` | the touch overlay's drawing |
| `port/ui/ui_fps.h`, `ui_fps.c` | the Frame rate counter: its text on the top layer and the calls that feed its number (`port/platform/fps_counter.h`) |
| `port/ui/ui_mouse.h`, `ui_mouse.c`, `ui_mouse_geom.h`, `ui_mouse_geom.c` | the mouse pointer in menus: the glue and the arithmetic |
| `port/ui/gallery.h`, `gallery.c`, `gallery_play.c` | Extras > Music: the list and its engine |
| `port/ui/photo_ui.h`, `photo_ui.c` | photo mode's screen and its help panel |
| `port/ui/ui_host.h`, `ui_host.c` | port/ui wired to the game and rd in the window build |
| `port/game/title_logo.h`, `title_logo.c` | the title's logo hidden under the menus; the title's full-frame models |
| `ico2/common/src/layout_texture.c`, `layout_action.c`, `kanban.c` | the game's side: the table lookups, the text hook, the procs' hooks |
| `port/ui/CMakeLists.txt` | the libraries (`ico_ui`, `ico_gallery_live`, `ico_ui_cpu`, `ico_ui_rd`, ...), the embedded font, the tests |

## The layout extension

The game's menus are tables loaded at run time: `texLayout[80]` (a layout: its first and last
property rows, its proc, fades, cursor and link) and `texProperty[436]` (a row: its texture
rectangle, its box on screen, its item links); on the Japanese disc `texLayout[76]` and
`texProperty[412]`, and the extension starts there (`LT_GAME_LAYOUT_COUNT` and
`LT_GAME_PROPERTY_COUNT` are the running disc's, `lt_ext_game_layout_count` and
`lt_ext_game_property_count`; arrays sized at build time take the larger, `_MAX` 80 and 436;
[The Japanese and Korean discs](#the-japanese-and-korean-discs)). Every property row is used, and the empty layout
rows 66..79 sit inside every stage's layout range, whose textures the game initialises at each
stage load, so a port row there would be looked up as a texture file. The port therefore extends
both tables past their ends: layouts `80 ..` and properties `436 ..`, up to
`LT_EXT_MAX_LAYOUTS` (32) and `LT_EXT_MAX_PROPERTIES` (768) (`port/ui/layout_ext.h`, header
comment). `layout_texture.c` reads both tables through `LT_LAYOUT(i)` and `LT_PROP(i)`, which are
`lt_ext_layout(i)` and `lt_ext_prop(i)`: the game's row below its count, the port's above it, and
a zeroed scratch row for an index in neither, never memory outside the tables
(`ico2/common/src/layout_texture.c`, the `LT_LAYOUT` and `LT_PROP` definitions;
`port/ui/layout_ext.c` (`lt_ext_layout`, `lt_ext_prop`)). No stage range reaches an extension
index, so no texture is looked up for a port row. Link fields may name game or port rows freely.

`lt_ext_add_layout` appends a layout and `lt_ext_add_property` a row with its label; each returns
the new index or -1 when full, logged once as `ui: layout extension full`
(`port/ui/layout_ext.c` (`lt_ext_add_layout`, `lt_ext_add_property`)). `lt_ext_reset` drops every
port row and layout. In developer mode the Settings build logs how much of the extension it
uses (`port/ui/settings.c` (`build`)).

### Rows

A port row is one of three kinds (`port/ui/layout_ext.c` (`ExtRow`)):

- a text row: a label, either a string id (`UiStrId`, which follows the language) or a literal
  UTF-8 text, with an em size (0 is `UI_MENU_TEXT_SIZE`, 27 y units) and an alignment in its box
  (`LtExtText`); `lt_ext_set_text`, `lt_ext_set_str` and `lt_ext_set_size` change it;
- a glyph row: one of the game's button sprites (`LtExtGlyph`: the four face buttons, the four
  shoulder buttons, the Options values' left and right arrows), drawn by the texture path from the
  texture and texel rectangle of the game row that shows it in the PAL tables; with tables that are
  not the PAL ones it draws nothing (`lt_ext_add_glyph`, `lt_ext_glyph_source`);
- a rectangle row: a filled box, `fill` of it from the left, in a colour times the row's colour
  (the music gallery's progress bar, the Characters page's swatches) (`lt_ext_add_rect`,
  `lt_ext_set_fill`, `lt_ext_set_rect_color`).

A row with no width gets 400 x units and one with no height 40 y units, the Options screen's row
height (`lt_ext_add_property`). `lt_ext_set_dim` greys a row: its colour at half whatever the
cursor does, the locked style of Settings > Extras (`lt_ext_set_dim`, `lt_ext_draw_row`). Masks
are the game's own: a row whose `masked` flag is set is not drawn, and the port sets it with the
game's `lt_mask_property` and the row's `defaultMask` (`ico2/common/src/layout_texture.c`
(`display_texture`, `lt_mask_property`); `port/ui/settings.c` (`layoutPage`)).

### The grid

`layout_texture.c` places rows on a 640-pixel by 226-field-line screen centred on (320, 113): a
row's box is `((dispX - 320) * 16, (dispY - 113) * 16)` in 1/16 pixel and 1/16 field line, its
size `dispW` by `dispH` (or the texel size when 0), centred across when `centerX` is set
(`ico2/common/src/layout_texture.c` (`primarySpriteRect`, `display_texture`)). The text API works
on the same grid with half field lines down, so both axes count 4:3 pixels of a 640 x 448 frame:
x 0..640, y 0..452 with the centre at (320, 226); a y unit is 15/14 of an x unit on screen, which
glyph placement corrects (`port/ui/font.h` (`UI_GRID_W`, `UI_GRID_CY`, `UI_X_PER_Y`)). The Settings
geometry is written in grid units: labels right-aligned from x 44 to 344, a stepped value centred
between arrows at 362 and 556, the header at field line 12, notes at 196 (`port/ui/settings.c`
(`LABEL_X`, `ARROW_L_X`, `ARROW_R_X`, `HEADER_Y`, `NOTE_Y`)).

A layout's backdrop and any item whose box spans the whole grid (the pause and End Game bars) are
drawn full frame: on a wide [native frame](ARCHITECTURE.md#terms) the 640-grid helpers scale x by
the frame width over 640, decided on the layout's data (`ico2/common/src/layout_texture.c`
(`LT_FULL_BEGIN`); `ico2/common/include/layout_texture.h` (`lt_rect_spans_grid`)). Everything else
is the centred 4:3 box.

### How the game draws a port row

`display_texture` is the game's row drawer. For each row it asks `lt_ext_is_text_row`: a port text
or rectangle row, a glyph row without its texture, or a game row of the menu text table is drawn as
text; every other row keeps its texture. For a text row the port's draw hook,
`lt_ext_draw_text_row`, runs where the game would call `gif_SpriteSensitiveOffset`, with the box,
texel rectangle and colour the game computed, so the fade, the half colour of an unselected row, the
cursor's sparkle and the selected row's glow are the game's (`ico2/common/src/layout_texture.c`
(`display_texture`, `lt_glow_sprite`, `ltHostTextRow`); `port/ui/layout_ext.h`
(`lt_ext_draw_text_row`)). A game row of the menu text table still transfers its texture, so VRAM
and the packets are the texture path's. The boot screens' prompts go through the same hook from
`kanban.c`'s own `display_texture` (`ico2/common/src/kanban.c`).

`lt_ext_draw_row` places a port label in the box the texture sprite would have had: the capitals'
middle two y units above the box's centre (where the game's 20-texel menu rows have theirs, so a
port row on the game's grid lines up with the game's rows), left, centred or right by the label's
alignment. A label wider than its box is set smaller to fit, down to 60 %, the widest line of a
multi-line label counting. The glow pass maps the label through the same stretch the glow sprite
gets, with the additive blend. Each draw is keyed by the row and the pass, so the presenter
[interpolates](ARCHITECTURE.md#terms) a row that moves or fades (`port/ui/layout_ext.c`
(`ROW_CAPS_ABOVE_CENTRE`, `lt_ext_draw_row`, `rowKey`)).

A port row in a layout chained after the current one with no cursor of its own (the Settings entry
rows after the title's and pause menu's rows) is selected and dimmed by the current layout's
cursor (`ico2/common/src/layout_texture.c` (`display_texture`, `curNo`)).

### Back and pointer roles

`lt_ext_back_buttons` is the pad bits that take the game menus' back action where the game checks
Triangle: Triangle, plus Circle while `[game] circle_back` is on (the default); off, Triangle alone,
the PS2's checks. On the Japanese disc it is Cross alone, that disc's cancel, and `[game]
circle_back` does not apply ([The Japanese and Korean discs](#the-japanese-and-korean-discs)). `layout_texture.c`'s `default_item_select` uses it as `LT_BACK_BUTTONS` and the
procs of `layout_action.c` as `LA_BACK`, except where Triangle is not a cancel or Circle has a
meaning of its own (`port/ui/layout_ext.h` (`lt_ext_back_buttons`);
`ico2/common/src/layout_action.c` (`LA_BACK`)). The port's own screens take Circle as Triangle
always and confirm with Cross; on the Japanese disc they take Cross as Triangle and confirm with
Circle (`port/ui/layout_ext.h` (`lt_ext_port_back_buttons`, `lt_ext_confirm_button`);
`port/ui/settings.c` (`PAD_BACK`, `PAD_OK`)). The option itself is
[GAME_FEATURES.md](GAME_FEATURES.md#circle-back)'s.

Each port row has a pointer role for the mouse: `LT_POINTER_AUTO` (an item is pointed at and a
click is the disc's confirm on it, Cross, or Circle on the Japanese disc), `NONE` (never hit: a list heading, an empty slot), `LEFT` and `RIGHT` (a
value's arrows, padded), `STEP` (a stepped value: a click is Right), `UP` and `DOWN` (a scroll mark: a click or a tap is Up or Down on the page's cursor, which does not move) (`port/ui/layout_ext.h`
(`lt_ext_set_pointer_role`)).

## The Settings menu

### Entry

`ui_settings_install`, called from the game's `init_layout_texture`, builds the port layouts once
and points the game's rows at them (`port/ui/settings.h`; `ico2/common/src/layout_texture.c`
(`init_layout_texture`)). The pause menu's Options row (294, layout 57) opens the Main page through
its right link, and a "Photo mode" row is placed under it while a stage runs, Back one pitch lower
(`port/ui/settings.c` (`repoint`, `placePause`)). On the title, an Options row and a "Quit to
desktop" row ("Quit game" on Android) are chained after both title menus (12, Continue / New
Game; 13, New Game) in a one-row port layout on the game layout's link chain
(`port/ui/settings.c` (`buildEntries`, `repoint`)). The game's own Options screen, layout 58, is
reached from nowhere: its settings are on the pages. The tables are checked first; tables that do
not look like the PAL data are left alone with one log line (`port/ui/settings.c` (`repoint`)).

`placeTitle` lays out the title block: Continue at field line 119, New Game, Options and Quit one
game pitch (20 field lines) apart, and the copyright line 19 lines under Quit, so the block fits
between the logo and the picture's end (`port/ui/settings.c` (`placeTitle`, `TITLE_CONTINUE_Y`,
`TITLE_PITCH`, `TITLE_COPYRIGHT_STEP`)). While a page opened from the title is up, the title's logo
is not drawn (`port/game/title_logo.h`; [The title](#the-title)).

### Pages

`build` makes every page from tables of rows, string ids and links; `_Static_assert`s keep the
tables in step and within the `MAX_ROWS` (16) budget of a page (`port/ui/settings.c` (`build`);
`port/ui/settings.h` (`UiSettingsPage`)):

| page | rows |
| --- | --- |
| Main (`UI_PAGE_MAIN`) | Display, Effects, Graphics (`UI_PAGE_GRAPHICS`), Graphics driver, Audio, Controls, Gameplay, Language, Achievements, Extras, Save a picture for a bug report, (an rc build on Android: Share a bug report,) Developer mode, Dump textures, Dump models, Back |
| Display | Preset, Resolution, Aspect, Anti-aliasing, Window mode, Vsync, Texture pack, Model pack, Frame rate, Frame rate counter (`UI_OPT_SHOW_FPS`, `[video] show_fps`, Off or On), Video mode, Back |
| Effects | Full height, Filtering, Brightness, CRT filter, CRT strength, the six game effects (Motion blur Off, Original or Smooth, `UI_OPT_EFFECT_MOTION_BLUR`, stepped round with `stepIndex` and its note `UI_STR_EFFECT_MOTION_BLUR_NOTE`) with Water reflections (Original or Sharp, `UI_OPT_EFFECT_WATER`) after Fog, Full pixel, Back |
| Graphics | Graphics API (`UI_OPT_GRAPHICS_API`: Automatic, then each name `ui_settings_set_backend_names` was given as Vulkan, OpenGL, Direct3D 12 or Direct3D 11; the shown value and the step base are the config's `video.backend`, an unlinked value shows as written and steps from Automatic; a step writes `video.backend` ("" for Automatic) and sets `s_dirtyConfig`, so the file is written on leaving; the note `UI_STR_GRAPHICS_API_NOTE` says Apply switches now and the choice is otherwise used from the next start), Apply (`UI_OPT_GRAPHICS_APPLY`, shown while a `UiGraphicsHost` is set, two or more names, no switch under way and the row's value, Automatic counting as the host's `automatic()` (the backend Automatic started this run) or else `vulkan`, a linked name that is not the host's `running()`; note `UI_STR_GRAPHICS_APPLY_NOTE`), Keep and Revert (`UI_OPT_GRAPHICS_KEEP`, `UI_OPT_GRAPHICS_REVERT`, shown only in the countdown, which hides Graphics API and Back), Back (the link and the page only with two or more names) |
| Graphics driver | Driver, Add a driver, Add from the drivers folder (`UI_OPT_GPU_ADD_FILE`, stepped over `fileName(i)`, shown while `fileCount()` is above 0, Cross calls `installFileBegin`), Remove this driver, Back (Android) |
| Audio | Volume, Music, Effects, Output, Device, Back |
| Controls | Remap (a list), Button configuration, Vibration, Hold type, Stick fix, the mouse camera's six rows, Circle back, Touch screen (a link, with a touch screen only), Back |
| Touch screen (`UI_PAGE_TOUCH`) | Touch controls, Touch size, Touch opacity, Edit layout (`UI_OPT_TOUCH_EDIT`), Snap to grid, Grid size, Reset layout, Reset all layouts, Back |
| Edit layout (`UI_PAGE_TOUCH_LAYOUT`) | no shown rows: the editor's own screen ([The touch layout editor](#the-touch-layout-editor)) |
| Gameplay | Yorda safe, Stick fix, Door fix, Natural chains, Skip the opening (`UI_OPT_SKIP_OPENING`), Film effect, Players, Back |
| Achievements | a list, the popups switch |
| Extras | Music, Models, Credits, Characters, Game variants (`UI_PAGE_VARIANTS`), Add to Steam, Export textures (`UI_OPT_EXTRAS_EXPORT_TEXTURES`), Achievement popups, Back |
| Game variants (`UI_PAGE_VARIANTS`) | Shadow toughness, One hit per swing, Shadows never dodge, Shadow aggression, No hints, Queen barrier, No cutscene look-around, Ignore cutscene cameras (`UI_OPT_VAR_*`, one per `IcoVariantId`), Debug menu (`UI_OPT_VAR_DEBUG_MENU`, a press row, shown with Developer mode on), Back |
| Music | the music gallery, a list |
| Characters | nine colour rows, Switch, Randomize, Reset, Back |

**Save a picture for a bug report** is an action row on Main directly above Developer mode
(`UI_OPT_REPORT_PICTURE`), a player's row shown with developer mode on or off while a host handler is set
(`ui_settings_set_frame_dump_handler`: the window host's `report_picture_arm`, which runs F12's
`frame_dump` 3 s later; none headless). Cross calls the handler once, logs `settings: a picture
for a bug report in 3 s` and shows the popup `UI_STR_REPORT_PICTURE_ARMED` ("Picture in 3
seconds") with `UI_STR_REPORT_PICTURE_ARMED_BODY`; its note is `UI_STR_REPORT_PICTURE_NOTE`
(`port/ui/settings.c` (`reportPicture`, `optShown`)).

**Share a bug report** (`UI_OPT_SHARE_REPORT`) exists in an rc build alone: the option, its two
strings (`UI_STR_OPT_SHARE_REPORT`, `UI_STR_SHARE_REPORT_NOTE`, all seven tables), the table
entries, its note and its Cross case are inside `#if ICO_RC_TOOLS` (`settings.h` and `strings.h`
default it to 0; [BUILDING.md](../BUILDING.md#release-candidate-tools)), and `MAX_ROWS` is
`16 + ICO_RC_TOOLS`. It sits directly below Save a picture for a bug report and shows on Android
(`ico_video_android`) while a handler is set (`ui_settings_set_share_report_handler`: the window
host's `share_report_arm`; at `ICO_RC_TOOLS` 0 the setter keeps nothing). Cross logs `settings:
share a bug report (an rc build's row)` and calls the handler once, with no popup (the system's
share menu opens over the game). With all eighteen rows shown (the Graphics link too) the page starts at line 26, 9
apart (Back at 179, its box to 219; `pagePitch`; no render check yet).

`buildOptionPage` makes a page: a centred header, then per option a right-aligned label and, for
a stepped option, a centred value row and the two arrow rows owned by the label, with their pointer
roles; then the page's notes, one centred line under the rows shown while its row is selected
(`port/ui/settings.c` (`buildOptionPage`, `addHeader`, `addOption`, `addNote`, `setNote`)). The
navigable rows are linked in a loop as the Options screen's are (`linkRows`), and the page's
layout runs `settingsProc`. The Main page also gets the version row (below); Extras its locked
value for Credits; Characters a colour swatch per row and the viewer's prompt line.
`buildListPage` makes the three list pages over `ui_list` ([Lists](#lists)).

Which rows show depends on where the menu was entered and what the machine has: the game's own
settings (Brightness, Vibration, Button configuration, Film effect, Players) only from
the pause menu (Hold type shows from the title too: `[gameplay] hold_type`), Film effect and Players once the game is cleared; Video mode, Model pack and Extras'
Music, Models and Credits only from the title (Add to Steam also only where the window host installs
its handler, Export textures only while a `UiTextureExportHost` is set, the window build's; neither is in
`isExtrasOpt`, which is the three rows that leave the stage); the dump rows only in developer mode; the Touch screen link only with a touch screen; the
window and mouse camera rows not on Android; the Graphics link only with two or more renderer names (`ui_settings_set_backend_names`), the Graphics driver link only with a driver host on an
Adreno GPU (`port/ui/settings.c` (`optShown`, `isGameOpt`, `isExtrasOpt`)). `layoutPage` masks the
hidden rows and spaces the shown ones by the page's pitch table, which keeps Back's box inside the
226 lines and above the notes (`port/ui/settings.c` (`layoutPage`, `pagePitch`): Display is 13 apart from
36 with its twelve rows on the title, Back at 179); Effects' fourteen
rows from the pause menu (Brightness shown) are 12 apart from line 30, Back's box ending at 222.

### Values and saving

A left or right press on a value calls its setter at once: `ico_video_set`, the `ico_opt_set_*`
options, the live binding table, the game's own variables (`NonLinearCameraMove` for the language,
`systemStatus[0]` and `gsResetFunc` for the video mode, `optionScreenMode`, `optionControlType`
(from `[gameplay] hold_type`: `applyHoldType`, also at `ui_settings_install`),
`girlControlMode`, `iosPadActRequestEnable`, the brightness step `systemStatus[11]`)
(`port/ui/settings.h`, header comment; `port/ui/settings.c` (`ui_settings_step`)). The game's own
settings are written by the game's saves, never into `config.toml`, except Hold type, which
`config.toml` keeps (the card's value is written but not read back). A row that cannot step now
(the video mode from the pause menu, the resolution under the CRT filter, a pack row with no pack
installed) shows its value and ignores the press (`canStep`).

What changed is written when a page is left (`leaveTo`), before an action that leaves the menu,
and on quit: `ui_settings_save` writes `[video]` through `ico_video_save`, the bindings through
`ico_input_write_bindings`, the rest through `ico_config_save`; `ui_settings_save_on_quit` also
catches setters outside the pages (`port/ui/settings.c` (`ui_settings_save`,
`ui_settings_save_on_quit`)). [CONFIG.md](CONFIG.md) has the file.

### Adding a row

A new option row needs:

1. a `UiSettingsOpt` value in `port/ui/settings.h`; a row with a value to step goes between
   `UI_OPT_PRESET` and `UI_OPT_DEVELOPER`, the range `steppable` treats as stepped
   (`port/ui/settings.c` (`steppable`));
2. its option and its label string in the page's tables in `build`, at the place it shows; each
   page's options, strings (and on Main and Controls, links) are parallel arrays the
   `_Static_assert`s keep in step, and a page holds at most `MAX_ROWS` (16) rows, which `addOption`
   places;
3. its value text in `rawValue` and its setter in `ui_settings_step`, plus a case in `optShown`
   when it is not always shown and in `canStep` when it cannot always step;
4. a `UI_STR_OPT_*` id in `port/ui/strings.h` and its text in each of `strings_en.c`,
   `strings_fr.c`, `strings_de.c`, `strings_it.c`, `strings_es.c`, `strings_ja.c` and
   `strings_ko.c` (`strings_complete` fails on a missing entry, which would fall back to
   English);
5. for a config key, the key in its owner's module ([CONFIG.md](CONFIG.md#adding-a-key)).

The `settings` test holds each page's rows as fixed lists (`port/ui/test/settings_test.c`
(`testBuild`)), which gain the row; new cases go in `port/ui/test/settings_extra_test.c` (ctest
`settings_extra`, with `checkPageFits` for the page's fit); `settings_render` draws the page and
`font_coverage` checks the label's characters.

### Back

`settingsProc` runs a page each [Main tick](ARCHITECTURE.md#terms): it sets the menu language
(`ui_settings_apply_language`), refreshes the values, waits for the fade, then handles the pad.
`PAD_BACK` (Triangle or Circle; Triangle or Cross on the Japanese disc) goes to the page's parent, or to the layout the menu was entered from, with
the cursor on the row that led there, saving first (`port/ui/settings.c` (`PAD_BACK`,
`settingsProc`, `leaveTo`); `ico2/common/src/layout_action.c` (`la_host_leave`)).

### The version row

`main_host.c` hands the package label (`ICO_VERSION_LABEL`, set by CMake from the release label)
to `ui_settings_set_version` before the pages are built; `addVersion` puts it small and greyed in
the Main page's top-right corner, right-aligned to x 620 above the header, not selectable and not
in the strings table. A development build's empty label shows nothing (`port/platform/main_host.c`
(`host_main`); `port/ui/settings.c` (`addVersion`, `VERSION_X`, `VERSION_SIZE`);
`port/ui/settings.h` (`ui_settings_set_version`, `ui_settings_version_row`)).

### The New Game and Quit screens

After the game's vibration choice, `la_vibe_select` switches to the port's New Game screen: Mirror
mode and New Game+, each Off or On; Cross or Start confirms both, sets the run's mirror mode and
`gFlagGameClear`, then calls `la_host_new_game_go`, which starts the game as the vibration screen
did. Triangle goes back to the vibration screen (`port/ui/settings.h`
(`ui_new_game_screen_enter`); `ico2/common/src/layout_action.c` (`la_host_new_game_screen`,
`la_host_new_game_go`)).

Cross on the title's Quit row opens "Quit to desktop?" with Yes and No, the cursor on No. Yes writes
what is pending and calls the quit handler: the [window build](ARCHITECTURE.md#terms)'s posts
`SDL_EVENT_QUIT`, so the run ends the way closing the window ends it; with no handler (the
[headless build](ARCHITECTURE.md#terms)) it calls `exit(0)` (`port/ui/settings.h`
(`ui_settings_set_quit_handler`); `port/ui/settings.c` (`requestQuit`); `port/ui/ui_host.c`
(`hostQuit`)).

### Hosts

The menu knows nothing of the platform; the window host installs what the pages ask: the window
mode query, the anti-aliasing list and block, the texture and model pack counts, the touch query,
the touch editor's host (`UiTouchEditHost`), the Quit word, the Graphics driver host (`UiGpuDriverHost`), the renderer names (`ui_settings_set_backend_names`, filled from `rhi_backend_name` in `ico_window_open`, so port/ui never links the RHI), the Graphics page's Apply host (`UiGraphicsHost`, below), the Steam handler, the texture export host (`UiTextureExportHost`, below), the models
handler, the Characters host (the model viewer's panel) (`port/ui/settings.h`
(`ui_settings_set_*`)). A page whose host is missing hides its row.

### Graphics > Apply

`UiGraphicsHost` (`port/ui/settings.h`) is set by `port/platform/window_switch.c`
(`ico_window_switch_install`): `running()` names the backend running as the config names it,
`switchBegin(name, why)` asks for a switch (`UI_GFX_WHY_APPLY`, `_REVERT`, `_TIMEOUT`) and answers
`UI_GFX_SWITCH_PENDING` (then `switchPoll()` is asked every frame and every vsync until it is not),
`_DONE`, `_FAILED` (the name did not start, the backend that ran is back) or `_REFUSED`; `kept()` is
Keep, which the window logs; `automatic()` (may be NULL) names the backend Automatic started this
run (its fallback or a restart's), NULL for Vulkan or a start the player chose. The page's state is
`s_gfx` (`port/ui/settings.c`): idle, applying, countdown, reverting.

- Cross on Apply records the backend running and asks for the row's (`gfxApply`); the row's value
  moves to `s_gfx.trial` and `video.backend` holds the value the page found until Keep, so every
  save in between (`ui_settings_save`, the close button's `ui_settings_save_on_quit`, any
  `ico_config_save`) writes that; the row shows the trial while applying and in the countdown
  (`backendValue`). While a switch is under way the page takes no input (`gfxProc`).
- Done: the countdown, `UI_GFX_KEEP_SECONDS` (10) times the popups' vsync rate
  (`ui_popup_vsync_hz`), counted by `ui_settings_vsync` (`ui_host_vsync` calls it after
  `ui_popup_vsync`); the cursor goes to Revert (`gfxRefresh`), so a second Cross meant for Apply
  goes back. Keep and Revert share one note, `UI_STR_GRAPHICS_KEEP_ASK` with one `%d`, the
  seconds left rounded up, written each refresh (`gfxNote`, outside `setNote`'s cache).
- Keep: the trial into `video.backend` and `s_dirtyConfig` (written on leaving), the host's
  `kept()`.
- Revert, Circle (`PAD_BACK`) or 0: `video.backend` back to the value the page found (`gfxEntry`:
  taken at the first step or Apply after the page is entered), `switchBegin` with the backend that
  ran.
- Failed: a popup, `UI_STR_GRAPHICS_FAILED` with two `%s` (the backend that did not start, the
  one kept, by their proper names); after Apply `video.backend` keeps the value the page found, as
  Revert puts it back, so the file names a backend that starts. Refused: nothing, the trial back
  in `video.backend`.

ctest `settings_extra` (`testGraphicsApply`).

### Extras > Export textures

`UiTextureExportHost` (`port/ui/settings.h`) is set by `port/platform/window_host.c` in
`ico_window_open` (`s_texExportHost`: `begin` and `cancel` are `texpack_export_begin` and
`texpack_export_cancel`, `poll` is `tex_export_poll`, which copies `texpack_export_poll`'s state,
`done`, `total` and `written` and writes `dir` as `textures/<pack_serial>/dumps`, the folder as the
player reads it; the states are asserted equal, `UI_TEXEXPORT_*` = `TEXPACK_EXPORT_*`) and cleared
in `ico_window_close`, which then calls `texpack_export_shutdown` before `rd_shutdown`;
`main_host.c` also registers `texpack_export_shutdown` with `at_shutdown` after
`texpack_shutdown`, so the worker joins before the pack closes ([PACKS.md](PACKS.md)). The headless
build installs no host, so the row stays hidden there and in the UI tests without a fake.

- The row shows from the title only (`optShown`), after Add to Steam; its note is
  `UI_STR_EXPORT_TEXTURES_NOTE`. Dump textures does not hide it (the same names, a file already
  there is kept).
- Cross (`texExportPress`): while the host's state is `UI_TEXEXPORT_RUNNING`, `cancel()` (log
  `extras: export textures: stop asked at D of T`); otherwise `begin()`: 0 sets
  `s_texExportPending` (log `extras: export textures: started`), -1 shows the failed popup at once
  (log `extras: export textures: could not start`; the core logs its reason).
- Each refresh (`refreshPage`) polls the host: running, the label is `UI_STR_EXPORT_TEXTURES_STOP`
  and the note `UI_STR_EXPORT_TEXTURES_PROGRESS` (two `%d`: done, total), written by
  `texExportNote` outside `setNote`'s cache, as `gfxNote`; else `UI_STR_EXTRAS_EXPORT_TEXTURES` and
  the note.
- The end (`texExportEnd`, from `ui_settings_vsync`, so on any screen, the menu left or a game
  started): pending and the state DONE, STOPPED or FAILED clears pending, logs `extras: export
  textures ended: state S, D of T, W written` and pushes `UI_STR_EXPORT_TEXTURES_DONE` with
  `_DONE_BODY` (`%d` the files written, `%s` the folder), `_STOPPED` with `_STOPPED_BODY` (`%d`
  written) or `_FAILED` with `_FAILED_BODY` (`%s` the folder).
- The bodies and the progress are filled by `fmtText`, which copies a table string's `%d` and
  `%s` in without handing the table to printf and falls back to the English string when a
  translation's conversions are not the expected ones (`strings_complete` already requires them
  equal).

ctest `settings_extra` (`testExportTextures`, a fake host), `settings_extras` (the rows hidden
without a host and on the pause menu).

## Strings and languages

Every word the port draws is a `UiStrId` into one table per language, `strings_en.c`, `fr`, `de`,
`it`, `es`, `ja`, `ko` and `zh`, `UI_LANG_COUNT` tables of `UI_STR_COUNT` UTF-8 entries written with designated
initialisers; the tables also hold the game's own menu words (`UI_STR_MT_*`) as each disc's sheets
letter them (`port/ui/strings.h`, header comment). `ui_str` answers in the current language and
falls back to English for a missing entry (`port/ui/strings.c` (`ui_str_in`)); ctest
`strings_complete` fails on a missing or empty entry, and on an entry whose printf conversions are
not the English one's, so the fallback never shows (`port/ui/test/strings_complete_test.c`). Every
table names the languages in their own words (English, Français, Deutsch, Italiano, Español,
日本語, 한국어, 繁體中文), so a player finds theirs whatever the menus are in.

The first five languages, `UI_LANG_PAL_COUNT`, are the European disc's, in the order of the game's
language numbers; Japanese (`UI_LANG_JA`), Korean (`UI_LANG_KO`) and Traditional Chinese (`UI_LANG_ZH`) are the
port's own text, the Japanese, Korean and Asian discs' words being their sheets. The current language is set by `ui_settings_apply_language`, which every screen of the
port calls before it draws its words (`settingsProc`, `ui_host.c`'s sync, photo mode, the model
viewer, the New Game and Quit screens, the pause menu's lines; also `select_disc`, since on
Android the UI is up before the disc is chosen): the language of the rule below
(`ui_set_language`) and the sheet language (`ui_set_sheet_language`: the disc's region row's
`menu_sheet`, `UI_SHEET_JA` on the Japanese disc, `UI_SHEET_KO` on the Korean one and `UI_SHEET_ZH` on the Asian one, else, -1,
the game's own language; `port/core/region.h` (`IcoRegionInfo`)) (`port/ui/settings.h` (`ui_settings_apply_language`);
`port/ui/ui_host.c` (`sync`); `port/platform/main_host.c` (`select_disc`)). The game's own
language is `NonLinearCameraMove`, 2..6 after the boot on the European disc (English, French,
German, Italian, Spanish), mapped by `ui_lang_from_game`; the Language row changes it, so the
menus follow at once while the menu language is Auto, and writes `[game] language`
(`port/config/sysconf.h` (`ico_sysconf_set_language`)). The Japanese disc has no game language:
the row is not shown there and the port never writes the word (`gameLanguage` is -1); `Main` sets it
from `ico_boot_language`, which gives 0 on that disc (`port/config/sysconf.c`), so it stays 0.

The menu language, `[ui] language` (`auto`, `en`, `fr`, `de`, `it`, `es`, `ja`, `ko` or `zh`;
`port/game/options.h` (`ico_opt_ui_language`)), is resolved by `ui_settings_resolve_language`: a
chosen language wins on every disc; `auto`, the default, follows the disc: the language its region
row names (`IcoRegionInfo.ui_lang_default`: `UI_LANG_JA` on the Japanese disc, `UI_LANG_KO` on the
Korean one, `UI_LANG_ZH` on the Asian one), else the game's own language on a disc whose game picks one
(`IcoRegionInfo.has_game_language`, the European disc, whose row names none, -1), else English.
The two region fields are plain numbers (`port/core` does not include `port/ui`'s headers);
`strings_complete` holds every row's to the `UiLang` and sheet ids. The host's system language is not consulted
(`port/ui/ui_lang.h`; the rule over its inputs is `ui_lang_resolve`, which `strings_complete`
tests).

The sheet language is kept apart from the port text's: the language of the sheets the game's
menus show, one of the European disc's five, `UI_SHEET_JA` (the Japanese disc's sheets) or
`UI_SHEET_KO` (the Korean disc's) or `UI_SHEET_ZH` (the Asian disc's; `UI_LANG_SHEET_COUNT` in all, 8; `UI_SHEET_JA_TITLE`, the Japanese title's geometry, is 8 too, not a sheet language). The look of the port's text in the sheets' style and the game's
menu words follow it whatever the port's text is in; until `ui_set_sheet_language` is called it
follows the port's language while that is a European one, else English (`port/ui/strings.h`
(`ui_set_sheet_language`, `ui_sheet_language`)).

Four strings name the running disc's pack folder, its PCSX2 serial (SCES-50760, SCPS-11003): the
texture and model pack notes and the two dump notes. Their entries are printf formats with one
`%s`; `ui_str_serial` writes one in the current language with the selected region's
`pack_serial` (`port/core/region.h`), and `ui_str_takes_serial` names the four
(`port/ui/strings.c`); the Settings notes are written through it (`port/ui/settings.c`
(`noteText`, `setNote`)).

The Japanese text has no spaces to break at. The notes' wrap (`port/ui/settings.c` (`wrapText`))
and a popup body wider than the panel (`port/ui/popup.c` (`wrapWide`)) also break between two
characters where `ui_break_before` allows: a wide character on either side (`ui_wide_char`: the CJK
blocks and the fullwidth forms), never before closing punctuation, the prolonged sound mark, the
middle dot, the ellipsis, a sound or iteration mark or a small kana, never after an opening
bracket (`port/ui/strings.h` (`ui_break_before`)). Latin text breaks at its spaces only, as
before: two Latin characters never give a break, so the European languages' lines are unchanged.

Korean is written with spaces between its words and breaks at them as Latin text does: Hangul
(`ui_hangul_char`: the jamo, the compatibility and extended jamo, the syllables) is not a wide
character, so `ui_break_before` gives no place between two Hangul characters. A Korean word alone
wider than the line breaks between two of its syllables, the only place the wrappers break inside
a word (`wrapText`'s and `wrapWide`'s `lastInWord`); a popup body, whose Latin lines are kept as
written, breaks at a space after a Hangul character, the space becoming the line break
(`port/ui/popup.c` (`wrapWide`)). The achievements' popup wrap counts a Hangul character two
columns, as a wide one (`port/game/achievements.c` (`columns`)). `strings_complete` holds the
rules; `ui_test` holds a Korean popup body and a 40-syllable word to the panel's width.

The Japanese table (`strings_ja.c`) takes the Japanese disc's own word wherever its menu sheets
letter one for the same thing (`Tex_menu01/option.tm2`, `title.tm2`, `mc01.tm2`, `mc02.tm2`,
`mc04.tm2`): はじめから and つづきから, オプション, もどる, キーコンフィグ, 手つなぎ設定,
バイブレーション, ノイズレベル (the film effect, photo mode's too), 明るさ調整, プレイ人数,
こうげき, おりる, てをつなぐ／よぶ, 決定, ゲーム終了, and the memory card messages' wording; the
port's rows for the same settings use the same words, and every "back" a button or prompt names
is もどる. Otherwise it is a console menu's register: nouns for rows, the katakana loanwords
Japanese games use, the sticks as 左スティック and 右スティック, the PlayStation buttons as
○ボタン, ×ボタン, □ボタン and △ボタン in sentences and the bare marks on buttons and prompts,
and full-width brackets with no space before them. A popup title is one line, so the titles are
short. The disc's save screen names no places (it shows the stage number), so the area names
(`UI_STR_MT_LOC_*`) are the port's own.

The Korean table (`strings_ko.c`) does the same with the Korean disc's sheets, which are lettered
in Hangul (the same `Tex_menu01` members, decoded from SCPS-56001's archive): 새로 시작하기 and
불러오기 (title), 옵션 and 게임으로 (the pause menu's back), 게임으로 돌아감 and 게임을 종료함,
조작 설정 (the button configuration), 잡기 설정 (the hold type), 진동 기능 with 켜기 and 끄기,
노이즈레벨, 음향, 스테레오 and 모노, 밝기 조정, 플레이어 수 with 1명 and 2명, 점프, 공격, 액션, 놓기,
손 잡기/부르기, 줌, 기본설정으로, 결정 (confirm), 네 and 아니오, the memory card messages with
their line breaks (메모리 카드(PS2)가 / 꽂혀 있지 않습니다, 저장하시겠습니까?, 액세스 중) and the
stage sheet's place names (낡은 다리, 광차 1, 정문, 서쪽 우상의 계단, 모래사장). Its own rows are
in the polite register the sheets use (…하시겠습니까?, …해 주십시오, 합니다 in the notes), the
achievements' descriptions as goals in the plain present (…에 도착한다.), the
PlayStation buttons the bare marks as in the Japanese table, the sticks 왼쪽 스틱 and 오른쪽
스틱, and ASCII brackets and colons with the spacing Korean takes. Every "back" a button or
prompt names is 돌아가기. A placeholder takes a noun after it (`%s 폴더를`), so no particle
depends on the serial's last sound.

The Chinese table (`strings_zh.c`, Traditional Chinese as written in Taiwan) does the same with
the Asian disc's sheets (`Tex_menu01`, decoded from SCPS-55001's archive; the numerals, SLOT 1 and
2, L1 to R2 and the button glyphs are kept as on the Japanese sheets, and a Japanese よろしいですか?
is left on the memory card sheet, which the port does not draw): 選項 (Options), 返回 (back),
按鈕設定 (the button configuration), 牽手設定 (the hold type), 聲音, 立體聲 and 單聲道, 調整明暗度
with 用方向按鈕調整明暗度, 跳躍, 攻擊, 行動, 手放開, 牽手 / 呼喚, 焦點 (zoom), 回到預設, 暗淡 and 明亮,
決定 (confirm), 是 and 否, 震動功能 with 開 and 關, 重新開始 and 途中開始 (the title), 遊玩人數 with 1人
and 2人, 回到遊戲 and 遊戲終了 (the pause menu's other rows), the memory card messages with their line
breaks (記憶卡(PS2專用) / 並未插在主機上, 您要保存嗎?, 存取中, 資料保存失敗) and the stage sheet's place
names (舊橋, 臺車 1, 正門, 西方偶像階梯, 沙灘). Its own rows are in the plain formal register the
sheets use (您…嗎?, 請…), the achievements' descriptions as goals (抵達…。), the PlayStation buttons
the bare marks as in the Japanese table, and ASCII brackets for the disc's own words but fullwidth
punctuation (，。？！：) in the port's sentences, which break between any two ideographs and never
before the closing marks, as the Japanese text does (`ui_break_before`). Ico and Yorda stay in
Latin letters, as the sheets never spell them. The language is named 繁體中文 in every table.

## Text: the font and the sheet look

### The font

The port's text is Arimo Regular (SIL OFL 1.1), embedded in the program: `embed_font.cmake` turns
`port/assets/fonts/Arimo-Regular.ttf` into `ui_font_ttf[]` at build time, so no file sits beside
the program (`port/ui/embed_font.cmake`; `port/ui/CMakeLists.txt`). The font is a subset; the
code points it holds are listed on the `subset:` line of `embed_font.cmake`, which
`tools/font_audit.py` checks the port's text against.

A character Arimo has no glyph for is drawn from a second embedded font, a subset of Noto Sans JP
Regular (SIL OFL 1.1; `port/assets/fonts/NotoSansJP-Regular-subset.ttf` into `ui_font_ja_ttf[]`,
142,792 bytes, 752 code points): the Japanese table, the language names and the PlayStation
button marks □ △ ○ (`port/ui/font.c` (`fontGlyph`)). Its metrics are scaled to Arimo's em (`jaK`),
a pair across the two fonts has no kerning, and every line keeps Arimo's ascent and line height,
so Latin text draws as before; the sheet rasteriser (`ui__sheet_raster_line`, the menus' look)
takes the same glyphs. A character neither has (Hangul) is drawn from a third embedded font, a
subset of Noto Sans KR Regular (SIL OFL 1.1; `port/assets/fonts/NotoSansKR-Regular-subset.ttf`
into `ui_font_ko_ttf[]`, 84,372 bytes, 655 code points: the Hangul of every table, 한국어 being
in all of them, and the Korean table's other characters outside Arimo), scaled to Arimo's em in
the same way (`koK`; `port/ui/font.c` (`fontGlyph`)). A fourth, a subset of Noto Sans TC Regular
(SIL OFL 1.1; `port/assets/fonts/NotoSansTC-Regular-subset.ttf` into `ui_font_zh_ttf[]`, 219,264
bytes, 893 code points: the Chinese table's and corpus' characters outside Arimo, Hangul left to
Noto Sans KR), holds the ideographs of the Chinese table (`zhK`). The order is Arimo, Noto Sans JP,
Noto Sans KR, Noto Sans TC, so a character two fonts hold (the button marks, the CJK punctuation)
comes from the same font whatever the language, and the Japanese and Korean text draw exactly as
before, except that while the port's language is Chinese (`ui_settings_apply_language` calls
`ui__font_prefer_chinese`) Noto Sans TC comes right after Arimo: a code point both Chinese and
Japanese text use (返, 選) is then drawn in its Traditional form, not the Japanese font's. The
glyph cache keeps both (its key carries the face order's bit), and the menus' strips are hashed
with it, so changing the Menu language between Japanese and Chinese draws each anew. The language
name 繁體中文 every table holds is in the Japanese subset too (`subset_font_ja.py` reads every table
but the Chinese one), so it is drawn from one font in every language. The fonts
are for the port's own text only: every word of the game's is the disc's own picture, on every
disc.

The subset pipeline: `tools/fetch_deps.sh` (section 7, `SKIP_NOTO_JP=1` skips it) fetches the
variable `NotoSansJP[wght].ttf` of `google/fonts` at a pinned commit into
`tools/toolchain/deps/noto-sans-jp/`, SHA-256 checked; `tools/subset_font_ja.py` makes the subset
from it with fontTools 4.63.0: the wght 400 instance, ASCII, the CJK punctuation, hiragana and
katakana, the fullwidth forms, the button marks and every character of the string tables, the
model viewer's names and the font corpus outside Arimo's subset, hinting and the layout tables
dropped, the source's timestamps kept so the same input makes the same bytes. It writes the
`subset-ja:` line of `embed_font.cmake`, which `font_audit.py` checks together with the `subset:`
line. After a change to a table, `tools/subset_font_ja.py --check` (ctest `font_subset_ja`) fails
until the tool is run again: it compares the recorded line with the characters the tables use,
the committed font's cmap with the line, and, with the download, the committed font with the
tool's output byte for byte. The build reads only the committed subset, never the download. The
Japanese subset leaves Hangul out (`tools/font_audit.py` (`HANGUL`)), so the Korean table does not
change it.

The Korean subset is made the same way: `tools/fetch_deps.sh` (section 8, `SKIP_NOTO_KR=1` skips
it) fetches the variable `NotoSansKR[wght].ttf` of `google/fonts` at a pinned commit into
`tools/toolchain/deps/noto-sans-kr/`; `tools/subset_font_ko.py` makes the wght 400 subset of
ASCII, the CJK punctuation, the fullwidth forms, the button marks, every Hangul character of the
tables, the model viewer's names and the corpus, and every other character of the Korean table and
`font_corpus/ko.txt` outside Arimo's subset, and writes the `subset-ko:` line of
`embed_font.cmake`; `--check` (ctest `font_subset_ko`) makes the same three comparisons.

The Chinese subset likewise: `tools/fetch_deps.sh` (section 8b, `SKIP_NOTO_TC=1` skips it) fetches
the variable `NotoSansTC[wght].ttf` of `google/fonts` at a pinned commit into
`tools/toolchain/deps/noto-sans-tc/`; `tools/subset_font_zh.py` makes the wght 400 subset of ASCII,
the CJK punctuation, the fullwidth forms, the button marks, and every character of the Chinese
table and `font_corpus/zh.txt` outside Arimo's subset but Hangul, and writes the `subset-zh:` line
of `embed_font.cmake`; `--check` (ctest `font_subset_zh`) makes the same three comparisons.

`font.c` rasterises it with stb_truetype into 8-bit coverage atlases, one set per pixel size, built
on demand; up to 32 size sets live at once, the one drawn least recently going once 4
[rd](ARCHITECTURE.md#terms) frames have closed since its last draw (`port/ui/font.c` (`MAX_SIZES`,
`EVICT_FRAMES`, `PAGE_MIN`, `PAGE_MAX`)). `ui_draw_text` records [GS](ARCHITECTURE.md#terms) sprites
into the current rd [list](ARCHITECTURE.md#terms) with the standard sprite blend; `ui_measure_text`
and `ui_font_metrics` measure in grid units (`port/ui/font.h`). Built without `ICO_RD` (the headless
build) the draw calls only measure.

Between `ui_begin_overlay` and `ui_end_overlay`, inside an rd overlay callback, the same calls
draw on the output at its resolution instead: the grid maps onto the 4:3 part of the presentation
box, text is rasterised at the output's pixel size and drawn texel for pixel, and nothing is
mirrored (`port/ui/font.h` (`ui_begin_overlay`)). Popups, photo mode's panel and the touch overlay's
words use it.

### The menus' look

Every word a menu shows, the game's own and the port's, is drawn in the look of the game's
pre-rendered menu sheets (`port/ui/menu_font.h`, header comment). A sheet texel is one x unit wide
and one field line tall; the menu rows' em is 13.5 texels, 27 y units. A text is rasterised on
that grid, its lettering 0.8 as wide as the typeface (`UI_SHEET_WIDTH`, the sheets having been
lettered for the GS's 512-wide frame) and a little heavier (`UI_MENU_BOLD_X`, `UI_MENU_BOLD_Y`),
then drawn with rd's sheet [shader](SHADERS.md): a rim around the letters and the antialiasing
quantised against a fixed dither, as the sheets read (`port/ui/menu_font.h`;
`port/ui/ui_internal.h` (`UI_SHEET_WIDTH`, `UI_MENU_BOLD_X`)).

The strips are rasterised at the [scene scale](ARCHITECTURE.md#terms), the scene's texels per GS
line rounded to whole strip texels per sheet texel, from 1 up to `ICO_SHEET_SCALE_MAX` (on the
overlay, the output's pixels per y unit), so the letters are sharp at every resolution while the
quad and the look stay the 1x strip's (`port/ui/menu_font.c` (`mfScale`); `port/ui/menu_font.h`
(`ui_draw_menu_text`)). Each drawn text is one strip in a page cache of four pages per style for
three styles, a page 1024 x 1024 at 1x and up to 4096 x 2048 at 4x (`port/ui/menu_font.c`
(`MF_PAGE`, `MF_PAGES`)). A page is filled in shelves, bands of rows packed left to right, each
holding its rows, its cursor, its strip count and the rd frame number at which one of its strips
was last drawn or placed (`MfShelf`, at most `MF_SHELVES` 128 a page; every cache hit marks the
strip's shelf). A new strip goes, in order, on the open shelf (the last one, which grows down to
its tallest strip), in a band an emptied shelf left (a strip no taller than the band), or on a new
shelf below the last (`shelfFit`). When no page of the strip's scale has room, every shelf of those
pages with strips that no frame has drawn for `MF_EVICT_FRAMES` (4) frames is emptied
(`reclaimStale`, `reclaimShelf`): its strips are forgotten, its coverage rows and, above 1x, its rim
rows (the band and one sheet texel above and below, which `placeCoverage` writes; the 5-texel
`MF_GAP_Y` keeps them clear of the neighbouring shelves') are cleared across the page, and empty
shelves at the bottom are dropped. The texture is kept. At 1x the cleared rows are replaced in it
with `rd_update_texture_rect` (about 30 KB: there is no rim plane, and the 1x shader draws the rim
from the coverage 6 and 4 texels round a strip, so the band must read empty). Above 1x the texture
is not touched by the reclaim: rd keeps one rectangle per texture to upload, and the coverage band
and the rim band, `p->h` rows apart, would make it most of the page (about 2.6 MB at 3x, the
whole texture when two shelves are emptied at once). No quad samples a reclaimed band, and
`placeCoverage` uploads each new strip's coverage and rim over the strip and the one sheet texel
round it (`[x - scale, x + w + scale) x [y - scale, y + h + scale)`), the border its bilinear read
takes, so the cleared texels a new strip needs reach the texture with it; ctest `menu_text`
checks those texels against the coverage on every frame of the 3x counter stream and that a frame
that empties shelves uploads under a quarter of the texture. Four frames is the
whole-page rule's margin: `RD_FRAME_RING` keeps three frames, so none can still name a cleared
texel. Only after that does the cache open a new page, then drop the page drawn least recently once
no recent frame can name it. A full strip table (`MF_STRIPS`, 1024) empties the stale shelves of
every style and scale before it drops a page. A screen with a string that changes every tick (the
Models viewer's "Frame N / M", issue 136) so keeps its steady rows in place and recycles the
counter's bands, where a whole-page rule alone finds every page drawn each frame and draws every
new word with the plain font once the pages are full. A word still falls back to the plain font only when every
shelf of every page of its scale holds a strip drawn in the last four frames; the log counts those
(`ui: menu text: N words drawn with the plain font so far (S strips cached, M shelves reclaimed)`)
and the reclaims (`ui: menu text: M shelves reclaimed so far (S strips cached, N words drawn with
the plain font)`), each at its first, tenth, hundredth and every further power of ten, beside the
once-a-run `ui: the menu text pages are full` (`ui_menu_font_fallbacks`,
`ui_menu_font_shelf_reclaims`; ctest `menu_text` draws the counter for 600 frames at 3x and 1x).

The inks (`UiMenuTextInk`): light letters with the dark rim (the menu rows; the port's text always
has the full rim), and dark, plain and grey letters without one (the white panel's prompts, the
save screens' play time and slot numbers). The light ink's rim level and fill are per sheet
language (`kSheetInk`, `ui_menu_sheet_ink`: the European disc's five measured on their sheets by
`menu_look`, and `UI_SHEET_JA`, the Japanese disc's, measured by `menu_look_jpn` on the pause and
title words of `Tex_menu01`'s sheets: fill 251.5, so the level is 252, the letters' palette white
being fcfcfc on these sheets where the European ones have ffffff; rim 34.3, a black-grey near the
English sheets' 24; one antialiasing step where the European sheets have three, which the shared
`ICO_SHEET_LEVELS` covers; `{1, 34, 252, 1, 0}`; and `UI_SHEET_KO`, the Korean disc's,
`{1, 42, 253, 1, 0}`, measured by `menu_look_kor` on the disc's title, mc04 and option words: fill
252.8, rim 41.8 (the off-line estimate was 250.7 / 42.9); and `UI_SHEET_ZH`, the Asian disc's, the
Japanese numbers `{1, 34, 252, 1, 0}` until `menu_look_cn` has measured SCPS-55001), and a change restyles the
pages without rasterising again (`port/ui/menu_font.h` (`ui_menu_sheet_ink`, `ui_menu_item_ink`)).

### The Japanese, Korean and Chinese sheets' geometry

The European sheets' lettering is Arimo's as it is (em 1, width 1, `UI_MENU_BOLD_X`/`Y`); the
Japanese and Korean discs' is thinner than Noto Sans JP and KR at the menus' em and set
proportionally: each glyph advances by its outline's width plus a gap in texels
(`ui__sheet_set_kana_gap`). The port's text over those sheets takes a per-sheet geometry, the em
over the one asked for, the width over `UI_SHEET_WIDTH`, the gap and the weight
(`port/ui/menu_font.c` (`sheetGeo`, `JA_GEO_*`, `JA_TITLE_GEO_*`, `KO_GEO_*`)). The Japanese
title sheet spaces its words apart more than the others, so the Japanese disc has two
(`UI_SHEET_JA` em 0.98, width 0.86, gap 1.5, weight 0.15; `UI_SHEET_JA_TITLE` em 1.06, width
0.70, gap 6, weight 0), fitted by `menu_look_jpn` (`testGeometry`): every word's ink height within
a texel of the disc's row, its width within 10 % on the title and 17 % elsewhere.

The Korean disc's title words are lettered as its other sheets' (syllables 12 to 14 texels high,
11 to 15 wide, 5 to 7 apart, a space 11 to 15), so it has one geometry, `UI_SHEET_KO`: em 1.12,
width 0.62, gap 7.5, no extra weight. It was measured off line, without a build: the sheets
decoded from SCPS-56001's archive, each word's fill box taken as `testGeometry` takes it (grey
>= 200, alpha >= 0.7), and the port's box from a Python emulation of `ui__sheet_raster_line`,
`embolden` and `sheet_ref.c`'s 1x texel over Noto Sans KR's outlines, checked first on the
Japanese geometry (オプション -11 %, もどる +14 %, ズーム +13 %, はじめから +3 %, つづきから -3 %, as
`menu_look_jpn` reports); then the same search as `ICO_MENU_LOOK_JPN_FIT` over both sheets'
words together. Disc and port (width x height): 옵션 32 x 14 and 32 x 13, 게임으로 71 x 14 and
72 x 13, 조작 설정 81 x 13 and 78 x 13, 줌 12 x 13 and 12 x 13, 게임을 종료함 106 x 12 and
116 x 13, 새로 시작하기 116 x 13 and 118 x 14, 불러오기 72 x 14 and 71 x 14: every height within
a texel and every width within 10 %. `menu_look_kor` holds the numbers on the
disc.

The Asian disc's sheets (`UI_SHEET_ZH`, Traditional Chinese: 選項, 返回, 按鈕設定, 重新開始, 途中開始,
回到遊戲, 遊戲終了) have one geometry for every screen like the Korean disc's; its first numbers are
the Japanese sheets' (`ZH_GEO_*` = `JA_GEO_*`: em 0.98, width 0.86, gap 1.5, weight 0.15), not yet
measured: the sheets are decoded and the words read, but `menu_look_cn` has not run on SCPS-55001
(it needs the disc image; `ICO_MENU_LOOK_JPN_FIT=1` searches the numbers, as for the others), so
its geometry gate and `kSheetInk[UI_SHEET_ZH]` hold the Japanese numbers until its output replaces
them (`kInkExpect`, `ZH_GEO_*`, `kSheetInk`).

### The game's own menu words

`menu_text.c` is the table of the game's menu words: for each sheet rectangle that holds text, the
string and where the lettering sits, per language, as the five PAL sheets letter it: the em, the
anchor, the first line's capital middle, the line pitch, the width against the menus'
(`wx`), the extra letter spacing (`track`) and the rim, fitted item by item to each sheet
(`port/ui/menu_text.h` (`UiMenuTextItem`); `port/ui/menu_text.c`, header comment). The rows list
every `texProperty` row that draws each rectangle. The table applies to the European disc only
(`ui_menu_text_applies`): on the Japanese disc every menu word keeps its own texels, and only the
port's own rows over those sheets are drawn in the sheet look, in `kSheetInk[UI_SHEET_JA]`
(`menu_text_jpn` checks no Japanese row is drawn from the table). A row whose texel rectangle differs from the
measured one (tables that are not the PAL ones) keeps its texture; artwork, logos, the copyright,
button glyphs, arrows, the shoulder labels and the subtitles are not in the table and keep their
texels (`ui_menu_text_item_of`). `ui_menu_word_draw` draws a row's words where its sprite would be,
with the sprite's own box and texel rectangle, so insets and half-texel offsets land as the sheet's
texels would.

Fit per language: the table's per-language em, anchor and width carry the differences between the
five sheets; a port label wider than its box shrinks to fit down to 60 %
([How the game draws a port row](#how-the-game-draws-a-port-row)); a prompt line shrinks as a
whole ([Hints](#hints)); the photo panel shrinks its size until the widest line fits
(`port/ui/photo_ui.h` (`ui__photo_hud_fit`)).

## Popups

`ui_popup_push` queues a title and body (eight at most); one shows at a time, sliding in from the
right edge of the 4:3 picture for 0.3 s, held for 4 s, sliding out for 0.3 s, counted in
[vsyncs](ARCHITECTURE.md#terms) by `ui_popup_vsync` at the rate the window host passes before each
call, `ico_host_vsync_hz()` (15, 200 and 15 at 50 Hz; 18, 240 and 18 at 60 Hz, the Japanese disc's
and the European disc's 60 Hz mode; `port/ui/popup.h` (`UI_POPUP_QUEUE`, `ui_popup_set_vsync_hz`,
`ui_popup_slide_vsyncs`, `ui_popup_hold_vsyncs`), 50 Hz by default in the tests). Achievements, photo mode, the model viewer, the Graphics driver page and
Extras > Add to Steam and Export textures push them (`port/ui/settings.c` (`steamAdd`,
`texExportEnd`)); `ui_popup_title` and `ui_popup_body` give the one showing (tests). They are drawn on the
presentation overlay (`rd_set_present_overlay`) at every [present](ARCHITECTURE.md#terms), after the
box blit and at the output's resolution: outside the game's frame, so a frame the GS keeps never
shows one twice, and never mirrored (`port/ui/popup.h`, header comment; `port/ui/ui_host.c`
(`hostOverlay`)). The panel's text uses the menus' look with the full rim and room for it
(`port/ui/popup.c` (`PAD_Y`)). A Japanese body wider than the panel takes line breaks
([Strings and languages](#strings-and-languages)); a title is one line. `[dev] popup_test` queues a test popup from Main tick 100 and every
150 ticks after (`ui_popup_dev_tick`).

## Hints and lists

### Hints

A `UiHint` is a line of button prompts: each item one or two of the game's glyphs followed by a
word, or a word alone, centred across the screen; the words are port rows, the glyphs glyph rows.
`ui_hint_layout` measures the words in the current language and places the items, setting a line
wider than 600 units smaller, glyphs and words alike, down to 60 % (`port/ui/ui_hint.h`
(`UI_HINT_WIDTH`, `ui_hint_layout`)). The music gallery's transport, the Characters viewer's prompts
and the model viewer's lines use it.

### Lists

`ui_list` is a window of eight slots over a list of N items, for the Achievements, Remap and Music
pages. A page describes its data with `UiListDef`: `count`, `fill` (a slot's label and up to two
columns, literal or string id), and optionally `heading` (shown, never selected), `input` (a press
on an item) and `decorate` (the page's own rows) (`port/ui/ui_list.h` (`UI_LIST_SLOTS`,
`UiListDef`)). The cursor moves on the slots through their item links; at the first or last slot
the list scrolls, wrapping at the ends as the Options screen's rows do, and a cursor landing on a
heading moves on in the direction it came from. The rows are port rows made with
`ui_settings_add_row` (`port/ui/ui_list.h`, header comment).

## The touch overlay

`touch_ui.c` draws the touch controls of `port/input/touch.h` on the output from the copy the
device layer takes once a vsync: each button a translucent disc or key, the D-pad one cross, the
game's own button pictures on them while their sheet is loaded (sprites in output pixels from the
game's textures, fitted to the shape), words on Select, Start and any zone whose sheet is not
loaded yet, the left stick's ring, run ring and knob, the look pad's ring and dot, a held button
brighter (`port/ui/touch_ui.h`, header comment). It draws only while the copy's opacity is above
0. It is on the presenter's top layer (`rd_set_present_overlay_top`), never through the CRT
filter (`port/ui/ui_host.c` (`hostOverlayTop`)). [INPUT.md](INPUT.md) has the touch input.

### The Frame rate counter

`ui_fps.c` draws Options > Display > Frame rate counter (`[video] show_fps`, default off, an On/Off
row stepped like Vsync): one `ui_draw_text` per present on the same top layer, called from
`hostOverlayTop` after the touch controls when `ico_video_show_fps()` is set. The text is
"<n> <fps unit>" (`UI_STR_FPS_UNIT`, the Frame rate row's unit), white (255, 255, 255 at
alpha 235), no background, 3.5 % of the output's height (at least 18 pixels), left and top aligned
a margin of 1.2 % of the output's height (at least 6) in from the corner of the output (the whole
screen, not the 4:3 picture: `pxToGrid` maps an output pixel to the font's grid, which reaches
outside 0..640 on a wide screen). The number is made by `port/platform/fps_counter.c` (free of SDL
and rd, `fps_counter_test`): the presents over the last second or more, the figure the 10 s line
gives as "presents/s", 0 until the first second (nothing is drawn while it is 0). `window_host.c`
only calls `ui_fps_tick(now)` at the top of `pace_log` and `ui_fps_present()` after a present
(`window_host.c` is within a few hundred bytes of the 256 KiB per-file gate: the rest lives here and in
`fps_counter.c`); `ui_fps_tick` passes the renderer's replay count (`rd_replay_time_max(0, ...)`,
which the 10 s line zeroes: a smaller count than the last is a zeroing) and `rd_interpolation_active()`.
With interpolation on the counter counts the window's presents, with it off the replays, as the 10 s
line does. The counting runs whether or not the option is on. While a touch layout exists
(`ui_touch_top_gap`) the corner is Left 2's, so the text starts 35 q in from the layout's left edge
(just right of the D-pad's Up key at 33 q) at the keys' row (3 q down) and is shrunk by 8 % steps (to
12 pixels at least) until it fits before Select's left less 2 q (`touch.c` header: the default
places). A player's edited layout can put a control there; the counter does not look. The log says
"window: frame rate counter on" or "off" when the option changes (and "on" at the start when it is
on).

## The touch layout editor

Options > Controls > Touch screen is `UI_PAGE_TOUCH` (parent Controls), nine rows. Touch controls,
Touch size and Touch opacity keep the values and steps they had on the Controls page. Snap to
grid (On or Off) and Grid size (2, 4 or 8 mm, `UI_STR_VAL_MM`) step the live edits (`snap`,
`grid`, `gen`) and set the page's dirty flag; Reset layout (`UI_OPT_TOUCH_RESET`) clears the
posture the host reports (`ico_touch_edits_reset`), Reset all layouts the three; both play the
confirm sound and log `touch: reset layout (...)`. Edit layout (`UI_OPT_TOUCH_EDIT`) is locked
and dimmed with a note (`UI_STR_TOUCH_EDIT_LOCKED`) while Touch controls is Off or no editor host
is set (`port/ui/settings.c` (`touchEditLocked`, `refreshPage`)). The Controls page has 14 rows
with the link; `pagePitch` spaces 10 rows 16 apart, 11 or 12 rows 13 apart from line 40 and 13 or
14 rows 11 apart from line 30.

Edit layout opens `UI_PAGE_TOUCH_LAYOUT`, a page of one masked row with no cursor, built as photo
mode's is (`buildTouchEditorPage`). `settingsProc` hands it to `ui_touch_edit_proc` each tick
(`port/ui/touch_edit_ui.c`): on the first call the host's `begin`, then the editor's tick with the
host's preview layout and the menus' pad word mapped to `ICO_PAD_*` (the left stick arrives as
D-pad bits; a button held from the press that opened the page is ignored until let go). Done,
or a back button other than Cross (Cross is the editor's Opacity +), calls `end(1)`, marks the
page dirty and returns to Touch screen with the cursor on Edit layout. `ui_settings_save` then
writes the edits and reads them back (`ico_input_write_touch_edits`,
`ico_input_reload_touch_edits`); `ui_settings_save_on_quit` counts them. The page logs
`touch: edit layout: begin (<posture>, N controls edited)` and `... done (...)`.

The host (`UiTouchEditHost`: `editor`, `begin`, `end`, `rule`, `layout`; `port/ui/settings.h`) is
installed by the window host; without it the row is locked.

`touch_ui.c` draws every control at its layout alpha and skips a Hidden or Off one (alpha 0).
While the snapshot's `edit` view is active it draws at opacity 1, floors every control at 0.35,
draws Hidden ones dashed with the word Hidden and Off ones (`active` 0) dashed at half strength
(`OFF_DIM`) with the word Off (`UI_STR_TED_OFF`), the D-pad key by key with its word across its
middle row and the stick's ring with its word, outlines the look pad's area, hatches the hinge band, dots the grid
from the anchor edges when snap is on, carried out to the bounds' edges (the window's, so the
dots reach under a camera hole) and skipping the band carried to them (so the dots are where the
drops land), outlines the
selection with its name and the overlapping controls in amber (`kWarn`), and draws the toolbar
strip along the top: each cell one line of words shrunk to fit ("Snap: On", "Grid: 4 mm",
"Opacity -"), the keys framed, the two readouts on a fainter backing without a frame ("Size 120
%" and "75 %" for the selected control, Hidden or Off at 0, "All" with All lit, "-" with nothing
selected); the opacity keys and the readouts dim with no selection and not All, Undo with
nothing to undo. While Reset asks, its three choices lie over Done to All with the cursor key lit
and the rest of the strip dims. The hint line or the refused or overlap note is drawn in the
view's `hint` box at the bottom, shrunk to its width, and the selection's name goes under the
control when the strip leaves no room over it; all at the rectangles the view carries (`port/ui/touch_ui.c`; `port/input/touch.h` (`IcoTouchEditView`)).
The words are the port's own (`UI_STR_TED_*`), in the seven languages.

## The mouse in menus

`ui_mouse_tick` runs once a Main tick after the pad is read and before the layouts, from the
game's `Main` (`port/ui/ui_mouse.h`; `ico2/common/src/main.c` (`Main`)). Over any layout the game
runs with a cursor (the port's pages and lists, the title's and pause menu's rows, the memory card
and save screens): pointing at a row moves the cursor onto it as the pad does, with its sound; a
click is the disc's confirm on a row (Cross, Circle on the Japanese disc), Left or Right on a value's arrows, Right on a stepped value; the wheel is
Up or Down, up to three notches queued; a key or pad press hides the pointer until the mouse moves
again. Nothing happens while a layout fades, while the procs have the item select off (the title's
card check, a list's wrap) or while the remap screen waits for a press; Play (layout 54) and its
scenes (55) are not menus (`port/ui/ui_mouse.h`, header comment; `port/ui/ui_mouse.c`). The
pointer is the system's.

The arithmetic is apart, with no game or renderer header, so its test runs alone: the output point
into the grid over the 2D layer's 4:3 box (`ui_mouse_to_grid`, the inverse of the overlay's map),
a row's box as `display_texture` computes it, arrow boxes grown by `UI_MOUSE_ARROW_PAD`, and the
hit test, smallest box first (`port/ui/ui_mouse_geom.h`). The view comes from the presenter's top
layer each present (`port/ui/ui_host.c` (`hostOverlayTop`, `ui_mouse_set_view`)). The game's side
supplies what the tables do not say: whether a row is shown (`lt_host_property_visible`), whether
the last tick had the item select off (`lt_host_select_disabled`), a cursor move without a pad
frame (`lt_host_point_item`), and whether a card screen's empty slot is pickable
(`la_host_mc_slot_pickable`: on the Save screen, not on Load) (`ico2/common/src/layout_texture.c`;
`ico2/common/src/layout_action.c`).

### Extras > Game variants

`UI_PAGE_VARIANTS`, a link on Extras after Characters (from the title and the pause menu: it is not
one of the three rows `isExtrasOpt` hides), its page built like Controls and Gameplay from the tables
`varOpts` and `varStrs` in `build`. Eight stepped rows, one `UI_OPT_VAR_*` per `IcoVariantId`
(`port/game/variants.h`; a `_Static_assert` keeps the order), then Debug menu (`UI_OPT_VAR_DEBUG_MENU`,
a press row after `UI_OPT_DEVELOPER` in the enum, so not stepped) and Back. The value text is
`variantText`; a step is `ico_variant_step`, which writes `[variants] <key>` in memory and logs
`variants: <key> = <value>`; the page's leaving saves the file as every page does.

- Values: Shadow toughness Original, American, Not tougher later (`no_plus`); Shadow aggression Their
  own, Bold patient, Bold quick, Wary patient, Wary quick; Queen barrier 5 hits, 7 hits (shown as the
  disc ships it until a step, which writes the number); the others Off, On.
- Each row's note (`UI_STR_OPT_VAR_*_NOTE`) is under the page while the cursor is on the row and ends
  with the rule: "Switches achievements off." for One hit per swing, Shadows never dodge, Ignore
  cutscene cameras and Debug menu (`ico_variants_suspend_achievements`, read by `gamestate.c`
  beside `ico_opt_developer_mode`), "Achievements stay on." for the others.
- Debug menu is shown only while Developer mode is on (`optShown`). Cross calls
  `ico_variants_debug_menu_pressed`, which logs `variants: the Debug menu is opened from Options`,
  marks the menu used (achievements off until `ico_variants_debug_menu_clear` or an options reload)
  and calls `ico_variants_open_debug_menu`, which opens the game's menu through the opener
  `debug_VariableInit` bound (`debug_MenuOpen`, [GAME_FEATURES.md](GAME_FEATURES.md#game-variants))
  and returns 1; Options then closes (`closeForDebugMenu`, what changed saved): from the title back
  to the title with the cursor on the Options row, from the pause menu into the game as the pause
  menu's Back does (`la_host_resume_play`, then the region's `layout_play`), logging `settings:
  Options closed for the Debug menu (to the title|back to the game)`. With nothing bound it logs
  `variants: the Debug menu cannot open: the game has not bound it` and Options stays.
- Pitch: nine rows 18 apart (Back at 184), ten with Debug menu 16 apart (`pagePitch`).
- Log lines: the values once at the first read (`variants: shade_toughness=... ignore_cutscene_cameras=0`),
  each change, the achievements rule when it changes (`variants: achievements are off (one_hit ...)`,
  `... are back on`), a value that is none (`variants: [variants] <key> = "<word>" is not a value; using
  <default>`).

ctests `settings` (the page's rows), `settings_extra`, `settings_extra_jpn`, `settings_extra_kor`
(`testVariantsPage`: the link, labels, values, a step and the key and the saved file on every row, the
notes and the achievements rule, the Debug menu row and its press: unbound it stays on the page,
bound to a fake opener it opens once and Options closes into the title or the game), `variants`,
`config`.

## Galleries and photo mode

### Extras > Music

The music gallery's list and engine are
[GAME_FEATURES.md](GAME_FEATURES.md#the-music-gallery)'s; the page is a `ui_list` page with a
progress bar of rectangle rows, the times and the transport's prompt line (`port/ui/settings.c`
(`buildGalleryBar`)).

### Photo mode

The pause menu's "Photo mode" row opens a port layout with one masked row, so the game draws
nothing of it; its proc hands the pad to `port/game/photo_mode.h` each Main tick, and Triangle,
Circle or Start go back to the pause menu (Cross for Circle on the Japanese disc, where Circle
saves a picture: `ico_photo_save_button`, `ico_photo_back_button`) (`port/ui/photo_ui.h`, header comment). The help panel
is drawn on the presentation overlay, never in the game's frame, so a capture never shows it.
Beside each action it shows the game's button picture (the glyphs' textures) or, for a button with
no picture, its name; once a key or the mouse was the last input, the bound keys' names in
brackets. `ui__photo_hud_build`, `ui__photo_hud_layout` and `ui__photo_hud_fit` make and place the
panel's items so a test can read them (`port/ui/photo_ui.h`). When Right cycles the Film effect
the proc queues a popup titled with the Options row's word whose body is
`ui__photo_film_word`'s: Off, the mode's number 1 to 4, or `UI_STR_PHOTO_FILM_DEJA` ("Deja vu",
the loanword as each language spells it) for mode 5 (`port/ui/photo_ui.c` (`photoProc`,
`ui__photo_film_word`)). [GAME_FEATURES.md](GAME_FEATURES.md) has the camera and the filters.

## The title

While a page opened from the title is up the title's logo is hidden
([GAME_FEATURES.md](GAME_FEATURES.md#the-title-logo)); Settings reports it with
`ui_settings_covers_title` (`port/ui/settings.h`).

Leaving Extras > Characters in the model viewer returns to the title, which runs unseen until its
card check has decided, then opens Settings on Extras: `ui_settings_title_return` arms it, masking
the title's rows and hiding the logo meanwhile (`port/ui/settings.h` (`ui_settings_title_return`,
`ui_settings_reopen_page`)).

## The Japanese and Korean discs

The port's UI names the game's menus by number, and each number is the running disc's, read from
the region table at run time (`port/core/region.h` (`IcoRegionInfo`, `ico_region_cur`);
[GAME_CODE.md](GAME_CODE.md#regions)): the layouts the Settings menu enters from and returns to,
the pause, title and memory card rows, the button glyphs, the stages, story flags and table sizes
of the gallery, the achievements, the credits, photo mode and the model viewer, the chain groups
the renderer lets down. The Japanese values, and what each was resolved against, are
`port/core/region_jpn.c`'s; the Korean disc's are `port/core/region_kor.c`'s. The Korean disc is
the Japanese program with Korean text, and every bullet below holds on it as well, with its own
row numbers (the title's copyright line is rows 12 and 13 there), its own sheets (`UI_SHEET_KO`)
and Korean in place of Japanese where a bullet names the language; the Korean disc has not been
run in the port yet, so those numbers are checked by the tests (`ui_kor`, `settings_kor` and the
other `_kor` rows of [TESTING.md](TESTING.md)) and by reading its tables, not by play. The Asian
disc (`region_cn.c`) is the Korean program on the Japanese disc's records: Circle confirms as there,
and its menu sheets are Traditional Chinese, so its Menu language starts on Chinese
(`ui_lang_default` `UI_LANG_ZH`) over its own sheets' look (`menu_sheet` `UI_SHEET_ZH`, the Japanese
numbers until measured); `ui_cn`, `settings_cn` and the other `_cn` rows run its numbers. The
North American disc, the August 2001 preview and the June 2001 demo (`region_ntsc.c`, `region_aug6.c`,
`region_jun26.c`) are PAL's text with their own lines: Cross confirms and Triangle goes back as on
the European disc, 60 Hz only and no game language (so the Video mode, Language rows are not shown and
the Menu language starts on English), with the English sheets' look the European one. Their layout, row
and stage numbers are their own, read from their rows, and none of the three has been run in the
port yet. The Model viewer's model ids (`mv_id_column`) are PAL's on these three until their
modelData columns are derived (`tools/gen_mv_region.py` aligns whole rows, and these discs' rows carry
two path strings PAL's do not). On the Japanese disc:

- **Buttons.** Circle confirms and Cross goes back in the game's menus (`pad_confirm` 0x20,
  `pad_back` 0x40). The port follows: `lt_ext_back_buttons` is Cross alone and `[game]
  circle_back` does not apply; the port's pages confirm with Circle and go back with Cross or
  Triangle (Triangle goes back on the port's pages on every disc); a mouse click on an item is
  Circle; photo mode saves with Circle and leaves with Cross, Triangle or Start; Escape (and
  Android's Back) presses Cross where it pressed Triangle (`port/ui/layout_ext.h`;
  `port/game/photo_mode.h` (`ico_photo_save_button`); `port/input/mouse_look.h`
  (`ico_escape_target`)). The prompt lines show Circle where they show Cross (`ui_hint_build`),
  and the remap page's hint is `UI_STR_REMAP_HINT_CIRCLE`. In the menus and photo mode the
  mouse's Cross and Circle buttons trade places, so the right button still goes back and photo
  mode's left button still saves (`port/input/pointer.h` (`ico_pointer_set_swap_face`), set by
  `port/ui/ui_mouse.c`; `port/input/input_sdl.c` (`swap_mouse_face`)). The keyboard and the
  gamepad keep their bindings: they press the same PlayStation buttons on every disc, so the
  gamepad's right face button (Circle) confirms and the bottom one (Cross) goes back, as on a
  Japanese PS2. The recorded pad scripts hold buttons, not meanings (`port/input/pad_script.h`).
  [DIVERGENCES.md](DIVERGENCES.md) GL-11 records the convention.
- **Rows.** The Circle goes back row, the 50 Hz Video mode row and the game's Language row are not
  shown (`lt_ext_has_circle_back`, `has_50hz_row`, `has_game_language`); the Menu language row is
  shown on every disc (`port/ui/settings.c` (`optShown`, `ui_settings_opt_shown`)). On the 2001
  discs Film effect is not shown either (`has_film_effect`; Players and Hold type are, Hold type
  starting on the disc's own B), and on
  the June 2001 demo Extras > Credits (`credits_staff_packs`); every disc's rows are
  [GAME_FEATURES.md](GAME_FEATURES.md#options-on-each-disc)'s matrix.
- **Disc.** Main's Disc row, after Menu language, shows on every disc when more than one disc's game data
  is there: the window host passes `ico_host_disc_archive_present` (`port/platform/host_config.h`:
  each region's `archive_name` in the per-user folder or beside the program, the same test
  `main_host.c`'s `mount_game_data` makes for `ico_host_choose_disc`) to
  `ui_settings_set_disc_handler`, asked once after each call. Its values, `UI_STR_DISC_PAL`,
  `UI_STR_DISC_JPN`, `UI_STR_DISC_KOR`, `UI_STR_DISC_NTSC`, `UI_STR_DISC_AUG6`, `UI_STR_DISC_JUN26`
  and `UI_STR_DISC_CN` ("Europe (SCES-50760)", "Japan (SCPS-11003)", "Korea (SCPS-56001)", "USA
  (SCUS-97113)", "Preview 2001-08", "Demo 2001-06", "Asia (SCPS-55001)" in English; the
  four newest are the same Latin text in every language, "Asia" translated where it differs), step through
  `config/regions.txt`'s rows and write `[game] disc` (`ico_config_disc_pin`), read at the next
  start; with no pin the row shows the disc playing, which with several discs' data and no pin is the
  one the next start picks (the disc played last). Its note is `UI_STR_DISC_NOTE` ("Takes effect at
  the next start"). With every Main row shown (the Graphics and Graphics driver links, Language, Disc and the
  two dump rows) the page has sixteen rows, 10 lines apart (`pagePitch`); with Save a picture for a
  bug report too, seventeen, 10 apart from line 26 (`MAX_ROWS` is 17, 18 in an rc build, whose
  eighteen rows are 9 apart: not rendered or checked on a phone). ctest `settings_extra` (`testDiscRow`,
  `testReportPicture`).
- **Glyphs.** The button pictures are the Japanese sheet's (`Tex_menu01/option.tm2`): rows 155,
  321, 320 and 319 (Cross, Circle, Square, Triangle, 32 x 15 texels shown 30 high), 326, 323, 325
  and 324 (L1, R1, L2, R2, 64 x 20 shown 40 high) and the arrows 278 and 279 (20 x 20, 40 high),
  each at its row's own `dispH` (`IcoGlyphRow.disp_h`; `port/ui/layout_ext.c` reads
  `IcoRegionInfo.glyph`, whose order `_Static_assert` ties to `LtExtGlyph`). The touch overlay's
  buttons keep their physical labels and draw these pictures through the same calls.
- **Text.** The game's menu words keep their texels; the port's pages, notes, prompts and popups
  are in the menu language (Japanese, or Korean on the Korean disc, unless another is chosen) in the sheets' look with the
  Japanese sheets' ink, `kSheetInk[UI_SHEET_JA]` ([The menus' look](#the-menus-look)). The title's
  copyright line is two rows on this disc (18 and 19), placed together (`ROW_TITLE_COPYRIGHT2`).

## The game's layout hooks

The game code calls the menus at named places, each marked "PC port" in a comment
([CONVENTIONS.md](CONVENTIONS.md)):

- `ico2/common/src/layout_texture.c`: `LT_LAYOUT`, `LT_PROP` and `LT_BACK_BUTTONS` over the
  extension; `ui_settings_install` in `init_layout_texture`; the text hook in `display_texture`
  and `lt_glow_sprite`; the chained-row cursor (`curNo`); the full-frame items; the draw keys
  (`LT_HOST_KEY`); `lt_host_property_visible`, `lt_host_select_disabled` and
  `lt_host_point_item` for the mouse.
- `ico2/common/src/layout_action.c`: the title procs leave Cross and Start on the Settings entry
  rows to `default_item_select` and mask them with their own rows during the card check
  (`ui_settings_entry_item`, `ui_settings_title_mask`, `LA_HOST_NOT_SETTINGS_ROW`); `LA_BACK`;
  `la_host_new_game_screen` and `la_host_new_game_go` for the New Game screen; the slot hooks for
  mirror mode and the achievements (`ico_mirror_slot_saved`, `ico_ach_slot_saved` and their
  `_loaded` pair) and the Stereo/Mono row (`ico_opt_output_toggled`); `ui_settings_key_config_back`,
  which returns the game's button configuration screen to Settings > Controls; `la_host_leave`,
  what every proc does before switching layout; `la_host_film_effect`, the Film effect row's stage
  animations; `la_host_title_music_fade` and `la_host_end_game` for the model viewer;
  `la_host_mc_slot_pickable` for the mouse.
- `ico2/common/src/kanban.c`: the text hook for the boot screens' prompts.
- `ico2/common/src/main.c`: `ui_mouse_tick` and `ico_title_logo_update` in `Main`.

## The window build's wiring

`ui_host.c` is linked into `ico_pc` only, since it reads the game's globals. `ui_host_init`, after
rd starts, loads the font, installs the sync hook (the game's GS frame, the language, the UI scale
of the [preset](ARCHITECTURE.md#terms)) and the record hook (`gif_HostFlush`, so the GS register
decoder flushes before port text is recorded into the list), the two presentation overlays, the
popup test switch and the quit handler; `ui_host_vsync` runs the popup clock once a vsync
(`port/ui/ui_host.h`; `port/ui/ui_host.c`; `port/ui/ui_internal.h` (`ui__set_record_hook`,
`ui__set_sync_hook`)).

## Tests

Each is a ctest of `port/ui/CMakeLists.txt`; the Settings tests share a fixture:

- `ui` (`test/ui_test.c`): the embedded fonts, UTF-8 decoding, measuring, the string tables, the
  layout extension through the real `layout_texture.c`, a Japanese popup body's line break.
  `ui_jpn` (`ui_test jpn`): the glyph rows and rectangles, the table sizes and the pad convention
  on the Japanese disc.
- `strings_complete` (`test/strings_complete_test.c`): every table defines every id with the
  English entry's conversions, the serial notes on every disc, the sheet language, the menu
  language's rule, the Japanese line-break rules.
- `settings` (`test/settings_test.c`): the menu over fake PAL-shaped tables: pages and labels, the
  entry repoint, the title and pause rows' places, navigation through the real `layout_texture.c`,
  the game's settings on the pages, what is written.
- `settings_extras` (`test/settings_extras_test.c`): the Extras page from the title and the pause
  menu, Credits, Models, Characters with a faked viewer, the pages that hide the logo.
- `settings_extra` (`test/settings_extra_test.c`): further Settings tests, one function per
  feature, kept in their own file under the source size cap of `tools/check_no_rom.sh`; new
  Settings tests go here (`port/ui/CMakeLists.txt`).
- `settings_render` (`test/settings_photo_test.c` with `SETTINGS_RENDER`): every screen through rd
  on a Vulkan device, written as PNGs, the words as sheet strips in the scene list, photo mode's
  panel in every language.
- `settings_jpn`, `settings_extras_jpn`, `settings_extra_jpn`, `settings_render_jpn` (each program
  with `jpn`): the Japanese disc over the fixture's tables at its numbers (`fxLayout`, `fxRow`):
  the rows it has not and the Menu language row, Circle confirming and Cross or Triangle going
  back, the title's rows and the copyright's halves placed, the notes naming SCPS-11003, Extras,
  the mouse (a click is Circle; the buttons traded in menus only), the prompts' Circle, the menu
  language Japanese by default (`settings_title_jpn.png`, `settings_main_jpn.png`), and the
  boot's hand-over to the title with Skip the opening (`testSkipOpeningTitle`, which the European
  run makes too: the boot's layout to the scene layout, the title's switch only once that layout
  has faded in, `lt_host_boot_settled`).
- `menu_text` (`test/menu_text_test.c`): the menu text table against the boot ELF's rows, every
  string drawable, the hook through the real `layout_texture.c`.
- `menu_look` (`test/menu_look_test.c`, `test/menu_sheets.c`): the menus' strips against the
  player's sheets through the sheet shader's CPU reference; pins the sheet constants.
- `menu_text_jpn`, `menu_look_jpn`, `menu_text_kor`, `menu_look_kor`, `menu_text_cn`,
  `menu_look_cn` (`test/menu_region_test.c` with a slug, and `test/menu_sheets.c`): over the disc's
  boot ELF's tables (`ICO_BASE_ELF_JPN`, `ICO_BASE_ELF_KOR`, `ICO_BASE_ELF_CN`), the region's layouts by their procedures, the title, pause and memory card
  rows, the glyph rows' rectangles and heights, no row drawn from the menu text table; with the
  disc's image (`ICO_DISC_IMAGE_JPN`, `ICO_DISC_IMAGE_KOR`, `ICO_DISC_IMAGE_CN`) each glyph
  rectangle's ink, the pause and title words' fill and rim, `kSheetInk[UI_SHEET_JA]`,
  `kSheetInk[UI_SHEET_KO]` or `kSheetInk[UI_SHEET_ZH]` held to
  them, and the port's words' box against the disc's rows. 77 without them.
- `font_coverage` (`test/font_coverage_test.c`, `test/font_corpus/`): every character the port
  draws, in the eight languages, has a glyph in one of the four fonts; a Japanese character is
  Noto Sans JP's own glyph, a Hangul one Noto Sans KR's, a Chinese-only ideograph Noto Sans TC's,
  and the face order follows the language (`ui__font_prefer_chinese`). `font_audit`,
  `font_subset_ja`, `font_subset_ko` and `font_subset_zh` (`port/test/CMakeLists.txt`) check the subsets' recorded code points without
  a build.
- `font_edge` (`test/font_edge_test.c`): plain text on the output at Enhanced 1080p and 4K, and
  the overlay unmirrored.
- `gallery` (`test/gallery_test.c`): the music gallery's list from the base ELF's tables.
  `gallery_jpn` (`gallery_test --jpn`): every disc's 105 `adpcmFile` paths equal row for row (the
  names are keyed by the row), then the list from the Japanese tables and image.
- `gallery_sweep` (`test/gallery_sweep.py`), `credits_headless` (`test/credits_headless.py`, with
  `test/headless_common.py`): the headless game through Extras > Music and Extras > Credits.
- `ui_mouse` (`test/ui_mouse_test.c`): the pointer's arithmetic and hit test.

`test/settings_fixture.h` and `settings_fixture.c` are the shared fixture: stubs for the game's
imports, fake tables (`fakeTables`, `setRow`, `setLayout`), the tick drivers (`frame`, `settle`,
`press`), the config helpers (`useConfig`, `writeFile`), the navigation (`enterMain`, `openPage`,
`pauseToMain`), the row lookups (`labelsAre`, `rowWithText`, `noteStarting`, `checkPageFits`,
`mainLink`: the Main row that opens a page, so a walk does not count Main's rows)
and the fake hosts; it is compiled once per test executable, with `SETTINGS_RENDER` for
`settings_render` (`port/ui/test/settings_fixture.h`, header comment). [TESTING.md](TESTING.md)
has how to run them.
