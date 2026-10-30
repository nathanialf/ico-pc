/*
 * port/game/test/achievements_test.c
 *
 * The achievements on the CPU: the game-state
 * view over a synthetic snapshot, the signals, every achievement's
 * condition through crafted transitions (unlocked once, suspended in developer
 * mode, yorda_safe counting), the achievements.toml round trip, the
 * popup rate limit and [game] achievements = false; the two discs' keys
 * in one file (stats.stages / stats.stages_jpn / stats.stages_kor, run.slot_N_* /
 * run.jpn_slot_N_* / run.kor_slot_N_*, one set of unlocks) and the popup gap in Main ticks.
 * argv[1]: a writable folder; argv[2]: the disc (a slug of
 * config/regions.txt, pal by default).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "achievements.h"
#include "config.h"
#include "host_config.h"
#include "ico_credits.h"
#include "ico_gamestate.h"
#include "options.h"
#include "popup.h"
#include "region.h" /* port/core: the selected disc and its keys */
#define R (ico_region_cur())
/* the stages the achievements name, the selected disc's (region.h
   ach_stages, in achievements.c's AS_* order; each disc's row resolved by
   its stageData name: gate st04a, grave st18a, windmill st09a, symmetry_L
   st04b, taki st02a, gondola st20a, watertower st10r, symmetry_R st05b,
   cliff st22a, queen st25a, beach st27a) */
#define ST_GATE (R->ach_stages[0])
#define ST_GRAVE (R->ach_stages[1])
#define ST_WINDMILL (R->ach_stages[2])
#define ST_SYMMETRY_L (R->ach_stages[3])
#define ST_WATERFALL (R->ach_stages[4])
#define ST_GONDOLA (R->ach_stages[5])
#define ST_WATERTOWER (R->ach_stages[6])
#define ST_SYMMETRY_R (R->ach_stages[7])
#define ST_CLIFF (R->ach_stages[8])
#define ST_QUEEN (R->ach_stages[9])
#define ST_BEACH (R->ach_stages[10])
_Static_assert(ICO_REGION_ACH_STAGES == 11, "the eleven stages above, achievements.c's AS_*");
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

int ui_popup_push(const char *title, const char *body)
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
    g.layout = R->layout_play;
    g.actors_valid = 1;
    g.boy_present = 1;
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

/* a new game from the title: the title's stage (1 on every disc), then the
   disc's New Game flag (382 on the European disc) */
static void new_game(void)
{
    g.stage_no = R->stage_title;
    flag(R->gflag_new_game, 0);
    ticks(1);
    flag(R->gflag_new_game, 1);
    ticks(1);
}

static int file_count(const char *needle)
{
    FILE *f;

    ico_ach_writer_idle(); /* the file is written in the background */
    f = fopen(s_path, "rb");
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

/* Each disc's retail addresses the view test peeks at, as literals (not
   read back from gamestate.c's tables, which are under test): the European
   (s_peek_pal's sources: config/symbol_addrs.pal.data.txt and the code
   that reads them), the Japanese (config/symbol_addrs.jpn.data.txt) and
   the Korean (config/symbol_addrs.kor.data.txt: gflags 0x002A3040, stage_no
   0x00633610, gFlagGameClear 0x006342D8, current_layout_id 0x00634ECC,
   IosMcPreviewInfo 0x00299940), the North American, August 2001 and June
   2001 discs' (their symbol_addrs.<slug>.data.txt: the story flags are gFlag
   there, 46/46/44 bytes, and there is no gFlagGameClear) and the Asian
   disc's, in config/regions.txt's order.  gameClear 0: the disc has no such
   word; rows: how many rows the disc's peek table has; flagBytes: the story
   flags' served size; previewBytes: IosMcPreviewInfo's */
typedef struct PeekWant {
    unsigned int stageNo, gflags, gameClear, layout, preview;
    int rows, flagBytes, previewBytes;
} PeekWant;

static const PeekWant kPeekWant[] = {
    {0x00639D10u, 0x002A50C0u, 0x0063AA00u, 0x0063B60Cu, 0x0029B9D0u, 14, 50, 24}, /* pal */
    {0x00633810u, 0x002A2FC0u, 0x006344C8u, 0x006350BCu, 0x002998C0u, 13, 50, 24}, /* jpn */
    {0x00633610u, 0x002A3040u, 0x006342D8u, 0x00634ECCu, 0x00299940u, 13, 50, 24}, /* kor */
    {0x00631990u, 0x0028A520u, 0, 0x0063314Cu, 0x00280F78u, 12, 46, 16},           /* ntsc */
    {0x0062BB10u, 0x00286890u, 0, 0x0062D28Cu, 0x0027D2F8u, 12, 46, 16},           /* aug6 */
    {0x00679410u, 0x00272D30u, 0, 0x0067AB3Cu, 0x002695B8u, 12, 44, 16},           /* jun26 */
    {0x00633890u, 0x002A3040u, 0x00634548u, 0x0063513Cu, 0x00299940u, 13, 50, 24}, /* cn */
};
/* IosMcPreviewInfo's play time word on each disc, in config/regions.txt's
   order, as literals: [2] on the European program's discs (la_playtime_count
   counts IosMcPreviewInfo[2]), [1] on the North American disc and the 2001
   previews (their la_playtime_count / la_game_loop count [1], and
   la_save_processing puts the sofa in [2]: the decomp's 2001 McPreview); the
   sofa is the word after it (issue 160) */
static const int kPreviewPlay[] = {2, 2, 2, 1, 1, 1, 2};
#define PLAY (kPreviewPlay[ico_region])
#define SOFA (kPreviewPlay[ico_region] + 1)
_Static_assert(sizeof(kPreviewPlay) / sizeof(kPreviewPlay[0]) == ICO_REGION_COUNT,
               "a preview layout for each row of config/regions.txt");
_Static_assert(sizeof(kPeekWant) / sizeof(kPeekWant[0]) == ICO_REGION_COUNT,
               "a row of retail addresses for each row of config/regions.txt");

static void test_view(void)
{
    unsigned int v;
    const IcoGsPeekEntry *table;
    int n;

    start("view");
    g.stage_no = ST_WINDMILL;
    g.stage_name = "st09a (WINDMILL)";
    g.game_clear = 1;
    g.mc_preview[PLAY] = 50 * 3725; /* frames at 50 a second: 1 h 2 min 5 s */
    g.weapon_kind = 5;
    g.hand_held = 1;
    g.girl_present = 1;
    flag(100, 1);
    ticks(1);
    CHECK(ico_gs_valid());
    CHECK(ico_gs_stage() == ST_WINDMILL);
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

    /* the retail addresses, the selected disc's (gamestate.c s_peek_pal,
       s_peek_jpn, ...: only the European disc has a NonLinearCameraMove
       row), each checked against kPeekWant's literals */
    {
        const int pal = ico_region == ICO_REGION_PAL;
        const PeekWant *w = &kPeekWant[ico_region];
        const unsigned int stageNo = w->stageNo;
        const unsigned int gflags = w->gflags;
        const unsigned int gameClear = w->gameClear; /* 0: none on this disc */
        const unsigned int layout = w->layout;
        const unsigned int preview = w->preview;

        g.stage_no = 0x11223344;
        g.gflags[1] = 0xA5;
        g.layout = R->layout_save_complete;
        ticks(1);
        CHECK(ico_gs_peek(stageNo, 4, &v) == 0 && v == 0x11223344u);
        CHECK(ico_gs_peek(stageNo + 1, 2, &v) == 0 && v == 0x2233u);
        CHECK(ico_gs_peek(stageNo + 3, 1, &v) == 0 && v == 0x11u);
        CHECK(ico_gs_peek(stageNo + 3, 2, &v) == -1); /* runs past stage_no */
        CHECK(ico_gs_peek(gflags + 1, 1, &v) == 0 && v == 0xA5u);
        CHECK(ico_gs_peek(gflags + w->flagBytes - 1, 1, &v) == 0);
        CHECK(ico_gs_peek(gflags + w->flagBytes, 1, &v) == -1);
        if (gameClear != 0) {
            CHECK(ico_gs_peek(gameClear, 4, &v) == 0 && v == 1u); /* gFlagGameClear */
        }
        CHECK(ico_gs_peek(layout, 4, &v) == 0 &&
              v == (unsigned)R->layout_save_complete); /* current_layout_id */
        CHECK(ico_gs_peek(preview + 4u * (unsigned)PLAY, 4, &v) == 0 && v == 50u * 3725u);
        CHECK(ico_gs_peek(preview + w->previewBytes - 4, 4, &v) == 0);
        CHECK(ico_gs_peek(preview + w->previewBytes, 4, &v) == -1);
        if (pal) {
            CHECK(ico_gs_peek(0x00639EA4u, 4, &v) == -1); /* boyGObj: a pointer, not served */
        }
        CHECK(ico_gs_peek(0x01000000u, 4, &v) == -1); /* heap */
        CHECK(ico_gs_peek(stageNo, 3, &v) == -1);
        n = ico_gs_peek_table(&table);
        /* PAL's 14 rows; the Japanese, Korean and Asian discs 13 (no
           NonLinearCameraMove row); the North American, August 2001 and June
           2001 discs 12 (neither it nor gFlagGameClear) */
        CHECK(n == w->rows && table[0].addr == gflags && table[0].size == (unsigned)w->flagBytes);
    }
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
    g.layout = R->layout_pause; /* any layout before the save-complete one */
    g.mc_preview[SOFA] = 1203;
    ticks(1);
    g.layout = R->layout_save_complete;
    ticks(1);
    CHECK(ico_gs_signaled(ICO_GS_EV_SAVE_DONE) == 1 &&
          ico_gs_signal_arg(ICO_GS_EV_SAVE_DONE) == 1203);
    ticks(1);
    CHECK(ico_gs_signaled(ICO_GS_EV_SAVE_DONE) == 0 && ico_gs_saves() == 1);
    new_game();
    CHECK(ico_gs_run_fresh());
    g.layout = R->layout_load;
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

/* a capture counts once (issue 162): the hook fires when the shadow takes
   Yorda, her carried mode follows a tick or so later; the pause menu's
   count is ico_gs_run_captures */
static void capture_start(void)
{
    start("capture_once");
    new_game();
    g.stage_no = ST_GATE;
    g.girl_present = 1;
    ticks(1);
}

static void test_capture_once(void)
{
    int r;
    const int saved_region = ico_region;

    /* the hook on tick N, the flag on tick N+1 (no hook): one */
    capture_start();
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 3757);
    ticks(1);
    CHECK(ico_gs_run_captures() == 1);
    g.yorda_carried_by_enemy = 1;
    ticks(1);
    CHECK(ico_gs_run_captures() == 1);
    ticks(6); /* still carried: nothing more */
    CHECK(ico_gs_run_captures() == 1);

    /* the flag alone: one */
    capture_start();
    g.yorda_carried_by_enemy = 1;
    ticks(1);
    CHECK(ico_gs_run_captures() == 1);
    ticks(3);
    CHECK(ico_gs_run_captures() == 1);

    /* the hook alone (her mode never changes): one */
    capture_start();
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 3757);
    ticks(10);
    CHECK(ico_gs_run_captures() == 1);

    /* the hook, the flag, a drop, a second hook and its flag: two */
    capture_start();
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 3757);
    ticks(1);
    g.yorda_carried_by_enemy = 1;
    ticks(1);
    g.yorda_carried_by_enemy = 0;
    ticks(1);
    CHECK(ico_gs_run_captures() == 1);
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 3757);
    ticks(1);
    g.yorda_carried_by_enemy = 1;
    ticks(1);
    CHECK(ico_gs_run_captures() == 2);

    /* the flag with a hook 3 ticks back and 5 ticks back, no drop between:
       the same capture; after a drop, 5 ticks on: a new one the hook missed */
    capture_start();
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 3757);
    ticks(5);
    g.yorda_carried_by_enemy = 1; /* the mail was slow: still the same capture */
    ticks(1);
    CHECK(ico_gs_run_captures() == 1);
    g.yorda_carried_by_enemy = 0;
    ticks(5);
    g.yorda_carried_by_enemy = 1;
    ticks(1);
    CHECK(ico_gs_run_captures() == 2);

    /* another stage clears the hooked capture: a flag-only capture counts */
    capture_start();
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 3757);
    ticks(1);
    g.stage_no = ST_GRAVE;
    ticks(6);
    g.yorda_carried_by_enemy = 1;
    ticks(1);
    CHECK(ico_gs_run_captures() == 2);

    /* the June 2001 demo's game-over hook counts once per stage entry (its
       death paths have no guard flag of their own) */
    capture_start();
    ico_gs_signal_game_over_once();
    ico_gs_signal_game_over_once();
    ticks(1);
    ico_gs_signal_game_over_once();
    ticks(1);
    CHECK(ico_gs_run_game_overs() == 1 && ico_gs_game_overs() == 1);
    ico_gs_signal(ICO_GS_EV_STAGE_ENTER, ST_GATE); /* the retry reloads the stage */
    ico_gs_signal_game_over_once();
    ticks(1);
    CHECK(ico_gs_run_game_overs() == 2 && ico_gs_game_overs() == 2);

    /* the carried mode, by disc: the disc's own mode (and only that) is
       Yorda carried by an enemy; an enemy must be the carrier */
    {
        static const int kCarried[ICO_REGION_COUNT] = {
            0x6F, /* pal */
            0x6F, /* jpn */
            0x6F, /* kor */
            107,  /* ntsc */
            107,  /* aug6 */
            106,  /* jun26 */
            0x6F, /* cn */
        };
        static const int kModes[] = {0x6F, 107, 106, 0, 105, 108, 110, 112};

        for (r = 0; r < ICO_REGION_COUNT; r++) {
            size_t m;

            ico_region = r;
            CHECK(ico_gs_carried_act_mode() == kCarried[r]);
            for (m = 0; m < sizeof(kModes) / sizeof(kModes[0]); m++) {
                CHECK(ico_gs_girl_carried_by_enemy(kModes[m], 1) == (kModes[m] == kCarried[r]));
                CHECK(!ico_gs_girl_carried_by_enemy(kModes[m], 0));
            }
        }
        ico_region = saved_region;
    }
}

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
    g.layout = R->layout_pause; /* any layout before the save-complete one */
    ticks(1);
    g.mc_preview[SOFA] = sofa;
    g.layout = R->layout_save_complete;
    ticks(1);
    g.layout = R->layout_play;
    ticks(1);
}

static void ending(int clear_before, int play_seconds)
{
    g.game_clear = clear_before;
    g.mc_preview[PLAY] = play_seconds * 50;
    ico_gs_signal(ICO_GS_EV_ENDING, clear_before);
    ticks(1);
}

/* the opening's five parts (op.c's three with gflags 2..4 as they set
   them, st13b's conte 02, deja.c's), the part skip_part skipped with START;
   the parts not in `only` (a bit mask, 0: all) are not signalled */
static void opening_parts(int skip_part, unsigned int only)
{
    int p;

    for (p = 1; p <= ICO_GS_OPENING_PARTS; p++) {
        if (only != 0 && !((only >> (p - 1)) & 1u)) {
            continue;
        }
        if (p <= 3) {
            flag(p + 1, 1);
        }
        ico_gs_signal(ICO_GS_EV_DEMO_END, p * 2 + (p == skip_part));
        stage(40 + p);
    }
}

static void opening(int skip_part)
{
    opening_parts(skip_part, 0);
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
    EXPECT_UNLOCK("gate", stage(ST_GATE));
    EXPECT_UNLOCK("windmill", stage(ST_WINDMILL));
    EXPECT_UNLOCK("graveyard", stage(ST_GRAVE));
    EXPECT_UNLOCK("waterfall", stage(ST_WATERFALL));
    EXPECT_UNLOCK("gondola", stage(ST_GONDOLA));
    EXPECT_UNLOCK("water_tower", stage(ST_WATERTOWER));
    EXPECT_UNLOCK("cliff", stage(ST_CLIFF));
    stage(ST_SYMMETRY_L);
    CHECK(st("east_and_west") == ICO_ACH_LOCKED);
    EXPECT_UNLOCK("east_and_west", stage(ST_SYMMETRY_R));
    EXPECT_UNLOCK("queen", stage(ST_QUEEN));
    /* the disc's queen flag (st25a.c actSt25aQueenDeadChk: gflagOn 338 on
       the European, Japanese, Korean and Asian discs, 307 on the North
       American, 306 on the August 2001 preview, 292 on the June 2001 demo) */
    EXPECT_UNLOCK("queen_defeated", (flag(R->gflag_queen_dead, 1), ticks(1)));
    flag(R->gflag_queen_dead, 0);
    EXPECT_UNLOCK("shore", stage(ST_BEACH));
    if (R->gflag_beach_secret >= 0) {
        EXPECT_UNLOCK("shore_secret", (flag(R->gflag_beach_secret, 1), ticks(1)));
        flag(R->gflag_beach_secret, 0);
    } else {
        /* the 2001 discs' actSt27aEndChk sets no flag for a held item
           (end.c: gflagOn 323, 322 and 308 only, the European arm's
           gflagOn(354) under GetCharHeldItem 3 is not in them): the
           European disc's flag does not unlock it (achievements.c logs
           "this disc has no beach secret" once) */
        flag(354, 1);
        ticks(2);
        CHECK(st("shore_secret") == ICO_ACH_LOCKED);
        CHECK(file_count("[unlocked.shore_secret]") == 0);
        flag(354, 0);
        ticks(1);
    }

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
    /* Extras > Credits opens once "finish" is unlocked */
    CHECK(!ico_credits_unlocked());
    ending(0, 4 * 3600);
    CHECK(st("finish") == ICO_ACH_UNLOCKED);
    CHECK(ico_credits_unlocked());
    CHECK(st("never_taken") == ICO_ACH_LOCKED);
    CHECK(st("unbroken") == ICO_ACH_LOCKED);
    CHECK(st("swift") == ICO_ACH_LOCKED);
    CHECK(st("finish_again") == ICO_ACH_LOCKED);
    /* a loaded game is not a run from a new game */
    new_game();
    g.layout = R->layout_load;
    ticks(1);
    g.layout = R->layout_play;
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
            /* all unlocked above, but the beach secret on a disc without one */
            if (R->gflag_beach_secret < 0 && strcmp(ico_ach_id(i), "shore_secret") == 0) {
                CHECK(st(ico_ach_id(i)) == ICO_ACH_LOCKED);
            } else {
                CHECK(st(ico_ach_id(i)) == ICO_ACH_UNLOCKED);
            }
            for (l = 0; l < UI_LANG_COUNT; l++) {
                CHECK(ui_str_in((UiLang)l, (UiStrId)ico_ach_title_str(i))[0] != '\0');
                CHECK(ui_str_in((UiLang)l, (UiStrId)ico_ach_desc_str(i))[0] != '\0');
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
    stage(ST_WINDMILL);
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
    stage(ST_WINDMILL);
    kills(1);
    CHECK(st("windmill") == ICO_ACH_LOCKED && stats().enemies == 0);
    /* a new run without it: progress resumes and unlocks, once */
    new_game();
    stage(ST_WINDMILL);
    kills(1);
    CHECK(st("windmill") == ICO_ACH_UNLOCKED && st("first_shadow") == ICO_ACH_UNLOCKED);
    CHECK(stats().enemies == 1 && file_count("[unlocked.windmill]") == 1);
    CHECK(s_pushes == 1 && strstr(s_last_body, "Assisted") == NULL);
    /* the Extras credits' playback of the ending suspends too, for the rest
       of the run, as developer mode does */
    new_game();
    ico_credits_set_active(1);
    stage(ST_BEACH);
    CHECK(st("shore") == ICO_ACH_LOCKED);
    ico_credits_set_active(0);
    stage(38);
    stage(ST_BEACH);
    CHECK(st("shore") == ICO_ACH_LOCKED);
    new_game();
    stage(ST_BEACH);
    CHECK(st("shore") == ICO_ACH_UNLOCKED);
    /* start_stage suspends too */
#ifndef _WIN32
    new_game();
    {
        char queen[16];

        snprintf(queen, sizeof(queen), "%d", ST_QUEEN);
        setenv("ICO_START_STAGE", queen, 1);
    }
    stage(ST_QUEEN);
    kills(1);
    CHECK(st("queen") == ICO_ACH_LOCKED && stats().enemies == 1);
    unsetenv("ICO_START_STAGE");
    ticks(1);
    stage(ST_QUEEN);
    CHECK(st("queen") == ICO_ACH_LOCKED); /* the run is still suspended */
    new_game();
    stage(ST_QUEEN);
    CHECK(st("queen") == ICO_ACH_UNLOCKED);
#endif
}

/* yorda_safe is not an assist: its progress and unlocks count */
static void test_yorda_safe_counts(void)
{
    start("yorda_safe");
    new_game();
    ico_opt_set_yorda_safe(1);
    stage(ST_GATE);
    CHECK(st("gate") == ICO_ACH_UNLOCKED);
    kills(25);
    CHECK(stats().enemies == 25 && st("shadows_25") == ICO_ACH_UNLOCKED);
    stage(ST_SYMMETRY_L);
    stage(ST_SYMMETRY_R);
    CHECK(st("east_and_west") == ICO_ACH_UNLOCKED);
    CHECK(file_count("[unlocked.gate]") == 1);
    CHECK(!ico_gs_run_suspended());
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
    g.mc_preview[PLAY] = 50 * 754;
    stage(ST_GATE);
    stage(ST_WINDMILL);
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
    g.stage_no = ST_GATE;
    ticks(2);
    CHECK(s_pushes == 0);
    CHECK(file_count("[unlocked.gate]") == 1);
    /* a missing file is an empty record */
    remove(s_path);
    ico_ach_init(s_path);
    CHECK(ico_ach_state(ico_ach_find("gate")) == ICO_ACH_LOCKED);
}

/* --- the opening's parts ------------------------------------------------------ */

static void test_opening(void)
{
    int p;

    start("opening");
    for (p = 1; p <= ICO_GS_OPENING_PARTS; p++) {
        /* each part skipped on its own keeps it locked, with a part unseen
           too */
        new_game();
        opening(p);
        CHECK(st("opening") == ICO_ACH_LOCKED);
        CHECK(ico_gs_run_opening_skipped() == 1);
        new_game();
        opening_parts(0, 0x1Fu & ~(1u << (p - 1)));
        CHECK(st("opening") == ICO_ACH_LOCKED);
        CHECK(ico_gs_run_opening_parts() == (0x1Fu & ~(1u << (p - 1))));
    }
    /* four parts watched do not unlock; the fifth does, at its end */
    new_game();
    opening_parts(0, 0x0Fu);
    CHECK(st("opening") == ICO_ACH_LOCKED);
    EXPECT_UNLOCK("opening", opening_parts(0, 0x10u));
    CHECK(ico_gs_run_opening_parts() == 0x1Fu);
    start("opening_loaded");
    opening(0); /* without a new game */
    CHECK(st("opening") == ICO_ACH_LOCKED);
}

/* --- run state per save slot ------------------------------------------------------- */

static void save_slot(int slot, unsigned int sum)
{
    ico_ach_slot_saved(slot, sum);
}

static void test_run_slots(void)
{
    IcoGsRun r;

    start("runslots");
    new_game();
    opening_parts(0, 0x07u);
    g.stage_no = ST_GATE;
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 5);
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    ticks(1);
    save_slot(3, 0xFFFFFFF0u);
    CHECK(file_count("slot_3_sum = 4294967280") == 1);
    CHECK(file_count("slot_3_fresh = true") == 1);
    CHECK(file_count("slot_3_captures = 1") == 1);
    CHECK(file_count("slot_3_game_overs = 2") == 1);

    /* another save in slot 4 after more play */
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    ticks(1);
    save_slot(4, 77u);

    /* quit and continue: a new process reads the file, the load restores */
    ico_config_reset(s_no_config, s_no_config);
    ico_ach_reset(s_path);
    ico_gs_set_sampler(sampler);
    fresh_world();
    ticks(1);
    CHECK(ico_gs_run_fresh() == 0 && ico_gs_run_captures() == 0);
    g.layout = R->layout_load; /* la_load_processing */
    ticks(1);
    g.layout = R->layout_play;
    ticks(1);
    CHECK(ico_ach_slot_loaded(3, 0xFFFFFFF0u) == 1);
    ico_gs_run_get(&r);
    CHECK(r.fresh == 1 && r.captures == 1 && r.game_overs == 2);
    CHECK(r.opening_parts == 0x07u && r.opening_skipped == 0 && r.suspended == 0);
    ticks(3);
    CHECK(ico_gs_run_fresh() == 1 && ico_gs_run_game_overs() == 2);
    /* slot 4's run is its own */
    CHECK(ico_ach_slot_loaded(4, 77u) == 1);
    CHECK(ico_gs_run_game_overs() == 3);
    /* a save with another checksum, or no entry, restores nothing */
    memset(&r, 0, sizeof(r));
    ico_gs_run_set(&r);
    CHECK(ico_ach_slot_loaded(3, 0x1234u) == 0);
    CHECK(ico_ach_slot_loaded(9, 0u) == 0);
    CHECK(ico_ach_slot_loaded(-1, 0u) == 0);
    CHECK(ico_gs_run_fresh() == 0);

    /* the fresh-run challenges across the quit: the ending of a restored
       run with a capture and game overs unlocks neither */
    CHECK(ico_ach_slot_loaded(4, 77u) == 1);
    ending(0, 3600);
    CHECK(st("never_taken") == ICO_ACH_LOCKED && st("unbroken") == ICO_ACH_LOCKED);

    /* a skipped part and a suspended run are kept too */
    start("runslots2");
    new_game();
    opening(2);
    ico_opt_set_developer_mode(1);
    ticks(1);
    save_slot(0, 1u);
    ico_opt_set_developer_mode(0);
    ico_ach_reset(s_path);
    ico_gs_set_sampler(sampler);
    fresh_world();
    ticks(1);
    CHECK(ico_ach_slot_loaded(0, 1u) == 1);
    ico_gs_run_get(&r);
    CHECK(r.opening_skipped == 1 && r.suspended == 1 && r.fresh == 1);

    /* New Game after loading slot 0 clears that slot's entry */
    new_game();
    CHECK(ico_gs_run_fresh() == 1 && ico_gs_run_opening_parts() == 0);
    CHECK(ico_ach_slot_loaded(0, 1u) == 0);
    /* and the next save there writes the new run */
    save_slot(0, 2u);
    CHECK(ico_ach_slot_loaded(0, 2u) == 1);
    CHECK(ico_gs_run_fresh() == 1 && ico_gs_run_opening_skipped() == 0);
    /* a New Game that followed no load clears nothing: slot 4 stays */
    start("runslots3");
    save_slot(4, 5u);
    ico_ach_reset(s_path);
    ico_gs_set_sampler(sampler);
    fresh_world();
    ticks(1);
    new_game();
    CHECK(ico_ach_slot_loaded(4, 5u) == 1);
    /* a clean run saved, the process restarted, the game continued to its
       ending */
    start("runslots4");
    new_game();
    save_slot(6, 9u);
    ico_ach_reset(s_path);
    ico_gs_set_sampler(sampler);
    fresh_world();
    ticks(1);
    CHECK(ico_ach_slot_loaded(6, 9u) == 1);
    ending(0, 3600);
    CHECK(st("never_taken") == ICO_ACH_UNLOCKED && st("unbroken") == ICO_ACH_UNLOCKED);
}

/* The journey's saves and enemies defeated (the pause menu's lines): counted
   in the run as the session counts them, reset at the title and by a New
   Game, written with the slot (the save that writes it included) and
   restored by its load */
static void test_run_counts(void)
{
    IcoGsRun r;

    start("runcounts");
    new_game();
    g.stage_no = ST_GATE;
    ticks(1);
    kills(3);
    CHECK(ico_gs_run_enemies() == 3 && ico_gs_enemies_killed() == 3);
    CHECK(ico_gs_run_saves() == 0);
    /* la_save_processing writes the slot, then "File saved." comes up */
    save_slot(2, 11u);
    save_on(329);
    CHECK(ico_gs_run_saves() == 1 && ico_gs_saves() == 1);
    CHECK(file_count("slot_2_saves = 1") == 1);
    CHECK(file_count("slot_2_enemies = 3") == 1);
    ico_gs_run_get(&r);
    CHECK(r.saves == 1 && r.enemies == 3);
    save_slot(2, 12u);
    save_on(329);
    CHECK(ico_gs_run_saves() == 2 && file_count("slot_2_saves = 2") == 1);

    /* quit and continue: the load restores the slot's counts, play goes on
       from them */
    ico_ach_reset(s_path);
    ico_gs_set_sampler(sampler);
    fresh_world();
    ticks(1);
    CHECK(ico_gs_run_saves() == 0 && ico_gs_run_enemies() == 0);
    CHECK(ico_ach_slot_loaded(2, 12u) == 1);
    CHECK(ico_gs_run_saves() == 2 && ico_gs_run_enemies() == 3);
    kills(1);
    CHECK(ico_gs_run_enemies() == 4);

    /* the title ends the run; the session's counts stay */
    stage(R->stage_title);
    CHECK(ico_gs_run_saves() == 0 && ico_gs_run_enemies() == 0);
    CHECK(ico_gs_saves() == 0 && ico_gs_enemies_killed() == 1);
    /* a New Game starts from nothing */
    new_game();
    g.stage_no = ST_GATE;
    ticks(1);
    kills(2);
    save_on(331);
    CHECK(ico_gs_run_saves() == 1 && ico_gs_run_enemies() == 2);
    new_game();
    CHECK(ico_gs_run_saves() == 0 && ico_gs_run_enemies() == 0);

    /* a run set without them has none */
    memset(&r, 0, sizeof(r));
    r.captures = 1;
    ico_gs_run_set(&r);
    CHECK(ico_gs_run_saves() == 0 && ico_gs_run_enemies() == 0 && ico_gs_run_captures() == 1);

    CHECK(!ico_gs_run_partial());

    /* a slot saved before v0.4.0 (no saves or enemies keys): the run is
       partial, the flag kept across a save and its load, cleared by a New
       Game */
    {
        IcoToml *t;

        ico_ach_reread_file(); /* the edit below is to the file on disk */
        t = ico_toml_load(s_path);
        CHECK(t != NULL);
        if (t != NULL) {
            char key[64];

            /* the selected disc's keys (run.slot_7_*, run.jpn_slot_7_*) */
            snprintf(key, sizeof(key), "run.%sslot_7_sum", ico_region_cur()->key_prefix);
            ico_toml_set_int(t, key, 70);
            snprintf(key, sizeof(key), "run.%sslot_7_captures", ico_region_cur()->key_prefix);
            ico_toml_set_int(t, key, 2);
            snprintf(key, sizeof(key), "run.%sslot_7_game_overs", ico_region_cur()->key_prefix);
            ico_toml_set_int(t, key, 4);
            CHECK(ico_toml_save(t, s_path) == 0);
            ico_toml_free(t);
        }
        CHECK(ico_ach_slot_loaded(7, 70u) == 1);
        CHECK(ico_gs_run_partial() && ico_gs_run_captures() == 2 && ico_gs_run_game_overs() == 4);
        save_slot(7, 71u);
        CHECK(file_count("slot_7_partial = true") == 1);
        ico_ach_reset(s_path);
        ico_gs_set_sampler(sampler);
        fresh_world();
        ticks(1);
        CHECK(!ico_gs_run_partial());
        CHECK(ico_ach_slot_loaded(7, 71u) == 1 && ico_gs_run_partial());
        /* a save with no entry at all: nothing known, partial too */
        memset(&r, 0, sizeof(r));
        ico_gs_run_set(&r);
        CHECK(ico_ach_slot_loaded(8, 5u) == 0 && ico_gs_run_partial());
        CHECK(ico_ach_slot_loaded(7, 71u) == 1);
        new_game();
        CHECK(!ico_gs_run_partial());
        g.stage_no = ST_GATE;
        ticks(1);
        save_slot(7, 72u);
        CHECK(ico_ach_slot_loaded(7, 72u) == 1 && !ico_gs_run_partial());
    }
}

/* --- the two discs ------------------------------------------------------------ */

/* a visited set as achievements.toml writes it (achievements.c hex_set: the
   128 bits as 32 hex digits, stage 0 the lowest bit) with stages a, b, c */
static void stages_hex(char *out, size_t size, int a, int b, int c)
{
    unsigned long long w[2] = {0, 0};
    const int s3[3] = {a, b, c};
    int i;

    for (i = 0; i < 3; i++) {
        w[s3[i] >> 6] |= 1ull << (s3[i] & 63);
    }
    snprintf(out, size, "%016llx%016llx", w[1], w[0]);
}

/* The file holds both discs: each disc's visited stages and per-slot runs
   are its own keys (its stage rows and its saves are its own), the
   unlocks and the counters one set for the player. The selected disc
   reads and writes its keys; the other disc's stay as the file has them. */
static void test_discs(void)
{
    const int me = ico_region;
    const int other = me == 0 ? 1 : 0;
    const char *meSfx = ico_region_at(me)->key_suffix;
    const char *otherSfx = ico_region_at(other)->key_suffix;
    const char *mePfx = ico_region_at(me)->key_prefix;
    const char *otherPfx = ico_region_at(other)->key_prefix;
    char key[96];
    char want[128];
    char mine[40];
    IcoToml *t;

    /* the set this disc writes below: stage 2 (fresh_world), its gate and
       its windmill; 0x8804 with the European numbers 2, 11 and 15 */
    stages_hex(mine, sizeof(mine), 2, 11, 15);
    CHECK(strcmp(mine, "00000000000000000000000000008804") == 0);
    stages_hex(mine, sizeof(mine), 2, ST_GATE, ST_WINDMILL);

    /* the popup gap is 5 s of Main ticks: 125 at 25 Hz (the European
       disc's 50 Hz mode), 150 at 30 Hz (60 Hz: the Japanese disc, and the
       European disc's 60 Hz mode) */
    start("discs");
    CHECK(ico_gs_tick_hz() == 25 && ico_ach_popup_gap_ticks() == 125);
    g.system_status[0] = 0;
    ticks(1);
    CHECK(ico_gs_tick_hz() == 30 && ico_ach_popup_gap_ticks() == 150);
    s_pushes = 0;
    g.stage_no = ST_GATE; /* gate: shown now */
    ticks(1);
    g.stage_no = ST_GRAVE; /* grave: waits the gap */
    ticks(1);
    CHECK(s_pushes == 1 && ico_ach_pending_popups() == 1);
    ticks(150 - 2);
    CHECK(s_pushes == 1);
    ticks(1);
    CHECK(s_pushes == 2 && ico_ach_pending_popups() == 0);
    g.system_status[0] = 1;

    /* the file as the other disc left it: its visited set (stage 0), a run
       in its slot 3, an unlock */
    snprintf(s_path, sizeof(s_path), "%s/discs.toml", s_dir);
    ico_ach_writer_idle(); /* the unlocks above write in the background: let them land first */
    remove(s_path);
    t = ico_toml_parse("");
    CHECK(t != NULL);
    if (t == NULL) {
        return;
    }
    ico_toml_set_int(t, "version", 2);
    snprintf(key, sizeof(key), "stats.stages%s", otherSfx);
    ico_toml_set_string(t, key, "00000000000000000000000000000001");
    snprintf(key, sizeof(key), "run.%sslot_3_sum", otherPfx);
    ico_toml_set_int(t, key, 4660);
    snprintf(key, sizeof(key), "run.%sslot_3_captures", otherPfx);
    ico_toml_set_int(t, key, 2);
    ico_toml_set_string(t, "unlocked.gate.time", "2025-10-05T12:00:00Z");
    ico_toml_set_int(t, "unlocked.gate.play_time", 1);
    CHECK(ico_toml_save(t, s_path) == 0);
    ico_toml_free(t);

    ico_config_reset(s_no_config, s_no_config);
    ico_ach_init(s_path);
    ico_ach_set_clock(fixed_clock);
    ico_gs_reset();
    ico_gs_set_sampler(sampler);
    s_pushes = 0;
    fresh_world(); /* stage 2 */
    ticks(1);
    /* one set of unlocks: the other disc's gate is this disc's too */
    CHECK(st("gate") == ICO_ACH_UNLOCKED);
    g.stage_no = ST_GATE;
    ticks(2);
    CHECK(s_pushes == 0);
    /* the other disc's slot 3 is not this disc's slot 3 */
    CHECK(ico_ach_slot_loaded(3, 4660u) == 0);
    /* this disc visits its windmill and saves slot 5 */
    g.stage_no = ST_WINDMILL;
    ticks(1);
    CHECK(st("windmill") == ICO_ACH_UNLOCKED);
    save_slot(5, 85u);
    ico_ach_flush();
    /* its own keys: stages 2, its gate and its windmill in its set, the
       other disc's set and slot as they were */
    snprintf(want, sizeof(want), "\nstages%s = \"%s\"", meSfx, mine);
    CHECK(file_count(want) == 1);
    snprintf(want, sizeof(want), "\nstages%s = \"00000000000000000000000000000001\"", otherSfx);
    CHECK(file_count(want) == 1);
    snprintf(want, sizeof(want), "\n%sslot_5_sum = 85", mePfx);
    CHECK(file_count(want) == 1);
    snprintf(want, sizeof(want), "\n%sslot_3_sum = 4660", otherPfx);
    CHECK(file_count(want) == 1);
    snprintf(want, sizeof(want), "\n%sslot_5_sum", otherPfx);
    CHECK(file_count(want) == 0);
    /* the unlock keys carry no disc */
    CHECK(file_count("[unlocked.gate]") == 1 && file_count("[unlocked.windmill]") == 1);
    CHECK(file_count("unlocked.jpn") == 0 && file_count("_jpn]") == 0);
    CHECK(file_count("unlocked.kor") == 0 && file_count("_kor]") == 0);
    /* the two discs' keys by name: the selected one's and the other's
       (PAL's are the bare names, the others' carry the slug) */
    snprintf(want, sizeof(want), "\nstages%s = \"", meSfx);
    CHECK(file_count(want) == 1);
    snprintf(want, sizeof(want), "\nstages%s = \"", otherSfx);
    CHECK(file_count(want) == 1);
    snprintf(want, sizeof(want), "\n%sslot_", mePfx);
    CHECK(file_count(want) >= 1);
    snprintf(want, sizeof(want), "\n%sslot_", otherPfx);
    CHECK(file_count(want) >= 1);

    /* the other disc reads its own: its slot 3 answers, this disc's slot 5
       does not, and its set is its own (stage 0, then 3: 0x9) */
    ico_region = other;
    ico_ach_init(s_path);
    ico_gs_reset();
    ico_gs_set_sampler(sampler);
    fresh_world();
    g.stage_no = 3;
    ticks(1);
    CHECK(ico_ach_slot_loaded(3, 4660u) == 1 && ico_gs_run_captures() == 2);
    CHECK(ico_ach_slot_loaded(5, 85u) == 0);
    ico_ach_flush();
    snprintf(want, sizeof(want), "\nstages%s = \"00000000000000000000000000000009\"", otherSfx);
    CHECK(file_count(want) == 1);
    snprintf(want, sizeof(want), "\nstages%s = \"%s\"", meSfx, mine);
    CHECK(file_count(want) == 1);
    ico_region = me;
    remove(s_path);
}

/* --- the file's write path (the writer thread) ---------------------------------- */

static char *read_whole(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf = malloc(65536);
    size_t n;

    if (f == NULL || buf == NULL) {
        if (f != NULL) {
            fclose(f);
        }
        free(buf);
        return NULL;
    }
    n = fread(buf, 1, 65535, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static void test_write_path(void)
{
    unsigned int sub0, wr0, rep0, sub, wr, rep;
    unsigned int hand_due;
    char want[64];
    char *text;
    char *disk;
    char bad[700];

    start("writepath");
    new_game();
    ico_ach_flush();
    /* the first kill unlocks first_shadow, and an unlock writes at once: get
       it out of the way so the kills below change a counter and nothing else */
    kills(1);
    ico_ach_writer_idle();
    /* only the hand-held time changes: written every 60 s of Main ticks, not
       every ICO_ACH_STATS_FLUSH_TICKS */
    g.girl_present = 1;
    g.hand_held = 1;
    ticks(1);
    hand_due = 60u * (unsigned int)ico_gs_tick_hz();
    CHECK(ico_ach_flush_due_ticks() == (int)hand_due);
    CHECK(hand_due > ICO_ACH_STATS_FLUSH_TICKS);
    ico_ach_writer_counts(&sub0, &wr0, &rep0);
    ticks((int)hand_due - 10);
    ico_ach_writer_counts(&sub, &wr, &rep);
    CHECK(sub == sub0); /* the short period has long passed: nothing was written */
    ticks(20);
    ico_ach_writer_counts(&sub, &wr, &rep);
    CHECK(sub == sub0 + 1);
    ico_ach_writer_idle();
    CHECK(ico_ach_flush_due_ticks() == (int)hand_due); /* the write cleared the dirty mark */

    /* any other change writes at the short period */
    g.hand_held = 0;
    kills(1);
    CHECK(ico_ach_flush_due_ticks() == ICO_ACH_STATS_FLUSH_TICKS);
    ico_ach_writer_counts(&sub0, &wr0, &rep0);
    ticks(ICO_ACH_STATS_FLUSH_TICKS - 20);
    ico_ach_writer_counts(&sub, &wr, &rep);
    CHECK(sub == sub0);
    ticks(40);
    ico_ach_writer_counts(&sub, &wr, &rep);
    CHECK(sub == sub0 + 1);

    /* the text handed to the writer is the file's: held back, a newer text
       replaces the waiting one, and the newer one is what lands on disk */
    ico_ach_writer_idle();
    ico_ach_writer_hold(1);
    ico_ach_writer_counts(&sub0, &wr0, &rep0);
    kills(1);
    ticks(ICO_ACH_STATS_FLUSH_TICKS + 20);
    kills(1);
    ticks(ICO_ACH_STATS_FLUSH_TICKS + 20);
    ico_ach_writer_counts(&sub, &wr, &rep);
    CHECK(sub == sub0 + 2);
    CHECK(rep == rep0 + 1); /* the first waiting text was replaced */
    CHECK(wr == wr0);       /* held: nothing was written */
    CHECK(ico_ach_written_text() != NULL);
    text = ico_ach_written_text() != NULL ? strdup(ico_ach_written_text()) : NULL;
    snprintf(want, sizeof(want), "enemies = %u", stats().enemies);
    CHECK(text != NULL && strstr(text, want) != NULL);
    ico_ach_writer_hold(0);
    ico_ach_flush();
    ico_ach_writer_counts(&sub, &wr, &rep);
    CHECK(wr == wr0 + 1); /* one file write for the two texts */
    disk = read_whole(s_path);
    CHECK(disk != NULL && text != NULL && strcmp(disk, text) == 0);
    free(disk);
    free(text);

    /* a flush writes a pending change and waits for it */
    kills(1);
    ico_ach_flush();
    snprintf(want, sizeof(want), "enemies = %u", stats().enemies);
    disk = read_whole(s_path);
    CHECK(disk != NULL && strstr(disk, want) != NULL);
    free(disk);

    /* a write that fails stays pending: the next period hands the text over again */
    snprintf(bad, sizeof(bad), "%s/no_such_folder_for_ach/x.toml", s_dir);
    ico_ach_reset(bad);
    ico_ach_set_clock(fixed_clock);
    ico_gs_set_sampler(sampler);
    fresh_world();
    ticks(1);
    kills(1);              /* first_shadow unlocks: its write fails at once */
    ico_ach_writer_idle(); /* that write is over before the counts are taken */
    ico_ach_writer_counts(&sub0, &wr0, &rep0);
    ticks(ICO_ACH_STATS_FLUSH_TICKS + 20);
    ico_ach_writer_idle();
    ico_ach_writer_counts(&sub, &wr, &rep);
    CHECK(sub == sub0 + 1 && wr == wr0 + 1);
    ticks(ICO_ACH_STATS_FLUSH_TICKS + 20); /* the failure was seen: pending again */
    ico_ach_writer_counts(&sub, &wr, &rep);
    CHECK(sub == sub0 + 2);
    ico_ach_flush();
}

/* --- popups ---------------------------------------------------------------------- */

static void test_popups(void)
{
    char cfg[600];
    FILE *f;

    start("popups");
    s_pushes = 0;
    /* three at once: one popup now, the next after the gap */
    g.stage_no = ST_GATE;
    ticks(1);
    g.stage_no = ST_WINDMILL;
    ticks(1);
    g.stage_no = ST_GRAVE;
    ticks(1);
    CHECK(s_pushes == 1);
    CHECK(strcmp(s_last_title, ui_str_in(UI_LANG_EN, UI_STR_ACH_GATE)) == 0);
    CHECK(strcmp(s_last_body, ui_str_in(UI_LANG_EN, UI_STR_ACH_GATE_DESC)) == 0);
    CHECK(ico_ach_pending_popups() == 2);
    ticks(ico_ach_popup_gap_ticks() - 3);
    CHECK(s_pushes == 1);
    ticks(1);
    CHECK(s_pushes == 2);
    CHECK(strcmp(s_last_title, ui_str_in(UI_LANG_EN, UI_STR_ACH_WINDMILL)) == 0);
    /* the UI queue full: kept and retried */
    s_push_full = 1;
    ticks(ico_ach_popup_gap_ticks() * 2);
    CHECK(s_pushes == 2 && ico_ach_pending_popups() == 1);
    s_push_full = 0;
    ticks(1);
    CHECK(s_pushes == 3 && ico_ach_pending_popups() == 0);
    /* the language at popup time */
    ui_set_language(UI_LANG_DE);
    g.stage_no = ST_WATERFALL;
    ticks(ico_ach_popup_gap_ticks());
    CHECK(strcmp(s_last_title, "Der Wasserfall") == 0);
    ui_set_language(UI_LANG_EN);
    /* a long description is broken into lines of at most 44 letters */
    {
        int i = ico_ach_find("never_taken");
        const char *d = ui_str_in(UI_LANG_EN, (UiStrId)ico_ach_desc_str(i));
        const char *b;

        CHECK(strlen(d) > 44);
        new_game();
        ending(0, 9 * 3600);
        ticks(ico_ach_popup_gap_ticks() * 4);
        b = pushed_body(ui_str_in(UI_LANG_EN, (UiStrId)ico_ach_title_str(i)));
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
    g.stage_no = ST_CLIFF;
    ticks(ico_ach_popup_gap_ticks() * 2);
    CHECK(st("cliff") == ICO_ACH_UNLOCKED);
    CHECK(s_pushes == 0 && ico_ach_pending_popups() == 0);
    ico_config_reset(s_no_config, s_no_config);
}

int main(int argc, char **argv)
{
    if (argc > 2 && ico_region_select(argv[2]) != 0) {
        fprintf(stderr, "achievements_test: no region %s\n", argv[2]);
        return 1;
    }
    printf("achievements_test: the %s disc\n", ico_region_cur()->slug);
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    /* no config.toml or ico-pc.ini of the build folder */
    snprintf(s_no_config, sizeof(s_no_config), "%s/achievements_no_config.toml", s_dir);
    remove(s_no_config);
    ico_config_reset(s_no_config, s_no_config);
    test_view();
    test_signals();
    test_capture_once();
    test_each();
    test_opening();
    test_run_slots();
    test_run_counts();
    test_suspended();
    test_yorda_safe_counts();
    test_persistence();
    test_write_path();
    test_popups();
    test_discs();
    if (failures) {
        fprintf(stderr, "achievements_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("achievements_test: ok (%d achievements)\n", ico_ach_count());
    return 0;
}
