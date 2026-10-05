#include "ios.h"
#include "debug.h"
#include "memory.h"
#include "message.h"
#include "thread.h"
#include "s_init.h"
#include <eekernel.h>
#include <sifrpc.h>
#include "main.h"
#include <sound.h>

/* the IOP heap shortfall the allocator records, then a word no retail code
   reads or writes */
int iopBuffOver = 0;

static int iosUnusedWord = 0; /* derived name */

inline int iosSifAllocIopHeapDebug(int size, const char *file, int line)
{
    int p = sceSifAllocIopHeap(size);

    if (p == 0) {
        debug_StdPrintfDummy("iosSifAllocIopHeapDebug: %s %d not alloc\n", file, line);
        if (iopBuffOver != 0) {
            while (sceSifAllocIopHeap(size - iopBuffOver) == 0) {
                iopBuffOver++;
            }
        } else {
            iopBuffOver = size;
        }
    }
    return p;
}

/* the four semaphore descriptors ios_init_plus fills in and hands to CreateSema */
static struct SemaParam padLockSemaParam; /* derived name */

static struct SemaParam stgMgrLockSemaParam; /* derived name */

static struct SemaParam cdLockSemaParam; /* derived name */

static struct SemaParam sndLockSemaParam; /* derived name */

/* keyInput.h is not included */
extern void InitKeyInput(int unused);

static void ios_init_plus(void)
{
    padLockSemaParam.attr = 1;
    padLockSemaParam.maxCount = 1;
    padLockSemaParam.initCount = 0;
    IosPadLock = CreateSema(&padLockSemaParam);
    cdLockSemaParam.attr = 1;
    cdLockSemaParam.maxCount = 1;
    cdLockSemaParam.initCount = 0;
    IosCdLock = CreateSema(&cdLockSemaParam);
    stgMgrLockSemaParam.attr = 1;
    stgMgrLockSemaParam.maxCount = 1;
    stgMgrLockSemaParam.initCount = 0;
    IosStgMgrLock = CreateSema(&stgMgrLockSemaParam);
    sndLockSemaParam.attr = 1;
    sndLockSemaParam.maxCount = 1;
    sndLockSemaParam.initCount = 0;
    IosSndLock = CreateSema(&sndLockSemaParam);
    system_stage_func = 0;
    InitKeyInput(0);
    debug_StdPrintfDummy("SgSndn2RemoteInit()\n");
    SgSndn2RemoteInit();
    sceSifInitIopHeap();
    debug_StdPrintfDummy("allocate IOP heap memory - \n");
    soundAllocIopHeap();
    soundInit();
    screen_offset_x = 0;
    screen_offset_y = 0;
}

void iosInitialize(void)
{
    debug_StdPrintfDummy("iosInitialize()\n");
    iosThreadInit();
    ios_partition_root = iosMallocInitPartition(0x760000, 0x1FEFFF0);
    ios_partition_common = iosMallocSetPartition(ios_partition_root, 4227072, 16);
    ios_partition_smotion = iosMallocSetPartition(ios_partition_root, 1179648, 16);
    ios_partition_s2motion = iosMallocSetPartition(ios_partition_root, 3145728, 16);
    ios_partition_event = iosMallocSetPartition(ios_partition_root, 262144, 16);
    ios_partition_oomori = iosMallocSetPartition(ios_partition_root, 327680, 16);
    ios_partition_horagai = iosMallocSetPartition(ios_partition_root, 1, 16);
    ios_partition_sound = iosMallocSetPartition(ios_partition_root, 32768, 16);
    ios_partition_sound_semi = iosMallocSetPartition(ios_partition_root, 20480, 16);
    ios_partition_shock = iosMallocSetPartition(ios_partition_root, 10240, 16);
    ios_partition_hara = iosMallocSetPartition(ios_partition_root, 1, 16);
    ios_partition_isys = ios_partition_seki = ios_partition_sugipon = ios_partition_dmotion =
        iosMallocSetPartition(ios_partition_root, 15826944, 16);
    iosMallocSetPartitionName(ios_partition_isys, "stage");
    iosMallocSetPartitionName(ios_partition_smotion, "stat mot");
    iosMallocSetPartitionName(ios_partition_s2motion, "demo mot");
    iosMallocSetPartitionName(ios_partition_event, "event");
    iosMallocSetPartitionName(ios_partition_hara, "hara");
    iosMallocSetPartitionName(ios_partition_oomori, "oomori");
    iosMallocSetPartitionName(ios_partition_horagai, "horagai");
    iosMallocSetPartitionName(ios_partition_sound, "sound");
    iosMallocSetPartitionName(ios_partition_shock, "shock");
    iosMallocSetPartitionName(ios_partition_common, "common");
    iosMsgInit();
    ios_init_plus();
}

/* .sdata, after iosInitialize's partition names: the partition handles, then
   global_variable, which no retail code uses. */
IosMemPart *ios_partition_root = 0;

IosMemPart *ios_partition_event = 0;

IosMemPart *ios_partition_isys = 0;

IosMemPart *ios_partition_hara = 0;

IosMemPart *ios_partition_sugipon = 0;

IosMemPart *ios_partition_common = 0;

IosMemPart *ios_partition_dmotion = 0;

IosMemPart *ios_partition_smotion = 0;

IosMemPart *ios_partition_s2motion = 0;

IosMemPart *ios_partition_seki = 0;

IosMemPart *ios_partition_oomori = 0;

IosMemPart *ios_partition_horagai = 0;

IosMemPart *ios_partition_sound = 0;

IosMemPart *ios_partition_sound_semi = 0;

IosMemPart *ios_partition_shock = 0;

IosMemPart *ios_partition_inflate = 0;

IosMemPart *ios_partition_mpeg = 0;

int global_variable = 0;
