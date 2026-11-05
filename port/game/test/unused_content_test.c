/*
 * port/game/test/unused_content_test.c
 *
 * Extras > Unused content (ico_unused.h), CPU only: unused_content.c (the
 * rows, the flag, the start, the engine indirection) and unused_live.c
 * (the engine) over stubs of the game, DATA.DF and Settings, one disc per
 * run (ctests unused_content, unused_content_<slug>).
 *
 * Checked: each row's stage on the disc (the European 88, 91, 59, 1 through
 * ico_region_stage: 86, 89, -1 on the 104-row discs, 85, 88, -1 on the
 * June 2001 demo); the engine's availability against a fake stageData
 * whose rows name the table's packs and stubbed ico_df_has /
 * ico_df_find_member holding what each disc's DATA.DF holds (every row on
 * the European disc, all but the movie on the 104-row discs, none on the
 * demo), asked once per disc; a stage row naming another pack refused;
 * without an engine nothing is available and a start fails; a start the
 * disc does not have is refused before the engine; the test rooms start
 * only from the title, keep the game's state, fade the title theme, enter
 * the stage through debug.c's hook, return the scene layout and set the
 * flag; both rooms place the boy and the sword (and the girl in the room
 * whose stageData flag0 is set, the climbing room) at -exitData[0].pos,
 * exitData[0].rot in radians (a non-zero fake row, so a copy as stored
 * fails), the climbing room's own entrance row not used, each logged; the
 * camera-zone rule once the boy is up (testZones, over a fake camera set
 * stored negated): a start inside a zone stays, one outside every zone
 * moves to the lowest zone whose centre has a floor in its box (ties: the
 * larger floor plan) at his own height above the floor, the girl with him
 * when she is up, no zone with a floor or no set leaves him, each logged; the
 * boy's line (root, ground height, the probe's floor, fall height) as the
 * stage is up, every 30 Main ticks to 300 and at a game over; a second
 * start while one runs is refused; the stage up, the title's stage entered
 * puts the state back, clears the flag, logs the time and End Game (or game
 * over after ico_unused_game_over) and arms Options' return on the page,
 * reopened once the title's menu is up or let go after its bound; a start
 * that never reaches its stage fails (on the title at once, elsewhere
 * through the title's request); the art row needs a model viewer row too
 * (refused, logged, when the viewer offers none), and its start opens the
 * viewer's list on the viewer's first art row (from the title only; the
 * list's refusal logged); the install hands Settings the host.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* the game's tables, defined here: stageData and exitData are const in
   the game's headers (as port/data/gen's table_desc.c does) */
#define stageData stageData_header_decl
#define exitData exitData_header_decl
#include "typedef.h"
#undef stageData
#undef exitData

#include "adpcm_init.h"
#include "camera-editor.h"
#include "gamesys.h"
#include "s_init.h"

#include "df_pack.h"
#include "ico_unused.h"
#include "settings.h"
#include "../../core/region.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: FAIL ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- the game's side ------------------------------------------------------ */

StgPre stageData[110];
/* the European disc's 261 rows (a room's start reads them for its
   entrance), filled by fakeTables: exitData[0] NOT the origin (every disc
   has the origin there), so a placement that copies the row as stored
   instead of negating it fails, and one entrance row into the climbing
   stage that the start must not use */
ExitData exitData[261];
int stage_no = 1;
int systemStatus[12] = {1, 1}; /* PAL, frame step 1 */
int current_layout_id;
struct SqEntry *titleAdpcm;
int gFlagGameClear;
char gameSysMainSaveBuff[25596];
void *gameSysMemoryFuncList[1];
static unsigned char s_gflags[400];
static unsigned int s_ticks;
static int s_world, s_loads, s_saves;
static int s_entered = -1, s_simpleTo = -1, s_fade = 2;

/* the records a start wrote (gamesysObjInfoPosNewStageSet), in order */
typedef struct {
    int label, kind, stage;
    float pos[3], rot[3];
} Placed;

static Placed s_placed[8];
static int s_nPlaced;

/* the boy unused_live.c follows in a room's log: his root, his ground and
   fall heights, the floor the game's probe finds (s_hasFloor) */
GObj *boyGObj;
int mpegPlay;
int stageManagerFreeResourceFlag;
static GObj s_boy;
static Sub15C s_boySub;
/* the title boy's actor, his weapon and held item (act-game.c's flag 1) */
static Act s_boyAct;
static GObj s_weapon, s_item;
static float s_boyPos[3];
static int s_hasFloor;
static float s_floorY;

/* the girl the camera-zone move takes with the boy (girlGObj, NULL when
   she is not up), and the moves made (SetDirectRootPosition) */
GObj *girlGObj;
static GObj s_girl;
static Sub15C s_girlSub;

typedef struct {
    GObj *who;
    float pos[3];
} Moved;

static Moved s_moved[4];
static int s_nMoved;

/* the stage's camera set (camera-ico2.c CameraSetCameraSet's
   TopCameraSetDataOfCurrentStage: a CamSetFile head, then the groups,
   centre and half-size stored negated), NULL in testRooms; each group's
   floor under its centre (s_zoneFloorOn, s_zoneFloorY) and the probes
   from its box's top */
void *TopCameraSetDataOfCurrentStage;
#define ZONES 5

static struct {
    CamSetFile head;
    CamGroup g[ZONES];
} s_camSet;

static int s_zoneFloorOn[ZONES];
static float s_zoneFloorY[ZONES];
static int s_zoneProbed[ZONES];

int gflagChk(int i)
{
    return s_gflags[i];
}

void gflagOn(int i)
{
    s_gflags[i] = 1;
}

void gflagOff(int i)
{
    s_gflags[i] = 0;
}

void gamesysMemorySave(void **tbl, void *mem, void *arg)
{
    (void)tbl;
    (void)arg;
    memcpy(mem, &s_world, sizeof(s_world));
    s_saves++;
}

void gamesysMemoryLoad(void **tbl, void *mem, void *arg)
{
    (void)tbl;
    (void)arg;
    memcpy(&s_world, mem, sizeof(s_world));
    s_loads++;
}

GamesysObjInfo *gamesysObjInfoPosNewStageSet(int no, int kind, int stage, float *pos, float *rot)
{
    if (s_nPlaced < (int)(sizeof(s_placed) / sizeof(s_placed[0]))) {
        Placed *p = &s_placed[s_nPlaced];
        p->label = no;
        p->kind = kind;
        p->stage = stage;
        memcpy(p->pos, pos, sizeof(p->pos));
        memcpy(p->rot, rot, sizeof(p->rot));
    }
    s_nPlaced++;
    return NULL;
}

/* no record yet: only asked for a row inside the stage's own label range */
GamesysObjInfo *gamesysObjInfoGet(int kind, int no)
{
    (void)kind;
    (void)no;
    return NULL;
}

void GetRootPosition(void *pos, GObj *obj)
{
    float *p = pos;
    CHECK(obj == &s_boy, "the root asked of the boy");
    p[0] = s_boyPos[0];
    p[1] = s_boyPos[1];
    p[2] = s_boyPos[2];
    p[3] = 1.0f;
}

/* the game's probe: a ray from pos 10000 down (+y), the floor's point in
   pt[2] */
void GetLowerPlaneCollision(ClipWork *w, float *pos)
{
    static int floorElem;
    w->pt[2][0] = pos[0];
    w->pt[2][2] = pos[2];
    /* under a camera group's world centre (x, z): its floor; a probe from
       the box's world top, -(centre.y + half.y), counted */
    for (int i = 0; TopCameraSetDataOfCurrentStage != NULL && i < s_camSet.head.count; i++) {
        const CamGroup *g = &s_camSet.g[i];
        if (pos[0] != -g->center[0] || pos[2] != -g->center[2]) {
            continue;
        }
        if (pos[1] == -(g->center[1] + g->range[1])) {
            s_zoneProbed[i]++;
        }
        const int hit = s_zoneFloorOn[i] && s_zoneFloorY[i] >= pos[1];
        w->floor.elem = hit ? &floorElem : NULL;
        w->pt[2][1] = hit ? s_zoneFloorY[i] : pos[1] + 10000.0f;
        return;
    }
    CHECK(pos[0] == s_boyPos[0] && pos[1] == s_boyPos[1] && pos[2] == s_boyPos[2],
          "the probe from the boy's root or under a camera group's centre (%.0f %.0f %.0f)",
          (double)pos[0], (double)pos[1], (double)pos[2]);
    w->floor.elem = s_hasFloor ? &floorElem : NULL;
    w->pt[2][1] = s_hasFloor ? s_floorY : pos[1] + 10000.0f;
}

void SetDirectRootPosition(GObj *self, void *v)
{
    const float *p = v;
    if (s_nMoved < (int)(sizeof(s_moved) / sizeof(s_moved[0]))) {
        s_moved[s_nMoved].who = self;
        memcpy(s_moved[s_nMoved].pos, p, sizeof(s_moved[0].pos));
    }
    s_nMoved++;
    if (self == &s_boy) {
        memcpy(s_boyPos, p, sizeof(s_boyPos));
    }
}

/* the record a start wrote for label/kind, NULL none */
static const Placed *placedOf(int label, int kind)
{
    for (int i = 0; i < s_nPlaced && i < (int)(sizeof(s_placed) / sizeof(s_placed[0])); i++) {
        if (s_placed[i].label == label && s_placed[i].kind == kind) {
            return &s_placed[i];
        }
    }
    return NULL;
}

/* the record at the room's start: -exitData[0].pos, exitData[0].rot in
   radians, on the room's stage */
static int atStart(const Placed *p, int stage)
{
    return p != NULL && p->stage == stage && p->pos[0] == -exitData[0].pos[0] &&
           p->pos[1] == -exitData[0].pos[1] && p->pos[2] == -exitData[0].pos[2] &&
           p->rot[0] == exitData[0].rot[0] * 0.017453292f &&
           p->rot[1] == exitData[0].rot[1] * 0.017453292f &&
           p->rot[2] == exitData[0].rot[2] * 0.017453292f;
}

/* debug.c's Stage Select way in: the boy's records go to the stage */
void ico_host_enter_test_stage(int stage)
{
    s_entered = stage;
    s_world = 100 + stage;
}

/* the movie row's: unused_movie_test checks them */
int mpegPlayReturnStage;
unsigned int stage_after_skipping_demo;

void stgmgrForceSwitchWithFade(int stage, float fadeIn, float fadeOut)
{
    (void)stage;
    (void)fadeIn;
    (void)fadeOut;
}

int RequestStageChangeSimple(int no, float speed, float wait, unsigned char r, unsigned char gr,
                             unsigned char b)
{
    (void)speed;
    (void)wait;
    (void)r;
    (void)gr;
    (void)b;
    s_simpleTo = no;
    return 0;
}

unsigned int ico_host_main_ticks(void)
{
    return s_ticks;
}

int lt_fade_status(void)
{
    return s_fade;
}

static int s_switched = -1;

void lt_switch_layout(int no)
{
    s_switched = no;
}

/* --- DATA.DF, per disc ------------------------------------------------------ */

static const char *s_slug = "pal";
static int s_dfAsked;
static int s_vfsByte;

IcoVfs *ico_vfs_disc(void)
{
    return (IcoVfs *)(void *)&s_vfsByte;
}

static int isPal(void)
{
    return strcmp(s_slug, "pal") == 0;
}

static int isDemo(void)
{
    return strcmp(s_slug, "jun26") == 0;
}

/* the directories the plan read from the ISOs (plan.md D2): the European
   disc holds the three packs and both PAL attract films, the 104-row discs
   the three packs and advertise.pss only, the June 2001 demo none */
int ico_df_has(IcoVfs *vfs, const char *name)
{
    (void)vfs;
    s_dfAsked++;
    if (isDemo()) {
        return 0;
    }
    if (strcmp(name, "STGBOSS_TEST.DF") == 0 || strcmp(name, "STG8TEST.DF") == 0 ||
        strcmp(name, "STGLOG.DF") == 0) {
        return 1;
    }
    if (strcmp(name, "pal_advertise576.pss") == 0) {
        return isPal();
    }
    return 0;
}

int ico_df_find_member(IcoVfs *vfs, const char *name, IcoDfMember *out)
{
    (void)vfs;
    s_dfAsked++;
    memset(out, 0, sizeof(*out));
    if (!isDemo() && strcmp(name, "object/sdf/e3/st13b/model/copy_right.p2o") == 0) {
        /* the member's id in each disc's STGLOG.DF (read from the ISOs) */
        const int ntsc = strcmp(s_slug, "ntsc") == 0, aug6 = strcmp(s_slug, "aug6") == 0;
        out->id = isPal() ? 1595 : ntsc ? 1546 : aug6 ? 1541 : 1579;
        return 0;
    }
    return -1;
}

/* --- the model viewer --------------------------------------------------------- */

/* its first art row (the table's last three rows; the European disc has the
   copyright only), -1 when it offers none */
static int s_artRow = -2;
static int s_listAt = -1;
static int s_listRefuse;

int mv_member_first_row(void)
{
    if (s_artRow != -2) {
        return s_artRow;
    }
    return isDemo() ? -1 : isPal() ? 25 : 23;
}

int ico_mv_models_enter_at(int row)
{
    if (s_listRefuse) {
        return -1;
    }
    s_listAt = row;
    return 91;
}

/* --- Settings ------------------------------------------------------------- */

static const UiUnusedHost *s_host;
static int s_titleReturn = -2, s_decided, s_reopened = -1;

void ui_settings_set_unused_host(const UiUnusedHost *host)
{
    s_host = host;
}

void ui_settings_title_return(int page)
{
    s_titleReturn = page;
}

int ui_settings_title_decided(void)
{
    return s_decided;
}

int ui_settings_reopen_page(int page)
{
    s_reopened = page;
    return 77;
}

/* --- the log --------------------------------------------------------------- */

static char s_dir[1024] = ".";
static int s_errSaved = -1;
static char s_log[8192];

static void logStart(void)
{
    char p[1100];
    snprintf(p, sizeof(p), "%s/unused_content_test_stderr.txt", s_dir);
    fflush(stderr);
    s_errSaved = dup(2);
    FILE *f = freopen(p, "wb", stderr);
    (void)f;
}

static const char *logEnd(void)
{
    char p[1100];
    snprintf(p, sizeof(p), "%s/unused_content_test_stderr.txt", s_dir);
    fflush(stderr);
    dup2(s_errSaved, 2);
    close(s_errSaved);
    clearerr(stderr);
    s_log[0] = '\0';
    FILE *f = fopen(p, "rb");
    if (f) {
        size_t got = fread(s_log, 1, sizeof(s_log) - 1, f);
        s_log[got] = '\0';
        fclose(f);
    }
    fputs(s_log, stdout); /* the lines, in the test's output */
    return s_log;
}

/* --- the tests ------------------------------------------------------------- */

static void setRow(int stage, const char *name, const char *dataFile, int labelTop, int labelEnd)
{
    if (stage <= 0 || stage >= (int)(sizeof(stageData) / sizeof(stageData[0]))) {
        return;
    }
    snprintf(stageData[stage].name, sizeof(stageData[stage].name), "%s", name);
    snprintf(stageData[stage].dataFile, sizeof(stageData[stage].dataFile), "%s", dataFile);
    stageData[stage].labelTop = labelTop;
    stageData[stage].labelEnd = labelEnd;
}

/* the disc's rows at its own numbers, the European data: boss_test's
   label range shortened to 10..19, 8a_TEST's to 30..39 (the boy's, the
   girl's and the sword's records lie below both, as on the discs); both
   rooms have the boy (attrTop), the climbing room the girl too (flag0) */
static void fakeTables(void)
{
    memset(stageData, 0, sizeof(stageData));
    memset(exitData, 0, sizeof(exitData));
    setRow(1, "logo", "STGLOG", 0, 0);
    setRow(ico_region_stage(59), "memoryPAL576", "#REF!", 0, 0);
    setRow(ico_region_stage(88), "boss_test", "STGBOSS_TEST", 10, 20);
    setRow(ico_region_stage(91), "8a_TEST", "STG8TEST", 30, 40);
    if (ico_region_stage(88) > 0) {
        stageData[ico_region_stage(88)].attrTop = 1;
    }
    if (ico_region_stage(91) > 0) {
        stageData[ico_region_stage(91)].attrTop = 1;
        stageData[ico_region_stage(91)].flag0 = 1;
        /* the climbing room's entrance (exitData 260 on the European disc:
           stored (3080, 2606, -1899), rot 0) at row 7 */
        exitData[7].pos[0] = 3080.0f;
        exitData[7].pos[1] = 2606.0f;
        exitData[7].pos[2] = -1899.0f;
        exitData[7].nextStage = ico_region_stage(91);
    }
    exitData[0].pos[0] = 12.0f;
    exitData[0].pos[1] = -34.0f;
    exitData[0].pos[2] = 56.0f;
    exitData[0].rot[1] = 90.0f;
    s_boy.dobj = &s_boySub;
    s_girl.dobj = &s_girlSub;
}

static void testStages(void)
{
    const int pal = isPal(), demo = isDemo();
    const int want[ICO_UNUSED_COUNT] = {pal    ? 88
                                        : demo ? 85
                                               : 86,
                                        pal    ? 91
                                        : demo ? 88
                                               : 89,
                                        pal ? 59 : -1, 1};
    for (int r = 0; r < ICO_UNUSED_COUNT; r++) {
        CHECK(ico_unused_stage(r) == want[r], "%s: row %d at stage %d, want %d", s_slug, r,
              ico_unused_stage(r), want[r]);
        CHECK(ico_unused_row(r) != NULL && ico_unused_row(r)->name != NULL, "row %d named", r);
    }
    CHECK(ico_unused_row(-1) == NULL && ico_unused_row(ICO_UNUSED_COUNT) == NULL &&
              ico_unused_stage(ICO_UNUSED_COUNT) == -1,
          "rows out of range");
    /* the table's packs are the European stage rows' (port/data/gen/pal) */
    CHECK(strcmp(ico_unused_row(ICO_UNUSED_ARENA)->pack, "STGBOSS_TEST.DF") == 0 &&
              strcmp(ico_unused_row(ICO_UNUSED_CLIMB)->pack, "STG8TEST.DF") == 0 &&
              ico_unused_row(ICO_UNUSED_MOVIE)->pack == NULL &&
              strcmp(ico_unused_row(ICO_UNUSED_MOVIE)->loose, "pal_advertise576.pss") == 0 &&
              strcmp(ico_unused_row(ICO_UNUSED_ART)->pack, "STGLOG.DF") == 0 &&
              strcmp(ico_unused_row(ICO_UNUSED_ART)->member,
                     "object/sdf/e3/st13b/model/copy_right.p2o") == 0,
          "the rows' data");
}

static void testAvailable(void)
{
    const int pal = isPal(), demo = isDemo();
    const int want[ICO_UNUSED_COUNT] = {!demo, !demo, pal, !demo};

    /* no engine: nothing, and a start fails */
    ico_unused_set_engine(NULL);
    logStart();
    for (int r = 0; r < ICO_UNUSED_COUNT; r++) {
        CHECK(!ico_unused_available(r), "no engine: row %d not available", r);
    }
    CHECK(ico_unused_start(ICO_UNUSED_ARENA) == -1, "no engine: the start fails");
    CHECK(strstr(logEnd(), "unused: failed: no engine in this program") != NULL,
          "no engine: logged");

    /* the engine (the install hands Settings its host) */
    s_host = NULL;
    ico_unused_engine_install();
    CHECK(s_host != NULL && s_host->available != NULL && s_host->open != NULL,
          "the install sets Settings' host");
    s_dfAsked = 0;
    logStart();
    for (int r = 0; r < ICO_UNUSED_COUNT; r++) {
        CHECK(ico_unused_available(r) == want[r], "%s: row %d available %d, want %d", s_slug, r,
              ico_unused_available(r), want[r]);
        CHECK(s_host == NULL || s_host->available(r) == want[r], "the host's answer, row %d", r);
    }
    const char *log = logEnd();
    const int asked = s_dfAsked;
    for (int r = 0; r < ICO_UNUSED_COUNT; r++) {
        (void)ico_unused_available(r);
    }
    CHECK(s_dfAsked == asked, "asked once per disc (%d, then %d)", asked, s_dfAsked);
    if (demo) {
        CHECK(strstr(log, "its DATA.DF holds no STGBOSS_TEST.DF") != NULL &&
                  strstr(log, "no attract movie (576 lines) row on") != NULL,
              "the demo's refusals logged");
    } else if (!pal) {
        CHECK(strstr(log, "no attract movie (576 lines) row on") != NULL &&
                  strstr(log, "European stage 59") != NULL,
              "the movie: no stage row on this disc, logged");
    }

    /* a start the disc does not have: refused before the engine */
    if (!want[ICO_UNUSED_MOVIE]) {
        logStart();
        CHECK(ico_unused_start(ICO_UNUSED_MOVIE) == -1 && !ico_unused_active(),
              "the movie refused");
        CHECK(strstr(logEnd(), "unused: failed: attract movie (576 lines) is not on this disc") !=
                  NULL,
              "the refusal logged");
    }

    /* the art without a model viewer row: refused, logged (a new engine
       asks again) */
    if (!demo) {
        s_artRow = -1;
        ico_unused_engine_install();
        logStart();
        CHECK(!ico_unused_available(ICO_UNUSED_ART), "the art without a viewer row: refused");
        CHECK(strstr(logEnd(), "the model viewer offers none of its models") != NULL,
              "the art without a viewer row: logged");
        s_artRow = -2;
        ico_unused_engine_install();
    }

    /* a stage row that names another pack: the row is refused (a new engine
       asks again) */
    if (!demo) {
        snprintf(stageData[ico_unused_stage(ICO_UNUSED_CLIMB)].dataFile, 32, "STGNOCD_OTHER");
        ico_unused_engine_install();
        logStart();
        CHECK(!ico_unused_available(ICO_UNUSED_CLIMB), "another pack: refused");
        CHECK(strstr(logEnd(), "names the pack STGNOCD_OTHER.DF, not STG8TEST.DF") != NULL,
              "another pack: logged");
        snprintf(stageData[ico_unused_stage(ICO_UNUSED_CLIMB)].dataFile, 32, "STG8TEST");
        ico_unused_engine_install();
    }
}

/* the title with no stage change, the clock at t */
static void onTitle(unsigned int t)
{
    stage_no = ico_region_cur()->stage_title;
    systemStatus[6] = 0;
    s_ticks = t;
}

static void testRooms(void)
{
    static struct AdpcmStreamTag stream;
    static struct SqEntry theme;
    const IcoRegionInfo *ri = ico_region_cur();
    const int arena = ico_unused_stage(ICO_UNUSED_ARENA);
    const int climb = ico_unused_stage(ICO_UNUSED_CLIMB);

    if (isDemo()) {
        onTitle(10);
        CHECK(ico_unused_start(ICO_UNUSED_ARENA) == -1 && s_entered == -1 && !ico_unused_active(),
              "the demo: no room starts");
        return;
    }

    /* off the title: refused, nothing kept */
    stage_no = 11;
    systemStatus[6] = 0;
    s_saves = 0;
    logStart();
    CHECK(ico_unused_start(ICO_UNUSED_ARENA) == -1 && s_entered == -1 && s_saves == 0 &&
              !ico_unused_active(),
          "off the title: refused");
    CHECK(strstr(logEnd(), "unused: failed: not on the title") != NULL, "off the title: logged");

    /* the arena from the title */
    onTitle(1000);
    s_world = 7;
    gflagOn(5);
    theme.stream = &stream;
    stream.fadeStep = 0;
    titleAdpcm = &theme;
    s_nPlaced = 0;
    logStart();
    const int to = ico_unused_start(ICO_UNUSED_ARENA);
    const char *log = logEnd();
    CHECK(to == ri->layout_scene && ico_unused_active() && ico_unused_running() == ICO_UNUSED_ARENA,
          "the arena starts (layout %d, want %d)", to, ri->layout_scene);
    CHECK(s_entered == arena && s_saves == 1 && stream.fadeStep == 0x40 && titleAdpcm == NULL,
          "entered through debug.c (%d), the state kept, the theme faded", s_entered);
    /* the game's no-entrance start: the boy and the sword at
       -exitData[0].pos (here (-12, 34, -56): the row is stored negated),
       rot 90 degrees in radians; no girl (the arena's flag0 is 0) */
    {
        const Placed *boy = placedOf(ri->credits_boy_label, 1);
        CHECK(atStart(boy, arena), "the arena's boy at -exitData[0] (%.0f %.0f %.0f, rot %f)",
              boy ? (double)boy->pos[0] : 0.0, boy ? (double)boy->pos[1] : 0.0,
              boy ? (double)boy->pos[2] : 0.0, boy ? (double)boy->rot[1] : 0.0);
        CHECK(atStart(placedOf(ri->credits_boy_label + 1, 14), arena),
              "the arena's sword at -exitData[0]");
        CHECK(placedOf(ri->girl_label, 2) == NULL && s_nPlaced == 2,
              "the arena: no girl (%d records)", s_nPlaced);
    }
    char want[512];
    snprintf(want, sizeof(want), "unused: test room arena (stage %d on this disc) started", arena);
    CHECK(strstr(log, want) != NULL, "the start logged");
    snprintf(want, sizeof(want),
             "unused: arena: the boy at the room's start (-12, 34, -56), the game's no-entrance "
             "point (exitData[0]): the room has no entrance row; placed: the boy (record %d), "
             "the sword (record %d)\n",
             ri->credits_boy_label, ri->credits_boy_label + 1);
    CHECK(strstr(log, want) != NULL, "the arena's placement logged (want \"%s\")", want);

    /* a second start while it runs */
    CHECK(ico_unused_start(ICO_UNUSED_CLIMB) == -1 && ico_unused_running() == ICO_UNUSED_ARENA,
          "a second start refused");

    /* the stage up: the boy's line at once (no boy yet), then every 30
       Main ticks (the boy on the floor 71 under his root) */
    stage_no = arena;
    systemStatus[6] = 0;
    s_ticks = 1300;
    boyGObj = NULL;
    logStart();
    ico_unused_host_poll();
    log = logEnd();
    CHECK(strstr(log, "unused: the test room's stage") != NULL &&
              strstr(log, "unused: arena tick 0: no boy (the stage has none up)\n") != NULL,
          "the stage up: the boy's first line");
    boyGObj = &s_boy;
    s_boyPos[0] = 100.0f;
    s_boyPos[1] = -71.0f;
    s_boyPos[2] = 200.0f;
    s_boySub.ctrl.groundHeight = 71.0f;
    s_boySub.ctrl.fallHeight = 5.0f;
    s_hasFloor = 1;
    s_floorY = 0.0f;
    s_ticks = 1315;
    logStart();
    ico_unused_host_poll();
    log = logEnd();
    CHECK(strstr(log, "unused: arena tick") == NULL, "no line before 30 ticks");
    /* the camera-zone check waits for the boy, then runs once: no camera
       set here (testZones has them) */
    CHECK(strstr(log, "unused: arena: no camera set loaded (0 groups): the boy stays at (100, "
                      "-71, 200)\n") != NULL &&
              s_nMoved == 0,
          "the zone check once the boy is up, no camera set");
    s_ticks = 1335;
    logStart();
    ico_unused_host_poll();
    CHECK(strstr(logEnd(), "unused: arena tick 35: the boy at (100, -71, 200), ground height 71 "
                           "(the floor 0 under him, 71 below), fall height 5\n") != NULL,
          "the boy's line at tick 35");
    s_ticks = 1350;
    logStart();
    ico_unused_host_poll();
    CHECK(strstr(logEnd(), "unused: arena tick") == NULL, "the next line due at 60");
    /* a stage change under way (the room's stage keeps playing through
       it): no probe, the line says why */
    s_ticks = 1599;
    systemStatus[6] = 1;
    logStart();
    ico_unused_host_poll();
    CHECK(strstr(logEnd(), "unused: arena tick 299: no boy (a stage change under way)\n") != NULL,
          "a line at tick 299, no probe during a stage change");
    systemStatus[6] = 0;
    s_ticks = 1601;
    logStart();
    ico_unused_host_poll();
    CHECK(strstr(logEnd(), "unused: arena tick") == NULL, "no line past 300 ticks");

    /* then End Game: the title's stage entered */
    gflagOff(5);
    s_ticks = 1500; /* 500 ticks of 20 ms (PAL, one field a tick): 10 s */
    s_loads = 0;
    s_titleReturn = -2;
    logStart();
    ico_unused_stage_enter(arena); /* not the title: nothing */
    CHECK(ico_unused_active(), "another stage keeps the flag");
    onTitle(1500);
    ico_unused_stage_enter(ri->stage_title);
    log = logEnd();
    CHECK(!ico_unused_active() && s_loads == 1 && s_world == 7 && gflagChk(5) == 1,
          "back at the title: the state put back, the flag cleared");
    CHECK(strstr(log, "unused: back at the title after 10.0 s (End Game)") != NULL,
          "the return logged with its time");
    CHECK(s_titleReturn == UI_PAGE_UNUSED, "Options armed to come back on the page (%d)",
          s_titleReturn);

    /* the title's menu up: reopened, once */
    s_decided = 1;
    s_fade = 2;
    current_layout_id = ri->layout_title_new;
    s_reopened = -1;
    s_switched = -1;
    ico_unused_host_poll();
    CHECK(s_reopened == UI_PAGE_UNUSED && s_switched == 77, "the page reopened (%d, layout %d)",
          s_reopened, s_switched);
    s_reopened = -1;
    ico_unused_host_poll();
    CHECK(s_reopened == -1, "reopened once");

    /* the climbing room: the boy, the girl (its flag0) and the sword at
       the room's start, not at its entrance row (row 7 here, exitData 260
       on the European disc: a walkway with no model) */
    onTitle(2000);
    s_nPlaced = 0;
    logStart();
    CHECK(ico_unused_start(ICO_UNUSED_CLIMB) == ri->layout_scene && s_entered == climb,
          "the climbing room starts");
    log = logEnd();
    CHECK(atStart(placedOf(ri->credits_boy_label, 1), climb) &&
              atStart(placedOf(ri->girl_label, 2), climb) &&
              atStart(placedOf(ri->credits_boy_label + 1, 14), climb) && s_nPlaced == 3,
          "the climbing room: the boy, the girl and the sword at -exitData[0] (%d records)",
          s_nPlaced);
    for (int i = 0; i < s_nPlaced && i < 8; i++) {
        CHECK(s_placed[i].pos[0] != -3080.0f && s_placed[i].pos[1] != -2606.0f,
              "no record at the entrance row");
    }
    snprintf(want, sizeof(want),
             "unused: climbing: the boy at the room's start (-12, 34, -56), the game's "
             "no-entrance point (exitData[0]); the game's own entrance row 7 (-3080, -2606, "
             "1899) not used: it leads to a walkway with no model; placed: the boy (record %d), "
             "the girl (record %d), the sword (record %d)\n",
             ri->credits_boy_label, ri->girl_label, ri->credits_boy_label + 1);
    CHECK(strstr(log, want) != NULL, "the climbing room's placement logged (want \"%s\")", want);

    /* the stage up, the poll window passed between two polls, then a game
       over: the boy's line with "game over"; Continue goes to the title */
    stage_no = climb;
    s_hasFloor = 0;
    s_boyPos[1] = -2533.0f;
    s_boySub.ctrl.fallHeight = 2533.0f;
    logStart();
    ico_unused_host_poll();
    CHECK(strstr(logEnd(), "unused: climbing tick 0: the boy at (100, -2533, 200), ground height "
                           "71 (no floor below), fall height 2533\n") != NULL,
          "the climbing room's first line, no floor");
    s_ticks = 2400;
    logStart();
    ico_unused_host_poll();
    CHECK(strstr(logEnd(), "unused: climbing tick") == NULL, "no line once 300 ticks passed");
    logStart();
    ico_unused_game_over();
    log = logEnd();
    CHECK(strcmp(ico_unused_left_by(), "game over") == 0, "a game over noted");
    CHECK(strstr(log, "unused: game over in the test room climbing") != NULL &&
              strstr(log, "unused: climbing game over: the boy at (100, -2533, 200), ground "
                          "height 71 (no floor below), fall height 2533\n") != NULL,
          "the game over's boy line");
    boyGObj = NULL;
    logStart();
    onTitle(2100);
    ico_unused_stage_enter(ri->stage_title);
    CHECK(strstr(logEnd(), "(game over)") != NULL && !ico_unused_active(),
          "the game over's return logged");
    CHECK(strcmp(ico_unused_left_by(), "End Game") == 0, "the reason cleared");
    /* the title's menu never comes: let go after the bound */
    s_decided = 0;
    s_titleReturn = -2;
    s_reopened = -1;
    s_ticks = 2100 + 251;
    ico_unused_host_poll();
    CHECK(s_titleReturn == -1 && s_reopened == -1, "the return let go after its bound (%d)",
          s_titleReturn);

    /* a start that never leaves the title: put back at once.  Its title
       boy holds a weapon and an item: as ACTGame_SetActors_Debug's flag 1
       pass, the weapon's and the item's records go to the start with the
       boy's, and there is no sword */
    onTitle(3000);
    s_weapon.labelId = 200;
    s_weapon.kind = 15;
    s_item.labelId = 300;
    s_item.kind = 20;
    s_boyAct.weapon = &s_weapon;
    s_boyAct.curItem = &s_item;
    s_boy.act = &s_boyAct;
    boyGObj = &s_boy;
    s_nPlaced = 0;
    logStart();
    ico_unused_start(ICO_UNUSED_ARENA);
    log = logEnd();
    {
        const Placed *w = placedOf(200, 15), *it = placedOf(300, 20);
        CHECK(atStart(placedOf(ri->credits_boy_label, 1), arena) && atStart(w, arena) &&
                  atStart(it, arena) && placedOf(ri->credits_boy_label + 1, 14) == NULL &&
                  s_nPlaced == 3 && s_placed[1].label == 200 && s_placed[2].label == 300,
              "a title boy with a weapon: the boy, his weapon and his item at the start, no "
              "sword (%d records)",
              s_nPlaced);
        snprintf(want, sizeof(want),
                 "placed: the boy (record %d), the boy's weapon (record 200, kind 15), the "
                 "boy's item (record 300, kind 20)\n",
                 ri->credits_boy_label);
        CHECK(strstr(log, want) != NULL, "the held weapon's placement logged (want \"%s\")", want);
    }
    s_boy.act = NULL;
    boyGObj = NULL;
    s_ticks = 3000 + 1501;
    logStart();
    ico_unused_host_poll();
    CHECK(strstr(logEnd(), "(fail-start)") != NULL && !ico_unused_active(),
          "fail-start on the title");
    /* one stuck on another stage: the title requested, put back on entry */
    onTitle(4000);
    ico_unused_start(ICO_UNUSED_CLIMB);
    stage_no = 50;
    s_simpleTo = -1;
    s_ticks = 4000 + 1501;
    ico_unused_host_poll();
    CHECK(s_simpleTo == ri->stage_title && ico_unused_active(), "the title requested (%d)",
          s_simpleTo);
    logStart();
    onTitle(5600);
    ico_unused_stage_enter(ri->stage_title);
    CHECK(strstr(logEnd(), "(fail-start)") != NULL && !ico_unused_active(),
          "fail-start through the title");
    s_decided = 0;
    s_ticks += 1000;
    ico_unused_host_poll(); /* the pending reopen let go */

    /* a game over then Quit: every Quit arm of la_game_over_continue
       (layout_action.c) calls ico_unused_game_over before it leaves, as
       Continue's do, so the boy's game-over line is written and the return
       reads "game over", not "End Game" */
    onTitle(7000);
    CHECK(ico_unused_start(ICO_UNUSED_ARENA) == ri->layout_scene, "the arena again");
    stage_no = arena;
    s_ticks = 7010;
    boyGObj = &s_boy;
    s_boyPos[1] = -71.0f;
    s_boySub.ctrl.fallHeight = 1200.0f;
    s_hasFloor = 1;
    ico_unused_host_poll(); /* the stage up */
    logStart();
    ico_unused_game_over(); /* the Quit arm's call */
    CHECK(strstr(logEnd(), "unused: arena game over: the boy at (100, -71, 200)") != NULL &&
              strcmp(ico_unused_left_by(), "game over") == 0,
          "Quit after a game over: the boy's game-over line, the reason game over");
    boyGObj = NULL;
    logStart();
    onTitle(7100);
    ico_unused_stage_enter(ri->stage_title);
    log = logEnd();
    CHECK(strstr(log, "(game over)") != NULL && strstr(log, "(End Game)") == NULL &&
              !ico_unused_active(),
          "Quit after a game over: the return logged as a game over");
    s_decided = 0;
    s_ticks += 1000;
    ico_unused_host_poll(); /* the pending reopen let go */

    /* the art: the model viewer's list on its first art row (the movie's
       start is unused_movie_test's) */
    onTitle(9000);
    s_entered = -1;
    if (ico_unused_available(ICO_UNUSED_ART)) {
        const int first = mv_member_first_row();
        s_listAt = -1;
        logStart();
        const int lay = ico_unused_start(ICO_UNUSED_ART);
        log = logEnd();
        CHECK(lay == 91 && s_listAt == first && !ico_unused_active() && s_entered == -1,
              "the art opens the list (layout %d, row %d, want 91 and %d)", lay, s_listAt, first);
        snprintf(want, sizeof(want), "the model viewer's list (layout 91) on its row %d", first);
        CHECK(strstr(log, want) != NULL, "the art's start logged");
        /* the list refused: the page stays */
        s_listRefuse = 1;
        logStart();
        CHECK(ico_unused_start(ICO_UNUSED_ART) == -1, "the list refused: -1");
        CHECK(strstr(logEnd(), "the model viewer's list did not open") != NULL,
              "the list's refusal logged");
        s_listRefuse = 0;
        /* off the title: refused, the list not asked */
        stage_no = 11;
        s_listAt = -1;
        logStart();
        CHECK(ico_unused_start(ICO_UNUSED_ART) == -1 && s_listAt == -1, "off the title: refused");
        CHECK(strstr(logEnd(), "unused: failed: not on the title") != NULL,
              "off the title: logged");
        onTitle(9100);
    } else {
        CHECK(isDemo(), "%s: the art is available on every retail disc", s_slug);
    }
}

/* a camera group at the world centre c, half-size r: stored negated, as
   the game stores them (a copy as stored misses every zone below) */
static void zone(int i, float cx, float cy, float cz, float rx, float ry, float rz, int floorOn,
                 float floorY)
{
    CamGroup *g = &s_camSet.g[i];
    memset(g, 0, sizeof(*g));
    snprintf(g->name, sizeof(g->name), "%d", i);
    g->center[0] = -cx;
    g->center[1] = -cy;
    g->center[2] = -cz;
    g->range[0] = rx;
    g->range[1] = ry;
    g->range[2] = rz;
    s_zoneFloorOn[i] = floorOn;
    s_zoneFloorY[i] = floorY;
}

/* a room from the title to its stage's first poll with the boy at
   (100, -71, 200) on the floor 0 (71 above it), the girl up or not; the
   poll's log copied to out; then back to the title */
static void zoneRun(int row, unsigned int t, int girlUp, char *out, size_t outSize)
{
    const IcoRegionInfo *ri = ico_region_cur();
    onTitle(t);
    boyGObj = &s_boy;
    s_boy.act = NULL;
    girlGObj = girlUp ? &s_girl : NULL;
    s_boyPos[0] = 100.0f;
    s_boyPos[1] = -71.0f;
    s_boyPos[2] = 200.0f;
    s_hasFloor = 1;
    s_floorY = 0.0f;
    s_boySub.ctrl.groundHeight = 71.0f;
    s_boySub.ctrl.fallHeight = 5.0f;
    s_nPlaced = 0;
    s_nMoved = 0;
    memset(s_zoneProbed, 0, sizeof(s_zoneProbed));
    CHECK(ico_unused_start(row) == ri->layout_scene, "the room starts (row %d)", row);
    stage_no = ico_unused_stage(row);
    s_ticks = t + 10;
    logStart();
    ico_unused_host_poll();
    snprintf(out, outSize, "%s", logEnd());
    onTitle(t + 20);
    ico_unused_stage_enter(ri->stage_title);
    CHECK(!ico_unused_active(), "back at the title (row %d)", row);
    s_decided = 0;
    s_ticks += 1000;
    ico_unused_host_poll(); /* the pending reopen let go */
    girlGObj = NULL;
}

/* the camera-zone rule (unused_live.c startInZone): a start inside a zone
   stays; one outside every zone moves, with the girl when she is up, to
   the lowest zone whose centre has a floor inside its box, at the root's
   own height above its floor; no such zone: he stays */
static void testZones(void)
{
    static char log[8192];
    char want[512];

    if (isDemo()) {
        return;
    }
    TopCameraSetDataOfCurrentStage = &s_camSet;

    /* the arena: its zone 0 (world centre (-375, 50, 0), the disc's) holds
       the start; group 0 here is stored at the root itself, so a test of
       the stored box against the root un-negated would name it */
    s_camSet.head.count = 2;
    zone(0, -100.0f, 71.0f, -200.0f, 10.0f, 10.0f, 10.0f, 1, 80.0f);
    zone(1, -375.0f, 50.0f, 0.0f, 1645.0f, 170.0f, 1200.0f, 1, 0.0f);
    zoneRun(ICO_UNUSED_ARENA, 20000, 0, log, sizeof(log));
    CHECK(strstr(log, "unused: arena: the start is inside camera zone 1 of 2 (centre (-375, 50, "
                      "0))\n") != NULL &&
              s_nMoved == 0,
          "the arena: inside zone 1, no move (%d moves)", s_nMoved);

    /* the climbing room (the disc's numbers, world): zone 0 the lowest,
       its floor below its box; zone 1 the thin side zone 47 and zone 3
       the big group 20, the same box bottom (-720), 1's floor 103 lower:
       the larger floor plan wins; zone 2 higher up; zone 4 no floor */
    s_camSet.head.count = 5;
    zone(0, 500.0f, -600.0f, 500.0f, 100.0f, 100.0f, 100.0f, 1, 0.0f);
    zone(1, 1847.0f, -1355.0f, -534.0f, 475.0f, 635.0f, 85.0f, 1, -1221.0f);
    zone(2, -2090.0f, -2210.0f, 4796.0f, 850.0f, 210.0f, 957.0f, 1, -2167.0f);
    zone(3, 1111.0f, -1355.0f, 81.0f, 1216.0f, 635.0f, 529.0f, 1, -1324.0f);
    zone(4, 240.0f, -3320.0f, 678.0f, 103.0f, 635.0f, 111.0f, 0, 0.0f);
    zoneRun(ICO_UNUSED_CLIMB, 21000, 1, log, sizeof(log));
    snprintf(want, sizeof(want), "%s",
             "unused: climbing: the game's no-entrance point (100, -71, 200) is outside every "
             "camera zone (the lowest zone's centre 529 above it): the boy moved to zone 3's "
             "floor at (1111, -1395, 81) (71 above the floor y -1324); the girl with him\n");
    CHECK(strstr(log, want) != NULL, "the climbing room's move logged (want \"%s\")", want);
    CHECK(s_nMoved == 2 && s_moved[0].who == &s_boy && s_moved[1].who == &s_girl &&
              s_moved[0].pos[0] == 1111.0f && s_moved[0].pos[1] == -1395.0f &&
              s_moved[0].pos[2] == 81.0f &&
              memcmp(s_moved[0].pos, s_moved[1].pos, sizeof(s_moved[0].pos)) == 0,
          "the boy and the girl moved to zone 3's floor, 71 above it (%d moves)", s_nMoved);
    for (int i = 0; i < 5; i++) {
        CHECK(s_zoneProbed[i] == 1, "zone %d probed once from its box's top (%d)", i,
              s_zoneProbed[i]);
    }
    CHECK(strstr(log, "unused: climbing tick 0: the boy at (1111, -1395, 81), ground height 71 "
                      "(the floor -1324 under him, 71 below), fall height 5\n") != NULL,
          "the tick line after the move");

    /* the girl not up: the boy alone */
    zoneRun(ICO_UNUSED_CLIMB, 22000, 0, log, sizeof(log));
    CHECK(s_nMoved == 1 && s_moved[0].who == &s_boy &&
              strstr(log, "(71 above the floor y -1324)\n") != NULL,
          "no girl: the boy alone moved (%d moves)", s_nMoved);

    /* no zone with a floor in its box: he stays */
    s_camSet.head.count = 1;
    zoneRun(ICO_UNUSED_CLIMB, 23000, 1, log, sizeof(log));
    CHECK(strstr(log, "unused: climbing: no camera zone holds a floor under its centre: the boy "
                      "stays at (100, -71, 200)\n") != NULL &&
              s_nMoved == 0,
          "no zone with a floor: no move (%d moves)", s_nMoved);

    /* no boy in the log's window: the check never ran, and says so */
    onTitle(24000);
    boyGObj = NULL;
    CHECK(ico_unused_start(ICO_UNUSED_CLIMB) == ico_region_cur()->layout_scene,
          "the climbing room again");
    stage_no = ico_unused_stage(ICO_UNUSED_CLIMB);
    s_ticks = 24010;
    ico_unused_host_poll();
    s_ticks = 24010 + 301;
    logStart();
    ico_unused_host_poll();
    CHECK(strstr(logEnd(), "unused: climbing: the camera-zone check never ran (no boy in the "
                           "first 300 ticks)\n") != NULL,
          "the check that never ran logged");
    onTitle(24400);
    ico_unused_stage_enter(ico_region_cur()->stage_title);
    s_decided = 0;
    s_ticks += 1000;
    ico_unused_host_poll();
    TopCameraSetDataOfCurrentStage = NULL;
}

int main(int argc, char **argv)
{
    /* unused_content_test DIR [SLUG]: that disc's region */
    if (argc > 1) {
        snprintf(s_dir, sizeof(s_dir), "%s", argv[1]);
    }
    if (argc > 2) {
        s_slug = argv[2];
        CHECK(ico_region_select(argv[2]) == 0, "the %s region", argv[2]);
    }
    fakeTables();
    testStages();
    testAvailable();
    testRooms();
    testZones();
    if (failures) {
        fprintf(stderr, "unused_content_test %s: %d failure(s)\n", s_slug, failures);
        return 1;
    }
    printf("unused_content_test: ok (%s)\n", ico_region_cur()->disc_id);
    return 0;
}
