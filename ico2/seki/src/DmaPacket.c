#include "typedef.h"
#include "DmaPacket.h"
#include "ios.h"
#include "memory.h"

/* the double-buffered packet area every packet builder writes into */
DpkCtl PacketBufferStruct = {0};

/* the DMA memory use debug's meter draws */
int used_dma_memory = 0;

void dpk_Init(void)
{
    PacketBufferStruct.cur = 0;
    PacketBufferStruct.buf[0] = (int *)ICO_UNCACHED_ACCEL(
        ICO_ADDR(iosMallocDebug(ios_partition_common, 524288, "src/DmaPacket.c", 134)));
    PacketBufferStruct.buf[1] = (int *)ICO_UNCACHED_ACCEL(
        ICO_ADDR(iosMallocDebug(ios_partition_common, 524288, "src/DmaPacket.c", 135)));
    PacketBufferStruct.ptr.i = PacketBufferStruct.buf[PacketBufferStruct.cur];
}

void dpk_SwapBuffer(void)
{
    int i = PacketBufferStruct.cur ^ 1;
    PacketBufferStruct.cur = i;
    PacketBufferStruct.ptr.i = PacketBufferStruct.buf[i];
    PacketBufferStruct.tail.c = 0;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.end.c = 0;
}

unsigned int dpk_CheckBufferSize(void)
{
    int idx = PacketBufferStruct.cur;
    int adj_cur = (int)PacketBufferStruct.ptr.c - 0x80000;
    int end_off = (int)PacketBufferStruct.buf[idx];
    return (end_off - adj_cur) >> 4;
}
