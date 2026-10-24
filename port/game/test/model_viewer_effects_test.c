/*
 * port/game/test/model_viewer_effects_test.c
 *
 * Issue 104: the model viewer crashed in stage 8 at the boy's SWIM, whose
 * frame-dependent sequence (ico2/sugipon/src/frameDependSequence.c,
 * ExecFrameDependSequence -> execEff -> playEff) enters a stage animation
 * (EntryStageMultiBgaManager, ico2/sugipon/src/stageMultiBgaManager.c) that
 * stage 8 does not load: stage_MakePlayBgAnimation returned 0 and the effect's
 * position was written through it.  Both game files are compiled into the
 * test; everything they call is a stub here, and the stage holds no
 * animation (stage_MakePlayBgAnimation returns 0) unless a case makes one.
 *
 * guard   EntryStageMultiBgaManagerWithStay and
 *         EntryStageMultiBgaManagerSensitiveWithStay with no node return
 *         without writing (no EntryMultiBgaManager call, the slot count kept:
 *         the next effect with a node takes the same slot), print
 *         "stage: no animation node for effect kind N (stage S): the effect
 *         is skipped" once per run, and with a node do what the PS2 does (the
 *         node gets the position and turn, the slot count steps).
 * sweep   every motion of every model of Extras > Models
 *         (port/game/model_viewer_table.c, mv_models) is stepped through
 *         ExecFrameDependSequence with every slot due (the frame past the
 *         motion's end), in two floor and water states so each condition
 *         branch of execEff and playSEConditionID is taken; first in the
 *         game (ico_mv_active 0: the guard skips every stage animation, none
 *         faults) and then in the viewer (ico_mv_active 1: no stage animation
 *         and no particle effect is entered; the sounds and the vibration are
 *         the game's, call for call).
 *
 * The motion tables: with the PAL base ELF (argv[2]) the game's own, filled
 * by the table loader (port/data/tables.c), and the SWIM motion must enter a
 * stage animation (the crash's path); without it a synthetic set that takes
 * every branch (the plain, random and conditional effect ids, stage
 * animations with and without the water height, particles).
 *
 * playable (the ELF run only) issue 136: the viewer's list rule
 *         (model_viewer_motion.c).  With the dynamic pool empty (area 4
 *         unloaded, every other area loaded: the reporter's Yorda stage, where
 *         the swap motion 542 GIRL YORO_F was loaded), 664-668 (HINT_POINT_*,
 *         NO_WALK*: plain, area 0) are playable and 674 "GIRL YORO_F(HETARI)",
 *         a node-blend motion whose second partner (669, area 4) is not
 *         loaded, is not, and names that partner; with 669 loaded 674 is; and
 *         for every motion of every
 *         viewer model "playable" means every partner of its blend list is in
 *         range, a plain motion and loaded.
 *
 * Usage: model_viewer_effects_test <outdir> [<base ELF>]; exit 0, 1 on a
 * failure, 77 when the base ELF argument is empty or names no file.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The tables this test fills are declared const in the game's headers
   (frameDependSequence.h) and defined writable in port/data/gen/table_defs.c
   (the loader fills them from the ELF). A store through a pointer cast from
   the const declaration is undefined, and clang drops it (LLVM marks the
   global constant): the condition table then has no -1 terminator and
   playSEConditionID walks off the end of .bss (the clang CI builds' segfault).
   So the headers' declarations are hidden, as table_desc.c hides them, and
   the writable ones declared below. */
#define motEffCondKind motEffCondKind_header_decl
#define motSECondKind motSECondKind_header_decl
#define motionEffKind motionEffKind_header_decl
#define randomEffKind randomEffKind_header_decl
#define randomSEKind randomSEKind_header_decl
#include "typedef.h"
#include "debug.h"
#include "pad.h"
#include "main.h"
#include "Matrix.h"
#include "StageAnimation.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "motionFileManager.h"
#include "motionManager2.h"
#include "motionOrientManager.h"
#include "multiBgaManager.h"
#include "particleEffect.h"
#include "quaternion.h"
#include "s_init.h"
#include "stageMultiBgaManager.h"
#include "weapon.h"

#include "model_viewer.h"
#include "options.h"
#include "tables.h"

#undef motEffCondKind
#undef motSECondKind
#undef motionEffKind
#undef randomEffKind
#undef randomSEKind
/* port/data/gen/table_defs.c's writable definitions; motionOrientManager.h
   declares motionKind not at all */
extern MotionDef motionKind[];
extern VibCondEntry motEffCondKind[];
extern SECondEntry motSECondKind[];
extern EffEntry motionEffKind[];
extern int randomEffKind[];
extern SERandEntry randomSEKind[];

static int failures;

/* motionFileManager.c's table of the loaded motions' data (not compiled in) */
int *motionTable[1150];

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ------------------------------------------------------------------------ */
/* the game's globals and calls the two files reach                          */

int ico_mv_active;
int stage_no = 8;
int systemStatus[12];
int girlControlMode;
IosPadCtx *girlPad;
IosPadCtx *boyPad;
int debug_seslotdisp_flag;
BgaAnimeState InitialBgaMultiAnimeState
    __attribute__((aligned(16))) = {-1.0f, {0}, {0}, {0}, {0, 0, 0, 1}, -1, 0, {0}};

/* what the stubs saw */
static struct {
    int makeNode;    /* stage_MakePlayBgAnimation calls */
    int lastKind;    /* its last key */
    int entryMulti;  /* EntryMultiBgaManager and ...Sensitive calls */
    int lastMultiNo; /* the slot the last one was handed */
    int particles;   /* SetParticleEffect calls */
    int se;          /* soundSeDefPlay and ...WithVolumeRate calls */
    unsigned seSum;  /* their ids, summed */
    int vib;         /* iosPadActRequest calls */
    unsigned vibSum; /* their ids, summed */
} seen;

static BgaPlayNode *s_node; /* what stage_MakePlayBgAnimation returns */

BgaPlayNode *stage_MakePlayBgAnimation(int key)
{
    seen.makeNode++;
    seen.lastKind = key;
    return s_node;
}

int stage_DispBgAnimation(BgaPlayNode **self)
{
    (void)self;
    return -1;
}

int stage_DispBgAnimationNoFinish(BgaPlayNode **self)
{
    (void)self;
    return -1;
}

void EntryMultiBgaManager(BgaDisp *bga, int no, int kind, void *pos, void *rot)
{
    (void)kind;
    seen.entryMulti++;
    seen.lastMultiNo = no;
    memcpy(bga[no].pos, pos, sizeof(bga[no].pos));
    memcpy(bga[no].rot, rot, sizeof(bga[no].rot));
}

void EntryMultiBgaManagerSensitive(BgaDisp *bga, int no, int kind, void *pos, void *rot, void *vel)
{
    EntryMultiBgaManager(bga, no, kind, pos, rot);
    memcpy(bga[no].vel, vel, sizeof(bga[no].vel));
}

void _CopyVector(void *dst, void *src)
{
    memcpy(dst, src, 16);
}

void _AddVector(void *dst, void *a, void *b)
{
    float *d = dst, *x = a, *y = b;
    for (int i = 0; i < 4; i++) {
        d[i] = x[i] + y[i];
    }
}

void CopyQuaternion(void *dst, void *src)
{
    memcpy(dst, src, 16);
}

void CopyVector(void *dst, void *src)
{
    memcpy(dst, src, 16);
}

void CopyMatrix(void *dst, void *src)
{
    memcpy(dst, src, 64);
}

static float s_mtx[4][4];

float (*MatrixDrive_GetMatrix(void))[4]
{
    return s_mtx;
}

void MatrixDrive_TransMatrix(float x, float y, float z)
{
    s_mtx[3][0] += x;
    s_mtx[3][1] += y;
    s_mtx[3][2] += z;
}

void GetRootQuaternion(void *q, struct GObj *obj)
{
    (void)obj;
    static const float id[4] = {0, 0, 0, 1};
    memcpy(q, id, sizeof(id));
}

void GetRootMatrix(void *mtx, struct GObj *obj)
{
    (void)obj;
    memset(mtx, 0, 64);
}

void RotQuaternionX(void *self, short ang)
{
    (void)self;
    (void)ang;
}

void RotQuaternionY(void *self, short ang)
{
    (void)self;
    (void)ang;
}

void RotQuaternionZ(void *self, short ang)
{
    (void)self;
    (void)ang;
}

int GetSkeltonFocusNode(GObj *self, int focus)
{
    (void)self;
    return focus & 7; /* inside the stub's eight node matrices */
}

int SetParticleEffect(int id, void *pos, void *quat)
{
    (void)id;
    (void)pos;
    (void)quat;
    seen.particles++;
    return 0;
}

static int s_floorMatch; /* the floor and wall attribute tests' answer */

int CheckFloorAttribute(GObj *self, int attr)
{
    (void)self;
    /* 0x40000 gates the effects in ExecFrameDependSequence: never set */
    return attr == 0x40000 ? 0 : s_floorMatch;
}

int CheckWallAttribute(GObj *self, int attr)
{
    (void)self;
    (void)attr;
    return s_floorMatch;
}

int CheckWeaponKind(struct GObj *self)
{
    (void)self;
    return 0;
}

void LightTorchOffOfWeapon(struct GObj *self)
{
    (void)self;
}

int iosPadActRequest(IosPadCtx *pad, int id)
{
    (void)pad;
    seen.vib++;
    seen.vibSum += (unsigned)id;
    return 0;
}

void iosPadActStop(int key)
{
    (void)key;
}

void soundSeGroupStop(int arg)
{
    (void)arg;
}

int soundSeDefPlay(int se, unsigned int owner, ICO_WORD_PTR(float *) pos, int playMode)
{
    (void)owner;
    (void)pos;
    (void)playMode;
    seen.se++;
    seen.seSum += (unsigned)se;
    return 0;
}

int soundSeDefPlayWithVolumeRate(int se, unsigned int owner, ICO_WORD_PTR(float *) pos,
                                 int playMode, float rate)
{
    (void)rate;
    return soundSeDefPlay(se, owner, pos, playMode);
}

void seMail(GObj *self, int id)
{
    (void)self;
    (void)id;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

/* ------------------------------------------------------------------------ */

static char s_logPath[1024];

/* the lines the run has printed to stderr (redirected to the log file) that
   contain needle */
static int logCount(const char *needle)
{
    fflush(stderr);
    FILE *f = fopen(s_logPath, "r");
    char line[512];
    int n = 0;
    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        if (strstr(line, needle) != NULL) {
            n++;
        }
    }
    fclose(f);
    return n;
}

static void testGuard(void)
{
    float pos[4] = {1, 2, 3, 1};
    float rot[4] = {0, 0.5f, 0, 0.5f};
    float vel[4] = {0, 1, 0, 0};
    BgaPlayNode *node = calloc(1, sizeof(BgaPlayNode));

    memset(&seen, 0, sizeof seen);
    stage_no = 8;
    InitStageMultiBgaManager();

    /* no node: nothing written, no slot used, one log line */
    s_node = NULL;
    EntryStageMultiBgaManager(512, pos, rot);
    EntryStageMultiBgaManagerSensitive(513, pos, rot, vel);
    EntryStageMultiBgaManagerWithStay(514, pos, rot, 1);
    EntryStageMultiBgaManagerSensitiveWithStay(515, pos, rot, vel, 1);
    CHECK(seen.makeNode == 4, "no node: %d node requests, 4 expected", seen.makeNode);
    CHECK(seen.entryMulti == 0, "no node: %d multi-BGA entries, 0 expected", seen.entryMulti);
    CHECK(logCount("stage: no animation node for effect kind 512 (stage 8): the effect is "
                   "skipped") == 1,
          "no node: the log line for kind 512 in stage 8, once");
    CHECK(logCount("stage: no animation node") == 1, "no node: the log line is printed once");

    /* a node: the PS2's path, in slot 0 (the skipped effects took none) */
    s_node = node;
    EntryStageMultiBgaManager(516, pos, rot);
    CHECK(seen.entryMulti == 1 && seen.lastMultiNo == 0,
          "node: entry %d in slot %d, 1 in slot 0 expected", seen.entryMulti, seen.lastMultiNo);
    CHECK(memcmp(node->pos, pos, sizeof pos) == 0 && memcmp(node->rot, rot, sizeof rot) == 0,
          "node: the position and turn are the effect's");
    EntryStageMultiBgaManagerSensitive(517, pos, rot, vel);
    CHECK(seen.entryMulti == 2 && seen.lastMultiNo == 1,
          "sensitive node: entry %d in slot %d, 2 in slot 1 expected", seen.entryMulti,
          seen.lastMultiNo);

    /* no node again between two with one: the slot count is kept */
    s_node = NULL;
    EntryStageMultiBgaManagerSensitive(518, pos, rot, vel);
    s_node = node;
    EntryStageMultiBgaManager(519, pos, rot);
    CHECK(seen.entryMulti == 3 && seen.lastMultiNo == 2,
          "after a skip: entry %d in slot %d, 3 in slot 2 expected", seen.entryMulti,
          seen.lastMultiNo);
    CHECK(logCount("stage: no animation node") == 1, "the log line is still printed once");

    s_node = NULL;
    InitStageMultiBgaManager();
    free(node);
    printf("guard: %s\n", failures == 0 ? "ok" : "FAILED");
}

/* ------------------------------------------------------------------------ */

/* The synthetic tables: every effect id kind execEff takes (plain, random,
   conditional), stage animations with and without the water height, at the
   root and at a node, and particles; sounds plain, random and conditional;
   vibration */
static void fillSynthetic(void)
{
    EffEntry *eff = motionEffKind;
    int *rnd = randomEffKind;
    VibCondEntry *cond = motEffCondKind;
    SECondEntry *seCond = motSECondKind;
    SERandEntry *seRnd = randomSEKind;

    /* 1: a stage animation at the root; 2: a particle at a node; 3: a stage
       animation at a node at the water's height; 4: a particle at the root */
    eff[1] = (EffEntry){0, 10, 0, 0, 90, 0, 500, -1, 1};
    eff[2] = (EffEntry){0, 0, 5, 0, 0, 0, 7, 3, 0};
    eff[3] = (EffEntry){0, 0, 0, 0, 0, 0, 501, 2, 3};
    eff[4] = (EffEntry){0, 0, 0, 0, 0, 0, 8, -1, 0};
    /* random 0: 1, 2 or 3 */
    rnd[0] = 1;
    rnd[1] = 2;
    rnd[2] = 3;
    rnd[3] = 0x18;
    /* condition 0: water shallower than 50 -> 3, else floor attribute 1 -> 1,
       else 4 */
    cond[0] = (VibCondEntry){2, 50, 3};
    cond[1] = (VibCondEntry){0, 1, 1};
    cond[2] = (VibCondEntry){0, -1, 4};
    cond[3] = (VibCondEntry){-1, 0, 0};
    /* sound condition 0: floor 1 -> 21, water under 50 -> 22; random 0: 31 or 32 */
    seCond[0] = (SECondEntry){0, 1, 21};
    seCond[1] = (SECondEntry){2, 50, 22};
    seCond[2] = (SECondEntry){-1, 0, 0};
    seRnd[0] = (SERandEntry){31, 50.0f};
    seRnd[1] = (SERandEntry){32, -1.0f};
    seRnd[2] = (SERandEntry){0, 0};

    static const int effIds[] = {1, 2, 3, 4, 0x10000, 0x20000, 0x18, 0};
    static const int seIds[] = {11, 0x10000, 0x20000, 12};
    for (int m = 0; m < 1148; m++) {
        MotionDef *d = (MotionDef *)&motionKind[m];
        memset(d, 0, sizeof *d);
        snprintf(d->name, sizeof d->name, "SYNTH %d", m);
        for (int i = 0; i < 12; i++) {
            d->eff[i].t = i < 6 ? (float)(i * 3) : -1.0f;
            d->eff[i].no = effIds[(m + i) % 8];
            d->se[i].t = i < 4 ? (float)i : -1.0f;
            d->se[i].no = seIds[(m + i) % 4];
        }
        d->vib[0].t = 1.0f;
        d->vib[0].no = 1 + m % 3;
        d->vib[1].t = -1.0f;
        d->weaponFrame = -1.0f;
    }
}

typedef struct {
    int makeNode, entryMulti, particles, se, vib;
    unsigned seSum, vibSum;
    int swimStage; /* stage animation requests of the BOY SWIM motion */
} Pass;

static GObj s_gobj;
static Sub15C *s_sub;

/* every motion of every viewer model, every slot due, in the two floor and
   water states */
static Pass sweep(int viewer)
{
    Pass p;
    memset(&p, 0, sizeof p);
    memset(&seen, 0, sizeof seen);
    ico_mv_active = viewer;
    s_node = NULL;
    InitStageMultiBgaManager();
    srand(104); /* crt_random_unit's rand(): the same draws in both passes */
    for (int state = 0; state < 2; state++) {
        s_floorMatch = state;
        s_sub->ctrl.waterDepth = state ? 1000.0f : 0.0f;
        for (int k = 0; k < mv_modelCount; k++) {
            const MvModel *m = &mv_models[k];
            for (int id = m->motFirst; id < m->motLast; id++) {
                int before = seen.makeNode;
                s_sub->ctrl.motion = id;
                s_sub->ctrl.animFrame = 100000.0f;
                InitFrameDependSequence(s_sub->fdsFlags);
                ExecFrameDependSequence(&s_gobj);
                if (strcmp(motionKind[id].name, "BOY SWIM") == 0) {
                    p.swimStage += seen.makeNode - before;
                }
            }
        }
    }
    p.makeNode = seen.makeNode;
    p.entryMulti = seen.entryMulti;
    p.particles = seen.particles;
    p.se = seen.se;
    p.vib = seen.vib;
    p.seSum = seen.seSum;
    p.vibSum = seen.vibSum;
    return p;
}

static void testSweep(int real)
{
    static float nodes[8][4][4];
    Pass game, viewer;

    memset(&s_gobj, 0, sizeof s_gobj);
    s_sub = calloc(1, sizeof(Sub15C));
    s_sub->nodeMtx = (ICO_WORD)(uintptr_t)nodes;
    s_sub->modelId = 0;
    s_gobj.dobj = s_sub;
    s_gobj.drawMask = 1;

    game = sweep(0);
    viewer = sweep(1);

    int motions = 0;
    for (int k = 0; k < mv_modelCount; k++) {
        motions += mv_models[k].motLast - mv_models[k].motFirst;
    }
    printf("sweep (%s tables): %d models, %d motions, two floor and water states\n",
           real ? "the PAL ELF's" : "synthetic", mv_modelCount, motions);
    printf("  game:   %d stage animation requests (no node: skipped), %d multi-BGA entries, "
           "%d particles, %d sounds, %d vibrations\n",
           game.makeNode, game.entryMulti, game.particles, game.se, game.vib);
    printf("  viewer: %d stage animation requests, %d particles, %d sounds, %d vibrations\n",
           viewer.makeNode, viewer.particles, viewer.se, viewer.vib);
    if (real) {
        printf("  BOY SWIM: %d stage animation requests in the game pass\n", game.swimStage);
    }

    CHECK(game.makeNode > 0, "game: the sweep entered no stage animation");
    CHECK(game.entryMulti == 0, "game: %d multi-BGA entries with no node", game.entryMulti);
    CHECK(game.particles > 0 || real, "game: the synthetic sweep entered no particle");
    CHECK(viewer.makeNode == 0, "viewer: %d stage animation requests", viewer.makeNode);
    CHECK(viewer.particles == 0, "viewer: %d particles", viewer.particles);
    CHECK(viewer.se == game.se && viewer.seSum == game.seSum,
          "viewer: sounds %d (sum %u), the game's %d (sum %u)", viewer.se, viewer.seSum, game.se,
          game.seSum);
    CHECK(viewer.vib == game.vib && viewer.vibSum == game.vibSum,
          "viewer: vibrations %d (sum %u), the game's %d (sum %u)", viewer.vib, viewer.vibSum,
          game.vib, game.vibSum);
    CHECK(viewer.se > 0 && viewer.vib > 0, "the sweep played no sound or vibration");
    if (real) {
        CHECK(game.swimStage > 0, "BOY SWIM entered no stage animation (issue 104's path)");
    }
    ico_mv_active = 0;
    free(s_sub);
    s_sub = NULL;
}

/* the rule's own reading of a partner list, apart from model_viewer_motion.c */
static int partnersLoaded(int id)
{
    int rows = (int)ico_tables_count(ICO_TID_blendMotionKind);
    int n = 0;
    for (int j = motionKind[id].blendKind; j >= 0 && j < rows; j++) {
        int m = blendMotionKind[j].motion;
        if (m == 1147) {
            return n > 0;
        }
        n++;
        if (m < 0 || m >= MV_MOTION_KINDS || motionKind[m].blendKind != 320 ||
            (motionKind[m].area != 0 && motionTable[m] == NULL)) {
            return 0;
        }
    }
    return 0;
}

static void testPlayable(void)
{
    static int dummy;
    int missing = -2;

    CHECK(ico_tables_count(ICO_TID_blendMotionKind) > 0, "playable: the blend table is empty");
    for (int i = 0; i < MV_MOTION_KINDS; i++) {
        motionTable[i] = motionKind[i].area != 4 ? &dummy : NULL;
    }
    for (int id = 664; id <= 668; id++) {
        CHECK(ico_mv_motion_playable(id, NULL), "playable: motion %d \"%s\" is not", id,
              motionKind[id].name);
    }
    CHECK(strcmp(motionKind[674].name, "GIRL YORO_F(HETARI)") == 0, "motion 674 is \"%s\"",
          motionKind[674].name);
    CHECK(motionKind[674].blendKind != 320, "motion 674 is not a node-blend motion");
    CHECK(!ico_mv_motion_playable(674, &missing), "playable: 674 with 669 not loaded is");
    CHECK(missing == 669, "playable: 674 names partner %d, not 669", missing);
    CHECK(missing >= 0 && strcmp(motionKind[missing].name, "GIRL STAND(HETARI)") == 0,
          "playable: the partner is \"%s\"", missing >= 0 ? motionKind[missing].name : "");
    motionTable[669] = &dummy;
    CHECK(ico_mv_motion_playable(674, &missing), "playable: 674 with 669 loaded is not");
    CHECK(missing == -1, "playable: a playable motion names partner %d", missing);
    motionTable[669] = NULL;

    int offered = 0, left = 0, blended = 0;
    for (int k = 0; k < mv_modelCount; k++) {
        for (int id = mv_models[k].motFirst; id < mv_models[k].motLast; id++) {
            int ok = ico_mv_motion_playable(id, NULL);
            if (motionKind[id].blendKind != 320) {
                blended++;
                CHECK(!ok || partnersLoaded(id),
                      "playable: %d \"%s\" offered, a partner is not "
                      "loaded",
                      id, motionKind[id].name);
            } else {
                CHECK(ok == (motionKind[id].area == 0 || motionTable[id] != NULL),
                      "playable: plain %d disagrees with the loaded rule", id);
            }
            ok ? offered++ : left++;
        }
    }
    printf("playable: %d offered, %d left out (%d node-blend) with the dynamic pool (area 4) "
           "empty\n",
           offered, left, blended);
    CHECK(left > 0 && blended > 0, "playable: nothing was left out");
    for (int i = 0; i < MV_MOTION_KINDS; i++) {
        motionTable[i] = NULL;
    }
}

static int loadElf(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 77;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        printf("cannot read %s\n", path);
        return 1;
    }
    fclose(f);
    char err[256] = "";
    int r = ico_tables_load_elf(&ico_table_set_pal, buf, (size_t)n, err, sizeof err);
    free(buf);
    if (r != 0) {
        printf("the table loader refused %s: %s\n", path, err);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *outdir = argc > 1 ? argv[1] : ".";
    const char *elf = argc > 2 ? argv[2] : NULL;

    if (elf != NULL && elf[0] == '\0') {
        printf("model_viewer_effects_test: no base ELF named: skipped\n");
        return 77;
    }
    if (elf != NULL) {
        int r = loadElf(elf);
        if (r == 77) {
            printf("model_viewer_effects_test: no base ELF at %s: skipped\n", elf);
            return 77;
        }
        if (r != 0) {
            return 1;
        }
    } else {
        fillSynthetic();
    }

    snprintf(s_logPath, sizeof s_logPath, "%s/model_viewer_effects%s.log", outdir,
             elf != NULL ? "_elf" : "");
    if (freopen(s_logPath, "w", stderr) == NULL) {
        printf("cannot write %s\n", s_logPath);
        return 1;
    }

    testGuard();
    testSweep(elf != NULL);
    if (elf != NULL) {
        testPlayable();
    }

    if (failures != 0) {
        printf("model_viewer_effects_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("model_viewer_effects_test: OK\n");
    return 0;
}
