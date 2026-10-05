#include "debug.h"
#include "Basic.h"
#include "memory.h"
#include "DisplayList.h"
#include "MicroCode.h"
#include "RegistPacket.h"
#include "Texture.h"
#include "delayFreeManager.h"
#include "lineManager.h"
#include "tableSin.h"
#include <string.h>
#include <stdio.h>
#include "GsBase.h"
#include "Matrix.h"
#include "main.h"
#include "debug_exception.h"
#include "GifPacket.h"
#include "Primitive.h"
#include "DmaPacket.h"
#include "ios.h"
#include <assert.h>

#ifdef ICO_RD

#include "GifHost.h"
#include "rd_mesh.h"

/* PC port (renderer wave 3, R3ab; docs/port/RENDER_API.md section 13): the
   VU1 chains this file builds also go through the host's VIF reader
   (mc_HostDma): prim_DispFan2D's SET_GSREGISTER fan reaches the GS register
   decoder, prim_DispMesh3D's matrix, light and UV packets and
   prim_DispParticle's matrix packet the list's VU state, and a particle
   batch's MSCNT draws it (rd_DrawVuParticles).  A Mesh3D packet buffer is
   drawn whole (primHostGrid, rd_DrawVuGrid). */
static void primHostGrid(Mesh3D *m)
{
    unsigned long long prim[2];
    unsigned long long tag;
    RdVuDraw d;
    RdVuGridDraw g;

    if (m->strips <= 0 || m->stripLen < 3) {
        return;
    }
    /* every strip's GIF tag (PRE) writes this PRIM (prim_InitMesh3D's) */
    memcpy(&tag, (char *)m->bufs[buffer_ID] + 0x10, 8);
    prim[0] = (tag >> 47) & 0x7FF;
    prim[1] = 0;
    gif_HostWriteRegs(prim, 1);
    if (!rd_VuDrawFromState(&d)) {
        return;
    }
    memset(&g, 0, sizeof(g));
    g.qw = (const float (*)[4])m->bufs[buffer_ID];
    g.strips = (uint32_t)m->strips;
    g.stripLen = (uint32_t)m->stripLen;
    g.lit = m->lit != 0;
    g.code = d.code;
    g.vu = d.vu;
    rd_DrawVuGrid(&g, RD_KEY(m, rd_CurrentList(), 0));
}

#endif

Fan2D *prim_InitFan2D(int n, float r, float *pos, unsigned int cc, unsigned int rc)
{
    Fan2D *f;
    Fan2DVtx *q;
    Fan2DVtx *first;
    int i;

    f = (Fan2D *)iosMallocDebug(ios_partition_seki, sizeof(Fan2D) > 12 ? sizeof(Fan2D) : 12,
                                "src/Primitive.c", 318);
    f->buf = (Fan2DVtx *)iosMallocDebug(ios_partition_seki, (n + 2) * 32, "src/Primitive.c", 319);
    q = f->buf;

    f->n = n;
    f->blend = 0;
    q->cr = cc >> 24;
    q->cg = (cc >> 16) & 0xFF;
    q->cb = (cc >> 8) & 0xFF;
    q->ca = cc & 0xFF;
    if ((cc & 0xFF) != 128) {
        f->blend = 1;
    }
    q->x = pos[0] + center_X;
    q->y = pos[1] + center_Y;
    q->z = -pos[2];
    q->w = 1.0f;
    q++;

    first = q;
    for (i = 0; i < n; i++) {
        q->x = r * GetTableCos((short)((float)i * 6.2831855f / (float)f->n * 10430.3779f)) +
               pos[0] + center_X;
        q->y = r * GetTableSin((short)((float)i * 6.2831855f / (float)f->n * 10430.3779f)) * 0.5f +
               pos[1] + center_Y;
        q->z = -pos[2];
        q->w = 1.0f;
        q->cr = rc >> 24;
        q->cg = (rc >> 16) & 0xFF;
        q->cb = (rc >> 8) & 0xFF;
        q->ca = rc & 0xFF;
        if ((rc & 0xFF) != 128) {
            f->blend = 1;
        }
        q++;
    }
    *q = *first;
    return f;
}

void prim_SetFan2D(Fan2D *f, float r, float *pos, unsigned int cc, unsigned int rc)
{
    Fan2DVtx *q;
    Fan2DVtx *first;
    int i;

    q = f->buf;
    q->cr = cc >> 24;
    q->cg = (cc >> 16) & 0xFF;
    q->cb = (cc >> 8) & 0xFF;
    q->ca = cc & 0xFF;
    q->x = pos[0] + center_X;
    q->y = pos[1] + center_Y;
    q->z = -pos[2];
    q->w = 1.0f;
    q++;

    first = q;
    for (i = 0; i < f->n; i++) {
        q->x = r * GetTableCos((short)((float)i * 6.2831855f / (float)f->n * 10430.3779f)) +
               pos[0] + center_X;
        q->y = r * GetTableSin((short)((float)i * 6.2831855f / (float)f->n * 10430.3779f)) *
                   ((float)ScreenHeight * 4.0f / ((float)ScreenWidth * 3.0f)) +
               pos[1] + center_Y;
        q->z = -pos[2];
        q->w = 1.0f;
        q->cr = rc >> 24;
        q->cg = (rc >> 16) & 0xFF;
        q->cb = (rc >> 8) & 0xFF;
        q->ca = rc & 0xFF;
        q++;
    }
    *q = *first;
}

void prim_DispFan2D(Fan2D *f, int mode)
{
    int v[4];
    Fan2DVtx *q;
    char *p;
    char *pp;
    char *n;
    char *m;
    char *end;
    char *gif;
    char *tail;
    int i;
    int kick = 3;

    q = f->buf;
    for (i = 0; i < f->n + 2; i++) {
        if (q->x < 0.0f) {
            return;
        }
        if (q->x > 4095.0f) {
            return;
        }
        if (q->y < 0.0f) {
            return;
        }
        if (q->y > 4095.0f) {
            return;
        }
        if (q->z < 0.0f) {
            return;
        }
        q++;
    }

    q = f->buf;
    {
        /* The header is written through a block-scoped handle on the packet
           context: one address materialisation covers the whole region,
           including the tag if/else. */
        DpkCtl *d = &PacketBufferStruct;

        p = d->ptr.c;
        d->dma.c = p;
        d->tail.c = p;
        d->gif.c = 0;
        d->end.c = 0;
        d->ptr.c = p + 8;
        ((GifPkWord *)(p + 8))->w[0] = 0x11000000;
        d->gif.c = p + 0xC;
        d->end.c = p + 0x10;
        d->ptr.c = p + 0x18;
        ((GifPkWord *)(p + 0x18))->d = 0xE;
        d->ptr.c = p + 0x20;
        if (mode == 0) {
            ((GifPkWord *)(p + 0x20))->d = ((long long)f->blend << 6) | 0x10D;
            d->ptr.c = p + 0x28;
            ((GifPkWord *)(p + 0x28))->d = 0;
            d->ptr.c = p + 0x30;
        } else {
            ((GifPkWord *)(p + 0x20))->d = ((long long)f->blend << 6) | 0x10A;
            d->ptr.c = p + 0x28;
            ((GifPkWord *)(p + 0x28))->d = 0;
            d->ptr.c = p + 0x30;
        }
    }

    for (i = 0; i < f->n + 2; i++) {
        _FTOI4Vector(v, &q->x);
        pp = PacketBufferStruct.ptr.c;
        ((GifPkWord *)pp)->d = ((long long)q->cr | ((long long)q->cg << 8) |
                                ((long long)q->cb << 16) | ((long long)q->ca << 24)) |
                               ((long long)0x3F800000 << 32);
        pp += 8;
        PacketBufferStruct.ptr.c = pp;
        ((GifPkWord *)pp)->d = 1;
        PacketBufferStruct.ptr.c = pp + 8;
        if (q->z < 0.0f) {
            kick = 3;
        }
        kick--;
        if (kick > 0) {
            ((GifPkWord *)(pp + 8))->d =
                (long long)v[0] | ((long long)v[1] << 16) | ((long long)v[2] << 32);
            PacketBufferStruct.ptr.c = pp + 0x10;
            ((GifPkWord *)(pp + 0x10))->d = 0xD;
            PacketBufferStruct.ptr.c = pp + 0x18;
        } else {
            ((GifPkWord *)(pp + 8))->d =
                (long long)v[0] | ((long long)v[1] << 16) | ((long long)v[2] << 32);
            PacketBufferStruct.ptr.c = pp + 0x10;
            ((GifPkWord *)(pp + 0x10))->d = 5;
            PacketBufferStruct.ptr.c = pp + 0x18;
        }
        q++;
    }

    end = PacketBufferStruct.end.c;
    ((GifPkWord *)end)->d =
        (unsigned int)(((unsigned int)(PacketBufferStruct.ptr.c - end) >> 4) - 1) |
        0x1000000000008000LL;
    gif = PacketBufferStruct.gif.c;
    ((GifPkWord *)gif)->w[0] =
        (((unsigned int)(PacketBufferStruct.ptr.c - gif) >> 4) << 16) | 0x6C008000;

    n = PacketBufferStruct.ptr.c;
    ((GifPkWord *)n)->w[0] = 0x15000000;
    n += 4;
    PacketBufferStruct.ptr.c = n;
    ((GifPkWord *)n)->w[0] = 0;
    PacketBufferStruct.ptr.c = n + 4;
    ((GifPkWord *)n)->w[1] = 0;
    PacketBufferStruct.ptr.c = n + 8;
    ((GifPkWord *)(n + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = n + 0xC;

    tail = PacketBufferStruct.tail.c;
    ((GifPkWord *)tail)->d =
        (unsigned int)(((unsigned int)(PacketBufferStruct.ptr.c - tail) >> 4) - 1) | 0x10000000;

    m = PacketBufferStruct.ptr.c;
    PacketBufferStruct.tail.c = m;
    ((GifPkWord *)m)->d = 0x60000000;
    PacketBufferStruct.ptr.c = m + 8;
    ((GifPkWord *)(m + 8))->w[0] = 0;
    PacketBufferStruct.ptr.c = m + 0xC;
    ((GifPkWord *)(m + 8))->w[1] = 0;
    PacketBufferStruct.ptr.c = m + 0x10;

    dl_OpenDma(5, PacketBufferStruct.dma.c, 0);
    dl_CloseDma();
#ifdef ICO_RD
    mc_HostDma(5, PacketBufferStruct.dma.c, 0);
#endif
}

typedef ICO_QW Qw128; /* derived name */

#ifdef ICO_HOST

#include "ee_view.h"

/* PC port: the mesh code moves its vertices (Prim3DVec) as quadwords; the
   sizes must agree (tools/template_audit.py) */
ICO_LAYOUT_SIZE(Qw128, Prim3DVec);

#endif

/* The mesh strip's GIF tag template: NLOOP and PRIM are ORed in per strip.
   prim_makePacketMesh3D reads it by pointer dereference. */
static const long long meshGifTag[2] = {0x3000400000008000LL, 0x512}; /* derived name */

static void prim_makePacketMesh3D(Mesh3D *m, void *pkt, int uv)
{
    Prim3DVec t;
    Prim3DVec nv = {0.0f, 0.0f, 0.0f, 1.0f};
    Prim3DVec tv = {0.0f, 0.0f, 0.0f, 1.0f};
    Prim3DVec c = {(float)((m->col >> 24) & 0xFF), (float)((m->col >> 16) & 0xFF),
                   (float)((m->col >> 8) & 0xFF), (float)(m->col & 0xFF)};
    char *p;
    int i;
    int k;

    p = (char *)pkt;
    if (c.w == 128.0f) {
        c.w = 127.0f;
    }
    _SetCurrentMatrix(m->mtx);
    for (i = 0; i < m->strips; i++) {
        int w = m->stripLen;
        int n = w * (m->lit + 2) + 2;
        long long reg = m->prim;
        DpkHead *h;

        if (n >= 253) {
            debug_StdPrintfDummy("too large mesh packet. %d\n", w);
            debug_assert("src/Primitive.c", 473);
            __assert("src/Primitive.c", 473, "0");
        }
        h = (DpkHead *)p;
        h->vif[0] = 0;
        h->vif[1] = 0;
        h->vif[2] = 0;
        h->vif[3] = (n << 16) | 0x6C008000;
        h->tag[0] = w | (*meshGifTag | (reg << 47));
        h->tag[1] = *(meshGifTag + 1);
        p += 0x20;
        *(Qw128 *)p = *(Qw128 *)&c;
        p += 0x10;
        for (k = 0; k < m->stripLen; k++) {
            *(Qw128 *)p = *(Qw128 *)&nv;
            p += 0x10;
            if (uv != 0) {
                *(Qw128 *)p = *(Qw128 *)&tv;
                p += 0x10;
            }
            {
                int nx = m->nx;
                int idx = (i + (k & 1)) * nx + (k >> 1);
                int u = idx / nx;
                int v = idx % nx;

                t.z = 1.0f;
                t.w = 0.0f;
                t.x = (float)v / (float)(nx - 1);
                t.y = (float)u / (float)(m->ny - 1);
                _ApplyCurrentMatrix(&t, &t);
                *(Qw128 *)p = *(Qw128 *)&t;
                p += 0x10;
            }
        }
        ((int *)p)[0] = 0x17000000;
        ((int *)p)[1] = 0;
        ((int *)p)[2] = 0;
        ((int *)p)[3] = 0;
        p += 0x10;
    }
}

Mesh3D *prim_InitMesh3D(int nx, int ny, int rot, long long col, unsigned int col2, int lit)
{
    Mesh3D *m;
    int i;

    m = (Mesh3D *)iosMallocDebug(ios_partition_seki, sizeof(Mesh3D) > 144 ? sizeof(Mesh3D) : 144,
                                 "src/Primitive.c", 576);
    m->nx = nx;
    m->ny = ny;
    m->pos =
        (Prim3DVec *)iosMallocDebug(ios_partition_seki, m->nx * 16 * m->ny, "src/Primitive.c", 579);
    m->nrm =
        (Prim3DVec *)iosMallocDebug(ios_partition_seki, m->nx * 16 * m->ny, "src/Primitive.c", 580);
    m->st =
        (Prim3DVec *)iosMallocDebug(ios_partition_seki, m->nx * 16 * m->ny, "src/Primitive.c", 581);
    for (i = 0; i < m->nx * m->ny; i++) {
        m->pos[i].x = m->pos[i].y = m->pos[i].z = 0.0f;
        m->pos[i].w = 1.0f;
        m->nrm[i].x = m->nrm[i].y = m->nrm[i].z = 0.0f;
        m->nrm[i].w = 0.0f;
        m->st[i].x = m->st[i].y = m->st[i].w = 0.0f;
        m->st[i].z = 1.0f;
    }
    m->stripLen = m->nx * 2;
    m->strips = m->ny - 1;

    m->lit = lit;

    m->qwc = m->strips * (m->stripLen * (lit + 2) + 4);

    m->bufs[0] = iosMallocDebug(ios_partition_seki, m->qwc * 16, "src/Primitive.c", 597);
    m->bufs[1] = iosMallocDebug(ios_partition_seki, m->qwc * 16, "src/Primitive.c", 598);

    _InitCurrentMatrix();
    _RotCurrentMatrixZ((short)((float)(rot % 4) * 3.1415927f * 0.5f * 10430.3779f));
    _GetCurrentMatrix(m->mtx);

    m->prim = col;
    m->col = col2;
    prim_makePacketMesh3D(m, m->bufs[0], m->lit);
    prim_makePacketMesh3D(m, m->bufs[1], m->lit);
    return m;
}

/* prim_makeNormal fills the per-vertex normal buffer at Mesh3D+0x70.  The
   four neighbours of a vertex are walked with one pair of indices, x and y,
   reassigned for each neighbour and wrapped when the mesh is closed in that
   direction. */

static void prim_makeNormal(Mesh3D *m)
{
    int f[4];
    Prim3DVec n;
    Prim3DVec a;
    Prim3DVec b;
    Prim3DVec c;
    Prim3DVec d;
    Prim3DVec e0;
    Prim3DVec e1;
    Prim3DVec e2;
    Prim3DVec e3;
    int i;
    int j;
    int x;
    int y;
    int cnt;
    float s;

    _PushCurrentMatrix();
    _SetCurrentMatrix(matrixptr + 0x80);
    for (i = 0; i < m->ny; i++) {
        for (j = 0; j < m->nx; j++) {
            f[0] = f[1] = f[2] = f[3] = 0;
            x = j - 1;
            y = i;
            if (x < 0) {
                if (m->wrapX != 0) {
                    x = m->nx - 1;
                }
            }
            if (x >= 0) {
                _SubVector(&e0, &m->pos[y * m->nx + x], &m->pos[i * m->nx + j]);
                f[0] = 1;
            }
            x = j;
            y = i - 1;
            if (y < 0) {
                if (m->wrapY != 0) {
                    y = m->ny - 1;
                }
            }
            if (y >= 0) {
                _SubVector(&e1, &m->pos[y * m->nx + x], &m->pos[i * m->nx + j]);
                f[1] = 1;
            }
            x = j + 1;
            y = i;
            if (x >= m->nx) {
                if (m->wrapX != 0) {
                    x = 0;
                }
            }
            if (x < m->nx) {
                _SubVector(&e2, &m->pos[y * m->nx + x], &m->pos[i * m->nx + j]);
                f[2] = 1;
            }
            x = j;
            y = i + 1;
            if (y >= m->ny) {
                if (m->wrapY != 0) {
                    y = 0;
                }
            }
            if (y < m->ny) {
                _SubVector(&e3, &m->pos[y * m->nx + x], &m->pos[i * m->nx + j]);
                f[3] = 1;
            }
            n.x = n.y = n.z = n.w = 0.0f;
            cnt = 0;
            if (f[0] != 0 && f[1] != 0) {
                _OuterProduct(&a, &e0, &e1);
                _AddVector(&n, &n, &a);
                cnt++;
            }
            if (f[1] != 0 && f[2] != 0) {
                _OuterProduct(&b, &e1, &e2);
                _AddVector(&n, &n, &b);
                cnt++;
            }
            if (f[2] != 0 && f[3] != 0) {
                _OuterProduct(&c, &e2, &e3);
                _AddVector(&n, &n, &c);
                cnt++;
            }
            if (f[3] != 0 && f[0] != 0) {
                _OuterProduct(&d, &e3, &e0);
                _AddVector(&n, &n, &d);
                cnt++;
            }
            s = 1.0f / cnt;
            m->nrm[i * m->nx + j].x = n.x * s;
            m->nrm[i * m->nx + j].y = n.y * s;
            m->nrm[i * m->nx + j].z = n.z * s;
            m->nrm[i * m->nx + j].w = 0.0f;
            _NormalizeVector(&m->nrm[i * m->nx + j], &m->nrm[i * m->nx + j]);
            _ApplyCurrentMatrix(&a, &m->nrm[i * m->nx + j]);
            b.x = b.y = 0.0f;
            b.z = b.w = 1.0f;
            a.z = _InnerProduct(&b, &a);
            if (a.z > 0.0f) {
                _ScaleVector(&m->nrm[i * m->nx + j], &m->nrm[i * m->nx + j], -1.0f);
            }
            m->nrm[i * m->nx + j].w = 0.0f;
        }
    }
    _PopCurrentMatrix();
}

void prim_UpdateMesh3D(Mesh3D *m, int flags, int idx)
{
    Qw128 *p;
    int i;
    int j;

    if (m->lit != 0 && (flags & 4) != 0) {
        prim_makeNormal(m);
    }
    p = (Qw128 *)m->bufs[idx];
    for (i = 0; i < m->strips; i++) {
        p += 2;
        if (flags & 0x10) {
            Prim3DVec c = {(float)((m->col >> 24) & 0xFF), (float)((m->col >> 16) & 0xFF),
                           (float)((m->col >> 8) & 0xFF), (float)(m->col & 0xFF)};

            if (c.w < 0.0f) {
                c.w = 0.0f;
            }
            if (c.w >= 128.0f) {
                c.w = 127.0f;
            }
            *p = *(Qw128 *)&c;
        }
        p++;
        for (j = 0; j < m->stripLen; j++) {
            if (flags & 1) {
                *p = *(Qw128 *)&m->pos[(i + (j & 1)) * m->nx + (j >> 1)];
            }
            p++;
            if (m->lit != 0) {
                if (flags & 6) {
                    *p = *(Qw128 *)&m->nrm[(i + (j & 1)) * m->nx + (j >> 1)];
                }
                p++;
            }
            if (flags & 8) {
                *p = *(Qw128 *)&m->st[(i + (j & 1)) * m->nx + (j >> 1)];
            }
            p++;
        }
        p++;
    }
}

/* setMatrix, setLight and clearUVOffset were GNU nested functions of
   prim_DispMesh3D; setLight took the parent's two light arguments as parameters.  A matrix
   or vector copied into the packet takes the cursor post-incremented as its
   destination, `_CopyMatrix(((float (*)[16])dd->ptr.c)++, m)`, and every
   packet copy here is spelled the same way. */

static void setMatrix(void)
{
    float mtx[16];
    DpkCtl *dd;
    char *q;
    char *r;

    _GetCurrentMatrix(mtx);
    dd = &PacketBufferStruct;
    q = dd->ptr.c;
    dd->tail.c = q;
    ((GifPkWord *)q)->d = 0x10000005;
    dd->ptr.c = q + 8;
    ((GifPkWord *)(q + 8))->w[0] = 0;
    dd->ptr.c = q + 0xC;
    dd->gif.c = q + 0xC;
    ((GifPkWord *)(q + 0xC))->w[0] = 0x6C048000;
    dd->ptr.c = q + 0x10;
    _CopyMatrix(ICO_POSTINC(float (*)[16], dd->ptr.c), mtx);
    r = dd->ptr.c;
    ((GifPkWord *)r)->w[0] = 0x15000010;
    r += 4;
    dd->ptr.c = r;
    ((GifPkWord *)r)->w[0] = 0;
    dd->ptr.c = r + 4;
    ((GifPkWord *)(r + 4))->w[0] = 0;
    dd->ptr.c = r + 8;
    ((GifPkWord *)(r + 8))->w[0] = 0;
    dd->ptr.c = r + 0xC;
}

static void setLight(void *la, void *lb)
{
    DpkCtl *dd;
    char *q;
    char *r;

    dd = &PacketBufferStruct;
    q = dd->ptr.c;
    dd->tail.c = q;
    ((GifPkWord *)q)->d = 0x10000009;
    dd->ptr.c = q + 8;
    ((GifPkWord *)(q + 8))->w[0] = 0;
    dd->ptr.c = q + 0xC;
    dd->gif.c = q + 0xC;
    ((GifPkWord *)(q + 0xC))->w[0] = 0x6C088000;
    dd->ptr.c = q + 0x10;
    _CopyMatrix(ICO_POSTINC(float (*)[16], dd->ptr.c), lb);
    _CopyMatrix(ICO_POSTINC(float (*)[16], dd->ptr.c), la);
    r = dd->ptr.c;
    ((GifPkWord *)r)->w[0] = 0x15000012;
    r += 4;
    dd->ptr.c = r;
    ((GifPkWord *)r)->w[0] = 0;
    dd->ptr.c = r + 4;
    ((GifPkWord *)(r + 4))->w[0] = 0;
    dd->ptr.c = r + 8;
    ((GifPkWord *)(r + 8))->w[0] = 0;
    dd->ptr.c = r + 0xC;
}

static void clearUVOffset(void)
{
    float v[4];
    DpkCtl *dd;
    char *q;
    char *r;

    memset(v, 0, 16);
    dd = &PacketBufferStruct;
    q = dd->ptr.c;
    dd->tail.c = q;
    ((GifPkWord *)q)->d = 0x10000002;
    dd->ptr.c = q + 8;
    ((GifPkWord *)(q + 8))->w[0] = 0;
    dd->ptr.c = q + 0xC;
    dd->gif.c = q + 0xC;
    ((GifPkWord *)(q + 0xC))->w[0] = 0x6C018000;
    dd->ptr.c = q + 0x10;
    _CopyVector(ICO_POSTINC(float (*)[4], dd->ptr.c), v);
    r = dd->ptr.c;
    ((GifPkWord *)r)->w[0] = 0x15000002;
    r += 4;
    dd->ptr.c = r;
    ((GifPkWord *)r)->w[0] = 0;
    dd->ptr.c = r + 4;
    ((GifPkWord *)(r + 4))->w[0] = 0;
    dd->ptr.c = r + 8;
    ((GifPkWord *)(r + 8))->w[0] = 0;
    dd->ptr.c = r + 0xC;
}

void prim_DispMesh3D(Mesh3D *m, void *la, void *lb, int tex)
{
    DpkCtl *d;
    char *p;
    char *q;
    TexExt *ext;
    int pri;

    pri = dl_GetPri();
    if (debug_disp_mesh == 0) {
        return;
    }
    ext = tex_GetTexExtData(tex);
    if (ext->animated != 0) {
        if (ext->file.shine != 0) {
            pri = reg_GetShinePri(ext->file.shine);
        }
    }
    if (tex != -1) {
        texturetranssize += tex_TransTexture(tex, pri);
    }
    mc_TransMicroCode(4, 1 << pri);
    d = &PacketBufferStruct;
    p = d->ptr.c;
    d->tail.c = 0;
    d->dma.c = p;
    d->gif.c = 0;
    d->end.c = 0;
    setMatrix();
    if (m->lit != 0) {
        setLight(la, lb);
    }
    if (tex == -1) {
        clearUVOffset();
    }
    q = d->ptr.c;
    d->tail.c = q;
    ((GifPkWord *)q)->d = 0x60000000;
    d->ptr.c = q + 8;
    ((GifPkWord *)(q + 8))->w[0] = 0;
    d->ptr.c = q + 0xC;
    ((GifPkWord *)(q + 0xC))->w[0] = 0;
    d->ptr.c = q + 0x10;
    dl_OpenDma(5, d->dma.c, 0);
    dl_CloseDma();
#ifdef ICO_RD
    mc_HostDma(5, d->dma.c, 0);
#endif
    gif_StartPacketPri(pri);
    gif_SetGsReg(0x4A, 0);
    gif_EndPacket();
    mc_SetMicroCode(2, m->lit, 0, 1, pri);
    dl_OpenDma(2, m->bufs[buffer_ID], m->qwc);
    dl_CloseDma();
#ifdef ICO_RD
    primHostGrid(m);
#endif
}

/* One 16-byte constant packet template, copied to the stack. */
typedef struct { /* field names derived */
    long long d[2];
} PrimQw; /* derived name */

PrimParticle *prim_InitParticleByPartition(int num, float x, float y, float z, int a1, char *name,
                                           int a3, void *heap)
{
    PrimQw hd = {{0x5000400000000000LL, 0x52521}};
    Prim3DVec cl0 = {1024.0f, 1024.0f, 0.0f, 1.0f};
    Prim3DVec cl1 = {3071.0f, 3071.0f, 0.0f, 16777215.0f};
    PrimParticle *p;
    PrimParticleObj *q;
    int *r;
    /* the GIF tag's PRIM field: sprite, TME, ABE, AA1 */
    long long prim = 214;

    if (num > 80) {
        debug_StdPrintfDummy(
            "Particle Object too big (%d particles). (must be under %d particles)\n", num, 80);
        return 0;
    }
    p = (PrimParticle *)iosMallocDebugNoAssert(
        heap, sizeof(PrimParticle) > 416 ? sizeof(PrimParticle) : 416, "src/Primitive.c", 911);
    if (p == 0) {
        return 0;
    }
    p->buf[0].head[0] = 0;
    p->buf[0].head[1] = 0;
    p->buf[0].head[2] = 0;
    p->buf[0].head[3] = 0x6C088000;
    _UnitMatrix(p->buf[0].mtx);
    _UnitMatrix(p->buf[0].lmtx);
    p->buf[0].tail[0] = 0x15000010;
    p->buf[0].tail[1] = 0;
    p->buf[0].tail[2] = 0;
    p->buf[0].tail[3] = 0;
    p->headQwc = 10;
    malloc_MemCpy(&p->buf[1], &p->buf[0], 160);
    p->word144 = 0;
    p->num = num;
    p->x = x;
    p->y = y;
    p->z = z;
    p->word158 = a1;
    sprintf(p->name, "%s", name);
    p->tex = tex_GetTextureNo(p->name);
    if (p->tex < 0 || p->tex >= tex_GetTextureNum()) {
        debug_StdPrintfDummy("prim_InitParticle:illegal texture no. %s:%d\n", p->name, p->tex);
        debug_assert("src/Primitive.c", 938);
        __assert("src/Primitive.c", 938, "FALSE");
    }
    p->objSize = num * 32 + 128;
    p->objs[0] =
        (PrimParticleObj *)iosMallocDebugNoAssert(heap, p->objSize, "src/Primitive.c", 944);
    if (p->objs[0] == 0) {
        iosFree(p);
        return 0;
    }
    p->objs[1] =
        (PrimParticleObj *)iosMallocDebugNoAssert(heap, p->objSize, "src/Primitive.c", 949);
    if (p->objs[1] == 0) {
        iosFree(p->objs[0]);
        iosFree(p);
        return 0;
    }
    q = p->objs[0];
    p->objSize = p->objSize >> 4;
    q->vif[0] = 0;
    q->vif[1] = 0;
    q->vif[2] = 0;
    q->vif[3] = ((p->objSize - 2) << 16) | 0x6C008000;
    q->num[0] = num;
    q->num[1] = 0;
    q->num[2] = 0;
    q->num[3] = 0;
    q->tag[0][0] = hd.d[0] | (prim << 47) | 1;
    q->tag[0][1] = hd.d[1];
    q->tag[1][0] = hd.d[0] | (prim << 47) | 0x8001;
    q->tag[1][1] = hd.d[1];
    _CopyVector(q->clipMin, &cl0);
    _CopyVector(q->clipMax, &cl1);
    r = q->vtx[num];
    r[0] = 0x13000000;
    r[1] = 0x17000000;
    r[2] = 0;
    r[3] = 0;
    q->pos[0] = x;
    q->pos[1] = y;
    q->pos[2] = z;
    q->pos[3] = 0.0f;
    malloc_MemCpy(p->objs[1], p->objs[0], p->objSize * 16);
    p->cur = 0;
    p->vtx = p->objs[0]->vtx;
    p->vtxNext = p->objs[1]->vtx;
    return p;
}

void prim_DispParticle(PrimParticle *p, void *mtx)
{
    int pri = dl_GetPri();

    if (debug_disp_particle != 0) {
        if (p->tex < 0 || p->tex >= tex_GetTextureNum()) {
            /* "the specified texture number is invalid" */
            debug_StdPrintfDummy("prim_DispParticle:指定したテクスチャ番号が異常です. %s:%d\n",
                                 p->name, p->tex);
            debug_assert("src/Primitive.c", 1020);
            __assert("src/Primitive.c", 1020, "FALSE");
            return;
        }
        if (p->num < 0x51) {
            texturetranssize += tex_TransTexture(p->tex, pri);
            _CopyMatrix(p->buf[p->cur].mtx, mtx);
            _CopyMatrix(p->buf[p->cur].lmtx, matrixptr + 0xC0);
            mc_TransMicroCode(5, 1 << pri);
            dl_OpenDma(2, &p->buf[p->cur], p->headQwc);
            dl_CloseDma();
#ifdef ICO_RD
            mc_HostDma(2, &p->buf[p->cur], p->headQwc);
#endif
            mc_SetMicroCode(3, 0, 0, 0, pri);
            dl_OpenDma(2, p->objs[p->cur], p->objSize);
            dl_CloseDma();
#ifdef ICO_RD
            mc_HostDma(2, p->objs[p->cur], p->objSize);
#endif
            if (systemStatus[5] == 0) {
                p->cur ^= 1;
            }
            p->vtx = p->objs[p->cur]->vtx;
            p->vtxNext = p->objs[p->cur ? 0 : 1]->vtx;
        }
    }
}

void prim_DeleteParticle(PrimParticle *p)
{
    EntryDelayFree(p->objs[1]);
    EntryDelayFree(p->objs[0]);
    EntryDelayFree(p);
}

static void drawDisc(float rr, float yy, float st, void *col, int flag)
{
    float a;
    Prim3DVec c = {0.0f, yy, 0.0f, 1.0f};

    for (a = 0.0f; a < 65536.0f; a += st) {
        Prim3DVec q0 = {rr * GetTableSin((short)a), yy, rr * GetTableCos((short)a), 1.0f};
        Prim3DVec q1 = {rr * GetTableSin((short)(a + st)), yy, rr * GetTableCos((short)(a + st)),
                        1.0f};

        DrawLineG(&q0, col, &q1, col, flag);
        DrawLineG(&q0, col, &c, col, flag);
    }
}

static inline void drawSide(float rr, float ya, float yb, float st, void *col,
                            int flag) /* derived name */
{
    float a;

    for (a = 0.0f; a < 65536.0f; a += st) {
        Prim3DVec p0 = {rr * GetTableSin((short)a), ya, rr * GetTableCos((short)a), 1.0f};
        Prim3DVec p1 = {p0.x, yb, p0.z, 1.0f};

        DrawLineG(&p0, col, &p1, col, flag);
    }
}

void prim_DispWireYCylinder(void *col, int n, int flag, float r, float y0, float y1)
{
    float st = 65536.0f / (float)n;

    drawDisc(r, y0, st, col, flag);
    drawDisc(r, y1, st, col, flag);
    drawSide(r, y0, y1, st, col, flag);
}

void prim_DispWireSphere(float r, void *col, int nu, int nv)
{
    float us = 65536.0f / (float)nu;
    float vs = 32768.0f / (float)nv;
    float u;
    float v;

    for (v = -16384.0f; v < 16384.0f; v += vs) {
        for (u = 0.0f; u < 65536.0f; u += us) {
            Prim3DVec p0 = {GetTableSin((short)u) * GetTableCos((short)v), GetTableSin((short)v),
                            GetTableCos((short)u) * GetTableCos((short)v), 1.0f};
            Prim3DVec p1 = {GetTableSin((short)(u + us)) * GetTableCos((short)v),
                            GetTableSin((short)v),
                            GetTableCos((short)(u + us)) * GetTableCos((short)v), 1.0f};
            Prim3DVec p2 = {GetTableSin((short)u) * GetTableCos((short)(v + vs)),
                            GetTableSin((short)(v + vs)),
                            GetTableCos((short)u) * GetTableCos((short)(v + vs)), 1.0f};

            _ScaleVectorXYZ(&p0, &p0, r);
            _ScaleVectorXYZ(&p1, &p1, r);
            _ScaleVectorXYZ(&p2, &p2, r);
            DrawLineG(&p0, col, &p1, col, 0);
            DrawLineG(&p0, col, &p2, col, 0);
        }
    }
}

/* Box corner, a VU0 quadword: DrawLineG takes 16-byte aligned vectors. */
typedef struct { /* field names derived */
    float x, y, z, w;
} PrimVtx __attribute__((aligned(16))); /* derived name */

void prim_DispWireBox(float *sz, void *col)
{
    PrimVtx bmin = {-sz[0], -sz[1], -sz[2], 1.0f};
    PrimVtx bmax = {sz[0], sz[1], sz[2], 1.0f};
    PrimVtx v[8];
    int e[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                    {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    int i;

    for (i = 0; i < 8; i++) {
        v[i].x = (i & 1) ? bmax.x : bmin.x;
        v[i].y = (i & 2) ? bmax.y : bmin.y;
        v[i].z = (i & 4) ? bmax.z : bmin.z;
        v[i].w = 1.0f;
    }
    for (i = 0; i < 12; i++) {
        DrawLineG(&v[e[i][0]], col, &v[e[i][1]], col, 0);
    }
}

PrimParticle *prim_InitParticle(int num, float x, float y, float z, int a1, char *name, int a3)
{
    return prim_InitParticleByPartition(num, x, y, z, a1, name, a3, ios_partition_oomori);
}
