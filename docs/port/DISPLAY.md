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
crt_mode = "consumer"       # "scanlines", "consumer", "trinitron", "pvm" or "shadow"
crt_strength = 1.0          # 0.0 to 1.0
```

| key | what it does |
| --- | --- |
| `preset` | `"original"`: the PS2 picture. `"enhanced"`: the options below apply. |
| `resolution` | How sharp the 3D scene is (Enhanced). `"window"`: as many pixels as the frame has on screen. `"2x"`: twice the PS2's resolution in each direction (widened with the aspect). `"1920x1440"`: that many pixels. At least the PS2's resolution, at most 4K (3840 x 2160). With the CRT filter on the scene is drawn at 1x whatever this says (the value is kept and applies again with the filter off; "CRT filter" below). Effects such as blur and glow keep their size on screen. The port's own text does not depend on it: in Enhanced the port's menus are drawn last, at the window's own pixel size ("Menu text" below); the game's own words are its textures and scale with the scene. |
| `aspect` | The shape of the picture (Enhanced). `"16:9"` and `"16:10"` show more of the world to the left and right; the menus, subtitles and the title text stay in a 4:3 frame in the middle; full-screen fades, the cinema bars, the black bands of the pause and memory card menus and the film grain stretch across ("Wide pictures" below). `"auto"` follows the window, between 4:3 and 16:9. The movies stay 4:3 with bars at the sides. |
| `fullscreen` | Borderless fullscreen at the desktop's resolution. Alt+Enter switches while playing. |
| `vsync` | Wait for the screen's refresh: no tearing. Off lets frames show as soon as they are ready. |
| `texture_filter` | (Enhanced) `"trilinear"` gives textures smaller versions for distant surfaces, so the ground and walls do not shimmer; `"anisotropic"` also keeps them sharp at grazing angles. Textures the game draws unfiltered (pixel-sharp) stay that way. Fences and leaves with see-through parts keep their thickness in the distance. |
| `full_height` | (Enhanced) Keep all 512 lines of the scene instead of halving them, so the picture is not line-doubled. |
| `backend` | The graphics API on Windows: `"vulkan"` (the default) or `"d3d12"`. Linux has Vulkan only. Read at start-up. The Direct3D 12 renderer has not yet been tested on real hardware. |
| `crt`, `crt_mode`, `crt_strength` | (Both presets) The CRT filter: the picture shown as a television or monitor of the PS2's time would show it ("CRT filter" below). |
| `framerate` | (Both presets) How often the picture is redrawn. `"original"`: once for each of the game's 25 (PAL) or 30 updates a second, as on the PS2. `"uncapped"`: as often as the screen refreshes (with `vsync`) or as fast as the computer can (without), drawing in-between pictures so movement is smooth. A number such as `"60"` or `"144"`: at most that many pictures a second. |

## Menu text

The game's own words (its menus, the memory card screens and prompts, the
subtitles, the end credits, the save screens' figures, the signs) are the
PS2's own lettering, its texels, in both presets: they are scaled with the
picture as every texture is, and there is no option to redraw them. The
text the port adds (the Settings and Extras pages, the popups, the hints)
is written in the game's own lettering, cut from the disc's menu sheets
(docs/port/UI.md, "The font"). With the Enhanced preset that text is drawn
after the picture is scaled to the window, at the window's own resolution,
so it is as sharp as the screen allows whatever `resolution` says. It still
fades with the picture, is cut by the cinema bars and sits where the menu
puts it, in the middle 4:3 frame. The Original preset draws it into the
PS2-sized picture; with the CRT filter on it is drawn there too, so that it
goes through the filter ("CRT filter" below).

## CRT filter

`crt = true` shows the picture through a simulated cathode-ray tube, in
either preset. Each of the PS2's pixels lights its own little patch of
phosphors: red, green and blue stripes (or dots) side by side, each glowing
with its own colour of that pixel and letting some of the other two
through (how much is the mode's mask strength), under a beam that is brightest in
the middle of the PS2's line and fades toward the next. Bright parts
bloom, and in some modes the glass curves and darkens toward its corners.
Settings > Display > "CRT filter" picks the mode (Off, Scanlines, Consumer
TV, Trinitron, PVM, Shadow mask) and "CRT strength" eases it in, 0 to
100 % in tens.

| mode (`crt_mode`) | imitates | the phosphors of one PS2 pixel | the rest |
| --- | --- | --- | --- |
| Scanlines (`"scanlines"`) | the scanline structure alone | no mask: the pixel's colour across the whole patch | each PS2 line a soft horizontal beam with dark gaps between, brighter lines thicker; no glow, flat |
| Consumer TV (`"consumer"`, the default) | a period living-room television (slot mask) | red, green and blue stripes cut into slots by a dark bridge over the last third of each line; every other pixel's slots sit half a line lower, so the bridges make a brick pattern | soft beam, a glow that spills a little into the black borders, a gently curved face with rounded corners and darker edges, deeper shadows (gamma 2.4 in, 2.2 out) |
| Trinitron (`"trinitron"`) | an aperture-grille set | red, green and blue stripes running unbroken from the top of the pixel to the bottom | clearer scanlines, a slight glow; the face curves left to right only (flat vertically, as a Trinitron's cylinder), a slight vignette |
| PVM (`"pvm"`) | a studio (broadcast) monitor | the same aperture grille, with darker gaps between the stripes where the screen has room for them | the sharpest beam, pronounced scanlines with distinct dark gaps, almost no glow, a flat face |
| Shadow mask (`"shadow"`) | a dot-triad (delta) shadow-mask set | dots instead of stripes: a row of red, green and blue dots in the upper half of the line and a second row shifted one stripe sideways in the lower half | a soft beam and glow, a gently curved face |

The parameters each mode uses (`port/render/rd_crt.c`; the shader is
`port/shaders/crt.hlsl`):

| parameter | Scanlines | Consumer TV | Trinitron | PVM | Shadow mask |
| --- | --- | --- | --- | --- | --- |
| scanline strength | 0.50 | 0.35 | 0.45 | 0.60 | 0.40 |
| beam width, dark to bright (lines) | 0.6 to 1.0 | 0.7 to 1.2 | 0.5 to 1.0 | 0.4 to 0.9 | 0.6 to 1.1 |
| mask | none | slot | aperture grille | aperture grille | dot triads |
| mask strength (how much of the other two colours a stripe holds back; 1 = pure stripes) | | 0.40 | 0.50 | 0.60 | 0.45 |
| halation | 0 | 0.12 | 0.05 | 0.03 | 0.08 |
| bloom | 0 | 0.15 | 0.10 | 0.05 | 0.10 |
| curvature x, y | 0, 0 | 0.030, 0.045 | 0.030, 0 | 0, 0 | 0.020, 0.030 |
| corner radius (of the height) | 0 | 0.03 | 0.02 | 0.01 | 0.02 |
| vignette | 0 | 0.15 | 0.08 | 0.05 | 0.10 |
| gamma in, out | 2.2, 2.2 | 2.4, 2.2 | 2.2, 2.2 | 2.2, 2.2 | 2.2, 2.2 |

**How it works.** The filter draws the 4:3 box (or the wide one) itself,
in place of the usual scaling, from the PS2's own pixel grid: 512 pixels
across (683 at 16:9: the grid is widened with the aspect) by the PS2's 256
lines (512 with `full_height`). Each line is not doubled: the scanlines
are the PS2's own lines. Every screen pixel of the box is worked out on
its own: its position (bent by the curve in the curved modes) falls in one
PS2 pixel and one PS2 line, and

- across the PS2 pixel it lands on one phosphor. The pixel's width on the
  screen is split into three equal stripes, red, green and blue from the
  left, and the screen pixel shows that stripe's colour of the PS2 pixel in
  full and the other two at 1 - the mask strength of theirs (at Consumer
  TV's 0.40 a red stripe of a white pixel is full red with 60 % green and
  blue). At mask strength 1 the stripes are pure: a pure red PS2 pixel
  lights only its red stripe, a white one all three, and every screen pixel
  is exactly one colour channel of one PS2 pixel. Nothing is scaled
  afterwards: every screen pixel is one PS2 pixel seen through one
  phosphor (before the glow is added). When a PS2 pixel is 4 or more screen
  pixels wide its last screen column is a gap between triads (at the same
  1 - strength), from 6 the last two;
- down the PS2 line it gets the beam: the line seen at that height (and a
  little of the lines above and below, which a wide bright beam reaches),
  a bell curve whose width grows with the colour's brightness;
- the mask strength is also the darkness of the gaps between phosphors:
  of a gap column, of the slot mask's bridges. Each triad keeps its own PS2
  pixel's light, colour by colour: the stripes are brightened by what the
  mask takes away, worked out from the screen columns that PS2 pixel
  actually has (2 or 3 at 1440 x 1080, so a triad short of a stripe is not
  tinted), so bright colours can reach the stripes' limit and look a
  little dimmer than without the filter. The beam is mixed with the flat
  pixel by the mode's scanline strength, as before.

Halation (a wide, faint glow of all the light) and bloom (a narrower glow
of the bright parts) come from a half-size blurred copy of the picture and
are added on top; the corners are darkened and the output gamma applied.

**The 1x rule.** A CRT of the time showed the PS2's pixels, so while the
filter is on the 3D scene is drawn at the PS2's resolution (1x) in the
Enhanced preset whatever `resolution` says. Settings > Display >
"Resolution" then reads "1x (CRT)", is greyed and does not change; the
`resolution` in `config.toml` is left as it was and applies again when the
filter is turned off (or its strength set to 0 %).

**Stripe widths.** A PS2 pixel is the box's width divided by the grid's
width screen pixels across, rarely a whole number:

| window | box (4:3) | screen pixels a PS2 pixel | stripes |
| --- | --- | --- | --- |
| 1280 x 720 | 960 x 720 | 1.88 | the mask faded out (below) |
| 1920 x 1080 | 1440 x 1080 | 2.81 | 1 or 2 pixels wide |
| 2048 x 1536 | 2048 x 1536 | 4 | 1 pixel each and a gap column |
| 2560 x 1440 | 1920 x 1440 | 3.75 | 1 or 2 pixels wide |
| 3840 x 2160 | 2880 x 2160 | 5.63 | 1 or 2 pixels and a gap column |

Where the width is not a whole number some stripes are one screen pixel
wider than the others (and some triads have only two of their stripes),
in a pattern that repeats across the picture; each triad still keeps its
pixel's colour. At exact multiples (a 1536- or 2048-wide
box) every triad is the same. Below a 1080-line box the stripes would be
under a screen pixel each, so the mask fades from full at a 1080-line box
to nothing at 720 lines and below (the beam and the glow stay). The slot
mask's bridge covers the last third of each PS2 line (half a line later in
every other column); the shadow mask's second row of dots, the lower half
of each line, is shifted by one stripe (half a triad, rounded down to a
whole stripe so the dots stay on whole screen pixels at 3 pixels a PS2
pixel).

**The UI under the filter.** With the filter on, everything on screen goes
through it: the game's menus and the port's Settings menu are drawn into
the PS2-sized picture as the Original preset draws them (the Enhanced
preset's sharp port text, "Menu text" above, is not used), and the port's
own popups, hint lines and photo mode's help lines are drawn into the
PS2's pixel grid at the PS2's scale before the filter, so they get the
same phosphors, scanlines and curve as the picture. Nothing is drawn on top
of the tube. A picture saved in photo mode therefore has the filter and,
if it was on screen, the help lines or a popup. The movies (the opening
and the ending) are drawn by their own path and are shown without the
filter.

**What it does not change.** The filter happens when the picture is shown:
the game's frames, F12 screenshots of DISPLAY, frame dumps and the replay
tool's DISPLAY and SCENE images are the same with it on or off (except
that the scene is drawn at 1x while it is on), and with it off (or at 0 %
strength) the shown picture is byte for byte what it was without the
option. The strength eases every part in together: the phosphors, the
glow, the curve, the vignette and the gamma.

**Cost.** Three small passes at half the PS2's size (the glow) and one
pass over the box: per screen pixel three reads of the PS2 picture (the
line and its neighbours) and five of the glow. At 1440 x 1080 that is
about 12 million reads a picture, a small part of a picture's work on a
desktop GPU; it has not been timed on hardware.

**Overrides.** `config.toml` also takes, under `[video]`, `crt_scanlines`,
`crt_mask`, `crt_halation`, `crt_bloom` (each 0 to 1) and `crt_curvature`
(0 to 0.25): each replaces that parameter of the mode (`crt_mask` is the
mask strength (1 for pure stripes), and on the Scanlines mode adds an
aperture grille;
`crt_curvature` is the x curvature, y being 1.5 times it except on the
Trinitron's flat vertical). A negative value, or no key, keeps the mode's
own. They have no Settings row.

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
lines or any popup (with the CRT filter on, the help lines and any popup
on screen are part of the filtered picture and are in the saved one too): `ico-<date>-<time>.png` in the `screenshots` folder of
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

Photo mode has no depth of field: the picture stays sharp.

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
