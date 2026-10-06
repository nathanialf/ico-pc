# Music gallery

Settings > Extras > Music (title only, docs/port/EXTRAS.md) lists everything
the game's sound system plays and plays any entry on demand, through the
game's own stream and effect engines, so what is heard is the game's own mix
at the Audio page's volumes. Nothing is shipped: the list is built at run
time from the game's tables, and the sounds come from the disc.

| file | what |
| --- | --- |
| `port/ui/gallery.h`, `gallery.c` | the list (groups, items, texts), the page's calls into the engine, the `ICO_GALLERY_PLAY` script |
| `port/ui/gallery_play.c` | the engine: the game's stream and effect calls, the bank loads and the title's restore (`ico_pc` only; installed by `host_loop.c`) |
| `port/ui/settings.c` | the page (`UI_PAGE_MUSIC`, a list page, `kGalDef`), its progress bar and transport, and the Extras hook `extrasMusic` |
| `port/ui/ui_hint.h`, `ui_hint.c` | the transport line: the game's button glyphs beside their words (UI.md, "Button glyphs") |
| `port/data/df_pack.h`, `df_pack.c` | one member of a DATA.DF stage pack read through the VFS |
| `ico2/fumi/sound/s_init.c` | `soundSeReqStop`, a port hook: stops every slot playing from a bank |

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
| Sound effects | 3022 | one headed group per bank (68 banks, deduplicated by their `.hd` file; the common banks and the banks the game's stages and cutscenes load, stages 1 to 56), the effects by their `seDef` names |

The streams whose files are not on the disc (the four `e3/` ones) are left
out. A bank file that several `seFile` rows share (the `share_amb*` banks)
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
rows (the light letters with the dark rim, deferred at the output's
resolution in Enhanced, quads under classic menu text).

**The progress bar** (188, x 150 to 490) is three rect rows of the layout
extension (UI.md, "Layout extension"): a dark rim, the track, and the fill
in the letters' colour, with the elapsed time at its left and the total at
its right (m:ss). It shows the item sounding or paused, whatever the cursor
is on, and is empty (0:00, 0:00) when nothing sounds. A stream's position is
what its record has consumed: `AdpcmStream.dataSize - remain`
(`adpcmTickProc` moves `remain` on by the IOP read offset's progress,
`stream.c`'s `read_off`, which the driver moves on by half the 16 KB SPU
ring per channel per fill), less 1.5 halves of the ring, which the SPU holds
ahead of the voice on average, so it is within about a sixth of a second.
Seconds are bytes / channels / 16 * 28 / the stream's rate (`adpcmFile`
`pitch`, Hz; the total from `sectors * 2048`). An effect's total is its
sample: the voices that start sounding after the request inside the bank's
SPU buffer, each read in sound RAM from its start address (SSA) to the
16-byte block with the end flag, at the voice's pitch; its elapsed time is
counted from the request on the SPU2's clock. A sample whose end block
loops repeats, and the bar then starts again each loop length (the loop
restarts at its loop point, not its start, so this is the sample's length,
not the loop's). Until the effect's voice is found the total shows `-:--`.

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
&handle, 1, 1, 1)`, which the script daemon's thread (`scpSubAdpcmPlay`,
running on the title) opens with `soundDataOpen` and `AdpcmPlay`, as every
scene's stream. Its voices are tagged music, or effects for 101 to 104, by
`adpcmDataSet` (AUDIO.md, "Gains and output mode"). The game has two stream
records and two IOP rings, and the title theme (op.c `actTitleShortCut`,
stream 56 in `titleAdpcm`, looping) holds one, so the first play fades the
theme with `scpAdpcmFadeCloseFunc(&titleAdpcm, 1024)` (op.c's step when the
demo leaves the title) and requests the stream when the theme has closed.
Then `titleAdpcm` is cleared, as op.c does after its own fade:
`actTitleShortCut` waits while it is set, and op.c's demo step fades
whatever it points to. A stream plays once (`loopNum` 1) and closes itself.
On leaving, a theme that was playing is requested again, from its start.

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
distance or their environment's procs. A stream restarts from the
beginning; the title theme restarts from its start on leaving. Opening the
gallery from the pause menu is not possible (Extras is a title-only page).

## Testing

`ICO_GALLERY_PLAY` (environment, developer only; the player never needs it)
is a comma-separated list of `kind:value` entries the page plays when it
opens, one every 200 Main ticks (8 s) from 50 ticks after it opens:
`stream:N` (an `adpcmFile` row), `env:I` (the ambience of `seEnv` row I),
`se:D` (the `seDef` row D, in the Voice group, else the first bank that has
it), `pause:0` (Cross on the entry sounding or paused: `gallery: paused`,
`gallery: resumed` or, for an effect, `gallery: stopped ... (held)`),
`seq:B` (logs the failure above). While an entry sounds the page logs where
it is every 50 Main ticks: `gallery: stream 47 at 7.6 s of 265.6 s`. Each play logs `gallery: playing
<group> <key> (<label>)`, then the engine's line: `gallery: stream N (...)
opened on SPU voices A and B at audio frame F` or `gallery: effect D from
bank B (program P, tone T) keyed at audio frame F` (F is `spu2_time()`, the
WAV dump's frame). After the last, `gallery: script done`. A failure is a
`gallery: failed ...` line.

- `gallery` (CPU; the base ELF, and the disc image when present; 77 without
  the ELF): the list from the real tables: keys in range, groups in order
  with one heading each, no key twice in a group, every stream named by its
  file without the folder and `.int` with no column and in the group its
  folder gives (47 is `event/39_8`, the title theme in the soundtrack, 55 a
  scene sound), L1 and R1 (`gallery_Step`: over a heading, wrapping at the
  ends), every label, column, asset and gallery string in the five
  languages drawn by the font (`ui_FontHasGlyph`), Left and Right; with
  the disc, the streams not on it left out and every listed bank found in a
  pack.
- `gallery_headless` (the headless build and the disc image; RUN_SERIAL,
  300 s, 77 without the image): `port/ui/test/gallery_headless.py` boots to
  the title with `pad-boot.txt`'s presses, opens Settings > Extras > Music
  and plays one entry of each group (stream 47, stream 87, the ambience of
  `seEnv` 61, stream 101, effect 130 of com_v, effect 1167 of `st25a_b-g`,
  which needs the bank load), then presses Triangle. It checks each
  `gallery: playing` line, the engine's line after it, that the WAV dump
  sounds within 4 s of each start, the theme's fade and restore, the banks'
  restore, and no `gallery: failed`.
- `settings` and `settings_render`: the Music row opens the page and
  Triangle comes back to it; the page over a fake engine (rows, the heading
  skip, the transport's words each after its glyph row, left to right, the
  glyph centred on its word's capitals; the bar empty and 0:00 with nothing
  playing, at 42.4 of 265.6 s with 0:42 and 4:25 while a stream plays;
  Cross play, pause (Paused, the bar kept) and resume, R1 and L1, Square,
  Left, Right, leaving); `settings_music_4x.png`, and
  `settings_music_1080.png` / `_classic.png` (a stream playing, Enhanced
  1920 x 1080).
