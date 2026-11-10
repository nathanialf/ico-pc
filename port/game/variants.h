/*
 * port/game/variants.h
 *
 * The game variants: Options > Gameplay (No hints) and its Shadows,
 * Combat and Cutscenes pages, the Debug menu on Developer.  Sixteen settings
 * that change how the game plays.  Every default is the game as the disc
 * ships it.
 *
 * Ten of them are rows of the journey's difficulty record (difficulty.h),
 * chosen per save on the New Game screen: shade_toughness, one_hit,
 * never_dodge, aggression, no_hints, barrier_hits, auto_guard, attack_turn,
 * shadows_wait_down, peaceful_shadows.  ico_variant_get and the accessors
 * read them from the active record, ico_variant_set and ico_variant_step
 * edit the active record (no config key is written); their [variants] keys
 * below are retired (config.c logs them as not used; read once more only by
 * a save's carry-over, ico_difficulty_from_config).  The other six
 * (no_cutscene_look, ignore_cutscene_cameras, sword_colour, weapon and the
 * two chain rows) stay in config.toml, each read once ([variants], via
 * ico_config_get_*) the first time it is asked for and settable at run time,
 * as the options of options.h.
 *
 *   [variants] shade_toughness
 *                          "original"   "original" (the disc's table),
 *                                       "american" (every Shadow as tough as
 *                                       on the North American disc, and no
 *                                       tougher on later playthroughs) or
 *                                       "no_plus" (the disc's Shadows, no
 *                                       tougher on later playthroughs) or
 *                                       "third_journey" (the factor of a
 *                                       third playthrough, twice the first
 *                                       journey's, that the game's table has
 *                                       and no playthrough reaches; the
 *                                       PAL, Japanese, Korean and Chinese
 *                                       discs only)
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
 *   [variants] auto_guard   true   Ico blocks a Shadow's attack from the front
 *   [variants] attack_turn  true   Ico's swing turns him toward the nearest Shadow
 *   [variants] shadows_wait_down
 *                          true   the Shadows wait while Ico is knocked down
 *   [variants] sword_colour "original" "original" or "red": the light sword
 *                                  (weapon kind 9) is made as the makers'
 *                                  red twin, kind 8 (European, Japanese,
 *                                  Korean and Asian discs only: the others
 *                                  have neither kind)
 *   [variants] weapon      "original" "original" or "net": the stick (weapon
 *                                  kind 1) is made as the makers' unused
 *                                  insect net (kind 7; kind 6 on the June
 *                                  2001 demo, whose table is one row
 *                                  shorter)
 *   [variants] peaceful_shadows
 *                          false  the Shadows get no brain thread (the game's
 *                                  BrainOnOff switch, for Shadows only)
 *   [variants] chain_swing_speed
 *                          4      1..10: how fast a chain swings under Ico
 *                                  (the game's debug row CHAIN CYCLE SPEED;
 *                                  higher is a shorter swing)
 *   [variants] chain_swing_damping
 *                          4      1..10: how quickly a swing dies down while
 *                                  Ico is not swinging it (CHAIN SLOW SPEED;
 *                                  higher is quicker)
 *
 * The game reads them in three ways (docs/port/GAME_FEATURES.md "Game
 * variants" names every site):
 *   - debug switches the game already has (One hit per swing, Shadows never
 *     dodge, Shadow aggression, Ignore cutscene cameras, and No cutscene
 *     look-around as debug_hand_camera 0, the USA disc's value; Chain swing
 *     speed and damping as debug_chain_cycle_speed and debug_chain_slow_speed,
 *     ico_variants_bind_debug_chain): debug.c's
 *     debug_VariableInit binds them (ico_variants_bind_debug) and
 *     ico_variants_apply writes them at the bind, at every reload and on every
 *     change; a variant at its shipped value leaves its switch as the game
 *     set it (and puts back the game's value when it is switched off);
 *   - the enemy table: enemy.c reads every enemyKind row through
 *     ico_variant_enemy_def (Shadow toughness American, Shadow aggression);
 *     enemy_act.c's restart life reads ico_variant_shade_toughness;
 *   - local reads: ico_hints_on (lws_kyomi.c, girl.c), ico_variant_barrier_hits
 *     (queen.c), ico_variant_weapon_kind (weapon.c InitWeaponGeo, each disc's
 *     copy) and ico_variant_weapon_put_kind (boyact.c PutWeapon).
 * With every row at its shipped value the game runs as before.
 *
 * Achievements are suspended (ico_variants_suspend_achievements, read by
 * gamestate.c beside developer mode) while Cutscene cameras ignored is on,
 * and from the Debug menu's being opened until the options are reloaded or
 * ico_variants_debug_menu_clear.  No difficulty row ever suspends them (One
 * hit per swing, Shadows never dodge and Peaceful shadows did before
 * v0.11.3), nor do the look-around, the two weapon rows (Light sword colour,
 * a colour only; Stick, a weapon that cannot hurt) and the two chain rows
 * (the swing's pace, the game's own debug values).
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
    ICO_VARIANT_AUTO_GUARD,
    ICO_VARIANT_ATTACK_TURN,
    ICO_VARIANT_SHADOWS_WAIT_DOWN,
    ICO_VARIANT_SWORD_COLOUR,
    ICO_VARIANT_WEAPON,
    ICO_VARIANT_PEACEFUL_SHADOWS,
    ICO_VARIANT_CHAIN_SWING_SPEED,   /* value 1..10 is index 0..9 */
    ICO_VARIANT_CHAIN_SWING_DAMPING, /* value 1..10 is index 0..9 */
    ICO_VARIANT_COUNT
} IcoVariantId;

/* Light sword colour's values */
typedef enum IcoVariantSwordColour {
    ICO_SWORD_ORIGINAL = 0, /* kind 9 as placed: the pale trail */
    ICO_SWORD_RED,          /* kind 8: the red trail (255, 32, 32) */
    ICO_SWORD_COUNT
} IcoVariantSwordColour;

/* Stick's values */
typedef enum IcoVariantWeapon {
    ICO_WEAPON_ORIGINAL = 0, /* the stick as placed */
    ICO_WEAPON_NET,          /* the insect net */
    ICO_WEAPON_COUNT
} IcoVariantWeapon;

typedef enum IcoVariantToughness {
    ICO_TOUGHNESS_ORIGINAL = 0,
    ICO_TOUGHNESS_AMERICAN,
    ICO_TOUGHNESS_NO_PLUS,
    ICO_TOUGHNESS_THIRD_JOURNEY, /* gameParam[40], the game's x2 */
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
/* The Combat page's rows, all three On as the game ships (a missing key reads
   as true).  Off: auto_guard, a hit from the front within 60 degrees is not
   blocked (commonact.c's guard rule); attack_turn, Ico's swing does not turn
   him toward the nearest Shadow (boyact.c's actBoyAttack); shadows_wait_down,
   the down act and its get-up timer no longer hold the Shadows' brains
   (boyact.c IsBoyStatus_EnemyMustWait, read by ebrain.c) and Ico's down
   action no longer makes him invincible (ico_variant_down_keeps_hit). */
int ico_variant_auto_guard(void);
int ico_variant_attack_turn(void);
int ico_variant_shadows_wait_down(void);
/* The down action (commonact.c's actCommonDown, the four disc copies) sets the
   actor's `hit` flag, the game's invincibility that AttackGenerate* skips,
   every frame it lies.  1 = keep setting it: any actor but Ico, and Ico while
   shadows_wait_down is On (the game's rule); 0 for Ico with it Off, so the
   Shadows' swings land while he is on the ground.  is_boy: self == boyGObj. */
int ico_variant_down_keeps_hit(int is_boy);
/* Peaceful shadows: 1 when a Shadow gets its brain thread (the game's
   own, the default), 0 with Peaceful shadows on.  Read at the four places
   enemy_act.c starts a Shadow (one per disc copy) beside debug_brain_flag, so
   Yorda's own brain thread (girl_act.c, the same flag) is never touched. */
int ico_variant_shadow_brain(void);

/* The two weapon rows (Gameplay > Combat), read where a weapon object is
   made.  ico_variant_weapon_kind(kind) is the kind InitWeaponGeo gives a
   weapon whose layout says kind (weapon.c, every disc's copy): with Light
   sword colour Red, the light sword's kind 9 becomes kind 8 (the same blade,
   model and power; its trail is red: weaponKind rows 8 and 9 differ only in
   the trail's colour on the four discs that have them); with Stick Insect
   net, the stick's kind 1 becomes the insect net's (7; 6 on the June 2001
   demo), whose row has power 0 and request kind 0, which no motion-orient
   row answers, so it is held but never swung and cannot be lit (only kind 1
   catches fire).  Every other kind, and every kind at the rows' Original,
   comes back as it went in.  Counted log lines (first 8, then every 100th).
   ico_variant_weapon_put_kind(kind) is the kind boyact.c PutWeapon's
   kind-9 check reads: 9 for a kind 8 on every disc that offers Red (whether
   Red is on now or was switched off after the sword was made), so the
   sword set down stands as the game's does; kind otherwise.
   ico_variant_net_kind() is this disc's insect-net kind (7, or 6 on the
   June 2001 demo). */
int ico_variant_weapon_kind(int kind);
int ico_variant_weapon_put_kind(int kind);
int ico_variant_net_kind(void);

/* The Settings page's way in.  A variant has ico_variant_count(id) values,
   numbered from 0 (a switch: 0 off, 1 on; the barrier: 0 is 5 hits, 1 is 7;
   the chain rows: index 0..9 is the game's value 1..10, 3 (value 4) its own;
   the others as their enums above).  ico_variant_get is the index in force,
   ico_variant_set sets it (clamped; no config write), ico_variant_step moves
   it by dir (wrapping), writes [variants] <key> in memory (the caller saves
   the file; a difficulty row writes no key: it edits the active record),
   logs the change and returns the new index. */
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
   unrelated Shadows), and Shadow toughness Third journey on the three
   discs whose restart life has no gameParam factor (the North American disc
   and the two 2001 builds: only the PAL, Japanese, Korean and Chinese discs
   multiply a Shadow's life by gameParam[38 + playthroughs cleared]). A config
   word that is not offered reads as Original with a log line,
   ico_variant_set gives Original for it, and ico_variant_step passes over
   it. The indices stay the enum's. */
int ico_variant_offered(IcoVariantId id, int index);

/* 1 while a variant that switches achievements off is in force: Cutscene
   cameras ignored, or the Debug menu opened (no difficulty row).  Logs
   "variants: achievements are off ..." when that changes. */
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
   ico_variants_apply.  The difficulty rows are the journey's and are kept. */
void ico_variants_reload(void);

/* The game's half (ico2/common/src/debug.c debug_VariableInit, after the
   retail values and the developer's option file are set): where the debug
   switches the variants hold are, and the debug menu's opener.  Each NULL
   unbinds (tests).  Logs once per bind and applies. */
void ico_variants_bind_debug(int *one_hit, int *ignore_dodge, int *battle_type,
                             int *ignore_demo_camera, int *hand_camera, void (*menu_open)(void));

/* The same for the two chain switches (debug_chain_cycle_speed, debug_chain_slow_speed),
   called right after ico_variants_bind_debug (a separate call so that one's
   signature stays).  A value of 4, the game's own, leaves the switch as the
   game set it. */
void ico_variants_bind_debug_chain(int *cycle_speed, int *slow_speed);

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
