#include "typedef.h"
#include "Packet.h"
#include "RegistPacket.h"
#include "DisplayP2O.h"
#include "debug.h"
#include "DisplayList.h"
#include "GsBase.h"
#include "Light.h"
#include "Shadow.h"
#include "Texture.h"
#include "lineManager.h"
#include "matrixDrive.h"
#include "main.h"
#include "debug_exception.h"
#include "GifPacket.h"
#include "Matrix.h"
#include "DmaPacket.h"
#include "ee_view.h"
#include <assert.h>
#include "MicroCode.h"

#ifdef ICO_RD

#include <string.h>
#include "GifHost.h"
#include "rd_mesh.h"
#include "modelpack.h"

/* ===================================================================== *
 * PC port (renderer wave 3, R3ab).
 *
 * Every DMA this file chains for VU1 (matrix, light, material, texture,
 * dissolve, specular and reflection packets, the point and line packets)
 * also goes through the host's VIF reader (mc_HostDma, MicroCode.c), which
 * updates the list's VU state (rd_mesh.h) and sends the SET_GSREGISTER
 * payloads to the GS register decoder, in list order.  The packets of a
 * model's vertex batches (pk->data) are drawn instead: regHostMesh records
 * rd_DrawVuMesh of the packet's mesh (Packet.c) with the VU state of the
 * list at that point, after the batches' GIF tag PRIM went to the decoder.
 * Game logic, culling (reg_clipPacketBoundingBox, gsb_ClipBox), the list
 * choices and the packets themselves are unchanged.
 * ===================================================================== */
static inline void regHostDma(int id, void *addr, int qwc)
{
    dl_OpenDma(id, addr, qwc);
    mc_HostDma(id, addr, qwc);
}

#define dl_OpenDma(id, addr, qwc) regHostDma((id), (void *)(addr), (qwc))

/* R7d: a mesh draw's RdKey is
 * the object, the part and the packet's place in the part's chain with the
 * pass (0 the material, 1 the specular, 2 the reflection pass), so the same
 * draw has the same key in every frame.  The packet pointer alone is not:
 * a morphing part draws grp->packets and grp->morph in alternate frames
 * (buffer_ID), and objects that share a model share its packets.  The
 * functions that walk a part's chain name it (regKeyPart) before the walk;
 * a packet in neither of that group's chains keeps R7b's key (packet, list,
 * MSCAL code). */
static Sub15C *regKeyObj;

static PObjGroup *regKeyGrp;

static int regKeyIdx;

static void regKeyPart(Sub15C *o, PObjGroup *grp, int part)
{
    regKeyObj = o;
    regKeyGrp = grp;
    regKeyIdx = part;
}

static int regKeyOrdinal(PacHeader *pk)
{
    PacHeader *q;
    int n;

    if (regKeyGrp == 0) {
        return -1;
    }
    for (n = 0, q = regKeyGrp->packets; q != 0; q = q->next, n++) {
        if (q == pk) {
            return n;
        }
    }
    for (n = 0, q = regKeyGrp->morph; q != 0; q = q->next, n++) {
        if (q == pk) {
            return n;
        }
    }
    return -1;
}

/* v0.5.0 (M4): the part a packet drawn now belongs to (Packet.h
   PacHostIdent), from the walk's names; a packet outside the named part's
   chains (n < 0) has no name, bone count or object */
static void regHostIdent(PacHostIdent *id, int n)
{
    memset(id, 0, sizeof(*id));
    id->part = -1;
    id->ordinal = -1;
    if (n < 0 || regKeyObj == 0) {
        return;
    }
    id->model = regKeyObj->model != 0 ? regKeyObj->model->name : regKeyGrp->name;
    id->part = regKeyIdx;
    id->ordinal = n;
    id->bones = regKeyObj->nodeNum;
    id->obj = regKeyObj;
}

/* the skeleton of the object drawn (regKeyObj) for the dump of a skinned
   part: bone i is node i, its inverse bind clusterMtx[i], its parent the
   skeleton node's (-1 past the skeleton's records); NULL for an object
   without cluster matrices */
static const ModelpackSkeleton *regHostSkeleton(void)
{
    static ModelpackSkeleton sk;
    const Sub15C *o = regKeyObj;
    int i;

    if (o == 0 || o->clusterMtx == 0 || o->nodeNum <= 0 || o->nodeNum > 60) {
        return 0;
    }
    memset(&sk, 0, sizeof(sk));
    sk.count = (uint32_t)o->nodeNum;
    for (i = 0; i < o->nodeNum; i++) {
        memcpy(sk.invBind[i], o->clusterMtx + i * 64, 64);
        sk.parent[i] = o->skel != 0 && i < o->skelNodeNum ? o->skel[i].parent : -1;
    }
    return &sk;
}

static void regHostMesh(PacHeader *pk, int pass)
{
    unsigned long long prim[2];
    unsigned long long tag;
    PacHostIdent id;
    RdVuDraw d;
    RdMesh m;
    int n;

    (dl_OpenDma)(2, pk->data, pk->size >> 4); /* the chain as the PS2 has it */
    n = regKeyOrdinal(pk);
    regHostIdent(&id, n);
    m.id = pac_HostMeshFor(pk, &id);
    if (m.id == 0) {
        return;
    }
    /* v0.5.0 (M4): the model pack's dump, at every draw (meshes are made
       lazily, and the one-shot dump wants each part of its object drawn) */
    if (modelpack_DumpWanted(rd_VuMeshHash(m), id.obj)) {
        id.skel = regHostSkeleton();
        pac_HostDump(pk, &id);
    }
    /* the PRIM every batch's GIF tag (PRE) writes: strip, IIP, TME, ABE */
    memcpy(&tag, pk->data + 0x10, 8);
    prim[0] = (tag >> 47) & 0x7FF;
    prim[1] = 0;
    gif_HostWriteRegs(prim, 1);
    if (rd_VuDrawFromState(&d)) {
        rd_DrawVuMesh(m, &d,
                      n >= 0 ? RD_KEY(regKeyObj, regKeyIdx, n * 4 + pass)
                             : RD_KEY(pk, rd_CurrentList(), d.code));
    }
}

#endif

/* the scissor switch reg_SetScissorSw sets and reg_Init clears */
static int scissorSw = 0; /* derived name */

/* one sixteen-byte record of a part's strip list as reg_setShape reads it
 * (Shadow.c's ShadowRun is the silhouette's view of the same list).  A strip
 * opens with a head record, its vertex count (-1 ends the list) and the
 * strip's offset in its material's packet data; the vertex records that
 * follow hold the vertex and normal indices, and the first of them the
 * strip's material and texture slot. */
typedef struct RegStripHead { /* field names derived */
    short count;
    short pad2;
    int ofs;
    char pad8[8];
} RegStripHead; /* derived name */

typedef struct RegStripVtx { /* field names derived */
    short flag;
    short pad2;
    short vtx;
    short nrm;
    char pad8[4];
    short mat;
    short texSlot;
} RegStripVtx; /* derived name */

static void reg_setShape(Sub15C *o, int idx, int flag, PacHeader *pkt, PObjMaterial *mat)
{
    float vec[4];
    PObjModel *mdl;
    PObjPart *s;
    char *p;
    PObjMorph *m;
    char *v;
    PacHeader *pk;
    char *t;
    ICO_WORD base;
    int i;
    int n;

    mdl = o->model;
    s = &mdl->parts[idx];
    {
        char *dst = s->vtx;

        for (i = 0; i < s->vtxCount; i++) {
            _CopyVector(dst + i * 0x10, s->vtxSave + i * 0x10);
        }
    }
    {
        char *dst = s->nrm;

        if (dst != 0) {
            for (i = 0; i < s->nrmCount; i++) {
                _CopyVector(dst + i * 0x10, s->nrmSave + i * 0x10);
            }
        }
    }
    for (i = 0; i < s->morphCount; i++) {
        if (o->morphWeight[i] != 0.0f) {
            m = s->morphs[i];
            if (m != 0) {
                while (m->index != -1) {
                    _ScaleVectorXYZ(vec, m->delta, o->morphWeight[i]);
                    if (m->isVertex == 1.0f) {
                        if (m->index >= s->vtxCount) {
                            debug_StdPrintfDummy("reg_setShape:illegal vertex index. %d/%d\n",
                                                 m->index, s->vtxCount);
                            debug_assert("src/RegistPacket.c", 635);
                            __assert("src/RegistPacket.c", 635, "0");
                        }
                        t = s->vtx;
                        t += m->index * 0x10;
                        _AddVectorXYZ(t, t, vec);
                    } else if (m->isVertex == 0.0f) {
                        if ((((int)(mat->attr.bits >> 5)) & 3) == 0) {
                            switch (mat->attr.word & 1) {
                            case 1:
                                break;
                            default:
                                goto nextbone;
                            }
                        }
                        if (m->index >= s->nrmCount) {
                            debug_StdPrintfDummy("reg_setShape:illegal normal index. %d/%d\n",
                                                 m->index, s->nrmCount);
                            debug_assert("src/RegistPacket.c", 642);
                            __assert("src/RegistPacket.c", 642, "0");
                        }
                        t = s->nrm;
                        t += m->index * 0x10;
                        _AddVectorXYZ(t, t, vec);
                    } else {
                        debug_assert("src/RegistPacket.c", 647);
                        __assert("src/RegistPacket.c", 647, "0");
                    }
                nextbone:
                    m++;
                }
            }
        }
    }
    for (i = 0; i < s->stripCount; i++) {
        v = ((char **)s->strips)[i];
        while (((RegStripHead *)v)->count != -1) {
            pk = pkt;
            n = ((RegStripHead *)v)->count;
            v += 0x10;
            while (pk != 0) {
                if (((RegStripVtx *)v)->texSlot == pk->texSlot &&
                    ((RegStripVtx *)v)->mat == pk->mat) {
                    break;
                }
                pk = pk->next;
            }
            if (pk == 0) {
                break;
            }
            base = (ICO_WORD)pk->data;
            p = (char *)(((RegStripHead *)(v - 0x10))->ofs + base);
            if (((RegStripHead *)(v - 0x10))->ofs != 0) {
                if (n != 0) {
                    do {
                        _CopyVector(p, s->vtx + ((RegStripVtx *)v)->vtx * 0x10);
                        p += 0x10;
                        if ((((int)(mat->attr.bits >> 5)) & 3) == 0) {
                            switch (mat->attr.word & 1) {
                            case 1:
                                break;
                            default:
                                goto nocopy;
                            }
                        }
                        _CopyVector(p, s->nrm + ((RegStripVtx *)v)->nrm * 0x10);
                        p += 0x10;
                    nocopy:
                        if (mdl->disp != 0) {
                            p += 0x10;
                        }
                        n--;
                        v += 0x10;
                        p += 0x20;
                    } while (n != 0);
                }
            }
        }
    }
#ifdef ICO_RD
    /* R3ab: the vertices were rewritten in the packets: the meshes follow
       (v0.5.0, M4: named, for a model pack's replacement that cannot) */
    for (n = 0, pk = pkt; pk != 0; pk = pk->next, n++) {
        PacHostIdent id;

        memset(&id, 0, sizeof(id));
        id.model = o->model != 0 ? o->model->name : 0;
        id.part = idx;
        id.ordinal = n;
        id.bones = o->nodeNum;
        id.obj = o;
        pac_HostRefreshFor(pk, &id);
    }
#endif
}

typedef union { /* field names derived */
    sceVu0IVECTOR c;
    unsigned long long w[2];
} RegColor; /* derived name */

typedef struct { /* field names derived */
    int e[12][2];
} RegBoxLines; /* derived name */

static void reg_dispBoxLine(PacHeader *pk)
{
    RegColor col;
    RegBoxLines line;
    int i;

    if (pk == 0) {
        return;
    }
    _SetCurrentMatrix(matrixptr + 0x40);
    gif_StartPacketPri(11);
    col = (RegColor){{255, 255, 255, 80}};
    line = (RegBoxLines){{{0, 1},
                          {1, 3},
                          {3, 2},
                          {2, 0},
                          {4, 5},
                          {5, 7},
                          {7, 6},
                          {6, 4},
                          {0, 4},
                          {1, 5},
                          {2, 6},
                          {3, 7}}};
    gif_SetAlpha(1, 4, 0x20);
    _CopyMatrix(MatrixDrive_GetMatrix(), matrixptr + 0x40);
    for (i = 0; i < 12; i++) {
        DrawLine(pk->box[line.e[i][0]], pk->box[line.e[i][1]], col.c, 0);
    }
    gif_EndPacket();
}

static int reg_clipPacketBoundingBox(PacHeader *pk)
{
    int ret = 1;
    int type;

    _SetCurrentMatrix(matrixptr + 0x300);

    type = pk->clip;
    switch (type) {
    case 0:
        ret = -1;
        break;
    case 1:
        ret = gsb_ClipBox(pk->box[0]);
        if (ret == 2) {
            ret = 1;
        }
        break;
    case 2:
        ret = gsb_ClipBox(pk->box[0]);
        break;
    case 3:
        ret = gsb_ClipBox(pk->box[0]);
        if (ret == 1) {
            ret = 2;
        }
        break;
    default:
        debug_StdPrintfDummy("illegal clip type. %d\n", type);
        debug_assert("src/RegistPacket.c", 821);
        __assert("src/RegistPacket.c", 821, "0");
        break;
    }
    if (debug_bounding_flag & 2) {
        reg_dispBoxLine(pk);
    }
    return ret;
}

/* The PS2 calls below pass one argument and leave the second in the
   register reg_transMicroCode received its mask in, so the program goes to
   the lists of mask; on the host the mask is passed (MicroCode.h). */
#define REG_MC_MASK(mask) , (mask)

static void reg_transMicroCode(Sub15C *o, int mask)
{
    if (o->model->disp != 0) {
        mc_TransMicroCode(3 REG_MC_MASK(mask));
        return;
    }
    if (o->lightMtx->mode == 0) {
        mc_TransMicroCode(1 REG_MC_MASK(mask));
        return;
    }
    mc_TransMicroCode(2 REG_MC_MASK(mask));
}

static void reg_chooseMicroCode(PObjMaterial *self, int clip, int pri)
{
    long long v_ll = self->attr.bits;
    int v_int = self->attr.word;
    mc_SetMicroCode(v_int & 1, ((int)(v_ll >> 5)) & 3, 0, clip, pri);
}

static void reg_chooseSpecularMicroCode(int mode, int clip, int pri)
{
    mc_SetMicroCode(mode, 1, 1, clip, pri);
}

static void reg_chooseReflectionMicroCode(int mode, int clip, int pri)
{
    mc_SetMicroCode(mode, 1, 2, clip, pri);
}

/* the quadword copy type src/Primitive.c and src/Shadow.c use */
typedef ICO_QW Qw128; /* derived name */

/* PacketBufferStruct (DmaPacket.h): every packet address (dma, ptr, tail,
 * gif, end) is one pointer union, read and written through its members. */

static void reg_setNMatrixPacket_setMatrix(void)
{
    char *c;
    char *m;

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = c;
    ((GifPkWord *)c)->d = 0x1000000D;
    PacketBufferStruct.ptr.c = c + 8;
    ((GifPkWord *)(c + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = c + 0xC;
    PacketBufferStruct.gif.c = c + 0xC;
    ((GifPkWord *)(c + 8))->w[1] = 0x6C0C8000;
    PacketBufferStruct.ptr.c = c + 0x50;
    _CopyMatrix(c + 0x10, matrixptr + 0x140);
    _MulMatrix(PacketBufferStruct.ptr.c, matrixptr + 0x200, matrixptr + 0x40);
    PacketBufferStruct.ptr.c = PacketBufferStruct.ptr.c + 0x40;
    _MulMatrix(PacketBufferStruct.ptr.c, matrixptr + 0x80, matrixptr + 0x40);
    m = PacketBufferStruct.ptr.c;
    PacketBufferStruct.ptr.c = m + 0x40;
    ((GifPkWord *)(m + 0x40))->w[0] = 0x15000010;
    PacketBufferStruct.ptr.c = m + 0x44;
    ((GifPkWord *)(m + 0x40))->w[1] = 0;
    PacketBufferStruct.ptr.c = m + 0x48;
    ((GifPkWord *)(m + 0x48))->d = 0;
    PacketBufferStruct.ptr.c = m + 0x50;
}

static void reg_setNMatrixPacket_setLight(Sub15C *o)
{
    char *c;
    char *m;
    char *n;

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = c;
    ((GifPkWord *)c)->d = 0x10000009;
    PacketBufferStruct.ptr.c = c + 8;
    ((GifPkWord *)(c + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = c + 0xC;
    PacketBufferStruct.gif.c = c + 0xC;
    ((GifPkWord *)(c + 8))->w[1] = 0x6C088000;
    PacketBufferStruct.ptr.c = c + 0x10;
    _GetCurrentMatrix(c + 0x10);
    m = PacketBufferStruct.ptr.c;
    PacketBufferStruct.ptr.c = m + 0x80;
    _CopyMatrix(m + 0x40, (char *)o->lightMtx + 64);
    n = PacketBufferStruct.ptr.c;
    ((GifPkWord *)n)->w[0] = 0x15000012;
    n += 4;
    PacketBufferStruct.ptr.c = n;
    ((GifPkWord *)n)->w[0] = 0;
    PacketBufferStruct.ptr.c = n + 4;
    ((GifPkWord *)(n + 4))->d = 0;
    PacketBufferStruct.ptr.c = n + 0xC;
}

static char *reg_setNMatrixPacket(Sub15C *o, int idx)
{
    char *pkt;
    float *box;
    struct DObjNode *scl;
    int mode;

    scl = (struct DObjNode *)(idx * 80 + (ICO_WORD)o->nodes);
    mode = o->lightMtx->mode;
    if (scl->scale[0] != 1.0f || scl->scale[1] != 1.0f || scl->scale[2] != 1.0f) {
        _InitCurrentMatrix();
        _SetCurrentMatrix((char *)o->nodeMtx + idx * 64);
        _ScaleCurrentMatrix(o->nodes[idx].scale[0], o->nodes[idx].scale[1], o->nodes[idx].scale[2]);
        _GetCurrentMatrix(matrixptr + 0x40);
    } else {
        _CopyMatrix(matrixptr + 0x40, (char *)o->nodeMtx + idx * 64);
    }
    _MulMatrix(matrixptr + 0x300, matrixptr + 0x280, matrixptr + 0x40);
    _MulMatrix(matrixptr + 0x140, matrixptr + 0x100, matrixptr + 0x40);
    box = o->model->box[0];
    _SetCurrentMatrix(matrixptr + 0x300);
    if (gsb_ClipBox(box) == 0) {
        if (o->shadow != 0) {
            light_MakeLightMatrix(o, idx);
        }
        return 0;
    }
    pkt = PacketBufferStruct.ptr.c;
    PacketBufferStruct.dma.c = pkt;
    PacketBufferStruct.tail.c = 0;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.end.c = 0;
    reg_setNMatrixPacket_setMatrix();
    if (mode != 0 && mode != 3) {
        light_MakeLightMatrix(o, idx);
        _SetCurrentMatrix(matrixptr + 0x40);
        _ClearTransCurrentMatrix();
        _MulCurrentMatrixL((char *)o->lightMtx);
        reg_setNMatrixPacket_setLight(o);
    }
    {
        char *c = PacketBufferStruct.ptr.c;

        PacketBufferStruct.tail.c = c;
        ((GifPkWord *)c)->d = 0x60000000;
        PacketBufferStruct.ptr.c = c + 8;
        ((GifPkWord *)(c + 8))->w[0] = 0;
        PacketBufferStruct.ptr.c = c + 0xC;
        ((GifPkWord *)(c + 8))->w[1] = 0;
        PacketBufferStruct.ptr.c = c + 0x10;
    }
    return pkt;
}

typedef struct { /* field names derived */
    float x;
    float y;
    float z;
    float w;
} RegVec; /* derived name */

typedef struct { /* field names derived */
    RegVec r[4];
} RegMtx; /* derived name */

static void reg_setMMatrixPacket_setMatrix(Sub15C *o, int idx)
{
    char *c;
    char *m;

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = c;
    ((GifPkWord *)c)->d = 0x1000000D;
    PacketBufferStruct.ptr.c = c + 8;
    ((GifPkWord *)(c + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = c + 0xC;
    PacketBufferStruct.gif.c = c + 0xC;
    ((GifPkWord *)(c + 8))->w[1] = 0x6C0C8000;
    PacketBufferStruct.ptr.c = c + 0x50;
    _CopyMatrix(c + 0x10, matrixptr + 0x140);
    if ((o->nodes[idx].flags.ll & 6) != 0) {
        _MulMatrix(PacketBufferStruct.ptr.c, matrixptr + 0x1C0, matrixptr + 0x180);
    } else {
        _MulMatrix(matrixptr + 0x180, matrixptr + 0x80, matrixptr + 0x40);
        _MulMatrix(PacketBufferStruct.ptr.c, matrixptr + 0x200, matrixptr + 0x40);
    }
    PacketBufferStruct.ptr.c = PacketBufferStruct.ptr.c + 0x40;
    _MulMatrix(PacketBufferStruct.ptr.c, matrixptr + 0x80, matrixptr + 0x40);
    m = PacketBufferStruct.ptr.c;
    PacketBufferStruct.ptr.c = m + 0x40;
    ((GifPkWord *)(m + 0x40))->w[0] = 0x15000010;
    PacketBufferStruct.ptr.c = m + 0x44;
    ((GifPkWord *)(m + 0x40))->w[1] = 0;
    PacketBufferStruct.ptr.c = m + 0x48;
    ((GifPkWord *)(m + 0x48))->d = 0;
    PacketBufferStruct.ptr.c = m + 0x50;
}

static void reg_setMMatrixPacket_setLight(Sub15C *o)
{
    char *c;
    char *m;
    char *n;

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = c;
    ((GifPkWord *)c)->d = 0x10000009;
    PacketBufferStruct.ptr.c = c + 8;
    ((GifPkWord *)(c + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = c + 0xC;
    PacketBufferStruct.gif.c = c + 0xC;
    ((GifPkWord *)(c + 8))->w[1] = 0x6C088000;
    PacketBufferStruct.ptr.c = c + 0x10;
    _GetCurrentMatrix(c + 0x10);
    m = PacketBufferStruct.ptr.c;
    PacketBufferStruct.ptr.c = m + 0x80;
    _CopyMatrix(m + 0x40, (char *)o->lightMtx + 64);
    n = PacketBufferStruct.ptr.c;
    ((GifPkWord *)n)->w[0] = 0x15000012;
    n += 4;
    PacketBufferStruct.ptr.c = n;
    ((GifPkWord *)n)->w[0] = 0;
    PacketBufferStruct.ptr.c = n + 4;
    ((GifPkWord *)(n + 4))->d = 0;
    PacketBufferStruct.ptr.c = n + 0xC;
}

static char *reg_setMMatrixPacket(Sub15C *o, int idx)
{
    RegVec s;
    RegVec v;
    char *pkt;
    float *box;
    struct DObjNode *w;
    int mode;

    w = (struct DObjNode *)(idx * 80 + (ICO_WORD)o->nodes);
    mode = o->lightMtx->mode;
    if ((w->flags.ll & 2) != 0) {
        RegMtx um;

        _SetCurrentMatrix((char *)o->nodeMtx + idx * 64);
        _ClearTransCurrentMatrix();
        _UnitMatrix(&um);
        _ApplyCurrentMatrix(&v, &um.r[0]);
        s.x = _GetLength(&v, &um.r[3]);
        _ApplyCurrentMatrix(&v, &um.r[1]);
        s.y = _GetLength(&v, &um.r[3]);
        _ApplyCurrentMatrix(&v, &um.r[2]);
        s.z = _GetLength(&v, &um.r[3]);
        _InitCurrentMatrix();
        if (o->nodes[idx].pos[2] < 5.0f) {
            _ScaleVectorXYZ(&s, &s, 5.0f);
            _ScaleCurrentMatrix(s.x, s.y, s.z);
            _ScaleVectorXYZ(&s, o->nodes[idx].pos, 5.0f);
            s.w = 1.0f;
            _SetTransCurrentMatrix(&s);
        } else {
            _ScaleCurrentMatrix(s.x, s.y, s.z);
            _SetTransCurrentMatrix(o->nodes[idx].pos);
        }
        _GetCurrentMatrix(matrixptr + 0x180);
        _MulMatrix(matrixptr + 0x140, matrixptr + 0x640, matrixptr + 0x180);
        _MulMatrix(matrixptr + 0x300, matrixptr + 0x680, matrixptr + 0x180);
    } else if ((w->flags.ll & 4) != 0) {
        RegMtx um2;

        _MulMatrix(matrixptr + 0x180, matrixptr + 0x80, (char *)o->nodeMtx + idx * 64);
        _UnitMatrix(&um2);
        _SetCurrentMatrix(matrixptr + 0x180);
        _ClearTransCurrentMatrix();
        _ApplyCurrentMatrix(&v, &um2.r[0]);
        s.x = _GetLength(&v, &um2.r[3]);
        _ApplyCurrentMatrix(&v, &um2.r[1]);
        s.y = _GetLength(&v, &um2.r[3]);
        _ApplyCurrentMatrix(&v, &um2.r[2]);
        s.z = _GetLength(&v, &um2.r[3]);
        _InitCurrentMatrix();
        _TransCurrentMatrix(matrixptr + 0x1B0);
        _RotCurrentMatrixZ(*(short *)((char *)&o->nodes[idx] + 58));
        _ScaleCurrentMatrix(s.x, s.y, s.z);
        _GetCurrentMatrix(matrixptr + 0x180);
        _MulMatrix(matrixptr + 0x140, matrixptr + 0xC0, matrixptr + 0x180);
        _MulMatrix(matrixptr + 0x300, matrixptr + 0x240, matrixptr + 0x180);
    } else {
        if (w->scale[0] != 1.0f || w->scale[1] != 1.0f || w->scale[2] != 1.0f) {
            _InitCurrentMatrix();
            _SetCurrentMatrix((char *)o->nodeMtx + idx * 64);
            _ScaleCurrentMatrix(o->nodes[idx].scale[0], o->nodes[idx].scale[1],
                                o->nodes[idx].scale[2]);
            _GetCurrentMatrix(matrixptr + 0x40);
        } else {
            _CopyMatrix(matrixptr + 0x40, (char *)o->nodeMtx + idx * 64);
        }
        _MulMatrix(matrixptr + 0x140, matrixptr + 0x100, matrixptr + 0x40);
        _MulMatrix(matrixptr + 0x300, matrixptr + 0x280, matrixptr + 0x40);
    }
    box = o->model->box[0];
    _SetCurrentMatrix(matrixptr + 0x300);
    if (gsb_ClipBox(box) == 0) {
        if (o->shadow != 0) {
            light_MakeLightMatrix(o, idx);
        }
        return 0;
    }
    pkt = PacketBufferStruct.ptr.c;
    PacketBufferStruct.dma.c = pkt;
    PacketBufferStruct.tail.c = 0;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.end.c = 0;
    reg_setMMatrixPacket_setMatrix(o, idx);
    if (mode != 0 && mode != 3) {
        light_MakeLightMatrix(o, idx);
        _SetCurrentMatrix(matrixptr + 0x40);
        _ClearTransCurrentMatrix();
        _MulCurrentMatrixL((char *)o->lightMtx);
        reg_setMMatrixPacket_setLight(o);
    }
    {
        char *c = PacketBufferStruct.ptr.c;

        PacketBufferStruct.tail.c = c;
        ((GifPkWord *)c)->d = 0x60000000;
        PacketBufferStruct.ptr.c = c + 8;
        ((GifPkWord *)(c + 8))->w[0] = 0;
        PacketBufferStruct.ptr.c = c + 0xC;
        ((GifPkWord *)(c + 8))->w[1] = 0;
        PacketBufferStruct.ptr.c = c + 0x10;
    }
    return pkt;
}

/* the head of a cluster matrix packet: the DMA tag with FLUSH and the
   UNPACK, then the first quadword unpacked, the unpack's quadword count and
   the alpha the microcode fades the cluster by.  The two pad words are
   stored as plain words: stored through pad14, reg_setCMatrixPacket's
   schedule moves (measured). */
typedef struct { /* field names derived */
    DpkTag dma;
    int qwc;
    int pad14[2];
    float alpha;
} RegClusterHead; /* derived name */

static inline void reg_setCMatrixPacket_pack(Sub15C *o, float alpha)
{
    char *c;
    char *m;
    int n;
    int i;

    n = o->nodeNum * 4;
    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = c;
    ((RegClusterHead *)c)->dma.tag = n | 0x10000002;
    PacketBufferStruct.ptr.c = c + 8;
    ((RegClusterHead *)c)->dma.vif[0] = 0x11000000;
    PacketBufferStruct.ptr.c = c + 0xC;
    PacketBufferStruct.gif.c = c + 0xC;
    ((RegClusterHead *)c)->dma.vif[1] = ((n + 1) << 16) | 0x6C008000;
    PacketBufferStruct.ptr.c = c + 0x10;
    ((RegClusterHead *)c)->qwc = n + 1;
    PacketBufferStruct.ptr.c = c + 0x14;
    *(int *)(c + 0x14) = 0;
    PacketBufferStruct.ptr.c = c + 0x18;
    *(int *)(c + 0x18) = 0;
    PacketBufferStruct.ptr.c = c + 0x1C;
    ((RegClusterHead *)c)->alpha = alpha;
    PacketBufferStruct.ptr.c = c + 0x20;
    for (i = 0; i < o->nodeNum; i++) {
        _MulMatrix(PacketBufferStruct.ptr.c, (char *)o->nodeMtx + i * 64, o->clusterMtx + i * 64);
        PacketBufferStruct.ptr.c = PacketBufferStruct.ptr.c + 0x40;
    }
    m = PacketBufferStruct.ptr.c;
    *(int *)m = 0x15000010;
    m += 4;
    PacketBufferStruct.ptr.c = m;
    *(int *)m = 0;
    PacketBufferStruct.ptr.c = m + 4;
    *(long long *)(m + 4) = 0;
    PacketBufferStruct.ptr.c = m + 0xC;
}

static inline void reg_setCMatrixPacket_light(Sub15C *o)
{
    char *c;
    char *n;

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = c;
    *(long long *)c = 0x10000009;
    PacketBufferStruct.ptr.c = c + 8;
    *(int *)PacketBufferStruct.ptr.c = 0;
    PacketBufferStruct.ptr.c = c + 0xC;
    PacketBufferStruct.gif.c = c + 0xC;
    *(int *)PacketBufferStruct.gif.c = 0x6C088000;
    /* the two light matrices go in through the cursor post-increment
     * src/Primitive.c's setLight uses; the c + 0x10 store is overwritten
     * by the first increment's store */
    PacketBufferStruct.ptr.c = c + 0x10;
    _CopyMatrix(ICO_POSTINC(float (*)[16], PacketBufferStruct.ptr.c), (char *)o->lightMtx);
    _CopyMatrix(ICO_POSTINC(float (*)[16], PacketBufferStruct.ptr.c), (char *)o->lightMtx + 64);
    n = PacketBufferStruct.ptr.c;
    *(int *)n = 0x15000012;
    n += 4;
    PacketBufferStruct.ptr.c = n;
    *(int *)n = 0;
    PacketBufferStruct.ptr.c = n + 4;
    *(long long *)(n + 4) = 0;
    PacketBufferStruct.ptr.c = n + 0xC;
}

static void reg_setCMatrixPacket(Sub15C *o, float alpha, int prilist)
{
    int i;
    int haslight;

    haslight = o->lightMtx->mode != 0;
    light_MakeLightMatrix(o, 0);
    PacketBufferStruct.dma.c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = 0;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.end.c = 0;
    reg_setCMatrixPacket_pack(o, alpha);
    if (haslight) {
        reg_setCMatrixPacket_light(o);
    } else {
        debug_StdPrintfDummy("no light calc cluster model %s\n", o->model);
        debug_assert("src/RegistPacket.c", 1238);
        __assert("src/RegistPacket.c", 1238, "0");
    }
    {
        char *c = PacketBufferStruct.ptr.c;

        PacketBufferStruct.tail.c = c;
        ((DpkTag *)c)->tag = 0x60000000;
        PacketBufferStruct.ptr.c = c + 8;
        ((DpkTag *)c)->vif[0] = 0;
        PacketBufferStruct.ptr.c = c + 0xC;
        ((DpkTag *)c)->vif[1] = 0;
        PacketBufferStruct.ptr.c = c + 0x10;
    }
    for (i = 0; i < 13; i++) {
        if ((prilist >> i) & 1) {
            dl_SetDLPriority(i);
            dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
            dl_CloseDma();
        }
    }
}

/* The GS state the specular pass draws in: a VIF DIRECT of three qwords, a
   GIF A+D tag with PABE and ALPHA_1, then the VIF MSCNT; qword aligned because
   dl_OpenDma chains it into the display list as a DMA source. */
static const unsigned int regSpecularPacket[5][4] __attribute__((aligned(16))) = {
    /* derived name */
    {0, 0, 0, 0x6C038000}, {0x8002, 0x10000000, 0xE, 0}, {0, 0, 0x49, 0},
    {0x48, 0x80, 0x42, 0}, {0x15000000, 0, 0, 0},
};

/* The specular pass, a file static all six reg_disp* functions tail-call. */
static void reg_dispSpecular(PacHeader *pkt, int clip, int mode) /* derived name */
{
    short h;
    dl_SetDLPriority(4);
    h = pkt->tex1;
    if (h >= 0) {
        texturetranssize += tex_TransTexture(h, 4);
    }
    dl_OpenDma(2, regSpecularPacket, 5);
    dl_CloseDma();
    reg_chooseSpecularMicroCode(mode, clip, 4);
#ifdef ICO_RD
    regHostMesh(pkt, 1);
#else
    dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
    dl_CloseDma();
}

static void reg_transMaterialPacket(PacHeader *self, PObjGroup *grp)
{
    short idx = self->mat;
    if (idx != -1) {
        PObjMaterial *v = &grp->materials[idx];
        dl_OpenDma(2, v, 6);
        dl_CloseDma();
    }
}

static int reg_setDissolve(float alpha, int pri)
{
    char *p;
    char *q;
    int v;

    v = (int)((alpha < 0.0f ? alpha + 1.0f : 1.0f - alpha) * 96.0f);
    if (v >= 128) {
        v = 127;
    }
    if (v < 0) {
        v = 0;
    }
    p = PacketBufferStruct.ptr.c;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.dma.c = p;
    PacketBufferStruct.end.c = 0;
    PacketBufferStruct.tail.c = p;
    ((GifPkWord *)p)->d = 0x10000005;
    PacketBufferStruct.ptr.c = p + 8;
    ((GifPkWord *)(p + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = p + 0xC;
    PacketBufferStruct.gif.c = p + 0xC;
    ((GifPkWord *)(p + 8))->w[1] = 0x6C048000;
    PacketBufferStruct.ptr.c = p + 0x10;
    ((GifPkWord *)(p + 0x10))->d = 0x1000000000008003LL;
    PacketBufferStruct.ptr.c = p + 0x18;
    ((GifPkWord *)(p + 0x18))->d = 14;
    PacketBufferStruct.ptr.c = p + 0x20;
    ((GifPkWord *)(p + 0x20))->d = 0;
    PacketBufferStruct.ptr.c = p + 0x28;
    ((GifPkWord *)(p + 0x28))->d = 0x49;
    PacketBufferStruct.ptr.c = p + 0x30;
    if (0.0f < alpha) {
        ((GifPkWord *)(p + 0x30))->d = ((long long)v << 32) | 0x68;
        PacketBufferStruct.ptr.c = p + 0x38;
    } else {
        ((GifPkWord *)(p + 0x30))->d = ((long long)v << 32) | 0x62;
        PacketBufferStruct.ptr.c = p + 0x38;
    }
    q = PacketBufferStruct.ptr.c;
    ((GifPkWord *)q)->d = 0x42;
    q += 8;
    PacketBufferStruct.ptr.c = q;
    ((GifPkWord *)q)->d = 0x1300000C0LL;
    PacketBufferStruct.ptr.c = q + 8;
    ((GifPkWord *)(q + 8))->d = 0x4E;
    PacketBufferStruct.ptr.c = q + 0x10;
    ((GifPkWord *)(q + 0x10))->w[0] = 0x15000000;
    PacketBufferStruct.ptr.c = q + 0x14;
    ((GifPkWord *)(q + 0x10))->w[1] = 0;
    PacketBufferStruct.ptr.c = q + 0x18;
    ((GifPkWord *)(q + 0x18))->d = 0;
    PacketBufferStruct.ptr.c = q + 0x20;
    PacketBufferStruct.tail.c = q + 0x20;
    ((GifPkWord *)(q + 0x20))->d = 0x60000000;
    PacketBufferStruct.ptr.c = q + 0x28;
    ((GifPkWord *)(q + 0x28))->w[0] = 0;
    PacketBufferStruct.ptr.c = q + 0x2C;
    ((GifPkWord *)(q + 0x28))->w[1] = 0;
    PacketBufferStruct.ptr.c = q + 0x30;
    dl_SetDLPriority(pri);
    dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
    dl_CloseDma();
    return 1;
}

/* The GS state the dissolve leaves behind, defined after reg_dispNObj (see
   there): a VIF DIRECT of two qwords, a GIF A+D tag with TEST_1, then the VIF
   MSCNT; qword aligned because
   dl_OpenDma chains it into the display list as a DMA source. */
static const unsigned int regDissolveResetPacket[4][4] __attribute__((aligned(16)));

static void reg_resetDissolve(int pri)
{
    dl_SetDLPriority(pri);
    dl_OpenDma(2, regDissolveResetPacket, 4);
    dl_CloseDma();
}

/* a texture upload counted into texturetranssize, which
   reg_TransTexturePacket, reg_RenderReflection and reg_DispMultiPri inline */
static inline void regTransTexturePacket(int tex, int pri) /* derived name */
{
    if (tex >= 0) {
        texturetranssize += tex_TransTexture(tex, pri);
    }
}

/* The GS state the reflection pass draws in: a VIF DIRECT of four qwords, a
   GIF A+D tag with CLAMP_1, PABE and ALPHA_1, then the VIF MSCNT; qword aligned because
   dl_OpenDma chains it into the display list as a DMA source. */
static const unsigned int regReflectionPacket[6][4] __attribute__((aligned(16))) = {
    /* derived name */
    {0, 0, 0, 0x6C048000}, {0x8003, 0x10000000, 0xE, 0}, {5, 0, 8, 0},
    {0, 0, 0x49, 0},       {0x48, 0x80, 0x42, 0},        {0x15000000, 0, 0, 0},
};

/* the display-list priority of a shine level, which reg_GetShinePri, the
   reg_disp*Obj and the reg_Disp* functions inline */
static inline int regGetShinePri(int shine) /* derived name */
{
    switch (shine) {
    case 1:
        return 7;
    case 2:
        return 8;
    case 3:
        return 9;
    }
    return 7;
}

/* Pick the display-list priority for one material and install it. */
static inline int regMaterialDLPri(PObjGroup *grp, int nodeIdx, TexExt *ext,
                                   float fade) /* derived name */
{
    PObjMaterial *mat = &grp->materials[nodeIdx];
    int pri = 0;

    if ((((int)(mat->attr.bits >> 1)) & 3) != 0) {
        switch (mat->attr.word & 1) {
        case 0:
            pri = 2;
            break;
        case 1:
            pri = 1;
            break;
        }
    }
    if (fade != 0.0f) {
        pri = 5;
    }
    if (ext->animated != 0 && ext->file.shine != 0) {
        pri = regGetShinePri(ext->file.shine);
    }
    dl_SetDLPriority(pri);
    return pri;
}

static void reg_dispNObj(Sub15C *o)
{
    PObjModel *mdl;
    PObjGroup *grp;
    char *pk;
    PacHeader *pkt;
    float *box;
    int i;
    int j;
    int r;
    int pri;
    int mode;

    mdl = o->model;
    grp = mdl->groups;
    reg_transMicroCode(o, 0x3B5);
    pk = reg_setNMatrixPacket(o, 0);
    if (pk != 0) {
        int prilist = 0x3B5;

        for (i = 0; i < 13; i++) {
            if ((prilist >> i) & 1) {
                dl_SetDLPriority(i);
                dl_OpenDma(5, pk, 0);
                dl_CloseDma();
            }
        }
        for (j = 0; j < mdl->partCount; j++, grp++) {
            box = (float *)(j * 128 + (ICO_WORD)o->model->boxes);
            _SetCurrentMatrix(matrixptr + 0x300);
            if (gsb_ClipBox(box) != 0) {
                pkt = grp->packets;
#ifdef ICO_RD
                regKeyPart(o, grp, j); /* R7d: the draws' keys */
#endif
                while (pkt != 0) {
                    r = reg_clipPacketBoundingBox(pkt);
                    if (r != 0) {
                        pri = regMaterialDLPri(grp, pkt->mat, tex_GetTexExtData(pkt->tex), 0.0f);
                        regTransTexturePacket(pkt->tex, pri);
                        reg_transMaterialPacket(pkt, grp);
                        reg_chooseMicroCode(&grp->materials[pkt->mat], r, pri);
#ifdef ICO_RD
                        regHostMesh(pkt, 0);
#else
                        dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                        dl_CloseDma();
                        if (o->lightMtx->mode == 2) {
                            if (pkt->tex1 != -1) {
                                reg_dispSpecular(pkt, r, 0);
                            }
                        }
                        mode = o->lightMtx->mode;
                        if (pkt->tex2 != -1) {
                            if (mode == 0) {
                                debug_StdPrintfDummy("光源オフでリフレクションを表示.\n");
                                mc_TransMicroCode(2, 0x10);
                            }
                            dl_SetDLPriority(4);
                            regTransTexturePacket(pkt->tex2, 4);
                            dl_OpenDma(2, regReflectionPacket, 6);
                            dl_CloseDma();
                            reg_chooseReflectionMicroCode(0, r, 4);
#ifdef ICO_RD
                            regHostMesh(pkt, 2);
#else
                            dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                            dl_CloseDma();
                            if (mode == 0) {
                                mc_TransMicroCode(1, 0x10);
                            }
                        }
                    }
                    pkt = pkt->next;
                }
            }
        }
    }
    if (o->shadow != 0) {
        shadow_RenderVolume(o);
    }
}

/* the packet that resets the dissolve state */
static const unsigned int regDissolveResetPacket[4][4] __attribute__((aligned(16))) = {
    /* derived name */
    {0, 0, 0, 0x6C028000},
    {0x8001, 0x10000000, 0xE, 0},
    {0x300000C0, 0, 0x4E, 0},
    {0x15000000, 0, 0, 0},
};

static void reg_dispMObj(Sub15C *o)
{
    PObjModel *mdl;
    PObjGroup *grp;
    PacHeader *pkt;
    char *pk;
    struct DObjNode *w;
    float alpha;
    int i;
    int j;
    int r;
    int dis;
    int fade;
    int pri;
    int mode;
    int prilist;

    mdl = o->model;
    grp = mdl->groups;
    reg_transMicroCode(o, 0x3B5);
    for (i = 0; i < o->nodeNum; i++) {
        w = (struct DObjNode *)(i * 80 + (ICO_WORD)o->nodes);
        alpha = 1.0f - (1.0f - w->fade) * w->alpha;
        if ((alpha < 0.0f ? -alpha : alpha) == 1.0f) {
            continue;
        }
        pk = reg_setMMatrixPacket(o, i);
        if (pk != 0) {
            prilist = 0x3B5;
            for (j = 0; j < 13; j++) {
                if ((prilist >> j) & 1) {
                    dl_SetDLPriority(j);
                    dl_OpenDma(5, pk, 0);
                    dl_CloseDma();
                }
            }
            if (mdl->parts[i].morphCount != 0) {
                if (grp->morph != 0) {
                    if (buffer_ID != 0) {
                        pkt = grp->packets;
                    } else {
                        pkt = grp->morph;
                    }
                    reg_setShape(o, i, buffer_ID == 0, pkt, &grp->materials[pkt->mat]);
                } else {
                    pkt = grp->packets;
                }
            } else {
                pkt = grp->packets;
            }
#ifdef ICO_RD
            regKeyPart(o, grp, i); /* R7d: the draws' keys */
#endif
            while (pkt != 0) {
                r = reg_clipPacketBoundingBox(pkt);
                if (r != 0) {
                    fade = 0;
                    if ((o->nodes[i].flags.ll & 1) == 1) {
                        if (alpha != 0.0f) {
                            fade = 1;
                        }
                    }
                    pri = regMaterialDLPri(grp, pkt->mat, tex_GetTexExtData(pkt->tex), fade);
                    regTransTexturePacket(pkt->tex, pri);
                    reg_transMaterialPacket(pkt, grp);
                    dis = 0;
                    if (fade != 0) {
                        dis = reg_setDissolve(alpha, pri);
                    }
                    if (dis != -1) {
                        reg_chooseMicroCode(&grp->materials[pkt->mat], r, pri);
#ifdef ICO_RD
                        regHostMesh(pkt, 0);
#else
                        dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                        dl_CloseDma();
                        if (o->lightMtx->mode == 2) {
                            if (pkt->tex1 != -1) {
                                reg_dispSpecular(pkt, r, 0);
                            }
                        }
                        mode = o->lightMtx->mode;
                        if (pkt->tex2 != -1) {
                            if (mode == 0) {
                                debug_StdPrintfDummy("光源オフでリフレクションを表示.\n");
                                mc_TransMicroCode(2, 0x10);
                            }
                            dl_SetDLPriority(4);
                            regTransTexturePacket(pkt->tex2, 4);
                            dl_OpenDma(2, regReflectionPacket, 6);
                            dl_CloseDma();
                            reg_chooseReflectionMicroCode(0, r, 4);
#ifdef ICO_RD
                            regHostMesh(pkt, 2);
#else
                            dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                            dl_CloseDma();
                            if (mode == 0) {
                                mc_TransMicroCode(1, 0x10);
                            }
                        }
                    }
                    if (dis == 1) {
                        reg_resetDissolve(pri);
                    }
                }
                pkt = pkt->next;
            }
        }
        if (o->shadow != 0) {
            shadow_RenderVolumeMulti(o, i);
        }
    }
}

static void reg_dispSObj(Sub15C *o, int idx)
{
    PObjGroup *grp;
    PacHeader *pkt;
    char *pk;
    int i;
    int r;
    int pri;
    int mode;

    grp = o->model->groups;
    pkt = grp->packets;
    reg_transMicroCode(o, 0x3B5);
    pk = reg_setMMatrixPacket(o, idx);
    if (pk != 0) {
        int prilist = 0x3B5;

        for (i = 0; i < 13; i++) {
            if ((prilist >> i) & 1) {
                dl_SetDLPriority(i);
                dl_OpenDma(5, pk, 0);
                dl_CloseDma();
            }
        }
#ifdef ICO_RD
        regKeyPart(o, grp, idx); /* R7d: the draws' keys */
#endif
        while (pkt != 0) {
            r = reg_clipPacketBoundingBox(pkt);
            if (r != 0) {
                pri = regMaterialDLPri(grp, pkt->mat, tex_GetTexExtData(pkt->tex), 0.0f);
                regTransTexturePacket(pkt->tex, pri);
                reg_transMaterialPacket(pkt, grp);
                reg_chooseMicroCode(&grp->materials[pkt->mat], r, pri);
#ifdef ICO_RD
                regHostMesh(pkt, 0);
#else
                dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                dl_CloseDma();
                if (o->lightMtx->mode == 2) {
                    if (pkt->tex1 != -1) {
                        reg_dispSpecular(pkt, r, 0);
                    }
                }
                mode = o->lightMtx->mode;
                if (pkt->tex2 != -1) {
                    if (mode == 0) {
                        debug_StdPrintfDummy("光源オフでリフレクションを表示.\n");
                        mc_TransMicroCode(2, 0x10);
                    }
                    dl_SetDLPriority(4);
                    regTransTexturePacket(pkt->tex2, 4);
                    dl_OpenDma(2, regReflectionPacket, 6);
                    dl_CloseDma();
                    reg_chooseReflectionMicroCode(0, r, 4);
#ifdef ICO_RD
                    regHostMesh(pkt, 2);
#else
                    dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                    dl_CloseDma();
                    if (mode == 0) {
                        mc_TransMicroCode(1, 0x10);
                    }
                }
            }
            pkt = pkt->next;
        }
    }
    if (o->shadow != 0) {
        shadow_RenderVolume(o);
    }
}

static void reg_dispCObj(Sub15C *o)
{
    PObjModel *mdl;
    PObjGroup *grp;
    PacHeader *pkt;
    PacHeader *node;
    int i;
    int pri;

    mdl = o->model;
    grp = mdl->groups;
    reg_transMicroCode(o, 0x3B3);
    reg_setCMatrixPacket(o, 1.0f, 0x3B3);
    for (i = 0; i < mdl->partCount; i++, grp++) {
        if (mdl->parts[i].morphCount != 0) {
            node = grp->morph;
            if (node != 0) {
                pkt = node;
                if (buffer_ID != 0) {
                    pkt = grp->packets;
                }
                reg_setShape(o, i, buffer_ID == 0, pkt, &grp->materials[pkt->mat]);
            } else {
                pkt = grp->packets;
            }
        } else {
            pkt = grp->packets;
        }
#ifdef ICO_RD
        regKeyPart(o, grp, i); /* R7d: the draws' keys */
#endif
        while (pkt != 0) {
            pri = regMaterialDLPri(grp, pkt->mat, tex_GetTexExtData(pkt->tex), 0.0f);
            regTransTexturePacket(pkt->tex, pri);
            reg_transMaterialPacket(pkt, grp);
            reg_chooseMicroCode(&grp->materials[pkt->mat], 0, pri);
#ifdef ICO_RD
            regHostMesh(pkt, 0);
#else
            dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
            dl_CloseDma();
            if (debug_specular_flag == 2 && o->lightMtx->mode == 2) {
                if (pkt->tex1 != -1) {
                    reg_dispSpecular(pkt, 0, 1);
                }
            }
            pkt = pkt->next;
        }
    }
    if (o->shadow != 0) {
        shadow_RenderVolume(o);
    }
}

static void reg_dispPoint(PacLine *node, float alpha, int idx, int flag)
{
    float fv[4];
    int iv[4];
    int iv2[4];
    Qw128 v;
    int pri;
    int on;
    int z;

    on = 0;
    pri = (node->attr.word & 0x1800) != 0 ? 2 : 0;
    if (alpha != 0.0f || (node->attr.word & 0x1800) != 0) {
        on = 1;
    }
    z = (int)(((alpha < 0.0f) ? (alpha + 1.0f) : (1.0f - alpha)) * 128.0f);
    dl_SetDLPriority(pri);
    {
        char *c = PacketBufferStruct.ptr.c;

        PacketBufferStruct.gif.c = 0;
        PacketBufferStruct.end.c = 0;
        PacketBufferStruct.dma.c = c;
        PacketBufferStruct.tail.c = c;
        PacketBufferStruct.ptr.c = c + 8;
        ((DpkTag *)c)->vif[0] = 0x11000000;
        PacketBufferStruct.gif.c = c + 0xC;
        PacketBufferStruct.end.c = c + 0x10;
        PacketBufferStruct.ptr.c = c + 0x18;
        ((GifPkWord *)(c + 0x18))->d = 0xE;
        PacketBufferStruct.ptr.c = c + 0x20;
    }
    _RotTransPersCurrentMatrix(fv, node->pos[0]);
    _FTOI4Vector(iv, fv);
    if (idx == -1) {
        if (flag != 0) {
            _CopyIVector(iv2, iv);
        } else {
            if (systemStatus[5] == 0) {
                v = *(Qw128 *)node->pos[1];
            } else {
                v = *(Qw128 *)node->scrPrev;
            }
            *(Qw128 *)iv2 = v;
        }
        if (systemStatus[5] == 0) {
            *(Qw128 *)node->scrPrev = *(Qw128 *)node->pos[1];
            *(Qw128 *)node->pos[1] = *(Qw128 *)iv;
        }
    } else {
        if (flag != 0) {
            _CopyIVector(iv2, iv);
        } else {
            if (systemStatus[5] == 0) {
                _CopyIVector(iv2, node->trail[idx]);
            } else {
                _CopyIVector(iv2, node->trailPrev[idx]);
            }
        }
        if (systemStatus[5] == 0) {
            _CopyIVector(node->trailPrev[idx], node->trail[idx]);
            _CopyIVector(node->trail[idx], iv);
        }
    }
    if (_IsInScreen(iv) != 0 && _IsInScreen(iv2) != 0) {
        if (on != 0) {
            if (0.0f < alpha) {
                *PacketBufferStruct.ptr.d++ = 0;
                *PacketBufferStruct.ptr.d++ = 0x49;
                *PacketBufferStruct.ptr.d++ = ((long long)z << 32) | 104;
                *PacketBufferStruct.ptr.d++ = 0x42;
            } else {
                *PacketBufferStruct.ptr.d++ = 0;
                *PacketBufferStruct.ptr.d++ = 0x49;
                *PacketBufferStruct.ptr.d++ = ((long long)z << 32) | 98;
                *PacketBufferStruct.ptr.d++ = 0x42;
            }
        }
        *PacketBufferStruct.ptr.d++ = ((long long)on << 6) | 0x101;
        *PacketBufferStruct.ptr.d++ = 0;
        *PacketBufferStruct.ptr.d++ = (long long)node->col0.r | ((long long)node->col0.g << 8) |
                                      ((long long)node->col0.b << 16) |
                                      ((long long)node->col0.a << 24) | (0x3F800000LL << 32);
        *PacketBufferStruct.ptr.d++ = 1;
        *PacketBufferStruct.ptr.d++ =
            (long long)iv2[0] | ((long long)iv2[1] << 16) | ((long long)iv2[2] << 32);
        *PacketBufferStruct.ptr.d++ = 5;
        *PacketBufferStruct.ptr.d++ =
            (long long)iv[0] | ((long long)iv[1] << 16) | ((long long)iv[2] << 32);
        *PacketBufferStruct.ptr.d++ = 5;
        {
            char *p;
            char *q;

            ((GifPkWord *)PacketBufferStruct.end.c)->d =
                (unsigned int)(((unsigned int)(PacketBufferStruct.ptr.c -
                                               PacketBufferStruct.end.c) >>
                                4) -
                               1) |
                0x1000000000008000LL;
            ((GifPkWord *)PacketBufferStruct.gif.c)->w[0] =
                (((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.gif.c) >> 4) << 16) |
                0x6C008000;
            p = PacketBufferStruct.ptr.c;
            ((GifPkWord *)p)->w[0] = 0x15000000;
            p += 4;
            PacketBufferStruct.ptr.c = p;
            ((GifPkWord *)p)->w[0] = 0;
            PacketBufferStruct.ptr.c = p + 4;
            ((GifPkWord *)(p + 4))->w[0] = 0;
            PacketBufferStruct.ptr.c = p + 8;
            ((GifPkWord *)(p + 8))->w[0] = 0;
            PacketBufferStruct.ptr.c = p + 0xC;
            ((GifPkWord *)PacketBufferStruct.tail.c)->d =
                (unsigned int)((((unsigned int)(PacketBufferStruct.ptr.c -
                                                PacketBufferStruct.tail.c) >>
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
        }
    }
}

/* a float's bits as the GS ST register takes them */
typedef union { /* field names derived */
    float f;
    unsigned int u;
} RegFloatBits; /* derived name */

static void reg_dispLine(PacLine *node, float alpha)
{
    float fv0[4];
    float fv1[4];
    int iv0[4];
    int iv1[4];
    char *p;
    char *q;
    long long h;
    int pri;
    int tex;
    int hastex;
    int on;
    int z;

    h = node->attr.word;
    pri = (h & 0x1800) != 0 ? 2 : 0;
    tex = (unsigned short)h << 21 >> 21;
    hastex = tex >= 0;
    on = 0;
    if (alpha != 0.0f || (h & 0x1800) != 0) {
        on = 1;
    }
    z = (int)(((alpha < 0.0f) ? (alpha + 1.0f) : (1.0f - alpha)) * 128.0f);
    dl_SetDLPriority(pri);
    if (hastex != 0) {
        long long f = node->attr.word;

        texturetranssize += tex_TransTexture((unsigned short)f << 21 >> 21, pri);
    }
    {
        char *c = PacketBufferStruct.ptr.c;

        PacketBufferStruct.gif.c = 0;
        PacketBufferStruct.end.c = 0;
        PacketBufferStruct.dma.c = c;
        PacketBufferStruct.tail.c = c;
        PacketBufferStruct.ptr.c = c + 8;
        ((DpkTag *)c)->vif[0] = 0x11000000;
        PacketBufferStruct.gif.c = c + 0xC;
        PacketBufferStruct.end.c = c + 0x10;
        PacketBufferStruct.ptr.c = c + 0x18;
        ((GifPkWord *)(c + 0x18))->d = 0xE;
        PacketBufferStruct.ptr.c = c + 0x20;
    }
    _RotTransPersCurrentMatrix(fv0, node->pos[0]);
    _RotTransPersCurrentMatrix(fv1, node->pos[1]);
    _FTOI4Vector(iv0, fv0);
    _FTOI4Vector(iv1, fv1);
    if (_IsInScreen(iv0) != 0 && _IsInScreen(iv1) != 0) {
        if (on != 0) {
            if (0.0f < alpha) {
                *PacketBufferStruct.ptr.d++ = 0;
                *PacketBufferStruct.ptr.d++ = 0x49;
                *PacketBufferStruct.ptr.d++ = ((long long)z << 32) | 104;
                *PacketBufferStruct.ptr.d++ = 0x42;
            } else {
                *PacketBufferStruct.ptr.d++ = 0;
                *PacketBufferStruct.ptr.d++ = 0x49;
                *PacketBufferStruct.ptr.d++ = ((long long)z << 32) | 98;
                *PacketBufferStruct.ptr.d++ = 0x42;
            }
        }
        /* PRIM in the GS register's field order: LINE with IIP, TME, ABE */
        *PacketBufferStruct.ptr.d++ = 9 | ((long long)hastex << 4) | ((long long)on << 6);
        *PacketBufferStruct.ptr.d++ = 0;
        *PacketBufferStruct.ptr.d++ = (long long)node->col0.r | ((long long)node->col0.g << 8) |
                                      ((long long)node->col0.b << 16) |
                                      ((long long)node->col0.a << 24) | (0x3F800000LL << 32);
        *PacketBufferStruct.ptr.d++ = 1;
        if (hastex != 0) {
            RegFloatBits u;
            RegFloatBits v;

            u.f = node->uv[0][0] / fv0[3];
            v.f = node->uv[0][1] / fv0[3];
            *PacketBufferStruct.ptr.d++ = (long long)u.u | ((long long)v.u << 32);
            *PacketBufferStruct.ptr.d++ = 2;
        }
        *PacketBufferStruct.ptr.d++ =
            (long long)iv0[0] | ((long long)iv0[1] << 16) | ((long long)iv0[2] << 32);
        *PacketBufferStruct.ptr.d++ = 5;
        *PacketBufferStruct.ptr.d++ = (long long)node->col1.r | ((long long)node->col1.g << 8) |
                                      ((long long)node->col1.b << 16) |
                                      ((long long)node->col1.a << 24) | (0x3F800000LL << 32);
        *PacketBufferStruct.ptr.d++ = 1;
        if (hastex != 0) {
            RegFloatBits u;
            RegFloatBits v;

            u.f = node->uv[1][0] / fv1[3];
            v.f = node->uv[1][1] / fv1[3];
            *PacketBufferStruct.ptr.d++ = (long long)u.u | ((long long)v.u << 32);
            *PacketBufferStruct.ptr.d++ = 2;
        }
        *PacketBufferStruct.ptr.d++ =
            (long long)iv1[0] | ((long long)iv1[1] << 16) | ((long long)iv1[2] << 32);
        *PacketBufferStruct.ptr.d++ = 5;
        ((GifPkWord *)PacketBufferStruct.end.c)->d =
            (unsigned int)(((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.end.c) >>
                            4) -
                           1) |
            0x1000000000008000LL;
        ((GifPkWord *)PacketBufferStruct.gif.c)->w[0] =
            (((unsigned int)(PacketBufferStruct.ptr.c - PacketBufferStruct.gif.c) >> 4) << 16) |
            0x6C008000;
        p = PacketBufferStruct.ptr.c;
        ((GifPkWord *)p)->w[0] = 0x15000000;
        p += 4;
        PacketBufferStruct.ptr.c = p;
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
    }
}

static void reg_dispPointLineObj(Sub15C *o)
{
    PObjModel *mdl;
    PObjGroup *grp;
    char *hdr;
    PacLine *node;
    PacLineSet *set;
    struct DObjNode *w;
    long long h;
    float alpha;
    int idx;
    int flag;
    int i;

    mdl = o->model;
    grp = mdl->groups;
    hdr = o->lineHdr;
    if (hdr != 0) {
        idx = *(unsigned short *)(hdr + 2) % 3;
    } else {
        idx = -1;
    }
    flag = 0;
    if (hdr != 0) {
        flag = ((int)(*(long long *)hdr >> 14)) & 1;
    }
    if (currentScreenWidth != 0 || GlobalTimer != 0) {
        flag = 1;
    }
    for (i = 0; i < o->nodeNum; i++) {
        w = (struct DObjNode *)(i * 80 + (ICO_WORD)o->nodes);
        set = grp->packets;
        alpha = 1.0f - (1.0f - w->fade) * w->alpha;
        if (!flag && (alpha < 0.0f ? -alpha : alpha) == 1.0f) {
            continue;
        }
        _SetCurrentMatrix((char *)o->nodeMtx + i * 64);
        _MulCurrentMatrixL(matrixptr + 0x100);
        node = set->lines;
        h = node->attr.word;
        while (h & 0xE000) {
            switch ((short)h >> 13) {
            case 1:
                reg_dispPoint(node, alpha, idx, flag);
                break;
            case 2:
                reg_dispLine(node, alpha);
                break;
            }
            node++;
            h = node->attr.word;
        }
    }
}

static void reg_setNMatrixPacketNoLightCalc_setMatrix(void)
{
    char *c;
    char *m;

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = c;
    ((GifPkWord *)c)->d = 0x1000000D;
    PacketBufferStruct.ptr.c = c + 8;
    ((GifPkWord *)(c + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = c + 0xC;
    PacketBufferStruct.gif.c = c + 0xC;
    ((GifPkWord *)(c + 8))->w[1] = 0x6C0C8000;
    PacketBufferStruct.ptr.c = c + 0x50;
    _CopyMatrix(c + 0x10, matrixptr + 0x140);
    _MulMatrix(PacketBufferStruct.ptr.c, matrixptr + 0x200, matrixptr + 0x40);
    PacketBufferStruct.ptr.c = PacketBufferStruct.ptr.c + 0x40;
    _MulMatrix(PacketBufferStruct.ptr.c, matrixptr + 0x80, matrixptr + 0x40);
    m = PacketBufferStruct.ptr.c;
    PacketBufferStruct.ptr.c = m + 0x40;
    ((GifPkWord *)(m + 0x40))->w[0] = 0x15000010;
    PacketBufferStruct.ptr.c = m + 0x44;
    ((GifPkWord *)(m + 0x40))->w[1] = 0;
    PacketBufferStruct.ptr.c = m + 0x48;
    ((GifPkWord *)(m + 0x48))->d = 0;
    PacketBufferStruct.ptr.c = m + 0x50;
}

static void reg_setNMatrixPacketNoLightCalc_setLight(Sub15C *o)
{
    char *c;
    char *m;
    char *n;

    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = c;
    ((GifPkWord *)c)->d = 0x10000009;
    PacketBufferStruct.ptr.c = c + 8;
    ((GifPkWord *)(c + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = c + 0xC;
    PacketBufferStruct.gif.c = c + 0xC;
    ((GifPkWord *)(c + 8))->w[1] = 0x6C088000;
    PacketBufferStruct.ptr.c = c + 0x10;
    _GetCurrentMatrix(c + 0x10);
    m = PacketBufferStruct.ptr.c;
    PacketBufferStruct.ptr.c = m + 0x80;
    _CopyMatrix(m + 0x40, (char *)o->lightMtx + 64);
    n = PacketBufferStruct.ptr.c;
    ((GifPkWord *)n)->w[0] = 0x15000012;
    n += 4;
    PacketBufferStruct.ptr.c = n;
    ((GifPkWord *)n)->w[0] = 0;
    PacketBufferStruct.ptr.c = n + 4;
    ((GifPkWord *)(n + 4))->d = 0;
    PacketBufferStruct.ptr.c = n + 0xC;
}

static char *reg_setNMatrixPacketNoLightCalc(Sub15C *o, Sub15C *src, int idx)
{
    char *pkt;
    float *box;
    struct DObjNode *scl;
    int mode;

    scl = (struct DObjNode *)(idx * 80 + (ICO_WORD)o->nodes);
    mode = o->lightMtx->mode;
    if (scl->scale[0] != 1.0f || scl->scale[1] != 1.0f || scl->scale[2] != 1.0f) {
        _InitCurrentMatrix();
        _SetCurrentMatrix((char *)o->nodeMtx + idx * 64);
        _ScaleCurrentMatrix(o->nodes[idx].scale[0], o->nodes[idx].scale[1], o->nodes[idx].scale[2]);
        _GetCurrentMatrix(matrixptr + 0x40);
    } else {
        _CopyMatrix(matrixptr + 0x40, (char *)o->nodeMtx + idx * 64);
    }
    _MulMatrix(matrixptr + 0x300, matrixptr + 0x280, matrixptr + 0x40);
    _MulMatrix(matrixptr + 0x140, matrixptr + 0x100, matrixptr + 0x40);
    box = o->model->box[0];
    _SetCurrentMatrix(matrixptr + 0x300);
    if (gsb_ClipBox(box) == 0) {
        return 0;
    }
    pkt = PacketBufferStruct.ptr.c;
    PacketBufferStruct.dma.c = pkt;
    PacketBufferStruct.tail.c = 0;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.end.c = 0;
    reg_setNMatrixPacketNoLightCalc_setMatrix();
    if (mode != 0 && mode != 3) {
        o->model->shadowLength = src->model->shadowLength;
        _CopyVector(ICO_RAWP(char *, o, 0x860, (char *)o->shadowDir),
                    ICO_RAWP(char *, src, 0x860, (char *)src->shadowDir));
        _CopyMatrix((char *)o->lightMtx, (char *)src->lightMtx);
        _CopyMatrix((char *)o->lightMtx + 64, (char *)src->lightMtx + 64);
        _SetCurrentMatrix(matrixptr + 0x40);
        _ClearTransCurrentMatrix();
        _MulCurrentMatrixL((char *)o->lightMtx);
        reg_setNMatrixPacketNoLightCalc_setLight(o);
    }
    {
        char *c = PacketBufferStruct.ptr.c;

        PacketBufferStruct.tail.c = c;
        ((GifPkWord *)c)->d = 0x60000000;
        PacketBufferStruct.ptr.c = c + 8;
        ((GifPkWord *)(c + 8))->w[0] = 0;
        PacketBufferStruct.ptr.c = c + 0xC;
        ((GifPkWord *)(c + 8))->w[1] = 0;
        PacketBufferStruct.ptr.c = c + 0x10;
    }
    return pkt;
}

void reg_DispAccessoryWithShadow(Sub15C *o, Sub15C *src)
{
    PObjModel *mdl;
    PObjGroup *grp;
    char *pk;
    PacHeader *pkt;
    float *box;
    int i;
    int j;
    int r;
    int pri;
    int mode;

    mdl = o->model;
    grp = mdl->groups;
    reg_transMicroCode(o, 0x3B5);
    pk = reg_setNMatrixPacketNoLightCalc(o, src, 0);
    if (pk != 0) {
        int prilist = 0x3B5;

        for (i = 0; i < 13; i++) {
            if ((prilist >> i) & 1) {
                dl_SetDLPriority(i);
                dl_OpenDma(5, pk, 0);
                dl_CloseDma();
            }
        }
        for (j = 0; j < mdl->partCount; j++, grp++) {
            box = (float *)(j * 128 + (ICO_WORD)o->model->boxes);
            _SetCurrentMatrix(matrixptr + 0x300);
            if (gsb_ClipBox(box) != 0) {
                pkt = grp->packets;
#ifdef ICO_RD
                regKeyPart(o, grp, j); /* R7d: the draws' keys */
#endif
                while (pkt != 0) {
                    r = reg_clipPacketBoundingBox(pkt);
                    if (r != 0) {
                        pri = regMaterialDLPri(grp, pkt->mat, tex_GetTexExtData(pkt->tex), 0.0f);
                        regTransTexturePacket(pkt->tex, pri);
                        reg_transMaterialPacket(pkt, grp);
                        reg_chooseMicroCode(&grp->materials[pkt->mat], r, pri);
#ifdef ICO_RD
                        regHostMesh(pkt, 0);
#else
                        dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                        dl_CloseDma();
                        if (o->lightMtx->mode == 2) {
                            if (pkt->tex1 != -1) {
                                reg_dispSpecular(pkt, r, 0);
                            }
                        }
                        mode = o->lightMtx->mode;
                        if (pkt->tex2 != -1) {
                            if (mode == 0) {
                                debug_StdPrintfDummy("光源オフでリフレクションを表示.\n");
                                mc_TransMicroCode(2, 0x10);
                            }
                            dl_SetDLPriority(4);
                            regTransTexturePacket(pkt->tex2, 4);
                            dl_OpenDma(2, regReflectionPacket, 6);
                            dl_CloseDma();
                            reg_chooseReflectionMicroCode(0, r, 4);
#ifdef ICO_RD
                            regHostMesh(pkt, 2);
#else
                            dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                            dl_CloseDma();
                            if (mode == 0) {
                                mc_TransMicroCode(1, 0x10);
                            }
                        }
                    }
                    pkt = pkt->next;
                }
            }
        }
        if (o->shadow != 0) {
            shadow_RenderVolume(o);
        }
    }
}

void reg_RenderReflection(Sub15C *o, int pri)
{
    PObjModel *mdl;
    PObjGroup *grp;
    PacHeader *pkt;
    char *pk;
    int i;
    int r;

    mdl = o->model;
    grp = mdl->groups;
    dl_SetDLPriority(pri);
    reg_transMicroCode(o, 1 << pri);
    pk = reg_setNMatrixPacket(o, 0);
    if (pk == 0) {
        return;
    }
    dl_SetDLPriority(pri);
    dl_OpenDma(5, pk, 0);
    dl_CloseDma();
    for (i = 0; i < mdl->partCount; i++) {
        pkt = grp->packets;
#ifdef ICO_RD
        regKeyPart(o, grp, i); /* R7d: the draws' keys */
#endif
        while (pkt != 0) {
            r = reg_clipPacketBoundingBox(pkt);
            if (r != 0) {
                regTransTexturePacket(pkt->tex, pri);
                reg_transMaterialPacket(pkt, grp);
                reg_chooseMicroCode(&grp->materials[pkt->mat], r, pri);
#ifdef ICO_RD
                regHostMesh(pkt, 0);
#else
                dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                dl_CloseDma();
            }
            pkt = pkt->next;
        }
        grp++;
    }
}

/* The sixteen bytes that close the enemy matrix packet: the VIF MSCAL 0x10
   code and three zero words, copied as one quadword. */
static const sceVu0IVECTOR regEnemyEndTag = {0x15000010, 0, 0, 0}; /* derived name */

static inline void reg_setEMatrixPacket_pack(Sub15C *o, float alpha)
{
    char *c;
    char *m;
    int n;
    int i;

    /* the object's matrix count through the object record, the view
     * reg_setMMatrixPacket takes of o */
    n = o->nodeNum * 4;
    c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = c;
    ((RegClusterHead *)c)->dma.tag = n | 0x10000002;
    PacketBufferStruct.ptr.c = c + 8;
    ((RegClusterHead *)c)->dma.vif[0] = 0x11000000;
    PacketBufferStruct.ptr.c = c + 0xC;
    PacketBufferStruct.gif.c = c + 0xC;
    ((RegClusterHead *)c)->dma.vif[1] = ((n + 1) << 16) | 0x6C008000;
    PacketBufferStruct.ptr.c = c + 0x10;
    ((RegClusterHead *)c)->qwc = n + 1;
    PacketBufferStruct.ptr.c = c + 0x14;
    *(int *)(c + 0x14) = 0;
    PacketBufferStruct.ptr.c = c + 0x18;
    *(int *)(c + 0x18) = 0;
    PacketBufferStruct.ptr.c = c + 0x1C;
    ((RegClusterHead *)c)->alpha = alpha;
    PacketBufferStruct.ptr.c = c + 0x20;
    for (i = 0; i < o->nodeNum; i++) {
        _MulMatrix(PacketBufferStruct.ptr.c, (char *)o->nodeMtx + i * 64, o->clusterMtx + i * 64);
        PacketBufferStruct.ptr.c = PacketBufferStruct.ptr.c + 0x40;
    }
    m = PacketBufferStruct.ptr.c;
    *(Qw128 *)m = *(Qw128 *)&regEnemyEndTag;
    m += 0x10;
    PacketBufferStruct.ptr.c = m;
}

static void reg_setEMatrixPacket(Sub15C *o, int prilist, float alpha)
{
    int i;

    PacketBufferStruct.dma.c = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = 0;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.end.c = 0;
    reg_setEMatrixPacket_pack(o, alpha);
    {
        char *c = PacketBufferStruct.ptr.c;

        PacketBufferStruct.tail.c = c;
        ((DpkTag *)c)->tag = 0x60000000;
        PacketBufferStruct.ptr.c = c + 8;
        ((DpkTag *)c)->vif[0] = 0;
        PacketBufferStruct.ptr.c = c + 0xC;
        ((DpkTag *)c)->vif[1] = 0;
        PacketBufferStruct.ptr.c = c + 0x10;
    }
    for (i = 0; i < 13; i++) {
        if ((prilist >> i) & 1) {
            dl_SetDLPriority(i);
            dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
            dl_CloseDma();
        }
    }
}

void reg_DispEnemy(void *sub)
{
    Sub15C *o = sub;
    PObjModel *mdl;
    PObjGroup *grp;
    PacHeader *pkt;
    float alpha;
    int i;
    int pri;

    mdl = o->model;
    grp = mdl->groups;
    alpha = 1.0f - o->nodes->fade;
    reg_transMicroCode(o, 0x3A0);
    reg_setEMatrixPacket(o, 0x3A0, alpha);
    if ((alpha < 0.0f ? -alpha : alpha) == 0.0f) {
        goto done;
    }
    {
        for (i = 0; i < mdl->partCount; i++, grp++) {
            if (mdl->parts[i].morphCount != 0) {
                if (grp->morph != 0) {
                    if (buffer_ID != 0) {
                        pkt = grp->packets;
                    } else {
                        pkt = grp->morph;
                    }
                    reg_setShape(o, i, buffer_ID == 0, pkt, &grp->materials[pkt->mat]);
                } else {
                    pkt = grp->packets;
                }
            } else {
                pkt = grp->packets;
            }
#ifdef ICO_RD
            regKeyPart(o, grp, i); /* R7d: the draws' keys */
#endif
            while (pkt != 0) {
                pri = regMaterialDLPri(grp, pkt->mat, tex_GetTexExtData(pkt->tex), 0.0f);
                regTransTexturePacket(pkt->tex, pri);
                reg_transMaterialPacket(pkt, grp);
                reg_chooseMicroCode(&grp->materials[pkt->mat], 0, pri);
#ifdef ICO_RD
                regHostMesh(pkt, 0);
#else
                dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                dl_CloseDma();
                pkt = pkt->next;
            }
        }
    }
done:
    if (o->shadow != 0) {
        shadow_RenderVolume(o);
    }
}

void reg_DispMultiPri(Sub15C *o, int pri)
{
    PObjModel *mdl;
    PObjGroup *grp;
    PacHeader *pkt;
    PacHeader *node;
    char *pk;
    struct DObjNode *w;
    float alpha;
    int i;
    int j;
    int r;
    int dis;
    int fade;
    int mode;
    int prilist;

    mdl = o->model;
    grp = mdl->groups;
    reg_transMicroCode(o, 1 << pri);
    for (i = 0; i < o->nodeNum; i++) {
        w = (struct DObjNode *)(i * 80 + (ICO_WORD)o->nodes);
        alpha = 1.0f - (1.0f - w->fade) * w->alpha;
        if ((alpha < 0.0f ? -alpha : alpha) == 1.0f) {
            continue;
        }
        pk = reg_setMMatrixPacket(o, i);
        if (pk == 0) {
            return;
        }
        prilist = 1 << pri;
        for (j = 0; j < 13; j++) {
            if ((prilist >> j) & 1) {
                dl_SetDLPriority(j);
                dl_OpenDma(5, pk, 0);
                dl_CloseDma();
            }
        }
        if (mdl->parts[i].morphCount != 0 && (node = grp->morph) != 0) {
            pkt = node;
            if (buffer_ID != 0) {
                pkt = grp->packets;
            }
            reg_setShape(o, i, buffer_ID == 0, pkt, &grp->materials[pkt->mat]);
        } else {
            pkt = grp->packets;
        }
#ifdef ICO_RD
        regKeyPart(o, grp, i); /* R7d: the draws' keys */
#endif
        while (pkt != 0) {
            r = reg_clipPacketBoundingBox(pkt);
            if (r != 0) {
                fade = 0;
                if ((o->nodes[i].flags.ll & 1) == 1) {
                    if (alpha != 0.0f) {
                        fade = 1;
                    }
                }
                regTransTexturePacket(pkt->tex, pri);
                reg_transMaterialPacket(pkt, grp);
                dis = 0;
                if (fade != 0) {
                    dis = reg_setDissolve(alpha, pri);
                }
                if (dis != -1) {
                    reg_chooseMicroCode(&grp->materials[pkt->mat], r, pri);
#ifdef ICO_RD
                    regHostMesh(pkt, 0);
#else
                    dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                    dl_CloseDma();
                    if (o->lightMtx->mode == 2) {
                        if (pkt->tex1 != -1) {
                            reg_dispSpecular(pkt, r, 0);
                        }
                    }
                    mode = o->lightMtx->mode;
                    if (pkt->tex2 != -1) {
                        if (mode == 0) {
                            debug_StdPrintfDummy("光源オフでリフレクションを表示.\n");
                            mc_TransMicroCode(2, 0x10);
                        }
                        dl_SetDLPriority(4);
                        regTransTexturePacket(pkt->tex2, 4);
                        dl_OpenDma(2, regReflectionPacket, 6);
                        dl_CloseDma();
                        reg_chooseReflectionMicroCode(0, r, 4);
#ifdef ICO_RD
                        regHostMesh(pkt, 2);
#else
                        dl_OpenDma(2, pkt->data, pkt->size >> 4);
#endif
                        dl_CloseDma();
                        if (mode == 0) {
                            mc_TransMicroCode(1, 0x10);
                        }
                    }
                }
                if (dis == 1) {
                    reg_resetDissolve(pri);
                }
            }
            pkt = pkt->next;
        }
    }
}

/* PC port (package L1): the title's logo is not drawn while a Settings or
   Extras page opened from the title covers it (port/game/title_logo.c) */
extern int ico_title_logo_skip(const char *model);

void reg_DispObj(Sub15C *o)
{
    if (ico_title_logo_skip(o->model->name)) {
        return;
    }
    if (o->dispType == 2) {
        unsigned short type = o->model->mode.bits >> 16;

        if ((type & 3) == 2) {
            reg_dispPointLineObj(o);
        } else {
            reg_dispMObj(o);
        }
    } else {
        unsigned short type = o->model->mode.bits >> 16;

        switch (type & 3) {
        case 0:
            reg_dispNObj(o);
            break;
        case 1:
            reg_dispCObj(o);
            break;
        case 2:
            reg_dispPointLineObj(o);
            break;
        }
    }
}

void reg_DispObj2(Sub15C *o, int idx)
{
    reg_dispSObj(o, idx);
}

void reg_SetScissorSw(int val)
{
    scissorSw = val;
}

void reg_TransTexturePacket(int tex, int pri)
{
    regTransTexturePacket(tex, pri);
}

void reg_Init(void)
{
    scissorSw = 0;
}

int reg_GetShinePri(int shine)
{
    return regGetShinePri(shine);
}
