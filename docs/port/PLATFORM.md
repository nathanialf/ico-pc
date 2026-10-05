# Platform layer: threads, vsync, heap

Package 1B. The game's PS2 kernel threads run as cooperative fibers on one
host thread under the EE kernel's scheduling rules; the host drives the
game's vsync in simulated time; the game heap lives in a host arena that
stands in for EE RAM. Nothing here runs the game yet: everything is checked
by unit tests (`port/platform/test/`).

| file | what |
| --- | --- |
| `port/platform/fiber.c`, `fiber.h` | context switch: minicoro, 256 KB stacks, guard page |
| `port/platform/sched.c`, `sched.h` | the EE thread and semaphore model |
| `port/platform/kernel_host.c`, `kernel_host.h` | the `eekernel.h` calls, the INTC table |
| `port/platform/host_loop.c`, `host_loop.h` | `ico_host_init`, `ico_host_step` |
| `port/platform/vsync_hooks.c` | `ico_host_on_vsync_register` (package 1C's disc completion) |
| `port/platform/arena.c`, `arena.h` | the EE RAM arena, heap statistics |
| `port/null/gfx_null.c` | the headless graphics seam |
| `port/third_party/minicoro/` | minicoro v0.2.0, vendored |
| `common/src/main.c` | `ico_vsync`, the idle loop's busy wait, the headless draw seam |
| `fumi/ios/message.c` | the vblank handler reads the host-set `GS_CSR` |
| `fumi/ios/memory.c` | `ICO_BREAK()` for `break`, `ICO_HEAP_STATS` hooks |
| `fumi/ios/ios.c` | the root partition inside the arena |

## Threads

Every thread the game creates (`CreateThread`, through `fumi/ios/thread.c`'s
`iosThreadCreate`) is a fiber. The game's main runs on the boot fiber
(thread id 1). `ico_sched_run` is the only resumer: a thread that must give
up the CPU yields to it, and it resumes the highest-priority ready thread.
Threads switch only inside kernel calls, so no game code runs concurrently
with other game code, exactly as on the single-core EE.

The game's own stack buffers (`mainThreadStack`, the `iosThreadCreateS`
heap stacks, `0x1800` for actor processes and so on) are still allocated
and stamped with `<THREAD_SP>` marks, so heap use is unchanged; fibers run
on their own 256 KB host stacks.

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

The game's own conventions then follow from these rules, and
`ios_chain_test` checks them with the real `thread.c`, `message.c` and
`memory.c`:

- An actor process (priority 0x13-0x1A) woken by Main (0x1B) through
  `iosThreadWakeup` runs at once and Main continues when it sleeps
  (`fumi/isys/obj_manager.c`, `_iosOmMain`).
- A process whose body returns is not exited: `iosThreadMain`
  (`fumi/ios/thread.c`) lowers it to 0x21 or 0x22, below the idle thread
  (0x20, always ready), so it never runs again; `_iosOmMain` sees 0x22 and
  has thread.c's destroy manager (priority 13) terminate and delete it.
- The idle thread (`main.c`, `idle`) busy-loops at 0x20. On the host its
  loop calls `ico_sched_spin_vsync()` each pass: it stays ready (so 0x21 and
  0x22 stay parked) and hands the host control back until the next vsync.

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
   creates its semaphores with `attr = 1` and never relies on the order with
   more than one waiter as far as the code shows (each queue or lock has one
   consumer).
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
8. **When interrupts arrive.** The vblank (and package 1C's disc
   completions) arrive only when every thread waits or busy-waits, never in
   the middle of a frame. On the PS2 a frame that ran longer than a vsync
   was interrupted and the frame step slipped; here no frame overruns (the
   plan's "no slowdown" risk). Game code that busy-waits on a flag set by an
   interrupt or another thread, without a kernel call, would hang a fiber.
   A grep of the tree (`while (...) {}` and loops on `systemStatus`, `mpeg*`,
   `iosCdvd*`) finds such loops only in the movie player (`ito/mpeg`, IPU
   and SIF DMA polls) and `seki/src/FileManager.c`'s IOP reboot loops,
   which their owners replace (1C, Phase 4).

## Vsync and the host loop

```
ico_host_step()                                  port/platform/host_loop.c
  ico_sched_vsync_advance()      busy waits for this vsync may continue
  ico_vsync(field)               common/src/main.c: GS_CSR.FIELD = field,
                                 then INTC 2 (vblank start), as interrupt code:
    signal_handler(2)            fumi/ios/message.c: odd_even = FIELD ^ 1,
                                 iWakeupThread(event thread)
  on-vsync callbacks             vsync_hooks.c (1C: disc completion)
  ico_sched_run()                the threads run until all wait again:
    event thread (pri 11)        msgSend -> SignalSema(scheduler queue)
    scheduler() (pri 0xF)        main.c, unchanged: frame step, wakes Main,
                                 sound, cdvd, stage manager, signals IosMcLock
    sound (0x10), Main (0x1B) and the others, idle (0x20) busy-waits: return
```

The field parity starts at 0 and alternates every vsync. `ico_host_step`
advances simulated time by 20 ms (PAL, `systemStatus[0] != 0`) or 16.683
ms; nothing paces it in real time.

`scheduler()` stays a thread. The brief asked for it to become callable as
`ico_vsync(field_parity)`; `ico_vsync` is the host's entry, but it raises
the vblank interrupt instead of running the scheduler's body on the host
context. That keeps the PS2's order exactly (event thread at 11, then the
scheduler at 0xF, then what it woke, each preempting as the kernel rules
say), keeps the `iosMsgSetEvent` heap allocation (16576 bytes in the
`event` partition) and the thread and semaphore it creates, and lets
anything in the scheduler's body block as it would have (nothing does today:
`iosCdvdDiskStatusGet` and `la_playtime_count` read and bump globals; the
real `gsb_SyncGSSystem` / `gsb_UpdateGSSystem` may wait on the GS once the
renderer exists). The EE path of `scheduler()` and
`iosMsgSetEvent` is unchanged; `AddIntcHandler` on the host fills
`kernel_host.c`'s table, which `ico_vsync` raises.

At boot, `scheduler()` first calls `sceGsSyncV(0)`, a busy wait; the idle
thread (lower priority) does not start the other threads until the first
simulated vsync releases it, as on the PS2. `ico_host_init` returns at that
point.

## Fibers

minicoro v0.2.0 (`minicoro.h` header, 2023-11-15; repository commit
`02dad0f8`, 2024-12-07), Unlicense or MIT-0
(`port/third_party/minicoro/LICENSE`). SHA-256 of the vendored files:
`minicoro.h` `c4205e8d...caa2f`, `LICENSE` `2b3ac34b...96553`. Unmodified.

| preset | backend (minicoro's own choice) | guard page |
| --- | --- | --- |
| `linux-x64`, `asan`, `fptrap`, `linux-x64-clang`, `win-x64`, `ref-m32` | assembly switch | ours, one page below the stack |
| `win-x86-ref` | Windows fibers (`CreateFiberEx`) | the system's stack guard |

minicoro allocates a coroutine as one block: its record, the switch
context, a storage area, then the stack growing down towards them. `fiber.c`
makes the storage area exactly one page, places the block so the stack
starts on a page boundary, and makes that page no-access (`mprotect` /
`VirtualProtect`), so an overflow faults instead of overwriting the record
(`fiber_guard` test). Under ASan minicoro annotates every switch
(`__sanitizer_start_switch_fiber`); destroyed stacks are unpoisoned before
they are unmapped.

FP control state: the assembly switch saves neither MXCSR nor the x87
control word, so all fibers share the host thread's. minicoro's Windows
path creates fibers with `FIBER_FLAG_FLOAT_SWITCH` (`minicoro.h` line 1440,
answering open question 3 of `docs/research/licences.md`), which gives each
fiber its own FP state, starting from the default rather than the creator's.
The scheduler therefore runs a start hook at the top of every fiber;
`host_loop.c` sets it to `ico_fpenv_sim_enter`, the same mode
`ico_host_init` set on the host thread. On the assembly backend that
re-applies the mode already in force; on Windows fibers it establishes it.
No fiber changes the mode afterwards.

## Heap

`ios.c` used to carve the heap from EE physical addresses:
`iosMallocInitPartition(0x760000, 0x1FEFFF0)`. On the host,
`port/platform/arena.c` allocates the EE's 32 MB plus 1 MB of headroom once,
zero filled, 1 MB aligned, and `ico_arena_ee_addr(ee)` turns an EE address
into the matching host address, so the root partition spans the same
offsets and size and every block lands at its EE offset from the base.
`memory_test` checks iosInitialize's eleven partitions against addresses
worked out by hand from memory.c's arithmetic (`common` at 0x1BE7F60,
`event` at 0x1787DB0, `stage` at 0x08101A0, and so on), and allocation,
best-fit reuse, aligned allocation, realloc and full coalescing.

- The 1 MB alignment keeps every address's residue modulo any power of two
  up to 1 MB equal to the EE's, which is what `iosMallocAlignDebug` (only
  called from the movie player) depends on.
- On 32-bit Linux the arena is mapped at a 0x10000000 hint; 32-bit Windows
  places it below 2 GB anyway. An EE RAM address was always positive as an
  `int`, and the game holds addresses in ints.
- The allocator itself is unchanged: memory.c's four `__asm__("break")`
  are `ICO_BREAK()` (a trap on the host, the `break` it was on the EE), and
  memory.c has no physical-address masks (its `& 0xFFFFFFF0` are 16-byte
  rounding, correct on 32-bit hosts). With `ICO_HEAP_STATS` it reports each
  allocation and free to `arena.c`, which logs every 64 KB of new
  high-water mark per partition on stderr.
- 64-bit: memory.c's block header is 0x40 bytes of 32-bit pointers and its
  address arithmetic is `unsigned int`. It compiles on the 64-bit presets
  but cannot work there until Phase 2 (2B) retypes it; `memory_test` and
  `ios_chain_test` skip on 64-bit hosts.

## Headless seam (`ICO_HEADLESS`)

`port/null/gfx_null.c` defines the renderer calls Main and the scheduler
make, as stubs: `gsb_InitGSSystem`, `gsb_Init`, `gsb_ResetSnap`,
`gsb_TakeSnap`, `gsb_SyncGSSystem` (reports the GS done, so no frame is
skipped), `gsb_UpdateGSSystem`, and libgraph's `sceGsSyncV` (keeps the busy
wait for the next vsync and returns `GS_CSR.FIELD`). In `main.c`, Main calls
`ico_null_create_dl()` instead of `iosOmCreateDL()` while `ICO_HEADLESS`.

That one replacement is a behaviour difference: `iosOmCreateDL`
(`fumi/isys/obj_manager.c`) calls every object's display-list callback, and
those callbacks are game code that may also update state. Headless runs
therefore skip whatever state the draw callbacks change. Trace comparisons
(Phase 2) must compare two builds in the same mode; restoring the call
needs the renderer-owned functions the callbacks reach.

## Records shared with game code

`struct ThreadParam` and `struct SemaParam` (`port/compat/eekernel.h`) are
written by game TUs, compiled with `-malign-double` on 32-bit x86 and
`-mno-ms-bitfields` on Windows, and read by `kernel_host.c`, compiled with
the platform ABI (`docs/research/compiler-semantics.md`, `IcoFlags.cmake`).
Both hold only `int`, `unsigned int` and pointer fields, so their layout is
the same under either option set.

## Not done here

- SIF and IOP: `ios.c` still calls `SgSndn2RemoteInit`,
  `sceSifInitIopHeap`, `soundAllocIopHeap` and `soundInit` (sound,
  package 1C and Phase 4); `seki/src/FileManager.c`'s `sceSifInitRpc` and
  module loads are 1C's.
- `SetAlarm` returns -1 (only the movie player uses alarms). DMAC handlers
  are recorded but never raised.
- Renderer calls outside Main and the scheduler (`gsb_SetBGColor`,
  `gsb_SetMotionBlur`, `sceGsSyncPath`, `sceGsResetPath`, ... from
  `StageManager.c` and others) are not stubbed; the renderer waves or a
  later headless pass own them.
- `scePrintf` prints only when the `ICO_TTY` environment variable is set
  (the retail game's TTY output went to the development kit).

## Tests

| test | checks | runs on |
| --- | --- | --- |
| `sched` | preemption on wakeup, FIFO within a priority, a preempted thread keeps its place, wakeup counts and `CancelWakeupThread`, semaphore FIFO release and counts, `DeleteSema`, exit / restart / terminate / delete, the 0x22 finished-process convention, `ChangeThreadPriority` and `RotateThreadReadyQueue`, `iWakeupThread` from the vblank handler, busy waits, suspend/resume, the boot thread | every Linux preset |
| `fiber` | 64 fibers switched round-robin with stack contents checked, 200 KB of stack use, destroying suspended fibers, FP mode inside a fiber; under `asan`, the sanitizer's fiber annotations | every Linux preset |
| `fiber_guard` | a stack overflow in a fiber faults on the guard page | Linux |
| `arena` | allocated once, aligned, zero filled, EE address mapping, below 2 GB on 32-bit, heap statistics | every Linux preset |
| `memory` | the game's allocator in the arena at the EE's addresses | `ref-m32` (32-bit) |
| `ios_chain` | thread.c, message.c and memory.c on the scheduler: vsync, vblank handler, event thread, a scheduler loop like main.c's, Main every second vsync, an actor process at 0x13 that ends at 0x22 and is destroyed | `ref-m32` (32-bit) |
