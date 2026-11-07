/*
 * port/platform/app_icon.c
 *
 * The icon's files under <user folder>/icon/ (app_icon.h): the paths, the
 * stamp and the "wanted" test, and the writes (each as <name>.tmp moved
 * over the old file, the stamp last).  Depends on host_fs only, so the
 * tests run on a fake folder; the wrappers that read the user folder, the
 * disc and the package label are app_icon_host.c.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "app_icon.h"
#include "host_fs.h"

#ifdef _WIN32
#define SEP '\\'
#else
#define SEP '/'
#endif

static const char *labelText(const char *label)
{
    return label != NULL && label[0] != '\0' ? label : "dev";
}

int ico_app_icon_path_at(char *out, size_t size, const char *dir, const char *name)
{
    if (out == NULL || size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (dir == NULL || dir[0] == '\0' || name == NULL) {
        return -1;
    }
    int n = snprintf(out, size, "%s%cicon%c%s", dir, SEP, SEP, name);
    if (n < 0 || (size_t)n >= size) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

static int discPath(char *out, size_t size, const char *dir, const char *discId, const char *ext)
{
    char name[128];
    int n = snprintf(name, sizeof(name), "ico-pc-%s.%s", discId, ext);
    if (n < 0 || (size_t)n >= sizeof(name)) {
        if (size) {
            out[0] = '\0';
        }
        return -1;
    }
    return ico_app_icon_path_at(out, size, dir, name);
}

/* the stamp's first line (no line ending) into out; 0, or -1 when missing */
static int readStamp(const char *path, char *out, size_t size)
{
    FILE *f = ico_fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    size_t n = fread(out, 1, size - 1, f);
    fclose(f);
    out[n] = '\0';
    for (size_t i = 0; i < n; i++) {
        if (out[i] == '\n' || out[i] == '\r') {
            out[i] = '\0';
            break;
        }
    }
    return 0;
}

int ico_app_icon_wanted_at(const char *dir, const char *discId, const char *label)
{
    char png[1024], stamp[1024], text[128];
    if (dir == NULL || discId == NULL || discPath(png, sizeof(png), dir, discId, "png") != 0 ||
        discPath(stamp, sizeof(stamp), dir, discId, "txt") != 0) {
        return 0; /* nothing can be written there either */
    }
    unsigned long long size = 0;
    if (ico_path_kind(png, &size, NULL) != 0 || size == 0) {
        return 1;
    }
    if (label == NULL || label[0] == '\0') {
        return 0; /* a development build keeps what is there */
    }
    if (readStamp(stamp, text, sizeof(text)) != 0) {
        return 1;
    }
    return strcmp(text, label) != 0;
}

static int makeIconDir(const char *dir)
{
    char d[1024];
    if (ico_app_icon_path_at(d, sizeof(d), dir, "") != 0) {
        return -1;
    }
    d[strlen(d) - 1] = '\0'; /* the trailing separator */
    if (ico_mkdir(d) == 0 || errno == EEXIST || ico_path_kind(d, NULL, NULL) == 1) {
        return 0;
    }
    return -1;
}

int ico_app_icon_store_at(const char *dir, const char *discId, const char *label,
                          const uint8_t *png, size_t pngN, const uint8_t *rgba256)
{
    char pngP[1024], icoP[1024], stampP[1024], curPng[1024], curIco[1024];
    if (dir == NULL || discId == NULL || png == NULL || pngN == 0 || rgba256 == NULL ||
        discPath(pngP, sizeof(pngP), dir, discId, "png") != 0 ||
        discPath(icoP, sizeof(icoP), dir, discId, "ico") != 0 ||
        discPath(stampP, sizeof(stampP), dir, discId, "txt") != 0 ||
        ico_app_icon_path_at(curPng, sizeof(curPng), dir, "ico-pc.png") != 0 ||
        ico_app_icon_path_at(curIco, sizeof(curIco), dir, "ico-pc.ico") != 0) {
        fprintf(stderr, "ico_pc: icon: refused (%s, %s): the path does not fit\n",
                discId ? discId : "?", labelText(label));
        return -1;
    }
    const char *text = labelText(label);
    if (makeIconDir(dir) != 0) {
        fprintf(stderr, "ico_pc: icon: refused (%s, %s): the icon folder cannot be made\n", discId,
                text);
        return -1;
    }
    /* the disc's pair, then the current copies, then the stamp: the stamp
       only ever vouches for a complete set */
    if (ico_app_icon_write_atomic(pngP, png, pngN) != 0 ||
        ico_icon_write(icoP, png, pngN, rgba256) != 0 ||
        ico_app_icon_write_atomic(curPng, png, pngN) != 0 ||
        ico_icon_write(curIco, png, pngN, rgba256) != 0 ||
        ico_app_icon_write_atomic(stampP, (const uint8_t *)text, strlen(text)) != 0) {
        fprintf(stderr, "ico_pc: icon: refused (%s, %s): a file in %s could not be written\n",
                discId, text, pngP);
        return -1;
    }
    fprintf(stderr, "ico_pc: icon: written %s (%s, %s)\n", pngP, discId, text);
    return 0;
}

static int tone_neutral(int v)
{
    return v == 0 || v == 50;
}

int ico_app_icon_picture_plain(int crtOn, float crtStrength, int gamma, int brightness,
                               int contrast, int saturation)
{
    if (crtOn && crtStrength > 0.0f) {
        return 0;
    }
    return tone_neutral(gamma) && tone_neutral(brightness) && tone_neutral(contrast) &&
           tone_neutral(saturation);
}
