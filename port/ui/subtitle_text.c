/*
 * port/ui/subtitle_text.c
 *
 * A community translation's subtitles drawn as text (subtitle_text.h).
 */
#include "subtitle_text.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "menu_font.h"
#include "menu_text.h"
#include "options.h" /* ico_opt_ui_language_name: the log's name */
#include "strings.h"
#include "ui_internal.h" /* ui__sync */
#include "ui_lang.h"     /* ui_settings_text_language */

_Static_assert(UI_LANG_COMMUNITY_COUNT == 3,
               "a subtitle table for each community translation (ui_subtitle_table)");

const IcoSubtitle *ui_subtitle_table(int lang, int *count)
{
    const IcoSubtitle *t = NULL;
    int n = 0;
    switch (lang) {
    case UI_LANG_RU_BOBTTZ:
        t = ico_subtitles_ru_bobttz;
        n = ico_subtitles_ru_bobttz_count;
        break;
    case UI_LANG_RU_COBRA838:
        t = ico_subtitles_ru_cobra838;
        n = ico_subtitles_ru_cobra838_count;
        break;
    case UI_LANG_PT_BR:
        t = ico_subtitles_pt_br;
        n = ico_subtitles_pt_br_count;
        break;
    default:
        break;
    }
    if (count) {
        *count = n;
    }
    return t;
}

const char *ui_subtitle_find(const IcoSubtitle *t, int n, int block, int gameClear)
{
    for (int i = 0; t && i < n; i++) {
        if (t[i].block == block) {
            /* Yorda's words: her own script's picture on a first play */
            return t[i].secondOnly && !gameClear ? NULL : t[i].words;
        }
    }
    return NULL;
}

/* the record the last ui_subtitle_words call was about, and the changes */
static int s_lastBlock = -1;
static int s_lastClear = -1;
static int s_changes;

const char *ui_subtitle_words(int block, int gameClear)
{
    const int lang = ui_settings_text_language();
    if (lang < 0) {
        return NULL;
    }
    int n = 0;
    const IcoSubtitle *t = ui_subtitle_table(lang, &n);
    const char *words = ui_subtitle_find(t, n, block, gameClear);
    if (block != s_lastBlock || gameClear != s_lastClear) {
        s_lastBlock = block;
        s_lastClear = gameClear;
        if (s_changes < 8 || s_changes % 100 == 0) {
            fprintf(stderr, "ui: subtitles: record %d (%s play) %s (%s, %d entries)\n", block,
                    gameClear ? "second" : "first",
                    words ? "drawn as text" : "keeps the disc's picture",
                    ico_opt_ui_language_name(lang), n);
        }
        s_changes++;
    }
    return words;
}

/* the lines of words (at most two): starts and lengths */
static int splitWords(const char *words, const char *start[2], size_t len[2])
{
    int n = 0;
    const char *s = words;
    while (n < 2) {
        const char *nl = strchr(s, '\n');
        start[n] = s;
        len[n] = nl ? (size_t)(nl - s) : strlen(s);
        n++;
        if (!nl) {
            break;
        }
        s = nl + 1;
    }
    return n;
}

int ui_subtitle_layout(const char *words, UiSubtitleLayout *out)
{
    if (!words || !*words || !out || !ui_font_init()) {
        return 0;
    }
    const char *start[2];
    size_t len[2];
    const int n = splitWords(words, start, len);
    float widest = 0.0f;
    for (int i = 0; i < n; i++) {
        char line[256];
        const size_t k = len[i] < sizeof(line) - 1 ? len[i] : sizeof(line) - 1;
        memcpy(line, start[i], k);
        line[k] = '\0';
        const float w = ui_measure_menu_text(UI_SUBTITLE_EM, line);
        widest = w > widest ? w : widest;
    }
    /* a line wider than the band is set smaller to fit, as the menus' rows
       are (the translation's words run longer than the English pictures') */
    float size = UI_SUBTITLE_EM;
    if (widest > UI_SUBTITLE_ROOM) {
        size *= UI_SUBTITLE_ROOM / widest;
    }
    const float mid = 0.5f * (UI_SUBTITLE_Y0 + UI_SUBTITLE_Y1);
    out->lines = n;
    out->size = size;
    out->x = 0.5f * (UI_SUBTITLE_X0 + UI_SUBTITLE_X1);
    out->y[0] = n > 1 ? mid - 0.5f * UI_SUBTITLE_PITCH : mid;
    out->y[1] = mid + 0.5f * UI_SUBTITLE_PITCH;
    return 1;
}

void ui_subtitle_draw(const char *words, int block, const unsigned char rgba[4])
{
    UiSubtitleLayout lay;
    if (!rgba || !ui_subtitle_layout(words, &lay)) {
        return;
    }
    const char *start[2];
    size_t len[2];
    const int n = splitWords(words, start, len);
    /* the packet's state so far (jimaku.c's), then the words in it */
    ui__sync();
    const int wasTitle = ui_menu_text_set_title(0);
    for (int i = 0; i < n; i++) {
        char line[256];
        const size_t k = len[i] < sizeof(line) - 1 ? len[i] : sizeof(line) - 1;
        memcpy(line, start[i], k);
        line[k] = '\0';
        /* keyed by the record and the line, so a subtitle that fades
           blends between ticks as its strips did */
        const uint64_t owner =
            ui_set_draw_key(0x4A494D0000000000ull ^ ((uint64_t)block << 4) ^ (uint64_t)(i + 1));
        ui_draw_menu_text(lay.x, lay.y[i], lay.size, rgba, line,
                          UI_KEEP_STATE | UI_ALIGN_CENTER | UI_VALIGN_MIDDLE, UI_INK_LIGHT, NULL);
        ui_set_draw_key(owner);
    }
    ui_menu_text_set_title(wasTitle);
}
