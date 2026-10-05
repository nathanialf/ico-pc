# Audio: the software SPU2 (Phase 4A)

`port/audio/` holds a clean-room software SPU2: the PS2's sound processor
rendered on the host as 48 kHz interleaved stereo 16-bit, plus the SPU
ADPCM decoder and a libsd-shaped front end. Phase 4B builds the SNDN2DRV
host (`sndn2_host.c`, per docs/research/sndn2drv.md) and the SDL output on
top of it. Nothing here is wired into `ico_pc` yet.

| file | contents |
|---|---|
| `adpcm.h/.c` | SPU ("VAG") ADPCM block decoder: 16-byte blocks, 28 samples, five filter pairs, loop flags. Also for 4B's `.int` streams |
| `spu2.h/.c` | the SPU2: 2 cores x 24 voices, 2 MB sound RAM, envelopes, volumes and sweeps, noise, mixing, reverb, transfers, AutoDMA input, IRQ |
| `spu2_tables.c` | the Gaussian interpolation table, the reverb resampling FIR, the reverb presets (transcribed from psx-spx) |
| `spu2_internal.h` | tables and the envelope step, shared with the tests |
| `spu2_sd.h/.c` | libsd's calls (`sceSdSetParam`, `SetSwitch`, `SetAddr`, `SetCoreAttr`, `SetEffectAttr`, `VoiceTrans`, `Init`) with libsd's encodings, as register writes |
| `test/adpcm_test.c`, `test/spu2_test.c` | unit tests (below) |

Build: `ico_audio` static library; `adpcm_test` and `spu2_test` are ctest
tests `adpcm` and `spu2` (`port/audio/CMakeLists.txt`).

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

A4, A5, A6 and A7 can reach the EE: the sequencer frees a voice when ENVX
drops below 2, and the stream scheduler reads NAX once per tick. A tick is
960 frames at 50 Hz, so these matter only near a tick boundary.

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
