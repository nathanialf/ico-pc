/*
 * port/game/test/credits_test.c
 *
 * The staff roll's port credit and the Extras credits' lock (package CRED), CPU only.
 *
 * The game's staffroll.c (its only path: every line, the port's included,
 * through font_Print's bitmap font) runs over a short table in the disc's
 * forms (the PAL ELF's staffRollNameData opens with
 * "{#FFFFFF80}{R} ICO Staff  ", its headings are "{R}< Game Design > ", its
 * names "{R}Fumito Ueda ", and it closes centred), with font_CheckAlign
 * recording each line the roll posts.  Checked: every disc line is posted,
 * then the port's, the heading and the name last, in the roll's heading and
 * name forms and right aligned; every line, the port's heading and name
 * included, is drawn by font_Print; the roll ends (staffRollStartFlag 0)
 * only after them; the port lines are ASCII and outside the range NULL.  Then
 * the lock: locked with no achievement and no key, unlocked with
 * unlock_credits=1 in the ini.  Last, the engine (credits_live.c) over
 * stubs of the game's side: a start whose stage never comes up fails after
 * its bound with the flag kept, requests the title, and puts the game's
 * state back as the title is entered (review finding 4); one that never
 * left the title is put back at once.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "DisplayFont.h"
#include "adpcm_init.h"
#include "achievements.h"
#include "config.h"
#include "ico_credits.h"
#include "staffroll.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- the game's side ------------------------------------------------------ */

static char s_l0[] = "{#FFFFFF80}{R} ICO Staff  ";
static char s_blank[] = " ";
static char s_l2[] = "{R}< Game Design > ";
static char s_l4[] = "{R}Fumito Ueda ";
static char s_l6[] = "{C}Presented by";
static char s_l8[] = "@ 2002 Sony Computer Entertainment Inc.";

/* two NULL entries at the end, as the disc's table has; the count is the
   table's less 2, as table_defs.c derives staffRollNameDataNum */
char *staffRollNameData[] = {s_l0, s_blank, s_l2, s_blank, s_l4, s_blank,
                             s_l6, s_blank, s_l8, NULL,    NULL};
int staffRollNameDataNum = (int)(sizeof(staffRollNameData) / sizeof(staffRollNameData[0])) - 2;

int systemStatus[12] = {1, 1}; /* PAL, frame step 1 */

#define MAX_POSTED 64
static const char *s_posted[MAX_POSTED];
static int s_align[MAX_POSTED];
static int s_nPosted;
static int s_fontAlign;

int font_GetHeight(void)
{
    return 20;
}

int font_GetWidth(void)
{
    return 20;
}

void font_Init(void) {}

/* font_CheckAlign's parse of {L}, {R}, {C} (the alignment holds from line
   to line); each call is one posted line */
int font_CheckAlign(SprCol *col, unsigned char *str)
{
    const char *s = (const char *)str;
    const char *p;

    for (p = s; (p = strchr(p, '{')) != NULL; p++) {
        if (p[1] == 'L' && p[2] == '}') {
            s_fontAlign = 1;
        } else if (p[1] == 'R' && p[2] == '}') {
            s_fontAlign = 2;
        } else if (p[1] == 'C' && p[2] == '}') {
            s_fontAlign = 0;
        }
    }
    memset(col, 0, sizeof(*col));
    if (s_nPosted < MAX_POSTED) {
        s_posted[s_nPosted] = s;
        s_align[s_nPosted] = s_fontAlign;
    }
    s_nPosted++;
    return s_fontAlign;
}

/* the lines font_Print drew: the port's heading and name among them */
static int s_printed, s_printedHeading, s_printedName, s_printedDisc;

void font_Print(unsigned int color, unsigned char *str, float x, float y, int align, SprCol col)
{
    const char *s = (const char *)str;

    (void)color;
    (void)x;
    (void)y;
    (void)align;
    (void)col;
    s_printed++;
    s_printedHeading |= strcmp(s, "{R}< Decompilation and PC Port > ") == 0;
    s_printedName |= strcmp(s, "{R}Nathanial Fine ") == 0;
    s_printedDisc |= strcmp(s, "{R}Fumito Ueda ") == 0;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assert(const char *file, int line)
{
    fprintf(stderr, "debug_assert %s:%d\n", file, line);
    exit(1);
}

/* the achievements: none unlocked, no clears */
int ico_ach_find(const char *id)
{
    return strcmp(id, "finish") == 0 ? 0 : -1;
}

IcoAchState ico_ach_state(int i)
{
    (void)i;
    return ICO_ACH_LOCKED;
}

void ico_ach_stats(IcoAchStats *out)
{
    memset(out, 0, sizeof(*out));
}

/* the engine's view of the game (credits_live.c) */
int stage_no = 1;
struct SqEntry *titleAdpcm;
struct SqEntry *sea;
int seEnvForceClose;
int gFlagGameClear;
char gameSysMainSaveBuff[25596];
void *gameSysMemoryFuncList[1];
static unsigned char s_gflags[400];
static unsigned int s_ticks;
static int s_switchTo = -1, s_simpleTo = -1, s_loads, s_world;

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

/* the "world": one int the save image carries */
void gamesysMemorySave(void **tbl, void *mem, void *arg)
{
    (void)tbl;
    (void)arg;
    memcpy(mem, &s_world, sizeof(s_world));
}

void gamesysMemoryLoad(void **tbl, void *mem, void *arg)
{
    (void)tbl;
    (void)arg;
    memcpy(&s_world, mem, sizeof(s_world));
    s_loads++;
}

const AdpcmDataRec adpcmFile[64];

void ico_adpcm_set_start(int no, int bytes)
{
    (void)no;
    (void)bytes;
}

void stgmgrForceSwitchWithFade(int stage, float fadeIn, float fadeOut)
{
    (void)fadeIn;
    (void)fadeOut;
    s_switchTo = stage;
}

void ACTGame_SetActors_Debug(int stage, unsigned char flag)
{
    (void)stage;
    (void)flag;
}

void ACTGame_StageChangeGObjID(int no, int kind, int idx)
{
    (void)no;
    (void)kind;
    s_world = idx; /* the boy's record moved to the entrance */
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

/* --- the tests ------------------------------------------------------------- */

static int ascii(const char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E) {
            return 0;
        }
    }
    return 1;
}

static void test_roll(void)
{
    int n = staffRollNameDataNum;
    int port = ico_roll_port_count();
    int steps = 0;
    int i;

    CHECK(port == ICO_ROLL_PORT_NAME + 1);
    CHECK(ico_roll_port_line(-1) == NULL && ico_roll_port_line(port) == NULL);
    for (i = 0; i < port; i++) {
        CHECK(ico_roll_port_line(i) != NULL && *ico_roll_port_line(i) != NULL);
        CHECK(ascii(*ico_roll_port_line(i)));
        /* the same storage each time: staffroll.c keeps the pointer */
        CHECK(ico_roll_port_line(i) == ico_roll_port_line(i));
    }

    staffRollStart(1.0f, 255);
    CHECK(staffRollStartFlag == 1);
    while (staffRollStartFlag != 0 && steps < 1000000) {
        staffRollMain();
        steps++;
    }
    CHECK(staffRollStartFlag == 0);
    printf("roll: %d lines posted in %d frames\n", s_nPosted, steps);
    CHECK(s_nPosted == n + port);
    if (s_nPosted != n + port || s_nPosted > MAX_POSTED) {
        return;
    }
    /* the disc's lines first, in order, untouched */
    for (i = 0; i < n; i++) {
        CHECK(s_posted[i] == staffRollNameData[i]);
    }
    /* then the port's: blank lines, the heading, blank lines, the name */
    for (i = 0; i < port; i++) {
        const char *s = s_posted[n + i];

        if (i == ICO_ROLL_PORT_HEADING) {
            CHECK(strcmp(s, "{R}< Decompilation and PC Port > ") == 0);
            /* the roll's heading form: {R}, "< ", " > " */
            CHECK(strncmp(s, "{R}< ", 5) == 0 && strstr(s, " > ") != NULL);
            CHECK(s_align[n + i] == 2);
        } else if (i == ICO_ROLL_PORT_NAME) {
            CHECK(strcmp(s, "{R}Nathanial Fine ") == 0);
            CHECK(s_align[n + i] == 2);
        } else {
            CHECK(strcmp(s, " ") == 0);
        }
    }
    /* the heading and the name are the last non-blank lines, the name last */
    CHECK(strstr(s_posted[s_nPosted - 1], "Nathanial Fine") != NULL);
    for (i = n + ICO_ROLL_PORT_HEADING + 1; i < s_nPosted - 1; i++) {
        CHECK(strcmp(s_posted[i], " ") == 0);
    }
    /* the roll's gap between a heading and its name: four blank lines */
    CHECK(ICO_ROLL_PORT_NAME - ICO_ROLL_PORT_HEADING == 5);
    /* the copyright line, centred by the disc's {C}, stays as it was */
    CHECK(s_align[n - 1] == 0);
    /* every line is drawn by the roll's own bitmap font (package TXT2): the
       disc's and the port's */
    printf("roll: %d font_Print calls\n", s_printed);
    CHECK(s_printed > 0 && s_printedDisc && s_printedHeading && s_printedName);
}

static void test_lock(const char *dir)
{
    char none[600], ini[600];
    FILE *f;

    snprintf(none, sizeof(none), "%s/credits_no_config.toml", dir);
    snprintf(ini, sizeof(ini), "%s/credits_unlock.ini", dir);
    remove(none);
    ico_config_reset(none, none);
    CHECK(!ico_credits_unlocked());
    CHECK(!ico_credits_active());
    f = fopen(ini, "w");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    fputs("unlock_credits=1\n", f);
    fclose(f);
    ico_config_reset(none, ini);
    CHECK(ico_credits_unlocked());
    ico_config_reset(none, none);
    remove(ini);
    /* no engine in this program: the start fails and leaves the flag off */
    CHECK(ico_credits_start() < 0);
    CHECK(!ico_credits_active());
}

/* credits_live.c (host_loop.c declares it so too) */
void ico_credits_engine_install(void);

/* polls n Main ticks */
static void runTicks(unsigned int n)
{
    while (n-- > 0) {
        s_ticks++;
        ico_credits_host_poll();
    }
}

static void test_engine_failure(void)
{
    ico_credits_engine_install();
    /* a stage change that starts and never ends */
    stage_no = 1;
    systemStatus[6] = 0;
    s_world = 7;
    gFlagGameClear = 1;
    memset(s_gflags, 0, sizeof(s_gflags));
    s_gflags[10] = 1;
    gameSysMainSaveBuff[0] = 'C';
    s_loads = 0;
    CHECK(ico_credits_start() >= 0 && ico_credits_active() && s_switchTo == 60);
    CHECK(s_world == 216); /* the boy at the ending's entrance */
    systemStatus[6] = 1;
    s_gflags[10] = 0; /* what the staff scenes would change */
    s_gflags[20] = 1;
    gameSysMainSaveBuff[0] = 'X';
    runTicks(2000);
    /* past REACH_TICKS with the stage change running: still waiting */
    CHECK(ico_credits_active() && s_simpleTo == -1 && s_loads == 0);
    runTicks(3000);
    /* past REACH_LIMIT: failed, the title requested, the flag kept */
    CHECK(s_simpleTo == 1 && ico_credits_active() && s_loads == 0);
    /* a late STAFF1 entry does not end it; the title's does, the state put
       back */
    ico_credits_stage_enter(60);
    CHECK(ico_credits_active());
    stage_no = 1;
    systemStatus[6] = 0;
    ico_credits_stage_enter(1);
    CHECK(!ico_credits_active() && s_loads == 1 && s_world == 7);
    CHECK(s_gflags[10] == 1 && s_gflags[20] == 0 && gFlagGameClear == 1);
    CHECK(gameSysMainSaveBuff[0] == 'C');

    /* the title never left: put back once REACH_TICKS pass */
    s_simpleTo = -1;
    s_loads = 0;
    CHECK(ico_credits_start() >= 0 && ico_credits_active());
    runTicks(1600);
    CHECK(!ico_credits_active() && s_loads == 1 && s_world == 7 && s_simpleTo == -1);

    /* the title does not come back either: put back after RETURN_TICKS */
    s_loads = 0;
    CHECK(ico_credits_start() >= 0);
    systemStatus[6] = 1;
    runTicks(4600);
    CHECK(s_simpleTo == 1 && ico_credits_active());
    runTicks(1600);
    CHECK(!ico_credits_active() && s_loads == 1 && s_world == 7);
    systemStatus[6] = 0;
    ico_credits_set_engine(NULL);
}

int main(int argc, char **argv)
{
    test_roll();
    test_lock(argc > 1 ? argv[1] : ".");
    test_engine_failure();
    if (failures) {
        fprintf(stderr, "credits_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("credits_test: ok\n");
    return 0;
}
