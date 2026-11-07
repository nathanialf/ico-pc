/*
 * port/game/test/options_test.c
 *
 * The gameplay options on the CPU: the defaults are
 * the original game's, the config and run-time setters, and eBrainGetTarget
 * (ico2/omori/src/ebrain.c, compiled in below so its statics are in reach)
 * with yorda_safe off and on, on a synthetic slot set.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "config.h"
#include "options.h"
/* the region table (port/core/region.h, by its path: the game's include
   directories hold the game's own region.h) */
#include "../../core/region.h"
#include "video_options.h"
/* the enemy brain, as the game's own source; stubs below satisfy it */
#include "../../../ico2/omori/src/ebrain.c"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- what ebrain.c needs ------------------------------------------------- */
static GObj s_boy, s_girl, s_enemy;

GObj *boyGObj, *girlGObj;

static int s_sees_boy, s_sees_girl;

int ACTCheckViewCl(struct GObj *self, void *target, void *targetPos, int range, float f)
{
    return target == &s_boy ? s_sees_boy : target == &s_girl ? s_sees_girl : 0;
}

void GetRootPosition(void *out, struct GObj *g)
{
    memset(out, 0, 16);
}

/* the rest are reached only from eBrainProcess and the generator lookups */
const StgPre stageData[1];

GenGeo objLayout[1];

void debug_StdPrintfDummy(const char *fmt, ...) {}

int IsBoyStatus_EnemyMustWait(void)
{
    return 0;
}

void sceVu0SubVector(void *dst, void *a, void *b) {}

float sceVu0InnerProduct(void *a, void *b)
{
    return 0.0f;
}

int GetMotherGenerator(int label)
{
    return -1;
}

void *isysGObjSearchFromObjLayoutID(int layoutId)
{
    return 0;
}

void debug_assert(const char *f, int l) {}

void __assert(const char *f, int l, const char *e) {}

/* One slot in the lists of both targets; the girl is the nearer. */
static EBSlot *setup(int status, int message, int chase)
{
    EBSlot *p;

    eBrainInit();
    boyGObj = &s_boy;
    girlGObj = &s_girl;
    p = (EBSlot *)eBrainStatusSet(&s_enemy, 4);
    p->status = (unsigned short)status;
    p->message = message;
    p->chaseFrames = chase;
    p->dist[0] = 1000.0f;
    p->dist[1] = 500.0f;
    boyTargets[0] = girlTargets[0] = p;
    boyTargetNum = girlTargetNum = 1;
    return p;
}

static void test_defaults(void)
{
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
    /* the stick fix starts off on every platform; with no config.toml the
       option is that default */
    CHECK(ico_opt_stick_fix_default_for(1) == 0);
    CHECK(ico_opt_stick_fix_default_for(0) == 0);
    CHECK(ico_opt_stick_fix_default() == 0);
    CHECK(ico_opt_stick_fix() == ico_opt_stick_fix_default());
    CHECK(ico_opt_yorda_safe() == 0);
    CHECK(ico_opt_pit_time() == ICO_PIT_ORIGINAL && ico_opt_pit_time_factor() == 1.0f);
    CHECK(ico_opt_shadow_speed() == ICO_SPEED_ORIGINAL && ico_opt_shadow_speed_factor() == 1.0f);
    CHECK(ico_opt_door_fix() == 0);
    CHECK(ico_opt_mirror() == 0);
    CHECK(ico_opt_developer_mode() == 0);
    CHECK(ico_opt_debug_option() == 0);
    /* the doorway fix: off by default, the setter keeps 0 or 1, a reload
       goes back to the config's (absent: off) */
    ico_opt_set_door_fix(7);
    CHECK(ico_opt_door_fix() == 1);
    ico_opt_reload();
    CHECK(ico_opt_door_fix() == 0);
    /* Yorda trips: the same (off, the setter's 0 or 1, back on reload) */
    CHECK(ico_opt_yorda_trips() == 0);
    ico_opt_set_yorda_trips(6);
    CHECK(ico_opt_yorda_trips() == 1);
    ico_opt_reload();
    CHECK(ico_opt_yorda_trips() == 0);
    /* Natural chains: the same (off, the setter's 0 or 1, back on reload) */
    CHECK(ico_opt_natural_chains() == 0);
    ico_opt_set_natural_chains(9);
    CHECK(ico_opt_natural_chains() == 1);
    ico_opt_reload();
    CHECK(ico_opt_natural_chains() == 0);
    /* Skip the opening: the same (off, the setter's 0 or 1, back on reload) */
    CHECK(ico_opt_skip_opening() == 0);
    ico_opt_set_skip_opening(4);
    CHECK(ico_opt_skip_opening() == 1);
    ico_opt_set_skip_opening(0);
    CHECK(ico_opt_skip_opening() == 0);
    ico_opt_set_skip_opening(1);
    ico_opt_reload();
    CHECK(ico_opt_skip_opening() == 0);
    /* Second memory card: the same (off, the setter's 0 or 1, back on reload) */
    CHECK(ico_opt_second_memory_card() == 0);
    ico_opt_set_second_memory_card(3);
    CHECK(ico_opt_second_memory_card() == 1);
    ico_opt_reload();
    CHECK(ico_opt_second_memory_card() == 0);
    /* Hold type: the disc's own by default (the region row's hold_type_own),
       the setter keeps 0 or 1, a reload goes back to the config's (absent:
       the disc's own) */
    CHECK(ico_opt_hold_choice() == ICO_HOLD_DISC);
    CHECK(ico_opt_hold_type() == ico_region_cur()->hold_type_own);
    ico_opt_set_hold_type(5);
    CHECK(ico_opt_hold_type() == ICO_HOLD_TOGGLE && ico_opt_hold_choice() == ICO_HOLD_TOGGLE);
    ico_opt_set_hold_type(0);
    CHECK(ico_opt_hold_type() == ICO_HOLD_HOLD && ico_opt_hold_choice() == ICO_HOLD_HOLD);
    ico_opt_set_hold_choice(7);
    CHECK(ico_opt_hold_choice() == ICO_HOLD_DISC);
    ico_opt_set_hold_type(1);
    ico_opt_reload();
    CHECK(ico_opt_hold_choice() == ICO_HOLD_DISC);
    CHECK(ico_opt_hold_type() == ico_region_cur()->hold_type_own);
    ico_opt_set_developer_mode(3);
    CHECK(ico_opt_developer_mode() == 1);
    ico_opt_set_yorda_safe(5);
    CHECK(ico_opt_yorda_safe() == 1);
    ico_opt_set_yorda_safe(0);
    CHECK(ico_opt_yorda_safe() == 0);
    ico_opt_set_pit_time(30);
    CHECK(ico_opt_pit_time() == 30 && ico_opt_pit_seconds() == 30);
    ico_opt_set_pit_time(8);
    CHECK(ico_opt_pit_time() == 8 && ico_opt_pit_seconds() == 8);
    ico_opt_set_pit_time(99); /* held to the range */
    CHECK(ico_opt_pit_time() == ICO_PIT_MAX_SECONDS);
    ico_opt_set_pit_time(1);
    CHECK(ico_opt_pit_time() == ICO_PIT_MIN_SECONDS);
    ico_opt_set_pit_time(ico_opt_pit_base_seconds()); /* the base is Original */
    CHECK(ico_opt_pit_time() == ICO_PIT_ORIGINAL && ico_opt_pit_time_factor() == 1.0f);
    ico_opt_set_pit_time(ICO_PIT_ORIGINAL);
    CHECK(ico_opt_pit_time() == ICO_PIT_ORIGINAL && ico_opt_pit_seconds() == 15);
    ico_opt_set_stick_fix(1);
    ico_opt_set_mirror(1);
    CHECK(ico_opt_stick_fix() == 1 && ico_opt_mirror() == 1);
    ico_opt_reload();
    CHECK(ico_opt_stick_fix() == ico_opt_stick_fix_default() && ico_opt_mirror() == 0);
    CHECK(ico_opt_developer_mode() == 0);
}

/* the Second memory card's listener (main_host.c points it at port 1) */
static int s_card_heard = -1, s_card_count;

static void card_listener(int on)
{
    s_card_heard = on;
    s_card_count++;
}

static void test_config(const char *dir)
{
    char path[512];
    FILE *f;

    snprintf(path, sizeof(path), "%s/options_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[gameplay]\nstick_fix = true\nyorda_safe = true\ndoor_fix = true\nmirror = true\n"
          "developer_mode = true\nnatural_chains = true\nskip_opening = true\n"
          "yorda_trips = true\nsecond_memory_card = true\n[dev]\n"
          "debug_option = 1\n",
          f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_stick_fix() == 1);
    CHECK(ico_opt_yorda_safe() == 1);
    CHECK(ico_opt_door_fix() == 1);
    CHECK(ico_opt_natural_chains() == 1);
    CHECK(ico_opt_skip_opening() == 1);
    CHECK(ico_opt_yorda_trips() == 1);
    CHECK(ico_opt_second_memory_card() == 1);
    /* its listener hears the current value at registration, then each set
       (a toggle puts the card in or takes it out at once) */
    ico_opt_set_second_memory_card_listener(card_listener);
    CHECK(s_card_heard == 1 && s_card_count == 1);
    ico_opt_set_second_memory_card(0);
    CHECK(s_card_heard == 0 && s_card_count == 2 && ico_opt_second_memory_card() == 0);
    ico_opt_set_second_memory_card(1);
    CHECK(s_card_heard == 1 && s_card_count == 3);
    ico_opt_set_second_memory_card_listener(NULL);
    ico_opt_set_second_memory_card(0);
    CHECK(s_card_count == 3);
    CHECK(ico_opt_mirror() == 1);
    /* developer mode and the debug option file switch */
    CHECK(ico_opt_developer_mode() == 1);
    CHECK(ico_opt_debug_option() == 1);
    /* hold_type: absent in this file reads the disc's own; "toggle" reads
       1; an unknown word reads the disc's own; the names round trip */
    CHECK(ico_opt_hold_choice() == ICO_HOLD_DISC);
    CHECK(ico_opt_hold_type() == ico_region_cur()->hold_type_own);
    CHECK(strcmp(ico_opt_hold_type_name(ICO_HOLD_HOLD), "hold") == 0);
    CHECK(strcmp(ico_opt_hold_type_name(ICO_HOLD_TOGGLE), "toggle") == 0);
    CHECK(strcmp(ico_opt_hold_type_name(ICO_HOLD_DISC), "disc") == 0);
    ico_opt_set_yorda_safe(0); /* the run-time value wins until a reload */
    CHECK(ico_opt_yorda_safe() == 0);
    remove(path);
    /* pit_time: seconds, "original", the earlier words (that many times the disc's 15.4 s,
       in whole seconds, held to 4..30), an unknown word reads original */
    {
        static const char *const names[] = {"half",  "original", "longer", "double", "triple",
                                            "bogus", "8",        "4",      "30",     "2",
                                            "45",    "15",       "8x"};
        static const int want[] = {8, 0, 23, 30, 30, 0, 8, 4, 30, 4, 30, 0, 0};
        int i;

        for (i = 0; i < 13; i++) {
            f = fopen(path, "wb");
            if (f == NULL) {
                failures++;
                break;
            }
            fprintf(f, "[gameplay]\npit_time = \"%s\"\n", names[i]);
            fclose(f);
            ico_config_reset(path, "/nonexistent/options_test.ini");
            ico_opt_reload();
            CHECK(ico_opt_pit_time() == want[i]);
        }
        /* an integer in the file, as the Settings menu writes it */
        f = fopen(path, "wb");
        if (f != NULL) {
            fprintf(f, "[gameplay]\npit_time = 8\n");
            fclose(f);
            ico_config_reset(path, "/nonexistent/options_test.ini");
            ico_opt_reload();
            CHECK(ico_opt_pit_time() == 8);
        } else {
            failures++;
        }
        remove(path);
    }
    /* shadow_speed: each name reads its factor; an unknown word reads original */
    {
        static const char *const names[] = {"slower", "original", "faster", "fastest", "bogus"};
        static const float want[] = {0.75f, 1.0f, 1.25f, 1.5f, 1.0f};
        int i;

        for (i = 0; i < 5; i++) {
            f = fopen(path, "wb");
            if (f == NULL) {
                failures++;
                break;
            }
            fprintf(f, "[gameplay]\nshadow_speed = \"%s\"\n", names[i]);
            fclose(f);
            ico_config_reset(path, "/nonexistent/options_test.ini");
            ico_opt_reload();
            CHECK(ico_opt_shadow_speed_factor() == want[i]);
            CHECK(ico_opt_shadow_speed() == (i < 4 ? i : ICO_SPEED_ORIGINAL));
        }
        remove(path);
    }
    /* debug_option is an int; a non-number or an absent key reads 0 */
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[dev]\ndebug_option = 7\n", f);
        fclose(f);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_opt_reload();
        CHECK(ico_opt_debug_option() == 7);
        CHECK(ico_opt_developer_mode() == 0);
        remove(path);
    }
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[dev]\ndebug_option = \"yes\"\n", f);
        fclose(f);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_opt_reload();
        CHECK(ico_opt_debug_option() == 0);
        remove(path);
    }
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[gameplay]\nhold_type = \"toggle\"\n", f);
        fclose(f);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_opt_reload();
        CHECK(ico_opt_hold_type() == ICO_HOLD_TOGGLE);
        CHECK(strcmp(ico_opt_hold_type_name(ico_opt_hold_type()), "toggle") == 0);
        remove(path);
    }
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[gameplay]\nhold_type = \"sideways\"\n", f);
        fclose(f);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_opt_reload();
        CHECK(ico_opt_hold_choice() == ICO_HOLD_DISC);
        CHECK(ico_opt_hold_type() == ico_region_cur()->hold_type_own);
        remove(path);
    }
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
}

/* [gameplay] hold_type per disc: "disc" (and no key) is A on the European,
   Japanese, Korean and Asian discs and B on the three 2001 discs (their
   game always toggles); "hold" and "toggle" are the same on every disc,
   except that an earlier version's "hold" (no hold_type_chosen beside it:
   every new config.toml before 0.9.1 had it) is the disc's own; the store
   writes the word and hold_type_chosen.  Each slug selected in turn, the
   run's own disc again at the end. */
static void test_hold_discs(const char *dir)
{
    static const struct {
        const char *slug;
        int own;
    } kDiscs[] = {{"pal", ICO_HOLD_HOLD},    {"jpn", ICO_HOLD_HOLD},    {"kor", ICO_HOLD_HOLD},
                  {"ntsc", ICO_HOLD_TOGGLE}, {"aug6", ICO_HOLD_TOGGLE}, {"jun26", ICO_HOLD_TOGGLE},
                  {"cn", ICO_HOLD_HOLD}};

    static const struct {
        const char *text; /* the file's [gameplay] lines */
        int choice;       /* ICO_HOLD_*, -1: the disc's own type */
    } kFiles[] = {
        {"", -1},
        {"hold_type = \"disc\"\n", -1},
        {"hold_type = \"hold\"\n", -1},
        {"hold_type = \"hold\"\nhold_type_chosen = false\n", -1},
        {"hold_type = \"hold\"\nhold_type_chosen = true\n", ICO_HOLD_HOLD},
        {"hold_type = \"toggle\"\n", ICO_HOLD_TOGGLE},
        {"hold_type = \"disc\"\nhold_type_chosen = true\n", -1},
    };

    const char *mine = ico_region_cur()->slug;
    char path[512];

    snprintf(path, sizeof(path), "%s/options_test_hold.toml", dir);
    for (unsigned d = 0; d < sizeof(kDiscs) / sizeof(kDiscs[0]); d++) {
        CHECK(ico_region_select(kDiscs[d].slug) == 0);
        CHECK(ico_region_cur()->hold_type_own == kDiscs[d].own);
        for (unsigned k = 0; k < sizeof(kFiles) / sizeof(kFiles[0]); k++) {
            FILE *f = fopen(path, "wb");

            CHECK(f != NULL);
            if (f == NULL) {
                continue;
            }
            fprintf(f, "[gameplay]\n%s", kFiles[k].text);
            fclose(f);
            ico_config_reset(path, "/nonexistent/options_test.ini");
            ico_opt_reload();
            if (kFiles[k].choice < 0) {
                CHECK(ico_opt_hold_choice() == ICO_HOLD_DISC);
                CHECK(ico_opt_hold_type() == kDiscs[d].own);
            } else {
                CHECK(ico_opt_hold_choice() == kFiles[k].choice);
                CHECK(ico_opt_hold_type() == kFiles[k].choice);
            }
            remove(path);
        }
        /* the store: the word and hold_type_chosen; read back, the choice */
        ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
        ico_opt_reload();
        ico_opt_set_hold_choice(ICO_HOLD_HOLD);
        CHECK(ico_opt_hold_choice_store() == 0);
        CHECK(strcmp(ico_config_get_string("gameplay.hold_type", ""), "hold") == 0);
        CHECK(ico_config_get_bool("gameplay.hold_type_chosen", 0) == 1);
        ico_opt_reload();
        CHECK(ico_opt_hold_type() == ICO_HOLD_HOLD);
        ico_opt_set_hold_choice(ICO_HOLD_DISC);
        CHECK(ico_opt_hold_choice_store() == 0);
        ico_opt_reload();
        CHECK(ico_opt_hold_choice() == ICO_HOLD_DISC && ico_opt_hold_type() == kDiscs[d].own);
    }
    CHECK(ico_region_select(mine) == 0);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
}

/* [ui] language: auto (-1) by default and for a value that is not one of
   the eight and the community translations; each name its UiLang number
   (port/ui/strings.h order); the setter and the names the Settings menu
   writes */
static void test_ui_language(const char *dir)
{
    enum { N = ICO_OPT_UI_LANGUAGE_COUNT };

    static const char *const names[N] = {"en", "fr", "de",        "it",          "es",   "ja",
                                         "ko", "zh", "ru-bobttz", "ru-cobra838", "pt-br"};
    char path[512];
    char text[64];
    FILE *f;

    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_ui_language() == -1);
    CHECK(strcmp(ico_opt_ui_language_name(-1), "auto") == 0);
    snprintf(path, sizeof(path), "%s/options_test.toml", dir);
    for (int i = 0; i < N + 2; i++) {
        const char *v = i < N ? names[i] : i == N ? "auto" : "klingon";
        f = fopen(path, "wb");
        if (f == NULL) {
            fprintf(stderr, "cannot write %s\n", path);
            failures++;
            return;
        }
        snprintf(text, sizeof(text), "[ui]\nlanguage = \"%s\"\n", v);
        fputs(text, f);
        fclose(f);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_opt_reload();
        CHECK(ico_opt_ui_language() == (i < N ? i : -1));
        CHECK(strcmp(ico_opt_ui_language_name(ico_opt_ui_language()), i < N ? v : "auto") == 0);
    }
    remove(path);
    ico_opt_set_ui_language(5);
    CHECK(ico_opt_ui_language() == 5);
    ico_opt_set_ui_language(N);
    CHECK(ico_opt_ui_language() == -1);
    ico_opt_set_ui_language(7);
    CHECK(ico_opt_ui_language() == 7);
    ico_opt_set_ui_language(8);
    CHECK(ico_opt_ui_language() == 8 && strcmp(ico_opt_ui_language_name(8), "ru-bobttz") == 0);
    ico_opt_set_ui_language(9);
    CHECK(ico_opt_ui_language() == 9 && strcmp(ico_opt_ui_language_name(9), "ru-cobra838") == 0);
    ico_opt_set_ui_language(10);
    CHECK(ico_opt_ui_language() == 10 && strcmp(ico_opt_ui_language_name(10), "pt-br") == 0);
    ico_opt_set_ui_language(-5);
    CHECK(ico_opt_ui_language() == -1);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_ui_language() == -1);
}

/* [game] text_language: "" (-1, none) by default and for a word that is not
   a community translation (a disc language among them: those are [game]
   language's); "ru-bobttz", "ru-cobra838" and "pt-br" their UiLang numbers
   (8, 9 and 10, the community translations); the setter takes only a community translation; the names
   the Language row writes; a reload reads the file again */
static void test_text_language(const char *dir)
{
    static const char *const words[] = {
        "",         "ru-bobttz", "ru-cobra838", "en", "ja",    "auto",  "ru",      "RU-BOBTTZ",
        "cobra838", "ru-cobra",  "pt-br",       "pt", "PT-BR", "pt_br", "ichagas7"};
    static const int want[] = {-1, 8, 9, -1, -1, -1, -1, -1, -1, -1, 10, -1, -1, -1, -1};
    char path[512];
    char text[96];
    FILE *f;

    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_text_language() == -1);
    CHECK(strcmp(ico_opt_text_language_name(-1), "") == 0);
    CHECK(ICO_OPT_TEXT_LANGUAGE_FIRST == 8 && ICO_OPT_UI_LANGUAGE_COUNT == 11);
    snprintf(path, sizeof(path), "%s/options_test.toml", dir);
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        f = fopen(path, "wb");
        if (f == NULL) {
            fprintf(stderr, "cannot write %s\n", path);
            failures++;
            return;
        }
        snprintf(text, sizeof(text), "[game]\ntext_language = \"%s\"\n", words[i]);
        fputs(text, f);
        fclose(f);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_opt_reload();
        CHECK(ico_opt_text_language() == want[i]);
        CHECK(strcmp(ico_opt_text_language_name(ico_opt_text_language()),
                     want[i] < 0 ? "" : words[i]) == 0);
        /* not the menu language's key: [ui] language stays auto */
        CHECK(ico_opt_ui_language() == -1);
    }
    remove(path);
    ico_opt_set_text_language(8);
    CHECK(ico_opt_text_language() == 8 && strcmp(ico_opt_text_language_name(8), "ru-bobttz") == 0);
    ico_opt_set_text_language(9);
    CHECK(ico_opt_text_language() == 9 &&
          strcmp(ico_opt_text_language_name(9), "ru-cobra838") == 0);
    ico_opt_set_text_language(10);
    CHECK(ico_opt_text_language() == 10 && strcmp(ico_opt_text_language_name(10), "pt-br") == 0);
    for (int l = -2; l < 8; l++) {
        ico_opt_set_text_language(l);
        CHECK(ico_opt_text_language() == -1 && strcmp(ico_opt_text_language_name(l), "") == 0);
    }
    ico_opt_set_text_language(11);
    CHECK(ico_opt_text_language() == -1);
    ico_opt_set_text_language(8);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_text_language() == -1);
}

/* --- pit_time: the dive ratio and the hook in actEnemyKidnapEnd ---------- */

/* the number of times `needle` occurs in the text */
static int count_of(const char *text, const char *needle)
{
    int n = 0;
    const char *p = text;

    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += strlen(needle);
    }
    return n;
}

static void test_pit_time(void)
{
    const char *slug = ico_region_cur()->slug;
    char *text = NULL;
    FILE *f = fopen(ICO_ENEMY_ACT_SRC, "rb");
    long n = 0;
    char hook[160];
    int first;
    int v;
    int note;

    /* the disc's own pull-in: 462 ticks (15.4 s) on the released discs, 463 on the June
       2001 demo; 15 s in whole seconds on both (pit_dive_base_test reads them off each
       disc) */
    CHECK(ico_opt_pit_chain_ticks_for("pal") == 462 && ico_opt_pit_chain_ticks_for("jpn") == 462 &&
          ico_opt_pit_chain_ticks_for("kor") == 462 && ico_opt_pit_chain_ticks_for("cn") == 462 &&
          ico_opt_pit_chain_ticks_for("ntsc") == 462 &&
          ico_opt_pit_chain_ticks_for("aug6") == 462 &&
          ico_opt_pit_chain_ticks_for("jun26") == 463);
    CHECK(ico_opt_pit_chain_ticks() == ico_opt_pit_chain_ticks_for(slug));
    CHECK(ico_opt_pit_base_seconds() == 15);
    /* the parser, for each length */
    CHECK(ico_opt_pit_parse("half", 462, &note) == 8 && note == 0);    /* 231 ticks: 7.7 s */
    CHECK(ico_opt_pit_parse("longer", 462, &note) == 23 && note == 0); /* 693: 23.1 s */
    /* 30.8 s and 46.3 s: a known word held to 30 (note 2, its own line),
       not a bad value */
    CHECK(ico_opt_pit_parse("double", 462, &note) == 30 && note == 2);
    CHECK(ico_opt_pit_parse("triple", 463, &note) == 30 && note == 2);
    CHECK(ico_opt_pit_parse("31", 462, &note) == 30 && note == 1); /* a number held */
    CHECK(ico_opt_pit_parse("original", 463, &note) == ICO_PIT_ORIGINAL && note == 0);
    CHECK(ico_opt_pit_parse("15", 463, &note) == ICO_PIT_ORIGINAL && note == 0);
    CHECK(ico_opt_pit_parse("16", 462, &note) == 16 && note == 0);
    CHECK(ico_opt_pit_parse("dive", 462, &note) == ICO_PIT_ORIGINAL && note == 1);
    CHECK(ico_opt_pit_parse("", 462, &note) == ICO_PIT_ORIGINAL && note == 1);
    CHECK(ico_opt_pit_parse(NULL, 462, &note) == ICO_PIT_ORIGINAL && note == 0);
    CHECK(ico_opt_pit_parse("-3", 462, NULL) == ICO_PIT_MIN_SECONDS);

    /* the factor: the seconds over the disc's own length, so the whole pull-in (the
       length divided by the play speed 1/factor) lasts the seconds set; the ratio
       reserved: 1/factor, 2/factor under a camera effect, on every disc */
    for (v = ICO_PIT_MIN_SECONDS; v <= ICO_PIT_MAX_SECONDS; v++) {
        const float want = (float)(v * 30) / (float)ico_opt_pit_chain_ticks();
        float secs;

        ico_opt_set_pit_time(v);
        if (v == ico_opt_pit_base_seconds()) {
            CHECK(ico_opt_pit_time() == ICO_PIT_ORIGINAL && ico_opt_pit_time_factor() == 1.0f);
            continue;
        }
        CHECK(ico_opt_pit_time() == v && ico_opt_pit_seconds() == v);
        CHECK(ico_opt_pit_time_factor() == want);
        CHECK(ico_opt_pit_dive_ratio(0) == 1.0f / want);
        CHECK(ico_opt_pit_dive_ratio(1) == 2.0f / want);
        secs = (float)ico_opt_pit_chain_ticks() / ico_opt_pit_dive_ratio(0) / 30.0f;
        CHECK(secs > (float)v - 0.01f && secs < (float)v + 0.01f);
    }
    ico_opt_set_pit_time(ICO_PIT_ORIGINAL);
    /* original: the game's own values, bit for bit */
    CHECK(ico_opt_pit_time_factor() == 1.0f);
    CHECK(ico_opt_pit_dive_ratio(0) == 1.0f && ico_opt_pit_dive_ratio(1) == 2.0f);

    /* the source: one hook in each of the four loops over that copy's five SINK motions
       (A to E), this disc's among them, the three camera timeouts scaled, the frame-50
       test untouched */
    if (f == NULL) {
        fprintf(stderr, "cannot read %s\n", ICO_ENEMY_ACT_SRC);
        failures++;
        return;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)n + 1);
    if (text == NULL || fread(text, 1, (size_t)n, f) != (size_t)n) {
        failures++;
        fclose(f);
        free(text);
        return;
    }
    text[n] = 0;
    fclose(f);
    CHECK(count_of(text, "ico_opt_pit_dive_ratio(") == 4 + 1 /* the declaration */);
    CHECK(count_of(text, "* 5 * ico_opt_pit_time_factor() <") == 3);
    CHECK(count_of(text, "50.0f < GOBJ_SUB(self)->ctrl.animFrame") >= 4);
    CHECK(count_of(text, "ico_opt_pit_time_factor() != 1.0f &&") == 4);
    /* the old one-motion hook is gone */
    CHECK(count_of(text, "ctrl.motion == 952 && ico_opt_pit_time_factor()") == 0);
    first = strcmp(slug, "jun26") == 0  ? 934
            : strcmp(slug, "aug6") == 0 ? 901
            : strcmp(slug, "ntsc") == 0 ? 899
                                        : 948;
    snprintf(hook, sizeof(hook),
             "ctrl.motion >= %d && GOBJ_SUB(self)->ctrl.motion <= %d &&\n"
             "            ico_opt_pit_time_factor() != 1.0f &&",
             first, first + 4);
    CHECK(count_of(text, hook) == 1);
    free(text);
}

/* --- shadow_speed: the setter, its log line and the sources that carry it - */

/* the whole text of a source file (caller frees), or NULL */
static char *read_source(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long n;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)n + 1);
    if (text == NULL || fread(text, 1, (size_t)n, f) != (size_t)n) {
        free(text);
        fclose(f);
        return NULL;
    }
    text[n] = 0;
    fclose(f);
    return text;
}

/* Review finding 15: the motions shadow_speed scales on this disc (options.h
   ico_opt_shadow_speed_motion; the ids are the decomp's motion-def.c names per disc): the
   locomotion ones in, and the stand, the fly start, the run's end, an attack, the grab, a
   hit, the dissolve's first sink motion and the pit dive out */
static void test_shadow_speed_motions(void)
{
    static const struct {
        const char *slug;
        int in[13];  /* -1 ends */
        int out[10]; /* stand, fly start, run end, attack1, hug start, hit1 front, sink A,
                        the dive (sink E), -1 */
    } kDiscs[] = {
        {"pal",
         {836, 838, 841, 842, 843, 844, 845, 846, 941, 942, -1},
         {834, 835, 847, 911, 938, 897, 948, 952, -1}},
        {"jpn",
         {836, 838, 841, 842, 843, 844, 845, 846, 941, 942, -1},
         {834, 835, 847, 911, 938, 897, 948, 952, -1}},
        {"kor",
         {836, 838, 841, 842, 843, 844, 845, 846, 941, 942, -1},
         {834, 835, 847, 911, 938, 897, 948, 952, -1}},
        {"cn",
         {836, 838, 841, 842, 843, 844, 845, 846, 941, 942, -1},
         {834, 835, 847, 911, 938, 897, 948, 952, -1}},
        {"ntsc",
         {790, 792, 795, 796, 797, 798, 799, 800, 888, 889, -1},
         {788, 789, 801, 865, 896, 851, 899, 903, -1}},
        {"aug6",
         {792, 794, 797, 798, 799, 800, 801, 802, 890, 891, -1},
         {790, 791, 803, 867, 898, 853, 901, 905, -1}},
        {"jun26",
         {823, 825, 828, 829, 830, 831, 832, 833, 923, 924, 942, 944, 955},
         {821, 822, 834, 900, 931, 886, 934, 938, -1}},
    };

    const char *mine = ico_region_cur()->slug;
    size_t d;
    int i;

    for (d = 0; d < sizeof(kDiscs) / sizeof(kDiscs[0]); d++) {
        CHECK(ico_region_select(kDiscs[d].slug) == 0);
        for (i = 0; i < 13 && kDiscs[d].in[i] >= 0; i++) {
            if (!ico_opt_shadow_speed_motion(kDiscs[d].in[i])) {
                fprintf(stderr, "%s: motion %d is not scaled\n", kDiscs[d].slug, kDiscs[d].in[i]);
                failures++;
            }
        }
        for (i = 0; kDiscs[d].out[i] >= 0; i++) {
            if (ico_opt_shadow_speed_motion(kDiscs[d].out[i])) {
                fprintf(stderr, "%s: motion %d is scaled\n", kDiscs[d].slug, kDiscs[d].out[i]);
                failures++;
            }
        }
        CHECK(!ico_opt_shadow_speed_motion(-1) && !ico_opt_shadow_speed_motion(0));
    }
    CHECK(ico_region_select(mine) == 0);
}

static void test_shadow_speed(void)
{
    static const float factor[ICO_SPEED_COUNT] = {0.75f, 1.0f, 1.25f, 1.5f};
    static const char *const name[ICO_SPEED_COUNT] = {"slower", "original", "faster", "fastest"};
    char *text;
    char logpath[512];
    char line[128];
    char want[96];
    FILE *lf;
    int v, got = 0;

    for (v = 0; v < ICO_SPEED_COUNT; v++) {
        ico_opt_set_shadow_speed(v);
        CHECK(ico_opt_shadow_speed() == v && ico_opt_shadow_speed_factor() == factor[v]);
    }
    ico_opt_set_shadow_speed(99); /* out of range reads original */
    CHECK(ico_opt_shadow_speed() == ICO_SPEED_ORIGINAL && ico_opt_shadow_speed_factor() == 1.0f);

    /* the log line on a change: fd 2 to a file for the three calls (one repeats: no line) */
    {
        const char *dir = getenv("TMPDIR");
        int saved, fd;

        snprintf(logpath, sizeof(logpath), "%s/ico_shadow_speed_%d.log", dir != NULL ? dir : "/tmp",
                 (int)getpid());
        fd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        saved = dup(2);
        if (fd < 0 || saved < 0) {
            failures++;
            return;
        }
        dup2(fd, 2);
        close(fd);
        ico_opt_set_shadow_speed(ICO_SPEED_FASTEST);
        ico_opt_set_shadow_speed(ICO_SPEED_FASTEST); /* no change: no line */
        ico_opt_set_shadow_speed(ICO_SPEED_SLOWER);
        fflush(stderr);
        dup2(saved, 2);
        close(saved);
    }
    lf = fopen(logpath, "rb");
    snprintf(want, sizeof(want), "gameplay: shadow speed %s (x%.2f)\n", name[ICO_SPEED_FASTEST],
             (double)factor[ICO_SPEED_FASTEST]);
    while (lf != NULL && fgets(line, sizeof(line), lf) != NULL) {
        got++;
        if (got == 1) {
            CHECK(strcmp(line, want) == 0);
        } else {
            CHECK(strcmp(line, "gameplay: shadow speed slower (x0.75)\n") == 0);
        }
    }
    CHECK(got == 2);
    if (lf != NULL) {
        fclose(lf);
    }
    remove(logpath);
    ico_opt_set_shadow_speed(ICO_SPEED_ORIGINAL);

    /* the turn limit follows the factor on a shadow's locomotion motion only, and the factor 1
       leaves it (pal: 838 is the fly, 834 the stand, 911 an attack) */
    {
        const char *mine = ico_region_cur()->slug;

        CHECK(ico_region_select("pal") == 0);
        for (v = 0; v < ICO_SPEED_COUNT; v++) {
            ico_opt_set_shadow_speed(v);
            CHECK(ico_opt_shadow_turn_limit(4, 838, 3.0f) == 3.0f * factor[v]);
            CHECK(ico_opt_shadow_turn_limit(4, 841, 7.5f) == 7.5f * factor[v]);
            CHECK(ico_opt_shadow_turn_limit(4, 834, 3.0f) == 3.0f);
            CHECK(ico_opt_shadow_turn_limit(4, 911, 3.0f) == 3.0f);
            CHECK(ico_opt_shadow_turn_limit(0, 838, 3.0f) == 3.0f);
        }
        ico_opt_set_shadow_speed(ICO_SPEED_ORIGINAL);
        CHECK(ico_opt_shadow_turn_limit(4, 838, 3.0f) == 3.0f);
        CHECK(ico_region_select(mine) == 0);
    }

    /* the hook: the multiplier in the one place that resolves the ratio, for enemies, not on
       the freeze, only on this disc's locomotion motions */
    text = read_source(ICO_ACT_GAME_SRC);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(count_of(text, "ratio *= ico_opt_shadow_speed_factor();") == 1);
        CHECK(count_of(text, "ratio > 0.001f &&") == 1);
        CHECK(count_of(text, "ico_opt_shadow_speed_factor() != 1.0f") == 1);
        CHECK(count_of(text, "kind == 4 && ratio > 0.001f") == 1);
        CHECK(count_of(text, "ico_opt_shadow_speed_motion(GOBJ_SUB(self)->ctrl.motion)") == 1);
        CHECK(count_of(text, "diveMotion") == 0);
        free(text);
    }
    /* the flight push: scaled once in each of the three flyCoreLoop copies */
    text = read_source(ICO_COMMONACT_SRC);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(count_of(text, "v.f[2] *= ico_opt_shadow_speed_factor();") == 3);
        /* the turn limit: once in each of the three SetMotionDirectionSmooze copies */
        CHECK(count_of(
                  text,
                  "s = ico_opt_shadow_turn_limit(self->kind, GOBJ_SUB(self)->ctrl.motion, s);") ==
              3);
        free(text);
    }
    /* the simulation keys name it (the pad recording's header and a save
       state's validity: port/platform/sim_keys.c ICO_SIM_KEYS) */
    text = read_source(ICO_SIM_KEYS_SRC);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(count_of(text, "\"gameplay.shadow_speed\",") == 1);
        CHECK(strstr(text, "\"gameplay.pit_time\",") < strstr(text, "\"gameplay.shadow_speed\","));
        free(text);
    }
}

/* --- shadows_together: the game's turn rule and the hook --------------------
   The game holds a Shadow back while another is attacking (enemy_act.c
   EnemyUtil_isOtherStatus, its eleven callers); the option lets it go, and
   counts and logs each turn it skipped. */
static void test_shadows_together(const char *dir)
{
    char path[512];
    char logpath[512];
    char line[160];
    char *text;
    FILE *f;
    int got = 0;

    /* the default is the game's: a Shadow waits while another attacks */
    snprintf(path, sizeof(path), "%s/options_shadows_together.toml", dir);
    remove(path);
    ico_config_reset(path, "no-such.ini");
    ico_opt_reload();
    CHECK(ico_opt_shadows_together() == 0);
    CHECK(ico_opt_shadow_waits(1) == 1 && ico_opt_shadow_waits(0) == 0);
    CHECK(ico_opt_shadows_together_went() == 0);
    /* from the file */
    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs("[gameplay]\nshadows_together = true\n", f);
        fclose(f);
    }
    ico_config_reset(path, "no-such.ini");
    ico_opt_reload();
    CHECK(ico_opt_shadows_together() == 1);
    ico_opt_set_shadows_together(0); /* the run-time value wins until a reload */
    CHECK(ico_opt_shadows_together() == 0 && ico_opt_shadow_waits(1) == 1);
    remove(path);

    /* on: no Shadow waits; each skipped turn is counted (none when no other
       attacks); the log lines: the change, the first 8 turns, then every 100th */
    {
        const char *tmp = getenv("TMPDIR");
        int saved, fd, i;

        snprintf(logpath, sizeof(logpath), "%s/ico_shadows_together_%d.log",
                 tmp != NULL ? tmp : "/tmp", (int)getpid());
        fd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        saved = dup(2);
        if (fd < 0 || saved < 0) {
            failures++;
            return;
        }
        dup2(fd, 2);
        close(fd);
        ico_opt_set_shadows_together(1);
        ico_opt_set_shadows_together(1); /* no change: no line */
        for (i = 0; i < 200; i++) {
            CHECK(ico_opt_shadow_waits(1) == 0);
            CHECK(ico_opt_shadow_waits(0) == 0);
        }
        ico_opt_set_shadows_together(0);
        CHECK(ico_opt_shadow_waits(1) == 1); /* off again: the game's turn, not counted */
        fflush(stderr);
        dup2(saved, 2);
        close(saved);
    }
    CHECK(ico_opt_shadows_together_went() == 200);
    f = fopen(logpath, "rb");
    while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        char want[160];

        got++;
        if (got == 1) {
            CHECK(strcmp(line, "gameplay: shadows attack together: all at once (no turns)\n") == 0);
        } else if (got <= 11) {
            const unsigned long n = got <= 9 ? (unsigned long)(got - 1) : got == 10 ? 100UL : 200UL;
            snprintf(want, sizeof(want),
                     "gameplay: shadows attack together: a shadow did not wait for another's "
                     "attack (%lu this run)\n",
                     n);
            CHECK(strcmp(line, want) == 0);
        } else {
            CHECK(strcmp(line,
                         "gameplay: shadows attack together: 1 at once (the game's turns)\n") == 0);
        }
    }
    CHECK(got == 12);
    if (f != NULL) {
        fclose(f);
    }
    remove(logpath);

    /* the hook: the game's rule under its own name, called once, by the
       wrapper the eleven callers keep calling; the option's answer decides */
    text = read_source(ICO_ENEMY_ACT_SRC);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(count_of(text, "EnemyUtil_isOtherStatus(self, 0)") == 11);
        CHECK(count_of(text, "EnemyUtil_isOtherStatus_disc(") == 2);
        CHECK(count_of(text, "ico_opt_shadow_waits(") == 1 + 1 /* the declaration */);
        CHECK(count_of(text, "    ICO_WORD_PTR(GObj *) other = EnemyUtil_isOtherStatus_disc(self, "
                             "mode);\n\n"
                             "    return ico_opt_shadow_waits(other != 0) ? other : 0;\n") == 1);
        free(text);
    }
    text = read_source(ICO_SIM_KEYS_SRC);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(count_of(text, "\"gameplay.shadows_together\",") == 1);
        free(text);
    }
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
}

/* --- yorda_trips: the hooks in the game's sources -------------------------
   Each actGirlHand copy calls the tick where it sets her run-pulled bit,
   with its disc's sit-down mail (the gate script's, st04a.c
   actSt04aGirlSitChk): 109 on the European, Japanese, Korean and Asian
   discs, 102 on the North American, 101 on the August 2001 preview, 100 on
   the June 2001 demo; each InitIcoMisc copy calls the stage load after the
   stage's pack; the key changes the simulation. */
static void test_yorda_trips(void)
{
    const char *slug = ico_region_cur()->slug;
    const int mail = strcmp(slug, "jun26") == 0  ? 100
                     : strcmp(slug, "aug6") == 0 ? 101
                     : strcmp(slug, "ntsc") == 0 ? 102
                                                 : 109;
    char hook[96];
    char *text = read_source(ICO_GIRL_ACT_SRC);

    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(count_of(text, "ico_opt_yorda_trip_tick(") == 4 + 1 /* the declaration */);
        snprintf(hook, sizeof(hook), "ico_opt_yorda_trip_tick((GObj *)self, %d);", mail);
        CHECK(count_of(text, hook) == 1);
        /* each right after its copy's run-pulled bit */
        static const char *const kSet[4] = {"|= 0x100000;\n", "|= 0x200;\n", "|= 0x40;\n",
                                            "|= 0x10;\n"};
        static const int kMail[4] = {109, 102, 101, 100};
        for (int i = 0; i < 4; i++) {
            char want[320];
            snprintf(want, sizeof(want),
                     "ICO_RAW(long long, sub, 0x20, sub->flags20.ll) %s"
                     "                /* PC port ([gameplay] yorda_trips): she may trip on this "
                     "run-pulled tick */\n"
                     "                ico_opt_yorda_trip_tick((GObj *)self, %d);",
                     kSet[i], kMail[i]);
            CHECK(count_of(text, want) == 1);
        }
        free(text);
    }
    text = read_source(ICO_ICOMISC_SRC);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(count_of(text, "ico_yorda_trip_stage_loaded(") == 2 + 1 /* the declaration */);
        CHECK(count_of(text, "    iosCdvdLoadPackFile(pack, GetDataFileName(stage, pack), 1);\n"
                             "    /* PC port ([gameplay] yorda_trips): her fall's motions, where "
                             "the pack has none */\n"
                             "    ico_yorda_trip_stage_loaded(stage);") == 2);
        free(text);
    }
    text = read_source(ICO_SIM_KEYS_SRC);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(count_of(text, "\"gameplay.yorda_trips\",") == 1);
        free(text);
    }
}

static void test_brain(void)
{
    EBSlot *p;
    int safe;

    for (safe = 0; safe < 2; safe++) {
        ico_opt_set_yorda_safe(safe);

        /* both in view, the girl nearer: the original goes for her */
        s_sees_boy = s_sees_girl = 1;
        p = setup(0, 0, 0);
        CHECK(eBrainGetTarget(&s_enemy) == p);
        CHECK(p->status == (safe ? 1 : 2));
        CHECK(p->target == (safe ? &s_boy : &s_girl));
        CHECK(eBrainGirlChaseCount == (safe ? 0 : 1));

        /* only the boy in view: boy, with the option either way */
        s_sees_boy = 1;
        s_sees_girl = 0;
        p = setup(0, 0, 0);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == 1 && p->target == &s_boy);

        /* only the girl in view */
        s_sees_boy = 0;
        s_sees_girl = 1;
        p = setup(0, 0, 0);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == (safe ? 0 : 2));

        /* the 181-frame switch from the boy to the girl */
        s_sees_boy = s_sees_girl = 1;
        p = setup(1, 0, 200);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == (safe ? 1 : 2));
        p = setup(1, 0, 100);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == 1);

        /* messages: 2 (chase the girl), 6 (go to the girl), 1 (the boy) */
        p = setup(0, 2, 0);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == (safe ? 1 : 2));
        p = setup(0, 6, 0);
        s_sees_boy = s_sees_girl = 0;
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == (safe ? 0 : 3));
        s_sees_boy = s_sees_girl = 1;
        p = setup(0, 1, 0);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == 1);

        /* the carry statuses are not the option's: a scripted or story grab
           sends 9 and 7 (status 4), which both settings keep */
        p = setup(0, 0, 0);
        eBrainSendMes(&s_enemy, 9);
        eBrainSendMes(&s_enemy, 7);
        eBrainGetTarget(&s_enemy);
        CHECK(p->status == 4);
    }
    ico_opt_set_yorda_safe(0);
}

/* --- mirror mode per save slot ------------------------------------------- */
static int s_heard = -1, s_heard_count;

static void listener(int on)
{
    s_heard = on;
    s_heard_count++;
}

static void test_mirror_slots(const char *dir)
{
    char path[512];
    FILE *f;

    snprintf(path, sizeof(path), "%s/options_mirror_test.toml", dir);
    remove(path);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    /* the listener hears the current value at once, then every change */
    ico_opt_set_mirror_listener(listener);
    CHECK(s_heard == 0 && s_heard_count == 1);
    /* New Game with Mirror On, saved to slot 3 (game.003) with sum 0x1234 */
    ico_opt_set_mirror(1);
    CHECK(s_heard == 1 && s_heard_count == 2);
    CHECK(ico_mirror_slot_saved(3, 0x1234u) == 0);
    /* a New Game with Off saved to slot 5 */
    ico_opt_set_mirror(0);
    CHECK(ico_mirror_slot_saved(5, 0xFFFFFFF0u) == 0);
    /* the next run: the file is read again */
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_mirror() == 0);
    CHECK(ico_mirror_slot_get(3, 0x1234u) == 1);
    CHECK(ico_mirror_slot_get(5, 0xFFFFFFF0u) == 0);
    CHECK(ico_mirror_slot_get(4, 0x1234u) == -1);  /* no entry */
    CHECK(ico_mirror_slot_get(3, 0x1235u) == -1);  /* another save in slot 3 */
    CHECK(ico_mirror_slot_get(-1, 0x1234u) == -1); /* not a slot */
    /* loading slot 3 sets the run's value On (the listener hears it) */
    CHECK(ico_mirror_slot_loaded(3, 0x1234u) == 1);
    CHECK(ico_opt_mirror() == 1 && s_heard == 1);
    /* loading slot 5: Off */
    CHECK(ico_mirror_slot_loaded(5, 0xFFFFFFF0u) == 0);
    CHECK(ico_opt_mirror() == 0 && s_heard == 0);
    /* a slot without an entry (a PS2 save) and a slot whose save was
       replaced elsewhere (another sum) load Off */
    ico_opt_set_mirror(1);
    CHECK(ico_mirror_slot_loaded(7, 42u) == 0 && ico_opt_mirror() == 0);
    ico_opt_set_mirror(1);
    CHECK(ico_mirror_slot_loaded(3, 0x9999u) == 0 && ico_opt_mirror() == 0);
    /* saving over slot 3 with another flag: the new flag wins */
    ico_opt_set_mirror(0);
    CHECK(ico_mirror_slot_saved(3, 0x5678u) == 0);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_mirror_slot_get(3, 0x5678u) == 0);
    CHECK(ico_mirror_slot_get(3, 0x1234u) == -1);
    /* the file holds the [mirror] table */
    f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        char buf[2048];
        char want[64];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        /* the selected disc's keys: slot_N (European), jpn_slot_N,
           kor_slot_N (the region's key_prefix) */
        CHECK(strstr(buf, "[mirror]") != NULL);
        snprintf(want, sizeof(want), "\n%sslot_3 = false", ico_region_cur()->key_prefix);
        CHECK(strstr(buf, want) != NULL);
        snprintf(want, sizeof(want), "\n%sslot_3_sum = 22136", ico_region_cur()->key_prefix);
        CHECK(strstr(buf, want) != NULL);
        snprintf(want, sizeof(want), "\n%sslot_5_sum = 4294967280", ico_region_cur()->key_prefix);
        CHECK(strstr(buf, want) != NULL);
        /* and no other disc's */
        {
            int r;

            for (r = 0; r < ico_region_count(); r++) {
                const char *pfx = ico_region_at(r)->key_prefix;

                if (strcmp(pfx, ico_region_cur()->key_prefix) != 0) {
                    snprintf(want, sizeof(want), "\n%sslot_", pfx);
                    CHECK(strstr(buf, want) == NULL);
                }
            }
        }
    }
    /* the title: back to [gameplay] mirror (absent: Off) */
    ico_opt_set_mirror(1);
    ico_opt_mirror_reset();
    CHECK(ico_opt_mirror() == 0 && s_heard == 0);
    ico_opt_set_mirror_listener(NULL);
    remove(path);
    ico_opt_reload();
}

/* Players kept per save slot on the 2001 discs ([players], options.h): the
   count each save was made with, tied to its sum, per disc; a save without
   an entry or with another sum loads as one player.  Run for the three
   2001 slugs on every disc's run, the selected disc restored after. */
static void test_players_slots(const char *dir)
{
    static const char *const kDiscs[] = {"ntsc", "aug6", "jun26"};
    const char *mine = ico_region_cur()->slug;
    char path[512];
    char buf[4096];
    char want[64];
    FILE *f;
    unsigned d;

    snprintf(path, sizeof(path), "%s/options_players_test.toml", dir);
    remove(path);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    for (d = 0; d < sizeof(kDiscs) / sizeof(kDiscs[0]); d++) {
        CHECK(ico_region_select(kDiscs[d]) == 0);
        /* slot 3 saved with two players, slot 5 with one */
        CHECK(ico_players_slot_saved(3, 0x1234u + d, 1) == 0);
        CHECK(ico_players_slot_saved(5, 0xFFFFFFF0u, 0) == 0);
    }
    /* the next run: the file is read again */
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    for (d = 0; d < sizeof(kDiscs) / sizeof(kDiscs[0]); d++) {
        CHECK(ico_region_select(kDiscs[d]) == 0);
        CHECK(ico_players_slot_get(3, 0x1234u + d) == 1);
        CHECK(ico_players_slot_get(5, 0xFFFFFFF0u) == 0);
        CHECK(ico_players_slot_get(4, 0x1234u + d) == -1);  /* no entry */
        CHECK(ico_players_slot_get(3, 0x9999u) == -1);      /* another save in slot 3 */
        CHECK(ico_players_slot_get(-1, 0x1234u + d) == -1); /* not a slot */
        /* each disc's slot 3 has its own sum: another disc's does not answer */
        CHECK(ico_players_slot_get(3, 0x1234u + (d + 1) % 3) == -1);
        /* the value girlControlMode takes at a load */
        CHECK(ico_players_slot_loaded(3, 0x1234u + d) == 1);
        CHECK(ico_players_slot_loaded(5, 0xFFFFFFF0u) == 0);
        CHECK(ico_players_slot_loaded(7, 42u) == 0);
        CHECK(ico_players_slot_loaded(3, 0x9999u) == 0);
    }
    /* saving over slot 3 with one player: the new count wins */
    CHECK(ico_region_select("ntsc") == 0);
    CHECK(ico_players_slot_saved(3, 0x5678u, 0) == 0);
    CHECK(ico_players_slot_get(3, 0x5678u) == 0 && ico_players_slot_get(3, 0x1234u) == -1);
    /* the file: the [players] table, each disc's prefix, the count 1 or 2 */
    f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);

        buf[n] = '\0';
        fclose(f);
        CHECK(strstr(buf, "[players]") != NULL);
        for (d = 0; d < sizeof(kDiscs) / sizeof(kDiscs[0]); d++) {
            CHECK(ico_region_select(kDiscs[d]) == 0);
            snprintf(want, sizeof(want), "\n%sslot_3 = %d", ico_region_cur()->key_prefix,
                     d == 0 ? 1 : 2);
            CHECK(strstr(buf, want) != NULL);
            snprintf(want, sizeof(want), "\n%sslot_5 = 1", ico_region_cur()->key_prefix);
            CHECK(strstr(buf, want) != NULL);
            snprintf(want, sizeof(want), "\n%sslot_5_sum = 4294967280",
                     ico_region_cur()->key_prefix);
            CHECK(strstr(buf, want) != NULL);
        }
    }
    CHECK(ico_region_select(mine) == 0);
    remove(path);
    ico_opt_reload();
}

/* The two discs' slots in one file: each disc's saves are another card
   directory, so slot 3 of one is not slot 3 of the other. The selected
   disc's write leaves the other's keys as they were, and the other's entry
   does not answer for it. */
static void test_mirror_discs(const char *dir)
{
    const int me = ico_region;
    const int other = me == 0 ? 1 : 0;
    char path[512];
    char text[2048];
    char want[64];
    FILE *f;

    snprintf(path, sizeof(path), "%s/options_mirror_discs_test.toml", dir);
    remove(path);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    /* the other disc saved slot 3 with Mirror On */
    ico_region = other;
    ico_opt_set_mirror(1);
    CHECK(ico_mirror_slot_saved(3, 0x1111u) == 0);
    CHECK(ico_mirror_slot_get(3, 0x1111u) == 1);
    /* the selected disc: slot 3 has no entry, even with the same sum */
    ico_region = me;
    CHECK(ico_mirror_slot_get(3, 0x1111u) == -1);
    CHECK(ico_mirror_slot_loaded(3, 0x1111u) == 0 && ico_opt_mirror() == 0);
    /* it saves slot 3 Off: its own keys, the other disc's still On */
    CHECK(ico_mirror_slot_saved(3, 0x1111u) == 0);
    CHECK(ico_mirror_slot_get(3, 0x1111u) == 0);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_opt_reload();
    ico_region = other;
    CHECK(ico_mirror_slot_get(3, 0x1111u) == 1);
    ico_region = me;
    CHECK(ico_mirror_slot_get(3, 0x1111u) == 0);
    f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        size_t n = fread(text, 1, sizeof(text) - 1, f);

        text[n] = '\0';
        fclose(f);
        /* each disc under its own prefix (the European disc's keys as they
           always were): this one Off, the other still On */
        snprintf(want, sizeof(want), "\n%sslot_3 = false", ico_region_at(me)->key_prefix);
        CHECK(strstr(text, want) != NULL);
        snprintf(want, sizeof(want), "\n%sslot_3 = true", ico_region_at(other)->key_prefix);
        CHECK(strstr(text, want) != NULL);
        snprintf(want, sizeof(want), "\n%sslot_3_sum = 4369", ico_region_at(me)->key_prefix);
        CHECK(strstr(text, want) != NULL);
        snprintf(want, sizeof(want), "\n%sslot_3_sum = 4369", ico_region_at(other)->key_prefix);
        CHECK(strstr(text, want) != NULL);
    }
    ico_opt_mirror_reset();
    remove(path);
    ico_opt_reload();
}

/* the five effect switches: on by default, read from [video],
   written back every time, and no part of the preset */
static char *read_text(const char *path)
{
    static char buf[16384];
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        return NULL;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

static int effects_are(const IcoVideoOptions *o, int g, int d, int s, int m, int f, int cb)
{
    return o->effectGlow == g && o->effectDepthOfField == d && o->effectSoftening == s &&
           o->effectMotionBlur == m && o->effectFog == f && o->effectCinematicBars == cb &&
           ico_video_effect_cinematic_bars() == cb && ico_video_effect_glow() == g &&
           ico_video_effect_depth_of_field() == d && ico_video_effect_softening() == s &&
           ico_video_effect_motion_blur() == m && ico_video_effect_fog() == f;
}

static void test_video_effects(const char *dir)
{
    static const char *const keys[6] = {"effect_glow = ",      "effect_depth_of_field = ",
                                        "effect_softening = ", "effect_motion_blur = ",
                                        "effect_fog = ",       "effect_cinematic_bars = "};
    char path[512];
    IcoVideoOptions o, d;
    const char *text;
    FILE *f;

    /* the defaults, and no file: every effect on */
    ico_video_defaults(&d);
    CHECK(d.effectGlow == 1 && d.effectDepthOfField == 1 && d.effectSoftening == 1 &&
          d.effectMotionBlur == 1 && d.effectFog == 1 && d.effectCinematicBars == 1);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(effects_are(&o, 1, 1, 1, 1, 1, 1));
    /* a file with fog off (and a stray value elsewhere) */
    snprintf(path, sizeof(path), "%s/options_video_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\neffect_fog = false\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(effects_are(&o, 1, 1, 1, 1, 0, 1));
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    /* run-time values are sanitized to 0 or 1 */
    o.effectGlow = 0;
    o.effectDepthOfField = 7;
    o.effectSoftening = 0;
    o.effectMotionBlur = -1;
    o.effectFog = 1;
    o.effectCinematicBars = 5;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(effects_are(&o, 0, 1, 0, 1, 1, 1));
    /* saved: all six keys written, and read back the same */
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL);
    for (int i = 0; text != NULL && i < 6; i++) {
        CHECK(strstr(text, keys[i]) != NULL);
    }
    CHECK(text != NULL && strstr(text, "effect_glow = false") != NULL &&
          strstr(text, "effect_softening = false") != NULL &&
          strstr(text, "effect_fog = true") != NULL &&
          strstr(text, "effect_cinematic_bars = true") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(effects_are(&o, 0, 1, 0, 1, 1, 1));
    /* none of the six moves the preset off Original, and Enhanced's
       shortcut leaves them alone */
    for (int i = 0; i < 6; i++) {
        ico_video_defaults(&o);
        int *fx[6] = {&o.effectGlow,       &o.effectDepthOfField, &o.effectSoftening,
                      &o.effectMotionBlur, &o.effectFog,          &o.effectCinematicBars};
        *fx[i] = 0;
        CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
        ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
        CHECK(*fx[i] == 0 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    }
    /* Water reflections: Original (0) by default and with no key, Sharp
       read, sanitized, saved and read back; outside the preset as the six */
    ico_video_defaults(&d);
    CHECK(d.effectWaterSharp == 0);
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[video]\neffect_fog = false\n", f);
        fclose(f);
    }
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.effectWaterSharp == 0 && ico_video_effect_water_sharp() == 0);
    o.effectWaterSharp = 3;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.effectWaterSharp == 1 && ico_video_effect_water_sharp() == 1);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "effect_water_sharp = true") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.effectWaterSharp == 1 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.effectWaterSharp == 1 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    ico_video_set_preset(&o, ICO_VIDEO_ORIGINAL);
    CHECK(o.effectWaterSharp == 1);
    /* Motion blur's way ([video] effect_motion_blur_mode): Original with no
       key (a file from before it), "original" and "smooth" read in any
       case, anything else Original (with a stderr line); Smooth only while
       the switch is on for the getter and the log's name, the way kept
       under Off; saved with every save, read back, outside the preset */
    {
        static const struct {
            const char *text;
            int smooth;
        } kMode[] = {{NULL, 0}, {"original", 0}, {"Smooth", 1}, {"fast", 0}};

        ico_video_defaults(&d);
        CHECK(d.effectMotionBlurSmooth == 0 &&
              strcmp(ico_video_motion_blur_name(&d), "original") == 0);
        for (int i = 0; i < 4; i++) {
            f = fopen(path, "wb");
            if (f != NULL) {
                fputs("[video]\n", f);
                if (kMode[i].text != NULL) {
                    fprintf(f, "effect_motion_blur_mode = \"%s\"\n", kMode[i].text);
                }
                fclose(f);
            }
            ico_config_reset(path, "/nonexistent/options_test.ini");
            ico_video_reload();
            ico_video_get(&o);
            CHECK(o.effectMotionBlur == 1 && o.effectMotionBlurSmooth == kMode[i].smooth &&
                  ico_video_effect_motion_blur_smooth() == kMode[i].smooth &&
                  ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
        }
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("[video]\neffect_motion_blur = false\neffect_motion_blur_mode = \"smooth\"\n", f);
            fclose(f);
        }
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        ico_video_get(&o);
        CHECK(o.effectMotionBlur == 0 && o.effectMotionBlurSmooth == 1 &&
              ico_video_effect_motion_blur_smooth() == 0 &&
              strcmp(ico_video_motion_blur_name(&o), "off") == 0);
        o.effectMotionBlur = 1;
        o.effectMotionBlurSmooth = 4;
        ico_video_set(&o);
        ico_video_get(&o);
        CHECK(o.effectMotionBlurSmooth == 1 && ico_video_effect_motion_blur_smooth() == 1 &&
              strcmp(ico_video_motion_blur_name(&o), "smooth") == 0);
        CHECK(ico_video_save() == 0);
        text = read_text(path);
        CHECK(text != NULL && strstr(text, "effect_motion_blur = true") != NULL &&
              strstr(text, "effect_motion_blur_mode = \"smooth\"") != NULL);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        ico_video_get(&o);
        CHECK(o.effectMotionBlurSmooth == 1 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
        ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
        CHECK(o.effectMotionBlurSmooth == 1 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
        /* a file without the key saved by this build gains it as Original */
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("[video]\neffect_fog = false\n", f);
            fclose(f);
        }
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        CHECK(ico_video_save() == 0);
        text = read_text(path);
        CHECK(text != NULL && strstr(text, "effect_motion_blur_mode = \"original\"") != NULL);
    }
    /* Motion blur strength ([video] effect_motion_blur_strength, issue 181):
       100 by default and with no key (a file from before it), 10..100 read
       as written in tens, a value off the tens rounded to the nearest (half
       up), outside 10..100 clamped (each with a stderr line); the getter
       reads it whatever Motion blur's way, Off keeps it; saved with every
       save and read back; outside the preset */
    {
        static const struct {
            const char *text;
            int want;
        } kStr[] = {{NULL, 100}, {"100", 100}, {"50", 50},  {"10", 10},   {"55", 60},
                    {"54", 50},  {"0", 10},    {"-30", 10}, {"250", 100}, {"\"half\"", 100}};

        ico_video_defaults(&d);
        CHECK(d.effectMotionBlurStrength == 100);
        for (size_t i = 0; i < sizeof(kStr) / sizeof(kStr[0]); i++) {
            f = fopen(path, "wb");
            if (f != NULL) {
                fputs("[video]\neffect_motion_blur_mode = \"smooth\"\n", f);
                if (kStr[i].text != NULL) {
                    fprintf(f, "effect_motion_blur_strength = %s\n", kStr[i].text);
                }
                fclose(f);
            }
            ico_config_reset(path, "/nonexistent/options_test.ini");
            ico_video_reload();
            ico_video_get(&o);
            CHECK(o.effectMotionBlurStrength == kStr[i].want &&
                  ico_video_effect_motion_blur_strength() == kStr[i].want &&
                  ico_video_effect_motion_blur_smooth() == 1 &&
                  ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
        }
        /* the clamp itself */
        CHECK(ico_video_motion_blur_strength_clamp(5) == 10 &&
              ico_video_motion_blur_strength_clamp(14) == 10 &&
              ico_video_motion_blur_strength_clamp(15) == 20 &&
              ico_video_motion_blur_strength_clamp(95) == 100 &&
              ico_video_motion_blur_strength_clamp(1000) == 100 &&
              ico_video_motion_blur_strength_clamp(70) == 70);
        /* ico_video_set clamps; Off keeps the value; the save writes it */
        ico_video_get(&o);
        o.effectMotionBlur = 0;
        o.effectMotionBlurStrength = 33;
        ico_video_set(&o);
        ico_video_get(&o);
        CHECK(o.effectMotionBlurStrength == 30 && ico_video_effect_motion_blur_strength() == 30 &&
              ico_video_effect_motion_blur() == 0);
        CHECK(ico_video_save() == 0);
        text = read_text(path);
        CHECK(text != NULL && strstr(text, "effect_motion_blur_strength = 30\n") != NULL &&
              strstr(text, "effect_motion_blur = false") != NULL);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        ico_video_get(&o);
        CHECK(o.effectMotionBlurStrength == 30 && o.effectMotionBlur == 0);
        ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
        CHECK(o.effectMotionBlurStrength == 30);
        ico_video_set_preset(&o, ICO_VIDEO_ORIGINAL);
        CHECK(o.effectMotionBlurStrength == 30 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
        /* a file without the key saved by this build gains it as 100 */
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("[video]\neffect_fog = false\n", f);
            fclose(f);
        }
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        CHECK(ico_video_save() == 0);
        text = read_text(path);
        CHECK(text != NULL && strstr(text, "effect_motion_blur_strength = 100\n") != NULL);
    }
    /* The colour adjustment ([video] gamma, brightness, contrast,
       saturation, issue 190): 50 by default and with no key (a file from
       before the rows), 1..100 read as written (a hand-set 47 kept), outside
       1..100 clamped and a value that is not a number 50; saved with every
       save and read back; outside the preset */
    {
        static const struct {
            const char *text;
            int want;
        } kTone[] = {{NULL, 50}, {"50", 50}, {"47", 47},   {"1", 1},     {"100", 100},
                     {"0", 1},   {"-30", 1}, {"250", 100}, {"\"x\"", 50}};

        static const char *const kKey[4] = {"gamma", "brightness", "contrast", "saturation"};

        ico_video_defaults(&d);
        CHECK(d.toneGamma == 50 && d.toneBrightness == 50 && d.toneContrast == 50 &&
              d.toneSaturation == 50);
        CHECK(ico_video_tone_clamp(0) == 1 && ico_video_tone_clamp(1) == 1 &&
              ico_video_tone_clamp(47) == 47 && ico_video_tone_clamp(100) == 100 &&
              ico_video_tone_clamp(101) == 100);
        for (size_t i = 0; i < sizeof(kTone) / sizeof(kTone[0]); i++) {
            for (int k = 0; k < 4; k++) {
                int got[4];

                f = fopen(path, "wb");
                if (f != NULL) {
                    fputs("[video]\n", f);
                    if (kTone[i].text != NULL) {
                        fprintf(f, "%s = %s\n", kKey[k], kTone[i].text);
                    }
                    fclose(f);
                }
                ico_config_reset(path, "/nonexistent/options_test.ini");
                ico_video_reload();
                ico_video_get(&o);
                got[0] = o.toneGamma;
                got[1] = o.toneBrightness;
                got[2] = o.toneContrast;
                got[3] = o.toneSaturation;
                for (int j = 0; j < 4; j++) {
                    CHECK(got[j] == (j == k ? kTone[i].want : 50));
                }
                CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
            }
        }
        /* ico_video_set clamps; the save writes all four; read back */
        ico_video_get(&o);
        o.toneGamma = 0;
        o.toneBrightness = 47;
        o.toneContrast = 300;
        o.toneSaturation = 80;
        ico_video_set(&o);
        ico_video_get(&o);
        CHECK(o.toneGamma == 1 && o.toneBrightness == 47 && o.toneContrast == 100 &&
              o.toneSaturation == 80 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
        CHECK(ico_video_save() == 0);
        text = read_text(path);
        CHECK(text != NULL && strstr(text, "gamma = 1\n") != NULL &&
              strstr(text, "brightness = 47\n") != NULL &&
              strstr(text, "contrast = 100\n") != NULL &&
              strstr(text, "saturation = 80\n") != NULL);
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        ico_video_get(&o);
        CHECK(o.toneGamma == 1 && o.toneBrightness == 47 && o.toneContrast == 100 &&
              o.toneSaturation == 80);
        ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
        CHECK(o.toneGamma == 1 && o.toneSaturation == 80);
        ico_video_set_preset(&o, ICO_VIDEO_ORIGINAL);
        CHECK(o.toneBrightness == 47 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
        /* a file without the keys saved by this build gains them as 50 */
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("[video]\neffect_fog = false\n", f);
            fclose(f);
        }
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        CHECK(ico_video_save() == 0);
        text = read_text(path);
        CHECK(text != NULL && strstr(text, "gamma = 50\n") != NULL &&
              strstr(text, "brightness = 50\n") != NULL &&
              strstr(text, "contrast = 50\n") != NULL && strstr(text, "saturation = 50\n") != NULL);
    }
    /* Glow between frames ([video] effect_glow_mode): Original with no key
       under the Original rows, "original" and "smooth" read in any case,
       anything else Original (with a stderr line); Smooth beside the
       Original rows is Custom; the getter is 1 only while Glow is on; saved
       with every save and read back; the Enhanced preset sets Smooth, the
       Original preset Original; no key under the Enhanced rows is Smooth
       and an explicit key wins */
    {
        static const struct {
            const char *text;
            int smooth;
        } kGlow[] = {{NULL, 0}, {"original", 0}, {"Smooth", 1}, {"fast", 0}};

        static const char kEnhancedRows[] =
            "preset = \"enhanced\"\nresolution = \"window\"\naspect = \"auto\"\n"
            "texture_filter = \"anisotropic\"\nfull_height = true\n";

        ico_video_defaults(&d);
        CHECK(d.effectGlowSmooth == 0 && strcmp(ico_video_glow_name(&d), "original") == 0);
        for (int i = 0; i < 4; i++) {
            f = fopen(path, "wb");
            if (f != NULL) {
                fputs("[video]\n", f);
                if (kGlow[i].text != NULL) {
                    fprintf(f, "effect_glow_mode = \"%s\"\n", kGlow[i].text);
                }
                fclose(f);
            }
            ico_config_reset(path, "/nonexistent/options_test.ini");
            ico_video_reload();
            ico_video_get(&o);
            CHECK(o.effectGlow == 1 && o.effectGlowSmooth == kGlow[i].smooth &&
                  ico_video_effect_glow_smooth() == kGlow[i].smooth &&
                  ico_video_preset(&o) ==
                      (kGlow[i].smooth ? ICO_VIDEO_CUSTOM : ICO_VIDEO_ORIGINAL) &&
                  ico_video_preset_rows(&o) == ICO_VIDEO_ORIGINAL);
        }
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("[video]\neffect_glow = false\neffect_glow_mode = \"smooth\"\n", f);
            fclose(f);
        }
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        ico_video_get(&o);
        CHECK(o.effectGlow == 0 && o.effectGlowSmooth == 1 && ico_video_effect_glow_smooth() == 0 &&
              strcmp(ico_video_glow_name(&o), "off") == 0);
        o.effectGlow = 1;
        o.effectGlowSmooth = 4;
        ico_video_set(&o);
        ico_video_get(&o);
        CHECK(o.effectGlowSmooth == 1 && ico_video_effect_glow_smooth() == 1 &&
              strcmp(ico_video_glow_name(&o), "smooth") == 0);
        CHECK(ico_video_save() == 0);
        text = read_text(path);
        CHECK(text != NULL && strstr(text, "effect_glow_mode = \"smooth\"") != NULL);
        ico_video_set_preset(&o, ICO_VIDEO_ORIGINAL);
        CHECK(o.effectGlowSmooth == 0 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
        ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
        CHECK(o.effectGlowSmooth == 1 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
        /* no key under the Enhanced rows: Smooth, as Enhanced */
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("[video]\n", f);
            fputs(kEnhancedRows, f);
            fclose(f);
        }
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        ico_video_get(&o);
        CHECK(o.effectGlowSmooth == 1 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED &&
              ico_video_effect_glow_smooth() == 1);
        /* an explicit key wins over the rows */
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("[video]\n", f);
            fputs(kEnhancedRows, f);
            fputs("effect_glow_mode = \"original\"\n", f);
            fclose(f);
        }
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        ico_video_get(&o);
        CHECK(o.effectGlowSmooth == 0 && ico_video_preset(&o) == ICO_VIDEO_CUSTOM);
        /* a file with other rows and no key: Original */
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("[video]\npreset = \"custom\"\nresolution = \"3x\"\n", f);
            fclose(f);
        }
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        ico_video_get(&o);
        CHECK(o.effectGlowSmooth == 0);
        /* Glow off keeps the mode: the save writes "smooth", not the log's
           "off", so the next start reads it back without a "not understood"
           line and the Preset row still reads Enhanced */
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("[video]\n", f);
            fputs(kEnhancedRows, f);
            fputs("effect_glow_mode = \"smooth\"\n", f);
            fclose(f);
        }
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        ico_video_get(&o);
        o.effectGlow = 0;
        ico_video_set(&o);
        CHECK(ico_video_save() == 0);
        text = read_text(path);
        CHECK(text != NULL && strstr(text, "effect_glow = false") != NULL &&
              strstr(text, "effect_glow_mode = \"smooth\"") != NULL &&
              strstr(text, "effect_glow_mode = \"off\"") == NULL);
        {
            const char *dir = getenv("TMPDIR");
            char glowLog[512], line[256];
            int saved, fd, notUnderstood = 0;
            FILE *lf;

            snprintf(glowLog, sizeof(glowLog), "%s/ico_glow_off_%d.log", dir != NULL ? dir : "/tmp",
                     (int)getpid());
            fd = open(glowLog, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            saved = dup(2);
            CHECK(fd >= 0 && saved >= 0);
            if (fd >= 0 && saved >= 0) {
                fflush(stderr);
                dup2(fd, 2);
                close(fd);
                ico_config_reset(path, "/nonexistent/options_test.ini");
                ico_video_reload();
                ico_video_get(&o);
                fflush(stderr);
                dup2(saved, 2);
                close(saved);
            }
            lf = fopen(glowLog, "rb");
            while (lf != NULL && fgets(line, sizeof(line), lf) != NULL) {
                notUnderstood += strstr(line, "effect_glow_mode not understood") != NULL;
            }
            if (lf != NULL) {
                fclose(lf);
            }
            remove(glowLog);
            CHECK(notUnderstood == 0);
        }
        CHECK(o.effectGlow == 0 && o.effectGlowSmooth == 1 &&
              ico_video_preset_rows(&o) == ICO_VIDEO_ENHANCED &&
              ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
        o.effectGlow = 1;
        ico_video_set(&o);
        ico_video_get(&o);
        CHECK(ico_video_effect_glow_smooth() == 1);
    }
    /* the Glow size key an older version wrote (effect_glow_size) is read
       by nothing: a file holding it loads as one without it (every effect
       on, the preset Original), and a save of a file without it does not
       write it */
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[video]\neffect_glow_size = \"scaled\"\n", f);
        fclose(f);
    }
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(effects_are(&o, 1, 1, 1, 1, 1, 1) && o.effectWaterSharp == 0 &&
          ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    remove(path);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "effect_glow =") != NULL &&
          strstr(text, "effect_glow_size") == NULL);
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* [video] effects_depth, on by default, read, sanitized,
   saved when off or already in the file, outside the preset, through
   ico_video_effects_depth */
static void test_video_effects_depth(const char *dir)
{
    char path[512];
    IcoVideoOptions o;
    const char *text;
    FILE *f;

    ico_video_defaults(&o);
    CHECK(o.effectsDepth == 1);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    CHECK(ico_video_effects_depth() == 1);
    snprintf(path, sizeof(path), "%s/options_depth_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\neffects_depth = false\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.effectsDepth == 0 && ico_video_effects_depth() == 0);
    CHECK(effects_are(&o, 1, 1, 1, 1, 1, 1));
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    /* sanitized to 0 or 1 */
    o.effectsDepth = 5;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.effectsDepth == 1 && ico_video_effects_depth() == 1);
    o.effectsDepth = 0;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "effects_depth = false") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    CHECK(ico_video_effects_depth() == 0);
    /* the preset's shortcut leaves it alone */
    ico_video_defaults(&o);
    o.effectsDepth = 0;
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.effectsDepth == 0);
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* [video] anti_aliasing (default off): the names, the file read, the
   round trip, the Original preset forcing it off in force, and the setter */
static void test_video_anti_alias(const char *dir)
{
    static const char *const names[ICO_AA_COUNT] = {"off",   "ssaa2", "ssaa4",
                                                    "msaa2", "msaa4", "msaa8"};
    char path[512];
    IcoVideoOptions o;
    const char *text;
    FILE *f;
    int aa = -1;

    ico_video_defaults(&o);
    CHECK(o.antiAlias == ICO_AA_OFF);
    for (int i = 0; i < ICO_AA_COUNT; i++) {
        CHECK(ico_video_parse_anti_alias(names[i], &aa) == 0 && aa == i);
        CHECK(strcmp(ico_video_anti_alias_name(i), names[i]) == 0);
    }
    CHECK(ico_video_parse_anti_alias("MSAA4", &aa) == 0 && aa == ICO_AA_MSAA4);
    aa = 7;
    CHECK(ico_video_parse_anti_alias("fxaa", &aa) == -1 && aa == 7);
    CHECK(strcmp(ico_video_anti_alias_name(99), "off") == 0);
    snprintf(path, sizeof(path), "%s/options_aa_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    /* no preset (Original): the shortcut puts it off */
    fputs("[video]\nanti_aliasing = \"msaa4\"\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    CHECK(o.antiAlias == ICO_AA_OFF && ico_video_anti_alias() == ICO_AA_OFF);
    /* Original rows with it set at run time: off in force */
    o.antiAlias = ICO_AA_MSAA4;
    ico_video_set(&o);
    CHECK(ico_video_anti_alias() == ICO_AA_OFF);
    /* Enhanced: in force */
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    o.antiAlias = ICO_AA_SSAA2;
    ico_video_set(&o);
    CHECK(ico_video_anti_alias() == ICO_AA_SSAA2);
    ico_video_set_anti_alias(ICO_AA_MSAA8);
    CHECK(ico_video_anti_alias() == ICO_AA_MSAA8);
    ico_video_set_anti_alias(42);
    CHECK(ico_video_anti_alias() == ICO_AA_OFF);
    ico_video_set_anti_alias(ICO_AA_SSAA4);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "anti_aliasing = \"ssaa4\"") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    CHECK(ico_video_anti_alias() == ICO_AA_SSAA4);
    /* the Original shortcut turns it off */
    ico_video_get(&o);
    ico_video_set_preset(&o, ICO_VIDEO_ORIGINAL);
    CHECK(o.antiAlias == ICO_AA_OFF);
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* [video] full_pixel: the default, the file read, the normalising, the
   round trip; without the key On under the Enhanced rows and Off under the
   Original ones, a saved value kept either way; the Preset shortcut sets it
   (Enhanced on, Original off) */
static void test_video_full_pixel(const char *dir)
{
    char path[512];
    IcoVideoOptions o;
    const char *text;
    FILE *f;

    ico_video_defaults(&o);
    CHECK(o.fullPixel == 0);
    snprintf(path, sizeof(path), "%s/options_full_pixel_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\nfull_pixel = true\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.fullPixel == 1);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    /* normalised to 0 or 1 */
    o.fullPixel = 7;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.fullPixel == 1);
    o.fullPixel = 0;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "full_pixel = false") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.fullPixel == 0);
    /* the Preset shortcut sets it; it is not one of the rows the preset is
       derived from */
    ico_video_defaults(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.fullPixel == 1);
    o.fullPixel = 0;
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    ico_video_set_preset(&o, ICO_VIDEO_ORIGINAL);
    CHECK(o.fullPixel == 0);
    o.fullPixel = 1;
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    /* no key: by the rows */
    {
        static const struct {
            const char *text;
            int want;
        } cases[] = {
            {"[video]\npreset = \"enhanced\"\nresolution = \"window\"\naspect = \"auto\"\n"
             "texture_filter = \"anisotropic\"\nfull_height = true\n",
             1},
            {"[video]\npreset = \"original\"\n", 0},
            {"version = 1\n", 0},
            {"[video]\npreset = \"custom\"\nresolution = \"2x\"\naspect = \"16:9\"\n", 0},
            /* a saved value kept, whichever the rows */
            {"[video]\npreset = \"enhanced\"\nresolution = \"window\"\naspect = \"auto\"\n"
             "texture_filter = \"anisotropic\"\nfull_height = true\nfull_pixel = false\n",
             0},
            {"[video]\npreset = \"original\"\nfull_pixel = true\n", 1},
        };

        for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
            f = fopen(path, "wb");
            if (f == NULL) {
                failures++;
                break;
            }
            fputs(cases[k].text, f);
            fclose(f);
            ico_config_reset(path, "/nonexistent/options_test.ini");
            ico_video_reload();
            ico_video_get(&o);
            if (o.fullPixel != cases[k].want) {
                fprintf(stderr, "full_pixel case %u: %d, %d wanted\n", (unsigned)k, o.fullPixel,
                        cases[k].want);
            }
            CHECK(o.fullPixel == cases[k].want);
        }
    }
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* [video] model_pack (default on) and dump_models (default off):
   the defaults, the file read, the round trip, and the preset untouched */
static void test_video_models(const char *dir)
{
    char path[512];
    IcoVideoOptions o, d;
    const char *text;
    FILE *f;

    ico_video_defaults(&d);
    CHECK(d.modelPack == 1 && d.dumpModels == 0);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.modelPack == 1 && o.dumpModels == 0);
    CHECK(ico_video_model_pack() == 1 && ico_video_dump_models() == 0);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    snprintf(path, sizeof(path), "%s/options_models_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\nmodel_pack = false\ndump_models = true\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.modelPack == 0 && o.dumpModels == 1);
    CHECK(ico_video_model_pack() == 0 && ico_video_dump_models() == 1);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    /* run-time values are sanitized to 0 or 1 */
    o.modelPack = 5;
    o.dumpModels = -1;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.modelPack == 1 && o.dumpModels == 1);
    o.modelPack = 0;
    o.dumpModels = 0;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "model_pack = false") != NULL &&
          strstr(text, "dump_models = false") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.modelPack == 0 && o.dumpModels == 0);
    /* neither moves the preset off Original, and Enhanced's shortcut
       leaves them alone */
    ico_video_defaults(&o);
    o.modelPack = 0;
    o.dumpModels = 1;
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.modelPack == 0 && o.dumpModels == 1 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* [video] downsample (issue 149, default "off"): the default, the file read
   ("box", any case; an unknown word is off), the run-time value sanitized,
   the round trip, and the preset untouched either way (it is no preset row) */
static void test_video_downsample(const char *dir)
{
    char path[512];
    IcoVideoOptions o, d;
    const char *text;
    FILE *f;
    int v = -1;

    ico_video_defaults(&d);
    CHECK(d.downsample == ICO_DOWNSAMPLE_OFF);
    CHECK(ico_video_parse_downsample("BOX", &v) == 0 && v == ICO_DOWNSAMPLE_BOX);
    CHECK(ico_video_parse_downsample("off", &v) == 0 && v == ICO_DOWNSAMPLE_OFF);
    CHECK(ico_video_parse_downsample("on", &v) != 0);
    CHECK(strcmp(ico_video_downsample_name(ICO_DOWNSAMPLE_BOX), "box") == 0);
    CHECK(strcmp(ico_video_downsample_name(99), "off") == 0);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.downsample == ICO_DOWNSAMPLE_OFF && ico_video_downsample() == ICO_DOWNSAMPLE_OFF &&
          ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    snprintf(path, sizeof(path), "%s/options_downsample_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\ndownsample = \"box\"\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.downsample == ICO_DOWNSAMPLE_BOX && ico_video_downsample() == ICO_DOWNSAMPLE_BOX &&
          ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    o.downsample = 7;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.downsample == ICO_DOWNSAMPLE_OFF && ico_video_downsample() == ICO_DOWNSAMPLE_OFF);
    o.downsample = ICO_DOWNSAMPLE_BOX;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "downsample = \"box\"") != NULL);
    o.downsample = ICO_DOWNSAMPLE_OFF;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "downsample = \"off\"") != NULL);
    /* an unknown word reads as off */
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[video]\ndownsample = \"sharp\"\n", f);
        fclose(f);
    }
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.downsample == ICO_DOWNSAMPLE_OFF);
    /* Enhanced's shortcut leaves it alone */
    o.downsample = ICO_DOWNSAMPLE_BOX;
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.downsample == ICO_DOWNSAMPLE_BOX && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* [video] upscaling and upscaling_sharpness (issue 99, default "off" and
   80): the default, the file read ("fsr1", any case; "fsr3", not in this
   version, and an unknown word are off; a sharpness outside 0..100 is
   clamped), the run-time values sanitized, the round trip, and the preset
   untouched either way (they are no preset rows) */
static void test_video_upscale(const char *dir)
{
    char path[512];
    IcoVideoOptions o, d;
    const char *text;
    FILE *f;
    int v = -1;

    ico_video_defaults(&d);
    CHECK(d.upscale == ICO_UPSCALE_OFF && d.upscaleSharpness == 80 &&
          d.upscaleQuality == ICO_UPSCALE_Q_NATIVE);
    for (int q = 0; q < ICO_UPSCALE_Q_COUNT; q++) {
        v = -1;
        CHECK(ico_video_parse_upscale_quality(ico_video_upscale_quality_name(q), &v) == 0 &&
              v == q);
    }
    CHECK(strcmp(ico_video_upscale_quality_name(ICO_UPSCALE_Q_BALANCED), "balanced") == 0 &&
          strcmp(ico_video_upscale_quality_name(-1), "native") == 0);
    CHECK(ico_video_parse_upscale_quality("Performance", &v) == 0 &&
          v == ICO_UPSCALE_Q_PERFORMANCE);
    CHECK(ico_video_parse_upscale_quality("ultra", &v) < 0 &&
          ico_video_parse_upscale_quality(NULL, &v) < 0);
    CHECK(ico_video_parse_upscale("FSR1", &v) == 0 && v == ICO_UPSCALE_FSR1);
    CHECK(ico_video_parse_upscale("off", &v) == 0 && v == ICO_UPSCALE_OFF);
    v = -1;
    CHECK(ico_video_parse_upscale("FSR3", &v) == 0 && v == ICO_UPSCALE_FSR3);
    CHECK(ico_video_parse_upscale("dlss", &v) < 0);
    CHECK(strcmp(ico_video_upscale_name(ICO_UPSCALE_FSR1), "fsr1") == 0);
    CHECK(strcmp(ico_video_upscale_name(99), "off") == 0);
    CHECK(ico_video_upscale_sharpness_clamp(-5) == 0 &&
          ico_video_upscale_sharpness_clamp(150) == 100 &&
          ico_video_upscale_sharpness_clamp(30) == 30);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.upscale == ICO_UPSCALE_OFF && ico_video_upscale() == ICO_UPSCALE_OFF &&
          ico_video_upscale_sharpness() == 80 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    snprintf(path, sizeof(path), "%s/options_upscale_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\nupscaling = \"fsr1\"\nupscaling_sharpness = 30\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.upscale == ICO_UPSCALE_FSR1 && ico_video_upscale() == ICO_UPSCALE_FSR1 &&
          o.upscaleSharpness == 30 && ico_video_upscale_sharpness() == 30 &&
          ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    o.upscale = 7;
    o.upscaleSharpness = 400;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.upscale == ICO_UPSCALE_OFF && ico_video_upscale() == ICO_UPSCALE_OFF &&
          o.upscaleSharpness == 100);
    o.upscale = ICO_UPSCALE_FSR1;
    o.upscaleSharpness = 60;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "upscaling = \"fsr1\"") != NULL &&
          strstr(text, "upscaling_sharpness = 60") != NULL);
    o.upscale = ICO_UPSCALE_OFF;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "upscaling = \"off\"") != NULL);
    /* an unknown word, an unknown quality and a sharpness out of range read
       as off, native and clamped; "fsr3" is read as it is saved (the settings
       page and the renderer decide whether it runs) and stays through a save */
    for (int i = 0; i < 2; i++) {
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs(i ? "[video]\nupscaling = \"sharp\"\nupscaling_sharpness = -3\n"
                      "upscaling_quality = \"ultra\"\n"
                    : "[video]\nupscaling = \"fsr3\"\nupscaling_sharpness = 101\n"
                      "upscaling_quality = \"balanced\"\n",
                  f);
            fclose(f);
        }
        ico_config_reset(path, "/nonexistent/options_test.ini");
        ico_video_reload();
        ico_video_get(&o);
        CHECK(o.upscale == (i ? ICO_UPSCALE_OFF : ICO_UPSCALE_FSR3) &&
              o.upscaleSharpness == (i ? 0 : 100) &&
              o.upscaleQuality == (i ? ICO_UPSCALE_Q_NATIVE : ICO_UPSCALE_Q_BALANCED) &&
              ico_video_upscale_quality() == o.upscaleQuality);
        if (!i) {
            CHECK(ico_video_save() == 0);
            text = read_text(path);
            CHECK(text != NULL && strstr(text, "upscaling = \"fsr3\"") != NULL &&
                  strstr(text, "upscaling_quality = \"balanced\"") != NULL);
        }
    }
    o.upscaleQuality = 9;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.upscaleQuality == ICO_UPSCALE_Q_NATIVE);
    /* Enhanced's shortcut leaves them alone */
    o.upscale = ICO_UPSCALE_FSR1;
    o.upscaleSharpness = 40;
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.upscale == ICO_UPSCALE_FSR1 && o.upscaleSharpness == 40 &&
          ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* [video] show_fps (the Frame rate counter, default off): the default, the
   file read, the run-time value sanitized to 0 or 1, the round trip, and
   the preset untouched either way */
static void test_video_show_fps(const char *dir)
{
    char path[512];
    IcoVideoOptions o, d;
    const char *text;
    FILE *f;

    ico_video_defaults(&d);
    CHECK(d.showFps == 0);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.showFps == 0 && ico_video_show_fps() == 0 &&
          ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    snprintf(path, sizeof(path), "%s/options_showfps_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\nshow_fps = true\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.showFps == 1 && ico_video_show_fps() == 1 &&
          ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    o.showFps = 7;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.showFps == 1 && ico_video_show_fps() == 1);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "show_fps = true") != NULL);
    o.showFps = 0;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "show_fps = false") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.showFps == 0 && ico_video_show_fps() == 0);
    /* Enhanced's shortcut leaves it alone */
    o.showFps = 1;
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.showFps == 1 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    remove(path);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
}

/* Resolution "auto" (parsed, named, read, saved and read
   back, kept by sanitize) on any build; the Android rules (checked here on
   any computer through ico_video_set_android / ico_video_defaults_for): the
   frame rate default 60, Enhanced's resolution "auto" both ways (the
   shortcut writes it, the classifier calls it Enhanced, "window" is then
   Custom), Original 1x as everywhere, and a file with no framerate or
   resolution key read as 60 and "auto" */
static void test_video_auto(const char *dir)
{
    char path[512], buf[32];
    IcoVideoOptions o, d;
    const char *text;
    FILE *f;
    int fr = 0;

    ico_video_defaults(&o);
    CHECK(ico_video_parse_resolution("auto", &o) == 0 && o.resScale == ICO_RES_AUTO &&
          o.resW == 0 && o.resH == 0);
    CHECK(strcmp(ico_video_resolution_name(&o, buf, sizeof(buf)), "auto") == 0);
    CHECK(ico_video_parse_resolution("AUTO", &o) == 0 && o.resScale == ICO_RES_AUTO);
    CHECK(ico_video_parse_resolution("auto2", &o) == -1 && o.resScale == ICO_RES_AUTO);
    /* 48:9 and 16x parse and print back as they were written */
    {
        int asp = -1;

        CHECK(ico_video_parse_aspect("48:9", &asp) == 0 && asp == ICO_ASPECT_48_9 &&
              strcmp(ico_video_aspect_name(asp), "48:9") == 0);
        CHECK(ico_video_parse_aspect("auto", &asp) == 0 && asp == ICO_ASPECT_AUTO &&
              strcmp(ico_video_aspect_name(asp), "auto") == 0);
        CHECK(ico_video_parse_resolution("16x", &o) == 0 && o.resScale == 16 &&
              strcmp(ico_video_resolution_name(&o, buf, sizeof(buf)), "16x") == 0);
        CHECK(ico_video_parse_resolution("17x", &o) == -1 && o.resScale == 16);
        CHECK(ico_video_parse_resolution("auto", &o) == 0);
    }
    /* the PC rules: "auto" is not Enhanced's; Enhanced keeps "window"; the
       default frame rate stays "uncapped" */
    ico_video_set_android(0);
    ico_video_defaults(&d);
    CHECK(d.framerate == ICO_FRAMERATE_UNCAPPED && d.resScale == 1);
    ico_video_set_preset(&d, ICO_VIDEO_ENHANCED);
    CHECK(d.resScale == 0 && ico_video_preset(&d) == ICO_VIDEO_ENHANCED);
    d.resScale = ICO_RES_AUTO;
    CHECK(ico_video_preset(&d) == ICO_VIDEO_CUSTOM);
    /* the round trip: Enhanced with resolution = auto, read, kept, saved */
    snprintf(path, sizeof(path), "%s/options_auto_test.toml", dir);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        failures++;
        return;
    }
    fputs("[video]\npreset = \"enhanced\"\nresolution = \"auto\"\n", f);
    fclose(f);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.resScale == ICO_RES_AUTO && o.resW == 0 && o.resH == 0);
    ico_video_set(&o); /* sanitize keeps it */
    ico_video_get(&o);
    CHECK(o.resScale == ICO_RES_AUTO);
    CHECK(ico_video_save() == 0);
    text = read_text(path);
    CHECK(text != NULL && strstr(text, "resolution = \"auto\"") != NULL);
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.resScale == ICO_RES_AUTO);
    /* the getter the Options row reads */
    ico_video_set_auto_scale(2);
    CHECK(ico_video_auto_scale() == 2);
    ico_video_set_auto_scale(-3);
    CHECK(ico_video_auto_scale() == 0);
    /* the Android rules */
    ico_video_defaults_for(&d, 1);
    CHECK(d.framerate == 60 && ico_video_default_framerate(1) == 60);
    CHECK(ico_video_default_framerate(0) == ICO_FRAMERATE_UNCAPPED);
    CHECK(d.resScale == 1 && d.aspect == ICO_ASPECT_4_3 && d.filter == ICO_FILTER_ORIGINAL &&
          !d.fullHeight);
    CHECK(strcmp(ico_video_framerate_name(d.framerate, buf, sizeof(buf)), "60") == 0 &&
          ico_video_parse_framerate(buf, &fr) == 0 && fr == 60);
    ico_video_set_android(1);
    CHECK(ico_video_android() == 1);
    ico_video_defaults(&o);
    CHECK(o.framerate == 60 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    /* the window's size on Android too (v0.10.0: Auto is the player's choice, never the
       preset's) */
    CHECK(o.resScale == 0 && o.aspect == ICO_ASPECT_AUTO && o.filter == ICO_FILTER_ANISOTROPIC &&
          o.fullHeight);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    o.resScale =
        ICO_RES_AUTO; /* a saved Enhanced with "auto" (v0.9.x's Android preset) stays Enhanced */
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    o.resScale = 2; /* any other resolution is Custom */
    CHECK(ico_video_preset(&o) == ICO_VIDEO_CUSTOM);
    ico_video_set_preset(&o, ICO_VIDEO_ORIGINAL);
    CHECK(o.resScale == 1 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    /* a framerate out of range falls back to the Android default */
    o.framerate = 5;
    ico_video_set(&o);
    ico_video_get(&o);
    CHECK(o.framerate == 60);
    /* a file with Enhanced and no resolution or framerate key */
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[video]\npreset = \"enhanced\"\naspect = \"auto\"\ntexture_filter = "
              "\"anisotropic\"\nfull_height = true\n",
              f);
        fclose(f);
    }
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.resScale == 0 && o.framerate == 60 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    /* a file with Enhanced at resolution "window" keeps its preset name and its resolution */
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs("[video]\npreset = \"enhanced\"\nresolution = \"window\"\naspect = "
              "\"auto\"\ntexture_filter = \"anisotropic\"\nfull_height = true\n",
              f);
        fclose(f);
    }
    ico_config_reset(path, "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.resScale == 0 && o.resW == 0 && o.resH == 0 &&
          ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    CHECK(o.resScale == 0 && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    /* and no file at all: Original at 1x, 60 a second */
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.resScale == 1 && o.framerate == 60 && ico_video_preset(&o) == ICO_VIDEO_ORIGINAL);
    ico_video_set_android(0);
    remove(path);
    ico_video_reload();
}

/* ico_video_interpolate: 1 for every framerate but "original", whatever
   the preset (the window's rule; GsBase.c's cull margin follows it) */
static void test_video_interpolate(void)
{
    static const int rates[] = {ICO_FRAMERATE_UNCAPPED, 30, 60, 144, 1000};
    IcoVideoOptions o, keep;

    ico_video_get(&keep);
    ico_video_defaults(&o);
    ico_video_set_preset(&o, ICO_VIDEO_ORIGINAL);
    o.framerate = ICO_FRAMERATE_ORIGINAL;
    ico_video_set(&o);
    CHECK(ico_video_framerate() == ICO_FRAMERATE_ORIGINAL && ico_video_interpolate() == 0);
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        o.framerate = rates[i];
        ico_video_set(&o);
        CHECK(ico_video_framerate() == rates[i] && ico_video_interpolate() == 1);
    }
    /* the preset does not enter into it */
    ico_video_set_preset(&o, ICO_VIDEO_ENHANCED);
    o.framerate = ICO_FRAMERATE_ORIGINAL;
    ico_video_set(&o);
    CHECK(ico_video_interpolate() == 0);
    o.framerate = ICO_FRAMERATE_UNCAPPED;
    ico_video_set(&o);
    CHECK(ico_video_interpolate() == 1);
    ico_video_set(&keep);
}

/* The window's "display changed" words (ico_video_changes_text): the fields
   that differ in order, in the startup line's words, "" when none does; a
   phone's switch from Custom 2x auto to Original, written as the window
   wrote it before the words moved here */
static void test_video_changes_text(void)
{
    IcoVideoOptions was, now;
    char line[512];

    ico_video_defaults(&now);
    ico_video_set_preset(&now, ICO_VIDEO_ORIGINAL);
    CHECK(ico_video_changes_text(&now, &now, line, sizeof(line)) == 0 && line[0] == '\0');
    was = now;
    was.resScale = 2;
    was.aspect = ICO_ASPECT_AUTO;
    CHECK(ico_video_preset(&was) == ICO_VIDEO_CUSTOM);
    CHECK(ico_video_changes_text(&was, &now, line, sizeof(line)) == (int)strlen(line));
    CHECK(strcmp(line, "preset Custom -> Original, resolution 2x -> 1x, aspect auto -> 4:3") == 0);
    was = now;
    was.vsync = !now.vsync;
    was.texturePack = !now.texturePack;
    (void)ico_video_changes_text(&was, &now, line, sizeof(line));
    CHECK(strstr(line, "vsync ") == line && strstr(line, ", texture pack ") != NULL);
    /* Motion blur strength in the words (issue 181) */
    was = now;
    was.effectMotionBlurStrength = 50;
    (void)ico_video_changes_text(&was, &now, line, sizeof(line));
    CHECK(strcmp(line, "motion blur strength 50 % -> 100 %") == 0);
    /* cut at the size, never past it */
    CHECK(ico_video_changes_text(&was, &now, line, 8) < 8 && strlen(line) < 8);
}

/* options_test DIR [SLUG]: SLUG the selected disc (config/regions.txt: pal,
   the default, jpn or kor) */
int main(int argc, char **argv)
{
    if (argc > 2 && ico_region_select(argv[2]) != 0) {
        fprintf(stderr, "options_test: no region %s\n", argv[2]);
        return 1;
    }
    printf("options_test: the %s disc\n", ico_region_cur()->slug);
    test_defaults();
    test_config(argc > 1 ? argv[1] : ".");
    test_mirror_slots(argc > 1 ? argv[1] : ".");
    test_hold_discs(argc > 1 ? argv[1] : ".");
    test_mirror_discs(argc > 1 ? argv[1] : ".");
    test_players_slots(argc > 1 ? argv[1] : ".");
    test_pit_time();
    test_shadow_speed();
    test_shadow_speed_motions();
    test_yorda_trips();
    test_shadows_together(argc > 1 ? argv[1] : ".");
    test_brain();
    test_ui_language(argc > 1 ? argv[1] : ".");
    test_text_language(argc > 1 ? argv[1] : ".");
    test_video_effects(argc > 1 ? argv[1] : ".");
    test_video_effects_depth(argc > 1 ? argv[1] : ".");
    test_video_full_pixel(argc > 1 ? argv[1] : ".");
    test_video_anti_alias(argc > 1 ? argv[1] : ".");
    test_video_models(argc > 1 ? argv[1] : ".");
    test_video_show_fps(argc > 1 ? argv[1] : ".");
    test_video_downsample(argc > 1 ? argv[1] : ".");
    test_video_upscale(argc > 1 ? argv[1] : ".");
    test_video_auto(argc > 1 ? argv[1] : ".");
    test_video_interpolate();
    test_video_changes_text();
    if (failures != 0) {
        fprintf(stderr, "options_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("options_test: ok\n");
    return 0;
}
