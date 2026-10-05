# Test checkpoints

The game never runs in the container (plan, "Where the game runs"). Agents
compile and run unit tests only. At each **[user test]** checkpoint the
orchestrator hands the user a Windows package. The user runs it and sends back
logs, and the next packages read them.

## Flow

1. **Build.** Configure `win-x86-ref` and `win-x64` with `-DICO_LINK_EXE=ON`
   into `build-host/<pkg>-<preset>`, then build. Check the import table with
   `tools/toolchain/mingw-gcc/usr/bin/{i686,x86_64}-w64-mingw32-objdump -p`.
   It must list only system DLLs (`KERNEL32`, `msvcrt`, `USER32`,
   `COMDLG32`) and the GUI subsystem. The Windows link uses `-mwindows
   -static-libgcc -static` (`CMakeLists.txt`).
2. **Package.** Stage the files under `dist/` (gitignored), one folder per
   architecture with its exe, its link map (`ico_pc.map`, written beside the
   exe by the link, `port/platform/CMakeLists.txt`), `ico-pc.ini` and
   `pad-script.txt`, plus a `TEST.md`. Zip the result as
   `dist/ico-pc-<phase>-win.zip`. Keep the zip: its exes (RelWithDebInfo,
   with DWARF) and maps turn crash offsets into source lines
   (`docs/port/BOOT_DIAG.md`). Build from a clean worktree of the commit
   being tested, not from a tree with other packages' uncommitted work.
3. **User run.** The user double-clicks the exe. No command-line flags, no
   console. Everything comes from the exe's folder (`port/platform/
   host_config.c`, `main_host.c`):
   - **ISO:** `Ico_PAL.iso` beside the exe, else `iso=` in `ico-pc.ini`,
     else a file dialog. A dialog choice is saved to the ini once it
     verifies.
   - **ISO check:** SHA-1 `1017b53f...` (docs/port/DATA.md), skipped with
     `verify=0`.
   - **Pad script:** `pad-script.txt`, used if present (format in
     `port/input/pad_script.h`).
   - **Logs:** stdout and stderr go to `logs/ico-pc.log`, unbuffered; the
     diagnostics (`port/platform/diag_host.h`) write there too. The trace
     goes to `logs/trace-<yyyymmdd-hhmmss>.txt` (`trace=0` turns it off),
     flushed line by line.
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
