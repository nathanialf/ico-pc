#include "effectTool.h"
#include "pad.h"
#include "camera-root.h"
#include "lineManager.h"
#include "matrixDrive.h"
#include "particleEffect.h"
#include "tableSin.h"
#include <stdio.h>
#include <libvu0.h>
#include <sifdev.h>
#include "geometryManager.h"
#include "script.h"
#include <string.h>
#include "quaternion.h"
#include "main.h"
#include "GifPacket.h"
#include "debug.h"

/* the effect-parameter descriptor table _dispParam/editParam walk: 0x1C per
 * entry, name pointer first, NULL-terminated.  `off` is the byte offset of the
 * field inside the effect package, `enums` an optional name table for a type-0
 * field and (min,max) the range printed after the label. */
typedef struct {       /* field names derived */
    char *name;        /* 0x00 */
    int off;           /* 0x04 */
    char **enums;      /* 0x08 */
    unsigned int type; /* 0x0C */
    int step;          /* 0x10 */
    int min;           /* 0x14 */
    int max;           /* 0x18 */
} EffParamDef;         /* derived name */

/* the three enum name tables the type-0 fields print through */

static char *drainTypeName[] = {"RELEASE", "LOOP"}; /* derived name */

static char *alphaTypeName[] = {"BLEND", "ADD", "SUB"}; /* derived name */

static char *upperLimitName[] = {"OFF", "ON"}; /* derived name */

static EffParamDef effParam[] = {
    {"U OFFSET", 0x80, 0, 0, 0, 0, 3},
    {"V OFFSET", 0x84, 0, 0, 0, 0, 3},
    {"DRAIN TYPE", 4, drainTypeName, 0, 1, 0, 1},
    {"ALPHA TYPE", 8, alphaTypeName, 0, 0, 0, 2},
    {"CONE ANGLE", 12, 0, 3, 0, 0, 360},
    {"WIND EFFECT", 0x90, 0, 1, 0, 0, 10},
    {"VELOCITY", 16, 0, 1, 0, 0, 100},
    {"VEL RND RATIO", 20, 0, 1, 0, 0, 1},
    {"VEL ACCEL    ", 24, 0, 1, 0, 0, 2},
    {"GRAVITY ACC", 28, 0, 1, 0, -10, 10},
    {"ROT BASE", 0x88, 0, 2, 0, -180, 180},
    {"ROT BASE RND", 0x8C, 0, 1, 0, 0, 1},
    {"ROT GROW", 32, 0, 2, 0, -180, 180},
    {"ROT GROW RND", 36, 0, 1, 0, 0, 1},
    {"ROT GROW ACC", 40, 0, 1, 0, 0, 2},
    {"SIZE BASE", 44, 0, 1, 0, 0, 50},
    {"SIZE BASE RND", 48, 0, 1, 0, 0, 1},
    {"SIZE GROW", 52, 0, 1, 0, -50, 50},
    {"SIZE GROW RND", 56, 0, 1, 0, 0, 1},
    {"SIZE GROW ACC", 60, 0, 1, 0, 0, 1},
    {"NB POLYGONS", 64, 0, 0, 1, 1, 80},
    {"LIFE SPAN", 68, 0, 0, 0, 0, 1000},
    {"LIFE SPAN RND", 72, 0, 1, 0, 0, 1},
    {"BIRTH RATE", 76, 0, 1, 0, 0, 100},
    {"FADE BASE", 80, 0, 1, 0, 0, 1},
    {"FADE BASE RND", 84, 0, 1, 0, 0, 1},
    {"FADE OUT", 88, 0, 0, 0, 0, 1000},
    {"FADE OUT RND", 92, 0, 1, 0, 0, 1},
    {"COLOR R", 0x70, 0, 0, 0, 0, 255},
    {"COLOR G", 0x74, 0, 0, 0, 0, 255},
    {"COLOR B", 0x78, 0, 0, 0, 0, 255},
    {"UPPER LIMIT", 0x94, upperLimitName, 0, 0, 0, 1},
    {"LIMIT HEIGHT", 0x98, 0, 0, 0, -100000, 100000},
    {0},
}; /* derived name */

static void _dispParam(int *pkg, int idx, int x, int y, int col)
{
    char lbl[256];
    char val[256];
    char rng[256];
    char *p = (char *)pkg + effParam[idx].off;
    EffParamDef *e = &effParam[idx];

    switch (e->type) {
    case 1:
        sprintf(val, "%4.3f", *(float *)p);
        sprintf(rng, "(%d,%d)", e->min, e->max);
        break;
    case 0:
        if (e->enums == 0) {
            sprintf(val, "%d", *(int *)p);
            sprintf(rng, "(%d,%d)", e->min, e->max);
        } else {
            sprintf(val, "%s", e->enums[*(int *)p]);
            rng[0] = 0;
        }
        break;
    case 2:
        sprintf(val, "%d", *(short *)p);
        sprintf(rng, "(%d,%d)", e->min, e->max);
        break;
    case 3:
        sprintf(val, "%d", *(unsigned short *)p);
        sprintf(rng, "(%d,%d)", e->min, e->max);
        break;
    default:
        sprintf(val, "Unknown Data Type \"%s\"\n", e->name);
        break;
    }
    sprintf(lbl, "%s%s", e->name, rng);
    debug_PrintfDummy(x, y, col, "%-20s:%s", lbl, val);
}

typedef union { /* field names derived */
    int i;
    float f;
    short s;
    unsigned short us;
} EffVal; /* derived name */

/* editParam's hold counter */
static int holdCount = 0; /* derived name */

/* a change flag per tool row, then the position the tool's effect is placed
   at */
static int effectToolDirty[64]; /* derived name */

static float effectToolPos[4]; /* derived name */

static int editParam(int id, int sel)
{
    EffParamDef *e = &effParam[sel];
    int *pkg = GetParticleEffectPackage(id);
    EffVal *p = (EffVal *)((char *)pkg + effParam[sel].off);
    int changed = 0;
    float step;
    int v;

    if ((pad[0].now & 0x8000) || (pad[1].now & 0x8000) || (pad[0].now & 0x2000) ||
        (pad[1].now & 0x2000)) {
        holdCount++;
    } else {
        holdCount = 0;
    }
    if (holdCount > 30) {
        step = (holdCount - 10) / 10;
    } else {
        step = 1.0f;
    }
    switch (e->type) {
    case 1:
        if ((pad[0].rep & 0x8000) || (pad[1].rep & 0x8000)) {
            p->f -= step * 0.01f;
            if (p->f < e->min) {
                p->f = e->min;
            } else {
                changed = 1;
            }
        }
        if ((pad[0].rep & 0x2000) || (pad[1].rep & 0x2000)) {
            p->f += step * 0.01f;
            if (e->max < p->f) {
                p->f = e->max;
            } else {
                changed |= 1;
            }
        }
        break;
    case 0:
        if ((pad[0].rep & 0x8000) || (pad[1].rep & 0x8000)) {
            p->i = (float)p->i - step;
            if (p->i < e->min) {
                p->i = e->min;
            } else {
                changed = 1;
            }
        }
        if ((pad[0].rep & 0x2000) || (pad[1].rep & 0x2000)) {
            p->i = (float)p->i + step;
            if (e->max < p->i) {
                p->i = e->max;
            } else {
                changed |= 1;
            }
        }
        break;
    case 2:
        if ((pad[0].rep & 0x8000) || (pad[1].rep & 0x8000)) {
            v = p->s;
            v = (float)v - step;
            if (v < e->min) {
                p->s = e->min;
            } else {
                p->s = v;
                changed = 1;
            }
        }
        if ((pad[0].rep & 0x2000) || (pad[1].rep & 0x2000)) {
            v = p->s;
            v = (float)v + step;
            if (e->max < v) {
                p->s = e->max;
            } else {
                p->s = v;
                changed |= 1;
            }
        }
        break;
    case 3:
        if ((pad[0].rep & 0x8000) || (pad[1].rep & 0x8000)) {
            v = p->us;
            v = (float)v - step;
            if (v < e->min) {
                p->us = e->min;
            } else {
                p->us = v;
                changed = 1;
            }
        }
        if ((pad[0].rep & 0x2000) || (pad[1].rep & 0x2000)) {
            v = p->us;
            v = (float)v + step;
            if (e->max < v) {
                p->us = e->max;
            } else {
                p->us = v;
                changed |= 1;
            }
        }
        break;
    default:
        break;
    }
    if (changed) {
        effectToolDirty[id] |= 1;
    }
    return (changed && e->step != 0) || (pad[0].flags & 0x20) || (pad[1].flags & 0x20);
}

/* the tool's line colour (r=0, g=0xC0, b=0xFF, a=0x1C) and the dimmed copy the
 * second, blended pass draws with. */
typedef struct { /* field names derived */
    int r;
    int g;
    int b;
    int a;
} __attribute__((aligned(16))) EffCol; /* derived name */

typedef struct { /* field names derived */
    float x;
    float y;
    float z;
    float w;
} __attribute__((aligned(16))) EffVec; /* derived name */

/* the effect tool's own line colour and the three axis-circle colours the
   XZ/YZ/XY passes draw with; drawEdge draws each once solid and once at a
   sixteenth over the top. */
static EffCol effectToolColor = {0x00, 0xC0, 0xFF, 0x1C}; /* derived name */

static EffCol circleColorXZ = {0x00, 0x20, 0xFF, 0x1C}; /* derived name */

static EffCol circleColorYZ = {0xFF, 0x00, 0x20, 0x1C}; /* derived name */

static EffCol circleColorXY = {0x00, 0xFF, 0x20, 0x1C}; /* derived name */

/* Used by dispXZYZCircle (three times, with three different colours),
 * dispCircle2 and dispEffectToolField: draw the edge once solid and once with
 * a 1/16 colour over the top. */
static inline void drawEdge(EffVec *from, EffVec *to, EffCol *c) /* derived name */
{
    EffCol dim = {c->r >> 4, c->g >> 4, c->b >> 4, c->a};

    DrawLineG(from, c, to, c, 0);
    DrawLineG(from, &dim, to, &dim, -1);
}

static void dispXZYZCircle(float rad, int from, int to, int step)
{
    int i;

    for (i = from; i < to; i += step) {
        EffVec a = {rad * GetTableSin(i), 0.0f, rad * GetTableCos(i), 1.0f};
        EffVec b = {rad * GetTableSin(i + step), 0.0f, rad * GetTableCos(i + step), 1.0f};

        drawEdge(&a, &b, &circleColorXZ);
    }
    for (i = from; i < to; i += step) {
        EffVec a = {0.0f, rad * GetTableSin(i), rad * GetTableCos(i), 1.0f};
        EffVec b = {0.0f, rad * GetTableSin(i + step), rad * GetTableCos(i + step), 1.0f};

        drawEdge(&a, &b, &circleColorYZ);
    }
    for (i = from; i < to; i += step) {
        EffVec a = {rad * GetTableSin(i), rad * GetTableCos(i), 0.0f, 1.0f};
        EffVec b = {rad * GetTableSin(i + step), rad * GetTableCos(i + step), 0.0f, 1.0f};

        drawEdge(&a, &b, &circleColorXY);
    }
}

static void dispCircle2(float rad, short elev, int step)
{
    EffVec o;
    int i;

    memset(&o, 0, sizeof(o));
    o.w = 1.0f;
    for (i = 0; i <= 65535; i += step) {
        float r = rad * GetTableSin(elev);
        EffVec a = {r * GetTableSin((short)i), r * GetTableCos((short)i), rad * GetTableCos(elev),
                    1.0f};
        EffVec b = {r * GetTableSin((short)(i + step)), r * GetTableCos((short)(i + step)),
                    rad * GetTableCos(elev), 1.0f};

        drawEdge(&a, &b, &effectToolColor);
        drawEdge(&o, &a, &effectToolColor);
    }
}

/* The tool's state: the particle effect on show, the view rotation, the
   effect and parameter being edited, the field display switch, the camera
   target to restore, and the tool's mode. */
static int effectHandle = -1; /* derived name */

static short viewRotY = 0; /* derived name */

static short viewRotX = 0; /* derived name */

static int effectId = 0; /* derived name */

static int lastEffectId = -1; /* derived name */

static int paramCursor = 0; /* derived name */

static int fieldDisp = 0; /* derived name */

static GObj *savedTarget = 0; /* derived name */

int targetMemo = 0;

static void setQ(int *self)
{
    SetIdentityQuaternion(self);
    RotQuaternionY(self, -viewRotY);
    RotQuaternionX(self, -viewRotX);
}

static void dispEffectToolField(int idx)
{
    int q[4];
    EffVec o;
    int *pkg = GetParticleEffectPackage(idx);

    setQ(q);
    gif_StartPacketPri(0xB);
    gif_SetAlpha(1, 5, 0x80);
    gif_SetZTest(1);
    MatrixDrive_PushMatrix();
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_TransMatrix(effectToolPos[0], effectToolPos[1], effectToolPos[2]);
    dispXZYZCircle(50.0f, -0x8000, 0x8000, 0x1000);
    MultiMatrixByQuaternion(q);
    dispCircle2(50.0f, (*(unsigned short *)((char *)pkg + 0xC) << 14) / 180, 0x1000);
    dispXZYZCircle(50.0f, -0x8000, 0x8000, 0x1000);

    memset(&o, 0, sizeof(o));
    o.w = 1.0f;
    /* the 100-unit +Z spoke drawn out of the origin */
    {
        EffVec e = {0.0f, 0.0f, 100.0f, 1.0f};

        drawEdge(&o, &e, &effectToolColor);
    }

    MatrixDrive_PopMatrix();
    gif_EndPacket();
    debug_PrintfDummy(450, 58, 0xFFFFFF00, "POS-X:%4.3f", -effectToolPos[0]);
    debug_PrintfDummy(450, 66, 0xFFFFFF00, "POS-Y:%4.3f", -effectToolPos[1]);
    debug_PrintfDummy(450, 74, 0xFFFFFF00, "POS-Z:%4.3f", -effectToolPos[2]);
    debug_PrintfDummy(450, 88, 0xFFFFFF00, "ROT-Y:%4.3f", viewRotY * -180.0f / 32768.0f);
    debug_PrintfDummy(450, 96, 0xFFFFFF00, "ROT-X:%4.3f", viewRotX * -180.0f / 32768.0f);
}

/* two helpers for EditTarget */
static inline int countEffectParams(void) /* derived name */
{
    int n = 0;
    if (effParam[0].name != 0) {
        do {
            n++;
        } while (effParam[n].name != 0);
    }
    return n;
}

static inline void dispEffectParams(int id, int sel) /* derived name */
{
    int n = countEffectParams();
    int *pkg = GetParticleEffectPackage(id);
    int start = sel - 5;
    int i;

    if (start < 0) {
        start = 0;
    }
    if (start + 10 > n) {
        start = n - 10;
    }
    for (i = 0; i < 10 && start + i < n; i++) {
        _dispParam(pkg, start + i, 10, i * 8 + 0x32, (sel == start + i) ? 0x00E0FF00 : 0xFFFFFF00);
    }
}

static int EditTarget(int id)
{
    int q[4];
    int n;

    n = countEffectParams();
    setQ(q);
    if (editParam(id, paramCursor) != 0) {
        ResetParticleEffectPackages(GetParticleEffectPackage(id));
        DeleteParticleEffect(effectHandle);
        effectHandle = SetParticleEffect(effectId, effectToolPos, q);
    }
    dispEffectParams(id, paramCursor);
    if ((pad[0].rep & 0x1000) || (pad[1].rep & 0x1000)) {
        paramCursor--;
        if (paramCursor < 0) {
            paramCursor = n - 1;
        }
    }
    if ((pad[0].rep & 0x4000) || (pad[1].rep & 0x4000)) {
        paramCursor++;
        if (paramCursor == n) {
            paramCursor = 0;
        }
    }
    if ((pad[0].flags & 0x40) || (pad[1].flags & 0x40)) {
        return -1;
    }
    return 0;
}

/* the set-up EffectTool runs first */
static inline void initEffectTool(void) /* derived name */
{
    int q[4];
    int i;

    setQ(q);
    savedTarget = CameraGetTarget();
    GetRootPosition(effectToolPos, savedTarget);
    effectToolPos[3] = 1.0f;
    GetRootQuaternion(q, savedTarget);
    CameraSetMode(1);
    scpBoyControlReadDisable = 1;
    debug_StdPrintfDummy("initialize\n");
    for (i = 0x3C; i >= 0; i--) {
        effectToolDirty[i] = 0;
    }
}

static int saveEffectData(int id)
{
    int *pkg;

    pkg = GetParticleEffectPackage(id);
    debug_closeLog();
    debug_StdPrintfDummy("==== Save effect ============================================\n");
    if (debugSceOpen(particleEffectFile[id].path, 0x602) < 0) {
        debug_StdPrintfDummy("saveEffectData: host file open error.\n");
    } else {
        debug_StdPrintfDummy("Save effect file [\033[36m%s\033[m](%s:%dbytes) \n",
                             particleEffectFile[id].path, particleEffectFile[id].name, 0xA0);
        debug_StdPrintfDummy("%d bytes wrote\n", sceWrite(0, pkg, 0xA0));
        debugSceClose(0);
    }
    debug_StdPrintfDummy("=============================================================\n");
    debug_openLog();
    return 0;
}

static void moveEffectToolGeometry(int idx)
{
    float v[4];
    IosPadCtx padCtx;
    IosPadStick st0;
    IosPadStick st1;
    int q[4];
    int *pkg;

    iosPadConnect(&padCtx, 0, 0, &iosPadConfDefault);
    iosPadRead(&padCtx);
    iosPadGetStick(&padCtx, &st0, 0, 2, 2, 0);
    iosPadGetStick(&padCtx, &st1, 1, 2, 2, 0);
    iosPadStickCameraCoord(v, &st0);
    if (st0.mag > 0.001f) {
        effectToolPos[0] += v[0] * st0.mag * 16.0f;
        effectToolPos[2] += v[2] * st0.mag * 16.0f;
    }
    if (st1.mag > 0.001f) {
        if (padCtx.now & 2) {
            effectToolPos[1] += st1.dz * st1.mag * 16.0f;
        } else {
            viewRotY = (short)(int)(viewRotY + st1.dx * 256.0f * st1.mag);
            viewRotX = (short)(int)(viewRotX + st1.dz * 256.0f * st1.mag);
            if (viewRotX < -0x4000) {
                viewRotX = -0x4000;
            }
            if (viewRotX > 0x4000) {
                viewRotX = 0x4000;
            }
        }
    }
    pkg = GetParticleEffectPackage(idx);
    if (pkg[1] != 0) {
        setQ(q);
        SetParticleEffectGeometry(effectHandle, effectToolPos, q);
    }
}

static int execEffectTool(void)
{
    int q[4];
    int r;

    switch (targetMemo) {
    default:
    case 0:
        r = debug_SelectCsvWindow("Effect Tools: PUSH 2-CON'\202' TO SAVE SELECTED DATA", 10, 0x32,
                                  0xB, particleEffectFile, 0x50, 0, 0, 0x3D, &effectId);
        if (effectId != lastEffectId) {
            setQ(q);
            if (effectHandle != -1) {
                DeleteParticleEffect(effectHandle);
            }
            effectHandle = SetParticleEffect(effectId, effectToolPos, q);
            lastEffectId = effectId;
            paramCursor = 0;
        }
        if (pad[1].flags & 0x10) {
            saveEffectData(effectId);
        }
        if (pad[1].flags & 0x20) {
            r = 1;
        }
        if (pad[1].rep & 0x1000) {
            effectId--;
            if (effectId < 0) {
                effectId = 0x3C;
            }
        }
        if (pad[1].rep & 0x4000) {
            effectId++;
            if (effectId >= 0x3D) {
                effectId = 0;
            }
        }
        if (r == 1) {
            r = 0;
            targetMemo++;
        }
        break;
    case 1:
        r = EditTarget(effectId);
        if (r == -1) {
            targetMemo--;
        }
        r = 0;
        break;
    }
    moveEffectToolGeometry(effectId);
    if ((pad[0].flags & 0x80) || (pad[1].flags & 0x80)) {
        fieldDisp = (fieldDisp == 0);
    }
    if (fieldDisp) {
        dispEffectToolField(effectId);
    }
    return r;
}

static void exitEffectTool(void)
{
    DeleteParticleEffect(effectHandle);
    effectHandle = -1;
    Camctrl_SetTarget(savedTarget, 0, 3);
    scpBoyControlReadDisable = 0;
    debug_StdPrintfDummy("exit\n");
}

/* EffectTool's entered flag */
static int toolEntered = 0; /* derived name */

int EffectTool(void)
{
    int r;

    if (toolEntered == 0) {
        initEffectTool();
        toolEntered = 1;
        fieldDisp = 1;
    }
    r = execEffectTool();
    if (r == -1) {
        exitEffectTool();
        toolEntered = 0;
        lastEffectId = r;
        paramCursor = 0;
    }
    return r;
}
