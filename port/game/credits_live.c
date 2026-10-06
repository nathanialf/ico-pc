/*
 * port/game/credits_live.c
 *
 * The Extras credits' engine (ico_credits.h; docs/port/EXTRAS.md,
 * "Credits"): the game's side of the playback, in the program only.
 *
 * Start (from the Settings menu's proc on the title, stage 1): the game's
 * state is kept (below), the title theme fades and the stage changes to
 * STAFF1 the way a load from the title changes to the save's stage
 * (layout_action.c, la_load_processing: the theme's fade step, the stage
 * environment sounds closed, stgmgrForceSwitchWithFade, then
 * ACTGame_SetActors_Debug, which gives the boy's records to the stage at
 * the first exit into it, exit 26), and the boy's record is then put at the
 * entrance the real ending uses, exit 216 from 26b4demo2
 * (ACTGame_StageChangeGObjID, as RequestStageChange does for him).  The
 * stage's own objects run actStaff1 and the rest of the ending's staff
 * scenes as in the real ending (end.c); under the flag actStaff3RollChk
 * goes back to the title when the roll ends.
 *
 * The state kept: everything a save holds, taken with the game's own
 * gamesysMemorySave into a buffer of this file (the flags, the object
 * records that place the boy and Yorda in a stage, the generators, hints,
 * the character record, the back stage, itou's flags), the checkpoint
 * image gameSysMainSaveBuff, gFlagGameClear and systemStatus[2..4].  The
 * staff scenes move the boy's record to STAFF1, 2 and 3 (the entrance, then
 * each RequestStageChange), so without it the title would come back with
 * no boy, and its camera, falling back to him when the title's animation
 * ends, would read through a null object.  It is put back as stage 1 is
 * entered again (StageManager.c, start_stage_Load_thread, after exit_stage
 * and before the stage's objects are built), with gamesysMemoryLoad as the
 * title's Load does, then the flags bit by bit (the load sets 394).
 *
 * The poll (once per vsync, after the game's threads) logs the stages and
 * the song.  A start that never reaches STAFF1 is a failure: logged, the
 * title requested and the state put back as it is entered, the flag kept
 * until then (a bound on each wait; see REACH_TICKS).
 */
#include <stdio.h>

#include "ico_credits.h"

#include <string.h>

#include "adpcm_init.h"
#include "gamesys.h"
#include "gflag.h"
#include "s_init.h"

/* the game's side, declared here as the callers in the game do */
extern int stage_no;
extern int systemStatus[];
extern struct SqEntry *titleAdpcm; /* op.c */
extern void stgmgrForceSwitchWithFade(int stage, float fadeIn, float fadeOut);
extern void ACTGame_SetActors_Debug(int stage, unsigned char flag);
extern void ACTGame_StageChangeGObjID(int no, int kind, int idx);
extern unsigned int ico_host_main_ticks(void);

extern struct SqEntry *sea; /* end.c: the ending's song */
extern int RequestStageChangeSimple(int no, float speed, float wait, unsigned char r,
                                    unsigned char gr, unsigned char b);
/* gamesys.c's checkpoint image, sized in gamesys.h (its definition's size,
   checked by the compiler against the declaration) */
#define SAVE_IMAGE ((int)sizeof(gameSysMainSaveBuff))

/* The ending's song, "ICO -You were there-" (adpcmFile 47, event/39_8.int,
   265.6 s), starts in actEndDemo06Chk, six scenes before the roll.  Run
   headless from 13b4demo3 (stage 47, where actEndDemo06 is) with the flags
   of the scenes before it, the real ending opened it at Main tick 122 and
   started the roll at Main tick 3899: 3777 ticks at PAL's 25 Hz, 151.1 s
   into the song, so 114.5 s of it are left when the roll starts and it
   ends 12 s into the third scene (STAFF3, whose own piece starts there). */
#define SONG 47
#define SONG_AT_ROLL_MS 151080
/* the playback requests it in actStaff1Chk, on the roll's tick, and the
   script daemon has it playing two Main ticks later (the headless run) */
#define SONG_OPEN_MS 80

/* the real ending's way into STAFF1: 26b4demo2's sixth exit (actConte14_13,
   RequestStageChange(6, ...)), and the boy's object record (label 54, kind
   1: ACTGame_SetActors_Debug's first row) */
#define ENTRANCE_EXIT 216
#define BOY_LABEL 54
#define BOY_KIND 1

/* the story flags: gflag.c's 50-byte bitmap */
#define GFLAG_COUNT 400
/* Main ticks the stage change may take before it counts as failed (the
   load of STAFF1 takes a few hundred): REACH_TICKS with no stage change
   running, REACH_LIMIT whatever runs; then RETURN_TICKS for the title's
   stage to come back before the state is put back where it stands */
#define REACH_TICKS 1500
#define REACH_LIMIT 4500
#define RETURN_TICKS 1500

/* PH_RETURNING: the start failed and the title was requested; the flag
   stays set (a late STAFF1 runs the Extras' ending, not the real one's
   save) until the title's stage is entered */
enum { PH_IDLE, PH_SWITCHING, PH_PLAYING, PH_RETURNING };

static int s_phase = PH_IDLE;
static unsigned char s_flags[GFLAG_COUNT];
static int s_gameClear;
static char s_world[SAVE_IMAGE];
static char s_checkpoint[SAVE_IMAGE];
static int s_status[3];
static unsigned int s_startTick;
static unsigned int s_returnTick;
static int s_lastStage;
static unsigned int s_songTick;
static int s_songSeen;

static void keepState(void)
{
    int i;

    for (i = 0; i < GFLAG_COUNT; i++) {
        s_flags[i] = (unsigned char)gflagChk(i);
    }
    s_gameClear = gFlagGameClear;
    memset(s_world, 0, sizeof(s_world));
    gamesysMemorySave(gameSysMemoryFuncList, s_world, 0);
    memcpy(s_checkpoint, gameSysMainSaveBuff, SAVE_IMAGE);
    for (i = 0; i < 3; i++) {
        s_status[i] = systemStatus[2 + i];
    }
}

/* the state as it was at the start; returns how many flags differed after
   the load (the load itself sets 394) */
static int restoreState(void)
{
    int i;
    int n = 0;

    gamesysMemoryLoad(gameSysMemoryFuncList, s_world, 0);
    memcpy(gameSysMainSaveBuff, s_checkpoint, SAVE_IMAGE);
    for (i = 0; i < 3; i++) {
        systemStatus[2 + i] = s_status[i];
    }

    for (i = 0; i < GFLAG_COUNT; i++) {
        if (gflagChk(i) != s_flags[i]) {
            n++;
            if (s_flags[i]) {
                gflagOn(i);
            } else {
                gflagOff(i);
            }
        }
    }
    gFlagGameClear = s_gameClear;
    return n;
}

static int begin(void)
{
    if (s_phase != PH_IDLE || stage_no != ICO_CREDITS_TITLE_STAGE || systemStatus[6] != 0) {
        fprintf(stderr, "credits: failed: not on the title (stage %d, stage change %d)\n", stage_no,
                systemStatus[6]);
        return -1;
    }
    keepState();
    ico_credits_set_active(1);
    s_phase = PH_SWITCHING;
    s_startTick = ico_host_main_ticks();
    s_lastStage = stage_no;
    s_songTick = 0;
    s_songSeen = 0;
    /* as la_load_processing leaves the title for a save's stage */
    seEnvForceClose = 1;
    if (titleAdpcm != 0) {
        titleAdpcm->stream->fadeStep = 0x40;
    }
    titleAdpcm = 0;
    stgmgrForceSwitchWithFade(ICO_CREDITS_STAGE, 8.0f, 4.0f);
    ACTGame_SetActors_Debug(ICO_CREDITS_STAGE, 0);
    ACTGame_StageChangeGObjID(BOY_LABEL, BOY_KIND, ENTRANCE_EXIT);
    fprintf(stderr, "credits: stage %d requested at Main tick %u\n", ICO_CREDITS_STAGE,
            s_startTick);
    return 0;
}

static void finish(const char *why, int restore)
{
    int n = restore ? restoreState() : 0;

    ico_adpcm_set_start(-1, 0); /* a song start not taken is not kept */

    ico_credits_set_active(0);
    s_phase = PH_IDLE;
    fprintf(stderr, "credits: %s at Main tick %u%s (%d game flag%s differed after the load)\n", why,
            ico_host_main_ticks(), restore ? ", the game's state put back" : "", n,
            n == 1 ? "" : "s");
}

void ico_credits_stage_enter(int stage)
{
    if (s_phase == PH_PLAYING && stage == ICO_CREDITS_TITLE_STAGE) {
        finish("back at the title", 1);
    } else if (s_phase == PH_RETURNING && stage == ICO_CREDITS_TITLE_STAGE) {
        finish("failed: back at the title", 1);
    }
}

/* the start did not reach STAFF1: back to the title (the roll's own way
   there, actStaff3RollChk), the state put back as the title is entered;
   still on the title with no stage change running: put back now */
static void failStart(void)
{
    fprintf(stderr,
            "credits: failed: the staff roll's stage was not reached by Main tick %u (stage %d, "
            "stage change %d)\n",
            ico_host_main_ticks(), stage_no, systemStatus[6]);
    if (stage_no == ICO_CREDITS_TITLE_STAGE && systemStatus[6] == 0) {
        finish("failed: stopped on the title", 1);
        return;
    }
    s_phase = PH_RETURNING;
    s_returnTick = ico_host_main_ticks();
    RequestStageChangeSimple(ICO_CREDITS_TITLE_STAGE, 16.0f, 8.0f, 0, 0, 0);
}

void ico_credits_host_poll(void)
{
    if (s_phase == PH_IDLE) {
        return;
    }
    if (stage_no != s_lastStage) {
        fprintf(stderr, "credits: stage %d at Main tick %u\n", stage_no, ico_host_main_ticks());
        s_lastStage = stage_no;
    }
    if (s_phase == PH_SWITCHING) {
        if (stage_no == ICO_CREDITS_STAGE && systemStatus[6] == 0) {
            s_phase = PH_PLAYING;
            fprintf(stderr, "credits: stage %d up at Main tick %u\n", stage_no,
                    ico_host_main_ticks());
        } else {
            const unsigned int t = ico_host_main_ticks() - s_startTick;
            if ((t > REACH_TICKS && systemStatus[6] == 0) || t > REACH_LIMIT) {
                failStart();
            }
        }
        return;
    }
    if (s_phase == PH_RETURNING) {
        if (ico_host_main_ticks() - s_returnTick > RETURN_TICKS) {
            finish("failed: the title did not come back; the state put back where it stands", 1);
        }
        return;
    }
    if (!s_songSeen && s_songTick != 0 && sea != 0) {
        s_songSeen = 1;
        fprintf(stderr, "credits: the ending's song is playing at Main tick %u (requested at %u)\n",
                ico_host_main_ticks(), s_songTick);
    }
    /* playing: the roll's end changes to the title (end.c,
       actStaff3RollChk), and ico_credits_stage_enter ends the playback */
}

int ico_credits_song_start(void)
{
    const AdpcmDataRec *f = &adpcmFile[SONG];
    /* 16-byte ADPCM blocks of 28 samples per channel, the channels
       interleaved in 0x400-byte runs (a 2 KB sector holds both) */
    long long blocks = (long long)(SONG_AT_ROLL_MS + SONG_OPEN_MS) * f->pitch / 28 / 1000;
    int bytes = (int)(blocks * 16 * f->channels) & ~0x7FF;

    ico_adpcm_set_start(SONG, bytes);
    s_songTick = ico_host_main_ticks();
    s_songSeen = 0;
    fprintf(stderr,
            "credits: the ending's song (stream %d) requested from byte %d of %d at Main tick %u\n",
            SONG, bytes, f->sectors << 11, s_songTick);
    return SONG;
}

static const IcoCreditsEngine kEngine = {begin};

void ico_credits_engine_install(void)
{
    ico_credits_set_engine(&kEngine);
}
