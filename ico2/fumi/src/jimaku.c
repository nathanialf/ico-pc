#include "cdvd.h"
#include "message.h"
#include "thread.h"
#include "Texture.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>
#include "debug.h"
#include "debug_exception.h"

struct jNode { /* field names derived */
    char pad0[4];
    int status;
    int field8;
    int fieldC;
    char pad10[4];
    int field14;
}; /* derived name */

struct jWayGroup { /* field names derived */ /* jimakuRing element, stride 0x18 */
    int block;                               /* 0x00, the disc block read into buf, -1 when none */
    int state;                               /* 0x04, 1 shown, 2 done with, 3 free, 4 read */
    int tex;                                 /* 0x08, the texture made from buf, -1 when none */
    int stamp;                               /* 0x0C, lock_execIcoMisc when tex was made */
    struct jWayGroup *node;                  /* the next group in the ring */
    char *buf;                               /* 0x14 its 0x8C40 read buffer */
}; /* derived name */

/* the four-group read ring, the groups' CD read buffers (64-byte aligned as
   DMA targets) and three semaphore records of iosSemaCreate's 13 words: read
   done (signalled by jimakuHandler), shown five frames (jimakuDisp) and one
   per frame (jimakuDisp) */
static struct jWayGroup jimakuRing[4];

static char jimakuBuf[4][0x8C40] __attribute__((aligned(64))); /* derived name */

static IosSema jimakuReadSema; /* derived name */

static IosSema jimakuShownSema; /* derived name */

static IosSema jimakuFrameSema; /* derived name */

#include "jimaku.h"
#include "layout_texture.h" /* texProperty: jimaku's entries are 434 and 435 */
#include "gflag.h"

typedef struct JimCol { /* field names derived */
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} JimCol; /* derived name */

/* .data, all zero: the subtitle thread's record and its 8 KB stack, the
   manager's message queue and the request the script actors hand it.
   .sdata: the display time in frames, the display flag, jimakuOn,
   display_texture's colour initialiser (a 4-byte template), then
   jimakuMgrNext's strings and jimakuMsgBuf. */
IOSThread jimakuThread = {0};

char jimakuThreadStack[8192] = {0};

IosMsgQueue jimakuMsgQ = {0};

JimakuArg jimaku_msg = {0};

static int jimakuDispTime = 120; /* derived name */

static int jimakuDispOn = 0; /* derived name */

int jimakuOn = 1;

/* as in GifPacket.h, which this TU does not include (gif_SpriteSensitiveOffset differs) */
extern void gif_StartPacketPri(int pri);
/* as in GifPacket.h, which this TU does not include (gif_SpriteSensitiveOffset differs) */
extern void gif_SetAlpha(long long alpha, long long mode, long long fix);
/* as in GifPacket.h, which this TU does not include (gif_SpriteSensitiveOffset differs) */
extern void gif_SetGsReg(long long reg, long long data);
/* as in GifPacket.h, which this TU does not include (gif_SpriteSensitiveOffset differs) */
extern void gif_SetZWrite(int on);
/* as in GifPacket.h, which this TU does not include (gif_SpriteSensitiveOffset differs) */
extern void gif_SetZTest(int on);

/* z is unsigned int here, long long in GifPacket.h; the host uses the
   definition's type (layout_texture.c says why) */

extern void gif_SpriteSensitiveOffset(int *r, long long z, int *uv, JimCol *col, int prim);
/* as in GifPacket.h, which this TU does not include (gif_SpriteSensitiveOffset differs) */
extern void gif_EndPacket(void);

#ifdef ICO_RD

/* PC port (renderer R7d): GifHost.h's key of the decoder's primitives, so
   the presenter matches a subtitle's two rows between ticks by its block */
extern void gif_HostDrawKey(const void *obj, int part, int ordinal);

#define JIM_HOST_KEY(obj, part) gif_HostDrawKey((obj), (part), 0)
#else
#define JIM_HOST_KEY(obj, part) ((void)0)
#endif

static void display_texture(LtProperty *t)
{
    JimCol col = {128, 128, 128, 128};
    int dst[4];
    int src[4];

    src[0] = (t->texU << 4) + 8;
    src[1] = (t->texV << 4) + 8;
    src[2] = t->texW << 4;
    src[3] = t->texH << 4;
    dst[2] = t->dispW << 4;
    dst[3] = t->dispH << 3;
    if (dst[2] == 0)
        dst[2] = src[2];
    if (dst[3] == 0)
        dst[3] = src[3] >> 1;
    dst[3] = dst[3] * 2;
    if (t->centerX != 0)
        dst[0] = (10240 - dst[2]) / 2 - 5120;
    else
        dst[0] = (t->dispX - 320) << 4;
    dst[1] = (t->dispY - 112) << 4;
    tex_TransTexture(t->texNo, 11);
    gif_StartPacketPri(11);
    gif_SetAlpha(1, 7, 0);
    gif_SetGsReg(74, 0);
    gif_SetZWrite(0);
    gif_SetZTest(0);
    col.r = ~GlobalStageSetting.reductionCol[0];
    col.g = ~GlobalStageSetting.reductionCol[1];
    col.b = ~GlobalStageSetting.reductionCol[2];
    gif_SpriteSensitiveOffset(dst, 0xFFFFFF9B, src, &col, 1);
    gif_SetZWrite(1);
    gif_SetZTest(1);
    gif_EndPacket();
}

void iosCdvdBackGroundReadJimaku(CdvdBgReq *self, void *buf, int size)
{
    int large = size + 0x7FE;
    int v1 = size - 1;
    int neg_one = -1;
    if (neg_one < v1)
        large = v1;
    large = ((large >> 11) + 1) << 11;
    iosCdvdBackGroundRead(self, buf, large);
    iosCdvdBackGroundMgrSeek(self, self->pos + size);
}

static int jimakuHandler(CdvdBgReq *self, JimakuArg *p)
{
    JimakuSub *sub = &p->sub;
    struct jWayGroup *g;
    int size = 0x8440;
    int left;
    int n;

    while (sub->ringPos != (sub->n + 3) % 4) {
        g = &jimakuRing[sub->ringPos];
        if (g->state == 2) {
            g->state = 3;
            break;
        }
        /* read the record in pieces of at most 0x8C40 bytes */
        left = size;
        while (left > 0) {
            n = (0x8C40 < left) ? 0x8C40 : left;
            jimakuBuf[sub->ringPos][0] = -1;
            jimakuBuf[sub->ringPos][1] = -1;
            iosCdvdBackGroundReadJimaku(self, jimakuBuf[sub->ringPos], n);
            left -= n;
        }
        jimakuRing[sub->ringPos].block = sub->block++;
        jimakuRing[sub->ringPos].state = 4;
        iosCdvdBackGroundMgrSeek(sub->bg, sub->block * 0x8800);
        sub->ringPos = (sub->ringPos + 1) % 4;
    }
    if (systemStatus[10] != 0) {
        iosSemaReferStatus(&jimakuReadSema);
        if (jimakuReadSema.status.numWaitThreads > 0) {
            iosSemaSignal(&jimakuReadSema);
        }
    }
    return 0;
}

static void jimakuMgrBegin(JimakuArg *p)
{
    JimakuSub *sub = &p->sub;
    int st = 0;
    int i;
    struct jWayGroup *g;

    if (systemStatus[10] != 0) {
        return;
    }
    systemStatus[10] = 1;
    iosSemaCreate(&jimakuReadSema, 0, 1, 0);
    iosSemaCreate(&jimakuShownSema, 0, 1, 0);
    iosSemaCreate(&jimakuFrameSema, 0, 1, 0);
    for (i = 0; i < 4; i++) {
        g = &jimakuRing[i];
        g->node = &jimakuRing[(i + 1) % 4];
        g->buf = jimakuBuf[i];
    }
    sub->n = 0;
    jimakuRing[0].block = -1;
    jimakuRing[0].state = 3;
    jimakuRing[0].tex = -1;
    sub->ringPos = 1;
    switch (NonLinearCameraMove) {
    case 2:
        st = 0;
        break;
    case 3:
        st = 2;
        break;
    case 4:
        st = 4;
        break;
    case 5:
        st = 6;
        break;
    case 6:
        st = 8;
        break;
    }
    if (gFlagGameClear != 0) {
        st = st + 1;
    }
    sub->bg =
        (void *)iosCdvdBackGroundMgrAdd(jimakuFileName[st].path, jimakuHandler, p, 0, 0, 0, 0, 0);
    {
        JimakuSub *q = &p->sub;
        int m;

        iosCdvdBackGroundMgrSeek(q->bg, q->block * 0x8800);
        m = (q->ringPos = (q->n + 1) % 4);
        while (m != q->n) {
            jimakuRing[m].block = -1;
            jimakuRing[m].state = 3;
            jimakuRing[m].tex = -1;
            m = (m + 1) % 4;
        }
    }
}

/* The DEBUG build's switch to print the read ring's states after each Next;
   retail builds it as 0. */
#ifdef DEBUG
#define JIMAKU_DEBUG_DUMP (debug_font_flag & 0x400) /* derived name */
#else
#define JIMAKU_DEBUG_DUMP 0 /* derived name */
#endif

static void jimakuMgrNext(JimakuArg *p)
{
    char buf[16];
    JimakuSub *sub = &p->sub;
    struct jWayGroup *g = &jimakuRing[sub->n];

    while (g->node->state != 4) {
        if (iosSemaWait(&jimakuReadSema) < 0) {
            return;
        }
    }
    if (iosSemaWait(&jimakuReadSema) < 0) {
        return;
    }
    g->node->state = 1;
    sprintf(buf, "jimaku%02d.tm2", (sub->n + 1) % 4);
    g->node->tex = tex_InitTexture(buf, g->node->buf);
    tex_SetSamplingType(tex_GetTextureData(g->node->tex), 1, 1);
    g->node->stamp = lock_execIcoMisc;
    if (g->node->tex == -1) {
        debug_StdPrintfDummy("already exist\n");
        debug_assert(__FILE__, 688);
        __assert(__FILE__, 688, "0");
    }
    sub->n = (sub->n + 1) % 4;
    if (iosSemaWait(&jimakuShownSema) < 0) {
        return;
    }
    g->state = 2;
    if (g->tex >= 0) {
        tex_FreeTexture(g->tex);
    }
    sub->cur = jimakuBuf[sub->n];
    jimakuDispOn = 1;
    /* the DEBUG build's dump of the four groups' states, the current one
     * marked */
    if (JIMAKU_DEBUG_DUMP) {
        int m;

        for (m = 0; m < 4; m++) {
            debug_StdPrintfDummy(m == sub->n ? ">%d" : " %d", jimakuRing[m].state);
        }
        debug_StdPrintfDummy("\n");
    }
}

static void jimakuMgrJump(JimakuArg *p)
{
    JimakuSub *q = &p->sub;
    int m;

    iosCdvdBackGroundMgrSeek(q->bg, q->block * 0x8800);
    m = (q->ringPos = (q->n + 1) % 4);
    while (m != q->n) {
        jimakuRing[m].block = -1;
        jimakuRing[m].state = 3;
        jimakuRing[m].tex = -1;
        m = (m + 1) % 4;
    }
    jimakuMgrNext(p);
}

static void jimakuMgrEnd(JimakuArg *p)
{
    CdvdBgReq *val = p->sub.bg;
    if (val != 0) {
        iosCdvdBackGroundMgrDelete(val);
    }
    iosSemaDelete(&jimakuFrameSema);
    iosSemaDelete(&jimakuShownSema);
    iosSemaDelete(&jimakuReadSema);
}

IosMsgWord jimakuMsgBuf[2] = {0};

inline void jimakuManager(void)
{
    JimakuArg *msg;

    iosMsgQueueCreate(&jimakuMsgQ, jimakuMsgBuf, 2);
    while (1) {
        iosMsgRecv(&jimakuMsgQ, (IosMsgWord *)&msg, 1);
        msg->done = 0;
        switch (msg->cmd) {
        case 0:
            jimakuMgrBegin(msg);
            break;
        case 1:
            jimakuMgrNext(msg);
            break;
        case 2:
            jimakuMgrJump(msg);
            break;
        case 3:
            jimakuMgrEnd(msg);
            break;
        default:
            debug_StdPrintfDummy("jimakuManager: recv command %d error.", msg->cmd);
            break;
        }
        msg->done = 1;
    }
}

void jimakuBegin(JimakuArg *msg)
{
    msg->cmd = 0;
    iosMsgSend(&jimakuMsgQ, (IosMsgWord)msg, 1);
}

void jimakuNext(JimakuArg *msg)
{
    if (systemStatus[10] != 0) {
        msg->cmd = 1;
        iosMsgSend(&jimakuMsgQ, (IosMsgWord)msg, 0);
    }
}

void jimakuJump(JimakuArg *msg)
{
    JimakuSub *sub = &msg->sub;
    if (systemStatus[10] == 0)
        return;
    {
        int v = sub->jump;

        if (v == -1) {
            jimakuDispTime = ((60 - systemStatus[0] * 10) / systemStatus[1]) << 2;
        } else {
            jimakuDispTime = v;
        }
    }
    msg->cmd = 2;
    iosMsgSend(&jimakuMsgQ, (IosMsgWord)msg, 0);
}

void jimakuEnd(JimakuArg *msg)
{
    systemStatus[10] = 0;
    jimakuMgrEnd(msg);
}

void jimakuDisp(JimakuArg *msg)
{
    struct jWayGroup *g = &jimakuRing[msg->sub.n];
    int c;

    if (systemStatus[10] == 0) {
        return;
    }
    c = g->stamp;
    if ((unsigned int)(c + jimakuDispTime) < (unsigned int)lock_execIcoMisc) {
        jimakuDispOn = 0;
    }
    if ((unsigned int)(c + 5) < (unsigned int)lock_execIcoMisc) {
        iosSemaReferStatus(&jimakuShownSema);
        if (jimakuShownSema.status.numWaitThreads > 0) {
            iosSemaSignal(&jimakuShownSema);
        }
    }
    if (systemStatus[10] != 0) {
        iosSemaReferStatus(&jimakuFrameSema);
        if (jimakuFrameSema.status.numWaitThreads > 0) {
            iosSemaSignal(&jimakuFrameSema);
        }
    }
    if (jimakuDispOn != 0) {
        int v = g->tex;
        texProperty[435].texNo = v;
        texProperty[434].texNo = v;
        if (v < 0) {
            return;
        }
        if (jimakuOn != 0) {
            JIM_HOST_KEY(g, 0);
            display_texture(&texProperty[434]);
            JIM_HOST_KEY(g, 1);
            display_texture(&texProperty[435]);
            JIM_HOST_KEY(0, 0);
        }
    }
}

inline void jimakuUndisp(JimakuArg *msg)
{
    jimakuDispOn = 0;
    jimakuOn = 0;
}
