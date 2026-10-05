#include "debug.h"
#include "Basic.h"
#include "DisplayP2O.h"
#include <stdio.h>
#include "debug_exception.h"
#include "Matrix.h"
#include "charFileManager.h"
#include <assert.h>

#ifdef ICO_HOST

#include "Light.h"
#include "eeword.h"
#include <stdlib.h>
#include <string.h>

#endif

/* a 16-byte aligned float[4], the shape of libvu0's sceVu0FVECTOR */
typedef float Vec[4] __attribute__((aligned(16))); /* derived name */

typedef struct PktHdr { /* field names derived */
    char pad0[240];
    int kind; /* 0xF0 */
} PktHdr;     /* derived name */

#ifdef ICO_HOST

/* The host has one definition of each record (docs/port/LOADERS.md): the
 * display object MakePacket allocates is typedef.h's Sub15C, a part is
 * DisplayP2O.h's PObjPart (decoded from the file's ObjRec, below), and the
 * model is DisplayP2O.h's PObjModel, which this file's PObj view must match
 * member for member (checked after the definition). */
typedef Sub15C PObjPkt;

typedef union PObjSub { /* a PObjPart, with the vertex list typed as vectors */
    PObjPart part;

    struct {
        char pad0[__builtin_offsetof(PObjPart, vtx)];
        Vec *vtx;
        unsigned int vtxCount;
    };
} PObjSub;

_Static_assert(__builtin_offsetof(PObjSub, vtx) == __builtin_offsetof(PObjPart, vtx) &&
                   __builtin_offsetof(PObjSub, vtxCount) ==
                       __builtin_offsetof(PObjPart, vtxCount) &&
                   sizeof(PObjSub) == sizeof(PObjPart),
               "PObjSub is a view of PObjPart");

#else

typedef struct PObjPkt { /* field names derived */
    char pad0[2132];
    struct PObj *owner; /* 0x854 */
    char pad858[24];
    void *nodes;      /* 0x870, one 80-byte node record a part */
    PktHdr *lightMtx; /* 0x874, the light matrices (Light.h), their mode at 0xF0 */
} PObjPkt;            /* derived name */

typedef struct PObjSub { /* field names derived */ /* 0x180 stride, hung off the PObj at 0x40 */
    long long pad0[18];
    Vec *vtx;              /* 0x90 */
    unsigned int vtxCount; /* 0x94 */
    long long pad98[(0x180 - 0x98) / 8];
} PObjSub; /* derived name */

#endif

typedef struct PObj { /* field names derived */
    char pad0[36];
    int image;    /* 0x24, the model file image (ObjHdr); an EE word on the host */
    PObjPkt *pkt; /* 0x28 */
    short spare;  /* 0x2C, cleared when the model is set up; nothing reads it */
    /* 0x2E and 0x2F, the part count and the cluster count (the file's objnum
       and clstnum), as one short's two bitfields */
    short partCount : 8;        /* 0x2E */
    unsigned short clstNum : 8; /* 0x2F */

    union {
        long long ll;

        struct {
            short nloop; /* 0x30 */
            short bits;  /* 0x32, the display type, shade and level-of-detail bits tag.ll sets */
            float lightScale; /* 0x34 */
        } v;
    } tag; /* 0x30 */

    float ambientScale; /* 0x38 */
    float shadowLength; /* 0x3C */
    PObjSub *sub;       /* 0x40 */
    Vec (*boxes)[8];    /* 0x44 */
#ifdef ICO_HOST
    PObjGroup *groups; /* 0x48, PObjModel's */
    char pad4C[4];
    float bb[8][4] __attribute__((aligned(16))); /* 0x50 */
#else
    char pad48[8];
    Vec bb[8]; /* 0x50 */
#endif
} PObj; /* derived name */

#ifdef ICO_HOST
#define POBJ_SAME(a, b) (__builtin_offsetof(PObj, a) == __builtin_offsetof(PObjModel, b))

_Static_assert(POBJ_SAME(image, pad24) && POBJ_SAME(pkt, dobj) && POBJ_SAME(spare, spare) &&
                   POBJ_SAME(tag, mode) && POBJ_SAME(ambientScale, ambientScale) &&
                   POBJ_SAME(shadowLength, shadowLength) && POBJ_SAME(sub, parts) &&
                   POBJ_SAME(boxes, boxes) && POBJ_SAME(groups, groups) && POBJ_SAME(bb, box) &&
                   sizeof(PObj) == sizeof(PObjModel),
               "PObj is a view of PObjModel");

#undef POBJ_SAME

/* the sizes MakePacket and AllocPObj wrote as numbers are these records'
   sizes on the EE, and so on a 32-bit host */
_Static_assert(sizeof(void *) != 4 ||
                   (sizeof(PObjModel) == 0xD0 && sizeof(PObjPart) == 0x180 &&
                    ((sizeof(Sub15C) + 15) & ~15) == 0x880 && sizeof(LightMatrix) == 0x100 &&
                    sizeof(struct DObjNode) == 80),
               "the EE record sizes on a 32-bit host");

#endif

/* the file's own name tidier, inlined once, into AllocPObj */
static __inline__ void TidyPObjName(char *name) /* derived name */
{
    char buf[256];
    int i;
    int top;

    top = 0;
    for (i = 0;; i++) {
        if (name[i] == 0)
            break;
        if (name[i] == '/')
            top = i + 1;
    }
    sprintf(buf, "%s", &name[top]);
    sprintf(name, "%s", buf);
    for (i = 0; name[i] != 0; i++) {
        if (name[i] == '.') {
            name[i] = 0;
            break;
        }
    }
}

/* MakeBoundingBox: one axis-aligned box per sub-object into the block it
   allocates at boxes, plus the whole object's box in self->bb. */
static void MakeBoundingBox(PObj *self)
{
    Vec mn;
    Vec mx;
    Vec gmn;
    Vec gmx;
    Vec *out;
    float *p;
    PObjSub *sub;
    int i;
    unsigned int j;
    int l;

    self->boxes = (Vec(*)[8])mallocseki(self->partCount << 7);

    gmn[0] = gmn[1] = gmn[2] = 16777215.0f;
    gmx[0] = gmx[1] = gmx[2] = -16777215.0f;

    for (i = 0; i < self->partCount; i++) {
        sub = &self->sub[i];
        out = self->boxes[i];

        mn[0] = mn[1] = mn[2] = 16777215.0f;
        mx[0] = mx[1] = mx[2] = -16777215.0f;

        for (j = 0; j < sub->vtxCount; j++) {
            p = sub->vtx[j];
            if (p[0] < mn[0])
                mn[0] = p[0];
            if (mx[0] < p[0])
                mx[0] = p[0];
            if (p[1] < mn[1])
                mn[1] = p[1];
            if (mx[1] < p[1])
                mx[1] = p[1];
            if (p[2] < mn[2])
                mn[2] = p[2];
            if (mx[2] < p[2])
                mx[2] = p[2];
            if (p[0] < gmn[0])
                gmn[0] = p[0];
            if (gmx[0] < p[0])
                gmx[0] = p[0];
            if (p[1] < gmn[1])
                gmn[1] = p[1];
            if (gmx[1] < p[1])
                gmx[1] = p[1];
            if (p[2] < gmn[2])
                gmn[2] = p[2];
            if (gmx[2] < p[2])
                gmx[2] = p[2];
        }

        for (l = 0; l < 8; l++) {
            if (l & 1)
                out[l][0] = mn[0];
            else
                out[l][0] = mx[0];
            if (l & 2)
                out[l][1] = mn[1];
            else
                out[l][1] = mx[1];
            if (l & 4)
                out[l][2] = mn[2];
            else
                out[l][2] = mx[2];
            out[l][3] = 1.0f;
        }
    }

    for (l = 0; l < 8; l++) {
        if (l & 1)
            self->bb[l][0] = gmn[0];
        else
            self->bb[l][0] = gmx[0];
        if (l & 2)
            self->bb[l][1] = gmn[1];
        else
            self->bb[l][1] = gmx[1];
        if (l & 4)
            self->bb[l][2] = gmn[2];
        else
            self->bb[l][2] = gmx[2];
        self->bb[l][3] = 1.0f;
    }
}

#ifdef ICO_HOST

/* the EE's MakePacket with the display object, its light matrices and node
   records allocated at their host sizes (0x880, 0x100 and 80 a part on the
   EE) and filled through Sub15C's own names */
static void MakePacket(PObj *p, int n)
{
    Sub15C *q;

    p->tag.v.nloop = n;
    p->tag.ll = (p->tag.ll & ~0x3C0000LL) | ((long long)modelData[n].shade << 18);
    p->tag.ll = (p->tag.ll & ~0x3C00000LL) | ((long long)modelData[n].lod << 22);
    p->tag.v.lightScale = modelData[n].lightScale;
    p->ambientScale = modelData[n].ambientScale;

    q = (Sub15C *)mallocseki(sizeof(Sub15C));
    p->pkt = q;
    q->lightMtx = (LightMatrix *)mallocseki(sizeof(LightMatrix));
    q->lightMtx->mode = modelData[n].pktKind;

    q->model = (PObjModel *)p;
    q->nodes = mallocseki(p->partCount * sizeof(struct DObjNode));
    if (q->lightMtx->mode != 4) {
        if (p->image != 0)
            p2o_MakePacket(q);
    }
    debug_StdPrintfDummy("end of packet making...\n");
}

#else

static void MakePacket(PObj *p, int n)
{
    PObjPkt *q;

    p->tag.v.nloop = n;
    p->tag.ll = (p->tag.ll & ~0x3C0000LL) | ((long long)modelData[n].shade << 18);
    p->tag.ll = (p->tag.ll & ~0x3C00000LL) | ((long long)modelData[n].lod << 22);
    p->tag.v.lightScale = modelData[n].lightScale;
    p->ambientScale = modelData[n].ambientScale;

    q = (PObjPkt *)mallocseki(0x880);
    p->pkt = q;
    q->lightMtx = (PktHdr *)mallocseki(0x100);
    q->lightMtx->kind = modelData[n].pktKind;

    q->owner = p;
    q->nodes = mallocseki(p->partCount * 80);
    if (q->lightMtx->kind != 4) {
        if (p->image != 0)
            p2o_MakePacket(q);
    }
    debug_StdPrintfDummy("end of packet making...\n");
}

#endif

/* the file's own vector setter, inlined into InitPObj */
static __inline__ void SetPObjVector(Vec v, float x, float y, float z) /* derived name */
{
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 0.0f;
}

/* ObjHdr, ObjEnt and ObjRec, the file records, are in DisplayP2O.h. */

#ifdef ICO_HOST

/* The host's AllocPObj (docs/port/LOADERS.md, p2o).  It relocates the file
 * image exactly as the EE does, every relocated word an EE word (eeword.h),
 * so the image's bytes are the EE's on a 32-bit host.  Each part record is
 * then decoded into the model's PObjPart (a runtime record with real
 * pointers) instead of copied whole.  The strip and morph tables are tables
 * of words in the image; the display walks them as pointer tables
 * (Packet.c, RegistPacket.c), so they are decoded into pointer tables too.
 * The EE leaves them in the image, which the loader frees once the model is
 * built; the host keeps them in one block that charFileManager.c frees at the
 * same point (PObj_FreeImageTables). */
static void **pobjImageTables; /* port: the decoded tables of the last model loaded */

void PObj_FreeImageTables(void)
{
    free(pobjImageTables);
    pobjImageTables = 0;
}

/* a table of n EE words as n pointers, at *out; advances *out */
static void **DecodeWordTable(ICO_EEWORD(char *) tbl, unsigned int n, void ***out)
{
    void **dst = *out;
    IcoEEWord *src = ICO_EEPTR(IcoEEWord *, tbl);
    unsigned int i;

    for (i = 0; i < n; i++)
        dst[i] = ICO_EEPTR(void *, src[i]);
    *out = dst + n;
    return dst;
}

static void DecodePObjPart(PObjPart *d, ObjRec *s, void ***tables)
{
    memcpy(d->pad00, s, sizeof(d->pad00)); /* 0x00-0x8F, the magic at 0x80 */
    d->vtx = ICO_EEPTR(char *, s->vtx);
    d->vtxCount = s->vtxCount;
    memcpy(d->pad98, s->pad98, sizeof(d->pad98));
    d->nrm = ICO_EEPTR(char *, s->nrm);
    d->nrmCount = s->nrmCount;
    memcpy(d->padA8, s->padA8, sizeof(d->padA8));
    d->uv = ICO_EEPTR(char *, s->uv);
    memcpy(d->padB4, s->padB4, sizeof(d->padB4));
    d->col = ICO_EEPTR(char *, s->col);
    memcpy(d->padC4, s->padC4, sizeof(d->padC4));
    d->mats = ICO_EEPTR(PObjMatDef *, s->mats);
    d->matCount = s->matCount;
    memcpy(d->padD8, s->padD8, sizeof(d->padD8));
    d->texDefs = ICO_EEPTR(PObjTexDef *, s->texDefs);
    d->texCount = s->texCount;
    memcpy(d->padE8, s->padE8, sizeof(d->padE8));
    d->polys = ICO_EEPTR(void *, s->polys);
    d->polyCount = s->polyCount;
    memcpy(d->padF8, s->padF8, sizeof(d->padF8));
    d->strips = DecodeWordTable(s->strips, s->stripCount, tables);
    d->stripCount = s->stripCount;
    memcpy(d->pad108, s->pad108, sizeof(d->pad108));
    d->lines = ICO_EEPTR(struct PObjLine *, s->lines);
    d->lineCount = s->lineCount;
    memcpy(d->pad118, s->pad118, sizeof(d->pad118));
    d->morphs = (PObjMorph **)DecodeWordTable(s->morphs, s->morphCount, tables);
    d->morphCount = s->morphCount;
    memcpy(d->pad128, s->pad128, sizeof(d->pad128));
    memcpy(d->mtx, s->mtx, sizeof(d->mtx));
    memcpy(d->pad170, s->pad170, sizeof(d->pad170));
    d->vtxSave = ICO_EEPTR(char *, s->vtxSave);
    d->nrmSave = ICO_EEPTR(char *, s->nrmSave);
    memcpy(d->pad17C, s->pad17C, sizeof(d->pad17C));
}

/* the sub-record table allocate and decode */
static void AllocPObjSubs(PObj *p, int *list)
{
    unsigned int total = 0;
    void **tables;
    int i;

    p->sub = (PObjSub *)mallocseki(p->partCount * sizeof(PObjSub));

    for (i = 0; i < p->partCount; i++) {
        ObjRec *o = ICO_EEPTR(ObjRec *, list[i]);

        total += o->stripCount + o->morphCount;
    }
    PObj_FreeImageTables();
    pobjImageTables = tables = (void **)malloc((total ? total : 1) * sizeof(void *));
    if (tables == 0) {
        __builtin_trap();
    }
    for (i = 0; i < p->partCount; i++)
        DecodePObjPart(&p->sub[i].part, ICO_EEPTR(ObjRec *, list[i]), &tables);
}

static void InitPObjHeader(PObj *p, ObjHdr *h, int n)
{
    p->image = ICO_EEW(h);
    p->pkt = 0;
    p->spare = 0;
    p->partCount = h->objNum;
    p->clstNum = h->clstNum;
    p->tag.ll &= ~0x30000LL;
    p->tag.ll &= ~0x4000000LL;
    p->shadowLength = modelData[n].shadowLength;
}

PObj *AllocPObj(ObjHdr *h, char *name, int n)
{
    PObj *p;
    int *tex;
    int *list;
    ObjRec *o;
    IcoEEWord *w;
    unsigned int i;
    unsigned int j;

    h->objTbl += ICO_EEW(h);

    if (h->texTbl != 0)
        h->texTbl += ICO_EEW(h);
    tex = ICO_EEPTR(int *, h->texTbl);

    list = ICO_EEPTR(int *, h->objTbl);

    debug_StdPrintfDummy("\033[33mobject info : adrs(%p) objnum(%d) clstnum(%d)\n", h, h->objNum,
                         h->clstNum);

    p = (PObj *)mallocseki(sizeof(PObjModel));
    sprintf((char *)p, "%s", name);
    TidyPObjName((char *)p);

    debug_StdPrintfDummy("            : object name (%s)\n", (char *)p);
    debug_StdPrintfDummy("            : object table (%p)\033\n", list);
    if (tex != 0)
        debug_StdPrintfDummy("            : texture table (%p)\033[m\n", tex);
    else
        debug_StdPrintfDummy("\033[m");

    if (tex != 0) {
        for (i = 0; i < h->texNum; i++)
            tex[i] += ICO_EEW(h);
    }

    debug_StdPrintfDummy("Solve object address. %p\n", list);

    for (i = 0; i < h->objNum; i++) {
        list[i] += ICO_EEW(h);
        o = ICO_EEPTR(ObjRec *, list[i]);

        if (o->magic != *(int *)"OBJH") {
            debug_StdPrintfDummy("allocPObj:Invalid Object.\n");
            debug_assert(__FILE__, 249);
            __assert(__FILE__, 249, "FALSE");
        }

        o->vtx += ICO_EEW(h);
        o->nrm += ICO_EEW(h);
        o->uv += ICO_EEW(h);
        o->col += ICO_EEW(h);
        o->mats += ICO_EEW(h);
        o->texDefs += ICO_EEW(h);
        o->polys += ICO_EEW(h);
        for (j = 0; j < o->polyCount; j++)
            ICO_EEPTR(ObjEnt *, o->polys)[j].p += ICO_EEW(h);
        o->strips += ICO_EEW(h);
        w = ICO_EEPTR(IcoEEWord *, o->strips);
        for (j = 0; j < o->stripCount; j++)
            w[j] += ICO_EEW(h);
        o->lines += ICO_EEW(h);
        o->morphs += ICO_EEW(h);
        w = ICO_EEPTR(IcoEEWord *, o->morphs);
        for (j = 0; j < o->morphCount; j++) {
            if (w[j] != 0)
                w[j] += ICO_EEW(h);
        }
    }

    InitPObjHeader(p, h, n);
    AllocPObjSubs(p, list);

    MakeBoundingBox(p);

    return p;
}

#else

/* the sub-record table allocate and copy, inlined once */
static __inline__ void AllocPObjSubs(PObj *p, int *list) /* derived name */
{
    int i;

    p->sub = (PObjSub *)mallocseki(p->partCount * sizeof(PObjSub));

    for (i = 0; i < p->partCount; i++)
        p->sub[i] = *(PObjSub *)list[i];
}

/* the header fields of a freshly allocated PObj */
static __inline__ void InitPObjHeader(PObj *p, ObjHdr *h, int n) /* derived name */
{
    p->image = (int)h;
    p->pkt = 0;
    p->spare = 0;
    p->partCount = h->objNum;
    p->clstNum = h->clstNum;
    p->tag.ll &= ~0x30000LL;
    p->tag.ll &= ~0x4000000LL;
    p->shadowLength = modelData[n].shadowLength;
}

PObj *AllocPObj(ObjHdr *h, char *name, int n)
{
    PObj *p;
    int *tex;
    int *list;
    ObjRec *o;
    unsigned int i;
    unsigned int j;

    h->objTbl += (int)h;

    if (h->texTbl != 0)
        h->texTbl += (int)h;
    tex = (int *)h->texTbl;

    list = (int *)h->objTbl;

    debug_StdPrintfDummy("\033[33mobject info : adrs(%p) objnum(%d) clstnum(%d)\n", h, h->objNum,
                         h->clstNum);

    p = (PObj *)mallocseki(0xD0);
    sprintf((char *)p, "%s", name);
    TidyPObjName((char *)p);

    debug_StdPrintfDummy("            : object name (%s)\n", (char *)p);
    debug_StdPrintfDummy("            : object table (%p)\033\n", list);
    if (tex != 0)
        debug_StdPrintfDummy("            : texture table (%p)\033[m\n", tex);
    else
        debug_StdPrintfDummy("\033[m");

    if (tex != 0) {
        for (i = 0; i < h->texNum; i++)
            tex[i] += (int)h;
    }

    debug_StdPrintfDummy("Solve object address. %p\n", list);

    for (i = 0; i < h->objNum; i++) {
        list[i] += (int)h;
        o = (ObjRec *)list[i];

        if (o->magic != *(int *)"OBJH") {
            debug_StdPrintfDummy("allocPObj:Invalid Object.\n");
            debug_assert(__FILE__, 249);
            __assert(__FILE__, 249, "FALSE");
        }

        o->vtx += (int)h;
        o->nrm += (int)h;
        o->uv += (int)h;
        o->col += (int)h;
        o->mats += (int)h;
        o->texDefs += (int)h;
        o->polys += (int)h;
        for (j = 0; j < o->polyCount; j++)
            ((ObjEnt *)o->polys)[j].p = (void *)((int)((ObjEnt *)o->polys)[j].p + (int)h);
        o->strips += (int)h;
        for (j = 0; j < o->stripCount; j++)
            ((void **)o->strips)[j] = (void *)((int)((void **)o->strips)[j] + (int)h);
        o->lines += (int)h;
        o->morphs += (int)h;
        for (j = 0; j < o->morphCount; j++) {
            if (((void **)o->morphs)[j] != 0)
                ((void **)o->morphs)[j] = (void *)((int)((void **)o->morphs)[j] + (int)h);
        }
    }

    InitPObjHeader(p, h, n);
    AllocPObjSubs(p, list);

    MakeBoundingBox(p);

    return p;
}

#endif

PObj *InitPObj(ICO_WORD h, ICO_WORD name, int n)
{
    PObj *p;
    Vec v;
    int num;
    int i;
    int j;

    p = AllocPObj((ObjHdr *)h, (char *)name, n);
    SetPObjVector(v, modelData[n].offset[0], modelData[n].offset[1], modelData[n].offset[2]);
    num = p->partCount;
    for (i = 0; i < num; i++) {
        PObjSub *g = &p->sub[i];
        int cnt = g->vtxCount;

        for (j = 0; j < cnt; j++)
            _AddVector(g->vtx[j], g->vtx[j], v);
        for (j = 0; j < 8; j++)
            _AddVector(p->boxes[i][j], p->boxes[i][j], v);
    }
    MakePacket(p, n);
    return p;
}

/* The DEBUG build's report of the model file image FreePObj releases, in
   AllocPObj's terms; built only under DEBUG (the text derived). */
static __inline__ void FreePObjDebugInfo(ObjHdr *h) /* derived name */
{
#ifdef DEBUG
    debug_StdPrintfDummy("            : adrs(%p)\n", h);
#endif
}

void FreePObj(PObj *p)
{
    debug_StdPrintfDummy("free object\n");
    FreePObjDebugInfo(ICO_EEPTR(ObjHdr *, p->image));
}
