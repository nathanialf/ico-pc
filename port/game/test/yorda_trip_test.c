/*
 * port/game/test/yorda_trip_test.c
 *
 * Yorda trips (port/game/yorda_trip.h) on the CPU, for the disc named on
 * the command line (pal, jpn, kor, cn, ntsc, aug6, jun26): the rule
 * (yorda_trip.c), the game's side (yorda_trip_live.c) and the motion reader
 * (yorda_trip_source.c), compiled in with the game and the disc stubbed
 * below.  The disc's numbers are the ones its motion table has (the
 * decomp's motion-def names), written out per disc in kDiscs.
 *
 *   names       the disc's run loop, the fall's three motions and her
 *               per-stage HETARI motions found by name, decoys left out (a
 *               blend motion of area 1, a boy's motion, a name without
 *               HETARI); the fall's exits from its motion-orient rows
 *               (SIT3END, the hand walk and run loops), KEEP left out
 *   count       consecutive run-pulled ticks counted; a second call on one
 *               tick counts nothing; a missed tick starts again at 1
 *   rate        10^6 continuous run-pulled ticks at 25 and at 30 ticks a
 *               second: trips within 20 % of one per ICO_YT_MEAN_S seconds,
 *               none in the first 3 s, none two closer than 30 s
 *   guards      no trip over 20 mean waits while one guard fails (paused,
 *               no control, the script holds Ico, game over, a gate stage,
 *               another motion, the fall or one of its exits not loaded
 *               (SIT3END, a motion set without it), her HETARI bit of this
 *               disc set, the option off); trips with every guard holding
 *               and the other discs' HETARI bits set; each trip mails her
 *               the caller's mail id with her as its argument
 *   state       the five blocks registered by name and size; their bytes
 *               taken and put back give the same trips again
 *   stage load  option off, a gate stage, the stage's own motions: nothing
 *               added; otherwise each missing motion in a block of the
 *               dynamic motion partition, its bytes the pack's, relocated
 *               (InitMotionFile) and counted (AddMotionMemorySize, the stage
 *               segment), the pack read once for many loads, picked by its
 *               members (not the first pack, not a non-motion member); a
 *               partition too full takes nothing and she does not trip
 *
 * Usage: yorda_trip_test <slug>; exit 0, 1 on a failure.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the game's stageData is const in its headers; this test writes its
   names, so the header's declaration is hidden and a writable one made
   (yorda_trip_live.c reads the same symbol) */
#define stageData stageData_header_decl
#include "typedef.h"
#include "main.h"
#include "boyact.h"
#include "script.h"
#include "obj_manager.h"
#include "ios.h"
#include "memory.h"
#include "motionFileManager.h"
#include "motionOrientManager.h"
#undef stageData

#include "../../core/region.h"
#include "df_pack.h"
#include "diag_host.h"
#include "options.h"
#include "tables.h"
#include "vfs.h"
#include "yorda_trip.h"
#include "../../platform/state_registry.h"
#include "../../test/ico_check.h"

void ico_opt_yorda_trip_tick(GObj *girl, int mail);

/* --- the game, stubbed ---------------------------------------------------- */
StgPre stageData[128];
MotionDef motionKind[1200];
MotionOrientEntry motionOrient[16];
static int s_orientRows;

uint32_t ico_tables_count(IcoTableId id)
{
    return id == ICO_TID_motionOrient ? (uint32_t)s_orientRows : 0u;
}

int *motionTable[1170];
int systemStatus[12];
int frame_count;
int gameover_flag;
int stage_no;
int scpBoyControlReadDisable;
static unsigned char s_able = 1;

unsigned char IsAbleBoyControl(void)
{
    return s_able;
}

/* diag_host.c's clock (the read's time in the log) */
unsigned long long ico_diag_now_ns(void)
{
    return 0;
}

static int s_mails, s_lastMail;
static GObj *s_mailTo;
static void *s_mailArg;

int iosOmSendMail(GObj *g, int type, void *arg)
{
    s_mails++;
    s_mailTo = g;
    s_lastMail = type;
    s_mailArg = arg;
    return 0;
}

/* the dynamic motion partition: one free node of a size the test sets */
static IosMemPart s_part;
static IosMemNode s_free;
struct IosMemPart *ios_partition_dmotion = &s_part;
static void *s_blocks[64];
static int s_nblocks;

static void setFree(long bytes)
{
    memset(&s_free, 0, sizeof s_free);
    memcpy(s_free.tag, "<FREE AREA>____", 16);
    s_free.size = (int)(bytes >> 4);
    s_part.head = &s_free;
}

void *iosMallocDebugNoAssert(IosMemPart *part, int size, const char *file, int line)
{
    const int need = (((size + 15) & ~15) + (int)sizeof(IosMemNode)) >> 4;

    (void)file;
    (void)line;
    if (part != &s_part || s_free.size < need || s_nblocks == 64) {
        return NULL;
    }
    s_free.size -= need;
    s_blocks[s_nblocks] = malloc((size_t)size);
    return s_blocks[s_nblocks++];
}

static int s_inits, s_addSeg = -1, s_addTotal;

void InitMotionFile(void *buf, char *name)
{
    (void)buf;
    (void)name;
    s_inits++;
}

int AddMotionMemorySize(int size, int seg)
{
    s_addSeg = seg;
    s_addTotal += size;
    return s_addTotal;
}

/* --- the disc, stubbed: three packs ----------------------------------------
 * 0 "STGLOG.DF": a texture whose id is the fall's first motion (not a motion
 * file); 1 "STGST04A.DF": other motions; 2 "STG4A2ND.DF": the disc's motions
 * and a second member with the fall's number (the first is kept). */
typedef struct FakeMember {
    int pack;
    int id;
    uint32_t size;
    char name[48];
} FakeMember;

struct IcoDfIndex {
    int dummy;
};

static struct IcoDfIndex s_index;
static FakeMember s_members[16];
static int s_nmembers;
static int s_opens;
static int s_vfs;
static const char *const kPacks[3] = {"STGLOG.DF", "STGST04A.DF", "STG4A2ND.DF"};

IcoVfs *ico_vfs_disc(void)
{
    return (IcoVfs *)&s_vfs;
}

const char *ico_vfs_location(const IcoVfs *vfs)
{
    return vfs != NULL ? "test.iso" : NULL;
}

IcoVfs *ico_vfs_reopen(const IcoVfs *vfs)
{
    return (IcoVfs *)vfs;
}

void ico_vfs_unmount(IcoVfs *vfs)
{
    (void)vfs;
}

IcoDfIndex *ico_df_index_open(IcoVfs *vfs)
{
    s_opens++;
    return vfs != NULL ? &s_index : NULL;
}

void ico_df_index_close(IcoDfIndex *idx)
{
    (void)idx;
}

int ico_df_index_member_count(const IcoDfIndex *idx)
{
    return idx != NULL ? s_nmembers : 0;
}

const char *ico_df_index_member_name(const IcoDfIndex *idx, int i)
{
    return idx != NULL && i >= 0 && i < s_nmembers ? s_members[i].name : NULL;
}

int ico_df_index_member_at(const IcoDfIndex *idx, int i, IcoDfMember *out)
{
    if (idx == NULL || i < 0 || i >= s_nmembers) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->pack = s_members[i].pack;
    out->size = s_members[i].size;
    out->id = s_members[i].id;
    return 0;
}

const char *ico_df_index_entry_name(const IcoDfIndex *idx, int i)
{
    return idx != NULL && i >= 0 && i < 3 ? kPacks[i] : NULL;
}

/* a member's bytes: its id and its offset */
static uint8_t byteOf(int id, uint32_t k)
{
    return (uint8_t)(id * 7 + k);
}

int ico_df_index_read_pack(IcoDfIndex *idx, int pack, IcoDfMemberFn fn, void *user)
{
    for (int i = 0; i < s_nmembers; i++) {
        if (s_members[i].pack != pack) {
            continue;
        }
        IcoDfMember m;
        uint8_t *data = malloc(s_members[i].size);
        ico_df_index_member_at(idx, i, &m);
        for (uint32_t k = 0; k < m.size; k++) {
            data[k] = byteOf(m.id, k);
        }
        int stop = fn(user, i, s_members[i].name, &m, data);
        free(data);
        if (stop) {
            return 1;
        }
    }
    return 0;
}

/* --- the discs' numbers ---------------------------------------------------- */
typedef struct Disc {
    const char *slug;
    int run_loop;
    int n;
    int ids[6];             /* her per-stage HETARI motions, table order */
    const char *names[6];   /* their names */
    int mail;               /* the sit-down mail of the disc's gate script */
    unsigned long long bit; /* her HETARI bit in flags20 on the disc */
    int sit3end, walk;      /* the fall's exits: GIRL SIT3END, GIRL HAND_WALKLOOP */
} Disc;

#define LATER(slug, mail)                                                                          \
    {slug,                                                                                         \
     580,                                                                                          \
     6,                                                                                            \
     {597, 669, 670, 671, 672, 673},                                                               \
     {"GIRL SIT3LOOP\xA1\xCAHETARI)", "GIRL STAND(HETARI)", "GIRL HAND_RUNLOOP(HETARI)",           \
      "GIRL SIT(HETARI)", "GIRL SITLOOP(HETARI)", "GIRL SITEND(HETARI)"},                          \
     mail,                                                                                         \
     1ULL << 16,                                                                                   \
     598,                                                                                          \
     576}
#define EARLY(slug, loop, first, mail, bit, sit3end, walk)                                         \
    {slug,                                                                                         \
     loop,                                                                                         \
     5,                                                                                            \
     {first, first + 1, first + 2, first + 3, first + 4},                                          \
     {"GIRL STAND(HETARI)", "GIRL HAND_RUNLOOP(HETARI)", "GIRL SIT(HETARI)",                       \
      "GIRL SITLOOP(HETARI)", "GIRL SITEND(HETARI)"},                                              \
     mail,                                                                                         \
     bit,                                                                                          \
     sit3end,                                                                                      \
     walk}

static const Disc kDiscs[] = {
    LATER("pal", 109),
    LATER("jpn", 109),
    LATER("kor", 109),
    LATER("cn", 109),
    EARLY("ntsc", 578, 584, 102, 1ULL << 6, 629, 574),
    EARLY("aug6", 579, 585, 101, 1ULL << 3, 631, 575),
    EARLY("jun26", 591, 599, 100, 1ULL << 1, 652, 586),
};

static const Disc *s_disc;
static int s_rows;
static int s_sit, s_sitLoop, s_sitEnd;

static void setName(int id, const char *name, int area)
{
    snprintf(motionKind[id].name, sizeof motionKind[id].name, "%s", name);
    motionKind[id].area = area;
}

static void fillTables(void)
{
    memset(motionKind, 0, sizeof motionKind);
    for (int i = 0; i < s_rows; i++) {
        snprintf(motionKind[i].name, sizeof motionKind[i].name, "BOY MOTION %d", i);
    }
    setName(s_disc->run_loop, "GIRL HAND_RUNLOOP", 1);
    for (int k = 0; k < s_disc->n; k++) {
        setName(s_disc->ids[k], s_disc->names[k], 4);
        if (strcmp(s_disc->names[k], "GIRL SIT(HETARI)") == 0) {
            s_sit = s_disc->ids[k];
        } else if (strcmp(s_disc->names[k], "GIRL SITLOOP(HETARI)") == 0) {
            s_sitLoop = s_disc->ids[k];
        } else if (strcmp(s_disc->names[k], "GIRL SITEND(HETARI)") == 0) {
            s_sitEnd = s_disc->ids[k];
        }
    }
    /* decoys: a node-blend motion of hers (area 1), a boy's per-stage one,
       one of hers without HETARI in its name */
    const int last = s_disc->ids[s_disc->n - 1];
    setName(last + 1, "GIRL YORO_F(HETARI)", 1);
    setName(last + 2, "BOY STAND(HETARI)", 4);
    setName(last + 3, "GIRL SIT3LOOP", 4);
    setName(s_disc->sit3end, "GIRL SIT3END", 1);
    setName(s_disc->walk, "GIRL HAND_WALKLOOP", 1);

    /* the fall's rows (the European disc's: SIT on to SITLOOP, SITEND and
       SIT3END; SITLOOP on to KEEP, SITEND and SIT3END; SITEND on to the
       hand walk and run loops), and two rows of another motion */
    const MotionOrientEntry rows[] = {
        {s_sit, 164, s_sitLoop, -1, 5, 0},       {s_sit, 16, s_sitEnd, -1, 5, 0},
        {s_sit, 1, s_disc->sit3end, -1, 5, 0},   {s_sitLoop, 164, s_rows, -1, -1, 0},
        {s_sitLoop, 16, s_sitEnd, 0, 10, 0},     {s_sitLoop, 1, s_disc->sit3end, 0, 10, 0},
        {s_sitEnd, 14, s_disc->walk, -1, 10, 0}, {s_sitEnd, 16, s_disc->run_loop, -1, 5, 0},
        {s_disc->run_loop, 21, s_sit, 0, 10, 0}, {s_disc->run_loop, 117, last + 3, 0, 10, 0},
    };
    memcpy(motionOrient, rows, sizeof rows);
    s_orientRows = (int)(sizeof rows / sizeof rows[0]);

    memset(stageData, 0, sizeof stageData);
    snprintf(stageData[11].name, sizeof stageData[11].name, "st04a (GATE_1ST)");
    snprintf(stageData[12].name, sizeof stageData[12].name, "st04a (GATE_2ND)");
    snprintf(stageData[15].name, sizeof stageData[15].name, "st09a (WINDMILL)");

    s_nmembers = 0;
    s_members[s_nmembers++] = (FakeMember){0, s_sit, 4000, "texture/logo.tm2"};
    s_members[s_nmembers++] = (FakeMember){1, s_disc->run_loop, 5000, "object/sdf/girl/a.mob"};
    for (int k = 0; k < s_disc->n; k++) {
        FakeMember m = {2, s_disc->ids[k], 1000u + 100u * (uint32_t)k, ""};
        snprintf(m.name, sizeof m.name, "object/sdf/girl/motion/m%d.mob", k);
        s_members[s_nmembers++] = m;
    }
    s_members[s_nmembers++] = (FakeMember){2, s_sit, 77, "object/sdf/girl/motion/again.mob"};
}

/* --- the world a trip needs ----------------------------------------------- */
static GObj s_girl;
static Act *s_act;
static Sub15C *s_sub;
static int s_dummy;

static void worldReady(int tps)
{
    systemStatus[0] = tps == 25 ? 1 : 0;
    systemStatus[1] = 2;
    systemStatus[5] = 0;
    s_able = 1;
    scpBoyControlReadDisable = 0;
    gameover_flag = 0;
    stage_no = 15;
    s_sub->ctrl.motion = s_disc->run_loop;
    s_act->flags20.ll = 0;
    motionTable[s_sit] = &s_dummy;
    motionTable[s_sitLoop] = &s_dummy;
    motionTable[s_sitEnd] = &s_dummy;
    motionTable[s_disc->sit3end] = &s_dummy;
    motionTable[s_disc->walk] = &s_dummy;
    motionTable[s_disc->run_loop] = &s_dummy;
    ico_opt_set_yorda_trips(1);
}

/* ticks [from, from + n) all run-pulled: the trips, the first trip's tick
   and the shortest gap between two */
static int runTicks(int from, int n, int *first, int *closest)
{
    int trips = 0, last = -1;

    *first = -1;
    *closest = 0x7FFFFFFF;
    for (int t = from; t < from + n; t++) {
        const int before = s_mails;
        frame_count = t;
        ico_opt_yorda_trip_tick(&s_girl, s_disc->mail);
        if (s_mails != before) {
            trips++;
            if (*first < 0) {
                *first = t;
            }
            if (last >= 0 && t - last < *closest) {
                *closest = t - last;
            }
            last = t;
        }
    }
    return trips;
}

/* --- the cases ------------------------------------------------------------- */
static void testNames(void)
{
    IcoYtIds ids;

    CHECK(ico_yorda_trip_find_ids(&ids, s_rows) == 0, "%s: the names are not found", s_disc->slug);
    CHECK(ids.run_loop == s_disc->run_loop, "%s: run loop %d, want %d", s_disc->slug, ids.run_loop,
          s_disc->run_loop);
    CHECK(ids.sit == s_sit && ids.sit_loop == s_sitLoop && ids.sit_end == s_sitEnd,
          "%s: the fall %d %d %d", s_disc->slug, ids.sit, ids.sit_loop, ids.sit_end);
    CHECK(ids.n == s_disc->n, "%s: %d motions, want %d", s_disc->slug, ids.n, s_disc->n);
    for (int k = 0; k < ids.n && k < s_disc->n; k++) {
        CHECK(ids.id[k] == s_disc->ids[k], "%s: motion %d is %d, want %d", s_disc->slug, k,
              ids.id[k], s_disc->ids[k]);
    }
    /* the exits: SIT3END, the walk and the run loops, once each, in row
       order; not KEEP, not the fall's own, not another motion's rows */
    CHECK(ids.n_exit == 3 && ids.exit[0] == s_disc->sit3end && ids.exit[1] == s_disc->walk &&
              ids.exit[2] == s_disc->run_loop,
          "%s: %d exits (%d %d %d)", s_disc->slug, ids.n_exit, ids.exit[0], ids.exit[1],
          ids.exit[2]);
    /* without the fall's names the disc is not taken */
    char keep[48];
    memcpy(keep, motionKind[s_sitEnd].name, sizeof keep);
    snprintf(motionKind[s_sitEnd].name, sizeof keep, "GIRL SITEND");
    CHECK(ico_yorda_trip_find_ids(&ids, s_rows) == -1);
    memcpy(motionKind[s_sitEnd].name, keep, sizeof keep);
}

static void testCount(void)
{
    const int tps = 30;

    ico_yorda_trip_reset();
    for (uint32_t t = 100; t < 100u + 3u * (uint32_t)tps; t++) {
        CHECK(ico_yorda_trip_step(t, tps, 1, 1) == 0, "a trip within the first 3 s at %u", t);
    }
    CHECK(ico_yorda_trip_run_ticks() == 3 * tps, "counted %d", ico_yorda_trip_run_ticks());
    ico_yorda_trip_step(100 + 3 * tps - 1, tps, 1, 1); /* the same tick again */
    CHECK(ico_yorda_trip_run_ticks() == 3 * tps, "a repeated tick counted: %d",
          ico_yorda_trip_run_ticks());
    ico_yorda_trip_step(100 + 3 * tps + 1, tps, 1, 1); /* one tick missed */
    CHECK(ico_yorda_trip_run_ticks() == 1, "after a gap %d", ico_yorda_trip_run_ticks());
    CHECK(ico_yorda_trip_rng() == 0, "the generator was seeded before a roll");
    CHECK(ico_yorda_trip_step(500, 0, 1, 1) == 0 && ico_yorda_trip_run_ticks() == 1,
          "no ticks a second counts");
}

static void testRate(int tps)
{
    const int n = 1000000;
    int first, closest;

    ico_yorda_trip_reset();
    worldReady(tps);
    s_mails = 0;
    const int trips = runTicks(1, n, &first, &closest);
    const double want = (double)n / ((double)ICO_YT_MEAN_S * tps);

    printf("yorda_trip_test: %s at %d ticks a second: %d trips in %d ticks (want about %.0f), "
           "first at %.1f s, closest %.1f s apart\n",
           s_disc->slug, tps, trips, n, want, first / (double)tps, closest / (double)tps);
    CHECK(trips > want * 0.8 && trips < want * 1.2, "%d trips, want %.0f +- 20 %%", trips, want);
    CHECK(first > ICO_YT_ELIGIBLE_S * tps, "a trip %d ticks in", first);
    CHECK(closest >= ICO_YT_REFRACTORY_S * tps, "two trips %d ticks apart", closest);
    CHECK(s_lastMail == s_disc->mail && s_mailTo == &s_girl && s_mailArg == &s_girl,
          "the mail %d to %p (%p)", s_lastMail, (void *)s_mailTo, s_mailArg);
}

static void testGuards(void)
{
    const int tps = 30;
    const int span = 20 * ICO_YT_MEAN_S * tps;
    int first, closest;

    for (int g = 0; g < 11; g++) {
        ico_yorda_trip_reset();
        worldReady(tps);
        switch (g) {
        case 0:
            systemStatus[5] = 1;
            break;
        case 1:
            s_able = 0;
            break;
        case 2:
            scpBoyControlReadDisable = 1;
            break;
        case 3:
            gameover_flag = 1;
            break;
        case 4:
            stage_no = 12;
            break;
        case 5:
            s_sub->ctrl.motion = s_disc->run_loop + 1;
            break;
        case 6:
            motionTable[s_sitLoop] = NULL;
            break;
        case 7:
            s_act->flags20.ll |= s_disc->bit;
            break;
        case 8:
            ico_opt_set_yorda_trips(0);
            break;
        case 9:
            motionTable[s_disc->sit3end] = NULL; /* a motion set without SIT3END */
            break;
        default:
            /* every guard holds; the other discs' HETARI bits are not hers */
            s_act->flags20.ll |=
                ((1ULL << 16) | (1ULL << 6) | (1ULL << 3) | (1ULL << 1)) & ~s_disc->bit;
            break;
        }
        s_mails = 0;
        const int trips = runTicks(1, span, &first, &closest);
        if (g < 10) {
            CHECK(trips == 0, "%s: guard %d failing, %d trips", s_disc->slug, g, trips);
        } else {
            CHECK(trips > 5, "%s: every guard holding, %d trips", s_disc->slug, trips);
        }
    }
    CHECK(ico_yorda_trip_run_ticks() > 0);
}

static void testState(void)
{
    static const char *const names[5] = {"yorda_trip.rng", "yorda_trip.seeded",
                                         "yorda_trip.run_ticks", "yorda_trip.refractory",
                                         "yorda_trip.last_tick"};
    const IcoStateEntry *e[5] = {0};
    unsigned char saved[5][8];
    int a[64], b[64], na = 0, nb = 0;
    const int tps = 25;

    ico_state_registry_reset();
    CHECK(ico_yorda_trip_state_register() == 0);
    CHECK(ico_yorda_trip_state_register() == -1, "a second registration repeats the names");
    for (int i = 0; i < ico_state_registry_count(); i++) {
        const IcoStateEntry *x = ico_state_registry_entry(i);
        for (int k = 0; k < 5; k++) {
            if (strcmp(x->name, names[k]) == 0) {
                e[k] = x;
            }
        }
    }
    for (int k = 0; k < 5; k++) {
        CHECK(e[k] != NULL && e[k]->n == 4 && e[k]->flags == 0, "block %s", names[k]);
        if (e[k] == NULL) {
            return;
        }
    }
    ico_yorda_trip_reset();
    worldReady(tps);
    int first, closest;
    runTicks(1, 200 * tps, &first, &closest); /* seeded, a trip or two, a refractory running */
    for (int k = 0; k < 5; k++) {
        memcpy(saved[k], e[k]->p, e[k]->n);
    }
    for (int t = 200 * tps; t < 1200 * tps && na < 64; t++) {
        const int before = s_mails;
        frame_count = t;
        ico_opt_yorda_trip_tick(&s_girl, s_disc->mail);
        if (s_mails != before) {
            a[na++] = t;
        }
    }
    for (int k = 0; k < 5; k++) {
        memcpy(e[k]->p, saved[k], e[k]->n);
    }
    for (int t = 200 * tps; t < 1200 * tps && nb < 64; t++) {
        const int before = s_mails;
        frame_count = t;
        ico_opt_yorda_trip_tick(&s_girl, s_disc->mail);
        if (s_mails != before) {
            b[nb++] = t;
        }
    }
    CHECK(na > 0 && na == nb, "%d trips, then %d after the blocks were put back", na, nb);
    for (int i = 0; i < na && i < nb; i++) {
        CHECK(a[i] == b[i], "trip %d at %d, then at %d", i, a[i], b[i]);
    }
    ico_state_registry_reset();
}

static int loadedCount(void)
{
    int n = 0;
    for (int k = 0; k < s_disc->n; k++) {
        n += motionTable[s_disc->ids[k]] != NULL;
    }
    return n;
}

static void clearMotions(void)
{
    for (int k = 0; k < s_disc->n; k++) {
        motionTable[s_disc->ids[k]] = NULL;
    }
}

static void testStageLoad(void)
{
    const int blocks0 = s_nblocks;

    clearMotions();
    setFree(4L << 20);
    ico_opt_set_yorda_trips(0);
    ico_yorda_trip_stage_loaded(15);
    CHECK(loadedCount() == 0 && s_opens == 0, "option off: %d loaded", loadedCount());

    ico_opt_set_yorda_trips(1);
    ico_yorda_trip_stage_loaded(12);
    ico_yorda_trip_stage_loaded(11);
    CHECK(loadedCount() == 0 && s_opens == 0, "a gate stage: %d loaded", loadedCount());

    s_inits = 0;
    s_addTotal = 0;
    ico_yorda_trip_stage_loaded(15);
    CHECK(s_opens == 1, "the pack opened %d times", s_opens);
    CHECK(loadedCount() == s_disc->n, "%d of %d loaded", loadedCount(), s_disc->n);
    CHECK(s_inits == s_disc->n && s_addSeg == 1, "%d relocated, segment %d", s_inits, s_addSeg);
    uint32_t total = 0;
    for (int k = 0; k < s_disc->n; k++) {
        const uint32_t size = 1000u + 100u * (uint32_t)k;
        const uint8_t *p = (const uint8_t *)motionTable[s_disc->ids[k]];
        int same = p != NULL;
        for (uint32_t j = 0; same && j < size; j++) {
            same = p[j] == byteOf(s_disc->ids[k], j);
        }
        CHECK(same, "motion %d's bytes are not the pack's first member's", s_disc->ids[k]);
        total += size;
    }
    CHECK(s_addTotal == (int)total, "counted %d bytes, want %u", s_addTotal, (unsigned)total);
    CHECK(s_nblocks - blocks0 == s_disc->n, "%d blocks", s_nblocks - blocks0);

    /* the stage holds them (its own pack or the last load): nothing more */
    ico_yorda_trip_stage_loaded(15);
    CHECK(s_nblocks - blocks0 == s_disc->n && s_opens == 1, "a second load allocated %d",
          s_nblocks - blocks0);

    /* the next stage (the reset cleared them): added again, the pack not reread */
    clearMotions();
    ico_yorda_trip_stage_loaded(15);
    CHECK(loadedCount() == s_disc->n && s_opens == 1, "the next stage: %d, %d opens", loadedCount(),
          s_opens);

    /* a partition too full: nothing, and no trip there */
    clearMotions();
    setFree(1L << 20);
    const int before = s_nblocks;
    ico_yorda_trip_stage_loaded(15);
    CHECK(loadedCount() == 0 && s_nblocks == before, "a full partition took %d", loadedCount());
    int first, closest;
    worldReady(30);
    clearMotions();
    s_mails = 0;
    CHECK(runTicks(1, 20 * ICO_YT_MEAN_S * 30, &first, &closest) == 0,
          "a trip without the fall loaded");
}

int main(int argc, char **argv)
{
    const char *slug = argc > 1 ? argv[1] : "pal";

    for (size_t i = 0; i < sizeof kDiscs / sizeof kDiscs[0]; i++) {
        if (strcmp(kDiscs[i].slug, slug) == 0) {
            s_disc = &kDiscs[i];
        }
    }
    if (s_disc == NULL || ico_region_select(slug) != 0) {
        fprintf(stderr, "yorda_trip_test: no disc %s\n", slug);
        return 1;
    }
    s_rows = ico_region_cur()->mv_motion_kinds;
    CHECK(s_rows > s_disc->ids[s_disc->n - 1] + 3 && s_rows <= 1170, "%d rows", s_rows);
    s_act = calloc(1, sizeof(Act));
    s_sub = calloc(1, sizeof(Sub15C));
    s_girl.act = s_act;
    s_girl.dobj = s_sub;
    fillTables();

    testNames();
    testCount();
    testRate(25);
    testRate(30);
    testGuards();
    testState();
    testStageLoad();

    for (int i = 0; i < s_nblocks; i++) {
        free(s_blocks[i]);
    }
    ico_yorda_trip_source_free(NULL);
    free(s_act);
    free(s_sub);
    return ico_check_summary("yorda_trip_test");
}
