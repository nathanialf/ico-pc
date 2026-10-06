# Display options

How ico-pc draws the picture, and what you can change. The settings live in
`config.toml` (docs/port/CONFIG.md says where that file is) under `[video]`;
the Settings menu changes the same values while the game runs.

## The two presets

**Original** (the default) shows the game as the PlayStation 2 showed it:
the 512-line picture, halved to 256 lines and shown with every line twice,
in a 4:3 frame, textures filtered exactly as the game asked. Nothing below
except `fullscreen`, `vsync`, `framerate` and the CRT filter changes it (`framerate` only
adds blended pictures between the game's updates; each update's picture
stays the PS2's). In a window that is not 4:3 the
picture gets black bars on the sides (or top and bottom).

**Enhanced** turns on the options below. Each one is separate: Enhanced with
`resolution = "1x"`, `aspect = "4:3"`, `texture_filter = "original"`,
`full_height = false` looks exactly like Original.

The game itself (what happens, where things are, when the shadows reach
Yorda) is the same in both presets and with every option: the options only
change how the picture is drawn.

## The keys

```toml
[video]
preset = "original"         # "original" or "enhanced"
resolution = "window"       # "window", "WxH" (e.g. "1920x1440") or "Nx" (e.g. "2x")
aspect = "4:3"              # "4:3", "16:10", "16:9" or "auto"
fullscreen = false
vsync = true
texture_filter = "original" # "original", "trilinear" or "anisotropic"
full_height = false
framerate = "uncapped"      # "original", "uncapped" or a number (30 to 1000)
backend = "vulkan"          # Windows: "vulkan" or "d3d12"
crt = false                 # the CRT filter ("CRT filter" below)
crt_mode = "consumer"       # "scanlines", "consumer", "trinitron" or "pvm"
crt_strength = 1.0          # 0.0 to 1.0
```

| key | what it does |
| --- | --- |
| `preset` | `"original"`: the PS2 picture. `"enhanced"`: the options below apply. |
| `resolution` | How sharp the 3D scene is (Enhanced). `"window"`: as many pixels as the frame has on screen. `"2x"`: twice the PS2's resolution in each direction (widened with the aspect). `"1920x1440"`: that many pixels. At least the PS2's resolution, at most 4K (3840 x 2160). Effects such as blur and glow keep their size on screen. The menu text does not depend on it: in Enhanced the game's menu rows and the port's own menus are drawn last, at the window's own pixel size, one font pixel to one screen pixel ("Menu text" below). |
| `aspect` | The shape of the picture (Enhanced). `"16:9"` and `"16:10"` show more of the world to the left and right; the menus, subtitles and the title text stay in a 4:3 frame in the middle; full-screen fades, the cinema bars, the black bands of the pause and memory card menus and the film grain stretch across ("Wide pictures" below). `"auto"` follows the window, between 4:3 and 16:9. The movies stay 4:3 with bars at the sides. |
| `fullscreen` | Borderless fullscreen at the desktop's resolution. Alt+Enter switches while playing. |
| `vsync` | Wait for the screen's refresh: no tearing. Off lets frames show as soon as they are ready. |
| `texture_filter` | (Enhanced) `"trilinear"` gives textures smaller versions for distant surfaces, so the ground and walls do not shimmer; `"anisotropic"` also keeps them sharp at grazing angles. Textures the game draws unfiltered (pixel-sharp) stay that way. Fences and leaves with see-through parts keep their thickness in the distance. |
| `full_height` | (Enhanced) Keep all 512 lines of the scene instead of halving them, so the picture is not line-doubled. |
| `backend` | The graphics API on Windows: `"vulkan"` (the default) or `"d3d12"`. Linux has Vulkan only. Read at start-up. The Direct3D 12 renderer has not yet been tested on real hardware. |
| `crt`, `crt_mode`, `crt_strength` | (Both presets) The CRT filter: the picture shown as a television or monitor of the PS2's time would show it ("CRT filter" below). |
| `framerate` | (Both presets) How often the picture is redrawn. `"original"`: once for each of the game's 25 (PAL) or 30 updates a second, as on the PS2. `"uncapped"`: as often as the screen refreshes (with `vsync`) or as fast as the computer can (without), drawing in-between pictures so movement is smooth. A number such as `"60"` or `"144"`: at most that many pictures a second. |

## Menu text

With the Enhanced preset, the words of the game's menus (title, pause,
Options, the memory card screens and prompts) and of the port's Settings
menu are drawn after the picture is scaled to the window, at the window's
own resolution: each letter is made at the size it is shown and placed on
whole screen pixels, so the text is as sharp as the screen allows whatever
`resolution` says. It still fades with the picture, is cut by the cinema
bars and sits where the menu puts it, in the middle 4:3 frame. The
Original preset draws it into the PS2-sized picture as before, and
Settings > Display > "Menu text: Classic" (`[game] classic_menu_text`)
brings back the PS2's own lettering and drawing order in both presets. The
subtitles, the end credits and the memory card screens' figures are drawn
the same way; the subtitles in Yorda's script stay the game's pictures.

## CRT filter

`crt = true` shows the picture through a simulated cathode-ray tube, in
either preset: the PS2's lines become glowing scanlines, a phosphor mask
covers the screen, bright parts bloom, and (in two modes) the glass curves
and darkens toward its corners. Settings > Display > "CRT filter" picks the
mode (Off, Scanlines, Consumer TV, Trinitron, PVM) and "CRT strength" mixes
it with the plain picture, 0 to 100 % in tens.

| mode (`crt_mode`) | imitates | what you see |
| --- | --- | --- |
| Scanlines (`"scanlines"`) | the scanline structure alone | each line of the PS2's picture a soft horizontal beam with dark gaps between, brighter lines thicker; no mask, no glow, the picture flat and as wide as without the filter |
| Consumer TV (`"consumer"`, the default) | a period living-room television | a soft, slightly blurred picture with faint scanlines and a fine slot-mask grid, a glow around bright areas that spills a little into the black borders, a gently curved face with rounded corners and darker edges, and deeper shadows (gamma 2.4 in, 2.2 out) |
| Trinitron (`"trinitron"`) | an aperture-grille set | crisper than Consumer TV, visible vertical red-green-blue stripes and clearer scanlines; the face curves left to right only (flat vertically, as a Trinitron's cylinder), a slight vignette |
| PVM (`"pvm"`) | a studio (broadcast) monitor | the sharpest: pronounced scanlines with distinct dark gaps, a fine two-pixel grille, almost no glow, a flat face |

The parameters each mode uses (`port/render/rd_crt.c`; the shader is
`port/shaders/crt.hlsl`):

| parameter | Scanlines | Consumer TV | Trinitron | PVM |
| --- | --- | --- | --- | --- |
| scanline strength | 0.50 | 0.35 | 0.45 | 0.60 |
| beam width, dark to bright (lines) | 0.6 to 1.0 | 0.7 to 1.2 | 0.5 to 1.0 | 0.4 to 0.9 |
| horizontal blur (source pixels) | 1.0 | 1.4 | 1.0 | 0.7 |
| mask | none | slot, 0.35 | aperture grille, 0.50 | aperture grille, 0.30 |
| mask pitch (screen pixels) | | 3 | 3 | 2 |
| halation | 0 | 0.12 | 0.05 | 0.03 |
| bloom | 0 | 0.15 | 0.10 | 0.05 |
| curvature x, y | 0, 0 | 0.030, 0.045 | 0.030, 0 | 0, 0 |
| corner radius (of the height) | 0 | 0.03 | 0.02 | 0.01 |
| vignette | 0 | 0.15 | 0.08 | 0.05 |
| gamma in, out | 2.2, 2.2 | 2.4, 2.2 | 2.2, 2.2 | 2.2, 2.2 |

The mean brightness stays that of the plain picture (within 3 % on the
test frames in Scanlines, Trinitron and PVM; Consumer TV is 6 to 19 %
darker, from its gamma, more in dark scenes).

How it works: the filter draws the 4:3 box (or the wide one) itself, in
place of the usual scaling. It takes the picture at the PS2's resolution:
512 pixels across (more with a wide `aspect`) and the PS2's 256 lines (512
with `full_height`). Each line is not doubled: the scanlines are the PS2's
own lines, one beam each, a Gaussian whose width grows with the colour's
brightness. Horizontally each line is a Gaussian blend of its four nearest
pixels. The mask is laid out in screen pixels. Halation (a wide, faint
glow of all the light) and bloom (a narrower glow of the bright parts)
come from a half-size blurred copy of the picture. With the Enhanced
preset at a higher `resolution` the picture is first averaged down to the
PS2's size: a CRT of the time showed the PS2's pixels, whatever the scene
was rendered at.

**The mask needs pixels.** A phosphor triad 3 pixels wide, over the
picture's 512 source pixels, needs about 1440 pixels across, a 4:3 box
1080 lines high. Below that the mask would beat against the screen's own
pixels (moiré), so its strength fades from full at a 1080-line box to
nothing at 720 lines and below. The scanlines and the glow stay at any
size, though with fewer than about 3 screen lines per PS2 line (a box under
768 lines) the scanlines lose their shape.

**What stays sharp.** The filter is part of the picture, so everything the
PS2 drew is filtered, including the Original preset's menu text. With the
Enhanced preset the menu text drawn at the window's resolution ("Menu
text" above) is drawn after the filter, unfiltered and not curved, and so
are the port's own menus and popups: they stay readable. With a curved mode
that text sits where the flat picture would have it, a few pixels inside
the curved edge near the corners. The movies (the opening and the
ending) are drawn by their own path and are shown without the filter.

**What it does not change.** The filter happens when the picture is shown:
the game's frames, F12 screenshots of DISPLAY, frame dumps and the replay
tool's DISPLAY and SCENE images are the same with it on or off, and with it
off (or at 0 % strength) the shown picture is byte for byte what it was
without the option.

**Cost.** Three small passes at half the PS2's size (the glow) and one
pass over the box: per screen pixel 8 samples for the beam, 5 for the glow
and 1 for the plain picture. At 1440 x 1080 that is about 22 million
texture samples a picture, a small part of a picture's work on a desktop
GPU; it has not been timed on hardware.

**Overrides.** `config.toml` also takes, under `[video]`, `crt_scanlines`,
`crt_mask`, `crt_halation`, `crt_bloom` (each 0 to 1) and `crt_curvature`
(0 to 0.25): each replaces that parameter of the mode (`crt_curvature` is
the x curvature, y being 1.5 times it except on the Trinitron's flat
vertical; `crt_mask` on the Scanlines mode adds an aperture grille). A
negative value, or no key, keeps the mode's own. They have no Settings row.

## Photo mode

Pause the game (START), choose Options, then "Photo mode" (under
"Settings"; the row is there only while a stage is running). The game
stays paused: nothing moves and nothing in the game changes, and the
picture is the paused scene seen through a camera you move. The game's
menus, subtitles and other text are hidden while photo mode is on.

| control | what it does |
| --- | --- |
| Left stick | orbit: turn around the point the game camera was looking at, left and right, and up and down (never past 85 degrees above or below the horizontal). That point is on the game camera's line of sight, as far ahead as the character the camera follows (4 m ahead when it follows none) |
| Right stick | up and down: move closer or further (dolly, from a twentieth to ten times the game camera's distance); left and right: slide sideways (pan) |
| L1, R1 | roll the camera |
| R2 or Up, L2 or Down | narrow (zoom in) or widen the field of view, between 10 and 100 degrees vertically |
| Select | back to the game's camera |
| Square | hide or show the help lines at the bottom left |
| Cross | save a picture |
| Triangle, Circle or START | leave photo mode (back to Options) |

The mouse moves the right stick as it does in play (Settings > Controls,
"Mouse sensitivity"). `[photo] stick_speed` scales the sticks' speeds and
`[photo] invert_y` swaps the left stick's up and down (CONFIG.md).

**Pictures.** Cross saves the picture as shown, at the window's own
resolution (the whole window, black bars included), with the preset, the
CRT filter and the Enhanced menu text as they are, but without the help
lines or any popup: `ico-<date>-<time>.png` in the `screenshots` folder of
the per-user folder (CONFIG.md says where; `[photo] png_dir` names another
folder there). A popup names the file. The picture is saved at the next
picture drawn after the press. F12 is unchanged (a frame dump and the
DISPLAY picture, for bug reports).

**What stays as the game drew it.** The game draws some things for its
own camera only, on the processor, and those cannot be seen from another
camera:

- the shadows that characters and objects cast (the dark shapes on the
  ground and walls) stay where they were drawn for the game camera, as
  flat shapes on the screen: move the camera far and they no longer sit
  under what casts them;
- lightning and the other effects drawn as flat shapes on the screen;
- reflections (puddles, pools) keep the game camera's view of the
  reflected scene;
- objects the game did not draw because its camera could not see them
  stay missing (turning the camera around shows the background colour where the
  game skipped them);
- with the camera very close to a character, a triangle of the character
  that reaches far off the picture or behind the camera is left out, as
  the PlayStation 2's programs leave it out.

Everything else (the castle, the characters, the plants, the water's
surface, the fog, the glow and blur effects) is drawn again for the new
camera. The motion blur's trail is not kept while the camera moves.

**Depth of field** (`[photo] dof`) is not implemented: the key is read and
logged, and the picture stays sharp.

## Wide pictures: what stretches and what stays in the middle

With `aspect` wider than 4:3, anything that covers the whole picture
(bars, fades, dimming, backdrops, borders) reaches the left and right edges;
anything placed on the screen (text, menu rows, button pictures, the logo)
stays in the 4:3 frame in the middle, at the size it has at 4:3 (at
1280 x 720 and 16:9, the middle 4:3 frame is columns 160 to 1119):

| what | covers |
| --- | --- |
| The cinema bars of the story scenes | the whole width |
| Fades to and from black (or white), stage changes | the whole width |
| The black bands at the top and bottom of the pause menu, Options, button configuration, Brightness, the "The game will end" question, the memory card load, save and format screens and "Continue?" after a game over | the whole width |
| The dimming of the scene behind those menus (darker; red after a game over) | the whole width |
| The black behind the boot signs and the memory card check | the whole width |
| The picture's thin border (2 pixels at the sides, 8 lines top and bottom on the PS2), brightness, the "keep the last picture" of a paused game, the edge smoothing and the film grain | the whole width |
| Menu text, the cursor's glow, the subtitles, the ICO logo, the copyright line, the loading bar, the port's own menus and popups | the middle 4:3 frame |
| The movies | the middle 4:3 frame, black at the sides |
| Developer overlays (debug text, memory bars) | the middle 4:3 frame |

The world itself, reflections in puddles and pools included, simply shows
more at the sides.

## Smooth motion (`framerate`)

The game still updates 25 times a second (30 in 60 Hz mode); nothing about
how it plays changes. With `framerate` other than `"original"`, the picture
between two updates is drawn part of the way from the earlier to the later
one: characters, objects, the camera, cloth, water, particles, shadows,
fades and the cinema bars move smoothly. This shows the game one update
(1/25 s) later than the original setting does.

Some things still change 25 times a second, as the game makes them: texture
animations, the film grain, flickering effects such as lightning and the
menu sparkle, menus and subtitles, the glow effect's trails, and faces
that change shape. After a camera cut, a stage change or a fade to black
the next picture is shown as it is, not blended from the one before. The
motion-blur trail keeps the same length at any frame rate. Flat elements
and shadows that appear or disappear between two updates (a sign coming
into view, a changed line of text, a shadow whose caster left) fade in or
out over the in-between pictures where their blending allows it, and
otherwise switch half way, instead of appearing or vanishing a whole update
early.

This applies to both presets: with Original, each of the game's updates is
still drawn exactly as on the PS2 and only the pictures in between are
blended. `framerate = "original"` keeps one picture per update, the PS2's
cadence, for either preset.

On a slow computer (or a software renderer) the game draws one picture per
update, as with `"original"`, rather than slowing the game down.

Turning things turn in between pictures: a character or object that spins
between two updates is drawn at the in-between angle at its full size, its
lighting turning with it at full strength, and the camera turns as one rigid camera, so the scenery does not shear. How far between the two updates each picture is drawn follows a
steady clock rather than the exact moment the picture was started, so
movement advances by even steps.

## Vsync and an uncapped frame rate

With `vsync = true` and `framerate` other than `"original"`, ico-pc asks the
graphics driver for its "mailbox" mode where it has one (Vulkan; most PC
drivers do): the screen still shows only whole pictures (no tearing), but
handing a picture to the screen never waits for the screen's refresh, and a
picture the screen had no time to show is replaced by the newer one. The
game's own updates therefore never wait on the display: they keep the PS2's
25 (or 30) a second however the screen's refresh lines up with them. With
`"uncapped"` the pictures are drawn about twice per screen refresh, so every
refresh has a fresh one, without drawing hundreds that are never shown.

Where mailbox is not offered (the D3D12 backend, some drivers), vsync uses
the usual queue of pictures waiting for the refresh, and `"uncapped"` draws
one picture per refresh. Either way, a picture between two updates is only
drawn when there is time for it before the next update is due, so a slow
picture costs smoothness, never game speed.

`framerate = "original"` presents each update once, and with `vsync`
that present waits for the refresh, as on the PS2.
`window:` lines in `logs/ico-pc.log` say which mode is in use (`present
mode mailbox` or `fifo`) and, every 10 seconds, how many pictures and game
updates there were and where the renderer's time went (docs/port/
RENDER_API.md section 18; `[dev] perf_log = true` writes one line per
picture into `logs/ico-pc-perf.csv`).

## What stays as on the PS2

- What you can see and what the game considers "on screen" for its own
  decisions: a wider picture shows more, but the game keeps using the 4:3
  frame for everything it checks.
- The colours of the effects that the PS2 computed in a particular way
  (motion blur, glows, fog) are computed the same way at any resolution.
- The menus' layout, placed in the middle 4:3 frame.
- With the Enhanced preset at a resolution above the PS2's, the corners of
  3D models are placed exactly rather than on the PS2's grid of sixteenths
  of a pixel (a quarter of a pixel at 4x), so slowly moving edges do not
  step. The Original preset keeps the grid.

## Notes

- Changing `resolution`, `aspect` or `full_height` while playing clears the
  motion-blur trail for one frame, and the next update is not blended.
- On a renderer backend without mipmapped textures, `texture_filter` falls
  back to the original filtering.
- Technical details: docs/port/RENDER_API.md sections 15 (presets, the
  options), 16 (frame rate) and 18 (performance).
