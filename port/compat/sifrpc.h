/*
 * port/compat/sifrpc.h
 *
 * The host build's sifrpc.h: the declarations the game uses, from
 * sce/libkernl/sifrpc.h (this project's own clean-room header, MIT), with
 * pointer-sized and 64-bit types made host-correct.
 */
#ifndef ICO_COMPAT_SIFRPC_H
#define ICO_COMPAT_SIFRPC_H

/* one SIF DMA transfer: the EE source, the IOP destination, the byte count
   and the DMAC attribute bits.  src and dest are the bus addresses the DMAC
   is handed, held as numbers: typed as pointers, _sceSifSendCmd's stores
   into the record schedule differently from the ROM.  The attribute word is
   reached through a union member.  Only the word member is known; the
   developers' union may have carried a flag-bit view beside it. */
typedef struct sceSifDmaData {
    __UINTPTR_TYPE__ src; /* EE address: host pointer-sized */
    unsigned int dest;    /* IOP address: 32 bits on any host */
    int size;

    union {
        int attr;
    } u;
} sceSifDmaData;

/* the function a client hands sceSifCallRpc, run when the reply lands */
typedef void (*sceSifEndFunc)(void *param); /* derived name */

/* a server function: handed the request number, the receive buffer and its
   length, it returns the reply buffer */
typedef void *(*sceSifRpcFunc)(int fno, void *buff, int size); /* derived name */

/* an EE client of an IOP RPC server.  The first three words are the request
   in flight, which the SIF command callback (sifrpc's _request_end) finishes.
   buff, cbuff and serve are the IOP's addresses the bind answer carries, held
   as the numbers the answer's words are: typed as pointers, _request_end's
   and sceSifCallRpc's accesses to them reorder against the packet words. */
typedef struct sceSifRpcClientData {
    void *pkt;             /* 0x00, the packet in flight, cleared by the reply */
    int pid;               /* 0x04, that packet's id */
    int sema;              /* 0x08, the semaphore a blocking request waits on, or -1 */
    unsigned int mode;     /* 0x0C */
    unsigned int command;  /* 0x10 */
    unsigned int buff;     /* 0x14, the server's receive buffer */
    unsigned int cbuff;    /* 0x18 */
    sceSifEndFunc endFunc; /* 0x1C */
    void *endParam;        /* 0x20 */
    unsigned int serve;    /* 0x24, the server record, zero until the bind is answered */
} sceSifRpcClientData;

/* the request sceSifGetOtherData makes: the IOP copies size bytes from its
   src to the EE's dest */
typedef struct sceSifReceiveData {
    void *pkt;         /* 0x00, as in the client record */
    int pid;           /* 0x04 */
    int sema;          /* 0x08 */
    unsigned int mode; /* 0x0C */
    void *src;         /* 0x10 */
    void *dest;        /* 0x14 */
    int size;          /* 0x18 */
} sceSifReceiveData;   /* derived name */

/* an EE RPC server, registered on a queue; the fields from client on are the
   request the IOP's call packet fills in */
typedef struct sceSifServeData {
    unsigned int command;         /* 0x00, the service id */
    sceSifRpcFunc func;           /* 0x04 */
    void *buff;                   /* 0x08 */
    int size;                     /* 0x0C */
    sceSifRpcFunc cfunc;          /* 0x10 */
    void *cbuff;                  /* 0x14 */
    int csize;                    /* 0x18 */
    sceSifRpcClientData *client;  /* 0x1C, the IOP client's record */
    void *paddr;                  /* 0x20, the IOP client's packet */
    unsigned int fno;             /* 0x24 */
    void *receive;                /* 0x28, the IOP buffer the reply goes to */
    int rsize;                    /* 0x2C */
    int rmode;                    /* 0x30, nonzero when the client has an end function */
    unsigned int rid;             /* 0x34, the client's packet slot << 16 | flags */
    struct sceSifServeData *link; /* 0x38, the next server on the queue */
    struct sceSifServeData *next; /* 0x3C, the next pending request */
    struct sceSifQueueData *base; /* 0x40 */
} sceSifServeData;

/* a queue of servers one thread serves */
typedef struct sceSifQueueData {
    int key;                       /* 0x00, the serving thread */
    int active;                    /* 0x04, set while a request runs */
    struct sceSifServeData *link;  /* 0x08, the registered servers */
    struct sceSifServeData *start; /* 0x0C, the pending requests */
    struct sceSifServeData *end;   /* 0x10 */
    struct sceSifQueueData *next;  /* 0x14 */
} sceSifQueueData;                 /* derived name */

int _sceSifLoadElfPart(void *name, int sec, int out, int rpcno);

int _sceSifLoadModule(void *name, int arglen, int args, int ret,
                      int rpcno); /* returns the module id or a negative error */

int _sceSifLoadModuleBuffer(void *addr, int arglen, int args, void *ret);
int _sceSifSendCmd(int cid, int mode, void *pkt, int pktsize, void *src, void *dest, int size);
int sceSifAllocIopHeap(int size);
int sceSifBindRpc(struct sceSifRpcClientData *cd, unsigned int sid, int mode);

int sceSifCallRpc(struct sceSifRpcClientData *cd, unsigned int rpc_number, unsigned int mode,
                  void *sendbuf, int ssize, void *recvbuf, int rsize, void (*end_func)(void *),
                  void *end_param);

int sceSifCheckStatRpc(struct sceSifRpcClientData *cd);
int sceSifDmaStat(int h);
void sceSifExecRequest(struct sceSifServeData *sd);
void sceSifExitCmd(void);
int sceSifFreeIopHeap(int addr);

unsigned int
sceSifGetReg(unsigned int reg); /* the register number is unsigned, see sceSifResetIop */

int sceSifInitIopHeap(void);
void sceSifInitRpc(int mode);
int sceSifLoadFileReset(void);
int sceSifLoadModule(void *name, int arglen, int args);
int sceSifRebootIop(const char *img);
int sceSifSetDma(struct sceSifDmaData *sdd, int len);
unsigned int sceSifSetReg(int reg, int val); /* returns a value */
int sceSifSyncIop(void);
void sceSifWriteBackDCache(void *addr, int len);
void sceSifExitRpc(void);
void sceSifStopDma(void);
int sceSifResetIop(char *arg, int mode);

#endif /* ICO_COMPAT_SIFRPC_H */
