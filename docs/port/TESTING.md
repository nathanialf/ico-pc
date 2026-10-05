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
   architecture with its exe, `ico-pc.ini` and `pad-script.txt`, plus a
   `TEST.md`. Zip the result as `dist/ico-pc-<phase>-win.zip`.
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
   - **Logs:** stdout and stderr go to `logs/ico-pc.log`. The trace goes to
     `logs/trace-<yyyymmdd-hhmmss>.txt` (`trace=0` turns it off).
   - **Exit:** after `ticks=` Main ticks; with no `ticks=`, it runs until
     closed.
   - **Fatal errors:** a message box naming the log.
4. **Return.** The user sends back the `logs/` folders. Read the exit
   summary at the end of `ico-pc.log` (Main ticks, vsyncs, last stage). Read
   the trace for the stage, the game flags and the save-buffer hash per tick.

Developer overrides exist for local runs (`--iso`, `--pad-script`,
`--trace`, `--ticks`, `--no-verify`, `--console`, `--help`). The user never
needs them.

## Trace format (Phase 1; package 2A replaces it)

The trace has one line per Main tick (`port/platform/trace_host.c`) and
starts with this header line:
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
