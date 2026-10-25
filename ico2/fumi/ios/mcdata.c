#include "region.h" /* PC port: the demo's arms test the disc */
#include "cdvd.h"
#include "mcdata.h"
#include "thread.h"
#include "mcard.h"
#include "memory.h"
#include "ios.h"

struct McIconWork { /* field names derived */
    int remain;
    int size;
    void *buf;
}; /* derived name */

/* the flag the mcard thread raises when the request the caller is spinning
   on has finished */
static int mcDataDone; /* derived name */

static inline int _iosMcIconWriteIconsys_jun26(CdvdBgReq *self, struct McIconWork *p)
{
    iosCdvdBackGroundRead(self, p->buf, p->size);
    return 1;
}

/* Background-read callback: pulls the icon file off the disc a chunk at a
   time into a 64-byte aligned buffer and waits for the writer to drain it. */
static inline int _iosMcIconWriteIconsys(CdvdBgReq *self, struct McIconWork *p)
{
    if (ICO_JUN26) {
        return _iosMcIconWriteIconsys_jun26(self, p);
    }
    char buf[51200 + 64];
    char *ptr;
    int size;
    int loop = 1;

    /* the buffer is on the stack, above 4 GB on a 64-bit host: no int */
    ptr = (char *)(((__UINTPTR_TYPE__)buf + 63) & ~(__UINTPTR_TYPE__)63);
    p->buf = ptr;

    do {
        size = p->remain > 51200 ? 51200 : p->remain;
        iosCdvdBackGroundRead(self, ptr, size);
        p->size = size;
        p->remain -= size;
        if (p->remain <= 0)
            loop = 0;
        IosCdvdMgrSleep = 1;
        mcDataDone = 0;
        do {
            iosThreadSleep();
        } while (mcDataDone == 0);
        IosCdvdMgrSleep = 0;
    } while (loop);

    return 1;
}

static inline int iosMcIconWriteIconsys_jun26(struct McMgr *self, const IconFile *p)
{
    struct McIconWork work;
    CdvdBgReq *hdl;

    work.size = (p->size + 2047) / 2048 * 2048;
    work.buf = iosMallocDebug(ios_partition_sound, work.size, __FILE__, 115);

    hdl = iosCdvdBackGroundMgrAdd(p->name, _iosMcIconWriteIconsys, &work, 0, 0, 0, 0, 0);

    while (hdl->readFunc != 0) {
        iosMcMgrSync(self);
    }
    iosCdvdBackGroundMgrDelete(hdl);
    iosMcHandlerWrite(self, work.buf, p->size);
    iosFree(work.buf);
    return 0;
}

inline int iosMcIconWriteIconsys(struct McMgr *self, const IconFile *p)
{
    if (ICO_JUN26) {
        return iosMcIconWriteIconsys_jun26(self, p);
    }
    struct McIconWork work;
    CdvdBgReq *hdl;
    int total = 0;
    int size;
    int len;

    work.remain = (p->size + 2047) / 2048 * 2048;

    hdl = iosCdvdBackGroundMgrAdd(p->name, _iosMcIconWriteIconsys, &work, 0, 0, 0, 0, 0);

    while (work.remain > 0) {
        work.size = 0;
        do {
            iosMcMgrSync(self);
        } while (work.size == 0);
        size = work.size;
        total += size;
        if (p->size < total) {
            len = size - (total - p->size);
        } else {
            len = size;
        }
        iosMcHandlerWrite(self, work.buf, len);
        mcDataDone = 1;
    }
    iosCdvdBackGroundMgrDelete(hdl);
    return 0;
}

inline int iosMcIconWriteIcon(struct McMgr *self, const IconFile *p)
{
    return iosMcIconWriteIconsys(self, p);
}
