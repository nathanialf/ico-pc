/*
 * port/audio/track_names.h
 *
 * The soundtrack album's names for the game's music, for the music gallery
 * (port/ui/gallery.c, docs/port/MUSIC.md).  A row names one piece of the
 * game's audio by what the game calls it (a stream's number in adpcmFile, or
 * a sequence's name) and gives the album track's title for it.  Nothing of
 * the disc or of the album is here: only the keys, the in-game file names
 * and the titles.  Provenance and method: the header of track_names.c.
 */
#ifndef ICO_PORT_TRACK_NAMES_H
#define ICO_PORT_TRACK_NAMES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum IcoTrackKind {
    ICO_TRACK_STREAM = 0, /* key: the adpcmFile row (fumi/sound/adpcm_init.c) */
    ICO_TRACK_SEQUENCE    /* key: the seFile row of a .sq bank; the PAL disc has none */
} IcoTrackKind;

typedef struct IcoTrackName {
    IcoTrackKind kind;
    int key;
    const char *asset; /* the in-game file, as adpcmFile names it, without sound/ICO_ADPCM/ */
    const char *title; /* the album's title, its capitalisation, UTF-8 */
} IcoTrackName;

extern const IcoTrackName ico_track_names[];
extern const int ico_track_name_count;

/* The album title of (kind, key), NULL when the piece is not on the album. */
const char *ico_track_name(IcoTrackKind kind, int key);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_TRACK_NAMES_H */
