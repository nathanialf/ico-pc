# Audio: the software SPU2 (Phase 4A), the driver and the output (Phase 4B)

`port/audio/` holds a clean-room software SPU2: the PS2's sound processor
rendered on the host as 48 kHz interleaved stereo 16-bit, plus the SPU
ADPCM decoder and a libsd-shaped front end (4A). On top of it (4B): the
game's Sg sequencer, moved here from `sce/libsndn2/` and made 64-bit clean;
the SNDN2DRV.IRX replacement behind the sequencer's RPC, with its ADPCM and
PCM stream engines (per docs/research/sndn2drv.md, "R1"); and the output:
one SPU2 render per simulated vsync into an SDL3 audio stream (window
build), a WAV file (ini `audio_dump=`), or nowhere (headless). It is all in
`ico_pc` since 4B; `port/null/snd_null.c` is gone.

| file | contents |
|---|---|
| `adpcm.h/.c` | SPU ("VAG") ADPCM block decoder: 16-byte blocks, 28 samples, five filter pairs, loop flags. Also for 4B's `.int` streams |
| `spu2.h/.c` | the SPU2: 2 cores x 24 voices, 2 MB sound RAM, envelopes, volumes and sweeps, noise, mixing, reverb, transfers, AutoDMA input, IRQ |
| `spu2_tables.c` | the Gaussian interpolation table, the reverb resampling FIR, the reverb presets (transcribed from psx-spx) |
| `spu2_internal.h` | tables and the envelope step, shared with the tests |
| `spu2_sd.h/.c` | libsd's calls (`sceSdSetParam`, `SetSwitch`, `SetAddr`, `SetCoreAttr`, `SetEffectAttr`, `VoiceTrans`, `Init`) with libsd's encodings, as register writes |
| `sg/sound.c`, `sg/sound.h` | the EE Sg sequencer the game calls (`libsndn2.a(sound.o)`, this project's clean-room reconstruction, MIT), with host seams under `ICO_HOST` (4B, "The sequencer on the host") |
| `sndn2_host.h/.c` | the SNDN2DRV host: RPC entry points, packet dispatcher, reply pages, pitch table |
| `stream.c`, `sndn2_internal.h` | the ADPCM stream engine (records, event queue, refill scheduler) and the PCM mixer |
| `audio_host.h/.c` | the per-vsync render and its sinks; `wav.c` the dump writer; `out_sdl.c` the SDL3 device (window build) |
| `test/adpcm_test.c`, `test/spu2_test.c`, `test/sndn2_test.c` | unit tests (below) |

Build: `ico_audio` static library (the SPU2); `ico_sndn2` (the rest; it
links `ico_audio`, `ico_port_data` and `ico_platform`, and SDL3 in the
window build). ctest tests `adpcm`, `spu2` and `sndn2`
(`port/audio/CMakeLists.txt`). `ico_pc` links `ico_sndn2`; the host loop
(`port/platform/host_loop.c`) calls `ico_audio_host_init` before boot and
`ico_audio_host_vsync` once per vsync.

## Design

### State and time

One global SPU2 (`spu2_reset()` returns it to power-on: RAM and registers
zero). Time is the output frame counter at 48 kHz (`spu2_time()`).
`spu2_write_reg(core, reg, value, time)` queues a write; `spu2_render()`
applies every write whose time has come before rendering that frame, in
time order and, for equal times, in call order. A time already rendered
means "before the next frame". So a caller that stamps every write of one
IOP tick with the same time gets block-accurate timing, and one that stamps
finer gets sample-accurate timing. `spu2_apply_pending()` applies the queue
at once. Reads (`spu2_read_reg`) see the state after the last rendered
frame.

Addresses are SPU2 halfword addresses (20 bits); libsd's byte addresses are
converted in `spu2_sd.c`. Sound RAM is `uint8_t[2 MB]`, little-endian
halfwords, reached through `spu2_ram()`.

### One frame

For core 0, then core 1:

1. Each voice: the sample is the 4-tap Gaussian interpolation of the last
   four decoded samples at pitch-counter bits 4-11 (or the core's noise
   level if NON selects it); times the ADSR level (`>> 15`); times the
   left/right volume (`>> 15`); added to the dry buses (VMIXL/VMIXR) and
   wet buses (VMIXEL/VMIXER). Then the envelope and volume sweeps step,
   and the pitch counter advances by PITCH (PMON: scaled by the previous
   voice's output + 0x8000; clamped to 0x4000), feeding one decoded sample
   to the interpolator per 0x1000. Decoding fetches the next 16-byte block
   at a block end: loop end jumps to LSAX and sets ENDX, end without repeat
   also forces release at level 0; loop start latches LSAX. Voices free-run
   (keep decoding) after their envelope ends, as on the hardware.
2. The noise generator steps (ATTR bits 8-13).
3. Buses saturate to 16 bits. MMIX selects, separately for dry and wet and
   left and right: the voices, the external input (core 1 only: core 0's
   output through AVOL) and the AutoDMA input (through BVOL).
4. The wet sum enters the reverb: the 39-tap FIR filters it and every second
   frame the reverb unit runs one 24 kHz step over the work area
   ESA..EEA; its output is zero-stuffed back to 48 kHz through the same FIR
   (gain 2), scaled by EVOL and added to the dry sum.
5. Master volume (with sweep) gives the core's output. Core 1's output is
   the frame's output; core 0's goes on to core 1.
6. Write-back areas get this frame's samples (voice 1 and 3 of each core,
   core 0's output, each core's dry and wet sums, at the halfword addresses
   the PCSX2 wiki lists).

### Register map covered

Offsets as ps2sdk's `spu2_mmio_hwport.h` lays out a core (core 1's bank
selected by the `core` argument; the 0x760 block uses core 0's offsets).

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
capture buffers, `sceSdProcBatch` (unused by SNDN2DRV, R1).

### libsd front end

`spu2_sd.c` turns libsd's calls into register writes stamped with the time
set by `spu2_sd_set_time()`. The argument encodings are libsd's ABI as
ps2sdk's `libsd-common.h` spells them (voice selector `core | voice << 1`,
`SD_VPARAM_*`, `SD_PARAM_*`, `SD_SWITCH_*`, `SD_ADDR_*` byte addresses,
`SD_CORE_*`). Behaviour and init values follow ps2sdk's clean-room libsd
(`freesd.c`, `effect.c`, `voice.c`), which R1 already marked "usable but
not authoritative". Points to confirm against the disc's `LIBSD.IRX`
(R1 open question 1):

- `sceSdInit` register values (`spu2_sd_init`; DIVERGENCES A14);
- the effect preset table and work area sizes (A9). The psx-spx presets'
  sizes are exactly libsd's `EffectSizes << 4` for all ten modes, which is
  how the mode order (off, room, studio 1-3 = small/medium/large, hall,
  space, echo, delay, pipe = half echo) was matched;
- `sceSdSetEffectAttr` with a stale `delay`/`feedback` (ignored here; the
  game never sends them, R1);
- `sceSdVoiceTrans`'s busy check (here: refused while the channel has a
  transfer queued or running).

`sceSdSetEffectAttr` places the work area as libsd does: ESA = end address
- size + 2 bytes, where the end address is `EEA << 17 | 0x1FFFF`; the
preset's address registers (PS1 units of 8 bytes) are written as halfwords
(x4), so byte offsets are the PS1 ones.

### Interface for Phase 4B

- Lifecycle: `spu2_reset()`, then `spu2_sd_init(hot)` for SNDN2DRV's
  command 0x1E.
- Per IOP tick: `spu2_sd_set_time(t)` with the frame the tick lands on,
  then the libsd calls the packet dispatcher makes (`spu2_sd_set_param`,
  `_set_switch`, `_set_addr`, `_set_core_attr`, `_set_effect_attr`,
  `_voice_trans`), then the reads for the mailbox page
  (`spu2_sd_get_param(V(ENVX))`, `spu2_sd_get_addr(V(NAX))`) and
  `spu2_sd_voice_trans_status(1, 0)` for the stream queue.
- Audio thread or pump: `spu2_render(out, frames)` produces 48 kHz stereo
  for SDL. Everything is single-threaded: the caller serialises render and
  register calls (a 4B host that ticks on the game's vsync and renders on
  the SDL callback needs a lock or a hand-off queue).
- Transfers: `spu2_set_trans_callback(chan, cb, user)` is the DMA-done
  interrupt (SNDN2DRV's 0x508 handler writes the transfer counter into the
  mailbox there); `spu2_set_dma_rate()` picks the transfer speed (A8).
- PCM (FMV) input: `spu2_memin_start(1, ring, cb, user)` with the IOP
  staging ring in libsd's 0x800-byte layout; `cb` is the half-done
  callback (SNDN2DRV 0x558), `spu2_memin_half()` is
  `sceSdBlockTransStatus(1, 0) & 0x01000000`. BVOL and MMIX come through
  `spu2_sd_set_param`.
- Reverb presets: `spu2_reverb_set_preset(mode, p)` replaces a built-in
  preset, e.g. with values read from the user's `LIBSD.IRX` at extraction
  time (as R1 proposes for the pitch table).
- The 608-entry pitch table (R1, "Pitch") belongs to the 4B host, not the
  SPU2: the SPU2 takes the PITCH word the driver computes.

## The sequencer on the host (4B)

`sce/libsndn2/sound.c` and `sound.h` moved to `port/audio/sg/`. The old
paths keep one-line includes of the new files, so the PS2 build still
compiles the member at its link-order path (`config/link_order.pal.txt`)
with the SDK archive flags `tools/compile_c.sh` gives `sce/` sources.
Every host change is an `#ifdef ICO_HOST` arm whose `#else` is the EE text
as it was, or the macro `SG_HEAD_T`, which spells the EE's own `int`.
Checked: the file preprocessed without `ICO_HOST` (stub SDK headers) gives
the same 23,807-token stream as the pre-move original. The period compiler
could not be fetched in this session, so the PS2 object itself was not
rebuilt.

`port/compat/sound.h` now includes `sg/sound.h` (host view: `long long`
for the EE's 64-bit `long` in `SgStPcmBufMode`, and the
`SgSetSePitchDirect` prototype the EE header leaves out). None of the
game's `soundSe*`/`Adpcm*` or raw `Sg*` call sites changed.

The seams (the comment at the top of `sg/sound.c` has the details):

| EE | host |
|---|---|
| the head context, 16 `int` words holding the five pointers the event decoder shares (program block, tone record, channel table, `.hd` header, event cursor), read as `int *`, `char *` and `unsigned char **` in different functions | `unsigned char *sgHeadContext[16]`; each function's view uses the pointer array |
| vab context word 0 = the `.hd` address | a nonzero token in the word, the address in `sgVabHd[vab]` |
| sequence context word 2 (+8) = the `.sq` body or the SE table entry | a token, the address in `sgSeqBody[seq]`; every `memset` of a context clears the token, so readers see NULL exactly where the EE saw 0 |
| `SgVabOpenFakeBody` writes header + the words at 0x10, 0x18-0x24 into the header's 0x30, 0x38-0x44 | `sgHdRel[vab]`, a record keyed by the header address (looked up by header, among open vabs); the header is never written (docs/port/LOADERS.md "hd and sq") |
| `sgIop2EeBuf | 0x20000000` (uncached alias) | `ICO_UNCACHED`, the identity |
| `_SgGetPacketCntext`: `page * 0x1000 + (int)p` | pointer arithmetic |
| `SgGetDmaTransferStatus(1)` spins on the reply's +0x1C0 | the spin calls `ico_sched_spin_vsync()` each pass: the reply that ends it comes with the sound thread's next tick, which a spinning fiber would otherwise block (R1, "Mailbox"). On the host context (tests) it returns 0 |
| `SgSndn2RemoteInit` binds to the IRX's server | registers the host server first (`ico_sndn2_host_register`) |

## The SNDN2DRV host (4B)

`sndn2_host.c` is a host SIF server (`port/data/sif_host.h`) under the
IRX's id 0x736E646E. RPC 0x65 runs one packet (the init, `{0x1E, hot}`)
and returns a zeroed page with the return word; RPC 0x64 runs a tick:
every 16-byte packet in order, then the ADPCM refill scheduler and event
queue, then the reply page. Each packet is the libsd calls R1 read off
the IRX, on `spu2_sd_*`.

### Command coverage

All 38 command ids R1 documents are handled (0x22, which the EE never
sends, and 0x3D, which does nothing in the IRX, included); the rest of the
IRX's jump table (1..0x4F) does nothing, and the host logs the first such
id once.

| group | ids | host |
|---|---|---|
| voices | 0x01 VOL, 0x02 ADSR, 0x03 SSA, 0x04 pitch | `SetParam`/`SetAddr` on `V(slot)` |
| masks | 0x0A KON, 0x0B KOFF (id ignored), 0x0C VMIXEL+VMIXER, 0x0D NON | `SetSwitch`, core 0 from w2, core 1 from w3 |
| core | 0x14 EEA, 0x15 type (clear bit dropped, depth 0, effect enable), 0x16 EVOL, 0x17 delay, 0x18 feedback, 0x28 MVOL, 0x32 id 8 noise clock (core = slot / 24), id 10 S/PDIF | as R1; 0x17/0x18 re-apply the last 0x15 attr twice (A20) |
| init | 0x1E `sceSdInit(hot)`, VMIXEL/R cleared, DMA 0 callback; 0x1F callback off | |
| sample RAM | 0x20 write, 0x21 read (IOP address into `ico_iop_ram`), 0x22 status into the return word | the counter `w1 >> 8` is kept; DMA 0's done callback writes it into both pages at +0x1C0 |
| ADPCM streams | 0x3C init, 0x3D quit, 0x3E open, 0x3F close, 0x40 volume, 0x41 pitch, 0x42 play, 0x43 stop | `stream.c` (below) |
| PCM streams | 0x46 init, 0x47 quit, 0x48 open, 0x49 close, 0x4A volume, 0x4B play, 0x4C stop, 0x4D seek, 0x4E effect (MMIX), 0x4F buffer mode | `stream.c` |

### Reply pages

Two 0x200-byte pages alternate with a counter, as in the IRX: words
0x000-0x0BF ENVX & 0x7FFF of the 48 voices (slot = core * 24 + voice),
0x0C0-0x17F the stream records' IOP read offsets, 0x180-0x1BF the PCM
channels' read offsets, 0x1C0 the transfer counter (written only by the
DMA callback, into both pages), the rest zero. The host SIF copies the
returned page into the EE's `sgIop2EeBuf` before `sceSifCallRpc`
returns, which the EE cannot tell from the PS2's asynchronous reply
because it reads the page only after `SgSndn2RemoteSync`.

### Timing

One tick per EE vsync, on the sound fiber, as on the PS2. The SPU2 renders
the vsync's frames in the host step (`ico_audio_host_vsync`) after the
vsync interrupt handlers and before the fibers run, so a tick's register
writes (stamped `spu2_time()`) take effect at the first frame of the next
block, and the ENVX/NAX it reads are the state at the end of the block
just rendered (A17). Voice transfers complete one frame after they start
(4A's default rate, A19), so a 0x20 issued in tick N reaches the EE's
+0x1C0 in the reply of tick N+1.

### Pitch table

Command 0x04 computes the PITCH word as R1 gives it (32-bit unsigned
arithmetic, the 441/480 step, the scale, `& 0xFFFF`) from a 608-entry
table. `ico_sndn2_pitch_load` reads it at registration from the mounted
disc: `SNDN2DRV.IRX` (checked: 20,941 bytes), file offset 0x3900, 0x4C0
bytes, accepted when T[208] = 0x1000. Without the disc it uses
`floor(4096 * 2^((i - 208) / 192))`, logs once and DIVERGENCES A15
applies. The index is clamped and counted, with one log line (R1 open
question 4; none in the 4B run).

Correction to R1 ("Pitch"): the disc's table differs from that formula in
32 entries, but only 30 by one. Entry 64 is 2360 (formula 2435) and entry
172 is 3340 (formula 3596), each below its predecessor: hand-made data with
two outliers, so the table is not monotonic and the loader does not check
order. Measured by `sndn2_test` against `baserom/Ico_PAL.iso`.

## Streams (4B, `stream.c`)

### ADPCM `.int` streams

The game reads a stream's file into IOP RAM itself (`fumi/sound/
adpcm_init.c` through `iosCdvdBackGroundReadIOPm`, i.e. `sceCdReadIOPm`
into `ico_iop_ram`, docs/port/DATA.md) and refills the ring behind the read
offset it polls. The host engine is R1's:

- 0x3E fills the 36-byte record (channels `n` from the attr byte, SPU ring
  start and size from the `AdpcmChReq` field the decomp calls `vol`, IOP
  ring start and size), sets SSA and ADSR 0x8080/0x808A.
- 0x42 queues a FILL of the first half (loop-start flags) per voice, then
  one KEYON for the masks; the 128-entry queue runs at most one transfer per
  tick, on DMA channel 1, and only when the channel is idle. So a stereo
  play keys on in the third tick (fill, fill, key on), and the IOP read
  offset moves on by `spu_size / 2 * n`, modulo the IOP ring, when the next
  tick finds the channel idle (one tick after the DMA).
- Every tick, each active voice's NAX is compared with its ring's halves;
  entering the other half queues a FILL of the half it left, from the
  current read offset.
- A FILL de-interleaves `len / (0x800 / n)` chunks of `0x800 / n` bytes
  from `src`, `src + 0x800`, ... into the staging buffer and patches the
  flag byte of the first and last block (half 1: 2 and 3; half 2: 6 and 2),
  so the voice loops over the whole ring and re-latches LSA at its start.
  A chunk outside IOP RAM is zero-filled and logged once (R1 open
  question 3: a 4-channel stream reads past its ring; IOP RAM continues
  there on the host as on the PS2).
- 0x43 cancels the voices' queued FILLs, clears their records' state and
  keys them off; 0x3F cancels and clears.

### PCM streams (movie audio)

0x46 points core 1's AutoDMA input at the staging buffer (the IRX shares
its 0x2000 bytes with the ADPCM fills; the PCM ring is its first 0x800),
sets BVOL 0x7FFF and installs the half-done callback. On each half-done
interrupt the half the input is not reading is zeroed and the 16 channels
are mixed into it: 256 left samples then 256 right, each sample `(vol *
x) >> 15` added with 16-bit wrap-around, `x` read at byte offset
`((2 * i) >> shift) * 2`, the read offset wrapping at the buffer size,
advancing by the open's step and stopping at the buffer-mode stop offset.
The movie player (`ito/mpeg/mv_audiodec.c`) is not in the build yet
(docs/port/HEADLESS_STUBS.md), so only `sndn2_test` drives this path.

## Output (4B)

`audio_host.c`: once per vsync the SPU2 renders 960 frames (50 Hz) or
48000 x 1001 / 60000 = 800.8 on average at 59.94 Hz (800 or 801, exact
over every five vsyncs), always on the simulation thread. The SPU2 and the
driver are never touched from another thread, so no lock is needed. The
frames then go to:

- **SDL3** (window build, `out_sdl.c`): one `SDL_AudioStream` opened with
  `SDL_OpenAudioDeviceStream` at 48 kHz stereo S16 (the SPU2's own
  format). `SDL_PutAudioStreamData` is SDL's thread-safe hand-off; SDL's
  audio thread only drains the stream. Latency target: two vsyncs (40 ms
  at 50 Hz) queued in the stream, plus a 480-frame (10 ms) device period
  (`SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`), plus up to one vsync between a
  sound tick and the render that plays it: about 50-70 ms from the EE's
  key-on to the device. When the queue holds less than a quarter vsync
  (start-up, a stall) one vsync of silence is queued ahead of the block;
  above four vsyncs (the simulation ran ahead of the window's pacing) the
  block is dropped. Both are counted and logged at exit (A22). Not
  measured on a real device: the build host has none. ini `audio=0` opens
  no device (the driver still runs).
- **WAV** (any build, ini `audio_dump=PATH`, relative to the ini's folder;
  `audio_dump=1` is `logs/audio.wav`): 16-bit stereo 48 kHz, header
  patched every second and at exit; a summary line (frames, peak, first
  non-silent vsync) goes to the log. `port/platform/host_config.c` hands
  the keys over as `ICO_AUDIO_DUMP` / `ICO_AUDIO`, as it does the renderer's
  dump keys.
- **Nothing** (headless without a dump). The driver and the SPU2 run
  regardless, because the game reads ENVX (slot release) and the stream
  read offsets back; everything is a function of the vsync count, so a
  headless run is deterministic and the same with or without a dump.

Linux SDL3 audio: `tools/fetch_deps.sh` now builds SDL3 with the ALSA
backend (dlopen of `libasound.so.2`; PulseAudio and PipeWire servers are
reached through their ALSA plugins), from Debian 13's `libasound2-dev`
headers unpacked into the build's scratch tree; `libpulse-dev` is unpacked
too and SDL enables that backend when the build host has `libpulse.so.0`
(this one does not). The stamp is `SDL3-3.4.18+audio`. Checked: the rebuilt
library lists `alsa disk dummy` and `SDL_Init(SDL_INIT_AUDIO)` picks
`alsa`. The Windows prebuilt has WASAPI.

## The 4B game run

One headless `linux-x64` run, `ticks=1300`, `pad-script.txt` =
`port/input/pad-boot.txt`, `audio_dump=logs/audio.wav` (2026-10-05): 1300
Main ticks, 2607 vsyncs, 10 s wall time, ended in stage 3. The driver
logged no ignored command, pitch clamp or out-of-range transfer; the pitch
table came from the disc.

The WAV: 2,502,720 frames (52.1 s = 2607 x 960). Silent to vsync 895
(17.9 s, inside stage 1, the title, which runs to vsync 1333); from there
-54 to -13 dBFS RMS per one-second window, peak 27272 (-1.6 dBFS), no
full-scale sample on either side. Loudest at 25-27 s (the end of the
title and the cut to the opening, -15 to -13 dBFS); the opening stages
41-45/40 (26.7-41.6 s) between -49 and -14 dBFS, with silence at 39-41 s
(the load into stage 3); stage 3 from 41.6 s at -20 dBFS, a dip to -53,
then a steady -34 dBFS (80-300 Hz holds 86-94% of the energy). Per window
the L/R correlation ranges from -0.02 to 0.87, so panning and the stereo
streams are live.

Against the null driver: the per-tick trace of this run equals 2J's
`linux-x64` run (same pad script, `port/null/snd_null.c`) for the first
860 ticks. Then game flag word gf0 gains bit 0x10 one tick later (tick 862
instead of 861, stage 43, the opening), and from tick 1116 (stage 3) the
trace's save-data hash differs; every other column stays equal to tick
1300. A one-tick shift in the opening is what the real stream path
predicts: the null model counted a stream's first fill at `SgStAdpcmPlay`,
while SNDN2DRV keys on in the third tick and reports the read offset a
tick after each DMA (R1). Which of the two the PS2 matches can only be
settled by a capture; the driver follows R1.

One feature is not explained: from 25 s to 31 s up to 42% of the right
channel's energy (8% of the left's) is below 5 Hz, a slowly decaying offset
of up to -5300 that follows the loud event at 25 s. An offline SPU2 check
(a loud zero-mean burst through studio-3 reverb at full EVOL) leaves at
most 6 LSB of mean, so the reverb is not the source. The likely source is
the program material (a sample with a DC component under a slow release),
which only a capture from a PS2 can confirm (open item).

## Sources

Public hardware documentation, cited where used. No emulator source was
read for this package; nothing was copied from any implementation.

| what | source |
|---|---|
| ADPCM block format, flags, loop behaviour, ENDX; pitch counter and PMON; Gaussian interpolation formula and table; ADSR register layout and the envelope step algorithm; volume and sweep modes; ENVX, VOLX; KON/KOFF; noise generator; ATTR, STATX; IRQ sources; reverb registers, formula, notes on precision and disable; the 39-tap resampling FIR; reverb presets and work area sizes | psx-spx, "Sound Processing Unit (SPU)", https://psx-spx.consoledev.net/soundprocessingunitspu/ (source `docs/ps1/spu/soundprocessingunitspu.md`, github.com/psx-spx/psx-spx.github.io, commit b8b3f284, 2026-09-30) |
| ADPCM filter coefficients (five pairs), the decode formula, reserved shift values | psx-spx, "CDROM XA-ADPCM", `decode_28_nibbles` and "Pos/neg Tables", source `docs/ps1/cdr/cdromformat.md`, same commit |
| SPU2 register layout (offsets of every register used here, the 0x760 block, address pairs) | ps2sdk `common/include/spu2_mmio_hwport.h`, https://github.com/ps2dev/ps2sdk (AFL-2.0; layout read, no code taken) |
| libsd encodings and behaviour: `libsd-common.h`; `sceSdInit`, `sceSdSetParam`, `sceSdSetCoreAttr`, `sceSdSetEffectAttr` (preset order, ESA and address scaling), `sceSdVoiceTransStatus` | ps2sdk `common/include/libsd-common.h`, `iop/sound/libsd/src/freesd.c`, `effect.c`, `voice.c` (commit ac92a9f6); read for behaviour only |
| SPU2 is two cores x 24 voices, 48 kHz, 2 MB; write-back areas (voice 1/3, core 0 output, dry/wet mixes) at halfword addresses 0x400-0x1FFF; free-running voices; IRQ on reverb accesses | PCSX2 wiki, "PCSX2 Documentation/SPU2 is more than just sound!" (prose article by Jake Stine), https://wiki.pcsx2.net/PCSX2_Documentation/SPU2_is_more_than_just_sound! (read via web.archive.org) |
| What SNDN2DRV writes: VOLL/VOLR (raw words, sweep possible when a tone's byte at slot +0x2E is non-zero, `sound.c:1154`), ADSR, PITCH, KON/KOFF, VMIXEL/R, NON, noise clock, EVOL, MVOL, EEA, effect mode 4 (studio 3) on both cores, MMIX 0xFF0/0xFFC and the FMV values, AutoDMA PCM layout, ENVX/NAX polling | docs/research/sndn2drv.md (R1), `sce/libsndn2/sound.c` |
| MMIX bit meanings | R1 ("PCM streams"), which read them from PCSX2 for behaviour; consistent with libsd's defaults 0xFF0/0xFFC |
| 4B: every command's libsd calls, the packet layouts, the reply page, the stream records, event queue, refill scheduler, FILL flag patching and PCM mixer, the pitch formula and table location | docs/research/sndn2drv.md (R1, from the user's `SNDN2DRV.IRX`) |
| 4B: the SDL3 audio stream calls and their thread safety | SDL 3.4.18's `include/SDL3/SDL_audio.h` and `SDL_hints.h` (zlib) |

Licences: psx-spx is documentation; ps2sdk is AFL-2.0 and only its
register layout, encodings and described behaviour were used; the PCSX2
wiki article is prose. No GPL code is in or behind this package
(docs/research/licences.md).

### Tables

- Gaussian table: psx-spx's 512 entries, transcribed by a script from the
  page's text and checked: the first sixteen are -1, the peak is 0x59B3,
  every set of four taps sums to 0x7F7F..0x7F81 (psx-spx's stated
  property). psx-spx shows no cosine series short of fifteen terms
  reproduces it, so the table is kept literal rather than generated.
- Reverb FIR: psx-spx's 39 taps (sum 0x7FFE, unity within 2 LSB).
- Reverb presets: psx-spx's ten examples in libsd's mode order. Compared
  word by word with ps2sdk's `EffectParams`: studio 1-3, hall, space and
  pipe are identical; off differs in unused address words, room in its
  same/different-side and comb 3/4 addresses; echo and delay differ in their APF sizes and most
  addresses (ps2sdk then rewrites them from delay/feedback). STUDIO_3, the
  only mode the game selects (R1), is the same in both.

## Approximations

Each is a row in docs/port/DIVERGENCES.md ("Platform"):

| row | what |
|---|---|
| A1 | reverb left and right computed in the same 24 kHz step |
| A2 | reverb rounding/saturation points; vIIR = -0x8000 quirk not modelled |
| A3 | ADPCM filters 5-7 as 0, shifts 13-15 as 9 |
| A4 | exponential decrease step floored (arithmetic shift) |
| A5 | envelope step counter handling across steps and phase changes |
| A6 | key on: no latency; histories zeroed; LSAX not reset |
| A7 | NAX position within a block |
| A8 | transfer duration (one frame, or a set rate) |
| A9 | reverb presets from psx-spx, delay/feedback ignored |
| A10 | ATTR mute / SPU-on bits ignored |
| A11 | AutoDMA input from a host ring, not sound RAM |
| A12 | no IRQ from reverb buffer accesses |
| A13 | reverb disabled: whole chain still read |
| A14 | `spu2_sd_init` values from ps2sdk, not the disc |
| A15 | pitch table: the formula when the disc's IRX cannot be read (4B) |
| A16 | stream cancel leaves queued KEYON events (4B) |
| A17 | driver writes take effect at the next vsync block; ENVX/NAX read at a block's end (4B) |
| A18 | a channel-1 ADPCM fill does not run the PCM mix callback (4B) |
| A19 | sample transfers complete one frame after they start (4B, closes A8's open choice) |
| A20 | commands 0x17/0x18 re-apply the last 0x15 attr, not the IRX's stale stack slot (4B) |
| A21 | an out-of-range pitch index is clamped, not read from the IRX's neighbouring data (4B) |
| A22 | the SDL output drops or pads whole vsync blocks to keep two vsyncs queued (4B) |

A4, A5, A6 and A7 can reach the EE: the sequencer frees a voice when ENVX
drops below 2, and the stream scheduler reads NAX once per tick. A tick is
960 frames at 50 Hz, so these matter only near a tick boundary. A16, A17 and A19
can reach the EE the same way (slot release, stream read offsets, the
DMA-status wait).

## Tests

`adpcm_test` (19 checks): five blocks decoded by hand in comments (no
filter, filter 1 with shift, filter 2 with negative history and floor
rounding, filter 4 saturation, reserved shift/filter), and history carried
across blocks.

`spu2_test` (112 checks):

- Gaussian table properties.
- ADSR: linear attack lengths for five rates against psx-spx's closed form
  `ceil(0x7FFF / ((7 - step) << max(0, 11 - shift))) << max(0, shift -
  11)`; linear release length; exponential attack; exponential decay at
  shift 4 against the geometric rate and at shift 11 exactly (9084 frames,
  derived band by band in the comment); exponential release reaches 0; rate
  0x7F never steps.
- Pitch: a 1 kHz sine encoded to ADPCM by a test-only encoder (best of the
  five filters and 13 shifts per block, closed loop), looped over 12 blocks,
  played at PITCH 0x0400, 0x0800, 0x1000, 0x2000, 0x3000; zero crossings
  over one second within 2 of 250/500/1000/2000/3000 Hz.
- Loop flags: end+mute silences and sets ENDX, LSAX latched by loop start,
  key on clears ENDX, key off release length.
- Mixing: one voice level through the interpolation gain, two voices
  saturating at the bus (32765 after master), negative volume, core 0
  through core 1's AVOL and MMIX, a linear volume sweep's length.
- A key on stamped for frame 50 starts there.
- AutoDMA input: both halves, half-done callbacks and the current half.
- Noise at the fastest and a slow clock.
- Transfers: busy for the set duration, refused while busy, callback,
  data in RAM; libsd status poll and wait; IRQ from a voice read and from a
  transfer.
- libsd front end: ESA from EEA for mode 4 on both cores (0x200000 - 0x6FE0
  and 0x1E0000 - 0x6FE0), address scaling, EVOL, core attributes, voice
  address with Sony's 0x40 flag, init defaults.
- Reverb impulse response (studio 3, wet only): output on both sides,
  energy peaks in the first 0.3 s and decays (last 0.1 s of 2 s under 5% of
  the peak; measured: about -45 dB), left and right differ; EVOL 0
  silences it.
- Determinism: a fixed scene (pseudo-random ADPCM over every filter and
  shift, four voices on both cores, noise, a sweep, a timed key off, reverb
  on both cores) renders the same twice and matches a golden FNV-1a
  checksum, 0x77DB2B76.

Results (2026-10-05): `linux-x64` both pass; `ref-m32` (i386) both pass
with the same checksum; the `asan` preset (ASan + UBSan) passes clean.
`win-x64` and `win-x86-ref` build without warnings; their executables were
not run (no Windows runner or Wine on the build host).

`sndn2_test` (996 checks, 4B; `sndn2_test [disc image]`):

- Transport: RPC 0x65 with `{0x1E, 0}` returns zeros and applies libsd's
  init (MMIX, VMIX); tick pages alternate; an upload (0x20, counter 7)
  lands in SPU RAM only when rendered and its counter appears at +0x1C0 of
  the next reply and in both pages; a read-back (0x21) returns the data;
  +0x1C4-0x1FF stay zero.
- Voices: 0x01-0x04 and 0x0A as register effects; ENVX in the reply after
  a key on, zero for the neighbour, below 2 after a key off (0x0B in
  SgQuit's shape).
- Core packets: EEA, effect mode with the clear bit dropped and depth 0,
  EVOL, MVOL, VMIXEL/R, NON, the noise clock on the slot's core, MMIX from
  0x4E; 0x1F stops the counter updates.
- Pitch: unity, octave up and down, a fifth, fine, scale, bend x range;
  an index far below the table is clamped and counted; the formula's
  anchors; with the disc image, the table from `SNDN2DRV.IRX`, its 32
  differences from the formula and the two outliers.
- ADPCM: a synthetic stereo `.int` ring (every block tagged with its
  sector, channel and block): first-half fills on ticks 1 and 2 with flags
  6/2, read offsets one tick behind, key on at tick 3, second-half fills
  from offset 0x4000 with flags 2/3, the refill of the first half from
  0x8000 when the voice crosses into the second half (about 15 vsyncs),
  looping with the envelope up, stop clearing offsets and releasing; a mono
  stream at 4x pitch wrapping its read offset at 0x5C000 in 0x2000 steps.
- PCM: two left channels adding with 16-bit wrap (2 x 0x6FFF), a right
  channel 0x400 ahead, the shift-2 channel's step, read offsets in the
  reply, stop by command and by stop offset, output through BVOL, quit.
- Sequencer, end to end: a synthetic `.hd` with one SE; `SgVabOpenFakeBody`
  leaves the header untouched; `SgSePlay` + `SgCalledTickProc` produce the
  SSA `(spu / 16 + 0x10) * 16`, pitch, ADSR, volume and key on through
  the head context and header records; the slot stays while ENVX says
  sounding and is freed after `SgSeStop` and a release; `SgVabClose`; the
  DMA status poll after `SgDmaWrite`.

Results (4B, 2026-10-05): `linux-x64` 34/34 ctest tests pass (`sndn2` with
the disc image); the `asan` preset (ASan + UBSan) runs `sndn2_test`,
`spu2_test` and `null_devices_test` clean; `win-x64` builds without
warnings in the new files, with `out_sdl.c` against SDL3.dll (not run: no
Windows runner or Wine). `ref-m32` and `win-x86-ref` no longer exist
(package 2J retired the 32-bit presets during 4B).

