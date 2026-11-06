/*
 * port/game/test/variants_test.c
 *
 * The game variants (port/game/variants.h) on the CPU: every default is the
 * game as shipped, every key round trips through config.toml, a word or a
 * number that is not a value reads as the default, and the achievements rule
 * (One hit per swing, Shadows never dodge, Cutscene cameras ignored and the
 * Debug menu switch them off; the other four do not).  The game hooks: the
 * debug switches the game binds (written only while a variant holds them,
 * the game's value back after), the Debug menu's opener, the hint rule per
 * disc, the enemy row reader on the disc's tables (ico2/sugipon/src/
 * enemyKind.c, compiled in: the disc's own row while nothing is chosen,
 * American = the USA life with the disc's dodge, aggression = the battle
 * type, never past the table: the June 2001 demo's 18 rows, which are never
 * mapped by index; American is not offered on the demo: test_offered) and the restart
 * life's playthrough index (a copy of enemy_act.c getEnemyRestartLife's).
 * The weapon rows (test_weapons): the kinds InitWeaponGeo and PutWeapon read
 * through ico_variant_weapon_kind and ico_variant_weapon_put_kind on this
 * disc (Red only where the light sword exists, the net's kind 6 on the June
 * 2001 demo and 7 elsewhere, every other kind untouched), the keys' words.
 *
 *   variants_test DIR [REGION]
 */
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "options.h"
#include "../../core/region.h"
#include "../../../ico2/sugipon/include/enemy.h"
#include "variants.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static char s_path[512];

/* the June 2001 demo is selected (American is not offered there) */
static int on_jun26(void)
{
    return strcmp(ico_region_cur()->slug, "jun26") == 0;
}

/* Shadow toughness Third journey is offered on the discs whose restart life
   multiplies by gameParam[38 + playthroughs]: the PAL, Japanese, Korean and
   Chinese ones (their gameParam has 44 entries; the North American disc's 36,
   the August 2001 build's 36 and the June 2001 demo's 32 have no entry 38..43) */
static int third_offered(void)
{
    const char *s = ico_region_cur()->slug;
    return strcmp(s, "ntsc") != 0 && strcmp(s, "aug6") != 0 && strcmp(s, "jun26") != 0;
}

/* one of the three 2001-family discs (North American, August 2001, June
   2001 demo): no light sword there */
static int y2001(void)
{
    const char *slug = ico_region_cur()->slug;
    return strcmp(slug, "ntsc") == 0 || strcmp(slug, "aug6") == 0 || strcmp(slug, "jun26") == 0;
}

/* config.toml with TEXT as its body (NULL: no file), options reloaded */
static void use_config(const char *text)
{
    FILE *f;

    remove(s_path);
    if (text != NULL) {
        f = fopen(s_path, "wb");
        if (f == NULL) {
            fprintf(stderr, "cannot write %s\n", s_path);
            failures++;
            return;
        }
        fputs(text, f);
        fclose(f);
    }
    ico_config_reset(s_path, "/nonexistent/variants_test.ini");
    ico_opt_reload();
}

static void test_defaults(void)
{
    use_config(NULL);
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_ORIGINAL);
    CHECK(ico_variant_one_hit() == 0);
    CHECK(ico_variant_never_dodge() == 0);
    CHECK(ico_variant_aggression() == ICO_AGGRESSION_OWN);
    CHECK(ico_variant_no_hints() == 0);
    CHECK(ico_variant_barrier_hits() == (strcmp(ico_region_cur()->slug, "jun26") == 0 ? 7 : 5));
    CHECK(ico_variant_no_cutscene_look() == 0);
    CHECK(ico_variant_ignore_cutscene_cameras() == 0);
    /* the Combat rows: all three On, the game as shipped */
    CHECK(ico_variant_auto_guard() == 1);
    CHECK(ico_variant_attack_turn() == 1);
    CHECK(ico_variant_shadows_wait_down() == 1);
    /* the weapon rows: Original, every kind as placed */
    CHECK(ico_variant_get(ICO_VARIANT_SWORD_COLOUR) == ICO_SWORD_ORIGINAL);
    CHECK(ico_variant_get(ICO_VARIANT_WEAPON) == ICO_WEAPON_ORIGINAL);
    for (int k = 0; k < 16; k++) {
        CHECK(ico_variant_weapon_kind(k) == k && ico_variant_weapon_put_kind(k) == k);
    }
    CHECK(ico_variant_shadow_brain() == 1); /* Peaceful shadows Off: the game's */
    CHECK(ico_variants_suspend_achievements() == 0);
    /* the shipped file's values are the defaults */
    use_config("[variants]\nshade_toughness = \"original\"\none_hit = false\n"
               "never_dodge = false\naggression = \"own\"\nno_hints = false\n"
               "no_cutscene_look = false\nignore_cutscene_cameras = false\n"
               "auto_guard = true\nattack_turn = true\nshadows_wait_down = true\n"
               "peaceful_shadows = false\n"
               "sword_colour = \"original\"\nweapon = \"original\"\n");
    CHECK(ico_variant_shadow_brain() == 1);
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_ORIGINAL);
    CHECK(ico_variant_aggression() == ICO_AGGRESSION_OWN);
    CHECK(ico_variant_auto_guard() == 1 && ico_variant_attack_turn() == 1 &&
          ico_variant_shadows_wait_down() == 1);
    CHECK(ico_variants_suspend_achievements() == 0);
}

/* the names: fourteen variants, the words the keys are written with */
static void test_names(void)
{
    CHECK(ico_variant_count(ICO_VARIANT_SHADE_TOUGHNESS) == 4);
    CHECK(ico_variant_count(ICO_VARIANT_PEACEFUL_SHADOWS) == 2);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_PEACEFUL_SHADOWS), "peaceful_shadows") == 0);
    CHECK(ico_variant_count(ICO_VARIANT_AGGRESSION) == 5);
    CHECK(ico_variant_count(ICO_VARIANT_BARRIER_HITS) == 2);
    CHECK(ico_variant_count(ICO_VARIANT_ONE_HIT) == 2);
    CHECK(ico_variant_count(ICO_VARIANT_COUNT) == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_SHADE_TOUGHNESS), "shade_toughness") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_ONE_HIT), "one_hit") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_NEVER_DODGE), "never_dodge") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_AGGRESSION), "aggression") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_NO_HINTS), "no_hints") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_BARRIER_HITS), "barrier_hits") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_NO_CUTSCENE_LOOK), "no_cutscene_look") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS), "ignore_cutscene_cameras") ==
          0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_AUTO_GUARD), "auto_guard") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_ATTACK_TURN), "attack_turn") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_SHADOWS_WAIT_DOWN), "shadows_wait_down") == 0);
    CHECK(ico_variant_count(ICO_VARIANT_AUTO_GUARD) == 2 &&
          ico_variant_count(ICO_VARIANT_ATTACK_TURN) == 2 &&
          ico_variant_count(ICO_VARIANT_SHADOWS_WAIT_DOWN) == 2);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AUTO_GUARD, 0), "false") == 0 &&
          strcmp(ico_variant_value_name(ICO_VARIANT_AUTO_GUARD, 1), "true") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, 0), "original") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, 1), "american") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, 2), "no_plus") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, 3), "third_journey") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, 4), "") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AGGRESSION, 0), "own") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AGGRESSION, 1), "bold_patient") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AGGRESSION, 2), "bold_quick") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AGGRESSION, 3), "wary_patient") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AGGRESSION, 4), "wary_quick") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_BARRIER_HITS, 0), "5") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_BARRIER_HITS, 1), "7") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_NO_HINTS, 1), "true") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_SWORD_COLOUR), "sword_colour") == 0);
    CHECK(strcmp(ico_variant_key(ICO_VARIANT_WEAPON), "weapon") == 0);
    CHECK(ico_variant_count(ICO_VARIANT_SWORD_COLOUR) == 2 &&
          ico_variant_count(ICO_VARIANT_WEAPON) == 2);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_SWORD_COLOUR, 0), "original") == 0 &&
          strcmp(ico_variant_value_name(ICO_VARIANT_SWORD_COLOUR, 1), "red") == 0 &&
          strcmp(ico_variant_value_name(ICO_VARIANT_SWORD_COLOUR, 2), "") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_WEAPON, 0), "original") == 0 &&
          strcmp(ico_variant_value_name(ICO_VARIANT_WEAPON, 1), "net") == 0);
}

/* every key: step through every value, save, reload, the same value back;
   the file names the key under [variants] */
static void test_round_trip(void)
{
    for (int id = 0; id < ICO_VARIANT_COUNT; id++) {
        const int n = ico_variant_count((IcoVariantId)id);

        for (int want = 0; want < n; want++) {
            int v;

            if (!ico_variant_offered((IcoVariantId)id, want)) {
                continue; /* American on the June 2001 demo: test_offered */
            }
            use_config(NULL);
            v = ico_variant_get((IcoVariantId)id);
            /* step forward to value WANT (wrapping); the shipped value needs
               no step, and then the key is not written */
            const int stepped = v != want;
            while (v != want) {
                v = ico_variant_step((IcoVariantId)id, 1);
            }
            CHECK(ico_variant_get((IcoVariantId)id) == want);
            CHECK(ico_config_save() == 0);
            {
                char path[64];
                snprintf(path, sizeof(path), "variants.%s", ico_variant_key((IcoVariantId)id));
                CHECK((ico_config_get_string(path, NULL) != NULL) == stepped);
            }
            ico_config_reset(s_path, "/nonexistent/variants_test.ini");
            ico_opt_reload();
            CHECK(ico_variant_get((IcoVariantId)id) == want);
        }
    }
    /* the accessors read the same values */
    use_config(NULL);
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_NO_PLUS);
    ico_variant_set(ICO_VARIANT_AGGRESSION, ICO_AGGRESSION_WARY_QUICK);
    ico_variant_set(ICO_VARIANT_BARRIER_HITS, 1);
    ico_variant_set(ICO_VARIANT_NO_HINTS, 1);
    ico_variant_set(ICO_VARIANT_NO_CUTSCENE_LOOK, 1);
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_NO_PLUS);
    CHECK(ico_variant_aggression() == ICO_AGGRESSION_WARY_QUICK);
    CHECK(ico_variant_barrier_hits() == 7);
    CHECK(ico_variant_no_hints() == 1);
    CHECK(ico_variant_no_cutscene_look() == 1);
    /* a clamped set (to Third journey, Original where it is not offered), a
       wrapping step in both directions */
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, 9);
    CHECK(ico_variant_shade_toughness() ==
          (third_offered() ? ICO_TOUGHNESS_THIRD_JOURNEY : ICO_TOUGHNESS_ORIGINAL));
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, -4);
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_ORIGINAL);
    CHECK(ico_variant_step(ICO_VARIANT_SHADE_TOUGHNESS, -1) ==
          (third_offered() ? ICO_TOUGHNESS_THIRD_JOURNEY : ICO_TOUGHNESS_NO_PLUS));
    CHECK(ico_variant_step(ICO_VARIANT_SHADE_TOUGHNESS, 1) == ICO_TOUGHNESS_ORIGINAL);
    /* a reload goes back to the config's */
    ico_opt_reload();
    CHECK(ico_variant_no_hints() == 0 && ico_variant_aggression() == ICO_AGGRESSION_OWN);
}

/* what a file may say */
static void test_file_words(void)
{
    use_config("[variants]\nshade_toughness = \"american\"\naggression = \"bold_quick\"\n"
               "barrier_hits = 7\none_hit = true\nno_hints = true\n");
    /* the June 2001 demo reads American as Original (test_offered) */
    CHECK(ico_variant_shade_toughness() ==
          (on_jun26() ? ICO_TOUGHNESS_ORIGINAL : ICO_TOUGHNESS_AMERICAN));
    CHECK(ico_variant_aggression() == ICO_AGGRESSION_BOLD_QUICK);
    CHECK(ico_variant_barrier_hits() == 7);
    CHECK(ico_variant_one_hit() == 1 && ico_variant_no_hints() == 1);
    CHECK(ico_variant_never_dodge() == 0);
    /* barrier_hits = 5 names 5 whatever the disc ships */
    use_config("[variants]\nbarrier_hits = 5\n");
    CHECK(ico_variant_barrier_hits() == 5);
    /* a word or a number that is no value reads as the default */
    use_config("[variants]\nshade_toughness = \"sideways\"\naggression = \"furious\"\n"
               "barrier_hits = 6\n");
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_ORIGINAL);
    CHECK(ico_variant_aggression() == ICO_AGGRESSION_OWN);
    CHECK(ico_variant_barrier_hits() == (strcmp(ico_region_cur()->slug, "jun26") == 0 ? 7 : 5));
}

/* The Combat rows through the config path: each hook is 1 with no key, 0 when
   the file says false (the others stay 1), steps to 0 and writes its key, and
   none of the three suspends achievements. The hooks are the externs the game's
   commonact.c, boyact.c (twice) read, so their value here is the value the
   game's conjunct sees. */
static void test_combat(void)
{
    static const struct {
        IcoVariantId id;
        const char *key;
        int (*hook)(void);
    } rows[3] = {
        {ICO_VARIANT_AUTO_GUARD, "auto_guard", ico_variant_auto_guard},
        {ICO_VARIANT_ATTACK_TURN, "attack_turn", ico_variant_attack_turn},
        {ICO_VARIANT_SHADOWS_WAIT_DOWN, "shadows_wait_down", ico_variant_shadows_wait_down}};

    for (int i = 0; i < 3; i++) {
        char text[96], path[64];

        use_config(NULL);
        CHECK(rows[i].hook() == 1);
        snprintf(text, sizeof(text), "[variants]\n%s = false\n", rows[i].key);
        use_config(text);
        for (int j = 0; j < 3; j++) {
            CHECK(rows[j].hook() == (j == i ? 0 : 1));
        }
        CHECK(ico_variants_suspend_achievements() == 0);
        /* a bad word reads as the default (On) */
        snprintf(text, sizeof(text), "[variants]\n%s = \"sideways\"\n", rows[i].key);
        use_config(text);
        CHECK(rows[i].hook() == 1);
        /* the page's step: On -> Off writes the key, Off -> On puts it back */
        use_config(NULL);
        snprintf(path, sizeof(path), "variants.%s", rows[i].key);
        CHECK(ico_variant_step(rows[i].id, 1) == 0 && rows[i].hook() == 0);
        CHECK(ico_config_get_bool(path, 1) == 0);
        CHECK(ico_variants_suspend_achievements() == 0);
        CHECK(ico_variant_step(rows[i].id, 1) == 1 && rows[i].hook() == 1);
        CHECK(ico_config_get_bool(path, 0) == 1);
        /* a reload goes back to the file */
        ico_variant_set(rows[i].id, 0);
        ico_opt_reload();
        CHECK(rows[i].hook() == 1);
    }
    use_config(NULL);
}

/* The weapon rows: the kinds weapon.c InitWeaponGeo (every disc's copy) and
   boyact.c PutWeapon read.  Red makes the light sword's kind 9 the red kind
   8 where the light sword exists and is read as Original elsewhere; the net
   makes the stick's kind 1 the disc's net kind (6 on the June 2001 demo, 7
   elsewhere); no other kind moves; neither row suspends achievements; the
   keys are words; a reload goes back to the file. */
static void test_weapons(void)
{
    const int light = !y2001();
    const int net = on_jun26() ? 6 : 7;

    use_config("[variants]\nsword_colour = \"red\"\n");
    CHECK(ico_variant_get(ICO_VARIANT_SWORD_COLOUR) == (light ? ICO_SWORD_RED : 0));
    CHECK(ico_variant_weapon_kind(9) == (light ? 8 : 9));
    CHECK(ico_variant_weapon_put_kind(8) == (light ? 9 : 8));
    CHECK(ico_variant_weapon_put_kind(9) == 9);
    for (int k = 0; k < 16; k++) {
        if (k != 9) {
            CHECK(ico_variant_weapon_kind(k) == k);
        }
        if (k != 8) {
            CHECK(ico_variant_weapon_put_kind(k) == k);
        }
    }
    CHECK(ico_variants_suspend_achievements() == 0);

    use_config("[variants]\nweapon = \"net\"\n");
    CHECK(ico_variant_net_kind() == net);
    CHECK(ico_variant_get(ICO_VARIANT_WEAPON) == ICO_WEAPON_NET);
    CHECK(ico_variant_weapon_kind(1) == net);
    for (int k = 0; k < 16; k++) {
        if (k != 1) {
            CHECK(ico_variant_weapon_kind(k) == k);
        }
        CHECK(ico_variant_weapon_put_kind(k) == k);
    }
    CHECK(ico_variants_suspend_achievements() == 0);

    /* both at once: each its own kind */
    use_config("[variants]\nsword_colour = \"red\"\nweapon = \"net\"\n");
    CHECK(ico_variant_weapon_kind(1) == net && ico_variant_weapon_kind(9) == (light ? 8 : 9));

    /* a word that is no value reads as Original */
    use_config("[variants]\nsword_colour = \"blue\"\nweapon = \"spoon\"\n");
    CHECK(ico_variant_get(ICO_VARIANT_SWORD_COLOUR) == 0 &&
          ico_variant_get(ICO_VARIANT_WEAPON) == 0);
    CHECK(ico_variant_weapon_kind(1) == 1 && ico_variant_weapon_kind(9) == 9);

    /* the page's step writes the word; Red is passed over where not offered */
    use_config(NULL);
    CHECK(ico_variant_step(ICO_VARIANT_WEAPON, 1) == ICO_WEAPON_NET);
    CHECK(strcmp(ico_config_get_string("variants.weapon", ""), "net") == 0);
    CHECK(ico_variant_step(ICO_VARIANT_WEAPON, 1) == ICO_WEAPON_ORIGINAL);
    CHECK(strcmp(ico_config_get_string("variants.weapon", ""), "original") == 0);
    CHECK(ico_variant_step(ICO_VARIANT_SWORD_COLOUR, 1) == (light ? ICO_SWORD_RED : 0));
    if (light) {
        CHECK(strcmp(ico_config_get_string("variants.sword_colour", ""), "red") == 0);
        CHECK(ico_variant_weapon_kind(9) == 8);
        /* a fresh config with no file goes back to Original (the step wrote
           the word into the loaded config, so a plain reload keeps it) */
        use_config(NULL);
        CHECK(ico_variant_weapon_kind(9) == 9);
    }
    use_config(NULL);
}

/* the achievements rule */
static void test_suspend(void)
{
    static const IcoVariantId cheats[] = {ICO_VARIANT_ONE_HIT, ICO_VARIANT_NEVER_DODGE,
                                          ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS,
                                          ICO_VARIANT_PEACEFUL_SHADOWS};
    static const IcoVariantId harmless[] = {ICO_VARIANT_NO_HINTS, ICO_VARIANT_NO_CUTSCENE_LOOK,
                                            ICO_VARIANT_BARRIER_HITS};

    for (int i = 0; i < 4; i++) {
        use_config(NULL);
        CHECK(ico_variants_suspend_achievements() == 0);
        ico_variant_set(cheats[i], 1);
        CHECK(ico_variants_suspend_achievements() == 1);
        ico_variant_set(cheats[i], 0);
        CHECK(ico_variants_suspend_achievements() == 0);
    }
    /* each of the others, at every value, leaves them on */
    for (int i = 0; i < 3; i++) {
        use_config(NULL);
        for (int v = 0; v < 2; v++) {
            ico_variant_set(harmless[i], v);
            CHECK(ico_variants_suspend_achievements() == 0);
        }
    }
    use_config(NULL);
    for (int v = 0; v < ICO_TOUGHNESS_COUNT; v++) {
        ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, v);
        CHECK(ico_variants_suspend_achievements() == 0);
    }
    for (int v = 0; v < ICO_AGGRESSION_COUNT; v++) {
        ico_variant_set(ICO_VARIANT_AGGRESSION, v);
        CHECK(ico_variants_suspend_achievements() == 0);
    }
    /* two cheats, one off: still suspended */
    ico_variant_set(ICO_VARIANT_ONE_HIT, 1);
    ico_variant_set(ICO_VARIANT_NEVER_DODGE, 1);
    ico_variant_set(ICO_VARIANT_ONE_HIT, 0);
    CHECK(ico_variants_suspend_achievements() == 1);
    /* a file with one on starts suspended */
    use_config("[variants]\nignore_cutscene_cameras = true\n");
    CHECK(ico_variants_suspend_achievements() == 1);
    use_config("[variants]\npeaceful_shadows = true\n");
    CHECK(ico_variants_suspend_achievements() == 1);
    /* the Debug menu: the press marks it used until cleared or reloaded */
    use_config(NULL);
    CHECK(ico_variants_suspend_achievements() == 0);
    /* nothing bound here: the press returns 0 (Options stays) and does not
       count as used (the menu never opened) */
    CHECK(ico_variants_debug_menu_pressed() == 0);
    CHECK(ico_variants_suspend_achievements() == 0);
    ico_variants_debug_menu_clear();
    CHECK(ico_variants_suspend_achievements() == 0);
}

/* the game's switches as debug_VariableInit leaves them on the European
   disc (retail), and the debug menu's opener */
static int s_one_hit, s_dodge, s_battle, s_demo_cam, s_hand, s_opens;

static void fake_menu_open(void)
{
    s_opens++;
}

static void bind_retail(void)
{
    s_one_hit = 0;
    s_dodge = 0;
    s_battle = 3;
    s_demo_cam = 0;
    s_hand = 1;
    ico_variants_bind_debug(&s_one_hit, &s_dodge, &s_battle, &s_demo_cam, &s_hand, fake_menu_open);
}

static int retail(void)
{
    return s_one_hit == 0 && s_dodge == 0 && s_battle == 3 && s_demo_cam == 0 && s_hand == 1;
}

/* ico_variants_apply through the bind: nothing written at the shipped
   values, each variant's value while it is on, the game's value back after */
static void test_apply(void)
{
    use_config(NULL);
    bind_retail();
    CHECK(retail());
    /* every shipped value, set again: still untouched */
    for (int id = 0; id < ICO_VARIANT_COUNT; id++) {
        ico_variant_set((IcoVariantId)id, ico_variant_get((IcoVariantId)id));
    }
    CHECK(retail());
    ico_variant_set(ICO_VARIANT_ONE_HIT, 1);
    CHECK(s_one_hit == 1);
    ico_variant_set(ICO_VARIANT_NEVER_DODGE, 1);
    CHECK(s_dodge == 1);
    ico_variant_set(ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS, 1);
    CHECK(s_demo_cam == 1);
    ico_variant_set(ICO_VARIANT_NO_CUTSCENE_LOOK, 1);
    CHECK(s_hand == 0);
    /* aggression: as debug.c names the battle types (0 Positive-Low,
       1 Negative-Low, 2 Positive-High, 3 Negative-High) */
    ico_variant_set(ICO_VARIANT_AGGRESSION, ICO_AGGRESSION_BOLD_PATIENT);
    CHECK(s_battle == 0);
    ico_variant_set(ICO_VARIANT_AGGRESSION, ICO_AGGRESSION_BOLD_QUICK);
    CHECK(s_battle == 2);
    ico_variant_set(ICO_VARIANT_AGGRESSION, ICO_AGGRESSION_WARY_PATIENT);
    CHECK(s_battle == 1);
    ico_variant_set(ICO_VARIANT_AGGRESSION, ICO_AGGRESSION_WARY_QUICK);
    CHECK(s_battle == 3);
    /* the toughness, the hints and the barrier hold no switch */
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_AMERICAN);
    ico_variant_set(ICO_VARIANT_NO_HINTS, 1);
    ico_variant_set(ICO_VARIANT_BARRIER_HITS, 1);
    /* a reload: the config's (shipped) values, the game's values back */
    ico_opt_reload();
    CHECK(retail());
    /* a value the game set while no variant held the switch (the debug
       menu, the option file) is the one put back */
    s_battle = 1;
    ico_variant_set(ICO_VARIANT_AGGRESSION, ICO_AGGRESSION_BOLD_QUICK);
    CHECK(s_battle == 2);
    ico_variant_set(ICO_VARIANT_AGGRESSION, ICO_AGGRESSION_OWN);
    CHECK(s_battle == 1);
    /* a file with a variant on: written at the bind */
    use_config("[variants]\none_hit = true\naggression = \"wary_patient\"\n");
    bind_retail();
    CHECK(s_one_hit == 1 && s_battle == 1 && s_dodge == 0 && s_hand == 1);
    /* the Debug menu through the bound opener */
    s_opens = 0;
    CHECK(ico_variants_suspend_achievements() == 1); /* one_hit from the file */
    use_config("[variants]\n");
    bind_retail();
    CHECK(ico_variants_suspend_achievements() == 0);
    s_opens = 0;
    CHECK(ico_variants_debug_menu_pressed() == 1 && s_opens == 1);
    /* the open marks it used: achievements off until cleared or reloaded */
    CHECK(ico_variants_suspend_achievements() == 1);
    ico_variants_debug_menu_clear();
    CHECK(ico_variants_suspend_achievements() == 0);
    ico_variants_debug_menu_pressed();
    CHECK(ico_variants_suspend_achievements() == 1);
    ico_opt_reload();
    CHECK(ico_variants_suspend_achievements() == 0);
    CHECK(ico_variants_open_debug_menu() == 1 && s_opens == 3);
    /* unbound: nothing written, no opener */
    use_config(NULL);
    ico_variants_bind_debug(NULL, NULL, NULL, NULL, NULL, NULL);
    s_one_hit = 5;
    ico_variant_set(ICO_VARIANT_ONE_HIT, 1);
    CHECK(s_one_hit == 5);
    CHECK(ico_variants_open_debug_menu() == 0 && s_opens == 3);
    use_config(NULL);
}

/* the hints: the disc's hint system and No hints */
static void test_hints(void)
{
    const char *slug = ico_region_cur()->slug;
    const int has =
        strcmp(slug, "ntsc") != 0 && strcmp(slug, "aug6") != 0 && strcmp(slug, "jun26") != 0;

    use_config(NULL);
    CHECK(ico_hints_on() == has);
    ico_variant_set(ICO_VARIANT_NO_HINTS, 1);
    CHECK(ico_hints_on() == 0);
    ico_variant_set(ICO_VARIANT_NO_HINTS, 0);
    CHECK(ico_hints_on() == has);
}

/* the enemy rows */
static void test_enemy_def(void)
{
    const char *slug = ico_region_cur()->slug;
    const int rows = enemyKindRows;
    const EnemyDef *disc = enemyKind;
    const EnemyDef *r;

    use_config(NULL);
    /* the running disc's table: 18 rows on the June 2001 demo, 45 else */
    CHECK(rows == (strcmp(slug, "jun26") == 0 ? 18 : 45));
    CHECK(enemyKindRows_ntsc == 45 && enemyKindRows_jun26 == 18);
    /* nothing chosen: the disc's own rows, the same objects */
    for (int k = 0; k < rows; k++) {
        CHECK(ico_variant_enemy_def(disc, rows, enemyKind_ntsc, enemyKindRows_ntsc, k) == &disc[k]);
    }
    /* never past the table: the last row */
    CHECK(ico_variant_enemy_def(enemyKind_jun26, enemyKindRows_jun26, enemyKind_ntsc,
                                enemyKindRows_ntsc, 18) == &enemyKind_jun26[17]);
    CHECK(ico_variant_enemy_def(enemyKind_jun26, enemyKindRows_jun26, enemyKind_ntsc,
                                enemyKindRows_ntsc, 44) == &enemyKind_jun26[17]);
    CHECK(ico_variant_enemy_def(enemyKind_pal, enemyKindRows_pal, enemyKind_ntsc,
                                enemyKindRows_ntsc, -1) == &enemyKind_pal[44]);
    CHECK(enemyKind_jun26[17].model == 1425 && enemyKind_jun26[0].model == 12);
    /* American on the European table: the USA life, scale and fly and battle
       types, the European model, dodge and parameter row (not on the June 2001
       demo, where American reads as Original: the disc's rows) */
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_AMERICAN);
    if (on_jun26()) {
        CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_ORIGINAL);
        for (int k = 0; k < enemyKindRows_pal; k++) {
            CHECK(ico_variant_enemy_def(enemyKind_pal, enemyKindRows_pal, enemyKind_ntsc,
                                        enemyKindRows_ntsc, k) == &enemyKind_pal[k]);
        }
        for (int k = 0; k < enemyKindRows_jun26; k++) {
            CHECK(ico_variant_enemy_def(enemyKind_jun26, enemyKindRows_jun26, enemyKind_ntsc,
                                        enemyKindRows_ntsc, k) == &enemyKind_jun26[k]);
        }
        ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_NO_PLUS);
        CHECK(ico_variant_enemy_def(enemyKind_jun26, enemyKindRows_jun26, enemyKind_ntsc,
                                    enemyKindRows_ntsc, 1) == &enemyKind_jun26[1]);
    } else {
        r = ico_variant_enemy_def(enemyKind_pal, enemyKindRows_pal, enemyKind_ntsc,
                                  enemyKindRows_ntsc, 1);
        CHECK(r != &enemyKind_pal[1]);
        CHECK(r->life == 400.0f && enemyKind_pal[1].life == 200.0f);
        CHECK(r->dodge == 350.0f && r->paraIndex == 1 && r->model == 22 && r->particle == -1);
        CHECK(r->float0C == enemyKind_pal[1].float0C);
        r = ico_variant_enemy_def(enemyKind_pal, enemyKindRows_pal, enemyKind_ntsc,
                                  enemyKindRows_ntsc, 2);
        CHECK(r->scale == 0.9f && r->life == 400.0f && r->dodge == 350.0f);
        r = ico_variant_enemy_def(enemyKind_pal, enemyKindRows_pal, enemyKind_ntsc,
                                  enemyKindRows_ntsc, 5);
        CHECK(r->life == 400.0f && r->flyType == enemyKind_ntsc[5].flyType &&
              r->battleType == enemyKind_ntsc[5].battleType && r->dodge == 400.0f);
        /* a row of the disc's "no model" stays whole */
        CHECK(ico_variant_enemy_def(enemyKind_pal, enemyKindRows_pal, enemyKind_ntsc,
                                    enemyKindRows_ntsc, 17) == &enemyKind_pal[17]);
        CHECK(ico_variant_enemy_def(enemyKind_jun26, enemyKindRows_jun26, enemyKind_ntsc,
                                    enemyKindRows_ntsc, 14) == &enemyKind_jun26[14]);
        /* the June 2001 demo's 18 rows are kinds of their own (not the USA
           disc's first 18): never mapped by index, every row the demo's own */
        for (int k = 0; k < enemyKindRows_jun26; k++) {
            CHECK(ico_variant_enemy_def(enemyKind_jun26, enemyKindRows_jun26, enemyKind_ntsc,
                                        enemyKindRows_ntsc, k) == &enemyKind_jun26[k]);
        }
        CHECK(enemyKind_jun26[1].model == 13);
    }
    /* Not tougher later: the disc's rows */
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_NO_PLUS);
    CHECK(ico_variant_enemy_def(enemyKind_pal, enemyKindRows_pal, enemyKind_ntsc,
                                enemyKindRows_ntsc, 1) == &enemyKind_pal[1]);
    /* aggression: every row's battle type, the rest the disc's */
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_ORIGINAL);
    ico_variant_set(ICO_VARIANT_AGGRESSION, ICO_AGGRESSION_BOLD_PATIENT);
    for (int k = 0; k < enemyKindRows_pal; k++) {
        r = ico_variant_enemy_def(enemyKind_pal, enemyKindRows_pal, enemyKind_ntsc,
                                  enemyKindRows_ntsc, k);
        CHECK(r->battleType == 0 && r->life == enemyKind_pal[k].life &&
              r->model == enemyKind_pal[k].model && r->flyType == enemyKind_pal[k].flyType);
    }
    ico_variant_set(ICO_VARIANT_AGGRESSION, ICO_AGGRESSION_WARY_QUICK);
    r = ico_variant_enemy_def(enemyKind_pal, enemyKindRows_pal, enemyKind_ntsc, enemyKindRows_ntsc,
                              19);
    CHECK(r->battleType == 3 && enemyKind_pal[19].battleType == 1);
    ico_variant_set(ICO_VARIANT_AGGRESSION, ICO_AGGRESSION_OWN);
    CHECK(ico_variant_enemy_def(enemyKind_pal, enemyKindRows_pal, enemyKind_ntsc,
                                enemyKindRows_ntsc, 19) == &enemyKind_pal[19]);
    use_config(NULL);
}

/* enemy_act.c getEnemyRestartLife's gameParam index, copied: 38 plus the
   playthroughs cleared, 38..43; 38 on every playthrough with Shadow toughness
   American or Not tougher later */
static int restart_index(int clears)
{
    int max = 43;
    int idx = clears + 38;

    if (ico_variant_shade_toughness() != 0) {
        idx = 38;
    }
    if (ico_variant_shade_toughness() == 3) {
        idx = 40; /* Third journey */
    }
    idx = (idx < 38) ? 38 : ((idx <= max) ? idx : max);
    return idx;
}

static void test_restart_life(void)
{
    use_config(NULL);
    for (int c = 0; c < 8; c++) {
        CHECK(restart_index(c) == (c < 5 ? 38 + c : 43));
    }
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_NO_PLUS);
    for (int c = 0; c < 8; c++) {
        CHECK(restart_index(c) == 38);
    }
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_AMERICAN);
    /* (Original on the June 2001 demo: the factor of the playthrough) */
    CHECK(restart_index(3) == (on_jun26() ? 41 : 38));
    /* Third journey: the table's entry 40 on every playthrough, on the discs
       that offer it; Original (the playthrough's own) where they do not */
    use_config("[variants]\nshade_toughness = \"third_journey\"\n");
    for (int c = 0; c < 8; c++) {
        CHECK(restart_index(c) == (third_offered() ? 40 : (c < 5 ? 38 + c : 43)));
    }
    use_config(NULL);
}

/* Peaceful shadows: the hook the four Shadow starts read (enemy_act.c, beside
   debug_brain_flag) is 1 by default, 0 with the key on; stepping writes the key;
   it is a cheat (achievements off); Yorda's own brain thread (girl_act.c) reads
   no variant, so nothing here touches it */
static void test_peaceful(void)
{
    char path[64] = "variants.peaceful_shadows";

    use_config(NULL);
    CHECK(ico_variant_shadow_brain() == 1 && ico_variants_suspend_achievements() == 0);
    use_config("[variants]\npeaceful_shadows = true\n");
    CHECK(ico_variant_shadow_brain() == 0 && ico_variants_suspend_achievements() == 1);
    use_config("[variants]\npeaceful_shadows = \"sideways\"\n");
    CHECK(ico_variant_shadow_brain() == 1);
    use_config(NULL);
    CHECK(ico_variant_step(ICO_VARIANT_PEACEFUL_SHADOWS, 1) == 1);
    CHECK(ico_variant_shadow_brain() == 0 && ico_config_get_bool(path, 0) == 1 &&
          ico_variants_suspend_achievements() == 1);
    CHECK(ico_variant_step(ICO_VARIANT_PEACEFUL_SHADOWS, 1) == 0);
    CHECK(ico_variant_shadow_brain() == 1 && ico_config_get_bool(path, 1) == 0 &&
          ico_variants_suspend_achievements() == 0);
    /* the other Shadow rows are untouched by it */
    ico_variant_set(ICO_VARIANT_PEACEFUL_SHADOWS, 1);
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_ORIGINAL &&
          ico_variant_aggression() == ICO_AGGRESSION_OWN && ico_variant_auto_guard() == 1);
    ico_opt_reload();
    CHECK(ico_variant_shadow_brain() == 1);
    use_config(NULL);
}

/* Shadow toughness American is offered on every disc but the June 2001 demo
   (its enemy rows are its own): there a config "american" reads as Original,
   a set of it gives Original, and the row steps Original <-> Not tougher
   later; every other value of every variant is offered everywhere */
static void test_offered(void)
{
    const int demo = on_jun26();

    CHECK(ico_variant_offered(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_AMERICAN) == !demo);
    CHECK(ico_variant_offered(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_ORIGINAL) == 1);
    CHECK(ico_variant_offered(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_NO_PLUS) == 1);
    CHECK(ico_variant_offered(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_THIRD_JOURNEY) ==
          third_offered());
    CHECK(ico_variant_offered(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_COUNT) == 0);
    CHECK(ico_variant_offered(ICO_VARIANT_SHADE_TOUGHNESS, -1) == 0);
    /* Red only where the light sword's kinds 8 and 9 exist */
    CHECK(ico_variant_offered(ICO_VARIANT_SWORD_COLOUR, ICO_SWORD_ORIGINAL) == 1);
    CHECK(ico_variant_offered(ICO_VARIANT_SWORD_COLOUR, ICO_SWORD_RED) == !y2001());
    for (int id = 1; id < ICO_VARIANT_COUNT; id++) {
        if (id == ICO_VARIANT_SWORD_COLOUR) {
            continue;
        }
        for (int v = 0; v < ico_variant_count((IcoVariantId)id); v++) {
            CHECK(ico_variant_offered((IcoVariantId)id, v) == 1);
        }
    }
    use_config(NULL);
    {
        /* the row steps through the offered values only, around both ways */
        int offs[ICO_TOUGHNESS_COUNT], n = 0;

        for (int v = 0; v < ICO_TOUGHNESS_COUNT; v++) {
            if (ico_variant_offered(ICO_VARIANT_SHADE_TOUGHNESS, v)) {
                offs[n++] = v;
            }
        }
        CHECK(n == 2 + !demo + third_offered());
        for (int k = 1; k <= n; k++) {
            CHECK(ico_variant_step(ICO_VARIANT_SHADE_TOUGHNESS, 1) == offs[k % n]);
            if (k == 1) {
                CHECK(strcmp(ico_config_get_string("variants.shade_toughness", ""),
                             ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, offs[1])) == 0);
            }
        }
        for (int k = n - 1; k >= 0; k--) {
            CHECK(ico_variant_step(ICO_VARIANT_SHADE_TOUGHNESS, -1) == offs[k]);
        }
        /* Third journey: its key when offered, Original (and a log line) when not */
        ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_THIRD_JOURNEY);
        CHECK(ico_variant_shade_toughness() ==
              (third_offered() ? ICO_TOUGHNESS_THIRD_JOURNEY : ICO_TOUGHNESS_ORIGINAL));
        use_config("[variants]\nshade_toughness = \"third_journey\"\n");
        CHECK(ico_variant_shade_toughness() ==
              (third_offered() ? ICO_TOUGHNESS_THIRD_JOURNEY : ICO_TOUGHNESS_ORIGINAL));
        use_config(NULL);
    }
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, ICO_TOUGHNESS_AMERICAN);
    CHECK(ico_variant_shade_toughness() ==
          (demo ? ICO_TOUGHNESS_ORIGINAL : ICO_TOUGHNESS_AMERICAN));
    use_config("[variants]\nshade_toughness = \"american\"\n");
    CHECK(ico_variant_shade_toughness() ==
          (demo ? ICO_TOUGHNESS_ORIGINAL : ICO_TOUGHNESS_AMERICAN));
    use_config(NULL);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";

    if (argc > 2 && ico_region_select(argv[2]) != 0) {
        fprintf(stderr, "variants_test: no region %s\n", argv[2]);
        return 1;
    }
    /* a file per disc: ctest runs the seven discs' tests in parallel */
    snprintf(s_path, sizeof(s_path), "%s/variants_test_%s.toml", dir, ico_region_cur()->slug);
    test_defaults();
    test_names();
    test_round_trip();
    test_file_words();
    test_combat();
    test_weapons();
    test_suspend();
    test_apply();
    test_hints();
    test_enemy_def();
    test_restart_life();
    test_offered();
    test_peaceful();
    remove(s_path);
    if (failures != 0) {
        fprintf(stderr, "variants_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("variants_test: ok\n");
    return 0;
}
