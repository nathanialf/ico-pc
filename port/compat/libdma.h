/*
 * port/compat/libdma.h
 *
 * The host build's libdma.h: the declarations the game uses, from
 * sce/libdma/libdma.h (this project's own clean-room header, MIT), with
 * pointer-sized and 64-bit types made host-correct.
 */
#ifndef ICO_COMPAT_LIBDMA_H
#define ICO_COMPAT_LIBDMA_H

/* The channel-environment record sceDmaPutEnv writes to D_CTRL, D_PCR,
   D_SQWC, D_RBOR and D_RBSR: the first three bytes select the D_CTRL MFD,
   STS and STD fields through sceDmaPutEnv's code tables, the fourth is the
   release cycle (0 = off). */
typedef struct {         /* field names derived */
    unsigned char mfd;   /* 0x00 */
    unsigned char sts;   /* 0x01 */
    unsigned char std;   /* 0x02 */
    unsigned char rcyc;  /* 0x03 */
    unsigned short cde;  /* 0x04, D_PCR's upper half */
    unsigned short cpc;  /* 0x06, D_PCR's lower half */
    unsigned short sqwc; /* 0x08, D_SQWC's lower half */
    unsigned short tqwc; /* 0x0A, D_SQWC's upper half */
    void *rbadr;         /* 0x0C ring buffer address, to D_RBOR */
    int rbsize;          /* 0x10 ring buffer size, to D_RBSR */
} DmaEnv;

/* A channel's register block, CHCR to TADR. */
typedef struct DmaChan {
    volatile int chcr; /* 0x00 */
    int pad0[3];
    int madr; /* 0x10 */
    int pad1[3];
    int qwc; /* 0x20 */
    int pad2[3];
    int tadr; /* 0x30 */
} DmaChan;

DmaChan *sceDmaGetChan(unsigned int id);
int sceDmaReset(int mode);
int sceDmaPutEnv(DmaEnv *env);
void sceDmaSend(DmaChan *ch, void *addr); /* EE: an unsigned int address */
int sceDmaSync(DmaChan *ch, int mode, int n);

#endif /* ICO_COMPAT_LIBDMA_H */
