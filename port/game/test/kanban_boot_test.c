/*
 * port/game/test/kanban_boot_test.c
 *
 * The boot's card check and signs (ico2/common/src/kanbanBoot.c, compiled in
 * below so its statics are in reach) on the CPU, for a first boot (a card
 * without the game's save) and a later one (a card with it), with the rest
 * of the game stubbed:
 *   - main.c's two boot values first, from a synthetic config: [game]
 *     language "de" and no [video] video_mode give the language 4 and 60 Hz
 *     (systemStatus[0] 0) before the first tick;
 *   - the boot then runs to kanbanBootEnd without reloading stage 1, without
 *     gsResetFunc, with one card check (one chdir on port 0), no language or
 *     50/60 Hz sign (kanbanReqAdd 0 or 1), no layout switch, never in the
 *     steps after the screens (102, 190..202), and with the Sony presents
 *     sign (kanbanReqAdd 2) shown once;
 *   - the values main.c set are the ones in force at the end, also when the
 *     card's system file holds others (a later boot: language 5, PAL);
 *   - with stage 1's load running into the check (systemStatus[6] until a
 *     set tick, as in a start-up's log: the load ends at Main tick 117, steps
 *     101, 300 and -1 are logged at 118, 119 and 120 and bootStep 3 at 121),
 *     every frame from the check's step 2 to the tick that asks for the Sony
 *     presents sign is a keep frame (fbKeep 1 at the frame's kick, after the
 *     tick and after the stage manager's end of the load), and that tick's
 *     frame is not: it carries the sign's backdrop (kanbanExec runs after
 *     kanbanBootMain in the tick, icoMisc.c).  The stage manager's end of the
 *     load is StageManager.c's fadeOut 0 path as the port has it (fadeStatus
 *     0, fbKeep 0 unless ico_kanban_boot_holds_keep), run before the tick
 *     that sees the load's end and, the other order the threads can take,
 *     between the tick before it and that tick's kick.  The step ticks are
 *     the log's.
 * With "jpn" (ctest kanban_boot_jpn) the same runs on the Japanese disc,
 * whose card check is kanbanBootMcCheck_jpn: no language step, no video
 * step, no read of the card's system file (so neither cameraMove nor
 * palMode), NonLinearCameraMove 0 (the disc has no language word) and
 * systemStatus[0] 0 (60 Hz) from the start and throughout, one card check,
 * the presents sign (its sign 0) once and no card warning (its sign 3);
 * over the load the check asks for the presents sign within a few ticks,
 * before the load ends, and every frame from its step 2 to that tick is a
 * keep frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "sysconf.h"
#include "../../core/region.h" /* ico_region_select: the Japanese run */
/* the boot, as the game's own source; stubs below satisfy it (its step
   log, diag_host.c's in ico_platform, is the test's own here) */
#define ico_host_kanban_step test_kanban_step
#include "../../../ico2/common/src/kanbanBoot.c"

static int failures;
static int s_jpn; /* the Japanese disc's run */

/* the language main.c sets from [game] language "de": German (4) on the
   European disc, 0 on the Japanese one (no language word) */
#define WANT_LANG (s_jpn ? 0 : 4)
/* the signs kanbanBoot.c asks for: the presents sign, the card warning */
#define SIGN_PRESENTS (s_jpn ? 0 : 2)
#define SIGN_WARNING (s_jpn ? 3 : 5)

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- the game's globals kanbanBoot.c reads ------------------------------- */
int systemStatus[12] = {1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7}; /* main.c's .data */
int NonLinearCameraMove;
int fbKeep;
int fadeStatus;
LtProp texLayout[64];
McProductFile IosMcProductFile[2];

/* --- what the boot calls, counted ---------------------------------------- */
static struct {
    int reloads, layoutSwitches, gsResets, chdirs, loads, adds[8], badStep, ticks;
    int savePresent;
    /* the tick each step was first logged at (kanbanBootMain's entry), -1
       before: card steps 100, 101, 300 and -1, bootStep 3 */
    int at100, at101, at300, atEnd, atBoot3;
} s;

static Kanban s_signs[16];
static int s_signCount;

Kanban *kanbanReqAdd(int no, int pri)
{
    (void)pri;
    if (no >= 0 && no < 8) {
        s.adds[no]++;
    }
    Kanban *k = &s_signs[s_signCount++ % 16];
    memset(k, 0, sizeof(*k));
    k->layout = &texLayout[no + 1];
    k->key = 1; /* answered at once */
    return k;
}

void kanbanReqDel(Kanban *self)
{
    self->layout = 0;
}

void kanbanReqDelFade(Kanban *self)
{
    self->layout = 0;
}

void kanbanReqAllDelFade(void)
{
    for (int i = 0; i < 16; i++) {
        s_signs[i].layout = 0;
    }
}

/* a formatted card in port 0 with room; the game's directory there when
   savePresent (sceMcChdir's result 0) */
void iosMcChdirProduct(McMgr *mp)
{
    s.chdirs++;
    mp->type = mp->port == 0 ? 2 : 0;
    mp->format = 1;
    mp->free = 4000;
    mp->result = mp->port == 0 && s.savePresent ? 0 : -4;
}

int iosMcSync(McMgr *mp)
{
    (void)mp;
    return 1;
}

/* the card's system file holds another language and PAL */
void iosMcLoadProductBlock(McMgr *mp)
{
    s.loads++;
    mp->result = 0;
    IosMcProductFile[mp->port].cameraMove = 5;
    IosMcProductFile[mp->port].palMode = 1;
}

void stgmgrForceSwitchWithFade(int stage, float fadeIn, float fadeOut)
{
    (void)stage, (void)fadeIn, (void)fadeOut;
    s.reloads++;
}

void lt_switch_layout(int no)
{
    (void)no;
    s.layoutSwitches++;
}

int gsResetFunc(int val)
{
    (void)val;
    s.gsResets++;
    return 0;
}

void isysGObjActiveLink(int bit, int set)
{
    (void)bit, (void)set;
}

static void firstAt(int *at, int cond)
{
    if (cond && *at < 0) {
        *at = s.ticks;
    }
}

void test_kanban_step(int boot_step, int mc_check_step)
{
    if (mc_check_step == 102 || (mc_check_step >= 190 && mc_check_step <= 202)) {
        s.badStep++;
    }
    firstAt(&s.at100, boot_step == 2 && mc_check_step == 100);
    firstAt(&s.at101, boot_step == 2 && mc_check_step == 101);
    firstAt(&s.at300, boot_step == 2 && mc_check_step == 300);
    firstAt(&s.atEnd, boot_step == 2 && mc_check_step == -1);
    firstAt(&s.atBoot3, boot_step == 3);
}

/* --- the runs ------------------------------------------------------------ */

static void writeFile(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void boot(const char *what, int savePresent)
{
    memset(&s, 0, sizeof(s));
    s.at100 = s.at101 = s.at300 = s.atEnd = s.atBoot3 = -1;
    s.savePresent = savePresent;
    memset(IosMcProductFile, 0, sizeof(IosMcProductFile));
    /* kanbanBoot.c's .sdata as the program starts */
    bootStep = mcCheckStep = bootStarted = bootKanbanDone = 0;
    mcRetryCount = 10;
    /* main.c: the PAL default and its language 3 (the Japanese disc: 60 Hz
       from the mount, no language), then the port's values */
    systemStatus[0] = s_jpn ? 0 : 1;
    NonLinearCameraMove = s_jpn ? 0 : 3;
    systemStatus[0] = ico_boot_video_mode();
    NonLinearCameraMove = ico_boot_language();
    CHECK(systemStatus[0] == 0 && NonLinearCameraMove == WANT_LANG,
          "%s: 60 Hz and language %d before the first tick (%d, %d)", what, WANT_LANG,
          systemStatus[0], NonLinearCameraMove);
    kanbanBootInit();
    kanbanBootStart();
    while (!kanbanBootEnd && s.ticks < 1000) {
        kanbanBootMain();
        s.ticks++;
        CHECK(systemStatus[0] == 0 && NonLinearCameraMove == WANT_LANG,
              "%s: tick %d: 60 Hz and language %d stay (%d, %d)", what, s.ticks, WANT_LANG,
              systemStatus[0], NonLinearCameraMove);
        if (failures > 10) {
            return;
        }
    }
    CHECK(kanbanBootEnd, "%s: the boot ends (%d ticks)", what, s.ticks);
    CHECK(s.reloads == 0, "%s: no reload of stage 1 (%d)", what, s.reloads);
    CHECK(s.layoutSwitches == 0, "%s: no layout switch (%d)", what, s.layoutSwitches);
    CHECK(s.gsResets == 0, "%s: no gsResetFunc (%d)", what, s.gsResets);
    CHECK(s.chdirs == 1, "%s: one card check (%d chdirs)", what, s.chdirs);
    /* the Japanese check never reads the card's system file: no
       cameraMove, no palMode */
    CHECK(s.loads == (s_jpn ? 0 : savePresent), "%s: the system file loaded %d times", what,
          s.loads);
    if (s_jpn) {
        CHECK(s.adds[1] == 0 && s.adds[2] == 0, "%s: no card sign (no card %d, full %d)", what,
              s.adds[1], s.adds[2]);
    } else {
        CHECK(s.adds[0] == 0 && s.adds[1] == 0, "%s: no language or 50/60 Hz sign (%d, %d)", what,
              s.adds[0], s.adds[1]);
    }
    CHECK(s.adds[SIGN_PRESENTS] == 1, "%s: the presents sign once (%d)", what,
          s.adds[SIGN_PRESENTS]);
    CHECK(s.adds[SIGN_WARNING] == 0, "%s: no card warning (%d)", what, s.adds[SIGN_WARNING]);
    CHECK(s.badStep == 0, "%s: no tick in steps 102 or 190..202 (%d)", what, s.badStep);
    CHECK(fbKeep == 0, "%s: the frame buffer released", what);
    printf("  %s: %d ticks, %d chdir, %d load, %d reloads, %d gsResetFunc\n", what, s.ticks,
           s.chdirs, s.loads, s.reloads, s.gsResets);
}

/* --- the start-up's frames over stage 1's load ------------------------- */

/* StageManager.c's fadeOut 0 path once the load thread has cleared
   systemStatus[6], as the port has it */
static void stageManagerLoadEnd(void)
{
    systemStatus[6] = 0;
    fadeStatus = 0;
    if (ico_kanban_boot_holds_keep() == 0) {
        fbKeep = 0;
    }
}

#define LOAD_END_TICK 117 /* the start-up log's: step 101 is logged at 118 */
#define LOAD_TICKS 400    /* the presents sign alone holds 150 ticks at 60 Hz */

/* releaseLate: the stage manager ends the load after the tick before
   LOAD_END_TICK has run and before its frame is kicked (the threads'
   other order); else before the tick LOAD_END_TICK */
static void bootOverLoad(const char *what, int releaseLate)
{
    int kickKeep[LOAD_TICKS];
    int signTick = -1, step2Tick = -1;

    memset(&s, 0, sizeof(s));
    s.at100 = s.at101 = s.at300 = s.atEnd = s.atBoot3 = -1;
    s.savePresent = 1; /* a later boot: steps 95 and 96, then 100 */
    memset(IosMcProductFile, 0, sizeof(IosMcProductFile));
    bootStep = mcCheckStep = bootStarted = bootKanbanDone = 0;
    mcRetryCount = 10;
    systemStatus[0] = ico_boot_video_mode();
    NonLinearCameraMove = ico_boot_language();
    /* main.c's stgmgrForceSwitchWithFade(1, 255.0f, 0.0f): StageManager.c's
       command 1 keeps the frame buffer and starts the load */
    fbKeep = 1;
    fadeStatus = 1;
    systemStatus[6] = 1;
    kanbanBootInit();
    kanbanBootStart();
    while (!kanbanBootEnd && s.ticks < LOAD_TICKS) {
        if (!releaseLate && s.ticks == LOAD_END_TICK) {
            stageManagerLoadEnd();
        }
        const int signs = s.adds[SIGN_PRESENTS] + s.adds[SIGN_WARNING];
        kanbanBootMain();
        if (releaseLate && s.ticks == LOAD_END_TICK - 1) {
            stageManagerLoadEnd();
        }
        if (step2Tick < 0 && bootStep == 2 && mcCheckStep > 2) {
            step2Tick = s.ticks;
        }
        if (signTick < 0 && s.adds[SIGN_PRESENTS] + s.adds[SIGN_WARNING] != signs) {
            signTick = s.ticks;
        }
        kickKeep[s.ticks] = fbKeep; /* the scheduler's kick at the next vsync */
        CHECK(systemStatus[0] == 0 && NonLinearCameraMove == WANT_LANG,
              "%s: tick %d: 60 Hz and language %d stay (%d, %d)", what, s.ticks, WANT_LANG,
              systemStatus[0], NonLinearCameraMove);
        s.ticks++;
    }
    CHECK(kanbanBootEnd, "%s: the boot ends (%d ticks)", what, s.ticks);
    CHECK(s.adds[SIGN_PRESENTS] == 1 && s.adds[SIGN_WARNING] == 0,
          "%s: the presents sign once, no card warning (%d, %d)", what, s.adds[SIGN_PRESENTS],
          s.adds[SIGN_WARNING]);
    if (s_jpn) {
        /* the Japanese check does not wait for the load: a card with the
           save ends it at step 4, and the presents sign is asked for in the
           next tick, well before the load's end */
        CHECK(s.at101 < 0 && s.at300 < 0 && step2Tick >= 0 && signTick > step2Tick &&
                  signTick < LOAD_END_TICK && s.atEnd == signTick,
              "%s: no step 101 or 300 (%d, %d); the card check from tick %d, its end and the "
              "presents sign at ticks %d and %d, before the load's end %d",
              what, s.at101, s.at300, step2Tick, s.atEnd, signTick, LOAD_END_TICK);
    } else {
        CHECK(s.at101 == LOAD_END_TICK + 1 && s.at300 == LOAD_END_TICK + 2 &&
                  s.atEnd == LOAD_END_TICK + 3 && s.atBoot3 == LOAD_END_TICK + 4,
              "%s: steps 101, 300, -1 and bootStep 3 at ticks %d, %d, %d, %d (the log's %d to %d)",
              what, s.at101, s.at300, s.atEnd, s.atBoot3, LOAD_END_TICK + 1, LOAD_END_TICK + 4);
        CHECK(step2Tick >= 0 && signTick == LOAD_END_TICK + 3,
              "%s: the card check from tick %d, the presents sign asked for at tick %d (want %d)",
              what, step2Tick, signTick, LOAD_END_TICK + 3);
    }
    if (step2Tick < 0 || signTick < 0) {
        return;
    }
    int leaks = 0;
    for (int t = step2Tick; t < signTick; t++) {
        if (!kickKeep[t]) {
            leaks++;
            CHECK(0, "%s: tick %d (card step logged %s): the frame is not a keep frame", what, t,
                  t == LOAD_END_TICK       ? "100, the load's end"
                  : t == LOAD_END_TICK + 1 ? "101"
                  : t == LOAD_END_TICK + 2 ? "300"
                  : t < LOAD_END_TICK      ? "100, before the load's end is seen"
                                           : "later");
        }
    }
    CHECK(!kickKeep[signTick], "%s: the sign's tick %d is a keep frame (its backdrop unseen)", what,
          signTick);
    CHECK(fbKeep == 0 && fadeStatus == 0, "%s: fbKeep %d, fadeStatus %d at the end", what, fbKeep,
          fadeStatus);
    printf("  %s: load ends at tick %d; steps 101/300/-1 and bootStep 3 at %d/%d/%d/%d; keep "
           "frames from tick %d to %d (%d not kept), the sign's frame at %d\n",
           what, LOAD_END_TICK, s.at101, s.at300, s.atEnd, s.atBoot3, step2Tick, signTick - 1,
           leaks, signTick);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char p[1100];
    if (argc > 2 && strcmp(argv[2], "jpn") == 0) {
        s_jpn = 1;
        if (ico_region_select("jpn") != 0) {
            printf("kanban_boot_test: no region jpn\n");
            return 1;
        }
        /* select_disc (main_host.c): the Japanese game's word starts at 0 */
        systemStatus[0] = 0;
    }
    snprintf(p, sizeof(p), "%s/kanban_boot_test.toml", dir);
    writeFile(p, "version = 1\n[game]\nlanguage = \"de\"\n");
    ico_config_reset(p, "");
    ico_sysconf_reset();
    boot("first boot (no save)", 0);
    boot("later boot (a save)", 1);
    bootOverLoad("over the load, the stage manager first", 0);
    bootOverLoad("over the load, the stage manager after the tick", 1);
    remove(p);
    if (failures) {
        printf("kanban_boot_test%s: %d failures\n", s_jpn ? " jpn" : "", failures);
        return 1;
    }
    printf("kanban_boot_test%s: ok\n", s_jpn ? " jpn" : "");
    return 0;
}
