/*
 * port/game/test/options_test.c
 *
 * The gameplay options on the CPU: the defaults are
 * the original game's, the config and run-time setters, and eBrainGetTarget
 * (ico2/omori/src/ebrain.c, compiled in below so its statics are in reach)
 * with yorda_safe off and on, on a synthetic slot set.
 */
#include <stdio.h>
#include <string.h>
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
    ico_opt_set_stick_fix(1);
    ico_opt_set_mirror(1);
    CHECK(ico_opt_stick_fix() == 1 && ico_opt_mirror() == 1);
    ico_opt_reload();
    CHECK(ico_opt_stick_fix() == ico_opt_stick_fix_default() && ico_opt_mirror() == 0);
    CHECK(ico_opt_developer_mode() == 0);
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
          "developer_mode = true\nnatural_chains = true\nskip_opening = true\n[dev]\n"
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
   the eight; each name its UiLang number (port/ui/strings.h order); the
   setter and the names the Settings menu writes */
static void test_ui_language(const char *dir)
{
    static const char *const names[8] = {"en", "fr", "de", "it", "es", "ja", "ko", "zh"};
    char path[512];
    char text[64];
    FILE *f;

    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_ui_language() == -1);
    CHECK(strcmp(ico_opt_ui_language_name(-1), "auto") == 0);
    snprintf(path, sizeof(path), "%s/options_test.toml", dir);
    for (int i = 0; i < 8 + 2; i++) {
        const char *v = i < 8 ? names[i] : i == 8 ? "auto" : "klingon";
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
        CHECK(ico_opt_ui_language() == (i < 8 ? i : -1));
        CHECK(strcmp(ico_opt_ui_language_name(ico_opt_ui_language()), i < 8 ? v : "auto") == 0);
    }
    remove(path);
    ico_opt_set_ui_language(5);
    CHECK(ico_opt_ui_language() == 5);
    ico_opt_set_ui_language(8);
    CHECK(ico_opt_ui_language() == -1);
    ico_opt_set_ui_language(7);
    CHECK(ico_opt_ui_language() == 7);
    ico_opt_set_ui_language(9);
    CHECK(ico_opt_ui_language() == -1);
    ico_config_reset("/nonexistent/options_test.toml", "/nonexistent/options_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_ui_language() == -1);
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
    CHECK(o.resScale == ICO_RES_AUTO && o.aspect == ICO_ASPECT_AUTO &&
          o.filter == ICO_FILTER_ANISOTROPIC && o.fullHeight);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    o.resScale = 0; /* a saved Enhanced with "window" stays Enhanced */
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
    CHECK(o.resScale == ICO_RES_AUTO && o.framerate == 60 &&
          ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
    /* a file with Enhanced at resolution "window" (written before "auto"
       existed) keeps its preset name and its resolution; the shortcut then
       writes "auto" */
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
    CHECK(o.resScale == ICO_RES_AUTO && ico_video_preset(&o) == ICO_VIDEO_ENHANCED);
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
    test_brain();
    test_ui_language(argc > 1 ? argv[1] : ".");
    test_video_effects(argc > 1 ? argv[1] : ".");
    test_video_effects_depth(argc > 1 ? argv[1] : ".");
    test_video_full_pixel(argc > 1 ? argv[1] : ".");
    test_video_anti_alias(argc > 1 ? argv[1] : ".");
    test_video_models(argc > 1 ? argv[1] : ".");
    test_video_show_fps(argc > 1 ? argv[1] : ".");
    test_video_auto(argc > 1 ? argv[1] : ".");
    test_video_interpolate();
    if (failures != 0) {
        fprintf(stderr, "options_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("options_test: ok\n");
    return 0;
}
