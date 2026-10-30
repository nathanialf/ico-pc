/*
 * port/game/achievements.h
 *
 * The port's built-in achievements.  A data table of achievements whose
 * conditions read the game-state view (port/include/ico_gamestate.h), checked once per
 * Main tick; unlocks persist in <pref>/achievements.toml and show a popup
 * (port/ui/popup.h) unless [game] achievements = false.
 *
 * Achievements are suspended while developer mode or [dev] start_stage is on,
 * and for the rest of a run in which either was (ico_gs_run_suspended): no
 * counter advances and nothing unlocks.  There is one kind of unlock.
 */
#ifndef ICO_PORT_GAME_ACHIEVEMENTS_H
#define ICO_PORT_GAME_ACHIEVEMENTS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum IcoAchState { ICO_ACH_LOCKED = 0, ICO_ACH_UNLOCKED = 1 } IcoAchState;

/* Main ticks between two popups, 5 s in the video mode in force
   (5 * ico_gs_tick_hz: 125 at the European disc's 25 Hz Main tick, 150 at
   30 Hz, the Japanese disc's and the European one's 60 Hz mode); a popup
   shows for 230 vsyncs, 115 ticks at 25 Hz. Further unlocks wait in a
   queue. */
int ico_ach_popup_gap_ticks(void);
/* Main ticks between two writes of changed counters (30 s); an unlock
   writes at once */
#define ICO_ACH_STATS_FLUSH_TICKS 750
/* Seconds between two writes while the only change is the hand-held time
   (it changes every tick Yorda is led, and the file is written in the
   background, so the long period spares the drive); any other change
   writes at ICO_ACH_STATS_FLUSH_TICKS. */
#define ICO_ACH_HAND_FLUSH_SECONDS 60
/* The ticks the counters wait now: ICO_ACH_STATS_FLUSH_TICKS when anything
   but the hand time changed, else ICO_ACH_HAND_FLUSH_SECONDS at the Main
   tick rate. */
int ico_ach_flush_due_ticks(void);

/* Reads [game] achievements and the unlock file (NULL: <pref dir>/
   achievements.toml).  ico_ach_host_poll calls it on first use. */
void ico_ach_init(const char *path);
/* The host loop, once per vsync, with the Main tick count
   (ico_host_main_ticks): runs ico_ach_tick once for each new Main tick. */
void ico_ach_host_poll(unsigned int main_ticks);
/* One Main tick: ico_gs_tick, then every locked achievement's condition,
   the popup queue, and the counters' write when due. */
void ico_ach_tick(void);
/* The run state per save slot (achievements.toml, run.slot_N_* for the
   European disc's saves, run.jpn_slot_N_* for the Japanese disc's, keyed
   as options.h's [mirror] slot_N; the unlocks and the counters are one set
   for both discs, the visited stages one per disc: stats.stages,
   stats.stages_jpn).  saved: the game wrote the block of slot
   `slot` (checksum `sum`): the run is stored.  loaded: it read that block:
   the run is restored when the stored checksum matches (1), else it stays
   as ico_gs_tick left it, not fresh (0).  A New Game after a load clears
   that slot's entry. */
void ico_ach_slot_saved(int slot, unsigned int sum);
int ico_ach_slot_loaded(int slot, unsigned int sum);
/* Writes the counters if they changed, waits for the background writer to
   finish and joins it (atexit, shutdown). */
void ico_ach_flush(void);
/* For tests: the file's text as last rendered (what the writer was or will
   be handed; the text read at start before any write), NULL with no file;
   hold keeps the writer from taking a waiting text (1) or lets it (0);
   the counts of texts handed over, files written and waiting texts
   replaced by a newer one. */
const char *ico_ach_written_text(void);
void ico_ach_writer_hold(int hold);
/* waits until nothing waits to be written and the writer is idle (the file
   on disk is then the last text rendered; hold must be off); the next
   read of the file's text goes to the disk again (a test that edited it) */
void ico_ach_writer_idle(void);
void ico_ach_reread_file(void);
void ico_ach_writer_counts(unsigned int *submitted, unsigned int *written, unsigned int *replaced);

int ico_ach_count(void);
const char *ico_ach_id(int i);
int ico_ach_find(const char *id);
IcoAchState ico_ach_state(int i);
int ico_ach_hidden(int i);
/* the unlock's time (seconds since 1970) and the game's play time then */
long long ico_ach_time(int i);
unsigned int ico_ach_play_time(int i);
/* the title and description string ids (port/ui/strings.h UiStrId) */
int ico_ach_title_str(int i);
int ico_ach_desc_str(int i);

/* popups on/off ([game] achievements; unlocks are recorded either way) */
int ico_ach_popups_enabled(void);
void ico_ach_set_popups(int on);
/* unlocks waiting for their popup */
int ico_ach_pending_popups(void);

/* the persisted counters */
typedef struct IcoAchStats {
    unsigned int enemies;
    unsigned long long hand_ms;
    unsigned int saves;
    unsigned int clears;
    int sofas; /* distinct couches saved on */
} IcoAchStats;

void ico_ach_stats(IcoAchStats *out);

/* Tests: forget everything (no file is written) and use path for the next
   writes; the clock for the unlock timestamps (seconds since 1970). */
void ico_ach_reset(const char *path);
void ico_ach_set_clock(long long (*now)(void));

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_GAME_ACHIEVEMENTS_H */
