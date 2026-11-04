# Movies

How the game's movies play: the PS2 player (`ico2/ito/mpeg`), which demuxed PSS files and decoded
MPEG-2 on the [IPU](ARCHITECTURE.md#terms), is not compiled; `port/fmv/movie.c` takes its place
with the same entry points, the same disc and sound calls and the same timing, a PSS demuxer and
libmpeg2 for the pictures, and the presenter for the screen. [ARCHITECTURE.md](ARCHITECTURE.md)
places it among the other layers; the sound side is [AUDIO.md](AUDIO.md)'s.

## Files

| path | what it holds |
| --- | --- |
| `port/fmv/movie.c` | `movie_init`, `movie_proc`, `movie_end`: the file, demux, decode, the display ring, the PCM audio, the skip |
| `port/fmv/movie_pace.h`, `movie_pace.c` | the display timing of the PS2 player, without the hardware |
| `port/fmv/pss.h`, `pss.c` | the PSS demuxer: packs, PES packets, Sony's audio framing, video access units |
| `port/fmv/m2v.h`, `m2v.c` | the MPEG-2 decoder: a wrapper over Ittiam libmpeg2, with the interlace flags read from the stream |
| `port/fmv/ithread_single.c` | libmpeg2's thread layer for a decoder that never starts a thread |
| `port/fmv/field_match.h`, `field_match.c` | field matching (inverse telecine), used by `fmv_fields_test` only |
| `port/fmv/rd_video.h` | the movie's display, implemented in `port/render/rd_video.c` (window build) |
| `port/render/rd_video.c` | the picture into the presenter's 4:3 box: upload, colour conversion, deinterlace, present |
| `port/shaders/yuv.hlsl` | the IPU's colour conversion and the field deinterlacer |
| `port/fmv/android/log/log.h` | the one Android logging call libmpeg2 makes, as a no-op |
| `port/fmv/CMakeLists.txt` | `ico_libmpeg2`, `ico_fmv`, `movie.c` in `ico_pc`, the tests |
| `port/fmv/test/` | the FMV tests |

libmpeg2 is a source tree fetched by `tools/fetch_deps.sh` (`tools/toolchain/deps/libmpeg2`,
Apache-2.0) and compiled with each preset's compiler into `ico_libmpeg2`; without it `ico_pc` does
not configure (`port/fmv/CMakeLists.txt`). `pss.c` and `m2v.c` have no game symbols and form
`ico_fmv`; `movie.c` and `movie_pace.c` call into the game and the game calls them, so they are
compiled into `ico_pc` itself.

## The movie player

The game's `Main` plays a movie with `movie_init(&movieFile[...], 720, 576 or 480, 36, 12, mono,
color)`, then `ret = movie_proc(movie_abort_check)`, `ret == 1` meaning the movie was skipped
(`port/fmv/movie.c`, header comment; `ico2/common/src/main.c`). `movie.c` keeps those signatures
and return values and does, step by step, what the PS2 player did (`port/fmv/movie.c`, header
comment):

| part | the PS2 player | here |
| --- | --- | --- |
| file | `strFileOpen`: `DFDATAS/<base name>` looked up in DATA.DF's directory, streamed with [libcdvd](ARCHITECTURE.md#terms)'s `sceCdSt*` through a 576-sector IOP ring | the same directory read through the VFS (`find_in_datadf`), the same `sceCdSt*` calls and IOP allocation, 64 KB reads (`read_chunk`) |
| video | libmpeg's `sceMpegDemuxPssRing` and the IPU's `sceMpegGetPicture` in a decode thread | `pss.c` and libmpeg2 on the calling [fiber](ARCHITECTURE.md#terms), a picture per free display slot (`fill_slots`) |
| timing | `mv_disp.c`'s vblank handler: two vsyncs a picture, five pictures decoded ahead | `movie_pace.c`, driven once per simulated vsync |
| audio | `mv_audiodec.c`: the PCM through an [EE](ARCHITECTURE.md#terms) ring into a 24576-byte IOP buffer read by SNDN2DRV's PCM channels 0 and 1 | the same `SgStPcm*` calls in the same order with the same numbers; the IOP copy is a `memcpy` into IOP RAM |
| abort | `movie_abort_check` polled once a vsync after 11 pictures | the same |

The decode thread, the interrupt and [DMA](ARCHITECTURE.md#terms) handlers, the
[GS](ARCHITECTURE.md#terms) register setup and the read-starved pause (`sceCdStStat` never runs low
on the host) have no host counterpart (`port/fmv/movie.c`, header comment).

### movie_init

`movie_init` records the display size and the mono flag, clears the display to the given colour
(`rd_video_set_display`, `rd_video_clear`), finds the base name in DATA.DF's directory, starts the
`sceCdSt*` stream over a 576-sector [IOP](ARCHITECTURE.md#terms) ring, creates the decoder, and
opens the PCM side as `audioDecCreate` did: `SgStPcmInit`, channel 0 and channel 1 opened on the IOP
buffer (0x200 apart) with `0x10400` (0x400 bytes a callback, step shift 1: 512-byte interleave
blocks), then `SgStPcmSetEffect(8)` (`port/fmv/movie.c` (`movie_init`)). A name missing from the
directory, a failed stream start or a PCM channel that does not open returns -1, logged as
`fmv: <name> is not in DATA.DF's directory` or `fmv: sceCdStStart(<lsn>) failed`. A decoder that
cannot be created (`fmv: the MPEG-2 decoder could not be created; pictures are dropped`) or no IOP
memory for the PCM buffer (`fmv: no IOP memory for the PCM buffer`) is logged and the movie plays on
without pictures or without sound (`port/fmv/movie.c` (`movie_init`)). `ICO_FMV_DECODE=0` in the
environment skips decoding; the timing is unchanged.

### movie_proc

`movie_proc` runs the whole movie inside one call, inside one [Main tick](ARCHITECTURE.md#terms),
then calls `movie_end` itself (`port/fmv/movie.c` (`movie_proc`, `readMpeg`)):

1. Preroll: fill the display ring and preset the IOP buffer full. Reads are immediate on the
   host, so no [vsync](ARCHITECTURE.md#terms) passes unless the ring and buffer are not yet full.
2. Start the display on a vblank that reports the even field, and start the PCM channels
   (`audio_start`: both channels at volume 0x3FFF panned left and right, or both at half volume
   for a mono movie).
3. Each vsync (`next_vsync`): rotate the caller's priority level as `readMpeg`'s
   `RotateThreadReadyQueue` did, spin to the next vsync with `ico_sched_spin_vsync` (so the
   higher-priority game threads run at the vblank as on the PS2), and read the GS field from
   `GS_CSR`. The pacing state machine says whether to show a picture or free a slot; the abort
   poll runs once due; free slots are refilled; the PCM is topped up behind
   `SgStPcmIopReadAddr(0)` in whole kilobytes.
4. At the end: clear the frame buffer, reset the PCM channels (volume 0, stopped).

Because the movie runs inside one tick, the watchdog hears from it through the progress count
instead: `ico_diag_set_movie(1)` for the duration, a progress note each preroll vsync and each
shown picture (`IcoMoviePace.on_show` is `ico_diag_note_progress`) (`port/fmv/movie.c`,
`readMpeg`; [PLATFORM.md](PLATFORM.md#diagnostics)).

The summary line names what happened:

    fmv: movie_proc played after <n> vsyncs: <n> pictures in the stream, <n> into the display
    ring, <n> shown, <n> decoded (<n> decoder errors), <n> PCM bytes sent, <n> movie presents
    (<n> failed)

(one line in the log; `aborted` in place of `played` after a skip). The presents count is the
[window build](ARCHITECTURE.md#terms)'s: fewer presents than shown pictures means the screen missed
some (`rd_video_presents`).

### The PSS demuxer

A PSS file is an MPEG-2 program stream whose video is one MPEG-2 elementary stream (stream id
0xE0) and whose audio is PCM in private stream 1 (0xBD) in Sony's own framing. On the PAL disc's
three PSS files: 16384-byte packs with MPEG-2 pack headers, a system header in the first pack,
padding packets, the end code last; video PES with PTS (and DTS) on the packets that start a
picture; audio PES whose every payload opens with `FF A0 00 00` (0xA0 + n is the sub-stream
libmpeg's `sceMpegAddStrCallback` selects), after which the first packet starts with a 40-byte
header ("SShd" and "SSbd" chunks: type, rate, channels, interleave, data size) and the rest is PCM
(`port/fmv/pss.h`, header comment, `IcoPssAudioHeader`).

`ico_pss_next` parses one item from a byte buffer: a pack, a video or audio packet (payload and
time stamps), something else, or the end code; 0 means more data is needed, -1 lost sync
(`ico_pss_resync` finds the next pack header). The MPEG-1 pack and PES forms are accepted too
(`port/fmv/pss.c`, header comment). `ico_pss_es_unit_end` cuts the video elementary stream into
access units: a picture with the sequence and GOP headers in front of it. Pure functions over byte
buffers: no I/O, no allocation (`port/fmv/pss.h`, header comment).

`movie.c` logs the audio header (`fmv: audio SShd type ..., ... Hz, ... channels, interleave ...,
... bytes`) and, when it is not 16-bit little-endian 48 kHz stereo in 512-byte blocks, says the
audio is sent as is, as on the PS2 (`port/fmv/movie.c` (`audio_put`)).

### The MPEG-2 decoder

`port/fmv/m2v.h` wraps Ittiam libmpeg2. It takes one access unit at a time and hands back decoded
pictures in display order as 4:2:0 planes, as the IPU's `sceMpegGetPicture` did before its colour
conversion, for any size up to 720 x 576 (`ICO_M2V_MAX_W`, `ICO_M2V_MAX_H`, the IPU's own limit
through `sceMpegGetPicture`) (`port/fmv/m2v.h`, header comment). The call sequence is the
library's own fuzzer's; its choices (`port/fmv/m2v.c`, header comment):

- deinterlace off: the IPU hands frame pictures out as they are coded;
- shared display buffers off: the library copies each output picture into planes `m2v.c` owns;
- one core, no threads (`ithread_single.c`);
- the interlace flags (`progressive_sequence`, `progressive_frame`, `top_field_first`, ...) read
  from the units by `ico_m2v_scan_unit`, since the library reports `progressive_frame` as 1
  whatever the stream says. Each picture's scan is paired with the picture that comes out through
  the time stamp the decoder carries with it (`IcoM2vFrame.scan`, `interlaced`).

`ico_m2v_decode` returns 1 with a picture, 0 when none comes out (an I or P picture is held until
the next reference), -1 for a unit that could not be decoded (counted; the decoder goes on);
`ico_m2v_flush` drains the held pictures at the end.

`port/fmv/ithread_single.c` replaces libmpeg2's pthread-based `common/ithread.c`, which is not
compiled (it would pull winpthreads into the Windows build): the decoder is created with one core,
so the library only takes and releases mutexes; mutexes and conditions are no-ops and creating a
thread fails (`port/fmv/ithread_single.c`, header comment).

The library is built from its generic C routines (its riscv function selectors pick the generic
ones and nothing else). On arm64 Android and Linux (`ICO_LIBMPEG2_NEON`) the library's NEON
assembly and armv8 selectors are compiled instead, as Android's own build of the library has them
(`port/fmv/CMakeLists.txt`). Its warnings are silenced, it builds with `-fwrapv`, and the
sanitizer builds turn off the shift check for it (it left-shifts negative coefficients).

### Field matching

`port/fmv/field_match.h` picks, per interlaced picture, whether its own field woven with the other
field of the previous or the next picture makes a less combed whole picture (inverse telecine).
The disc's movies carry no telecined film, so the player does not use it; it is built into
`fmv_fields_test` only, for that test's report (`port/fmv/field_match.h`, header comment).

## Pacing of movie pictures

`port/fmv/movie_pace.h` is the display timing of `mv_disp.c`'s `vblankHandler` and `mv_vobuf.c`'s
ring (`port/fmv/movie_pace.h`, header comment):

- The decoder fills a ring of `ICO_MOVIE_SLOTS` (5) decoded pictures as fast as it can, one per
  free slot (`ico_movie_pace_room`, `ico_movie_pace_put`).
- Once the display runs, each vblank looks at the oldest picture: on a field-0 vblank a fresh
  picture is shown (`ICO_PACE_SHOW`, its even field); on the next field-1 vblank its odd field goes
  out and its slot is freed (`ICO_PACE_FREE`). Every picture is on screen for exactly two vsyncs:
  25 pictures a second at 50 Hz (`ico_movie_pace_vblank`).
- The player stops when the decoder has put the last picture into the ring, so the pictures still
  in the ring are never shown, as on the PS2 (`ico_movie_pace_check_end`, ended 1).
- The pad is polled once 11 pictures are decoded (`ICO_MOVIE_POLL_AFTER`); an abort stops the
  movie at the next freed slot (ended 2).

Nothing here depends on whether the pictures are decoded or drawn, only on how many the stream
holds: a movie lasts the same number of vsyncs in the [window build](ARCHITECTURE.md#terms) and
the [headless build](ARCHITECTURE.md#terms). The headless build decodes the pictures and drops
them (`port/fmv/movie.c`, header comment).

## The picture on screen

The PS2 player bypassed the game's display: it set its own display environment (720 wide, all 576
or 480 interlaced lines) and sent each picture from the IPU straight into it. The port does the
same at the presenter's level: `port/render/rd_video.c` draws each picture straight to the output,
and the game's frames and its DISPLAY target are not touched (the DISPLAY target is the game's
reduced 512-wide frame, which would halve the picture) (`port/fmv/rd_video.h`,
`port/render/rd_video.c`, header comments). `movie.c` calls it only in the window build: the file
is compiled there with `ICO_RD` (`port/fmv/CMakeLists.txt`), and without either `ICO_RD` or
`ICO_HEADLESS` it does not compile.

- **The box**: the picture goes into the presenter's 4:3 box, pillarboxed in a wider window and
  letterboxed in a taller one, the rest black. The box is the PS2 display area
  (`rd_video_set_display`: 720 x 576 or 480); a w x h picture sits in it at ((dispW - w) / 2,
  (dispH - h) / 2), the offsets the PS2 player used, scaled with the area. A picture larger than
  the area on one side is fitted into the box on that side, not cut (`port/render/rd_video.c`,
  header comment, `drawPicture`). The box stays 4:3 whatever the display options.
- **Colour conversion**: one pass converts the decoder's 4:2:0 planes with the IPU's CSC in
  `yuv.hlsl`: ITU-R BT.601 limited range, coefficients in 1/64 units (149, 204, -104, -50 and 258, written as signed decimals in `yuv.hlsl`: the IPU's hex 0x95, 0xCC, -0x68, -0x32, 0x102 would be read as unsigned by FXC, the Direct3D 11 backend's compiler, and every negative chroma term would saturate, a purple movie), Y below 16 clamped, chroma
  taken per 2 x 2 luma block without interpolation, each channel rounded and clamped. The constants
  are PCSX2's IPU model (behaviour, not code). Scaling to the output is bilinear between converted
  samples, so at 1:1 the output is the IPU's value (`port/shaders/yuv.hlsl`, header comment).
- **Interlaced pictures**: the disc's films are interlaced frame pictures, top field first
  (`port/fmv/movie.c`, `mv.prev` comment). An interlaced picture is shown a field a vsync
  (`rd_video_field`, `RD_VIDEO_DEINTERLACE`): field 0 at the picture's even vblank, field 1 at the
  odd one, its own rows from the picture and the other rows filled by `yuv_field_ps` from the field
  and the pictures before and after it. The field pass converts at the picture's size into an
  RGBA8 target, which the box pass then scales. A progressive picture, or a field pass that cannot
  be set up, goes out whole (`rd_video_frame`) (`port/fmv/movie.c` (`show_field`);
  `port/render/rd_video.c`, header comment). The log says it once: `fmv: interlaced pictures (top
  field first): a field a vsync, deinterlaced`.
- **CRT filter**: under the CRT filter the picture is drawn into a target of the display area's
  size and then through the CRT pass into the same box, on the film's grid (the game's 512 triads,
  the film's field lines); clear frames take the same road (`port/render/rd_video.c`, header
  comment). [DISPLAY.md](DISPLAY.md#the-crt-filter) has the filter.
- **Mirror**: the films follow the renderer's mirror mode (`rd_set_mirror`, `RdSettings.mirror`)
  (`port/render/rd_video.c` (`rd__mirror_on`)).
- **Overlay**: the presentation overlay (the touch controls, the popups) is drawn over the film in
  its box (`port/render/rd_video.c` (`rd__overlay_collect_film`); `rd_video_test`, overlay).

Each call waits for its [frame slot](ARCHITECTURE.md#terms), records, submits and presents on its
own, as `rd_end_frame` does for a game frame, on the simulation thread between game frames; the game
draws no frame while a movie plays (`port/render/rd_video.c`, header comment). `rd_video_presents`
counts the pictures put on screen and the ones that could not be (no device, no swapchain image);
the window's 10 s statistics line and the `movie_proc` summary take differences, the only record in
a log that a movie reached the screen (`port/fmv/rd_video.h` (`rd_video_presents`)).

## Android

The movie path is the same on Android, with two build differences (`port/fmv/CMakeLists.txt`):

- `ico_libmpeg2` is built with the library's NEON routines on arm64 (`ICO_LIBMPEG2_NEON`).
- libmpeg2 calls AOSP's `android_errorWriteLog` under `__ANDROID__` (a security event log on
  malformed streams), from `<log/log.h>`, which the NDK does not have. `port/fmv/android/log/log.h`
  defines it as a no-op and is on `ico_libmpeg2`'s include path for Android only; the decoder
  rejects such a stream anyway (`port/fmv/android/log/log.h`).

## The pad during movies

The skip is the game's own: `movie_abort_check` (`ico2/common/src/main.c`) runs `ExecKeyInput`
and tests `pad[0].flags & 0x800` (Start) once a vsync from inside the player's busy loop, and the
pad manager thread (`ico2/fumi/ios/pad.c`, priority 17) is what reads
[libpad](ARCHITECTURE.md#terms). The busy loop gives
it the CPU through `next_vsync`'s rotate and spin. A skip is logged with its source:

    fmv: skip (START, <source>) at vsync <n>

where the source is `gamepad`, `keyboard`, `mouse`, `escape`, `touch` or a `+` combination, or
`pad script` while a [pad script](ARCHITECTURE.md#terms) drives the pad
(`port/input/pad_host.c`, `ico_input_start_source`; [INPUT.md](INPUT.md)). Every 60
vsyncs the player also logs what the pad delivered to the poll: `fmv: pad now 0x.... flags 0x....
(Start is 0x0800), start source ...` (`port/fmv/movie.c` (`readMpeg`)).

`port/platform/test/movie_pad_test.c` runs that path with the game's own code: `keyInput.c`
(`InitKeyInput`, `ExecKeyInput`), `pad.c` (the pad manager thread and `iosPadRead`),
`shockdriver.c`, the thread and message layer, and `port/input`'s libpad and bindings, on the
[fiber](ARCHITECTURE.md#terms) scheduler, with a priority-27 thread that spins to the next vsync and
polls as the movie loop does. The host side feeds the input layer as `input_sdl.c` does each vsync:
a gamepad's Start, then the key bound to Start, then a one-vsync tap of each. It asserts that the
Start edge reaches the poll within a vsync of the press for every source, exactly once per press,
that the held button shows in `pad[0].now`, and that `ico_input_start_source` names the source for
the skip's log line (`port/platform/test/movie_pad_test.c`, header comment).

## Tests

The ctests `fmv`, `fmv_fields`, `rd_video`, `rd_video_mali` and `rd_video_window`
(`port/fmv/test/`) cover the player's parts, the display and the window path;
[TESTING_PROGRAMS.md](TESTING_PROGRAMS.md#portfmvtest) describes each. `movie_pad`
(`port/platform/test/movie_pad_test.c`) is the pad test above. The `fmv*` and `rd_video*` tests are
registered in `port/fmv/CMakeLists.txt` (the `rd_video*` ones only with `ico_render`, and the
window test only with Vulkan and SDL3), `movie_pad` in `port/platform/CMakeLists.txt`.
