/*
 * port/game/variants.c
 *
 * The game variants' values (variants.h): the global ones read lazily from
 * [variants], the ten that are rows of the journey's difficulty record
 * (difficulty.h) read from the active record, set by the Settings page, the
 * achievements rule; the game's debug switches
 * they hold (ico_variants_apply), the enemy row reader (ico_variant_enemy_def),
 * the hint rule (ico_hints_on) and the weapon kinds (ico_variant_weapon_kind).
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
#include "difficulty.h"

static const char *const kToughness[ICO_TOUGHNESS_COUNT] = {"original", "american", "no_plus",
                                                            "third_journey"};
static const char *const kAggression[ICO_AGGRESSION_COUNT] = {"own", "bold_patient", "bold_quick",
                                                              "wary_patient", "wary_quick"};
static const char *const kBarrier[2] = {"5", "7"};
static const char *const kBool[2] = {"false", "true"};
static const char *const kSword[ICO_SWORD_COUNT] = {"original", "red"};
static const char *const kWeapon[ICO_WEAPON_COUNT] = {"original", "net"};
/* the chain rows: the game's debug values 1..10 (debug.c's CHAIN CYCLE SPEED
   and CHAIN SLOW SPEED rows run 1 to 10), index = value - 1 */
#define CHAIN_VALUES 10
#define CHAIN_SHIPPED 4 /* debug_VariableInit's value for both */
static const char *const kChain[CHAIN_VALUES] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};

static const char *const kKeys[ICO_VARIANT_COUNT] = {
    "shade_toughness", "one_hit",          "never_dodge",       "aggression",
    "no_hints",        "barrier_hits",     "no_cutscene_look",  "ignore_cutscene_cameras",
    "auto_guard",      "attack_turn",      "shadows_wait_down", "sword_colour",
    "weapon",          "peaceful_shadows", "chain_swing_speed", "chain_swing_damping"};

/* 0: not read yet */
static int s_loaded;
/* the global variants' values; a moved one's entry is unused (val) */
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
    DBG_CHAIN_CYCLE,        /* debug_chain_cycle_speed (ico_variants_bind_debug_chain) */
    DBG_CHAIN_SLOW,         /* debug_chain_slow_speed */
    DBG_COUNT,
    DBG_BIND_FIRST = DBG_CHAIN_CYCLE /* ico_variants_bind_debug binds the ones before */
};

static const char *const kDbgNames[DBG_COUNT] = {
    "one_hit_only", "ignore_dodge",      "enemy_battle_type", "ignore_demo_camera",
    "hand_camera",  "chain_cycle_speed", "chain_slow_speed"};
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
    case ICO_VARIANT_AUTO_GUARD:
    case ICO_VARIANT_ATTACK_TURN:
    case ICO_VARIANT_SHADOWS_WAIT_DOWN:
    case ICO_VARIANT_PEACEFUL_SHADOWS:
        return 2;
    case ICO_VARIANT_SWORD_COLOUR:
        return ICO_SWORD_COUNT;
    case ICO_VARIANT_WEAPON:
        return ICO_WEAPON_COUNT;
    case ICO_VARIANT_CHAIN_SWING_SPEED:
    case ICO_VARIANT_CHAIN_SWING_DAMPING:
        return CHAIN_VALUES;
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
    case ICO_VARIANT_SWORD_COLOUR:
        return kSword[index];
    case ICO_VARIANT_WEAPON:
        return kWeapon[index];
    case ICO_VARIANT_CHAIN_SWING_SPEED:
    case ICO_VARIANT_CHAIN_SWING_DAMPING:
        return kChain[index];
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

/* PC port: the June 2001 demo's enemy table is its own 18 rows (other kinds,
   models and roles than the North American disc's first 18: ico2/sugipon/src/
   enemyKind.c enemyKind_jun26), so a by-index copy of the USA rows would
   make ground Shadows fly and redraw a scale-4 kind at 1.1: American is not
   offered there */
static int american_offered(void)
{
    const IcoRegionInfo *r = ico_region_cur();
    return !(r != NULL && r->slug != NULL && strcmp(r->slug, "jun26") == 0);
}

/* PC port: only the PAL, Japanese, Korean and Chinese discs' restart life
   (enemy_act.c getEnemyRestartLife) multiplies a Shadow's life by
   gameParam[38 + playthroughs cleared]; the North American disc's and the two
   2001 builds' restarts use the table's life as it is, and their gameParam
   (36, 36 and 32 entries) has no entry 38..43. Third journey would change
   nothing there, so it is not offered. */
static int third_journey_offered(void)
{
    return !ICO_NTSCAUG6JUN26;
}

/* PC port: the light sword's two kinds, 8 and 9, are rows of the European,
   Japanese, Korean and Asian discs' weaponKind table only (ten rows; the North
   American and August 2001 tables have eight, the June 2001 demo's seven) and
   only their InitWeaponGeo makes them (weapon.c: the 2001-family copies have no
   case 8 or 9), so Red is offered on those four discs alone */
static int red_sword_offered(void)
{
    return !ICO_NTSCAUG6JUN26;
}

int ico_variant_offered(IcoVariantId id, int index)
{
    if (index < 0 || index >= ico_variant_count(id)) {
        return 0;
    }
    if (id == ICO_VARIANT_SWORD_COLOUR && index == ICO_SWORD_RED) {
        return red_sword_offered();
    }
    if (id != ICO_VARIANT_SHADE_TOUGHNESS) {
        return 1;
    }
    return index == ICO_TOUGHNESS_AMERICAN        ? american_offered()
           : index == ICO_TOUGHNESS_THIRD_JOURNEY ? third_journey_offered()
                                                  : 1;
}

/* The variants that are rows of the journey's difficulty record
   (difficulty.h): their row, or -1 for a global one ([variants] in
   config.toml: the cutscene rows, the weapon rows, the chain rows). */
static int moved_row(IcoVariantId id)
{
    switch (id) {
    case ICO_VARIANT_SHADE_TOUGHNESS:
        return ICO_DIFF_SHADE_TOUGHNESS;
    case ICO_VARIANT_ONE_HIT:
        return ICO_DIFF_ONE_HIT;
    case ICO_VARIANT_NEVER_DODGE:
        return ICO_DIFF_NEVER_DODGE;
    case ICO_VARIANT_AGGRESSION:
        return ICO_DIFF_AGGRESSION;
    case ICO_VARIANT_NO_HINTS:
        return ICO_DIFF_NO_HINTS;
    case ICO_VARIANT_BARRIER_HITS:
        return ICO_DIFF_BARRIER_HITS;
    case ICO_VARIANT_AUTO_GUARD:
        return ICO_DIFF_AUTO_GUARD;
    case ICO_VARIANT_ATTACK_TURN:
        return ICO_DIFF_ATTACK_TURN;
    case ICO_VARIANT_SHADOWS_WAIT_DOWN:
        return ICO_DIFF_SHADOWS_WAIT_DOWN;
    case ICO_VARIANT_PEACEFUL_SHADOWS:
        return ICO_DIFF_PEACEFUL;
    default:
        return -1;
    }
}

/* The value index in force: a moved variant's from the active record (the
   barrier's row 0 is the disc's own, 5 and 7 its indices 0 and 1), a global
   one's from [variants] */
static int val(IcoVariantId id)
{
    const int row = moved_row(id);
    int v;

    if (row < 0) {
        return s_value[id];
    }
    v = ico_difficulty_row((IcoDiffRow)row);
    if (id == ICO_VARIANT_BARRIER_HITS) {
        return v == 0 ? shipped_barrier() : v == 7;
    }
    return v;
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
            "variants: no_cutscene_look=%d ignore_cutscene_cameras=%d sword_colour=%s weapon=%s "
            "chain_swing_speed=%s chain_swing_damping=%s (the Shadow, Combat, hints and "
            "barrier rows are the journey's difficulty: difficulty.h)\n",
            s_value[ICO_VARIANT_NO_CUTSCENE_LOOK], s_value[ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS],
            kSword[s_value[ICO_VARIANT_SWORD_COLOUR]], kWeapon[s_value[ICO_VARIANT_WEAPON]],
            kChain[s_value[ICO_VARIANT_CHAIN_SWING_SPEED]],
            kChain[s_value[ICO_VARIANT_CHAIN_SWING_DAMPING]]);
}

/* [variants] key as a game value 1..10 (the debug rows' range) as an index
   0..9; anything else is named in the log and gives the game's own */
static int chain_index(const char *key)
{
    char path[64];
    long long v;

    snprintf(path, sizeof(path), "variants.%s", key);
    v = ico_config_get_int(path, CHAIN_SHIPPED);
    if (v < 1 || v > CHAIN_VALUES) {
        fprintf(stderr, "variants: [variants] %s = %lld is not 1 to %d; using %d\n", key, v,
                CHAIN_VALUES, CHAIN_SHIPPED);
        v = CHAIN_SHIPPED;
    }
    return (int)v - 1;
}

static void load(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = 1;
    /* the ten rows of the journey's difficulty record (shade_toughness,
       one_hit, never_dodge, aggression, no_hints, barrier_hits, auto_guard,
       attack_turn, shadows_wait_down, peaceful_shadows) are not read here:
       their keys are retired (config.c), read once more only by the
       carry-over of a save without a record (difficulty.h) */
    s_value[ICO_VARIANT_NO_CUTSCENE_LOOK] =
        ico_config_get_bool("variants.no_cutscene_look", 0) != 0;
    s_value[ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS] =
        ico_config_get_bool("variants.ignore_cutscene_cameras", 0) != 0;
    /* the weapon rows: Original is the game as shipped */
    s_value[ICO_VARIANT_SWORD_COLOUR] =
        word_index(kSword, ICO_SWORD_COUNT,
                   ico_config_get_string("variants.sword_colour", kSword[0]), 0, "sword_colour");
    if (!ico_variant_offered(ICO_VARIANT_SWORD_COLOUR, s_value[ICO_VARIANT_SWORD_COLOUR])) {
        fprintf(stderr, "variants: sword_colour = red is not offered on this disc (it has no "
                        "light sword); using original\n");
        s_value[ICO_VARIANT_SWORD_COLOUR] = ICO_SWORD_ORIGINAL;
    }
    s_value[ICO_VARIANT_WEAPON] =
        word_index(kWeapon, ICO_WEAPON_COUNT, ico_config_get_string("variants.weapon", kWeapon[0]),
                   0, "weapon");
    /* the chain rows: 4 is the game's own value */
    s_value[ICO_VARIANT_CHAIN_SWING_SPEED] = chain_index("chain_swing_speed");
    s_value[ICO_VARIANT_CHAIN_SWING_DAMPING] = chain_index("chain_swing_damping");
    log_summary();
}

int ico_variant_get(IcoVariantId id)
{
    if (id < 0 || id >= ICO_VARIANT_COUNT) {
        return 0;
    }
    load();
    {
        const int v = val(id);
        /* a value read before the disc was chosen and not offered on it */
        return ico_variant_offered(id, v) ? v : 0;
    }
}

void ico_variant_set(IcoVariantId id, int index)
{
    const int n = ico_variant_count(id);
    if (n == 0) {
        return;
    }
    load();
    index = index < 0 ? 0 : index >= n ? n - 1 : index;
    if (!ico_variant_offered(id, index)) {
        index = 0; /* American on the June 2001 demo: Original */
    }
    if (moved_row(id) >= 0) {
        /* the active record's row (the barrier's index 0 and 1 are 5 and 7) */
        ico_difficulty_set_row((IcoDiffRow)moved_row(id),
                               id == ICO_VARIANT_BARRIER_HITS ? (index == 1 ? 7 : 5) : index);
    } else {
        s_value[id] = index;
    }
    ico_variants_apply();
}

/* the value a variant holds switch i at, or -1: the game's own */
static int dbg_wanted(int i)
{
    switch (i) {
    case DBG_ONE_HIT:
        return val(ICO_VARIANT_ONE_HIT) ? 1 : -1;
    case DBG_IGNORE_DODGE:
        return val(ICO_VARIANT_NEVER_DODGE) ? 1 : -1;
    case DBG_BATTLE_TYPE:
        return kBattleType[val(ICO_VARIANT_AGGRESSION)];
    case DBG_IGNORE_DEMO_CAMERA:
        return s_value[ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS] ? 1 : -1;
    case DBG_HAND_CAMERA:
        /* the European cutscene look-around (camera-root.c's stick block
           under debug_hand_camera, which the USA disc never sets) */
        return s_value[ICO_VARIANT_NO_CUTSCENE_LOOK] ? 0 : -1;
    case DBG_CHAIN_CYCLE:
        /* the game's own value (4) leaves the switch as the game set it */
        return s_value[ICO_VARIANT_CHAIN_SWING_SPEED] + 1 == CHAIN_SHIPPED
                   ? -1
                   : s_value[ICO_VARIANT_CHAIN_SWING_SPEED] + 1;
    case DBG_CHAIN_SLOW:
        return s_value[ICO_VARIANT_CHAIN_SWING_DAMPING] + 1 == CHAIN_SHIPPED
                   ? -1
                   : s_value[ICO_VARIANT_CHAIN_SWING_DAMPING] + 1;
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
            "ignore_cutscene_cameras=%d auto_guard=%d attack_turn=%d shadows_wait_down=%d "
            "sword_colour=%s weapon=%s (net kind %d) peaceful_shadows=%d chain_swing_speed=%s "
            "chain_swing_damping=%s;%s\n",
            kToughness[val(ICO_VARIANT_SHADE_TOUGHNESS)], val(ICO_VARIANT_ONE_HIT),
            val(ICO_VARIANT_NEVER_DODGE), kAggression[val(ICO_VARIANT_AGGRESSION)],
            val(ICO_VARIANT_NO_HINTS), ico_hints_on() ? "on" : "off", ico_variant_barrier_hits(),
            s_value[ICO_VARIANT_NO_CUTSCENE_LOOK], s_value[ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS],
            val(ICO_VARIANT_AUTO_GUARD), val(ICO_VARIANT_ATTACK_TURN),
            val(ICO_VARIANT_SHADOWS_WAIT_DOWN), kSword[s_value[ICO_VARIANT_SWORD_COLOUR]],
            kWeapon[s_value[ICO_VARIANT_WEAPON]], ico_variant_net_kind(),
            val(ICO_VARIANT_PEACEFUL_SHADOWS), kChain[s_value[ICO_VARIANT_CHAIN_SWING_SPEED]],
            kChain[s_value[ICO_VARIANT_CHAIN_SWING_DAMPING]], sw);
}

void ico_variants_bind_debug(int *one_hit, int *ignore_dodge, int *battle_type,
                             int *ignore_demo_camera, int *hand_camera, void (*menu_open)(void))
{
    int *const p[DBG_BIND_FIRST] = {one_hit, ignore_dodge, battle_type, ignore_demo_camera,
                                    hand_camera};
    for (int i = 0; i < DBG_BIND_FIRST; i++) {
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

void ico_variants_bind_debug_chain(int *cycle_speed, int *slow_speed)
{
    int *const p[2] = {cycle_speed, slow_speed};
    for (int i = 0; i < 2; i++) {
        s_dbg[DBG_BIND_FIRST + i] = p[i];
        s_dbg_base[DBG_BIND_FIRST + i] = p[i] != NULL ? *p[i] : 0;
        s_dbg_held[DBG_BIND_FIRST + i] = 0;
    }
    if (cycle_speed != NULL) {
        fprintf(stderr,
                "variants: the game's chain switches are bound (chain_cycle_speed=%d "
                "chain_slow_speed=%d)\n",
                s_dbg_base[DBG_CHAIN_CYCLE], s_dbg_base[DBG_CHAIN_SLOW]);
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
       "no model" (its table's last row's model) keeps the disc's. Only on a
       table of the same kinds, row for row (the same row count): never on
       the June 2001 demo, whose 18 rows are kinds of its own (PC port:
       american_offered) */
    american = val(ICO_VARIANT_SHADE_TOUGHNESS) == ICO_TOUGHNESS_AMERICAN && usa != NULL &&
               american_offered() && rows == usaRows && kind < usaRows &&
               row->model != disc[rows - 1].model;
    battle = kBattleType[val(ICO_VARIANT_AGGRESSION)];
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
    int v, from;

    if (n == 0) {
        return 0;
    }
    load();
    from = val(id);
    v = from;
    do {
        /* past a value this disc does not offer (American on the June 2001
           demo: Original <-> Not tougher later) */
        v = ((v + dir) % n + n) % n;
    } while (!ico_variant_offered(id, v) && v != from && dir != 0);
    ico_variant_set(id, v);
    if (moved_row(id) >= 0) {
        /* a row of the journey's difficulty record: no config key (the New
           Game screen chooses it; difficulty.h) */
        fprintf(stderr, "variants: %s = %s (the journey's difficulty; config.toml unchanged)\n",
                kKeys[id], ico_variant_value_name(id, v));
        return v;
    }
    snprintf(path, sizeof(path), "variants.%s", kKeys[id]);
    if (id == ICO_VARIANT_BARRIER_HITS) {
        ico_config_set_int(path, v == 1 ? 7 : 5);
    } else if (id == ICO_VARIANT_CHAIN_SWING_SPEED || id == ICO_VARIANT_CHAIN_SWING_DAMPING) {
        ico_config_set_int(path, v + 1);
    } else if (id == ICO_VARIANT_SHADE_TOUGHNESS || id == ICO_VARIANT_AGGRESSION ||
               id == ICO_VARIANT_SWORD_COLOUR || id == ICO_VARIANT_WEAPON) {
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

/* The Combat rows: a hook that is read in a fight counts the times it kept the
   game's rule off, the first 8 then every 100th (every 600th for the two read
   every frame), so a reporter's log shows the row
   at work (variants: ... lines carry the values). */
static void count_off(const char *what, unsigned *n, unsigned every)
{
    (*n)++;
    if (*n <= 8 || *n % every == 0) {
        fprintf(stderr, "variants: %s is off (%u)\n", what, *n);
    }
}

int ico_variant_auto_guard(void)
{
    static unsigned n;
    const int on = ico_variant_get(ICO_VARIANT_AUTO_GUARD);
    if (!on) {
        count_off("auto_guard: a front hit lands as a hit", &n, 100);
    }
    return on;
}

int ico_variant_attack_turn(void)
{
    static unsigned n;
    const int on = ico_variant_get(ICO_VARIANT_ATTACK_TURN);
    if (!on) {
        count_off("attack_turn: the swing does not turn Ico", &n, 600);
    }
    return on;
}

int ico_variant_shadows_wait_down(void)
{
    static unsigned n;
    const int on = ico_variant_get(ICO_VARIANT_SHADOWS_WAIT_DOWN);
    if (!on) {
        count_off("shadows_wait_down: the Shadows are not held", &n, 600);
    }
    return on;
}

int ico_variant_down_keeps_hit(int is_boy)
{
    static unsigned n;
    if (!is_boy) {
        return 1;
    }
    if (!ico_variant_shadows_wait_down()) {
        count_off(
            "down_keeps_hit: Ico's down action leaves his hit flag clear (hittable while down)", &n,
            600);
        return 0;
    }
    return 1;
}

int ico_variant_shadow_brain(void)
{
    static unsigned n;
    const int peaceful = ico_variant_get(ICO_VARIANT_PEACEFUL_SHADOWS);
    if (peaceful) {
        /* a Shadow starts: the first 8, then every 100th */
        n++;
        if (n <= 8 || n % 100 == 0) {
            fprintf(stderr, "variants: peaceful_shadows: Shadow %u starts without its brain\n", n);
        }
    }
    return !peaceful;
}

/* The weapon kinds (weapon.c InitWeaponGeo, boyact.c PutWeapon).  The kinds
   are weaponKind's rows: 1 the stick (the one that carries a torch and
   catches fire), 8 and 9 the light sword (the same blade, model and power;
   8's trail red, 9's pale), and the insect net, 7 on six discs and 6 on the
   June 2001 demo (its table has seven rows; WeaponDL_jun26 draws
   dispInsectNet at case 6) */
#define WK_STICK 1
#define WK_LIGHT_RED 8
#define WK_LIGHT 9

int ico_variant_net_kind(void)
{
    return ICO_JUN26 ? 6 : 7;
}

int ico_variant_weapon_kind(int kind)
{
    static unsigned nSword, nNet;
    int to = kind;

    if (kind == WK_LIGHT && ico_variant_get(ICO_VARIANT_SWORD_COLOUR) == ICO_SWORD_RED) {
        to = WK_LIGHT_RED;
        nSword++;
        if (nSword <= 8 || nSword % 100 == 0) {
            fprintf(stderr,
                    "variants: sword_colour: the light sword (kind 9) is made as kind 8, its "
                    "trail red (%u)\n",
                    nSword);
        }
    } else if (kind == WK_STICK && ico_variant_get(ICO_VARIANT_WEAPON) == ICO_WEAPON_NET) {
        to = ico_variant_net_kind();
        nNet++;
        if (nNet <= 8 || nNet % 100 == 0) {
            fprintf(stderr,
                    "variants: weapon: the stick (kind 1) is made as the insect net (kind %d) "
                    "(%u)\n",
                    to, nNet);
        }
    }
    return to;
}

int ico_variant_weapon_put_kind(int kind)
{
    /* a kind 8 in play is the light sword the hook made, whatever Light sword
       colour says now: the row can go back to Original while that sword is
       in the room (or carried), and it must still be set down as kind 9 is
       (SetWeaponOffsetMode 1).  So 8 reads as 9 on every disc that offers
       Red, not only while Red is on.  Safe: the only kind 8 placements on
       those discs are objLayout rows 12 and 86, the makers' unused rows of
       one of every weapon, which no story stage loads; a kind 8 made from
       them would only be set down as the light sword is (the same blade and
       model).  The 2001-family discs have no kind 8 and no light sword: 8
       stays 8 there */
    if (kind == WK_LIGHT_RED && red_sword_offered()) {
        return WK_LIGHT;
    }
    return kind;
}

int ico_variants_suspend_achievements(void)
{
    int mask;
    load();
    /* the difficulty rows (One hit per swing, Shadows never dodge, Peaceful
       shadows among them) never switch achievements off: only Ignore
       cutscene cameras and the Debug menu do */
    mask = (s_value[ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS] ? 4 : 0) | (s_debug_menu_used ? 8 : 0);
    if (mask != s_suspend_logged) {
        s_suspend_logged = mask;
        if (mask != 0) {
            fprintf(stderr, "variants: achievements are off (%s%s)\n",
                    mask & 4 ? "ignore_cutscene_cameras " : "", mask & 8 ? "debug_menu" : "");
        } else {
            fprintf(stderr, "variants: achievements are back on\n");
        }
    }
    return mask != 0;
}

int ico_variants_debug_menu_pressed(void)
{
    /* marked used (achievements off until a reload) only when it opens: an
       unbound press changes nothing */
    if (!ico_variants_open_debug_menu()) {
        return 0;
    }
    s_debug_menu_used = 1;
    fprintf(stderr, "variants: the Debug menu is opened from Options\n");
    return 1;
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
