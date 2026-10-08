/*
 * port/platform/trace_host.c
 *
 * The Main tick count and the --trace file (trace_host.h). Package 2A
 * replaces the trace; the tick hook stays.
 */
#include "trace_host.h"
#include <stdint.h>
#include <stdio.h>
#include "cdvd_host.h"
#include "diag_host.h"
#include "host_fs.h"
#include "host_loop.h"
#include "pad_script.h"
#include "sif_host.h"

/* The game's state the trace reads (each defined in the file named). */
extern int stage_no;               /* common/src/main.c:428 */
extern int systemStatus[];         /* common/src/main.c:37, int[12] */
extern int gameover_flag;          /* common/src/main.c:493 */
extern char gameSysMainSaveBuff[]; /* common/src/gamesys.c:79 */
int gflagChk(int bit_idx);         /* script/src/gflag.c:58 */
/* port/game/options.h: developer mode (renderer wave 6, R6a) */
int ico_opt_developer_mode(void);
/* read by the heartbeat (ico_host_status) */
extern int fadeStatus;                   /* seki/src/Basic.c:124 */
extern int mpegPlay;                     /* common/src/StageManager.c:73 */
extern int mpegInitDone;                 /* common/src/StageManager.c:75 */
extern int stageManagerFreeResourceFlag; /* common/src/StageManager.c:81 */
extern int stgMgrWakeupRequest;          /* common/src/StageManager.c:83 */
extern int IosCdvdMgrSleep;              /* fumi/ios/cdvd.c:114 */
extern int iosCdvdBackGroundMgrRunning;  /* fumi/ios/cdvd.c */
extern int kanbanBootEnd;                /* common/src/kanbanBoot.c:25 */
extern int game_pause;                   /* common/src/main.c */
extern int current_layout_id;            /* common/src/layout_texture.c:34 */

#define SAVE_BUFF_SIZE 25596 /* gameSysMainSaveBuff[25596], gamesys.c:79 */
#define GFLAG_COUNT 400      /* gflags[50], script/src/gflag.c:14 */
#define GFLAG_WORDS ((GFLAG_COUNT + 31) / 32)

static unsigned int main_ticks;

static unsigned int traced_ticks;

static FILE *trace;

static int logged_stage = -1;

static int logged_layout = -1;

void ico_host_main_tick(void)
{
    if (main_ticks == 0) {
        ico_diag_milestone("first Main tick done (stage_no %d)", stage_no);
    }
    main_ticks++;
    ico_pad_script_set_tick(main_ticks);
    if (stage_no != logged_stage) {
        ico_diag_milestone("stage_no %d -> %d", logged_stage, stage_no);
        logged_stage = stage_no;
    }
    /* v0.4.2: the menu screen (layout_action.c's numbers: 12 the title
       with Continue, 20 the load's card check, 19 the file select, 25 the
       load itself), so a log that ends after Continue says which step it
       reached */
    if (current_layout_id != logged_layout) {
        ico_diag_milestone("menu screen %d -> %d", logged_layout, current_layout_id);
        logged_layout = current_layout_id;
    }
}

/* The heartbeat's game state (diag_host.h): only plain reads, it runs on
   the watchdog thread. */
void ico_host_status(char *out, size_t size)
{
    IcoCdvdStats cd;
    unsigned int sid;
    unsigned int rpc;
    unsigned int rpcs;

    ico_cdvd_host_stats(&cd);
    ico_sif_host_last_rpc(&sid, &rpc, &rpcs);
    snprintf(out, size,
             "vsync %u, tick %u, stage %d (sys5 %d sys6 %d sys7 %d sys8 %d), fade %d, mpeg %d/%d, "
             "stgmgr free %d wake %d, pause %d, kanbanEnd %d | cd: %u reads, %u sectors, last lsn "
             "%u, busy %d, %d waiting, stream %d@%u, cdvd thread %s%s | sif: %u calls, last "
             "0x%08x/0x%x",
             ico_host_vsync_count(), main_ticks, stage_no, systemStatus[5], systemStatus[6],
             systemStatus[7], systemStatus[8], fadeStatus, mpegPlay, mpegInitDone,
             stageManagerFreeResourceFlag, stgMgrWakeupRequest, game_pause, kanbanBootEnd, cd.reads,
             cd.sectors, cd.last_lsn, cd.busy, cd.waiters, cd.stream_active, cd.stream_lsn,
             IosCdvdMgrSleep ? "asleep" : "awake",
             iosCdvdBackGroundMgrRunning ? " (background reads)" : "", rpcs, sid, rpc);
}

unsigned int ico_host_main_ticks(void)
{
    return main_ticks;
}

int ico_host_stage_no(void)
{
    return stage_no;
}

int ico_trace_open(const char *path)
{
    int i;

    trace = ico_fopen(path, "w");
    if (trace == NULL) {
        fprintf(stderr, "trace: cannot create %s\n", path);
        return -1;
    }
    /* developer mode as the run starts: the
       menu and option table it opens can change the simulation, so a trace
       says which kind of run it is.  Comparisons skip '#' lines. */
    fprintf(trace, "# developer_mode %d\n", ico_opt_developer_mode() ? 1 : 0);
    fprintf(trace, "# tick vsync stage sys0 sys1 gameover");
    for (i = 0; i < GFLAG_WORDS; i++) {
        fprintf(trace, " gf%d", i);
    }
    fprintf(trace, " save\n");
    /* the header reaches the disc now, and every line after it (below), so
       a crash or a kill leaves the trace up to the last tick */
    fflush(trace);
    traced_ticks = main_ticks;
    return 0;
}

static uint32_t fnv1a(const unsigned char *p, unsigned int n)
{
    uint32_t h = 0x811C9DC5u;
    unsigned int i;

    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

static void write_line(unsigned int tick)
{
    int w;

    fprintf(trace, "%u %u %d %d %d %d", tick, ico_host_vsync_count(), stage_no, systemStatus[0],
            systemStatus[1], gameover_flag);
    for (w = 0; w < GFLAG_WORDS; w++) {
        uint32_t word = 0;
        int b;

        for (b = 0; b < 32 && w * 32 + b < GFLAG_COUNT; b++) {
            if (gflagChk(w * 32 + b)) {
                word |= (uint32_t)1 << b;
            }
        }
        fprintf(trace, " %08x", (unsigned int)word);
    }
    fprintf(trace, " %08x\n",
            (unsigned int)fnv1a((const unsigned char *)gameSysMainSaveBuff, SAVE_BUFF_SIZE));
}

void ico_trace_poll(void)
{
    if (trace == NULL) {
        return;
    }
    /* Main ticks at most once per vsync, so this is one line or none; a
       backlog would be written with the state as it is now. */
    while (traced_ticks < main_ticks) {
        write_line(traced_ticks);
        traced_ticks++;
        fflush(trace);
    }
}

void ico_trace_close(void)
{
    if (trace != NULL) {
        fclose(trace);
        trace = NULL;
    }
}
