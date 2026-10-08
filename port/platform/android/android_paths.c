/*
 * port/platform/android/android_paths.c
 *
 * The Android build's file layout (android_paths.h). No Android call:
 * compiled and tested on every platform.
 */
#include "android_paths.h"
#include <stdio.h>
#include <string.h>

/* dir + "/" + name into out; -1 (out emptied) when it does not fit */
static int join(char *out, size_t size, const char *dir, const char *name)
{
    size_t d = strlen(dir), n = strlen(name);

    if (d + 1 + n >= size) {
        out[0] = '\0';
        return -1;
    }
    memcpy(out, dir, d);
    out[d] = '/';
    memcpy(out + d + 1, name, n + 1);
    return 0;
}

/* the folder without trailing slashes ("/" stays "/"); -1 when it is
   missing, relative or does not fit */
static int folder(char *out, size_t size, const char *dir)
{
    size_t n;

    if (dir == NULL || dir[0] != '/') {
        return -1;
    }
    n = strlen(dir);
    while (n > 1 && dir[n - 1] == '/') {
        n--;
    }
    if (n >= size) {
        return -1;
    }
    memcpy(out, dir, n);
    out[n] = '\0';
    return 0;
}

/* 1 when path is dir or inside it */
static int under(const char *path, const char *dir)
{
    size_t n = strlen(dir);

    if (strcmp(dir, "/") == 0) {
        return 1;
    }
    return strncmp(path, dir, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

int ico_android_layout(const char *files, const char *cache, IcoAndroidPaths *out)
{
    IcoAndroidPaths *p = out;
    int bad = 0;

    memset(p, 0, sizeof(*p));
    if (folder(p->files, sizeof(p->files), files) != 0 ||
        folder(p->cache, sizeof(p->cache), cache) != 0 || under(p->cache, p->files)) {
        memset(p, 0, sizeof(*p));
        return -1;
    }
    bad |= join(p->ini, sizeof(p->ini), p->files, "ico-pc.ini");
    bad |= join(p->config, sizeof(p->config), p->files, "config.toml");
    bad |= join(p->archive, sizeof(p->archive), p->files, "ico.o2r");
    bad |= join(p->memcard, sizeof(p->memcard), p->files, "memcard");
    bad |= join(p->logs, sizeof(p->logs), p->files, "logs");
    bad |= join(p->log, sizeof(p->log), p->files, "logs/ico-pc.log");
    bad |= join(p->dumps, sizeof(p->dumps), p->files, "dumps");
    bad |= join(p->textures, sizeof(p->textures), p->files, "textures/SCES-50760/replacements");
    bad |= join(p->pipeline_cache, sizeof(p->pipeline_cache), p->cache, "pipelines.vkcache");
    if (bad) {
        memset(p, 0, sizeof(*p));
        return -1;
    }
    return 0;
}

int ico_android_image_path(const IcoAndroidPaths *p, const char *ext, char *out, size_t size)
{
    char name[32];
    int n;

    if (size == 0) {
        return -1;
    }
    n = snprintf(name, sizeof(name), "Ico_PAL.%s", ext);
    if (p->files[0] == '\0' || n < 0 || (size_t)n >= sizeof(name)) {
        out[0] = '\0';
        return -1;
    }
    return join(out, size, p->files, name);
}
