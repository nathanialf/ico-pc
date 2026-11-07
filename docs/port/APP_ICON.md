# The application icon

Files: `port/platform/app_icon.c`, `app_icon.h` (paths, stamp, the wanted test, the atomic writes; the
header is the interface the capture and the window host use), `app_icon_image.c`, `app_icon_image.h`
(the image maths), `app_icon_ico.c` (the `.ico` writer and the `.tmp` + rename write), and
`app_icon_host.c` (the window build's wrappers: the user folder, the disc and the package label, the
PNG, the "icon changed" flag).

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

## Tests

The tests are `app_icon_image_test` (`app_icon_image`), `ico_icon_test` (`ico_icon`),
`app_icon_files_test` (`app_icon_files`) and `rd_png_mem_test` (`rd_png_mem`)
([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)); the capture's are `title_logo_test` (`title_logo`: the
name rule and the steps with the renderer and the files stubbed) and `rd_icon_pass_test`
(`rd_icon_pass`: the renderer's pass recorded without a device,
[TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)).
