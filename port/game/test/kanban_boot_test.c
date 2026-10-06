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
 *     card's system file holds others (a later boot: language 5, PAL).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "sysconf.h"
/* the boot, as the game's own source; stubs below satisfy it (its step
   log, diag_host.c's in ico_platform, is the test's own here) */
#define ico_host_kanban_step test_kanban_step
#include "../../../ico2/common/src/kanbanBoot.c"

static int failures;

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

void test_kanban_step(int boot_step, int mc_check_step)
{
    (void)boot_step;
    if (mc_check_step == 102 || (mc_check_step >= 190 && mc_check_step <= 202)) {
        s.badStep++;
    }
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
    s.savePresent = savePresent;
    memset(IosMcProductFile, 0, sizeof(IosMcProductFile));
    /* kanbanBoot.c's .sdata as the program starts */
    bootStep = mcCheckStep = bootStarted = bootKanbanDone = 0;
    mcRetryCount = 10;
    /* main.c: the PAL default and its language 3, then the port's values */
    systemStatus[0] = 1;
    NonLinearCameraMove = 3;
    systemStatus[0] = ico_boot_video_mode();
    NonLinearCameraMove = ico_boot_language();
    CHECK(systemStatus[0] == 0 && NonLinearCameraMove == 4,
          "%s: 60 Hz and German before the first tick (%d, %d)", what, systemStatus[0],
          NonLinearCameraMove);
    kanbanBootInit();
    kanbanBootStart();
    while (!kanbanBootEnd && s.ticks < 1000) {
        kanbanBootMain();
        s.ticks++;
        CHECK(systemStatus[0] == 0 && NonLinearCameraMove == 4,
              "%s: tick %d: 60 Hz and German stay (%d, %d)", what, s.ticks, systemStatus[0],
              NonLinearCameraMove);
        if (failures > 10) {
            return;
        }
    }
    CHECK(kanbanBootEnd, "%s: the boot ends (%d ticks)", what, s.ticks);
    CHECK(s.reloads == 0, "%s: no reload of stage 1 (%d)", what, s.reloads);
    CHECK(s.layoutSwitches == 0, "%s: no layout switch (%d)", what, s.layoutSwitches);
    CHECK(s.gsResets == 0, "%s: no gsResetFunc (%d)", what, s.gsResets);
    CHECK(s.chdirs == 1, "%s: one card check (%d chdirs)", what, s.chdirs);
    CHECK(s.loads == savePresent, "%s: the system file loaded %d times", what, s.loads);
    CHECK(s.adds[0] == 0 && s.adds[1] == 0, "%s: no language or 50/60 Hz sign (%d, %d)", what,
          s.adds[0], s.adds[1]);
    CHECK(s.adds[2] == 1, "%s: the presents sign once (%d)", what, s.adds[2]);
    CHECK(s.adds[5] == 0, "%s: no card warning (%d)", what, s.adds[5]);
    CHECK(s.badStep == 0, "%s: no tick in steps 102 or 190..202 (%d)", what, s.badStep);
    CHECK(fbKeep == 0, "%s: the frame buffer released", what);
    printf("  %s: %d ticks, %d chdir, %d load, %d reloads, %d gsResetFunc\n", what, s.ticks,
           s.chdirs, s.loads, s.reloads, s.gsResets);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char p[1100];
    snprintf(p, sizeof(p), "%s/kanban_boot_test.toml", dir);
    writeFile(p, "version = 1\n[game]\nlanguage = \"de\"\n");
    ico_config_reset(p, "");
    ico_sysconf_reset();
    boot("first boot (no save)", 0);
    boot("later boot (a save)", 1);
    remove(p);
    if (failures) {
        printf("kanban_boot_test: %d failures\n", failures);
        return 1;
    }
    printf("kanban_boot_test: ok\n");
    return 0;
}
