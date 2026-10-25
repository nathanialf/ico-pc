/*
 * port/game/variants.c
 *
 * The game variants' values (variants.h): read lazily from [variants],
 * set by the Settings page, the achievements rule; the game's debug switches
 * they hold (ico_variants_apply), the enemy row reader (ico_variant_enemy_def)
 * and the hint rule (ico_hints_on).
 *
 * Nothing here names a game symbol: the game hands over what this file writes
 * or reads (debug.c's ico_variants_bind_debug, enemy.c's tables), so the
 * library links into the tests without the game, and the program's link
 * order (the game library first) needs no game symbol from here.
 */
#include "region.h" /* port/core (and the game's ico2/common/include/region.h) */
/* the game's enemy record, EnemyDef (no symbol of the game is used) */
#include "../../ico2/sugipon/include/enemy.h"

#include "variants.h"

#include <stdio.h>
#include <string.h>

#include "config.h"

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

/* the game's debug switches the variants hold (ico_variants_bind_debug):
   where each is, the game's own value under a variant's, and whether a
   variant's value is in it now */
enum {
    DBG_ONE_HIT,            /* debug_one_hit_only */
    DBG_IGNORE_DODGE,       /* debug_ignore_dodge */
    DBG_BATTLE_TYPE,        /* debug_enemy_battle_type */
    DBG_IGNORE_DEMO_CAMERA, /* debug_ignore_demo_camera */
    DBG_HAND_CAMERA,        /* debug_hand_camera */
    DBG_COUNT
};

static const char *const kDbgNames[DBG_COUNT] = {
    "one_hit_only", "ignore_dodge", "enemy_battle_type", "ignore_demo_camera", "hand_camera"};
static int *s_dbg[DBG_COUNT];
static int s_dbg_base[DBG_COUNT];
static int s_dbg_held[DBG_COUNT];
static void (*s_menu_open)(void);

/* debug_enemy_battle_type by Shadow aggression, as debug.c names the values
   (debugEnemyBattleName: 0 Positive-Low, 1 Negative-Low, 2 Positive-High,
   3 Negative-High): bold is Positive, wary Negative, patient Low, quick
   High; -1: the Shadow's own (the switch keeps the game's value) */
static const int kBattleType[ICO_AGGRESSION_COUNT] = {-1, 0, 2, 1, 3};

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
   (ico2/ito/src/queen.c, QueenBarrierGeo and its June 2001 twin) */
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
    ico_variants_apply();
}

/* the value a variant holds switch i at, or -1: the game's own */
static int dbg_wanted(int i)
{
    switch (i) {
    case DBG_ONE_HIT:
        return s_value[ICO_VARIANT_ONE_HIT] ? 1 : -1;
    case DBG_IGNORE_DODGE:
        return s_value[ICO_VARIANT_NEVER_DODGE] ? 1 : -1;
    case DBG_BATTLE_TYPE:
        return kBattleType[s_value[ICO_VARIANT_AGGRESSION]];
    case DBG_IGNORE_DEMO_CAMERA:
        return s_value[ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS] ? 1 : -1;
    case DBG_HAND_CAMERA:
        /* the European cutscene look-around (camera-root.c's stick block
           under debug_hand_camera, which the USA disc never sets) */
        return s_value[ICO_VARIANT_NO_CUTSCENE_LOOK] ? 0 : -1;
    default:
        return -1;
    }
}

void ico_variants_apply(void)
{
    char sw[160];
    int at = 0;

    load();
    for (int i = 0; i < DBG_COUNT; i++) {
        const int w = dbg_wanted(i);
        if (s_dbg[i] == NULL) {
            continue;
        }
        if (w >= 0) {
            if (!s_dbg_held[i]) {
                s_dbg_base[i] = *s_dbg[i]; /* the game's value, back when the variant is off */
                s_dbg_held[i] = 1;
            }
            *s_dbg[i] = w;
        } else if (s_dbg_held[i]) {
            *s_dbg[i] = s_dbg_base[i];
            s_dbg_held[i] = 0;
        }
    }
    for (int i = 0; i < DBG_COUNT && at < (int)sizeof(sw); i++) {
        if (s_dbg[i] == NULL) {
            at += snprintf(sw + at, sizeof(sw) - (size_t)at, " %s=unbound", kDbgNames[i]);
        } else {
            at += snprintf(sw + at, sizeof(sw) - (size_t)at, " %s=%d%s", kDbgNames[i], *s_dbg[i],
                           s_dbg_held[i] ? "" : "(game's)");
        }
    }
    fprintf(stderr,
            "variants: applied shade_toughness=%s one_hit=%d never_dodge=%d aggression=%s "
            "no_hints=%d (hints %s) barrier_hits=%d no_cutscene_look=%d "
            "ignore_cutscene_cameras=%d;%s\n",
            kToughness[s_value[ICO_VARIANT_SHADE_TOUGHNESS]], s_value[ICO_VARIANT_ONE_HIT],
            s_value[ICO_VARIANT_NEVER_DODGE], kAggression[s_value[ICO_VARIANT_AGGRESSION]],
            s_value[ICO_VARIANT_NO_HINTS], ico_hints_on() ? "on" : "off",
            ico_variant_barrier_hits(), s_value[ICO_VARIANT_NO_CUTSCENE_LOOK],
            s_value[ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS], sw);
}

void ico_variants_bind_debug(int *one_hit, int *ignore_dodge, int *battle_type,
                             int *ignore_demo_camera, int *hand_camera, void (*menu_open)(void))
{
    int *const p[DBG_COUNT] = {one_hit, ignore_dodge, battle_type, ignore_demo_camera, hand_camera};
    for (int i = 0; i < DBG_COUNT; i++) {
        s_dbg[i] = p[i];
        s_dbg_base[i] = p[i] != NULL ? *p[i] : 0;
        s_dbg_held[i] = 0;
    }
    s_menu_open = menu_open;
    if (one_hit != NULL) {
        fprintf(stderr,
                "variants: the game's debug switches are bound (one_hit_only=%d ignore_dodge=%d "
                "enemy_battle_type=%d ignore_demo_camera=%d hand_camera=%d, debug menu %s)\n",
                s_dbg_base[DBG_ONE_HIT], s_dbg_base[DBG_IGNORE_DODGE], s_dbg_base[DBG_BATTLE_TYPE],
                s_dbg_base[DBG_IGNORE_DEMO_CAMERA], s_dbg_base[DBG_HAND_CAMERA],
                menu_open != NULL ? "yes" : "no");
    }
    ico_variants_apply();
}

int ico_hints_on(void)
{
    /* the USA disc and the two 2001 builds have no hint system */
    return !ICO_NTSCAUG6JUN26 && !ico_variant_get(ICO_VARIANT_NO_HINTS);
}

/* the rows a changed row is built in (the largest table has 45) */
#define ENEMY_ROWS_MAX 64
static EnemyDef s_enemy_rows[ENEMY_ROWS_MAX];
static int s_enemy_past_logged;

const EnemyDef *ico_variant_enemy_def(const EnemyDef *disc, int rows, const EnemyDef *usa,
                                      int usaRows, int kind)
{
    const EnemyDef *row;
    int american;
    int battle;

    if (disc == NULL || rows <= 0) {
        return disc;
    }
    if (kind < 0 || kind >= rows) {
        if (!s_enemy_past_logged) {
            s_enemy_past_logged = 1;
            fprintf(stderr,
                    "variants: enemy kind %d is past this disc's %d enemy rows; its last row "
                    "stands in\n",
                    kind, rows);
        }
        kind = rows - 1;
    }
    row = &disc[kind];
    load();
    /* American: the USA disc's row of the same kind, but a row of the disc's
       "no model" (its table's last row's model) keeps the disc's */
    american = s_value[ICO_VARIANT_SHADE_TOUGHNESS] == ICO_TOUGHNESS_AMERICAN && usa != NULL &&
               kind < usaRows && row->model != disc[rows - 1].model;
    battle = kBattleType[s_value[ICO_VARIANT_AGGRESSION]];
    if ((!american && battle < 0) || kind >= ENEMY_ROWS_MAX) {
        return row; /* the disc's own row, as the game read it */
    }
    s_enemy_rows[kind] = *row;
    if (american) {
        /* the USA life, scale and attribute bits; the disc's model, particle,
           fly acceleration, dodge range and parameter row */
        s_enemy_rows[kind].life = usa[kind].life;
        s_enemy_rows[kind].scale = usa[kind].scale;
        s_enemy_rows[kind].flyType = usa[kind].flyType;
        s_enemy_rows[kind].battleType = usa[kind].battleType;
    }
    if (battle >= 0) {
        s_enemy_rows[kind].battleType = (unsigned int)battle;
    }
    return &s_enemy_rows[kind];
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

int ico_variants_debug_menu_pressed(void)
{
    s_debug_menu_used = 1;
    fprintf(stderr, "variants: the Debug menu is opened from Options\n");
    return ico_variants_open_debug_menu();
}

void ico_variants_debug_menu_clear(void)
{
    s_debug_menu_used = 0;
}

int ico_variants_open_debug_menu(void)
{
    if (s_menu_open == NULL) {
        fprintf(stderr, "variants: the Debug menu cannot open: the game has not bound it\n");
        return 0;
    }
    s_menu_open(); /* debug.c debug_MenuOpen: as SELECT opens it */
    return 1;
}

void ico_variants_reload(void)
{
    s_loaded = 0;
    s_debug_menu_used = 0;
    s_suspend_logged = 0;
    ico_variants_apply();
}
