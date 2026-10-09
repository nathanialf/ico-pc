/*
 * port/data/test/iso_import_test.c
 *
 * The Android first start's copy of the chosen disc image
 * (port/platform/android/iso_import.h), on Linux with SDL's memory streams
 * standing in for the content:// stream:
 *   copy     3 MB + 1 byte from SDL_IOFromConstMem: the file byte for byte,
 *            the .tmp gone, the progress (phase "copy", then one "save" with no
 *            byte count) never going back
 *            and ending at done == total == the size
 *   cancel   the callback stops it at half way: no file and no .tmp left
 *   fail     a copy into a read-only folder (as root, into a "folder" that
 *            is a file) returns -1 with the reason naming the path
 *   name     a name that does not end in .tmp is refused
 *   need     ico_iso_need_bytes, the space message's rounding, the
 *            extension from the header and the name
 *
 * Usage: iso_import_test <scratch dir>
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "iso_import.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define SIZE ((size_t)3 * 1024 * 1024 + 1)

typedef struct Seen {
    int calls;
    int backwards;     /* a call whose done or total was below the one before */
    int wrongPhase;    /* a phase other than "copy" and "save" */
    int saves;         /* calls with phase "save" (done and total 0) */
    int saveBad;       /* a "save" call with a byte count, or one before the last chunk */
    int copyAfterSave; /* "copy" calls after the "save" one: only the last word */
    int stopAtHalf;    /* return 1 once done reaches half the total */
    uint64_t done, total;
} Seen;

static int progress(void *ctx, const char *phase, uint64_t done, uint64_t total)
{
    Seen *s = ctx;

    if (strcmp(phase, "save") == 0) {
        /* the flush to storage, after the last chunk and before the rename */
        s->saves++;
        if (done != 0 || total != 0 || s->calls == 0) {
            s->saveBad++;
        }
        return 0;
    }
    if (strcmp(phase, "copy") != 0) {
        s->wrongPhase++;
    }
    if (s->saves > 0) {
        s->copyAfterSave++;
    }
    if (s->calls > 0 && (done < s->done || total < s->total)) {
        s->backwards++;
    }
    s->calls++;
    s->done = done;
    s->total = total;
    return s->stopAtHalf && total > 0 && done * 2 >= total;
}

static int exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

static unsigned char *pattern(void)
{
    unsigned char *p = malloc(SIZE);
    uint32_t x = 0x12345678u;
    size_t i;

    for (i = 0; p != NULL && i < SIZE; i++) {
        x = x * 1664525u + 1013904223u;
        p[i] = (unsigned char)(x >> 24);
    }
    return p;
}

static void checkCopy(const char *dir, const unsigned char *data)
{
    char tmp[1024 + 8], final[1024], why[512];
    SDL_IOStream *src = SDL_IOFromConstMem(data, SIZE);
    Seen s;
    FILE *f;
    unsigned char *back;
    size_t got = 0;
    int r;

    snprintf(final, sizeof(final), "%s/iso_import_copy.iso", dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", final);
    remove(final);
    memset(&s, 0, sizeof(s));
    r = ico_iso_copy(src, tmp, progress, &s, why, sizeof(why));
    SDL_CloseIO(src);
    CHECK(r == 0, "copy: returned %d (%s)", r, why);
    CHECK(!exists(tmp), "copy: %s is still there", tmp);
    CHECK(s.wrongPhase == 0, "copy: %d calls with another phase", s.wrongPhase);
    CHECK(s.backwards == 0, "copy: the progress went back %d times", s.backwards);
    CHECK(s.saves == 1 && s.saveBad == 0, "copy: %d save calls (%d malformed), expected one",
          s.saves, s.saveBad);
    CHECK(s.copyAfterSave <= 1, "copy: %d copy calls after the save call", s.copyAfterSave);
    /* 0, then one call a chunk: 1, 2, 3 MB and the last byte */
    CHECK(s.calls >= 5, "copy: %d progress calls", s.calls);
    CHECK(s.done == SIZE && s.total == SIZE, "copy: the last call %llu of %llu",
          (unsigned long long)s.done, (unsigned long long)s.total);
    f = fopen(final, "rb");
    CHECK(f != NULL, "copy: %s is not there", final);
    if (f == NULL) {
        return;
    }
    back = malloc(SIZE + 16);
    if (back != NULL) {
        got = fread(back, 1, SIZE + 16, f);
        CHECK(got == SIZE && memcmp(back, data, SIZE) == 0, "copy: %zu bytes read back, %s", got,
              got == SIZE ? "different" : "of the wrong size");
        free(back);
    }
    fclose(f);
    remove(final);
    printf("  copy: %zu bytes in %d progress calls\n", got, s.calls);
}

static void checkCancel(const char *dir, const unsigned char *data)
{
    char tmp[1024 + 8], final[1024], why[512];
    SDL_IOStream *src = SDL_IOFromConstMem(data, SIZE);
    Seen s;
    int r;

    snprintf(final, sizeof(final), "%s/iso_import_cancel.iso", dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", final);
    remove(final);
    memset(&s, 0, sizeof(s));
    s.stopAtHalf = 1;
    r = ico_iso_copy(src, tmp, progress, &s, why, sizeof(why));
    SDL_CloseIO(src);
    CHECK(r == 1, "cancel: returned %d (%s)", r, why);
    CHECK(s.done * 2 >= s.total && s.done < s.total, "cancel: stopped at %llu of %llu",
          (unsigned long long)s.done, (unsigned long long)s.total);
    CHECK(!exists(tmp) && !exists(final), "cancel: a file is left behind (%s %s)",
          exists(tmp) ? tmp : "", exists(final) ? final : "");
}

static void checkFail(const char *dir, const unsigned char *data)
{
    char ro[1024], tmp[1024 + 32], why[512];
    SDL_IOStream *src = SDL_IOFromConstMem(data, SIZE);
    int r, asFile = 0;

    snprintf(ro, sizeof(ro), "%s/iso_import_ro", dir);
    mkdir(ro, 0755);
    chmod(ro, 0555);
    if (access(ro, W_OK) == 0) {
        /* root writes anywhere: a "folder" that is a file fails for everyone */
        FILE *f;

        chmod(ro, 0755);
        rmdir(ro);
        snprintf(ro, sizeof(ro), "%s/iso_import_file", dir);
        f = fopen(ro, "wb");
        if (f != NULL) {
            fclose(f);
        }
        asFile = 1;
    }
    snprintf(tmp, sizeof(tmp), "%s/Ico_PAL.iso.tmp", ro);
    why[0] = '\0';
    r = ico_iso_copy(src, tmp, NULL, NULL, why, sizeof(why));
    SDL_CloseIO(src);
    CHECK(r == -1, "fail: returned %d", r);
    CHECK(why[0] != '\0' && strstr(why, tmp) != NULL, "fail: the reason \"%s\"", why);
    printf("  fail (%s): %s\n", asFile ? "a file as the folder" : "a read-only folder", why);
    if (asFile) {
        remove(ro);
    } else {
        chmod(ro, 0755);
        rmdir(ro);
    }
}

static void checkName(const char *dir, const unsigned char *data)
{
    char path[1024], why[512];
    SDL_IOStream *src = SDL_IOFromConstMem(data, 16);
    int r;

    snprintf(path, sizeof(path), "%s/iso_import_name.iso", dir);
    r = ico_iso_copy(src, path, NULL, NULL, why, sizeof(why));
    SDL_CloseIO(src);
    CHECK(r == -1 && !exists(path), "name: a name without .tmp gave %d", r);
    CHECK(ico_iso_copy(NULL, "x.tmp", NULL, NULL, why, sizeof(why)) == -1, "name: no stream");
}

static void checkNeed(void)
{
    static const unsigned char chd[8] = {'M', 'C', 'o', 'm', 'p', 'r', 'H', 'D'};
    static const unsigned char iso[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    char text[512];

    CHECK(ICO_ISO_IMPORT_SPARE_BYTES == 1200000000ull, "need: the spare is 1.2 GB");
    CHECK(ico_iso_need_bytes(0) == 1200000000ull, "need: of nothing");
    CHECK(ico_iso_need_bytes(1400000000ull) == 2600000000ull, "need: of 1.4 GB");
    /* needed rounded up, free rounded down */
    static const char want1[] = "This disc image needs 2.7 GB free in /data/files; 1.1 GB is free.";
    static const char want2[] = "This disc image needs 2.6 GB free in /f; 0.0 GB is free.";
    ico_iso_space_text(text, sizeof(text), 2600000001ull, 1199999999ull, "/data/files");
    CHECK(strncmp(text, want1, sizeof(want1) - 1) == 0, "need: the message \"%s\"", text);
    ico_iso_space_text(text, sizeof(text), 2600000000ull, 0, "/f");
    CHECK(strncmp(text, want2, sizeof(want2) - 1) == 0, "need: the message \"%s\"", text);
    CHECK(strcmp(ico_iso_ext_for(NULL, chd, sizeof(chd)), "chd") == 0, "ext: a CHD header");
    CHECK(strcmp(ico_iso_ext_for("x.chd", iso, sizeof(iso)), "iso") == 0,
          "ext: the header wins over the name");
    CHECK(strcmp(ico_iso_ext_for("content://p/document/primary%3AIco_PAL.CHD", NULL, 0), "chd") ==
              0,
          "ext: a .CHD name without a header");
    CHECK(strcmp(ico_iso_ext_for("content://p/document/1234", NULL, 0), "iso") == 0,
          "ext: a name that says nothing");
    CHECK(strcmp(ico_iso_ext_for(NULL, NULL, 0), "iso") == 0, "ext: nothing known");
    {
        static const unsigned char sync[16] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                               0xff, 0xff, 0xff, 0x00, 0x00, 0x02, 0x00, 0x02};
        static const unsigned char text[16] = {'F', 'I', 'L', 'E', ' ', '"', 'x', '"',
                                               ' ', 'B', 'I', 'N', 'A', 'R', 'Y', '\n'};

        CHECK(strcmp(ico_iso_ext_for(NULL, sync, sizeof(sync)), "bin") == 0,
              "ext: a raw CD image's sync head");
        CHECK(strcmp(ico_iso_ext_for("x.iso", sync, sizeof(sync)), "bin") == 0,
              "ext: the sync head wins over the name");
        CHECK(strcmp(ico_iso_ext_for(NULL, sync, 8), "iso") == 0,
              "ext: eight bytes cannot show the sync");
        CHECK(strcmp(ico_iso_ext_for("content://p/document/primary%3AIco_PAL.BIN", NULL, 0),
                     "bin") == 0,
              "ext: a .BIN name without a header");
        CHECK(!ico_iso_is_cue(NULL, sync, sizeof(sync)), "cue: a raw image is not a sheet");
        CHECK(!ico_iso_is_cue(NULL, iso, sizeof(iso)), "cue: an .iso head is not a sheet");
        CHECK(ico_iso_is_cue("content://p/document/primary%3AIco_PAL.CUE", NULL, 0),
              "cue: a .CUE name");
        CHECK(ico_iso_is_cue("content://p/document/1234", text, sizeof(text)),
              "cue: text starting FILE");
        CHECK(ico_iso_is_cue(NULL, "rem GENRE", 9), "cue: text starting rem");
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    unsigned char *data = pattern();

    if (data == NULL) {
        printf("FAIL: memory\n");
        return 1;
    }
    checkCopy(dir, data);
    checkCancel(dir, data);
    checkFail(dir, data);
    checkName(dir, data);
    checkNeed();
    free(data);
    if (failures) {
        printf("iso_import_test: %d failures\n", failures);
        return 1;
    }
    printf("iso_import_test: ok\n");
    return 0;
}
