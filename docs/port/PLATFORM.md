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
| `port/platform/diag_host.c`, `diag_host.h` | crash handlers, heartbeat, watchdog, milestones, float faults |
| `port/platform/trace_host.c`, `trace_host.h` | the Main tick count, the heartbeat's state line, the trace file |
| `port/platform/assert_host.c` | newlib's `__assert` as the game calls it |
| `port/platform/window_host.c`, `window_host.h` | the SDL3 window, the renderer device, the real-time pacing, the stats lines (window build) |
| `port/platform/window_video.c`, `window_video.h` | window mode requests and their readback |
| `port/platform/window_lifecycle.c`, `window_lifecycle.h` | the steps run when a mobile system moves the app |
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
writes the cause, the faulting address as module+offset (to be looked up in `ico_pc.map`, the link
map written beside the program), the current game thread and its last kernel call, return-address
candidates, the heartbeat line and every thread, then on Windows shows a message box and exits with
code 3 (`port/platform/diag_host.h`; `port/platform/CMakeLists.txt` (the link map)). Every
diagnostic line is written with one unbuffered OS call to its own handle on the log, so a crash
leaves everything before it (`port/platform/diag_host.c` (header comment)). On Android the handler
then puts the system's handlers back so the system still writes its tombstone
(`port/platform/diag_host.c` (`restore_handlers`)).

A crash while the renderer builds a pipeline is a graphics driver crash: the activity note
(`ico_diag_set_activity`) starts with "creating pipeline" or "creating the start-up pipelines". On
Windows, Linux and macOS the handler then starts the program again at once, with
`ICO_STARTUP_RETRY=<step>:<backend>` in the new run's environment; nothing is written to the user
folder or the settings. The steps come in order, each taken only when the run before crashed:
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
`ico_pc: the previous run ended while: <note>` and creates the pipelines as the frames need them;
another version's file is logged and the set tried again; a lazy run's clean exit leaves its own
line, logged as `ico_pc: the previous run created the pipelines as the frames needed them; this one
does too`, so the lazy start sticks until the version changes (`port/platform/diag_host.h`
(`ico_diag_startup_activity_begin`, `ico_diag_startup_activity_exit`)). Deleting the file makes the
next start try the set again. The driver compiles the set on worker threads
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
next. While a step runs the activity file's note says so (`replaying the lost frame draw by draw
(the loss bisect)`); a start after one ended inside a step logs it, is not lazy for it, and the
second level continues. An unfinished second level goes before a newer loss dump, which stays in
logs. ctest `diag` runs it with the step stubbed (`actbisect2`, `ICO_TEST_BISECT2`): all in one
start, a start killed at step 3 continued at step 4, a renderer that did not start, the 16-step and
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
`port/platform/main_host.c` (`start_watchdog`)). Milestone lines carry the wall time, vsyncs, Main
ticks and the current thread (`ico_diag_milestone`), and game threads are named by the function
`iosThreadCreate` runs (`ico_host_thread_func`). `ico_diag_float_fault` logs a NaN that reaches game
code the PS2 never fed one (`port/platform/diag_host.h` (`ico_diag_float_fault`)).

The window build logs a stats block every 10 s. A host step longer than `[dev] slow_step_ms`
(default 8, 0 off) logs a slow-step line ([DISPLAY.md](DISPLAY.md#pacing);
`port/platform/window_host.c` (`slow_step`); `port/platform/host_loop.h` (`IcoStepProfile`)).

**A lost graphics device.** When the backend reports the device gone (`rhi_device_lost`), the window
pump and the start-up screens' pump end the session through the normal quit path: one line
`window: the graphics device was lost; quitting`, then the renderer's
[pass ring](RENDER.md#the-pass-ring) (the last replays and the passes of the last three, with the
sequence numbers the checkpoint markers carry and each effect's name from `rd_perf_post_name`), a
flush of the log, the [loss dump](RENDER.md#the-loss-dump) (`logs/loss-<date>-<time>.zip`, logged as
`window: loss dump: frames N and M (the one before) written to <zip>`, and noted in the activity
file), and one message box: the card stopped responding, the game has to close, the save is kept,
"If the graphics card is overclocked, try its stock clocks first.", and, when the loss dump was
written, its file name in the logs folder and a request to attach it with the log; on a phone the
box asks to send the log and that file. The loss restart above comes after the loss dump, but a
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
(`port/platform/window_host.c` (`lifecycle_watch`)).

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
