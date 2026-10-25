/*
 * port/game/test/variants_test.c
 *
 * The game variants (port/game/variants.h) on the CPU: every default is the
 * game as shipped, every key round trips through config.toml, a word or a
 * number that is not a value reads as the default, and the achievements rule
 * (One hit per swing, Shades never dodge, Cutscene cameras ignored and the
 * Debug menu switch them off; the other four do not).
 *
 *   variants_test DIR [REGION]
 */
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "options.h"
#include "../../core/region.h"
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
    CHECK(ico_variants_suspend_achievements() == 0);
    /* the shipped file's values are the defaults */
    use_config("[variants]\nshade_toughness = \"original\"\none_hit = false\n"
               "never_dodge = false\naggression = \"own\"\nno_hints = false\n"
               "no_cutscene_look = false\nignore_cutscene_cameras = false\n");
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_ORIGINAL);
    CHECK(ico_variant_aggression() == ICO_AGGRESSION_OWN);
    CHECK(ico_variants_suspend_achievements() == 0);
}

/* the names: eight variants, the words the keys are written with */
static void test_names(void)
{
    CHECK(ico_variant_count(ICO_VARIANT_SHADE_TOUGHNESS) == 3);
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
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, 0), "original") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, 1), "american") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, 2), "no_plus") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_SHADE_TOUGHNESS, 3), "") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AGGRESSION, 0), "own") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AGGRESSION, 1), "bold_patient") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AGGRESSION, 2), "bold_quick") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AGGRESSION, 3), "wary_patient") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_AGGRESSION, 4), "wary_quick") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_BARRIER_HITS, 0), "5") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_BARRIER_HITS, 1), "7") == 0);
    CHECK(strcmp(ico_variant_value_name(ICO_VARIANT_NO_HINTS, 1), "true") == 0);
}

/* every key: step through every value, save, reload, the same value back;
   the file names the key under [variants] */
static void test_round_trip(void)
{
    for (int id = 0; id < ICO_VARIANT_COUNT; id++) {
        const int n = ico_variant_count((IcoVariantId)id);

        for (int want = 0; want < n; want++) {
            int v;

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
    /* a clamped set, a wrapping step in both directions */
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, 9);
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_NO_PLUS);
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, -4);
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_ORIGINAL);
    CHECK(ico_variant_step(ICO_VARIANT_SHADE_TOUGHNESS, -1) == ICO_TOUGHNESS_NO_PLUS);
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
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_AMERICAN);
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

/* the achievements rule */
static void test_suspend(void)
{
    static const IcoVariantId cheats[] = {ICO_VARIANT_ONE_HIT, ICO_VARIANT_NEVER_DODGE,
                                          ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS};
    static const IcoVariantId harmless[] = {ICO_VARIANT_NO_HINTS, ICO_VARIANT_NO_CUTSCENE_LOOK,
                                            ICO_VARIANT_BARRIER_HITS};

    for (int i = 0; i < 3; i++) {
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
    /* the Debug menu: the press marks it used until cleared or reloaded */
    use_config(NULL);
    CHECK(ico_variants_suspend_achievements() == 0);
    ico_variants_debug_menu_pressed(); /* the stub hook logs "not wired yet" */
    CHECK(ico_variants_suspend_achievements() == 1);
    ico_variants_debug_menu_clear();
    CHECK(ico_variants_suspend_achievements() == 0);
    ico_variants_debug_menu_pressed();
    ico_opt_reload();
    CHECK(ico_variants_suspend_achievements() == 0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";

    if (argc > 2 && ico_region_select(argv[2]) != 0) {
        fprintf(stderr, "variants_test: no region %s\n", argv[2]);
        return 1;
    }
    snprintf(s_path, sizeof(s_path), "%s/variants_test.toml", dir);
    test_defaults();
    test_names();
    test_round_trip();
    test_file_words();
    test_suspend();
    remove(s_path);
    if (failures != 0) {
        fprintf(stderr, "variants_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("variants_test: ok\n");
    return 0;
}
