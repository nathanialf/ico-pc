/* texpack_e2e_test: the texture pack end to end on the real pieces.
 *
 *   texpack_e2e_test NAMES TEXTURES WORKDIR
 *
 * NAMES is texpack_disc_test's texpack_names.txt (every texture of the
 * disc with its candidate names, as the cache's hook computes them);
 * TEXTURES a folder laid out as a "textures" folder beside the program
 * (it holds SCES-50760/replacements, e.g. build-host/tmp/texpack).  WORKDIR
 * gets a program folder whose "textures" links to TEXTURES, which
 * texpack_init walks as the game does.  Every texture's candidates are
 * looked up in that index in the hook's order (the first hit is the one
 * requested), and every file reached is read with the real loaders.
 * Then the precache at its defaults (the RAM cache's automatic limit) is
 * run over the whole pack: every PNG of the pack (its menus and subtitles)
 * must be read ahead, whatever the DDS files take.  It holds the pack in
 * memory (about 4 GB for Sad Origami's).
 * Exit 77 without the names file or the pack.  Fails when no texture
 * reaches a file, a reached file does not load, or a PNG is not read
 * ahead.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "texpack.h"

#ifndef _WIN32
#include <unistd.h>
#endif

static int readAll(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = ico_fopen(path, "rb");
    long n;
    *data = NULL;
    if (!f || fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        if (f) {
            fclose(f);
        }
        return -1;
    }
    *data = malloc((size_t)n);
    if (!*data || fread(*data, 1, (size_t)n, f) != (size_t)n) {
        free(*data);
        fclose(f);
        return -1;
    }
    fclose(f);
    *size = (size_t)n;
    return 0;
}

static int hasExt(const char *p, const char *ext)
{
    size_t n = strlen(p), e = strlen(ext);
    if (n < e) {
        return 0;
    }
    for (size_t i = 0; i < e; i++) {
        char c = p[n - e + i];
        if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != ext[i]) {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: texpack_e2e_test NAMES TEXTURES WORKDIR\n");
        return 2;
    }
    char repl[2048], prog[2048];
    snprintf(repl, sizeof(repl), "%s/SCES-50760/replacements", argv[2]);
    FILE *names = ico_fopen(argv[1], "rb");
    if (!names || ico_path_kind(repl, NULL, NULL) != 1) {
        printf("texpack_e2e_test: no names file (%s) or no pack (%s): skipped\n", argv[1], repl);
        if (names) {
            fclose(names);
        }
        return 77;
    }
#ifdef _WIN32
    printf("texpack_e2e_test: needs a folder link: skipped on Windows\n");
    fclose(names);
    return 77;
#else
    char link[2100];
    snprintf(prog, sizeof(prog), "%s/texpack_e2e_prog", argv[3]);
    snprintf(link, sizeof(link), "%s/textures", prog);
    (void)ico_mkdir(prog);
    (void)remove(link);
    if (symlink(argv[2], link) != 0) {
        printf("texpack_e2e_test: cannot link %s: skipped\n", link);
        fclose(names);
        return 77;
    }
#endif
    TexpackConfig c;
    memset(&c, 0, sizeof(c));
    c.programDir = prog;
    c.serial = "SCES-50760";
    c.bcSupported = 1;
    const int indexed = texpack_init(&c);
    TexpackStats st;
    texpack_get_stats(&st);
    printf("texpack_e2e_test: %d replacements indexed (%u files, %u duplicates, %u malformed)\n",
           indexed, st.files, st.duplicates, st.malformed);

    char *hit = calloc((size_t)(indexed > 0 ? st.files : 1) + 1, 1);
    static char line[1 << 16];
    int textures = 0, reached = 0, refused = 0, firstNotSingle = 0;
    while (fgets(line, sizeof(line), names)) {
        char *colon = strstr(line, " : ");
        if (!colon) {
            continue;
        }
        textures++;
        if (strncmp(colon + 3, "refused", 7) == 0) {
            refused++;
            continue;
        }
        /* the candidates in the hook's order: name@level[/chain] */
        for (char *tok = strtok(colon + 3, " \r\n"); tok; tok = strtok(NULL, " \r\n")) {
            char file[128];
            char *at = strchr(tok, '@');
            TexpackName tn;
            if (!at || (size_t)(at - tok) + 5 > sizeof(file)) {
                continue;
            }
            snprintf(file, sizeof(file), "%.*s.png", (int)(at - tok), tok);
            if (texpack_parse_name(file, &tn) != 0) {
                continue;
            }
            int e = texpack_lookup(&tn);
            if (e >= 0) {
                reached++;
                if (e <= (int)st.files) {
                    hit[e] = 1;
                }
                /* "@0" alone: the bound base level, the reference pack's */
                firstNotSingle += strcmp(at, "@0") != 0;
                break;
            }
        }
    }
    fclose(names);

    int entries = 0, loaded = 0, failed = 0;
    for (int e = 0; e <= (int)st.files; e++) {
        if (!hit[e]) {
            continue;
        }
        entries++;
        const char *path = texpack_entry_path(e);
        uint8_t *data;
        size_t size;
        TexpackImage img;
        int r = -1;
        if (path && readAll(path, &data, &size) == 0) {
            r = hasExt(path, ".dds") ? texpack_load_dds(data, size, 1, path, &img)
                                     : texpack_load_png(data, size, path, &img);
            free(data);
        }
        if (r == 0 && img.w > 0 && img.h > 0 && img.levels > 0) {
            loaded++;
            texpack_free_image(&img);
        } else {
            failed++;
            printf("texpack_e2e_test: %s does not load\n", path ? path : "?");
        }
    }
    free(hit);
    texpack_shutdown();
    printf("texpack_e2e_test: %d textures (%d refused); %d reach a file (%d through a name other "
           "than the base level alone); %d of %d indexed files reached, %d loaded, %d failed\n",
           textures, refused, reached, firstNotSingle, entries, indexed, loaded, failed);

    /* the precache at its defaults: every PNG read ahead */
    int precacheOk = 1;
#ifndef _WIN32
    int pngs = 0, pngsCached = 0, uiPngs = 0, uiCached = 0;
    c.precache = 1;
    c.cacheMb = 0;
    texpack_init(&c);
    for (int waited = 0; waited < 1800 * 10; waited++) {
        texpack_get_stats(&st);
        if (st.precacheDone) {
            break;
        }
        usleep(100 * 1000);
    }
    texpack_get_stats(&st);
    for (int e = 0; e <= (int)st.files; e++) {
        const char *path = texpack_entry_path(e);
        if (!path || !hasExt(path, ".png")) {
            continue;
        }
        const int ui = strstr(path, "/04 - UI/") != NULL;
        const int cached = texpack_entry_cached(e);
        pngs++;
        pngsCached += cached;
        uiPngs += ui;
        uiCached += ui && cached;
    }
    printf("texpack_e2e_test: precache (limit %llu MB): %u files read ahead, %llu MB, %u did not "
           "fit, %u failed%s; PNG files read ahead: %d of %d (\"04 - UI\": %d of %d)\n",
           (unsigned long long)(st.cacheLimit >> 20), st.cached,
           (unsigned long long)(st.cacheBytes >> 20), st.skipped, st.loadFailed,
           st.precacheDone ? "" : " (not finished)", pngsCached, pngs, uiCached, uiPngs);
    precacheOk = st.precacheDone && pngsCached == pngs;
    texpack_shutdown();
#endif
    if (reached == 0 || failed > 0 || !precacheOk) {
        printf("texpack_e2e_test: FAIL\n");
        return 1;
    }
    printf("texpack_e2e_test: ok\n");
    return 0;
}
