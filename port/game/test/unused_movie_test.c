/*
 * port/game/test/unused_movie_test.c
 *
 * Extras > Unused content's attract movie row (unused_live.c beginMovie and
 * its poll), CPU only, over stubs of the game, DATA.DF and Settings, one disc
 * per run (ctests unused_movie, unused_movie_<slug>).
 *
 * Checked: the row is on the European disc only (stage 59 and the loose
 * pal_advertise576.pss) and a start on another disc is refused before the
 * engine, no stage switch asked; a start off the title is refused; the start
 * on the title sets mpegPlayReturnStage to the title, fades the title theme,
 * clears the skip word, asks the stage switch to the movie's stage with
 * op.c's 256.0 / 4.0 fades, returns the scene layout, sets the flag and keeps
 * no game state; a second start is refused; the movie's stage up is logged;
 * the title's stage entered ends it with "movie ended" or, when Main set the
 * skip word, "skipped", clears the flag, arms Options' return on the page and
 * touches no game flag (nothing was kept); the page reopens once the title's
 * menu is up; a start that never reaches the stage fails (on the title at
 * once, elsewhere through the title's request, and when the title does not
 * come back after its bound).
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
   entrance), exitData[0] the origin as on every disc */
const ExitData exitData[261] = {[0] = {.pos = {0.0f, 0.0f, 0.0f}}};
GenGeo objLayout[64];
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
static int s_forced = -1;
static float s_forcedIn, s_forcedOut;
static int s_forcedCalls;
int mpegPlayReturnStage = 3;
unsigned int stage_after_skipping_demo = 0xFFFFFFFEu; /* an earlier film's skip */
static int s_placedLabel = -1, s_placedKind = -1, s_placedStage = -1;
static float s_placedPos[3];
/* the boy unused_live.c follows in a test room's log: none here (the
   movie starts no room) */
GObj *boyGObj;
int mpegPlay;
int stageManagerFreeResourceFlag;

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
    (void)rot;
    s_placedLabel = no;
    s_placedKind = kind;
    s_placedStage = stage;
    memcpy(s_placedPos, pos, sizeof(s_placedPos));
    return NULL;
}

GamesysObjInfo *gamesysObjInfoGet(int kind, int no)
{
    (void)kind;
    (void)no;
    return NULL;
}

void GetRootPosition(void *pos, GObj *obj)
{
    (void)obj;
    memset(pos, 0, 4 * sizeof(float));
}

void GetLowerPlaneCollision(ClipWork *w, float *pos)
{
    (void)pos;
    w->floor.elem = NULL;
}

/* the camera-zone move's (unused_live.c startInZone): no room starts here */
GObj *girlGObj;
void *TopCameraSetDataOfCurrentStage;

void SetDirectRootPosition(GObj *self, void *v)
{
    (void)self;
    (void)v;
}

/* debug.c's Stage Select way in: the boy's records go to the stage */
void ico_host_enter_test_stage(int stage)
{
    s_entered = stage;
    s_world = 100 + stage;
}

void ico_host_skip_entry_walk(void) {}

/* camera-ico2.c: the camera beside the boy's lines (no room starts here) */
int ico_host_camera_view(float *eye, float *at, int *mode, int *computed)
{
    for (int k = 0; k < 3; k++) {
        eye[k] = 0.0f;
        at[k] = 0.0f;
    }
    *mode = 0;
    *computed = 0;
    return -1;
}

/* StageManager.c: the stage switch the attract asks for */
void stgmgrForceSwitchWithFade(int stage, float fadeIn, float fadeOut)
{
    s_forced = stage;
    s_forcedIn = fadeIn;
    s_forcedOut = fadeOut;
    s_forcedCalls++;
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
        out->id = isPal() ? 1595 : 1544;
        return 0;
    }
    return -1;
}

/* --- the model viewer (the art row's start, unused_content_test's) -------- */

int mv_member_first_row(void)
{
    return isDemo() ? -1 : 23;
}

int ico_mv_models_enter_at(int row)
{
    (void)row;
    return -1;
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
    snprintf(p, sizeof(p), "%s/unused_movie_test_%s_stderr.txt", s_dir, s_slug);
    fflush(stderr);
    s_errSaved = dup(2);
    FILE *f = freopen(p, "wb", stderr);
    (void)f;
}

static const char *logEnd(void)
{
    char p[1100];
    snprintf(p, sizeof(p), "%s/unused_movie_test_%s_stderr.txt", s_dir, s_slug);
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

/* the title with no stage change, the clock at t */
static void onTitle(unsigned int t)
{
    stage_no = ico_region_cur()->stage_title;
    systemStatus[6] = 0;
    s_ticks = t;
}

static void testNotOnDisc(void)
{
    /* the film is the European disc's alone: the start is refused before
       the engine, no stage switch asked, no flag set */
    onTitle(100);
    logStart();
    CHECK(!ico_unused_available(ICO_UNUSED_MOVIE), "%s: no movie row", s_slug);
    CHECK(ico_unused_start(ICO_UNUSED_MOVIE) == -1 && s_forcedCalls == 0 && !ico_unused_active(),
          "%s: the start refused", s_slug);
    CHECK(strstr(logEnd(), "unused: failed: attract movie (576 lines) is not on this disc") != NULL,
          "%s: the refusal logged", s_slug);
}

static void testMovie(void)
{
    static struct AdpcmStreamTag stream;
    static struct SqEntry theme;
    const IcoRegionInfo *ri = ico_region_cur();
    const int movie = ico_unused_stage(ICO_UNUSED_MOVIE);
    const int title = ri->stage_title;
    char line[160];
    int reopenFrom;

    CHECK(movie == 59 && ico_unused_available(ICO_UNUSED_MOVIE), "the European stage 59 (%d)",
          movie);

    /* off the title: refused, nothing asked */
    stage_no = 11;
    systemStatus[6] = 0;
    logStart();
    CHECK(ico_unused_start(ICO_UNUSED_MOVIE) == -1 && s_forcedCalls == 0 && !ico_unused_active() &&
              mpegPlayReturnStage == 3,
          "off the title: refused");
    CHECK(strstr(logEnd(), "unused: failed: not on the title") != NULL, "off the title: logged");

    /* from the title */
    onTitle(1000);
    s_world = 7;
    s_saves = 0;
    gflagOn(5);
    theme.stream = &stream;
    stream.fadeStep = 0;
    titleAdpcm = &theme;
    logStart();
    const int to = ico_unused_start(ICO_UNUSED_MOVIE);
    const char *log = logEnd();
    CHECK(to == ri->layout_scene && ico_unused_active() && ico_unused_running() == ICO_UNUSED_MOVIE,
          "the movie starts (layout %d, want %d)", to, ri->layout_scene);
    CHECK(s_forced == movie && s_forcedCalls == 1 && s_forcedIn == 256.0f && s_forcedOut == 4.0f,
          "the switch to stage %d, fades %.1f / %.1f (op.c: 256 / 4)", s_forced, (double)s_forcedIn,
          (double)s_forcedOut);
    CHECK(mpegPlayReturnStage == title, "the return stage is the title (%d)", mpegPlayReturnStage);
    CHECK(stream.fadeStep == 0x40 && titleAdpcm == NULL, "the title theme faded");
    CHECK(stage_after_skipping_demo == 0, "the skip word cleared");
    CHECK(s_entered == -1 && s_saves == 0 && s_placedLabel == -1,
          "no test room entry, no state kept, no boy placed");
    CHECK(strstr(log, "unused: attract movie (576 lines) started from the title") != NULL,
          "the start logged");

    /* a second start while it runs */
    CHECK(ico_unused_start(ICO_UNUSED_MOVIE) == -1 && s_forcedCalls == 1, "a second start refused");
    CHECK(ico_unused_start(ICO_UNUSED_ARENA) == -1 && s_forcedCalls == 1 && s_entered == -1,
          "a room refused while the movie runs");

    /* the movie's stage up, then the title: a film that played out */
    systemStatus[6] = 1;
    stage_no = movie;
    s_ticks = 1100;
    logStart();
    ico_unused_host_poll(); /* the switch still running */
    CHECK(strstr(logEnd(), "is up") == NULL, "not up while the stage change runs");
    systemStatus[6] = 0;
    logStart();
    ico_unused_host_poll();
    snprintf(line, sizeof(line), "unused: the movie's stage %d is up", movie);
    CHECK(strstr(logEnd(), line) != NULL, "the movie's stage up, logged");
    gflagOff(5);
    s_titleReturn = -2;
    s_loads = 0;
    logStart();
    ico_unused_stage_enter(movie); /* not the title: nothing */
    CHECK(ico_unused_active(), "another stage keeps the flag");
    onTitle(1200);
    ico_unused_stage_enter(title);
    log = logEnd();
    CHECK(!ico_unused_active() && s_loads == 0 && s_world == 7 && gflagChk(5) == 0,
          "back at the title: the flag cleared, no game state put back (none kept)");
    CHECK(strstr(log, "unused: back at the title after ") != NULL &&
              strstr(log, " s (movie ended)") != NULL,
          "the return logged: movie ended");
    CHECK(s_titleReturn == UI_PAGE_UNUSED, "Options armed to come back on the page (%d)",
          s_titleReturn);

    /* the title's menu up: reopened, once */
    s_decided = 1;
    s_fade = 2;
    current_layout_id = ri->layout_title_new;
    s_reopened = -1;
    s_switched = -1;
    ico_unused_host_poll();
    reopenFrom = s_reopened;
    CHECK(reopenFrom == UI_PAGE_UNUSED && s_switched == 77, "the page reopened (%d, layout %d)",
          reopenFrom, s_switched);
    s_reopened = -1;
    ico_unused_host_poll();
    CHECK(s_reopened == -1, "reopened once");

    /* a skipped film: Main sets the word before the title is entered */
    onTitle(2000);
    s_forcedCalls = 0;
    CHECK(ico_unused_start(ICO_UNUSED_MOVIE) == ri->layout_scene && s_forcedCalls == 1,
          "a second film starts");
    stage_no = movie;
    ico_unused_host_poll();
    stage_after_skipping_demo = 0xFFFFFFFEu;
    logStart();
    onTitle(2100);
    ico_unused_stage_enter(title);
    CHECK(strstr(logEnd(), " s (skipped)") != NULL && !ico_unused_active(), "the skip logged");
    s_decided = 0;
    s_ticks = 2100 + 251;
    ico_unused_host_poll(); /* the pending reopen let go after its bound */

    /* a start that never leaves the title: put back at once */
    onTitle(3000);
    ico_unused_start(ICO_UNUSED_MOVIE);
    s_ticks = 3000 + 1501;
    logStart();
    ico_unused_host_poll();
    log = logEnd();
    CHECK(strstr(log, "unused: failed: the movie's stage 59 was not reached") != NULL &&
              strstr(log, "(fail-start)") != NULL && !ico_unused_active(),
          "fail-start on the title");
    s_ticks += 1000;
    ico_unused_host_poll();

    /* one stuck on another stage: the title requested, put back on entry */
    onTitle(4000);
    ico_unused_start(ICO_UNUSED_MOVIE);
    stage_no = 50;
    s_simpleTo = -1;
    s_ticks = 4000 + 1501;
    ico_unused_host_poll();
    CHECK(s_simpleTo == title && ico_unused_active(), "the title requested (%d)", s_simpleTo);
    logStart();
    onTitle(5600);
    ico_unused_stage_enter(title);
    CHECK(strstr(logEnd(), "(fail-start)") != NULL && !ico_unused_active(),
          "fail-start through the title");
    s_ticks += 1000;
    ico_unused_host_poll();

    /* the title never answers the request: let go after its bound */
    onTitle(7000);
    ico_unused_start(ICO_UNUSED_MOVIE);
    stage_no = 50;
    s_ticks = 7000 + 1501;
    ico_unused_host_poll();
    logStart();
    s_ticks += 1501;
    ico_unused_host_poll();
    CHECK(strstr(logEnd(), "(fail-start, the title did not come back)") != NULL &&
              !ico_unused_active(),
          "the title did not come back");
}

int main(int argc, char **argv)
{
    /* unused_movie_test DIR [SLUG]: that disc's region */
    if (argc > 1) {
        snprintf(s_dir, sizeof(s_dir), "%s", argv[1]);
    }
    if (argc > 2) {
        s_slug = argv[2];
        CHECK(ico_region_select(argv[2]) == 0, "the %s region", argv[2]);
    }
    memset(stageData, 0, sizeof(stageData));
    memset(objLayout, 0, sizeof(objLayout));
    ico_unused_engine_install();
    if (isPal()) {
        testMovie();
    } else {
        testNotOnDisc();
    }
    if (failures) {
        fprintf(stderr, "unused_movie_test %s: %d failure(s)\n", s_slug, failures);
        return 1;
    }
    printf("unused_movie_test: ok (%s)\n", ico_region_cur()->disc_id);
    return 0;
}
