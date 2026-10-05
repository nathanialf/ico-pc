# Display options

How ico-pc draws the picture, and what you can change. The settings live in
`config.toml` (docs/port/CONFIG.md says where that file is) under `[video]`;
the Settings menu changes the same values while the game runs.

## The two presets

**Original** (the default) shows the game as the PlayStation 2 showed it:
the 512-line picture, halved to 256 lines and shown with every line twice,
in a 4:3 frame, textures filtered exactly as the game asked. Nothing below
except `fullscreen`, `vsync` and `framerate` changes it (`framerate` only
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
```

| key | what it does |
| --- | --- |
| `preset` | `"original"`: the PS2 picture. `"enhanced"`: the options below apply. |
| `resolution` | How sharp the 3D scene is (Enhanced). `"window"`: as many pixels as the frame has on screen. `"2x"`: twice the PS2's resolution in each direction (widened with the aspect). `"1920x1440"`: that many pixels. At least the PS2's resolution, at most 4K (3840 x 2160). Effects such as blur and glow keep their size on screen. |
| `aspect` | The shape of the picture (Enhanced). `"16:9"` and `"16:10"` show more of the world to the left and right; the menus, subtitles and the title text stay in a 4:3 frame in the middle; full-screen fades, the cinema bars and the film grain stretch across. `"auto"` follows the window, between 4:3 and 16:9. The movies stay 4:3 with bars at the sides. |
| `fullscreen` | Borderless fullscreen at the desktop's resolution. Alt+Enter switches while playing. |
| `vsync` | Wait for the screen's refresh: no tearing. Off lets frames show as soon as they are ready. |
| `texture_filter` | (Enhanced) `"trilinear"` gives textures smaller versions for distant surfaces, so the ground and walls do not shimmer; `"anisotropic"` also keeps them sharp at grazing angles. Textures the game draws unfiltered (pixel-sharp) stay that way. Fences and leaves with see-through parts keep their thickness in the distance. |
| `full_height` | (Enhanced) Keep all 512 lines of the scene instead of halving them, so the picture is not line-doubled. |
| `framerate` | (Both presets) How often the picture is redrawn. `"original"`: once for each of the game's 25 (PAL) or 30 updates a second, as on the PS2. `"uncapped"`: as often as the screen refreshes (with `vsync`) or as fast as the computer can (without), drawing in-between pictures so movement is smooth. A number such as `"60"` or `"144"`: at most that many pictures a second. |

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
motion-blur trail keeps the same length at any frame rate.

This applies to both presets: with Original, each of the game's updates is
still drawn exactly as on the PS2 and only the pictures in between are
blended. `framerate = "original"` keeps one picture per update, the PS2's
cadence, for either preset.

On a slow computer (or a software renderer) the game draws one picture per
update, as with `"original"`, rather than slowing the game down.

Turning things turn in between pictures: a character or object that spins
between two updates is drawn at the in-between angle at its full size (in v0.3
version 0.3 a fast turn shrank it slightly half way, which looked like a
wobble). How far between the two updates each picture is drawn follows a
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

`framerate = "original"` presents each update once, as before, and with
`vsync` that present waits for the refresh, as it did on the PS2.
`window:` lines in `logs/ico-pc.log` say which mode is in use (`present
mode mailbox` or `fifo`) and, every 10 seconds, how many pictures and game
updates there were and where the renderer's time went (docs/port/
RENDER_API.md section 22; `[dev] perf_log = true` writes one line per
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
- Technical details: docs/port/RENDER_API.md sections 19 (presets, the
  options), 20 (frame rate) and 22 (performance).
