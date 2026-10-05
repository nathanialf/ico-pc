#include "debug.h"
#include "memory.h"
#include "camera-root.h"
#include "Basic.h"
#include "Light.h"
#include "ee_view.h"
#include "DisplayP2O.h"
#include "geometryManager.h"
#include "lineManager.h"
#include "matrixDrive.h"
#include <math.h>
#include <string.h>
#include <libvu0.h>
#include "ios.h"
#include "Matrix.h"
#include "Primitive.h"
#include "debug_exception.h"
#include "wireLetter.h"
#include "main.h"
#include "GifPacket.h"
#include <stdio.h>
#include <assert.h>

/* one light on the list: where it is, the direction a flat light shines,
   its colour, the scale and range the stage gives it, the falloff and the
   strength light_getNearLight works out for the object being lit, the object
   carrying it (kind 1) and its kind (0 flat, 1 object, 2 and 3 fixed) */
typedef struct Light { /* field names derived */
    float pos[4];      /* 0x00 */
    float dir[4];      /* 0x10 */
    float col[4];      /* 0x20 */
    float scale;       /* 0x30 */
    float range;       /* 0x34 */
    float falloff;     /* 0x38 */
    float strength;    /* 0x3C */
    GObj *owner;       /* 0x40 */
    short kind;        /* 0x44 */
    char pad46[2];
    struct Light *next; /* 0x48 */
    struct Light *prev; /* 0x4C */
} Light;                /* derived name */

/* one ambient volume: its placement matrix, the ambient colour inside it,
   the inverse extents of its inner and outer shells, the scale of the volume,
   the light scale inside it and its shape (0 off, 1 box, 2 ellipsoid) */
typedef struct AmbientVolume { /* field names derived */
    float mtx[4][4];           /* 0x00 */
    float col[4];              /* 0x40 */
    float inner[4];            /* 0x50 */
    float outer[4];            /* 0x60 */
    float size[4];             /* 0x70 */
    float lightScale;          /* 0x80 */
    char pad84[12];
    int shape;                  /* 0x90 */
    struct AmbientVolume *next; /* 0x94 */
    struct AmbientVolume *prev; /* 0x98 */
} AmbientVolume;                /* derived name */

/* The cursor debug view's two pad angles, the newest light and the newest ambient volume
   (each list is walked back through prev), and the light count the retail
   build no longer increments. */
static int cursorRotY; /* derived name */

static int cursorRotX; /* derived name */

/* the newest light, kept as a word */
static ICO_WORD lastLight; /* derived name */

static AmbientVolume *lastAmbient; /* derived name */

static int lightCount; /* derived name */

/* the count of flat lights light_AddLight has registered, which
   light_resetFlatLight clears */
static int flatLightNum = 0; /* derived name */

static void light_killLinkLight(Light *p)
{
    if (p == 0) {
        /* "the light is NULL" */
        debug_StdPrintfDummy("Light:NULLになってんで\n");
        debug_assert("src/Light.c", 424);
        __assert("src/Light.c", 424, "0");
    }
    if (p->next != 0) {
        p->next->prev = p->prev;
    } else {
        lastLight = (ICO_WORD)p->prev;
    }
    if (p->prev != 0) {
        p->prev->next = p->next;
    }
    if (lastLight != 0) {
        ((Light *)lastLight)->next = 0;
    }
    freeseki(p);
}

static void light_killLinkAmbient(AmbientVolume *p)
{
    if (p == 0) {
        /* "the ambient volume is NULL" */
        debug_StdPrintfDummy("AmbientVolume:NULLになってんで\n");
        debug_assert("src/Light.c", 451);
        __assert("src/Light.c", 451, "0");
    }
    if (p->next != 0) {
        p->next->prev = p->prev;
    } else {
        lastAmbient = p->prev;
    }
    if (p->prev != 0) {
        p->prev->next = p->next;
    }
    if (lastAmbient != 0) {
        lastAmbient->next = 0;
    }
    freeseki(p);
}

/* The three flat lights light_AddLight registers, kept so
   light_resetFlatLight can reload them from the stage setting. */
static ICO_WORD flatLightSlot[3] = {0, 0, 0}; /* derived name */

/* The three flat lights the stage setting is reloaded into. */
static Light flatLight[3]; /* derived name */

/* The list head keeps the newest node. */
static inline void light_setLinkLight(Light *p) /* derived name */
{
    if (lastLight != 0) {
        ((Light *)lastLight)->next = p;
    }
    p->next = 0;
    p->prev = (Light *)lastLight;
    lastLight = (ICO_WORD)p;
}

Light *light_AddLight(GObj *self, int b, int kind)
{
    int i;
    float d;
    const ObjLight *t;

    switch (kind) {
    case 0: {
        Light *l;

        if (flatLightNum != 0) {
            debug_StdPrintfDummy("Flat Lights already exist.\n");
            light_resetFlatLight();
            return 0;
        }
        for (i = 0; i < 3; i++) {
            l = &flatLight[i];
            _CopyVector(l->col, GlobalStageSetting.flatLightCol[i]);
            _NormalizeVector(l->dir, GlobalStageSetting.flatLightDir[i]);
            l->scale = 1.0f;
            l->range = 0.0f;
            l->falloff = 1.0f;
            d = (l->col[0] + l->col[1] + l->col[2]) * 0.3333f;
            if (d < 0.0f) {
                d = -d;
            }
            l->strength = d;
            light_setLinkLight(l);
            flatLightSlot[(flatLightNum)++] = (ICO_WORD)l;
        }
        return 0;
    }
    case 1: {
        Light *q;

        if (b == 0) {
            return 0;
        }
        if (self->dobj == 0) {
            return 0;
        }
        q = (Light *)iosMallocDebug(ios_partition_seki, sizeof(Light) > 80 ? sizeof(Light) : 80,
                                    "src/Light.c", 620);
        self->dobj->lightId = b;
        q->owner = self;
        q->kind = kind;
        t = &objectLight[b];
        q->col[0] = t->col[0] * 0.00390625f;
        q->col[1] = t->col[1] * 0.00390625f;
        q->col[2] = t->col[2] * 0.00390625f;
        q->col[3] = 1.0f;
        q->scale = 1.0f;
        q->range = (0.0f < t->range) ? t->range : 1.0f;
        q->falloff = 1.0f;
        d = q->col[0] + q->col[1] + q->col[2];
        if (d < 0.0f) {
            d = -d;
        }
        q->strength = d;
        light_setLinkLight(q);
        return q;
    }
    case 2:
    case 3: {
        Light *r;

        r = (Light *)iosMallocDebug(ios_partition_seki, sizeof(Light) > 80 ? sizeof(Light) : 80,
                                    "src/Light.c", 685);
        r->kind = kind;
        r->scale = 1.0f;
        r->range = 32768.0f;
        light_setLinkLight(r);
        return r;
    }
    default:
        debug_StdPrintfDummy("Added Light is illegal.\n");
        debug_assert("src/Light.c", 705);
        __assert("src/Light.c", 705, "0");
    }
    return 0;
}

/* Each switch arm writes its own abs and weight store; the range tests are
   nested ifs. */
static void light_getNearLight(Sub15C *self, int idx)
{
    Light *near[3];
    float pos[4];
    float dir[4];
    float tmp[4];
    Light *p;
    float d;
    int i;
    int j;
    int k;

    memset(dir, 0, 16);
    if (lastLight == 0) {
        return;
    }
    for (i = 0; i < 3; i++) {
        near[i] = 0;
    }
    if (self->dispType == 2) {
        _CopyVector(pos, (char *)self->nodeMtx + idx * 64 + 48);
    } else {
        _CopyVector(pos, (char *)self->nodeMtx + 48);
    }
    for (p = (Light *)lastLight; p != 0; p = p->prev) {
        switch (p->kind) {
        case 0:
            p->scale = 1.0f;
            p->range = 0.0f;
            p->falloff = 1.0f;
            d = (p->col[0] + p->col[1] + p->col[2]) * 0.3333f;
            if (d < 0.0f) {
                d = -d;
            }
            p->strength = d;
            break;
        case 1:
            /* the dobj's light number, in j, the insert loop's index;
               GetRootPositionByDObj takes the dobj as its second argument
               (geometryManager.c) */
            j = p->owner->dobj->lightId;
            if (j == 0) {
                p->falloff = 0.0f;
                p->strength = 0.0f;
                continue;
            }
            GetRootPositionByDObj(p->pos, p->owner->dobj);
            d = _GetLength(pos, p);
            if (p->range < d) {
                p->falloff = 0.0f;
                p->strength = 0.0f;
                continue;
            }
            p->falloff = (p->range - d) / p->range;
            d = p->falloff * p->scale * ((p->col[0] + p->col[1] + p->col[2]) * 0.3333f);
            if (d < 0.0f) {
                d = -d;
            }
            p->strength = d;
            break;
        case 2:
        case 3:
            d = _GetLength(pos, p);
            if (p->range < d) {
                p->falloff = 0.0f;
                p->strength = 0.0f;
                continue;
            }
            p->falloff = (p->range - d) / p->range;
            d = p->falloff * p->scale * ((p->col[0] + p->col[1] + p->col[2]) * 0.3333f);
            if (d < 0.0f) {
                d = -d;
            }
            p->strength = d;
            break;
        default:
            continue;
        }
    }
    for (p = (Light *)lastLight; p != 0; p = p->prev) {
        if (p->strength == 0.0f) {
            continue;
        }
        for (j = 0; j < 3; j++) {
            if (near[j] == 0 || near[j]->strength < p->strength) {
                for (k = 2; k > j; k--) {
                    near[k] = near[k - 1];
                }
                near[j] = p;
                break;
            }
        }
    }
    for (p = (Light *)lastLight; p != 0; p = p->prev) {
        if (p->strength == 0.0f) {
            continue;
        }
        if (p->kind == 0) {
            _ScaleVectorXYZ(tmp, p->dir, p->strength);
            _AddVector(dir, dir, tmp);
        } else if (p->kind >= 0) {
            if (p->kind < 3) {
                _SubVector(tmp, pos, p);
                _NormalizeVector(tmp, tmp);
                _ScaleVectorXYZ(tmp, tmp, p->strength);
                _AddVector(dir, dir, tmp);
            }
        }
    }
    _NormalizeVector(ICO_RAWP(char *, self, 0x860, (char *)self->shadowDir), dir);
    for (i = 0; i < 3; i++) {
        if (near[i] != 0) {
            if (near[i]->kind == 0) {
                _NormalizeVector(self->lightMtx->dir[i], near[i]->dir);
                _CopyVector(self->lightMtx->col[i], near[i]->col);
            } else if (near[i]->kind >= 0) {
                if (near[i]->kind < 4) {
                    _SubVector(self->lightMtx->dir[i], pos, near[i]);
                    _NormalizeVector(self->lightMtx->dir[i], self->lightMtx->dir[i]);
                    _ScaleVectorXYZ(self->lightMtx->col[i], near[i]->col,
                                    near[i]->falloff * near[i]->scale);
                }
            }
        } else {
            _UnitVector(self->lightMtx->dir[i]);
            _UnitVector(self->lightMtx->col[i]);
        }
        self->lightMtx->dir[i][3] = 0.0f;
        self->lightMtx->col[i][3] = 1.0f;
    }
}

/* a macro, so each use calls _GetNorm once for the sign test and once in
   each arm */
#define LIGHT_ABS(x) ((x) < 0.0f ? -(x) : (x)) /* derived name */

static void light_getAmbientLight(Sub15C *self, int idx)
{
    float pos[4];
    float p[4];
    float q[4];
    float s0[4];
    float s1[4];
    AmbientVolume *v;
    float best;
    float scale;
    /* mx/my carry the largest inner-ellipsoid component and its outer
       partner in the kind-1 arm; the kind-2 arm reuses my as its own blend
       total. */
    float mx;
    float my;

    scale = 1.0f;
    if (lastAmbient == 0) {
        _CopyVector(self->lightMtx->ambient, GlobalStageSetting.ambientCol);
        return;
    }
    _CopyVector(self->lightMtx->ambient, GlobalStageSetting.ambientCol);
    best = 3.0f;
    if (self->dispType == 2) {
        _CopyVector(pos, (char *)self->nodeMtx + (idx << 6) + 48);
    } else {
        _CopyVector(pos, (char *)self->nodeMtx + 48);
    }
    for (v = lastAmbient; v != 0; v = v->prev) {
        if (v->shape == 0) {
            continue;
        }
        _SetCurrentMatrix(v);
        _InverseCurrentMatrix();
        _ApplyCurrentMatrix(p, pos);
        _ApplyCurrentMatrix(q, pos);
        _ScaleVector2XYZ(p, p, v->size);
        _ScaleVector2XYZ(q, q, v->size);
        _ScaleVector2XYZ(p, p, v->inner);
        _ScaleVector2XYZ(q, q, v->outer);
        switch (v->shape) {
        case 2: {
            float nx;
            float ny;
            float rx;
            float ry;

            nx = LIGHT_ABS(_GetNorm(p));
            ny = LIGHT_ABS(_GetNorm(q));
            if (nx <= 1.0f) {
                _CopyVector(self->lightMtx->ambient, v->col);
                scale = v->lightScale;
                goto found;
            }
            if (ny <= 1.0f) {
                rx = nx - 1.0f;
                ry = 1.0f - ny;
                _SubVectorXYZ(s0, GlobalStageSetting.ambientCol, v->col);
                _ScaleVectorXYZ(s0, s0, rx / (rx + ry));
                _AddVector(s0, v->col, s0);
                my = s0[0] + s0[1] + s0[2];
                if (my < best) {
                    _CopyVector(self->lightMtx->ambient, s0);
                    best = my;
                    scale = v->lightScale + (1.0f - v->lightScale) * rx / (rx + ry);
                }
            }
            break;
        }
        case 1: {
            float sum;

            if (LIGHT_ABS(p[0]) <= 1.0f && LIGHT_ABS(p[1]) <= 1.0f && LIGHT_ABS(p[2]) <= 1.0f) {
                _CopyVector(self->lightMtx->ambient, v->col);
                scale = v->lightScale;
                goto found;
            }
            if (LIGHT_ABS(q[0]) <= 1.0f && LIGHT_ABS(q[1]) <= 1.0f && LIGHT_ABS(q[2]) <= 1.0f) {
                mx = LIGHT_ABS(p[0]);
                my = LIGHT_ABS(q[0]);
                if (mx < LIGHT_ABS(p[1])) {
                    mx = LIGHT_ABS(p[1]);
                    my = LIGHT_ABS(q[1]);
                }
                if (mx < LIGHT_ABS(p[2])) {
                    mx = LIGHT_ABS(p[2]);
                    my = LIGHT_ABS(q[2]);
                }
                mx = mx - 1.0f;
                my = 1.0f - my;
                _SubVectorXYZ(s1, GlobalStageSetting.ambientCol, v->col);
                _ScaleVectorXYZ(s1, s1, mx / (mx + my));
                _AddVector(s1, v->col, s1);
                sum = s1[0] + s1[1] + s1[2];
                if (sum < best) {
                    /* the kind-1 arm copies the volume colour here where the
                       kind-2 arm copies its blended vector */
                    _CopyVector(self->lightMtx->ambient, v->col);
                    best = sum;
                    scale = v->lightScale + (1.0f - v->lightScale) * mx / (mx + my);
                }
            }
            break;
        }
        }
    }
found:
    _ScaleVectorXYZ(self->lightMtx->col[0], self->lightMtx->col[0], scale);
    _ScaleVectorXYZ(self->lightMtx->col[1], self->lightMtx->col[1], scale);
    _ScaleVectorXYZ(self->lightMtx->col[2], self->lightMtx->col[2], scale);
    self->lightMtx->ambient[3] = 1.0f;
}

void light_MakeLightMatrix(Sub15C *self, int idx)
{
    int i;

    if (self->lightMtx->mode == 0) {
        return;
    }
    light_getNearLight(self, idx);
    light_getAmbientLight(self, idx);
    for (i = 0; i < 3; i++) {
        _ScaleVectorXYZ(self->lightMtx->col[i], self->lightMtx->col[i],
                        self->model->mode.s.lightScale);
    }
    _ScaleVectorXYZ(self->lightMtx->ambient, self->lightMtx->ambient, self->model->ambientScale);
    _MakeNormalLightMatrix(self->lightMtx->normal, self->lightMtx->dir[0], self->lightMtx->dir[1],
                           self->lightMtx->dir[2]);
    _MakeLightColorMatrix(self->lightMtx->color, self->lightMtx->col[0], self->lightMtx->col[1],
                          self->lightMtx->col[2], self->lightMtx->ambient);
}

/* The light and ambient volume debug view.  The slot search that would set
   `i` is not in the retail build, so the test below reads what the previous
   light left in it.  Both lists are walked as
   `p = head; while (p != 0) { ...; p = p->prev; }`, and the ambient colour is
   built by its initializer. */

void light_DispVolume(void)
{
    int i;

    if (debug_ambient_volume & 1) {
        sceVu0FMATRIX m;
        char buf[256];
        int col[4];
        int black[4];
        int dir[4];
        float pos[4];
        float p0[4];
        float p1[4];
        Light *lp;

        lp = (Light *)lastLight;
        while (lp != 0) {
            switch (lp->kind) {
            case 1:
                if (lp == 0) {
                    break;
                }
                if (lp->owner == 0) {
                    break;
                }
                if (lp->owner->dobj->lightId == 0) {
                    break;
                }
            case 2:
            case 3:
                if (lp->range == 0.0f) {
                    break;
                }
                _UnitMatrix(MatrixDrive_GetMatrix());
                _CopyVector(MatrixDrive_GetMatrix()[3], lp);
                _TransposeMatrix(m, matrixptr + 0x80);
                m[0][3] = m[1][3] = m[2][3] = 0.0f;
                _MulMatrix(MatrixDrive_GetMatrix(), MatrixDrive_GetMatrix(), m);
                if (lp->kind == 1) {
                    sprintf(buf, "OBJ");
                } else {
                    sprintf(buf, "FIX");
                }
                DispWireString(buf);
                gif_StartPacketPri(11);
                col[0] = lp->col[0] * 255.0f;
                col[1] = lp->col[1] * 255.0f;
                col[2] = lp->col[2] * 255.0f;
                col[3] = 128;
                black[0] = black[1] = black[2] = 0;
                black[3] = 128;
                _UnitMatrix(MatrixDrive_GetMatrix());
                _CopyVector(MatrixDrive_GetMatrix()[3], lp);
                gif_SetZTest(1);
                gif_SetAlpha(1, 2, 64);
                prim_DispWireSphere(lp->range * 0.1f, col, 6, 6);
                if (i != 0) {
                    GetRootPosition(pos, boyGObj);
                    _SubVector(pos, pos, lp);
                    pos[3] = 1.0f;
                    _NormalizeVector(pos, pos);
                    _ScaleVectorXYZ(pos, pos, lp->range);
                    _UnitVector(dir);
                    DrawLineG(dir, col, pos, black, 0);
                }
                for (i = 0; i < 3; i++) {
                    _UnitVector(p0);
                    _UnitVector(p1);
                    p0[i] -= lp->range;
                    p1[i] += lp->range;
                    p0[3] = p1[3] = 1.0f;
                    DrawLineG(p0, col, p1, col, 0);
                }
                gif_EndPacket();
                break;
            }
            lp = lp->prev;
        }
    }
    if (debug_ambient_volume & 2) {
        AmbientVolume *av;

        av = lastAmbient;
        while (av != 0) {
            Col4 col = {{av->col[0] * 255.0f, av->col[1] * 255.0f, av->col[2] * 255.0f, 128}};
            float ext[4];

            /* a second null test of av, which the loop test already makes;
               the first loop re-tests its light pointer the same way */
            if (av == 0) {
                break;
            }
            switch (av->shape) {
            case 2:
                _SetCurrentMatrix(av);
                _ScaleCurrentMatrix(1.0f / av->size[0], 1.0f / av->size[1], 1.0f / av->size[2]);
                _GetCurrentMatrix(MatrixDrive_GetMatrix());
                gif_StartPacketPri(11);
                gif_SetZTest(1);
                gif_SetAlpha(1, 2, 64);
                prim_DispWireSphere(1.0f / av->outer[0], &col, 6, 6);
                prim_DispWireSphere(1.0f / av->inner[0], &col, 6, 6);
                gif_EndPacket();
                break;
            case 1:
                _SetCurrentMatrix(av);
                _ScaleCurrentMatrix(1.0f / av->size[0], 1.0f / av->size[1], 1.0f / av->size[2]);
                _GetCurrentMatrix(MatrixDrive_GetMatrix());
                gif_StartPacketPri(11);
                gif_SetZTest(1);
                gif_SetAlpha(1, 2, 64);
                ext[0] = 1.0f / av->inner[0];
                ext[1] = 1.0f / av->inner[1];
                ext[2] = 1.0f / av->inner[2];
                prim_DispWireBox(ext, &col);
                ext[0] = 1.0f / av->outer[0];
                ext[1] = 1.0f / av->outer[1];
                ext[2] = 1.0f / av->outer[2];
                prim_DispWireBox(ext, &col);
                gif_EndPacket();
                break;
            }
            av = av->prev;
        }
    }
}

/* Declared `inline`: light_Tool expands it and the out-of-line copy goes to
   the end of the object.  light_AddLight sits above this definition and so
   keeps its out-of-line call. */
inline void light_resetFlatLight(void)
{
    int i;
    Light *l;

    for (i = 0; i < 3; i++) {
        l = (Light *)flatLightSlot[i];
        if (l != 0) {
            _CopyVector(l->col, GlobalStageSetting.flatLightCol[i]);
            _NormalizeVector(l->dir, GlobalStageSetting.flatLightDir[i]);
            l->scale = 1.0f;
            l->range = 0.0f;
            l->falloff = 1.0f;
            l->strength = (l->col[0] + l->col[1] + l->col[2]) * 0.3333f;
        }
    }
}

static void light_GetColorAnalog(float *col)
{
    float x;
    float y;
    float a;
    float r;
    float g;
    float b;
    float d;

    x = (float)(pad[1].ana[0] - 128);
    y = (float)(pad[1].ana[1] - 128);

    a = atan2f(x, y);
    r = 0.0f - a;
    g = 2.0943952f - a;
    b = 4.1887903f - a;
    while (r > 3.1415927f) {
        r -= 3.1415927f;
    }
    while (g > 3.1415927f) {
        g -= 3.1415927f;
    }
    while (b > 3.1415927f) {
        b -= 3.1415927f;
    }
    while (r < -3.1415927f) {
        r += 3.1415927f;
    }
    while (g < -3.1415927f) {
        g += 3.1415927f;
    }
    while (b < -3.1415927f) {
        b += 3.1415927f;
    }
    if (r > 2.0943952f) {
        r = 0.0f;
    }
    if (g > 2.0943952f) {
        g = 0.0f;
    }
    if (b > 2.0943952f) {
        b = 0.0f;
    }
    r = (2.0943952f - r) * 64.0f / 2.0943952f;
    g = (2.0943952f - g) * 64.0f / 2.0943952f;
    b = (2.0943952f - b) * 64.0f / 2.0943952f;
    d = _Sqrt(x * x + y * y);
    r += d - 64.0f;
    g += d - 64.0f;
    b += d - 64.0f;
    r *= 1.2f;
    g *= 1.2f;
    b *= 1.2f;
    if (r > 255.0f) {
        r = 255.0f;
    }
    if (g > 255.0f) {
        g = 255.0f;
    }
    if (b > 255.0f) {
        b = 255.0f;
    }
    if (r < 0.0f) {
        r = 0.0f;
    }
    if (g < 0.0f) {
        g = 0.0f;
    }
    if (b < 0.0f) {
        b = 0.0f;
    }
    col[0] = r;
    col[1] = g;
    col[2] = b;
}

/* A cursor vertex or the cursor's RGBA colour, one quadword either way. */
typedef union LtVec { /* field names derived */
    sceVu0FVECTOR f;
    int i[4];
} LtVec; /* derived name */

static void light_DrawCursor(float *dir, int mode)
{
    float m[4][4];
    LtVec sub;
    LtVec tip;
    LtVec base;
    LtVec left;
    LtVec right;
    int i;

    tip = (LtVec){{0.0f, 0.0f, -100.0f, 1.0f}};
    memset(&base, 0, 16);
    left = (LtVec){{10.0f, 0.0f, -25.0f, 1.0f}};
    right = (LtVec){{-10.0f, 0.0f, -25.0f, 1.0f}};
    base.f[3] = 1.0f;
    GetRootMatrix(m, CameraGetTarget());
    if (mode == 0) {
        LtVec col;

        cursorRotY = (128 - pad[1].ana[0]) * 32767 / 128;
        cursorRotX = (pad[1].ana[1] - 128) * 32767 / 128;
        _InitCurrentMatrix();
        _TransCurrentMatrix(m[3]);
        _RotCurrentMatrixX(cursorRotX);
        _RotCurrentMatrixY(cursorRotY);
        _ApplyCurrentMatrix(&tip, &tip);
        _ApplyCurrentMatrix(&left, &left);
        _ApplyCurrentMatrix(&right, &right);
        _ApplyCurrentMatrix(&base, &base);
        _SubVector(&sub, &tip, &base);
        _AddVector(&tip, &tip, &sub);
        _AddVector(&left, &left, &sub);
        _AddVector(&right, &right, &sub);
        _AddVector(&base, &base, &sub);
        dir[0] = dir[1] = dir[3] = 0.0f;
        dir[2] = 1.0f;
        _ApplyCurrentMatrix(dir, dir);
        _NormalizeVector(dir, dir);
        gif_StartPacketPri(11);
        col = (LtVec){.i = {255, 255, 255, 128}};
        MatrixDrive_PushMatrix();
        _UnitMatrix(MatrixDrive_GetMatrix());
        DrawLine(&tip, &base, &col, -1);
        DrawLine(&left, &base, &col, -1);
        DrawLine(&right, &base, &col, -1);
        MatrixDrive_PopMatrix();
        gif_EndPacket();
    }
    gif_StartPacketPri(11);
    {
        LtVec col;

        col = (LtVec){.i = {255, 255, 255, 128}};
        for (i = 0; i < 3; i++) {
            _ScaleVector(&tip, GlobalStageSetting.flatLightDir[i], -100.0f);
            _ScaleVector(&left, GlobalStageSetting.flatLightDir[i], -200.0f);
            _AddVectorXYZ(&tip, &tip, m[3]);
            _AddVectorXYZ(&left, &left, m[3]);
            MatrixDrive_PushMatrix();
            _UnitMatrix(MatrixDrive_GetMatrix());
            DrawLine(&tip, &left, &col, -1);
            MatrixDrive_PopMatrix();
        }
    }
    gif_EndPacket();
}

/* light_Tool below is the flat-light editor page of the debug menu.  Three
   pages selected by toolPage (colour, direction vector, ambient), each
   editing light toolLight with component toolItem (3 = all three at once).
   The retail build prints through debug_PrintfDummy.  The blink guard masks
   frame_count with an unsigned constant.  light_resetFlatLight is expanded at
   the tail. */
/* One idle flag per editor page
   (0 colour, 1 vector, 2 ambient): 1 while the page is only being shown, 0
   while the analog sticks are driving that page's values. */
static int pageIdle[3] = {1, 1, 1}; /* derived name */

static int toolPage = 0; /* derived name */ /* the editor page: 0 colour, 1 vector, 2 ambient */

static int toolItem = 0; /* derived name */ /* the selected component: 0 x/r, 1 y/g, 2 z/b, 3 all */

static int toolLight = 0; /* derived name */ /* the selected flat light: 0..2 */

int light_Tool(void)
{
    float dir[4];
    float col1[4];
    float col2[4];
    int ret = 0;
    int i;
    char *name[3] = {"r:", "g:", "b:"};
    unsigned int col;
    short rot;
    float (*c)[4];

    if (pad[0].flags & 0x4000) {
        if (++toolPage == 3) {
            toolPage = 0;
        }
    }
    if (pad[0].flags & 0x1000) {
        if (--toolPage == -1) {
            toolPage = 2;
        }
    }
    if (pad[0].flags & 0x1) {
        if (++toolLight == 3) {
            toolLight = 0;
        }
    }
    if (pad[0].flags & 0x100) {
        pageIdle[0] = pageIdle[1] = pageIdle[2] = 1;
        ret = -1;
    }
    if (pageIdle[1] == 0 && toolPage == 1) {
        light_DrawCursor(dir, 0);
    } else {
        light_DrawCursor(dir, 1);
    }
    switch (toolPage) {
    case 0:
        pageIdle[1] = pageIdle[2] = 1;
        light_GetColorAnalog(col1);
        if ((pad[0].flags & 0x400) && toolItem == 3) {
            pageIdle[0] ^= 1;
        }
        if (pageIdle[0] == 0 && toolItem == 3) {
            /* the row pointer is into flatLightDir; the three stores reach
               flatLightCol, 0x30 further on, off the same base */
            c = &GlobalStageSetting.flatLightDir[toolLight];
            c[3][0] = col1[0] / 128.0f;
            c[3][1] = col1[1] / 128.0f;
            c[3][2] = col1[2] / 128.0f;
            break;
        }
        if (pad[0].flags & 0x2000) {
            if (++toolItem == 4) {
                toolItem = 0;
            }
        }
        if (pad[0].flags & 0x8000) {
            if (--toolItem == -1) {
                toolItem = 3;
            }
        }
        if (toolItem != 3) {
            if (pad[0].rep & 0x20) {
                GlobalStageSetting.flatLightCol[toolLight][toolItem] += 0.01f;
            }
            if (pad[0].rep & 0x40) {
                GlobalStageSetting.flatLightCol[toolLight][toolItem] -= 0.01f;
            }
            break;
        }
        if (pad[0].rep & 0x20) {
            for (i = 0; i < 3; i++) {
                GlobalStageSetting.flatLightCol[toolLight][i] *= 1.01f;
            }
        }
        if (pad[0].rep & 0x40) {
            for (i = 0; i < 3; i++) {
                GlobalStageSetting.flatLightCol[toolLight][i] *= 0.99f;
            }
        }
        break;
    case 1:
        pageIdle[0] = pageIdle[2] = 1;
        if ((pad[0].flags & 0x400) && toolItem == 3) {
            pageIdle[1] ^= 1;
        }
        if (pageIdle[1] == 0 && toolItem == 3) {
            _CopyVector(GlobalStageSetting.flatLightDir[toolLight], dir);
            break;
        }
        if (pad[0].flags & 0x2000) {
            if (++toolItem == 4) {
                toolItem = 0;
            }
        }
        if (pad[0].flags & 0x8000) {
            if (--toolItem == -1) {
                toolItem = 3;
            }
        }
        if (toolItem == 3) {
            break;
        }
        if (pad[0].rep & 0x20) {
            rot = 1024;
        } else {
            rot = (pad[0].rep & 0x40) ? -1024 : 0;
        }
        _InitCurrentMatrix();
        switch (toolItem) {
        case 0:
            _RotCurrentMatrixX(rot);
            break;
        case 1:
            _RotCurrentMatrixY(rot);
            break;
        case 2:
            _RotCurrentMatrixZ(rot);
            break;
        }
        _ApplyCurrentMatrix(GlobalStageSetting.flatLightDir[toolLight],
                            GlobalStageSetting.flatLightDir[toolLight]);
        break;
    case 2:
        pageIdle[0] = pageIdle[1] = 1;
        light_GetColorAnalog(col2);
        if ((pad[0].flags & 0x400) && toolItem == 3) {
            pageIdle[2] ^= 1;
        }
        if (pageIdle[2] == 0 && toolItem == 3) {
            GlobalStageSetting.ambientCol[0] = col2[0] / 255.0f;
            GlobalStageSetting.ambientCol[1] = col2[1] / 255.0f;
            GlobalStageSetting.ambientCol[2] = col2[2] / 255.0f;
            break;
        }
        if (pad[0].flags & 0x2000) {
            if (++toolItem == 4) {
                toolItem = 0;
            }
        }
        if (pad[0].flags & 0x8000) {
            if (--toolItem == -1) {
                toolItem = 3;
            }
        }
        if (toolItem != 3) {
            if (pad[0].rep & 0x20) {
                GlobalStageSetting.ambientCol[toolItem] += 0.01f;
            }
            if (pad[0].rep & 0x40) {
                GlobalStageSetting.ambientCol[toolItem] -= 0.01f;
            }
            break;
        }
        if (pad[0].rep & 0x20) {
            for (i = 0; i < 3; i++) {
                GlobalStageSetting.ambientCol[i] *= 1.01f;
            }
        }
        if (pad[0].rep & 0x40) {
            for (i = 0; i < 3; i++) {
                GlobalStageSetting.ambientCol[i] *= 0.99f;
            }
        }
        break;
    }
    debug_PrintfDummy(10, 46, 0xFF800000, "PUSH R2 SELECT LIGHT (%d/3) ('SELECT'RETURN MENU)",
                      toolLight + 1);
    col = (toolPage == 0) ? 0xFFC0C000 : 0xFFFFFF00;
    if (pageIdle[0] != 0 || (frame_count & 0x1FU) < 20) {
        debug_PrintfDummy(10, 56, col, "COL ");
    }
    for (i = 0; i < 3; i++) {
        if (pageIdle[0] == 0 || toolPage != 0 || (toolItem != i && toolItem != 3) ||
            (frame_count & 0x1FU) < 20) {
            debug_PrintfDummy(46 + i * 168, 56, col, "%s%11f", name[i],
                              GlobalStageSetting.flatLightCol[toolLight][i] * 128.0f);
        }
    }
    col = (toolPage == 1) ? 0xFFC0C000 : 0xFFFFFF00;
    if (pageIdle[1] != 0 || (frame_count & 0x1FU) < 20) {
        debug_PrintfDummy(10, 66, col, "VEC ");
    }
    for (i = 0; i < 3; i++) {
        if (pageIdle[1] == 0 || toolPage != 1 || (toolItem != i && toolItem != 3) ||
            (frame_count & 0x1FU) < 20) {
            debug_PrintfDummy(46 + i * 168, 66, col, "%s%11f", name[i],
                              GlobalStageSetting.flatLightDir[toolLight][i]);
        }
    }
    col = (toolPage == 2) ? 0xFFC0C000 : 0xFFFFFF00;
    if (pageIdle[2] != 0 || (frame_count & 0x1FU) < 20) {
        debug_PrintfDummy(10, 76, col, "AMB ");
    }
    for (i = 0; i < 3; i++) {
        if (pageIdle[2] == 0 || toolPage != 2 || (toolItem != i && toolItem != 3) ||
            (frame_count & 0x1FU) < 20) {
            debug_PrintfDummy(46 + i * 168, 76, col, "%s%11f", name[i],
                              GlobalStageSetting.ambientCol[i] * 255.0f);
        }
    }
    light_resetFlatLight();
    return ret;
}

void light_InitLight(void)
{
    lastLight = 0;
    lastAmbient = 0;
    flatLightNum = 0;
}

void light_ResetLight(void) {}

void light_KillAllFixLight(void)
{
    Light *p = (Light *)lastLight;
    while (p != 0) {
        short v = p->kind;
        if (v < 4) {
            if (v >= 2) {
                Light *node = p;
                p = p->prev;
                light_killLinkLight(node);
                continue;
            }
        }
        p = p->prev;
    }
    lightCount = 0;
}

void light_KillAllAmbient(void)
{
    AmbientVolume *p = lastAmbient;
    while (p != 0) {
        int v = p->shape;
        if (v < 3) {
            if (v >= 0) {
                AmbientVolume *node = p;
                p = p->prev;
                light_killLinkAmbient(node);
                continue;
            }
        }
        p = p->prev;
    }
}

static inline void light_setLinkAmbient(AmbientVolume *p) /* derived name */
{
    if (lastAmbient != 0)
        lastAmbient->next = p;
    p->next = 0;
    p->prev = lastAmbient;
    lastAmbient = p;
}

AmbientVolume *light_AddAmbientObject(int obj)
{
    AmbientVolume *p;

    p = (AmbientVolume *)iosMallocDebug(ios_partition_seki,
                                        sizeof(AmbientVolume) > 160 ? sizeof(AmbientVolume) : 160,
                                        "src/Light.c", 723);
    p->shape = obj;
    p->lightScale = 1.0f;
    light_setLinkAmbient(p);
    return p;
}
