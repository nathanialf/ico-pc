# Display options

How ico-pc draws the picture, and what you can change. The settings live in
`config.toml` (docs/port/CONFIG.md says where that file is) under `[video]`;
the Settings menu changes the same values while the game runs.

## The two presets

**Original** (the default) shows the game as the PlayStation 2 showed it:
the 512-line picture, halved to 256 lines and shown with every line twice,
in a 4:3 frame, textures filtered exactly as the game asked. Nothing below
except `fullscreen` and `vsync` changes it. In a window that is not 4:3 the
picture gets black bars on the sides (or top and bottom).

**Enhanced** turns on the options below. Each one is separate: Enhanced with
`resolution = "1x"`, `aspect = "4:3"`, `texture_filter = "original"` and
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

`framerate` (smooth motion between the game's 25 or 30 updates a second)
is a separate option, described with it.

## What stays as on the PS2

- What you can see and what the game considers "on screen" for its own
  decisions: a wider picture shows more, but the game keeps using the 4:3
  frame for everything it checks.
- The colours of the effects that the PS2 computed in a particular way
  (motion blur, glows, fog) are computed the same way at any resolution.
- The menus' layout, placed in the middle 4:3 frame.

## Notes

- Changing `resolution`, `aspect` or `full_height` while playing clears the
  motion-blur trail for one frame.
- On a renderer backend without mipmapped textures, `texture_filter` falls
  back to the original filtering.
- Technical details: docs/port/RENDER_API.md section 19.
