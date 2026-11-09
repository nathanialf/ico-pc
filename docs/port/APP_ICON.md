# The application icon

Files: `port/platform/app_icon.c`, `app_icon.h` (paths, stamp, the wanted test, the atomic writes; the
header is the interface the capture and the window host use), `app_icon_image.c`, `app_icon_image.h`
(the image maths), `app_icon_ico.c` (the `.ico` writer and the `.tmp` + rename write), and
`app_icon_host.c` (the window build's wrappers: the user folder, the disc and the package label, the
PNG, the "icon changed" flag), `app_icon_window.c`/`.h` (the icon on the window) and
`macos/app_icon_mac.m` (the Dock icon), `desktop_entry.c`/`.h` (the Linux applications-menu entry),
and for the Windows exe's own icon `app_icon_exe.c`/`.h` (the game's side), `icon_stamp.c`/`.h` (the
resources built from the `.ico`, the stamp, the command-line quoting) and `tools/ico_icon_stamp.c`
(the helper that writes them).

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
title's icon pass"), `port/render/rd_icon_hold.c`, `rd_icon_hold.h` (`rd_icon_hold`: the title's
picture held on screen).

The letter is geometry (a stage model), not a texture, and the screen-space transform runs in the
vertex shaders, so the icon is cut from the title as it is drawn. `ico_title_logo_update` steps the
capture once a tick (`ico_title_icon_tick`). Once the title's menu (Continue / New Game, layout 12 or
13 on the European disc) has been up for 30 ticks with the logo shown, the model viewer off and no
game fade running (`gsb_fade`'s `fadeStatus` 0, or 3 held at level 0: the title fades in from its
pale colour for some seconds after the menu shows; the ticks under it are not counted, the line
`ico_pc: icon: waiting for the title's fade to end` once a run), and
`ico_app_icon_wanted()` says so, the renderer is asked to hold the title's picture
(`rd_icon_hold(true)`: from then on every present copies its output, before the overlay, into an
output-sized texture), and once it holds one (`rd_icon_hold_ready()`, the tick after; at most
`ICO_TITLE_ICON_HELD_WAIT` (10) ticks, then the line `ico_pc: icon: no picture of the title was
presented in 10 ticks to keep on screen: the capture's frames show black`) the pass comes on:

- the game side: `ico_title_logo_skip` answers 1 for every model but `I`, so `reg_DispObj` draws the
  letter alone (no game code changes);
- the renderer: `rd_set_icon_pass(colour)` leaves out the UI and full-frame screen prims (the menus,
  the game's fade, brightness and letterbox sprites), the deferred text and the post effects over
  SCENE that are not the letter's (`rd_post`'s fog, glow and flare, depth of field, motion blur,
  aura, eye blur and film noise: `rd__icon_pass_drops_post`, `port/render/rd_icon_hold.h`), and makes
  the frame head clear SCENE to the colour, black first; world draws, the reduction (with its tint)
  and copies are untouched. The fog matters most: it is drawn by depth, and with the backdrop and
  the castle left out the pass's depth is the cleared far plane over the whole frame, so with the
  fog drawn the pass frames would be the fog colour (the title's table entry 0, `DFDBCD`, about
  86 % over the clear) through the reduction's tint on every pixel, in both pictures, and the matte
  would cut that as the letter: a box of the whole frame and one colour, `(255, 247, 227)` at alpha
  215, on every pixel of the icon (the arithmetic is in the comment at `rd_post`'s drop).
  At the off one line counts what the pass left out: `rd: icon pass: frames A..B drew the title's
  letter alone: N screen prim calls (the menus, the fade, the brightness), N text items and N post
  effects (the fog, the glow, the blurs) left out`;
- the hold: every present whose picture holds the pass (its frame at or past the frame open when
  the pass came on, and not yet past the pass's last frame by `ico_title_icon_present_clean`'s rule;
  `rd__icon_hold_tainted`) is rendered and copied for the shot as any other, then the held picture is
  drawn over the whole output, one texel a pixel (`rd_present.c` `rd__present_record`, after
  `captureRecord`, before the overlay: the popups, the HUD and the touch controls stay live). The
  player sees the title's last picture, still, for the half second the capture takes; nothing turns
  black. With no picture held (no present between the hold and the pass: the window minimised) those
  presents are cleared to black instead, the old curtain's look. The first present past the pass's
  frames shows the live picture again. The frames, their replay and the pictures the shot reads are
  the same with and without the hold, so the icon files are too.

The shot is armed (`rd_capture_shot`, with its own answer slot: the pause picture's deferred slot is
polled by `state_host.c` every host step) after three ticks with the colour in force, because the tick
the colour changed had already recorded the menus and an in-between picture also reads the tick
before. The shot copies the presentation box before the held picture is drawn, so the shot is the
pass's own picture. Once the picture over black is back, the clear turns white and the same wait and
shot follow. Then the pair goes to `ico_app_icon_deliver` (on the game thread, 30 to 130 ms at
3840 x 2160: the ghost test and the matte read every pixel of both pictures; the PNG and the `.ico`
are of the 256 x 256 image; the presents of that step still show the held picture, the pass being on),
the pass goes off, and the hold is released at the first present past the pass's frames:
`rd_icon_pass_last_frame()` at the pass's off is the newest frame number given out (the frame open
then), and the hold stays while `rd_last_present_info()` names that frame or an older one, or blends
a later one with it (alpha under 1 and the frame before at or under it;
`ico_title_icon_present_clean`; the renderer applies the same rule to each present, so the live
picture is back from that present on whether or not the game thread has released the hold yet). A
frame walked before the pass went off may be presented several ticks later, and its picture is the
white field or the bare letter. After `ICO_TITLE_ICON_HOLD_MAX` (60) ticks without such a present
the hold is released anyway, with the line `ico_pc: icon: no picture past the pass's frames (frame
N) was presented in 60 ticks: the held picture is released`. A pair refused as moved
(`ICO_APP_ICON_GHOST`, or the two pictures of different sizes) is taken again once; a second refusal
cuts the icon from the picture over black alone (`ico_app_icon_deliver_single`). An empty answer
(the present dropped it) or none within 60 ticks arms the shot again, at most three times in a run;
then the run gives up. Leaving the menu during the pass (New Game, Continue, a Settings page) ends
it, and the next time the menu has been up for 30 ticks it starts again. The desktop window builds
and Android capture (the files land in `<files>/icon/` on Android, `ico_host_pref_dir`); iOS captures
nothing (the capture is compiled out under `ICO_IOS`). The Android video defaults are the plain
picture (Original preset: no Upscaling, CRT off, the four Picture colour settings at their defaults),
so the capture does not wait there.

The log lines: `ico_pc: icon: capturing the title's I (pass 1 of 2)` and `(pass 2 of 2)`;
`ico_pc: icon: made from the title's I (<w>x<h> pictures)`; `ico_pc: icon: the two pictures
differ (...): taking the pair again` and `... differ again: the icon is cut from the picture over
black alone`; `ico_pc: icon: could not be made (the lines above say why) ...`;
`ico_pc: icon: the title's picture was not taken (<why>): trying again`; `ico_pc: icon: no picture of
the title came back (<why>; <n> tries): no icon made this run, the next start tries again`;
`ico_pc: icon: the title's menu was left during the capture: tried again the next time it shows`.
The renderer's lines say the capture never showed: `rd: icon: the capture's frames are drawn off
screen from frame N: the title's last picture stays on screen` at the pass's first present, and
`rd: icon: the title held on screen for N presents while the capture ran (its frames A..B drawn off
screen, the held picture frame F's, copied C times): the live picture is back at frame M` (or `the
hold released at frame M` when the safety net released it first); presents cleared to black for
want of a held picture add `rd: icon: N presents of the capture's frames were black: ...`.

The capture needs the renderer and the title stage, which are up only at the title, so the first
run's icon (and the first run's after an update) appears at its first title screen; the next start
finds the file before the first frame.

## When the capture waits

`ico_app_icon_wanted()` answers `ICO_APP_ICON_NOT_PLAIN` while an icon is due but the presented
picture is not plain (`ico_app_icon_picture_plain`, `app_icon.c`): the CRT filter on (strength above
0), or any of the four Picture-page adjustments (Gamma, Brightness, Contrast, Saturation) off the
game's own 50 (a stored 0 reads as 50), or Upscaling on (FSR 1.0 chosen, or FSR 3.1 in force:
`rd_upscale_ratio()` above 0; an FSR 3.1 the device cannot run is drawn as Off and does not count).
The filter and the tone act on the finished picture before the capture's copy, so the white
background would not come back as 255 and the matte would be wrong (the box of alpha could grow to
the whole frame); FSR 1.0's edge-adaptive scale and sharpening are not linear in the background
colour (fringes at the letter's edge), and FSR 3.1 carries its history and jitter from the shot over
black to the one over white. Screen softening does not matter: a blur of a flat field is flat. The
first refusal logs `ico_pc: icon: waiting for a plain picture (CRT filter, Brightness or Upscaling
on)`; the capture goes back to idle with its count of menu ticks at 0 (not done for the run), so it
asks again after another `ICO_TITLE_ICON_WAIT` ticks of the title's menu, on this visit or a later
one, and the icon is made once the settings are plain (or on a run with them plain). `app_icon_host.c` reads the settings with `ico_video_get`; the rule itself is
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
that made the icon (`SDL_SetWindowIcon` is main-thread only; the pump is the main thread). iOS keeps its packaged launcher icon (the function does nothing there; the capture is not
compiled). On Android the function hands the PNG's path to `ico_android_task_icon`
(`host_android.c`), which calls `IcoActivity.setTaskIcon` (the recents card, below); the log line is
`window: icon: the recents card is set from <path>`, or `window: icon: <reason>`.

| platform | what the icon reaches |
| --- | --- |
| Windows | the window's title bar and the taskbar button (SDL sets the window's big and small icons); the program file (`ico_pc_x64.exe` in Explorer, its shortcuts and a pinned taskbar button) from the first exit after the icon was made: `tools\ico_icon_stamp.exe` writes the `.ico` into the exe once the game has closed ([below](#the-exes-icon)); until then, or with the program in a folder the player cannot write to, the generic icon |
| Linux, X11 | the window and the taskbar through `_NET_WM_ICON` |
| Linux, Wayland | nothing from the window: SDL 3.4's Wayland has no toplevel-icon support (its library holds no `xdg_toplevel_icon`), so a Wayland desktop takes the icon from the applications-menu entry named after the app id (`com.defnf.icopc`), which the Extras row writes ([below](#the-applications-menu)) |
| macOS | SDL's Cocoa windows have no icon of their own, so `app_icon_mac.m` calls `[NSApp setApplicationIconImage:]` with the PNG: the Dock icon while the game runs. The bundle's Finder icon stays the generic one: no `CFBundleIconFile`, and the bundle is ad-hoc signed with nothing in `Resources` that is made at run time. CMake compiles the `.m` (`enable_language(OBJC)`) and links `-framework AppKit` |
| Android | the recents card (the task switcher): `ActivityManager.TaskDescription` with the PNG as a bitmap, set again in `onStart` because a recreated activity resets it. The launcher (home-screen) icon is a compiled resource and stays the plain one; some OEM launchers ignore a task description's icon |
| iOS | unchanged: the app keeps its packaged icon |

### The exe's icon

A running Windows program cannot rewrite its own file, and the icon does not exist until the player's
disc has made it, so the package's exe carries no icon and a helper beside it gives it the icon after
a run: `x64\tools\ico_icon_stamp.exe` (`tools/ico_icon_stamp.c`, CMake target `ico_icon_stamp`,
Windows only, shipped by `tools/package_win.sh`).

- **At start** (`ico_app_icon_exe_start`, `app_icon_exe.c`, called from `main_host.c`'s
  `open_window`): the exe's own `RT_RCDATA` resource `ICO_ICON_STAMP` (`FindResourceW` on
  `GetModuleHandleW(NULL)`) is read and logged, `window: icon: the exe carries the icon (stamp
  <S>)` or `window: icon: the exe has no icon yet`, and the helper's last line from
  `logs\icon-stamp.log` is repeated as `window: icon: the stamp helper's last line
  (logs\icon-stamp.log): <line>` (the helper's result of the run before reaches the reporter's log).
- **At the end of the run** (`ico_app_icon_exe_at_exit`, an end-of-run step `open_window` registers
  before the window's own, so it runs after the window has closed and before the summary's lines):
  the wanted stamp is `<the icon's .txt> <disc id>` (`ico_icon_stamp_text`: for example `<package label>
  SCES-50760`) for the current disc's `ico-pc-<disc id>.ico`. The files are read then, so the icon
  this run's capture made counts. When the `.ico` exists and the stamp differs from the exe's, the
  helper is started with `CreateProcessW` (`DETACHED_PROCESS | CREATE_NO_WINDOW`, below-normal
  priority, no handles inherited, out of the game's job when the job allows it:
  `CREATE_BREAKAWAY_FROM_JOB`, then without it) as `ico_icon_stamp <exe> <ico> <stamp>
  <logs\icon-stamp.log>` (each argument quoted by `ico_icon_stamp_quote` as
  `CommandLineToArgvW` splits it). One line says what happened: `window: icon: the exe's icon is
  stamped after this run (tools\ico_icon_stamp.exe, stamp <S>; its result in
  logs\icon-stamp.log)`, `... is current (stamp <S>)`, `... is not stamped: the title screen has
  not made this disc's icon yet` (or another reason), `window: icon: tools\ico_icon_stamp.exe is
  missing: the exe keeps its icon`, or `... did not start (error <n>)`. It runs once a run; a folder
  the helper could not write is tried again only at the next exit.
- **The helper** waits up to 10 s (100 ms steps) for the exe to open for writing (the game still
  exiting, a virus scanner reading it); a sharing violation for the whole wait, or at once any other
  error (access denied: a folder the account cannot write to, as under Program Files), ends it with
  exit 1 and nothing changed. It reads the `.ico` and builds the resources with
  `ico_icon_stamp_build` (`icon_stamp.c`, no Windows call): one `RT_ICON` per image, ids 1..n, each
  the image's bytes as the `.ico` holds them (the BMP entries' `BITMAPINFOHEADER` data and the 256
  PNG are both legal payloads), and `RT_GROUP_ICON` 1, the `ICONDIR` with each entry's 4-byte file
  offset replaced by its 2-byte id (`GRPICONDIR`, 6 + 14 bytes an image); a file that is not an icon,
  has no images or more than 32, or runs past its end is exit 2. It copies the exe to
  `<exe>.stamp-<pid>.tmp` beside it, writes the icon resources and `RT_RCDATA "ICO_ICON_STAMP"` (the
  stamp text, no terminator) into the copy (`BeginUpdateResourceW(copy, FALSE)`, all language
  neutral, the manifest and the version information kept, `EndUpdateResourceW`), reads the copy's
  stamp and group back (`LoadLibraryExW` as an image resource), checks that what runs is unchanged
  (`ico_icon_stamp_pe_check` on both files mapped: the same machine, image base and entry point,
  every section that starts below the old resource section at the same place with the same bytes,
  the import, exception, TLS and import address table directories the same, every section of the
  copy inside its file and `SizeOfImage`; `EndUpdateResourceW` rewrites `.rsrc` and moves what
  follows it, which in the game's exe is `.reloc`, the DWARF sections and the COFF symbol table the
  map-based crash symbols do not use), and puts the copy in the exe's place
  (`ReplaceFileW`, which keeps the file's attributes and security; `MoveFileExW` when that fails), so
  a failure at any step leaves the exe as it was and removes the copy. Then
  `SHChangeNotify(SHCNE_UPDATEITEM)` on the exe and `SHChangeNotify(SHCNE_ASSOCCHANGED)` tell
  Explorer to draw it again. Each run appends one line to `logs\icon-stamp.log`: the local time,
  the result (`stamped: 4 images (1 PNG), stamp "<S>", after <ms> ms`, `not stamped: ...` with the
  system's words for the error, `refused: ...`), the exe and the `.ico`. Exit 0 stamped, 1 not
  written, 2 a bad `.ico` or arguments.

The exe takes the icon of the disc played last (each disc's icon is its own stamp), and a release
that makes the icon again (a new label in the `.txt`) stamps the exe again; a portable copy works the
same (its folder is writable). The crash report's symbols come from `ico_pc_x64.map` by name, not
from a hash of the exe, so a stamped exe is symbolised as before; Steam's entry keeps the `.ico`
path ([Add to Steam](#add-to-steam)).

Risks: an exe that rewrites another exe is a pattern antivirus heuristics may flag (the helper is
small, unsigned, and only touches the file it is given). A helper a scanner stopped leaves the
game's "stamped after this run" line with no new line in `logs\icon-stamp.log`, which the next
start's log shows (the last line repeated is the older one). Explorer caches icons: the change notifications and the new file
time usually refresh the picture at once, but a view may show the old icon until it is refreshed or
the icon cache is rebuilt. The resource update itself runs only on Windows and is checked there by
hand (an rc check: the exe's icon after the first exit, `logs\icon-stamp.log`); the ctest covers the
resource building, the stamp and the quoting.

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

## The applications menu

Options > Extras > Add to the applications menu (`UI_OPT_EXTRAS_DESKTOP`; Linux only, shown
from the title while `ui_settings_set_desktop_handler` holds the window host's `desktop_add_handler`,
the Add to Steam row's rule) writes a freedesktop desktop entry and the icon, and nothing is written
before the press. `desktop_entry.c` has pure functions over a base folder and the program's path, so
the test runs on a fake folder:

- `ico_desktop_entry_data_home`: `$XDG_DATA_HOME` when it is an absolute path, else
  `$HOME/.local/share`.
- `ico_desktop_entry_text(out, size, exe)`: the entry, exactly `[Desktop Entry]`, `Type=Application`,
  `Name=ICO`, `Exec="<exe>"`, `Icon=com.defnf.icopc`, `Categories=Game;`, `Terminal=false`,
  `StartupWMClass=com.defnf.icopc`, one per line. Inside the quotes of `Exec` the desktop entry
  specification's rules apply: backslash, double quote, backtick and dollar get a backslash, and a
  percent sign is doubled (a lone `%` would be a field code); then the string value's own escaping
  doubles every backslash of that, because a reader unescapes the string before it applies the
  quoting rule (the specification's own note: a literal backslash in a quoted argument is four
  backslashes, a dollar `\\$`). A path with a control character (a
  newline would end the line and let the rest be read as another key) or too long for the buffer is
  refused.
- `ico_desktop_entry_install(dataHome, exe, pngPath)`: the PNG first, copied to
  `<dataHome>/icons/hicolor/256x256/apps/com.defnf.icopc.png`, then the entry to
  `<dataHome>/applications/com.defnf.icopc.desktop`, making the folders; each file goes to `<name>.tmp`
  and is moved over the old one, so a failure never leaves a half-written file or an entry whose icon
  was refused. A NULL path writes the entry alone.

Both files are named after the application id the window carries (`SDL.app.metadata.identifier`,
`com.defnf.icopc`): a Wayland compositor matches a window to its desktop entry by that name (the
window's `app_id`), and X11 matches `StartupWMClass` to the window class; X11 also gets the icon from
the window itself. `Exec` is the program Add to Steam records (`ico_steam_self_exe`): the AppImage
file when run from one, else the running program. The handler passes the current icon
(`ico_app_icon_current_path`); before the capture has made one, the entry is written without a
picture and the press logs `no icon made yet`; pressing the row again at a later run copies the icon
(a newly made icon, as after an update, reaches the menu the same way: the copy is not refreshed
without the press). The press logs `ico_pc: applications menu: <exe> in <data folder> (with the icon /
no icon made yet): written | failed`, and the popup says `Added to the menu` or `Could not add to the
menu`. The AppImage's own `com.defnf.icopc.desktop` and `.svg` (`tools/appimage/`) carry the same
name, with the placeholder art and `Icon=com.defnf.icopc`, so integrations that read the AppImage's
entry match the app id; the placeholder is not the I. What only a desktop proves: the entry showing in a
KDE or GNOME menu, and a Wayland window matching it.

## Tests

The applications menu's are `desktop_entry_test` (`desktop_entry`: the exact file text, the `Exec`
quoting for a path with a space, `$`, `"`, `\`, a backtick and `%`, the refusals, the data folder
rule, the install over a fake folder with the PNG copied byte for byte and no `.tmp` left) and the
Extras row's in `settings_desktop_test` (`settings_desktop`: hidden without a handler and on the pause
menu, after Add to Steam, Cross and the popups, the strings), with the row counts in `settings_test`
and the other Extras tests.

The tests are `app_icon_image_test` (`app_icon_image`), `ico_icon_test` (`ico_icon`),
`app_icon_files_test` (`app_icon_files`), `icon_stamp_test` (`icon_stamp`: the exe's resources from
an `.ico` the writer made, the refusals, the stamp text and the quoting) and `rd_png_mem_test`
(`rd_png_mem`)
([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)), with the guard's cases in `app_icon_files_test` and the
icon field's in `steam_shortcut_test` (`steam_shortcut`: `ico_steam_vdf_set_icon` on every outcome, the
whole job with an old entry, a new entry, the player's icon and no icon file); the capture's are `title_logo_test` (`title_logo`: the
name rule and the steps with the renderer and the files stubbed), `rd_icon_pass_test`
(`rd_icon_pass`: the renderer's pass recorded without a device) and `rd_icon_hold_test`
(`rd_icon_hold`, `rd_icon_hold_gl`: the held picture presented on a device while the shot reads the
pass's own, [TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)).
