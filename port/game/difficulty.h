/*
 * port/game/difficulty.h
 *
 * The difficulty of a journey (one save's run): the record every save
 * carries, chosen on the New Game screen as Easy, Normal or Hard (or
 * customised row by row), and the rules it adds (the knock-down limit,
 * Permadeath, No Shadows).  The rows that used to be global config keys
 * ([gameplay] yorda_safe, pit_time, shadow_speed, yorda_trips,
 * shadows_together and [variants] shade_toughness, one_hit, never_dodge,
 * aggression, no_hints, barrier_hits, auto_guard, attack_turn,
 * shadows_wait_down, peaceful_shadows) are rows of this record now: the
 * accessors of options.h and variants.h keep their names and read the active
 * record, so the game's hook sites are unchanged.  The fifteen keys are
 * retired (config.c logs them as not used) and read once more only for the
 * carry-over of a save that has no record yet (ico_difficulty_from_config).
 *
 * The active record is the run's: Normal (the game as the disc ships it) at
 * the title (ico_opt_mirror_reset calls ico_difficulty_reset), the draft's at
 * the New Game confirm (ico_difficulty_begin), the save's at a load
 * (difficulty_file.c, through ico_difficulty_set_active), the state's at a
 * save-state load (ico_difficulty_state_loaded).  A record names no game
 * symbol and holds no pointer: it is one save-state block as it is.
 *
 * Every change of the active record logs one line naming the mode, the
 * journey, its origin and every row that differs from Normal ("difficulty:
 * Hard in force (journey 9f3a6c0e5d2b7a11, new): shadow_speed=faster ...").
 * Each counted knock-down logs "difficulty: knock-down 2 of 3 in this room",
 * the limit and a Permadeath end their own lines.
 *
 * Compiled into ico_options with variants.c and options.c (it calls
 * ico_variants_apply and the pit-time parser); difficulty_file.c (the
 * difficulty.toml sidecar) beside it.  Tests: port/game/test/difficulty_test.c.
 */
#ifndef ICO_PORT_GAME_DIFFICULTY_H
#define ICO_PORT_GAME_DIFFICULTY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum IcoDiffMode {
    ICO_DIFF_EASY = 0,
    ICO_DIFF_NORMAL,
    ICO_DIFF_HARD,
    ICO_DIFF_CUSTOM,
    ICO_DIFF_MODE_COUNT
} IcoDiffMode;

/* Where the active record came from (the sidecar's slot_N_origin). */
typedef enum IcoDiffOrigin {
    ICO_DIFF_ORIGIN_NONE = 0, /* the title's Normal: no journey */
    ICO_DIFF_ORIGIN_NEW,      /* a New Game */
    ICO_DIFF_ORIGIN_CONFIG,   /* the one-time carry-over of the retired config keys */
    ICO_DIFF_ORIGIN_STATE,    /* a save state made before records existed (its keys) */
    ICO_DIFF_ORIGIN_COUNT
} IcoDiffOrigin;

/* The rows.  Each value is the integer the old option or variant used, so
   the accessors return it unchanged:
     SHADOW_SPEED       ICO_SPEED_* (options.h): 0 slower, 1 original, 2 faster, 3 fastest
     SHADE_TOUGHNESS    IcoVariantToughness (variants.h)
     AGGRESSION         IcoVariantAggression (variants.h)
     BARRIER_HITS       0 the disc's own (5; 7 on the June 2001 demo), 5 or 7
     PIT_TIME           0 the disc's own (ICO_PIT_ORIGINAL), else 4..30 seconds
     KNOCK_LIMIT        0 off, 1..9 knock-downs
     KNOCK_SCOPE        0 each room, 1 the whole journey
     the others         0 off, 1 on (AUTO_GUARD, ATTACK_TURN, SHADOWS_WAIT_DOWN
                        are 1 in the game as shipped) */
typedef enum IcoDiffRow {
    ICO_DIFF_SHADOW_SPEED = 0,
    ICO_DIFF_SHADE_TOUGHNESS,
    ICO_DIFF_AGGRESSION,
    ICO_DIFF_PEACEFUL,
    ICO_DIFF_YORDA_SAFE,
    ICO_DIFF_SHADOWS_TOGETHER,
    ICO_DIFF_NO_SHADOWS,
    ICO_DIFF_AUTO_GUARD,
    ICO_DIFF_ATTACK_TURN,
    ICO_DIFF_SHADOWS_WAIT_DOWN,
    ICO_DIFF_ONE_HIT,
    ICO_DIFF_NEVER_DODGE,
    ICO_DIFF_BARRIER_HITS,
    ICO_DIFF_PIT_TIME,
    ICO_DIFF_YORDA_TRIPS,
    ICO_DIFF_NO_HINTS,
    ICO_DIFF_KNOCK_LIMIT,
    ICO_DIFF_KNOCK_SCOPE,
    ICO_DIFF_PERMADEATH,
    ICO_DIFF_ROW_COUNT
} IcoDiffRow;

#define ICO_DIFF_VERSION 1
#define ICO_DIFF_ROW_SLOTS 32 /* rows with spares: the state block never resizes */
#define ICO_DIFF_KNOCK_MAX 9
#define ICO_DIFF_SCOPE_ROOM 0
#define ICO_DIFF_SCOPE_JOURNEY 1
/* the act mode of the down act (commonact.c actCommonDown, 0x15 = 21) */
#define ICO_DIFF_DOWN_ACT_MODE 0x15

/* int32 only, no pointers: the save-state block "difficulty.record" as it
   is (ICO_STATE_FRESH: a state of an earlier version lacks it). */
typedef struct IcoDifficulty {
    int32_t version; /* ICO_DIFF_VERSION */
    int32_t mode;    /* IcoDiffMode as chosen (the carry-over: Normal or Custom) */
    int32_t origin;  /* IcoDiffOrigin */
    int32_t row[ICO_DIFF_ROW_SLOTS];
    uint32_t journey[2]; /* the journey's id, 0 0 for none (the title) */
    int32_t knocks_room; /* counted knock-downs since the room was entered */
    int32_t
        knocks_journey;   /* counted knock-downs since the journey began or its limit's game over */
    int32_t in_fall;      /* 1 from a counted knock-down until Ico is back on his feet */
    int32_t over_pending; /* 1 from the knock-down that reached the limit until the game over */
    int32_t finished;     /* 1 once a Permadeath journey's game over ended it */
    int32_t run_port;     /* the card port and slot the run was last loaded from or saved to */
    int32_t run_slot;
    int32_t run_has_slot; /* 0 until a load or a save names them */
    uint32_t run_sum;     /* that save's game-block checksum */
    int32_t reserved[18];
} IcoDifficulty;

/* --- presets, classification, the rows ------------------------------------ */

/* The preset's rows for the selected disc (ico_region_cur): Hard's Shadow
   toughness is Third journey on the European, Japanese, Korean and Chinese
   discs, American on the North American disc and the August 2001 preview,
   Original on the June 2001 demo.  A value the disc does not offer falls back
   to Original with a log line.  Counts, journey and run fields zero; origin
   NONE; mode = the preset (ICO_DIFF_CUSTOM gives Normal's rows, mode Custom). */
void ico_difficulty_preset(IcoDiffMode mode, IcoDifficulty *out);
/* The preset whose rows match d's exactly (after the disc's equal values are
   made one: a barrier of the disc's own number is the disc's, a time to rescue
   of the disc's own seconds is Original), else ICO_DIFF_CUSTOM. */
IcoDiffMode ico_difficulty_classify(const IcoDifficulty *d);
/* "easy", "normal", "hard", "custom" (the sidecar's words); "" past the end.
   _parse gives the mode of a word, -1 for none. */
const char *ico_difficulty_mode_name(IcoDiffMode mode);
int ico_difficulty_mode_parse(const char *word);
/* "none", "new", "config", "state"; _parse the reverse (-1 for none) */
const char *ico_difficulty_origin_name(IcoDiffOrigin origin);
int ico_difficulty_origin_parse(const char *word);
/* A row's key in the rows text ("shadow_speed", "knock_limit": the retired
   config words for the fifteen old rows), "" past the end. */
const char *ico_difficulty_row_key(IcoDiffRow row);
/* 1 when value is one row may hold on the selected disc (the toughness
   values ico_variant_offered gives; pit time 0 or 4..30; barrier 0, 5, 7;
   the limit 0..9; the switches 0, 1). */
int ico_difficulty_row_valid(IcoDiffRow row, int value);
/* Moves d's row by dir (the Customize page's Left/Right) and returns the new
   value: the switches toggle; the word rows wrap, past values the disc does
   not offer; the barrier has two steps, the disc's own number
   and the other one; the time to rescue
   steps a second at a time from the disc's own seconds (shown for Original),
   held at 4 and 30, the disc's own seconds reading as Original; the limit
   wraps Off, 1..9.  d's mode is left as it is (the caller classifies). */
int ico_difficulty_row_step(IcoDifficulty *d, IcoDiffRow row, int dir);

/* --- the active record ------------------------------------------------------ */

/* New Game confirm (before la_host_new_game_go): the draft's rows (made
   valid for the disc), mode = their classification, origin NEW, a new
   journey id, every count and flag 0, no run slot; applied (variants'
   debug switches) and logged. */
void ico_difficulty_begin(const IcoDifficulty *draft);
/* The title (inside ico_opt_mirror_reset): Normal, no journey.  Logs and
   applies only when the record changes. */
void ico_difficulty_reset(void);
/* The active record's value of a row (Normal's until one is set); 0 past
   the end. */
int ico_difficulty_row(IcoDiffRow row);
/* The active record (never NULL). */
const IcoDifficulty *ico_difficulty_active(void);
/* Replaces the active record (a save's, a state's): rows made valid for the
   disc, applied, logged, the listener told. */
void ico_difficulty_set_active(const IcoDifficulty *d);
/* Sets one row of the active record (the Settings rows still stepping the
   moved options until the New Game screen owns them, the tests): no change,
   no line; else the row, the mode classified again (origin and journey
   kept), one log line.  Does not run ico_variants_apply (ico_variant_set
   does, for the rows that hold a debug switch). */
void ico_difficulty_set_row(IcoDiffRow row, int value);
/* The run's card slot (difficulty_file.c at a save or a load). */
void ico_difficulty_set_run(int port, int slot, uint32_t sum);
/* A new journey id (two random words, never both 0; not the game's rand(),
   whose state is the simulation's). */
void ico_difficulty_new_journey(uint32_t out[2]);
/* The journey id as 16 lowercase hex digits ("0000000000000000" for none);
   out holds at least 17 bytes. */
void ico_difficulty_journey_text(const IcoDifficulty *d, char *out, size_t size);
/* 1 when the active record has a journey (not the title's Normal). */
int ico_difficulty_has_journey(void);

/* --- the rows as text ------------------------------------------------------- */

/* "shadow_speed=faster shade_toughness=third_journey ... knock_limit=3
   knock_scope=room permadeath=false": every row in IcoDiffRow order, as the
   sidecar's slot_N_rows keeps them.  The words: the switches true/false;
   shadow_speed slower/original/faster/fastest; shade_toughness and
   aggression the [variants] words; barrier_hits disc/5/7; pit_time
   original or the seconds; knock_limit 0..9 (0 off); knock_scope
   room/journey.  Returns the length (snprintf's: past size when cut). */
#define ICO_DIFF_ROWS_TEXT_MAX 640
int ico_difficulty_rows_format(const IcoDifficulty *d, char *out, size_t size);
/* Reads such a text into out's rows, starting from Normal's (a row the text
   does not name is Normal's; the other fields of out are left as they are).
   Keys and values as _format writes them; the earlier words of pit_time
   (ico_opt_pit_parse) read too.  Returns the number of words that were not
   a key=value it knows (each logged, the first 8): 0 for a clean text. */
int ico_difficulty_rows_parse(const char *text, IcoDifficulty *out);

/* --- old saves and old states ----------------------------------------------- */

/* The one-time carry-over (a save with no record): the fifteen retired keys
   as the config gives them now (ico_config_get_string; unset is the game's
   default, a word that is no value its default with a log line, as the old
   readers did), the new rows at Normal; mode Normal when every row is
   Normal's, else Custom; origin CONFIG; a new journey id; counts 0.  Logs
   "difficulty: carried over from config.toml: <mode> (...)". */
void ico_difficulty_from_config(IcoDifficulty *out);
/* The same from a save state's keys text ("key=value\n" lines, the header
   of a state made before records existed: state_keys may be NULL); origin
   STATE; the journey id a hash of the fifteen keys' text (the same state
   gives the same id every load).  Never reads config. */
void ico_difficulty_from_keys(const char *state_keys, IcoDifficulty *out);

/* --- the sidecar: difficulty_file.c (difficulty.toml beside achievements.toml) --
   Keyed by the disc's key prefix + the card ("" for port 0, "b_" for port 1) +
   slot_N (docs: design-difficulty.md "Sidecar").  Every call is safe before
   ico_difficulty_file_init (the per-user folder is then used). */

/* The folder difficulty.toml lives in (NULL: the per-user folder, beside
   achievements.toml); tests pass their own.  Forgets what was read. */
void ico_difficulty_file_init(const char *dir);
/* A save was written (layout_action.c, beside ico_ach_slot_saved): the active
   record under (port, slot) with the game block's checksum and the product
   file's preview (stage, play frames, couch: known before a load). */
void ico_difficulty_slot_saved(int port, int slot, uint32_t sum, int stage, uint32_t play,
                               int sofa);
/* A save was read (layout_action.c, beside ico_ach_slot_loaded): the
   matching entry's record becomes the active one; with none, the one-time
   carry-over (ico_difficulty_from_config) is made the active record and
   written at once with origin config. */
void ico_difficulty_slot_loaded(int port, int slot, uint32_t sum);
/* 1 when the save at (port, slot) with this preview belongs to a journey
   that ended (the Continue list refuses it); 0 otherwise. */
int ico_difficulty_slot_finished(int port, int slot, int stage, uint32_t play, int sofa);
/* 1 when a save with this game-block checksum belongs to an ended journey
   (the load's backstop at step 10). */
int ico_difficulty_sum_finished(uint32_t sum);
/* The active journey ended (Permadeath): its id into [finished] at once. */
void ico_difficulty_file_journey_ended(void);
/* The whole-journey count went back to 0 (the limit's game over): written
   through to the run's slot entry. */
void ico_difficulty_file_knocks_reset(void);

/* --- the knock-down limit --------------------------------------------------- */

/* Each actCommonDown copy (commonact.c), after its after-proc is set: is_boy
   (self == boyGObj), landing (mails 55/56: the landing of a fall that began
   in mode 21, the same fall going on), down_timer (Ico's get-up timer, 0 for
   another actor).  Counts when the limit is on, it is Ico, not a landing, the
   in-fall latch is clear and down_timer is 0 (a knock-down before he is back
   on his feet is the same fall); counting sets the latch.  Returns 1 when
   the count in the row's scope reaches the limit (the copy then holds Ico
   down and calls the game over), and again on every later entry until the
   game over is seen (over_pending), else 0. */
int ico_difficulty_knockdown(int is_boy, int landing, int down_timer);
/* The host poll's sample of Ico (IcoGsSnapshot): valid 0 does nothing; the
   latch clears once his act mode is not ICO_DIFF_DOWN_ACT_MODE and his
   get-up timer is 0 (back on his feet). */
void ico_difficulty_tick(int valid, int boy_act_mode, int boy_down_timer);
/* Room entry (StageManager.c): the room's count starts again. */
void ico_difficulty_stage_enter(int stage);
/* ICO_GS_EV_GAME_OVER: the limit's game over ends the pending state (the
   whole-journey count back to 0 when it caused it, written through); with
   Permadeath on, ANY game over ends the journey (finished, written at once,
   logged, the listener told). */
void ico_difficulty_game_over(void);
/* 1 once the active journey ended (the Continue twins act as Quit). */
int ico_difficulty_finished(void);

/* No Shadows (a Customize row): 1 while the active record has it on. */
int ico_no_shadows(void);

/* --- save states ------------------------------------------------------------- */

/* Registers the active record as the block "difficulty.record"
   (ICO_STATE_FRESH): 0, or -1 when the registry refused it. */
int ico_difficulty_state_register(void);
/* After a state load (state_host.c, beside ico_gs_rebaseline): with the block
   (block_absent 0) the restored record is checked (a version or a value it
   cannot hold rebuilds it from saved_keys), applied and logged; without it
   (a state of v0.11.2 or older) the record is built from the state's own
   keys text (ico_difficulty_from_keys), never from config. */
void ico_difficulty_state_loaded(int block_absent, const char *saved_keys);
/* 1 while the active record has Permadeath on: save states do not load. */
int ico_difficulty_states_locked(void);

/* --- the listener (the UI's toasts; the module does not depend on the UI) --- */

typedef enum IcoDiffEvent {
    ICO_DIFF_EV_ACTIVE = 0, /* the active record changed (a, b: 0) */
    ICO_DIFF_EV_KNOCK,      /* a counted knock-down: a = the count, b = the limit */
    ICO_DIFF_EV_LIMIT,      /* the limit is reached: a = the count, b = the limit */
    ICO_DIFF_EV_ENDED,      /* a Permadeath journey ended (a, b: 0) */
    ICO_DIFF_EV_REFUSED     /* a save of an ended journey was refused (a, b: 0) */
} IcoDiffEvent;

typedef void (*IcoDiffListener)(IcoDiffEvent ev, int a, int b);
/* One listener; NULL unregisters. */
void ico_difficulty_set_listener(IcoDiffListener fn);
/* Raises an event to the listener (difficulty_file.c: the Continue list's
   refusal and the load's backstop raise ICO_DIFF_EV_REFUSED). */
void ico_difficulty_notify(IcoDiffEvent ev, int a, int b);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_GAME_DIFFICULTY_H */
