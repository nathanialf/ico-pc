/*
 * port/platform/test/dump_zip_test.c
 *
 * F12's zip (dump_zip.h): a fake .rddump and log written under the build
 * folder, zipped, and read back with miniz's reader: the entry names are
 * the files' own names, the sizes and bytes match, a missing log leaves the
 * dump alone in the zip, and a missing dump writes nothing.  The aspect
 * sweep's zip (ico_sweep_zip): <aspect>.rddump and <aspect>.png for each of
 * 4x3, 16x9, 32x9, 48x9 and 60x9, 4:3 first, the loose files gone after; an
 * aspect missing a file is left out; none to zip writes nothing.  The
 * device-loss zip (ico_loss_dump_zip): the lost frame's dump first, the one
 * before it, the log, the loose dumps gone after; without the one before,
 * the lost one and the log; the second .rddump (ico_dump_zip_extract_nth),
 * also of F12's pair (ico_frame_dump_zip with the tick before); its path
 * (ico_loss_dump_path); the pruning
 * (ico_loss_dump_prune) keeps the newest loss zips by name and leaves
 * every other file alone.  ico_zip_add_file: the bug-report picture added to
 * a finished zip, its entries kept.
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dump_zip.h"
#include "host_fs.h"
#include "miniz.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int put(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    const int ok = f != NULL && fwrite(data, 1, n, f) == n;

    if (f != NULL) {
        fclose(f);
    }
    return ok ? 0 : -1;
}

/* the entry called name in z: its index, or -1 */
static int find(mz_zip_archive *z, const char *name)
{
    return mz_zip_reader_locate_file(z, name, NULL, 0);
}

/* 1 when entry name holds exactly the n bytes of data */
static int entry_is(mz_zip_archive *z, const char *name, const void *data, size_t n)
{
    mz_zip_archive_file_stat st;
    char back[4096];
    const int i = find(z, name);

    if (i < 0 || !mz_zip_reader_file_stat(z, (mz_uint)i, &st) || st.m_uncomp_size != n ||
        n > sizeof(back)) {
        return 0;
    }
    return mz_zip_reader_extract_to_mem(z, (mz_uint)i, back, sizeof(back), 0) &&
           memcmp(back, data, n) == 0;
}

static void test_loss(const char *base)
{
    char dir[1200], lost[1300], before[1300], log[1300], zip[1300], name[64], why[160];
    const char a[] = "the lost frame", b[] = "the frame before", text[] = "rhi_vk: device lost\n";
    mz_zip_archive z;
    size_t size = 0;

    snprintf(dir, sizeof(dir), "%s/loss-test", base);
    ico_mkdir(dir);
    snprintf(lost, sizeof(lost), "%s/frame-121-lost.rddump", dir);
    snprintf(before, sizeof(before), "%s/frame-120-before.rddump", dir);
    snprintf(log, sizeof(log), "%s/ico-pc.log", dir);
    CHECK(ico_loss_dump_path(zip, sizeof(zip), dir, "20261015-101500") == 0);
    CHECK(strstr(zip, "/loss-20261015-101500.zip") != NULL);
    CHECK(ico_loss_dump_path(zip, 8, dir, "20261015-101500") == -1);
    CHECK(ico_loss_dump_path(zip, sizeof(zip), dir, "20261015-101500") == 0);
    CHECK(put(lost, a, sizeof(a)) == 0);
    CHECK(put(before, b, sizeof(b)) == 0);
    CHECK(put(log, text, sizeof(text) - 1) == 0);
    CHECK(ico_loss_dump_zip(zip, lost, before, log, why, sizeof(why)) == 0);
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_reader_init_file(&z, zip, 0));
    CHECK(mz_zip_reader_get_num_files(&z) == 3);
    CHECK(find(&z, "frame-121-lost.rddump") == 0);
    CHECK(entry_is(&z, "frame-121-lost.rddump", a, sizeof(a)));
    CHECK(entry_is(&z, "frame-120-before.rddump", b, sizeof(b)));
    CHECK(entry_is(&z, "ico-pc.log", text, sizeof(text) - 1));
    mz_zip_reader_end(&z);
    CHECK(ico_path_kind(lost, NULL, NULL) == -1);
    CHECK(ico_path_kind(before, NULL, NULL) == -1);
    CHECK(ico_path_kind(log, NULL, NULL) == 0);
    /* the loss bisect's dump: the first .rddump, the lost frame */
    void *got = ico_dump_zip_extract(zip, &size, name, sizeof(name));
    CHECK(got != NULL && size == sizeof(a) && memcmp(got, a, size) == 0);
    CHECK(strcmp(name, "frame-121-lost.rddump") == 0);
    free(got);
    /* the tick before, the zip's second .rddump (rd_replay_tool --interp) */
    got = ico_dump_zip_extract_nth(zip, 1, &size, name, sizeof(name));
    CHECK(got != NULL && size == sizeof(b) && memcmp(got, b, size) == 0);
    CHECK(strcmp(name, "frame-120-before.rddump") == 0);
    free(got);
    CHECK(ico_dump_zip_extract_nth(zip, 2, &size, NULL, 0) == NULL);
    /* F12's pair: the frame, then the tick before, then the log */
    CHECK(put(lost, a, sizeof(a)) == 0);
    CHECK(put(before, b, sizeof(b)) == 0);
    CHECK(ico_frame_dump_zip(zip, lost, before, log, why, sizeof(why)) == 0);
    got = ico_dump_zip_extract_nth(zip, 1, &size, NULL, 0);
    CHECK(got != NULL && size == sizeof(b) && memcmp(got, b, size) == 0);
    free(got);
    CHECK(ico_path_kind(before, NULL, NULL) == -1);
    /* no frame before it: the lost one and the log */
    CHECK(put(lost, a, sizeof(a)) == 0);
    CHECK(ico_loss_dump_zip(zip, lost, NULL, log, why, sizeof(why)) == 0);
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_reader_init_file(&z, zip, 0));
    CHECK(mz_zip_reader_get_num_files(&z) == 2);
    mz_zip_reader_end(&z);
    /* a missing lost dump writes nothing */
    remove(zip);
    CHECK(ico_loss_dump_zip(zip, lost, NULL, log, why, sizeof(why)) == -1);
    CHECK(ico_path_kind(zip, NULL, NULL) == -1);
    /* the pruning: five loss zips, the newest three kept, the log and a
       zip of another name untouched */
    static const char *const stamps[5] = {"20261015-090000", "20261013-235959", "20261015-101500",
                                          "20261014-120000", "20261015-101501"};
    for (int i = 0; i < 5; i++) {
        CHECK(ico_loss_dump_path(zip, sizeof(zip), dir, stamps[i]) == 0);
        CHECK(put(zip, "z", 1) == 0);
    }
    snprintf(zip, sizeof(zip), "%s/frame-x-v1.zip", dir);
    CHECK(put(zip, "z", 1) == 0);
    CHECK(ico_loss_dump_prune(dir, 3) == 2);
    for (int i = 0; i < 5; i++) {
        CHECK(ico_loss_dump_path(zip, sizeof(zip), dir, stamps[i]) == 0);
        /* 20261013 and 20261014 were the oldest */
        CHECK(ico_path_kind(zip, NULL, NULL) == (i == 1 || i == 3 ? -1 : 0));
        remove(zip);
    }
    snprintf(zip, sizeof(zip), "%s/frame-x-v1.zip", dir);
    CHECK(ico_path_kind(zip, NULL, NULL) == 0);
    remove(zip);
    CHECK(ico_path_kind(log, NULL, NULL) == 0);
    remove(log);
    snprintf(zip, sizeof(zip), "%s/no-such-folder", dir);
    CHECK(ico_loss_dump_prune(zip, 3) == -1);
    ico_rmdir(dir);
}

/* ico_zip_add_file: the bug-report picture added to a finished zip; the
   entries it held stay with their bytes, the new one is there, a missing
   file or zip changes nothing */
static void test_add(const char *base)
{
    char dump[1200], log[1200], png[1200], zip[1200], why[160];
    char rd[700], pic[5000], text[] = "a log\n";
    size_t added = 0;
    mz_zip_archive z;

    snprintf(dump, sizeof(dump), "%s/add-v1.rddump", base);
    snprintf(log, sizeof(log), "%s/add.log", base);
    snprintf(png, sizeof(png), "%s/add-present.png", base);
    snprintf(zip, sizeof(zip), "%s/add.zip", base);
    for (size_t i = 0; i < sizeof(rd); i++) {
        rd[i] = (char)(i * 5 + 1);
    }
    for (size_t i = 0; i < sizeof(pic); i++) {
        pic[i] = (char)(i * 3 + (i >> 4));
    }
    CHECK(put(dump, rd, sizeof(rd)) == 0);
    CHECK(put(log, text, sizeof(text) - 1) == 0);
    CHECK(put(png, pic, sizeof(pic)) == 0);
    CHECK(ico_frame_dump_zip(zip, dump, NULL, log, why, sizeof(why)) == 0);
    CHECK(ico_zip_add_file(zip, png, "add-present.png", why, sizeof(why), &added) == 0);
    CHECK(added == sizeof(pic));
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_reader_init_file(&z, zip, 0));
    CHECK(mz_zip_reader_get_num_files(&z) == 3);
    CHECK(entry_is(&z, "add-v1.rddump", rd, sizeof(rd)));
    CHECK(entry_is(&z, "add.log", text, sizeof(text) - 1));
    CHECK(entry_is(&z, "add-present.png", pic, sizeof(pic)));
    mz_zip_reader_end(&z);
    /* the first .rddump is still the frame */
    {
        void *first = ico_dump_zip_extract(zip, &added, NULL, 0);

        CHECK(first != NULL && added == sizeof(rd));
        free(first);
    }
    /* a missing picture: refused, the zip as it was */
    remove(png);
    why[0] = '\0';
    CHECK(ico_zip_add_file(zip, png, "x.png", why, sizeof(why), NULL) == -1);
    CHECK(why[0] != '\0');
    CHECK(mz_zip_reader_init_file(&z, zip, 0));
    CHECK(mz_zip_reader_get_num_files(&z) == 3);
    mz_zip_reader_end(&z);
    remove(zip);
    CHECK(ico_zip_add_file(zip, log, "x.txt", why, sizeof(why), NULL) == -1);
    remove(log);
}

int main(int argc, char **argv)
{
    char dump[1200], log[1200], zip[1200], why[160];
    char rd[3000], text[] = "window: a log line\nwindow: another\n";
    mz_zip_archive z;

    if (argc < 2) {
        printf("dump_zip_test: needs a folder\n");
        return 2;
    }
    snprintf(dump, sizeof(dump), "%s/frame-x-v1.rddump", argv[1]);
    snprintf(log, sizeof(log), "%s/ico-pc.log", argv[1]);
    snprintf(zip, sizeof(zip), "%s/frame-x-v1.zip", argv[1]);
    for (size_t i = 0; i < sizeof(rd); i++) {
        rd[i] = (char)(i * 7 + (i >> 5));
    }
    CHECK(put(dump, rd, sizeof(rd)) == 0);
    CHECK(put(log, text, sizeof(text) - 1) == 0);

    /* both files, each under its own name, the dump's size and bytes kept;
       the loose dump is gone after a good zip */
    why[0] = '\0';
    CHECK(ico_frame_dump_zip(zip, dump, NULL, log, why, sizeof(why)) == 0);
    CHECK(ico_path_kind(dump, NULL, NULL) == -1);

    /* ico_dump_zip_extract gives the dump back, with its name */
    {
        size_t n = 0;
        char name[64];
        void *back = ico_dump_zip_extract(zip, &n, name, sizeof(name));

        CHECK(back != NULL && n == sizeof(rd) && memcmp(back, rd, n) == 0);
        CHECK(strcmp(name, "frame-x-v1.rddump") == 0);
        free(back);
        CHECK(ico_dump_zip_extract(log, &n, NULL, 0) == NULL);
    }
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_reader_init_file(&z, zip, 0));
    CHECK(mz_zip_reader_get_num_files(&z) == 2);
    CHECK(entry_is(&z, "frame-x-v1.rddump", rd, sizeof(rd)));
    CHECK(entry_is(&z, "ico-pc.log", text, sizeof(text) - 1));
    mz_zip_reader_end(&z);

    /* an unreadable log: the dump alone, still a zip */
    CHECK(put(dump, rd, sizeof(rd)) == 0);
    snprintf(log, sizeof(log), "%s/no-such.log", argv[1]);
    CHECK(ico_frame_dump_zip(zip, dump, NULL, log, why, sizeof(why)) == 1);
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_reader_init_file(&z, zip, 0));
    CHECK(mz_zip_reader_get_num_files(&z) == 1);
    CHECK(entry_is(&z, "frame-x-v1.rddump", rd, sizeof(rd)));
    mz_zip_reader_end(&z);

    CHECK(ico_path_kind(dump, NULL, NULL) == -1);

    /* no log named (--console): the same */
    CHECK(put(dump, rd, sizeof(rd)) == 0);
    CHECK(ico_frame_dump_zip(zip, dump, NULL, NULL, why, sizeof(why)) == 1);

    /* a zip that cannot be written (its folder is missing): refused, the
       loose dump is kept */
    CHECK(put(dump, rd, sizeof(rd)) == 0);
    {
        char bad[1300];

        snprintf(bad, sizeof(bad), "%s/no-such-folder/x.zip", argv[1]);
        why[0] = '\0';
        CHECK(ico_frame_dump_zip(bad, dump, NULL, NULL, why, sizeof(why)) == -1);
        CHECK(why[0] != '\0');
        CHECK(ico_path_kind(dump, NULL, NULL) == 0);
    }
    remove(dump);

    /* no dump: refused with a reason, no new file */
    remove(zip);
    snprintf(dump, sizeof(dump), "%s/no-such.rddump", argv[1]);
    why[0] = '\0';
    CHECK(ico_frame_dump_zip(zip, dump, NULL, NULL, why, sizeof(why)) == -1);
    CHECK(why[0] != '\0');
    CHECK(ico_path_kind(zip, NULL, NULL) == -1);

    /* the aspect sweep: five aspects' pairs under the prefix, in the zip as
       <aspect>.rddump and <aspect>.png, 4:3 the first entry (and so the
       .rddump rd_replay_tool replays) */
    {
        static const char *const names[ICO_SWEEP_COUNT] = {"4x3", "16x9", "32x9", "48x9", "60x9"};
        char prefix[1200], f[1300], png[64];
        char dumpBytes[ICO_SWEEP_COUNT][400], pngBytes[ICO_SWEEP_COUNT][200];

        CHECK(
            ico_sweep_aspect_value(0) == 4.0f / 3.0f && ico_sweep_aspect_value(1) == 16.0f / 9.0f &&
            ico_sweep_aspect_value(2) == 32.0f / 9.0f &&
            ico_sweep_aspect_value(3) == 48.0f / 9.0f && ico_sweep_aspect_value(4) == 60.0f / 9.0f);
        snprintf(prefix, sizeof(prefix), "%s/sweep-x-", argv[1]);
        snprintf(zip, sizeof(zip), "%s/sweep-x.zip", argv[1]);
        for (int i = 0; i < ICO_SWEEP_COUNT; i++) {
            CHECK(strcmp(ico_sweep_aspect_name(i), names[i]) == 0);
            for (size_t k = 0; k < sizeof(dumpBytes[i]); k++) {
                dumpBytes[i][k] = (char)(k * 3 + i);
            }
            for (size_t k = 0; k < sizeof(pngBytes[i]); k++) {
                pngBytes[i][k] = (char)(k * 5 + 7 * i);
            }
            snprintf(f, sizeof(f), "%s%s.rddump", prefix, names[i]);
            CHECK(put(f, dumpBytes[i], sizeof(dumpBytes[i])) == 0);
            snprintf(f, sizeof(f), "%s%s.png", prefix, names[i]);
            CHECK(put(f, pngBytes[i], sizeof(pngBytes[i])) == 0);
        }
        why[0] = '\0';
        CHECK(ico_sweep_zip(zip, prefix, why, sizeof(why)) == ICO_SWEEP_COUNT);
        memset(&z, 0, sizeof(z));
        CHECK(mz_zip_reader_init_file(&z, zip, 0));
        CHECK(mz_zip_reader_get_num_files(&z) == 2 * ICO_SWEEP_COUNT);
        for (int i = 0; i < ICO_SWEEP_COUNT; i++) {
            snprintf(png, sizeof(png), "%s.rddump", names[i]);
            CHECK(entry_is(&z, png, dumpBytes[i], sizeof(dumpBytes[i])));
            snprintf(png, sizeof(png), "%s.png", names[i]);
            CHECK(entry_is(&z, png, pngBytes[i], sizeof(pngBytes[i])));
        }
        mz_zip_reader_end(&z);
        {
            size_t n = 0;
            char name[64];
            void *back = ico_dump_zip_extract(zip, &n, name, sizeof(name));

            CHECK(back != NULL && n == sizeof(dumpBytes[0]) && memcmp(back, dumpBytes[0], n) == 0);
            CHECK(strcmp(name, "4x3.rddump") == 0);
            free(back);
        }
        snprintf(f, sizeof(f), "%s4x3.rddump", prefix);
        CHECK(ico_path_kind(f, NULL, NULL) == -1);
        snprintf(f, sizeof(f), "%s60x9.png", prefix);
        CHECK(ico_path_kind(f, NULL, NULL) == -1);

        /* 32:9 lost its picture: left out, the loose dump of it kept */
        for (int i = 0; i < ICO_SWEEP_COUNT; i++) {
            if (i == 2) {
                snprintf(f, sizeof(f), "%s%s.rddump", prefix, names[i]);
                CHECK(put(f, dumpBytes[i], sizeof(dumpBytes[i])) == 0);
                continue;
            }
            snprintf(f, sizeof(f), "%s%s.rddump", prefix, names[i]);
            CHECK(put(f, dumpBytes[i], sizeof(dumpBytes[i])) == 0);
            snprintf(f, sizeof(f), "%s%s.png", prefix, names[i]);
            CHECK(put(f, pngBytes[i], sizeof(pngBytes[i])) == 0);
        }
        CHECK(ico_sweep_zip(zip, prefix, why, sizeof(why)) == ICO_SWEEP_COUNT - 1);
        memset(&z, 0, sizeof(z));
        CHECK(mz_zip_reader_init_file(&z, zip, 0));
        CHECK(mz_zip_reader_get_num_files(&z) == 2 * (ICO_SWEEP_COUNT - 1));
        CHECK(find(&z, "32x9.rddump") < 0 && find(&z, "32x9.png") < 0);
        CHECK(find(&z, "48x9.png") >= 0);
        mz_zip_reader_end(&z);
        snprintf(f, sizeof(f), "%s32x9.rddump", prefix);
        CHECK(ico_path_kind(f, NULL, NULL) == 0);
        remove(f);

        /* nothing to zip: refused with a reason, no new file */
        remove(zip);
        why[0] = '\0';
        CHECK(ico_sweep_zip(zip, prefix, why, sizeof(why)) == -1);
        CHECK(why[0] != '\0');
        CHECK(ico_path_kind(zip, NULL, NULL) == -1);
    }

    test_loss(argv[1]);
    test_add(argv[1]);

    printf("dump_zip_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
