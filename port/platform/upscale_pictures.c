/* upscale_pictures.c: the upscaler's pictures beside F12's present picture,
 * named in the log and added to the bug-report zip (upscale_pictures.h;
 * issue 203). No renderer here (upscale_pictures_arm.c asks for them), so
 * dump_zip_test runs it without a device. */
#include "upscale_pictures.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "dump_zip.h"
#include "host_fs.h"

static const char *const kSuffix[ICO_UPSCALE_PICTURES] = {"-scene.png", "-velocity.png",
                                                          "-depth.png", "-composite.png"};

const char *ico_upscale_picture_suffix(int i)
{
    return i >= 0 && i < ICO_UPSCALE_PICTURES ? kSuffix[i] : "";
}

int ico_upscale_pictures_stem(const char *present_png, char *out, size_t size)
{
    static const char tail[] = "-present.png";
    const size_t n = present_png != NULL ? strlen(present_png) : 0;
    const size_t t = sizeof(tail) - 1;

    if (n <= t || strcmp(present_png + n - t, tail) != 0 || n - t >= size) {
        if (size > 0) {
            out[0] = '\0';
        }
        return -1;
    }
    memcpy(out, present_png, n - t);
    out[n - t] = '\0';
    return 0;
}

/* through ico_path_kind (host_fs.c): a plain stat() takes the ANSI code page
   on Windows, so a user folder such as C:\Users\José\ read as absent */
static int file_size(const char *path, uint64_t *bytes)
{
    unsigned long long size = 0;

    if (ico_path_kind(path, &size, NULL) != 0 || size == 0) {
        return -1;
    }
    *bytes = (uint64_t)size;
    return 0;
}

static const char *base_of(const char *p)
{
    const char *b = strrchr(p, '/');
#ifdef _WIN32
    const char *b2 = strrchr(p, '\\');

    b = b2 != NULL && (b == NULL || b2 > b) ? b2 : b;
#endif
    return b != NULL ? b + 1 : p;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
static void put(char *line, size_t size, size_t *at, const char *fmt, ...)
{
    if (*at < size) {
        va_list ap;

        va_start(ap, fmt);
        const int w = vsnprintf(line + *at, size - *at, fmt, ap);
        va_end(ap);
        *at += w > 0 ? (size_t)w : 0u;
    }
}

int ico_upscale_pictures_zip(const char *present_png, const char *zip, uint64_t cap,
                             uint64_t reserve, int *added, char *line, size_t size)
{
    char stem[1024], path[1100];
    int found = 0;
    size_t at = 0;

    if (added != NULL) {
        *added = 0;
    }
    if (size > 0) {
        line[0] = '\0';
    }
    if (ico_upscale_pictures_stem(present_png, stem, sizeof(stem)) != 0) {
        return 0;
    }
    const int zipping = zip != NULL && zip[0] != '\0';
    uint64_t have = 0;

    if (zipping && file_size(zip, &have) != 0) {
        put(line, size, &at, "the zip cannot be read; ");
        return 0;
    }
    for (int i = 0; i < ICO_UPSCALE_PICTURES; i++) {
        uint64_t bytes = 0;

        snprintf(path, sizeof(path), "%s%s", stem, kSuffix[i]);
        if (file_size(path, &bytes) != 0) {
            continue;
        }
        found++;
        if (!zipping) {
            put(line, size, &at, found > 1 ? ", %s" : "%s", found > 1 ? kSuffix[i] : path);
            continue;
        }
        /* a PNG deflates little more: its own size is the zip's growth at
           most (the local header and the directory entry aside) */
        if (have + bytes + reserve + 512u > cap) {
            put(line, size, &at, "%s not added (the zip would pass %llu bytes); ", base_of(path),
                (unsigned long long)cap);
            continue;
        }
        char why[160];
        size_t got = 0;

        why[0] = '\0';
        if (ico_zip_add_file(zip, path, base_of(path), why, sizeof(why), &got) == 0) {
            if (added != NULL) {
                (*added)++;
            }
            put(line, size, &at, "%s (%llu bytes) added; ", base_of(path), (unsigned long long)got);
            (void)file_size(zip, &have);
        } else {
            put(line, size, &at, "%s not added: %s; ", base_of(path), why);
        }
    }
    return found;
}

void ico_upscale_pictures_done(const char *present_png, const char *zip)
{
    char line[1400];
    int added = 0;
    uint64_t reserve = 0;

    /* the present's own picture goes into the zip after these */
    if (zip != NULL && zip[0] != '\0' && file_size(present_png, &reserve) != 0) {
        reserve = 0;
    }
    const int found = ico_upscale_pictures_zip(present_png, NULL, 0, 0, NULL, line, sizeof(line));

    if (found == 0) {
        return; /* no upscaler in force: nothing was asked of it */
    }
    fprintf(stderr,
            "window: F12: wrote %s (the upscaler's inputs and output: lines in -scene.png are the "
            "port's, lines only in -composite.png are the upscaler's)%s\n",
            line,
            found < ICO_UPSCALE_PICTURES ? "; not all four: the renderer's line says why" : "");
    if (zip != NULL && zip[0] != '\0') {
        (void)ico_upscale_pictures_zip(present_png, zip, ICO_REPORT_ZIP_CAP, reserve, &added, line,
                                       sizeof(line));
        fprintf(stderr, "window: bug report: the upscaler's pictures: %s%d of %d in %s\n", line,
                added, found, zip);
    }
}
