/*
 * tools/arm64_diff/ikdiff.c
 *
 * The arm IK's differential harness (tools/arm64_diff.sh): the real game
 * code of Ico and Yorda holding hands, compiled once per target with that
 * target's own flags (the x86-64 builds' and the Android arm64 build's),
 * run over the same deterministic inputs, every result printed as bits in
 * a fixed order.  The script diffs the outputs: any line that differs is
 * game code the two targets compile to different results (issue 19 came
 * from one).  Not a test of what the right answer is: of whether every
 * build gives the same one.
 *
 * Two programs are built from this file:
 *   - ikdiff_path: the game's own translation units (handManager.c,
 *     motionManager.c, matrixDrive.c, quaternion.c, tableSin.c and
 *     port/math) linked as the game links them, so their code is the
 *     game's; it runs whole frames of the hand-holding path
 *     (HandManager, then GetMatrixOfMotion's _getFinalMatrix over a
 *     skeleton with the arm, torso, head and leg kinds) and the exported
 *     helpers;
 *   - ikdiff_units (IKDIFF_UNITS): this file includes motionManager.c to
 *     reach its static helpers (limitHPAngleAndSetB, limitInterpAngle,
 *     classifyInterpAngle, getDirOfAngle, getRotOfDir) and sweeps them.
 *
 * Inputs are pseudo-random from a fixed seed; arithmetic here is plain
 * IEEE float (no libm), so both targets feed the game the same bits.
 * NaNs print as one value (their sign and payload are the host's).
 *
 * Arguments: [elf] [verbose-scenario].  elf, the PS2 game's main ELF
 * (the one the port reads its tables from): the joint limit table is taken
 * from it, as the game loads it; without it synthetic limits are used.
 * verbose-scenario: print every node matrix of that path scenario.
 */
#ifdef IKDIFF_UNITS
#include "../../ico2/sugipon/src/motionManager.c"
#else
#include "typedef.h"
#include "main.h"
#include "debug.h"
#include "motionOrientManager.h"
#include "motionManager.h"
#include "matrixDrive.h"
#include "quaternion.h"
#include "tableSin.h"
#include "Matrix.h"
#include <libvu0.h>
#include <string.h>
#include <stdio.h>
#endif
#include "handManager.h"
#include "../../port/platform/fpenv.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

/* --- output -------------------------------------------------------------- */
static FILE *out;

static unsigned int fbits(float f)
{
    union {
        float f;
        unsigned int u;
    } c;

    if (f != f) {
        return 0x7FC00000u;
    }
    c.f = f;
    return c.u;
}

static void pf(float f)
{
    fprintf(out, " %08x", fbits(f));
}

static void pfv(const float *v, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        pf(v[i]);
    }
}

static void pi(int i)
{
    fprintf(out, " %d", i);
}

static uint32_t hashv;

static void hf(float f)
{
    unsigned int u = fbits(f);
    int i;

    for (i = 0; i < 4; i++) {
        hashv ^= (u >> (8 * i)) & 0xFF;
        hashv *= 16777619u;
    }
}

static void hfv(const float *v, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        hf(v[i]);
    }
}

static void hi(int v)
{
    int i;

    for (i = 0; i < 4; i++) {
        hashv ^= ((unsigned int)v >> (8 * i)) & 0xFF;
        hashv *= 16777619u;
    }
}

/* --- inputs -------------------------------------------------------------- */
static uint32_t rs = 0x1C0B0A7Du;

static uint32_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * ((float)(rnd() >> 8) * (1.0f / 16777216.0f));
}

static int irand(int lo, int hi)
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

/* a float of any exponent: sign, exponent 0..254, mantissa */
static float anyfloat(void)
{
    union {
        float f;
        unsigned int u;
    } c;

    c.u = rnd();
    if (((c.u >> 23) & 0xFF) == 0xFF) {
        c.u &= ~0x00800000u;
    }
    return c.f;
}

static float fsqrt_(float x)
{
    /* Newton steps from a bit guess: identical on every target, no libm */
    union {
        float f;
        unsigned int u;
    } c;

    float g;
    int i;

    if (x <= 0.0f) {
        return 0.0f;
    }
    c.f = x;
    c.u = 0x1FBD1DF5u + (c.u >> 1);
    g = c.f;
    for (i = 0; i < 4; i++) {
        g = 0.5f * (g + x / g);
    }
    return g;
}

static void randUnitQuat(float *q, float spread)
{
    float n;

    q[0] = frand(-spread, spread);
    q[1] = frand(-spread, spread);
    q[2] = frand(-spread, spread);
    q[3] = 1.0f;
    n = fsqrt_(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    q[0] /= n;
    q[1] /= n;
    q[2] /= n;
    q[3] /= n;
}

static void randAnyQuat(float *q)
{
    float n;

    do {
        q[0] = frand(-1.0f, 1.0f);
        q[1] = frand(-1.0f, 1.0f);
        q[2] = frand(-1.0f, 1.0f);
        q[3] = frand(-1.0f, 1.0f);
        n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    } while (n < 0.01f);
    n = fsqrt_(n);
    q[0] /= n;
    q[1] /= n;
    q[2] /= n;
    q[3] /= n;
}

/* a direction: random, along an axis, tiny, huge or degenerate */
static void randDir(float *v)
{
    int k = irand(0, 15);

    v[3] = 0.0f;
    switch (k) {
    case 0:
        v[0] = v[1] = v[2] = 0.0f;
        break;
    case 1:
        v[0] = frand(-1.0f, 1.0f) < 0.0f ? -50.0f : 50.0f;
        v[1] = v[2] = 0.0f;
        break;
    case 2:
        v[1] = frand(-1.0f, 1.0f) < 0.0f ? -50.0f : 50.0f;
        v[0] = v[2] = 0.0f;
        break;
    case 3:
        v[2] = frand(-1.0f, 1.0f) < 0.0f ? -50.0f : 50.0f;
        v[0] = v[1] = 0.0f;
        break;
    case 4:
        v[0] = frand(-0.01f, 0.01f);
        v[1] = frand(-0.01f, 0.01f);
        v[2] = frand(-100.0f, 100.0f);
        break;
    case 5:
        v[0] = frand(-1e-6f, 1e-6f);
        v[1] = frand(-1e-6f, 1e-6f);
        v[2] = frand(-1e-6f, 1e-6f);
        break;
    case 6:
        v[0] = frand(-1e18f, 1e18f);
        v[1] = frand(-1e18f, 1e18f);
        v[2] = frand(-1e18f, 1e18f);
        break;
    default:
        v[0] = frand(-3000.0f, 3000.0f);
        v[1] = frand(-3000.0f, 3000.0f);
        v[2] = frand(-3000.0f, 3000.0f);
        break;
    }
}

/* --- the game's calls this harness answers ------------------------------- */
static unsigned int faults;

int ico_diag_float_fault(const char *site, const void *caller)
{
    (void)caller;
    faults++;
    fprintf(out, "  fault %s\n", site);
    return 1;
}

void ico_diag_log(const char *fmt, ...)
{
    (void)fmt;
}

unsigned int ico_diag_float_faults(void)
{
    return faults;
}

int GetSkeltonFocusNode(GObj *self, int focus)
{
    return GOBJ_SUB(self)->focusNodes[focus];
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

float GetProjectionOfPlane(void *o, void *plane, void *pos)
{
    (void)plane;
    memcpy(o, pos, 16);
    return 0.0f;
}

float GetWeaponWeight(GObj *w)
{
    (void)w;
    return 0.0f;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

/* stubs.c (generated by tools/arm64_diff.sh) sends every other call here */
void ikdiff_stub(const char *name)
{
    fflush(out);
    fprintf(stderr, "ikdiff: the harness has no %s\n", name);
    abort();
}

/* --- the joint limit table ------------------------------------------------- */
/* ikdiff_data.c: the tables, writable (the game's headers declare them
   const) */
MotOriLimit *ikdiff_limits(void);
MotionDef *ikdiff_motion_kind(void);
GenGeo *ikdiff_obj_layout(void);
#define LIMITS (ikdiff_limits())

static int limitsFromElf;

/* the game's motion-limit-def (48 rows of 0x30 bytes at 0x626628 in the PAL
   ELF), read from the ELF's program headers as port/data/tables.c does */
static int readLimits(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char h[52];
    unsigned int phoff;
    int phnum;
    int phent;
    int i;
    const unsigned int va = 0x00626628u;

    if (f == 0) {
        return 0;
    }
    if (fread(h, 1, 52, f) != 52 || h[0] != 0x7F || h[1] != 'E' || h[4] != 1) {
        fclose(f);
        return 0;
    }
    phoff = h[28] | h[29] << 8 | h[30] << 16 | (unsigned int)h[31] << 24;
    phent = h[42] | h[43] << 8;
    phnum = h[44] | h[45] << 8;
    for (i = 0; i < phnum; i++) {
        unsigned char p[32];
        unsigned int type, off, vaddr, filesz;

        fseek(f, (long)(phoff + (unsigned int)(i * phent)), SEEK_SET);
        if (fread(p, 1, 32, f) != 32) {
            break;
        }
        type = p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24;
        off = p[4] | p[5] << 8 | p[6] << 16 | (unsigned int)p[7] << 24;
        vaddr = p[8] | p[9] << 8 | p[10] << 16 | (unsigned int)p[11] << 24;
        filesz = p[16] | p[17] << 8 | p[18] << 16 | (unsigned int)p[19] << 24;
        if (type == 1 && vaddr <= va && va + 48 * 48 <= vaddr + filesz) {
            fseek(f, (long)(off + (va - vaddr)), SEEK_SET);
            if (fread(LIMITS, 48, 48, f) == 48) {
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

/* without the ELF: rows shaped like the game's (heading -180..180 with the
   180 edge, pitch and bank across their range), four triples a character */
static void syntheticLimits(void)
{
    int i;

    for (i = 0; i < 48; i++) {
        MotOriLimit *r = &LIMITS[i];
        float hmax = (i % 6 == 0) ? 180.0f : frand(10.0f, 180.0f);
        float hmin = (i % 9 == 0) ? -180.0f : -frand(10.0f, 180.0f);

        r->lo.x = hmin;
        r->hi.x = hmax;
        r->mid.x = 0.0f;
        r->lo.y = frand(-120.0f, 120.0f);
        r->mid.y = frand(-120.0f, 120.0f);
        r->hi.y = frand(-120.0f, 120.0f);
        r->lo.z = frand(-170.0f, 170.0f);
        r->mid.z = frand(-170.0f, 170.0f);
        r->hi.z = frand(-170.0f, 170.0f);
        r->node = (int[]){35, 1, 3, 19}[(i / 3) % 4];
        r->float28 = (i / 3) % 4 == 1 ? 45.0f : 0.0f;
        r->word2C = 0;
    }
}

/* SetNodeRotationLimitDataTable's row reordering (motionOrientManager.c),
   once for the whole table as the boy's (0..12) and the girl's (12..24)
   calls and the rest leave it */
static void orderLimits(void)
{
    int i;

    for (i = 0; i + 2 < 48; i += 3) {
        MotOriLimit tmp;

        if (LIMITS[i].mid.y < LIMITS[i + 2].mid.y) {
            tmp = LIMITS[i];
            LIMITS[i] = LIMITS[i + 2];
            LIMITS[i + 2] = tmp;
        }
        if (LIMITS[i + 1].hi.x < LIMITS[i + 1].lo.x) {
            int j;

            for (j = 0; j < 3; j++) {
                tmp.lo = LIMITS[i + j].hi;
                tmp.mid = LIMITS[i + j].mid;
                tmp.hi = LIMITS[i + j].lo;
                tmp.node = LIMITS[i + j].node;
                tmp.float28 = LIMITS[i + j].float28;
                tmp.word2C = LIMITS[i + j].word2C;
                LIMITS[i + j] = tmp;
            }
        }
    }
}

/* --- the exported helpers ---------------------------------------------- */
static void sweepTables(void)
{
    int i;
    static const float special[] = {0.0f,   -0.0f,   1.0f,     -1.0f,    0.5f,     -0.5f,
                                    1e-45f, -1e-45f, 3.4e38f,  -3.4e38f, 1.00001f, -1.00001f,
                                    2.0f,   -2.0f,   0.99999f, -0.99999f};

    fprintf(out, "# GetTableSin/GetTableCos, every short\n");
    for (i = -32768; i < 32768; i++) {
        float s = GetTableSin((short)i);
        float c = GetTableCos((short)i);

        if ((i & 255) == 0) {
            fprintf(out, "sincos %d", i);
            pf(s);
            pf(c);
            fprintf(out, "\n");
        }
        hashv = 2166136261u;
        hf(s);
        hf(c);
        if ((i & 255) == 255) {
            fprintf(out, "sincos-hash %d %08x\n", i, hashv);
        }
    }
    /* through the int declarations quaternion.c uses (float (int)) */
    fprintf(out, "# GetTableSin/Cos through float (int), ints past a short\n");
    for (i = -140000; i <= 140000; i += 37) {
        float s = ((float (*)(int))GetTableSin)(i);
        float c = ((float (*)(int))GetTableCos)(i);

        fprintf(out, "sincos-int %d", i);
        pf(s);
        pf(c);
        fprintf(out, "\n");
    }
    fprintf(out, "# GetTableArcCos/ArcSin/ArcTan2\n");
    for (i = -12000; i <= 12000; i++) {
        float x = (float)i * (1.0f / 8192.0f);
        float y = frand(-2.0f, 2.0f);

        fprintf(out, "arc %08x %08x %d %d %d %d\n", fbits(x), fbits(y), GetTableArcCos(x),
                GetTableArcSin(x), GetTableArcTan2(y, x), GetTableArcTan2(-y, x));
    }
    for (i = 0; i < 20000; i++) {
        float x = i < 16 ? special[i] : anyfloat();

        fprintf(out, "arc-any %08x %d %d %d\n", fbits(x), GetTableArcCos(x), GetTableArcSin(x),
                GetTableArcTan2(x, -x));
    }
}

static void sweepTurns(void)
{
    int i;

    fprintf(out, "# MatrixDrive_GetTurn*\n");
    for (i = 0; i < 30000; i++) {
        float v[4];
        short a = 0x1234, b = 0x1234;

        randDir(v);
        fprintf(out, "turn %d", i);
        pfv(v, 3);
        MatrixDrive_GetTurnXAngleZY(&a, &b, v[0], v[1], v[2]);
        pi(a);
        pi(b);
        a = b = 0x1234;
        MatrixDrive_GetTurnXAngleYZ(&a, &b, v[0], v[1], v[2]);
        pi(a);
        pi(b);
        a = b = 0x1234;
        MatrixDrive_GetTurnYAngleXZ(&a, &b, v[0], v[1], v[2]);
        pi(a);
        pi(b);
        a = b = 0x1234;
        MatrixDrive_GetTurnZAngleXY(&a, &b, v[0], v[1], v[2]);
        pi(a);
        pi(b);
        a = b = 0x1234;
        MatrixDrive_GetTurnZAngleYX(&a, &b, v[0], v[1], v[2]);
        pi(a);
        pi(b);
        a = b = 0x1234;
        MatrixDrive_GetTurnMinusZAngleXY(&a, &b, v[0], v[1], v[2]);
        pi(a);
        pi(b);
        fprintf(out, "\n");
    }
}

static void sweepQuaternions(void)
{
    int i;
    int a;
    static const float rates[] = {0.0f, 0.1f, 0.25f, 0.3f,  0.5f,  0.6f,  0.9f, 0.99f,
                                  1.0f, 1.2f, 2.0f,  -0.5f, 0.05f, 0.75f, 4.0f, 30.0f};

    fprintf(out, "# SetQuaternionByAxisRotateV and the RotQuaternion family, every angle\n");
    for (a = -32768; a < 32768; a++) {
        float axis[4];
        float q[4];
        float r[4];
        float s[4];
        float t[4];

        axis[0] = frand(-1.0f, 1.0f);
        axis[1] = frand(-1.0f, 1.0f);
        axis[2] = frand(-1.0f, 1.0f);
        axis[3] = 0.0f;
        randAnyQuat(r);
        CopyQuaternion(s, r);
        CopyQuaternion(t, r);
        SetQuaternionByAxisRotateV(q, (short)a, axis);
        RotQuaternionX(r, (short)a);
        RotQuaternionY(s, (short)a);
        RotQuaternionZ(t, (short)a);
        hashv = 2166136261u;
        hfv(q, 4);
        hfv(r, 4);
        hfv(s, 4);
        hfv(t, 4);
        SetQuaternionByAxisRotateWithNoRegularize(q, (short)a, axis[0], axis[1], axis[2]);
        hfv(q, 4);
        if ((a & 63) == 0) {
            fprintf(out, "quat-angle %d", a);
            pfv(q, 4);
            pfv(r, 4);
            fprintf(out, "\n");
        }
        if ((a & 63) == 63) {
            fprintf(out, "quat-angle-hash %d %08x\n", a, hashv);
        }
    }
    /* the angle as the int a caller's conversion may leave (a float past
       a short converted without the wrap): the callee's own extension */
    fprintf(out, "# the same through (int) declarations, ints past a short\n");
    for (a = -140000; a <= 140000; a += 41) {
        float axis[4] = {0.3f, -0.5f, 0.8f, 0.0f};
        float q[4];
        float r[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        ((void (*)(float *, int, float *))SetQuaternionByAxisRotateV)(q, a, axis);
        fprintf(out, "quat-int %d", a);
        pfv(q, 4);
        ((void (*)(void *, int))RotQuaternionX)(r, a);
        pfv(r, 4);
        ((void (*)(float *, int, float, float, float))SetQuaternionByAxisRotateWithNoRegularize)(
            q, a, 0.0f, 0.0f, -1.0f);
        pfv(q, 4);
        fprintf(out, "\n");
    }
    fprintf(out, "# GetSlerpQuaternion, MultiQuaternion, DivQuaternion, matrices\n");
    for (i = 0; i < 40000; i++) {
        float qa[4], qb[4], o[4], m[16];
        float t = i < 16 ? rates[i] : (i & 1 ? frand(0.0f, 1.0f) : rates[i & 15]);

        randAnyQuat(qa);
        if (i % 7 == 0) {
            CopyQuaternion(qb, qa);
        } else if (i % 7 == 1) {
            qb[0] = -qa[0];
            qb[1] = -qa[1];
            qb[2] = -qa[2];
            qb[3] = -qa[3];
        } else if (i % 7 == 2) {
            randUnitQuat(qb, 0.001f);
            MultiQuaternion(qb, qa, qb);
        } else {
            randAnyQuat(qb);
        }
        fprintf(out, "slerp %d", i);
        pfv(qa, 4);
        pfv(qb, 4);
        pf(t);
        GetSlerpQuaternion(o, qa, qb, t);
        pfv(o, 4);
        GetSlerpQuaternionNoRegularize(o, qa, qb, t);
        pfv(o, 4);
        MultiQuaternion(o, qa, qb);
        pfv(o, 4);
        DivQuaternion(o, qa, qb);
        pfv(o, 4);
        GetMatrixFromQuaternion(m, qa);
        pfv(m, 16);
        GetMatrixFromQuaternionRotElem(m, qb);
        pfv(m, 16);
        CopyQuaternion(o, qa);
        o[0] *= 3.0f;
        RegularizeQuaternion(o);
        pfv(o, 4);
        fprintf(out, "\n");
    }
}

static void sweepMatrixDrive(void)
{
    int i;

    fprintf(out, "# MatrixDrive_*\n");
    for (i = 0; i < 20000; i++) {
        float v[4];
        float q[4];
        float m[16];
        float (*c)[4];
        short a = (short)irand(-32768, 32767);
        short b = (short)irand(-32768, 32767);
        short d = (short)irand(-32768, 32767);

        randDir(v);
        randAnyQuat(q);
        MatrixDrive_PushMatrix();
        GetMatrixFromQuaternion(MatrixDrive_GetMatrix(), q);
        MatrixDrive_GetMatrix()[3][0] = frand(-3000.0f, 3000.0f);
        MatrixDrive_GetMatrix()[3][1] = frand(-3000.0f, 3000.0f);
        MatrixDrive_GetMatrix()[3][2] = frand(-3000.0f, 3000.0f);
        MatrixDrive_RotMatrixX(a);
        MatrixDrive_RotMatrixY(b);
        MatrixDrive_RotMatrixZ(d);
        MatrixDrive_TransMatrix(v[0], v[1], v[2]);
        MatrixDrive_ScaleMatrix(1.0f, frand(0.5f, 2.0f), 1.0f);
        MatrixDrive_TurnViewMatrix(v[0] + 1.0f, v[1], v[2]);
        c = MatrixDrive_GetMatrix();
        MatrixDrive_SetTransposeMatrix(m, c);
        fprintf(out, "md %d %d %d %d", i, a, b, d);
        pfv(v, 3);
        pfv(&c[0][0], 16);
        pfv(m, 16);
        fprintf(out, "\n");
        /* the Turn*ObjectMatrix wrappers keep their first angle in a local
           the turn leaves alone for a direction on its axis (the stack word
           the build left there): their own tag */
        MatrixDrive_PushMatrix();
        switch (i % 5) {
        case 0:
            MatrixDrive_TurnObjectMatrix(v[2], v[0], v[1]);
            break;
        case 1:
            MatrixDrive_TurnXObjectMatrixZY(v[1], v[2], v[0]);
            break;
        case 2:
            MatrixDrive_TurnXObjectMatrixYZ(v[0], v[1], v[2]);
            break;
        case 3:
            MatrixDrive_TurnYObjectMatrixXZ(v[0], v[1], v[2]);
            break;
        default:
            MatrixDrive_TurnZObjectMatrixXY(v[0], v[1], v[2]);
            break;
        }
        fprintf(out, "md-turnobj %d", i);
        pfv(&MatrixDrive_GetMatrix()[0][0], 12);
        fprintf(out, "\n");
        MatrixDrive_PopMatrix();
        MatrixDrive_PopMatrix();
    }
}

#ifdef IKDIFF_UNITS
/* --- motionManager.c's statics -------------------------------------------- */
static struct MotCtrl unitCtrl;

static void sweepLimits(void)
{
    int row;
    int i;
    static const float rates[] = {1.0f, 0.5f, 0.25f, 0.0f, 0.01f, 0.999f, 2.0f};

    skelMotCtrl = &unitCtrl;
    fprintf(out, "# limitHPAngleAndSetB, limitInterpAngle, classifyInterpAngle%s\n",
            limitsFromElf ? " (the ELF's limits)" : " (synthetic limits)");
    for (row = 0; row + 2 < 48; row += 3) {
        MotLimAng *lim = (MotLimAng *)&LIMITS[row];

        for (i = 0; i < 6000; i++) {
            short h = (short)(i < 64 ? (i - 32) * 1024 : irand(-32768, 32767));
            short p = (short)(i < 64 ? (i & 7) * 8192 - 32768 : irand(-32768, 32767));
            short prevH = (short)(i % 3 == 0 ? 0 : irand(-32768, 32767));
            short prevP = (short)(i % 5 == 0 ? 0 : irand(-32768, 32767));
            float rate = i % 4 == 0 ? rates[(i / 4) % 7] : 1.0f;
            int setb = i & 1;
            int setflag = (i >> 1) & 1;
            short ha = 0x1234, pa = 0x1234, ba = 0x1234;

            unitCtrl.flags = 0;
            limitHPAngleAndSetB(&ha, &pa, &ba, lim, h, prevH, p, prevP, setb, setflag, rate);
            /* every call in the game passes a rate of 1; a rate of 0
               divides by zero (its own tag) */
            fprintf(out, "%s %d %d %d %d %d %d %d %d", rate == 0.0f ? "lim-rate0" : "lim", row, h,
                    p, prevH, prevP, setb, setflag, (int)(rate * 1000.0f));
            fprintf(out, " -> %d %d %d %x", ha, pa, ba, unitCtrl.flags);
            fprintf(out, " | %d %d %d %d\n", limitInterpAngle(lim, h),
                    classifyInterpAngle(lim, h, 1.2f), classifyInterpAngle(lim, p, 0.8f),
                    limitInterpAngle(lim, p));
        }
    }
    /* a null record: the angles pass */
    {
        short ha, pa, ba;

        limitHPAngleAndSetB(&ha, &pa, &ba, 0, 12345, 0, -23456, 0, 1, 0, 1.0f);
        fprintf(out, "lim null -> %d %d %d\n", ha, pa, ba);
    }
}

static void sweepDirs(void)
{
    int h;
    int p;
    int i;

    fprintf(out, "# getDirOfAngle, getRotOfDir\n");
    for (h = -32768; h < 32768; h += 211) {
        for (p = -32768; p < 32768; p += 223) {
            float v[4];
            float q[4];

            getDirOfAngle(v, (short)h, (short)p);
            getRotOfDir(q, v);
            hashv = 2166136261u;
            hfv(v, 4);
            hfv(q, 4);
            if (((h + p) & 0xFFF) == 0 || p == -32768) {
                fprintf(out, "dir %d %d", h, p);
                pfv(v, 3);
                pfv(q, 4);
                fprintf(out, "\n");
            }
            fprintf(out, "dir-hash %d %d %08x\n", h, p, hashv);
        }
    }
    for (i = 0; i < 20000; i++) {
        float v[4];
        float n[4];
        float q[4];

        randDir(v);
        _NormalizeVector(n, v);
        getRotOfDir(q, n);
        fprintf(out, "rot %d", i);
        pfv(v, 3);
        pfv(q, 4);
        fprintf(out, "\n");
    }
}
#endif

/* --- two characters holding hands ------------------------------------------ */
#define NN 21

typedef struct {
    GObj g;
    Sub15C sub;
    struct DObjNode node;
    SkelNode skel[NN];
    float mtx[NN][16] __attribute__((aligned(16)));
    float quat[NN][4] __attribute__((aligned(16)));
    float motion[NN][8] __attribute__((aligned(16)));
    float motionStep[NN][4];
    MotIk ik[NN];
    int limit[NN];
    char focus[64];
    float ofs[4] __attribute__((aligned(16)));
} Character;

static Character ico, yorda;

/* the skeleton: kind (the focus id it answers to), parent, offset; the
   arm bones' lengths are set per scenario */
static const struct {
    int kind;
    int parent;
    float x, y, z;
} kSkel[NN] = {
    {0, -1, 0.0f, 0.0f, 0.0f},    /* 0 the hips (kind 0) */
    {1, 0, 0.0f, 10.0f, 0.0f},    /* 1 the spine: the torso turn */
    {18, 1, 0.0f, 12.0f, 0.0f},   /* 2 the chest */
    {34, 2, 0.0f, 10.0f, 0.0f},   /* 3 the neck: the look turn */
    {35, 3, 0.0f, 6.0f, 0.0f},    /* 4 the head: the look IK */
    {3, 2, -8.0f, 8.0f, 0.0f},    /* 5 hand 1's upper arm */
    {4, 5, 0.0f, 0.0f, 0.0f},     /* 6 its forearm */
    {5, 6, 0.0f, 0.0f, 0.0f},     /* 7 its wrist */
    {6, 7, 2.0f, 0.0f, 0.0f},     /* 8 its hand */
    {16, 8, 0.0f, 0.0f, 0.0f},    /* 9 its finger tip */
    {19, 2, 8.0f, 8.0f, 0.0f},    /* 10 hand 0's upper arm */
    {20, 10, 0.0f, 0.0f, 0.0f},   /* 11 its forearm */
    {21, 11, 0.0f, 0.0f, 0.0f},   /* 12 its wrist */
    {22, 12, 2.0f, 0.0f, 0.0f},   /* 13 its hand */
    {32, 13, 0.0f, 0.0f, 0.0f},   /* 14 its finger tip */
    {45, 0, -5.0f, -3.0f, 0.0f},  /* 15 a thigh */
    {46, 15, 0.0f, -25.0f, 0.0f}, /* 16 its shin */
    {47, 16, 0.0f, -25.0f, 0.0f}, /* 17 its foot */
    {49, 0, 5.0f, -3.0f, 0.0f},   /* 18 the other thigh */
    {50, 18, 0.0f, -25.0f, 0.0f}, /* 19 its shin */
    {51, 19, 0.0f, -25.0f, 0.0f}, /* 20 its foot */
};

typedef struct {
    float scale;
    float l1, l2, l3;
    int limitFrom;
    int staleIk;
} CharParams;

static void yawQuat(float *q, short yaw)
{
    int half = yaw >> 1;

    q[0] = 0.0f;
    q[1] = GetTableSin((short)half);
    q[2] = 0.0f;
    q[3] = GetTableCos((short)half);
}

static void setupCharacter(Character *c, const CharParams *cp, int labelId)
{
    int i;

    memset(c, 0, sizeof *c);
    c->g.self = &c->g;
    c->g.labelId = labelId;
    c->g.dobj = &c->sub;
    c->sub.nodes = &c->node;
    c->sub.skel = c->skel;
    c->sub.nodeNum = NN;
    c->sub.skelNodeNum = NN;
    c->sub.nodeMtx = (ICO_WORD)c->mtx;
    c->sub.nodeQuat = (ICO_WORD)c->quat;
    c->sub.nodeRotElem = c->ik;
    c->sub.nodeLimit = c->limit;
    c->sub.focusNodes = c->focus;
    c->node.scale[0] = c->node.scale[1] = c->node.scale[2] = cp->scale;
    memset(c->focus, -1, sizeof c->focus);
    for (i = 0; i < NN; i++) {
        SkelNode *n = &c->skel[i];
        int j;

        n->kind = kSkel[i].kind;
        n->parent = kSkel[i].parent;
        n->child = -1;
        n->sibling = -1;
        n->pos[0] = kSkel[i].x;
        n->pos[1] = kSkel[i].y;
        n->pos[2] = kSkel[i].z;
        n->pos[3] = 1.0f;
        n->quat[3] = 1.0f;
        c->focus[n->kind] = (char)i;
        for (j = i + 1; j < NN; j++) {
            if (kSkel[j].parent == i) {
                n->child = j;
                break;
            }
        }
        if (n->parent >= 0) {
            for (j = i + 1; j < NN; j++) {
                if (kSkel[j].parent == n->parent) {
                    n->sibling = j;
                    break;
                }
            }
        }
        c->mtx[i][0] = c->mtx[i][5] = c->mtx[i][10] = c->mtx[i][15] = 1.0f;
        c->quat[i][3] = 1.0f;
        /* DObj.c's initialRotElem, or a state an earlier stretch of play
           left */
        if (cp->staleIk) {
            c->ik[i].rate = frand(0.0f, 1.0f);
            c->ik[i].pitch = (short)irand(-8000, 8000);
            c->ik[i].pitchSpeed = (short)irand(-500, 500);
            randAnyQuat(c->ik[i].q);
            randUnitQuat(c->ik[i].step, 0.1f);
            randAnyQuat(c->ik[i].offset);
        } else {
            c->ik[i].q[3] = 1.0f;
            c->ik[i].step[3] = 1.0f;
            c->ik[i].offset[3] = 1.0f;
        }
    }
    /* the arm bones along +x; hand 1's arm turned to the other side */
    c->skel[6].pos[0] = c->skel[11].pos[0] = cp->l1;
    c->skel[7].pos[0] = c->skel[12].pos[0] = cp->l2;
    c->skel[9].pos[0] = c->skel[14].pos[0] = cp->l3;
    c->skel[5].quat[1] = 1.0f;
    c->skel[5].quat[3] = 0.0f;
    /* the joint limits: the character's four triples, by focus node */
    for (i = cp->limitFrom; i < cp->limitFrom + 12; i += 3) {
        int n = c->focus[LIMITS[i].node & 63];

        if (n >= 0) {
            c->limit[n] = i + 1;
        }
    }
    c->sub.ctrl.catchBoy = 1;
    c->sub.ctrl.motion = 0;
    c->sub.root.handIK = 1;
    c->sub.root.handTurnIK = 1;
    c->sub.root.twistRate = 1.0f;
    c->sub.root.handRate = 0.3f;
    c->sub.root.ikRate0 = 0.2f;
    c->sub.root.ikRate1 = 0.3f;
    c->sub.root.ikRate2 = 0.4f;
    c->sub.root.height = 55.0f;
    c->sub.root.quat[3] = 1.0f;
    c->sub.root.armTwist[3] = 1.0f;
    c->sub.root.lookPos[3] = 1.0f;
}

/* the current motion's record: GetGeometryOfMotion sets it before it calls
   GetMatrixOfMotion, which the harness calls alone (tools/arm64_diff.sh
   makes motionManager.o's static global under this name) */
#ifdef IKDIFF_UNITS
#define CUR_MOTION_DEF skelMotDef
#else
extern const MotionDef *ikdiff_skelMotDef;
#define CUR_MOTION_DEF ikdiff_skelMotDef
#endif

static void matrixOfMotion(Character *c)
{
    /* GetGeometryOfMotion places the root at its position plus the
       translation (both in the root block) */
    CopyVector(c->sub.root.pos, c->ofs);
    CUR_MOTION_DEF = &ikdiff_motion_kind()[c->sub.ctrl.motion];
    GetMatrixOfMotion(&c->g, (char *)c->motion, c->ofs);
}

static void connectHands(void)
{
    yorda.sub.root.hand0.mode = 6;
    yorda.sub.root.hand0.obj = &ico.g;
    yorda.sub.root.hand0.node = 0;
    ico.sub.root.hand1.mode = 5;
    ico.sub.root.hand1.obj = &yorda.g;
    ico.sub.root.hand1.node = 0;
}

static void stepMotion(Character *c)
{
    int i;

    for (i = 0; i < NN; i++) {
        float *q = &c->motion[i][4];
        float n;

        q[0] += c->motionStep[i][0];
        q[1] += c->motionStep[i][1];
        q[2] += c->motionStep[i][2];
        n = fsqrt_(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        q[0] /= n;
        q[1] /= n;
        q[2] /= n;
        q[3] /= n;
    }
}

static void randomMotion(Character *c, float spread)
{
    int i;

    for (i = 0; i < NN; i++) {
        randUnitQuat(&c->motion[i][4], spread);
        c->motionStep[i][0] = frand(-0.02f, 0.02f);
        c->motionStep[i][1] = frand(-0.02f, 0.02f);
        c->motionStep[i][2] = frand(-0.02f, 0.02f);
    }
}

static void hashCharacter(const Character *c)
{
    int i;

    for (i = 0; i < NN; i++) {
        hfv(c->mtx[i], 16);
        hfv(c->quat[i], 4);
        hf(c->ik[i].rate);
        hi(c->ik[i].pitch);
        hi(c->ik[i].pitchSpeed);
        hfv(c->ik[i].q, 4);
        hfv(c->ik[i].step, 4);
        hfv(c->ik[i].offset, 4);
    }
    hfv(c->sub.root.armTwist, 4);
    hi(c->sub.root.h);
    hi(c->sub.root.p);
    hi(c->sub.root.b);
    hf(c->sub.root.twistRate);
}

static void printCharacter(const char *who, const Character *c)
{
    int i;

    for (i = 0; i < NN; i++) {
        fprintf(out, "  %s node %d kind %d", who, i, c->skel[i].kind);
        pfv(c->mtx[i], 16);
        fprintf(out, " ik");
        pf(c->ik[i].rate);
        pi(c->ik[i].pitch);
        pi(c->ik[i].pitchSpeed);
        pfv(c->ik[i].q, 4);
        pfv(c->ik[i].step, 4);
        fprintf(out, "\n");
    }
    fprintf(out, "  %s root h %d p %d b %d twistRate", who, c->sub.root.h, c->sub.root.p,
            c->sub.root.b);
    pf(c->sub.root.twistRate);
    pfv(c->sub.root.armTwist, 4);
    fprintf(out, "\n");
}

/* Ico and Yorda standing side by side, Yorda's shoulder (the node at Ico's
   focus 19 index) at a distance l from Ico's right shoulder at the same
   height: the hand target connectToTarget gives (hand mode 5), relative to
   the shoulder.  Both reach (upper arm + forearm + hand) 26 at scale 1. */
static void sweepSideBySide(void)
{
    static const float scales[][2] = {{1.0f, 1.0f}, {1.2f, 1.0f}, {1.0f, 1.2f}};
    CharParams cp;
    int k;
    int l;

    fprintf(out, "# connectToTarget: Yorda beside Ico at a shoulder distance l\n");
    cp.l1 = 12.0f;
    cp.l2 = 10.0f;
    cp.l3 = 4.0f;
    cp.staleIk = 0;
    for (k = 0; k < 3; k++) {
        for (l = 0; l <= 140; l += 2) {
            const float *t;
            int n;

            cp.scale = scales[k][0];
            cp.limitFrom = 0;
            setupCharacter(&ico, &cp, 1);
            cp.scale = scales[k][1];
            cp.limitFrom = 12;
            setupCharacter(&yorda, &cp, 2);
            for (n = 0; n < NN; n++) {
                ico.mtx[n][13] = 130.0f;
                yorda.mtx[n][12] = (float)l;
                yorda.mtx[n][13] = 130.0f;
            }
            connectHands();
            HandManager(&ico.g);
            t = ico.sub.root.hand1.ikDir;
            fprintf(out, "side %d %d", k, l);
            pfv(t, 3);
            fprintf(out, " (%.3f %.3f %.3f)\n", (double)t[0], (double)(t[1] - 130.0f),
                    (double)t[2]);
        }
    }
}

static void sweepPath(int verbose)
{
    int s;

    fprintf(out, "# the hand-holding path: HandManager, GetMatrixOfMotion, 16 frames\n");
    for (s = 0; s < 4000; s++) {
        CharParams pi_, py;
        float dist;
        short yaw;
        short side;
        int f;
        int k = s % 10;

        pi_.scale = k == 7 ? 0.0f : (k == 8 ? frand(0.5f, 1.5f) : 1.0f);
        py.scale = k == 7 ? 0.0f : (k == 9 ? frand(0.5f, 1.5f) : 1.0f);
        pi_.l1 = frand(10.0f, 80.0f);
        pi_.l2 = frand(10.0f, 80.0f);
        pi_.l3 = frand(2.0f, 20.0f);
        py.l1 = frand(10.0f, 80.0f);
        py.l2 = frand(10.0f, 80.0f);
        py.l3 = frand(2.0f, 20.0f);
        pi_.limitFrom = 0;
        py.limitFrom = 12;
        pi_.staleIk = py.staleIk = (s & 3) == 1;
        ikdiff_obj_layout()[1].kind = (unsigned char)((s & 15) == 3 ? 2 : ((s & 15) == 5 ? 4 : 0));
        setupCharacter(&ico, &pi_, 1);
        setupCharacter(&yorda, &py, 2);
        connectHands();
        ikdiff_motion_kind()[0].faceRotRatio = frand(0.0f, 1.0f);
        systemStatus[0] = (s & 8) ? 1 : 0;
        systemStatus[1] = (s % 13 == 4) ? 1 : 2;
        ico.sub.root.handIK = (s % 6 == 2) ? 2 : 1;
        yorda.sub.root.handIK = (s % 6 == 4) ? 2 : 1;
        ico.sub.root.handTurnIK = (s % 5 == 3) ? 0 : 1;
        yorda.sub.root.handTurnIK = (s % 7 == 3) ? 0 : 1;
        /* the rates the actors set: 0.05 to 0.8 (act-game.c, boyact.c) */
        ico.sub.root.handRate = frand(0.05f, 0.5f);
        yorda.sub.root.handRate = frand(0.05f, 0.5f);
        ico.sub.root.ikRate0 = frand(0.05f, 0.8f);
        ico.sub.root.ikRate1 = frand(0.05f, 0.8f);
        ico.sub.root.ikRate2 = frand(0.05f, 0.8f);
        yorda.sub.root.ikRate0 = frand(0.05f, 0.8f);
        yorda.sub.root.ikRate1 = frand(0.05f, 0.8f);
        yorda.sub.root.ikRate2 = frand(0.05f, 0.8f);
        ico.sub.root.twist = (short)irand(-4000, 4000);
        yorda.sub.root.twist = (short)irand(-4000, 4000);
        ico.sub.root.flag330 = yorda.sub.root.flag330 = (s % 11) == 5;
        ico.sub.root.flag334 = yorda.sub.root.flag334 = (s % 11) == 6;
        if (s % 9 == 4) {
            ico.sub.root.lookIK = 1;
            ico.sub.root.lookMode = 1 + (s & 1);
        }
        if (s % 9 == 6) {
            yorda.sub.root.lookIK = 1;
            yorda.sub.root.lookMode = 1 + (s & 1);
        }
        if (s % 8 == 7) {
            ico.sub.root.lifting = yorda.sub.root.lifting = 1;
            ico.sub.root.lift[0] = frand(-20.0f, 20.0f);
            ico.sub.root.lift[1] = frand(-20.0f, 20.0f);
            yorda.sub.root.lift[0] = frand(-20.0f, 20.0f);
            yorda.sub.root.lift[1] = frand(-20.0f, 20.0f);
        }
        yaw = (short)irand(-32768, 32767);
        yawQuat(ico.sub.root.quat, yaw);
        yawQuat(yorda.sub.root.quat, (short)(yaw + irand(-6000, 6000)));
        ico.ofs[0] = frand(-3000.0f, 3000.0f);
        ico.ofs[1] = frand(-500.0f, 500.0f);
        ico.ofs[2] = frand(-3000.0f, 3000.0f);
        ico.ofs[3] = 1.0f;
        /* Yorda beside Ico: within reach, just past it, far, or at his place */
        dist = k == 1 ? frand(0.0f, 0.5f) : (k == 2 ? frand(150.0f, 400.0f) : frand(5.0f, 130.0f));
        side = (short)(yaw + 16384 + irand(-9000, 9000));
        yorda.ofs[0] = ico.ofs[0] + GetTableCos(side) * dist;
        yorda.ofs[1] = ico.ofs[1] + frand(-40.0f, 40.0f);
        yorda.ofs[2] = ico.ofs[2] + GetTableSin(side) * dist;
        yorda.ofs[3] = 1.0f;
        ico.sub.root.lookPos[0] = ico.ofs[0] + frand(-300.0f, 300.0f);
        ico.sub.root.lookPos[1] = ico.ofs[1] + frand(-100.0f, 200.0f);
        ico.sub.root.lookPos[2] = ico.ofs[2] + frand(-300.0f, 300.0f);
        CopyVector(yorda.sub.root.lookPos, ico.ofs);
        randomMotion(&ico, k == 3 ? 1.0f : 0.3f);
        randomMotion(&yorda, k == 3 ? 1.0f : 0.3f);
        for (f = 0; f < 16; f++) {
            unsigned int f0 = faults;

            HandManager(&ico.g);
            HandManager(&yorda.g);
            matrixOfMotion(&ico);
            matrixOfMotion(&yorda);
            stepMotion(&ico);
            stepMotion(&yorda);
            /* the characters move a little each frame */
            ico.ofs[0] += frand(-3.0f, 3.0f);
            ico.ofs[2] += frand(-3.0f, 3.0f);
            yorda.ofs[0] += frand(-3.0f, 3.0f);
            yorda.ofs[2] += frand(-3.0f, 3.0f);
            hashv = 2166136261u;
            hashCharacter(&ico);
            hashCharacter(&yorda);
            fprintf(out, "path %d %d", s, f);
            pfv(ico.sub.root.hand1.ikDir, 3);
            pfv(yorda.sub.root.hand0.ikDir, 3);
            /* Ico's right hand and Yorda's left hand, world positions */
            pfv(&ico.mtx[8][12], 3);
            pfv(&yorda.mtx[13][12], 3);
            fprintf(out, " %08x%s\n", hashv, faults != f0 ? " fault" : "");
            if (verbose == s) {
                printCharacter("ico", &ico);
                printCharacter("yorda", &yorda);
            }
        }
    }
}

int main(int argc, char **argv)
{
    int verbose = -1;

    out = stdout;
    /* the game runs in the EE's rounding: toward zero, denormals flushed
       (port/platform/fpenv.c); IKDIFF_NEAREST=1 keeps the host's default */
    {
        const char *e = getenv("IKDIFF_NEAREST");

        if (e == 0 || e[0] != '1') {
            ico_fpenv_sim_enter();
        }
    }
    if (argc > 1 && argv[1][0] != 0) {
        limitsFromElf = readLimits(argv[1]);
        if (!limitsFromElf) {
            fprintf(stderr, "ikdiff: %s has no joint limit table\n", argv[1]);
            return 2;
        }
    }
    if (argc > 2) {
        verbose = atoi(argv[2]);
    }
    if (!limitsFromElf) {
        syntheticLimits();
    }
    orderLimits();
    InitMatrixDrive();
    if (verbose < 0) {
        sweepTables();
        sweepTurns();
        sweepQuaternions();
        sweepMatrixDrive();
#ifdef IKDIFF_UNITS
        sweepLimits();
        sweepDirs();
#endif
        sweepSideBySide();
    }
    sweepPath(verbose);
    fprintf(out, "# faults %u, sounds %d\n", faults, seCalls);
    return 0;
}
