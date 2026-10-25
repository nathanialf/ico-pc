/*
 * port/data/test/archive_test.c
 *
 * The archive backend and the first-run extractor (port/data/archive.c,
 * extract.c).
 *
 *   archive_test synth <dir>         vfs_test's synthetic ISO9660 image,
 *                                    extracted into <dir> and compared with
 *                                    the ISO backend (no disc needed)
 *   archive_test shift <dir>         a synthetic image whose directory lists
 *                                    DATA.DF 5 sectors before its real start
 *                                    (the Korean disc's layout, df_lsn_shift
 *                                    in archive.h): the ISO backend's
 *                                    shifted entry, the libcdvd search, the
 *                                    extraction and the archive (no disc)
 *   archive_test sub <dir>           a synthetic image whose BOOT2 is a menu
 *                                    and whose game is a folder (the June
 *                                    2001 magazine demo's layout, archive.h
 *                                    game_dir), with a row the test supplies:
 *                                    the folder's files under their paths,
 *                                    no image hash, the ELF and DATA.DF rule,
 *                                    the game's requests through libcdvd
 *   archive_test disc <iso> <dir> [slug]
 *                                    the user's image of the disc `slug`
 *                                    (a slug of config/regions.txt, pal by
 *                                    default): extract
 *                                    into a temporary archive (in $TMPDIR
 *                                    when set, else <dir>), mount it and
 *                                    compare it with the image; 77
 *                                    (skipped) without it
 *   archive_test manifest [--region slug] <iso>
 *                                    print the DATA.DF manifest block for
 *                                    discs.c (region pal by default)
 *   archive_test manifest-check <slug> <iso> <discs.c>
 *                                    the block the image gives against the
 *                                    one discs.c holds for that region,
 *                                    number by number; 77 without the image
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
#include "df_pack.h"
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
    int di;
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

    /* 1. none of the discs: refused, nothing left behind; the reason names the discs */
    CHECK(ico_extract_archive(iso_path, ar_path, 0, NULL, NULL, &res, why, sizeof(why)) != 0);
    printf("archive_test synth: refused as expected: %s\n", why);
    CHECK(strstr(why, "is not a disc image this port plays") != NULL &&
          strstr(why, "SCES-50760") != NULL && strstr(why, "SCPS-11003") != NULL);
    CHECK(ico_extract_identify(iso_path, NULL, 0) == NULL);
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
        /* the box text: the image's path, then every disc's name (both %s filled) */
        CHECK(strstr(why, "Cannot read the disc image") != NULL &&
              strstr(why, "SCPS-56001") != NULL);
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
    for (di = 0; di < ICO_REGION_COUNT; di++) {
        const IcoDiscInfo *d = &ico_discs[di];
        IcoArchiveInfo good = info;

        snprintf(good.disc_id, sizeof(good.disc_id), "%s", d->id);
        snprintf(good.accepted_by, sizeof(good.accepted_by), "%s",
                 d->iso_sha1 != NULL ? ICO_RULE_ISO_SHA1 : ICO_RULE_ELF_DATADF);
        snprintf(good.iso_sha1, sizeof(good.iso_sha1), "%s",
                 d->iso_sha1 != NULL ? d->iso_sha1 : info.iso_sha1);
        snprintf(good.elf_sha1, sizeof(good.elf_sha1), "%s", d->elf_sha1);
        CHECK(ico_archive_info_acceptable(&good, why, sizeof(why)));
        /* the other disc's boot ELF under this disc's id */
        snprintf(good.elf_sha1, sizeof(good.elf_sha1), "%s",
                 ico_discs[(di + 1) % ICO_REGION_COUNT].elf_sha1);
        CHECK(!ico_archive_info_acceptable(&good, why, sizeof(why)));
        snprintf(good.elf_sha1, sizeof(good.elf_sha1), "%s", d->elf_sha1);
        good.version = ICO_ARCHIVE_VERSION + 1;
        CHECK(!ico_archive_info_acceptable(&good, why, sizeof(why)));
    }
    {
        IcoArchiveInfo other = info;

        snprintf(other.disc_id, sizeof(other.disc_id), "SLES-00000");
        snprintf(other.accepted_by, sizeof(other.accepted_by), "%s", ICO_RULE_ISO_SHA1);
        CHECK(!ico_archive_info_acceptable(&other, why, sizeof(why)));
    }
    /* the table: distinct ids, slugs and archives; look-ups both ways */
    {
        int a, b;

        for (a = 0; a < ICO_REGION_COUNT; a++) {
            for (b = a + 1; b < ICO_REGION_COUNT; b++) {
                CHECK(strcmp(ico_discs[a].id, ico_discs[b].id) != 0 &&
                      strcmp(ico_discs[a].slug, ico_discs[b].slug) != 0 &&
                      strcmp(ico_discs[a].archive_name, ico_discs[b].archive_name) != 0 &&
                      strcmp(ico_discs[a].image_base, ico_discs[b].image_base) != 0);
                /* a boot file is shared only where the boot ELFs differ, which is what
                   ico_disc_identify decides by */
                CHECK(strcmp(ico_discs[a].boot_elf, ico_discs[b].boot_elf) != 0 ||
                      strcmp(ico_discs[a].elf_sha1, ico_discs[b].elf_sha1) != 0);
            }
        }
    }
    CHECK(ico_disc_by_id("SCES-50760") == &ico_discs[0] &&
          ico_disc_by_id("SCPS-11003") == &ico_discs[1] &&
          ico_disc_by_id("SCPS-56001") == &ico_discs[2] &&
          ico_disc_by_id("SCUS-97113") == &ico_discs[ICO_REGION_NTSC] &&
          ico_disc_by_id("PROTO-20010806") == &ico_discs[ICO_REGION_AUG6] &&
          ico_disc_by_id("SCUS-97158") == &ico_discs[ICO_REGION_JUN26] &&
          ico_disc_by_id("SCPS-55001") == &ico_discs[ICO_REGION_CN] &&
          ico_disc_by_id("SLUS-20000") == NULL);
    CHECK(ico_disc_by_boot("SCES_507.60") == &ico_discs[0] &&
          ico_disc_by_boot("SCPS_110.03") == &ico_discs[1] &&
          ico_disc_by_boot("SCPS_560.01") == &ico_discs[2] &&
          ico_disc_by_boot("SCUS_971.58") == &ico_discs[ICO_REGION_JUN26] &&
          ico_disc_by_boot("SCPS_550.01") == &ico_discs[ICO_REGION_CN] &&
          ico_disc_by_boot("") == NULL);
    /* SCUS_971.13 boots two discs: the first row answers by name, the boot ELF's SHA-1 decides */
    CHECK(ico_disc_boot_rows("SCUS_971.13") == 2 && ico_disc_boot_rows("SCES_507.60") == 1 &&
          ico_disc_boot_rows("SLUS_000.00") == 0 && ico_disc_boot_rows(NULL) == 0);
    CHECK(ico_disc_by_boot("SCUS_971.13") == &ico_discs[ICO_REGION_NTSC] &&
          ico_discs[ICO_REGION_NTSC].df_lsn_shift == ico_discs[ICO_REGION_AUG6].df_lsn_shift &&
          ico_discs[ICO_REGION_NTSC].cd_type == ico_discs[ICO_REGION_AUG6].cd_type);
    CHECK(ico_disc_by_boot_elf("SCUS_971.13", ico_discs[ICO_REGION_NTSC].elf_sha1) ==
              &ico_discs[ICO_REGION_NTSC] &&
          ico_disc_by_boot_elf("SCUS_971.13", ico_discs[ICO_REGION_AUG6].elf_sha1) ==
              &ico_discs[ICO_REGION_AUG6] &&
          ico_disc_by_boot_elf("SCUS_971.13", ico_discs[0].elf_sha1) == NULL &&
          ico_disc_by_boot_elf("SCUS_971.13", NULL) == NULL);
    /* ico_disc_identify: the given hash picks the row; a name only one row has needs none;
       a volume without the shared name's ELF (the synthetic image has no SCUS_971.13) is no
       disc when no hash is given. The hash read from a real SCUS_971.13 is the disc boot's
       (ico_pc logs it once: "boot file SCUS_971.13 is shared by 2 discs"). */
    {
        IcoVfs *syn = ico_vfs_mount(&ico_vfs_iso9660, iso_path);

        CHECK(syn != NULL);
        CHECK(ico_disc_identify(syn, "SCUS_971.13", ico_discs[ICO_REGION_NTSC].elf_sha1) ==
              &ico_discs[ICO_REGION_NTSC]);
        CHECK(ico_disc_identify(syn, "SCUS_971.13", ico_discs[ICO_REGION_AUG6].elf_sha1) ==
              &ico_discs[ICO_REGION_AUG6]);
        CHECK(ico_disc_identify(syn, "SCUS_971.13", ico_discs[0].elf_sha1) == NULL);
        CHECK(ico_disc_identify(syn, "SCUS_971.13", NULL) == NULL);
        CHECK(ico_disc_identify(syn, "SCPS_110.03", NULL) == &ico_discs[1]);
        CHECK(ico_disc_identify(syn, "SCUS_971.58", NULL) == &ico_discs[ICO_REGION_JUN26]);
        CHECK(ico_disc_identify(syn, "SCPS_550.01", NULL) == &ico_discs[ICO_REGION_CN]);
        CHECK(ico_disc_identify(syn, "SLUS_000.00", NULL) == NULL);
        ico_vfs_unmount(syn);
    }
    CHECK(ico_discs[0].df_count == 193 && ico_discs[1].df_count == 183 &&
          ico_discs[2].df_count == 183 && ico_discs[ICO_REGION_NTSC].df_count == 172 &&
          ico_discs[ICO_REGION_AUG6].df_count == 172 &&
          ico_discs[ICO_REGION_JUN26].df_count == 49 && ico_discs[ICO_REGION_CN].df_count == 183);
    /* the magazine demo's game lives in a folder and has no image hash on file */
    CHECK(strcmp(ico_discs[ICO_REGION_JUN26].game_dir, "CUS97113") == 0 &&
          ico_discs[ICO_REGION_JUN26].iso_sha1 == NULL &&
          strcmp(ico_discs[ICO_REGION_JUN26].df_path, "CUS97113/DFDATAS/DATA.DF") == 0 &&
          strcmp(ico_discs[ICO_REGION_JUN26].elf_path, "CUS97113/SCPS_000.00") == 0 &&
          ico_discs[ICO_REGION_NTSC].iso_sha1 != NULL &&
          ico_discs[ICO_REGION_AUG6].iso_sha1 != NULL);
    /* only a disc with a shifted DATA.DF carries a shift (the Korean and Asian discs) */
    CHECK(ico_discs[0].df_lsn_shift == 0 && ico_discs[1].df_lsn_shift == 0 &&
          ico_discs[2].df_lsn_shift == 153600 && ico_discs[ICO_REGION_NTSC].df_lsn_shift == 0 &&
          ico_discs[ICO_REGION_AUG6].df_lsn_shift == 0 &&
          ico_discs[ICO_REGION_JUN26].df_lsn_shift == 0 &&
          ico_discs[ICO_REGION_CN].df_lsn_shift == 153600);
    CHECK(ico_discs[2].df_size == 866859008u - 153600u * 2048u);
    /* the Asian disc is a DVD with its DATA.DF 153600 sectors into the 867,420,160-byte file */
    CHECK(ico_discs[ICO_REGION_CN].df_size == 867420160u - 153600u * 2048u &&
          ico_discs[ICO_REGION_CN].cd_type == 0x14 && ico_discs[ICO_REGION_CN].iso_sha1 != NULL &&
          strcmp(ico_discs[ICO_REGION_CN].game_dir, "") == 0);

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
    {
        /* ico_vfs_reopen: a second archive mount of its own (FILE* and
           entry table), reading the same sectors interleaved with the
           first, and the first still reading after it is gone */
        IcoVfs *two = ico_vfs_reopen(ar);

        CHECK(two != NULL);
        if (two != NULL) {
            same_stat(ar, two, "\\DFDATAS\\DATA.DF;1");
            same_stat(ar, two, "SLUS_000.00");
            CHECK(same_file_sectors(ar, two, "DFDATAS/DATA.DF"));
            CHECK(same_sectors(ar, two, SYN_CNF, 3));
            ico_vfs_unmount(two);
            CHECK(same_sectors(iso, ar, SYN_CNF, 3));
        }
    }
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

static int run_disc_archive(const char *iso_path, const char *dir, const char *slug)
{
    static unsigned char a[1 << 20], b[1 << 20];
    char ar_path[1024], why[1024], hex[41], boot[64], search[80], dfsearch[80];
    const IcoDiscInfo *disc = NULL;
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

    for (i = 0; i < ICO_REGION_COUNT; i++) {
        if (strcmp(ico_discs[i].slug, slug) == 0) {
            disc = &ico_discs[i];
        }
    }
    if (disc == NULL) {
        fprintf(stderr, "archive_test disc: no disc row '%s'\n", slug);
        return 2;
    }
    if (!file_exists(iso_path)) {
        printf("archive_test disc: SKIPPED: %s is absent (put the %s image there)\n", iso_path,
               disc->id);
        return SKIP;
    }
    /* which disc the image is, by its boot file */
    CHECK(ico_extract_identify(iso_path, boot, sizeof(boot)) == disc);
    CHECK(strcmp(boot, disc->boot_elf) == 0);
    /* the game's ELF and DATA.DF as the game asks for them ("\\CUS97113\\DFDATAS\\DATA.DF;1"
       on a disc whose game is in a folder) */
    snprintf(search, sizeof(search), "\\%s;1", disc->elf_path);
    snprintf(dfsearch, sizeof(dfsearch), "\\%s;1", disc->df_path);
    for (i = 0; search[i] != '\0'; i++) {
        search[i] = search[i] == '/' ? '\\' : search[i];
    }
    for (i = 0; dfsearch[i] != '\0'; i++) {
        dfsearch[i] = dfsearch[i] == '/' ? '\\' : dfsearch[i];
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
    /* both rules hold for the image: the image's hash, and the ELF plus
       DATA.DF's manifest (what a re-dump with other padding is accepted by) */
    CHECK(strcmp(res.rule, disc->iso_sha1 != NULL ? ICO_RULE_ISO_SHA1 : ICO_RULE_ELF_DATADF) == 0);
    CHECK((disc->iso_sha1 == NULL || strcmp(res.iso_sha1, disc->iso_sha1) == 0) &&
          res.iso_size == disc->iso_bytes);
    CHECK(strcmp(res.elf_sha1, disc->elf_sha1) == 0);
    CHECK(res.datadf_ok);
    if (!res.datadf_ok) {
        fprintf(stderr, "  DATA.DF manifest: %s\n", res.datadf_why);
    }
    CHECK(strcmp(res.disc_id, disc->id) == 0);
    CHECK(ico_archive_read_info(ar_path, &info, why, sizeof(why)) == 0);
    CHECK(ico_archive_info_acceptable(&info, why, sizeof(why)));
    /* meta.json names the disc and the rule; the row's ELF is the one */
    CHECK(strcmp(info.disc_id, disc->id) == 0 &&
          strcmp(info.accepted_by,
                 disc->iso_sha1 != NULL ? ICO_RULE_ISO_SHA1 : ICO_RULE_ELF_DATADF) == 0 &&
          strcmp(info.elf_sha1, disc->elf_sha1) == 0);
    /* a disc with no image SHA-1 on file is not hashed: the source's SHA-1 is its ELF's */
    CHECK(res.iso_hashed == (disc->iso_sha1 != NULL));
    CHECK(disc->iso_sha1 != NULL || strcmp(info.iso_sha1, disc->elf_sha1) == 0);
    printf("archive_test disc: meta.json: disc %s, accepted by %s\n", info.disc_id,
           info.accepted_by);

    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    ar = ico_vfs_mount_archive(ar_path);
    CHECK(iso != NULL && ar != NULL);
    if (iso == NULL || ar == NULL) {
        return 1;
    }
    CHECK(ico_vfs_volume_sectors(ar) == ico_vfs_volume_sectors(iso));
    printf("archive_test disc: volume %u sectors on both\n", (unsigned)ico_vfs_volume_sectors(ar));
    /* DATA.DF: the volume reports the entry the archive header starts (the
       row's df_lsn_shift past the directory's LSN, the size that much
       shorter): the header decodes to the manifest's member count, and the
       size is the manifest's */
    {
        IcoVfsFile dfi;
        unsigned char head[4];

        CHECK(ico_vfs_open(iso, disc->df_path, &dfi) == 0);
        CHECK(dfi.entry.size == disc->df_size);
        CHECK(ico_vfs_read(&dfi, 0, head, 4) == 4 && rd32(head) == disc->df_count);
        /* the directory's own record lies df_lsn_shift sectors earlier */
        ico_vfs_iso9660_force_df_shift(0);
        {
            IcoVfs *raw = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
            IcoVfsEntry rawe;

            CHECK(raw != NULL && ico_vfs_stat(raw, disc->df_path, &rawe) == 0);
            if (raw != NULL) {
                CHECK(rawe.lsn + disc->df_lsn_shift == dfi.entry.lsn);
                CHECK(rawe.size == dfi.entry.size + disc->df_lsn_shift * ICO_VFS_SECTOR);
                ico_vfs_unmount(raw);
            }
        }
        ico_vfs_iso9660_force_df_shift(-1);
    }

    /* the boot ELF read back from the archive */
    CHECK(ico_vfs_open(ar, search, &fa) == 0);
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
    printf("archive_test disc: %s from the archive: SHA-1 %s\n", disc->elf_path, hex);
    CHECK(strcmp(hex, disc->elf_sha1) == 0);

    if (disc->game_dir[0] != '\0') {
        /* a disc whose game is in a folder (the magazine demo): the folder's ELF, its
           six modules and DATA.DF, SYSTEM.CNF; the menu (the boot file) listed, not stored */
        static const char *const mods[] = {"SNDN2DRV.IRX", "LIBSD.IRX", "SIO2MAN.IRX",
                                           "PADMAN.IRX",   "MCMAN.IRX", "MCSERV.IRX"};
        char p[96];
        IcoVfsEntry e;

        same_stat(iso, ar, "");
        same_stat(iso, ar, disc->game_dir);
        snprintf(p, sizeof(p), "%s/DFDATAS", disc->game_dir);
        same_stat(iso, ar, p);
        same_stat(iso, ar, disc->boot_elf);
        CHECK(ico_vfs_stat(ar, disc->boot_elf, &e) == 0 &&
              ico_vfs_read_sectors(ar, e.lsn, 1, a) != 0);
        same_stat(iso, ar, "SYSTEM.CNF");
        CHECK(same_file_sectors(iso, ar, "SYSTEM.CNF"));
        same_stat(iso, ar, disc->elf_path);
        CHECK(same_file_sectors(iso, ar, disc->elf_path));
        for (i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
            snprintf(p, sizeof(p), "%s/%s", disc->game_dir, mods[i]);
            same_stat(iso, ar, p);
            CHECK(same_file_sectors(iso, ar, p));
            snprintf(p, sizeof(p), "cdrom0:\\%s\\%s;1", disc->game_dir, mods[i]);
            same_search(iso, ar, p);
        }
        CHECK(res.files == 9);
        /* the menu's root and the other games' folders are not kept */
        CHECK(ico_vfs_stat(ar, "SCE_DD", NULL) != 0 && ico_vfs_stat(ar, "DFDATAS", NULL) != 0);
    } else {
        /* look-ups and the small files, sector for sector: the root files each
           disc has (the Japanese and Korean discs have no MCXMAN, MCXSERV, PANICSYS or
           DUMMY.TXT; its IOPRP224.IMG is not stored) */
        {
            static const char *const pal[] = {
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
            /* the discs without those files (jpn, kor): the boot file is the
               row's, the fifth path */
            char bootPath[32];
            const char *other[] = {
                "",           "DFDATAS",      "DFDATAS/DATA.DF", "SYSTEM.CNF",
                bootPath,     "SNDN2DRV.IRX", "LIBSD.IRX",       "SIO2MAN.IRX",
                "PADMAN.IRX", "MCMAN.IRX",    "MCSERV.IRX",
            };
            const int isPal = disc == &ico_discs[0];
            const char *const *paths = isPal ? pal : other;
            const uint32_t n = isPal ? (uint32_t)(sizeof(pal) / sizeof(pal[0]))
                                     : (uint32_t)(sizeof(other) / sizeof(other[0]));

            snprintf(bootPath, sizeof(bootPath), "%s", disc->boot_elf);
            for (i = 0; i < n; i++) {
                same_stat(iso, ar, paths[i]);
                if (i >= 3) {
                    CHECK(same_file_sectors(iso, ar, paths[i]));
                }
            }
            /* the 9 files stored from the Japanese and Korean discs: 8 in
               the root and DATA.DF; the August 2001 preview's root holds
               the European root's files (its 9 IRX, DUMMY.TXT): 13 */
            if (!isPal && strcmp(disc->slug, "aug6") == 0) {
                CHECK(res.files == 13);
                CHECK(ico_vfs_stat(ar, "MCXMAN.IRX", NULL) == 0 &&
                      ico_vfs_stat(ar, "DUMMY.TXT", NULL) == 0 &&
                      ico_vfs_stat(ar, "IOPRP224.IMG", NULL) != 0);
            } else if (!isPal) {
                CHECK(res.files == 9);
                CHECK(ico_vfs_stat(ar, "MCXMAN.IRX", NULL) != 0 &&
                      ico_vfs_stat(ar, "DUMMY.TXT", NULL) != 0 &&
                      ico_vfs_stat(ar, "IOPRP224.IMG", NULL) != 0);
            }
        }
    }
    same_search(iso, ar, search);
    same_search(iso, ar, dfsearch);
    same_search(iso, ar, "\\DFDATAS\\DATA.DF;1");
    same_search(iso, ar, "\\SYSTEM.CNF;1");
    same_search(iso, ar, "\\SNDN2DRV.IRX;1");
    same_search(iso, ar, "\\NOPE.BIN;1");
    same_search(iso, ar, "\\DFDATAS;1");
    ico_cdvd_host_reset();
    ico_vfs_set_disc(ar);
    CHECK(sceCdGetDiskType() == disc->cd_type);
    CHECK(ico_cdvd_host_boot_name() != NULL &&
          strcmp(ico_cdvd_host_boot_name(), disc->boot_elf) == 0);
    ico_vfs_set_disc(NULL);
    ico_cdvd_host_reset();

    /* all of DATA.DF, through both backends: equal chunk by chunk, CRC-32 */
    CHECK(ico_vfs_open(iso, disc->df_path, &fi) == 0);
    CHECK(ico_vfs_open(ar, disc->df_path, &fa) == 0);
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
    CHECK(count == disc->df_count);
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
    /* over DATA.DF's end into DUMMY.TXT, the next file on the PAL disc (on
       the Japanese disc nothing follows DATA.DF) */
    if (disc == &ico_discs[0]) {
        ok &= same_sectors(iso, ar, df.lsn + ico_vfs_size_to_sectors(df.size) - 2, 3);
    }
    CHECK(ok);

    ico_vfs_unmount(ar);
    ico_vfs_unmount(iso);
    remove(ar_path);
    printf("archive_test disc: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

/* --- a DATA.DF the directory lists too early ------------------------------------ */

/* The Korean disc's layout in miniature: the directory puts DATA.DF at
   SH_DIR_LSN, the archive header lies SH_SHIFT sectors later (the sectors
   between are filler), and the directory keeps the size of the whole span.
   The image names no disc of the table (SLUS_000.00), so the shift is the
   test's own (ico_vfs_iso9660_force_df_shift). */
#define SH_SHIFT 5
#define SH_SECTORS 30
#define SH_ROOT 18
#define SH_DFDATAS 19
#define SH_CNF 20
#define SH_BOOT 21
#define SH_DIR_LSN 22
#define SH_REAL (SH_DIR_LSN + SH_SHIFT)
#define SH_REAL_SIZE 5000
#define SH_BOOT_SIZE 100

static int build_shifted(const char *path)
{
    static unsigned char img[SH_SECTORS * 2048];
    unsigned char *s;
    unsigned o, d;
    uint32_t i, rootSize;
    FILE *fp;

    memset(img, 0, sizeof(img));

    s = img + SH_ROOT * 2048;
    o = 0;
    o += put_rec(s + o, SH_ROOT, 0, 1, "\0", 1);
    o += put_rec(s + o, SH_ROOT, 0, 1, "\1", 1);
    o += put_named(s + o, SH_DFDATAS, 0, 1, "DFDATAS");
    o += put_named(s + o, SH_BOOT, SH_BOOT_SIZE, 0, "SLUS_000.00;1");
    o += put_named(s + o, SH_CNF, (uint32_t)strlen(synCnf), 0, "SYSTEM.CNF;1");
    rootSize = o;
    put32both(s + 10, rootSize);
    put32both(s + 34 + 10, rootSize);

    s = img + SH_DFDATAS * 2048;
    d = 0;
    d += put_rec(s + d, SH_DFDATAS, 0, 1, "\0", 1);
    d += put_rec(s + d, SH_ROOT, rootSize, 1, "\1", 1);
    d += put_named(s + d, SH_DIR_LSN, SH_SHIFT * 2048u + SH_REAL_SIZE, 0, "DATA.DF;1");
    put32both(s + 10, d);
    put32both(img + SH_ROOT * 2048 + 34 + 34 + 10, d);

    s = img + 16 * 2048;
    s[0] = 1;
    memcpy(s + 1, "CD001", 5);
    s[6] = 1;
    put32both(s + 80, SH_SECTORS);
    s[128] = 0x00;
    s[129] = 0x08;
    put_rec(s + 156, SH_ROOT, rootSize, 1, "\0", 1);
    s = img + 17 * 2048;
    s[0] = 255;
    memcpy(s + 1, "CD001", 5);
    s[6] = 1;

    memcpy(img + SH_CNF * 2048, synCnf, strlen(synCnf));
    for (i = 0; i < SH_BOOT_SIZE; i++) {
        img[SH_BOOT * 2048 + i] = synthByte(SH_BOOT, i);
    }
    /* the filler before the archive: nonzero, and not a directory */
    memset(img + SH_DIR_LSN * 2048, 0xEE, SH_SHIFT * 2048);
    /* the archive: a unifile directory of one member at offset 2048 */
    s = img + SH_REAL * 2048;
    put32le(s, 1);
    strcpy((char *)s + 4, "alpha.pak");
    put32le(s + 4 + 32, 2048);
    put32le(s + 4 + 36, 100);
    for (i = 2048; i < SH_REAL_SIZE; i++) {
        s[i] = synthByte(SH_REAL, i);
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

static int run_shift(const char *dir)
{
    char iso_path[1024], ar_path[1024], why[1024];
    unsigned char buf[2048];
    IcoVfs *iso, *ar;
    IcoVfsEntry e, ea;
    IcoVfsFile f;
    IcoExtractResult res;
    IcoArchiveInfo info;
    sceCdlFILE fp;
    uint32_t i;
    int ok;

    snprintf(iso_path, sizeof(iso_path), "%s/archive_test_shift.iso", dir);
    snprintf(ar_path, sizeof(ar_path), "%s/archive_test_shift.o2r", dir);
    remove(ar_path);
    if (build_shifted(iso_path) != 0) {
        fprintf(stderr, "cannot write %s\n", iso_path);
        return 1;
    }

    /* a disc not in the table: the directory's entry as it is */
    ico_vfs_iso9660_force_df_shift(-1);
    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    CHECK(iso != NULL);
    if (iso == NULL) {
        return 1;
    }
    CHECK(ico_vfs_stat(iso, "DFDATAS/DATA.DF", &e) == 0);
    CHECK(e.lsn == SH_DIR_LSN && e.size == SH_SHIFT * 2048u + SH_REAL_SIZE);
    CHECK(ico_vfs_read_sectors(iso, e.lsn, 1, buf) == 0 && buf[0] == 0xEE);
    ico_vfs_unmount(iso);

    /* the shift: lsn + SH_SHIFT, size - SH_SHIFT sectors, whatever the spelling */
    ico_vfs_iso9660_force_df_shift(SH_SHIFT);
    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    CHECK(iso != NULL);
    if (iso == NULL) {
        ico_vfs_iso9660_force_df_shift(-1);
        return 1;
    }
    CHECK(ico_vfs_stat(iso, "DFDATAS/DATA.DF", &e) == 0);
    CHECK(e.lsn == SH_REAL && e.size == SH_REAL_SIZE);
    CHECK(ico_vfs_stat(iso, "\\DFDATAS\\DATA.DF;1", &ea) == 0 && ea.lsn == SH_REAL &&
          ea.size == SH_REAL_SIZE);
    CHECK(ico_vfs_stat(iso, "dfdatas/data.df", &ea) == 0 && ea.lsn == SH_REAL);
    /* nothing else moves */
    CHECK(ico_vfs_stat(iso, "SYSTEM.CNF", &ea) == 0 && ea.lsn == SH_CNF);
    CHECK(ico_vfs_stat(iso, "SLUS_000.00", &ea) == 0 && ea.lsn == SH_BOOT);
    CHECK(ico_vfs_stat(iso, "DFDATAS", &ea) == 0 && ea.lsn == SH_DFDATAS);
    /* the archive header and a member's bytes */
    CHECK(ico_vfs_open(iso, "DFDATAS/DATA.DF", &f) == 0);
    CHECK(ico_vfs_read(&f, 0, buf, 44) == 44);
    CHECK(rd32(buf) == 1 && memcmp(buf + 4, "alpha.pak", 10) == 0);
    CHECK(rd32(buf + 4 + 32) == 2048 && rd32(buf + 4 + 36) == 100);
    ok = ico_vfs_read(&f, 2048, buf, 100) == 100;
    for (i = 0; i < 100; i++) {
        ok &= buf[i] == synthByte(SH_REAL, 2048 + i);
    }
    CHECK(ok);
    CHECK(ico_vfs_read(&f, 4900, buf, 500) == 100); /* short at the file's end */
    CHECK(ico_vfs_read(&f, SH_REAL_SIZE, buf, 10) == 0);

    /* libcdvd answers the search with the shifted span */
    ico_cdvd_host_reset();
    ico_vfs_set_disc(iso);
    memset(&fp, 0xEE, sizeof(fp));
    CHECK(sceCdSearchFile((struct sceCdlFILE *)&fp, "\\DFDATAS\\DATA.DF;1") == 1);
    CHECK(fp.lsn == SH_REAL && fp.size == SH_REAL_SIZE);
    ico_vfs_set_disc(NULL);
    ico_cdvd_host_reset();
    ico_vfs_unmount(iso);

    /* a shift the directory's size cannot hold: no entry */
    ico_vfs_iso9660_force_df_shift(1000);
    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    CHECK(iso != NULL);
    if (iso != NULL) {
        CHECK(ico_vfs_stat(iso, "DFDATAS/DATA.DF", NULL) != 0);
        ico_vfs_unmount(iso);
    }

    /* the extraction: the archive holds the shifted span at the shifted LSN */
    ico_vfs_iso9660_force_df_shift(SH_SHIFT);
    CHECK(ico_extract_archive(iso_path, ar_path, ICO_EXTRACT_NO_VERIFY, NULL, NULL, &res, why,
                              sizeof(why)) == 0);
    if (failures) {
        fprintf(stderr, "archive_test shift: extraction failed: %s\n", why);
    }
    CHECK(res.files == 3); /* SYSTEM.CNF, SLUS_000.00, DFDATAS/DATA.DF */
    CHECK(res.bytes == strlen(synCnf) + SH_BOOT_SIZE + SH_REAL_SIZE);
    CHECK(ico_archive_read_info(ar_path, &info, why, sizeof(why)) == 0);
    CHECK(info.volume_sectors == SH_SECTORS);
    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    ar = ico_vfs_mount_archive(ar_path);
    CHECK(iso != NULL && ar != NULL);
    if (iso != NULL && ar != NULL) {
        CHECK(ico_vfs_stat(ar, "DFDATAS/DATA.DF", &ea) == 0 && ea.lsn == SH_REAL &&
              ea.size == SH_REAL_SIZE);
        same_stat(iso, ar, "DFDATAS/DATA.DF");
        CHECK(same_file_sectors(iso, ar, "DFDATAS/DATA.DF"));
        CHECK(ico_vfs_open(ar, "DFDATAS/DATA.DF", &f) == 0);
        CHECK(ico_vfs_read(&f, 0, buf, 4) == 4 && rd32(buf) == 1);
        same_search(iso, ar, "\\DFDATAS\\DATA.DF;1");
    }
    ico_vfs_unmount(iso);
    ico_vfs_unmount(ar);
    ico_vfs_iso9660_force_df_shift(-1);
    remove(ar_path);
    remove(iso_path);
    printf("archive_test shift: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

/* --- a disc whose game is in a folder ------------------------------------------- */

/* The June 2001 magazine demo's layout in miniature: SYSTEM.CNF's BOOT2 is a
   menu at the root (SLUS_999.99), the game is the folder GAME with its ELF,
   its modules and its own DFDATAS/DATA.DF, and the root holds files of the
   menu and of other games (ROOT.IRX, MODULES/) the archive leaves out. No
   row of the table boots SLUS_999.99, so the test supplies one
   (ico_disc_set_test_row) with no image SHA-1, as the demo's row has. */
#define SUB_SECTORS 40
#define SUB_ROOT 18
#define SUB_GAME 19
#define SUB_DFDATAS 20
#define SUB_MODULES 21
#define SUB_CNF 22
#define SUB_MENU 23 /* the root's boot file: the menu */
#define SUB_ELF 24  /* two sectors: GAME/SLUS_000.00 */
#define SUB_LIBSD 26
#define SUB_IOPRP 27
#define SUB_ROOT_IRX 28
#define SUB_X_IRX 29
#define SUB_DATADF 30 /* seven sectors */
#define SUB_OTHER 37
#define SUB_README 38
#define SUB_MENU_SIZE 100
#define SUB_ELF_SIZE 3000
#define SUB_LIBSD_SIZE 500
#define SUB_SMALL_SIZE 100
#define SUB_DATADF_SIZE 12289
#define SUB_OTHER_SIZE 200
#define SUB_README_SIZE 50

static const char subCnf[] = "BOOT2 = cdrom0:\\SLUS_999.99;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n";
static unsigned char subImg[SUB_SECTORS * 2048];
static char subElfSha1[41];
static IcoDfManifestEntry subMembers[3];
static IcoDiscInfo subRow = {
    .id = "SLUS-99999",
    .slug = "sub",
    .boot_elf = "SLUS_999.99",
    .game_dir = "GAME",
    .elf_path = "GAME/SLUS_000.00",
    .df_path = "GAME/DFDATAS/DATA.DF",
    .iso_sha1 = NULL,
    .elf_sha1 = subElfSha1,
    .iso_bytes = SUB_SECTORS * 2048u,
    .df_size = SUB_DATADF_SIZE,
    .df_count = 3,
    .members = subMembers,
    .member_count = 3,
    .archive_name = "ico-sub.o2r",
    .image_base = "Ico_SUB",
    .cd_type = ICO_CD_TYPE_PS2DVD,
    .product_name = "the test's folder disc",
};

/* a directory's "." and "..", its records, and its own size in "." */
static unsigned put_dir_head(unsigned char *s, uint32_t self, uint32_t parent)
{
    unsigned o = put_rec(s, self, 0, 1, "\0", 1);

    return o + put_rec(s + o, parent, 0, 1, "\1", 1);
}

static void fill(uint32_t lsn, uint32_t size)
{
    uint32_t i;

    for (i = 0; i < size; i++) {
        subImg[lsn * 2048 + i] = synthByte(lsn, i);
    }
}

static void build_subfolder(void)
{
    unsigned char *s;
    unsigned o;
    uint32_t dfdSize, modSize, gameSize, rootSize, i;
    Sha1 sha;

    memset(subImg, 0, sizeof(subImg));
    s = subImg + SUB_DFDATAS * 2048;
    o = put_dir_head(s, SUB_DFDATAS, SUB_GAME);
    o += put_named(s + o, SUB_DATADF, SUB_DATADF_SIZE, 0, "DATA.DF;1");
    o += put_named(s + o, SUB_OTHER, SUB_OTHER_SIZE, 0, "OTHER.BIN;1");
    dfdSize = o;
    put32both(s + 10, dfdSize);

    s = subImg + SUB_MODULES * 2048;
    o = put_dir_head(s, SUB_MODULES, SUB_ROOT);
    o += put_named(s + o, SUB_X_IRX, SUB_SMALL_SIZE, 0, "X.IRX;1");
    modSize = o;
    put32both(s + 10, modSize);

    s = subImg + SUB_GAME * 2048;
    o = put_dir_head(s, SUB_GAME, SUB_ROOT);
    o += put_named(s + o, SUB_DFDATAS, dfdSize, 1, "DFDATAS");
    o += put_named(s + o, SUB_IOPRP, SUB_SMALL_SIZE, 0, "IOPRP.IMG;1");
    o += put_named(s + o, SUB_LIBSD, SUB_LIBSD_SIZE, 0, "LIBSD.IRX;1");
    o += put_named(s + o, SUB_README, SUB_README_SIZE, 0, "README.TXT;1");
    o += put_named(s + o, SUB_ELF, SUB_ELF_SIZE, 0, "SLUS_000.00;1");
    gameSize = o;
    put32both(s + 10, gameSize);

    s = subImg + SUB_ROOT * 2048;
    o = put_dir_head(s, SUB_ROOT, SUB_ROOT);
    o += put_named(s + o, SUB_GAME, gameSize, 1, "GAME");
    o += put_named(s + o, SUB_MODULES, modSize, 1, "MODULES");
    o += put_named(s + o, SUB_ROOT_IRX, SUB_SMALL_SIZE, 0, "ROOT.IRX;1");
    o += put_named(s + o, SUB_MENU, SUB_MENU_SIZE, 0, "SLUS_999.99;1");
    o += put_named(s + o, SUB_CNF, (uint32_t)strlen(subCnf), 0, "SYSTEM.CNF;1");
    rootSize = o;
    put32both(s + 10, rootSize);
    put32both(s + 34 + 10, rootSize);

    s = subImg + 16 * 2048;
    s[0] = 1;
    memcpy(s + 1, "CD001", 5);
    s[6] = 1;
    put32both(s + 80, SUB_SECTORS);
    s[128] = 0x00;
    s[129] = 0x08;
    put_rec(s + 156, SUB_ROOT, rootSize, 1, "\0", 1);
    s = subImg + 17 * 2048;
    s[0] = 255;
    memcpy(s + 1, "CD001", 5);
    s[6] = 1;

    memcpy(subImg + SUB_CNF * 2048, subCnf, strlen(subCnf));
    fill(SUB_MENU, SUB_MENU_SIZE);
    fill(SUB_ELF, SUB_ELF_SIZE);
    fill(SUB_LIBSD, SUB_LIBSD_SIZE);
    fill(SUB_IOPRP, SUB_SMALL_SIZE);
    fill(SUB_ROOT_IRX, SUB_SMALL_SIZE);
    fill(SUB_X_IRX, SUB_SMALL_SIZE);
    fill(SUB_OTHER, SUB_OTHER_SIZE);
    fill(SUB_README, SUB_README_SIZE);
    /* DATA.DF: the synthetic image's directory of three members */
    s = subImg + SUB_DATADF * 2048;
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
    for (i = 2048; i < SUB_DATADF_SIZE; i++) {
        s[i] = synthByte(SUB_DATADF, i);
    }

    /* the row's verification values, computed from the image as discs.c's are from a disc */
    sha1_init(&sha);
    sha1_update(&sha, subImg + SUB_ELF * 2048, SUB_ELF_SIZE);
    sha1_final_hex(&sha, subElfSha1);
    subRow.df_dir_crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, s, 4 + 40 * 3);
    for (i = 0; i < 3; i++) {
        subMembers[i].offset = rd32(s + 4 + 40 * i + 32);
        subMembers[i].size = rd32(s + 4 + 40 * i + 36);
        subMembers[i].crc =
            (uint32_t)mz_crc32(MZ_CRC32_INIT, s + subMembers[i].offset, subMembers[i].size);
    }
}

static int write_image(const char *path, const unsigned char *img, size_t n)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        return -1;
    }
    if (fwrite(img, 1, n, fp) != n) {
        fclose(fp);
        return -1;
    }
    return fclose(fp);
}

/* An image with one byte changed at (lsn, off) is refused by the ELF and
   DATA.DF rule, with `reason` in the text and nothing left behind. */
static void check_refused(const char *dir, uint32_t lsn, uint32_t off, const char *reason)
{
    static unsigned char bad[sizeof(subImg)];
    char path[1024], ar_path[1040], why[1024], hex[41];
    IcoExtractResult res;

    snprintf(path, sizeof(path), "%s/archive_test_sub_bad.iso", dir);
    snprintf(ar_path, sizeof(ar_path), "%s.o2r", path);
    memcpy(bad, subImg, sizeof(bad));
    bad[lsn * 2048 + off] ^= 0x01;
    CHECK(write_image(path, bad, sizeof(bad)) == 0);
    CHECK(ico_extract_verify_files(path, &subRow, hex, why, sizeof(why)) != 0);
    CHECK(strstr(why, reason) != NULL);
    if (strstr(why, reason) == NULL) {
        fprintf(stderr, "  verify: \"%s\" lacks \"%s\"\n", why, reason);
    }
    CHECK(ico_extract_archive(path, ar_path, 0, NULL, NULL, &res, why, sizeof(why)) != 0);
    CHECK(res.wrong_disc && res.rule[0] == '\0' && !res.iso_hashed);
    CHECK(strstr(why, "is not a disc image this port plays") != NULL &&
          strstr(why, "(not hashed)") != NULL);
    CHECK(!file_exists(ar_path));
    remove(path);
}

static int run_sub(const char *dir)
{
    static const char *const stored[] = {
        "SYSTEM.CNF",           "GAME/SLUS_000.00",       "GAME/LIBSD.IRX",
        "GAME/DFDATAS/DATA.DF", "GAME/DFDATAS/OTHER.BIN",
    };
    static const char *const left_out[] = {
        "ROOT.IRX", "MODULES", "MODULES/X.IRX", "GAME/IOPRP.IMG", "GAME/README.TXT",
    };
    char iso_path[1024], ar_path[1024], why[1024], hex[41], boot[64], path[64];
    IcoExtractResult res;
    IcoArchiveInfo info;
    IcoVfs *iso, *ar;
    unsigned char sec[2048], buf[16];
    sceCdlFILE fp;
    int calls = 0;
    uint32_t i;
    int ok;

    snprintf(iso_path, sizeof(iso_path), "%s/archive_test_sub.iso", dir);
    snprintf(ar_path, sizeof(ar_path), "%s/archive_test_sub.o2r", dir);
    remove(ar_path);
    build_subfolder();
    if (write_image(iso_path, subImg, sizeof(subImg)) != 0) {
        fprintf(stderr, "cannot write %s\n", iso_path);
        return 1;
    }

    /* 1. no row boots the menu: no disc, and the walk keeps the root's files only */
    ico_disc_set_test_row(NULL);
    CHECK(ico_extract_identify(iso_path, NULL, 0) == NULL);
    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    CHECK(iso != NULL && ico_vfs_game_dir(iso)[0] == '\0' &&
          strcmp(ico_vfs_df_path(iso), "DFDATAS/DATA.DF") == 0);
    ico_vfs_unmount(iso);
    CHECK(ico_extract_archive(iso_path, ar_path, ICO_EXTRACT_NO_VERIFY, NULL, NULL, &res, why,
                              sizeof(why)) == 0);
    CHECK(res.files == 3 && res.iso_hashed); /* SYSTEM.CNF, SLUS_999.99, ROOT.IRX */
    remove(ar_path);

    /* 2. the row: the folder, DATA.DF's path, a file's path under the folder */
    ico_disc_set_test_row(&subRow);
    CHECK(ico_extract_identify(iso_path, boot, sizeof(boot)) == &subRow &&
          strcmp(boot, "SLUS_999.99") == 0);
    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    CHECK(iso != NULL);
    if (iso == NULL) {
        ico_disc_set_test_row(NULL);
        return 1;
    }
    CHECK(strcmp(ico_vfs_game_dir(iso), "GAME") == 0 &&
          strcmp(ico_vfs_df_path(iso), "GAME/DFDATAS/DATA.DF") == 0);
    CHECK(ico_vfs_game_path(iso, "LIBSD.IRX", path, sizeof(path)) == 0 &&
          strcmp(path, "GAME/LIBSD.IRX") == 0);
    CHECK(ico_vfs_game_path(iso, "LIBSD.IRX", path, 8) != 0);
    CHECK(ico_vfs_game_dir(NULL)[0] == '\0' &&
          strcmp(ico_vfs_df_path(NULL), "DFDATAS/DATA.DF") == 0);
    ico_vfs_unmount(iso);

    /* 3. the start-from-the-image check: the game ELF and DATA.DF, no image hash */
    CHECK(ico_extract_verify_files(iso_path, &subRow, hex, why, sizeof(why)) == 0);
    CHECK(strcmp(hex, subElfSha1) == 0);

    /* 4. the extraction, verified: no hash phase, the ELF and DATA.DF rule, the
       game ELF's SHA-1 as the source's */
    phase_seq[0] = '\0';
    CHECK(ico_extract_archive(iso_path, ar_path, 0, progress_cb, &calls, &res, why, sizeof(why)) ==
          0);
    if (failures) {
        fprintf(stderr, "archive_test sub: extraction failed: %s\n", why);
    }
    CHECK(strcmp(phase_seq, "oef") == 0);
    CHECK(strcmp(res.rule, ICO_RULE_ELF_DATADF) == 0 && !res.iso_hashed && res.datadf_ok);
    CHECK(strcmp(res.disc_id, "SLUS-99999") == 0);
    CHECK(res.iso_size == SUB_SECTORS * 2048u);
    CHECK(strcmp(res.elf_sha1, subElfSha1) == 0 && strcmp(res.iso_sha1, subElfSha1) == 0);
    CHECK(res.files == 5);
    CHECK(res.bytes ==
          strlen(subCnf) + SUB_ELF_SIZE + SUB_LIBSD_SIZE + SUB_DATADF_SIZE + SUB_OTHER_SIZE);
    CHECK(ico_archive_read_info(ar_path, &info, why, sizeof(why)) == 0);
    CHECK(strcmp(info.accepted_by, ICO_RULE_ELF_DATADF) == 0 &&
          strcmp(info.iso_sha1, subElfSha1) == 0 && info.iso_size == SUB_SECTORS * 2048u);
    /* the root, GAME, GAME/DFDATAS, the five files and the menu listed without data */
    CHECK(info.stored == 5 && info.entries == 9);
    CHECK(ico_archive_info_acceptable(&info, why, sizeof(why)));
    {
        /* the image rule for a row with no image hash on file is refused */
        IcoArchiveInfo other = info;

        snprintf(other.accepted_by, sizeof(other.accepted_by), "%s", ICO_RULE_ISO_SHA1);
        CHECK(!ico_archive_info_acceptable(&other, why, sizeof(why)));
    }
    {
        mz_zip_archive z;

        memset(&z, 0, sizeof(z));
        CHECK(mz_zip_reader_init_file(&z, ar_path, 0));
        for (i = 0; i < sizeof(stored) / sizeof(stored[0]); i++) {
            snprintf(path, sizeof(path), "disc/%s", stored[i]);
            CHECK(mz_zip_reader_locate_file(&z, path, NULL, 0) >= 0);
        }
        for (i = 0; i < sizeof(left_out) / sizeof(left_out[0]); i++) {
            snprintf(path, sizeof(path), "disc/%s", left_out[i]);
            CHECK(mz_zip_reader_locate_file(&z, path, NULL, 0) < 0);
        }
        /* the menu is not stored */
        CHECK(mz_zip_reader_locate_file(&z, "disc/SLUS_999.99", NULL, 0) < 0);
        mz_zip_reader_end(&z);
    }

    /* 5. the archive answers the folder's paths as the image does */
    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    ar = ico_vfs_mount_archive(ar_path);
    CHECK(iso != NULL && ar != NULL);
    if (iso == NULL || ar == NULL) {
        ico_disc_set_test_row(NULL);
        return 1;
    }
    CHECK(strcmp(ico_vfs_df_path(ar), "GAME/DFDATAS/DATA.DF") == 0);
    same_stat(iso, ar, "");
    same_stat(iso, ar, "GAME");
    same_stat(iso, ar, "GAME/DFDATAS");
    same_stat(iso, ar, "\\GAME\\DFDATAS\\DATA.DF;1");
    same_stat(iso, ar, "cdrom0:\\GAME\\LIBSD.IRX;1");
    same_stat(iso, ar, "SLUS_999.99");
    same_stat(iso, ar, "DFDATAS/DATA.DF"); /* neither: the root has no DFDATAS */
    for (i = 0; i < sizeof(stored) / sizeof(stored[0]); i++) {
        same_stat(iso, ar, stored[i]);
        CHECK(same_file_sectors(iso, ar, stored[i]));
    }
    for (i = 0; i < sizeof(left_out) / sizeof(left_out[0]); i++) {
        CHECK(ico_vfs_stat(iso, left_out[i], NULL) == 0 &&
              ico_vfs_stat(ar, left_out[i], NULL) != 0);
    }
    /* the menu's entry answers, its bytes are not there */
    CHECK(ico_vfs_read_sectors(ar, SUB_MENU, 1, sec) != 0);
    /* the game's requests through libcdvd: the same records from both */
    same_search(iso, ar, "\\GAME\\DFDATAS\\DATA.DF;1");
    same_search(iso, ar, "cdrom0:\\GAME\\LIBSD.IRX;1");
    same_search(iso, ar, "\\GAME\\SLUS_000.00;1");
    same_search(iso, ar, "\\SLUS_999.99;1");
    same_search(iso, ar, "\\DFDATAS\\DATA.DF;1");
    ico_cdvd_host_reset();
    ico_vfs_set_disc(ar);
    memset(&fp, 0xEE, sizeof(fp));
    CHECK(sceCdSearchFile((struct sceCdlFILE *)&fp, "cdrom0:\\GAME\\DFDATAS\\DATA.DF;1") == 1);
    CHECK(fp.lsn == SUB_DATADF && fp.size == SUB_DATADF_SIZE);
    /* the drive's answers for the archive: the row's type and the menu as the boot file */
    CHECK(sceCdGetDiskType() == ICO_CD_TYPE_PS2DVD);
    CHECK(ico_cdvd_host_boot_name() != NULL &&
          strcmp(ico_cdvd_host_boot_name(), "SLUS_999.99") == 0);
    ico_vfs_set_disc(NULL);
    ico_cdvd_host_reset();
    /* the port's own DATA.DF reads (df_pack.h) find the folder's */
    CHECK(ico_df_has(ar, "beta.int") == 1 && ico_df_size(ar, "beta.int") == 5000);
    CHECK(ico_df_read(ar, "beta.int", 0, buf, sizeof(buf)) == (int64_t)sizeof(buf));
    ok = 1;
    for (i = 0; i < sizeof(buf); i++) {
        ok &= buf[i] == synthByte(SUB_DATADF, 4096 + i);
    }
    CHECK(ok);
    ico_vfs_unmount(ar);
    ico_vfs_unmount(iso);

    /* 6. a changed game ELF, a changed DATA.DF member: refused without the image hash */
    check_refused(dir, SUB_ELF, 1000, "GAME/SLUS_000.00 SHA-1");
    check_refused(dir, SUB_DATADF, 4096 + 7, "DATA.DF member 1");

    ico_disc_set_test_row(NULL);
    remove(ar_path);
    remove(iso_path);
    printf("archive_test sub: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

static int run_manifest(const char *slug, const char *iso_path)
{
    IcoVfs *iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    int r;

    if (iso == NULL) {
        return 1;
    }
    r = ico_extract_print_datadf_manifest(iso, slug, stdout);
    ico_vfs_unmount(iso);
    return r == 0 ? 0 : 1;
}

/* The numbers of a manifest block, in order (the defines' values and every
   entry's), from its BEGIN line to its END line; the text's layout, which
   clang-format sets in discs.c, does not count. 0, or -1 when the block
   is missing. */
static int block_numbers(const char *text, const char *slug, unsigned long *out, size_t max,
                         size_t *n)
{
    char begin[64], end[64];
    const char *p, *e;

    snprintf(begin, sizeof(begin), "/* BEGIN DATA.DF MANIFEST %s */", slug);
    snprintf(end, sizeof(end), "/* END DATA.DF MANIFEST %s */", slug);
    p = strstr(text, begin);
    e = p != NULL ? strstr(p, end) : NULL;
    if (p == NULL || e == NULL) {
        return -1;
    }
    p += strlen(begin);
    *n = 0;
    while (p < e) {
        if (*p >= '0' && *p <= '9' && !(p[-1] == '_' || (p[-1] >= 'A' && p[-1] <= 'z'))) {
            char *q;
            unsigned long v = strtoul(p, &q, 0);

            if (*n < max) {
                out[*n] = v;
            }
            (*n)++;
            p = q;
        } else {
            p++;
        }
    }
    return 0;
}

static char *read_text(FILE *f)
{
    char *buf = NULL;
    size_t n = 0, cap = 0;
    int c;

    while ((c = fgetc(f)) != EOF) {
        if (n + 2 > cap) {
            char *nb = realloc(buf, cap ? cap * 2 : 65536);

            if (nb == NULL) {
                free(buf);
                return NULL;
            }
            buf = nb;
            cap = cap ? cap * 2 : 65536;
        }
        buf[n++] = (char)c;
    }
    if (buf != NULL) {
        buf[n] = '\0';
    }
    return buf;
}

/* The block the image gives against the one discs.c holds. */
static int run_manifest_check(const char *slug, const char *iso_path, const char *source)
{
    static unsigned long want[4096], got[4096];
    size_t nw = 0, ng = 0, i;
    char *committed = NULL, *printed = NULL;
    FILE *f, *t;
    IcoVfs *iso;
    int r;

    if (!file_exists(iso_path)) {
        printf("archive_test manifest-check: SKIPPED: %s is absent\n", iso_path);
        return SKIP;
    }
    f = fopen(source, "rb");
    t = tmpfile();
    iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    CHECK(f != NULL && t != NULL && iso != NULL);
    if (f == NULL || t == NULL || iso == NULL) {
        return 1;
    }
    committed = read_text(f);
    fclose(f);
    r = ico_extract_print_datadf_manifest(iso, slug, t);
    ico_vfs_unmount(iso);
    CHECK(r == 0);
    rewind(t);
    printed = read_text(t);
    fclose(t);
    CHECK(committed != NULL && printed != NULL);
    if (committed != NULL && printed != NULL) {
        CHECK(block_numbers(committed, slug, want, 4096, &nw) == 0);
        CHECK(block_numbers(printed, slug, got, 4096, &ng) == 0);
        CHECK(nw == ng && nw > 3 && nw <= 4096);
        for (i = 0; i < nw && i < ng && i < 4096; i++) {
            if (want[i] != got[i]) {
                fprintf(stderr, "manifest %s: number %u: discs.c has %lu, the image gives %lu\n",
                        slug, (unsigned)i, want[i], got[i]);
                CHECK(want[i] == got[i]);
                break;
            }
        }
        printf("archive_test manifest-check %s: %u numbers (%lu bytes, %lu members, directory "
               "CRC-32 %08lx) match discs.c\n",
               slug, (unsigned)ng, ng > 0 ? got[0] : 0ul, ng > 1 ? got[1] : 0ul,
               ng > 2 ? got[2] : 0ul);
    }
    free(committed);
    free(printed);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "synth") == 0) {
        return run_synth(argv[2]);
    }
    if ((argc == 4 || argc == 5) && strcmp(argv[1], "disc") == 0) {
        return run_disc_archive(argv[2], argv[3], argc == 5 ? argv[4] : "pal");
    }
    if (argc == 3 && strcmp(argv[1], "shift") == 0) {
        return run_shift(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "sub") == 0) {
        return run_sub(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "manifest") == 0) {
        return run_manifest("pal", argv[2]);
    }
    if (argc == 5 && strcmp(argv[1], "manifest") == 0 && strcmp(argv[2], "--region") == 0) {
        return run_manifest(argv[3], argv[4]);
    }
    if (argc == 5 && strcmp(argv[1], "manifest-check") == 0) {
        return run_manifest_check(argv[2], argv[3], argv[4]);
    }
    fprintf(stderr,
            "usage: %s synth <dir> | shift <dir> | sub <dir> | disc <iso> <dir> [slug] | manifest "
            "[--region "
            "slug] <iso> | manifest-check <slug> <iso> <discs.c>\n",
            argv[0]);
    return 2;
}
