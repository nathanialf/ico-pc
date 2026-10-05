/*
 * port/data/test/vfs_test.c
 *
 * Tests of the disc layer (port/data): the VFS and its ISO9660 backend, the
 * libcdvd host layer, the SIF host layer and IOP RAM.
 *
 *   vfs_test synth <dir>    builds a small ISO9660 image in <dir> and runs
 *                           everything against it (no disc needed)
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
