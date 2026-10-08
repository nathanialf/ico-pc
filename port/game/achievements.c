/*
 * port/game/achievements.c
 *
 * The built-in achievements (achievements.h).
 */
#include "achievements.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "diag_host.h"
#include "host_config.h"
#include "ico_gamestate.h"
#include "popup.h"
#include "strings.h"
#ifdef __ANDROID__
#include "host_loop.h"
#endif

/* --- conditions ------------------------------------------------------------
 * Each returns nonzero when its achievement's condition holds.  They read
 * only the game-state view and the counters below. */

/* stages (stageData order; the names are the game's) */
#define ST_GATE 11       /* st04a (GATE_1ST) */
#define ST_GRAVE 13      /* st18a (GRAVE) */
#define ST_WINDMILL 15   /* st09a (WINDMILL) */
#define ST_SYMMETRY_L 18 /* st04b (SYMMETRY_L) */
#define ST_WATERFALL 22  /* st02a (WATERFALL) */
#define ST_GONDOLA 25    /* st20a (GONDOLA) */
#define ST_WATERTOWER 26 /* st10r (WATERTOWER) */
#define ST_SYMMETRY_R 27 /* st05b (SYMMETRY_R) */
#define ST_CLIFF 31      /* st22a (CLIFF) */
#define ST_QUEEN 37      /* st25a (QUEEN) */
#define ST_BEACH 39      /* st27a (BEACH) */
#define STAGE_BITS 128

/* story flags */
#define GF_QUEEN_DEAD 338   /* actSt25aQueenDeadChk, st25a.c:565 */
#define GF_BEACH_SECRET 354 /* actSt27aEndChk, end.c:1061: item kind 3 held */

/* weapon kinds (weaponKind[10]) */
#define WK_SWORD 4
#define WK_QUEEN_SWORD 5
#define WK_LIGHT_A 8
#define WK_LIGHT_B 9

#define FAST_SECONDS (3u * 3600u)
#define HAND_10_MIN_MS (10ull * 60ull * 1000ull)
#define HAND_60_MIN_MS (60ull * 60ull * 1000ull)
#define SOFA_MAX 64

/* persisted counters and sets */
static IcoAchStats s_stats;
static int s_sofa[SOFA_MAX];
static unsigned long long s_visited[STAGE_BITS / 64];
static int s_stats_dirty;
static unsigned int s_stats_written_tick;
static unsigned int s_hand_rem; /* hand_ms's sub-millisecond carry, in 1/hz ms */

static int visited(const unsigned long long *set, int stage)
{
    return stage >= 0 && stage < STAGE_BITS && ((set[stage >> 6] >> (stage & 63)) & 1u);
}

static int in_stage(int stage)
{
    return ico_gs_valid() && ico_gs_stage() == stage;
}

static int c_opening(void)
{
    /* a run from a new game in which every opening part was watched to its
       end: op.c's three, st13b's conte 02 and deja.c's (DEMO_END parts 1 to
       5), none skipped with START; true from the last part's end on */
    return ico_gs_run_fresh() && !ico_gs_run_opening_skipped() &&
           ico_gs_run_opening_parts() == (1u << ICO_GS_OPENING_PARTS) - 1u;
}

static int c_hand(void)
{
    return ico_gs_yorda_held();
}

static int c_gate(void)
{
    return in_stage(ST_GATE);
}

static int c_windmill(void)
{
    return in_stage(ST_WINDMILL);
}

static int c_grave(void)
{
    return in_stage(ST_GRAVE);
}

static int c_waterfall(void)
{
    return in_stage(ST_WATERFALL);
}

static int c_gondola(void)
{
    return in_stage(ST_GONDOLA);
}

static int c_watertower(void)
{
    return in_stage(ST_WATERTOWER);
}

static int c_cliff(void)
{
    return in_stage(ST_CLIFF);
}

static int c_wings(void)
{
    return visited(s_visited, ST_SYMMETRY_L) && visited(s_visited, ST_SYMMETRY_R);
}

static int c_queen(void)
{
    return in_stage(ST_QUEEN);
}

static int c_queen_defeated(void)
{
    return ico_gs_flag(GF_QUEEN_DEAD);
}

static int c_beach(void)
{
    return in_stage(ST_BEACH);
}

static int c_clear(void)
{
    return ico_gs_signaled(ICO_GS_EV_ENDING) != 0;
}

static int c_clear2(void)
{
    /* the ending of a New Game+ journey, from a cleared save or chosen on the
       New Game screen (gFlagGameClear 1 as the ending starts; actEndingSave
       only saves when it is 0) */
    return ico_gs_signaled(ICO_GS_EV_ENDING) && ico_gs_signal_arg(ICO_GS_EV_ENDING) != 0;
}

static int c_first_kill(void)
{
    return s_stats.enemies >= 1;
}

static int c_kills_25(void)
{
    return s_stats.enemies >= 25;
}

static int c_kills_100(void)
{
    return s_stats.enemies >= 100;
}

static int c_rescue(void)
{
    return ico_gs_signaled(ICO_GS_EV_YORDA_RESCUED) != 0;
}

static int c_hand_10(void)
{
    return s_stats.hand_ms >= HAND_10_MIN_MS;
}

static int c_hand_60(void)
{
    return s_stats.hand_ms >= HAND_60_MIN_MS;
}

static int c_first_save(void)
{
    return ico_gs_signaled(ICO_GS_EV_SAVE_DONE) != 0;
}

static int c_sofas_5(void)
{
    return s_stats.sofas >= 5;
}

static int weapon_is(int k)
{
    return ico_gs_weapon_kind() == k ||
           (ico_gs_signaled(ICO_GS_EV_WEAPON) && ico_gs_signal_arg(ICO_GS_EV_WEAPON) == k);
}

static int c_sword(void)
{
    return weapon_is(WK_SWORD);
}

static int c_queen_sword(void)
{
    return weapon_is(WK_QUEEN_SWORD);
}

static int c_light_blade(void)
{
    return weapon_is(WK_LIGHT_A) || weapon_is(WK_LIGHT_B);
}

static int c_no_capture(void)
{
    return ico_gs_signaled(ICO_GS_EV_ENDING) && ico_gs_run_fresh() && ico_gs_run_captures() == 0;
}

static int c_no_gameover(void)
{
    return ico_gs_signaled(ICO_GS_EV_ENDING) && ico_gs_run_fresh() && ico_gs_run_game_overs() == 0;
}

static int c_fast(void)
{
    return ico_gs_signaled(ICO_GS_EV_ENDING) && ico_gs_play_seconds() < FAST_SECONDS;
}

static int c_secret(void)
{
    return ico_gs_flag(GF_BEACH_SECRET);
}

/* --- the table ------------------------------------------------------------ */

typedef struct AchDef {
    const char *id; /* the key in achievements.toml; never renamed */
    UiStrId title;
    UiStrId desc;
    int (*check)(void);
    int hidden; /* not listed before it is unlocked (a future list view) */
} AchDef;

#define ACH(id, name, fn, hidden) {id, UI_STR_ACH_##name, UI_STR_ACH_##name##_DESC, fn, hidden}

static const AchDef s_defs[] = {
    /* story */
    ACH("opening", OPENING, c_opening, 0),
    ACH("hand_in_hand", HAND, c_hand, 0),
    ACH("gate", GATE, c_gate, 0),
    ACH("windmill", WINDMILL, c_windmill, 0),
    ACH("graveyard", GRAVE, c_grave, 0),
    ACH("waterfall", WATERFALL, c_waterfall, 0),
    ACH("gondola", GONDOLA, c_gondola, 0),
    ACH("water_tower", WATERTOWER, c_watertower, 0),
    ACH("cliff", CLIFF, c_cliff, 0),
    ACH("east_and_west", WINGS, c_wings, 0),
    ACH("queen", QUEEN, c_queen, 0),
    ACH("queen_defeated", QUEEN_DEFEATED, c_queen_defeated, 0),
    ACH("shore", BEACH, c_beach, 0),
    ACH("finish", CLEAR, c_clear, 0),
    ACH("finish_again", CLEAR2, c_clear2, 0),
    /* shadows and Yorda */
    ACH("first_shadow", FIRST_KILL, c_first_kill, 0),
    ACH("shadows_25", KILLS_25, c_kills_25, 0),
    ACH("shadows_100", KILLS_100, c_kills_100, 0),
    ACH("rescue", RESCUE, c_rescue, 0),
    ACH("hand_10_minutes", HAND_10, c_hand_10, 0),
    ACH("hand_60_minutes", HAND_60, c_hand_60, 0),
    /* couches */
    ACH("first_save", FIRST_SAVE, c_first_save, 0),
    ACH("couches_5", SOFAS_5, c_sofas_5, 0),
    /* weapons */
    ACH("sword", SWORD, c_sword, 0),
    ACH("queen_sword", QUEEN_SWORD, c_queen_sword, 0),
    ACH("light_blade", LIGHT_BLADE, c_light_blade, 1),
    /* challenges */
    ACH("never_taken", NO_CAPTURE, c_no_capture, 0),
    ACH("unbroken", NO_GAMEOVER, c_no_gameover, 0),
    ACH("swift", FAST, c_fast, 0),
    ACH("shore_secret", SECRET, c_secret, 1),
};

#define ACH_COUNT ((int)(sizeof(s_defs) / sizeof(s_defs[0])))

/* --- state ------------------------------------------------------------------ */

typedef struct AchRec {
    int unlocked;
    long long time;         /* seconds since 1970 */
    unsigned int play_secs; /* the game's play time at the unlock */
} AchRec;

static AchRec s_rec[ACH_COUNT];
static int s_inited;
static char s_path[1024];
static int s_popups = 1;
static unsigned int s_last_main_tick;
static long long (*s_clock)(void);

/* the popup queue: indices into s_defs */
#define POPUP_QUEUE 64
static int s_popq[POPUP_QUEUE];
static int s_popq_n;
static unsigned int s_last_push_tick;
static int s_pushed_any;

static long long now_seconds(void)
{
    return s_clock != NULL ? s_clock() : (long long)time(NULL);
}

void ico_ach_set_clock(long long (*now)(void))
{
    s_clock = now;
}

static void fmt_time(long long t, char *out, size_t size)
{
    time_t tt = (time_t)t;
    struct tm *tm = gmtime(&tt);

    if (tm == NULL) {
        snprintf(out, size, "%lld", t);
        return;
    }
    strftime(out, size, "%Y-%m-%dT%H:%M:%SZ", tm);
}

static long long parse_time(const char *s)
{
    int y, mo, d, h, mi, se;
    long long days;

    /* the widths keep sscanf in range, the checks keep the arithmetic
       below from overflowing on a hand-edited file: 0 (an unknown time)
       for anything else, the unlock itself stays */
    if (s == NULL || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6 ||
        mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 ||
        se > 60) {
        return 0;
    }
    /* days from civil (proleptic Gregorian), UTC */
    y -= mo <= 2;
    {
        long long era = (y >= 0 ? y : y - 399) / 400;
        long long yoe = y - era * 400;
        long long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

        days = era * 146097 + doe - 719468;
    }
    return days * 86400 + h * 3600 + mi * 60 + se;
}

/* --- the file --------------------------------------------------------------
 *   version = 2
 *   [stats]  enemies, hand_ms, saves, clears, couches ("id,id,..."),
 *            stages (128-bit hex)
 *   [unlocked.<id>]  time = "2026-10-05T12:00:00Z", play_time = <seconds>. Unknown keys are ignored.
 */

static void sofa_list(const int *set, int n, char *out, size_t size)
{
    int i;
    size_t len = 0;

    out[0] = '\0';
    for (i = 0; i < n && len + 16 < size; i++) {
        len += (size_t)snprintf(out + len, size - len, i ? ",%d" : "%d", set[i]);
    }
}

static int sofa_parse(const char *s, int *set)
{
    int n = 0;

    while (s != NULL && *s != '\0' && n < SOFA_MAX) {
        char *end;
        long v = strtol(s, &end, 10);

        if (end == s) {
            break;
        }
        set[n++] = (int)v;
        s = *end == ',' ? end + 1 : end;
    }
    return n;
}

static void hex_set(const unsigned long long *set, char *out, size_t size)
{
    snprintf(out, size, "%016llx%016llx", set[1], set[0]);
}

static void hex_parse(const char *s, unsigned long long *set)
{
    char hi[17], lo[17];

    set[0] = set[1] = 0;
    if (s == NULL || strlen(s) != 32) {
        return;
    }
    memcpy(hi, s, 16);
    hi[16] = '\0';
    memcpy(lo, s + 16, 16);
    lo[16] = '\0';
    set[1] = strtoull(hi, NULL, 16);
    set[0] = strtoull(lo, NULL, 16);
}

#define FILE_VERSION 2

/* stats.<name>, def when absent or not a number; a negative or too large
   value from a hand-edited file is not wrapped into a huge count (which
   would unlock the counting achievements) but read as def */
static long long get_counter(const IcoToml *t, const char *name, long long def, long long max)
{
    char key[64];
    long long v;

    snprintf(key, sizeof(key), "stats.%s", name);
    v = ico_toml_get_int(t, key, def);
    return v < 0 || v > max ? def : v;
}

static const char *get_counter_str(const IcoToml *t, const char *name)
{
    char key[64];

    snprintf(key, sizeof(key), "stats.%s", name);
    return ico_toml_get(t, key);
}

static int write_file(void)
{
    IcoToml *t = ico_toml_load(s_path);
    char buf[SOFA_MAX * 12 + 8];
    char key[160];
    int i;
    int rc;

    if (t == NULL) {
        t = ico_toml_parse("");
    }
    if (t == NULL) {
        s_stats_dirty = 1; /* retried, as a failed write below */
        return -1;
    }
    ico_toml_set_int(t, "version", FILE_VERSION);
    ico_toml_set_int(t, "stats.enemies", s_stats.enemies);
    ico_toml_set_int(t, "stats.hand_ms", (long long)s_stats.hand_ms);
    ico_toml_set_int(t, "stats.saves", s_stats.saves);
    ico_toml_set_int(t, "stats.clears", s_stats.clears);
    sofa_list(s_sofa, s_stats.sofas, buf, sizeof(buf));
    ico_toml_set_string(t, "stats.couches", buf);
    hex_set(s_visited, buf, sizeof(buf));
    ico_toml_set_string(t, "stats.stages", buf);
    for (i = 0; i < ACH_COUNT; i++) {
        if (!s_rec[i].unlocked) {
            continue;
        }
        fmt_time(s_rec[i].time, buf, sizeof(buf));
        snprintf(key, sizeof(key), "unlocked.%s.time", s_defs[i].id);
        ico_toml_set_string(t, key, buf);
        snprintf(key, sizeof(key), "unlocked.%s.play_time", s_defs[i].id);
        ico_toml_set_int(t, key, s_rec[i].play_secs);
    }
    rc = ico_toml_save(t, s_path);
    ico_toml_free(t);
    if (rc != 0) {
        ico_diag_log("achievements: cannot write %s", s_path);
    }
    /* a failed write stays pending: the flush retries it every
       ICO_ACH_STATS_FLUSH_TICKS and at exit, so an unlock or a counter is
       not lost to one failure (a locked file, a full disk) */
    s_stats_dirty = rc != 0;
    s_stats_written_tick = ico_gs_ticks();
    return rc;
}

static void read_file(void)
{
    IcoToml *t = ico_toml_load(s_path);
    char key[160];
    int i;

    if (t == NULL) {
        return;
    }
    s_stats.enemies = (unsigned int)get_counter(t, "enemies", 0, 0xFFFFFFFFLL);
    s_stats.hand_ms = (unsigned long long)get_counter(t, "hand_ms", 0, 0x7FFFFFFFFFFFFFFFLL);
    s_stats.saves = (unsigned int)get_counter(t, "saves", 0, 0xFFFFFFFFLL);
    s_stats.clears = (unsigned int)get_counter(t, "clears", 0, 0xFFFFFFFFLL);
    s_stats.sofas = sofa_parse(get_counter_str(t, "couches"), s_sofa);
    hex_parse(get_counter_str(t, "stages"), s_visited);
    for (i = 0; i < ACH_COUNT; i++) {
        snprintf(key, sizeof(key), "unlocked.%s.time", s_defs[i].id);
        /* an unlock is a record with a time */
        if (!ico_toml_has(t, key)) {
            continue;
        }
        s_rec[i].unlocked = 1;
        s_rec[i].time = parse_time(ico_toml_get(t, key));
        snprintf(key, sizeof(key), "unlocked.%s.play_time", s_defs[i].id);
        s_rec[i].play_secs = (unsigned int)ico_toml_get_int(t, key, 0);
    }
    ico_toml_free(t);
}

/* --- run state per save slot ----------------------------------------------------- */

/* run.slot_N_*: the run (ico_gs_run_get) as the save in slot N (the game's
   file number) left it, with run.slot_N_sum the save block's checksum, as
   options.c keeps [mirror] slot_N.  A load whose checksum differs finds no
   entry.  A sum of -1 marks a cleared entry. */
#define RUN_NO_SUM (-1LL)
#define RUN_SKIPPED_BIT 0x100

static int s_run_slot = -1; /* the slot the run was last loaded from or saved to */

static int run_key(int slot, const char *name, char *key, size_t size)
{
    if (slot < 0 || slot > 99) {
        return -1;
    }
    snprintf(key, size, "run.slot_%d%s", slot, name);
    return 0;
}

static void run_slot_write(int slot, const IcoGsRun *run, long long sum)
{
    IcoToml *t = ico_toml_load(s_path);
    char key[64];
    int rc;

    if (t == NULL) {
        t = ico_toml_parse("");
    }
    if (t == NULL) {
        return;
    }
    ico_toml_set_int(t, "version", FILE_VERSION);
    run_key(slot, "_sum", key, sizeof(key));
    ico_toml_set_int(t, key, sum);
    if (run != NULL) {
        run_key(slot, "_fresh", key, sizeof(key));
        ico_toml_set_bool(t, key, run->fresh);
        run_key(slot, "_captures", key, sizeof(key));
        ico_toml_set_int(t, key, run->captures);
        run_key(slot, "_game_overs", key, sizeof(key));
        ico_toml_set_int(t, key, run->game_overs);
        run_key(slot, "_opening", key, sizeof(key));
        ico_toml_set_int(
            t, key, (long long)run->opening_parts | (run->opening_skipped ? RUN_SKIPPED_BIT : 0));
        run_key(slot, "_suspended", key, sizeof(key));
        ico_toml_set_bool(t, key, run->suspended);
        run_key(slot, "_saves", key, sizeof(key));
        ico_toml_set_int(t, key, run->saves);
        run_key(slot, "_enemies", key, sizeof(key));
        ico_toml_set_int(t, key, run->enemies);
        run_key(slot, "_partial", key, sizeof(key));
        ico_toml_set_bool(t, key, run->partial);
    }
    rc = ico_toml_save(t, s_path);
    ico_toml_free(t);
    if (rc != 0) {
        ico_diag_log("achievements: cannot write %s", s_path);
    }
}

void ico_ach_slot_saved(int slot, unsigned int sum)
{
    IcoGsRun run;

    if (slot < 0 || slot > 99) {
        return;
    }
    if (!s_inited) {
        ico_ach_init(NULL);
    }
    ico_gs_run_get(&run);
    /* this save: la_save_processing calls here before it shows "File
       saved." (layout 41), and the tick counts the save when that layout
       comes up, after the slot is written; a load of this slot then finds
       the count the run has once the save is done */
    run.saves++;
    run_slot_write(slot, &run, (long long)sum);
    s_run_slot = slot;
}

int ico_ach_slot_loaded(int slot, unsigned int sum)
{
    IcoToml *t;
    IcoGsRun run;
    char key[64];
    long long v;

    s_run_slot = -1;
    if (slot < 0 || slot > 99) {
        return 0;
    }
    if (!s_inited) {
        ico_ach_init(NULL);
    }
    s_run_slot = slot; /* a New Game from this slot's load clears its entry */
    t = ico_toml_load(s_path);
    if (t == NULL) {
        return 0;
    }
    run_key(slot, "_sum", key, sizeof(key));
    if (ico_toml_get_int(t, key, RUN_NO_SUM) != (long long)sum) {
        ico_toml_free(t);
        /* nothing kept for this save: its saves and enemies are not known */
        ico_gs_run_get(&run);
        run.partial = 1;
        ico_gs_run_set(&run);
        return 0;
    }
    memset(&run, 0, sizeof(run));
    run_key(slot, "_fresh", key, sizeof(key));
    run.fresh = ico_toml_get_bool(t, key, 0) != 0;
    run_key(slot, "_captures", key, sizeof(key));
    v = ico_toml_get_int(t, key, 0);
    run.captures = v < 0 || v > 0xFFFFFFFFLL ? 0u : (unsigned int)v;
    run_key(slot, "_game_overs", key, sizeof(key));
    v = ico_toml_get_int(t, key, 0);
    run.game_overs = v < 0 || v > 0xFFFFFFFFLL ? 0u : (unsigned int)v;
    run_key(slot, "_opening", key, sizeof(key));
    v = ico_toml_get_int(t, key, 0);
    if (v < 0) {
        v = 0;
    }
    run.opening_parts = (unsigned int)(v & 0xFF);
    run.opening_skipped = (v & RUN_SKIPPED_BIT) != 0;
    run_key(slot, "_suspended", key, sizeof(key));
    run.suspended = ico_toml_get_bool(t, key, 0) != 0;
    run_key(slot, "_saves", key, sizeof(key));
    v = ico_toml_get_int(t, key, 0);
    run.saves = v < 0 || v > 0xFFFFFFFFLL ? 0u : (unsigned int)v;
    run_key(slot, "_enemies", key, sizeof(key));
    v = ico_toml_get_int(t, key, 0);
    run.enemies = v < 0 || v > 0xFFFFFFFFLL ? 0u : (unsigned int)v;
    /* a slot saved before v0.4.0 has no saves or enemies keys: those two
       count from this load only, and stay marked so through later saves
       until a New Game starts a journey counted from its start */
    run_key(slot, "_partial", key, sizeof(key));
    run.partial = ico_toml_get_bool(t, key, 0) != 0;
    run_key(slot, "_saves", key, sizeof(key));
    if (!ico_toml_has(t, key)) {
        run.partial = 1;
    }
    run_key(slot, "_enemies", key, sizeof(key));
    if (!ico_toml_has(t, key)) {
        run.partial = 1;
    }
    ico_toml_free(t);
    ico_gs_run_set(&run);
    return 1;
}

/* New Game on a slot that was loaded (a cleared save's New Game) ends that
   slot's run: its entry is cleared, the next save there writes the new one */
static void run_slot_clear(void)
{
    if (s_run_slot >= 0) {
        run_slot_write(s_run_slot, NULL, RUN_NO_SUM);
        s_run_slot = -1;
    }
}

/* --- init and reset ---------------------------------------------------------- */

static void clear_state(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_rec, 0, sizeof(s_rec));
    memset(s_visited, 0, sizeof(s_visited));
    s_stats_dirty = 0;
    s_stats_written_tick = 0;
    s_hand_rem = 0;
    s_popq_n = 0;
    s_last_push_tick = 0;
    s_pushed_any = 0;
    s_last_main_tick = 0;
}

void ico_ach_reset(const char *path)
{
    clear_state();
    ico_gs_reset();
    s_run_slot = -1;
    snprintf(s_path, sizeof(s_path), "%s", path != NULL ? path : "achievements.toml");
    s_popups = 1;
    s_inited = 1;
}

void ico_ach_flush(void)
{
    if (s_inited && s_stats_dirty) {
        write_file();
    }
}

void ico_ach_init(const char *path)
{
    int i, n = 0;

    clear_state();
    if (path != NULL) {
        snprintf(s_path, sizeof(s_path), "%s", path);
    } else {
        char dir[900];

        ico_host_pref_dir(dir, sizeof(dir));
        snprintf(s_path, sizeof(s_path), "%s/achievements.toml", dir);
    }
    s_popups = ico_config_get_bool("game.achievements", 1) != 0;
    read_file();
    for (i = 0; i < ACH_COUNT; i++) {
        n += s_rec[i].unlocked;
    }
    ico_diag_log("achievements: %d of %d unlocked, popups %s, %s", n, ACH_COUNT,
                 s_popups ? "on" : "off", s_path);
    if (!s_inited) {
#ifdef __ANDROID__
        /* Android: Quit returns from ico_host_main, which runs this */
        ico_host_at_shutdown(ico_ach_flush);
#else
        atexit(ico_ach_flush);
#endif
    }
    s_inited = 1;
}

/* --- per tick ------------------------------------------------------------------- */

static void add_sofa(int *set, int *n, int id)
{
    int i;

    for (i = 0; i < *n; i++) {
        if (set[i] == id) {
            return;
        }
    }
    if (*n < SOFA_MAX) {
        set[(*n)++] = id;
    }
}

static void mark_stage(unsigned long long *set, int stage)
{
    if (stage >= 0 && stage < STAGE_BITS) {
        set[stage >> 6] |= 1ull << (stage & 63);
    }
}

static void update_stats(void)
{
    int kills = ico_gs_signaled(ICO_GS_EV_ENEMY_KILLED);
    int saves = ico_gs_signaled(ICO_GS_EV_SAVE_DONE);
    int endings = ico_gs_signaled(ICO_GS_EV_ENDING);

    if (kills) {
        s_stats.enemies += (unsigned int)kills;
        s_stats_dirty = 1;
    }
    if (ico_gs_yorda_held() && !ico_gs_paused()) {
        /* a tick's milliseconds, the remainder carried: 30 ticks a second
           (60 Hz) count 33, 33, 34, not 33 every tick (1 % short) */
        int hz = ico_gs_tick_hz();
        unsigned int n = hz > 0 ? (unsigned int)hz : 25u;
        unsigned int ms = (1000u + s_hand_rem) / n;

        s_hand_rem = (1000u + s_hand_rem) % n;
        s_stats.hand_ms += ms;
        s_stats_dirty = 1;
    }
    if (saves) {
        int sofa = ico_gs_signal_arg(ICO_GS_EV_SAVE_DONE);

        s_stats.saves += (unsigned int)saves;
        if (sofa >= 0) {
            add_sofa(s_sofa, &s_stats.sofas, sofa);
        }
        s_stats_dirty = 1;
    }
    if (endings) {
        s_stats.clears += (unsigned int)endings;
        s_stats_dirty = 1;
    }
    if (ico_gs_valid()) {
        int st = ico_gs_stage();

        if (!visited(s_visited, st)) {
            mark_stage(s_visited, st);
            s_stats_dirty = 1;
        }
    }
}

static void queue_popup(int i)
{
    int k;

    for (k = 0; k < s_popq_n; k++) {
        if (s_popq[k] == i) {
            return;
        }
    }
    if (s_popq_n < POPUP_QUEUE) {
        s_popq[s_popq_n++] = i;
    }
}

static void unlock(int i)
{
    s_rec[i].unlocked = 1;
    s_rec[i].time = now_seconds();
    s_rec[i].play_secs = ico_gs_play_seconds();
    ico_diag_log("achievements: unlocked \"%s\" at Main tick %u, stage %d %s, play time %us",
                 s_defs[i].id, ico_gs_ticks(), ico_gs_stage(), ico_gs_stage_name(),
                 s_rec[i].play_secs);
    write_file();
    if (s_popups) {
        queue_popup(i);
    }
}

/* Breaks text into lines of at most POPUP_LINE code points at spaces (the
   popup does not wrap). */
#define POPUP_LINE 44

static void wrap(const char *in, char *out, size_t size)
{
    size_t o = 0;
    size_t last_space = (size_t)-1;
    int col = 0;

    for (; *in != '\0' && o + 1 < size; in++) {
        unsigned char c = (unsigned char)*in;

        out[o] = (char)c;
        if (c == '\n') {
            col = 0;
            last_space = (size_t)-1;
        } else if ((c & 0xC0) != 0x80) {
            if (c == ' ') {
                last_space = o;
            }
            if (++col > POPUP_LINE && last_space != (size_t)-1) {
                size_t k;

                out[last_space] = '\n';
                col = 0;
                for (k = last_space + 1; k <= o; k++) {
                    col += ((unsigned char)out[k] & 0xC0) != 0x80;
                }
                last_space = (size_t)-1;
            }
        }
        o++;
    }
    out[o] = '\0';
}

static void pump_popups(void)
{
    unsigned int now = ico_gs_ticks();
    char text[UI_POPUP_TEXT];
    int i;

    if (s_popq_n == 0) {
        return;
    }
    if (s_pushed_any && now - s_last_push_tick < ICO_ACH_POPUP_GAP_TICKS) {
        return;
    }
    i = s_popq[0];
    wrap(ui_Str(s_defs[i].desc), text, sizeof(text));
    if (ui_PopupPush(ui_Str(s_defs[i].title), text) != 0) {
        return; /* the popup queue is full: try again next tick */
    }
    memmove(s_popq, s_popq + 1, (size_t)(s_popq_n - 1) * sizeof(s_popq[0]));
    s_popq_n--;
    s_last_push_tick = now;
    s_pushed_any = 1;
}

void ico_ach_tick(void)
{
    int i;

    if (!s_inited) {
        ico_ach_init(NULL);
    }
    ico_gs_tick();
    if (!ico_gs_valid()) {
        return;
    }
    if (ico_gs_signaled(ICO_GS_EV_NEW_GAME)) {
        run_slot_clear();
    }
    /* suspended (developer mode or start_stage, now or earlier in this
       run): no counter advances and nothing unlocks; the popup queue and
       the file's write still run */
    if (!ico_gs_achievements_suspended() && !ico_gs_run_suspended()) {
        update_stats();
        for (i = 0; i < ACH_COUNT; i++) {
            if (!s_rec[i].unlocked && s_defs[i].check()) {
                unlock(i);
            }
        }
    }
    pump_popups();
    if (s_stats_dirty && ico_gs_ticks() - s_stats_written_tick >= ICO_ACH_STATS_FLUSH_TICKS) {
        write_file();
    }
}

void ico_ach_host_poll(unsigned int main_ticks)
{
    if (!s_inited) {
        ico_ach_init(NULL);
        s_last_main_tick = main_ticks;
    }
    /* Main ticks at most once per vsync; a backlog runs one check per tick */
    while (s_last_main_tick != main_ticks) {
        s_last_main_tick++;
        ico_ach_tick();
    }
}

/* --- queries ------------------------------------------------------------------- */

int ico_ach_count(void)
{
    return ACH_COUNT;
}

const char *ico_ach_id(int i)
{
    return i >= 0 && i < ACH_COUNT ? s_defs[i].id : "";
}

int ico_ach_find(const char *id)
{
    int i;

    for (i = 0; i < ACH_COUNT; i++) {
        if (strcmp(s_defs[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

IcoAchState ico_ach_state(int i)
{
    return i >= 0 && i < ACH_COUNT && s_rec[i].unlocked ? ICO_ACH_UNLOCKED : ICO_ACH_LOCKED;
}

int ico_ach_hidden(int i)
{
    return i >= 0 && i < ACH_COUNT ? s_defs[i].hidden : 0;
}

long long ico_ach_time(int i)
{
    return i >= 0 && i < ACH_COUNT ? s_rec[i].time : 0;
}

unsigned int ico_ach_play_time(int i)
{
    return i >= 0 && i < ACH_COUNT ? s_rec[i].play_secs : 0u;
}

int ico_ach_title_str(int i)
{
    return i >= 0 && i < ACH_COUNT ? (int)s_defs[i].title : 0;
}

int ico_ach_desc_str(int i)
{
    return i >= 0 && i < ACH_COUNT ? (int)s_defs[i].desc : 0;
}

int ico_ach_popups_enabled(void)
{
    return s_popups;
}

void ico_ach_set_popups(int on)
{
    s_popups = on != 0;
    if (!s_popups) {
        s_popq_n = 0;
    }
}

int ico_ach_pending_popups(void)
{
    return s_popq_n;
}

void ico_ach_stats(IcoAchStats *out)
{
    *out = s_stats;
}
