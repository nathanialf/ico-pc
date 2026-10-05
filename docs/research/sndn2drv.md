# SNDN2DRV.IRX packet semantics

A research note written before the audio host was built, kept as the
reference behind `port/audio/` (docs/port/AUDIO.md); it is not updated as
the code changes. The goal it served is a host replacement for the
IOP side of `_SgSndn2Remote` that does what the retail IRX does with every
packet the EE sequencer (`sce/libsndn2/sound.c`) sends.

Everything below was derived from the disassembly of the user's own
`SNDN2DRV.IRX`. No bytes of the module, and no table out of it, are in this
repository. Statements marked **(inference)** are not read directly off an
instruction.

## What was examined

| input | where | notes |
|---|---|---|
| `SNDN2DRV.IRX` | `/SNDN2DRV.IRX;1` in `baserom/Ico_PAL.iso` (SCES-50760), 20,941 bytes, SHA-1 `a8b01a0e88a05b96133bc358f4cb722dd9d5ff58` | the module the game loads (`seki/src/FileManager.c:69`, `cdrom0:\SNDN2DRV.IRX;1`) |
| `LIBSD.IRX` | `/LIBSD.IRX;1` in the same ISO, 26,285 bytes, SHA-1 `49bbaf50d622b04c02dd645fde1b33972b16567f` | identical to `/primary/dev/ico/assets/disc/libsd.irx`; only its export ordinals were needed |
| a second `sndn2drv.irx` | `/primary/dev/ico/assets/disc/sndn2drv.irx`, 20,925 bytes | **not the PAL disc's module.** Same code except the DMA-done callback (see "Mailbox"), which writes the transfer counter only into the current reply page. Its origin is unknown; use the ISO copy |
| EE side | `sce/libsndn2/sound.c` (every `_SgSetPkAdd` site, `_SgDmaCommon`, `_SgInit`, `_SgCalledTickProc`, the mailbox readers), `sce/libsndn2/sound.h` | |
| game callers | `ico2/fumi/sound/s_init.c:119-176, 465-490`, `ico2/fumi/sound/adpcm_init.c:120-170`, `ico2/fumi/include/adpcm_init.h:32-39`, `ico2/ito/mpeg/mv_audiodec.c:30-80`, `ico2/fumi/sound/soundManager.c:12-36`, `ico2/common/src/main.c:248-295` | |

Method: `mips-linux-gnu-objdump -d -r -m mips:3000` over the ELF (sections are
linked at 0, so text addresses below are module-relative; `.text` is at file
offset 0xA0, `.data` at file offset 0x38F0 for address 0x3850). The IOP import
stubs (`0x41E00000` magic, library name, then `jr ra; addiu zero,zero,N`
pairs at 0x3580-0x36F8) were decoded by a small script and each `jal` to a stub
annotated with the public ps2sdk name for that ordinal. The module is compiled
without optimisation (frame pointer in `s8`, every local on the stack), so the
logic reads directly.

### Public references used (behaviour and ordinals only, no code copied)

- libsd export ordinals: ps2sdk `iop/sound/libsd/src/exports.tab`,
  https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/libsd/src/exports.tab,
  and the `I_sceSd*` import list in
  https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/libsd/include/libsd.h
- libsd register/parameter encodings (`SD_VPARAM_*`, `SD_PARAM_*`,
  `SD_SWITCH_*`, `SD_ADDR_*`, `SD_CORE_*`, `sceSdEffectAttr`):
  https://github.com/ps2dev/ps2sdk/blob/master/common/include/libsd-common.h
- libsd behaviour (what `sceSdInit` resets, what `sceSdSetEffectAttr` does with
  mode/depth/delay/feedback): ps2sdk's clean-room libsd,
  https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/libsd/src/freesd.c and
  https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/libsd/src/effect.c
  (this is a reimplementation, not Sony's module; where the port depends on a
  detail, check it against the disc's `LIBSD.IRX`)
- other imports: ps2sdk `iop/system/{intrman,sifcmd,sifman,sysclib}/include/*.h`
  and `iop/kernel/include/thbase.h` (`DECLARE_IMPORT` ordinals)
- SPU2 `MMIX` bit meanings: PCSX2 `pcsx2/SPU2/spu2sys.cpp` (the `REG_S_MMIX`
  write, lines ~1151-1162 at the time of writing) and `pcsx2/SPU2/Mixer.cpp`
  `MixCore`, https://github.com/PCSX2/pcsx2/tree/master/pcsx2/SPU2 . GPL: read
  for behaviour, nothing copied.
- SPU ADPCM block flags, reverb presets, ADSR: Martin Korth's psx-spx,
  https://psx-spx.consoledev.net/soundprocessingunitspu/ (PS1 SPU; the SPU2
  core is the same voice/ADPCM/reverb design, per the same document's PS2
  notes and PCSX2).

## Module structure

| text addr | what |
|---|---|
| 0x0000 | `_start`: `CpuEnableIntr`, `sceSifInit` if needed, `sceSifInitRpc`; thread priority from `argv[1]` (`strtol`, default 32), `CreateThread` (attr 0x02000000, stack 0x2000), `StartThread` |
| 0x0110 | RPC thread: `sceSifSetRpcQueue`, `sceSifRegisterRpc(sid 0x736E646E "sndn", handler 0x18C, receive buffer 0x3D40)`, `sceSifRpcLoop` |
| 0x018C | RPC handler `(fn, buf, size)` |
| 0x0254 | tick: dispatch every 16-byte packet in the buffer, then fill the reply page |
| 0x0508 | `sceSdSetTransCallback(0, …)` target: DMA channel 0 done |
| 0x0558 | `sceSdSetTransCallback(1, …)` target: core 1 AutoDMA half done (PCM mixer) |
| 0x060C | packet dispatcher: `switch (p[0])`, jump table at `.rodata` 0x3710, 79 entries for commands 1..0x4F |
| 0x248C | cancel queued stream events for one voice |
| 0x2778 | ADPCM stream refill scheduler (runs every tick) |
| 0x2A54 | stream event queue runner (runs every tick) |
| 0x3334 | enqueue a stream event |
| 0x3480 | PCM stream mixer (16 channels into the AutoDMA half) |
| `.data` 0x3860 | the pitch table, 608 `u16` (see command 4) |

Imports (stub address: library ordinal, ps2sdk name): libsd 4 `sceSdInit`,
5 `sceSdSetParam`, 6 `sceSdGetParam`, 7 `sceSdSetSwitch`, 9 `sceSdSetAddr`,
10 `sceSdGetAddr`, 11 `sceSdSetCoreAttr`, 17 `sceSdVoiceTrans`,
18 `sceSdBlockTrans`, 19 `sceSdVoiceTransStatus`, 20 `sceSdBlockTransStatus`,
21 `sceSdSetTransCallback`, 23 `sceSdSetEffectAttr`; intrman 6 `EnableIntr`,
9 `CpuEnableIntr`; sifcmd 14 `sceSifInitRpc`, 17 `sceSifRegisterRpc`,
19 `sceSifSetRpcQueue`, 22 `sceSifRpcLoop`; sifman 5 `sceSifInit`,
29 `sceSifCheckInit`; sysclib 12 `memcpy`, 14 `memset`, 36 `strtol`; thbase 4
`CreateThread`, 6 `StartThread`, 20 `GetThreadId`. Not imported:
`sceSdProcBatch`, `sceSdSetCoreAttr` for anything but noise clock / SPDIF /
effect enable, `sceSifSetDma`: everything the EE sees comes back as the RPC
reply.

IOP `.bss` (module-relative) the port has to model:

| addr | size | contents |
|---|---|---|
| 0x3D20 | 4 | reply page counter (page = counter & 1) |
| 0x3D24 / 0x3D28 | 4 + 4 | stream event queue write / read index (ring of 128) |
| 0x3D2C | 4 | transfer counter of the most recent 0x20/0x21 command |
| 0x3D30 | 4 | pending stream fill: `(core<<8 | voice) + 0x10000`, 0 when none |
| 0x3D40 | 0x1000 | RPC receive buffer (256 packets) |
| 0x4D40 | 2 × 0x200 | the two reply (mailbox) pages |
| 0x5140 | 0x2000 (0x800 used by PCM) | staging buffer, **shared** by ADPCM fills and the PCM AutoDMA ring |
| 0x7140 | 4 | return word of a non-tick RPC |
| 0x7148 | 48 × 36 | ADPCM stream voice records, index `core*24 + voice` |
| 0x7808 | 128 × 32 | stream event queue |
| 0x8808 | 16 × 32 | PCM stream channel records |

## Transport

- Bind: EE `sceSifBindRpc(&sgClient, 0x736E646E, 0)` (`sound.c:1909`).
- **RPC 0x65 (init)**, synchronous (`sound.c:1872`): 0x40-byte buffer whose
  first 16 bytes are one packet, `{0x1E, hot, …}`. The handler runs the
  dispatcher once on it and returns a pointer to 0x7140. The EE ignores the
  reply. This is the only way command 0x1E arrives.
- **RPC 0x64 (tick)**, `SIF_RPC_M_NOWAIT` (`sound.c:239`): send = the packet
  page the EE filled this tick (`count*16` bytes, at most 255 packets,
  `sound.c:256`), receive = 0x200 bytes into `sgIop2EeBuf` through its
  uncached alias (`sound.c:1868`). The handler:
  1. dispatches each 16-byte packet in order (0x254);
  2. runs the ADPCM refill scheduler (0x2778) and the stream event queue
     (0x2A54);
  3. increments the page counter and fills page `counter & 1` (below);
  4. returns that page as the reply.
- The EE sound thread is woken once per vsync by `scheduler()`
  (`common/src/main.c:292`), waits for the previous RPC to finish
  (`SgSndn2RemoteSync`, `soundManager.c:23`), then runs `SgCalledTickProc`,
  which ends in the 0x64 call. So there is exactly one IOP tick per EE vsync
  (50 Hz on PAL), and the mailbox the EE reads is the reply of the previous
  tick.

Packet: four little-endian 32-bit words, `{cmd, id, w2, w3}` (`_SgSetPkAdd`,
`sound.c:249-264`). Below, "slot" is a voice number 0..47 with
`core = slot / 24`, `voice = slot % 24`, and `V(x)` means the libsd voice
selector `core | voice << 1 | x`.

## Command table

Every command the EE emits, with the IRX handler and its libsd calls. "Sites"
are `sound.c` lines.

### Voices (sequencer, every tick)

| cmd | EE sites | packet | IRX (handler) |
|---|---|---|---|
| 0x01 | 1158 (`_SgSeqSeVolume`) | id = slot, w2 = left, w3 = right (16-bit SPU volume words) | `sceSdSetParam(V(SD_VPARAM_VOLL), w2 & 0xFFFF)`, `sceSdSetParam(V(SD_VPARAM_VOLR), w3 & 0xFFFF)` (0x680) |
| 0x02 | 362, 496, 1669, 2349, 2532, 2642 | id = slot, w2 = ADSR1, w3 = ADSR2 (`0, 0` to silence a stolen voice) | `SetParam(V(ADSR1), w2)`, `SetParam(V(ADSR2), w3)` (0x7D0) |
| 0x03 | 361, 495 | id = slot, w2 = SPU byte address of the sample: `(vab_base + offset) << shift` | `sceSdSetAddr(V(SD_VADDR_SSA) | 0x40, w2)` (0x928). The 0x40 is the voice-address flag of Sony's encoding; ps2sdk spells SSA as 0x2000 without it **(inference: same register)** |
| 0x04 | 1120 (`_SgPitchTableVag`) | id = slot; w2 = `base<<24 | note<<16 | (fine & 0xFF)<<8 | bend`; w3 = `range<<24 | scale` (scale 0x1000 = 1.0; `_SgSeMain` passes the sequence's pitch word, `_SgBgmMain` 0x1000) | computes a pitch and `SetParam(V(SD_VPARAM_PITCH), p & 0xFFFF)` (0x9D8), see "Pitch" |
| 0x0A | 230 (and 1947 via 0x0B) | id 0; w2 = key-on mask voices 0..23 of core 0; w3 = core 1 | `sceSdSetSwitch(SD_SWITCH_KON|0, w2)`, `(KON|1, w3)` (0xE9C) |
| 0x0B | 235, 1947 | as 0x0A, key-off | `SetSwitch(SD_SWITCH_KOFF|core, …)` (0xECC). `SgQuit` sends `{0xB, i, 0xFFFFFF, 0}` for i = 0, 1: both go to core 0's mask in w2 = all, core 1 gets 0 **(the EE's id is ignored)** |
| 0x0C | 220 | as 0x0A, the reverb (wet) send mask | `SetSwitch(SD_SWITCH_VMIXEL|0, w2)`, `(VMIXER|0, w2)`, `(VMIXEL|1, w3)`, `(VMIXER|1, w3)` (0xEFC) |
| 0x0D | 225 | as 0x0A, the noise mask | `SetSwitch(SD_SWITCH_NON|core, …)` (0xF54) |
| 0x32 / id 8 | 373 | w2 = slot, w3 = noise clock (the SE record's byte 2) | `sceSdSetCoreAttr((w2/24) | SD_CORE_NOISE_CLK, w3 & 0xFFFF)` (0xD84). The noise clock is per core, so the last noise voice keyed on a core wins |

Ordering within one tick, as `_SgCalledTickProc` writes it: the per-voice
packets (1, 2, 3, 4) of every event, then 0x0C, 0x0D, 0x0A, 0x0B
(`sound.c:219-238`). The IRX applies them in that order, so a key-on in the
same tick as its parameters always sees them.

### Core, reverb, output

| cmd | EE | packet | IRX |
|---|---|---|---|
| 0x14 | `SgSetReverbEndAddr` 2154 | id = core, w2 = byte address | `sceSdSetAddr(core | SD_ADDR_EEA, w2)` (0xF84) |
| 0x15 | `SgSetReverbType` 2159 | id = core, w2 = mode | builds a stack `sceSdEffectAttr` with `mode = w2 & ~0x100` (the `SD_EFFECT_MODE_CLEAR` bit is always dropped, so the work area is never cleared by this command) and `depth_L = depth_R = 0`; `sceSdSetEffectAttr(core, &attr)`; then `sceSdSetCoreAttr(core | SD_CORE_EFFECT_ENABLE, 1)` (0xFB8) |
| 0x16 | `SgSetReverbDepth` 2164 | id = core, w2 = left, w3 = right | `SetParam(core | SD_PARAM_EVOLL, w2)`, `(core | SD_PARAM_EVOLR, w3)` (0x1018) |
| 0x17 | `SgSetReverbDelaytime` 2169 | id = core, w2 = delay | `attr.delay = w2`, `sceSdSetEffectAttr(core, &attr)` twice (0x1078) |
| 0x18 | `SgSetReverbFeedback` 2174 | id = core, w2 = feedback | `attr.feedback = w2`, `sceSdSetEffectAttr` twice (0x10BC) |
| 0x28 | `SgSetMasterVol` 2225, `SgQuit` 1948 | id = core, w2 = left, w3 = right | `SetParam(core | SD_PARAM_MVOLL, w2)`, `(core | SD_PARAM_MVOLR, w3)` (0xE3C) |
| 0x32 / id 10 | `SgSetDigitalOutputMode` 1963 | w2 = mode | `sceSdSetCoreAttr(SD_CORE_SPDIF_MODE, w2 & 0xFFFF)` (0xE0C). The game sends 0x80 for media type 1 (CD) and 0x880 otherwise (`s_init.c:126-130`) |
| 0x1E | `_SgInit` via RPC 0x65 | id = hot (0 `SgInit`, 1 `SgInitHot`) | `sceSdInit(hot)`; `CpuEnableIntr`; `EnableIntr(36)`, `(40)`, `(9)`; `SetSwitch(VMIXEL|0, 0)`, `(VMIXER|0, 0)`, `(VMIXEL|1, 0)`, `(VMIXER|1, 0)`; `sceSdSetTransCallback(0, 0x508)` (0x1100) |
| 0x1F | `SgQuit` 1950 | – | `sceSdSetTransCallback(0, NULL)` (0x117C) |

Notes:

- **Output mode (mono/stereo) is not an IOP command.** `SgSetOutputMode`
  stores a flag at common-context +0x38 (`sound.c:2177`) and
  `_SgSeqSeVolume` folds L/R to the larger magnitude on the EE
  (`sound.c:1144-1152`). "0x28" is master volume, "0x32" is a two-way
  sub-command (noise clock, SPDIF mode).
- `sceSdEffectAttr` is a local of the dispatcher's frame (s8+48..67). Command
  0x15 writes `mode`, `depth_L`, `depth_R`; 0x17 writes only `delay`; 0x18 only
  `feedback`; `core` is never written. Each `sceSdSetEffectAttr` call
  therefore uses whatever an earlier command left in the other fields of that
  stack slot. The game never sends 0x17 or 0x18 (grep of `ico2/`), so the
  host can implement them as "re-apply the last 0x15 attr with this
  delay/feedback" and log it.
- `sceSdSetEffectAttr` with depth 0 sets EVOL to 0 (ps2sdk `effect.c`
  behaviour, **to confirm against the disc's libsd**); the game follows type
  with depth. `soundInit` (`s_init.c:133-140`) sets: EEA core 0 = 0x1FFFFF,
  core 1 = 0x1DFFFF; type 4 (`SD_EFFECT_MODE_STUDIO_3`) on both; depth
  0xCCC/0xCCC on **core 0 twice** and never on core 1 (a game-side slip;
  core 1's EVOL stays 0 until `soundReverbDepthSet` sets both,
  `s_init.c:166-175`); master volume 0 on both until `soundReverbDepthSet`
  sets 0x3FFF.
- Interrupt lines 36, 40, 9 are IOP DMA 4 (SPU2 core 0), DMA 7 (core 1) and
  the SPU2 IRQ (ps2sdk `IOP_IRQ_DMA_SPU`, `IOP_IRQ_DMA_SPU2`, `IOP_IRQ_SPU`,
  as `freesd.c:sceSdInit` enables them). Host: nothing to do.

### Sample upload (`SgDmaWrite` / `SgDmaRead`)

`_SgDmaCommon` (`sound.c:1978-1991`) increments the EE transfer counter at
common-context +0x48 and packs:

```
w1 = counter << 8 | (iop >> 16) & 0xFF
w2 = iop << 16    | (spu >> 8) & 0xFFFF
w3 = spu << 24    | size & 0xFFFFFF
```

| cmd | IRX |
|---|---|
| 0x20 write | unpack `iop = (w1 & 0xFF) << 16 | w2 >> 16`, `spu = (w2 & 0xFFFF) << 8 | w3 >> 24`, `size = w3 & 0xFFFFFF`; store `w1 >> 8` at 0x3D2C; `sceSdVoiceTrans(0, SD_TRANS_WRITE | SD_TRANS_MODE_DMA, iop, spu, size)` (0x1190) |
| 0x21 read | same with `SD_TRANS_READ` (0x1268) |
| 0x22 | `sceSdVoiceTransStatus(0, (short)id)`, result into the RPC return word; **never sent by the EE** (0x1340) |

The return value of `sceSdVoiceTrans` is not checked: a second 0x20 in the
same tick while channel 0 is busy is dropped by libsd **(inference from
libsd's busy check; the game avoids it, below)**. The IOP address is an IOP
heap block the EE filled by SIF DMA (`Ee2Iop`, `s_init.c:100-117`); the game
writes in chunks of at most 0x78000 and pads tiny ones to 0x50 bytes
(`s_init.c:465-490`), waiting with `SgGetDmaTransferStatus(1)` before reusing
the IOP buffer.

### ADPCM streams (0x3C-0x43)

EE request (`AdpcmChReq`, `ico2/fumi/include/adpcm_init.h:32-39`) and its
packing in `SgStAdpcmOpen` (`sound.c:2677-2708`):

```
w1 = ch << 24 | attr & 0xFF0000 | f14 & 0xFF00 | (spuAddr >> 16) & 0xFF
w2 = spuAddr << 16 | (iopSize >> 8) & 0xFFFF
w3 = iopSize << 24 | iopAddr & 0xFFFFFF
```

The IRX reads the field at request +0x14 (bits 8..15 only) as the **SPU
buffer size** (it halves it for double buffering). The decomp's derived name
`vol` for that field is wrong; the game passes 0x4000 (16 KB SPU ring,
`adpcm_init.c:150`). Only the attr byte `attr >> 16` survives: the number of
interleaved channels in the IOP buffer (1, 2 or 4); the low `| 2` the game
ORs in is dropped.

Stream voice record (36 bytes at 0x7148 + 36 × slot):

| off | field |
|---|---|
| +0x00 | active (bit 0 set by the key-on event) |
| +0x04 | channels in the IOP interleave (1/2/4) |
| +0x08 | SPU ring start |
| +0x0C | SPU ring size |
| +0x10 | IOP ring start for this channel (the EE adds `(0x800/n)*j`) |
| +0x14 | IOP ring size |
| +0x18 | IOP read offset, reported to the EE |
| +0x1C | which half NAX is in now (1 or 2) |
| +0x20 | half last scheduled |

| cmd | EE | packet | IRX |
|---|---|---|---|
| 0x3C | `SgStAdpcmInit` | – | `memset` the 48 stream records, the 128-entry event queue **and the 16 PCM channel records** (0x190C) |
| 0x3D | `SgStAdpcmQuit` | – | nothing (0x1950) |
| 0x3E | `SgStAdpcmOpen` | above | fill the record for slot `w1 >> 24`; `sceSdSetAddr(V(SD_VADDR_SSA)|0x40, spuAddr)`; `SetParam(V(ADSR1), 0x8080)`, `SetParam(V(ADSR2), 0x808A)` (0x1958) |
| 0x3F | `SgStAdpcmClose` | id = slot | cancel the slot's queued events (0x248C) and clear its record (0x1CD0) |
| 0x40 | `SgStAdpcmChannelVolume` | id = slot mask core 0 (24 bits), w2 = core 1, w3 = `left << 16 | right` | for every set bit: `SetParam(V(VOLL), w3 >> 16)`, `SetParam(V(VOLR), w3 & 0xFFFF)` (0x1DB4) |
| 0x41 | `SgStAdpcmChannelPitch` | masks as 0x40, w3 = sample rate in Hz (0..192000) | for every set bit: `SetParam(V(PITCH), (w3 << 12) / 48000)` (signed divide by multiply-high, 0x1EEC) |
| 0x42 | `SgStAdpcmPlay` | masks | for every set bit: enqueue `FILL(slot, half=2, n, src = iopStart + readOff, dst = spuStart, len = spuSize/2)`; then enqueue `KEYON(mask0, mask1)` (0x2008) |
| 0x43 | `SgStAdpcmStop` | masks | for every set bit: cancel queued events, clear active, read offset, half state; then `SetSwitch(KOFF|0, w1)`, `(KOFF|1, w2)` (0x2250) |

Every tick (0x2778): for each active record, `nax = sceSdGetAddr(V(SD_VADDR_NAX)|0x40)`.
If `start < nax < start + size/2` the voice is in half 1 and the next fill
targets `start + size/2`; if `start + size/2 < nax < start + size` it is in
half 2 and the fill targets `start`. (NAX exactly on a boundary leaves the
state as it was.) When the half differs from the last scheduled one, enqueue
`FILL(slot, half, n, iopStart + readOff, target, size/2)`.

Event queue runner (0x2A54), once per tick, only while
`sceSdVoiceTransStatus(1, 0) == 1` (DMA channel 1 idle):

1. If a fill is pending (0x3D30): `readOff = (readOff + (size/2) * n) % iopSize`
   for that record, clear pending.
2. Take the next event (read index & 0x7F):
   - **FILL**: de-interleave `len / (0x800/n)` chunks of `0x800/n` bytes from
     `src`, `src + 0x800`, … into the staging buffer at 0x5140; set the ADPCM
     flag byte (byte 1 of a 16-byte block) of the first and last block: half 1
     → first 2, last 3; half 2 → first 6, last 2 (bit 0 end, bit 1 repeat,
     bit 2 loop start, per psx-spx), so the voice loops over the whole SPU
     ring and LSA is re-latched at its start; then
     `sceSdVoiceTrans(1, SD_TRANS_WRITE | SD_TRANS_MODE_DMA, 0x5140, dst, len)`;
     mark pending; **stop** for this tick.
   - **KEYON**: `SetSwitch(KON|0, mask0)`, `(KON|1, mask1)`; for each voice in
     the masks set active and clear both half fields; continue with the next
     event.
   - a cancelled (zeroed) entry: skip, continue.
3. Free the entry and advance the read index.

Consequences the host must reproduce: one stream DMA per tick at most; a
stereo `Play` keys on no earlier than the third tick after the packet (fill
ch 0, fill ch 1, then key-on), and the read offset the EE polls lags the DMA
by one tick.

### PCM streams (0x46-0x4F, FMV audio)

PCM channel record (32 bytes at 0x8808 + 32 × ch, ch 0..15):

| off | field |
|---|---|
| +0x00 | stop offset (0x200000 = none) |
| +0x04 | IOP buffer |
| +0x08 | IOP buffer size (wrap point) |
| +0x0C | read offset, reported to the EE |
| +0x10 | low 16 bits: bytes to advance per mix callback; high 16 bits: playing, and the sample-step shift |
| +0x14 / +0x18 | volume L / R (0..0x7FFF) |
| +0x1C | the play/shift bits `id & 0xFF0000` of the open, which 0x4B ORs into +0x10 and 0x4C clears from it |

| cmd | EE | packet | IRX |
|---|---|---|---|
| 0x46 | `SgStPcmInit` | – | clear the 0x2000 staging buffer and the 16 records; `sceSdBlockTrans(1, SD_TRANS_LOOP | SD_TRANS_WRITE, 0x5140, 0x800)` (core 1 AutoDMA over a 2 × 0x400 ring); `sceSdSetTransCallback(1, 0x558)`; `SetParam(1 | SD_PARAM_BVOLL, 0x7FFF)`, `(1 | SD_PARAM_BVOLR, 0x7FFF)` (0x1360) |
| 0x47 | `SgStPcmQuit` | – | `sceSdBlockTrans(1, SD_TRANS_STOP, 0, 0)`; clear callback 1; BVOLL/R of core 1 = 0 (0x13D0) |
| 0x48 | `SgStPcmOpen` | id = `ch << 24 | flags` (game: 0x10400), w2 = IOP buffer, w3 = size | stop offset = 0x200000, +0x10 = `flags & 0xFFFF`, +0x1C = `flags & 0xFF0000`, +0x04, +0x08 (0x1410). The read offset is **not** reset |
| 0x49 | `SgStPcmClose` | id = ch | clear the record (0x1500) |
| 0x4A | `SgStPcmVolume` | id = ch mask (16 bits), w2 = L, w3 = R | set +0x14/+0x18 for each channel in the mask (0x178C) |
| 0x4B | `SgStPcmPlay` | id = mask | `+0x10 |= +0x1C` (0x15B8) |
| 0x4C | `SgStPcmStop` | id = mask | `+0x10 &= ~+0x1C` (0x1678) |
| 0x4D | `SgStPcmLseek` | id = ch, w2 = offset | read offset = w2 (0x1754) |
| 0x4E | `SgStPcmSetEffect` | id = 4 or 8 | 4: `SetParam(SD_PARAM_MMIX|0, 0xFFF0)`, `(MMIX|1, 0xFFFC)`; 8: `(MMIX|0, 0xFFC0)`, `(MMIX|1, 0xFFCC)`; others ignored (0x153C) |
| 0x4F | `SgStPcmBufMode` | id = mask, w2 = offset, w3 = mode | mode 1: stop offset = w2; else 0x200000 (0x1840) |

MMIX bits (PCSX2 naming): 0/1 wet R/L of the external input (core 0's
output, for core 1), 2/3 dry R/L of it, 4/5 wet R/L of the sound-data input
(the AutoDMA stream), 6/7 dry R/L, 8/9 wet voices, 10/11 dry voices. So
effect 4 = PCM into both dry and reverb (ps2sdk `sceSdInit` uses the same
0xFF0/0xFFC as defaults); effect 8 (what `mv_audiodec.c:65` sets) = PCM dry
only, and core 0's output dry only into core 1.

Mix callback (0x558, on each AutoDMA half): pick the half the hardware is not
playing (`sceSdBlockTransStatus(1, 0) & 0x01000000` → 0, else 0x400), zero it,
and run the mixer (0x3480) over the 16 records into it: 256 samples left at
+0, 256 right at +0x200. For each record whose `+0x10 >> 16` (the shift `s`)
is non-zero:

- if read offset == stop offset: clear the playing bits and skip;
- if read offset == size: wrap to 0;
- for i in 0..255: `x = s16 at buf + off + ((2*i) >> s) * 2`;
  `L[i] += (volL * x) >> 15`, `R[i] += (volR * x) >> 15`, **16-bit wrapping
  adds, no saturation** (`mult`, `srl 15`, `add`, `sh`);
- `off += +0x10 & 0xFFFF`.

The movie opens ch 0 at `iopBuf` and ch 1 at `iopBuf + 0x200`, both with
advance 0x400 and shift 1 (`mv_audiodec.c:48-62`): the IOP buffer holds the
SPU2 stereo interleave (0x200 bytes left, 0x200 bytes right) and each channel
reads one side at 48 kHz. Shift 2 would play each sample twice (24 kHz
source). Which output side each channel reaches is set only by its L/R
volumes.

## Mailbox (the 0x200-byte RPC reply)

Filled after the packets and stream work of each tick, into page
`counter & 1`:

| EE offset | contents | IRX source | EE reader |
|---|---|---|---|
| 0x000-0x05F | core 0 voices 0..23: `sceSdGetParam(V(SD_VPARAM_ENVX)) & 0x7FFF` | 0x2E0 loop | `_SgSeqSeRrEnd` (`sound.c:1818`): a slot whose ENVX < 2, not reserved, aged ≥ 2 ticks, is freed |
| 0x060-0x0BF | core 1 voices 0..23, same | | |
| 0x0C0-0x11F | core 0 stream records: IOP read offset (+0x18) | | `SgStAdpcmIopReadAddr` (`sound.c:2761-2768`) |
| 0x120-0x17F | core 1 stream records, same | | |
| 0x180-0x1BF | PCM channels 0..15: read offset (+0x0C) | | `SgStPcmIopReadAddr` (`sound.c:2854-2861`) |
| 0x1C0 | transfer counter | written by the channel-0 DMA-done callback (0x508) into **both** pages: the value of 0x3D2C, i.e. the counter of the most recently issued 0x20/0x21 | `SgGetDmaTransferStatus` (`sound.c:1993-2011`) compares with common-context +0x48 |
| 0x1C4-0x1FF | never written (zero) | | |

Because the page reaches the EE only as the reply of an RPC, the EE's +0x1C0
(and every other field) updates once per tick. `SgGetDmaTransferStatus(1)`
spins on it, which terminates only because the sound thread keeps ticking
from the vsync handler; on the host the spin must yield to the scheduler
(`port/platform/sched.c`) or the spinning fiber blocks the tick that would
release it.

## Pitch (command 4)

With `base`, `note`, `fine` (signed byte), `bend`, `range`, `scale` from the
packet and `T` the 608-entry `u16` table at `.data` 0x3860:

```
if note >= base: d = note - base; idx = (d % 12) * 16
                 p = T[idx + ((bend - 64) * range >> 2) + 208 + fine] << (d / 12)
else:            d = base - note; idx = (12 - d % 12) * 16
                 p = T[idx + ((bend - 64) * range >> 2) + 208 + fine] >> (d / 12 + 1)
p = p * 441 / 480          (unsigned, by multiply-high: 44.1 kHz VAGs on a 48 kHz SPU)
p = (scale * p) >> 12
sceSdSetParam(V(PITCH), p & 0xFFFF)
```

`T` is 1/16-semitone steps with `T[208] = 0x1000` (unity); it is close to
`floor(4096 * 2^((i - 208)/192))` but that formula misses 32 of the 608
entries by one, and the table is not octave-consistent, so it is hand-made
data. The port must not commit it: read it from the user's `SNDN2DRV.IRX` at
extraction time (ISO path `/SNDN2DRV.IRX;1`, file offset 0x3900, 0x4C0
bytes) or accept the formula and log the 32 one-unit differences in
`docs/port/DIVERGENCES.md`. The index is not range-checked: a large `range`
with an extreme bend or fine reads before or after the table (into
`.data`/`.bss`); the host should clamp and log when that happens, and record
whether it ever does.

## Recommendations for the Phase 4 packages

1. `port/audio/sg/sndn2_host.c`: implement exactly the two RPC entry points.
   0x65 runs one packet; 0x64 runs the packet page, then the stream scheduler
   and event queue, then fills the reply page. Keep the double-buffered page
   and counter so ordering matches; a synchronous host call that copies the
   page into `sgIop2EeBuf` at the end of `_SgSndn2Remote` is equivalent for
   the EE because the EE never reads the reply before the next
   `SgSndn2RemoteSync`.
2. Express the driver against a libsd-shaped API in the host SPU2
   (`spu2.c`): `SetParam`/`GetParam` (VOLL/R, PITCH, ADSR1/2, ENVX, MVOL,
   EVOL, BVOL, MMIX), `SetSwitch` (KON, KOFF, NON, VMIXEL/R), `SetAddr`/`GetAddr`
   (SSA, NAX, EEA), `SetCoreAttr` (noise clock, effect enable, SPDIF is a
   no-op), `SetEffectAttr` (mode, ESA from EEA and the mode's size), voice
   transfer (a memcpy into SPU RAM, completing at the next tick at the
   earliest), and AutoDMA input on core 1 (a callback every 256 output
   samples). Nothing else is used.
3. `sceSdInit` defaults matter: VMIXL/VMIXR (dry) all on, VMIXEL/VMIXER
   cleared by 0x1E, MMIX 0xFF0/0xFFC, effect off, volumes per the libsd
   init. Take them from the disc's `LIBSD.IRX` behaviour; ps2sdk's
   `freesd.c` is a usable reference but not authoritative.
4. Reverb: SPU2 reverb presets (mode 4 = studio 3 on both cores) and work
   area sizes are needed. The PS1 preset values are published in psx-spx;
   confirm the SPU2 `sceSdSetEffectAttr` table against the disc's
   `LIBSD.IRX` (it is data in that module, so read at run time like the pitch
   table, not committed).
5. Streams: reproduce the one-DMA-per-tick queue, the ADPCM loop-flag
   patching and the shared 0x5140 staging buffer behaviour (ADPCM fills and
   the PCM ring alias; see open questions). PCM mix with 16-bit wrapping adds.
6. Timing: one tick per EE vsync; voice transfers complete "by the next
   tick"; NAX is sampled once per tick. Unit tests: pitch formula against the
   disc table, ADPCM de-interleave + flag patching on synthetic data, PCM
   mixer wrap/shift/stop-offset, mailbox layout offsets.

## Open questions

1. Sony libsd details the host must match: `sceSdInit(1)` (hot) versus
   `sceSdInit(0)`; `sceSdSetEffectAttr` with the stale stack fields; the
   effect area sizes and preset registers of mode 4; the busy behaviour of
   `sceSdVoiceTrans`. Answer by disassembling the disc's `LIBSD.IRX` (same
   method; not done in this time box).
2. Does anything run ADPCM streams while the PCM ring is active (FMV)? Both
   use the staging buffer at 0x5140: a FILL during a movie would overwrite
   the AutoDMA ring, and the PCM callback zeroes half of it under a pending
   ADPCM DMA. `soundVBlank` is skipped while `mpegPlay` is set
   (`soundManager.c:27`), which suggests no; confirm in `ito/mpeg/` and
   `fumi/sound/`.
3. 4-channel ADPCM streams: with the game's IOP ring of 0x5C000 and SPU ring
   0x4000, one refill consumes 0x8000 bytes, and 0x5C000 is not a multiple of
   it, so a refill can read 0x4000 bytes past the ring before the modulo
   wraps. Are 4-channel files used (`adpcmFile[].channels`, data-side), and
   what lies after the ring in IOP memory?
4. Pitch index range: do any VAG/sequence records drive the index outside
   0..607? Instrument the host and find out.
5. `SgSetTickMode(60)` (`s_init.c:132`) on a 50 Hz tick: the sequencer's
   tempo base is EE-side (common context +0x3A), not IRX; check whether PAL
   music runs at the tempo the data intends or 5/6 of it, and keep whatever
   the PS2 did.
6. Why `/primary/dev/ico/assets/disc/sndn2drv.irx` differs from the PAL
   disc's module (single-page counter write in the DMA callback). Possibly
   another region's disc. The PAL behaviour (both pages) is the one to
   implement.
7. Command 0x22 (`VoiceTransStatus`) and the 0x7140 return word: unused by
   this game's EE code; implement as a no-op returning 1.
