/* libpad.a.  The archive's member boundaries in this build are not known,
 * so this file is the archive's whole run. */
#include <sifrpc.h>
#include <sifcmd.h>
#include <libpad.h>
#include <stdio.h>
#include <eekernel.h>
#include <string.h>

#define PAD_DEBUG 0 /* derived name */

struct S12 { /* field names derived */
    char b[12];
};

/* The per-port, per-slot state record (0x1C bytes, padSlot[2][4]).  The
 * pointer fields are typed by what scePadPortOpen stores in them: the EE pad
 * DMA area the caller passes, this slot's command buffer (read back as an int
 * pointer by scePadSetActDirect), and the IOP-side buffer address the open
 * RPC returns, with the command buffer's type.  scePadInit clears the two
 * words at 0x14 and 0x18, which nothing else in the run reads. */
typedef struct {   /* field names derived */
    void *dmaArea; /* 0x00 */
    int *cmdBuf;   /* 0x04 */
    int *iopBuf;   /* 0x08 */
    int dmaId;     /* 0x0C, the sceSifSetDma id _send_to_iop polls */
    int opened;    /* 0x10 */
    int pad14;     /* 0x14 */
    int pad18;     /* 0x18 */
} PadSlot;         /* derived name */

/* the member's .data: the build stamp, the init flag scePadInit sets and
   scePadEnd clears, the flag every diagnostic print tests, and the names
   scePadStateIntToStr and scePadReqIntToStr copy out */
static char scePadVersion[16] = "PsIIlibpad  2200"; /* derived name */

static int padInited = 0; /* derived name */

static int padDebug = 1; /* derived name */

/* the member's .bss: the two RPC clients (padman's two servers), the slot
   records, each slot's 64-byte command buffer and the RPC buffer */
static sceSifRpcClientData padClient[2]; /* derived name */

static PadSlot padSlot[2][4]; /* derived name */

static int padCmd[2][4][16] __attribute__((aligned(64))); /* derived name */

static int padRpcBuf[32] __attribute__((aligned(64))); /* derived name */

/* The IOP send helper, libpad.o's first function (its first string is the
   member's first .rodata entry). */
void _send_to_iop(int port, int slot)
{
    sceSifDmaData dma[16]; /* the frame holds sixteen transfer records; one is sent */

    int *cmd = padSlot[port][slot].cmdBuf;
    int ret = sceSifDmaStat(padSlot[port][slot].dmaId);

    if (ret >= 0) {
        if (padDebug != 0) {
            printf("libpad: sceSifSetDma faild\n");
        }
    } else {
        int n = *cmd + 1;
        int *v =
            padSlot[port][slot].iopBuf + ((n & 1) << 3); /* the 32-byte half, iopBuf is int * */
        int r;
        if (PAD_DEBUG) {
            printf("libpad: tPadDma Structure Invalid\n");
        }
        *cmd = n;
        SyncDCache(cmd, (char *)cmd + 0x20);
        dma[0].src = (unsigned int)cmd;
        dma[0].dest = (unsigned int)v;
        dma[0].size = 0x20;
        dma[0].u.attr = 0;
        r = sceSifSetDma(dma, 1);
        if (r == 0) {
            if (padDebug != 0) {
                printf("libpad: sceSifSetDma faild\n");
            }
        }
        padSlot[port][slot].dmaId = r;
    }
}

int scePadInit(int a0)
{
    int ver;
    int major;
    int i;

    padInited = 1;
    while (1) {
        sceSifBindRpc(&padClient[0], 0x80000100, 0);
        if (padClient[0].serve != 0) {
            break;
        }
        for (i = 0x10000; i != -1; i--) {}
    }
    while (1) {
        sceSifBindRpc(&padClient[1], 0x80000101, 0);
        if (padClient[1].serve != 0) {
            break;
        }
        for (i = 0x10000; i != -1; i--) {}
    }
    ver = scePadGetModVersion();
    major = ver >> 8;
    if (major != 4) {
        if (padDebug != 0) {
            printf("libpad: Module version mismatch ");
            printf("[libpad.a = %d.%d, padman.irx = %d.%d]\n", 4, 0, major, ver & 0xFF);
        }
        return 0;
    }
    return scePadInit2(a0);
}

int scePadInit2(int a0)
{
    int ret;
    int i;

    for (i = 0; i < 4; i++) {
        padSlot[0][i].opened = 0;
        padSlot[0][i].pad18 = 0;
        padSlot[0][i].pad14 = 0;
        padSlot[1][i].opened = 0;
        padSlot[1][i].pad18 = 0;
        padSlot[1][i].pad14 = 0;
    }
    padRpcBuf[0] = 0x10;
    padRpcBuf[4] = 0;
    ret = sceSifCallRpc(&padClient[0], 1, 0, padRpcBuf, 0x80, padRpcBuf, 0x80, 0, 0);
    if (ret < 0) {
        return 0;
    }
    return padRpcBuf[3];
}

int scePadEnd(void)
{
    int ret;
    int val;
    padRpcBuf[0] = 0xF;
    ret = sceSifCallRpc(&padClient[0], 1, 0, padRpcBuf, 0x80, padRpcBuf, 0x80, 0, 0);
    if (ret < 0) {
        return 0;
    }
    val = padRpcBuf[3];
    if (val == 1) {
        padInited = 0;
    }
    return val;
}

int scePadPortOpen(int port, int slot, void *addr)
{
    int ret;
    int val;
    int i;
    char *p;
    int *q;

    if (((unsigned int)addr & 0x3F) != 0) {
        if (padDebug != 0) {
            printf("libpad: buffer addr is not 64 byte align. %08x\n", addr);
        }
        return 0;
    }
    if (padSlot[port][slot].opened == 1) {
        if (padDebug != 0) {
            printf("libpad: pad port is already open [%d][%d]\n", port, slot);
        }
        return 0;
    }
    p = (char *)addr;
    for (i = 0; i < 2; i++) {
        *(int *)(p + 0x58) = 0;
        p[0x70] = 5;
        p[0x71] = 2;
        p[0x67] = 0;
        memset(p, 0xFF, 0x20);
        *(int *)(p + 0x60) = 0;
        p += 0x80;
    }
    padRpcBuf[0] = 1;
    padRpcBuf[1] = port;
    padRpcBuf[2] = slot;
    padRpcBuf[4] = (int)addr;
    ret = sceSifCallRpc(&padClient[0], 1, 0, padRpcBuf, 0x80, padRpcBuf, 0x80, 0, 0);
    if (ret < 0) {
        return 0;
    }
    /* q is this slot's command buffer, padCmd[2][4][16]; reply word 5 is
     * the IOP buffer address, read as a pointer. */
    q = padCmd[port][slot];
    val = (int)*(void **)&padRpcBuf[5];
    padSlot[port][slot].opened = 1;
    padSlot[port][slot].iopBuf = (int *)val;
    padSlot[port][slot].dmaArea = addr;
    padSlot[port][slot].cmdBuf = q;
    q[0] = 0;
    padSlot[port][slot].dmaId = 0;
    return padRpcBuf[3];
}

int scePadPortClose(int port, int slot)
{
    int ret;

    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    padRpcBuf[0] = 0xE;
    padRpcBuf[1] = port;
    padRpcBuf[2] = slot;
    padRpcBuf[4] = 1;
    ret = sceSifCallRpc(&padClient[0], 1, 0, padRpcBuf, 0x80, padRpcBuf, 0x80, 0, 0);
    if (ret < 0) {
        return 0;
    }
    padSlot[port][slot].opened = 0;
    return padRpcBuf[3];
}

int scePadGetDmaStr(int port, int slot)
{
    int s0;
    int v0, v1, r;
    s0 = *(int *)((char *)padSlot + slot * 0x1C + port * 0x70);
    SyncDCache((char *)s0, (char *)s0 + 0x100);
    v0 = *(int *)(s0 + 0x58);
    v1 = *(int *)(s0 + 0xD8);
    r = (v0 < v1);
    return s0 + (r << 7);
}

int scePadGetFrameCount(int port, int slot)
{
    int ret = 0;
    if (padSlot[port][slot].opened == 0) {
        return ret;
    }
    return *(int *)(scePadGetDmaStr(port, slot) + 0x58);
}

int scePadRead(int port, int slot, void *data)
{
    int s0;
    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    s0 = scePadGetDmaStr(port, slot);
    memcpy(data, (void *)s0, *(int *)(s0 + 0x60));
    return *(int *)(s0 + 0x60);
}

int scePadGetState(int port, int slot)
{
    unsigned char *p;
    if (padSlot[port][slot].opened == 0)
        return 0x63;
    p = (unsigned char *)scePadGetDmaStr(port, slot);
    if (p[0x70] != 6)
        return p[0x70];
    if (p[0x71] == 2)
        return 5;
    return p[0x70];
}

static char *padStateStr[8] = {"DISCONNECT", "",        "FINDCTP1", "",
                               "",           "EXECCMD", "STABLE",   "ERROR"}; /* derived name */

void scePadStateIntToStr(unsigned int state, char *str)
{
    if (state < 8) {
        strcpy(str, padStateStr[state]);
    } else {
        strcpy(str, "");
    }
}

int scePadSetReqState(int port, int slot, int state)
{
    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    ((unsigned char *)scePadGetDmaStr(port, slot))[0x71] = state;
    return 1;
}

int scePadGetReqState(int port, int slot)
{
    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    return ((unsigned char *)scePadGetDmaStr(port, slot))[0x71];
}

static char *padReqStr[3] = {"COMPLETE", "FAILED", "BUSY"}; /* derived name */

void scePadReqIntToStr(unsigned int state, char *str)
{
    if (state < 4) {
        strcpy(str, padReqStr[state]);
    } else {
        strcpy(str, "");
    }
}

/* the pad DMA buffer scePadGetDmaStr hands back, laid out from the offsets
   this TU's members use: the input data scePadRead copies out (scePadPortOpen
   fills its first 32 bytes with 0xFF), the actuator and combination tables of
   four-byte records, the mode table scePadInfoMode indexes, the frame count
   and data size, the levels of information the pad has answered with, and the
   state bytes scePadGetState and scePadSetReqState use.  A byte nothing reads
   is padding. */
typedef struct {               /* field names derived */
    unsigned char data[32];    /* 0x00 */
    unsigned char pad20[16];   /* 0x20 */
    unsigned char act[4][4];   /* 0x30 */
    unsigned char comb[4][4];  /* 0x40 */
    unsigned short modeTbl[4]; /* 0x50 */
    int frame;                 /* 0x58 */
    unsigned char pad5C[4];    /* 0x5C */
    int size;                  /* 0x60 */
    unsigned char infoLevel;   /* 0x64, 2 or more once the act and comb tables are filled */
    unsigned char modeId;      /* 0x65, the pad's id, 0xF3 for none */
    unsigned char maskLevel;   /* 0x66, 2 or more once the button mask is filled */
    unsigned char pad67;       /* 0x67 */
    unsigned char nmode;       /* 0x68 */
    unsigned char curMode;     /* 0x69 */
    unsigned char nact;        /* 0x6A */
    unsigned char ncomb;       /* 0x6B */
    unsigned char pad6C[4];    /* 0x6C */
    unsigned char state;       /* 0x70 */
    unsigned char reqState;    /* 0x71 */
    unsigned char infoValid;   /* 0x72, 1 when the pad's information is valid */
} PadDmaStr;                   /* derived name */

int scePadInfoAct(int port, int slot, int act, int term)
{
    PadDmaStr *p;

    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    p = (PadDmaStr *)scePadGetDmaStr(port, slot);
    if (p->infoValid != 1) {
        return 0;
    }
    if (p->infoLevel < 2) {
        return 0;
    }
    if (act >= p->nact) {
        return 0;
    }
    if (act == -1) {
        return p->nact;
    }
    switch (term) {
    case 1:
        return p->act[act][0];
    case 2:
        return p->act[act][1];
    case 3:
        return p->act[act][2];
    case 4:
        return p->act[act][3];
    }
    return 0;
}

int scePadInfoComb(int port, int slot, int comb, int term)
{
    PadDmaStr *p;

    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    p = (PadDmaStr *)scePadGetDmaStr(port, slot);
    if (p->infoValid != 1) {
        return 0;
    }
    if (p->infoLevel < 2) {
        return 0;
    }
    if (comb == -1) {
        return p->ncomb;
    }
    if (comb >= p->ncomb) {
        return 0;
    }
    switch (term) {
    case -1:
        return p->comb[comb][0];
    case 0:
        return p->comb[comb][1];
    case 1:
        return p->comb[comb][2];
    case 2:
        return p->comb[comb][3];
    }
    return 0;
}

int scePadInfoMode(int port, int slot, int term, int index)
{
    int q;
    int t72;
    int v;

    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    q = scePadGetDmaStr(port, slot);
    t72 = *(unsigned char *)(q + 0x72);
    if (t72 == 1 && *(unsigned char *)(q + 0x71) != 2) {
        if (term == 2) {
            goto case2;
        }
        if (term >= 3) {
            goto ge3;
        }
        if (term == t72) {
            goto case1;
        }
        return 0;
    ge3:
        if (term == 3) {
            goto case3;
        }
        if (term == 4) {
            goto case4;
        }
        return 0;
    case1:
        v = *(unsigned char *)(q + 0x65);
        if (v == 0xF3) {
            return 0;
        }
        return v >> 4;
    case2:
        if (*(unsigned char *)(q + 0x64) == t72) {
            return 0;
        }
        return *(unsigned short *)(q + (*(unsigned char *)(q + 0x69) << 1) + 0x50);
    case3:
        if (*(unsigned char *)(q + 0x64) == t72) {
            return 0;
        }
        return *(unsigned char *)(q + 0x69);
    case4:
        if (*(unsigned char *)(q + 0x64) == t72) {
            return 0;
        }
        if (index == -1) {
            return *(unsigned char *)(q + 0x68);
        }
        if (index >= (int)*(unsigned char *)(q + 0x68)) {
            return 0;
        }
        return *(unsigned short *)(q + (index << 1) + 0x50);
    }
    return 0;
}

int scePadSetMainMode(int port, int slot, int mode, int option)
{
    int *s0 = padRpcBuf;
    void *local = 0;
    int ret;
    int s;
    padRpcBuf[0] = 6;
    s0[1] = port;
    s0[2] = slot;
    s0[3] = mode;
    s0[4] = option;
    ret = sceSifCallRpc(&padClient[0], 1, 0, s0, 0x80, s0, 0x80, 0, local);
    if (ret < 0) {
        return 0;
    }
    s = s0[5];
    if (s == 1) {
        scePadSetReqState(port, slot, 2);
        s = s0[5];
    }
    return s;
}

int scePadSetActDirect(int port, int slot, unsigned char *act)
{
    int *p;
    unsigned char *q;
    int i;

    if (((unsigned char *)scePadGetDmaStr(port, slot))[0x72] != 1) {
        return 0;
    }
    p = padSlot[port][slot].cmdBuf;
    q = (unsigned char *)p + 0xC;
    for (i = 0; i < 6; i++) {
        q[i] = act[i];
    }
    p[2] = 6;
    p[1] = 1;
    _send_to_iop(port, slot);
    return 1;
}

int scePadSetActAlign(int port, int slot, char *act)
{
    int *buf = padRpcBuf;
    int i;
    int val;
    int ret;
    char *dst;
    padRpcBuf[0] = 8;
    buf[1] = port;
    buf[2] = slot;
    dst = (char *)buf + 0xC;
    for (i = 0; i < 6; i++) {
        dst[i] = act[i];
    }
    ret = sceSifCallRpc(&padClient[0], 1, 0, buf, 0x80, buf, 0x80, 0, 0);
    if (ret < 0) {
        return 0;
    }
    val = buf[5];
    if (val == 1) {
        scePadSetReqState(port, slot, 2);
        val = buf[5];
    }
    return val;
}

int scePadGetButtonMask(int port, int slot)
{
    unsigned char *p;

    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    p = (unsigned char *)scePadGetDmaStr(port, slot);
    if (p[0x72] != 1) {
        return 0;
    }
    if (p[0x64] < 2) {
        return 0;
    }
    if (p[0x66] < 2) {
        return 0;
    }
    return (long long)p[0x79] + ((long long)p[0x7A] << 8) + ((long long)p[0x7B] << 16) +
           ((long long)p[0x7C] << 24);
}

int scePadSetButtonInfo(int port, int slot, int mask)
{
    int ret;
    padRpcBuf[3] = mask;
    padRpcBuf[0] = 0xA;
    padRpcBuf[1] = port;
    padRpcBuf[2] = slot;
    if (sceSifCallRpc(&padClient[0], 1, 0, padRpcBuf, 0x80, padRpcBuf, 0x80, 0, 0) < 0) {
        return 0;
    }
    ret = padRpcBuf[4];
    if (ret == 1) {
        scePadSetReqState(port, slot, 2);
        ret = padRpcBuf[4];
    }
    return ret;
}

int scePadInfoPressMode(int port, int slot)
{
    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    return scePadGetButtonMask(port, slot) == 0x3FFFF;
}

int scePadEnterPressMode(int port, int slot)
{
    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    return scePadSetButtonInfo(port, slot, 0xFFF);
}

int scePadExitPressMode(int port, int slot)
{
    if (padSlot[port][slot].opened == 0) {
        return 0;
    }
    return scePadSetButtonInfo(port, slot, 0);
}

int scePadSetVrefParam(int port, int slot, void *param)
{
    int r;
    padRpcBuf[1] = port;
    padRpcBuf[0] = 0xB;
    padRpcBuf[2] = slot;
    *(struct S12 *)((char *)padRpcBuf + 0xC) = *(struct S12 *)param;
    r = sceSifCallRpc(&padClient[0], 1, 0, padRpcBuf, 0x80, padRpcBuf, 0x80, 0, 0);
    if (r < 0) {
        return 0;
    }
    if (padRpcBuf[7] == 1) {
        scePadSetReqState(port, slot, 2);
    }
    return padRpcBuf[7];
}

int scePadGetPortMax(void)
{
    int ret;
    padRpcBuf[0] = 0xC;
    ret = sceSifCallRpc(&padClient[0], 1, 0, padRpcBuf, 0x80, padRpcBuf, 0x80, 0, 0);
    if (ret < 0) {
        return 0;
    }
    return padRpcBuf[3];
}

int scePadGetSlotMax(int port)
{
    int ret;
    padRpcBuf[0] = 0xD;
    padRpcBuf[1] = port;
    ret = sceSifCallRpc(&padClient[0], 1, 0, padRpcBuf, 0x80, padRpcBuf, 0x80, 0, 0);
    if (ret < 0) {
        return 0;
    }
    return padRpcBuf[3];
}

int scePadGetModVersion(void)
{
    int ret;
    padRpcBuf[0] = 0x12;
    ret = sceSifCallRpc(&padClient[0], 1, 0, padRpcBuf, 0x80, padRpcBuf, 0x80, 0, 0);
    if (ret < 0) {
        return 0;
    }
    return padRpcBuf[3];
}

int scePadSetWarningLevel(int level)
{
    int ret;
    padRpcBuf[0] = 0x14;
    padRpcBuf[1] = level;
    ret = sceSifCallRpc(&padClient[0], 1, 0, padRpcBuf, 0x80, padRpcBuf, 0x80, 0, 0);
    if (ret < 0) {
        return 0;
    }
    return padRpcBuf[2];
}
