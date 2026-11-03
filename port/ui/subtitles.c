/*
 * port/ui/subtitles.c
 *
 * Text subtitle packs keyed by language ID and game block.
 */
#include "subtitles.h"

#include <string.h>

#include "../game/options.h"
#include "../platform/host_config.h"
#include "../platform/host_fs.h"

static const UiSubtitlePack *const kPacks[] = {
    &ui_subtitle_pack_rus,
    &ui_subtitle_pack_rus_2,
    &subtitles_eng_glitterberri,
    &subtitles_ptBR,
};

size_t ui_subtitle_pack_count(void)
{
    return sizeof(kPacks) / sizeof(kPacks[0]);
}

const char *ui_subtitle_pack_id(size_t index)
{
    return index < ui_subtitle_pack_count() ? kPacks[index]->id : NULL;
}

const char *ui_subtitle_pack_name(size_t index)
{
    return index < ui_subtitle_pack_count() ? kPacks[index]->name : NULL;
}

int ui_subtitle_pack_index(const char *id)
{
    if (id == NULL) {
        return -1;
    }
    for (size_t i = 0; i < ui_subtitle_pack_count(); i++) {
        if (strcmp(kPacks[i]->id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

const char *ui_subtitle_effective_language(int source, const char *language_id)
{
    if (source == ICO_SUBTITLE_SOURCE_DISC) {
        return "game";
    }
    if (language_id != NULL && strcmp(language_id, "game") != 0 &&
        ui_subtitle_pack_index(language_id) >= 0) {
        return language_id;
    }
    if (source == ICO_SUBTITLE_SOURCE_TEXT && ui_subtitle_pack_count() > 0) {
        return ui_subtitle_pack_id(0);
    }
    return "game";
}

int ui_subtitle_block_uses_disc(int block, int game_cleared)
{
    static const int kRunicDialogueBlocks[] = {9, 29, 31, 48, 49, 86, 91, 112};
    if (game_cleared) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(kRunicDialogueBlocks) / sizeof(kRunicDialogueBlocks[0]); i++) {
        if (block == kRunicDialogueBlocks[i]) {
            return 1;
        }
    }
    return 0;
}

const char *ui_subtitle_text(const char *language_id, int block)
{
    if (language_id == NULL || block < 0) {
        return NULL;
    }
    const int index = ui_subtitle_pack_index(language_id);
    if (index < 0) {
        return NULL;
    }

    const UiSubtitlePack *pack = kPacks[index];
    size_t lo = 0;
    size_t hi = pack->count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (pack->lines[mid].block < block) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < pack->count && pack->lines[lo].block == block ? pack->lines[lo].text : NULL;
}
