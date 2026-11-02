/*
 * port/ui/subtitles.h
 *
 * Text subtitle packs keyed by the game's subtitle block number.
 */
#ifndef PORT_UI_SUBTITLES_H
#define PORT_UI_SUBTITLES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UiSubtitleLine {
    int block;
    const char *text;
} UiSubtitleLine;

typedef struct UiSubtitlePack {
    const char *id;
    const char *name;
    const UiSubtitleLine *lines;
    size_t count;
} UiSubtitlePack;

extern const UiSubtitlePack ui_subtitle_pack_rus;

/* The text subtitle packs included in the program. */
size_t ui_subtitle_pack_count(void);
const char *ui_subtitle_pack_id(size_t index);
const char *ui_subtitle_pack_name(size_t index);
int ui_subtitle_pack_index(const char *id);
/* Resolve a configured language against the selected source. */
const char *ui_subtitle_effective_language(int source, const char *language_id);
/* NULL when this pack has no entry for the block. */
const char *ui_subtitle_text(const char *language_id, int block);

#ifdef __cplusplus
}
#endif

#endif
