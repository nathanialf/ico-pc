#include "typedef.h"
#include "debug.h"
#include "cdvd.h"
#include "memory.h"
#include "ios.h"
#include <string.h>
#include <ctype.h>
#include <eekernel.h>
#include "FileManager.h"
#include "debug_exception.h"
#include <libcdvd.h>
#include <sifrpc.h>
#include <assert.h>

/* libcdvd.h leaves sceCdlFILE incomplete.  This file's entry is 48 bytes,
   with a 32-byte name. */
typedef struct sceCdlFILE {
    unsigned int lsn;
    unsigned int size;
    char name[32];
    unsigned char date[8];
} sceCdlFILE;

/* Wait for the disc; inlined at all seven wait sites. */
static inline void file_WaitDisc(void) /* derived name */
{
    sceCdDiskReady(0);
    while (sceCdStatus() != 10)
        ;
}

void file_Init(void)
{
    debug_StdPrintfDummy("-------------------------------- read from CD\n");
    debug_StdPrintfDummy("initialize CD device.\n");
    sceSifInitRpc(0);
    sceCdInit(0);
    sceCdMmode(iosCdvdMediaType);
    debug_StdPrintfDummy("done.\n");
    debug_StdPrintfDummy("load default module.\n");
    do
        file_WaitDisc();
    while (sceSifRebootIop("cdrom0:\\IOPRP224.IMG;1") == 0);
    while (sceSifSyncIop() == 0)
        ;
    sceSifInitRpc(0);
    sceSifLoadFileReset();
    sceFsReset();
    sceCdInit(0);
    sceCdMmode(iosCdvdMediaType);
    debug_StdPrintfDummy("done.\n");
    debug_StdPrintfDummy("loading iop modules.\n");
    do
        file_WaitDisc();
    while (sceSifLoadModule("cdrom0:\\SIO2MAN.IRX;1", 0, 0) < 0);
    do
        file_WaitDisc();
    while (sceSifLoadModule("cdrom0:\\PADMAN.IRX;1", 0, 0) < 0);
    do
        file_WaitDisc();
    while (sceSifLoadModule("cdrom0:\\MCMAN.IRX;1", 0, 0) < 0);
    do
        file_WaitDisc();
    while (sceSifLoadModule("cdrom0:\\MCSERV.IRX;1", 0, 0) < 0);
    do
        file_WaitDisc();
    while (sceSifLoadModule("cdrom0:\\LIBSD.IRX;1", 0, 0) < 0);
    do
        file_WaitDisc();
    while (sceSifLoadModule("cdrom0:\\SNDN2DRV.IRX;1", 0, 0) < 0);
    debug_StdPrintfDummy("done.\n");
}

static int file_LoadCDFile(void **adr, char *fname, int area)
{
    char path[256];
    sceCdlFILE fp;
    CdRMode mode;
    char *d;
    char *s;
    int c;
    int t;
    int size;
    int asize;
    int sec;
    int err;

    /* the load report, a nested function every switch arm calls; it reads
       fname, adr and size from the enclosing frame */
    inline void PrintLoad(void) /* derived name */
    {
        debug_StdPrintfDummy(
            "loading:\"\033[33m%s\033[m\" (address:\033[35m%p\033[m/size:\033[35m%d\033[m)", fname,
            *adr, size);
    }

    path[0] = '\\';
    d = &path[1];
    s = fname;
    do {
        c = *s;
        if (c == '/') {
            *d = '\\';
            d++;
            continue;
        }
        t = c - 'a' + 'A';
        if (!islower(c))
            t = c;
        *d = t;
        d++;
    } while (*s++ != 0);
    strcat(path, ";1");

    while (sceCdDiskReady(0) == 6)
        ;
    if (sceCdSearchFile(&fp, path) == 0) {
        debug_StdPrintfDummy("file_LoadCDFile:file is not exists? (%s)\n", fname);
        debug_StdPrintfDummy("file_LoadCDFile:file is not exists? (%s)\n", path);
        return -1;
    }

    size = fp.size;
    asize = (size / 2048 + 1) * 2048;

    switch (area) {
    default:
    case 0:
    case 2:
    case 4:
        *adr = iosMallocDebug(ios_partition_seki, asize, "src/FileManager.c", LN(349, 331));
        PrintLoad();
        debug_StdPrintfDummy(" to seki area.(%2.1f%%)\n",
                             ((int)*adr + asize - (int)ios_partition_seki->start) * 100.0f /
                                 10059776.0f);
        break;
    case 1:
        *adr = iosMallocDebug(ios_partition_sugipon, asize, "src/FileManager.c", LN(357, 339));
        PrintLoad();
        debug_StdPrintfDummy(" to sugi area.(%2.1f%%/%2.1f%%)\n", asize * 100.0f / 524288.0f,
                             ((int)*adr + asize - (int)ios_partition_sugipon->start) * 100.0f /
                                 524288.0f);
        break;
    case 3:
        *adr = iosMallocDebug(ios_partition_smotion, asize, "src/FileManager.c", LN(366, 348));
        PrintLoad();
        debug_StdPrintfDummy(
            " to static motion area.(%2.1f%%/%2.1f%%)\n", asize * 100.0f / 1179648.0f,
            ((int)*adr + asize - (int)ios_partition_smotion->start) * 100.0f / 1179648.0f);
        break;
    case 5:
        *adr = iosMallocDebug(ios_partition_dmotion, asize, "src/FileManager.c", LN(375, 357));
        PrintLoad();
        debug_StdPrintfDummy(
            " to dynamic motion area.(%2.1f%%/%2.1f%%)\n", asize * 100.0f / 3670016.0f,
            ((int)*adr + asize - (int)ios_partition_dmotion->start) * 100.0f / 3670016.0f);
        break;
    case 6:
        *adr = iosMallocDebug(ios_partition_hara, asize, "src/FileManager.c", LN(384, 366));
        PrintLoad();
        debug_StdPrintfDummy(" to hara-area.(%2.1f%%)\n",
                             ((int)*adr + asize - (int)ios_partition_hara->start) * 100.0f);
        break;
    case 7:
        *adr = iosMallocDebug(ios_partition_oomori, asize, "src/FileManager.c", LN(392, 374));
        PrintLoad();
        debug_StdPrintfDummy(" to oomori area.(%2.1f%%)\n",
                             ((int)*adr + asize - (int)ios_partition_oomori->start) * 100.0f /
                                 327680.0f);
        break;
    case 8:
        *adr = iosMallocDebug(ios_partition_horagai, asize, "src/FileManager.c", LN(400, 382));
        PrintLoad();
        debug_StdPrintfDummy(" to horagai-area.\n");
        break;
    case 9:
        *adr = iosMallocDebug(ios_partition_sound, asize, "src/FileManager.c", LN(405, 387));
        PrintLoad();
        debug_StdPrintfDummy(" to sound-area.\n");
        break;
    case 10:
        *adr = iosMallocDebug(ios_partition_sound_semi, asize, "src/FileManager.c", LN(410, 392));
        PrintLoad();
        debug_StdPrintfDummy(" to sound_semi-area.\n");
        break;
    }

    mode.trycount = 0;
    mode.spindlctrl = 1;
    mode.datapattern = 0;
    sec = (size - 1) / 2048 + 1;

    FlushCache(0);
    sceCdRead(fp.lsn, sec, *adr, &mode);
    while (sceCdSync(0))
        ;
    while (sceCdDiskReady(0) == 6)
        ;

    err = sceCdGetError();
    switch (err) {
    case 0x32:
        debug_StdPrintfDummy("file_LoadCDFile:Reach to CD end.\n");
        break;
    case 0x31:
        debug_StdPrintfDummy("file_LoadCDFile:Open tray at reading.\n");
        break;
    case 0x30:
        debug_StdPrintfDummy("file_LoadCDFile:Read error.\n");
        break;
    case 0x14:
        debug_StdPrintfDummy("file_LoadCDFile:Invalid Disc.\n");
        break;
    case 0x21:
        debug_StdPrintfDummy("file_LoadCDFile:Invalid transfer length.\n");
        break;
    case 0x20:
        debug_StdPrintfDummy("file_LoadCDFile:Invalid transfer address.\n");
        break;
    case 0x13:
        debug_StdPrintfDummy("file_LoadCDFile:Not ready.\n");
        break;
    case 0x12:
        debug_StdPrintfDummy("file_LoadCDFile:No Disc.\n");
        break;
    case 0x11:
        debug_StdPrintfDummy("file_LoadCDFile:Tray is opened.\n");
        break;
    case 0x10:
        debug_StdPrintfDummy("Undefined command.\n");
    case 0x01:
        debug_StdPrintfDummy("file_LoadCDFile:Aborted.\n");
        break;
    case -1:
        debug_StdPrintfDummy("file_LoadCDFile:Fail\n");
    }

    if (err != 0) {
        debug_assert("src/FileManager.c", LN(440, 422));
        __assert("src/FileManager.c", LN(440, 422), "FALSE");
    }
    return size;
}

int file_LoadFile(void **adr, char *fname, int area)
{
    return file_LoadCDFile(adr, fname, area);
}
