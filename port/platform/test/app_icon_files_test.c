/*
 * port/platform/test/app_icon_files_test.c
 *
 * The icon's files and stamp (app_icon.c) over a fake user folder under
 * the build folder: ico_app_icon_wanted_at is 1 with nothing there, 0 after
 * ico_app_icon_store_at, 1 on another label, 0 on "" (a development
 * build) with the files present and 1 on "" with the PNG gone, 1 for a
 * disc with no files and for an empty PNG; the set's paths and bytes (the
 * disc's PNG and .ico, the current copies, the stamp holding the label or
 * "dev"); no .tmp left behind; a second store replacing the first; the
 * refusals.  argv[1]: a folder the test may write in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_icon.h"
#include "host_fs.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static char s_dir[1024];

static const char *const k_names[] = {
    "ico-pc-SCES-50760.png",
    "ico-pc-SCES-50760.ico",
    "ico-pc-SCES-50760.txt",
    "ico-pc.png",
    "ico-pc.ico",
    "ico-pc-SLUS-97113.png",
    "ico-pc-SLUS-97113.txt",
};
#define NNAMES (sizeof(k_names) / sizeof(k_names[0]))

static void path(char *out, const char *name)
{
    CHECK(ico_app_icon_path_at(out, 1100, s_dir, name) == 0);
}

static int slurp(const char *name, uint8_t *buf, size_t cap, size_t *n)
{
    char p[1100];
    path(p, name);
    FILE *f = ico_fopen(p, "rb");
    if (!f) {
        return -1;
    }
    *n = fread(buf, 1, cap, f);
    fclose(f);
    return 0;
}

static int absent(const char *name)
{
    char p[1100];
    path(p, name);
    return ico_path_kind(p, NULL, NULL) == -1;
}

static void removeAll(void)
{
    for (size_t i = 0; i < NNAMES; i++) {
        char p[1100], t[1110];
        path(p, k_names[i]);
        snprintf(t, sizeof(t), "%s.tmp", p);
        (void)ico_remove(p);
        (void)ico_remove(t);
    }
}

static void checkNoTmp(void)
{
    for (size_t i = 0; i < NNAMES; i++) {
        char n[128];
        snprintf(n, sizeof(n), "%s.tmp", k_names[i]);
        CHECK(absent(n));
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: app_icon_files_test <folder>\n");
        return 2;
    }
    snprintf(s_dir, sizeof(s_dir), "%s/app_icon_files_user", argv[1]);
    (void)ico_mkdir(s_dir);
    char icon[1100];
    snprintf(icon, sizeof(icon), "%s/icon", s_dir);
    (void)ico_mkdir(icon);
    removeAll();

    const char *disc = "SCES-50760";
    uint8_t png[400];
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memcpy(png, sig, 8);
    for (int i = 8; i < 400; i++) {
        png[i] = (uint8_t)(i * 13);
    }
    uint8_t *img = calloc(256 * 256, 4);

    /* the paths */
    char p[1100];
    CHECK(ico_app_icon_path_at(p, sizeof(p), "/u", "ico-pc.png") == 0);
    CHECK(strstr(p, "icon") != NULL && strstr(p, "ico-pc.png") != NULL && strncmp(p, "/u", 2) == 0);
    char small[8];
    CHECK(ico_app_icon_path_at(small, sizeof(small), "/user/folder", "ico-pc.png") == -1);
    CHECK(small[0] == '\0');
    CHECK(ico_app_icon_path_at(p, sizeof(p), "", "x") == -1);

    /* nothing there */
    CHECK(ico_app_icon_wanted_at(s_dir, disc, "v1") == 1);
    CHECK(ico_app_icon_wanted_at(s_dir, disc, "") == 1);

    /* a store */
    CHECK(ico_app_icon_store_at(s_dir, disc, "v1", png, sizeof(png), img) == 0);
    uint8_t buf[2000];
    size_t n = 0;
    CHECK(slurp("ico-pc-SCES-50760.png", buf, sizeof(buf), &n) == 0 && n == sizeof(png) &&
          memcmp(buf, png, n) == 0);
    CHECK(slurp("ico-pc.png", buf, sizeof(buf), &n) == 0 && n == sizeof(png) &&
          memcmp(buf, png, n) == 0);
    CHECK(slurp("ico-pc-SCES-50760.ico", buf, sizeof(buf), &n) == 0 && n > 70 && buf[2] == 1 &&
          buf[4] == 4);
    size_t n2 = 0;
    uint8_t buf2[2000];
    CHECK(slurp("ico-pc.ico", buf2, sizeof(buf2), &n2) == 0 && n2 == n &&
          memcmp(buf, buf2, n) == 0);
    CHECK(slurp("ico-pc-SCES-50760.txt", buf, sizeof(buf), &n) == 0 && n == 2 &&
          memcmp(buf, "v1", 2) == 0);
    checkNoTmp();

    /* wanted */
    CHECK(ico_app_icon_wanted_at(s_dir, disc, "v1") == 0);
    CHECK(ico_app_icon_wanted_at(s_dir, disc, "v2") == 1);
    CHECK(ico_app_icon_wanted_at(s_dir, disc, "") == 0);
    CHECK(ico_app_icon_wanted_at(s_dir, disc, NULL) == 0);
    CHECK(ico_app_icon_wanted_at(s_dir, "SLUS-97113", "v1") == 1);

    /* a stamp read with a line ending still matches */
    {
        char sp[1100];
        path(sp, "ico-pc-SCES-50760.txt");
        FILE *f = ico_fopen(sp, "wb");
        CHECK(f != NULL);
        if (f) {
            fputs("v1\r\n", f);
            fclose(f);
        }
        CHECK(ico_app_icon_wanted_at(s_dir, disc, "v1") == 0);
        CHECK(ico_app_icon_wanted_at(s_dir, disc, "v1x") == 1);
    }

    /* a second store replaces; an empty label stores "dev" */
    png[20] ^= 0xFF;
    CHECK(ico_app_icon_store_at(s_dir, disc, "", png, sizeof(png), img) == 0);
    CHECK(slurp("ico-pc-SCES-50760.png", buf, sizeof(buf), &n) == 0 && memcmp(buf, png, n) == 0);
    CHECK(slurp("ico-pc-SCES-50760.txt", buf, sizeof(buf), &n) == 0 && n == 3 &&
          memcmp(buf, "dev", 3) == 0);
    CHECK(ico_app_icon_wanted_at(s_dir, disc, "dev") == 0);
    CHECK(ico_app_icon_wanted_at(s_dir, disc, "v1") == 1);
    checkNoTmp();

    /* a missing stamp: a release build wants, a development build does not */
    {
        char sp[1100];
        path(sp, "ico-pc-SCES-50760.txt");
        (void)ico_remove(sp);
        CHECK(ico_app_icon_wanted_at(s_dir, disc, "v1") == 1);
        CHECK(ico_app_icon_wanted_at(s_dir, disc, "") == 0);
    }

    /* an empty PNG is no icon; a missing one wants in a development build too */
    {
        char pp[1100];
        path(pp, "ico-pc-SCES-50760.png");
        FILE *f = ico_fopen(pp, "wb");
        if (f) {
            fclose(f);
        }
        CHECK(ico_app_icon_wanted_at(s_dir, disc, "") == 1);
        (void)ico_remove(pp);
        CHECK(ico_app_icon_wanted_at(s_dir, disc, "") == 1);
    }

    /* the refusals write nothing */
    removeAll();
    CHECK(ico_app_icon_store_at(s_dir, disc, "v1", NULL, 0, img) == -1);
    CHECK(ico_app_icon_store_at(s_dir, disc, "v1", png, sizeof(png), NULL) == -1);
    CHECK(ico_app_icon_store_at(NULL, disc, "v1", png, sizeof(png), img) == -1);
    CHECK(absent("ico-pc-SCES-50760.png") && absent("ico-pc.png"));
    CHECK(ico_app_icon_wanted_at(NULL, disc, "v1") == 0);

    /* the capture guard: a plain picture only (the game's own tone, no CRT
       filter); 0 reads as 50; a CRT filter at strength 0 presents as off */
    CHECK(ico_app_icon_picture_plain(0, 1.0f, 50, 50, 50, 50) == 1);
    CHECK(ico_app_icon_picture_plain(0, 0.0f, 0, 0, 0, 0) == 1);
    CHECK(ico_app_icon_picture_plain(1, 0.0f, 50, 50, 50, 50) == 1);
    CHECK(ico_app_icon_picture_plain(1, 1.0f, 50, 50, 50, 50) == 0);
    CHECK(ico_app_icon_picture_plain(1, 0.5f, 0, 0, 0, 0) == 0);
    CHECK(ico_app_icon_picture_plain(0, 1.0f, 49, 50, 50, 50) == 0);
    CHECK(ico_app_icon_picture_plain(0, 1.0f, 50, 51, 50, 50) == 0);
    CHECK(ico_app_icon_picture_plain(0, 1.0f, 50, 50, 1, 50) == 0);
    CHECK(ico_app_icon_picture_plain(0, 1.0f, 50, 50, 50, 100) == 0);

    removeAll();
    (void)ico_rmdir(icon);
    (void)ico_rmdir(s_dir);
    free(img);
    if (fails) {
        printf("app_icon_files_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("app_icon_files_test: ok\n");
    return 0;
}
