/* weapon_draw_test.c: the insect net of Gameplay > Combat's Stick row
 * (port/game/variants.h) drawn and held through the game's own weapon code.
 *
 * ico2/sugipon/src/weapon.c with lineManager.c, matrixDrive.c and the 2D
 * layer (GifPacket.c, DisplayList.c, DmaPacket.c, Matrix.c) compiled as the
 * window build has them (ICO_HOST, ICO_RD), the variants (ico_options) linked,
 * the rest of the game stubbed below; recording only (no device).  One run
 * per disc (the argument): the region picks the disc's own copies of
 * InitWeaponGeo, WeaponGeo and WeaponDL (weapon.c: the European body,
 * InitWeaponGeo_ntsc / _aug6 / _jun26, WeaponDL_ntscaug6 / _jun26).  The
 * weapon rows used are the discs' own (weapon_kinds_disc checks them on the
 * images): the stick's row 1 and the net's (7; 6 on the June 2001 demo).
 *
 *   net      Stick at Insect net: a layout stick (kind 1) is made as the
 *            disc's net kind, with no torch (CreateLayoutedGObj is not
 *            called: only kind 1 carries one); lying on the floor it is
 *            CheckSwapableWeapon's answer for the boy beside it (kind 0 alone
 *            is passed over); PickupWeapon gives it to him (his pickedWeapon,
 *            the achievements' weapon signal with the net's kind); held, one
 *            WeaponGeo then WeaponDL records exactly dispInsectNet's 17 lines
 *            (the handle and the 16 segments of the hoop) in list 11, Gouraud,
 *            every vertex the net's grey (128, 128, 128, 128), at the boy's
 *            hand; the model path (p2o_DispVU1) is not taken
 *   stick    Stick at Original: the same layout stick is kind 1, made with
 *            its torch, and WeaponDL draws its model (p2o_SetDefaultEnviroment,
 *            p2o_DispVU1) and no line
 *   nothing  reaches the decoder undecoded.
 *
 * Exit 0, 1 on a mismatch.
 *
 *   weapon_draw_test <slug> <work folder>
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
/* the game's side */
#include "typedef.h"
#include "DisplayList.h"
#include "GifHost.h"
#include "matrixDrive.h"
#include "box.h"
#include "motionOrientManager.h"
#include "sceneManager.h"
#include "weapon.h"
/* the port's */
#include "config.h"
#include "variants.h"
#include "../../core/region.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define W 512
#define H 512

/* ------------------------------------------------- what the files import */
int ScreenWidth = W, ScreenHeight = H;
float center_X = 2048.0f, center_Y = 2048.0f;
float vsHeight = 448.0f;
int screenOffsetX, screenOffsetY;
int fbKeep;
int currentScreenHeight = H;
void *ios_partition_common, *ios_partition_sugipon;
char *matrixptr;
int systemStatus[12];
int stage_no;
GObj *boyGObj;
float IdentityQuaternion[4] = {0.0f, 0.0f, 0.0f, 1.0f};
/* port/data/gen/table_defs.c's (the tables the game reads), filled below */
WeaponDef weaponKind[10];
MotionDef motionKind[4];
FumbleRow weaponFumbleGeo[7 * 3];
AccessaryRec accessary[26];

static unsigned char s_heap[8u << 20] __attribute__((aligned(16)));
static size_t s_heapAt;

static void *zalloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (s_heapAt + n > sizeof(s_heap)) {
        printf("test heap exhausted\n");
        abort();
    }
    void *p = s_heap + s_heapAt;
    s_heapAt += n ? n : 16;
    memset(p, 0, n);
    return p;
}

void *iosMallocDebug(void *part, int size, const char *file, int line)
{
    (void)part, (void)file, (void)line;
    return zalloc((size_t)size);
}

void iosFree(void *p)
{
    (void)p;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    abort();
}

/* ico_assert is port/platform's (assert_host.c, linked through ico_options'
   ico_config); FlushCache stays this file's (kernel_host.c is not linked) */
void FlushCache(int op)
{
    (void)op;
}

void mc_Reset(void) {}

void InitTableSin(void) {}

void InitQuaternionDrive(void) {}

float GetTableSin(short angle)
{
    return sinf((float)angle * (3.14159265f / 32768.0f));
}

float GetTableCos(short angle)
{
    return cosf((float)angle * (3.14159265f / 32768.0f));
}

short GetTableArcCos(float x)
{
    return (short)(acosf(x) * (32768.0f / 3.14159265f));
}

short GetTableArcTan2(float y, float x)
{
    return (short)(atan2f(y, x) * (32768.0f / 3.14159265f));
}

/* GsBase.c: lineManager.c _getLine's x clip, the 4:3 half-width */
float gsb_HostLineHalfWidth(void)
{
    return 256.0f;
}

/* -------------------------------------------- the objects and their maths
 * A node matrix is a translation (the quaternions are the identity in every
 * call here): UpdateRootMatrix places node 0 at the root's position,
 * GetRootMatrix reads it back. */
static void translation(float *m, const float *pos)
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    m[12] = pos[0];
    m[13] = pos[1];
    m[14] = pos[2];
}

static float *nodeMatrix(GObj *g, int n)
{
    return (float *)((char *)GOBJ_SUB(g)->nodeMtx + n * 64);
}

void UpdateRootMatrix(GObj *g)
{
    if (GOBJ_SUB(g)->nodeMtx != 0) {
        translation(nodeMatrix(g, 0), GOBJ_SUB(g)->root.pos);
    }
}

void GetRootMatrix(void *m, GObj *g)
{
    memcpy(m, nodeMatrix(g, 0), 64);
}

void GetRootPosition(float *pos, void *g)
{
    memcpy(pos, GOBJ_SUB((GObj *)g)->root.pos, 16);
}

void GetMatrixFromQuaternion(void *m, void *q)
{
    static const float zero[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    (void)q;
    translation(m, zero);
}

void GetMatrixFromQuaternionPos(void *m, void *q, void *pos)
{
    (void)q;
    translation(m, pos);
}

void CopyQuaternion(void *d, void *s)
{
    memcpy(d, s, 16);
}

void GetInverseQuaternion(void *d, void *s)
{
    const float *q = s;
    float r[4] = {-q[0], -q[1], -q[2], q[3]};
    memcpy(d, r, 16);
}

void DivQuaternion(void *d, void *a, void *b)
{
    (void)a, (void)b;
    memcpy(d, IdentityQuaternion, 16);
}

void GetSlerpQuaternion(void *d, void *a, void *b, float t)
{
    (void)b, (void)t;
    memcpy(d, a, 16);
}

void SetQuaternionByAxisRotateV(void *q, short a, void *axis)
{
    (void)a, (void)axis;
    memcpy(q, IdentityQuaternion, 16);
}

void SetQuaternionByAxisRotateVWithNoRegularize(void *q, short a, void *axis)
{
    (void)a, (void)axis;
    memcpy(q, IdentityQuaternion, 16);
}

void RegularizeQuaternion(void *q)
{
    (void)q;
}

void RotQuaternionX(void *q, int a)
{
    (void)q, (void)a;
}

void RotQuaternionY(void *q, int a)
{
    (void)q, (void)a;
}

void RotQuaternionZ(void *q, int a)
{
    (void)q, (void)a;
}

static GObj *newObject(int nodes)
{
    GObj *g = zalloc(sizeof(GObj));
    Sub15C *d = zalloc(sizeof(Sub15C));
    g->self = g;
    g->active = 1;
    g->dobj = d;
    d->nodeNum = nodes;
    d->nodeMtx = (ICO_WORD)zalloc((size_t)nodes * 64);
    d->nodeQuat = (ICO_WORD)zalloc((size_t)nodes * 16);
    for (int i = 0; i < nodes; i++) {
        memcpy((char *)d->nodeQuat + i * 16, IdentityQuaternion, 16);
    }
    d->skelNodeNum = nodes;
    d->skel = zalloc((size_t)nodes * sizeof(SkelNode));
    memcpy(d->root.quat, IdentityQuaternion, 16);
    memcpy(d->root.itemQuat, IdentityQuaternion, 16);
    d->root.pos[3] = 1.0f;
    return g;
}

/* the torch a stick makes (kind 1's CreateLayoutedGObj) */
static int s_torches;

GObj *CreateLayoutedGObj(int id, int model, int acc, int light, void *lay, int label, int key,
                         int useStart)
{
    (void)id, (void)model, (void)acc, (void)light, (void)lay, (void)label, (void)key,
        (void)useStart;
    s_torches++;
    return newObject(1);
}

void LinkParentOfDObj(GObj *o, ObjNode *lnk)
{
    GOBJ_SUB(o)->parent = *lnk;
}

void SetTorchLife(GObj *o, int life, int b)
{
    (void)o, (void)life, (void)b;
}

void LightTorchOn(GObj *o)
{
    (void)o;
}

void LightTorchOff(GObj *o)
{
    (void)o;
}

void SetTorchChainReactionFlag(GObj *o, int flag)
{
    (void)o, (void)flag;
}

Sub15C *CSVSYSTEM_InitDObj(int id, SObjSimpleSetting *lay)
{
    (void)id, (void)lay;
    return GOBJ_SUB(newObject(1));
}

/* the one weapon object of the scene (isysGObjSearchFromObjKindID_begin(14)) */
static GObj *s_weapon;

void *isysGObjSearchFromObjKindID_begin(int kind)
{
    return kind == 14 ? s_weapon : NULL;
}

void *isysGObjSearchFromObjKindID_next(void *g)
{
    (void)g;
    return NULL;
}

int GetSkeltonFocusNode(GObj *g, int focus)
{
    (void)g, (void)focus;
    return 0;
}

int ACTGame_FLAG_TETSUNAGI_VISUAL(void)
{
    return 0;
}

/* the achievements' weapon signal (ico_gamestate.h) */
static int s_signalEv = -1, s_signalArg = -1;

void ico_gs_signal(int ev, int arg)
{
    s_signalEv = ev;
    s_signalArg = arg;
}

/* the model path (DisplayP2O.c) */
static int s_dispVU1, s_defaultEnv;

void p2o_DispVU1(GObj *g)
{
    (void)g;
    s_dispVU1++;
}

void p2o_DispVU1DObj(void *d)
{
    (void)d;
    s_dispVU1++;
}

void p2o_SetDefaultEnviroment(void)
{
    s_defaultEnv++;
}

/* the hit, the collision, the effects and the sound: nothing here */
void CheckEnemyHit(void *enemy, void *a, void *b, void *c)
{
    (void)enemy, (void)a, (void)b, (void)c;
}

void ClipCollision(void *cc)
{
    (void)cc;
}

void GetReflectionElement(void *cc, float a, float b)
{
    (void)cc, (void)a, (void)b;
}

int GetWallAttribute(void *cc)
{
    (void)cc;
    return 0;
}

int GetFloorAttribute(void *cc)
{
    (void)cc;
    return 0;
}

int SetParticleEffect(int no, void *pos, void *quat)
{
    (void)no, (void)pos, (void)quat;
    return -1;
}

void *GetParticleEffectData(int h)
{
    (void)h;
    return NULL;
}

void ExecParticleEffect(int h)
{
    (void)h;
}

void ExecuteDirectSE(void *g, int se)
{
    (void)g, (void)se;
}

void ExecuteSEPackage(void *g, int no)
{
    (void)g, (void)no;
}

void StopSEPackage(void *g, int no)
{
    (void)g, (void)no;
}

float stage_PlayBgAnimation(int no, float frame, char *mtx, float *quat)
{
    (void)no, (void)mtx, (void)quat;
    return frame;
}

void stage_SetLoopFlag(int no, int flag)
{
    (void)no, (void)flag;
}

/* --------------------------------------------------------- the scene
 * View identity (+0x80); the screen matrix (+0xC0) maps view (x, y, z) to
 * X = 2048 + 400 x / z, Y = 2048 + 400 y / z, GS Z = 2^24 / z, as rd_raw_test's. */
static float s_scratch[0x800 / 4] __attribute__((aligned(16)));

static void qw4(float *d, float x, float y, float z, float w)
{
    d[0] = x;
    d[1] = y;
    d[2] = z;
    d[3] = w;
}

static void buildScene(void)
{
    static const float zero[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    matrixptr = (char *)s_scratch;
    translation((float *)(matrixptr + 0x80), zero);
    float *S = (float *)(matrixptr + 0xC0);
    qw4(S + 0, 400.0f, 0, 0, 0);
    qw4(S + 4, 0, 400.0f, 0, 0);
    qw4(S + 8, 2048, 2048, 0, 1);
    qw4(S + 12, 0, 0, 16777216.0f, 0);
    memcpy(matrixptr + 0x100, S, 64);
}

/* the weapon rows the code reads: the discs' own (weapon_kinds_disc) */
static void fillRows(int net)
{
    static const WeaponDef stick = {80.0f, 30.0f, 1.5f, 1500, 99, 0, {60, 44, 32, 80}, 43, 1};
    static const WeaponDef insectNet = {120.0f, 40.0f, 0.0f, 1500, 0, 0, {60, 44, 32, 80}, 0, 0};
    memset(weaponKind, 0, sizeof(weaponKind));
    weaponKind[0].power = 1.0f;
    weaponKind[0].blur = -1;
    weaponKind[1] = stick;
    weaponKind[net] = insectNet;
}

/* ---------------------------------------------------------- the frame */
typedef struct Line {
    int list;
    int gouraud;
    RdScreenVtx v[2];
} Line;

#define MAXLINES 256
static Line s_lines[MAXLINES];
static int s_nlines;
static const RdFrame *s_frame;

static void walkCb(void *user, int list, uint32_t index, const RdCmd *cmd,
                   const RdStateBlock *state)
{
    (void)user, (void)index;
    if (cmd->type != RDC_SCREEN || cmd->b[0] != RD_PRIM_LINES) {
        return;
    }
    const RdScreenVtx *v = (const RdScreenVtx *)(s_frame->payload + cmd->u[0]);
    for (uint32_t k = 0; k + 1 < cmd->u[1] && s_nlines < MAXLINES; k += 2) {
        s_lines[s_nlines].list = list;
        s_lines[s_nlines].gouraud = state->gouraud;
        s_lines[s_nlines].v[0] = v[k];
        s_lines[s_nlines].v[1] = v[k + 1];
        s_nlines++;
    }
}

static void collect(void)
{
    s_frame = rd__last_frame();
    RdStateBlock s = s_frame->startState;
    s_nlines = 0;
    rd__walk(s_frame, s_frame->keep, &s, walkCb, NULL);
}

/* the boy, his hand (node 0) at hand[], standing at his root */
static GObj *makeBoy(const float *hand)
{
    GObj *b = newObject(1);
    translation(nodeMatrix(b, 0), hand);
    memcpy(GOBJ_SUB(b)->root.pos, hand, 12);
    return b;
}

/* a layout weapon of kind `kind`, lying at pos */
static GObj *makeWeapon(int kind, const float *pos)
{
    SObjSimpleSetting lay;
    GObj *g = newObject(1);

    memset(&lay, 0, sizeof(lay));
    memcpy(lay.pos, pos, 12);
    lay.pos[3] = 1.0f;
    lay.scale[0] = lay.scale[1] = lay.scale[2] = lay.scale[3] = 1.0f;
    lay.obj = kind;
    memcpy(GOBJ_SUB(g)->root.pos, pos, 12);
    GOBJ_SUB(g)->root.pos[3] = 1.0f;
    UpdateRootMatrix(g);
    /* InitWeaponGeo_jun26 returns the work without storing it (the
       constructor's caller does) */
    GOBJ_SUB(g)->work = InitWeaponGeo(g, &lay);
    s_weapon = g;
    return g;
}

/* one frame: the weapon's geometry, then its display list, in the frame */
static void drawFrame(GObj *g)
{
    dl_Clear();
    WeaponGeo(g);
    WeaponDL(g);
    dl_Swap();
    collect();
}

static void checkNet(const char *slug, int net)
{
    static const float hand[4] = {0.0f, 0.0f, 1000.0f, 1.0f};
    static const float floorPos[4] = {20.0f, 0.0f, 1000.0f, 1.0f};

    ico_variant_set(ICO_VARIANT_WEAPON, ICO_WEAPON_NET);
    boyGObj = makeBoy(hand);
    s_torches = 0;
    GObj *g = makeWeapon(1, floorPos);
    CHECK(CheckWeaponKind(g) == net, "%s: the layout stick is kind %d (want the net's %d)", slug,
          CheckWeaponKind(g), net);
    CHECK(s_torches == 0, "%s: the net carries no torch (%d made)", slug, s_torches);

    /* on the floor: its geometry once (the tip the European code reads),
       then the boy beside it finds it and picks it up */
    WeaponGeo(g);
    GObj *found = CheckSwapableWeapon(boyGObj, 200.0f);
    CHECK(found == g, "%s: CheckSwapableWeapon finds the net beside the boy", slug);
    PickupWeapon(g, boyGObj, 0x16);
    CHECK(GOBJ_SUB(boyGObj)->ctrl.pickedWeapon == g, "%s: the boy holds the net", slug);
    CHECK(s_signalArg == net, "%s: the weapon signal carries kind %d (%d)", slug, net, s_signalArg);
    CHECK(CheckSwapableWeapon(boyGObj, 200.0f) == NULL,
          "%s: a held net is no longer offered for a swap", slug);

    /* held: one frame */
    s_dispVU1 = s_defaultEnv = 0;
    drawFrame(g);
    int list11 = 0, grey = 0, gouraud = 0, near = 0;
    for (int i = 0; i < s_nlines; i++) {
        const Line *l = &s_lines[i];
        if (l->list != 11) {
            continue;
        }
        list11++;
        gouraud += l->gouraud != 0;
        int ok = 1;
        for (int k = 0; k < 2; k++) {
            ok &= l->v[k].rgba[0] == 0x80 && l->v[k].rgba[1] == 0x80 && l->v[k].rgba[2] == 0x80 &&
                  l->v[k].rgba[3] == 0x80;
            /* within 100 pixels of the hand's picture (the net is 190 units
               long at about 1000 units: under 80 pixels) */
            near +=
                fabs(l->v[k].x / 16.0 - 2048.0) < 100.0 && fabs(l->v[k].y / 16.0 - 2048.0) < 100.0;
        }
        grey += ok;
    }
    printf("  %s: the held net: %d lines in list 11 (%d Gouraud, %d grey), %d of their ends near "
           "the hand; %d lines in all; model draws %d\n",
           slug, list11, gouraud, grey, near, s_nlines, s_dispVU1);
    CHECK(list11 == 17 && s_nlines == 17, "%s: dispInsectNet's 17 lines in list 11 (%d, %d in all)",
          slug, list11, s_nlines);
    CHECK(gouraud == list11 && grey == list11, "%s: Gouraud lines of the net's grey (%d, %d)", slug,
          gouraud, grey);
    CHECK(near == 2 * list11, "%s: the net is drawn at the boy's hand (%d of %d ends)", slug, near,
          2 * list11);
    CHECK(s_dispVU1 == 0 && s_defaultEnv == 0, "%s: no model drawn for the net (%d, %d)", slug,
          s_dispVU1, s_defaultEnv);
}

static void checkStick(const char *slug)
{
    static const float hand[4] = {0.0f, 0.0f, 1000.0f, 1.0f};
    static const float floorPos[4] = {20.0f, 0.0f, 1000.0f, 1.0f};

    ico_variant_set(ICO_VARIANT_WEAPON, ICO_WEAPON_ORIGINAL);
    boyGObj = makeBoy(hand);
    s_torches = 0;
    GObj *g = makeWeapon(1, floorPos);
    CHECK(CheckWeaponKind(g) == 1, "%s: Original: the layout stick is kind 1 (%d)", slug,
          CheckWeaponKind(g));
    CHECK(s_torches == 1, "%s: Original: the stick carries its torch (%d made)", slug, s_torches);
    WeaponGeo(g);
    CHECK(CheckSwapableWeapon(boyGObj, 200.0f) == g, "%s: Original: the stick is found", slug);
    PickupWeapon(g, boyGObj, 0x16);
    s_dispVU1 = s_defaultEnv = 0;
    drawFrame(g);
    int list11 = 0;
    for (int i = 0; i < s_nlines; i++) {
        list11 += s_lines[i].list == 11;
    }
    CHECK(s_dispVU1 == 1 && s_defaultEnv == 1 && list11 == 0,
          "%s: Original: the stick's model (%d, %d), no net lines (%d)", slug, s_dispVU1,
          s_defaultEnv, list11);
}

int main(int argc, char **argv)
{
    const char *slug = argc > 1 ? argv[1] : "pal";
    const char *dir = argc > 2 ? argv[2] : ".";
    char cfg[512];

    if (ico_region_select(slug) != 0) {
        printf("weapon_draw_test: no region %s\n", slug);
        return 1;
    }
    /* the variants with no config file of the user's (one per disc: ctest
       runs the discs in parallel) */
    snprintf(cfg, sizeof cfg, "%s/weapon_draw_%s.toml", dir, slug);
    remove(cfg);
    ico_config_reset(cfg, "/nonexistent/weapon_draw_test.ini");
    ico_variants_reload();
    const int net = ico_variant_net_kind();
    printf("weapon_draw_test %s: the net is kind %d\n", slug, net);

    /* the game's frame: 50 Hz, two fields a tick (the torch's life reads it) */
    systemStatus[0] = 0;
    systemStatus[1] = 2;
    fillRows(net);

    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    if (!rd__init_record_only(W, H)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    dl_Init();
    InitMatrixDrive();
    buildScene();
    checkNet(slug, net);
    checkStick(slug);
    CHECK(gif_HostUndecodedTotal() == 0, "%u GS writes undecoded", gif_HostUndecodedTotal());
    rd_shutdown();
    ico_variant_set(ICO_VARIANT_WEAPON, ICO_WEAPON_ORIGINAL);
    remove(cfg);
    if (failures) {
        printf("weapon_draw_test %s: %d failures\n", slug, failures);
        return 1;
    }
    printf("weapon_draw_test %s: ok\n", slug);
    return 0;
}
