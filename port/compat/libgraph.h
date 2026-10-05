/*
 * port/compat/libgraph.h
 *
 * The host build's libgraph.h: the declarations the game uses, from
 * sce/libgraph/libgraph.h (this project's own clean-room header, MIT), with
 * pointer-sized and 64-bit types made host-correct.
 */
#ifndef ICO_COMPAT_LIBGRAPH_H
#define ICO_COMPAT_LIBGRAPH_H

/* The record sceGsGetGParam hands back (graph001.o's .data), laid out from
   the offsets sceGsResetGraph writes; graph003, graph004, graph005, graph009
   and graph011 read it. */
typedef struct {
    short inter;   /* 0x0 */
    short omode;   /* 0x2 */
    short ffmd;    /* 0x4 */
    short version; /* 0x6, the GS revision out of CSR bits 16..23 */
    int intcUsed;  /* 0x8, set while graph001 owns the INTC 2 handler */
    int handler;   /* 0xC, the handler id RemoveIntcHandler takes */
} sceGsGParam;

/* PMODE / SMODE2 / DISPFB / DISPLAY / BGCOLOR, one qword each. */
typedef struct {
    long long pmode;   /* 0x00 */
    long long smode2;  /* 0x08 */
    long long dispfb;  /* 0x10 */
    long long display; /* 0x18 */
    long long bgcolor; /* 0x20 */
} sceGsDispEnv;

/* Four (register-address, data) pairs: TEXFLUSH-or-NOP, TEX1_1, TEX0_1, CLAMP_1. */
typedef struct {
    long long texflush;      /* 0x00 */
    long long texflush_addr; /* 0x08 */
    long long tex1;          /* 0x10 */
    long long tex1_addr;     /* 0x18 */
    long long tex0;          /* 0x20 */
    long long tex0_addr;     /* 0x28 */
    long long clamp;         /* 0x30 */
    long long clamp_addr;    /* 0x38 */
} sceGsTexEnv;

/* Eight (register-address, data) pairs: FRAME_1, ZBUF_1, XYOFFSET_1, SCISSOR_1,
   PRMODECONT, COLCLAMP, DTHE, TEST_1.  Same pair convention as graph007. */
typedef struct {
    long long frame;           /* 0x00 */
    long long frame_addr;      /* 0x08 */
    long long zbuf;            /* 0x10 */
    long long zbuf_addr;       /* 0x18 */
    long long xyoffset;        /* 0x20 */
    long long xyoffset_addr;   /* 0x28 */
    long long scissor;         /* 0x30 */
    long long scissor_addr;    /* 0x38 */
    long long prmodecont;      /* 0x40 */
    long long prmodecont_addr; /* 0x48 */
    long long colclamp;        /* 0x50 */
    long long colclamp_addr;   /* 0x58 */
    long long dthe;            /* 0x60 */
    long long dthe_addr;       /* 0x68 */
    long long test;            /* 0x70 */
    long long test_addr;       /* 0x78 */
} sceGsDrawEnv;

/* The (data, address) register pair list sceGsSetDefClear fills: six GS
   register writes, TEST_1 / PRIM / RGBAQ / XYZ2 / XYZ2 / TEST_1. */
typedef struct {
    unsigned long long test_1;       /* 0x00 */
    unsigned long long test_1_addr;  /* 0x08 */
    unsigned long long prim;         /* 0x10 */
    unsigned long long prim_addr;    /* 0x18 */
    unsigned long long rgbaq;        /* 0x20 */
    unsigned long long rgbaq_addr;   /* 0x28 */
    unsigned long long xyz2_0;       /* 0x30 */
    unsigned long long xyz2_0_addr;  /* 0x38 */
    unsigned long long xyz2_1;       /* 0x40 */
    unsigned long long xyz2_1_addr;  /* 0x48 */
    unsigned long long test_1r;      /* 0x50 */
    unsigned long long test_1r_addr; /* 0x58 */
} sceGsClear;

/* The host-to-local transfer packet sceGsSetDefLoadImage fills (graph015):
   a GIF tag, four (data, address) register pairs, the image's GIF tag. */
typedef struct {
    long long giftag0[2]; /* 0x00 */
    long long bitbltbuf;  /* 0x10 */
    long long abitbltbuf; /* 0x18 */
    long long trxpos;     /* 0x20 */
    long long atrxpos;    /* 0x28 */
    long long trxreg;     /* 0x30 */
    long long atrxreg;    /* 0x38 */
    long long trxdir;     /* 0x40 */
    long long atrxdir;    /* 0x48 */
    long long giftag1[2]; /* 0x50 */
} sceGsLoadImage;

/* The local-to-host transfer packet sceGsSetDefStoreImage fills (graph016):
   four VIF codes, a GIF tag, five (data, address) register pairs. */
typedef struct {
    unsigned int vifcode[4]; /* 0x00 */
    long long giftag[2];     /* 0x10 */
    long long bitbltbuf;     /* 0x20 */
    long long abitbltbuf;    /* 0x28 */
    long long trxpos;        /* 0x30 */
    long long atrxpos;       /* 0x38 */
    long long trxreg;        /* 0x40 */
    long long atrxreg;       /* 0x48 */
    long long finish;        /* 0x50 */
    long long afinish;       /* 0x58 */
    long long trxdir;        /* 0x60 */
    long long atrxdir;       /* 0x68 */
} sceGsStoreImage;

int sceGsExecLoadImage(void *pkt, void *img);
int sceGsExecStoreImage(void *pkt, void *img);

/* The double buffer sceGsSetDefDBuff fills and sceGsSwapDBuff flips: the two
   display environments, then each frame's GIF tag, draw environment and
   clear list. */
typedef struct {
    sceGsDispEnv disp[2]; /* 0x000 */
    long long giftag0[2]; /* 0x050 */
    sceGsDrawEnv draw0;   /* 0x060 */
    sceGsClear clear0;    /* 0x0E0 */
    long long giftag1[2]; /* 0x140 */
    sceGsDrawEnv draw1;   /* 0x150 */
    sceGsClear clear1;    /* 0x1D0 */
} sceGsDBuff;

sceGsGParam *sceGsGetGParam(void);
unsigned long long sceGsGetIMR(void);
void sceGsPutDispEnv(void *disp);
int sceGsPutDrawEnv(void *pkt);
unsigned long long sceGsPutIMR(unsigned long long imr);
void sceGsResetGraph(short mode, short inter, short omode, short ffmd);
void sceGsResetPath(void);
int sceGsSetDefAlphaEnv(long long *pkt, int pabe);
void sceGsSetDefDispEnv(sceGsDispEnv *disp, short psm, short w, short h, short dx, short dy);

int sceGsSetDefClear(sceGsClear *cl, short ztst, short x, short y, short w, short h,
                     unsigned char r, unsigned char g, unsigned char b, unsigned char a,
                     unsigned int z);

void sceGsSetDefDBuff(sceGsDBuff *db, short psm, short w, short h, short ztst, short zpsm,
                      short flag);

int sceGsSetDefDrawEnv(sceGsDrawEnv *env, short psm, short w, short h, short ztst, short zpsm);

int sceGsSetDefLoadImage(sceGsLoadImage *di, short dbp, short dbw, short dpsm, short dsax,
                         short dsay, short rrw, short rrh);

/* sceGsSetDefStoreImage is left out: Texture.c, the game's caller that
   includes this header, declares it with int parameters, which its bytes pin. */

int sceGsSetDefTexEnv(sceGsTexEnv *env, short flush, short tbp, short tbw, short psm, short tw,
                      short th, short tfx, short cbp, short cpsm, short cld, short flt);

void sceGsSetHalfOffset(void *draw, short x, short y, short half);
int sceGsSwapDBuff(void *db, int id);
int sceGsSyncPath(int mode, unsigned short timeout);
int sceGsSyncV(int mode);
short sceGszbufaddr(short psm, short width, short height);

#endif /* ICO_COMPAT_LIBGRAPH_H */
