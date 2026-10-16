# Audio

How the game's sound reaches the speakers: the game's sequencer runs as compiled, its RPC to the
[IOP](ARCHITECTURE.md#terms) sound driver lands in a host server that does what SNDN2DRV.IRX did,
the driver's [libsd](ARCHITECTURE.md#terms) calls drive a software [SPU2](ARCHITECTURE.md#terms),
and once per [vsync](ARCHITECTURE.md#terms) the host loop renders that vsync's samples into an
SDL3 device, a WAV file, or nothing.
[ARCHITECTURE.md](ARCHITECTURE.md) places this layer among the others.

## Files

| path | what it holds |
| --- | --- |
| `sce/libsndn2/sound.c` | the Sg sequencer the game calls (Sony's `libsndn2.a(sound.o)`, reconstructed), with its host seams |
| `port/audio/spu2.h`, `spu2.c` | the software SPU2: voices, envelopes, interpolation, mixing, reverb, transfers, the AutoDMA input, IRQ |
| `port/audio/spu2_internal.h` | what `spu2.c`, `spu2_tables.c` and the tests share; the stage profile |
| `port/audio/spu2_tables.c` | the Gaussian interpolation table, the reverb resampling filter, the built-in reverb presets |
| `port/audio/adpcm.h`, `adpcm.c` | the SPU ADPCM block decoder |
| `port/audio/spu2_sd.h`, `spu2_sd.c` | the libsd front end: libsd's calls and encodings as SPU2 register writes |
| `port/audio/libsd_irx.h`, `libsd_irx.c` | the reverb presets and the idle voice block read from the disc's LIBSD.IRX |
| `port/audio/sndn2_host.h`, `sndn2_host.c` | SNDN2DRV.IRX as a host SIF server: the packet dispatcher, the reply page, the pitch table |
| `port/audio/sndn2_internal.h` | what the dispatcher and the stream engines share |
| `port/audio/stream.c` | the driver's ADPCM stream engine and PCM mixer |
| `port/audio/stream_watch.h`, `stream_watch.c` | the stream log lines, the heartbeat's stream part and the end-of-run stream counts |
| `port/audio/mix_gain.h`, `mix_gain.c` | the music, effects and film gains on the voice volumes |
| `port/audio/volume.c` | the output volume and the output's play ratio |
| `port/audio/audio_host.h`, `audio_host.c` | the per-vsync render and its sinks; mirror mode's pan swap |
| `port/audio/out_sdl.c` | the SDL3 output (window build) |
| `port/audio/wav.c` | the WAV writer behind the audio dump |
| `port/audio/CMakeLists.txt` | the `ico_audio` and `ico_sndn2` libraries and the audio tests |
| `port/audio/test/` | the audio unit tests and the render benchmark |

`ico_audio` (the SPU2, ADPCM, libsd front end, tables, volume, gains) is integer code with no
platform dependency; `ico_sndn2` adds the sequencer, the driver, the LIBSD.IRX reader, the
streams, the audio host and the WAV writer, and `out_sdl.c` when the build has a window and SDL3
(`port/audio/CMakeLists.txt`).

## The path of a sound

1. The game's sound thread calls the Sg sequencer (`SgSePlay`, `SgCalledTickProc`, the
   `SgStAdpcm*` and `SgStPcm*` stream calls). The sequencer fills a page of 16-byte packets and
   sends it to the IOP by [SIF](ARCHITECTURE.md#terms) RPC (`sce/libsndn2/sound.c`).
2. The [SIF](ARCHITECTURE.md#terms) layer runs the registered host server synchronously
   (`port/data/sif_host.c`; [DATA.md](DATA.md) "SIF and IOP RAM"). The server is
   `ico_sndn2_host_serve` (`port/audio/sndn2_host.c`).
3. Each packet becomes the libsd calls SNDN2DRV.IRX made, through the
   front end in `port/audio/spu2_sd.c`, which turns them into timed SPU2 register writes and
   transfers.
4. Once per vsync the host loop calls `ico_audio_host_vsync` (`port/platform/host_loop.c`), which
   renders the vsync's 48 kHz frames with `spu2_render` and hands them to the sinks
   (`port/audio/audio_host.c`).

Everything runs on the one simulation thread: the server on the game's sound
[fiber](ARCHITECTURE.md#terms), the render in [host context](ARCHITECTURE.md#terms) at the vsync,
so the calls never overlap and nothing is locked (`port/audio/sndn2_host.h`, header comment;
`port/audio/audio_host.h`, header comment). The driver runs in every build, headless included,
because the game reads the envelopes and stream offsets the driver reports.

## The software SPU2

`port/audio/spu2.h` models the PS2's sound processor: two cores of 24 ADPCM voices over 2 MB of
sound RAM (`SPU2_RAM_SIZE`), a reverb unit per core, core 0's output mixed into core 1, rendered as
48 kHz interleaved stereo 16-bit (`SPU2_RATE`). It is clean-room: the voice, envelope,
interpolation and reverb units follow psx-spx (the SPU2 cores share them with the PS1 SPU), the
register map follows ps2sdk's `spu2_mmio_hwport.h` layout, and no emulator code is used
(`port/audio/spu2.h`, header comment). Every behaviour in `spu2.c` is from psx-spx's SPU
description unless a comment there names another source (`port/audio/spu2.c`, header comment).

### Units and registers

SPU addresses (SSA, LSAX, NAX, ESA, IRQA, TSA and the reverb offsets) count 16-bit halfwords and
are 20 bits wide (`SPU2_ADDR_MASK`); a byte address is twice the SPU address. Time is the output
frame counter, 0 at `spu2_reset` and read with `spu2_time` (`port/audio/spu2.h`, Units).

Registers are addressed by their offset within a core in ps2sdk's layout: a voice's parameters
at `SPU2_R_VP(v) + SPU2_VP_*`, its addresses at `SPU2_R_VA(v) + SPU2_VA_*`, the voice switches
(PMON, NON, VMIXL/R, VMIXEL/R, KON, KOFF, ENDX) as pairs of halfwords (voices 0-15, then 16-23),
and the 0x760 block (master, effect, external and AutoDMA input volumes, the reverb coefficients)
with core 0's offsets for both cores, the `core` argument choosing the bank (`port/audio/spu2.h`,
register offsets).

`spu2_write_reg` queues a write to take effect before the frame rendered at its time; writes with
equal times apply in call order, and a time already rendered means "before the next frame".
`spu2_read_reg` returns the state as of the last rendered frame; ENVX, VOLX, MVOLX, NAX, LSAX,
ENDX and STATX read live state (`port/audio/spu2.h` (`spu2_write_reg`, `spu2_read_reg`)). Writes
and transfers wait in a time-ordered event queue of 16384 entries; a full queue runs its oldest
event at once (`port/audio/spu2.c` (`ev_push`, `ev_run_due`)).

A write to NAX moves the voice to that block and reloads it; a write to the DATA port stores the
halfword at TSA and advances TSA (the manual transfer); KON and KOFF key the voices whose bits are
set (`port/audio/spu2.c` (`reg_apply`)).

### One frame

The reference renderer, `frame`, renders one output frame (`port/audio/spu2.c`, header comment):

1. apply the queued register writes and transfers due at this frame;
2. for core 0, then core 1 (`core_frame`): run the 24 voices, each one interpolated (or the noise
   generator's level for a voice in NON), enveloped and scaled by its volume, summed into the dry
   and wet buses by VMIXL/VMIXR/VMIXEL/VMIXER; then `core_finish` adds the external input (core
   1: core 0's output through AVOL) and the AutoDMA input (through BVOL) as MMIX selects, runs the
   reverb, adds its output through EVOL and applies the master volume;
3. store the write-back areas and output core 1's result.

The write-back areas follow the PCSX2 wiki's description: each core writes its voice 1 and voice
3 outputs (core 0 at halfword 0x400 and 0x600, core 1 at 0xC00 and 0xE00), core 0 its output
(0x800, 0xA00), and each core its four voice buses (core 0 from 0x1000, core 1 from 0x1800), every
area a ring of 0x200 samples indexed by the frame count (`port/audio/spu2.c` (`core_finish`)).

### Voices

`key_on` latches SSA as the voice's address, clears the decoder history and the interpolator,
starts the attack phase from level 0, clears the voice's ENDX bit and loads the first block
(`port/audio/spu2.c` (`key_on`)). `key_off` moves a sounding voice to release.

A voice decodes one 16-byte ADPCM block at a time (`voice_load_block`). A block with the loop
start flag sets LSAX to its own address. At the end of a block with the loop end flag the voice
sets its ENDX bit and jumps to LSAX; without the repeat flag as well, it is the "end + mute" code
and the envelope drops to 0 in release (`voice_fetch`). A block may start in the last 16 bytes of
sound RAM and then wraps to address 0, as every access does (`ram_block`).

NAX reads as the halfword the next sample comes from: the block's address plus 1 (the header
halfword) plus the sample position over 4 (`port/audio/spu2.c` (`voice_nax`)). The driver's
stream scheduler reads it to tell which half of a ring a voice is playing (below).

The pitch counter follows psx-spx's "Pitch Counter": the PITCH register is added each frame (capped
at 0x4000, four times the output rate), whole samples are shifted into the interpolator, and a
voice in PMON has its step scaled by the previous voice's output of the same frame
(`port/audio/spu2.c` (`voice_frame`)). Interpolation is the 4-point Gaussian of `spu2_gauss`
indexed by the counter's fraction (`interpolate`). The noise generator steps by each core's ATTR
noise clock (`noise_tick`).

### Envelopes and volumes

`env_tick` is one step of the SPU envelope generator for ADSR phases and volume sweeps: a 7-bit
rate (shift and step), linear or exponential, increasing or decreasing, with psx-spx's special
cases (an exponential increase above 0x6000 slows, an exponential decrease scales the step by the
level) and the "all rate bits set" case that neither steps nor saturates. `spu2_env_tick` exposes
it to the tests (`port/audio/spu2_internal.h` (`spu2_env_tick`); `port/audio/spu2.c`
(`env_tick`)).

`adsr_tick` runs a voice's ADSR from ADSR1 and ADSR2: attack until the level reaches 0x7FFF,
decay (exponential) while the level is above the sustain level (SL + 1) x 0x800, sustain at its
own rate and direction, release until the level reaches 0 and the voice goes off
(`port/audio/spu2.c` (`adsr_run`, `adsr_tick`)).

A voice or master volume register with bit 15 clear is a fixed level (bits 0-14, the volume over
2); with bit 15 set it sweeps from the current level through `env_tick` (bit 14 exponential, 13
decrease, 12 negative phase, bits 0-6 the rate) (`port/audio/spu2.c` (`vol_write`, `vol_tick`)).

### Reverb

`reverb_step` is one 24 kHz step of psx-spx's "Reverb Formula" over the core's work area, from
ESA to the end address (EEA << 16 | 0xFFFF); `rv_plan_build` reduces every address offset modulo
the area's size once per chunk, so a step needs no division (`port/audio/spu2.c` (`reverb_step`,
`rv_plan_build`)). The work area is written only with ATTR's effect bit set
(`SPU2_ATTR_EFFECT`).

`reverb_frame` runs the reverb every second frame: the wet input goes through the 39-tap
resampling filter `spu2_reverb_fir` down to 24 kHz, and the output through the same filter back
up to 48 kHz (zero-stuffed, gain 2). The filter is half-band and symmetric, so only its non-zero
taps are summed, in pairs, with the same integer result (`port/audio/spu2.c` (`fir_even`,
`reverb_frame`)).

The ten reverb presets are libsd's effect modes (off, room, studio small, medium and large, hall,
space echo, echo, delay, pipe): a work area size in bytes and the 32 register values in psx-spx's
PS1 order (`port/audio/spu2.h` (`spu2_reverb_preset`)). `spu2_reverb_apply_preset` writes one as
libsd's `sceSdSetEffectAttr` does: the work area is the last size/2 halfwords below the core's end
address, the address registers take the preset's 8-byte units times 4, the coefficients go in
unchanged (`port/audio/spu2.h` (`spu2_reverb_apply_preset`)).

### Transfers, the AutoDMA input and IRQ

`spu2_dma_write` and `spu2_dma_read` copy at once. `spu2_voice_trans` is a timed transfer on DMA
channel 0 or 1 (libsd's channel is the core): the data is copied at its time, the channel stays busy
for the transfer's duration, then the channel's callback runs; a call on a busy channel returns -1,
libsd's busy check (`port/audio/spu2.h` (`spu2_voice_trans`)). The duration is the length over the
rate set by `spu2_set_dma_rate`, rounded up; at rate 0, the default, a transfer completes one frame
after it starts (`port/audio/spu2.c` (`dma_duration`)). Nothing outside `port/audio/spu2.c` and the
tests calls `spu2_set_dma_rate`, so every transfer takes one frame. `spu2_voice_trans_wait` finishes
a transfer at once (libsd's blocking status wait, which cannot advance render time).

The AutoDMA input reads a host ring of 0x800 bytes as libsd's looping block transfer lays it out:
two halves of 0x400 bytes, each 256 left samples then 256 right. One stereo sample is consumed per
frame, and each finished half calls the callback, which refills that half in place
(`port/audio/spu2.h` (`spu2_memin_start`); `port/audio/spu2.c` (`memin_sample`, `memin_advance`)).

A core with ATTR's IRQ bit set raises its IRQ when a voice reads the ADPCM block holding IRQA, a
transfer or a DATA-port write stores to it, or a write-back stores to it; the flag stays set until
`spu2_irq_clear` (`port/audio/spu2.c` (`irq_check`); `port/audio/spu2.h`, Interrupts). The driver
sets no IRQ callback.

### Chunked rendering

`spu2_render` gets the reference renderer's bits faster by rendering up to 256 frames voice by
voice (`render_chunk`), over a stretch where nothing outside the SPU2 can look or change anything:
no queued write or transfer falls due, no transfer finishes, the AutoDMA callback comes only after
the last frame, and no IRQ can be raised (each core's IRQ disabled or already pending)
(`port/audio/spu2.c`, "Chunked rendering", `chunk_frames`, `chunk_allowed`). Inside such a stretch
the registers are constant and voices of one core interact only through PMON. A voice that loads a
block from sound RAM the chunk's frames write (a write-back or reverb work area) flags a hazard;
the voices are put back and the stretch renders frame by frame. `spu2_get_stats` counts the
chunks, the chunked and exact frames and the hazards; `ico_audio_host_shutdown` logs them
(`audio: SPU2 rendered ...`). `spu2_set_exact(1)` forces the reference renderer, for the tests
that compare the two (`port/audio/spu2_internal.h` (`spu2_set_exact`)).

### Determinism

The SPU2 uses integer arithmetic only (right shifts of negative values are arithmetic on every
supported compiler), so the same writes at the same times render the same bits on every host
(`port/audio/spu2.h`, Determinism). `spu2_render_crc` holds the renderer to golden CRCs (Tests).

### Tables

`port/audio/spu2_tables.c` holds three tables transcribed by script from psx-spx's "Sound
Processing Unit (SPU)" page, unedited (`port/audio/spu2_tables.c`, header comment):

| table | what it is |
| --- | --- |
| `spu2_gauss` | the 512-entry "4-Point Gaussian Interpolation" table; each set of four taps sums to 0x7F7F..0x7F81 (`spu2_test` checks it) |
| `spu2_reverb_fir` | the 39-tap "Reverb Buffer Resampling" filter |
| `spu2_reverb_presets` | "SPU Reverb Examples" in libsd's effect mode order, with psx-spx's work area sizes in bytes; the built-in values the disc's LIBSD.IRX replaces |

### ADPCM

`port/audio/adpcm.h` decodes the SPU ("VAG") ADPCM block: 16 bytes to 28 signed samples. Byte 0
holds the shift (bits 0-3) and filter (bits 4-6), byte 1 the flags (bit 0 loop end, bit 1 loop
repeat, bit 2 loop start), bytes 2-15 the 28 nibbles, low nibble first. A sample is
`clamp16((nibble << 12 >> shift) + ((old*f0 + older*f1 + 32) >> 6))` with one of the five filter
pairs (0,0) (60,0) (115,-52) (98,-55) (122,-60) (`port/audio/adpcm.h`, header comment). Written
from psx-spx's description, integer only (`port/audio/adpcm.c`, header comment). The game's
`.int` streams are decoded here too, as SPU2 voices.

### Where the model is approximate

What the sources state as unknown or approximate:

- Filters 5-7 are not documented and decode as filter 0 (`port/audio/adpcm.h` (`adpcm_filter`));
  shifts 13-15 are reserved and act as 9 (`port/audio/adpcm.c` (`adpcm_decode_block`)).
- `spu2_sd_set_effect_attr` ignores the delay and feedback fields (`port/audio/spu2_sd.h`
  (`spu2_sd_set_effect_attr`)), and the S/PDIF mode does nothing (`port/audio/spu2_sd.c`
  (`spu2_sd_set_core_attr`)).
- Transfer durations are one frame (above): the real DMA rate is not modelled.
- Without the disc's tables the reverb presets are psx-spx's and the pitch table is a formula that
  is 1 off in 32 entries (below).
- Whether the game ever indexes the pitch table outside its 608 entries is not known
  (`port/audio/sndn2_host.c` (`ico_sndn2_pitch_compute`)).

## The libsd front end

`port/audio/spu2_sd.h` gives the SPU2 the calls SNDN2DRV.IRX makes, with libsd's argument
encodings (`SD_VPARAM_*`, `SD_PARAM_*`, `SD_SWITCH_*`, `SD_ADDR_*`, `SD_CORE_*`, the effect modes)
restated as `SPU2_SD_*` so the port does not depend on ps2sdk's headers. Behaviour follows the
disc's LIBSD.IRX where its code was read and ps2sdk's clean-room libsd (`freesd.c`, `effect.c`,
`voice.c`; read for behaviour, no code taken) elsewhere (`port/audio/spu2_sd.h`, `spu2_sd.c`,
header comments).

Every write is stamped with the time set by `spu2_sd_set_time` (never behind `spu2_time`). The
driver stamps each RPC with the current SPU2 time, so its writes take effect at the start of the
next rendered block: timing is block-accurate, one vsync per block (`port/audio/sndn2_host.c`
(`ico_sndn2_host_serve`)).

The calls:

- `spu2_sd_set_param` / `get_param` (voice VOLL, VOLR, PITCH, ADSR1, ADSR2, ENVX, VOLX; MMIX and
  the core volumes), `set_switch` / `get_switch` (PMON, NON, KON, KOFF, ENDX, the VMIX masks),
  `set_addr` / `get_addr` (ESA, EEA, TSA, IRQA, a voice's SSA, LSAX, NAX; byte addresses), and
  `set_core_attr` / `get_core_attr` (effect enable, IRQ enable, mute, noise clock, S/PDIF), each
  mapped onto the register its libsd entry names.
- `spu2_sd_init` is LIBSD.IRX's `sceSdInit`, read off its code: on a cold init every voice keyed
  off, PMON and NON cleared on core 1 only (the IRX's loop over the cores never steps the register
  base), the mode-0 effect area placed below each core's end address; then the idle block written
  at byte 0x5000 with the 16 bytes that follow it (zero), every voice's volumes zeroed, pitch
  0x3FFF and SSA at the idle block, all voices keyed on and off, ENDX of core 0 cleared, and the
  core volumes set as `InitCoreVolume` does (`port/audio/spu2_sd.c` (`spu2_sd_init`)).
- `spu2_sd_set_effect_attr` loads the mode's preset, sets EVOL to the depths and ESA from the
  core's EEA, and with `SPU2_SD_EFFECT_MODE_CLEAR` zeroes the work area; a mode above 9 returns
  -1.
- `spu2_sd_voice_trans` is `sceSdVoiceTrans` (returns the size, or -1 while the channel is busy);
  `spu2_sd_voice_trans_status` polls (flag 0) or finishes the transfer at once (flag 1).

## Values read from the disc

No byte of Sony's modules is in the repository; the two tables the SPU2 needs from them are read
from the player's disc at start-up (`port/audio/libsd_irx.h`, `port/audio/sndn2_host.h`).

### LIBSD.IRX

`ico_libsd_load` opens `LIBSD.IRX` on the mounted disc, checks its size against the PAL disc's
(`ICO_LIBSD_IRX_SIZE`, 26285 bytes), finds the `.data` section by name and reads three tables at
the module addresses found through the code that reads them: the ten work area sizes at 0x41C8,
the ten 0x44-byte presets at 0x41F0 (both read by `sceSdSetEffectAttr`), and the 16-byte idle
voice block at 0x4A60 (written by `InitVoices`) (`port/audio/libsd_irx.h` (`ICO_LIBSD_*`)).
`ico_libsd_parse` refuses a table that fails its structural checks (every preset's flags word 0,
its address registers inside its work area, the input volumes of modes 1-9 0x8000, the idle
block's loop flags).

On success the values are installed (`spu2_reverb_set_preset`, `spu2_sd_set_idle_block`) and one
line names any mode that differs from the built-in values:

    libsd: reverb presets, work area sizes and idle block from the disc's LIBSD.IRX

On any failure the built-in values stay and one line says why:

    libsd: <reason>; using the built-in reverb presets (psx-spx) and idle block (ps2sdk)

The built-in idle block is ps2sdk's `VoiceDataInit`, sixteen 0x07 bytes, the same as the disc's
(`port/audio/spu2_sd.c` (`idle_block`)).

### SNDN2DRV.IRX's pitch table

The driver turns a note into a PITCH value through a table of 608 u16 entries in 1/16-semitone
steps, entry 208 = 0x1000. `ico_sndn2_pitch_load` reads it from `SNDN2DRV.IRX` on the disc (file
offset 0x3900, 0x4C0 bytes) after checking the file's size against the PAL disc's (20941 bytes);
a table without 0x1000 at entry 208 is refused. The table is not checked for order: the disc's has
two entries below their predecessors (64 and 172) (`port/audio/sndn2_host.c`
(`ico_sndn2_pitch_load`)). The log line on success:

    sndn2: pitch table: 608 entries from the disc's SNDN2DRV.IRX

Without the file the driver uses `floor(4096 * 2^((i - 208) / 192))`
(`ico_sndn2_pitch_formula`), 1 low or high in 32 entries of the disc's table, and says:

    sndn2: pitch table: <reason>; using the formula floor(4096 * 2^((i - 208) / 192))

`ico_sndn2_pitch_compute` is command 0x04's arithmetic, the IRX's 32-bit unsigned arithmetic: the
table index from the note's distance to the base note within the octave, the pitch bend times its
range and the fine tune, the entry shifted by whole octaves, then scaled by 441/480 and by the
packet's 24-bit scale in 1/4096 units. The IRX does not check the index; the port clamps it,
counts it (`ico_sndn2_pitch_clamps`) and logs the first one:

    sndn2: pitch index <i> outside the table (base ... note ... fine ... bend ... range ...): clamped

## The sound driver: SNDN2DRV.IRX as a host server

`ico_sndn2_host_register` is called by the sequencer's `SgSndn2RemoteInit` before it binds. The
first call powers the SPU2 on (`spu2_reset`), loads the pitch table and LIBSD.IRX's values, and
registers `ico_sndn2_host_serve` under the IRX's server id `ICO_SNDN2_SERVER_ID` ("sndn"); later
calls register again only (`port/audio/sndn2_host.c` (`ico_sndn2_host_register`)). Every command
and its libsd calls are as read off the code of the player's SNDN2DRV.IRX (PAL disc); the comments
name the IRX's handler addresses (`port/audio/sndn2_host.c`, header comment).

`ico_sndn2_host_serve` takes two RPC numbers (`port/audio/sndn2_host.h`):

- `ICO_SNDN2_RPC_TICK` (0x64): a page of 16-byte packets, each dispatched in order, then the ADPCM
  stream scheduler and its queue (`st_adpcm_tick`), then the 0x200-byte reply page.
- any other number (the [EE](ARCHITECTURE.md#terms) sends only `ICO_SNDN2_RPC_INIT`, 0x65): one
  packet, and a zeroed return page.

### The packets

A packet is four little-endian words: the command, an id (a voice slot, core * 24 + voice, or a
mask), and two arguments (`port/audio/sndn2_host.c` (`dispatch`)):

| command | effect |
| --- | --- |
| 0x01 | a voice's VOLL and VOLR, through the music and effects gains (`ico_audio_gain_voice`) |
| 0x02 | a voice's ADSR1 and ADSR2 |
| 0x03 | a voice's SSA |
| 0x04 | a voice's PITCH from `ico_sndn2_pitch_compute` |
| 0x0A, 0x0B | KON, KOFF for the two cores' masks (0x0B's id is ignored) |
| 0x0C | the effect send masks VMIXEL and VMIXER, per core |
| 0x0D | NON masks |
| 0x14 | a core's EEA |
| 0x15 | a core's effect mode (depths 0) and its effect enable |
| 0x16 | a core's EVOL left and right |
| 0x17, 0x18 | the last effect attributes with a new delay or feedback, re-applied (0x17 is never sent by the game) |
| 0x1E | `sceSdInit` (hot when the id's bit 0 is set), the cached voice volumes forgotten, the effect sends cleared, channel 0's DMA-done callback installed |
| 0x1F | channel 0's callback removed |
| 0x20, 0x21 | a transfer to or from sound RAM on channel 0, from IOP RAM; a range outside IOP RAM is dropped and logged once |
| 0x22 | the channel 0 status into the return word (never sent by the EE) |
| 0x28 | a core's master volume |
| 0x32 | sub-command 8: a core's noise clock; 10: the S/PDIF mode (nothing on the host); others logged once |
| 0x3C-0x43 | the ADPCM streams: 0x3C init, 0x3D nothing, 0x3E open, 0x3F close, 0x40 volume, 0x41 pitch, 0x42 play, 0x43 stop |
| 0x46-0x4F | the PCM channels: init, quit, open, close, volume, play, stop, seek, effect, buffer mode |

Any other command is logged once (`sndn2: command 0x.. ignored (not one the IRX handles)`) and does
nothing, as the IRX's jump table does. The driver's `sndn2:` diagnostics go through
`sndn2_log_once`: one line per kind of anomaly, then silence (`port/audio/sndn2_internal.h`); the
two below are counted instead.

### The EE's packet page

The sequencer queues every command in a page of at most 255 packets, which the sound thread's tick
sends (`_SgCalledTickProc`). On the PS2 a packet that finds the page full is dropped:
`_SgSetPkAdd` returns -1 and no caller checks (`sce/libsndn2/sound.c` (`_SgSetPkAdd`)). A stream
open that lands on a full page would lose its volume (0x40) or its key-on (0x42), which the game
sends once, and the piece would play silently or not at all for its whole length. The port hands
such a packet to the driver instead (`ico_sndn2_pk_full`, `port/audio/sndn2_host.h`): up to 4096
are kept and run right after the page's own packets in the same tick, in the order sent; the EE
still sees -1 and an unchanged page. This is [DIVERGENCES.md](DIVERGENCES.md) AU-7. The first full
page and every 100th after the first 8 are logged with the page's commonest commands, and every
full page is counted:

```
sndn2: packet page full (255 packets: 0x01 x255); the later packets of this tick are carried to the driver (page 1)
sndn2: the first packet carried past the page's 255 (packet 0x01): the PS2 would have lost it; the driver runs it after the page's own
sndn2: the first stream packet carried past the page's 255 (0x3E, a stream's open): without the carry the stream's volume would have been lost
```

The two carry lines come once a run each, the second for the first stream packet (0x3E open, 0x40
volume, 0x41 pitch, 0x42 play, 0x43 stop) the carry saved: the line that proves issue 101's
mechanism happened in a player's run (`port/audio/stream_watch.c` (`sw_carried`)).

A key-off from the sequencer that lands on one of the four stream voices (slot state 3, taken by
`SgGetSpuSlotMalloc(1)` in `AdpcmStreamInit`) is logged for the first 8 and counted, naming the
function that set the key-off bit: `sndn2: key-off from the sequencer (_SgContDump) on stream voice 2
(stream 43)`. Of the six sites (`_SgSeKeyOff`, `_SgSeqKeyOff`, `_SgContDump`, `SgVabClose`,
`SgBgmStop`, `SgSeStop`), only `_SgContDump` (the damper pedal's release) tests no slot state
(`sce/libsndn2/sound.c` (`sgHostKeyOffCheck`)).

### The reply page

Each tick fills one of two alternating 0x200-byte pages (`port/audio/sndn2_host.c` (`fill_page`);
offsets in `port/audio/sndn2_host.h`):

| offset | content |
| --- | --- |
| 0x000 | 48 words: each slot's ENVX & 0x7FFF |
| 0x0C0 | 48 words: each ADPCM stream's IOP read offset |
| 0x180 | 16 words: each PCM channel's read offset |
| 0x1C0 | the transfer counter of the last 0x20/0x21, written into both pages by the DMA-done callback (`dma0_done`) |

The game reads the envelopes and the offsets from this page, which is why the driver runs even
without an audio device (`port/audio/audio_host.h`, header comment).

## Streams

`port/audio/stream.c` holds the IRX's two stream engines. Both read their data where the game put
it, in the IOP RAM stand-in (`port/data/iop_ram.h`) (`port/audio/stream.c`, header comment).

### ADPCM streams

The game's background reader fills each stream's IOP ring from the `.int` files with
`sceCdReadIOPm` (`ico2/fumi/sound/adpcm_init.c`, `ico2/fumi/ios/cdvd.c`). The IOP ring holds the
file's interleave: n = 1, 2 or 4 channels, 0x800 / n bytes of each per 0x800-byte sector. Each
stream voice plays an SPU ring (16 KB in the game) as one loop in two halves
(`port/audio/stream.c`, header comment):

- Every tick, `schedule` reads each active voice's NAX; when the voice has moved into the other
  half, it queues a FILL of the half it left.
- `run_queue` runs while DMA channel 1 is idle and does at most one transfer per tick:
  `run_fill` de-interleaves the voice's channel into the IRX's 0x2000-byte staging buffer,
  patches the loop flags of the first and last block (so the ring loops and LSAX re-latches at its
  start), and sends it on channel 1. A queued KEYON keys its voices on and starts their
  scheduling.
- The IOP read offset the EE polls moves on by half the SPU ring times n when the next tick finds
  the channel idle.

The queue holds 128 events; a full queue drops the new event (`sndn2: ADPCM stream event queue
full: the new event is dropped`), as the IRX does. Closing a stream forgets its pending fill,
zeroes its queued FILLs and removes its bit from a queued KEYON; for a core 0 voice the IRX clears
the same bit in both cores' masks, and so does the port (`port/audio/stream.c` (`cancel`)). A fill
that reads outside IOP RAM writes zeros and is logged once.

### What the log says about streams

A stream that is keyed on at zero volume, or keyed off while it plays, still consumes its ring at
the real rate (the SPU2 runs every voice whatever its envelope, `port/audio/spu2.c`), closes on
time and leaves the open and close lines as a stream that was heard. So the log also writes down
the levels the voices play at (`port/audio/stream_watch.h`, header comment):

| line | written by | when |
| --- | --- | --- |
| `adpcm: open 43 slot 0 voices 0,1` | the game (`adpcmDataSet`) | the open: stream number, the game's stream slot (0 or 1), the SPU2 voices |
| `adpcm: 43 voices 0,1 vol 3fff/3fff env 7fff/7fff (opened on a page of 37 packets)` | the driver (`stream_watch.c`) | two driver ticks after the stream's key-on; the end says how full the packet page its open ran on was, or `(opened on a full page: carried past its 255)` |
| `adpcm: 43 at half: voices 0,1 vol ... env ...` | the driver | once the driver has fed half the file's bytes to the voices |
| `adpcm: 43 at close: voices 0,1 vol ... env ...` | the driver | the stop (0x43) reaching the driver, before its key-off |
| `adpcm: close 43 (now)` or `(fade)` | the game (`AdpcmClose`) | the close; "fade" when a fade ran out |

`vol` is, per voice, the louder side of its VOLL/VOLR registers as the SPU2 holds them, so it
includes the Music volume setting (`mix_gain.h`); a stereo stream's voices are left-only and
right-only, so both read the level. `env` is each voice's ENVX. A healthy stream reads `vol
3fff/3fff` at 100 % music and a nonzero `env`; a fade's close reads a small `vol`. At most three
driver lines per stream. Examples from `adpcm_stream_test`:

```
adpcm: open 43 slot 0 voices 0,1
adpcm: 43 voices 0,1 vol 3fff/3fff env 7fff/7fff (opened on a page of N packets)
adpcm: 43 at half: voices 0,1 vol 3fff/3fff env 7fff/7fff
adpcm: 43 at close: voices 0,1 vol 3fff/3fff env 7fff/7fff
adpcm: close 43 (now)
```

The heartbeat (`ico_host_status`, `port/platform/trace_host.c`) ends with the two stream slots as
the driver plays them, `stream@first voice` or `-` for an empty slot (`43@0 opening` between the
open and the key-on):

```
... | adpcm: -, 44@2 vol 3fff/3fff env 7fff/7fff
```

The end of the run adds `audio: streams: N opened, F of them on a full packet page; M key-offs from
the sequencer on stream voices; P packet pages full (Q packets carried, S of them a stream's, R
dropped)`: F counts the game's streams whose open packet ran on a page of 255 or was carried past
it, so a run whose music played well still says whether issue 101's case came up. The game's background disc reader logs a read
it is about to retry, the first 8 and then every 100th (`port/data/cdvd_host.c`
(`ico_cdvd_host_bg_retry`)): `cdvd: background read of <file> at lsn N retried (the drive was not
ready; retry 1)`, or `(read error E; retry 1)`.

### PCM channels

The movie player's PCM reaches the driver through 16 PCM channels whose buffers the game fills by
SIF DMA ([FMV.md](FMV.md)). Core 1's AutoDMA input reads the staging buffer as a 2 x 0x400 ring;
at each half-done interrupt the finished half is zeroed and the 16 channels are mixed into it with
16-bit wrapping adds, each channel's volume scaled by the film gain (`port/audio/stream.c`
(`pcm_half_done`, `pcm_mix`)). A channel's control word holds the bytes per callback and the
sample-step shift; a stop offset (buffer mode 1) ends it. `st_pcm_effect` sets MMIX on both cores
for the two effect ids the IRX knows (4 and 8).

## Mixing and volume

### Music, effects and film gains

The SPU2 renders one stereo mix, so the music and effects volumes scale each voice's own volume
registers by the category of what the voice plays (`port/audio/mix_gain.h`, header comment):

- Each of the 48 slots carries a category tag, set where the game decides what plays on it: the
  sequencer's volume packet (`_SgSeqSeVolume`, `sce/libsndn2/sound.c`) tags a BGM sequence's
  voice as music and an SE sequence's as effects; an ADPCM stream's open (`adpcmDataSet`,
  `ico2/fumi/sound/adpcm_init.c`, through `ico_audio_tag_stream`) tags its voices as music, or as
  effects for Yorda's hint voice (streams 101 to 104).
- The driver's voice volume writes (packet 0x01 and the stream volume 0x40) go through
  `ico_audio_gain_voice`, which caches the raw VOLL/VOLR and writes them scaled by the slot's
  gain. At 100 % the written word is the raw word, bit for bit. Untagged slots are unscaled.
- A volume word in sweep mode (bit 15) is written unscaled (logged once while a gain is below
  100 %); the game's voice volumes are all fixed levels.
- The film category scales the PCM mixer's channel volumes (`ico_audio_gain_pcm`). Nothing sets
  it: the Settings menu and the `[audio]` keys set music and effects only
  (`port/ui/settings.c`; `port/audio/audio_host.c` (`ico_audio_host_init`)).
- A gain change re-issues the cached volumes of the slots it covers, so it is heard at once
  (`ico_audio_set_gain`).

Gains are 0.0 to 1.0, clamped (NaN as 0), stored in 1/4096 steps (`ICO_AUDIO_GAIN_ONE`); the
scaling rounds the signed 15-bit level (`port/audio/mix_gain.c` (`ico_audio_gain_scale`)). The
driver's init (0x1E) and reset forget the cache (`ico_audio_gain_forget`).

### Output volume

`[audio] volume` (0.0 to 1.0) is stored by `ico_audio_set_volume` as a 0..256 gain in 1/256
steps and applied by `ico_audio_scale` with integer rounding and clamping to the SDL device's
blocks only; a WAV dump stays unscaled (`port/audio/volume.c`; `port/audio/audio_host.h`
(`ico_audio_set_volume`)). The
value is read on every push, so a change from the Settings menu is heard once the queued audio
(the cushion below and a vsync) has played.

### Mirror mode

While the mirror option is on (`ico_opt_mirror`, `port/game/options.h`),
`ico_audio_host_vsync` swaps left and right in each rendered block, so the device and the WAV dump
hear the mirrored pan; the SPU2 and the driver are untouched (`port/audio/audio_host.c`
(`ico_audio_pan_mirror`)).

## The audio host

`ico_audio_host_init` reads the environment `port/platform/host_config.c` sets from the ini and
config, powers the driver on and opens the sinks (`port/audio/audio_host.h`
(`ico_audio_host_init`)):

| variable | from | effect |
| --- | --- | --- |
| `ICO_AUDIO_DUMP` | ini `audio_dump=` (a path; `1` is `logs/audio.wav`) | write the mixed output to that WAV |
| `ICO_AUDIO` | ini `audio=` | `0`: no audio device in the window build |
| `ICO_AUDIO_VOLUME`, `ICO_AUDIO_MUSIC`, `ICO_AUDIO_EFFECTS` | `[audio] volume`, `music`, `effects` | the output volume and the two gains |
| `ICO_AUDIO_DEVICE` | `[audio] device` | the SDL output device by name |

Its log lines: `audio: writing the mixed output to <path> (48 kHz stereo)`,
`audio: audio=0: no audio device (the driver still runs)` in the [window
build](ARCHITECTURE.md#terms), and in the [headless build](ARCHITECTURE.md#terms)
`audio: headless build: the driver runs, the output is only dumped` (or `discarded`). Shutdown runs
at exit on the desktop and through `ico_host_at_shutdown` on Android and iOS, and logs the chunk
statistics and the dump's length, peak and first sound (`ico_audio_host_shutdown`).

`ico_audio_host_vsync(hz)` renders `ico_audio_host_frames(hz, n)` frames: 960 every vsync at 50
Hz; at 59.94 Hz 800.8 on average, spread exactly over every five vsyncs as 800 801 801 801 801
(48000 x 1001 / 60000 per vsync, rounded by the running total) (`port/audio/audio_host.c`
(`ico_audio_host_frames`)). The count depends on the rate and the vsync count alone.

## The output

`port/audio/out_sdl.c` plays the blocks on an SDL3 device in the window build
(`port/audio/out_sdl.c`, header comment):

- One `SDL_AudioStream` at the SPU2's own format, 48 kHz stereo S16, so SDL converts only if the
  device needs it, with a device buffer of 480 frames (10 ms,
  `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES`). The simulation thread pushes one vsync of frames per
  vsync; nothing else is shared with SDL's audio thread.
- The stream keeps a cushion of queued audio. It starts at two vsyncs (two pushed blocks: 33 ms
  at 59.94 Hz, 40 ms at 50 Hz), and the open and every resume prime the stream with the current
  cushion of silence (`cushion_frames`, `open_stream`, `ico_audio_sdl_pause`). When the queue is
  below a quarter of a vsync before a push (a stall: the simulation fell behind real time by more
  than the cushion) the cushion doubles, up to 120 ms (`CUSHION_MAX_FRAMES`), and silence fills
  the queue to it ahead of the block; the first eight such refills each log `audio: low queue <n>
  frames, <ms> ms between pushes; refilled to a <ms> ms cushion`. After 20 s of pushed audio
  without a dry queue (`CUSHION_QUIET_FRAMES`) the cushion halves, down to two vsyncs again, and
  the trim below plays the surplus out. With more than the cushion plus four vsyncs queued (the
  simulation ran ahead; never more than 250 ms, `QUEUE_CEILING_FRAMES`) the block is dropped
  (`queue_limit_frames`). The close logs the cushion and the counters: `audio: SDL output closed
  (cushion <ms> ms, largest <ms> ms; <n> refills after a dry queue, <n> blocks dropped)`
  (`ico_audio_sdl_push`, `ico_audio_sdl_close`).
- The window reads the queue once per vsync it paces (`ico_audio_sdl_active`,
  `ico_audio_sdl_queued_ms`, `ico_audio_sdl_cushion_ms`): with less than half the cushion queued
  the audio is about to run dry, one of the two conditions under which the timer path leaves a
  late new frame unpresented ([DISPLAY.md](DISPLAY.md#pacing); `port/platform/window_host.c`
  (`pace`)).
- Under the window's [vsync lock](ARCHITECTURE.md#terms) the display is the simulation's clock, so
  the simulation runs a little off 59.94 Hz. The window measures that rate every 10 s and hands the
  ratio to `ico_audio_set_rate_ratio` (`port/platform/window_host.c` (`pace_log`); the nominal
ratio at lock start in `pace_locked`), and the stream plays at it
  (`SDL_SetAudioStreamFrequencyRatio`); ratios outside 0.5..2 are taken as 1. While more than the
  cushion waits before a push, the stream plays `ICO_AUDIO_TRIM` (0.25 %) faster on top, which drains
  latency the 10 s measurement left behind without an audible pitch change (`port/audio/volume.c`
  (`ico_audio_play_ratio`); `port/audio/audio_host.h` (`ICO_AUDIO_TRIM`)).
- The device is the one `[audio] device` names, or SDL's default (which follows the system's)
  when the name is empty or no device has it (`audio: no output device called "..."; using the
  default`). A name cut to the Settings list's buffer matches a device whose name starts with it
  (`find_device`). `ico_audio_sdl_reopen` moves the stream to another device for the Settings
  menu; when the chosen device is removed (`SDL_EVENT_AUDIO_DEVICE_REMOVED`, delivered by
  `port/platform/window_host.c`) the stream moves to the default and the setting stays
  (`ico_audio_sdl_device_removed`).
- `ico_audio_sdl_pause` stops the device while the app is in the background (Android) and clears
  the queue on resume, so no stale audio plays; the resume then primes the current cushion.

Without SDL output (the headless build) the device functions are stubs: no devices, reopen fails,
no stream (`ico_audio_sdl_active` 0, `ico_audio_sdl_queued_ms` -1) (`port/audio/audio_host.c`).

## The WAV dump

`port/audio/wav.c` writes 16-bit stereo RIFF/WAVE at 48 kHz. The header's sizes are patched every
second of audio and at the close, so a run that is killed still leaves a playable file
(`port/audio/wav.c`, header comment). The dump takes the blocks after the mirror swap and before
the output volume (`port/audio/audio_host.c` (`ico_audio_host_vsync`)).

## The sequencer

`sce/libsndn2/sound.c` is the EE side: the `_Sg` family and the `Sg` entry points of Sony's
`libsndn2.a(sound.o)`, reconstructed in this repository's `sce/` tree and compiled as game code (the
game's warning set and `ICO_HOST`) inside `ico_sndn2` (`port/audio/CMakeLists.txt`). Its host seams,
under `ICO_HOST`, are listed in its head comment (`sce/libsndn2/sound.c`, "Host seams for the PC
port"):

- The EE kept addresses in 32-bit words of its context blocks and of the loaded `.hd` header. On
  the host the head context (the five pointers the event decoder shares) is an array of pointers;
  the vab context's header word and the sequence context's body word hold a nonzero token, with
  the address in `sgVabHd` / `sgSeqBody` at the same index, so a `memset` of a context still reads
  as NULL where the EE saw 0; the five header slots `SgVabOpenFakeBody` relocates are kept in
  `sgHdRel`, a record per open vab, and never written into the header.
- The IOP-to-EE page is read through `ICO_UNCACHED`, the identity on the host.
- The DMA-status wait yields the fiber a vsync at a time; the reply that ends it arrives with the
  sound thread's next tick.
- `SgSndn2RemoteInit` registers the host server before binding to it.
- `_SgSeqSeVolume` tags the voice as music or effects for the gains; the packets are the EE's.

## Tests

| test | checks |
| --- | --- |
| `adpcm` (`adpcm_test.c`) | ADPCM block decoding against vectors worked by hand from the format's formula |
| `spu2` (`spu2_test.c`) | the SPU2 and the libsd front end: the Gaussian table, ADSR lengths against psx-spx's closed forms, a sine at several pitches, loop flags and ENDX, mixing, sweeps, core 0 into core 1, the AutoDMA input, noise, timed writes, transfers and IRQ, the reverb impulse, a determinism checksum |
| `spu2_render_crc` (`spu2_bench --check`) | five 10 s scenes against golden CRCs of everything observable from the frame-by-frame renderer, and 24 random 2 s scenes rendered both chunked and frame by frame |
| `volume` (`volume_test.c`) | the output volume's mapping, clamp and integer scaling; the trim's play ratio against a target |
| `audio_queue` (`audio_queue_test.c`, window build) | `out_sdl.c` on a real SDL stream without a device: the first cushion of two vsyncs, the refill and doubling after a dry queue up to 120 ms, the resume's prime, the halving after 20 s, the trim and drop limit following the cushion, the queued-ms query |
| `mix_gain` (`mix_gain_test.c`) | the gains: identity at 100 %, scaling, sweep words, the stream categories, the cache and its re-issue |
| `adpcm_slots` (`adpcm_slots_test.c`) | the game's `adpcm_init.c` with the driver stubbed: a stream already fading out gives its slot up at once when both stream slots are taken; the ending's six streams opened one over the other alternate the two slots and the two voice pairs, each open's line names its voices and hands the log watch its length |
| `adpcm_stream` (`adpcm_stream_test.c`) | the game's `adpcm_init.c` on the real sequencer, driver and SPU2 with synthesized streams: a stream reopened on the voices a fade closed lands VOL 0x3FFF and is heard at its level; the ending's six-stream shape, each stream's level alone against its amplitude; stream 43's length (57.8 s) at 1x with 7 IOP ring wraps, no 10 ms of silence and the close at the file's length (about 2,900 vsyncs, about 2 s); the stream log lines |
| `sndn2` (`sndn2_test.c`) | the driver and the sequencer against the IRX's code: transport and reply pages, packets as register effects, transfers, the pitch arithmetic and table (the disc's when `ICO_DISC_IMAGE` is there), an ADPCM stream through the half-fill scheme, the PCM mixer, a sound effect through the sequencer's pointer seams, a stream opened on a full packet page (AU-7) |
| `libsd_irx` (`libsd_irx_test.c`) | the LIBSD.IRX reader on a synthetic module, the structural checks, the fallback without a disc |
| `audio_pan` (`pan_test.c`) | mirror mode's swap and the per-vsync frame counts at 50 and 59.94 Hz |

`spu2_bench` without arguments times `spu2_render` per vsync for an idle SPU2 and 24 and 48
voices with reverb; `--print` and `--trace-crc` print CRCs to compare builds. `spu2_prof` is the
same harness over an SPU2 built with `SPU2_PROFILE`, printing per-stage times
(`port/audio/test/spu2_bench.c`, header comment). All are registered in
`port/audio/CMakeLists.txt`.

A deliberate change to what the SPU2 renders changes the golden CRCs: `spu2_bench --print` prints
the named scenes' frame-by-frame CRCs, which replace the golden table `scene_golden` in
`port/audio/test/spu2_bench.c` (`port/audio/test/spu2_bench.c`, header comment).

Run the set with
`ctest --preset linux-x64 -R '^(adpcm|spu2|spu2_render_crc|volume|mix_gain|adpcm_slots|adpcm_stream|sndn2|libsd_irx|audio_pan)$'`
([TESTING.md](TESTING.md) has the presets and labels).
