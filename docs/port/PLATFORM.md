# Platform layer: threads, vsync, heap

The game was written for the PS2's EE kernel: preemptive-priority threads on
one core, a vblank interrupt that drives everything, and 32 MB of RAM
carved into partitions at fixed addresses. The port keeps all three as the
game expects them. The game's kernel threads run as cooperative fibers on
one host thread under the EE kernel's scheduling rules; the host raises the
game's vsync in simulated time; and the game heap lives in a host arena that
stands in for EE RAM. The unit tests in `port/platform/test/` check each
part.

| file | what |
| --- | --- |
| `port/platform/fiber.c`, `fiber.h` | context switch: minicoro, 256 KB stacks, guard page |
| `port/platform/sched.c`, `sched.h` | the EE thread and semaphore model; `ico_sched_call_on_host` |
| `port/platform/kernel_host.c`, `kernel_host.h` | the `eekernel.h` calls, the INTC table |
| `port/platform/host_loop.c`, `host_loop.h` | `ico_host_init`, `ico_host_step` |
| `port/platform/vsync_hooks.c` | `ico_host_on_vsync_register` (the disc completion, the clock) |
| `port/platform/clock.c`, `clock.h` | the wall clock behind `sceCdReadClock` and the EE timer counters, stepped from a vsync hook (CONFIG.md) |
| `port/platform/hwregs.c` | the EE I/O and GS register pages as plain memory; the timer counters in the EE page are advanced by `clock.c` |
| `port/platform/fpenv.c`, `fpenv.h` | the simulation's float mode and the host's |
| `port/platform/host_config.c`, `.h` | exe folder, per-user folder (`SDL_GetPrefPath` in the window build), `ico-pc.ini`, the `config.toml` reader and writer (CONFIG.md) |
| `port/platform/host_fs.c`, `host_fs.h` | file calls on UTF-8 paths (DATA.md, "Paths") |
| `port/platform/arena.c`, `arena.h` | the EE RAM arena, heap statistics |
| `port/platform/diag_host.c`, `.h`, `trace_host.c`, `.h` | the log's diagnostics (BOOT_DIAG.md) and the per-tick trace (TESTING.md) |
| `port/platform/main_host.c`, `window_host.c` | the program's `main`, the window and the frame loop |
| `port/third_party/minicoro/` | minicoro v0.2.0, vendored |
| `common/src/main.c` | `ico_vsync`, the idle loop's busy wait |
| `fumi/ios/message.c` | the vblank handler reads the host-set `GS_CSR` |
| `fumi/ios/memory.c` | `ICO_BREAK()` for `break`, `ICO_HEAP_STATS` and `ICO_HEAP_ASAN` hooks |
| `fumi/ios/ios.c` | the root partition inside the arena, the partitions' host sizes |

## Threads

Every thread the game creates (`CreateThread`, through `fumi/ios/thread.c`'s
`iosThreadCreate`) is a fiber. The game's main runs on the boot fiber
(thread id 1). `ico_sched_run` is the only resumer: a thread that must give
up the CPU yields to it, and it resumes the highest-priority ready thread.
Threads switch only inside kernel calls, so no game code runs concurrently
with other game code, exactly as on the single-core EE. That is the reason
for fibers rather than OS threads: the game's code assumes it is never
interrupted between kernel calls, and its order of events (which thread runs
after a wakeup, a signal or a vsync) is part of its logic. A cooperative
scheduler that follows the EE's rules reproduces that order exactly, and a
run is a function of its input and the vsync count only.

The game's own stack buffers (`mainThreadStack`, the `iosThreadCreateS`
heap stacks, `0x1800` for actor processes and so on) are still allocated
and stamped with `<THREAD_SP>` marks, so heap use is the game's; fibers run
on their own host stacks.

### Rules and where they come from

The EE kernel is not public. Each rule below is taken from Play!'s EE kernel
HLE, `Source/ee/PS2OS.cpp` (https://github.com/jpd002/Play-, BSD-2-Clause,
commit `83700b2c`, read for its semantics only; no code copied), from the
decompiled libkernl in this tree (`sce/libkernl/`), or from the game's use.

| rule | source |
| --- | --- |
| strict priority 0-127, lower number first; no time slice | Play! `ThreadShakeAndBake` picks the schedule's head |
| ready queue per priority, FIFO: a thread made ready goes behind its peers | Play! `LinkThread` (inserts before the first thread of a lower priority) |
| the running thread stays in its queue, so a preempted thread resumes before its peers | Play! (running threads stay linked; `SleepThread` and `WaitSema` unlink) |
| `StartThread`, `WakeupThread`, `SignalSema`, `ResumeThread`, `ChangeThreadPriority` switch at once when a higher priority becomes ready | Play! `sc_StartThread`, `sc_WakeupThread`, ... call `ThreadShakeAndBake` |
| the `i`-variants (`iWakeupThread`, `iSignalSema`) never switch; the switch happens on return from the interrupt | Play! (`isInt` skips `ThreadShakeAndBake`) |
| `ChangeThreadPriority` of a ready thread moves it to the tail of the new priority's queue, even when the priority is unchanged; returns the old priority | Play! `sc_ChangeThreadPriority` (`UnlinkThread` + `LinkThread`) |
| `RotateThreadReadyQueue(p)` moves the head of queue `p` to its tail | Play! `sc_RotateThreadReadyQueue` |
| `SleepThread` consumes a pending wakeup without waiting; `WakeupThread` of a thread that is not sleeping counts up; `CancelWakeupThread` returns the count and clears it | Play! `sc_SleepThread`, `sc_WakeupThread`, `sc_CancelWakeupThread` |
| `WakeupThread` of itself, of id 0 or of a dormant thread fails (-1) | Play! `sc_WakeupThread` |
| semaphore waiters are released in the order they waited, whatever their priority | Play! `SemaLinkThread` (appends), `SemaReleaseSingleThread` (takes the first) |
| `SignalSema` with no waiter adds one to the count | Play! `sc_SignalSema` |
| `DeleteSema` releases every waiter; their `WaitSema` returns -1 | Play! `sc_DeleteSema`, `SemaReleaseSingleThread(..., true)` |
| returning from a thread's entry function is `ExitThread`; an exited or terminated thread is dormant and `StartThread` begins it again at its entry with its initial priority | Play! (thread epilogue, `ThreadReset`) |
| `TerminateThread` and `DeleteThread` of the caller fail; `DeleteThread` needs a dormant thread | Play! `sc_TerminateThread`, `sc_DeleteThread` |
| `ReferThreadStatus(0, ...)` is the caller; a deleted thread returns 0 | Play! `sc_ReferThreadStatus` |
| `VSync` and libgraph's `sceGsSyncV` busy-wait for the vblank (lower priorities do not run meanwhile) | `sce/libkernl/glue.c` (`VSync` polls `INTC_STAT`), `sce/libgraph/graph011.c` |
| the main thread runs at priority 1 when `main` starts | `sce/libkernl/thread.c` `InitThread` (`ChangeThreadPriority(GetThreadId(), 1)`), called by `_InitSys` from `crt0` |

The game's own conventions follow from these rules, and `ios_chain_test`
checks them with the real `thread.c`, `message.c` and `memory.c`:

- An actor process (priority 0x13-0x1A) woken by Main (0x1B) through
  `iosThreadWakeup` runs at once, and Main continues when it sleeps
  (`fumi/isys/obj_manager.c`, `_iosOmMain`).
- A process whose body returns is not exited: `iosThreadMain`
  (`fumi/ios/thread.c`) lowers it to 0x21 or 0x22, below the idle thread
  (0x20, always ready), so it never runs again; `_iosOmMain` sees 0x22 and
  has thread.c's destroy manager (priority 13) terminate and delete it.
- The idle thread (`main.c`, `idle`) busy-loops at 0x20. On the host its
  loop calls `ico_sched_spin_vsync()` each pass: it stays ready (so 0x21 and
  0x22 stay parked) and hands control back to the host until the next
  vsync.

### Cases where the EE's behaviour is not known

Play! is an emulator's reimplementation, not the kernel. Where it is silent
or says it differs from the hardware, the choice is recorded here.

1. **`SignalSema` at `maxCount`.** Play! has `//TODO: Check maximum value`
   and counts up without a limit; so does this port. In the game it matters
   only for `IosMcLock` (max 1), which the scheduler signals every vsync
   while the memory-card thread polls `sceMcSync`; that thread runs every
   vsync here, so the count never passes 1.
2. **Semaphore wait order.** FIFO, as Play!; the `attr` field is stored and
   ignored. The EE kernel may offer priority order through `attr`; the game
   creates its semaphores with `attr = 1` and, as far as the code shows,
   never has more than one waiter on a queue or lock.
3. **`SuspendThread` of the caller.** Play! refuses it with the comment
   "This actually works on a real PS2"; this port allows it (the thread
   stops until resumed). The game's `iosThreadSuspend` has no callers.
4. **Thread and semaphore ids.** Allocated lowest free from 1; the EE's
   policy is unknown, and libkernl's `InitThread` takes one thread and one
   semaphore before `main`, which this port does not create, so ids are one
   lower than the EE's. The game uses ids only as table indexes
   (`iosThreadTable`, `msgQueueTable`) and in debug output.
5. **`iWakeupThread` of the interrupted thread.** libkernl posts that case
   to its kernel event thread, which calls `WakeupThread` from priority 0
   (`sce/libkernl/thread.c`). Interrupts here only arrive while the
   interrupted thread busy-waits (it is never sleeping), and the posted
   `WakeupThread` would only count up, which is what the direct call does.
6. **`GetThreadId` from interrupt code** returns the thread that ran last
   (the EE's `iGetThreadId` returns the interrupted thread).
7. **INTC handler chains.** Handlers run in `AddIntcHandler` order (`next ==
   0` puts one first) and their return values are ignored. The game has one
   handler per cause.
8. **When interrupts arrive.** The vblank (and the disc completions) arrive
   only when every thread waits or busy-waits, never in the middle of a
   frame. On the PS2 a frame that ran longer than a vsync was interrupted
   and the frame step slipped; here no frame overruns. Game code that
   busy-waits on a flag set by an interrupt or another thread, without a
   kernel call, would hang a fiber; the only such loops are in the PS2 movie
   player (`ito/mpeg`, not compiled on the host) and `FileManager.c`'s IOP
   reboot loops (left out on the host, DATA.md).

Other kernel calls: `SetAlarm` returns -1 (only the PS2 movie player used
alarms); DMAC handlers are recorded but never raised; `scePrintf` prints
only when the `ICO_TTY` environment variable is set (the retail game's TTY
output went to the development kit).

## Vsync and the host loop

```
ico_host_step()                                  port/platform/host_loop.c
  ico_sched_vsync_advance()      busy waits for this vsync may continue
  ico_vsync(field)               common/src/main.c: GS_CSR.FIELD = field,
                                 then INTC 2 (vblank start), as interrupt code:
    signal_handler(2)            fumi/ios/message.c: odd_even = FIELD ^ 1,
                                 iWakeupThread(event thread)
  on-vsync callbacks             vsync_hooks.c (disc completion, clock)
  ico_sched_run()                the threads run until all wait again:
    event thread (pri 11)        msgSend -> SignalSema(scheduler queue)
    scheduler() (pri 0xF)        main.c, unchanged: frame step, wakes Main,
                                 sound, cdvd, stage manager, signals IosMcLock
    sound (0x10), Main (0x1B) and the others, idle (0x20) busy-waits: return
```

The field parity starts at 0 and alternates every vsync. `ico_host_step`
advances simulated time by 20 ms (PAL, `systemStatus[0] != 0`) or 16.683 ms;
the window build paces the steps in real time, the headless build runs them
as fast as it can.

`scheduler()` stays a game thread. `ico_vsync` raises the vblank interrupt
instead of running the scheduler's body on the host. That keeps the PS2's
order exactly (event thread at 11, then the scheduler at 0xF, then what it
woke, each preempting as the kernel rules say), keeps the `iosMsgSetEvent`
heap allocation (16,576 bytes in the `event` partition) and the thread and
semaphore it creates, and lets anything in the scheduler's body block as it
would have. `AddIntcHandler` on the host fills `kernel_host.c`'s table,
which `ico_vsync` raises.

At boot, `scheduler()` first calls `sceGsSyncV(0)`, a busy wait; the idle
thread (lower priority) does not start the other threads until the first
simulated vsync releases it, as on the PS2. `ico_host_init` returns at that
point.

### EE timers

`T0_COUNT`..`T3_COUNT` (`port/compat/eeregs.h`) advance with simulated time:
`clock.c` steps them from a vsync hook, by the vsync period times the rate
the mode word's `CLKS` field selects, while `CUE` is set; 16 bits, the
overflow flag set on a wrap (CONFIG.md, "EE timers"). Values depend on the
vsync count only, so runs stay deterministic. The game reads them only for
its profiler displays (`debug.c`, `fieldCollision.c`).

## Fibers

minicoro v0.2.0 (`minicoro.h` header, 2023-11-15; repository commit
`02dad0f8`, 2024-12-07), Unlicense or MIT-0
(`port/third_party/minicoro/LICENSE`; THIRD_PARTY.md). Unmodified. Every
preset uses minicoro's assembly switch, with the port's own guard page one
page below each stack.

minicoro allocates a coroutine as one block: its record, the switch
context, a storage area, then the stack growing down towards them. `fiber.c`
makes the storage area exactly one page, places the block so the stack
starts on a page boundary, and makes that page no-access (`mprotect` /
`VirtualProtect`), so an overflow faults instead of overwriting the record
(`fiber_guard` test). Under ASan minicoro annotates every switch
(`__sanitizer_start_switch_fiber`); destroyed stacks are unpoisoned before
they are unmapped.

### FP mode

The simulation runs in its own float mode (round toward zero, denormals
flushed: the PS2's, MATH.md), the host's code in the normal one (round to
nearest, denormals kept). The assembly switch saves neither MXCSR nor the
x87 control word, so all fibers share the host thread's mode, and host code
that runs on the same OS thread (SDL, the audio push, the GPU driver) may
change it. So:

- the scheduler runs a start hook at the top of every fiber; `host_loop.c`
  sets it to `ico_fpenv_sim_enter`, the mode `ico_host_init` set on the host
  thread (minicoro's Windows-fiber path would start each fiber from the
  default mode, `FIBER_FLAG_FLOAT_SWITCH`; the hook covers that backend
  too);
- `ico_host_step` re-asserts `ico_fpenv_sim_enter()` at its top and again
  after `ico_audio_host_vsync` (an SDL call) before the threads run;
- `main_host.c` puts the host mode (`ico_fpenv_host_enter`) around the
  window work after each step: event pump, the Settings apply, `rd_Present`
  and the pacing. The trace poll stays in the simulation's mode, so the
  trace's printed floats do not depend on the window;
- `ico_sched_call_on_host` (below) runs its function in the host mode and
  puts the simulation's mode back before the fiber resumes.

### Fiber stacks and host calls

Fiber stacks are 256 KB (`ICO_FIBER_STACK_SIZE`, guard page below). The
game's own code fits; a GPU driver may not: the frame is replayed and
presented from `dl_Swap` (`ico2/seki/src/DisplayList.c`), on the game
thread that calls it, and pipeline creation inside a driver can recurse
deeply. Rather than give every drawing thread a large stack, the driver work
moves onto the host stack:

- `ico_sched_call_on_host(fn, arg)` (`sched.h`): from a fiber, the thread
  records the call and yields; `ico_sched_run`, on the host thread's own
  stack, makes the call (host FP mode) and resumes the same thread at once,
  so no other thread runs in between and the game sees a plain synchronous
  call. From the host context it calls `fn` directly.
- rd takes the hook (`rd_SetHostCall`, `rd.h`); the window build installs
  `ico_sched_call_on_host` after `rd_Init`. Through it go `rd_EndFrame`'s
  replay and present, `rd_BeginFrame`'s target re-creation after a Settings
  change, and the FMV picture's present (`rd_video.c`). Tests and tools
  leave the hook unset and call directly.
- The replay is not deferred: it still happens at `dl_Swap`, with the game
  state of that moment; only the stack and the FP mode change. Deferring it
  to after `ico_sched_run` would be wrong, because the frame's textures can
  change between `dl_Swap` and the end of the step.
- `rd_PrecreatePipelines` (window build, after `rd_Init`) creates the whole
  reachable pipeline set at start-up and logs the count and the time, so no
  frame waits on a pipeline compile.

### Device loss

A removed or reset GPU device (`DXGI_ERROR_DEVICE_REMOVED`/`RESET`/`HUNG`,
`VK_ERROR_DEVICE_LOST`) is logged once with the removal reason and marks the
device lost (`rhi_DeviceLost`); the next `ico_window_pump` shows one message
box and returns 0, and the program exits through its normal path instead of
leaving a frozen window (`port/rhi/d3d12/README.md`).

## Heap

On the PS2 `ios.c` carves the heap from physical addresses:
`iosMallocInitPartition(0x760000, 0x1FEFFF0)`. On the host,
`port/platform/arena.c` allocates the EE's 32 MB plus 8 MB of headroom once,
zero filled, 1 MB aligned, wherever the OS maps it, and
`ico_arena_ee_addr(ee)` turns an EE address into the matching host address,
so the root partition starts at the same offset into the arena as on the
EE. The arena exists so that the game's allocator (`fumi/ios/memory.c`)
runs unchanged, with its partitions, free lists, best-fit reuse and leak
checks, and so that a 32-bit word can still name any heap address: words in
disc records hold arena offsets (`ICO_EEWORD`, `eeword.h`; LOADERS.md).
Nothing on the host depends on the arena's address or size:
`ICO_PHYS` and the uncached aliases are the identity (`typedef.h`), and the
heap-ASan ranges come from the partitions.

- The 1 MB alignment keeps every address's residue modulo any power of two
  up to 1 MB equal to the EE's, which `iosMallocAlignDebug` depends on.
- memory.c's four `__asm__("break")` are `ICO_BREAK()` (a trap on the host,
  the `break` it was on the EE). Its `& 0xFFFFFFF0` are 16-byte rounding,
  correct on every host.
- On the host the block header is 0x50 bytes (the EE's 0x40) and the
  partition record 0x70 (0x50), derived from `sizeof` with pointer-wide
  address arithmetic (`IosMemAddr`; LAYOUT.md). Every block therefore
  costs 16 bytes more, and records that hold pointers are wider.
- With `ICO_HEAP_STATS` memory.c reports each allocation and free to
  `arena.c`, which logs every 64 KB of new high-water mark per partition on
  stderr; `ICO_HEAP_ASAN` poisons headers and slack (BOOT_DIAG.md).

### Partitions

`iosInitialize` carves eleven partitions from the end of the root down, in
this order, so the stage partition, the last, keeps the EE's start. The
host's root ends `ICO_HOST_HEAP_EXTRA` (6.25 MB) past the EE's 0x1FEFFF0, in
the arena's headroom, and four partitions get that extra through
`ICO_PART_SIZE` (`fumi/ios/ios.c`): at the EE's sizes the plaza (stage 16)
runs out of the stage partition in `InitIcoMisc` with 5,335 blocks live,
idle boots of stages 12, 17, 23 and 42 come within 300 KB of its end, and
`common` and `stat mot` peak at 98% and 94% (DIVERGENCES.md D12). The stage
partition gets 4 MB, and the others the same share of their own size,
rounded up to 64 KB.

| partition | EE size (bytes) | host extra | EE address |
| --- | --- | --- | --- |
| `common` | 4,227,072 | +0x120000 | 0x1BE7F60 |
| `stat mot` (`ios_partition_smotion`) | 1,179,648 | +0x50000 | 0x1AC7ED0 |
| `demo mot` (`ios_partition_s2motion`) | 3,145,728 | +0xD0000 | 0x17C7E40 |
| `event` | 262,144 | | 0x1787DB0 |
| `oomori` | 327,680 | | 0x1737D20 |
| `horagai` | 1 | | 0x1737C80 |
| `sound` | 32,768 | | 0x172FBF0 |
| `sound_semi` (no name) | 20,480 | | 0x172AB60 |
| `shock` | 10,240 | | 0x17282D0 |
| `hara` | 1 | | 0x1728230 |
| `stage` (`isys`, `seki`, `sugipon`, `dmotion`) | 15,826,944 | +0x400000 | 0x08101A0 |

The EE addresses are worked out by hand from memory.c's arithmetic with the
EE's record sizes; `memory_test` checks them with the same function
(`partition_layout`) it uses to predict the host's offsets from the host's
sizes. On the host every partition but the stage's sits higher.

## Records shared with game code

`struct ThreadParam` and `struct SemaParam` (`port/compat/eekernel.h`) are
written by game units, compiled with `-mno-ms-bitfields` on Windows, and
read by `kernel_host.c`, compiled with the platform ABI
(`docs/research/compiler-semantics.md`, `cmake/IcoFlags.cmake`). Both hold
only `int`, `unsigned int` and pointer fields, so their layout does not
depend on that option.

## Tests

| test | checks |
| --- | --- |
| `sched` | preemption on wakeup, FIFO within a priority, a preempted thread keeps its place, wakeup counts and `CancelWakeupThread`, semaphore FIFO release and counts, `DeleteSema`, exit / restart / terminate / delete, the 0x22 finished-process convention, `ChangeThreadPriority` and `RotateThreadReadyQueue`, `iWakeupThread` from the vblank handler, busy waits, suspend/resume, the boot thread, `ico_sched_call_on_host` (host stack, FP mode, same thread after) |
| `fiber` | 64 fibers switched round-robin with stack contents checked, 200 KB of stack use, destroying suspended fibers, FP mode inside a fiber; under `asan`, the sanitizer's fiber annotations |
| `fiber_guard` | a stack overflow in a fiber faults on the guard page (Linux) |
| `arena` | allocated once, aligned, zero filled, EE address mapping, heap statistics |
| `memory` | the game's allocator in the arena at the host's partition offsets, and the EE's addresses from the same arithmetic with the EE's sizes; allocation, best-fit reuse, aligned allocation, realloc and full coalescing |
| `ios_chain` | thread.c, message.c and memory.c on the scheduler: vsync, vblank handler, event thread, a scheduler loop like main.c's, Main every second vsync, an actor process at 0x13 that ends at 0x22 and is destroyed |
| `diag` | the crash handler and the log lines (BOOT_DIAG.md) |
| `hotkeys` | the window build's hot keys |
| `host_fs` | `host_fs.h` on a UTF-8 path with Latin, Greek and Japanese characters: mkdir, write, rename over, kind and size, remove, rmdir |
| `host_config` | the ini, SHA-1 and path helpers (built from `port/null/CMakeLists.txt`) |
| `config` | config.toml reader and writer (round trip, atomic save), ini over toml precedence, `sceScfGetLanguage`, the BCD clock, the EE timers (`port/config/test`) |
