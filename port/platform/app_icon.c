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
#include <stdlib.h>
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
                               int contrast, int saturation, int upscaleOn)
{
    if ((crtOn && crtStrength > 0.0f) || upscaleOn) {
        return 0;
    }
    return tone_neutral(gamma) && tone_neutral(brightness) && tone_neutral(contrast) &&
           tone_neutral(saturation);
}

/* the four corner texels of a PNG this port wrote (rd_write_png_mem: 8-bit
   RGBA, filter 0, one IDAT of stored deflate blocks) into flat; 1 when all
   four are at least half opaque, 0 when not, -1 when the file is not of that
   form (kept as it is) */
int ico_app_icon_png_flat(const uint8_t *png, size_t n, uint8_t corner[4])
{
    enum { S = ICO_ICON_SIZE, ROW = S * 4 + 1, RAW = ROW * S };

    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (png == NULL || n < 8 + 25 || memcmp(png, sig, 8) != 0) {
        return -1;
    }
    const uint8_t *ihdr = png + 16;
    if (memcmp(png + 12, "IHDR", 4) != 0 || ihdr[0] || ihdr[1] || ihdr[2] != 1 || ihdr[3] ||
        ihdr[4] || ihdr[5] || ihdr[6] != 1 || ihdr[7] || ihdr[8] != 8 || ihdr[9] != 6 ||
        ihdr[12] != 0) {
        return -1;
    }
    size_t at = 8 + 25;
    while (at + 12 <= n && memcmp(png + at + 4, "IDAT", 4) != 0) {
        const size_t len = ((size_t)png[at] << 24) | ((size_t)png[at + 1] << 16) |
                           ((size_t)png[at + 2] << 8) | png[at + 3];
        if (len > n) {
            return -1;
        }
        at += 12 + len;
    }
    if (at + 12 > n) {
        return -1;
    }
    size_t len = ((size_t)png[at] << 24) | ((size_t)png[at + 1] << 16) |
                 ((size_t)png[at + 2] << 8) | png[at + 3];
    if (len > n || at + 8 + len > n || len < 6) {
        return -1;
    }
    const uint8_t *z = png + at + 8, *end = z + len;
    z += 2; /* zlib header */
    uint8_t *raw = malloc(RAW);
    if (raw == NULL) {
        return -1;
    }
    size_t got = 0;
    for (;;) {
        if (end - z < 5 || (z[0] & 6) != 0) { /* BTYPE 00 only */
            got = 0;
            break;
        }
        const int last = z[0] & 1;
        const size_t bl = (size_t)z[1] | ((size_t)z[2] << 8);
        z += 5;
        if ((size_t)(end - z) < bl || got + bl > RAW) {
            got = 0;
            break;
        }
        memcpy(raw + got, z, bl);
        got += bl;
        z += bl;
        if (last) {
            break;
        }
    }
    int r = -1;
    if (got == RAW) {
        const size_t at4[4] = {1, 1 + (S - 1) * 4, (size_t)(S - 1) * ROW + 1,
                               (size_t)(S - 1) * ROW + 1 + (S - 1) * 4};
        r = 1;
        for (int i = 0; i < 4; i++) {
            if (raw[at4[i] + 3] < 128) {
                r = 0;
            }
        }
        if (corner != NULL) {
            memcpy(corner, raw + at4[0], 4);
        }
    }
    free(raw);
    return r;
}

int ico_app_icon_drop_wash(const char *dir, const char *discId)
{
    char png[1024];
    if (dir == NULL || discId == NULL || discPath(png, sizeof(png), dir, discId, "png") != 0) {
        return 0;
    }
    FILE *f = ico_fopen(png, "rb");
    if (f == NULL) {
        return 0;
    }
    uint8_t *buf = malloc(300000);
    const size_t n = buf != NULL ? fread(buf, 1, 300000, f) : 0;
    fclose(f);
    uint8_t c[4] = {0, 0, 0, 0};
    const int flat = n > 0 ? ico_app_icon_png_flat(buf, n, c) : -1;
    free(buf);
    if (flat != 1) {
        return 0;
    }
    char other[1024];
    fprintf(stderr,
            "ico_pc: icon: the stored icon %s is a wash over the whole frame (corner colour %d %d "
            "%d, alpha %d): removed; made again at the next title screen\n",
            png, c[0], c[1], c[2], c[3]);
    (void)ico_remove(png);
    if (discPath(other, sizeof(other), dir, discId, "ico") == 0) {
        (void)ico_remove(other);
    }
    if (discPath(other, sizeof(other), dir, discId, "txt") == 0) {
        (void)ico_remove(other);
    }
    if (ico_app_icon_path_at(other, sizeof(other), dir, "ico-pc.png") == 0) {
        (void)ico_remove(other);
    }
    if (ico_app_icon_path_at(other, sizeof(other), dir, "ico-pc.ico") == 0) {
        (void)ico_remove(other);
    }
    return 1;
}
