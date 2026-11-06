# Display, pacing and presentation

How a [tick](ARCHITECTURE.md#terms)'s picture reaches the screen: the
[native frame](ARCHITECTURE.md#terms) the game builds, the display options and their
[presets](ARCHITECTURE.md#terms), the SDL window, the real-time pacing of the simulated
[vsyncs](ARCHITECTURE.md#terms) (the [vsync lock](ARCHITECTURE.md#terms) and its display clocks),
the [present](ARCHITECTURE.md#terms), [interpolation](ARCHITECTURE.md#terms), the CRT filter and the
log lines that describe all of it. What the renderer draws is [RENDER.md](RENDER.md); the backends
are [RHI.md](RHI.md).

## Files

| path | what it holds |
| --- | --- |
| `port/include/ico_frame.h` | the native frame: its width for an aspect, the exact width, the TEX0 helpers |
| `port/game/video_options.h`, `video_options.c` | the `[video]` keys, the presets, Auto aspect, the frame for the options in force |
| `port/platform/window_host.h`, `window_host.c` | the SDL window, the device on it, the pacing loop, the 10 s stats lines, F11 and F12 |
| `port/platform/window_video.h`, `window_video.c` | window mode requests (windowed, borderless over the display, fullscreen at the desktop's mode) and their readback |
| `port/platform/pace_policy.h`, `pace_policy.c` | the pacing decisions, free of SDL: slow presents, the gap, the vsync lock, the display's intervals, Auto resolution |
| `port/platform/present_fallback.h`, `present_fallback.c` | the FIFO fallback's decision and lines, free of SDL ([The FIFO fallback](#the-fifo-fallback)) |
| `port/platform/window_present_fallback.h`, `window_present_fallback.c` | the window's side of the FIFO fallback: the gate, the switch, the lines |
| `port/platform/display_rate.h`, `display_rate.c` | the refresh rate's log lines and the frame rate pin's decisions, free of SDL ([The refresh rate and the frame rate pin](#the-refresh-rate-and-the-frame-rate-pin)) |
| `port/platform/window_display_rate.h`, `window_display_rate.c` | the window's side: SDL's display mode in the log, the Android pin through the NDK |
| `port/platform/present_hist.h`, `present_hist.c` | the 10 s block's histogram of the driver's present call (over 20, 50, 100 and 300 ms); the texts of the lines around a present that blocked (its split, the replay after it) |
| `port/platform/window_stall.h`, `window_stall.c` | the window's lines around a present that blocked (issue 123), from the replays' records as they pop; the kernel GPU driver's lines on Android |
| `port/platform/gpu_sysfs.h`, `gpu_sysfs.c` | the kernel GPU driver's nodes, the pure part: the parser, the ring of samples, the block and around reductions, the lines ([ANDROID_DIAGNOSTICS.md](ANDROID_DIAGNOSTICS.md) has the sampler) |
| `port/platform/window_disc.h`, `window_disc.c` | the window's side of the disc reader: the step's disc wait for the present allowance, the 10 s disc line |
| `port/platform/window_blended.h`, `window_blended.c` | F12's blended present's frame: the arm after F12's zip (a result still to collect collected first) and the move into it |
| `port/platform/display_clock.h` | the vblank clock's interface |
| `port/platform/display_clock_win.c` | the vblank clock on Windows: a thread on the window's DXGI output |
| `port/platform/display_clock_none.c` | no vblank clock (every other platform) |
| `port/platform/perf_block.h`, `perf_block.c` | a 10 s block of replays by phase (the replays line and its maxima) |
| `port/platform/hotkeys.h`, `hotkeys.c` | F11, F12, Shift+F12 and the stats period |
| `port/render/rd_present.c` | DISPLAY to the output: the box, line doubling, the overlay, the display options as scales |
| `port/render/rd_prerotate.c` | the present into a swapchain made in the surface's own turn ([The turned swapchain](#the-turned-swapchain)) |
| `port/render/rd_interp.c` | presenting between ticks |
| `port/render/rd_present_clock.c` | the present clock (`rd_present_clock_alpha`), what it notes for `rd_present`, Motion blur: Smooth's presents a tick |
| `port/render/rd_crt.c`, `port/shaders/crt.hlsl` | the CRT filter |
| `port/render/rd_tone.c`, `port/render/rd_tone.h`, `port/shaders/tone.hlsli` | the colour adjustment (Options > Picture's Gamma, Picture brightness, Contrast, Saturation; [Presentation](#presentation)) |
| `port/platform/fps_counter.h`, `fps_counter.c` | the Frame rate counter's presents per second, free of SDL and rd ([UI.md](UI.md#the-frame-rate-counter)) |
| `port/platform/test/` | `fps_counter_test.c`, `pace_loop_test.c`, `pace_policy_test.c`, `present_fallback_test.c`, `display_rate_test.c`, `present_hist_test.c`, `perf_block_test.c`, `window_video_test.c`, `hotkeys_test.c` |

## The native frame

The game builds each tick for a [GS](ARCHITECTURE.md#terms) window as wide as the display aspect
asks, with the PS2's height and vertical field of view. The projection never changes with the
aspect: a world point lands on the same GS X at every width, about the centre 2048; only the
window's extent grows (`port/include/ico_frame.h`, header comment).

| field | meaning |
| --- | --- |
| `w` | the native GS width, `16 * ceil(32 k)` with `k = aspect / (4/3)` clamped to 1..5: 512 at 4:3, 688 at 16:9, 2560 at most |
| `h` | `ScreenHeight`: 448 NTSC, 512 PAL |
| `fbw` | `(w + 63) / 64`, the FRAME and TEX0 FBW of a frame-sized buffer |
| `wExact` | `512 k`, the visible width with Full pixel on and off; the presenter crops `(w - wExact) / 2` columns a side |
| `aspect` | the aspect the frame is built for, 4/3 exactly when `w` is 512 |

(`port/include/ico_frame.h` (`IcoFrame`, `ico_frame_width_for`, `ico_frame_make`)). The width is a
multiple of 16 so the frame's halves, quarters and eighths start their 4:3 part on a whole pixel,
and `32 k` is taken 1e-4 short of the next whole number so the float error of a listed aspect does
not round up a step. `ICO_FRAME_K_MAX` (5) makes 60:9 the widest frame and `ICO_FRAME_ASPECT_MAX`
its aspect (`port/include/ico_frame.h`). `ico_frame_x` scales a 4:3 length to the frame's width and
`ico_frame_tex0_w` gives the TBW and TW of a work buffer sized "per 512 columns"; at 4:3 these are
the PS2's own fields (`port/include/ico_frame.h` (`ico_frame_tex0_w`)).

`ico_video_frame` builds the frame from the options in force; `ico2/seki/src/GsBase.c` latches it at
each flip as `gsFrame`, which every frame extent of the next tick reads.
`port/shaders/shader_consts.h` mirrors the constants as `ICO_SH_FRAME_*`, and `rd_present_test`
asserts they agree (`port/include/ico_frame.h`, header comment; `port/game/video_options.c`
(`ico_video_frame`)).

**[Full pixel](ARCHITECTURE.md#terms)** (`[video] full_pixel`) makes the picture the window's own
pixels: the present box has the frame's exact width (`wExact / 384`), and with the "window"
resolution and a frame wider than its exact width the scene's scale is the box's width over the
exact width (2.8125 at 1080p 16:9), held to a 1/4096 step when that keeps the box's width, and the
scene-class targets hold the exact width alone, so one texel is one output pixel and nothing is
cropped; the reduction draws over its border (`port/render/rd_blur_border.c`
(`rd__blur_post_sprite`)). Where the frame is its exact width (4:3, 48:9, 2340x1080's 832 columns:
`rd__frame_cropped` false) nothing of it is cropped, so the PS2's hidden columns, GS 0, 1, W - 2 and
W - 1, would be on the screen; the game does not always draw them (issue 129: a bright band at a
phone's left edge, the fog's colour at depth 0 spread by the glow), so the reduction draws them
again as columns 2 and W - 3, bit for bit in the GS model ([RENDER_POST.md](RENDER_POST.md#post-passes), the
open border's outer columns). The glow reads SCENE before the reduction, so a fogged frame's SCENE
has those columns repeated from columns 2 and W - 3 first, wherever the border is open or the
frame cropped (issue 26: the same band at 16:9, where the present leaves the columns out but the
glow had spread them about ten GS pixels in); with the border closed (Full pixel off at 4:3 and
48:9) SCENE stays the game's (the camera's: the console's except inside triangles it dropped,
VD-29) ([RENDER_POST.md](RENDER_POST.md#post-passes), SCENE's outer
columns). The rows the reduction's scissor kept black at the top and bottom show what SCENE
holds.
Without it the picture is the same: the same box, scale, crop and replay (the host sets
`RdSettings.borderMask`, and the reduction draws over its border as with the option on,
`port/render/rd_replay.c` (`borderOpen`)), and the presenter masks the game's border over its edge at
the picture's own scale, where the PS2's reduction scissor held it black (`port/render/rd_post.c`
(`postReduction`)): 2 GS columns at each side, `round(2 k)` output pixels with `k` the box's width
over the exact width (6 at 1080p 16:9, 5.625 exactly; 11 at 4K), and `crop / (h / 2)` of the box's
height at the top and bottom, `crop` 2 rows, or 8 at 512 lines (`rd__reduction_crop`; 10 rows at
1080p, 19 at 4K, 34 at 1080p and 50 Hz), and none on the Japanese and Korean discs, whose reduction keeps every
line (`port/core/region.h` `reduction_crops_rows`), so its mask is the side columns alone. The mask is the box blit's scissor inset by the bands, the
viewport the box's, so the cleared black stays in them; the effects depth blit and the CRT filter's
last pass take the same scissor (the tube's box at its grid's scale), and the deferred text is
clipped to it (`port/render/rd_present.c` (`rd__border_mask`, `blit`, `depthBlit`, `textRegion`),
`port/render/rd_crt.c` (`compose`)). Nothing is squeezed or moved. The mask is the presenter's alone:
DISPLAY, the F12 frame `.png` and a dump have no border; the F12 `-present.png` shows it. A zeroed
`RdSettings` (the tests' PS2 models, the [replay tool](ARCHITECTURE.md#terms) without
`--border-mask`) keeps the reduction's recorded scissor instead, the GS's own DISPLAY, in the same
box. The mask and the open border apply only on a cropped frame (`rd__frame_cropped`: `gsW` over
512 and `wExact` under it, as at 16:9, 688 over 682.67, where the crop cuts the game's own border
away); at 4:3 and at aspects where the frame is exactly the picture's width (48:9 at 2048), Full
pixel off is the game's own border as before, the reduction's recorded scissor and no mask, so the
Original preset at 4:3 is the camera's: the console's except inside triangles it dropped, [DIVERGENCES.md](DIVERGENCES.md) VD-29; byte for byte the PS2's under `[dev] region_rule = ps2` (`port/render/rd_present.c`
(`rd__frame_cropped`), `port/render/rd_replay.c` (`borderOpen`)). The `rd: display:` line ends
with the frame as the options give it, `frame W GS wide (exact E: cropped|exact width), border
open (its outer two columns a side repeat their neighbours)|open (its columns left out at
present)|closed`, and `rd_log_frame_state(when)` logs the same words with the output's size, the
size asked for, the scene's scale, the aspect, how many times the outer columns were drawn as
their neighbours and how many times SCENE's were repeated: `rd: frame state (after resume): output 2340x1080 (asked 2340x1080), scene
2.8125x2.10938, aspect 2.167, frame 832 GS wide (exact 832.00: exact width), border open (...),
the outer columns drawn as their neighbours N times, repeated on SCENE after the fog M times`
(M the frames whose SCENE had its outer columns repeated). The window host calls it when the app comes
back to the foreground, after reading the window's size again (a size a few pixels off the
frame's exact width crops the frame, which hides those columns), with the line `window: after the
app came back: window WxH px, safe area x,y wxh` (`port/platform/window_host.c`
(`resume_recheck`, `window_size_recheck`); `port/render/rd_present.c` (`frameWords`,
`rd_log_frame_state`)). The box (`wExact / 384`), the UI box, the pointer, the movie's box and the CRT filter's grid are
the same with the option on and off (`port/render/rd_present.c` (`outputBox`, `rd_ui_box`,
`rd__ui_box`, `rd__apply_display`, `sourceCrop`), `port/render/rd_crt.c` (`rd__crt_crop`); the
targets: [RENDER.md](RENDER.md#frames-and-targets)).

**A draw that samples a render target** clamps at the target's edges while the border is open
(Full pixel on, or the mask on a cropped frame: `borderOpen`): the game leaves its screen-space
lookups (the mirror floors' copy of the scene, the reflections) on REPEAT, and at the frame's outer
columns they wrapped to the far side of the picture, which the PS2's border hid
(`port/render/rd_replay.c` (`drawWrap`); ctest `rd_present` (`checkFullPixelTargetWrap`)).
The clamp is for the copies of the frame read in screen space (the pool's and the queen barrier's
grids over their copies of the scene, SCENE, the work buffers): a screen prim reading a
render-to-texture block smaller than the frame keeps the game's REPEAT whatever its coordinates
(`port/render/rd_water.c` (`rd__block_keeps_repeat`);
`DrawSetup.screenPrim`). The puddle's ripples (`ico2/sugipon/src/puddle.c` (`drawRipple`)) sample
the reflection block at STs in later periods of the REPEAT, a ring often across a period's end;
clamped, those fragments took the block's edge and corner texels, so the ring's segments came out
flat and bright (issue 106: 2089 and 3014 near-white pixels in the ring areas of the reporter's
2026-10-24 dumps at their settings; a rule on the coordinates alone missed a ring across a period's
end). The puddle's copy over the frame reads the block inside its edges, the same either way. A
frame-sized block (the dark volume's) and every VU draw keep the clamp. The `--list` water line and
the F12 line name the counts: `Full pixel on, border mask off: N render-target reads clamped, M
block reads kept on REPEAT` (ctest `rd_water`, the ripples; `rd_replay_tool_ripple`,
`rd_replay_tool_ripple_447`, `rd_replay_tool_ripple_436`, `rd_replay_tool_mirror_floor`).

**The [scene scale](ARCHITECTURE.md#terms)** comes from the Resolution row in `rd__apply_display`:
"Nx" is the GS size times N; "WxH" that size; "window" the presentation box (only with the Enhanced
flag, so a zeroed `RdSettings` stays the GS size); otherwise the GS size. The CRT filter forces 1x.
Supersampling multiplies the scale by the factor the picture still lacks (`ceil(factor / t)`, `t`
the texels an output pixel on the sparser axis), then both axes are held to the GPU's largest render
target (4096 on a tile-based GPU) with the shape kept; MSAA on a tile-based GPU halves the scale
until the multisampled pair fits `RD_MSAA_TILER_BUDGET`. The work buffers keep the PS2's sizes
at every scale ([RENDER.md](RENDER.md#the-glow-size)). A change logs one `display: scene ...`
line (`port/render/rd_present.c` (`rd__apply_display`)).

## Video options and presets

`port/game/video_options.h` holds the `[video]` keys in one place, so the window, the game's native
frame and the Settings menu read and write the same values (`port/game/video_options.h`, header
comment). Each key's type, default and values are [CONFIG.md](CONFIG.md#video)'s; a `WxH` resolution
is 64..7680 by 64..4320 (`port/game/video_options.c` (`read_config`, `ico_video_parse_resolution`)).
"Mobile" is the Android and iOS builds (`port/game/video_options.c` (`s_android`)).
`[video] backend` picks the [RHI](ARCHITECTURE.md#terms) backend (`port/platform/window_host.c`
(`ico_window_open`)).

**The preset.** A preset is not an option but a shortcut over four rows: resolution, aspect, texture
filter and height. `ico_video_preset` derives it: Original when the rows are 1x, 4:3, original and
half; Enhanced when they are window (or auto on mobile), auto, anisotropic and full; Custom
otherwise. `ico_video_set_preset` writes the four rows; Original also turns anti-aliasing and Full
pixel off, Enhanced turns Full pixel on (`port/game/video_options.c` (`ico_video_preset`,
`set_preset`)). On reading the config only `enhanced` and `custom` take the rows as written;
`original`, no key or a misspelling puts them at the PS2's values whatever the file says, keeping a
saved Full pixel. Full pixel without its key is on when the rows add up to Enhanced
(`port/game/video_options.c` (`read_config`)). With no `config.toml` the preset is Original, so the
four rows are 1x, 4:3, original and half; an absent `resolution` key reads as `window` (`auto` on
mobile) only under `enhanced` or `custom` (`port/game/video_options.c` (`ico_video_defaults_for`,
`read_config`)). `ico_video_save` writes `original` when the rows are the PS2's, else `enhanced`
with the rows, which read back as Enhanced or Custom (`port/game/video_options.c`
(`ico_video_save`)). `ico_video_original` is the test for [out of the box](ARCHITECTURE.md#terms):
`GsBase.c`'s frame latch hands its opposite to `GifPacket.c`, and the window gives
[rd](ARCHITECTURE.md#terms) the same test as `RdSettings.preset` (`port/game/video_options.h`
(`ico_video_original`); `port/platform/window_host.c` (`video_settings`)).

**The AA lock.** `ico_video_anti_alias` returns off whenever the preset is Original, whatever the
key says; the renderer draws an MSAA count the device lacks as off (`port/game/video_options.c`
(`ico_video_anti_alias`); `port/platform/window_host.c` (`video_settings`)).

**Downsampling.** `downsample` (`"off"` or `"box"`) is read and saved outside the preset, as
`show_fps` is, and reaches the renderer as `RdSettings.downsample`; the renderer decides where it is
in force ([downsampling](#downsampling)).

**Fallbacks.** A key the parser does not understand logs one `video: ... not understood` line and
takes the default (resolution, aspect, texture filter, window mode, anti-aliasing,
frame rate, CRT mode); the texture pack sizes are clamped with a line; `window_mode` absent reads the `fullscreen`
bool. Every run-time change goes through `sanitize`, which puts out-of-range values back to the
defaults and clamps the CRT values (`port/game/video_options.c` (`read_config`, `sanitize`)). A
backend that is not in the build falls back to Vulkan with a line (`port/platform/window_host.c`
(`ico_window_open`)).

**Auto aspect** is the window's width over its height clamped to 4:3 .. 60:9; with no window size
yet (0 x 0, the [headless build](ARCHITECTURE.md#terms)) it is 4:3 (`port/game/video_options.c`
(`ico_video_aspect`)). The window passes its size on open and on every resize
(`ico_video_set_window`).

**Frame rate.** `original` presents once per tick, each picture held for the tick's refreshes;
`uncapped` presents as often as the display allows and interpolates; a number does the same at most
that many times a second. It applies whatever the preset (`port/game/video_options.h`, header
comment; `port/game/video_options.c` (`ico_video_interpolate`)).

**Run-time changes.** `ico_video_set` bumps `ico_video_serial`; the window compares it at each pump
and applies the change (`video_apply`): the window mode through SDL (asked again when the option
differs from the window's state; never on a resize's
forced call, so a refused request is not made again at every size event), the rest through
`rd_set_settings` at the next frame, which recreates targets and the swapchain as needed. A change
also logs one `window: display changed:` line naming only the fields that differ
(`port/game/video_options.c` (`ico_video_changes_text`); `port/platform/window_host.c`
(`video_apply`, `video_log_changes`)).

What a display change derives again, as a fresh start at the new options would (a PowerVR
DXT-48 phone's log, below):

- **The present allowance and the GPU times** ([the present allowance](#the-present-allowance)):
  the timer path's allowance starts unbound again, a lock starts again, and the GPU time the timer
  path's climb gate reads (the median of the last 8 replays) is emptied; the `display changed` line ends
  `; the present allowance (held to N a step|not held) and GPU times start again`
  (`video_log_changes`). The phone: at Custom 2x its GPU (24-29 ms a picture) held the presents to
  0 a step; after the switch to Original the hold stayed, every replay a tick's first (the glow's
  chain drawn at each, 22 passes, none pasted between ticks), the GPU at 19.5-21 ms a presented
  replay (each pass 1.2 to 1.7 times its time in a fresh start at Original on the same menu, the
  same draws: the GPU's clock under the lighter load is the likely reason, not proven from the
  log) against the gate's period of 16.7 ms, every climb refused (`climbs refused 10` in each
  10 s block), 30 pictures a second where a fresh start at Original showed 67-88. The renderer's own counts were the fresh start's (939 against 940 draws a
  replay, the same alpha plan, no no-planes pass; the glow and fx plan counts higher by the chain
  drawn at every replay).
- **The renderer's state** (`rd_begin_frame`, `port/render/rd_core.c` (`logDerived`)): the scene
  scale and the targets, the preset's rules (the VU region rule, the planes at a fractional
  scale, GS levels at Original, the crack fill's reach) are read from the settings at each draw
  or set by `rd__apply_display`; a change of scale or preset logs them in one line, `rd: display
  change: derived: preset P, frame W GS wide, scene SxS (whole|fractional), VU planes kept|dropped
  (vu_np_ps), VU region rule the camera's[ (extended)]|the PS2's|extended (the camera's under the
  default `[dev] region_rule = camera`, [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-camera-rule)), texture filter F, VU pre-pass on|off, fog path N;
  profile ...`, so a reporter's log after a change compares with a fresh start's lines.
- **The pipelines** (`port/render/rd_pipeline.c` (`rd__display_pipelines`)): after the settings
  apply (and after a frame-width reset, `rd_reset_scene`), the reachable set of the new state
  (`rd__enumerate_reachable`: the preset, the scale and with it the `vu_np_ps` twins of a
  fractional scale, the crack fill's and the blur fold's keys; the above-1x sprites' kernel is a
  draw constant, no key) is held
  against the cache and the keys it lacks are made as the start-up set's are: in batches on the
  driver's threads (`createKeys`), under the window's policy (a careful device's batches of 8, its
  memory budget, the system's low-memory signal), the pipeline cache written after them on a phone,
  before the frame replays. The window shows its graphics screen meanwhile ("Changing the display",
  "Preparing graphics (n of m)", after 250 ms) and takes the window's events
  (`rd_set_display_progress`, `display_progress`); each pipeline finished counts as progress for
  the watchdog, which sees no Main tick meanwhile (`ico_diag_note_progress`). One line: `rd: display change: N pipelines of
  the new state made in M batches, X ms (K of its T keys missing, ...)` (with `0 ... (its T keys
  made already)` when the targets were made again and nothing was missing), the slowest compiles
  after it; a key that still misses is made on first use with its `made on first use` line. A run
  without a start-up set (`rd_pipelines_lazy`) stays on first use. The phone: Original -> Enhanced at
  2364x1080 (scale 2.8125 x 2.41) made the twins on first use, 640-700 ms each on the main thread;
  the heartbeat stopped advancing and the log ended without an exit line.
- **Not derived again** (no state of the old options): the blur fold's "reported once a width"
  memos are log limits, not decisions; the fold is decided per replay from the commands
  (`rd_replay_tiler.c`). The phone's log after the switch to Original has, once each, `blur fold:
  WORK1 256x256 ... pass 1 draws columns 1..255` (a fresh start at Original: `0..254`; at 2x the
  848-wide frame's pass 1 started at column 1) and `shadow command without a depth-stencil target
  of the colour target's size: skipped`, neither repeated and the replays' counts after them the
  fresh start's: read as one frame of the old width replayed on the new targets (not proven from
  the log).

**Auto resolution** (`resolution = "auto"`) starts the scene at the window's size and lowers it
while the presents take too long: every 2 s window of samples (at least 8), when the median cost is
above 70 % of the frame budget the scale steps down once: the window's size to the largest of 3x,
2x, 1x below the window's own scale (the box's height over 448), Nx to (N-1)x, anything above 3x to
3x; never below 1x, never back up (`port/platform/pace_policy.h` (`pace_auto_resolution_step`)). The
cost is a [replay](ARCHITECTURE.md#terms)'s GPU time without the uploads when the backend has
timestamps (Vulkan and D3D12: the sample is taken whenever the replay's timestamps
came back, so D3D12 under its FIFO present samples), else in mailbox mode its CPU time without
the acquire and the present; under FIFO without timestamps there is no sample. The budget is the cap's period, two fields at frame rate
Original, else 1/60 s; the rule is off under the CRT filter (`port/platform/window_host.c`
(`auto_res_sample`, `auto_res_update`, `video_settings`)).

## The window host

`ico_window_open` creates one SDL3 window titled "ICO", first 960 x 720 (4:3, three times 320 x
240), resizable and high pixel density on the desktop and always fullscreen on mobile, where
`window_mode` is ignored; a configured fullscreen mode is asked for at creation so the window
manager sees it before mapping. It picks the backend from `[video] backend`, makes the device
through `rd_init`, points the Vulkan pipeline cache at `pipelines-<key>.vkcache` in the [user
folder](ARCHITECTURE.md#terms) (the app's cache folder on Android; `<key>` the shader set's and the
pipeline format's, not the build's: [RHI.md](RHI.md#vulkan)), creates the whole reachable
pipeline set before the first frame, runs the fog's self-test and logs one start-up line with the
backend, adapter and options (`window mode M` among them, as in the summary line),
then `window: present mode M` (`port/platform/window_host.c` (`ico_window_open`, `window_flags`,
`log_summary`)). Before `rd_init` it sets the renderer's wait hooks
(`ico_window_switch_wait_hooks`, [RHI.md](RHI.md#wait-hooks)): OpenGL's shader compiles then run on
a thread of their own while the window takes its events, so the window stays responsive, and one
compile is held to the GPU hang limit. The renderer's "Preparing graphics" screen cannot be drawn
while the device's own shaders are made (it draws with them), so OpenGL draws a screen of its own
there with no shader at all: "Preparing graphics for OpenGL: N of M shaders" over the same bar,
from scissored clears ([RHI.md](RHI.md#opengl), `port/rhi/gl/gl_start_screen.c`), counted against
the renderer's plan of the set (`rhi_plan_shaders`, `port/render/rd_shader_plan.c`); the
renderer's own screen takes over at the pipeline set. The made hook writes the set's steps
(`window: shaders: the start-up set begins, 1 of M`, a line at each quarter, `the start-up set is
over, N made in S s`; `ico_diag_shader_set`) on every backend. Each step of the start and of Options > Graphics > Apply's switch
writes its line at once (`ico_diag_step`), and the switch runs under its own watchdog limit
([PLATFORM.md](PLATFORM.md#the-renderer-switched-from-the-menu), the in-process switch and its log lines).

**The window modes** (`port/platform/window_video.c` (`ico_window_video_mode`,
`ico_window_video_desktop_mode`)). Three distinct modes, as in other games:

- **Fullscreen** is an exclusive fullscreen at the desktop's own mode: `SDL_SetWindowFullscreenMode`
  with `SDL_GetDesktopDisplayMode` of the window's display (one of the display's modes, so no change
  of resolution or refresh rate) before `SDL_SetWindowFullscreen`. When the desktop mode is unknown or
  SDL refuses it, the mode is NULL, SDL's borderless desktop fullscreen. The request logs `window:
  fullscreen asks the desktop's mode WxH at R Hz (exclusive)` or `window: fullscreen without a mode
  (the desktop's mode is unknown|was refused: <SDL's error>); SDL's desktop fullscreen`. The window
  is not minimised when it loses focus (`SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS` "0" before
  `SDL_CreateWindow`; SDL's "auto" minimises an exclusive mode to restore the desktop's mode, which
  here is the mode in use). Wayland,
  gamescope (the Deck's Game Mode) and macOS handle a mode as they can; the mode line below says
  what the window holds. A configured Fullscreen is asked for at creation through the window's
  flag, and the desktop mode is set right after (`port/platform/window_host.c` (`ico_window_open`)).
- **Borderless** is a frameless window moved and sized to its display's bounds (on Wayland, which
  does not let a client place its window, a frameless maximised one), and a composed window: the
  swapchain asks the driver not to take exclusive presentation (below), so programs that draw over
  the window or scale it (Lossless Scaling, overlays) can hook it.
- **Windowed** is the framed window at the rectangle it had when it last left the windowed state
  (`remember_rect`), else 960 x 720 centred. It is composed too.

Every request waits for the window system and logs what the window became: `window: mode M (window
WxH pixels)`, for Fullscreen `(window WxH pixels, the desktop's mode asked (exclusive))` or `(window
WxH pixels, desktop fullscreen)` (`SDL_GetWindowFullscreenMode`), with `; the window manager did not
follow the request` when the mode read back differs.

**The swapchain's exclusive request** (`port/rhi/rhi.h` (`rhi_set_exclusive_fullscreen`)). A stored
flag the next swapchain creation reads: allowed for Fullscreen, disallowed for Windowed and
Borderless. The window host sets it from the window mode before each mode request (`video_apply`),
at creation before `rd_init` makes the first swapchain, and when the window manager changes the
window (`window_fullscreen_event`) (`port/platform/window_host.c` (`exclusive_follow`)). The
swapchain is made again by the size event that follows a mode change; a change between Borderless
and Fullscreen at the same pixels has none, so `video_apply` makes it again itself (`window: the
swapchain made again for the M mode at WxH`). Vulkan on Windows: the instance enables
`VK_KHR_get_surface_capabilities2` (unless SDL already lists it) and the device
`VK_EXT_full_screen_exclusive` when both are offered (`port/rhi/vk/vk_device.c`
(`vkr_create_instance`, `vkr_create_device`); `VkrState.fullScreenExclusive`), and
`vkr_swapchain_create` chains a `VkSurfaceFullScreenExclusiveInfoEXT` into the
`VkSwapchainCreateInfoKHR`, `VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT` or
`VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT`, and into the capability and present-mode queries the
swapchain is checked against (`vkGetPhysicalDeviceSurfaceCapabilities2KHR`,
`vkGetPhysicalDeviceSurfacePresentModes2EXT`, which the backend loads itself: volk declares it
only under `VK_USE_PLATFORM_WIN32_KHR`) (`port/rhi/vk/vk_swapchain.c` (`vkr_fse_chain`,
`vkr_surface_caps`, `vkr_present_modes_query`)). Without the request, the driver decides (the
extension's `VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT`: implementation-specific), and a driver may take
exclusive-style presentation for a frameless window that covers its monitor, which such programs
cannot hook. One line per swapchain creation whose state
differs from the last one logged: `rhi: swapchain exclusive fullscreen disallowed
(VK_EXT_full_screen_exclusive)`, `rhi: swapchain exclusive fullscreen allowed (the driver
decides)` or `rhi: swapchain exclusive fullscreen not available on this driver`
(`vkr_fse_log`). Without the extension (Linux, Android, macOS, lavapipe) nothing changes and
nothing is logged. D3D12 ignores the flag: its swapchain is DXGI's flip model in the window
(`port/rhi/d3d12/d3d12_device.c` (`rhi_set_exclusive_fullscreen`)).

After a graphics driver crash while the pipelines are built, the crash handler starts the program
again with `ICO_STARTUP_RETRY=<step>:<backend>`: `cache` deletes the loaded pipeline cache, `d3d12`
takes Direct3D 12 when the player chose no backend, `lazy` builds each pipeline when a frame first
needs it instead of before the first frame. `ico_window_open` takes the retry's backend unless the
player chose one, logs the step and skips the up-front pipeline set on `lazy`
(`port/platform/startup_retry.h`; `port/platform/window_host.c` (`ico_window_open`)).

`ico_window_pump` drains SDL's events once per vsync: the close button quits; F11, F12 and Shift+F12
are the diagnostic keys; Alt+Enter flips between Windowed and the last other mode (in memory, not
saved); a pixel size change reaches `rd_resize_output` and Auto aspect; a display change refits a
borderless window to the new display's bounds (`window: borderless refitted to its display`;
`ico_window_video_refit`), decides
the cap and present mode again and moves the vblank clock to the new
output (`port/platform/window_host.c` (`ico_window_pump`, `toggle_fullscreen`)). The window's
fullscreen state is read back from SDL, not the option, since the window manager can refuse a
request (`port/platform/window_video.h`; `port/platform/window_host.c` (`window_fullscreen_event`)).
A lost device shows one message box and ends the session (`device_lost_quit`).

## The stats lines

Every 10 s of real time (every second for 30 s after F11) the window logs a block of lines, in both
presentation modes (`port/platform/window_host.c` (`window_pace_log`); `port/platform/hotkeys.h`
(`ico_stats_period_ns`)).

**The first line** (`window: N presents, ...`): the presents, the movie presents (and how many
failed), the game frames shown and the frame numbers (which also count the frames the game dropped
unshown, so a gap between the two is not a slowdown), the block's seconds, presented and game frames
a second, the vsyncs and the simulated rate, the resyncs and the lag they dropped, the new frames
the late-present skip left unpresented (`skipped N presents (late L, slow S)`: by arm, the timer
path's and the lock's together, the lock having the late arm only, [Pacing](#pacing)); at Frame
rate Original `steps late N (frames not presented M)`, the steps that ended past their period and
the frames its late skip left out ([Pacing](#pacing), item 1), then the present
allowance of the path in use (`presents held to A a step (lowest L, R refused, D drops, C climbs,
climbs refused X)`, or `presents paced to N.N a step (...)` while the timer path's allowance is
unbound and [paced by the GPU](#the-present-allowance) at under 8 a step (N.N the budget's presents
a step, `period / G`), or `presents not held (...)` while it is unbound otherwise, or `vsync off,
uncapped: pictures as made (2 in flight at most[, the GPU's time would pace them to N.N a step])
(...)` with vsync off, the frame rate uncapped and the render thread on, where the allowance only
measures ([Further presents and the game step](#further-presents-and-the-game-step)), and
`lowest not held` when it was unbound all block: the allowance now and its lowest in the block, the
in-between presents it refused, its drops and climbs and the climbs its GPU gate refused in the
block (the timer path's; under the lock every climb is a probe, none refused); then `GPU span G ms
a timed replay`, the median of the last 8 timed presented replays' GPU time (`perf_drain`,
`pace_hist_add`) that the timer path's climb gate, the paced budget and the step rule's pipe read,
not the block's mean: it is the value at the block's end, so a block that ends on a stage's loading
pictures reads low (a phone at the Original preset: `GPU span 1.9 ms` where the block's replays
averaged 14.76 ms and the step rule's asks read 13.4 on average, its sum line's `the GPU 13.4 ms a
picture`). It is the replay's stamp span on every backend (`RdPerfRecord.gpuMs`, the first stamp to
the present's end: the GPU's waits for the CPU's submissions are in it), hence `span`; a busy
time would read `GPU busy` (`pace_budget_block_text`'s `gpuIsSpan`), and OpenGL's busy clock is
only in the replays line's pass-timed replays (the replays line's `GPU` is the mean over the block's replays: issue 130's log had this
field at 14.1 ms, then named `a presented replay`, beside a block mean of 1.44 ms), or `no
GPU timestamps` where the backend has none, or `GPU not timed yet`;
[The present allowance](#the-present-allowance)), the longest [replay](ARCHITECTURE.md#terms) and the replay count with how many
were over a refresh, the slow
steps, the frame rate option (with `(held to the display's X Hz)` while a cap above the refresh
presents once a refresh, [the present gap](#pacing)), the present mode with, on Vulkan, the
swapchain's images and those the game holds as the block ends (`present mailbox (images: 10, in use
1)`: acquired and not yet presented, `RhiStats.swapAcquired`; the hand-over's count while the
render thread is busy; images the compositor holds are not visible to an app), the display's refresh with presents a refresh, the
window's pixel size, "step late max" (the latest a simulation step started against when it was due),
"pump max" (the longest window event pump, [PLATFORM.md](PLATFORM.md#diagnostics)) and the lock's
state (`window_pace_log`), one of:

```
lock held (grid G ms, n presents a step, mailbox|fifo, audio rate xR, CLOCK,
           W waited for the display, S skipped)
```

(one line)

- the line above (S: the lock's further presents of a frame already shown that it skipped, the
  skip guard's rule; its new frames left unpresented are in the first line's `late`), CLOCK one of
  `display clock vblank (output NAME, measured T ms, slot +P ppm, phase +F ms)` (the slot against
  the report, positive when longer, and the step's offset after the last vblank),
  `display clock vblank (output NAME, measuring)`, `display clock stopped`,
  `display clock left (its interval was off the report)`,
  `display timing on: K presents timed, phase nudge +M ms, L late, E early`,
  `display timing stopped|left (...)`, or `display clock off`;
- `no lock (fifo, steps on the game's own timer)`: a cap equal to the refresh with no whole
  multiple;
- `lock off`.

**Event lines.** The present allowance writes four, each one line, the first 8 then every 100th
(`ico_log_limited`; `port/platform/window_present.c` (`budget_lines`), the words from
`port/platform/pace_policy.c` (`pace_budget_event_text`, unit-tested word for word)):

- a drop: `window: presents held to A a step: the step ended L ms late after P presents, K steps
  after one Lx ms late after Px (the last present C ms, waited F ms for the GPU and D ms for the
  display[, the fence waits included: OpenGL]; GPU span G ms a replay); back to H after 120 steps
  on time if the GPU allows` (K and Lx, Px: the late step that armed it; H `not held` on the timer
  path; the OpenGL words where the display's wait is the whole wait, below); under the lock it ends
  `back to n a step after W steps on time: a probe` (W the probe's wait, below);
- a failed probe (the lock): `window: presents held to A a step: the step ended L ms late after P
  presents, the probe failed after K steps (the last present ...; GPU span G ms a replay); next
  probe after W steps` (K steps since the probe's climb, W the doubled wait);
- a climb: `window: presents back to A a step|not held[ (where they started)] after S steps (GPU
  span G ms x K + step T ms under the period P ms)` (S steps since the drop, K the presents the
  gate asked for); under the lock `window: presents back to n a step (where they started) after S
  steps: a probe (GPU span G ms a replay, step T ms, period P ms)`, the GPU figure reported only;
- a refused climb (the timer path only): `window: presents stay at A a step: GPU span G ms x K +
  step T ms is not under the period P ms (S steps since the drop; back to H when it is)`;
- the paced budget (the timer path with vsync off or mailbox), when its value rounded to a tenth
  changes:
  `window: presents paced by the GPU: N.N a step|not held (GPU span G ms a picture, period P ms)`
  (`not held` at 8 a step or more).

The GPU span G these lines and the allowance's gate read is a timed replay's span less its display
wait (`rd_perf_pace_gpu_ms`): the present's own list waits for the window's image between the
scene's stamps and the present's ([RENDER.md](RENDER.md#the-presents-own-list)), and that wait is
the display's, not the GPU's work, so under vsync it does not make the GPU look slower.

Paced with the GPU's time known, the drop ends `back to paced by the GPU after 60 steps on time`
and its climb reads `window: presents back to paced by the GPU (where they started) after S steps
(GPU span G ms a picture, step T ms, period P ms)`; no refused climb is written there.

A late step that only armed the allowance is not logged (the 10 s line's `step late max` has it).
A present whose present call (the replay's `present` phase: `vkQueuePresentKHR`, `Present`, the
swap) took over 250 ms writes `window: a present blocked M ms in the driver's present call (replay
R; API on ADAPTER, DRIVER; present MODE, framerate X)` (the first 8,
then every 100th; `present_blocked`), and the replays line is followed, in a block with any present
call over 50 ms, by `window: present blocked N times over 50 ms, longest M ms (mode MODE, framerate
X)` (`perf_log`; issue 123: the Adreno 740 on Qualcomm's driver spent 363-583 ms inside
`vkQueuePresentKHR`). Both read the replay records' present time (`perf_drain`); nothing new is
timed. Under mailbox with vsync off the 10 s line ends with the [FIFO fallback](#the-fifo-fallback)'s
count, `; k of 3 over 200 ms in 30 s toward FIFO`, and after its switch with `; K since presents went
FIFO N s ago` (K: the presents over 50 ms since).

The blocked line goes on past its parenthesis with the present timed apart (issue 123;
`rhi_present_split`, Vulkan; `RdPerfRecord.presentLockMs`, `presentCallMs`, `presentPollMs`,
carried from the submit thread's job, `RdSubmitJob`) and the replay's GPU numbers: `: inside
vkQueuePresentKHR 351.6 ms, the swapchain lock 0.0 ms, the surface poll 0.0 ms, other 0.2 ms; the
replay's GPU span 71.3 ms against the block's 27.0 ms so far, the GPU idle 0.4 ms before it`
(`present_split_text`; `other` is the present's time outside the three: the present-signal submit,
a retired swapchain's destruction, a rebuild). When the lock was taken but no present call followed
(no slot, or the present-signal submit failed: `RhiPresentSplit.called` false, `presentSplit` 2) it
reads `no present call made: the swapchain lock 351.5 ms, other 0.5 ms`, so a lock stall still
shows. Elsewhere the first part reads `the call not timed apart on this graphics API`; a replay without timestamps `the replay's GPU span not timed`; the first timed replay of a
block `(the block's first timed replay)`; a first stamp written before the replay before ended
`begun X ms before the one before ended`. The record popped after a present of 250 ms or more
writes `window: after the present that blocked (replay N, 352 ms): replay N+1 waited 0.3 ms for its frame
slot (fence 0.2), acquired its image in 298.7 ms, GPU span 26.9 ms, the GPU idle 301.2 ms before
it` (the first 8, then every 100th; `present_after_text`, `port/platform/window_stall.c`). The
after, around and stalled-replay lines start at 250 ms (`PRESENT_STALL_LINE_MS`,
`present_stall_lines`), the blocked line's own size, so the presents of 50 to 250 ms (17 in 110 s of
a reporter's log) do not spend each line's first 8 before the real stalls; a present over 50 ms
still arms the per-pass timing, counts in the 10 s lines and adds its gap to the stall gaps. Read
together: the call holding the whole time with the lock and the poll near 0 puts the wait inside
the driver; a stalled replay's GPU span far over the block's is the GPU running slow while the
present waited (a clock drop, another GPU client); a GPU idle about as long as the stall before the
next replay is the queue draining behind the present, the driver waiting on something other than
this replay's work.

On Android the kernel GPU driver's state is sampled every 250 ms on a thread of its own ([ANDROID_DIAGNOSTICS.md](ANDROID_DIAGNOSTICS.md);
`port/platform/gpu_sysfs.h`, `port/platform/window_stall.c`), and three lines come from it. At the
window's open, once a run: `window: kernel GPU driver: /sys/class/kgsl/kgsl-3d0: readable gpuclk,
gpu_busy_percentage, reset_count (3 so far), thermal_pwrlevel, ...; not readable temp (EACCES); not
present clock_mhz, devfreq/cur_freq; the fastest clock 905 MHz, power levels 0 (fastest) to 8;
sampled every 250 ms`, or `... kgsl-3d0: not readable (EACCES)` (every node denied: nothing
sampled); on a phone without KGSL (not a Qualcomm GPU) the GPU's devfreq entry and the candidate
paths, each node's state said and the unverified texts quoted (`window: kernel GPU driver:
/sys/class/devfreq/13040000.mali: readable cur_freq, max_freq; not present load, ...; the fastest
clock 950 MHz; CPU clocks of cores 0-7 sampled with it; sampled every 250 ms`, or `...
/sys/class/kgsl/kgsl-3d0 not present, no GPU entry in /sys/class/devfreq; not present ...`;
[ANDROID_DIAGNOSTICS.md](ANDROID_DIAGNOSTICS.md#a-phone-without-kgsl-and-the-cpu-clocks)). Each
10 s block, after the `present calls` line: `window: kernel GPU driver: clock 285-905 MHz (average
612) over 40 samples, busy 71 % on average, resets 0 in the block (3 in all), thermal power level
at most 0` (a temperature where `temp` is readable), then the CPU cores' clocks by speed class
(`; CPU clocks: cores 0-5 1000-1800 MHz (average 1500), cores 6-7 1600-2000 MHz (average
1900)`). For every present that blocked 250 ms or more, once the sample after it came (or a
second passed): `window: kernel GPU driver around the present that blocked (replay N): 0.12 s
before: clock 905 MHz, busy 74 %, resets 3, thermal power level 0; 1 during: clock 285 MHz, busy 9
%, resets 3; 0.03 s after: clock 285 MHz, busy 12 %, resets 3, thermal power level 0; CPU core 6
(the game thread's): 2000 MHz before, 1600 MHz during, 2000 MHz after` (the first 8, then every
100th; the parts whose node is not readable left out; the core the game thread was on as the
record popped). Nothing is written on other platforms.

The app's own words on the phone's thermal state and screen recording (issue 123;
[ANDROID_DIAGNOSTICS.md](ANDROID_DIAGNOSTICS.md#the-thermal-state-and-screen-recording) has the Java bridge) come as `ico_pc: thermal status changed:
MODERATE (2) -> SEVERE (3) (run time 212.4 s)` and `ico_pc: screen recording: visible (run time 40.1
s)` the moment the system reports a change, and in the window's lines: each 10 s block, after the
kernel GPU driver's line, `window: thermal: headroom 0.72 (1.0 is the throttling point), status
MODERATE (2); screen recording not visible` (a headroom of 1.0 or more is the throttling point;
the status runs NONE 0, LIGHT 1, MODERATE 2, SEVERE 3, CRITICAL 4, EMERGENCY 5, SHUTDOWN 6; a part
the phone does not report is left out; `window: thermal: not available (...)` once when it reports
none), and at the end of each `kernel GPU driver around the present that blocked` line,
`; thermal headroom 0.98, status SEVERE; screen recording visible`. The screen recording state exists
from Android 15 and shows only recordings the system announces; on older phones a recorder is not
visible to the log. Thermal status SEVERE or worse, or a headroom near or above 1.0, beside a
clock drop is the phone throttling; a visible recording beside the first pass grown alone is the
recorder holding the image. How the lines tell the stall's causes apart, from one log:

| Reading | the GPU's clock or a thermal step | another GPU client | a GPU fault's recovery | the driver's present path | the screen recorder |
| --- | --- | --- | --- | --- | --- |
| clock before / after the stall | drops (905 to 285 MHz), thermal level above 0 | unchanged | unchanged | unchanged | unchanged |
| busy during | high | high | any | low | low to moderate |
| resets | none | none | one more per stall | none | none |
| the stalled replay's passes | all about 3x | one or a few grown | one by hundreds of ms | mildly, alike | mildly |
| the stalled replay's first pass against the others (`the first pass X ms (the block's Y: it waits for the display's image), the other N timed passes Z ms`) | the others grown too | the others grown in places | one later pass grown by hundreds of ms | the first pass grown alone: the display held the image | the first pass grown alone |
| the GPU idle before the next replay | small | small | large | about the stall | about the stall |
| the present's split | the call holds it | the call holds it | the call holds it | the call, lock and poll near 0 | the call holds it |
| `[dev] render_thread` / `submit_thread` = off | unchanged | unchanged | unchanged | the stall's length or period changes | unchanged |

A numeric cap above the display's refresh in mailbox mode writes `window: frame rate N
is above the display's X Hz: presenting once a refresh (mailbox)` once after each settings change
(`port/platform/window_present.c` (`pace`)).

**The display line** (`window: display: ...`, under the lock only): the vblanks taken in the block,
their mean interval with its minimum and maximum and the rate, the refresh SDL reports, the
difference in ppm (`pace_ppm`: positive when the measured refresh is longer) and the wake-ups the
clock thread missed; then the presents' returns (mean, minimum, maximum interval, how many came a
refresh or more late), the presents that waited for the display and the longest wait; with D3D12 the
swapchain's own counts, `DXGI: P pictures shown over R refreshes, X repeated`. Without a clock the
first part reads `no display clock`, `display clock on, no vblanks yet` or `display clock stopped`
(`port/platform/window_host.c` (`disp_log`); `port/platform/pace_policy.h` (`PaceIntervals`,
`PaceShown`)). An interval under half a slot is dropped as not a new refresh; one of about k slots
(k 2 or 3) counts as k refreshes; a longer one is missed and left out of the mean
(`port/platform/pace_policy.c` (`pace_intervals_add`)). `pace_shown_add` counts the DXGI repeats as
the refreshes minus the pictures between two samples when positive, starting again on a count that
went back or a gap over `PACE_SHOWN_GAP` (240) refreshes (`port/platform/pace_policy.c`
(`pace_shown_add`)).

**The replays line**:

```
window: N replays: CPU A ms (max M): interp, wait, acquire, upload, walk, bind, submit, present,
        readback (fence waits); longest: ...; GPU ...; simulation step ...; strip verdicts S
        strips, T triangles, C ms a step
```

It gives the replays' mean CPU time by phase, then `longest:` with each phase's maximum in the block
(wait, acquire, upload, walk, bind, submit, present), so a log says which phase made the slowest
replay slow; the GPU times when the backend has timestamps (uploads, [lists](ARCHITECTURE.md#terms)
0-10, lists 11-12, display wait, present: `GPU 64.0 ms (max 70.1): uploads 2.1, lists 0-10 55.2,
11-12 1.0, display wait 3.4, present 2.0`; the display wait is the stretch from the scene's list
to the present's own, [RENDER.md](RENDER.md#the-presents-own-list)), else `no GPU timestamps`; the simulation step's mean and maximum;
and a step's mean of the region rule's card verdicts the CPU decided while the draws were recorded
(the strips and triangles tested, the time;
[VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)) (`perf_log`; `port/platform/perf_block.h`
(`perf_block_max_text`); `port/render/rd.h` (`rd_vu_card_counts`)). The phases are the replay's own:
`wait` the [frame slot](ARCHITECTURE.md#terms)'s fence (`rhi_wait_frame`), `upload` the ring and the
uploads, `acquire` the backbuffer, `walk` recording the VU vertex pre-pass and the lists less `bind`,
`submit`, `present`; `interp` the presenter's blend between the ticks
([RENDER.md](RENDER.md#performance-records));
`fence waits` is the backend's blocked time inside wait and readback (`port/render/rd_replay.c`
(`replayFrame`); `port/render/rd.h` (`RdPerfRecord`)). Further lines give the GPU time per list and
per picture effect, the per-replay counts (draws, passes, binds, barriers, uploads) with the block's
creations, the MSAA samples and resolves, and the graphics allocations against the device's limit
with the process's memory and the device's available and total memory (`perf_log`;
`ico_diag_device_memory`, [PLATFORM.md](PLATFORM.md#diagnostics)). Three lines come from the per-pass timing
([RENDER.md](RENDER.md#the-per-pass-timing)): `window: GPU top passes: ...` (the most expensive
passes of the block's timed replays, each timed between stamps outside the render passes, with
the count of passes a replay and of those under 0.05 ms; only with GPU timestamps),
`window: passes per replay N: render passes R (lists L, <effect> E, ...), <kind> K, ...` (what a
replay begins, by kind, every replay) and `window: CPU walk ms per replay by list 0-12: ...;
uploaded KB per replay: textures T, meshes M (interpolated I), uniform blocks U, VU streams S,
other O[; Direct3D 11 submits wrote W KB to the GPU]` (`perf_log`, `perf_passes_add`). The
uploaded KB are what the replay wrote into the upload ring (the uniform blocks part counts each
distinct block once: the VU draws' DrawCBs share copies through the content cache, so equal blocks
count once); the last part, on Direct3D 11 only, is
what that backend's submits then copied from the ring's CPU copy to the GPU
(`RdPerfRecord.backendWriteBytes`, `RhiStats.uploadWriteBytes`; the block's mean from
`port/render/rd_perf.c` (`rd_perf_backend_text`)): the ranges the lists read, cut at
the ring's written end ([RHI.md](RHI.md#direct3d-11), **The written mark**). On the other backends
the GPU reads the ring itself and the part is left out. Every backend's line ends with the store
plan's means per replay, `; stores: N depth and M colour stores dropped a replay of P passes
(planned; [dev] store_plan)` (`every store kept` with the plan off;
[WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)).

**Reading the lines for a speed report.** Four lines carry the evidence, with the start's
`ico_pc: summary:` and `rhi_vk:` device lines (`port/platform/window_host.c` (`perf_log`,
`log_summary`); `port/render/rd_perf.c` (`rd_perf_pass_kinds_text`, `rd_perf_pass_agg_text`)):

- `window: passes per replay N: render passes R (lists L, <effect> E, ...), <kind> K, ...`: N is
  every piece of GPU work a replay begins, by kind (`uploads`, `VU pre-pass`, `render pass`,
  `copy`, `snapshot`, `DATE snapshot`, `fog depth read`, `exact blend`, `MSAA resolve`,
  `stencil clear`, `present`); the render passes split into the lists' (L) and each picture effect's.
  On a tile-based phone each pass has a fixed cost of about 0.05 to 0.15 ms
  (`RD_PERF_PASS_SMALL_US`), so N above about 45 at Original 1x says the pass structure is where the
  time goes. On a tile-based profile `DATE snapshot` and `fog depth read` read 0 where the local read
  is on (the profile's `DATE stencil` and `fog local on`). The title frame's glow passes are 26 with
  the blur fold on (every class but Mali; 35 without: each folded pair's one pass is begun and timed as the glow's, as its
  pass 2 was, `port/render/rd_replay_tiler.c` (`rd__replay_blur_fold`)). A storage-path count in the hundreds is the exact blend
  (the opt-in), not the default.
- `window: GPU top passes: <label> <target> WxH [N passes] D draws X.XX ms, ... (K of P passes a
  replay, T timed replays; passes under 0.05 ms: S a replay, Y ms)`: the five most expensive passes
  of the timed replays, a replay's mean, each timed between stamps outside the render passes. S
  times about 0.08 ms is the fixed cost of the small passes. A `list 0` `SCENE` entry with hundreds
  of draws is geometry; a SCENE entry with few draws and a millisecond or more at its native size is
  a pass broken by a load or store (a copy or a read of the target in the middle of the list); `glow
  WORK1 256x128` with its pass count is the blur chain. On OpenGL an entry reads `X.XX ms (busy
  B.BB)` and the parenthesis ends `; group entries N a timed replay, M already bound; GPU busy B ms,
  span X ms a timed replay`: OpenGL runs the commands while the replay records them, so X also
  counts the graphics card waiting for the CPU, and B is what it ran once the work was handed over
  (a lower bound of its own time, [RHI.md](RHI.md#opengl), the busy clock). X well above B with a
  long walk in the replays line is the CPU feeding the card too slowly, not the card; M is the
  binds the backend skipped as already made. A timed pass carries the list it began in, also in
  the passes CSV's `list` column (`port/render/rd_perf.c` (`rd__perf_pass`)): a render pass the
  replay keeps open across lists is labelled by its first one, so on the title and in the menus the
  last `list 3` `SCENE` entry, which begins after the shadow composite's three DATE snapshots, holds
  lists 4 to 11 and the port's menu text with them. A large `list 3` entry with the menus up is
  the menu as much as the scene; the bisect is a replay with list 11's draws turned off
  (`rd_replay_tool --nop 11:A-B`, [RENDER.md](RENDER.md#dumps-and-the-replay-tool)).
- `window: GPU ms per replay: lists 0-12 ...; effects: ...; present ...; VU pre-pass ...` and the
  replays line's GPU part: the per-list and per-effect times. On a tile-based GPU the work of a pass
  is done where the pass ends, not where its commands were recorded, so a list's or an effect's time
  there is where the work ended and not its own cost ([RENDER.md](RENDER.md#the-per-pass-timing));
  the top passes line and the total are the numbers to compare, and a reporter on such a device
  should not be asked to explain the lists' split. The GPU ms per replay and top passes lines end with that sentence on a tile-based GPU, and say that the present's own list alone waits for the display's image. The Vulkan acquire's wait (`COLOR_ATTACHMENT_OUTPUT`) holds every colour write of the batch it is attached to, not only the window's image; with a picture in one list and one submit the wait held the scene's off-screen passes too and landed in uploads when new targets were cleared first and otherwise in list 0's first pass (a phone at the Original preset in play: uploads 0.53 ms, list 0 6.91 of 14.76; issue 179's Mali in its boat scene: 38 to 84 ms of 88 to 143 ms pictures). The present's pass is a list of its own, and only its submit takes the wait (`port/rhi/vk/vk_cmd.c` `vkr_submit_batch`, [RENDER.md](RENDER.md#the-presents-own-list)), so the scene's passes run while the presentation engine still holds the image; the replays line's `display wait` is the stretch between the two lists, and where the GPU holds only colour output for the wait (the Mali) the wait shows in `present` instead.
- `window: GPU between replays: N timed replays after the one before: the GPU idle before I of them (A ms on average, the longest L ms), K begun before the one before ended (by O ms on average: their spans count that end too); the GPU's time a replay past the one before's end E ms, its span S ms`: each replay's first stamp against the replay before's last (`RdPerfRecord.gpuGapNs`, the same GPU clock; `PaceGpuGap`, `pace_gpu_gap_text`). Every Vulkan stamp is bottom-of-pipe, written once the earlier commands are done, so K is 0 on a driver that keeps the rule and a span then holds its replay's whole GPU time, any wait inside the replay (the display's image) included: that is the time from the replay's start on the GPU to its fence, what the step rule's pipe needs. K above 0 is a driver that writes the first stamp early: the spans count the replay before's end too and E is the GPU's own time a replay, the one to compare with the span the rule reads. I is the GPU waiting for the CPU's submissions. A replay's gap is measured from the timed replay before it across any replays without timestamps between (their GPU work then inside it; refused only past 60 s), and when any were, the line goes on `; C of them timed across untimed replays`; the gaps of the replays right after a present that blocked over 50 ms are listed too, `; across the 3 stalls: the GPU was idle 301.2, 298.0, 310.5 ms before the next replay` (the first 8 of a block, `the first 8 of the 11 stalls` past them; a negative one is a replay begun before the stalled one ended) (issue 123; `rd_perf_gap.h`, `pace_gpu_gap_stall`, `port/platform/window_stall.c`). A gap about the stall's length is a GPU that sat idle while the present blocked: the wait was not this picture's GPU work. From the first such present on Vulkan (never OpenGL or Direct3D) every replay is timed pass by pass and a stalled replay gets `window: stalled replay N (...)` ([RENDER.md](RENDER.md#the-per-pass-timing)).
- `window: per replay ...`: the counts (draws, passes, pipeline and bind group binds, barriers,
  copies, `submits 2.0` (the queue submits a replay: a presenting picture's two lists, the scene's
  and the present's own, [RENDER.md](RENDER.md#the-presents-own-list)), uploads, DATE snapshots). After the binds, `(repeats skipped: X pipeline, Y bind group, Z
  state)`: the binds and state sets the RHI found already in force and recorded nothing for
  ([RHI.md](RHI.md#the-bind-cache)); the binds before it are what was recorded. Its exact-blend fields (`exact blends`, `destination-read
  draws`, `storage passes`, `pass switches for the exact blend`) are 0 unless the player opted into
  an exact blend with `[dev] blend`; nonzero ones on a log without that setting are a defect.
  `pipelines created` in the block should be 0 once the set is made (a pipeline made while playing
  is a hitch).
- `window: pixel diet per replay ...`: how many draws a replay took the pixel diet's steps for (the
  alpha plan's one-pass and opaque draws, the post sprites drawn by the constant fold, the
  premultiplied forms and the local read, the 3D passes drawn without the planes), each 0 where its
  flag is off ([RENDER.md](RENDER.md#performance-records); the flags are in the `rd: profile:` line).

The device class and its flags are in `ico_pc: summary:` (`profile <class>: fog local on|off, DATE
stencil|snapshot, depth stores planned|kept, blur fold on|off, local read yes|no, uint views ...`, a
`([dev])` or `(self-test)` mark on a flag a key set or a self-test turned off) and in the one
`rd: profile:` line; the blend is in `blend fixed-function (self-test: ...)`. A speed report is
worth acting on with the `rhi_vk:` device line, the summary, these four lines from one block at
the slow setting and one at 1x, and the log's start-up lines for the mechanisms (`rd: DATE:`,
`rd: blur fold:`, `rd: blend:`).

**The slow replay line**
(`window: slow replay T ms (replay R, frame F, alpha A, about vsync V): ...`) is logged for a replay
over the display's refresh, at most `SLOW_REPLAY_LINES` (5) a block: its phases with the fence wait
inside `wait`, its uploads, pipeline creations, allocations, buffers and textures created and DATE
snapshots, and its GPU time. When the cap kept the block's longest replay out, its line is written
at the block's end, before the first stats line, ending `; the block's longest, past its 5 lines`
(`slow_replay_longest`), so a stall never goes unexplained behind five earlier lines. The records
arrive two replays late, so the vsync is the one the record was drained at
(`port/platform/window_host.c` (`slow_replay`, `perf_drain`)). A walk over 500 ms also gets the
renderer's `rd: slow walk` line ([RENDER.md](PIPELINES.md#the-start-up-set)). `[dev] slow_step_ms`
(default 8, 0 off) logs up to 5 `slow step` lines a block for simulation steps over it, split into
the step's parts and the pump's, with the step's own card verdicts next to the game threads' time
(`slow_step`). After the step's time, the game thread's own CPU time over the same span
(`window: slow step 64.1 ms (CPU 9.8 ms of it) at vsync ...`; `cpu_place_thread_cpu_ns` read at
the end of one pace and the start of the next, left out where the system has no thread clock): a
step far longer than its CPU time waited (a present, the render thread, a core), one near it
computed (issue 179). The game threads' part names where that time went: `game threads 199.3 in 39
switches (longest Main 171.2 ms; on core 2)` is the longest single fiber switch of the step (one
resume of a game thread until it next yields, the thread named as the thread lines name it: Main,
the sound thread, the stage's), its time, and the core the game thread was on at the step's end
(`ico_sched_switch_max`: two clock reads a switch in `ico_sched_run`; the core left out where it is not
known). A step with the usual 38 or 39 switches and one long switch is script or disc work on that
thread; many switches and none long, or a long one on a thread that waits, with a picture whose GPU
span and present blocked beside it, is the system holding the whole process (issue 179). The
words are `window_scene.c` (`window_scene_threads_text`).

**The layout line** (`window: layout: play 6.2 s, scene 3.1 s, pause 0.7 s`), after the first
line: the block's simulation steps' time by the game's layout when each step ended: `play` (the
game's play layout), `scene` (a scene's empty layout, or any layout while a movie plays), `title`
(the two title layouts), `pause` (the pause menu), `settings` (the game's options and button
configuration) and `other` (the port's pages, the memory card's), those with any time only, in that
order, in seconds (`window: layout: no steps` when the game did not run). The layout ids are the
running disc's (`port/core/region.h`: `layout_play`, `layout_scene`, `layout_title_*`,
`layout_pause`, `layout_options`, `layout_key_config`). It reads a cutscene out of one log: the
blocks with `scene` time are the scene, whatever the stage (`window_scene.c`, `scene_note_step`).

**The VU line** (`window: VU: 1182 draws, 61k triangles, 550k vertex invocations (pre-pass off: 9 a
triangle)`), right after the `N replays:` line: a replay's average of the VU draws the walk issued
(`vuDraw`, before any merge: the cut and the kick of a fan count one each), the triangles in them
(index lists in threes) and the vertex invocations the picture costs: with the vertex pre-pass off
(tilers) 9 a triangle (every triangle shades 3 corners, three times each, no index reuse), with it
on the vertices it recorded (`pre-pass: each vertex once`). The title's pictures are 250 to 340
draws, the cutscenes of issue 179 1182 to 1473 (`RdPerfRecord.vuDraws`, `vuTris`). With GPU
timestamps it ends with list 0's (the scene's) GPU time over the block's timed replays and a
least-squares line of it against their VU triangles: `; list 0 GPU 41.2 ms a replay, 12.3 ms +
0.41 ms a thousand triangles (R^2 0.80, 38 replays)` (`window_scene_vu_fit`; the mean alone with
fewer than 3 replays or one triangle count, no R^2 when list 0 did not vary). A high R^2 with a
large slope is the vertex work setting list 0's time, the question the VU pre-pass answers on a
tiler ([ANDROID_DIAGNOSTICS.md](ANDROID_DIAGNOSTICS.md#issue-179s-lines-what-each-decides)).

## Pacing

The loop runs on the game thread; with the [render thread](PLATFORM.md#the-render-thread) on, each
present it makes hands the picture over whole (`rd_present_post`: the interpolation, the walk, the
submit and the present on the render thread) after the one before it finished, so the next step
runs while the render thread makes and presents the picture; a change the step makes to the
renderer's state waits for the picture's walk. The alpha is the one the loop computed when it
posted (the present clock's value travels with the picture). Under the vsync lock with the
display's timing (present wait) each picture waits for its present, since the lock reads the
present's id right after it (`rd_submit_hold`).

`ico_window_pace(hz)` runs after every simulated vsync: it presents until the vsync's deadline (50
Hz PAL: 20 ms; NTSC: 16.683 ms) as often as the frame rate and vsync allow, then sleeps to it; more
than 100 ms behind (`RESYNC_NS`) the lag is dropped and counted instead of run off fast
(`port/platform/window_host.c` (`ico_window_pace`); `port/platform/window_present.c` (`pace`, `resync`)). A pacing rule is written in
`port/platform/pace_policy.c`, which has no SDL, so `pace_policy_test` and `pace_loop_test` drive it
with a fake clock and swapchain; the window only supplies its I/O (`port/platform/window_present.c`
(`pace`, `pace_locked`, the `lock_io_*` callbacks of `PaceLockIo`)). Three paths:

1. **Frame rate Original**: `rd_end_frame` makes each frame's one picture; the window only sleeps
   to the deadline (`pace`). The rule is a rate requirement: the game steps at the disc's rate (50
   or 59.94 a second) in real time whatever the GPU and the display, and the pictures float with
   their cost. With the render thread on, the picture is handed over as the other frame rates'
   are (`port/render/rd_thread.h` (`rd__replay_frame_post`), `port/render/rd_core.c`
   (`originalPicture`)): the render thread walks it, the submit thread submits and presents it,
   and the step goes on at once; its next renderer call waits only for the picture's reads (the
   walk, texLock, [PLATFORM.md](PLATFORM.md#the-render-thread)). The driver's present call holds
   the swapchain's lock (`port/rhi/vk/vk_swapchain.c` (`rhi_present`)) and the next picture's
   acquire takes it, so a picture posted while a present blocks would wait inside its reads, and
   the step with it: a Mali-G52 in a scene blocked 165-300 ms in `vkQueuePresentKHR`, and with
   the replay inside the step every second step lasted 55-230 ms and the game ran at half its rate
   (issue 179). Before a frame's picture the renderer asks the window on the game thread
   (`rd_set_replay_gate`, `orig_gate`, `port/platform/window_lines.c` (`window_orig_gate`)) with
   whether the render thread or the submit thread is still busy with the picture before
   (`rd__thread_picture_busy`): busy, the frame is neither replayed nor presented and the last
   picture stays (`port/platform/pace_policy.h` (`pace_original_gate`, `PACE_ORIG_BUSY`)). One
   present is in flight, the steps keep the disc's rate, and the pictures come about `1000 /
   (walk + present)` a second, a frame interval less at worst (a picture waits for the first
   frame closed after the one before is done). Without the render thread, parked, in sync, with a
   capture armed, or under `[dev] original_async = false` ([CONFIG.md](CONFIG.md)) the frame is
   replayed and presented inside the step, the game thread waiting. On both, a frame whose step is
   already a whole period or more past its due time at that point (the late arm of the timer
   path's skip) is neither replayed nor presented, and the last picture stays on the screen
   (`port/platform/pace_policy.h` (`pace_original_present`, `PACE_ORIG_LATE`)). A
   keep frame (a fade or a menu drawn over the picture before it) is always drawn. The skipped
   frame's step ends at once and pays back a period of the debt, so a picture of G ms against a
   period of P ms comes about `1000 / (G + step)` times a second (all of them while a step and its
   picture fit the period), the steps stay at the disc's rate, and the lag behind real time stays
   under a period plus one picture: the audio's 120 ms lead ([AUDIO.md](AUDIO.md#the-outputs-clock))
   is not reached for a picture up to about 70 ms. There is no "never two in a row": a skipped
   frame is never shown later, and a picture over two periods needs two skips to every present.
   The swapchain takes mailbox where the surface offers it, as the timer path does (`video_settings`),
   so that present never waits for the display (on the submit thread, or inside the step where the
   picture is replayed there); FIFO only without mailbox
   (Direct3D 11 and 12, OpenGL, some surfaces), where a wait for the display shows as a late step
   and the next frame's replay is the one left out. A replayed frame draws the same picture as
   before (the gate decides only whether it is replayed: Original at 4:3 1x stays the camera's
   picture, the console's except inside triangles it dropped, VD-29); the tools and tests set no gate and replay every frame inside the step. Since the gate
   skips the replay itself, not only the present (the timer path's skip still replays), an effect
   that reads the previous picture (a motion blur's history, an after-image pass) advances once per
   drawn picture, not once per game step, on a GPU too slow for every picture, and a skipped
   frame's texture uploads and decode settles wait for the next replay (review finding 7: a known
   change of look on a slow GPU, not a defect).
   A keep frame (every frame of a stage change's fade and load, the boot card check, the ending)
   that the gate presents while the render thread is busy still waits inside the step for the
   picture before, its present included, before it is handed over, so an acquire never waits
   behind a present inside a picture's reads; the 10 s line counts it in `N waited for the
   picture before`. The lines: at each start `rd: render thread: with Frame rate Original
   each game frame's picture is handed over as the other frame rates' are: its submit and present
   run on the submit thread; ... but a kept frame (a fade, a load) still waits for the picture
   before, its present included ...` (or `... is replayed and presented inside the game step, the game
   thread waiting ([dev] original_async = false)`); the 10 s game-thread line's `(closing a frame
   ...; its picture handed over in 0.6 ms (longest 14.2 ms; 3 waited for the picture before, 31.0 ms
   in all; 12 skipped: render thread busy); presents waited inside the step: 0 (0.0 ms))`; the 10 s
   line's `steps late N (frames not presented M: the step late L, the render thread busy B)`; a slow
   step's `, the picture handed over 0.5 ms (0.0 ms waiting for the one before)`, `, the picture
   replayed inside the step 165.6 ms` or `, the picture not presented (...)`; a present that blocked
   `window: a present blocked N ms in the driver's present call (...; on the submit thread)` or `(...;
   inside the game step)`. `pace_policy_test`'s `testOriginalSplit` holds the hand-over's rates:
   walks of 5 to 40 ms and presents of 1 to 300 ms, the game at 50 steps a second within 2 % and no
   resync, the pictures between `1000 / (present + walk + a frame interval)` and `1000 / (present +
   walk)` a second; the same 320 ms picture inside the step resyncs. `rd_tex`'s `threadOriginal`
   checks the threading and the bytes.
   `pace_policy_test` (`testOriginalRule`, `testOriginalLoop`) holds the rates: 9 to 17 ms a
   picture at 20 ms every frame presented, no skip; 20 to 90 ms 50 steps a second of real time,
   about `1000 / (G + 1)` presents a second, no resync; without the skip a 30 ms picture ran the game
   under its rate. Issue 123's case: without the skip a 25-45 ms picture (an Adreno 740 at 3x)
   runs the game under its rate, the audio reaches its bound and holds back, and the title's sound
   comes and goes.
2. **The timer path** (any other frame rate without the lock): presents at the current alpha until
   the deadline. No further present of a frame already shown once the deadline has passed, while
   presents are slow, off the screen, (except with a cap equal to the refresh) when the last
   present's cost would end past the deadline, or past the
   [present allowance](#the-present-allowance) (`pace`). Presents are slow when the median of the
   last 8 present costs is above `max(refresh, period) + period / 2` (a refresh more with an effects
   program loaded), entering after 3 such presents in a row and leaving below 80 % of it
   (`port/platform/pace_policy.h` (`pace_slow_present`, `pace_slow_threshold`)). Presents that
   hold the next step back keep the game below real time until the pacer drops the lag, so a new
   frame found past its deadline is left unpresented (the next step runs at
   once and the next present shows the newer frame) when the next step is already a whole period
   late (the step debt: the time against its due time, as "step late max" measures it; the late
   arm) or presents are slow (the slow arm); the same frame may stay unpresented at the following
   steps while that holds, and the frame after a skipped one is always presented, so the picture
   keeps at least every other frame. A game at 30 frames a second closes a frame every second step,
   so a frame skipped at one step and presented at the next would save nothing: on a renderer whose
   presents take longer than two steps (a GPU-bound phone at 2x, 39 ms a present against 33.4 ms)
   the frames go unshown one at a time and the game keeps real time instead of falling behind until
   a resync (`pace_policy_test` (`testLateSkipLoop`)). A single late present on a renderer that keeps
   up, less than a period behind, never skips. The late arm reads the loop's own lateness, not the
   audio queue, on either audio clock, so it holds with or without sound
   (`port/platform/pace_policy.c` (`pace_late_skip`, `pace_skipped_last`), called from `pace` with
   `s_pres.skipOpen` and `s_pres.skipFrame`; any present closes the skip, the lock's too,
   `lock_io_present`). The first stats line counts the block's skips by arm.
3. **The vsync lock** (below; a frame rate equal to the display's refresh: Android's default 60 on a
   60 Hz phone takes it), with the late arm of the same skip.

**The present gap** on the timer path: "uncapped" with vsync on is a sixteenth of a refresh less
than one refresh, and in mailbox mode never fewer than sixty presents a second whatever the display
reports; a numeric cap is `1 / cap`, except a cap above the refresh in mailbox mode, which takes
"uncapped"'s gap (`pace_cap_above_refresh`: 120 on a 60 Hz screen presents once a refresh; not a cap
equal to the refresh, 60 on 59.94 or 60.00; under FIFO the cap keeps `1 / cap`): the display shows
no more pictures than its refresh, and the rest took the GPU time the shown ones need; "uncapped"
without vsync is 0 (back to back) (`port/platform/pace_policy.c` (`pace_present_gap`)). The gap counts on a grid (`pace_next_present`:
the next present is the last one's due time plus the gap, so a late wake-up does not push every
later one back; a stall restarts the grid).

**Cap versus refresh.** `pace_cap_equals_refresh` treats a numeric cap as the display's rate within
0.5 Hz, and up to 1 Hz above a whole reported rate of 100 Hz or more (Windows cuts such rates down:
a 143.9 Hz screen reads 143). SDL turns Windows' whole 29, 59 and 119 into x / 1.001, so a rate that
reads as a whole 59 is a true 59.00 Hz mode and keeps the 0.5 Hz bound
(`port/platform/pace_policy.h` (`pace_cap_equals_refresh`)). With vsync on and interpolation, a cap
equal to the refresh takes the vsync lock when it is a whole multiple n of the simulation's rate
(`pace_lock_presents`: within 1 %; 60 and 120 on 59.94, 50 and 100 on 50) and n refreshes are within
1 % of the simulation's period (`pace_grid_period_ok`). A cap equal to the refresh without a whole
multiple (144 on 59.94) presents FIFO with no gap: the blocking present is the timer, and when the
refresh is shorter than the period `pace_fifo_hold` holds back a present that would return on a
vblank past the deadline, a new frame at most once and never once the deadline has passed
(`port/platform/window_host.c` (`video_settings`); `port/platform/window_present.c` (`pace`); `port/platform/pace_policy.c`
(`pace_fifo_hold`)). Any other cap prefers mailbox (`rhi_prefer_mailbox`).

**A window smaller than the display** (Windowed) paces the same:
the lock and the present mode are decided from the display's refresh, not from how much of the
display the window covers (`video_settings`), the presenter boxes the picture into any output size
(`port/render/rd_present.c` (`rd__present_box`)), and the vblank clock is the output's
(`port/platform/display_clock.h`). When the desktop compositor composes the window a present is
shown one composition later, about one refresh more latency than a window flipped to the display; how well the lock
holds its phase there has not been measured on hardware: the 10 s `lock held` line is the check.

### Further presents and the game step

With the [render thread](PLATFORM.md#the-render-thread) on, a further present (a picture between
the game's frames, of a frame already shown) is held only when it would make a game step miss its
own period: the user's rule, "just give pictures when they can". The game steps every vsync (16.7
ms at 60 Hz), and its frame is two steps of different kinds: the step that closes a frame carries
the game's work for the next one (8-10 ms of 16.7 on the PowerVR phone: every slow step line of
its log is at the vsync that closes a frame), the step between frames under 1 ms, with no renderer
call during play (4-17 read waits a 10 s block there, although the frame's own picture's reads ran
past that step nearly every frame). A picture the render thread is making holds the renderer's
state until its reads end, and the next step's first renderer call waits for them; a step that
starts a few ms late that way still ends inside its period. So a further present may start the next
step late by up to `period - (step + 2 ms)`, the step being the typical time of its own kind
(`pace_step_plan_ns`) and 2 ms the margin for what that time does not see (the pump, the pacer's
wake, the audio callback and other threads on the core). When the next step closes a frame, the
step after it, behind that new frame's own picture (never held: every frame is shown), must fit
its own period too; when the next step is one between frames, no new frame's picture follows it
and its own slack is the whole test. The window knows which step closes a frame from the game's
frame length in steps (the last two frames' closes apart; past four, after a load or a pause, not
known: every step is then planned as one that closes a frame) (`pace_step_closes`).

Each kind's typical time is a decaying mean (an eighth of the way a step of that kind) and half
its deviation, at most a quarter of the mean (`PaceStepTyp`, `pace_step_typ_note`,
`pace_typ_share_ns`; the whole deviation planned a phone's 9 ms closing step at 9.9-12.0 ms, a
deviation of 1-3 ms; a 15 ms one among 9 ms ones now plans the next at 10.7 ms where the whole
deviation planned 11.7, back to 9.5 ms 16 steps later). A step with the game's loading work (disc reads,
texture decodes, pipelines made: what `window_hold_line` names) is left out, and so is a step over
the period whose kind's step before was not (an isolated long step: a bug report's capture, a
stall); two in a row are the scene's time and count. Before this the plan was one recent maximum
for every step (its longest lately, falling an eighth a step): on the phone 7.4-9.4 ms for the
step between frames too, so its slack looked like 5 ms instead of 14, and nearly every in-between
picture was held (30-31 presented fps where the GPU made 38-43). One mean of both kinds and its
deviation would read 8.6 ms, no better; by kind the phone's load reads about 9.2 and 0.5 ms.

The render thread's times the rule plans by are typical times too, not the longest lately
(`PaceRtTyp`, `pace_rt_typ_note`; `port/platform/render_thread.c`): a picture's reads (the
hand-over), its whole job, its reads less their waits, its interpolation and the submit thread's
job are each a decaying mean, a sixteenth of the way a picture (a picture's weight is under 2 %
after 60), and half its deviation, at most a quarter of the mean. A picture over twice the mean
whose picture before was not (a spike: a pipeline's first use, a stage's textures) counts as twice
the mean, so it raises the plan by its share only (a 100 ms picture among 10 ms ones: 11.9 ms,
back to 10.2 ms 60 pictures later); two in a row are the scene's time and count in full. The
present call's wait keeps its recent longest (`pace_rt_estimate`), since its hold is for the long
ones. Before this every one of them was its longest lately, falling an eighth a picture: on a
PowerVR phone the rule planned reads of 10.2-17.7 ms, 5.0-8.7 ms of them the interpolation,
where the same 10 s blocks' means were about 10 and 3.3-4.0, and held 529-589 pictures between
frames a block (30-32 presented fps at 24-26 ms GPU pictures). A picture that runs longer than
typical is what the back-off below is for: the floor, the widened margin, the third miss's strict
plan. The model at that phone's numbers (reads of 10 ms with a deviation of 2.5 ms, the closing
step 9 ms, the step between 1 ms, a submit of 8 ms, 24 / 25 / 26 ms GPU pictures, over 100 s)
gives 41.0 / 39.5 / 38.1 presented fps where the longest gave 34.8 / 34.4 / 34.3 (the GPU makes
41.7 / 40 / 38.5), 14 / 22 / 2 steps missing their period in 100 s (7 / 8 / 0 over the floor,
the latest 2.8 / 3.5 / 1.3 ms past it), one fall-back of 120 steps in the first 10 s while the
means learn, and the game's clock at most 8.3 ms behind, so the sound, up to 120 ms ahead, is
never held back (`pace_loop_test` `testRc11`; `build-host/tmp/v098/evidence/pace4/`). The 10 s
clause shows the plan's inputs: `a picture holds the renderer for R ms typically (its mean M ms and
half its deviation of D ms; ...)` and `the steps planned at X ms closing a frame (its mean M ms, its
deviation D ms) and Y ms between frames`.

Over the game's frame this is the frame's budget: the closing step may start late by its own
slack (period less its time and the margin, 5-6 ms on the phone), the step between by most of its
period. A frame's two pictures chain the closing step, the frame's own picture's reads, its submit
on the submit thread and the further picture's reads past its interpolation (on the phone 9.5 +
0.1 + 12 + 7 + 7.8 = 36.4 ms against the frame's 33.4 at 12 ms reads); the lateness that chain
leaves past the frame's period carries into the next frame until a further picture is held. So no
step ends past its period, the game's clock is never more than the closing step's slack behind
(the 10 s line's `step late max`), and the sound, which the game feeds up to 120 ms ahead
([AUDIO.md](AUDIO.md)), is never held back by it. Two stricter and looser readings were modelled
at the phone's numbers and not taken: never starting the closing step late (the frame's chain
must fit 33.4 ms with its margin: held at 12 ms reads, 30 presented fps, two pictures a frame only
at reads under about 9.3 ms with a 9 ms closing step, or a closing step under about 4.5 ms at 12
ms reads) and letting the game's clock fall up to a whole field
behind (the closing step then ends past its own period by design and the miss counts move to the
frame's end): at the phone's 25 ms GPU pictures 39.8 presented fps against the rule's 39.2, 45-54
against 40-50 where the GPU has time to spare, for up to a field more input latency and the
back-off's misses redefined (`build-host/tmp/v098/evidence/pace3/rc10_cells.py`, `lag_trace.py`).
The render thread's times say how long a picture's reads will take: its interpolation, its waits
for the submit thread and for the GPU's slot fences (2 pictures in flight at most), at the GPU's
measured time a picture, and its own reads (`port/platform/pace_policy.h` (`pace_rt_further_ask`,
`pace_rt_step_miss`, `PaceRtPipe`, `PaceStepEst`); [PLATFORM.md](PLATFORM.md#the-render-thread)).
A step that ends past its deadline anyway because of the pictures (an estimate that was wrong: a
stage's first textures, a burst of slow pictures) is answered in proportion to its lateness L
(`pace_step_est_note`, `PaceStepEst`):
- L under the floor, 2 ms (the rule's own margin, `PACE_STEP_MISS_FLOOR_NS`), changes nothing:
  it is the scheduling noise the margin is there for.
- L over it widens the margin by L: the step's time the rule plans by is its estimate plus the
  widening, so the slack is `period - (step + 2 ms + widening)` and the next pictures are held
  that much earlier; the widening falls an eighth a step (gone under 1 us), so a 15 ms miss's
  widening is under 2 ms after 17 steps.
- Only the third such miss within 300 steps (`PACE_STEP_MISS_WINDOW`) plans the whole period, the
  rule before it (the reads by the next step's due time), for 120 steps; a further fall-back
  doubles it, to 480 at most; 300 steps without a miss over the floor halve it, then forget it.

So a wrong estimate costs one late step and a few steps of a wider margin, and a load that keeps
the pictures' misses coming costs 2, 4, then 8 seconds of the strict rule at most, never the state
of steps 40 ms late with the sound held back. A phone's log is why: with every miss of the
pictures' doubling the strict plan up to 1920 steps, misses of 0.0-0.6 ms among them, 478-600 of
every block's 600 steps ran the rule before at 30-41 presented fps where the GPU made 34-45. The
model at that phone's numbers (a 4.5 ms step, 13 ms reads,
25 ms GPU pictures, a picture 12-26 ms slower than its estimate every 250 steps) plans no step at
the whole period where the rule before planned 5819 of 6000, and gives 39-40 presented fps where it
gave 30 (`pace_loop_test` `testBackoffCause`; `build-host/tmp/v098/evidence/fixbackoff/`, re-run in
`build-host/tmp/v098/evidence/pace4/c_cells.py`).

A miss is the pictures' only when their holds on the step (the presents running past its start,
its waits for a picture's reads) are at least the floor and at least its lateness
(`pace_step_by_pictures`): misses of 0.0-0.1 ms with the presents 0.3-0.4 ms past the step's
start and no wait (that phone's log) are noise, the game's. A miss of the game's own (a long step, a load, a compile,
a bug report's capture, the disc, or the lateness a long step carries into the next start) changes
nothing (`pace_loop_test` `testBackoffCause`). The 10 s line's `game thread:` clause counts the
holds (`would have made the step miss its period`), the steps started late by the pictures (with
the longest) and the steps that missed their period: `N (P the pictures': S under 2 ms, W widened
the margin, B the third in 300 steps; G the game's own; latest L ms past it; Y steps planned at the
whole period after the pictures')`. Each miss names what it did (each first 8, then every 100th):
`window: step rule: a miss the pictures caused under 2 ms (late by L ms, R ms of it waiting for a
picture, P ms the presents ran past the step's start): inside the rule's margin, nothing changed;
...`, `window: step rule: the margin widened by L ms to W ms after a miss the pictures caused (...;
K of the pictures' over 2 ms in the last 300 steps): ...`, `window: step rule: back-off N steps
after the third miss the pictures caused within 300 steps (...)` and `window: the game step missed
its period by ... the game's own time, not the pictures' ...`.

The rule is a rate requirement ([Pacing](#pacing)): `pace_loop_test` (`testStepRule`) asserts the
presents in 10 s for 300 game frames by the render thread's reads, at a uniform 4.5 ms step with
the submit thread: reads 10 / 12 / 13 / 15 / 17 ms give 700 / 600 / 550 / 447 / 301 (the rule before:
600 / 600 / 301 / 301 / 301); two a game frame while the reads fit the period less the step (12.18 ms
there), past it as many as keep every step inside its period, and none misses it. `testFrameShape`
asserts the frame's two kinds of step at the phone's load (12 ms reads, a 7 ms submit, the closing
step 8-10 ms, the step between 0.5 ms with no renderer call): 450 / 423 / 392 / 376 presents at
21.7 / 23 / 25 / 26 ms GPU pictures where one estimate for every step gives 390 / 389 / 376 / 374
(the GPU makes 461 / 435 / 400 / 385), and with 8 ms GPU pictures, a closing step of 8.5 / 9.5 ms
and reads 8 / 10 / 10.5 / 12 ms, 602 / 600 / 600 / 500 and 601 / 600 / 597 / 450 (one estimate:
653 / 600 / 600 / 391 and 601 / 600 / 552 / 301); no step misses its period and none starts later
than the closing step's slack. Those reads are measured with their waits for the GPU and the
submit thread, which vary from picture to picture, so their typical time is under their longest
even at a steady load (the recent longest gave 450 / 423 / 392 / 376 and 601 / 600 / 600 / 450,
601 / 600 / 596 / 400).

**The fence the pipe predicts.** A further picture's reads wait for the GPU's slot fence of the
picture two before it (2 in flight). The picture before's fence (one in flight) is predicted only
with the submit thread off and the presents behind, the present call's wait at its recent longest
half a refresh or more (`pace_rt_pipe_behind`): there a present that waits for the GPU's earlier
pictures holds the render thread's next picture. With the submit thread on, the present and its
wait are the submit thread's job, which the next picture's first graphics call already waits for,
and the GPU keeps two pictures in flight. Before this the picture before's fence was predicted
whenever the presents were behind, and on a phone in stage 6 the submit thread's present
waited 2.2 ms on average and its recent longest 4.5-7.6 ms, over half the 120 Hz refresh most of
a block: every further picture then waited in the plan for the frame's own picture's whole GPU
time (21.7 ms and more) and was held, 32.6 and 31.0 presented fps at 21.7 and 29.5 ms GPU spans
where the block with presents under half a refresh made 44.6 at 22.4 ms, the GPU's own rate. The
model at those blocks' typical times gives 30.2 / 30.1 presented fps with the presents behind all
the time, 41.0 / 46.2 / 34.0 with the fence at two in flight whatever the present waits (the GPU
makes 44.6 / 46.1 / 33.9), with no step missing its period at the two slower blocks
(`pace_loop_test` `testRc12`; `build-host/tmp/v098/evidence/pace5/`). With the frame slots apart
(the Vulkan backend with the timeline sync) the submit thread's job is not before the next
picture, so the render thread's plan gives it as 0 and says the thread is on beside it
(`IcoRtPlan.submitOn`, `PaceRtAsk.submitOn`); a build that read the 0 as the thread off planned
the picture before's fence again: a phone at the Original preset (512x448, the GPU 14.8 ms a
picture, the reads 8.7 ms, a 3.4 ms closing step, the presents waiting 8.4 ms at the longest)
predicted it in 595 of 770 asks, held 540 and showed 45.6 presented fps, the Enhanced preset at
848x448 45.5. The model of those blocks gives 46.0 / 45.1 with that reading and 67.1 / 65.3 with
the thread on, the GPU's own 67.6 / 68.5 (63.3 at the Original preset with the GPU's time
alternating 21 / 8.6 ms a picture, as that phone's slow replay lines show), 54.5 at 1696x896
(the GPU's 56.2) and 43.1 / 45.8 / 33.9 at the native blocks above (`pace_loop_test` `testRc13`;
`build-host/tmp/v098/evidence/pace6/`). The line `window: step rule: the submit thread on (the
frame slots apart|the next picture waits for its job): the further pictures planned with two
pictures in flight, ...` (or `... off: ... the picture before's fence ...`) says which reading the
rule took, once each time it changes; the sum line's `the fence of the picture before predicted
in 0: the rest the submit thread's wait` says the same per block. The pipe's GPU time a picture
is the median span (above): from the replay's start on the GPU to its fence when the spans do not
overlap (`window: GPU between replays`), which is what the pipe adds to the later of the reads'
end and the picture before's fence. If the GPU's real time were under the spans (a wait the spans
counted that did not delay the fence), the rule would hold at the spans' rate and no lower: at the
Original preset 67.7 presented fps with 2 or 8 ms GPU pictures read as 14.8
(`build-host/tmp/v098/evidence/pace6/overstate.py`). With the driver's
back-pressure itself (a present that returns only once the GPU finished the picture before), the
submit thread's job carries the wait and the plan sees it there (26 ms pictures: 376 presents in
10 s where the picture before's fence gave 320; the GPU makes 385; no step missing its period).

**The presents-behind hold reads the submit thread too.** On the paced path (vsync on with a
mailbox, or vsync off with a cap; not as made) a further present is also held while the present
call's wait at its recent longest is half a refresh or more (`PACE_RT_BEHIND`). That hold is for
a blocking present on the render thread, which holds the next post's drain with it, so it
applies only with the submit thread off, read as the pipe's fence reads it
(`pace_rt_pipe_behind`: `submitNs` 0 and `submitOn` false). With the submit thread on the wait
is the submit thread's job, which the next picture waits for already and the step rule predicts.
Android's defaults are vertical sync on and frame rate 60; on a 120 Hz screen 60 is not the
refresh, so there is no vsync lock: a mailbox present, the cap's gap of 1/60 s, the paced path.
The PowerVR phone's presents wait half a refresh or more in most asks (a 10 s block of its log
in stage 6 at the native picture: 703 of 1027, its present waits 5.6 ms on average, 31.8 at the
longest), so with the hold reading every present wait as the render thread's those settings
held every further present: the model of that block (the native 2364x1080 picture, the GPU 13.0 ms a replay, the reads 8.8 ms, the submit
thread's job 8.3 ms, a 5.7 ms step) gives 30.1 presented fps for 30 game frames with that
reading, 53.3 with it reading the submit thread (54.4 over 100 s, no step back-off, the fewest in
a 10 s block 533), and 56.8 as made (vsync off, uncapped: the phone showed 60.7)
(`pace_loop_test` `testDefaults`; `build-host/tmp/v098/evidence/fixreview2/dflt_cells.py`,
`.out`). The phone's log says which reading the hold took: `window: step rule: the submit thread
on (...): ..., the present's wait the submit thread's (none held for it)`, and in the game
thread's 10 s line `0 while the presents waited for the display or the GPU`.

**The decision trace.** Each further present the rule holds is traced in the log, the first 8 of
a 10 s block and then every 100th, with every input and intermediate of `pace_rt_step_miss`
(`pace_rt_step_trace`, `PaceRtTrace`, `pace_rt_trace_text`), every time against the next step's
due time:
`window: step rule: held (hold 3 in this 10 s block): asked 7.0 ms before the next step's due
time, the picture in progress done in 0.0 ms, posted at -7.0; its reads would end at +17.4 ms, set
by the GPU's slot fence [the measured reads +2.1, the interpolation -3.8, the submit thread free
-3.0, the fence +12.0, then 5.4 ms of its own]; the fence the picture before's: the presents wait
7.6 ms at the longest lately, at or over half a refresh; the next step closes a frame or is not
known, planned at 9.2 ms and the 2.0 ms margin: it may start 5.5 ms late; held: its start past
that by 11.9 ms; the inputs: reads 9.1 ms (8.6 its own, 3.2 the interpolation), the whole job 9.2,
the submit thread's part 7.7, the GPU 21.7 a picture, the period 16.7`. When the next step passes
and the step after it holds, the line goes on with `the step after: the next step ends at ... (...
without the further picture), the next frame's picture posted at ..., its reads end at ... [...]
(... without); the step after starts at ... against ... without, planned at ... ms: it may start
... ms past its due time ...; held: its start past that by ... ms`. After the 10 s clause a line
sums the block's asks (`PaceRtTraceSum`, `pace_rt_trace_sum_text`): `window: step rule: in 10 s N
further presents asked of the rule, H held by a step's period (F the next step's start past its
slack, by X ms on average; A the step after's, by Y ms), G passed by the rule; the held ones' reads
ended by the measured reads a, the interpolation b, the submit thread c, the GPU's slot fence d;
the presents waited half a refresh or more in K asks (the fence of the picture before predicted
in J...); the asks' means: ...`, the inputs' means over the asks. So a reporter's log says which
test held each picture, by how much, and which wait set it.

**Vsync off, uncapped.** With vsync off and the frame rate uncapped (the timer path, no FIFO lock,
the render thread on: `pace_rt_as_made`) the pictures go as they are made, as on a PC with vsync
off: the [present allowance](#the-present-allowance) refuses nothing (it stays as a measurement,
its 10 s part `vsync off, uncapped: pictures as made (2 in flight at most, the GPU's time would
pace them to N.N a step)`), and the hold while the presents wait for the display or the GPU is
off. The GPU's own back-pressure is the limit: at most two pictures in flight (the slot fences),
and the render thread waits there, inside a picture's reads, which the step rule above sees. The
hold for a picture that ran past its estimate stays (the present's hand-over would hold the game
thread for the render thread's job, which the fences do not bound). `testStepRule` at 9 / 12 / 13
/ 15 / 17 / 25 ms GPU pictures, an immediate swapchain: 1080 / 834 / 770 / 668 / 587 / 400 presents
in 10 s, what the GPU makes (about `10 s / G`, two a step at most), no step missing its period. On
a phone whose vsync off is a mailbox whose present blocks behind the GPU at a door (the PowerVR
driver), the block lands on the submit thread and the render thread waits for it: the player sees
about the GPU's rate (26 ms pictures: 37.6 a second of the GPU's 38.5, the rest held by the step
rule; 32 while the plan took the picture before's fence), and that is the GPU, not a rule. A mailbox still presents once a refresh at most (the
display shows no more). With vsync on (the lock, the grid, FIFO, mailbox) and at Frame rate
Original nothing changes.

### The present allowance

On the timer path and under the lock a step's presents of a frame already shown (the in-between
ones; a new frame's present is always made) are held to an allowance (not with vsync off and the
frame rate uncapped with the render thread on: [there it only measures](#further-presents-and-the-game-step)), so the simulation, whose clock
the audio follows, is never held below real time by presents nobody needs
(`port/platform/pace_policy.h` (`PaceBudget`); `port/platform/pace_policy.c` (`pace_budget_*`)).
On the timer path it is the last term of the refusal after the cost guard above, which stays as it
was: a further present of a frame already shown is refused once the step has made as many presents
(the new frame's included) as the allowance (`pace`). The timer path's allowance starts unbound
(`PACE_ALLOW_NONE`, `pace_budget_reset_unbound`), so nothing is refused before its first drop; under the lock it runs inside `pace_lock_vsync` after the late-present
skip and before the skip guard, a refused slot passing as a skipped one does, so the next present's
alpha stays the grid's.

- **Late**: a step that ends (the next step's start) half a period or more past its due time.
  Only a late step that made at least one in-between present arms the allowance; a late step of new
  frames only (a hitch's catch-up, a load) is not the in-betweens' doing and arms nothing.
- **Excused**: the last present waited a quarter of a refresh or more for the display (the acquire
  and the present: FIFO's full queue; not the fence waits, which are the GPU behind) and the step is
  less than a refresh late, the skip guard's exception restated. `present_once` takes the two waits
  from the backend's counts (`RhiStats`: `acquireNs` and `presentNs` against `fenceWaitNs`); where
  the backend times nothing the whole present counts as the display's. A present handed to the
  [render thread](PLATFORM.md#the-render-thread) (`rd_submit_async`) returns before its own
  picture is made: its display wait is its wait for the render thread's picture before it
  (`rd_submit_waited_ns`: that one's walk, acquire and present, FIFO's full queue), its GPU wait
  the fence waits counted by then, its cost the game thread's time in it; the pacing rules read them as before
  (`pace_loop`'s `testAsyncLock` and `testAsyncRates`). With no submit thread (Direct3D 11) the
  wait for room in the present queue (the frame latency wait the next acquire would make) is made
  at the end of the picture before, after its present, outside its reads
  ([PLATFORM.md](PLATFORM.md#the-render-thread), the display's wait ahead): the game's step no
  longer waits through it as a read wait, which nothing excuses; what is left of it when the next
  present is posted is that present's wait for the render thread, the display's wait above
  (`pace_loop`'s `testRoomOffGame`: at 60 Hz about 14 ms a picture on the render thread, no read
  wait in the step, the period kept; made in the acquire instead, about 4.2 ms of every step's
  time is the display's as read waits). A backend whose fence waits
  hold the display's wait sets `RhiStats.fenceWaitsHoldDisplay` and the whole wait is the
  display's (`pace_display_wait`): OpenGL, whose swap returns at once while the driver waits for
  FIFO's queue inside the next `glClientWaitSync` (issue 126: a GTX 750 Ti under the n = 1 lock
  logged `waited 8.3 ms for the GPU and 0.1 ms for the display` with 3.6 ms of GPU a replay; the
  GPU's wait alone would have held it to the game's 30 on those cheap scenes). Only with vsync on:
  with it off the fence waits are the GPU's alone. Vulkan, D3D12 and D3D11 keep the
  display's wait in the acquire (D3D11 and D3D12: the frame latency wait) and the present, and
  their fence waits are the GPU's.
- **Excused by the disc**: the game thread waited for a disc read in the step (the step profile's
  `cdWaitMs`: a read on the game thread, or one on the disc reader thread not finished by the
  vsync that completes it; [DATA.md](DATA.md#the-disc-reader-thread)) and the step ended no later
  than that wait and half a refresh (`pace_budget_step`'s `discWaitNs`; `PaceLock.discWaitNs` under
  the lock, both from `port/platform/window_disc.c` (`window_disc_wait_ns`)). A step later than
  that is late as any other. `PaceBudget.discExcused` counts the late steps it excused, for the
  10 s disc line (`window: disc: ... 3 late steps excused by disc waits`). Issue 126's D3D11
  playthrough (the lock, n = 1) had 477 steps over 16 ms, nearly all with one disc read of 16 to
  896 sectors on a slow drive, mean 27 ms and at most 123 ms; each pair of them within 60 steps
  held the presents to 0 a step for at least 120 steps, so the streaming rooms never climbed back
  (36-42 fps with 9 ms of GPU). The drop line names the dropping step's disc wait when it had one
  (`; GPU span G ms a replay, disc wait D ms)`). `pace_policy_test` (`testBudgetDiscWait126`: 100
  ms late with 100 ms of disc wait drops nothing, with none or 20 ms it drops to 0, the edge, the
  timer path paced by the GPU, the drop line) and `pace_loop_test` (`testDiscWait126`: every 150th
  step 100 ms long waiting for the disc drops nothing).
- **Armed, then dropped**: the first late step (with an in-between) only arms the allowance for
  `PACE_ALLOW_HOLD` (60) steps; a second late step while armed drops it to one below that step's
  presents, at most its ceiling (below), held 60 steps and armed again. The second may be a step of
  new frames only (the GPU still behind the in-betweens that armed it: the new frame's present
  waits for it), and then the drop is to one below the arming step's presents, when that lowers the
  allowance. One hitch (issue 130: an uncapped RTX 4090 held at one present a step, 60 a second, after a single
  late step) only arms and expires; a machine late every step drops at its second late step.
- **Climb on the timer path**: after the hold, every `PACE_ALLOW_CLIMB_STEP` (60) steps on time,
  straight back to unbound when the GPU gate lets that many presents (`K * G + step < period`: K
  the most presents a late step of this drop made, no more than the ceiling + 1), else up by one
  when the gate lets one more (`(allow + 1) * G + step < period`), else the climb is refused and
  counted. G is the median of the last 8 presented replays' GPU time (`perf_drain`,
  `pace_hist_add`; two replays late) and step the simulation step's own time; with no GPU time
  (no timestamps) the gate passes: the climb probes, and a second late step drops it again. The
  ceiling is `PACE_ALLOW_MAX` (8), where one more climb makes the allowance unbound again. From a
  drop to unbound again takes 120 steps (2 s) when the GPU has the time.
- **Climb under the lock: a probe** (issue 126). `PACE_PROBE_WAIT` (120) steps on time
  after a drop (the 60-step hold among them) the allowance goes straight back to n whatever the GPU
  figure says, and is armed for the next 60 steps: a late step in them drops it again at once (a
  failed probe) and doubles the wait before the next probe (240, 480, then `PACE_PROBE_WAIT_MAX`,
  960 steps, 16 s at 60); a probe that lasts the 60 steps sets the wait back to 120. The figure
  decides nothing here because it overstates the cost: it is the timed replay's stamp span, which
  holds the GPU's idle waits for the CPU's submissions. The reporter of issue 126 (GTX 750 Ti,
  D3D11, Frame rate 60 with vsync) presented 601 pictures in a 10 s block at a 12.4 ms span with
  598 of them waiting for the display, yet after one late step the gate (`1 x 14 + 5` over the
  16.7 ms period) held the same rooms at 30 for 660 to 3600 steps, 32 blocks of a flat 30 in a
  3 h playthrough. A machine that truly cannot keep up pays one late step per probe, at most one
  every 16 s, and stays at the game's own 30 between them. The ceiling is n, which the allowance
  never passes.
- A resync does not reset it; a new lock, a frame rate change, leaving the lock, the pace reset
  (back from the background) and a display change (the GPU times with it:
  [run-time changes](#video-options-and-presets)) do.
- **Paced by the GPU** (issue 130; the timer path where a present never waits for the display:
  vsync off (immediate) or the swapchain in mailbox mode, `pace_budget_paced_mode` with
  `rhi_present_mailbox`, so never on D3D12 with vsync on and not where the surface keeps FIFO;
  Uncapped and numeric rates, the mailbox clamp above the refresh included). A whole number a step cannot say "between 1 and 2":
  an RTX 4090 at 16x needing 11-17 ms a picture was held to 1 a step, 60 a second, by the climb
  gate (`2 x 12 + step` is over the period), where the uncapped presents without an allowance had
  been 60-89 a second; a PowerVR phone with vsync off needing 20 ms a picture was held to the
  game's own 30 (the first paced rule, `2 x G + step <= period + carry`, needed 47 ms in at most
  33.4) where the GPU could draw 50 a second. With G known the budget is a signed running balance
  of GPU time, `balance`, within one period either way: every present, the new frame's (always
  made) too, owes G, every step pays a period, and an in-between present may start while
  `balance - (presents so far this step + 1) x G >= -period` (`pace_budget_allows_paced`): the
  GPU's backlog never passes one period, the latency bound. At the step's end (`pace_budget_step`)
  `balance = clamp(balance - presents x G + period, -period, +period)`, then 0 after a late step
  (the safety net's lateness: half a period or more, not excused) and a positive balance halved
  by a step that ended past its due time by less. There is no step term: the simulation step
  runs while the GPU draws. The presents a step then average `period / G`, the GPU's own pace;
  with a game frame every two steps 12 ms pictures give 83 a second (1, 1, 2 a step), 9 ms 111,
  17 ms 58, 20 ms 50, 25 ms 40 (one in-between every six steps), 29 ms the new frames only (30:
  a step after a new frame never has the time for one more within a period owed), 1 ms ones bind
  nothing. A new frame the GPU cannot keep up with is still made; the balance stops at a period
  owed, so a lighter scene gets its in-betweens back within a few steps. The arm, drop and hold
  stay as the safety net (the held allowance binds as well as the budget); the climb after the
  hold is straight home at its last step (no 60-step probe: the budget is the gate). With no GPU
  time (no timestamps, or not timed yet) the whole-number rule and its probing climb are as above.
- **FIFO keeps the whole-number rule** (vsync on without mailbox, the cap equal to the refresh,
  and the lock): a present there blocks until the display takes it, so the display's refresh, not
  the GPU, sets the pace; an in-between costs a whole refresh of the step's time whatever the GPU
  needs, and a balance of GPU time would allow presents the display then makes the step wait for.
  The display-wait excuse above already keeps that wait from counting against the GPU.

Presents that keep up never end a step half a period late, and then the whole-number allowance
binds nothing: the presents are those of the rule before (`pace_policy_test` (`testBudgetTimerLoop`): a GPU of 4 or 12 ms a
picture at 120 a second, and back-to-back presents of 1 ms (Uncapped without vsync, about 13 a
step), the same presents to the one; `testBudgetUnbound`: 15 presents a step on time are never
refused). At 120 Hz under the lock with the fence (a
replay waits for the GPU's picture two before, `RHI_FRAMES_IN_FLIGHT` 2) and 12 ms of GPU a
picture, the rule before ends steps up to 20 ms late with the skip guard dropping about one present
in three; the allowance holds the in-betweens to one a step, its probes failing back to one with
no step a period late (`pace_loop_test` (`test120Budget`), which prints both). On the timer path at 120 a second the rule
before keeps up to about 16 ms of GPU a picture in that model (the cost guard holds the presents to
the GPU's pace); at 20 ms its steps end 11.6 ms late and the allowance keeps them under half a
period. A GPU-bound phone (20 ms a picture at 60 Hz, n = 1) presents about its new frames only, 30 a
second and under 33 with the probes' pictures, where the rule before presented 50 a second with steps up to 32 ms late; the
allowance may reach 0 under the n = 1 lock. The reporters' sequences are replayed in
`pace_policy_test`: issue 130's one late step among 6 presents a step leaves it unbound, two within
60 steps hold it to 1 and 120 steps later it is unbound again with 1.2 ms of GPU, at 1 with 9 ms
(`testBudgetHitch130`); the item-3 case (cap 120, two presents a step 9 ms late) at 1 and issue
139's GPU-bound lock (19 ms of GPU, 4x supersampling) at 0, the game's own 30 with two failed
probes in 600 steps (`testBudgetSustained`); issue 126's OpenGL wait excused where Vulkan's same
numbers drop and come back, and its GPU-bound OpenGL blocks at 0 (`testBudgetGlWait126`); the
drop, climb, refused climb and 10 s words on those sequences (`testBudgetLines`, with the lock's
clock part, `pace_lock_timing_text`, and the paced words). The lock's probe as rates: issue 126's
D3D11 sequence (a step 21.2 ms late, the next 20.6 ms, a 15 ms span and a 3 ms step that the old
gate refused) back at 1 a step 120 steps after the drop and 600 presents in the next 600 steps,
60 a second (`testBudgetProbe126`); failed probes waiting 240, 480, 960 and 960 steps with the
new frames only between them, and a probe that holds setting the wait back to 120
(`testBudgetProbeWait`). The paced budget: `testBudgetPaced` (vsync off
and mailbox paced, FIFO not; the 12, 9, 17, 20, 25, 29 and 1 ms rates above over 120 steps with
the lowest balance each ends a step with, never past a period owed; a new frame every step on a
29 ms GPU holds the balance at a period owed, no in-between; no timestamps as before; the safety
net, the straight climb and the halving) and `testBudgetPacedLoop` (the timer path's fake clock,
back to back with a 3 ms step and a game frame every two steps: at 12, 9, 17, 20 and 25 ms no
step a quarter of a period late, no drop, no picture queued behind more than a period of GPU
time, at least `600 x period / G - 5` presents in 600 steps and never more GPU time than there
was; 29 ms the new frames only; 1 ms the rule before's presents to the one).

Limits: D3D12 has no mailbox and may lack timestamps (as may OpenGL without `ARB_timer_query` and
Direct3D 11 where its timestamp queries cannot be made), so the timer path's climb
probes on time alone there: two steps a little late every 2 s while the GPU stays too slow (under
the lock the climb is a probe on every backend, the wait doubling up to 16 s). The Steam
Deck at a cap of 90 is the FIFO timer path with `pace_fifo_hold`; its present that returns on a
vblank up to a refresh past the deadline is the display's wait and is excused.

### The vsync lock

Under the lock the simulation steps on a grid of the wall clock whose period is n refreshes; each
step is followed by exactly n presents, the first at once and the others a refresh apart (the grid's
slots), and the k-th present since the last frame closed shows alpha `k / (n * perFrame)`,
`perFrame` the vsyncs between the last two closes (1..4; 2 until measured)
(`port/platform/pace_policy.h`, the vsync lock; `port/platform/pace_policy.c` (`pace_lock_vsync`,
`pace_lock_alpha`)). On a 60.00 Hz display the game then runs 0.1 % fast and the audio follows: the
lock starts the audio at `pace_lock_nominal_ratio` (the simulation's period over n reported
refreshes) and each 10 s block sets the measured `pace_lock_rate_ratio` (at least
`PACE_LOCK_RATE_MIN_STEPS` (100) steps; 1 when lag was dropped in the block or the ratio is over 2 %
from 1) through `ico_audio_set_rate_ratio` (`port/platform/window_host.c` (`window_pace_log`); `port/platform/window_present.c` (`pace_locked`)).

The grid's slot starts at the refresh the display reports (`window_refresh_ns`, from SDL's mode). A
display clock moves it onto the display; the clock never steps the simulation itself and the loop
never waits for it (`port/platform/pace_policy.h`, the vsync lock). `pace_locked` offers the clock
kind when a lock starts: SHOWN when the backend times presents (`rhi_present_timing`), else VBLANK
when the vblank clock started, else none (`port/platform/window_present.c` (`pace_locked`)). The
present mode follows: mailbox with SHOWN, FIFO otherwise (`video_settings`).

**PACE_CLOCK_KIND_VBLANK** (Windows, the vblank clock, FIFO). Every vblank noted goes into
`PaceLock.vb` against the report. Once `PACE_VB_MIN` (64) refreshes are in, the measured refresh
`Tm` is the mean over all of them and two laws run (`port/platform/pace_policy.c` (`vblank_take`)):

- **period**: each step `slot += (Tm - slot) * PACE_VB_KP_PERIOD` (1/64);
- **phase**: the step is wanted `slot / PACE_VB_OFFSET_DIV` (1/16 of a slot) after a vblank, the
  most time before the next one, so a picture finished within 15/16 of a refresh of its step is
  never a refresh late and a full queue cannot ratchet. For each vblank `v`,
  `ph = (v - due) mod slot` and the error `e = wrap_half(ph + slot / 16 - nudgeLeft)`, `nudgeLeft`
  the moves the steps still owe; the steps move later by `e * PACE_VB_KP_PHASE` (1/16), at most
  `PACE_GRID_NUDGE_MAX_NS` (20 us) a step. The first time 64 refreshes are in, and again after a
  resync or a pause, the grid moves by `e` at once (`phaseSet`, half a slot at most).

**The 1 % bound.** The clock is left, and the slot goes back to the report, when `Tm` is more than 1
% off the report at its first use (another output's vblanks) or the slot drifts past 1 % of it, so a
wrong clock never runs the game more than 1 % off its speed; a stopped clock is left the same way
(`PACE_CLOCK_LEFT_PERIOD`, `PACE_CLOCK_LEFT_ERROR`; `port/platform/pace_policy.c` (`clock_leave`)).
The window then keeps the grid on the report until the display or the settings change
(`port/platform/window_present.c` (`pace_locked`, `vblankGone`)). The noted times carry the thread's
wake-up (tens of microseconds), so the step sits that much later after the true vblank
(`port/platform/pace_policy.h`).

**PACE_CLOCK_KIND_SHOWN** (`ICO_VK_PRESENT_WAIT=1` on Vulkan, mailbox): a phase-locked loop on the
pictures shown. At each step the loop takes the presents the display showed; the refresh that showed
one, against the slot it was presented in and the wanted offset, wrapped to half a refresh, is the
error `e`; the steps move by `PACE_GRID_KP * e` (1/64, at most 20 us a step) and the slot by
`PACE_GRID_KI * e` (1/8192), within the same 1 % bound. The wanted offset is the first one seen,
moved a 64th of a refresh each time a picture is shown more than half a refresh later or earlier
than the median of the last `PACE_GRID_RING` (31), once `PACE_GRID_RING_MIN` (15) are in, until
`PACE_GRID_CLEAN` (300) pictures in a row are shown with the run, and half a refresh at most. A
present that started half a slot late or took a whole slot is not counted
(`port/platform/pace_policy.c` (`clock_take`, `clock_seen`)). When this clock stops or is left the
window presents FIFO until the next settings change (`port/platform/window_present.c` (`pace_locked`, `clockGone`)).

**Without a clock** (every platform but Windows, and a Windows clock that did not start) the grid
stays on the report and the presents go FIFO, which shows them in order whatever their phase against
the refreshes (`port/platform/pace_policy.h`; `pace_loop_test` `testNoClockMode`).

**Waited.** A present that waited a quarter of a slot or more waited for the display (FIFO's queue
was full) and is counted (`waited`, `waitedMaxNs`). The wait is the backend's own blocked time where
it gives one, never more than the present's whole time: on Vulkan the acquire
(`vkAcquireNextImageKHR`), `vkQueuePresentKHR` and the fence waits; on D3D12 the frame latency wait,
`Present` and the fence waits; the whole present where the backend times nothing
(`port/platform/window_present.c` (`present_once`, `lock_io_waited`); `port/rhi/rhi.h`
(`RhiStats`)).

**The skip guard.** A step that ends past the next step's due time skips the rest of its presents (a
frame already shown), unless the present before waited for the display and the step is less than a
slot behind: then the lateness is the display's own wait, the queue has pictures to show, and a skip
would show a picture twice. A step a slot or more behind (a slow GPU, a 30 ms step) still skips, so
the guard costs at most one slot (`port/platform/pace_policy.c` (`pace_lock_vsync`, `lastBlocked`)).
`pace_fifo_hold` is not used under the lock. The [present allowance](#the-present-allowance) runs
before the skip guard (`PaceLock.budget`, max n; `PaceLockIo.displayWaited` gives the last present's
wait for the display, and without it the allowance is not judged).

**The late-present skip under the lock.** A lock that presents every new frame keeps the game below
real time on presents slower than the frames, until the resync below. Before each present
the lock asks `pace_late_skip` with the next step's due time as the deadline and the slow arm off: a
frame not presented yet while the next step is a whole period or more late is left unpresented
(`PaceLock.skippedLate`), never two frames in a row (`pace_skipped_last` on the lock's count of
closes, `PaceLock.frames`; a present closes it, `skipOpen`), the same frame again while that holds
and presented once the step is less than a period late. On presents of 39 ms against a 30 frames
a second game (two 2.4 ms steps a frame) more than half the frames are presented, never two in a
row left out, and no lag is dropped, where presenting every frame falls behind by about 10 ms a
frame (43.8 ms of work against 33.4) until a resync
(`port/platform/test/pace_loop_test.c` (`testLateSkipLock`), which prints the counts). The window
adds the lock's count to the first stats line's `late` (`window_pace_log`).

**Resync and off the screen.** More than `PACE_LOCK_DEBT_VSYNCS` (6) periods behind (a stage load)
the lag is dropped and counted and the grid starts again from now. Off the screen (minimised,
hidden, occluded, 0 x 0) nothing is presented and the grid goes on; back on the screen the lock
starts again (`port/platform/pace_policy.c` (`pace_lock_vsync`); `port/platform/window_present.c`
(`pace`, `window_off_screen`)).

**The pause on a swapchain rebuild.** Rebuilding the swapchain (a present mode change, a size event,
fullscreen) idles the device, and the first acquire on the new chain can take ~140 ms on some
drivers; that time is a pause of the host, not lag. `video_apply` times the rebuild;
`pace_first_present_pause` takes the first present on a new swapchain's cost over a refresh
(`RhiStats.swapchains` tells a new one); `window_pace_paused` moves the deadline, the frame times and the
present grid by it, and under the lock `pace_lock_pause` moves the grid and everything timed on it
and has the vblank clock snap the phase again (`port/platform/window_host.c` (`window_pace_paused`); `port/platform/window_present.c` (`present_once`); `port/platform/pace_policy.c` (`pace_lock_pause`)).

**Log lines.** A new lock logs

```
window: vsync lock: the game steps every X ms (n refreshes of Y ms), n presents a step,
        present mode M, CLOCK
```

(one line)

and, on Windows, `window: vsync lock: DWM reports H Hz (P ms); SDL reports H Hz (P ms)`, the
compositor's refresh of the primary display as a cross-check, not a clock
(`ico_display_dwm_refresh`). A clock that stops or is left logs one line, and the first 48 presents
of a lock are logged as their intervals between returns, with the time from the slot to the refresh
that showed each where the clock saw it (`PACE_LOCK_TRACE_FIRST`, `pace_lock_trace_text`)
(`port/platform/window_present.c` (`pace_locked`, `lock_clock_words`)). The lock line, the DWM line,
the trace and the display clock's start line are logged for the first 8 locks of a session, a clock
change for the first 16 (`pace_locked`, `disp_clock_start`). Other pacing lines:
`window: the cap equals the refresh: the game steps every X ms (n refreshes), present mode M` or
`window: the cap ... the refresh: present mode ...` when `video_settings` changes its decision,
`window: present mode M` once at start, `window: display clock: stopped (...)` when the vblank
thread stops itself, and `window: off the screen` / `back on the screen`
(`port/platform/window_host.c` (`video_settings`, `ico_window_open`, `window_disp_take`); `port/platform/window_present.c` (`pace`)).

### The vblank clock

`display_clock.h` is the display's vblanks on the window's clock (`SDL_GetTicksNS`). On Windows
(`display_clock_win.c`) a thread waits on each vblank of the DXGI output whose monitor is the
window's (`MonitorFromWindow` against `DXGI_OUTPUT_DESC.Monitor`) with `IDXGIOutput::WaitForVBlank`
and notes when the wait returned. `dxgi.dll` and `dwmapi.dll` come from the system directory, so an
effects program's `dxgi.dll` beside the executable is not loaded for it. The thread runs at
time-critical priority (a late wake-up is a late note), keeps `ICO_DISPLAY_CLOCK_NOTES` (256) notes
in a ring under a mutex (the oldest dropped), and stops itself when a wait fails or 16 waits in a
row return within 1 ms of the one before (no vblank to wait for: a display switched off, a remote
session). Stopping never waits: a thread still in a wait is left to end on its own after it
(`port/platform/display_clock_win.c`, header comment, `dc_thread`, `ico_display_clock_stop`).

The API: `ico_display_clock_start(sdlWindow)` (false: no clock), `_restart`, `_next(&ns)` (1 a note
taken, 0 none yet, -1 not running), `_name` (the output's name for the log, `\\.\DISPLAY1`),
`_stop`, and `ico_display_dwm_refresh` (`port/platform/display_clock.h`). Every other platform links
`display_clock_none.c`: no clock, `_next` -1, the name `none`.

The window starts the clock with each new lock and stops it when the lock ends and at close; a
display change restarts it on the new output and starts the lock again. The lock takes the notes
when its clock kind is VBLANK; otherwise the window drains them every locked vsync for the display
line (`port/platform/window_host.c` (`window_disp_lock_start`, `window_disp_take`, `window_disp_vsync`,
`ico_window_pump`)).

## Presentation

`rd_present.c` shows the reduced DISPLAY frame (W x H/2) in a centred box of the output with the
shape of the columns it shows (`wExact / 384`, the exact width, with Full pixel on and off; 4:3 in
integer arithmetic at 512) in two blits: DISPLAY to a lines target, nearest, doubling each field
line (skipped with full height); then the lines target to the box, bilinear, black outside, the
columns past the exact width left out of the source rectangle; with Full pixel off on a cropped
frame its scissor is inset by the game's border ([the native frame](#the-native-frame), `rd__border_mask`). The
mirror mode flips the second blit. The output is the swapchain's backbuffer, or a headless RGBA8
texture for the tests and the [replay tool](ARCHITECTURE.md#terms) (`port/render/rd_present.c`,
header comment, `rd__present_box`). After the picture come the overlay (the port's popups and hints,
in output pixels), the deferred text (Enhanced: the frame's text items laid out on the output) and
the top layer (the touch controls); `rd_present_blank` draws the overlay alone (the start-up
screens). With supersampling, or without anti-aliasing when a scaled scene has more texels than the
box has pixels on both axes (never at scale 1), `aa_reduce_ps` box-averages DISPLAY to the box
(`reduceRecord`); with an effects program
and `effects_depth` `blit_depth_ps` writes the scene's depth beside it (`port/render/rd_present.c`).
The UI box is the 4:3 part of the presentation box (`port/render/rd_present.c` (`rd_ui_box_of`)).
Movies draw their own box ([FMV.md](FMV.md#the-picture-on-screen)).

**The colour adjustment.** Options > Picture's Gamma, Picture brightness, Contrast and Saturation
(`RdSettings.toneGamma`, `toneBrightness`, `toneContrast`, `toneSaturation`, 1..100) are PCSX2's
shade boost: its arithmetic (`bin/resources/shaders/dx11/shadeboost.fx`
`ContrastSaturationBrightness`, luma weights 0.2125, 0.7154, 0.0721) with each value / 50 as its
factor (`pcsx2/GS/Renderers/Common/GSDevice.cpp` `GSDevice::ShadeBoost`), so 50 is the identity; the
one deviation is a 1e-6 floor before the power, where a contrast above 50 takes a dark channel below
0 (`port/shaders/tone.hlsli`). It acts on the finished picture in the box blit: `blit_tone_ps`
(`blit_depth_tone_ps` with the effects depth) in place of `blit_ps`, the factors in `DrawCB.param`,
the bars the cleared black (`port/render/rd_present.c` (`blit`, `depthBlit`), `port/render/rd_tone.c`).
The line doubling, the CRT filter's crop and overlay copies (`rd__present_blit`), the turn into a
pre-rotated swapchain and the MSAA resolve keep `blit_ps`. While every row is 50 (or 0, a zeroed
`RdSettings`) `rd__tone_active` is false and the present is the one without the rows byte for byte.
The deferred text and the overlay draw after the box blit and are not adjusted; the movies draw
their own box and are not adjusted either. The four keys are made at start-up on both outputs
(`rd__enumerate_reachable_screen`). Log: `present: colour adjustment gamma G, brightness B, contrast
C, saturation S (50 = the game's picture)` at start-up when on and at every change.

**The reduction stays a pass of its own.** Even where the box is the window's size (the phone's
Resolution Auto with Full pixel: DISPLAY 2366 texels, the window 2364 pixels), the present is not a
copy of DISPLAY at its own pixels: Full pixel's DISPLAY holds one guard texel more at each side
(`RD_FULL_PIXEL_GUARD`), which the box blit leaves out (`sourceCrop`), so the window's pixel x is
DISPLAY's texel x + 1. The reduction drawn straight into the swapchain image would need its own
viewport shifted by a pixel, which the GPU's floating-point viewport transform does not place to
the bit, and two attachments in one pass cannot be a pixel apart; the present's pass also draws the
text, the overlay and the touch controls, flips for the mirror mode and writes the swapchain's own
format. So the present samples DISPLAY as before; what the picture saves is the reduction's own
clear pass, now its draw's load ([WORK_BUFFERS.md](WORK_BUFFERS.md#passes-cut), the clear carry).

**Captures.** Two requests copy a present's output before the overlay (so never the port's popups,
hints or HUD; the deferred text and the CRT filter are in it), in one texture copy when both are
armed at one present, read back after the submit (`port/render/rd_present.c` (`captureRecord`,
`rd__capture_finish`)). `rd_capture_presented` writes the whole output to a PNG (photo mode, F12's
`-present.png`; `rd_capture_presented_max` box-downscales it first). `rd_capture_shot` is the save
state's pause picture ([PLATFORM.md](PLATFORM.md#saving-and-loading)): it takes only a present of a
frame begun by the time it was asked (`g_rd.frameCounter` then; the present's frame from
`rd_last_present_info`), so a later frame's present drops it, crops to the presentation box (the
CRT filter's box under the filter) and hands the RGBA to the caller, which frees it; a second
request, a failed copy or the shutdown tell the caller none. `rd_present_box_size` is the last
present's box size, the picture's shape on screen.

**Present modes.** The window asks for mailbox through `rhi_prefer_mailbox`; what each backend then
takes is [RHI.md](RHI.md#vulkan)'s (Vulkan: mailbox when preferred, FIFO latest ready with
`ICO_VK_FIFO_LATEST_READY=1`, else FIFO; immediate without vsync) and [RHI.md](RHI.md#direct3d-12)'s
(D3D12: `fifo` with vsync and no mailbox mode with it; the waitable chain, `ICO_D3D12_FRAME_LATENCY`
default 2, puts the wait for the display in the replay's "acquire" phase). `ICO_VK_PRESENT_WAIT=1`
gives the lock its SHOWN clock. A vsync change idles the device and rebuilds the swapchain
(`port/render/rd_present.c` (`rd__apply_display`)).

### The turned swapchain

An Android phone held sideways shows the game's surface turned: the surface's current transform is
ROTATE_90 (or 270 held the other way round; issue 179's Mali-G52 and issue 188's PowerVR logs:
`surface transform 0x2 (offered 0x1ff)`), its extent the window's own, landscape (1612x720,
2400x1080). A swapchain made with the identity transform has the compositor turn every image on
the GPU before showing it, work that preempts the game's own (Google,
"Handling device orientation efficiently in Vulkan with pre-rotation",
android-developers.googleblog.com, February 2020: 1-3 ms a frame and a 40 % higher GPU clock on a
Pixel 4 XL; Arm's `surface_rotation` sample the same). So the swapchain is made with the current
transform as its preTransform and its images the extent turned (720x1612), as the spec asks of a
pre-rotated swapchain, and the compositor shows each image as it is (`port/rhi/vk/vk_swapchain.c`
(`vkr_swapchain_create`), the choice `vkr_prerotate_turns` in `port/rhi/vk/vk_present_mode.c`).
The renderer draws everything it drew into the backbuffer (the box blit, the CRT filter, the
deferred text, the overlay with the touch controls and the frame rate corner, the films, the blank
start-up frames) into a texture of the window's own size and orientation, exactly as before, and
one last pass turns that texture into the swapchain image (`port/render/rd_prerotate.c`:
`blit_vs` with `DrawCB.aux1.w` the quarter turns, the full-screen triangle reading the source at
the turned position, one texel a pixel, nearest, the bytes unchanged). `rhi_swapchain_size` stays
the window's size, so every target, every dump, the box, the touch coordinates (window pixels) and
the captures (F12's `-present.png`, photo mode and the save state's picture, taken from that
texture) are as they were; the picture on the screen is the same, and only the image the
compositor gets is turned.

The turn, from the spec (`VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR`: the image content is rotated
90 degrees clockwise), in the image's 0..1 position (U, V), V down, for the picture's (u, v): 90
(V, 1 - U), the picture's top-left at the image's top-right; 180 (1 - U, 1 - V); 270 (1 - V, U).
Android's present reports suboptimal whenever the swapchain's preTransform is not the surface's
transform (AOSP `frameworks/native/vulkan/libvulkan/swapchain.cpp`, `QueuePresentKHR`): with
identity on a turned surface at every present, which changes nothing and is ignored as before;
pre-rotated, when the phone is turned over (90 to 270), and then the swapchain is made again in the
new turn (`vkr_swap_differs`; the surface poll asks the same).

Where it does not apply: identity and the mirrored transforms (the desktop's surfaces are always
identity), a turn the surface does not offer, a quarter turn whose images would pass the surface's
largest image (Android's is 4096 x 4096 at least), and `[dev] prerotate = false`
(`ICO_VK_PREROTATE=0`), the escape, which keeps identity and the compositor's turn
([CONFIG.md](CONFIG.md)). Direct3D and OpenGL never turn (`rhi_swapchain_rotation` is 0).

The cost: the picture is drawn into its own texture and read once more by the turn, a full-screen
pass of the window's size (about 2 x 10 MB of memory traffic a picture at 2400x1080) and one more
texture of the window's size. It replaces the compositor's own full-screen turn of every image;
whether the trade pays on a given phone (a compositor that turns in the display hardware costs
nothing, and there the extra pass is a cost) shows in the GPU span and the frame rate of the 10 s
lines against a run with `[dev] prerotate = false`.

The lines: at a swapchain made in a new turn `rhi_vk: swapchain 2400x1080 pre-rotated 90 (the
surface's transform 0x2; the compositor no longer rotates): images 1080x2400, the present turns
the picture into them (turn change 1)`, back to identity
`rhi_vk: swapchain WxH no longer pre-rotated (...)`; a swapchain made again for a turn alone `rhi_vk: the surface turned (90 to 270
degrees) at a suboptimal present: the swapchain made again (N times so far)` (the first 8, then
every 100th); the renderer's first picture in a turn `rd: present: the 2400x1080 picture turned 90
degrees into the swapchain's 1080x2400 images in one pass (pre-rotated: the compositor shows them
as they are)`; with the escape, the old `rhi_vk: surface transform 0x2 (offered 0x1ff): presenting
with identity, the compositor rotates ([dev] prerotate = false, ICO_VK_PREROTATE=0)`. When the
texture before the turn cannot be made after an image was acquired, the image is cleared at its
own size and presented (`rd__prerotate_blank`: an acquired image is never left unpresented, the
films' rule), and `rd: present: no WxH texture for the picture before the turn; a blank image is
presented in its place (N times)` says so (the first 8, then every 100th).

**The twin.** A headless device has no swapchain to turn, so the tests use a twin: under
`ICO_VK_FAKE_TRANSFORM=90`, `180` or `270` a headless Vulkan device answers that turn
(`rhi_swapchain_rotation`), the present draws its picture as always and then turns it, in the same
pass, into a texture of the turned size, and `rd_read_presented` reads that texture back and turns
it back on the CPU by the table above (`rd__prerotate_twin_read`); `rd_replay_tool_prerotate`
holds the result to the identity present byte for byte ([Tests](#tests)). The twin turns back by
the shader's own table, so it cannot tell a 90 from a 270 swapped on both sides; the direction
itself is pinned in `shaders_pixel` (`checkTurns` in `port/shaders/test/shaders_pixel_test.c`):
`blit_vs` turning a 3x2 picture 90, 180 and 270 degrees against images written out by hand from
the spec's words and Google's sample (the picture's top-left at the image's top-right at 90).
Only a phone shows whether the spec's reading is the device's.

### The FIFO fallback

With vsync off a surface without immediate mode (Android) presents in mailbox mode with one image
over the surface's minimum. Qualcomm's own Adreno driver can then spend 376-453 ms in
`vkQueuePresentKHR`, one to eight times every 10 s, at the same length whatever the scene and with
the GPU at a steady 28 ms a picture: the queue's wait in the driver or the compositor, each one a
stutter. The window counts them and presents FIFO for the rest of the run once they repeat
(`port/platform/present_fallback.h`, the decision, pure; `port/platform/window_present_fallback.c`,
the window's side, called from `port/platform/window_host.c` (`video_settings`, `present_blocked`,
`ico_window_pace`, `perf_log`)):

- A block is a present that spent 200 ms or more in the driver's present call (the replay record's
  present time, `present_blocked`). Three within 30 s switch, once a run, when the vsync option is
  Off, the swapchain is in mailbox mode, the backend is Vulkan and the driver is Qualcomm's own (vendor id `0x5143`, text naming neither
  `Mesa` nor `turnip`: Turnip on the same tablet showed a few blocks of 230-238 ms in a whole run)
  (`ico_present_fallback_note`; the window's `gate()` asks `ico_present_fallback_gate`, which
  holds the whole decision without the window, `ico_present_fallback_qualcomm_own` in it). Any
  other driver answers
  `GATE_OTHER`: the blocks are counted, mailbox is kept, and the window writes `window: present
  blocked 3 times (204-240 ms) in 8 s on ADAPTER (DRIVER) under mailbox: mailbox kept (presents go
  FIFO by themselves only on Qualcomm's own driver, where the blocks were measured; [dev]
  present_fallback = fifo asks for FIFO here)` once (`ico_present_fallback_other_line`), with no
  `toward FIFO` words on the 10 s lines and no `FIFO fallback armed` on the start's line. A Mali-G52
  (issue 179) took the switch after three blocks of 204-240 ms and its present calls over
  100 ms went from 1-4 to 12-23 a 10 s block (blocks of 285-308 ms inside `vkQueuePresentKHR`), and
  the switch's evidence base, issue 123's Qualcomm logs, had the stall continue under FIFO.
- The switch line is `window: present blocked 3 times (376-382 ms) in 14 s on ADAPTER (DRIVER) under
  mailbox (vsync off, 6 images): presents now FIFO for this run ([dev] present_fallback=off keeps
  mailbox)`. At the step's end the backend's flag is set (`rhi_present_fifo_fallback`), the
  swapchain is made again at its own size as on any present mode change, its time taken out of the
  pacing (`pausePendingNs`), and `window: present mode fifo, FIFO fallback taken after N blocks`
  follows the backend's `rhi_vk: swapchain ... present mode fifo (vsync off, fifo fallback) ...`
  line. Every later swapchain is FIFO with exactly the surface's minimum image count (two when the
  minimum is one).
- The vsync option and `config.toml` are not touched: Options still says Off, and the next run
  starts in mailbox again.
- At 120 Hz a FIFO present waits a refresh (8.3 ms) at most; a 28 ms GPU never fills the queue, so
  the game's own 30 pictures a second stay, and vsync off keeps the paced allowance whatever the
  present mode (`pace_budget_paced_mode`; `pace_policy_test` (`testBudgetFifo120`)).
- The start's `window: present mode ...` line and the summary end `, FIFO fallback armed` where the
  switch can be taken (Qualcomm's own driver only), `, FIFO fallback taken after N blocks` once taken, `, FIFO fallback set by
  [dev] present_fallback=fifo` under that value.
- `[dev] present_fallback` ([CONFIG.md](CONFIG.md#dev)): `auto` (the default) as above, and FIFO from
  the start on Qualcomm's own driver (below); on other drivers `auto` keeps mailbox and `fifo` is the
  A/B; `off` keeps
  mailbox and writes `window: present blocked 3 times over 200 ms in N s under mailbox: mailbox kept
  ([dev] present_fallback=off)` once; `fifo` presents FIFO from the start.

The first two or three stutters (20 to 30 s) still happen before the switch, except on Qualcomm's
own driver, which starts FIFO:

- **FIFO from the start on Qualcomm's own driver (Android).** With `[dev] present_fallback` unset
  or `auto`, on Android only (`ico_present_fallback_start_asked`; Windows on Snapdragon reports the
  same vendor id with Qualcomm's own Vulkan driver, and its surface offers immediate mode, so Vsync
  Off stays immediate there), vsync off, the Vulkan backend and `vendorId` 0x5143 with a driver text naming neither `Mesa` nor
  `turnip` (`ico_present_fallback_qualcomm_own`, `ico_present_fallback_start`), the first swapchain
  is FIFO at the surface's fewest images, as the fallback's own swapchain is, so no mailbox to FIFO
  switch happens mid-run and the GPU draws nothing that is never shown. The window hands the
  rhi the question before the device exists (`window_fifo_set` -> `rhi_present_fifo_start`); the
  swapchain asks it with the device's ids when it is made (`vk_swapchain.c`), only with vsync off,
  and its `rhi_vk: swapchain` line ends `, fifo from the start`. The start line is `window: present
  fifo (Qualcomm's own driver: FIFO from the start; [dev] present_fallback = off keeps mailbox)`
  once, before the `window: present mode fifo` line, and the summary's present word is `, FIFO from
  the start (Qualcomm's own driver)`. `off` keeps mailbox for the whole run (the A/B); `fifo` is
  unchanged; every other device and vsync on are unchanged. The gate sees a FIFO swapchain, so no
  switch is counted or taken after it. The reason is Qualcomm's Adreno best-practices guide (FIFO
  with a minimum image count of 3; mailbox "can sometimes help with frame pacing/latency, but often
  can cost significant battery and generate significant heat for no benefit"). It is not the fix
  for issue 123: the stalls continued under FIFO in three logs. Pacing is the fallback's after its
  switch: vsync off keeps the uncapped rules and the paced allowance whatever the present mode
  (`pace_rt_as_made`, `pace_budget_paced_mode`; `pace_policy_test` (`testFifoFromStart`)).

### The refresh rate and the frame rate pin

Issue 123's log (Xiaomi Pad 6S Pro, 144 Hz LTPO panel, Qualcomm's own driver) shows a present blocking
344 to 365 ms five times in 150 s under mailbox and under the FIFO fallback alike, with the GPU
idle: not the GPU and not the present mode. What the log did not hold was the screen's refresh
rate, so the next log carries it (`port/platform/display_rate.h`, `window_display_rate.c`):

- `window: display refresh: 144.00 Hz, 3048x2032 "<display>" (run time 85.2 s, N changes this run)`
  at the start and in every 10 s block (SDL's `SDL_GetCurrentDisplayMode`; the rate from its
  numerator and denominator where it gives them), `window: display refresh changed: A Hz -> B Hz,
  WxH (run time T s, change N of this run)` for a change SDL announced
  (`SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED`), and `window: display refresh changed without an event
  from SDL: B Hz` for one no event announced. A change is more than 0.05 Hz. SDL posts the event
  from its own thread (Android's Java thread), and `SDL_GetCurrentDisplayMode` and
  `SDL_GetDisplayName` are main-thread calls, so the event watch only counts the events and keeps
  the last one's run time; the 10 s block's poll on the main thread reads the mode and writes the
  change's line with T the event's run time (`display_rate_poll`, pure; `display_rate_test`). The
  line can come up to 10 s after the change; its run time is the change's.
- `window: present calls: N timed (over 20 ms a, over 50 ms b, over 100 ms c, over 300 ms d), longest
  M ms, total T ms` in every 10 s block, on every platform: each presented replay's present time
  (the submit thread's own timing of its present call, `RdPerfRecord.presentMs`) in every row it
  is over (`port/platform/present_hist.h`). The older `window: present blocked N times over 50 ms`
  line and the first-8-then-every-100th `a present blocked` lines stay.
- Android, 11 and later: while the Frame rate row is a number the window asks the system for that rate
  as a fixed source (`window_frame_rate_pin`, from `video_settings`: `ANativeWindow_setFrameRateWithChangeStrategy`
  with only-if-seamless, or `ANativeWindow_setFrameRate` where that is the only one the phone has;
  both through `dlopen("libandroid.so")` and `dlsym`, so the APK keeps minSdk 29; the ANativeWindow
  from `SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER`). Original, Uncapped, `[dev] frame_rate_pin = off`
  and a rate above 1000 ask for none and release an earlier ask; the window closing releases it.
  A seamless change only: a mode change that blinks the screen is not asked for. The decisions are
  `display_pin_mode`, `display_pin_rate` and `display_pin_next` (pure; `display_rate_test`). The first
  call says `window: frame rate pin: on ([dev] frame_rate_pin = auto); the device offers the call` (or
  `does not offer`, or `(no native window from SDL)`), then one line each time the ask changes:
  `window: frame rate pin: asked 60 Hz (fixed source; the Frame rate row is 60) with <call>:
  returned 0` (`(refused)` after a return other than 0, tried again at the next change), and
  `window: frame rate pin: released (the Frame rate row is Uncapped) with <call>: returned 0`.
  The ask is a vote on the Surface: going to the background destroys the SurfaceView's surface and
  the foreground brings a new native window with no vote. The pin keeps the native window it asked
  on (`DisplayPin.surface`); a call with another one, or the pump's `resume_recheck` after the app
  came back (`window_frame_rate_pin_resumed`: the pointer may be reused), forgets the ask
  (`display_pin_surface`) and asks again: `window: frame rate pin: asked again for a new surface:
  60 Hz (fixed source; the Frame rate row is 60) with <call>: returned 0`. A new surface with no
  rate wanted asks and says nothing. Nothing runs on any other platform. It is a hint to the system's refresh rate governor: it does not
  change the game's own pacing, and nothing about it is measured on a device yet.

### Downsampling

`RdSettings.downsample` (Options > Picture > Downsampling: To original size; `[video] downsample =
"box"`, default `"off"`, kept outside the preset) draws the scene at the Resolution row's scale and
box-averages DISPLAY to its GS size, `w x h` of the target record (twice the rows with the full-height
scene), before the two blits above, which then draw it exactly as they draw Resolution 1x: the line
doubling, the crop of the columns past the exact width, the border mask and the mirror. The reduce
is `aa_reduce_ps` with no crop, into a texture of its own (`s_down`), integer with its rounding
defined, so its bytes are the same on every backend; the reduce to the window's box is skipped
after it (`port/render/rd_present.c` (`downsampleRecord`, `reduceInto`)). It is in force
(`g_rd.downsample`) with a scale or a fixed size and no CRT filter; never at the `window`
resolution (one texel an output pixel already); at 1x it is in force and does nothing (DISPLAY is
its GS size) (`port/render/rd_present.c` (`rd__apply_display`)). With it, supersampling is measured
against the GS grid instead of the window: `perPx` is the scale, so 4x with SSAA2 adds nothing (the
`skipped: the resolution has 4.00 texels an output pixel` note), 2x with SSAA4 draws 4x and 1x with
SSAA2 draws 2x, one reduce for both. The display line ends its options with `, downsampled to original
size` and is logged again when that changes. The replay tool's `--downsample` sets it, and `--stats`
prints `present: downsample WxH -> wxh, window reduce off|WxH -> wxh` (`rd__last_present_reduce`).

## Interpolation between ticks

With `RdSettings.interpolate` on (any frame rate but Original) `rd_end_frame` only closes the frame
and the window calls `rd_present(alpha)`: the last closed frame is replayed with every keyed draw
blended from its match in the frame before, weight alpha on the newer one, so the picture runs one
tick behind the simulation and never feeds back into it (`port/render/rd_interp.c`, header comment).
What blends: the VU draws' blocks (the UV scroll, the common block's matrices, the model and light
matrices, the bones), a grid's vertices, particles' positions and sizes, screen prims' XY, Z and
colour, shadow volumes prism by prism, and the overlay text's anchors and colours; matrices blend as
a slerped rotation and a lerped stretch, and the camera is one rigid blend for the frame that every
VU draw is re-based on. A draw snaps (shows the newer frame's data) without a match, when its shape
changed or when it jumped (a screen prim of one tick only fades in or out; the same prim redrawn
under a new key is drawn whole, [WORK_BUFFERS.md](WORK_BUFFERS.md#screen-sprites-redrawn-under-a-new-key-between-ticks)); the whole frame snaps on a camera cut, a fade edge, a keep frame, a large
camera turn or move, an eye whose path crossed a wall or a floor of the stage
([RENDER.md](RENDER.md#camera-blending)), and after the targets were recreated. CLUT animation, the rand-driven draws,
the dissolve and film noise are held at the tick, and the feedback passes read the tick's first
present's copies (`FEED_HELD`, `DISPLAY_HELD`), so the motion blur runs once a tick at any present
rate (`port/render/rd_interp.c`, header comment). Under Motion blur: Smooth
(`[video] effect_motion_blur_mode = "smooth"`, `RdSettings.motionBlurSmooth`) the motion blur's
sprite instead reads an RGBA16F accumulator of the scenes of every present, `A = S + a' (A - S)`,
with `a' = FIX' / 128` and `FIX' = clamp(round(128 (FIX / 128)^(dt / tick)), FIX, 127)` for a present
dt after the one before (dt held to [tick / 64, tick]), so the trail moves on at every present and
keeps the PS2's share over a tick whatever the count or spacing of the presents; no
`DISPLAY_HELD` copy is made then. What it gives up against Original: the light the passes after
the sprite add (the aura, the brightness step, the reduction's tint, a fade) is not fed
back into the trail, and the old frame's vertical halving (DISPLAY's H / 2 lines) is gone; a
still picture is the same within a level. dt is the present clock's time since the present before
on the timer path; under the vsync lock, with no clock, dt is tick / k, k the presents the tick
before counted (the refresh period), 1 for the first tick; at one present a tick FIX' is FIX
([RENDER_POST.md](RENDER_POST.md#the-motion-blur-under-smooth)).

**The still re-present.** A between-tick present whose frame would draw the bytes of the tick's last
rendered present shows that picture again instead of replaying it (`port/render/rd_reshow.c`,
header comment). `rd__interp_frame` hashes the frame it builds with xxh3 before the feedback head
copies (`outHash`): every list's commands, the payload, the start state, the camera, the VU common
block, the ticks' cameras, the head scalars (number, scene size, aspect, exact width, fade, text
items) and the scratch meshes of the present with the mesh each was made from (a scratch mesh's id
is reused across presents). The meshes the draws name are versioned by frame number, so a frame's
ids name the same bytes at every present of the tick. The hash is taken only with the camera still
(`s_cam.still`) or no blended camera, and never under Motion blur: Smooth (its trail is a
per-present accumulator) or with a video up. `rd__present_blended` shows the picture again when the
hash equals the last rendered present's of the same frame, no other replay ran since
(`g_rd.replayCounter`) and what the frame does not carry is unchanged: the settings, DISPLAY's and
SCENE's textures and sizes, no texture waiting to upload (`texDirtyCount`), the device alive. The
replay is the empty keep frame's (no pass; the present chain draws DISPLAY again) with the frame's
exact width and camera and the deferred text laid out from the frame
(`rd__overlay_reshow_text`). A tick's first present is always rendered. FEED128 and DISPLAY are
where the rendered present left them, which is where the skipped one would have left them (it
starts from the held copies and draws the same frame). `[dev] still_represent = false`
(`ICO_RD_STILL_REPRESENT=0`) renders every present; the 10 s `interp:` line says
`still re-present ([dev] still_represent = true): N of M presents shown again without a replay;
rendered: ...` with the reasons, the window's uploaded-KB line ends `; N presents re-shown without a
replay`, `RdPerfRecord.reshown` marks the replay, and `rd_replay_tool --presents` ends each present's
line `shown again` or `rendered`.

**The glow between ticks.** A tick's later presents paste the glow its first present drew instead
of drawing list 7's glow chain again (`port/render/rd_glow_hold.c`; [WORK_BUFFERS.md](WORK_BUFFERS.md#the-glow-between-ticks),
[DIVERGENCES.md](DIVERGENCES.md) VD-25): the first copies WORK3 into WORK3_HELD after the chain,
the later ones skip the chain's 17 passes (the mask, the reduction, the blur, the eye blur) and copy
WORK3_HELD back before the paste. On a still tick the picture is the same bytes; during a pan the
glow's halo is the tick's first present's, half a tick behind the scene. The edit is made after the
still re-present's hash: two presents of a tick whose frames hash alike paste the same glow, so the
re-present stays exact. Options > Effects > Glow between frames (Original or Smooth, `[video]
effect_glow_mode`, `RdSettings.glowSmooth`; greyed at Frame rate Original or with Glow off; the
Enhanced preset sets Smooth) draws the chain at every present as well, from the next present, with
`rd: glow between ticks: drawn at every present (Glow between frames: Smooth), from the next
present` or `held (the tick's first present's)`; an explicit `ICO_RD_GLOW_REDRAW` wins over the
row. `[dev] glow_redraw = true` (`ICO_RD_GLOW_REDRAW=1`) draws the chain at every
present; the 10 s `interp:` line says `glow between ticks: held: N later presents pasted the tick's
glow without its chain; ...` (or `redrawn ([dev] glow_redraw = true)`) with the presents that drew it
and why, the window's uploaded-KB line ends `; N between-tick pictures pasted the tick's glow without
its chain`, `RdPerfRecord.glowHeld` marks the replay, and `rd_replay_tool --presents` names it before
`rendered`: `glow drawn (the tick's first present)`, `glow held`, `glow redrawn (...)` or `no glow`.

**The alpha.** On the timer path the window takes alpha from a present clock: it advances by the
running mean of the intervals between presents and moves towards the measured time by an eighth of
the error a present; an error over half a tick, a pause of more than four steps or time going
backwards resets it; `alpha = (clock - tickAt) / tick` in [0, 0.999], `tickAt` the simulated time
the frame closed and `tick` the last two closes apart, held to one to four periods. The step is held
above a quarter of the nominal interval (the Frame rate's gap, a refresh under FIFO), without one
(Uncapped) a 64th of a tick; a present less than a quarter of the nominal after the one before is a
doubled present: it moves neither the mean nor the step, and it moves the clock by its own interval
with no pull towards the measured time, so the clock never goes back at one (a whole step put the
clock ahead of the measured time and the next doubled present reset it backwards, up to 0.28 of a
tick at one every 7th); a reset while presents go on takes the step from that interval when it is at
most four steps or four nominals (a tick without a nominal), whichever is more (a longer one is a
pause and keeps the step), so a rate slower than four nominals is learnt at one reset (a 120 Hz FIFO
at 25 presented fps: one reset, then none). A reset that took a doubled present's 0.5 ms as the step
would hold it there, every later present being over four steps apart and resetting without learning
one (issue 193: Motion blur: Smooth read 64 presents a tick)
(`port/render/rd.h` (`RdPresentClock`); `port/render/rd_present_clock.c` (`rd_present_clock_alpha`);
`port/platform/window_present.c` (`pace`)). `ICO_RD_S2_LEGACY=1` takes the raw measured time instead
and turns off the finer blending (`port/render/rd_internal.h` (`rd__s2_legacy`)). Under the lock the
alpha is the grid's exact `k / (n * perFrame)` (`pace_lock_alpha`).

`tickAt` is the deadline the closing step was due, while the clock is a smoothed estimate, so a
present right after a tick closes can find the clock below it: its value is negative and the alpha
is clamped to exactly 0. Such a present is one between the ticks like any other, drawn through the
tick before's camera with every part of the world, matched or the tick's alone, placed by it
([RENDER.md](RENDER.md#camera-blending)). The 10 s summary counts every present's alpha
(`port/render/rd_interp.c` (`rd__alpha_bin`, `presentLog`)):

```
interp: alpha of the N presents: Z at 0 (C clamped from below, lowest -0.0123; E exactly 0), by tenths a b c d e f g h i j, T at 0.999, O at 1
```

C counts the presents whose clock value was below 0 (the lowest of them after it), E the other
zeros: a clock value of exactly 0, and every 0 of a present the clock did not time (the vsync
lock's alphas are never 0). The tenths count every 0 < alpha < 1 but exactly 0.999 by its first
decimal (a lock alpha such as 0.9995 among them, in the last tenth), T an alpha of exactly 0.999, the
clock's top, and O an alpha of 1, the lock's last slot (`port/render/rd_interp.c`
(`rd__alpha_bin`)); Z + the tenths + T + O = N, the presents of the block. With
`ICO_RD_S2_LEGACY=1` the bin of a 0 follows the clock's value, not the measured time that replaced
it (`rd__present_clock_raw`).

## The CRT filter

`rd_crt.c` is a present-time pass in place of the line doubling and the box blit, and the last pass
of the present but for the top layer. It reads SCENE and DISPLAY and never writes them, so dumps are
the same with it on (`port/render/rd_crt.c`, header comment). It works on the source grid: the PS2
picture's pixels, 512 across at 4:3 and DISPLAY's field lines; on a wider frame the columns the
present shows (the exact width in whole GS pixels, with Full pixel on and off; off, the last pass's
scissor masks the game's border at the grid's scale),
so a phosphor triad covers one of the picture's pixels at every aspect, in a box of the grid's shape
(`rd__crt_crop`, `rd__crt_box`). With the filter on the scene renders at 1x and no
text is deferred. The passes: the overlay drawn into a line-doubled layer and reduced back to the
grid (the UI is part of the picture the tube shows); `crt_bloom_ps` (linear light, a 9-tap
horizontal Gaussian into half the grid's size, RGBA16F); `crt_blur_ps` (the vertical Gaussian);
`crt_ps`, per output pixel: curvature, the source pixel and line the warped position falls in, the
phosphor its column lands on, the beam of its line, halation and bloom mixed in, vignette, rounded
corners, a soft shoulder and gamma (`port/render/rd_crt.c`, header comment; `port/shaders/crt.hlsl`,
header comment).

The modes (`s_modes`): scanlines (the beam alone, no mask), consumer (slot mask, glow, a curved
face), trinitron (aperture grille, flat vertically), pvm (a grille with darker gaps, sharp, flat),
shadow (a dot-triad mask, gently curved). `crt_strength` 0 draws nothing; the config-only overrides
replace the mode's scanline, mask, halation, bloom and curvature values (`rd__crt_resolve`). The
mask fades in with the box: none at 720 output lines or fewer, full at 1080 or more
(`rd__crt_mask_fade`). `crt.hlsl` binds `CrtCB` at b1 space1 (DrawCB's slot and size, `IcoCrtCB`),
the source at t1, the blurred target at t2 and a bilinear clamped sampler at s1, all in space2; it
does not include `common.hlsli` (`port/shaders/crt.hlsl`, header comment).

The colour adjustment ([Presentation](#presentation)) acts on the finished picture before the CRT
filter, so the filter's scanlines, mask, glow and gamma work on the adjusted colours: `crt.hlsl`'s
`toneOf` adjusts every source read (`linearAt` in `crt_bloom_ps`, `linearLoad` in `crt_ps`) before
the gamma decode, the factors in `IcoCrtCB.pad[0..3]` (`c_toneAdj`) and `pad[4]` 1 with it on
(`c_toneOn`; `port/render/rd_crt.c` (`compose`)). A uniform branch, not a pipeline of its own: with
every row at 50 it is not taken and each mode's present is the recorded one (`rd_crt_test`
`checkTone`). This order is the port's own; PCSX2 applies its shade boost to the merged frame before
its TV shader, by our reading of its source (not checked against PCSX2 on a device).

## Hotkeys for pictures

F12 (the frame shown), Shift+F12 (the aspect sweep in photo mode, below) and F11 (the stats lines
every second for 30 s) are [PLATFORM.md](PLATFORM.md#hotkeys-and-dump-zips)'s; `hotkeys_test` checks
them (`port/platform/hotkeys.h`, header comment; `port/platform/window_host.c` (`frame_dump`,
`sweep_start`, `stats_fast_toggle`)).

F12's `-present.png` is taken by the first present after F12 arms the capture, and the line saying
it was written comes from a later pump, after which more presents may have run. So `present_once`
notes `rd_last_present_info` of the first present that succeeds after the arming
(`RdPresentInfo.alpha`, the alpha it was drawn at) and the line names that present:

```
window: F12: wrote <dumps>/frame-<time>-present.png (the present of frame F at alpha A, keep 0: rd_replay_tool --interp A rebuilds it from the zip)
```

`rd_replay_tool frame.zip out.png --interp A frame.zip` then draws the same picture from the zip's
two ticks ([RENDER.md](RENDER.md#dumps-and-the-replay-tool)). When a tick closed between F12 and
that present, the line says `a later frame than the zip's N` instead and the zip cannot rebuild it.
The rebuild is named only when the zip holds the tick before (its `-prev.rddump`): without it the
line says `keep 0; the zip has no tick before to rebuild it from`. A keep frame's present
(`RdPresentInfo.keep`) shows DISPLAY again, not an interpolated frame: `keep 1: the picture shown
again`;
with interpolation off the present is `rd_end_frame`'s, alpha 1 (`port/platform/window_host.c`
(`frame_dump`, `photo_pump`); `port/platform/window_present.c` (`present_once`);
`port/render/rd_interp.c` (`rd_present`); `port/render/rd_present_info.c`
(`rd_last_present_info`, `rd__note_frame_present`)). The alpha says which kind of present it was: 1 the tick's own picture,
below 1 one drawn between the tick before and the tick.

With the tick before in the zip, F12 also arms the same present's own frame
(`rd_dump_blended_arm`; the F12 line ends `; the next present's frame if between the ticks`). When
that present is drawn between the ticks the renderer writes it, and the pump moves it into the zip
(`ico_zip_move_file`) and says which present it was:

```
window: F12: the present of frame F at alpha A was drawn between the ticks: the zip holds the blended present's frame (rd_replay_tool replays it as drawn), <dumps>/frame-<time>-v<vsync>-blended.rddump
window: F12: the present of frame F at alpha 1 was the tick's own picture (the zip's frame)
window: F12: no present between the ticks followed (frame F): the zip holds the frame and the tick before, no blended present's frame
```

The first replays without `--interp`, as the device drew it, beside the zip's two ticks
([TESTING.md](TESTING.md#f12-dumps-and-the-replay-tool)); a zip it could not be added to keeps the loose
file (`; the .rddump is kept`). The third follows an arm that no present took: a present that drew
nothing (a movie, interpolation off), Frame rate set to Original before the next present, or two
frames closed without a present, after `rd: dump: no blended present followed F12 at frame F
(<why>); nothing written` (`port/render/rd_interp.c` (`blendedDumpExpire`)), so a later scene's
present never lands in an old zip. A second F12 collects the first one's result before it arms
again (`port/platform/window_blended.c` (`window_blended_arm`)), so the first zip still gets its
frame.

The sweep forces the native frame's aspect (`ico_video_frame_force_aspect`); the next flip latches
the new width and remakes the scene-sized targets, and the paused photo camera rebuilds the game's
matrices at that width each tick; after `SWEEP_SETTLE` (4) closed frames the F12 path takes the
frame. The presentation's aspect stays the live one; the forcing is cleared after the last aspect,
on leaving photo mode or on a timeout (`port/platform/window_host.c`, the sweep's comment;
`port/game/video_options.h` (`ico_video_frame_force_aspect`)). `tools/native_frame_check.py` reads
the sweep's zip.

## The perf CSV

With `[dev] perf_log = true` the window writes one line per replay to `ico-pc-perf.csv` in the logs
folder (`logs/` beside the executable, in the [user folder](ARCHITECTURE.md#terms) on macOS; none on
iOS) and logs its path (`port/platform/host_config.c` (`ico_host_logs_dir`)). The columns are the
`RdPerfRecord`: the replay and frame numbers, interpolated, keep, presented, the total and each CPU
phase in ms, the backend's fence wait, the creation, bind, draw, barrier, copy and upload counts,
the upload bytes, `gpu_valid`, the GPU total, uploads, each list `gpu_list<N>_ms`, the present, the
replay's start, alpha, `first_of_tick`, each picture effect `gpu_<effect>_ms`,
`gpu_effects_partial`, the VU vertex pre-pass's `gpu_prepass_ms`, `vu_pre_vertices`,
`vu_pre_draws` and `vu_pre_fallbacks` ([RENDER.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)), the walk's
CPU time by list `walk_list<N>_ms`, the upload bytes by what they carry `upload_texture_bytes`,
`upload_uniform_bytes`, `upload_interp_bytes` (of the mesh bytes) and `upload_stream_bytes`, and
the per-pass timing's `passes` (the pieces begun, the display wait not among them), `pass_timed`
and `pass_count` ([RENDER.md](RENDER.md#the-per-pass-timing)), then `gpu_display_wait_ms` (the
scene list's end to the present's own list's start, `RdPerfRecord.gpuDisplayWaitMs`) and `submits`
(the queue submits counted with the record: 2 a presenting picture, its scene's list and the
present's, [RENDER.md](RENDER.md#the-presents-own-list))
(`port/platform/window_host.c` (`perf_csv_open`, `perf_csv_line`)). The file
is flushed after each drain, every vsync (`perf_drain`).

With `[dev] perf_passes = true` (`ICO_RD_PERF_PASSES=1`) every replay is timed pass by pass and the
window writes `ico-pc-passes.csv` beside it, logging `window: perf_passes: one line per pass of
every replay in <path>`: one line per piece of GPU work, `replay,frame,pass,kind,list,effect,
target,width,height,draws,pipeline_binds,gpu_us,cpu_us` (the kind in words: `uploads`, `VU
pre-pass`, `render pass`, `copy`, `snapshot`, `DATE snapshot`, `fog depth read`, `exact blend`,
`MSAA resolve`, `stencil clear`, `present`, `display wait`; the list -1 outside the lists; the effect `none` or
its name; the target `SCENE`, `WORK0`, ..., `block`, `temporary`, `window`, `record` or `none`;
`gpu_us` -1 when a stamp is missing), the replay and frame numbers those of the perf CSV's line
(`port/platform/window_host.c` (`perf_pass_csv_open`, `perf_passes_add`); `port/render/rd_perf.c`
(`rd_perf_pass_csv_header`, `rd_perf_pass_csv_line`)). Timing every pass adds a timestamp between
every two passes, which on a tile-based GPU can stop one pass's geometry overlapping the last
one's pixels: for a measurement, not for play.

## Tests

| test | checks |
| --- | --- |
| `port/platform/test/pace_loop_test.c` | the vsync lock's loop under a fake clock and swapchain (mailbox, FIFO, a FIFO queue, a vblank clock): grid, alphas, phase, drift (`testFifoDrift`), the ratchet (`testFifoRatchet`), D3D12's full queue (`testQueueBlocking`), the skip guard (`testSkipGuard`), the late-present skip under the lock (`testLateSkipLock`), the present allowance with the fence (`test120Budget`), clocks left, off the screen, long steps, pauses; FIFO latest ready (`testLatestReady`), the timer path's FIFO hold (`testFifoHold`), the audio's starting ratio (`testNominalRatio`), the log's trace (`testTrace`); the render thread's further presents: the step-deadline rule's rates and vsync off's pictures as made (`testStepRule`); the lock with Direct3D 11's wait for room in the present queue on the render thread, made after the picture before's present (`testRoomOffGame`) |
| `port/platform/test/pace_policy_test.c` | the slow-present rule, Auto resolution's steps, the present gap, cap versus refresh, the limiter grid, the lock's n and alphas, the grid's period check, the lock's pause, the vblank laws, the first-present pause, the display's intervals, the late-present skip and its loop under a fake clock, the present allowance's rule (`testBudgetRule`), the timer path's unbound start (`testBudgetUnbound`), the reporters' sequences (`testBudgetHitch130`, `testBudgetSustained`, `testBudgetGlWait126`), the window's allowance and clock words (`testBudgetLines`), the timer path with a GPU queue (`testBudgetTimerLoop`) and paced by the GPU (`testBudgetPaced`, `testBudgetPacedLoop`), vsync off under the FIFO fallback at 120 Hz (`testBudgetFifo120`), the step-deadline rule's arithmetic, its back-off, the reads' prediction and the as-made line (`testStepRule`) |
| `port/platform/test/present_fallback_test.c` | the FIFO fallback's decision over the stock driver's blocks (the third within 30 s switches, never a second time), blocks too far apart or too short, Turnip's driver text, `[dev] present_fallback=off`, the window's gate (`ico_present_fallback_gate`: Qualcomm's own yes, Mali, PowerVR, NVIDIA and AMD other, Mesa and Turnip, vsync on, not mailbox, not Vulkan and fifo no, off kept), FIFO from the start on Qualcomm's own driver (auto on Android yes; off, fifo, vsync on, Mesa or Turnip text, other vendors, any other platform no), the lines word for word |
| `port/platform/test/perf_block_test.c` | a block's sums and maxima, the replays over the threshold, the longest-phase text, a reset |
| `port/platform/test/window_video_test.c` | the window mode requests and their readback on SDL's offscreen driver (77 without one): fullscreen at the desktop's size, Borderless, Windowed back at 960 x 720; Fullscreen through `ico_window_video_mode` holds the desktop's mode (its size and refresh rate) or, where the driver takes none, no mode, and `ico_window_video_desktop_mode` returns which. Whether the display goes exclusive and the swapchain's `VK_EXT_full_screen_exclusive` request are the window system's and a Windows driver's: no test here sees them (lavapipe has no such extension) |
| `port/platform/test/hotkeys_test.c` | F11, F12, the stats period, F12's file names |
| `port/render/test/rd_present_test.c` | the display options from a config (an old file's `window_size` ignored and kept as written), the wide camera at 16:9, `rd__apply_display`'s scales, `rd__present_box`, and presented pictures on a Vulkan device |
| `port/render/test/rd_interp_test.c` | blending two synthetic frames: the ends, half way, the snaps, ordinals, grids, meshes, particles, shadows, the present clock |
| `port/render/test/rd_crt_test.c` | the CRT keys, the modes' table and overrides, and each mode's hash at two sizes on a Vulkan device |
| `port/render/test/rd_boot_present_test.c` | a saved start-up through the presenter, present by present, with DISPLAY's probe points |
| `port/rhi/test/rhi_vk_present_mode_test.c` | `vkr_choose_present_mode` (mailbox, FIFO latest ready, FIFO, immediate, and FIFO under the FIFO fallback over every set), `vkr_present_wait_wanted` and `vkr_prerotate_turns` (the turned swapchain's choice: 90, 180, 270; identity, a mirrored turn, a turn not offered, `[dev] prerotate = false` and images past the surface's largest keep identity) |
| `port/render/test/rd_replay_tool_prerotate.cmake` | the turned present's twin (`ICO_VK_FAKE_TRANSFORM=90`, `180`, `270`) read back turned back is the identity present byte for byte: rd_pixel's dump plain, wide (1280x720), with the overlay test pattern and under the CRT filter, and the dumps of `tools/prerotate_dumps.txt` that are here at 640x480 and 2400x1080; each turned run must name its twin |

The ctest names are the file names without `_test`: `pace_loop`, `pace_policy`, `perf_block`,
`window_video`, `hotkeys` (`port/platform/CMakeLists.txt`), `rd_present`, `rd_interp`, `rd_crt`,
`rd_boot_present`, `rd_replay_tool_prerotate` (`port/render/CMakeLists.txt`), `rhi_vk_present_mode`
(`port/rhi/CMakeLists.txt`).
A pacing change runs ([BUILDING.md](../BUILDING.md#building-a-few-targets)):

```sh
tools/dev_build.sh . pace_policy_test pace_loop_test perf_block_test \
    -- -R '^(pace_policy|pace_loop|perf_block)$'
```

`display_clock_win.c` and the D3D12 waitable chain have no test of their own: `pace_loop_test`
drives the lock with a fake vblank clock and a fake FIFO queue, and on hardware the 10 s `lock held`
and `window: display:` lines are the check.
