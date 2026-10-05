# FMV playback (Phase 4E)

The PS2 movie player (`ico2/ito/mpeg/mv_*.c`) is PS2-only now
(`ICO_EE_ONLY_SOURCES`, `tools/gen_sources.py`); the host build's
`movie_init`, `movie_proc` and `movie_end` are `port/fmv/movie.c`, with the
same signatures and return values (`ico2/ito/include/mv_main.h`). Main's
call site (`ico2/common/src/main.c`, the `mpegPlay` block of `Main`) is
unchanged.

| file | what |
| --- | --- |
| `port/fmv/pss.c`, `pss.h` | PSS demuxer: packs, PES, Sony's audio framing, the 40-byte audio header, MPEG video access units |
| `port/fmv/m2v.c`, `m2v.h` | the MPEG-2 decoder wrapper over Ittiam libmpeg2 |
| `port/fmv/ithread_single.c` | libmpeg2's thread layer for a single-threaded decoder (replaces its pthread `ithread.c`) |
| `port/fmv/movie_pace.c`, `movie_pace.h` | the display timing state machine (mv_disp.c's vblank handler and mv_vobuf.c's ring) |
| `port/fmv/movie.c` | `movie_init` / `movie_proc` / `movie_end`: file, stream, demux, decode, audio, pacing, display |
| `port/fmv/rd_video.h`, `port/render/rd_video.c`, `port/shaders/yuv.hlsl` | the picture on the output (window build) |
| `port/fmv/test/fmv_test.c`, `test/rd_video_test.c` | tests (below) |

## Decoder

Ittiam libmpeg2 (AOSP `platform/external/libmpeg2`, Apache-2.0), tag
`android-16.0.0_r4`, as `docs/research/licences.md` (R5, "MPEG-2
decoding") recommended. Pin, fetch and licence: `tools/fetch_deps.sh`
section 5 and `docs/port/THIRD_PARTY.md`. It compiled with no change for
`linux-x64` (gcc 14), `win-x64` (mingw-w64 gcc) and `asan`, so the FFmpeg
fallback was not needed and nothing of FFmpeg is fetched. Built by
`port/fmv/CMakeLists.txt` from the fetched source with each preset's
compiler: the generic C path only (the `riscv` function selectors select
the generic routines; no SSE/AVX), one core, no deinterlacing, 4:2:0
planar output into planes `m2v.c` owns, maximum size 720 x 576 (the IPU's
own limit through `sceMpegGetPicture(.., 1620)`, 1620 macroblocks). The
library left-shifts negative values (`impeg2d_vld.c`), so the sanitizer
build compiles it with `-fno-sanitize=shift`. Cost measured in the game run
below: about 3440 pictures of 720 x 480 decoded per film inside a 70 s run of 6000 ticks
and two films (no profiler run; roughly 7 ms a picture on this host).

## Streams

The PAL disc's `DATA.DF` directory holds three PSS files (read with the
directory format of `fumi/ios/cdvd.c` `unifile_read_func`; measured
2026-10-05 on `baserom/Ico_PAL.iso`):

| file | DATA.DF offset, size | video | pictures | audio |
| --- | --- | --- | --- | --- |
| `pal_advertise.pss` | 390270976, 130301956 | 720 x 480, 4:3 (aspect code 2), 25 fps, Main@Main, frame pictures, I/P/B (234 I, 934 P, 2270 B), 234 GOPs | 3438 | PCM 48 kHz stereo, 26,413,056 bytes (137.57 s) |
| `pal_advertise576.pss` | 520574976, 130334724 | 720 x 576, 4:3, 25 fps, frame pictures | 3439 | same format |
| `advertise.pss` | 762783744, 104398852 | 720 x 480, 4:3, 29.97 fps (frame rate code 4), frame pictures | about 4100 (start-code count, not demuxed) | same format |

`movieFile` (the `moviefile` data member) also names
`movie/pal_demo_advertise.pss`, `movie/pal_end.pss` and
`movie/pal_badend.pss` (stages 103-105, the E3 build's); they are not on
the disc. `movie_init` returns -1 for a name the directory lacks and
`movie_proc` then returns 0 at once (the PS2's `getFileLsn` would assert;
the retail game never asks for them).

Which stage plays which: `stageData[].mpegNo` (stage-all): 57 `memory`
-> 1 (`advertise.pss`), 58 `memoryPAL` -> 2 (`pal_advertise.pss`), 59
`memoryPAL576` -> 3. The title (`script/src/op.c`, `actOpDemo01`) plays
stage 58 in PAL (57 in NTSC, `systemStatus[0]`) when the title sequence
runs out with no START: mode 0 ends, its 10 s timer runs, mode 2 calls
`stgmgrForceSwitchWithFade(58, ...)` with `mpegPlayReturnStage = 1`. The PAL
player is thus given a 720 x 480 stream in a 720 x 576 display (`main.c`
passes `systemStatus[0] ? 576 : 480`).

### PSS format

- 16384-byte packs with MPEG-2 pack headers (ISO/IEC 13818-1 2.5.3), a
  system header (0xBB) in the first, padding (0xBE), the end code
  0x000001B9 last.
- Video: PES 0xE0, MPEG-2 PES headers, PTS+DTS on picture-starting packets
  (first PTS 4724 at 90 kHz).
- Audio: PES 0xBD (private stream 1), PTS on each, 10-byte header data
  (PTS + 5 stuffing bytes). Every payload starts with 4 bytes, `FF A0 00
  00` in every packet of all three files; `0xA0 + n` is the sub-stream that
  libmpeg's `sceMpegAddStrCallback(type 2, channel n)` selects and
  `mv_audiodec.c` `pcmCallback` skips the 4 bytes (`rd += 4`). After them
  the first packet carries a 40-byte header that `audioDecEndPut` collects:
  `"SShd"`, then little-endian words header size 24, type (0 PCM big-endian,
  1 PCM little-endian, 2 ADPCM: `mv_audiodec.c`'s own debug print), rate,
  channels, interleave, interleave start/end block; `"SSbd"`, data size.
  All three files: type 1, 48000 Hz, 2 channels, interleave 512,
  start/end -1, data 26,413,056 (`pal_advertise.pss`). The rest is PCM:
  512-byte blocks alternating between the two channels (256 samples each).
- `pss.c` also accepts MPEG-1 pack and PES forms; `pss.h` documents the
  API.

## Timing

`movie_pace.c` reproduces the PS2 display loop
(`mv_disp.c` `vblankHandler`, `handler_endimage`; `mv_vobuf.c`;
`mv_videodec.c` `decBitStrm0`; `mv_main.c` `readMpeg`):

1. Preroll: decode until the 5-slot picture ring is full
   (`voBufCreate`: `max = 5`) and the 24576-byte IOP PCM buffer is preset
   (`audioDecIsPreset`). The host reads instantly, so this takes no vsync.
2. `startDisplay(1)`: `sceGsSyncV` until it reports the even field; the
   PCM channels start (`audioDecStart`).
3. Per vsync (the loop yields with `RotateThreadReadyQueue` at Main's
   priority and `ico_sched_spin_vsync`, as `readMpeg`'s `switchThread` busy
   loop did; the scheduler, sound and other threads run at their vblank as
   on the PS2): the oldest picture's even field goes out on a field-0
   vblank and its odd field on the next field-1 vblank, which frees its
   slot. Two vsyncs per picture, so 25 pictures a second at 50 Hz (and 30
   at 60 Hz, faster than the 25 fps stream, as the PS2 code does in its
   60 Hz mode). The decoder fills a freed slot in the same vsync.
4. The end: when the last picture of the stream goes into the ring
   (`readMpeg`'s flush wait, `videoDecIsFlushed`). The pictures still in the
   ring are never shown, as on the PS2: N - 5 of N pictures are shown and
   the film lasts 2 (N - 5) + 1 vsyncs after the display starts.
5. Abort: `poll` (`movie_abort_check`, which runs `ExecKeyInput` once per
   vsync and returns 1 on START) is called once per vsync from the vsync
   where 11 pictures have been decoded (`frameCount >= 11`); a 1 ends the
   film at the next freed slot (`decBitStrm0` sees the abort only after its
   wait for a slot) and `movie_proc` returns 1 (Main then sets
   `stage_after_skipping_demo`).

The timing depends on the stream's picture count only: not on decoding,
decode errors, the window or the read speed, so a film lasts the same
number of vsyncs headless, in the window, with `ICO_FMV_DECODE=0` and on
any host. `fmv_test` checks the state machine (3438 pictures: 6867 vsyncs
from the display start, 3433 shown, the poll from vsync 14, aborts).

Not reproduced (no host counterpart): the decode thread and its
interleaving, the read-starved pause (`sceCdStStat() < 32`; the host
stream never runs low), the IPU/GS DMA handlers. The PS2 polls only while
undemuxed bytes remain (`left >= 5`), i.e. not in its last moments; the
port polls every vsync until the end. Pictures go into the ring at the
vsync rate the PS2 reached with a fast decoder; a PS2 decode that fell
behind would have stretched the film (a missing picture holds the
display), which the port does not model.

## Audio

`movie.c` makes `mv_audiodec.c`'s calls with its numbers: `SgStPcmInit`,
`SgStPcmOpen({0, 0x10400, iopBuf, 24576})` and `({1, 0x10400, iopBuf +
0x200, 24576})` (0x400 bytes per callback, sample step shift 1: each
channel reads one 512-byte block of every 1 KB), `SgStPcmSetEffect(8)`;
at the display start `SgStPcmLseek(0/1, 0)`, `SgStPcmVolume(1, 0,
0x3FFF)`, `SgStPcmVolume(2, 0x3FFF, 0)` (mono: `(3, 0x1FFF, 0x1FFF)`),
`SgStPcmPlay(3)`; at the end `SgStPcmVolume(3, 0, 0)`, `SgStPcmStop(3)`,
then close and `SgStPcmQuit`. The PCM after the header is copied into the
IOP buffer (`sceSifAllocIopHeap(24576)`, after the stream ring's
`576 * 2048 + 16`, in the PS2's order) in whole kilobytes: first up to
24576 bytes (preset), then up to 1 KB short of `SgStPcmIopReadAddr(0)`
once per vsync (`audioDecSendToIOP`). The EE side ring's 49152-byte cap is
kept as a per-send limit. The IOP copy is a `memcpy` into port/data's IOP
RAM (`sceSifSetDma` on the PS2). Everything after the SNDN2DRV RPC is
package 4B's (`docs/port/AUDIO.md`, "PCM streams").

Checked in the game run's WAV (below): over the film the right output
channel equals the stream's first block of each pair times 0.4995 and the
left the second block times 0.4996 (correlation 1.00000 each), i.e.
0x3FFF / 0x8000 = 0.49997 and the panning `mv_audiodec.c` sets (channel 0,
the first block, panned right).

## Colour

The IPU converted each picture with its CSC command (libmpeg `csc.c`
`_doCSC`: `IPU_CMD` 0x7 with DTE 0, OFM 0: RGB32, no dither). Its maths is
hardware; the port takes PCSX2's IPU model for it (`pcsx2/IPU/yuv2rgb.cpp`
`yuv2rgb_reference`, commit 144a19ba, read for the behaviour, no code
copied): ITU-R BT.601 limited range with 1/64 coefficients,

    lum = (0x95 * max(0, Y - 16)) >> 6
    R = clamp((lum + ((0xCC * (Cr - 128)) >> 6) + 1) >> 1)
    G = clamp((lum + ((-0x68 * (Cr - 128)) >> 6) + ((-0x32 * (Cb - 128)) >> 6) + 1) >> 1)
    B = clamp((lum + ((0x102 * (Cb - 128)) >> 6) + 1) >> 1)

with Cb/Cr taken from the 2 x 2 block's own sample (no chroma
interpolation). `yuv.hlsl` computes exactly this per source sample in
integers and blends four converted samples bilinearly for scaling, so a
1:1 draw is bit-exact with the model (`rd_video_test`). Not verified
against a real IPU.

## Display

The PS2 player set up its own display (`mv_disp.c`: 720 wide, the full
576/480 interlaced lines, both fields from one frame) and placed the
picture centred: `(imageW - w) / 2`, `(imageH - h) / 2` lines
(`mv_videodec.c` `dispSetTags`; for `pal_advertise.pss` in PAL, 48 black
lines above and below). The port's `DISPLAY` target is the game's reduced
512 x H/2 frame, so drawing the film into it would halve its resolution.
`rd_video.c` therefore draws at the presenter's level instead, which is
where the PS2's own display was: each picture goes straight to the output
in the presenter's 4:3 box (the box rule of `rd_present.c`), the 720 x
576 (or 480) display area filling the box and the picture at its PS2
place in it, everything else in `dispClear`'s colour (`mpegPlayInitColor`,
black unless a script sets it). The game's `DISPLAY` and frames are not
touched. It presents once per picture (25 Hz), from `movie_proc`'s
vsync loop, as `rd_EndFrame` does for game frames.

- **Widescreen:** the box stays 4:3, so a wider window pillarboxes the film;
  it is never stretched. All three disc streams are 4:3 (aspect code 2).
  16:9 content would be shown as the PS2 showed it, in the 4:3 area
  (anamorphic, squeezed), since the PS2 player ignored the aspect code; no
  stream on the disc needs more.
- **Mirror:** UI-space-like: unmirrored by default. With mirror mode on
  (`rd__MirrorOn`: the run's `rd_SetMirror` or `RdSettings.mirror`) the
  film's rectangle is drawn flipped when both switches in
  `port/render/rd_video.c` are on (R7c, RENDER_API.md section 21 "FMV"):
  the player's `[game] mirror_fmv` (default `true`; docs/port/CONFIG.md),
  which `port/ui/settings.c` hands to `rd_VideoSetMirrorOption` when the
  Settings menu installs and again from the Gameplay page's "Mirror the
  movies" row (docs/port/SETTINGS.md), and `movie.c`'s per-movie
  `rd_VideoSetMirror`, on unless the developer environment variable
  `ICO_MIRROR_FMV` is `0` (read in `movie_init`, `port/fmv/movie.c`), a
  developer override kept beside the key. The film's sound goes through
  the SPU2 and is swapped with mirror mode whatever `mirror_fmv` says
  (docs/port/AUDIO.md "Mirror mode").
- **Headless:** no renderer; pictures are decoded and dropped
  (`ICO_FMV_DECODE=0` skips the decoding; the timing is the same).

## Tests

- `fmv` (`port/fmv/test/fmv_test.c`, all presets; the disc part takes the
  image path and skips without it):
  - a synthetic PSS built in the test (MPEG-2 packs, a system header, video
    PES with PTS/DTS and without, audio PES with the Sony framing and the
    SShd/SSbd header, padding, the end code) parsed in every split size
    from 1 byte to 4 KB: elementary streams, audio bytes, time stamps and
    the header fields come back; resync after garbage;
  - access-unit cutting of a video ES;
  - the decoder: no test vector ships with libmpeg2 (its tree has the
    decoder, a test program and a fuzzer, no streams), and no encoder is
    available, so the test hand-assembles a conforming MPEG-2 Main Profile
    stream (sequence header and extension, GOP, I picture with picture
    coding extension, two slices, four intra macroblocks of DC-only blocks
    coded with tables B-12, B-13, B-14) whose luma and chroma values are
    known, and checks every decoded sample and the sequence info. This
    covers the header path, intra VLC/DC decoding and the API sequence;
    not P/B prediction or AC coefficients;
  - the disc: `pal_advertise.pss` demuxed whole (3438 pictures, the audio
    header, all bytes parsed) and its first 60 access units decoded (58
    pictures out, the rest held for display order; 0 errors). This is where
    P/B decoding is exercised, on the real content, without a reference
    image to compare against;
  - the pacing state machine (above).
- `rd_video` (`port/fmv/test/rd_video_test.c`, rd's headless device,
  lavapipe here; exit 77 without one; with the validation layer when
  fetched): a flat picture of a known colour placed in a larger display
  area (the picture's pixels are the CSC of Y 81 Cb 90 Cr 240 = 254 0 0,
  the rest the clear colour), a 64 x 48 ramp with varied luma (including
  below 16 and above 235) and per-block chroma bit-exact against the CPU
  model, and the mirror toggle (flipped with mirror mode and toggle on,
  unflipped with the toggle off). 0 validation errors.
- `shaders_table` lists `yuv_vs` and `yuv_ps`.

## The game run (2026-10-05)

One headless `linux-x64` run (`-DICO_LINK_EXE=ON`), `timeout 600`,
`ticks=6000`, `audio_dump=1`. `pad-boot.txt` (1300 ticks) contains no
film: it presses START, which skips the title sequence, and starts a new
game (stages 1 -> 41 -> ... -> 3; films are stages 57-59 and 103-105 only).
So the run's pad script was `pad-boot.txt`'s phase 1 up to tick 560 with
its START presses removed (LEFT and CROSS answer the boot signs), then
nothing, letting the title sequence run out.

- Exit 0 after 6000 Main ticks, 25755 vsyncs, 70 s wall time.
- The film played twice (title idle cycle): `movie_init` at tick 2613 and
  at tick 5506, `movie/pal_advertise.pss`, 720 x 576 display, stereo.
  Each: 3438 pictures in the stream, 3438 put into the ring, 3433 shown,
  3437 decoded, 0 decoder errors, 26,413,056 PCM bytes sent (all of it), 6868
  vsyncs inside `movie_proc` (2 x 3433 + 2: the `startDisplay` wait and
  the final vblank). In the trace the vsync column jumps by 6876 across
  each film (tick 2613 -> 2614: vsync 5233 -> 12109), the same both
  times.
- Audio during the first film (WAV vsyncs 5240-12100): left -24.0 dBFS
  RMS, peak 14863; right -24.2 dBFS, peak 14041; one-second windows
  between -28 and -23 dBFS; the channel/gain match against the stream is
  under "Audio". The whole WAV: 24,724,800 frames, peak 30845.
- The 3437 exposed an off-by-one fixed after the run: libmpeg2 puts its
  first picture out after the third access unit (two units of latency, not
  one: measured over the whole stream, 3436 pictures out of 3438 units plus
  2 at the flush), and the demux only guaranteed two units ahead, so the
  first ring slot got no picture and every shown picture was one late.
  `decode_into` now reads on until the decoder has its next unit. This
  touches only which picture fills a slot, not the timing or any game
  state; the run was not repeated (one run allowed), so the fixed count
  (3438) is from the standalone measurement, not from the game.
- Trace against 2J's (`build-host/2j-linux-x64/logs/trace-20261005-062107.txt`,
  `pad-boot.txt`): identical for ticks 0-518; from tick 519 they differ
  because the inputs do (2J's START presses; gflag word 12 bit 1 sets in
  2J's), well before the first film at tick 2613. 4B's trace is identical
  to 2J's there, with the same first difference. No earlier trace
  contains a film, and before this package a film took no simulated time,
  so there is nothing to compare the film itself with.

## Package V1: the window build showed no picture

User report (`dist/ico-pc-v0.4-win.zip`, Windows): the title's attract
film (stage 58, `pal_advertise.pss`) played its sound and no picture.

- **Cause.** `movie.c` hands pictures to `rd_video.c` only under
  `#ifdef ICO_RD` (`show_clear`, `show_slot`). `movie.c` is one of
  `ico_pc`'s own sources (`port/fmv/CMakeLists.txt`), and `ico_pc`'s
  definitions never had `ICO_RD`: the top-level `CMakeLists.txt` puts it
  in `ICO_GAME_DEFINITIONS`, which only the game libraries get, and
  `ico_pc` gets `ICO_HEADLESS` in the headless build and nothing in the
  window build. So since Phase 4 every window build compiled `movie.c`'s
  headless branch: decoded pictures dropped, the audio path untouched.
  Every film was affected, on Vulkan and D3D12 alike (a compile-time
  switch, before any backend). The v0.4 map shows it:
  `libico_render.a(rd_video.c.obj)` is pulled in by `settings.c`
  (`rd_VideoSetMirrorOption`), not by `movie.c` (`ico_pc_x64.map`, lines
  480-481).
- **Change.** `port/fmv/CMakeLists.txt` gives `movie.c` `ICO_RD=1` in the
  window build (`NOT ICO_HEADLESS`), and `movie.c` stops the build with an
  `#error` when neither `ICO_RD` nor `ICO_HEADLESS` is defined, so a
  build that loses the define fails instead of shipping a silent film.
  `rd_video.c` and the backends needed no change.
- **Measured** (window build `linux-x64`, lavapipe, `SDL_VIDEODRIVER=
  offscreen`, `start_stage=58`, a temporary probe in `presentVideo` that
  logged its calls and read the swapchain image back every 250th picture,
  removed afterwards): before the change `presentVideo` was never called
  (no probe line; `fmv: movie_proc played after 6868 vsyncs ... 3433
  shown`); after it, the first call was `movie_init`'s clear and every
  later one a 720 x 480 picture into the 960 x 720 output (probe lines
  through call 3400 of 3433 shown), and the 13 read-back frames each had
  exactly the 960 x 600 picture area non-black (576000 pixels; mean level
  30 to 136 by scene) with black bars above and below: the film in its
  PS2 place (frame 1250 inspected by eye). The same 6868 vsyncs and audio
  byte count as before. The title's idle path to the film (tick 2613 in
  the game run above) was not run in the window build: lavapipe reached
  tick 1489 in 300 s. It goes through the same `movie_init` /
  `movie_proc` calls, and the cause does not depend on how the film is
  reached.
- D3D12 was not run here. Its path is the one `rd_present.c` already
  uses for every game frame (backbuffer acquire with an undefined start
  state, the transition to render target and to present), plus a
  buffer-to-texture copy whose row pitch and offset `rd_video.c` aligns
  to the backend's `copyRowPitchAlign` / `copyOffsetAlign` (256 / 512 on
  D3D12, `d3d12_device.c`).

## Open items

- The EE heap: the PS2 player took about 14 MB from `ios_partition_mpeg`
  (= `ios_partition_isys`): the 320 KB read buffer, the 1.8 MB libmpeg
  work area, the 8.3 MB + 3.9 MB picture ring and tags, the IPU input ring,
  the 48 KB audio ring; `readBufDelete` and `audioDecDelete` never free
  theirs. The port uses host memory for all of it. Whether the partition's
  state after a film is observable (the stage switch that follows reloads
  the stage) is not checked.
- The window build's film was not seen running until package V1 (above):
  it never drew there.
- The CSC constants come from PCSX2's model, not from hardware.
- `advertise.pss` (NTSC, 29.97 fps) and `pal_advertise576.pss` were
  inspected, not played.
- The `mirror_fmv` key and Settings row and the audio pan swap came with
  R7c (above; docs/port/AUDIO.md "Mirror mode"). Left: with mirror mode on
  and `mirror_fmv` off, the film's stereo is still swapped while its
  picture is not.
- `docs/port/AUDIO.md` ("PCM streams") still says the movie player is not
  in the build, and the top-level `CMakeLists.txt` comments still name
  `ito/mpeg` among `ICO_RENDERER_SOURCES` and the FMV among the null floor:
  outside this package's files.
- `ico2/common/src/main.c` needed no change.
