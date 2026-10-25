# Architecture

How the PC port runs a PlayStation 2 game: one section per piece of PS2 hardware or Sony library
the game used, saying what stands in for it on the host and where that code lives. Start here;
the other documents in `docs/port/` go deeper into each part. The glossary is the last section,
[Terms](#terms).

## Files

| path | what it holds |
| --- | --- |
| `ico2/` | the game's decompiled C, one folder per original programmer (`common`, `fumi`, `ito`, `omori`, `script`, `seki`, `sugipon`), plus `vusrc/` (the VU1 microprograms, kept as the shaders' reference and never assembled here) |
| `sce/` | reconstructions of Sony's libraries; the host compiles only `libsndn2/sound.c` |
| `port/compat/` | the host's versions of Sony's SDK headers (`eekernel.h`, `eeregs.h`, `libcdvd.h`, `libpad.h`, `libmc.h`, ...) |
| `port/platform/` | the EE kernel (threads, semaphores, interrupts), the host loop, the EE RAM arena, the window, the log and diagnostics ([PLATFORM.md](PLATFORM.md)) |
| `port/data/` | the disc: libcdvd, the VFS, the ISO/CHD/BIN reader, `ico.o2r`, the extractor, the data tables, the SIF and IOP RAM |
| `port/math/` | VU0 maths in C, the PS2 float models, the EE's soft `double`, host copies of the EE's libm |
| `port/render/` | rd: the GS and VU1 replacement the game's render layer draws through, and the presenter |
| `port/rhi/` | the RHI and its backends: `vk/` (Vulkan, MoltenVK on Apple), `d3d12/`, `d3d11/` and `gl/` (OpenGL) |
| `port/shaders/` | the HLSL shaders, among them one per VU1 microprogram family |
| `port/null/` | the graphics floor under the game's render layer in both builds (libgraph, libdma) |
| `port/audio/` | the software SPU2, the SNDN2DRV.IRX sound driver as a host RPC server, the output |
| `port/fmv/` | the movie player (PSS demux, MPEG-2 decode) in place of the IPU path |
| `port/input/` | libpad and the input layers under it (SDL3 devices, bindings, pad scripts) |
| `port/save/` | libmc over a folder |
| `port/config/`, `port/game/`, `port/ui/` | `config.toml`, the options, the port's own menus and features |
| `port/core/`, `config/regions.txt` | the game region: which disc is being played, and each disc's numbers |
| `port/include/ico_frame.h` | the native frame's arithmetic, shared by `ico2/` and `port/` |
| `cmake/IcoFlags.cmake` | the compile options that give the game its EE semantics |
| `CMakeLists.txt` | the game libraries, the data tables, the program |

## What runs

The game itself is the decompiled C under `ico2/`, compiled natively for the host by the host's
own compiler: one object library per programmer folder, each listing its sources in the retail
link's order (`CMakeLists.txt` (the game section); `cmake/IcoSources.cmake` (`ICO_PROGRAMMERS`,
`ICO_SOURCES_<programmer>`)). No PS2 code
is interpreted or recompiled. Every game source is compiled except the nine files of the PS2
movie player, which `port/fmv` replaces (`cmake/IcoSources.cmake` (`ICO_EE_ONLY_SOURCES`)). The
blocked-source lists are empty: every game source compiles with gcc and clang
(`cmake/IcoExclusions.cmake` (`ICO_BLOCKED_SOURCES`)).

The compile options carry the semantics the game was written against on ee-gcc 2.9 for the
R5900: `-fno-strict-aliasing`, `-fwrapv`, `-ffp-contract=off`, `-fno-fast-math`,
`-fsigned-char`, `-fno-common` and `-fgnu89-inline`, and on Windows `-mno-ms-bitfields` for the
game's record layout (`cmake/IcoFlags.cmake` (`ICO_SEMANTIC_OPTIONS`,
`ICO_GAME_LAYOUT_OPTIONS`)). No configuration defines `NDEBUG`, so the game's asserts are
evaluated as on the PS2 (`CMakeLists.txt`). Every game source is compiled with `ICO_HOST=1`,
which selects the host branches written into `ico2/`; the window build adds `ICO_RD=1`, the
headless build `ICO_HEADLESS=1` (`CMakeLists.txt` (`ICO_GAME_DEFINITIONS`)).
[GAME_CODE.md](GAME_CODE.md) covers the flags, the host branches and the audits.

The game's `main` (`ico2/common/src/main.c`) is renamed `ico_game_main` by a definition on that
one source (`CMakeLists.txt`). The program's real entry point is `port/platform/main_host.c`
(`host_main`): it opens the log, reads `ico-pc.ini`, mounts the game data, loads the data tables,
opens the window, then calls `ico_host_init`, which runs `ico_game_main` on the boot
[fiber](#terms), and then calls `ico_host_step` once per [vsync](#terms) forever
(`port/platform/host_loop.c` (`ico_host_init`, `boot_main`)). [PLATFORM.md](PLATFORM.md) has the
entry points of each operating system.

There are two builds of the same game code. The [window build](#terms) (`ICO_HEADLESS=OFF`, the
default) opens an SDL3 window and draws through rd; the [headless build](#terms)
(`ICO_HEADLESS=ON`) has no window, no renderer and no audio device, runs vsyncs as fast as it
can, and serves the [trace](#terms) and test runs (`port/platform/main_host.c` (header comment);
`CMakeLists.txt` (`ICO_HEADLESS`)). Both compile the game's own render layer, so the state its draw
phase changes (fades, view matrices, heap allocations) is the same with or without a renderer; the
headless build writes its packets as on the PS2, and the window build sends the GS register writes
to the decoder instead (`port/null/gfx_null.c` (header comment); `ico2/seki/src/GifPacket.c`
(`setGsReg`)).

`sce/` holds this project's reconstructions of Sony's libraries. On the host they are mostly
references: the headers in `port/compat/` keep their signatures (`port/compat/eekernel.h`,
`port/compat/eeregs.h`), `port/math/libvu0.c` and `port/math/newlib/ico_libm.c` are written from
`sce/libvu0` and `sce/libm`, and `sce/libgcc`'s soft float is the oracle of the `softdouble`
test (`port/math/CMakeLists.txt`). The one `sce/` source compiled into the program is the Sg
sound sequencer, `sce/libsndn2/sound.c`, with its host seams (`port/audio/CMakeLists.txt`
(`ico_sndn2`)).

## EE: CPU and memory

The [EE](#terms)'s integer code needs no emulation: the game's C runs on the host CPU. What the
code assumes about memory is kept. The game carves its heap out of fixed EE physical addresses
(`iosMallocInitPartition(0x760000, 0x1FEFFF0)`); on the host the same range is an offset into
one [arena](#terms) allocated at start, 32 MB plus 8 MB of headroom, zero filled and 1 MB
aligned, so every heap block has the EE's offset and the EE's alignment residue
(`port/platform/arena.h` (`ico_arena_init`, `ico_arena_ee_addr`)). The game's
[partitions](#terms) are carved from that range by the game's own allocator; four of them are
larger on the host, because a block header is 0x50 bytes there against 0x40 on the EE and records
holding pointers grow (`ico2/fumi/ios/ios.c` (`ICO_HOST_HEAP_EXTRA`, `iosInitialize`)).

The EE's memory-mapped registers are two plain memory blocks, the EE I/O page (0x10000000) and the
GS privileged page (0x12000000): a store lands there, a load reads the last store back, and nothing
reacts (`port/compat/eeregs.h` (`ico_hw_eeio`, `ico_hw_gs`); `port/platform/hwregs.c`). The code
that polled or kicked hardware ([DMA](#terms) kicks, `GS_CSR` polling, the timers) is replaced by
the library that owns it. Two registers do move: the host loop sets `GS_CSR.FIELD` at each vsync
(`ico2/common/src/main.c` (`ico_vsync`)), and a vsync callback advances the EE timer counters while
their mode enables them (`port/platform/clock.h` (`ico_clock_timers_attach`)).

The EE's FPU rounds every single-precision result toward zero and flushes denormals to zero. The
simulation runs with the host FPU in the closest IEEE mode, round toward zero with flush-to-zero
and denormals-are-zero (MXCSR on x86-64, FPCR on arm64); host code (SDL, the renderer, the libc)
runs in the default mode (`port/platform/fpenv.h` (`ico_fpenv_sim_enter`,
`ico_fpenv_host_enter`)). The EE behaviours IEEE cannot express are `port/math`'s
([libm and float semantics](#libm-and-float-semantics)).

## EE kernel: threads, semaphores, interrupts

Every thread the game creates is a [fiber](#terms) on one host thread (`port/platform/fiber.h`).
A scheduler picks which one runs with the EE kernel's rules: strict priority 0 to 127 with no time
slice, one FIFO ready queue per priority, a readied thread preempting a lower-priority caller at
once, wakeup counts, FIFO semaphore waiters (`port/platform/sched.h` (`ico_sched_run`)). Threads
switch only inside kernel calls, as on the single-core EE, so the game's order of events is
reproduced and a run is a function of its input and the vsync count, save the sound driver's state
when the audio renders on the device's clock ([DIVERGENCES.md](DIVERGENCES.md) AU-8; a run with
the fixed clock, a trace's, renders it on the game's). The SDK names
(`CreateThread`, `WaitSema`, `iSignalSema`, `AddIntcHandler`, ...) map onto the scheduler in
`port/platform/kernel_host.c`; cache and interrupt-disable calls do nothing there.

There is no real interrupt. Once per vsync the host loop advances the vsync count, raises the
vblank-start [INTC](#terms) cause through the game's handler table, runs the registered on-vsync
callbacks as [interrupt code](#terms), renders the vsync's audio when the audio runs on the game's
clock, and then runs every thread that
became ready until all of them wait again (`port/platform/host_loop.c` (`ico_host_step`);
`port/platform/kernel_host.c` (`ico_kernel_raise_intc`)). The game's vblank handler wakes an event
thread, whose message wakes the game's `scheduler` thread, which wakes `Main` once every
`systemStatus[1]` vsyncs (`ico2/fumi/ios/message.c` (`signal_handler`, `iosMsgSetEvent`);
`ico2/common/src/main.c` (`scheduler`)); one pass of `Main` is a [Main tick](#terms), reported by
the game through `ico_host_main_tick` (`port/platform/host_loop.h`; `port/platform/trace_host.h`).
`VSync()` and libgraph's `sceGsSyncV` are busy waits on the EE; on the host they are
`ico_sched_spin_vsync`, which keeps lower priorities from running until the next vsync
(`port/platform/kernel_host.c` (`VSync`); `port/null/gfx_null.c` (`sceGsSyncV`)).
[PLATFORM.md](PLATFORM.md) has the rules, the contexts and the game's threads.

## IOP and SIF

There is no [IOP](#terms). The [SIF](#terms) calls the game makes (`sceSifCallRpc`,
`sceSifAllocIopHeap`, `sceSifSetDma`, the IOP reboot and module loads) are host functions with
nothing behind them: the module loads report success, and an RPC server is a host function
registered under the server id the EE binds to, run synchronously, so a call has finished by the
time `sceSifCallRpc` returns (`port/data/sif_host.h` (`ico_sif_register_server`);
`port/data/sif_host.c` (header comment)). The one server is the sound driver,
SNDN2DRV.IRX's replacement (`port/audio/sndn2_host.c` (`ico_sndn2_host_register`)).

The game still names IOP memory by IOP address: an int it gets from `sceSifAllocIopHeap` and
hands to `sceCdReadIOPm`, the sound driver and SIF DMA. Those addresses index a 2 MB array that
stands in for IOP RAM, with a first-fit heap above 0x10000; address 0 is never handed out, so the
game's "0 means failed" tests hold (`port/data/iop_ram.h` (`ico_iop_ram`, `ico_iop_heap_alloc`)).
The development kit's `host0:` file device maps to `<user folder>/dev/` for the developer tools
(`port/data/sifdev_host.c` (header comment)).

## CD/DVD

The game reads the disc through a [libcdvd](#terms) subset (`sceCdInit`, `sceCdRead`,
`sceCdSearchFile`, the `sceCdSt*` stream calls), defined by `port/data/cdvd_host.c` over the port's
read-only [VFS](#terms) (`port/data/cdvd_host.h`). A non-blocking command does its work when issued
and completes at the next simulated vsync, from an on-vsync callback (`port/data/cdvd_host.c`
(`ico_cdvd_host_vsync`)). The VFS serves one flat space of 2048-byte sectors with the disc's own
numbering, because the game does sector arithmetic of its own inside `DATA.DF` (`port/data/vfs.h`).

Two backends serve it. `ico_vfs_iso9660` reads the player's disc image in place: a plain ISO, a
raw CD image (`.bin` or the `.cue` that names it), or a CHD read with libchdr
(`port/data/iso9660.c` (header comment)). `ico_vfs_archive` reads [`ico.o2r`](#terms), a ZIP of
stored entries that keeps each file's disc sector range (`port/data/archive.h`). On the first run
the extractor verifies the image (the whole image's SHA-1, or the boot ELF's SHA-1 plus the CRC
of each `DATA.DF` member) and writes `ico.o2r` atomically; later runs mount the archive
(`port/data/extract.h`; `port/platform/main_host.c` (`mount_game_data`)). The `use_iso` setting
mounts the image directly, the default of the headless build (`port/platform/main_host.c`
(`use_iso_mode`)).

The binary holds no disc data. The 73 data-only members the PS2 build compiled from the ELF are
uninitialised arrays on the host, filled before the game starts from the boot ELF
(`SCES_507.60`, `SCPS_110.03`, `SCPS_560.01`) on the player's disc: each record is decoded field by field into the
host layout, PAL's for all three discs, through that disc's descriptor set, pointer words resolved
through an address registry, every row checked against a CRC first (`port/data/tables.h`
(`ico_tables_load_vfs`); `CMakeLists.txt` (the data tables section)).
[DATA.md](DATA.md) covers the disc, the archive and the tables.

## VU0

The game used [VU0](#terms) in macro mode, through inline assembly and Sony's libvu0, for vector and
matrix maths. On the host that is C: the libvu0 entry points are `port/math/libvu0.c`, written
from the instruction sequences of the reconstruction in `sce/libvu0`; the game's inline helpers
call `port/math/vector_inline.h` from their `ICO_HOST` bodies, with sums in the assembly's order
(`port/math/vector_inline.h`). The state the game kept in VU0 registers between calls, the
"current matrix" in vf4-vf7, is a global (`port/math/ico_math.h` (`ico_current_matrix`,
`ico_vu0_registers_push`)). `ico2/common/include/typedef.h` includes `ico_math.h`, so every game
source sees it. Where the VU's multiplier and adder give a result IEEE does not, the
`port/math/ps2float.h` models are used ([libm and float semantics](#libm-and-float-semantics)).
[MATH.md](MATH.md) has the details.

## VU1

[VU1](#terms) ran five [microprograms](#terms) that transform, light and clip the game's meshes,
skinned characters, grids and particles
(`ico2/vusrc/{normal_c,normal_l,cluster,mesh,particle}.vsm`). The host does not run them: nothing in
this tree assembles `ico2/vusrc` (the decompilation's PS2 build does), and the microprogram address
table is empty (`ico2/seki/src/MicroCode.c` (`MicroCodeAddress`)). In the window build each program
is a vertex shader: the GPU gets the [VIF](#terms) UNPACK payload the game built as the vertex
stream and the VU1 data memory the program reads as a constant block, and the `MSCALF` code the game
chose picks the shader entry and clip mode (`port/render/rd_mesh.h` (the programs table);
`port/shaders/vu_*.hlsl`). The game's DMA chains for VU1 are read on the host for their VIF codes:
register packets go to the GS register decoder, the program calls to rd (`ico2/seki/src/MicroCode.c`
(the `ICO_RD` section)).

Each program also has a CPU reference that does what the program's instructions do to one batch
and returns what it writes to the GS; these are the oracles the shader tests compare against, and
the renderer uses the particle one for its end-tag check (`port/render/vu1_ref/vu1_ref.h`).
[VU1_PROGRAMS.md](VU1_PROGRAMS.md) has the per-program detail.

## GIF and GS

On the PS2 the game's render layer (`ico2/seki`) builds 13 priority-ordered DMA chain lists per
frame and kicks them in order at `dl_Swap`; the [GS](#terms) is one state machine, so register
writes made by one list are still in force when the next starts. The port keeps both facts: a
frame is 13 ordered [lists](#terms) recorded by the game thread and [replayed](#terms) in order
against one persistent draw state, and draws are never sorted by material (`port/render/rd.h`
(header comment)).

In the window build the [GIF](#terms) packet helpers do not write packets; they feed every GS
register write to a decoder that turns it into rd state and primitives
(`ico2/seki/src/GifPacket.c` (`gsWrite`, `gif_HostWriteRegs`); `ico2/seki/include/GifHost.h`).
A [TEX0](#terms) write binds the texture a resolver returns, which `Texture.c` registers
(`ico2/seki/src/Texture.c` (`texHostResolve`)). Meshes skip the decoder and go to the VU1 shaders
(`port/render/rd_mesh.h`). In both builds the libgraph and libdma calls under the render layer are
the null floor: a DMA kick is a no-op and the GS is always idle (`port/null/gfx_null.c`).

[rd](#terms) (`port/render/`) records a frame from `rd_begin_frame` to `rd_end_frame` and replays
it onto the [RHI](#terms) (`port/render/rd_replay.c`). Coordinates stay in GS units (12.4 fixed
point about the 2048,2048 window centre) until the replay converts them under the GS sampling
rules and the resolution scale in force (`port/render/rd.h` (Coordinates)). The RHI is a small
interface the backends implement: Vulkan (`port/rhi/vk`), D3D12 (`port/rhi/d3d12`), D3D11 (`port/rhi/d3d11`), OpenGL (`port/rhi/gl`), and on macOS
and iOS Vulkan over MoltenVK, linked statically (`port/rhi/rhi.h`; `cmake/IcoMoltenVK.cmake`).
One build may link several backends; `rhi_create_backend` picks one (`port/rhi/rhi.h`
(`rhi_create_backend`); `port/rhi/rhi_backend.h` (header comment)).
[GS.md](GS.md), [RENDER.md](RENDER.md), [RHI.md](RHI.md) and [SHADERS.md](SHADERS.md) cover the
decoder, rd, the backends and the shaders.

## Display and timing

The game builds each tick for a [native frame](#terms): the GS window as wide as the display
aspect asks, with the PS2's height and vertical field of view, 512 GS pixels at 4:3 and up to
2560 (`port/include/ico_frame.h` (`ico_frame_width_for`)). The window build paces the simulated
vsyncs to real time (20 ms PAL, 16.683 ms NTSC), or under the [vsync lock](#terms) to a grid of
display refreshes, and presents as often as the frame rate option and vsync allow
(`port/platform/window_host.h` (`ico_window_pace`); `port/platform/pace_policy.h`
(`pace_lock_vsync`)); between ticks it can
[present](#terms) the last frame blended from the one before with
[interpolation](#terms), one tick behind, never feeding back into the simulation
(`port/render/rd_interp.c` (header comment)). The [presets](#terms), the
[scene scale](#terms), the pacing and the presenter are [DISPLAY.md](DISPLAY.md)'s.

## SPU2 and the sound driver

The game's sound thread calls Sony's Sg sequencer (`sce/libsndn2/sound.c` (compiled with its host
seams)), which talks to SNDN2DRV.IRX on the IOP by RPC. On the host that driver is a SIF server: RPC
0x65 runs the init packet, RPC 0x64 a tick (a page of 16-byte packets, then the ADPCM stream
scheduler, then a reply page the game reads), and every packet becomes the [libsd](#terms) calls the
IRX makes (`port/audio/sndn2_host.h` (`ico_sndn2_host_serve`)). The libsd calls drive a software
[SPU2](#terms): two cores of 24 ADPCM voices over 2 MB of sound RAM with reverb, written from public
hardware notes, integer arithmetic only, rendering 48 kHz stereo (`port/audio/spu2.h`;
`port/audio/spu2_sd.h`). The streams read the rings the game fills in the IOP RAM stand-in
(`port/audio/stream.c` (header comment)).

The SPU2 renders on the audio device's clock in the window build: SDL's audio thread renders what
the device asks for, under one lock with the driver. On the game's clock (the headless build, the
WAV dump, the fixed clock, `[dev] audio_sync`, no device) the host loop renders each simulated
vsync's frames (960 at 50 Hz) and hands them to the SDL3 device, a WAV file, or nothing. The
driver runs in every mode because the game reads its envelopes and stream offsets
(`port/audio/audio_host.h`, header comment). Values the SPU2 needs from Sony's modules
(SNDN2DRV's pitch table, LIBSD's reverb presets) are read from the player's disc
(`port/audio/sndn2_host.h`; `port/audio/libsd_irx.h`). [AUDIO.md](AUDIO.md) has the rest.

## IPU and movies

The PS2 movie player (`ico2/ito/mpeg`) demuxed PSS files and decoded MPEG-2 on the [IPU](#terms).
It is not compiled. `port/fmv/movie.c` keeps its entry points (`movie_init`, `movie_proc`,
`movie_end`) and return values, reads the same file through the same `sceCdSt*` calls, demuxes
the PSS container (`port/fmv/pss.h`), decodes the video with libmpeg2 (`port/fmv/m2v.h`), and
feeds the PCM audio to the sound driver's PCM channels with the same calls and buffer sizes the
PS2 player used. Timing follows the PS2 player's (two vsyncs a picture) and depends only on the
stream's picture count, so it is the same with or without a window (`port/fmv/movie.c` (header
comment)). The window build draws the pictures through `rd_video_frame`
(`port/fmv/rd_video.h`). [FMV.md](FMV.md) has the detail.

## libpad

`port/input/pad_host.c` is [libpad](#terms): one DualShock 2 in port 0, slot 0, when something feeds
it. A [pad script](#terms) comes first, then the live virtual pad the binding layer fills each vsync
from gamepads, keyboard, mouse and touch; with neither, no controller is plugged in. The pad answers
the game's mode and actuator queries so that its own state machine (`ico2/fumi/ios/pad.c`
(`controler_stable_check`)) brings it up in analog mode as on hardware (`port/input/pad_host.c`
(header comment, `scePadRead`)). The layers above it are listed in `port/input/input.h`.
[INPUT.md](INPUT.md) has them.

## libmc

`port/save/mc_host.c` is [libmc](#terms) over a host folder: port 0 is a formatted 8 MB card whose
root is the card folder, port 1 a second card when one is configured. Files are stored byte for byte
in the folder layout PCSX2's folder memory cards use. A request does its work when issued and
completes at the next vsync, as disc reads do; a file open for writing is written to a temporary
copy and moved over the original at close (`port/save/mc_host.c` (header comment, `sceMcSync`)).
[SAVES.md](SAVES.md) covers the folders and the import of existing cards.

## libm and float semantics

The simulation's FP mode handles rounding and denormals; the EE behaviours left over are written out
by hand. `port/math/ps2float.h` holds models of the EE's and VU's division, multiplier, adder,
square root, reciprocal square root and float-to-int conversion (`ps2_div`, `ps2_mul`, `ps2_add`,
`ps2_sub`, `ps2_sqrt`, `ps2_rsqrt`, `ps2_ftoi`), which the sites
[MATH.md](MATH.md#where-the-models-are-used) lists call. The EE has no `double` hardware; the game's
few `double` expressions go through an integer model of libgcc's soft float
(`port/math/softdouble.h`). The libm functions the game calls are host copies of the EE's newlib
single-precision code, with the additions and subtractions of the trigonometric ones through the EE
adder model (`port/math/newlib/ico_libm.c` (header comment)). [MATH.md](MATH.md) lists every
function and where it is used.

## Seven discs, one program

The port is built to play more than one release of the game from one compiled program: the
European disc (SCES-50760), the Japanese disc (SCPS-11003), the Korean disc (SCPS-56001), the
North American disc (SCUS-97113), the August 2001 preview disc, the June 2001 magazine demo
(SCUS-97158) and the Asian disc (SCPS-55001), listed in `config/regions.txt`.
Their differences are inline blocks in the decompilation's one tree; here each becomes a run-time
choice on the game's region index, `ico_region`, which is set once, when the disc image is
identified, and is the European disc's 0 until then. The game code reads it through
`ICO_<SLUG>` and the selector macros of `ico2/common/include/region.h`, the port's own code through
`port/core/region.h`'s per-disc tables; one in-memory layout, the European one, serves all seven.
[GAME_CODE.md](GAME_CODE.md#regions) has the forms and the rule for adding a target.

## The port's own additions

Beside the PS2 stand-ins, the port adds features of its own: the Settings menu and the other
menus (`port/ui/`), the options and their hooks into the game, achievements, photo mode, the
model viewer, the Extras credits (`port/game/`), `config.toml` (`port/config/`), texture and model
packs (`port/render/texpack.c`, `port/render/modelpack.c`), and the picture options (presets,
aspect, resolution, anti-aliasing, the CRT filter). The game calls them through hooks at named
places in `ico2/`, each marked "PC port" in a comment (for example `ico2/common/src/main.c`,
`Main`: `ui_mouse_tick`, `ico_mv_tick`, `ico_photo_view_tick`). [UI.md](UI.md),
[GAME_FEATURES.md](GAME_FEATURES.md), [CONFIG.md](CONFIG.md) and [PACKS.md](PACKS.md) cover them;
[DIVERGENCES.md](DIVERGENCES.md) lists where the port's game differs from the PS2's.

## Where to read next

[README.md](README.md) lists the documents in reading order.

## Terms

The one glossary of `docs/port/`. Other documents link here and do not redefine these words.

**PS2 hardware and libraries**

- **EE** (Emotion Engine): the PS2's main CPU, a MIPS R5900 with 32 MB of RAM, an FPU and the
  two vector units. The game's C ran on it. Here: the host CPU, the [arena](#terms) and
  `port/platform`.
- **IOP** (I/O processor): the PS2's second CPU, with 2 MB of RAM, running Sony's driver modules
  (IRX files) for the disc, sound, pads and memory cards. Not present here; its RAM is
  `port/data/iop_ram.h`'s stand-in.
- **VU0**: the vector unit the EE used for maths, here through libvu0 and inline assembly. Here:
  C in `port/math`.
- **VU1**: the vector unit that ran the microprograms transforming geometry for the GS. Here: one
  shader per microprogram family, with CPU references for the tests.
- **microprogram**: a VU1 program from `ico2/vusrc/*.vsm`; the game picks one and starts it with
  an `MSCAL`/`MSCALF` code.
- **GS** (Graphics Synthesizer): the PS2's rasteriser, driven entirely by register writes. Here:
  rd and the RHI.
- **GIF**: the GS interface that turns packets (a GIF tag and its data) into GS register writes.
  Here: the register decoder in `ico2/seki/src/GifPacket.c`.
- **DMA**: the EE's channels that moved packets to the VIF, GIF, IPU and SIF. Here: a no-op
  (`port/null/gfx_null.c`); the packets are read in place.
- **VIF**: the unit in front of each VU that unpacks data into VU memory and starts programs.
- **SIF**: the EE-IOP link: RPC calls, IOP heap allocation and DMA. Here: `port/data/sif_host.c`.
- **SPU2**: the PS2's sound processor, two cores of 24 ADPCM voices. Here: `port/audio/spu2.c`.
- **IPU**: the EE's MPEG-2 decoder unit, used by the movie player. Here: libmpeg2 in `port/fmv`.
- **libcdvd, libmc, libpad, libsd**: Sony's libraries for the disc drive, memory cards, pads and
  (on the IOP) the sound processor. Here: `port/data/cdvd_host.c`, `port/save/mc_host.c`,
  `port/input/pad_host.c`, `port/audio/spu2_sd.c`.
- **INTC**: the EE's interrupt controller. Here a handler table; the host loop raises the
  vblank-start cause once per vsync, and no other cause is raised (`ico2/common/src/main.c`
  (`ico_vsync`); `port/platform/kernel_host.h` (`ico_kernel_raise_intc`)).
- **vsync**: one video field, 50 a second in PAL and 59.94 in NTSC. The unit of simulated time: the
  host loop runs one step per vsync (`ico_host_step`).
- **TEX0, TEXA, FBMSK**: GS register fields: TEX0 names the texture (base, format, CLUT, texture
  function), TEXA the alpha that 16- and 24-bit texels expand to, FBMSK (in FRAME) the
  framebuffer bits a draw may not write (`port/render/rd_state.h`).

**The simulation**

- **Main tick**, **tick**: one pass of the game's `Main` loop, normally every second vsync (25
  a second in PAL); the game's frame. Counted from 0 by `ico_host_main_tick`
  (`port/platform/trace_host.h`).
- **fiber**: a coroutine with its own stack; each game thread is one, and the scheduler is the only
  one that resumes them (`port/platform/fiber.h`).
- **host context**: code not on a game fiber: the scheduler's caller, the vsync callbacks and the
  host's own work (`port/platform/sched.h` (Contexts)).
- **interrupt code**: host-context code run as the EE ran interrupt handlers; it may wake threads
  with the `i` calls (`iWakeupThread`, `iSignalSema`) but never switches (`port/platform/sched.h`
  (Contexts)).
- **arena**: the host block that stands in for the EE's 32 MB of RAM (`port/platform/arena.h`).
- **partition**: one of the game's heap regions (stage, common, motion, ...), carved from the
  arena by the game's allocator (`ico2/fumi/ios/ios.c`).
- **host extra**: the bytes a partition has on the host beyond its EE size
  (`ICO_HOST_HEAP_EXTRA`).
- **VFS**: the port's read-only view of the disc by sector number (`port/data/vfs.h`).
- **ico.o2r**: the archive of the game's disc files the first run extracts from the player's
  image (`port/data/archive.h`).

**Builds and runs**

- **window build**: `ico_pc` built with `ICO_HEADLESS=OFF`: the window, rd, audio output. The
  build players run (`CMakeLists.txt` (`ICO_HEADLESS`)).
- **headless build**: `ico_pc` built with `ICO_HEADLESS=ON`: no window or renderer, vsyncs as fast
  as the CPU allows; used for traces and tests (`CMakeLists.txt` (`ICO_HEADLESS`)).
- **pad script**: a text file of Main ticks and pad states that drives the pad instead of a
  person (`port/input/pad_script.h`).
- **trace**: a text file with one line of game state per Main tick: stage, story flags and a
  hash of the save buffer (`port/platform/trace_host.h`).
- **user folder**: the per-user folder for settings, saves and dumps (`ico_host_pref_dir`,
  [PLATFORM.md](PLATFORM.md)).

**Rendering and display**

- **rd**: the port's renderer API the game's render layer draws through
  (`port/render/rd.h`).
- **RHI**: the render hardware interface under rd that the Vulkan, D3D12, D3D11 and OpenGL backends implement
  (`port/rhi/rhi.h`).
- **frame slot**: one of the RHI's `RHI_FRAMES_IN_FLIGHT` (2) sets of per-frame resources;
  `rhi_wait_frame` waits for the frame last submitted in a slot before the slot is reused
  (`port/rhi/rhi.h` (`rhi_frame_slot`)).
- **list**: one of the 13 ordered command lists of a frame, numbered by the game's display-list
  priorities (0 opaque world, ..., 11 UI, 12 debug font; `port/render/rd.h`, Lists).
- **replay**: rd walking a recorded frame's lists in order and issuing them to the RHI
  (`port/render/rd_replay.c`).
- **present**: putting the replayed picture on the screen (`port/render/rd_interp.c`
  (`rd_present`); the box blit in `port/render/rd_present.c`).
- **vsync lock**: the pacing in which the display's refresh drives the simulated vsyncs: the game
  steps on a grid of n refreshes and each step is presented n times (`port/platform/pace_policy.h`
  (`pace_lock_vsync`); [DISPLAY.md](DISPLAY.md#the-vsync-lock)).
- **interpolation**: presenting between ticks by blending each keyed draw of the last frame from
  its match in the frame before (`port/render/rd_interp.c`).
- **native frame**: the GS window a tick is built for, as wide as the display aspect asks with the
  PS2's height; 512 GS pixels at 4:3 (`port/include/ico_frame.h`).
- **Full pixel**: `[video] full_pixel`: the scene drawn at the window's own pixel width, so one
  texel is one output pixel and nothing is cropped (`port/game/video_options.h` (`fullPixel`);
  [DISPLAY.md](DISPLAY.md#the-native-frame)).
- **preset**: `[video] preset`, `original`, `enhanced` or `custom`: a shortcut over the display
  options (`port/game/video_options.h`).
- **out of the box**: the Original preset on the 4:3 frame at 1x, the PS2's picture. In this state
  the renderer reproduces the PS2's behaviour; any other state draws what a game made for that
  picture would draw (`ico2/seki/src/GifPacket.c` (`gif_HostExtended`);
  `port/game/video_options.h` (`ico_video_original`)).
- **region rule**: what the port draws of a VU1 triangle with a vertex outside the GS region: the
  PS2 drops such a triangle; out of the box the port drops it too, and otherwise draws it clipped
  unless it belongs to an artist's card at the picture's edge (`port/shaders/vu_common.hlsli`
  (`vu_region_rule`)).
- **scene scale**: the factor between the scene's texture and the native frame's GS size
  (`port/render/rd.h` (`RdSettings.sceneScale`)).
- **.rddump**: a file holding one recorded rd frame, written by F12 or `dump_every`
  (`port/render/rd_dump.c`).
- **replay tool**: `rd_replay_tool`, which renders an `.rddump` to a PNG without the game
  (`port/render/tools/rd_replay_tool.c`).
