/*
 * port/platform/test/host_fs_test.c
 *
 * host_fs.h's helpers on a path outside ASCII (UTF-8: Latin, Greek, CJK):
 * a folder made, a file written, read back, moved over another, its kind
 * and size, removed, the folder removed. On Linux the bytes go to the file
 * system as they are; on Windows the same test goes through the wide calls.
 * Exit status 0 on success.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_fs.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int write_text(const char *path, const char *text)
{
    FILE *f = ico_fopen(path, "wb");
    size_t n = strlen(text);
    int ok;

    if (f == NULL) {
        return -1;
    }
    ok = fwrite(text, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int read_text(const char *path, char *out, size_t size)
{
    FILE *f = ico_fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        return -1;
    }
    n = fread(out, 1, size - 1, f);
    out[n] = '\0';
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    /* "Ico tmp: Yorda, Ἰκώ, イコ" under the given folder (the build tree) */
    const char *name = "ico-tmp-Yörda-\xe1\xbc\xb8\xce\xba\xcf\x8e-\xe3\x82\xa4\xe3\x82\xb3";
    const char *base = argc > 1 ? argv[1] : ".";
    char dir[1024], a[1100], b[1100], got[64];
    unsigned long long size = 0;

    snprintf(dir, sizeof(dir), "%s/%s", base, name);
    snprintf(a, sizeof(a), "%s/sävé.tmp", dir);
    snprintf(b, sizeof(b), "%s/sävé-日本.ini", dir);
    /* a leftover from an earlier failed run */
    ico_remove(a);
    ico_remove(b);
    ico_rmdir(dir);

    CHECK(ico_path_kind(dir, NULL, NULL) == -1);
    CHECK(ico_mkdir(dir) == 0);
    CHECK(ico_mkdir(dir) == -1 && errno == EEXIST);
    CHECK(ico_path_kind(dir, NULL, NULL) == 1);

    CHECK(write_text(b, "old") == 0);
    CHECK(write_text(a, "new contents") == 0);
    CHECK(ico_path_kind(a, &size, NULL) == 0 && size == 12);
    /* the temp-and-rename pattern the ini, config.toml and the card use */
    CHECK(ico_rename_replace(a, b) == 0);
    CHECK(ico_path_kind(a, NULL, NULL) == -1);
    CHECK(read_text(b, got, sizeof(got)) == 0 && strcmp(got, "new contents") == 0);

    /* a move from nothing fails with errno set (ENOENT on POSIX; the Windows
       table maps ERROR_FILE_NOT_FOUND to the same) and leaves the target */
    errno = 0;
    CHECK(ico_rename_replace(a, b) == -1 && errno == ENOENT);
    CHECK(read_text(b, got, sizeof(got)) == 0 && strcmp(got, "new contents") == 0);
    /* and a second round: write a new temp, move it over, read the new text */
    CHECK(write_text(a, "again") == 0);
    CHECK(ico_rename_replace(a, b) == 0);
    CHECK(read_text(b, got, sizeof(got)) == 0 && strcmp(got, "again") == 0);

    CHECK(ico_remove(b) == 0);
    CHECK(ico_path_kind(b, NULL, NULL) == -1);
    CHECK(ico_rmdir(dir) == 0);
    CHECK(ico_path_kind(dir, NULL, NULL) == -1);
    CHECK(ico_fopen(b, "rb") == NULL);

    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("host_fs: all passed (%s)\n", dir);
    return 0;
}
