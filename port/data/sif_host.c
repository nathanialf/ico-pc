/*
 * port/data/sif_host.c
 *
 * sifrpc and sifdev on the host (sif_host.h).  What the game asks of the IOP
 * at boot (seki/src/FileManager.c: the IOPRP image reboot, the six IRX
 * modules) has nothing to do here, so those calls report success.  The IOP
 * heap and SIF DMA act on ico_iop_ram.  RPC calls run the registered host
 * server synchronously, which is complete by the time sceSifCallRpc returns:
 * sceSifCheckStatRpc always reports the call finished.
 *
 * The sifdev file calls (sceOpen and the rest) are sifdev_host.c's since
 * renderer wave 6 (R6a): host0: paths map to <pref>/dev/ for developer
 * mode.
 */
#include "sif_host.h"
#include "iop_ram.h"
#include <sifdev.h>
#include <sifrpc.h>
#include <stdint.h>
#include <string.h>

#define MAX_SERVERS 8

static struct {
    unsigned int sid;
    IcoSifServerFn fn;
} servers[MAX_SERVERS];

static int serverCount;

/* the last RPC, for the diagnostics heartbeat */
static unsigned int lastSid;

static unsigned int lastRpc;

static unsigned int rpcCount;

void ico_sif_host_last_rpc(unsigned int *sid, unsigned int *rpc, unsigned int *count)
{
    *sid = lastSid;
    *rpc = rpcCount != 0 ? lastRpc : 0;
    *count = rpcCount;
}

static int find_server(unsigned int sid)
{
    int i;

    for (i = 0; i < serverCount; i++) {
        if (servers[i].sid == sid) {
            return i;
        }
    }
    return -1;
}

int ico_sif_register_server(unsigned int sid, IcoSifServerFn fn)
{
    int i = find_server(sid);

    if (i >= 0) {
        servers[i].fn = fn;
        return 0;
    }
    if (serverCount >= MAX_SERVERS) {
        return -1;
    }
    servers[serverCount].sid = sid;
    servers[serverCount].fn = fn;
    serverCount++;
    return 0;
}

void ico_sif_host_reset(void)
{
    memset(servers, 0, sizeof(servers));
    serverCount = 0;
    ico_iop_heap_reset();
}

/* --- boot: the IOP reboot and the modules --------------------------------- */

void sceSifInitRpc(int mode)
{
    (void)mode;
}

void sceSifExitRpc(void) {}

int sceSifRebootIop(const char *img)
{
    (void)img;
    return 1;
}

int sceSifResetIop(char *arg, int mode)
{
    (void)arg;
    (void)mode;
    return 1;
}

int sceSifSyncIop(void)
{
    return 1;
}

int sceSifLoadFileReset(void)
{
    return 0;
}

int sceSifLoadModule(void *name, int arglen, int args)
{
    static int moduleId;

    (void)name;
    (void)arglen;
    (void)args;
    return ++moduleId; /* a module id: any value >= 0 is success */
}

/* --- the IOP heap and SIF DMA ---------------------------------------------- */

int sceSifInitIopHeap(void)
{
    return 0;
}

int sceSifAllocIopHeap(int size)
{
    if (size <= 0) {
        return 0;
    }
    return (int)ico_iop_heap_alloc((uint32_t)size);
}

int sceSifFreeIopHeap(int addr)
{
    return ico_iop_heap_free((uint32_t)addr);
}

/* Each record copies size bytes from the EE address src to the IOP address
   dest.  Returns a nonzero transfer id (0 is the PS2's "not queued"). */
int sceSifSetDma(struct sceSifDmaData *sdd, int len)
{
    static int dmaId;
    int i;

    if (sdd == NULL || len <= 0) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        void *dst;

        if (sdd[i].size <= 0) {
            continue;
        }
        if (!ico_iop_range_ok(sdd[i].dest, (uint32_t)sdd[i].size)) {
            return 0;
        }
        dst = ico_iop_ptr(sdd[i].dest);
        memcpy(dst, (const void *)sdd[i].src, (size_t)sdd[i].size);
    }
    if (++dmaId <= 0) {
        dmaId = 1;
    }
    return dmaId;
}

/* A negative status is "finished"; every host transfer is. */
int sceSifDmaStat(int h)
{
    (void)h;
    return -1;
}

void sceSifWriteBackDCache(void *addr, int len)
{
    (void)addr;
    (void)len;
}

/* --- RPC -------------------------------------------------------------------- */

int sceSifBindRpc(struct sceSifRpcClientData *cd, unsigned int sid, int mode)
{
    int i;

    (void)mode;
    if (cd == NULL) {
        return -1;
    }
    cd->pkt = NULL;
    cd->command = sid;
    i = find_server(sid);
    /* serve is the IOP server record's address on the PS2, zero until the
       bind is answered; here a nonzero token for a registered server */
    cd->serve = i >= 0 ? 0x1000u + (unsigned int)i : 0;
    return 0;
}

int sceSifCallRpc(struct sceSifRpcClientData *cd, unsigned int rpc_number, unsigned int mode,
                  void *sendbuf, int ssize, void *recvbuf, int rsize, void (*end_func)(void *),
                  void *end_param)
{
    int i;
    void *reply;

    (void)mode;
    if (cd == NULL || cd->serve == 0) {
        return -1;
    }
    i = find_server(cd->command);
    if (i < 0) {
        return -1;
    }
    lastSid = cd->command;
    lastRpc = rpc_number;
    rpcCount++;
    reply = servers[i].fn(rpc_number, sendbuf, ssize, rsize);
    if (recvbuf != NULL && rsize > 0) {
        if (reply != NULL) {
            memmove(recvbuf, reply, (size_t)rsize);
        } else {
            memset(recvbuf, 0, (size_t)rsize);
        }
    }
    if (end_func != NULL) {
        end_func(end_param);
    }
    return 0;
}

int sceSifCheckStatRpc(struct sceSifRpcClientData *cd)
{
    (void)cd;
    return 0;
}
