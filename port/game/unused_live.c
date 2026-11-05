/*
 * port/game/unused_live.c
 *
 * Extras > Unused content's engine (ico_unused.h): the game's side, in the
 * program only.
 *
 * Whether the running disc has a row (available): the row's stage on this
 * disc (ico_unused_stage, through ico_region_stage), the pack its
 * stageData row's dataFile names in DATA.DF's directory (as the model
 * viewer asks for a host stage's pack, model_viewer.c modelStageOnDisc),
 * the movie's loose file, the art's pack member.  The June 2001 demo's
 * DATA.DF holds none of them; the 104-row discs have no stage row for the
 * movie.
 *
 * The test rooms (begin, from the Settings menu's proc on the title): the
 * game's state is kept (game_state_keep.c, as Extras > Credits keeps it),
 * the title theme fades as a load from the title fades it
 * (layout_action.c la_load_processing, credits_live.c), and the game's own
 * debug.c enters the stage the way its Stage Select does
 * (ico_host_enter_test_stage: the port writes no game flag).  boss_test
 * has no exitData row into it, so ACTGame_SetActors_Debug puts the boy's
 * record at exitData[0]'s place, the origin; nothing of the arena's layout
 * stands at that height (its sword at y 65, its sixteen shadows at y 71:
 * objLayout rows 3589 and 3591-3606 on the European disc), so the boy's
 * record is moved to the first shadow row of the stage's layout (a read
 * of the running disc's objLayout, kind 4: attackhit.c's enemy kind), and
 * the log names both places.  8a_TEST has its entrance (exitData 260 on the European disc).
 *
 * The rooms have no exit: pause > End Game goes back to the title (the
 * game's own path), and a game over goes there with either choice
 * (layout_action.c la_game_over_continue, ico_unused_game_over).  As the
 * title's stage is entered again the state is put back and the flag
 * cleared (ico_unused_stage_enter), and Settings opens again on the
 * Unused content page once the title's menu is up
 * (ui_settings_title_return / ui_settings_reopen_page, as the model
 * viewer's Characters comes back).
 *
 * The poll (once per vsync, after the game's threads): a start that never
 * reaches its stage is a failure, logged, the title requested and the
 * state put back as it is entered (the credits' bounds).
 *
 * The attract movie (beginMovie): the title's attract path started from
 * the Settings menu: mpegPlayReturnStage the title, the theme faded, a
 * stage switch to the movie stage; Main plays it and returns by itself.
 * The art row's start belongs to later work; here it refuses with a line.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ico_unused.h"
#include "../core/region.h" /* ico_region_cur */

#include "df_pack.h" /* port/data: ico_df_has, ico_df_find_member */
#include "game_state_keep.h"
#include "settings.h" /* the host, the title return */

#include "adpcm_init.h"
#include "gamesys.h"
#include "layout_texture.h"
#include "s_init.h"
#include "typedef.h"

/* the game's side, declared here as the callers in the game do */
extern int stage_no;
extern int systemStatus[];
extern struct SqEntry *titleAdpcm;             /* op.c */
extern int mpegPlayReturnStage;                /* StageManager.c */
extern unsigned int stage_after_skipping_demo; /* layout_action.c */
extern void stgmgrForceSwitchWithFade(int stage, float fadeIn, float fadeOut);
extern unsigned int ico_host_main_ticks(void);
extern int RequestStageChangeSimple(int no, float speed, float wait, unsigned char r,
                                    unsigned char gr, unsigned char b);

#define TITLE_STAGE (ico_region_cur()->stage_title)
/* the game's empty layout, the one a scene runs under (55, 52 on the
   Japanese and Korean discs: region.h); a stage's own init switches to the
   play layout (layout_texture.c init_layout_texture), whose Start opens
   the pause menu */
#define LAYOUT_NONE (ico_region_cur()->layout_scene)
/* the boy's object record (kind 1): ACTGame_SetActors_Debug's first row,
   the disc's (54, 51, 50, 47: region.h) */
#define BOY_LABEL (ico_region_cur()->credits_boy_label)
#define BOY_KIND 1
/* attackhit.c's enemy kind: a shadow's objLayout row */
#define KIND_ENEMY 4

/* Main ticks the stage change may take before it counts as failed (the
   credits' bounds: credits_live.c REACH_TICKS) */
#define REACH_TICKS 1500
#define REACH_LIMIT 4500
#define RETURN_TICKS 1500
/* the title's menu after the return (10 s at 25 ticks a second, the
   model viewer's RETURN_TIMEOUT_TICKS): past this Settings is not opened
   again (a log line) */
#define REOPEN_TICKS 250u

enum {
    PH_IDLE,
    PH_SWITCHING,
    PH_PLAYING,
    PH_RETURNING,
    /* the attract movie: the stage switch asked, the film playing, the
       title requested after a start that never reached it */
    PH_MOVIE_SWITCHING,
    PH_MOVIE_PLAYING,
    PH_MOVIE_RETURNING
};

/* main.c sets this when the player skipped a movie (movie_proc returned 1) */
#define SKIPPED_DEMO 0xFFFFFFFEu

static int s_phase = PH_IDLE;
static int s_row = -1;
static int s_stage = -1;
static IcoGameKeep s_kept;
static unsigned int s_startTick;
static unsigned int s_returnTick;
static int s_lastStage;
static int s_reopen;
static unsigned int s_reopenTick;
/* the movie's start in wall seconds: Main runs no tick while a film plays,
   so its length is not read from the Main ticks */
static time_t s_movieStart;

/* --- available ----------------------------------------------------------- */

static int available(int row)
{
    const IcoUnusedRow *r = ico_unused_row(row);
    const IcoRegionInfo *ri = ico_region_cur();
    const int stage = ico_unused_stage(row);
    IcoVfs *vfs = ico_vfs_disc();
    char name[48];

    if (r == NULL || stage <= 0 || stage >= ri->stage_count) {
        return 0;
    }
    if (vfs == NULL) {
        fprintf(stderr, "unused: no %s row: the disc cannot be read\n", r->name);
        return 0;
    }
    if (r->pack != NULL) {
        /* the stage row's own pack, as the stage load opens it */
        snprintf(name, sizeof(name), "%s.DF", stageData[stage].dataFile);
        if (strcmp(name, r->pack) != 0) {
            fprintf(stderr,
                    "unused: no %s row on %s: its stage %d (%s) names the pack %s, not %s\n",
                    r->name, ri->disc_id, stage, stageData[stage].name, name, r->pack);
            return 0;
        }
        if (ico_df_has(vfs, name) != 1) {
            fprintf(stderr, "unused: no %s row on %s: its DATA.DF holds no %s (stage %d)\n",
                    r->name, ri->disc_id, name, stage);
            return 0;
        }
    }
    if (r->loose != NULL && ico_df_has(vfs, r->loose) != 1) {
        fprintf(stderr, "unused: no %s row on %s: its DATA.DF holds no %s (stage %d)\n", r->name,
                ri->disc_id, r->loose, stage);
        return 0;
    }
    if (r->member != NULL) {
        IcoDfMember m;
        if (ico_df_find_member(vfs, r->member, &m) != 0) {
            fprintf(stderr, "unused: no %s row on %s: no pack holds %s\n", r->name, ri->disc_id,
                    r->member);
            return 0;
        }
    }
    fprintf(stderr, "unused: %s on %s (stage %d)\n", r->name, ri->disc_id, stage);
    return 1;
}

/* --- the test rooms ------------------------------------------------------ */

/* boss_test: the boy's record from exitData[0] (the origin) to the first
   shadow row of the stage's layout; 0 when the layout has none (the record
   stays, logged) */
static int placeOnFloor(int stage)
{
    const StgPre *sd = &stageData[stage];
    for (int i = sd->labelTop; i < sd->labelEnd; i++) {
        if (objLayout[i].kind == KIND_ENEMY) {
            float pos[3] = {objLayout[i].pos[0], objLayout[i].pos[1], objLayout[i].pos[2]};
            float rot[3] = {0.0f, 0.0f, 0.0f};
            gamesysObjInfoPosNewStageSet(BOY_LABEL, BOY_KIND, stage, pos, rot);
            fprintf(stderr,
                    "unused: the arena has no entrance: the boy at its first shadow row %d "
                    "(%.0f, %.0f, %.0f), not exitData[0]'s (%.0f, %.0f, %.0f)\n",
                    i, (double)pos[0], (double)pos[1], (double)pos[2], (double)exitData[0].pos[0],
                    (double)exitData[0].pos[1], (double)exitData[0].pos[2]);
            return 1;
        }
    }
    fprintf(stderr,
            "unused: the arena's layout rows %d..%d hold no shadow: the boy stays at "
            "exitData[0]'s (%.0f, %.0f, %.0f)\n",
            sd->labelTop, sd->labelEnd - 1, (double)exitData[0].pos[0], (double)exitData[0].pos[1],
            (double)exitData[0].pos[2]);
    return 0;
}

static int beginRoom(int row)
{
    const IcoUnusedRow *r = ico_unused_row(row);
    const int stage = ico_unused_stage(row);

    if (s_phase != PH_IDLE || stage_no != TITLE_STAGE || systemStatus[6] != 0) {
        fprintf(stderr, "unused: failed: not on the title (stage %d, stage change %d)\n", stage_no,
                systemStatus[6]);
        return -1;
    }
    ico_game_keep_take(&s_kept);
    ico_unused_set_running(row);
    s_phase = PH_SWITCHING;
    s_row = row;
    s_stage = stage;
    s_startTick = ico_host_main_ticks();
    s_lastStage = stage_no;
    s_reopen = 0;
    /* the title theme, as la_load_processing leaves the title for a save's
       stage (credits_live.c begin) */
    if (titleAdpcm != 0) {
        titleAdpcm->stream->fadeStep = 0x40;
    }
    titleAdpcm = 0;
    ico_host_enter_test_stage(stage);
    if (row == ICO_UNUSED_ARENA) {
        placeOnFloor(stage);
    }
    fprintf(stderr,
            "unused: test room %s (stage %d on this disc) started from the title at Main tick "
            "%u\n",
            r->name, stage, s_startTick);
    return LAYOUT_NONE;
}

/* --- the attract movie --------------------------------------------------- */

/* The European disc's second attract film, as the title's attract starts the
   first (op.c, actOpDemo01 mode 2): the title is the return stage, the
   title theme fades, and the stage switch goes to the movie stage; Main's
   movie path (main.c: movie_init, movie_proc(movie_abort_check), then
   ACTGame_SetActors_Debug and the switch to mpegPlayReturnStage) plays it,
   Start skips it, and the title comes back by itself.  No game state is
   touched, so none is kept.  The title's menu then reopens on the page. */
static int beginMovie(void)
{
    const IcoUnusedRow *r = ico_unused_row(ICO_UNUSED_MOVIE);
    const int stage = ico_unused_stage(ICO_UNUSED_MOVIE);

    if (s_phase != PH_IDLE || stage_no != TITLE_STAGE || systemStatus[6] != 0) {
        fprintf(stderr, "unused: failed: not on the title (stage %d, stage change %d)\n", stage_no,
                systemStatus[6]);
        return -1;
    }
    ico_unused_set_running(ICO_UNUSED_MOVIE);
    s_phase = PH_MOVIE_SWITCHING;
    s_row = ICO_UNUSED_MOVIE;
    s_stage = stage;
    s_startTick = ico_host_main_ticks();
    s_movieStart = time(NULL);
    s_lastStage = stage_no;
    s_reopen = 0;
    mpegPlayReturnStage = TITLE_STAGE;
    /* a skip is told by this word at the return; an earlier film's would
       read as this one's */
    stage_after_skipping_demo = 0;
    if (titleAdpcm != 0) {
        titleAdpcm->stream->fadeStep = 0x40;
    }
    titleAdpcm = 0;
    stgmgrForceSwitchWithFade(stage, 256.0f, 4.0f);
    fprintf(stderr, "unused: %s started from the title (stage %d, Main tick %u)\n", r->name, stage,
            s_startTick);
    return LAYOUT_NONE;
}

static void movieFinish(const char *why)
{
    fprintf(stderr, "unused: back at the title after %.0f s (%s)\n",
            difftime(time(NULL), s_movieStart), why);
    ico_unused_set_running(-1);
    s_phase = PH_IDLE;
    s_row = -1;
    s_stage = -1;
    ui_settings_title_return(UI_PAGE_UNUSED);
    s_reopen = 1;
    s_reopenTick = ico_host_main_ticks();
}

static void movieEnter(void)
{
    if (s_phase == PH_MOVIE_PLAYING) {
        movieFinish(stage_after_skipping_demo == SKIPPED_DEMO ? "skipped" : "movie ended");
    } else if (s_phase == PH_MOVIE_RETURNING) {
        movieFinish("fail-start");
    }
}

static void moviePoll(void)
{
    if (s_phase == PH_MOVIE_SWITCHING) {
        if (stage_no == s_stage && systemStatus[6] == 0) {
            s_phase = PH_MOVIE_PLAYING;
            fprintf(stderr, "unused: the movie's stage %d is up at Main tick %u; Start skips it\n",
                    stage_no, ico_host_main_ticks());
        } else {
            const unsigned int t = ico_host_main_ticks() - s_startTick;
            if ((t > REACH_TICKS && systemStatus[6] == 0) || t > REACH_LIMIT) {
                fprintf(stderr,
                        "unused: failed: the movie's stage %d was not reached by Main tick %u "
                        "(stage %d, stage change %d)\n",
                        s_stage, ico_host_main_ticks(), stage_no, systemStatus[6]);
                if (stage_no == TITLE_STAGE && systemStatus[6] == 0) {
                    movieFinish("fail-start");
                } else {
                    s_phase = PH_MOVIE_RETURNING;
                    s_returnTick = ico_host_main_ticks();
                    RequestStageChangeSimple(TITLE_STAGE, 16.0f, 8.0f, 0, 0, 0);
                }
            }
        }
    } else if (s_phase == PH_MOVIE_RETURNING &&
               ico_host_main_ticks() - s_returnTick > RETURN_TICKS) {
        movieFinish("fail-start, the title did not come back");
    }
}

static int begin(int row)
{
    switch (row) {
    case ICO_UNUSED_ARENA:
    case ICO_UNUSED_CLIMB:
        return beginRoom(row);
    case ICO_UNUSED_MOVIE:
        return beginMovie();
    case ICO_UNUSED_ART:
    default:
        /* the art's start is not in this build */
        fprintf(stderr, "unused: failed: %s cannot be started in this build\n",
                ico_unused_row(row) != NULL ? ico_unused_row(row)->name : "?");
        return -1;
    }
}

/* the seconds since the start, a tick systemStatus[1] fields of 50 Hz or
   59.94 Hz (systemStatus[0]), as credits_live.c roll measures */
static double secondsSinceStart(void)
{
    const int fields = systemStatus[1] > 0 ? systemStatus[1] : 2;
    const double tickMs = fields * (systemStatus[0] ? 20.0 : 1001.0 / 60.0);
    return (ico_host_main_ticks() - s_startTick) * tickMs / 1000.0;
}

static void finish(const char *why)
{
    const int n = ico_game_keep_put_back(&s_kept);
    const char *name = s_row >= 0 ? ico_unused_row(s_row)->name : "?";

    fprintf(stderr,
            "unused: back at the title after %.1f s (%s): the test room %s ended, the game's "
            "state put back (%d game flag%s differed)\n",
            secondsSinceStart(), why, name, n, n == 1 ? "" : "s");
    ico_unused_set_running(-1);
    s_phase = PH_IDLE;
    s_row = -1;
    s_stage = -1;
    /* the title comes up with no menu and no logo until its card check
       has decided, then Settings opens on the page (the poll) */
    ui_settings_title_return(UI_PAGE_UNUSED);
    s_reopen = 1;
    s_reopenTick = ico_host_main_ticks();
}

void ico_unused_stage_enter(int stage)
{
    if (stage != TITLE_STAGE) {
        return;
    }
    if (s_phase >= PH_MOVIE_SWITCHING) {
        movieEnter();
    } else if (s_phase == PH_PLAYING) {
        finish(ico_unused_left_by()); /* End Game, or a game over's choice */
    } else if (s_phase == PH_RETURNING) {
        finish("fail-start");
    }
}

/* the start did not reach the room: back to the title, the state put back
   as the title is entered; still on the title with no stage change
   running: put back now */
static void failStart(void)
{
    fprintf(stderr,
            "unused: failed: the test room's stage %d was not reached by Main tick %u (stage %d, "
            "stage change %d)\n",
            s_stage, ico_host_main_ticks(), stage_no, systemStatus[6]);
    if (stage_no == TITLE_STAGE && systemStatus[6] == 0) {
        finish("fail-start");
        return;
    }
    s_phase = PH_RETURNING;
    s_returnTick = ico_host_main_ticks();
    RequestStageChangeSimple(TITLE_STAGE, 16.0f, 8.0f, 0, 0, 0);
}

/* back at the title: Settings on Unused content once the title's menu is
   up and faded in (lt_switch_layout works only then), as the model
   viewer's way back from Characters */
static void reopenPoll(void)
{
    const IcoRegionInfo *ri = ico_region_cur();
    if (stage_no == TITLE_STAGE && systemStatus[6] == 0 && lt_fade_status() == 2 &&
        ui_settings_title_decided() &&
        (current_layout_id == ri->layout_title_continue ||
         current_layout_id == ri->layout_title_new)) {
        s_reopen = 0;
        const int to = ui_settings_reopen_page(UI_PAGE_UNUSED);
        if (to >= 0) {
            lt_switch_layout(to);
        }
        fprintf(stderr, "unused: Options open again on Unused content (layout %d)\n", to);
    } else if (ico_host_main_ticks() - s_reopenTick > REOPEN_TICKS) {
        s_reopen = 0;
        ui_settings_title_return(-1);
        fprintf(stderr,
                "unused: the title's menu did not come back; Options not reopened (stage %d, "
                "layout %d)\n",
                stage_no, current_layout_id);
    }
}

void ico_unused_host_poll(void)
{
    if (s_reopen) {
        reopenPoll();
    }
    if (s_phase == PH_IDLE) {
        return;
    }
    if (stage_no != s_lastStage) {
        fprintf(stderr, "unused: stage %d at Main tick %u\n", stage_no, ico_host_main_ticks());
        s_lastStage = stage_no;
    }
    if (s_phase >= PH_MOVIE_SWITCHING) {
        moviePoll();
        return;
    }
    if (s_phase == PH_SWITCHING) {
        if (stage_no == s_stage && systemStatus[6] == 0) {
            s_phase = PH_PLAYING;
            fprintf(stderr,
                    "unused: the test room's stage %d is up at Main tick %u (%.1f s); leave "
                    "with Start, then End Game\n",
                    stage_no, ico_host_main_ticks(), secondsSinceStart());
        } else {
            const unsigned int t = ico_host_main_ticks() - s_startTick;
            if ((t > REACH_TICKS && systemStatus[6] == 0) || t > REACH_LIMIT) {
                failStart();
            }
        }
        return;
    }
    if (s_phase == PH_RETURNING && ico_host_main_ticks() - s_returnTick > RETURN_TICKS) {
        /* the state put back where it stands, the flag cleared */
        finish("fail-start, the title did not come back");
    }
}

/* --- the host ------------------------------------------------------------ */

static int hostAvailable(int row)
{
    return ico_unused_available(row);
}

static int hostOpen(int row)
{
    return ico_unused_start(row);
}

static const IcoUnusedEngine kEngine = {available, begin};

void ico_unused_engine_install(void)
{
    static const UiUnusedHost kHost = {hostAvailable, hostOpen};
    ico_unused_set_engine(&kEngine);
    ui_settings_set_unused_host(&kHost);
}
