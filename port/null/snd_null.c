/*
 * port/null/snd_null.c
 *
 * A silent sound driver, in two halves.
 *
 * The IOP half stands in for SNDN2DRV.IRX's RPC server (docs/research/
 * sndn2drv.md, "Transport" and "Mailbox").  Registered with the host SIF
 * under server id 0x736E646E, it accepts the init call (0x65, one packet)
 * and the tick call (0x64, a page of 16-byte packets) and answers with the
 * 0x200-byte reply page: all zero (every voice's envelope 0, every stream's
 * IOP read offset 0) except +0x1C0, the transfer counter, which echoes the
 * counter of the last sample upload (0x20) or download (0x21) packet seen,
 * as the IRX's DMA-done callback does, so a sequencer waiting on
 * SgGetDmaTransferStatus sees its transfers finish.  Two pages alternate
 * with a page counter, as in the IRX.
 *
 * The EE half is the Sg API the game calls.  sce/libsndn2/sound.c is not in
 * the host build (its uncached-alias pointers need the Phase 4 port), so
 * these are its entry points with nothing behind them: requests are
 * accepted, nothing plays, nothing is ever reported as sounding.
 * _SgSndn2Remote, SgSndn2RemoteInit and SgSndn2RemoteSync are real: they
 * bind and call the IOP half through the host SIF, and SgCalledTickProc
 * sends one (empty) tick per call.
 *
 * Phase 4 deletes the EE half (the sequencer moves to port/audio/sg/) and
 * replaces the IOP half with sndn2_host.c.
 */
#include "null_devices.h"

#include "sif_host.h"

#include <sifrpc.h>
#include <sound.h>

#include <stdint.h>
#include <string.h>

/* --- the IOP half ---------------------------------------------------------- */

#define SND_RPC_TICK 0x64
#define SND_RPC_INIT 0x65
#define SND_PKT_DMA_WRITE 0x20
#define SND_PKT_DMA_READ 0x21
#define SND_REPLY_COUNTER 0x1C0

static unsigned char replyPages[2][ICO_SND_REPLY_SIZE];
static unsigned int pageCounter;
static uint32_t transferCounter;
static int32_t initReturn; /* the IRX's return word of a non-tick call */
static const unsigned char *lastReply;

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void run_packets(const unsigned char *pk, int count)
{
    int i;

    for (i = 0; i < count; i++, pk += 16) {
        uint32_t cmd = rd32(pk);

        if (cmd == SND_PKT_DMA_WRITE || cmd == SND_PKT_DMA_READ) {
            /* id word: the EE's transfer counter << 8 | IOP address bits */
            transferCounter = rd32(pk + 4) >> 8;
        }
    }
}

static void *snd_server(unsigned int rpc_number, void *send, int ssize, int rsize)
{
    unsigned char *page;
    int p;

    (void)rsize;
    if (rpc_number == SND_RPC_INIT) {
        if (send != NULL && ssize >= 16) {
            run_packets(send, 1);
        }
        initReturn = 0;
        return &initReturn;
    }
    if (send != NULL && ssize > 0) {
        run_packets(send, ssize / 16);
    }
    pageCounter++;
    page = replyPages[pageCounter & 1];
    memset(page, 0, ICO_SND_REPLY_SIZE);
    /* the DMA-done callback writes the counter into both pages */
    for (p = 0; p < 2; p++) {
        unsigned char *c = &replyPages[p][SND_REPLY_COUNTER];

        c[0] = (unsigned char)transferCounter;
        c[1] = (unsigned char)(transferCounter >> 8);
        c[2] = (unsigned char)(transferCounter >> 16);
        c[3] = (unsigned char)(transferCounter >> 24);
    }
    lastReply = page;
    return page;
}

void ico_snd_null_register(void)
{
    ico_sif_register_server(ICO_SND_SERVER_ID, snd_server);
}

const unsigned char *ico_snd_null_last_reply(void)
{
    return lastReply;
}

/* --- the EE half: the RPC client ------------------------------------------- */

static sceSifRpcClientData sgClient;
static unsigned char sgIop2EeBuf[ICO_SND_REPLY_SIZE] __attribute__((aligned(64)));
static int sgBound;

int _SgSndn2Remote(int rpc_number, int mode, void *sendbuf, void *recvbuf, int ssize, int rsize)
{
    return sceSifCallRpc(&sgClient, (unsigned int)rpc_number, (unsigned int)mode, sendbuf, ssize,
                         recvbuf, rsize, 0, 0);
}

int SgSndn2RemoteInit(void)
{
    ico_snd_null_register();
    sceSifInitRpc(0);
    do {
        if (sceSifBindRpc(&sgClient, ICO_SND_SERVER_ID, 0) < 0) {
            return -1;
        }
    } while (sgClient.serve == 0);
    sgBound = 1;
    return 0;
}

int SgSndn2RemoteSync(void)
{
    return sgBound ? sceSifCheckStatRpc(&sgClient) : 0;
}

/* the init packet sound.c's _SgInit sends: {0x1E, hot} */
static void sg_init(int hot)
{
    int buf[16];

    memset(buf, 0, sizeof(buf));
    buf[0] = 0x1E;
    buf[1] = hot;
    if (sgBound) {
        _SgSndn2Remote(SND_RPC_INIT, 0, buf, buf, 0x40, 0x40);
    }
    memset(sgIop2EeBuf, 0, sizeof(sgIop2EeBuf));
}

void SgInit(void)
{
    sg_init(0);
}

void SgInitHot(void)
{
    sg_init(1);
}

void SgQuit(void) {}

/* One driver tick with no packets; the reply lands in sgIop2EeBuf. */
void SgCalledTickProc(void)
{
    if (sgBound) {
        _SgSndn2Remote(SND_RPC_TICK, 1, sgIop2EeBuf, sgIop2EeBuf, 0, ICO_SND_REPLY_SIZE);
    }
}

/* --- the EE half: the silent API ------------------------------------------- */

void SgSetReverbType(int core, int type)
{
    (void)core;
    (void)type;
}

void SgSetReverbDepth(int core, int left, int right)
{
    (void)core;
    (void)left;
    (void)right;
}

void SgSetReverbDelaytime(int core, int time)
{
    (void)core;
    (void)time;
}

void SgSetReverbFeedback(int core, int feedback)
{
    (void)core;
    (void)feedback;
}

void SgSetReverbEndAddr(int core, int addr)
{
    (void)core;
    (void)addr;
}

void SgSetDigitalOutputMode(int mode)
{
    (void)mode;
}

void SgSetOutputMode(int mode)
{
    (void)mode;
}

void SgSetTickMode(int tick)
{
    (void)tick;
}

void SgSetMasterVol(int core, int left, int right)
{
    (void)core;
    (void)left;
    (void)right;
}

/* Sample uploads complete at once: there is no SPU RAM to fill. */
int SgDmaWrite(unsigned int iop, unsigned int spu, unsigned int size)
{
    (void)iop;
    (void)spu;
    (void)size;
    return 0;
}

int SgDmaRead(unsigned int spu, unsigned int iop, unsigned int size)
{
    (void)spu;
    (void)iop;
    (void)size;
    return 0;
}

/* mode 0 polls, mode 1 waits; every transfer is already done */
int SgGetDmaTransferStatus(int mode)
{
    return (mode == 0 || mode == 1) ? 1 : -1;
}

int SgVabOpen(int iop, int *hd, int spu)
{
    (void)iop;
    (void)hd;
    (void)spu;
    return 0;
}

int SgVabOpenFakeBody(int *hd, int spu)
{
    (void)hd;
    (void)spu;
    return 0;
}

int SgVabClose(int vab)
{
    (void)vab;
    return 0;
}

int SgBgmOpen(int vab, void *sq)
{
    (void)vab;
    (void)sq;
    return 0;
}

int SgBgmClose(int id)
{
    (void)id;
    return 0;
}

void SgBgmPlay(unsigned int id)
{
    (void)id;
}

void SgBgmStop(unsigned int id, int mode)
{
    (void)id;
    (void)mode;
}

void SgSetBgmTempo(unsigned int id, int tempo)
{
    (void)id;
    (void)tempo;
}

int SgGetBgmTempo(unsigned int id)
{
    (void)id;
    return 0;
}

int SgGetBgmStatus(int id)
{
    (void)id;
    return 0;
}

int SgGetBgmChStatus(unsigned int id, int channel, int kind)
{
    (void)id;
    (void)channel;
    (void)kind;
    return 0;
}

int SgSetBgmPanpot(unsigned int id, int pan)
{
    (void)id;
    (void)pan;
    return 0;
}

int SgSetBgmVol(unsigned int id, int vol, int mask)
{
    (void)id;
    (void)vol;
    (void)mask;
    return 0;
}

/* nothing sounds: every slot and sequence reads as stopped */
int SgGetSlotStatus(int kind, int slot)
{
    (void)kind;
    (void)slot;
    return 0;
}

int SgSetSeMasterVol(int vab, int vol)
{
    (void)vab;
    (void)vol;
    return 0;
}

/* accepted with handle 0; SgGetSlotStatus then reports it stopped */
int SgSePlay(int vabflags, int prog, int tone)
{
    (void)vabflags;
    (void)prog;
    (void)tone;
    return 0;
}

void SgSeStop(int id)
{
    (void)id;
}

void SgSeStopAll(int immediate)
{
    (void)immediate;
}

void SgSetSeVolDirect(unsigned int id, int left, int right)
{
    (void)id;
    (void)left;
    (void)right;
}

void SgSetSePitchDirect(unsigned int id, int pitch)
{
    (void)id;
    (void)pitch;
}

/* the 48 voices, handed out in turn */
int SgGetSpuSlotMalloc(int mode)
{
    static int next;

    (void)mode;
    next = (next + 1) % 48;
    return 47 - next;
}

int SgSetSpuSlotFree(unsigned int slot)
{
    (void)slot;
    return 0;
}

void SgStAdpcmInit(void) {}

void SgStAdpcmQuit(void) {}

int SgStAdpcmOpen(void *req)
{
    (void)req;
    return 0;
}

int SgStAdpcmClose(unsigned int ch)
{
    (void)ch;
    return 0;
}

int SgStAdpcmChannelVolume(unsigned long long mask, unsigned int left, int right)
{
    (void)mask;
    (void)left;
    (void)right;
    return 0;
}

int SgStAdpcmChannelPitch(unsigned long long mask, int pitch)
{
    (void)mask;
    (void)pitch;
    return 0;
}

int SgStAdpcmPlay(unsigned long long mask)
{
    (void)mask;
    return 0;
}

int SgStAdpcmStop(unsigned long long mask)
{
    (void)mask;
    return 0;
}

/* the stream never advances: the IOP read offset stays 0 */
int SgStAdpcmIopReadAddr(int ch)
{
    (void)ch;
    return 0;
}

void SgStPcmInit(void) {}

void SgStPcmQuit(void) {}

int SgStPcmOpen(int *req)
{
    (void)req;
    return 0;
}

int SgStPcmClose(unsigned int ch)
{
    (void)ch;
    return 0;
}

void SgStPcmSetEffect(int effect)
{
    (void)effect;
}

int SgStPcmPlay(unsigned long long mask)
{
    (void)mask;
    return 0;
}

int SgStPcmStop(unsigned long long mask)
{
    (void)mask;
    return 0;
}

int SgStPcmLseek(unsigned int ch, unsigned int offset)
{
    (void)ch;
    (void)offset;
    return 0;
}

void SgStPcmVolume(unsigned long long mask, unsigned int left, int right)
{
    (void)mask;
    (void)left;
    (void)right;
}

int SgStPcmIopReadAddr(unsigned int ch)
{
    (void)ch;
    return 0;
}

int SgStPcmBufMode(int mode, long long mask, int addr)
{
    (void)mode;
    (void)mask;
    (void)addr;
    return 0;
}
