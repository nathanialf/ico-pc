/*
 * port/game/subtitles.h
 *
 * A community translation's subtitles (port/ui/strings.h
 * UI_LANG_COMMUNITY_FIRST, docs/TRANSLATING.md): the words of each subtitle
 * record it translates, drawn as text in place of the record's picture
 * (port/ui/subtitle_text.h, called from ico2/fumi/src/jimaku.c jimakuDisp).
 *
 * The game reads its subtitles from text/data_<EG|FR|GR|IT|SP><01|02>.jim
 * (jimaku.c jimakuFileName, the 02 file once the game is cleared,
 * gFlagGameClear): record N is the picture the two strips of the subtitle
 * band show (texProperty 434 and 435 on the European disc), picked by the
 * script's sub.block number.  A table here has one entry per record of the English
 * files the translation covers, in record order; a record it leaves out
 * keeps its picture.  The records Yorda speaks show her own script on the
 * first play and English words on a cleared game's: their entries are
 * second-play only, and the first play keeps the script's picture.
 */
#ifndef ICO_PORT_GAME_SUBTITLES_H
#define ICO_PORT_GAME_SUBTITLES_H

#ifdef __cplusplus
extern "C" {
#endif

/* the longest line a table holds, in characters (code points), and the
   lines a record has at most (ctest subtitles_text) */
#define ICO_SUBTITLE_LINE_MAX 50
#define ICO_SUBTITLE_LINES 2

typedef struct IcoSubtitle {
    short block;              /* the record (the CSV's sub_NNN) */
    unsigned char secondOnly; /* 1: Yorda's words, shown as text on a cleared game only */
    const char *words;        /* UTF-8, the lines split at '\n' */
} IcoSubtitle;

/* Russian, by bob-ttz: port/game/subtitles_ru_bobttz.c */
extern const IcoSubtitle ico_subtitles_ru_bobttz[];
extern const int ico_subtitles_ru_bobttz_count;

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_GAME_SUBTITLES_H */
