# Diagnostics: the log, crash reports and the heap checker

A test build has to explain its own failures: the user runs it by
double-click, with no console and no debugger, and sends back the `logs/`
folder. This file describes what `ico_pc` writes to its log, how to turn a
crash address into a function, and the diagnostic builds a developer uses
to find a memory fault (`ICO_HEAP_ASAN`) or a float fault (the `fptrap`
preset). How to run the tests and read a trace is in `TESTING.md`.

## What the log contains

All of this is on in every build, with no flags; it lives in
`port/platform/diag_host.{h,c}`. The log is `logs/ico-pc.log` beside the
executable, rewritten each run (`--console` keeps output on the console
instead). Every diagnostics line is one unbuffered OS write, so a crash or a
kill keeps everything written before it. The C library's stdout and stderr
are buffered and flushed once per vsync; a crash, abort or watchdog report
writes out what they hold first, and a milestone flushes them before its own
line, so the log keeps its order. The heartbeat and the watchdog run on
their own OS thread: a game thread that spins without a kernel call blocks
the host loop but cannot stop them.

- **Boot milestones**, stamped `[wall s, vsync, tick, #thread name]`:
  - `boot starts`, `boot: file_Init`, `boot: iosInitialize`,
    `boot: iosInitialize done`;
  - each thread created (id, creator, priority, the function it runs), in
    full until the first Main tick and then for a limited number more;
  - `boot: idle and scheduler started`, `boot ran until every thread waits`;
  - `scheduler: vsync event set`, `idle: every thread started`,
    `first vsync done`;
  - `Main: past IosPadLock and IosStgMgrLock`,
    `Main: gsb_InitGSSystem done`, `first Main tick done`;
  - every `stage_no` change, and every change of `kanbanBoot.c`'s
    `bootStep` / `mcCheckStep`.

  The game-side hooks are port lines in `common/src/main.c`,
  `fumi/ios/thread.c` (`ico_host_thread_func`) and
  `common/src/kanbanBoot.c` (`ico_host_kanban_step`).
- **Heartbeat**, every 2 s of wall time: vsyncs and Main ticks,
  `stage_no`, `systemStatus[5..8]`, `fadeStatus`, `mpegPlay` /
  `mpegInitDone`, StageManager's free and wake flags, `game_pause`,
  `kanbanBootEnd`; the disc layer (reads, sectors, last LSN, busy, waiters,
  stream state, whether the cdvd thread is asleep, background reads); SIF
  (calls, the last server id and RPC number, `port/data/sif_host.c`); the
  thread on the CPU (id, name, priority), ready and waiting counts, context
  switches. When the line repeats unchanged it ends `no progress for N s`.
- **Crash report** (exit code 3). It covers access violations, illegal
  instructions (`__builtin_trap`, `ICO_BREAK`), divide errors, stack
  overflow, float traps and `abort()` (`ico_assert`, the game's
  `debug_assert*` and the scheduler's fatal errors all set a "last failure
  message" first). It contains:
  - the exception or signal and the faulting PC as `module+offset`;
  - the faulting data address and whether it was a read or a write;
  - the game thread at the time, with its last kernel call (call, argument,
    caller `module+offset`, vsync; every kernel call in
    `port/platform/kernel_host.c` records itself, and the null graphics
    layer's `sceGsSyncV` does too);
  - up to 24 return-address candidates from the stack;
  - the last failure message, a heartbeat line and every thread;
  - then the summary (ticks, vsyncs, stage).

  On Windows the report is written by the diagnostics thread, so a stack
  overflow does not format it on the overflowed stack. A message box names
  the log, and Windows' own crash dialog is suppressed.
- **Watchdog** (exit code 4). It fires when no Main tick has come
  `watchdog=` seconds after boot started (default 30; `[dev] watchdog` in
  config.toml), or no new one for twice that. It samples the main thread's
  PC and stack (`SuspendThread` / `GetThreadContext` on Windows, a signal on
  POSIX), so a spin shows where it is, dumps every thread (state, what it
  waits on: sleep, a semaphore with its count and waiters, or the vsync it
  busy-waits for; last kernel call and caller), writes the summary, shows a
  message box and exits. `watchdog=0` turns it off.
- **Exit line** on every normal end: `exit: ticks= reached`,
  `the game called Exit(n)`, `host: the game's main returned`, or
  `exit() from the game or the C library`. The summary follows it.
- **Trace**: when tracing is on, the header and every tick line are flushed
  as they are written (`port/platform/trace_host.c`; `TESTING.md`).

Thread functions without a name print as `fn ico_pc+0x...`. Those are the
static ones in `fumi/ios/thread.c` (the destroy manager, priority 13),
`fumi/ios/message.c` (the vsync event thread, 11), `fumi/ios/cdvd.c` (the
stream manager, 27), `fumi/ios/pad.c` (the pad device manager), and the
game's per-object threads.

## Turning an offset into a function

Every build writes a link map beside the executable
(`LINKER:-Map=ico_pc.map`, `port/platform/CMakeLists.txt`), and the packages
ship it: `ico_pc_x64.map` in the Windows package (`tools/package_win.sh`),
`ico_pc.map` in the Linux one (`tools/package_linux.sh`). The executables are
RelWithDebInfo with DWARF line tables, so the same executable also answers
with `addr2line`:

```
tools/toolchain/mingw-gcc/usr/bin/x86_64-w64-mingw32-addr2line -f -i -e ico_pc.exe 0x<0x140000000 + offset>
addr2line -f -i -e ico_pc 0x<offset>
```

`__image_base__` in the Windows map gives the base (0x140000000). On Linux
the offsets are already file addresses. Keep the exact executable the report
came from; a rebuild moves every function.

## Reproducing a failure

- `ticks=N` (ini) or `--ticks N` stops a run after N Main ticks.
- `start_stage=N` (`[dev] start_stage`, developer key) boots Main straight
  into stage N through `debug_TryToGetStartStage` (`ICO_START_STAGE`); it
  applies `[video] video_mode` as `kanbanBoot.c`'s step 200 would, so a
  developer boot runs at the user's 50 or 60 Hz. An idle boot of every stage
  with data under the `fptrap` and heap-ASan builds below, at both rates, is
  the way to sweep code the boot script never reaches.
- A user's pad recording (`logs/input-<time>.txt`) replays in the headless
  build as a pad script (`pad_script=`); with `trace=1` on both sides the
  two traces can be compared tick by tick (`TESTING.md`).

## ICO_HEAP_ASAN

AddressSanitizer alone does not see an overrun inside the game heap: every
game allocation comes out of the arena (`port/platform/arena.c`), one
`mmap`. `fumi/ios/memory.c` therefore has a debug mode, off unless
`ICO_HEAP_ASAN` is defined, that needs `-fsanitize=address` (it stops the
build with `#error` otherwise). The allocator itself is unchanged (same
partitions, offsets, free lists and `ICO_HEAP_STATS` accounting). Each
public function (`iosMallocDebug`, `iosFree`, `iosMallocAlignDebug`,
`iosReallocDebug`, the partition functions, the leak checks) becomes a
wrapper that unpoisons the root partition, runs the original, then walks
every partition's node list and poisons:

- every block header (0x50 bytes on the host);
- the slack between a block's requested size and its 16-byte-rounded end
  (the requested size is kept in a side table);
- free areas' bodies, only with `ICO_HEAP_ASAN_FREE=1` in the environment.
  The EE code writes into free memory in at least one place
  (`seki/src/Packet.c`'s line-list end mark, `p[src->lineCount].attr.b.type
  = 0` after `p` has already been advanced, which lands past the block on
  the PS2 too), so this is off by default.

The first write into a header then stops with the writer's stack. To build
and run it:

```
cmake --preset asan -B build-host/heap-asan -DICO_LINK_EXE=ON \
  -DICO_SANITIZE=address \
  "-DCMAKE_C_FLAGS=-DICO_HEAP_ASAN=1 --param=asan-instrument-reads=0"
cmake --build build-host/heap-asan --target ico_pc
cd build-host/heap-asan    # ico-pc.ini and a pad script as in TESTING.md
printf 'interceptor_via_fun:sceSifSetDma\n' > asan.supp
ASAN_OPTIONS=detect_leaks=0:fast_unwind_on_fatal=1:suppressions=$PWD/asan.supp ./ico_pc
```

Why each option:

- `ICO_SANITIZE=address` without `undefined`: the `asan` preset's UBSan
  stops at `fumi/isys/gobj.c`'s pointer arithmetic on a null table before
  the first object exists, which the EE does too.
- `--param=asan-instrument-reads=0`: only writes are checked. The EE code
  reads past the end of records on purpose in many places (16-byte VU0 loads
  of 12-byte vectors, `omori/src/gv.c` on `camera-ico2.c`'s
  `targetASmooth`). Corruption is always a write.
- `fast_unwind_on_fatal=1`: the default slow unwinder faults on the fiber
  stacks (minicoro); the fast one stops at `_mco_main`, which is enough.
- the `sceSifSetDma` suppression: `soundBDDataSet` rounds a sound bank up to
  64 bytes and DMAs that many, reading up to 63 bytes past the block, as the
  EE does.

`ICO_HEAP_STATS` (a CMake option of `port/platform`) is the companion: it
reports each allocation and free to `arena.c`, which logs every 64 KB of
new high-water mark per partition. It is how the partition sizes in
`PLATFORM.md`, "Heap", were chosen.

## The fptrap preset

`cmake --preset fptrap` builds with `ICO_FPTRAP`: the simulation's float
mode unmasks divide-by-zero and invalid, so the first IEEE Inf or NaN that
the PS2 would not have produced stops the run with a crash report at the
operation. Each site found this way is converted to the PS2's result
(`ps2_div`, `ps2_ftoi`, `ps2_operand`; `DIVERGENCES.md` F5).
