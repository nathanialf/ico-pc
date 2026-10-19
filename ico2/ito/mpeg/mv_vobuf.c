#include "mv_defs.h"
#include "typedef.h"
#include "memory.h"
#include <eekernel.h>
#include "mv_vobuf.h"

static void Free(int addr);

/* this file's own free_buf; mv_videodec.c and mv_vibuf.c have theirs */
static void free_buf(VoBuf *self)
{
    Free((int)self->data);
    Free((int)self->tag);
}

int voBufCreate(VoBuf *self)
{
    int data;
    int tag;
    int i;

    data = alloc_zeroed(PAL_JPNKOR(8294400, 6912000), 64);
    if (data == 0) {
        return -1;
    }
    self->data = (VoData *)uncached_accel_addr(data);
    tag = alloc_zeroed(PAL_JPNKOR(3936320, 3289920), 64);
    self->tag = (VoTag *)tag;
    if (tag == 0) {
        return -1;
    }
    self->max = 5;
    self->count = 0;
    self->idx = 0;
    for (i = 0; i < self->max; i++) {
        self->tag[i].status = 0;
    }
    return 0;
}

void voBufDelete(VoBuf *self)
{
    free_buf(self);
}

/* this file's own Free (mv_defs.h) */
static void Free(int addr)
{
    /* the buffers are kept as addresses (alloc_zeroed), some at their
       uncached-accelerated alias; the heap takes the block back with the
       segment bits off */
    iosFree((void *)phys_addr(addr));
}

void voBufReset(VoBuf *self)
{
    self->count = 0;
    self->idx = 0;
}

/* Whether all of the ring's slots are in use.  isFull below is
 * the same test, for voBufGetData. */
int voBufIsFull(VoBuf *self)
{
    return self->count == self->max;
}

static inline int isFull(VoBuf *self) /* derived name */
{
    return self->count == self->max;
}

void voBufIncCount(VoBuf *self)
{
    DIntr();
    self->tag[self->idx].status = 2;
    self->count++;
    self->idx = (self->idx + 1) % self->max;
    SYNC();
    EI();
}

VoData *voBufGetData(VoBuf *self)
{
    return !isFull(self) ? &self->data[self->idx] : 0;
}

static __inline__ int voBufIsEmpty(VoBuf *self) /* derived name */
{
    return self->count == 0;
}

VoTag *voBufGetTag(VoBuf *self)
{
    return !voBufIsEmpty(self) ? &self->tag[(self->idx - self->count + self->max) % self->max] : 0;
}

void voBufDecCount(VoBuf *self)
{
    if (self->count > 0) {
        --self->count;
    }
}
