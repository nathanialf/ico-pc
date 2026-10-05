# Phase 1 boot failure: diagnosis and diagnostics (package 1E)

The Phase 1 test package (`dist/ico-pc-phase1-win.zip`, commit `6e3e567c`)
died about 2 s into boot on Windows, on both `x86` and `x64`. Its log ended
after `ico_pc: exit after 3000 Main ticks`. The trace was empty and there
was no summary line. This file records what killed it, what was fixed, what
is still open, and what the log now contains so that the next failure
explains itself.

For this package only, the user allowed bounded runs of the headless game in
the container (`timeout 120`, 50 to 3000 ticks, `ref-m32` and `linux-x64`,
ISO at `baserom/Ico_PAL.iso`). Every finding below comes from those runs or
from reading the code; both are cited. The runs used a clean worktree of
`HEAD` (`93d918e4`), whose `ico2/` and `port/{platform,null,data}` are
identical to `6e3e567c`, so other packages' uncommitted work was not part of
them.

## Result

With the fixes below, `ref-m32` (the Linux twin of `win-x86-ref`) runs all
3000 Main ticks:

- game flag 382 (New Game) sets at tick 661;
- `stage_no` goes 1 → 41 (tick 663) → 42 → 43 → 45 → 40 → 3 (tick 1036);
- the run ends with `3000 Main ticks, 6007 vsyncs, stage_no 3`.

That is the plan's Phase 1 exit ("boots to title and ticks a stage") on the
32-bit build, pending the user's Windows run. `linux-x64` still crashes
during the first stage load; that is Phase 2 work (see "64-bit").

## Why the log said nothing

- **The trace was empty.** `trace_host.c` wrote the header and the tick
  lines into a fully buffered `FILE` and flushed every 64 lines.
  `msvcrt`'s buffer is 4 KB, about 29 lines, so a run that died before then
  left 0 bytes, even if a few ticks had run. In this case no tick had run:
  crash 1 is in the first pass of Main, before the first
  `ico_host_main_tick()`.
- **There was no summary line.** It is registered with `atexit`, and an
  access violation in a GUI exe ends the process without running `atexit`
  handlers or showing anything.
- **The log's last write time came ~2 s after the trace opened.** The
  process ended then. The handles were closed at termination, and with
  nothing left in a buffer, nothing more was written.

## Crash 1 (definite, fixed): prototype mismatches shift stack arguments on i386

**Symptom.** `ref-m32`: SIGSEGV reading address `0x1`. The faulting PC
symbolises to `gif_MakeSprite` at `seki/src/GifPacket.c:173`, the
`GIF_COLOR(col)` read. The stack is
`Main` (`common/src/main.c:200` region) → `ExecIcoMisc` →
`exec_layout_texture` → `lt_draw_primary_sprite`
(`common/src/layout_texture.c:557`) → `gif_SpriteSensitive`
(`GifPacket.c:292`). This is Main's first iteration, before the first tick.
It matches the Windows symptom: death within seconds, no tick.

**Cause.** `layout_texture.c:407` declares its own
`gif_SpriteSensitive(SprRect *r, unsigned int z, ...)`, but `GifPacket.c:291`
defines `z` as `long long`. The declaration is the reconstruction's own, and
it is correct for the EE: every argument there has its own 64-bit register,
so the narrower type only changes how the constant is loaded. On i386,
arguments go on the stack, so the caller pushes 4 bytes where the callee
reads 8. Every later argument shifts by one word: the callee reads `uv` from
the caller's `col` (non-NULL, so it takes the textured branch) and `col`
from the caller's `prim` (`1`). On x86-64, each argument still has its own
register, so the mismatch is harmless there. That is why only the 32-bit
builds die here.

**How the rest were found.** A `ref-m32` link with
`-flto -Wlto-type-mismatch` (GCC 14) lists every cross-file declaration
that disagrees with its definition: 56 warnings. The ones that change
argument sizes on i386 are all of the same family: `GifPacket.c` takes
`long long` where callers' local externs say `int` or `unsigned int`.

| caller | function | declared | defined |
| --- | --- | --- | --- |
| `common/src/layout_texture.c` | `gif_SpriteSensitive`, `gif_SpriteSensitiveOffset` | `unsigned int z` | `long long z` |
| `fumi/src/jimaku.c` | `gif_SpriteSensitiveOffset` | `unsigned int z` | `long long z` |
| `common/src/kanban.c` | `gif_SpriteSensitive`, `gif_SpriteSensitiveOffset` | `unsigned int z` | `long long z` |
| `common/src/layout_action.c` | `gif_Sprite` | `unsigned int z` | `long long z` |
| `common/src/icoMisc.c` | `gif_MakeSpriteNoTexture` | `unsigned int z` | `long long z` |
| `seki/src/GsBase.c` | `gif_MakeSpriteNoTexture`; `gif_SetAlpha`; `gif_SetGsReg`; `gif_SetDrawEnviroment` | `unsigned int z`; `int` ×3; `int reg`; `int fbp, int psm` | `long long`; `long long` ×3; `long long reg`; `unsigned long long` ×2 |
| `sugipon/src/staticBlur.c` | `gif_SpriteSensitiveOrg`; `gif_SetAlpha`; `gif_SetGsReg`; `gif_SetDrawEnviroment` | as `GsBase.c` | as above |

**Fix.** Each of these externs gets an `#ifdef ICO_HOST` twin with the
definition's types. The EE declarations are unchanged. The values are
unchanged too:

- `z` only ever has its low 32 bits used (`GIF_XY`'s `z << 32`);
- an `int` converted to `long long` is sign-extended, which is how the EE
  holds an `int` in a 64-bit register.

After this fix, `ref-m32` ran to tick ~95 (crash 2).

**The other LTO warnings** do not move arguments on i386 or x86-64 in the
paths that run:

- return types `int` vs `void *` vs `short`;
- `short` vs `int` parameters (both take one 4-byte slot);
- struct-tag differences between `data/*.c` and their users (the
  layout-assert work, package 2B);
- pointer vs `int` (32-bit is fine; 64-bit is Phase 2:
  `soundSeDefPlay`/`soundSeDefPlayWithVolumeRate` `pos`, `seMail`,
  `GetChainAnimation`, `GetTorchGObjOfWeapon`, `iosReallocDebug`).

Two of them read garbage on any host, as they presumably did on the EE:

- `IsWallLeverStatus(void)` is called from `script.c:1555` but defined with
  a `GObj *lev` argument (`sugipon/src/switch.c.inc:249`);
- `GetChainNodeID` is called with one argument but defined with two
  (`clothAnimation.c:1980`).

Neither is on the boot path. `fptodp` is declared `double` in
`motMan_getFinalMatrix.c.inc:1023` but `int` in `debug_null.c` and
`boyact.c`. Its result only goes to the debug printfs, and on i386 the
mismatch leaves the x87 stack balanced (an `fstp` of an empty register,
masked). It would trap under the `fptrap` preset. To reproduce the list:
configure `ref-m32` with `-flto=auto -ffat-lto-objects` in `CMAKE_C_FLAGS`
and `-flto=auto -Wlto-type-mismatch` in `CMAKE_EXE_LINKER_FLAGS`, then
build `ico_pc`.

## Crash 2 (definite, fixed): blocking disc reads took no simulated time

**Symptom.** `ref-m32`, tick ~95-105 (stage 1, the first stage switch):
SIGSEGV reading address `0x74` in `_Clip` (`fumi/src/fieldCollision.c:1013`,
`sub->disp` with `sub = obj->dobj == NULL`). The stack is `_Clip` ←
`ClipFloor` ← `adjustMotionHeightToNearestField` / `InitMotionGeoInfo`
(`sugipon/src/motionManager2.c:867`) ← `initGeometryState`
(`common/src/DObj.c:131`) ← `CSVSYSTEM_InitDObj`, on the stage-load thread
(`InitIcoMisc`, thread 14). Temporary logging showed the object in
`colObjList` had `self == NULL`, `labelType == -1` and `dobj == NULL`: a
slot `isysGObjRemoveAll` had cleared. Main had built the list
(`MakeCollisionDependGObjList`, run at the top of every Main pass) one tick
earlier. StageManager's `stop_free_resources` then removed every object, and
the load thread ran before Main could rebuild the list.

**Cause.** `cdvd_host.c` completed a blocking `sceCdSync(0)` on the spot.
Its comment said: "every blocking caller only waits, so the order of game
events is the same". On the PS2, libcdvd's `sceCdSync(0)` loops on
`sceCdDelayThread` (`sce/libcdvd/cdvd000.c:522-531`, `:143-155`:
`CreateSema`, `SetAlarm`, `WaitSema`). The waiting thread blocks, and every
other ready thread runs while the drive works, the same-priority Main
included. Headless, the stream manager (priority 27) read a whole stage
pack through 56 blocking reads in one `ico_sched_run`. The load thread
(also 27) went from the removal straight into object initialisation with no
Main pass in between. On the PS2 a load spans many vsyncs, and Main rebuilds
the list in each one.

**Fix.** `cdvd_host.c` `sceCdSync` (even mode), called from a game thread
with a command in flight, now waits on a semaphore until the vsync that
ends the command. `ico_cdvd_host_vsync` signals it, as the vblank-time
completion already did for polls. Each read command now takes at least one
simulated vsync whether polled or blocking. Called from the host context (the
unit tests), it still completes at once. `docs/port/DATA.md`, "Timing", is
updated.

After this fix, `ref-m32` ran all 3000 ticks (see "Result").

## 64-bit

`linux-x64` crash sites, in order of appearance:

1. `fumi/ios/cdvd.c:656` `iosCdStRead`: the ring copy's source was
   `(req->readPos << 11) + (int)req->buf`, a truncated pointer, so `memcpy`
   faulted on the first `DATA.DF` read at boot. This is almost certainly the
   Windows `x64` death at ~2 s. **Fixed** under `ICO_HOST` with
   pointer-wide arithmetic.
2. `fumi/isys/gobj.c` `isysGObjGetExist_begin` (and the two other
   `&gobjTable[gobjMax - 1]` sites): with no table yet (`gobjMax == 0`),
   `gobjMax - 1` wraps to `0xFFFFFFFF`. On the EE the address wraps back to
   `start`, so the loop does not run. On a 64-bit host the zero-extended
   index lands 64 GB away and the loop reads from NULL. Reached from
   `shadow_Init` in Main's first `gsb_InitGSSystem`. **Fixed** under
   `ICO_HOST` (`gobjTable + (int)(gobjMax - 1)`).
3. `seki/src/Light.c:131` `light_setLinkLight`: `lastLight` is an `int`
   holding a `Light *`. **Open**: it is Phase 2's pointer-in-int sweep
   (2B/2D). The x64 build stops here, during the stage-1 load.

The boot audit (a subagent reading the code) also listed for Phase 2:

- `bgRunning = (int)bg` (`cdvd.c:1309`, read back as a pointer in
  `cdWait`);
- `buf[0] = (int)&iosCdvd` (`cdvd.c:1192`);
- `Ee2Iop(int ee, ...)` with host pointers (`s_init.c:100`, `:478`);
- `stThread[120]` (`cdvd.c:143`) used as an `IOSThread`, which is larger
  on 64-bit.

## Candidates checked and ruled out

These are the hang and deadlock candidates from the package brief. Each was
checked against the code, and against the runs where noted.

| candidate | verdict and evidence |
| --- | --- |
| `file_Init` disc wait spins without yielding | Ruled out. `sceCdStatus` returns `0x0A` with a disc mounted, and `sceCdDiskReady` returns 2 (`cdvd_host.c`); `main_host.c` mounts the image before boot. Log: `boot: iosInitialize` follows `boot: file_Init` at once. |
| `iosInitialize` / `SgSndn2RemoteInit` / `soundInit` waits | Ruled out. The null server is registered before the bind (`snd_null.c` `SgSndn2RemoteInit`). `sceSifDmaStat` returns -1, which ends `Ee2Iop`'s loop (`s_init.c:112`). `SgSndn2RemoteSync` returns 0. `SgGetDmaTransferStatus` returns 1. The IOP heap holds sound and both ADPCM rings. |
| fibers run before the first vsync / busy-waits starve the host loop | Ruled out. `ico_host_init` runs fibers until all wait or the top one spins. `VSync`, `sceGsSyncV` and the idle loop yield through `ico_sched_spin_vsync`, which returns to the host when the spinning thread is the highest ready one (`sched.c` `ico_sched_run`). Log: `boot ran until every thread waits`, then `first vsync done`. |
| scheduler semantics (self-wakeup, pending wakeups, same-priority signal, priority 33/34 parking, `DeleteSema` with waiters) | Ruled out by the audit and the run. They match the rules in `sched.h`. |
| stream-manager livelock / background reader "read command fail" | Ruled out. The stream manager fills its 0x380-sector ring and blocks. The background reader only runs on the cdvd thread, and `cd.busy` clears at each vsync before any fiber runs. |
| `kanbanBoot` / `layout` waits on `frame_count` or `fadeStatus` | Ruled out. The run's kanban steps go 0 → 2 and through the card checks (log lines `kanbanBoot: bootStep … mcCheckStep …`), and flag 382 sets at tick 661. |
| Main never woken (`systemStatus[1]`, `stageManagerFreeResourceFlag`) | Ruled out. `systemStatus[1]` is 2 at boot (`main.c:37`); the flag is cleared at `StageManager.c:181` and `:202`. |
| opening demo skip depends on the ADPCM open | Satisfied. The open completes through the cdvd background reader (`adpcmOpenProc`, `adpcm_init.c:524`), not through the sound driver. The demo gave way to the title, and New Game was chosen at tick 661. |
| stack alignment of Windows fibers on i386 | Fixed defensively (audit finding). `CreateFiberEx` promises 4-byte alignment at entry, and GCC on i686 keeps SSE spills in 16-byte-aligned slots (the audit found `cvtdq2ps 0x50(%esp)` in `display_texture`, `xorps` in `HandCameraCorrect` and `__getCloth4D`). `fiber.c`'s `fiber_main` and the diagnostics' OS entry points now carry `force_align_arg_pointer` on i386. Linux's assembly fibers align themselves, so the runs here could not show this. |

**Not from this package's tree.** In the main working tree, package 2C's
uncommitted `PObj.c` crashes earlier: `pac_getWeight` (`Packet.c:316-321`)
dereferences `ObjEnt.p`, which is still an EE word after relocation (see
the open item in `docs/port/LOADERS.md`). The same applies to
`Shadow.c:1324-1331` `shadow_MakeObjectData`. Neither is in `HEAD`, so
neither is in the 1b package; 2C should pick them up.

## What the log contains now

All of this is on by default, with no flags. It lives in
`port/platform/diag_host.{h,c}`. Every line is one unbuffered OS write to
`logs/ico-pc.log`, so a crash or a kill keeps everything written before it.
The heartbeat and the watchdog run on their own OS thread. A game thread
that spins without a kernel call blocks the host loop, but it cannot stop
them.

- **Boot milestones**, stamped `[wall s, vsync, tick, #thread name]`:
  - `boot starts`, `boot: file_Init`, `boot: iosInitialize`,
    `boot: iosInitialize done`;
  - each thread created (id, creator, priority, the function it runs);
  - `boot: idle and scheduler started`, `boot ran until every thread waits`;
  - `scheduler: vsync event set`, `idle: every thread started`,
    `first vsync done`;
  - `Main: past IosPadLock and IosStgMgrLock`,
    `Main: gsb_InitGSSystem done`, `first Main tick done`;
  - every `stage_no` change, and every change of `kanbanBoot.c`'s
    `bootStep` / `mcCheckStep`.

  Thread creation is logged in full until the first tick, then 16 more.
- **Heartbeat**, every 2 s of wall time:
  - vsyncs and Main ticks, `stage_no`, `systemStatus[5..8]`, `fadeStatus`,
    `mpegPlay` / `mpegInitDone`, StageManager's free and wake flags,
    `game_pause`, `kanbanBootEnd`;
  - cdvd: reads, sectors, last LSN, busy, waiters, stream state, whether
    the cdvd thread is asleep, background reads;
  - SIF: calls, the last server id and RPC number;
  - the thread on the CPU (id, name, priority), ready and waiting counts,
    context switches, the kanban steps.

  After 3 identical beats the line ends `no progress for N s`.
- **Crash report** (exit code 3). It covers access violations, illegal
  instructions (`__builtin_trap`, `ICO_BREAK`), divide errors, stack
  overflow, FP traps and `abort()` (`ico_assert`, `debug_assert*` and the
  scheduler's fatal errors all set a "last failure message" first). It
  contains:
  - the exception or signal and the faulting PC as `module+offset`;
  - the faulting data address and whether it was a read or a write;
  - the game thread then, with its last kernel call (call, argument,
    caller `module+offset`, vsync);
  - up to 24 return-address candidates from the stack;
  - the last failure message, a heartbeat line and every thread;
  - then the summary (ticks, vsyncs, stage).

  On Windows the report is written by the diagnostics thread, so a stack
  overflow does not format it on the overflowed stack. A message box names
  the log, and Windows' own crash dialog is suppressed.
- **Watchdog** (exit code 4). It fires when no Main tick has come
  `watchdog=` seconds (default 30) after boot started, or no new one for
  twice that (default 60). It samples the main thread's PC and stack
  (`SuspendThread` / `GetThreadContext` on Windows, a signal on POSIX), so
  a spin shows where it is. It then dumps every thread (state, what it
  waits on: sleep, sema N with count and waiters, or the vsync it
  busy-waits for; last kernel call and caller), writes the summary, shows
  a message box and exits.
- **Exit line** on every normal end: `exit: ticks= reached`,
  `the game called Exit(n)`, `host: the game's main returned`, or `exit()
  from the game or the C library`. The summary follows it.
- **Trace**: the header and every tick line are flushed as they are
  written.

**Turning an offset into a function.** The package ships each exe's link
map (`ico_pc_x86.map`, `ico_pc_x64.map`). The exes are RelWithDebInfo with
DWARF line tables, so the same exe (keep the zip) also answers:

```
tools/toolchain/mingw-gcc/usr/bin/i686-w64-mingw32-addr2line -f -i -e ico_pc_x86.exe 0x<0x400000 + offset>
tools/toolchain/mingw-gcc/usr/bin/x86_64-w64-mingw32-addr2line -f -i -e ico_pc_x64.exe 0x<0x140000000 + offset>
```

`__image_base__` in the map gives the base (0x400000 for x86,
0x140000000 for x64). On Linux the offsets are already file addresses:
`addr2line -e ico_pc 0x<offset>`.

Unnamed thread functions print as `fn ico_pc.exe+0x…`. Those are the
static ones in `thread.c` (destroy manager, priority 13), `message.c` (the
vsync event thread, 11), `cdvd.c` (stream manager, 27), `pad.c` (pad device
manager), and the game's per-object threads.

## Game sources changed (`#ifdef ICO_HOST`, EE path unchanged)

| file | change |
| --- | --- |
| `common/src/layout_texture.c`, `common/src/kanban.c`, `common/src/layout_action.c`, `common/src/icoMisc.c`, `fumi/src/jimaku.c`, `seki/src/GsBase.c`, `sugipon/src/staticBlur.c` | host twins of the mismatched `gif_*` externs (crash 1) |
| `fumi/ios/cdvd.c` | `iosCdStRead`'s ring copy with pointer-wide arithmetic (64-bit) |
| `fumi/isys/gobj.c` | the three `end` pointers wrap as on the EE when `gobjMax` is 0 (64-bit) |
| `common/src/main.c` (EUC-JP, ASCII patch) | milestones in `boot`, `idle`, `scheduler` and `Main`; names for the static `idle` and `scheduler` |
| `fumi/ios/thread.c` | `iosThreadCreate` reports the thread's function and priority (`ico_host_thread_func`) |
| `common/src/kanbanBoot.c` | `kanbanBootMain` reports `bootStep` / `mcCheckStep` (`ico_host_kanban_step`) |

## Port sources changed

- `port/data/cdvd_host.c`: blocking `sceCdSync` waits for the vsync
  (crash 2); drive counters for the heartbeat.
- `port/data/sif_host.c`: last RPC for the heartbeat.
- `port/null/snd_null.c`: ADPCM streams advance (`HEADLESS_STUBS.md`).
- `port/null/gfx_null.c`: `sceGsSyncV` records its call for the thread
  dump.
- `port/null/debug_null.c`: asserts set the failure message.
- `port/platform/`:
  - new `diag_host.{h,c}` and `test/diag_test.c`;
  - `sched.{h,c}`: per-thread last call, views for the dump; `sched.h`
    passes `<sched.h>` through to the C library for `<pthread.h>`;
  - `kernel_host.c`: every kernel call records itself, and `Exit` logs;
  - `fiber.{h,c}`: stack bounds and i386 entry realignment;
  - `trace_host.{h,c}`: per-line flush, the heartbeat status, stage and
    first-tick milestones;
  - `main_host.c`: wiring, `watchdog=`, the exit lines;
  - `host_loop.c`, `assert_host.c`;
  - `CMakeLists.txt`: `diag_host.c`, Threads, `-Map=ico_pc.map`.

# Package 2H: the x64 heap check at Main tick 20

The `win-x64` build of `712d7b62` (package 2G's zip) stopped at Main tick
20, vsync 47, in thread `iosCdvdManager`, on the allocator's own check
`ios/memory.c:598` ("mem:illegal free area pointer"): a free-list node's tag
had been overwritten. The x86 build ran 2,000+ ticks. `linux-x64` headless
reproduces it at the same tick and vsync.

## Result

- The overrun is `seki/src/Packet.c` `pac_makePacket`: it allocated each
  strip header (`PacHeader`) with the EE's literal 160 bytes. On a 64-bit
  host the record is 176 bytes (`next` and `data` are pointers), so
  `node->data` and `node->next` went to bytes 0xA0..0xAF of a 160-byte
  block: the first 16 bytes of the next node's header, which is its tag.
  The same function sized `PObjGroup` (48 EE, 64 x64), `MatLine` (16, 32)
  and `PacLineSet` (144, 152) by literal; all now use `sizeof`.
- After the fix `linux-x64` runs to Main tick 117 and stops with a
  SIGSEGV in `getParallelWindVector` (`sugipon/src/windField.c:249`): the
  cloth's first point is NaN because the skeleton node matrix (node 33) the
  cloth hangs from is NaN on x64 only. `(int)NaN` is `0x80000000` and
  `windStrength[0x80000000]` faults on a 64-bit host. `ref-m32` has no NaN
  there. This is not a heap overrun (the write-checking run below is clean up
  to it). Package 2H did not fix it.
- The per-tick traces of `linux-x64` and `ref-m32` (3000 ticks) are
  identical for every line `linux-x64` wrote (header plus ticks 0..116).
  `ref-m32` reaches stage 3 as before: stage 1 -> 41 at tick 663,
  -> 42 (756), -> 43 (836), -> 45 (892), -> 40 (956), -> 3 (1036),
  3000 ticks, 6007 vsyncs.

## How it was found: `ICO_HEAP_ASAN`

ASan alone does not see this kind of overrun: every game allocation comes
out of the arena (`port/platform/arena.c`), one `mmap`. `fumi/ios/memory.c`
now has a debug mode, off unless `ICO_HEAP_ASAN` is defined, that needs
`-fsanitize=address` (it stops the build with `#error` otherwise). The
allocator itself is unchanged (same partitions, offsets, free lists and
`ICO_HEAP_STATS` accounting). Each public function (`iosMallocDebug`,
`iosFree`, `iosMallocAlignDebug`, `iosReallocDebug`, the partition
functions, the leak checks) becomes a wrapper that unpoisons the root
partition, runs the original, and then walks every partition's node list
and poisons:

- every block header (0x50 bytes on x64);
- the slack between a block's requested size and its 16-byte-rounded end
  (the requested size is kept in a side table);
- free areas' bodies, only with `ICO_HEAP_ASAN_FREE=1` in the environment.
  The EE code writes into free memory in at least one place on purpose
  (see below), so this is off by default.

The first write into a header then stops with the writer's stack. To build
and run it:

```
cmake --preset asan -B build-host/2h-asan -DICO_LINK_EXE=ON \
  -DICO_DATA_DIR=$PWD/build/data -DICO_SANITIZE=address \
  "-DCMAKE_C_FLAGS=-DICO_HEAP_ASAN=1 --param=asan-instrument-reads=0"
cmake --build build-host/2h-asan --target ico_pc
cd build-host/2h-asan    # ico-pc.ini, pad-script.txt as in TESTING.md
printf 'interceptor_via_fun:sceSifSetDma\n' > asan.supp
ASAN_OPTIONS=detect_leaks=0:fast_unwind_on_fatal=1:suppressions=$PWD/asan.supp ./ico_pc
```

The options matter:

- `ICO_SANITIZE=address`: the preset's `undefined` stops at
  `fumi/isys/gobj.c:518` (pointer arithmetic on a null table before the
  first object exists, as on the EE). That is not this bug.
- `--param=asan-instrument-reads=0`: only writes are checked. The EE code
  reads past the end of records on purpose in many places (16-byte VU0
  loads of 12-byte vectors, `omori/src/gv.c:91` on `camera-ico2.c`'s
  `targetASmooth`). Corruption is always a write.
- `fast_unwind_on_fatal=1`: the default slow unwinder faults on the fiber
  stacks (minicoro); the fast one stops at `_mco_main`, which is enough.
- the `sceSifSetDma` suppression: `soundBDDataSet` rounds a sound bank up
  to 64 bytes and DMAs that many (`s_init.c:497`), reading up to 63 bytes
  past the block, as the EE does.

## Other host bugs the runs found and fixed

All are under `#ifdef ICO_HOST` or written so the EE compile is unchanged
(see "EE identity" below).

| Where | What |
| --- | --- |
| `ito/src/itou_boss.c` `itou_boss_gflag_init` | one `memset` cleared `gflag[16]` and `capsule[53]` together, assuming the EE linker's order. GCC put `capsule` first, so the `memset` ran 3,392 bytes past `gflag` into other `.bss`. Two `memset`s on the host. |
| `seki/src/Primitive.c` | `Fan2D` (12 EE, 16 x64), `Mesh3D` (144, 176) and `PrimParticle` (416, 424) allocated by literal; now `sizeof(T) > N ? sizeof(T) : N` (the `BgAnimation.c` idiom, so the 32-bit sizes are unchanged). |
| `seki/src/Light.c` | `Light` (80, 96) and `AmbientVolume` (160, 168), same fix. |
| `fumi/ios/shockdriver.c` `Init_Shock` | `ShockDriver` is `int[4]` used as a `ShockMgr` (24 bytes on x64); the host hands `Init_ShockDriver` a static `ShockMgr` instead. |
| `fumi/src/act-game.c` `_ACTCharStatus_Init` | read the act pointer as `self[0x59]` of an `int **` (byte 0x2C8 on x64, a null there): it now clears `bits58` and `pad60` through `GOBJ_ACT`. This was the SIGSEGV at tick 117 before the windField one. |
| `fumi/sound/s_init.c:869` | `self->proc()` with no argument; on the EE `$a0` still held the slot, which every `stageSE*` proc takes as `self`. The host passes `self` (reported by 2F). |
| `common/src/kanban.c`, `common/src/layout_texture.c` | `init_textures_of_specified_property` stored `texNo` and `texData` by the EE's stride (0x70) and offset (`texData` = `texNo` - 4); on x64 `LtProperty` is 0x78 with `texData` 8 bytes, so every row after the first was written at the wrong place. The host writes the fields by name. The texture base-name helper returned a pointer into its own stack buffer (ASan: stack-use-after-scope); the buffer is `static` on the host. (2F-owned files.) |

Not fixed, noted for later:

- `seki/src/Packet.c` line-record end mark: `p[src->lineCount].attr.b.type
  = 0` after `p` has already been advanced `lineCount` times, so it writes
  `lineCount * 192 + 0xB8` bytes past the start of the list, past the
  block's end, into what is free memory at that moment. On the EE the same.
  The intended record is `p[0]`. Left as is (it changes the EE's behaviour).
- About 50 other allocations in `ico2` still pass a literal size (for
  example `sugipon/src/boy.c`, `box.c`, `a_p_1.c` node buffers,
  `sugipon/src/worm.c`, `cage.c`, `weapon.c`, `seki/src/BgAnimation.c:384`,
  `seki/src/StageAnimation.c:1045`, `fumi/ios/thread.c:258,278`,
  `common/src/PObj.c:281,283,539`). The `ICO_HEAP_ASAN` run only checks
  the ones on the boot path up to tick 117. A static pass (literal size
  against `sizeof` of the pointer's type on x64) would find the rest.
- The x64 NaN in node 33's matrix (above).
- The `fptrap` preset traps at vsync 6 in `gsb_SetVSMatrixSub`
  (`seki/src/GsBase.c:1361`), a divide by zero the EE takes as
  saturating (DIVERGENCES.md F5). That stops it before the stage load, so
  it could not locate the NaN.

## EE identity

The 10 changed game TUs (`kanban.c`, `layout_texture.c`, `memory.c`,
`shockdriver.c`, `s_init.c`, `act-game.c`, `itou_boss.c`, `Light.c`, `Packet.c`,
`Primitive.c`) compile with `tools/compile_c.sh` to the same `.text`,
`.data`, `.rodata`, `.sdata`, `.bss`, `.sbss`, `.lit4`, `.lit8` and
relocations as a `HEAD` (`5ddb5feb`) worktree.
