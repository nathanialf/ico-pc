/*
 * port/platform/test/android_paths_test.c
 *
 * The Android build's file layout (port/platform/android/android_paths.c),
 * on any platform: every path under the right folder, the trailing slashes
 * dropped, long folders refused rather than cut, the cache folder never the
 * files folder or inside it.
 */
#include "android_paths.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define FILES "/storage/emulated/0/Android/data/com.defnf.icopc/files"
#define CACHE "/data/user/0/com.defnf.icopc/cache"

static IcoAndroidPaths p;

static int empty(const IcoAndroidPaths *q)
{
    static const IcoAndroidPaths zero;

    return memcmp(q, &zero, sizeof(zero)) == 0;
}

static void test_layout(void)
{
    char img[ICO_ANDROID_PATH_MAX];

    CHECK(ico_android_layout(FILES, CACHE, NULL, &p) == 0);
    CHECK(strcmp(p.files, FILES) == 0);
    CHECK(strcmp(p.cache, CACHE) == 0);
    CHECK(strcmp(p.ini, FILES "/ico-pc.ini") == 0);
    CHECK(strcmp(p.config, FILES "/config.toml") == 0);
    CHECK(strcmp(p.archive, FILES "/ico.o2r") == 0);
    CHECK(strcmp(p.memcard, FILES "/memcard") == 0);
    CHECK(strcmp(p.logs, FILES "/logs") == 0);
    CHECK(strcmp(p.log, FILES "/logs/ico-pc.log") == 0);
    CHECK(strcmp(p.dumps, FILES "/dumps") == 0);
    CHECK(strcmp(p.drivers, FILES "/drivers") == 0);
    CHECK(strcmp(p.textures, FILES "/textures/SCES-50760/replacements") == 0);
    CHECK(strcmp(p.pipeline_cache, CACHE "/pipelines.vkcache") == 0);
    CHECK(ico_android_image_path(&p, NULL, "iso", img, sizeof(img)) == 0 &&
          strcmp(img, FILES "/Ico_PAL.iso") == 0);
    CHECK(ico_android_image_path(&p, NULL, "chd", img, sizeof(img)) == 0 &&
          strcmp(img, FILES "/Ico_PAL.chd") == 0);
    CHECK(ico_android_image_path(&p, NULL, "bin", img, sizeof(img)) == 0 &&
          strcmp(img, FILES "/Ico_PAL.bin") == 0);
    CHECK(ico_android_image_path(&p, NULL, "iso", img, 8) == -1 && img[0] == '\0');
    /* the Japanese disc: its serial's texture pack folder, its image name */
    CHECK(ico_android_layout(FILES, CACHE, "SCPS-11003", &p) == 0);
    CHECK(strcmp(p.textures, FILES "/textures/SCPS-11003/replacements") == 0);
    CHECK(strcmp(p.archive, FILES "/ico.o2r") == 0 && strcmp(p.ini, FILES "/ico-pc.ini") == 0);
    CHECK(ico_android_image_path(&p, "Ico_JPN", "bin", img, sizeof(img)) == 0 &&
          strcmp(img, FILES "/Ico_JPN.bin") == 0);
    CHECK(ico_android_image_path(&p, "Ico_PAL", "chd", img, sizeof(img)) == 0 &&
          strcmp(img, FILES "/Ico_PAL.chd") == 0);
    /* the Korean disc likewise */
    CHECK(ico_android_layout(FILES, CACHE, "SCPS-56001", &p) == 0);
    CHECK(strcmp(p.textures, FILES "/textures/SCPS-56001/replacements") == 0);
    CHECK(ico_android_image_path(&p, "Ico_KOR", "iso", img, sizeof(img)) == 0 &&
          strcmp(img, FILES "/Ico_KOR.iso") == 0);
    CHECK(ico_android_layout(FILES, CACHE, "SCES-50760", &p) == 0);
    CHECK(strcmp(p.textures, FILES "/textures/SCES-50760/replacements") == 0);
    /* the external folder may be missing (no shared storage): the internal
       one has the same layout */
    CHECK(ico_android_layout("/data/user/0/com.defnf.icopc/files", CACHE, NULL, &p) == 0);
    CHECK(strcmp(p.log, "/data/user/0/com.defnf.icopc/files/logs/ico-pc.log") == 0);
}

static void test_slashes(void)
{
    CHECK(ico_android_layout(FILES "//", CACHE "/", NULL, &p) == 0);
    CHECK(strcmp(p.files, FILES) == 0);
    CHECK(strcmp(p.ini, FILES "/ico-pc.ini") == 0);
    CHECK(strcmp(p.pipeline_cache, CACHE "/pipelines.vkcache") == 0);
}

static void test_refused(void)
{
    char longdir[ICO_ANDROID_PATH_MAX + 8];
    size_t n;

    CHECK(ico_android_layout(NULL, CACHE, NULL, &p) == -1 && empty(&p));
    CHECK(ico_android_layout(FILES, NULL, NULL, &p) == -1 && empty(&p));
    CHECK(ico_android_layout("", CACHE, NULL, &p) == -1 && empty(&p));
    CHECK(ico_android_layout("files", CACHE, NULL, &p) == -1 && empty(&p));
    CHECK(ico_android_layout(FILES, "cache", NULL, &p) == -1 && empty(&p));
    /* the cache folder is not the files folder or under it */
    CHECK(ico_android_layout(FILES, FILES, NULL, &p) == -1 && empty(&p));
    CHECK(ico_android_layout(FILES, FILES "/", NULL, &p) == -1 && empty(&p));
    CHECK(ico_android_layout(FILES, FILES "/cache", NULL, &p) == -1 && empty(&p));
    CHECK(ico_android_layout("/", CACHE, NULL, &p) == -1 && empty(&p));
    /* a sibling whose name starts the same is not under it */
    CHECK(ico_android_layout(FILES, FILES "-cache", NULL, &p) == 0);
    /* every path fits or the layout is refused: the longest folder whose
       textures path fits is accepted, one byte more is not */
    n = ICO_ANDROID_PATH_MAX - 1 - strlen("/textures/SCES-50760/replacements");
    memset(longdir, 'a', sizeof(longdir));
    longdir[0] = '/';
    longdir[n] = '\0';
    CHECK(ico_android_layout(longdir, CACHE, NULL, &p) == 0);
    CHECK(strlen(p.textures) == ICO_ANDROID_PATH_MAX - 1);
    longdir[n] = 'a';
    longdir[n + 1] = '\0';
    CHECK(ico_android_layout(longdir, CACHE, NULL, &p) == -1 && empty(&p));
    /* the same for the cache folder's one file */
    n = ICO_ANDROID_PATH_MAX - 1 - strlen("/pipelines.vkcache");
    longdir[0] = '/';
    memset(longdir + 1, 'c', n - 1);
    longdir[n] = '\0';
    CHECK(ico_android_layout(FILES, longdir, NULL, &p) == 0);
    longdir[n] = 'c';
    longdir[n + 1] = '\0';
    CHECK(ico_android_layout(FILES, longdir, NULL, &p) == -1 && empty(&p));
    /* a folder longer than the buffer itself */
    memset(longdir, 'a', sizeof(longdir) - 1);
    longdir[0] = '/';
    longdir[sizeof(longdir) - 1] = '\0';
    CHECK(ico_android_layout(longdir, CACHE, NULL, &p) == -1 && empty(&p));
}

int main(void)
{
    test_layout();
    test_slashes();
    test_refused();
    printf("android_paths_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
