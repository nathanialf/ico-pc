#include "mv_defs.h"
#include "typedef.h"
#include "mv_vobuf.h"
#include "mv_disp.h"
#include <eeregs.h>
#include <libgraph.h>
#include "main.h"

inline void *setTEX0_1(int *p, int tbp0, int tbw, int psm, int tw, int th, int tcc, int tfx,
                       unsigned int cbp, unsigned int cpsm, unsigned int csm, unsigned int csa,
                       unsigned int cld);

inline void *setPRIM(int *p, int prim, int iip, int tme, int fge, int abe, int aa1, int fst,
                     unsigned int ctxt, unsigned int fix);

inline void *setUV(int *p, int u, int v);
inline void *setRGBAQ(int *p, int r, int g, int b, int a, int q);
inline void *setXYZ2(int *p, int x, int y, int z);
inline void *setFRAME_1(int *p, int fbp, int fbw, int psm, int fbmsk);

inline void *setTEST_1(int *p, int ate, int atst, int aref, int afail, int date, int datm, int zte,
                       unsigned int ztst);

inline void *setSCISSOR_1(int *p, int scax0, int scax1, int scay0, int scay1);
inline void *setXYOFFSET_1(int *p, unsigned int ofx, unsigned int ofy);
inline void *setPRMODECONT(int *p, int ac);

inline void *setPRMODE(int *p, int iip, int tme, int fge, int abe, int aa1, int fst, int ctxt,
                       unsigned int fix);

inline void *setCLAMP_1(int *p, unsigned int wms, unsigned int wmt, unsigned int minu,
                        unsigned int maxu, unsigned int minv, unsigned int maxv);

inline int *setBITBLTBUF(int *p, long long dbp, long long dbw, long long dpsm);
inline int *setTRXPOS(int *p, long long dir, int dsax, int dsay);
inline void *setTRXREG(int *p, int rrw, int rrh);
inline void *setTRXDIR(int *p, unsigned int xdir);

/* the two GIF packets this file builds, one per display path */
static int mvDispPacket[80]; /* derived name */

static int mvClearPacket[80]; /* derived name */

/* The display state vblankHandler shares with the foreground code: the
   vblanks counted while displaying, the displaying flag, the image-done flag
   handler_endimage clears, the field the GS reports, and the path-sync
   result. */
static int dispVblankCount = 0; /* derived name */

static int dispRunning = 0; /* derived name */

static int dispImageDone = 0; /* derived name */

static int dispField = 0; /* derived name */

static int dispSyncBusy = 0; /* derived name */

/* the frame counter the display loop keeps */
static int mvFrameCount; /* derived name */

inline void loadImage(int addr)
{
    *D2_TADR = phys_addr(addr);
    *D2_QWC = 0;
    *D2_CHCR = 0x105;
}

typedef struct MvRect { /* field names derived */
    int x;
    int y;
    int w;
    int h;
} MvRect; /* derived name */

/* a textured sprite over r, sampling uv */
static inline int *setTexSprite(int *p, MvRect *r, MvRect *uv) /* derived name */
{
    p = setPRIM(p, 6, 0, 1, 0, 0, 0, 1, 0, 0);
    p = setUV(p, uv->x, uv->y);
    p = setXYZ2(p, r->x, r->y, 0);
    p = setUV(p, uv->x + uv->w, uv->y + uv->h);
    p = setXYZ2(p, r->x + r->w, r->y + r->h, 0);
    return p;
}

/* a flat sprite of colour col over r */
static inline int *setClearSprite(int *p, MvRect *r, unsigned int col) /* derived name */
{
    p = setPRIM(p, 6, 0, 0, 0, 0, 0, 1, 0, 0);
    p = setXYZ2(p, r->x, r->y, 0);
    p = setRGBAQ(p, col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, (col >> 24) & 0xFF, 0);
    p = setXYZ2(p, r->x + r->w, r->y + r->h, 0);
    return p;
}

void dispClear(MvDispEnv *self, unsigned int col)
{
    MvRect r;
    int *p;

    r.x = 0;
    r.y = 0;
    r.w = self->width << 4;
    r.h = ((self->height + 31) / 32 * 64 + self->height * PAL_JPN(2, 3)) << 4;

    p = setGIFtag((int *)uncached_accel_addr((int)mvClearPacket), 14, 1, 0, 0, 0, 1, 4);
    setClearSprite(p, &r, col);

    *D2_MADR = phys_addr((int)mvClearPacket);
    *D2_QWC = 5;
    *D2_CHCR = 0x101;

    sceGsSyncPath(0, 0);
}

void setDispEnv(MvDispEnv *self, int imageW, int imageH, int dbx, int dby)
{
    int *p;
#ifdef VERSION_JPN
    int w = imageW;
    int h = imageH / 2;
#else
    int w = 720;
    int h = 288;
#endif

    self->fbp[0] = 0;
    self->fbp[1] = PAL_JPN(108, (w + 63) / 64 * 64 * ((h + 31) / 32 * 32) >> 11);
    self->width = w;
    self->height = h;
    self->imageWidth = imageW;
    self->imageHeight = imageH;

    sceGsSetDefDispEnv(&self->env.gs, 0, imageW, imageH / 2, 0, 0);

    if (ICO_JPN) {
        p = (int *)uncached_accel_addr((int)mvDispPacket);
    }
    self->env.w.dispfbDB = (self->env.w.dispfbDB & ~0x7FF) | (dbx & 0x7FF);
    self->env.w.dispfbDB = (self->env.w.dispfbDB & 0xFFC007FF) | ((dby & 0x7FF) << 11);
    if (!ICO_JPN) {
        self->env.w.dispfb = (self->env.w.dispfb & ~0x7E00) | 0x1800;
    }

    p = setGIFtag(PAL_JPN((int *)uncached_accel_addr((int)mvDispPacket), p), 14, 1, 0, 0, 0, 1, 6);
    p = setPRMODECONT(p, 1);
    p = setFRAME_1(p, 0, (self->width + 63) / 64, 0, 0);
    p = setTEST_1(p, 0, 0, 0, 0, 0, 0, 0, 0);
    p = setSCISSOR_1(p, 0, self->width - 1, 0,
                     PAL_JPN((self->height + 31) / 32 * 128 - 1,
                             (self->height + 31) / 32 * 64 + self->height * 3));
    p = setXYOFFSET_1(p, 0, 0);
    setCLAMP_1(p, 1, 1, 0, 0, 0, 0);
}

void setImageSize(MvDispEnv *self, int imageW, int imageH, int dbx, int dby)
{
    int lim = self->imageHeight;
    if (imageH <= lim) {
        imageH = lim;
    }
    setDispEnv(self, imageW, imageH, dbx, dby);
}

void sendDispEnv(MvDispEnv *self)
{
    sceGsPutDispEnv(&self->env.gs);
    *D2_MADR = phys_addr((int)mvDispPacket);
    *D2_QWC = 7;
    *D2_CHCR = 0x101;
    sceGsSyncPath(0, 0);
}

void dispCreate(MvDispEnv *self, int imageW, int imageH, int dbx, int dby)
{
    /* the five display-state words are read and written by vblankHandler on the
       vblank interrupt, so they are reset through volatile */
    *(volatile int *)&dispRunning = 0;
    *(volatile int *)&dispVblankCount = 0;
    *(volatile int *)&dispImageDone = 0;
    *(volatile int *)&dispField = 0;
    *(volatile int *)&dispSyncBusy = 0;
    sceGsSyncV(0);
    sceGsResetGraph(0, 1, PAL_JPN(systemStatus[0] != 0 ? 3 : 2, 2), 1);
    sceGsResetPath();
    setDispEnv(self, imageW, imageH, dbx, dby);
    sendDispEnv(self);
}

inline void dispDelete(MvDispEnv *self) {}

void dispSetTags(MvDispEnv *self, int src, int image, int field, int x, int y, int w, int h,
                 int texW, int texH)
{
    MvRect r;
    MvRect uv;
    MvRect r2;
    MvRect uv2;
    void *p = (void *)uncached_accel_addr(src);
    int nx;
    int ny;
    int bw;
    int bh;
    int dbp;
    int i;
    int j;

    nx = texW >> 4;
    ny = texH >> 4;

    if (ICO_JPN) {
        r.x = 0;
        r.y = ((self->height + 31) / 32 * 64 + self->height * 2) << 4;
        r.w = texW << 4;
        r.h = texH / 2 << 4;
    } else {
        r.x = x << 4;
        r.y = y << 4;
        r.w = w << 4;
        r.h = h << 4;
        uv.x = 8;
        uv.y = 8;
        uv.w = texW << 4;
        uv.h = texH << 4;
    }

    if (field == 0) {
        if (ICO_JPN) {
            uv.x = 8;
            uv.y = 8;
            uv.w = texW << 4;
            uv.h = texH << 4;

            r2.x = x << 4;
            r2.y = y << 4;
            r2.w = w << 4;
            r2.h = h << 4;

            uv2.x = 8;
            uv2.y = (self->height * 2 << 4) + 8;
            uv2.w = texW << 4;
            uv2.h = texH / 2 << 4;
        }

        bh = (self->height + 31) / 32;
        bw = (self->width + 63) / 64;
        dbp = bh * (bw << 6);
        p = setDMAscTag(p, 0, 0, 0, 1, 0, 3);
        p = setGIFtag(p, 14, 1, 0, 0, 0, 0, 2);
        p = setBITBLTBUF(p, dbp, bw, 0);
        p = setTRXREG(p, 16, 16);
        for (i = 0; i < nx; i++) {
            for (j = 0; j < ny; j++) {
                p = setDMAscTag(p, 0, 0, 0, 1, 0, 4);
                p = setGIFtag(p, 14, 1, 0, 0, 0, 0, 2);
                p = setTRXPOS(p, 0, i << 4, j << 4);
                p = setTRXDIR(p, 0);
                p = setGIFtag(p, 0, 0, 2, 0, 0, 0, 64);
                p = setDMAscTag(p, 0, phys_addr(image), 0, 3, 0, 64);
                image += 1024;
            }
        }
    } else {
        if (ICO_JPN) {
            uv.x = 8;
        }
        uv.y = 24;
        if (ICO_JPN) {
            uv.w = texW << 4;
            uv.h = texH << 4;

            r2.x = x << 4;
            r2.y = (y + (self->height + 31) / 32 * 32) << 4;
            r2.w = w << 4;
            r2.h = h << 4;

            uv2.x = 8;
            uv2.y = (self->height * 2 << 4) + 8;
            uv2.w = texW << 4;
            uv2.h = texH / 2 << 4;
        } else {
            r.y = (y + (self->height + 31) / 32 * 32) << 4;
        }
    }

    p = setDMAscTag(p, 0, 0, 0, 7, 0, 16);
    p = setGIFtag(p, 14, 1, 0, 0, 0, 1, 15);
    p = setTEXFLUSH(p);
    p = setTEX1_1(p, 0, 0, PAL_JPN(1, 0), PAL_JPN(1, 0), 0, 0, 0);
    p = setTEX0_1(p, (self->height + 31) / 32 * ((self->width + 63) / 64 << 6),
                  (self->width + 63) / 64, 0, 10, 10, 0, 1, 0, 0, 0, 0, 0);
    if (ICO_JPN) {
        p = setTexSprite(p, &r, &uv);
        p = setTEXFLUSH(p);
        p = setTEX1_1(p, 0, 0, 1, 1, 0, 0, 0);
        setTexSprite(p, &r2, &uv2);
    } else {
        setTexSprite(p, &r, &uv);
    }
}

void dispSwitch(MvDispEnv *self, int flag)
{
    int src;
    if (flag != 0) {
        src = self->fbp[1];
    } else {
        src = self->fbp[0];
    }
    self->env.w.dispfb = (self->env.w.dispfb & ~0x1FF) | (src & 0x1FF);
    sceGsPutDispEnv(&self->env.gs);
}

int vblankHandler(int cause)
{
    VoTag *tag;
    int st;

    *(volatile int *)&dispField = (int)((*GS_CSR >> 13) & 1);
    if (*(volatile int *)&dispRunning != 0) {
        *(volatile int *)&dispVblankCount = *(volatile int *)&dispVblankCount + 1;
        /* the display-state words are shared with the foreground code, which
           reads them between vblanks */
        *(volatile int *)&dispSyncBusy = sceGsSyncPath(1, 0);
        if (*(volatile int *)&dispSyncBusy == 0) {
            tag = voBufGetTag(&voBuf);
            if (tag == 0) {
                mvFrameCount++;
                SYNC();
                EI();
                return 0;
            }
            if (*(volatile int *)&dispField == 0 && tag->status == 2) {
                dispSwitch(&display, 0);
                loadImage((int)tag->packet[1]);
                tag->status = 1;
            } else if (*(volatile int *)&dispField != 0 && (st = tag->status) == 1) {
                dispSwitch(&display, 1);
                loadImage((int)tag->packet[0]);
                tag->status = 0;
                *(volatile int *)&dispImageDone = st;
            }
        }
    }
    SYNC();
    EI();
    return 0;
}

inline int handler_endimage(int channel)
{
    if (dispImageDone != 0) {
        voBufDecCount(&voBuf);
        dispImageDone = 0;
    }
    SYNC();
    EI();
    return 0;
}

inline void startDisplay(int field)
{
    while (sceGsSyncV(0) == field)
        ;
    *(volatile int *)&dispRunning = 1;
    mvFrameCount = 0;
    *(volatile int *)&dispVblankCount = 0;
}

inline void endDisplay(void)
{
    dispRunning = 0;
    mvFrameCount = 0;
}

inline void *setDMAscTag(void *p, int spr, unsigned int addr, int irq, int id, int pce, int qwc)
{
    unsigned long long g1 = ((unsigned long long)spr << 63) | (unsigned int)qwc;
    unsigned long long g2 = ((unsigned long long)(unsigned int)id << 28) |
                            ((unsigned long long)(unsigned int)irq << 31);
    unsigned long long g3 = ((unsigned long long)(addr & 0xFFFFFFF0) << 32) |
                            ((unsigned long long)(unsigned int)pce << 26);
    *(long long *)p = g1 | g2 | g3;
    return (char *)p + 0x10;
}

inline void *setGIFtag(int *p, long long regs, int nreg, int flg, int prim, int pre, int eop,
                       int nloop)
{
    int hi = (pre << 14) | (nreg << 28);
    int lo = (flg << 26) | (prim << 15);
    p[0] = (eop << 15) | nloop;
    p[1] = hi | lo;
    p[2] = (int)(regs & 0xFFFFFFFFLL);
    p[3] = (int)(regs >> 32);
    return (char *)p + 0x10;
}

inline void *setTEXFLUSH(int *p)
{
    p[0] = 0;
    p[2] = 63;
    p[1] = 0;
    p[3] = 0;
    return p + 4;
}

inline void *setGIFad(int *p, int addr, long long data)
{
    p[0] = (int)(data & 0xFFFFFFFFLL);
    p[1] = (int)(data >> 32);
    p[2] = addr;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setTEX1_1(int *p, int lcm, int mxl, int mmag, int mmin, int mtba, int l, int k)
{
    long long t = (unsigned int)lcm | ((unsigned long long)(unsigned int)mxl << 2) |
                  ((unsigned long long)(unsigned int)mmag << 5) |
                  ((unsigned long long)(unsigned int)mmin << 6) |
                  ((unsigned long long)(unsigned int)mtba << 9) |
                  ((unsigned long long)(unsigned int)l << 19) | ((long long)k << 32);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 0x14;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setTEX0_1(int *p, int tbp0, int tbw, int psm, int tw, int th, int tcc, int tfx,
                       unsigned int cbp, unsigned int cpsm, unsigned int csm, unsigned int csa,
                       unsigned int cld)
{
    long long t = (unsigned int)tbp0 | ((unsigned long long)(unsigned int)tbw << 14) |
                  ((unsigned long long)(unsigned int)psm << 20) |
                  ((unsigned long long)(unsigned int)tw << 26) |
                  ((unsigned long long)(unsigned int)th << 30) | ((long long)tcc << 34) |
                  ((long long)tfx << 35) | ((unsigned long long)cbp << 37) |
                  ((unsigned long long)cpsm << 51) | ((unsigned long long)csm << 55) |
                  ((unsigned long long)csa << 56) | ((unsigned long long)cld << 61);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 6;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setPRIM(int *p, int prim, int iip, int tme, int fge, int abe, int aa1, int fst,
                     unsigned int ctxt, unsigned int fix)
{
    long long t = (unsigned int)prim | ((unsigned long long)(unsigned int)iip << 3) |
                  ((unsigned long long)(unsigned int)tme << 4) |
                  ((unsigned long long)(unsigned int)fge << 5) |
                  ((unsigned long long)(unsigned int)abe << 6) |
                  ((unsigned long long)(unsigned int)aa1 << 7) |
                  ((unsigned long long)(unsigned int)fst << 8) | ((unsigned long long)ctxt << 9) |
                  ((unsigned long long)fix << 10);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 0;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setUV(int *p, int u, int v)
{
    long long t = (unsigned int)u | ((unsigned long long)(unsigned int)v << 16);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 3;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setRGBAQ(int *p, int r, int g, int b, int a, int q)
{
    long long t = (unsigned int)r | ((unsigned long long)(unsigned int)g << 8) |
                  ((unsigned long long)(unsigned int)b << 16) |
                  ((unsigned long long)(unsigned int)a << 24) | ((long long)q << 32);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 1;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setXYZ2(int *p, int x, int y, int z)
{
    long long t =
        (unsigned int)x | ((unsigned long long)(unsigned int)y << 16) | ((long long)z << 32);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 5;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setFRAME_1(int *p, int fbp, int fbw, int psm, int fbmsk)
{
    long long t = (unsigned int)fbp | ((unsigned long long)(unsigned int)fbw << 16) |
                  ((unsigned long long)(unsigned int)psm << 24) | ((long long)fbmsk << 32);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 0x4C;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setTEST_1(int *p, int ate, int atst, int aref, int afail, int date, int datm, int zte,
                       unsigned int ztst)
{
    long long t = (unsigned int)ate | ((unsigned long long)(unsigned int)atst << 1) |
                  ((unsigned long long)(unsigned int)aref << 4) |
                  ((unsigned long long)(unsigned int)afail << 12) |
                  ((unsigned long long)(unsigned int)date << 14) |
                  ((unsigned long long)(unsigned int)datm << 15) |
                  ((unsigned long long)(unsigned int)zte << 16) | ((unsigned long long)ztst << 17);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 0x47;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setSCISSOR_1(int *p, int scax0, int scax1, int scay0, int scay1)
{
    long long t = (unsigned int)scax0 | ((unsigned long long)(unsigned int)scax1 << 16) |
                  ((long long)scay0 << 32) | ((long long)scay1 << 48);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 0x40;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setXYOFFSET_1(int *p, unsigned int ofx, unsigned int ofy)
{
    long long t = (unsigned int)ofx | ((unsigned long long)ofy << 32);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 0x18;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setPRMODECONT(int *p, int ac)
{
    long long t = (unsigned int)ac;
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 0x1A;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setPRMODE(int *p, int iip, int tme, int fge, int abe, int aa1, int fst, int ctxt,
                       unsigned int fix)
{
    long long t = ((unsigned long long)(unsigned int)iip << 3) |
                  ((unsigned long long)(unsigned int)tme << 4) |
                  ((unsigned long long)(unsigned int)fge << 5) |
                  ((unsigned long long)(unsigned int)abe << 6) |
                  ((unsigned long long)(unsigned int)aa1 << 7) |
                  ((unsigned long long)(unsigned int)fst << 8) |
                  ((unsigned long long)(unsigned int)ctxt << 9) | ((unsigned long long)fix << 10);
    p[0] = (int)(t & 0xFFFFFFFFLL);
    p[1] = (int)(t >> 32);
    p[2] = 0x1B;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setCLAMP_1(int *p, unsigned int wms, unsigned int wmt, unsigned int minu,
                        unsigned int maxu, unsigned int minv, unsigned int maxv)
{
    long long v = wms | ((long long)wmt << 2) | ((long long)minu << 4) | ((long long)maxu << 14) |
                  ((long long)minv << 24) | ((long long)maxv << 34);
    p[0] = v & 0xffffffff;
    p[2] = 8;
    p[1] = (int)(v >> 32);
    p[3] = 0;
    return (char *)p + 0x10;
}

inline int *setBITBLTBUF(int *p, long long dbp, long long dbw, long long dpsm)
{
    long long t = (dpsm << 56) | (dbw << 48) | (dbp << 32);
    p[1] = (int)(t >> 32);
    p[2] = 0x50;
    p[0] = 0;
    p[3] = 0;
    return p + 4;
}

inline int *setTRXPOS(int *p, long long dir, int dsax, int dsay)
{
    long long t = (dir << (27 + 32)) | ((long long)dsay << 48) | ((long long)dsax << 32);
    p[1] = (int)(t >> 32);
    p[2] = 0x51;
    p[0] = 0;
    p[3] = 0;
    return p + 4;
}

inline void *setTRXREG(int *p, int rrw, int rrh)
{
    unsigned long long v = (unsigned int)rrw;
    unsigned long long packed = ((unsigned long long)rrh << 32) | v;
    p[0] = (int)v;
    p[1] = (int)(packed >> 32);
    p[2] = 0x52;
    p[3] = 0;
    return (char *)p + 0x10;
}

inline void *setTRXDIR(int *p, unsigned int xdir)
{
    unsigned long long v = (unsigned int)xdir;
    p[2] = 83;
    p[0] = (int)v;
    p[1] = (int)(v >> 32);
    p[3] = 0;
    return p + 4;
}
