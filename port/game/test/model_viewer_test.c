/* model_viewer_test.c: the model viewer's table and words (package MV;
 * port/game/model_viewer.h), on the CPU:
 *   - the motion-kind blocks are ordered, apart, inside the motion-kind and
 *     motion-orient tables;
 *   - each model's motions [motFirst, motLast) lie inside one block, with
 *     that block's motion-orient rows; a model without motions has none;
 *   - its host stage is one with data on the PAL disc (1 to 63, 103 to
 *     105; 88 and 91 have data but are the ending's), not the title; its
 *     kind, model and layout row inside the game's tables;
 *   - rows MV_ROW_ICO and MV_ROW_YORDA (Settings > Extras > Characters)
 *     are the boy and the girl;
 *   - its name, and the viewer's words, are non-empty in the five
 *     languages, different from the other models' in each, and every
 *     character has a glyph in the port font (ui_FontHasGlyph).
 * Exit 0, 1 on a mismatch. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "model_viewer.h"
#include "strings.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the game's tables (port/data/gen/table_defs.c): objKindData[70],
   modelData[1637], objLayout[3759] */
#define KINDS 70
#define MODELS 1637
#define LAYOUT_ROWS 3759

static const char *const kLang[UI_LANG_COUNT] = {"en", "fr", "de", "it", "es"};

/* the next code point of a UTF-8 string, 0 at the end, -1 if malformed */
static int32_t next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int32_t c;
    int n;
    if (p[0] == 0) {
        return 0;
    }
    if (p[0] < 0x80) {
        *s += 1;
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0) {
        c = p[0] & 0x1F;
        n = 1;
    } else if ((p[0] & 0xF0) == 0xE0) {
        c = p[0] & 0x0F;
        n = 2;
    } else if ((p[0] & 0xF8) == 0xF0) {
        c = p[0] & 0x07;
        n = 3;
    } else {
        return -1;
    }
    for (int i = 1; i <= n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            return -1;
        }
        c = (c << 6) | (p[i] & 0x3F);
    }
    *s += n + 1;
    return c;
}

static void checkText(int id, const char *what)
{
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        const char *s = ui_StrIn((UiLang)l, (UiStrId)id);
        CHECK(s != NULL && s[0] != '\0', "%s (%d): empty in %s", what, id, kLang[l]);
        if (s == NULL) {
            continue;
        }
        if (l != UI_LANG_EN) {
            /* a translation of its own, not the English fallback */
            const char *const *table[UI_LANG_COUNT] = {ui_strings_en, ui_strings_fr, ui_strings_de,
                                                       ui_strings_it, ui_strings_es};
            CHECK(table[l][id] != NULL, "%s (%d): no %s entry", what, id, kLang[l]);
        }
        const char *p = s;
        int32_t c;
        while ((c = next(&p)) > 0) {
            if (c == '\n') {
                continue;
            }
            CHECK(c >= 0x20 && ui_FontHasGlyph((uint32_t)c), "%s (%d) in %s: U+%04X has no glyph",
                  what, id, kLang[l], (unsigned)c);
        }
        CHECK(c == 0, "%s (%d) in %s: malformed UTF-8", what, id, kLang[l]);
    }
}

static int dataStage(int s)
{
    return (s >= 2 && s <= 63) || (s >= 103 && s <= 105);
}

int main(void)
{
    if (!ui_FontInit()) {
        printf("model_viewer_test: the port font did not load\n");
        return 1;
    }

    /* the blocks */
    CHECK(mv_blockCount == 6, "%d motion-kind blocks", mv_blockCount);
    for (int b = 0; b < mv_blockCount; b++) {
        const MvBlock *k = &mv_blocks[b];
        CHECK(k->first >= 0 && k->first < k->last && k->last <= MV_MOTION_KINDS,
              "block %d: motions %d..%d", b, k->first, k->last);
        CHECK(k->oriFrom >= 0 && k->oriFrom < k->oriTo && k->oriTo <= MV_ORIENT_ROWS,
              "block %d: orient rows %d..%d", b, k->oriFrom, k->oriTo);
        CHECK(b == 0 || mv_blocks[b - 1].last <= k->first, "block %d: overlaps the one before", b);
    }

    /* the models */
    CHECK(mv_modelCount >= 20 && mv_modelCount <= 40, "%d models", mv_modelCount);
    for (int i = 0; i < mv_modelCount; i++) {
        const MvModel *m = &mv_models[i];
        const char *en = ui_StrIn(UI_LANG_EN, (UiStrId)m->nameStr);
        CHECK(m->nameStr >= UI_STR_MV_ICO && m->nameStr < UI_STR_COUNT, "model %d: name id %d", i,
              m->nameStr);
        checkText(m->nameStr, en);
        /* the list's label column: 560 px at size 24 (model_viewer.c; the model
         list has no value column) */
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            const char *nm = ui_StrIn((UiLang)l, (UiStrId)m->nameStr);
            const float w = ui_MeasureText(24.0f, nm);
            CHECK(w <= 560.0f, "%s in %s: %.1f px wide, the label column is 560", en, kLang[l], w);
        }
        CHECK(dataStage(m->stage), "%s: host stage %d", en, m->stage);
        CHECK(m->kind >= 1 && m->kind < KINDS, "%s: kind %d", en, m->kind);
        CHECK(m->charId >= 0 && m->charId < MODELS, "%s: model %d", en, m->charId);
        CHECK(m->label == -1 || (m->label > 0 && m->label < LAYOUT_ROWS), "%s: layout row %d", en,
              m->label);
        if (m->motFirst == m->motLast) {
            CHECK(m->motFirst == 0 && m->oriFrom == 0 && m->oriTo == 0,
                  "%s: no motions, but motion-orient rows %d..%d", en, m->oriFrom, m->oriTo);
        } else {
            int in = -1;
            for (int b = 0; b < mv_blockCount; b++) {
                if (mv_blocks[b].first <= m->motFirst && m->motLast <= mv_blocks[b].last) {
                    in = b;
                }
            }
            CHECK(m->motFirst < m->motLast && in >= 0, "%s: motions %d..%d in no block", en,
                  m->motFirst, m->motLast);
            CHECK(in < 0 ||
                      (m->oriFrom == mv_blocks[in].oriFrom && m->oriTo == mv_blocks[in].oriTo),
                  "%s: motion-orient rows %d..%d are not its block's", en, m->oriFrom, m->oriTo);
        }
        for (int j = 0; j < i; j++) {
            CHECK(mv_models[j].nameStr != m->nameStr, "%s: listed twice", en);
            for (int l = 0; l < UI_LANG_COUNT; l++) {
                CHECK(strcmp(ui_StrIn((UiLang)l, (UiStrId)mv_models[j].nameStr),
                             ui_StrIn((UiLang)l, (UiStrId)m->nameStr)) != 0,
                      "%s: the same name as model %d in %s", en, j, kLang[l]);
            }
        }
    }

    /* v0.4.2: Characters loads the table's first two rows: Ico (the boy,
       kind 1) and Yorda (the girl, kind 2), the numbers
       ico_appearance_character gives (appearance.h) */
    CHECK(mv_modelCount > MV_ROW_YORDA && mv_models[MV_ROW_ICO].nameStr == UI_STR_MV_ICO &&
              mv_models[MV_ROW_ICO].kind == 1 &&
              mv_models[MV_ROW_YORDA].nameStr == UI_STR_MV_YORDA &&
              mv_models[MV_ROW_YORDA].kind == 2,
          "rows %d and %d are Ico and Yorda", MV_ROW_ICO, MV_ROW_YORDA);

    /* the viewer's words */
    static const int kWords[] = {
        UI_STR_MV_ANIMATION, UI_STR_MV_LOOP, UI_STR_MV_FRAME, UI_STR_MV_NO_ANIMATIONS,
        UI_STR_MV_HINT_VIEW, UI_STR_MV_HINT_TITLE, UI_STR_MV_HINT_PLAY, UI_STR_MV_HINT_TURN,
        UI_STR_MV_HINT_ZOOM, UI_STR_MV_HINT_MOVE, UI_STR_EXTRAS_MODELS, UI_STR_BACK,
        UI_STR_MV_HINT_SAVE, UI_STR_MV_SAVED_FMT, UI_STR_MV_SAVED_NONE,
        /* v0.4.2: Characters inside the viewer (its Switch row, prompts) */
        UI_STR_CHAR_SWITCH_YORDA, UI_STR_CHAR_SWITCH_ICO, UI_STR_CHAR_HINT_COLOUR,
        UI_STR_CHAR_HINT_CHARACTER, UI_STR_VAL_ORIGINAL};
    for (unsigned i = 0; i < sizeof(kWords) / sizeof(kWords[0]); i++) {
        checkText(kWords[i], ui_StrIn(UI_LANG_EN, (UiStrId)kWords[i]));
    }

    ui_FontShutdown();
    if (failures == 0) {
        printf("model_viewer_test: %d models in %d blocks, names and words in %d languages\n",
               mv_modelCount, mv_blockCount, UI_LANG_COUNT);
    }
    return failures ? 1 : 0;
}
