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
 * (ico_host_enter_test_stage: the port writes no game flag).  Both rooms
 * then start where Stage Select starts a stage with no entrance: the
 * boy, the girl (where the stage's stageData flag0 asks for her) and the
 * sword at exitData[0]'s place, the origin (placeAtStart).  boss_test has
 * no exitData row into it, so this is what the game itself did; 8a_TEST's
 * one entrance row (exitData 260 on the European disc, 256 on the North
 * American) leads onto a walkway of collision only, 2533 above the
 * room's floor, that no model draws (build-host/tmp/v0104/evidence/rooms),
 * so its placement is replaced.  Once the room's stage is up, a boy outside
 * every camera zone (the climbing room's origin: its zones lie 1355 and more
 * above its floor) moves to the lowest zone with a floor (startInZone), so
 * the camera finds him.  For the first 300 Main ticks of a room,
 * and at a game over in one, the log follows the boy (boyLine).
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
 *
 * The early show title art (beginArt): Extras > Models' list opened on its
 * first art row (model_viewer.h ico_mv_models_enter_at); the viewer loads
 * the art's members on its host stage (model_viewer_member.c), and Back
 * from the list comes back to this page.  The row is offered where the
 * viewer offers an art row (mv_member_first_row: the disc's logo pack holds
 * one), so the page and the list agree.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ico_unused.h"
#include "../core/region.h" /* ico_region_cur */

#include "df_pack.h" /* port/data: ico_df_has, ico_df_find_member */
#include "game_state_keep.h"
#include "model_viewer.h" /* the art's rows in the model viewer */
#include "settings.h"     /* the host, the title return */

#include "adpcm_init.h"
#include "camera-editor.h" /* CamGroup, CamSetFile */
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
extern GObj *boyGObj;                                        /* main.c */
extern int mpegPlay;                                         /* StageManager.c */
extern int stageManagerFreeResourceFlag;                     /* StageManager.c */
extern void GetRootPosition(void *pos, GObj *obj);           /* geometryManager.c */
extern void GetLowerPlaneCollision(ClipWork *w, float *pos); /* motionManager2.c */
extern GObj *girlGObj;                                       /* main.c */
extern void SetDirectRootPosition(GObj *self, void *v);      /* geometryManager.c */
extern void *TopCameraSetDataOfCurrentStage;                 /* camera-set-manager.c */

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
/* the girl's (kind 2: 148 European, region.h) and the sword's (kind 14:
   the boy's label + 1 on every disc), ACTGame_SetActors_Debug's second
   and third rows */
#define GIRL_LABEL (ico_region_cur()->girl_label)
#define GIRL_KIND 2
#define SWORD_LABEL (BOY_LABEL + 1)
#define SWORD_KIND 14

/* the boy followed in the log: a line as the room's stage is up and every
   PROBE_STEP Main ticks after, to PROBE_TICKS */
#define PROBE_STEP 30u
#define PROBE_TICKS 300u

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
/* the Main tick the room's stage came up, and the next tick the boy's line
   is due (past PROBE_TICKS: none) */
static unsigned int s_upTick;
static unsigned int s_nextProbe;
/* the camera-zone check (startInZone) has run in this room */
static int s_zoneDone;
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
        if (row == ICO_UNUSED_ART && mv_member_first_row() < 0) {
            fprintf(stderr,
                    "unused: no %s row on %s: the model viewer offers none of its models (its "
                    "lines say why)\n",
                    r->name, ri->disc_id);
            return 0;
        }
    }
    fprintf(stderr, "unused: %s on %s (stage %d)\n", r->name, ri->disc_id, stage);
    return 1;
}

/* --- the test rooms ------------------------------------------------------ */

/* The first exitData row leading into the stage, -1 none: the entrance
   ACTGame_SetActors_Debug takes (act-game.c getExitIndexOfStage, over the
   disc's exitData rows) */
static int entranceOf(int stage)
{
    for (int i = 0; i < PAL_NTSCAUG6_JUN26(261, 257, 256); i++) {
        if (exitData[i].nextStage == stage) {
            return i;
        }
    }
    return -1;
}

/* Both rooms start where the game's Stage Select starts a stage with no
   entrance row: ACTGame_SetActors_Debug's three rows (act-game.c) at
   exitData[0], written again after debug.c's hook has run it with the
   stage's own entrance.  The game stores exit rows (as it stores layout
   rows) negated: EXITDATA_GetNextPosition (act-game.c) takes
   pos = -exitData[idx].pos and the rotation in degrees times
   0.017453292 (radians), as initSceneGObj builds a scene object at
   -gen->pos (sceneManager.c:478); +y is down in the world.  Copying a
   row as stored is what put v0.10.3's arena boy 3404 under its floor.
   The boy and the sword where the stage's attrTop is set, the girl where
   its flag0 is; a row inside the stage's own label range with no record
   yet is left to the stage's layout, as the game leaves it ("first").
   The title has a boy (its stageData row's attrTop is set, and its
   camera falls back to him: game_state_keep.c), and the hook runs
   ACTGame_SetActors_Debug with flag 1 while boyGObj is still he, so as
   the game's own pass this one takes his weapon and held item
   (GOBJ_ACT(boyGObj)->weapon, ->curItem): a weapon skips the sword row,
   and the weapon's and the item's records go to the start with the
   boy's row, whether the boy's record is written or left ("first").  A
   zero is printed as 0, not as the -0 the negation makes of it. */
static void placeAtStart(int stage, int row)
{
    const StgPre *sd = &stageData[stage];

    const struct {
        int label, kind, on;
        const char *name;
    } rows[3] = {
        {BOY_LABEL, BOY_KIND, sd->attrTop, "the boy"},
        {GIRL_LABEL, GIRL_KIND, sd->flag0, "the girl"},
        {SWORD_LABEL, SWORD_KIND, sd->attrTop, "the sword"},
    };

    float pos[4] = {-exitData[0].pos[0], -exitData[0].pos[1], -exitData[0].pos[2], 0.0f};
    float rot[4] = {exitData[0].rot[0] * 0.017453292f, exitData[0].rot[1] * 0.017453292f,
                    exitData[0].rot[2] * 0.017453292f, 0.0f};
    /* the title boy's weapon and held item, as act-game.c reads them */
    GObj *held[2] = {0, 0};
    if (boyGObj != 0 && GOBJ_ACT(boyGObj) != 0) {
        held[0] = GOBJ_ACT(boyGObj)->weapon;
        held[1] = GOBJ_ACT(boyGObj)->curItem;
    }
    static const char *const heldName[2] = {"the boy's weapon", "the boy's item"};
    char placed[192] = "";
    size_t used = 0;

    for (int n = 0; n < 3; n++) {
        if (n == 2 && held[0] != 0) {
            break; /* the boy holds a weapon: no sword */
        }
        if (!rows[n].on) {
            continue;
        }
        const int set = !(rows[n].label >= sd->labelTop && rows[n].label < sd->labelEnd &&
                          gamesysObjInfoGet(rows[n].kind, rows[n].label) == 0);
        if (set) {
            gamesysObjInfoPosNewStageSet(rows[n].label, rows[n].kind, stage, pos, rot);
            if (used < sizeof(placed)) {
                int w = snprintf(placed + used, sizeof(placed) - used, "%s%s (record %d)",
                                 used ? ", " : "", rows[n].name, rows[n].label);
                used += w > 0 ? (size_t)w : 0;
            }
        }
        for (int h = 0; n == 0 && h < 2; h++) {
            if (held[h] == 0) {
                continue;
            }
            gamesysObjInfoPosNewStageSet(held[h]->labelId, held[h]->kind, stage, pos, rot);
            if (used < sizeof(placed)) {
                int w = snprintf(placed + used, sizeof(placed) - used, "%s%s (record %d, kind %d)",
                                 used ? ", " : "", heldName[h], held[h]->labelId, held[h]->kind);
                used += w > 0 ? (size_t)w : 0;
            }
        }
    }

    const char *name = ico_unused_row(row)->name;
    const int entrance = entranceOf(stage);
    char why[160];
    if (entrance < 0) {
        snprintf(why, sizeof(why), ": the room has no entrance row");
    } else {
        snprintf(why, sizeof(why),
                 "; the game's own entrance row %d (%.0f, %.0f, %.0f) not used: it leads to a "
                 "walkway with no model",
                 entrance, 0.0 - exitData[entrance].pos[0], 0.0 - exitData[entrance].pos[1],
                 0.0 - exitData[entrance].pos[2]);
    }
    fprintf(stderr,
            "unused: %s: the boy at the room's start (%.0f, %.0f, %.0f), the game's no-entrance "
            "point (exitData[0])%s; placed: %s\n",
            name, pos[0] + 0.0, pos[1] + 0.0, pos[2] + 0.0, why,
            placed[0] ? placed : "none (the stage's rows ask for none)");
}

/* The boy as the game sees him: his root, the ground height and the fall
   height the death checks compare (commonact.c: a fall past gameParam[2],
   1000 on the European disc, is fatal), and the floor the game's own
   probe finds under him (act.c's GetLowerPlaneCollision: a ray from the
   root 10000 down, +y). when: "tick T" (Main ticks since the room's stage
   came up) or "game over" */
/* why the boy cannot be read or probed now, NULL when he can: systemStatus[6],
   a stage change under way (the room's PH_PLAYING outlives it): its
   InitIcoMisc may have the boy up while colObjList still names the old
   stage's objects (fieldCollision.c _Clip); or no stage up with a boy */
static const char *boyMissing(void)
{
    if (systemStatus[6] != 0) {
        return "a stage change under way";
    }
    if (stageManagerFreeResourceFlag != 0 || mpegPlay != 0 || boyGObj == 0 ||
        GOBJ_SUB(boyGObj) == 0) {
        return "the stage has none up";
    }
    return NULL;
}

static void boyLine(const char *when)
{
    const char *name = s_row >= 0 ? ico_unused_row(s_row)->name : "?";
    const char *missing = boyMissing();

    if (missing != NULL) {
        fprintf(stderr, "unused: %s %s: no boy (%s)\n", name, when, missing);
        return;
    }
    float pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    ClipWork w;
    char floor[80];

    GetRootPosition(pos, boyGObj);
    memset(&w, 0, sizeof(w));
    GetLowerPlaneCollision(&w, pos);
    if (w.floor.elem != 0) {
        snprintf(floor, sizeof(floor), "the floor %.0f under him, %.0f below", (double)w.pt[2][1],
                 (double)(w.pt[2][1] - pos[1]));
    } else {
        snprintf(floor, sizeof(floor), "no floor below");
    }
    fprintf(stderr,
            "unused: %s %s: the boy at (%.0f, %.0f, %.0f), ground height %.0f (%s), fall "
            "height %.0f\n",
            name, when, (double)pos[0], (double)pos[1], (double)pos[2],
            (double)GOBJ_SUB(boyGObj)->ctrl.groundHeight, floor,
            (double)GOBJ_SUB(boyGObj)->ctrl.fallHeight);
}

/* The camera finds the boy only inside one of the stage's camera zones:
   the groups of its camera set (CameraSetCameraSet, camera-ico2.c, loads
   stageData[stage].camSetId's set into TopCameraSetDataOfCurrentStage: a
   CamSetFile head, then `count` CamGroup records), whose centre and
   half-size are stored negated as layout rows are (the game tests a group
   against the root times -1: camera-ico2.c findCameraGroupContaining over
   ico2camera_GetTargetPos's negated targets).  The arena's no-entrance
   point lies inside its zone 0 (world centre (-375, 50, 0)); every one of
   the climbing room's 51 zones lies 1355 to 4980 above its big floor, so
   from the origin the camera never finds him
   (build-host/tmp/v0104/evidence/rooms/camera-zones.txt).  Once per room,
   as soon as the boy is up: a root inside a zone is left; otherwise the
   boy (and the girl when she is up) goes to the lowest zone (its box's
   bottom nearest the big floor) whose centre has a floor inside the box,
   probed down from the box's top with the game's own floor probe, at the
   root's own height above its floor (H, from the probe under him where
   he stands), as E3_StageStartBoy moves him with SetDirectRootPosition.
   Zones with the same bottom: the one with the larger floor plan, then
   the set's order (the climbing room's group 20 and its thin side zone
   47 share the bottom; 47's floor lies 103 lower than 20's).  A zero is
   printed as 0, not as the -0 a negation makes of it. */
static void startInZone(void)
{
    const char *name = ico_unused_row(s_row)->name;
    float root[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    ClipWork w;

    GetRootPosition(root, boyGObj);
    memset(&w, 0, sizeof(w));
    GetLowerPlaneCollision(&w, root);
    const int under = w.floor.elem != 0;
    const float h = under ? w.pt[2][1] - root[1] : 0.0f;

    const CamSetFile *set = (const CamSetFile *)TopCameraSetDataOfCurrentStage;
    const int n = set != NULL ? set->count : 0;
    if (set == NULL || n <= 0 || n > 100) {
        fprintf(stderr,
                "unused: %s: no camera set loaded (%d groups): the boy stays at (%.0f, %.0f, "
                "%.0f)\n",
                name, n, root[0] + 0.0, root[1] + 0.0, root[2] + 0.0);
        return;
    }
    const CamGroup *g = (const CamGroup *)(const void *)(set + 1);

    /* the game's test, on the stored box against the negated root */
    for (int i = 0; i < n; i++) {
        int k = 0;
        while (k < 3 && -root[k] >= g[i].center[k] - g[i].range[k] &&
               -root[k] <= g[i].center[k] + g[i].range[k]) {
            k++;
        }
        if (k == 3) {
            fprintf(stderr,
                    "unused: %s: the start is inside camera zone %d of %d (centre (%.0f, %.0f, "
                    "%.0f))\n",
                    name, i, n, 0.0 - g[i].center[0], 0.0 - g[i].center[1], 0.0 - g[i].center[2]);
            return;
        }
    }

    int best = -1;
    float bestBot = 0.0f, bestArea = 0.0f, bestFloor = 0.0f;
    float lowestCentre = 0.0f;
    for (int i = 0; i < n; i++) {
        const float c[3] = {-g[i].center[0], -g[i].center[1], -g[i].center[2]};
        const float yTop = -(g[i].center[1] + g[i].range[1]);
        const float yBot = -(g[i].center[1] - g[i].range[1]);
        if (i == 0 || c[1] > lowestCentre) {
            lowestCentre = c[1];
        }
        float from[4] = {c[0], yTop, c[2], 1.0f};
        memset(&w, 0, sizeof(w));
        GetLowerPlaneCollision(&w, from);
        if (w.floor.elem == 0 || w.pt[2][1] > yBot) {
            continue; /* no floor, or the first one lies below the box */
        }
        const float area = g[i].range[0] * g[i].range[2];
        if (best < 0 || yBot > bestBot || (yBot == bestBot && area > bestArea)) {
            best = i;
            bestBot = yBot;
            bestArea = area;
            bestFloor = w.pt[2][1];
        }
    }
    if (best < 0) {
        fprintf(stderr,
                "unused: %s: no camera zone holds a floor under its centre: the boy stays at "
                "(%.0f, %.0f, %.0f)\n",
                name, root[0] + 0.0, root[1] + 0.0, root[2] + 0.0);
        return;
    }
    float to[4] = {-g[best].center[0], bestFloor - h, -g[best].center[2], 1.0f};
    SetDirectRootPosition(boyGObj, to);
    const int girl = girlGObj != 0 && GOBJ_SUB(girlGObj) != 0;
    if (girl) {
        SetDirectRootPosition(girlGObj, to);
    }
    /* +y is down: the centre above the start when root.y - centre.y > 0
       (a zone below the start makes it negative: said as below) */
    const double above = (double)(root[1] - lowestCentre);
    fprintf(stderr,
            "unused: %s: the game's no-entrance point (%.0f, %.0f, %.0f) is outside every "
            "camera zone (the lowest zone's centre %.0f %s it): the boy moved to zone %d's "
            "floor at (%.0f, %.0f, %.0f) (%.0f above the floor y %.0f)%s%s\n",
            name, root[0] + 0.0, root[1] + 0.0, root[2] + 0.0, (above < 0.0 ? -above : above) + 0.0,
            above < 0.0 ? "below" : "above", best, to[0] + 0.0, to[1] + 0.0, to[2] + 0.0, (double)h,
            (double)bestFloor, under ? "" : "; no floor under the start, so 0 above",
            girl ? "; the girl with him" : "");
}

/* the zone check once the boy is up, within the log's window; past it a
   line says it never ran */
static void zonePoll(void)
{
    if (s_zoneDone) {
        return;
    }
    if (ico_host_main_ticks() - s_upTick > PROBE_TICKS) {
        s_zoneDone = 1;
        fprintf(stderr,
                "unused: %s: the camera-zone check never ran (no boy in the first %u ticks)\n",
                ico_unused_row(s_row)->name, PROBE_TICKS);
        return;
    }
    if (boyMissing() == NULL) {
        s_zoneDone = 1;
        startInZone();
    }
}

/* the boy's line every PROBE_STEP ticks from the stage up to PROBE_TICKS */
static void probePoll(void)
{
    const unsigned int t = ico_host_main_ticks() - s_upTick;
    if (s_nextProbe > PROBE_TICKS || t < s_nextProbe) {
        return;
    }
    if (t > PROBE_TICKS) {
        s_nextProbe = PROBE_TICKS + 1; /* the window passed between two polls */
        return;
    }
    char when[32];
    snprintf(when, sizeof(when), "tick %u", t);
    boyLine(when);
    while (s_nextProbe <= t) {
        s_nextProbe += PROBE_STEP;
    }
}

/* layout_action.c's game over in a room (ico_unused_game_over): the boy as
   he ended */
static void gameOver(void)
{
    if (s_phase == PH_PLAYING) {
        boyLine("game over");
    }
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
    /* no walk in: the climbing room's start is a ramp 1,324 above its
       floor, and the 50-frame walk could take him off it into a fatal
       fall (the game's own way: debug.c ico_host_skip_entry_walk) */
    ico_host_skip_entry_walk();
    placeAtStart(stage, row);
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

/* --- the early show title art ------------------------------------------- */

/* The model list, opened on the first art row the disc has; the viewer
   does the rest (model_viewer.c, model_viewer_member.c). */
static int beginArt(void)
{
    const IcoUnusedRow *r = ico_unused_row(ICO_UNUSED_ART);
    const int row = mv_member_first_row();

    if (s_phase != PH_IDLE || stage_no != TITLE_STAGE || systemStatus[6] != 0) {
        fprintf(stderr, "unused: failed: not on the title (stage %d, stage change %d)\n", stage_no,
                systemStatus[6]);
        return -1;
    }
    if (row < 0) {
        fprintf(stderr, "unused: failed: the model viewer offers no %s row on %s\n", r->name,
                ico_region_cur()->disc_id);
        return -1;
    }
    const int to = ico_mv_models_enter_at(row);
    if (to < 0) {
        fprintf(stderr, "unused: failed: the model viewer's list did not open for %s\n", r->name);
        return -1;
    }
    fprintf(stderr, "unused: %s: the model viewer's list (layout %d) on its row %d\n", r->name, to,
            row);
    return to;
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
        return beginArt();
    default:
        fprintf(stderr, "unused: failed: row %d has no start\n", row);
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
            s_upTick = ico_host_main_ticks();
            s_nextProbe = 0;
            s_zoneDone = 0;
            zonePoll();
            probePoll();
        } else {
            const unsigned int t = ico_host_main_ticks() - s_startTick;
            if ((t > REACH_TICKS && systemStatus[6] == 0) || t > REACH_LIMIT) {
                failStart();
            }
        }
        return;
    }
    if (s_phase == PH_PLAYING) {
        zonePoll();
        probePoll();
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

static const IcoUnusedEngine kEngine = {available, begin, gameOver};

void ico_unused_engine_install(void)
{
    static const UiUnusedHost kHost = {hostAvailable, hostOpen};
    ico_unused_set_engine(&kEngine);
    ui_settings_set_unused_host(&kHost);
}
