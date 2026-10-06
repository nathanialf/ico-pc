/*
 * port/ui/gallery.h
 *
 * Settings > Extras > Music, the music gallery (docs/port/MUSIC.md): a list
 * of everything the game's sound system plays, built at run time from the
 * game's own tables, each entry played on demand through the game's own
 * engines.
 *
 * The list (gallery.c, no game code: the tables come in a GalleryTables):
 *   Soundtrack     the streams (adpcmFile 1..100) that are music: every
 *                  event/ stream (the scored scenes and battles), the title
 *                  theme and any stream the album names (track_names.c);
 *                  the album's title where it has one
 *   Scene sounds   the other streams: the event2/ machinery stingers
 *                  (gondolas, gates, idols, lifts)
 *   Ambience       the stage sound environments (seEnv), one entry per
 *                  sound, from the stages of the game (stageData 1..39)
 *   Voice          Yorda's hint voices (streams 101..104) and the com_v bank
 *   Sound effects  one headed group per bank (seFile, deduplicated by file),
 *                  the effects by their seDef names
 * then Back.  Headings are shown, never selected (ui_list.h).  The PAL disc
 * has no sequenced music: no stage keeps a BGM bank (stageData seSegData1
 * and 2 are 0 everywhere) and no pack holds a .sq, so there is no sequence
 * group (MUSIC.md, "Sequences").
 *
 * Playback (gallery_play.c, the game side; GalleryEngine): streams through
 * scpAdpcmPlayRequestFunc after the title theme's fade, effects through
 * soundSeDefPlay with their bank loaded.  gallery_Tick runs the engine once
 * a Main tick while the page is up.
 */
#ifndef PORT_UI_GALLERY_H
#define PORT_UI_GALLERY_H

#include <stddef.h>

#include "adpcm_init.h"
#include "s_init.h"
#include "typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GalleryGroup {
    GAL_G_SOUNDTRACK = 0,
    GAL_G_SCENE,
    GAL_G_AMBIENCE,
    GAL_G_VOICE,
    GAL_G_SE,
    GAL_G_BACK, /* the last item */
    GAL_G_COUNT
} GalleryGroup;

typedef enum GalleryKind {
    GAL_K_HEADING = 0,
    GAL_K_STREAM, /* key: the adpcmFile row */
    GAL_K_SE,     /* key: the seDef row; bank: the seFile row it plays from */
    GAL_K_BACK
} GalleryKind;

typedef struct GalleryItem {
    unsigned char group; /* GalleryGroup */
    unsigned char kind;  /* GalleryKind */
    short bank;          /* GAL_K_SE and a bank's heading: the seFile row, else -1 */
    short stage;         /* an ambience: the first stage that plays it, else -1 */
    short env;           /* an ambience: its first seEnv row, else -1 */
    int key;             /* see GalleryKind; -1 for a heading */
} GalleryItem;

/* The game's tables as the build sees them (the counts are the PAL ELF's). */
typedef struct GalleryTables {
    const AdpcmDataRec *adpcm;
    int adpcmCount; /* 105 */
    const SeBank *seFile;
    int seFileCount; /* 104 */
    const SeDef *seDef;
    int seDefCount; /* 1426 */
    const SeKind *seList;
    int seListCount; /* 3837 */
    const SeEnvDef *seEnv;
    int seEnvCount; /* 425 */
    const StgPre *stage;
    int stageCount; /* 106 */
    /* whether the stream's file is on the disc (the four e3/ streams are
       not); NULL: every stream */
    int (*streamOnDisc)(int no);
} GalleryTables;

/* What the engine (gallery_play.c) does for the page.  Every hook may be
   NULL (tests). */
typedef struct GalleryEngine {
    /* the tables, filled at the first gallery_Enter; 0 when there are none */
    int (*tables)(GalleryTables *out);
    void (*enter)(void);
    void (*leave)(void);
    /* starts an item (stopping what plays); 0, or -1 (logged) */
    int (*play)(const GalleryItem *it);
    void (*stop)(void);
    /* once a Main tick while the page is up */
    void (*tick)(void);
    /* the item now sounding (the one last played, while it sounds), NULL */
    const GalleryItem *(*playing)(void);
} GalleryEngine;

void gallery_SetEngine(const GalleryEngine *e);

/* The list from the tables; returns the item count (Back included). */
int gallery_Build(const GalleryTables *t);
int gallery_Count(void);
const GalleryItem *gallery_Item(int i);
/* The item of (group, kind, key[, bank]): the first match, -1. bank -1
   matches any. */
int gallery_Find(int group, int kind, int key, int bank);
/* The next (dir +1) or previous (-1) heading's first selectable item from i,
   wrapping. */
int gallery_JumpGroup(int i, int dir);

/* The texts: the label (the album's title, or the asset's name), column A
   (the in-game name when the label is the album's), the asset (a stream's
   file, a bank's file and the effect's program and tone).  buf holds the
   text when one is built. */
const char *gallery_Label(int i, char *buf, size_t n);
const char *gallery_ColA(int i, char *buf, size_t n);
const char *gallery_Asset(int i, char *buf, size_t n);
/* The group's name (strings.h id). */
int gallery_GroupStr(int group);

/* The page (settings.c): enter builds the list the first time and tells the
   engine; play / stop / leave go to the engine; tick runs it and the
   ICO_GALLERY_PLAY script (MUSIC.md, "Testing"). */
void gallery_Enter(void);
void gallery_Leave(void);
int gallery_Play(int i);
void gallery_Stop(void);
void gallery_Tick(void);
int gallery_Playing(void);
const GalleryTables *gallery_Tables(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_GALLERY_H */
