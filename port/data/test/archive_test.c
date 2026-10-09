/*
 * port/data/test/archive_test.c
 *
 * The archive backend and the first-run extractor (port/data/archive.c,
 * extract.c).
 *
 *   archive_test synth <dir>         vfs_test's synthetic ISO9660 image,
 *                                    extracted into <dir> and compared with
 *                                    the ISO backend (no disc needed)
 *   archive_test disc <iso> <dir>    the user's PAL image: extract into a
 *                                    temporary archive (in $TMPDIR when set,
 *                                    else <dir>), mount it and compare it
 *                                    with the image; 77 (skipped) without it
 *   archive_test manifest <iso>      print the DATA.DF manifest block for
 *                                    extract.c
 *
 * No byte of the disc is reproduced: the disc mode compares the two backends
 * with each other and checks hashes.
 */

/* vfs_test.c's synthetic image, CHECK, the vsync stand-in and the libcdvd
   record type, with its own main renamed */
#define main vfs_test_main
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "vfs_test.c"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#undef main

#include <time.h>
#include "archive.h"
#include "extract.h"
#include "miniz.h"

#ifdef _WIN32
#include <io.h>
#endif

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}

static int cancel_at;

/* the phases in the order they were reported, one letter each: o(pen),
   h(ash), e(xtract), f(inish); repeats are collapsed */
static char phase_seq[16];

static void note_phase(const char *phase)
{
    const char c = phase[0];
    size_t n = strlen(phase_seq);

    if (n == 0 || (phase_seq[n - 1] != c && n + 1 < sizeof(phase_seq))) {
        phase_seq[n] = c;
        phase_seq[n + 1] = '\0';
    }
}

static int progress_cb(void *ctx, const char *phase, uint64_t done, uint64_t total)
{
    int *calls = ctx;

    (*calls)++;
    CHECK(done <= total);
    CHECK(strcmp(phase, "open") == 0 || strcmp(phase, "hash") == 0 ||
          strcmp(phase, "extract") == 0 || strcmp(phase, "finish") == 0);
    /* the steps with no byte count report 0 of 0; the others have a total */
    if (strcmp(phase, "open") == 0 || strcmp(phase, "finish") == 0) {
        CHECK(done == 0 && total == 0);
    } else {
        CHECK(total > 0);
    }
    note_phase(phase);
    return cancel_at > 0 && *calls >= cancel_at;
}

/* Both backends answer a look-up the same way. */
static void same_stat(IcoVfs *iso, IcoVfs *ar, const char *path)
{
    IcoVfsEntry a, b;
    int ra, rb;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    ra = ico_vfs_stat(iso, path, &a);
    rb = ico_vfs_stat(ar, path, &b);
    CHECK(ra == rb);
    if (ra == 0 && rb == 0) {
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    if (ra != rb) {
        fprintf(stderr, "  look-up differs for '%s' (iso %d, archive %d)\n", path, ra, rb);
    }
}

/* Both backends read the same sectors (or both fail). */
static int same_sectors(IcoVfs *iso, IcoVfs *ar, uint32_t lsn, uint32_t count)
{
    static unsigned char a[64 * 2048], b[64 * 2048];
    int ra, rb;

    if (count > 64) {
        count = 64;
    }
    memset(a, 0x11, (size_t)count * 2048);
    memset(b, 0x22, (size_t)count * 2048);
    ra = ico_vfs_read_sectors(iso, lsn, count, a);
    rb = ico_vfs_read_sectors(ar, lsn, count, b);
    if (ra != 0 || rb != 0) {
        return ra == rb;
    }
    return memcmp(a, b, (size_t)count * 2048) == 0;
}

/* Every sector of a file, both backends. */
static int same_file_sectors(IcoVfs *iso, IcoVfs *ar, const char *path)
{
    IcoVfsEntry e;
    uint32_t n, s;
    int ok = 1;

    if (ico_vfs_stat(iso, path, &e) != 0) {
        return 0;
    }
    n = ico_vfs_size_to_sectors(e.size);
    for (s = 0; s < n; s += 64) {
        ok &= same_sectors(iso, ar, e.lsn + s, n - s < 64 ? n - s : 64);
    }
    return ok;
}

/* sceCdSearchFile on one disc, then the other: the records match. */
static void same_search(IcoVfs *iso, IcoVfs *ar, const char *name)
{
    sceCdlFILE a, b;
    int ra, rb;

    memset(&a, 0xEE, sizeof(a));
    memset(&b, 0xEE, sizeof(b));
    ico_cdvd_host_reset();
    ico_vfs_set_disc(iso);
    ra = sceCdSearchFile((struct sceCdlFILE *)&a, name);
    ico_cdvd_host_reset();
    ico_vfs_set_disc(ar);
    rb = sceCdSearchFile((struct sceCdlFILE *)&b, name);
    ico_vfs_set_disc(NULL);
    ico_cdvd_host_reset();
    CHECK(ra == rb);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    if (ra != rb) {
        fprintf(stderr, "  sceCdSearchFile differs for %s (iso %d, archive %d)\n", name, ra, rb);
    }
}

static int copy_truncated(const char *from, const char *to, long cut)
{
    FILE *in = fopen(from, "rb");
    FILE *out = fopen(to, "wb");
    static unsigned char buf[1 << 16];
    long size, done = 0;
    int ok = in != NULL && out != NULL;

    if (ok) {
        fseek(in, 0, SEEK_END);
        size = ftell(in) - cut;
        fseek(in, 0, SEEK_SET);
        while (ok && done < size) {
            size_t want = (size_t)(size - done) < sizeof(buf) ? (size_t)(size - done) : sizeof(buf);

            ok = fread(buf, 1, want, in) == want && fwrite(buf, 1, want, out) == want;
            done += (long)want;
        }
    }
    if (in != NULL) {
        fclose(in);
    }
    if (out != NULL) {
        ok &= fclose(out) == 0;
    }
    return ok ? 0 : -1;
}

/* --- the synthetic image ----------------------------------------------------- */

static int run_synth(const char *dir)
{
    char iso_path[1024], ar_path[1024], tmp_path[1040], bad_path[1040];
    char why[1024];
    IcoExtractResult res;
    IcoArchiveInfo info;
    IcoVfs *iso, *ar;
    FILE *fp;
    uint32_t lsn;
    int calls = 0;
    int ok;

    snprintf(iso_path, sizeof(iso_path), "%s/archive_test_synthetic.iso", dir);
    snprintf(ar_path, sizeof(ar_path), "%s/archive_test_synthetic.o2r", dir);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", ar_path);
    snprintf(bad_path, sizeof(bad_path), "%s.bad", ar_path);
    remove(ar_path);
    if (build_synthetic(iso_path) != 0) {
        fprintf(stderr, "cannot write %s\n", iso_path);
        return 1;
    }
    /* a nonzero tail after the boot file's end, so the archive keeps one */
    fp = fopen(iso_path, "r+b");
    CHECK(fp != NULL);
    if (fp != NULL) {
        CHECK(fseek(fp, SYN_BOOT * 2048L + SYN_BOOT_SIZE + 5, SEEK_SET) == 0);
        CHECK(fputc(0x5A, fp) == 0x5A);
        fclose(fp);
    }

    /* 1. not the PAL disc: refused, nothing left behind */
    CHECK(ico_extract_archive(iso_path, ar_path, 0, NULL, NULL, &res, why, sizeof(why)) != 0);
    printf("archive_test synth: refused as expected: %s\n", why);
    CHECK(strstr(why, "not the ICO PAL disc") != NULL);
    CHECK(!file_exists(ar_path) && !file_exists(tmp_path));
    CHECK(res.rule[0] == '\0' && !res.datadf_ok);
    /* the reason, for the box the player sees: a disc, but the wrong one */
    CHECK(res.wrong_disc && !res.unreadable && !res.cancelled);

    /* 1b. not a disc image at all (a .bin's raw sectors, a damaged file):
       unreadable, not the wrong disc */
    fp = fopen(bad_path, "wb");
    CHECK(fp != NULL);
    if (fp != NULL) {
        static const char junk[4096] = "not a disc image";
        CHECK(fwrite(junk, 1, sizeof(junk), fp) == sizeof(junk));
        fclose(fp);
        CHECK(ico_extract_archive(bad_path, ar_path, 0, NULL, NULL, &res, why, sizeof(why)) != 0);
        CHECK(res.unreadable && !res.wrong_disc);
        CHECK(!file_exists(ar_path) && !file_exists(tmp_path));
        remove(bad_path);
    }

    /* 2. a cancel from the progress callback: nothing left behind */
    cancel_at = 3;
    calls = 0;
    CHECK(ico_extract_archive(iso_path, ar_path, ICO_EXTRACT_NO_VERIFY, progress_cb, &calls, &res,
                              why, sizeof(why)) != 0);
    CHECK(res.cancelled);
    CHECK(!file_exists(ar_path) && !file_exists(tmp_path));
    cancel_at = 0;

    /* 3. unverified extraction (tests only) */
    calls = 0;
    phase_seq[0] = '\0';
    CHECK(ico_extract_archive(iso_path, ar_path, ICO_EXTRACT_NO_VERIFY, progress_cb, &calls, &res,
                              why, sizeof(why)) == 0);
    CHECK(calls > 0);
    /* open, then hash, extract, finish (open and finish at 0 of 0) */
    CHECK(strcmp(phase_seq, "ohef") == 0);
    CHECK(strcmp(res.rule, ICO_RULE_UNVERIFIED) == 0);
    CHECK(strcmp(res.disc_id, "SLUS-00000") == 0);
    CHECK(res.files == 3); /* SYSTEM.CNF, SLUS_000.00, DFDATAS/DATA.DF */
    CHECK(res.bytes == strlen(synCnf) + SYN_BOOT_SIZE + SYN_DATADF_SIZE);
    CHECK(file_exists(ar_path) && !file_exists(tmp_path));
    CHECK(ico_archive_read_info(ar_path, &info, why, sizeof(why)) == 0);
    CHECK(strcmp(info.format, ICO_ARCHIVE_FORMAT) == 0 && info.version == ICO_ARCHIVE_VERSION);
    CHECK(strcmp(info.extractor, ICO_ARCHIVE_EXTRACTOR) == 0);
    CHECK(strcmp(info.accepted_by, ICO_RULE_UNVERIFIED) == 0);
    CHECK(info.volume_sectors == SYN_SECTORS);
    CHECK(info.stored == 3 && info.entries == 5); /* + the root and DFDATAS */
    CHECK(info.file_bytes == res.archive_bytes);
    CHECK(strcmp(info.iso_sha1, res.iso_sha1) == 0 && info.iso_size == SYN_SECTORS * 2048u);
    /* an unverified archive is never played */
    CHECK(!ico_archive_info_acceptable(&info, why, sizeof(why)));
    {
        IcoArchiveInfo good = info;

        snprintf(good.disc_id, sizeof(good.disc_id), "%s", ICO_DISC_ID);
        snprintf(good.accepted_by, sizeof(good.accepted_by), "%s", ICO_RULE_ISO_SHA1);
        snprintf(good.iso_sha1, sizeof(good.iso_sha1), "%s", ICO_DISC_ISO_SHA1);
        snprintf(good.elf_sha1, sizeof(good.elf_sha1), "%s", ICO_DISC_ELF_SHA1);
        CHECK(ico_archive_info_acceptable(&good, why, sizeof(why)));
        good.version = ICO_ARCHIVE_VERSION + 1;
        CHECK(!ico_archive_info_acceptable(&good, why, sizeof(why)));
    }

    /* the entries are stored, the boot file has its tail */
    {
        mz_zip_archive z;
        mz_zip_archive_file_stat st;
        int i, tails = 0;

        memset(&z, 0, sizeof(z));
        CHECK(mz_zip_reader_init_file(&z, ar_path, 0));
        for (i = 0; i < (int)mz_zip_reader_get_num_files(&z); i++) {
            CHECK(mz_zip_reader_file_stat(&z, (mz_uint)i, &st));
            CHECK(st.m_method == 0 && st.m_comp_size == st.m_uncomp_size);
            tails += strncmp(st.m_filename, "tail/", 5) == 0;
        }
        CHECK(mz_zip_reader_locate_file(&z, "disc/DFDATAS/DATA.DF", NULL, 0) >= 0);
        CHECK(mz_zip_reader_locate_file(&z, "disc/SLUS_000.00", NULL, 0) >= 0);
        CHECK(mz_zip_reader_locate_file(&z, "tail/SLUS_000.00", NULL, 0) >= 0);
        CHECK(mz_zip_reader_locate_file(&z, ICO_ARCHIVE_META, NULL, 0) >= 0);
        CHECK(tails == 1);
        mz_zip_reader_end(&z);
    }

    /* 4. the archive answers as the image did */
    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    ar = ico_vfs_mount_archive(ar_path);
    CHECK(iso != NULL && ar != NULL);
    if (iso == NULL || ar == NULL) {
        return 1;
    }
    CHECK(ico_vfs_volume_sectors(ar) == ico_vfs_volume_sectors(iso));
    same_stat(iso, ar, "");
    same_stat(iso, ar, "DFDATAS");
    same_stat(iso, ar, "\\DFDATAS\\DATA.DF;1");
    same_stat(iso, ar, "cdrom0:\\SYSTEM.CNF;1");
    same_stat(iso, ar, "SLUS_000.00");
    same_stat(iso, ar, "NOPE.BIN");
    same_stat(iso, ar, "DFDATAS/ALPHA.PAK");
    /* not extracted: a directory outside DFDATAS */
    CHECK(ico_vfs_stat(iso, "MANY", NULL) == 0 && ico_vfs_stat(ar, "MANY", NULL) != 0);
    CHECK(same_file_sectors(iso, ar, "SYSTEM.CNF"));
    CHECK(same_file_sectors(iso, ar, "SLUS_000.00"));
    CHECK(same_file_sectors(iso, ar, "DFDATAS/DATA.DF"));
    /* runs across files and every in-range start inside DATA.DF */
    ok = 1;
    for (lsn = SYN_DATADF; lsn < SYN_DATADF + 7; lsn++) {
        uint32_t n;

        for (n = 1; lsn + n <= SYN_DATADF + 7; n++) {
            ok &= same_sectors(iso, ar, lsn, n);
        }
    }
    CHECK(ok);
    CHECK(same_sectors(iso, ar, SYN_CNF, 3)); /* SYSTEM.CNF, then the boot file */
    {
        unsigned char sec[2048 * 2];

        /* the boot file's last sector carries the tail byte */
        CHECK(ico_vfs_read_sectors(ar, SYN_BOOT + 1, 1, sec) == 0);
        CHECK(sec[SYN_BOOT_SIZE - 2048 + 5] == 0x5A);
        /* sectors no stored file covers, and past the end of the volume */
        CHECK(ico_vfs_read_sectors(ar, 16, 1, sec) != 0);
        CHECK(ico_vfs_read_sectors(ar, SYN_SHARED, 1, sec) != 0);
        CHECK(ico_vfs_read_sectors(ar, SYN_DATADF + 6, 2, sec) != 0);
        CHECK(ico_vfs_read_sectors(ar, SYN_SECTORS, 1, sec) != 0);
        CHECK(ico_vfs_read_sectors(ar, 0xFFFFFFFFu, 2, sec) != 0);
    }
    CHECK(check_unifile(ar, 3) == 3);
    same_search(iso, ar, "\\DFDATAS\\DATA.DF;1");
    same_search(iso, ar, "\\SLUS_000.00;1");
    same_search(iso, ar, "\\SYSTEM.CNF;1");
    same_search(iso, ar, "\\NOPE.BIN;1");
    same_search(iso, ar, "\\DFDATAS;1");
    /* the libcdvd layer identifies the archive's disc */
    ico_cdvd_host_reset();
    ico_vfs_set_disc(ar);
    CHECK(sceCdGetDiskType() == ICO_CD_TYPE_PS2DVD);
    CHECK(ico_cdvd_host_boot_name() != NULL &&
          strcmp(ico_cdvd_host_boot_name(), "SLUS_000.00") == 0);
    ico_vfs_set_disc(NULL);
    ico_cdvd_host_reset();
    ico_vfs_unmount(ar);
    ico_vfs_unmount(iso);

    /* 5. damaged archives are refused at mount */
    CHECK(copy_truncated(ar_path, bad_path, 100) == 0);
    CHECK(ico_archive_read_info(bad_path, &info, why, sizeof(why)) != 0);
    CHECK(ico_vfs_mount_archive(bad_path) == NULL);
    fp = fopen(bad_path, "wb");
    if (fp != NULL) {
        fputs("not a zip", fp);
        fclose(fp);
    }
    CHECK(ico_archive_read_info(bad_path, &info, why, sizeof(why)) != 0);
    CHECK(ico_archive_read_info("nonexistent.o2r", &info, why, sizeof(why)) != 0);
    remove(bad_path);

    /* 6. a second extraction replaces the archive in place */
    CHECK(ico_extract_archive(iso_path, ar_path, ICO_EXTRACT_NO_VERIFY, NULL, NULL, &res, why,
                              sizeof(why)) == 0);
    CHECK(ico_archive_read_info(ar_path, &info, why, sizeof(why)) == 0);

    remove(ar_path);
    remove(iso_path);
    printf("archive_test synth: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

/* --- the user's disc ----------------------------------------------------------- */

static int run_disc_archive(const char *iso_path, const char *dir)
{
    static unsigned char a[1 << 20], b[1 << 20];
    char ar_path[1024], why[1024], hex[41];
    const char *tmpdir = getenv("TMPDIR");
    IcoExtractResult res;
    IcoArchiveInfo info;
    IcoVfs *iso, *ar;
    IcoVfsFile fi, fa;
    IcoVfsEntry df;
    Sha1 sha;
    uint64_t off;
    uint32_t crc_i, crc_a, count, i, seed;
    unsigned char hdr[4], ent[40];
    int calls = 0;
    int ok;

    if (!file_exists(iso_path)) {
        printf("archive_test disc: SKIPPED: %s is absent (put the SCES-50760 image there)\n",
               iso_path);
        return SKIP;
    }
    snprintf(ar_path, sizeof(ar_path), "%s/archive_test_disc-%ld.o2r",
             tmpdir != NULL && tmpdir[0] != '\0' ? tmpdir : dir, (long)time(NULL));

    /* extraction, verified */
    CHECK(ico_extract_archive(iso_path, ar_path, 0, progress_cb, &calls, &res, why, sizeof(why)) ==
          0);
    if (failures) {
        fprintf(stderr, "archive_test disc: extraction failed: %s\n", why);
        return 1;
    }
    printf("archive_test disc: extracted %u files (%llu bytes) in %.1f s (hash %.1f s, copy "
           "%.1f s): %s, %llu bytes; rule %s\n",
           (unsigned)res.files, (unsigned long long)res.bytes,
           res.hash_seconds + res.extract_seconds, res.hash_seconds, res.extract_seconds, ar_path,
           (unsigned long long)res.archive_bytes, res.rule);
    /* both rules hold for the PAL image: the image's hash, and the ELF plus
       DATA.DF's manifest (what a re-dump with other padding is accepted by) */
    CHECK(strcmp(res.rule, ICO_RULE_ISO_SHA1) == 0);
    CHECK(strcmp(res.iso_sha1, ICO_DISC_ISO_SHA1) == 0);
    CHECK(strcmp(res.elf_sha1, ICO_DISC_ELF_SHA1) == 0);
    CHECK(res.datadf_ok);
    if (!res.datadf_ok) {
        fprintf(stderr, "  DATA.DF manifest: %s\n", res.datadf_why);
    }
    CHECK(strcmp(res.disc_id, ICO_DISC_ID) == 0);
    CHECK(ico_archive_read_info(ar_path, &info, why, sizeof(why)) == 0);
    CHECK(ico_archive_info_acceptable(&info, why, sizeof(why)));

    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    ar = ico_vfs_mount_archive(ar_path);
    CHECK(iso != NULL && ar != NULL);
    if (iso == NULL || ar == NULL) {
        return 1;
    }
    CHECK(ico_vfs_volume_sectors(ar) == ico_vfs_volume_sectors(iso));
    printf("archive_test disc: volume %u sectors on both\n", (unsigned)ico_vfs_volume_sectors(ar));

    /* the boot ELF read back from the archive */
    CHECK(ico_vfs_open(ar, "\\SCES_507.60;1", &fa) == 0);
    sha1_init(&sha);
    for (off = 0; off < fa.entry.size;) {
        int64_t n = ico_vfs_read(&fa, off, a, sizeof(a));

        if (n <= 0) {
            CHECK(n > 0);
            break;
        }
        sha1_update(&sha, a, (size_t)n);
        off += (uint64_t)n;
    }
    sha1_final_hex(&sha, hex);
    printf("archive_test disc: SCES_507.60 from the archive: SHA-1 %s\n", hex);
    CHECK(strcmp(hex, ICO_DISC_ELF_SHA1) == 0);

    /* look-ups and the small files, sector for sector */
    {
        static const char *const paths[] = {
            "",
            "DFDATAS",
            "DFDATAS/DATA.DF",
            "SYSTEM.CNF",
            "SCES_507.60",
            "SNDN2DRV.IRX",
            "LIBSD.IRX",
            "SIO2MAN.IRX",
            "PADMAN.IRX",
            "MCMAN.IRX",
            "MCSERV.IRX",
            "MCXMAN.IRX",
            "MCXSERV.IRX",
            "PANICSYS.IRX",
            "DUMMY.TXT",
        };

        for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
            same_stat(iso, ar, paths[i]);
            if (i >= 3) {
                CHECK(same_file_sectors(iso, ar, paths[i]));
            }
        }
    }
    same_search(iso, ar, "\\SCES_507.60;1");
    same_search(iso, ar, "\\DFDATAS\\DATA.DF;1");
    same_search(iso, ar, "\\SYSTEM.CNF;1");
    same_search(iso, ar, "\\SNDN2DRV.IRX;1");
    same_search(iso, ar, "\\NOPE.BIN;1");
    same_search(iso, ar, "\\DFDATAS;1");
    ico_cdvd_host_reset();
    ico_vfs_set_disc(ar);
    CHECK(sceCdGetDiskType() == ICO_CD_TYPE_PS2DVD);
    CHECK(ico_cdvd_host_boot_name() != NULL &&
          strcmp(ico_cdvd_host_boot_name(), "SCES_507.60") == 0);
    ico_vfs_set_disc(NULL);
    ico_cdvd_host_reset();

    /* all of DATA.DF, through both backends: equal chunk by chunk, CRC-32 */
    CHECK(ico_vfs_open(iso, "DFDATAS/DATA.DF", &fi) == 0);
    CHECK(ico_vfs_open(ar, "DFDATAS/DATA.DF", &fa) == 0);
    df = fi.entry;
    crc_i = crc_a = (uint32_t)mz_crc32(MZ_CRC32_INIT, NULL, 0);
    ok = 1;
    for (off = 0; off < df.size;) {
        int64_t ni = ico_vfs_read(&fi, off, a, sizeof(a));
        int64_t na = ico_vfs_read(&fa, off, b, sizeof(b));

        if (ni <= 0 || ni != na) {
            ok = 0;
            break;
        }
        ok &= memcmp(a, b, (size_t)ni) == 0;
        crc_i = (uint32_t)mz_crc32(crc_i, a, (size_t)ni);
        crc_a = (uint32_t)mz_crc32(crc_a, b, (size_t)na);
        off += (uint64_t)ni;
    }
    CHECK(ok && off == df.size);
    CHECK(crc_i == crc_a);
    printf("archive_test disc: DATA.DF, %u bytes, CRC-32 %08x on both\n", (unsigned)df.size,
           (unsigned)crc_a);

    /* the directory's members by LSN as cdvd.c addresses them: a sample */
    CHECK(ico_vfs_read(&fa, 0, hdr, 4) == 4);
    count = rd32(hdr);
    CHECK(count == ico_extract_datadf_manifest_count());
    CHECK(check_unifile(ar, -1) == (int)count);
    ok = 1;
    for (i = 0; i < count; i++) {
        uint32_t moff, msize, mlsn, n, s;

        if (i % 6 != 0 && i != count - 1) {
            continue;
        }
        CHECK(ico_vfs_read(&fa, 4 + (uint64_t)i * 40, ent, 40) == 40);
        moff = rd32(ent + 32);
        msize = rd32(ent + 36);
        mlsn = df.lsn + moff / 2048;
        n = ico_vfs_size_to_sectors(msize);
        for (s = 0; s < n; s += 64) {
            ok &= same_sectors(iso, ar, mlsn + s, n - s < 64 ? n - s : 64);
        }
    }
    CHECK(ok);

    /* arbitrary sector runs inside DATA.DF */
    ok = 1;
    seed = 12345u;
    for (i = 0; i < 4000; i++) {
        uint32_t nsec = ico_vfs_size_to_sectors(df.size);
        uint32_t lsn, n;

        seed = seed * 1664525u + 1013904223u;
        lsn = df.lsn + (seed >> 8) % nsec;
        seed = seed * 1664525u + 1013904223u;
        n = 1 + (seed >> 16) % 64;
        if (lsn + n > df.lsn + nsec) {
            n = df.lsn + nsec - lsn;
        }
        ok &= same_sectors(iso, ar, lsn, n);
    }
    ok &= same_sectors(iso, ar, df.lsn, 1);
    ok &= same_sectors(iso, ar, df.lsn + ico_vfs_size_to_sectors(df.size) - 1, 1);
    /* over DATA.DF's end into DUMMY.TXT, the next file on the disc */
    ok &= same_sectors(iso, ar, df.lsn + ico_vfs_size_to_sectors(df.size) - 2, 3);
    CHECK(ok);

    ico_vfs_unmount(ar);
    ico_vfs_unmount(iso);
    remove(ar_path);
    printf("archive_test disc: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

static int run_manifest(const char *iso_path)
{
    IcoVfs *iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    int r;

    if (iso == NULL) {
        return 1;
    }
    r = ico_extract_print_datadf_manifest(iso, stdout);
    ico_vfs_unmount(iso);
    return r == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "synth") == 0) {
        return run_synth(argv[2]);
    }
    if (argc == 4 && strcmp(argv[1], "disc") == 0) {
        return run_disc_archive(argv[2], argv[3]);
    }
    if (argc == 3 && strcmp(argv[1], "manifest") == 0) {
        return run_manifest(argv[2]);
    }
    fprintf(stderr, "usage: %s synth <dir> | disc <iso> <dir> | manifest <iso>\n", argv[0]);
    return 2;
}
