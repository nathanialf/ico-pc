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
 *                                       "american" (every Shade as tough as
 *                                       on the North American disc, and no
 *                                       tougher on later playthroughs) or
 *                                       "no_plus" (the disc's Shades, no
 *                                       tougher on later playthroughs)
 *   [variants] one_hit      false  one hit per swing (debug_one_hit_only)
 *   [variants] never_dodge  false  the Shades never dodge (debug_ignore_dodge)
 *   [variants] aggression  "own"   "own" (each Shade's own), "bold_patient",
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
 * This file is the page's half: the values, their config keys and the
 * achievements rule.  The game hooks that read them (enemy.c, attackhit.c,
 * queen.c, the hint code, the camera) are another change; until they are
 * wired the page changes the keys and nothing else.
 *
 * Achievements are suspended (ico_variants_suspend_achievements, read by
 * gamestate.c beside developer mode) while One hit per swing, Shades never
 * dodge or Cutscene cameras ignored is on, and from the Debug menu's being
 * opened until the options are reloaded or ico_variants_debug_menu_clear.
 * Shade toughness, aggression, No hints, the barrier and the look-around do
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
    ICO_AGGRESSION_OWN = 0, /* the Shade's own; the game sets nothing */
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

/* 1 while a variant that switches achievements off is in force: One hit per
   swing, Shades never dodge, Cutscene cameras ignored, or the Debug menu
   opened.  Logs "variants: achievements are off ..." when that changes. */
int ico_variants_suspend_achievements(void);

/* The Debug menu row (shown with Developer mode on) was pressed: marks the
   Debug menu used (achievements suspended) and calls
   ico_variants_open_debug_menu. */
void ico_variants_debug_menu_pressed(void);
void ico_variants_debug_menu_clear(void);

/* The host hook behind the row: opens the game's debug menu and closes
   Options.  Implemented with the game hooks; until then a stub in
   variants.c that logs once. */
void ico_variants_open_debug_menu(void);

/* Back to the config's values (ico_opt_reload calls it). */
void ico_variants_reload(void);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_GAME_VARIANTS_H */
