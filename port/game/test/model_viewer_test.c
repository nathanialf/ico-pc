/* model_viewer_test.c: the model viewer's table and words
 * (port/game/model_viewer.h), on the CPU:
 *   - the motion-kind blocks are ordered, apart, inside the motion-kind and
 *     motion-orient tables;
 *   - each model's motions [motFirst, motLast) lie inside one block, with
 *     that block's motion-orient rows; a model without motions has none;
 *   - its host stage is one with data on the PAL disc (1 to 63, 103 to
 *     105; 88 and 91 have data but are the ending's), not the title; its
 *     kind, model and layout row inside the game's tables;
 *   - rows MV_ROW_ICO and MV_ROW_YORDA (Settings > Extras > Characters)
 *     are the boy and the girl;
 *   - its name, and the viewer's words, are non-empty in the six
 *     languages, different from the other models' in each, and every
 *     character has a glyph in the port font (ui_font_has_glyph);
 *   - the member rows (the early show title art: MvModel.member) are the
 *     table's last, kind 35 on the Cage's stage with no motions, a .p2o and
 *     the .tm2 it names; on the disc of the run (model_viewer_test [SLUG],
 *     a fake of its logo pack's directory, read from the ISOs) each one's
 *     id is its member's PackEnt.id there, the list offers the ones the
 *     pack holds (the European disc the copyright only, the June 2001 demo
 *     none) and mv_member_first_row the first of them; the ids are asked
 *     once per disc (none while no disc is mounted, again after a region
 *     change), and a member found in another pack is not offered.
 * Exit 0, 1 on a mismatch. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "df_pack.h"
#include "font.h"
#include "model_viewer.h"
#include "strings.h"
#include "vfs.h"

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
#define MODELS_JPN 1621 /* the Japanese disc's (data_schema.jpn.txt model-path) */
#define LAYOUT_ROWS 3759

static const char *const kLang[UI_LANG_COUNT] = {"en", "fr", "de", "it", "es", "ja", "ko", "zh"};

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
        const char *s = ui_str_in((UiLang)l, (UiStrId)id);
        CHECK(s != NULL && s[0] != '\0', "%s (%d): empty in %s", what, id, kLang[l]);
        if (s == NULL) {
            continue;
        }
        if (l != UI_LANG_EN) {
            /* a translation of its own, not the English fallback */
            const char *const *table[UI_LANG_COUNT] = {ui_strings_en, ui_strings_fr, ui_strings_de,
                                                       ui_strings_it, ui_strings_es, ui_strings_ja,
                                                       ui_strings_ko, ui_strings_zh};
            CHECK(table[l][id] != NULL, "%s (%d): no %s entry", what, id, kLang[l]);
        }
        const char *p = s;
        int32_t c;
        while ((c = next(&p)) > 0) {
            if (c == '\n') {
                continue;
            }
            CHECK(c >= 0x20 && ui_font_has_glyph((uint32_t)c), "%s (%d) in %s: U+%04X has no glyph",
                  what, id, kLang[l], (unsigned)c);
        }
        CHECK(c == 0, "%s (%d) in %s: malformed UTF-8", what, id, kLang[l]);
    }
}

static int dataStage(int s)
{
    return (s >= 2 && s <= 63) || (s >= 103 && s <= 105);
}

/* --- the disc: its logo pack's directory, per disc ----------------------- */

static const char *s_slug = "pal";
static int s_noDisc;    /* no disc mounted */
static int s_otherPack; /* the members found in another pack */
static int s_vfsByte;

IcoVfs *ico_vfs_disc(void)
{
    return s_noDisc ? NULL : (IcoVfs *)(void *)&s_vfsByte;
}

/* the art's PackEnt ids in STGLOG.DF (e3_ver, p_start, copy_right; -1 for
   none), read from each disc's ISO; the June 2001 demo has no STGLOG.DF */
static void artIds(const char *slug, int ids[3])
{
    static const struct {
        const char *slug;
        int ids[3];
    } kDiscs[] = {{"pal", {-1, -1, 1595}},      {"jpn", {1577, 1578, 1579}},
                  {"kor", {1577, 1578, 1579}},  {"cn", {1577, 1578, 1579}},
                  {"ntsc", {1544, 1545, 1546}}, {"aug6", {1539, 1540, 1541}},
                  {"jun26", {-1, -1, -1}}};

    ids[0] = ids[1] = ids[2] = -1;
    for (size_t i = 0; i < sizeof(kDiscs) / sizeof(kDiscs[0]); i++) {
        if (strcmp(kDiscs[i].slug, slug) == 0) {
            memcpy(ids, kDiscs[i].ids, sizeof(kDiscs[i].ids));
        }
    }
}

#define LOGO_PACK 2  /* STGLOG.DF's DATA.DF entry in the fake */
#define OTHER_PACK 5 /* another pack's */

int ico_df_find_member(IcoVfs *vfs, const char *name, IcoDfMember *out)
{
    static const char *const kArt[3] = {"object/sdf/e3/st13b/model/e3_ver.p2o",
                                        "object/sdf/e3/st13b/model/p_start.p2o",
                                        "object/sdf/e3/st13b/model/copy_right.p2o"};
    int ids[3];

    (void)vfs;
    memset(out, 0, sizeof(*out));
    artIds(s_slug, ids);
    for (int k = 0; k < 3; k++) {
        if (strcmp(name, kArt[k]) == 0 && ids[k] >= 0) {
            out->pack = s_otherPack ? OTHER_PACK : LOGO_PACK;
            out->size = 864;
            out->id = ids[k];
            out->kind = 4;
            return 0;
        }
    }
    return -1;
}

const char *ico_df_entry_name(IcoVfs *vfs, int i)
{
    (void)vfs;
    return i == LOGO_PACK ? "STGLOG.DF" : i == OTHER_PACK ? "STGST13C.DF" : NULL;
}

static int endsWith(const char *s, const char *tail)
{
    const size_t n = s != NULL ? strlen(s) : 0, t = strlen(tail);
    return n >= t && strcmp(s + n - t, tail) == 0;
}

/* the member rows on the run's disc */
static void checkMembers(void)
{
    int ids[3];
    int first = -1, members = 0, offered = 0, stageRows = 0;

    artIds(s_slug, ids);
    for (int i = 0; i < mv_modelCount; i++) {
        const MvModel *m = &mv_models[i];
        if (m->member == NULL) {
            CHECK(members == 0, "row %d: a stage row after the member rows", i);
            CHECK(m->texture == NULL, "row %d: a texture without a member", i);
            CHECK(mv_model_shown(i), "row %d: a stage row is always offered", i);
            stageRows++;
            continue;
        }
        const char *en = ui_str_in(UI_LANG_EN, (UiStrId)m->nameStr);
        CHECK(m->kind == 35 && m->label == -1 && m->stage == 31 && m->pal_id == -1 &&
                  m->jpn_id == -1 && m->motFirst == 0 && m->motLast == 0 && m->oriFrom == 0 &&
                  m->oriTo == 0,
              "%s: kind %d, label %d, stage %d, ids %d %d, motions %d..%d", en, m->kind, m->label,
              m->stage, m->pal_id, m->jpn_id, m->motFirst, m->motLast);
        /* the names as the logo pack's directory spells them: the TIM2s
           under model/../texture/, unnormalised (US MANIFEST.tsv rows 9-11),
           which is what ico_df_find_member compares */
        static const char kModelDir[] = "object/sdf/e3/st13b/model/";
        static const char kTexDir[] = "object/sdf/e3/st13b/model/../texture/";
        CHECK(endsWith(m->member, ".p2o") && endsWith(m->texture, ".tm2") &&
                  strncmp(m->member, kModelDir, sizeof kModelDir - 1) == 0 &&
                  strchr(m->member + sizeof kModelDir - 1, '/') == NULL && m->texture != NULL &&
                  strncmp(m->texture, kTexDir, sizeof kTexDir - 1) == 0 &&
                  strchr(m->texture + sizeof kTexDir - 1, '/') == NULL,
              "%s: members %s, %s", en, m->member, m->texture != NULL ? m->texture : "(none)");
        CHECK(members < 3, "%s: more than three member rows", en);
        const int want = members < 3 ? ids[members] : -1;
        CHECK(mv_model_id(m) == want, "%s on %s: id %d, want %d", en, s_slug, mv_model_id(m), want);
        CHECK(mv_model_shown(i) == (want >= 0), "%s on %s: offered %d", en, s_slug,
              mv_model_shown(i));
        if (want >= 0) {
            offered++;
            first = first < 0 ? i : first;
        }
        members++;
    }
    CHECK(members == 3 && stageRows == mv_modelCount - 3, "%d member rows, %d stage rows", members,
          stageRows);
    CHECK(mv_member_first_row() == first, "on %s: first art row %d, want %d", s_slug,
          mv_member_first_row(), first);
    const int wantOffered = strcmp(s_slug, "pal") == 0 ? 1 : strcmp(s_slug, "jun26") == 0 ? 0 : 3;
    CHECK(offered == wantOffered, "on %s: %d art rows offered, want %d", s_slug, offered,
          wantOffered);
    CHECK(!mv_model_shown(-1) && !mv_model_shown(mv_modelCount), "rows out of range");
}

static void testMembers(void)
{
    /* no disc mounted: -1, and nothing kept */
    s_noDisc = 1;
    CHECK(mv_model_id(&mv_models[mv_modelCount - 1]) == -1 && mv_member_lookups() == 0,
          "no disc: no id, nothing asked (%d)", mv_member_lookups());
    s_noDisc = 0;

    /* the disc: each member asked once */
    checkMembers();
    const int asked = mv_member_lookups();
    CHECK(asked == 3, "on %s: %d look-ups for 3 member rows", s_slug, asked);
    checkMembers();
    CHECK(mv_member_lookups() == asked, "asked once per disc (%d, then %d)", asked,
          mv_member_lookups());

    /* a region change asks again (one row asked on the other disc, then
       the three back on this one); members in another pack are not
       offered */
    const char *other = strcmp(s_slug, "jpn") == 0 ? "pal" : "jpn";
    const MvModel *last = &mv_models[mv_modelCount - 1];
    CHECK(ico_region_select(other) == 0, "the %s region", other);
    (void)mv_model_id(last);
    CHECK(mv_member_lookups() == asked + 1, "another region asked again (%d, want %d)",
          mv_member_lookups(), asked + 1);
    CHECK(ico_region_select(s_slug) == 0, "the %s region", s_slug);
    s_otherPack = 1;
    for (int i = 0; i < mv_modelCount; i++) {
        if (mv_models[i].member != NULL) {
            CHECK(mv_model_id(&mv_models[i]) == -1 && !mv_model_shown(i),
                  "row %d in another pack: not offered", i);
        }
    }
    CHECK(mv_member_first_row() == -1, "another pack: no first art row");
    CHECK(mv_member_lookups() == asked + 4, "back on %s: asked again (%d, want %d)", s_slug,
          mv_member_lookups(), asked + 4);
    s_otherPack = 0;
    CHECK(ico_region_select(other) == 0, "the %s region", other);
    (void)mv_model_id(last);
    CHECK(ico_region_select(s_slug) == 0, "the %s region", s_slug);
    checkMembers();
}

int main(int argc, char **argv)
{
    if (!ui_font_init()) {
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
        const char *en = ui_str_in(UI_LANG_EN, (UiStrId)m->nameStr);
        CHECK(m->nameStr >= UI_STR_MV_ICO && m->nameStr < UI_STR_COUNT, "model %d: name id %d", i,
              m->nameStr);
        checkText(m->nameStr, en);
        /* the list's label column: 560 px at size 24 (model_viewer.c; the model
         list has no value column) */
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            const char *nm = ui_str_in((UiLang)l, (UiStrId)m->nameStr);
            const float w = ui_measure_text(24.0f, nm);
            CHECK(w <= 560.0f, "%s in %s: %.1f px wide, the label column is 560", en, kLang[l], w);
        }
        CHECK(dataStage(m->stage), "%s: host stage %d", en, m->stage);
        CHECK(m->kind >= 1 && m->kind < KINDS, "%s: kind %d", en, m->kind);
        /* a member row's id is its pack's (checkMembers) */
        CHECK(m->member != NULL || (m->pal_id >= 0 && m->pal_id < MODELS), "%s: model %d", en,
              m->pal_id);
        /* the Japanese disc's modelData has 1621 rows (ctest model_viewer_jpn
           derives the column from both ELFs) */
        CHECK(m->member != NULL ||
                  (m->jpn_id >= 0 && m->jpn_id < MODELS_JPN && m->jpn_id <= m->pal_id),
              "%s: Japanese model %d", en, m->jpn_id);
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
                CHECK(strcmp(ui_str_in((UiLang)l, (UiStrId)mv_models[j].nameStr),
                             ui_str_in((UiLang)l, (UiStrId)m->nameStr)) != 0,
                      "%s: the same name as model %d in %s", en, j, kLang[l]);
            }
        }
    }

    /* Characters loads the table's first two rows: Ico (the boy,
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
        /* Characters inside the viewer (its Switch row, prompts) */
        UI_STR_CHAR_SWITCH_YORDA, UI_STR_CHAR_SWITCH_ICO, UI_STR_CHAR_HINT_COLOUR,
        UI_STR_CHAR_HINT_CHARACTER, UI_STR_VAL_ORIGINAL};
    for (unsigned i = 0; i < sizeof(kWords) / sizeof(kWords[0]); i++) {
        checkText(kWords[i], ui_str_in(UI_LANG_EN, (UiStrId)kWords[i]));
    }

    /* model_viewer_test [SLUG]: the member rows on that disc (the table
       above is the European disc's: the 2001 discs' motion tables are
       smaller, so their region is selected only here) */
    if (argc > 1) {
        s_slug = argv[1];
        CHECK(ico_region_select(argv[1]) == 0, "the %s region", argv[1]);
    }
    testMembers();

    ui_font_shutdown();
    if (failures == 0) {
        printf("model_viewer_test: %d models in %d blocks, names and words in %d languages; %s: "
               "first art row %d\n",
               mv_modelCount, mv_blockCount, UI_LANG_COUNT, s_slug, mv_member_first_row());
    }
    return failures ? 1 : 0;
}
