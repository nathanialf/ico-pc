/*
 * port/game/unused_content.c
 *
 * Extras > Unused content (ico_unused.h): the rows' table, the flag, the
 * start and the engine indirection.  Port code only: the game's side
 * (whether the disc holds a row's data, the start itself) is the engine in
 * unused_live.c, installed by the program.
 */
#include "ico_unused.h"

#include <stdio.h>

#include "../core/region.h" /* ico_region_cur, ico_region_stage */

/* The European disc's rows (port/data/gen/pal/table_desc.c stage-all,
   read from the ELF): 88 boss_test and 91 8a_TEST, whose dataFile names
   STGBOSS_TEST and STG8TEST; 59 memoryPAL576, whose dataFile is "#REF!"
   (no pack: the stage is its movie, movieFile row 3); the art sits in the
   logo pack of stage 1 (STGLOG.DF), the boot logo and title. */
static const IcoUnusedRow kRows[ICO_UNUSED_COUNT] = {
    [ICO_UNUSED_ARENA] = {88, "STGBOSS_TEST.DF", NULL, NULL, "arena"},
    [ICO_UNUSED_CLIMB] = {91, "STG8TEST.DF", NULL, NULL, "climbing"},
    [ICO_UNUSED_MOVIE] = {59, NULL, "pal_advertise576.pss", NULL, "attract movie (576 lines)"},
    [ICO_UNUSED_ART] = {1, "STGLOG.DF", NULL, "object/sdf/e3/st13b/model/copy_right.p2o",
                        "early show title art"},
};

static const IcoUnusedEngine *s_engine;
static int s_running = -1;
static int s_gameOver;
/* each row's answer on the disc it was asked on (-1 not asked yet) */
static const IcoRegionInfo *s_askedDisc;
static signed char s_has[ICO_UNUSED_COUNT];

const IcoUnusedRow *ico_unused_row(int row)
{
    return row >= 0 && row < ICO_UNUSED_COUNT ? &kRows[row] : NULL;
}

int ico_unused_stage(int row)
{
    const IcoUnusedRow *r = ico_unused_row(row);
    /* ico_region_stage: the running disc's row of the European stage */
    return r != NULL ? ico_region_stage(r->pal_stage) : -1;
}

static void forget(void)
{
    s_askedDisc = NULL;
    for (int i = 0; i < ICO_UNUSED_COUNT; i++) {
        s_has[i] = -1;
    }
}

int ico_unused_available(int row)
{
    if (ico_unused_row(row) == NULL) {
        return 0;
    }
    if (s_askedDisc != ico_region_cur()) {
        forget();
        s_askedDisc = ico_region_cur();
    }
    if (s_has[row] < 0) {
        int has = 0;
        if (ico_unused_stage(row) <= 0) {
            fprintf(stderr,
                    "unused: no %s row on %s (its stageData has no row for the European "
                    "stage %d)\n",
                    kRows[row].name, ico_region_cur()->disc_id, kRows[row].pal_stage);
        } else if (s_engine != NULL && s_engine->available != NULL) {
            has = s_engine->available(row) == 1;
        }
        s_has[row] = (signed char)has;
    }
    return s_has[row];
}

int ico_unused_running(void)
{
    return s_running;
}

int ico_unused_active(void)
{
    return s_running >= 0;
}

void ico_unused_set_running(int row)
{
    s_running = ico_unused_row(row) != NULL ? row : -1;
    s_gameOver = 0;
}

void ico_unused_game_over(void)
{
    if (s_running >= 0) {
        s_gameOver = 1;
        fprintf(stderr, "unused: game over in the test room %s: both choices go to the title\n",
                kRows[s_running].name);
    }
}

const char *ico_unused_left_by(void)
{
    return s_gameOver ? "game over" : "End Game";
}

void ico_unused_set_engine(const IcoUnusedEngine *e)
{
    s_engine = e;
    forget(); /* a new engine answers again */
}

int ico_unused_start(int row)
{
    const IcoUnusedRow *r = ico_unused_row(row);

    if (r == NULL) {
        fprintf(stderr, "unused: failed: no row %d\n", row);
        return -1;
    }
    if (s_running >= 0) {
        fprintf(stderr, "unused: failed: %s while %s runs\n", r->name, kRows[s_running].name);
        return -1;
    }
    if (s_engine == NULL || s_engine->begin == NULL) {
        fprintf(stderr, "unused: failed: no engine in this program\n");
        return -1;
    }
    if (!ico_unused_available(row)) {
        fprintf(stderr, "unused: failed: %s is not on this disc (%s)\n", r->name,
                ico_region_cur()->disc_id);
        return -1;
    }
    fprintf(stderr, "unused: enter %s\n", r->name);
    return s_engine->begin(row); /* the engine logged a refusal */
}
