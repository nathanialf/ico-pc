# Testing

How to test the port: which builds to use, the settings that control a test
run, how to replay a player's session, how to read the log and the trace,
how to report a visual bug, the renderer's backend checks, the packages and
CI. Building is in [`docs/BUILDING.md`](../BUILDING.md); every configuration
key is in [`CONFIG.md`](CONFIG.md).

## The two builds

The same game sources build two programs (`ICO_HEADLESS`, docs/BUILDING.md):

- **The window build** (`-DICO_HEADLESS=OFF`) is the game: an SDL3 window,
  the Vulkan or D3D12 renderer, audio and the live pad. It is what the
  packages ship and what a player runs.
- **The headless build** (`ICO_HEADLESS=ON`, the `linux-x64` preset's
  default) has no window and no renderer. It runs the simulation as fast as
  the host allows, takes its input from a pad script, writes a trace by
  default, fixes the disc clock and mounts the disc image directly. It is
  the build for reproducible runs: trace comparisons, replays of recorded
  sessions, stage sweeps and CI.

Both are deterministic for a given pad input, settings and memory card
folder, so a window session's recording replays tick for tick in the
headless build. Neither takes command-line flags in normal use: a
double-clicked executable reads `ico-pc.ini` beside itself and
`config.toml` in the per-user folder, and writes its logs to `logs/` beside
itself (`port/platform/host_config.c`, `port/platform/main_host.c`).

The sanitizer and trap presets (`asan`, `fptrap`) are headless builds that
stop at the first memory error or the first float divide by zero or
invalid operation in the simulation. `fptrap` finds the sites where the
host's IEEE result differs from the EE's saturating one (DIVERGENCES.md,
F5). The heap-ASan build, which poisons the game allocator's block
headers and slack, is described in [`BOOT_DIAG.md`](BOOT_DIAG.md).

## Settings for test runs

`ico-pc.ini` beside the executable wins over `config.toml` in the per-user
folder (`SDL_GetPrefPath("ico-pc", "ico-pc")`: `%APPDATA%\ico-pc\ico-pc\`
on Windows, `~/.local/share/ico-pc/ico-pc/` on Linux; the headless build
uses the executable's folder). Each ini key has a `config.toml` name, and a
true/false toml value reads as 1/0. The keys a test run uses:

| ini key | `config.toml` | meaning |
| --- | --- | --- |
| `iso=PATH` | `[paths] iso` | the disc image (a file-dialog choice is saved here) |
| `saves=PATH` | `[paths] saves` | the memory card folder |
| `use_iso=0/1` | `[dev] use_iso` | 1 mounts the disc image directly; 0 mounts the extracted `ico.o2r`, extracting it on the first run. Default 1 in the headless build, 0 in the window build |
| `verify=0` | `[dev] verify` | skip the image's SHA-1 check when mounting it directly (extraction always verifies) |
| `ticks=N` | `[dev] ticks` | exit after N Main ticks; without it the run goes on until the window is closed |
| `watchdog=S` | `[dev] watchdog` | stop with a report when no Main tick comes S seconds after boot starts, or no new one for 2S seconds. Default 30, 0 off |
| `trace=0` / `trace=1` / `trace=PATH` | `[dev] trace` | no trace / `logs/trace-<time>.txt` / a trace at PATH (a path also fixes the clock). Default on in the headless build, off in the window build |
| `pad_script=PATH` | `[dev] pad_script` | drive the pad from a script or a recording (format in [`INPUT.md`](INPUT.md) and `port/input/pad_script.h`). The headless build also takes a `pad-script.txt` beside the executable; the window build ignores one the settings do not name |
| `input_record=0/1/PATH` | `[dev] input_record` | the pad recording: `logs/input-<time>.txt` (1) or PATH. Default on in the window build, off headless |
| `headless=1` | `[dev] headless` | a run for traces and tests: fixes the clock (the headless build always does) |
| `fixed_clock=0/1` | `[dev] fixed_clock` | the disc clock (`sceCdReadClock`): 2002-01-01 00:00:00 when fixed, the host's local time otherwise. Fixed by default in the headless build, with `headless=1` or with a `trace=` path. A fixed clock keeps the save serial, which contains the clock, and so the trace's save hash reproducible |
| `start_stage=N` | `[dev] start_stage` | boot straight into stage N (`stageData` order), skipping the boot signs and the title ([`DEVELOPER_MODE.md`](DEVELOPER_MODE.md)) |
| `dump_every=N`, `dump_dir=PATH` | `[dev] dump_every`, `[dev] dump_dir` | write every Nth rd frame to `dumps/rd-NNNNN.rddump` (window build; [`RENDER_API.md`](RENDER_API.md)) |
| `dump_from=N`, `dump_interp=1` | `[dev] dump_from`, `[dev] dump_interp` | the first frame `dump_every` writes; also write each dump's interpolated half-way frame |
| `audio=0` | `[audio] enabled` | no audio device |
| `audio_dump=PATH` | `[dev] audio_dump` | a WAV of the mixed audio (`1`: `logs/audio.wav`) |
| `backend=vulkan/d3d12` | `[video] backend` | the window build's renderer backend (default and fallback `vulkan`) |
| | `[dev] slow_step_ms` | log a `window: slow step` line for every simulation step over this many milliseconds (default 8, 0 off) |
| | `[dev] perf_log` | write per-frame timings to `logs/ico-pc-perf.csv` |

A run that must be reproducible uses the headless build, or sets
`headless=1` or a `trace=` path. The packages leave these out, so a player's
clock is real.

The executable also takes developer overrides on the command line, which
win over every file: `--iso PATH`, `--pad-script FILE`, `--trace FILE|none`,
`--ticks N`, `--vsync-rate X`, `--no-verify`, `--console` (keep stdout and
stderr on the console instead of the log) and `--help`. A player never needs
them.

The environment variable `ICO_GALLERY_PLAY` (developer only) gives the
music gallery a list of entries to play when its page opens, one every 8 s,
for headless renders and the `gallery_headless` test
(docs/port/MUSIC.md, "Testing"): `stream:N`, `env:I`, `se:D`, comma
separated.

## Game data on the first run

Without `use_iso`, a run looks for `ico.o2r` in the per-user folder, then
beside the executable. On the first run there is none: it finds the disc
image (`Ico_PAL.iso` beside the executable, else `iso=`, else `ICO_ISO`,
else `baserom/Ico_PAL.iso` under the working folder, else a file dialog whose
choice is saved to the ini), checks it and extracts the game's files into
the archive once (about 870 MB; progress in the log and, in the window
build, a small window). The extractor accepts the image by its SHA-1, or by
the boot ELF's SHA-1 plus the manifest of `DATA.DF` for a re-dump, and logs
which; a wrong image stops the run with a message. Deleting `ico.o2r` makes
the next run extract again. [`DATA.md`](DATA.md) has the details.

## Replaying a session

The window build records the pad in every session (`input_record`, on by
default) to `logs/input-<time>.txt`: what the game read from the pad at
every Main tick where it changed, in the pad-script format, after a header
with the build (`git describe`), the start time and the settings the
simulation depends on (`[video] video_mode`, `[game] language`,
`[gameplay] mirror`, `stick_fix`, `yorda_safe`, `developer_mode`,
`[dev] start_stage`, and whether the clock was fixed;
`record_keys` in `port/platform/main_host.c`).

To replay it, run the headless build with `pad_script=` that file, the same
settings in `config.toml`, `ticks=` past the point of interest and, for a
session started with Continue, a copy of the memory card folder as it was
when the session began (a New Game session needs none: the boot's card
check takes the same ticks with an empty folder). The log then says
`pad script is a recording made with this build and settings`, or names
each header value that differs from the run's; a differing build or
language does not necessarily change the run, but it is the first thing to
suspect when a replay drifts. A faithful replay changes stage at the same
Main ticks and vsyncs as the session did, and its trace matches the
session's trace line for line when the session wrote one (the Windows test
package sets `trace=1` for that reason).

Because the replay is deterministic, a temporary probe called from
`ico_host_main_tick` (`port/platform/trace_host.c`) can print game state at
a chosen tick without changing the run: for a position bug, for example,
`GetRootPosition` of `boyGObj` (the world position, with the parent
object's node matrix and the root height applied), the action mode (names in
`actModeTbl`) and the motion (names in `motionKind`).

## Reporting a visual bug

Press **F12** when the problem is on screen, then send the two files it
names and the session's pad recording:

- The log gets a line
  `window: F12 at vsync V, Main tick T, frame F: wrote <pref>/dumps/frame-<time>-vV.rddump and ...png`.
  The `.rddump` is the last frame the game recorded, as an rd frame dump
  ([`RENDER_API.md`](RENDER_API.md)); the `.png` is the picture the window
  last presented (the DISPLAY target, read back once). A dump holds the
  game's textures, so it is for the developers only and is never committed
  or published. `rd_replay_tool` renders it on either backend.
- `logs/input-<time>.txt`. F12 flushes it, so it is complete up to the dump;
  the Main tick in the F12 line says where to look in the replay.

**F11** switches the `window:` statistics lines from every 10 seconds to
every second for 30 seconds, for a stutter that comes and goes; pressing it
again switches back. Slow steps are logged either way (`[dev] slow_step_ms`).

## Reading `ico-pc.log`

stdout and stderr go to `logs/ico-pc.log`, unbuffered, and the
diagnostics (`port/platform/diag_host.h`) write there too. Every way a run
can end leaves a distinct ending:

| the log ends with | meaning |
| --- | --- |
| `exit: ticks= reached` or `exit: the window was closed`, then `N Main ticks, V vsyncs, stage_no S` | a normal end |
| `CRASH: <exception or signal> at ico_pc.exe+0x…`, the fault address, the game thread and its last kernel call, stack candidates, the last assertion message, a heartbeat, every thread, the summary | a crash (exit code 3). Look the offsets up in the link map shipped beside the executable (`ico_pc.map`) or with `addr2line` on the RelWithDebInfo build |
| `WATCHDOG: no Main tick …` or `no new Main tick …`, where the main thread was, stack candidates, every thread with what it waits on | a hang (exit code 4): a spin shows its address, a deadlock shows every thread waiting |
| heartbeat lines ending `no progress for N s` | the run stalled; the watchdog's lines follow |
| `the game called Exit(n)` or `host: the game's main returned` | the game ended itself |
| none of these after the last milestone | the process was killed from outside, or the OS ended it before the handlers ran; the last milestone or heartbeat locates it |

The milestones (`boot starts`, `boot ran until every thread waits`,
`first vsync done`, `first Main tick done`, each `stage_no a -> b`, the boot
sequence's `kanbanBoot: bootStep …` lines) and a heartbeat every 2 seconds
show how far a run got. [`BOOT_DIAG.md`](BOOT_DIAG.md) describes the crash report and the
watchdog in detail. A fatal error (no disc image, a wrong one, a bad pad
script) is logged, shown in a message box, and exits 1.

## The trace

The trace has one line per Main tick (`port/platform/trace_host.c`), each
flushed as it is written, so a crash leaves it complete up to the last tick.
It opens with `# developer_mode 0` or `1` (the developer menu can change the
simulation) and the header line
`# tick vsync stage sys0 sys1 gameover gf0 … gf12 save`:

| column | contents |
| --- | --- |
| `tick` | the Main tick number |
| `vsync` | the vsync count |
| `stage` | `stage_no` |
| `sys0`, `sys1` | `systemStatus[0]` and `[1]` (the video mode, and vsyncs per Main tick) |
| `gameover` | `gameover_flag` |
| `gf0` … `gf12` | the 400 game flags as 32-bit hex words: flag 32N+b is bit b of gfN (New Game sets flag 382, bit 30 of `gf11`) |
| `save` | the FNV-1a 32 hash of `gameSysMainSaveBuff` |

A Main tick is one pass of `Main`'s loop; `common/src/main.c` calls
`ico_host_main_tick()` after `frameReady = 1`. Ticks are not vsyncs: a movie
or a stage load takes many vsyncs and no Main tick. Two runs agree when
their traces are byte-identical; the first differing line gives the tick and
which state moved.

## Booting every stage

`start_stage` makes it cheap to boot each stage on its own. An idle boot of
every stage with data on the disc (1 to 63, 88, 91 and 103 to 105; the
others' data files, `STGNOCD_*` and `STGONLYSAMPLE_*`, are not in
`DFDATAS/DATA.DF`, and `start_stage` refuses them) (600 Main ticks, at 50 Hz and at 60 Hz through
`[video] video_mode`) in the `fptrap` build and in the heap-ASan build is
the check for float traps, out-of-bounds writes and heap exhaustion that a
play-through would only reach hours in. A start-stage boot applies
`[video] video_mode` as the normal boot would, and its game flags are those
of a fresh boot, so a stage that expects earlier progress may behave
differently from a play-through. Achievements are suspended in such runs
([`ACHIEVEMENTS.md`](ACHIEVEMENTS.md)).

A cold boot does not exercise a stage change. During one the old stage's
lists, static state and heap contents are still there while the new stage
initialises (`common/src/StageManager.c`, `start_stage_Load_thread`;
`common/src/icoMisc.c`, `InitIcoMisc`), and the new stage's allocations land
on the old stage's bytes, where a cold boot's are zero. `[dev] switch_to` and
`[dev] switch_at` ([`CONFIG.md`](CONFIG.md)) force one change: from Main
tick `switch_at` the start stage's exit to `switch_to` is taken through the
call the boy's exit floor makes, and the log gets `dev: switch_to N at tick
T` and then `dev: stage N up at tick T`. The transition sweep boots
`start_stage = from`, `switch_to = to`, `switch_at = 300`, `ticks = 900`,
`watchdog = 120` for each (from, to) pair the exit tables define for
stages 1 to 63 (`stageData[].ent` through `exitData[].nextStage`: 240 exits,
167 distinct pairs), in the `fptrap` build at 60 Hz (the heap-ASan build
and 50 Hz are the same driver with another build or `video_mode`). A run
passes when it reaches 900 ticks with `dev:
stage N up` in the log and no crash, trap or ASan report. Stage 1's one exit
(to 41) is taken by the opening, after the title: forced at tick 300 it
lands on the boot signs and fails a texture assertion, so that pair is
checked by replaying a recording that plays from the boot into stage 41 and
on (`pad_script`). The forced switches do not cover the door and exit
animations, the boy's walk into the exit, the girl following him (the
switch takes the boy alone, as an exit taken without her), the exits that
only a script takes at a point in its own sequence, nor a play-through's
game flags; a play-through or a recording covers those.

## The renderer backends

The renderer has a Vulkan backend and a Direct3D 12 backend
(`port/rhi/`, `port/rhi/d3d12/README.md`). The Windows presets build both
(`ICO_RHI_D3D12`, on by default for 64-bit Windows). Three checks cover
D3D12, in order:

1. **`rhi_d3d12_test.exe`.** Copy it and `SDL3.dll`
   (`build-host/<dir>/port/rhi/`) into any folder and double-click it. It
   shows no console, writes `rhi_d3d12_test.log` beside itself and ends with
   a message box listing four parts: the exact-texel cells of
   `port/rhi/test/rhi_test_common.c` (dual-source blend, stencil wrap,
   reversed-Z depth with exact readback, colour masks, an integer target,
   texture upload and sampling, copies) on WARP, Windows' software
   rasteriser, which is the reference; the same cells on the default
   hardware adapter; a swapchain test with a resize; and the same cells on
   Vulkan for comparison. Every line must read "pass" ("skipped" is
   acceptable for the hardware adapter and for Vulkan). With the D3D12 debug
   layer installed (Windows' optional feature "Graphics Tools") any
   debug-layer error fails the run. Exit code 0 pass, 1 fail, 77 nothing ran.
2. **The game on D3D12.** Set `backend=d3d12` in `ico-pc.ini` or
   `[video] backend = "d3d12"` in `config.toml`. The log's `window:` line
   then names D3D12 and the adapter it runs on.
3. **Vulkan against D3D12 on the same frames.** Write dumps with
   `dump_every=N`, then put `rd_replay_tool.exe`, `SDL3.dll`,
   `compare_backends.cmd` and `compare_png.ps1` (`port/rhi/test/`) beside the
   `dumps\` folder and double-click `compare_backends.cmd`. For every dump it
   renders the DISPLAY target and the Original presenter's 640x480 output on
   both backends and writes `compare_backends.log` with, for each pair, the
   largest difference; the target is agreement within 1 LSB. By hand:

        rd_replay_tool.exe dumps\rd-00100.rddump vk.png --backend vulkan
        rd_replay_tool.exe dumps\rd-00100.rddump dx.png --backend d3d12
        powershell -NoProfile -ExecutionPolicy Bypass -File compare_png.ps1 vk.png dx.png

`rd_replay_tool` also takes `--target NAME`, `--present WxH`, the display
options (`--enhanced`, `--aspect`, `--resolution`, `--full-height`,
`--filter`, `--mirror`) and inspection switches (`--list`, `--nop`,
`--mesh`, `--dump-textures`, `--stats`: the replay's draw, pass and bind
group counts); its usage is at the top of
`port/render/tools/rd_replay_tool.c`.

The renderer's own tests (`rd_*`, `rhi_*`, `shaders_pixel`, `vu1`) run on
Vulkan by default; `ICO_RHI_BACKEND=d3d12` runs them on D3D12 and
`ICO_D3D12_ADAPTER=warp` picks the software rasteriser. They count Vulkan
validation errors only, so on D3D12 read the debug-layer lines in their
output. On Linux without a GPU they run on Mesa's lavapipe.

The D3D12 backend has not yet been run on Windows hardware
([`docs/TODO.md`](../TODO.md)).

## The dump corpus

A change that may move pixels or the simulation is checked against a baseline
kept under `build-host/tmp/corpus/` (never committed). For each of a boot
(`port/input/pad-boot.txt`, 1500 ticks) and `start_stage` runs of 600 ticks
(a plain stage, a stage with puddles, one with lightning and the Queen's
stage) the headless build writes a `trace=` file; the window build, run with
`SDL_VIDEODRIVER=offscreen` (it renders on lavapipe and needs no display),
the same ini plus `headless=1`, `dump_dir=`, `dump_every=N` and `dump_from=N`
writes `.rddump` frames, three kept per run. `rd_replay_tool` then renders each
dump as the DISPLAY target, with `--present 640x480` (Original), with
`--mirror`, and with `--enhanced` at 16:9 and at `--resolution 4x`, and a
`SHA256SUMS` over the PNGs and traces records the result. After a change,
repeat the runs into another folder and compare the sums and the traces;
compare PNGs and traces, not the `.rddump` files, whose bytes differ between
identical runs. The window run's trace is byte-identical to the headless one.
`--mirror` in the tool flips UI prims only, so it matches the Original render
of a frame that has none. The scripts and the exact inis are in that folder's
`README.md`.

## Unit tests

`ctest` in a Linux build directory runs the unit tests: the platform layer
(`fpenv`, `fiber_guard`, `memory`, `ios_chain`, …), the maths, the data
layer, audio, input, settings, achievements, the layout asserts and the
audits (`offset_audit`, `template_audit`), and the renderer. A test that
needs the disc image (`vfs_disc`, `archive_disc`) or a Vulkan device exits
77 without one, which ctest reports as skipped and counts as a pass.
`tables_loader` and `tables_manifest` are built only when a base ELF is
present (docs/BUILDING.md); `gallery` exits 77 without one.
`gallery_headless` runs the headless game itself (a copy of `ico_pc` in
`port/ui/gallery_headless/` of the build directory, with its own
`ico-pc.ini`, pad script and WAV dump): it boots to the title, opens
Settings > Extras > Music and plays one entry of each group through
`ICO_GALLERY_PLAY`, then checks the log and the dump
(docs/port/MUSIC.md, "Testing"). It needs the disc image (77 without),
runs serially and takes about ten seconds. The Windows presets build the test executables
without running them.

## The model viewer run

`model_viewer_headless` (ctest, headless build only, `RUN_SERIAL`, 300 s;
skipped without the disc image at `ICO_DISC_IMAGE`) plays Extras > Models
end to end: `port/game/test/model_viewer_headless.cmake` copies the headless
program into an empty folder under the build directory
(`port/game/model_viewer_run/`, with its own `ico-pc.ini`, card folder and
`logs/`) and runs 1300 ticks of `port/game/test/model_viewer_pad.txt`: the
boot signs, the title (up at tick 438 with an empty card folder), Settings,
six Downs to Extras, Models, Cross on the first model (Ico, stage 8), Cross
to play an animation, and 60 ticks later Triangle to the list and Triangle
to the title. The log must hold `model_viewer: stage S loaded id N "name"`,
a `model_viewer: motion "name" frame F/N` line with F above 0,
`model_viewer: the title is back`, no `model_viewer: failed`, and the run's
`exit: ticks= reached`. The script's comments give each press's tick and
the screen it lands on; a change to the boot's or the title's timing moves
them.

## Packages

**Windows: `tools/package_win.sh <label>`.** Builds HEAD in a clean
detached worktree (`build-host/pkg-wt`, with `.venv` and `tools/toolchain`
symlinked in), so uncommitted work is not packaged; `ICO_PKG_FILES="path
..."` copies those working-tree files over HEAD first, to try a change
before committing it. It builds `win-x64` with `-DICO_LINK_EXE=ON` and
stages one `x64/` folder: `ico_pc_x64.exe`, its link map, `SDL3.dll`,
`ico-pc.ini` (`watchdog=30`, `trace=1`, no `ticks=`; an `iso=` line already
in the staged ini is kept), `LICENSE.txt` and `NOTICES.txt`, and under
`x64/tools/` the backend checks above (`rhi_d3d12_test.exe`,
`rd_replay_tool.exe`, `SDL3.dll`, `compare_backends.cmd` with CRLF line ends,
`compare_png.ps1`). It writes a `TEST.md` for the tester (its text lives in
the script) and zips everything as `dist/ico-pc-<label>-win.zip`. The Windows
link uses `-mwindows -static-libgcc -static`, so the executable imports only
system DLLs and `SDL3.dll`; Vulkan's `vulkan-1.dll` is loaded at run time
from the driver. Keep the zip of any build a tester ran: its RelWithDebInfo
executable and map turn crash offsets into source lines.

**Linux: `tools/package_linux.sh <label>`.** The same for Linux: a clean
worktree at `build-host/pkg-linux-wt`, the `linux-x64` window build, a check
that the binary needs only `libSDL3.so.0`, libc and libm and has the run
path `$ORIGIN`, and `dist/ico-pc-<label>-linux.tar.gz`.
[`STEAMDECK.md`](STEAMDECK.md) has its contents and how to run it.

Both scripts are quiet and can be re-run; their full output goes to
`build-host/pkg-<label>.log` and `build-host/pkg-linux-<label>.log`. Neither
runs the game.

## Continuous integration

`.github/workflows/ci.yml` builds `linux-x64` headless and window,
`linux-x64-clang` and `win-x64` on every push and pull request, runs `ctest`
on the Linux builds with lavapipe as the Vulkan device, and has no disc
image. docs/BUILDING.md lists its steps.
