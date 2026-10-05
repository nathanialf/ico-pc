#include "debug.h"
#include "cdvd.h"
#include "memory.h"
#include "matrixDrive.h"
#include "streamMotionManager.h"
#include "GsBase.h"
#include "BgAnimation.h"
#include "ios.h"
#include "main.h"
#include <string.h>
#include "thread.h"

static int _closeHander(void);
static int _handler(struct CdvdBgReq *self);
static void _deleteStreamMotionManager(void);

/* the stream entry count and state, the background reader's state and id,
   the ring buffer's read and write offsets and buffers, the header parse, the
   owner, the ring use, the idle flag and the frame clock */
static int streamNum = 0; /* derived name */

static int streamState = 0; /* derived name */

static int readState = 0; /* derived name */

static CdvdBgReq *bgMgrId = 0; /* derived name */

static int ringRead = 0; /* derived name */

static int ringWrite = 0; /* derived name */

static char *ringBuf = 0; /* derived name */

static char *readBuf = 0; /* derived name */

static char *readBufRaw = 0; /* derived name */

static int headerRead = 0; /* derived name */

static int headerSize = 0; /* derived name */

static char *streamOwner = 0; /* derived name */

static int ringUsed = 0; /* derived name */

static unsigned int streamIdle = 1; /* derived name */

static int framePlayed = 0; /* derived name */

static int frameTime = 0; /* derived name */

static int frameCount = 0; /* derived name */

/* One character's stream: the node and shape counts the frame header gives,
   the frame's size, the ring offsets of the current and the next frame, the
   object and the function called when the stream ends. */
typedef struct {  /* field names derived */
    int nodeNum;  /* 0x00, 8 bytes a node */
    int shapeNum; /* 0x04, 4 bytes a shape */
    int size;     /* 0x08, 16 + the two */
    int cur;      /* 0x0C, -1 until data arrives */
    int next;     /* 0x10 */

    union {
        int i;
        GObj *p;
    } obj; /* 0x14, stored as a word: EntryStreamMotion's store of it stays
              behind the reads of the object it follows only under an
              any-type view */

    void (*finishFunc)(GObj *); /* 0x18 */
} SMotion;                      /* derived name */

/* the ten stream-motion slots _deleteStreamMotionManager resets to
   emptyEntry */
static SMotion streamEntry[10]; /* derived name */

/* The ring is 163840 bytes; the check asks whether the write pointer has run
 * far enough ahead of the read pointer for `room` more bytes to be there. */
static inline int _checkRing(int room) /* derived name */
{
    unsigned int p = ringRead;
    unsigned int end = p + room;
    unsigned int q = ringWrite;
    int r;

    if (q < p) {
        q += 163840;
    }
    r = 1;
    if (!(q < p) && (int)q < (int)end) {
        r = 0;
    }
    return r;
}

static inline void _setEntryOffsets(void) /* derived name */
{
    unsigned int off = 0;
    int i;

    for (i = 0; i < streamNum; i++) {
        streamEntry[i].cur = streamEntry[i].next = (ringRead + off) % 163840;
        off += streamEntry[i].size;
    }
}

static inline void _setNextEntryOffsets(unsigned int base) /* derived name */
{
    unsigned int off = 0;
    int i;

    for (i = 0; i < streamNum; i++) {
        streamEntry[i].next = (base + off) % 163840;
        off += streamEntry[i].size;
    }
}

static inline void _advanceRing(int amt) /* derived name */
{
    ringUsed += amt;
    ringRead += amt;
    if ((unsigned int)ringRead > 163839) {
        ringRead -= 163840;
    }
}

static int _infoUpdate(void)
{
    int top;
    int n;
    int i;

    if (headerRead == 0) {
        if (_checkRing(4096)) {
            headerSize = 0;
            for (i = 0; i < streamNum; i++) {
                int a = ringRead + headerSize;

                streamEntry[i].nodeNum = *(unsigned char *)(ringBuf + (a + 2) % 163840);
                streamEntry[i].shapeNum = *(unsigned char *)(ringBuf + (a + 3) % 163840);
                streamEntry[i].size = 16 + streamEntry[i].nodeNum * 8 + streamEntry[i].shapeNum * 4;
                headerSize += streamEntry[i].size;
            }
            n = 3619;
            if (systemStatus[0] == 0) {
                n = 2997;
            }
            headerRead = 1;
            frameTime = n;
        } else {
            /* the data had not arrived in time when the stream motion started */
            debug_StdPrintfDummy(
                "ストリームモーション開始時にデータの転送が間に合っていませんでした。\n");
            iosCdvdStDelayCnt++;
            return 0;
        }
    }
    top = ringRead;
    framePlayed = 0;
    while (frameTime >= 2997) {
        if (!_checkRing(headerSize)) {
            /* the stream motion data transfer is not keeping up */
            debug_StdPrintfDummy("ストリームモーションのデータ転送が間に合っていません。\n");
            iosCdvdStDelayCnt++;
            return 0;
        } else {
            if (*(unsigned char *)(ringBuf + ringRead) == 0xFF) {
                return 1;
            }
            top = ringRead;
            _setEntryOffsets();
            _advanceRing(headerSize);
            if (_checkRing(headerSize) && *(unsigned char *)(ringBuf + ringRead) == 0) {
                _setNextEntryOffsets(ringRead);
            }
            frameTime -= 2997;
            frameCount++;
            if (debug_font_flag3 != 0 || (debug_font_flag & 1)) {
                debug_Printf(500, ScreenHeight / 2 - 48, 0xCCCCCC00, "S:%d", frameCount);
            }
            if (*(unsigned char *)(ringBuf + top) == 1) {
                if (frameTime == 2997) {
                    /* the motion divided evenly and the cut switched over */
                    debug_StdPrintfDummy(
                        "\033[33mキリ良くモーションが割り切れてカットが切り替わり\033[m\n");
                } else {
                    /* the next cut's data arrived, so a forced switch (remainder: %f) */
                    debug_StdPrintfDummy(
                        "\033[33m次のカットデータ来たので強制切り替わり(余り：%f)\033[m\n",
                        (float)frameTime / 2997.0f);
                }
                break;
            }
        }
    }
    if (*(unsigned char *)(ringBuf + top) == 1) {
        debug_StdPrintfDummy("\033[33mFIND HEADER FLAG\033[m\n");
        if (bgaStreamSync != 0) {
            debug_StdPrintfDummy("\033[36mSTREAM MOTION SYNCHRONIZE OK(%d)\033[m\n", frame_count);
        } else {
            debug_StdPrintfDummy("\033[33mSTREAM MOTION SYNCHRONIZE NG(%d)\033[m\n");
        }
    }
    if (bgaStreamSync != 0) {
        debug_StdPrintfDummy("\033[33mCLEAR FRAME MOD\033[m\n");
        frameCount = 0;
        frameTime = 0;
        bgaStreamSync = 0;
    }
    framePlayed = frameTime;
    frameTime += systemStatus[0] == 0 ? 2997 : 3619;
    return 0;
}

void PlayStreamMotion(void)
{
    if (bgMgrId == 0) {
        /* StandbyStreamMotion has not been called; nothing was played */
        debug_StdPrintfDummy("StandbyStreamMotionが呼ばれてません。再生はされませんでした。\n");
        return;
    }
    streamState = 1;
}

/* reset the actor's stream-motion state; the offset trace at the end is
 * switched off */
void ClearStreamMotionEntry(GObj *gobj)
{
    GOBJ_SUB(gobj)->ctrl.stream = -1;
    GOBJ_SUB(gobj)->streamScale = 1;
    CopyVector(GOBJ_SUB(gobj)->streamOfs, ZeroVector);
    GOBJ_SUB(gobj)->ctrl.catchBoy = 1;
    if (0) {
        float v[4];

        CopyVector(v, GOBJ_SUB(gobj)->streamOfs);
        debug_StdPrintfDummy("ADJUST %08x(%f)\n", gobj, v[0]);
    }
}

/* the cleared entry every slot is reset to */
static SMotion emptyEntry = {0, 0, -1, -1, -1, {0}, 0}; /* derived name */

static void _deleteStreamMotionManager(void)
{
    int i;

    if (streamNum != 0) {
        for (i = 0; i < streamNum; i++) {
            ClearStreamMotionEntry(streamEntry[i].obj.p);
            if (streamEntry[i].finishFunc != 0) {
                streamEntry[i].finishFunc(streamEntry[i].obj.p);
            }
        }
        streamNum = 0;
    }
    for (i = 0; i < 10; i++) {
        streamEntry[i] = emptyEntry;
    }
    streamState = 0;
    readState = 0;
    ringRead = 0;
    ringWrite = 0;
    streamIdle = 1;
    headerRead = 0;
    headerSize = 0;
    ringUsed = 0;
    framePlayed = 0;
    frameTime = 0;
    if (bgMgrId != 0) {
        bgMgrId = 0;
    }
    debug_StdPrintfDummy("delete stream motion manager\n");
}

void DisableStreamMotionManagerAutomaticDelete(void)
{
    streamIdle = 0;
    debug_StdPrintfDummy("disable automatic delete\n");
}

inline int GetDataSizeOfStreamMotion(int no)
{
    if (streamEntry[no].cur < 0) {
        /* tried to get the work size before the data has arrived */
        debug_StdPrintfDummy("データがまだ来ていないのにワークサイズの取得をしようとしました\n");
        return 4;
    }
    return streamEntry[no].size;
}

static void getStreamMotionData(char *dst, int off, int no)
{
    int size = streamEntry[no].size;
    int over = off + size - 163840;

    if (over > 0) {
        int first = 163840 - off;

        memcpy(dst, ringBuf + off, first);
        memcpy(dst + first, ringBuf, over);
        return;
    }
    memcpy(dst, ringBuf + off, size);
}

static void getStreamMotionBlendData(char *dst, int no)
{
    int size = streamEntry[no].size;
    char a[size];
    char b[size];
    int i;

    getStreamMotionData(a, streamEntry[no].cur, no);
    getStreamMotionData(b, streamEntry[no].next, no);
    for (i = 0; i < 4; i++) {
        dst[i] = a[i];
    }
}

void GetStreamMotionDataNext(char *dst, int no)
{
    getStreamMotionData(dst, streamEntry[no].next, no);
}

typedef struct { /* field names derived */
    char c[4];
} StreamMotionHead; /* derived name */

/* the four bytes GetStreamMotionData hands back while no data has arrived */
static const char streamDummyHead[] = {0, 0xFF, 0, 0}; /* derived name */

inline float GetStreamMotionData(char *dst, int no)
{
    if (streamEntry[no].cur < 0) {
        *(StreamMotionHead *)dst = *(const StreamMotionHead *)streamDummyHead;
        /* "tried to get the stream motion before the data has arrived" */
        debug_StdPrintfDummy(
            "データがまだ来ていないのにストリームモーションの取得をしようとしました\n");
        return -1.0f;
    }
    getStreamMotionData(dst, streamEntry[no].cur, no);
    return (float)framePlayed / 2997.0f;
}

static void _transRingBuf(int *idx_p, char *dst, int size, char *src, int amt)
{
    int old_idx = *idx_p;
    int new_idx = old_idx + amt;
    *idx_p = new_idx;
    if (new_idx >= size) {
        int overflow = new_idx - size;
        int first_chunk = amt - overflow;
        *idx_p = overflow;
        memcpy(dst + old_idx, src, first_chunk);
        memcpy(dst, src + first_chunk, *idx_p);
        return;
    }
    memcpy(dst + old_idx, src, amt);
}

void ExecStreamMotionManager(void)
{
    int i;
    int pct;

    switch (streamState) {
    case 0:
        break;
    case 1:
        if (_infoUpdate() != 0) {
            /* "detected the end of the stream motion" */
            debug_StdPrintfDummy("ストリームモーションの終了を検知\n");
            streamState = 0;
            if (streamIdle != 0) {
                if (bgMgrId != 0) {
                    iosCdvdBackGroundMgrDelete(bgMgrId);
                } else {
                    _deleteStreamMotionManager();
                }
                if (streamNum != 0) {
                    for (i = 0; i < streamNum; i++) {
                        ClearStreamMotionEntry(streamEntry[i].obj.p);
                        if (streamEntry[i].finishFunc != 0) {
                            streamEntry[i].finishFunc(streamEntry[i].obj.p);
                        }
                    }
                    streamNum = 0;
                }
            }
        }
        break;
    }
    if (bgMgrId != 0) {
        unsigned int wp = ringWrite;
        unsigned int rp = ringRead;

        if (wp < rp) {
            pct = wp + 163840 - rp;
        } else {
            pct = wp - rp;
        }
        pct = pct * 100 / 163840;
        if (debug_font_flag & 1) {
            debug_Printf(0, ScreenHeight / 2 - 16, 0xFF404000, " %d%%", pct);
        }
        if (debug_font_flag & 1) {
            debug_Printf(58, ScreenHeight / 2 - 16, 0x40FF4000, "STANDBY %d CHARS %s", streamNum,
                         streamOwner);
        }
    }
}

void MallocStreamMotionBuffer(void)
{
    ringBuf = iosMallocDebug(ios_partition_sugipon, 163840, "src/streamMotionManager.c", 602);
    readBufRaw = iosMallocDebug(ios_partition_sugipon, 163904, "src/streamMotionManager.c", 604);
#ifdef ICO_HOST
    readBuf = (char *)((ICO_WORD)(readBufRaw + 63) & ~(ICO_WORD)63);
#else
    readBuf = (char *)((int)(readBufRaw + 63) & 0xFFFFFFC0);
#endif
    if (ringBuf == 0 || readBuf == 0) {
        /* "could not allocate the stream buffer memory" */
        debug_StdPrintfDummy("ストリーム用のバッファメモリが確保できませんでした\n");
    }
}

inline void ClearAllStreamMotionEntry(void)
{
    int i;

    if (streamNum == 0) {
        return;
    }
    for (i = 0; i < streamNum; i++) {
        ClearStreamMotionEntry(streamEntry[i].obj.p);
        if (streamEntry[i].finishFunc != 0) {
            streamEntry[i].finishFunc(streamEntry[i].obj.p);
        }
    }
    streamNum = 0;
}

inline void DeleteStreamMotionManager(void)
{
    if (bgMgrId != 0) {
        iosCdvdBackGroundMgrDelete(bgMgrId);
    } else {
        _deleteStreamMotionManager();
    }
    ClearAllStreamMotionEntry();
}

inline void StandbyStreamMotion(char *file)
{
    DeleteStreamMotionManager();
    while (bgMgrId != 0) {
        debug_StdPrintfDummy("\033[36mWait!!!\033[m\n");
        iosThreadSleep();
    }
    bgMgrId = iosCdvdBackGroundMgrAdd(file, _handler, 0, 0, 0, 0, _closeHander, 0);
    streamOwner = file;
}

inline void StopStreamMotion(void)
{
    streamState = 0;
}

inline int EntryStreamMotion(GObj *gobj)
{
    int no = streamNum;

    streamEntry[no].obj.p = gobj;

    GOBJ_SUB(gobj)->ctrl.stream = no;
    GOBJ_SUB(gobj)->ctrl.loopFlag = 0;
    GOBJ_SUB(gobj)->ctrl.posReserve = 0;
    GOBJ_SUB(gobj)->ctrl.catchBoy = 0;
    streamNum = no + 1;
    return no;
}

inline void InitStreamMotionManager(void)
{
    readBufRaw = 0;
    ringBuf = 0;
    readBuf = 0;
}

inline int CheckReadyStreamMotion(void)
{
    unsigned int p = ringRead;
    unsigned int q = ringWrite;
    unsigned int end = p + 4096;
    int r;
    if (q < p)
        q += 163840;
    r = 1;
    if (!(q < p) && (int)q < (int)end)
        r = 0;
    return r;
}

inline void SetStreamMotionFinishCallBackFunc(int no, void (*func)(GObj *))
{
    streamEntry[no].finishFunc = func;
}

inline void FreeStreamMotionBuffer(void)
{
    if (ringBuf != 0) {
        iosFree(ringBuf);
        iosFree(readBufRaw);
        readBufRaw = 0;
        ringBuf = 0;
        readBuf = 0;
    }
}

static inline int _closeHander(void)
{
    _deleteStreamMotionManager();
    return 1;
}

static inline int _handler(CdvdBgReq *self)
{
    unsigned int wp = ringWrite;
    unsigned int rp = ringRead;
    int rest;
    int size;

    if (wp < rp) {
        rest = wp + 163840 - rp;
    } else {
        rest = wp - rp;
    }
    switch (readState) {
    default:
    case 0:
        if (rest > 81919) {
            break;
        }
        readState = 1;
    case 1:
        size = 163840 - rest;
        size = ((size - 1) / 2048) * 2048;
        iosCdvdBackGroundRead(self, readBuf, size);
        _transRingBuf(&ringWrite, ringBuf, 163840, readBuf, size);
        readState = 0;
        break;
    }
    return 0;
}
