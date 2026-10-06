# Music gallery

Settings > Extras > Music (title only, docs/port/EXTRAS.md) lists everything
the game's sound system plays and plays any entry on demand, through the
game's own stream and effect engines, so what is heard is the game's own mix
at the Audio page's volumes. Nothing is shipped: the list is built at run
time from the game's tables, and the sounds come from the disc.

| file | what |
| --- | --- |
| `port/ui/gallery.h`, `gallery.c` | the list (groups, items, texts), the page's calls into the engine, a stream's time (`gallery_StreamSeconds`, the NAX clock), the bank header check (`gallery_HdHas`), the `ICO_GALLERY_PLAY` script |
| `port/ui/gallery_play.c` | the engine: the game's stream and effect calls, the bank loads and the title's restore (`ico_pc` only; installed by `host_loop.c`) |
| `port/ui/settings.c` | the page (`UI_PAGE_MUSIC`, a list page, `kGalDef`), its progress bar and transport, and the Extras hook `extrasMusic` |
| `port/ui/ui_hint.h`, `ui_hint.c` | the transport line: the game's button glyphs beside their words (UI.md, "Button glyphs") |
| `port/data/df_pack.h`, `df_pack.c` | one member of a DATA.DF stage pack read through the VFS |
| `ico2/fumi/sound/s_init.c` | `soundSeReqStop`, a port hook: stops every slot playing from a bank |
| `ico2/omori/src/fightSound.c` | `fightSoundHostHold`, a port hook: the fight music's step does nothing while the gallery is open |

## The list

Built once, the first time the page opens, from `adpcmFile`, `seFile`,
`seDef`, `seList`, `seEnv` and `stageData` as the table loader filled them
(docs/port/DATA.md). Each group opens with a heading, which the cursor skips
(`ui_list.h`); Back ends the list. On the PAL disc:

| group | entries | what |
| --- | --- | --- |
| Soundtrack | 51 | the streams of `adpcmFile` 1 to 100 of the score: `battle.int`, every `event/` stream (the scored scenes, the ending) and the title theme (56, `event2/50.int`, op.c's `titleAdpcm`) |
| Scene sounds | 45 | the other `event2/` streams: the machinery's stingers (the scripts' handles name them: gondolas, idols' doors, lifts, gates, bridges, chains, lightning) |
| Ambience | 67 | the stage environment sounds (`seEnv`) of stages 1 to 39, one entry per sound, from the bank that stage plays it from |
| Voice | 184 | Yorda's hint voices (streams 101 to 104) and the com_v bank's effects |
| Sound effects | 2649 | one headed group per bank (60 banks, deduplicated by their `.hd` file; the common banks and the banks the game's stages and cutscenes load, stages 1 to 56), the effects by their `seDef` names |

The streams whose files are not on the disc (the four `e3/` ones) are left
out. So is an effect whose `seList` row names a program or tone its bank's
header does not have: the sound library's `SgSePlay` refuses it
(`sce/libsndn2/sound.c`: the header's SE table at the offset in its word
0x1C, the last program, the program's entry, its last tone), so it could
never sound from that bank, in the game either. The engine reads each
bank's `.hd` from its pack when the list is built (`GalleryTables.seInBank`,
`gallery_HdHas`, about 0.2 s for all of them). That leaves 373 of the 3022
effects out, and 8 banks with none (their rows name programs their headers
lack; `st00a_obj` has a 256-byte body), so 2649 effects in 60 banks. A bank file that several `seFile` rows share (the `share_amb*` banks)
is one group; an effect plays from the first row that has its kind.

**Sequences.** The game's sound library can play sequenced music (a `.hd`,
`.bd` and `.sq` per stage, `soundHDDataSet`, `soundBDDataSet`,
`soundSQDataSet` on segment 1 in mode 1, `SgBgmOpen`, `SgBgmPlay`), but the
PAL game never does: `stageData[].seSegData1` and `seSegData2` are 0 in all
106 rows, no pack member is a `.sq`, and every `.hd` and `.bd` member of the
68 packs has kind 11 (an effect bank), none kind 10 (BGM). All of ICO's
music is streamed, so the gallery has no sequence group, and
`ICO_GALLERY_PLAY=seq:B` logs `gallery: failed seq B: the disc has no
sequenced music`.

The split is the disc's folders: `event2/` holds the stingers, `event/` and
`battle.int` the score, and the title theme is the title's music by op.c's
use of it. Nothing else decides it: an earlier album-title table also moved
stream 55 (`event2/00.int`, st06a's `toge`) to the soundtrack; with the
table gone it is a scene sound.

**Names.** Every entry is named by its asset: a stream by its file under
`sound/ICO_ADPCM/` without the `.int` (`event/39_8`, `event2/hint1_1`), an
effect by its `seDef` name. Column A is empty but for an ambience, which
shows its stage's key. Headings are drawn 16 units left of the entries.

**The page.** The list (8 slots from field line 38, 16 apart, at the list
pages' sizes: 24 for the entries, 21 for the column), then the status line
(168: the group, the asset (the stream's file, or the bank's file with the
effect's program and tone) and Playing, Paused or Stopped), the progress
bar and the transport. Every word is a port row drawn like the Settings
rows (the game's lettering with its dark rim, deferred at the output's
resolution in Enhanced).

**The progress bar** (188, x 150 to 490) is three rect rows of the layout
extension (UI.md, "Layout extension"): a dark rim, the track, and the fill
in the letters' colour, with the elapsed time at its left and the total at
its right (m:ss). It shows the item sounding or paused, whatever the cursor
is on, and is empty (0:00, 0:00) when nothing sounds.

**A stream's time** (`gallery.h`, "A stream's time"). An `.int` is
`channels` channels of SPU ADPCM interleaved by sector (0x800 / channels
bytes of each), 16 bytes for 28 samples a channel, at `adpcmFile.pitch` Hz,
so

    seconds = bytes / channels / 16 * 28 / pitch

One pass is the table's `sectors * 2048` bytes, which is the total. Every
`.int` on the disc is exactly that plus 0x5C000 bytes (the IOP ring's size:
the reader's last read of a pass runs into it), checked for all 100 by the
`gallery` test, so the total is the file's own. The elapsed time is what the
stream's voice has played: the page follows the voice's NAX (its next
address in its 16 KB SPU ring, `spu2_sd_get_addr`) once a Main tick from the
key-on (the first IOP read, when `remain` first moves) and adds each move,
across the ring's wrap (`gallery_ClockStep`; a Main tick is about 1 KB at
44.1 kHz). A pause sets the voice's pitch to 0, so NAX stands and the time
with it. A move back of more than half the ring counts nothing (a voice
looping one block). Before this the time was `dataSize - remain` less 1.5
ring halves: the reader's progress, not the voice's, ahead by 0.3 to 0.6 s.

An effect's total is its sample: the voices that start sounding after the
request inside the bank's SPU buffer, each read in sound RAM from its start
address (SSA) to the 16-byte block with the end flag, at the voice's pitch;
its elapsed time is counted from the request on the SPU2's clock. A sample
whose end block loops (an ambience, a machine's hum) is stopped when its
length has played (one pass, below; the loop restarts at its loop point,
not its start, so the length is the sample's, not the loop's), within the
Main tick the page polls on: the bar never wraps. Until the effect's voice is
found the total shows `-:--`.

**Seeking** is not offered. Restarting a stream at an offset is not a
fill from a chosen IOP offset: the background reader has already filled the
IOP ring ahead from the file, so a seek would need the ring flushed, the
reader re-seeked (`iosCdvdBackGroundMgrSeek`) and the SPU ring refilled
before the voices key on again, which is the whole open path; the bar is a
display only, and Left and Right keep jumping between the groups.

**Buttons** (the transport, each the game's own glyph beside its word):
L1 previous, Cross play or pause, Square stop, R1 next, Left / Right
section, Triangle back. Cross on the entry sounding pauses it (the word
reads Pause while the cursor is on it), on the entry paused resumes it, on
any other entry plays it (stopping what plays). A stream pauses as the
pause menu pauses the game's streams, `adpcmPauseRequest(1)` (every stream
voice's pitch 0, the reader's accounting held); an effect cannot pause, so
Cross stops it and holds it: the status reads Paused and the next Cross
plays it again from its start. L1 and R1 move the cursor to the previous or
next entry that plays (past the headings and Back, wrapping) and play it.
Square stops, Left and Right jump to the previous or next group (its
heading at the top of the window), Triangle or Circle and Back leave.

## Playback

Everything runs on the simulation thread, from the page's layout proc
(`settingsProc` calls `gallery_Tick` once a Main tick).

**Streams** go through the game's request queue: `scpAdpcmPlayRequestFunc(no,
&handle, 1, 2, 1)`, which the script daemon's thread (`scpSubAdpcmPlay`,
running on the title) opens with `soundDataOpen` and `AdpcmPlay`, as every
scene's stream. Its voices are tagged music, or effects for 101 to 104, by
`adpcmDataSet` (AUDIO.md, "Gains and output mode"). The game has two stream
records and two IOP rings, and the title theme (op.c `actTitleShortCut`,
stream 56 in `titleAdpcm`, looping) holds one, so the first play fades the
theme with `scpAdpcmFadeCloseFunc(&titleAdpcm, 1024)` (op.c's step when the
demo leaves the title) and requests the stream when the theme has closed.
Then `titleAdpcm` is cleared, as op.c does after its own fade:
`actTitleShortCut` waits while it is set, and op.c's demo step fades
whatever it points to. On leaving, a theme that was playing is requested
again, from its start.

**One pass.** Every piece plays once, from its start to its end, and
stops; no stream loops, `loopStart` or not, and no effect: a looping
effect sample is stopped after its length (`effectTick`, logged as
`gallery: effect N stopped after one pass of its looping sample`). The engine's own end is early for
this: with `loopNum` N, `adpcmTickProc2` closes the stream when the reader's
count of consumed bytes (`remain`, moved by the IOP read offset) has gone
round N times, and the reads run ahead of the voice by what the SPU ring
holds, so a scene's stream is cut 0.5 to 0.9 s before its end (1.6 s for
`event/10` at 18 kHz), which a scene covers with its fade but a gallery
cannot. So the page requests two passes (`loopNum` 2: the engine never
reaches its own close) and closes the stream itself, with
`scpAdpcmCloseFunc`, when the voice has played the pass (the NAX clock
above, within half a Main tick: 95 streams of the sweep closed within 992
bytes, 20 ms, of their end) and logs `gallery: stream N closed at its end`.
The previous stream is closed (`AdpcmClose`, synchronous) before the next
is requested, and the next is requested only when the request queue is
empty and an IOP ring is free. A stream that does not open is logged
(`gallery: failed stream N: ...`, with why: the title theme did not close,
the queue stayed busy, no ring came free, or the daemon gave no handle) and
stops.

**battle.int** (stream 1, 1:22) is the fight music, and the fight music's
step (`omori/src/fightSound.c`, `fightSoundProcess`, every Main tick) looks
the stream up by its number whoever opened it: with no fight on (the boy
not in status 17, the girl not held) it fades the volume from 0 in steps of
96, finds 0 at once and closes it. So the gallery's battle.int was closed
the tick after it opened. While the gallery is open the step does nothing
(`fightSoundHostHold`, set by the engine's enter, cleared by its leave; no
fight can be on at the title).

**event/40.int** (stream 50, `adpcmFile` 3:12) is blank on the PAL disc
from byte 0x93000 (11.9 s in) to the end of its pass: 4,438 of its 4,732
sectors are 0xFF bytes (its ring pad is not), on the image the port accepts (SHA-1 `1017b53f...`,
DATA.md). An ADPCM block of 0xFF bytes has the end, repeat and loop-start
flags, so the voice loops that one block, the driver's fills stop and the
time stood at 11.9 s. No script requests stream 50. The engine looks once
per stream, at its first play, for a blank tail: the pass's last sector is
read, and when it holds a block with the end flag the first such sector is
found by halving (`gallery_StreamBlankFrom`, about a dozen sector reads, not
the file; only event/40 has one inside its pass, and the `gallery` test
checks that the halving finds what a scan of every stream's whole pass
finds), logs `gallery: stream 50 (...): the disc's file has an
end block at byte 0x93000 (blank from there): 11.9 s of its 192.3 s play`,
and plays and shows that much: its total is 0:11 and it ends there.

**Effects, ambiences and voices** play with `soundSeDefPlay(def, 0xFFFFFFFF,
NULL, 0)`: no position, so no distance attenuation or panning (a stage
ambience's own proc and placement do not run either), and the slot is the
game's, tagged effects by the sequencer's volume packet. Before the call
`seKind[kind]` is pointed at the bank's own row, so the effect is the bank's
version whatever else is loaded; `soundSeKindBuild` on leaving restores the
game's mapping. The title theme is faded first here too.

**Banks.** A bank already in sound RAM (the common banks, or what the title
holds) plays at once. Any other is read from a stage pack on the disc
(`df_pack.c`: DATA.DF's directory, the pack's raw deflate stream through
miniz's tinfl, the member at its offset) and loaded as the pack loader does:
the body to sound RAM with `soundBDDataSet`, then the header with
`soundHDDataSet`. On the title, sound RAM holds the six common banks
(segment 0, from 0x5010 to 0x154A10) and the logo stage's `st27a_sys`
(segment 2, 407,552 bytes), leaving 134,672 bytes below segment 1's top
(0x1D9020), too little for most stage banks. So the first load stops every
slot playing from the banks of segments 1 and 2 (`soundSeReqStop`), closes
them (`soundDataSegAllClose`), and the gallery's bank takes segment 1, where
542,224 bytes are then free; the largest bank, `st13b_obj.bd4`, is 507,264
bytes, so every bank fits. One gallery bank is resident at a time: the next
load closes it first. On leaving, the gallery's bank is closed and the
closed banks are read back into their segments in their allocation order
(segment 1 from its top down, segment 2 up), with the headers the game left
in memory, each at its old address (a different address is logged as
`gallery: failed`).

**Limits.** No sequenced music (above). Effects play without position, pan,
distance or their environment's procs; a looping sample plays once. A stream
restarts from the beginning; the title theme restarts from its start on
leaving. A stall of the simulation thread longer than a ring (0.37 s at
44.1 kHz) would miscount the NAX clock; the menu has none. Opening the
gallery from the pause menu is not possible (Extras is a title-only page).

## Findings: the sweep (package MUS3)

A player reported pieces that loop or show wrong times, and pieces of 1:22
that do not play. `gallery_sweep` (below) played every stream once through
the gallery, and three effects of every bank, in one headless run, before
and after the fixes above. Before (engine of `7188bff6`):

- **battle.int (1:22) did not play**: the fight music's step closed it the
  tick after it opened (Playback, "battle.int"). The other 1:22 pieces,
  `event/03` and `event/32_2`, played.
- **Every other stream ended early**, 0.5 to 0.9 s before its total (1.6 s
  for `event/10` at 18 kHz): the engine's `loopNum` close comes when the
  reads, not the voice, reach the end (Playback, "One pass"); the bar never
  reached the total.
- **event/40 stood still** at 11.9 s of 3:12 with its voice looping one
  block: the disc's file is blank from there (Playback, "event/40.int").
  The bar crawled 0.12 s a second.
- **62 of the 204 effect plays were refused** (`soundSeDefPlay` returned
  -1): their rows' programs or tones are not in their banks' headers
  (The list).
- No stream wrapped or played past its end, and none with a `loopStart`
  looped: `loopNum` 1 closed every one at its first end. The streams that
  showed silence in the first 4 s (`event/30`, `event/41`, `event2/54`) open
  quietly and sound later in the dump.

After: every listed stream plays once from its start and ends with the bar
at its total (within 0.1 s, the log's rounding; the voice within 20 ms of
the last byte), battle.int included; event/40 plays its 11.9 s and ends
there; no wrap; every one of the 180 effects sampled from the 60 banks is
keyed (44 of them loop and were cut at the 10 s dwell; since then a
looping effect stops after one pass, "One pass", not yet re-swept); R1 and L1 start the
next and previous entry while a stream plays; the title's banks and theme
come back on leaving. The "before" ends are the time the page showed then
(`dataSize - remain` less 1.5 ring halves); the "after" ends are the voice's
(the NAX clock). Streams 2 to 5 (`e3/`) are not on the disc and not listed.
Pieces longer than the 95 s dwell are cut there; their position moved one
second a second to the cut.

| stream | file | listed total | before: end (played, looped) | before: problem | after: end (played, looped) |
| --- | --- | --- | --- | --- | --- |
| 1 | battle.int (loop start 114) | 1:22 (82.6 s) | 0.0 s end (NO, no) | closed by the fight music's step the tick after it opened | 82.6 s end (yes, no) |
| 6 | event/01.int | 1:18 (78.6 s) | 77.9 s end (yes, no) | ended 0.7 s early | 78.5 s end (yes, no) |
| 7 | event/02_1a.int | 0:45 (45.3 s) | 44.7 s end (yes, no) | ended 0.6 s early | 45.3 s end (yes, no) |
| 8 | event/02_1b.int | 0:52 (52.5 s) | 51.9 s end (yes, no) | ended 0.6 s early | 52.5 s end (yes, no) |
| 9 | event/02_1c.int | 0:39 (39.7 s) | 38.8 s end (yes, no) | ended 0.9 s early | 39.6 s end (yes, no) |
| 10 | event/02_1d.int | 1:04 (64.3 s) | 63.6 s end (yes, no) | ended 0.7 s early | 64.2 s end (yes, no) |
| 11 | event/02_2.int | 1:55 (115.8 s) | 94.8 s cut (yes, no) | - | 94.9 s cut (yes, no) |
| 12 | event/03.int | 1:22 (82.6 s) | 81.8 s end (yes, no) | ended 0.8 s early | 82.5 s end (yes, no) |
| 13 | event/04.int | 0:28 (28.0 s) | 27.5 s end (yes, no) | ended 0.5 s early | 28.0 s end (yes, no) |
| 14 | event/05.int | 0:12 (12.9 s) | 12.2 s end (yes, no) | ended 0.7 s early | 12.9 s end (yes, no) |
| 15 | event/06.int | 1:15 (75.9 s) | 75.3 s end (yes, no) | ended 0.6 s early | 75.9 s end (yes, no) |
| 16 | event/07.int | 0:14 (14.1 s) | 13.5 s end (yes, no) | ended 0.6 s early | 14.1 s end (yes, no) |
| 17 | event/08.int | 0:09 (9.9 s) | 9.3 s end (yes, no) | ended 0.6 s early | 9.9 s end (yes, no) |
| 18 | event/09.int | 0:10 (10.4 s) | 9.6 s end (yes, no) | ended 0.8 s early | 10.4 s end (yes, no) |
| 19 | event/09_2.int | 0:10 (10.4 s) | 9.6 s end (yes, no) | ended 0.8 s early | 10.4 s end (yes, no) |
| 20 | event/10.int | 0:05 (5.2 s) | 3.6 s end (yes, no) | ended 1.6 s early | 5.1 s end (yes, no) |
| 21 | event/11.int | 0:16 (16.5 s) | 15.8 s end (yes, no) | ended 0.7 s early | 16.4 s end (yes, no) |
| 22 | event/12.int (loop start 330) | 0:54 (54.6 s) | 53.9 s end (yes, no) | ended 0.7 s early | 54.6 s end (yes, no) |
| 23 | event/13.int | 2:06 (126.4 s) | 94.8 s cut (yes, no) | - | 95.0 s cut (yes, no) |
| 24 | event/16.int | 0:10 (10.8 s) | 10.2 s end (yes, no) | ended 0.6 s early | 10.8 s end (yes, no) |
| 25 | event/17.int | 0:12 (12.0 s) | 11.2 s end (yes, no) | ended 0.8 s early | 12.0 s end (yes, no) |
| 26 | event/17a.int | 0:12 (12.1 s) | 11.5 s end (yes, no) | ended 0.6 s early | 12.1 s end (yes, no) |
| 27 | event/18.int | 0:13 (13.1 s) | 12.5 s end (yes, no) | ended 0.6 s early | 13.1 s end (yes, no) |
| 28 | event/18a.int | 0:15 (15.8 s) | 15.1 s end (yes, no) | ended 0.7 s early | 15.8 s end (yes, no) |
| 29 | event/19.int | 0:26 (26.7 s) | 26.2 s end (yes, no) | ended 0.5 s early | 26.7 s end (yes, no) |
| 30 | event/20.int | 0:26 (26.8 s) | 26.2 s end (yes, no) | ended 0.6 s early | 26.8 s end (yes, no) |
| 31 | event/24_1.int | 1:20 (80.6 s) | 79.8 s end (yes, no) | ended 0.8 s early | 80.6 s end (yes, no) |
| 32 | event/24_2.int (loop start 700) | 1:03 (63.3 s) | 62.6 s end (yes, no) | ended 0.7 s early | 63.3 s end (yes, no) |
| 33 | event/24_3.int | 0:49 (49.8 s) | 49.2 s end (yes, no) | ended 0.6 s early | 49.8 s end (yes, no) |
| 34 | event/26.int | 0:17 (17.7 s) | 17.1 s end (yes, no) | ended 0.6 s early | 17.7 s end (yes, no) |
| 35 | event/27.int | 0:11 (11.9 s) | 11.2 s end (yes, no) | ended 0.7 s early | 11.8 s end (yes, no) |
| 36 | event/29.int | 0:12 (12.5 s) | 11.9 s end (yes, no) | ended 0.6 s early | 12.5 s end (yes, no) |
| 37 | event/29a.int (loop start 154) | 1:16 (76.0 s) | 75.3 s end (yes, no) | ended 0.7 s early | 76.0 s end (yes, no) |
| 38 | event/30.int | 0:21 (21.0 s) | 20.3 s end (yes, no) | ended 0.7 s early | 21.0 s end (yes, no) |
| 39 | event/32_1.int | 0:10 (10.5 s) | 9.9 s end (yes, no) | ended 0.6 s early | 10.4 s end (yes, no) |
| 40 | event/32_2.int | 1:22 (82.2 s) | 81.4 s end (yes, no) | ended 0.8 s early | 82.2 s end (yes, no) |
| 41 | event/33.int (loop start 114) | 1:31 (91.9 s) | 91.2 s end (yes, no) | ended 0.7 s early | 91.8 s end (yes, no) |
| 42 | event/38.int | 0:51 (51.3 s) | 50.5 s end (yes, no) | ended 0.8 s early | 51.2 s end (yes, no) |
| 43 | event/39_1.int | 0:57 (57.8 s) | 57.1 s end (yes, no) | ended 0.7 s early | 57.8 s end (yes, no) |
| 44 | event/39_2.int | 0:53 (53.8 s) | 53.2 s end (yes, no) | ended 0.6 s early | 53.8 s end (yes, no) |
| 45 | event/39_3.int | 0:15 (15.4 s) | 14.8 s end (yes, no) | ended 0.6 s early | 15.3 s end (yes, no) |
| 46 | event/39_7.int | 0:42 (42.9 s) | 42.1 s end (yes, no) | ended 0.8 s early | 42.9 s end (yes, no) |
| 47 | event/39_8.int | 4:25 (265.6 s) | 94.8 s cut (yes, no) | - | 95.1 s cut (yes, no) |
| 48 | event/39_9.int | 0:04 (4.4 s) | 3.7 s end (yes, no) | ended 0.7 s early | 4.3 s end (yes, no) |
| 49 | event/39_10.int | 1:57 (117.2 s) | 94.8 s cut (yes, no) | - | 95.1 s cut (yes, no) |
| 50 | event/40.int | 3:12 (192.3 s) | 11.9 s cut (yes, no) | the voice stuck on the disc's blank from 11.9 s; the time stood | 11.9 s end (its blank: total 0:11) (yes, no) |
| 51 | event/41.int | 0:25 (25.1 s) | 24.5 s end (yes, no) | ended 0.6 s early | 25.1 s end (yes, no) |
| 52 | event/37.int (loop start 44) | 0:37 (37.7 s) | 36.9 s end (yes, no) | ended 0.8 s early | 37.7 s end (yes, no) |
| 53 | event/42.int | 0:09 (9.3 s) | 8.6 s end (yes, no) | ended 0.7 s early | 9.3 s end (yes, no) |
| 54 | event/43.int | 0:11 (11.4 s) | 10.6 s end (yes, no) | ended 0.8 s early | 11.3 s end (yes, no) |
| 55 | event2/00.int | 0:17 (17.9 s) | 17.1 s end (yes, no) | ended 0.8 s early | 17.8 s end (yes, no) |
| 56 | event2/50.int (loop start 122) | 0:52 (52.6 s) | 51.9 s end (yes, no) | ended 0.7 s early | 52.6 s end (yes, no) |
| 57 | event2/51.int | 0:19 (19.0 s) | 18.4 s end (yes, no) | ended 0.6 s early | 19.0 s end (yes, no) |
| 58 | event2/52.int | 0:19 (19.4 s) | 18.7 s end (yes, no) | ended 0.7 s early | 19.4 s end (yes, no) |
| 59 | event2/53.int | 0:16 (16.3 s) | 15.8 s end (yes, no) | ended 0.5 s early | 16.3 s end (yes, no) |
| 60 | event2/54.int | 0:13 (13.2 s) | 12.5 s end (yes, no) | ended 0.7 s early | 13.1 s end (yes, no) |
| 61 | event2/55.int | 0:18 (18.5 s) | 17.7 s end (yes, no) | ended 0.8 s early | 18.5 s end (yes, no) |
| 62 | event2/56.int | 0:07 (7.6 s) | 7.0 s end (yes, no) | ended 0.6 s early | 7.5 s end (yes, no) |
| 63 | event2/57.int | 0:05 (5.0 s) | 4.4 s end (yes, no) | ended 0.6 s early | 5.0 s end (yes, no) |
| 64 | event2/58_1.int | 0:08 (8.5 s) | 7.6 s end (yes, no) | ended 0.9 s early | 8.4 s end (yes, no) |
| 65 | event2/58_2.int | 0:07 (7.5 s) | 7.0 s end (yes, no) | ended 0.5 s early | 7.5 s end (yes, no) |
| 66 | event2/59_1.int | 0:09 (9.0 s) | 8.3 s end (yes, no) | ended 0.7 s early | 9.0 s end (yes, no) |
| 67 | event2/59_2.int | 0:08 (8.9 s) | 8.3 s end (yes, no) | ended 0.6 s early | 8.9 s end (yes, no) |
| 68 | event2/60.int | 0:09 (9.3 s) | 8.6 s end (yes, no) | ended 0.7 s early | 9.3 s end (yes, no) |
| 69 | event2/61.int | 0:20 (20.3 s) | 19.7 s end (yes, no) | ended 0.6 s early | 20.3 s end (yes, no) |
| 70 | event2/62.int | 0:20 (20.2 s) | 19.3 s end (yes, no) | ended 0.9 s early | 20.1 s end (yes, no) |
| 71 | event2/63.int | 0:07 (7.7 s) | 7.0 s end (yes, no) | ended 0.7 s early | 7.7 s end (yes, no) |
| 72 | event2/64.int | 0:13 (13.6 s) | 12.8 s end (yes, no) | ended 0.8 s early | 13.5 s end (yes, no) |
| 73 | event2/65.int | 0:06 (6.6 s) | 6.0 s end (yes, no) | ended 0.6 s early | 6.6 s end (yes, no) |
| 74 | event2/66.int | 0:06 (6.8 s) | 6.0 s end (yes, no) | ended 0.8 s early | 6.8 s end (yes, no) |
| 75 | event2/67.int | 0:06 (6.7 s) | 6.0 s end (yes, no) | ended 0.7 s early | 6.7 s end (yes, no) |
| 76 | event2/68.int | 0:09 (9.8 s) | 9.3 s end (yes, no) | ended 0.5 s early | 9.8 s end (yes, no) |
| 77 | event2/69_1A.int | 0:15 (15.8 s) | 15.1 s end (yes, no) | ended 0.7 s early | 15.7 s end (yes, no) |
| 78 | event2/69_1B.int | 0:10 (10.6 s) | 9.9 s end (yes, no) | ended 0.7 s early | 10.6 s end (yes, no) |
| 79 | event2/69_2A.int | 0:10 (10.2 s) | 9.6 s end (yes, no) | ended 0.6 s early | 10.1 s end (yes, no) |
| 80 | event2/69_2B.int | 0:17 (17.0 s) | 16.4 s end (yes, no) | ended 0.6 s early | 16.9 s end (yes, no) |
| 81 | event2/70.int | 0:11 (11.2 s) | 10.6 s end (yes, no) | ended 0.6 s early | 11.2 s end (yes, no) |
| 82 | event2/71.int | 0:05 (5.5 s) | 4.7 s end (yes, no) | ended 0.8 s early | 5.5 s end (yes, no) |
| 83 | event2/72.int | 0:06 (6.3 s) | 5.7 s end (yes, no) | ended 0.6 s early | 6.3 s end (yes, no) |
| 84 | event2/73.int | 0:06 (6.7 s) | 6.0 s end (yes, no) | ended 0.7 s early | 6.6 s end (yes, no) |
| 85 | event2/74.int | 0:05 (5.0 s) | 4.4 s end (yes, no) | ended 0.6 s early | 5.0 s end (yes, no) |
| 86 | event2/75.int | 0:06 (6.4 s) | 5.7 s end (yes, no) | ended 0.7 s early | 6.4 s end (yes, no) |
| 87 | event2/76.int | 0:07 (7.9 s) | 7.3 s end (yes, no) | ended 0.6 s early | 7.8 s end (yes, no) |
| 88 | event2/77_1.int | 0:08 (8.4 s) | 7.6 s end (yes, no) | ended 0.8 s early | 8.3 s end (yes, no) |
| 89 | event2/77_2.int | 0:08 (8.1 s) | 7.3 s end (yes, no) | ended 0.8 s early | 8.1 s end (yes, no) |
| 90 | event2/78.int | 0:08 (8.9 s) | 8.3 s end (yes, no) | ended 0.6 s early | 8.9 s end (yes, no) |
| 91 | event2/79.int | 0:05 (5.0 s) | 4.4 s end (yes, no) | ended 0.6 s early | 4.9 s end (yes, no) |
| 92 | event2/80.int | 0:05 (5.1 s) | 4.4 s end (yes, no) | ended 0.7 s early | 5.1 s end (yes, no) |
| 93 | event2/81.int | 0:06 (6.0 s) | 5.4 s end (yes, no) | ended 0.6 s early | 6.0 s end (yes, no) |
| 94 | event2/82.int (loop start 46) | 0:41 (41.7 s) | 41.2 s end (yes, no) | ended 0.5 s early | 41.7 s end (yes, no) |
| 95 | event2/83.int (loop start 846) | 1:32 (92.0 s) | 91.2 s end (yes, no) | ended 0.8 s early | 92.0 s end (yes, no) |
| 96 | event2/84.int | 0:03 (3.7 s) | 3.1 s end (yes, no) | ended 0.6 s early | 3.6 s end (yes, no) |
| 97 | event2/85.int | 0:09 (9.1 s) | 8.3 s end (yes, no) | ended 0.8 s early | 9.1 s end (yes, no) |
| 98 | event2/86.int | 0:08 (8.9 s) | 8.3 s end (yes, no) | ended 0.6 s early | 8.9 s end (yes, no) |
| 99 | event2/87.int | 0:05 (5.9 s) | 5.0 s end (yes, no) | ended 0.9 s early | 5.8 s end (yes, no) |
| 100 | event2/88.int | 0:05 (5.4 s) | 4.7 s end (yes, no) | ended 0.7 s early | 5.4 s end (yes, no) |
| 101 | event2/hint1_1.int | 0:01 (1.5 s) | 0.8 s end (yes, no) | ended 0.7 s early | 1.5 s end (yes, no) |
| 102 | event2/hint1_2.int | 0:02 (2.5 s) | 1.8 s end (yes, no) | ended 0.7 s early | 2.5 s end (yes, no) |
| 103 | event2/hint2_1.int | 0:01 (1.9 s) | 1.1 s end (yes, no) | ended 0.8 s early | 1.8 s end (yes, no) |
| 104 | event2/hint2_2.int | 0:01 (1.6 s) | 0.8 s end (yes, no) | ended 0.8 s early | 1.6 s end (yes, no) |

## Testing

`ICO_GALLERY_PLAY` (environment, developer only; the player never needs it)
is a comma-separated list of `kind:value` entries the page plays when it
opens, one every 200 Main ticks (8 s) from 50 ticks after it opens:
`stream:N` (an `adpcmFile` row), `env:I` (the ambience of `seEnv` row I),
`se:D` (the `seDef` row D, in the Voice group, else the first bank that has
it), `bank:K.J` (the K-th bank section of the sound effects: its first (J
0), middle (1) or last (2) effect), `pause:0` (Cross on the entry sounding
or paused: `gallery: paused`, `gallery: resumed` or, for an effect,
`gallery: stopped ... (held)`), `seq:B` (logs the failure above),
`dwell:S` (from here each entry lasts until it has ended, then a second,
or S seconds, when it is stopped and `gallery: stream N cut at E s of T s
(dwell S s)` logged; `dwell:0` goes back to the 8 s steps) and `leave:0`
(the page leaves, as on Triangle). While an entry sounds the page logs where
it is every 50 Main ticks (every second after a `dwell`): `gallery: stream
47 at 7.6 s of 265.6 s`; when it ends by itself, `gallery: stream N ended at
E s of T s`; when its time goes back, `gallery: stream N wrapped at A s to B
s of T s`. Each play logs `gallery: playing <group> <key> (<label>)`; a
stream's play then `gallery: stream N file <path>: S sectors (B bytes, T s),
loop start L, H Hz, C channels; the disc's file D bytes`, and the engine's
line: `gallery: stream N (...) opened on SPU voices A and B at audio frame
F` or `gallery: effect D from bank B (program P, tone T) keyed at audio
frame F` (F is `spu2_time()`, the WAV dump's frame). After the last,
`gallery: script done`. A failure is a `gallery: failed ...` line.

- `gallery` (CPU; the base ELF, and the disc image when present; 77 without
  the ELF): the list from the real tables: keys in range, groups in order
  with one heading each, no key twice in a group, every stream named by its
  file without the folder and `.int` with no column and in the group its
  folder gives (47 is `event/39_8`, the title theme in the soundtrack, 55 a
  scene sound), L1 and R1 (`gallery_Step`: over a heading, wrapping at the
  ends), every label, column, asset and gallery string in the five
  languages drawn by the font (`ui_FontHasGlyph`), Left and Right; a
  stream's time on synthetic records (the seconds of battle.int's 2032
  sectors at 44068 Hz and of a mono 18 kHz stream, the NAX clock over the
  ring's wrap, standing while paused, not counting a voice looping one
  block, the end within half a step), `gallery_StreamEndBlock` and
  `gallery_HdHas` on a made-up header; with the disc, the streams not on it
  left out, every listed bank found in a pack, every listed effect's
  program and tone in its bank's header, every stream's file its pass plus
  0x5C000 bytes, and no end block inside a pass but event/40's at 0x93000.
- `gallery_headless` (the headless build and the disc image; RUN_SERIAL,
  300 s, 77 without the image): `port/ui/test/gallery_headless.py` boots to
  the title with `pad-boot.txt`'s presses, opens Settings > Extras > Music
  and plays one entry of each group (stream 47, stream 87, the ambience of
  `seEnv` 61, stream 101, effect 130 of com_v, effect 1167 of `st25a_b-g`,
  which needs the bank load), then presses Triangle. It checks each
  `gallery: playing` line, the engine's line after it, that the WAV dump
  sounds within 4 s of each start, the theme's fade and restore, the banks'
  restore, and no `gallery: failed`.
- `gallery_sweep` (the headless build and the disc image; RUN_SERIAL,
  3600 s, 77 without the image; about three minutes, the headless game
  running faster than real time): `port/ui/test/gallery_sweep.py` boots as
  `gallery_headless` does and plays `stream:47` (R1 at 3 s and L1 at 6 s:
  the next and previous entries play), `dwell:95`, `stream:1` to
  `stream:104`, `dwell:10`, `bank:K.J` for 68 sections and J 0 to 2 (the
  sections past the last answer "no such entry"), `leave:0`, and stops the
  run 10 s after the page has left. From the log and the WAV dump it prints
  the two tables of "Findings" (into `port/ui/gallery_sweep/table.md` of
  the build directory) and fails when a listed stream does not open, is
  silent over its play, is not closed by the gallery at its end, ends more
  than 0.35 s before its total, goes past it or back, shows a total other
  than its file's (or its blank's), or, cut at the dwell, did not move one
  second a second; when an effect is not keyed; when R1 or L1 opens nothing;
  when the e3/ streams are listed; when the theme and banks are not
  restored; or on any other `gallery: failed`. `--reread` judges a finished
  run's folder again, and `--play LIST` (no checks) runs some entries only.
- `settings` and `settings_render`: the Music row opens the page and
  Triangle comes back to it; the page over a fake engine (rows, the heading
  skip, the transport's words each after its glyph row, left to right, the
  glyph centred on its word's capitals; the bar empty and 0:00 with nothing
  playing, at 42.4 of 265.6 s with 0:42 and 4:25 while a stream plays;
  Cross play, pause (Paused, the bar kept) and resume, R1 and L1, Square,
  Left, Right, leaving); `settings_music_4x.png`, and
  `settings_music_1080.png` / `_classic.png` (a stream playing, Enhanced
  1920 x 1080).
