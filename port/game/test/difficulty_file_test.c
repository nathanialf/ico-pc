/*
 * port/game/test/difficulty_file_test.c
 *
 * The difficulty sidecar (port/game/difficulty_file.c, difficulty.toml) on
 * the CPU, on the disc named (its key prefix and preview layout), in a
 * folder of the test's own:
 *
 *   round trip   a save's record (rows, mode, journey, origin, the
 *                whole-journey count) written under the disc's key prefix
 *                and the card ("" / "b_") and read back by a load whose
 *                checksum matches; the preview tuple stored
 *   two cards    the same slot number on card 1 and card 2 keeps two records
 *   moved file   a load whose slot holds no entry with its checksum finds the
 *                entry by the checksum anywhere on the disc
 *   carry-over   a save with no entry takes the retired config keys once
 *                (origin config), written at once; the second load finds the
 *                entry (config changed in between: not read again), and a
 *                later save keeps the origin
 *   finished     a Permadeath game over writes the journey into [finished];
 *                the Continue list's preview match (the slot's own entry, a
 *                file moved to another slot or card, a newer save of the slot
 *                not refused) and the load's backstop by checksum; a load of
 *                such a save marks the record finished
 *   knock-downs  the whole-journey count at a save, written through as 0
 *                after the game over the limit causes
 *   damaged      a file that is no version 1 file reads as empty with a log
 *                line and is not written over by a load's carry-over; the
 *                next save keeps it as difficulty.toml.bad and writes a new
 *                file
 *   preview      the disc's preview words (port/core/region.h mc_preview_play
 *                / mc_preview_sofa: 1 and 2 on the 2001 discs, 2 and 3 on the
 *                others) are the ones layout_action.c's la_preview_play /
 *                la_preview_sofa read, and the save hook and the Continue list
 *                both read the preview through them
 *   hooks        layout_action.c: the save hook, both load twins' hook and
 *                backstop (before gflagKeepState), the Continue list's
 *                refusal in both twins, the three game over twins' Quit only;
 *                backup.c holds difficulty.toml (a text scan)
 *   log lines    found by slot, found by sum, carry-over, none, the writes,
 *                the refusals, the damaged file
 *
 *   difficulty_file_test DIR [REGION]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "difficulty.h"
#include "host_config.h"
#include "options.h"
#include "../../core/region.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static char s_dir[600];
static char s_config[700];
static char s_file[700];
static char s_bad[720];
static char s_log[700];

static const char *slug(void)
{
    return ico_region_cur()->slug;
}

static int y2001(void)
{
    return strcmp(slug(), "ntsc") == 0 || strcmp(slug(), "aug6") == 0 ||
           strcmp(slug(), "jun26") == 0;
}

/* the whole file at path (malloc'd, NULL: none) */
static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (len != NULL) {
        *len = 0;
    }
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)(n > 0 ? n : 0) + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        n = 0;
    }
    buf[n > 0 ? n : 0] = '\0';
    fclose(f);
    if (len != NULL) {
        *len = (size_t)(n > 0 ? n : 0);
    }
    return buf;
}

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        printf("cannot write %s\n", path);
        failures++;
        return;
    }
    fputs(text, f);
    fclose(f);
}

/* config.toml with TEXT as its body (NULL: no file), the options reloaded and
   the record back to the title's Normal */
static void use_config(const char *text)
{
    remove(s_config);
    if (text != NULL) {
        put(s_config, text);
    }
    ico_config_reset(s_config, "/nonexistent/difficulty_file_test.ini");
    ico_opt_reload();
    ico_difficulty_reset();
}

/* a fresh sidecar */
static void fresh(void)
{
    remove(s_file);
    remove(s_bad);
}

/* the value of an entry's field in the file as written ("save.<prefix><card>slot_N_<field>") */
static char s_value[1024];

static const char *file_value(int port, int slot, const char *field)
{
    IcoToml *t = ico_toml_load(s_file);
    char base[64], key[96];
    const char *v;

    s_value[0] = '\0';
    if (t == NULL) {
        return NULL;
    }
    snprintf(base, sizeof(base), "%sslot_%d_%s", port == 1 ? "b_" : "", slot, field);
    ico_region_key_prefixed("save", base, key, (int)sizeof(key));
    v = ico_toml_get(t, key);
    if (v != NULL) {
        snprintf(s_value, sizeof(s_value), "%s", v);
    }
    ico_toml_free(t);
    return v != NULL ? s_value : NULL;
}

static int file_finished(const IcoDifficulty *d)
{
    IcoToml *t = ico_toml_load(s_file);
    char id[20], key[40];
    int on;

    if (t == NULL) {
        return 0;
    }
    ico_difficulty_journey_text(d, id, sizeof(id));
    snprintf(key, sizeof(key), "finished.%s", id);
    on = ico_toml_get_bool(t, key, 0);
    ico_toml_free(t);
    return on;
}

static int same_journey(const IcoDifficulty *a, const IcoDifficulty *b)
{
    return a->journey[0] == b->journey[0] && a->journey[1] == b->journey[1];
}

static int same_rows(const IcoDifficulty *a, const IcoDifficulty *b)
{
    return memcmp(a->row, b->row, sizeof(a->row[0]) * ICO_DIFF_ROW_COUNT) == 0;
}

/* a New Game of the preset (with one row changed when row >= 0); returns the
   record as it began */
static IcoDifficulty start(IcoDiffMode mode, int row, int value)
{
    IcoDifficulty d;

    ico_difficulty_preset(mode, &d);
    if (row >= 0) {
        d.row[row] = value;
    }
    ico_difficulty_begin(&d);
    return *ico_difficulty_active();
}

static void test_round_trip(void)
{
    IcoDifficulty hard, easy;
    const IcoDifficulty *a;
    char key[96];

    use_config(NULL);
    fresh();
    hard = start(ICO_DIFF_HARD, -1, 0);
    ico_difficulty_slot_saved(0, 3, 1111u, 12, 345678u, 2);
    CHECK(ico_difficulty_active()->run_has_slot == 1 && ico_difficulty_active()->run_slot == 3 &&
          ico_difficulty_active()->run_port == 0 && ico_difficulty_active()->run_sum == 1111u);
    CHECK(file_value(0, 3, "sum") != NULL && strcmp(s_value, "1111") == 0);
    CHECK(file_value(0, 3, "preview") != NULL && strcmp(s_value, "12:345678:2") == 0);
    CHECK(file_value(0, 3, "mode") != NULL && strcmp(s_value, "hard") == 0);
    CHECK(file_value(0, 3, "origin") != NULL && strcmp(s_value, "new") == 0);
    CHECK(file_value(0, 3, "knocks") != NULL && strcmp(s_value, "0") == 0);
    CHECK(file_value(0, 3, "rows") != NULL && strstr(s_value, "knock_limit=3") != NULL);
    {
        char id[20];

        ico_difficulty_journey_text(&hard, id, sizeof(id));
        CHECK(file_value(0, 3, "journey") != NULL && strcmp(s_value, id) == 0);
    }
    /* the disc's own prefix: another disc's key is not this one */
    CHECK(ico_region_key_prefixed("save", "slot_3_sum", key, (int)sizeof(key)) == 0);
    CHECK(strcmp(slug(), "pal") == 0 ? strcmp(key, "save.slot_3_sum") == 0
                                     : strncmp(key + 5, slug(), strlen(slug())) == 0);

    /* the title, then the load */
    ico_difficulty_reset();
    CHECK(ico_difficulty_active()->mode == ICO_DIFF_NORMAL && !ico_difficulty_has_journey());
    ico_difficulty_slot_loaded(0, 3, 1111u);
    a = ico_difficulty_active();
    CHECK(a->mode == ICO_DIFF_HARD && a->origin == ICO_DIFF_ORIGIN_NEW);
    CHECK(same_rows(a, &hard) && same_journey(a, &hard));
    CHECK(a->run_port == 0 && a->run_slot == 3 && a->run_has_slot && a->run_sum == 1111u);
    CHECK(a->finished == 0 && ico_difficulty_finished() == 0);

    /* card 2 (b_): the same slot number keeps a record of its own */
    easy = start(ICO_DIFF_EASY, -1, 0);
    ico_difficulty_slot_saved(1, 3, 2222u, 30, 99u, 1);
    CHECK(file_value(1, 3, "sum") != NULL && strcmp(s_value, "2222") == 0);
    CHECK(file_value(1, 3, "mode") != NULL && strcmp(s_value, "easy") == 0);
    CHECK(file_value(0, 3, "mode") != NULL && strcmp(s_value, "hard") == 0);
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(1, 3, 2222u);
    a = ico_difficulty_active();
    CHECK(a->mode == ICO_DIFF_EASY && same_rows(a, &easy) && same_journey(a, &easy));
    CHECK(a->run_port == 1 && a->run_slot == 3);
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(0, 3, 1111u);
    CHECK(ico_difficulty_active()->mode == ICO_DIFF_HARD &&
          same_journey(ico_difficulty_active(), &hard));

    /* the file moved: card 1 slot 5 holds card 2 slot 3's save */
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(0, 5, 2222u);
    a = ico_difficulty_active();
    CHECK(a->mode == ICO_DIFF_EASY && same_journey(a, &easy) && a->origin == ICO_DIFF_ORIGIN_NEW);
    CHECK(a->run_port == 0 && a->run_slot == 5 && a->run_sum == 2222u);
    /* a load whose checksum matches no entry is not the slot's old record */
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(0, 3, 1112u);
    a = ico_difficulty_active();
    CHECK(a->origin == ICO_DIFF_ORIGIN_CONFIG && !same_journey(a, &hard));
    /* the carry-over replaced the slot's entry: the Hard save's preview is
       not this journey's */
    CHECK(file_value(0, 3, "preview") == NULL || s_value[0] == '\0');
    CHECK(file_value(0, 3, "origin") != NULL && strcmp(s_value, "config") == 0);
}

static void test_carry_over(void)
{
    IcoDifficulty first;
    const IcoDifficulty *a;

    fresh();
    use_config("[variants]\none_hit = true\n[gameplay]\nshadow_speed = \"faster\"\n");
    ico_difficulty_slot_loaded(0, 7, 3333u);
    a = ico_difficulty_active();
    first = *a;
    CHECK(a->origin == ICO_DIFF_ORIGIN_CONFIG && a->mode == ICO_DIFF_CUSTOM);
    CHECK(a->row[ICO_DIFF_ONE_HIT] == 1 && a->row[ICO_DIFF_SHADOW_SPEED] == ICO_SPEED_FASTER);
    CHECK(a->journey[0] != 0 || a->journey[1] != 0);
    /* written at once, with no preview yet (the next save gives it) */
    CHECK(file_value(0, 7, "origin") != NULL && strcmp(s_value, "config") == 0);
    CHECK(file_value(0, 7, "sum") != NULL && strcmp(s_value, "3333") == 0);
    CHECK(file_value(0, 7, "mode") != NULL && strcmp(s_value, "custom") == 0);
    CHECK(file_value(0, 7, "preview") == NULL || s_value[0] == '\0');

    /* config changes: the second load finds the entry, not the config */
    use_config(NULL);
    ico_difficulty_slot_loaded(0, 7, 3333u);
    a = ico_difficulty_active();
    CHECK(a->origin == ICO_DIFF_ORIGIN_CONFIG && a->mode == ICO_DIFF_CUSTOM);
    CHECK(same_rows(a, &first) && same_journey(a, &first));

    /* a later save keeps the origin */
    ico_difficulty_slot_saved(0, 7, 3334u, 5, 1234u, 0);
    CHECK(file_value(0, 7, "origin") != NULL && strcmp(s_value, "config") == 0);
    CHECK(file_value(0, 7, "preview") != NULL && strcmp(s_value, "5:1234:0") == 0);
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(0, 7, 3334u);
    a = ico_difficulty_active();
    CHECK(a->origin == ICO_DIFF_ORIGIN_CONFIG && same_journey(a, &first) && same_rows(a, &first));

    /* every key the game's own: Normal */
    use_config(NULL);
    ico_difficulty_slot_loaded(1, 8, 4321u);
    a = ico_difficulty_active();
    CHECK(a->origin == ICO_DIFF_ORIGIN_CONFIG && a->mode == ICO_DIFF_NORMAL);
    CHECK(file_value(1, 8, "mode") != NULL && strcmp(s_value, "normal") == 0);

    /* not a save slot: none, the record unchanged */
    first = *ico_difficulty_active();
    ico_difficulty_slot_loaded(0, 100, 1u);
    ico_difficulty_slot_loaded(2, 1, 1u);
    CHECK(same_journey(ico_difficulty_active(), &first));
}

static void test_finished(void)
{
    IcoDifficulty pd, other;
    const IcoDifficulty *a;

    use_config(NULL);
    fresh();
    /* nothing on file: nothing refused */
    CHECK(ico_difficulty_slot_finished(0, 4, 20, 1000u, 1) == 0);
    CHECK(ico_difficulty_sum_finished(4444u) == 0);

    pd = start(ICO_DIFF_NORMAL, ICO_DIFF_PERMADEATH, 1);
    ico_difficulty_slot_saved(0, 4, 4444u, 20, 1000u, 1);
    other = start(ICO_DIFF_HARD, -1, 0);
    ico_difficulty_slot_saved(0, 6, 6666u, 21, 2000u, 3);
    /* the Permadeath journey again, its game over */
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(0, 4, 4444u);
    CHECK(same_journey(ico_difficulty_active(), &pd) && ico_difficulty_finished() == 0);
    CHECK(ico_difficulty_slot_finished(0, 4, 20, 1000u, 1) == 0);
    ico_difficulty_game_over();
    CHECK(ico_difficulty_finished() == 1);
    CHECK(file_finished(&pd) == 1 && file_finished(&other) == 0);

    /* the Continue list: the slot's own entry by its preview */
    CHECK(ico_difficulty_slot_finished(0, 4, 20, 1000u, 1) == 1);
    /* the slot saved over since (another preview): not refused */
    CHECK(ico_difficulty_slot_finished(0, 4, 20, 1001u, 1) == 0);
    CHECK(ico_difficulty_slot_finished(0, 4, 19, 1000u, 1) == 0);
    CHECK(ico_difficulty_slot_finished(0, 4, 20, 1000u, 2) == 0);
    /* the file moved to card 2 slot 9: found by its preview */
    CHECK(ico_difficulty_slot_finished(1, 9, 20, 1000u, 1) == 1);
    /* another journey's save */
    CHECK(ico_difficulty_slot_finished(0, 6, 21, 2000u, 3) == 0);
    CHECK(ico_difficulty_slot_finished(0, 100, 20, 1000u, 1) == 0);

    /* the backstop by checksum */
    CHECK(ico_difficulty_sum_finished(4444u) == 1);
    CHECK(ico_difficulty_sum_finished(6666u) == 0);
    CHECK(ico_difficulty_sum_finished(4445u) == 0);

    /* a load of it all the same (the backstop missed): the record is finished */
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(0, 4, 4444u);
    a = ico_difficulty_active();
    CHECK(same_journey(a, &pd) && a->finished == 1 && ico_difficulty_finished() == 1);
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(0, 6, 6666u);
    CHECK(ico_difficulty_finished() == 0 && same_journey(ico_difficulty_active(), &other));
    /* a game over of a journey without Permadeath ends nothing */
    ico_difficulty_game_over();
    CHECK(ico_difficulty_finished() == 0 && file_finished(&other) == 0);
    CHECK(ico_difficulty_sum_finished(6666u) == 0);
}

static void test_knocks(void)
{
    IcoDifficulty d;
    const IcoDifficulty *a;

    use_config(NULL);
    fresh();
    ico_difficulty_preset(ICO_DIFF_NORMAL, &d);
    d.row[ICO_DIFF_KNOCK_LIMIT] = 2;
    d.row[ICO_DIFF_KNOCK_SCOPE] = ICO_DIFF_SCOPE_JOURNEY;
    ico_difficulty_begin(&d);
    d = *ico_difficulty_active();
    /* one knock-down, then back on his feet: the count 1 at the save */
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 0);
    ico_difficulty_tick(1, 0, 0);
    ico_difficulty_slot_saved(0, 2, 7777u, 3, 50u, 0);
    CHECK(file_value(0, 2, "knocks") != NULL && strcmp(s_value, "1") == 0);
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(0, 2, 7777u);
    a = ico_difficulty_active();
    CHECK(a->knocks_journey == 1 && same_journey(a, &d));
    /* the second knock-down reaches the limit; its game over starts the count
       again, in memory and in the run's entry */
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 1);
    CHECK(ico_difficulty_active()->knocks_journey == 2);
    ico_difficulty_game_over();
    CHECK(ico_difficulty_active()->knocks_journey == 0);
    CHECK(file_value(0, 2, "knocks") != NULL && strcmp(s_value, "0") == 0);
    CHECK(file_value(0, 2, "sum") != NULL && strcmp(s_value, "7777") == 0);
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(0, 2, 7777u);
    CHECK(ico_difficulty_active()->knocks_journey == 0);
    /* the slot now holds another journey's entry: not written through */
    start(ICO_DIFF_NORMAL, -1, 0);
    ico_difficulty_slot_saved(0, 2, 7778u, 3, 60u, 0);
    ico_difficulty_preset(ICO_DIFF_NORMAL, &d);
    d.row[ICO_DIFF_KNOCK_LIMIT] = 1;
    d.row[ICO_DIFF_KNOCK_SCOPE] = ICO_DIFF_SCOPE_JOURNEY;
    ico_difficulty_begin(&d);
    ico_difficulty_set_run(0, 2, 7778u);
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 1);
    ico_difficulty_game_over();
    CHECK(file_value(0, 2, "mode") != NULL && strcmp(s_value, "normal") == 0);
    CHECK(file_value(0, 2, "rows") != NULL && strstr(s_value, "knock_limit=0") != NULL);
}

static void test_damaged(void)
{
    static const char kGarbage[] = "\x01\x02 this is not the file\nslot = [\n";
    char *before, *after, *bad;
    size_t n_before, n_after;
    const IcoDifficulty *a;
    IcoDifficulty d;

    use_config("[variants]\nnever_dodge = true\n");
    fresh();
    put(s_file, kGarbage);
    before = slurp(s_file, &n_before);
    CHECK(ico_difficulty_slot_finished(0, 1, 1, 1u, 1) == 0);
    CHECK(ico_difficulty_sum_finished(1u) == 0);
    /* reads as empty: the carry-over, not written into the damaged file */
    ico_difficulty_slot_loaded(0, 1, 9999u);
    a = ico_difficulty_active();
    CHECK(a->origin == ICO_DIFF_ORIGIN_CONFIG && a->row[ICO_DIFF_NEVER_DODGE] == 1);
    after = slurp(s_file, &n_after);
    CHECK(before != NULL && after != NULL && n_before == n_after &&
          memcmp(before, after, n_before) == 0);
    free(after);
    /* the next save: the damaged text kept as .bad, a new file written */
    ico_difficulty_slot_saved(0, 1, 9999u, 2, 3u, 0);
    bad = slurp(s_bad, NULL);
    CHECK(bad != NULL && before != NULL && strcmp(bad, before) == 0);
    free(bad);
    free(before);
    CHECK(file_value(0, 1, "origin") != NULL && strcmp(s_value, "config") == 0);
    after = slurp(s_file, NULL);
    CHECK(after != NULL && strstr(after, "version = 1") != NULL && strstr(after, "\x01") == NULL);
    free(after);
    d = *ico_difficulty_active();
    ico_difficulty_reset();
    ico_difficulty_slot_loaded(0, 1, 9999u);
    CHECK(same_journey(ico_difficulty_active(), &d));

    /* a later version's file is not read either, and a Permadeath end keeps it
       as .bad before it writes */
    fresh();
    put(s_file, "version = 2\n[save]\nslot_1_sum = 5\n");
    CHECK(ico_difficulty_sum_finished(5u) == 0);
    start(ICO_DIFF_NORMAL, ICO_DIFF_PERMADEATH, 1);
    d = *ico_difficulty_active();
    ico_difficulty_game_over();
    bad = slurp(s_bad, NULL);
    CHECK(bad != NULL && strstr(bad, "version = 2") != NULL);
    free(bad);
    CHECK(file_finished(&d) == 1);
    /* an empty file is no damage */
    fresh();
    put(s_file, "\n");
    ico_difficulty_slot_loaded(0, 1, 77u);
    CHECK(file_value(0, 1, "sum") != NULL && strcmp(s_value, "77") == 0);
    {
        char *none = slurp(s_bad, NULL);

        CHECK(none == NULL);
        free(none);
    }
}

/* needle starts within `within` bytes of from */
static int near(const char *from, const char *needle, long within)
{
    const char *q = from != NULL ? strstr(from, needle) : NULL;

    return q != NULL && q - from <= within;
}

/* the save preview's words as layout_action.c reads them on this disc */
static void test_preview(void)
{
    const IcoRegionInfo *r = ico_region_cur();
    char *src = slurp(ICO_LAYOUT_ACTION_SRC, NULL);

    CHECK(r->mc_preview_play == (y2001() ? 1 : 2));
    CHECK(r->mc_preview_sofa == (y2001() ? 2 : 3));
    CHECK(src != NULL);
    if (src == NULL) {
        return;
    }
    /* words 1 and 2 are the host McPreview's cleared and playTime, word 3 its
       sofa: the 2001 discs keep play time and sofa one word earlier */
    CHECK(strstr(src, "return ICO_NTSCAUG6JUN26 ? p->cleared : p->playTime;") != NULL);
    CHECK(strstr(src, "return ICO_NTSCAUG6JUN26 ? p->playTime : p->sofa;") != NULL);
    CHECK(strstr(src, "int cleared;   /* 0x04 */") != NULL &&
          strstr(src, "int playTime;  /* 0x08, in frames */") != NULL &&
          strstr(src, "int sofa;      /* 0x0C */") != NULL);
    /* the save hook and the Continue list read the same words */
    {
        const char *saved = strstr(src, "static void la_host_difficulty_saved(void)");
        const char *refused = strstr(src, "static int la_host_load_refused(int port, int item)");
        const char *k = "(unsigned int)la_preview_play(p)";

        CHECK(saved != NULL && refused != NULL);
        if (saved != NULL && refused != NULL) {
            CHECK(near(saved, k, 600) && near(strstr(saved, k), "la_preview_sofa(p)", 80));
            CHECK(near(refused, k, 700) && near(strstr(refused, k), "la_preview_sofa(p)", 120));
            CHECK(near(saved, "IosMcProductFile[mc.port].file[mc.fileNo]", 200));
            CHECK(near(refused, "IosMcProductFile[port].file[item]", 400));
        }
    }
    free(src);
}

static int count(const char *hay, const char *needle)
{
    int n = 0;
    const char *p = hay;

    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += strlen(needle);
    }
    return n;
}

/* each occurrence of `a` is followed by `b` within `within` bytes */
static int each_followed(const char *hay, const char *a, const char *b, long within)
{
    const char *p = hay;
    int n = 0;

    while ((p = strstr(p, a)) != NULL) {
        const char *q = strstr(p, b);

        if (q == NULL || q - p > within) {
            return -1;
        }
        n++;
        p += strlen(a);
    }
    return n;
}

static void test_hooks(void)
{
    char *src = slurp(ICO_LAYOUT_ACTION_SRC, NULL);
    char *bk = slurp(ICO_BACKUP_SRC, NULL);

    CHECK(src != NULL && bk != NULL);
    if (src != NULL) {
        /* the save hook, beside the achievements' */
        CHECK(count(src, "la_host_difficulty_saved();") == 1);
        CHECK(each_followed(src, "ico_ach_slot_saved(mc.fileNo, (unsigned int)mc.sum);",
                            "la_host_difficulty_saved();", 120) == 1);
        /* both load twins, beside the achievements' */
        CHECK(count(src, "ico_difficulty_slot_loaded(mc.port, mc.fileNo, (unsigned int)mc.sum);") ==
              2);
        CHECK(each_followed(src, "ico_ach_slot_loaded(mc.fileNo, (unsigned int)mc.sum);",
                            "ico_difficulty_slot_loaded(mc.port, mc.fileNo", 160) == 2);
        /* the backstop of each load twin comes before gflagKeepState and goes
           back to the file list */
        CHECK(each_followed(src, "if (ico_difficulty_sum_finished((unsigned int)mc.sum)) {",
                            "gflagKeepState();", 400) == 2);
        CHECK(each_followed(src, "if (ico_difficulty_sum_finished((unsigned int)mc.sum)) {",
                            "loadStep = 0;", 80) == 2);
        CHECK(strstr(src, "return PAL_JPNKORCN_NTSCAUG6JUN26(0x13, 0x10, 16);\n        }\n"
                          "        gflagKeepState();") != NULL);
        CHECK(strstr(src, "return 16;\n        }\n        gflagKeepState();") != NULL);
        /* the Continue list's refusal in both twins, with the negative sound,
           before the positive one */
        CHECK(each_followed(src, "if (la_host_load_refused(filePort, item)) {", "NEGATIVE_SE();",
                            40) == 2);
        CHECK(strstr(src,
                     "if (la_host_load_refused(filePort, item)) {\n            NEGATIVE_SE();\n"
                     "            return -1;\n        }\n        POSITIVE_SE();") != NULL);
        /* the three game over twins: Quit only, and Continue acts as Quit */
        CHECK(count(src, "la_host_game_over_quit_only(") == 4);
        CHECK(strstr(src, "static int la_game_over_continue_jun26(int first)\n{\n"
                          "    la_host_game_over_quit_only(378);") != NULL);
        CHECK(strstr(src, "static int la_game_over_continue_ntscaug6(int first)\n{\n"
                          "    la_host_game_over_quit_only(378);") != NULL);
        CHECK(strstr(src, "la_host_game_over_quit_only(429 - PAL_JPNCN_KOR(0, 23, 29));") != NULL);
        CHECK(count(src, "if (ico_unused_active() || ico_difficulty_finished()) {") == 3);
        CHECK(count(src, "if (ico_unused_active()) {") == 0);
        /* the Continue property's ids are the ones the twins switch on */
        CHECK(count(src, "case 378:") == 2 && count(src, "case 429:") == 1 &&
              count(src, "case 379:") == 2 && count(src, "case 430:") == 1);
    }
    if (bk != NULL) {
        CHECK(count(bk, "\"difficulty.toml\"") == 2);
    }
    free(src);
    free(bk);
}

static void test_log(void)
{
    char *log;

    fflush(stderr);
    log = slurp(s_log, NULL);
    CHECK(log != NULL);
    if (log == NULL) {
        return;
    }
    CHECK(strstr(log, "): found by slot: Hard, journey ") != NULL);
    CHECK(strstr(log, "): found by sum at card 2 slot 3 (the file was moved or copied)") != NULL);
    CHECK(strstr(log, "): no entry on this disc (") != NULL && strstr(log, "carry-over: ") != NULL);
    CHECK(strstr(log, "difficulty: load of card 1 slot 100: none (not a save slot)") != NULL);
    CHECK(strstr(log, "difficulty: wrote ") != NULL &&
          strstr(log, "card 1 slot 3 saved: Hard") != NULL);
    CHECK(strstr(log, "difficulty: the Continue list refuses card 1 slot 4 (preview 20:1000:1)") !=
          NULL);
    CHECK(strstr(log, "difficulty: the Continue list refuses card 2 slot 9 (preview 20:1000:1)") !=
          NULL);
    CHECK(strstr(log, "difficulty: the load refuses the save with sum 4444") != NULL);
    CHECK(strstr(log, "is damaged or from a later version (version missing)") != NULL);
    CHECK(strstr(log, "is damaged or from a later version (version 2)") != NULL);
    CHECK(strstr(log, "the carry-over is not written into the damaged file") != NULL);
    CHECK(strstr(log, " ended)") != NULL);
    CHECK(strstr(log, "the journey's knock-downs back to 0") != NULL);
    CHECK(strstr(log, "holds another journey's entry or none") != NULL);
    if (failures != 0) {
        printf("--- the log ---\n%s--- end of the log ---\n", log);
    }
    free(log);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";

    if (argc > 2 && ico_region_select(argv[2]) != 0) {
        printf("difficulty_file_test: no region %s\n", argv[2]);
        return 1;
    }
    snprintf(s_dir, sizeof(s_dir), "%s", dir);
    snprintf(s_config, sizeof(s_config), "%s/config.toml", s_dir);
    snprintf(s_file, sizeof(s_file), "%s/difficulty.toml", s_dir);
    snprintf(s_bad, sizeof(s_bad), "%s.bad", s_file);
    snprintf(s_log, sizeof(s_log), "%s/difficulty_file_test.log", s_dir);
    if (freopen(s_log, "w", stderr) == NULL) {
        printf("difficulty_file_test: cannot write %s\n", s_log);
        return 1;
    }
    setvbuf(stderr, NULL, _IONBF, 0);
    ico_difficulty_file_init(s_dir);

    test_round_trip();
    test_carry_over();
    test_finished();
    test_knocks();
    test_damaged();
    test_preview();
    test_hooks();
    test_log();

    fresh();
    remove(s_config);
    if (failures != 0) {
        printf("difficulty_file_test (%s): %d failure(s)\n", slug(), failures);
        return 1;
    }
    printf("difficulty_file_test (%s): ok\n", slug());
    return 0;
}
