#include "debug.h"
#include "s_init.h"
#include "pad.h"
#include "main.h"
#include "mcdata.h"
#include <string.h>
#include "mcard.h"
#include <eekernel.h>
#include <libmc.h>
#include <stdlib.h>
#include <stdio.h>
#include "message.h"
#include "typedef.h"

/* the semaphore descriptor the card lock is created from, then the manager
   queue's 16-slot message ring */
static struct SemaParam mcLockSemaParam;

static int mcMsgRing[16]; /* derived name */

/* .data: the three icon file names, the save segment names, then after the
   block handlers the segment table, the two product records, the preview
   record and the manager's queue.
   .sdata: the two words, the "game." name (emitted with the segment names,
   last first, as are the two longer ones in .rodata), the lock count, then
   the literals in first-use order. */
int IosMcMgrSleep = 0;

int IosMcLock = -1;

char *iconName[3] = {"boy_blk.ico", "boy_blk.ico", "boy_blk.ico"};

char *iOSMcSaveSeg[6] = {"BESCES-50760ico", "icon.sys",    "boy_blk.ico",
                         "boy_blk.ico",     "boy_blk.ico", "game."};

static int mcLockCount = 0; /* derived name */

inline void iosMcMgrSync(McMgr *mp)
{
    mcLockSemaParam.attr = 1;
    mcLockSemaParam.initCount = 0;
    mcLockSemaParam.maxCount = 1;
    IosMcLock = CreateSema(&mcLockSemaParam);
    debug_StdPrintfDummy("%d\n", IosMcLock);
    do {
        WaitSema(IosMcLock);
        mcLockCount++;
    } while (sceMcSync(1, &mp->cmd, &mp->result) == 0);
    DeleteSema(IosMcLock);
    IosMcLock = -1;
}

inline void iosMcTest(void) {}

inline int iosMcSync(McMgr *mp)
{
    unsigned long long x = mp->flags.ll;
    char y = x;
    unsigned long long z = y & 1ull;
    y = z;
    return -((int)y);
}

inline int iosMcGetInfo(McMgr *mp)
{
    mp->flags.w.command = 0;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcFormat(McMgr *mp)
{
    mp->flags.w.command = 3;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcUnformat(McMgr *mp)
{
    mp->flags.w.command = 4;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcGetDir(McMgr *mp)
{
    mp->flags.w.command = 6;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcDelete(McMgr *mp)
{
    mp->flags.w.command = 2;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcSaveIconBlock(McMgr *mp)
{
    mp->flags.w.command = 7;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcSaveProductBlock(McMgr *mp)
{
    mp->flags.w.command = 8;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcLoadProductBlock(McMgr *mp)
{
    mp->flags.w.command = 9;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcSaveGameBlock(McMgr *mp, void *arg)
{
    mp->flags.w.command = 10;
    mp->segArg = arg;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcLoadGameBlock(McMgr *mp, void *arg)
{
    mp->flags.w.command = 11;
    mp->segArg = arg;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcChdirProduct(McMgr *mp)
{
    mp->flags.w.command = 12;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

inline int iosMcGetBlockSaveInfo(McMgr *mp)
{
    mp->flags.w.command = 13;
    mp->flags.ll = mp->flags.ll & -2;
    return iosMsgSend(&McMsgQ, mp, 0);
}

typedef struct { /* field names derived */
    char b[64];
} McBlk; /* derived name */

static inline int product_write(McMgr *self)
{
    (IosMcProductFile + self->port)->soundMode = systemStatus[11];
    (IosMcProductFile + self->port)->outputMode = soundOutputModeGet();
    (IosMcProductFile + self->port)->vibration = iosPadActRequestEnable;
    (IosMcProductFile + self->port)->controlType = optionControlType;
    (IosMcProductFile + self->port)->cameraMove = NonLinearCameraMove;
    (IosMcProductFile + self->port)->palMode = systemStatus[0];
    *(McBlk *)(IosMcProductFile + self->port)->padConf = *(McBlk *)iosPadConfCustom.bit;
    iosMcHandlerWrite(self, (unsigned char *)(IosMcProductFile + self->port),
                      sizeof(McProductFile));
    return 0;
}

static inline int product_read(McMgr *self)
{
    int idx = self->port;
    iosMcHandlerRead(self, (unsigned char *)&IosMcProductFile[idx], sizeof(McProductFile));
    return self->result;
}

static inline int gameblock_write(McMgr *self, void *buf)
{
    iosMcHandlerWrite(self, buf, 25588);
    iosMcHandlerWrite(self, (unsigned char *)&optionScreenMode, 4);
    iosMcHandlerWrite(self, (unsigned char *)&girlControlMode, 4);
    return 0;
}

static inline int gameblock_read(McMgr *self, void *buf)
{
    iosMcHandlerRead(self, buf, 25588);
    systemStatus[11] = (IosMcProductFile + self->port)->soundMode;
    soundOutputModeSet((IosMcProductFile + self->port)->outputMode);
    iosPadActRequestEnable = (IosMcProductFile + self->port)->vibration;
    optionControlType = (IosMcProductFile + self->port)->controlType;
    *(McBlk *)iosPadConfCustom.bit = *(McBlk *)(IosMcProductFile + self->port)->padConf;
    iosMcHandlerRead(self, (unsigned char *)&optionScreenMode, 4);
    iosMcHandlerRead(self, (unsigned char *)&girlControlMode, 4);
    return self->result;
}

/* the product directory name, 17 bytes including the terminator, copied as
   a byte-aligned record */
typedef struct { /* field names derived */
    char c[17];
} McName; /* derived name */

static void iosMcMgrGetInfo(McMgr *mp)
{
    int r;

    while ((r = sceMcGetInfo(mp->port, mp->slot, &mp->type, &mp->free, &mp->format)) != 0) {
        debug_StdPrintfDummy("iosMcMgrGetInfo: request busy %d\n", r);
    }

    iosMcMgrSync(mp);

    debug_StdPrintfDummy("mcMgrGetInfo result:%d type:%d\n", mp->result, mp->type);

    if (mp->result != 0 && mp->result >= -10) {
        mp->cardState = mp->result;
    }
}

/* the file-static card helpers the manager entry points inline */
static inline void iosMcMgrFormat(McMgr *mp) /* derived name */
{
    while (sceMcFormat(mp->port, mp->slot) > 0) {
        debug_StdPrintfDummy("iosMcMgrFormat: request busy\n");
    }

    iosMcMgrSync(mp);

    if (mp->result == 0) {
        mp->cardState = -1;
    }
}

static inline void iosMcMgrUnformat(McMgr *mp) /* derived name */
{
    while (sceMcUnformat(mp->port, mp->slot) > 0) {
        debug_StdPrintfDummy("iosMcMgrUnformat: request busy\n");
    }

    iosMcMgrSync(mp);

    if (mp->result == 0) {
        mp->cardState = -2;
    }
}

static inline void iosMcMgrWrite(McMgr *mp, void *p) /* derived name */
{
    while (sceMcWrite(mp->fd, p, mp->size) > 0) {
        debug_StdPrintfDummy("iosMcMgrWrite: request busy\n");
    }

    iosMcMgrSync(mp);
}

static inline void iosMcMgrRead(McMgr *mp, void *p) /* derived name */
{
    while (sceMcRead(mp->fd, p, mp->size) > 0) {
        debug_StdPrintfDummy("iosMcMgrRead: request busy\n");
    }

    iosMcMgrSync(mp);
}

static inline void iosMcMgrOpen(McMgr *mp) /* derived name */
{
    while (sceMcOpen(mp->port, mp->slot, mp->path, mp->openMode) > 0) {
        debug_StdPrintfDummy("sceMcOpen: request busy\n");
    }

    iosMcMgrSync(mp);

    if (mp->result < 0) {
        return;
    }

    mp->fd = mp->result;
    mp->sum = 0;
    mp->end = 0;
    mp->pos = 0;
}

static inline void iosMcMgrClose(McMgr *mp) /* derived name */
{
    while (sceMcClose(mp->fd) > 0) {
        debug_StdPrintfDummy("sceMcClose: request busy\n");
    }

    iosMcMgrSync(mp);
}

static inline void iosMcMgrChdir(McMgr *mp) /* derived name */
{
    while (sceMcChdir(mp->port, mp->slot, mp->dirName, mp->pwd) > 0) {
        debug_StdPrintfDummy("iosMcMgrChdir: request busy\n");
    }

    iosMcMgrSync(mp);
}

static inline void iosMcMgrGetDir(McMgr *mp) /* derived name */
{
    while (sceMcGetDir(mp->port, mp->slot, mp->path, 0, 20, mp->dir) > 0) {
        debug_StdPrintfDummy("sceMcGetdir: request busy\n");
    }

    iosMcMgrSync(mp);

    if (mp->result < 0) {
        mp->dirCount = 0;
    } else {
        mp->dirCount = mp->result;
    }
}

static inline void iosMcMgrMkdir(McMgr *mp) /* derived name */
{
    while (sceMcMkdir(mp->port, mp->slot, mp->path) > 0) {
        debug_StdPrintfDummy("iosMcMgrMkdir: request busy\n");
    }

    iosMcMgrSync(mp);
}

static inline void iosMcMgrDelete(McMgr *mp) /* derived name */
{
    while (sceMcDelete(mp->port, mp->slot, mp->path) > 0) {
        debug_StdPrintfDummy("iosMcMgrDelete: request busy\n");
    }

    iosMcMgrSync(mp);
}

static inline void iosMcMgrSum(McMgr *mp, void *q, int n) /* derived name */
{
    unsigned char *p = q;
    int i;

    for (i = 0; i < n; i++) {
        mp->sum += p[i];
    }
}

void iosMcHandlerWrite(McMgr *mp, unsigned char *buf, int len)
{
    int n;
    int m;

    if (len > 1024) {
        if (mp->pos != 0) {
            mp->size = mp->pos;
            iosMcMgrWrite(mp, mp->buf);

            if (mp->result < 0) {
                return;
            }

            mp->pos = 0;
        }

        mp->size = len;
        iosMcMgrWrite(mp, buf);
        iosMcMgrSum(mp, buf, mp->size);

        if (mp->result < 0) {
            return;
        }

        len = 0;
    }

    while (len > 0) {
        n = 1024 - mp->pos;

        if (len < n) {
            m = len;
        } else {
            m = n;
        }

        memcpy(mp->buf + mp->pos, buf, m);
        iosMcMgrSum(mp, mp->buf + mp->pos, m);

        buf += m;
        mp->pos += m;
        n -= m;

        if (n == 0) {
            mp->size = 1024;
            iosMcMgrWrite(mp, mp->buf);

            if (mp->result < 0) {
                return;
            }

            mp->pos = 0;
        }

        len -= m;
    }

    sceMcFlush(mp->fd);
    iosMcMgrSync(mp);
}

void iosMcHandlerRead(McMgr *mp, unsigned char *buf, int len)
{
    int n;

    if (len > 1024) {
        if (mp->pos != 0) {
            n = mp->end - mp->pos;
            memcpy(buf, mp->buf + mp->pos, n);
            iosMcMgrSum(mp, mp->buf + mp->pos, n);
            buf += n;
            len -= n;
        }

        mp->size = len;
        iosMcMgrRead(mp, buf);
        iosMcMgrSum(mp, buf, len);

        if (mp->result < 0) {
            return;
        }

        mp->end = 0;
        mp->pos = 0;
        len = 0;
    }

    while (len > 0) {
        debug_StdPrintfDummy("%d\n", len);

        if (mp->pos == 0) {
            mp->size = 1024;
            iosMcMgrRead(mp, mp->buf);

            if (mp->result < 0) {
                return;
            }

            if ((mp->end = mp->result) == 0) {
                debug_StdPrintfDummy(
                    "iosMcHandlerRead: メモリカードからデータ読めなかった(リクエストの方がサイズ大きい) %d %d\n",
                    mp->pos, len);
                mp->result = -15;
                return;
            }
        }

        n = mp->end - mp->pos;

        if (n > len) {
            n = len;
        }

        memcpy(buf, mp->buf + mp->pos, n);
        iosMcMgrSum(mp, mp->buf + mp->pos, n);

        len -= n;
        mp->pos += n;
        buf += n;

        if (mp->pos >= mp->end) {
            mp->end = 0;
            mp->pos = 0;
        }
    }
}

static void iosMcMgrChdirProduct(McMgr *mp)
{
    int r;

retry:
    iosMcMgrGetInfo(mp);

    switch (mp->result) {
    case 0:
    case -1:
        r = 0;
        break;
    case -2:
        r = mp->cardState;
        break;
    default:
        r = -9;
        break;
    }

    if (r != 0) {
        mp->result = r;
        return;
    }

    if (mp->flags.ll & 2) {
        *(McName *)mp->path = *(McName *)"/BESCES-50760ico";
        iosMcMgrMkdir(mp);
        if (mp->result != 0 && mp->result != -4) {
            return;
        }
    }

    *(McName *)mp->dirName = *(McName *)"/BESCES-50760ico";
    iosMcMgrChdir(mp);

    if (mp->result != 0) {
        if (mp->result == -4) {
            mp->result = -14;
        }
    }

    if (mp->result != 0 && mp->result != -4 && mp->result != -14 && mp->result != -2) {
        goto retry;
    }
}

/* the per-slot segment table: one record per loadable block */
typedef struct {   /* field names derived */
    int id;        /* 0x0 */
    int (*load)(); /* 0x4 */
    int (*save)(); /* 0x8 */
} McSegEnt;        /* derived name */

/* mcdata.c's icon writers */

McSegEnt iOSMcSaveList[6] = {
    {1, 0, iosMcIconWriteIconsys},    {2, 0, iosMcIconWriteIcon},
    {3, 0, iosMcIconWriteIcon},       {4, 0, iosMcIconWriteIcon},
    {0, product_read, product_write}, {5, gameblock_read, gameblock_write},
};

McProductFile IosMcProductFile[2] = {0};

int IosMcPreviewInfo[6] = {0};

IosMsgQueue McMsgQ = {0};

static void iosMcMgrSaveSeg(McMgr *mp, char *suffix)
{
    int r = 0;
    unsigned int i;
    McSegEnt *e;

    mp->flags.ll = mp->flags.ll | 2;
    iosMcMgrChdirProduct(mp);

    if (mp->result != 0) {
        return;
    }

    mp->openMode = 0x203;
    strcpy(mp->path, iOSMcSaveSeg[mp->segment]);

    if (suffix != 0) {
        strcat(mp->path, suffix);
    }

    iosMcMgrOpen(mp);

    if (mp->result < 0) {
        return;
    }

    for (i = 0; i < 6; i++) {
        e = &iOSMcSaveList[i];
        if (e->id != mp->segment) {
            continue;
        }
        if (e->save == 0) {
            continue;
        }
        if (e->save(mp, mp->segArg) < 0) {
            r = -15;
            break;
        }
    }

    if (r == 0) {
        if (mp->pos != 0) {
            mp->size = mp->pos;
            iosMcMgrWrite(mp, mp->buf);

            if (mp->result < 0) {
                return;
            }

            mp->pos = 0;
        }

        if (mp->segment < 5) {
            if (mp->segment > 0) {
                goto flush;
            }
        }

        mp->size = 4;
        debug_StdPrintfDummy("write checkSum: %d\n", mp->sum);
        iosMcMgrWrite(mp, &mp->sum);
    }

flush:
    sceMcFlush(mp->fd);
    iosMcMgrSync(mp);

    iosMcMgrClose(mp);

    if (r != 0) {
        mp->result = r;
    }
}

static void iosMcMgrLoadSeg(McMgr *mp, char *suffix)
{
    int r = 0;
    unsigned int i;
    McSegEnt *e;

    mp->flags.ll = mp->flags.ll & -3;
    iosMcMgrChdirProduct(mp);

    if (mp->result != 0) {
        return;
    }

    mp->openMode = 1;
    strcpy(mp->path, iOSMcSaveSeg[mp->segment]);

    if (suffix != 0) {
        strcat(mp->path, suffix);
    }

    iosMcMgrOpen(mp);

    if (mp->result < 0) {
        return;
    }

    for (i = 0; i < 6; i++) {
        e = &iOSMcSaveList[i];
        if (e->id != mp->segment) {
            continue;
        }
        if (e->load == 0) {
            continue;
        }
        debug_StdPrintfDummy("call read_func\n");
        if (e->load(mp, mp->segArg) < 0) {
            r = -15;
            break;
        }
    }

    if (r == 0) {
        while (sceMcSeek(mp->fd, -4, 2) > 0) {
            debug_StdPrintfDummy("sceMcSeek: request busy\n");
        }

        iosMcMgrSync(mp);

        mp->size = 4;
        iosMcMgrRead(mp, &mp->readSum);

        if (mp->result < 0) {
            return;
        }

        if (mp->sum != mp->readSum) {
            r = -16;
        }
    }

    iosMcMgrClose(mp);

    if (r != 0) {
        mp->result = r;
    }
}

/* the three file-static block helpers the manager dispatch inlines.  The icon
   save ends with a DEBUG-build report of the card result. */
static __inline__ void iosMcMgrSaveIconDebugResult(int result) /* derived name */
{
#ifdef DEBUG
    debug_StdPrintfDummy("icon save result %d\n", result);
#endif
}

static inline void iosMcMgrSaveIcon(McMgr *mp) /* derived name */
{
    mp->segment = 1;
    mp->segArg = &iconFile[1];
    iosMcMgrSaveSeg(mp, 0);

    if (mp->result < 0) {
        return;
    }

    mp->segment = 2;
    mp->segArg = &iconFile[2];
    iosMcMgrSaveSeg(mp, 0);
    iosMcMgrSaveIconDebugResult(mp->result);
}

static void iosMcMgrSaveProductBlock(McMgr *mp)
{
    mp->segment = 0;
    iosMcMgrSaveSeg(mp, 0);
}

static void iosMcMgrLoadProductBlock(McMgr *mp)
{
    mp->segment = 0;
    iosMcMgrLoadSeg(mp, 0);
}

static inline void iosMcMgrSaveGame(McMgr *mp) /* derived name */
{
    char buf[16];

    sprintf(buf, "%3.3d", mp->fileNo);
    mp->segment = 5;
    iosMcMgrSaveSeg(mp, buf);
}

static inline void iosMcMgrLoadGame(McMgr *mp) /* derived name */
{
    char buf[16];

    sprintf(buf, "%3.3d", mp->fileNo);
    mp->segment = 5;
    iosMcMgrLoadSeg(mp, buf);
}

static void iosMcMgrGetBlockSaveInfo(McMgr *mp)
{
    int i;

    mp->mask = 0;
    mp->flags.ll = mp->flags.ll & -3;
    mp->dirCount = 0;

    iosMcMgrChdirProduct(mp);

    if (mp->result == -14) {
        mp->result = 0;
        return;
    }

    if (mp->result < 0) {
        return;
    }

    strcat(mp->path, "*");

    iosMcMgrGetDir(mp);

    if (mp->result < 0) {
        return;
    }

    for (i = 0; i < mp->dirCount; i++) {
        mp->mask |= 1 << atoi(&mp->dir[i].EntryName[strlen(mp->dir[i].EntryName) - 3]);
    }
}

void iosMcManager(void)
{
    McMgr *mp;
    McMgr **pp;

    sceMcInit();
    iosMsgQueueCreate(&McMsgQ, mcMsgRing, 16);
    pp = &mp;

    for (;;) {
        iosMsgRecv(&McMsgQ, pp, 1);
        debug_StdPrintfDummy("done 0 %p\n", mp);

        mp->flags.ll = mp->flags.ll & -2;
        debug_StdPrintfDummy("done 1\n");

        switch (mp->flags.w.command) {
        case 0:
            iosMcMgrGetInfo(mp);
            break;

        case 3:
            debug_StdPrintfDummy("format");
            iosMcMgrFormat(mp);
            break;

        case 4:
            debug_StdPrintfDummy("Unformat");
            iosMcMgrUnformat(mp);
            break;

        case 5:
            debug_StdPrintfDummy("chdir");
            iosMcMgrChdir(mp);
            break;

        case 6:
            debug_StdPrintfDummy("getdir");
            iosMcMgrGetDir(mp);
            break;

        case 2:
            debug_StdPrintfDummy("delete");
            iosMcMgrDelete(mp);
            break;

        case 7:
            debug_StdPrintfDummy("IconBlock save");
            iosMcMgrSaveIcon(mp);
            break;

        case 8:
            debug_StdPrintfDummy("ProductBlock save");
            iosMcMgrSaveProductBlock(mp);
            break;

        case 9:
            iosMcMgrLoadProductBlock(mp);
            break;

        case 10:
            debug_StdPrintfDummy("GameBlock save");
            iosMcMgrSaveGame(mp);
            break;

        case 11:
            debug_StdPrintfDummy("GameBlock load");
            iosMcMgrLoadGame(mp);
            break;

        case 12:
            debug_StdPrintfDummy("chdirproduct");
            iosMcMgrChdirProduct(mp);
            break;

        case 13:
            iosMcMgrGetBlockSaveInfo(mp);
            break;

        case 14:
            debug_StdPrintfDummy("test");
            iosMcMgrMkdir(mp);
            break;

        default:
            debug_StdPrintfDummy("iosMcManager: recv command %d error.", mp->flags.w.command);
            break;
        }

        mp->flags.ll = mp->flags.ll | 1;
    }
}
