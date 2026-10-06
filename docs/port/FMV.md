# FMV playback

The PS2 movie player (`ico2/ito/mpeg/mv_*.c`) is built only for the EE
(`ICO_EE_ONLY_SOURCES` in `tools/gen_sources.py`). The host build's
`movie_init`, `movie_proc` and `movie_end` are in `port/fmv/movie.c`, with
the same signatures and return values (`ico2/ito/include/mv_main.h`), so
Main's call site (the `mpegPlay` block of `Main` in
`ico2/common/src/main.c`) is unchanged.

| file | what |
| --- | --- |
| `port/fmv/pss.c`, `pss.h` | the PSS demuxer: packs, PES, Sony's audio framing, the 40-byte audio header, MPEG video access units |
| `port/fmv/m2v.c`, `m2v.h` | the MPEG-2 decoder wrapper over Ittiam libmpeg2 |
| `port/fmv/ithread_single.c` | libmpeg2's thread layer for a single-threaded decoder (replaces its pthread `ithread.c`) |
| `port/fmv/movie_pace.c`, `movie_pace.h` | the display timing state machine (`mv_disp.c`'s vblank handler and `mv_vobuf.c`'s ring) |
| `port/fmv/movie.c` | `movie_init` / `movie_proc` / `movie_end`: file, stream, demux, decode, audio, pacing, display |
| `port/fmv/rd_video.h`, `port/render/rd_video.c`, `port/shaders/yuv.hlsl` | the picture on the output (window build) |
| `port/fmv/test/fmv_test.c`, `port/fmv/test/rd_video_test.c` | the tests (below) |

`movie.c` draws only when compiled with `ICO_RD`, which
`port/fmv/CMakeLists.txt` gives it in the window build; it stops the build
with an `#error` when neither `ICO_RD` nor `ICO_HEADLESS` is defined, so a
build that loses the define fails instead of shipping films with sound and
no picture.

## Decoder

The decoder is Ittiam libmpeg2 (AOSP `platform/external/libmpeg2`,
Apache-2.0), tag `android-16.0.0_r4`, chosen in docs/research/licences.md
("MPEG-2 decoding"). The pin, fetch and licence are in
`tools/fetch_deps.sh` and docs/port/THIRD_PARTY.md. It compiles unchanged
with gcc, mingw-w64 gcc and the sanitizer build, so nothing of FFmpeg is
needed. `port/fmv/CMakeLists.txt` builds it from the fetched source with
each preset's compiler: the generic C path only (the `riscv` function
selectors pick the generic routines; no SSE or AVX), one core, no
deinterlacing, 4:2:0 planar output into planes `m2v.c` owns, and a maximum
size of 720 x 576 (the IPU's own limit through
`sceMpegGetPicture(.., 1620)`, 1620 macroblocks). The library left-shifts
negative values (`impeg2d_vld.c`), so the sanitizer build compiles it with
`-fno-sanitize=shift`. Decoding took roughly 7 ms per 720 x 480 picture on
the development machine, well inside the 40 ms (two vsyncs at 50 Hz) a
picture is shown for.

## Streams

The PAL disc's `DATA.DF` directory holds three PSS files (read with the
directory format of `unifile_read_func` in `fumi/ios/cdvd.c`):

| file | DATA.DF offset, size | video | pictures | audio |
| --- | --- | --- | --- | --- |
| `pal_advertise.pss` | 390270976, 130301956 | 720 x 480, 4:3 (aspect code 2), 25 fps, Main@Main, frame pictures, I/P/B (234 I, 934 P, 2270 B), 234 GOPs | 3438 | PCM 48 kHz stereo, 26,413,056 bytes (137.57 s) |
| `pal_advertise576.pss` | 520574976, 130334724 | 720 x 576, 4:3, 25 fps, frame pictures | 3439 | same format |
| `advertise.pss` | 762783744, 104398852 | 720 x 480, 4:3, 29.97 fps (frame rate code 4), frame pictures | about 4100 (start-code count) | same format |

`movieFile` (the `moviefile` data member) also names
`movie/pal_demo_advertise.pss`, `movie/pal_end.pss` and
`movie/pal_badend.pss` (stages 103 to 105, from the E3 build); they are not
on the disc. `movie_init` returns -1 for a name the directory lacks, and
`movie_proc` then returns 0 at once. (The PS2's `getFileLsn` would assert;
the retail game never asks for them.)

Which stage plays which film is `stageData[].mpegNo`: 57 `memory` plays 1
(`advertise.pss`), 58 `memoryPAL` plays 2 (`pal_advertise.pss`), 59
`memoryPAL576` plays 3. The title (`actOpDemo01` in `script/src/op.c`)
plays stage 58 in 50 Hz mode and 57 in 60 Hz mode (`systemStatus[0]`) when
the title sequence runs out with no START: mode 0 ends, its 10 s timer
runs, and mode 2 calls `stgmgrForceSwitchWithFade(58, ...)` with
`mpegPlayReturnStage = 1`. The PAL player is thus given a 720 x 480 stream
in a 720 x 576 display (`main.c` passes `systemStatus[0] ? 576 : 480`).

### PSS format

- 16384-byte packs with MPEG-2 pack headers (ISO/IEC 13818-1 2.5.3), a
  system header (0xBB) in the first, padding (0xBE), and the end code
  0x000001B9 last.
- Video: PES 0xE0, MPEG-2 PES headers, PTS and DTS on picture-starting
  packets (the first PTS is 4724 at 90 kHz).
- Audio: PES 0xBD (private stream 1), PTS on each, 10 bytes of header data
  (PTS and 5 stuffing bytes). Every payload starts with 4 bytes, `FF A0 00
  00` in every packet of all three files; `0xA0 + n` is the sub-stream that
  libmpeg's `sceMpegAddStrCallback(type 2, channel n)` selects, and
  `pcmCallback` in `mv_audiodec.c` skips the 4 bytes. After them the first
  packet carries a 40-byte header that `audioDecEndPut` collects: `"SShd"`,
  then little-endian words for header size 24, type (0 PCM big-endian, 1
  PCM little-endian, 2 ADPCM, per `mv_audiodec.c`'s own debug print), rate,
  channels, interleave, interleave start and end block; then `"SSbd"` and
  the data size. All three files are type 1, 48000 Hz, 2 channels,
  interleave 512, start and end -1. The rest is PCM: 512-byte blocks
  alternating between the two channels (256 samples each).
- `pss.c` also accepts MPEG-1 pack and PES forms; `pss.h` documents the
  API.

## Timing

`movie_pace.c` reproduces the PS2 display loop (`vblankHandler` and
`handler_endimage` in `mv_disp.c`, `mv_vobuf.c`, `decBitStrm0` in
`mv_videodec.c`, `readMpeg` in `mv_main.c`):

1. Preroll: decode until the 5-slot picture ring is full (`voBufCreate`:
   `max = 5`) and the 24576-byte IOP PCM buffer is preset
   (`audioDecIsPreset`). The host reads instantly, so this takes no vsync.
2. `startDisplay(1)`: `sceGsSyncV` until it reports the even field; the PCM
   channels start (`audioDecStart`).
3. Per vsync (the loop yields with `RotateThreadReadyQueue` at Main's
   priority and `ico_sched_spin_vsync`, as `readMpeg`'s `switchThread` busy
   loop did, so the scheduler, sound and other threads run at their vblank
   as on the PS2): the oldest picture's even field goes out on a field-0
   vblank and its odd field on the next field-1 vblank, which frees its
   slot. That is two vsyncs per picture, so 25 pictures a second at 50 Hz
   (and 30 at 60 Hz, faster than a 25 fps stream, as the PS2 code does in
   its 60 Hz mode). The decoder fills a freed slot in the same vsync.
4. The end comes when the last picture of the stream goes into the ring
   (`readMpeg`'s flush wait, `videoDecIsFlushed`). The pictures still in
   the ring are never shown, as on the PS2: N - 5 of N pictures are shown
   and the film lasts 2 (N - 5) + 1 vsyncs after the display starts.
5. Abort: `movie_abort_check`, which runs `ExecKeyInput` once per vsync and
   returns 1 on START, is polled once per vsync from the vsync where 11
   pictures have been decoded (`frameCount >= 11`). A 1 ends the film at
   the next freed slot (`decBitStrm0` sees the abort only after its wait
   for a slot), and `movie_proc` returns 1 (Main then sets
   `stage_after_skipping_demo`).

The timing depends only on the stream's picture count, not on decoding,
decode errors, the window or the read speed, so a film lasts the same
number of vsyncs headless, in the window, with `ICO_FMV_DECODE=0`, and on
any host; that keeps traces through a film reproducible. For
`pal_advertise.pss` (3438 pictures) that is 6867 vsyncs from the display
start, 3433 pictures shown, and the abort poll from vsync 14.

libmpeg2 puts its first picture out only after the third access unit, so
`decode_into` reads on until the decoder has its next unit; otherwise the
first ring slot would get no picture and every shown picture would be one
late.

Not reproduced, because there is no host counterpart: the decode thread and
its interleaving, the read-starved pause (`sceCdStStat() < 32`; the host
stream never runs low), and the IPU and GS DMA handlers. The PS2 polls
for START only while undemuxed bytes remain (`left >= 5`), so not in its
last moments; the port polls every vsync until the end. Pictures go into
the ring at the rate the PS2 reached with a fast decoder; a PS2 decode that
fell behind would have stretched the film (a missing picture holds the
display), which the port does not model.

## Audio

`movie.c` makes `mv_audiodec.c`'s calls with its numbers: `SgStPcmInit`,
`SgStPcmOpen({0, 0x10400, iopBuf, 24576})` and
`({1, 0x10400, iopBuf + 0x200, 24576})` (0x400 bytes per callback, sample
step shift 1, so each channel reads one 512-byte block of every 1 KB),
`SgStPcmSetEffect(8)`; at the display start `SgStPcmLseek(0/1, 0)`,
`SgStPcmVolume(1, 0, 0x3FFF)`, `SgStPcmVolume(2, 0x3FFF, 0)` (mono:
`(3, 0x1FFF, 0x1FFF)`), `SgStPcmPlay(3)`; at the end
`SgStPcmVolume(3, 0, 0)`, `SgStPcmStop(3)`, then close and `SgStPcmQuit`.
The PCM after the header is copied into the IOP buffer
(`sceSifAllocIopHeap(24576)`, after the stream ring's `576 * 2048 + 16`, in
the PS2's order) in whole kilobytes: first up to 24576 bytes (the preset),
then up to 1 KB short of `SgStPcmIopReadAddr(0)` once per vsync
(`audioDecSendToIOP`). The EE side ring's 49152-byte cap is kept as a
per-send limit. The IOP copy is a `memcpy` into the emulated IOP RAM
(`sceSifSetDma` on the PS2). From there the sound driver host plays it
(docs/port/AUDIO.md, "PCM streams").

In a WAV dump of the film, the right output channel equals the stream's
first block of each pair times 0.4995 and the left the second block times
0.4996 (correlation 1.00000 each), that is 0x3FFF / 0x8000 = 0.49997 with
the panning `mv_audiodec.c` sets (channel 0, the first block, panned
right).

## Colour

The IPU converted each picture with its CSC command (libmpeg `csc.c`
`_doCSC`: `IPU_CMD` 0x7 with DTE 0, OFM 0: RGB32, no dither). Its maths is
hardware, so the port takes PCSX2's IPU model for it
(`pcsx2/IPU/yuv2rgb.cpp` `yuv2rgb_reference`, commit 144a19ba, read for the
behaviour, no code copied): ITU-R BT.601 limited range with 1/64
coefficients,

    lum = (0x95 * max(0, Y - 16)) >> 6
    R = clamp((lum + ((0xCC * (Cr - 128)) >> 6) + 1) >> 1)
    G = clamp((lum + ((-0x68 * (Cr - 128)) >> 6) + ((-0x32 * (Cb - 128)) >> 6) + 1) >> 1)
    B = clamp((lum + ((0x102 * (Cb - 128)) >> 6) + 1) >> 1)

with Cb and Cr taken from the 2 x 2 block's own sample (no chroma
interpolation). `yuv.hlsl` computes exactly this per source sample in
integers and blends four converted samples bilinearly for scaling, so a
1:1 draw is bit-exact with the model. The model has not been compared with
a real IPU's output.

## Display

The PS2 player set up its own display (`mv_disp.c`: 720 wide, the full
576 or 480 interlaced lines, both fields from one frame) and centred the
picture: `(imageW - w) / 2`, `(imageH - h) / 2` lines (`dispSetTags` in
`mv_videodec.c`; for `pal_advertise.pss` in PAL, 48 black lines above and
below). The port's `DISPLAY` target is the game's reduced 512 x H/2 frame,
so drawing the film into it would halve its resolution. `rd_video.c`
therefore draws at the presenter's level instead, which is where the PS2's
own display was: each picture goes straight to the output in the
presenter's 4:3 box (the box rule of `rd_present.c`), the 720 x 576 (or
480) display area filling the box and the picture at its PS2 place in it,
everything else in `dispClear`'s colour (`mpegPlayInitColor`, black unless
a script sets it). The game's `DISPLAY` target and frames are not touched.
It presents once per picture (25 Hz), from `movie_proc`'s vsync loop, as
`rd_EndFrame` does for game frames. The upload's row pitch and offset are
aligned to the backend's `copyRowPitchAlign` and `copyOffsetAlign` (256 and
512 on D3D12).

- **Widescreen:** the box stays 4:3, so a wider window pillarboxes the
  film; it is never stretched. All three disc streams are 4:3 (aspect code
  2). The PS2 player ignored the aspect code, so 16:9 content would be
  shown squeezed in the 4:3 area as on the PS2; no stream on the disc needs
  more.
- **Mirror:** the film is flipped exactly when mirror mode is on
  (`rd__MirrorOn`: the run's `rd_SetMirror` or `RdSettings.mirror`;
  `port/render/rd_video.c`); there is no separate switch. The film's sound
  goes through the SPU2 and is swapped with mirror mode too
  (docs/port/AUDIO.md, "Mirror mode").
- **Headless:** there is no renderer; pictures are decoded and dropped.
  `ICO_FMV_DECODE=0` skips the decoding; the timing is the same.

The PS2 player took about 14 MB from `ios_partition_mpeg` (the same
partition as `ios_partition_isys`): the 320 KB read buffer, the 1.8 MB
libmpeg work area, the 8.3 MB and 3.9 MB picture ring and tags, the IPU
input ring and the 48 KB audio ring (`readBufDelete` and `audioDecDelete`
never free theirs). The port uses host memory for all of it and leaves the
partition untouched.

## Tests

- `fmv` (`port/fmv/test/fmv_test.c`; the disc part takes the image path and
  skips without it):
  - a synthetic PSS built in the test (MPEG-2 packs, a system header, video
    PES with and without PTS/DTS, audio PES with the Sony framing and the
    SShd/SSbd header, padding, the end code) parsed in every split size
    from 1 byte to 4 KB: elementary streams, audio bytes, time stamps and
    header fields come back, and the parser resyncs after garbage;
  - access-unit cutting of a video ES;
  - the decoder: libmpeg2 ships no test vectors and no encoder is
    available, so the test hand-assembles a conforming MPEG-2 Main Profile
    stream (sequence header and extension, GOP, an I picture with picture
    coding extension, two slices, four intra macroblocks of DC-only blocks
    coded with tables B-12, B-13 and B-14) whose luma and chroma values are
    known, and checks every decoded sample and the sequence info. This
    covers the header path, intra VLC/DC decoding and the API sequence, not
    P/B prediction or AC coefficients;
  - the disc: `pal_advertise.pss` demuxed whole (3438 pictures, the audio
    header, all bytes parsed) and its first 60 access units decoded (58
    pictures out, the rest held for display order; 0 errors). This is
    where P/B decoding is exercised, on the real content, without a
    reference image to compare with;
  - the pacing state machine (the counts above).
- `rd_video` (`port/fmv/test/rd_video_test.c`, on the renderer's headless
  device; exit 77 without one; with the validation layer when fetched): a
  flat picture of a known colour placed in a larger display area (the CSC
  of Y 81 Cb 90 Cr 240 is 254 0 0; the rest is the clear colour), a
  64 x 48 ramp with varied luma (including below 16 and above 235) and
  per-block chroma bit-exact against the CPU model, and the mirror toggle.
- `shaders_table` lists `yuv_vs` and `yuv_ps`.

Open items for films are in docs/TODO.md.
