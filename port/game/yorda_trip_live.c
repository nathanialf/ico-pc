/*
 * port/game/yorda_trip_live.c
 *
 * Yorda trips: the game's side (yorda_trip.h).  Built with the game's
 * include path and record layout, into the program (and into
 * yorda_trip_test with the game stubbed).
 *
 * The stage load (ico_yorda_trip_stage_loaded, from InitIcoMisc right
 * after the stage's pack): with the option on, every per-stage HETARI
 * motion of hers the stage did not load is put into the stage's motion
 * memory as charFileManager.c ReadMotionFile puts one there: a block of the
 * dynamic motion partition (ios_partition_dmotion, the stage partition),
 * the file copied in, InitMotionFile (which relocates it in place, so it
 * must lie in the arena) and AddMotionMemorySize; motionTable[id] then
 * holds it until the next stage change clears every per-stage entry
 * (motionFileManager.c ResetDynamicMotionManager) and resets the
 * partition.  The files are read once a run from the pack that holds them
 * (yorda_trip_source.c) and kept on the host heap as the pack has them.
 *
 * That read is the port's own disc I/O on the game thread (the stage load
 * runs in a game fiber): a second mount of the disc (ico_vfs_reopen) and an
 * index of its own (ico_df_index_open), made and dropped inside the call.
 * The only lock taken is that mount's own (vfs.c, around each read); the
 * game's disc reader thread (port/data/cdvd_host.c) reads the game's mount
 * and never waits for this one.
 *
 * Save states: the blocks lie in the arena and motionTable is the game's,
 * so a state carries the stage's motions as loaded; nothing a state holds
 * points at the host copies, and they are only read at the next stage load.
 *
 * The tick (ico_opt_yorda_trip_tick, from each disc's actGirlHand where it
 * sets her run-pulled bit) checks the guards and asks the rule
 * (yorda_trip.c, the gate scene's 3 s count); a trip is the watcher's own
 * mail (st04a.c actSt04aGirlSitChk: iosOmSendMail(girl, mail, girl)), the
 * disc's mail id passed by the caller.
 */
#include <stdio.h>
#include <string.h>

#include "typedef.h"
#include "main.h"
#include "boyact.h"
#include "script.h"
#include "obj_manager.h"
#include "ios.h"
#include "memory.h"
#include "motionFileManager.h"
#include "motionOrientManager.h"

#include "../core/region.h" /* ico_region_cur */
#include "df_pack.h"
#include "diag_host.h"
#include "options.h"
#include "vfs.h"
#include "yorda_trip.h"

/* port/data/gen/table_defs.c's writable definition (the table loader fills
   it); motionOrientManager.h declares none */
extern MotionDef motionKind[];

/* the actGirlHand copies' hook (ico2/fumi/src/girl_act.c declares it) */
void ico_opt_yorda_trip_tick(GObj *girl, int mail);

/* the stage partition keeps at least this much free after her motions go
   in (the host gives the partition 4 MB over the PS2's, ios.c
   ICO_HOST_STAGE_EXTRA; the motions take about 120 KB) */
#define YT_STAGE_RESERVE (1024 * 1024)

/* the disc's numbers, for the region they were read for */
static IcoYtIds s_ids;
static const IcoRegionInfo *s_ids_for;
static int s_ids_ok;

/* the files, read once for the disc mounted from s_src_disc */
static IcoYtSource s_src;
static int s_src_state; /* 0 not read, 1 read, -1 not on this disc or not readable */
static char s_src_disc[512];

/* trips this run (the log's rate limit) */
static unsigned s_trips;

static int idsReady(void)
{
    if (s_ids_for != ico_region_cur()) {
        s_ids_for = ico_region_cur();
        s_ids_ok = ico_yorda_trip_find_ids(&s_ids, ico_region_cur()->mv_motion_kinds) == 0;
    }
    return s_ids_ok;
}

/* the main gate's two stages, whose own script and pack play her fall */
static int gateStage(int stage)
{
    return stage >= 0 && stage < ico_region_cur()->stage_count &&
           strncmp(stageData[stage].name, "st04a ", 6) == 0;
}

static int loaded(int id)
{
    return id >= 0 && motionTable[id] != 0;
}

/* every motion the fall leads on to is the stage's own or loaded: the
   motion set of the stages without her hand walk (MOTION2.DF on the
   European disc) lacks SIT3END and the hand loops */
static int exitsLoaded(void)
{
    for (int k = 0; k < s_ids.n_exit; k++) {
        if (motionKind[s_ids.exit[k]].area != 0 && !loaded(s_ids.exit[k])) {
            return 0;
        }
    }
    return 1;
}

static int sourceReady(void)
{
    IcoVfs *disc = ico_vfs_disc();
    const char *where = disc != NULL ? ico_vfs_location(disc) : NULL;
    char why[160];

    if (where == NULL) {
        return 0;
    }
    if (s_src_state != 0 && strcmp(where, s_src_disc) == 0) {
        return s_src_state > 0;
    }
    ico_yorda_trip_source_free(&s_src);
    snprintf(s_src_disc, sizeof s_src_disc, "%s", where);
    const unsigned long long t0 = ico_diag_now_ns();
    IcoVfs *vfs = ico_vfs_reopen(disc);
    IcoDfIndex *idx = vfs != NULL ? ico_df_index_open(vfs) : NULL;
    const int rc = ico_yorda_trip_source_read(idx, &s_ids, &s_src, why, sizeof why);
    ico_df_index_close(idx);
    ico_vfs_unmount(vfs);
    s_src_state = rc == 0 && s_src.n > 0 ? 1 : -1;
    fprintf(stderr, "yorda trips: %s in %.0f ms%s\n", why, (double)(ico_diag_now_ns() - t0) / 1e6,
            s_src_state > 0 ? "" : ": she does not trip on this disc");
    return s_src_state > 0;
}

/* the free bytes of a partition, from its free list (read only) */
static long freeBytes(const IosMemPart *part, long *largest)
{
    long sum = 0;
    int n = 0;

    *largest = 0;
    for (const IosMemNode *node = part != NULL ? part->head : NULL; node != NULL && n < 1 << 20;
         node = node->free_next, n++) {
        if (strncmp(node->tag, "<FREE AREA>", 11) != 0) {
            break;
        }
        const long bytes = (long)node->size << 4;
        sum += bytes;
        *largest = bytes > *largest ? bytes : *largest;
    }
    return sum;
}

void ico_yorda_trip_stage_loaded(int stage)
{
    const IcoYtCopy *add[ICO_YT_MAX_MOTIONS];
    int missing = 0;
    long need = 0;
    long biggest = 0;
    long largest = 0;

    if (!ico_opt_yorda_trips()) {
        return;
    }
    if (gateStage(stage)) {
        fprintf(stderr, "yorda trips: stage %d (%s) plays her own fall: nothing added\n", stage,
                stageData[stage].name);
        return;
    }
    if (!idsReady()) {
        fprintf(stderr, "yorda trips: this disc's motion table does not name her fall: she does "
                        "not trip\n");
        return;
    }
    if (!sourceReady()) {
        return;
    }
    for (int k = 0; k < s_src.n; k++) {
        const IcoYtCopy *c = &s_src.m[k];

        if (!loaded(c->id)) {
            add[missing++] = c;
            need += (long)((c->size + 15u) & ~15u) + (long)sizeof(IosMemNode);
            biggest = (long)c->size > biggest ? (long)c->size : biggest;
        }
    }
    if (missing == 0) {
        fprintf(stderr, "yorda trips: stage %d loads her %d motions itself\n", stage, s_src.n);
        return;
    }
    const long left = freeBytes(ios_partition_dmotion, &largest);
    if (left - need < YT_STAGE_RESERVE || largest < biggest + (long)sizeof(IosMemNode)) {
        fprintf(stderr,
                "yorda trips: the stage's motion memory is full (stage %d: %ld KB free, %ld KB "
                "needed with %d KB kept free): Yorda does not trip here\n",
                stage, left / 1024, need / 1024, YT_STAGE_RESERVE / 1024);
        return;
    }
    int added = 0;
    unsigned bytes = 0;
    for (int k = 0; k < missing; k++) {
        const IcoYtCopy *c = add[k];
        void *p = iosMallocDebugNoAssert(ios_partition_dmotion, (int)c->size, "yorda_trip.c", 1);

        if (p == NULL) {
            fprintf(stderr,
                    "yorda trips: the stage's motion memory is full (stage %d, motion %d, %u "
                    "bytes): Yorda does not trip here\n",
                    stage, c->id, (unsigned)c->size);
            break;
        }
        memcpy(p, c->data, c->size);
        InitMotionFile(p, (char *)c->name);
        AddMotionMemorySize((int)c->size, 1);
        motionTable[c->id] = (int *)p;
        added++;
        bytes += c->size;
    }
    if (added == missing) {
        fprintf(stderr,
                "yorda trips: %d motions (%u KB) added to the stage's motion memory (stage %d, "
                "%ld KB left)\n",
                added, (bytes + 1023u) / 1024u, stage,
                freeBytes(ios_partition_dmotion, &largest) / 1024);
    }
}

/* the bit of hers that turns on the exhausted (HETARI) motion set: the
   watcher sets it in the gate stage and the ending keeps it (act-game.c
   ACTParaStatus_Exec and its 2001 copies); the port never sets it */
static unsigned long long hetariBit(void)
{
    return PAL_NTSC_AUG6_JUN26(1ULL << 16, 1ULL << 6, 1ULL << 3, 1ULL << 1);
}

/* every condition for a trip on this tick, the tick stamp aside */
static int guards(GObj *girl)
{
    return systemStatus[5] == 0 && IsAbleBoyControl() && scpBoyControlReadDisable == 0 &&
           gameover_flag == 0 && !gateStage(stage_no) && idsReady() &&
           GOBJ_SUB(girl)->ctrl.motion == s_ids.run_loop && loaded(s_ids.sit) &&
           loaded(s_ids.sit_loop) && loaded(s_ids.sit_end) && exitsLoaded() &&
           (GOBJ_ACT(girl)->flags20.ll & hetariBit()) == 0;
}

void ico_opt_yorda_trip_tick(GObj *girl, int mail)
{
    if (girl == NULL || !ico_opt_yorda_trips() || systemStatus[1] <= 0) {
        return;
    }
    const int tps = (60 - systemStatus[0] * 10) / systemStatus[1];
    const uint32_t tick = (uint32_t)frame_count;

    if (!ico_yorda_trip_step(tick, tps, guards(girl))) {
        return;
    }
    iosOmSendMail(girl, mail, girl);
    s_trips++;
    if (s_trips <= 8 || s_trips % 100 == 0) {
        fprintf(stderr,
                "yorda trips: tripped at stage %d after %.0f s of running (trip %u this run)\n",
                stage_no, tps > 0 ? (double)(ico_yorda_trip_last_run() - 1) / tps : 0.0, s_trips);
    }
}
