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
| `port/platform/window_video.h`, `window_video.c` | window mode requests (windowed, borderless, fullscreen) and their readback |
| `port/platform/pace_policy.h`, `pace_policy.c` | the pacing decisions, free of SDL: slow presents, the gap, the vsync lock, the display's intervals, Auto resolution |
| `port/platform/display_clock.h` | the vblank clock's interface |
| `port/platform/display_clock_win.c` | the vblank clock on Windows: a thread on the window's DXGI output |
| `port/platform/display_clock_none.c` | no vblank clock (every other platform) |
| `port/platform/perf_block.h`, `perf_block.c` | a 10 s block of replays by phase (the replays line and its maxima) |
| `port/platform/hotkeys.h`, `hotkeys.c` | F11, F12, Shift+F12 and the stats period |
| `port/render/rd_present.c` | DISPLAY to the output: the box, line doubling, the overlay, the display options as scales |
| `port/render/rd_interp.c` | presenting between ticks; the present clock |
| `port/render/rd_crt.c`, `port/shaders/crt.hlsl` | the CRT filter |
| `port/platform/test/` | `pace_loop_test.c`, `pace_policy_test.c`, `perf_block_test.c`, `window_video_test.c`, `hotkeys_test.c` |

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
cropped; the reduction draws over its border (`port/render/rd_replay.c` (`doBlurSprite`)).
Without it the picture is the same: the same box, scale, crop and replay (the host sets
`RdSettings.borderMask`, and the reduction draws over its border as with the option on,
`port/render/rd_replay.c` (`borderOpen`)), and the presenter masks the game's border over its edge at
the picture's own scale, where the PS2's reduction scissor held it black (`port/render/rd_post.c`
(`postReduction`)): 2 GS columns at each side, `round(2 k)` output pixels with `k` the box's width
over the exact width (6 at 1080p 16:9, 5.625 exactly; 11 at 4K), and `crop / (h / 2)` of the box's
height at the top and bottom, `crop` 2 rows, or 8 at 512 lines (`rd__reduction_crop`; 10 rows at
1080p, 19 at 4K, 34 at 1080p and 50 Hz). The mask is the box blit's scissor inset by the bands, the
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
Original preset at 4:3 is the PS2's picture byte for byte (`port/render/rd_present.c`
(`rd__frame_cropped`), `port/render/rd_replay.c` (`borderOpen`)). The box (`wExact / 384`), the UI box, the pointer, the movie's box and the CRT filter's grid are
the same with the option on and off (`port/render/rd_present.c` (`outputBox`, `rd_ui_box`,
`rd__ui_box`, `rd__apply_display`, `sourceCrop`), `port/render/rd_crt.c` (`rd__crt_crop`); the
targets: [RENDER.md](RENDER.md#frames-and-targets)).

**A draw that samples a render target** clamps at the target's edges while the border is open
(Full pixel on, or the mask on a cropped frame: `borderOpen`): the game leaves its screen-space
lookups (the mirror floors' copy of the scene, the reflections) on REPEAT, and at the frame's outer
columns they wrapped to the far side of the picture, which the PS2's border hid
(`port/render/rd_replay.c` (`drawWrap`); ctest `rd_present` (`checkFullPixelTargetWrap`)). A screen
prim whose texel coordinates lie wholly in one period of the REPEAT past the first keeps the wrap on
that axis (`port/render/rd_water.c` (`rd__repeat_kept`, `rd__screen_repeat_kept`);
`DrawSetup.repeatKept`): the puddle's ripples (`ico2/sugipon/src/puddle.c` (`drawRipple`)) sample
the reflection block at STs 1.05 to 1.95, its middle on the GS, and clamped every fragment took the
block's corner texel, so the ring's segments came out flat and bright (issue 106, on every wide
frame since the clamp). A lookup that runs from about 0 to the size, or across a period's end, stays
clamped. Only screen prims carry the range on the CPU; the VU draws keep the clamp (ctest
`rd_water`, the ripples).

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

**Fallbacks.** A key the parser does not understand logs one `video: ... not understood` line and
takes the default (resolution, aspect, texture filter, window mode, anti-aliasing, frame rate, CRT
mode); the texture pack sizes are clamped with a line; `window_mode` absent reads the `fullscreen`
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
and applies the change (`video_apply`): the window mode through SDL, the rest through
`rd_set_settings` at the next frame, which recreates targets and the swapchain as needed. A change
also logs one `window: display changed:` line naming only the fields that differ
(`port/platform/window_host.c` (`video_apply`, `video_log_changes`)).

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
through `rd_init`, points the Vulkan pipeline cache at `pipelines.vkcache` in the [user
folder](ARCHITECTURE.md#terms) (the app's cache folder on Android), creates the whole reachable
pipeline set before the first frame, runs the fog's self-test and logs one start-up line with the
backend, adapter and options, then `window: present mode M` (`port/platform/window_host.c`
(`ico_window_open`, `window_flags`)).

After a graphics driver crash while the pipelines are built, the crash handler starts the program
again with `ICO_STARTUP_RETRY=<step>:<backend>`: `cache` deletes the loaded pipeline cache, `d3d12`
takes Direct3D 12 when the player chose no backend, `lazy` builds each pipeline when a frame first
needs it instead of before the first frame. `ico_window_open` takes the retry's backend unless the
player chose one, logs the step and skips the up-front pipeline set on `lazy`
(`port/platform/startup_retry.h`; `port/platform/window_host.c` (`ico_window_open`)).

`ico_window_pump` drains SDL's events once per vsync: the close button quits; F11, F12 and Shift+F12
are the diagnostic keys; Alt+Enter flips between Windowed and the last other mode (in memory, not
saved); a pixel size change reaches `rd_resize_output` and Auto aspect; a display change refits a
borderless window, decides the cap and present mode again and moves the vblank clock to the new
output (`port/platform/window_host.c` (`ico_window_pump`, `toggle_fullscreen`)). The window's
fullscreen state is read back from SDL, not the option, since the window manager can refuse a
request (`port/platform/window_video.h`; `port/platform/window_host.c` (`window_fullscreen_event`)).
A lost device shows one message box and ends the session (`device_lost_quit`).

## The stats lines

Every 10 s of real time (every second for 30 s after F11) the window logs a block of lines, in both
presentation modes (`port/platform/window_host.c` (`pace_log`); `port/platform/hotkeys.h`
(`ico_stats_period_ns`)).

**The first line** (`window: N presents, ...`): the presents, the movie presents (and how many
failed), the game frames shown and the frame numbers (which also count the frames the game dropped
unshown, so a gap between the two is not a slowdown), the block's seconds, presented and game frames
a second, the vsyncs and the simulated rate, the resyncs and the lag they dropped, the new frames
the late-present skip left unpresented (`skipped N presents (late L, slow S)`: by arm, the timer
path's and the lock's together, the lock having the late arm only, [Pacing](#pacing)), the longest [replay](ARCHITECTURE.md#terms) and the replay count with how many
were over a refresh, the slow
steps, the frame rate option, the present mode, the display's refresh with presents a refresh, the
window's pixel size, "step late max" (the latest a simulation step started against when it was due),
"pump max" (the longest window event pump, [PLATFORM.md](PLATFORM.md#diagnostics)) and the lock's
state (`pace_log`), one of:

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
0-10, lists 11-12, present), else `no GPU timestamps`; the simulation step's mean and maximum;
and a step's mean of the region rule's card verdicts the CPU decided while the draws were recorded
(the strips and triangles tested, the time;
[VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)) (`perf_log`; `port/platform/perf_block.h`
(`perf_block_max_text`); `port/render/rd.h` (`rd_vu_card_counts`)). The phases are the replay's own:
`wait` the [frame slot](ARCHITECTURE.md#terms)'s fence (`rhi_wait_frame`), `upload` the ring and the
uploads, `acquire` the backbuffer, `walk` recording the VU vertex pre-pass and the lists less `bind`,
`submit`, `present`;
`fence waits` is the backend's blocked time inside wait and readback (`port/render/rd_replay.c`
(`replayFrame`); `port/render/rd.h` (`RdPerfRecord`)). Further lines give the GPU time per list and
per picture effect, the per-replay counts (draws, passes, binds, barriers, uploads) with the block's
creations, the MSAA samples and resolves, and the graphics allocations against the device's limit
with the process's memory (`perf_log`). Three lines come from the per-pass timing
([RENDER.md](RENDER.md#the-per-pass-timing)): `window: GPU top passes: ...` (the most expensive
passes of the block's timed replays, each timed between stamps outside the render passes, with
the count of passes a replay and of those under 0.05 ms; only with GPU timestamps),
`window: passes per replay N: render passes R (lists L, <effect> E, ...), <kind> K, ...` (what a
replay begins, by kind, every replay) and `window: CPU walk ms per replay by list 0-12: ...;
uploaded KB per replay: textures T, meshes M (interpolated I), uniform blocks U, VU streams S,
other O` (`perf_log`, `perf_passes_add`).

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
  is on (the profile's `DATE stencil` and `fog local on`), and the title frame's glow passes are 26 with
  the blur fold on (35 without). A storage-path count in the hundreds is the exact blend
  (the opt-in), not the default.
- `window: GPU top passes: <label> <target> WxH [N passes] D draws X.XX ms, ... (K of P passes a
  replay, T timed replays; passes under 0.05 ms: S a replay, Y ms)`: the five most expensive passes
  of the timed replays, a replay's mean, each timed between stamps outside the render passes. S
  times about 0.08 ms is the fixed cost of the small passes. A `list 0` `SCENE` entry with hundreds
  of draws is geometry; a SCENE entry with few draws and a millisecond or more at its native size is
  a pass broken by a load or store (a copy or a read of the target in the middle of the list); `glow
  WORK1 256x128` with its pass count is the blur chain.
- `window: GPU ms per replay: lists 0-12 ...; effects: ...; present ...; VU pre-pass ...` and the
  replays line's GPU part: the per-list and per-effect times. On a tile-based GPU the work of a pass
  is done where the pass ends, not where its commands were recorded, so a list's or an effect's time
  there is where the work ended and not its own cost ([RENDER.md](RENDER.md#the-per-pass-timing));
  the top passes line and the total are the numbers to compare, and a reporter on such a device
  should not be asked to explain the lists' split.
- `window: per replay ...`: the counts (draws, passes, pipeline and bind group binds, barriers,
  copies, uploads, DATE snapshots). Its exact-blend fields (`exact blends`, `destination-read
  draws`, `storage passes`, `pass switches for the exact blend`) are 0 unless the player opted into
  an exact blend with `[dev] blend`; nonzero ones on a log without that setting are a defect.
  `pipelines created` in the block should be 0 once the set is made (a pipeline made while playing
  is a hitch).

The device class and its flags are in `ico_pc: summary:` (`profile <class>: fog local on|off, DATE
stencil|snapshot, depth stores kept, blur fold on|off, local read yes|no, uint views ...`, a
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
renderer's `rd: slow walk` line ([RENDER.md](RENDER.md#the-start-up-set)). `[dev] slow_step_ms`
(default 8, 0 off) logs up to 5 `slow step` lines a block for simulation steps over it, split into
the step's parts and the pump's, with the step's own card verdicts next to the game threads' time
(`slow_step`).

## Pacing

`ico_window_pace(hz)` runs after every simulated vsync: it presents until the vsync's deadline (50
Hz PAL: 20 ms; NTSC: 16.683 ms) as often as the frame rate and vsync allow, then sleeps to it; more
than 100 ms behind (`RESYNC_NS`) the lag is dropped and counted instead of run off fast
(`port/platform/window_host.c` (`ico_window_pace`, `pace`, `resync`)). A pacing rule is written in
`port/platform/pace_policy.c`, which has no SDL, so `pace_policy_test` and `pace_loop_test` drive it
with a fake clock and swapchain; `window_host.c` only supplies its I/O (`pace`, `pace_locked`, the
`lock_io_*` callbacks of `PaceLockIo`). Three paths:

1. **Frame rate Original**: `rd_end_frame` presented the frame once inside the step; the window only
   sleeps to the deadline (`pace`). No present is skipped: one replay a game frame, as the PS2
   draws, so a renderer that cannot draw a frame within its step slows the game, and the pacer
   drops the lag past 100 ms (a resync).
2. **The timer path** (any other frame rate without the lock): presents at the current alpha until
   the deadline. No further present of a frame already shown once the deadline has passed, while
   presents are slow, off the screen, or (except with a cap equal to the refresh) when the last
   present's cost would end past the deadline (`pace`). Presents are slow when the median of the
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
reports; a numeric cap is `1 / cap`; "uncapped" without vsync is 0 (back to back)
(`port/platform/pace_policy.c` (`pace_present_gap`)). The gap counts on a grid (`pace_next_present`:
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
(`port/platform/window_host.c` (`video_settings`, `pace`); `port/platform/pace_policy.c`
(`pace_fifo_hold`)). Any other cap prefers mailbox (`rhi_prefer_mailbox`).

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
from 1) through `ico_audio_set_rate_ratio` (`port/platform/window_host.c` (`pace_locked`,
`pace_log`)).

The grid's slot starts at the refresh the display reports (`display_refresh_ns`, from SDL's mode). A
display clock moves it onto the display; the clock never steps the simulation itself and the loop
never waits for it (`port/platform/pace_policy.h`, the vsync lock). `pace_locked` offers the clock
kind when a lock starts: SHOWN when the backend times presents (`rhi_present_timing`), else VBLANK
when the vblank clock started, else none (`port/platform/window_host.c` (`pace_locked`)). The
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
(`port/platform/window_host.c` (`pace_locked`, `vblankGone`)). The noted times carry the thread's
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
window presents FIFO until the next settings change (`port/platform/window_host.c` (`pace_locked`,
`clockGone`)).

**Without a clock** (every platform but Windows, and a Windows clock that did not start) the grid
stays on the report and the presents go FIFO, which shows them in order whatever their phase against
the refreshes (`port/platform/pace_policy.h`; `pace_loop_test` `testNoClockMode`).

**Waited.** A present that waited a quarter of a slot or more waited for the display (FIFO's queue
was full) and is counted (`waited`, `waitedMaxNs`). The wait is the backend's own blocked time where
it gives one, never more than the present's whole time: on Vulkan the acquire
(`vkAcquireNextImageKHR`), `vkQueuePresentKHR` and the fence waits; on D3D12 the frame latency wait,
`Present` and the fence waits; the whole present where the backend times nothing
(`port/platform/window_host.c` (`lock_io_present`, `lock_io_waited`); `port/rhi/rhi.h`
(`RhiStats`)).

**The skip guard.** A step that ends past the next step's due time skips the rest of its presents (a
frame already shown), unless the present before waited for the display and the step is less than a
slot behind: then the lateness is the display's own wait, the queue has pictures to show, and a skip
would show a picture twice. A step a slot or more behind (a slow GPU, a 30 ms step) still skips, so
the guard costs at most one slot (`port/platform/pace_policy.c` (`pace_lock_vsync`, `lastBlocked`)).
`pace_fifo_hold` is not used under the lock.

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
adds the lock's count to the first stats line's `late` (`pace_log`).

**Resync and off the screen.** More than `PACE_LOCK_DEBT_VSYNCS` (6) periods behind (a stage load)
the lag is dropped and counted and the grid starts again from now. Off the screen (minimised,
hidden, occluded, 0 x 0) nothing is presented and the grid goes on; back on the screen the lock
starts again (`port/platform/pace_policy.c` (`pace_lock_vsync`); `port/platform/window_host.c`
(`pace`, `window_off_screen`)).

**The pause on a swapchain rebuild.** Rebuilding the swapchain (a present mode change, a size event,
fullscreen) idles the device, and the first acquire on the new chain can take ~140 ms on some
drivers; that time is a pause of the host, not lag. `video_apply` times the rebuild;
`pace_first_present_pause` takes the first present on a new swapchain's cost over a refresh
(`RhiStats.swapchains` tells a new one); `pace_paused` moves the deadline, the frame times and the
present grid by it, and under the lock `pace_lock_pause` moves the grid and everything timed on it
and has the vblank clock snap the phase again (`port/platform/window_host.c` (`present_once`,
`pace_paused`); `port/platform/pace_policy.c` (`pace_lock_pause`)).

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
(`port/platform/window_host.c` (`pace_locked`, `lock_clock_words`)). The lock line, the DWM line,
the trace and the display clock's start line are logged for the first 8 locks of a session, a clock
change for the first 16 (`pace_locked`, `disp_clock_start`). Other pacing lines:
`window: the cap equals the refresh: the game steps every X ms (n refreshes), present mode M` or
`window: the cap ... the refresh: present mode ...` when `video_settings` changes its decision,
`window: present mode M` once at start, `window: display clock: stopped (...)` when the vblank
thread stops itself, and `window: off the screen` / `back on the screen`
(`port/platform/window_host.c` (`video_settings`, `ico_window_open`, `disp_take`, `pace`)).

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
line (`port/platform/window_host.c` (`disp_lock_start`, `disp_take`, `disp_vsync`,
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
screens). With supersampling `aa_reduce_ps` box-averages DISPLAY to the box; with an effects program
and `effects_depth` `blit_depth_ps` writes the scene's depth beside it (`port/render/rd_present.c`).
The UI box is the 4:3 part of the presentation box (`port/render/rd_present.c` (`rd_ui_box_of`)).
Movies draw their own box ([FMV.md](FMV.md#the-picture-on-screen)).

**Present modes.** The window asks for mailbox through `rhi_prefer_mailbox`; what each backend then
takes is [RHI.md](RHI.md#vulkan)'s (Vulkan: mailbox when preferred, FIFO latest ready with
`ICO_VK_FIFO_LATEST_READY=1`, else FIFO; immediate without vsync) and [RHI.md](RHI.md#direct3d-12)'s
(D3D12: `fifo` with vsync and no mailbox mode with it; the waitable chain, `ICO_D3D12_FRAME_LATENCY`
default 2, puts the wait for the display in the replay's "acquire" phase). `ICO_VK_PRESENT_WAIT=1`
gives the lock its SHOWN clock. A vsync change idles the device and rebuilds the swapchain
(`port/render/rd_present.c` (`rd__apply_display`)).

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
changed or when it jumped; the whole frame snaps on a camera cut, a fade edge, a keep frame, a large
camera turn or move, and after the targets were recreated. CLUT animation, the rand-driven draws,
the dissolve and film noise are held at the tick, and the feedback passes read the tick's first
present's copies (`FEED_HELD`, `DISPLAY_HELD`), so the motion blur runs once a tick at any present
rate (`port/render/rd_interp.c`, header comment).

**The alpha.** On the timer path the window takes alpha from a present clock: it advances by the
running mean of the intervals between presents and moves towards the measured time by an eighth of
the error a present; an error over half a tick, a pause of more than four steps or time going
backwards resets it; `alpha = (clock - tickAt) / tick` in [0, 0.999], `tickAt` the simulated time
the frame closed and `tick` the last two closes apart, held to one to four periods
(`port/render/rd.h` (`RdPresentClock`); `port/render/rd_interp.c` (`rd_present_clock_alpha`);
`port/platform/window_host.c` (`pace`)). `ICO_RD_S2_LEGACY=1` takes the raw measured time instead
and turns off the finer blending (`port/render/rd_internal.h` (`rd__s2_legacy`)). Under the lock the
alpha is the grid's exact `k / (n * perFrame)` (`pace_lock_alpha`).

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

## Hotkeys for pictures

F12 (the frame shown), Shift+F12 (the aspect sweep in photo mode, below) and F11 (the stats lines
every second for 30 s) are [PLATFORM.md](PLATFORM.md#hotkeys-and-dump-zips)'s; `hotkeys_test` checks
them (`port/platform/hotkeys.h`, header comment; `port/platform/window_host.c` (`frame_dump`,
`sweep_start`, `stats_fast_toggle`)).

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
`vu_pre_draws` and `vu_pre_fallbacks` ([RENDER.md](RENDER.md#the-vertex-and-triangle-pre-passes)), the walk's
CPU time by list `walk_list<N>_ms`, the upload bytes by what they carry `upload_texture_bytes`,
`upload_uniform_bytes`, `upload_interp_bytes` (of the mesh bytes) and `upload_stream_bytes`, and
the per-pass timing's `passes` (the pieces begun), `pass_timed` and `pass_count`
([RENDER.md](RENDER.md#the-per-pass-timing))
(`port/platform/window_host.c` (`perf_csv_open`, `perf_csv_line`)). The file
is flushed after each drain, every vsync (`perf_drain`).

With `[dev] perf_passes = true` (`ICO_RD_PERF_PASSES=1`) every replay is timed pass by pass and the
window writes `ico-pc-passes.csv` beside it, logging `window: perf_passes: one line per pass of
every replay in <path>`: one line per piece of GPU work, `replay,frame,pass,kind,list,effect,
target,width,height,draws,pipeline_binds,gpu_us,cpu_us` (the kind in words: `uploads`, `VU
pre-pass`, `render pass`, `copy`, `snapshot`, `DATE snapshot`, `fog depth read`, `exact blend`,
`MSAA resolve`, `stencil clear`, `present`; the list -1 outside the lists; the effect `none` or
its name; the target `SCENE`, `WORK0`, ..., `block`, `temporary`, `window`, `record` or `none`;
`gpu_us` -1 when a stamp is missing), the replay and frame numbers those of the perf CSV's line
(`port/platform/window_host.c` (`perf_pass_csv_open`, `perf_passes_add`); `port/render/rd_perf.c`
(`rd_perf_pass_csv_header`, `rd_perf_pass_csv_line`)). Timing every pass adds a timestamp between
every two passes, which on a tile-based GPU can stop one pass's geometry overlapping the last
one's pixels: for a measurement, not for play.

## Tests

| test | checks |
| --- | --- |
| `port/platform/test/pace_loop_test.c` | the vsync lock's loop under a fake clock and swapchain (mailbox, FIFO, a FIFO queue, a vblank clock): grid, alphas, phase, drift (`testFifoDrift`), the ratchet (`testFifoRatchet`), D3D12's full queue (`testQueueBlocking`), the skip guard (`testSkipGuard`), the late-present skip under the lock (`testLateSkipLock`), clocks left, off the screen, long steps, pauses; FIFO latest ready (`testLatestReady`), the timer path's FIFO hold (`testFifoHold`), the audio's starting ratio (`testNominalRatio`), the log's trace (`testTrace`) |
| `port/platform/test/pace_policy_test.c` | the slow-present rule, Auto resolution's steps, the present gap, cap versus refresh, the limiter grid, the lock's n and alphas, the grid's period check, the lock's pause, the vblank laws, the first-present pause, the display's intervals, the late-present skip and its loop under a fake clock |
| `port/platform/test/perf_block_test.c` | a block's sums and maxima, the replays over the threshold, the longest-phase text, a reset |
| `port/platform/test/window_video_test.c` | the window mode requests and their readback on SDL's offscreen driver (77 without one) |
| `port/platform/test/hotkeys_test.c` | F11, F12, the stats period, F12's file names |
| `port/render/test/rd_present_test.c` | the display options from a config, the wide camera at 16:9, `rd__apply_display`'s scales, `rd__present_box`, and presented pictures on a Vulkan device |
| `port/render/test/rd_interp_test.c` | blending two synthetic frames: the ends, half way, the snaps, ordinals, grids, meshes, particles, shadows, the present clock |
| `port/render/test/rd_crt_test.c` | the CRT keys, the modes' table and overrides, and each mode's hash at two sizes on a Vulkan device |
| `port/render/test/rd_boot_present_test.c` | a saved start-up through the presenter, present by present, with DISPLAY's probe points |
| `port/rhi/test/rhi_vk_present_mode_test.c` | `vkr_choose_present_mode` (mailbox, FIFO latest ready, FIFO, immediate) and `vkr_present_wait_wanted` |

The ctest names are the file names without `_test`: `pace_loop`, `pace_policy`, `perf_block`,
`window_video`, `hotkeys` (`port/platform/CMakeLists.txt`), `rd_present`, `rd_interp`, `rd_crt`,
`rd_boot_present` (`port/render/CMakeLists.txt`), `rhi_vk_present_mode` (`port/rhi/CMakeLists.txt`).
A pacing change runs ([BUILDING.md](../BUILDING.md#building-a-few-targets)):

```sh
tools/dev_build.sh . pace_policy_test pace_loop_test perf_block_test \
    -- -R '^(pace_policy|pace_loop|perf_block)$'
```

`display_clock_win.c` and the D3D12 waitable chain have no test of their own: `pace_loop_test`
drives the lock with a fake vblank clock and a fake FIFO queue, and on hardware the 10 s `lock held`
and `window: display:` lines are the check.
