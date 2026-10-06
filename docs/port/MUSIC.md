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
| `port/ui/settings.c` | the page (`UI_PAGE_MUSIC`, a list page, `kGalDef`) and the Extras hook `extrasMusic` |
| `port/audio/track_names.h`, `track_names.c` | the album's titles by stream number, with their provenance |
| `port/data/df_pack.h`, `df_pack.c` | one member of a DATA.DF stage pack read through the VFS |
| `tools/match_tracks.py` | the matching behind `track_names.c` |
| `ico2/fumi/sound/s_init.c` | `soundSeReqStop`, a port hook: stops every slot playing from a bank |

## The list

Built once, the first time the page opens, from `adpcmFile`, `seFile`,
`seDef`, `seList`, `seEnv` and `stageData` as the table loader filled them
(docs/port/DATA.md). Each group opens with a heading, which the cursor skips
(`ui_list.h`); Back ends the list. On the PAL disc:

| group | entries | what |
| --- | --- | --- |
| Soundtrack | 52 | the streams of `adpcmFile` 1 to 100 that are music: `battle.int`, every `event/` stream (the scored scenes, the ending), the title theme (56, `event2/50.int`) and any stream the album names (55, `event2/00.int`) |
| Scene sounds | 44 | the other `event2/` streams: the machinery's stingers (the scripts' handles name them: gondolas, idols' doors, lifts, gates, bridges, chains, lightning) |
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

**Columns.** The label is the album's title when `track_names.c` has the
stream, else the file (`event/39_7.int`, without `sound/ICO_ADPCM/`), or the
effect's `seDef` name. Column A is the file when the label is the album's,
and the stage's key for an ambience. The status line is the group, the asset
(the stream's file, or the bank's file with the effect's program and tone)
and Playing or Stopped; the hint line under it names the buttons. Headings
are drawn 16 units left of the entries.

**Buttons.** Cross plays the entry under the cursor (stopping what plays),
Square stops, Left and Right jump to the previous or next group (its heading
at the top of the window), Triangle or Circle and Back leave.

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

## Names

`track_names.c` maps a stream (its `adpcmFile` row) to the title of the
album track it is: "ICO - Perfect Music Files" (2021), the user's FLAC copy
(41 tracks at 24-bit 96 kHz; tracks 40 and 41 are re-recordings, not in the
game). Titles keep the album's capitalisation. The file holds only stream
numbers, the in-game file names and the titles, with the evidence per row;
nothing of the disc or the album. 30 streams have a title, 24 of the 39
album tracks are named; the rest keep their file names. The header of
`track_names.c` lists the evidence, the close calls and the unmatched
candidates.

**Method** (`tools/match_tracks.py`). A stream's length is `sectors * 2048 /
channels / 16 * 28 / pitch` seconds (16-byte SPU ADPCM blocks of 28 samples;
the pitch word is Hz, `stream.c` `st_adpcm_pitch` turns it into
`hz * 4096 / 48000`); an album track's is its FLAC STREAMINFO samples over
rate. Lengths alone do not decide: many album tracks are longer edits or
open with something else. With `--features` (numpy and soundfile, from
wheels in a scratch venv) every stream on the disc is decoded from its
`.int` file and every album track read, both become 12-bin chroma and log
energy per 0.1 s, the shorter slides over the longer, and the score is the
mean chroma cosine where the shorter one is loud. A stream takes the best
track at a score of 0.85, or 0.5 with a margin of 0.2 over the runner-up and
a z-score of 2.8 over all the tracks. Close calls were checked by playing
the stream through the gallery in the headless game
(`ICO_GALLERY_PLAY=stream:N`, `audio_dump=`) and scoring the dump's first
7.5 s the same way: 28 (reflector I over reflector III), 13 (impression over
its reprise) and 46 (collapse, below the disc pass's bar, 0.91 in the
render), which is the one row taken from the render.

| stream | file | album track | score | margin | offset in the track |
| --- | --- | --- | --- | --- | --- |
| 1 | battle.int | darkness | 0.99 | 0.52 | -3.9 s |
| 6 | event/01.int | prologue | 0.92 | 0.45 | +42.6 s |
| 10 | event/02_1d.int | cave | 0.87 | 0.52 | -0.9 s |
| 11 | event/02_2.int | coffin | 0.71 | 0.30 | +24.6 s |
| 12 | event/03.int | déjà vu | 0.66 | 0.26 | +0.4 s |
| 13 | event/04.int | impression | 0.90 (render 0.91) | 0.07 (render 0.07) | +0.3 s |
| 14 | event/05.int | cage | 0.84 | 0.51 | +0.2 s |
| 15 | event/06.int | Who are you | 0.86 | 0.47 | +21.6 s |
| 16 | event/07.int | hold hands | 0.77 | 0.28 | +0.4 s |
| 18 | event/09.int | open II | 0.80 | 0.39 | +0.3 s |
| 19 | event/09_2.int | open II | 0.66 | 0.27 | +0.3 s |
| 20 | event/10.int | darkness | 0.97 | 0.25 | +0.4 s |
| 21 | event/11.int | stairway I | 0.70 | 0.34 | +0.3 s |
| 22 | event/12.int | heal | 0.96 | 0.53 | +0.2 s |
| 23 | event/13.int | Queen (reprise) | 0.77 | 0.25 | +36.3 s |
| 28 | event/18a.int | reflector I | 0.87 (render 0.90) | 0.10 (render 0.22) | -0.3 s |
| 31 | event/24_1.int | bridge I | 0.70 | 0.28 | -0.3 s |
| 34 | event/26.int | impression (reprise) | 0.97 | 0.07 | +0.3 s |
| 35 | event/27.int | reunion | 0.81 | 0.31 | +0.5 s |
| 36 | event/29.int | Shadow | 0.73 | 0.27 | +0.3 s |
| 37 | event/29a.int | Shadow | 0.95 | 0.54 | +6.3 s |
| 40 | event/32_2.int | Queen (reprise) | 0.81 | 0.21 | -0.3 s |
| 41 | event/33.int | Entity | 0.98 | 0.49 | +0.4 s |
| 46 | event/39_7.int | collapse | 0.85 (render 0.91) | 0.13 (render 0.15) | 0.0 s |
| 47 | event/39_8.int | ICO -You were there- | 0.99 | 0.46 | +0.4 s |
| 49 | event/39_10.int | Castle in the Mist | 0.95 | 0.28 | +0.4 s |
| 52 | event/37.int | continue | 0.97 | 0.48 | +36.3 s |
| 53 | event/42.int | beginning | 0.68 | 0.23 | +0.3 s |
| 54 | event/43.int | reflector II | 0.83 | 0.48 | +0.4 s |
| 55 | event2/00.int | stairway I | 0.95 | 0.52 | +0.4 s |

Unmatched, with the best candidate: 9 sword (0.73, margin 0.17), 30
reflector IV (0.56), 32 bridge II (0.52), 38 stairway II (0.64, z 2.7), 51
cave (0.64), 56 the title theme (open III, 0.55); every other stream scores
below 0.6. Album tracks no stream matched: sword, open I, The Gate, Queen,
open III, open IV, open V, open VI, reflector III, reflector IV, bridge II,
bridge III, falling down (nonomori) and stairway II. They may be edits the
album made from several cues, or sounds the game plays from its effect
banks; the gallery does not try to name effects.

**Regenerating.** With the base ELF, the disc image and the album folder:

```
python3 -m venv /tmp/mt && /tmp/mt/bin/pip install numpy soundfile
/tmp/mt/bin/python tools/match_tracks.py --elf baserom/pal/baseelf.elf \
    --iso baserom/Ico_PAL.iso --album "<album folder>" --features --emit-c
```

prints every stream's verdict and the rows for `track_names.c` (the decode
is pure Python and takes about four minutes). Without `--features` it lists
the lengths only (standard library). A close call is rendered with the
headless build: an `ico-pc.ini` with `audio_dump=PATH`, the pad script of
`port/ui/test/gallery_headless.py` and `ICO_GALLERY_PLAY=stream:N`.

## Testing

`ICO_GALLERY_PLAY` (environment, developer only; the player never needs it)
is a comma-separated list of `kind:value` entries the page plays when it
opens, one every 200 Main ticks (8 s) from 50 ticks after it opens:
`stream:N` (an `adpcmFile` row), `env:I` (the ambience of `seEnv` row I),
`se:D` (the `seDef` row D, in the Voice group, else the first bank that has
it), `seq:B` (logs the failure above). Each play logs `gallery: playing
<group> <key> (<label>)`, then the engine's line: `gallery: stream N (...)
opened on SPU voices A and B at audio frame F` or `gallery: effect D from
bank B (program P, tone T) keyed at audio frame F` (F is `spu2_time()`, the
WAV dump's frame). After the last, `gallery: script done`. A failure is a
`gallery: failed ...` line.

- `gallery` (CPU; the base ELF, and the disc image when present; 77 without
  the ELF): the list from the real tables: keys in range, groups in order
  with one heading each, no key twice in a group, every name-table row on the
  list with its title, every label, column, asset and gallery string in the
  five languages drawn by the font (`ui_FontHasGlyph`), Left and Right; with
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
  skip, Cross, Square, Left, Right, leaving); `settings_music_4x.png`.
