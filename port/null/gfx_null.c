/*
 * port/null/gfx_null.c
 *
 * The graphics floor under the game's renderer layer, in both builds (the
 * window build draws through rd above it).  The build compiles the game's
 * own renderer layer (seki/src: GsBase, GifPacket, DisplayList, DmaPacket,
 * Texture, Packet, RegistPacket, ...; the sugipon and ito effect files): it
 * builds its packets and display lists in its own buffers exactly as on the
 * PS2, so the state the draw phase changes (GsBase's fades and view
 * matrices, the R register lightning reseeds, the game-over ring's mail,
 * heap allocations made by the packet builders) is the same as with a
 * renderer.  What is stubbed is the hardware underneath, here: Sony's
 * libgraph and libdma entry points (the FMV player is port/fmv/movie.c).
 * Nothing is sent anywhere: a DMA kick is a no-op, the GS is always idle.
 * (MicroCode.c's VU1 microprogram table is empty on the host: ico2/vusrc is
 * assembled only by the PS2 build.)
 */
#include <eeregs.h>
#include <libdma.h>
#include <libgraph.h>
#include <string.h>
#include "sched.h"

/* --- libgraph ---------------------------------------------------------------- */

/* sceGsSyncV keeps the EE's busy wait for the next vsync (libgraph's polls
   through VSync, sce/libgraph/graph011.c) and returns the field the host set
   in GS_CSR. */
int sceGsSyncV(int mode)
{
    (void)mode;
    ico_sched_note("sceGsSyncV", 0, __builtin_return_address(0));
    ico_sched_spin_vsync();
    return (int)((*GS_CSR >> 13) & 1);
}

/* 0: every path has drained (GsBase.c's gsb_Init and gsb_SyncGSSystem wait
   for this; nonzero would make gsb_SyncGSSystem skip the frame). */
int sceGsSyncPath(int mode, unsigned short timeout)
{
    (void)mode;
    (void)timeout;
    return 0;
}

void sceGsResetGraph(short mode, short inter, short omode, short ffmd)
{
    (void)mode;
    (void)inter;
    (void)omode;
    (void)ffmd;
}

void sceGsResetPath(void) {}

/* The double-buffer and display records are register images for the GS;
   nothing on the host reads them, and GsBase.c only patches fields in them
   (gsb_SetFrame). */
void sceGsSetDefDBuff(sceGsDBuff *db, short psm, short w, short h, short ztst, short zpsm,
                      short flag)
{
    (void)db;
    (void)psm;
    (void)w;
    (void)h;
    (void)ztst;
    (void)zpsm;
    (void)flag;
}

void sceGsSetDefDispEnv(sceGsDispEnv *disp, short psm, short w, short h, short dx, short dy)
{
    (void)disp;
    (void)psm;
    (void)w;
    (void)h;
    (void)dx;
    (void)dy;
}

void sceGsSetHalfOffset(void *draw, short x, short y, short half)
{
    (void)draw;
    (void)x;
    (void)y;
    (void)half;
}

/* The flip; GsBase.c ignores the result. */
int sceGsSwapDBuff(void *db, int id)
{
    (void)db;
    (void)id;
    return 0;
}

/* --- libdma ------------------------------------------------------------------ */

/* The channel register blocks: seki/src/Basic.c's dma_init sets CHCR.TIE
   (|= 0x40) in three of them, so they are writable memory. */
#define DMA_CHANNELS 10
static DmaChan dmaChannels[DMA_CHANNELS];

DmaChan *sceDmaGetChan(unsigned int id)
{
    return id < DMA_CHANNELS ? &dmaChannels[id] : NULL;
}

int sceDmaReset(int mode)
{
    (void)mode;
    memset(dmaChannels, 0, sizeof dmaChannels);
    return 0;
}

/* DisplayList.c's dl_Swap sends the frame's chain, DisplayP2O.c the VU1
   microprograms: nothing consumes them. */
void sceDmaSend(DmaChan *ch, void *addr)
{
    (void)ch;
    (void)addr;
}
