/*
 * port/game/variants.c
 *
 * The game variants' values (variants.h): read lazily from [variants],
 * set by the Settings page, the achievements rule.
 */
#include "variants.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "region.h" /* port/core: the disc's own barrier */

static const char *const kToughness[ICO_TOUGHNESS_COUNT] = {"original", "american", "no_plus"};
static const char *const kAggression[ICO_AGGRESSION_COUNT] = {"own", "bold_patient", "bold_quick",
                                                              "wary_patient", "wary_quick"};
static const char *const kBarrier[2] = {"5", "7"};
static const char *const kBool[2] = {"false", "true"};

static const char *const kKeys[ICO_VARIANT_COUNT] = {
    "shade_toughness", "one_hit",      "never_dodge",      "aggression",
    "no_hints",        "barrier_hits", "no_cutscene_look", "ignore_cutscene_cameras"};

/* 0: not read yet */
static int s_loaded;
static int s_value[ICO_VARIANT_COUNT];
static int s_debug_menu_used;
static int s_suspend_logged; /* the achievements rule's last mask */
static int s_stub_logged;

int ico_variant_count(IcoVariantId id)
{
    switch (id) {
    case ICO_VARIANT_SHADE_TOUGHNESS:
        return ICO_TOUGHNESS_COUNT;
    case ICO_VARIANT_AGGRESSION:
        return ICO_AGGRESSION_COUNT;
    case ICO_VARIANT_ONE_HIT:
    case ICO_VARIANT_NEVER_DODGE:
    case ICO_VARIANT_NO_HINTS:
    case ICO_VARIANT_BARRIER_HITS:
    case ICO_VARIANT_NO_CUTSCENE_LOOK:
    case ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS:
        return 2;
    default:
        return 0;
    }
}

const char *ico_variant_key(IcoVariantId id)
{
    return id >= 0 && id < ICO_VARIANT_COUNT ? kKeys[id] : "";
}

const char *ico_variant_value_name(IcoVariantId id, int index)
{
    if (index < 0 || index >= ico_variant_count(id)) {
        return "";
    }
    switch (id) {
    case ICO_VARIANT_SHADE_TOUGHNESS:
        return kToughness[index];
    case ICO_VARIANT_AGGRESSION:
        return kAggression[index];
    case ICO_VARIANT_BARRIER_HITS:
        return kBarrier[index];
    default:
        return kBool[index];
    }
}

/* the barrier the disc ships: 7 hits on the June 2001 demo disc, else 5
   (ico2/ito/src/queen.c, PAL_JUN26(5, 7)) */
static int shipped_barrier(void)
{
    const IcoRegionInfo *r = ico_region_cur();
    return r != NULL && r->slug != NULL && strcmp(r->slug, "jun26") == 0 ? 1 : 0;
}

static int word_index(const char *const *names, int n, const char *word, int def, const char *key)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(word, names[i]) == 0) {
            return i;
        }
    }
    fprintf(stderr, "variants: [variants] %s = \"%s\" is not a value; using %s\n", key, word,
            names[def]);
    return def;
}

static void log_summary(void)
{
    fprintf(stderr,
            "variants: shade_toughness=%s one_hit=%d never_dodge=%d aggression=%s no_hints=%d "
            "barrier_hits=%s no_cutscene_look=%d ignore_cutscene_cameras=%d\n",
            kToughness[s_value[ICO_VARIANT_SHADE_TOUGHNESS]], s_value[ICO_VARIANT_ONE_HIT],
            s_value[ICO_VARIANT_NEVER_DODGE], kAggression[s_value[ICO_VARIANT_AGGRESSION]],
            s_value[ICO_VARIANT_NO_HINTS], kBarrier[s_value[ICO_VARIANT_BARRIER_HITS]],
            s_value[ICO_VARIANT_NO_CUTSCENE_LOOK], s_value[ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS]);
}

static void load(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = 1;
    s_value[ICO_VARIANT_SHADE_TOUGHNESS] = word_index(
        kToughness, ICO_TOUGHNESS_COUNT,
        ico_config_get_string("variants.shade_toughness", kToughness[0]), 0, "shade_toughness");
    s_value[ICO_VARIANT_ONE_HIT] = ico_config_get_bool("variants.one_hit", 0) != 0;
    s_value[ICO_VARIANT_NEVER_DODGE] = ico_config_get_bool("variants.never_dodge", 0) != 0;
    s_value[ICO_VARIANT_AGGRESSION] =
        word_index(kAggression, ICO_AGGRESSION_COUNT,
                   ico_config_get_string("variants.aggression", kAggression[0]), 0, "aggression");
    s_value[ICO_VARIANT_NO_HINTS] = ico_config_get_bool("variants.no_hints", 0) != 0;
    {
        const long long b = ico_config_get_int("variants.barrier_hits", 0);
        const int named = b == 5 || b == 7;
        if (!named && b != 0) {
            fprintf(stderr,
                    "variants: [variants] barrier_hits = %lld is not 5 or 7; using the "
                    "disc's\n",
                    b);
        }
        s_value[ICO_VARIANT_BARRIER_HITS] = named ? (b == 7) : shipped_barrier();
    }
    s_value[ICO_VARIANT_NO_CUTSCENE_LOOK] =
        ico_config_get_bool("variants.no_cutscene_look", 0) != 0;
    s_value[ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS] =
        ico_config_get_bool("variants.ignore_cutscene_cameras", 0) != 0;
    log_summary();
}

int ico_variant_get(IcoVariantId id)
{
    if (id < 0 || id >= ICO_VARIANT_COUNT) {
        return 0;
    }
    load();
    return s_value[id];
}

void ico_variant_set(IcoVariantId id, int index)
{
    const int n = ico_variant_count(id);
    if (n == 0) {
        return;
    }
    load();
    s_value[id] = index < 0 ? 0 : index >= n ? n - 1 : index;
}

int ico_variant_step(IcoVariantId id, int dir)
{
    const int n = ico_variant_count(id);
    char path[64];
    int v;

    if (n == 0) {
        return 0;
    }
    load();
    v = ((s_value[id] + dir) % n + n) % n;
    ico_variant_set(id, v);
    snprintf(path, sizeof(path), "variants.%s", kKeys[id]);
    if (id == ICO_VARIANT_BARRIER_HITS) {
        ico_config_set_int(path, v == 1 ? 7 : 5);
    } else if (id == ICO_VARIANT_SHADE_TOUGHNESS || id == ICO_VARIANT_AGGRESSION) {
        ico_config_set_string(path, ico_variant_value_name(id, v));
    } else {
        ico_config_set_bool(path, v);
    }
    fprintf(stderr, "variants: %s = %s\n", kKeys[id], ico_variant_value_name(id, v));
    return v;
}

int ico_variant_shade_toughness(void)
{
    return ico_variant_get(ICO_VARIANT_SHADE_TOUGHNESS);
}

int ico_variant_one_hit(void)
{
    return ico_variant_get(ICO_VARIANT_ONE_HIT);
}

int ico_variant_never_dodge(void)
{
    return ico_variant_get(ICO_VARIANT_NEVER_DODGE);
}

int ico_variant_aggression(void)
{
    return ico_variant_get(ICO_VARIANT_AGGRESSION);
}

int ico_variant_no_hints(void)
{
    return ico_variant_get(ICO_VARIANT_NO_HINTS);
}

int ico_variant_barrier_hits(void)
{
    return ico_variant_get(ICO_VARIANT_BARRIER_HITS) == 1 ? 7 : 5;
}

int ico_variant_no_cutscene_look(void)
{
    return ico_variant_get(ICO_VARIANT_NO_CUTSCENE_LOOK);
}

int ico_variant_ignore_cutscene_cameras(void)
{
    return ico_variant_get(ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS);
}

int ico_variants_suspend_achievements(void)
{
    int mask;
    load();
    mask = (s_value[ICO_VARIANT_ONE_HIT] ? 1 : 0) | (s_value[ICO_VARIANT_NEVER_DODGE] ? 2 : 0) |
           (s_value[ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS] ? 4 : 0) | (s_debug_menu_used ? 8 : 0);
    if (mask != s_suspend_logged) {
        s_suspend_logged = mask;
        if (mask != 0) {
            fprintf(stderr, "variants: achievements are off (%s%s%s%s)\n",
                    mask & 1 ? "one_hit " : "", mask & 2 ? "never_dodge " : "",
                    mask & 4 ? "ignore_cutscene_cameras " : "", mask & 8 ? "debug_menu" : "");
        } else {
            fprintf(stderr, "variants: achievements are back on\n");
        }
    }
    return mask != 0;
}

void ico_variants_debug_menu_pressed(void)
{
    s_debug_menu_used = 1;
    fprintf(stderr, "variants: the Debug menu is opened from Options\n");
    ico_variants_open_debug_menu();
}

void ico_variants_debug_menu_clear(void)
{
    s_debug_menu_used = 0;
}

/* the hook's stub until the game hooks are wired (variants.h) */
void ico_variants_open_debug_menu(void)
{
    if (!s_stub_logged) {
        s_stub_logged = 1;
        fprintf(stderr, "variants: the debug menu hook is not wired yet\n");
    }
}

void ico_variants_reload(void)
{
    s_loaded = 0;
    s_debug_menu_used = 0;
    s_suspend_logged = 0;
}
