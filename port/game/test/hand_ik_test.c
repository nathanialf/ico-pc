/*
 * port/game/test/hand_ik_test.c
 *
 * Issue 19 (v0.4.3 AN-19): on a phone Ico and Yorda walked hand in hand
 * with both arms raised straight up.  A NaN reaching GetTableArcCos gives
 * exactly 90 degrees on arm64 (the index conversion saturates to 0) where
 * x86-64 reads 4 GB below the table.  This test runs the hand records the
 * way ACTGame_ConnectHand sets them (Ico's hand 1 in mode 5 reaching for
 * Yorda, Yorda's hand 0 in mode 6 following Ico's hand), through the real
 * ico2/sugipon/src/handManager.c (compiled in below so its statics are in
 * reach), tableSin.c, quaternion.c and matrixDrive.c, the rest of the game
 * stubbed:
 *   - a normal grip, hands out of reach, hands at one point, a zero bone
 *     scale: every hand target is finite and no NaN reached the tables;
 *   - Ico at an infinite position (a value the PS2 cannot hold): no crash,
 *     Yorda's target has no NaN, and a NaN in Ico's target is logged
 *     (counted by ico_diag_float_faults);
 *   - GetTableArcCos and GetTableArcSin of a NaN give the angle 0 and count a
 *     fault; of an infinity the clamp's angle (that of 1 or -1) and none;
 *   - a sweep of ordinary inputs gives what the unguarded formula gives.
 */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diag_host.h"
#include "../../../ico2/sugipon/src/handManager.c"

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

/* --- the game's globals and calls handManager.c reaches ------------------ */
int debug_now_motion_viewer;
const MotionDef motionKind[1];
char motionIKEffKind[256];

void InitMatrixDrive(void);

/* the focus nodes the hand code asks for: 3 Ico's right shoulder, 19 the
   left shoulder, 20 and 22 the forearm and the hand, 6 the hand Yorda
   follows */
enum { N_SHOULDER1 = 2, N_HAND6 = 3, N_SHOULDER0 = 4, N_ELBOW = 5, N_WRIST = 6, N_NODES = 16 };

int GetSkeltonFocusNode(GObj *self, int focus)
{
    (void)self;
    switch (focus) {
    case 3:
        return N_SHOULDER1;
    case 6:
        return N_HAND6;
    case 19:
        return N_SHOULDER0;
    case 20:
        return N_ELBOW;
    case 22:
        return N_WRIST;
    }
    return 0;
}

static int seCalls;

void ExecuteSEPackage(struct GObj *gobj, int id)
{
    (void)gobj;
    (void)id;
    seCalls++;
}

void GetGlobalWallPlane(float *plane, WallCfg *wall)
{
    (void)wall;
    memset(plane, 0, 16);
}

float GetProjectionOfPlane(void *out, void *plane, void *pos)
{
    (void)plane;
    memcpy(out, pos, 16);
    return 0.0f;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

/* --- two characters shaped as the hand code reads them ------------------- */
typedef struct {
    GObj g;
    Sub15C sub;
    struct DObjNode node;
    SkelNode skel[N_NODES];
    float mtx[N_NODES][16];
} Character;

static Character ico, yorda;

/* bones: upper arm, forearm (stored negative: getBone takes |x|), hand */
static void setupCharacter(Character *c, float scale)
{
    int i;

    memset(c, 0, sizeof *c);
    c->g.dobj = &c->sub;
    c->sub.nodes = &c->node;
    c->sub.skel = c->skel;
    c->sub.nodeMtx = (ICO_WORD)c->mtx;
    c->node.scale[0] = c->node.scale[1] = c->node.scale[2] = scale;
    for (i = 0; i < N_NODES; i++) {
        c->skel[i].child = -1;
        c->skel[i].parent = -1;
        c->skel[i].sibling = -1;
        c->mtx[i][0] = c->mtx[i][5] = c->mtx[i][10] = c->mtx[i][15] = 1.0f;
    }
    c->skel[N_SHOULDER0].child = 8;
    c->skel[N_ELBOW].child = 9;
    c->skel[N_WRIST].child = 10;
    c->skel[8].pos[0] = 12.0f;
    c->skel[9].pos[0] = -10.0f;
    c->skel[10].pos[0] = 4.0f;
    c->sub.root.handIK = 1;
    c->sub.root.handRate = 0.3f;
}

static void placeNode(Character *c, int n, float x, float y, float z)
{
    c->mtx[n][12] = x;
    c->mtx[n][13] = y;
    c->mtx[n][14] = z;
}

static void placeAll(Character *c, float x, float y, float z)
{
    int i;

    for (i = 0; i < N_NODES; i++) {
        placeNode(c, i, x, y, z);
    }
}

/* ACTGame_ConnectHand: Yorda's hand 0 in mode 6 on Ico, Ico's hand 1 in mode
   5 on Yorda (updateHMC) */
static void connectHands(void)
{
    yorda.sub.root.hand0.mode = 6;
    yorda.sub.root.hand0.obj = &ico.g;
    yorda.sub.root.hand0.node = 0;
    ico.sub.root.hand1.mode = 5;
    ico.sub.root.hand1.obj = &yorda.g;
    ico.sub.root.hand1.node = 0;
}

static int isFinite3(const float *v)
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static int hasNaN3(const float *v)
{
    return isnan(v[0]) || isnan(v[1]) || isnan(v[2]);
}

static float dist3(const float *a, const float *b)
{
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};

    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

static const float *nodePos(Character *c, int n)
{
    return &c->mtx[n][12];
}

/* one frame of both hand managers; returns the faults counted in it */
static unsigned int runFrame(void)
{
    unsigned int before = ico_diag_float_faults();

    HandManager(&ico.g);
    HandManager(&yorda.g);
    return ico_diag_float_faults() - before;
}

/* --- the cases ------------------------------------------------------------ */
static void testGrip(const char *name, float scaleIco, float scaleYorda, const float *icoAt,
                     const float *yordaAt)
{
    const float *ikIco;
    const float *ikYorda;
    unsigned int faults;

    setupCharacter(&ico, scaleIco);
    setupCharacter(&yorda, scaleYorda);
    placeAll(&ico, icoAt[0], icoAt[1], icoAt[2]);
    placeAll(&yorda, yordaAt[0], yordaAt[1], yordaAt[2]);
    connectHands();
    faults = runFrame();
    ikIco = ico.sub.root.hand1.ikDir;
    ikYorda = yorda.sub.root.hand0.ikDir;
    CHECK(isFinite3(ikIco), "%s: Ico's hand target %g %g %g", name, (double)ikIco[0],
          (double)ikIco[1], (double)ikIco[2]);
    CHECK(isFinite3(ikYorda), "%s: Yorda's hand target %g %g %g", name, (double)ikYorda[0],
          (double)ikYorda[1], (double)ikYorda[2]);
    CHECK(faults == 0, "%s: %u NaN faults", name, faults);
    CHECK(ico.sub.root.hand1.ikMode == 1 && yorda.sub.root.hand0.ikMode == 1, "%s: ikMode %d %d",
          name, ico.sub.root.hand1.ikMode, yorda.sub.root.hand0.ikMode);
}

static void testCases(void)
{
    float a[3] = {0.0f, 100.0f, 0.0f};
    float near[3] = {30.0f, 96.0f, 5.0f};
    float far[3] = {80.0f, 100.0f, 0.0f};
    float sa = 12.0f + 10.0f + 4.0f; /* one character's reach at scale 1 */

    /* a normal grip: the shoulders 30.5 apart, reach 26 each; Ico's target
       is one reach from his shoulder */
    testGrip("normal grip", 1.0f, 1.0f, a, near);
    {
        float d = dist3(ico.sub.root.hand1.ikDir, nodePos(&ico, N_SHOULDER1));

        CHECK(fabsf(d - sa) < 0.05f * sa, "normal grip: Ico's reach %g, expected %g", (double)d,
              (double)sa);
    }
    /* out of reach (sa + sb < len): the target is one reach from Yorda */
    testGrip("out of reach", 1.0f, 1.0f, a, far);
    {
        float d = dist3(ico.sub.root.hand1.ikDir, nodePos(&yorda, N_SHOULDER0));

        CHECK(fabsf(d - sa) < 0.05f * sa, "out of reach: %g from Yorda, expected %g", (double)d,
              (double)sa);
    }
    /* hands at one point (len 0): before AN-19 (ll + ss - tt) / (l2 * sa)
       was 0 / 0, a NaN into GetTableArcCos (the fault count above) */
    testGrip("same point", 1.0f, 1.0f, a, a);
    /* a zero bone scale, apart and at one point (0 / 0 again) */
    testGrip("zero scale", 0.0f, 0.0f, a, near);
    testGrip("zero scale, same point", 0.0f, 0.0f, a, a);
    testGrip("Ico zero scale", 0.0f, 1.0f, a, near);

    /* Ico at an infinite position: a value the PS2 cannot hold, so no
       target is promised; nothing crashes, Yorda's target (Ico's hand
       matrix moved by a finite step) has no NaN, and a NaN in Ico's is
       logged and counted */
    {
        float inf[3] = {INFINITY, 100.0f, 0.0f};
        unsigned int before;
        unsigned int after;
        int nan1;
        int nan2;

        setupCharacter(&ico, 1.0f);
        setupCharacter(&yorda, 1.0f);
        placeAll(&ico, inf[0], inf[1], inf[2]);
        placeAll(&yorda, near[0], near[1], near[2]);
        connectHands();
        before = ico_diag_float_faults();
        runFrame();
        nan1 = hasNaN3(ico.sub.root.hand1.ikDir);
        after = ico_diag_float_faults();
        CHECK(!hasNaN3(yorda.sub.root.hand0.ikDir), "infinite: Yorda's target has a NaN");
        CHECK(!nan1 || after > before, "infinite: Ico's NaN target was not counted");
        /* a second frame counts again (the log line is written once) */
        runFrame();
        nan2 = hasNaN3(ico.sub.root.hand1.ikDir);
        CHECK(nan1 == nan2, "infinite: the second frame differs");
        CHECK(!nan2 || ico_diag_float_faults() > after, "infinite: second NaN not counted");
        printf("hand_ik: Ico at +Inf: target %g %g %g (%s)\n", (double)ico.sub.root.hand1.ikDir[0],
               (double)ico.sub.root.hand1.ikDir[1], (double)ico.sub.root.hand1.ikDir[2],
               nan1 ? "NaN, logged" : "no NaN");
    }
    CHECK(seCalls == 0, "Yorda's hand played %d sounds (ikReached is 0)", seCalls);
}

/* --- the tables --------------------------------------------------------- */

/* tableSin.c's arc-sine table and its two lookups as they were before the
   NaN guard, built here the same way */
static unsigned short refTable[4097];

static void refInit(void)
{
    int i;
    float k = 0.000244140625f;
    float s = 10430.378f;

    for (i = 0; i < 4097; i++) {
        refTable[i] = (int)(asinf((float)i * k) * s);
    }
}

static void refClamp(float *x, int *neg)
{
    if (1.0f < *x) {
        *x = 1.0f;
    }
    if (*x < -1.0f) {
        *x = -1.0f;
    }
    if (*x < 0.0f) {
        *neg = 1;
        *x = -*x;
    } else {
        *neg = 0;
    }
}

static short refArcSin(float x)
{
    int neg;
    int hi;

    refClamp(&x, &neg);
    hi = ((short *)refTable)[(int)(x * 4096.0f)];
    return (short)(neg ? -hi : hi);
}

static short refArcCos(float x)
{
    int neg;
    int hi;

    refClamp(&x, &neg);
    hi = (short)(refTable[(int)(x * 4096.0f)] + 16384);
    if (neg == 0) {
        return (short)(32768 - hi);
    }
    return hi;
}

static void testTables(void)
{
    static const float extra[] = {0.0f,    -0.0f,   1.0f,    -1.0f,    0.5f,        -0.5f,
                                  1.0001f, -1.01f,  1e30f,   -1e30f,   1e-30f,      -1e-30f,
                                  1e-40f,  -1e-40f, FLT_MAX, -FLT_MAX, 0.99999994f, -0.99999994f};
    unsigned int before;
    int i;
    int bad = 0;

    refInit();
    before = ico_diag_float_faults();
    for (i = -5000; i <= 5000; i++) {
        float x = (float)i * (1.0f / 4096.0f) * 1.2f;
        float y = (float)i * 0.0001234f;

        if (GetTableArcCos(x) != refArcCos(x) || GetTableArcSin(x) != refArcSin(x) ||
            GetTableArcCos(y) != refArcCos(y) || GetTableArcSin(y) != refArcSin(y)) {
            bad++;
        }
    }
    for (i = 0; i < (int)(sizeof extra / sizeof extra[0]); i++) {
        if (GetTableArcCos(extra[i]) != refArcCos(extra[i]) ||
            GetTableArcSin(extra[i]) != refArcSin(extra[i])) {
            CHECK(0, "table: %g gives %d %d, expected %d %d", (double)extra[i],
                  GetTableArcCos(extra[i]), GetTableArcSin(extra[i]), refArcCos(extra[i]),
                  refArcSin(extra[i]));
        }
    }
    CHECK(bad == 0, "table: %d ordinary inputs changed", bad);
    CHECK(ico_diag_float_faults() == before, "table: ordinary inputs counted %u faults",
          ico_diag_float_faults() - before);

    /* the infinities take the clamp: the angles of 1 and -1, no fault */
    CHECK(GetTableArcCos(INFINITY) == refArcCos(1.0f), "ArcCos(+Inf) %d, expected %d",
          GetTableArcCos(INFINITY), refArcCos(1.0f));
    CHECK(GetTableArcCos(-INFINITY) == refArcCos(-1.0f), "ArcCos(-Inf) %d, expected %d",
          GetTableArcCos(-INFINITY), refArcCos(-1.0f));
    CHECK(GetTableArcSin(INFINITY) == refArcSin(1.0f), "ArcSin(+Inf) %d", GetTableArcSin(INFINITY));
    CHECK(GetTableArcSin(-INFINITY) == refArcSin(-1.0f), "ArcSin(-Inf) %d",
          GetTableArcSin(-INFINITY));
    CHECK(ico_diag_float_faults() == before, "the infinities counted a fault");
    printf("hand_ik: ArcCos(+Inf) %d, ArcCos(-Inf) %d\n", GetTableArcCos(INFINITY),
           GetTableArcCos(-INFINITY));

    /* a NaN (either sign) is the angle 0, and counted */
    {
        float nan = NAN;
        float nnan = -NAN;
        unsigned int n0 = ico_diag_float_faults();

        CHECK(GetTableArcCos(nan) == refArcCos(1.0f), "ArcCos(NaN) %d, expected %d",
              GetTableArcCos(nan), refArcCos(1.0f));
        CHECK(GetTableArcCos(nan) >= -1 && GetTableArcCos(nan) <= 1, "ArcCos(NaN) %d not 0",
              GetTableArcCos(nan));
        CHECK(GetTableArcSin(nan) == 0, "ArcSin(NaN) %d", GetTableArcSin(nan));
        CHECK(GetTableArcCos(nnan) == refArcCos(1.0f), "ArcCos(-NaN) %d", GetTableArcCos(nnan));
        CHECK(GetTableArcSin(nnan) == 0, "ArcSin(-NaN) %d", GetTableArcSin(nnan));
        CHECK(GetTableArcTan2(1.0f, nan) == GetTableArcCos(1.0f), "ArcTan2(1, NaN) %d",
              GetTableArcTan2(1.0f, nan));
        CHECK(ico_diag_float_faults() >= n0 + 7, "NaN faults counted %u, expected 7 or more",
              ico_diag_float_faults() - n0);
    }
}

int main(void)
{
    InitMatrixDrive();
    testTables();
    testCases();
    if (failures != 0) {
        fprintf(stderr, "hand_ik: %d failure(s)\n", failures);
        return 1;
    }
    printf("hand_ik: ok\n");
    return 0;
}
