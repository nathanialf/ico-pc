/*
 * port/game/variants.h
 *
 * The game variants: Options > Extras > Game variants.  Eight settings
 * that change how the game plays, each read once from config.toml
 * ([variants], via ico_config_get_*) the first time it is asked for and
 * settable at run time, as the options of options.h.  Every default is the
 * game as the disc ships it.
 *
 *   [variants] shade_toughness
 *                          "original"   "original" (the disc's table),
 *                                       "american" (every Shadow as tough as
 *                                       on the North American disc, and no
 *                                       tougher on later playthroughs) or
 *                                       "no_plus" (the disc's Shadows, no
 *                                       tougher on later playthroughs)
 *   [variants] one_hit      false  one hit per swing (debug_one_hit_only)
 *   [variants] never_dodge  false  the Shadows never dodge (debug_ignore_dodge)
 *   [variants] aggression  "own"   "own" (each Shadow's own), "bold_patient",
 *                                  "bold_quick", "wary_patient" or
 *                                  "wary_quick" (debug_enemy_battle_type)
 *   [variants] no_hints     false  no hints (the North American disc has none)
 *   [variants] barrier_hits (absent) 5 or 7: the hits the Queen's barrier
 *                                  takes; absent: the disc's own (5, and 7 on
 *                                  the June 2001 demo disc, "jun26")
 *   [variants] no_cutscene_look
 *                          false  no camera look-around in cutscenes
 *   [variants] ignore_cutscene_cameras
 *                          false  the cutscenes' cameras are ignored
 *                                  (debug_ignore_demo_camera)
 *
 * The game reads them in three ways (docs/port/GAME_FEATURES.md "Game
 * variants" names every site):
 *   - debug switches the game already has (One hit per swing, Shadows never
 *     dodge, Shadow aggression, Ignore cutscene cameras, and No cutscene
 *     look-around as debug_hand_camera 0, the USA disc's value): debug.c's
 *     debug_VariableInit binds them (ico_variants_bind_debug) and
 *     ico_variants_apply writes them at the bind, at every reload and on every
 *     change; a variant at its shipped value leaves its switch as the game
 *     set it (and puts back the game's value when it is switched off);
 *   - the enemy table: enemy.c reads every enemyKind row through
 *     ico_variant_enemy_def (Shadow toughness American, Shadow aggression);
 *     enemy_act.c's restart life reads ico_variant_shade_toughness;
 *   - local reads: ico_hints_on (lws_kyomi.c, girl.c), ico_variant_barrier_hits
 *     (queen.c).
 * With every row at its shipped value the game runs as before.
 *
 * Achievements are suspended (ico_variants_suspend_achievements, read by
 * gamestate.c beside developer mode) while One hit per swing, Shadows never
 * dodge or Cutscene cameras ignored is on, and from the Debug menu's being
 * opened until the options are reloaded or ico_variants_debug_menu_clear.
 * Shadow toughness, aggression, No hints, the barrier and the look-around do
 * not suspend them.
 */
#ifndef ICO_PORT_GAME_VARIANTS_H
#define ICO_PORT_GAME_VARIANTS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum IcoVariantId {
    ICO_VARIANT_SHADE_TOUGHNESS = 0,
    ICO_VARIANT_ONE_HIT,
    ICO_VARIANT_NEVER_DODGE,
    ICO_VARIANT_AGGRESSION,
    ICO_VARIANT_NO_HINTS,
    ICO_VARIANT_BARRIER_HITS,
    ICO_VARIANT_NO_CUTSCENE_LOOK,
    ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS,
    ICO_VARIANT_COUNT
} IcoVariantId;

typedef enum IcoVariantToughness {
    ICO_TOUGHNESS_ORIGINAL = 0,
    ICO_TOUGHNESS_AMERICAN,
    ICO_TOUGHNESS_NO_PLUS,
    ICO_TOUGHNESS_COUNT
} IcoVariantToughness;

typedef enum IcoVariantAggression {
    ICO_AGGRESSION_OWN = 0, /* the Shadow's own; the game sets nothing */
    ICO_AGGRESSION_BOLD_PATIENT,
    ICO_AGGRESSION_BOLD_QUICK,
    ICO_AGGRESSION_WARY_PATIENT,
    ICO_AGGRESSION_WARY_QUICK,
    ICO_AGGRESSION_COUNT
} IcoVariantAggression;

/* The values, as the game reads them (0 or 1 for the switches). */
int ico_variant_shade_toughness(void); /* IcoVariantToughness */
int ico_variant_one_hit(void);
int ico_variant_never_dodge(void);
int ico_variant_aggression(void); /* IcoVariantAggression */
int ico_variant_no_hints(void);
int ico_variant_barrier_hits(void); /* 5 or 7 */
int ico_variant_no_cutscene_look(void);
int ico_variant_ignore_cutscene_cameras(void);

/* The Settings page's way in.  A variant has ico_variant_count(id) values,
   numbered from 0 (a switch: 0 off, 1 on; the barrier: 0 is 5 hits, 1 is 7;
   the others as their enums above).  ico_variant_get is the index in force,
   ico_variant_set sets it (clamped; no config write), ico_variant_step moves
   it by dir (wrapping), writes [variants] <key> in memory (the caller saves
   the file), logs the change and returns the new index. */
int ico_variant_count(IcoVariantId id);
int ico_variant_get(IcoVariantId id);
void ico_variant_set(IcoVariantId id, int index);
int ico_variant_step(IcoVariantId id, int dir);
/* the config key under [variants], and the word a value is written as
   ("original", "no_plus", "true", "7", ...) */
const char *ico_variant_key(IcoVariantId id);
const char *ico_variant_value_name(IcoVariantId id, int index);
/* 1 when value index of id is offered on the selected disc: every value but
   Shadow toughness American on the June 2001 demo, whose enemy rows are
   kinds of its own (PC port: a by-index copy of the USA rows would change
   unrelated Shadows). A config "american" there reads as Original with a log
   line, ico_variant_set gives Original for it, and ico_variant_step passes
   over it (Original <-> Not tougher later). The indices stay the enum's. */
int ico_variant_offered(IcoVariantId id, int index);

/* 1 while a variant that switches achievements off is in force: One hit per
   swing, Shadows never dodge, Cutscene cameras ignored, or the Debug menu
   opened.  Logs "variants: achievements are off ..." when that changes. */
int ico_variants_suspend_achievements(void);

/* The Debug menu row (shown with Developer mode on) was pressed: marks the
   Debug menu used (achievements suspended) and calls
   ico_variants_open_debug_menu; returns what it returns (1: the menu is
   open, the caller closes Options). */
int ico_variants_debug_menu_pressed(void);
void ico_variants_debug_menu_clear(void);

/* Opens the game's debug menu through the opener the game bound
   (debug.c debug_MenuOpen: menuState as SELECT sets it in developer mode);
   0 and a log line when nothing is bound (the game has not started, or a
   test without the game). */
int ico_variants_open_debug_menu(void);

/* Back to the config's values (ico_opt_reload calls it), then
   ico_variants_apply. */
void ico_variants_reload(void);

/* The game's half (ico2/common/src/debug.c debug_VariableInit, after the
   retail values and the developer's option file are set): where the debug
   switches the variants hold are, and the debug menu's opener.  Each NULL
   unbinds (tests).  Logs once per bind and applies. */
void ico_variants_bind_debug(int *one_hit, int *ignore_dodge, int *battle_type,
                             int *ignore_demo_camera, int *hand_camera, void (*menu_open)(void));

/* Writes the bound switches from the values in force (variants.h above) and
   logs "variants: applied ..." with every value and every switch.  Called at
   the bind, by ico_variants_reload and by ico_variant_set (so by every step
   of the page). */
void ico_variants_apply(void);

/* 1 when the disc has a hint system (not the USA disc nor the two 2001
   builds) and No hints is off: lws_kyomi.c's hint timers and girl.c's hint
   voice read it. */
int ico_hints_on(void);

#ifdef ENEMY_H
/* The enemy table row the game reads for kind (enemy.c, every reader of
   enemyKind): the disc's own row while Shadow toughness is not American and
   Shadow aggression is the Shadow's own; otherwise a copy with the USA table's
   life, scale, fly type and battle type (American: the disc's model,
   particle, fly acceleration, dodge range and parameter row, and a row of
   the disc's "no model" left whole) and the chosen battle type
   (aggression).  A kind outside 0 .. rows-1 reads the last row (logged
   once): the June 2001 demo's table has 18 rows, the others 45. */
const EnemyDef *ico_variant_enemy_def(const EnemyDef *disc, int rows, const EnemyDef *usa,
                                      int usaRows, int kind);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_GAME_VARIANTS_H */
