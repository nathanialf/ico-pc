#include "mv_defs.h"
#include "mv_readbuf.h"
#include "memory.h"

static void Free(int addr);

/* the ring readBufCreate allocated */
static void free_buffer(ReadBuf *self)
{
    Free((int)self->data);
}

int readBufCreate(ReadBuf *self)
{
    int buf;

    buf = alloc_zeroed(327680, 64);
    self->data = (unsigned char *)buf;
    if (buf == 0) {
        return -1;
    }
    self->size = 327680;
    self->put = self->count = 0;
    return 0;
}

void readBufDelete(ReadBuf *self) {}

/* this file's own Free (mv_defs.h) */
static void Free(int addr)
{
    iosFree((void *)phys_addr(addr));
}

int readBufBeginPut(ReadBuf *self, void **p)
{
    int diff = self->size - self->count;
    if (diff != 0) {
        int pos = self->put;
        *p = self->data + pos;
    }
    return diff;
}

void readBufEndPut(ReadBuf *self, int n)
{
    int size;
    int pos;
    int cum;
    int remaining;
    int step;
    size = self->size;
    cum = self->count;
    pos = self->put;
    remaining = size - cum;
    step = (n < remaining) ? (n) : (remaining);
    pos += step;
    cum = cum + step;
    self->put = pos % size;
    self->count = cum;
}

int readBufBeginGet(ReadBuf *self, void **p)
{
    int n = self->count;
    if (n != 0) {
        int v1 = self->put - n;
        int divisor = self->size;
        v1 = v1 + divisor;
        *p = self->data + (v1 % divisor);
    }
    return n;
}

int readBufEndGet(ReadBuf *self, int n)
{
    int rest = self->count;
    if (n < rest) {
        rest = n;
    }
    self->count -= rest;
    return rest;
}
