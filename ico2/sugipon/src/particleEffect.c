#include "ee_view.h"
#include "debug.h"
#include "DisplayList.h"
#include "Primitive.h"
#include "quaternion.h"
#include <libvu0.h>
#include <string.h>
#include "memory.h"
#include "typedef.h"
#include "ios.h"
#include "Matrix.h"
#include "main.h"
#include "matrixDrive.h"
#include "DmaPacket.h"
#include "windField.h"
#include "particleEffect.h"

#ifdef ICO_RD

#include <stdio.h>
#include "MicroCode.h"

#endif

static ICO_WORD setParticleEffect(struct PEGeo *self, struct PEPackage *pkg,
                                  struct IosMemPart *part);

/* one vertex of the particle primitive's buffers */
typedef struct PEVtx { /* field names derived */
    float pos[3];      /* 0x00 */
    float size;        /* 0x0C */
    float u;           /* 0x10 */
    float v;           /* 0x14 */
    float q;           /* 0x18 */
    float alpha;       /* 0x1C */
} PEVtx;               /* derived name */

typedef struct { /* field names derived */
    float v[4];
} PEVector; /* derived name */

typedef struct { /* field names derived */
    float v[4];
} PEQuaternion; /* derived name */

typedef struct {            /* field names derived */
    int used;               /* 0x00 */
    int pause;              /* 0x04 */
    int geoCtrl;            /* 0x08 */
    int sensing;            /* 0x0C */
    PEVector *sensPos;      /* 0x10 */
    PEQuaternion *sensQuat; /* 0x14 */
    PEGeo *geo;             /* 0x18 */
} PEffect;                  /* derived name */

/* 128 effect slots of 0x1C bytes, then 61 parameter records of 160 bytes
   (the PE160 pool the effect slots index into) */
static PEffect particleEffects[128]; /* derived name */

static int particleParams[61 * 40]; /* derived name */

/* the free-slot search, inlined at its one call site */
static inline int searchFreeParticleEffect(void) /* derived name */
{
    int i;

    for (i = 0; i < 128; i++) {
        if (particleEffects[i].used == 0) {
            if (particleEffects[i].geo != 0) {
                debug_StdPrintfDummy("PARTICLE EFFECT WRONG\n");
                for (;;) {}
            }
            return i;
        }
    }
    return -1;
}

static void setParticleEffectGeometry(PEGeo *geo, void *pos, void *quat)
{
    CopyVector(geo->pos, pos);
    CopyQuaternion(geo->quat, quat);
}

/* the staging record makeParticle fills before copying it whole into the
   caller's slot, the blank particle peSetVtx writes for an unused vertex, and
   the cleared effect slot InitParticleEffects fills the table with */
static PEPartRec particleWork = {
    1,                        /* alive */
    0,                        /* spin */
    0,                        /* pad08 */
    {0.0f, 0.0f, 0.0f, 1.0f}, /* pos */
    {0.0f, 0.0f, 0.0f, 0.0f}, /* vel */
    0,
    0,    /* spinX, spinY */
    1.0f, /* size */
    0.1f, /* sizeStep */
    1.0f, /* alpha */
    1.0f, /* alphaStep */
    60,   /* life */
    {0},
    {128, 128, 128, 128}, /* col */
    0.0f,
    0.0f, /* u, v */
}; /* derived name */

static PEPartRec blankParticle = {
    1,
    0,
    0,
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    0,
    0,
    0.0f,
    0.1f,
    1.0f,
    1.0f,
    60,
    {0},
    {0, 0, 0, 0},
    0.0f,
    0.0f,
}; /* derived name */

static PEffect emptyEffect = {0, 0, 1, 0, 0, 0, 0}; /* derived name */

/* The scratch vector the spread offset is written into and the current
   matrix is applied to; its definition follows the default package. */
static sceVu0FVECTOR spreadVector; /* derived name */

/* the 0..1 draw and the same draw mapped onto -1..1, as sugiCommon.h's
   random_unit and random_signed */
static inline float sugiRandom(void) /* derived name */
{
    return _GetRandom();
}

static inline float sugiSignedRandom(void) /* derived name */
{
    return sugiRandom() * 2.0f - 1.0f;
}

static void _setParticleEffect(PEPartRec *out, PEPackage *pkg, char *m, float k)
{
    PEPartRec *w;
    int n;
    float span;

    w = &particleWork;
    CopyVector(w->pos, m + 0x30);
    spreadVector[2] = pkg->speed * (pkg->speedRand * sugiSignedRandom() + 1.0f);
    CopyMatrix(MatrixDrive_GetMatrix(), m);
    if (pkg->spread != 0) {
        MatrixDrive_RotMatrixY((short)((float)pkg->spread * (sugiRandom() - 0.5f) * 182.04445f));
        MatrixDrive_RotMatrixX((short)((float)pkg->spread * (sugiRandom() - 0.5f) * 182.04445f));
    }
    sceVu0ApplyMatrix(w->vel, MatrixDrive_GetMatrix(), spreadVector);
    n = (int)((float)(unsigned int)pkg->emit * (pkg->emitRand * sugiSignedRandom() + 1.0f));
    w->life = (float)n * k;
    w->size = pkg->size * (pkg->sizeRand * sugiSignedRandom() + 1.0f);
    w->alpha = pkg->alpha * (pkg->alphaRand * sugiSignedRandom() + 1.0f) * k;
    w->sizeStep = pkg->sizeStep * (pkg->sizeStepRand * sugiSignedRandom() + 1.0f);
    if (pkg->spinY != 0) {
        w->spin = 1;
        w->spinX = (short)((float)pkg->spinX * (pkg->spinXRand * sugiSignedRandom() + 1.0f));
        w->spinY = (short)((float)pkg->spinY * (pkg->spinYRand * sugiSignedRandom() + 1.0f));
    } else {
        w->spin = 0;
    }
    span = (float)pkg->life * (pkg->lifeRand * sugiSignedRandom() + 1.0f);
#ifdef ICO_HOST
    /* span is 0 when a package's life and lifeRand make it so (stage 5's
       torches): the EE's div gives Fmax; the host's Inf times the zero
       below would be NaN (docs/port/DIVERGENCES.md) */
    w->alphaStep = ps2_div(w->alpha, span);
#else
    w->alphaStep = w->alpha / span;
#endif
    if ((float)w->life < span) {
        w->alpha = w->alpha - (span - (float)w->life) * w->alphaStep;
    }
    CopyIVector(w->col, &pkg->col);
    w->u = (float)pkg->u * 0.25f;
    w->v = (float)pkg->v * 0.25f;
    *out = particleWork;
}

/* Project the effect's origin through the current camera matrix and report
   whether the result falls outside the screen box. */
static inline int particleEffectOffScreen(PEGeo *geo) /* derived name */
{
    float v[4];

    if (geo->clip != 0) {
        sceVu0ApplyMatrix(v, matrixptr + 0x100, geo->pos);
#ifdef ICO_HOST
        /* PC port: w is 0 for an origin on the camera plane (seen at
           stage 7, Main tick 115 of a start_stage boot; DIVERGENCES.md F5) */
        sceVu0ScaleVectorXYZ(v, v, ps2_div(1.0f, v[3]));
#else
        sceVu0ScaleVectorXYZ(v, v, 1.0f / v[3]);
#endif
        if (v[2] < 0.0f || v[0] < 0.0f || 4095.0f < v[0] || v[1] < 0.0f || 4095.0f < v[1]) {
            return 1;
        }
    }
    return 0;
}

/* the vertex writer */
static inline void peSetVtx(PEVtx *dst, PEPartRec *pt) /* derived name */
{
    CopyVector(dst, pt->pos);
    dst->size = pt->size;
    dst->alpha = pt->alpha * 128.0f;
    dst->u = pt->u;
    dst->v = pt->v;
    dst->q = 128.0f;
}

/* The statements follow the developer's line order (287 self->pkg, 289,
   291, 292, 293, 295, 296, 299, 300, 303). */
static ICO_WORD setParticleEffect(PEGeo *self, PEPackage *pkg, struct IosMemPart *part)
{
    float m[16];
    PEPartRec *p;
    PEVtx *d0;
    PEVtx *d1;
    int i;
    int n;

    self->pkg = pkg;

    self->clip = 1;

    self->floorOn = pkg->floorOn;
    self->floor = (float)(-pkg->floorDepth);
    self->rate = 1.0f;

    n = pkg->count;
    self->n = n;
    self->emitted = 0.0f;

    self->proc = 0;
    self->endFunc = 0;

    self->prim = prim_InitParticleByPartition(n, 1.0f, 0.25f, 0.25f, 1, "enemy_tex01", 1, part);
    if (self->prim == 0)
        return 0;
    self->parts = iosMallocDebugNoAssert(part, self->n * sizeof(PEPartRec), __FILE__, 320);
    if (self->parts == 0) {
        prim_DeleteParticle(self->prim);
        return 0;
    }
    p = self->parts;
    d0 = self->prim->vtx;
    d1 = self->prim->vtxNext;
    GetMatrixFromQuaternionPos((char *)m, self->quat, self->pos);
    MatrixDrive_PushMatrix();
    for (i = 0; i < self->n; i++) {
        _setParticleEffect(p, self->pkg, (char *)m, 1.0f);
        peSetVtx(d0, p);
        peSetVtx(d1, p);
        p++;
        d0++;
        d1++;
    }
    MatrixDrive_PopMatrix();
    if (self->pkg->mode == 1) {
        for (i = 0; i < self->n; i++) {
            self->parts[i].life = (int)(_GetRandom() * (float)(unsigned int)self->pkg->emit);
        }
        self->emitted = (float)self->n;
    }
    return (ICO_WORD)self->parts;
}

/* the EE scratchpad holds the particle being updated */
#define PEWORK (*(PEPartRec *)ICO_SPR_ADDR(0)) /* derived name */

/* the per-particle integrator: 0 for a slot that is already dead, 1
   otherwise */
static inline int updateParticle(PEGeo *self, float *m) /* derived name */
{
    float wv[4];
    PEPackage *pkg;
    void *wind;

    pkg = self->pkg;
    if (PEWORK.alive == 0) {
        return 0;
    }
    wind = GetWindVector(0, PEWORK.pos);
    PEWORK.vel[0] = PEWORK.vel[0] + (sugiRandom() - 0.5f) * 0.2f;
    PEWORK.vel[1] = PEWORK.vel[1] + pkg->gravity;
    PEWORK.vel[2] = PEWORK.vel[2] + (sugiRandom() - 0.5f) * 0.2f;
    sceVu0ScaleVector(PEWORK.vel, PEWORK.vel, pkg->drag);
    sceVu0AddVector(PEWORK.pos, PEWORK.pos, PEWORK.vel);
    _ScaleVectorXYZ(wv, wind, pkg->wind);
    _AddVectorXYZ(PEWORK.pos, PEWORK.pos, wv);
    if (self->floorOn != 0) {
        if (self->floor > PEWORK.pos[1]) {
            PEWORK.pos[1] = self->floor;
            PEWORK.vel[1] = 0.0f;
        }
    }
    if (PEWORK.life < pkg->life) {
        PEWORK.alpha = PEWORK.alpha - PEWORK.alphaStep;
    }
    if (PEWORK.alpha < 0.0f) {
        PEWORK.alpha = 0.0f;
    }
    PEWORK.size = PEWORK.size + PEWORK.sizeStep;
    if (PEWORK.size < 0.0f) {
        PEWORK.size = 0.0f;
    }
    PEWORK.sizeStep = PEWORK.sizeStep * pkg->sizeStepDecay;
    if (PEWORK.spin != 0) {
        PEWORK.spinX = PEWORK.spinX + PEWORK.spinY;
        PEWORK.spinY = (short)((float)PEWORK.spinY * pkg->spinYDecay);
    }
    PEWORK.life = PEWORK.life - 1;
    if (PEWORK.life < 0) {
        if (pkg->mode == 1) {
            MatrixDrive_PushMatrix();
            _setParticleEffect(&PEWORK, pkg, (char *)m, self->rate);
            MatrixDrive_PopMatrix();
        } else {
            PEWORK.alive = 0;
        }
    }
    return 1;
}

static int execParticleEffect(PEGeo *self)
{
    float m[16];
    PEPartRec *part;
    PEPartRec *base;
    PEVtx *d0;
    PEVtx *v0;
    PEVtx *v1;
    int flags;
    int i;
    int n;
    float last;
    float total;
    float next;

    d0 = (PEVtx *)self->prim->vtx;
    flags = 0;
    if (particleEffectOffScreen(self)) {
        return self->pkg->mode == 1;
    }
    GetMatrixFromQuaternionPos(m, self->quat, self->pos);
    part = self->parts;
    for (i = 0; i < self->n; i++, part++, d0++) {
        if ((float)i < self->emitted) {
            PEWORK = *part;
            flags |= updateParticle(self, m);
            peSetVtx(d0, &PEWORK);
            *part = PEWORK;
        } else {
            peSetVtx(d0, &blankParticle);
            flags |= 1;
        }
    }
    total = (float)self->n;
    last = self->emitted;
    if (last < total) {
        v0 = self->prim->vtx;
        v1 = self->prim->vtxNext;
        base = self->parts;
        next = last + self->pkg->emitStep;
        n = (int)next;
        if (total < next) {
            n = (int)total;
        }
        for (i = (int)last; i < n; i++) {
            _setParticleEffect(&base[i], self->pkg, (char *)m, 1.0f);
            peSetVtx(&v0[i], &base[i]);
            peSetVtx(&v1[i], &base[i]);
        }
        self->emitted = next;
    }
    return flags;
}

/* The display-list packet builder state, the same record src/GifPacket.c
   carries: `ptr` is the write cursor and `dma`, `tail`, `gif` and `end` are
   the back-pointers the end-of-packet patch fills in once the size is known. */

/* One 64-bit slot of a DMA/GIF packet, written either whole or as one half. */

/* the GS-register writer, a file-static copy of gif_SetGsReg (the same
   construct src/GifPacket.c uses); the out-of-line copy is in src/GifPacket.c */
static inline void peSetGsReg(long long reg, long long data) /* derived name */
{
    *PacketBufferStruct.ptr.d++ = data;
    *PacketBufferStruct.ptr.d++ = reg;
}

static void dispParticleEffect(PEGeo *geo)
{
    char *c;
    char *p;
    char *q;

    if (particleEffectOffScreen(geo)) {
        return;
    }
    dl_SetDLPriority(6);
    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.end.c = 0;
    PacketBufferStruct.dma.c = c;
    PacketBufferStruct.tail.c = c;
    PacketBufferStruct.ptr.c = c + 8;
    ((GifPkWord *)(c + 8))->w[0] = 0x11000000;
    PacketBufferStruct.gif.c = c + 0xC;
    PacketBufferStruct.end.c = c + 0x10;
    PacketBufferStruct.ptr.c = c + 0x18;
    ((GifPkWord *)(c + 0x18))->d = 0xE;
    PacketBufferStruct.ptr.c = c + 0x20;
    switch (geo->pkg->alphaMode) {
    case 1:
        peSetGsReg(0x49, 0);
        peSetGsReg(0x42, 0x48);
        break;
    case 2:
        peSetGsReg(0x49, 0);
        peSetGsReg(0x42, 0x42);
        break;
    case 0:
    default:
        peSetGsReg(0x49, 0);
        peSetGsReg(0x42, 0x44);
        break;
    }
    ((GifPkWord *)PacketBufferStruct.end.c)->d =
        (unsigned int)(((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.end.c) >> 4) -
                       1) |
        0x1000000000008000LL;
    ((GifPkWord *)PacketBufferStruct.gif.c)->w[0] =
        (((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.gif.c) >> 4) << 16) |
        0x6C008000;
    p = PacketBufferStruct.ptr.c;
    ((GifPkWord *)p)->w[0] = 0x15000000;
    p += 4;
    PacketBufferStruct.ptr.d = (unsigned long long *)p;
    ((GifPkWord *)p)->w[0] = 0;
    PacketBufferStruct.ptr.c = p + 4;
    ((GifPkWord *)(p + 4))->w[0] = 0;
    PacketBufferStruct.ptr.c = p + 8;
    ((GifPkWord *)(p + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = p + 0xC;
    ((GifPkWord *)PacketBufferStruct.tail.c)->d =
        (unsigned int)((((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.tail.c) >>
                         4) -
                        1) |
                       0x10000000);
    q = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = q;
    ((GifPkWord *)q)->d = 0x60000000;
    PacketBufferStruct.ptr.c = q + 8;
    ((GifPkWord *)(q + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = q + 0xC;
    ((GifPkWord *)(q + 8))->w[1] = 0;
    PacketBufferStruct.ptr.c = q + 0x10;
    dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
    dl_CloseDma();
#ifdef ICO_RD
    /* PC port (wave 5, R5c; RENDER_API.md "Full-screen effects and the raw packet builders"): the
       packet is a VU1 SET_GSREGISTER packet (PABE 0 and the effect's ALPHA,
       mode 5, 6 or 4 by alphaMode); mc_HostDma hands its A+D pairs to the GS
       register decoder ahead of the batch prim_DispParticle chains (already
       read by Primitive.c's host path, R3ab) */
    mc_HostDma(5, PacketBufferStruct.dma.c, 0);
    {
        static int reported;

        if (!reported) {
            reported = 1;
            fprintf(stderr, "particleEffect: first effect drawn (alphaMode %u; reported once)\n",
                    geo->pkg->alphaMode);
        }
    }
#endif
    prim_DispParticle(geo->prim, matrixptr + 0x100);
}

int SetParticleEffectByPartition(int no, void *pos, void *quat, struct IosMemPart *part)
{
    int id;

    id = searchFreeParticleEffect();
    if (id < 0) {
        debug_StdPrintfDummy("No more effect... Ignored.\n");
        return -1;
    }
    particleEffects[id].used = 1;
    particleEffects[id].geoCtrl = 1;
    particleEffects[id].geo = (PEGeo *)iosMallocDebugNoAssert(part, sizeof(PEGeo), __FILE__, 501);
    particleEffects[id].sensing = 0;
    particleEffects[id].sensPos = 0;
    particleEffects[id].sensQuat = 0;
    if (particleEffects[id].geo != 0) {
        setParticleEffectGeometry(particleEffects[id].geo, pos, quat);
        if (setParticleEffect(particleEffects[id].geo,
                              (PEPackage *)((char *)particleParams + no * 160), part) == 0) {
            iosFree(particleEffects[id].geo);
            particleEffects[id].geo = 0;
            particleEffects[id].used = 0;
            id = -1;
        }
    } else {
        particleEffects[id].used = 0;
        id = -1;
    }
    return id;
}

/* free one effect's geometry; every deleter in this file inlines it */
static inline void deleteParticleEffectGeo(int no) /* derived name */
{
    prim_DeleteParticle(particleEffects[no].geo->prim);
    ICO_RAW(int, particleEffects[no].geo, 0x28, particleEffects[no].geo->prim) = 0;
    iosFree(particleEffects[no].geo->parts);
    iosFree(particleEffects[no].geo);
    particleEffects[no].geo = 0;
}

void SetParticleEffectGeometry(int id, void *pos, void *quat)
{
    if (id >= 0) {
        if (particleEffects[id].used == 0) {
            debug_StdPrintfDummy(
                "\033[36mError!!! Set geometry for release type particle.\033[m\n");
        } else {
            setParticleEffectGeometry(particleEffects[id].geo, pos, quat);
        }
    }
}

void SetParticleEffectUpperLimit(int no, float f)
{
    PEGeo *o;
    if (no >= 0) {
        o = particleEffects[no].geo;
        o->floorOn = 1;
        o->floor = f;
        execParticleEffect(o);
    }
}

/* the per-particle vector setup, shared by setParticleEffect,
 * execParticleEffect and ExecParticleEffect */
static inline void setParticleVector(PEVtx *d, PEPartRec *s) /* derived name */
{
    CopyVector(d, s->pos);
    d->size = s->size;
    d->alpha = s->alpha * 128.0f;
    d->u = s->u;
    d->v = s->v;
    d->q = 128.0f;
}

static inline void updateParticleVectors(int no) /* derived name */
{
    PEGeo *g;
    PEVtx *d;
    PEPartRec *s;
    int i;

    g = particleEffects[no].geo;
    d = (PEVtx *)g->prim->vtx;
    s = g->parts;
    for (i = 0; i < g->n; i++) {
        setParticleVector(d, s);
        s++;
        d++;
    }
}

void ExecParticleEffect(int no)
{
    int (*proc)(PEGeo *);

    if (particleEffects[no].used == 0) {
        return;
    }
    if (particleEffects[no].pause != 0) {
        return;
    }
    if (particleEffects[no].geoCtrl != 0) {
        if (particleEffects[no].sensing != 0) {
            SetParticleEffectGeometry(no, particleEffects[no].sensPos,
                                      particleEffects[no].sensQuat);
        }
        if (execParticleEffect(particleEffects[no].geo) == 0) {
            deleteParticleEffectGeo(no);
            particleEffects[no].used = 0;
        }
    } else {
        updateParticleVectors(no);
        proc = particleEffects[no].geo->proc;
        if (proc != 0) {
            if (proc(particleEffects[no].geo) == 0) {
                deleteParticleEffectGeo(no);
                particleEffects[no].used = 0;
            }
        }
    }
}

void ResetParticleEffectPackages(int *pkg)
{
    PEVector pos;
    PEQuaternion quat;
    struct IosMemPart *part;
    int i;

    part = ios_partition_oomori;
    for (i = 0; i < 128; i++) {
        if (particleEffects[i].used != 0 && particleEffects[i].geo->pkg == (PEPackage *)pkg) {
            CopyVector(&pos, particleEffects[i].geo);
            CopyQuaternion(&quat, (char *)particleEffects[i].geo + 0x10);
            deleteParticleEffectGeo(i);
            particleEffects[i].geo =
                (PEGeo *)iosMallocDebugNoAssert(part, sizeof(PEGeo), __FILE__, 663);
            particleEffects[i].used = 1;
            setParticleEffectGeometry(particleEffects[i].geo, &pos, &quat);
            setParticleEffect(particleEffects[i].geo, (PEPackage *)pkg, part);
        }
    }
}

/* the default package a file's record is laid over, then the spread
   vector */
static PEPackage defaultPackage = {
    11,                          /* version */
    1,                           /* mode */
    1,                           /* alphaMode */
    360,                         /* spread */
    {0},   2.0f,                 /* speed */
    0.1f,                        /* speedRand */
    0.95f,                       /* drag */
    -0.1f,                       /* gravity */
    0,                           /* spinY */
    {0},   0.1f,                 /* spinYRand */
    0.95f,                       /* spinYDecay */
    10.0f,                       /* size */
    0.1f,                        /* sizeRand */
    0.3f,                        /* sizeStep */
    0.01f,                       /* sizeStepRand */
    1.0f,                        /* sizeStepDecay */
    80,                          /* count */
    80,                          /* emit */
    0.1f,                        /* emitRand */
    1.0f,                        /* emitStep */
    0.2f,                        /* alpha */
    0.1f,                        /* alphaRand */
    80,                          /* life */
    0.1f,                        /* lifeRand */
    {0},   {128, 128, 128, 128}, /* col */
    0,                           /* u */
    0,                           /* v */
    0,                           /* spinX */
    {0},   0.1f,                 /* spinXRand */
    1.0f,                        /* wind */
    0,                           /* floorOn */
    0,                           /* floorDepth */
}; /* derived name */

static sceVu0FVECTOR spreadVector = {0.0f, 0.0f, 1.0f, 0.0f}; /* derived name */

void SetParticleEffectPackage(int no, int *data, int size)
{
    *(PEPackage *)((unsigned char *)particleParams + no * 160) = defaultPackage;
    if (*(int *)&defaultPackage != *data) {
        debug_StdPrintfDummy("\033[36mThis is old version(%d) file. May be an error occur.\033[m\n",
                             *data);
    }
#ifdef ICO_HOST
    /* a .pef longer than its 160-byte slot would run into the next package */
    if (size > 160) {
        size = 160;
    }
#endif
    memcpy(((unsigned char *)particleParams + no * 160), data, size);
}

void InitParticleEffects(void)
{
    int i;

    for (i = 0; i < 128; i++) {
        particleEffects[i] = emptyEffect;
    }
}

void ExecParticleEffects(void)
{
    int i;
    for (i = 0; i < 128; i++) {
        ExecParticleEffect(i);
    }
}

void DispParticleEffects(void)
{
    int i;

    for (i = 0; i < 128; i++) {
        if (particleEffects[i].used != 0) {
            dispParticleEffect(particleEffects[i].geo);
        }
    }
}

void DeleteParticleEffect(int no)
{
    if (particleEffects[no].used != 0 || particleEffects[no].geo != 0) {
        deleteParticleEffectGeo(no);
        particleEffects[no].used = 0;
    }
}

void SetParticleEffectPauseFlag(int id, int pause)
{
    particleEffects[id].pause = pause;
}

/* the body of SetParticleEffect, which SetParticleEffectActiveSensing
 * inlines and SetParticleEffect calls */
static inline int SetParticleEffect_inl(int no, void *pos, void *quat) /* derived name */
{
    return SetParticleEffectByPartition(no, pos, quat, ios_partition_oomori);
}

int SetParticleEffect(int no, void *pos, void *quat)
{
    return SetParticleEffect_inl(no, pos, quat);
}

int SetParticleEffectActiveSensing(int no, void *pos, void *quat)
{
    int id;

    id = SetParticleEffect_inl(no, pos, quat);
    if (id != -1) {
        particleEffects[id].sensing = 1;
        particleEffects[id].sensPos = pos;
        particleEffects[id].sensQuat = quat;
    }
    return id;
}

/* the bodies of GetParticleEffectPackage and DeleteParticleEffectsByPackage,
 * which DeleteParticleEffectsByID inlines and the two exported functions
 * call */
static inline int *GetParticleEffectPackage_inl(int idx) /* derived name */
{
    return (int *)((char *)particleParams + idx * 160);
}

static inline void DeleteParticleEffectsByPackage_inl(int *pkg) /* derived name */
{
    int i;

    for (i = 0; i < 128; i++) {
        if (particleEffects[i].used != 0 && particleEffects[i].geo->pkg == (PEPackage *)pkg) {
            deleteParticleEffectGeo(i);
            particleEffects[i].used = 0;
        }
    }
}

int *GetParticleEffectPackage(int idx)
{
    return GetParticleEffectPackage_inl(idx);
}

void DeleteParticleEffectsByPackage(int *pkg)
{
    DeleteParticleEffectsByPackage_inl(pkg);
}

void DeleteParticleEffectsByID(int id)
{
    DeleteParticleEffectsByPackage_inl(GetParticleEffectPackage_inl(id));
}

PEGeo *GetParticleEffectData(int id)
{
    return particleEffects[id].geo;
}

void DisableParticleEffectGeometryControl(int id)
{
    particleEffects[id].geoCtrl = 0;
}

int GetParticleIDWithName(char *name)
{
    int i;
    for (i = 0; i < 61; i++) {
        if (strcmp(particleEffectFile[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int GetParticleLoopFlag(int id)
{
    int *p;
    if (id < 0) {
        return -1;
    }
    p = (int *)((char *)particleParams + id * 160);
    return p[1] == 1;
}

void ParticleEffects_SetAllGoal(void *goal)
{
    int i;

    for (i = 0; i < 128; i++) {
        if (particleEffects[i].used != 0) {
            PEGeo *v = particleEffects[i].geo;
            if (v != 0) {
                sceVu0CopyVector(v->goal, goal);
            }
        }
    }
}

void SetParticleEffectClipEnableFlag(int id, int on)
{
    if (id >= 0) {
        particleEffects[id].geo->clip = on;
    }
}

void SetParticleEffectDrainLevel(int id, float level)
{
    if (id >= 0) {
        particleEffects[id].geo->rate = level;
    }
}
