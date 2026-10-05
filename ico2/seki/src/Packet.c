#include <libvu0.h>
#include "debug.h"
#include "cdvd.h"
#include "Basic.h"
#include "Matrix.h"
#include "Texture.h"
#include "Packet.h"
#include "DisplayP2O.h"
#include "Light.h"
#include <stdio.h>
#include "debug_exception.h"
#include "memory.h"
#include <assert.h>

/* the largest packet pac_MakePacket has built so far */
static int maxPacketSize = 0; /* derived name */

/* A word written as a float through the union member. */
typedef union { /* field names derived */
    float f;
    unsigned int ui;
} PacketFloat; /* derived name */

/* A 16-byte vector copied as two doublewords.  pacWork.boxMin and boxMax
   are the 32-byte bounding box, minimum then maximum corner. */
typedef struct { /* field names derived */
    sceVu0FVECTOR f;
} PacBoxVec; /* derived name */

/* The context's counter pair at +0x30 (w[0] the element count, w[1] the GIF
   count) and the state word at +0x38, each read both as 32-bit words and as
   a 64-bit mask. */
typedef union { /* field names derived */
    unsigned long long ul;
    long long l;
    int w[2];
} PacState; /* derived name */

/* A packet address held as a word: unsigned int on the EE, and pointer-wide
   on the host, where the packet builder's addresses are host pointers. */
#ifdef ICO_HOST

typedef __UINTPTR_TYPE__ PacAddr;

#else

typedef unsigned int PacAddr;

#endif
#define PAC_PTR(T, a) ((T)(ICO_WORD)(a))

/* The work area's layout.  The cursor is a union of packet pointers, and
   each word is one post-increment statement. */
typedef union { /* field names derived */
    int *i;
    float *f;
    PacAddr addr;
    PacBoxVec *v;
} PacCursor; /* derived name */

typedef struct { /* field names derived */
    char name[32];
    PacAddr dmaTag;
    PacAddr vifCode;
    PacAddr gifTag;
    PacCursor cursor;
    PacState counts;
    PacState state;
    PacketFloat boxMin[4];
    PacketFloat boxMax[4];
} PacWork; /* derived name */

/* the packet builder's work area: the model name the error messages print,
 * the open DMA tag, VIF code and GIF tag, the write cursor, the element
 * counts, the state bits and the bounding box */
static PacWork pacWork; /* derived name */

static void pac_DispQW(void *p, int size)
{
    int i;
    int j;
    int flag;

    flag = 0;
    switch (size) {
    case 0:
        flag = 1;
        size = 4;
        debug_StdPrintfDummy("(addr 0x%08x <fl) : ", p);
        break;
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
        debug_StdPrintfDummy("(addr 0x%08x <%2d>) : ", p, size);
        break;
    default:
        return;
    }
    for (i = 0; i < 16 / size; i++) {
        if (flag == 0) {
            for (j = 16 / (16 / size) - 1; j >= 0; j--)
                debug_StdPrintfDummy("%02x", ((unsigned char *)p)[i * size + j]);
            debug_StdPrintfDummy(" ");
        } else {
            debug_StdPrintfDummy("%12f ", ((float *)p)[i]);
        }
    }
    debug_StdPrintfDummy("\n");
}

inline void pac_Dump(int *data, int size)
{
    int *p = data;
    int count;
    size >>= 4;
    if (size <= 0)
        return;
    count = size;
    do {
        int *arg = p;
        p += 4;
        pac_DispQW(arg, 4);
        count--;
    } while (count != 0);
}

void pac_DumpPac(PacHeader *pac)
{
    char *q;
    int i;
    int cnt;
    int n;

    while (pac != 0) {
        q = pac->data;
        cnt = 0;
        for (i = 0; i < (pac->size >> 4); i++) {
            if (cnt == 0) {
                if (i == 0)
                    debug_StdPrintfDummy(":::VIFCODE:UNPACK\n");
                else if (i == (pac->size >> 4) - 1)
                    debug_StdPrintfDummy(":::VIFCODE:CAL/CNT END\n");
                else
                    debug_StdPrintfDummy(":::VIFCODE:CAL/CNT-UNPACK\n");
                debug_StdPrintfDummy(q);
            } else if (cnt == -1) {
                PacWork *ctx = &pacWork;
                n = (*(int *)q & 0xFFF) * ctx->counts.w[0];
                cnt = n + 1;
                debug_StdPrintfDummy(":::GIFTAG:NLOOP=%d(S:%d)\n", n, ctx->counts.w[0]);
                debug_StdPrintfDummy(q);
                debug_StdPrintfDummy(":::PACKET:%s\n", ctx->name);
            } else {
                debug_StdPrintfDummy(q);
            }
            q += 0x10;
            cnt--;
        }
        pac = pac->next;
        debug_StdPrintfDummy("\n");
    }
}

inline void pac_DispVu1Memory(int idx, int n, int size)
{
#ifdef ICO_HOST
    /* 0x1100C000 is VU1 data memory, which has no host address */
#else
    char *p = (char *)0x1100C000 + (idx << 4);
    int i;
    for (i = 0; i < n; i++) {
        char *q = p;
        p += 0x10;
        pac_DispQW(q, size);
    }
#endif
}

/* grows the context's bounding box by one vertex (the strip builders inline
   it) */
static inline void pac_growBounds(PacWork *ctx, char *vtx, int idx) /* derived name */
{
    if (ctx->boxMin[0].f > *(float *)(vtx + idx * 16))
        ctx->boxMin[0].f = *(float *)(vtx + idx * 16);
    if (ctx->boxMax[0].f < *(float *)(vtx + idx * 16))
        ctx->boxMax[0].f = *(float *)(vtx + idx * 16);
    if (ctx->boxMin[1].f > *(float *)(vtx + idx * 16 + 4))
        ctx->boxMin[1].f = *(float *)(vtx + idx * 16 + 4);
    if (ctx->boxMax[1].f < *(float *)(vtx + idx * 16 + 4))
        ctx->boxMax[1].f = *(float *)(vtx + idx * 16 + 4);
    if (ctx->boxMin[2].f > *(float *)(vtx + idx * 16 + 8))
        ctx->boxMin[2].f = *(float *)(vtx + idx * 16 + 8);
    if (ctx->boxMax[2].f < *(float *)(vtx + idx * 16 + 8))
        ctx->boxMax[2].f = *(float *)(vtx + idx * 16 + 8);
}

static void pac_makeBoundingBox(float (*box)[4], int flag)
{
    PacBoxVec sum;
    PacBoxVec mrg;
    PacWork *ctx;
    int i;

    memset(&sum, 0, sizeof(sum));
    mrg = (PacBoxVec){{300.0f, 300.0f, 300.0f, 0.0f}};
    if (flag != 0) {
        _AddVectorXYZ(pacWork.boxMax, pacWork.boxMax, &mrg);
        _SubVectorXYZ(pacWork.boxMin, pacWork.boxMin, &mrg);
    }
    /* the eight corners, indexed off the box */
    for (ctx = &pacWork, i = 0; i < 8; i++) {
        if (i & 1)
            box[i][0] = ctx->boxMax[0].f;
        else
            box[i][0] = ctx->boxMin[0].f;
        if (i & 2)
            box[i][1] = ctx->boxMax[1].f;
        else
            box[i][1] = ctx->boxMin[1].f;
        if (i & 4)
            box[i][2] = ctx->boxMax[2].f;
        else
            box[i][2] = ctx->boxMin[2].f;
        box[i][3] = 1.0f;
        _AddVectorXYZ(&sum, &sum, box[i]);
    }
}

static void pac_error(char *name, int type)
{
    switch (type) {
    case 5:
        debug_Assert("IN %s\n%s:NoTexture Polygon(s) exist(s).\n", pacWork.name, name);
        break;
    case 1:
        debug_Assert("IN %s\n%s:NoNormal Cluster Model exists.\n", pacWork.name, name);
        break;
    case 2:
        debug_Assert("IN %s\n%s:Abnormal Weight Data exist(s).\n", pacWork.name, name);
        break;
    case 3:
        debug_Assert("IN %s\n%s:Too Much Weight (>=4) Vertex exist(s).\n", pacWork.name, name);
        break;
    case 4:
        debug_Assert("IN %s\n%s:No Weight Vertex exist(s).\n", pacWork.name, name);
        break;
    }
    debug_assert("src/Packet.c", 684);
    __assert("src/Packet.c", 684, "0");
}

static int pac_makeNormalStrip(PObjPart *obj, short *strip, int num)
{
    char buf[256];
    short *v;
    char *vtx;
    char *nrm;
    char *uv;
    PObjTexDef *ary;
    char *col;
    PacWork *ctx;
    float f2;
    int i;

    vtx = obj->vtx;
    nrm = obj->nrm;
    uv = obj->uv;
    ary = obj->texDefs;
    col = obj->col;
    ctx = &pacWork;
    *(int *)(strip - 6) = (ICO_PHYS(ctx->cursor.addr)) - ctx->dmaTag;
    for (i = 0, v = strip; i < num; i++, v += 8) {
        PacWork *ctx = &pacWork;

        pac_growBounds(ctx, vtx, v[2]);
        _CopyVector(ctx->cursor.v++, vtx + v[2] * 16);
        if (*(float *)(vtx + v[2] * 16 + 12) != 1.0f) {
            sprintf(buf, "\033[31msorce normal model data broken! %f %f %f %f [%d]\033[0m\n",
                    *(float *)(vtx + v[2] * 16), *(float *)(vtx + v[2] * 16 + 4),
                    *(float *)(vtx + v[2] * 16 + 8), *(float *)(vtx + v[2] * 16 + 12), v[2]);
            debug_StdPrintfDummy(buf);
        }
        if ((int)(ctx->state.l >> 1) & 1)
            _CopyVector(ctx->cursor.v++, nrm + v[3] * 16);
        if (((int)(ctx->state.l >> 3) & 1) && v[4] != -1) {
            f2 = 1.0f;
            if (i == 0)
                f2 = 0.0f;
            *ctx->cursor.f++ = *(float *)(uv + v[4] * 16) * ary[v[7]].scaleU;
            *ctx->cursor.f++ = *(float *)(uv + v[4] * 16 + 4) * ary[v[7]].scaleV;
            *ctx->cursor.f++ = 1.0f;
            *ctx->cursor.f++ = f2;
        } else {
            pac_error("pac_makeNormalStrip", 5);
        }
        if (((int)(pacWork.state.l >> 4) & 1) && v[5] != -1) {
            *pacWork.cursor.f++ = (float)(unsigned char)col[v[5] * 4];
            *pacWork.cursor.f++ = (float)(unsigned char)col[v[5] * 4 + 1];
            *pacWork.cursor.f++ = (float)(unsigned char)col[v[5] * 4 + 2];
            *pacWork.cursor.f++ = 127.0f;
        } else {
            *pacWork.cursor.f++ = 128.0f;
            *pacWork.cursor.f++ = 128.0f;
            *pacWork.cursor.f++ = 128.0f;
            *pacWork.cursor.f++ = 127.0f;
        }
    }
    return num - 2;
}

/* one entry of the four-entry cluster weight table pac_getWeight fills: the
 * cluster's bone number and its weight on the vertex */
typedef struct { /* field names derived */
    int no;
    float weight;
} PacWeight; /* derived name */

/* clang-format off */
static int pac_getWeight(PacWeight *w, PObjPart *obj, char *shp, int num)
{
    int ret = -1, n = 0, i = 0;
    int j, id;
    PacWeight tmp; char *bone; float sum;
    for (j = 0; j < 4; j++) { w[j].weight = 0.0f; w[j].no = 0; }

    for (j = 0; j < obj->polyCount; j++) {
#ifdef ICO_HOST
        /* ObjEnt.p is an EE word; the entries are walked as bytes */
        bone = ICO_EEPTR(char *, ((ObjEnt *)((char *)obj->polys + j * 16))->p);
#else
        bone = *(char **)(j * 16 + (int)obj->polys);
#endif

        for (; *(int *)(i * 16 + (ICO_WORD)bone) >= 0;) {
            if ((id = *(int *)(i * 16 + (ICO_WORD)bone)) == *(short *)(shp + 4)) {
                w[n].no = *(int *)(j * 16 + (ICO_WORD)obj->polys + 4);
                w[n].weight = *(float *)(i * 16 + (ICO_WORD)bone + 4);
                if (n == 0) {
                    ret = id;
                    if ((unsigned int)ret >= obj->vtxCount)
                        pac_error("pac_getWeight(0)", 2);
                }
                if (++n >= 4)
                    pac_error("pac_getWeight(1)", 3);
            }
            i++;
        }
        i = 0;
    }
    if (ret == -1)
        pac_error("pac_getWeight(2)", 4);
    if (n == 3) {

        debug_StdPrintfDummy("vertex has 3 cluster-weights %d(%f) %d(%f) %d(%f)\n", w[0].no, w[0].weight, w[1].no, w[1].weight, w[2].no, w[2].weight);

        for (j = 0; j < n; j++) {
            for (i = j; i < n; i++) {
                if (j != i)
                    if (w[j].weight < w[i].weight) {
                        tmp = w[i];
                        w[i] = w[j];
                        w[j] = tmp;
                    }
            }
        }
        for (j = 2; j < n; j++)
            w[0].weight += w[j].weight;
    }

    sum = w[0].weight + w[1].weight;
    if (sum < 0.99f)
        debug_StdPrintfDummy("warning:weight total %f VtxIdx:%d\n", sum, ret);

    return ret;
}

/* clang-format on */

static int pac_makeClusterStrip(PObjPart *obj, short *strip, int num)
{
    PacWeight w[4];
    char buf[256];
    int i;
    short *v;
    char *vtx;
    char *nrm;
    char *uv;
    char *col;
    PacWork *ctx;

    vtx = obj->vtx;
    nrm = obj->nrm;
    uv = obj->uv;
    col = obj->col;
    ctx = &pacWork;
    *(int *)(strip - 6) = (ICO_PHYS(ctx->cursor.addr)) - ctx->dmaTag;
    for (i = 0, v = strip; i < num; i++, v += 8) {
        PacWork *ctx;
        int idx;

        idx = pac_getWeight(w, obj, (char *)v, num);
        ctx = &pacWork;
        pac_growBounds(ctx, vtx, idx);
        _CopyVector(ctx->cursor.v++, vtx + v[2] * 16);
        if (*(float *)(vtx + v[2] * 16 + 12) != 1.0f) {
            sprintf(buf, "\033[31msorce cluster model data broken! %f %f %f %f [%d]\033[0m\n",
                    *(float *)(vtx + v[2] * 16), *(float *)(vtx + v[2] * 16 + 4),
                    *(float *)(vtx + v[2] * 16 + 8), *(float *)(vtx + v[2] * 16 + 12), v[2]);
            debug_StdPrintfDummy(buf);
        }
        if ((int)(ctx->state.l >> 1) & 1)
            _CopyVector(ctx->cursor.v++, nrm + v[3] * 16);
        else
            pac_error("pac_makeClusterStrip", 1);
        *pacWork.cursor.i++ = w[0].no * 4 + 16;
        *pacWork.cursor.f++ = w[0].weight;
        *pacWork.cursor.i++ = w[1].no * 4 + 16;
        *pacWork.cursor.f++ = w[1].weight;
        if (w[0].no >= 60 || w[1].no >= 60) {
            debug_StdPrintfDummy("over 60 skeltons exist.\n");
            debug_assert("src/Packet.c", 917);
            __assert("src/Packet.c", 917, "0");
        }
        if (((int)(pacWork.state.l >> 3) & 1) && v[4] != -1) {
            *pacWork.cursor.f++ = *(float *)(uv + v[4] * 16) * obj->texDefs[v[7]].scaleU;
            *pacWork.cursor.f++ = *(float *)(uv + v[4] * 16 + 4) * obj->texDefs[v[7]].scaleV;
            *pacWork.cursor.f++ = 1.0f;
            *pacWork.cursor.f++ = i == 0 ? 0.0f : 1.0f;
        } else {
            pac_error("pac_makeClusterStrip", 5);
        }
        if (((int)(pacWork.state.l >> 4) & 1) && v[5] != -1) {
            if (col == 0) {
                debug_StdPrintfDummy("color table not exists.\n");
                debug_assert("src/Packet.c", 936);
                __assert("src/Packet.c", 936, "0");
            }
            *pacWork.cursor.f++ = (float)(unsigned char)col[v[5] * 4];
            *pacWork.cursor.f++ = (float)(unsigned char)col[v[5] * 4 + 1];
            *pacWork.cursor.f++ = (float)(unsigned char)col[v[5] * 4 + 2];
            *pacWork.cursor.f++ = 127.0f;
        } else {
            *pacWork.cursor.f++ = 128.0f;
            *pacWork.cursor.f++ = 128.0f;
            *pacWork.cursor.f++ = 128.0f;
            *pacWork.cursor.f++ = 127.0f;
        }
    }
    return num - 2;
}

static void pac_openDmaTag(ICO_WORD buf)
{
    register int mask = 0x0FFFFFFF;
    PacWork *ctx = &pacWork;
    float f0 = 16777215.0f;
    float f1 = -16777215.0f;
#ifdef ICO_HOST
    ctx->dmaTag = ICO_PHYS(buf);
    ctx->vifCode = ICO_PHYS(buf + 0x8);
    ctx->gifTag = ICO_PHYS(buf + 0x10);
#else
    ctx->dmaTag = buf & mask;
    ctx->vifCode = (buf + 0x8) & mask;
    ctx->gifTag = (buf + 0x10) & mask;
#endif
    ctx->cursor.addr = buf + 0x20;
    ctx->boxMin[2].f = f0;
    ctx->boxMin[1].f = f0;
    ctx->boxMin[0].f = f0;
    ctx->boxMax[2].f = f1;
    ctx->boxMax[1].f = f1;
    ctx->boxMax[0].f = f1;
#ifdef ICO_HOST
    debug_StdPrintfDummy("DMAOPEN   :%p\n", ICO_PHYS(buf));
#else
    debug_StdPrintfDummy("DMAOPEN   :%p\n", buf & mask);
#endif
}

static void pac_setVifCode(int num)
{
    PacWork *ctx = &pacWork;
    PAC_PTR(int *, ctx->vifCode)[0] = 0;
    PAC_PTR(int *, ctx->vifCode)[1] = (num << 16) | 0x6C008000;
    debug_StdPrintfDummy("VIFUNPACK :%08x %08x (%p:%d)\n", PAC_PTR(int *, ctx->vifCode)[0],
                         PAC_PTR(int *, ctx->vifCode)[1], PAC_PTR(void *, ctx->vifCode), num);
}

static void pac_setVifEndCode(void)
{
    PacWork *ctx = &pacWork;
    int *p = ctx->cursor.i;
    *p++ = 0x17000000;
    ctx->cursor.i = p;
    p[0] = 0;
    ctx->cursor.i = p + 1;
    p[1] = 0;
    ctx->cursor.i = p + 2;
    p[2] = 0;
    ctx->cursor.i = p + 3;
    debug_StdPrintfDummy((char *)(p + 3));
}

/* GIF tag template for the two texture-mapping modes: per mode the tag's
   FLG/NREG half, then the REGS descriptor. */
typedef struct { /* field names derived */
    unsigned long long tag;
    unsigned long long regs;
} GifTagTmpl; /* derived name */

/* One packet qword. The file writes this memory both as 32-bit VIF codes
   (pac_setVifCode, pac_setVifEndCode) and as a 64-bit GIF tag, so the
   packet word is a union of the two views. */
typedef union { /* field names derived */
    unsigned long long ul;
    unsigned int ui[2];
} PacketWord; /* derived name */

static const GifTagTmpl gifTagTmpl[2] = {
    /* derived name */
    {0x2000400000008000ULL, 0x51},
    {0x3000400000008000ULL, 0x512},
};

static void pac_setGifTag(PObjMaterial *mat, PObjTexInfo *tex, unsigned long long nloop)
{
    int abe;
    int tme;
    PacWork *ctx;

    if (tex == 0)
        abe = ((int)(mat->attr.bits >> 1) & 3) != 0;
    else if ((tex->found & 6) == 0)
        abe = ((int)(mat->attr.bits >> 1) & 3) != 0;
    else
        abe = 1;
    tme = tex->found & 1;
    ctx = &pacWork;
    PAC_PTR(PacketWord *, ctx->gifTag)
    [0].ul = gifTagTmpl[tme].tag |
             ((0xCULL | ((unsigned long long)tme << 4) | ((unsigned long long)abe << 6)) << 47) |
             nloop;
    PAC_PTR(PacketWord *, ctx->gifTag)[1].ul = gifTagTmpl[tme].regs;
    debug_StdPrintfDummy("GIFTAG    :");
    debug_StdPrintfDummy(PAC_PTR(char *, ctx->gifTag));
    debug_StdPrintfDummy(" (%d)\n", nloop);
}

/* the packet bytes pac_closeTag adds up, the polygons pac_makeStrip counts, the tags
   pac_closeTag and pac_continueTag open, the strips of the current chain
   (the "fchain" the divide messages print) and a word only pac_Init
   clears. pac_makePacket clears and reads the first three. */
static unsigned int pacPacketBytes; /* derived name */

static unsigned int pacPolyCount; /* derived name */

static unsigned int pacTagCount; /* derived name */

static unsigned int pacStripCount; /* derived name */

static int pacUnusedWord; /* derived name */

/* Zero the open DMA tag's two words.  The context pointer is read again for
   the second word: the first store may alias it. */
static inline void pac_closeDmaTag(void) /* derived name */
{
    PAC_PTR(unsigned int *, pacWork.dmaTag)[0] = 0;
    PAC_PTR(unsigned int *, pacWork.dmaTag)[1] = 0;
}

static int pac_closeTag(PObjMaterial *mat, PObjTexInfo *tex)
{
    PacWork *ctx;
    unsigned int n;
    unsigned int qwc;

    ctx = &pacWork;
    n = ((ICO_PHYS(ctx->cursor.addr)) - ctx->gifTag) >> 4;
    if (n == 1) {
        ctx->dmaTag = 0;
        ctx->vifCode = 0;
        ctx->gifTag = 0;
        return 0;
    }
    pac_setVifCode(n);
    pac_setGifTag(mat, tex,
                  (((((ICO_PHYS(ctx->cursor.addr)) - ctx->gifTag) >> 4) - 1) /
                   (unsigned int)ctx->counts.w[0]));
    pac_setVifEndCode();
    qwc = ((ICO_PHYS(ctx->cursor.addr)) - ctx->dmaTag) >> 4;
    pac_closeDmaTag();
    pacPacketBytes += qwc * 16;
    pacTagCount += 1;
    return qwc * 16;
}

/* Close the VIF list and reopen the DMA tag one qword further on. */
static inline void pac_continueDmaTag(void) /* derived name */
{
    PacWork *ctx = &pacWork;
    int *p = ctx->cursor.i;
    *p++ = 0x17000000;
    ctx->cursor.i = p;
    p[0] = 0;
    ctx->vifCode = (PacAddr)ICO_PHYS(ICO_ADDR(p + 1));
    ctx->gifTag = (PacAddr)ICO_PHYS(ICO_ADDR(p + 3));
    ctx->cursor.i = p + 7;
}

static void pac_continueTag(PObjMaterial *mat, PObjTexInfo *tex)
{
    PacWork *ctx;

    ctx = &pacWork;
    if (((ICO_PHYS(ctx->cursor.addr)) - ctx->gifTag) >> 4 == 1) {
        debug_StdPrintfDummy("pac_continueTag:Packet too small. %d\n", 0);
        debug_assert("src/Packet.c", 1147);
        __assert("src/Packet.c", 1147, "0");
    }
    pac_setVifCode(((ICO_PHYS(ctx->cursor.addr)) - ctx->gifTag) >> 4);
    pac_setGifTag(mat, tex,
                  (((((ICO_PHYS(ctx->cursor.addr)) - ctx->gifTag) >> 4) - 1) /
                   (unsigned int)ctx->counts.w[0]));
    pac_continueDmaTag();
    pacTagCount += 1;
}

/* 192 is the DMA chain's qword budget.  In the "gif over! cut!" message the
   last field is the qword count plus the gif tags the chain already holds
   plus the one about to be opened. */
static void pac_checkDivide(int num, PObjMaterial *mat, PObjTexInfo *tex)
{
    int limit = 192;
    PacWork *ctx;
    unsigned int qwc;

    ctx = &pacWork;
    if (ctx->counts.w[0] * num > limit) {
        debug_StdPrintfDummy("Original Strip Too Long. Size %d\n", ctx->counts.w[0] * num);
        debug_assert("src/Packet.c", 1172);
        __assert("src/Packet.c", 1172, "0");
    }
    qwc = ((ICO_PHYS(ctx->cursor.addr)) - ctx->gifTag) >> 4;
    if (qwc + ctx->counts.w[0] * num > limit) {
        pac_continueTag(mat, tex);
        debug_StdPrintfDummy("size(0x%x) strips(%d)\n",
                             ((ICO_PHYS(ctx->cursor.addr)) - ctx->gifTag) >> 4, pacStripCount);
        debug_StdPrintfDummy("--- cut ---\n\n");
        pacStripCount = 0;
    } else if ((qwc - 1) / (unsigned int)ctx->counts.w[0] * ctx->counts.w[1] + 1 +
                   ctx->counts.w[1] * num >
               limit) {
        debug_StdPrintfDummy(
            "gif over! cut! %d/%d polys:%d/%d fchain:%d vif+gif:%d\n", qwc + ctx->counts.w[0] * num,
            limit, pacPolyCount, ctx->counts.w[0], pacStripCount,
            qwc + ((qwc - 1) / (unsigned int)ctx->counts.w[0] * ctx->counts.w[1] + 1));
        pac_continueTag(mat, tex);
        debug_StdPrintfDummy("size(0x%x) strips(%d)\n",
                             ((ICO_PHYS(ctx->cursor.addr)) - ctx->gifTag) >> 4, pacStripCount);
        debug_StdPrintfDummy("--- cut ---\n\n");
        pacStripCount = 0;
    } else if (ctx->counts.w[0] * num >= 256) {
        debug_StdPrintfDummy("chain too long! cut!\n");
        pac_continueTag(mat, tex);
        debug_StdPrintfDummy("size(0x%x) strips(%d)\n",
                             ((ICO_PHYS(ctx->cursor.addr)) - ctx->gifTag) >> 4, pacStripCount);
        debug_StdPrintfDummy("--- cut ---\n\n");
        pacStripCount = 0;
    }
}

/* the material attribute word's microprogram-mode bit, read at word width */
typedef struct { /* field names derived */
    unsigned int mode : 1;
} PacMatMode; /* derived name */

/* Copy a finished packet down into a fresh seki-heap block.  The packet
   builder holds its packet addresses as words (pacWork's tags and cursor,
   pac_makeStrip's pkt and dst) and masks the segment bits off them, so the
   heap's pointers are kept as ints here and there. */
static inline ICO_WORD pac_moveToSeki(ICO_WORD src, int size) /* derived name */
{
    ICO_WORD p;

    p = (ICO_WORD)mallocseki(size);
    if (p == 0)
        debug_Assert("pac_copyStrip:No Enough Memory for Packet.\n");
    debug_StdPrintfDummy("ALL:src:%p => dst:%p (size:%x)\n", src, p, size);
    if (src != p)
        malloc_MemCpy((void *)p, (void *)src, size);
    return p;
}

static void pac_countOneVertexPacketSize(PObjMaterial *mat, PObjTexInfo *tex)
{
    {
        PacWork *ctx = &pacWork;
        ctx->counts.w[0] = 1;
        ctx->state.ul |= 1;
    }
    if (((int)(mat->attr.bits >> 5) & 3) != 0 || ((PacMatMode *)&mat->attr)->mode == 1 ||
        (tex != 0 && tex->texRef >= 0)) {
        PacWork *ctx = &pacWork;
        ctx->counts.w[0] += 1;
        ctx->state.ul |= 2;
    } else {
        PacWork *ctx = &pacWork;
        ctx->state.ul &= ~2;
    }
    if (((PacMatMode *)&mat->attr)->mode == 1) {
        PacWork *ctx = &pacWork;
        ctx->counts.w[0] += 1;
        ctx->state.ul |= 4;
    } else {
        PacWork *ctx = &pacWork;
        ctx->state.ul &= ~4;
    }
    if (((int)(mat->attr.bits >> 7) & 1) != 0) {
        PacWork *ctx = &pacWork;
        ctx->counts.w[0] += 1;
        ctx->state.ul |= 8;
    } else {
        pac_error("pac_countOneVertexPacketSize", 5);
    }
    if (((int)(mat->attr.bits >> 8) & 1) != 0) {
        PacWork *ctx = &pacWork;
        ctx->counts.w[0] += 1;
        ctx->state.ul |= 0x10;
    } else {
        PacWork *ctx = &pacWork;
        ctx->counts.w[0] += 1;
        ctx->state.ul &= ~0x10;
    }
    {
        PacWork *ctx = &pacWork;
        ctx->counts.w[1] = 3;
    }
}

static int pac_makeStrip(char **out, PObjPart *obj, PObjGroup *tbl, int matno, int texno,
                         PObjModel *mdl)
{
    char buf[1024];
    int num;
    ICO_WORD dst;
    PObjTexInfo *tex;
    PObjMaterial *mat;
    ICO_WORD pkt;
    short *p;
    int n;
    int i;
    int first;
    int size;
    int sz;
    int used;
    int packetSize;
    PacWork *ctx;
    float t0;

    num = obj->stripCount;
    dst = 0;
    tex = 0;
    mat = &tbl->materials[matno];
    pacStripCount = 0;
    if (texno != -1) {
        tex = &tbl->texs[texno];
    }
    pac_countOneVertexPacketSize(mat, tex);
    pkt = (ICO_WORD)mallocsekistage(1048576);
    if (pkt == 0)
        debug_Assert("pac_makeStrip:No Memory To Convert.\n");
    pac_openDmaTag(pkt);
    t0 = debug_GetTimerSec();
    for (i = 0; i < num; i++) {
        p = ((short **)obj->strips)[i];
        n = p[0];
        first = 0;
        while (n >= 3) {
            p += 8;
            if (p[7] == texno) {
                if (texno == -1)
                    debug_Assert("pac_makeStrip:No Tex Poly Exists.%s\n", mdl->name);
                if (p[6] == matno) {
                    if (first != 0)
                        pac_checkDivide(n, mat, tex);
                    else
                        first = 1;
                    if ((mat->attr.word & 1) == 0)
                        sz = pac_makeNormalStrip(obj, p, n);
                    else
                        sz = pac_makeClusterStrip(obj, p, n);
                    pacPolyCount += sz;
                    pacStripCount += 1;
                }
            }
            p += n * 8;
            n = p[0];
        }
    }
    inflateSec += debug_GetTimerSec() - t0;
    size = pac_closeTag(mat, tex);
    ctx = &pacWork;
    used = ctx->cursor.addr - pkt;
    if (maxPacketSize < used) {
        maxPacketSize = used;
        debug_StdPrintfDummy("\033[31mMaxPacketSize = %d\033[0m\n", used);
    }
    /* "IN OBJECT \"%s\"\nHUGE PACKET (SIZE:0x%x) APPEARED\nMAYBE INFLATE MEMORY AREA WAS BROKEN.\n" carries two conversions, "%s" for the object and "0x%x" for
       the size, so the recomputed size is sprintf's fourth argument. */
    packetSize = ctx->cursor.addr - pkt;
    if (1048576 < packetSize) {
        sprintf(
            buf,
            "IN OBJECT \"%s\"\nHUGE PACKET (SIZE:0x%x) APPEARED\nMAYBE INFLATE MEMORY AREA WAS BROKEN.\n",
            ctx->name, packetSize);
        debug_assertMessage("src/Packet.c", 1362, buf);
        __assert("src/Packet.c", 1362, "e");
    }
    if (size > 0) {
        if (malloc_GetPartition() == 0) {
            dst = pac_moveToSeki(ICO_PHYS(pkt), size);
            iosFree((void *)ICO_PHYS(pkt));
        } else {
#ifdef ICO_HOST
            dst = (ICO_WORD)reallocseki((void *)ICO_PHYS(pkt), size);
#else
            dst = reallocseki(ICO_PHYS(pkt), size);
#endif
        }
    } else {
        iosFree((void *)ICO_PHYS(pkt));
    }
    *out = (char *)dst;
    return size;
}

static void pac_setMaterialPacket(PObjMaterial *ent)
{
    char *p;

    p = (char *)ent;
    *(int *)p = 0;
    p += 4;
    *(int *)p = 0;
    p += 4;
    *(int *)p = 0;
    p += 4;
    *(int *)p = 0x6C048000;
    p += 4;
    *(long long *)p = 0x1000000000008003LL;
    p += 8;
    *(long long *)p = 14;
    p += 8;
    switch ((int)(ent->attr.bits >> 1) & 3) {
    case 2:
        *(long long *)p = 0x8000000048LL;
        p += 8;
        break;
    case 3:
        *(long long *)p = 0x8000000042LL;
        p += 8;
        break;
    case 1:
        *(long long *)p = 0x8000000044LL;
        p += 8;
        break;
    default:
        *(long long *)p = 0x8000000044LL;
        p += 8;
        break;
    }
    *(long long *)p = 0x42;
    p += 8;
    switch ((int)(ent->attr.bits >> 3) & 3) {
    case 0:
        *(long long *)p = 5;
        p += 8;
        break;
    case 1:
        *(long long *)p = 4;
        p += 8;
        break;
    case 2:
        *(long long *)p = 1;
        p += 8;
        break;
    default:
        *(long long *)p = 0;
        p += 8;
        break;
    }
    *(long long *)p = 8;
    p += 8;
    *(long long *)p = (int)(ent->attr.bits >> 9) & 1;
    p += 8;
    *(long long *)p = 0x4A;
    p += 8;
    *(int *)p = 0x14000000;
    p += 4;
    *(int *)p = 0;
    p += 4;
    *(int *)p = 0;
    *(int *)(p + 4) = 0;
}

static void pac_makeMaterialTable(PObjGroup *out, PObjPart *obj, int variant, int blend,
                                  unsigned int mode)
{
    PObjMaterial *tbl;
    PObjMaterial *ent;
    PObjMatDef *src;
    unsigned int i;
    unsigned int flag;
    int a;
    int x;

    tbl = mallocseki(obj->matCount * 112);
    for (i = 0; i < obj->matCount; i++) {
        ent = &tbl[i];
        /* the entry address is formed by hand: &obj->mats[i] moves the bytes
           (measured) */
        src = (PObjMatDef *)(i * 0x10 + (ICO_WORD)obj->mats);
        flag = src->alpha >= 0.501960814f;
        a = src->wrap;
        x = src->fbaOff == 0;
        if (mode != 0)
            x = debug_shadow_flag == 1;
        ent->attr.b.mode = mode;
        ent->attr.b.blend = flag * blend;
        ent->attr.b.wrap = (a < 4) ? a : 3;
        ent->attr.b.fba = x;
        ent->attr.b.variant = obj->nrm ? variant : 0;
        ent->attr.b.hasUv = obj->uv != 0;
        ent->attr.b.hasCol = obj->col != 0;
        pac_setMaterialPacket(ent);
    }
    out->materials = tbl;
    out->matCount = obj->matCount;
}

/* the line-primitive part's group record: its material table, its
   texture-info table and the two counts */
typedef struct MatLine {        /* field names derived */
    PObjMaterial *materials;    /* 0x00 */
    PObjTexInfo *texs;          /* 0x04 */
    struct PacLineSet *lineSet; /* 0x08 */
    short matCount;             /* 0x0C */
    short texCount;             /* 0x0E */
} MatLine;                      /* derived name */

static void pac_makeMaterialTableLine(MatLine *out, PObjPart *obj, int variant, int blend,
                                      unsigned int mode)
{
    PObjMaterial *tbl;
    PObjMaterial *ent;
    PObjMatDef *src;
    unsigned int i;
    unsigned int flag;
    short a;
    int x;

    tbl = mallocseki(obj->matCount * 112);
    for (i = 0; i < obj->matCount; i++) {
        ent = &tbl[i];
        src = (PObjMatDef *)(i * 0x10 + (ICO_WORD)obj->mats);
        flag = src->alpha >= 0.501960814f;
        a = src->wrap;
        x = src->fbaOff == 0;
        ent->attr.b.mode = mode;
        ent->attr.b.blend = flag * blend;
        if (a >= 4)
            a = 3;
        ent->attr.b.wrap = a;
        ent->attr.b.fba = x;
        ent->attr.b.variant = obj->nrm ? variant : 0;
        ent->attr.b.hasUv = obj->uv != 0;
        ent->attr.b.hasCol = obj->col != 0;
        pac_setMaterialPacket(ent);
    }
    out->materials = tbl;
    out->matCount = obj->matCount;
}

static void pac_getTextureInfo(PObjTexInfo *m, PObjPart *info, int idx)
{
    int n;

    if (idx != -1) {
        sprintf(m->name, "%s", info->texDefs[idx].name);
        m->tex = tex_GetTextureNo(m->name);
        tex_GetTextureData(m->tex);
        if (m->tex != -1) {
            m->found |= 1;
            sprintf(m->nameL, "%s_l", m->name);
            n = tex_GetTextureNo(m->nameL);
            if (n == -1) {
                m->texL = n;
                m->nameL[0] = 0;
                m->found &= 0xFFFD;
            } else {
                m->texL = tex_GetTextureNo(m->nameL);
                m->found |= 2;
            }
            sprintf(m->nameRef, "%s_ref", m->name);
            n = tex_GetTextureNo(m->nameRef);
            if (n == -1) {
                m->texRef = n;
                m->found &= 0xFFFB;
                m->nameRef[0] = 0;
            } else {
                m->texRef = tex_GetTextureNo(m->nameRef);
                m->found |= 4;
            }
        } else {
            debug_Assert("pac_makeTextureTable:\n\tTexture not Found. %s\n", m->name);
        }
    } else {
        m->found |= 1;
    }
}

/* One 16-byte shape-table qword, four ints. */
typedef struct { /* field names derived */
    int word[4];
} PacQw; /* derived name */

/* One 32-byte morph target entry (DisplayP2O.h's PObjMorph) as the copy
   loop sees it, 8-byte aligned so it moves in doublewords; a list of them
   ends with -1 in the index word at +0x10. */
typedef struct { /* field names derived */
    long long data[2];
    int index;
    int pad14[3];
} PacNode; /* derived name */

static void pac_makeShapeTable(PObjGroup *grp, PObjPart *obj)
{
    unsigned int i;
    int k;
    int cnt;
    int m;
    short *p;
    int n;
    PacQw **tbl;
    PacNode **ntbl;
    PacNode *dst;
    PacNode *r;
    PacNode *q;

    obj->vtxSave = mallocseki(obj->vtxCount * 16);
    for (i = 0; i < obj->vtxCount; i++)
        _CopyVector(obj->vtxSave + i * 16, obj->vtx + i * 16);
    obj->nrmSave = mallocseki(obj->nrmCount * 16);
    for (i = 0; i < obj->nrmCount; i++)
        _CopyVector(obj->nrmSave + i * 16, obj->nrm + i * 16);
    obj->vtx = mallocseki(obj->vtxCount * 16);
    for (i = 0; i < obj->vtxCount; i++)
        _CopyVector(obj->vtx + i * 16, obj->vtxSave + i * 16);
    obj->nrm = mallocseki(obj->nrmCount * 16);
    for (i = 0; i < obj->nrmCount; i++)
        _CopyVector(obj->nrm + i * 16, obj->nrmSave + i * 16);
    tbl = (PacQw **)mallocseki(obj->stripCount * sizeof(void *));
    for (i = 0; i < obj->stripCount; i++) {
        p = ((short **)obj->strips)[i];
        cnt = 0;
        n = p[0];
        while (n != 0) {
            p += n * 8 + 8;
            cnt += n + 1;
            n = p[0];
        }
        cnt++;
        tbl[i] = (PacQw *)mallocseki(cnt * 16);
        for (k = 0; k < cnt; k++)
            tbl[i][k] = ((PacQw **)obj->strips)[i][k];
    }
    obj->strips = tbl;
    ntbl = (PacNode **)mallocseki(obj->morphCount * sizeof(void *));
    for (i = 0; i < obj->morphCount; i++) {
        ntbl[i] = 0;
        q = ((PacNode **)obj->morphs)[i];
        if (q != 0) {
            r = q;
            for (m = 0; r->index != -1; r++)
                m++;
            m += 2;
            ntbl[i] = (PacNode *)mallocseki(m * 32);
            for (r = ((PacNode **)obj->morphs)[i], dst = ntbl[i];; r++, dst++) {
                *dst = *r;
                if (r->index == -1)
                    break;
            }
        }
    }
    obj->morphs = (PObjMorph **)ntbl;
}

/* one line of a line part, 80 bytes: its two vertex indices, two uv indices
   and two colour indices, the vertex count (1 a point, 2 a line), the
   material and the texture slot (-1 for none) */
typedef struct PObjLine { /* field names derived */
    int vtx[2];           /* 0x00 */
    char pad08[24];
    int uv[2]; /* 0x20 */
    char pad28[8];
    int col[2]; /* 0x30 */
    char pad38[8];
    int num; /* 0x40 */
    char pad44[4];
    int mat; /* 0x48 */
    int tex; /* 0x4C */
} PObjLine;  /* derived name */

/* clears the running packet byte counter before a build */
static inline void pac_resetPacketCount(void) /* derived name */
{
    pacPacketBytes = 0;
}

/* allocates and fills a material's texture-info table
   for the strip path */
static inline void pac_makeTextureTable(PObjGroup *dst, PObjPart *src) /* derived name */
{
    PObjTexInfo *tex;
    unsigned int i;

    tex = mallocseki(src->texCount * 80);
    for (i = 0; i < src->texCount; i++)
        pac_getTextureInfo(&tex[i], src, i);
    dst->texs = tex;
    dst->texCount = src->texCount;
}

/* the line-primitive counterpart; it writes the count into
   the line header's own halfword at +0xE */
static inline void pac_makeTextureTableLine(MatLine *dst, PObjPart *src) /* derived name */
{
    PObjTexInfo *tex;
    unsigned int i;

    tex = mallocseki(src->texCount * 80);
    for (i = 0; i < src->texCount; i++)
        pac_getTextureInfo(&tex[i], src, i);
    dst->texs = tex;
    dst->texCount = src->texCount;
}

/* prev builds the strip chain and then walks it for the clone, j counts the
   materials, m the texture slots, and j then the line records; out is the packet address pac_makeStrip
   returns. */
static void pac_makePacket(PObjModel *obj, int variant, int mode)
{
    char *out;
    int lod;
    PObjGroup *tbl;
    MatLine *mtbl;
    int nmat;
    PObjPart *src;
    PacHeader *node;
    PacHeader *prev;
    PacHeader *p;
    PacHeader *last;
    PacLine *top;
    PObjLine *line;
    char *vtx;
    char *uv;
    char *idx;
    int i;
    int j;
    int m;
    int ntex;
    int sz;

    lod = obj->mode.s.lod;
    pac_resetPacketCount();
    tbl = 0;
    mtbl = 0;
    obj->mode.s.type = 0 < obj->disp;
    if (obj->parts->lineCount != 0)
        obj->mode.s.type = 2;
    /* the display type, bits 16 and 17 of the mode word */
    if (((unsigned short)(obj->mode.bits >> 16) & 3) == 2) {
        mtbl = mallocseki(obj->partCount * 16);
        obj->groups = (PObjGroup *)mtbl;
    } else {
        tbl = mallocseki(obj->partCount * 48);
        obj->groups = tbl;
        sprintf(tbl->name, "%s", obj->name);
    }
    sprintf(pacWork.name, "%s", obj->name);
    if (obj->mode.s.type != 2) {
        for (i = 0; i < obj->partCount; i++) {
            prev = 0;
            src = &obj->parts[i];
            ntex = src->texCount;
            nmat = src->matCount;
            pac_makeMaterialTable(tbl, src, variant, lod, mode);
            pac_makeTextureTable(tbl, src);
            if (src->morphCount != 0)
                pac_makeShapeTable(tbl, src);
            if (src->mats == 0) {
                debug_StdPrintfDummy("pac_makePacket:Material Table Not Found. (%s:%s)\n",
                                     obj->name, src);
                debug_assert("src/Packet.c", 1732);
                __assert("src/Packet.c", 1732, "0");
            }
            for (j = 0; j < nmat; j++) {
                for (m = -1; m < ntex; m++) {
                    out = 0;
                    pacTagCount = 0;
                    pacPolyCount = 0;
                    sz = pac_makeStrip(&out, src, tbl, j, m, obj);
                    if (sz > 0) {
                        node = mallocseki(160);
                        node->mat = j;
                        node->texSlot = m;
                        node->tex = tbl->texs[m].tex;
                        node->tex1 = tbl->texs[m].texL;
                        node->tex2 = tbl->texs[m].texRef;
                        node->npoly = pacPolyCount;
                        node->ntag = (unsigned short)pacTagCount;
                        node->data = out;
                        node->size = sz;
                        node->clip = obj->mode.s.shade;
                        node->next = prev;
                        pac_makeBoundingBox(node->box, obj->mode.s.type == 1);
                        prev = node;
                    } else if (sz < 0) {
                        debug_StdPrintfDummy("illegal size = %d\n", sz);
                        debug_assert("src/Packet.c", 1762);
                        __assert("src/Packet.c", 1762, "0");
                    }
                }
            }
            tbl->packets = prev;
            if (src->morphCount != 0) {
                p = mallocseki(160);
                p->next = 0;
                prev = tbl->packets;
                tbl->morph = p;
                do {
                    malloc_MemCpy(p, prev, 160);
                    p->data = mallocseki(prev->size);
                    p->size = prev->size;
                    malloc_MemCpy(p->data, prev->data, prev->size);
                    prev = prev->next;
                    if (prev != 0) {
                        last = p;
                        p = mallocseki(160);
                        p->next = 0;
                        last->next = p;
                    }
                } while (prev != 0);
            } else {
                tbl->morph = 0;
            }
            tbl++;
        }
    } else {
        PObjPart *src;
        PacLine *p;

        for (i = 0; i < obj->partCount; i++) {
            src = &obj->parts[i];
            line = src->lines;
            p = mallocseki((src->lineCount + 1) * 192);
            vtx = src->vtx;
            uv = src->uv;
            idx = src->col;
            pac_makeMaterialTableLine(mtbl, src, variant, lod, mode);
            pac_makeTextureTableLine(mtbl, src);
            obj->mode.s.type = 2;
            top = p;
            for (j = 0; j < src->lineCount; j++) {
                p->attr.b.type = line->num;
                switch (p->attr.b.type) {
                case 1:
                    _CopyVector(p->pos[0], vtx + line->vtx[0] * 16);
                    p->col0 = ((PacColor *)idx)[line->col[0]];
                    p->attr.b.blend = 0.5019608f <= src->mats[line->mat].alpha;
                    break;
                case 2:
                    _CopyVector(p->pos[0], vtx + line->vtx[0] * 16);
                    _CopyVector(p->pos[1], vtx + line->vtx[1] * 16);
                    p->uv[0][0] = *(float *)(uv + line->uv[0] * 16);
                    p->uv[0][1] = *(float *)(uv + line->uv[0] * 16 + 4);
                    p->uv[1][0] = *(float *)(uv + line->uv[1] * 16);
                    p->uv[1][1] = *(float *)(uv + line->uv[1] * 16 + 4);
                    p->uv[0][2] = 1.0f;
                    p->uv[1][3] = 0.0f;
                    p->col0 = ((PacColor *)idx)[line->col[0]];
                    p->col1 = ((PacColor *)idx)[line->col[1]];
                    p->attr.b.blend = 0.5019608f <= src->mats[line->mat].alpha;
                    if (line->tex >= 0)
                        p->attr.b.tex = mtbl->texs[line->tex].tex;
                    else
                        p->attr.b.tex = -1;
                    break;
                default:
                    debug_StdPrintfDummy("illegal vertex num %d\n", line->num);
                    debug_assert("src/Packet.c", 1845);
                    __assert("src/Packet.c", 1845, "0");
                    break;
                }
                line++;
                p++;
            }
            p[src->lineCount].attr.b.type = 0;
            mtbl->lineSet = mallocseki(144);
            mtbl->lineSet->lines = top;
            mtbl++;
        }
    }
}

void pac_MakePacket(Sub15C *o)
{
    PObjModel *p = o->model;
    pac_makePacket(p, o->lightMtx->mode, p->disp > 0);
}

inline void pac_Init(void)
{
    pacUnusedWord = 0;
}
