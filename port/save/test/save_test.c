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
 *   B. fumi/ios/mcard.c itself, compiled unchanged with the game's options,
 *      on the fiber scheduler: its manager thread, driven through the
 *      iosMc* entry points the way kanbanBoot.c and layout_action.c drive
 *      them, with the bytes it leaves in the card folder checked against a
 *      hand-laid-out expectation.
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
#include "mc_host.h"
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

/* mcdata.c writes the icon files from the disc; this writes a pattern of
   the same size through the same handler */
int iosMcIconWriteIconsys(struct McMgr *self, const IconFile *p)
{
    unsigned char buf[3000];

    memset(buf, p->name[0], sizeof(buf));
    iosMcHandlerWrite(self, buf, p->size);
    return 0;
}

int iosMcIconWriteIcon(struct McMgr *self, const IconFile *p)
{
    return iosMcIconWriteIconsys(self, p);
}

/* --- files --------------------------------------------------------------------- */

#define CARD_A "save_test_card_a"
#define CARD_B "save_test_card_b"
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
    static const char *const files[] = {"icon.sys", "boy_blk.ico", PRODUCT, "game.000",
                                        "game.001", "game.002",    "extra"};
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
static void do_save(int fileNo, void *buf)
{
    req.port = 0;
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
    /* the icon files are the disc's bytes as the handler gets them, and have
       no sum */
    n = slurp(CARD_B "/" PRODUCT "/icon.sys", file, sizeof(file));
    CHECK(n == 964 && file[0] == 'i' && file[963] == 'i');

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

    /* port 1 is empty: the game's "no card" path (type 0, result -9) */
    req.port = 1;
    iosMcChdirProduct(&req);
    wait_request();
    CHECK(req.result == -9 && req.type == 0);
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
    /* the game's priorities: the manager 27, the callers lower */
    iosThreadCreate(&mcTh, 1, iosMcManager, 0, mcStack, sizeof mcStack, 27);
    iosThreadStart(&mcTh);
    iosThreadCreate(&driverTh, 1, (void (*)())driver, 0, driverStack, sizeof driverStack, 0x1C);
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

int main(void)
{
    test_libmc();
    test_mcard();
    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
