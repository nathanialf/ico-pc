/*
 * port/data/test/vfs_test.c
 *
 * Tests of the disc layer (port/data): the VFS and its ISO9660 backend, the
 * libcdvd host layer, the SIF host layer and IOP RAM.
 *
 *   vfs_test synth <dir>    builds a small ISO9660 image in <dir> and runs
 *                           everything against it (no disc needed), then
 *                           the same image wrapped in .chd files it writes
 *                           (a DVD CHD and a CD CHD, plus refused ones)
 *   vfs_test disc <iso>     checks the user's PAL disc image at run time;
 *                           exits 77 (skipped) when the image is absent
 *
 * No byte of the disc is reproduced here: the disc test only compares
 * hashes and structure.
 */
#include "cdvd_host.h"
#include "iop_ram.h"
#include "sif_host.h"
#include "vfs.h"

#include "sha1.h"

#include <libcdvd.h>
#include <sifrpc.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIP 77

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* sceCdlFILE as libcdvd fills it (0x24 bytes), with room to spare */
typedef struct sceCdlFILE {
    uint32_t lsn;
    uint32_t size;
    char name[16];
    uint8_t date[8];
    uint32_t flag;
    uint32_t guard[3];
} sceCdlFILE;

/* sceCdCLOCK */
typedef struct sceCdCLOCK {
    uint8_t stat, second, minute, hour, pad, day, month, year;
} sceCdCLOCK;

/* The host loop's vsync callback list (port/platform/host_loop.c, package
   1B) is not linked into this test: this stand-in records what sceCdInit
   registers, and vsync() runs it as one simulated vsync would. */
static void (*vsyncFn)(void *);
static void *vsyncUser;
static int vsyncRegistrations;

int ico_host_on_vsync_register(void (*fn)(void *user), void *user)
{
    vsyncFn = fn;
    vsyncUser = user;
    vsyncRegistrations++;
    return 0;
}

void ico_host_on_vsync_unregister(void (*fn)(void *user), void *user)
{
    if (vsyncFn == fn && vsyncUser == user) {
        vsyncFn = NULL;
        vsyncUser = NULL;
    }
}

static void vsync(void)
{
    if (vsyncFn != NULL) {
        vsyncFn(vsyncUser);
    }
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* --- the synthetic image ----------------------------------------------------- */

#define SYN_SECTORS 40
#define SYN_ROOT 18
#define SYN_DFDATAS 19
#define SYN_MANY 20 /* two sectors */
#define SYN_CNF 29
#define SYN_BOOT 30 /* two sectors */
#define SYN_DATADF 32
#define SYN_SHARED 39
#define SYN_MANY_FILES 60
#define SYN_BOOT_SIZE 3000
#define SYN_DATADF_SIZE 12289

static const char synCnf[] = "BOOT2 = cdrom0:\\SLUS_000.00;1\r\nVER = 1.00\r\nVMODE = PAL\r\n";

static unsigned char synthByte(uint32_t lsn, uint32_t off)
{
    return (unsigned char)(lsn * 7u + off * 13u + (off >> 8));
}

static void put32both(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
    p[4] = (unsigned char)(v >> 24);
    p[5] = (unsigned char)(v >> 16);
    p[6] = (unsigned char)(v >> 8);
    p[7] = (unsigned char)v;
}

static void put32le(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* one directory record; returns its length */
static unsigned put_rec(unsigned char *p, uint32_t lsn, uint32_t size, int dir, const char *name,
                        unsigned nlen)
{
    unsigned len = 33 + nlen + ((33 + nlen) & 1);

    memset(p, 0, len);
    p[0] = (unsigned char)len;
    put32both(p + 2, lsn);
    put32both(p + 10, size);
    p[18] = 101; /* 2001-09-24 12:34:56 */
    p[19] = 9;
    p[20] = 24;
    p[21] = 12;
    p[22] = 34;
    p[23] = 56;
    p[25] = dir ? 2 : 0;
    p[28] = 1;
    p[31] = 1;
    p[32] = (unsigned char)nlen;
    memcpy(p + 33, name, nlen);
    return len;
}

static unsigned put_named(unsigned char *p, uint32_t lsn, uint32_t size, int dir, const char *name)
{
    return put_rec(p, lsn, size, dir, name, (unsigned)strlen(name));
}

static int build_synthetic(const char *path)
{
    static unsigned char img[SYN_SECTORS * 2048];
    unsigned char *s;
    unsigned o;
    uint32_t i;
    uint32_t manySize = 0;
    FILE *fp;

    memset(img, 0, sizeof(img));

    /* MANY: 60 records over two sectors, the length recorded unaligned */
    s = img + SYN_MANY * 2048;
    o = 0;
    o += put_rec(s + o, SYN_MANY, 0, 1, "\0", 1);
    o += put_rec(s + o, SYN_ROOT, 0, 1, "\1", 1);
    for (i = 0; i < SYN_MANY_FILES; i++) {
        char name[16];
        unsigned len;

        snprintf(name, sizeof(name), "F%02u.BIN;1", (unsigned)i);
        len = 33 + (unsigned)strlen(name) + ((33 + (unsigned)strlen(name)) & 1);
        if (o % 2048 + len > 2048) {
            o = (o / 2048 + 1) * 2048; /* records never cross a sector */
        }
        o += put_named(s + o, SYN_SHARED, 10 + i, 0, name);
    }
    manySize = o;
    /* the self record carries the directory's length */
    put32both(s + 10, manySize);

    /* the root */
    s = img + SYN_ROOT * 2048;
    o = 0;
    o += put_rec(s + o, SYN_ROOT, 0, 1, "\0", 1);
    o += put_rec(s + o, SYN_ROOT, 0, 1, "\1", 1);
    o += put_named(s + o, SYN_DFDATAS, 0, 1, "DFDATAS");
    o += put_named(s + o, SYN_MANY, manySize, 1, "MANY");
    o += put_named(s + o, SYN_BOOT, SYN_BOOT_SIZE, 0, "SLUS_000.00;1");
    o += put_named(s + o, SYN_CNF, (uint32_t)strlen(synCnf), 0, "SYSTEM.CNF;1");
    {
        uint32_t rootSize = o;
        unsigned char *dfd = img + SYN_DFDATAS * 2048;
        unsigned d = 0;

        put32both(s + 10, rootSize);
        put32both(s + 34 + 10, rootSize);
        d += put_rec(dfd + d, SYN_DFDATAS, 0, 1, "\0", 1);
        d += put_rec(dfd + d, SYN_ROOT, rootSize, 1, "\1", 1);
        d += put_named(dfd + d, SYN_DATADF, SYN_DATADF_SIZE, 0, "DATA.DF;1");
        put32both(dfd + 10, d);
        /* DFDATAS's length in the root record */
        put32both(s + 34 + 34 + 10, d);

        /* the primary volume descriptor and the terminator */
        s = img + 16 * 2048;
        s[0] = 1;
        memcpy(s + 1, "CD001", 5);
        s[6] = 1;
        put32both(s + 80, SYN_SECTORS);
        s[120] = 1;
        s[123] = 1;
        s[124] = 1;
        s[127] = 1;
        s[128] = 0x00;
        s[129] = 0x08;
        s[130] = 0x08;
        s[131] = 0x00;
        put_rec(s + 156, SYN_ROOT, rootSize, 1, "\0", 1);
    }
    s = img + 17 * 2048;
    s[0] = 255;
    memcpy(s + 1, "CD001", 5);
    s[6] = 1;

    memcpy(img + SYN_CNF * 2048, synCnf, strlen(synCnf));
    for (i = 0; i < SYN_BOOT_SIZE; i++) {
        img[SYN_BOOT * 2048 + i] = synthByte(SYN_BOOT, i);
    }
    /* DATA.DF: a unifile directory {count, {name[32], offset, size}...}
       and three members at sector-aligned offsets */
    s = img + SYN_DATADF * 2048;
    put32le(s, 3);
    strcpy((char *)s + 4, "alpha.pak");
    put32le(s + 4 + 32, 2048);
    put32le(s + 4 + 36, 100);
    strcpy((char *)s + 44, "beta.int");
    put32le(s + 44 + 32, 4096);
    put32le(s + 44 + 36, 5000);
    strcpy((char *)s + 84, "gamma.smb");
    put32le(s + 84 + 32, 12288);
    put32le(s + 84 + 36, 1);
    for (i = 2048; i < SYN_DATADF_SIZE; i++) {
        s[i] = synthByte(SYN_DATADF, i);
    }
    for (i = 0; i < 2048; i++) {
        img[SYN_SHARED * 2048 + i] = synthByte(SYN_SHARED, i);
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    if (fwrite(img, 1, sizeof(img), fp) != sizeof(img)) {
        fclose(fp);
        return -1;
    }
    return fclose(fp);
}

static void test_normalize(void)
{
    char out[64];

    CHECK(ico_vfs_normalize("cdrom0:\\IOPRP224.IMG;1", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "IOPRP224.IMG") == 0);
    CHECK(ico_vfs_normalize("\\DFDATAS\\DATA.DF;1", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "DFDATAS/DATA.DF") == 0);
    CHECK(ico_vfs_normalize("dfdatas/data.df", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "DFDATAS/DATA.DF") == 0);
    CHECK(ico_vfs_normalize("//a\\\\b/", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "A/B") == 0);
    CHECK(ico_vfs_normalize("SCES_507.60", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "SCES_507.60") == 0);
    CHECK(ico_vfs_normalize("", out, sizeof(out)) == 0 && out[0] == '\0');
    CHECK(ico_vfs_normalize("ABCDEFGH", out, 4) == -1);
    CHECK(ico_vfs_lsn_to_offset(3) == 6144);
    CHECK(ico_vfs_offset_to_lsn(6143) == 2 && ico_vfs_offset_to_lsn(6144) == 3);
    CHECK(ico_vfs_size_to_sectors(0) == 0 && ico_vfs_size_to_sectors(1) == 1);
    CHECK(ico_vfs_size_to_sectors(2048) == 1 && ico_vfs_size_to_sectors(2049) == 2);
}

static void test_vfs_synthetic(IcoVfs *vfs)
{
    IcoVfsEntry e;
    IcoVfsFile f;
    unsigned char buf[8192];
    uint32_t i;
    int ok;

    CHECK(ico_vfs_volume_sectors(vfs) == SYN_SECTORS);
    CHECK(ico_vfs_stat(vfs, "SYSTEM.CNF", &e) == 0);
    CHECK(e.lsn == SYN_CNF && e.size == strlen(synCnf) && !e.is_dir);
    CHECK(strcmp(e.name, "SYSTEM.CNF;1") == 0);
    CHECK(e.date[0] == 101 && e.date[1] == 9 && e.date[5] == 56);
    CHECK(ico_vfs_stat(vfs, "cdrom0:\\system.cnf;1", &e) == 0 && e.lsn == SYN_CNF);
    CHECK(ico_vfs_stat(vfs, "\\DFDATAS\\DATA.DF;1", &e) == 0);
    CHECK(e.lsn == SYN_DATADF && e.size == SYN_DATADF_SIZE);
    CHECK(ico_vfs_stat(vfs, "DFDATAS", &e) == 0 && e.is_dir);
    CHECK(ico_vfs_stat(vfs, "", &e) == 0 && e.is_dir && e.lsn == SYN_ROOT);
    CHECK(ico_vfs_stat(vfs, "MISSING.BIN", &e) != 0);
    CHECK(ico_vfs_stat(vfs, "SYSTEM.CNF/X", &e) != 0);
    CHECK(ico_vfs_stat(vfs, "DFDATAS/ALPHA.PAK", &e) != 0); /* inside DATA.DF, not on the disc */
    /* a directory spanning two sectors, recorded with an unaligned length */
    CHECK(ico_vfs_stat(vfs, "MANY/F00.BIN", &e) == 0 && e.size == 10);
    CHECK(ico_vfs_stat(vfs, "MANY/F59.BIN;1", &e) == 0 && e.size == 69);
    CHECK(ico_vfs_stat(vfs, "MANY/F60.BIN", &e) != 0);

    /* byte reads */
    CHECK(ico_vfs_open(vfs, "SLUS_000.00", &f) == 0);
    CHECK(ico_vfs_size(&f) == SYN_BOOT_SIZE);
    CHECK(ico_vfs_read(&f, 0, buf, sizeof(buf)) == SYN_BOOT_SIZE);
    ok = 1;
    for (i = 0; i < SYN_BOOT_SIZE; i++) {
        ok &= buf[i] == synthByte(SYN_BOOT, i);
    }
    CHECK(ok);
    /* a read crossing the sector boundary from an odd offset */
    memset(buf, 0, sizeof(buf));
    CHECK(ico_vfs_read(&f, 2000, buf, 100) == 100);
    ok = 1;
    for (i = 0; i < 100; i++) {
        ok &= buf[i] == synthByte(SYN_BOOT, 2000 + i);
    }
    CHECK(ok);
    CHECK(ico_vfs_read(&f, SYN_BOOT_SIZE - 10, buf, 100) == 10);
    CHECK(ico_vfs_read(&f, SYN_BOOT_SIZE, buf, 100) == 0);

    /* sector reads */
    CHECK(ico_vfs_read_sectors(vfs, SYN_SECTORS - 1, 1, buf) == 0);
    CHECK(ico_vfs_read_sectors(vfs, SYN_SECTORS - 1, 2, buf) != 0);
    CHECK(ico_vfs_read_sectors(vfs, SYN_SECTORS, 1, buf) != 0);
    CHECK(ico_vfs_read_sectors(vfs, 0xFFFFFFFFu, 2, buf) != 0);
}

/* The DATA.DF directory as fumi/ios/cdvd.c's unifile_read_func walks it:
   {count, {name[32], offset, size} x count}, each member's LSN being DATA.DF's
   LSN + offset / 2048.  Returns the entry count, or -1. */
static int check_unifile(IcoVfs *vfs, int expect_count)
{
    IcoVfsFile f;
    unsigned char hdr[4];
    unsigned char ent[40];
    uint32_t count;
    uint32_t i;
    int ok = 1;

    if (ico_vfs_open(vfs, "\\DFDATAS\\DATA.DF;1", &f) != 0) {
        return -1;
    }
    if (ico_vfs_read(&f, 0, hdr, 4) != 4) {
        return -1;
    }
    count = rd32(hdr);
    if (count == 0 || (uint64_t)count * 40 + 4 > f.entry.size) {
        return -1;
    }
    /* the game's directory cache holds 200 entries, DATA.DF's own included,
       and unifile_read_func fills it without a bound */
    CHECK(count + 1 <= 200);
    for (i = 0; i < count; i++) {
        uint32_t off;
        uint32_t size;
        uint32_t lsn;

        if (ico_vfs_read(&f, 4 + (uint64_t)i * 40, ent, 40) != 40) {
            return -1;
        }
        off = rd32(ent + 32);
        size = rd32(ent + 36);
        lsn = off / 2048 + f.entry.lsn;
        ok &= memchr(ent, 0, 32) != NULL; /* the name is terminated */
        ok &= off % 2048 == 0;            /* members start on a sector */
        ok &= (uint64_t)off + size <= f.entry.size;
        ok &= lsn + ico_vfs_size_to_sectors(size) <= ico_vfs_volume_sectors(vfs);
    }
    CHECK(ok);
    if (expect_count >= 0) {
        CHECK((int)count == expect_count);
    }
    return (int)count;
}

static void test_cdvd_host(IcoVfs *vfs)
{
    sceCdlFILE fp;
    sceCdCLOCK clk;
    CdRMode mode = {0, 1, 0, 0};
    unsigned char buf[4 * 2048];
    uint32_t iop;
    int err;
    int i;
    int ok;

    ico_cdvd_host_reset();
    ico_vfs_set_disc(vfs);
    CHECK(sceCdInit(0) == 1);
    CHECK(sceCdInit(0) == 1);
    /* sceCdInit hooks the completion into the host loop's vsync, once */
    CHECK(ico_cdvd_host_vsync_hooked());
    CHECK(vsyncFn == ico_cdvd_host_vsync && vsyncRegistrations == 1);
    CHECK(sceCdMmode(2) == 1);
    CHECK(sceCdDiskReady(0) == ICO_CD_READY_COMPLETE);
    CHECK(sceCdStatus() == ICO_CD_STAT_PAUSE);
    CHECK(sceCdGetDiskType() == ICO_CD_TYPE_PS2DVD);
    CHECK(ico_cdvd_host_boot_name() != NULL &&
          strcmp(ico_cdvd_host_boot_name(), "SLUS_000.00") == 0);

    /* sceCdSearchFile: the 0x24-byte record, nothing past it */
    memset(&fp, 0xEE, sizeof(fp));
    CHECK(sceCdSearchFile((struct sceCdlFILE *)&fp, "\\DFDATAS\\DATA.DF;1") == 1);
    CHECK(fp.lsn == SYN_DATADF && fp.size == SYN_DATADF_SIZE);
    CHECK(strcmp(fp.name, "DATA.DF;1") == 0);
    CHECK(fp.date[1] == 56 && fp.date[2] == 34 && fp.date[3] == 12 && fp.date[4] == 24 &&
          fp.date[5] == 9 && (fp.date[6] | fp.date[7] << 8) == 2001);
    CHECK(fp.guard[0] == 0xEEEEEEEEu);
    CHECK(sceCdSearchFile((struct sceCdlFILE *)&fp, "\\SLUS_000.00;1") == 1);
    CHECK(sceCdSearchFile((struct sceCdlFILE *)&fp, "\\NOPE.BIN;1") == 0);
    CHECK(sceCdSearchFile((struct sceCdlFILE *)&fp, "\\DFDATAS;1") == 0); /* a directory */

    /* a non-blocking read stays busy until the next vsync */
    memset(buf, 0, sizeof(buf));
    CHECK(sceCdRead(SYN_BOOT, 2, buf, &mode) == 1);
    CHECK(sceCdSync(1) == 1);
    CHECK(sceCdRead(SYN_BOOT, 1, buf, &mode) == 0); /* the drive is busy */
    vsync();
    CHECK(sceCdSync(1) == 0);
    CHECK(sceCdGetError() == ICO_CD_ERR_NONE);
    ok = 1;
    for (i = 0; i < SYN_BOOT_SIZE; i++) {
        ok &= buf[i] == synthByte(SYN_BOOT, (uint32_t)i);
    }
    CHECK(ok);

    /* a blocking sync ends the command at once */
    CHECK(sceCdRead(SYN_DATADF, 1, buf, &mode) == 1);
    CHECK(sceCdSync(0) == 0);
    CHECK(sceCdSync(1) == 0);
    CHECK(rd32(buf) == 3);

    /* reading past the end of the volume: "Reach to CD end." */
    CHECK(sceCdRead(SYN_SECTORS - 1, 2, buf, &mode) == 1);
    CHECK(sceCdSync(0) == 0);
    CHECK(sceCdGetError() == ICO_CD_ERR_END);
    /* sceCdBreak aborts a command in flight */
    CHECK(sceCdRead(SYN_ROOT, 1, buf, &mode) == 1);
    CHECK(sceCdBreak() == 1);
    CHECK(sceCdSync(1) == 0 && sceCdGetError() == ICO_CD_ERR_ABORT);

    /* sceCdReadIOPm reads into the IOP heap */
    ico_sif_host_reset();
    CHECK(sceSifInitIopHeap() == 0);
    iop = (uint32_t)sceSifAllocIopHeap(3 * 2048);
    CHECK(iop >= ICO_IOP_HEAP_BASE && iop + 3 * 2048 <= ICO_IOP_HEAP_END);
    CHECK(sceCdReadIOPm(SYN_DATADF + 1, 3, (void *)(uintptr_t)iop, &mode) == 1);
    CHECK(sceCdSync(1) == 1);
    vsync();
    CHECK(sceCdSync(1) == 0 && sceCdGetError() == 0);
    ok = 1;
    for (i = 0; i < 3 * 2048; i++) {
        ok &= ((unsigned char *)ico_iop_ptr(iop))[i] == synthByte(SYN_DATADF, 2048u + (uint32_t)i);
    }
    CHECK(ok);
    /* kseg1 view of the same address */
    CHECK(ico_iop_ptr(iop | 0xA0000000u) == ico_iop_ptr(iop));
    /* an address outside IOP RAM is refused with "Invalid transfer address." */
    CHECK(sceCdReadIOPm(SYN_DATADF, 1, (void *)(uintptr_t)(ICO_IOP_RAM_SIZE - 1024), &mode) == 1);
    CHECK(sceCdSync(0) == 0 && sceCdGetError() == ICO_CD_ERR_ADDRESS);
    CHECK(sceSifFreeIopHeap((int)iop) == 0);
    CHECK(sceSifFreeIopHeap((int)iop) == -1);

    /* the stream: cdvd.c's DirectSt path asks for 576 sectors of ring */
    iop = (uint32_t)sceSifAllocIopHeap(576 * 2048 + 16);
    CHECK(iop != 0);
    CHECK(sceCdStInit(576, 36, (void *)(uintptr_t)((iop + 15) & ~15u)) == 1);
    CHECK(sceCdStRead(1, buf, 1, &err) == 0); /* not started */
    CHECK(sceCdStStart(SYN_DATADF, &mode) == 1);
    CHECK(sceCdStStat() == 576);
    CHECK(sceCdStRead(2, buf, 1, &err) == 2 && err == 0);
    CHECK(rd32(buf) == 3 && buf[2048] == synthByte(SYN_DATADF, 2048));
    CHECK(sceCdStRead(1, buf, 1, &err) == 1 && buf[0] == synthByte(SYN_DATADF, 4096));
    CHECK(sceCdStSeek(SYN_SECTORS - 1) == 1);
    CHECK(sceCdStRead(4, buf, 1, &err) == 1 && err == 0); /* clipped at the volume's end */
    CHECK(sceCdStRead(1, buf, 1, &err) == 0 && err == ICO_CD_ERR_END);
    CHECK(sceCdStPause() == 1 && sceCdStResume() == 1);
    CHECK(sceCdStStop() == 1);
    CHECK(sceCdStStat() == 0);
    CHECK(sceSifFreeIopHeap((int)iop) == 0);

    /* the clock: fixed and BCD */
    CHECK(sceCdReadClock((struct sceCdCLOCK *)&clk) == 1);
    CHECK(clk.stat == 0 && clk.year == 0x02 && clk.month == 0x01 && clk.day == 0x01 &&
          clk.hour == 0 && clk.minute == 0 && clk.second == 0);

    ico_vfs_set_disc(NULL);
}

static void clock_1999(IcoCdClockTime *t)
{
    t->year = 1999;
    t->month = 12;
    t->day = 31;
    t->hour = 23;
    t->minute = 59;
    t->second = 58;
}

static void test_clock_source(void)
{
    sceCdCLOCK clk;

    ico_cdvd_host_set_clock_source(clock_1999);
    CHECK(sceCdReadClock((struct sceCdCLOCK *)&clk) == 1);
    CHECK(clk.year == 0x99 && clk.month == 0x12 && clk.day == 0x31 && clk.hour == 0x23 &&
          clk.minute == 0x59 && clk.second == 0x58);
    ico_cdvd_host_set_clock_source(NULL);
}

static void test_no_disc(void)
{
    CdRMode mode = {0, 1, 0, 0};
    unsigned char buf[2048];

    /* nothing mounted and no default image to find */
#ifdef _WIN32
    _putenv("ICO_ISO=nonexistent-disc-image.iso");
#else
    setenv("ICO_ISO", "nonexistent-disc-image.iso", 1);
#endif
    ico_cdvd_host_reset();
    CHECK(sceCdInit(0) == 1);
    CHECK(sceCdDiskReady(1) == ICO_CD_READY_NOT_READY);
    CHECK(sceCdStatus() == ICO_CD_STAT_STOP);
    CHECK(sceCdGetDiskType() == ICO_CD_TYPE_NODISC);
    CHECK(sceCdRead(0, 1, buf, &mode) == 1);
    CHECK(sceCdSync(0) == 0 && sceCdGetError() == ICO_CD_ERR_NODISC);
    ico_cdvd_host_reset();
}

static void test_sif(void)
{
    static unsigned char src[300];
    sceSifDmaData d;
    uint32_t a, b, c;
    int id;
    int i;

    ico_sif_host_reset();
    a = (uint32_t)sceSifAllocIopHeap(100);
    b = (uint32_t)sceSifAllocIopHeap(0x78000);
    c = (uint32_t)sceSifAllocIopHeap(0xB8800);
    CHECK(a != 0 && b != 0 && c != 0);
    CHECK(a % 16 == 0 && b == a + 112 && c == b + 0x78000);
    CHECK(sceSifAllocIopHeap(ICO_IOP_RAM_SIZE) == 0);
    CHECK(sceSifAllocIopHeap(0) == 0);
    CHECK(sceSifFreeIopHeap((int)b) == 0);
    /* first fit reuses the hole */
    CHECK((uint32_t)sceSifAllocIopHeap(0x1000) == b);

    for (i = 0; i < (int)sizeof(src); i++) {
        src[i] = (unsigned char)(i * 3 + 1);
    }
    d.src = (__UINTPTR_TYPE__)src;
    d.dest = c;
    d.size = (int)sizeof(src);
    d.u.attr = 0;
    id = sceSifSetDma(&d, 1);
    CHECK(id != 0);
    CHECK(sceSifDmaStat(id) < 0);
    CHECK(memcmp(ico_iop_ptr(c), src, sizeof(src)) == 0);
    CHECK(ico_iop_addr(ico_iop_ptr(c)) == c);
    CHECK(ico_iop_addr(src) == 0);
    d.dest = ICO_IOP_RAM_SIZE - 10;
    CHECK(sceSifSetDma(&d, 1) == 0);

    /* boot: the IOP calls FileManager.c makes all succeed */
    CHECK(sceSifRebootIop("cdrom0:\\IOPRP224.IMG;1") != 0);
    CHECK(sceSifSyncIop() != 0);
    CHECK(sceSifLoadModule("cdrom0:\\SIO2MAN.IRX;1", 0, 0) >= 0);
    CHECK(sceSifLoadModule("cdrom0:\\SNDN2DRV.IRX;1", 0, 0) >= 0);
    CHECK(sceSifLoadFileReset() == 0);
    ico_sif_host_reset();
}

/* --- .chd images around the synthetic ISO ---------------------------------- */

/* No .chd is committed and the container has no chdman, so the test writes
   CHD v5 files itself, uncompressed (every compressor "none"), from the
   layout in libchdr's chd.h: the 124-byte header (big-endian), an optional
   metadata entry (tag, flags and length, next offset, then the text), the
   map (one 32-bit entry per hunk: the hunk's file offset / hunkbytes, or 0
   for a hunk of zeros, which libchdr fills itself), then the hunks at
   hunk-aligned offsets. */

#define CHD_V5_HEADER 124u
#define CHD_META_HEADER 16u
#define CHD_CD_FRAME 2448u

enum { CHD_PLAIN = 0, CHD_BAD_MAGIC = 1, CHD_WITH_PARENT = 2 };

static uint32_t chdZeroHunks; /* the last write_chd's all-zero hunks */

static void put_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void put_be64(unsigned char *p, uint64_t v)
{
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

static unsigned char *read_whole(const char *path, size_t *n)
{
    FILE *fp = fopen(path, "rb");
    unsigned char *p = NULL;
    long len;

    *n = 0;
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) == 0 && (len = ftell(fp)) > 0 && fseek(fp, 0, SEEK_SET) == 0) {
        p = malloc((size_t)len);
        if (p != NULL && fread(p, 1, (size_t)len, fp) != (size_t)len) {
            free(p);
            p = NULL;
        }
        *n = p != NULL ? (size_t)len : 0;
    }
    fclose(fp);
    return p;
}

/* Write `data` (n bytes, the CHD's logical bytes) as an uncompressed CHD
   v5; `meta` is a CD track entry's text or NULL. 0 or -1. */
static int write_chd(const char *path, const unsigned char *data, uint64_t n, uint32_t hunkbytes,
                     uint32_t unitbytes, const char *meta, int variant)
{
    uint32_t hunks = (uint32_t)((n + hunkbytes - 1) / hunkbytes);
    uint32_t metalen = meta != NULL ? (uint32_t)strlen(meta) + 1 : 0;
    uint64_t metaoff = meta != NULL ? CHD_V5_HEADER : 0;
    uint64_t mapoff = CHD_V5_HEADER + (meta != NULL ? CHD_META_HEADER + metalen : 0);
    uint64_t first = (mapoff + 4ull * hunks + hunkbytes - 1) / hunkbytes; /* in hunks */
    size_t total = (size_t)((first + hunks) * hunkbytes);
    unsigned char *f = calloc(1, total);
    uint32_t h;
    FILE *fp;
    int rc = 0;

    chdZeroHunks = 0;
    if (f == NULL) {
        return -1;
    }
    memcpy(f, variant == CHD_BAD_MAGIC ? "MComprHX" : "MComprHD", 8);
    put_be32(f + 8, CHD_V5_HEADER);
    put_be32(f + 12, 5);
    /* [16] compressors[4]: all 0, uncompressed */
    put_be64(f + 32, n);
    put_be64(f + 40, mapoff);
    put_be64(f + 48, metaoff);
    put_be32(f + 56, hunkbytes);
    put_be32(f + 60, unitbytes);
    if (variant == CHD_WITH_PARENT) {
        f[104] = 0x5a; /* parentsha1 not zero: a diff against a parent */
    }
    if (meta != NULL) {
        put_be32(f + metaoff, (uint32_t)'C' << 24 | (uint32_t)'H' << 16 | (uint32_t)'T' << 8 | '2');
        put_be32(f + metaoff + 4, metalen); /* flags 0 in the top byte */
        put_be64(f + metaoff + 8, 0);       /* no next entry */
        memcpy(f + metaoff + CHD_META_HEADER, meta, metalen);
    }
    for (h = 0; h < hunks; h++) {
        uint64_t at = (uint64_t)h * hunkbytes;
        size_t k = n - at < hunkbytes ? (size_t)(n - at) : hunkbytes;
        unsigned char *dst = f + (first + h) * hunkbytes;
        size_t i;
        int zero = 1;

        for (i = 0; i < k; i++) {
            zero &= data[at + i] == 0;
        }
        if (zero) {
            put_be32(f + mapoff + 4u * h, 0);
            chdZeroHunks++;
            continue;
        }
        memcpy(dst, data + at, k);
        put_be32(f + mapoff + 4u * h, (uint32_t)(first + h));
    }
    fp = fopen(path, "wb");
    if (fp == NULL || fwrite(f, 1, total, fp) != total) {
        rc = -1;
    }
    if (fp != NULL && fclose(fp) != 0) {
        rc = -1;
    }
    free(f);
    return rc;
}

/* The SHA-1 and size of an image's logical bytes, read in odd-sized pieces
   so reads straddle hunks and frames. */
static int image_sha1(const char *path, char hex[41], uint64_t *bytes)
{
    IcoDiscImage *img = ico_disc_image_open(path);
    unsigned char buf[5000];
    uint64_t at = 0;
    Sha1 sha;

    if (img == NULL) {
        return -1;
    }
    *bytes = ico_disc_image_bytes(img);
    sha1_init(&sha);
    while (at < *bytes) {
        size_t k = *bytes - at < sizeof(buf) ? (size_t)(*bytes - at) : sizeof(buf);

        if (ico_disc_image_read(img, at, buf, k) != 0) {
            ico_disc_image_close(img);
            return -1;
        }
        sha1_update(&sha, buf, k);
        at += k;
    }
    CHECK(ico_disc_image_read(img, *bytes - 1, buf, 2) != 0); /* past the end */
    ico_disc_image_close(img);
    sha1_final_hex(&sha, hex);
    return 0;
}

/* Mount a .chd, .bin or .cue made from the synthetic ISO and compare it with
   the ISO. */
static void check_image_volume(const char *chd, const unsigned char *iso, size_t isosize,
                               const char *isohex)
{
    unsigned char sec[2 * ICO_VFS_SECTOR];
    char hex[41];
    uint64_t bytes = 0;
    uint32_t lsn;
    IcoVfs *vfs = ico_vfs_mount(&ico_vfs_iso9660, chd);
    int same = 1;

    CHECK(vfs != NULL);
    if (vfs == NULL) {
        return;
    }
    test_vfs_synthetic(vfs);
    CHECK(ico_vfs_volume_sectors(vfs) == SYN_SECTORS);
    for (lsn = 0; lsn + 1 < SYN_SECTORS; lsn++) {
        if (ico_vfs_read_sectors(vfs, lsn, 2, sec) != 0) {
            same = 0;
            break;
        }
        same &= memcmp(sec, iso + (size_t)lsn * ICO_VFS_SECTOR, sizeof(sec)) == 0;
    }
    CHECK(same);
    ico_vfs_unmount(vfs);
    CHECK(image_sha1(chd, hex, &bytes) == 0);
    CHECK(bytes == isosize);
    CHECK(strcmp(hex, isohex) == 0);
}

static void test_chd(const char *dir, const char *isopath)
{
    char chd[1024], isohex[41], hex[41], meta[160];
    unsigned char *iso, *cd;
    size_t isosize, i;
    uint64_t bytes = 0;
    uint32_t frames, s;
    Sha1 sha;

    iso = read_whole(isopath, &isosize);
    CHECK(iso != NULL && isosize == (size_t)SYN_SECTORS * ICO_VFS_SECTOR);
    if (iso == NULL) {
        return;
    }
    /* the plain file's own SHA-1 is what both containers must give */
    sha1_init(&sha);
    sha1_update(&sha, iso, isosize);
    sha1_final_hex(&sha, isohex);
    CHECK(image_sha1(isopath, hex, &bytes) == 0 && bytes == isosize && strcmp(hex, isohex) == 0);
    snprintf(chd, sizeof(chd), "%s/vfs_test_synthetic.chd", dir);

    /* a DVD CHD (chdman createdvd): 2048-byte units; three sectors a hunk,
       so the last hunk is partly past the end */
    CHECK(write_chd(chd, iso, isosize, 3 * ICO_VFS_SECTOR, ICO_VFS_SECTOR, NULL, CHD_PLAIN) == 0);
    CHECK(chdZeroHunks > 0); /* the system area: map entries of both kinds */
    check_image_volume(chd, iso, isosize, isohex);

    /* the same bytes with the magic changed: not a CHD, and no ISO9660
       volume either, so it is refused */
    CHECK(write_chd(chd, iso, isosize, 3 * ICO_VFS_SECTOR, ICO_VFS_SECTOR, NULL, CHD_BAD_MAGIC) ==
          0);
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, chd) == NULL);

    /* a CHD that needs its parent is refused */
    CHECK(write_chd(chd, iso, isosize, 3 * ICO_VFS_SECTOR, ICO_VFS_SECTOR, NULL, CHD_WITH_PARENT) ==
          0);
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, chd) == NULL);
    CHECK(ico_disc_image_open(chd) == NULL);

    /* units that are neither a DVD sector nor a CD frame are refused */
    CHECK(write_chd(chd, iso, isosize, 4 * 512, 512, NULL, CHD_PLAIN) == 0);
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, chd) == NULL);

    /* a CD CHD (chdman createcd): 2448-byte frames, raw mode 1 sectors
       (sync, header, the 2048 user bytes at 16, EDC/ECC and subcode left
       zero), eight frames a hunk, the track padded to four frames */
    frames = (SYN_SECTORS + 3) / 4 * 4;
    cd = calloc(frames, CHD_CD_FRAME);
    CHECK(cd != NULL);
    if (cd != NULL) {
        for (s = 0; s < SYN_SECTORS; s++) {
            unsigned char *fr = cd + (size_t)s * CHD_CD_FRAME;

            for (i = 1; i < 11; i++) {
                fr[i] = 0xff;
            }
            fr[15] = 1; /* mode 1 */
            memcpy(fr + 16, iso + (size_t)s * ICO_VFS_SECTOR, ICO_VFS_SECTOR);
        }
        snprintf(meta, sizeof(meta),
                 "TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:%u PREGAP:0 PGTYPE:MODE1 PGSUB:RW "
                 "POSTGAP:0",
                 (unsigned)SYN_SECTORS);
        CHECK(write_chd(chd, cd, (uint64_t)frames * CHD_CD_FRAME, 8 * CHD_CD_FRAME, CHD_CD_FRAME,
                        meta, CHD_PLAIN) == 0);
        check_image_volume(chd, iso, isosize, isohex);

        /* an audio first track holds no data */
        snprintf(meta, sizeof(meta),
                 "TRACK:1 TYPE:AUDIO SUBTYPE:NONE FRAMES:%u PREGAP:0 PGTYPE:MODE1 PGSUB:RW "
                 "POSTGAP:0",
                 (unsigned)SYN_SECTORS);
        CHECK(write_chd(chd, cd, (uint64_t)frames * CHD_CD_FRAME, 8 * CHD_CD_FRAME, CHD_CD_FRAME,
                        meta, CHD_PLAIN) == 0);
        CHECK(ico_vfs_mount(&ico_vfs_iso9660, chd) == NULL);
        free(cd);
    }
    remove(chd);
    free(iso);
}

/* --- raw CD images (.bin) and cue sheets ---------------------------------- */

#define BIN_FRAME 2352u

static int write_file(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (f == NULL) {
        return -1;
    }
    ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}

/* The synthetic ISO as raw 2352-byte frames after `lead` zero frames:
   mode 1 (user data at 16) or mode 2 form 1 (subheader, user data at 24).
   `form2_first` marks sector 0's subheader as form 2.  `extra` zero bytes
   follow the last frame.  Returns the buffer and its size in *n. */
static unsigned char *build_bin(const unsigned char *iso, int mode, unsigned lead, int form2_first,
                                size_t extra, size_t *n)
{
    size_t total = ((size_t)lead + SYN_SECTORS) * BIN_FRAME + extra;
    unsigned char *bin = calloc(1, total);
    uint32_t s;
    int i;

    if (bin == NULL) {
        return NULL;
    }
    for (s = 0; s < SYN_SECTORS; s++) {
        unsigned char *fr = bin + ((size_t)lead + s) * BIN_FRAME;

        for (i = 1; i < 11; i++) {
            fr[i] = 0xff;
        }
        fr[15] = (unsigned char)mode;
        if (mode == 2) {
            fr[18] = fr[22] = (s == 0 && form2_first) ? 0x20 : 0x08;
            memcpy(fr + 24, iso + (size_t)s * ICO_VFS_SECTOR, ICO_VFS_SECTOR);
        } else {
            memcpy(fr + 16, iso + (size_t)s * ICO_VFS_SECTOR, ICO_VFS_SECTOR);
        }
    }
    *n = total;
    return bin;
}

static void check_cue_text(const char *text, int ok, const char *file, const char *type,
                           uint32_t first_unit, const char *what)
{
    IcoCueTrack t;
    char err[160];
    int rc = ico_cue_parse(text, strlen(text), &t, err, sizeof(err));

    if (rc != (ok ? 0 : -1) || (ok && (strcmp(t.file, file) != 0 || strcmp(t.type, type) != 0 ||
                                       t.first_unit != first_unit))) {
        fprintf(stderr, "cue case failed: %s (rc %d, file \"%s\", type %s, first %u, \"%s\")\n",
                what, rc, t.file, t.type, (unsigned)t.first_unit, err);
        failures++;
    }
}

static void test_cue_parse(void)
{
    IcoCueTrack t;
    char err[160];
    const char *p = "FILE \"a b.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n";

    check_cue_text(p, 1, "a b.bin", "MODE2/2352", 0, "quoted name with a space");
    check_cue_text("FILE x.bin BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n", 1, "x.bin",
                   "MODE1/2352", 0, "bare name");
    check_cue_text("FILE \"x.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n", 1,
                   "x.bin", "MODE2/2352", 0, "CRLF and indentation");
    check_cue_text("file \"x.bin\" binary\ntrack 01 mode2/2352\nindex 01 00:00:00", 1, "x.bin",
                   "mode2/2352", 0, "lower case, no final newline");
    check_cue_text("REM COMMENT x\nFILE \"x.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 00 00:00:00\n"
                   "INDEX 01 01:02:03\n",
                   1, "x.bin", "MODE2/2352", (1 * 60 + 2) * 75 + 3, "MSF to frames");
    check_cue_text("\xef\xbb\xbf"
                   "FILE \"x.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:02\n",
                   1, "x.bin", "MODE1/2352", 2, "byte-order mark");
    check_cue_text("FILE \"x.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n"
                   "TRACK 02 AUDIO\nINDEX 01 50:00:00\n",
                   1, "x.bin", "MODE1/2352", 0, "later tracks are ignored");
    check_cue_text("FILE \"x.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n"
                   "FILE \"y.bin\" BINARY\nTRACK 02 AUDIO\nINDEX 01 00:00:00\n",
                   0, NULL, NULL, 0, "two files");
    check_cue_text("FILE \"x.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n", 0, NULL, NULL, 0,
                   "audio first");
    check_cue_text("FILE \"x.bin\" BINARY\nTRACK 01 MODE9/2352\nINDEX 01 00:00:00\n", 0, NULL, NULL,
                   0, "unknown type");
    check_cue_text("FILE \"x.bin\" BINARY\nTRACK 01 MODE1\nINDEX 01 00:00:00\n", 0, NULL, NULL, 0,
                   "type with no unit size");
    check_cue_text("FILE \"x.wav\" WAVE\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n", 0, NULL, NULL,
                   0, "not a binary file");
    check_cue_text("FILE \"x.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:75\n", 0, NULL, NULL,
                   0, "frame 75");
    check_cue_text("FILE \"x.bin\" BINARY\nTRACK 01 MODE1/2352\n", 0, NULL, NULL, 0, "no INDEX 01");
    check_cue_text("", 0, NULL, NULL, 0, "empty");
    CHECK(ico_cue_parse(p, strlen(p), &t, err, sizeof(err)) == 0 && t.unit_bytes == 2352 &&
          t.data_off == 24);
    check_cue_text("FILE \"x.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n", 1, "x.bin",
                   "MODE1/2352", 0, "again");
    {
        const char *cooked = "FILE \"x.bin\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n";

        CHECK(ico_cue_parse(cooked, strlen(cooked), &t, err, sizeof(err)) == 0 &&
              t.unit_bytes == 2048 && t.data_off == 0);
    }
}

static void test_bin(const char *dir, const char *isopath)
{
    char binp[1024], cuep[1024], isohex[41], text[512];
    unsigned char *iso, *bin;
    size_t isosize, n;
    Sha1 sha;

    test_cue_parse();
    iso = read_whole(isopath, &isosize);
    CHECK(iso != NULL && isosize == (size_t)SYN_SECTORS * ICO_VFS_SECTOR);
    if (iso == NULL) {
        return;
    }
    sha1_init(&sha);
    sha1_update(&sha, iso, isosize);
    sha1_final_hex(&sha, isohex);
    snprintf(binp, sizeof(binp), "%s/vfs_test_synthetic.bin", dir);
    snprintf(cuep, sizeof(cuep), "%s/vfs_test_synthetic.CUE", dir);

    /* a bare .bin is found by its sync pattern: mode 1, data at 16 */
    bin = build_bin(iso, 1, 0, 0, 0, &n);
    CHECK(bin != NULL && write_file(binp, bin, n) == 0);
    check_image_volume(binp, iso, isosize, isohex);
    free(bin);

    /* a size that is not whole frames: the stray bytes are ignored */
    bin = build_bin(iso, 1, 0, 0, 100, &n);
    CHECK(bin != NULL && write_file(binp, bin, n) == 0);
    check_image_volume(binp, iso, isosize, isohex);
    free(bin);

    /* mode 2 form 1: data at 24 after the subheader */
    bin = build_bin(iso, 2, 0, 0, 0, &n);
    CHECK(bin != NULL && write_file(binp, bin, n) == 0);
    check_image_volume(binp, iso, isosize, isohex);

    /* a cue beside it (upper-case extension, CRLF) */
    snprintf(text, sizeof(text),
             "FILE \"vfs_test_synthetic.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 "
             "00:00:00\r\n");
    CHECK(write_file(cuep, text, strlen(text)) == 0);
    check_image_volume(cuep, iso, isosize, isohex);

    /* the cue's type and the sector header must agree */
    snprintf(text, sizeof(text),
             "FILE \"vfs_test_synthetic.bin\" BINARY\nTRACK 01 MODE1/2352\nINDEX 01 00:00:00\n");
    CHECK(write_file(cuep, text, strlen(text)) == 0);
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, cuep) == NULL);

    /* a cue naming a .bin that is not there */
    snprintf(text, sizeof(text),
             "FILE \"no_such_image.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    CHECK(write_file(cuep, text, strlen(text)) == 0);
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, cuep) == NULL);

    /* two files, an audio first track, an unknown type */
    snprintf(text, sizeof(text),
             "FILE \"vfs_test_synthetic.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n"
             "FILE \"second.bin\" BINARY\nTRACK 02 AUDIO\nINDEX 01 00:00:00\n");
    CHECK(write_file(cuep, text, strlen(text)) == 0);
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, cuep) == NULL);
    snprintf(text, sizeof(text),
             "FILE \"vfs_test_synthetic.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n");
    CHECK(write_file(cuep, text, strlen(text)) == 0);
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, cuep) == NULL);
    snprintf(text, sizeof(text),
             "FILE \"vfs_test_synthetic.bin\" BINARY\nTRACK 01 MODE9/2352\nINDEX 01 00:00:00\n");
    CHECK(write_file(cuep, text, strlen(text)) == 0);
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, cuep) == NULL);
    free(bin);

    /* two frames come before the track's sector 0 (INDEX 01 00:00:02) */
    bin = build_bin(iso, 2, 2, 0, 0, &n);
    CHECK(bin != NULL && write_file(binp, bin, n) == 0);
    snprintf(text, sizeof(text),
             "FILE \"vfs_test_synthetic.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 00 00:00:00\n"
             "INDEX 01 00:00:02\n");
    CHECK(write_file(cuep, text, strlen(text)) == 0);
    check_image_volume(cuep, iso, isosize, isohex);
    free(bin);

    /* a form 2 sector 0 holds no disc data; with or without a cue */
    bin = build_bin(iso, 2, 0, 1, 0, &n);
    CHECK(bin != NULL && write_file(binp, bin, n) == 0);
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, binp) == NULL);
    CHECK(ico_disc_image_open(binp) == NULL);
    snprintf(text, sizeof(text),
             "FILE \"vfs_test_synthetic.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    CHECK(write_file(cuep, text, strlen(text)) == 0);
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, cuep) == NULL);
    free(bin);

    /* sector mode 0 (or any other) is refused */
    bin = build_bin(iso, 1, 0, 0, 0, &n);
    if (bin != NULL) {
        bin[15] = 0;
        CHECK(write_file(binp, bin, n) == 0);
        CHECK(ico_vfs_mount(&ico_vfs_iso9660, binp) == NULL);
        free(bin);
    }
    remove(binp);
    remove(cuep);
    free(iso);
}

static int run_synthetic(const char *dir)
{
    char path[1024];
    IcoVfs *vfs;

    snprintf(path, sizeof(path), "%s/vfs_test_synthetic.iso", dir);
    if (build_synthetic(path) != 0) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    test_normalize();
    CHECK(ico_vfs_mount(&ico_vfs_iso9660, "nonexistent-image.iso") == NULL);
    vfs = ico_vfs_mount(&ico_vfs_iso9660, path);
    CHECK(vfs != NULL);
    if (vfs == NULL) {
        return 1;
    }
    test_vfs_synthetic(vfs);
    CHECK(check_unifile(vfs, 3) == 3);
    test_cdvd_host(vfs);
    test_clock_source();
    test_sif();
    ico_vfs_unmount(vfs);
    test_no_disc();
    test_chd(dir, path);
    test_bin(dir, path);
    remove(path);
    printf("vfs_test synth: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

/* --- the user's disc ----------------------------------------------------- */

#define PAL_ELF_SHA1 "da3644c54c26fe760f3b6a591a5fc2eab396ed2b"

static int run_disc(const char *iso)
{
    static unsigned char chunk[64 * 2048];
    IcoVfs *vfs;
    IcoVfsFile f;
    IcoVfsEntry e;
    sceCdlFILE fp;
    Sha1 sha;
    char hex[41];
    uint64_t off;
    FILE *probe;
    int count;

    probe = fopen(iso, "rb");
    if (probe == NULL) {
        printf("vfs_test disc: SKIPPED: %s is absent (put the SCES-50760 image there)\n", iso);
        return SKIP;
    }
    fclose(probe);
    vfs = ico_vfs_mount(&ico_vfs_iso9660, iso);
    CHECK(vfs != NULL);
    if (vfs == NULL) {
        return 1;
    }
    CHECK(ico_vfs_stat(vfs, "SYSTEM.CNF", &e) == 0 && !e.is_dir);
    CHECK(ico_vfs_stat(vfs, "cdrom0:\\SCES_507.60;1", &e) == 0 && !e.is_dir);
    CHECK(ico_vfs_stat(vfs, "\\DFDATAS\\DATA.DF;1", &e) == 0 && !e.is_dir);
    /* the six modules FileManager.c used to load, and the IOP image */
    CHECK(ico_vfs_stat(vfs, "cdrom0:\\IOPRP224.IMG;1", NULL) == 0);
    CHECK(ico_vfs_stat(vfs, "cdrom0:\\SIO2MAN.IRX;1", NULL) == 0);
    CHECK(ico_vfs_stat(vfs, "cdrom0:\\PADMAN.IRX;1", NULL) == 0);
    CHECK(ico_vfs_stat(vfs, "cdrom0:\\MCMAN.IRX;1", NULL) == 0);
    CHECK(ico_vfs_stat(vfs, "cdrom0:\\MCSERV.IRX;1", NULL) == 0);
    CHECK(ico_vfs_stat(vfs, "cdrom0:\\LIBSD.IRX;1", NULL) == 0);
    CHECK(ico_vfs_stat(vfs, "cdrom0:\\SNDN2DRV.IRX;1", NULL) == 0);

    /* the boot ELF through the VFS hashes to config/sha1sums.txt's value */
    CHECK(ico_vfs_open(vfs, "SCES_507.60", &f) == 0);
    sha1_init(&sha);
    for (off = 0; off < f.entry.size;) {
        int64_t n = ico_vfs_read(&f, off, chunk, sizeof(chunk));

        if (n <= 0) {
            CHECK(n > 0);
            break;
        }
        sha1_update(&sha, chunk, (size_t)n);
        off += (uint64_t)n;
    }
    sha1_final_hex(&sha, hex);
    printf("vfs_test disc: SCES_507.60 via the VFS: %u bytes, SHA-1 %s\n", (unsigned)f.entry.size,
           hex);
    CHECK(strcmp(hex, PAL_ELF_SHA1) == 0);

    count = check_unifile(vfs, -1);
    CHECK(count > 0);
    printf("vfs_test disc: DATA.DF directory: %d entries, all inside DATA.DF\n", count);

    /* the libcdvd layer on the real disc */
    ico_cdvd_host_reset();
    ico_vfs_set_disc(vfs);
    CHECK(sceCdGetDiskType() == ICO_CD_TYPE_PS2DVD);
    CHECK(ico_cdvd_host_boot_name() != NULL &&
          strcmp(ico_cdvd_host_boot_name(), "SCES_507.60") == 0);
    CHECK(sceCdSearchFile((struct sceCdlFILE *)&fp, "\\SCES_507.60;1") == 1);
    CHECK(fp.size == f.entry.size && fp.lsn == f.entry.lsn);
    CHECK(sceCdSearchFile((struct sceCdlFILE *)&fp, "\\DFDATAS\\DATA.DF;1") == 1);
    ico_vfs_set_disc(NULL);
    ico_cdvd_host_reset();
    ico_vfs_unmount(vfs);
    printf("vfs_test disc: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "synth") == 0) {
        return run_synthetic(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "disc") == 0) {
        return run_disc(argv[2]);
    }
    fprintf(stderr, "usage: %s synth <dir> | disc <iso>\n", argv[0]);
    return 2;
}
