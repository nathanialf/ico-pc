# Audio

`port/audio/` holds the whole sound path of the port:

- a clean-room software SPU2, the PS2's sound processor rendered on the
  host as 48 kHz interleaved stereo 16-bit, with the SPU ADPCM decoder and
  a libsd-shaped front end;
- the game's Sg sequencer, the EE side of the sound library, made 64-bit
  clean;
- a host replacement for SNDN2DRV.IRX, the IOP sound driver behind the
  sequencer's RPC, with its ADPCM and PCM stream engines, following
  docs/research/sndn2drv.md;
- the output: one SPU2 render per simulated vsync into an SDL3 audio
  stream (window build), a WAV file (`audio_dump`), or nowhere (headless).

| file | contents |
|---|---|
| `adpcm.h`, `adpcm.c` | the SPU ("VAG") ADPCM block decoder: 16-byte blocks, 28 samples, five filter pairs, loop flags; also used for the `.int` streams |
| `spu2.h`, `spu2.c` | the SPU2: 2 cores x 24 voices, 2 MB sound RAM, envelopes, volumes and sweeps, noise, mixing, reverb, transfers, AutoDMA input, IRQ |
| `spu2_tables.c` | the Gaussian interpolation table, the reverb resampling FIR, the built-in reverb presets (transcribed from psx-spx; the disc's replace them at start) |
| `spu2_internal.h` | tables and the envelope step, shared with the tests |
| `spu2_sd.h`, `spu2_sd.c` | libsd's calls (`sceSdSetParam`, `SetSwitch`, `SetAddr`, `SetCoreAttr`, `SetEffectAttr`, `VoiceTrans`, `Init`) with libsd's encodings, as register writes |
| `sce/libsndn2/sound.c`, `sound.h` (repository root) | the EE Sg sequencer the game calls (`libsndn2.a(sound.o)`, the decomp's clean-room reconstruction, MIT), with the host seams made in place |
| `sndn2_host.h`, `sndn2_host.c` | the SNDN2DRV host: RPC entry points, packet dispatcher, reply pages, pitch table |
| `libsd_irx.h`, `libsd_irx.c` | the disc's libsd values: `LIBSD.IRX`'s reverb presets, work area sizes and idle voice block, read at start |
| `stream.c`, `sndn2_internal.h` | the ADPCM stream engine (records, event queue, refill scheduler) and the PCM mixer |
| `audio_host.h`, `audio_host.c` | the per-vsync render and its sinks; `wav.c` is the dump writer, `out_sdl.c` the SDL3 device and its choice (window build), `volume.c` the output volume |
| `mix_gain.h`, `mix_gain.c` | the music, effects and film gains on the voices' volume registers and the PCM mixer, with each voice slot's category |
| `test/` | the unit tests and the render benchmark (below) |

The build has two static libraries: `ico_audio` (the SPU2) and `ico_sndn2`
(the rest; it links `ico_audio`, `ico_port_data` and `ico_platform`, and
SDL3 in the window build). `ico_pc` links `ico_sndn2`; the host loop
(`port/platform/host_loop.c`) calls `ico_audio_host_init` before boot and
`ico_audio_host_vsync` once per vsync.

## The SPU2

### State and time

There is one global SPU2; `spu2_reset()` returns it to power-on (RAM and
registers zero). Time is the output frame counter at 48 kHz
(`spu2_time()`). `spu2_write_reg(core, reg, value, time)` queues a write,
and `spu2_render()` applies every write whose time has come before
rendering that frame, in time order and, for equal times, in call order. A
time already rendered means "before the next frame". A caller that stamps
every write of one IOP tick with the same time therefore gets
block-accurate timing, and one that stamps finer gets sample-accurate
timing. `spu2_apply_pending()` applies the queue at once. Reads
(`spu2_read_reg`) see the state after the last rendered frame.

Addresses are SPU2 halfword addresses (20 bits); libsd's byte addresses
are converted in `spu2_sd.c`. Sound RAM is `uint8_t[2 MB]` of
little-endian halfwords, reached through `spu2_ram()`.

### One frame

For core 0, then core 1:

1. Each voice: the sample is the 4-tap Gaussian interpolation of the last
   four decoded samples at pitch-counter bits 4-11 (or the core's noise
   level if NON selects it), times the ADSR level (`>> 15`), times the
   left/right volume (`>> 15`), added to the dry buses (VMIXL/VMIXR) and
   wet buses (VMIXEL/VMIXER). Then the envelope and volume sweeps step, and
   the pitch counter advances by PITCH (with PMON scaled by the previous
   voice's output + 0x8000, clamped to 0x4000), feeding one decoded sample
   to the interpolator per 0x1000. Decoding fetches the next 16-byte block
   at a block end: loop end jumps to LSAX and sets ENDX, end without repeat
   also forces release at level 0, loop start latches LSAX. Voices
   free-run (keep decoding) after their envelope ends, as on the hardware.
2. The noise generator steps (ATTR bits 8-13).
3. Buses saturate to 16 bits. MMIX selects, separately for dry and wet and
   left and right, the voices, the external input (core 1 only: core 0's
   output through AVOL) and the AutoDMA input (through BVOL).
4. The wet sum enters the reverb: the 39-tap FIR filters it, every second
   frame the reverb unit runs one 24 kHz step over the work area ESA..EEA,
   and its output is zero-stuffed back to 48 kHz through the same FIR
   (gain 2), scaled by EVOL and added to the dry sum.
5. The master volume (with sweep) gives the core's output. Core 1's output
   is the frame's output; core 0's goes on to core 1.
6. The write-back areas get this frame's samples (voices 1 and 3 of each
   core, core 0's output, each core's dry and wet sums, at the halfword
   addresses the PCSX2 wiki lists).

This order is the reference (`frame()` in `spu2.c`). `spu2_render`
produces the same bits faster by running most frames in chunks, voice by
voice ("Render cost" below).

### Register map

Offsets follow ps2sdk's `spu2_mmio_hwport.h` layout of a core (core 1's
bank selected by the `core` argument; the 0x760 block uses core 0's
offsets).

| range | registers | behaviour |
|---|---|---|
| 0x000-0x17F | VOLL, VOLR, PITCH, ADSR1, ADSR2, ENVX, VOLXL, VOLXR x 24 | all; VOL with sweep modes; ENVX readable and writable; VOLX read the current volumes |
| 0x180-0x197 | PMON, NON, VMIXL, VMIXEL, VMIXR, VMIXER | all |
| 0x198 | MMIX | all 12 bits |
| 0x19A | ATTR | bit 7 reverb write enable, bit 6 IRQ enable, bits 8-13 noise clock; bits 14/15 stored, not acted on (A10) |
| 0x19C | IRQA | voice block reads, transfers, write-backs (A12) |
| 0x1A0, 0x1A4 | KON, KOFF | per half-word write |
| 0x1A8, 0x1AC | TSA, DATA | manual transfer: DATA stores at TSA and advances it |
| 0x1B0 | ADMAS | stored only; AutoDMA input is `spu2_memin_start` (A11) |
| 0x1C0-0x2DF | SSA, LSAX, NAX x 24 | SSA used at key on; LSAX read/write; NAX read (A7), a write moves the voice |
| 0x2E0-0x33B | ESA and the 22 reverb address registers | all; an ESA write resets the buffer position |
| 0x33C | EEA | end address bits 16-19 |
| 0x340 | ENDX | read; a write sets it (libsd writes 0 to clear) |
| 0x344 | STATX | bit 10 transfer busy, bit 6 IRQ |
| 0x760-0x787 | MVOL, EVOL, AVOL, BVOL, MVOLX, the ten reverb volumes | all; MVOL with sweep; MVOLX reads the current level |

Not covered: S/PDIF and the 0x7C0 block (no host meaning), the PS1-mode
capture buffers, and `sceSdProcBatch` (SNDN2DRV does not use it).

### libsd front end

`spu2_sd.c` turns libsd's calls into register writes stamped with the time
set by `spu2_sd_set_time()`. The argument encodings are libsd's ABI as
ps2sdk's `libsd-common.h` spells them (voice selector `core | voice << 1`,
`SD_VPARAM_*`, `SD_PARAM_*`, `SD_SWITCH_*`, `SD_ADDR_*` byte addresses,
`SD_CORE_*`). Behaviour follows ps2sdk's clean-room libsd (`freesd.c`,
`effect.c`, `voice.c`), checked against the disc's `LIBSD.IRX` where it
matters (next section):

- `sceSdInit`'s register writes (`spu2_sd_init`; DIVERGENCES A14), read
  off the disc's code;
- the effect preset table and work area sizes (A9), read from the disc's
  data at start; the built-in fallback is psx-spx's presets, whose sizes
  are exactly libsd's for all ten modes, which is how the mode order (off,
  room, studio 1-3 = small/medium/large, hall, space, echo, delay, pipe =
  half echo) was matched;
- `sceSdSetEffectAttr` with a stale `delay`/`feedback`, which is ignored
  here; the game never sends them;
- `sceSdVoiceTrans`'s busy check: refused while the channel has a transfer
  queued or running.

`sceSdSetEffectAttr` places the work area as libsd does: ESA = end address
- size + 2 bytes, where the end address is `EEA << 17 | 0x1FFFF`; the
preset's address registers (PS1 units of 8 bytes) are written as halfwords
(x4), so byte offsets are the PS1 ones.

### libsd values (`libsd_irx.c`)

`LIBSD.IRX` (`/LIBSD.IRX;1`, 26,285 bytes, SHA-1
`49bbaf50d622b04c02dd645fde1b33972b16567f`) was disassembled with
`mips-linux-gnu-objdump -d -r -m mips:3000` (the module is linked at 0;
`.text` at file offset 0xB0, `.data` at 0x4240 for address 0x4190). The
export table at the start of `.text` gives the entry points by ordinal
(4 `sceSdInit` 0x1420, 23 `sceSdSetEffectAttr` 0x900). Unlike SNDN2DRV it
is compiled with optimisation.

**Data, read at start.** `ico_sndn2_host_register` calls `ico_libsd_load`
after the pitch table: it reads `LIBSD.IRX` from the mounted disc (the
image, or the archive, which holds every root `*.IRX`), finds `.data` by
its section name, and takes three tables by module address, each
identified by the code that reads it:

| address (file offset) | what | identified by |
|---|---|---|
| 0x41C8 (0x4278) | 10 x u32: each mode's work area size in 8-byte units | `sceSdSetEffectAttr` (0x9B8-0x9D4): `ESA = end - (sizes[mode] * 8 - 2)`; `sceSdInit`'s cold path (0x1384) with mode 0 |
| 0x41F0 (0x42A0) | 10 x 0x44 bytes: a u32 (0 for every mode; ps2sdk's `mode_flags`) then the 32 registers in psx-spx's order | `sceSdSetEffectAttr` (0x9E4-0xA08) copies 0x44 bytes from `0x41F0 + mode * 0x44` |
| 0x4A60 (0x4B10) | the idle voice block, 16 bytes | `InitVoices` (0x3BA0) writes 16 halfwords from it to the data port at TSA 0x2800 (byte 0x5000) |

The values are installed with `spu2_reverb_set_preset` (size = units x 8)
and `spu2_sd_set_idle_block`. Checks before they are used: `.data` holds
the three tables; every flags word is 0; every preset's address
registers (x8) lie inside its work area; modes 1-9 have vLIN = vRIN =
0x8000; the idle block's flag byte loops on itself (end and repeat).
Anything else, no disc, a missing file or another size keeps the built-in
values and logs one line (`libsd: ...; using the built-in ...`); the log
also names the modes in which the disc's values differ from the built-in
ones.

**What the disc holds.** The presets and sizes equal ps2sdk's `effect.c`
`EffectParams` and `EffectSizes << 4` bit for bit for all ten modes. Against
the built-in psx-spx table they differ in modes 0 (off: unused address
words), 1 (room: comb 3/4 and diffusion addresses), 7 (echo) and 8 (delay:
APF sizes and most addresses); studio 1-3, hall, space and pipe are
identical, and so is STUDIO_3, the only mode the game selects. The idle
block is 16 bytes of 0x07 (shift 7, filter 0, flags loop start + repeat +
end, every nibble 7), ps2sdk's `VoiceDataInit`; `InitVoices` writes 16
halfwords from it, so the 16 bytes after it in memory (the start of
`.bss`, zero at the first `sceSdInit`) go to byte 0x5010.

**Code, transcribed.** `sceSdInit(flag)` (0x1420), the order the host
follows (`spu2_sd_init`): with `flag & 0xF` = 0 (the game's `SgInit`),
`ResetAll` (0x1A00): every voice keyed off (KOFF 0xFFFF in both halves),
PMON and NON cleared on core 1 only (the loop does not step the register
base), then each core's ESA set to mode 0's area below the end address the
core has at that moment (before `InitCoreVolume` writes EEA); then
`InitVoices` (0x3BA0): the idle block, every voice's VOLL/VOLR 0, PITCH
0x3FFF, ADSR 0, SSA byte 0x5000, KON then KOFF of all 24 voices on both
cores, ENDX of core 0 only cleared; then `InitCoreVolume(flag & 0xF)`
(0xE8C): ATTR 0xC000/0xC001 (0xC080/0xC081 with a non-zero flag), VMIXL,
VMIXR, VMIXEL, VMIXER all on, MMIX 0xFF0/0xFFC, and with flag 0 MVOL and
EVOL 0 and EEA 0xE/0xF; AVOL 0 on core 0 and 0x7FFF on core 1; BVOL 0.
This is ps2sdk's `sceSdInit` write for write. Not modelled: the SPDIF,
SSBUS and DMA control registers, the transient ATTR writes and STATX waits
of the manual transfer, the writes at 0xBF900B60 (outside the modelled
register file), and the delays between the KON and KOFF writes.

### Interface

- Lifecycle: `spu2_reset()`, then `spu2_sd_init(hot)` for SNDN2DRV's
  command 0x1E.
- Per IOP tick: `spu2_sd_set_time(t)` with the frame the tick lands on,
  then the libsd calls the packet dispatcher makes (`spu2_sd_set_param`,
  `_set_switch`, `_set_addr`, `_set_core_attr`, `_set_effect_attr`,
  `_voice_trans`), then the reads for the reply page
  (`spu2_sd_get_param(V(ENVX))`, `spu2_sd_get_addr(V(NAX))`) and
  `spu2_sd_voice_trans_status(1, 0)` for the stream queue.
- Rendering: `spu2_render(out, frames)` produces 48 kHz stereo. Everything
  is single-threaded; the caller serialises render and register calls.
- Transfers: `spu2_set_trans_callback(chan, cb, user)` is the DMA-done
  interrupt (SNDN2DRV's handler writes the transfer counter into the reply
  page there); `spu2_set_dma_rate()` picks the transfer speed (A8).
- PCM (FMV) input: `spu2_memin_start(1, ring, cb, user)` with the IOP
  staging ring in libsd's 0x800-byte layout; `cb` is the half-done
  callback, and `spu2_memin_half()` is
  `sceSdBlockTransStatus(1, 0) & 0x01000000`. BVOL and MMIX come through
  `spu2_sd_set_param`.
- Reverb presets: `spu2_reverb_set_preset(mode, p)` replaces a built-in
  preset (`ico_libsd_load` does so with the disc's); `spu2_reset` restores
  the built-in ones. `spu2_sd_set_idle_block` sets the block `spu2_sd_init`
  writes.
- The 608-entry pitch table belongs to the driver host, not the SPU2: the
  SPU2 takes the PITCH word the driver computes.

## The sequencer on the host

The sequencer's source is `sce/libsndn2/sound.c` and `sound.h`, the
decomp's reconstruction with the host changes made in place (the head
context's type is the macro `SG_HEAD_T`, where the EE had `int`).

`port/compat/sound.h` includes `sce/libsndn2/sound.h` (the host view: `long long` for
the EE's 64-bit `long` in `SgStPcmBufMode`, and the `SgSetSePitchDirect`
prototype the EE header leaves out). None of the game's `soundSe*`,
`Adpcm*` or raw `Sg*` call sites change.

The seams (the comment at the top of `sound.c` has the details):

| EE | host |
|---|---|
| the head context, 16 `int` words holding the five pointers the event decoder shares (program block, tone record, channel table, `.hd` header, event cursor), read as `int *`, `char *` and `unsigned char **` in different functions | `unsigned char *sgHeadContext[16]`; each function's view uses the pointer array |
| vab context word 0 = the `.hd` address | a nonzero token in the word, the address in `sgVabHd[vab]` |
| sequence context word 2 (+8) = the `.sq` body or the SE table entry | a token, the address in `sgSeqBody[seq]`; every `memset` of a context clears the token, so readers see NULL exactly where the EE saw 0 |
| `SgVabOpenFakeBody` writes the header and the words at 0x10, 0x18-0x24 into the header's 0x30, 0x38-0x44 | `sgHdRel[vab]`, a record keyed by the header address (looked up by header, among open vabs); the header is never written (docs/port/LOADERS.md, "hd and sq") |
| `sgIop2EeBuf | 0x20000000` (uncached alias) | `ICO_UNCACHED`, the identity |
| `_SgGetPacketCntext`: `page * 0x1000 + (int)p` | pointer arithmetic |
| `SgGetDmaTransferStatus(1)` spins on the reply's +0x1C0 | the spin calls `ico_sched_spin_vsync()` each pass, because the reply that ends it comes with the sound thread's next tick, which a spinning fiber would otherwise block. On the host context (tests) it returns 0 |
| `SgSndn2RemoteInit` binds to the IRX's server | registers the host server first (`ico_sndn2_host_register`) |

## The SNDN2DRV host

`sndn2_host.c` is a host SIF server (`port/data/sif_host.h`) under the
IRX's id 0x736E646E. RPC 0x65 runs one packet (the init, `{0x1E, hot}`) and
returns a zeroed page with the return word; RPC 0x64 runs a tick: every
16-byte packet in order, then the ADPCM refill scheduler and event queue,
then the reply page. Each packet becomes the libsd calls the research read
off the IRX, on `spu2_sd_*`.

### Commands

All 38 command ids the research documents are handled, including 0x22,
which the EE never sends, and 0x3D, which does nothing in the IRX. The
rest of the IRX's jump table (1 to 0x4F) does nothing, and the host logs
the first such id once.

| group | ids | host |
|---|---|---|
| voices | 0x01 VOL, 0x02 ADSR, 0x03 SSA, 0x04 pitch | `SetParam`/`SetAddr` on `V(slot)` |
| masks | 0x0A KON, 0x0B KOFF (id ignored), 0x0C VMIXEL+VMIXER, 0x0D NON | `SetSwitch`, core 0 from w2, core 1 from w3 |
| core | 0x14 EEA, 0x15 type (clear bit dropped, depth 0, effect enable), 0x16 EVOL, 0x17 delay, 0x18 feedback, 0x28 MVOL, 0x32 id 8 noise clock (core = slot / 24), id 10 S/PDIF | as the IRX; 0x17/0x18 re-apply the last 0x15 attr twice (A20) |
| init | 0x1E `sceSdInit(hot)`, VMIXEL/R cleared, DMA 0 callback; 0x1F callback off | |
| sample RAM | 0x20 write, 0x21 read (IOP address into `ico_iop_ram`), 0x22 status into the return word | the counter `w1 >> 8` is kept; DMA 0's done callback writes it into both pages at +0x1C0 |
| ADPCM streams | 0x3C init, 0x3D quit, 0x3E open, 0x3F close, 0x40 volume, 0x41 pitch, 0x42 play, 0x43 stop | `stream.c` (below) |
| PCM streams | 0x46 init, 0x47 quit, 0x48 open, 0x49 close, 0x4A volume, 0x4B play, 0x4C stop, 0x4D seek, 0x4E effect (MMIX), 0x4F buffer mode | `stream.c` |

### Reply pages

Two 0x200-byte pages alternate with a counter, as in the IRX: words
0x000-0x0BF hold ENVX & 0x7FFF of the 48 voices (slot = core * 24 +
voice), 0x0C0-0x17F the stream records' IOP read offsets, 0x180-0x1BF the
PCM channels' read offsets, 0x1C0 the transfer counter (written only by the
DMA callback, into both pages), and the rest zero. The host SIF copies the
returned page into the EE's `sgIop2EeBuf` before `sceSifCallRpc` returns.
The EE cannot tell this from the PS2's asynchronous reply, because it reads
the page only after `SgSndn2RemoteSync`.

### Timing

The driver ticks once per EE vsync, on the sound fiber, as on the PS2. The
SPU2 renders the vsync's frames in the host step (`ico_audio_host_vsync`)
after the vsync interrupt handlers and before the fibers run, so a tick's
register writes (stamped `spu2_time()`) take effect at the first frame of
the next block, and the ENVX and NAX it reads are the state at the end of
the block just rendered (A17). Voice transfers complete one frame after
they start (A19), so a 0x20 issued in tick N reaches the EE's +0x1C0 in
the reply of tick N+1. All of it is a function of the vsync count, which
keeps headless runs deterministic.

### Pitch table

Command 0x04 computes the PITCH word as the IRX does (32-bit unsigned
arithmetic, the 441/480 step, the scale, `& 0xFFFF`) from a 608-entry
table. `ico_sndn2_pitch_load` reads it at registration from the mounted
disc: `SNDN2DRV.IRX` (20,941 bytes), file offset 0x3900, 0x4C0 bytes,
accepted when T[208] = 0x1000. Without the disc it falls back to
`floor(4096 * 2^((i - 208) / 192))`, logs once, and DIVERGENCES A15
applies. The index is clamped and counted, with one log line (A21).

The disc's table differs from that formula in 32 entries, 30 of them by
one. Entry 64 is 2360 (the formula gives 2435) and entry 172 is 3340
(3596), each below its predecessor: hand-made data with two outliers. The
table is therefore not monotonic, and the loader does not check order;
this is why the port reads the table from the disc rather than generating
it.

## Streams (`stream.c`)

### ADPCM `.int` streams

The game reads a stream's file into IOP RAM itself (`fumi/sound/
adpcm_init.c` through `iosCdvdBackGroundReadIOPm`, that is `sceCdReadIOPm`
into `ico_iop_ram`, docs/port/DATA.md) and refills the ring behind the
read offset it polls. The host engine follows the IRX:

- 0x3E fills the 36-byte record (channels `n` from the attr byte, the SPU
  ring start and size from the `AdpcmChReq` field the decompilation calls
  `vol`, the IOP ring start and size) and sets SSA and ADSR 0x8080/0x808A.
- 0x42 queues a FILL of the first half (loop-start flags) per voice, then
  one KEYON for the masks. The 128-entry queue runs at most one transfer
  per tick, on DMA channel 1, and only when the channel is idle, so a
  stereo play keys on in the third tick (fill, fill, key on), and the IOP
  read offset moves on by `spu_size / 2 * n`, modulo the IOP ring, when the
  next tick finds the channel idle (one tick after the DMA).
- Every tick each active voice's NAX is compared with its ring's halves;
  entering the other half queues a FILL of the half it left, from the
  current read offset.
- A FILL de-interleaves `len / (0x800 / n)` chunks of `0x800 / n` bytes
  from `src`, `src + 0x800`, ... into the staging buffer and patches the
  flag byte of the first and last block (half 1: 2 and 3; half 2: 6 and
  2), so the voice loops over the whole ring and re-latches LSA at its
  start. A chunk outside IOP RAM is zero-filled and logged once (a
  4-channel stream reads past its ring; IOP RAM continues there on the
  host as on the PS2).
- 0x43 cancels each voice (0x248C, docs/research/sndn2drv.md, "Stream
  cancel"), clears its record's state and keys it off; 0x3F cancels and
  clears. A cancel forgets the voice's pending fill (the read offset does
  not advance for it), zeroes its queued FILLs and clears its bit in any
  queued KEYON; for a core 0 voice the IRX also clears the same bit in the
  KEYON's core 1 mask (a missing `break`), which the host reproduces.
- A full queue (128 events) drops the new event, as the IRX's enqueue does.

### PCM streams (film audio)

0x46 points core 1's AutoDMA input at the staging buffer (the IRX shares
its 0x2000 bytes with the ADPCM fills; the PCM ring is its first 0x800),
sets BVOL 0x7FFF and installs the half-done callback. On each half-done
interrupt the half the input is not reading is zeroed and the 16 channels
are mixed into it: 256 left samples then 256 right, each sample
`(vol * x) >> 15` added with 16-bit wrap-around, `x` read at byte offset
`((2 * i) >> shift) * 2`, the read offset wrapping at the buffer size,
advancing by the open's step and stopping at the buffer-mode stop offset.
The port's film player (`port/fmv/movie.c`) makes the same `SgStPcm*`
calls as the PS2's (docs/port/FMV.md, "Audio"), so a film's sound runs
through this engine.

## Mirror mode

With mirror mode on, `ico_audio_host_vsync` (`port/audio/audio_host.c`)
swaps the left and right samples of each rendered block
(`ico_audio_pan_mirror`) right after `spu2_render`, before the WAV dump,
the volume scaling and the SDL queue, so the device and the dump agree.
The SPU2, the driver and the game's pan values are untouched. The switch is
the run's mirror mode (`ico_opt_mirror()`, docs/port/OPTIONS.md), read per
block, so a change takes effect from the next vsync.

A film's sound is mixed by the same SPU2, so it is swapped with mirror
mode too, as the picture is flipped (docs/port/FMV.md, "Mirror").

## Output

Once per vsync `audio_host.c` has the SPU2 render 960 frames (50 Hz), or
48000 x 1001 / 60000 = 800.8 on average at 59.94 Hz (800 or 801, exact
over every five vsyncs), always on the simulation thread. The SPU2 and the
driver are never touched from another thread, so no lock is needed. The
frames then go to:

- **SDL3** (window build, `out_sdl.c`): one `SDL_AudioStream` opened with
  `SDL_OpenAudioDeviceStream` at 48 kHz stereo S16, the SPU2's own format.
  `SDL_PutAudioStreamData` is SDL's thread-safe hand-off; SDL's audio
  thread only drains the stream. The latency target is two vsyncs (40 ms
  at 50 Hz) queued in the stream, plus a 480-frame (10 ms) device period
  (`SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`), plus up to one vsync between a
  sound tick and the render that plays it: about 50 to 70 ms from the
  EE's key-on to the device. When the queue holds less than a quarter
  vsync (start-up, a stall) one vsync of silence is queued ahead of the
  block; above four vsyncs (the simulation ran ahead of the window's
  pacing) the block is dropped. Both are counted and logged at exit (A22).
  `[audio] enabled = false` (`audio=0`) opens no device; the driver still
  runs. The output volume (`[audio] volume`, 0.0 to 1.0, exported as
  `ICO_AUDIO_VOLUME`; the Settings menu steps it live through
  `ico_audio_set_volume`) scales each block just before it is queued:
  `(sample * q8 + 128) >> 8` with `q8 = round(volume * 256)` clamped to
  0..256 and the result clamped to 16 bits (`volume.c`).
- **WAV** (any build, `audio_dump=PATH`, relative to the executable's
  folder; `audio_dump=1` is `logs/audio.wav`): 16-bit stereo 48 kHz, the
  header patched every second and at exit, and a summary line (frames,
  peak, first non-silent vsync) in the log. The dump is taken before the
  volume scaling and stays at full volume. `port/platform/host_config.c`
  hands the keys over as `ICO_AUDIO_DUMP` and `ICO_AUDIO`.
- **Nothing** (headless without a dump). The driver and the SPU2 run
  regardless, because the game reads ENVX (slot release) and the stream
  read offsets back; everything is a function of the vsync count, so a
  headless run is deterministic and the same with or without a dump.

On Linux, `tools/fetch_deps.sh` builds SDL3 with the ALSA backend (a
dlopen of `libasound.so.2`; PulseAudio and PipeWire servers are reached
through their ALSA plugins) from Debian's `libasound2-dev` headers
unpacked into the build's scratch tree; `libpulse-dev` is unpacked too,
and SDL enables that backend when the build host has `libpulse.so.0`. The
stamp is `SDL3-<version>+audio`. The Windows prebuilt uses WASAPI.

## Gains and output mode

The SPU2 renders one stereo mix, so the music and effects volumes
(`[audio] music`, `[audio] effects`, Settings > Audio) cannot scale a
music bus after the fact. `mix_gain.c` scales the voices' own volume
registers instead, by what each of the 48 voice slots (core * 24 + voice)
is playing:

- **Tags.** The sequencer's volume packet, `_SgSeqSeVolume`
  (`sce/libsndn2/sound.c`), tags its voice music when the sequence is a
  BGM (`(status & 5) == 1`, set by `SgBgmOpen`) and effects when it is an
  SE (`== 4`), the test `_SgSetRealtimeVolume` makes. An ADPCM stream's
  open, `adpcmDataSet` (`fumi/sound/adpcm_init.c`, which both
  `AdpcmOpenSync` and `ReadSoundAdpcmFile` go through), tags each of its
  voices by the stream's number: 101 to 104 are
  `sound/ICO_ADPCM/event2/hint1_1.int` to `hint2_2.int`, Yorda's hint
  voice, which counts as effects; 1 to 100 (`battle.int`, `e3/*`,
  `event/*`, `event2/*`) are music. The names are the `adpcmFile` table's
  in the PAL `SCES_507.60` (0x69 records of 0x40 bytes). A slot no tag has
  reached is left alone. Sequenced voices and streams share the pool, so
  a slot's tag is whatever last played on it.
- **Apply.** The driver's two voice volume writes, packet 0x01 (the
  sequencer's) and packet 0x40 (a stream's), call `ico_audio_gain_voice`:
  the raw VOLL and VOLR are kept per slot and written scaled by the slot's
  gain, `floor((level * q + 2048) / 4096)` on the word's low 15 bits as a
  signed level, with `q` the gain in 1/4096 steps. At 100 % the written
  word is the packet's word bit for bit, so the default mix, the `sndn2`
  and `spu2_render_crc` goldens and the WAV dump are unchanged (a boot run's
  dump at the default gains is byte-identical to one from before the
  gains). A word in sweep mode (bit 15) is written unscaled and logged
  once when a gain is below 100 %; the game sends only fixed levels
  (`_SgSeqSeVolume`'s words have bit 15 clear unless the slot's +0x2E
  attenuation byte has its top bit set; streams send levels below 0x4000).
- **Live.** A gain change re-issues the kept volumes of that category's
  slots at once. The driver's init (0x1E) and reset forget them.
- **Film.** A film's sound is the PCM mixer's (`stream.c`, "PCM
  streams"); a film gain scales its channel volumes there, the identity at
  100 %. No setting changes it: a film's track mixes music and effects, so
  films follow the master volume alone.

The two gains are exported as `ICO_AUDIO_MUSIC` and `ICO_AUDIO_EFFECTS`
and set in `ico_audio_host_init`; the Settings rows call
`ico_audio_set_gain`.

A boot run (the corpus's `pad-boot.txt`: boot, title, New Game; 1500
ticks, 60 s of audio) was dumped at the defaults and at music 0.5, music 0,
effects 0 and effects 0.5, and compared second by second. Where a run at
effects 0 is byte-identical to the default (only music sounds: seconds 15
to 20), music 0.5 is at 0.50 of the default's RMS and effects 0.5 is
byte-identical; where a run at music 0 is byte-identical to the default
(only effects: seconds 43 to 60), music 0.5 is byte-identical and effects
0.5 is at 0.50. Seconds 21 to 28 mix both (music 0.5 keeps 0.88 to 1.00
of the RMS, effects 0.5 0.53 to 0.69), and 29 to 42 are music over a
faint effect (music 0.5 at 0.50, effects 0.5 byte-identical). The game's
trace is the same in every run: the gains change only what is heard.

**Output mode.** The PS2's stereo or mono choice is the game's
(`soundOutputModeSet`, `fumi/sound/s_init.c`): in mono `_SgSeqSeVolume`
gives both sides the larger level (the common context's 0x38 word, set by
`SgSetOutputMode`), `AdpcmInterStereoVolumeSet` sends both of a stereo
stream's channels to both sides, and the film player is opened mono
(`common/src/main.c`). `[audio] output` (`"auto"`, `"stereo"`, `"mono"`;
docs/port/OPTIONS.md, "Sound output") chooses it over the memory card's
value; Auto leaves it to the game.

## Output device

`[audio] device` names the playback device (window build). The name is
SDL's (`SDL_GetAudioDeviceName`); `out_sdl.c` opens the stream on the
playback device with that name, or on SDL's default device
(`SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK`, which follows the system's default)
when the name is empty or no device has it (one log line). The key is
exported as `ICO_AUDIO_DEVICE` and read in `ico_audio_sdl_open`.

- `ico_audio_sdl_devices` lists the playback devices' names;
  Settings > Audio > Output device steps through Default and them.
- `ico_audio_sdl_reopen(name)` destroys the stream and opens it again on
  the new device, playing; the queue refills with the next vsync's silence
  as after a stall.
- `SDL_EVENT_AUDIO_DEVICE_REMOVED` (`port/platform/window_host.c`) for the
  device the stream plays on reopens it on the default device; the key is
  kept for the next start.
- The headless build has no devices: the functions are stubs in
  `audio_host.c` (no devices, reopen fails).

## Render cost

`spu2_render` runs on the simulation path (`host_loop.c` calls
`ico_audio_host_vsync` between the vsync callbacks and the game threads),
so its cost is paid inside every simulation step. A plain frame-by-frame
render of the reference order cost about 2 ms per vsync even when idle,
mostly because `spu2_sd_init` keys all 48 voices on at pitch 0x3FFF over
the silent init block and they free-run forever, and because of the
per-frame call chain and the reverb's two full 39-tap FIRs. The chunked
renderer brings the idle state to about 0.24 ms and a busy game scene to
about 0.66 ms per vsync, with bit-identical output:

1. **Chunks, voice by voice** (`render_chunk`). After the frame's queued
   writes and transfer completions (`frame_begin`), the render takes up to
   256 frames up to the next queued write or transfer time, the next
   transfer completion and the next AutoDMA half boundary
   (`chunk_frames`). Nothing outside the SPU2 can see or change anything
   inside such a stretch, provided no IRQ can be raised, so each core's
   IRQ must be disabled or already pending (`chunk_allowed`). The
   registers are then constant, and each voice runs all the chunk's frames
   in one loop (`voice_chunk`) into per-frame bus arrays, core by core and
   voice by voice; PMON reads the previous voice's per-frame outputs, and
   NON reads a noise sequence computed first. A second pass runs steps 3
   to 6 per frame in the reference order (`core_finish`: MMIX, reverb,
   master volume, write-backs, AutoDMA advance). Otherwise (a write or
   completion due next frame, an armed IRQ) the frame renders with the
   reference `frame()`.
2. **Hazards.** The only reordering is voices reading sound RAM before the
   chunk's frames write it. A voice that loads a block overlapping the
   write-back area (halfwords 0x400-0x1FFF) or a core's reverb work area
   with writes enabled sets a flag; the voices (a snapshot taken at the
   chunk start), ENDX and the noise state are put back and the chunk
   renders frame by frame.
3. **The fused voice loop** reads registers once per chunk and keeps
   envelope, volume and pitch state in locals (the bus stores would
   otherwise force reloads under `-fno-strict-aliasing`). The interpolator
   is a window over the current block (`win`), so feeding 0 to 4 samples is
   an index add and a block end is handled once per block. A zero envelope
   skips the interpolation and the mix.
4. **Envelope between steps** (`adsr_quiet_inc`). While `counter + inc`
   stays below 0x8000 an ADSR tick only adds `inc`; the full `adsr_run`
   runs only on a step or when a phase may end.
5. **Silent voices in bulk** (`chunk_silent`). A voice with the envelope
   off at level 0 and no PMON outputs 0 every frame; only its counter,
   decoder and sweeps move, so the fetches are skipped a block at a time
   (`chunk_skip`, still decoding every block for the history, ENDX and
   LSAX). An end + mute met on the way stops the skip at the end of that
   frame.
6. **Silent blocks** (`adpcm_decode_block`). Data bytes all zero with
   filter 0 or a zero history decode to 28 zeros without the loop.
7. **Reverb.** The resampling FIR is half-band (odd taps zero except the
   centre 0x4000) and symmetric, so it takes 10 multiplies over pair sums;
   the even frames' zero-stuffed input meets only the centre. The
   work-area offsets are reduced modulo the area size once per chunk
   (`rv_plan`), so an access is an add and a compare.

`frame()`, `core_frame`, `voice_frame` and `voice_fetch` stay as the
reference and handle everything the chunk conditions exclude.
`spu2_set_exact(1)` forces the reference path. `spu2_get_stats` counts
chunked and frame-by-frame frames and hazards, and
`ico_audio_host_shutdown` logs them (`audio: SPU2 rendered N frames voice
by voice in C chunks and M frame by frame (H hazards)`), so a game run
shows whether anything keeps the render on the slow path.

### Proving identical output

`spu2_render_crc` (`spu2_bench --check`) takes a CRC-32 over the output,
the registers the driver and sequencer read after every vsync (ENVX, VOLX,
NAX, LSAX, ENDX, STATX, MVOLX, the IRQ flag), every callback with its time
and the reads it makes, and the final 2 MB of sound RAM. Five 10-second
scenes must match golden values taken from the frame-by-frame renderer,
and 24 random 2-second scenes (random PMON, NON, VMIX, MMIX, AVOL/BVOL,
reverb modes, transfer rates, IRQ, AutoDMA, NAX/LSAX/ADSR/ENVX/pitch writes
inside the block, sample uploads under playing voices) are rendered
chunked and with `spu2_set_exact(1)` and must agree. `spu2_bench --print`
lists all 29 CRCs and the five scenes' frame-by-frame CRCs, from which the
goldens are taken. They were retaken once since S3, for the disc's
`sceSdInit` (DIVERGENCES A14) and the hazard scene's voice across the end
of RAM.

The harness must produce the same random scenes under every compiler. C
leaves the evaluation order of function arguments and of most operands
unspecified, and gcc and clang order them differently, so every random
draw in `spu2_bench.c` is its own full expression (`voice_start_random`,
`random_header`, `random_block_addr`). Neither UBSan nor
`-Wsequence-point`/`-Wunsequenced` reports such code, because it is
unspecified behaviour rather than undefined; a new draw must follow the
same rule. `spu2_bench --trace-crc <scene | seed>` prints the harness PRNG
state, the running CRC and a CRC per component after every vsync, which
locates where two builds diverge.

### The benchmark

`spu2_bench` (`port/audio/test/spu2_bench.c`; it also builds for Windows)
renders the five scenes with `spu2_sd` calls the way the driver makes them
and times each `spu2_render` call (one vsync) with `clock_gettime`
(QueryPerformanceCounter on Windows):

| scene | what |
|---|---|
| idle | `spu2_sd_init` only, reverb on both cores (studio large, hall): the boot state |
| game | 24 voices keyed over time, key offs, pitch changes, sweeps, ENVX writes on ended voices, noise, PMON, AutoDMA input, transfers, writes inside the block, effect enable toggling; 800/801 frames per vsync |
| hazard | the game scene plus an armed IRQ on a played block and voices playing from the write-back area, from core 1's reverb work area and from a one-block loop across the end of sound RAM (renders mostly frame by frame by design) |
| full48 | 48 looping voices (pitch 0x0400-0x3BFF), reverb on both cores, a pitch change and a retrigger per vsync, 960 frames per vsync |
| full24 | the same with 24 voices |

`spu2_prof` is the same harness over a copy of the SPU2 built with
`SPU2_PROFILE`: `SPU2_LAP(stage)` points (compiled out otherwise) charge
the time since the previous lap to a stage, and the harness subtracts one
clock read per lap. The ADPCM decoder is bound by its own recurrence
(about 11 cycles a sample), so further headroom would come from decoding
two voices' blocks interleaved.

## Sources

Public hardware documentation, cited where used. No emulator source was
read for the SPU2, and nothing was copied from any implementation.

| what | source |
|---|---|
| ADPCM block format, flags, loop behaviour, ENDX; pitch counter and PMON; Gaussian interpolation formula and table; ADSR register layout and the envelope step algorithm; volume and sweep modes; ENVX, VOLX; KON/KOFF; noise generator; ATTR, STATX; IRQ sources; reverb registers, formula, notes on precision and disable; the 39-tap resampling FIR; reverb presets and work area sizes | psx-spx, "Sound Processing Unit (SPU)", https://psx-spx.consoledev.net/soundprocessingunitspu/ (source `docs/ps1/spu/soundprocessingunitspu.md`, github.com/psx-spx/psx-spx.github.io, commit b8b3f284, 2026-09-30) |
| ADPCM filter coefficients (five pairs), the decode formula, reserved shift values | psx-spx, "CDROM XA-ADPCM", `decode_28_nibbles` and "Pos/neg Tables", source `docs/ps1/cdr/cdromformat.md`, same commit |
| SPU2 register layout (offsets of every register used here, the 0x760 block, address pairs) | ps2sdk `common/include/spu2_mmio_hwport.h`, https://github.com/ps2dev/ps2sdk (AFL-2.0; layout read, no code taken) |
| libsd encodings and behaviour: `libsd-common.h`; `sceSdInit`, `sceSdSetParam`, `sceSdSetCoreAttr`, `sceSdSetEffectAttr` (preset order, ESA and address scaling), `sceSdVoiceTransStatus` | ps2sdk `common/include/libsd-common.h`, `iop/sound/libsd/src/freesd.c`, `effect.c`, `voice.c` (commit ac92a9f6); read for behaviour only |
| SPU2 is two cores x 24 voices, 48 kHz, 2 MB; write-back areas (voice 1/3, core 0 output, dry/wet mixes) at halfword addresses 0x400-0x1FFF; free-running voices; IRQ on reverb accesses | PCSX2 wiki, "PCSX2 Documentation/SPU2 is more than just sound!" (prose article by Jake Stine), https://wiki.pcsx2.net/PCSX2_Documentation/SPU2_is_more_than_just_sound! (read via web.archive.org) |
| What SNDN2DRV writes: VOLL/VOLR (raw words, sweep possible when a tone's byte at slot +0x2E is non-zero, see `sound.c`), ADSR, PITCH, KON/KOFF, VMIXEL/R, NON, noise clock, EVOL, MVOL, EEA, effect mode 4 (studio 3) on both cores, MMIX 0xFF0/0xFFC and the FMV values, AutoDMA PCM layout, ENVX/NAX polling | docs/research/sndn2drv.md, `sce/libsndn2/sound.c` |
| MMIX bit meanings | docs/research/sndn2drv.md ("PCM streams"), which read them from PCSX2 for behaviour; consistent with libsd's defaults 0xFF0/0xFFC |
| every command's libsd calls, the packet layouts, the reply page, the stream records, event queue, refill scheduler, FILL flag patching and PCM mixer, the pitch formula and table location | docs/research/sndn2drv.md (from the user's `SNDN2DRV.IRX`) |
| the SDL3 audio stream calls and their thread safety | SDL 3.4.18's `include/SDL3/SDL_audio.h` and `SDL_hints.h` (zlib) |

Licences: psx-spx is documentation; ps2sdk is AFL-2.0 and only its
register layout, encodings and described behaviour were used; the PCSX2
wiki article is prose. No GPL code is in or behind the audio path
(docs/research/licences.md).

### Tables

- Gaussian table: psx-spx's 512 entries, transcribed by a script from the
  page's text and checked: the first sixteen are -1, the peak is 0x59B3,
  and every set of four taps sums to 0x7F7F..0x7F81 (psx-spx's stated
  property). psx-spx shows that no cosine series short of fifteen terms
  reproduces it, so the table is kept literal rather than generated.
- Reverb FIR: psx-spx's 39 taps (sum 0x7FFE, unity within 2 LSB).
- Reverb presets (the built-in fallback; the disc's are used when present,
  "libsd values"): psx-spx's ten examples in libsd's mode order. Compared
  word by word with ps2sdk's `EffectParams`: studio 1-3, hall, space and
  pipe are identical; off differs in unused address words, room in its
  same/different-side and comb 3/4 addresses; echo and delay differ in
  their APF sizes and most addresses (ps2sdk then rewrites them from
  delay/feedback). STUDIO_3, the only mode the game selects, is the same in
  both.

## Approximations

Each is a row in docs/port/DIVERGENCES.md ("Platform"):

| row | what |
|---|---|
| A1 | reverb left and right computed in the same 24 kHz step |
| A2 | reverb rounding/saturation points; the vIIR = -0x8000 quirk not modelled |
| A3 | ADPCM filters 5-7 as 0, shifts 13-15 as 9 |
| A4 | exponential decrease step floored (arithmetic shift) |
| A5 | envelope step counter handling across steps and phase changes |
| A6 | key on: no latency; histories zeroed; LSAX not reset |
| A7 | NAX position within a block |
| A8 | transfer duration (one frame, or a set rate) |
| A9 | reverb presets from the disc (psx-spx's without it), delay/feedback ignored |
| A10 | ATTR mute / SPU-on bits ignored |
| A11 | AutoDMA input from a host ring, not sound RAM |
| A12 | no IRQ from reverb buffer accesses |
| A13 | reverb disabled: the whole chain is still read |
| A14 | `spu2_sd_init`: the disc's writes; SPDIF/SSBUS/DMA control and delays not modelled |
| A15 | pitch table: the formula when the disc's IRX cannot be read |
| A16 | stream cancel: fixed, as the IRX (kept for the record) |
| A17 | driver writes take effect at the next vsync block; ENVX/NAX read at a block's end |
| A18 | a channel-1 ADPCM fill does not run the PCM mix callback |
| A19 | sample transfers complete one frame after they start |
| A20 | commands 0x17/0x18 re-apply the last 0x15 attr, not the IRX's stale stack slot |
| A21 | an out-of-range pitch index is clamped, not read from the IRX's neighbouring data |
| A22 | the SDL output drops or pads whole vsync blocks to keep two vsyncs queued |

A4, A5, A6 and A7 can reach the EE: the sequencer frees a voice when ENVX
drops below 2, and the stream scheduler reads NAX once per tick. A tick is
960 frames at 50 Hz, so these matter only near a tick boundary. A17
and A19 can reach the EE the same way (slot release, stream read offsets,
the DMA-status wait). In particular, the real driver keys a stream on in
its third tick and reports the read offset a tick after each DMA, so
traces through the opening differ by a tick from a model that counts the
first fill at `SgStAdpcmPlay`; the host follows the IRX.

## Tests

All in `port/audio/test/`, registered in `port/audio/CMakeLists.txt`:

- `adpcm`: five blocks decoded by hand in comments (no filter, filter 1
  with shift, filter 2 with negative history and floor rounding, filter 4
  saturation, reserved shift/filter), and history carried across blocks.
- `spu2`: Gaussian table properties; ADSR attack, decay and release
  lengths against psx-spx's closed forms; pitch (a 1 kHz sine encoded to
  ADPCM by a test-only encoder and played at five PITCH values, with zero
  crossings over one second within 2 of the expected frequency); loop
  flags; mixing and saturation; a timed key on; AutoDMA input; noise;
  transfers and IRQs; the libsd front end (ESA from EEA for mode 4, address
  scaling, EVOL, core attributes, voice address with Sony's 0x40 flag, init
  defaults); a reverb impulse response (studio 3, decaying to about -45 dB
  by 2 s); and a deterministic scene that must match the golden FNV-1a
  checksum 0x77DB2B76.
- `sndn2` (`sndn2_test [disc image]`): the RPC transport and reply pages,
  voice and core packets as register effects, ENVX in the reply, the pitch
  maths and clamp (and, with the disc image, the table from
  `SNDN2DRV.IRX`, its 32 differences from the formula and the two
  outliers), a synthetic stereo `.int` stream through fills, key on,
  refills and stop, a mono stream at 4x pitch wrapping its read offset,
  the stream cancel (a queued KEYON losing the stopped voice's bit, the
  core 0 fall-through into the core 1 mask, the pending fill forgotten),
  the PCM mixer, with the disc image the libsd values from `LIBSD.IRX`, and the sequencer end to end from a synthetic `.hd`
  through `SgSePlay`, `SgCalledTickProc`, `SgSeStop` and `SgVabClose`.
- `libsd_irx`: the `LIBSD.IRX` reader on a synthetic ELF (made-up tables
  at the disc module's addresses): parsing, every refusal, the values
  reaching the presets, the idle block at byte 0x5000 and the cold-init
  ESA, and the fallback to the built-in values without a disc.
- `volume`: the output volume scaling.
- `mix_gain`: the gains (`mix_gain.h`): every fixed volume word unchanged
  at 100 % (and every word of 0..0xFFFF through a music, effects and
  untagged slot), the signed 15-bit scaling and its rounding, sweep words
  passed through, the stream numbers' tags, the kept volumes written to
  the SPU2's registers and re-issued on a change (only the changed
  category's), forget, the film word.
- `audio_pan`: the mirror-mode channel swap.
- `spu2_render_crc`: the chunked renderer against the reference (above).

Open items for audio are in docs/TODO.md.
