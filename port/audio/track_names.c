/*
 * port/audio/track_names.c
 *
 * The album's names for the game's streamed music (track_names.h).
 *
 * Provenance.  Album: "ICO - Perfect Music Files" (2021), 41 tracks, the
 * user's FLAC copy (24-bit, 96 kHz); tracks 40 and 41 are re-recordings
 * that are not in the game and were left out.  Titles are the album's, with
 * its capitalisation.  Matched on 2026-10-06 by tools/match_tracks.py
 * against the PAL disc (SCES-50760): every stream of adpcmFile 1..100 on
 * the disc was decoded from its .int file (SPU ADPCM, 0x800-byte rows per
 * channel) and every album track read from its FLAC; both became 12-bin
 * chroma per 0.1 s, the shorter slid over the longer, and the score is the
 * mean chroma cosine where the shorter is loud (docs/port/MUSIC.md,
 * "Names").  A stream takes a title at a score of 0.85 or more, or 0.5 or
 * more with a margin of 0.2 over the runner-up and a z-score of 2.8; the
 * comment on each row gives its score, margin and z.  The offset is where
 * the stream starts in the album track: near 0 for most, later where the
 * album track opens with something else (prologue +42.6 s, Who are you
 * +21.6 s, coffin +24.6 s, Queen (reprise) +36.3 s, continue +36.3 s, the
 * looped streams).  The durations agree where the offset is 0 (cave 64.3 s
 * against 66.1 s, ICO -You were there- 265.6 against 268.6, Castle in the
 * Mist 117.2 against 187.5 being the exception: the album's version runs
 * on past the staff roll's stream).  The agent did not listen; the notes
 * on what the in-game uses are from the scripts' variable names: 1 is the
 * shadows' battle loop, 6 the opening demo (op.c titleSub), 12 deja.c,
 * 18 and 19 the idols' doors (sekizo), 47 and 49 the ending and staff roll
 * (end.c ed and staff3), 52 the game-over "continue" loop, 55 sekizo_common.
 * Close calls were rendered through the gallery in the headless game
 * (ICO_GALLERY_PLAY, audio_dump; the first 7.5 s of each, the same
 * features): 28 is reflector I (0.90 against reflector III's 0.68; the
 * disc pass had a margin of 0.10), 13 impression (0.91 against impression
 * (reprise)'s 0.84; 34 is the reprise, 0.97), and 46, below the disc
 * pass's bar, collapse (0.91 against 0.76).  Unmatched candidates (score,
 * best track): 9 sword 0.73, 30 reflector IV 0.56, 32 bridge II 0.52, 38
 * stairway II 0.64, 51 cave 0.64, 56 (the title theme) open III 0.55; the
 * gallery shows their file names.  Album tracks no stream matched: sword,
 * open I, The Gate, Queen, open III to VI, reflector III, reflector IV,
 * bridge II, bridge III, falling down (nonomori), stairway II.
 *
 * Regenerate: MUSIC.md, "Names".
 */
#include "track_names.h"

#include <stddef.h>

const IcoTrackName ico_track_names[] = {
    {ICO_TRACK_STREAM, 1, "battle.int", "darkness"},    /* c 0.99, margin 0.52, z 5.1 */
    {ICO_TRACK_STREAM, 6, "event/01.int", "prologue"},  /* c 0.92, margin 0.45, z 4.7 */
    {ICO_TRACK_STREAM, 10, "event/02_1d.int", "cave"},  /* c 0.87, margin 0.52, z 4.7 */
    {ICO_TRACK_STREAM, 11, "event/02_2.int", "coffin"}, /* c 0.71, margin 0.30, z 4.0 */
    {ICO_TRACK_STREAM, 12, "event/03.int",
     "d\xC3\xA9j\xC3\xA0 vu"},                                      /* c 0.66, margin 0.26, z 4.1 */
    {ICO_TRACK_STREAM, 13, "event/04.int", "impression"},           /* c 0.90, margin 0.07, z 3.1 */
    {ICO_TRACK_STREAM, 14, "event/05.int", "cage"},                 /* c 0.84, margin 0.51, z 4.4 */
    {ICO_TRACK_STREAM, 15, "event/06.int", "Who are you"},          /* c 0.86, margin 0.47, z 4.8 */
    {ICO_TRACK_STREAM, 16, "event/07.int", "hold hands"},           /* c 0.77, margin 0.28, z 3.2 */
    {ICO_TRACK_STREAM, 18, "event/09.int", "open II"},              /* c 0.80, margin 0.39, z 4.1 */
    {ICO_TRACK_STREAM, 19, "event/09_2.int", "open II"},            /* c 0.66, margin 0.27, z 3.7 */
    {ICO_TRACK_STREAM, 20, "event/10.int", "darkness"},             /* c 0.97, margin 0.25, z 2.6 */
    {ICO_TRACK_STREAM, 21, "event/11.int", "stairway I"},           /* c 0.70, margin 0.34, z 3.9 */
    {ICO_TRACK_STREAM, 22, "event/12.int", "heal"},                 /* c 0.96, margin 0.53, z 4.3 */
    {ICO_TRACK_STREAM, 23, "event/13.int", "Queen (reprise)"},      /* c 0.77, margin 0.25, z 3.6 */
    {ICO_TRACK_STREAM, 28, "event/18a.int", "reflector I"},         /* c 0.87, margin 0.10, z 3.9 */
    {ICO_TRACK_STREAM, 31, "event/24_1.int", "bridge I"},           /* c 0.70, margin 0.28, z 4.3 */
    {ICO_TRACK_STREAM, 34, "event/26.int", "impression (reprise)"}, /* c 0.97, margin 0.07, z 3.1 */
    {ICO_TRACK_STREAM, 35, "event/27.int", "reunion"},              /* c 0.81, margin 0.31, z 3.4 */
    {ICO_TRACK_STREAM, 36, "event/29.int", "Shadow"},               /* c 0.73, margin 0.27, z 3.7 */
    {ICO_TRACK_STREAM, 37, "event/29a.int", "Shadow"},              /* c 0.95, margin 0.54, z 4.3 */
    {ICO_TRACK_STREAM, 40, "event/32_2.int", "Queen (reprise)"},    /* c 0.81, margin 0.21, z 3.8 */
    {ICO_TRACK_STREAM, 41, "event/33.int", "Entity"},               /* c 0.98, margin 0.49, z 4.4 */
    /* below the disc pass's bar (c 0.85, margin 0.13, offset 0.0 s); the
       headless render's first 7.5 s scores 0.91 against collapse, 0.76 the
       runner-up */
    {ICO_TRACK_STREAM, 46, "event/39_7.int", "collapse"},
    {ICO_TRACK_STREAM, 47, "event/39_8.int",
     "ICO -You were there-"}, /* c 0.99, margin 0.46, z 4.5 */
    {ICO_TRACK_STREAM, 49, "event/39_10.int",
     "Castle in the Mist"},                                 /* c 0.95, margin 0.28, z 4.1 */
    {ICO_TRACK_STREAM, 52, "event/37.int", "continue"},     /* c 0.97, margin 0.48, z 4.5 */
    {ICO_TRACK_STREAM, 53, "event/42.int", "beginning"},    /* c 0.68, margin 0.23, z 2.8 */
    {ICO_TRACK_STREAM, 54, "event/43.int", "reflector II"}, /* c 0.83, margin 0.48, z 4.0 */
    {ICO_TRACK_STREAM, 55, "event2/00.int", "stairway I"},  /* c 0.95, margin 0.52, z 4.5 */
};

const int ico_track_name_count = (int)(sizeof(ico_track_names) / sizeof(ico_track_names[0]));

const char *ico_track_name(IcoTrackKind kind, int key)
{
    for (int i = 0; i < ico_track_name_count; i++) {
        if (ico_track_names[i].kind == kind && ico_track_names[i].key == key) {
            return ico_track_names[i].title;
        }
    }
    return NULL;
}
