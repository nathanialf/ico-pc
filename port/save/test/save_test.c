/*
 * port/save/test/save_test.c
 *
 * The host memory card (port/save/mc_host.c) in two parts, on temporary card
 * folders under the working directory:
 *
 *   A. the sceMc calls fumi/ios/mcard.c issues, scripted by hand: first boot
 *      on a card with no save, the first save to slot 1, reload, re-save
 *      (the file is byte-identical), delete; and the result codes the game
 *      compares (libmc.h sceMcRes*).
 *   B. fumi/ios/mcard.c and fumi/ios/mcdata.c themselves, compiled unchanged
 *      with the game's options, on the fiber scheduler: the card manager
 *      thread, driven through the iosMc* entry points the way kanbanBoot.c
 *      and layout_action.c drive them, with the bytes it leaves in the card
 *      folder checked against a hand-laid-out expectation. The icon files
 *      come through mcdata.c's background read (iosMcSaveIconBlock ->
 *      iosMcIconWriteIconsys -> iosCdvdBackGroundRead into its 64-byte
 *      aligned stack buffer); a stand-in of cdvd.c's background manager
 *      below runs the read on its own thread and fills a known pattern.
 *   C. the importer (port/save/mc_import.c) on synthetic card images, plain
 *      and with ECC spares, and a synthetic .psu, all built here: the
 *      game's files read back through the folder card byte for byte, other
 *      games' entries skipped and named; tools/mc_import (argv[1]) on the
 *      same files; and the second card in port 1.
 *
 * Both run on simulated vsyncs, as host_loop.c drives the hooks. No game
 * data is needed.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32

#include <direct.h>

#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#else

#include <unistd.h>

#define MKDIR(p) mkdir((p), 0777)
#define RMDIR(p) rmdir(p)
#endif

#include <eekernel.h>
#include <eeregs.h>
#include <libmc.h>
#include "debug.h"
#include "ios.h"
#include "main.h"
#include "cdvd.h"
#include "mc_host.h"
#include "mc_import.h"
#include "mcard.h"
#include "mcdata.h"
#include "memory.h"
#include "message.h"
#include "pad.h"
#include "s_init.h"
#include "thread.h"
#include "typedef.h"
#include "arena.h"
#include "host_loop.h"
#include "kernel_host.h"
#include "sched.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* --- what the rest of the game would define ----------------------------------- */

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("FAIL debug_assertMessage %s:%d: %s\n", file, line, mes);
    fails++;
}

void debug_assert(const char *file, int line)
{
    printf("FAIL debug_assert %s:%d\n", file, line);
    fails++;
}

int odd_even;

IosMemPart *ios_partition_root;

IosMemPart *ios_partition_event;

/* the globals product_write and gameblock_write read, and the setters
   product_read and gameblock_read call */
int systemStatus[12];

int NonLinearCameraMove;

int optionControlType;

int optionScreenMode;

int girlControlMode;

int iosPadActRequestEnable;

PadConf iosPadConfCustom;

static int soundOutputMode;

int soundOutputModeGet(void)
{
    return soundOutputMode;
}

void soundOutputModeSet(int mode)
{
    soundOutputMode = mode;
}

/* the icon files' directory entries (name, size): the disc's, which the
   port gets from the data tables */
const IconFile iconFile[] = {{"", 0}, {"icon.sys", 964}, {"boy_blk.ico", 3000}};

/* --- cdvd.c's background manager, as mcdata.c uses it ---------------------------
 *
 * The game runs each request's read function on the cdvd manager thread,
 * which Main wakes once a vsync while IosCdvdMgrSleep is set (main.c). Here
 * the thread is bgTh, the driver's poll wakes it, and the "disc" is a pattern:
 * byte k of file f is icon_byte(f, k). */
int IosCdvdMgrSleep;

static CdvdBgReq bgReq[2];

static IOSThread bgTh;

static char bgStack[0x8000] __attribute__((aligned(16)));

static int bgReads;

static int bgMisaligned;

static unsigned char icon_byte(const char *name, long k)
{
    return (unsigned char)(k * 7 + (unsigned char)name[0] * 13 + (k >> 8));
}

CdvdBgReq *iosCdvdBackGroundMgrAdd(const char *name, void *readFunc, void *readArg, void *readyFunc,
                                   void *resumeFunc, void *cbArg, void *closeFunc, void *closeArg)
{
    int i;

    (void)readyFunc;
    (void)resumeFunc;
    (void)cbArg;
    for (i = 0; i < 2; i++) {
        CdvdBgReq *bg = &bgReq[i];

        if (bg->name[0] == 0) {
            memset(bg, 0, sizeof(*bg));
            strcpy(bg->name, name);
            bg->readFunc = (int (*)(CdvdBgReq *, void *))readFunc;
            bg->readArg = readArg;
            bg->closeFunc = (int (*)(CdvdBgReq *, void *))closeFunc;
            bg->closeArg = closeArg;
            bg->size = 1 << 20;
            return bg;
        }
    }
    printf("FAIL no free background request\n");
    fails++;
    return &bgReq[0];
}

int iosCdvdBackGroundRead(CdvdBgReq *self, void *buf, int size)
{
    unsigned char *p = buf;
    int i;

    /* mcdata.c rounds its stack buffer up to 64 bytes for the DMA */
    if (((uintptr_t)buf & 63) != 0) {
        bgMisaligned++;
    }
    for (i = 0; i < size; i++) {
        p[i] = icon_byte(self->name, self->pos + i);
    }
    self->pos += size;
    bgReads++;
    return !(self->pos < self->size);
}

void iosCdvdBackGroundMgrDelete(CdvdBgReq *self)
{
    self->flags.del = 1;
}

/* iosCdvdBackGroundMgr's loop: each request's read function until it says
   it is done, then the close when its deletion is asked for */
static void bg_manager(void)
{
    int i;

    for (;;) {
        for (i = 0; i < 2; i++) {
            CdvdBgReq *bg = &bgReq[i];

            if (bg->name[0] == 0) {
                continue;
            }
            if (bg->flags.del == 0) {
                if (bg->readFunc != 0 && bg->readFunc(bg, bg->readArg) > 0) {
                    bg->readFunc = 0;
                }
            } else {
                if (bg->closeFunc != 0) {
                    bg->closeFunc(bg, bg->closeArg);
                }
                bg->name[0] = 0;
            }
        }
        IosCdvdMgrSleep = 1;
        iosThreadSleep();
        IosCdvdMgrSleep = 0;
    }
}

/* the game's mcdata.c, unchanged (the test's own unit, so the port's build
   lists need no entry for it) */
#include "../../../ico2/fumi/ios/mcdata.c"

/* --- files --------------------------------------------------------------------- */

#define CARD_A "save_test_card_a"
#define CARD_B "save_test_card_b"
#define CARD_C "save_test_card_c" /* port 1 in part B */
#define CARD_I "save_test_card_i" /* imports, part C */
#define CARD_J "save_test_card_j"
#define CARD_K "save_test_card_k"
#define PRODUCT "BESCES-50760ico"

static long file_size(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static int slurp(const char *path, unsigned char *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, max, f);
    fclose(f);
    return (int)n;
}

static void rm_card(const char *card)
{
    static const char *const files[] = {"icon.sys", "boy_blk.ico", PRODUCT,    "game.000",
                                        "game.001", "game.002",    "game.003", "extra"};
    char path[256];
    size_t i;

    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(path, sizeof(path), "%s/" PRODUCT "/%s", card, files[i]);
        remove(path);
    }
    snprintf(path, sizeof(path), "%s/" PRODUCT, card);
    RMDIR(path);
    RMDIR(card);
}

/* --- part A: libmc ------------------------------------------------------------- */

static void vsync_hooks(void)
{
    ico_host_run_vsync_hooks();
}

/* a request is accepted, is not done before a vsync, and is done after one */
static int finish(int expect_cmd)
{
    int cmd = 0;
    int result = 12345;

    CHECK(ico_mc_host_pending() == 1);
    CHECK(sceMcSync(1, &cmd, &result) == 0);
    vsync_hooks();
    CHECK(sceMcSync(1, &cmd, &result) == 1);
    CHECK(cmd == expect_cmd);
    CHECK(sceMcSync(1, &cmd, &result) == -1);
    return result;
}

static int c_getinfo(int port, int *type, int *free, int *format)
{
    CHECK(sceMcGetInfo(port, 0, type, free, format) == 0);
    return finish(sceMcFuncNoCardInfo);
}

static int c_chdir(const char *dir, char *pwd)
{
    CHECK(sceMcChdir(0, 0, (char *)dir, pwd) == 0);
    return finish(sceMcFuncNoChDir);
}

static int c_mkdir(const char *dir)
{
    CHECK(sceMcMkdir(0, 0, (char *)dir) == 0);
    return finish(sceMcFuncNoMkdir);
}

static int c_open(const char *name, int flags)
{
    CHECK(sceMcOpen(0, 0, (char *)name, flags) == 0);
    return finish(sceMcFuncNoOpen);
}

static int c_close(int fd)
{
    CHECK(sceMcClose(fd) == 0);
    return finish(sceMcFuncNoClose);
}

static int c_write(int fd, const void *buf, int len)
{
    CHECK(sceMcWrite(fd, (void *)buf, len) == 0);
    return finish(sceMcFuncNoWrite);
}

static int c_read(int fd, void *buf, int len)
{
    CHECK(sceMcRead(fd, buf, len) == 0);
    return finish(sceMcFuncNoRead);
}

static int c_seek(int fd, int off, int origin)
{
    CHECK(sceMcSeek(fd, off, origin) == 0);
    return finish(sceMcFuncNoSeek);
}

static int c_flush(int fd)
{
    CHECK(sceMcFlush(fd) == 0);
    return finish(sceMcFuncNoFlush);
}

static int c_delete(const char *name)
{
    CHECK(sceMcDelete(0, 0, (char *)name) == 0);
    return finish(sceMcFuncNoDelete);
}

static int c_getdir(const char *pattern, sceMcTblGetDir *t, int n)
{
    CHECK(sceMcGetDir(0, 0, (char *)pattern, 0, n, t) == 0);
    return finish(sceMcFuncNoGetDir);
}

/* mcard.c's save of one file: 0x203, 1024-byte writes, the four-byte sum,
   flush, close */
static void save_file(const char *name, const unsigned char *data, int len)
{
    int fd = c_open(name, 0x203);
    int off;
    int sum = 0;
    int i;

    CHECK(fd >= 0);
    for (off = 0; off < len; off += 1024) {
        int n = len - off < 1024 ? len - off : 1024;
        CHECK(c_write(fd, data + off, n) == n);
    }
    for (i = 0; i < len; i++) {
        sum += data[i];
    }
    CHECK(c_write(fd, &sum, 4) == 4);
    CHECK(c_flush(fd) == 0);
    CHECK(c_close(fd) == 0);
}

static void test_libmc(void)
{
    static unsigned char data[25600];
    static unsigned char back[25700];
    static unsigned char first[25700];
    sceMcTblGetDir dir[20];
    char pwd[20];
    char path[256];
    int type;
    int free;
    int format;
    int fd;
    int i;
    int n;
    int fds[4];

    for (i = 0; i < (int)sizeof(data); i++) {
        data[i] = (unsigned char)(i * 7 + 3);
    }
    ico_sched_reset();
    rm_card(CARD_A);
    ico_mc_host_set_root(CARD_A);
    CHECK(sceMcSync(1, NULL, NULL) == -1); /* nothing pending before the init */
    CHECK(sceMcInit() == 0);
    CHECK(sceMcSync(1, NULL, NULL) == -1);

    /* first boot: a formatted 8 MB card, new, with nothing on it. The folder
       is not created by looking at it. */
    CHECK(c_getinfo(0, &type, &free, &format) == sceMcResChangedCard);
    CHECK(type == 2 && format == 1 && free == ICO_MC_HOST_CLUSTERS);
    CHECK(c_getinfo(0, &type, &free, &format) == 0);
    CHECK(type == 2 && format == 1 && free >= 360);
    CHECK(c_getinfo(1, &type, &free, &format) == sceMcResFailDetect2);
    CHECK(type == 0 && free == 0 && format == 0); /* port 1 is empty */
    {
        struct stat st;
        CHECK(stat(CARD_A, &st) != 0);
    }
    CHECK(c_chdir("/" PRODUCT, NULL) == sceMcResNoEntry);
    CHECK(c_open("/" PRODUCT "/game.001", 1) == sceMcResNoEntry);
    CHECK(c_getdir("game.*", dir, 20) == 0);
    CHECK(c_getdir("/" PRODUCT "/game.*", dir, 20) == sceMcResNoEntry);
    CHECK(sceMcOpen(1, 0, "/" PRODUCT "/game.001", 1) == 0);
    CHECK(finish(sceMcFuncNoOpen) == sceMcResFailDetect);

    /* the first save: Mkdir (a second time: -4, which the game tolerates),
       Chdir, the three file kinds */
    CHECK(c_mkdir("/" PRODUCT) == 0);
    CHECK(c_mkdir("/" PRODUCT) == sceMcResNoEntry);
    CHECK(c_chdir("/" PRODUCT, pwd) == 0);
    CHECK(strcmp(pwd, "/" PRODUCT) == 0);
    save_file("icon.sys", data, 964);
    save_file("boy_blk.ico", data + 1, 3000);
    save_file(PRODUCT, data + 2, 496);
    save_file("game.001", data, 25596);
    CHECK(file_size(CARD_A "/" PRODUCT "/game.001") == 25600);
    CHECK(file_size(CARD_A "/" PRODUCT "/icon.sys") == 968);
    CHECK(file_size(CARD_A "/" PRODUCT "/" PRODUCT) == 500);

    /* the card now lists the save, and the free space has dropped by what
       the files take (1 KB clusters, a directory one) */
    CHECK(c_getinfo(0, &type, &free, &format) == 0);
    CHECK(free == ICO_MC_HOST_CLUSTERS - (1 + 1 + 3 + 1 + 25));
    n = c_getdir("game.*", dir, 20);
    CHECK(n == 1);
    CHECK(strcmp((char *)dir[0].EntryName, "game.001") == 0);
    CHECK(dir[0].FileSizeByte == 25600);
    CHECK(dir[0].AttrFile == 0x8497);
    CHECK(c_getdir("*", dir, 20) ==
          6); /* . .. then sorted: BESCES.., boy_blk.ico, game.001, icon.sys */
    CHECK(c_getdir("/*", dir, 20) == 1);
    CHECK(strcmp((char *)dir[0].EntryName, PRODUCT) == 0 && dir[0].AttrFile == 0x8427);

    /* reload: open read-only, read in 1024s, seek to the sum, read it */
    fd = c_open("game.001", 1);
    CHECK(fd >= 0);
    memset(back, 0, sizeof(back));
    for (i = 0; i < 25596; i += 1024) {
        int want = 25596 - i < 1024 ? 25596 - i : 1024;
        CHECK(c_read(fd, back + i, want) == want);
    }
    CHECK(memcmp(back, data, 25596) == 0);
    CHECK(c_read(fd, back, 1024) == 4); /* the sum */
    CHECK(c_read(fd, back, 1024) == 0); /* the end of the file */
    CHECK(c_seek(fd, -4, SCE_SEEK_END) == 25596);
    CHECK(c_read(fd, back, 4) == 4);
    CHECK(c_seek(fd, 0, SCE_SEEK_CUR) == 25600);
    CHECK(c_write(fd, back, 4) == sceMcResDeniedPermit); /* opened read-only */
    CHECK(c_close(fd) == 0);
    CHECK(c_close(fd) == sceMcResDeniedPermit);

    /* re-save: the same bytes, the same file; the open does not truncate */
    CHECK(slurp(CARD_A "/" PRODUCT "/game.001", first, sizeof(first)) == 25600);
    save_file("game.001", data, 25596);
    CHECK(slurp(CARD_A "/" PRODUCT "/game.001", back, sizeof(back)) == 25600);
    CHECK(memcmp(first, back, 25600) == 0);
    /* a shorter write over it leaves the rest, as a card does; with SCE_TRUNC
       the file is cut */
    fd = c_open("game.001", 0x203);
    CHECK(c_write(fd, data, 10) == 10);
    CHECK(c_close(fd) == 0);
    CHECK(file_size(CARD_A "/" PRODUCT "/game.001") == 25600);
    fd = c_open("game.001", SCE_WRONLY | SCE_TRUNC);
    CHECK(c_close(fd) == 0);
    CHECK(file_size(CARD_A "/" PRODUCT "/game.001") == 0);
    save_file("game.001", data, 25596);

    /* handles: three at a time */
    for (i = 0; i < 3; i++) {
        fds[i] = c_open("game.001", 1);
        CHECK(fds[i] == i);
    }
    CHECK(c_open("game.001", 1) == sceMcResUpLimitHandle);
    CHECK(c_close(fds[1]) == 0);
    CHECK(c_open("game.001", 1) == 1);
    CHECK(c_close(0) == 0 && c_close(1) == 0 && c_close(2) == 0);

    /* open without CREAT; names the card cannot hold; the host's own files */
    CHECK(c_open("game.002", 1) == sceMcResNoEntry);
    CHECK(c_open("game.002", 0x203) >= 0);
    CHECK(c_close(0) == 0);
    CHECK(c_open("a\\b", 0x203) == sceMcResNoEntry);
    CHECK(c_open("/nodir/file", 0x203) == sceMcResNoEntry);
    CHECK(c_open("/../" PRODUCT "/game.001", 1) == 0); /* ".." stops at the root */
    CHECK(c_close(0) == 0);
    CHECK(c_open("this-name-is-longer-than-thirty-one-bytes", 0x203) == sceMcResNoEntry);
    snprintf(path, sizeof(path), "%s/.DS_Store", CARD_A);
    {
        FILE *f = fopen(path, "wb");
        CHECK(f != NULL);
        if (f != NULL) {
            fclose(f);
        }
    }
    CHECK(c_getdir("/*", dir, 20) == 1); /* hidden files are not the card's */
    remove(path);

    /* delete */
    CHECK(c_delete("game.002") == 0);
    CHECK(c_delete("game.002") == sceMcResNoEntry);
    CHECK(c_delete("game.001") == 0);
    CHECK(c_getdir("game.*", dir, 20) == 0);
    CHECK(c_delete("/" PRODUCT) == sceMcResNotEmpty);
    CHECK(c_delete("icon.sys") == 0 && c_delete("boy_blk.ico") == 0 && c_delete(PRODUCT) == 0);
    CHECK(c_delete("/" PRODUCT) == 0);
    CHECK(c_chdir("/" PRODUCT, NULL) == sceMcResNoEntry);

    /* unformat and format change what the card says, never what is on it */
    CHECK(sceMcUnformat(0, 0) == 0);
    CHECK(finish(sceMcFuncNoUnformat) == 0);
    CHECK(c_getinfo(0, &type, &free, &format) == sceMcResNoFormat);
    CHECK(type == 2 && format == 0);
    CHECK(c_chdir("/", NULL) == sceMcResNoFormat);
    CHECK(sceMcFormat(0, 0) == 0);
    CHECK(finish(sceMcFuncNoFormat) == 0);
    CHECK(c_getinfo(0, &type, &free, &format) == 0 && format == 1);

    rm_card(CARD_A);
}

/* --- part B: fumi/ios/mcard.c on the scheduler ---------------------------------- */

static McMgr req;

static IOSThread mcTh;

static IOSThread driverTh;

static IOSThread idleTh;

static char mcStack[0x4000] __attribute__((aligned(16)));

static char driverStack[0x8000] __attribute__((aligned(16)));

static char idleStack[0x1800] __attribute__((aligned(16)));

static int driverDone;

static int vsyncs;

static void idle(void)
{
    iosThreadSetPri(0, 0x20);
    for (;;) {
        ico_sched_spin_vsync();
    }
}

/* a request is finished when iosMcSync says so; the driver polls once a
   vsync, as the game's Main does each tick */
static void wait_request(void)
{
    int guard = 0;

    while (iosMcSync(&req) == 0) {
        /* Main's per-vsync wake of the sleeping cdvd thread (main.c) */
        if (IosCdvdMgrSleep != 0) {
            iosThreadWakeup(&bgTh);
        }
        ico_sched_spin_vsync();
        if (++guard > 2000) {
            printf("FAIL the card request never finished (command %d)\n", req.flags.w.command);
            fails++;
            return;
        }
    }
}

/* the product file as the card holds it: 0x1F0 bytes then their byte sum */
static void le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t sum_bytes(const unsigned char *p, int n)
{
    uint32_t s = 0;
    int i;

    for (i = 0; i < n; i++) {
        s += p[i];
    }
    return s;
}

/* the sequence layout_action.c's save runs (iosMcGetBlockSaveInfo, the icon
   block, the product block, the game block) */
static void do_save_on(int port, int fileNo, void *buf)
{
    req.port = port;
    req.slot = 0;
    strcpy(req.path, "game.");
    req.fileNo = fileNo;
    iosMcGetBlockSaveInfo(&req);
    wait_request();
    iosMcSaveIconBlock(&req);
    wait_request();
    CHECK(req.result == 0);
    iosMcSaveProductBlock(&req);
    wait_request();
    CHECK(req.result == 0);
    iosMcSaveGameBlock(&req, buf);
    wait_request();
    CHECK(req.result == 0);
}

static void do_save(int fileNo, void *buf)
{
    do_save_on(0, fileNo, buf);
}

static void driver(void *arg)
{
    static unsigned char buf[25588];
    static unsigned char back[25588];
    static unsigned char file[26000];
    static unsigned char again[26000];
    static unsigned char want[26000];
    int i;
    int n;

    (void)arg;
    CHECK(sizeof(McProductFile) == 0x1F0);
    rm_card(CARD_B);
    ico_mc_host_set_root(CARD_B);

    /* the boot card check (kanbanBoot.c, steps 2-4): a formatted card with
       no save directory is result -14 with type 2, formatted, space; the
       screens show */
    req.port = 0;
    req.slot = 0;
    req.flags.ll &= ~2;
    iosMcChdirProduct(&req);
    wait_request();
    CHECK(req.result == -14);
    CHECK(req.type == 2 && req.format == 1 && req.free >= 360);
    {
        struct stat st;
        CHECK(stat(CARD_B, &st) != 0); /* looking did not create it */
    }
    /* the load menu: no files */
    strcpy(req.path, "game.");
    iosMcGetBlockSaveInfo(&req);
    wait_request();
    CHECK(req.result == 0 && req.dirCount == 0 && req.mask == 0);

    /* save to slot 1 with an all-zero game: the product file and the game
       block are then all zeros but for their layout */
    memset(buf, 0, sizeof(buf));
    do_save(1, buf);
    CHECK(file_size(CARD_B "/" PRODUCT "/icon.sys") == 964);
    CHECK(file_size(CARD_B "/" PRODUCT "/boy_blk.ico") == 3000);
    n = slurp(CARD_B "/" PRODUCT "/" PRODUCT, file, sizeof(file));
    CHECK(n == 0x1F0 + 4);
    memset(want, 0, sizeof(want));
    CHECK(n == 500 && memcmp(file, want, 500) == 0);
    n = slurp(CARD_B "/" PRODUCT "/game.001", file, sizeof(file));
    CHECK(n == 25588 + 4 + 4 + 4); /* the block, the two option words, the sum */
    CHECK(n == 25600 && memcmp(file, want, 25600) == 0);
    /* the icon files are the disc's bytes as mcdata.c's background read
       hands them to the handler (a 2048-byte rounded read, the file's size
       written), and have no sum */
    n = slurp(CARD_B "/" PRODUCT "/icon.sys", file, sizeof(file));
    CHECK(n == 964);
    for (i = 0; i < n; i++) {
        if (file[i] != icon_byte("icon.sys", i)) {
            printf("FAIL icon.sys byte %d is %u, not %u\n", i, file[i], icon_byte("icon.sys", i));
            fails++;
            break;
        }
    }
    n = slurp(CARD_B "/" PRODUCT "/boy_blk.ico", file, sizeof(file));
    CHECK(n == 3000);
    for (i = 0; i < n; i++) {
        if (file[i] != icon_byte("boy_blk.ico", i)) {
            printf("FAIL boy_blk.ico byte %d is %u, not %u\n", i, file[i],
                   icon_byte("boy_blk.ico", i));
            fails++;
            break;
        }
    }
    CHECK(bgReads >= 2 && bgMisaligned == 0);
    CHECK(bgReq[0].name[0] == 0 && bgReq[1].name[0] == 0); /* closed */

    /* the card now has the save: the load menu sees slot 1 */
    strcpy(req.path, "game.");
    iosMcGetBlockSaveInfo(&req);
    wait_request();
    CHECK(req.result == 1 && req.dirCount == 1 && req.mask == (1 << 1)); /* GetDir's count */

    /* a game with everything set: the product file's fields land at their
       offsets, the game block is the buffer then the two option words */
    systemStatus[11] = 2;
    soundOutputMode = 1;
    iosPadActRequestEnable = 1;
    optionControlType = 3;
    NonLinearCameraMove = 4;
    systemStatus[0] = 1;
    optionScreenMode = 0x01020304;
    girlControlMode = 0x0A0B0C0D;
    for (i = 0; i < 16; i++) {
        iosPadConfCustom.bit[i] = 0x100 + i;
    }
    memset(&IosMcProductFile[0], 0, sizeof(IosMcProductFile[0]));
    IosMcProductFile[0].file[0].stage = 7;
    IosMcProductFile[0].file[0].cleared = 1;
    IosMcProductFile[0].file[0].playTime = 1234;
    IosMcProductFile[0].file[0].sofa = 5;
    IosMcProductFile[0].file[19].stage = 0xFFFFFFFFu;
    IosMcProductFile[0].fileNo = 1;
    IosMcProductFile[0].serial = 0xCAFE;
    for (i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = (unsigned char)(i * 7 + 3);
    }
    do_save(1, buf);

    memset(want, 0, sizeof(want));
    le32(want + 0x00, 7);
    le32(want + 0x04, 1);
    le32(want + 0x08, 1234);
    le32(want + 0x0C, 5);
    le32(want + 19 * 20, 0xFFFFFFFFu);
    le32(want + 0x190, 2);
    le32(want + 0x194, 1);
    le32(want + 0x198, 1);
    le32(want + 0x19C, 3);
    for (i = 0; i < 16; i++) {
        le32(want + 0x1A0 + 4 * i, 0x100 + i);
    }
    le32(want + 0x1E0, 1);
    le32(want + 0x1E4, 0xCAFE);
    le32(want + 0x1E8, 4);
    le32(want + 0x1EC, 1);
    le32(want + 0x1F0, sum_bytes(want, 0x1F0));
    n = slurp(CARD_B "/" PRODUCT "/" PRODUCT, file, sizeof(file));
    CHECK(n == 500 && memcmp(file, want, 500) == 0);

    memset(want, 0, sizeof(want));
    memcpy(want, buf, 25588);
    le32(want + 25588, 0x01020304);
    le32(want + 25592, 0x0A0B0C0D);
    le32(want + 25596, sum_bytes(want, 25596));
    n = slurp(CARD_B "/" PRODUCT "/game.001", file, sizeof(file));
    CHECK(n == 25600 && memcmp(file, want, 25600) == 0);
    memcpy(again, file, 25600);

    /* reload on a fresh run: the product file's options and the game block
       come back, the checksums agree */
    systemStatus[11] = 0;
    soundOutputMode = 0;
    iosPadActRequestEnable = 0;
    optionControlType = 0;
    optionScreenMode = 0;
    girlControlMode = 0;
    memset(&iosPadConfCustom, 0, sizeof(iosPadConfCustom));
    memset(&IosMcProductFile[0], 0, sizeof(IosMcProductFile[0]));
    memset(back, 0, sizeof(back));
    req.port = 0;
    req.flags.ll &= ~2;
    iosMcChdirProduct(&req); /* the boot check: the save directory exists now */
    wait_request();
    CHECK(req.result == 0);
    iosMcLoadProductBlock(&req); /* kanbanBoot step 95 */
    wait_request();
    CHECK(req.result == 0);
    CHECK(IosMcProductFile[0].file[0].stage == 7 && IosMcProductFile[0].file[0].playTime == 1234);
    CHECK(IosMcProductFile[0].serial == 0xCAFE && IosMcProductFile[0].palMode == 1);
    CHECK(IosMcProductFile[0].cameraMove == 4);
    req.fileNo = 1;
    iosMcLoadGameBlock(&req, back);
    wait_request();
    CHECK(req.result == 0);
    CHECK(memcmp(back, buf, sizeof(buf)) == 0);
    CHECK(systemStatus[11] == 2 && soundOutputMode == 1 && iosPadActRequestEnable == 1);
    CHECK(optionControlType == 3 && optionScreenMode == 0x01020304);
    CHECK(girlControlMode == 0x0A0B0C0D && iosPadConfCustom.bit[15] == 0x10F);

    /* re-save the loaded state: the same files, byte for byte */
    do_save(1, back);
    n = slurp(CARD_B "/" PRODUCT "/game.001", file, sizeof(file));
    CHECK(n == 25600 && memcmp(file, again, 25600) == 0);
    n = slurp(CARD_B "/" PRODUCT "/" PRODUCT, file, sizeof(file));
    CHECK(n == 500);

    /* a flipped byte fails the checksum: -16 */
    {
        FILE *f = fopen(CARD_B "/" PRODUCT "/game.001", "r+b");
        CHECK(f != NULL);
        if (f != NULL) {
            fseek(f, 100, SEEK_SET);
            fputc(again[100] ^ 0xFF, f);
            fclose(f);
        }
    }
    req.fileNo = 1;
    iosMcLoadGameBlock(&req, back);
    wait_request();
    CHECK(req.result == -16);
    /* a slot with no file: -4 (the load menu checks the mask first) */
    req.fileNo = 2;
    iosMcLoadGameBlock(&req, back);
    wait_request();
    CHECK(req.result == -4);

    /* delete (layout_action.c's delete: the entry's name from the last
       listing) */
    strcpy(req.path, "game.");
    iosMcGetBlockSaveInfo(&req);
    wait_request();
    CHECK(req.dirCount == 1 && req.mask == (1 << 1));
    req.fileNo = 0;
    strcpy(req.path, (char *)req.dir[0].EntryName);
    CHECK(strcmp(req.path, "game.001") == 0);
    iosMcDelete(&req);
    wait_request();
    CHECK(req.result == 0);
    strcpy(req.path, "game.");
    iosMcGetBlockSaveInfo(&req);
    wait_request();
    CHECK(req.result == 0 && req.dirCount == 0 && req.mask == 0);
    CHECK(file_size(CARD_B "/" PRODUCT "/game.001") < 0);

    /* Saves as they reach a phone (Android, issue 20).
       A save folder copied or renamed with other capitals: on a file
       system that tells case apart (Linux; Android's app folder) it is
       still found (mc_host.c fold_case), and the load reads it whole. */
    memset(buf, 0, sizeof(buf));
    for (i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = (unsigned char)(i * 13 + 1);
    }
    do_save(3, buf);
    CHECK(rename(CARD_B "/" PRODUCT, CARD_B "/besces-50760ICO") == 0);
    {
        struct stat st;
        const int folds = stat(CARD_B "/" PRODUCT, &st) != 0; /* the system tells case apart */

        req.port = 0;
        req.flags.ll &= ~2;
        iosMcChdirProduct(&req);
        wait_request();
        CHECK(req.result == 0);
        strcpy(req.path, "game.");
        iosMcGetBlockSaveInfo(&req);
        wait_request();
        CHECK(req.dirCount == 1 && req.mask == (1 << 3));
        req.fileNo = 3;
        memset(back, 0, sizeof(back));
        iosMcLoadGameBlock(&req, back);
        wait_request();
        CHECK(req.result == 0 && memcmp(back, buf, sizeof(buf)) == 0);
        /* a re-save goes into that folder, not a second one */
        do_save(3, back);
        CHECK(file_size(CARD_B "/besces-50760ICO/game.003") == 25600);
        if (folds) {
            CHECK(stat(CARD_B "/" PRODUCT, &st) != 0);
        }
        printf("part B: a save folder in other capitals loads (%s)\n",
               folds ? "the file system tells case apart" : "the file system ignores case");
    }
    CHECK(rename(CARD_B "/besces-50760ICO", CARD_B "/" PRODUCT) == 0);

    /* An empty product file (a copy cut short): the card check reads it as
       no save (-15, which _la_memory_card_check turns into -14), and the
       load of a game file of 0 bytes fails with -15, not a crash */
    {
        FILE *f = fopen(CARD_B "/" PRODUCT "/" PRODUCT, "wb");

        CHECK(f != NULL);
        if (f != NULL) {
            fclose(f);
        }
        iosMcLoadProductBlock(&req);
        wait_request();
        CHECK(req.result == -15);
        f = fopen(CARD_B "/" PRODUCT "/game.003", "wb");
        CHECK(f != NULL);
        if (f != NULL) {
            fclose(f);
        }
        req.fileNo = 3;
        iosMcLoadGameBlock(&req, back);
        wait_request();
        CHECK(req.result == -15);
    }
#ifndef _WIN32
    /* A card folder that cannot be written (a copy with no write right):
       the load works, a save fails with a card error the save screen shows
       (sceMcResDeniedPermit, -5 here) and the files stay as they were.
       root writes anyway: then only the load is checked. */
    do_save(3, buf);
    CHECK(chmod(CARD_B "/" PRODUCT, 0555) == 0);
    {
        const int asRoot = geteuid() == 0;

        req.fileNo = 3;
        memset(back, 0, sizeof(back));
        iosMcLoadGameBlock(&req, back);
        wait_request();
        CHECK(req.result == 0 && memcmp(back, buf, sizeof(buf)) == 0);
        if (!asRoot) {
            req.port = 0;
            strcpy(req.path, "game.");
            req.fileNo = 3;
            iosMcSaveIconBlock(&req);
            wait_request();
            CHECK(req.result < 0);
            iosMcSaveGameBlock(&req, back);
            wait_request();
            CHECK(req.result < 0);
            n = slurp(CARD_B "/" PRODUCT "/game.003", file, sizeof(file));
            CHECK(n == 25600);
        }
        printf("part B: a card folder that cannot be written: the load works%s\n",
               asRoot ? " (root: the refused save is not checked)" : ", the save is refused");
    }
    CHECK(chmod(CARD_B "/" PRODUCT, 0755) == 0);
#endif
    remove(CARD_B "/" PRODUCT "/game.003");

    /* port 1 is empty: the game's "no card" path (type 0, result -9) */
    req.port = 1;
    iosMcChdirProduct(&req);
    wait_request();
    CHECK(req.result == -9 && req.type == 0);

    /* a second card (saves2): port 1 answers as a card of its own. The
       boot check finds it formatted with no save (-14, type 2), a save to
       it lands in its folder only, and each port's listing sees its own
       slots, as layout_action.c lists the two cards (mcPortInfo[0], [1]) */
    rm_card(CARD_C);
    ico_mc_host_set_port_root(1, CARD_C);
    req.port = 1;
    req.flags.ll &= ~2;
    iosMcChdirProduct(&req);
    wait_request();
    CHECK(req.result == -14 && req.type == 2 && req.format == 1 && req.free >= 360);
    memset(buf, 0, sizeof(buf));
    do_save_on(1, 4, buf);
    CHECK(file_size(CARD_C "/" PRODUCT "/game.004") == 25600);
    CHECK(file_size(CARD_C "/" PRODUCT "/" PRODUCT) == 500);
    CHECK(file_size(CARD_B "/" PRODUCT "/game.004") < 0);
    req.port = 1;
    strcpy(req.path, "game.");
    iosMcGetBlockSaveInfo(&req);
    wait_request();
    CHECK(req.result == 1 && req.dirCount == 1 && req.mask == (1 << 4));
    req.port = 0;
    strcpy(req.path, "game.");
    iosMcGetBlockSaveInfo(&req);
    wait_request();
    CHECK(req.result == 0 && req.dirCount == 0 && req.mask == 0);
    req.port = 1;
    req.fileNo = 4;
    iosMcLoadGameBlock(&req, back);
    wait_request();
    CHECK(req.result == 0 && memcmp(back, buf, sizeof(buf)) == 0);
    ico_mc_host_set_port_root(1, NULL);
    {
        char path[64];

        snprintf(path, sizeof(path), CARD_C "/" PRODUCT "/game.004");
        remove(path);
    }
    rm_card(CARD_C);
    req.port = 0;

    rm_card(CARD_B);
    driverDone = 1;
    for (;;) {
        ico_sched_spin_vsync();
    }
}

static void boot(void *arg)
{
    (void)arg;
    ChangeThreadPriority(GetThreadId(), 14);
    iosThreadInit();
    ios_partition_root =
        iosMallocInitPartition(ico_arena_ee_addr(0x760000), ico_arena_ee_addr(0x1FEFFF0));
    ios_partition_event = iosMallocSetPartition(ios_partition_root, 262144, 16);
    iosMsgInit();
    iosThreadCreate(&idleTh, 1, idle, 0, idleStack, sizeof idleStack, 0x1B);
    iosThreadStart(&idleTh);
    /* above the driver, which busy-waits (spins) between its polls as Main
       does, so the woken reader runs within the vsync */
    iosThreadCreate(&bgTh, 1, bg_manager, 0, bgStack, sizeof bgStack, 0x1A);
    iosThreadStart(&bgTh);
    /* the game's priorities: the manager 27, the callers lower */
    iosThreadCreate(&mcTh, 1, iosMcManager, 0, mcStack, sizeof mcStack, 27);
    iosThreadStart(&mcTh);
    iosThreadCreate(&driverTh, 1, driver, 0, driverStack, sizeof driverStack, 0x1C);
    iosThreadStart(&driverTh);
    iosThreadSleep();
}

static void vsync(int field)
{
    ico_sched_vsync_advance();
    if (field) {
        *GS_CSR |= 1ull << 13;
    } else {
        *GS_CSR &= ~(1ull << 13);
    }
    ico_kernel_raise_intc(2);
    ico_host_run_vsync_hooks();
    CHECK(ico_sched_run() == ICO_SCHED_SPINNING);
    vsyncs++;
}

static void test_mcard(void)
{
    CHECK(ico_arena_init() == 0);
    ico_sched_reset();
    ico_kernel_reset();
    ico_sched_boot(boot, NULL, 1);
    CHECK(ico_sched_run() == ICO_SCHED_SPINNING);
    while (!driverDone && vsyncs < 20000) {
        vsync(vsyncs & 1);
    }
    CHECK(driverDone);
    printf("part B: %d vsyncs\n", vsyncs);
    ico_sched_reset();
}

/* --- part C: the importer and the second card --------------------------------------
 *
 * A synthetic 8 MB card laid out as a formatted PS2 card is: the superblock
 * in cluster 0, the indirect FAT cluster 8 (ifc_list[0]), FAT clusters 9-40,
 * the allocatable clusters from 41, the root directory at allocatable
 * cluster 0. Chains take every other cluster, so no file is contiguous, and
 * the game's directory sits past FAT entry 256 (the second FAT cluster). */

#define IMG_CLUSTERS 8192
#define IMG_ALLOC 41
#define IMG_SIZE (IMG_CLUSTERS * 1024)
#define IMG_ECC_SIZE (IMG_CLUSTERS * 2 * 528)
#define OTHER "BASLUS-99999OTHER"

typedef struct {
    const char *name;
    uint32_t len;
} TFile;

/* the game's directory: the shapes mcard.c writes, and an empty file */
static const TFile tfiles[] = {{"icon.sys", 964},   {"boy_blk.ico", 3000}, {PRODUCT, 500},
                               {"game.000", 25600}, {"game.003", 25600},   {"empty", 0}};

#define NTF ((int)(sizeof(tfiles) / sizeof(tfiles[0])))

static unsigned char *img;

static uint32_t imgNext;

static unsigned char tbyte(int f, uint32_t k)
{
    return (unsigned char)(k * 13u + (uint32_t)f * 71u + (k >> 9));
}

static void tfill(int f, unsigned char *out)
{
    uint32_t k;

    for (k = 0; k < tfiles[f].len; k++) {
        out[k] = tbyte(f, k);
    }
}

static void le16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void img_fat(uint32_t n, uint32_t v)
{
    le32(img + (size_t)(9 + n / 256) * 1024 + (n % 256) * 4, v);
}

/* len bytes (at least one cluster) into a chain from imgNext; its first
   allocatable cluster */
static uint32_t img_alloc(const unsigned char *data, uint32_t len)
{
    uint32_t n = len == 0 ? 1 : (len + 1023) / 1024;
    uint32_t first = imgNext;
    uint32_t prev = 0;
    uint32_t k;

    for (k = 0; k < n; k++) {
        uint32_t c = imgNext;
        uint32_t take = len - k * 1024 < 1024 ? len - k * 1024 : 1024;

        imgNext += 2;
        if (len > 0) {
            memcpy(img + (size_t)(IMG_ALLOC + c) * 1024, data + k * 1024, take);
        }
        if (k > 0) {
            img_fat(prev, 0x80000000u | c);
        }
        prev = c;
    }
    img_fat(prev, 0xFFFFFFFFu);
    return first;
}

static void dirent(unsigned char *e, unsigned mode, uint32_t len, uint32_t cluster,
                   const char *name)
{
    memset(e, 0, 512);
    le16(e, mode);
    le32(e + 4, len);
    le32(e + 0x10, cluster);
    memcpy(e + 0x40, name, strlen(name));
}

static void build_image(void)
{
    static unsigned char file[26000];
    static unsigned char dir[16 * 512];
    static const char version[] = "1.2.0.0";
    uint32_t cl[NTF];
    uint32_t other;
    uint32_t ico;
    uint32_t i;
    int f;

    memset(img, 0xFF, IMG_SIZE);
    memset(img, 0, 1024);
    memcpy(img, "Sony PS2 Memory Card Format ", 28);
    memcpy(img + 0x1C, version, sizeof(version));
    le16(img + 0x28, 512);
    le16(img + 0x2A, 2);
    le16(img + 0x2C, 16);
    le16(img + 0x2E, 0xFF00);
    le32(img + 0x30, IMG_CLUSTERS);
    le32(img + 0x34, IMG_ALLOC);
    le32(img + 0x38, 8135);
    le32(img + 0x3C, 0);
    le32(img + 0x40, 1023);
    le32(img + 0x44, 1022);
    le32(img + 0x50, 8);
    img[0x150] = 2;
    img[0x151] = 0x52;
    for (i = 0; i < 256; i++) {
        le32(img + 8 * 1024 + i * 4, i < 32 ? 9 + i : 0xFFFFFFFFu);
    }
    for (i = 0; i < 32 * 256; i++) {
        img_fat(i, 0x7FFFFFFFu);
    }
    /* another game's save (the root takes clusters 0, 2, 4) */
    imgNext = 6;
    memset(file, 0x5A, 700);
    dirent(dir, 0x8427, 3, 0, ".");
    dirent(dir + 512, 0x8427, 0, 0, "..");
    dirent(dir + 1024, 0x8497, 700, img_alloc(file, 700), "data.bin");
    other = img_alloc(dir, 3 * 512);
    /* ICO's, past FAT entry 256: the files, then the directory with a
       deleted entry among them */
    imgNext = 301;
    for (f = 0; f < NTF; f++) {
        tfill(f, file);
        cl[f] = tfiles[f].len > 0 ? img_alloc(file, tfiles[f].len) : 0xFFFFFFFFu;
    }
    dirent(dir, 0x8427, NTF + 3, 0, ".");
    dirent(dir + 512, 0x8427, 0, 0, "..");
    dirent(dir + 1024, 0x0497, 25600, cl[3], "game.001"); /* deleted */
    for (f = 0; f < NTF; f++) {
        dirent(dir + (3 + f) * 512, 0x8497, tfiles[f].len, cl[f], tfiles[f].name);
    }
    ico = img_alloc(dir, (NTF + 3) * 512);
    /* the root: ".", "..", the other game, a deleted ICO entry, ICO's */
    dirent(dir, 0x8427, 5, 0, ".");
    dirent(dir + 512, 0x8427, 0, 0, "..");
    dirent(dir + 1024, 0x8427, 3, other, OTHER);
    dirent(dir + 1536, 0x0427, 3, other, PRODUCT);
    dirent(dir + 2048, 0x8427, NTF + 3, ico, PRODUCT);
    imgNext = 0;
    CHECK(img_alloc(dir, 5 * 512) == 0);
}

static int write_blob(const char *path, const unsigned char *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (f == NULL) {
        return -1;
    }
    ok = fwrite(p, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    return ok ? 0 : -1;
}

/* the image with a 16-byte spare after every 512-byte page, as PCSX2 and
   mymc keep it (the ECC itself is not checked) */
static int write_ecc(const char *path)
{
    FILE *f = fopen(path, "wb");
    unsigned char spare[16];
    uint32_t p;
    int ok = 1;

    if (f == NULL) {
        return -1;
    }
    memset(spare, 0xFF, sizeof(spare));
    for (p = 0; p < IMG_CLUSTERS * 2; p++) {
        ok = ok && fwrite(img + (size_t)p * 512, 1, 512, f) == 512 && fwrite(spare, 1, 16, f) == 16;
    }
    ok = fclose(f) == 0 && ok;
    return ok ? 0 : -1;
}

/* a .psu: the directory's entry, "." and "..", each file's entry and its
   data padded to 1024 bytes */
static int write_psu(const char *path, const char *dirname)
{
    static unsigned char file[26000];
    unsigned char e[512];
    unsigned char pad[1024];
    FILE *fp = fopen(path, "wb");
    int ok = 1;
    int f;

    if (fp == NULL) {
        return -1;
    }
    memset(pad, 0, sizeof(pad));
    dirent(e, 0x8427, NTF + 2, 0, dirname);
    ok = ok && fwrite(e, 1, 512, fp) == 512;
    dirent(e, 0x8427, 0, 0, ".");
    ok = ok && fwrite(e, 1, 512, fp) == 512;
    dirent(e, 0x8427, 0, 0, "..");
    ok = ok && fwrite(e, 1, 512, fp) == 512;
    for (f = 0; f < NTF; f++) {
        uint32_t padded = (tfiles[f].len + 1023) / 1024 * 1024;

        tfill(f, file);
        dirent(e, 0x8497, tfiles[f].len, 0, tfiles[f].name);
        ok = ok && fwrite(e, 1, 512, fp) == 512;
        ok = ok && fwrite(file, 1, tfiles[f].len, fp) == tfiles[f].len;
        ok = ok && fwrite(pad, 1, padded - tfiles[f].len, fp) == padded - tfiles[f].len;
    }
    ok = fclose(fp) == 0 && ok;
    return ok ? 0 : -1;
}

static void rm_import(const char *card)
{
    char path[256];

    snprintf(path, sizeof(path), "%s/" PRODUCT "/empty", card);
    remove(path);
    rm_card(card);
}

/* every file of the game's directory, on disk, is the synthetic bytes */
static void check_folder(const char *card)
{
    static unsigned char want[26000];
    static unsigned char got[26100];
    char path[256];
    int f;

    for (f = 0; f < NTF; f++) {
        int n;

        snprintf(path, sizeof(path), "%s/" PRODUCT "/%s", card, tfiles[f].name);
        tfill(f, want);
        n = slurp(path, got, sizeof(got));
        if (n != (int)tfiles[f].len || memcmp(got, want, tfiles[f].len) != 0) {
            printf("FAIL %s: %d bytes, want %u, or other bytes\n", path, n,
                   (unsigned)tfiles[f].len);
            fails++;
        }
    }
}

/* every file read back through the folder card on port, byte for byte */
static void check_card(int port)
{
    static unsigned char want[26000];
    static unsigned char got[26100];
    char path[64];
    int f;

    for (f = 0; f < NTF; f++) {
        int fd;
        int total = 0;
        int n;

        snprintf(path, sizeof(path), "/" PRODUCT "/%s", tfiles[f].name);
        CHECK(sceMcOpen(port, 0, path, SCE_RDONLY) == 0);
        fd = finish(sceMcFuncNoOpen);
        CHECK(fd >= 0);
        if (fd < 0) {
            continue;
        }
        do {
            CHECK(sceMcRead(fd, got + total, 1024) == 0);
            n = finish(sceMcFuncNoRead);
            total += n > 0 ? n : 0;
        } while (n == 1024 && total + 1024 <= (int)sizeof(got));
        CHECK(sceMcClose(fd) == 0);
        CHECK(finish(sceMcFuncNoClose) == 0);
        tfill(f, want);
        if (total != (int)tfiles[f].len || memcmp(got, want, tfiles[f].len) != 0) {
            printf("FAIL port %d %s: %d bytes read back, want %u, or other bytes\n", port, path,
                   total, (unsigned)tfiles[f].len);
            fails++;
        }
    }
}

static int run_cli(const char *exe, const char *args)
{
    char cmd[1024];
    int r;

    snprintf(cmd, sizeof(cmd), "\"%s\" %s", exe, args);
    r = system(cmd);
#ifndef _WIN32
    if (r != -1 && (r & 0x7F) == 0) {
        r = (r >> 8) & 0xFF;
    }
#endif
    return r;
}

static void test_import(const char *cli)
{
    IcoMcImport res;
    sceMcTblGetDir dir[20];
    int type;
    int freeCl;
    int format;
    int i;

    img = malloc(IMG_SIZE);
    CHECK(img != NULL);
    if (img == NULL) {
        return;
    }
    build_image();
    CHECK(write_blob("save_test_card.ps2", img, IMG_SIZE) == 0);
    CHECK(write_ecc("save_test_card_ecc.ps2") == 0);
    CHECK(write_psu("save_test_save.psu", PRODUCT) == 0);
    CHECK(write_psu("save_test_other.psu", OTHER) == 0);
    CHECK(write_blob("save_test_save.max", (const unsigned char *)"Ps2PowerSave\0\0\0\0", 16) == 0);
    free(img);
    img = NULL;
    rm_import(CARD_I);
    rm_import(CARD_J);
    rm_import(CARD_K);

    CHECK(ico_mc_import_detect("save_test_card.ps2") == ICO_MC_FMT_RAW);
    CHECK(ico_mc_import_detect("save_test_card_ecc.ps2") == ICO_MC_FMT_RAW);
    CHECK(ico_mc_import_detect("save_test_save.psu") == ICO_MC_FMT_PSU);
    CHECK(ico_mc_import_detect("save_test_save.max") == ICO_MC_FMT_MAX);

    /* the plain image into card I: the game's six files, the other game's
       directory skipped and named, the deleted entries ignored */
    CHECK(ico_mc_import("save_test_card.ps2", CARD_I, 0, &res) == 0);
    CHECK(res.format == ICO_MC_FMT_RAW && res.files == NTF);
    CHECK(res.skipped == 1 && strcmp(res.skippedName[0], OTHER) == 0);
    check_folder(CARD_I);
    CHECK(file_size(CARD_I "/" PRODUCT "/game.001") < 0);
    /* again: refused without overwrite, nothing changed; then replaced */
    CHECK(ico_mc_import("save_test_card.ps2", CARD_I, 0, &res) == -1);
    CHECK(strstr(res.why, "already holds") != NULL && res.files == 0);
    CHECK(ico_mc_import("save_test_card.ps2", CARD_I, ICO_MC_IMPORT_OVERWRITE, &res) == 0);
    CHECK(res.files == NTF);
    check_folder(CARD_I);

    /* the ECC image into card J */
    CHECK(ico_mc_import("save_test_card_ecc.ps2", CARD_J, 0, &res) == 0);
    CHECK(res.files == NTF && res.skipped == 1);
    check_folder(CARD_J);

    /* the .psu, and the refusals, through the command line */
    if (cli != NULL) {
        CHECK(run_cli(cli, "--to " CARD_K " save_test_save.psu") == 0);
        check_folder(CARD_K);
        CHECK(run_cli(cli, "--to " CARD_K " save_test_save.psu") == 2);
        CHECK(run_cli(cli, "--overwrite --to " CARD_K " save_test_save.psu") == 0);
        check_folder(CARD_K);
        CHECK(run_cli(cli, "--to " CARD_K " save_test_other.psu") == 1);
        CHECK(run_cli(cli, "--to " CARD_K " save_test_save.max") == 2);
        CHECK(run_cli(cli, "save_test_save.psu") == 2); /* no --to */
    } else {
        printf("FAIL no mc_import path given (ctest passes it)\n");
        fails++;
    }
    CHECK(ico_mc_import("save_test_other.psu", CARD_K, 0, &res) == 1);
    CHECK(res.skipped == 1 && strcmp(res.skippedName[0], OTHER) == 0);
    CHECK(ico_mc_import("save_test_save.max", CARD_K, 0, &res) == -1);
    CHECK(res.format == ICO_MC_FMT_MAX);

    /* two cards: card I in port 0, card J in port 1; both formatted PS2
       cards with the same save, read back byte for byte through libmc */
    ico_mc_host_set_port_root(0, CARD_I);
    ico_mc_host_set_port_root(1, CARD_J);
    CHECK(sceMcInit() == 0);
    for (i = 0; i < 2; i++) {
        CHECK(sceMcGetInfo(i, 0, &type, &freeCl, &format) == 0);
        CHECK(finish(sceMcFuncNoCardInfo) == sceMcResChangedCard);
        CHECK(type == 2 && format == 1 && freeCl > 360 && freeCl < ICO_MC_HOST_CLUSTERS);
        check_card(i);
        CHECK(sceMcChdir(i, 0, "/" PRODUCT, NULL) == 0);
        CHECK(finish(sceMcFuncNoChDir) == 0);
        CHECK(sceMcGetDir(i, 0, "game.*", 0, 20, dir) == 0);
        CHECK(finish(sceMcFuncNoGetDir) == 2);
        CHECK(strcmp((char *)dir[0].EntryName, "game.000") == 0);
        CHECK(strcmp((char *)dir[1].EntryName, "game.003") == 0);
    }
    /* a write on port 1 lands in card J only */
    CHECK(sceMcDelete(1, 0, "game.003") == 0);
    CHECK(finish(sceMcFuncNoDelete) == 0);
    CHECK(file_size(CARD_J "/" PRODUCT "/game.003") < 0);
    CHECK(file_size(CARD_I "/" PRODUCT "/game.003") == 25600);
    /* no folder for port 1: no card, as before */
    ico_mc_host_set_port_root(1, NULL);
    CHECK(sceMcGetInfo(1, 0, &type, &freeCl, &format) == 0);
    CHECK(finish(sceMcFuncNoCardInfo) == sceMcResFailDetect2 && type == 0);
    CHECK(strcmp(ico_mc_host_port_root(0), CARD_I) == 0 && ico_mc_host_port_root(1)[0] == '\0');

    rm_import(CARD_I);
    rm_import(CARD_J);
    rm_import(CARD_K);
    remove("save_test_card.ps2");
    remove("save_test_card_ecc.ps2");
    remove("save_test_save.psu");
    remove("save_test_other.psu");
    remove("save_test_save.max");
}

int main(int argc, char **argv)
{
    test_libmc();
    test_import(argc > 1 ? argv[1] : NULL);
    test_mcard();
    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
