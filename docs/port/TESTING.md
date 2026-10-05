# Test checkpoints

The game never runs in the container (plan, "Where the game runs"). Agents
compile and run unit tests only. At each **[user test]** checkpoint the
orchestrator hands the user a Windows package. The user runs it and sends back
logs, and the next packages read them.

The 32-bit Windows build (`win-x86-ref`, with its Linux twin `ref-m32`) was
the oracle for the 64-bit build until Phase 2 exit. It was retired at commit
36a1d73e, when the x64 traces were byte-identical to it over 3000 ticks, and
packages now carry one `x64/` folder. The checkpoint records below that name
`x86` or the 32-bit presets are history.

## Flow

1. **Build.** Configure `win-x64` with `-DICO_LINK_EXE=ON`
   into `build-host/<pkg>-<preset>`, then build. Check the import table with
   `tools/toolchain/mingw-gcc/usr/bin/x86_64-w64-mingw32-objdump -p`.
   It must list only system DLLs (`KERNEL32`, `msvcrt`, `USER32`,
   `COMDLG32`) and the GUI subsystem; since renderer wave 2 the window build
   also imports `SDL3.dll`, which the build copies beside the exe and the
   package ships (Vulkan's `vulkan-1.dll` is loaded at run time, from the
   driver). The Windows link uses `-mwindows
   -static-libgcc -static` (`CMakeLists.txt`).
2. **Package.** Stage the files under `dist/` (gitignored), one `x64/` folder
   with its exe, its link map (`ico_pc.map`, written beside the
   exe by the link, `port/platform/CMakeLists.txt`), `ico-pc.ini` and
   `pad-script.txt`, plus a `TEST.md`. Zip the result as
   `dist/ico-pc-<phase>-win.zip`. Keep the zip: its exes (RelWithDebInfo,
   with DWARF) and maps turn crash offsets into source lines
   (`docs/port/BOOT_DIAG.md`). Build from a clean worktree of the commit
   being tested, not from a tree with other packages' uncommitted work.
3. **User run.** The user double-clicks the exe. No command-line flags, no
   console. Settings come from `ico-pc.ini` in the exe's folder and from
   `config.toml` in the per-user folder (below); logs and the trace go to the
   exe's folder (`port/platform/host_config.c`, `main_host.c`):
   - **Game data:** `ico.o2r` in the per-user folder (Windows
     `%APPDATA%\ico-pc\ico-pc\`) or beside the exe. The first run has
     none: it finds the ISO (below), verifies it and extracts it there once
     (about 870 MB; progress in the log and in a small window; 14 s on the
     container, longer on a slow disk), then boots. Later runs mount the
     archive and never open the ISO. Deleting `ico.o2r` makes the next run
     extract again. `use_iso=1` reads the ISO directly instead
     (docs/port/DATA.md, "Backend 2: the archive").
   - **ISO:** `Ico_PAL.iso` beside the exe, else `iso=` in `ico-pc.ini`,
     else a file dialog. A dialog choice is saved to the ini once it
     verifies.
   - **ISO check:** the extractor accepts the image by its SHA-1
     `1017b53f...`, or by `SCES_507.60`'s SHA-1 plus DATA.DF's manifest (a
     re-dump), and logs which; a wrong image stops with a message box. With
     `use_iso=1` the SHA-1 check is the old one, skipped with `verify=0`.
   - **Pad script:** `pad_script=FILE` in the ini (format in
     `port/input/pad_script.h`); the headless build also takes
     `pad-script.txt` beside the exe if present. The window build ignores a
     `pad-script.txt` the ini does not name (package F2).
   - **Logs:** stdout and stderr go to `logs/ico-pc.log`, unbuffered; the
     diagnostics (`port/platform/diag_host.h`) write there too. The trace
     goes to `logs/trace-<yyyymmdd-hhmmss>.txt`, flushed line by line: by
     default in the headless build (`trace=0` turns it off); the window
     build writes it only with `trace=1` or `trace=PATH` (package F2).
   - **Exit:** after `ticks=` Main ticks; with no `ticks=`, it runs until
     closed.
   - **Watchdog:** `watchdog=S` (default 30, 0 off): no Main tick S
     seconds after boot started, or none for 2S seconds after the first,
     stops the run with a report.
   - **Fatal errors:** a message box naming the log. A crash or the
     watchdog writes a report first (exit codes 3 and 4).
4. **Return.** The user sends back the `logs/` folders. Read the end of
   `ico-pc.log` (below), then the trace for the stage, the game flags and
   the save-buffer hash per tick.

## Keys: `ico-pc.ini` and `config.toml`

Precedence: `ico-pc.ini` beside the exe > `config.toml` > defaults
(docs/port/CONFIG.md has the whole table). `config.toml` is in the folder
`SDL_GetPrefPath("ico-pc", "ico-pc")` names in the window build (Windows
`%APPDATA%\ico-pc\ico-pc\`), the exe's folder in the headless build. Each ini
key has a `config.toml` name; a true/false toml value reads as 1/0.

| ini key | `config.toml` | meaning |
| --- | --- | --- |
| `iso=PATH` | `[paths] iso` | disc image (a dialog choice is saved to the ini) |
| `saves=PATH` | `[paths] saves` | memory card folder |
| `ticks=N` | `[dev] ticks` | exit after N Main ticks |
| `watchdog=S` | `[dev] watchdog` | default 30, 0 off |
| `trace=0` / `trace=1` / `trace=PATH` | `[dev] trace` | no trace / `logs/trace-<time>.txt` / trace there; a path also fixes the clock. Default: on in the headless build, off in the window build |
| `verify=0` | `[dev] verify` | skip the SHA-1 check (`use_iso=1` only; extraction always verifies) |
| `use_iso=0/1` | `[dev] use_iso` | 1: mount the ISO directly (dev mode); 0: the extracted `ico.o2r`, extracting it on the first run. Default 1 in the headless build (trace runs and tests read the ISO as before), 0 in the window build |
| `pad_script=PATH` | `[dev] pad_script` | the scripted pad |
| `dump_every=N`, `dump_dir=PATH` | `[dev] dump_every`, `dump_dir` | rd frame dumps (window build) |
| `audio=0` | `[audio] enabled` | no audio device |
| `audio_dump=PATH` | `[dev] audio_dump` | WAV of the mixed audio (`1`: `logs/audio.wav`) |
| `headless=1` | `[dev] headless` | a run for traces and tests: fixes the clock (the headless build always is) |
| `fixed_clock=0/1` | `[dev] fixed_clock` | the disc clock (`sceCdReadClock`): 2002-01-01 00:00:00 when fixed, the host's local time when not. Default: fixed for the headless build, `headless=1`, or a `trace=` path; real otherwise. A fixed clock keeps the save serial (the clock is in it) and so the trace's save hash reproducible |
| | `[game] language` | `auto`, `en`, `fr`, `de`, `it`, `es`: the boot language sign's preselected item (`auto`: the system locale) |

A trace run that must be reproducible sets `headless=1` or a `trace=` path (or
runs the headless build); the Windows user package leaves them out, so the
clock it reports is real.

## Reading `ico-pc.log`

Every failure mode leaves a distinct ending (`docs/port/BOOT_DIAG.md`,
"What the log contains now"):

| the log ends with | meaning |
| --- | --- |
| `exit: ticks= reached` and `N Main ticks, V vsyncs, stage_no S` | the run completed |
| `CRASH: <exception or signal> at ico_pc.exe+0x…`, the fault address, the game thread and its last kernel call, stack candidates, `last failure message` (assertions), a heartbeat, every thread, `the run ended`, the summary | a crash (exit code 3); look the offsets up in the map or with `addr2line` |
| `WATCHDOG: no Main tick …` or `no new Main tick …`, `the main thread was at ico_pc.exe+0x…`, stack candidates, every thread with what it waits on | a hang (exit code 4): a spin shows its address; a deadlock shows every thread waiting |
| heartbeat lines ending `no progress for N s` | the run stalled; the next lines are the watchdog's |
| `the game called Exit(n)` or `host: the game's main returned` | the game ended itself |
| none of these after the last milestone | the process was killed from outside (Task Manager) or Windows ended it before the handlers ran; the last milestone or heartbeat locates it |

The boot milestones (`boot: file_Init`, thread creations,
`first vsync done`, `Main: …`, `first Main tick done`, `stage_no a -> b`,
`kanbanBoot: …`) and the heartbeat every 2 s show how far the run got.

Developer overrides exist for local runs (`--iso`, `--pad-script`,
`--trace`, `--ticks`, `--no-verify`, `--console`, `--help`). The user never
needs them.

## Trace format (Phase 1; package 2A replaces it)

The trace has one line per Main tick (`port/platform/trace_host.c`),
flushed as it is written, and starts with this header line, written when the
file opens:
`# tick vsync stage sys0 sys1 gameover gf0..gf12 save`

| column | contents |
| --- | --- |
| `tick` | the Main tick number |
| `vsync` | the vsync count |
| `stage` | `stage_no` |
| `sys0`, `sys1` | `systemStatus[0..1]` |
| `gameover` | `gameover_flag` |
| `gf0`..`gf12` | the game flags as 32-bit words (flag 32N+b is bit b of gfN; New Game sets flag 382, which is `gf11` bit 30) |
| `save` | the FNV-1a 32 hash of `gameSysMainSaveBuff` |

A Main tick is one pass of Main's loop. `common/src/main.c` calls
`ico_host_main_tick()` after `frameReady = 1`.

## Phase 1 checkpoint (package 1D)

- **Package:** `dist/ico-pc-phase1-win.zip`. It holds `x86/` and `x64/`
  (each with the exe, `ico-pc.ini` with `ticks=3000`, and `pad-script.txt`
  = `port/input/pad-boot.txt`) and `TEST.md`.
- **Pass:** `win-x86-ref` boots to the title and ticks a stage with null
  devices, writing a trace (the plan's Phase 1 exit). In the trace, look for
  flag 382 and a stage change after it.
- **`win-x64`** is expected to be unreliable until Phase 2 (32-bit struct
  and pointer-in-int assumptions). Its log only shows how far it gets.
- **Boot script timing** is derived from the code and has never run. If the
  trace shows a stall, find the stage and tick, adjust
  `port/input/pad-boot.txt`, and once the tick of flag 382 is known, cut the
  script after it.

## Phase 1b checkpoint (package 1E)

- **Package:** `dist/ico-pc-phase1b-win.zip`, root folder
  `ico-pc-phase1b/`, with `TEST.md` and `x86/`, `x64/` (each: the exe, its
  `.map`, `ico-pc.ini` with `iso=`, `ticks=3000` and `watchdog=30`, and
  `pad-script.txt` = `port/input/pad-boot.txt`). Built from a clean
  worktree of `93d918e4` plus package 1E's changes.
- **Why:** the Phase 1 package died ~2 s into boot on both architectures,
  with an empty log tail. `docs/port/BOOT_DIAG.md` has the causes (i386
  argument shifts in `gif_*` externs; blocking disc reads that took no
  simulated time; on x64, a truncated pointer in `iosCdStRead`) and the
  fixes.
- **Expected:** `x86` completes 3000 ticks. On `ref-m32` it sets flag 382 at
  tick 661 and reaches stage 3 at tick 1036; Windows should match tick for
  tick if the build is deterministic across the two, which this run tests.
  `x64` is still expected to crash in the first stage load (on
  `linux-x64`: `seki/src/Light.c:131`, a pointer held in an `int`, Phase 2;
  where Windows puts its heap decides whether that truncates); its log
  should now end in a crash report naming the location.

## Renderer wave 2 checkpoint (package R2a)

- **Package:** `dist/ico-pc-wave2-win.zip`, root folder `ico-pc-wave2/`,
  with `TEST.md` and `x86/`, `x64/` (each: the window exe, its `.map`,
  `SDL3.dll`, `ico-pc.ini` with `iso=` and `watchdog=30` and no `ticks=`,
  and `pad-script.txt` = `port/input/pad-boot.txt`). Built from a clean
  worktree of `43bb792c` plus package R2a's changes (presets `win-x86-ref`
  and `win-x64`, which build the window since R2a; `-DICO_LINK_EXE=ON`).
- **What it is:** the first window. The seki 2D layer draws through `rd`
  (`docs/port/RENDER_API.md` section 9) in real time at 50 Hz with vsync;
  game textures are placeholders (a checker per texture) until the texture
  package; the 3D world is not drawn (wave 3).
- **Expected:** the boot signs and the title as groups of checker
  rectangles that fade in and out, in a 4:3 box; `exit: the window was
  closed` at the end of the log. `gif:` lines list the GS registers the
  decoder does not handle (expected so far: none on the boot signs and the
  title; the texture uploads' BITBLTBUF/TRX* do not pass through the
  decoder). The script's New Game set flag 382 at tick 661 on the headless
  `ref-m32` run (Phase 1b above), so the title is up for a few seconds
  before it.
- **Container checks for this package:** `rd_layout` (the layout frame
  through the host `GifPacket.c`, recording and pixels), `rd_pixel` (adds
  DATE and flat shading), headless `linux-x64` and `ref-m32` ctest.

## Phase 4F run (config, language, clock)

Headless `linux-x64` (`-DICO_LINK_EXE=ON`), `ticks=1300`, `pad_script` =
`port/input/pad-boot.txt`, `verify=0` in the build folder's `ico-pc.ini`, and
`[game] language = "fr"` in a `config.toml` beside the exe (2026-10-05):

- the log shows `scf: language 2 from [game] language = "fr"` at
  `kanbanBoot` step 101 (tick 118), the first `sceScfGetLanguage` of the boot;
- the pad script's Cross confirmed the sign at tick 143 (`mcCheckStep` 102 ->
  190). `NonLinearCameraMove` (read from the running process through
  `/proc/<pid>/mem` at the symbol's address) was 3 from the first read to the
  end of the run. 3 is also the value `Main` sets at start
  (`main.c:139`), so the evidence that French was selected is that English,
  the cursor's position without the config, would have changed it to 2 at
  tick 143, and it did not change;
- the run reached stage 3 at tick 996 (`stage_no 40 -> 3`), 1300 Main ticks,
  2607 vsyncs, exit `ticks= reached`, as in the 4B run.

## Phase 5A run (the archive)

Headless `linux-x64` (`-DICO_LINK_EXE=ON`, build folder
`build-host/5a-linux-x64`), `ticks=1300`, `pad_script` =
`port/input/pad-boot.txt`, `watchdog=60`, `verify=0`, `iso=` the PAL image in
`ico-pc.ini`, and `[dev] use_iso = false` in a `config.toml` beside the exe,
no `ico.o2r` present (2026-10-05):

- the first run extracted: `accepted by iso-sha1`, 13 files, 872,906,360
  bytes, `ico.o2r` 872,910,571 bytes, 14.2 s (image SHA-1 6.1 s, copy
  8.2 s), progress logged in tenths;
- it mounted the archive (`game data .../ico.o2r (SCES-50760, 13 files ...)`),
  loaded the 75 table rows and the SNDN2DRV pitch table from it, reached
  stage 3 at tick 996 and ended at 1300 Main ticks, 2607 vsyncs, `ticks=
  reached`;
- the trace is byte-identical to package 5B's (`build-host/5b-linux-x64/
  logs/trace-*.txt`, the ISO; SHA-1 `36fc27275414fe291ca0a892439de7d27773fa64`
  for both), and the `stage_no` milestones fall on the same vsyncs and ticks.

The window build has not run a first-run extraction yet (the game never
runs in the container): the Windows checkpoint should check the progress
window, the time on the user's disk and the `ico.o2r` in `%APPDATA%`.

## Renderer wave 6: D3D12 (package R6c)

The D3D12 backend (`port/rhi/d3d12/README.md`) is built by `win-x64` and
`win-x64-clang` (`ICO_RHI_D3D12`, default ON for 64-bit Windows) next to the
Vulkan one. Nothing of it has run yet: the container has no Windows and no
Wine. Three checks, in order.

**1. `rhi_d3d12_test.exe` (double-click).** From
`build-host/<pkg>-win-x64/port/rhi/`: `rhi_d3d12_test.exe` and `SDL3.dll`,
copied together into any folder. Double-clicked it shows no console, takes
no flags, writes `rhi_d3d12_test.log` beside itself and ends with a message
box:

- *1. D3D12 on WARP*: the exact-texel cells of
  `port/rhi/test/rhi_test_common.c` (dual-source blend, stencil wrap,
  reversed-Z depth with exact readback, colour masks, an RGBA8_UINT target
  with an integer clear, texture upload and sampling, R8 copies, a copied
  vertex buffer; three frames) on Windows' software rasteriser. This is the
  reference: it does not depend on the GPU driver.
- *2. D3D12 on hardware*: the same cells on the default (discrete first)
  adapter; "skipped" when the machine has none.
- *3. swapchain*: a hidden window, six frames of clear, readback (BGRA) and
  present, with a resize.
- *4. Vulkan (comparison)*: the same cells on Vulkan; "skipped" without a
  Vulkan driver, which is not a failure.

Expected: PASSED, every line "pass" (or "skipped" for 2 or 4). The D3D12
debug layer is used when installed (Settings > System > Optional features >
add "Graphics Tools"); then every debug-layer error fails the run and is in
the log. Send back `rhi_d3d12_test.log` either way: it names the adapters,
whether the debug layer was on, and each mismatch with the texel's value.
Exit code 0 pass, 1 fail, 77 nothing ran. `--console` (developer switch)
prints to the console instead and shows no message box.

**2. The window build on D3D12.** In `config.toml` (per-user folder):

    [video]
    backend = "d3d12"

or `backend=d3d12` in `ico-pc.ini` beside the exe (the ini wins). Default
and fallback: `vulkan`. `logs/ico-pc.log` then reads `window: ... D3D12 on
<adapter>` and lists the adapters (`rhi_d3d12: adapter 0: ...`).

**3. Vulkan vs D3D12 on the same frame dumps (plan: within 1 LSB).**

1. Run the game on either backend with `dump_every=N` (and optionally
   `dump_dir=`) in `ico-pc.ini`: every Nth frame goes to
   `dumps\rd-NNNNN.rddump` beside the exe (local only: dumps hold disc
   assets, never share or commit one).
2. Beside the `dumps\` folder put `rd_replay_tool.exe` and `SDL3.dll`
   (`build-host/<pkg>-win-x64/port/render/`), and `compare_backends.cmd` and
   `compare_png.ps1` (`port/rhi/test/`). Double-click
   `compare_backends.cmd`. For every dump it renders DISPLAY and the
   Original presenter's 640x480 output on both backends into
   `compare_out\` and writes `compare_backends.log`, opened in Notepad at
   the end: per pair "max difference N LSB: within 1 LSB", or the count of
   texels over 1 LSB and the first one's position.
3. By hand, for one dump:

        rd_replay_tool.exe dumps\rd-00100.rddump vk.png --backend vulkan
        rd_replay_tool.exe dumps\rd-00100.rddump dx.png --backend d3d12
        powershell -NoProfile -ExecutionPolicy Bypass -File compare_png.ps1 vk.png dx.png

   (`--target NAME` picks another target, `--present WxH` the presenter.)

Developer path, also by hand: every `rd_*` GPU test runs on D3D12 with
`set ICO_RHI_BACKEND=d3d12` (and `set ICO_D3D12_ADAPTER=warp` for the
software rasteriser) before starting the test exe from `cmd`; they count
Vulkan validation errors only, so read the D3D12 debug lines in their
output.

**Container checks for this package:** `rhi_d3d12_plan` (CPU: the barrier
plan, buffer tracking, copy ordering, descriptor rings, layouts, the DXBC
reader, every game vertex shader's DXIL inputs against its SPIR-V
locations), `rhi_vk` through the backend dispatcher, the full `linux-x64`
ctest; `win-x64` (mingw-w64 GCC 14, `-DICO_LINK_EXE=ON`) and
`win-x64-clang` builds of `ico_rhi_d3d12`, `rhi_d3d12_test.exe`,
`rd_replay_tool.exe` and `ico_pc.exe`, warning-free.

## Repeatable packaging: `tools/package_win.sh <label>`

One command builds and zips the Windows test package for the current HEAD
(`tools/package_win.sh wave2b`). It:

1. Makes a clean detached worktree of HEAD at `build-host/pkg-wt` (so other
   packages' uncommitted work is not built) and symlinks `.venv` and
   `tools/toolchain` into it. `ICO_PKG_FILES="path ..."` copies those
   working-tree files over HEAD first, to try a change before it is
   committed.
2. Sets `TMPDIR=build-host/tmp` (the system `/tmp` is nearly full), then
   configures and builds `win-x64` with
   `-DICO_LINK_EXE=ON` (no `baserom` is needed: the binary holds no disc
   data). The
   window build is the preset default. Any failure stops the script with
   the log tail and a non-zero exit.
3. Stages `dist/stage/x64/` with `ico_pc_x64.exe`,
   `ico_pc_x64.map`, `SDL3.dll`, `ico-pc.ini` (`watchdog=30`, no
   `ticks=`; an `iso=` line already in the staged ini is kept) and
   `pad-script.txt` (`port/input/pad-boot.txt`), plus `TEST.md` with the
   label, date and commit in its heading. `x64/tools/` holds the R6c
   backend checks: `rhi_d3d12_test.exe`, `rd_replay_tool.exe`, `SDL3.dll`,
   `compare_backends.cmd` (staged with CRLF line ends) and `compare_png.ps1`;
   `TEST.md` has a section on them.
4. Writes `dist/ico-pc-<label>-win.zip` (root `ico-pc-<label>/`, no
   `logs/` folders), prints the zip path and the commit, and removes the
   worktree (`git worktree remove --force`, `git worktree prune`).

The script is quiet and can be re-run; the full output goes to
`build-host/pkg-<label>.log`. The `TEST.md` text lives in the script. It
does not run the game.

## Linux package: `tools/package_linux.sh <label>`

`tools/package_linux.sh <label>` does the same for Linux: a clean worktree of
HEAD at `build-host/pkg-linux-wt`, preset `linux-x64` with
`-DICO_HEADLESS=OFF -DICO_LINK_EXE=ON` (target `ico_pc` only), then it checks
the binary's dynamic dependencies (`libSDL3.so.0`, libc, libm; run path
`$ORIGIN`), stages `dist/stage/linux/` (`ico_pc`, `libSDL3.so.0`,
`ico-pc.ini`, `ico_pc.map`, `README.txt`, `LICENSE`, `THIRD_PARTY.md`; an
`iso=` line already in the staged ini is kept) and writes
`dist/ico-pc-<label>-linux.tar.gz` (root `ico-pc-<label>/`, owner root).
The log is `build-host/pkg-linux-<label>.log`; it never runs the game.
`ICO_PKG_FILES` works as for the Windows script. docs/port/STEAMDECK.md says
what is in the package and how to run it.

## Continuous integration

`.github/workflows/ci.yml` (docs/BUILDING.md, "Continuous integration")
builds `linux-x64` headless and window, `linux-x64-clang` and `win-x64` and
runs `ctest` on the Linux ones, with no disc image. Tests that need the disc
or a Vulkan device exit 77 and are reported skipped, which passes.
