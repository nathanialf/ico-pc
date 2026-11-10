/*
 * port/game/test/difficulty_knock_test.c
 *
 * The knock-down limit's game side (port/game/difficulty.h's record, driven
 * through port/game/gamestate.c with a synthetic sampler, as the program's
 * hooks drive it):
 *
 *   snapshot the boy's act mode and get-up timer, unknown (-1, 0) while his
 *            object is unreadable or the view invalid
 *   latch    ico_gs_knockdown, the hook each actCommonDown copy calls: a
 *            fall of the boy's put down by a hit counts once; the same fall
 *            (still down, or the get-up timer running), a landing, a fall
 *            no hit caused and another actor's fall do not; the in-fall
 *            latch clears through the snapshot exactly as host_loop.c's poll
 *            passes it to ico_difficulty_tick, and not while his object is
 *            unreadable; one log line per entry of the boy's
 *   limit    the third fall on Hard returns 1, and so does every later
 *            entry of his while the game over is due; ico_gs_knock_game_
 *            over_take answers 1 once (again after a save state's load);
 *            the watch line when he is neither down nor dead for 2 s with
 *            the game over due
 *   room     a room entry (the stage manager's STAGE_ENTER signal) starts
 *            the room's count again and keeps the journey's
 *   journey  the journey scope: the count crosses rooms and starts again
 *            after the game over it caused, not after another one; the
 *            June 2001 demo's ico_gs_signal_game_over_once (two calls, one
 *            game over)
 *   perma    Permadeath: a game over ends the journey
 *   off      Normal: nothing counts, no line
 *   hooks    text scans: the four copies of actCommonDown in commonact.c
 *            (the hook with each disc's landing argument, the lying after
 *            DownFunc and before the loop), actDownGameOver's hold (the
 *            game's own frame-rate idiom, no get-up mail),
 *            actDownGameOverNow's two game-over forms, actAfterDown's call,
 *            the after-proc run at every interrupt in both of act.c's
 *            BeforeFunc bodies, the room entry through gamestate.c's signal
 *            (StageManager.c not edited), the game over in gamestate.c's
 *            tick, host_loop.c's ico_difficulty_tick call, the snapshot's
 *            two fields
 *
 * CPU only; one run per disc (the slug selects the region row).  The log
 * lines are read back on POSIX hosts only.
 *
 * Usage: difficulty_knock_test <work folder> <disc slug>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include "config.h"
#include "difficulty.h"
#include "ico_gamestate.h"
#include "region.h" /* port/core: the selected disc */
#include "../../test/ico_check.h"

/* ico_achievements shows its unlocks through the UI's popup; no UI here */
int ui_popup_push(const char *title, const char *body);

int ui_popup_push(const char *title, const char *body)
{
    (void)title;
    (void)body;
    return 0;
}

#define DOWN 0x15 /* actModeTbl's DOWN (actCommonDown) on every disc */
#define DEAD 0x16 /* DEAD (actCommonDie) */
#define STAND 1
#define MAIL 291 /* the European family's knock-down mail (only logged) */

static char s_dir[512];

/* --- the synthetic game ---------------------------------------------------------- */

static IcoGsSnapshot g;

static void sampler(IcoGsSnapshot *out)
{
    *out = g;
}

/* a running game in stage 10, 25 Main ticks a second, the boy standing */
static void world(void)
{
    memset(&g, 0, sizeof(g));
    g.valid = 1;
    g.stage_no = 10;
    g.system_status[0] = 1;
    g.system_status[1] = 2;
    g.actors_valid = 1;
    g.boy_present = 1;
    g.boy_act_mode = STAND;
    g.boy_down_timer = 0;
}

/* one Main tick and host_loop.c's poll after it (ico_host_step: the
   achievements' poll ticks the view, then the latch) */
static void tick(void)
{
    ico_gs_tick();
    {
        const int boy_mode = ico_gs_boy_act_mode();

        ico_difficulty_tick(boy_mode >= 0, boy_mode, ico_gs_boy_down_timer());
    }
}

static void ticks(int n)
{
    while (n-- > 0) {
        tick();
    }
}

static const IcoDifficulty *rec(void)
{
    return ico_difficulty_active();
}

/* the listener's events */
static int s_ev[4], s_ev_a, s_ev_b;

static void listener(IcoDiffEvent ev, int a, int b)
{
    if ((int)ev >= 0 && (int)ev < 4) {
        s_ev[ev]++;
    }
    s_ev_a = a;
    s_ev_b = b;
}

/* a knock-down by a Shadow's hit while he stands, the hook's arguments as
   the copies pass them (is_boy, landing, by_hit, get-up timer, mail) */
static int knock(void)
{
    return ico_gs_knockdown(1, 0, 1, 0, MAIL);
}

/* down, then up again with the get-up timer run out: the latch clears */
static void get_up(void)
{
    g.boy_act_mode = DOWN;
    tick();
    g.boy_act_mode = STAND;
    g.boy_down_timer = 0;
    tick();
}

static int count_of(const char *text, const char *needle)
{
    int n = 0;
    const char *p = text;
    const size_t len = strlen(needle);

    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += len;
    }
    return n;
}

/* --- stderr into a file (the log lines) -------------------------------------------- */

static char s_cap_path[700];
#ifndef _WIN32
static int s_cap_saved = -1;
#endif

static void cap_begin(const char *name)
{
    snprintf(s_cap_path, sizeof(s_cap_path), "%s/%s.log", s_dir, name);
#ifndef _WIN32
    {
        int fd = open(s_cap_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

        fflush(stderr);
        s_cap_saved = fd >= 0 ? dup(2) : -1;
        if (s_cap_saved >= 0) {
            dup2(fd, 2);
        }
        if (fd >= 0) {
            close(fd);
        }
    }
#endif
}

/* the captured text (malloc'd), or NULL where nothing is captured (Windows) */
static char *cap_end(void)
{
#ifndef _WIN32
    FILE *f;
    char *text;
    long n;

    fflush(stderr);
    if (s_cap_saved < 0) {
        return NULL;
    }
    dup2(s_cap_saved, 2);
    close(s_cap_saved);
    s_cap_saved = -1;
    f = fopen(s_cap_path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)(n > 0 ? n : 0) + 1);
    if (text != NULL) {
        size_t got = n > 0 ? fread(text, 1, (size_t)n, f) : 0;

        text[got] = '\0';
    }
    fclose(f);
    remove(s_cap_path);
    return text;
#else
    return NULL;
#endif
}

/* --- the record through the view ---------------------------------------------------- */

static void start_hard(void)
{
    IcoDifficulty d;

    ico_gs_reset();
    ico_gs_set_sampler(sampler);
    world();
    ico_difficulty_preset(ICO_DIFF_HARD, &d);
    ico_difficulty_begin(&d);
    CHECK(rec()->row[ICO_DIFF_KNOCK_LIMIT] == 3 &&
              rec()->row[ICO_DIFF_KNOCK_SCOPE] == ICO_DIFF_SCOPE_ROOM,
          "Hard: limit %d scope %d", (int)rec()->row[ICO_DIFF_KNOCK_LIMIT],
          (int)rec()->row[ICO_DIFF_KNOCK_SCOPE]);
    tick();
    ico_gs_signal(ICO_GS_EV_STAGE_ENTER, g.stage_no);
    tick();
    memset(s_ev, 0, sizeof(s_ev));
}

static void test_snapshot(void)
{
    ico_gs_reset();
    ico_gs_set_sampler(sampler);
    world();
    g.boy_act_mode = DOWN;
    g.boy_down_timer = 7;
    tick();
    CHECK(ico_gs_boy_act_mode() == DOWN && ico_gs_boy_down_timer() == 7, "mode %d timer %d",
          ico_gs_boy_act_mode(), ico_gs_boy_down_timer());
    /* his object unreadable (a stage being torn down, a movie): unknown */
    g.actors_valid = 0;
    tick();
    CHECK(ico_gs_boy_act_mode() == -1 && ico_gs_boy_down_timer() == 0);
    g.valid = 0;
    tick();
    CHECK(ico_gs_boy_act_mode() == -1 && ico_gs_boy_down_timer() == 0);
}

static void test_latch(void)
{
    char *log;

    start_hard();
    cap_begin("knock_latch");
    g.boy_act_mode = DOWN; /* the act runs from this tick; the hook is at its entry */
    CHECK(knock() == 0);
    CHECK(rec()->knocks_room == 1 && rec()->knocks_journey == 1 && rec()->in_fall == 1);
    CHECK(s_ev[ICO_DIFF_EV_KNOCK] == 1 && s_ev_a == 1 && s_ev_b == 3);
    /* the same fall: another entry while he lies */
    CHECK(knock() == 0 && rec()->knocks_room == 1);
    ticks(10);
    CHECK(rec()->in_fall == 1, "the latch cleared while he lay");
    /* up, the get-up timer running: still the same fall */
    g.boy_act_mode = STAND;
    g.boy_down_timer = 54;
    tick();
    CHECK(rec()->in_fall == 1, "the latch cleared while the get-up timer ran");
    CHECK(ico_gs_knockdown(1, 0, 1, 54, MAIL) == 0 && rec()->knocks_room == 1);
    /* his object unreadable: the latch is kept */
    g.actors_valid = 0;
    g.boy_down_timer = 0;
    tick();
    CHECK(rec()->in_fall == 1, "the latch cleared on an unreadable snapshot");
    g.actors_valid = 1;
    tick();
    CHECK(rec()->in_fall == 0, "the latch stayed with Ico up and the timer run out");
    /* not counted: no hit put him down (a ledge), a landing, another actor */
    CHECK(ico_gs_knockdown(1, 0, 0, 0, 290) == 0);
    CHECK(ico_gs_knockdown(1, 1, 1, 0, 56) == 0);
    CHECK(ico_gs_knockdown(0, 0, 1, 0, MAIL) == 0);
    CHECK(rec()->knocks_room == 1 && rec()->knocks_journey == 1 && rec()->in_fall == 0,
          "room %d journey %d in_fall %d", (int)rec()->knocks_room, (int)rec()->knocks_journey,
          (int)rec()->in_fall);
    log = cap_end();
    if (log != NULL) {
        CHECK(strstr(log, "difficulty: Ico down 1 (mail 291, hit 1, landing 0, get-up timer 0, "
                          "stage 10): counted; 1 in this room, 1 on this journey, limit 3 a "
                          "room") != NULL,
              "no counted line:\n%s", log);
        CHECK(strstr(log, "Ico down 2 (mail 291, hit 1, landing 0, get-up timer 0, stage 10): "
                          "not counted: the same fall (down, or getting up)") != NULL);
        CHECK(strstr(log, "Ico down 3 (mail 291, hit 1, landing 0, get-up timer 54, stage 10): "
                          "not counted: the same fall (down, or getting up)") != NULL);
        CHECK(strstr(log, "Ico down 4 (mail 290, hit 0, landing 0, get-up timer 0, stage 10): "
                          "not counted: no hit put him down") != NULL);
        CHECK(strstr(log, "Ico down 5 (mail 56, hit 1, landing 1, get-up timer 0, stage 10): "
                          "not counted: the landing of the fall the down act began") != NULL);
        CHECK(count_of(log, "difficulty: Ico down ") == 5, "%d entry lines (another actor's?)",
              count_of(log, "difficulty: Ico down "));
        free(log);
    }
}

static void test_limit(void)
{
    char *log;

    start_hard();
    CHECK(knock() == 0);
    get_up();
    CHECK(knock() == 0);
    get_up();
    CHECK(rec()->knocks_room == 2 && rec()->over_pending == 0);
    cap_begin("knock_limit");
    g.boy_act_mode = DOWN;
    CHECK(knock() == 1, "the third fall did not reach the limit");
    CHECK(rec()->over_pending == 1 && rec()->knocks_room == 3);
    CHECK(s_ev[ICO_DIFF_EV_LIMIT] == 1 && s_ev_a == 3 && s_ev_b == 3);
    /* due: every entry of his answers 1 (another mail took him into the act
       again), a landing and a fall without a hit too, never another actor's */
    CHECK(knock() == 1);
    CHECK(ico_gs_knockdown(1, 1, 1, 0, 56) == 1);
    CHECK(ico_gs_knockdown(1, 0, 0, 0, 290) == 1);
    CHECK(ico_gs_knockdown(0, 0, 1, 0, MAIL) == 0);
    CHECK(rec()->knocks_room == 3, "a due entry counted again");
    /* asked for once: the lying's end, then actAfterDown finds it taken */
    CHECK(ico_gs_knock_game_over_take(ICO_GS_KNOCK_LAY_END) == 1);
    CHECK(ico_gs_knock_game_over_take(ICO_GS_KNOCK_LEFT_DOWN) == 0);
    CHECK(ico_gs_knock_game_over_take(ICO_GS_KNOCK_LAY_END) == 0);
    /* lying or dead: no watch line; standing 2 s with it due: one line */
    ticks(60);
    g.boy_act_mode = DEAD;
    ticks(60);
    g.boy_act_mode = STAND;
    ticks(60);
    log = cap_end();
    if (log != NULL) {
        const char *w = strstr(log, "difficulty: the knock-down limit's game over is due but Ico "
                                    "is in act mode 1 (not down, not dead; asked 1, stage 10): "
                                    "it comes at his next knock-down");

        CHECK(strstr(log, "difficulty: Ico down 3 (mail 291, hit 1, landing 0, get-up timer 0, "
                          "stage 10): the limit's game over is due: he lies, then the game "
                          "over; 3 in this room, 3 on this journey, limit 3 a room") != NULL,
              "no limit line:\n%s", log);
        CHECK(strstr(log, "difficulty: the knock-down limit's game over asked for (he lay 2 s; "
                          "stage 10)") != NULL);
        CHECK(w != NULL, "no watch line:\n%s", log);
        CHECK(count_of(log, "is due but Ico is in act mode") == 1, "%d watch lines",
              count_of(log, "is due but Ico is in act mode"));
        free(log);
    }

    /* the game over (ACT_LAYOUT_GAMEOVER's signal): the due game over cleared,
       the room's count after the limit's game over, the journey's kept */
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    tick();
    CHECK(rec()->over_pending == 0 && rec()->knocks_room == 0 && rec()->knocks_journey == 3,
          "after the game over: pending %d room %d journey %d", (int)rec()->over_pending,
          (int)rec()->knocks_room, (int)rec()->knocks_journey);
    CHECK(ico_gs_game_overs() == 1);
    CHECK(ico_gs_knock_game_over_take(ICO_GS_KNOCK_LEFT_DOWN) == 0);
    CHECK(knock() == 0 && rec()->knocks_room == 1, "the first fall after the game over");

    /* a mail took him out of the act first: actAfterDown's call asks */
    get_up();
    CHECK(knock() == 0);
    get_up();
    CHECK(knock() == 1);
    CHECK(ico_gs_knock_game_over_take(ICO_GS_KNOCK_LEFT_DOWN) == 1);
    CHECK(ico_gs_knock_game_over_take(ICO_GS_KNOCK_LAY_END) == 0);
    /* a save state's load takes the baseline again: the restored lying asks
       again (the state's record holds its own due game over) */
    ico_gs_rebaseline();
    CHECK(ico_gs_knock_game_over_take(ICO_GS_KNOCK_LAY_END) == 1);
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    tick();
    CHECK(rec()->over_pending == 0);
}

static void test_room(void)
{
    start_hard();
    CHECK(knock() == 0);
    get_up();
    CHECK(knock() == 0);
    get_up();
    CHECK(rec()->knocks_room == 2 && rec()->knocks_journey == 2);
    /* the next room (the stage manager's signal; a Continue comes through it
       too) */
    g.stage_no = 11;
    ico_gs_signal(ICO_GS_EV_STAGE_ENTER, 11);
    CHECK(rec()->knocks_room == 0 && rec()->knocks_journey == 2,
          "the room entry: room %d journey %d", (int)rec()->knocks_room,
          (int)rec()->knocks_journey);
    tick();
    CHECK(knock() == 0 && rec()->knocks_room == 1);
    get_up();
    CHECK(knock() == 0);
    get_up();
    CHECK(knock() == 1, "the third fall in the new room");
}

static void test_journey(void)
{
    char *log;

    start_hard();
    ico_difficulty_set_row(ICO_DIFF_KNOCK_SCOPE, ICO_DIFF_SCOPE_JOURNEY);
    ico_difficulty_set_row(ICO_DIFF_KNOCK_LIMIT, 2);
    CHECK(knock() == 0);
    get_up();
    g.stage_no = 12;
    ico_gs_signal(ICO_GS_EV_STAGE_ENTER, 12);
    tick();
    CHECK(rec()->knocks_room == 0 && rec()->knocks_journey == 1);
    g.boy_act_mode = DOWN;
    CHECK(knock() == 1, "the journey's second fall");
    /* the June 2001 demo's death paths: one game over for two calls */
    cap_begin("knock_journey");
    ico_gs_signal_game_over_once();
    ico_gs_signal_game_over_once();
    tick();
    log = cap_end();
    CHECK(ico_gs_signaled(ICO_GS_EV_GAME_OVER) == 1);
    CHECK(rec()->over_pending == 0 && rec()->knocks_journey == 0 && rec()->knocks_room == 0,
          "journey scope after its game over: pending %d room %d journey %d",
          (int)rec()->over_pending, (int)rec()->knocks_room, (int)rec()->knocks_journey);
    if (log != NULL) {
        CHECK(strstr(log, "difficulty: the knock-down limit's game over: the journey's count (2) "
                          "starts again") != NULL,
              "no journey line:\n%s", log);
        free(log);
    }
    /* a game over the limit did not cause keeps the journey's count */
    g.boy_act_mode = STAND;
    get_up();
    CHECK(knock() == 0 && rec()->knocks_journey == 1);
    g.stage_no = 13;
    ico_gs_signal(ICO_GS_EV_STAGE_ENTER, 13);
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    tick();
    CHECK(rec()->knocks_journey == 1 && rec()->over_pending == 0);
}

static void test_permadeath(void)
{
    start_hard();
    ico_difficulty_set_row(ICO_DIFF_PERMADEATH, 1);
    CHECK(!ico_difficulty_finished());
    /* any game over, here the June 2001 demo's form */
    ico_gs_signal_game_over_once();
    tick();
    CHECK(ico_difficulty_finished(), "Permadeath: the game over did not end the journey");
    CHECK(s_ev[ICO_DIFF_EV_ENDED] == 1);
}

static void test_off(void)
{
    char *log;
    int i;

    ico_gs_reset();
    ico_gs_set_sampler(sampler);
    world();
    ico_difficulty_begin(NULL); /* Normal: no limit */
    tick();
    cap_begin("knock_off");
    for (i = 0; i < 5; i++) {
        g.boy_act_mode = DOWN;
        CHECK(knock() == 0);
        get_up();
    }
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    tick();
    log = cap_end();
    CHECK(rec()->knocks_room == 0 && rec()->knocks_journey == 0 && rec()->over_pending == 0);
    CHECK(ico_gs_knock_game_over_take(ICO_GS_KNOCK_LAY_END) == 0);
    if (log != NULL) {
        CHECK(strstr(log, "Ico down") == NULL && strstr(log, "knock-down") == NULL,
              "a line with the limit off:\n%s", log);
        free(log);
    }
}

/* --- the hooks ------------------------------------------------------------------------ */

static char *read_source(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long n;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)(n > 0 ? n : 0) + 1);
    if (text != NULL) {
        size_t got = n > 0 ? fread(text, 1, (size_t)n, f) : 0;

        text[got] = '\0';
    }
    fclose(f);
    return text;
}

/* the function whose definition starts with head: a malloc'd copy from head
   to its closing brace, or NULL */
static char *body_of(const char *text, const char *head)
{
    const char *p = strstr(text, head);
    const char *q;
    int depth = 0;
    char *out;

    if (p == NULL || (q = strchr(p, '{')) == NULL) {
        return NULL;
    }
    for (; *q != '\0'; q++) {
        if (*q == '{') {
            depth++;
        } else if (*q == '}' && --depth == 0) {
            break;
        }
    }
    if (*q == '\0') {
        return NULL;
    }
    out = (char *)malloc((size_t)(q - p) + 2);
    if (out != NULL) {
        memcpy(out, p, (size_t)(q - p) + 1);
        out[q - p + 1] = '\0';
    }
    return out;
}

/* the hook in one copy: the call with its landing argument, then the lying
   after the copy's DownFunc and before its loop */
static void check_copy(const char *text, const char *head, const char *landing)
{
    char *b = body_of(text, head);
    char call[160];

    CHECK(b != NULL, "commonact.c: no %s", head);
    if (b == NULL) {
        return;
    }
    snprintf(call, sizeof(call), "over = ico_gs_knockdown(self == boyGObj, %s, ", landing);
    CHECK(count_of(b, call) == 1, "%s: the hook \"%s\"", head, call);
    CHECK(count_of(b, "GOBJ_ACT(self)->attacker != 0,") == 1, "%s: by_hit", head);
    CHECK(count_of(b, "ico_gs_game_down_timer(self), s->intrKind);") == 1, "%s: the timer", head);
    CHECK(count_of(b, "if (over) {\n        actDownGameOver(self); /* PC port: never returns */") ==
              1,
          "%s: the lying", head);
    {
        const char *after = strstr(b, "ACT_AFTER_PROC(s) = (void (*)(GObj *))actAfterDown;");
        const char *hook = strstr(b, call);
        const char *down = strstr(b, "DownFunc(self);");
        const char *lie = strstr(b, "actDownGameOver(self);");
        const char *loop = strstr(b, "while (1) {");

        CHECK(after != NULL && hook != NULL && down != NULL && lie != NULL && loop != NULL &&
                  after < hook && hook < down && down < lie && lie < loop,
              "%s: the order (after-proc, hook, DownFunc, lying, loop)", head);
    }
    free(b);
}

static void test_hooks(void)
{
    char *t = read_source(ICO_COMMONACT_SRC);
    char *b;

    CHECK(t != NULL, "no %s", ICO_COMMONACT_SRC);
    if (t != NULL) {
        check_copy(t, "static void actCommonDown_ntsc(GObj *volatile self)", "0");
        check_copy(t, "inline static void actCommonDown_jun26(GObj *volatile self)", "0");
        check_copy(t, "static void actCommonDown_aug6(GObj *volatile self)", "0");
        check_copy(t, "void actCommonDown(GObj *volatile self)\n{", "!notDamage");
        CHECK(count_of(t, "ico_gs_knockdown(") == 4, "commonact.c: %d hook calls",
              count_of(t, "ico_gs_knockdown("));
        CHECK(count_of(t, "actDownGameOver(self); /* PC port: never returns */") == 4);
        /* the lying: the game's own frame-rate idiom (actCommonDie's), the hit
           flag every frame, no get-up mail */
        b = body_of(t, "static void actDownGameOver(GObj *volatile self)");
        CHECK(b != NULL);
        if (b != NULL) {
            CHECK(count_of(b, "cnt <= ((60 - systemStatus[0] * 10) / systemStatus[1]) * 2;") == 1);
            CHECK(count_of(b, "GOBJ_ACT(self)->hit = 1;") == 2);
            CHECK(count_of(b, "actDownGameOverNow(ICO_GS_KNOCK_LAY_END);") == 1);
            CHECK(count_of(b, "ACTSendMailCorrect") == 0 && count_of(b, "iosOmSendMail") == 0);
            free(b);
        }
        /* the game over: once, the demo's own form there */
        b = body_of(t, "static void actDownGameOverNow(int why)");
        CHECK(b != NULL);
        if (b != NULL) {
            CHECK(count_of(b, "if (!ico_gs_knock_game_over_take(why)) {") == 1);
            CHECK(count_of(b, "if (ICO_JUN26) {") == 1);
            CHECK(count_of(b, "ico_gs_signal_game_over_once();") == 1);
            CHECK(count_of(b, "lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(62, 59, 58));") == 1);
            CHECK(count_of(b, "ACT_LAYOUT_GAMEOVER();") == 1);
            CHECK(count_of(b, "enable_game_pause") == 0 &&
                      count_of(b, "scpBoyControlReadDisable") == 0,
                  "the game over leaves the pause and control flags as the boy's death does");
            free(b);
        }
        /* the layout is ACT_LAYOUT_GAMEOVER's own */
        b = body_of(t, "void ACT_LAYOUT_GAMEOVER(void)");
        CHECK(b != NULL &&
              strstr(b, "lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(62, 59, 58));") != NULL);
        free(b);
        /* a mail took him out of the act: the game over at once */
        b = body_of(t, "inline void actAfterDown(GObj *volatile self)\n{"); /* not the prototype */
        CHECK(b != NULL);
        if (b != NULL) {
            CHECK(count_of(b, "if (self == boyGObj) {\n        "
                              "actDownGameOverNow(ICO_GS_KNOCK_LEFT_DOWN);") == 1);
            free(b);
        }
        free(t);
    }

    /* the after-proc runs at every interrupt's mode change, in both bodies of
       BeforeFunc (act.c beside commonact.c): actAfterDown runs whenever the
       down act is left through a mail, on every disc */
    {
        char path[1024];
        const char *src = ICO_COMMONACT_SRC;
        const char *slash = strrchr(src, '/');
        const int dir = slash != NULL ? (int)(slash - src) + 1 : 0;

        snprintf(path, sizeof(path), "%.*sact.c", dir, src);
        t = read_source(path);
        CHECK(t != NULL, "no %s", path);
        if (t != NULL) {
            const char *run = "if (ACT_AFTER_PROC(w) != 0) {\n                "
                              "ACT_AFTER_PROC(w)((GObj *)self);";
            char *j = body_of(t, "static void BeforeFunc_jun26(GObj *self)");
            char *m = body_of(t, "void BeforeFunc(GObj *self)\n{");

            CHECK(j != NULL && count_of(j, run) == 1, "act.c: BeforeFunc_jun26's after-proc");
            CHECK(m != NULL && count_of(m, run) == 1, "act.c: BeforeFunc's after-proc");
            free(j);
            free(m);
            free(t);
        }
    }

    /* the room entry: the stage manager's one signal, which gamestate.c's
       ico_gs_signal hands to the record (StageManager.c is not edited) */
    t = read_source(ICO_STAGEMANAGER_SRC);
    CHECK(t != NULL);
    if (t != NULL) {
        b = body_of(t, "static void start_stage_Load_thread(int stage)");
        CHECK(b != NULL && count_of(b, "ico_gs_signal(ICO_GS_EV_STAGE_ENTER, stage);") == 1);
        free(b);
        CHECK(count_of(t, "ICO_GS_EV_STAGE_ENTER") == 1 && count_of(t, "ico_difficulty") == 0);
        free(t);
    }
    t = read_source(ICO_GAMESTATE_SRC);
    CHECK(t != NULL);
    if (t != NULL) {
        b = body_of(t, "void ico_gs_signal(int event, int arg)");
        CHECK(b != NULL && count_of(b, "ico_difficulty_stage_enter(arg);") == 1);
        free(b);
        b = body_of(t, "void ico_gs_tick(void)");
        CHECK(b != NULL && count_of(b, "if (s_count[ICO_GS_EV_GAME_OVER] != 0) {") == 1 &&
              count_of(b, "ico_difficulty_game_over();") == 1);
        free(b);
        CHECK(count_of(t, "ico_difficulty_game_over()") == 1 &&
              count_of(t, "ico_difficulty_stage_enter(") == 1);
        CHECK(count_of(t, "s->boy_act_mode = a->actMode;") == 1 &&
              count_of(t, "s->boy_down_timer = ico_gs_game_down_timer(boyGObj);") == 1);
        free(t);
    }
    /* the latch's poll, after the achievements' (it ticks the view) */
    t = read_source(ICO_HOST_LOOP_SRC);
    CHECK(t != NULL);
    if (t != NULL) {
        const char *poll = strstr(t, "ico_ach_host_poll(ico_host_main_ticks());");
        const char *call =
            strstr(t, "ico_difficulty_tick(boy_mode >= 0, boy_mode, ico_gs_boy_down_timer());");

        CHECK(poll != NULL && call != NULL && poll < call &&
                  count_of(t, "ico_difficulty_tick(") == 1,
              "host_loop.c: the latch's poll");
        CHECK(count_of(t, "const int boy_mode = ico_gs_boy_act_mode();") == 1);
        free(t);
    }
    t = read_source(ICO_GAMESTATE_HDR);
    CHECK(t != NULL);
    if (t != NULL) {
        CHECK(count_of(t, "int boy_act_mode;") == 1 && count_of(t, "int boy_down_timer;") == 1);
        free(t);
    }
}

int main(int argc, char **argv)
{
    char cfg[600];

    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    if (argc > 2 && ico_region_select(argv[2]) != 0) {
        fprintf(stderr, "difficulty_knock_test: no region %s\n", argv[2]);
        return 1;
    }
    printf("difficulty_knock_test: the %s disc\n", ico_region_cur()->slug);
    /* no config.toml or ico-pc.ini of the build folder; the sidecar here */
    snprintf(cfg, sizeof(cfg), "%s/difficulty_knock_no_config.toml", s_dir);
    remove(cfg);
    ico_config_reset(cfg, cfg);
    ico_difficulty_file_init(s_dir);
    ico_difficulty_set_listener(listener);

    test_snapshot();
    test_latch();
    test_limit();
    test_room();
    test_journey();
    test_permadeath();
    test_off();
    test_hooks();

    ico_difficulty_set_listener(NULL);
    ico_gs_set_sampler(NULL);
    return ico_check_summary("difficulty_knock_test");
}
