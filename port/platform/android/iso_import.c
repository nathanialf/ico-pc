/*
 * port/platform/android/iso_import.c
 *
 * The first start's copy of the chosen disc image (iso_import.h).
 */
#include "iso_import.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#ifndef _WIN32
#include <unistd.h> /* fsync */
#endif
#ifdef __ANDROID__
#include "host_android.h"
#endif

#define PATH_MAX_LEN 1024

static int fail(char *why, size_t n, const char *fmt, const char *a, const char *b)
{
    if (why != NULL && n > 0) {
        snprintf(why, n, fmt, a, b);
    }
    return -1;
}

int ico_iso_copy(SDL_IOStream *src, const char *dstTmp, IcoExtractProgressFn progress, void *ctx,
                 char *why, size_t n)
{
    char final[PATH_MAX_LEN];
    size_t len = dstTmp != NULL ? strlen(dstTmp) : 0;
    Sint64 size;
    uint64_t total, done = 0, lastDone = 0, lastTotal = 0;
    unsigned char *buf;
    FILE *f;
    int r = 0, reported = 0;

    if (why != NULL && n > 0) {
        why[0] = '\0';
    }
    if (src == NULL || len < 5 || strcmp(dstTmp + len - 4, ".tmp") != 0 ||
        len - 4 >= sizeof(final)) {
        return fail(why, n, "no stream, or the copy's name %s%s does not end in .tmp",
                    dstTmp != NULL ? dstTmp : "(none)", "");
    }
    memcpy(final, dstTmp, len - 4);
    final[len - 4] = '\0';
    size = SDL_GetIOSize(src);
    total = size > 0 ? (uint64_t)size : 0;
    buf = malloc(ICO_ISO_COPY_CHUNK);
    if (buf == NULL) {
        return fail(why, n, "no memory for the copy of %s%s", final, "");
    }
    f = fopen(dstTmp, "wb");
    if (f == NULL) {
        r = fail(why, n, "cannot create %s: %s", dstTmp, strerror(errno));
        free(buf);
        return r;
    }
    if (progress != NULL && progress(ctx, "copy", 0, total) != 0) {
        r = 1;
    }
    while (r == 0) {
        size_t got = SDL_ReadIO(src, buf, ICO_ISO_COPY_CHUNK);

        if (got == 0) {
            SDL_IOStatus st = SDL_GetIOStatus(src);

            if (st == SDL_IO_STATUS_NOT_READY) {
                SDL_Delay(1);
                continue;
            }
            if (st != SDL_IO_STATUS_EOF && st != SDL_IO_STATUS_READY) {
                r = fail(why, n, "cannot read the chosen file: %s%s", SDL_GetError(), "");
            }
            break;
        }
        if (fwrite(buf, 1, got, f) != got) {
            r = fail(why, n, "cannot write %s: %s", dstTmp, strerror(errno));
            break;
        }
        done += got;
        if (total != 0 && done > total) {
            total = done; /* longer than its size said: follow it */
        }
        if (progress != NULL) {
            lastDone = done;
            lastTotal = total;
            reported = 1;
            if (progress(ctx, "copy", done, total) != 0) {
                r = 1;
            }
        }
    }
    if (r == 0 && total != 0 && done < total) {
        char a[32], b[32];

        snprintf(a, sizeof(a), "%llu", (unsigned long long)done);
        snprintf(b, sizeof(b), "%llu", (unsigned long long)total);
        r = fail(why, n, "the chosen file ended after %s of its %s bytes", a, b);
    }
    if (r == 0 && progress != NULL) {
        /* flushing the copy to the storage takes a few seconds with no byte
           count; the stop answer is not used here */
        (void)progress(ctx, "save", 0, 0);
    }
    if (r == 0 && fflush(f) != 0) {
        r = fail(why, n, "cannot write %s: %s", dstTmp, strerror(errno));
    }
#ifndef _WIN32
    if (r == 0 && fsync(fileno(f)) != 0) {
        r = fail(why, n, "cannot write %s to the storage: %s", dstTmp, strerror(errno));
    }
#endif
    if (fclose(f) != 0 && r == 0) {
        r = fail(why, n, "cannot write %s: %s", dstTmp, strerror(errno));
    }
    free(buf);
    if (r == 0 && rename(dstTmp, final) != 0) {
        r = fail(why, n, "cannot rename the copy to %s: %s", final, strerror(errno));
    }
    if (r != 0) {
        remove(dstTmp);
        return r;
    }
    /* the last word: done == total, also when the size was unknown */
    if (progress != NULL && (!reported || lastDone != done || lastTotal != done)) {
        progress(ctx, "copy", done, done);
    }
    return 0;
}

uint64_t ico_iso_need_bytes(uint64_t imageBytes)
{
    return imageBytes + ICO_ISO_IMPORT_SPARE_BYTES;
}

/* bytes in tenths of a GB (10^8 bytes), rounded up */
static uint64_t tenths_up(uint64_t bytes)
{
    return (bytes + 99999999ull) / 100000000ull;
}

void ico_iso_space_text(char *out, size_t size, uint64_t needBytes, uint64_t freeBytes,
                        const char *folder)
{
    /* tenths of a GB: what is needed up, what is free down, so the two
       never read the same */
    const uint64_t need = tenths_up(needBytes), have = freeBytes / 100000000ull;

    snprintf(out, size,
             "This disc image needs %llu.%llu GB free in %s; %llu.%llu GB is free.\n"
             "Free up some space (apps, videos or downloads you no longer need) and start "
             "the game again.",
             (unsigned long long)(need / 10), (unsigned long long)(need % 10), folder,
             (unsigned long long)(have / 10), (unsigned long long)(have % 10));
}

const char *ico_iso_ext_for(const char *name, const void *head, size_t headLen)
{
    static const char magic[8] = {'M', 'C', 'o', 'm', 'p', 'r', 'H', 'D'};

    if (head != NULL && headLen >= sizeof(magic)) {
        return memcmp(head, magic, sizeof(magic)) == 0 ? "chd" : "iso";
    }
    if (name != NULL) {
        size_t len = strlen(name);

        if (len >= 4 && name[len - 4] == '.' && tolower((unsigned char)name[len - 3]) == 'c' &&
            tolower((unsigned char)name[len - 2]) == 'h' &&
            tolower((unsigned char)name[len - 1]) == 'd') {
            return "chd";
        }
    }
    return "iso";
}

#ifdef __ANDROID__

int ico_iso_import(const char *uri, char *out, size_t outSize, IcoExtractProgressFn progress,
                   void *ctx, char *why, size_t n)
{
    const IcoAndroidPaths *p = ico_android_paths();
    char tmp[PATH_MAX_LEN + 8], copyWhy[512];
    unsigned char head[8];
    size_t headLen;
    SDL_IOStream *io;
    Sint64 size;
    long long freeBytes;
    Uint64 t0;
    double secs;
    int r;

    if (out != NULL && outSize > 0) {
        out[0] = '\0';
    }
    if (p == NULL) {
        snprintf(why, n,
                 "The app cannot use its own folder for the game's files.\n"
                 "Check that the device has free space, then start the game again.");
        return ICO_ISO_IMPORT_FAILED;
    }
    io = SDL_IOFromFile(uri, "rb");
    if (io == NULL) {
        fprintf(stderr, "ico_pc: cannot open %s: %s\n", uri, SDL_GetError());
        snprintf(why, n,
                 "The chosen file cannot be opened.\n"
                 "Start the game again and choose your ICO disc image (a .iso or .chd file), "
                 "or copy it as Ico_PAL.iso into %s with a USB cable or the Files app.",
                 p->files);
        return ICO_ISO_IMPORT_FAILED;
    }
    size = SDL_GetIOSize(io);
    /* the first bytes say .chd or .iso; read again from the start */
    headLen = SDL_ReadIO(io, head, sizeof(head));
    if (SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) != 0) {
        SDL_CloseIO(io);
        io = SDL_IOFromFile(uri, "rb");
        if (io == NULL) {
            fprintf(stderr, "ico_pc: cannot open %s again: %s\n", uri, SDL_GetError());
            snprintf(why, n,
                     "The chosen file cannot be read.\n"
                     "Start the game again and choose your ICO disc image, or copy it as "
                     "Ico_PAL.iso into %s with a USB cable or the Files app.",
                     p->files);
            return ICO_ISO_IMPORT_FAILED;
        }
    }
    if (ico_android_image_path(p, ico_iso_ext_for(uri, head, headLen), out, outSize) != 0 ||
        snprintf(tmp, sizeof(tmp), "%s.tmp", out) >= (int)sizeof(tmp)) {
        SDL_CloseIO(io);
        snprintf(why, n, "The app's folder %s has too long a path for the disc image.", p->files);
        return ICO_ISO_IMPORT_FAILED;
    }
    /* an earlier start's, stopped half way: the .iso's and the .chd's, so a
       copy of the other kind does not stay behind */
    {
        static const char *const exts[2] = {"iso", "chd"};
        char other[ICO_ANDROID_PATH_MAX];
        char otherTmp[ICO_ANDROID_PATH_MAX + 8];
        int i;

        for (i = 0; i < 2; i++) {
            if (ico_android_image_path(p, exts[i], other, sizeof(other)) == 0 &&
                snprintf(otherTmp, sizeof(otherTmp), "%s.tmp", other) < (int)sizeof(otherTmp)) {
                remove(otherTmp);
            }
        }
    }
    freeBytes = ico_android_free_bytes(p->files);
    if (size > 0 && freeBytes >= 0 && (uint64_t)freeBytes < ico_iso_need_bytes((uint64_t)size)) {
        fprintf(stderr, "ico_pc: %s: %lld bytes, %lld free in %s, %llu needed\n", uri,
                (long long)size, freeBytes, p->files,
                (unsigned long long)ico_iso_need_bytes((uint64_t)size));
        SDL_CloseIO(io);
        ico_iso_space_text(why, n, ico_iso_need_bytes((uint64_t)size), (uint64_t)freeBytes,
                           p->files);
        return ICO_ISO_IMPORT_FAILED;
    }
    fprintf(stderr, "ico_pc: copying the disc image %s (%lld bytes%s; %lld free) to %s\n", uri,
            (long long)size, size > 0 ? "" : ", size unknown", freeBytes, out);
    t0 = SDL_GetTicksNS();
    r = ico_iso_copy(io, tmp, progress, ctx, copyWhy, sizeof(copyWhy));
    SDL_CloseIO(io);
    secs = (double)(SDL_GetTicksNS() - t0) / 1e9;
    if (r == 1) {
        fprintf(stderr, "ico_pc: the copy was stopped after %.1f s; nothing was kept\n", secs);
        out[0] = '\0';
        return ICO_ISO_IMPORT_CANCELLED;
    }
    if (r != 0) {
        fprintf(stderr, "ico_pc: the copy failed after %.1f s: %s\n", secs, copyWhy);
        if (size > 0) {
            const uint64_t gb10 = tenths_up(ico_iso_need_bytes((uint64_t)size));

            snprintf(why, n,
                     "The disc image could not be copied into %s.\n"
                     "Check that about %llu.%llu GB is free and that the file is still there, "
                     "then start the game again. The log says why.",
                     p->files, (unsigned long long)(gb10 / 10), (unsigned long long)(gb10 % 10));
        } else {
            snprintf(why, n,
                     "The disc image could not be copied into %s.\n"
                     "Check that there is room for the disc image and about 1.2 GB more, and "
                     "that the file is still there, then start the game again. The log says "
                     "why.",
                     p->files);
        }
        out[0] = '\0';
        return ICO_ISO_IMPORT_FAILED;
    }
    fprintf(stderr, "ico_pc: copied the disc image in %.1f s (%.1f MB/s) to %s\n", secs,
            secs > 0.0 && size > 0 ? (double)size / 1e6 / secs : 0.0, out);
    return ICO_ISO_IMPORT_OK;
}

#endif
