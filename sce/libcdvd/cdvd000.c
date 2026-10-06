/* libcdvd.a(cdvd000): the callback thread, the power-off callback, the ncmd
 * and scmd pre-checks and the sync, init, disk-ready and media-mode entry
 * points. */
#include <eekernel.h>
#include <stdio.h>
#include <sifrpc.h>
#include <sifcmd.h>
#include <libcdvd.h>
#include <libcdvd_internal.h>

/* the search RPC's request record: the 0x24-byte file entry the reply fills,
   the 256-byte name, then the request's own address. */
typedef struct { /* field names derived */
    unsigned char file[0x24];
    char name[0x100];
    void *addr;
} CdSearchReq; /* derived name */

/* The member's .data in link order.  The names the other libcdvd members
   bind to are globals; the rest are statics named for their role.  Every SIF
   RPC buffer is 64-byte aligned, a cache line.  The bind states are -1 until
   the server is bound and 0 after. */
static char sceCdvdVersion[16] = "PsIIlibcdvd 2240"; /* derived name */

int SCE_CD_debug = 0;

static int cb_thread_id = 0; /* derived name */

static int scmd_keep_cmd = 0; /* derived name */

static int ncmd_keep_cmd = 0; /* derived name */

static int cb_semid = -1; /* derived name */

static int poff_busy = 0; /* derived name */

int _sceCd_ncmd_semid = -1;

int _sceCd_scmd_semid = -1;

int _sceCd_c_cb_sem = 0;

volatile int _sceCd_ee_read_mode = 0;

static int ncmd_bind = -1; /* derived name */

static int poff_bind = -1; /* derived name */

static int search_bind = -1; /* derived name */

static int diskready_bind = -1; /* derived name */

static int scmd_bind = -1; /* derived name */

static int init_bind = -1; /* derived name */

static int init_count = 0; /* derived name */

/* The command number the SIF RPC end interrupt writes and the callback thread
   polls: read back after every store, in every function of the member. */
volatile int sceCdCbfunc_num = 0;

int sceCdCbfunc_number = 0;

/* the RPC receive and send buffers the IOP reads and writes by SIF DMA, each
   on its own 64-byte line */
int _sceCd_ncmdrdata[32] __attribute__((aligned(64))) = {0};

int _sceCd_ncmdsdata[1024] __attribute__((aligned(64))) = {0};

int _sceCd_rd_intr_data[48] __attribute__((aligned(64))) = {0};

int _sceCd_Read_cur_pos[4] __attribute__((aligned(64))) = {0};

sceSifRpcClientData _sceCd_cd_ncmd = {0};

int _sceCd_scmdrdata[272] __attribute__((aligned(64))) = {0};

int _sceCd_scmdsdata[258] __attribute__((aligned(64))) = {0};

sceSifRpcClientData _sceCd_cd_scmd = {0};

/* The member's .bss in link order, all file statics: the callbacks, the
   callback thread, and each bound server's client record and buffers (the
   RPC buffers on the SIF DMA's alignment: a 64-byte line, or a quadword for
   diskready's send word). */
static int cd_cbfunc; /* derived name */

static void (*poff_cbfunc)(int); /* derived name */

static int poff_cbarg; /* derived name */

static int cb_thread_word; /* derived name */

static int cd_thread_id; /* derived name */

static struct ThreadParam cd_thread_stat;

static struct ThreadParam cb_thread_param;

static sceSifRpcClientData poff_cd; /* derived name */

static int poff_sdata; /* derived name */

static CdSearchReq search_req __attribute__((aligned(64))); /* derived name */

static int search_rdata[16] __attribute__((aligned(64))); /* derived name */

static sceSifRpcClientData search_cd; /* derived name */

static sceSifRpcClientData init_cd; /* derived name */

static sceSifRpcClientData diskready_cd; /* derived name */

static int init_sdata __attribute__((aligned(64)));      /* derived name */
static int diskready_sdata __attribute__((aligned(16))); /* derived name */

/* CB_DelayTh is the member's first function; its first word sits in the delay
   slot of the jr that ends libkernl.a's sceSifWriteBackDCache, the previous
   object.  The SetAlarm callback: signal the semaphore from the handler and
   re-enable interrupts. */
__asm__(".section .text\n"
        "    .set at\n"
        "    .set noreorder\n"
        "    .global CB_DelayTh\n"
        "    .type CB_DelayTh, @function\n"
        "    .align 2\n"
        "CB_DelayTh:\n"
        "    addiu $29, $29, -0x10\n"
        "    sd    $31, 0x0($29)\n"
        "    jal   iSignalSema\n"
        "    daddu $4, $6, $0\n"
        "    sync\n"
        "    ei\n"
        "    ld    $31, 0x0($29)\n"
        "    jr    $31\n"
        "    addiu $29, $29, 0x10\n"
        "    .size CB_DelayTh, . - CB_DelayTh\n"
        "    nop\n"
        "    .set reorder\n"
        "    .set at\n");

void sceCdDelayThread(unsigned short ticks)
{
    struct SemaParam buf;
    unsigned short id = ticks;
    int r;
    buf.maxCount = 1;
    buf.initCount = 0;
    buf.option = 0;
    r = CreateSema(&buf);
    SetAlarm(id, CB_DelayTh, (void *)r);
    WaitSema(r);
    DeleteSema(r);
}

int sceCdCallback(int func)
{
    int ret;
    if (sceCdSync(1) != 0) {
        return 0;
    }
    DIntr();
    ret = cd_cbfunc;
    cd_cbfunc = func;
    EIntr();
    return ret;
}

/* Runs from the SIF RPC end interrupt.  The callback number, the flag and
 * the semaphore handles are read and written here with volatile accesses,
 * since the callback thread and the command entry points use the same words;
 * the words themselves are not volatile objects, and the member's other
 * synchronisation points (_Cdvd_cbLoop's guard read of the number, the
 * PollSema and error-path SignalSema handle loads of _sceCd_ncmd_prechk,
 * _sceCd_scmd_prechk, sceCdSearchFile and sceCdDiskReady) read them the same
 * way.  How Sony spelled the volatile accesses is not known. */
void _sceCd_cd_callback(void *data)
{
    int *result = data;

    sceCdCbfunc_num = result[0];
    *(volatile int *)&sceCdCbfunc_number = sceCdCbfunc_num;
    if (sceCdCbfunc_num == 11) {
        sceCdCbfunc_num = 0;
        *(volatile int *)&_sceCd_c_cb_sem = 0;
        return;
    }
    iSignalSema(*(volatile int *)&_sceCd_ncmd_semid);
    if (cb_thread_id != 0 && cd_cbfunc != 0) {
        iSignalSema(*(volatile int *)&cb_semid);
    } else {
        _sceCd_c_cb_sem = 0;
    }
    sceCdCbfunc_num = 0;
}

/* The callback number is written by the interrupt-side _sceCd_cd_callback and
 * the loop reads it per access at the guard and again for the argument; the
 * loop-closing release of _sceCd_c_cb_sem is a per-access store as in the
 * callback.  How Sony spelled the volatile accesses is not known. */
void _Cdvd_cbLoop(void *arg)
{
    while (1) {
        WaitSema(cb_semid);
        if (sceCdCbfunc_num == -1) {
            _sceCd_c_cb_sem = 0;
            sceCdCbfunc_num = 0;
            cb_thread_id = 0;
            cb_thread_word = 0;
            ExitDeleteThread();
        }
        if (SCE_CD_debug > 0) {
            scePrintf("sceCdCbfunc= %d sceCdCbfunc_num= %d\n", cd_cbfunc, sceCdCbfunc_number);
        }
        if (cd_cbfunc != 0 && *(volatile int *)&sceCdCbfunc_number != 0) {
            ((void (*)(int))cd_cbfunc)(*(volatile int *)&sceCdCbfunc_number);
        }
        *(volatile int *)&_sceCd_c_cb_sem = 0;
    }
}

/* The link's small-data base, which no header declares */
extern char _gp[];

int sceCdInitEeCB(int priority, void *stack, int stackSize)
{
    int r = 1;

    if (cb_thread_id == 0) {
        cd_thread_id = GetThreadId();
        ReferThreadStatus(cd_thread_id, &cd_thread_stat);
        cb_thread_param.stackSize = stackSize;
        cb_thread_param.gpReg = _gp;
        cb_thread_param.entry = _Cdvd_cbLoop;
        cb_thread_param.stack = stack;
        cb_thread_param.initPriority = priority;
        cb_thread_id = CreateThread(&cb_thread_param);
        StartThread(cb_thread_id, 0);
    } else {
        ChangeThreadPriority(cb_thread_id, priority);
        r = 0;
    }
    return r;
}

/* the read RPC's reply record: the byte counts and destinations of the
   unaligned head and tail of a read, then the two 64-byte bounce buffers. */
typedef struct { /* field names derived */
    int size1;
    int size2;
    char *dest1;
    char *dest2;
    char buf1[64];
    char buf2[64];
} CdReadEnd; /* derived name */

void _sceCd_cd_read_intr(void *pkt)
{
    CdReadEnd *r = (CdReadEnd *)((int)pkt | 0x20000000);
    char *dst;
    int i;

    if (r->size1 > 0) {
        dst = r->dest1;
        for (i = 0; i < r->size1; i++) {
            dst[i] = r->buf1[i];
        }
    }
    if (r->size2 > 0) {
        dst = r->dest2;
        for (i = 0; i < r->size2; i++) {
            dst[i] = r->buf2[i];
        }
    }
    _sceCd_cd_callback((void *)&sceCdCbfunc_num);
}

void cmd_sem_init(void)
{
    struct SemaParam buf;

    if (_sceCd_ncmd_semid == -1 || _sceCd_scmd_semid == -1) {
        buf.option = 0;
        buf.initCount = 1;
        buf.maxCount = 1;
        /* The handle stores are the member's per-access volatile spelling (the
           form the semaphore reads at the wait sites above use). */
        *(volatile int *)&_sceCd_ncmd_semid = CreateSema(&buf);
        _sceCd_scmd_semid = CreateSema(&buf);
        buf.initCount = 0;
        *(volatile int *)&cb_semid = CreateSema(&buf);
        *(volatile int *)&_sceCd_c_cb_sem = 0;
    }
}

void cdvd_exit(void)
{
    if (cb_thread_id != 0) {
        sceCdCbfunc_num = -1;
        /* The handle read volatile, as _sceCd_cd_callback's iSignalSema and
           cmd_sem_init's store access it: the callback thread waits on it.
           Both accesses volatile, the load stays behind the -1 store
           (alias.c true_dependence) and out of the call's slot at reorg;
           SCE's 2.10 assembler fills the slot with it. */
        SignalSema(*(volatile int *)&cb_semid);
    }
    DeleteSema(_sceCd_ncmd_semid);
    DeleteSema(_sceCd_scmd_semid);
    DeleteSema(cb_semid);
    DIntr();
    sceSifRemoveCmdHandler(0x80000012);
    EIntr();
}

int sceCdPOffCallback(int func, int arg)
{
    int ret;
    if (poff_bind < 0) {
        PowerOffCB();
    }
    DIntr();
    ret = (int)poff_cbfunc;
    poff_cbarg = arg;
    poff_cbfunc = (void (*)(int))func;
    EIntr();
    return ret;
}

void _sceCd_Poff_Intr(void)
{
    if (poff_cbfunc != 0 && poff_busy == 0) {
        poff_cbfunc(poff_cbarg);
    }
}

/* Binds the power-off RPC once and asks the IOP side to arm it.  The bound
 * flag is cleared in the serve arm ahead of the break.  The busy flag
 * poff_busy is read by _sceCd_Poff_Intr from the SIF command interrupt: its
 * three clears are volatile accesses while the set is plain.  How Sony
 * spelled the volatile clears is not known. */
int PowerOffCB(void)
{
    int i;
    int w;

    sceSifInitRpc(0);
    poff_busy = 1;
    DIntr();
    sceSifAddCmdHandler(0x80000012, _sceCd_Poff_Intr, 0);
    EIntr();
    if (poff_bind < 0) {
        i = 0;
        while (1) {
            if (sceSifBindRpc(&poff_cd, 0x80000596, 0) < 0) {
                if (SCE_CD_debug > 0) {
                    scePrintf("Libcdvd bind err PowerOffCB\n");
                }
                w = 0x100000;
                while (w--) {}
                continue;
            }
            if (poff_cd.serve != 0) {
                poff_bind = 0;
                break;
            }
            w = 0x100000;
            while (w--) {}
            if (i++ >= 17) {
                *(volatile int *)&poff_busy = 0;
                return 0;
            }
        }
    }
    poff_sdata = 11;
    if (sceSifCallRpc(&poff_cd, 1, 1, 0, 0, 0, 0, 0, 0) < 0) {
        *(volatile int *)&poff_busy = 0;
        return 0;
    }
    *(volatile int *)&poff_busy = 0;
    return 1;
}

/* the file entry the call fills in, libcdvd's sceCdlFILE: the first sector,
   the byte length, the name and the date.  This member copies it whole as
   0x24 bytes and reads its words by offset; the copy's lwl/lwr pairs show the
   record it copies is byte-aligned here, so its body is the bytes. */
typedef struct sceCdlFILE {
    unsigned char b[0x24];
} sceCdlFILE;

/* The bind block is PowerOffCB's (bound flag cleared in the serve arm); the
   semaphore-handle loads are the per-site volatile accesses described above
   _sceCd_cd_callback. */
int sceCdSearchFile(sceCdlFILE *fp, const char *name)
{
    char *req;
    int w;
    int i;
    int v;

    cmd_sem_init();
    if (_sceCd_ncmd_semid != PollSema(*(volatile int *)&_sceCd_ncmd_semid)) {
        return 0;
    }
    ncmd_keep_cmd = 1;
    ReferThreadStatus(cd_thread_id, &cd_thread_stat);
    if (sceCdSync(1) != 0) {
        SignalSema(*(volatile int *)&_sceCd_ncmd_semid);
        return 0;
    }
    sceSifInitRpc(0);
    if (search_bind < 0) {
        while (1) {
            if (sceSifBindRpc(&search_cd, 0x80000597, 0) < 0) {
                if (SCE_CD_debug > 0) {
                    scePrintf("Libcdvd bind err CdSearchFile\n");
                }
                w = 0x100000;
                while (w--) {}
                continue;
            }
            if (search_cd.serve != 0) {
                search_bind = 0;
                break;
            }
            w = 0x100000;
            while (w--) {}
        }
    }
    for (i = 0; i < 0x100; i++) {
        if ((search_req.name[i] = name[i]) == 0) {
            break;
        }
    }
    if (i == 0x100) {
        search_req.name[i - 1] = 0;
    }
    req = (char *)&search_req;
    *(char **)(req + 0x124) = req;
    if (SCE_CD_debug > 0) {
        scePrintf("ee call cmd search %s\n", req + 0x24);
    }
    sceSifWriteBackDCache(req, 0x128);
    if (sceSifCallRpc(&search_cd, 0, 0, req, 0x128, search_rdata, 4, 0, 0) < 0) {
        SignalSema(*(volatile int *)&_sceCd_ncmd_semid);
        return 0;
    }
    *fp = *(sceCdlFILE *)((int)req | 0x20000000);
    if (SCE_CD_debug > 0) {
        scePrintf("search name %s\n", (char *)fp + 8);
    }
    if (SCE_CD_debug > 0) {
        scePrintf("search size %d\n", ((int *)fp)[1]);
    }
    if (SCE_CD_debug > 0) {
        scePrintf("search loc lbn %d\n", ((int *)fp)[0]);
    }
    v = *(int *)((int)search_rdata | 0x20000000);
    SignalSema(_sceCd_ncmd_semid);
    return v;
}

/* Same bind block and handle accesses as sceCdSearchFile. */
int _sceCd_ncmd_prechk(int cmd)
{
    int w;

    cmd_sem_init();
    if (_sceCd_ncmd_semid != PollSema(*(volatile int *)&_sceCd_ncmd_semid)) {
        if (SCE_CD_debug > 0) {
            scePrintf("Ncmd fail sema cur_cmd:%d keep_cmd:%d\n", cmd, ncmd_keep_cmd);
        }
        return 0;
    }
    ncmd_keep_cmd = cmd;
    ReferThreadStatus(cd_thread_id, &cd_thread_stat);
    if (sceCdSync(1) != 0) {
        SignalSema(*(volatile int *)&_sceCd_ncmd_semid);
        return 0;
    }
    sceSifInitRpc(0);
    if (ncmd_bind < 0) {
        while (1) {
            if (sceSifBindRpc(&_sceCd_cd_ncmd, 0x80000595, 0) < 0) {
                if (SCE_CD_debug > 0) {
                    scePrintf("Libcdvd bind err N CMD\n");
                }
                w = 0x100000;
                while (w--) {}
                continue;
            }
            if (_sceCd_cd_ncmd.serve != 0) {
                ncmd_bind = 0;
                break;
            }
            w = 0x100000;
            while (w--) {}
        }
    }
    return 1;
}

int sceCdNcmdDiskReady(void)
{
    int *p;
    int v;
    if (_sceCd_ncmd_prechk(2) == 0) {
        return 0;
    }
    p = _sceCd_ncmdrdata;
    if (sceSifCallRpc(&_sceCd_cd_ncmd, 0xE, 0, 0, 0, p, 4, 0, 0) < 0) {
        SignalSema(_sceCd_ncmd_semid);
        return 0;
    }
    v = *(int *)((int)p | 0x20000000);
    SignalSema(_sceCd_ncmd_semid);
    return v;
}

int sceCdSync(int mode)
{
    if (!mode) {
        if (SCE_CD_debug > 0)
            scePrintf("N cmd wait\n");
        while (_sceCd_c_cb_sem != 0 || sceSifCheckStatRpc(&_sceCd_cd_ncmd)) {
            sceCdDelayThread(0x3C);
        }
        return 0;
    }
    if (_sceCd_c_cb_sem != 0 || sceSifCheckStatRpc(&_sceCd_cd_ncmd) != 0) {
        return 1;
    }
    return 0;
}

int sceCdSyncS(int mode)
{
    if (!mode) {
        if (SCE_CD_debug > 0)
            scePrintf("S cmd wait\n");
        while (sceSifCheckStatRpc(&_sceCd_cd_scmd)) {
            sceCdDelayThread(0x3C);
        }
        return 0;
    }
    return sceSifCheckStatRpc(&_sceCd_cd_scmd);
}

/* The scmd counterpart of _sceCd_ncmd_prechk. */
int _sceCd_scmd_prechk(int cmd)
{
    int w;

    cmd_sem_init();
    if (_sceCd_scmd_semid != PollSema(*(volatile int *)&_sceCd_scmd_semid)) {
        if (SCE_CD_debug > 0) {
            scePrintf("Scmd fail sema cur_cmd:%d keep_cmd:%d\n", cmd, scmd_keep_cmd);
        }
        return 0;
    }
    scmd_keep_cmd = cmd;
    ReferThreadStatus(cd_thread_id, &cd_thread_stat);
    if (sceCdSyncS(1) != 0) {
        SignalSema(*(volatile int *)&_sceCd_scmd_semid);
        return 0;
    }
    sceSifInitRpc(0);
    if (scmd_bind < 0) {
        while (1) {
            if (sceSifBindRpc(&_sceCd_cd_scmd, 0x80000593, 0) < 0) {
                if (SCE_CD_debug > 0) {
                    scePrintf("Libcdvd bind err S cmd\n");
                }
                w = 0x100000;
                while (w--) {}
                continue;
            }
            if (_sceCd_cd_scmd.serve != 0) {
                scmd_bind = 0;
                break;
            }
            w = 0x100000;
            while (w--) {}
        }
    }
    return 1;
}

/* Binds the init RPC and reads the IOP module's version reply.  The busy flag
 * set and the ee_read_mode reset are volatile accesses: the busy word is read
 * by _sceCd_Poff_Intr from the SIF command interrupt (its clears here and in
 * PowerOffCB are volatile too) and ee_read_mode is shared with sceCdRead and
 * sceCdReadIOPm.  The poff_bind reset is the last -1 store, and the init_bind
 * reset sits between the diskready_bind and poff_bind resets. */
int sceCdInit(int mode)
{
    int *p;
    int ver;
    int r;
    int w;
    int type;
    int v1;
    int v2;

    if (sceCdSyncS(1) != 0) {
        return 0;
    }
    sceSifInitRpc(0);
    cd_thread_id = GetThreadId();
    *(volatile int *)&poff_busy = 1;
    search_bind = -1;
    ncmd_bind = -1;
    scmd_bind = -1;
    diskready_bind = -1;
    init_bind = -1;
    poff_bind = -1;
    _sceCd_ee_read_mode = 0;
    init_count++;
    while (1) {
        r = sceSifBindRpc(&init_cd, 0x80000592, 0);
        if (r < 0) {
            if (SCE_CD_debug > 0) {
                scePrintf("Libcdvd bind err %d CD_Init %d\n", r, init_count);
            }
            w = 0x100000;
            while (w--) {}
            continue;
        }
        if (init_cd.serve != 0) {
            init_sdata = mode;
            init_bind = 0;
            sceSifWriteBackDCache(&init_sdata, 4);
            p = _sceCd_scmdrdata;
            if (sceSifCallRpc(&init_cd, 0, 0, &init_sdata, 4, p, 16, 0, 0) < 0) {
                *(volatile int *)&poff_busy = 0;
                return 0;
            }
            break;
        }
        w = 0x100000;
        while (w--) {}
    }
    v1 = *(int *)((int)(p + 1) | 0x20000000);
    v2 = *(int *)((int)(p + 2) | 0x20000000);
    type = *(int *)((int)(p + 3) | 0x20000000);
    ver = 1;
    if (type == 0xFF) {
    } else if (type == 0xFE) {
        SCE_CD_debug = 1;
    } else if (v1 / 256 < 2 || v2 / 256 < 2) {
        ver = 2;
    }
    *(volatile int *)&poff_busy = 0;
    switch (mode) {
    case 5:
        if (SCE_CD_debug > 0) {
            scePrintf("Libcdvd Exit\n");
        }
        cdvd_exit();
        *(volatile int *)&_sceCd_ncmd_semid = -1;
        *(volatile int *)&_sceCd_scmd_semid = -1;
        *(volatile int *)&cb_semid = -1;
        break;
    case 0:
    case 1:
    default:
        cmd_sem_init();
        PowerOffCB();
        break;
    }
    return ver;
}

int sceCdDiskReady(int mode)
{
    int v;
    int w;

    if (SCE_CD_debug > 0) {
        scePrintf("DiskReady 0\n");
    }
    cmd_sem_init();
    if (_sceCd_scmd_semid != PollSema(*(volatile int *)&_sceCd_scmd_semid)) {
        return 6;
    }
    if (sceCdSyncS(1) != 0) {
        SignalSema(*(volatile int *)&_sceCd_scmd_semid);
        return (mode != 8) ? 6 : -1;
    }
    sceSifInitRpc(0);
    if (diskready_bind < 0) {
        while (1) {
            if (sceSifBindRpc(&diskready_cd, 0x8000059A, 0) < 0) {
                if (SCE_CD_debug > 0) {
                    scePrintf("Libcdvd bind err CdDiskReady\n");
                }
                w = 0x100000;
                while (w--) {}
                continue;
            }
            if (diskready_cd.serve != 0) {
                diskready_bind = 0;
                break;
            }
            w = 0x100000;
            while (w--) {}
        }
    }
    diskready_sdata = mode;
    sceSifWriteBackDCache(&diskready_sdata, 4);
    if (sceSifCallRpc(&diskready_cd, 0, 0, &diskready_sdata, 4, _sceCd_scmdrdata, 4, 0, 0) < 0) {
        SignalSema(*(volatile int *)&_sceCd_scmd_semid);
        return (mode != 8) ? 6 : -1;
    }
    if (SCE_CD_debug > 0) {
        scePrintf("DiskReady ended\n");
    }
    v = *(int *)((int)_sceCd_scmdrdata | 0x20000000);
    SignalSema(_sceCd_scmd_semid);
    return v;
}

int sceCdMmode(int media)
{
    int *p;
    int *sd;
    int v;
    sd = _sceCd_scmdsdata;
    if (_sceCd_scmd_prechk(0x22) == 0) {
        return 0;
    }
    sd[0] = media;
    sceSifWriteBackDCache(sd, 4);
    p = _sceCd_scmdrdata;
    if (sceSifCallRpc(&_sceCd_cd_scmd, 0x22, 0, sd, 4, p, 4, 0, 0) < 0) {
        SignalSema(_sceCd_scmd_semid);
        return 0;
    }
    v = *(int *)((int)p | 0x20000000);
    SignalSema(_sceCd_scmd_semid);
    return v;
}
