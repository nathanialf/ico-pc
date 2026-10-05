/*
 * port/game/test/achievements_test.c
 *
 * The achievements on the CPU (docs/port/ACHIEVEMENTS.md): the game-state
 * view over a synthetic snapshot, the signals, every achievement's
 * condition through crafted transitions (unlocked once, suspended in developer
 * mode, yorda_safe counting, a version 1 file), the achievements.toml round trip, the
 * popup rate limit and [game] achievements = false.  argv[1]: a writable
 * folder.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"
#include "config.h"
#include "ico_gamestate.h"
#include "options.h"
#include "popup.h"
#include "strings.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- the popup queue (port/ui/popup.c is the program's) -------------------- */

static int s_pushes;
static int s_push_full; /* answer "queue full" */
static char s_last_title[UI_POPUP_TEXT], s_last_body[UI_POPUP_TEXT];
#define HISTORY 64
static char s_hist_title[HISTORY][UI_POPUP_TEXT], s_hist_body[HISTORY][UI_POPUP_TEXT];

int ui_PopupPush(const char *title, const char *body)
{
    if (s_push_full) {
        return -1;
    }
    snprintf(s_last_title, sizeof(s_last_title), "%s", title);
    snprintf(s_last_body, sizeof(s_last_body), "%s", body);
    snprintf(s_hist_title[s_pushes % HISTORY], UI_POPUP_TEXT, "%s", title);
    snprintf(s_hist_body[s_pushes % HISTORY], UI_POPUP_TEXT, "%s", body);
    s_pushes++;
    return 0;
}

/* the body of the last popup with this title, or NULL */
static const char *pushed_body(const char *title)
{
    int i;

    for (i = s_pushes - 1; i >= 0 && i >= s_pushes - HISTORY; i--) {
        if (strcmp(s_hist_title[i % HISTORY], title) == 0) {
            return s_hist_body[i % HISTORY];
        }
    }
    return NULL;
}

/* --- the synthetic game ----------------------------------------------------- */

static IcoGsSnapshot g;

static void sampler(IcoGsSnapshot *out)
{
    *out = g;
}

static void flag(int n, int on)
{
    if (on) {
        g.gflags[n >> 3] |= (unsigned char)(1u << (n & 7));
    } else {
        g.gflags[n >> 3] &= (unsigned char)~(1u << (n & 7));
    }
}

static void ticks(int n)
{
    while (n-- > 0) {
        ico_ach_tick();
    }
}

static char s_dir[512];
static char s_path[600];
static char s_no_config[600]; /* a config.toml and ini that do not exist */

static long long fixed_clock(void)
{
    return 1759665600LL; /* 2025-10-05T12:00:00Z */
}

/* a running game in a stage, PAL (systemStatus 0 = 1, 1 = 2: 25 Main ticks
   a second), playing (layout 54) */
static void fresh_world(void)
{
    memset(&g, 0, sizeof(g));
    g.valid = 1;
    g.stage_no = 2;
    g.system_status[0] = 1;
    g.system_status[1] = 2;
    g.layout = 54;
    g.actors_valid = 1;
    g.boy_present = 1;
    g.held_item = -1;
    g.stage_name = "title";
}

static void start(const char *name)
{
    snprintf(s_path, sizeof(s_path), "%s/%s.toml", s_dir, name);
    remove(s_path);
    ico_ach_reset(s_path);
    ico_ach_set_clock(fixed_clock);
    ico_gs_set_sampler(sampler);
    ico_opt_set_developer_mode(0);
    ico_opt_set_yorda_safe(0);
#ifdef _WIN32
    _putenv("ICO_START_STAGE=");
#else
    unsetenv("ICO_START_STAGE");
#endif
    s_pushes = 0;
    s_push_full = 0;
    fresh_world();
    ticks(1);
}

static IcoAchState st(const char *id)
{
    int i = ico_ach_find(id);

    if (i < 0) {
        fprintf(stderr, "no achievement %s\n", id);
        failures++;
        return ICO_ACH_LOCKED;
    }
    return ico_ach_state(i);
}

/* a new game from the title: stage 1, then gflag 382 */
static void new_game(void)
{
    g.stage_no = 1;
    flag(382, 0);
    ticks(1);
    flag(382, 1);
    ticks(1);
}

static int file_count(const char *needle)
{
    FILE *f = fopen(s_path, "rb");
    char buf[16384];
    size_t n;
    int c = 0;
    const char *p;

    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    for (p = buf; (p = strstr(p, needle)) != NULL; p++) {
        c++;
    }
    return c;
}

/* --- the view ------------------------------------------------------------------ */

static void test_view(void)
{
    unsigned int v;
    const IcoGsPeekEntry *table;
    int n;

    start("view");
    g.stage_no = 15;
    g.stage_name = "st09a (WINDMILL)";
    g.game_clear = 1;
    g.mc_preview[2] = 50 * 3725; /* frames at 50 a second: 1 h 2 min 5 s */
    g.weapon_kind = 5;
    g.hand_held = 1;
    g.girl_present = 1;
    flag(100, 1);
    ticks(1);
    CHECK(ico_gs_valid());
    CHECK(ico_gs_stage() == 15);
    CHECK(strcmp(ico_gs_stage_name(), "st09a (WINDMILL)") == 0);
    CHECK(ico_gs_stage_entered());
    CHECK(ico_gs_flag(100) && ico_gs_flag_rose(100) && !ico_gs_flag(101));
    CHECK(ico_gs_flag(-1) == 0 && ico_gs_flag(400) == 0);
    CHECK(ico_gs_game_clear() == 1);
    CHECK(ico_gs_tick_hz() == 25);
    CHECK(ico_gs_play_seconds() == 3725);
    CHECK(ico_gs_weapon_kind() == 5);
    CHECK(ico_gs_yorda_held() && ico_gs_yorda_present() && !ico_gs_yorda_captured());
    ticks(1);
    CHECK(!ico_gs_stage_entered() && !ico_gs_flag_rose(100));
    /* NTSC: 30 Main ticks a second */
    g.system_status[0] = 0;
    ticks(1);
    CHECK(ico_gs_tick_hz() == 30);
    g.system_status[0] = 1;

    /* the retail addresses */
    g.stage_no = 0x11223344;
    g.gflags[1] = 0xA5;
    g.layout = 41;
    ticks(1);
    CHECK(ico_gs_peek(0x00639D10u, 4, &v) == 0 && v == 0x11223344u);
    CHECK(ico_gs_peek(0x00639D11u, 2, &v) == 0 && v == 0x2233u);
    CHECK(ico_gs_peek(0x00639D13u, 1, &v) == 0 && v == 0x11u);
    CHECK(ico_gs_peek(0x00639D13u, 2, &v) == -1); /* runs past stage_no */
    CHECK(ico_gs_peek(0x002A50C1u, 1, &v) == 0 && v == 0xA5u);
    CHECK(ico_gs_peek(0x002A50C0u + 49, 1, &v) == 0);
    CHECK(ico_gs_peek(0x002A50C0u + 50, 1, &v) == -1);
    CHECK(ico_gs_peek(0x0063AA00u, 4, &v) == 0 && v == 1u);  /* gFlagGameClear */
    CHECK(ico_gs_peek(0x0063B60Cu, 4, &v) == 0 && v == 41u); /* current_layout_id */
    CHECK(ico_gs_peek(0x0029B9D0u + 8, 4, &v) == 0 && v == 50u * 3725u);
    CHECK(ico_gs_peek(0x00639EA4u, 4, &v) == -1); /* boyGObj: a pointer, not served */
    CHECK(ico_gs_peek(0x01000000u, 4, &v) == -1); /* heap */
    CHECK(ico_gs_peek(0x00639D10u, 3, &v) == -1);
    n = ico_gs_peek_table(&table);
    CHECK(n == 14 && table[0].addr == 0x002A50C0u && table[0].size == 50);
}

static void test_signals(void)
{
    int i;

    start("signals");
    ico_gs_signal(ICO_GS_EV_ENEMY_KILLED, 3757);
    ico_gs_signal(ICO_GS_EV_ENEMY_KILLED, 12);
    ico_gs_signal(ICO_GS_EV_NONE, 1);
    ico_gs_signal(ICO_GS_EV_COUNT, 1);
    ico_gs_signal(-3, 1);
    CHECK(ico_gs_signaled(ICO_GS_EV_ENEMY_KILLED) == 0); /* not before the tick */
    ticks(1);
    CHECK(ico_gs_signaled(ICO_GS_EV_ENEMY_KILLED) == 2);
    CHECK(ico_gs_signal_arg(ICO_GS_EV_ENEMY_KILLED) == 12);
    CHECK(ico_gs_enemies_killed() == 2);
    ticks(1);
    CHECK(ico_gs_signaled(ICO_GS_EV_ENEMY_KILLED) == 0); /* consumed in its tick */
    for (i = 0; i < 130; i++) {
        ico_gs_signal(ICO_GS_EV_CHECKPOINT, i);
    }
    ticks(1);
    CHECK(ico_gs_signaled(ICO_GS_EV_CHECKPOINT) == 128);
    CHECK(ico_gs_signals_dropped() == 2);

    /* polled: a save (layout 38 then 41), a load (25), a new game (382) */
    g.layout = 38;
    g.mc_preview[3] = 1203;
    ticks(1);
    g.layout = 41;
    ticks(1);
    CHECK(ico_gs_signaled(ICO_GS_EV_SAVE_DONE) == 1 &&
          ico_gs_signal_arg(ICO_GS_EV_SAVE_DONE) == 1203);
    ticks(1);
    CHECK(ico_gs_signaled(ICO_GS_EV_SAVE_DONE) == 0 && ico_gs_saves() == 1);
    new_game();
    CHECK(ico_gs_run_fresh());
    g.layout = 25;
    ticks(1);
    CHECK(ico_gs_signaled(ICO_GS_EV_LOAD) && !ico_gs_run_fresh());

    /* a capture (the hook, then the snapshot) and a rescue */
    new_game();
    g.stage_no = 16;
    g.girl_present = 1;
    ticks(1);
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 3757);
    g.yorda_carried_by_enemy = 1;
    ticks(1);
    CHECK(ico_gs_yorda_captured() && ico_gs_run_captures() == 1);
    ticks(3);
    g.yorda_carried_by_enemy = 0;
    ticks(1);
    CHECK(ico_gs_signaled(ICO_GS_EV_YORDA_RESCUED) == 1);
    /* one the hook missed counts too; a stage change is not a rescue */
    g.yorda_carried_by_enemy = 1;
    ticks(1);
    CHECK(ico_gs_run_captures() == 2);
    g.yorda_carried_by_enemy = 0;
    g.stage_no = 17;
    ticks(1);
    CHECK(ico_gs_signaled(ICO_GS_EV_YORDA_RESCUED) == 0);
    /* the game-over counters */
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    ticks(1);
    CHECK(ico_gs_game_overs() == 1 && ico_gs_run_game_overs() == 1);
    new_game();
    CHECK(ico_gs_run_game_overs() == 0 && ico_gs_run_captures() == 0);
    /* the options and the suspension: yorda_safe does not suspend */
    CHECK(!ico_gs_achievements_suspended());
    ico_opt_set_yorda_safe(1);
    CHECK(ico_gs_yorda_safe() && !ico_gs_achievements_suspended());
    ticks(1);
    CHECK(!ico_gs_run_suspended());
    ico_opt_set_yorda_safe(0);
    ico_opt_set_developer_mode(1);
    CHECK(ico_gs_developer_mode() && ico_gs_achievements_suspended());
    ticks(1);
    ico_opt_set_developer_mode(0);
    ticks(1);
    CHECK(!ico_gs_achievements_suspended() && ico_gs_run_suspended());
    new_game();
    CHECK(!ico_gs_run_suspended());
#ifndef _WIN32
    setenv("ICO_START_STAGE", "34", 1);
    CHECK(ico_gs_start_stage_used() && ico_gs_achievements_suspended());
    setenv("ICO_START_STAGE", "1", 1);
    CHECK(!ico_gs_start_stage_used());
    unsetenv("ICO_START_STAGE");
#endif
}

/* --- every achievement ----------------------------------------------------------- */

/* sets up, checks it is locked, runs the trigger, checks it unlocked once */
#define EXPECT_UNLOCK(id, trigger)                                                                 \
    do {                                                                                           \
        int before_;                                                                               \
        CHECK(st(id) == ICO_ACH_LOCKED);                                                           \
        trigger;                                                                                   \
        CHECK(st(id) == ICO_ACH_UNLOCKED);                                                         \
        before_ = file_count("[unlocked." id "]");                                                 \
        CHECK(before_ == 1);                                                                       \
        trigger;                                                                                   \
        ticks(2);                                                                                  \
        CHECK(file_count("[unlocked." id "]") == 1);                                               \
    } while (0)

static void stage(int n)
{
    g.stage_no = n;
    ticks(1);
}

static void kills(int n)
{
    while (n > 0) {
        int k = n > 100 ? 100 : n;
        int i;

        for (i = 0; i < k; i++) {
            ico_gs_signal(ICO_GS_EV_ENEMY_KILLED, 1000 + i);
        }
        ticks(1);
        n -= k;
    }
}

static IcoAchStats stats(void)
{
    IcoAchStats a;

    ico_ach_stats(&a);
    return a;
}

/* kills up to total - 1 in all */
static void kills_until_one_short(unsigned int total)
{
    unsigned int have = stats().enemies;

    if (have + 1 < total) {
        kills((int)(total - 1 - have));
    }
}

/* holds hands (unpaused) until one tick short of ms */
static void hand_until_one_short(unsigned long long ms)
{
    unsigned long long have = stats().hand_ms;

    g.hand_held = 1;
    if (have + 40 < ms) {
        ticks((int)((ms - have) / 40 - 1));
    }
}

static void save_on(int sofa)
{
    g.layout = 38;
    ticks(1);
    g.mc_preview[3] = sofa;
    g.layout = 41;
    ticks(1);
    g.layout = 54;
    ticks(1);
}

static void ending(int clear_before, int play_seconds)
{
    g.game_clear = clear_before;
    g.mc_preview[2] = play_seconds * 50;
    ico_gs_signal(ICO_GS_EV_ENDING, clear_before);
    ticks(1);
}

/* op.c's three parts (gflags 2..4 as they set them), then st13b */
static void opening(int skip_part)
{
    int p;

    for (p = 1; p <= 3; p++) {
        flag(p + 1, 1);
        ico_gs_signal(ICO_GS_EV_DEMO_END, p * 2 + (p == skip_part));
        stage(40 + p);
    }
    stage(3);
}

#define HAND_MS_10 (10ull * 60ull * 1000ull)
#define HAND_MS_60 (60ull * 60ull * 1000ull)

static void test_each(void)
{
    start("each");
    CHECK(ico_ach_count() >= 20 && ico_ach_count() <= 30);

    /* the opening: not when a part is skipped, nor without a new game */
    opening(0);
    CHECK(st("opening") == ICO_ACH_LOCKED);
    new_game();
    opening(2);
    CHECK(st("opening") == ICO_ACH_LOCKED);
    new_game();
    EXPECT_UNLOCK("opening", opening(0));

    g.girl_present = 1;
    EXPECT_UNLOCK("hand_in_hand", (g.hand_held = 1, ticks(1)));
    g.hand_held = 0;
    EXPECT_UNLOCK("gate", stage(11));
    EXPECT_UNLOCK("windmill", stage(15));
    EXPECT_UNLOCK("graveyard", stage(13));
    EXPECT_UNLOCK("waterfall", stage(22));
    EXPECT_UNLOCK("gondola", stage(25));
    EXPECT_UNLOCK("water_tower", stage(26));
    EXPECT_UNLOCK("cliff", stage(31));
    stage(18);
    CHECK(st("east_and_west") == ICO_ACH_LOCKED);
    EXPECT_UNLOCK("east_and_west", stage(27));
    EXPECT_UNLOCK("queen", stage(37));
    EXPECT_UNLOCK("queen_defeated", (flag(338, 1), ticks(1)));
    flag(338, 0);
    EXPECT_UNLOCK("shore", stage(39));
    EXPECT_UNLOCK("shore_secret", (flag(354, 1), ticks(1)));
    flag(354, 0);

    EXPECT_UNLOCK("first_shadow", kills(1));
    kills_until_one_short(25);
    CHECK(stats().enemies == 24);
    CHECK(st("shadows_25") == ICO_ACH_LOCKED);
    EXPECT_UNLOCK("shadows_25", kills(1));
    kills_until_one_short(100);
    CHECK(st("shadows_100") == ICO_ACH_LOCKED);
    EXPECT_UNLOCK("shadows_100", kills(1));
    CHECK(stats().enemies == 101);

    g.girl_present = 1;
    g.yorda_carried_by_enemy = 1;
    ticks(2);
    CHECK(st("rescue") == ICO_ACH_LOCKED);
    EXPECT_UNLOCK("rescue", (g.yorda_carried_by_enemy = 0, ticks(1)));

    /* hand time: 40 ms a tick at 25 Hz, paused time not counted */
    {
        unsigned long long before = stats().hand_ms;

        g.hand_held = 1;
        g.system_status[5] = 1;
        ticks(20000);
        CHECK(stats().hand_ms == before);
        g.system_status[5] = 0;
        ticks(1);
        CHECK(stats().hand_ms == before + 40);
    }
    hand_until_one_short(HAND_MS_10);
    CHECK(st("hand_10_minutes") == ICO_ACH_LOCKED);
    EXPECT_UNLOCK("hand_10_minutes", ticks(1));
    hand_until_one_short(HAND_MS_60);
    CHECK(st("hand_60_minutes") == ICO_ACH_LOCKED);
    EXPECT_UNLOCK("hand_60_minutes", ticks(1));
    g.hand_held = 0;

    EXPECT_UNLOCK("first_save", save_on(1203));
    save_on(1203); /* the same couch again */
    save_on(1450);
    save_on(-1); /* not on a couch (the ending's save) */
    save_on(1461);
    save_on(1470);
    CHECK(stats().sofas == 4);
    CHECK(st("couches_5") == ICO_ACH_LOCKED);
    EXPECT_UNLOCK("couches_5", save_on(1500));

    EXPECT_UNLOCK("sword", (g.weapon_kind = 4, ticks(1)));
    g.weapon_kind = 1;
    EXPECT_UNLOCK("queen_sword", (ico_gs_signal(ICO_GS_EV_WEAPON, 5), ticks(1)));
    EXPECT_UNLOCK("light_blade", (g.weapon_kind = 9, ticks(1)));
    g.weapon_kind = 0;
    CHECK(ico_ach_hidden(ico_ach_find("light_blade")));

    /* challenges: a run from a new game */
    new_game();
    stage(4);
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 3757);
    ticks(1);
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    ticks(1);
    ending(0, 4 * 3600);
    CHECK(st("finish") == ICO_ACH_UNLOCKED);
    CHECK(st("never_taken") == ICO_ACH_LOCKED);
    CHECK(st("unbroken") == ICO_ACH_LOCKED);
    CHECK(st("swift") == ICO_ACH_LOCKED);
    CHECK(st("finish_again") == ICO_ACH_LOCKED);
    /* a loaded game is not a run from a new game */
    new_game();
    g.layout = 25;
    ticks(1);
    g.layout = 54;
    ending(0, 3600);
    CHECK(st("never_taken") == ICO_ACH_LOCKED && st("unbroken") == ICO_ACH_LOCKED);
    CHECK(st("swift") == ICO_ACH_UNLOCKED);
    new_game();
    EXPECT_UNLOCK("never_taken", ending(0, 5 * 3600));
    CHECK(st("unbroken") == ICO_ACH_UNLOCKED);
    EXPECT_UNLOCK("finish_again", ending(1, 5 * 3600));
    /* "finish" and "unbroken" were checked by the runs above */
    CHECK(file_count("[unlocked.finish]") == 1);
    CHECK(file_count("[unlocked.unbroken]") == 1);
    CHECK(file_count("[unlocked.swift]") == 1);

    /* every achievement has strings in every language */
    {
        int i, l;

        for (i = 0; i < ico_ach_count(); i++) {
            CHECK(st(ico_ach_id(i)) == ICO_ACH_UNLOCKED); /* all unlocked above */
            for (l = 0; l < UI_LANG_COUNT; l++) {
                CHECK(ui_StrIn((UiLang)l, (UiStrId)ico_ach_title_str(i))[0] != '\0');
                CHECK(ui_StrIn((UiLang)l, (UiStrId)ico_ach_desc_str(i))[0] != '\0');
            }
        }
    }
}

/* --- suspension ------------------------------------------------------------------ */

static void test_suspended(void)
{
    start("suspended");
    /* developer mode: nothing unlocks, no counter advances, no popup */
    ico_opt_set_developer_mode(1);
    stage(15);
    kills(3);
    g.hand_held = 1;
    ticks(5);
    g.hand_held = 0;
    CHECK(st("windmill") == ICO_ACH_LOCKED && st("first_shadow") == ICO_ACH_LOCKED);
    CHECK(st("hand_in_hand") == ICO_ACH_LOCKED);
    CHECK(stats().enemies == 0 && stats().hand_ms == 0);
    CHECK(s_pushes == 0 && file_count("[unlocked.") <= 0);
    /* switched off in the same run: still suspended */
    ico_opt_set_developer_mode(0);
    stage(16);
    stage(15);
    kills(1);
    CHECK(st("windmill") == ICO_ACH_LOCKED && stats().enemies == 0);
    /* a new run without it: progress resumes and unlocks, once */
    new_game();
    stage(15);
    kills(1);
    CHECK(st("windmill") == ICO_ACH_UNLOCKED && st("first_shadow") == ICO_ACH_UNLOCKED);
    CHECK(stats().enemies == 1 && file_count("[unlocked.windmill]") == 1);
    CHECK(s_pushes == 1 && strstr(s_last_body, "Assisted") == NULL);
    /* start_stage suspends too */
#ifndef _WIN32
    new_game();
    setenv("ICO_START_STAGE", "37", 1);
    stage(37);
    kills(1);
    CHECK(st("queen") == ICO_ACH_LOCKED && stats().enemies == 1);
    unsetenv("ICO_START_STAGE");
    ticks(1);
    stage(37);
    CHECK(st("queen") == ICO_ACH_LOCKED); /* the run is still suspended */
    new_game();
    stage(37);
    CHECK(st("queen") == ICO_ACH_UNLOCKED);
#endif
}

/* yorda_safe is not an assist: its progress and unlocks count */
static void test_yorda_safe_counts(void)
{
    start("yorda_safe");
    new_game();
    ico_opt_set_yorda_safe(1);
    stage(11);
    CHECK(st("gate") == ICO_ACH_UNLOCKED);
    kills(25);
    CHECK(stats().enemies == 25 && st("shadows_25") == ICO_ACH_UNLOCKED);
    stage(18);
    stage(27);
    CHECK(st("east_and_west") == ICO_ACH_UNLOCKED);
    CHECK(file_count("[unlocked.gate]") == 1);
    CHECK(!ico_gs_run_suspended());
}

/* a version 1 file: both counters, a category on every unlock */
static void test_old_file(void)
{
    FILE *f;

    start("oldfile");
    f = fopen(s_path, "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    fputs("version = 1\n\n[stats]\nenemies_all = 9\nenemies_normal = 4\n"
          "hand_ms_all = 800\nhand_ms_normal = 400\nsaves = 2\nclears = 0\n"
          "couches_all = \"77,78\"\ncouches_normal = \"77\"\n\n"
          "[unlocked.gate]\ntime = \"2025-10-05T12:00:00Z\"\ncategory = \"normal\"\n"
          "play_time = 754\n\n"
          "[unlocked.windmill]\ntime = \"2025-10-05T12:00:00Z\"\ncategory = \"assisted\"\n"
          "play_time = 800\n",
          f);
    fclose(f);
    ico_config_reset(s_no_config, s_no_config);
    ico_ach_init(s_path);
    CHECK(st("gate") == ICO_ACH_UNLOCKED && st("windmill") == ICO_ACH_UNLOCKED);
    CHECK(st("cliff") == ICO_ACH_LOCKED);
    CHECK(ico_ach_time(ico_ach_find("windmill")) == fixed_clock());
    CHECK(ico_ach_play_time(ico_ach_find("windmill")) == 800);
    CHECK(stats().enemies == 4 && stats().hand_ms == 400 && stats().sofas == 1);
    CHECK(stats().saves == 2);
    /* the next write is a version 2 file with the new keys and no popup
       for the old unlocks */
    ico_gs_set_sampler(sampler);
    fresh_world();
    s_pushes = 0;
    stage(15);
    ticks(1);
    CHECK(pushed_body(ui_StrIn(UI_LANG_EN, UI_STR_ACH_WINDMILL)) == NULL);
    kills(1);
    ico_ach_flush();
    CHECK(file_count("version = 2") == 1 && file_count("enemies = 5") == 1);
    CHECK(file_count("[unlocked.gate]") == 1 && file_count("[unlocked.windmill]") == 1);
    CHECK(file_count("category = \"assisted\"") == 1); /* an unknown key is kept */
}

/* --- the file -------------------------------------------------------------------- */

static void test_persistence(void)
{
    IcoAchStats a, b;
    int i;
    int states[64];
    long long times[64];
    unsigned int plays[64];
    int n;

    start("persist");
    new_game();
    g.mc_preview[2] = 50 * 754;
    stage(11);
    stage(15);
    kills(3);
    save_on(77);
    g.girl_present = 1;
    g.hand_held = 1;
    ticks(10);
    ico_ach_flush();
    ico_ach_stats(&a);
    CHECK(a.enemies == 3 && a.saves == 1 && a.sofas == 1 && a.hand_ms >= 400);
    n = ico_ach_count();
    for (i = 0; i < n; i++) {
        states[i] = (int)ico_ach_state(i);
        times[i] = ico_ach_time(i);
        plays[i] = ico_ach_play_time(i);
    }
    CHECK(ico_ach_state(ico_ach_find("gate")) == ICO_ACH_UNLOCKED);
    CHECK(ico_ach_time(ico_ach_find("gate")) == fixed_clock());
    CHECK(ico_ach_play_time(ico_ach_find("gate")) == 754);
    CHECK(file_count("time = \"2025-10-05T12:00:00Z\"") >= 2);
    CHECK(file_count("category") == 0);
    CHECK(file_count("play_time = 754") >= 2);

    /* read back */
    ico_config_reset(s_no_config, s_no_config);
    ico_ach_init(s_path);
    ico_ach_stats(&b);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    for (i = 0; i < n; i++) {
        CHECK((int)ico_ach_state(i) == states[i]);
        CHECK(ico_ach_time(i) == times[i]);
        CHECK(ico_ach_play_time(i) == plays[i]);
    }
    /* unlocked ones stay unlocked: no second record, no popup */
    s_pushes = 0;
    ico_gs_set_sampler(sampler);
    g.stage_no = 11;
    ticks(2);
    CHECK(s_pushes == 0);
    CHECK(file_count("[unlocked.gate]") == 1);
    /* a missing file is an empty record */
    remove(s_path);
    ico_ach_init(s_path);
    CHECK(ico_ach_state(ico_ach_find("gate")) == ICO_ACH_LOCKED);
}

/* --- popups ---------------------------------------------------------------------- */

static void test_popups(void)
{
    char cfg[600];
    FILE *f;

    start("popups");
    s_pushes = 0;
    /* three at once: one popup now, the next after the gap */
    g.stage_no = 11;
    ticks(1);
    g.stage_no = 15;
    ticks(1);
    g.stage_no = 13;
    ticks(1);
    CHECK(s_pushes == 1);
    CHECK(strcmp(s_last_title, ui_StrIn(UI_LANG_EN, UI_STR_ACH_GATE)) == 0);
    CHECK(strcmp(s_last_body, ui_StrIn(UI_LANG_EN, UI_STR_ACH_GATE_DESC)) == 0);
    CHECK(ico_ach_pending_popups() == 2);
    ticks(ICO_ACH_POPUP_GAP_TICKS - 3);
    CHECK(s_pushes == 1);
    ticks(1);
    CHECK(s_pushes == 2);
    CHECK(strcmp(s_last_title, ui_StrIn(UI_LANG_EN, UI_STR_ACH_WINDMILL)) == 0);
    /* the UI queue full: kept and retried */
    s_push_full = 1;
    ticks(ICO_ACH_POPUP_GAP_TICKS * 2);
    CHECK(s_pushes == 2 && ico_ach_pending_popups() == 1);
    s_push_full = 0;
    ticks(1);
    CHECK(s_pushes == 3 && ico_ach_pending_popups() == 0);
    /* the language at popup time */
    ui_SetLanguage(UI_LANG_DE);
    g.stage_no = 22;
    ticks(ICO_ACH_POPUP_GAP_TICKS);
    CHECK(strcmp(s_last_title, "Der Wasserfall") == 0);
    ui_SetLanguage(UI_LANG_EN);
    /* a long description is broken into lines of at most 44 letters */
    {
        int i = ico_ach_find("never_taken");
        const char *d = ui_StrIn(UI_LANG_EN, (UiStrId)ico_ach_desc_str(i));
        const char *b;

        CHECK(strlen(d) > 44);
        new_game();
        ending(0, 9 * 3600);
        ticks(ICO_ACH_POPUP_GAP_TICKS * 4);
        b = pushed_body(ui_StrIn(UI_LANG_EN, (UiStrId)ico_ach_title_str(i)));
        CHECK(b != NULL);
        if (b != NULL) {
            const char *nl = strchr(b, '\n');

            CHECK(nl != NULL && nl - b <= 44 && strlen(nl + 1) <= 44);
            CHECK(strlen(b) == strlen(d)); /* only a space became a line break */
        }
    }

    /* [game] achievements = false: recorded, no popup */
    snprintf(cfg, sizeof(cfg), "%s/popups_off_config.toml", s_dir);
    f = fopen(cfg, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs("[game]\nachievements = false\n", f);
        fclose(f);
    }
    snprintf(s_path, sizeof(s_path), "%s/popups_off.toml", s_dir);
    remove(s_path);
    ico_config_reset(cfg, s_no_config);
    ico_ach_init(s_path);
    ico_gs_reset();
    ico_gs_set_sampler(sampler);
    CHECK(!ico_ach_popups_enabled());
    s_pushes = 0;
    fresh_world();
    ticks(1);
    g.stage_no = 31;
    ticks(ICO_ACH_POPUP_GAP_TICKS * 2);
    CHECK(st("cliff") == ICO_ACH_UNLOCKED);
    CHECK(s_pushes == 0 && ico_ach_pending_popups() == 0);
    ico_config_reset(s_no_config, s_no_config);
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    /* no config.toml or ico-pc.ini of the build folder */
    snprintf(s_no_config, sizeof(s_no_config), "%s/achievements_no_config.toml", s_dir);
    remove(s_no_config);
    ico_config_reset(s_no_config, s_no_config);
    test_view();
    test_signals();
    test_each();
    test_suspended();
    test_yorda_safe_counts();
    test_old_file();
    test_persistence();
    test_popups();
    if (failures) {
        fprintf(stderr, "achievements_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("achievements_test: ok (%d achievements)\n", ico_ach_count());
    return 0;
}
