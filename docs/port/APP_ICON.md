# The application icon

Files: `port/platform/app_icon.c`, `app_icon.h` (paths, stamp, the wanted test, the atomic writes; the
header is the interface the capture and the window host use), `app_icon_image.c`, `app_icon_image.h`
(the image maths), `app_icon_ico.c` (the `.ico` writer and the `.tmp` + rename write), and
`app_icon_host.c` (the window build's wrappers: the user folder, the disc and the package label, the
PNG, the "icon changed" flag), `app_icon_window.c`/`.h` (the icon on the window) and
`macos/app_icon_mac.m` (the Dock icon).

The program's icon is the title lettering's "I", made from the player's own disc and kept as files in
the user folder; nothing of it is in the repository or the download
([LEGAL.md](../LEGAL.md#what-is-not-in-this-repository-and-must-never-be-added)). The files are
`<user folder>/icon/` (`port/platform/app_icon.h`):

| file | holds |
| --- | --- |
| `ico-pc-<disc id>.png` | 256 x 256 RGBA, straight alpha, the image the window and the shortcuts use |
| `ico-pc-<disc id>.ico` | 16, 32 and 48 as 32-bit BGRA bottom-up BMP entries (a zero 1-bit mask) and 256 as a PNG entry; written on every platform, used on Windows |
| `ico-pc-<disc id>.txt` | the stamp: `ICO_VERSION_LABEL`, or `dev` for a build without a label |
| `ico-pc.png`, `ico-pc.ico` | copies of the current disc's pair, at names that do not change |

The disc id is the disc's (`ico_region_cur()->disc_id`, for example `SCES-50760`), so each disc keeps
its own icon. Every file is written as `<name>.tmp` and moved over the old one
(`ico_rename_replace`, as the extractor does), the stamp last, so the stamp only vouches for a
complete set. `ico_app_icon_wanted()` is 1 when the disc's PNG is missing or empty, or the stamp
differs from the package label; a build without a label (`dev`) regenerates only when the PNG is
missing. Each write logs `ico_pc: icon: written <path> (<disc id>, <label>)`; each refusal logs
`ico_pc: icon: refused ...` with the reason.

`ico_app_icon_deliver(a, b, w, h)` takes two captures of the same tick, the clear colour black (A)
and white (B), as RGBA8 with alpha 255 (`rd_capture_shot`'s form). The letter's coverage is the
difference between them: with B - A = 255 - alpha in every channel, alpha = 255 - max over the
channels of (B - A) and colour = A * 255 / alpha (`port/platform/app_icon_image.c`). The crop is the
box of alpha >= 8, made a square with a 10 % margin on each side, centred, kept inside the frame, and
resampled to 256 x 256 (an area average when shrinking, bilinear when growing, weighted by alpha). A
ghost check refuses a pair whose ink boxes (A's non-black pixels, B's non-white pixels) differ by
more than one pixel at any edge (the letter moved between the captures): the call answers
`ICO_APP_ICON_GHOST` and writes nothing. `ico_app_icon_deliver_single` is the fallback, a luma key
from one frame (alpha = the brightest channel), logged as such. The PNG is encoded in memory by
`rd_write_png_mem` (`port/render/rd_png.c`, the same bytes `rd_write_png` writes to a file); a
delivery raises a flag that `ico_app_icon_changed_take()` reads and clears.

## The capture on the title

Files: `port/game/title_icon.c`, `title_icon.h` (the steps), `port/game/title_logo.c` (the name
rule), `port/render/rd_frame.c` and `rd_core.c` (`rd_set_icon_pass`, `port/render/rd.h` "the
title's icon pass"), `port/ui/ui_host.c` (`ui_host_curtain`).

The letter is geometry (a stage model), not a texture, and the screen-space transform runs in the
vertex shaders, so the icon is cut from the title as it is drawn. `ico_title_logo_update` steps the
capture once a tick (`ico_title_icon_tick`). Once the title's menu (Continue / New Game, layout 12 or
13 on the European disc) has been up for 30 ticks with the logo shown and the model viewer off, and
`ico_app_icon_wanted()` says so, the pass comes on:

- the game side: `ico_title_logo_skip` answers 1 for every model but `I`, so `reg_DispObj` draws the
  letter alone (no game code changes);
- the renderer: `rd_set_icon_pass(colour)` leaves out the UI and full-frame screen prims and the
  deferred text (the menus) and makes the frame head clear SCENE to the colour, black first; world
  draws and post passes are untouched;
- the curtain: `ui_host_curtain(1)` puts an opaque black quad over the whole output at the start of
  the presenter's top overlay layer, so the player sees black frames.

The shot is armed (`rd_capture_shot`, with its own answer slot: the pause picture's deferred slot is
polled by `state_host.c` every host step) after three ticks with the colour in force, because the tick
the colour changed had already recorded the menus and an in-between picture also reads the tick
before. The shot copies the presentation box before the overlay, so the curtain is never in it. Once
the picture over black is back, the clear turns white and the same wait and shot follow. Then the
pass goes off, the pair goes to `ico_app_icon_deliver`, and the curtain comes off two ticks later
(pictures of the pass's frames may still be presented). A pair refused as moved
(`ICO_APP_ICON_GHOST`, or the two pictures of different sizes) is taken again once; a second refusal
cuts the icon from the picture over black alone (`ico_app_icon_deliver_single`). An empty answer
(the present dropped it) or none within 60 ticks arms the shot again, at most three times in a run;
then the run gives up. Leaving the menu during the pass (New Game, Continue, a Settings page) ends
it, and the next time the menu has been up for 30 ticks it starts again. Only the window build
captures; on Android nothing is captured (the launcher icon is the package's).

The log lines: `ico_pc: icon: capturing the title's I (pass 1 of 2)` and `(pass 2 of 2)`;
`ico_pc: icon: made from the title's I (<w>x<h> pictures)`; `ico_pc: icon: the two pictures
differ (...): taking the pair again` and `... differ again: the icon is cut from the picture over
black alone`; `ico_pc: icon: could not be made (the lines above say why) ...`;
`ico_pc: icon: the title's picture was not taken (<why>): trying again`; `ico_pc: icon: no picture of
the title came back (<why>; <n> tries): no icon made this run, the next start tries again`;
`ico_pc: icon: the title's menu was left during the capture: tried again the next time it shows`.

The capture needs the renderer and the title stage, which are up only at the title, so the first
run's icon (and the first run's after an update) appears at its first title screen; the next start
finds the file before the first frame.

## When the capture waits

`ico_app_icon_wanted()` also answers 0 while the presented picture is not plain
(`ico_app_icon_picture_plain`, `app_icon.c`): the CRT filter on (strength above 0), or any of the four
Picture-page adjustments (Gamma, Brightness, Contrast, Saturation) off the game's own 50 (a stored 0
reads as 50). Both act on the finished picture before the capture's copy, so the white background
would not come back as 255 and the matte would be wrong (the box of alpha could grow to the whole
frame). Screen softening does not matter: a blur of a flat field is flat. The first refusal logs
`ico_pc: icon: waiting for a plain picture (CRT filter or Brightness on)`; the title's capture asks
again at each title visit, so the icon is made at a later visit once the settings are plain (or on
a run with them plain). `app_icon_host.c` reads the settings with `ico_video_get`; the rule itself is
tested without the settings (`app_icon_files_test`).

## Applying the icon

`ico_app_icon_apply(SDL_Window *)` (`app_icon_window.c`) reads `<user folder>/icon/ico-pc.png`
(`ico_app_icon_current_path`), decodes it with the texture packs' PNG loader (`texpack_load_png`),
and hands SDL an RGBA32 surface made over the pixels (`SDL_CreateSurfaceFrom`) for
`SDL_SetWindowIcon`, which copies it; the surface and the decoded image are freed at once. No file, or
a file that does not decode: nothing is done and SDL's default stays. It logs `window: icon: set from
<path>` on success and a `window: icon: ...` line on a read or SDL failure. It runs at the end of
`window_attach` (`window_host.c`), so at the start and again when Apply reopens the window for another
renderer, and in `ico_window_pump` whenever `ico_app_icon_changed_take()` is 1, which is the run
that made the icon (`SDL_SetWindowIcon` is main-thread only; the pump is the main thread). Android
and iOS keep their packaged launcher icons (the function does nothing there; the capture is not
compiled on Android either).

| platform | what the icon reaches |
| --- | --- |
| Windows | the window's title bar and the taskbar button (SDL sets the window's big and small icons); `ico_pc.exe` in Explorer keeps the generic icon: it carries no icon resource and is not rewritten (the icon does not exist until the player's disc has made it) |
| Linux, X11 | the window and the taskbar through `_NET_WM_ICON` |
| Linux, Wayland | nothing: SDL 3.4's Wayland has no toplevel-icon support, so a Wayland desktop takes the icon from the applications-menu entry named after the app id (`com.defnf.icopc`) |
| macOS | SDL's Cocoa windows have no icon of their own, so `app_icon_mac.m` calls `[NSApp setApplicationIconImage:]` with the PNG: the Dock icon while the game runs. The bundle's Finder icon stays the generic one: no `CFBundleIconFile`, and the bundle is ad-hoc signed with nothing in `Resources` that is made at run time. CMake compiles the `.m` (`enable_language(OBJC)`) and links `-framework AppKit` |
| Android, iOS | unchanged |

### Add to Steam

`IcoSteamEnv.iconPath` (`steam_shortcut.h`) holds the current copy's path when the file exists:
`<user folder>/icon/ico-pc.ico` on Windows (backslashes), `ico-pc.png` on Linux. `ico_steam_add_this_game`
fills it (`ico_steam_env_default` leaves it empty, so the tests' environments are their own; the
function that links the icon's files is the caller) and gives it to a new entry as its icon field;
with no file the old rule stands (Windows: the program itself, Linux: none). An entry of this program
already in Steam's list is upgraded: `ico_steam_vdf_set_icon(vdf, n, exe, caseless, icon, &out,
&outN)` finds the first entry whose Exe matches (either field order, `exe` quoted or plain) and
replaces its icon value, or adds the field before the entry's end when it has none, only when the
value is empty or the program (quoted or plain). It answers 1 (changed, the file in `out`, every
other byte as it was), 0 (no such entry, or the player's own icon: left alone; or `icon` empty), -1
(not a shortcuts file). `ico_steam_add_shortcut` then writes it on the same path as an added entry
(`shortcuts.vdf.bak`, `.tmp`, rename) and still answers `ICO_STEAM_EXISTS`, logging `ico_pc: Add to
Steam: already there; icon set to <path>`. The icon file is in the user folder (the install folder
may be read-only; portable mode's `userdata` moves with the program). That Steam draws a `.ico` in its list is
untested here.

## Tests

The tests are `app_icon_image_test` (`app_icon_image`), `ico_icon_test` (`ico_icon`),
`app_icon_files_test` (`app_icon_files`) and `rd_png_mem_test` (`rd_png_mem`)
([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)), with the guard's cases in `app_icon_files_test` and the
icon field's in `steam_shortcut_test` (`steam_shortcut`: `ico_steam_vdf_set_icon` on every outcome, the
whole job with an old entry, a new entry, the player's icon and no icon file); the capture's are `title_logo_test` (`title_logo`: the
name rule and the steps with the renderer and the files stubbed) and `rd_icon_pass_test`
(`rd_icon_pass`: the renderer's pass recorded without a device,
[TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)).
