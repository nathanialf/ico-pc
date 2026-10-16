# Platform layer

The host side of the [EE](ARCHITECTURE.md#terms): the program's entry point on each operating
system, the game's threads as [fibers](ARCHITECTURE.md#terms) under the EE kernel's scheduling
rules, the [vsync](ARCHITECTURE.md#terms)-driven host loop, the [arena](ARCHITECTURE.md#terms) that
stands in for EE RAM, the folders the port reads and writes, and the diagnostics that make a failed
run explain itself. [ARCHITECTURE.md](ARCHITECTURE.md) places this layer among the others.

## Files

| path | what it holds |
| --- | --- |
| `port/platform/main_host.c` | the program's `main` (desktop) or `ico_host_main` (Android, iOS): log, ini, game data, tables, window, the vsync loop |
| `port/platform/host_loop.c`, `host_loop.h` | `ico_host_init`, `ico_host_step`, the step profile |
| `port/platform/vsync_hooks.c` | the on-vsync callback list |
| `port/platform/fiber.c`, `fiber.h` | the context switch over minicoro (`port/third_party/minicoro`), guarded stacks |
| `port/platform/sched.c`, `sched.h` | the EE thread and semaphore model; `ico_sched_call_on_host` |
| `port/platform/kernel_host.c`, `kernel_host.h` | the EE kernel calls of `port/compat/eekernel.h`; the INTC table |
| `port/platform/arena.c`, `arena.h` | the EE RAM arena and the heap statistics |
| `port/platform/hwregs.c` | the EE I/O and GS privileged register pages as plain memory |
| `port/platform/fpenv.c`, `fpenv.h` | the simulation's FP mode and the host's |
| `port/platform/clock.c`, `clock.h` | the wall clock behind `sceCdReadClock`; the EE timers T0 to T3 |
| `port/platform/host_config.c`, `host_config.h` | the executable's folder, the user folder, portable mode, `ico-pc.ini`, the TOML reader and writer, SHA-1, message boxes, the log redirect |
| `port/platform/host_fs.c`, `host_fs.h` | file calls on UTF-8 paths (wide calls on Windows) |
| `port/platform/diag_host.c`, `diag_host.h` | crash handlers, heartbeat, watchdog, milestones, float faults, the summary line, `ico_log_limited` |
| `port/platform/symmap.c`, `symmap.h` | the crash and watchdog reports' symbolizer: a GNU ld link map or an nm table (plain or gzip), no allocation |
| `port/platform/report.c`, `report.h` | the report file of an abnormal end, `logs/report-<date>-<time>.zip`, no allocation |
| `port/platform/trace_host.c`, `trace_host.h` | the Main tick count, the heartbeat's state line, the trace file |
| `port/platform/assert_host.c` | newlib's `__assert` as the game calls it |
| `port/platform/window_host.c`, `window_host.h` | the SDL3 window, the renderer device, the real-time pacing, the stats lines (window build) |
| `port/platform/window_video.c`, `window_video.h` | window mode requests and their readback |
| `port/platform/window_lifecycle.c`, `window_lifecycle.h` | the steps run when a mobile system moves the app |
| `port/platform/pump_watch.c`, `pump_watch.h` | the event pump's recent window messages and SDL events, the controllers that came and went (name, GUID, SDL driver), SDL's own log ring, and the names of the messages (pure C, tested everywhere) |
| `port/platform/window_pump_win.c`, `window_pump_win.h` | Windows: the window's messages recorded for the slow-pump lines, the display and the computer kept awake |
| `port/platform/pace_policy.c`, `pace_policy.h` | the pacing decisions, free of SDL: the vsync lock, slow presents, the gap between presents |
| `port/platform/display_clock.h`, `display_clock_win.c`, `display_clock_none.c`, `perf_block.c`, `perf_block.h` | the vsync lock's vblank clock and the replay blocks ([DISPLAY.md](DISPLAY.md#files)) |
| `port/platform/startup_retry.c`, `startup_retry.h` | the steps the relaunch takes after a graphics driver crash or a lost graphics device |
| `port/platform/hotkeys.c`, `hotkeys.h` | F11 and F12 |
| `port/platform/dump_zip.c`, `dump_zip.h` | F12's zip and the aspect sweep's zip |
| `port/platform/steam_shortcut.c`, `steam_shortcut.h` | Settings > Extras > Add to Steam (Windows and Linux; not offered on macOS) |
| `port/platform/gpu_driver.c`, `gpu_driver.h` | Android graphics driver packages (pure C, tested everywhere) |
| `port/platform/android/` | `main_android.c`, `host_android.c`, `android_paths.c`, `iso_import.c`, `gpu_driver_android.c` |
| `port/platform/ios/main_ios.c`, `ios/Info.plist.in` | the iOS entry point and bundle |
| `port/platform/macos/Info.plist.in` | the macOS bundle |
| `port/platform/win/` | the Windows manifest and resource script (`ico_pc.manifest`, `ico_pc.rc`) |
| `port/platform/ico_version.h.in`, `build_commit.cmake` | the package label and the commit, generated into `<build>/generated/` for the log header, the Options menu and the Windows version information |
| `port/platform/test/` | the platform unit tests |
| `port/platform/fpenv_test.c` | the FP-mode test (`fpenv`, `fpenv_trap`) |
| `port/compat/eekernel.h`, `port/compat/eeregs.h` | the game-side declarations of the kernel calls and register names |
| `port/null/gfx_null.c`, `libgcc_null.c` | the graphics floor (libgraph, libdma) and `fptodp` |
| `ico2/fumi/ios/ios.c` | the game's partitions, with the [host extras](ARCHITECTURE.md#terms) |

## Entry points

On Windows, Linux and macOS the program's `main` is in `port/platform/main_host.c` and calls
`host_main`. It attaches a console when started from one, parses the developer options
(`--iso`, `--pad-script`, `--trace`, `--ticks`, `--no-verify`, `--console`), opens the log, reads
`ico-pc.ini`, writes a first `config.toml`, mounts the game data, loads the data tables, opens
the window, starts the watchdog, calls `ico_host_init` and then loops on `ico_host_step`
(`port/platform/main_host.c` (`host_main`, `parse_args`)). An error that stops the run is logged,
shown in a message box on Windows, and exits 1; a bad option exits 2 (`port/platform/main_host.c`
(header comment); `port/platform/host_config.h` (`ico_host_fatal`)). On macOS the program is an app
bundle (`CMakeLists.txt` (`MACOSX_BUNDLE`); `port/platform/macos/Info.plist.in`).

On Android the program is `libmain.so`, which SDL's Java side loads. `SDL_main`
(`port/platform/android/main_android.c`) sets the hints and runs `ico_host_main` on an SDL thread
with a 32 MB stack (`ICO_MAIN_STACK_BYTES`), because the [replay](ARCHITECTURE.md#terms), the
[present](ARCHITECTURE.md#terms) and the movie decoder run on the host stack. `ico_host_main` runs
the end-of-run steps itself (`ico_host_shutdown`) before returning, since returning finishes the
activity (`port/platform/main_host.c` (`ico_host_main`)). The window opens before the game data is
mounted, because SDL has one window there and the first start's picker, copy and extraction progress
are drawn in it (`port/platform/main_host.c` (`host_main`, `ICO_ANDROID_UI`);
`port/platform/android/iso_import.h`). The log is mirrored to logcat and a fatal error shows SDL's
message box (`port/platform/android/host_android.h`).

On iOS, `main` (`port/platform/ios/main_ios.c`) keeps UIKit and the renderer on the main thread:
it changes to the Documents folder and runs `ico_host_main` on a fiber with a guarded 32 MB stack.
It also sets `ICO_RD_BLEND=fixed`, the fixed-function blend path, because storage interlock
costs rendering and speed on MoltenVK there (`port/platform/ios/main_ios.c` (`main`)).

## Fibers

Each [fiber](ARCHITECTURE.md#terms) is an asymmetric coroutine over minicoro: `ico_fiber_resume`
runs it on its own stack until it calls `ico_fiber_yield` (`port/platform/fiber.h`). The scheduler
is the only resumer, so every switch between two game threads goes fiber, scheduler, fiber. Stacks
are 256 KB (`ICO_FIBER_STACK_SIZE`). With minicoro's assembly backend (x86-64 Linux and Windows,
arm64) the stack has a no-access guard page below it, so an overflow faults instead of corrupting
the fiber's control block (`port/platform/fiber.c` (the stack allocation section);
`ico_fiber_has_guard_page`).

The game's own thread stacks (`mainThreadStack`, the stacks `iosThreadCreate` is handed) are
still allocated and stamped with `<THREAD_SP>` marks by the game, so its memory use is the PS2's
(`ico2/fumi/ios/thread.c`); the scheduler records them, and every fiber runs on a host stack of
`ICO_FIBER_STACK_SIZE` (`port/platform/sched.c` (`ico_sched_create_thread`,
`ico_sched_start_thread`)).

A thread's fiber lives from `StartThread` to the thread's exit. `CreateThread` records a dormant
thread (`ico_sched_create_thread`); `StartThread` creates its fiber and puts it on the tail of its
ready queue (`ico_sched_start_thread`); `ExitThread` and `ExitDeleteThread` mark the thread to be
reaped and yield, and `ico_sched_run` destroys the fiber once it has yielded, freeing the id too
for `ExitDeleteThread` (`port/platform/sched.c` (`exit_current`, `ico_sched_run`)). Thread ids and
semaphore ids each run from 1 to 255 (`port/platform/sched.h` (`ICO_SCHED_MAX_THREADS`,
`ICO_SCHED_MAX_SEMAS`)).

## Scheduler

`port/platform/sched.h` states the rules, which follow Play!'s EE kernel HLE (read for its
semantics only) and the game's use:

- strict priority from 0 (highest) to 127, no time slice;
- one FIFO ready queue per priority; the running thread stays at the head of its queue, so a
  thread preempted by a higher priority resumes before its peers;
- a thread made ready (`StartThread`, `WakeupThread`, `SignalSema`, `ResumeThread`) goes to the
  tail of its queue and preempts the caller at once when its priority is higher;
- `ChangeThreadPriority` and `RotateThreadReadyQueue` move a ready thread to the tail of its
  queue;
- `SleepThread`/`WakeupThread` keep a wakeup count, so a wakeup before the sleep is remembered;
- semaphores keep their waiters in FIFO order.

Every switch goes through the dispatcher, `ico_sched_run`: a thread that must give up the CPU yields
to it, and it resumes the highest-priority ready thread until none is ready or the highest one
busy-waits for the next vsync (`port/platform/sched.c` (`ico_sched_run`, `pick`)). Code runs either
on a fiber (a game thread) or on the [host context](ARCHITECTURE.md#terms) (the dispatcher's caller,
the vsync callbacks). A call that would block is a fatal error on the host context; a call that
wakes a thread from the host context never switches, and the next `ico_sched_run` picks the thread
up, as the EE does on return from an interrupt (`port/platform/sched.h` (Contexts)).

A wait (`WaitSema` on a zero count, `SleepThread` with no wakeup counted) takes the thread off its
ready queue, marks it waiting and yields its fiber to the dispatcher. A call from a fiber that
readies a higher-priority thread yields the caller the same way, so the dispatcher's next pick is
the readied thread (`port/platform/sched.c` (`ico_sched_wait_sema`, `ico_sched_sleep`,
`reschedule`)).

The EE's busy waits (`VSync`, `VSync2`, libgraph's `sceGsSyncV`, the game's idle loop) are
`ico_sched_spin_vsync`: the thread stays ready at the head of its queue, lower priorities do not
run, higher ones woken by an interrupt do, and `ico_sched_run` returns to the host once that
thread is the highest ready one, until the next vsync (`port/platform/sched.h` (Busy waits);
`port/platform/kernel_host.c` (`VSync`); `port/null/gfx_null.c` (`sceGsSyncV`);
`ico2/common/src/main.c` (`idle`)).

Host work too deep for a 256 KB fiber stack (the GPU driver's replay, pipeline creation, present)
goes through `ico_sched_call_on_host`: from a fiber the thread yields, the dispatcher runs the
function on the host stack in the host FP mode and resumes the same thread at once, with no other
thread running in between (`port/platform/sched.c` (`ico_sched_call_on_host`)).
[rd](ARCHITECTURE.md#terms) reaches it through `rd_set_host_call` (`port/platform/window_host.c`
(`ico_window_open`)).

## Kernel calls

`port/platform/kernel_host.c` defines the EE kernel calls `port/compat/eekernel.h` declares, with
the signatures of `sce/libkernl/eekernel.h` and the 64-bit [GS](ARCHITECTURE.md#terms) registers as
`unsigned long long`. Threads and semaphores map onto the scheduler (`CreateThread` to
`ico_sched_create_thread`, `WaitSema` to `ico_sched_wait_sema`, ...); each call notes itself for the
diagnostics (`ico_sched_note`). Interrupts are a table: `AddIntcHandler` and `AddDmacHandler` build
handler lists per [INTC](ARCHITECTURE.md#terms) cause and [DMA](ARCHITECTURE.md#terms) channel,
`EnableIntc` and `DisableIntc` set masks, and `ico_kernel_raise_intc` runs an enabled cause's
handlers in order as [interrupt code](ARCHITECTURE.md#terms); for vblank start it first completes a
pending `SetVSyncFlag` (`port/platform/kernel_host.c` (`ico_kernel_raise_intc`, `SetVSyncFlag`)).
`DIntr`/`EIntr` report interrupts enabled, `FlushCache`, `SyncDCache` and `SetGsCrt` do nothing,
`SetAlarm` fails (its one user, the PS2 movie player, is not compiled), `Exit` logs and exits, and
`scePrintf` writes to stdout only when the `ICO_TTY` environment variable is set
(`port/platform/kernel_host.c` (`SetAlarm`, `Exit`, `scePrintf`)).

## Host loop and vsync callbacks

`ico_host_init` puts the FPU in simulation mode, allocates the arena, starts the audio host and
the port's music gallery and credits engines, ties the EE timers to the game's video mode word,
resets the scheduler, installs the fiber start hook, boots the game's `main` as thread 1 at
priority 1 (libkernl's `InitThread` value) and runs it until every thread waits
(`port/platform/host_loop.c` (`ico_host_init`, `BOOT_PRIORITY`)). If `main` ever returns, the
program exits (`boot_main`).

The game then makes its own threads; the priority is the last argument of `iosThreadCreate`, and a
lower number runs first. `main` drops the boot thread to 14 and runs `boot`, which starts `idle`
(27) and `scheduler` (15) and then sleeps. `idle` starts the disc manager (28), the stage manager
(27), the memory card manager (27), the subtitle manager (27), the sound manager (16) and `Main`
(27), lowers itself to 32 and spins on `ico_sched_spin_vsync` (`ico2/common/src/main.c` (`main`,
`boot`, `idle`); `ico2/fumi/ios/thread.c` (`iosThreadCreate`)). `scheduler` waits on the vblank
event: the game's vblank handler wakes an event thread at priority 11, whose message wakes
`scheduler`, which wakes `Main` every `systemStatus[1]` vsyncs, the sound manager every vsync, and
the memory card, disc and stage manager threads when they wait for it (`ico2/fumi/ios/message.c`
(`iosMsgSetEvent`, `signal_handler`); `ico2/common/src/main.c` (`scheduler`, `ico_vsync`)).

`ico_host_step` is one simulated vsync (`port/platform/host_loop.c` (`ico_host_step`)):

1. the simulation's FP mode again (host work between steps may leave anything in MXCSR or FPCR);
2. `ico_sched_vsync_advance`, then `ico_vsync(field)` in the game (`ico2/common/src/main.c`), which
   sets `GS_CSR.FIELD` and raises the vblank-start cause, the field alternating from 0;
3. the on-vsync callbacks, as interrupt code (`ico_host_run_vsync_hooks`);
4. the vsync's audio from the [SPU2](ARCHITECTURE.md#terms) state the last sound tick left
   (`port/audio/audio_host.h` (`ico_audio_host_vsync`));
5. `ico_sched_run`: every thread that became ready runs until all wait again;
6. the achievements' poll (once per new [Main tick](ARCHITECTURE.md#terms), reading game state only)
   and the Extras credits' poll (nothing unless a credits playback runs);
7. the step profile: the real time of each phase, fiber switches, disc reads and sectors, a pending
   memory card command, the stage and the loading flag (`ico_host_step_profile`).

The callbacks live in `port/platform/vsync_hooks.c`, apart from the loop so device code and its
tests link them without the game: at most 16, run in registration order between
`ico_sched_interrupt_begin` and `ico_sched_interrupt_end` (`ico_host_on_vsync_register`). Disc
reads (`port/data/cdvd_host.c` (`ico_cdvd_host_vsync`)), memory card requests
(`port/save/mc_host.c` (`sceMcInit`)) and the EE timers (`port/platform/clock.c`
(`ico_clock_timers_attach`)) complete or advance there, so a request issued during one tick
completes at the next vsync.

The vsync rate is the game's: 50 when `systemStatus[0]` is nonzero (PAL), else 60
(`ico_host_vsync_hz`); the window build paces an NTSC vsync at 16.683 ms ([Pacing](#pacing)). The Main tick is counted by the game's call to `ico_host_main_tick` at the
end of each pass of `Main` (`port/platform/trace_host.c` (`ico_host_main_tick`)). After each step
`host_main` writes the [trace](ARCHITECTURE.md#terms) and pad recording lines, flushes the log, and
in the [window build](ARCHITECTURE.md#terms) switches to the host FP mode, pumps the window and
paces (`port/platform/main_host.c` (`host_main`)).

## Pacing

In the window build `ico_window_pace(hz)` runs after every simulated vsync. Outside the
[vsync lock](ARCHITECTURE.md#terms) it presents until the vsync's deadline in real time (20 ms PAL,
16.683 ms NTSC), as often as the frame rate option and vsync allow, then sleeps to it; more than
100 ms behind, the lag is dropped instead of run off fast (`port/platform/window_host.c` (`pace`,
`RESYNC_NS`, `resync`)). Under the vsync lock (vsync on, and a frame rate cap equal to the
display's refresh and a whole multiple of the game's rate) the step is due on a grid of n display
refreshes and is followed by n presents; more than `PACE_LOCK_DEBT_VSYNCS` (6) periods behind, the
lag is dropped and the grid starts again (`port/platform/window_host.c` (`pace_locked`);
`port/platform/pace_policy.h` (`pace_lock_vsync`, `PACE_LOCK_DEBT_VSYNCS`)). Every decision free
of SDL (the vsync lock and its display clocks, slow presents, the gap between presents, the FIFO
hold, the first present on a new swapchain, the automatic resolution step) is in
`port/platform/pace_policy.c`, tested by ctest `pace_policy` and `pace_loop`; `window_host.c` only
times and calls them. The [headless build](ARCHITECTURE.md#terms) has no pacing and runs vsyncs as
fast as it can. [DISPLAY.md](DISPLAY.md#pacing) describes the paths and the vsync lock.

## Arena, partitions and host extras

`ico_arena_init` allocates one block of `ICO_ARENA_EE_SIZE` (32 MB) plus `ICO_ARENA_HEADROOM`
(8 MB), zero filled (`VirtualAlloc` on Windows, `mmap` elsewhere), with its base 1 MB aligned so
every address keeps the EE's residue modulo any alignment up to 1 MB
(`port/platform/arena.h`; `port/platform/arena.c` (`ico_arena_init`)). `ico_arena_ee_addr(ee)`
is the host address of EE physical address `ee`. `ico_host_init` logs the base, which turns the
object pointers in the renderer's draw keys back into EE addresses
(`port/platform/host_loop.c` (`ico_host_init`)).

The game's `iosInitialize` makes its root [partition](ARCHITECTURE.md#terms) over
`ico_arena_ee_addr(0x760000)` to the EE's end plus `ICO_HOST_HEAP_EXTRA`, and carves the named
partitions from the root's end down, so the stage partition keeps the EE's start
(`ico2/fumi/ios/ios.c` (`iosInitialize`)). Four partitions are larger on the host, because a block
header is 0x50 bytes there (0x40 on the EE) and records with pointers grow: stage by
`ICO_HOST_STAGE_EXTRA` (4 MB), common by `ICO_HOST_COMMON_EXTRA` (0x1E0000, which also carries the
packet banks' host pad, `ico2/seki/src/DmaPacket.c`, `DPK_HOST_PAD`), the static motion partition by
`ICO_HOST_SMOTION_EXTRA` and the demo motion partition by `ICO_HOST_S2MOTION_EXTRA`
(`ico2/fumi/ios/ios.c`). Together they are 7 MB of the arena's 8 MB headroom
(`port/platform/arena.h` (`ICO_ARENA_HEADROOM`)).

Built with `ICO_HEAP_STATS`, the game's allocator reports each allocation and free, and the arena
keeps the bytes in use and the high-water mark per partition, logging every new mark that grows
by 64 KB (`port/platform/arena.h` (`ico_heap_stats_alloc`); `ico2/fumi/ios/memory.c`).

## Register pages

`port/compat/eeregs.h` gives the EE's register names (`T0_COUNT`, `D2_CHCR`, `GIF_STAT`, `GS_CSR`,
...) as pointers into two blocks, `ico_hw_eeio` (the 64 KB EE I/O page at 0x10000000) and
`ico_hw_gs` (the GS privileged page at 0x12000000), each name at its EE address minus the block base
(`port/platform/hwregs.c`). A store lands in the block and a load reads it back; nothing reacts. The
code that waited on hardware is replaced by the library that owns the hardware: the vsync waits are
busy waits on the scheduler (above), `sceGsSyncPath` reports every path drained, `sceDmaSend` sends
nothing (`port/null/gfx_null.c` (`sceGsSyncPath`, `sceDmaSend`)), and the movie player's
[IPU](ARCHITECTURE.md#terms) and DMA register code is not compiled (`cmake/IcoSources.cmake`
(`ICO_EE_ONLY_SOURCES`)). Two registers do change on the host: the host loop's `GS_CSR.FIELD`
(`ico2/common/src/main.c` (`ico_vsync`)) and the timer counters (below).

## Floating-point environment

`ico_fpenv_sim_enter` sets the host FPU to round toward zero with flush-to-zero and
denormals-are-zero (MXCSR `0x1F80 | RC=toward zero | FTZ | DAZ` on x86-64; FPCR RMode toward zero
and FZ on arm64), the closest IEEE mode to the EE's FPU; `ico_fpenv_host_enter` puts back round to
nearest with every exception masked for host code (`port/platform/fpenv.c`). Any other architecture
is a compile error. With `ICO_FPTRAP` (the `fptrap` CMake preset) the divide-by-zero and invalid
exceptions are unmasked in simulation mode, so the first such operation stops the program at the
instruction (`port/platform/fpenv.h`).

The fiber switch does not save MXCSR or FPCR on minicoro's assembly backend, and its Windows fiber
backend gives each fiber an FP state of its own; so the scheduler runs a start hook,
`ico_fpenv_sim_enter`, at the top of every fiber (`port/platform/fiber.h`;
`port/platform/sched.h` (`ico_sched_set_fiber_start_hook`)), and `ico_host_step` enters the
simulation mode again before the vsync and after the audio push (`port/platform/host_loop.c`).
`ico_sched_call_on_host` brackets its host function with the host mode
(`port/platform/sched.h`).

## Clocks and EE timers

`sceCdReadClock` reports the host's local wall clock, or 2002-01-01 00:00:00 when the clock is
fixed, so trace runs and the save serial (which packs this clock) are reproducible
(`port/platform/clock.h` (`ico_clock_now`); `port/data/cdvd_host.c` (`sceCdReadClock`)). The clock
is fixed unless `ICO_FIXED_CLOCK` is `0`; `host_config.c` sets that variable from
`[dev] fixed_clock`, else fixed for the headless build or a trace written to an explicit path
(`port/platform/host_config.h` (`ico_host_fixed_clock`)).

The EE timers T0 to T3 are counters in `ico_hw_eeio`. A vsync callback advances each one whose
mode's CUE bit is set, at the rate its CLKS field selects (bus clock 147.456 MHz, /16, /256, or
the horizontal blank), 16 bits wide, setting the overflow flag on a wrap; a write by the game is
kept (`port/platform/clock.h` (`ico_clock_timers_step`)). The rate follows the game's video mode
word (`ico_clock_set_mode_word`, called by `ico_host_init`). The callback is registered by the
first libscf call of the boot (`port/config/sysconf.c`; `ico_clock_timers_attach`).

## Folders, ico-pc.ini and the user folder

Three places matter (`port/platform/host_config.h`):

- the executable's folder (`ico_host_exe_dir`): `ico-pc.ini` and `logs/` beside the program on
  Windows and Linux;
- the [user folder](ARCHITECTURE.md#terms) (`ico_host_pref_dir`): `config.toml`, the memory cards
  (`memcard/`), the extracted `ico.o2r`, `dumps/`, the developer `dev/` folder. In the window build
  it is SDL's `SDL_GetPrefPath("ico-pc", "ico-pc")`; in the headless build and the tests, which have
  no SDL, the executable's folder (`port/platform/host_config.c` (`ico_host_pref_dir`);
  `port/config/CMakeLists.txt` (`ICO_HOST_SDL_PREFPATH`));
- the disc image, found beside the executable, through `iso=`, `$ICO_ISO`, `baserom/Ico_PAL.iso`
  under the working folder, or a file dialog whose answer is saved as `iso=`
  (`port/platform/main_host.c` (`find_iso`)).

Per platform:

| platform | ini and logs | user folder |
| --- | --- | --- |
| Windows, Linux (window build) | beside the executable | SDL's pref path, or `userdata/` in portable mode |
| macOS app | the user folder (`port/platform/host_config.h` (`ico_host_ini_path`, `ico_host_logs_dir`)) | SDL's pref path, or `userdata/` beside the executable inside the bundle in portable mode |
| headless build | beside the executable | the executable's folder |
| Android | the app's files folder | the same folder, always |
| iOS | the app's Documents folder | the same folder, always |

Portable mode makes the user folder `userdata/` beside the executable, created if missing: on with
`portable=1` in `ico-pc.ini` or when that folder already exists, off with `portable=0`
(`port/platform/host_config.c` (`portable_dir`, `ico_host_portable_value`)). A folder that cannot
be made falls back to the normal user folder with one log line. The Android and iOS builds are
compiled with `ICO_HOST_FORCE_PORTABLE`: the user folder is the executable's folder itself (the
app's files folder on Android, Documents on iOS), and a `portable=` line is logged as ignored
(`port/platform/host_config.c` (`ico_host_set_portable`); `CMakeLists.txt` (the Android and iOS
targets)). The Android layout is listed in `port/platform/android/android_paths.h`: everything in
the files folder (shared storage when writable), the Vulkan pipeline cache in the cache folder.

`ico-pc.ini` is `key=value` lines with `#` or `;` comments; a quoted value loses its quotes; a
relative path is taken from the executable's folder. Its keys win over the matching
`config.toml` entries, which win over the defaults; `ico_ini_load` also exports the keys other
libraries read from the environment (`ICO_AUDIO`, `ICO_AUDIO_DUMP`, `ICO_AUDIO_VOLUME`,
`ICO_AUDIO_MUSIC`, `ICO_AUDIO_EFFECTS`, `ICO_AUDIO_DEVICE`, `ICO_FIXED_CLOCK`, `ICO_START_STAGE`,
`ICO_RD_DUMP_*`) (`port/platform/host_config.h` (`ico_ini_load`, `ico_config_ini_key`)). Paths are
UTF-8 throughout and reach the file system through `host_fs.h`, which converts to UTF-16 and calls
the wide functions on Windows (`port/platform/host_fs.h`). [CONFIG.md](CONFIG.md) lists every key.

## Diagnostics

The log is `logs/ico-pc.log`, stdout and stderr redirected there, rewritten each run with the run
before kept as `logs/ico-pc-previous.log`; `--console` keeps them on the console
(`port/platform/main_host.c` (`keep_previous_log`); `port/platform/host_config.h`
(`ico_host_redirect_output`)). After the start time and folder the log's next line is
`ico_pc: version <label> (commit <hash>)`, or `ico_pc: development build (commit <hash>)` when the
build has no package label (`port/platform/main_host.c` (`host_main`); `CMakeLists.txt`
(`ICO_VERSION_LABEL`)). On Windows the streams are fully buffered and the host loop flushes
them once per vsync; elsewhere they are unbuffered. Other files in `logs/`: the
trace (`trace-<time>.txt`, by default in the headless build only), the
pad recording (`input-<time>.txt`, by default in the window build only), `ico-pc-perf.csv` with
`[dev] perf_log`, and `audio.wav` with `audio_dump=1` (`port/platform/main_host.c` (header
comment); `port/platform/window_host.c` (the perf record comment)).

`ico_diag_init`, called as soon as the log is open, installs the crash handlers: a vectored handler
and the unhandled-exception filter on Windows, SIGSEGV, SIGBUS, SIGILL, SIGFPE and SIGTRAP on an
alternate stack on POSIX, and `abort` (asserts, `ico_assert`, the scheduler's fatal errors). A crash
writes the cause, the faulting address as module+offset, the current game thread and its last
kernel call, return-address candidates, the heartbeat line and every thread, then the
[symbol lines](#the-symbol-lines) naming each offset and the [report file](#the-report-file), then
on Windows shows a message box naming the report and exits with code 3 (`port/platform/diag_host.h`;
`port/platform/CMakeLists.txt` (the link map)). Every
diagnostic line is written with one unbuffered OS call to its own handle on the log, so a crash
leaves everything before it (`port/platform/diag_host.c` (header comment)). On Android the handler
then puts the system's handlers back so the system still writes its tombstone
(`port/platform/diag_host.c` (`restore_handlers`)).

A crash while the renderer builds a pipeline is a graphics driver crash: the activity note
(`ico_diag_set_activity`) starts with "creating pipeline" or "creating the start-up pipelines". On
Windows, Linux and macOS the handler then starts the program again at once, with
`ICO_STARTUP_RETRY=<step>:<backend>` in the new run's environment; nothing is written to the user
folder or the settings (an end without a crash report has a step of its own, which writes one
on the second such end in a row: below). The steps come in order, each taken only when the run before crashed:
`cache` (the crashed run had loaded the pipeline cache file: it is deleted and the same backend
tries again), `fixedblend` (the note names a pipeline whose `dst` field is nonzero, an exact-blend
pass with a fragment shader interlock or rasterizer-ordered view entry: the same backend with the
fixed-function blend, `ICO_RD_BLEND=fixed` from `ico_ini_export` as for the loss steps, which builds
no such pipeline; skipped when the player chose the blending with `blend=` or `ICO_RD_BLEND`;
`startup_retry_note_has_dst` reads the note), `d3d12` (a build with the Direct3D 12 backend whose
player named no backend; its own blending, not the fixed one), `lazy`
(each pipeline is built when a frame first needs it), then none: the crash ends as any other, with
the driver-crash text in the crash box where the platform shows one (`port/platform/startup_retry.h`
(`startup_retry_next`); `port/platform/diag_host.c` (`driver_crash_plan`, `driver_box_text`)).
Everything the relaunch needs is captured at start-up, so the handler does not allocate
(`port/platform/diag_host.h` (`ico_diag_prepare_relaunch`)). On Windows the relaunch is
`CreateProcessW` with the module path, `GetCommandLineW` and a copy of the environment block, and
the crashed process then ends; on Linux and macOS it is `execve` of `/proc/self/exe` or
`_NSGetExecutablePath` with the same `argv`, which keeps the process id (`port/platform/diag_host.c`
(`relaunch_now`); `port/platform/startup_retry.h` (`startup_retry_block_copy`,
`startup_retry_envp`)). Android and iOS do not relaunch. The new run appends to the crashed run's
log instead of keeping it as `ico-pc-previous.log`, logs
`ico_pc: restarted after a graphics driver crash while preparing the shaders: <what this run does>`,
and takes the step's backend
unless the player chose one (`port/platform/main_host.c` (`host_main`);
`port/platform/window_host.c` (`ico_window_open`); `port/platform/startup_retry.c`
(`startup_retry_what`)). ctest `startup_retry` checks the steps and the relaunch's environment
(`port/platform/test/startup_retry_test.c`).

A lost graphics device (the driver reset the card, `DXGI_ERROR_DEVICE_HUNG`, `VK_ERROR_DEVICE_LOST`:
`rhi_device_lost`, not a crash) restarts the program too, on Windows, Linux and macOS. The window
logs the loss and the renderer's pass ring, then asks for the loss step
(`port/platform/window_host.c` (`device_lost_quit`); `port/platform/diag_host.c`
(`ico_diag_device_lost_restart`)); the steps come in order, each taken only when the run before
lost the device: `blend` (the same backend with the fixed-function blend, `ICO_RD_BLEND=fixed`, so
no fragment shader interlock or rasterizer-ordered view passes; skipped when the losing run already
drew with it or the player chose the blending with `blend=` or `ICO_RD_BLEND`), `backend` (the
other backend, Direct3D 12 from Vulkan or Vulkan from Direct3D 12, with the fixed-function blend; in
a build with both, when the player named no backend and no blending other than `fixed`), then none
(`port/platform/startup_retry.h` (`startup_retry_next`, `StartupRetryRun`); `port/platform/startup_retry.c`
(`loss_next`)). With a step, the session ends through the normal quit path without a box (the
settings are saved and the window, audio and files closed as at any quit) and the program starts
again at exit, after the end-of-run steps, with `ICO_STARTUP_RETRY=blend:<backend>` or
`backend:<backend>`; the relaunch is the same `relaunch_now` as the crash's, run from an `atexit`
handler registered before the end-of-run steps (`port/platform/diag_host.c`
(`loss_relaunch_at_exit`, `loss_relaunch_register`)). When it cannot start the program, Windows and
Linux show a box. Without a step the window shows its box, which says the game already restarted
itself with simpler blending when the run was a loss step's. Android and iOS keep the box. The new
run starts at the title as a fresh launch (what was not saved was lost with the card, as at a quit),
appends to the log, logs
`ico_pc: restarted after the graphics card stopped responding: <what this run does>`, and gets
`ICO_RD_BLEND=fixed` from `ico_ini_export`, which wins over `blend=` and over an `ICO_RD_BLEND` in
the environment for that run (the step is never taken against a player's blending other than
`fixed`) (`port/platform/host_config.c` (`ico_ini_export`, `ico_dev_env_export_value`)). When the
`backend` step's Direct3D 12 cannot start, that run goes on with Vulkan itself
(`port/platform/window_host.c` (`ico_window_open`)). The step only grows (the loss steps come after
`lazy`), so a crash in a loss step's run ends with the crash box and every chain ends. Nothing is
written to the user folder. ctest `startup_retry` checks both orders over every input
and the loss values in the relaunch's environment; ctest `host_config` checks the `ICO_RD_BLEND`
precedence (`port/platform/test/host_config_test.c` (`test_retry_blend`)). ctest `diag` runs the
loss restart end to end with the relaunch prepared and stubbed (`ico_diag_set_relaunch_stub`, so no
process is started): the step and its value (`blend:vulkan`, then `backend:d3d12`), the quit path's
`atexit` steps before the relaunch, nothing at exit when no step is left, and the box when the
relaunch fails (`port/platform/test/diag_test.c` (`loss_child`)).

An end without a crash report (the system or the driver ends the process) is caught by the start-up
activity file, `logs/startup-activity.txt`: the version's line, then the activity note. While the
start-up pipeline set is created (`ICO_DIAG_ACTIVITY_STARTUP`) each batch's note (its range of the
set and its first and last key, [RENDER.md](RENDER.md#pipelines)) replaces the text, and the file is
deleted when the set is done; a lazy run (`ICO_DIAG_ACTIVITY_LAZY`) records
every first-use pipeline and a line between them for the whole run. The file is opened once when
the recording starts and closed when it ends; each note is one write at the file's start, padded
with NULs over a longer text before it, then the file is cut to the note, so it reads as one whole
note even when the process is ended between the two (`port/platform/diag_host.c` (`act_write`,
`act_open`, `act_close`)). The next start of the same version logs
`ico_pc: the previous run ended while: <note>` and creates the pipelines as the frames need them
(for how long: the chain's step below);
another version's file is logged (`ico_pc: the previous run ended while: <note> (version <old>);
trying the start-up set again on <new>`, without the last clause when the note is an end while
making the pipelines, which the chain's step below counts whatever the version); a lazy run's clean
exit leaves its own
line, logged as `ico_pc: the previous run created the pipelines as the frames needed them; this one
does too`, so the lazy start sticks until the version changes (`port/platform/diag_host.h`
(`ico_diag_startup_activity_begin`, `ico_diag_startup_activity_exit`)).

The chain's step for an end without a crash report (issues 93 and 107). Such an end is ambiguous:
the driver may have killed the process, or the player swiped the app away, the system ended it in
the background, or Task Manager did. So the first one changes one start only, and the second in a
row makes it last. A note that names a pipeline being made (`creating pipeline ...`, `creating
pipelines ...` or `creating the start-up pipelines`), from a start of any version that was not a
restart after a crash report and was in the foreground, counts one end in a row of its kind: an
extended VU batch's (` ext 1`, `startup_retry_note_has_vu_ext`) or else the pipelines'. The counts
live beside the activity file in `logs/startup-activity-ends.txt` (`pipelines <n>`, `vu_ext <n>`),
and each counted end logs `ico_pc: starts in a row that ended while preparing the graphics (the
pipelines): 1` (`(the extended VU shaders)` for the other kind) (`port/platform/diag_host.h`
(`ico_diag_startup_end_count`); `port/platform/diag_host.c` (`ends_count_prev`)). A count file
that cannot be written is logged once a run: `ico_pc: the count of starts that ended while
preparing the graphics could not be written to <path> (pipelines <n>, extended VU shaders <n>)`
(`ends_write`). The first pipelines' end makes the next start, and only that one, skip the
start-up set (`ico_diag_startup_pipeline_set`, `ICO_DIAG_PIPESET_LAZY_ONCE`); once the device is
made it logs `ico_pc: the previous start ended while preparing the graphics (<note>); this start
only makes the pipelines as the game needs them, with no start-up set (nothing is written to
<path>); if this start also ends while preparing them, [dev] pipeline_set = "lazy" is written for
good` (`ico_diag_startup_end_persist`); its notes between two creations and its clean exit say
`for this start only`, so the start after a clean exit logs `ico_pc: the previous run made the
pipelines as the frames needed them for that start only; this one tries the start-up set again`,
and the start after one ended between two creations `ico_pc: the previous run ended while: running
with the pipelines created as the frames need them for this start only (none was being created);
that lazy run was for that start only, so this one tries the start-up set again`. A second end in
a row (that lazy start also ended inside a pipeline) writes `[dev] pipeline_set = "lazy"` to
`config.toml` once the device is made and logs `ico_pc: the previous start ended while preparing
the graphics (<note>); this start and every start after it make the pipelines as the game needs
them, with no start-up set ([dev] pipeline_set = "lazy" in <path>); delete that line to try the
start-up set again` (`ico_diag_startup_end_persist`; `port/platform/window_host.c`
(`pipeline_set_persist_lazy`)), and the renderer's line says `[dev] pipeline_set = "lazy" from now
on`. The count stays in the file until then: a start that is due but fails before the device is
made leaves it, and the next end in a row is due again. When the write fails the line ends `,
which could not be written: this start only`, the renderer's line says `[dev] pipeline_set =
"lazy" could not be written, so for this start only`, the lazy run's notes say `for this start
only`, and the count stays, so a further end is due again. With `[dev] pipeline_set = "lazy"`
already set the pipelines' count is cleared and no line about it follows. It is the crash order's `lazy` step made to last, for the ends a
crash handler never sees (the system ending the app, a device that freezes); a crash with a report
keeps its own order above. The setting outlives the version, as `[dev] vu_plain_only` does; deleting
the line makes the next start try the set again ([RENDER.md](RENDER.md#the-start-up-set)).

What breaks a row: an end in the background (below) breaks the row of its kind; any other note in
the file (the run ended between two creations, or in the loss bisect) breaks the pipelines' row; a
start-up set that finished, or a lazy run's normal exit, clears the pipelines' row, and the extended
VU row too when that run built extended VU pipelines (`ends_clean`). The plain run an extended VU
end gives builds none, so it leaves that row as it was, and the next start that compiles them and
ends there again is the second. A restart after a crash report counts nothing.

The background mark (Android and iOS only: the desktops send no lifecycle events, so an end there
in the background counts): while the activity file records, the app going to the background (the
lifecycle's `SDL_EVENT_WILL_ENTER_BACKGROUND` on Android and iOS, `lifecycle_watch`, or Android's
`onTrimMemory` at level 20 or more, which arrives on the activity's thread even when the main
thread is held) writes `logs/startup-activity-background.txt` (the version, `background: the app
went to the background at pipelines 201 of 527 of the start-up set` or `with <n> pipelines made`,
then every `system: ` line that comes while there) and logs `ico_pc: the app went to the background
at pipelines 201 of 527 of the start-up set; an end there is not counted as one while preparing the
graphics`; coming back deletes it (`ico_pc: back in the foreground; the background mark is
deleted`), and so does the end of the recording. The mark is set under the log's lock, so when
both signals come at once (the lifecycle watch and `onTrimMemory(20)` on the Java thread) only the
thread that set it writes the text and logs. The app's state is kept whether the file records or
not: when the recording starts with the app in the background (Home while the renderer starts,
seen by `onTrimMemory(20)`), the mark is written then (`background: the app was in the background
before preparing the graphics began (with 0 pipelines made)`, logged the same way with `; an end
there is not counted as one while preparing the graphics`). The lifecycle watch itself is added
after the renderer starts, so a Home press before that is seen only through Android's
`onTrimMemory(20)` (not on iOS). The next start reads and deletes the mark: an end
while preparing the graphics with it is logged as `ico_pc: the previous run ended while: <note>, in
the background (the app went to the background at ...); the system ends apps there, so it is not
counted as an end while preparing the graphics: the start-up set as usual`, and the mark's system
lines the activity file did not get as `ico_pc: the previous run was told by the system in the
background: <line>` (`ico_diag_startup_activity_background`). A mark beside any other note (a lazy
run ended between two creations, a restart after a crash report) is logged as `ico_pc: the
previous run was in the background when it ended (<line>)` and decides nothing.

On a device whose table entry already rules a kind nothing is ever written for it, and its count
is cleared: the setting would be redundant and would outlive a change of the table. For the
pipelines that is the lazy column (`RhiLimits.lazyPipelines`, the Adreno 740 row of
`port/rhi/vk/vk_device.c`); the first end logs `ico_pc: the previous start ended while preparing
the graphics (<note>); nothing is counted or written to <path>: the device table already makes the
pipelines as the game needs them on this device (Adreno 740, issues 93 and 107)`, and its lazy run
follows the table, its notes without `for this start only`; the second in a row logs `ico_pc: the
second start in a row ended while preparing the graphics (<note>); [dev] pipeline_set = "lazy" is
not written to <path>: the device table already makes the pipelines as the game needs them on this
device (Adreno 740, issues 93 and 107)`. For the extended VU kind it is the plain-only column
(`RhiLimits.vuPlainOnly`): `... [dev] vu_plain_only = true is not written to <path>: the device
table already draws with the smaller 3D shaders (the VU plain variants) only on this device
(Adreno 740, issue 107)`.

A batch's note, and any note naming an extended VU pipeline, is written through to the disc
(fsync, `act_sync_note`) before the driver starts on it, and in a lazy run every first-use
pipeline's note, so it is still there after a device that froze was rebooted (issue 107). Each
batch costs one sync: its `compiled: ` line (below) is written without one and goes to the disc
with the next note's sync (`act_write_sync`, `act_unsynced`). An end inside a batch of the VU
vertex shaders' extended variants follows the same rule with its own count and setting: the first
makes the next start, unless it is a restart after a crash report, draw with the plain variants
for that start only (`ico_pc: the previous start ended while the graphics driver compiled the
largest 3D shaders (the VU extended variants); this start only uses the smaller ones (nothing is
written to <path>): the wide-screen rule is off for this start; if a start that compiles them ends
there again, [dev] vu_plain_only = true is written for good`, logged once the device is made,
`ico_diag_startup_vu_plain`, `ico_diag_startup_end_persist`); the
second in a row writes `[dev] vu_plain_only = true` to `config.toml` and logs `ico_pc: the previous
start ended while the graphics driver compiled the largest 3D shaders (the VU extended variants);
this start and every start after it use the smaller ones only ([dev] vu_plain_only = true in
<path>): the wide-screen rule is off; delete that line to try the larger ones again`
(`vu_plain_persist`). Either start builds the set as usual, not lazily
([RENDER.md](RENDER.md#the-vu-shader-variants); `port/platform/window_host.c`,
`ico_diag_startup_activity_prev`). While the set is created the heartbeat line ends with the note
in flight (`| creating pipelines ...`), so a log that stops mid-set says what the driver was
compiling; a VU batch's note names each vertex shader with its words and SPIR-V instructions and
ends with the device's compiler (`; compiler E031.41.03.47`). Each finished batch adds a
`compiled: ` line after the note (the last 24 lines, written at once, so on the disc with the next
batch's note): its range, kind (2D, or VU with its variant), compiler, pipeline count, the compile
times' minimum, maximum and mean, the batch's wall time and threads, the process's memory and the
pipeline cache's size after it, then a VU batch's times by vertex shader with its words and
instructions or a 2D batch's slowest pipeline with its modules; a run without the set adds its lazy
batches' lines the same way ([RENDER.md](RENDER.md#the-start-up-set); `rd.h` `rd_set_batch_fn`,
`diag_host.h` `ico_diag_startup_activity_batch`). The next start logs each as `ico_pc: the previous
run (version V) compiled: VU batch 297-328 of 702 (plain, compiler ...): 32 pipelines 0.20-0.90 s,
mean 0.41 s, ...` before the note it ended in, then the last one again as `ico_pc: the previous
run's last finished batch: <line>`, so a device that froze shows where its compile times stood
against the shaders' sizes (issue 107) and a start the system ended shows its memory batch by
batch (issue 93); the log itself has the line as each batch completes, with every pipeline's time
(`rd: pipelines: compiled ...; each (s): ...`). On Android the system's request to free memory
(`onTrimMemory`, which `IcoActivity` hands to `nativeTrimMemory` with its level before SDL turns it
into `SDL_EVENT_LOW_MEMORY`, which has none) is logged at once from the activity's thread: `ico_pc:
the system asked the app to free memory: low memory (onTrimMemory level 15, running critical;
signal 1 of this run) at pipelines 201 of 527 of the start-up set; memory 102 MB (most 194 MB)`
(`with <n> pipelines made` outside the set). Only levels 5, 10 and 15 (`TRIM_MEMORY_RUNNING_*`:
the device is low on memory while the app runs) are called low memory; level 20 is `the app's
screen was hidden` (`TRIM_MEMORY_UI_HIDDEN`) and 40, 60 and 80 `the app is in the background`
(its place in the system's list of cached processes; Android 14 and later send 20 and 40 only),
which say the app left the foreground, not that memory ran short. The main thread's next poll (the start-up screen's
progress, the window's pump: `ico_diag_startup_activity_poll`) adds it to the activity file as a
`system: ` line among the `compiled: ` ones, and the next start logs it as `ico_pc: the previous
run (version V) was told by the system: low memory ...`, so an end the system chose shows whether a
low-memory signal came before it (`port/platform/diag_host.h` (`ico_diag_note_low_memory`);
`port/platform/android/host_android.c`). The activity note holds 640 bytes. The driver compiles the set on worker threads
(`rhi_create_pipelines`, [RHI.md](RHI.md#the-interface)); the main thread waits in the renderer's
progress function, about every 50 ms, and from it shows the "Preparing graphics (N of M)" screen on
every platform, drawn only once the set has taken 250 ms and then at most ten times a second, with
the time left from the rate so far under the bar once 3 s and 8 pipelines have passed ("About 2
minutes left") and, on Android, "This happens once after an update". It takes the window's events
each time it draws, so Windows does not mark the window "Not Responding" and Android does not offer
to close the app on a cold driver cache; a quit or close request, or a lost device, stops the set
after the batch in flight and the run ends (`port/platform/window_host.c` (`pipeline_progress`,
`pipe_estimate`, `progress_present_notes`)). While the set runs the heartbeat line ends with
`pipelines N of M, longest 1.3 s`, the count and the longest single compile so far
(`port/platform/diag_host.h` (`ico_diag_set_pipelines`)). A set that was not stopped is then
written to the Vulkan pipeline cache file at once (`rhi_pipeline_cache_save`,
[RHI.md](RHI.md#vulkan)), not only at a clean exit, so the "once after an update" holds
when the system ends the app or the device is lost later. A crash on a worker thread inside the
driver is reported as any crash, with the batch's note as the `while:` line, so the restart steps
treat it as a driver crash while preparing the shaders; the fixed-blend step reads the batch's
`dst` field, which every key of the batch shares (`port/platform/startup_retry.h`
(`startup_retry_note_has_dst`)).
A device loss adds a third line, `loss dump: <zip>` (or
`graphics device lost, no loss dump`), written at the loss and kept through the run's end whatever
the note and the mode, so the next start finds it even when the process is ended after the loss
(`ico_diag_note_device_loss`). That start logs `ico_pc: the previous run lost the graphics device;
its loss dump is <zip>` and, for a dump of the same version, runs the loss bisect once before the
game ([RENDER.md](RENDER.md#the-loss-bisect); `ico_diag_loss_bisect`, the bisect itself a
callback, `port/platform/window_host.c` (`window_loss_bisect`)): the file's note says the bisect is
running while it runs (`replaying the lost frame pass by pass (the loss bisect)`), the result is
logged as `ico_pc: loss bisect: <line>`, then `ico_pc: loss bisect: done; the game starts`, and the
file is deleted. A start ended inside the bisect leaves that note; the start after logs it, is not
lazy for it and does not bisect again. A loss of another version is logged, not replayed. ctest
`diag` runs these steps with the bisect stubbed (a lost pass, a clean frame, a bisect that could not
run, a start ended inside it), the loss noted on a run at the loss restart with the relaunch stubbed,
a lazy run's loss and a killed run's (`port/platform/test/diag_test.c` (`activity_child`,
`bisect_stub`)).

When the first level ends on a pass that drew, the second level narrows it to the draw in the same
start ([RENDER.md](RENDER.md#the-loss-bisect); `ico_diag_loss_bisect2`, the step a callback,
`port/platform/window_host.c` (`window_bisect_step`)). Its state is a file of its own,
`logs/loss-bisect.txt`, not the activity file:
```
ico-pc loss bisect 1
version <the program's version>
zip <the loss dump>
pass <P> draws <N> passes <T>
starts <k>
step <i> <kind> <first> <last> <filter> <arg> <lost|clean|killed|-> <ms> key <valid> <vu> <skinned> <vs> <clip> <tex> note <text>
```
one `step` line a step, written before the step runs with `-` for its verdict and again with the
verdict after it; the file is replaced whole each time (a temporary file renamed over it). The next
start reads it as untrusted: lines past 1100 bytes, numbers out of range, a step out of order, a
kind or filter it does not know, or another version's file delete it (logged); other lines are
ignored. A step without a verdict becomes `killed` (logged as `ico_pc: loss bisect: continuing at
step 7 (start 2 of 4; step 6 ended the run)`); a step whose renderer did not start is planned again
at the next start. The file is deleted when the plan ends (16 steps at most), after 4 starts, or
when the dump cannot be replayed; a start spends at most 90 s on steps and leaves the rest to the
next, and it runs at most 3 steps that lose the device: after the third, `ico_pc: loss bisect: 3
lost steps in this start; step 5 waits for the next start`. With the first level's loss a start
then loses the device at most 4 times, under the 5 graphics resets in 60 s after which Windows
restarts the whole system (its `TdrLimitCount` and `TdrLimitTime` defaults); the bound holds on
every platform, and the state file carries the rest to the next start, within the 4. While a step runs the activity file's note says so (`replaying the lost frame draw by draw
(the loss bisect)`); a start after one ended inside a step logs it, is not lazy for it, and the
second level continues. An unfinished second level goes before a newer loss dump, which stays in
logs. ctest `diag` runs it with the step stubbed (`actbisect2`, `ICO_TEST_BISECT2`): the draw
found in 4 starts of at most 3 lost steps each, a start killed at step 3 continued at step 4, a renderer that did not start, the 16-step and
4-start bounds, the time budget, and state files that do not parse or are another version's
(`port/platform/test/diag_test.c` (`bisect2_stub`)).

A heartbeat and a watchdog share one OS thread of their own, so a game thread that spins without a
kernel call cannot stop them. Every 2 s the heartbeat logs one line of progress
(`port/platform/trace_host.c` (`ico_host_status`: vsyncs, ticks, stage, the disc and
[SIF](ARCHITECTURE.md#terms) counters)). The watchdog stops the run, with every thread dumped and
exit code 4, when no Main tick comes `watchdog=` seconds after boot (default 30) or no new tick or
other progress for twice that; a movie notes each picture as progress, time inside a present is
excused for an effects program, and the first limit is doubled when one is loaded
(`port/platform/diag_host.h` (`ico_diag_start`, `ico_diag_note_progress`);
`port/platform/main_host.c` (`start_watchdog`)).

**A wait the system holds.** The hang verdict applies to the game's own code only. The window build
marks the main thread's calls into the system that run none of the game's code (`SDL_PollEvent`, and
on Windows `SDL_UpdateJoysticks`) with `ico_diag_os_wait_enter`/`ico_diag_os_wait_leave`, and the time
inside them counts towards neither limit. A wait that outlasts the limit in force is named once:

```
ico_pc: watchdog: the window's event pump has not returned for 60 s (the system is holding it: the game is not hung); waiting
ico_pc: watchdog: in the system's messages and SDL's own work (SDL_PollEvent), handling WM_xxx(0x....) for 59.8 s; last messages before it: ...; SDL events before it: ...
```

then `ico_pc: watchdog: the window's event pump returned after N s; the game goes on` when it comes
back, and the heartbeat carries `| in the window's event pump N s` while it lasts (outside its
"no progress" comparison). The run still ends with the watchdog's report when one wait passes
`[dev] watchdog_pump` seconds (default 600, 0 no limit), so a deadlock in the system is reported; a
system sleep inside a wait counts towards neither. The bound holds only while a watchdog limit does:
with `[dev] watchdog = 0`, and on Android before the game starts (the diagnostics thread runs from
the start, `ico_diag_start_reporter`, while the start-up set's progress screen pumps the window's
events), a wait never ends the run, and time in the background pauses the watchdog there too
(`lifecycle_watch` before the window loop is open), so Home pressed on "Preparing graphics" for an
hour does not end the app (`port/platform/diag_host.h`
(`ico_diag_os_wait_enter`, `ico_diag_set_os_wait_limit`, `ico_diag_set_os_wait_describe`)). The case
behind it: a Windows player's game, paused and left alone, had its pump held for 50 s three times in
a row right after a controller turned itself off, and the 60 s limit ended a run that was waiting on
Windows, not hung. ctest `diag` checks a 3.5 s held pump against a 1 s limit (no end, the line
once), a stall in the game's code right after it (the end), the bound, and the bound with no limit
in force (`pump`, `pumphang`, `pumpbound`, `pumpearly`, `pumpoff` in
`port/platform/test/diag_test.c`). Milestone lines carry the wall time, vsyncs, Main
ticks and the current thread (`ico_diag_milestone`), and game threads are named by the function
`iosThreadCreate` runs (`ico_host_thread_func`). `ico_diag_float_fault` logs a NaN that reaches game
code the PS2 never fed one (`port/platform/diag_host.h` (`ico_diag_float_fault`)).

The window build logs a stats block every 10 s. A host step longer than `[dev] slow_step_ms`
(default 8, 0 off) logs a slow-step line ([DISPLAY.md](DISPLAY.md#pacing);
`port/platform/window_host.c` (`slow_step`); `port/platform/host_loop.h` (`IcoStepProfile`)).

**The event pump's long calls.** Each part of `ico_window_pump` over 1 s logs a line of its own:

```
window: the event pump's PART took N ms at vsync V (Main tick T); last messages before it: WM_TIMER(0x0113) x3 0.4 s ago, WM_DEVICECHANGE DBT_DEVICEREMOVECOMPLETE(0x0219) 50.3 s ago, ...; SDL events before it: gamepad removed 50.3 s ago, ...
```

PART is the system's messages and SDL's own work (`SDL_PollEvent`), on Windows the controllers'
scan and update (`SDL_UpdateJoysticks`), the handling of one event, the settings (`video_apply`),
the captures and the pointer, or the pads' reading and rumble. On Windows SDL's own joystick update
is taken out of `SDL_PollEvent` (`SDL_HINT_AUTO_UPDATE_JOYSTICKS` = 0 before `SDL_Init`) and called
at the pump's start (and on the start-up screens), so the two are timed apart: after a device
change SDL 3.4 enumerates the DirectInput devices on the thread that pumps
(`SDL_windowsjoystick.c` `WINDOWS_JoystickDetect`, `SDL_dinputjoystick.c` `SDL_DINPUT_JoystickDetect`,
whose own comment says `EnumDevices` can take a minute). The messages come from SDL's message hook
(`SDL_SetWindowsMessageHook`: the posted ones, for every window of the main thread, SDL's hidden
device-notification windows included) and a window procedure in front of SDL's on the game window
(the sent ones, which never pass the hook: `WM_SYSCOMMAND`, `WM_POWERBROADCAST`,
`WM_DISPLAYCHANGE`, `WM_ACTIVATE`...); both let every message through unchanged, and a message whose
handling takes over 1 s logs `window: the window message NAME took N ms in SDL's window procedure`.
The last 16 messages and 8 SDL events are kept, a repeat of the newest counted (`x3`), mouse moves,
raw input and hit tests left out; the names cover `WM_SYSCOMMAND` `SC_SCREENSAVE` and
`SC_MONITORPOWER` (on, low, off), `WM_POWERBROADCAST`, `WM_DEVICECHANGE`, `WM_DISPLAYCHANGE`,
`WM_ACTIVATE`, `WM_NCACTIVATE`, `WM_SETFOCUS`/`WM_KILLFOCUS`, `WM_SIZE`,
`WM_DWMCOMPOSITIONCHANGED`, `WM_INPUT_DEVICE_CHANGE` and `WM_TIMER` among others, and an unknown id
as `message 0x....`. The window's focus, visibility and display events log `window: system event:
NAME at vsync V`; a joystick that arrives or leaves logs `window: system event: joystick added at
vsync V: "Xbox Controller" (GUID 03000000..., XInput)`, recorded with its name, GUID and the SDL
driver its GUID names (byte 14: `x` XInput, `h` HIDAPI, `r` Raw Input, `w` Windows.Gaming.Input,
`g` GameInput, 0 DirectInput on Windows; a removal named from its arrival), and every slow-pump line
and the watchdog's description end with `controllers before it: removed "..." (GUID ..., XInput)
50.3 s ago, ...` (the last 4). The 10 s line carries `pump max N ms`
(`port/platform/window_host.c` (`pump_poll`, `pump_joysticks`, `pump_slow`); `port/platform/pump_watch.h`;
`port/platform/window_pump_win.h`).

**What settles issue 105.** The standing hypothesis is that the 50 s holds are SDL's DirectInput
enumeration after a controller change. The next log decides it four ways:

- A slow controllers' scan adds `window: the controllers' scan: N controllers after it; the HID
  device list changed K times during it (SDL_hid_device_change_count a -> b); the last controller
  arrived or left T s before it ended; the DirectInput scan is on|off`.
- SDL's own log: the window installs `SDL_SetLogOutputFunction` before `SDL_Init` with the INPUT
  and SYSTEM categories at VERBOSE, so the joystick drivers' own lines (a HIDAPI device added or
  removed with its path and driver, DirectInput's warnings) are kept in a ring of the last 64
  (`pump_watch_sdl_log`); every slow-pump line is followed by the ring's lines from 120 s before the
  part began (`window: SDL's own log from 120 s before it to its end (N lines, oldest first):`, then
  `window:   12.3 s ago INPUT debug: ...`), and the report file's `report.txt` ends with the whole
  ring (`ico_diag_set_report_extra`). SDL's info, warning, error and critical lines also go to the log as
  `window: SDL <category> <priority>: ...`, as SDL's own output wrote them before. Info and warning
  lines each go through the rate limiter under a key of their own (`window: SDL's log, info`,
  `window: SDL's log, warnings`: the first 8, then every 100th), so a flood of one cannot silence
  the other; error and critical lines are never limited and always logged (`sdl_log_output` in
  `port/platform/window_host.c`).
- On Windows the watchdog samples the held main thread at 2, 10 and 30 s into a held wait:
  `ico_pc: watchdog: the window's event pump has held the main thread for 2 s: at win32u.dll+0x...;
  code on its stack (innermost first): DINPUT8.dll+0x..., SDL3.dll+0x..., ico_pc_x64.exe+0x...`.
  Each stack word that points into executable image memory is named from the module's own export
  table (no loader lock, which the held thread may own: a device scan loads drivers), so
  `DINPUT8.dll` on the stack names the DirectInput scan and `hid.dll`, `setupapi.dll` or
  `cfgmgr32.dll` the HID enumeration (`port/platform/diag_host.c` (`os_wait_sample`,
  `code_address`)).
- The test: `[dev] joystick_directinput = false` (exported as `ICO_JOYSTICK_DIRECTINPUT=0`) sets
  `SDL_HINT_JOYSTICK_DIRECTINPUT` to 0 before `SDL_Init`; SDL then never creates its DirectInput
  object and `SDL_DINPUT_JoystickDetect` returns at once. XInput, HIDAPI and Raw Input still find
  the usual pads; a device only DirectInput knows is not found. The summary line says `controller
  ... (DirectInput scan on|off)` and a line names the switch when it is off. SDL 3.4 offers no
  switch that moves the scan off the main thread: `SDL_HINT_JOYSTICK_THREAD` (on by default) only
  runs the device-notification window and its 300 ms and 2 s timers on a thread of its own
  (`SDL_windowsjoystick.c` `SDL_JoystickThread`); the scan itself stays in `WINDOWS_JoystickDetect`,
  which `SDL_UpdateJoysticks` calls on the pumping thread. So there is no `joystick_update` key.

If the hypothesis holds, the line names the controllers' scan, the stack sample shows `DINPUT8.dll`
and the holds stop with `joystick_directinput = false`; if the stack shows another module, or the
held part is `SDL_PollEvent` with no controller change before it, the cause is elsewhere and the
same lines name it.

**The display and the computer awake.** SDL's `SDL_DisableScreenSaver` sets
`ES_CONTINUOUS | ES_DISPLAY_REQUIRED` on Windows (`SDL_windowsvideo.c` `WIN_SuspendScreenSaver`) and
returns 0 for `SC_SCREENSAVE` and `SC_MONITORPOWER` while it is in force (`SDL_windowsevents.c`,
`WM_SYSCOMMAND`); the display flag alone does not keep the computer out of its idle sleep. The
Windows window build sets `ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED` from the main
thread at the first pump and every 30 s after, logged once (`window: the display and the computer
are kept awake while the game runs`), but only while the window has the input focus and is not
minimized (the caller passes it from `SDL_GetWindowFlags`: `SDL_WINDOW_INPUT_FOCUS` set,
`SDL_WINDOW_MINIMIZED` clear). When it loses focus or is minimized, `SetThreadExecutionState(ES_CONTINUOUS)`
is called once to release and `window: the display may sleep while the game is minimized or in the
background` is logged; when it is focused again the next pump sets the flags at once and logs
`window: the display and the computer are kept awake again`. Both lines go through the rate limiter
(first 8, then every 100th). A game left running in front never lets the display or the computer
sleep, as a console does; one left behind does not hold them (`port/platform/window_pump_win.c`
(`ico_pump_win_keep_awake`)).

**A lost graphics device.** When the backend reports the device gone (`rhi_device_lost`), the window
pump and the start-up screens' pump end the session through the normal quit path: one line
`window: the graphics device was lost; quitting`, then the renderer's
[pass ring](RENDER.md#the-pass-ring) (the last replays and the passes of the last three, with the
sequence numbers the checkpoint markers carry and each effect's name from `rd_perf_post_name`), a
flush of the log, the [loss dump](RENDER.md#the-loss-dump) (`logs/loss-<date>-<time>.zip`, logged as
`window: loss dump: frames N and M (the one before) written to <zip>`, and noted in the activity
file), the [report file](#the-report-file) (which holds the loss dump and the log), and one message
box: the card stopped responding, the game has to close, the save is kept, "If the graphics card is
overclocked, try its stock clocks first.", and a request to attach the newest file in logs that
starts with report-, naming it (without a report, the loss dump's file name and a request to attach
it with the log); on a phone the box asks the same. The loss restart above comes after the loss dump, but a
restarted run does not bisect it: its step changed the blending or the backend, so its replay would
not be the path that lost the device, and a loss it reproduced would add a GPU reset to the
restart chain. It logs `ico_pc: loss bisect: not run (this start changed the blending or backend
after the loss); the loss dump stays in logs: <zip>`, and the zip stays for `rd_replay_tool
--loss-bisect`. Only a start at no restart step bisects. The box is modal:
the watchdog is paused before it (`ico_diag_watchdog_pause`, so the time the player takes to read
it is not reported as a hang), the heartbeat stops, and after it one line
`window: the message was closed after N s` and the watchdog running again, so a hang in the shutdown
that follows is reported, before the session ends
(`port/platform/window_host.c` (`device_lost_quit`)). The backend's own loss report comes before
it, at the first failing call ([RHI.md](RHI.md#device-loss)).

`port/platform/assert_host.c` is newlib's `__assert` as the game calls it: the same message on
stderr, then `abort`, which the crash handler reports.

### The symbol lines

A crash or watchdog report names, after its raw lines, every offset in the program it printed (the
faulting address, the stack's return-address candidates, the thread table's `fn <module>+0x...`
and each last kernel call's caller), in the order printed, from the symbol file shipped with the
program:

```
ico_pc: symbols from ico_pc_x64.map (each offset above in ico_pc_x64.exe, in order):
ico_pc:   +0x1b35e4 _CopyVector+0x4 (vu0.c.obj)
ico_pc:   +0x9add a static function in diag_test.c.o, +0x11d into its code
```

The file is the GNU ld link map beside the program, its name the program's with `.exe` replaced
by `.map` (`x64/ico_pc_x64.map` on Windows, `ico_pc.map` on Linux; both packages ship it), opened
at `ico_diag_init` and read only when a report is written; on Android it is the APK's
`assets/libmain.syms.gz`, `llvm-nm -n` of the unstripped `libmain.so` gzip-compressed and stored
by `tools/package_android.sh`, whose range of the APK file the window opens at start with
`AAsset_openFileDescriptor64` (`port/platform/android/host_android.c`
(`ico_android_open_symbols`); `ico_diag_set_symbols_fd`). macOS and iOS ship no map in the bundle:
their reports say so and keep the raw offsets (`port/platform/diag_host.c` (`sym_open_default`,
`report_symbols`)).

The parser (`port/platform/symmap.c`) streams the file twice through static buffers, a line at a
time (lines cut at 4096 bytes): the first pass finds the anchor `ico_diag_init`, whose map address
minus its offset in the running module places the map against the program (a PE map's image base,
a PIE's zero and a shared library's table read alike); the second compares every offset (64 at
most) with every symbol line and input section line of the code's output sections (`.text`,
`.init`, `.fini`, `.plt`). A map names global functions only: a symbol counts only inside its own
object's input section, and an offset in an object with no global function before it is printed as
the object and the offset into its code (`a static function in rd_blur.c.o, +0x1c0 into its
code`); a `.text.<name>` section (`-ffunction-sections`) names its function; an nm table's `t`
lines name static functions too. The map is checked against this build: `ico_diag_log` must come
out at `+0x0`, else one line says the map is not this build's and the offsets stay as they are.
Nothing is allocated, so it runs inside the signal handler of a crash; the symbol lines come after
the raw ones, so a parser that fails loses nothing; a 3 MB map is a few milliseconds
(`port/platform/symmap.h`).

### The report file

On a crash, a watchdog stop, a lost graphics device and graphics that could not start, the port
writes one zip, `logs/report-<date>-<time>.zip` (a second in the same second
`report-<date>-<time>-2.zip`), the one file a player attaches; the boxes of those ends say "attach
the newest file in logs that starts with report-" and name it (`port/platform/diag_host.c`
(`report_file`, `report_box`, `fatal_text`); `port/platform/window_host.c` (`device_lost_quit`);
`port/platform/main_host.c` (`open_window`)). It holds:

| entry | what |
| --- | --- |
| `report.txt` | what ended the run and when, the `VERSION.txt` lines (beside the program or one folder up; Android's from the app's assets), `achievements.toml`'s `stages` line (not the file), the [summary line](#the-summary-line), the log's latest device, robust access, blend, VU index, fog, shader and window lines, the last 20 `ico_pc:`, `window:` and `rd:` lines before the report, the report block itself (the crash or watchdog report with its symbol lines), and the window's own part (`IcoReportInputs.extra`, `ico_diag_set_report_extra`: SDL's own log, the last 64 lines) |
| `ico-pc.log` | the log so far |
| `ico-pc-previous.log` | the run before's |
| `startup-activity.txt` | when there is one |
| `config.toml`, `ico-pc.ini` | the settings |
| `loss-<stamp>.zip` | this run's [loss dump](RENDER.md#the-loss-dump), else the one the previous run left, stored as it is |

`port/platform/report.c` writes it without allocation: raw file calls (`open`/`read`/`write`/`lseek`,
the wide Win32 calls through a static buffer), a zip writer of its own (UTF-8 names, the sizes
patched into each local header) and miniz's `tdefl` deflating into a static compressor; the local
time from an offset noted at start, so no time zone lookup runs in a signal handler. The loss
dump's writer (`dump_zip.c`) is not used: miniz's zip writer allocates, which a crash inside
`malloc` would deadlock. `ico_report_prune` keeps the newest five: it sorts the `report-YYYYMMDD-HHMMSS[-k].zip` names
explicitly by stamp, then by `k` (the plain name is `k = 0`, so a same-second `-1` is newer; the
folder's order and byte order are not age), and removes the oldest. It lists the folder and so runs
only on the normal paths (at start, after a lost device's or a failed start's report)
(`port/platform/report.h`). The line `ico_pc: report: report-<stamp>.zip in the logs folder (...)`
names it in the log.

### The summary line

Once a start, after the device line, the self-tests and the window line, the window logs one line
with what a report usually asks:

```
ico_pc: summary: version vX.Y.Z (commit 1a2b3c4d); Windows x64; GPU NVIDIA GeForce GTX 1060 (device 10de:1c03; driver NVIDIA 566.36, version 566.36.0.0); Vulkan; Enhanced preset, resolution ..., aspect ..., framerate ...; blend exact (interlock) (self-test: ...); index values dense (marks in the stream); fog inplace (self-test: ...); VU shaders plain and extended; pipelines the start-up set; index values up to 4294967295; robust access off; controller Xbox Series X Controller (DirectInput scan on); disc SCES_507.60; user profile
```

(`port/platform/window_host.c` (`log_summary`); `port/render/rd_core.c` (`rd_summary_text`, from
the self-tests' results); `port/rhi/rhi.h` (`RhiLimits.driverText`, `robustText`)). The controller
is what SDL has connected then ("none yet" when the pad arrives later; its own `input: gamepad`
line follows), on Windows with whether SDL's DirectInput scan is on (`[dev] joystick_directinput`);
the VU shaders are `plain only (issue 107)` on a device or setting that keeps them to the plain
variants, and the pipelines `on first use (no start-up set)` where the set is skipped
([RENDER.md](RENDER.md#the-start-up-set)); on Android the disc is mounted after the window ("not mounted yet").
`ico_diag_set_summary` flushes the C library's held lines first, so on Windows the line follows the
device and window lines in the file, and keeps the text for the report file. The 10 s block's
`window: memory:` line has the graphics memory in use and its peak where the backend counts it
(Vulkan), and the process's (`port/platform/window_host.c` (`perf_log`)).

### Rate-limited lines

Every line that may repeat (a per-frame or per-event condition) goes through one helper,
`ico_log_limited(key, fmt, ...)` (`port/platform/diag_host.h`): the first 8 of a key are written,
then every 100th, the 8th saying `(8 of these so far; from here every 100th is written)` and a later
one `(number N of these)`; the hot path is one atomic increment and a compare, the arguments
evaluated and the line formatted only when it is written. At the end of a run, normal or fatal, one
line counts every key: `ico_pc: limited lines (key, count, written): rd: interp snap 412 (11
written); ...` (`port/platform/main_host.c` (`summary`, `fatal_summary`)).

## Hotkeys and dump zips

The window build's diagnostic keys are free of SDL so a unit test checks them
(`port/platform/hotkeys.h` (`ico_hotkey_for_mod`)):

- **F12**: an [.rddump](ARCHITECTURE.md#terms) of the last closed frame and a PNG of it
  (`frame-<time>-v<vsync>.rddump`, `.png`), and a PNG of the picture as presented
  (`frame-<time>-present.png`), in `<user folder>/dumps/`. The
  dump and a copy of the log go into one zip, and the loose dump is deleted
  (`port/platform/dump_zip.h` (`ico_frame_dump_zip`)). The
  [replay tool](ARCHITECTURE.md#terms) takes the zip directly (`ico_dump_zip_extract`).
- **Shift+F12** in photo mode: the aspect sweep, the paused frame rendered at 4:3, 16:9, 32:9,
  48:9 and 60:9 into `sweep-<time>.zip` (`ico_sweep_zip`).
- **F11**: stats lines every second for 30 s instead of every 10 s; again turns it off
  (`ico_stats_fast_toggle`).

Neither key can be bound, and a held key's repeats do nothing. `dump_every=N` in `ico-pc.ini`
writes every Nth frame as an `.rddump` (`port/platform/host_config.h`).

A device loss writes its own zip with no key: `logs/loss-<date>-<time>.zip`, the lost frame's dump
first, the frame before it, then the log, the newest three kept
([RENDER.md](RENDER.md#the-loss-dump); `port/platform/dump_zip.h` (`ico_loss_dump_zip`,
`ico_loss_dump_path`, `ico_loss_dump_prune`)). ctest `dump_zip` checks its entries and their order,
that the bisect's extraction takes the lost frame, and the pruning
(`port/platform/test/dump_zip_test.c` (`test_loss`)).

## Window lifecycle

When a mobile system moves the app, the window build runs one table of steps per event through
operations it supplies, so `port/platform/test/lifecycle_test.c` checks the order with fakes
(`port/platform/window_lifecycle.h` (`ico_lifecycle_on`)):

- will enter background: a line, the GPU idle, the surface released (the device kept), the
  watchdog paused, the sound paused, the log flushed;
- did enter foreground: the surface and swapchain made again, the watchdog running, the sound
  resumed with its queue emptied, the pacer's deadline reset to now, a line;
- terminating: a line, the settings saved when changed, the pad recording closed, the log flushed;
- low memory: the texture pack's read-ahead held, a line, the log flushed.

SDL3 delivers these only to an event watch, which `window_host.c` installs on Android and iOS
after the renderer starts, before the start-up set (`port/platform/window_host.c`
(`lifecycle_watch`)). Before the window loop is open (Android's start-up set) the table does not
run: the watch only pauses the watchdog in the background and runs it again in the foreground.

## Android graphics driver packages

The Android build can load a community Vulkan driver for Adreno GPUs (Turnip and others) through
libadrenotools instead of the phone's. A package is a flat zip of `meta.json`, the driver
library it names and that library's dependencies; installed packages live in folders under the
app's internal `drivers/` folder, the only place the system lets the app load a library from
(`port/platform/gpu_driver.h`). A trial marker names the driver a start is trying: written
before the driver opens, deleted after 300 presents or at a normal end, so a marker still there
at the next start means that start died inside the driver, which is then recorded as failed and
replaced by the phone's own driver with a message box
(`port/platform/android/gpu_driver_android.h` (`ico_gpu_driver_android_start`)). The package
handling is pure C and tested on every platform (`port/platform/test/gpu_driver_test.c`).

## Steam shortcut

Settings > Extras > Add to Steam writes a non-Steam shortcut for the program into the most recent
Steam user's `userdata/<id>/config/shortcuts.vdf`, inserting one entry before the end of the
`shortcuts` map so every existing entry keeps its bytes, after copying the file to
`shortcuts.vdf.bak`. Nothing is written while Steam runs, because Steam rewrites the file when it
exits (`port/platform/steam_shortcut.h`; `port/platform/steam_shortcut.c` (header comment)). It is
compiled into the Windows and Linux window builds only, not macOS, Android or iOS
(`CMakeLists.txt` (the window build section)).

## The null floor and the headless build

The game's render layer is compiled in both builds, so the state the draw phase changes (fades,
view matrices, the lightning's random reseeds, heap allocations) is the same with or without a
renderer; the headless build writes its packets and display lists as on the PS2, and the window
build sends the GS register writes to the decoder instead (`port/null/gfx_null.c` (header
comment); `ico2/seki/src/GifPacket.c` (`setGsReg`)). Under it,
`port/null/gfx_null.c` stubs Sony's libgraph and libdma: a DMA kick is a no-op and the GS is always
idle (`port/null/gfx_null.c` (header comment)). `port/null/libgcc_null.c` supplies `fptodp`, which
only feeds debug prints. `ico_port_null` links the host's devices (the disc,
[libpad](ARCHITECTURE.md#terms), libscf) into `ico_pc` and the device tests
(`port/null/CMakeLists.txt`).

The headless build (`ICO_HEADLESS=ON`) leaves out the window, rd, the
[RHI](ARCHITECTURE.md#terms) and SDL. It differs from the window build in its defaults, all from
`port/platform/main_host.c`'s header comment and `host_main`: vsyncs as fast as the CPU allows; a
trace by default; `pad-script.txt` beside the executable taken as the
[pad script](ARCHITECTURE.md#terms) when present, else no pad; no pad recording; `use_iso` on (the
disc image read in place); the clock fixed; no `config.toml` written unless `write_config=1`;
the user folder is the executable's folder. `--ticks N` (or `ticks=`) ends the run after N Main
ticks. In `CMakePresets.json` the `base` preset sets `ICO_HEADLESS=ON`, so `linux-x64` and the
presets that inherit it (`asan`, `fptrap`, the clang ones) are headless; `linux-arm64`, `win-x64`,
`android-arm64` and the Apple presets set it off. [TESTING.md](TESTING.md) covers the runs built
on it.

## Tests

`ctest -L platform` runs this layer's tests, together with those of `port/input`, `port/config` and
the top level (`cmake/IcoTests.cmake` (`_ico_test_area`)); [TESTING.md](TESTING.md#portplatformtest)
lists each program.
